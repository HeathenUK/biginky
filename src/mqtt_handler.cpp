/**
 * @file mqtt_handler.cpp
 * @brief MQTT connection, publishing, and message handling implementation
 * 
 * Extracted from main_esp32p4_test.cpp as part of Priority 1 refactoring.
 */

#include "mqtt_handler.h"
#include "json_utils.h"
#include "webui_crypto.h"
#include "thumbnail_utils.h"
#include "wifi_manager.h"  // For wifiLoadCredentials, wifiConnectPersistent
#include "EL133UF1.h"
#include <Arduino.h>
#include <WiFi.h>  // For WiFi.status()
#include <time.h>
#include <string.h>
#include <vector>
#include "esp_crt_bundle.h"
#include "driver/jpeg_encode.h"
#include "ff.h"  // FatFs
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "platform_hal.h"  // For PSRAM allocation (hal_psram_malloc)
#include "lodepng_psram.h"  // Custom PSRAM allocators for lodepng (must be before lodepng.h)
#include "lodepng.h"  // For PNG encoding
#include "EL133UF1_Color.h"  // For spectra6Color global instance
#include "miniz.h"    // For zlib decompression (tinfl_decompress_mem_to_mem)
#include "cJSON.h"  // Pure C JSON library for building JSON (handles escaping, large payloads)

// MQTT configuration - hardcoded
#define MQTT_BROKER_HOSTNAME "mqtt.flespi.io"
#define MQTT_BROKER_PORT 8883
#define MQTT_CLIENT_ID "esp32p4_device"
#define MQTT_USERNAME "e2XkCCjnqSpUIxeSKB7WR7z7BWa8B6YAqYQaSKYQd0CBavgu0qeV6c2GQ6Af4i8w"
#define MQTT_PASSWORD ""
#define MQTT_TOPIC_SUBSCRIBE "devices/twilio_sms_bridge/cmd"
#define MQTT_TOPIC_WEBUI "devices/web-ui/cmd"
#define MQTT_TOPIC_PUBLISH "devices/twilio_sms_bridge/outbox"
#define MQTT_TOPIC_STATUS "devices/web-ui/status"
#define MQTT_TOPIC_THUMB "devices/web-ui/thumb"
#define MQTT_TOPIC_MEDIA "devices/web-ui/media"
#define MQTT_MAX_MESSAGE_SIZE (1024 * 1024)  // 1MB maximum message size

// MQTT runtime state
static esp_mqtt_client_handle_t mqttClient = nullptr;
static bool mqttConnected = false;
static SemaphoreHandle_t mqttPublishSem = nullptr;  // Semaphore to wait for publish completion
static int mqttPendingPublishMsgId = -1;  // Message ID we're waiting for
static char mqttBroker[128] = MQTT_BROKER_HOSTNAME;
static int mqttPort = MQTT_BROKER_PORT;
static char mqttClientId[64] = MQTT_CLIENT_ID;
static char mqttUsername[128] = MQTT_USERNAME;
static char mqttPassword[64] = MQTT_PASSWORD;
static char mqttTopicSubscribe[128] = MQTT_TOPIC_SUBSCRIBE;
static char mqttTopicWebUI[128] = MQTT_TOPIC_WEBUI;
static char mqttTopicPublish[128] = MQTT_TOPIC_PUBLISH;
static char mqttTopicStatus[128] = MQTT_TOPIC_STATUS;
static char mqttTopicThumb[128] = MQTT_TOPIC_THUMB;
static char mqttTopicMedia[128] = MQTT_TOPIC_MEDIA;
static bool mqttMessageReceived = false;
static String lastMqttMessage = "";

// Core 1 worker task for thumbnail generation, MQTT message building, and CPU-intensive operations
enum MqttWorkType {
    MQTT_WORK_THUMBNAIL,
    MQTT_WORK_MEDIA_MAPPINGS,
    MQTT_WORK_CANVAS_DECODE,  // Base64 decode + zlib decompress for canvas commands
    MQTT_WORK_PNG_ENCODE,     // PNG encoding for canvas save
    MQTT_WORK_PNG_DECODE      // PNG decoding for background images (text display)
};

// Canvas decode/encode work data structures are now defined in mqtt_handler.h

struct MqttWorkRequest {
    MqttWorkType type;
    SemaphoreHandle_t completionSem;  // Required for synchronous operations (Core 0 waits)
    bool* success;  // Optional: set result here
    union {
        CanvasDecodeWorkData* canvasDecode;  // For MQTT_WORK_CANVAS_DECODE
        PngEncodeWorkData* pngEncode;         // For MQTT_WORK_PNG_ENCODE
        PngDecodeWorkData* pngDecode;         // For MQTT_WORK_PNG_DECODE
    } data;
};

static QueueHandle_t mqttWorkQueue = nullptr;
static TaskHandle_t mqttWorkerTaskHandle = nullptr;
static bool mqttWorkerTaskInitialized = false;
static uint8_t* mqttMessageBuffer = nullptr;
static size_t mqttMessageBufferSize = 0;
static size_t mqttMessageBufferTotalLen = 0;
static size_t mqttMessageBufferUsed = 0;
static bool mqttMessageRetain = false;
static char mqttMessageTopic[128] = "";

// Forward declaration for event handler
void publishMQTTCommandCompletion(const String& commandId, const String& commandName, bool success);
static void mqttEventHandler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);

// Forward declarations for structs and functions from main file
struct MediaMapping {
    String imageName;
    String audioFile;
};
extern int loadMediaMappingsFromSD(bool autoPublish);
extern bool sdInitDirect(bool mode1bit);

// Getter functions for external access
esp_mqtt_client_handle_t getMqttClient() {
    return mqttClient;
}

bool isMqttConnected() {
    return mqttConnected;
}

const char* getMqttTopicPublish() {
    return mqttTopicPublish;
}

// Load MQTT configuration (hardcoded values)
void mqttLoadConfig() {
    strncpy(mqttBroker, MQTT_BROKER_HOSTNAME, sizeof(mqttBroker) - 1);
    mqttPort = MQTT_BROKER_PORT;
    strncpy(mqttClientId, MQTT_CLIENT_ID, sizeof(mqttClientId) - 1);
    strncpy(mqttUsername, MQTT_USERNAME, sizeof(mqttUsername) - 1);
    strncpy(mqttPassword, MQTT_PASSWORD, sizeof(mqttPassword) - 1);
    strncpy(mqttTopicSubscribe, MQTT_TOPIC_SUBSCRIBE, sizeof(mqttTopicSubscribe) - 1);
    strncpy(mqttTopicWebUI, MQTT_TOPIC_WEBUI, sizeof(mqttTopicWebUI) - 1);
    strncpy(mqttTopicPublish, MQTT_TOPIC_PUBLISH, sizeof(mqttTopicPublish) - 1);
    strncpy(mqttTopicStatus, MQTT_TOPIC_STATUS, sizeof(mqttTopicStatus) - 1);
    strncpy(mqttTopicThumb, MQTT_TOPIC_THUMB, sizeof(mqttTopicThumb) - 1);
    strncpy(mqttTopicMedia, MQTT_TOPIC_MEDIA, sizeof(mqttTopicMedia) - 1);
    Serial.printf("MQTT config (hardcoded): broker=%s, port=%d, client_id=%s\n", 
                  mqttBroker, mqttPort, mqttClientId);
}

// Save MQTT configuration (no-op, using hardcoded values)
void mqttSaveConfig() {
    Serial.println("MQTT configuration is hardcoded - edit #defines in source code to change");
}

// Connect to MQTT broker
bool mqttConnect() {
    if (strlen(mqttBroker) == 0) {
        Serial.println("No MQTT broker configured");
        return false;
    }
    
    // Create publish semaphore if not already created
    if (mqttPublishSem == nullptr) {
        mqttPublishSem = xSemaphoreCreateBinary();
        if (mqttPublishSem == nullptr) {
            Serial.println("WARNING: Failed to create MQTT publish semaphore");
        }
    }
    
    // Disconnect existing client if any
    if (mqttClient != nullptr) {
        esp_mqtt_client_stop(mqttClient);
        esp_mqtt_client_destroy(mqttClient);
        mqttClient = nullptr;
    }
    
    // Reset message state for new connection
    mqttMessageReceived = false;
    lastMqttMessage = "";
    
    // Generate unique client ID if not set
    if (strlen(mqttClientId) == 0) {
        snprintf(mqttClientId, sizeof(mqttClientId), "esp32p4_%08X", (unsigned int)ESP.getEfuseMac());
    }
    
    Serial.printf("Connecting to MQTT broker: %s:%d (TLS)\n", mqttBroker, mqttPort);
    
    // Configure MQTT client
    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.hostname = mqttBroker;
    mqtt_cfg.broker.address.port = mqttPort;
    mqtt_cfg.credentials.client_id = mqttClientId;
    
    if (strlen(mqttUsername) > 0) {
        mqtt_cfg.credentials.username = mqttUsername;
        mqtt_cfg.credentials.authentication.password = mqttPassword;
    }
    
    // Configure TLS/SSL transport
    if (mqttPort == 8883) {
        mqtt_cfg.broker.address.transport = MQTT_TRANSPORT_OVER_SSL;
        mqtt_cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    } else {
        mqtt_cfg.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
    }
    
    mqtt_cfg.session.keepalive = 60;
    mqtt_cfg.network.reconnect_timeout_ms = 0;  // Disable auto-reconnect
    mqtt_cfg.network.timeout_ms = 10000;
    mqtt_cfg.task.stack_size = 16384;  // 16KB stack for large messages
    mqtt_cfg.task.priority = 5;
    
    // Create and start MQTT client
    mqttClient = esp_mqtt_client_init(&mqtt_cfg);
    if (mqttClient == nullptr) {
        Serial.println("Failed to initialize MQTT client");
        return false;
    }
    
    // Register event handler
    esp_mqtt_client_register_event(mqttClient, MQTT_EVENT_ANY, mqttEventHandler, nullptr);
    
    // Start MQTT client
    esp_err_t err = esp_mqtt_client_start(mqttClient);
    if (err != ESP_OK) {
        Serial.printf("Failed to start MQTT client: %s\n", esp_err_to_name(err));
        esp_mqtt_client_destroy(mqttClient);
        mqttClient = nullptr;
        return false;
    }
    
    // Wait for connection to establish
    uint32_t start = millis();
    while (!mqttConnected && (millis() - start < 10000)) {
        delay(50);
    }
    
    return mqttConnected;
}

// Disconnect from MQTT broker
void mqttDisconnect() {
    if (mqttClient != nullptr) {
        Serial.println("Disconnecting from MQTT...");
        esp_mqtt_client_unregister_event(mqttClient, MQTT_EVENT_ANY, mqttEventHandler);
        delay(50);
        esp_mqtt_client_stop(mqttClient);
        delay(200);
        vTaskDelay(pdMS_TO_TICKS(50));
        esp_mqtt_client_destroy(mqttClient);
        mqttClient = nullptr;
        mqttConnected = false;
        delay(200);
        vTaskDelay(pdMS_TO_TICKS(50));
        Serial.println("MQTT disconnected and cleaned up");
    }
}

// Check for MQTT messages (non-blocking)
bool mqttCheckMessages(uint32_t timeoutMs) {
    if (mqttClient == nullptr || !mqttConnected) {
        return false;
    }
    
    uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        if (mqttMessageReceived && lastMqttMessage.length() > 0) {
            return true;
        }
        
        if (!mqttConnected || mqttClient == nullptr) {
            return false;
        }
        
        delay(25);
    }
    
    return false;
}

// Check if a large message is still being received
bool mqttIsMessageInProgress() {
    return (mqttMessageBuffer != nullptr && 
            mqttMessageBufferTotalLen > 0 && 
            mqttMessageBufferUsed < mqttMessageBufferTotalLen);
}

// Get the last received MQTT message
String mqttGetLastMessage() {
    return lastMqttMessage;
}

// MQTT event handler (continued in next part due to size)
static void mqttEventHandler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    esp_mqtt_client_handle_t client = event->client;
    
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            mqttConnected = true;
            
            // Subscribe to topics
            if (strlen(mqttTopicSubscribe) > 0) {
                int msg_id = esp_mqtt_client_subscribe(client, mqttTopicSubscribe, 1);
                Serial.printf("Subscribed to %s (msg_id: %d)\n", mqttTopicSubscribe, msg_id);
            }
            if (strlen(mqttTopicWebUI) > 0) {
                int msg_id = esp_mqtt_client_subscribe(client, mqttTopicWebUI, 1);
                Serial.printf("Subscribed to %s (msg_id: %d)\n", mqttTopicWebUI, msg_id);
            }
            
            // Check if there's a pending thumbnail to publish
            if (thumbnailPendingPublish) {
                Serial.println("Publishing pending thumbnail after MQTT reconnect...");
                delay(500);
                thumbnailPendingPublish = false;
                
                if (display.getBuffer() != nullptr) {
                    Serial.println("Regenerating thumbnail from current framebuffer...");
                    publishMQTTThumbnail();
                } else {
                    Serial.println("Framebuffer lost, loading thumbnail from SD card...");
                    if (!sdCardMounted) {
                        Serial.println("SD card not mounted - mounting now to load thumbnail...");
                        if (!sdInitDirect(false)) {
                            Serial.println("ERROR: Failed to mount SD card for thumbnail load");
                        } else {
                            Serial.println("SD card mounted successfully");
                        }
                    }
                    char* jsonFromSD = loadThumbnailFromSD();
                    if (jsonFromSD != nullptr) {
                        Serial.println("Loaded thumbnail from SD card, publishing...");
                        int msg_id = esp_mqtt_client_publish(client, mqttTopicThumb, jsonFromSD, strlen(jsonFromSD), 1, 1);
                        if (msg_id > 0) {
                            Serial.printf("Published thumbnail from SD to %s (msg_id: %d)\n", mqttTopicThumb, msg_id);
                        } else {
                            Serial.printf("Failed to publish thumbnail from SD (msg_id: %d)\n", msg_id);
                        }
                        free(jsonFromSD);
                    } else {
                        Serial.println("WARNING: Cannot publish thumbnail - SD file missing and framebuffer lost");
                    }
                }
            }
            break;
            
        case MQTT_EVENT_SUBSCRIBED:
            Serial.printf("MQTT subscription confirmed (msg_id: %d)\n", event->msg_id);
            break;
            
        case MQTT_EVENT_UNSUBSCRIBED:
            Serial.printf("MQTT unsubscribed (msg_id: %d)\n", event->msg_id);
            break;
            
        case MQTT_EVENT_PUBLISHED:
            // Message was published successfully
            Serial.printf("MQTT_EVENT_PUBLISHED: msg_id=%d, pending_msg_id=%d\n", 
                         event->msg_id, mqttPendingPublishMsgId);
            if (mqttPublishSem != nullptr && event->msg_id == mqttPendingPublishMsgId) {
                Serial.printf("MQTT message published (msg_id: %d) - signaling semaphore\n", event->msg_id);
                mqttPendingPublishMsgId = -1;
                xSemaphoreGive(mqttPublishSem);
            } else if (mqttPublishSem != nullptr) {
                Serial.printf("MQTT_EVENT_PUBLISHED: msg_id mismatch (got %d, waiting for %d)\n", 
                             event->msg_id, mqttPendingPublishMsgId);
            }
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            Serial.println("MQTT disconnected");
            mqttConnected = false;
            if (mqttMessageBuffer != nullptr) {
                free(mqttMessageBuffer);
                mqttMessageBuffer = nullptr;
                mqttMessageBufferSize = 0;
                mqttMessageBufferTotalLen = 0;
                mqttMessageBufferUsed = 0;
            }
            break;
            
        case MQTT_EVENT_DATA: {
            // Extract topic
            char topic[event->topic_len + 1];
            if (event->topic_len > 0) {
                memcpy(topic, event->topic, event->topic_len);
                topic[event->topic_len] = '\0';
            } else {
                topic[0] = '\0';
            }
            
            // Handle multi-chunk messages
            if (event->current_data_offset == 0) {
                if (mqttMessageBuffer != nullptr) {
                    free(mqttMessageBuffer);
                    mqttMessageBuffer = nullptr;
                }
                
                if (event->total_data_len > MQTT_MAX_MESSAGE_SIZE) {
                    Serial.printf("ERROR: MQTT message too large: %d bytes (max: %d)\n", 
                                 event->total_data_len, MQTT_MAX_MESSAGE_SIZE);
                    break;
                }
                
                mqttMessageBufferTotalLen = event->total_data_len;
                mqttMessageBufferUsed = 0;
                mqttMessageRetain = event->retain;
                
                if (event->topic_len > 0 && event->topic_len < sizeof(mqttMessageTopic)) {
                    memcpy(mqttMessageTopic, event->topic, event->topic_len);
                    mqttMessageTopic[event->topic_len] = '\0';
                } else {
                    mqttMessageTopic[0] = '\0';
                }
                
                mqttMessageBufferSize = event->total_data_len + 1;
                mqttMessageBuffer = (uint8_t*)malloc(mqttMessageBufferSize);
                
                if (mqttMessageBuffer == nullptr) {
                    Serial.printf("ERROR: Failed to allocate %d bytes for MQTT message buffer!\n", mqttMessageBufferSize);
                    break;
                }
                
                Serial.printf("Starting new MQTT message: total_len=%d, allocated buffer=%d bytes, retain=%d, topic='%s'\n", 
                             event->total_data_len, mqttMessageBufferSize, mqttMessageRetain ? 1 : 0, mqttMessageTopic);
            }
            
            // Append current chunk to buffer
            if (event->data_len > 0 && mqttMessageBuffer != nullptr) {
                size_t offset = event->current_data_offset;
                if (offset + event->data_len <= mqttMessageBufferSize) {
                    memcpy(mqttMessageBuffer + offset, event->data, event->data_len);
                    mqttMessageBufferUsed = offset + event->data_len;
                    if (mqttMessageBufferUsed % 51200 < event->data_len || mqttMessageBufferUsed >= event->total_data_len) {
                        Serial.printf("MQTT message progress: %d/%d bytes (%.1f%%)\n",
                                     mqttMessageBufferUsed, event->total_data_len,
                                     100.0f * mqttMessageBufferUsed / event->total_data_len);
                    }
                } else {
                    Serial.printf("ERROR: Chunk would overflow buffer! offset=%d, chunk_len=%d, buffer_size=%d\n",
                                 offset, event->data_len, mqttMessageBufferSize);
                    free(mqttMessageBuffer);
                    mqttMessageBuffer = nullptr;
                    break;
                }
            }
            
            // Check if we have the complete message
            bool messageComplete = (mqttMessageBufferUsed >= event->total_data_len);
            if (!messageComplete) {
                break;
            }
            
            // Null-terminate and process
            if (mqttMessageBuffer != nullptr) {
                mqttMessageBuffer[mqttMessageBufferUsed] = '\0';
            }
            Serial.printf("Complete MQTT message received: %d bytes\n", mqttMessageBufferUsed);
            const char* message = (const char*)mqttMessageBuffer;
            
            const char* topicToClear = (strlen(mqttMessageTopic) > 0) ? mqttMessageTopic : topic;
            bool shouldClearRetained = false;
            
            // Process retained messages
            if (mqttMessageRetain && mqttMessageBufferUsed > 0 && mqttMessageBuffer != nullptr) {
                shouldClearRetained = true;
                Serial.printf("Processing retained message: topic='%s', size=%d\n", mqttMessageTopic, mqttMessageBufferUsed);
                const char* topicToCheck = (strlen(mqttMessageTopic) > 0) ? mqttMessageTopic : topic;
                if ((strcmp(mqttMessageTopic, mqttTopicWebUI) == 0 || strcmp(topic, mqttTopicWebUI) == 0) && message[0] == '{') {
                    String jsonMessage = String((const char*)mqttMessageBuffer, mqttMessageBufferUsed);
                    Serial.printf("Received retained JSON message (web interface) on topic %s: %d bytes\n", mqttMessageTopic, mqttMessageBufferUsed);
                    
                    String command = extractJsonStringField(jsonMessage, "command");
                    if (command.length() > 0) {
                        command.toLowerCase();
                    }
                    
                    if (command == "next" || command == "canvas_display" || command == "text_display" || command == "clear") {
                        Serial.printf("Deferring heavy '%s' command to process after MQTT disconnect\n", command.c_str());
                        webUICommandPending = true;
                        pendingWebUICommand = jsonMessage;
                    } else {
                        handleWebInterfaceCommand(jsonMessage);
                    }
                } else if (strcmp(mqttMessageTopic, mqttTopicSubscribe) == 0 || strcmp(topic, mqttTopicSubscribe) == 0) {
                    lastMqttMessage = String((const char*)mqttMessageBuffer, mqttMessageBufferUsed);
                    mqttMessageReceived = true;
                }
            }
            
            // Process non-retained JSON messages
            else if (!mqttMessageRetain && messageComplete && mqttMessageBufferUsed > 0 && mqttMessageBuffer != nullptr && message[0] == '{' && strcmp(mqttMessageTopic, mqttTopicWebUI) == 0) {
                String jsonMessage = String((const char*)mqttMessageBuffer, mqttMessageBufferUsed);
                Serial.printf("Received non-retained JSON message from web UI: %d bytes\n", mqttMessageBufferUsed);
                
                // Extract command ID for tracking
                extern String lastProcessedCommandId;
                String cmdId = extractJsonStringField(jsonMessage, "id");
                if (cmdId.length() > 0) {
                    lastProcessedCommandId = cmdId;
                    Serial.printf("Command ID: %s\n", cmdId.c_str());
                }
                
                String command = extractJsonStringField(jsonMessage, "command");
                command.toLowerCase();
                
                if (command == "next" || command == "canvas_display" || command == "text_display" || command == "clear" || command == "go") {
                    Serial.printf("Deferring heavy '%s' command to process after MQTT disconnect\n", command.c_str());
                    webUICommandPending = true;
                    pendingWebUICommand = jsonMessage;
                    publishMQTTStatus();
                } else {
                    bool success = handleWebInterfaceCommand(jsonMessage);
                    // Publish completion status with command ID
                    publishMQTTCommandCompletion(cmdId, command, success);
                }
            }
            
            // Clear retained messages (must happen after processing, regardless of which path was taken)
            // Always clear retained messages to prevent them from being processed again on next reconnect
            // Check mqttMessageRetain directly (not shouldClearRetained) to ensure we clear even if message took non-retained path
            if (mqttMessageRetain && strlen(topicToClear) > 0 && client != nullptr) {
                Serial.printf("Clearing retained message on topic %s (safety measure)...\n", topicToClear);
                int msg_id = esp_mqtt_client_publish(client, topicToClear, "", 0, 1, 1);
                if (msg_id > 0) {
                    Serial.printf("Published blank retained message to clear topic %s (msg_id: %d)\n", topicToClear, msg_id);
                } else {
                    Serial.printf("ERROR: Failed to publish blank message to clear topic %s (msg_id: %d, client=%p)\n", 
                                 topicToClear, msg_id, (void*)client);
                }
            }
            
            // Free buffer after processing
            if (messageComplete && mqttMessageBuffer != nullptr) {
                free(mqttMessageBuffer);
                mqttMessageBuffer = nullptr;
                mqttMessageBufferSize = 0;
                mqttMessageBufferTotalLen = 0;
                mqttMessageBufferUsed = 0;
                mqttMessageRetain = false;
                mqttMessageTopic[0] = '\0';
            }
            break;
        }
            
        case MQTT_EVENT_ERROR:
            Serial.printf("MQTT error: %s\n", esp_err_to_name(event->error_handle->error_type));
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_ESP_TLS) {
                Serial.printf("  ESP-TLS error: 0x%x\n", event->error_handle->esp_tls_last_esp_err);
            } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                Serial.printf("  Connection refused: 0x%x\n", event->error_handle->connect_return_code);
            }
            mqttConnected = false;
            Serial.println("MQTT connection marked as failed due to error");
            break;
            
        default:
            break;
    }
}

// Publish device status to MQTT
void publishMQTTStatus() {
    Serial.println("publishMQTTStatus() called");
    if (mqttClient == nullptr) {
        Serial.println("ERROR: mqttClient is nullptr, cannot publish status");
        return;
    }
    if (!mqttConnected) {
        Serial.println("ERROR: mqttConnected is false, cannot publish status");
        return;
    }
    Serial.println("MQTT client and connection OK, building status JSON...");
    
    size_t jsonSize = 512;
    char* jsonBuffer = (char*)malloc(jsonSize);
    if (jsonBuffer == nullptr) {
        Serial.println("ERROR: Failed to allocate JSON buffer for status");
        return;
    }
    
    time_t now = time(nullptr);
    int written = 0;
    
    written = snprintf(jsonBuffer, jsonSize, "{\"timestamp\":%ld", (long)now);
    
    if (now > 1577836800) {
        struct tm tm_utc;
        gmtime_r(&now, &tm_utc);
        char timeStr[32];
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec);
        written += snprintf(jsonBuffer + written, jsonSize - written, ",\"current_time\":\"%s\"", timeStr);
    }
    
    if (g_media_mappings_loaded && g_media_mappings.size() > 0) {
        uint32_t nextIndex = (lastMediaIndex + 1) % g_media_mappings.size();
        const char* imageName = g_media_mappings[nextIndex].imageName.c_str();
        const char* audioFile = g_media_mappings[nextIndex].audioFile.c_str();
        
        if (audioFile[0] != '\0') {
            written += snprintf(jsonBuffer + written, jsonSize - written,
                               ",\"next_media\":{\"index\":%lu,\"image\":\"%s\",\"audio\":\"%s\"}",
                               (unsigned long)nextIndex, imageName, audioFile);
        } else {
            written += snprintf(jsonBuffer + written, jsonSize - written,
                               ",\"next_media\":{\"index\":%lu,\"image\":\"%s\"}",
                               (unsigned long)nextIndex, imageName);
        }
    }
    
    if (now > 1577836800) {
        struct tm tm_utc;
        gmtime_r(&now, &tm_utc);
        uint32_t sec = (uint32_t)tm_utc.tm_sec;
        uint32_t min = (uint32_t)tm_utc.tm_min;
        uint32_t interval_minutes = g_sleep_interval_minutes;
        if (interval_minutes == 0 || 60 % interval_minutes != 0) {
            interval_minutes = 1;
        }
        
        uint32_t current_slot = (min / interval_minutes) * interval_minutes;
        uint32_t next_slot = current_slot + interval_minutes;
        uint32_t sleep_s;
        if (next_slot < 60) {
            sleep_s = (next_slot - min) * 60 - sec;
        } else {
            sleep_s = (60 - min) * 60 - sec;
        }
        if (sleep_s == 0) sleep_s = interval_minutes * 60;
        if (sleep_s < 5 && sleep_s > 0) sleep_s += interval_minutes * 60;
        
        uint32_t minutes_to_add = (sleep_s + 59) / 60;
        uint32_t total_minutes = min + minutes_to_add;
        uint32_t wake_min = total_minutes % 60;
        uint32_t wake_hour = tm_utc.tm_hour + (total_minutes / 60);
        if (wake_hour >= 24) {
            wake_hour = wake_hour % 24;
        }
        
        char wakeTimeStr[16];
        snprintf(wakeTimeStr, sizeof(wakeTimeStr), "%02d:%02d", wake_hour, wake_min);
        written += snprintf(jsonBuffer + written, jsonSize - written,
                           ",\"next_wake\":\"%s\",\"sleep_interval_minutes\":%lu",
                           wakeTimeStr, (unsigned long)interval_minutes);
    }
    
    written += snprintf(jsonBuffer + written, jsonSize - written, ",\"connected\":true");
    
    if (webUICommandPending && pendingWebUICommand.length() > 0) {
        String cmdName = extractJsonStringField(pendingWebUICommand, "command");
        if (cmdName.length() == 0) {
            cmdName = "unknown";
        }
        written += snprintf(jsonBuffer + written, jsonSize - written, ",\"pending_action\":\"%s\"",
                           cmdName.c_str());
    }
    
    written += snprintf(jsonBuffer + written, jsonSize - written, "}");
    
    if (written < 0 || written >= (int)jsonSize) {
        Serial.printf("ERROR: Status JSON buffer too small (needed %d, had %d)\n", written, jsonSize);
        free(jsonBuffer);
        return;
    }
    
    String plaintextJson = String(jsonBuffer);
    String encryptedJson = encryptAndFormatMessage(plaintextJson);
    free(jsonBuffer);
    
    if (encryptedJson.length() == 0) {
        Serial.println("ERROR: Failed to encrypt status - publishing without encryption");
        return;
    }
    
    size_t encryptedLen = encryptedJson.length();
    jsonBuffer = (char*)malloc(encryptedLen + 1);
    if (!jsonBuffer) {
        Serial.println("ERROR: Failed to allocate memory for encrypted status JSON");
        return;
    }
    
    strncpy(jsonBuffer, encryptedJson.c_str(), encryptedLen);
    jsonBuffer[encryptedLen] = '\0';
    written = encryptedLen;
    
    bool isEncrypted = isEncryptionEnabled();
    Serial.printf("Publishing %s status JSON (%d bytes) to %s...\n", 
                  isEncrypted ? "encrypted" : "unencrypted", written, mqttTopicStatus);
    int msg_id = esp_mqtt_client_publish(mqttClient, mqttTopicStatus, jsonBuffer, written, 1, 1);
    if (msg_id > 0) {
        Serial.printf("Published %s status to %s (msg_id: %d)\n", 
                      isEncrypted ? "encrypted" : "unencrypted", mqttTopicStatus, msg_id);
    } else {
        Serial.printf("Failed to publish status to %s (msg_id: %d)\n", mqttTopicStatus, msg_id);
    }
    
    free(jsonBuffer);
}

// Publish command completion status to MQTT
void publishMQTTCommandCompletion(const String& commandId, const String& commandName, bool success) {
    // Always connect WiFi and MQTT if needed, then publish completion status
    // This ensures command completion is always published
    
    // Load WiFi credentials if needed
    if (!wifiLoadCredentials()) {
        Serial.println("WARNING: No WiFi credentials, cannot publish command completion");
        return;
    }
    
    // Connect to WiFi if not already connected
    bool wifiWasConnected = (WiFi.status() == WL_CONNECTED);
    
    if (!wifiWasConnected) {
        Serial.println("Connecting to WiFi for command completion publish...");
        if (!wifiConnectPersistent(5, 20000, false)) {  // 5 retries, 20s per attempt, not required
            Serial.println("WARNING: WiFi connection failed, cannot publish command completion");
            return;
        }
        Serial.println("WiFi connected for command completion publish");
    }
    
    // Load MQTT config and connect if needed
    mqttLoadConfig();
    bool mqttWasConnected = mqttConnected;
    
    if (!mqttWasConnected) {
        Serial.println("Connecting to MQTT for command completion publish...");
        if (!mqttConnect()) {
            Serial.println("WARNING: MQTT connection failed, cannot publish command completion");
            // Don't disconnect WiFi - we might want to keep it connected
            return;
        }
        Serial.println("MQTT connected for command completion publish");
    }
    
    if (mqttClient == nullptr || !mqttConnected) {
        Serial.println("ERROR: MQTT client or connection state invalid after connect attempt");
        return;
    }
    
    Serial.printf("Publishing command completion: id=%s, command=%s, success=%d\n", 
                  commandId.c_str(), commandName.c_str(), success ? 1 : 0);
    
    // Build completion status JSON
    size_t jsonSize = 512;
    char* jsonBuffer = (char*)malloc(jsonSize);
    if (jsonBuffer == nullptr) {
        Serial.println("ERROR: Failed to allocate JSON buffer for command completion");
        return;
    }
    
    time_t now = time(nullptr);
    int written = 0;
    
    written = snprintf(jsonBuffer, jsonSize, "{\"timestamp\":%ld", (long)now);
    
    // Add command completion info
    if (commandId.length() > 0) {
        written += snprintf(jsonBuffer + written, jsonSize - written, ",\"id\":\"%s\"", commandId.c_str());
    }
    if (commandName.length() > 0) {
        written += snprintf(jsonBuffer + written, jsonSize - written, ",\"command\":\"%s\"", commandName.c_str());
    }
    written += snprintf(jsonBuffer + written, jsonSize - written, ",\"command_completed\":true,\"success\":%s", success ? "true" : "false");
    
    written += snprintf(jsonBuffer + written, jsonSize - written, ",\"connected\":true}");
    
    if (written < 0 || written >= (int)jsonSize) {
        Serial.printf("ERROR: Command completion JSON buffer too small (needed %d, had %d)\n", written, jsonSize);
        free(jsonBuffer);
        return;
    }
    
    String plaintextJson = String(jsonBuffer);
    String encryptedJson = encryptAndFormatMessage(plaintextJson);
    free(jsonBuffer);
    
    if (encryptedJson.length() == 0) {
        Serial.println("ERROR: Failed to encrypt command completion - publishing without encryption");
        return;
    }
    
    size_t encryptedLen = encryptedJson.length();
    jsonBuffer = (char*)malloc(encryptedLen + 1);
    if (!jsonBuffer) {
        Serial.println("ERROR: Failed to allocate memory for encrypted command completion JSON");
        return;
    }
    
    strncpy(jsonBuffer, encryptedJson.c_str(), encryptedLen);
    jsonBuffer[encryptedLen] = '\0';
    written = encryptedLen;
    
    bool isEncrypted = isEncryptionEnabled();
    Serial.printf("Publishing %s command completion JSON (%d bytes) to %s...\n", 
                  isEncrypted ? "encrypted" : "unencrypted", written, mqttTopicStatus);
    
    // Wait for publish completion using semaphore
    if (mqttPublishSem != nullptr) {
        mqttPendingPublishMsgId = -1;  // Reset
        xSemaphoreTake(mqttPublishSem, 0);  // Clear semaphore (non-blocking)
    }
    
    int msg_id = esp_mqtt_client_publish(mqttClient, mqttTopicStatus, jsonBuffer, written, 1, 1);
    if (msg_id > 0) {
        Serial.printf("Published %s command completion to %s (msg_id: %d), waiting for confirmation...\n", 
                      isEncrypted ? "encrypted" : "unencrypted", mqttTopicStatus, msg_id);
        
        // Wait for MQTT_EVENT_PUBLISHED event (up to 5 seconds)
        if (mqttPublishSem != nullptr) {
            mqttPendingPublishMsgId = msg_id;
            if (xSemaphoreTake(mqttPublishSem, pdMS_TO_TICKS(5000)) == pdTRUE) {
                Serial.println("Command completion message confirmed published");
            } else {
                Serial.println("WARNING: Timeout waiting for command completion publish confirmation");
            }
            mqttPendingPublishMsgId = -1;
        }
    } else {
        Serial.printf("Failed to publish command completion to %s (msg_id: %d)\n", mqttTopicStatus, msg_id);
    }
    
    free(jsonBuffer);
}

// Shared buffer for parallel status preparation (Core 1 prepares, Core 0 publishes)
static char* g_preparedStatusBuffer = nullptr;
static size_t g_preparedStatusSize = 0;
static bool g_statusPrepared = false;
static TaskHandle_t g_statusPrepTaskHandle = nullptr;
static TaskHandle_t g_mainTaskHandle = nullptr;  // Main task that waits for status preparation

// Task function to prepare status JSON on Core 1 (runs in parallel with WiFi/MQTT connection)
static void statusPreparationTask(void* arg) {
    (void)arg;
    Serial.println("[Core 1] Starting status JSON preparation...");
    
    // Build status JSON (same logic as publishMQTTStatus, but without publishing)
    size_t jsonSize = 512;
    char* jsonBuffer = (char*)malloc(jsonSize);
    if (jsonBuffer == nullptr) {
        Serial.println("[Core 1] ERROR: Failed to allocate JSON buffer for status");
        g_statusPrepared = false;
        vTaskDelete(nullptr);
        return;
    }
    
    time_t now = time(nullptr);
    int written = 0;
    
    written = snprintf(jsonBuffer, jsonSize, "{\"timestamp\":%ld", (long)now);
    
    if (now > 1577836800) {
        struct tm tm_utc;
        gmtime_r(&now, &tm_utc);
        char timeStr[32];
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec);
        written += snprintf(jsonBuffer + written, jsonSize - written, ",\"current_time\":\"%s\"", timeStr);
    }
    
    if (g_media_mappings_loaded && g_media_mappings.size() > 0) {
        uint32_t nextIndex = (lastMediaIndex + 1) % g_media_mappings.size();
        const char* imageName = g_media_mappings[nextIndex].imageName.c_str();
        const char* audioFile = g_media_mappings[nextIndex].audioFile.c_str();
        
        if (audioFile[0] != '\0') {
            written += snprintf(jsonBuffer + written, jsonSize - written,
                               ",\"next_media\":{\"index\":%lu,\"image\":\"%s\",\"audio\":\"%s\"}",
                               (unsigned long)nextIndex, imageName, audioFile);
        } else {
            written += snprintf(jsonBuffer + written, jsonSize - written,
                               ",\"next_media\":{\"index\":%lu,\"image\":\"%s\"}",
                               (unsigned long)nextIndex, imageName);
        }
    }
    
    if (now > 1577836800) {
        struct tm tm_utc;
        gmtime_r(&now, &tm_utc);
        uint32_t sec = (uint32_t)tm_utc.tm_sec;
        uint32_t min = (uint32_t)tm_utc.tm_min;
        uint32_t interval_minutes = g_sleep_interval_minutes;
        if (interval_minutes == 0 || 60 % interval_minutes != 0) {
            interval_minutes = 1;
        }
        
        uint32_t current_slot = (min / interval_minutes) * interval_minutes;
        uint32_t next_slot = current_slot + interval_minutes;
        uint32_t sleep_s;
        if (next_slot < 60) {
            sleep_s = (next_slot - min) * 60 - sec;
        } else {
            sleep_s = (60 - min) * 60 - sec;
        }
        if (sleep_s == 0) sleep_s = interval_minutes * 60;
        if (sleep_s < 5 && sleep_s > 0) sleep_s += interval_minutes * 60;
        
        uint32_t minutes_to_add = (sleep_s + 59) / 60;
        uint32_t total_minutes = min + minutes_to_add;
        uint32_t wake_min = total_minutes % 60;
        uint32_t wake_hour = tm_utc.tm_hour + (total_minutes / 60);
        if (wake_hour >= 24) {
            wake_hour = wake_hour % 24;
        }
        
        char wakeTimeStr[16];
        snprintf(wakeTimeStr, sizeof(wakeTimeStr), "%02d:%02d", wake_hour, wake_min);
        written += snprintf(jsonBuffer + written, jsonSize - written,
                           ",\"next_wake\":\"%s\",\"sleep_interval_minutes\":%lu",
                           wakeTimeStr, (unsigned long)interval_minutes);
    }
    
    written += snprintf(jsonBuffer + written, jsonSize - written, ",\"connected\":true");
    
    if (webUICommandPending && pendingWebUICommand.length() > 0) {
        String cmdName = extractJsonStringField(pendingWebUICommand, "command");
        if (cmdName.length() == 0) {
            cmdName = "unknown";
        }
        written += snprintf(jsonBuffer + written, jsonSize - written, ",\"pending_action\":\"%s\"",
                           cmdName.c_str());
    }
    
    written += snprintf(jsonBuffer + written, jsonSize - written, "}");
    
    if (written < 0 || written >= (int)jsonSize) {
        Serial.printf("[Core 1] ERROR: Status JSON buffer too small (needed %d, had %d)\n", written, jsonSize);
        free(jsonBuffer);
        g_statusPrepared = false;
        vTaskDelete(nullptr);
        return;
    }
    
    // Encrypt the JSON
    String plaintextJson = String(jsonBuffer);
    String encryptedJson = encryptAndFormatMessage(plaintextJson);
    free(jsonBuffer);
    
    if (encryptedJson.length() == 0) {
        Serial.println("[Core 1] ERROR: Failed to encrypt status");
        g_statusPrepared = false;
        vTaskDelete(nullptr);
        return;
    }
    
    // Store encrypted result in shared buffer
    size_t encryptedLen = encryptedJson.length();
    if (g_preparedStatusBuffer != nullptr) {
        free(g_preparedStatusBuffer);
    }
    g_preparedStatusBuffer = (char*)malloc(encryptedLen + 1);
    if (g_preparedStatusBuffer == nullptr) {
        Serial.println("[Core 1] ERROR: Failed to allocate memory for encrypted status JSON");
        g_statusPrepared = false;
        vTaskDelete(nullptr);
        return;
    }
    
    strncpy(g_preparedStatusBuffer, encryptedJson.c_str(), encryptedLen);
    g_preparedStatusBuffer[encryptedLen] = '\0';
    g_preparedStatusSize = encryptedLen;
    g_statusPrepared = true;
    
    Serial.printf("[Core 1] Status JSON prepared (%d bytes, encrypted)\n", encryptedLen);
    
    // Signal completion to main task
    if (g_mainTaskHandle != nullptr) {
        xTaskNotify(g_mainTaskHandle, 1, eSetBits);
    }
    
    vTaskDelete(nullptr);
}

// Start parallel status preparation on Core 1
bool prepareStatusJsonParallel() {
    // Clear any previous preparation
    if (g_preparedStatusBuffer != nullptr) {
        free(g_preparedStatusBuffer);
        g_preparedStatusBuffer = nullptr;
    }
    g_preparedStatusSize = 0;
    g_statusPrepared = false;
    
    // Store main task handle so status prep task can notify it
    g_mainTaskHandle = xTaskGetCurrentTaskHandle();
    
    // Create task on Core 1 to prepare status in parallel
    BaseType_t result = xTaskCreatePinnedToCore(
        statusPreparationTask,
        "status_prep",
        16384,  // Stack size
        nullptr,
        5,  // Priority (same as main task)
        &g_statusPrepTaskHandle,
        1  // Core 1
    );
    
    if (result != pdPASS) {
        Serial.println("ERROR: Failed to create status preparation task");
        g_mainTaskHandle = nullptr;
        return false;
    }
    
    Serial.println("[Core 0] Started status preparation task on Core 1");
    return true;
}

// Wait for status preparation to complete and publish it
bool publishPreparedStatus() {
    if (mqttClient == nullptr || !mqttConnected) {
        Serial.println("ERROR: MQTT not connected, cannot publish prepared status");
        return false;
    }
    
    // Wait for status preparation to complete (with timeout)
    // Note: We wait on our own task handle, which the status prep task will notify
    uint32_t notificationValue = 0;
    if (g_statusPrepTaskHandle != nullptr) {
        if (xTaskNotifyWait(0, ULONG_MAX, &notificationValue, pdMS_TO_TICKS(5000)) == pdTRUE) {
            Serial.println("[Core 0] Status preparation completed");
        } else {
            Serial.println("[Core 0] WARNING: Status preparation timeout");
            // Clean up task handles
            g_statusPrepTaskHandle = nullptr;
            g_mainTaskHandle = nullptr;
            return false;
        }
    }
    
    if (!g_statusPrepared || g_preparedStatusBuffer == nullptr) {
        Serial.println("ERROR: Status not prepared or buffer is null");
        return false;
    }
    
    // Publish the pre-prepared status
    Serial.printf("Publishing pre-prepared encrypted status JSON (%d bytes) to %s...\n", 
                  g_preparedStatusSize, mqttTopicStatus);
    int msg_id = esp_mqtt_client_publish(mqttClient, mqttTopicStatus, g_preparedStatusBuffer, 
                                         g_preparedStatusSize, 1, 1);
    if (msg_id > 0) {
        bool isEncrypted = isEncryptionEnabled();
        Serial.printf("Published %s status to %s (msg_id: %d)\n", 
                      isEncrypted ? "encrypted" : "unencrypted", mqttTopicStatus, msg_id);
        
        // Clean up
        free(g_preparedStatusBuffer);
        g_preparedStatusBuffer = nullptr;
        g_preparedStatusSize = 0;
        g_statusPrepared = false;
        g_statusPrepTaskHandle = nullptr;
        g_mainTaskHandle = nullptr;
        
        return true;
    } else {
        Serial.printf("Failed to publish status to %s (msg_id: %d)\n", mqttTopicStatus, msg_id);
        return false;
    }
}

// Internal implementation of thumbnail generation (runs on Core 1)
static void publishMQTTThumbnailInternalImpl() {
    if (display.getBuffer() == nullptr) {
        Serial.println("[Core 1] WARNING: Display buffer is nullptr, cannot generate thumbnail");
        return;
    }
    
    // Ensure SD card is mounted (required for saving thumbnail)
    if (!sdCardMounted) {
        Serial.println("[Core 1] SD card not mounted, attempting to mount for thumbnail save...");
        if (!sdInitDirect(false)) {
            Serial.println("[Core 1] WARNING: Failed to mount SD card, thumbnail will not be saved to SD");
            // Continue anyway - we can still publish the thumbnail via MQTT
        } else {
            Serial.println("[Core 1] SD card mounted successfully for thumbnail save");
        }
    }
    
    const int srcWidth = 1600;
    const int srcHeight = 1200;
    // Use native size for preview thumbnail (no scaling)
    const int thumbWidth = srcWidth;
    const int thumbHeight = srcHeight;
    
    bool isARGBMode = false;
#if EL133UF1_USE_ARGB8888
    isARGBMode = display.isARGBMode();
#endif
    
    // Allocate buffer for RGB values (3 bytes per pixel) - we'll convert palette indices to RGB
    // Optimization #1: Allocate buffer for palette indices directly (1 byte per pixel instead of 3)
    // This reduces memory usage by 3x and eliminates redundant RGB conversion
    size_t thumbSize = thumbWidth * thumbHeight;
    uint8_t* thumbBuffer = (uint8_t*)hal_psram_malloc(thumbSize);
    if (thumbBuffer == nullptr) {
        Serial.println("[Core 1] ERROR: Failed to allocate PSRAM for thumbnail buffer");
        return;
    }
    
    Serial.printf("[Core 1] Generating native-size thumbnail: %dx%d (mode: %s, palette-based)\n", 
                  thumbWidth, thumbHeight, isARGBMode ? "ARGB8888" : "L8");
    
    uint32_t convertStart = millis();
    
    // Optimization #2: Lookup table instead of switch statement (faster, no branching)
    // EL133UF1 colors: BLACK=0, WHITE=1, YELLOW=2, RED=3, BLUE=5, GREEN=6
    // Palette indices: 0=BLACK, 1=WHITE, 2=YELLOW, 3=RED, 4=BLUE, 5=GREEN
    static const uint8_t einkToPaletteLUT[8] = {
        0,  // 0 → 0 (BLACK)
        1,  // 1 → 1 (WHITE)
        2,  // 2 → 2 (YELLOW)
        3,  // 3 → 3 (RED)
        1,  // 4 → 1 (WHITE, invalid e-ink color)
        4,  // 5 → 4 (BLUE)
        5,  // 6 → 5 (GREEN)
        1   // 7 → 1 (WHITE, invalid e-ink color)
    };
    
    // Optimization #3: Process in cache-friendly row-by-row order (already optimal)
    // Extract e-ink color indices and convert directly to palette indices (no RGB conversion)
    if (isARGBMode) {
#if EL133UF1_USE_ARGB8888
        uint32_t* argbBuffer = display.getBufferARGB();
        if (argbBuffer != nullptr) {
            for (int y = 0; y < thumbHeight; y++) {
                for (int x = 0; x < thumbWidth; x++) {
                    int srcIdx = y * srcWidth + x;
                    uint32_t argb = argbBuffer[srcIdx];
                    uint8_t einkColor = EL133UF1::argbToColor(argb);
                    // Direct LUT lookup - no branching, no RGB conversion
                    thumbBuffer[y * thumbWidth + x] = einkToPaletteLUT[einkColor & 0x07];
                }
            }
        }
#endif
    } else {
        uint8_t* framebuffer = display.getBuffer();
        if (framebuffer != nullptr) {
            for (int y = 0; y < thumbHeight; y++) {
                for (int x = 0; x < thumbWidth; x++) {
                    int srcIdx = y * srcWidth + x;
                    uint8_t einkColor = framebuffer[srcIdx] & 0x07;
                    // Direct LUT lookup - no branching, no RGB conversion
                    thumbBuffer[y * thumbWidth + x] = einkToPaletteLUT[einkColor];
                }
            }
        }
    }
    
    uint32_t convertTime = millis() - convertStart;
    Serial.printf("[Core 1] Color conversion completed: %lu ms (processing %d pixels, direct palette indices)\n", 
                  convertTime, thumbWidth * thumbHeight);
    
    // Encode to PNG using palette-based encoding (PNG8) - direct palette index input
    unsigned char* pngData = nullptr;
    size_t pngSize = 0;
    
    uint32_t encodeStart = millis();
    
    // Set up LodePNGState with palette mode
    LodePNGState state;
    lodepng_state_init(&state);
    
    // Configure color mode for palette - input is already palette indices!
    state.info_png.color.colortype = LCT_PALETTE;
    state.info_png.color.bitdepth = 8; // 8-bit palette indices
    state.info_raw.colortype = LCT_PALETTE; // Input is palette indices (1 byte per pixel) - optimization #1
    state.info_raw.bitdepth = 8;
    
    // Disable auto-convert to ensure palette mode is used
    state.encoder.auto_convert = 0;
    
    // Add 6 colors to palette (matching useDefaultPalette() in EL133UF1_Color.cpp)
    lodepng_palette_clear(&state.info_png.color);
    lodepng_palette_add(&state.info_png.color, 10, 10, 10, 255);      // 0: BLACK
    lodepng_palette_add(&state.info_png.color, 245, 245, 235, 255);   // 1: WHITE
    lodepng_palette_add(&state.info_png.color, 245, 210, 50, 255);    // 2: YELLOW
    lodepng_palette_add(&state.info_png.color, 190, 60, 55, 255);     // 3: RED
    lodepng_palette_add(&state.info_png.color, 45, 75, 160, 255);     // 4: BLUE
    lodepng_palette_add(&state.info_png.color, 55, 140, 85, 255);      // 5: GREEN
    
    // Encode using palette mode - lodepng will convert RGB to palette indices
    unsigned error = lodepng_encode(&pngData, &pngSize, thumbBuffer, 
                                   (unsigned)thumbWidth, (unsigned)thumbHeight, 
                                   &state);
    
    lodepng_state_cleanup(&state);
    hal_psram_free(thumbBuffer);
    
    if (error) {
        Serial.printf("[Core 1] ERROR: PNG palette encoding failed: %u %s\n", 
                     error, lodepng_error_text(error));
        if (pngData) lodepng_free(pngData);
        return;
    }
    
    if (!pngData || pngSize == 0) {
        Serial.println("[Core 1] ERROR: PNG palette encoding returned empty data");
        if (pngData) lodepng_free(pngData);
        return;
    }
    
    uint32_t encodeTime = millis() - encodeStart;
    Serial.printf("[Core 1] PNG palette encoded successfully: %zu bytes (native %dx%d) in %lu ms\n", 
                  pngSize, thumbWidth, thumbHeight, encodeTime);
    
    // Use the encoded PNG data directly (no need for processPngEncodeWork)
    unsigned char* pngBuffer = pngData;
    
    size_t pngSize_u32 = (size_t)pngSize;
    // Base64 encoding: 4 output bytes for every 3 input bytes, rounded up
    size_t base64Size = ((pngSize_u32 + 2) / 3) * 4 + 1;
    char* base64Buffer = (char*)malloc(base64Size);
    if (base64Buffer == nullptr) {
        Serial.println("ERROR: Failed to allocate base64 buffer");
        lodepng_free(pngBuffer);
        return;
    }
    
    const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t base64Idx = 0;
    
    for (size_t i = 0; i < pngSize_u32; i += 3) {
        uint32_t b0 = pngBuffer[i];
        uint32_t b1 = (i + 1 < pngSize_u32) ? pngBuffer[i + 1] : 0;
        uint32_t b2 = (i + 2 < pngSize_u32) ? pngBuffer[i + 2] : 0;
        uint32_t value = (b0 << 16) | (b1 << 8) | b2;
        
        if (base64Idx + 4 < base64Size) {
            base64Buffer[base64Idx++] = base64_chars[(value >> 18) & 0x3F];
            base64Buffer[base64Idx++] = base64_chars[(value >> 12) & 0x3F];
            base64Buffer[base64Idx++] = (i + 1 < pngSize_u32) ? base64_chars[(value >> 6) & 0x3F] : '=';
            base64Buffer[base64Idx++] = (i + 2 < pngSize_u32) ? base64_chars[value & 0x3F] : '=';
        }
    }
    base64Buffer[base64Idx] = '\0';
    lodepng_free(pngBuffer);
    
    // Calculate JSON size more accurately:
    // {"width":1600,"height":1200,"format":"png","palette":true,"data":"<base64>"}
    // Overhead: ~70 bytes for JSON structure + width/height digits
    // Add 20% safety margin for base64 length variations
    size_t jsonSize = 70 + base64Idx + (base64Idx / 5) + 1;  // +20% margin
    char* jsonBuffer = (char*)malloc(jsonSize);
    if (jsonBuffer == nullptr) {
        Serial.println("ERROR: Failed to allocate JSON buffer");
        free(base64Buffer);
        return;
    }
    
    int written = snprintf(jsonBuffer, jsonSize, 
                          "{\"width\":%d,\"height\":%d,\"format\":\"png\",\"palette\":true,\"data\":\"%s\"}",
                          thumbWidth, thumbHeight, base64Buffer);
    
    if (written < 0 || written >= (int)jsonSize) {
        Serial.printf("ERROR: JSON buffer too small (needed %d, had %d)\n", written, jsonSize);
        free(jsonBuffer);
        free(base64Buffer);
        return;
    }
    
    free(base64Buffer);
    String plaintextJson = String(jsonBuffer);
    String encryptedJson = encryptAndFormatMessage(plaintextJson);
    free(jsonBuffer);
    
    if (encryptedJson.length() == 0) {
        Serial.println("ERROR: Failed to encrypt thumbnail - publishing without encryption");
        return;
    }
    
    size_t encryptedLen = encryptedJson.length();
    jsonBuffer = (char*)malloc(encryptedLen + 1);
    if (!jsonBuffer) {
        Serial.println("ERROR: Failed to allocate memory for encrypted JSON");
        return;
    }
    
    strncpy(jsonBuffer, encryptedJson.c_str(), encryptedLen);
    jsonBuffer[encryptedLen] = '\0';
    written = encryptedLen;
    
    bool isEncrypted = isEncryptionEnabled();
    Serial.printf("[Core 1] Publishing %s thumbnail JSON (%d bytes) to %s...\n", 
                  isEncrypted ? "encrypted" : "unencrypted", written, mqttTopicThumb);
    int msg_id = esp_mqtt_client_publish(mqttClient, mqttTopicThumb, jsonBuffer, written, 1, 1);
    if (msg_id > 0) {
        bool isEncrypted = isEncryptionEnabled();
        Serial.printf("\n[Core 1] Published %s thumbnail to %s (msg_id: %d)\n", 
                      isEncrypted ? "encrypted" : "unencrypted", mqttTopicThumb, msg_id);
    } else {
        Serial.printf("[Core 1] Failed to publish thumbnail to %s (msg_id: %d)\n", mqttTopicThumb, msg_id);
    }
    
    free(jsonBuffer);
}

// Public function - queues work to Core 1 worker task
void publishMQTTThumbnail() {
    if (mqttClient == nullptr || !mqttConnected) {
        return;
    }
    
    // Initialize worker task if not already done
    if (!mqttWorkerTaskInitialized) {
        initMqttWorkerTask();
    }
    
    if (mqttWorkQueue == nullptr) {
        Serial.println("WARNING: MQTT work queue not initialized, falling back to synchronous");
        publishMQTTThumbnailInternalImpl();
        return;
    }
    
    // Create work request
    MqttWorkRequest request;
    request.type = MQTT_WORK_THUMBNAIL;
    request.success = nullptr;
    request.completionSem = nullptr;  // Fire and forget for thumbnails
    
    // Queue work request (non-blocking)
    if (xQueueSend(mqttWorkQueue, &request, 0) != pdTRUE) {
        Serial.println("WARNING: MQTT work queue full, falling back to synchronous");
        publishMQTTThumbnailInternalImpl();
        return;
    }
    
    Serial.println("Queued thumbnail generation to Core 1 worker task");
}

void publishMQTTThumbnailIfConnected() {
    if (mqttConnected) {
        publishMQTTThumbnail();
    } else {
        Serial.println("MQTT not connected - generating thumbnail and saving to SD card for later publish...");
        if (display.getBuffer() == nullptr) {
            Serial.println("WARNING: Display buffer is nullptr, cannot generate thumbnail");
            thumbnailPendingPublish = true;
            return;
        }
        thumbnailPendingPublish = true;
    }
}

void publishMQTTThumbnailAlways() {
    // Always connect WiFi and MQTT if needed, then publish thumbnail
    // This ensures thumbnails are published after every display update
    
    // Check if display buffer is available
    if (display.getBuffer() == nullptr) {
        Serial.println("WARNING: Display buffer is nullptr, cannot generate thumbnail");
        thumbnailPendingPublish = true;
        return;
    }
    
    // Load WiFi credentials if needed
    if (!wifiLoadCredentials()) {
        Serial.println("WARNING: No WiFi credentials, cannot publish thumbnail");
        thumbnailPendingPublish = true;
        return;
    }
    
    // Connect to WiFi if not already connected
    bool wifiWasConnected = (WiFi.status() == WL_CONNECTED);
    
    if (!wifiWasConnected) {
        Serial.println("Connecting to WiFi for thumbnail publish...");
        if (!wifiConnectPersistent(5, 20000, false)) {  // 5 retries, 20s per attempt, not required
            Serial.println("WARNING: WiFi connection failed, saving thumbnail to SD for later publish");
            thumbnailPendingPublish = true;
            return;
        }
        Serial.println("WiFi connected for thumbnail publish");
    }
    
    // Load MQTT config and connect if needed
    mqttLoadConfig();
    bool mqttWasConnected = mqttConnected;
    
    if (!mqttWasConnected) {
        Serial.println("Connecting to MQTT for thumbnail publish...");
        if (!mqttConnect()) {
            Serial.println("WARNING: MQTT connection failed, saving thumbnail to SD for later publish");
            thumbnailPendingPublish = true;
            // Don't disconnect WiFi - we might want to keep it connected
            return;
        }
        Serial.println("MQTT connected for thumbnail publish");
    }
    
    // Now publish the thumbnail
    // Since we're already connected and this is called from display.update(),
    // call the internal implementation directly (synchronous) to ensure it publishes immediately
    // The thumbnail generation is CPU-intensive but we want it to complete before returning
    publishMQTTThumbnailInternalImpl();
    
    // Note: We intentionally do NOT disconnect WiFi/MQTT here
    // This allows them to stay connected for subsequent operations
    // The user requested considering never disconnecting WiFi
}

// Forward declarations (must be before mqttWorkerTask which uses them)
static void publishMQTTMediaMappingsInternalImpl();
static bool processCanvasDecodeWork(CanvasDecodeWorkData* work);
static bool processPngDecodeWork(PngDecodeWorkData* work);
void publishMQTTCommandCompletion(const String& commandId, const String& commandName, bool success);

// Process PNG decode work on Core 1 (defined before mqttWorkerTask)
static bool processPngDecodeWork(PngDecodeWorkData* work) {
    if (work == nullptr || work->pngData == nullptr || work->pngDataLen == 0) {
        Serial.println("[Core 1] ERROR: Invalid PNG decode work data");
        return false;
    }
    
    Serial.printf("[Core 1] Decoding PNG (len=%zu)...\n", work->pngDataLen);
    
    // Decode PNG to RGBA8888 using lodepng
    unsigned char* rgbaData = nullptr;
    unsigned width = 0;
    unsigned height = 0;
    unsigned error = lodepng_decode32(&rgbaData, &width, &height, work->pngData, work->pngDataLen);
    
    if (error) {
        Serial.printf("[Core 1] ERROR: PNG decoding failed: %u %s\n", error, lodepng_error_text(error));
        work->error = error;
        work->success = false;
        return false;
    }
    
    if (!rgbaData || width == 0 || height == 0) {
        Serial.println("[Core 1] ERROR: PNG decoding returned empty data");
        work->error = 1;
        work->success = false;
        return false;
    }
    
    work->rgbaData = rgbaData;
    work->width = width;
    work->height = height;
    work->error = 0;
    work->success = true;
    Serial.printf("[Core 1] PNG decoded: %ux%u RGBA8888 (%zu bytes)\n", width, height, width * height * 4);
    return true;
}

// Core 1 worker task - handles thumbnail generation and MQTT message building
static void mqttWorkerTask(void* param) {
    Serial.println("[Core 1] MQTT worker task started");
    
    MqttWorkRequest request;
    while (true) {
        // Wait for work request (blocking)
        if (xQueueReceive(mqttWorkQueue, &request, portMAX_DELAY) == pdTRUE) {
            bool workSuccess = false;
            
            if (request.type == MQTT_WORK_THUMBNAIL) {
                Serial.println("[Core 1] Processing thumbnail generation work...");
                // Generate thumbnail and publish (this is CPU-intensive)
                if (mqttClient != nullptr && mqttConnected) {
                    // Call the actual thumbnail generation function (runs on Core 1)
                    publishMQTTThumbnailInternalImpl();
                    workSuccess = true;
                } else {
                    Serial.println("[Core 1] MQTT not connected, skipping thumbnail publish");
                }
            } else if (request.type == MQTT_WORK_MEDIA_MAPPINGS) {
                Serial.println("[Core 1] Processing media mappings generation work...");
                // Generate media mappings and publish (this is CPU-intensive)
                if (mqttClient != nullptr && mqttConnected) {
                    // Call the actual media mappings generation function (runs on Core 1)
                    publishMQTTMediaMappingsInternalImpl();
                    workSuccess = true;
                } else {
                    Serial.println("[Core 1] MQTT not connected, skipping media mappings publish");
                }
            } else if (request.type == MQTT_WORK_CANVAS_DECODE) {
                Serial.println("[Core 1] Processing canvas decode/decompress work...");
                if (request.data.canvasDecode != nullptr) {
                    workSuccess = processCanvasDecodeWork(request.data.canvasDecode);
                } else {
                    Serial.println("[Core 1] ERROR: Canvas decode work data is null");
                }
            } else if (request.type == MQTT_WORK_PNG_ENCODE) {
                Serial.println("[Core 1] Processing PNG encode work...");
                if (request.data.pngEncode != nullptr) {
                    workSuccess = processPngEncodeWork(request.data.pngEncode);
                } else {
                    Serial.println("[Core 1] ERROR: PNG encode work data is null");
                }
            } else if (request.type == MQTT_WORK_PNG_DECODE) {
                Serial.println("[Core 1] Processing PNG decode work...");
                if (request.data.pngDecode != nullptr) {
                    workSuccess = processPngDecodeWork(request.data.pngDecode);
                } else {
                    Serial.println("[Core 1] ERROR: PNG decode work data is null");
                }
            }
            
            // Signal completion (required for synchronous operations)
            if (request.completionSem != nullptr) {
                if (request.success != nullptr) {
                    *(request.success) = workSuccess;
                }
                xSemaphoreGive(request.completionSem);
            }
        }
    }
}

// Internal implementation of thumbnail publish (runs on Core 1)
static void publishMQTTThumbnailInternal() {
    // This is the actual thumbnail generation code (moved from publishMQTTThumbnail)
    // We'll refactor publishMQTTThumbnail to call this via queue
    publishMQTTThumbnail();  // For now, just call the existing function
}

// Forward declarations (must be before first use)
static void publishMQTTMediaMappingsInternalImpl();
static bool processCanvasDecodeWork(CanvasDecodeWorkData* work);

// Internal implementation of media mappings publish (runs on Core 1)
static void publishMQTTMediaMappingsInternal() {
    // This is the actual media mappings generation code (moved from publishMQTTMediaMappings)
    // We'll refactor publishMQTTMediaMappings to call this via queue
    publishMQTTMediaMappingsInternalImpl();
}

// Process canvas decode/decompress work on Core 1
static bool processCanvasDecodeWork(CanvasDecodeWorkData* work) {
    if (work == nullptr || work->base64Data == nullptr) {
        Serial.println("[Core 1] ERROR: Invalid canvas decode work data");
        return false;
    }
    
    Serial.printf("[Core 1] Decoding base64 (len=%zu) and decompressing (compressed=%s)...\n", 
                  work->base64DataLen, work->isCompressed ? "yes" : "no");
    
    // Decode base64
    size_t decodedLen = (work->base64DataLen * 3) / 4;
    uint8_t* compressedData = (uint8_t*)hal_psram_malloc(decodedLen);
    if (!compressedData) {
        Serial.println("[Core 1] ERROR: Failed to allocate PSRAM for compressed data");
        return false;
    }
    
    // Simple base64 decode
    size_t compressedSize = 0;
    for (size_t i = 0; i < work->base64DataLen && compressedSize < decodedLen; i += 4) {
        uint32_t value = 0;
        int padding = 0;
        
        for (int j = 0; j < 4 && (i + j) < work->base64DataLen; j++) {
            char c = work->base64Data[i + j];
            if (c == '=') {
                padding++;
                value <<= 6;
            } else if (c >= 'A' && c <= 'Z') {
                value = (value << 6) | (c - 'A');
            } else if (c >= 'a' && c <= 'z') {
                value = (value << 6) | (c - 'a' + 26);
            } else if (c >= '0' && c <= '9') {
                value = (value << 6) | (c - '0' + 52);
            } else if (c == '+') {
                value = (value << 6) | 62;
            } else if (c == '/') {
                value = (value << 6) | 63;
            }
        }
        
        int bytes = 3 - padding;
        for (int j = 0; j < bytes && compressedSize < decodedLen; j++) {
            compressedData[compressedSize++] = (value >> (8 * (2 - j))) & 0xFF;
        }
    }
    
    Serial.printf("[Core 1] Base64 decoded: %zu bytes\n", compressedSize);
    
    // Decompress if needed
    if (work->isCompressed) {
        size_t expectedSize = work->width * work->height;
        work->pixelData = (uint8_t*)hal_psram_malloc(expectedSize);
        if (!work->pixelData) {
            Serial.println("[Core 1] ERROR: Failed to allocate PSRAM for decompressed pixel data");
            hal_psram_free(compressedData);
            return false;
        }
        
        size_t decompressedSize = tinfl_decompress_mem_to_mem(work->pixelData, expectedSize, compressedData, compressedSize, 0);
        
        if (decompressedSize == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) {
            Serial.println("[Core 1] miniz decompression failed, trying with zlib header...");
            decompressedSize = tinfl_decompress_mem_to_mem(work->pixelData, expectedSize, compressedData, compressedSize, TINFL_FLAG_PARSE_ZLIB_HEADER);
            if (decompressedSize == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) {
                Serial.println("[Core 1] Zlib header flag also failed");
                hal_psram_free(compressedData);
                hal_psram_free(work->pixelData);
                work->pixelData = nullptr;
                return false;
            }
        }
        
        if (decompressedSize != expectedSize) {
            Serial.printf("[Core 1] ERROR: Decompressed size mismatch: got %zu, expected %zu\n", decompressedSize, expectedSize);
            hal_psram_free(compressedData);
            hal_psram_free(work->pixelData);
            work->pixelData = nullptr;
            return false;
        }
        
        work->pixelDataLen = decompressedSize;
        hal_psram_free(compressedData);
        Serial.printf("[Core 1] Decompressed: %zu bytes\n", work->pixelDataLen);
    } else {
        // Not compressed, use directly
        work->pixelData = compressedData;
        work->pixelDataLen = compressedSize;
        compressedData = nullptr;  // Prevent double-free
    }
    
    work->success = true;
    return true;
}

// Process PNG encode work on Core 1
bool processPngEncodeWork(PngEncodeWorkData* work) {
    if (work == nullptr || work->rgbData == nullptr) {
        Serial.println("[Core 1] ERROR: Invalid PNG encode work data");
        return false;
    }
    
    Serial.printf("[Core 1] Encoding PNG with palette: %ux%u, RGB data: %zu bytes\n", 
                  work->width, work->height, work->rgbDataLen);
    
    uint32_t convertStart = millis();
    
    // Convert RGB888 to palette indices
    // Map Spectra color codes to palette indices: 0=BLACK, 1=WHITE, 2=YELLOW, 3=RED, 5=BLUE, 6=GREEN
    // Palette indices: 0=BLACK, 1=WHITE, 2=YELLOW, 3=RED, 4=BLUE, 5=GREEN
    static const uint8_t spectraToPaletteLUT[] = {
        0,  // 0 → 0 (BLACK)
        1,  // 1 → 1 (WHITE)
        2,  // 2 → 2 (YELLOW)
        3,  // 3 → 3 (RED)
        1,  // 4 → 1 (WHITE, invalid e-ink color)
        4,  // 5 → 4 (BLUE)
        5,  // 6 → 5 (GREEN)
        1   // 7 → 1 (WHITE, invalid e-ink color)
    };
    
    // Ensure LUT is built if using custom palette
    if (spectra6Color.hasCustomPalette() && !spectra6Color.hasLUT()) {
        spectra6Color.buildLUT();
    }
    
    // Allocate palette index buffer (1 byte per pixel)
    size_t paletteSize = work->width * work->height;
    uint8_t* paletteBuffer = (uint8_t*)hal_psram_malloc(paletteSize);
    if (paletteBuffer == nullptr) {
        Serial.println("[Core 1] ERROR: Failed to allocate PSRAM for palette buffer");
        work->error = 1;
        work->success = false;
        return false;
    }
    
    // Convert RGB888 to palette indices
    const uint8_t* rgbData = work->rgbData;
    for (size_t i = 0; i < paletteSize; i++) {
        uint8_t r = rgbData[i * 3 + 0];
        uint8_t g = rgbData[i * 3 + 1];
        uint8_t b = rgbData[i * 3 + 2];
        
        // Map RGB to Spectra color code, then to palette index
        uint8_t spectraCode = spectra6Color.mapColorFast(r, g, b);
        paletteBuffer[i] = spectraToPaletteLUT[spectraCode & 0x07];
    }
    
    uint32_t convertTime = millis() - convertStart;
    Serial.printf("[Core 1] RGB to palette conversion completed: %lu ms (processing %zu pixels)\n", 
                  convertTime, paletteSize);
    
    // Encode to PNG using palette-based encoding (PNG8)
    unsigned char* pngData = nullptr;
    size_t pngSize = 0;
    
    uint32_t encodeStart = millis();
    
    // Set up LodePNGState with palette mode
    LodePNGState state;
    lodepng_state_init(&state);
    
    // Configure color mode for palette - input is palette indices
    state.info_png.color.colortype = LCT_PALETTE;
    state.info_png.color.bitdepth = 8; // 8-bit palette indices
    state.info_raw.colortype = LCT_PALETTE; // Input is palette indices (1 byte per pixel)
    state.info_raw.bitdepth = 8;
    
    // Disable auto-convert to ensure palette mode is used
    state.encoder.auto_convert = 0;
    
    // Add 6 colors to palette (matching useDefaultPalette() in EL133UF1_Color.cpp)
    lodepng_palette_clear(&state.info_png.color);
    lodepng_palette_add(&state.info_png.color, 10, 10, 10, 255);      // 0: BLACK
    lodepng_palette_add(&state.info_png.color, 245, 245, 235, 255);   // 1: WHITE
    lodepng_palette_add(&state.info_png.color, 245, 210, 50, 255);    // 2: YELLOW
    lodepng_palette_add(&state.info_png.color, 190, 60, 55, 255);     // 3: RED
    lodepng_palette_add(&state.info_png.color, 45, 75, 160, 255);     // 4: BLUE
    lodepng_palette_add(&state.info_png.color, 55, 140, 85, 255);      // 5: GREEN
    
    // Encode using palette mode
    unsigned error = lodepng_encode(&pngData, &pngSize, paletteBuffer, 
                                   (unsigned)work->width, (unsigned)work->height, 
                                   &state);
    
    lodepng_state_cleanup(&state);
    hal_psram_free(paletteBuffer);
    
    if (error) {
        Serial.printf("[Core 1] ERROR: PNG palette encoding failed: %u %s\n", 
                     error, lodepng_error_text(error));
        work->error = error;
        work->success = false;
        return false;
    }
    
    if (!pngData || pngSize == 0) {
        Serial.println("[Core 1] ERROR: PNG palette encoding returned empty data");
        work->error = 1;
        work->success = false;
        return false;
    }
    
    uint32_t encodeTime = millis() - encodeStart;
    work->pngData = pngData;
    work->pngSize = pngSize;
    work->error = 0;
    work->success = true;
    Serial.printf("[Core 1] PNG palette encoded successfully: %zu bytes (native %dx%d) in %lu ms\n", 
                  pngSize, work->width, work->height, encodeTime);
    return true;
}

// Initialize Core 1 worker task
void initMqttWorkerTask() {
    if (mqttWorkerTaskInitialized) {
        return;  // Already initialized
    }
    
    // Create work queue
    mqttWorkQueue = xQueueCreate(5, sizeof(MqttWorkRequest));
    if (mqttWorkQueue == nullptr) {
        Serial.println("ERROR: Failed to create MQTT work queue");
        return;
    }
    
    // Create worker task on Core 1
    xTaskCreatePinnedToCore(
        mqttWorkerTask,
        "mqtt_worker",
        16384,  // 16KB stack (needed for thumbnail generation and JSON building)
        nullptr,
        5,  // Priority 5 (same as MQTT task)
        &mqttWorkerTaskHandle,
        1  // Core 1
    );
    
    if (mqttWorkerTaskHandle == nullptr) {
        Serial.println("ERROR: Failed to create MQTT worker task");
        vQueueDelete(mqttWorkQueue);
        mqttWorkQueue = nullptr;
        return;
    }
    
    mqttWorkerTaskInitialized = true;
    Serial.println("[Core 1] MQTT worker task initialized");
}

void publishMQTTMediaMappings(bool waitForCompletion) {
    // Initialize worker task if not already done
    if (!mqttWorkerTaskInitialized) {
        initMqttWorkerTask();
    }
    
    if (mqttWorkQueue == nullptr) {
        Serial.println("ERROR: MQTT work queue not initialized, falling back to synchronous");
        publishMQTTMediaMappingsInternalImpl();
        return;
    }
    
    // Create work request
    MqttWorkRequest request;
    request.type = MQTT_WORK_MEDIA_MAPPINGS;
    request.success = nullptr;
    request.completionSem = nullptr;
    
    SemaphoreHandle_t completionSem = nullptr;
    bool success = false;
    
    if (waitForCompletion) {
        completionSem = xSemaphoreCreateBinary();
        if (completionSem != nullptr) {
            request.completionSem = completionSem;
            request.success = &success;
        }
    }
    
    // Queue work request
    if (xQueueSend(mqttWorkQueue, &request, 0) != pdTRUE) {
        Serial.println("WARNING: MQTT work queue full, falling back to synchronous");
        if (completionSem != nullptr) {
            vSemaphoreDelete(completionSem);
        }
        publishMQTTMediaMappingsInternalImpl();
        return;
    }
    
    Serial.println("Queued media mappings generation to Core 1 worker task");
    
    // Wait for completion if requested
    if (waitForCompletion && completionSem != nullptr) {
        xSemaphoreTake(completionSem, portMAX_DELAY);
        vSemaphoreDelete(completionSem);
        Serial.printf("Media mappings generation completed (success: %s)\n", success ? "yes" : "no");
    }
}

// Helper function to escape JSON string (for filenames, etc.)
static void escapeJsonString(const char* input, char* output, size_t outputSize) {
    size_t inPos = 0;
    size_t outPos = 0;
    while (input[inPos] != '\0' && outPos < outputSize - 1) {
        char c = input[inPos];
        if (c == '\\') {
            if (outPos + 2 < outputSize - 1) {
                output[outPos++] = '\\';
                output[outPos++] = '\\';
            } else break;
        } else if (c == '"') {
            if (outPos + 2 < outputSize - 1) {
                output[outPos++] = '\\';
                output[outPos++] = '"';
            } else break;
        } else if (c == '\n') {
            if (outPos + 2 < outputSize - 1) {
                output[outPos++] = '\\';
                output[outPos++] = 'n';
            } else break;
        } else if (c == '\r') {
            if (outPos + 2 < outputSize - 1) {
                output[outPos++] = '\\';
                output[outPos++] = 'r';
            } else break;
        } else {
            output[outPos++] = c;
        }
        inPos++;
    }
    output[outPos] = '\0';
}

// Internal implementation (actual work, runs on Core 1)
static void publishMQTTMediaMappingsInternalImpl() {
    if (mqttClient == nullptr || !mqttConnected) {
        Serial.println("[Core 1] ERROR: MQTT not connected, cannot publish media mappings");
        return;
    }
    
    if (!g_media_mappings_loaded || g_media_mappings.size() == 0) {
        Serial.println("[Core 1] Media mappings not loaded yet - loading from SD card now...");
        // Ensure SD card is mounted before loading mappings
        if (!sdCardMounted) {
            Serial.println("[Core 1] SD card not mounted, attempting to mount for media mappings load...");
            if (!sdInitDirect(false)) {
                Serial.println("[Core 1] ERROR: Failed to mount SD card, cannot load media mappings");
                return;
            }
            Serial.println("[Core 1] SD card mounted successfully for media mappings load");
        }
        loadMediaMappingsFromSD(false);
        if (!g_media_mappings_loaded || g_media_mappings.size() == 0) {
            Serial.println("[Core 1] WARNING: No media mappings found on SD card, cannot publish media mappings");
            return;
        }
    }
    
    Serial.printf("[Core 1] Publishing media mappings (%zu entries) to %s...\n", g_media_mappings.size(), mqttTopicMedia);
    
    // Use cJSON library to build JSON properly (handles escaping, large payloads)
    // Generate all thumbnails first
    std::vector<String> thumbnailBase64s;
    thumbnailBase64s.reserve(g_media_mappings.size());
    
    for (size_t i = 0; i < g_media_mappings.size(); i++) {
        const MediaMapping& mm = g_media_mappings[i];
        Serial.printf("[Core 1] Generating thumbnail for [%zu] %s...\n", i, mm.imageName.c_str());
        String thumbnailBase64 = generateThumbnailFromImageFile(mm.imageName);
        thumbnailBase64s.push_back(thumbnailBase64);
        Serial.printf("[Core 1] Completed [%zu] %s (thumbnail: %d bytes base64)\n", i, mm.imageName.c_str(), thumbnailBase64.length());
    }
    
    // List all image files
    Serial.println("[Core 1] Listing all image files from SD card for allImages array...");
    std::vector<String> allImages = listImageFilesVector();
    Serial.printf("[Core 1] Found %zu image files on SD card\n", allImages.size());
    
    // Create root JSON object
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        Serial.println("[Core 1] ERROR: Failed to create root JSON object");
        return;
    }
    
    // Create mappings array
    cJSON* mappingsArray = cJSON_CreateArray();
    if (!mappingsArray) {
        Serial.println("[Core 1] ERROR: Failed to create mappings array");
        cJSON_Delete(root);
        return;
    }
    cJSON_AddItemToObject(root, "mappings", mappingsArray);
    
    // Add each mapping to the array
    for (size_t i = 0; i < g_media_mappings.size(); i++) {
        const MediaMapping& mm = g_media_mappings[i];
        const String& thumbnailBase64 = thumbnailBase64s[i];
        
        cJSON* mappingObj = cJSON_CreateObject();
        if (!mappingObj) {
            Serial.printf("[Core 1] ERROR: Failed to create mapping object for index %zu\n", i);
            cJSON_Delete(root);
            return;
        }
        
        // Add index
        cJSON_AddNumberToObject(mappingObj, "index", (double)i);
        
        // Add image name (cJSON handles JSON escaping automatically)
        cJSON_AddStringToObject(mappingObj, "image", mm.imageName.c_str());
        
        // Add audio file if present
        if (mm.audioFile.length() > 0) {
            cJSON_AddStringToObject(mappingObj, "audio", mm.audioFile.c_str());
        }
        
        // Add thumbnail if present
        if (thumbnailBase64.length() > 0) {
            cJSON_AddStringToObject(mappingObj, "thumbnail", thumbnailBase64.c_str());
        }
        
        cJSON_AddItemToArray(mappingsArray, mappingObj);
    }
    
    // Create allImages array
    cJSON* allImagesArray = cJSON_CreateArray();
    if (!allImagesArray) {
        Serial.println("[Core 1] ERROR: Failed to create allImages array");
        cJSON_Delete(root);
        return;
    }
    
    for (size_t i = 0; i < allImages.size(); i++) {
        cJSON* imageItem = cJSON_CreateString(allImages[i].c_str());
        if (!imageItem) {
            Serial.printf("[Core 1] ERROR: Failed to create image string for index %zu\n", i);
            cJSON_Delete(root);
            return;
        }
        cJSON_AddItemToArray(allImagesArray, imageItem);
    }
    cJSON_AddItemToObject(root, "allImages", allImagesArray);
    
    // Print JSON to string (cJSON_Print handles memory allocation)
    // Use cJSON_PrintBuffered with custom malloc for PSRAM
    char* jsonString = cJSON_Print(root);
    if (!jsonString) {
        Serial.println("[Core 1] ERROR: Failed to print JSON to string");
        cJSON_Delete(root);
        return;
    }
    
    size_t jsonLen = strlen(jsonString);
    Serial.printf("[Core 1] Built JSON using cJSON: %zu bytes\n", jsonLen);
    
    // Convert to Arduino String for encryption
    String jsonStr = String(jsonString);
    
    // Free cJSON's allocated string and object tree
    free(jsonString);
    cJSON_Delete(root);
    
    // Verify String conversion
    if (jsonStr.length() != jsonLen) {
        Serial.printf("[Core 1] ERROR: String conversion length mismatch - expected %zu bytes, got %d\n", jsonLen, jsonStr.length());
        return;
    }
    
    String encryptedJson = encryptAndFormatMessage(jsonStr);
    if (encryptedJson.length() == 0) {
        Serial.println("[Core 1] ERROR: Failed to encrypt media mappings - publishing without encryption");
        return;
    }
    
    size_t encryptedLen = encryptedJson.length();
    char* encryptedBuffer = (char*)malloc(encryptedLen + 1);
    if (!encryptedBuffer) {
        Serial.println("[Core 1] ERROR: Failed to allocate memory for encrypted media mappings JSON");
        return;
    }
    
    strncpy(encryptedBuffer, encryptedJson.c_str(), encryptedLen);
    encryptedBuffer[encryptedLen] = '\0';
    
    int msg_id = esp_mqtt_client_publish(mqttClient, mqttTopicMedia, encryptedBuffer, encryptedLen, 1, 1);
    if (msg_id > 0) {
        bool isEncrypted = isEncryptionEnabled();
        Serial.printf("[Core 1] Published %s media mappings to %s (msg_id: %d, size: %zu bytes)\n",
                      isEncrypted ? "encrypted" : "unencrypted", mqttTopicMedia, msg_id, encryptedLen);
    } else {
        Serial.printf("[Core 1] Failed to publish media mappings (msg_id: %d)\n", msg_id);
    }
    
    free(encryptedBuffer);
    Serial.println("[Core 1] Media mappings publish complete");
}

// Backward compatibility wrapper (no parameters)
void publishMQTTMediaMappings() {
    publishMQTTMediaMappings(false);  // Async by default
}

// Queue canvas decode/decompress work to Core 1 (synchronous - waits for completion)
bool queueCanvasDecodeWork(CanvasDecodeWorkData* work) {
    if (work == nullptr) {
        return false;
    }
    
    // Initialize worker task if not already done
    if (!mqttWorkerTaskInitialized) {
        initMqttWorkerTask();
    }
    
    if (mqttWorkQueue == nullptr) {
        Serial.println("WARNING: MQTT work queue not initialized, cannot queue canvas decode work");
        return false;
    }
    
    // Create work request
    MqttWorkRequest request;
    request.type = MQTT_WORK_CANVAS_DECODE;
    request.data.canvasDecode = work;
    request.success = &(work->success);
    
    // Create semaphore for synchronization (Core 0 will wait for Core 1)
    SemaphoreHandle_t completionSem = xSemaphoreCreateBinary();
    if (completionSem == nullptr) {
        Serial.println("ERROR: Failed to create semaphore for canvas decode work");
        return false;
    }
    request.completionSem = completionSem;
    
    // Queue work request
    if (xQueueSend(mqttWorkQueue, &request, portMAX_DELAY) != pdTRUE) {
        Serial.println("ERROR: Failed to queue canvas decode work");
        vSemaphoreDelete(completionSem);
        return false;
    }
    
    Serial.println("Queued canvas decode/decompress to Core 1 worker task (waiting for completion)...");
    
    // Wait for completion (Core 0 blocks here until Core 1 finishes)
    xSemaphoreTake(completionSem, portMAX_DELAY);
    vSemaphoreDelete(completionSem);
    
    Serial.printf("Canvas decode/decompress completed (success: %s)\n", work->success ? "yes" : "no");
    return work->success;
}

// Queue PNG encode work to Core 1 (synchronous - waits for completion)
bool queuePngEncodeWork(PngEncodeWorkData* work) {
    if (work == nullptr) {
        return false;
    }
    
    // Initialize worker task if not already done
    if (!mqttWorkerTaskInitialized) {
        initMqttWorkerTask();
    }
    
    if (mqttWorkQueue == nullptr) {
        Serial.println("WARNING: MQTT work queue not initialized, cannot queue PNG encode work");
        return false;
    }
    
    // Create work request
    MqttWorkRequest request;
    request.type = MQTT_WORK_PNG_ENCODE;
    request.data.pngEncode = work;
    request.success = &(work->success);
    
    // Create semaphore for synchronization (Core 0 will wait for Core 1)
    SemaphoreHandle_t completionSem = xSemaphoreCreateBinary();
    if (completionSem == nullptr) {
        Serial.println("ERROR: Failed to create semaphore for PNG encode work");
        return false;
    }
    request.completionSem = completionSem;
    
    // Queue work request
    if (xQueueSend(mqttWorkQueue, &request, portMAX_DELAY) != pdTRUE) {
        Serial.println("ERROR: Failed to queue PNG encode work");
        vSemaphoreDelete(completionSem);
        return false;
    }
    
    Serial.println("Queued PNG encode to Core 1 worker task (waiting for completion)...");
    
    // Wait for completion (Core 0 blocks here until Core 1 finishes)
    xSemaphoreTake(completionSem, portMAX_DELAY);
    vSemaphoreDelete(completionSem);
    
    Serial.printf("PNG encode completed (success: %s, size: %zu bytes)\n", 
                  work->success ? "yes" : "no", work->success ? work->pngSize : 0);
    return work->success;
}

// Queue PNG decode work to Core 1 (synchronous - waits for completion)
bool queuePngDecodeWork(PngDecodeWorkData* work) {
    if (work == nullptr) {
        return false;
    }
    
    // Initialize worker task if not already done
    if (!mqttWorkerTaskInitialized) {
        initMqttWorkerTask();
    }
    
    if (mqttWorkQueue == nullptr) {
        Serial.println("WARNING: MQTT work queue not initialized, cannot queue PNG decode work");
        return false;
    }
    
    MqttWorkRequest request = {
        .type = MQTT_WORK_PNG_DECODE,
        .completionSem = nullptr,  // Will create below
        .success = nullptr,  // Success status is returned via work->rgbaData != nullptr
        .data = { .pngDecode = work }
    };
    
    SemaphoreHandle_t completionSem = xSemaphoreCreateBinary();
    if (completionSem == nullptr) {
        Serial.println("ERROR: Failed to create semaphore for PNG decode work.");
        return false;
    }
    request.completionSem = completionSem;
    
    if (xQueueSend(mqttWorkQueue, &request, portMAX_DELAY) != pdTRUE) {
        Serial.println("ERROR: Failed to queue PNG decode work.");
        vSemaphoreDelete(completionSem);
        return false;
    }
    
    xSemaphoreTake(completionSem, portMAX_DELAY); // Wait for completion
    vSemaphoreDelete(completionSem);
    return (work->rgbaData != nullptr); // Success if rgbaData was allocated
}

// Forward declaration
static void mqttStatus();

void mqttSetConfig() {
    Serial.println("\n=== MQTT Configuration ===");
    Serial.println("MQTT configuration is now hardcoded.");
    Serial.println("Edit the #defines in the source code to change:");
    Serial.println("  MQTT_BROKER_HOSTNAME");
    Serial.println("  MQTT_BROKER_PORT");
    Serial.println("  MQTT_USERNAME");
    Serial.println("  MQTT_PASSWORD");
    Serial.println("  MQTT_TOPIC_SUBSCRIBE");
    Serial.println("  MQTT_TOPIC_PUBLISH");
    Serial.println("==========================\n");
    mqttStatus();
}

static void mqttStatus() {
    Serial.println("\n=== MQTT Status ===");
    mqttLoadConfig();
    
    if (strlen(mqttBroker) > 0) {
        Serial.printf("Broker: %s:%d\n", mqttBroker, mqttPort);
        Serial.printf("Client ID: %s\n", strlen(mqttClientId) > 0 ? mqttClientId : "(auto-generated)");
        if (strlen(mqttUsername) > 0) {
            Serial.printf("Username: %s\n", mqttUsername);
            Serial.println("Password: ***");
        } else {
            Serial.println("Authentication: None");
        }
        Serial.printf("Topics:\n");
        Serial.printf("  Subscribe: %s\n", mqttTopicSubscribe);
        Serial.printf("  Web UI: %s\n", mqttTopicWebUI);
        Serial.printf("  Publish: %s\n", mqttTopicPublish);
        Serial.printf("  Status: %s\n", mqttTopicStatus);
        Serial.printf("  Thumbnail: %s\n", mqttTopicThumb);
        Serial.printf("  Media: %s\n", mqttTopicMedia);
        Serial.printf("Connection: %s\n", mqttConnected ? "Connected" : "Disconnected");
    } else {
        Serial.println("No MQTT broker configured");
    }
    Serial.println("==================\n");
}


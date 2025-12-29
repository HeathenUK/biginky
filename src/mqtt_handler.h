/**
 * @file mqtt_handler.h
 * @brief MQTT connection, publishing, and message handling
 * 
 * Provides functions for:
 * - MQTT connection and disconnection
 * - Message checking and retrieval
 * - Publishing status, thumbnails, and media mappings
 * - MQTT event handling
 * 
 * Extracted from main_esp32p4_test.cpp as part of Priority 1 refactoring.
 */

#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include <Arduino.h>
#include "mqtt_client.h"
#include "driver/sdmmc_types.h"  // For sdmmc_card_t type

// Forward declarations for external dependencies
class EL133UF1;
extern EL133UF1 display;
extern bool sdCardMounted;
extern sdmmc_card_t* sd_card;
extern bool thumbnailPendingPublish;
extern bool webUICommandPending;
extern String pendingWebUICommand;
extern std::vector<struct MediaMapping> g_media_mappings;
extern bool g_media_mappings_loaded;
extern uint32_t lastMediaIndex;
extern uint8_t g_sleep_interval_minutes;

// Forward declarations for functions from main file
bool sdInitDirect(bool mode1bit);
bool handleWebInterfaceCommand(const String& jsonMessage);  // Returns bool, not void
// encryptAndFormatMessage is in webui_crypto.h (included by mqtt_handler.cpp)
// extractJsonStringField is static in json_utils.h, so we include that header instead
// Thumbnail functions are now in thumbnail_utils.h

/**
 * Load MQTT configuration (hardcoded values)
 */
void mqttLoadConfig();

/**
 * Save MQTT configuration (no-op, using hardcoded values)
 */
void mqttSaveConfig();

/**
 * Connect to MQTT broker
 * @return true if connected successfully, false otherwise
 */
bool mqttConnect();

/**
 * Disconnect from MQTT broker
 */
void mqttDisconnect();

/**
 * Check for MQTT messages (non-blocking)
 * @param timeoutMs Maximum time to wait for messages
 * @return true if message received, false otherwise
 */
bool mqttCheckMessages(uint32_t timeoutMs);

/**
 * Check if a large message is still being received
 * @return true if message is in progress, false otherwise
 */
bool mqttIsMessageInProgress();

/**
 * Get the last received MQTT message
 * @return The last message as a String
 */
String mqttGetLastMessage();

/**
 * Publish device status to MQTT
 */
void publishMQTTStatus();

/**
 * Prepare status JSON and encrypt it (can run in parallel on Core 1)
 * This function builds the status JSON and encrypts it, storing the result
 * in a shared buffer for later publishing.
 * @return true if preparation succeeded, false otherwise
 */
bool prepareStatusJsonParallel();

/**
 * Publish the pre-prepared status JSON (must be called after prepareStatusJsonParallel)
 * This is faster than publishMQTTStatus() because encryption is already done.
 * @return true if published successfully, false otherwise
 */
bool publishPreparedStatus();

/**
 * Publish display thumbnail to MQTT
 */
void publishMQTTThumbnail();

/**
 * Publish thumbnail if MQTT is connected (called from EL133UF1 library)
 */
void publishMQTTThumbnailIfConnected();

/**
 * Publish media.txt mappings with thumbnails to MQTT
 */
void publishMQTTMediaMappings();

/**
 * Get MQTT client handle (for external use)
 */
esp_mqtt_client_handle_t getMqttClient();

/**
 * Check if MQTT is connected
 */
bool isMqttConnected();

/**
 * Get MQTT topic for publishing (for external use)
 */
const char* getMqttTopicPublish();

#endif // MQTT_HANDLER_H

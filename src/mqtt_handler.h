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
extern String lastProcessedCommandId;  // ID of the last command that was processed
extern std::vector<struct MediaMapping> g_media_mappings;
extern bool g_media_mappings_loaded;
extern uint32_t lastMediaIndex;
extern uint8_t g_sleep_interval_minutes;

// Font list from RTC memory (scanned at cold boot)
// FontInfo struct is defined in main.cpp - forward declare here
struct FontInfo;
extern FontInfo g_rtcFontList[];
extern uint8_t g_rtcFontCount;

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
 * Always connect WiFi and MQTT (if needed) and publish thumbnail
 * This ensures thumbnails are always published after display updates
 */
void publishMQTTThumbnailAlways();

/**
 * Publish media.txt mappings with thumbnails to MQTT (backward compatibility - async)
 */
void publishMQTTMediaMappings();

/**
 * Publish media.txt mappings with thumbnails to MQTT
 * This function queues the work to Core 1 and returns immediately (async)
 * @param waitForCompletion If true, wait for completion before returning (default: false)
 */
void publishMQTTMediaMappings(bool waitForCompletion);

/**
 * Initialize Core 1 task for thumbnail generation and MQTT message building
 * Should be called once at startup
 */
void initMqttWorkerTask();

// Canvas decode work data (passed between cores)
struct CanvasDecodeWorkData {
    const char* base64Data;      // Input: base64 string (owned by caller, must remain valid)
    size_t base64DataLen;        // Input: length of base64 string
    int width;                   // Input: canvas width
    int height;                  // Input: canvas height
    bool isCompressed;           // Input: whether data is compressed
    uint8_t* pixelData;          // Output: decompressed pixel data (allocated by Core 1, caller must free)
    size_t pixelDataLen;         // Output: length of pixel data
    bool success;                // Output: whether operation succeeded
};

// PNG encode work data (passed between cores)
struct PngEncodeWorkData {
    const uint8_t* rgbData;      // Input: RGB888 data (owned by caller, must remain valid)
    size_t rgbDataLen;           // Input: length of RGB data
    unsigned width;              // Input: image width
    unsigned height;             // Input: image height
    unsigned char* pngData;      // Output: PNG data (allocated by Core 1, caller must free)
    size_t pngSize;             // Output: size of PNG data
    unsigned error;              // Output: lodepng error code (0 = success)
    bool success;                // Output: whether operation succeeded
};

// PNG decode work data (passed between cores)
struct PngDecodeWorkData {
    const uint8_t* pngData;      // Input: PNG data (owned by caller, must remain valid)
    size_t pngDataLen;           // Input: length of PNG data
    unsigned char* rgbaData;     // Output: RGBA8888 data (allocated by Core 1, caller must free)
    unsigned width;              // Output: image width
    unsigned height;             // Output: image height
    unsigned error;              // Output: lodepng error code (0 = success)
    bool success;                // Output: whether operation succeeded
};

/**
 * Decode base64 and decompress canvas data on Core 1 (synchronous - waits for completion)
 * @param work Work data structure with input/output parameters
 * @return true if work was queued successfully, false otherwise (falls back to synchronous)
 */
bool queueCanvasDecodeWork(CanvasDecodeWorkData* work);

/**
 * Encode RGB data as PNG on Core 1 (synchronous - waits for completion)
 * @param work Work data structure with input/output parameters
 * @return true if work was queued successfully, false otherwise (falls back to synchronous)
 */
bool queuePngEncodeWork(PngEncodeWorkData* work);

/**
 * Process PNG encoding work directly (for use when already on Core 1)
 * @param work Work data structure with input/output parameters
 * @return true if encoding succeeded, false otherwise
 */
bool processPngEncodeWork(PngEncodeWorkData* work);

/**
 * Decode PNG data to RGBA8888 buffer on Core 1 (synchronous - waits for completion)
 * @param work Work data structure with input/output parameters
 * @return true if work was queued successfully, false otherwise (falls back to synchronous)
 */
bool queuePngDecodeWork(PngDecodeWorkData* work);

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

/**
 * @file canvas_handler.cpp
 * @brief Canvas display and save functionality implementation
 * 
 * Extracted from main.cpp as part of modular refactoring.
 */

#include "canvas_handler.h"
#include "mqtt_handler.h"  // For queueCanvasDecodeWork, queuePngEncodeWork, isMqttConnected, publishMQTTMediaMappings
#include "json_utils.h"    // For extractJsonIntField, extractJsonStringField, extractJsonBoolField
#include "EL133UF1.h"      // For display constants and display object
#include "platform_hal.h"  // For hal_psram_malloc, hal_psram_free
#include "lodepng_psram.h"  // Custom PSRAM allocators for lodepng (must be before lodepng.h)
#include "lodepng.h"       // For PNG encoding
#include <Arduino.h>
#include <time.h>
#include "ff.h"            // FatFs for SD card operations

// External dependencies (from main.cpp)
extern EL133UF1 display;
extern SPIClass displaySPI;
extern bool sdCardMounted;
extern sdmmc_card_t* sd_card;

// Pin definitions (from main.cpp or build flags)
#ifndef PIN_SPI_SCK
#define PIN_SPI_SCK   3
#endif
#ifndef PIN_SPI_MOSI
#define PIN_SPI_MOSI  2
#endif
#ifndef PIN_CS0
#define PIN_CS0       23
#endif
#ifndef PIN_CS1
#define PIN_CS1       48
#endif
#ifndef PIN_DC
#define PIN_DC        26
#endif
#ifndef PIN_RESET
#define PIN_RESET     22
#endif
#ifndef PIN_BUSY
#define PIN_BUSY      47
#endif

// Forward declaration for SD card mounting
bool sdInitDirect(bool mode1bit = false);

// Helper function to ensure SD card is mounted
static bool ensureSDMounted() {
    if (sdCardMounted) {
        return true;
    }
    return sdInitDirect(false);
}

bool handleCanvasDisplayCommand(const String& messageToProcess) {
    // Extract width, height, pixelData, and compressed using JSON utilities
    int width = extractJsonIntField(messageToProcess, "width", 0);
    int height = extractJsonIntField(messageToProcess, "height", 0);
    String base64Data = extractJsonStringField(messageToProcess, "pixelData");
    bool isCompressed = extractJsonBoolField(messageToProcess, "compressed", false);
    
    if (width == 0 || height == 0 || base64Data.length() == 0) {
        Serial.printf("ERROR: canvas_display command missing required fields (width=%d, height=%d, pixelData_len=%d)\n", 
                     width, height, base64Data.length());
        return false;
    }
    
    // Calculate sizes for comparison
    size_t base64Size = base64Data.length();
    size_t expectedRawSize = width * height;  // 480,000 bytes for 800x600
    
    Serial.printf("Canvas display: width=%d, height=%d\n", width, height);
    Serial.printf("  Base64 payload size: %zu bytes (%.1f KB)\n", base64Size, base64Size / 1024.0f);
    Serial.printf("  Compressed: %s\n", isCompressed ? "yes" : "no");
    Serial.printf("  Expected raw pixel size: %zu bytes (%.1f KB)\n", expectedRawSize, expectedRawSize / 1024.0f);
    
    // Queue decode/decompress work to Core 1 (synchronous - waits for completion)
    CanvasDecodeWorkData decodeWork = {0};
    decodeWork.base64Data = base64Data.c_str();
    decodeWork.base64DataLen = base64Data.length();
    decodeWork.width = width;
    decodeWork.height = height;
    decodeWork.isCompressed = isCompressed;
    decodeWork.pixelData = nullptr;
    decodeWork.pixelDataLen = 0;
    decodeWork.success = false;
    
    if (!queueCanvasDecodeWork(&decodeWork)) {
        Serial.println("ERROR: Canvas decode/decompress failed on Core 1");
        return false;
    }
    
    // Core 1 has completed - pixelData is now available
    uint8_t* pixelData = decodeWork.pixelData;
    size_t actualLen = decodeWork.pixelDataLen;
    
    if (pixelData == nullptr || actualLen == 0) {
        Serial.println("ERROR: Core 1 decode returned null or empty pixel data");
        return false;
    }
    
    Serial.printf("  Decode/decompress completed: %zu bytes (%.1f KB)\n", actualLen, actualLen / 1024.0f);
    
    // Ensure display is initialized
    if (display.getBuffer() == nullptr) {
        Serial.println("Display not initialized - initializing now...");
        displaySPI.begin(PIN_SPI_SCK, -1, PIN_SPI_MOSI, -1);
        
        if (!display.begin(PIN_CS0, PIN_CS1, PIN_DC, PIN_RESET, PIN_BUSY)) {
            Serial.println("ERROR: Display initialization failed!");
            hal_psram_free(pixelData);
            return false;
        }
        Serial.println("Display initialized");
    }
    
    // Clear display
    display.clear(EL133UF1_WHITE);
    
    // Draw pixel data (scale from 800x600 to 1600x1200, centered)
    int16_t scaleX = display.width() / width;
    int16_t scaleY = display.height() / height;
    int16_t offsetX = (display.width() - (width * scaleX)) / 2;
    int16_t offsetY = (display.height() - (height * scaleY)) / 2;
    
    for (int y = 0; y < height && (y * width) < (int)actualLen; y++) {
        for (int x = 0; x < width && (y * width + x) < (int)actualLen; x++) {
            uint8_t color = pixelData[y * width + x];
            for (int sy = 0; sy < scaleY; sy++) {
                for (int sx = 0; sx < scaleX; sx++) {
                    int16_t px = offsetX + (x * scaleX) + sx;
                    int16_t py = offsetY + (y * scaleY) + sy;
                    if (px >= 0 && px < display.width() && py >= 0 && py < display.height()) {
                        display.setPixel(px, py, color);
                    }
                }
            }
        }
    }
    
    hal_psram_free(pixelData);
    
    // Update display (non-blocking - returns immediately, refresh happens on panel)
    Serial.println("Updating display (e-ink refresh - non-blocking, panel will take 20-30s)...");
    display.update();  // Non-blocking - returns immediately
    Serial.println("Display update started (can continue with other tasks or sleep)");
    
    return true;
}

bool saveCanvasAsPNG(const uint8_t* pixelData, size_t pixelCount, unsigned width, unsigned height, const String& filename) {
    if (!ensureSDMounted()) {
        Serial.println("SD card not mounted, cannot save canvas PNG");
        return false;
    }
    
    // Map e-ink color indices to RGB values
    // EL133UF1_BLACK=0, WHITE=1, YELLOW=2, RED=3, BLUE=5, GREEN=6
    static const uint8_t colorToRGB[7][3] = {
        {0, 0, 0},      // BLACK (0)
        {255, 255, 255}, // WHITE (1)
        {255, 255, 0},   // YELLOW (2)
        {255, 0, 0},     // RED (3)
        {255, 255, 255}, // (4) unused, default to white
        {0, 0, 255},     // BLUE (5)
        {0, 255, 0}      // GREEN (6)
    };
    
    // Convert e-ink color indices to RGB888 (3 bytes per pixel)
    size_t rgbSize = width * height * 3;
    uint8_t* rgbData = (uint8_t*)hal_psram_malloc(rgbSize);
    if (!rgbData) {
        Serial.println("ERROR: Failed to allocate PSRAM for RGB conversion");
        return false;
    }
    
    size_t pixelIdx = 0;
    for (unsigned y = 0; y < height; y++) {
        for (unsigned x = 0; x < width; x++) {
            if (pixelIdx < pixelCount) {
                uint8_t colorIdx = pixelData[pixelIdx];
                // Clamp color index to valid range (0-6)
                if (colorIdx > 6) colorIdx = 1; // Default to white
                
                uint8_t* rgbPixel = &rgbData[(y * width + x) * 3];
                rgbPixel[0] = colorToRGB[colorIdx][0]; // R
                rgbPixel[1] = colorToRGB[colorIdx][1]; // G
                rgbPixel[2] = colorToRGB[colorIdx][2]; // B
                pixelIdx++;
            } else {
                // Fill remaining pixels with white
                uint8_t* rgbPixel = &rgbData[(y * width + x) * 3];
                rgbPixel[0] = 255;
                rgbPixel[1] = 255;
                rgbPixel[2] = 255;
            }
        }
    }
    
    // Queue PNG encoding to Core 1 (synchronous - waits for completion)
    PngEncodeWorkData encodeWork = {0};
    encodeWork.rgbData = rgbData;
    encodeWork.rgbDataLen = rgbSize;
    encodeWork.width = width;
    encodeWork.height = height;
    encodeWork.pngData = nullptr;
    encodeWork.pngSize = 0;
    encodeWork.error = 0;
    encodeWork.success = false;
    
    if (!queuePngEncodeWork(&encodeWork)) {
        Serial.printf("ERROR: PNG encoding failed on Core 1: %u %s\n", 
                     encodeWork.error, encodeWork.error ? lodepng_error_text(encodeWork.error) : "unknown");
        hal_psram_free(rgbData);
        return false;
    }
    
    // Core 1 has completed - PNG data is now available
    unsigned char* pngData = encodeWork.pngData;
    size_t pngSize = encodeWork.pngSize;
    
    // Free RGB data (no longer needed)
    hal_psram_free(rgbData);
    
    if (!pngData || pngSize == 0) {
        Serial.println("ERROR: Core 1 PNG encoding returned empty data");
        if (pngData) lodepng_free(pngData);  // lodepng uses PSRAM allocator
        return false;
    }
    
    // Save PNG to SD card
    String fatfsPath = "0:/" + filename;
    FIL file;
    FRESULT fileRes = f_open(&file, fatfsPath.c_str(), FA_WRITE | FA_CREATE_ALWAYS);
    if (fileRes != FR_OK) {
        Serial.printf("ERROR: Failed to open file for writing: %d (path: %s)\n", fileRes, fatfsPath.c_str());
        lodepng_free(pngData);  // lodepng uses PSRAM allocator
        return false;
    }
    
    UINT bytesWritten = 0;
    fileRes = f_write(&file, pngData, pngSize, &bytesWritten);
    f_close(&file);
    
    lodepng_free(pngData);  // lodepng uses PSRAM allocator
    
    if (fileRes != FR_OK || bytesWritten != pngSize) {
        Serial.printf("ERROR: Failed to write PNG to SD: res=%d, written=%u/%zu\n", fileRes, bytesWritten, pngSize);
        return false;
    }
    
    Serial.printf("Canvas saved as PNG to SD: %u bytes to %s (original: %zu pixels)\n", bytesWritten, filename.c_str(), pixelCount);
    return true;
}

bool handleCanvasDisplaySaveCommand(const String& messageToProcess) {
    // Extract width, height, pixelData, compressed, and filename using JSON utilities
    int width = extractJsonIntField(messageToProcess, "width", 0);
    int height = extractJsonIntField(messageToProcess, "height", 0);
    String base64Data = extractJsonStringField(messageToProcess, "pixelData");
    bool isCompressed = extractJsonBoolField(messageToProcess, "compressed", false);
    String filename = extractJsonStringField(messageToProcess, "filename");
    
    if (width == 0 || height == 0 || base64Data.length() == 0) {
        Serial.printf("ERROR: canvas_display_save command missing required fields (width=%d, height=%d, pixelData_len=%d)\n", 
                     width, height, base64Data.length());
        return false;
    }
    
    // Validate filename (security: no path separators)
    if (filename.length() == 0) {
        // Generate default filename with timestamp
        time_t now = time(nullptr);
        struct tm tm_utc;
        gmtime_r(&now, &tm_utc);
        char timeStr[32];
        strftime(timeStr, sizeof(timeStr), "canvas_%Y%m%d_%H%M%S.png", &tm_utc);
        filename = String(timeStr);
    } else {
        // Remove any path separators for security
        filename.replace("/", "_");
        filename.replace("\\", "_");
        // Ensure .png extension
        if (!filename.endsWith(".png")) {
            filename += ".png";
        }
    }
    
    Serial.printf("Canvas display and save: width=%d, height=%d, filename=%s\n", width, height, filename.c_str());
    
    // Queue decode/decompress work to Core 1 (synchronous - waits for completion)
    CanvasDecodeWorkData decodeWork = {0};
    decodeWork.base64Data = base64Data.c_str();
    decodeWork.base64DataLen = base64Data.length();
    decodeWork.width = width;
    decodeWork.height = height;
    decodeWork.isCompressed = isCompressed;
    decodeWork.pixelData = nullptr;
    decodeWork.pixelDataLen = 0;
    decodeWork.success = false;
    
    if (!queueCanvasDecodeWork(&decodeWork)) {
        Serial.println("ERROR: Canvas decode/decompress failed on Core 1");
        return false;
    }
    
    // Core 1 has completed - pixelData is now available
    uint8_t* pixelData = decodeWork.pixelData;
    size_t actualLen = decodeWork.pixelDataLen;
    
    if (pixelData == nullptr || actualLen == 0) {
        Serial.println("ERROR: Core 1 decode returned null or empty pixel data");
        return false;
    }
    
    Serial.printf("  Decode/decompress completed: %zu bytes (%.1f KB)\n", actualLen, actualLen / 1024.0f);
    
    // Ensure display is initialized
    if (display.getBuffer() == nullptr) {
        Serial.println("Display not initialized - initializing now...");
        displaySPI.begin(PIN_SPI_SCK, -1, PIN_SPI_MOSI, -1);
        
        if (!display.begin(PIN_CS0, PIN_CS1, PIN_DC, PIN_RESET, PIN_BUSY)) {
            Serial.println("ERROR: Display initialization failed!");
            hal_psram_free(pixelData);
            return false;
        }
        Serial.println("Display initialized");
    }
    
    // Display the canvas (same as handleCanvasDisplayCommand)
    display.clear(EL133UF1_WHITE);
    
    int16_t scaleX = display.width() / width;
    int16_t scaleY = display.height() / height;
    int16_t offsetX = (display.width() - (width * scaleX)) / 2;
    int16_t offsetY = (display.height() - (height * scaleY)) / 2;
    
    for (int y = 0; y < height && (y * width) < (int)actualLen; y++) {
        for (int x = 0; x < width && (y * width + x) < (int)actualLen; x++) {
            uint8_t color = pixelData[y * width + x];
            for (int sy = 0; sy < scaleY; sy++) {
                for (int sx = 0; sx < scaleX; sx++) {
                    int16_t px = offsetX + (x * scaleX) + sx;
                    int16_t py = offsetY + (y * scaleY) + sy;
                    if (px >= 0 && px < display.width() && py >= 0 && py < display.height()) {
                        display.setPixel(px, py, color);
                    }
                }
            }
        }
    }
    
    // Save to SD card BEFORE updating display (so we have the pixel data)
    Serial.printf("Saving canvas to SD card as %s...\n", filename.c_str());
    bool saveOk = saveCanvasAsPNG(pixelData, actualLen, width, height, filename);
    
    hal_psram_free(pixelData);
    
    if (!saveOk) {
        Serial.println("WARNING: Failed to save canvas to SD card, but continuing with display update");
    } else {
        // Trigger media mappings republish to update allImages list
        Serial.println("Canvas saved successfully - triggering media mappings republish...");
        if (isMqttConnected()) {
            publishMQTTMediaMappings();
        }
    }
    
    // Update display
    Serial.println("Updating display (e-ink refresh - non-blocking, panel will take 20-30s)...");
    display.update();
    Serial.println("Display update started (can continue with other tasks or sleep)");
    
    return saveOk;
}

bool handleCanvasSaveCommand(const String& messageToProcess) {
    // Extract width, height, pixelData, compressed, and filename using JSON utilities
    int width = extractJsonIntField(messageToProcess, "width", 0);
    int height = extractJsonIntField(messageToProcess, "height", 0);
    String base64Data = extractJsonStringField(messageToProcess, "pixelData");
    bool isCompressed = extractJsonBoolField(messageToProcess, "compressed", false);
    String filename = extractJsonStringField(messageToProcess, "filename");
    
    if (width == 0 || height == 0 || base64Data.length() == 0) {
        Serial.printf("ERROR: canvas_save command missing required fields (width=%d, height=%d, pixelData_len=%d)\n", 
                     width, height, base64Data.length());
        return false;
    }
    
    // Validate filename (security: no path separators)
    if (filename.length() == 0) {
        // Generate default filename with timestamp
        time_t now = time(nullptr);
        struct tm tm_utc;
        gmtime_r(&now, &tm_utc);
        char timeStr[32];
        strftime(timeStr, sizeof(timeStr), "canvas_%Y%m%d_%H%M%S.png", &tm_utc);
        filename = String(timeStr);
    } else {
        // Remove any path separators for security
        filename.replace("/", "_");
        filename.replace("\\", "_");
        // Ensure .png extension
        if (!filename.endsWith(".png")) {
            filename += ".png";
        }
    }
    
    Serial.printf("Canvas save (no display): width=%d, height=%d, filename=%s\n", width, height, filename.c_str());
    
    // Queue decode/decompress work to Core 1 (synchronous - waits for completion)
    CanvasDecodeWorkData decodeWork = {0};
    decodeWork.base64Data = base64Data.c_str();
    decodeWork.base64DataLen = base64Data.length();
    decodeWork.width = width;
    decodeWork.height = height;
    decodeWork.isCompressed = isCompressed;
    decodeWork.pixelData = nullptr;
    decodeWork.pixelDataLen = 0;
    decodeWork.success = false;
    
    if (!queueCanvasDecodeWork(&decodeWork)) {
        Serial.println("ERROR: Canvas decode/decompress failed on Core 1");
        return false;
    }
    
    // Core 1 has completed - pixelData is now available
    uint8_t* pixelData = decodeWork.pixelData;
    size_t actualLen = decodeWork.pixelDataLen;
    
    if (pixelData == nullptr || actualLen == 0) {
        Serial.println("ERROR: Core 1 decode returned null or empty pixel data");
        return false;
    }
    
    Serial.printf("  Decode/decompress completed: %zu bytes (%.1f KB)\n", actualLen, actualLen / 1024.0f);
    
    // Save to SD card (no display update)
    Serial.printf("Saving canvas to SD card as %s (no display)...\n", filename.c_str());
    bool saveOk = saveCanvasAsPNG(pixelData, actualLen, width, height, filename);
    
    hal_psram_free(pixelData);
    
    if (saveOk) {
        Serial.printf("Canvas saved successfully to %s\n", filename.c_str());
        // Trigger media mappings republish to update allImages list
        Serial.println("Triggering media mappings republish to update image list...");
        if (isMqttConnected()) {
            publishMQTTMediaMappings();
        }
    } else {
        Serial.println("ERROR: Failed to save canvas to SD card");
    }
    
    return saveOk;
}


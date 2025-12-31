/**
 * @file thumbnail_utils.cpp
 * @brief Implementation of thumbnail generation and SD card operations
 * 
 * Extracted from main_esp32p4_test.cpp as part of Priority 1 refactoring.
 */

#include "thumbnail_utils.h"
#include <Arduino.h>
#include "ff.h"  // FatFs for SD card operations
#include "sdmmc_cmd.h"  // For sdmmc_card_t
#include "platform_hal.h"  // For hal_psram_malloc/free
#include <pngle.h>  // For PNG decoding
#include <string.h>  // For memset
#include "mqtt_handler.h"  // For processPngEncodeWork
#include "lodepng_psram.h"  // For lodepng_free (must be before lodepng.h)
#include "lodepng.h"  // For lodepng_error_text

// External dependencies from main file
extern bool sdCardMounted;
extern sdmmc_card_t* sd_card;
bool sdInitDirect(bool mode1bit = false);  // Forward declaration for SD card mounting

// Helper structure for PNG to RGB decoding
static struct {
    uint8_t* rgbBuffer;
    uint32_t width;
    uint32_t height;
    bool success;
} g_pngToRGBContext;

// Callback for pngle to write directly to RGB buffer
static void pngle_rgb_callback(pngle_t* pngle, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint8_t rgba[4]) {
    if (!g_pngToRGBContext.rgbBuffer) return;
    
    uint32_t imgWidth = pngle_get_width(pngle);
    
    // Write RGBA to RGB888 buffer (alpha blending with white background)
    for (uint32_t py = 0; py < h && (y + py) < g_pngToRGBContext.height; py++) {
        for (uint32_t px = 0; px < w && (x + px) < g_pngToRGBContext.width; px++) {
            uint32_t idx = ((y + py) * imgWidth + (x + px)) * 3;
            if (idx + 2 < g_pngToRGBContext.width * g_pngToRGBContext.height * 3) {
                // Convert RGBA to RGB888 (simple alpha blending with white background)
                uint8_t alpha = rgba[3];
                uint8_t r = (rgba[0] * alpha + 255 * (255 - alpha)) / 255;
                uint8_t g = (rgba[1] * alpha + 255 * (255 - alpha)) / 255;
                uint8_t b = (rgba[2] * alpha + 255 * (255 - alpha)) / 255;
                
                g_pngToRGBContext.rgbBuffer[idx + 0] = r;
                g_pngToRGBContext.rgbBuffer[idx + 1] = g;
                g_pngToRGBContext.rgbBuffer[idx + 2] = b;
            }
        }
    }
}

// Decode PNG to RGB888 buffer (doesn't require display)
static bool decodePNGToRGB(const uint8_t* pngData, size_t pngLen, uint8_t** rgbBuffer, uint32_t* width, uint32_t* height) {
    if (!pngData || pngLen < 24) return false;
    
    // Check PNG signature
    static const uint8_t PNG_SIG[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    for (int i = 0; i < 8; i++) {
        if (pngData[i] != PNG_SIG[i]) return false;
    }
    
    // Read dimensions from IHDR
    *width = ((uint32_t)pngData[16] << 24) | ((uint32_t)pngData[17] << 16) | 
             ((uint32_t)pngData[18] << 8) | pngData[19];
    *height = ((uint32_t)pngData[20] << 24) | ((uint32_t)pngData[21] << 16) | 
              ((uint32_t)pngData[22] << 8) | pngData[23];
    
    if (*width == 0 || *height == 0 || *width > 4096 || *height > 4096) {
        Serial.printf("ERROR: Invalid PNG dimensions: %lux%lu\n", *width, *height);
        return false;
    }
    
    // Allocate RGB buffer
    size_t rgbSize = *width * *height * 3;
    *rgbBuffer = (uint8_t*)hal_psram_malloc(rgbSize);
    if (!*rgbBuffer) {
        Serial.println("ERROR: Failed to allocate PSRAM for RGB buffer");
        return false;
    }
    memset(*rgbBuffer, 255, rgbSize);  // Initialize to white
    
    // Create pngle instance
    pngle_t* pngle = pngle_new();
    if (!pngle) {
        Serial.println("ERROR: Failed to create pngle instance");
        hal_psram_free(*rgbBuffer);
        *rgbBuffer = nullptr;
        return false;
    }
    
    // Set up global context for callback
    g_pngToRGBContext.rgbBuffer = *rgbBuffer;
    g_pngToRGBContext.width = *width;
    g_pngToRGBContext.height = *height;
    g_pngToRGBContext.success = false;
    
    pngle_set_draw_callback(pngle, pngle_rgb_callback);
    
    // Decode PNG
    int result = pngle_feed(pngle, pngData, pngLen);
    if (result >= 0) {
        g_pngToRGBContext.success = true;
    } else {
        Serial.printf("ERROR: PNG decode failed: %s\n", pngle_error(pngle));
        hal_psram_free(*rgbBuffer);
        *rgbBuffer = nullptr;
    }
    
    pngle_destroy(pngle);
    bool success = g_pngToRGBContext.success;
    g_pngToRGBContext.rgbBuffer = nullptr;  // Clear context
    return success;
}

char* loadThumbnailFromSD() {
    if (!sdCardMounted) {
        Serial.println("SD card not mounted, cannot load thumbnail");
        return nullptr;
    }
    
    const char* thumbPath = "0:/thumbnail.jpg";
    FILINFO fno;
    FRESULT res = f_stat(thumbPath, &fno);
    if (res != FR_OK) {
        Serial.println("Thumbnail file not found on SD card");
        return nullptr;
    }
    
    FIL thumbFile;
    res = f_open(&thumbFile, thumbPath, FA_READ);
    if (res != FR_OK) {
        Serial.printf("ERROR: Failed to open thumbnail file for reading: %d\n", res);
        return nullptr;
    }
    
    size_t fileSize = fno.fsize;
    uint8_t* jpegData = (uint8_t*)malloc(fileSize);
    if (jpegData == nullptr) {
        Serial.println("ERROR: Failed to allocate memory for thumbnail");
        f_close(&thumbFile);
        return nullptr;
    }
    
    UINT bytesRead = 0;
    res = f_read(&thumbFile, jpegData, fileSize, &bytesRead);
    f_close(&thumbFile);
    
    if (res != FR_OK || bytesRead != fileSize) {
        Serial.printf("ERROR: Failed to read thumbnail from SD: res=%d, read=%d/%d\n", res, bytesRead, fileSize);
        free(jpegData);
        return nullptr;
    }
    
    // Base64 encode the JPEG
    size_t base64Size = ((fileSize + 2) / 3) * 4 + 1;
    char* base64Buffer = (char*)malloc(base64Size);
    if (base64Buffer == nullptr) {
        Serial.println("ERROR: Failed to allocate base64 buffer");
        free(jpegData);
        return nullptr;
    }
    
    const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t base64Idx = 0;
    
    for (size_t i = 0; i < fileSize; i += 3) {
        uint32_t b0 = jpegData[i];
        uint32_t b1 = (i + 1 < fileSize) ? jpegData[i + 1] : 0;
        uint32_t b2 = (i + 2 < fileSize) ? jpegData[i + 2] : 0;
        uint32_t value = (b0 << 16) | (b1 << 8) | b2;
        
        if (base64Idx + 4 < base64Size) {
            base64Buffer[base64Idx++] = base64_chars[(value >> 18) & 0x3F];
            base64Buffer[base64Idx++] = base64_chars[(value >> 12) & 0x3F];
            base64Buffer[base64Idx++] = (i + 1 < fileSize) ? base64_chars[(value >> 6) & 0x3F] : '=';
            base64Buffer[base64Idx++] = (i + 2 < fileSize) ? base64_chars[value & 0x3F] : '=';
        }
    }
    base64Buffer[base64Idx] = '\0';
    free(jpegData);
    
    // Create JSON payload
    size_t jsonSize = 55 + base64Idx + 1;
    char* jsonBuffer = (char*)malloc(jsonSize);
    if (jsonBuffer == nullptr) {
        Serial.println("ERROR: Failed to allocate JSON buffer");
        free(base64Buffer);
        return nullptr;
    }
    
    int written = snprintf(jsonBuffer, jsonSize, 
                          "{\"width\":400,\"height\":300,\"format\":\"png\",\"data\":\"%s\"}",
                          base64Buffer);
    free(base64Buffer);
    
    if (written < 0 || written >= (int)jsonSize) {
        Serial.printf("ERROR: JSON buffer too small (needed %d, had %d)\n", written, jsonSize);
        free(jsonBuffer);
        return nullptr;
    }
    
    // Delete the file after loading
    f_unlink(thumbPath);
    Serial.printf("Loaded thumbnail from SD and created JSON (%d bytes)\n", written);
    return jsonBuffer;
}

String generateThumbnailFromImageFile(const String& imagePath) {
    // Ensure SD card is mounted (required for reading image and saving thumbnail)
    if (!sdCardMounted) {
        Serial.printf("SD card not mounted, attempting to mount for thumbnail generation...\n");
        if (!sdInitDirect(false)) {
            Serial.printf("ERROR: Failed to mount SD card, cannot generate thumbnail for %s\n", imagePath.c_str());
            return "";
        }
        Serial.println("SD card mounted successfully for thumbnail generation");
    }
    
    // Load image file from SD into memory
    String fullPath = "0:/" + imagePath;
    
    FILINFO fno;
    FRESULT res = f_stat(fullPath.c_str(), &fno);
    if (res != FR_OK) {
        Serial.printf("ERROR: Image file not found: %s\n", fullPath.c_str());
        return "";
    }
    size_t fileSize = fno.fsize;
    
    FIL imageFile;
    res = f_open(&imageFile, fullPath.c_str(), FA_READ);
    if (res != FR_OK) {
        Serial.printf("ERROR: Failed to open image file: %s (res=%d)\n", fullPath.c_str(), res);
        return "";
    }
    
    uint8_t* imageData = (uint8_t*)hal_psram_malloc(fileSize);
    if (!imageData) {
        Serial.println("ERROR: Failed to allocate PSRAM for image data");
        f_close(&imageFile);
        return "";
    }
    
    UINT bytesRead = 0;
    res = f_read(&imageFile, imageData, fileSize, &bytesRead);
    f_close(&imageFile);
    
    if (res != FR_OK || bytesRead != fileSize) {
        Serial.printf("ERROR: Failed to read image file: res=%d, read=%u/%u\n", res, bytesRead, fileSize);
        hal_psram_free(imageData);
        return "";
    }
    
    // Decode PNG to RGB buffer (works without display)
    uint8_t* rgbBuffer = nullptr;
    uint32_t srcWidth = 0, srcHeight = 0;
    bool loaded = false;
    
    // Try PNG first
    if (decodePNGToRGB(imageData, fileSize, &rgbBuffer, &srcWidth, &srcHeight)) {
        loaded = true;
    } else {
        // Try BMP as fallback (would need BMP decoder, but for now just skip)
        Serial.printf("WARNING: PNG decode failed, BMP fallback not implemented yet\n");
    }
    
    hal_psram_free(imageData);
    
    if (!loaded || !rgbBuffer) {
        Serial.printf("ERROR: Failed to decode image %s for thumbnail generation\n", imagePath.c_str());
        if (rgbBuffer) hal_psram_free(rgbBuffer);
        return "";
    }
    
    // Generate quarter-size thumbnail
    // Quarter size: 200x150 for 800x600, or 400x300 for 1600x1200
    const int thumbWidth = srcWidth / 4;   // Quarter width
    const int thumbHeight = srcHeight / 4;  // Quarter height
    const int scale = 4;
    
    size_t thumbSize = thumbWidth * thumbHeight * 3;
    uint8_t* thumbBuffer = (uint8_t*)hal_psram_malloc(thumbSize);
    if (thumbBuffer == nullptr) {
        Serial.println("ERROR: Failed to allocate PSRAM for thumbnail buffer");
        hal_psram_free(rgbBuffer);
        return "";
    }
    
    // Scale down by averaging 4x4 blocks from RGB buffer
    for (int ty = 0; ty < thumbHeight; ty++) {
        for (int tx = 0; tx < thumbWidth; tx++) {
            int sx = tx * scale;
            int sy = ty * scale;
            
            uint32_t rSum = 0, gSum = 0, bSum = 0;
            int count = 0;
            
            for (int dy = 0; dy < scale && (sy + dy) < (int)srcHeight; dy++) {
                for (int dx = 0; dx < scale && (sx + dx) < (int)srcWidth; dx++) {
                    int srcIdx = ((sy + dy) * srcWidth + (sx + dx)) * 3;
                    if (srcIdx + 2 < (int)(srcWidth * srcHeight * 3)) {
                        rSum += rgbBuffer[srcIdx + 0];
                        gSum += rgbBuffer[srcIdx + 1];
                        bSum += rgbBuffer[srcIdx + 2];
                        count++;
                    }
                }
            }
            
            if (count > 0) {
                int thumbIdx = (ty * thumbWidth + tx) * 3;
                // PNG encoder (lodepng) expects RGB888 format (R, G, B order)
                thumbBuffer[thumbIdx + 0] = rSum / count;  // R
                thumbBuffer[thumbIdx + 1] = gSum / count;  // G
                thumbBuffer[thumbIdx + 2] = bSum / count;  // B
            }
        }
    }
    
    hal_psram_free(rgbBuffer);
    
    // Encode to PNG using processPngEncodeWork directly (we're already on Core 1)
    // This matches the approach used in Canvas save, but calls processPngEncodeWork directly
    // since generateThumbnailFromImageFile is called from Core 1 worker task
    PngEncodeWorkData encodeWork = {0};
    encodeWork.rgbData = thumbBuffer;
    encodeWork.rgbDataLen = thumbSize;
    encodeWork.width = thumbWidth;
    encodeWork.height = thumbHeight;
    encodeWork.pngData = nullptr;
    encodeWork.pngSize = 0;
    encodeWork.error = 0;
    encodeWork.success = false;
    
    // Call processPngEncodeWork directly (we're already on Core 1)
    if (!processPngEncodeWork(&encodeWork)) {
        Serial.printf("ERROR: PNG encoding failed: %u %s\n", 
                     encodeWork.error, encodeWork.error ? lodepng_error_text(encodeWork.error) : "unknown");
        hal_psram_free(thumbBuffer);
        return "";
    }
    
    // PNG encoding completed - PNG data is now available
    unsigned char* pngBuffer = encodeWork.pngData;
    size_t pngSize = encodeWork.pngSize;
    
    // Free RGB data (no longer needed)
    hal_psram_free(thumbBuffer);
    
    if (!pngBuffer || pngSize == 0) {
        Serial.println("ERROR: PNG encoding returned empty data");
        if (pngBuffer) lodepng_free(pngBuffer);
        return "";
    }
    
    Serial.printf("PNG encoded successfully: %zu bytes\n", pngSize);
    
    size_t png_size_u32 = (size_t)pngSize;
    
    // Save PNG thumbnail to SD card for debugging (before base64 encoding)
    // Try to save directly - if SD card is accessible (which it clearly is since we just read the image),
    // FatFs will work regardless of the global mount status variables
    // Generate filename from image path (e.g., "sunset.png" -> "thumb_sunset.png")
    String thumbFilename = "0:/thumb_";
    // Extract base filename without extension
    int lastSlash = imagePath.lastIndexOf('/');
    int lastDot = imagePath.lastIndexOf('.');
    if (lastDot > lastSlash && lastDot > 0) {
        thumbFilename += imagePath.substring(lastSlash + 1, lastDot);
    } else {
        thumbFilename += imagePath.substring(lastSlash + 1);
    }
    thumbFilename += ".png";
    
    FIL thumbFile;
    FRESULT saveRes = f_open(&thumbFile, thumbFilename.c_str(), FA_WRITE | FA_CREATE_ALWAYS);
    if (saveRes == FR_OK) {
        UINT bytesWritten = 0;
        saveRes = f_write(&thumbFile, pngBuffer, png_size_u32, &bytesWritten);
        f_close(&thumbFile);
        if (saveRes == FR_OK && bytesWritten == png_size_u32) {
            Serial.printf("Saved media thumbnail to SD: %s (%u bytes)\n", thumbFilename.c_str(), bytesWritten);
        } else {
            Serial.printf("WARNING: Failed to write media thumbnail to SD: res=%d, written=%u/%u\n", saveRes, bytesWritten, png_size_u32);
        }
    } else {
        Serial.printf("WARNING: Failed to open media thumbnail file for writing: %s (res=%d)\n", thumbFilename.c_str(), saveRes);
    }
    
    // Base64 encode the PNG
    size_t base64Size = ((png_size_u32 + 2) / 3) * 4 + 1;
    char* base64Buffer = (char*)malloc(base64Size);
    if (base64Buffer == nullptr) {
        Serial.println("ERROR: Failed to allocate base64 buffer");
        lodepng_free(pngBuffer);
        return "";
    }
    
    const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t base64Idx = 0;
    
    for (size_t i = 0; i < png_size_u32; i += 3) {
        uint32_t b0 = pngBuffer[i];
        uint32_t b1 = (i + 1 < png_size_u32) ? pngBuffer[i + 1] : 0;
        uint32_t b2 = (i + 2 < png_size_u32) ? pngBuffer[i + 2] : 0;
        uint32_t value = (b0 << 16) | (b1 << 8) | b2;
        
        if (base64Idx + 4 < base64Size) {
            base64Buffer[base64Idx++] = base64_chars[(value >> 18) & 0x3F];
            base64Buffer[base64Idx++] = base64_chars[(value >> 12) & 0x3F];
            base64Buffer[base64Idx++] = (i + 1 < png_size_u32) ? base64_chars[(value >> 6) & 0x3F] : '=';
            base64Buffer[base64Idx++] = (i + 2 < png_size_u32) ? base64_chars[value & 0x3F] : '=';
        }
    }
    base64Buffer[base64Idx] = '\0';
    
    lodepng_free(pngBuffer);
    String result = String(base64Buffer);
    free(base64Buffer);
    
    return result;
}

bool saveThumbnailToSD(const uint8_t* jpegData, size_t jpegSize) {
    if (!sdCardMounted) {
        Serial.println("ERROR: SD card not mounted, cannot save thumbnail");
        return false;
    }
    
    if (!jpegData || jpegSize == 0) {
        Serial.println("ERROR: Invalid JPEG data for thumbnail save");
        return false;
    }
    
    const char* thumbPath = "0:/thumbnail.jpg";
    
    // Remove existing thumbnail if it exists
    f_unlink(thumbPath);
    
    FIL thumbFile;
    FRESULT res = f_open(&thumbFile, thumbPath, FA_WRITE | FA_CREATE_ALWAYS);
    if (res != FR_OK) {
        Serial.printf("ERROR: Failed to open thumbnail file for writing: %d\n", res);
        return false;
    }
    
    UINT bytesWritten = 0;
    res = f_write(&thumbFile, jpegData, jpegSize, &bytesWritten);
    f_close(&thumbFile);
    
    if (res != FR_OK || bytesWritten != jpegSize) {
        Serial.printf("ERROR: Failed to write thumbnail to SD: res=%d, written=%u/%u\n", res, bytesWritten, jpegSize);
        return false;
    }
    
    Serial.printf("Saved thumbnail to SD card (%u bytes)\n", bytesWritten);
    return true;
}

std::vector<String> listImageFilesVector() {
    std::vector<String> files;
    
    if (!sdCardMounted) {
        Serial.println("ERROR: SD card not mounted, cannot list image files");
        return files;
    }
    
    FF_DIR dir;
    FILINFO fno;
    FRESULT res = f_opendir(&dir, "0:/");
    
    if (res == FR_OK) {
        while (true) {
            res = f_readdir(&dir, &fno);
            if (res != FR_OK || fno.fname[0] == 0) break;
            
            // Check if it's a file (not directory) and has image extension
            if (!(fno.fattrib & AM_DIR)) {
                String filename = String(fno.fname);
                String filenameLower = filename;
                filenameLower.toLowerCase();
                if (filenameLower.endsWith(".png") || filenameLower.endsWith(".bmp") || 
                    filenameLower.endsWith(".jpg") || filenameLower.endsWith(".jpeg")) {
                    files.push_back(filename);
                }
            }
        }
        f_closedir(&dir);
    } else {
        Serial.printf("ERROR: Failed to open SD card directory for image listing: %d\n", res);
    }
    
    return files;
}


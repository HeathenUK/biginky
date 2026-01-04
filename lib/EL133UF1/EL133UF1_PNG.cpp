/**
 * @file EL133UF1_PNG.cpp
 * @brief PNG image decoder implementation for EL133UF1 display
 * 
 * Uses lodepng for PNG decoding. Decodes to RGBA8888 buffer then draws to display.
 */

#include "EL133UF1_PNG.h"
#include "EL133UF1_Color.h"
#include "../../src/lodepng_psram.h"  // Custom PSRAM allocators for lodepng (must be before lodepng.h)
#include "../../lib/lodepng/lodepng.h"  // For PNG decoding
#include "../platform_hal/platform_hal.h"  // For hal_psram_malloc/free
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Set to 1 to enable per-pixel diagnostic counters (adds overhead)
#ifndef PNG_DEBUG_STATS
#define PNG_DEBUG_STATS 0
#endif

#if PNG_DEBUG_STATS
// Diagnostic counters
static uint32_t g_pixelCount = 0;
static uint32_t g_minY = UINT32_MAX;
static uint32_t g_maxY = 0;
static uint32_t g_minX = UINT32_MAX;
static uint32_t g_maxX = 0;
static uint32_t g_drawnCount = 0;
static uint32_t g_drawnMinY = UINT32_MAX;
static uint32_t g_drawnMaxY = 0;
#endif

// No longer needed - lodepng doesn't use callbacks

EL133UF1_PNG::EL133UF1_PNG() 
    : _display(nullptr), _offsetX(0), _offsetY(0), _width(0), _height(0), _useDithering(false) {}

bool EL133UF1_PNG::begin(EL133UF1* display) {
    if (display == nullptr) return false;
    _display = display;
    return true;
}

uint8_t EL133UF1_PNG::mapToSpectra6(uint8_t r, uint8_t g, uint8_t b) {
    // Use the global color mapper with fast LUT lookup if available
    return spectra6Color.mapColorFast(r, g, b);
}

// No longer needed - lodepng decodes entire image at once

PNGResult EL133UF1_PNG::draw(int16_t x, int16_t y, const uint8_t* data, size_t len) {
    if (_display == nullptr) return PNG_ERR_NO_DISPLAY;
    if (data == nullptr || len == 0) return PNG_ERR_NULL_DATA;
    
    // Build custom LUT if using non-default palette (otherwise PROGMEM LUT is used)
    if (spectra6Color.hasCustomPalette() && !spectra6Color.hasLUT()) {
        spectra6Color.buildLUT();
    }
    
    // Set offset for drawing
    _offsetX = x;
    _offsetY = y;
    
    // Reset dithering error buffer if dithering is enabled
    if (_useDithering) {
        spectra6Color.resetDither();
    }
    
    // Decode PNG to RGBA8888 using lodepng (allocates in PSRAM via lodepng_psram)
    unsigned char* rgbaData = nullptr;
    unsigned width = 0;
    unsigned height = 0;
    
    unsigned error = lodepng_decode32(&rgbaData, &width, &height, data, len);
    
    if (error) {
        return PNG_ERR_DECODE_FAILED;
    }
    
    if (!rgbaData || width == 0 || height == 0) {
        if (rgbaData) lodepng_free(rgbaData);
        return PNG_ERR_DECODE_FAILED;
    }
    
    // Store dimensions
    _width = (int32_t)width;
    _height = (int32_t)height;
    
    // Draw RGBA data to display buffer
    for (unsigned py = 0; py < height; py++) {
        int16_t dstY = _offsetY + (int16_t)py;
        if (dstY < 0 || dstY >= _display->height()) continue;  // Skip off-screen rows
        
        // Process row pixel-by-pixel (handles transparent pixels correctly)
        const unsigned char* srcRow = rgbaData + (py * width * 4);
        for (unsigned px = 0; px < width; px++) {
            int16_t dstX = _offsetX + (int16_t)px;
            if (dstX < 0 || dstX >= _display->width()) continue;  // Skip off-screen pixels
            
            uint8_t r = srcRow[px * 4];
            uint8_t g = srcRow[px * 4 + 1];
            uint8_t b = srcRow[px * 4 + 2];
            uint8_t a = srcRow[px * 4 + 3];
            
            if (a >= 128) {  // Opaque pixel
                uint8_t color;
                if (_useDithering) {
                    color = spectra6Color.mapColorDithered(dstX, dstY, r, g, b, _width);
                } else {
                    color = mapToSpectra6(r, g, b);
                }
                _display->setPixel(dstX, dstY, color);
            }
            // Transparent pixels are skipped - background shows through
        }
        
        // Yield every 32 rows to prevent watchdog timeout
        if ((py % 32) == 0) {
            vTaskDelay(1);
        }
    }
    
    // Free RGBA data (allocated by lodepng using PSRAM allocator)
    lodepng_free(rgbaData);
    
    return PNG_OK;
}

PNGResult EL133UF1_PNG::drawFullscreen(const uint8_t* data, size_t len) {
    if (_display == nullptr) return PNG_ERR_NO_DISPLAY;
    if (data == nullptr || len == 0) return PNG_ERR_NULL_DATA;
    
    // We need to do a two-pass: first to get dimensions, then to draw
    // Or we can parse the PNG header manually for dimensions
    
    // PNG header: 8 bytes signature + IHDR chunk
    // IHDR starts at byte 8, has 4-byte length, 4-byte type, then width (4 bytes), height (4 bytes)
    if (len < 24) return PNG_ERR_INVALID_FORMAT;
    
    // Check PNG signature
    static const uint8_t PNG_SIG[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    for (int i = 0; i < 8; i++) {
        if (data[i] != PNG_SIG[i]) return PNG_ERR_INVALID_FORMAT;
    }
    
    // Read IHDR dimensions (big-endian)
    uint32_t width = ((uint32_t)data[16] << 24) | ((uint32_t)data[17] << 16) | 
                     ((uint32_t)data[18] << 8) | data[19];
    uint32_t height = ((uint32_t)data[20] << 24) | ((uint32_t)data[21] << 16) | 
                      ((uint32_t)data[22] << 8) | data[23];
    
    // Center the image
    int16_t x = (_display->width() - (int32_t)width) / 2;
    int16_t y = (_display->height() - (int32_t)height) / 2;
    
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    
    return draw(x, y, data, len);
}

const char* EL133UF1_PNG::getErrorString(PNGResult result) {
    switch (result) {
        case PNG_OK: return "OK";
        case PNG_ERR_NULL_DATA: return "Null or empty data";
        case PNG_ERR_DECODE_FAILED: return "PNG decode failed";
        case PNG_ERR_NO_DISPLAY: return "Display not initialized";
        case PNG_ERR_ALLOC_FAILED: return "Memory allocation failed";
        case PNG_ERR_INVALID_FORMAT: return "Invalid PNG format";
        default: return "Unknown error";
    }
}

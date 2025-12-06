/**
 * @file EL133UF1_PNG.cpp
 * @brief PNG image decoder implementation for EL133UF1 display
 * 
 * Uses the pngle streaming PNG decoder which is designed for embedded systems.
 * Memory usage is minimal as pixels are written directly to the display buffer
 * as they are decoded, without needing an intermediate RGB buffer.
 */

#include "EL133UF1_PNG.h"
#include "EL133UF1_Color.h"
#include <pngle.h>

// Global pointer for callback (pngle uses C callbacks)
static EL133UF1_PNG* g_pngInstance = nullptr;

// Diagnostic counters
static uint32_t g_pixelCount = 0;
static uint32_t g_minY = UINT32_MAX;
static uint32_t g_maxY = 0;
static uint32_t g_minX = UINT32_MAX;
static uint32_t g_maxX = 0;
static uint32_t g_drawnCount = 0;
static uint32_t g_drawnMinY = UINT32_MAX;
static uint32_t g_drawnMaxY = 0;

// C callback function for pngle
static void pngle_draw_callback(pngle_t* pngle, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint8_t rgba[4]) {
    if (g_pngInstance) {
        g_pngInstance->_onDraw(x, y, w, h, rgba);
        
        // Track statistics
        g_pixelCount++;
        if (y < g_minY) g_minY = y;
        if (y > g_maxY) g_maxY = y;
        if (x < g_minX) g_minX = x;
        if (x > g_maxX) g_maxX = x;
    }
}

EL133UF1_PNG::EL133UF1_PNG() 
    : _display(nullptr), _offsetX(0), _offsetY(0), _width(0), _height(0), _useDithering(false) {}

bool EL133UF1_PNG::begin(EL133UF1* display) {
    if (display == nullptr) return false;
    _display = display;
    return true;
}

uint8_t EL133UF1_PNG::mapToSpectra6(uint8_t r, uint8_t g, uint8_t b) {
    // Use the global color mapper (Lab color space by default)
    return spectra6Color.mapColor(r, g, b);
}

void EL133UF1_PNG::_onDraw(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint8_t rgba[4]) {
    if (_display == nullptr) return;
    
    // Apply offset
    int16_t dstX = _offsetX + (int16_t)x;
    int16_t dstY = _offsetY + (int16_t)y;
    
    // Bounds check
    if (dstX < 0 || dstX >= _display->width()) return;
    if (dstY < 0 || dstY >= _display->height()) return;
    
    // Map RGBA to Spectra 6 color
    uint8_t color;
    if (rgba[3] < 128) {
        // Transparent - use white (background)
        color = EL133UF1_WHITE;
    } else if (_useDithering) {
        // Use Floyd-Steinberg dithering for better gradients
        color = spectra6Color.mapColorDithered(x, y, rgba[0], rgba[1], rgba[2], _width);
    } else {
        // Standard color mapping (Lab perceptual)
        color = mapToSpectra6(rgba[0], rgba[1], rgba[2]);
    }
    
    _display->setPixel(dstX, dstY, color);
    
    // Track what we actually drew
    g_drawnCount++;
    if ((uint32_t)dstY < g_drawnMinY) g_drawnMinY = dstY;
    if ((uint32_t)dstY > g_drawnMaxY) g_drawnMaxY = dstY;
}

PNGResult EL133UF1_PNG::draw(int16_t x, int16_t y, const uint8_t* data, size_t len) {
    if (_display == nullptr) return PNG_ERR_NO_DISPLAY;
    if (data == nullptr || len == 0) return PNG_ERR_NULL_DATA;
    
    // Set offset for callback
    _offsetX = x;
    _offsetY = y;
    
    // Set global instance for C callback
    g_pngInstance = this;
    
    // Reset diagnostic counters
    g_pixelCount = 0;
    g_minY = UINT32_MAX;
    g_maxY = 0;
    g_minX = UINT32_MAX;
    g_maxX = 0;
    g_drawnCount = 0;
    g_drawnMinY = UINT32_MAX;
    g_drawnMaxY = 0;
    
    // Reset dithering error buffer if dithering is enabled
    if (_useDithering) {
        spectra6Color.resetDither();
    }
    
    // Create pngle instance
    pngle_t* pngle = pngle_new();
    if (pngle == nullptr) {
        Serial.println("PNG: Failed to create pngle instance");
        return PNG_ERR_ALLOC_FAILED;
    }
    
    // Set draw callback
    pngle_set_draw_callback(pngle, pngle_draw_callback);
    
    Serial.printf("PNG: Decoding %zu bytes...\n", len);
    uint32_t t0 = millis();
    
    // Feed data to decoder
    // pngle processes data in chunks for streaming
    const size_t CHUNK_SIZE = 1024;
    size_t fed = 0;
    int result = 0;
    
    while (fed < len) {
        size_t chunk = (len - fed > CHUNK_SIZE) ? CHUNK_SIZE : (len - fed);
        result = pngle_feed(pngle, data + fed, chunk);
        if (result < 0) {
            Serial.printf("PNG: Decode error at offset %zu: %s\n", fed, pngle_error(pngle));
            Serial.printf("PNG: Pixels drawn before error: %lu, Y range: [%lu-%lu]\n",
                          g_pixelCount, g_minY, g_maxY);
            pngle_destroy(pngle);
            g_pngInstance = nullptr;
            return PNG_ERR_DECODE_FAILED;
        }
        // pngle_feed returns bytes consumed (should equal chunk on success)
        if (result == 0) {
            Serial.printf("PNG: Warning - pngle_feed consumed 0 bytes at offset %zu\n", fed);
            fed += chunk;  // Advance anyway to avoid infinite loop
        } else {
            fed += result;
        }
    }
    
    // Store dimensions
    _width = pngle_get_width(pngle);
    _height = pngle_get_height(pngle);
    
    uint32_t elapsed = millis() - t0;
    Serial.printf("PNG: Decoded %ldx%ld in %lu ms\n", _width, _height, elapsed);
    Serial.printf("PNG: Callback stats - count=%lu, X range=[%lu-%lu], Y range=[%lu-%lu]\n",
                  g_pixelCount, g_minX, g_maxX, g_minY, g_maxY);
    Serial.printf("PNG: Drawn stats - count=%lu, display Y range=[%lu-%lu]\n",
                  g_drawnCount, g_drawnMinY, g_drawnMaxY);
    Serial.printf("PNG: Expected pixels: %ld, callbacks=%lu, drawn=%lu\n",
                  _width * _height, g_pixelCount, g_drawnCount);
    
    pngle_destroy(pngle);
    g_pngInstance = nullptr;
    
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
    
    Serial.printf("PNG: Image dimensions %lux%lu\n", width, height);
    Serial.printf("PNG: Display dimensions %dx%d\n", _display->width(), _display->height());
    
    // Center the image
    int16_t x = (_display->width() - (int32_t)width) / 2;
    int16_t y = (_display->height() - (int32_t)height) / 2;
    
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    
    Serial.printf("PNG: Drawing at offset (%d, %d)\n", x, y);
    
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

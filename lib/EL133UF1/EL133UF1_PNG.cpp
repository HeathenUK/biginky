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

// C callback function for pngle
static void pngle_draw_callback(pngle_t* pngle, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint8_t rgba[4]) {
    if (g_pngInstance) {
        g_pngInstance->_onDraw(x, y, w, h, rgba);
        
#if PNG_DEBUG_STATS
        // Track statistics (adds overhead per pixel)
        g_pixelCount++;
        if (y < g_minY) g_minY = y;
        if (y > g_maxY) g_maxY = y;
        if (x < g_minX) g_minX = x;
        if (x > g_maxX) g_maxX = x;
#endif
    }
}

EL133UF1_PNG::EL133UF1_PNG() 
    : _display(nullptr), _offsetX(0), _offsetY(0), _width(0), _height(0), _useDithering(false),
      _rowBuffer(nullptr), _rowBufferSize(0), _currentRow(-1), _rowMinX(0), _rowMaxX(0), _rowBufferValid(false) {}

bool EL133UF1_PNG::begin(EL133UF1* display) {
    if (display == nullptr) return false;
    _display = display;
    return true;
}

uint8_t EL133UF1_PNG::mapToSpectra6(uint8_t r, uint8_t g, uint8_t b) {
    // Use the global color mapper with fast LUT lookup if available
    return spectra6Color.mapColorFast(r, g, b);
}

void EL133UF1_PNG::_flushRow() {
    if (!_rowBufferValid || !_display || _currentRow < 0) return;
    if (_rowMinX > _rowMaxX) return;  // No pixels in this row
    
    int16_t dstY = _offsetY + _currentRow;
    if (dstY < 0 || dstY >= _display->height()) return;
    
    // Calculate display coordinates
    int16_t dstX = _offsetX + _rowMinX;
    int32_t count = _rowMaxX - _rowMinX + 1;
    
    // Clamp to display bounds
    if (dstX < 0) {
        count += dstX;
        dstX = 0;
    }
    if (dstX + count > _display->width()) {
        count = _display->width() - dstX;
    }
    
    if (count > 0 && _display->canUseFastRowAccess()) {
        // Fast path: batch write entire row segment
        _display->writeRowFast(dstX, dstY, _rowBuffer + _rowMinX, count);
    } else if (count > 0) {
        // Fallback: write pixels individually
        for (int32_t i = 0; i < count; i++) {
            _display->setPixel(dstX + i, dstY, _rowBuffer[_rowMinX + i]);
        }
    }
    
    // Reset row tracking
    _rowMinX = _rowBufferSize;
    _rowMaxX = -1;
}

void EL133UF1_PNG::_onDraw(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint8_t rgba[4]) {
    if (_display == nullptr) return;
    
    // Check if we've moved to a new row - flush previous row
    if (_rowBufferValid && (int32_t)y != _currentRow && _currentRow >= 0) {
        _flushRow();
    }
    _currentRow = (int32_t)y;
    
    // Bounds check for row buffer
    if (_rowBufferValid && (int32_t)x < _rowBufferSize) {
        // Map RGBA to Spectra 6 color
        uint8_t color;
        if (rgba[3] < 128) {
            // Transparent - use white (background)
            color = EL133UF1_WHITE;
        } else if (_useDithering) {
            // Use Floyd-Steinberg dithering for better gradients
            color = spectra6Color.mapColorDithered(x, y, rgba[0], rgba[1], rgba[2], _width);
        } else {
            // Fast LUT-based color mapping
            color = mapToSpectra6(rgba[0], rgba[1], rgba[2]);
        }
        
        // Store in row buffer
        _rowBuffer[x] = color;
        
        // Track min/max X for this row
        if ((int32_t)x < _rowMinX) _rowMinX = (int32_t)x;
        if ((int32_t)x > _rowMaxX) _rowMaxX = (int32_t)x;
    } else {
        // Fallback: direct pixel write (shouldn't happen with proper setup)
        int16_t dstX = _offsetX + (int16_t)x;
        int16_t dstY = _offsetY + (int16_t)y;
        
        if (dstX >= 0 && dstX < _display->width() && 
            dstY >= 0 && dstY < _display->height()) {
            uint8_t color = (rgba[3] < 128) ? EL133UF1_WHITE : mapToSpectra6(rgba[0], rgba[1], rgba[2]);
            _display->setPixel(dstX, dstY, color);
        }
    }
    
#if PNG_DEBUG_STATS
    g_drawnCount++;
    int16_t dstY = _offsetY + (int16_t)y;
    if ((uint32_t)dstY < g_drawnMinY) g_drawnMinY = dstY;
    if ((uint32_t)dstY > g_drawnMaxY) g_drawnMaxY = dstY;
#endif
}

PNGResult EL133UF1_PNG::draw(int16_t x, int16_t y, const uint8_t* data, size_t len) {
    if (_display == nullptr) return PNG_ERR_NO_DISPLAY;
    if (data == nullptr || len == 0) return PNG_ERR_NULL_DATA;
    
    // Build custom LUT if using non-default palette (otherwise PROGMEM LUT is used)
    if (spectra6Color.hasCustomPalette() && !spectra6Color.hasLUT()) {
        spectra6Color.buildLUT();
    }
    
    // Set offset for callback
    _offsetX = x;
    _offsetY = y;
    
    // Set global instance for C callback
    g_pngInstance = this;
    
#if PNG_DEBUG_STATS
    // Reset diagnostic counters
    g_pixelCount = 0;
    g_minY = UINT32_MAX;
    g_maxY = 0;
    g_minX = UINT32_MAX;
    g_maxX = 0;
    g_drawnCount = 0;
    g_drawnMinY = UINT32_MAX;
    g_drawnMaxY = 0;
#endif
    
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
    
    // Pre-parse PNG header to get dimensions for row buffer allocation
    // PNG header: 8 bytes signature + IHDR chunk (length + type + data)
    // Width is at bytes 16-19, height at bytes 20-23 (big-endian)
    int32_t preWidth = 0;
    if (len >= 24) {
        preWidth = ((int32_t)data[16] << 24) | ((int32_t)data[17] << 16) | 
                   ((int32_t)data[18] << 8) | data[19];
    }
    
    // Allocate row buffer for batch writes
    _rowBuffer = nullptr;
    _rowBufferSize = 0;
    _rowBufferValid = false;
    _currentRow = -1;
    _rowMinX = 0;
    _rowMaxX = -1;
    
    if (preWidth > 0 && preWidth <= 4096) {  // Reasonable max width
        _rowBuffer = (uint8_t*)malloc(preWidth);
        if (_rowBuffer) {
            _rowBufferSize = preWidth;
            _rowBufferValid = true;
            memset(_rowBuffer, EL133UF1_WHITE, preWidth);  // Default to white
        }
    }
    
    // Set draw callback
    pngle_set_draw_callback(pngle, pngle_draw_callback);
    
    Serial.printf("PNG: Decoding %zu bytes (row buffer: %s)...\n", len, 
                  _rowBufferValid ? "enabled" : "disabled");
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
#if PNG_DEBUG_STATS
            Serial.printf("PNG: Pixels drawn before error: %lu, Y range: [%lu-%lu]\n",
                          g_pixelCount, g_minY, g_maxY);
#endif
            // Cleanup
            if (_rowBuffer) { free(_rowBuffer); _rowBuffer = nullptr; }
            _rowBufferValid = false;
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
    
    // Flush final row
    if (_rowBufferValid) {
        _flushRow();
    }
    
    // Store dimensions
    _width = pngle_get_width(pngle);
    _height = pngle_get_height(pngle);
    
    uint32_t elapsed = millis() - t0;
    Serial.printf("PNG: Decoded %ldx%ld in %lu ms\n", _width, _height, elapsed);
#if PNG_DEBUG_STATS
    Serial.printf("PNG: Callback stats - count=%lu, X range=[%lu-%lu], Y range=[%lu-%lu]\n",
                  g_pixelCount, g_minX, g_maxX, g_minY, g_maxY);
    Serial.printf("PNG: Drawn stats - count=%lu, display Y range=[%lu-%lu]\n",
                  g_drawnCount, g_drawnMinY, g_drawnMaxY);
    Serial.printf("PNG: Expected pixels: %ld, callbacks=%lu, drawn=%lu\n",
                  _width * _height, g_pixelCount, g_drawnCount);
#endif
    
    // Cleanup
    if (_rowBuffer) { free(_rowBuffer); _rowBuffer = nullptr; }
    _rowBufferValid = false;
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

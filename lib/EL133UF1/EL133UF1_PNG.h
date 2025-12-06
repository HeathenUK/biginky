/**
 * @file EL133UF1_PNG.h
 * @brief PNG image decoder for EL133UF1 display using pngle (streaming decoder)
 * 
 * Memory requirements:
 * - PNG compressed data: stored in PSRAM (typically 500KB-2MB for 1024x1024)
 * - Decoded pixels: directly written to display buffer (no intermediate RGB buffer needed)
 * - pngle context: ~48KB working memory
 * 
 * This uses the pngle library which is a streaming PNG decoder designed for
 * embedded systems. It decodes row-by-row without needing the full decoded
 * image in memory.
 */

#ifndef EL133UF1_PNG_H
#define EL133UF1_PNG_H

#include <Arduino.h>
#include "EL133UF1.h"

// Forward declaration - pngle.h will provide the actual type

// Result codes
enum PNGResult {
    PNG_OK = 0,
    PNG_ERR_NULL_DATA,
    PNG_ERR_DECODE_FAILED,
    PNG_ERR_NO_DISPLAY,
    PNG_ERR_ALLOC_FAILED,
    PNG_ERR_INVALID_FORMAT
};

class EL133UF1_PNG {
public:
    EL133UF1_PNG();
    
    /**
     * @brief Initialize PNG decoder with display
     * @param display Pointer to initialized EL133UF1 display
     * @return true on success
     */
    bool begin(EL133UF1* display);
    
    /**
     * @brief Draw PNG image at position
     * @param x X position
     * @param y Y position
     * @param data PNG data (can be in PROGMEM or PSRAM)
     * @param len Length of PNG data
     * @return PNGResult code
     */
    PNGResult draw(int16_t x, int16_t y, const uint8_t* data, size_t len);
    
    /**
     * @brief Draw PNG centered/scaled to fill display
     * @param data PNG data
     * @param len Length of PNG data
     * @return PNGResult code
     */
    PNGResult drawFullscreen(const uint8_t* data, size_t len);
    
    /**
     * @brief Get error string for result code
     */
    const char* getErrorString(PNGResult result);
    
    /**
     * @brief Get decoded image dimensions (valid after decode)
     */
    int32_t getWidth() const { return _width; }
    int32_t getHeight() const { return _height; }
    
    // Internal callback - public for C callback access
    void _onDraw(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint8_t rgba[4]);
    
private:
    EL133UF1* _display;
    int16_t _offsetX;
    int16_t _offsetY;
    int32_t _width;
    int32_t _height;
    
    uint8_t mapToSpectra6(uint8_t r, uint8_t g, uint8_t b);
};

#endif // EL133UF1_PNG_H

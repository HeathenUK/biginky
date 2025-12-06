/**
 * @file EL133UF1_Color.h
 * @brief Color mapping utilities for Spectra 6 e-ink display
 * 
 * Provides perceptually-accurate color quantization from RGB to the
 * 6-color Spectra palette using CIE Lab color space.
 */

#ifndef EL133UF1_COLOR_H
#define EL133UF1_COLOR_H

#include <Arduino.h>
#include "EL133UF1.h"

/**
 * @brief Color quantization mode
 */
enum ColorMapMode {
    COLOR_MAP_NEAREST,      // Simple nearest color (fastest)
    COLOR_MAP_LAB,          // CIE Lab perceptual matching (better quality)
    COLOR_MAP_DITHER        // Floyd-Steinberg dithering (best for photos)
};

/**
 * @brief Spectra 6 color mapper with perceptual color matching
 */
class Spectra6ColorMap {
public:
    Spectra6ColorMap();
    ~Spectra6ColorMap();
    
    /**
     * @brief Set the color mapping mode
     */
    void setMode(ColorMapMode mode) { _mode = mode; }
    ColorMapMode getMode() const { return _mode; }
    
    /**
     * @brief Map RGB to nearest Spectra 6 color
     * @param r Red component (0-255)
     * @param g Green component (0-255)
     * @param b Blue component (0-255)
     * @return Spectra 6 color code
     */
    uint8_t mapColor(uint8_t r, uint8_t g, uint8_t b);
    
    /**
     * @brief Map RGB to Spectra 6 with dithering
     * Call this for each pixel in row-major order for dithering to work
     * @param x X coordinate
     * @param y Y coordinate
     * @param r Red component (0-255)
     * @param g Green component (0-255)
     * @param b Blue component (0-255)
     * @param imageWidth Width of the image (needed for dithering)
     * @return Spectra 6 color code
     */
    uint8_t mapColorDithered(int x, int y, uint8_t r, uint8_t g, uint8_t b, int imageWidth);
    
    /**
     * @brief Reset dithering error buffer (call before each new image)
     */
    void resetDither();
    
    /**
     * @brief Set custom calibrated palette colors
     * Use this if you've measured your actual display's colors
     * @param index Palette index (0-5: black, white, yellow, red, blue, green)
     * @param r Red component
     * @param g Green component  
     * @param b Blue component
     */
    void setCalibratedColor(int index, uint8_t r, uint8_t g, uint8_t b);
    
    /**
     * @brief Use default (estimated) Spectra 6 colors
     */
    void useDefaultPalette();
    
    /**
     * @brief Use idealized pure RGB colors (not recommended)
     */
    void useIdealizedPalette();

private:
    ColorMapMode _mode;
    
    // Calibrated RGB palette (can be customized)
    uint8_t _palette[6][3];
    
    // Pre-computed Lab values for palette
    float _paletteLab[6][3];
    
    // Dithering error buffer (for current and next row)
    // Dynamically allocated only when dithering is enabled to save ~21KB RAM
    static const int MAX_DITHER_WIDTH = 1800;
    int16_t* _errorR[2];  // Allocated on demand
    int16_t* _errorG[2];
    int16_t* _errorB[2];
    int _currentRow;
    int _ditherWidth;
    bool _ditherAllocated;
    
    // Color mapping for palette index to Spectra code
    static const uint8_t SPECTRA_CODE[6];
    
    // Internal functions
    void rgbToLab(uint8_t r, uint8_t g, uint8_t b, float* L, float* a, float* labB);
    void updatePaletteLab();
    uint8_t findNearestRGB(uint8_t r, uint8_t g, uint8_t b);
    uint8_t findNearestLab(uint8_t r, uint8_t g, uint8_t b);
    void getPaletteRGB(uint8_t spectraCode, uint8_t* r, uint8_t* g, uint8_t* b);
};

// Global instance for convenience
extern Spectra6ColorMap spectra6Color;

#endif // EL133UF1_COLOR_H

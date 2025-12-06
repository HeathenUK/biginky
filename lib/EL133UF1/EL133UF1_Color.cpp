/**
 * @file EL133UF1_Color.cpp
 * @brief Color mapping implementation for Spectra 6 e-ink display
 */

#include "EL133UF1_Color.h"
#include <math.h>

// Map palette index to Spectra color code
const uint8_t Spectra6ColorMap::SPECTRA_CODE[6] = {
    EL133UF1_BLACK,   // 0
    EL133UF1_WHITE,   // 1
    EL133UF1_YELLOW,  // 2
    EL133UF1_RED,     // 3
    EL133UF1_BLUE,    // 4
    EL133UF1_GREEN    // 5
};

// Global instance
Spectra6ColorMap spectra6Color;

Spectra6ColorMap::Spectra6ColorMap() 
    : _mode(COLOR_MAP_LAB), _currentRow(0), _ditherWidth(0) {
    useDefaultPalette();
    resetDither();
}

void Spectra6ColorMap::useDefaultPalette() {
    // Calibrated Spectra 6 colors - realistic e-ink values
    // These are estimates based on typical e-ink color characteristics
    // E-ink colors are generally less saturated than LCD/OLED
    
    // Black - quite dark but not pure black
    _palette[0][0] = 10;   _palette[0][1] = 10;   _palette[0][2] = 10;
    
    // White - slightly warm off-white
    _palette[1][0] = 245;  _palette[1][1] = 245;  _palette[1][2] = 235;
    
    // Yellow - fairly saturated, warm yellow
    _palette[2][0] = 245;  _palette[2][1] = 210;  _palette[2][2] = 50;
    
    // Red - brick/tomato red, not fire-engine red
    _palette[3][0] = 190;  _palette[3][1] = 60;   _palette[3][2] = 55;
    
    // Blue - deep navy blue
    _palette[4][0] = 45;   _palette[4][1] = 75;   _palette[4][2] = 160;
    
    // Green - teal/forest green
    _palette[5][0] = 55;   _palette[5][1] = 140;  _palette[5][2] = 85;
    
    updatePaletteLab();
}

void Spectra6ColorMap::useIdealizedPalette() {
    // Pure RGB colors (not realistic for e-ink)
    _palette[0][0] = 0;    _palette[0][1] = 0;    _palette[0][2] = 0;     // Black
    _palette[1][0] = 255;  _palette[1][1] = 255;  _palette[1][2] = 255;   // White
    _palette[2][0] = 255;  _palette[2][1] = 255;  _palette[2][2] = 0;     // Yellow
    _palette[3][0] = 255;  _palette[3][1] = 0;    _palette[3][2] = 0;     // Red
    _palette[4][0] = 0;    _palette[4][1] = 0;    _palette[4][2] = 255;   // Blue
    _palette[5][0] = 0;    _palette[5][1] = 255;  _palette[5][2] = 0;     // Green
    
    updatePaletteLab();
}

void Spectra6ColorMap::setCalibratedColor(int index, uint8_t r, uint8_t g, uint8_t b) {
    if (index < 0 || index > 5) return;
    _palette[index][0] = r;
    _palette[index][1] = g;
    _palette[index][2] = b;
    updatePaletteLab();
}

// Convert RGB to CIE Lab color space
void Spectra6ColorMap::rgbToLab(uint8_t r, uint8_t g, uint8_t b, float* L, float* a, float* labB) {
    // First convert RGB to XYZ (assuming sRGB)
    float rf = r / 255.0f;
    float gf = g / 255.0f;
    float bf = b / 255.0f;
    
    // Apply sRGB gamma correction
    rf = (rf > 0.04045f) ? powf((rf + 0.055f) / 1.055f, 2.4f) : rf / 12.92f;
    gf = (gf > 0.04045f) ? powf((gf + 0.055f) / 1.055f, 2.4f) : gf / 12.92f;
    bf = (bf > 0.04045f) ? powf((bf + 0.055f) / 1.055f, 2.4f) : bf / 12.92f;
    
    // Convert to XYZ using sRGB matrix
    float x = rf * 0.4124564f + gf * 0.3575761f + bf * 0.1804375f;
    float y = rf * 0.2126729f + gf * 0.7151522f + bf * 0.0721750f;
    float z = rf * 0.0193339f + gf * 0.1191920f + bf * 0.9503041f;
    
    // Normalize for D65 white point
    x /= 0.95047f;
    y /= 1.00000f;
    z /= 1.08883f;
    
    // Convert XYZ to Lab
    const float epsilon = 0.008856f;
    const float kappa = 903.3f;
    
    float fx = (x > epsilon) ? cbrtf(x) : (kappa * x + 16.0f) / 116.0f;
    float fy = (y > epsilon) ? cbrtf(y) : (kappa * y + 16.0f) / 116.0f;
    float fz = (z > epsilon) ? cbrtf(z) : (kappa * z + 16.0f) / 116.0f;
    
    *L = 116.0f * fy - 16.0f;
    *a = 500.0f * (fx - fy);
    *labB = 200.0f * (fy - fz);
}

void Spectra6ColorMap::updatePaletteLab() {
    for (int i = 0; i < 6; i++) {
        rgbToLab(_palette[i][0], _palette[i][1], _palette[i][2],
                 &_paletteLab[i][0], &_paletteLab[i][1], &_paletteLab[i][2]);
    }
}

uint8_t Spectra6ColorMap::findNearestRGB(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t minDist = UINT32_MAX;
    uint8_t bestIdx = 1;  // Default to white
    
    for (int i = 0; i < 6; i++) {
        int32_t dr = (int32_t)r - _palette[i][0];
        int32_t dg = (int32_t)g - _palette[i][1];
        int32_t db = (int32_t)b - _palette[i][2];
        
        // Weighted Euclidean distance (crude perceptual weighting)
        uint32_t dist = (dr * dr * 2) + (dg * dg * 4) + (db * db);
        
        if (dist < minDist) {
            minDist = dist;
            bestIdx = i;
        }
    }
    
    return SPECTRA_CODE[bestIdx];
}

uint8_t Spectra6ColorMap::findNearestLab(uint8_t r, uint8_t g, uint8_t b) {
    float L, a, labB;
    rgbToLab(r, g, b, &L, &a, &labB);
    
    float minDist = 1e10f;
    uint8_t bestIdx = 1;  // Default to white
    
    for (int i = 0; i < 6; i++) {
        // CIE76 Delta E (simple Euclidean in Lab space)
        float dL = L - _paletteLab[i][0];
        float da = a - _paletteLab[i][1];
        float db = labB - _paletteLab[i][2];
        
        float dist = dL * dL + da * da + db * db;
        
        if (dist < minDist) {
            minDist = dist;
            bestIdx = i;
        }
    }
    
    return SPECTRA_CODE[bestIdx];
}

void Spectra6ColorMap::getPaletteRGB(uint8_t spectraCode, uint8_t* r, uint8_t* g, uint8_t* b) {
    // Find palette index from spectra code
    int idx = 1;  // Default to white
    for (int i = 0; i < 6; i++) {
        if (SPECTRA_CODE[i] == spectraCode) {
            idx = i;
            break;
        }
    }
    *r = _palette[idx][0];
    *g = _palette[idx][1];
    *b = _palette[idx][2];
}

uint8_t Spectra6ColorMap::mapColor(uint8_t r, uint8_t g, uint8_t b) {
    switch (_mode) {
        case COLOR_MAP_NEAREST:
            return findNearestRGB(r, g, b);
        case COLOR_MAP_LAB:
        case COLOR_MAP_DITHER:
        default:
            return findNearestLab(r, g, b);
    }
}

void Spectra6ColorMap::resetDither() {
    _currentRow = 0;
    _ditherWidth = 0;
    memset(_errorR, 0, sizeof(_errorR));
    memset(_errorG, 0, sizeof(_errorG));
    memset(_errorB, 0, sizeof(_errorB));
}

uint8_t Spectra6ColorMap::mapColorDithered(int x, int y, uint8_t r, uint8_t g, uint8_t b, int imageWidth) {
    // Handle row changes
    if (y != _currentRow) {
        // Copy next row errors to current, clear next
        if (y == _currentRow + 1) {
            memcpy(_errorR[0], _errorR[1], sizeof(_errorR[0]));
            memcpy(_errorG[0], _errorG[1], sizeof(_errorG[0]));
            memcpy(_errorB[0], _errorB[1], sizeof(_errorB[0]));
            memset(_errorR[1], 0, sizeof(_errorR[1]));
            memset(_errorG[1], 0, sizeof(_errorG[1]));
            memset(_errorB[1], 0, sizeof(_errorB[1]));
        } else {
            // Non-sequential row, reset dithering
            resetDither();
        }
        _currentRow = y;
        _ditherWidth = imageWidth;
    }
    
    if (x < 0 || x >= MAX_DITHER_WIDTH || x >= imageWidth) {
        return findNearestLab(r, g, b);
    }
    
    // Add accumulated error to this pixel
    int16_t newR = (int16_t)r + _errorR[0][x];
    int16_t newG = (int16_t)g + _errorG[0][x];
    int16_t newB = (int16_t)b + _errorB[0][x];
    
    // Clamp to valid range
    if (newR < 0) newR = 0; else if (newR > 255) newR = 255;
    if (newG < 0) newG = 0; else if (newG > 255) newG = 255;
    if (newB < 0) newB = 0; else if (newB > 255) newB = 255;
    
    // Find nearest palette color
    uint8_t spectraColor = findNearestLab((uint8_t)newR, (uint8_t)newG, (uint8_t)newB);
    
    // Get the actual palette RGB for error calculation
    uint8_t palR, palG, palB;
    getPaletteRGB(spectraColor, &palR, &palG, &palB);
    
    // Calculate quantization error
    int16_t errR = newR - (int16_t)palR;
    int16_t errG = newG - (int16_t)palG;
    int16_t errB = newB - (int16_t)palB;
    
    // Distribute error using Floyd-Steinberg coefficients:
    //        *   7/16
    //  3/16 5/16 1/16
    
    // Right pixel (7/16)
    if (x + 1 < imageWidth && x + 1 < MAX_DITHER_WIDTH) {
        _errorR[0][x + 1] += (errR * 7) / 16;
        _errorG[0][x + 1] += (errG * 7) / 16;
        _errorB[0][x + 1] += (errB * 7) / 16;
    }
    
    // Bottom-left pixel (3/16)
    if (x > 0) {
        _errorR[1][x - 1] += (errR * 3) / 16;
        _errorG[1][x - 1] += (errG * 3) / 16;
        _errorB[1][x - 1] += (errB * 3) / 16;
    }
    
    // Bottom pixel (5/16)
    if (x < MAX_DITHER_WIDTH) {
        _errorR[1][x] += (errR * 5) / 16;
        _errorG[1][x] += (errG * 5) / 16;
        _errorB[1][x] += (errB * 5) / 16;
    }
    
    // Bottom-right pixel (1/16)
    if (x + 1 < imageWidth && x + 1 < MAX_DITHER_WIDTH) {
        _errorR[1][x + 1] += (errR * 1) / 16;
        _errorG[1][x + 1] += (errG * 1) / 16;
        _errorB[1][x + 1] += (errB * 1) / 16;
    }
    
    return spectraColor;
}

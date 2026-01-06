/**
 * @file EL133UF1_SVG.h
 * @brief SVG icon renderer for EL133UF1 display using NanoSVG
 * 
 * Memory requirements:
 * - SVG source data: stored in PSRAM or flash (typically 5-50KB for simple icons)
 * - Rasterized RGBA buffer: allocated in PSRAM (width * height * 4 bytes)
 * - NanoSVG parser/rasterizer: ~50-100KB working memory (stack)
 * 
 * This uses NanoSVG to parse SVG files and rasterize them to RGBA, then maps
 * colors to the Spectra 6 palette and writes to the display buffer.
 * 
 * Example usage:
 *   // From memory (null-terminated string)
 *   EL133UF1_SVG svg;
 *   svg.begin(&display);
 *   const char* svgIcon = "<svg>...</svg>";
 *   svg.draw(100, 100, svgIcon, 2.0f); // 2x scale
 * 
 *   // From binary buffer with length
 *   uint8_t* svgData = ...;  // SVG file data
 *   size_t svgLen = ...;
 *   svg.draw(100, 100, svgData, svgLen, 1.5f); // 1.5x scale
 * 
 *   // Check for errors
 *   SVGResult result = svg.draw(0, 0, svgData, svgLen);
 *   if (result != SVG_OK) {
 *       Serial.printf("SVG error: %s\n", svg.getErrorString(result));
 *   }
 */

#ifndef EL133UF1_SVG_H
#define EL133UF1_SVG_H

#include <Arduino.h>
#include "EL133UF1.h"

// Result codes
enum SVGResult {
    SVG_OK = 0,
    SVG_ERR_NULL_DATA,
    SVG_ERR_PARSE_FAILED,
    SVG_ERR_RENDER_FAILED,
    SVG_ERR_NO_DISPLAY,
    SVG_ERR_ALLOC_FAILED,
    SVG_ERR_INVALID_SIZE
};

class EL133UF1_SVG {
public:
    EL133UF1_SVG();
    ~EL133UF1_SVG();
    
    /**
     * @brief Initialize SVG renderer with display
     * @param display Pointer to initialized EL133UF1 display
     * @return true on success
     */
    bool begin(EL133UF1* display);
    
    /**
     * @brief Enable/disable Floyd-Steinberg dithering
     * Dithering improves gradient appearance but takes more time
     * @param enable true to enable dithering
     */
    void setDithering(bool enable) { _useDithering = enable; }
    bool getDithering() const { return _useDithering; }
    
    /**
     * @brief Enable/disable auto-cropping to content bounds
     * When enabled, calculates the bounding box of all visible shapes and
     * crops out empty space, centering the content
     * @param enable true to enable auto-cropping
     */
    void setAutoCrop(bool enable) { _autoCrop = enable; }
    bool getAutoCrop() const { return _autoCrop; }
    
    /**
     * @brief Enable/disable color inversion (black becomes white)
     * When enabled, inverts RGB colors after rasterization but before mapping to Spectra6
     * Useful for rendering black-on-transparent SVGs as white-on-transparent
     * @param enable true to enable color inversion
     */
    void setInvertColors(bool enable) { _invertColors = enable; }
    bool getInvertColors() const { return _invertColors; }
    
    /**
     * @brief Draw SVG icon from memory data
     * @param x X position on display
     * @param y Y position on display
     * @param svgData SVG file data (must be null-terminated string)
     * @param scale Scale factor (1.0 = original size, 2.0 = double size, etc.)
     * @param bgColor Background color for transparent areas (default: current display color)
     * @return SVGResult code
     */
    SVGResult draw(int16_t x, int16_t y, const char* svgData, float scale = 1.0f, 
                   uint8_t bgColor = 0xFF);
    
    /**
     * @brief Draw SVG icon from memory data (binary buffer with length)
     * @param x X position on display
     * @param y Y position on display
     * @param svgData SVG file data
     * @param svgDataLen Length of SVG data
     * @param scale Scale factor (1.0 = original size, 2.0 = double size, etc.)
     * @param bgColor Background color for transparent areas (default: current display color)
     * @return SVGResult code
     */
    SVGResult draw(int16_t x, int16_t y, const uint8_t* svgData, size_t svgDataLen, 
                   float scale = 1.0f, uint8_t bgColor = 0xFF);
    
    /**
     * @brief Get error string for result code
     */
    const char* getErrorString(SVGResult result);
    
    /**
     * @brief Get last rendered image dimensions (valid after successful draw)
     */
    int32_t getWidth() const { return _width; }
    int32_t getHeight() const { return _height; }
    
    /**
     * @brief Get SVG dimensions without rendering
     * @param svgData SVG file data (must be null-terminated string)
     * @param width Output width
     * @param height Output height
     * @return true if dimensions could be determined
     */
    bool getDimensions(const char* svgData, float* width, float* height);
    
    /**
     * @brief Get SVG dimensions without rendering (binary buffer version)
     * @param svgData SVG file data
     * @param svgDataLen Length of SVG data
     * @param width Output width
     * @param height Output height
     * @return true if dimensions could be determined
     */
    bool getDimensions(const uint8_t* svgData, size_t svgDataLen, float* width, float* height);
    
private:
    EL133UF1* _display;
    int32_t _width;
    int32_t _height;
    bool _useDithering;
    bool _autoCrop;
    bool _invertColors;
    
    uint8_t mapToSpectra6(uint8_t r, uint8_t g, uint8_t b);
    SVGResult rasterizeAndDraw(int16_t x, int16_t y, void* image, float scale, uint8_t bgColor);
    bool calculateContentBounds(void* image, float* minX, float* minY, float* maxX, float* maxY);
};

#endif // EL133UF1_SVG_H

/**
 * @file text_layout.h
 * @brief Modular text layout system for optimal placement of multiple text elements
 * 
 * Provides a clean, extensible system for placing text elements (time/date, quotes, etc.)
 * on the display with automatic overlap prevention and optimal positioning.
 */

#ifndef TEXT_LAYOUT_H
#define TEXT_LAYOUT_H

#include <Arduino.h>
#include "EL133UF1.h"
#include "EL133UF1_TTF.h"
#include "EL133UF1_TextPlacement.h"  // For ExclusionZone

// Use ExclusionZone from EL133UF1_TextPlacement.h

/**
 * @brief Base class for all text content elements
 */
class TextContentElement {
public:
    virtual ~TextContentElement() = default;
    
    /**
     * @brief Get the dimensions this element needs
     * @param width Output: width in pixels
     * @param height Output: height in pixels
     */
    virtual void getDimensions(int16_t& width, int16_t& height) = 0;
    
    /**
     * @brief Draw this element at the specified center position
     * @param centerX Center X coordinate
     * @param centerY Center Y coordinate
     */
    virtual void draw(int16_t centerX, int16_t centerY) = 0;
    
    /**
     * @brief Get priority for placement order (higher = placed first)
     */
    virtual int getPriority() const = 0;
    
    /**
     * @brief Check if this element supports adaptive sizing
     */
    virtual bool canAdaptSize() const { return false; }
    
    /**
     * @brief Set adaptive size scale (1.0 = normal, <1.0 = smaller)
     */
    virtual void setAdaptiveSize(float scale) { (void)scale; }
    
    /**
     * @brief Get exclusion zone after placement
     * @param centerX Center X where element was placed
     * @param centerY Center Y where element was placed
     * @return Exclusion zone to prevent overlap with future elements
     */
    virtual ExclusionZone getExclusionZone(int16_t centerX, int16_t centerY) const = 0;
    
    /**
     * @brief Get text colors (for scoring)
     * @param textColor Output: primary text color
     * @param outlineColor Output: outline color
     */
    virtual void getColors(uint8_t& textColor, uint8_t& outlineColor) const = 0;
};

/**
 * @brief Layout engine for placing multiple text elements optimally
 */
class TextLayoutEngine {
public:
    TextLayoutEngine(EL133UF1* display, EL133UF1_TTF* ttf);
    
    /**
     * @brief Set keepout margins (areas where text cannot be placed)
     * @param margin Pixels from each edge
     */
    void setKeepout(int16_t margin);
    
    /**
     * @brief Place a single element optimally
     * @param element Element to place
     * @param centerX Output: center X where element was placed
     * @param centerY Output: center Y where element was placed
     * @param score Output: placement score (0.0-1.0)
     * @return true if placed successfully
     */
    bool placeElement(TextContentElement* element, int16_t& centerX, int16_t& centerY, float& score);
    
    /**
     * @brief Place multiple elements in priority order
     * @param elements Array of element pointers
     * @param numElements Number of elements
     * @return true if all elements placed successfully
     */
    bool placeElements(TextContentElement** elements, int numElements);
    
    /**
     * @brief Clear all exclusion zones (start fresh)
     */
    void clearExclusionZones();
    
    /**
     * @brief Add an exclusion zone
     */
    void addExclusionZone(const ExclusionZone& zone);
    
private:
    EL133UF1* _display;
    EL133UF1_TTF* _ttf;
    TextPlacementAnalyzer _analyzer;  // Use analyzer for parallel scoring
    int16_t _keepout;
    
    static const int MAX_EXCLUSION_ZONES = 16;
    ExclusionZone _exclusionZones[MAX_EXCLUSION_ZONES];
    int _numExclusionZones;
    
    // Simplified scoring (fallback if analyzer not available)
    float scoreRegion(int16_t x, int16_t y, int16_t w, int16_t h, 
                     uint8_t textColor, uint8_t outlineColor);
    float computeContrastScore(int16_t x, int16_t y, int16_t w, int16_t h,
                               uint8_t textColor, uint8_t outlineColor);
    float computeUniformityScore(int16_t x, int16_t y, int16_t w, int16_t h);
    
    bool overlapsExclusionZone(int16_t x, int16_t y, int16_t w, int16_t h) const;
    bool isWithinSafeArea(int16_t x, int16_t y, int16_t w, int16_t h) const;
};

#endif // TEXT_LAYOUT_H

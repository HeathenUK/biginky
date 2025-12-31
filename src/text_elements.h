/**
 * @file text_elements.h
 * @brief Concrete text content element implementations
 */

#ifndef TEXT_ELEMENTS_H
#define TEXT_ELEMENTS_H

#include "text_layout.h"
#include "EL133UF1_TTF.h"
#include <vector>

// Forward declaration to avoid ExclusionZone conflict
class TextPlacementAnalyzer;

/**
 * @brief Time and date display element
 */
class TimeDateElement : public TextContentElement {
public:
    TimeDateElement(EL133UF1_TTF* ttf, const char* timeText, const char* dateText);
    
    void getDimensions(int16_t& width, int16_t& height) override;
    void draw(int16_t centerX, int16_t centerY) override;
    int getPriority() const override { return 100; }  // High priority (placed first)
    bool canAdaptSize() const override { return true; }
    void setAdaptiveSize(float scale) override;
    ExclusionZone getExclusionZone(int16_t centerX, int16_t centerY) const override;
    void getColors(uint8_t& textColor, uint8_t& outlineColor) const override;
    
    void setTimeText(const char* text) { _timeText = text; }
    void setDateText(const char* text) { _dateText = text; }
    
private:
    EL133UF1_TTF* _ttf;
    const char* _timeText;
    const char* _dateText;
    float _timeFontSize;
    float _dateFontSize;
    int16_t _timeOutline;
    int16_t _dateOutline;
    int16_t _gapBetween;
    float _sizeScale;
    
    void recalculateDimensions();
    int16_t _cachedWidth;
    int16_t _cachedHeight;
};

/**
 * @brief Quote with author display element
 */
class QuoteElement : public TextContentElement {
public:
    QuoteElement(EL133UF1_TTF* ttf, const char* quoteText, const char* authorText);
    
    void getDimensions(int16_t& width, int16_t& height) override;
    void draw(int16_t centerX, int16_t centerY) override;
    int getPriority() const override { return 50; }  // Lower priority (placed after time/date)
    bool canAdaptSize() const override { return true; }
    void setAdaptiveSize(float scale) override;
    ExclusionZone getExclusionZone(int16_t centerX, int16_t centerY) const override;
    void getColors(uint8_t& textColor, uint8_t& outlineColor) const override;
    
    void setQuoteText(const char* text) { _quoteText = text; }
    void setAuthorText(const char* text) { _authorText = text; }
    
private:
    EL133UF1_TTF* _ttf;
    const char* _quoteText;
    const char* _authorText;
    float _quoteFontSize;
    float _authorFontSize;
    int16_t _outlineWidth;
    float _sizeScale;
    
    // Wrapping state
    char _wrappedQuote[512];
    int _quoteLines;
    int16_t _quoteWidth;
    int16_t _quoteHeight;
    int16_t _authorWidth;
    int16_t _authorHeight;
    int16_t _totalWidth;
    int16_t _totalHeight;
    
    void recalculateDimensions();
    void wrapQuote();
};

/**
 * @brief TextContentElement for displaying weather information.
 */
class WeatherElement : public TextContentElement {
public:
    WeatherElement(EL133UF1_TTF* ttf, const char* temperature, const char* condition, const char* location);
    
    void getDimensions(int16_t& width, int16_t& height) override;
    void draw(int16_t centerX, int16_t centerY) override;
    int getPriority() const override { return 75; } // Medium-high priority (between time/date and quote)
    bool canAdaptSize() const override { return true; }
    void setAdaptiveSize(float scale) override;
    ExclusionZone getExclusionZone(int16_t centerX, int16_t centerY) const override;
    void getColors(uint8_t& textColor, uint8_t& outlineColor) const override;
    
private:
    EL133UF1_TTF* _ttf;
    char _temperature[16];
    char _condition[64];
    char _location[64];
    float _tempFontSize;
    float _conditionFontSize;
    float _locationFontSize;
    const int16_t _gapBetween;
    const int16_t _outlineWidth;
    float _sizeScale;
    
    // Cached dimensions
    int16_t _cachedWidth;
    int16_t _cachedHeight;
    int16_t _cachedTempW, _cachedTempH;
    int16_t _cachedConditionW, _cachedConditionH;
    int16_t _cachedLocationW, _cachedLocationH;
    
    void recalculateDimensions();
};

#endif // TEXT_ELEMENTS_H

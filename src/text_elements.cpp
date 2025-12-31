/**
 * @file text_elements.cpp
 * @brief Implementation of concrete text content elements
 */

#include "text_elements.h"
#include "EL133UF1_TextPlacement.h"
#include <string.h>
#include <math.h>

// ============================================================================
// TimeDateElement Implementation
// ============================================================================

TimeDateElement::TimeDateElement(EL133UF1_TTF* ttf, const char* timeText, const char* dateText)
    : _ttf(ttf), _timeText(timeText), _dateText(dateText),
      _timeFontSize(180.0f), _dateFontSize(96.0f),
      _timeOutline(3), _dateOutline(2), _gapBetween(20),
      _sizeScale(1.0f), _cachedWidth(0), _cachedHeight(0)
{
    recalculateDimensions();
}

void TimeDateElement::setAdaptiveSize(float scale) {
    _sizeScale = scale;
    recalculateDimensions();
}

void TimeDateElement::recalculateDimensions() {
    float timeSize = _timeFontSize * _sizeScale;
    float dateSize = _dateFontSize * _sizeScale;
    
    int16_t timeW = _ttf->getTextWidth(_timeText, timeSize) + (_timeOutline * 2);
    int16_t timeH = _ttf->getTextHeight(timeSize) + (_timeOutline * 2);
    int16_t dateW = _ttf->getTextWidth(_dateText, dateSize) + (_dateOutline * 2);
    int16_t dateH = _ttf->getTextHeight(dateSize) + (_dateOutline * 2);
    
    _cachedWidth = (timeW > dateW) ? timeW : dateW;
    _cachedHeight = timeH + _gapBetween + dateH;
}

void TimeDateElement::getDimensions(int16_t& width, int16_t& height) {
    width = _cachedWidth;
    height = _cachedHeight;
}

void TimeDateElement::draw(int16_t centerX, int16_t centerY) {
    float timeSize = _timeFontSize * _sizeScale;
    float dateSize = _dateFontSize * _sizeScale;
    
    int16_t timeW = _ttf->getTextWidth(_timeText, timeSize) + (_timeOutline * 2);
    int16_t timeH = _ttf->getTextHeight(timeSize) + (_timeOutline * 2);
    int16_t dateW = _ttf->getTextWidth(_dateText, dateSize) + (_dateOutline * 2);
    int16_t dateH = _ttf->getTextHeight(dateSize) + (_dateOutline * 2);
    
    int16_t blockH = timeH + _gapBetween + dateH;
    
    // Calculate individual positions
    int16_t timeY = centerY - (blockH/2) + (timeH/2);
    int16_t dateY = centerY + (blockH/2) - (dateH/2);
    
    // Draw time and date
    _ttf->drawTextAlignedOutlined(centerX, timeY, _timeText, timeSize,
                                  EL133UF1_WHITE, EL133UF1_BLACK,
                                  ALIGN_CENTER, ALIGN_MIDDLE, _timeOutline);
    _ttf->drawTextAlignedOutlined(centerX, dateY, _dateText, dateSize,
                                  EL133UF1_WHITE, EL133UF1_BLACK,
                                  ALIGN_CENTER, ALIGN_MIDDLE, _dateOutline);
}

ExclusionZone TimeDateElement::getExclusionZone(int16_t centerX, int16_t centerY) const {
    // MAXIMALIST exclusion zone: use cached dimensions with generous margins
    // CRITICAL: _cachedWidth is max(timeW, dateW) and _cachedHeight is timeH + gap + dateH
    // This ensures the exclusion zone covers BOTH time and date elements completely
    
    // Add substantial extra margin to width/height to account for text extent
    int16_t extraWidthMargin = 100;   // Extra margin for text extent (outlines, kerning, etc.)
    int16_t extraHeightMargin = 80;   // Extra margin for text extent
    
    // Use cached dimensions (already includes outline padding)
    // _cachedWidth = max(time width, date width) - ensures full width coverage
    // _cachedHeight = time height + gap + date height - ensures full height coverage
    int16_t safeWidth = _cachedWidth + extraWidthMargin;
    int16_t safeHeight = _cachedHeight + extraHeightMargin;
    
    // VERY LARGE padding to ensure minimum distance from other elements
    // This is critical for preventing overlap - use 500px minimum
    int16_t padding = 500;  // Minimum 500px distance from other elements
    
    Serial.printf("[TimeDate] Exclusion zone: center=(%d,%d) size=%dx%d (cached=%dx%d) pad=%d\n",
                 centerX, centerY, safeWidth, safeHeight, _cachedWidth, _cachedHeight, padding);
    
    return ExclusionZone(centerX, centerY, safeWidth, safeHeight, padding);
}

void TimeDateElement::getColors(uint8_t& textColor, uint8_t& outlineColor) const {
    textColor = EL133UF1_WHITE;
    outlineColor = EL133UF1_BLACK;
}

// ============================================================================
// QuoteElement Implementation
// ============================================================================

QuoteElement::QuoteElement(EL133UF1_TTF* ttf, const char* quoteText, const char* authorText)
    : _ttf(ttf), _quoteText(quoteText), _authorText(authorText),
      _quoteFontSize(128.0f), _authorFontSize(96.0f), _outlineWidth(2),
      _sizeScale(1.0f), _quoteLines(1),
      _quoteWidth(0), _quoteHeight(0), _authorWidth(0), _authorHeight(0),
      _totalWidth(0), _totalHeight(0)
{
    _wrappedQuote[0] = '\0';
    recalculateDimensions();
}

void QuoteElement::setAdaptiveSize(float scale) {
    _sizeScale = scale;
    recalculateDimensions();
}

void QuoteElement::wrapQuote() {
    // Use existing text placement analyzer for wrapping
    // Calculate available width (accounting for keepout and outline)
    // Use standard display width (1600x1200) minus margins
    int16_t displayWidth = 1600;  // Standard display width
    int16_t availableWidth = displayWidth - 200 - (_outlineWidth * 4);  // 200px keepout, outline padding
    
    // Try different line counts (1-3 lines)
    float bestScore = -1.0f;
    int bestLines = 1;
    char bestWrapped[512];
    int16_t bestWidth = 0;
    
    for (int targetLines = 1; targetLines <= 3; targetLines++) {
        char testWrapped[512];
        int actualLines = 0;
        
        int16_t targetWidth = (targetLines == 1) ? 0 : (availableWidth / targetLines) + 50;
        if (targetWidth > availableWidth) targetWidth = availableWidth;
        
        int16_t wrappedW = TextPlacementAnalyzer::wrapText(_ttf, _quoteText, 
                                                  _quoteFontSize * _sizeScale,
                                                  targetWidth, testWrapped, sizeof(testWrapped),
                                                  &actualLines);
        
        if (actualLines > 0 && wrappedW <= availableWidth) {
            // Prefer fewer lines if width is similar
            float score = (float)actualLines / (float)targetLines;
            if (score > bestScore || (score == bestScore && actualLines < bestLines)) {
                bestScore = score;
                bestLines = actualLines;
                strncpy(bestWrapped, testWrapped, sizeof(bestWrapped) - 1);
                bestWrapped[sizeof(bestWrapped) - 1] = '\0';
                bestWidth = wrappedW;
            }
        }
    }
    
    if (bestLines > 0) {
        strncpy(_wrappedQuote, bestWrapped, sizeof(_wrappedQuote) - 1);
        _wrappedQuote[sizeof(_wrappedQuote) - 1] = '\0';
        _quoteLines = bestLines;
        _quoteWidth = bestWidth;
    } else {
        // Fallback: single line
        strncpy(_wrappedQuote, _quoteText, sizeof(_wrappedQuote) - 1);
        _wrappedQuote[sizeof(_wrappedQuote) - 1] = '\0';
        _quoteLines = 1;
        _quoteWidth = _ttf->getTextWidth(_quoteText, _quoteFontSize * _sizeScale);
    }
}

void QuoteElement::recalculateDimensions() {
    float quoteSize = _quoteFontSize * _sizeScale;
    float authorSize = _authorFontSize * _sizeScale;
    
    // Wrap quote text
    wrapQuote();
    
    // Calculate quote block dimensions
    int16_t quoteLineHeight = _ttf->getTextHeight(quoteSize);
    int16_t quoteLineGap = quoteLineHeight / 4;
    _quoteHeight = _quoteLines * quoteLineHeight + (_quoteLines - 1) * quoteLineGap;
    
    // Calculate author dimensions
    char authorText[128];
    snprintf(authorText, sizeof(authorText), "— %s", _authorText ? _authorText : "Unknown");
    _authorWidth = _ttf->getTextWidth(authorText, authorSize);
    _authorHeight = _ttf->getTextHeight(authorSize);
    int16_t gapBeforeAuthor = _authorHeight / 2;
    
    // Total dimensions
    _totalWidth = ((_quoteWidth > _authorWidth) ? _quoteWidth : _authorWidth) + (_outlineWidth * 2);
    _totalHeight = _quoteHeight + gapBeforeAuthor + _authorHeight + (_outlineWidth * 2);
}

void QuoteElement::getDimensions(int16_t& width, int16_t& height) {
    width = _totalWidth;
    height = _totalHeight;
}

void QuoteElement::draw(int16_t centerX, int16_t centerY) {
    float quoteSize = _quoteFontSize * _sizeScale;
    float authorSize = _authorFontSize * _sizeScale;
    
    int16_t quoteLineHeight = _ttf->getTextHeight(quoteSize);
    int16_t quoteLineGap = quoteLineHeight / 4;
    int16_t gapBeforeAuthor = _ttf->getTextHeight(authorSize) / 2;
    
    // Calculate block edges (accounting for outline)
    int16_t blockTop = centerY - _totalHeight / 2 + _outlineWidth;
    int16_t blockLeft = centerX - _totalWidth / 2 + _outlineWidth;
    int16_t blockRight = centerX + _totalWidth / 2 - _outlineWidth;
    
    // Draw quote lines
    if (_quoteLines == 1) {
        int16_t quoteY = blockTop + quoteLineHeight / 2;
        _ttf->drawTextAlignedOutlined(blockLeft, quoteY, _wrappedQuote, quoteSize,
                                     EL133UF1_WHITE, EL133UF1_BLACK,
                                     ALIGN_LEFT, ALIGN_MIDDLE, _outlineWidth);
    } else {
        // Multi-line: draw each line
        char quoteCopy[512];
        strncpy(quoteCopy, _wrappedQuote, sizeof(quoteCopy) - 1);
        quoteCopy[sizeof(quoteCopy) - 1] = '\0';
        
        char* line = quoteCopy;
        int16_t startY = blockTop + quoteLineHeight / 2;
        
        for (int i = 0; i < _quoteLines && line && *line; i++) {
            char* nextLine = strchr(line, '\n');
            if (nextLine) {
                *nextLine = '\0';
            }
            
            int16_t lineY = startY + i * (quoteLineHeight + quoteLineGap);
            _ttf->drawTextAlignedOutlined(blockLeft, lineY, line, quoteSize,
                                         EL133UF1_WHITE, EL133UF1_BLACK,
                                         ALIGN_LEFT, ALIGN_MIDDLE, _outlineWidth);
            
            if (nextLine) {
                line = nextLine + 1;
            } else {
                break;
            }
        }
    }
    
    // Draw author (right-aligned)
    char authorText[128];
    snprintf(authorText, sizeof(authorText), "— %s", _authorText ? _authorText : "Unknown");
    int16_t authorY = blockTop + _quoteHeight + gapBeforeAuthor + _authorHeight / 2;
    
    _ttf->drawTextAlignedOutlined(blockRight, authorY, authorText, authorSize,
                                 EL133UF1_WHITE, EL133UF1_BLACK,
                                 ALIGN_RIGHT, ALIGN_MIDDLE, _outlineWidth);
}

ExclusionZone QuoteElement::getExclusionZone(int16_t centerX, int16_t centerY) const {
    // MAXIMALIST exclusion zone: use total dimensions with generous margins
    // CRITICAL: _totalWidth = max(quoteWidth, authorWidth) + outline
    //           _totalHeight = quoteHeight + gap + authorHeight + outline
    // This ensures the exclusion zone covers BOTH quote text AND author completely
    
    // Add substantial extra margin to account for text extent, outlines, and author positioning
    int16_t extraWidthMargin = 120;   // Extra margin for text extent (author can extend far right)
    int16_t extraHeightMargin = 100;  // Extra margin for text extent (multi-line quotes)
    
    // Use total dimensions (already includes outline padding)
    // _totalWidth = max(quote width, author width) - ensures full width coverage including right-aligned author
    // _totalHeight = quote height + gap + author height - ensures full height coverage
    int16_t safeWidth = _totalWidth + extraWidthMargin;
    int16_t safeHeight = _totalHeight + extraHeightMargin;
    
    // VERY LARGE padding to ensure minimum distance from other elements
    // This is critical for preventing overlap - use 500px minimum
    int16_t padding = 500;  // Minimum 500px distance from other elements
    
    Serial.printf("[Quote] Exclusion zone: center=(%d,%d) size=%dx%d (total=%dx%d, quote=%dx%d, author=%dx%d) pad=%d\n",
                 centerX, centerY, safeWidth, safeHeight, _totalWidth, _totalHeight,
                 _quoteWidth, _quoteHeight, _authorWidth, _authorHeight, padding);
    
    return ExclusionZone(centerX, centerY, safeWidth, safeHeight, padding);
}

void QuoteElement::getColors(uint8_t& textColor, uint8_t& outlineColor) const {
    textColor = EL133UF1_WHITE;
    outlineColor = EL133UF1_BLACK;
}

// ============================================================================
// WeatherElement Implementation
// ============================================================================

WeatherElement::WeatherElement(EL133UF1_TTF* ttf, const char* temperature, const char* condition, const char* location)
    : _ttf(ttf), _tempFontSize(180.0f), _conditionFontSize(96.0f), _locationFontSize(96.0f),
      _gapBetween(15), _outlineWidth(2), _sizeScale(1.0f),
      _cachedWidth(0), _cachedHeight(0),
      _cachedTempW(0), _cachedTempH(0),
      _cachedConditionW(0), _cachedConditionH(0),
      _cachedLocationW(0), _cachedLocationH(0)
{
    strncpy(_temperature, temperature ? temperature : "72°F", sizeof(_temperature) - 1);
    _temperature[sizeof(_temperature) - 1] = '\0';
    strncpy(_condition, condition ? condition : "Partly Cloudy", sizeof(_condition) - 1);
    _condition[sizeof(_condition) - 1] = '\0';
    strncpy(_location, location ? location : "San Francisco, CA", sizeof(_location) - 1);
    _location[sizeof(_location) - 1] = '\0';
    recalculateDimensions();
}

void WeatherElement::setAdaptiveSize(float scale) {
    _sizeScale = scale;
    recalculateDimensions();
}

void WeatherElement::recalculateDimensions() {
    float tempSize = _tempFontSize * _sizeScale;
    float conditionSize = _conditionFontSize * _sizeScale;
    float locationSize = _locationFontSize * _sizeScale;
    
    _cachedTempW = _ttf->getTextWidth(_temperature, tempSize) + (_outlineWidth * 2);
    _cachedTempH = _ttf->getTextHeight(tempSize) + (_outlineWidth * 2);
    _cachedConditionW = _ttf->getTextWidth(_condition, conditionSize) + (_outlineWidth * 2);
    _cachedConditionH = _ttf->getTextHeight(conditionSize) + (_outlineWidth * 2);
    _cachedLocationW = _ttf->getTextWidth(_location, locationSize) + (_outlineWidth * 2);
    _cachedLocationH = _ttf->getTextHeight(locationSize) + (_outlineWidth * 2);
    
    _cachedWidth = ((_cachedTempW > _cachedConditionW) ? _cachedTempW : _cachedConditionW);
    if (_cachedLocationW > _cachedWidth) _cachedWidth = _cachedLocationW;
    
    _cachedHeight = _cachedTempH + _gapBetween + _cachedConditionH + _gapBetween + _cachedLocationH;
}

void WeatherElement::getDimensions(int16_t& width, int16_t& height) {
    width = _cachedWidth;
    height = _cachedHeight;
}

void WeatherElement::draw(int16_t centerX, int16_t centerY) {
    float tempSize = _tempFontSize * _sizeScale;
    float conditionSize = _conditionFontSize * _sizeScale;
    float locationSize = _locationFontSize * _sizeScale;
    
    int16_t tempY = centerY - (_cachedHeight / 2) + (_cachedTempH / 2);
    int16_t conditionY = centerY;
    int16_t locationY = centerY + (_cachedHeight / 2) - (_cachedLocationH / 2);
    
    // Draw temperature (large, centered)
    _ttf->drawTextAlignedOutlined(centerX, tempY, _temperature, tempSize,
                                  EL133UF1_WHITE, EL133UF1_BLACK,
                                  ALIGN_CENTER, ALIGN_MIDDLE, _outlineWidth);
    
    // Draw condition (medium, centered)
    _ttf->drawTextAlignedOutlined(centerX, conditionY, _condition, conditionSize,
                                  EL133UF1_WHITE, EL133UF1_BLACK,
                                  ALIGN_CENTER, ALIGN_MIDDLE, _outlineWidth);
    
    // Draw location (small, centered)
    _ttf->drawTextAlignedOutlined(centerX, locationY, _location, locationSize,
                                  EL133UF1_WHITE, EL133UF1_BLACK,
                                  ALIGN_CENTER, ALIGN_MIDDLE, _outlineWidth);
}

ExclusionZone WeatherElement::getExclusionZone(int16_t centerX, int16_t centerY) const {
    // MAXIMALIST exclusion zone: use cached dimensions with generous margins
    int16_t extraWidthMargin = 80;   // Extra margin for text extent
    int16_t extraHeightMargin = 60;  // Extra margin for text extent
    
    // Use cached dimensions (already includes outline padding)
    int16_t safeWidth = _cachedWidth + extraWidthMargin;
    int16_t safeHeight = _cachedHeight + extraHeightMargin;
    
    // VERY LARGE padding to ensure minimum distance from other elements
    // This is critical for preventing overlap - use 500px minimum
    int16_t padding = 500;  // Minimum 500px distance from other elements
    
    return ExclusionZone(centerX, centerY, safeWidth, safeHeight, padding);
}

void WeatherElement::getColors(uint8_t& textColor, uint8_t& outlineColor) const {
    textColor = EL133UF1_WHITE;
    outlineColor = EL133UF1_BLACK;
}

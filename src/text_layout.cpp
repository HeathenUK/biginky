/**
 * @file text_layout.cpp
 * @brief Implementation of modular text layout system
 */

#include "text_layout.h"
#include "EL133UF1.h"
#include "EL133UF1_TextPlacement.h"  // For TextPlacementAnalyzer
#include <math.h>
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

// ============================================================================
// TextLayoutEngine Implementation
// ============================================================================

TextLayoutEngine::TextLayoutEngine(EL133UF1* display, EL133UF1_TTF* ttf)
    : _display(display), _ttf(ttf), _analyzer(), _keepout(100), _numExclusionZones(0)
{
    // Enable parallel mode for faster scoring on ESP32-P4
    _analyzer.setParallelMode(true);
    _analyzer.setKeepout(_keepout);
}

void TextLayoutEngine::setKeepout(int16_t margin) {
    _keepout = margin;
    _analyzer.setKeepout(margin);
}

void TextLayoutEngine::clearExclusionZones() {
    _numExclusionZones = 0;
    _analyzer.clearExclusionZones();
    Serial.println("[LayoutEngine] Cleared all exclusion zones");
}

void TextLayoutEngine::addExclusionZone(const ExclusionZone& zone) {
    if (_numExclusionZones < MAX_EXCLUSION_ZONES) {
        _exclusionZones[_numExclusionZones++] = zone;
        // Also add to analyzer for use in scanForBestPosition
        // IMPORTANT: The analyzer's addExclusionZone expects a TextPlacementRegion
        // and padding separately. The padding is applied when checking overlaps.
        TextPlacementRegion region = {zone.x, zone.y, zone.width, zone.height, 0.0f};
        bool added = _analyzer.addExclusionZone(region, zone.padding);
        Serial.printf("[LayoutEngine] Added exclusion zone to analyzer: center=(%d,%d) size=%dx%d pad=%d, success=%d\n",
                     zone.x, zone.y, zone.width, zone.height, zone.padding, added ? 1 : 0);
    } else {
        Serial.println("[LayoutEngine] ERROR: Max exclusion zones reached!");
    }
}

bool TextLayoutEngine::overlapsExclusionZone(int16_t x, int16_t y, int16_t w, int16_t h) const {
    for (int i = 0; i < _numExclusionZones; i++) {
        if (_exclusionZones[i].overlaps(x, y, w, h)) {
            return true;
        }
    }
    return false;
}

bool TextLayoutEngine::isWithinSafeArea(int16_t x, int16_t y, int16_t w, int16_t h) const {
    int16_t left = x - w / 2;
    int16_t right = x + w / 2;
    int16_t top = y - h / 2;
    int16_t bottom = y + h / 2;
    
    int16_t dispW = _display->width();
    int16_t dispH = _display->height();
    
    return (left >= _keepout && right <= dispW - _keepout &&
            top >= _keepout && bottom <= dispH - _keepout);
}

float TextLayoutEngine::computeContrastScore(int16_t x, int16_t y, int16_t w, int16_t h,
                                             uint8_t textColor, uint8_t outlineColor) {
    // Simplified contrast: count pixels matching text/outline colors
    // Lower match = better contrast
    
    if (!_display || w <= 0 || h <= 0) return 0.5f;
    
    // Clamp to display bounds
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > _display->width()) w = _display->width() - x;
    if (y + h > _display->height()) h = _display->height() - y;
    if (w <= 0 || h <= 0) return 0.5f;
    
    // Sample pixels (every 4th pixel for speed)
    uint32_t textMatches = 0;
    uint32_t outlineMatches = 0;
    uint32_t totalSamples = 0;
    
#if EL133UF1_USE_ARGB8888
    if (_display->isARGBMode() && _display->getBufferARGB()) {
        // ARGB mode: read from ARGB buffer
        uint32_t* buffer = _display->getBufferARGB();
        int stride = _display->width();
        
        for (int16_t py = 0; py < h; py += 4) {
            for (int16_t px = 0; px < w; px += 4) {
                uint32_t argb = buffer[(y + py) * stride + (x + px)];
                uint8_t pixelColor = EL133UF1::argbToColor(argb);
                if (pixelColor == textColor) {
                    textMatches++;
                }
                if (pixelColor == outlineColor) {
                    outlineMatches++;
                }
                totalSamples++;
            }
        }
    } else
#endif
    {
        // L8 mode: use getPixel
        for (int16_t py = 0; py < h; py += 4) {
            for (int16_t px = 0; px < w; px += 4) {
                uint8_t pixelColor = _display->getPixel(x + px, y + py);
                if (pixelColor == textColor) {
                    textMatches++;
                }
                if (pixelColor == outlineColor) {
                    outlineMatches++;
                }
                totalSamples++;
            }
        }
    }
    
    if (totalSamples == 0) return 0.5f;
    
    // Penalize matching colors
    float textMatchPct = (float)textMatches / totalSamples;
    float outlineMatchPct = (float)outlineMatches / totalSamples;
    
    // Text matching is worse than outline matching
    float penalty = textMatchPct * 1.0f + outlineMatchPct * 0.5f;
    float score = 1.0f - penalty;
    
    // Clamp to 0-1
    if (score < 0.0f) score = 0.0f;
    if (score > 1.0f) score = 1.0f;
    
    return score;
}

float TextLayoutEngine::computeUniformityScore(int16_t x, int16_t y, int16_t w, int16_t h) {
    // Simplified uniformity: compute variance of pixel values
    // Lower variance = more uniform = better
    
    if (!_display || w <= 0 || h <= 0) return 0.5f;
    
    // Clamp to display bounds
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > _display->width()) w = _display->width() - x;
    if (y + h > _display->height()) h = _display->height() - y;
    if (w <= 0 || h <= 0) return 0.5f;
    
    // Map Spectra colors to luminance values for variance calculation
    static const uint8_t spectraLuminance[8] = {
        0,    // BLACK (0)
        255,  // WHITE (1)
        200,  // YELLOW (2)
        120,  // RED (3)
        128,  // (unused 4)
        80,   // BLUE (5)
        100,  // GREEN (6)
        128   // (unused 7)
    };
    
    // Sample pixels (every 4th pixel)
    uint64_t sum = 0;
    uint64_t sumSq = 0;
    uint32_t count = 0;
    
#if EL133UF1_USE_ARGB8888
    if (_display->isARGBMode() && _display->getBufferARGB()) {
        // ARGB mode: read from ARGB buffer
        uint32_t* buffer = _display->getBufferARGB();
        int stride = _display->width();
        
        for (int16_t py = 0; py < h; py += 4) {
            for (int16_t px = 0; px < w; px += 4) {
                uint32_t argb = buffer[(y + py) * stride + (x + px)];
                uint8_t pixelColor = EL133UF1::argbToColor(argb);
                uint8_t lum = spectraLuminance[pixelColor & 0x07];
                sum += lum;
                sumSq += (uint32_t)lum * lum;
                count++;
            }
        }
    } else
#endif
    {
        // L8 mode: use getPixel
        for (int16_t py = 0; py < h; py += 4) {
            for (int16_t px = 0; px < w; px += 4) {
                uint8_t pixelColor = _display->getPixel(x + px, y + py);
                uint8_t lum = spectraLuminance[pixelColor & 0x07];
                sum += lum;
                sumSq += (uint32_t)lum * lum;
                count++;
            }
        }
    }
    
    if (count == 0) return 0.5f;
    
    // Compute variance
    float mean = (float)sum / count;
    float meanSq = (float)sumSq / count;
    float variance = meanSq - (mean * mean);
    
    // Normalize variance (typical range 0-6000 for 8-bit values)
    float normalizedVar = variance / 6000.0f;
    if (normalizedVar > 1.0f) normalizedVar = 1.0f;
    
    // Uniformity = inverse of normalized variance
    float uniformity = 1.0f - normalizedVar;
    
    // Clamp to 0-1
    if (uniformity < 0.0f) uniformity = 0.0f;
    if (uniformity > 1.0f) uniformity = 1.0f;
    
    return uniformity;
}

float TextLayoutEngine::scoreRegion(int16_t x, int16_t y, int16_t w, int16_t h,
                                    uint8_t textColor, uint8_t outlineColor) {
    // Combined score: 60% contrast, 40% uniformity
    float contrast = computeContrastScore(x, y, w, h, textColor, outlineColor);
    float uniformity = computeUniformityScore(x, y, w, h);
    
    float score = 0.6f * contrast + 0.4f * uniformity;
    
    // Clamp to 0-1
    if (score < 0.0f) score = 0.0f;
    if (score > 1.0f) score = 1.0f;
    
    return score;
}

bool TextLayoutEngine::placeElement(TextContentElement* element, 
                                    int16_t& centerX, int16_t& centerY, float& score) {
    if (!element || !_display || !_ttf) {
        return false;
    }
    
    // Get element dimensions
    int16_t width, height;
    element->getDimensions(width, height);
    
    if (width <= 0 || height <= 0) {
        return false;
    }
    
    // Get text colors
    uint8_t textColor, outlineColor;
    element->getColors(textColor, outlineColor);
    
    // Use TextPlacementAnalyzer for parallel scanning (much faster on ESP32-P4)
    // This automatically uses dual-core parallel scoring when enabled
    Serial.printf("[LayoutEngine] Finding position for element (size: %dx%d)\n", width, height);
    TextPlacementRegion bestPos = _analyzer.scanForBestPosition(
        _display, width, height, textColor, outlineColor);
    
    Serial.printf("[LayoutEngine] Placed element at (%d,%d) with score %.3f\n", 
                  bestPos.x, bestPos.y, bestPos.score);
    
    centerX = bestPos.x;
    centerY = bestPos.y;
    score = bestPos.score;
    
    return (score > 0.0f);
}

bool TextLayoutEngine::placeElements(TextContentElement** elements, int numElements) {
    if (!elements || numElements <= 0) {
        return false;
    }
    
    Serial.printf("[LayoutEngine] Placing %d elements using modular layout system\n", numElements);
    
    // Sort by priority (higher priority first)
    // Simple bubble sort (small arrays, so fine)
    for (int i = 0; i < numElements - 1; i++) {
        for (int j = 0; j < numElements - i - 1; j++) {
            if (elements[j]->getPriority() < elements[j+1]->getPriority()) {
                TextContentElement* temp = elements[j];
                elements[j] = elements[j+1];
                elements[j+1] = temp;
            }
        }
    }
    
    // Place each element
    for (int i = 0; i < numElements; i++) {
        Serial.printf("[LayoutEngine] Placing element %d/%d (priority: %d)\n", 
                     i+1, numElements, elements[i]->getPriority());
        Serial.printf("[LayoutEngine] Current exclusion zone count in analyzer: %d\n", _analyzer.getExclusionZoneCount());
        
        // Yield to other tasks periodically to prevent watchdog timeout
        if (i > 0) {
            vTaskDelay(1);  // Yield after each element placement
        }
        
        int16_t centerX, centerY;
        float score;
        
        // Try adaptive sizing if supported
        // PRIORITY: Maximize font size while ensuring good spacing
        bool placed = false;
        float bestScale = 1.0f;
        float bestScore = 0.0f;
        int16_t bestX = 0, bestY = 0;
        
        if (elements[i]->canAdaptSize()) {
            // Try full size first, then reduce only if necessary
            // This prioritizes readability (larger fonts) while maintaining spacing
            for (int attempt = 0; attempt < 4; attempt++) {
                float scale = 1.0f - (attempt * 0.12f);  // 1.0, 0.88, 0.76, 0.64
                if (scale < 0.60f) scale = 0.60f;  // Minimum 60% size
                
                elements[i]->setAdaptiveSize(scale);
                
                int16_t testX, testY;
                float testScore;
                if (placeElement(elements[i], testX, testY, testScore)) {
                    // Prefer larger fonts with good spacing
                    // Accept if score is good (>= 0.3) OR if we're at minimum size
                    // But track the best combination of size and score
                    float sizeWeightedScore = testScore * scale;  // Reward larger fonts
                    
                    if (testScore >= 0.3f && sizeWeightedScore > bestScore) {
                        bestScore = sizeWeightedScore;
                        bestScale = scale;
                        bestX = testX;
                        bestY = testY;
                        placed = true;
                        
                        // If we found a good position at full size, prefer it
                        if (scale >= 1.0f && testScore >= 0.3f) {
                            break;  // Found good position at full size, use it
                        }
                    }
                    
                    // If at minimum size, accept even if score is lower
                    if (scale <= 0.60f && testScore >= 0.2f) {
                        bestScore = sizeWeightedScore;
                        bestScale = scale;
                        bestX = testX;
                        bestY = testY;
                        placed = true;
                        break;  // At minimum, accept this
                    }
                }
            }
            
            // Use the best scale we found
            if (placed) {
                elements[i]->setAdaptiveSize(bestScale);
                centerX = bestX;
                centerY = bestY;
                score = bestScore / bestScale;  // Restore original score
                Serial.printf("[LayoutEngine] Selected scale %.2f for element %d (score=%.3f)\n",
                             bestScale, i+1, score);
            }
        } else {
            placed = placeElement(elements[i], centerX, centerY, score);
        }
        
        if (!placed) {
            // Fallback to center
            centerX = _display->width() / 2;
            centerY = _display->height() / 2;
            score = 0.0f;
        }
        
        // CRITICAL: Add exclusion zone BEFORE drawing, so it's available for next element
        // This ensures proper spacing - the exclusion zone must be in the analyzer
        // BEFORE we start placing the next element
        ExclusionZone zone = elements[i]->getExclusionZone(centerX, centerY);
        Serial.printf("[LayoutEngine] Element %d placed at (%d,%d), exclusion zone: center=(%d,%d) size=%dx%d pad=%d\n",
                     i+1, centerX, centerY, zone.x, zone.y, zone.width, zone.height, zone.padding);
        Serial.printf("[LayoutEngine] Exclusion zone bounds: left=%d right=%d top=%d bottom=%d\n",
                     zone.left(), zone.right(), zone.top(), zone.bottom());
        
        // Add to analyzer IMMEDIATELY so next element sees it
        addExclusionZone(zone);
        
        // Now draw the element
        elements[i]->draw(centerX, centerY);
    }
    
    return true;
}

/**
 * @file EL133UF1_TextPlacement.cpp
 * @brief Implementation of intelligent text placement analyzer
 * 
 * Optimized for:
 * - ESP32-P4: ARGB8888 buffer, PIE SIMD, dual-core parallel analysis
 * - RP2350: L8 buffer with cache-friendly access patterns
 */

#include "EL133UF1_TextPlacement.h"
#include <string.h>
#include <math.h>

// Static member initialization
uint8_t TextPlacementAnalyzer::_argbToLuminance[256];
bool TextPlacementAnalyzer::_lutInitialized = false;

// ============================================================================
// Spectra6Histogram implementation
// ============================================================================

uint32_t& Spectra6Histogram::operator[](uint8_t spectraCode) {
    switch (spectraCode) {
        case EL133UF1_BLACK:  return black;
        case EL133UF1_WHITE:  return white;
        case EL133UF1_YELLOW: return yellow;
        case EL133UF1_RED:    return red;
        case EL133UF1_BLUE:   return blue;
        case EL133UF1_GREEN:  return green;
        default:              return white;  // Fallback
    }
}

uint32_t Spectra6Histogram::operator[](uint8_t spectraCode) const {
    switch (spectraCode) {
        case EL133UF1_BLACK:  return black;
        case EL133UF1_WHITE:  return white;
        case EL133UF1_YELLOW: return yellow;
        case EL133UF1_RED:    return red;
        case EL133UF1_BLUE:   return blue;
        case EL133UF1_GREEN:  return green;
        default:              return 0;
    }
}

uint8_t Spectra6Histogram::dominantColor() const {
    uint32_t maxCount = black;
    uint8_t dominant = EL133UF1_BLACK;
    
    if (white > maxCount)  { maxCount = white;  dominant = EL133UF1_WHITE; }
    if (yellow > maxCount) { maxCount = yellow; dominant = EL133UF1_YELLOW; }
    if (red > maxCount)    { maxCount = red;    dominant = EL133UF1_RED; }
    if (blue > maxCount)   { maxCount = blue;   dominant = EL133UF1_BLUE; }
    if (green > maxCount)  { maxCount = green;  dominant = EL133UF1_GREEN; }
    
    return dominant;
}

float Spectra6Histogram::percentage(uint8_t spectraCode) const {
    if (total == 0) return 0.0f;
    return (float)(*this)[spectraCode] / (float)total;
}

// ============================================================================
// TextPlacementAnalyzer implementation
// ============================================================================

TextPlacementAnalyzer::TextPlacementAnalyzer() 
    : _useParallel(true),  // Enable by default on ESP32-P4
      _keepout()           // No keepout by default
{
    initLUT();
}

bool TextPlacementAnalyzer::isWithinSafeArea(int16_t displayWidth, int16_t displayHeight,
                                              int16_t x, int16_t y, int16_t w, int16_t h) const
{
    // Calculate the region bounds (x,y is center)
    int16_t left = x - w / 2;
    int16_t right = x + w / 2;
    int16_t top = y - h / 2;
    int16_t bottom = y + h / 2;
    
    // Check against keepout margins
    if (left < _keepout.left) return false;
    if (right > displayWidth - _keepout.right) return false;
    if (top < _keepout.top) return false;
    if (bottom > displayHeight - _keepout.bottom) return false;
    
    return true;
}

TextPlacementAnalyzer::~TextPlacementAnalyzer() {
}

void TextPlacementAnalyzer::initLUT() {
    if (_lutInitialized) return;
    
    // Initialize luminance LUT
    // This maps 0-255 values to approximate luminance
    // (Used for variance computation from single channel)
    for (int i = 0; i < 256; i++) {
        _argbToLuminance[i] = i;  // Identity for now, can add gamma correction
    }
    
    _lutInitialized = true;
}

// ============================================================================
// Main API
// ============================================================================

TextPlacementRegion TextPlacementAnalyzer::findBestPosition(
    EL133UF1* display, EL133UF1_TTF* ttf,
    const char* text, float fontSize,
    const TextPlacementRegion* candidates, int numCandidates,
    uint8_t textColor, uint8_t outlineColor)
{
    if (!display || !ttf || !candidates || numCandidates <= 0) {
        return TextPlacementRegion{0, 0, 0, 0, 0.0f};
    }
    
    // Get text dimensions (used as fallback if candidate dimensions are 0)
    int16_t defaultWidth = ttf->getTextWidth(text, fontSize);
    int16_t defaultHeight = ttf->getTextHeight(fontSize);
    int16_t dispW = display->width();
    int16_t dispH = display->height();
    
    // Copy candidates and fill in dimensions if not already set
    // This allows callers to specify custom dimensions (e.g., for combined text blocks)
    TextPlacementRegion* scored = new TextPlacementRegion[numCandidates];
    int validCount = 0;
    
    for (int i = 0; i < numCandidates; i++) {
        scored[i] = candidates[i];
        // Only use default dimensions if candidate has zero dimensions
        if (scored[i].width <= 0) scored[i].width = defaultWidth;
        if (scored[i].height <= 0) scored[i].height = defaultHeight;
        
        // Check if this candidate is within the safe area (outside keepout margins)
        if (isWithinSafeArea(dispW, dispH, scored[i].x, scored[i].y, 
                             scored[i].width, scored[i].height)) {
            scored[i].score = 0.0f;  // Will be scored
            validCount++;
        } else {
            scored[i].score = -1.0f;  // Mark as invalid (in keepout zone)
        }
    }
    
    // If no valid candidates, return first candidate with score 0
    if (validCount == 0) {
        Serial.println("[TextPlacement] Warning: All candidates in keepout zone!");
        TextPlacementRegion result = scored[0];
        result.score = 0.0f;
        delete[] scored;
        return result;
    }
    
#if defined(SOC_PPA_SUPPORTED) && SOC_PPA_SUPPORTED
    if (_useParallel && validCount > 2) {
        // Use dual-core parallel scoring on ESP32-P4
        // Note: parallel scorer will skip candidates with score < 0
        scoreRegionsParallel(display, scored, numCandidates, textColor, outlineColor);
    } else
#endif
    {
        // Sequential scoring - use each candidate's own dimensions
        for (int i = 0; i < numCandidates; i++) {
            // Skip candidates marked as invalid (in keepout zone)
            if (scored[i].score < 0.0f) continue;
            
            int16_t rx = scored[i].drawX();
            int16_t ry = scored[i].drawY();
            scored[i].score = scoreRegion(display, rx, ry, 
                                          scored[i].width, scored[i].height,
                                          textColor, outlineColor);
        }
    }
    
    // Find best scoring region (skip invalid candidates with score < 0)
    int bestIdx = -1;
    float bestScore = -1.0f;
    for (int i = 0; i < numCandidates; i++) {
        if (scored[i].score > bestScore) {
            bestScore = scored[i].score;
            bestIdx = i;
        }
    }
    
    // Fallback to first candidate if somehow no valid one found
    if (bestIdx < 0) bestIdx = 0;
    
    TextPlacementRegion result = scored[bestIdx];
    delete[] scored;
    
    return result;
}

float TextPlacementAnalyzer::scoreRegion(EL133UF1* display, int16_t x, int16_t y,
                                          int16_t w, int16_t h,
                                          uint8_t textColor, uint8_t outlineColor)
{
    RegionMetrics metrics = analyzeRegion(display, x, y, w, h, textColor, outlineColor);
    return metrics.overallScore;
}

RegionMetrics TextPlacementAnalyzer::analyzeRegion(EL133UF1* display, 
                                                    int16_t x, int16_t y,
                                                    int16_t w, int16_t h,
                                                    uint8_t textColor, 
                                                    uint8_t outlineColor)
{
    RegionMetrics metrics;
    memset(&metrics, 0, sizeof(metrics));
    
    if (!display || w <= 0 || h <= 0) {
        return metrics;
    }
    
    // Clamp to display bounds
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > display->width()) w = display->width() - x;
    if (y + h > display->height()) h = display->height() - y;
    if (w <= 0 || h <= 0) return metrics;
    
    // Get histogram
    getColorHistogram(display, x, y, w, h, metrics.histogram);
    
    // Get variance
    metrics.variance = computeVariance(display, x, y, w, h);
    
    // Get edge density
    metrics.edgeDensity = computeEdgeDensity(display, x, y, w, h);
    
    // Compute contrast score
    metrics.contrastScore = computeContrastScore(metrics.histogram, textColor, outlineColor);
    
    // Compute uniformity score (inverse of normalized variance)
    // Normalize variance: typical range 0-6000 for 8-bit values
    float normalizedVar = metrics.variance / 6000.0f;
    if (normalizedVar > 1.0f) normalizedVar = 1.0f;
    metrics.uniformityScore = 1.0f - normalizedVar;
    
    // Compute overall weighted score
    float edgeScore = 1.0f - metrics.edgeDensity;
    metrics.overallScore = _weights.contrast * metrics.contrastScore +
                           _weights.uniformity * metrics.uniformityScore +
                           _weights.edgeAvoidance * edgeScore;
    
    // Clamp to 0-1
    if (metrics.overallScore < 0.0f) metrics.overallScore = 0.0f;
    if (metrics.overallScore > 1.0f) metrics.overallScore = 1.0f;
    
    return metrics;
}

// ============================================================================
// Color Histogram
// ============================================================================

void TextPlacementAnalyzer::getColorHistogram(EL133UF1* display, 
                                               int16_t x, int16_t y,
                                               int16_t w, int16_t h, 
                                               Spectra6Histogram& histogram)
{
    memset(&histogram, 0, sizeof(histogram));
    
    if (!display) return;
    
#if EL133UF1_USE_ARGB8888
    if (display->isARGBMode() && display->getBufferARGB()) {
        getColorHistogramARGB(display->getBufferARGB(), display->width(),
                              x, y, w, h, histogram);
        return;
    }
#endif
    
    if (display->getBuffer()) {
        getColorHistogramL8(display->getBuffer(), display->width(),
                            x, y, w, h, histogram);
    }
}

#if EL133UF1_USE_ARGB8888
void TextPlacementAnalyzer::getColorHistogramARGB(uint32_t* buffer, int stride,
                                                   int16_t x, int16_t y, 
                                                   int16_t w, int16_t h,
                                                   Spectra6Histogram& histogram)
{
    // ARGB8888 format: 0xAARRGGBB
    // We need to map back to Spectra colors
    // Use the color mapping from EL133UF1::argbToColor()
    
    uint32_t* row = buffer + y * stride + x;
    
    for (int py = 0; py < h; py++) {
        int px = 0;
        
        // Process 4 pixels at a time (unrolled for cache efficiency)
        for (; px + 4 <= w; px += 4) {
            uint32_t p0 = row[px + 0];
            uint32_t p1 = row[px + 1];
            uint32_t p2 = row[px + 2];
            uint32_t p3 = row[px + 3];
            
            // Convert ARGB to Spectra color and increment histogram
            histogram[EL133UF1::argbToColor(p0)]++;
            histogram[EL133UF1::argbToColor(p1)]++;
            histogram[EL133UF1::argbToColor(p2)]++;
            histogram[EL133UF1::argbToColor(p3)]++;
        }
        
        // Handle remaining pixels
        for (; px < w; px++) {
            histogram[EL133UF1::argbToColor(row[px])]++;
        }
        
        row += stride;
    }
    
    histogram.total = w * h;
}
#endif

void TextPlacementAnalyzer::getColorHistogramL8(uint8_t* buffer, int stride,
                                                 int16_t x, int16_t y, 
                                                 int16_t w, int16_t h,
                                                 Spectra6Histogram& histogram)
{
    // L8 format: 1 byte per pixel, value is direct Spectra color code (0-6)
    uint8_t* row = buffer + y * stride + x;
    
    for (int py = 0; py < h; py++) {
        int px = 0;
        
        // Process 8 pixels at a time for cache efficiency
        for (; px + 8 <= w; px += 8) {
            histogram[row[px + 0] & 0x07]++;
            histogram[row[px + 1] & 0x07]++;
            histogram[row[px + 2] & 0x07]++;
            histogram[row[px + 3] & 0x07]++;
            histogram[row[px + 4] & 0x07]++;
            histogram[row[px + 5] & 0x07]++;
            histogram[row[px + 6] & 0x07]++;
            histogram[row[px + 7] & 0x07]++;
        }
        
        // Handle remaining pixels
        for (; px < w; px++) {
            histogram[row[px] & 0x07]++;
        }
        
        row += stride;
    }
    
    histogram.total = w * h;
}

// ============================================================================
// Variance Computation
// ============================================================================

float TextPlacementAnalyzer::computeVariance(EL133UF1* display, 
                                              int16_t x, int16_t y,
                                              int16_t w, int16_t h)
{
    if (!display) return 0.0f;
    
#if EL133UF1_USE_ARGB8888
    if (display->isARGBMode() && display->getBufferARGB()) {
        return computeVarianceARGB(display->getBufferARGB(), display->width(),
                                   x, y, w, h);
    }
#endif
    
    if (display->getBuffer()) {
        return computeVarianceL8(display->getBuffer(), display->width(),
                                 x, y, w, h);
    }
    
    return 0.0f;
}

#if EL133UF1_USE_ARGB8888
float TextPlacementAnalyzer::computeVarianceARGB(uint32_t* buffer, int stride,
                                                  int16_t x, int16_t y, 
                                                  int16_t w, int16_t h)
{
    // Use green channel as luminance proxy (middle of RGB spectrum)
    // This is faster than computing true luminance and works well for variance
    
    uint32_t* row = buffer + y * stride + x;
    uint64_t sum = 0;
    uint64_t sumSq = 0;
    uint32_t count = w * h;
    
    for (int py = 0; py < h; py++) {
        int px = 0;
        
        // Process 4 pixels at a time
        for (; px + 4 <= w; px += 4) {
            // Extract green channel (bits 8-15 in ARGB8888)
            uint32_t g0 = (row[px + 0] >> 8) & 0xFF;
            uint32_t g1 = (row[px + 1] >> 8) & 0xFF;
            uint32_t g2 = (row[px + 2] >> 8) & 0xFF;
            uint32_t g3 = (row[px + 3] >> 8) & 0xFF;
            
            sum += g0 + g1 + g2 + g3;
            sumSq += g0*g0 + g1*g1 + g2*g2 + g3*g3;
        }
        
        // Handle remaining pixels
        for (; px < w; px++) {
            uint32_t g = (row[px] >> 8) & 0xFF;
            sum += g;
            sumSq += g * g;
        }
        
        row += stride;
    }
    
    if (count == 0) return 0.0f;
    
    // Variance = E[X²] - E[X]²
    float mean = (float)sum / count;
    float meanSq = (float)sumSq / count;
    return meanSq - (mean * mean);
}
#endif

float TextPlacementAnalyzer::computeVarianceL8(uint8_t* buffer, int stride,
                                                int16_t x, int16_t y, 
                                                int16_t w, int16_t h)
{
    // L8 format: Spectra color codes (0-6)
    // Map to luminance values for variance computation
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
    
    uint8_t* row = buffer + y * stride + x;
    uint64_t sum = 0;
    uint64_t sumSq = 0;
    uint32_t count = w * h;
    
    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            uint8_t lum = spectraLuminance[row[px] & 0x07];
            sum += lum;
            sumSq += (uint32_t)lum * lum;
        }
        row += stride;
    }
    
    if (count == 0) return 0.0f;
    
    float mean = (float)sum / count;
    float meanSq = (float)sumSq / count;
    return meanSq - (mean * mean);
}

// ============================================================================
// Edge Density Computation
// ============================================================================

float TextPlacementAnalyzer::computeEdgeDensity(EL133UF1* display, 
                                                 int16_t x, int16_t y,
                                                 int16_t w, int16_t h)
{
    if (!display) return 0.0f;
    
#if EL133UF1_USE_ARGB8888
    if (display->isARGBMode() && display->getBufferARGB()) {
        return computeEdgeDensityARGB(display->getBufferARGB(), display->width(),
                                      x, y, w, h);
    }
#endif
    
    if (display->getBuffer()) {
        return computeEdgeDensityL8(display->getBuffer(), display->width(),
                                    x, y, w, h);
    }
    
    return 0.0f;
}

#if EL133UF1_USE_ARGB8888
float TextPlacementAnalyzer::computeEdgeDensityARGB(uint32_t* buffer, int stride,
                                                     int16_t x, int16_t y, 
                                                     int16_t w, int16_t h)
{
    // Simple edge detection: count pixels where neighbor differs significantly
    // Use green channel for luminance comparison
    
    if (w < 2 || h < 2) return 0.0f;
    
    uint32_t edgeCount = 0;
    uint32_t totalChecked = 0;
    
    // Edge threshold (luminance difference to count as edge)
    const int EDGE_THRESHOLD = 40;
    
    for (int py = 0; py < h - 1; py++) {
        uint32_t* row = buffer + (y + py) * stride + x;
        uint32_t* rowBelow = row + stride;
        
        for (int px = 0; px < w - 1; px++) {
            int g = (row[px] >> 8) & 0xFF;
            int gRight = (row[px + 1] >> 8) & 0xFF;
            int gBelow = (rowBelow[px] >> 8) & 0xFF;
            
            // Check horizontal and vertical gradients
            int gradX = abs(g - gRight);
            int gradY = abs(g - gBelow);
            int grad = (gradX > gradY) ? gradX : gradY;
            
            if (grad > EDGE_THRESHOLD) {
                edgeCount++;
            }
            totalChecked++;
        }
    }
    
    if (totalChecked == 0) return 0.0f;
    return (float)edgeCount / (float)totalChecked;
}
#endif

float TextPlacementAnalyzer::computeEdgeDensityL8(uint8_t* buffer, int stride,
                                                   int16_t x, int16_t y, 
                                                   int16_t w, int16_t h)
{
    // For L8 (Spectra colors), an "edge" is where adjacent pixels have different colors
    
    if (w < 2 || h < 2) return 0.0f;
    
    uint32_t edgeCount = 0;
    uint32_t totalChecked = 0;
    
    for (int py = 0; py < h - 1; py++) {
        uint8_t* row = buffer + (y + py) * stride + x;
        uint8_t* rowBelow = row + stride;
        
        for (int px = 0; px < w - 1; px++) {
            uint8_t c = row[px] & 0x07;
            uint8_t cRight = row[px + 1] & 0x07;
            uint8_t cBelow = rowBelow[px] & 0x07;
            
            // Count color transitions as edges
            if (c != cRight || c != cBelow) {
                edgeCount++;
            }
            totalChecked++;
        }
    }
    
    if (totalChecked == 0) return 0.0f;
    return (float)edgeCount / (float)totalChecked;
}

// ============================================================================
// Contrast Score
// ============================================================================

float TextPlacementAnalyzer::computeContrastScore(const Spectra6Histogram& histogram,
                                                   uint8_t textColor, 
                                                   uint8_t outlineColor)
{
    if (histogram.total == 0) return 0.5f;
    
    // Score based on how different the background is from text/outline colors
    // 
    // For white text with black outline (typical):
    // - Worst: white background (text invisible) or black background (outline invisible)
    // - Best: colored backgrounds (red, yellow, blue, green)
    // - Medium: mixed backgrounds
    
    float textColorPct = histogram.percentage(textColor);
    float outlineColorPct = histogram.percentage(outlineColor);
    
    // Penalize backgrounds that match text or outline color
    // Text matching is worse than outline matching (outline is thinner)
    float penalty = textColorPct * 1.0f + outlineColorPct * 0.5f;
    
    // Score starts at 1.0 and decreases with penalty
    float score = 1.0f - penalty;
    
    // Bonus for high-contrast solid colors
    // If a single color dominates (>70%), give bonus if it contrasts well
    uint8_t dominant = histogram.dominantColor();
    float dominantPct = histogram.percentage(dominant);
    
    if (dominantPct > 0.7f) {
        // Check if dominant color contrasts with text
        bool goodContrast = (dominant != textColor && dominant != outlineColor);
        if (goodContrast) {
            // Give bonus for uniform contrasting background
            score += 0.2f * (dominantPct - 0.7f) / 0.3f;
        }
    }
    
    // Clamp to 0-1
    if (score < 0.0f) score = 0.0f;
    if (score > 1.0f) score = 1.0f;
    
    return score;
}

// ============================================================================
// Utility: Generate Standard Candidates
// ============================================================================

int TextPlacementAnalyzer::generateStandardCandidates(
    EL133UF1* display, int16_t textWidth, int16_t textHeight,
    int16_t margin, TextPlacementRegion* candidates, bool includeCorners)
{
    if (!display || !candidates) return 0;
    
    int16_t W = display->width();
    int16_t H = display->height();
    int16_t cx = W / 2;
    int16_t cy = H / 2;
    
    int count = 0;
    
    // Center (most common choice)
    candidates[count++] = {cx, cy, textWidth, textHeight, 0.0f};
    
    // Top center
    candidates[count++] = {cx, margin + textHeight/2, textWidth, textHeight, 0.0f};
    
    // Bottom center
    candidates[count++] = {cx, H - margin - textHeight/2, textWidth, textHeight, 0.0f};
    
    // Left center
    candidates[count++] = {margin + textWidth/2, cy, textWidth, textHeight, 0.0f};
    
    // Right center
    candidates[count++] = {W - margin - textWidth/2, cy, textWidth, textHeight, 0.0f};
    
    if (includeCorners) {
        // Top-left
        candidates[count++] = {margin + textWidth/2, margin + textHeight/2, 
                               textWidth, textHeight, 0.0f};
        // Top-right
        candidates[count++] = {W - margin - textWidth/2, margin + textHeight/2, 
                               textWidth, textHeight, 0.0f};
        // Bottom-left
        candidates[count++] = {margin + textWidth/2, H - margin - textHeight/2, 
                               textWidth, textHeight, 0.0f};
        // Bottom-right
        candidates[count++] = {W - margin - textWidth/2, H - margin - textHeight/2, 
                               textWidth, textHeight, 0.0f};
    }
    
    return count;
}

// ============================================================================
// ESP32-P4 Parallel Scoring
// ============================================================================

#if defined(SOC_PPA_SUPPORTED) && SOC_PPA_SUPPORTED

// Task parameters for parallel scoring
struct ParallelScoreParams {
    TextPlacementAnalyzer* analyzer;
    EL133UF1* display;
    TextPlacementRegion* regions;
    int startIdx;
    int endIdx;
    uint8_t textColor;
    uint8_t outlineColor;
    SemaphoreHandle_t doneSemaphore;
};

static void parallelScoreTask(void* param) {
    ParallelScoreParams* p = (ParallelScoreParams*)param;
    
    for (int i = p->startIdx; i < p->endIdx; i++) {
        // Skip candidates marked as invalid (in keepout zone)
        if (p->regions[i].score < 0.0f) continue;
        
        int16_t rx = p->regions[i].drawX();
        int16_t ry = p->regions[i].drawY();
        p->regions[i].score = p->analyzer->scoreRegion(
            p->display, rx, ry,
            p->regions[i].width, p->regions[i].height,
            p->textColor, p->outlineColor);
    }
    
    // Signal completion
    xSemaphoreGive(p->doneSemaphore);
    vTaskDelete(NULL);
}

void TextPlacementAnalyzer::scoreRegionsParallel(
    EL133UF1* display,
    TextPlacementRegion* regions, int numRegions,
    uint8_t textColor, uint8_t outlineColor)
{
    if (numRegions <= 2) {
        // Not worth parallelizing
        for (int i = 0; i < numRegions; i++) {
            int16_t rx = regions[i].drawX();
            int16_t ry = regions[i].drawY();
            regions[i].score = scoreRegion(display, rx, ry,
                                           regions[i].width, regions[i].height,
                                           textColor, outlineColor);
        }
        return;
    }
    
    // Create semaphore for synchronization
    SemaphoreHandle_t doneSem = xSemaphoreCreateBinary();
    if (!doneSem) {
        // Fallback to sequential
        for (int i = 0; i < numRegions; i++) {
            int16_t rx = regions[i].drawX();
            int16_t ry = regions[i].drawY();
            regions[i].score = scoreRegion(display, rx, ry,
                                           regions[i].width, regions[i].height,
                                           textColor, outlineColor);
        }
        return;
    }
    
    // Split work: first half on core 1, second half on current core
    int mid = numRegions / 2;
    
    ParallelScoreParams core1Params = {
        this, display, regions,
        mid, numRegions,  // Second half
        textColor, outlineColor,
        doneSem
    };
    
    // Create task on core 1 for second half
    TaskHandle_t taskHandle = NULL;
    BaseType_t created = xTaskCreatePinnedToCore(
        parallelScoreTask,
        "score_c1",
        4096,
        &core1Params,
        5,  // Priority
        &taskHandle,
        1   // Core 1
    );
    
    if (created != pdPASS) {
        // Fallback to sequential if task creation failed
        vSemaphoreDelete(doneSem);
        for (int i = 0; i < numRegions; i++) {
            int16_t rx = regions[i].drawX();
            int16_t ry = regions[i].drawY();
            regions[i].score = scoreRegion(display, rx, ry,
                                           regions[i].width, regions[i].height,
                                           textColor, outlineColor);
        }
        return;
    }
    
    // Process first half on current core (0)
    for (int i = 0; i < mid; i++) {
        int16_t rx = regions[i].drawX();
        int16_t ry = regions[i].drawY();
        regions[i].score = scoreRegion(display, rx, ry,
                                       regions[i].width, regions[i].height,
                                       textColor, outlineColor);
    }
    
    // Wait for core 1 to finish (with timeout)
    if (xSemaphoreTake(doneSem, pdMS_TO_TICKS(1000)) != pdTRUE) {
        Serial.println("[TextPlacement] Warning: Core 1 task timeout");
    }
    
    vSemaphoreDelete(doneSem);
}

#endif // SOC_PPA_SUPPORTED

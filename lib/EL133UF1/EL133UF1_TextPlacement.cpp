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
      _keepout(),          // No keepout by default
      _numExclusionZones(0),
      _keepOutMap()        // No keep-out map by default
{
    initLUT();
}

// ============================================================================
// Exclusion Zone Management
// ============================================================================

bool TextPlacementAnalyzer::addExclusionZone(const ExclusionZone& zone) {
    if (_numExclusionZones >= MAX_EXCLUSION_ZONES) {
        Serial.println("[TextPlacement] Warning: Max exclusion zones reached!");
        return false;
    }
    _exclusionZones[_numExclusionZones++] = zone;
    Serial.printf("[TextPlacement] Added exclusion zone %d: center=(%d,%d) size=%dx%d pad=%d\n",
                  _numExclusionZones, zone.x, zone.y, zone.width, zone.height, zone.padding);
    return true;
}

bool TextPlacementAnalyzer::addExclusionZone(const TextPlacementRegion& region, int16_t padding) {
    return addExclusionZone(ExclusionZone(region, padding));
}

void TextPlacementAnalyzer::clearExclusionZones() {
    _numExclusionZones = 0;
    Serial.println("[TextPlacement] Cleared all exclusion zones");
}

bool TextPlacementAnalyzer::overlapsExclusionZone(int16_t x, int16_t y, int16_t w, int16_t h) const {
    for (int i = 0; i < _numExclusionZones; i++) {
        if (_exclusionZones[i].overlaps(x, y, w, h)) {
            return true;
        }
    }
    return false;
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
    
    // Check against exclusion zones
    if (overlapsExclusionZone(x, y, w, h)) return false;
    
    return true;
}

// ============================================================================
// Keep-Out Map Management
// ============================================================================

// RP2350/SdFat version
#if !defined(DISABLE_SDIO_TEST) && (defined(ARDUINO_ARCH_RP2040) || defined(PICO_RP2350) || defined(TARGET_RP2350))
bool TextPlacementAnalyzer::loadKeepOutMap(FsFile& file) {
    Serial.println("[TextPlacement] Loading keep-out map from SD card...");
    
    // Clear any existing map
    clearKeepOutMap();
    
    // Read header (16 bytes)
    struct __attribute__((packed)) MapHeader {
        char magic[5];      // "KOMAP"
        uint8_t version;
        uint16_t width;
        uint16_t height;
        uint8_t reserved[6];
    } header;
    
    if (file.read(&header, sizeof(header)) != sizeof(header)) {
        Serial.println("[TextPlacement] ERROR: Failed to read map header");
        return false;
    }
    
    // Verify magic
    if (memcmp(header.magic, "KOMAP", 5) != 0) {
        Serial.println("[TextPlacement] ERROR: Invalid map file (bad magic)");
        return false;
    }
    
    // Check version
    if (header.version != 1) {
        Serial.printf("[TextPlacement] ERROR: Unsupported map version: %d\n", header.version);
        return false;
    }
    
    Serial.printf("[TextPlacement] Map dimensions: %dx%d\n", header.width, header.height);
    
    // Calculate bitmap size
    uint32_t bitmapSize = ((uint32_t)header.width * header.height + 7) / 8;
    Serial.printf("[TextPlacement] Bitmap size: %lu bytes (%.1f KB)\n", 
                  bitmapSize, bitmapSize / 1024.0f);
    
    // Allocate bitmap
    uint8_t* bitmap = (uint8_t*)malloc(bitmapSize);
    if (!bitmap) {
        Serial.println("[TextPlacement] ERROR: Failed to allocate bitmap memory");
        return false;
    }
    
    // Read bitmap data
    size_t bytesRead = file.read(bitmap, bitmapSize);
    if (bytesRead != bitmapSize) {
        Serial.printf("[TextPlacement] ERROR: Failed to read bitmap (got %zu of %lu bytes)\n",
                      bytesRead, bitmapSize);
        free(bitmap);
        return false;
    }
    
    // Store in keep-out map structure
    _keepOutMap.width = header.width;
    _keepOutMap.height = header.height;
    _keepOutMap.bitmap = bitmap;
    
    // Calculate coverage statistics
    uint32_t keepOutPixels = 0;
    for (uint32_t i = 0; i < bitmapSize; i++) {
        // Count set bits in each byte
        uint8_t byte = bitmap[i];
        while (byte) {
            keepOutPixels += (byte & 1);
            byte >>= 1;
        }
    }
    
    float coverage = (float)keepOutPixels / (header.width * header.height) * 100.0f;
    Serial.printf("[TextPlacement] Keep-out coverage: %.1f%% (%lu pixels)\n", 
                  coverage, keepOutPixels);
    
    Serial.println("[TextPlacement] Keep-out map loaded successfully!");
    return true;
}
#endif

void TextPlacementAnalyzer::clearKeepOutMap() {
    if (_keepOutMap.bitmap) {
        free(_keepOutMap.bitmap);
        _keepOutMap.bitmap = nullptr;
    }
    _keepOutMap.width = 0;
    _keepOutMap.height = 0;
}

bool TextPlacementAnalyzer::loadKeepOutMapFromBuffer(const uint8_t* data, size_t dataSize) {
    Serial.println("[TextPlacement] Loading keep-out map from buffer...");
    
    // Clear any existing map
    clearKeepOutMap();
    
    // Check minimum size (header = 16 bytes)
    if (dataSize < 16) {
        Serial.println("[TextPlacement] ERROR: Buffer too small for header");
        return false;
    }
    
    // Read header
    struct __attribute__((packed)) MapHeader {
        char magic[5];
        uint8_t version;
        uint16_t width;
        uint16_t height;
        uint8_t reserved[6];
    };
    
    const MapHeader* header = (const MapHeader*)data;
    
    // Verify magic
    if (memcmp(header->magic, "KOMAP", 5) != 0) {
        Serial.println("[TextPlacement] ERROR: Invalid map file (bad magic)");
        return false;
    }
    
    // Check version
    if (header->version != 1) {
        Serial.printf("[TextPlacement] ERROR: Unsupported map version: %d\n", header->version);
        return false;
    }
    
    Serial.printf("[TextPlacement] Map dimensions: %dx%d\n", header->width, header->height);
    
    // Calculate bitmap size
    uint32_t bitmapSize = ((uint32_t)header->width * header->height + 7) / 8;
    Serial.printf("[TextPlacement] Bitmap size: %lu bytes (%.1f KB)\n", 
                  bitmapSize, bitmapSize / 1024.0f);
    
    // Check buffer has enough data
    if (dataSize < 16 + bitmapSize) {
        Serial.printf("[TextPlacement] ERROR: Buffer too small (need %lu, have %zu)\n",
                      16 + bitmapSize, dataSize);
        return false;
    }
    
    // Allocate bitmap
    uint8_t* bitmap = (uint8_t*)malloc(bitmapSize);
    if (!bitmap) {
        Serial.println("[TextPlacement] ERROR: Failed to allocate bitmap memory");
        return false;
    }
    
    // Copy bitmap data
    memcpy(bitmap, data + 16, bitmapSize);
    
    // Store in keep-out map structure
    _keepOutMap.width = header->width;
    _keepOutMap.height = header->height;
    _keepOutMap.bitmap = bitmap;
    
    // Calculate coverage statistics
    uint32_t keepOutPixels = 0;
    for (uint32_t i = 0; i < bitmapSize; i++) {
        // Count set bits in each byte
        uint8_t byte = bitmap[i];
        while (byte) {
            keepOutPixels += (byte & 1);
            byte >>= 1;
        }
    }
    
    float coverage = (float)keepOutPixels / (header->width * header->height) * 100.0f;
    Serial.printf("[TextPlacement] Keep-out coverage: %.1f%% (%lu pixels)\n", 
                  coverage, keepOutPixels);
    
    Serial.println("[TextPlacement] Keep-out map loaded successfully!");
    return true;
}

void TextPlacementAnalyzer::debugDrawKeepOutAreas(EL133UF1* display, uint8_t color) {
    if (!_keepOutMap.bitmap || !display) {
        Serial.println("[KeepOut Debug] No map loaded or invalid display");
        return;
    }
    
    Serial.println("[KeepOut Debug] Drawing keep-out area boundaries...");
    uint32_t pixelsDrawn = 0;
    
    // Draw borders around keep-out areas
    // We'll draw a pixel in the debug color if it's a keep-out pixel 
    // adjacent to a non-keep-out pixel (edge detection)
    for (uint16_t y = 0; y < _keepOutMap.height; y++) {
        for (uint16_t x = 0; x < _keepOutMap.width; x++) {
            if (_keepOutMap.isKeepOut(x, y)) {
                // Check if this is an edge pixel (has at least one non-keep-out neighbor)
                bool isEdge = false;
                
                // Check 4-connected neighbors
                if (x > 0 && !_keepOutMap.isKeepOut(x-1, y)) isEdge = true;
                if (x < _keepOutMap.width-1 && !_keepOutMap.isKeepOut(x+1, y)) isEdge = true;
                if (y > 0 && !_keepOutMap.isKeepOut(x, y-1)) isEdge = true;
                if (y < _keepOutMap.height-1 && !_keepOutMap.isKeepOut(x, y+1)) isEdge = true;
                
                if (isEdge) {
                    display->setPixel(x, y, color);
                    pixelsDrawn++;
                }
            }
        }
    }
    
    Serial.printf("[KeepOut Debug] Drew %lu edge pixels\n", (unsigned long)pixelsDrawn);
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

// ============================================================================
// Grid-based scanning methods
// ============================================================================

TextPlacementRegion TextPlacementAnalyzer::scanForBestPosition(
    EL133UF1* display, EL133UF1_TTF* ttf,
    const char* text, float fontSize,
    uint8_t textColor, uint8_t outlineColor,
    int16_t gridStepX, int16_t gridStepY)
{
    if (!display || !ttf || !text) {
        return TextPlacementRegion{0, 0, 0, 0, 0.0f};
    }
    
    // Get text dimensions
    int16_t blockWidth = ttf->getTextWidth(text, fontSize);
    int16_t blockHeight = ttf->getTextHeight(fontSize);
    
    return scanForBestPosition(display, blockWidth, blockHeight, 
                               textColor, outlineColor, gridStepX, gridStepY);
}

TextPlacementRegion TextPlacementAnalyzer::scanForBestPosition(
    EL133UF1* display,
    int16_t blockWidth, int16_t blockHeight,
    uint8_t textColor, uint8_t outlineColor,
    int16_t gridStepX, int16_t gridStepY)
{
    if (!display || blockWidth <= 0 || blockHeight <= 0) {
        return TextPlacementRegion{0, 0, 0, 0, 0.0f};
    }
    
    int16_t dispW = display->width();
    int16_t dispH = display->height();
    
    // Calculate safe area bounds (inside keepout margins)
    int16_t safeLeft = _keepout.left + blockWidth / 2;
    int16_t safeRight = dispW - _keepout.right - blockWidth / 2;
    int16_t safeTop = _keepout.top + blockHeight / 2;
    int16_t safeBottom = dispH - _keepout.bottom - blockHeight / 2;
    
    if (safeLeft >= safeRight || safeTop >= safeBottom) {
        Serial.println("[TextPlacement] Warning: Safe area too small for text!");
        return TextPlacementRegion{(int16_t)(dispW/2), (int16_t)(dispH/2), 
                                   blockWidth, blockHeight, 0.0f};
    }
    
    // Calculate grid steps if not provided
    // Aim for roughly 8-12 positions per axis for good coverage without being too slow
    if (gridStepX <= 0) {
        int16_t safeWidth = safeRight - safeLeft;
        gridStepX = max((int16_t)50, (int16_t)(safeWidth / 10));
    }
    if (gridStepY <= 0) {
        int16_t safeHeight = safeBottom - safeTop;
        gridStepY = max((int16_t)50, (int16_t)(safeHeight / 8));
    }
    
    // Count grid positions
    int numX = (safeRight - safeLeft) / gridStepX + 1;
    int numY = (safeBottom - safeTop) / gridStepY + 1;
    int maxCandidates = numX * numY;
    
    Serial.printf("[TextPlacement] Scanning grid: %dx%d (%d positions), step=%dx%d\n",
                  numX, numY, maxCandidates, gridStepX, gridStepY);
    
    // Generate grid candidates, skipping those that overlap exclusion zones or keep-out areas
    TextPlacementRegion* candidates = new TextPlacementRegion[maxCandidates];
    int numCandidates = 0;
    int skippedByExclusion = 0;
    int skippedByKeepOut = 0;
    
    for (int16_t cy = safeTop; cy <= safeBottom && numCandidates < maxCandidates; cy += gridStepY) {
        for (int16_t cx = safeLeft; cx <= safeRight && numCandidates < maxCandidates; cx += gridStepX) {
            // Check if this position overlaps any exclusion zone
            if (overlapsExclusionZone(cx, cy, blockWidth, blockHeight)) {
                skippedByExclusion++;
                continue;  // Skip this position entirely
            }
            
            // Check if this position overlaps keep-out areas (> 10%)
            // This is an optimization to avoid scoring obviously bad positions
            if (_keepOutMap.bitmap) {
                int16_t rx = cx - blockWidth / 2;
                int16_t ry = cy - blockHeight / 2;
                float coverage = _keepOutMap.getKeepOutCoverage(rx, ry, blockWidth, blockHeight);
                if (coverage > 0.10f) {  // Reject >10% overlap
                    skippedByKeepOut++;
                    continue;  // Skip overlapped positions
                }
            }
            
            candidates[numCandidates++] = {cx, cy, blockWidth, blockHeight, 0.0f};
        }
    }
    
    if (skippedByExclusion > 0) {
        Serial.printf("[TextPlacement] Skipped %d positions due to exclusion zones\n", skippedByExclusion);
    }
    if (skippedByKeepOut > 0) {
        Serial.printf("[TextPlacement] Skipped %d positions due to keep-out map (>50%% overlap)\n", skippedByKeepOut);
    }
    
    if (numCandidates == 0) {
        Serial.println("[TextPlacement] Warning: All positions excluded! Using center.");
        delete[] candidates;
        return TextPlacementRegion{(int16_t)(dispW/2), (int16_t)(dispH/2), 
                                   blockWidth, blockHeight, 0.0f};
    }
    
    // Score all candidates (uses parallel scoring on ESP32-P4 if enabled)
#if defined(SOC_PPA_SUPPORTED) && SOC_PPA_SUPPORTED
    if (_useParallel && numCandidates > 2) {
        scoreRegionsParallel(display, candidates, numCandidates, textColor, outlineColor);
    } else
#endif
    {
        for (int i = 0; i < numCandidates; i++) {
            int16_t rx = candidates[i].drawX();
            int16_t ry = candidates[i].drawY();
            candidates[i].score = scoreRegion(display, rx, ry, blockWidth, blockHeight,
                                              textColor, outlineColor);
        }
    }
    
    // Find best
    int bestIdx = 0;
    float bestScore = candidates[0].score;
    for (int i = 1; i < numCandidates; i++) {
        if (candidates[i].score > bestScore) {
            bestScore = candidates[i].score;
            bestIdx = i;
        }
    }
    
    TextPlacementRegion result = candidates[bestIdx];
    delete[] candidates;
    
    Serial.printf("[TextPlacement] Best position: (%d,%d) score=%.3f\n",
                  result.x, result.y, result.score);
    
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
    
    // Check keep-out map FIRST (if available)
    // If region overlaps keep-out areas AT ALL, apply severe penalty or reject
    float keepOutCoverage = 0.0f;
    if (_keepOutMap.bitmap) {
        keepOutCoverage = _keepOutMap.getKeepOutCoverage(x, y, w, h);
        
        // STRICT POLICY: ANY overlap with keep-out areas is very bad
        // We want text to NEVER overlap detected objects
        // > 10% overlap: complete rejection
        // 5-10% overlap: severe penalty (score * 0.1)
        // 1-5% overlap: heavy penalty (score * 0.3)
        // < 1% overlap: moderate penalty (score * 0.7)
        if (keepOutCoverage > 0.10f) {
            // Complete rejection - unacceptable overlap with objects
            metrics.overallScore = 0.0f;
            return metrics;
        } else if (keepOutCoverage > 0.0f) {
            // Store coverage for later penalty application
            metrics.variance = keepOutCoverage;
        }
    }
    
    // Get histogram
    getColorHistogram(display, x, y, w, h, metrics.histogram);
    
    // Get variance (but don't overwrite if we stored keepOutCoverage there)
    float originalVariance = metrics.variance;
    metrics.variance = computeVariance(display, x, y, w, h);
    // keepOutCoverage already declared earlier, just restore it
    if (originalVariance != 0.0f) {
        keepOutCoverage = originalVariance;  // Restore from where we stored it
    }
    
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
    
    // Apply keep-out penalty if there was partial overlap
    if (_keepOutMap.bitmap && keepOutCoverage > 0.0f) {
        // VERY aggressive penalty for any keep-out overlap
        float penalty;
        if (keepOutCoverage > 0.05f) {
            penalty = 0.1f;  // 5-10% overlap: keep only 10% of score
        } else if (keepOutCoverage > 0.01f) {
            penalty = 0.3f;  // 1-5% overlap: keep only 30% of score
        } else {
            penalty = 0.7f;  // <1% overlap: keep 70% of score
        }
        metrics.overallScore *= penalty;
    }
    
    // Clamp to 0-1
    if (metrics.overallScore < 0.0f) metrics.overallScore = 0.0f;
    if (metrics.overallScore > 1.0f) metrics.overallScore = 1.0f;
    
    // Debug: Log regions with any keep-out overlap
    if (_keepOutMap.bitmap && keepOutCoverage > 0.0f) {
        Serial.printf("[KeepOut] Region (%d,%d %dx%d) has %.1f%% keep-out coverage, score=%.3f\n",
                      x, y, w, h, keepOutCoverage * 100.0f, metrics.overallScore);
    }
    
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

// ============================================================================
// Text Wrapping and Multi-line Layout
// ============================================================================

int16_t TextPlacementAnalyzer::wrapText(EL133UF1_TTF* ttf, const char* text, 
                                         float fontSize, int16_t targetWidth,
                                         char* output, size_t outputSize,
                                         int* numLines)
{
    if (!ttf || !text || !output || outputSize < 2) {
        if (numLines) *numLines = 0;
        return 0;
    }
    
    // If no target width, just copy text as-is
    if (targetWidth <= 0) {
        strncpy(output, text, outputSize - 1);
        output[outputSize - 1] = '\0';
        if (numLines) *numLines = 1;
        return ttf->getTextWidth(text, fontSize);
    }
    
    // Split into words
    const int MAX_WORDS = 64;
    const char* words[MAX_WORDS];
    int wordLengths[MAX_WORDS];
    int wordCount = 0;
    
    const char* p = text;
    while (*p && wordCount < MAX_WORDS) {
        // Skip leading spaces
        while (*p == ' ') p++;
        if (!*p) break;
        
        // Find word end
        words[wordCount] = p;
        while (*p && *p != ' ') p++;
        wordLengths[wordCount] = p - words[wordCount];
        wordCount++;
    }
    
    if (wordCount == 0) {
        output[0] = '\0';
        if (numLines) *numLines = 0;
        return 0;
    }
    
    // Build wrapped text
    char* out = output;
    char* outEnd = output + outputSize - 1;
    int lines = 1;
    int16_t maxLineWidth = 0;
    int16_t currentLineWidth = 0;
    char lineBuffer[256] = {0};
    int lineBufferLen = 0;
    
    for (int i = 0; i < wordCount; i++) {
        // Build potential line with this word
        char testLine[256];
        if (lineBufferLen > 0) {
            snprintf(testLine, sizeof(testLine), "%s %.*s", lineBuffer, wordLengths[i], words[i]);
        } else {
            snprintf(testLine, sizeof(testLine), "%.*s", wordLengths[i], words[i]);
        }
        
        int16_t testWidth = ttf->getTextWidth(testLine, fontSize);
        
        if (testWidth <= targetWidth || lineBufferLen == 0) {
            // Word fits, add to current line
            strcpy(lineBuffer, testLine);
            lineBufferLen = strlen(lineBuffer);
            currentLineWidth = testWidth;
        } else {
            // Word doesn't fit, start new line
            // First, output current line
            int len = strlen(lineBuffer);
            if (out + len < outEnd) {
                memcpy(out, lineBuffer, len);
                out += len;
            }
            if (currentLineWidth > maxLineWidth) maxLineWidth = currentLineWidth;
            
            // Add newline
            if (out < outEnd) *out++ = '\n';
            lines++;
            
            // Start new line with this word
            snprintf(lineBuffer, sizeof(lineBuffer), "%.*s", wordLengths[i], words[i]);
            lineBufferLen = strlen(lineBuffer);
            currentLineWidth = ttf->getTextWidth(lineBuffer, fontSize);
        }
    }
    
    // Output final line
    if (lineBufferLen > 0 && out < outEnd) {
        int len = strlen(lineBuffer);
        if (out + len < outEnd) {
            memcpy(out, lineBuffer, len);
            out += len;
        }
        if (currentLineWidth > maxLineWidth) maxLineWidth = currentLineWidth;
    }
    
    *out = '\0';
    
    if (numLines) *numLines = lines;
    return maxLineWidth;
}

TextPlacementAnalyzer::WrappedTextResult TextPlacementAnalyzer::findBestWrappedPosition(
    EL133UF1* display, EL133UF1_TTF* ttf,
    const char* text, float fontSize,
    const TextPlacementRegion* candidates, int numCandidates,
    uint8_t textColor, uint8_t outlineColor,
    int maxLines, int minWordsPerLine)
{
    WrappedTextResult bestResult;
    memset(&bestResult, 0, sizeof(bestResult));
    bestResult.position.score = -1.0f;
    
    if (!display || !ttf || !text || !candidates || numCandidates <= 0) {
        return bestResult;
    }
    
    // Count words in text
    int wordCount = 0;
    const char* p = text;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        wordCount++;
        while (*p && *p != ' ') p++;
    }
    
    // Get line height for multi-line calculations
    int16_t lineHeight = ttf->getTextHeight(fontSize);
    int16_t lineGap = lineHeight / 4;  // 25% of line height as gap
    
    // Get full text width for calculating target widths
    int16_t fullWidth = ttf->getTextWidth(text, fontSize);
    
    // Try different numbers of lines
    int maxPossibleLines = wordCount / minWordsPerLine;
    if (maxPossibleLines < 1) maxPossibleLines = 1;
    if (maxPossibleLines > maxLines) maxPossibleLines = maxLines;
    
    for (int targetLines = 1; targetLines <= maxPossibleLines; targetLines++) {
        char wrappedText[512];
        int actualLines = 0;
        
        // Calculate target width for this number of lines
        // Aim for roughly equal line lengths
        int16_t targetWidth = (targetLines == 1) ? 0 : (fullWidth / targetLines) + 50;
        
        int16_t wrappedWidth = wrapText(ttf, text, fontSize, targetWidth, 
                                        wrappedText, sizeof(wrappedText), &actualLines);
        
        // Calculate block dimensions
        int16_t blockWidth = wrappedWidth;
        int16_t blockHeight = actualLines * lineHeight + (actualLines - 1) * lineGap;
        
        // Create modified candidates with this block size
        TextPlacementRegion* modifiedCandidates = new TextPlacementRegion[numCandidates];
        for (int i = 0; i < numCandidates; i++) {
            modifiedCandidates[i] = candidates[i];
            modifiedCandidates[i].width = blockWidth;
            modifiedCandidates[i].height = blockHeight;
        }
        
        // Find best position for this wrapping
        TextPlacementRegion bestPos = findBestPosition(
            display, ttf, wrappedText, fontSize,
            modifiedCandidates, numCandidates,
            textColor, outlineColor);
        
        delete[] modifiedCandidates;
        
        // Check if this is better than our current best
        if (bestPos.score > bestResult.position.score) {
            strncpy(bestResult.wrappedText, wrappedText, sizeof(bestResult.wrappedText) - 1);
            bestResult.wrappedText[sizeof(bestResult.wrappedText) - 1] = '\0';
            bestResult.width = blockWidth;
            bestResult.height = blockHeight;
            bestResult.numLines = actualLines;
            bestResult.position = bestPos;
        }
    }
    
    // If no valid position found, return single-line version at first candidate
    if (bestResult.position.score < 0) {
        strncpy(bestResult.wrappedText, text, sizeof(bestResult.wrappedText) - 1);
        bestResult.width = fullWidth;
        bestResult.height = lineHeight;
        bestResult.numLines = 1;
        bestResult.position = candidates[0];
        bestResult.position.score = 0.0f;
    }
    
    return bestResult;
}

// ============================================================================
// Quote Layout with Author
// ============================================================================

TextPlacementAnalyzer::QuoteLayoutResult TextPlacementAnalyzer::findBestQuotePosition(
    EL133UF1* display, EL133UF1_TTF* ttf,
    const Quote& quote, float quoteFontSize, float authorFontSize,
    const TextPlacementRegion* candidates, int numCandidates,
    uint8_t textColor, uint8_t outlineColor,
    int maxLines, int minWordsPerLine)
{
    QuoteLayoutResult bestResult;
    memset(&bestResult, 0, sizeof(bestResult));
    bestResult.position.score = -1.0f;
    
    if (!display || !ttf || !quote.text || !candidates || numCandidates <= 0) {
        return bestResult;
    }
    
    // Measure author text
    char authorText[128];
    snprintf(authorText, sizeof(authorText), "— %s", quote.author ? quote.author : "Unknown");
    int16_t authorWidth = ttf->getTextWidth(authorText, authorFontSize);
    int16_t authorHeight = ttf->getTextHeight(authorFontSize);
    int16_t gapBeforeAuthor = authorHeight / 2;  // Gap between quote and author
    
    // Count words in quote
    int wordCount = 0;
    const char* p = quote.text;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        wordCount++;
        while (*p && *p != ' ') p++;
    }
    
    // Get line metrics for quote
    int16_t quoteLineHeight = ttf->getTextHeight(quoteFontSize);
    int16_t quoteLineGap = quoteLineHeight / 4;
    
    // Get full quote width for target calculations
    int16_t fullQuoteWidth = ttf->getTextWidth(quote.text, quoteFontSize);
    
    // Try different numbers of lines
    int maxPossibleLines = wordCount / minWordsPerLine;
    if (maxPossibleLines < 1) maxPossibleLines = 1;
    if (maxPossibleLines > maxLines) maxPossibleLines = maxLines;
    
    for (int targetLines = 1; targetLines <= maxPossibleLines; targetLines++) {
        char wrappedQuote[512];
        int actualLines = 0;
        
        // Calculate target width for this number of lines
        int16_t targetWidth = (targetLines == 1) ? 0 : (fullQuoteWidth / targetLines) + 50;
        
        int16_t quoteWidth = wrapText(ttf, quote.text, quoteFontSize, targetWidth,
                                       wrappedQuote, sizeof(wrappedQuote), &actualLines);
        
        // Calculate quote block dimensions
        int16_t quoteHeight = actualLines * quoteLineHeight + (actualLines - 1) * quoteLineGap;
        
        // Total block dimensions (quote + gap + author)
        int16_t totalWidth = max(quoteWidth, authorWidth);
        int16_t totalHeight = quoteHeight + gapBeforeAuthor + authorHeight;
        
        // Create modified candidates with this block size
        TextPlacementRegion* modifiedCandidates = new TextPlacementRegion[numCandidates];
        for (int i = 0; i < numCandidates; i++) {
            modifiedCandidates[i] = candidates[i];
            modifiedCandidates[i].width = totalWidth;
            modifiedCandidates[i].height = totalHeight;
        }
        
        // Find best position for this wrapping
        TextPlacementRegion bestPos = findBestPosition(
            display, ttf, wrappedQuote, quoteFontSize,
            modifiedCandidates, numCandidates,
            textColor, outlineColor);
        
        delete[] modifiedCandidates;
        
        // Check if this is better than our current best
        if (bestPos.score > bestResult.position.score) {
            strncpy(bestResult.wrappedQuote, wrappedQuote, sizeof(bestResult.wrappedQuote) - 1);
            bestResult.wrappedQuote[sizeof(bestResult.wrappedQuote) - 1] = '\0';
            bestResult.quoteWidth = quoteWidth;
            bestResult.quoteHeight = quoteHeight;
            bestResult.quoteLines = actualLines;
            bestResult.authorWidth = authorWidth;
            bestResult.authorHeight = authorHeight;
            bestResult.totalWidth = totalWidth;
            bestResult.totalHeight = totalHeight;
            bestResult.position = bestPos;
        }
    }
    
    // If no valid position found, use single-line at first candidate
    if (bestResult.position.score < 0) {
        strncpy(bestResult.wrappedQuote, quote.text, sizeof(bestResult.wrappedQuote) - 1);
        bestResult.quoteWidth = fullQuoteWidth;
        bestResult.quoteHeight = quoteLineHeight;
        bestResult.quoteLines = 1;
        bestResult.authorWidth = authorWidth;
        bestResult.authorHeight = authorHeight;
        bestResult.totalWidth = max(fullQuoteWidth, authorWidth);
        bestResult.totalHeight = quoteLineHeight + gapBeforeAuthor + authorHeight;
        bestResult.position = candidates[0];
        bestResult.position.score = 0.0f;
    }
    
    return bestResult;
}

TextPlacementAnalyzer::QuoteLayoutResult TextPlacementAnalyzer::scanForBestQuotePosition(
    EL133UF1* display, EL133UF1_TTF* ttf,
    const Quote& quote, float quoteFontSize, float authorFontSize,
    uint8_t textColor, uint8_t outlineColor,
    int maxLines, int minWordsPerLine)
{
    QuoteLayoutResult bestResult;
    memset(&bestResult, 0, sizeof(bestResult));
    bestResult.position.score = -1.0f;
    
    if (!display || !ttf || !quote.text) {
        return bestResult;
    }
    
    int16_t dispW = display->width();
    int16_t dispH = display->height();
    
    // Measure author text
    char authorText[128];
    snprintf(authorText, sizeof(authorText), "— %s", quote.author ? quote.author : "Unknown");
    int16_t authorWidth = ttf->getTextWidth(authorText, authorFontSize);
    int16_t authorHeight = ttf->getTextHeight(authorFontSize);
    int16_t gapBeforeAuthor = authorHeight / 2;
    
    // Count words in quote
    int wordCount = 0;
    const char* p = quote.text;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        wordCount++;
        while (*p && *p != ' ') p++;
    }
    
    // Get line metrics for quote
    int16_t quoteLineHeight = ttf->getTextHeight(quoteFontSize);
    int16_t quoteLineGap = quoteLineHeight / 4;
    
    // Get full quote width for target calculations
    int16_t fullQuoteWidth = ttf->getTextWidth(quote.text, quoteFontSize);
    
    // Try different numbers of lines
    int maxPossibleLines = wordCount / minWordsPerLine;
    if (maxPossibleLines < 1) maxPossibleLines = 1;
    if (maxPossibleLines > maxLines) maxPossibleLines = maxLines;
    
    Serial.printf("[TextPlacement] Scanning for quote, trying %d line layouts\n", maxPossibleLines);
    
    for (int targetLines = 1; targetLines <= maxPossibleLines; targetLines++) {
        char wrappedQuote[512];
        int actualLines = 0;
        
        // Calculate target width for this number of lines
        int16_t targetWidth = (targetLines == 1) ? 0 : (fullQuoteWidth / targetLines) + 50;
        
        int16_t quoteWidth = wrapText(ttf, quote.text, quoteFontSize, targetWidth,
                                       wrappedQuote, sizeof(wrappedQuote), &actualLines);
        
        // Calculate quote block dimensions
        int16_t quoteHeight = actualLines * quoteLineHeight + (actualLines - 1) * quoteLineGap;
        
        // Total block dimensions (quote + gap + author)
        int16_t totalWidth = max(quoteWidth, authorWidth);
        int16_t totalHeight = quoteHeight + gapBeforeAuthor + authorHeight;
        
        // Use grid scanning to find best position for this layout
        TextPlacementRegion bestPos = scanForBestPosition(display, totalWidth, totalHeight,
                                                          textColor, outlineColor);
        
        // Check if this is better than our current best
        if (bestPos.score > bestResult.position.score) {
            strncpy(bestResult.wrappedQuote, wrappedQuote, sizeof(bestResult.wrappedQuote) - 1);
            bestResult.wrappedQuote[sizeof(bestResult.wrappedQuote) - 1] = '\0';
            bestResult.quoteWidth = quoteWidth;
            bestResult.quoteHeight = quoteHeight;
            bestResult.quoteLines = actualLines;
            bestResult.authorWidth = authorWidth;
            bestResult.authorHeight = authorHeight;
            bestResult.totalWidth = totalWidth;
            bestResult.totalHeight = totalHeight;
            bestResult.position = bestPos;
        }
    }
    
    // If no valid position found, use single-line at display center
    if (bestResult.position.score < 0) {
        strncpy(bestResult.wrappedQuote, quote.text, sizeof(bestResult.wrappedQuote) - 1);
        bestResult.quoteWidth = fullQuoteWidth;
        bestResult.quoteHeight = quoteLineHeight;
        bestResult.quoteLines = 1;
        bestResult.authorWidth = authorWidth;
        bestResult.authorHeight = authorHeight;
        bestResult.totalWidth = max(fullQuoteWidth, authorWidth);
        bestResult.totalHeight = quoteLineHeight + gapBeforeAuthor + authorHeight;
        bestResult.position = {(int16_t)(dispW/2), (int16_t)(dispH/2), 
                               bestResult.totalWidth, bestResult.totalHeight, 0.0f};
    }
    
    return bestResult;
}

void TextPlacementAnalyzer::drawQuote(EL133UF1_TTF* ttf, const QuoteLayoutResult& layout,
                                       const char* author, float quoteFontSize, float authorFontSize,
                                       uint8_t textColor, uint8_t outlineColor, int outlineWidth)
{
    if (!ttf) return;
    
    int16_t quoteLineHeight = ttf->getTextHeight(quoteFontSize);
    int16_t quoteLineGap = quoteLineHeight / 4;
    int16_t gapBeforeAuthor = ttf->getTextHeight(authorFontSize) / 2;
    
    // Calculate block edges
    // Block is centered at position.x, position.y
    int16_t blockTop = layout.position.y - layout.totalHeight / 2;
    int16_t blockLeft = layout.position.x - layout.totalWidth / 2;
    int16_t blockRight = layout.position.x + layout.totalWidth / 2;
    
    // Draw quote lines (left-aligned)
    if (layout.quoteLines == 1) {
        // Single line - left aligned
        int16_t quoteY = blockTop + quoteLineHeight / 2;
        ttf->drawTextAlignedOutlined(blockLeft, quoteY, layout.wrappedQuote, quoteFontSize,
                                     textColor, outlineColor, ALIGN_LEFT, ALIGN_MIDDLE, outlineWidth);
    } else {
        // Multi-line - draw each line left-aligned
        int16_t startY = blockTop + quoteLineHeight / 2;
        
        // Need to make a mutable copy for strtok-style parsing
        char quoteCopy[512];
        strncpy(quoteCopy, layout.wrappedQuote, sizeof(quoteCopy) - 1);
        quoteCopy[sizeof(quoteCopy) - 1] = '\0';
        
        char* line = quoteCopy;
        for (int i = 0; i < layout.quoteLines && line && *line; i++) {
            // Find end of this line
            char* nextLine = strchr(line, '\n');
            if (nextLine) {
                *nextLine = '\0';
            }
            
            // Draw this line left-aligned
            int16_t lineY = startY + i * (quoteLineHeight + quoteLineGap);
            ttf->drawTextAlignedOutlined(blockLeft, lineY, line, quoteFontSize,
                                         textColor, outlineColor, ALIGN_LEFT, ALIGN_MIDDLE, outlineWidth);
            
            // Move to next line
            if (nextLine) {
                line = nextLine + 1;
            } else {
                break;
            }
        }
    }
    
    // Draw author - right-aligned to the quote block's right edge
    // Position: below the quote, right-aligned
    int16_t authorY = blockTop + layout.quoteHeight + gapBeforeAuthor + layout.authorHeight / 2;
    
    char authorText[128];
    snprintf(authorText, sizeof(authorText), "— %s", author ? author : "Unknown");
    
    ttf->drawTextAlignedOutlined(blockRight, authorY, authorText, authorFontSize,
                                 textColor, outlineColor, ALIGN_RIGHT, ALIGN_MIDDLE, outlineWidth);
}

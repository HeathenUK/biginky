/**
 * @file EL133UF1_TextPlacement.h
 * @brief Intelligent text placement analysis for EL133UF1 display
 * 
 * Analyzes the framebuffer to find optimal positions for text overlay
 * based on background uniformity, contrast, and edge density.
 * 
 * Features:
 * - ARGB8888 optimized analysis for ESP32-P4 (uses existing PPA buffer)
 * - L8 fallback for RP2350 and other platforms
 * - SIMD-style optimizations using PIE on ESP32-P4
 * - Dual-core parallel scoring on ESP32-P4
 * - Multiple scoring metrics: histogram, variance, edge density
 * 
 * Usage:
 *   TextPlacementAnalyzer analyzer;
 *   
 *   // Define candidate positions
 *   TextPlacementRegion candidates[] = {
 *       {800, 550, 400, 100, 0},  // Center
 *       {800, 100, 400, 100, 0},  // Top
 *       {800, 1000, 400, 100, 0}, // Bottom
 *   };
 *   
 *   // Find best position
 *   auto best = analyzer.findBestPosition(&display, &ttf, "12:34",
 *                                         160.0f, candidates, 3,
 *                                         EL133UF1_WHITE, EL133UF1_BLACK);
 */

#ifndef EL133UF1_TEXTPLACEMENT_H
#define EL133UF1_TEXTPLACEMENT_H

#include <Arduino.h>
#include "EL133UF1.h"  // Must come first - defines EL133UF1_USE_ARGB8888
#include "EL133UF1_TTF.h"

// ESP32-P4 specific includes for parallel analysis
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#include "soc/soc_caps.h"
#if defined(SOC_PPA_SUPPORTED) && SOC_PPA_SUPPORTED
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#endif
#endif

/**
 * @brief Describes a candidate region for text placement
 */
struct TextPlacementRegion {
    int16_t x;          ///< X coordinate of region center (for alignment)
    int16_t y;          ///< Y coordinate of region center (for alignment)
    int16_t width;      ///< Width of text bounding box
    int16_t height;     ///< Height of text bounding box
    float score;        ///< Placement score (higher = better), set by analyzer
    
    // Computed top-left for actual drawing
    int16_t drawX() const { return x - width / 2; }
    int16_t drawY() const { return y - height / 2; }
};

/**
 * @brief Color histogram for Spectra 6 palette
 */
struct Spectra6Histogram {
    uint32_t black;     ///< EL133UF1_BLACK (0)
    uint32_t white;     ///< EL133UF1_WHITE (1)
    uint32_t yellow;    ///< EL133UF1_YELLOW (2)
    uint32_t red;       ///< EL133UF1_RED (3)
    uint32_t blue;      ///< EL133UF1_BLUE (5)
    uint32_t green;     ///< EL133UF1_GREEN (6)
    uint32_t total;     ///< Total pixel count
    
    // Access by Spectra color code
    uint32_t& operator[](uint8_t spectraCode);
    uint32_t operator[](uint8_t spectraCode) const;
    
    // Get dominant color
    uint8_t dominantColor() const;
    
    // Get percentage of a specific color
    float percentage(uint8_t spectraCode) const;
};

/**
 * @brief Detailed analysis metrics for a region
 */
struct RegionMetrics {
    Spectra6Histogram histogram;  ///< Color distribution
    float variance;               ///< Luminance variance (0=uniform, higher=varied)
    float edgeDensity;           ///< Edge pixel ratio (0=smooth, 1=all edges)
    float contrastScore;         ///< Contrast against text color (0-1)
    float uniformityScore;       ///< How uniform the region is (0-1)
    float overallScore;          ///< Combined weighted score (0-1)
};

/**
 * @brief Scoring weights for text placement
 */
struct ScoringWeights {
    float contrast;      ///< Weight for contrast score (default 0.5)
    float uniformity;    ///< Weight for uniformity score (default 0.3)
    float edgeAvoidance; ///< Weight for edge avoidance (default 0.2)
    
    ScoringWeights() : contrast(0.5f), uniformity(0.3f), edgeAvoidance(0.2f) {}
};

/**
 * @brief Keepout margins - areas where text should not be placed
 * 
 * Defines a rectangular "safe area" inset from the display edges.
 * Any candidate position that would place text outside this area
 * will be rejected (score = 0).
 */
struct KeepoutMargins {
    int16_t top;         ///< Pixels from top edge
    int16_t bottom;      ///< Pixels from bottom edge
    int16_t left;        ///< Pixels from left edge
    int16_t right;       ///< Pixels from right edge
    
    KeepoutMargins() : top(0), bottom(0), left(0), right(0) {}
    KeepoutMargins(int16_t all) : top(all), bottom(all), left(all), right(all) {}
    KeepoutMargins(int16_t tb, int16_t lr) : top(tb), bottom(tb), left(lr), right(lr) {}
    KeepoutMargins(int16_t t, int16_t b, int16_t l, int16_t r) : top(t), bottom(b), left(l), right(r) {}
};

/**
 * @brief Intelligent text placement analyzer
 * 
 * Analyzes framebuffer regions to find optimal text placement positions.
 * Optimized for ESP32-P4 with ARGB8888 buffer and PIE SIMD instructions.
 */
class TextPlacementAnalyzer {
public:
    TextPlacementAnalyzer();
    ~TextPlacementAnalyzer();
    
    /**
     * @brief Set scoring weights
     * @param weights Custom weights for scoring factors
     */
    void setWeights(const ScoringWeights& weights) { _weights = weights; }
    
    /**
     * @brief Get current scoring weights
     */
    const ScoringWeights& getWeights() const { return _weights; }
    
    /**
     * @brief Set keepout margins (areas where text cannot be placed)
     * 
     * Text will not be placed such that any part of it falls within
     * the keepout margins from the display edges.
     * 
     * @param margins Keepout margins in pixels
     */
    void setKeepout(const KeepoutMargins& margins) { _keepout = margins; }
    
    /**
     * @brief Set uniform keepout margin on all sides
     * @param margin Pixels from each edge
     */
    void setKeepout(int16_t margin) { _keepout = KeepoutMargins(margin); }
    
    /**
     * @brief Get current keepout margins
     */
    const KeepoutMargins& getKeepout() const { return _keepout; }
    
    /**
     * @brief Enable/disable parallel analysis on ESP32-P4
     * @param enable True to use both cores for analysis
     */
    void setParallelMode(bool enable) { _useParallel = enable; }
    
    /**
     * @brief Check if a region fits within the safe area (outside keepout)
     * 
     * @param displayWidth Display width in pixels
     * @param displayHeight Display height in pixels
     * @param x Region center X
     * @param y Region center Y
     * @param w Region width
     * @param h Region height
     * @return true if region is fully within safe area
     */
    bool isWithinSafeArea(int16_t displayWidth, int16_t displayHeight,
                          int16_t x, int16_t y, int16_t w, int16_t h) const;
    
    // ========================================================================
    // Main API
    // ========================================================================
    
    /**
     * @brief Find the best position from a set of candidates
     * 
     * Analyzes each candidate region and returns the one with the highest
     * score for text readability.
     * 
     * @param display Pointer to display instance
     * @param ttf Pointer to TTF renderer (for text metrics)
     * @param text Text string to be placed
     * @param fontSize Font size in pixels
     * @param candidates Array of candidate regions (x,y are center points)
     * @param numCandidates Number of candidates
     * @param textColor Primary text color (e.g., EL133UF1_WHITE)
     * @param outlineColor Outline color (e.g., EL133UF1_BLACK)
     * @return Best candidate with score filled in
     */
    TextPlacementRegion findBestPosition(
        EL133UF1* display, EL133UF1_TTF* ttf,
        const char* text, float fontSize,
        const TextPlacementRegion* candidates, int numCandidates,
        uint8_t textColor, uint8_t outlineColor);
    
    /**
     * @brief Score a single region for text placement
     * 
     * @param display Pointer to display instance
     * @param x Top-left X of region
     * @param y Top-left Y of region
     * @param w Width of region
     * @param h Height of region
     * @param textColor Primary text color
     * @param outlineColor Outline color
     * @return Score from 0.0 (worst) to 1.0 (best)
     */
    float scoreRegion(EL133UF1* display, int16_t x, int16_t y,
                      int16_t w, int16_t h,
                      uint8_t textColor, uint8_t outlineColor);
    
    /**
     * @brief Get detailed metrics for a region
     * 
     * Useful for debugging or custom scoring logic.
     */
    RegionMetrics analyzeRegion(EL133UF1* display, int16_t x, int16_t y,
                                int16_t w, int16_t h,
                                uint8_t textColor, uint8_t outlineColor);
    
    // ========================================================================
    // Low-level analysis functions
    // ========================================================================
    
    /**
     * @brief Compute color histogram of a region
     * 
     * @param display Pointer to display instance
     * @param x Top-left X
     * @param y Top-left Y
     * @param w Width
     * @param h Height
     * @param histogram Output histogram
     */
    void getColorHistogram(EL133UF1* display, int16_t x, int16_t y,
                           int16_t w, int16_t h, Spectra6Histogram& histogram);
    
    /**
     * @brief Compute luminance variance in a region
     * 
     * Lower variance = more uniform background = better for text.
     * 
     * @return Variance value (0 = perfectly uniform)
     */
    float computeVariance(EL133UF1* display, int16_t x, int16_t y,
                          int16_t w, int16_t h);
    
    /**
     * @brief Compute edge density using Sobel-like gradient
     * 
     * Higher density = more edges = text might cross image features.
     * 
     * @return Edge density from 0.0 (smooth) to 1.0 (all edges)
     */
    float computeEdgeDensity(EL133UF1* display, int16_t x, int16_t y,
                             int16_t w, int16_t h);
    
    /**
     * @brief Compute contrast score for text color against background
     * 
     * @param histogram Background color histogram
     * @param textColor Primary text color
     * @param outlineColor Outline color
     * @return Contrast score from 0.0 (poor) to 1.0 (excellent)
     */
    float computeContrastScore(const Spectra6Histogram& histogram,
                               uint8_t textColor, uint8_t outlineColor);
    
    // ========================================================================
    // Utility functions
    // ========================================================================
    
    /**
     * @brief Generate standard candidate positions for centered text
     * 
     * Creates candidates at: center, top-center, bottom-center,
     * and optionally corners.
     * 
     * @param display Display for dimensions
     * @param textWidth Width of text bounding box
     * @param textHeight Height of text bounding box
     * @param margin Margin from edges
     * @param candidates Output array (must hold at least 5 elements)
     * @param includeCorners If true, adds corner positions (needs 9 elements)
     * @return Number of candidates generated
     */
    static int generateStandardCandidates(
        EL133UF1* display, int16_t textWidth, int16_t textHeight,
        int16_t margin, TextPlacementRegion* candidates,
        bool includeCorners = false);

    // ========================================================================
    // Quote structure and multi-line text wrapping
    // ========================================================================
    
    /**
     * @brief A quote with its author
     */
    struct Quote {
        const char* text;          ///< The quote text (without author)
        const char* author;        ///< Author name (e.g., "Brene Brown")
        
        Quote() : text(nullptr), author(nullptr) {}
        Quote(const char* t, const char* a) : text(t), author(a) {}
    };
    
    /**
     * @brief Result of multi-line text layout optimization
     */
    struct WrappedTextResult {
        char wrappedText[512];     ///< Text with newlines inserted
        int16_t width;             ///< Width of wrapped text block
        int16_t height;            ///< Height of wrapped text block (quote only)
        int numLines;              ///< Number of lines in quote
        TextPlacementRegion position;  ///< Best position for this layout
    };
    
    /**
     * @brief Result of quote layout with author
     */
    struct QuoteLayoutResult {
        char wrappedQuote[512];    ///< Quote text with newlines inserted
        int16_t quoteWidth;        ///< Width of quote text block
        int16_t quoteHeight;       ///< Height of quote text block
        int quoteLines;            ///< Number of lines in quote
        int16_t authorWidth;       ///< Width of author text
        int16_t authorHeight;      ///< Height of author text
        int16_t totalWidth;        ///< Total width of quote+author block
        int16_t totalHeight;       ///< Total height including author
        TextPlacementRegion position;  ///< Best position for the block center
    };
    
    /**
     * @brief Find optimal line-wrapping and position for text
     * 
     * Tries different line-break configurations (1, 2, 3 lines) and finds
     * the combination of wrapping + position that scores best.
     * 
     * @param display Pointer to display instance
     * @param ttf Pointer to TTF renderer
     * @param text Original text (will try different wrappings)
     * @param fontSize Font size in pixels
     * @param candidates Array of candidate positions
     * @param numCandidates Number of candidates
     * @param textColor Primary text color
     * @param outlineColor Outline color
     * @param maxLines Maximum number of lines to try (1-4, default 3)
     * @param minWordsPerLine Minimum words per line (default 3)
     * @return Best wrapped text result with position
     */
    WrappedTextResult findBestWrappedPosition(
        EL133UF1* display, EL133UF1_TTF* ttf,
        const char* text, float fontSize,
        const TextPlacementRegion* candidates, int numCandidates,
        uint8_t textColor, uint8_t outlineColor,
        int maxLines = 3, int minWordsPerLine = 3);
    
    /**
     * @brief Wrap text to fit within a target width
     * 
     * @param ttf TTF renderer for measuring
     * @param text Input text
     * @param fontSize Font size
     * @param targetWidth Target width in pixels (0 = no limit)
     * @param output Output buffer for wrapped text
     * @param outputSize Size of output buffer
     * @param numLines Output: number of lines created
     * @return Width of the resulting wrapped text block
     */
    static int16_t wrapText(EL133UF1_TTF* ttf, const char* text, float fontSize,
                            int16_t targetWidth, char* output, size_t outputSize,
                            int* numLines);
    
    /**
     * @brief Find optimal layout and position for a quote with author
     * 
     * The author is displayed in a smaller font, right-aligned below the quote.
     * Tries different line-break configurations for the quote text.
     * 
     * @param display Pointer to display instance
     * @param ttf Pointer to TTF renderer
     * @param quote Quote with text and author
     * @param quoteFontSize Font size for quote text
     * @param authorFontSize Font size for author (typically smaller)
     * @param candidates Array of candidate positions
     * @param numCandidates Number of candidates
     * @param textColor Primary text color
     * @param outlineColor Outline color
     * @param maxLines Maximum lines for quote text (default 3)
     * @param minWordsPerLine Minimum words per line (default 3)
     * @return Layout result with wrapped text and optimal position
     */
    QuoteLayoutResult findBestQuotePosition(
        EL133UF1* display, EL133UF1_TTF* ttf,
        const Quote& quote, float quoteFontSize, float authorFontSize,
        const TextPlacementRegion* candidates, int numCandidates,
        uint8_t textColor, uint8_t outlineColor,
        int maxLines = 3, int minWordsPerLine = 3);
    
    /**
     * @brief Draw a quote with author using the layout result
     * 
     * @param ttf TTF renderer
     * @param layout Layout result from findBestQuotePosition
     * @param author Author string
     * @param quoteFontSize Quote font size
     * @param authorFontSize Author font size
     * @param textColor Text color
     * @param outlineColor Outline color
     * @param outlineWidth Outline width
     */
    void drawQuote(EL133UF1_TTF* ttf, const QuoteLayoutResult& layout,
                   const char* author, float quoteFontSize, float authorFontSize,
                   uint8_t textColor, uint8_t outlineColor, int outlineWidth = 2);

private:
    ScoringWeights _weights;
    KeepoutMargins _keepout;
    bool _useParallel;
    
    // Luminance lookup table for fast ARGB analysis
    static uint8_t _argbToLuminance[256];  // Green channel to luminance
    static bool _lutInitialized;
    
    void initLUT();
    
    // Platform-specific implementations
#if EL133UF1_USE_ARGB8888
    void getColorHistogramARGB(uint32_t* buffer, int stride,
                               int16_t x, int16_t y, int16_t w, int16_t h,
                               Spectra6Histogram& histogram);
    float computeVarianceARGB(uint32_t* buffer, int stride,
                              int16_t x, int16_t y, int16_t w, int16_t h);
    float computeEdgeDensityARGB(uint32_t* buffer, int stride,
                                 int16_t x, int16_t y, int16_t w, int16_t h);
#endif
    
    void getColorHistogramL8(uint8_t* buffer, int stride,
                             int16_t x, int16_t y, int16_t w, int16_t h,
                             Spectra6Histogram& histogram);
    float computeVarianceL8(uint8_t* buffer, int stride,
                            int16_t x, int16_t y, int16_t w, int16_t h);
    float computeEdgeDensityL8(uint8_t* buffer, int stride,
                               int16_t x, int16_t y, int16_t w, int16_t h);
    
    // ESP32-P4 parallel scoring
#if defined(SOC_PPA_SUPPORTED) && SOC_PPA_SUPPORTED
    void scoreRegionsParallel(EL133UF1* display,
                              TextPlacementRegion* regions, int numRegions,
                              uint8_t textColor, uint8_t outlineColor);
#endif
};

#endif // EL133UF1_TEXTPLACEMENT_H

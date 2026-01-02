/**
 * @file display_manager.h
 * @brief Display manager for unified media display with text overlay
 * 
 * Provides a single unified function for displaying media from mappings
 * with text overlay (time/date/weather/quote) and audio playback.
 * Used by top-of-hour cycles, !go commands, web UI, and HTTP API.
 */

#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include "EL133UF1.h"  // For color constants

/**
 * Unified function to display media from media mappings with text overlay and audio
 * This is the SINGLE function used by:
 * - Top-of-hour cycle (targetIndex = -1 for sequential)
 * - !go command (targetIndex = specific index)
 * - Web UI HTTP API (targetIndex = specific index)
 * - Web UI MQTT/GitHub Pages (targetIndex = specific index via dispatcher)
 * 
 * @param targetIndex Target media index (0-based), or -1 to use next sequential index
 * @param keepoutMargin Keepout margin for text placement (default 100)
 * @return true if successful, false otherwise
 */
bool displayMediaWithOverlay(int targetIndex = -1, int16_t keepoutMargin = 100);

// Forward declarations for display and TTF types
class EL133UF1;
class EL133UF1_TTF;

/**
 * Add text overlay (time/date/weather/quote) to an already-drawn display
 * Used by !show command and show_media_task for arbitrary image files
 * 
 * @param display Display instance
 * @param ttf TTF renderer instance
 * @param keepoutMargin Keepout margin for text placement (default 50: 25px left/right, 50px top/bottom)
 * @param textColor Text color (default EL133UF1_WHITE)
 * @param outlineColor Outline color (default EL133UF1_BLACK)
 * @param outlineThickness Outline thickness for quote/author text (default 3)
 */
void addTextOverlayToDisplay(EL133UF1* display, EL133UF1_TTF* ttf, int16_t keepoutMargin = 50, uint8_t textColor = EL133UF1_WHITE, uint8_t outlineColor = EL133UF1_BLACK, int16_t outlineThickness = 3);

/**
 * Configuration structure for Happy weather scene
 */
struct HappyWeatherConfig {
    // Location configuration
    struct Location {
        const char* name;
        float lat;
        float lon;
        int8_t timezoneOffset;
    };
    static constexpr int MAX_LOCATIONS = 6;
    Location locations[MAX_LOCATIONS];
    int numLocations;
    
    // Layout constants
    int16_t displayWidth;
    int16_t displayHeight;
    int16_t marginTop;
    int16_t marginBottom;
    int16_t gapBetweenPanels;
    
    // Panel configuration
    int16_t panelWidths[MAX_LOCATIONS];
    int numPanels;
    
    // Background image
    const char* backgroundImagePath;
    
    // Font and spacing configuration
    float baseTimeFontSize;
    float baseLocationFontSize;
    float locationFontSizeOffset;  // Added to calculated location font size
    int16_t gapBetweenLocationAndTime;
    int16_t gapBetweenTimeAndWeather;
    
    // Vertical positioning
    int16_t verticalMarginTop;
    int16_t verticalMarginBottom;
    
    // Horizontal offsets per panel (for fine-tuning)
    int16_t horizontalOffsets[MAX_LOCATIONS];
    
    // Left margin for first panel (other panels start at 0)
    int16_t firstPanelLeftMargin;
    
    // Panel alignment (true = top aligned, false = bottom aligned)
    bool panelTopAligned[MAX_LOCATIONS];
};

/**
 * Get default Happy weather scene configuration (hardcoded fallback)
 * This function returns the original hardcoded configuration values
 * 
 * @return HappyWeatherConfig with default values
 */
HappyWeatherConfig getDefaultHappyWeatherConfig();

/**
 * Display the Happy weather scene
 * Shows a static background image from LittleFS with 6 time/weather overlays
 * Each overlay represents a different geographic location
 * 
 * @param config Configuration to use (if nullptr, uses default configuration)
 * @return true if successful, false otherwise
 */
bool displayHappyWeatherScene(const HappyWeatherConfig* config = nullptr);

#endif // DISPLAY_MANAGER_H

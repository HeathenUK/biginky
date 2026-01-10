/**
 * @file nvs_manager.h
 * @brief NVS (Non-Volatile Storage) manager for persistent settings
 * 
 * Provides functions to load and save persistent settings to NVS:
 * - Audio volume
 * - Media index and mode (sequential/shuffle)
 * - Sleep duration interval
 * - Management interface timeout setting
 * 
 * Note: Hour schedule is now managed by schedule_manager.h (detailed schedule system)
 * 
 * Extracted from main_esp32p4_test.cpp as part of Priority 1 refactoring.
 */

#ifndef NVS_MANAGER_H
#define NVS_MANAGER_H

#include <Arduino.h>

// Forward declarations for global variables (defined in main file)
extern int g_audio_volume_pct;
extern uint32_t lastMediaIndex;
extern int8_t g_sleep_interval_minutes;
extern bool g_is_cold_boot;

/**
 * Load audio volume from NVS
 * Called on startup to restore the last volume setting
 */
void volumeLoadFromNVS();

/**
 * Save audio volume to NVS
 * Called whenever volume is changed
 */
void volumeSaveToNVS();

/**
 * Load media index from NVS
 * Called on startup to restore the last media index
 */
void mediaIndexLoadFromNVS();

/**
 * Save media index to NVS
 * Called whenever the media index changes
 */
void mediaIndexSaveToNVS();

/**
 * Load media index mode from NVS
 * Called on startup to restore the shuffle/sequential mode setting
 */
void mediaIndexModeLoadFromNVS();

/**
 * Save media index mode to NVS
 * Called whenever the media index mode changes
 */
void mediaIndexModeSaveToNVS();

/**
 * Get/set media index mode value (0 = SEQUENTIAL, 1 = SHUFFLE)
 * Used by main.cpp to sync with NVS functions
 */
uint8_t getMediaIndexModeValue();
void setMediaIndexModeValue(uint8_t value);

/**
 * Load sleep duration interval from NVS
 * Called on startup to restore the last sleep interval setting
 */
void sleepDurationLoadFromNVS();

/**
 * Save sleep duration interval to NVS
 * Called whenever the sleep interval changes
 */
void sleepDurationSaveToNVS();

/**
 * Load management interface timeout disabled state from NVS
 * Called on startup to restore the timeout disabled setting
 */
void manageTimeoutDisabledLoadFromNVS();

/**
 * Save management interface timeout disabled state to NVS
 * Called whenever the timeout disabled setting changes
 */
void manageTimeoutDisabledSaveToNVS();

/**
 * Get current management interface timeout disabled state
 */
bool getManageTimeoutDisabled();

/**
 * Set management interface timeout disabled state
 */
void setManageTimeoutDisabled(bool disabled);

// ============================================================================
// Display Bezel (hidden area calibration) and Content Padding
// ============================================================================
// 
// Two concepts:
// 1. BEZEL - Physical display area hidden by frame/bezel (visibility boundary)
//    Used for: calibration pattern, canvas safe area overlay
// 
// 2. CONTENT PADDING - Aesthetic spacing from visible edge to content
//    Used for: text positioning, UI elements, scene layouts
//
// Content boundary = bezel + padding
// ============================================================================

/**
 * Load display bezel settings from NVS
 * Called on startup to restore the last bezel calibration
 */
void displayBezelLoadFromNVS();

/**
 * Save display bezel settings to NVS
 * Called whenever bezel is recalibrated
 */
void displayBezelSaveToNVS();

/**
 * Get display bezel (in pixels) - the area hidden by the physical frame
 * These define the VISIBLE area boundary
 */
int16_t getDisplayBezelTop();
int16_t getDisplayBezelBottom();
int16_t getDisplayBezelLeft();
int16_t getDisplayBezelRight();

/**
 * Set display bezel (in pixels)
 * @param top Pixels hidden at top edge
 * @param bottom Pixels hidden at bottom edge
 * @param left Pixels hidden at left edge
 * @param right Pixels hidden at right edge
 */
void setDisplayBezel(int16_t top, int16_t bottom, int16_t left, int16_t right);

/**
 * Set individual display bezel values
 */
void setDisplayBezelTop(int16_t value);
void setDisplayBezelBottom(int16_t value);
void setDisplayBezelLeft(int16_t value);
void setDisplayBezelRight(int16_t value);

/**
 * Content padding - aesthetic spacing from visible edge to content
 */
int16_t getContentPadding();
void setContentPadding(int16_t value);
void contentPaddingLoadFromNVS();
void contentPaddingSaveToNVS();

/**
 * Helper struct for visible bounds (bezel only)
 */
struct VisibleBounds {
    int16_t left;      // Visible left edge (bezel_left)
    int16_t right;     // Visible right edge (display_width - bezel_right)
    int16_t top;       // Visible top edge (bezel_top)
    int16_t bottom;    // Visible bottom edge (display_height - bezel_bottom)
    int16_t width;     // Visible width
    int16_t height;    // Visible height
};

/**
 * Helper struct for content bounds (bezel + padding)
 */
struct ContentBounds {
    int16_t left;      // Content left edge (bezel_left + padding)
    int16_t right;     // Content right edge (display_width - bezel_right - padding)
    int16_t top;       // Content top edge (bezel_top + padding)
    int16_t bottom;    // Content bottom edge (display_height - bezel_bottom - padding)
    int16_t width;     // Content width
    int16_t height;    // Content height
};

/**
 * Get visible bounds (bezel only) - what's physically visible on display
 * Use this for: calibration overlay, canvas safe area
 */
VisibleBounds getVisibleBounds(int16_t displayWidth, int16_t displayHeight);

/**
 * Get content bounds (bezel + padding) - where content should be placed
 * Use this for: text positioning, UI elements, scene layouts
 */
ContentBounds getContentBounds(int16_t displayWidth, int16_t displayHeight);

// ============================================================================
// Legacy aliases for backward compatibility (deprecated - use Bezel functions)
// ============================================================================
inline void displayMarginsLoadFromNVS() { displayBezelLoadFromNVS(); }
inline void displayMarginsSaveToNVS() { displayBezelSaveToNVS(); }
inline int16_t getDisplayMarginTop() { return getDisplayBezelTop(); }
inline int16_t getDisplayMarginBottom() { return getDisplayBezelBottom(); }
inline int16_t getDisplayMarginLeft() { return getDisplayBezelLeft(); }
inline int16_t getDisplayMarginRight() { return getDisplayBezelRight(); }
inline void setDisplayMargins(int16_t t, int16_t b, int16_t l, int16_t r) { setDisplayBezel(t, b, l, r); }
inline void setDisplayMarginTop(int16_t v) { setDisplayBezelTop(v); }
inline void setDisplayMarginBottom(int16_t v) { setDisplayBezelBottom(v); }
inline void setDisplayMarginLeft(int16_t v) { setDisplayBezelLeft(v); }
inline void setDisplayMarginRight(int16_t v) { setDisplayBezelRight(v); }

// Legacy DisplayBounds - now returns VisibleBounds
typedef VisibleBounds DisplayBounds;
inline DisplayBounds getDisplayBounds(int16_t w, int16_t h) { return getVisibleBounds(w, h); }

#endif // NVS_MANAGER_H



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
// Display Margins (for calibrating visible screen area)
// ============================================================================

/**
 * Load display margins from NVS
 * Called on startup to restore the last margin settings
 */
void displayMarginsLoadFromNVS();

/**
 * Save display margins to NVS
 * Called whenever margins are changed
 */
void displayMarginsSaveToNVS();

/**
 * Get display margins (in pixels)
 * These define the safe area where content should be displayed
 */
int16_t getDisplayMarginTop();
int16_t getDisplayMarginBottom();
int16_t getDisplayMarginLeft();
int16_t getDisplayMarginRight();

/**
 * Set display margins (in pixels)
 * @param top Pixels from top edge to safe area
 * @param bottom Pixels from bottom edge to safe area  
 * @param left Pixels from left edge to safe area
 * @param right Pixels from right edge to safe area
 */
void setDisplayMargins(int16_t top, int16_t bottom, int16_t left, int16_t right);

/**
 * Set individual display margins
 */
void setDisplayMarginTop(int16_t value);
void setDisplayMarginBottom(int16_t value);
void setDisplayMarginLeft(int16_t value);
void setDisplayMarginRight(int16_t value);

/**
 * Helper struct for getting display safe bounds
 */
struct DisplayBounds {
    int16_t left;      // Safe left edge (margin_left)
    int16_t right;     // Safe right edge (display_width - margin_right)
    int16_t top;       // Safe top edge (margin_top)
    int16_t bottom;    // Safe bottom edge (display_height - margin_bottom)
    int16_t width;     // Safe width (right - left)
    int16_t height;    // Safe height (bottom - top)
};

/**
 * Get display safe bounds based on current margins
 * @param displayWidth Full display width (e.g., 1600)
 * @param displayHeight Full display height (e.g., 1200)
 * @return DisplayBounds struct with safe area coordinates
 */
DisplayBounds getDisplayBounds(int16_t displayWidth, int16_t displayHeight);

#endif // NVS_MANAGER_H



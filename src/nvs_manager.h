/**
 * @file nvs_manager.h
 * @brief NVS (Non-Volatile Storage) manager for persistent settings
 * 
 * Provides functions to load and save persistent settings to NVS:
 * - Audio volume
 * - Media index
 * - Sleep duration interval
 * - Hour schedule (24-hour enable/disable flags)
 * 
 * Extracted from main_esp32p4_test.cpp as part of Priority 1 refactoring.
 */

#ifndef NVS_MANAGER_H
#define NVS_MANAGER_H

#include <Arduino.h>

// Forward declarations for global variables (defined in main file)
extern int g_audio_volume_pct;
extern uint32_t lastMediaIndex;
extern uint8_t g_sleep_interval_minutes;
extern bool g_hour_schedule[24];
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
 * Load hour schedule from NVS
 * Hour schedule: 24 boolean flags (one per hour, 0-23)
 * If true, wake during that hour; if false, sleep through entire hour
 * Called on startup to restore the hour schedule
 */
void hourScheduleLoadFromNVS();

/**
 * Save hour schedule to NVS
 * Called whenever the hour schedule changes
 */
void hourScheduleSaveToNVS();

#endif // NVS_MANAGER_H



/**
 * @file nvs_manager.cpp
 * @brief NVS (Non-Volatile Storage) manager implementation
 * 
 * Extracted from main_esp32p4_test.cpp as part of Priority 1 refactoring.
 */

#include "nvs_manager.h"
#include "nvs_guard.h"
#include <Preferences.h>
#include <Arduino.h>

// External references to global state in main file
extern int g_audio_volume_pct;
extern uint32_t lastMediaIndex;
extern uint8_t g_sleep_interval_minutes;
extern bool g_hour_schedule[24];
extern bool g_is_cold_boot;

// External references to Preferences objects in main file
extern Preferences volumePrefs;
extern Preferences mediaPrefs;
extern Preferences sleepPrefs;
extern Preferences hourSchedulePrefs;

void volumeLoadFromNVS() {
    NVSGuard guard(volumePrefs, "audio", true);  // read-only
    if (!guard.isOpen()) {
        Serial.println("WARNING: Failed to open NVS for volume - using default (50%)");
        g_audio_volume_pct = 50;
        return;
    }
    
    int savedVolume = guard.get().getInt("volume", 50);  // Default to 50 if not set
    // guard automatically calls end() in destructor
    
    // Clamp to valid range
    if (savedVolume < 0) savedVolume = 0;
    if (savedVolume > 100) savedVolume = 100;
    
    g_audio_volume_pct = savedVolume;
    if (g_is_cold_boot) {
        Serial.printf("Loaded volume from NVS: %d%%\n", g_audio_volume_pct);
    }
}

void volumeSaveToNVS() {
    NVSGuard guard(volumePrefs, "audio", false);  // Read-write
    if (!guard.isOpen()) {
        Serial.println("WARNING: Failed to open NVS for saving volume");
        return;
    }
    
    guard.get().putInt("volume", g_audio_volume_pct);
    
    Serial.printf("Saved volume to NVS: %d%%\n", g_audio_volume_pct);
}

void mediaIndexLoadFromNVS() {
    NVSGuard guard(mediaPrefs, "media", true);  // Read-only
    if (!guard.isOpen()) {
        Serial.println("WARNING: Failed to open NVS for media index - using default (0)");
        lastMediaIndex = 0;
        return;
    }
    
    uint32_t savedIndex = guard.get().getUInt("index", 0);
    
    lastMediaIndex = savedIndex;
    if (g_is_cold_boot) {
        Serial.printf("Loaded media index from NVS: %lu\n", (unsigned long)lastMediaIndex);
    }
}

void mediaIndexSaveToNVS() {
    // Note: This function was using manual begin/end in the original code
    // We use NVSGuard for consistency, but the original pattern is preserved
    NVSGuard guard(mediaPrefs, "media", false);  // Read-write
    if (!guard.isOpen()) {
        Serial.println("WARNING: Failed to open NVS for saving media index");
        return;
    }
    
    guard.get().putUInt("index", lastMediaIndex);
    
    Serial.printf("Saved media index to NVS: %lu\n", (unsigned long)lastMediaIndex);
}

// Media index mode storage (uint8_t: 0 = SEQUENTIAL, 1 = SHUFFLE)
// This is set/read by main.cpp functions, we just store/retrieve from NVS
static uint8_t g_mediaIndexModeValue = 0;  // 0 = SEQUENTIAL (default)

uint8_t getMediaIndexModeValue() {
    return g_mediaIndexModeValue;
}

void setMediaIndexModeValue(uint8_t value) {
    g_mediaIndexModeValue = value;
}

void mediaIndexModeLoadFromNVS() {
    NVSGuard guard(mediaPrefs, "media", true);  // Read-only
    if (!guard.isOpen()) {
        Serial.println("WARNING: Failed to open NVS for media index mode - using default (SEQUENTIAL)");
        g_mediaIndexModeValue = 0;  // SEQUENTIAL
        return;
    }
    
    g_mediaIndexModeValue = guard.get().getUChar("mode", 0);  // 0 = SEQUENTIAL, 1 = SHUFFLE
    
    if (g_is_cold_boot) {
        Serial.printf("Loaded media index mode from NVS: %s\n", 
                     (g_mediaIndexModeValue == 1) ? "SHUFFLE" : "SEQUENTIAL");
    }
}

void mediaIndexModeSaveToNVS() {
    NVSGuard guard(mediaPrefs, "media", false);  // Read-write
    if (!guard.isOpen()) {
        Serial.println("WARNING: Failed to open NVS for saving media index mode");
        return;
    }
    
    guard.get().putUChar("mode", g_mediaIndexModeValue);
    
    Serial.printf("Saved media index mode to NVS: %s\n",
                 (g_mediaIndexModeValue == 1) ? "SHUFFLE" : "SEQUENTIAL");
}

void sleepDurationLoadFromNVS() {
    NVSGuard guard(sleepPrefs, "sleep", true);  // Read-only
    if (!guard.isOpen()) {
        Serial.println("WARNING: Failed to open NVS for sleep duration - using default (1 minute)");
        g_sleep_interval_minutes = 1;
        return;
    }
    
    uint8_t savedInterval = guard.get().getUChar("interval", 1);  // Default to 1 if not set
    
    // Validate: must be a factor of 60
    // Valid factors: 1, 2, 3, 4, 5, 6, 10, 12, 15, 20, 30, 60
    if (savedInterval == 0 || 60 % savedInterval != 0) {
        Serial.printf("WARNING: Invalid sleep interval %d in NVS (not a factor of 60), using default (1)\n", savedInterval);
        g_sleep_interval_minutes = 1;
    } else {
        g_sleep_interval_minutes = savedInterval;
        if (g_is_cold_boot) {
            Serial.printf("Loaded sleep interval from NVS: %d minutes\n", g_sleep_interval_minutes);
        }
    }
}

void sleepDurationSaveToNVS() {
    NVSGuard guard(sleepPrefs, "sleep", false);  // Read-write
    if (!guard.isOpen()) {
        Serial.println("WARNING: Failed to open NVS for saving sleep duration");
        return;
    }
    
    guard.get().putUChar("interval", g_sleep_interval_minutes);
    
    Serial.printf("Saved sleep interval to NVS: %d minutes\n", g_sleep_interval_minutes);
}

void hourScheduleLoadFromNVS() {
    // Initialize all hours to enabled by default
    for (int i = 0; i < 24; i++) {
        g_hour_schedule[i] = true;
    }
    
    NVSGuard guard(hourSchedulePrefs, "hours", true);  // Read-only
    if (!guard.isOpen()) {
        // This is normal on first boot or after NVS clear - namespace doesn't exist yet
        // We'll use defaults (all hours enabled) and the namespace will be created on first save
        if (g_is_cold_boot) {
            Serial.println("INFO: NVS namespace 'hours' not found or failed to open - using default (all hours enabled)");
            Serial.println("      This is normal on first run or after NVS erase. Your schedule will be saved when you configure it.");
        }
        return;
    }
    
    // Load hour schedule as a 24-byte string (each byte is '1' or '0')
    String scheduleStr = guard.get().getString("schedule", "");
    
    if (scheduleStr.length() == 24) {
        // Parse the schedule string
        for (int i = 0; i < 24; i++) {
            g_hour_schedule[i] = (scheduleStr.charAt(i) == '1');
        }
        if (g_is_cold_boot) {
            Serial.println("Loaded hour schedule from NVS:");
            for (int i = 0; i < 24; i++) {
                Serial.printf("  Hour %02d: %s\n", i, g_hour_schedule[i] ? "ENABLED" : "DISABLED");
            }
        }
    } else {
        if (g_is_cold_boot) {
            Serial.println("No hour schedule in NVS - using default (all hours enabled)");
        }
    }
}

void hourScheduleSaveToNVS() {
    NVSGuard guard(hourSchedulePrefs, "hours", false);  // Read-write
    if (!guard.isOpen()) {
        Serial.println("WARNING: Failed to open NVS for saving hour schedule");
        return;
    }
    
    // Save hour schedule as a 24-byte string (each byte is '1' or '0')
    String scheduleStr = "";
    for (int i = 0; i < 24; i++) {
        scheduleStr += (g_hour_schedule[i] ? '1' : '0');
    }
    
    guard.get().putString("schedule", scheduleStr);
    
    Serial.println("Saved hour schedule to NVS:");
    for (int i = 0; i < 24; i++) {
        Serial.printf("  Hour %02d: %s\n", i, g_hour_schedule[i] ? "ENABLED" : "DISABLED");
    }
}


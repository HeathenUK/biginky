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
extern int8_t g_sleep_interval_minutes;
extern bool g_is_cold_boot;

// External references to Preferences objects in main file
extern Preferences volumePrefs;
extern Preferences mediaPrefs;
extern Preferences sleepPrefs;
extern Preferences detailedSchedulePrefs;
extern Preferences managePrefs;

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
    
    // Use getChar for signed 8-bit value (backward compatible - same bits, different interpretation)
    int8_t savedInterval = guard.get().getChar("interval", 1);  // Default to 1 if not set
    
    // Validate: 0 = always-on, -1 = event-driven, >0 must be factor of 60
    if (savedInterval == 0 || savedInterval == -1) {
        // Special modes - valid as-is
        g_sleep_interval_minutes = savedInterval;
        if (g_is_cold_boot) {
            if (savedInterval == 0) {
                Serial.println("Loaded sleep mode from NVS: ALWAYS-ON (0)");
            } else {
                Serial.println("Loaded sleep mode from NVS: EVENT-DRIVEN (-1)");
            }
        }
    } else if (savedInterval > 0 && 60 % savedInterval == 0) {
        // Valid interval (factor of 60)
        g_sleep_interval_minutes = savedInterval;
        if (g_is_cold_boot) {
            Serial.printf("Loaded sleep interval from NVS: %d minutes\n", g_sleep_interval_minutes);
        }
    } else {
        Serial.printf("WARNING: Invalid sleep interval %d in NVS, using default (1)\n", savedInterval);
        g_sleep_interval_minutes = 1;
    }
}

void sleepDurationSaveToNVS() {
    NVSGuard guard(sleepPrefs, "sleep", false);  // Read-write
    if (!guard.isOpen()) {
        Serial.println("WARNING: Failed to open NVS for saving sleep duration");
        return;
    }
    
    guard.get().putChar("interval", g_sleep_interval_minutes);
    
    if (g_sleep_interval_minutes == 0) {
        Serial.println("Saved sleep mode to NVS: ALWAYS-ON (0)");
    } else if (g_sleep_interval_minutes == -1) {
        Serial.println("Saved sleep mode to NVS: EVENT-DRIVEN (-1)");
    } else {
        Serial.printf("Saved sleep interval to NVS: %d minutes\n", g_sleep_interval_minutes);
    }
}

// Management interface timeout disabled state
static bool g_manage_timeout_disabled = false;  // Default: timeout enabled (false = timeout active)

bool getManageTimeoutDisabled() {
    return g_manage_timeout_disabled;
}

void setManageTimeoutDisabled(bool disabled) {
    g_manage_timeout_disabled = disabled;
}

void manageTimeoutDisabledLoadFromNVS() {
    NVSGuard guard(managePrefs, "manage", true);  // Read-only
    if (!guard.isOpen()) {
        Serial.println("WARNING: Failed to open NVS for manage timeout disabled - using default (timeout enabled)");
        g_manage_timeout_disabled = false;
        return;
    }
    
    g_manage_timeout_disabled = guard.get().getBool("timeout_disabled", false);  // Default: timeout enabled (false)
    
    if (g_is_cold_boot) {
        Serial.printf("Loaded management interface timeout disabled state from NVS: %s\n",
                     g_manage_timeout_disabled ? "DISABLED (no timeout)" : "ENABLED (5 min timeout)");
    }
}

void manageTimeoutDisabledSaveToNVS() {
    NVSGuard guard(managePrefs, "manage", false);  // Read-write
    if (!guard.isOpen()) {
        Serial.println("WARNING: Failed to open NVS for saving manage timeout disabled");
        return;
    }
    
    guard.get().putBool("timeout_disabled", g_manage_timeout_disabled);
    
    Serial.printf("Saved management interface timeout disabled state to NVS: %s\n",
                 g_manage_timeout_disabled ? "DISABLED (no timeout)" : "ENABLED (5 min timeout)");
}

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

// ============================================================================
// Display Bezel (hidden area) and Content Padding
// ============================================================================

// External reference to Preferences object in main file
extern Preferences displayPrefs;

// Display bezel values (in pixels hidden by frame at each edge)
// Defaults based on typical e-ink display overscan
static int16_t g_display_bezel_top = 50;      // Top bezel
static int16_t g_display_bezel_bottom = 70;   // Bottom bezel (usually larger due to connector)
static int16_t g_display_bezel_left = 60;     // Left bezel
static int16_t g_display_bezel_right = 60;    // Right bezel

// Content padding (aesthetic spacing from visible edge to content)
static int16_t g_content_padding = 20;        // Default 20px padding

// Bezel getters
int16_t getDisplayBezelTop() { return g_display_bezel_top; }
int16_t getDisplayBezelBottom() { return g_display_bezel_bottom; }
int16_t getDisplayBezelLeft() { return g_display_bezel_left; }
int16_t getDisplayBezelRight() { return g_display_bezel_right; }

// Bezel setters
void setDisplayBezel(int16_t top, int16_t bottom, int16_t left, int16_t right) {
    g_display_bezel_top = top;
    g_display_bezel_bottom = bottom;
    g_display_bezel_left = left;
    g_display_bezel_right = right;
}

void setDisplayBezelTop(int16_t value) { g_display_bezel_top = value; }
void setDisplayBezelBottom(int16_t value) { g_display_bezel_bottom = value; }
void setDisplayBezelLeft(int16_t value) { g_display_bezel_left = value; }
void setDisplayBezelRight(int16_t value) { g_display_bezel_right = value; }

// Content padding getter/setter
int16_t getContentPadding() { return g_content_padding; }
void setContentPadding(int16_t value) { 
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    g_content_padding = value; 
}

void displayBezelLoadFromNVS() {
    NVSGuard guard(displayPrefs, "display", true);  // Read-only
    if (!guard.isOpen()) {
        Serial.println("WARNING: Failed to open NVS for display bezel - using defaults");
        return;
    }
    
    // Load bezel values (using legacy key names for backward compatibility)
    g_display_bezel_top = guard.get().getShort("margin_top", 50);
    g_display_bezel_bottom = guard.get().getShort("margin_bot", 70);
    g_display_bezel_left = guard.get().getShort("margin_left", 60);
    g_display_bezel_right = guard.get().getShort("margin_right", 60);
    
    // Clamp to reasonable values (0-200 pixels)
    if (g_display_bezel_top < 0) g_display_bezel_top = 0;
    if (g_display_bezel_top > 200) g_display_bezel_top = 200;
    if (g_display_bezel_bottom < 0) g_display_bezel_bottom = 0;
    if (g_display_bezel_bottom > 200) g_display_bezel_bottom = 200;
    if (g_display_bezel_left < 0) g_display_bezel_left = 0;
    if (g_display_bezel_left > 200) g_display_bezel_left = 200;
    if (g_display_bezel_right < 0) g_display_bezel_right = 0;
    if (g_display_bezel_right > 200) g_display_bezel_right = 200;
    
    if (g_is_cold_boot) {
        Serial.printf("Loaded display bezel from NVS: top=%d, bottom=%d, left=%d, right=%d\n",
                     g_display_bezel_top, g_display_bezel_bottom, 
                     g_display_bezel_left, g_display_bezel_right);
    }
}

void displayBezelSaveToNVS() {
    NVSGuard guard(displayPrefs, "display", false);  // Read-write
    if (!guard.isOpen()) {
        Serial.println("WARNING: Failed to open NVS for saving display bezel");
        return;
    }
    
    // Save bezel values (using legacy key names for backward compatibility)
    guard.get().putShort("margin_top", g_display_bezel_top);
    guard.get().putShort("margin_bot", g_display_bezel_bottom);
    guard.get().putShort("margin_left", g_display_bezel_left);
    guard.get().putShort("margin_right", g_display_bezel_right);
    
    Serial.printf("Saved display bezel to NVS: top=%d, bottom=%d, left=%d, right=%d\n",
                 g_display_bezel_top, g_display_bezel_bottom, 
                 g_display_bezel_left, g_display_bezel_right);
}

void contentPaddingLoadFromNVS() {
    NVSGuard guard(displayPrefs, "display", true);  // Read-only
    if (!guard.isOpen()) {
        Serial.println("WARNING: Failed to open NVS for content padding - using default (20px)");
        g_content_padding = 20;
        return;
    }
    
    g_content_padding = guard.get().getShort("padding", 20);
    
    // Clamp to reasonable values (0-100 pixels)
    if (g_content_padding < 0) g_content_padding = 0;
    if (g_content_padding > 100) g_content_padding = 100;
    
    if (g_is_cold_boot) {
        Serial.printf("Loaded content padding from NVS: %d\n", g_content_padding);
    }
}

void contentPaddingSaveToNVS() {
    NVSGuard guard(displayPrefs, "display", false);  // Read-write
    if (!guard.isOpen()) {
        Serial.println("WARNING: Failed to open NVS for saving content padding");
        return;
    }
    
    guard.get().putShort("padding", g_content_padding);
    
    Serial.printf("Saved content padding to NVS: %d\n", g_content_padding);
}

VisibleBounds getVisibleBounds(int16_t displayWidth, int16_t displayHeight) {
    VisibleBounds bounds;
    bounds.left = g_display_bezel_left;
    bounds.right = displayWidth - g_display_bezel_right;
    bounds.top = g_display_bezel_top;
    bounds.bottom = displayHeight - g_display_bezel_bottom;
    bounds.width = bounds.right - bounds.left;
    bounds.height = bounds.bottom - bounds.top;
    return bounds;
}

ContentBounds getContentBounds(int16_t displayWidth, int16_t displayHeight) {
    ContentBounds bounds;
    bounds.left = g_display_bezel_left + g_content_padding;
    bounds.right = displayWidth - g_display_bezel_right - g_content_padding;
    bounds.top = g_display_bezel_top + g_content_padding;
    bounds.bottom = displayHeight - g_display_bezel_bottom - g_content_padding;
    bounds.width = bounds.right - bounds.left;
    bounds.height = bounds.bottom - bounds.top;
    return bounds;
}

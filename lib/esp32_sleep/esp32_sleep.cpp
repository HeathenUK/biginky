/**
 * @file esp32_sleep.cpp
 * @brief Deep Sleep functionality implementation for ESP32
 * 
 * Uses ESP32's deep sleep with RTC memory for persistent data.
 */

#include "esp32_sleep.h"

#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)

#include <esp_sleep.h>
#include <esp_system.h>
#include <sys/time.h>
#include <time.h>

// Try to include DS3231 driver if available
#if __has_include("DS3231.h")
#include "DS3231.h"
#define HAS_DS3231 1
#else
#define HAS_DS3231 0
#endif

// ========================================================================
// RTC Memory - persists across deep sleep
// ========================================================================

// Magic number to detect valid RTC memory
#define RTC_MAGIC 0xDEADBEEF

// RTC memory structure
typedef struct {
    uint32_t magic;              // Magic number for validity check
    uint32_t boot_count;         // Number of boots
    uint32_t uptime_seconds;     // Accumulated uptime
    int32_t drift_ppm;           // Drift calibration
    uint64_t last_sync_time;     // Last NTP sync time
    uint64_t last_sync_rtc;      // RTC value at last sync
    bool wake_flag;              // Set before sleep, cleared on wake check
    int8_t wake_gpio_pins[4];    // GPIO wake sources
    bool wake_gpio_active_high[4];
    int8_t wake_gpio_count;
} rtc_sleep_data_t;

// RTC memory storage (survives deep sleep, not power loss)
RTC_DATA_ATTR static rtc_sleep_data_t rtc_data;

// ========================================================================
// DS3231 RTC state
// ========================================================================

static bool _rtc_present = false;
static int _rtc_int_pin = -1;

// ========================================================================
// Helper functions
// ========================================================================

static void init_rtc_data_if_needed(void) {
    if (rtc_data.magic != RTC_MAGIC) {
        // First boot or RTC memory corrupted - initialize
        Serial.println("[ESP32_SLEEP] Initializing RTC memory");
        memset(&rtc_data, 0, sizeof(rtc_data));
        rtc_data.magic = RTC_MAGIC;
        for (int i = 0; i < 4; i++) {
            rtc_data.wake_gpio_pins[i] = -1;
        }
    }
}

// ========================================================================
// DS3231 External RTC functions
// ========================================================================

bool sleep_init_rtc(int sda_pin, int scl_pin, int int_pin) {
    init_rtc_data_if_needed();
    
    Serial.printf("[ESP32_SLEEP] sleep_init_rtc: SDA=%d, SCL=%d, INT=%d\n", 
                  sda_pin, scl_pin, int_pin);
    
#if HAS_DS3231
    // Initialize I2C for DS3231
    Wire.begin(sda_pin, scl_pin);
    Wire.setClock(100000);  // 100kHz I2C
    
    // Try to initialize DS3231
    _rtc_present = rtc.begin(&Wire, sda_pin, scl_pin);
    _rtc_int_pin = int_pin;
    
    if (_rtc_present) {
        Serial.println("[ESP32_SLEEP] DS3231 RTC detected");
        rtc.printStatus();
        
        if (int_pin >= 0) {
            pinMode(int_pin, INPUT_PULLUP);
            rtc.clearAlarm1();
        }
        return true;
    }
#endif
    
    Serial.println("[ESP32_SLEEP] DS3231 not found - using ESP32 internal RTC");
    return false;
}

bool sleep_has_rtc(void) {
    return _rtc_present;
}

int sleep_get_rtc_int_pin(void) {
    return _rtc_int_pin;
}

// ========================================================================
// Sleep functions
// ========================================================================

bool sleep_woke_from_deep_sleep(void) {
    init_rtc_data_if_needed();
    
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    
    if (cause == ESP_SLEEP_WAKEUP_UNDEFINED) {
        // Fresh boot, not wake from sleep
        return false;
    }
    
    // Check our RTC memory flag too
    return rtc_data.wake_flag;
}

void sleep_clear_wake_flag(void) {
    init_rtc_data_if_needed();
    rtc_data.wake_flag = false;
}

void sleep_clear_all_state(void) {
    memset(&rtc_data, 0, sizeof(rtc_data));
    rtc_data.magic = RTC_MAGIC;
    for (int i = 0; i < 4; i++) {
        rtc_data.wake_gpio_pins[i] = -1;
    }
    Serial.println("[ESP32_SLEEP] All sleep state cleared");
}

void sleep_goto_dormant_for_ms(uint32_t delay_ms) {
    init_rtc_data_if_needed();
    
    // Set wake flag so we know this was a sleep wake
    rtc_data.wake_flag = true;
    rtc_data.boot_count++;
    
    // Update uptime
    rtc_data.uptime_seconds += millis() / 1000;
    
    Serial.printf("[ESP32_SLEEP] Entering deep sleep for %lu ms\n", delay_ms);
    Serial.flush();
    
#if HAS_DS3231
    // If we have DS3231, use its alarm for wake
    if (_rtc_present && _rtc_int_pin >= 0) {
        Serial.println("[ESP32_SLEEP] Using DS3231 alarm for wake");
        
        // Clear any existing alarm and set new one
        rtc.clearAlarm1();
        rtc.setAlarm1(delay_ms);
        
        // Configure GPIO wake on RTC INT pin (active low)
        esp_sleep_enable_ext0_wakeup((gpio_num_t)_rtc_int_pin, 0);
    } else
#endif
    {
        // Use ESP32's internal timer for wake
        esp_sleep_enable_timer_wakeup((uint64_t)delay_ms * 1000ULL);
    }
    
    // Configure additional GPIO wake sources if any
    uint64_t gpio_mask = 0;
    for (int i = 0; i < rtc_data.wake_gpio_count; i++) {
        if (rtc_data.wake_gpio_pins[i] >= 0) {
            gpio_mask |= (1ULL << rtc_data.wake_gpio_pins[i]);
        }
    }
    
    if (gpio_mask != 0) {
        // Use ext1 for multiple GPIO wake sources
        // Note: All pins must have same wake level with ext1
        esp_sleep_enable_ext1_wakeup(gpio_mask, ESP_EXT1_WAKEUP_ALL_LOW);
    }
    
    // Enter deep sleep
    esp_deep_sleep_start();
    
    // Never reached
}

// ========================================================================
// Time functions
// ========================================================================

uint64_t sleep_get_time_ms(void) {
#if HAS_DS3231
    if (_rtc_present) {
        return rtc.getTimeMs();
    }
#endif
    
    // Fallback to ESP32 system time
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + tv.tv_usec / 1000;
}

void sleep_set_time_ms(uint64_t time_ms) {
#if HAS_DS3231
    if (_rtc_present) {
        rtc.setTimeMs(time_ms);
        Serial.printf("[ESP32_SLEEP] DS3231 time set to %llu ms\n", time_ms);
    }
#endif
    
    // Also set ESP32 system time
    struct timeval tv;
    tv.tv_sec = time_ms / 1000;
    tv.tv_usec = (time_ms % 1000) * 1000;
    settimeofday(&tv, NULL);
}

uint32_t sleep_get_uptime_seconds(void) {
    init_rtc_data_if_needed();
    return rtc_data.uptime_seconds + (millis() / 1000);
}

uint64_t sleep_get_corrected_time_ms(void) {
#if HAS_DS3231
    // DS3231 is crystal-accurate, no drift correction needed
    if (_rtc_present) {
        return rtc.getTimeMs();
    }
#endif
    
    // ESP32's RTC is also quite accurate, return as-is
    return sleep_get_time_ms();
}

void sleep_calibrate_drift(uint64_t accurate_time_ms) {
    init_rtc_data_if_needed();
    
#if HAS_DS3231
    if (_rtc_present) {
        // Just set the time, DS3231 doesn't need drift calibration
        rtc.setTimeMs(accurate_time_ms);
        rtc_data.last_sync_time = accurate_time_ms;
        rtc_data.last_sync_rtc = accurate_time_ms;
        Serial.println("[ESP32_SLEEP] DS3231 calibrated from NTP");
        return;
    }
#endif
    
    // Set ESP32 system time
    sleep_set_time_ms(accurate_time_ms);
    
    // Store sync point
    rtc_data.last_sync_time = accurate_time_ms;
    rtc_data.last_sync_rtc = accurate_time_ms;
    
    Serial.printf("[ESP32_SLEEP] Time calibrated to %llu ms\n", accurate_time_ms);
}

int32_t sleep_get_drift_ppm(void) {
    init_rtc_data_if_needed();
    return rtc_data.drift_ppm;
}

void sleep_set_drift_ppm(int32_t drift_ppm) {
    init_rtc_data_if_needed();
    rtc_data.drift_ppm = drift_ppm;
}

// ========================================================================
// GPIO wake sources
// ========================================================================

int sleep_add_gpio_wake_source(int pin, bool active_high) {
    init_rtc_data_if_needed();
    
    if (rtc_data.wake_gpio_count >= 4) {
        Serial.println("[ESP32_SLEEP] No more GPIO wake slots available");
        return -1;
    }
    
    int slot = rtc_data.wake_gpio_count;
    rtc_data.wake_gpio_pins[slot] = pin;
    rtc_data.wake_gpio_active_high[slot] = active_high;
    rtc_data.wake_gpio_count++;
    
    // Configure pin
    if (active_high) {
        pinMode(pin, INPUT_PULLDOWN);
    } else {
        pinMode(pin, INPUT_PULLUP);
    }
    
    Serial.printf("[ESP32_SLEEP] Added GPIO%d as wake source (slot %d, active-%s)\n",
                  pin, slot, active_high ? "high" : "low");
    
    return slot;
}

void sleep_clear_gpio_wake_sources(void) {
    init_rtc_data_if_needed();
    
    for (int i = 0; i < 4; i++) {
        rtc_data.wake_gpio_pins[i] = -1;
        rtc_data.wake_gpio_active_high[i] = false;
    }
    rtc_data.wake_gpio_count = 0;
    
    Serial.println("[ESP32_SLEEP] Cleared all GPIO wake sources");
}

int sleep_get_wake_gpio(void) {
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    
    if (cause == ESP_SLEEP_WAKEUP_EXT0) {
        // EXT0 is the DS3231 INT pin
        return _rtc_int_pin;
    }
    
    if (cause == ESP_SLEEP_WAKEUP_EXT1) {
        // Check which GPIO triggered
        uint64_t status = esp_sleep_get_ext1_wakeup_status();
        for (int i = 0; i < 64; i++) {
            if (status & (1ULL << i)) {
                return i;
            }
        }
    }
    
    return -1;  // Timer wake or unknown
}

#endif // ESP32 || ARDUINO_ARCH_ESP32

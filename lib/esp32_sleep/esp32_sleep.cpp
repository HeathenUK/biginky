/**
 * @file esp32_sleep.cpp
 * @brief Deep Sleep functionality implementation for ESP32
 * 
 * Uses ESP32's deep sleep with RTC memory for persistent data.
 * 
 * IMPORTANT for ESP32-P4:
 * - ext0 wake is NOT supported, only ext1
 * - Only GPIO 0-15 (LP GPIOs) can wake from deep sleep
 * - RTC INT pin must be on GPIO 0-15 for wake functionality
 */

#include "esp32_sleep.h"

#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)

#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_chip_info.h>
#include <soc/soc_caps.h>
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
// Platform detection and wake source support
// ========================================================================

// ESP32-P4 has 16 LP GPIOs (0-15) that can be used for deep sleep wake
#if CONFIG_IDF_TARGET_ESP32P4
    #define ESP32P4_LP_GPIO_MAX 15
    #define HAS_EXT0_WAKE 0  // ESP32-P4 does NOT support ext0
    #define HAS_EXT1_WAKE 1
#elif CONFIG_IDF_TARGET_ESP32S3
    #define HAS_EXT0_WAKE 1
    #define HAS_EXT1_WAKE 1
#elif CONFIG_IDF_TARGET_ESP32S2
    #define HAS_EXT0_WAKE 1
    #define HAS_EXT1_WAKE 1
#elif CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C6
    // ESP32-C3/C6 use gpio_wakeup instead
    #define HAS_EXT0_WAKE 0
    #define HAS_EXT1_WAKE 0
    #define HAS_GPIO_WAKEUP 1
#else
    // Original ESP32
    #define HAS_EXT0_WAKE 1
    #define HAS_EXT1_WAKE 1
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

/**
 * @brief Check if a GPIO can be used for deep sleep wake
 * 
 * On ESP32-P4, only GPIO 0-15 (LP GPIOs) can wake from deep sleep.
 * On other ESP32 variants, RTC GPIOs can be used.
 */
static bool is_valid_wake_gpio(int gpio) {
#if CONFIG_IDF_TARGET_ESP32P4
    // ESP32-P4: Only LP GPIOs 0-15 can wake from deep sleep
    if (gpio < 0 || gpio > ESP32P4_LP_GPIO_MAX) {
        return false;
    }
    return true;
#elif defined(SOC_RTCIO_PIN_COUNT)
    // Other ESP32 variants: Check if it's an RTC GPIO
    // This is a simplification - proper check would use rtc_io_number_get()
    return gpio >= 0 && gpio < SOC_RTCIO_PIN_COUNT;
#else
    return gpio >= 0;
#endif
}

static const char* get_chip_name(void) {
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    switch (chip_info.model) {
        case CHIP_ESP32:   return "ESP32";
        case CHIP_ESP32S2: return "ESP32-S2";
        case CHIP_ESP32S3: return "ESP32-S3";
        case CHIP_ESP32C3: return "ESP32-C3";
        case CHIP_ESP32C6: return "ESP32-C6";
#if defined(CHIP_ESP32H2)
        case CHIP_ESP32H2: return "ESP32-H2";
#endif
#if defined(CHIP_ESP32P4)
        case CHIP_ESP32P4: return "ESP32-P4";
#endif
        default: return "Unknown ESP32";
    }
}

// ========================================================================
// DS3231 External RTC functions
// ========================================================================

bool sleep_init_rtc(int sda_pin, int scl_pin, int int_pin) {
    init_rtc_data_if_needed();
    
    Serial.printf("[ESP32_SLEEP] %s: sleep_init_rtc(SDA=%d, SCL=%d, INT=%d)\n", 
                  get_chip_name(), sda_pin, scl_pin, int_pin);
    
    // Validate wake pin for deep sleep wake capability
    if (int_pin >= 0 && !is_valid_wake_gpio(int_pin)) {
#if CONFIG_IDF_TARGET_ESP32P4
        Serial.printf("[ESP32_SLEEP] WARNING: GPIO%d cannot wake from deep sleep on ESP32-P4!\n", int_pin);
        Serial.println("[ESP32_SLEEP] ESP32-P4 can only wake from GPIO 0-15 (LP GPIOs)");
        Serial.println("[ESP32_SLEEP] Suggest moving RTC INT to GPIO4, GPIO5, GPIO7, or GPIO8");
        // Don't fail - the RTC can still be used for timekeeping, just not wake
#endif
    }
    
#if HAS_DS3231
    // Initialize I2C for DS3231
    Wire.begin(sda_pin, scl_pin);
    Wire.setClock(100000);  // 100kHz I2C
    
    // Try to initialize DS3231
    _rtc_present = rtc.begin(&Wire, sda_pin, scl_pin);
    _rtc_int_pin = int_pin;
    
    if (_rtc_present) {
        Serial.println("[ESP32_SLEEP] DS3231 RTC detected");
        
        if (int_pin >= 0) {
            pinMode(int_pin, INPUT_PULLUP);
            rtc.clearAlarm1();
            
            if (is_valid_wake_gpio(int_pin)) {
                Serial.printf("[ESP32_SLEEP] GPIO%d configured for wake\n", int_pin);
            }
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
    
    Serial.printf("[ESP32_SLEEP] Entering deep sleep for %lu ms on %s\n", delay_ms, get_chip_name());
    
    bool timer_enabled = false;
    bool gpio_wake_configured = false;
    
#if HAS_DS3231
    // If we have DS3231 with valid wake pin, use its alarm
    if (_rtc_present && _rtc_int_pin >= 0 && is_valid_wake_gpio(_rtc_int_pin)) {
        Serial.printf("[ESP32_SLEEP] Using DS3231 alarm + GPIO%d for wake\n", _rtc_int_pin);
        
        // Clear any existing alarm and set new one
        rtc.clearAlarm1();
        rtc.setAlarm1(delay_ms);
        rtc.enableAlarm1Interrupt(true);
        
#if CONFIG_IDF_TARGET_ESP32P4
        // ESP32-P4: Use ext1 (ext0 not supported)
        uint64_t gpio_mask = (1ULL << _rtc_int_pin);
        esp_err_t err = esp_sleep_enable_ext1_wakeup(gpio_mask, ESP_EXT1_WAKEUP_ANY_LOW);
        if (err != ESP_OK) {
            Serial.printf("[ESP32_SLEEP] WARNING: ext1 wake config failed: %d\n", err);
        } else {
            gpio_wake_configured = true;
        }
#elif HAS_EXT0_WAKE
        // Other ESP32 variants: Use ext0 for single pin wake
        esp_err_t err = esp_sleep_enable_ext0_wakeup((gpio_num_t)_rtc_int_pin, 0);
        if (err != ESP_OK) {
            Serial.printf("[ESP32_SLEEP] WARNING: ext0 wake config failed: %d\n", err);
        } else {
            gpio_wake_configured = true;
        }
#endif
    } else if (_rtc_present && _rtc_int_pin >= 0) {
        // RTC present but INT pin can't wake - still use alarm but fall back to timer
        Serial.println("[ESP32_SLEEP] WARNING: RTC INT pin cannot wake, using timer fallback");
        rtc.clearAlarm1();
        rtc.setAlarm1(delay_ms);
    }
#endif

    // If GPIO wake not configured, use timer
    if (!gpio_wake_configured) {
        Serial.println("[ESP32_SLEEP] Using ESP32 timer for wake");
        esp_sleep_enable_timer_wakeup((uint64_t)delay_ms * 1000ULL);
        timer_enabled = true;
    }
    
    // Configure additional GPIO wake sources if any
    uint64_t extra_gpio_mask = 0;
    for (int i = 0; i < rtc_data.wake_gpio_count; i++) {
        int pin = rtc_data.wake_gpio_pins[i];
        if (pin >= 0 && is_valid_wake_gpio(pin)) {
            extra_gpio_mask |= (1ULL << pin);
            Serial.printf("[ESP32_SLEEP] Adding GPIO%d to wake mask\n", pin);
        }
    }
    
    if (extra_gpio_mask != 0) {
#if CONFIG_IDF_TARGET_ESP32P4 || HAS_EXT1_WAKE
        // Combine with RTC INT pin mask if configured
        if (gpio_wake_configured && _rtc_int_pin >= 0) {
            extra_gpio_mask |= (1ULL << _rtc_int_pin);
        }
        esp_err_t err = esp_sleep_enable_ext1_wakeup(extra_gpio_mask, ESP_EXT1_WAKEUP_ANY_LOW);
        if (err != ESP_OK) {
            Serial.printf("[ESP32_SLEEP] WARNING: ext1 additional GPIOs failed: %d\n", err);
        }
#endif
    }
    
    Serial.printf("[ESP32_SLEEP] Boot count: %lu, total uptime: %lu s\n", 
                  rtc_data.boot_count, rtc_data.uptime_seconds);
    Serial.flush();
    delay(10);  // Ensure serial output completes
    
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
    
    // Validate the GPIO can wake from deep sleep
    if (!is_valid_wake_gpio(pin)) {
#if CONFIG_IDF_TARGET_ESP32P4
        Serial.printf("[ESP32_SLEEP] ERROR: GPIO%d cannot wake from deep sleep on ESP32-P4\n", pin);
        Serial.println("[ESP32_SLEEP] Only GPIO 0-15 (LP GPIOs) can wake from deep sleep");
#else
        Serial.printf("[ESP32_SLEEP] WARNING: GPIO%d may not support deep sleep wake\n", pin);
#endif
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
    
#if HAS_EXT0_WAKE
    if (cause == ESP_SLEEP_WAKEUP_EXT0) {
        // EXT0 is the DS3231 INT pin
        return _rtc_int_pin;
    }
#endif
    
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

/**
 * @brief Get a string describing the wake cause
 */
const char* sleep_get_wake_cause_string(void) {
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    
    switch (cause) {
        case ESP_SLEEP_WAKEUP_UNDEFINED: return "Power on / reset";
        case ESP_SLEEP_WAKEUP_ALL: return "Unknown";
        case ESP_SLEEP_WAKEUP_EXT0: return "EXT0 (single GPIO)";
        case ESP_SLEEP_WAKEUP_EXT1: return "EXT1 (GPIO mask)";
        case ESP_SLEEP_WAKEUP_TIMER: return "Timer";
        case ESP_SLEEP_WAKEUP_TOUCHPAD: return "Touchpad";
        case ESP_SLEEP_WAKEUP_ULP: return "ULP program";
        case ESP_SLEEP_WAKEUP_GPIO: return "GPIO";
        case ESP_SLEEP_WAKEUP_UART: return "UART";
        case ESP_SLEEP_WAKEUP_WIFI: return "WiFi";
        case ESP_SLEEP_WAKEUP_COCPU: return "Co-CPU";
        case ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG: return "Co-CPU trap";
        case ESP_SLEEP_WAKEUP_BT: return "Bluetooth";
        default: return "Unknown";
    }
}

/**
 * @brief Print detailed sleep/wake information
 */
void sleep_print_info(void) {
    init_rtc_data_if_needed();
    
    Serial.println("\n=== ESP32 Sleep Info ===");
    Serial.printf("  Chip: %s\n", get_chip_name());
    Serial.printf("  Boot count: %lu\n", rtc_data.boot_count);
    Serial.printf("  Total uptime: %lu seconds\n", sleep_get_uptime_seconds());
    Serial.printf("  Wake cause: %s\n", sleep_get_wake_cause_string());
    
    int wake_gpio = sleep_get_wake_gpio();
    if (wake_gpio >= 0) {
        Serial.printf("  Wake GPIO: %d\n", wake_gpio);
    }
    
#if HAS_DS3231
    Serial.printf("  External RTC: %s\n", _rtc_present ? "DS3231 present" : "Not found");
    if (_rtc_present) {
        Serial.printf("  RTC INT pin: GPIO%d", _rtc_int_pin);
        if (_rtc_int_pin >= 0 && !is_valid_wake_gpio(_rtc_int_pin)) {
            Serial.print(" (CANNOT wake - not an LP GPIO!)");
        }
        Serial.println();
    }
#endif

#if CONFIG_IDF_TARGET_ESP32P4
    Serial.println("  Wake GPIOs: GPIO 0-15 only (LP GPIOs)");
    Serial.println("  Wake mode: ext1 (ext0 not supported)");
#elif HAS_EXT0_WAKE && HAS_EXT1_WAKE
    Serial.println("  Wake modes: ext0, ext1, timer");
#endif
    
    Serial.printf("  Configured wake sources: %d\n", rtc_data.wake_gpio_count);
    for (int i = 0; i < rtc_data.wake_gpio_count; i++) {
        if (rtc_data.wake_gpio_pins[i] >= 0) {
            Serial.printf("    Slot %d: GPIO%d (active-%s)\n", 
                          i, rtc_data.wake_gpio_pins[i],
                          rtc_data.wake_gpio_active_high[i] ? "high" : "low");
        }
    }
    
    Serial.println("========================\n");
}

#endif // ESP32 || ARDUINO_ARCH_ESP32

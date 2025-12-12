/**
 * @file sleep_hal.h
 * @brief Platform-independent sleep interface
 * 
 * Automatically includes the correct sleep implementation based on platform:
 * - RP2350: pico_sleep.h
 * - ESP32:  esp32_sleep.h
 */

#ifndef SLEEP_HAL_H
#define SLEEP_HAL_H

#include "platform_hal.h"

#if defined(PLATFORM_RP2350)
    #include "pico_sleep.h"
#elif defined(PLATFORM_ESP32) || defined(PLATFORM_ESP32P4)
    #include "esp32_sleep.h"
#else
    // Provide stub implementations for unknown platforms
    #warning "Unknown platform - sleep functions will be stubs"
    
    static inline bool sleep_init_rtc(int sda, int scl, int intpin) { return false; }
    static inline bool sleep_has_rtc(void) { return false; }
    static inline int sleep_get_rtc_int_pin(void) { return -1; }
    static inline void sleep_goto_dormant_for_ms(uint32_t ms) { delay(ms); }
    static inline void sleep_run_from_lposc(void) { }
    static inline bool sleep_woke_from_deep_sleep(void) { return false; }
    static inline void sleep_clear_wake_flag(void) { }
    static inline void sleep_clear_all_state(void) { }
    static inline uint64_t sleep_get_time_ms(void) { return millis(); }
    static inline void sleep_set_time_ms(uint64_t ms) { }
    static inline uint32_t sleep_get_uptime_seconds(void) { return millis() / 1000; }
    static inline uint64_t sleep_get_corrected_time_ms(void) { return millis(); }
    static inline void sleep_calibrate_drift(uint64_t ms) { }
    static inline int32_t sleep_get_drift_ppm(void) { return 0; }
    static inline void sleep_set_drift_ppm(int32_t ppm) { }
    static inline int sleep_add_gpio_wake_source(int pin, bool high) { return -1; }
    static inline void sleep_clear_gpio_wake_sources(void) { }
    static inline int sleep_get_wake_gpio(void) { return -1; }
#endif

#endif // SLEEP_HAL_H

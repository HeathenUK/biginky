/**
 * @file esp32_sleep.h
 * @brief Deep Sleep functionality for ESP32 (including ESP32-P4)
 * 
 * Uses ESP32's deep sleep and RTC memory for implementation.
 * 
 * Features:
 * - Timer-based wake from deep sleep
 * - GPIO-based wake (for RTC alarm or button)
 * - Persistent data across deep sleep using RTC memory
 * - Uses ESP32 internal RTC only (DS3231 support removed)
 */

#ifndef _ESP32_SLEEP_H_
#define _ESP32_SLEEP_H_

#include <Arduino.h>

#ifdef __cplusplus
extern "C" {
#endif

// ========================================================================
// RTC functions (DS3231 support removed - using ESP32 internal RTC only)
// ========================================================================

/**
 * @brief Initialize RTC (DS3231 support removed - always returns false)
 * @param sda_pin Unused (DS3231 removed)
 * @param scl_pin Unused (DS3231 removed)
 * @param int_pin Unused (DS3231 removed)
 * @return Always returns false (no external RTC)
 */
bool sleep_init_rtc(int sda_pin, int scl_pin, int int_pin);

/**
 * @brief Check if external RTC is available
 * @return Always returns false (DS3231 support removed)
 */
bool sleep_has_rtc(void);

/**
 * @brief Get the GPIO pin used for RTC interrupt/wake
 * @return Always returns -1 (DS3231 support removed)
 */
int sleep_get_rtc_int_pin(void);

/**
 * @brief Send system to deep sleep for a specified duration
 * 
 * The system will enter deep sleep and wake up after the specified
 * number of milliseconds.
 * 
 * @param delay_ms Duration to sleep in milliseconds
 */
void sleep_goto_dormant_for_ms(uint32_t delay_ms);

/**
 * @brief Prepare for deep sleep (ESP32 compatibility stub)
 * 
 * On ESP32, this is not needed as the RTC handles timing.
 */
static inline void sleep_run_from_lposc(void) {
    // No-op on ESP32 - RTC handles this automatically
}

/**
 * @brief Check if we just woke from deep sleep
 * 
 * @return true if we woke from deep sleep, false otherwise
 */
bool sleep_woke_from_deep_sleep(void);

/**
 * @brief Clear the wake-from-deep-sleep flag
 */
void sleep_clear_wake_flag(void);

/**
 * @brief Clear all sleep-related state (for testing)
 */
void sleep_clear_all_state(void);

/**
 * @brief Get the current RTC time in milliseconds
 * 
 * Uses ESP32's internal RTC only (DS3231 support removed).
 * 
 * @return Current time in milliseconds since epoch
 */
uint64_t sleep_get_time_ms(void);

/**
 * @brief Set the RTC time in milliseconds
 * 
 * @param time_ms Time to set in milliseconds since epoch
 */
void sleep_set_time_ms(uint64_t time_ms);

/**
 * @brief Get uptime in seconds
 * 
 * @return Seconds since boot (persists across deep sleep)
 */
uint32_t sleep_get_uptime_seconds(void);

/**
 * @brief Get drift-compensated time in milliseconds
 * 
 * @return Corrected time in milliseconds
 */
uint64_t sleep_get_corrected_time_ms(void);

/**
 * @brief Update drift calibration based on NTP sync
 * 
 * @param accurate_time_ms The accurate (NTP) time in milliseconds
 */
void sleep_calibrate_drift(uint64_t accurate_time_ms);

/**
 * @brief Get the current drift correction in parts-per-million
 * 
 * @return Drift in PPM
 */
int32_t sleep_get_drift_ppm(void);

/**
 * @brief Set a known drift correction in parts-per-million
 * 
 * @param drift_ppm Drift correction value
 */
void sleep_set_drift_ppm(int32_t drift_ppm);

/**
 * @brief Add a GPIO pin as an additional wake source
 * 
 * @param pin GPIO pin number
 * @param active_high true for active-high, false for active-low
 * @return slot number used, or -1 if no slots available
 */
int sleep_add_gpio_wake_source(int pin, bool active_high);

/**
 * @brief Clear all GPIO wake sources
 */
void sleep_clear_gpio_wake_sources(void);

/**
 * @brief Check which wake source triggered the wake
 * 
 * @return GPIO pin number that triggered wake, or -1 if timer/unknown
 */
int sleep_get_wake_gpio(void);

/**
 * @brief Print sleep system status information
 */
void sleep_print_info(void);

#ifdef __cplusplus
}
#endif

#endif // _ESP32_SLEEP_H_

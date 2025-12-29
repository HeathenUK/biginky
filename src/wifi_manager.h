/**
 * @file wifi_manager.h
 * @brief WiFi connection and NTP synchronization manager
 * 
 * Provides functions for:
 * - WiFi credential management (load, save, clear)
 * - Persistent WiFi connection with retries
 * - NTP time synchronization
 * - Time validation
 * - Configuration mode for interactive credential setup
 * 
 * Extracted from main_esp32p4_test.cpp as part of Priority 1 refactoring.
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>

/**
 * Load WiFi credentials from NVS or RTC cache
 * Returns true if credentials were loaded successfully, false if NVS failed or credentials missing
 * Optimized: Uses RTC memory cache to avoid NVS reads on every cycle (saves ~50-100ms)
 */
bool wifiLoadCredentials();

/**
 * Save WiFi credentials to NVS
 */
void wifiSaveCredentials();

/**
 * Clear WiFi credentials from NVS
 */
void wifiClearCredentials();

/**
 * Enter interactive configuration mode for WiFi credentials
 * Blocks until credentials are configured via serial input
 */
void enterConfigMode();

/**
 * Persistent WiFi connection function - keeps trying until connected
 * @param maxRetries Maximum number of connection attempts
 * @param timeoutPerAttemptMs Timeout for each connection attempt in milliseconds
 * @param required If true, will keep trying indefinitely until connected
 * @return true if connected, false only if credentials are missing or not required
 */
bool wifiConnectPersistent(int maxRetries = 10, uint32_t timeoutPerAttemptMs = 20000, bool required = true);

/**
 * Perform NTP time synchronization
 * @param timeout_ms Maximum time to wait for NTP sync (0 = use default 30s per attempt)
 * @return true if sync successful, false otherwise
 */
bool performNtpSync(uint32_t timeout_ms = 30000);

/**
 * Ensure system time is valid (after 2020-01-01)
 * If time is invalid, attempts WiFi connection and NTP sync
 * @param timeout_ms Maximum time to wait for sync (0 = use default 60s)
 * @param forceSync If true, force NTP sync even if time appears valid
 * @return true if time is valid, false otherwise
 */
bool ensureTimeValid(uint32_t timeout_ms = 20000, bool forceSync = false);

#endif // WIFI_MANAGER_H



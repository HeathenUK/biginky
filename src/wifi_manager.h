/**
 * @file wifi_manager.h
 * @brief WiFi connection and NTP synchronization manager
 * 
 * Provides functions for:
 * - WiFi credential management (load, save, clear)
 * - Multi-network support with RSSI-based selection
 * - Hidden network support
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

// Maximum number of WiFi networks that can be stored
#define WIFI_MAX_NETWORKS 8

/**
 * WiFi network configuration
 */
struct WiFiNetwork {
    char ssid[33];      // Max SSID length is 32 + null
    char psk[65];       // Max PSK length is 64 + null
    bool hidden;        // True if hidden network (must probe)
    bool enabled;       // Can disable without deleting
};

/**
 * Get the list of configured WiFi networks
 * @param networks Array to fill with network configs
 * @param maxNetworks Maximum number of networks to return
 * @return Number of networks loaded
 */
int wifiGetNetworks(WiFiNetwork* networks, int maxNetworks);

/**
 * Add or update a WiFi network
 * @param ssid Network SSID
 * @param psk Network password (empty for open networks)
 * @param hidden True if this is a hidden network
 * @param enabled True if network should be used for connections
 * @return true if added/updated successfully
 */
bool wifiAddNetwork(const char* ssid, const char* psk, bool hidden = false, bool enabled = true);

/**
 * Remove a WiFi network by SSID
 * @param ssid Network SSID to remove
 * @return true if removed, false if not found
 */
bool wifiRemoveNetwork(const char* ssid);

/**
 * Get all networks as JSON array string
 * @return JSON array of network objects (ssid, hidden, enabled - PSK excluded for security)
 */
String wifiGetNetworksJSON();

/**
 * Get all networks as JSON array string including PSKs (for config export)
 * @return JSON array of network objects including PSKs
 */
String wifiGetNetworksJSONWithPSK();

/**
 * Load networks from JSON array (for config import)
 * @param json JSON array of network objects
 * @return Number of networks imported
 */
int wifiLoadNetworksFromJSON(const char* json);

/**
 * Connect to the best available network (strongest signal)
 * Scans for visible networks, matches against known networks,
 * connects to strongest signal first. Falls back to hidden networks.
 * @param timeoutPerAttemptMs Timeout for each connection attempt
 * @return true if connected to any network
 */
bool wifiConnectBest(uint32_t timeoutPerAttemptMs = 15000);

/**
 * Print list of configured networks to Serial
 */
void wifiPrintNetworks();

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
 * Start AP mode with captive portal for WiFi configuration
 * Creates a WiFi access point "BigInky-Setup" that users can connect to
 * Serves a simple web page to configure WiFi credentials
 * @param timeoutMinutes How long to run AP mode before giving up (0 = indefinite)
 * @return true if WiFi was configured, false if timed out or failed
 */
bool wifiStartAPConfigMode(uint8_t timeoutMinutes = 5);

/**
 * Clear all stored networks
 */
void wifiClearNetworks();

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



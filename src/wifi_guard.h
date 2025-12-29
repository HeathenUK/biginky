/**
 * @file wifi_guard.h
 * @brief RAII wrapper for WiFi connections to ensure proper connect/disconnect pairing
 * 
 * This wrapper ensures that wifiConnectPersistent() and WiFi.disconnect() are always
 * called in pairs, with appropriate delays for connection establishment and
 * operation completion.
 * 
 * Usage:
 *   {
 *       WiFiGuard guard(10, 30000, true);  // 10 retries, 30s timeout, required
 *       if (!guard.isConnected()) {
 *           // Handle error
 *           return;
 *       }
 *       // Do WiFi work (NTP sync, MQTT, etc.)
 *       performNtpSync();
 *   } // Automatically calls WiFi.disconnect() here
 * 
 * The wrapper automatically:
 * - Calls wifiConnectPersistent() in constructor
 * - Calls WiFi.disconnect() in destructor
 * - Waits appropriate delays for disconnect to complete
 */

#ifndef WIFI_GUARD_H
#define WIFI_GUARD_H

#include <Arduino.h>
#include <WiFi.h>

// Forward declarations
bool wifiConnectPersistent(int maxRetries, uint32_t timeoutPerAttemptMs, bool required);

/**
 * RAII wrapper for WiFi connections
 */
class WiFiGuard {
public:
    /**
     * Constructor - connects to WiFi with specified parameters
     * @param maxRetries Maximum number of connection retries (default 10)
     * @param timeoutPerAttemptMs Timeout per connection attempt in milliseconds (default 20000)
     * @param required Whether connection is required (default true)
     * @param disconnectDelayMs Delay after disconnect in milliseconds (default 100)
     */
    WiFiGuard(int maxRetries = 10, uint32_t timeoutPerAttemptMs = 20000, bool required = true, uint32_t disconnectDelayMs = 100)
        : connected_(false)
        , maxRetries_(maxRetries)
        , timeoutPerAttemptMs_(timeoutPerAttemptMs)
        , required_(required)
        , disconnectDelayMs_(disconnectDelayMs)
    {
        connected_ = wifiConnectPersistent(maxRetries_, timeoutPerAttemptMs_, required_);
    }

    /**
     * Destructor - automatically disconnects if connected
     */
    ~WiFiGuard() {
        if (connected_) {
            WiFi.disconnect();
            delay(disconnectDelayMs_);  // Allow time for disconnect to complete
        }
    }

    // Delete copy constructor and assignment operator (non-copyable)
    WiFiGuard(const WiFiGuard&) = delete;
    WiFiGuard& operator=(const WiFiGuard&) = delete;

    /**
     * Check if WiFi connection was successful
     * @return true if connected, false otherwise
     */
    bool isConnected() const {
        return connected_;
    }

    /**
     * Manually disconnect early (normally handled by destructor)
     * Useful if you need to disconnect before the guard goes out of scope
     */
    void disconnect() {
        if (connected_) {
            WiFi.disconnect();
            delay(disconnectDelayMs_);  // Allow time for disconnect to complete
            connected_ = false;
        }
    }

private:
    bool connected_;                    // True if WiFi connection succeeded
    int maxRetries_;                    // Maximum connection retries
    uint32_t timeoutPerAttemptMs_;     // Timeout per connection attempt (ms)
    bool required_;                     // Whether connection is required
    uint32_t disconnectDelayMs_;        // Delay after disconnect (ms)
};

#endif // WIFI_GUARD_H


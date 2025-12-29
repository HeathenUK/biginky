/**
 * @file mqtt_guard.h
 * @brief RAII wrapper for MQTT connections to ensure proper connect/disconnect pairing
 * 
 * This wrapper ensures that mqttConnect() and mqttDisconnect() are always
 * called in pairs, with appropriate delays for connection establishment and
 * operation completion.
 * 
 * Usage:
 *   {
 *       MQTTGuard guard;
 *       if (!guard.isConnected()) {
 *           // Handle error
 *           return;
 *       }
 *       // Do MQTT work (publish, check messages, etc.)
 *       publishMQTTStatus();
 *   } // Automatically calls mqttDisconnect() here
 * 
 * The wrapper automatically:
 * - Calls mqttConnect() in constructor
 * - Waits 1000ms for connection to establish
 * - Calls mqttDisconnect() in destructor
 * - Waits 200ms after operations (before disconnect) and 100ms after disconnect
 */

#ifndef MQTT_GUARD_H
#define MQTT_GUARD_H

#include <Arduino.h>

// Forward declarations
bool mqttConnect();
void mqttDisconnect();

/**
 * RAII wrapper for MQTT connections
 */
class MQTTGuard {
public:
    /**
     * Constructor - connects to MQTT and waits for connection
     * @param connectDelayMs Delay after connect (default 1000ms)
     */
    MQTTGuard(uint32_t connectDelayMs = 1000)
        : connected_(false)
        , connectDelayMs_(connectDelayMs)
    {
        connected_ = mqttConnect();
        if (connected_) {
            delay(connectDelayMs_);  // Wait for connection and subscriptions
        }
    }

    /**
     * Destructor - automatically disconnects if connected
     */
    ~MQTTGuard() {
        if (connected_) {
            delay(200);  // Allow time for operations to complete
            mqttDisconnect();
            delay(100);  // Allow time for disconnect to complete
        }
    }

    // Delete copy constructor and assignment operator (non-copyable)
    MQTTGuard(const MQTTGuard&) = delete;
    MQTTGuard& operator=(const MQTTGuard&) = delete;

    /**
     * Check if MQTT connection was successful
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
            delay(200);  // Allow time for operations to complete
            mqttDisconnect();
            delay(100);  // Allow time for disconnect to complete
            connected_ = false;
        }
    }

private:
    bool connected_;           // True if MQTT connection succeeded
    uint32_t connectDelayMs_;  // Delay after connect (ms)
};

#endif // MQTT_GUARD_H


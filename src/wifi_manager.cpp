/**
 * @file wifi_manager.cpp
 * @brief WiFi connection and NTP synchronization manager implementation
 * 
 * Extracted from main_esp32p4_test.cpp as part of Priority 1 refactoring.
 * WiFi is always enabled - WIFI_ENABLED guards removed.
 */

#include "wifi_manager.h"
#include "nvs_guard.h"
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <Preferences.h>
#include <string.h>

// External references to global state in main file
extern char wifiSSID[33];
extern char wifiPSK[65];
extern Preferences wifiPrefs;

// External references to RTC memory cache (defined in main file)
extern char cachedWifiSSID[33];
extern char cachedWifiPSK[65];
extern bool wifiCredentialsCached;

bool performNtpSync(uint32_t timeout_ms) {
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    
    // NTP sync with retries - be persistent like WiFi connection
    const int maxNtpRetries = 5;
    const uint32_t ntpTimeoutPerAttempt = 30000;  // 30 seconds per attempt
    
    for (int retry = 0; retry < maxNtpRetries; retry++) {
        if (retry > 0) {
            Serial.printf("NTP sync retry %d of %d...\n", retry + 1, maxNtpRetries);
            delay(2000);  // Brief delay between retries
        }
        
        Serial.print("Syncing NTP");
        uint32_t start = millis();
        time_t now = time(nullptr);
        
        uint32_t attemptTimeout = ntpTimeoutPerAttempt;
        if (timeout_ms > 0 && timeout_ms < attemptTimeout) {
            attemptTimeout = timeout_ms;
        }
        
        while ((millis() - start) < attemptTimeout) {
            now = time(nullptr);
            if (now > 1577836800) {
                struct tm tm_utc;
                gmtime_r(&now, &tm_utc);
                char buf[32];
                strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", &tm_utc);
                Serial.printf("\nNTP sync OK: %s\n", buf);
                return true;
            }
            delay(500);
            if ((millis() - start) % 5000 == 0) {
                Serial.print(".");
            }
            
            // Check overall timeout
            if (timeout_ms > 0 && (millis() - start) >= timeout_ms) {
                break;
            }
        }
        
        Serial.println();
        Serial.printf("NTP sync attempt %d timed out after %lu seconds\n", retry + 1, (millis() - start) / 1000);
        
        // If WiFi is still connected, try reconfiguring NTP
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("WiFi still connected, reconfiguring NTP...");
            configTime(0, 0, "pool.ntp.org", "time.google.com");
        } else {
            Serial.println("WiFi disconnected during NTP sync - caller should handle reconnection");
            // Don't try to reconnect here - this function assumes WiFi is already connected
            // If WiFi disconnects, that's a problem for the caller to handle
        }
    }
    
    Serial.println("NTP sync failed after all retries");
    return false;
}

bool ensureTimeValid(uint32_t timeout_ms, bool forceSync) {
    time_t now = time(nullptr);
    if (!forceSync && now > 1577836800) {  // 2020-01-01
        return true;
    }

    // If timeout is 0, use a default reasonable timeout
    if (timeout_ms == 0) {
        timeout_ms = 60000;  // Default 60 seconds
    }
    
    uint32_t overallStart = millis();

    // Load creds (if any) directly from NVS and try NTP.
    NVSGuard guard("wifi", true);  // read-only
    if (!guard.isOpen()) {
        Serial.println("\n========================================");
        Serial.println("ERROR: Failed to open NVS for WiFi credentials!");
        Serial.println("NVS may be corrupted or not initialized.");
        Serial.println("Error: nvs_open failed (NOT_FOUND or other error)");
        Serial.println("========================================");
        Serial.println("Cannot open NVS - configuration mode needed.");
        Serial.println("This function cannot enter config mode (called from task context).");
        Serial.println("Returning false - caller should handle config mode.");
        // Don't call enterConfigMode() here - it's called from a task context
        // The caller (auto_cycle_task) will set flag and exit task
        return false;
    }
    
    String ssid = guard.get().getString("ssid", "");
    String psk = guard.get().getString("psk", "");
    // guard automatically calls end() in destructor

    if (ssid.length() == 0) {
        Serial.println("\n========================================");
        Serial.println("ERROR: No WiFi credentials found in NVS!");
        Serial.println("========================================");
        Serial.println("Configuration mode needed.");
        Serial.println("This function cannot enter config mode (called from task context).");
        Serial.println("Returning false - caller should handle config mode.");
        // Don't call enterConfigMode() here - it's called from a task context
        // The caller (auto_cycle_task) will set flag and exit task
        return false;
    }

    Serial.printf("Time invalid; syncing NTP via WiFi SSID '%s'...\n", ssid.c_str());
    
    // Configure WiFi for better connection reliability and speed
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);  // Disable WiFi sleep for better connection stability
    WiFi.setTxPower(WIFI_POWER_19_5dBm);  // Maximum power for better range and faster connection
    WiFi.setAutoReconnect(true);  // Enable auto-reconnect
    
    // Additional optimizations for faster connection:
    // - TX power already at maximum (19.5 dBm) for best signal strength
    // - WiFi sleep disabled for immediate connection attempts
    // - Auto-reconnect enabled for persistent connections
    // - Connection timeout reduced to 20s for faster retries
    
    // Try connecting with multiple retries, but respect overall timeout
    int maxRetries = 15;
    bool connected = false;
    
    for (int retry = 0; retry < maxRetries && !connected; retry++) {
        // Check overall timeout
        if ((millis() - overallStart) > timeout_ms) {
            Serial.println("Overall timeout exceeded during WiFi connection attempts.");
            break;
        }
        
        if (retry > 0) {
            Serial.printf("WiFi connection attempt %d/%d...\n", retry + 1, maxRetries);
            delay(2000);  // Wait 2 seconds before retry
            // Only disconnect if not already connected
            if (WiFi.status() != WL_CONNECTED) {
                WiFi.disconnect();
                delay(500);
            }
        }
        
        Serial.print("Connecting");
        // Only call begin if not already connected
        if (WiFi.status() != WL_CONNECTED) {
            WiFi.begin(ssid.c_str(), psk.c_str());
        }

        uint32_t start = millis();
        // Reduced timeout for faster failure and retry (20s instead of 30s)
        // This allows faster retries when connection is slow, improving overall connection time
        uint32_t timeoutPerAttempt = 20000;  // 20 seconds per attempt (reduced from 30s)
        // Reduce timeout if we're close to overall timeout
        uint32_t remainingTime = timeout_ms - (millis() - overallStart);
        if (remainingTime < timeoutPerAttempt) {
            timeoutPerAttempt = remainingTime;
        }
        
        while (WiFi.status() != WL_CONNECTED && (millis() - start < timeoutPerAttempt)) {
            // Check overall timeout
            if ((millis() - overallStart) > timeout_ms) {
                Serial.println("\nOverall timeout exceeded during WiFi connection.");
                break;
            }
            
            delay(500);
            Serial.print(".");
            
            // Show progress every 5 seconds
            if ((millis() - start) % 5000 < 500) {
                Serial.printf(" [%lu s]", (millis() - start) / 1000);
            }
        }
        Serial.println();
        
        if (WiFi.status() == WL_CONNECTED) {
            connected = true;
            Serial.println("WiFi connected!");
        } else {
            Serial.printf("Connection attempt %d failed (status: %d)\n", retry + 1, WiFi.status());
        }
    }
    
    if (!connected) {
        Serial.println("WiFi connect failed after all retries; cannot NTP sync.");
        // Don't retry indefinitely - respect timeout
        Serial.println("WiFi connection failed, giving up NTP sync.");
        return false;
    }

    configTime(0, 0, "pool.ntp.org", "time.google.com");

    // NTP sync with retries - be persistent like WiFi connection
    const int maxNtpRetries = 5;
    const uint32_t ntpTimeoutPerAttempt = 30000;  // 30 seconds per attempt
    
    for (int retry = 0; retry < maxNtpRetries; retry++) {
        if (retry > 0) {
            Serial.printf("NTP sync retry %d of %d...\n", retry + 1, maxNtpRetries);
            delay(2000);  // Brief delay between retries
        }
        
        Serial.print("Syncing NTP");
        uint32_t start = millis();
        
        while ((millis() - start) < ntpTimeoutPerAttempt) {
            now = time(nullptr);
            if (now > 1577836800) {
                struct tm tm_utc;
                gmtime_r(&now, &tm_utc);
                char buf[32];
                strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", &tm_utc);
                Serial.printf("\nNTP sync OK: %s\n", buf);
                // Keep WiFi connected - will be disconnected before deep sleep
                return true;
            }
            delay(500);
            if ((millis() - start) % 5000 == 0) {
                Serial.print(".");
            }
        }
        
        Serial.println();
        Serial.printf("NTP sync attempt %d timed out after %lu seconds\n", retry + 1, ntpTimeoutPerAttempt / 1000);
        
        // If WiFi is still connected, try reconfiguring NTP
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("WiFi still connected, reconfiguring NTP...");
            configTime(0, 0, "pool.ntp.org", "time.google.com");
        } else {
            Serial.println("WiFi disconnected during NTP sync, will retry WiFi connection");
            // Try to reconnect WiFi before continuing with NTP retries
            WiFi.disconnect();
            delay(1000);
            WiFi.begin(ssid.c_str(), psk.c_str());
            
            uint32_t reconnectStart = millis();
            uint32_t reconnectTimeout = 20000;  // 20 seconds to reconnect
            while (WiFi.status() != WL_CONNECTED && (millis() - reconnectStart < reconnectTimeout)) {
                delay(500);
                Serial.print(".");
                
                // Check overall timeout
                if ((millis() - overallStart) > timeout_ms) {
                    Serial.println("\nOverall timeout exceeded during WiFi reconnection.");
                    return false;
                }
            }
            Serial.println();
            
            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("WiFi reconnected, reconfiguring NTP...");
                configTime(0, 0, "pool.ntp.org", "time.google.com");
            } else {
                Serial.println("WiFi reconnection failed, will retry in next loop iteration");
                // Continue to next retry - will try to reconnect WiFi again
            }
        }
    }
    
    // If we've exhausted retries but WiFi is still connected, try a few more times
    // but respect the overall timeout to prevent infinite loops
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("NTP sync failed after all retries, but WiFi is connected.");
        Serial.println("Will try a few more times (respecting timeout)...");
        
        uint32_t additionalStart = millis();
        int additionalRetries = 3;  // Limit additional retries
        
        for (int extraRetry = 0; extraRetry < additionalRetries; extraRetry++) {
            // Check if we've exceeded the overall timeout
            if (timeout_ms > 0 && (millis() - overallStart) > timeout_ms) {
                Serial.println("Overall timeout exceeded, giving up NTP sync.");
                break;
            }
            
            Serial.printf("Additional NTP sync retry %d of %d...\n", extraRetry + 1, additionalRetries);
            configTime(0, 0, "pool.ntp.org", "time.google.com");
            delay(2000);
            
            uint32_t start = millis();
            while ((millis() - start) < ntpTimeoutPerAttempt) {
                // Check overall timeout
                if (timeout_ms > 0 && (millis() - overallStart) > timeout_ms) {
                    Serial.println("Overall timeout exceeded during NTP sync.");
                    return false;
                }
                
                now = time(nullptr);
                if (now > 1577836800) {
                    struct tm tm_utc;
                    gmtime_r(&now, &tm_utc);
                    char buf[32];
                    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", &tm_utc);
                    Serial.printf("NTP sync OK: %s\n", buf);
                    return true;
                }
                delay(500);
            }
            Serial.println("NTP sync retry timed out, trying again...");
        }
    }

    // If WiFi disconnected and we couldn't reconnect, try one more time
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi disconnected; attempting final reconnection...");
        WiFi.disconnect();
        delay(1000);
        WiFi.begin(ssid.c_str(), psk.c_str());
        
        uint32_t finalReconnectStart = millis();
        uint32_t finalReconnectTimeout = 15000;  // 15 seconds for final attempt
        while (WiFi.status() != WL_CONNECTED && (millis() - finalReconnectStart < finalReconnectTimeout)) {
            delay(500);
            Serial.print(".");
            
            // Check overall timeout
            if ((millis() - overallStart) > timeout_ms) {
                Serial.println("\nOverall timeout exceeded during final WiFi reconnection.");
                Serial.println("NTP sync failed; WiFi connection lost.");
                return false;
            }
        }
        Serial.println();

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("WiFi reconnected on final attempt, trying NTP sync one more time...");
            configTime(0, 0, "pool.ntp.org", "time.google.com");
            delay(2000);
            
            uint32_t finalNtpStart = millis();
            uint32_t finalNtpTimeout = 20000;  // 20 seconds for final NTP attempt
            while ((millis() - finalNtpStart) < finalNtpTimeout) {
                // Check overall timeout
                if ((millis() - overallStart) > timeout_ms) {
                    Serial.println("Overall timeout exceeded during final NTP sync.");
                    return false;
                }
                
                now = time(nullptr);
                if (now > 1577836800) {
                    struct tm tm_utc;
                    gmtime_r(&now, &tm_utc);
                    char buf[32];
                    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", &tm_utc);
                    Serial.printf("NTP sync OK: %s\n", buf);
                    return true;
                }
                delay(500);
            }
        }
    }

    Serial.println("NTP sync failed after all attempts.");
    // Keep WiFi connected if it still is - will be disconnected before deep sleep
    return false;
}

void enterConfigMode() {
    Serial.println("\n\n========================================");
    Serial.println("    CONFIGURATION MODE");
    Serial.println("========================================");
    Serial.println("WiFi credentials are required to continue.");
    Serial.println("Please enter your WiFi network details below.");
    Serial.println("========================================\n");
    
    while (true) {
        // Prompt for SSID
        Serial.print("WiFi SSID: ");
        Serial.flush();
        
        // Wait for input with timeout
        uint32_t start = millis();
        String ssid = "";
        while ((millis() - start) < 60000) {  // 60 second timeout
            if (Serial.available()) {
                ssid = Serial.readStringUntil('\n');
                ssid.trim();
                break;
            }
            delay(10);
        }
        
        if (ssid.length() == 0) {
            Serial.println("\nTimeout or empty input. Please try again.");
            continue;
        }
        
        if (ssid == "clear") {
            wifiClearCredentials();
            Serial.println("Credentials cleared. Please enter new credentials.");
            continue;
        }
        
        // Prompt for password
        Serial.print("WiFi Password (or press Enter for open network): ");
        Serial.flush();
        
        start = millis();
        String psk = "";
        while ((millis() - start) < 60000) {  // 60 second timeout
            if (Serial.available()) {
                psk = Serial.readStringUntil('\n');
                psk.trim();
                break;
            }
            delay(10);
        }
        
        // Save credentials
        strncpy(wifiSSID, ssid.c_str(), sizeof(wifiSSID) - 1);
        wifiSSID[sizeof(wifiSSID) - 1] = '\0';
        strncpy(wifiPSK, psk.c_str(), sizeof(wifiPSK) - 1);
        wifiPSK[sizeof(wifiPSK) - 1] = '\0';
        
        // Save to NVS
        wifiPrefs.begin("wifi", false);  // Read-write
        wifiPrefs.putString("ssid", wifiSSID);
        wifiPrefs.putString("psk", wifiPSK);
        wifiPrefs.end();
        
        Serial.printf("\nCredentials saved: SSID='%s'\n", wifiSSID);
        Serial.println("Verifying credentials were saved...");
        
        // Verify they were saved
        wifiPrefs.begin("wifi", true);  // Read-only
        String savedSSID = wifiPrefs.getString("ssid", "");
        wifiPrefs.end();
        
        if (savedSSID.length() > 0 && savedSSID == wifiSSID) {
            Serial.println("✓ Credentials verified and saved successfully!");
            Serial.println("\n========================================");
            Serial.println("Configuration complete!");
            Serial.println("========================================\n");
            return;  // Exit config mode
        } else {
            Serial.println("✗ ERROR: Failed to verify saved credentials!");
            Serial.println("Please try again.\n");
            continue;  // Loop back to try again
        }
    }
}

bool wifiLoadCredentials() {
    // Clear credentials first
    wifiSSID[0] = '\0';
    wifiPSK[0] = '\0';
    
    // Check if credentials are cached in RTC memory (survives deep sleep)
    if (wifiCredentialsCached && strlen(cachedWifiSSID) > 0) {
        strncpy(wifiSSID, cachedWifiSSID, sizeof(wifiSSID) - 1);
        wifiSSID[sizeof(wifiSSID) - 1] = '\0';
        strncpy(wifiPSK, cachedWifiPSK, sizeof(wifiPSK) - 1);
        wifiPSK[sizeof(wifiPSK) - 1] = '\0';
        Serial.printf("Loaded WiFi credentials from cache: %s\n", wifiSSID);
        return true;
    }
    
    // Not cached - load from NVS
    // Try to open NVS namespace
    if (!wifiPrefs.begin("wifi", true)) {  // Read-only
        Serial.println("\n========================================");
        Serial.println("ERROR: Failed to open NVS for WiFi credentials!");
        Serial.println("NVS may be corrupted or not initialized.");
        Serial.println("========================================");
        Serial.println("\n>>> CONFIGURATION REQUIRED <<<");
        Serial.println("Please configure WiFi credentials using:");
        Serial.println("  Command 'W' - Set WiFi credentials");
        Serial.println("\nDevice will wait for configuration...");
        return false;
    }
    
    String ssid = wifiPrefs.getString("ssid", "");
    String psk = wifiPrefs.getString("psk", "");
    wifiPrefs.end();
    
    if (ssid.length() > 0) {
        strncpy(wifiSSID, ssid.c_str(), sizeof(wifiSSID) - 1);
        wifiSSID[sizeof(wifiSSID) - 1] = '\0';  // Ensure null termination
        strncpy(wifiPSK, psk.c_str(), sizeof(wifiPSK) - 1);
        wifiPSK[sizeof(wifiPSK) - 1] = '\0';  // Ensure null termination
        
        // Cache credentials in RTC memory for next cycle (survives deep sleep)
        strncpy(cachedWifiSSID, wifiSSID, sizeof(cachedWifiSSID) - 1);
        cachedWifiSSID[sizeof(cachedWifiSSID) - 1] = '\0';
        strncpy(cachedWifiPSK, wifiPSK, sizeof(cachedWifiPSK) - 1);
        cachedWifiPSK[sizeof(cachedWifiPSK) - 1] = '\0';
        wifiCredentialsCached = true;
        
        Serial.printf("Loaded WiFi credentials from NVS (cached): %s\n", wifiSSID);
        return true;
    } else {
        // Configuration mode needed, but this function is called from task context
        // Return false - caller should handle config mode by setting flag and exiting task
        Serial.println("Configuration mode needed.");
        Serial.println("This function cannot enter config mode (called from task context).");
        Serial.println("Returning false - caller should handle config mode.");
        return false;
    }
}

bool wifiConnectPersistent(int maxRetries, uint32_t timeoutPerAttemptMs, bool required) {
    if (strlen(wifiSSID) == 0) {
        Serial.println("No WiFi credentials configured");
        return false;
    }
    
    Serial.printf("Connecting to WiFi: %s (persistent mode)\n", wifiSSID);
    
    // Configure WiFi for better connection reliability and speed
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);  // Disable WiFi sleep for better connection stability
    WiFi.setTxPower(WIFI_POWER_19_5dBm);  // Maximum power for better range and faster connection
    WiFi.setAutoReconnect(true);  // Enable auto-reconnect
    
    // Additional optimizations for faster connection:
    // - TX power already at maximum (19.5 dBm) for best signal strength
    // - WiFi sleep disabled for immediate connection attempts
    // - Auto-reconnect enabled for persistent connections
    // - Connection timeout reduced to 20s for faster retries
    
    // Try connecting with multiple retries
    for (int retry = 0; retry < maxRetries; retry++) {
        if (retry > 0) {
            Serial.printf("WiFi connection attempt %d/%d...\n", retry + 1, maxRetries);
            delay(2000);  // Wait 2 seconds before retry
            // Only disconnect if we're not already connected
            if (WiFi.status() != WL_CONNECTED) {
                WiFi.disconnect();
                delay(500);
            }
        }
        
        Serial.print("Connecting");
        // Only call begin if not already connected
        if (WiFi.status() != WL_CONNECTED) {
            WiFi.begin(wifiSSID, wifiPSK);
        }
        
        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - start < timeoutPerAttemptMs)) {
            delay(500);
            Serial.print(".");
            
            // Show progress every 5 seconds
            if ((millis() - start) % 5000 < 500) {
                Serial.printf(" [%lu s]", (millis() - start) / 1000);
            }
        }
        Serial.println();
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("WiFi connected!");
            Serial.printf("  IP: %s\n", WiFi.localIP().toString().c_str());
            Serial.printf("  RSSI: %d dBm\n", WiFi.RSSI());
            Serial.printf("  Channel: %d\n", WiFi.channel());
            return true;
        } else {
            Serial.printf("Connection attempt %d failed (status: %d)\n", retry + 1, WiFi.status());
        }
    }
    
    // All retries exhausted
    if (required) {
        Serial.println("ERROR: WiFi connection failed after all retries - this is required, will keep trying...");
        // If required, keep trying indefinitely
        while (WiFi.status() != WL_CONNECTED) {
            Serial.println("Retrying WiFi connection (required)...");
            delay(5000);  // Wait 5 seconds before retry
            // Only disconnect if not already connected
            if (WiFi.status() != WL_CONNECTED) {
                WiFi.disconnect();
                delay(500);
                WiFi.begin(wifiSSID, wifiPSK);
            }
            
            uint32_t start = millis();
            while (WiFi.status() != WL_CONNECTED && (millis() - start < timeoutPerAttemptMs)) {
                delay(500);
                Serial.print(".");
            }
            Serial.println();
            
            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("WiFi connected after persistent retry!");
                Serial.printf("  IP: %s\n", WiFi.localIP().toString().c_str());
                Serial.printf("  RSSI: %d dBm\n", WiFi.RSSI());
                Serial.printf("  Channel: %d\n", WiFi.channel());
                return true;
            }
        }
    } else {
        Serial.println("WiFi connection failed after all retries");
        return false;
    }
    
    return true;  // Should never reach here, but satisfy compiler
}

void wifiSaveCredentials() {
    wifiPrefs.begin("wifi", false);  // Read-write
    wifiPrefs.putString("ssid", wifiSSID);
    wifiPrefs.putString("psk", wifiPSK);
    wifiPrefs.end();
    
    // Update RTC cache to avoid NVS reads on next cycle
    strncpy(cachedWifiSSID, wifiSSID, sizeof(cachedWifiSSID) - 1);
    cachedWifiSSID[sizeof(cachedWifiSSID) - 1] = '\0';
    strncpy(cachedWifiPSK, wifiPSK, sizeof(cachedWifiPSK) - 1);
    cachedWifiPSK[sizeof(cachedWifiPSK) - 1] = '\0';
    wifiCredentialsCached = true;
    
    Serial.println("WiFi credentials saved to NVS and cached");
}

void wifiClearCredentials() {
    wifiPrefs.begin("wifi", false);
    wifiPrefs.clear();
    wifiPrefs.end();
    wifiSSID[0] = '\0';
    wifiPSK[0] = '\0';
    
    // Clear RTC cache as well
    cachedWifiSSID[0] = '\0';
    cachedWifiPSK[0] = '\0';
    wifiCredentialsCached = false;
    
    Serial.println("WiFi credentials cleared from NVS and cache");
}



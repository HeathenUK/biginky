/**
 * @file wifi_manager.cpp
 * @brief WiFi connection and NTP synchronization manager implementation
 * 
 * Extracted from main_esp32p4_test.cpp as part of Priority 1 refactoring.
 * WiFi is always enabled - WIFI_ENABLED guards removed.
 * 
 * Multi-network support: stores up to 8 networks, connects to strongest signal.
 */

#include "wifi_manager.h"
#include "nvs_guard.h"
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <Preferences.h>
#include <string.h>
#include "cJSON.h"

// External references to global state in main file
extern char wifiSSID[33];
extern char wifiPSK[65];
extern Preferences wifiPrefs;

// External references to RTC memory cache (defined in main file)
extern char cachedWifiSSID[33];
extern char cachedWifiPSK[65];
extern bool wifiCredentialsCached;

// ============================================================================
// Multi-Network Storage
// ============================================================================

// In-memory cache of networks (loaded from NVS on first access)
static WiFiNetwork g_wifiNetworks[WIFI_MAX_NETWORKS];
static int g_wifiNetworkCount = 0;
static bool g_wifiNetworksLoaded = false;

// Load networks from NVS into memory cache
static void loadNetworksFromNVS() {
    if (g_wifiNetworksLoaded) return;
    
    g_wifiNetworkCount = 0;
    memset(g_wifiNetworks, 0, sizeof(g_wifiNetworks));
    
    Preferences prefs;
    if (!prefs.begin("wifi_nets", true)) {  // Read-only
        Serial.println("No wifi_nets namespace - will migrate primary");
        g_wifiNetworksLoaded = true;
        
        // Migrate primary network if it exists
        if (strlen(wifiSSID) > 0) {
            strncpy(g_wifiNetworks[0].ssid, wifiSSID, sizeof(g_wifiNetworks[0].ssid) - 1);
            strncpy(g_wifiNetworks[0].psk, wifiPSK, sizeof(g_wifiNetworks[0].psk) - 1);
            g_wifiNetworks[0].hidden = false;
            g_wifiNetworks[0].enabled = true;
            g_wifiNetworkCount = 1;
            Serial.printf("Migrated primary network: %s\n", wifiSSID);
        }
        return;
    }
    
    // Load JSON blob
    String json = prefs.getString("networks", "[]");
    prefs.end();
    
    cJSON* root = cJSON_Parse(json.c_str());
    if (!root || !cJSON_IsArray(root)) {
        if (root) cJSON_Delete(root);
        g_wifiNetworksLoaded = true;
        return;
    }
    
    int count = cJSON_GetArraySize(root);
    for (int i = 0; i < count && g_wifiNetworkCount < WIFI_MAX_NETWORKS; i++) {
        cJSON* net = cJSON_GetArrayItem(root, i);
        if (!net) continue;
        
        cJSON* ssid = cJSON_GetObjectItem(net, "ssid");
        cJSON* psk = cJSON_GetObjectItem(net, "psk");
        cJSON* hidden = cJSON_GetObjectItem(net, "hidden");
        cJSON* enabled = cJSON_GetObjectItem(net, "enabled");
        
        if (!ssid || !cJSON_IsString(ssid) || strlen(ssid->valuestring) == 0) continue;
        
        WiFiNetwork& n = g_wifiNetworks[g_wifiNetworkCount];
        strncpy(n.ssid, ssid->valuestring, sizeof(n.ssid) - 1);
        n.ssid[sizeof(n.ssid) - 1] = '\0';
        
        if (psk && cJSON_IsString(psk)) {
            strncpy(n.psk, psk->valuestring, sizeof(n.psk) - 1);
            n.psk[sizeof(n.psk) - 1] = '\0';
        } else {
            n.psk[0] = '\0';
        }
        
        n.hidden = hidden && cJSON_IsBool(hidden) ? cJSON_IsTrue(hidden) : false;
        n.enabled = enabled && cJSON_IsBool(enabled) ? cJSON_IsTrue(enabled) : true;
        
        g_wifiNetworkCount++;
    }
    
    cJSON_Delete(root);
    g_wifiNetworksLoaded = true;
    Serial.printf("Loaded %d WiFi networks from NVS\n", g_wifiNetworkCount);
}

// Save networks from memory cache to NVS
static void saveNetworksToNVS() {
    cJSON* root = cJSON_CreateArray();
    
    for (int i = 0; i < g_wifiNetworkCount; i++) {
        WiFiNetwork& n = g_wifiNetworks[i];
        if (strlen(n.ssid) == 0) continue;
        
        cJSON* net = cJSON_CreateObject();
        cJSON_AddStringToObject(net, "ssid", n.ssid);
        cJSON_AddStringToObject(net, "psk", n.psk);
        cJSON_AddBoolToObject(net, "hidden", n.hidden);
        cJSON_AddBoolToObject(net, "enabled", n.enabled);
        cJSON_AddItemToArray(root, net);
    }
    
    char* json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (json) {
        Preferences prefs;
        if (prefs.begin("wifi_nets", false)) {  // Read-write
            prefs.putString("networks", json);
            prefs.end();
            Serial.printf("Saved %d WiFi networks to NVS\n", g_wifiNetworkCount);
        }
        free(json);
    }
    
    // Also update primary credentials (for backward compatibility)
    if (g_wifiNetworkCount > 0) {
        // Find first enabled network and set as primary
        for (int i = 0; i < g_wifiNetworkCount; i++) {
            if (g_wifiNetworks[i].enabled) {
                strncpy(wifiSSID, g_wifiNetworks[i].ssid, sizeof(wifiSSID) - 1);
                strncpy(wifiPSK, g_wifiNetworks[i].psk, sizeof(wifiPSK) - 1);
                break;
            }
        }
    }
}

int wifiGetNetworks(WiFiNetwork* networks, int maxNetworks) {
    loadNetworksFromNVS();
    
    int count = (g_wifiNetworkCount < maxNetworks) ? g_wifiNetworkCount : maxNetworks;
    memcpy(networks, g_wifiNetworks, count * sizeof(WiFiNetwork));
    return count;
}

bool wifiAddNetwork(const char* ssid, const char* psk, bool hidden, bool enabled) {
    if (!ssid || strlen(ssid) == 0) return false;
    
    loadNetworksFromNVS();
    
    // Check if network already exists (update it)
    for (int i = 0; i < g_wifiNetworkCount; i++) {
        if (strcmp(g_wifiNetworks[i].ssid, ssid) == 0) {
            // Update existing - only update password if a non-empty one is provided
            // This allows "save all" to preserve existing passwords when loading shows blank
            if (psk && strlen(psk) > 0) {
                strncpy(g_wifiNetworks[i].psk, psk, sizeof(g_wifiNetworks[i].psk) - 1);
                g_wifiNetworks[i].psk[sizeof(g_wifiNetworks[i].psk) - 1] = '\0';
            }
            g_wifiNetworks[i].hidden = hidden;
            g_wifiNetworks[i].enabled = enabled;
            saveNetworksToNVS();
            Serial.printf("Updated WiFi network: %s (password %s)\n", ssid, 
                         (psk && strlen(psk) > 0) ? "updated" : "preserved");
            return true;
        }
    }
    
    // Add new network
    if (g_wifiNetworkCount >= WIFI_MAX_NETWORKS) {
        Serial.println("ERROR: Maximum WiFi networks reached (8)");
        return false;
    }
    
    WiFiNetwork& n = g_wifiNetworks[g_wifiNetworkCount];
    strncpy(n.ssid, ssid, sizeof(n.ssid) - 1);
    n.ssid[sizeof(n.ssid) - 1] = '\0';
    
    if (psk) {
        strncpy(n.psk, psk, sizeof(n.psk) - 1);
        n.psk[sizeof(n.psk) - 1] = '\0';
    } else {
        n.psk[0] = '\0';
    }
    
    n.hidden = hidden;
    n.enabled = enabled;
    g_wifiNetworkCount++;
    
    saveNetworksToNVS();
    Serial.printf("Added WiFi network: %s (hidden=%d)\n", ssid, hidden);
    return true;
}

bool wifiRemoveNetwork(const char* ssid) {
    if (!ssid) return false;
    
    loadNetworksFromNVS();
    
    for (int i = 0; i < g_wifiNetworkCount; i++) {
        if (strcmp(g_wifiNetworks[i].ssid, ssid) == 0) {
            // Shift remaining networks down
            for (int j = i; j < g_wifiNetworkCount - 1; j++) {
                g_wifiNetworks[j] = g_wifiNetworks[j + 1];
            }
            g_wifiNetworkCount--;
            memset(&g_wifiNetworks[g_wifiNetworkCount], 0, sizeof(WiFiNetwork));
            
            saveNetworksToNVS();
            Serial.printf("Removed WiFi network: %s\n", ssid);
            return true;
        }
    }
    
    Serial.printf("WiFi network not found: %s\n", ssid);
    return false;
}

String wifiGetNetworksJSON() {
    loadNetworksFromNVS();
    
    cJSON* root = cJSON_CreateArray();
    
    for (int i = 0; i < g_wifiNetworkCount; i++) {
        WiFiNetwork& n = g_wifiNetworks[i];
        if (strlen(n.ssid) == 0) continue;
        
        cJSON* net = cJSON_CreateObject();
        cJSON_AddStringToObject(net, "ssid", n.ssid);
        cJSON_AddBoolToObject(net, "hidden", n.hidden);
        cJSON_AddBoolToObject(net, "enabled", n.enabled);
        // PSK excluded for security
        cJSON_AddItemToArray(root, net);
    }
    
    char* json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    String result = json ? json : "[]";
    if (json) free(json);
    return result;
}

String wifiGetNetworksJSONWithPSK() {
    loadNetworksFromNVS();
    
    cJSON* root = cJSON_CreateArray();
    
    for (int i = 0; i < g_wifiNetworkCount; i++) {
        WiFiNetwork& n = g_wifiNetworks[i];
        if (strlen(n.ssid) == 0) continue;
        
        cJSON* net = cJSON_CreateObject();
        cJSON_AddStringToObject(net, "ssid", n.ssid);
        cJSON_AddStringToObject(net, "psk", n.psk);
        cJSON_AddBoolToObject(net, "hidden", n.hidden);
        cJSON_AddBoolToObject(net, "enabled", n.enabled);
        cJSON_AddItemToArray(root, net);
    }
    
    char* json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    String result = json ? json : "[]";
    if (json) free(json);
    return result;
}

int wifiLoadNetworksFromJSON(const char* json) {
    if (!json) return 0;
    
    cJSON* root = cJSON_Parse(json);
    if (!root || !cJSON_IsArray(root)) {
        if (root) cJSON_Delete(root);
        return 0;
    }
    
    // Clear existing networks
    g_wifiNetworkCount = 0;
    memset(g_wifiNetworks, 0, sizeof(g_wifiNetworks));
    
    int count = cJSON_GetArraySize(root);
    int imported = 0;
    
    for (int i = 0; i < count && g_wifiNetworkCount < WIFI_MAX_NETWORKS; i++) {
        cJSON* net = cJSON_GetArrayItem(root, i);
        if (!net) continue;
        
        cJSON* ssid = cJSON_GetObjectItem(net, "ssid");
        cJSON* psk = cJSON_GetObjectItem(net, "psk");
        cJSON* hidden = cJSON_GetObjectItem(net, "hidden");
        cJSON* enabled = cJSON_GetObjectItem(net, "enabled");
        
        if (!ssid || !cJSON_IsString(ssid) || strlen(ssid->valuestring) == 0) continue;
        
        WiFiNetwork& n = g_wifiNetworks[g_wifiNetworkCount];
        strncpy(n.ssid, ssid->valuestring, sizeof(n.ssid) - 1);
        
        if (psk && cJSON_IsString(psk)) {
            strncpy(n.psk, psk->valuestring, sizeof(n.psk) - 1);
        }
        
        n.hidden = hidden && cJSON_IsBool(hidden) ? cJSON_IsTrue(hidden) : false;
        n.enabled = enabled && cJSON_IsBool(enabled) ? cJSON_IsTrue(enabled) : true;
        
        g_wifiNetworkCount++;
        imported++;
    }
    
    cJSON_Delete(root);
    g_wifiNetworksLoaded = true;
    
    saveNetworksToNVS();
    Serial.printf("Imported %d WiFi networks from JSON\n", imported);
    return imported;
}

void wifiPrintNetworks() {
    loadNetworksFromNVS();
    
    Serial.println("\n=== Configured WiFi Networks ===");
    if (g_wifiNetworkCount == 0) {
        Serial.println("  (none)");
    } else {
        for (int i = 0; i < g_wifiNetworkCount; i++) {
            WiFiNetwork& n = g_wifiNetworks[i];
            Serial.printf("  %d. %s%s%s\n", 
                i + 1, 
                n.ssid,
                n.hidden ? " [hidden]" : "",
                n.enabled ? "" : " [disabled]");
        }
    }
    Serial.printf("Total: %d/%d networks\n", g_wifiNetworkCount, WIFI_MAX_NETWORKS);
    Serial.println("================================\n");
}

// Structure for sorting networks by RSSI
struct NetworkMatch {
    int networkIndex;  // Index in g_wifiNetworks
    int32_t rssi;      // Signal strength
};

bool wifiConnectBest(uint32_t timeoutPerAttemptMs) {
    loadNetworksFromNVS();
    
    if (g_wifiNetworkCount == 0) {
        Serial.println("No WiFi networks configured");
        return false;
    }
    
    // Configure WiFi
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    WiFi.setAutoReconnect(true);
    
    Serial.println("Scanning for WiFi networks...");
    int scanResult = WiFi.scanNetworks(false, true);  // async=false, show_hidden=true
    
    if (scanResult < 0) {
        Serial.printf("WiFi scan failed: %d\n", scanResult);
        // Fall back to trying networks in order
        scanResult = 0;
    }
    
    Serial.printf("Found %d networks in range\n", scanResult);
    
    // Build list of matching visible networks sorted by RSSI
    NetworkMatch matches[WIFI_MAX_NETWORKS];
    int matchCount = 0;
    
    // Track which of our networks are hidden (to try later)
    bool hiddenNetworksTried[WIFI_MAX_NETWORKS] = {false};
    
    for (int i = 0; i < g_wifiNetworkCount; i++) {
        if (!g_wifiNetworks[i].enabled) continue;
        if (g_wifiNetworks[i].hidden) continue;  // Skip hidden for now
        
        // Look for this network in scan results
        for (int j = 0; j < scanResult; j++) {
            if (WiFi.SSID(j) == g_wifiNetworks[i].ssid) {
                // Found a match
                matches[matchCount].networkIndex = i;
                matches[matchCount].rssi = WiFi.RSSI(j);
                matchCount++;
                Serial.printf("  Matched: %s (RSSI: %d dBm)\n", 
                    g_wifiNetworks[i].ssid, WiFi.RSSI(j));
                break;
            }
        }
    }
    
    // Sort matches by RSSI (strongest first) - simple bubble sort
    for (int i = 0; i < matchCount - 1; i++) {
        for (int j = 0; j < matchCount - i - 1; j++) {
            if (matches[j].rssi < matches[j + 1].rssi) {
                NetworkMatch temp = matches[j];
                matches[j] = matches[j + 1];
                matches[j + 1] = temp;
            }
        }
    }
    
    WiFi.scanDelete();  // Free scan memory
    
    // Try visible networks in RSSI order
    for (int i = 0; i < matchCount; i++) {
        WiFiNetwork& net = g_wifiNetworks[matches[i].networkIndex];
        Serial.printf("Trying %s (RSSI: %d dBm)...\n", net.ssid, matches[i].rssi);
        
        WiFi.disconnect();
        delay(100);
        WiFi.begin(net.ssid, net.psk);
        
        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutPerAttemptMs) {
            delay(250);
            Serial.print(".");
        }
        Serial.println();
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("Connected to %s!\n", net.ssid);
            Serial.printf("  IP: %s, RSSI: %d dBm\n", 
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
            
            // Update primary credentials for backward compatibility
            strncpy(wifiSSID, net.ssid, sizeof(wifiSSID) - 1);
            strncpy(wifiPSK, net.psk, sizeof(wifiPSK) - 1);
            return true;
        }
        
        Serial.printf("Failed to connect to %s\n", net.ssid);
    }
    
    // Try hidden networks (can't know RSSI until connected)
    Serial.println("Trying hidden networks...");
    for (int i = 0; i < g_wifiNetworkCount; i++) {
        WiFiNetwork& net = g_wifiNetworks[i];
        if (!net.enabled || !net.hidden) continue;
        
        Serial.printf("Probing hidden network: %s...\n", net.ssid);
        
        WiFi.disconnect();
        delay(100);
        WiFi.begin(net.ssid, net.psk);
        
        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutPerAttemptMs) {
            delay(250);
            Serial.print(".");
        }
        Serial.println();
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("Connected to hidden network %s!\n", net.ssid);
            Serial.printf("  IP: %s, RSSI: %d dBm\n", 
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
            
            // Update primary credentials for backward compatibility
            strncpy(wifiSSID, net.ssid, sizeof(wifiSSID) - 1);
            strncpy(wifiPSK, net.psk, sizeof(wifiPSK) - 1);
            return true;
        }
        
        Serial.printf("Failed to connect to hidden network %s\n", net.ssid);
    }
    
    Serial.println("Failed to connect to any configured network");
    return false;
}

bool performNtpSync(uint32_t timeout_ms) {
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    
    // NTP sync with retries - be very persistent (retry a lot harder)
    const int maxNtpRetries = 20;  // Increased from 5 to 20 for much more persistence
    const uint32_t ntpTimeoutPerAttempt = 60000;  // 60 seconds per attempt (increased from 30s)
    
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
        // Also ensure networks are loaded
        loadNetworksFromNVS();
        return true;
    }
    
    // Load multi-network configuration
    loadNetworksFromNVS();
    
    // If we have networks configured, use the first enabled one as primary
    if (g_wifiNetworkCount > 0) {
        for (int i = 0; i < g_wifiNetworkCount; i++) {
            if (g_wifiNetworks[i].enabled) {
                strncpy(wifiSSID, g_wifiNetworks[i].ssid, sizeof(wifiSSID) - 1);
                wifiSSID[sizeof(wifiSSID) - 1] = '\0';
                strncpy(wifiPSK, g_wifiNetworks[i].psk, sizeof(wifiPSK) - 1);
                wifiPSK[sizeof(wifiPSK) - 1] = '\0';
                
                // Cache in RTC memory
                strncpy(cachedWifiSSID, wifiSSID, sizeof(cachedWifiSSID) - 1);
                cachedWifiSSID[sizeof(cachedWifiSSID) - 1] = '\0';
                strncpy(cachedWifiPSK, wifiPSK, sizeof(cachedWifiPSK) - 1);
                cachedWifiPSK[sizeof(cachedWifiPSK) - 1] = '\0';
                wifiCredentialsCached = true;
                
                Serial.printf("Loaded WiFi from networks list: %s (%d total networks)\n", 
                    wifiSSID, g_wifiNetworkCount);
                return true;
            }
        }
    }
    
    // Fall back to legacy single-network NVS storage
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
        wifiSSID[sizeof(wifiSSID) - 1] = '\0';
        strncpy(wifiPSK, psk.c_str(), sizeof(wifiPSK) - 1);
        wifiPSK[sizeof(wifiPSK) - 1] = '\0';
        
        // Migrate to new multi-network system
        wifiAddNetwork(wifiSSID, wifiPSK, false, true);
        
        // Cache credentials in RTC memory for next cycle (survives deep sleep)
        strncpy(cachedWifiSSID, wifiSSID, sizeof(cachedWifiSSID) - 1);
        cachedWifiSSID[sizeof(cachedWifiSSID) - 1] = '\0';
        strncpy(cachedWifiPSK, wifiPSK, sizeof(cachedWifiPSK) - 1);
        cachedWifiPSK[sizeof(cachedWifiPSK) - 1] = '\0';
        wifiCredentialsCached = true;
        
        Serial.printf("Loaded WiFi from legacy NVS (migrated): %s\n", wifiSSID);
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
    // If already connected, return early
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("WiFi already connected to %s (RSSI: %d dBm)\n", 
            WiFi.SSID().c_str(), WiFi.RSSI());
        return true;
    }
    
    // Try multi-network connection first (scan and connect to strongest)
    loadNetworksFromNVS();
    if (g_wifiNetworkCount > 1) {
        Serial.printf("Multi-network mode: %d networks configured\n", g_wifiNetworkCount);
        if (wifiConnectBest(timeoutPerAttemptMs)) {
            return true;
        }
        Serial.println("Multi-network connection failed, falling back to primary...");
    }
    
    // Fall back to single-network mode (legacy or only 1 network configured)
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
    
    // Try connecting with multiple retries
    for (int retry = 0; retry < maxRetries; retry++) {
        if (retry > 0) {
            Serial.printf("WiFi connection attempt %d/%d...\n", retry + 1, maxRetries);
            delay(2000);  // Wait 2 seconds before retry
            if (WiFi.status() != WL_CONNECTED) {
                WiFi.disconnect();
                delay(500);
            }
        }
        
        Serial.print("Connecting");
        if (WiFi.status() != WL_CONNECTED) {
            WiFi.begin(wifiSSID, wifiPSK);
        }
        
        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - start < timeoutPerAttemptMs)) {
            delay(500);
            Serial.print(".");
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
        
        // Even for required connections, have an absolute maximum to allow fallback to AP config mode
        // 20 additional retries at ~30s each = about 10 more minutes before giving up
        const int absoluteMaxRetries = 20;
        int persistentRetry = 0;
        
        while (WiFi.status() != WL_CONNECTED && persistentRetry < absoluteMaxRetries) {
            persistentRetry++;
            Serial.printf("Retrying WiFi connection (required) - attempt %d/%d...\n", persistentRetry, absoluteMaxRetries);
            delay(5000);
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
        
        // Exhausted all retries even in required mode
        Serial.println("\n========================================");
        Serial.println("CRITICAL: WiFi connection failed after exhaustive retries!");
        Serial.println("Network may be unavailable or credentials incorrect.");
        Serial.println("Returning false to allow AP config mode fallback.");
        Serial.println("========================================\n");
        return false;
    } else {
        Serial.println("WiFi connection failed after all retries");
        return false;
    }
}

void wifiSaveCredentials() {
    wifiPrefs.begin("wifi", false);  // Read-write
    wifiPrefs.putString("ssid", wifiSSID);
    wifiPrefs.putString("psk", wifiPSK);
    wifiPrefs.end();
    
    // Also add to multi-network system
    wifiAddNetwork(wifiSSID, wifiPSK, false, true);
    
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

void wifiClearNetworks() {
    g_wifiNetworkCount = 0;
    memset(g_wifiNetworks, 0, sizeof(g_wifiNetworks));
    g_wifiNetworksLoaded = true;  // Mark as loaded (but empty)
    saveNetworksToNVS();
    Serial.println("All WiFi networks cleared");
}

// ============================================================================
// AP Mode Captive Portal for WiFi Configuration
// ============================================================================

#include <DNSServer.h>
#include <WebServer.h>

// Captive portal HTML - minimal and functional
static const char AP_CONFIG_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>BigInky WiFi Setup</title>
<style>
*{box-sizing:border-box;font-family:system-ui,-apple-system,sans-serif}
body{margin:0;padding:20px;background:#1a1a2e;color:#eee;min-height:100vh}
.c{max-width:400px;margin:0 auto}
h1{color:#e94560;margin:0 0 10px;font-size:24px}
p{color:#aaa;margin:0 0 20px;font-size:14px}
.card{background:#16213e;padding:20px;border-radius:12px;margin-bottom:15px}
label{display:block;margin-bottom:5px;color:#aaa;font-size:13px}
input,select{width:100%;padding:12px;border:1px solid #333;border-radius:8px;background:#0f0f23;color:#fff;font-size:16px;margin-bottom:15px}
input:focus,select:focus{outline:none;border-color:#e94560}
button{width:100%;padding:14px;background:#e94560;border:none;border-radius:8px;color:#fff;font-size:16px;cursor:pointer;font-weight:600}
button:hover{background:#ff6b6b}
.scan{background:#4a4e69;margin-bottom:10px}
.scan:hover{background:#6c757d}
.net{padding:10px;background:#0f0f23;border-radius:6px;margin:5px 0;cursor:pointer;display:flex;justify-content:space-between}
.net:hover{background:#1a1a2e}
.rssi{color:#4a4e69;font-size:12px}
.hidden{display:none}
.msg{padding:15px;border-radius:8px;margin:15px 0;text-align:center}
.ok{background:#2d6a4f;color:#fff}
.err{background:#9b2226;color:#fff}
#networks{max-height:200px;overflow-y:auto}
</style>
</head>
<body>
<div class="c">
<h1>&#128420; BigInky Setup</h1>
<p>Connect your device to WiFi</p>
<div id="msg"></div>
<div class="card">
<button class="scan" onclick="scan()">&#128270; Scan for Networks</button>
<div id="networks"></div>
<form id="f" onsubmit="return save()">
<label>Network Name (SSID)</label>
<input type="text" id="ssid" name="ssid" required maxlength="32" placeholder="Enter or select network">
<label>Password</label>
<input type="password" id="psk" name="psk" maxlength="64" placeholder="Leave empty for open network">
<label><input type="checkbox" id="hidden" name="hidden"> Hidden network</label><br><br>
<button type="submit">&#128274; Save &amp; Connect</button>
</form>
</div>
<p style="text-align:center;font-size:12px;color:#666">After saving, the device will restart and connect to your network.</p>
</div>
<script>
function scan(){
document.getElementById('networks').innerHTML='<p style="color:#aaa">Scanning...</p>';
fetch('/scan').then(r=>r.json()).then(d=>{
let h='';
d.forEach(n=>{
h+='<div class="net" onclick="pick(\''+n.ssid+'\')"><span>'+n.ssid+(n.enc?' &#128274;':' &#128275;')+'</span><span class="rssi">'+n.rssi+' dBm</span></div>';
});
document.getElementById('networks').innerHTML=h||'<p style="color:#aaa">No networks found</p>';
}).catch(e=>{
document.getElementById('networks').innerHTML='<p style="color:#e94560">Scan failed</p>';
});
}
function pick(s){document.getElementById('ssid').value=s;document.getElementById('psk').focus();}
function save(){
let f=document.getElementById('f');
let fd=new FormData(f);
fetch('/save',{method:'POST',body:fd}).then(r=>r.json()).then(d=>{
if(d.success){
document.getElementById('msg').innerHTML='<div class="msg ok">&#9989; WiFi saved! Restarting...</div>';
setTimeout(()=>{document.getElementById('msg').innerHTML='<div class="msg ok">You can close this page and reconnect to your normal WiFi.</div>';},3000);
}else{
document.getElementById('msg').innerHTML='<div class="msg err">&#10060; '+d.error+'</div>';
}
}).catch(e=>{
document.getElementById('msg').innerHTML='<div class="msg err">&#10060; Connection error</div>';
});
return false;
}
</script>
</body>
</html>
)rawliteral";

// Global state for AP mode
static bool g_apConfigComplete = false;
static WebServer* g_apServer = nullptr;
static DNSServer* g_dnsServer = nullptr;

// Handler for captive portal detection
static void handleCaptivePortal() {
    g_apServer->send(200, "text/html", AP_CONFIG_HTML);
}

// Handler for WiFi scan
static void handleScan() {
    Serial.println("AP Config: Scanning for networks...");
    int n = WiFi.scanNetworks(false, true);  // Include hidden networks
    
    String json = "[";
    for (int i = 0; i < n; i++) {
        if (i > 0) json += ",";
        json += "{\"ssid\":\"";
        json += WiFi.SSID(i);
        json += "\",\"rssi\":";
        json += String(WiFi.RSSI(i));
        json += ",\"enc\":";
        json += (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) ? "true" : "false";
        json += "}";
    }
    json += "]";
    
    WiFi.scanDelete();
    Serial.printf("AP Config: Found %d networks\n", n);
    g_apServer->send(200, "application/json", json);
}

// Handler for saving WiFi credentials
static void handleSave() {
    String ssid = g_apServer->arg("ssid");
    String psk = g_apServer->arg("psk");
    bool hidden = g_apServer->hasArg("hidden");
    
    Serial.printf("AP Config: Saving SSID='%s' hidden=%d\n", ssid.c_str(), hidden);
    
    if (ssid.length() == 0 || ssid.length() > 32) {
        g_apServer->send(200, "application/json", "{\"success\":false,\"error\":\"Invalid SSID\"}");
        return;
    }
    
    if (psk.length() > 64) {
        g_apServer->send(200, "application/json", "{\"success\":false,\"error\":\"Password too long\"}");
        return;
    }
    
    // Save to multi-network system
    if (!wifiAddNetwork(ssid.c_str(), psk.c_str(), hidden, true)) {
        g_apServer->send(200, "application/json", "{\"success\":false,\"error\":\"Failed to save\"}");
        return;
    }
    
    // Also save as primary credentials for legacy compatibility
    strncpy(wifiSSID, ssid.c_str(), sizeof(wifiSSID) - 1);
    wifiSSID[sizeof(wifiSSID) - 1] = '\0';
    strncpy(wifiPSK, psk.c_str(), sizeof(wifiPSK) - 1);
    wifiPSK[sizeof(wifiPSK) - 1] = '\0';
    wifiSaveCredentials();
    
    g_apServer->send(200, "application/json", "{\"success\":true}");
    g_apConfigComplete = true;
}

bool wifiStartAPConfigMode(uint8_t timeoutMinutes) {
    Serial.println("\n========================================");
    Serial.println("  WiFi AP Configuration Mode");
    Serial.println("========================================");
    
    // Disconnect any existing connection
    WiFi.disconnect(true);
    delay(100);
    
    // Start Access Point
    const char* apSSID = "BigInky-Setup";
    Serial.printf("Starting AP: %s\n", apSSID);
    
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(apSSID)) {
        Serial.println("ERROR: Failed to start AP");
        return false;
    }
    
    delay(100);
    IPAddress apIP = WiFi.softAPIP();
    Serial.printf("AP IP address: %s\n", apIP.toString().c_str());
    
    // Start DNS server for captive portal (redirect all domains to our IP)
    g_dnsServer = new DNSServer();
    g_dnsServer->start(53, "*", apIP);
    Serial.println("DNS server started (captive portal)");
    
    // Start HTTP server
    g_apServer = new WebServer(80);
    
    // Captive portal detection endpoints (various OS/browsers)
    g_apServer->on("/", handleCaptivePortal);
    g_apServer->on("/generate_204", handleCaptivePortal);  // Android
    g_apServer->on("/gen_204", handleCaptivePortal);       // Android
    g_apServer->on("/hotspot-detect.html", handleCaptivePortal);  // iOS
    g_apServer->on("/canonical.html", handleCaptivePortal);  // iOS
    g_apServer->on("/success.txt", handleCaptivePortal);   // iOS
    g_apServer->on("/ncsi.txt", handleCaptivePortal);      // Windows
    g_apServer->on("/connecttest.txt", handleCaptivePortal);  // Windows
    g_apServer->on("/redirect", handleCaptivePortal);      // Windows
    g_apServer->onNotFound(handleCaptivePortal);           // Catch-all
    
    // API endpoints
    g_apServer->on("/scan", HTTP_GET, handleScan);
    g_apServer->on("/save", HTTP_POST, handleSave);
    
    g_apServer->begin();
    Serial.println("HTTP server started");
    
    Serial.println("========================================");
    Serial.printf("Connect to WiFi: %s\n", apSSID);
    Serial.println("Then open any webpage or http://192.168.4.1");
    Serial.println("========================================\n");
    
    // Run AP mode loop
    g_apConfigComplete = false;
    uint32_t startTime = millis();
    uint32_t timeoutMs = timeoutMinutes * 60 * 1000;
    
    while (!g_apConfigComplete) {
        g_dnsServer->processNextRequest();
        g_apServer->handleClient();
        delay(10);
        
        // Check for timeout (if not indefinite)
        if (timeoutMinutes > 0 && (millis() - startTime) >= timeoutMs) {
            Serial.println("AP Config: Timeout reached");
            break;
        }
        
        // Check for serial input to exit early
        if (Serial.available()) {
            char ch = Serial.read();
            if (ch == 'q' || ch == 'Q') {
                Serial.println("AP Config: Cancelled via serial");
                break;
            }
        }
    }
    
    // Cleanup
    Serial.println("Stopping AP mode...");
    g_apServer->stop();
    delete g_apServer;
    g_apServer = nullptr;
    
    g_dnsServer->stop();
    delete g_dnsServer;
    g_dnsServer = nullptr;
    
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    
    Serial.println("AP mode stopped");
    
    return g_apConfigComplete;
}



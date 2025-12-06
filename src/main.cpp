/**
 * @file main.cpp
 * @brief Example application for EL133UF1 13.3" Spectra 6 E-Ink Display
 * 
 * This example demonstrates driving the EL133UF1 e-ink panel with a
 * Pimoroni Pico LiPo 2 XL W (RP2350) using the Arduino-Pico framework.
 * Build target: pimoroni_pico_plus_2w (compatible)
 * 
 * Wiring (Pimoroni Inky Impression 13.3" + Pico LiPo 2 XL W):
 *   Display      Pico LiPo 2 XL W
 *   -------      ----------------
 *   MOSI    ->   GP11 (SPI1 TX)
 *   SCLK    ->   GP10 (SPI1 SCK)
 *   CS0     ->   GP26 (left half)
 *   CS1     ->   GP16 (right half)
 *   DC      ->   GP22
 *   RESET   ->   GP27
 *   BUSY    ->   GP17
 *   GND     ->   GND
 *   3.3V    ->   3V3
 * 
 * DS3231 RTC (optional, for accurate timekeeping):
 *   SDA     ->   GP2 (I2C1)
 *   SCL     ->   GP3 (I2C1)
 *   INT     ->   GP18 (wake from sleep)
 * 
 * Battery Monitoring:
 *   VBAT    ->   GP43 (ADC, via voltage divider)
 * 
 * WiFi Configuration:
 *   On first boot (or press 'c' within 3 seconds), enter config mode
 *   to set WiFi credentials via serial. Credentials are stored in EEPROM.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "EL133UF1.h"
#include "EL133UF1_TTF.h"
#include "EL133UF1_BMP.h"
#include "EL133UF1_PNG.h"
#include "OpenAIImage.h"
#include "fonts/opensans.h"
#include "pico_sleep.h"
#include "DS3231.h"
#include "AT24C32.h"
#include "hardware/structs/powman.h"
#include "hardware/powman.h"

// WiFi credentials - loaded from EEPROM or set via serial config
// Compile-time fallback (optional, for development only)
// Set via platformio_local.ini (not committed to git):
//   build_flags = -DWIFI_SSID_DEFAULT=\"YourSSID\" -DWIFI_PSK_DEFAULT=\"YourPassword\"
#ifndef WIFI_SSID_DEFAULT
#define WIFI_SSID_DEFAULT ""
#endif
#ifndef WIFI_PSK_DEFAULT
#define WIFI_PSK_DEFAULT ""
#endif

// Runtime credential buffers
static char wifiSSID[33] = {0};
static char wifiPSK[65] = {0};

// Pin definitions for Pimoroni Pico Plus 2 W with Inky Impression 13.3"
// These match the working CircuitPython reference
#define PIN_SPI_SCK   10    // SPI1 SCK (GP10)
#define PIN_SPI_MOSI  11    // SPI1 TX/MOSI (GP11)
#define PIN_CS0       26    // Chip Select 0 - left half (GP26)
#define PIN_CS1       16    // Chip Select 1 - right half (GP16)
#define PIN_DC        22    // Data/Command (GP22)
#define PIN_RESET     27    // Reset (GP27)
#define PIN_BUSY      17    // Busy (GP17)

// DS3231 RTC pins (I2C1)
#define PIN_RTC_SDA    2    // I2C1 SDA (GP2)
#define PIN_RTC_SCL    3    // I2C1 SCL (GP3)
#define PIN_RTC_INT   18    // DS3231 INT/SQW pin for wake (GP18)

// Battery voltage monitoring (Pimoroni Pico LiPo 2 XL W)
// GP43 is the battery voltage ADC pin
#define PIN_VBAT_ADC  43    // Battery voltage ADC pin (GP43 on Pico LiPo)

// Voltage divider ratio - adjust based on actual circuit
// If battery shows wrong voltage, measure with multimeter and calibrate
#define VBAT_DIVIDER_RATIO  3.0f
// ADC reference voltage (3.3V for RP2350)
#define VBAT_ADC_REF  3.3f

// Create display instance using SPI1
// (SPI1 is the correct bus for GP10/GP11 on Pico)
EL133UF1 display(&SPI1);

// TTF font renderer
EL133UF1_TTF ttf;

// BMP image loader
EL133UF1_BMP bmp;

// PNG decoder and OpenAI image generator
EL133UF1_PNG png;
OpenAIImage openai;

// AI-generated image stored in PSRAM (persists between updates)
static uint8_t* aiImageData = nullptr;
static size_t aiImageLen = 0;

// ================================================================
// Battery voltage monitoring
// ================================================================

float readBatteryVoltage() {
    // Set ADC resolution to 12-bit
    analogReadResolution(12);
    
    // Read battery voltage from GP43
    static bool firstRead = true;
    if (firstRead) {
        // Just read GP43 - the designated battery ADC pin
        // Note: Battery reading may only work when running on battery (not USB)
        pinMode(PIN_VBAT_ADC, INPUT);
        uint16_t raw = analogRead(PIN_VBAT_ADC);
        Serial.printf("  [Battery] GP%d raw=%u -> %.2fV\n", 
                      PIN_VBAT_ADC, raw, raw * 3.3f / 4095.0f * VBAT_DIVIDER_RATIO);
        firstRead = false;
    }
    
    // Use the configured pin
    pinMode(PIN_VBAT_ADC, INPUT);
    
    // Take multiple readings and average for stability
    uint32_t sum = 0;
    const int samples = 16;
    for (int i = 0; i < samples; i++) {
        sum += analogRead(PIN_VBAT_ADC);
        delayMicroseconds(100);
    }
    uint16_t adcValue = sum / samples;
    
    // Convert to voltage
    // ADC is 12-bit (0-4095), reference is 3.3V
    float adcVoltage = (adcValue / 4095.0f) * VBAT_ADC_REF;
    
    // Apply voltage divider ratio to get actual battery voltage
    float batteryVoltage = adcVoltage * VBAT_DIVIDER_RATIO;
    
    return batteryVoltage;
}

// Get battery percentage (rough estimate for LiPo)
// LiPo: 4.2V = 100%, 3.7V = ~50%, 3.0V = 0%
int getBatteryPercent(float voltage) {
    if (voltage >= 4.2f) return 100;
    if (voltage <= 3.0f) return 0;
    
    // Linear interpolation between 3.0V and 4.2V
    return (int)((voltage - 3.0f) / 1.2f * 100.0f);
}

// Forward declarations
void drawDemoPattern();
bool connectWiFiAndGetNTP();
void formatTime(uint64_t time_ms, char* buf, size_t len);
void logStage(uint8_t stage);
void logUpdateInfo(uint16_t updateNum, uint32_t wakeTime);
void reportLastUpdate();

// ================================================================
// WiFi Credential Management
// ================================================================

// Load credentials from EEPROM or compiled fallback
bool loadWifiCredentials() {
    Serial.printf("loadWifiCredentials: eeprom.isPresent()=%d\n", eeprom.isPresent());
    
    if (eeprom.isPresent()) {
        bool hasCreds = eeprom.hasWifiCredentials();
        Serial.printf("loadWifiCredentials: hasWifiCredentials()=%d\n", hasCreds);
        
        if (hasCreds) {
            eeprom.getWifiCredentials(wifiSSID, sizeof(wifiSSID), wifiPSK, sizeof(wifiPSK));
            Serial.printf("WiFi: Loaded from EEPROM: '%s'\n", wifiSSID);
            return true;
        }
    }
    
    // Fallback to compiled defaults (if any)
    if (strlen(WIFI_SSID_DEFAULT) > 0) {
        strncpy(wifiSSID, WIFI_SSID_DEFAULT, sizeof(wifiSSID) - 1);
        strncpy(wifiPSK, WIFI_PSK_DEFAULT, sizeof(wifiPSK) - 1);
        Serial.printf("WiFi: Using compiled fallback: '%s'\n", wifiSSID);
        return true;
    }
    
    Serial.println("WiFi: No credentials available");
    return false;
}

// Read a line from Serial with echo
String serialReadLine(bool maskInput = false) {
    // Flush any pending newlines from previous input
    while (Serial.available()) {
        char c = Serial.peek();
        if (c == '\n' || c == '\r') {
            Serial.read();  // Consume it
        } else {
            break;
        }
    }
    
    String result = "";
    while (true) {
        if (Serial.available()) {
            char c = Serial.read();
            if (c == '\n' || c == '\r') {
                // End of line - consume any trailing \n after \r
                delay(10);  // Brief delay for trailing chars to arrive
                while (Serial.available()) {
                    char next = Serial.peek();
                    if (next == '\n' || next == '\r') {
                        Serial.read();
                    } else {
                        break;
                    }
                }
                Serial.println();
                break;
            } else if (c == '\b' || c == 127) {  // Backspace
                if (result.length() > 0) {
                    result.remove(result.length() - 1);
                    Serial.print("\b \b");
                }
            } else if (c >= 32 && c < 127) {  // Printable
                result += c;
                Serial.print(maskInput ? '*' : c);
            }
        }
        delay(10);
    }
    return result;
}

// Interactive serial configuration mode
void enterConfigMode() {
    Serial.println("\n========================================");
    Serial.println("       Configuration Mode");
    Serial.println("========================================");
    
    // --- WiFi Configuration ---
    Serial.println("\n--- WiFi Settings ---");
    
    // Load existing credentials if any
    char existingSSID[33] = {0};
    char existingPSK[65] = {0};
    bool hasExisting = eeprom.isPresent() && eeprom.hasWifiCredentials();
    if (hasExisting) {
        eeprom.getWifiCredentials(existingSSID, sizeof(existingSSID), 
                                   existingPSK, sizeof(existingPSK));
        Serial.printf("Current SSID: '%s'\n", existingSSID);
        Serial.println("(Press Enter to keep current, or type new value)");
    }
    
    // Get SSID
    Serial.print("WiFi SSID: ");
    String ssid = serialReadLine(false);
    
    // If empty and we have existing, keep it
    if (ssid.length() == 0 && hasExisting) {
        Serial.println("(keeping existing SSID)");
        ssid = existingSSID;
    }
    
    if (ssid.length() == 0) {
        Serial.println("ERROR: SSID cannot be empty!");
        return;
    }
    
    // Get PSK
    Serial.print("WiFi Password: ");
    String psk = serialReadLine(true);  // Masked input
    
    // If empty and we have existing, keep it
    if (psk.length() == 0 && hasExisting) {
        Serial.println("(keeping existing password)");
        psk = existingPSK;
    }
    
    // Save WiFi to EEPROM
    if (eeprom.isPresent()) {
        eeprom.setWifiCredentials(ssid.c_str(), psk.c_str());
        Serial.println("WiFi credentials saved!");
        
        // Load into active buffers
        strncpy(wifiSSID, ssid.c_str(), sizeof(wifiSSID) - 1);
        strncpy(wifiPSK, psk.c_str(), sizeof(wifiPSK) - 1);
    } else {
        Serial.println("WARNING: EEPROM not available, using for this session only");
        strncpy(wifiSSID, ssid.c_str(), sizeof(wifiSSID) - 1);
        strncpy(wifiPSK, psk.c_str(), sizeof(wifiPSK) - 1);
    }
    
    // --- OpenAI API Key Configuration ---
    Serial.println("\n--- OpenAI API Key (for AI image generation) ---");
    
    if (eeprom.isPresent() && eeprom.hasOpenAIKey()) {
        char currentKey[64];
        eeprom.getOpenAIKey(currentKey, sizeof(currentKey));
        // Show only first/last few chars for security
        Serial.printf("Current key: %.7s...%s\n", currentKey, currentKey + strlen(currentKey) - 4);
        Serial.println("(Press Enter to keep current, or paste new key)");
    } else {
        Serial.println("No API key configured.");
        Serial.println("Get one at: https://platform.openai.com/api-keys");
    }
    
    Serial.print("OpenAI API Key: ");
    String apiKey = serialReadLine(true);  // Masked input
    
    if (apiKey.length() > 0) {
        if (apiKey.startsWith("sk-")) {
            if (eeprom.isPresent()) {
                eeprom.setOpenAIKey(apiKey.c_str());
                Serial.println("API key saved!");
            }
        } else {
            Serial.println("WARNING: Key doesn't start with 'sk-', not saved.");
        }
    } else if (eeprom.hasOpenAIKey()) {
        Serial.println("(keeping existing key)");
    }
    
    Serial.println("\n========================================\n");
}

// Check for config mode trigger during boot
// Returns true if config mode was entered
bool checkConfigMode() {
    // Skip on wake from deep sleep
    if (sleep_woke_from_deep_sleep()) {
        return false;
    }
    
    Serial.println("\nPress 'c' for config (WiFi/API key), 'r' to reset sleep state (3s)...");
    Serial.flush();
    
    uint32_t start = millis();
    while (millis() - start < 3000) {
        if (Serial.available()) {
            char c = Serial.read();
            if (c == 'c' || c == 'C') {
                enterConfigMode();
                return true;
            } else if (c == 'r' || c == 'R') {
                sleep_clear_all_state();
                Serial.println("Reboot to apply clean state.");
                return true;
            }
        }
        // Show countdown
        uint32_t remaining = 3 - (millis() - start) / 1000;
        static uint32_t lastShown = 99;
        if (remaining != lastShown) {
            Serial.printf("\r%lu... ", remaining);
            lastShown = remaining;
        }
        delay(50);
    }
    Serial.println("continuing.");
    return false;
}

// ================================================================
// Connect to WiFi and sync NTP time (arduino-pico native)
// ================================================================
bool connectWiFiAndGetNTP() {
    if (strlen(wifiSSID) == 0) {
        Serial.println("ERROR: No WiFi credentials configured!");
        return false;
    }
    
    Serial.println("\n=== Connecting to WiFi ===");
    Serial.printf("SSID: %s\n", wifiSSID);
    
    WiFi.begin(wifiSSID, wifiPSK);
    
    Serial.print("Connecting");
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start < 30000)) {
        Serial.print(".");
        delay(500);
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\nWiFi connect failed!");
        return false;
    }
    
    Serial.println("\nWiFi connected!");
    Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
    
    // Override DNS with Cloudflare and Google
    WiFi.setDNS(IPAddress(1, 1, 1, 1), IPAddress(8, 8, 8, 8));
    delay(500);
    
    Serial.println("\n=== Getting NTP time ===");
    
    // Use Google NTP (very reliable) with pool.ntp.org as backup
    IPAddress ntpServer1(216, 239, 35, 0);   // time.google.com
    IPAddress ntpServer2(216, 239, 35, 4);   // time2.google.com
    
    // Try DNS for pool.ntp.org as alternative
    IPAddress poolServer;
    if (WiFi.hostByName("pool.ntp.org", poolServer)) {
        ntpServer2 = poolServer;
        Serial.printf("Using: time.google.com + pool.ntp.org (%s)\n", poolServer.toString().c_str());
    } else {
        Serial.println("Using: time.google.com (primary + backup)");
    }
    
    NTP.begin(ntpServer1, ntpServer2);
    Serial.println("NTP initialized, waiting for sync...");
    delay(1000);
    
    // Wait for valid time - up to 90 seconds total with periodic status
    time_t now = time(nullptr);
    int totalWait = 0;
    const int maxWait = 90;  // 90 seconds max
    
    Serial.print("Syncing: ");
    while (now < 1700000000 && totalWait < maxWait) {
        // Process network for 1 second
        for (int i = 0; i < 10; i++) {
            delay(100);
            yield();
        }
        totalWait++;
        now = time(nullptr);
        
        // Show progress every 5 seconds
        if (totalWait % 5 == 0) {
            Serial.printf("[%ds", totalWait);
            if (now > 0) Serial.printf(":%lld", (long long)now);
            Serial.print("] ");
        } else {
            Serial.print(".");
        }
        
        // Success! Exit early
        if (now >= 1700000000) {
            Serial.println(" OK!");
            break;
        }
    }
    
    if (now < 1700000000) {
        Serial.println("\nNTP sync FAILED!");
        WiFi.disconnect(true);
        return false;
    }
    
    Serial.printf("NTP sync successful after %d seconds\n", totalWait);
    Serial.flush();
    
    // Got NTP time - calibrate drift and set time
    Serial.println("Calling sleep_calibrate_drift...");
    Serial.flush();
    
    uint64_t now_ms = (uint64_t)now * 1000;
    sleep_calibrate_drift(now_ms);
    
    Serial.printf("Drift correction: %d ppm\n", sleep_get_drift_ppm());
    Serial.flush();
    
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    Serial.print("Current time: ");
    Serial.println(asctime(&timeinfo));
    Serial.printf("Epoch: %lld\n", (long long)now);
    Serial.flush();
    
    // Save NTP sync time to EEPROM
    if (eeprom.isPresent()) {
        eeprom.setLastNtpSync((uint32_t)now);
        Serial.println("NTP sync time saved to EEPROM");
    }
    
    // Keep WiFi connected - we may need it for AI image generation
    // WiFi will be disconnected after display update
    Serial.println("WiFi staying connected for potential API calls");
    
    return true;
}

// ================================================================
// Format time from powman timer (ms since epoch) to readable string
// ================================================================
void formatTime(uint64_t time_ms, char* buf, size_t len) {
    time_t time_sec = (time_t)(time_ms / 1000);
    struct tm* timeinfo = gmtime(&time_sec);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S UTC", timeinfo);
}

void doDisplayUpdate(int updateNumber);  // Forward declaration

// Track how many updates we've done (stored in scratch register 1)
#define UPDATE_COUNT_REG 1

// NTP resync interval - LPOSC drifts ~1-5%, so resync periodically
// Every 10 updates at 10s sleep = ~100s between syncs
#define NTP_RESYNC_INTERVAL  10  // Resync every N updates

int getUpdateCount() {
    return (int)powman_hw->scratch[UPDATE_COUNT_REG];
}

void setUpdateCount(int count) {
    powman_hw->scratch[UPDATE_COUNT_REG] = (uint32_t)count;
}

void setup() {
    // Initialize serial for debugging
    Serial.begin(115200);
    
    // Wait for serial connection
    // After deep sleep, USB needs time to re-enumerate (can take 5-10 seconds)
    // Blink LED to show we're alive while waiting
    pinMode(LED_BUILTIN, OUTPUT);
    uint32_t startWait = millis();
    while (!Serial && (millis() - startWait < 10000)) {
        digitalWrite(LED_BUILTIN, (millis() / 200) % 2);  // Fast blink while waiting
        delay(50);
    }
    digitalWrite(LED_BUILTIN, HIGH);  // LED on when serial ready
    delay(500);  // Extra delay for serial to stabilize
    
    // Immediate sign of life
    Serial.println("\n\n>>> BOOT <<<");
    Serial.printf("Serial ready after %lu ms\n", millis() - startWait);
    Serial.flush();
    delay(100);
    
    // ================================================================
    // EARLY BOOT TIMER DIAGNOSTICS
    // Capture timer state before anything else touches it
    // ================================================================
    uint64_t boot_timer_value = powman_timer_get_ms();
    bool boot_timer_running = powman_timer_is_running();
    bool boot_using_lposc = (powman_hw->timer & POWMAN_TIMER_USING_LPOSC_BITS) != 0;
    bool boot_using_xosc = (powman_hw->timer & POWMAN_TIMER_USING_XOSC_BITS) != 0;
    uint32_t boot_millis = millis();
    
    Serial.println("=== EARLY BOOT TIMER STATE ===");
    Serial.printf("  powman timer: %llu ms\n", boot_timer_value);
    Serial.printf("  timer running: %d\n", boot_timer_running);
    Serial.printf("  using LPOSC: %d, using XOSC: %d\n", boot_using_lposc, boot_using_xosc);
    Serial.printf("  Arduino millis(): %lu\n", boot_millis);
    Serial.printf("  powman_hw->timer raw: 0x%08lx\n", (unsigned long)powman_hw->timer);
    Serial.println("==============================");
    Serial.flush();
    delay(100);
    
    // ================================================================
    // Initialize DS3231 RTC if present
    // ================================================================
    Serial.println("\n=== Checking for DS3231 RTC ===");
    Serial.printf("  I2C pins: SDA=%d, SCL=%d, INT=%d\n", PIN_RTC_SDA, PIN_RTC_SCL, PIN_RTC_INT);
    Serial.flush();
    delay(100);
    
    Serial.println("  Calling sleep_init_rtc...");
    Serial.flush();
    
    bool hasRTC = sleep_init_rtc(PIN_RTC_SDA, PIN_RTC_SCL, PIN_RTC_INT);
    if (hasRTC) {
        Serial.println("DS3231 RTC found - using for timekeeping");
        // Read current RTC time
        uint64_t rtcTime = sleep_get_time_ms();
        char timeBuf[32];
        formatTime(rtcTime, timeBuf, sizeof(timeBuf));
        Serial.printf("  RTC time: %s\n", timeBuf);
        Serial.printf("  Temperature: %.1f°C\n", rtc.getTemperature());
        
        // Initialize EEPROM (on same I2C bus)
        if (eeprom.begin(&Wire1, 0x57)) {
            eeprom.printStatus();
            
            // Report what happened in the previous session
            reportLastUpdate();
            
            // Log temperature
            eeprom.logTemperature(rtc.getTemperature());
            
            // Debug: verify EEPROM still readable after logTemperature
            Serial.println("--- After logTemperature ---");
            uint8_t test1 = eeprom.readByte(0x0100);
            Serial.printf("  Read 0x0100 = 0x%02X ('%c')\n", test1, (test1 >= 32 && test1 < 127) ? test1 : '?');
        }
    } else {
        Serial.println("No DS3231 found - using LPOSC (less accurate)");
    }
    Serial.println("===============================\n");
    Serial.flush();
    
    // Get current update count (from powman scratch for this session)
    int updateCount = sleep_woke_from_deep_sleep() ? getUpdateCount() : 0;
    uint32_t uptime = sleep_get_uptime_seconds();
    
    // Increment persistent boot count in EEPROM (survives power loss)
    uint32_t totalBoots = 0;
    if (eeprom.isPresent()) {
        eeprom.incrementBootCount();
        totalBoots = eeprom.getBootCount();
        
        // Debug: verify EEPROM still readable after write
        Serial.println("--- EEPROM read test after incrementBootCount ---");
        uint8_t testByte = eeprom.readByte(0x0100);  // EEPROM_WIFI_SSID
        Serial.printf("  Direct read of 0x0100 = 0x%02X ('%c')\n", 
                      testByte, (testByte >= 32 && testByte < 127) ? testByte : '?');
    }
    
    // ================================================================
    // WiFi credential management
    // ================================================================
    Serial.println("\n--- WiFi Credential Check ---");
    
    // Debug: check EEPROM object state
    eeprom.debugState();
    
    // Try a direct read first WITHOUT any I2C reinit
    Serial.println("  Direct read attempt 1:");
    uint8_t directTest = eeprom.readByte(0x0100);
    Serial.printf("  0x0100 = 0x%02X ('%c')\n", directTest, (directTest >= 32 && directTest < 127) ? directTest : '?');
    
    // If we got 0xFF, the bus might be stuck - try to recover
    if (directTest == 0xFF && eeprom.isPresent()) {
        Serial.println("  Got 0xFF - trying I2C bus recovery...");
        
        // Toggle SDA/SCL to try to unstick any slave device
        Wire1.end();
        delay(5);
        
        // Manually toggle SCL to free any stuck slave
        pinMode(3, OUTPUT);  // SCL
        for (int i = 0; i < 16; i++) {
            digitalWrite(3, HIGH);
            delayMicroseconds(50);
            digitalWrite(3, LOW);
            delayMicroseconds(50);
        }
        digitalWrite(3, HIGH);
        delay(5);
        
        // Reinit I2C
        Wire1.setSDA(2);
        Wire1.setSCL(3);
        Wire1.begin();
        Wire1.setClock(100000);
        delay(10);
        
        // Try again
        Serial.println("  Direct read attempt 2 after recovery:");
        directTest = eeprom.readByte(0x0100);
        Serial.printf("  0x0100 = 0x%02X ('%c')\n", directTest, (directTest >= 32 && directTest < 127) ? directTest : '?');
    }
    
    Serial.printf("eeprom.isPresent() = %d\n", eeprom.isPresent());
    if (eeprom.isPresent()) {
        Serial.printf("eeprom.hasWifiCredentials() = %d\n", eeprom.hasWifiCredentials());
    }
    Serial.flush();
    
    // Check for config mode (only on cold boot, not wake from sleep)
    checkConfigMode();
    
    // Load WiFi credentials
    if (!loadWifiCredentials()) {
        Serial.println("No WiFi credentials - entering config mode");
        enterConfigMode();
        
        // Check again after config
        if (!loadWifiCredentials()) {
            Serial.println("WARNING: Still no WiFi credentials, NTP sync will fail");
        }
    }
    
    // ================================================================
    // Check if we woke from deep sleep
    // ================================================================
    bool needsNtpSync = false;
    
    if (sleep_woke_from_deep_sleep()) {
        Serial.println("\n\n========================================");
        Serial.printf("*** WOKE FROM DEEP SLEEP! (update #%d) ***\n", updateCount + 1);
        if (eeprom.isPresent()) {
            Serial.printf("*** Total boots (EEPROM): %lu ***\n", totalBoots);
        }
        Serial.printf("*** RTC uptime: %lu seconds ***\n", uptime);
        if (hasRTC) {
            Serial.println("*** Wake source: DS3231 RTC alarm ***");
            // Clear the DS3231 alarm flag
            rtc.clearAlarm1();
        } else {
            Serial.println("*** Wake source: LPOSC timer ***");
        }
        Serial.println("========================================\n");
        
        // Clear the wake flag
        sleep_clear_wake_flag();
        
        // With DS3231: NTP resync much less often (crystal accurate ~2ppm)
        // Without: Resync every NTP_RESYNC_INTERVAL updates (LPOSC drifts ~1-5%)
        int resyncInterval = hasRTC ? 100 : NTP_RESYNC_INTERVAL;  // 100 = ~1000 seconds
        
        if ((updateCount + 1) % resyncInterval == 0) {
            Serial.println(">>> Periodic NTP resync <<<");
            needsNtpSync = true;
        }
    } else {
        // First boot
        Serial.println("\n\n===========================================");
        Serial.println("EL133UF1 13.3\" Spectra 6 E-Ink Display Demo");
        Serial.println("===========================================\n");
        
        // Check if DS3231 already has valid time (battery-backed)
        // Note: RTC time before 1970 returns garbage when cast to uint64_t
        // So we check the raw time_t value from the RTC
        if (hasRTC) {
            time_t rtcTimeSec = (time_t)(sleep_get_time_ms() / 1000);
            // Valid if between 2020 and 2100
            if (rtcTimeSec > 1577836800 && rtcTimeSec < 4102444800) {
                Serial.println("DS3231 already has valid time from battery backup");
                needsNtpSync = false;
            } else {
                Serial.println("DS3231 time is invalid, need NTP sync");
                needsNtpSync = true;
            }
        } else {
            needsNtpSync = true;
        }
        setUpdateCount(0);
    }
    
    // Sync NTP if needed (cold boot or periodic resync)
    if (needsNtpSync) {
        uint64_t oldTime = sleep_get_time_ms();
        if (connectWiFiAndGetNTP()) {
            uint64_t newTime = sleep_get_time_ms();
            int64_t drift = (int64_t)(newTime - oldTime);
            if (oldTime > 1700000000000ULL) {
                Serial.printf(">>> Time correction: %+lld ms <<<\n", drift);
            }
        } else {
            Serial.println("WARNING: NTP sync failed, using existing time");
            if (sleep_get_time_ms() < 1700000000000ULL) {
                Serial.println("ERROR: No valid time available!");
            }
        }
    }
    
    // ================================================================
    // Common setup
    // ================================================================

    // Check memory availability
    Serial.println("Memory check:");
    Serial.printf("  Total heap: %d bytes\n", rp2040.getTotalHeap());
    Serial.printf("  Free heap:  %d bytes\n", rp2040.getFreeHeap());
    
    // Check PSRAM availability (critical for this display!)
    size_t psramSize = rp2040.getPSRAMSize();
    Serial.printf("  PSRAM size: %d bytes", psramSize);
    if (psramSize > 0) {
        Serial.printf(" (%d MB)\n", psramSize / (1024*1024));
        // Show PSRAM clock speed
        uint32_t sysClk = rp2040.f_cpu();
        Serial.printf("  System clock: %lu MHz\n", sysClk / 1000000);
        #ifdef RP2350_PSRAM_MAX_SCK_HZ
        Serial.printf("  PSRAM max: %d MHz (divisor ~%lu)\n", 
                      RP2350_PSRAM_MAX_SCK_HZ / 1000000,
                      (sysClk + RP2350_PSRAM_MAX_SCK_HZ - 1) / RP2350_PSRAM_MAX_SCK_HZ);
        #endif
    } else {
        Serial.println(" (NOT DETECTED!)");
        Serial.println("\n  WARNING: No PSRAM detected!");
        Serial.println("  This display requires ~2MB PSRAM for the frame buffer.");
    }
    
    // Quick test of pmalloc
    void* testPsram = pmalloc(1024);
    if (testPsram) {
        Serial.printf("  pmalloc test: OK at %p\n", testPsram);
        free(testPsram);
    } else {
        Serial.println("  pmalloc test: FAILED - PSRAM not working!");
    }
    
    Serial.println("\nPico Plus 2 W Pin Configuration:");
    Serial.printf("  SPI SCK:  GP%d\n", PIN_SPI_SCK);
    Serial.printf("  SPI MOSI: GP%d\n", PIN_SPI_MOSI);
    Serial.printf("  CS0:      GP%d\n", PIN_CS0);
    Serial.printf("  CS1:      GP%d\n", PIN_CS1);
    Serial.printf("  DC:       GP%d\n", PIN_DC);
    Serial.printf("  RESET:    GP%d\n", PIN_RESET);
    Serial.printf("  BUSY:     GP%d\n", PIN_BUSY);
    Serial.println();

    // Do display update (handles SPI/display/TTF initialization internally)
    updateCount++;
    setUpdateCount(updateCount);
    doDisplayUpdate(updateCount);
    
    // Calculate sleep until next even minute
    time_t now = rtc.getTime();
    struct tm* tm = gmtime(&now);
    
    // Calculate seconds until next even minute
    int currentMin = tm->tm_min;
    int currentSec = tm->tm_sec;
    int nextEvenMin = (currentMin % 2 == 0) ? currentMin + 2 : currentMin + 1;
    int secsUntilNextEven = (nextEvenMin - currentMin) * 60 - currentSec;
    
    // Handle edge case: if we're very close to the next even minute, skip to the one after
    // Need at least 15 seconds to wake up and start the display update
    if (secsUntilNextEven < 15) {
        secsUntilNextEven += 120;  // Add 2 minutes
    }
    
    uint32_t sleepMs = secsUntilNextEven * 1000;
    
    Serial.printf("\n=== Entering deep sleep until next even minute ===\n");
    Serial.printf("Current time: %02d:%02d:%02d\n", tm->tm_hour, tm->tm_min, tm->tm_sec);
    Serial.printf("Sleep duration: %d seconds (until xx:%02d:00)\n", 
                  secsUntilNextEven, (currentMin + (secsUntilNextEven / 60)) % 60);
    Serial.println("Using RP2350 powman - TRUE deep sleep (core powers down)");
    
    // Verify RTC is still responding before sleep (catch I2C lockup)
    if (sleep_has_rtc()) {
        time_t rtcTime = rtc.getTime();
        if (rtcTime < 1700000000) {
            Serial.println("WARNING: RTC not responding or time invalid!");
            Serial.println("Attempting I2C bus recovery...");
            Wire1.end();
            delay(10);
            Wire1.setSDA(PIN_RTC_SDA);
            Wire1.setSCL(PIN_RTC_SCL);
            Wire1.begin();
            Wire1.setClock(100000);
            delay(10);
            rtcTime = rtc.getTime();
            if (rtcTime < 1700000000) {
                Serial.println("ERROR: RTC still not responding after I2C recovery!");
                Serial.println("Cannot safely enter sleep - hanging here");
                while(1) {
                    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
                    delay(100);  // Fast blink indicates error
                }
            }
        }
        Serial.printf("RTC verified OK: %lu\n", (unsigned long)rtcTime);
    }
    
    Serial.flush();
    delay(100);
    
    // Prepare for deep sleep
    if (sleep_has_rtc()) {
        // DS3231 handles wake via alarm - no LPOSC setup needed
        Serial.println("Using DS3231 RTC for wake timing");
    } else {
        // LPOSC fallback - need to configure powman timer
        Serial.println("Using LPOSC for wake timing (preparing timer...)");
        sleep_run_from_lposc();
    }
    
    // Go to deep sleep until next even minute
    sleep_goto_dormant_for_ms(sleepMs);
    
    // We should never reach here
    Serial.println("ERROR: Should not reach here after deep sleep!");
    while(1) delay(1000);
}

// ================================================================
// Perform a display update (called on each wake cycle)
// ================================================================
// Expected time for FULL display update cycle (from reading time to display complete)
// This includes: init (~1.5s) + drawing (~0.5s) + rotate/pack (~0.7s) + SPI (~0.5s) + panel refresh (~20-32s)
// First update after power-on may be slightly slower due to panel warmup
#define DISPLAY_REFRESH_COLD_MS  32000  // First update after power-on
#define DISPLAY_REFRESH_WARM_MS  28000  // Subsequent updates

// Stage codes for EEPROM logging
#define STAGE_START       0x01
#define STAGE_PSRAM_OK    0x02
#define STAGE_DISPLAY_OK  0x03
#define STAGE_TTF_OK      0x04
#define STAGE_DRAWING     0x05
#define STAGE_UPDATING    0x06
#define STAGE_COMPLETE    0x07
#define STAGE_ERROR       0xFF

void logStage(uint8_t stage) {
    if (eeprom.isPresent()) {
        eeprom.writeByte(EEPROM_LAST_STAGE, stage);
    }
}

void logUpdateInfo(uint16_t updateNum, uint32_t wakeTime) {
    if (eeprom.isPresent()) {
        eeprom.writeUInt16(EEPROM_LAST_UPDATE, updateNum);
        eeprom.writeUInt32(EEPROM_LAST_WAKE_TIME, wakeTime);
    }
}

void reportLastUpdate() {
    if (eeprom.isPresent()) {
        uint8_t lastStage = eeprom.readByte(EEPROM_LAST_STAGE);
        uint16_t lastUpdate = eeprom.readUInt16(EEPROM_LAST_UPDATE);
        uint32_t lastWakeTime = eeprom.readUInt32(EEPROM_LAST_WAKE_TIME);
        
        Serial.println("=== Previous Session Info ===");
        Serial.printf("  Last update #: %u\n", lastUpdate);
        Serial.printf("  Last stage:    0x%02X", lastStage);
        switch(lastStage) {
            case 0x01: Serial.println(" (START)"); break;
            case 0x02: Serial.println(" (PSRAM_OK)"); break;
            case 0x03: Serial.println(" (DISPLAY_OK)"); break;
            case 0x04: Serial.println(" (TTF_OK)"); break;
            case 0x05: Serial.println(" (DRAWING)"); break;
            case 0x06: Serial.println(" (UPDATING)"); break;
            case 0x07: Serial.println(" (COMPLETE)"); break;
            case 0xFF: Serial.println(" (ERROR)"); break;
            default: Serial.println(" (unknown)"); break;
        }
        if (lastWakeTime > 1700000000) {
            // Valid unix time
            time_t t = (time_t)lastWakeTime;
            struct tm* tm = gmtime(&t);
            Serial.printf("  Last wake:     %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                         tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                         tm->tm_hour, tm->tm_min, tm->tm_sec);
        }
        Serial.println("=============================");
    }
}

void doDisplayUpdate(int updateNumber) {
    Serial.printf("\n=== Display Update #%d ===\n", updateNumber);
    logStage(STAGE_START);
    
    // Log this update's info for post-mortem analysis
    uint64_t now_ms = sleep_get_corrected_time_ms();
    logUpdateInfo((uint16_t)updateNumber, (uint32_t)(now_ms / 1000));
    
    // Get current time with drift correction applied
    char timeStr[32];
    formatTime(now_ms, timeStr, sizeof(timeStr));
    Serial.printf("Drift correction: %d ppm\n", sleep_get_drift_ppm());
    Serial.printf("Current time: %s\n", timeStr);
    
    // Predict what time it will be when the display finishes refreshing
    // First update needs init sequence (~1.7s extra), subsequent updates can skip it
    bool isColdBoot = (updateNumber == 1);
    uint32_t expectedRefreshMs = isColdBoot ? DISPLAY_REFRESH_COLD_MS : DISPLAY_REFRESH_WARM_MS;
    
    uint64_t display_time_ms = now_ms + expectedRefreshMs;
    char displayTimeStr[32];
    formatTime(display_time_ms, displayTimeStr, sizeof(displayTimeStr));
    Serial.printf("Display will show: %s (compensating +%lu ms, %s)\n", 
                  displayTimeStr, expectedRefreshMs, 
                  isColdBoot ? "cold boot" : "warm update");
    
    // Reinitialize SPI
    SPI1.setSCK(PIN_SPI_SCK);
    SPI1.setTX(PIN_SPI_MOSI);
    SPI1.begin();
    
    // Check PSRAM availability after wake
    Serial.println("Checking PSRAM...");
    size_t psramSize = rp2040.getPSRAMSize();
    Serial.printf("  PSRAM size: %u bytes (%u MB)\n", psramSize, psramSize / (1024*1024));
    
    if (psramSize == 0) {
        Serial.println("  ERROR: PSRAM not detected after wake!");
        logStage(STAGE_ERROR);
        // Blink error: 1 long blink
        digitalWrite(LED_BUILTIN, HIGH);
        delay(1000);
        digitalWrite(LED_BUILTIN, LOW);
        delay(500);
    }
    
    // Test PSRAM accessibility
    void* testPtr = pmalloc(1024);
    if (testPtr) {
        memset(testPtr, 0xAA, 1024);
        uint8_t* p = (uint8_t*)testPtr;
        bool ok = (p[0] == 0xAA && p[512] == 0xAA && p[1023] == 0xAA);
        Serial.printf("  PSRAM alloc test: %s (ptr=%p)\n", ok ? "OK" : "FAILED", testPtr);
        free(testPtr);
        if (!ok) {
            Serial.println("  ERROR: PSRAM read/write failed!");
            logStage(STAGE_ERROR);
            return;
        }
    } else {
        Serial.println("  PSRAM alloc test: ALLOCATION FAILED!");
        logStage(STAGE_ERROR);
        return;
    }
    logStage(STAGE_PSRAM_OK);
    
    // Always do full display initialization
    Serial.println("Initializing display...");
    if (!display.begin(PIN_CS0, PIN_CS1, PIN_DC, PIN_RESET, PIN_BUSY)) {
        Serial.println("ERROR: Display initialization failed!");
        logStage(STAGE_ERROR);
        // Blink error pattern: 2 blinks
        for (int i = 0; i < 10; i++) {
            digitalWrite(LED_BUILTIN, (i < 4) ? (i % 2) : LOW);
            delay(200);
        }
        return;
    }
    Serial.printf("Display buffer: %p\n", display.getBuffer());
    logStage(STAGE_DISPLAY_OK);
    
    // Initialize TTF renderer
    Serial.println("Initializing TTF...");
    ttf.begin(&display);
    if (!ttf.loadFont(opensans_ttf, opensans_ttf_len)) {
        Serial.println("ERROR: TTF font load failed!");
        logStage(STAGE_ERROR);
    }
    logStage(STAGE_TTF_OK);
    
    // Enable glyph cache for time display (160px digits)
    ttf.enableGlyphCache(160.0, "0123456789: ");
    
    // Initialize PNG decoder
    png.begin(&display);
    
    // Draw update info with performance profiling
    uint32_t drawStart = millis();
    uint32_t t0, t1;
    uint32_t ttfTotal = 0, bitmapTotal = 0;
    
    // ================================================================
    // BACKGROUND - AI-generated image (or fallback to solid color)
    // ================================================================
    
    // Generate new AI image on first boot, or if we don't have one cached
    bool needNewImage = (aiImageData == nullptr);
    
    // Check if we have an API key configured
    char apiKey[64] = {0};
    bool hasApiKey = eeprom.isPresent() && eeprom.hasOpenAIKey() && 
                     eeprom.getOpenAIKey(apiKey, sizeof(apiKey));
    
    // Debug: show AI image generation status
    Serial.println("--- AI Image Status ---");
    Serial.printf("  Need new image: %s\n", needNewImage ? "YES" : "NO (cached)");
    Serial.printf("  EEPROM present: %s\n", eeprom.isPresent() ? "YES" : "NO");
    Serial.printf("  Has API key: %s\n", hasApiKey ? "YES" : "NO");
    Serial.printf("  WiFi status: %d (connected=%d)\n", WiFi.status(), WL_CONNECTED);
    if (hasApiKey) {
        Serial.printf("  API key: %.7s...%s\n", apiKey, apiKey + strlen(apiKey) - 4);
    }
    
    if (needNewImage && hasApiKey && WiFi.status() == WL_CONNECTED) {
        Serial.println("Generating AI background image...");
        
        // Initialize OpenAI client
        openai.begin(apiKey);
        openai.setModel(DALLE_3);
        openai.setSize(DALLE_1024x1024);
        openai.setQuality(DALLE_STANDARD);
        
        // Prompt optimized for Spectra 6 display (6 colors: black, white, red, yellow, blue, green)
        const char* prompt = 
            "A beautiful nature scene designed for a 6-color e-ink display. "
            "Use ONLY these colors: pure black, pure white, bright red, bright yellow, "
            "bright blue, and bright green. No gradients, no shading, no intermediate colors. "
            "Bold graphic style like a vintage travel poster or woodblock print. "
            "High contrast with clear separation between color regions. "
            "Simple shapes, no fine details. A serene forest landscape with mountains.";
        
        t0 = millis();
        OpenAIResult result = openai.generate(prompt, &aiImageData, &aiImageLen, 90000);
        t1 = millis() - t0;
        
        if (result == OPENAI_OK && aiImageData != nullptr) {
            Serial.printf("  AI image generated: %zu bytes in %lu ms\n", aiImageLen, t1);
        } else {
            Serial.printf("  AI generation failed: %s\n", openai.getLastError());
            aiImageData = nullptr;
            aiImageLen = 0;
        }
    } else if (needNewImage) {
        // Explain why we're not generating
        if (!hasApiKey) {
            Serial.println("  Skipping AI generation: No API key configured");
            Serial.println("  (Press 'c' on boot to configure)");
        } else if (WiFi.status() != WL_CONNECTED) {
            Serial.println("  Skipping AI generation: WiFi not connected");
        }
    } else {
        Serial.printf("  Using cached AI image: %zu bytes\n", aiImageLen);
    }
    
    // Draw the background
    t0 = millis();
    if (aiImageData != nullptr && aiImageLen > 0) {
        // Draw AI-generated PNG
        PNGResult pngResult = png.drawFullscreen(aiImageData, aiImageLen);
        bitmapTotal = millis() - t0;
        if (pngResult != PNG_OK) {
            Serial.printf("  PNG error: %s\n", png.getErrorString(pngResult));
            display.clear(EL133UF1_WHITE);
        }
        Serial.printf("  PNG background: %lu ms\n", bitmapTotal);
    } else {
        // No AI image available - use a simple colored background
        display.clear(EL133UF1_WHITE);
        
        // Draw a simple decorative pattern using the 6 colors
        int bandHeight = display.height() / 6;
        uint8_t colors[] = {EL133UF1_RED, EL133UF1_YELLOW, EL133UF1_GREEN, 
                            EL133UF1_BLUE, EL133UF1_WHITE, EL133UF1_BLACK};
        for (int i = 0; i < 6; i++) {
            display.fillRect(0, i * bandHeight, display.width(), bandHeight / 4, colors[i]);
        }
        bitmapTotal = millis() - t0;
        Serial.printf("  Fallback background: %lu ms\n", bitmapTotal);
        
        if (!hasApiKey) {
            Serial.println("  (No OpenAI API key configured - press 'c' on boot to set)");
        }
    }
    
    // ================================================================
    // TIME - Large outlined text, centered
    // ================================================================
    
    // Display the PREDICTED time (what it will be when refresh completes)
    time_t time_sec = (time_t)(display_time_ms / 1000);
    struct tm* tm = gmtime(&time_sec);
    char timeBuf[16];
    strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", tm);
    
    // Time - large outlined text for readability on any background
    t0 = millis();
    ttf.drawTextAlignedOutlined(display.width() / 2, display.height() / 2 - 50, timeBuf, 160.0,
                                 EL133UF1_WHITE, EL133UF1_BLACK,
                                 ALIGN_CENTER, ALIGN_MIDDLE, 3);
    t1 = millis() - t0;
    ttfTotal += t1;
    Serial.printf("  TTF time 160px: %lu ms\n", t1);
    
    // ================================================================
    // DATE - Below time, also outlined for readability
    // ================================================================
    char dateBuf[32];
    strftime(dateBuf, sizeof(dateBuf), "%A, %d %B %Y", tm);
    t0 = millis();
    ttf.drawTextAlignedOutlined(display.width() / 2, display.height() / 2 + 100, dateBuf, 48.0,
                                 EL133UF1_WHITE, EL133UF1_BLACK,
                                 ALIGN_CENTER, ALIGN_TOP, 2);
    t1 = millis() - t0;
    ttfTotal += t1;
    Serial.printf("  TTF date 48px:  %lu ms\n", t1);
    
    // ================================================================
    // BATTERY - Bottom right corner, outlined
    // ================================================================
    char buf[64];
    float batteryV = readBatteryVoltage();
    int batteryPct = getBatteryPercent(batteryV);
    Serial.printf("  Battery: %.2fV (%d%%)\n", batteryV, batteryPct);
    
    snprintf(buf, sizeof(buf), "%.1fV %d%%", batteryV, batteryPct);
    t0 = millis();
    ttf.drawTextAlignedOutlined(display.width() - 30, display.height() - 30, buf, 36.0,
                                 EL133UF1_WHITE, EL133UF1_BLACK,
                                 ALIGN_RIGHT, ALIGN_BOTTOM, 2);
    t1 = millis() - t0;
    ttfTotal += t1;
    Serial.printf("  TTF battery:    %lu ms\n", t1);
    
    Serial.printf("--- Drawing summary ---\n");
    Serial.printf("  TTF total:      %lu ms\n", ttfTotal);
    Serial.printf("  Bitmap total:   %lu ms\n", bitmapTotal);
    Serial.printf("  All drawing:    %lu ms\n", millis() - drawStart);
    logStage(STAGE_DRAWING);
    
    // Update display and measure actual refresh time
    // Always skip init in update() since begin() already ran it
    Serial.println("Starting display.update()...");
    Serial.flush();
    logStage(STAGE_UPDATING);
    
    // LED feedback: turn off during update, on when done
    digitalWrite(LED_BUILTIN, LOW);
    
    uint32_t refreshStart = millis();
    display.update(true);  // skipInit=true - begin() handles init
    
    digitalWrite(LED_BUILTIN, HIGH);  // LED on = update complete
    Serial.println("display.update() complete.");
    logStage(STAGE_COMPLETE);
    uint32_t actualRefreshMs = millis() - refreshStart;
    
    // Get actual time now for comparison (with drift correction)
    uint64_t actual_now_ms = sleep_get_corrected_time_ms();
    char actualTimeStr[32];
    formatTime(actual_now_ms, actualTimeStr, sizeof(actualTimeStr));
    
    Serial.printf("Update #%d complete.\n", updateNumber);
    Serial.printf("  Displayed time: %s\n", displayTimeStr);
    Serial.printf("  Actual time:    %s\n", actualTimeStr);
    Serial.printf("  Refresh took:   %lu ms (predicted %lu ms)\n", 
                  actualRefreshMs, expectedRefreshMs);
    
    int32_t errorMs = (int32_t)(actual_now_ms - display_time_ms);
    const char* accuracy = (abs(errorMs) < 2000) ? "excellent" : 
                           (abs(errorMs) < 5000) ? "good" : "acceptable";
    Serial.printf("  Display vs actual: %+ld ms (%s)\n", errorMs, accuracy);
}

void loop() {
    // Nothing to do in loop for this demo
    delay(10000);
}

/**
 * @brief Draw a demonstration pattern showing all 6 colors
 */
void drawDemoPattern() {
    Serial.println("Drawing orientation test pattern...");
    
    const uint16_t w = display.width();   // 1600
    const uint16_t h = display.height();  // 1200
    
    // Clear to white
    display.clear(EL133UF1_WHITE);
    
    // Draw a black border around the whole display
    for (int i = 0; i < 5; i++) {
        display.drawRect(i, i, w - 2*i, h - 2*i, EL133UF1_BLACK);
    }
    
    // Text size for corner labels (size 6 = 48x48 pixels per char)
    const uint8_t textSize = 6;
    const uint16_t charW = 8 * textSize;  // 48 pixels per character
    const uint16_t charH = 8 * textSize;  // 48 pixels tall
    const uint16_t margin = 30;
    
    // Top-Left corner label
    display.fillRect(margin, margin, charW * 8 + 20, charH + 20, EL133UF1_WHITE);
    display.drawRect(margin, margin, charW * 8 + 20, charH + 20, EL133UF1_BLACK);
    display.drawText(margin + 10, margin + 10, "TOP-LEFT", EL133UF1_BLACK, EL133UF1_WHITE, textSize);
    
    // Top-Right corner label  
    uint16_t trX = w - margin - (charW * 9 + 20);
    display.fillRect(trX, margin, charW * 9 + 20, charH + 20, EL133UF1_WHITE);
    display.drawRect(trX, margin, charW * 9 + 20, charH + 20, EL133UF1_BLACK);
    display.drawText(trX + 10, margin + 10, "TOP-RIGHT", EL133UF1_BLACK, EL133UF1_WHITE, textSize);
    
    // Bottom-Left corner label
    uint16_t blY = h - margin - (charH + 20);
    display.fillRect(margin, blY, charW * 11 + 20, charH + 20, EL133UF1_WHITE);
    display.drawRect(margin, blY, charW * 11 + 20, charH + 20, EL133UF1_BLACK);
    display.drawText(margin + 10, blY + 10, "BOTTOM-LEFT", EL133UF1_BLACK, EL133UF1_WHITE, textSize);
    
    // Bottom-Right corner label
    uint16_t brX = w - margin - (charW * 12 + 20);
    display.fillRect(brX, blY, charW * 12 + 20, charH + 20, EL133UF1_WHITE);
    display.drawRect(brX, blY, charW * 12 + 20, charH + 20, EL133UF1_BLACK);
    display.drawText(brX + 10, blY + 10, "BOTTOM-RIGHT", EL133UF1_BLACK, EL133UF1_WHITE, textSize);
    
    // Draw colored corners to make orientation obvious
    // Top-left: RED square
    display.fillRect(margin, margin + charH + 40, 100, 100, EL133UF1_RED);
    display.drawText(margin, margin + charH + 150, "RED", EL133UF1_RED, EL133UF1_WHITE, 3);
    
    // Top-right: BLUE square
    display.fillRect(w - margin - 100, margin + charH + 40, 100, 100, EL133UF1_BLUE);
    display.drawText(w - margin - 100, margin + charH + 150, "BLUE", EL133UF1_BLUE, EL133UF1_WHITE, 3);
    
    // Bottom-left: GREEN square
    display.fillRect(margin, blY - 150, 100, 100, EL133UF1_GREEN);
    display.drawText(margin, blY - 170, "GREEN", EL133UF1_GREEN, EL133UF1_WHITE, 3);
    
    // Bottom-right: YELLOW square
    display.fillRect(w - margin - 100, blY - 150, 100, 100, EL133UF1_YELLOW);
    display.drawText(w - margin - 140, blY - 170, "YELLOW", EL133UF1_YELLOW, EL133UF1_WHITE, 3);
    
    // Center info
    const char* centerText1 = "EL133UF1 Display";
    const char* centerText2 = "1600 x 1200 pixels";
    uint16_t cx = w / 2;
    uint16_t cy = h / 2;
    
    display.drawText(cx - (16 * 8 * 4) / 2, cy - 50, centerText1, EL133UF1_BLACK, EL133UF1_WHITE, 4);
    display.drawText(cx - (18 * 8 * 3) / 2, cy + 30, centerText2, EL133UF1_BLACK, EL133UF1_WHITE, 3);
    
    // Draw arrows pointing to edges
    // Arrow pointing UP at top center
    int16_t arrowX = cx;
    int16_t arrowY = 150;
    for (int i = 0; i < 30; i++) {
        display.drawHLine(arrowX - i, arrowY + i, i * 2 + 1, EL133UF1_BLACK);
    }
    display.drawText(arrowX - 24, arrowY + 40, "UP", EL133UF1_BLACK, EL133UF1_WHITE, 3);
    
    Serial.println("Orientation test pattern drawn to buffer");
}

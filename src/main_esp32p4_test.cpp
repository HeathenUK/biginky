/**
 * @file main_esp32p4_test.cpp
 * @brief Minimal test application for ESP32-P4 porting
 * 
 * This is a simplified version of main.cpp for testing the EL133UF1
 * display driver on ESP32-P4. It focuses only on basic display functionality
 * without WiFi, SD card, or complex power management.
 * 
 * Build with: pio run -e esp32p4_minimal
 * 
 * === PIN MAPPING FOR WAVESHARE ESP32-P4-WIFI6 ===
 * Uses same PHYSICAL pin locations as Pico Plus 2 W (form-factor compatible)
 * Configured via build flags in platformio.ini
 * 
 * Display SPI (Pico GP -> ESP32-P4 GPIO):
 *   SCLK    ->   GPIO3  (was GP10, pin 14)
 *   MOSI    ->   GPIO2  (was GP11, pin 15)
 *   CS0     ->   GPIO23 (was GP26, pin 31)
 *   CS1     ->   GPIO48 (was GP16, pin 21)
 *   DC      ->   GPIO26 (was GP22, pin 29)
 *   RESET   ->   GPIO22 (was GP27, pin 32)
 *   BUSY    ->   GPIO47 (was GP17, pin 22)
 *
 * DS3231 RTC (optional):
 *   SDA     ->   GPIO31 (was GP2, pin 4)
 *   SCL     ->   GPIO30 (was GP3, pin 5)
 *   INT     ->   GPIO46 (was GP18, pin 24)
 */

// Only compile this file for ESP32 builds
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include "platform_hal.h"
#include "EL133UF1.h"
#include "EL133UF1_TTF.h"
#include "EL133UF1_Color.h"
#include "fonts/opensans.h"
#include "DS3231.h"

// WiFi support for ESP32-P4 (via ESP32-C6 companion chip)
#if !defined(DISABLE_WIFI) || defined(ENABLE_WIFI_TEST)
#include <WiFi.h>
#define WIFI_ENABLED 1
#else
#define WIFI_ENABLED 0
#endif

// SD Card support via SDMMC
#if !defined(DISABLE_SDMMC)
#include <SD_MMC.h>
#include <FS.h>
#define SDMMC_ENABLED 1
#else
#define SDMMC_ENABLED 0
#endif

// ============================================================================
// Pin definitions for ESP32-P4
// Override these with build flags or edit for your specific board
// ============================================================================

// Defaults for Waveshare ESP32-P4-WIFI6 - matches Pico physical pin locations
#ifndef PIN_SPI_SCK
#define PIN_SPI_SCK   3     // GPIO3 = Pico GP10 (pin 14)
#endif
#ifndef PIN_SPI_MOSI
#define PIN_SPI_MOSI  2     // GPIO2 = Pico GP11 (pin 15)
#endif
#ifndef PIN_CS0
#define PIN_CS0       23    // GPIO23 = Pico GP26 (pin 31)
#endif
#ifndef PIN_CS1
#define PIN_CS1       48    // GPIO48 = Pico GP16 (pin 21)
#endif
#ifndef PIN_DC
#define PIN_DC        26    // GPIO26 = Pico GP22 (pin 29)
#endif
#ifndef PIN_RESET
#define PIN_RESET     22    // GPIO22 = Pico GP27 (pin 32)
#endif
#ifndef PIN_BUSY
#define PIN_BUSY      47    // GPIO47 = Pico GP17 (pin 22)
#endif

// RTC I2C pins
#ifndef PIN_RTC_SDA
#define PIN_RTC_SDA   31    // GPIO31 = Pico GP2 (pin 4)
#endif
#ifndef PIN_RTC_SCL
#define PIN_RTC_SCL   30    // GPIO30 = Pico GP3 (pin 5)
#endif
#ifndef PIN_RTC_INT
#define PIN_RTC_INT   46    // GPIO46 = Pico GP18 (pin 24)
#endif

// SDMMC SD Card pins (ESP32-P4 Slot 0 IOMUX pins)
#ifndef PIN_SD_CLK
#define PIN_SD_CLK    43
#endif
#ifndef PIN_SD_CMD
#define PIN_SD_CMD    44
#endif
#ifndef PIN_SD_D0
#define PIN_SD_D0     39
#endif
#ifndef PIN_SD_D1
#define PIN_SD_D1     40
#endif
#ifndef PIN_SD_D2
#define PIN_SD_D2     41
#endif
#ifndef PIN_SD_D3
#define PIN_SD_D3     42
#endif

// ============================================================================
// Global objects
// ============================================================================

// Create display instance
// On ESP32, we typically use the default SPI bus (VSPI or HSPI)
SPIClass displaySPI(HSPI);
EL133UF1 display(&displaySPI);

// TTF font renderer
EL133UF1_TTF ttf;

// ============================================================================
// WiFi Functions
// ============================================================================

#if WIFI_ENABLED
// WiFi credentials - set via serial or compile-time
static char wifiSSID[33] = "";
static char wifiPSK[65] = "";

void wifiScan() {
    Serial.println("\n=== WiFi Scan ===");
    Serial.println("Scanning for networks...");
    
    int n = WiFi.scanNetworks();
    
    if (n == 0) {
        Serial.println("No networks found!");
    } else {
        Serial.printf("Found %d networks:\n", n);
        for (int i = 0; i < n; i++) {
            Serial.printf("  %2d: %-32s  Ch:%2d  RSSI:%4d dBm  %s\n",
                         i + 1,
                         WiFi.SSID(i).c_str(),
                         WiFi.channel(i),
                         WiFi.RSSI(i),
                         (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "Open" : "Encrypted");
        }
    }
    
    WiFi.scanDelete();
    Serial.println("=================\n");
}

void wifiConnect() {
    if (strlen(wifiSSID) == 0) {
        Serial.println("No WiFi credentials set. Use 'W' to configure.");
        return;
    }
    
    Serial.printf("\n=== Connecting to WiFi ===\n");
    Serial.printf("SSID: %s\n", wifiSSID);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSSID, wifiPSK);
    
    Serial.print("Connecting");
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start < 30000)) {
        Serial.print(".");
        delay(500);
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println(" Connected!");
        Serial.printf("  IP Address: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("  Gateway:    %s\n", WiFi.gatewayIP().toString().c_str());
        Serial.printf("  DNS:        %s\n", WiFi.dnsIP().toString().c_str());
        Serial.printf("  RSSI:       %d dBm\n", WiFi.RSSI());
        Serial.printf("  Channel:    %d\n", WiFi.channel());
        Serial.printf("  MAC:        %s\n", WiFi.macAddress().c_str());
    } else {
        Serial.println(" FAILED!");
        Serial.printf("  Status: %d\n", WiFi.status());
    }
    Serial.println("==========================\n");
}

void wifiDisconnect() {
    Serial.println("\n=== Disconnecting WiFi ===");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("WiFi disconnected and radio off.");
    Serial.println("===========================\n");
}

void wifiStatus() {
    Serial.println("\n=== WiFi Status ===");
    Serial.printf("Mode: ");
    switch (WiFi.getMode()) {
        case WIFI_OFF: Serial.println("OFF"); break;
        case WIFI_STA: Serial.println("Station"); break;
        case WIFI_AP: Serial.println("Access Point"); break;
        case WIFI_AP_STA: Serial.println("AP+Station"); break;
        default: Serial.println("Unknown"); break;
    }
    
    Serial.printf("Status: ");
    switch (WiFi.status()) {
        case WL_IDLE_STATUS: Serial.println("Idle"); break;
        case WL_NO_SSID_AVAIL: Serial.println("No SSID available"); break;
        case WL_SCAN_COMPLETED: Serial.println("Scan completed"); break;
        case WL_CONNECTED: Serial.println("Connected"); break;
        case WL_CONNECT_FAILED: Serial.println("Connect failed"); break;
        case WL_CONNECTION_LOST: Serial.println("Connection lost"); break;
        case WL_DISCONNECTED: Serial.println("Disconnected"); break;
        default: Serial.printf("Unknown (%d)\n", WiFi.status()); break;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("SSID:     %s\n", WiFi.SSID().c_str());
        Serial.printf("IP:       %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("RSSI:     %d dBm\n", WiFi.RSSI());
        Serial.printf("Channel:  %d\n", WiFi.channel());
    }
    
    Serial.printf("MAC:      %s\n", WiFi.macAddress().c_str());
    Serial.println("===================\n");
}

void wifiSetCredentials() {
    Serial.println("\n=== Set WiFi Credentials ===");
    Serial.println("Enter SSID:");
    
    // Wait for input
    while (!Serial.available()) delay(10);
    delay(100);  // Wait for full input
    
    String ssid = Serial.readStringUntil('\n');
    ssid.trim();
    
    if (ssid.length() == 0) {
        Serial.println("Cancelled.");
        return;
    }
    
    strncpy(wifiSSID, ssid.c_str(), sizeof(wifiSSID) - 1);
    Serial.printf("SSID set to: %s\n", wifiSSID);
    
    Serial.println("Enter password (or empty for open network):");
    while (!Serial.available()) delay(10);
    delay(100);
    
    String psk = Serial.readStringUntil('\n');
    psk.trim();
    
    strncpy(wifiPSK, psk.c_str(), sizeof(wifiPSK) - 1);
    Serial.println("Password set.");
    Serial.println("============================\n");
    Serial.println("Use 'w' to connect with these credentials.");
}

void wifiNtpSync() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected! Connect first with 'w'");
        return;
    }
    
    Serial.println("\n=== NTP Time Sync ===");
    
    // Configure NTP
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    
    Serial.print("Waiting for NTP sync");
    time_t now = time(nullptr);
    uint32_t start = millis();
    while (now < 1700000000 && (millis() - start < 30000)) {
        Serial.print(".");
        delay(500);
        now = time(nullptr);
    }
    
    if (now >= 1700000000) {
        Serial.println(" OK!");
        struct tm* timeinfo = gmtime(&now);
        Serial.printf("UTC Time: %04d-%02d-%02d %02d:%02d:%02d\n",
                     timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                     timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
        Serial.printf("Unix timestamp: %lu\n", (unsigned long)now);
        
        // If we have RTC, sync it
        if (rtc.isPresent()) {
            rtc.setTime(now);
            Serial.println("RTC synchronized!");
        }
    } else {
        Serial.println(" FAILED!");
    }
    Serial.println("====================\n");
}

// ============================================================================
// SD Card Functions (SDMMC)
// ============================================================================

#if SDMMC_ENABLED
static bool sdCardMounted = false;

void sdDiagnostics() {
    Serial.println("\n=== SD Card Pin Diagnostics ===");
    Serial.printf("Expected IOMUX pins for Slot 0:\n");
    Serial.printf("  CLK=43, CMD=44, D0=39, D1=40, D2=41, D3=42\n");
    Serial.printf("Configured pins:\n");
    Serial.printf("  CLK=%d, CMD=%d, D0=%d, D1=%d, D2=%d, D3=%d\n",
                  PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3);
    
    // Read pin states (configure as inputs with pull-ups first)
    Serial.println("\nPin states (with internal pull-up):");
    int pins[] = {PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3};
    const char* names[] = {"CLK", "CMD", "D0", "D1", "D2", "D3"};
    
    for (int i = 0; i < 6; i++) {
        pinMode(pins[i], INPUT_PULLUP);
    }
    delay(10);
    
    for (int i = 0; i < 6; i++) {
        int state = digitalRead(pins[i]);
        Serial.printf("  GPIO%d (%s): %s\n", pins[i], names[i], state ? "HIGH" : "LOW");
    }
    
    Serial.println("\nIf all pins are HIGH, card may not be inserted or wrong pins.");
    Serial.println("If CMD/D0-D3 are LOW with card inserted, wiring is likely correct.");
    Serial.println("================================\n");
}

bool sdInit(bool mode1bit = false) {
    if (sdCardMounted) {
        Serial.println("SD card already mounted");
        return true;
    }
    
    Serial.println("\n=== Initializing SD Card (SDMMC) ===");
    Serial.printf("Pins: CLK=%d, CMD=%d, D0=%d, D1=%d, D2=%d, D3=%d\n",
                  PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3);
    
    // Set custom pins (for GPIO matrix mode)
    // Note: ESP32-P4 Slot 0 uses IOMUX, so pins must match the IOMUX pins
    if (!SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3)) {
        Serial.println("SD_MMC.setPins failed!");
        return false;
    }
    
    // Use external power (power_channel = -1) since Waveshare board has its own regulator
    // This avoids conflicts with LDO channels used by PSRAM/Flash
    SD_MMC.setPowerChannel(-1);
    Serial.println("Using external power for SD card");
    
    Serial.printf("Trying %s mode...\n", mode1bit ? "1-bit" : "4-bit");
    if (!SD_MMC.begin("/sdcard", mode1bit, false, SDMMC_FREQ_DEFAULT)) {
        Serial.println("SD_MMC.begin failed!");
        Serial.println("Error 0x107 = timeout - check if card is inserted");
        Serial.println("Make sure SD card lines have pull-up resistors");
        Serial.println("\nRun 'D' for pin diagnostics");
        return false;
    }
    
    Serial.printf("Mounted in %s mode\n", mode1bit ? "1-bit" : "4-bit");
    sdCardMounted = true;
    Serial.println("SD card mounted successfully!");
    Serial.println("==================================\n");
    return true;
}

void sdInfo() {
    if (!sdCardMounted) {
        Serial.println("SD card not mounted. Use 'M' to mount.");
        return;
    }
    
    Serial.println("\n=== SD Card Info ===");
    
    uint8_t cardType = SD_MMC.cardType();
    Serial.printf("Card Type: ");
    switch (cardType) {
        case CARD_NONE: Serial.println("No card"); break;
        case CARD_MMC: Serial.println("MMC"); break;
        case CARD_SD: Serial.println("SD"); break;
        case CARD_SDHC: Serial.println("SDHC"); break;
        default: Serial.println("Unknown"); break;
    }
    
    uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
    uint64_t totalBytes = SD_MMC.totalBytes() / (1024 * 1024);
    uint64_t usedBytes = SD_MMC.usedBytes() / (1024 * 1024);
    
    Serial.printf("Card Size: %llu MB\n", cardSize);
    Serial.printf("Total Space: %llu MB\n", totalBytes);
    Serial.printf("Used Space: %llu MB\n", usedBytes);
    Serial.printf("Free Space: %llu MB\n", totalBytes - usedBytes);
    Serial.println("====================\n");
}

void sdList(const char* dirname = "/") {
    if (!sdCardMounted) {
        Serial.println("SD card not mounted. Use 'M' to mount.");
        return;
    }
    
    Serial.printf("\n=== Listing: %s ===\n", dirname);
    
    File root = SD_MMC.open(dirname);
    if (!root) {
        Serial.println("Failed to open directory");
        return;
    }
    if (!root.isDirectory()) {
        Serial.println("Not a directory");
        return;
    }
    
    File file = root.openNextFile();
    int count = 0;
    while (file && count < 50) {
        if (file.isDirectory()) {
            Serial.printf("  [DIR]  %s/\n", file.name());
        } else {
            uint64_t size = file.size();
            if (size >= 1024 * 1024) {
                Serial.printf("  [FILE] %-30s  %.2f MB\n", file.name(), size / (1024.0 * 1024.0));
            } else if (size >= 1024) {
                Serial.printf("  [FILE] %-30s  %.2f KB\n", file.name(), size / 1024.0);
            } else {
                Serial.printf("  [FILE] %-30s  %llu bytes\n", file.name(), size);
            }
        }
        file = root.openNextFile();
        count++;
    }
    
    if (count == 0) {
        Serial.println("  (empty)");
    } else if (count >= 50) {
        Serial.println("  ... (truncated at 50 entries)");
    }
    
    Serial.println("======================\n");
}

void sdReadTest() {
    if (!sdCardMounted) {
        Serial.println("SD card not mounted. Use 'M' to mount.");
        return;
    }
    
    Serial.println("\n=== SD Read Speed Test ===");
    
    // Try to find a file to read
    File root = SD_MMC.open("/");
    File testFile;
    
    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory() && file.size() > 100000) {
            testFile = SD_MMC.open(file.path());
            break;
        }
        file = root.openNextFile();
    }
    
    if (!testFile) {
        Serial.println("No suitable file found for speed test (need >100KB)");
        Serial.println("Creating test file...");
        
        // Create a test file
        File writeFile = SD_MMC.open("/speedtest.bin", FILE_WRITE);
        if (!writeFile) {
            Serial.println("Failed to create test file");
            return;
        }
        
        uint8_t* buf = (uint8_t*)malloc(4096);
        if (!buf) {
            Serial.println("Failed to allocate buffer");
            writeFile.close();
            return;
        }
        
        memset(buf, 0xAA, 4096);
        uint32_t writeStart = millis();
        for (int i = 0; i < 256; i++) {  // 1MB
            writeFile.write(buf, 4096);
        }
        writeFile.flush();
        uint32_t writeTime = millis() - writeStart;
        writeFile.close();
        free(buf);
        
        Serial.printf("Write: 1MB in %lu ms = %.2f MB/s\n", writeTime, 1000.0 / writeTime);
        
        testFile = SD_MMC.open("/speedtest.bin");
    }
    
    // Read speed test
    uint8_t* buf = (uint8_t*)malloc(4096);
    if (!buf) {
        Serial.println("Failed to allocate buffer");
        testFile.close();
        return;
    }
    
    size_t bytesToRead = min((size_t)1048576, (size_t)testFile.size());  // Max 1MB
    size_t bytesRead = 0;
    
    uint32_t readStart = millis();
    while (bytesRead < bytesToRead) {
        size_t read = testFile.read(buf, 4096);
        if (read == 0) break;
        bytesRead += read;
    }
    uint32_t readTime = millis() - readStart;
    
    testFile.close();
    free(buf);
    
    float speedMBs = (bytesRead / (1024.0 * 1024.0)) / (readTime / 1000.0);
    Serial.printf("Read: %zu bytes in %lu ms = %.2f MB/s\n", bytesRead, readTime, speedMBs);
    Serial.println("===========================\n");
}

void sdUnmount() {
    if (!sdCardMounted) {
        Serial.println("SD card not mounted");
        return;
    }
    
    SD_MMC.end();
    sdCardMounted = false;
    Serial.println("SD card unmounted");
}
#endif // SDMMC_ENABLED

void wifiVersionInfo() {
    Serial.println("\n=== ESP-Hosted Version Info ===");
    
    // Get version info from esp32-hal-hosted
    extern void hostedGetHostVersion(uint32_t *major, uint32_t *minor, uint32_t *patch);
    extern void hostedGetSlaveVersion(uint32_t *major, uint32_t *minor, uint32_t *patch);
    extern char* hostedGetUpdateURL();
    extern bool hostedHasUpdate();
    
    uint32_t hMajor, hMinor, hPatch;
    uint32_t sMajor, sMinor, sPatch;
    
    hostedGetHostVersion(&hMajor, &hMinor, &hPatch);
    Serial.printf("Host (ESP32-P4) expects:  v%lu.%lu.%lu\n", hMajor, hMinor, hPatch);
    
    hostedGetSlaveVersion(&sMajor, &sMinor, &sPatch);
    Serial.printf("Slave (ESP32-C6) version: v%lu.%lu.%lu\n", sMajor, sMinor, sPatch);
    
    if (hostedHasUpdate()) {
        Serial.println("\n*** FIRMWARE UPDATE NEEDED ***");
        Serial.printf("Download URL: %s\n", hostedGetUpdateURL());
        Serial.println("\nTo update the ESP32-C6:");
        Serial.println("1. Connect USB to the ESP32-C6 port (separate from P4)");
        Serial.println("2. Hold BOOT button on C6, press RESET");
        Serial.println("3. Flash with: esptool.py --chip esp32c6 write_flash 0x0 <firmware.bin>");
    } else {
        Serial.println("Firmware versions match!");
    }
    Serial.println("================================\n");
}
#endif // WIFI_ENABLED

// ============================================================================
// Test patterns
// ============================================================================

void drawColorBars() {
    Serial.println("Drawing color bars...");
    
    const uint16_t w = display.width();   // 1600
    const uint16_t h = display.height();  // 1200
    
    // Divide display into 6 vertical bands for 6 colors
    uint16_t bandWidth = w / 6;
    
    uint8_t colors[] = {
        EL133UF1_BLACK,
        EL133UF1_WHITE,
        EL133UF1_RED,
        EL133UF1_YELLOW,
        EL133UF1_GREEN,
        EL133UF1_BLUE
    };
    
    const char* colorNames[] = {
        "BLACK", "WHITE", "RED", "YELLOW", "GREEN", "BLUE"
    };
    
    for (int i = 0; i < 6; i++) {
        display.fillRect(i * bandWidth, 0, bandWidth, h, colors[i]);
        Serial.printf("  Band %d: %s\n", i, colorNames[i]);
    }
}

void drawTestPattern() {
    Serial.println("Drawing test pattern...");
    
    const uint16_t w = display.width();
    const uint16_t h = display.height();
    
    // Clear to white
    display.clear(EL133UF1_WHITE);
    
    // Draw border
    for (int i = 0; i < 5; i++) {
        display.drawRect(i, i, w - 2*i, h - 2*i, EL133UF1_BLACK);
    }
    
    // Draw corner markers
    int markerSize = 100;
    
    // Top-left: RED
    display.fillRect(20, 20, markerSize, markerSize, EL133UF1_RED);
    
    // Top-right: BLUE
    display.fillRect(w - 20 - markerSize, 20, markerSize, markerSize, EL133UF1_BLUE);
    
    // Bottom-left: GREEN
    display.fillRect(20, h - 20 - markerSize, markerSize, markerSize, EL133UF1_GREEN);
    
    // Bottom-right: YELLOW
    display.fillRect(w - 20 - markerSize, h - 20 - markerSize, markerSize, markerSize, EL133UF1_YELLOW);
    
    // Center text using built-in font
    const char* line1 = "EL133UF1 Display Test";
    const char* line2 = "ESP32-P4 Port";
    const char* line3 = "1600 x 1200 pixels";
    
    int textSize = 4;  // 32x32 pixels per character
    int charW = 8 * textSize;
    
    int x1 = (w - strlen(line1) * charW) / 2;
    int x2 = (w - strlen(line2) * charW) / 2;
    int x3 = (w - strlen(line3) * charW) / 2;
    
    display.drawText(x1, h/2 - 80, line1, EL133UF1_BLACK, EL133UF1_WHITE, textSize);
    display.drawText(x2, h/2,      line2, EL133UF1_RED, EL133UF1_WHITE, textSize);
    display.drawText(x3, h/2 + 80, line3, EL133UF1_BLACK, EL133UF1_WHITE, textSize);
}

void drawTTFTest() {
    Serial.println("Drawing TTF test...");
    
    // Initialize TTF renderer
    ttf.begin(&display);
    
    if (!ttf.loadFont(opensans_ttf, opensans_ttf_len)) {
        Serial.println("ERROR: Failed to load TTF font!");
        return;
    }
    
    // Clear display
    display.clear(EL133UF1_WHITE);
    
    // Draw TTF text at various sizes
    ttf.drawTextAligned(display.width() / 2, 100, "ESP32-P4 + EL133UF1", 72.0,
                        EL133UF1_BLACK, ALIGN_CENTER, ALIGN_TOP);
    
    ttf.drawTextAligned(display.width() / 2, 250, "Spectra 6 E-Ink Display", 48.0,
                        EL133UF1_BLUE, ALIGN_CENTER, ALIGN_TOP);
    
    // Draw a large time display
    ttf.drawTextAligned(display.width() / 2, display.height() / 2, "12:34:56", 160.0,
                        EL133UF1_BLACK, ALIGN_CENTER, ALIGN_MIDDLE);
    
    // Draw some info at bottom
    char buf[64];
    snprintf(buf, sizeof(buf), "PSRAM: %lu KB | Heap: %lu KB", 
             (unsigned long)(hal_psram_get_size() / 1024),
             (unsigned long)(hal_heap_get_free() / 1024));
    
    ttf.drawTextAligned(display.width() / 2, display.height() - 50, buf, 32.0,
                        EL133UF1_BLACK, ALIGN_CENTER, ALIGN_BOTTOM);
}

// ============================================================================
// Setup and Loop
// ============================================================================

void setup() {
    Serial.begin(115200);
    
    // Wait for serial (with timeout)
    uint32_t start = millis();
    while (!Serial && (millis() - start < 5000)) {
        delay(100);
    }
    
    Serial.println("\n\n========================================");
    Serial.println("EL133UF1 ESP32-P4 Port Test");
    Serial.println("========================================\n");
    
    // Print platform info
    hal_print_info();
    
    // Print pin configuration
    Serial.println("\nPin Configuration:");
    Serial.printf("  SPI SCK:  GPIO%d\n", PIN_SPI_SCK);
    Serial.printf("  SPI MOSI: GPIO%d\n", PIN_SPI_MOSI);
    Serial.printf("  CS0:      GPIO%d\n", PIN_CS0);
    Serial.printf("  CS1:      GPIO%d\n", PIN_CS1);
    Serial.printf("  DC:       GPIO%d\n", PIN_DC);
    Serial.printf("  RESET:    GPIO%d\n", PIN_RESET);
    Serial.printf("  BUSY:     GPIO%d\n", PIN_BUSY);
    Serial.println();
    
    // Check PSRAM
    if (!hal_psram_available()) {
        Serial.println("ERROR: PSRAM not detected!");
        Serial.println("This display requires ~2MB PSRAM for the frame buffer.");
        Serial.println("Check board configuration and PSRAM settings.");
        
        // Halt with error message
        while (1) {
            Serial.println("PSRAM ERROR - halted");
            delay(1000);
        }
    }
    
    Serial.printf("PSRAM OK: %lu KB available\n", (unsigned long)(hal_psram_get_size() / 1024));
    
    // Initialize SPI with custom pins
    Serial.println("\nInitializing SPI...");
    displaySPI.begin(PIN_SPI_SCK, -1, PIN_SPI_MOSI, -1);  // SCK, MISO (unused), MOSI, SS (unused)
    
    // Initialize display
    Serial.println("Initializing display...");
    if (!display.begin(PIN_CS0, PIN_CS1, PIN_DC, PIN_RESET, PIN_BUSY)) {
        Serial.println("ERROR: Display initialization failed!");
        while (1) delay(1000);
    }
    
    Serial.println("Display initialized successfully!\n");
    Serial.printf("Display buffer at: %p\n", display.getBuffer());
    
    // Draw test pattern
    Serial.println("\n--- Drawing Test Pattern ---");
    drawTestPattern();
    
    // Update display
    Serial.println("\n--- Updating Display ---");
    Serial.println("This will take 20-30 seconds...\n");
    display.update();
    
    Serial.println("\n========================================");
    Serial.println("Test complete!");
    Serial.println("========================================");
    Serial.println("\nCommands:");
    Serial.println("  Display: 'c'=color bars, 't'=TTF, 'p'=pattern");
    Serial.println("  RTC:     'r'=RTC test, 's'=set RTC time");
    Serial.println("  System:  'i'=info");
#if WIFI_ENABLED
    Serial.println("  WiFi:    'w'=connect, 'W'=set credentials, 'q'=scan, 'd'=disconnect, 'n'=NTP sync, 'x'=status");
#endif
#if SDMMC_ENABLED
    Serial.println("  SD Card: 'M'=mount(4-bit), 'm'=mount(1-bit), 'L'=list, 'I'=info, 'T'=speed test, 'U'=unmount, 'D'=diagnostics");
#endif
    
    // Initialize WiFi (just check status, don't connect yet)
#if WIFI_ENABLED
    Serial.println("\n--- WiFi Status ---");
    Serial.printf("WiFi library available: YES\n");
    Serial.printf("MAC Address: %s\n", WiFi.macAddress().c_str());
    Serial.println("WiFi not connected (use 'W' to set credentials, 'w' to connect)");
#else
    Serial.println("\n--- WiFi Status ---");
    Serial.println("WiFi: DISABLED (DISABLE_WIFI defined)");
#endif

    // Initialize RTC
    Serial.println("\n--- Initializing RTC ---");
    Serial.printf("RTC pins: SDA=%d, SCL=%d, INT=%d\n", PIN_RTC_SDA, PIN_RTC_SCL, PIN_RTC_INT);
    
    if (rtc.begin(&Wire, PIN_RTC_SDA, PIN_RTC_SCL)) {
        Serial.println("DS3231 RTC found!");
        rtc.printStatus();
        
        // Read current time
        time_t now = rtc.getTime();
        Serial.printf("Current RTC time: %lu (Unix timestamp)\n", (unsigned long)now);
        
        // Convert to human readable
        struct tm* timeinfo = gmtime(&now);
        Serial.printf("  UTC: %04d-%02d-%02d %02d:%02d:%02d\n",
                      timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                      timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
        
        // Read temperature
        float temp = rtc.getTemperature();
        Serial.printf("RTC temperature: %.2f °C\n", temp);
    } else {
        Serial.println("DS3231 RTC not found - check wiring");
    }
}

void loop() {
    if (Serial.available()) {
        char c = Serial.read();
        
        if (c == 'c' || c == 'C') {
            Serial.println("\n--- Color Bars Test ---");
            display.clear(EL133UF1_WHITE);
            drawColorBars();
            Serial.println("Updating display...");
            display.update();
            Serial.println("Done!");
        }
        else if (c == 't' || c == 'T') {
            Serial.println("\n--- TTF Test ---");
            drawTTFTest();
            Serial.println("Updating display...");
            display.update();
            Serial.println("Done!");
        }
        else if (c == 'p' || c == 'P') {
            Serial.println("\n--- Test Pattern ---");
            drawTestPattern();
            Serial.println("Updating display...");
            display.update();
            Serial.println("Done!");
        }
        else if (c == 'i' || c == 'I') {
            Serial.println("\n--- Platform Info ---");
            hal_print_info();
        }
        else if (c == 'r' || c == 'R') {
            Serial.println("\n--- RTC Test ---");
            
            if (!rtc.isPresent()) {
                Serial.println("RTC not initialized, trying again...");
                if (!rtc.begin(&Wire, PIN_RTC_SDA, PIN_RTC_SCL)) {
                    Serial.println("DS3231 RTC not found!");
                    return;
                }
            }
            
            // Print full status
            rtc.printStatus();
            
            // Read current time
            time_t now = rtc.getTime();
            Serial.printf("\nCurrent RTC time: %lu\n", (unsigned long)now);
            struct tm* timeinfo = gmtime(&now);
            Serial.printf("  UTC: %04d-%02d-%02d %02d:%02d:%02d\n",
                          timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                          timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
            
            // Temperature
            Serial.printf("Temperature: %.2f °C\n", rtc.getTemperature());
            
            // Test alarm (set 5 second alarm)
            Serial.println("\nSetting 5-second alarm test...");
            rtc.clearAlarm1();
            rtc.setAlarm1(5000);  // 5 seconds
            rtc.enableAlarm1Interrupt(true);
            Serial.println("Alarm set! INT pin should go LOW in 5 seconds.");
            Serial.printf("INT pin (GPIO%d) current state: %s\n", 
                          PIN_RTC_INT, digitalRead(PIN_RTC_INT) ? "HIGH" : "LOW");
            
            // Wait for alarm
            Serial.println("Waiting for alarm...");
            unsigned long start = millis();
            pinMode(PIN_RTC_INT, INPUT_PULLUP);
            while (millis() - start < 7000) {
                if (digitalRead(PIN_RTC_INT) == LOW) {
                    Serial.printf("Alarm triggered after %lu ms!\n", millis() - start);
                    rtc.clearAlarm1();
                    Serial.println("Alarm cleared.");
                    break;
                }
                delay(100);
            }
            if (millis() - start >= 7000) {
                Serial.println("Alarm did not trigger within timeout");
                Serial.printf("Alarm1 flag: %d\n", rtc.alarm1Triggered());
            }
        }
        else if (c == 's' || c == 'S') {
            // Set RTC time from serial input
            Serial.println("\n--- Set RTC Time ---");
            Serial.println("Enter Unix timestamp (seconds since 1970):");
            Serial.println("Example: 1733673600 = 2024-12-08 12:00:00 UTC");
            
            // Wait for input
            while (!Serial.available()) delay(10);
            delay(100);  // Wait for full input
            
            String input = Serial.readStringUntil('\n');
            input.trim();
            unsigned long timestamp = input.toInt();
            
            if (timestamp > 0) {
                Serial.printf("Setting RTC to: %lu\n", timestamp);
                rtc.setTime((time_t)timestamp);
                delay(100);
                
                // Verify
                time_t now = rtc.getTime();
                Serial.printf("RTC now reads: %lu\n", (unsigned long)now);
                struct tm* timeinfo = gmtime(&now);
                Serial.printf("  UTC: %04d-%02d-%02d %02d:%02d:%02d\n",
                              timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                              timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
            } else {
                Serial.println("Invalid timestamp");
            }
        }
#if WIFI_ENABLED
        else if (c == 'q' || c == 'Q') {
            wifiScan();
        }
        else if (c == 'w') {
            wifiConnect();
        }
        else if (c == 'W') {
            wifiSetCredentials();
        }
        else if (c == 'd' || c == 'D') {
            wifiDisconnect();
        }
        else if (c == 'x' || c == 'X') {
            wifiStatus();
        }
        else if (c == 'n' || c == 'N') {
            wifiNtpSync();
        }
#endif
#if SDMMC_ENABLED
        else if (c == 'M') {
            sdInit(false);  // 4-bit mode
        }
        else if (c == 'm') {
            sdInit(true);   // 1-bit mode
        }
        else if (c == 'L') {
            sdList("/");
        }
        else if (c == 'I') {
            sdInfo();
        }
        else if (c == 'T') {
            if (!sdCardMounted) {
                Serial.println("Mounting SD card first (4-bit mode)...");
                sdInit(false);
            }
            if (sdCardMounted) {
                sdReadTest();
            }
        }
        else if (c == 'U') {
            sdUnmount();
        }
        else if (c == 'D') {
            sdDiagnostics();
        }
#endif
    }
    
    delay(100);
}

#endif // ESP32 || ARDUINO_ARCH_ESP32

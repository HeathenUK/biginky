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
 * SDIO SD Card (Pico LiPo 2 XL W):
 *   CLK     ->   GP31
 *   CMD     ->   GP36
 *   DAT0    ->   GP32
 *   DAT1    ->   GP33
 *   DAT2    ->   GP34
 *   DAT3    ->   GP35
 *   DET     ->   GP37 (card detect, active low)
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
#include "EL133UF1_Color.h"
#include "OpenAIImage.h"
#include "GetimgAI.h"
#include "ModelsLabAI.h"
#include "fonts/opensans.h"
#include "pico_sleep.h"
#include "DS3231.h"
#include "AT24C32.h"
#include "hardware/structs/powman.h"
#include "hardware/powman.h"

// SdFat for SDIO SD card support (can disable with -DDISABLE_SDIO_TEST)
#ifndef DISABLE_SDIO_TEST
#include <SdFat.h>
#endif

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

// Button pins (directly active-low buttons to GND)
#define PIN_BTN_WAKE   1    // Wake button (GP1) - press to wake from sleep

// Battery voltage monitoring (Pimoroni Pico LiPo 2 XL W)
// GP43 is the battery voltage ADC pin
#define PIN_VBAT_ADC  43    // Battery voltage ADC pin (GP43 on Pico LiPo)

// SDIO SD Card pins (Pico LiPo 2 XL W microSD slot)
// Directly wired to the on-board or connected microSD card
#define PIN_SDIO_CLK   31   // SDIO CLK (GP31)
#define PIN_SDIO_CMD   36   // SDIO CMD (GP36)
#define PIN_SDIO_DAT0  32   // SDIO DAT0 (GP32)
#define PIN_SDIO_DAT1  33   // SDIO DAT1 (GP33)
#define PIN_SDIO_DAT2  34   // SDIO DAT2 (GP34)
#define PIN_SDIO_DAT3  35   // SDIO DAT3 (GP35)
#define PIN_SDIO_DET   37   // Card Detect (GP37) - active low when card inserted

// Voltage divider ratio - adjust based on actual circuit
// If battery shows wrong voltage, measure with multimeter and calibrate
#define VBAT_DIVIDER_RATIO  3.0f
// ADC reference voltage (3.3V for RP2350)
#define VBAT_ADC_REF  3.3f

// Unix timestamp validation bounds
#define TIMESTAMP_MIN_VALID 1700000000UL  // ~2023, older means RTC not set
#define TIMESTAMP_MAX_VALID 4102444800UL  // ~2100, sanity check

// Create display instance using SPI1
// (SPI1 is the correct bus for GP10/GP11 on Pico)
EL133UF1 display(&SPI1);

// TTF font renderer
EL133UF1_TTF ttf;

// BMP image loader
EL133UF1_BMP bmp;

// PNG decoder and AI image generators
EL133UF1_PNG png;
OpenAIImage openai;
GetimgAI getimgai;
ModelsLabAI modelslab;

// AI-generated image stored in PSRAM (persists between updates)
static uint8_t* aiImageData = nullptr;
static size_t aiImageLen = 0;

// ================================================================
// SDIO SD Card support
// ================================================================
#ifndef DISABLE_SDIO_TEST
// Use SdFs for FAT16/FAT32/exFAT support
// Note: These are created as pointers to avoid crashes from global object construction
static SdFs* sd = nullptr;
static FsFile* sdFile = nullptr;
#endif

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
// Generate AI background image using ModelsLab Qwen at exact display resolution
// ================================================================
/**
 * @brief Generate a 1600x1200 image using ModelsLab's Qwen model
 * 
 * This function generates an image at the exact resolution of the EL133UF1
 * display, optimized for the 6-color Spectra palette.
 * 
 * @param apiKey ModelsLab API key
 * @param outData Pointer to receive allocated image data (caller must free!)
 * @param outLen Pointer to receive image data length
 * @param customPrompt Optional custom prompt (nullptr for default)
 * @return true if successful
 * 
 * @note Requires WiFi to be connected before calling
 */
bool generateDisplayImage_ModelsLabQwen(const char* apiKey, 
                                         uint8_t** outData, 
                                         size_t* outLen,
                                         const char* customPrompt = nullptr) {
    if (!apiKey || !outData || !outLen) {
        Serial.println("ModelsLabQwen: Invalid parameters");
        return false;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("ModelsLabQwen: WiFi not connected");
        return false;
    }
    
    *outData = nullptr;
    *outLen = 0;
    
    // Default prompt optimized for 6-color e-ink display
    const char* defaultPrompt = 
        "A beautiful landscape scene with bold, flat colors. "
        "Use only pure black, pure white, bright red, bright yellow, "
        "bright blue, and bright green. No gradients or shading. "
        "Graphic poster style with high contrast and clear color separation. "
        "Simple shapes, clean lines, vintage travel poster aesthetic. "
        "Mountains, forest, and sky in a serene composition.";
    
    const char* prompt = customPrompt ? customPrompt : defaultPrompt;
    
    Serial.println("=== ModelsLab Qwen Image Generation ===");
    Serial.printf("  Resolution: 1600x1200 (exact display size)\n");
    Serial.printf("  Model: qwen2-vl-flux\n");
    Serial.printf("  Prompt: %.60s...\n", prompt);
    
    // Configure ModelsLab client
    modelslab.begin(apiKey);
    modelslab.setModel(MODELSLAB_QWEN);      // Use Qwen model
    modelslab.setSize(1600, 1200);            // Exact display resolution
    modelslab.setSteps(25);                   // Good balance of quality/speed
    modelslab.setGuidance(7.5f);              // Standard CFG
    modelslab.setNegativePrompt(
        "blurry, gradient, photorealistic, complex details, "
        "fine textures, shadows, 3d render, photograph"
    );
    
    Serial.println("  Generating image...");
    uint32_t startTime = millis();
    
    ModelsLabResult result = modelslab.generate(prompt, outData, outLen, 120000);  // 2 min timeout
    
    uint32_t elapsed = millis() - startTime;
    
    if (result == MODELSLAB_OK && *outData != nullptr && *outLen > 0) {
        Serial.printf("  Success! %zu bytes in %lu ms\n", *outLen, elapsed);
        Serial.println("========================================");
        return true;
    } else {
        Serial.printf("  Failed: %s\n", modelslab.getLastError());
        Serial.printf("  Result code: %d\n", result);
        Serial.println("========================================");
        return false;
    }
}

/**
 * @brief Example: Generate and display a Qwen-generated background
 * 
 * Call this from setup() or doDisplayUpdate() when you want to 
 * generate a fresh AI background at exact display resolution.
 */
void exampleGenerateAndDisplayQwenImage() {
    // Get API key from EEPROM
    char apiKey[200] = {0};
    if (!eeprom.isPresent() || !eeprom.hasModelsLabKey()) {
        Serial.println("No ModelsLab API key configured");
        Serial.println("Press 'c' on boot to configure");
        return;
    }
    eeprom.getModelsLabKey(apiKey, sizeof(apiKey));
    
    // Ensure WiFi is connected
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected, skipping image generation");
        return;
    }
    
    // Generate the image
    uint8_t* imageData = nullptr;
    size_t imageLen = 0;
    
    // Option 1: Use default prompt
    bool success = generateDisplayImage_ModelsLabQwen(apiKey, &imageData, &imageLen);
    
    // Option 2: Use custom prompt
    // const char* myPrompt = "A cozy cabin in snowy mountains, flat colors, poster style";
    // bool success = generateDisplayImage_ModelsLabQwen(apiKey, &imageData, &imageLen, myPrompt);
    
    if (success && imageData != nullptr) {
        Serial.println("Drawing generated image to display...");
        
        // Initialize PNG decoder if not already done
        png.begin(&display);
        png.setDithering(true);  // Floyd-Steinberg for better color mapping
        
        // Draw at 0,0 - image is already 1600x1200 so no scaling needed
        PNGResult pngResult = png.draw(0, 0, imageData, imageLen);
        
        if (pngResult == PNG_OK) {
            Serial.println("Image drawn successfully!");
        } else {
            Serial.printf("PNG decode error: %s\n", png.getErrorString(pngResult));
            display.clear(EL133UF1_WHITE);  // Fallback to white
        }
        
        // Free the image data
        free(imageData);
    } else {
        Serial.println("Image generation failed, using fallback");
        display.clear(EL133UF1_WHITE);
    }
}

// Default time from wake to display completion (boot + draw + refresh)
// This is the initial estimate; actual measured value is used after first cycle
#define DEFAULT_WAKE_TO_DISPLAY_SECONDS  32  // ~2s boot + ~1s draw + ~28s refresh

// Powman scratch register for storing measured wake-to-display time
#define WAKE_DURATION_REG  3  // Stores measured seconds (uses scratch[3])

// Get the measured wake-to-display duration from scratch register
// Returns the stored value, or default if not yet measured
uint32_t getWakeToDisplaySeconds() {
    uint32_t stored = powman_hw->scratch[WAKE_DURATION_REG];
    // Sanity check: should be between 20 and 60 seconds
    if (stored >= 20 && stored <= 60) {
        return stored;
    }
    return DEFAULT_WAKE_TO_DISPLAY_SECONDS;
}

// Store the measured wake-to-display duration
void setWakeToDisplaySeconds(uint32_t seconds) {
    // Sanity check and clamp
    if (seconds < 20) seconds = 20;
    if (seconds > 60) seconds = 60;
    powman_hw->scratch[WAKE_DURATION_REG] = seconds;
}

// Calculate sleep duration so display update COMPLETES at the next even minute
// Returns sleep duration in seconds and optionally fills displayHour/displayMin
// (the time that will be shown on the display when refresh completes)
uint32_t calculateNextWakeTime(int currentMin, int currentSec, int currentHour,
                                int* displayHour = nullptr, int* displayMin = nullptr) {
    // Get the measured (or default) wake-to-display duration
    uint32_t wakeToDisplay = getWakeToDisplaySeconds();
    
    // Find the next even minute (this is when we want the display to SHOW)
    int targetMin = (currentMin % 2 == 0) ? currentMin + 2 : currentMin + 1;
    int secsUntilTarget = (targetMin - currentMin) * 60 - currentSec;
    
    // We need to wake wakeToDisplay seconds before the target time
    // so the display refresh completes right at the even minute
    int sleepDuration = secsUntilTarget - (int)wakeToDisplay;
    
    // If we don't have enough time (would need to wake in the past or too soon),
    // skip to the next even minute
    if (sleepDuration < 5) {  // Need at least 5 seconds of sleep
        sleepDuration += 120;  // Add 2 minutes
        targetMin += 2;
    }
    
    // Calculate the display time (what will be shown)
    if (displayHour || displayMin) {
        int hour = currentHour;
        int min = targetMin % 60;
        if (targetMin >= 60) {
            hour = (hour + 1) % 24;
        }
        if (displayHour) *displayHour = hour;
        if (displayMin) *displayMin = min;
    }
    
    return (uint32_t)sleepDuration;
}

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
    Serial.println("\n--- OpenAI API Key (for DALL-E image generation) ---");
    
    if (eeprom.isPresent() && eeprom.hasOpenAIKey()) {
        char currentKey[200];
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
    
    // --- getimg.ai API Key Configuration ---
    Serial.println("\n--- getimg.ai API Key (for Stable Diffusion/Flux image generation) ---");
    
    if (eeprom.isPresent() && eeprom.hasGetimgKey()) {
        char currentKey[200];
        eeprom.getGetimgKey(currentKey, sizeof(currentKey));
        // Show only first/last few chars for security
        Serial.printf("Current key: %.7s...%s\n", currentKey, currentKey + strlen(currentKey) - 4);
        Serial.println("(Press Enter to keep current, or paste new key)");
    } else {
        Serial.println("No API key configured.");
        Serial.println("Get one at: https://getimg.ai/dashboard");
    }
    
    Serial.print("getimg.ai API Key: ");
    String getimgKey = serialReadLine(true);  // Masked input
    
    if (getimgKey.length() > 0) {
        if (getimgKey.startsWith("key-")) {
            if (eeprom.isPresent()) {
                eeprom.setGetimgKey(getimgKey.c_str());
                Serial.println("API key saved!");
            }
        } else {
            Serial.println("WARNING: Key doesn't start with 'key-', not saved.");
        }
    } else if (eeprom.hasGetimgKey()) {
        Serial.println("(keeping existing key)");
    }
    
    // --- ModelsLab API Key Configuration ---
    Serial.println("\n--- ModelsLab API Key (for Stable Diffusion/Flux image generation) ---");
    
    if (eeprom.isPresent() && eeprom.hasModelsLabKey()) {
        char currentKey[200];
        eeprom.getModelsLabKey(currentKey, sizeof(currentKey));
        // Show only first/last few chars for security
        Serial.printf("Current key: %.7s...%s\n", currentKey, currentKey + strlen(currentKey) - 4);
        Serial.println("(Press Enter to keep current, or paste new key)");
    } else {
        Serial.println("No API key configured.");
        Serial.println("Get one at: https://modelslab.com/dashboard");
    }
    
    Serial.print("ModelsLab API Key: ");
    String modelslabKey = serialReadLine(true);  // Masked input
    
    if (modelslabKey.length() > 0) {
        // ModelsLab keys are alphanumeric, no specific prefix
        if (modelslabKey.length() >= 10) {
            if (eeprom.isPresent()) {
                eeprom.setModelsLabKey(modelslabKey.c_str());
                Serial.println("API key saved!");
            }
        } else {
            Serial.println("WARNING: Key too short, not saved.");
        }
    } else if (eeprom.hasModelsLabKey()) {
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

// Boot timestamp for measuring wake-to-display duration
static uint32_t g_bootTimestamp = 0;

// ================================================================
// SDIO SD Card Debug Function
// ================================================================
/**
 * @brief Test SDIO SD card connectivity and print debug info
 * 
 * This function attempts to initialize an SD card via SDIO interface
 * and prints detailed debug information about the card and filesystem.
 * 
 * @return true if SD card was successfully initialized
 */
#ifndef DISABLE_SDIO_TEST
bool testSdioSdCard() {
    Serial.println("\n=== SDIO SD Card Debug ===");
    Serial.printf("  SDIO Pins: CLK=GP%d, CMD=GP%d, DAT0-3=GP%d-%d, DET=GP%d\n",
                  PIN_SDIO_CLK, PIN_SDIO_CMD, PIN_SDIO_DAT0, PIN_SDIO_DAT3, PIN_SDIO_DET);
    Serial.flush();
    
    // Card detect already checked before calling this function
    // Just log for debugging
    Serial.printf("  Card Detect (GP%d): confirmed present\n", PIN_SDIO_DET);
    
    // Allocate SD object dynamically to avoid global constructor issues
    Serial.println("  Allocating SdFs object...");
    Serial.flush();
    
    if (sd == nullptr) {
        sd = new SdFs();
        if (sd == nullptr) {
            Serial.println("  ERROR: Failed to allocate SdFs object!");
            Serial.println("=============================\n");
            return false;
        }
    }
    Serial.println("  SdFs object allocated OK");
    Serial.flush();
    
    // Configure SDIO pins for RP2350 PIO-based SDIO
    // The SdFat library uses PIO for SDIO on RP2040/RP2350
    // PioSdioConfig(clkPin, cmdPin, dat0Pin, clkDiv)
    // DAT1-3 must be consecutive after DAT0 (dat0+1, dat0+2, dat0+3)
    Serial.println("  Creating SDIO configuration...");
    Serial.printf("    CLK=GP%d, CMD=GP%d, DAT0=GP%d (DAT1-3 consecutive)\n",
                  PIN_SDIO_CLK, PIN_SDIO_CMD, PIN_SDIO_DAT0);
    Serial.flush();
    
    // Verify DAT pins are consecutive (required by PIO SDIO driver)
    static_assert(PIN_SDIO_DAT1 == PIN_SDIO_DAT0 + 1, "DAT1 must be DAT0+1");
    static_assert(PIN_SDIO_DAT2 == PIN_SDIO_DAT0 + 2, "DAT2 must be DAT0+2");
    static_assert(PIN_SDIO_DAT3 == PIN_SDIO_DAT0 + 3, "DAT3 must be DAT0+3");
    
    Serial.println("  Attempting SDIO initialization...");
    Serial.println("  NOTE: If device crashes here, SDIO pins may be incompatible with PIO driver");
    Serial.flush();
    delay(100);  // Give time for serial to flush
    
    uint32_t startTime = millis();
    
    // Create SDIO config with our pins
    // clkDiv=1.0 for maximum speed (can increase to 2.0 if unstable)
    Serial.println("  Creating SdioConfig...");
    Serial.flush();
    SdioConfig sdioConfig(PIN_SDIO_CLK, PIN_SDIO_CMD, PIN_SDIO_DAT0, 1.0);
    
    Serial.println("  Calling sd->begin()...");
    Serial.flush();
    delay(100);
    
    bool success = sd->begin(sdioConfig);
    uint32_t initTime = millis() - startTime;
    
    if (!success) {
        Serial.printf("  SDIO init FAILED after %lu ms\n", initTime);
        Serial.println("  Possible causes:");
        Serial.println("    - No SD card inserted");
        Serial.println("    - SD card not properly seated");
        Serial.println("    - Wrong pin configuration");
        Serial.println("    - Card not compatible with SDIO mode");
        Serial.println("    - Card requires SPI mode instead");
        
        // Try to get more error info
        if (sd->sdErrorCode()) {
            Serial.printf("  SD Error Code: 0x%02X\n", sd->sdErrorCode());
            Serial.printf("  SD Error Data: 0x%02X\n", sd->sdErrorData());
        }
        Serial.println("=============================\n");
        return false;
    }
    
    Serial.printf("  SDIO init SUCCESS in %lu ms\n", initTime);
    
    // Get card info
    cid_t cid;
    csd_t csd;
    
    if (sd->card()->readCID(&cid)) {
        Serial.println("  --- Card Identification (CID) ---");
        Serial.printf("    Manufacturer ID: 0x%02X\n", cid.mid);
        Serial.printf("    OEM ID: %c%c\n", cid.oid[0], cid.oid[1]);
        Serial.printf("    Product: %.5s\n", cid.pnm);
        Serial.printf("    Revision: %d.%d\n", cid.prvN(), cid.prvM());
        Serial.printf("    Serial: 0x%08lX\n", (unsigned long)cid.psn());
        Serial.printf("    Mfg Date: %d/%d\n", cid.mdtMonth(), 2000 + cid.mdtYear());
    } else {
        Serial.println("  Failed to read CID");
    }
    
    if (sd->card()->readCSD(&csd)) {
        Serial.println("  --- Card Specific Data (CSD) ---");
        // CSD version is in the first byte
        uint8_t csdVersion = (csd.csd[0] >> 6) & 0x03;
        Serial.printf("    CSD Version: %d\n", csdVersion + 1);
    } else {
        Serial.println("  Failed to read CSD");
    }
    
    // Card capacity and type
    uint64_t cardSize = sd->card()->sectorCount() * 512ULL;
    Serial.println("  --- Card Info ---");
    Serial.printf("    Card Size: %.2f GB\n", cardSize / (1024.0 * 1024.0 * 1024.0));
    Serial.printf("    Sectors: %lu\n", (unsigned long)sd->card()->sectorCount());
    Serial.printf("    Card Type: ");
    switch (sd->card()->type()) {
        case SD_CARD_TYPE_SD1:  Serial.println("SD1 (<=2GB)"); break;
        case SD_CARD_TYPE_SD2:  Serial.println("SD2"); break;
        case SD_CARD_TYPE_SDHC: 
            if (cardSize > 32ULL * 1024 * 1024 * 1024) {
                Serial.println("SDXC (>32GB)");
            } else {
                Serial.println("SDHC (4-32GB)");
            }
            break;
        default: Serial.printf("Unknown (%d)\n", sd->card()->type()); break;
    }
    
    // Filesystem info
    Serial.println("  --- Filesystem Info ---");
    Serial.printf("    FAT Type: ");
    switch (sd->fatType()) {
        case FAT_TYPE_FAT12: Serial.println("FAT12"); break;
        case FAT_TYPE_FAT16: Serial.println("FAT16"); break;
        case FAT_TYPE_FAT32: Serial.println("FAT32"); break;
        case FAT_TYPE_EXFAT: Serial.println("exFAT"); break;
        default: Serial.printf("Unknown (%d)\n", sd->fatType()); break;
    }
    
    uint64_t freeSpace = sd->freeClusterCount() * sd->bytesPerCluster();
    uint64_t totalSpace = sd->clusterCount() * sd->bytesPerCluster();
    Serial.printf("    Cluster Size: %lu bytes\n", (unsigned long)sd->bytesPerCluster());
    Serial.printf("    Total Clusters: %lu\n", (unsigned long)sd->clusterCount());
    Serial.printf("    Free Clusters: %lu\n", (unsigned long)sd->freeClusterCount());
    Serial.printf("    Total Space: %.2f GB\n", totalSpace / (1024.0 * 1024.0 * 1024.0));
    Serial.printf("    Free Space: %.2f GB (%.1f%%)\n", 
                  freeSpace / (1024.0 * 1024.0 * 1024.0),
                  100.0 * freeSpace / totalSpace);
    
    // List root directory (first 10 entries)
    Serial.println("  --- Root Directory (first 10 entries) ---");
    FsFile root;
    if (root.open("/")) {
        FsFile entry;
        int count = 0;
        while (entry.openNext(&root, O_RDONLY) && count < 10) {
            char name[64];
            entry.getName(name, sizeof(name));
            
            if (entry.isDirectory()) {
                Serial.printf("    [DIR]  %s/\n", name);
            } else {
                uint64_t size = entry.fileSize();
                if (size >= 1024 * 1024) {
                    Serial.printf("    [FILE] %s (%.2f MB)\n", name, size / (1024.0 * 1024.0));
                } else if (size >= 1024) {
                    Serial.printf("    [FILE] %s (%.2f KB)\n", name, size / 1024.0);
                } else {
                    Serial.printf("    [FILE] %s (%llu bytes)\n", name, size);
                }
            }
            entry.close();
            count++;
        }
        if (count == 0) {
            Serial.println("    (empty)");
        }
        root.close();
    } else {
        Serial.println("    Failed to open root directory");
    }
    
    // Speed test - read first sector
    Serial.println("  --- Speed Test (read 100 sectors) ---");
    uint8_t* testBuf = (uint8_t*)malloc(512 * 100);
    if (testBuf) {
        startTime = millis();
        bool readOk = true;
        for (int i = 0; i < 100 && readOk; i++) {
            readOk = sd->card()->readSector(i, testBuf + i * 512);
        }
        uint32_t readTime = millis() - startTime;
        free(testBuf);
        
        if (readOk) {
            float speedKBs = (100 * 512.0 / 1024.0) / (readTime / 1000.0);
            Serial.printf("    Read 100 sectors (50KB) in %lu ms\n", readTime);
            Serial.printf("    Read Speed: %.1f KB/s\n", speedKBs);
        } else {
            Serial.println("    Read test FAILED");
        }
    } else {
        Serial.println("    Could not allocate test buffer");
    }
    
    Serial.println("=============================\n");
    return true;
}
#endif // DISABLE_SDIO_TEST

// ================================================================
// SD Card BMP Image Display (Streaming - no large buffer needed)
// ================================================================
#ifndef DISABLE_SDIO_TEST

// BMP header structures for streaming reader
#pragma pack(push, 1)
struct BmpFileHeader {
    uint16_t signature;      // 'BM' = 0x4D42
    uint32_t fileSize;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t dataOffset;
};

struct BmpInfoHeader {
    uint32_t headerSize;
    int32_t  width;
    int32_t  height;
    uint16_t planes;
    uint16_t bitsPerPixel;
    uint32_t compression;
    uint32_t imageSize;
    int32_t  xPixelsPerMeter;
    int32_t  yPixelsPerMeter;
    uint32_t colorsUsed;
    uint32_t colorsImportant;
};
#pragma pack(pop)

/**
 * @brief Stream a BMP file from SD card directly to display (optimized)
 * 
 * Reads multiple rows at once to minimize SD card seeks.
 * Uses ~200KB buffer for batched reading (50 rows at 1600px 24bpp).
 * Uses fast row-wise color mapping with LUT for maximum throughput.
 * 
 * @param file Open file handle positioned at start
 * @param filename For logging
 * @return true if successful
 */
bool streamBmpToDisplay(FsFile& file, const char* filename) {
    // Read file header
    BmpFileHeader fileHeader;
    if (file.read(&fileHeader, sizeof(fileHeader)) != sizeof(fileHeader)) {
        Serial.println("  Failed to read BMP file header");
        return false;
    }
    
    // Verify BMP signature
    if (fileHeader.signature != 0x4D42) {
        Serial.println("  Invalid BMP signature");
        return false;
    }
    
    // Read info header
    BmpInfoHeader infoHeader;
    if (file.read(&infoHeader, sizeof(infoHeader)) != sizeof(infoHeader)) {
        Serial.println("  Failed to read BMP info header");
        return false;
    }
    
    // Check format support
    if (infoHeader.compression != 0) {
        Serial.println("  Compressed BMPs not supported");
        return false;
    }
    
    int32_t width = infoHeader.width;
    int32_t height = infoHeader.height;
    bool topDown = (height < 0);
    if (topDown) height = -height;
    uint16_t bpp = infoHeader.bitsPerPixel;
    
    Serial.printf("  BMP: %ldx%ld, %d bpp, %s\n", width, height, bpp,
                  topDown ? "top-down" : "bottom-up");
    
    if (bpp != 24 && bpp != 32) {
        Serial.println("  Only 24/32-bit BMPs supported for streaming");
        return false;
    }
    
    // Build color LUT if not already done (one-time ~150ms cost, huge speedup)
    if (!spectra6Color.hasLUT()) {
        spectra6Color.buildLUT();
    }
    
    // Calculate row size (padded to 4-byte boundary)
    int bytesPerPixel = bpp / 8;
    uint32_t rowSize = ((width * bytesPerPixel + 3) / 4) * 4;
    
    // Batch size - read multiple rows at once for speed
    // Target ~200KB buffer = ~50 rows for 1600px 24bpp
    const int ROWS_PER_BATCH = 50;
    uint32_t batchSize = rowSize * ROWS_PER_BATCH;
    
    // Try to allocate batch buffer (use PSRAM if available)
    uint8_t* batchBuffer = (uint8_t*)pmalloc(batchSize);
    if (batchBuffer == nullptr) {
        batchBuffer = (uint8_t*)malloc(batchSize);
    }
    if (batchBuffer == nullptr) {
        Serial.println("  Failed to allocate batch buffer");
        return false;
    }
    
    // Allocate row color buffer for batch writes
    uint8_t* rowColors = (uint8_t*)malloc(width);
    if (rowColors == nullptr) {
        Serial.println("  Failed to allocate row color buffer");
        free(batchBuffer);
        return false;
    }
    
    Serial.printf("  Batch buffer: %lu bytes (%d rows), fast row access: %s\n", 
                  batchSize, ROWS_PER_BATCH,
                  display.canUseFastRowAccess() ? "YES" : "no");
    
    // Calculate centering offset
    int16_t offsetX = (display.width() - width) / 2;
    int16_t offsetY = (display.height() - height) / 2;
    if (offsetX < 0) offsetX = 0;
    if (offsetY < 0) offsetY = 0;
    
    // Clear display first (in case image doesn't cover everything)
    display.clear(EL133UF1_WHITE);
    
    Serial.println("  Streaming to display...");
    uint32_t streamStart = millis();
    uint64_t totalBytesRead = 0;
    
    // Check if we can use fast row access
    bool useFastPath = display.canUseFastRowAccess();
    
    int32_t totalBatches = (height + ROWS_PER_BATCH - 1) / ROWS_PER_BATCH;
    
    for (int32_t batch = 0; batch < totalBatches; batch++) {
        // Calculate which rows this batch covers
        int32_t displayRowStart = batch * ROWS_PER_BATCH;
        int32_t displayRowEnd = min((int32_t)((batch + 1) * ROWS_PER_BATCH), height);
        int32_t rowsInBatch = displayRowEnd - displayRowStart;
        
        // Calculate file position for this batch
        int32_t fileRowStart;
        if (topDown) {
            fileRowStart = displayRowStart;
        } else {
            fileRowStart = height - displayRowEnd;
        }
        
        uint64_t batchOffset = fileHeader.dataOffset + (uint64_t)fileRowStart * rowSize;
        
        // Seek and read entire batch
        if (!file.seek(batchOffset)) {
            Serial.printf("  Seek failed at batch %ld\n", batch);
            free(batchBuffer);
            free(rowColors);
            return false;
        }
        
        uint32_t bytesToRead = rowsInBatch * rowSize;
        if (file.read(batchBuffer, bytesToRead) != (int)bytesToRead) {
            Serial.printf("  Read failed at batch %ld\n", batch);
            free(batchBuffer);
            free(rowColors);
            return false;
        }
        totalBytesRead += bytesToRead;
        
        // Process rows in this batch
        for (int32_t i = 0; i < rowsInBatch; i++) {
            int32_t displayRow = displayRowStart + i;
            int16_t dstY = offsetY + displayRow;
            if (dstY < 0 || dstY >= display.height()) continue;
            
            int32_t bufferRow = topDown ? i : (rowsInBatch - 1 - i);
            uint8_t* rowPtr = batchBuffer + bufferRow * rowSize;
            
            if (useFastPath && offsetX >= 0 && offsetX + width <= display.width()) {
                // FAST PATH: Convert entire row to spectra colors, then batch write
                int32_t pixelsToWrite = min((int32_t)width, (int32_t)(display.width() - offsetX));
                
                // Convert BGR to Spectra colors using LUT (vectorizable loop)
                for (int32_t col = 0; col < pixelsToWrite; col++) {
                    uint8_t b = rowPtr[col * bytesPerPixel + 0];
                    uint8_t g = rowPtr[col * bytesPerPixel + 1];
                    uint8_t r = rowPtr[col * bytesPerPixel + 2];
                    rowColors[col] = spectra6Color.mapColorFast(r, g, b);
                }
                
                // Batch write entire row
                display.writeRowFast(offsetX, dstY, rowColors, pixelsToWrite);
            } else {
                // FALLBACK: Per-pixel with bounds checking
                for (int32_t col = 0; col < width; col++) {
                    int16_t dstX = offsetX + col;
                    if (dstX < 0 || dstX >= display.width()) continue;
                    
                    uint8_t b = rowPtr[col * bytesPerPixel + 0];
                    uint8_t g = rowPtr[col * bytesPerPixel + 1];
                    uint8_t r = rowPtr[col * bytesPerPixel + 2];
                    
                    uint8_t spectraColor = spectra6Color.mapColorFast(r, g, b);
                    display.setPixel(dstX, dstY, spectraColor);
                }
            }
        }
        
        // Progress update every 10 batches
        if (batch % 10 == 0 || batch == totalBatches - 1) {
            Serial.printf("  Batch %ld/%ld (rows %ld-%ld)\r", 
                          batch + 1, totalBatches, displayRowStart, displayRowEnd - 1);
        }
    }
    
    uint32_t streamTime = millis() - streamStart;
    float speedMBs = (totalBytesRead / (1024.0f * 1024.0f)) / (streamTime / 1000.0f);
    Serial.printf("\n  Streamed %.1f MB in %lu ms (%.1f MB/s)\n", 
                  totalBytesRead / (1024.0f * 1024.0f), streamTime, speedMBs);
    
    free(batchBuffer);
    free(rowColors);
    return true;
}

/**
 * @brief Scan SD card root for BMP files and display a random one
 * 
 * Scans the root directory for .bmp files, picks one at random,
 * and streams it directly to the display (no large buffer needed).
 * 
 * @return true if a BMP was found and displayed successfully
 */
bool displayRandomBmpFromSd() {
    if (sd == nullptr) {
        Serial.println("SD: Card not initialized");
        return false;
    }
    
    Serial.println("\n=== Scanning SD Card for BMP files ===");
    
    // First pass: count BMP files
    FsFile root;
    if (!root.open("/")) {
        Serial.println("SD: Failed to open root directory");
        return false;
    }
    
    int bmpCount = 0;
    FsFile entry;
    while (entry.openNext(&root, O_RDONLY)) {
        if (!entry.isDirectory()) {
            char name[64];
            entry.getName(name, sizeof(name));
            size_t len = strlen(name);
            // Check for .bmp extension (case insensitive)
            if (len > 4 && strcasecmp(name + len - 4, ".bmp") == 0) {
                bmpCount++;
                Serial.printf("  Found: %s (%llu bytes)\n", name, entry.fileSize());
            }
        }
        entry.close();
    }
    root.close();
    
    if (bmpCount == 0) {
        Serial.println("  No BMP files found in root directory");
        Serial.println("=====================================\n");
        return false;
    }
    
    Serial.printf("  Total BMP files: %d\n", bmpCount);
    
    // Pick a random file
    int targetIndex = micros() % bmpCount;
    Serial.printf("  Randomly selected index: %d\n", targetIndex);
    
    // Second pass: find and open the selected file
    if (!root.open("/")) {
        Serial.println("SD: Failed to reopen root directory");
        return false;
    }
    
    char selectedName[64] = {0};
    int currentIndex = 0;
    FsFile selectedFile;
    
    while (entry.openNext(&root, O_RDONLY)) {
        if (!entry.isDirectory()) {
            char name[64];
            entry.getName(name, sizeof(name));
            size_t len = strlen(name);
            if (len > 4 && strcasecmp(name + len - 4, ".bmp") == 0) {
                if (currentIndex == targetIndex) {
                    strncpy(selectedName, name, sizeof(selectedName) - 1);
                    Serial.printf("  Selected: %s (%llu bytes)\n", name, entry.fileSize());
                    
                    // Open the file for streaming
                    char fullPath[72];
                    snprintf(fullPath, sizeof(fullPath), "/%s", name);
                    entry.close();
                    root.close();
                    
                    if (!selectedFile.open(fullPath, O_RDONLY)) {
                        Serial.printf("SD: Failed to open %s\n", fullPath);
                        return false;
                    }
                    
                    // Stream directly to display
                    bool success = streamBmpToDisplay(selectedFile, selectedName);
                    selectedFile.close();
                    
                    if (success) {
                        Serial.printf("  Successfully displayed: %s\n", selectedName);
                    }
                    Serial.println("=====================================\n");
                    return success;
                }
                currentIndex++;
            }
        }
        entry.close();
    }
    root.close();
    
    Serial.println("SD: Failed to find selected BMP");
    return false;
}
#endif // DISABLE_SDIO_TEST

void setup() {
    // Record boot time immediately (before any delays)
    // We'll use this to measure actual wake-to-display duration
    g_bootTimestamp = millis();
    
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
        
        // Add button as additional wake source (active-low)
        sleep_add_gpio_wake_source(PIN_BTN_WAKE, false);  // false = active-low
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
    // SDIO SD Card Test
    // ================================================================
    #ifndef DISABLE_SDIO_TEST
    // Quick card detect check BEFORE slow SDIO init
    pinMode(PIN_SDIO_DET, INPUT_PULLUP);
    delay(5);
    bool cardDetectState = (digitalRead(PIN_SDIO_DET) == HIGH);  // Active high
    
    bool hasSDCard = false;
    if (!cardDetectState) {
        Serial.println("SD Card: No card detected (skipping SDIO init)");
    } else {
        Serial.println("\n>>> Card detected, initializing SDIO...");
        Serial.flush();
        hasSDCard = testSdioSdCard();
        if (hasSDCard) {
            Serial.println("SD Card: Available and working");
        } else {
            Serial.println("SD Card: Init failed (continuing without SD)");
        }
    }
    #else
    Serial.println("\n>>> SDIO test disabled (DISABLE_SDIO_TEST defined)");
    bool hasSDCard = false;
    #endif
    
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
    
#if 0  // AI image generation disabled for demo
    // ================================================================
    // Connect WiFi for AI image generation (if needed)
    // ================================================================
    // On cold boot, if we have an API key but skipped NTP sync (RTC had valid time),
    // we still need to connect WiFi for the AI image API call
    if (!sleep_woke_from_deep_sleep() && WiFi.status() != WL_CONNECTED) {
        // Check if we have an API key (OpenAI, getimg.ai, or ModelsLab) and might need to generate an image
        bool hasAnyKey = eeprom.isPresent() && 
                         (eeprom.hasOpenAIKey() || eeprom.hasGetimgKey() || eeprom.hasModelsLabKey());
        if (hasAnyKey && aiImageData == nullptr) {
            Serial.println("\n=== Connecting WiFi for AI image generation ===");
            if (strlen(wifiSSID) > 0) {
                WiFi.begin(wifiSSID, wifiPSK);
                Serial.print("Connecting to ");
                Serial.print(wifiSSID);
                uint32_t start = millis();
                while (WiFi.status() != WL_CONNECTED && (millis() - start < 15000)) {
                    Serial.print(".");
                    delay(500);
                }
                if (WiFi.status() == WL_CONNECTED) {
                    Serial.println(" connected!");
                    Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
                } else {
                    Serial.println(" FAILED");
                }
            }
        }
    }
#endif
    
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
    
    // Measure actual wake-to-display duration and store for next cycle
    uint32_t actualWakeDuration = (millis() - g_bootTimestamp) / 1000;
    uint32_t previousEstimate = getWakeToDisplaySeconds();
    
    // Use exponential moving average to smooth out variations
    // New = 0.7 * measured + 0.3 * previous (weights recent measurement more)
    uint32_t smoothed = (actualWakeDuration * 7 + previousEstimate * 3) / 10;
    setWakeToDisplaySeconds(smoothed);
    
    Serial.printf("\n=== Wake-to-Display Timing ===\n");
    Serial.printf("  Boot to display ready: %lu seconds\n", actualWakeDuration);
    Serial.printf("  Previous estimate:     %lu seconds\n", previousEstimate);
    Serial.printf("  New estimate (EMA):    %lu seconds\n", smoothed);
    Serial.println("===============================");
    
    // Disconnect WiFi to save power before sleep
    if (WiFi.status() == WL_CONNECTED) {
        WiFi.disconnect(true);
        Serial.println("WiFi disconnected (saving power for sleep)");
    }
    
    // Calculate sleep so display update COMPLETES at next even minute
    time_t now = rtc.getTime();
    struct tm* tm = gmtime(&now);
    
    int displayHour, displayMin;
    uint32_t sleepSecs = calculateNextWakeTime(tm->tm_min, tm->tm_sec, tm->tm_hour, 
                                                &displayHour, &displayMin);
    uint32_t sleepMs = sleepSecs * 1000;
    
    // Get the offset we're using (measured or default)
    uint32_t wakeOffset = getWakeToDisplaySeconds();
    
    // Calculate actual wake time for logging
    // We wake wakeOffset seconds before displayHour:displayMin:00
    int totalDisplaySecs = displayHour * 3600 + displayMin * 60;
    int totalWakeSecs = totalDisplaySecs - (int)wakeOffset;
    if (totalWakeSecs < 0) totalWakeSecs += 24 * 3600;
    int wakeHour = (totalWakeSecs / 3600) % 24;
    int wakeMin = (totalWakeSecs / 60) % 60;
    int wakeSec = totalWakeSecs % 60;
    
    Serial.printf("\n=== Entering deep sleep ===\n");
    Serial.printf("Current time:   %02d:%02d:%02d\n", tm->tm_hour, tm->tm_min, tm->tm_sec);
    Serial.printf("Sleep duration: %lu seconds\n", sleepSecs);
    Serial.printf("Wake offset:    %lu seconds (measured)\n", wakeOffset);
    Serial.printf("Will wake at:   ~%02d:%02d:%02d\n", wakeHour, wakeMin, wakeSec);
    Serial.printf("Display ready:  %02d:%02d:00\n", displayHour, displayMin);
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
    
    // Initialize PNG decoder with dithering for better gradient handling
    png.begin(&display);
    png.setDithering(true);  // Floyd-Steinberg dithering for AI images
    
    // Draw update info with performance profiling
    uint32_t drawStart = millis();
    uint32_t t0, t1;
    uint32_t ttfTotal = 0, bitmapTotal = 0;
    
    // ================================================================
    // BACKGROUND
    // ================================================================
    
    t0 = millis();
    bool backgroundSet = false;
    
    #ifndef DISABLE_SDIO_TEST
    // Try to display a random BMP from SD card
    if (sd != nullptr) {
        backgroundSet = displayRandomBmpFromSd();
    }
    #endif
    
    // Fallback to white background if no BMP displayed
    if (!backgroundSet) {
        Serial.println("  Using white background (no SD BMP)");
        display.clear(EL133UF1_WHITE);
    }
    
    bitmapTotal = millis() - t0;
    Serial.printf("  Background: %lu ms\n", bitmapTotal);
    
#if 0  // AI image generation - disabled for demo, enable with #if 1
    // Generate new AI image on first boot, or if we don't have one cached
    bool needNewImage = (aiImageData == nullptr);
    
    // Check if we have API keys configured (prefer getimg.ai/modelslab for speed/cost)
    char apiKeyOpenAI[200] = {0};
    char apiKeyGetimg[200] = {0};
    char apiKeyModelsLab[200] = {0};
    bool hasOpenAIKey = eeprom.isPresent() && eeprom.hasOpenAIKey() && 
                        eeprom.getOpenAIKey(apiKeyOpenAI, sizeof(apiKeyOpenAI));
    bool hasGetimgKey = eeprom.isPresent() && eeprom.hasGetimgKey() && 
                        eeprom.getGetimgKey(apiKeyGetimg, sizeof(apiKeyGetimg));
    bool hasModelsLabKey = eeprom.isPresent() && eeprom.hasModelsLabKey() && 
                           eeprom.getModelsLabKey(apiKeyModelsLab, sizeof(apiKeyModelsLab));
    bool hasAnyKey = hasOpenAIKey || hasGetimgKey || hasModelsLabKey;
    
    // Debug: show AI image generation status
    Serial.println("--- AI Image Status ---");
    Serial.printf("  Need new image: %s\n", needNewImage ? "YES" : "NO (cached)");
    Serial.printf("  EEPROM present: %s\n", eeprom.isPresent() ? "YES" : "NO");
    Serial.printf("  Has OpenAI key: %s\n", hasOpenAIKey ? "YES" : "NO");
    Serial.printf("  Has getimg.ai key: %s\n", hasGetimgKey ? "YES" : "NO");
    Serial.printf("  Has ModelsLab key: %s\n", hasModelsLabKey ? "YES" : "NO");
    Serial.printf("  WiFi status: %d (connected=%d)\n", WiFi.status(), WL_CONNECTED);
    if (hasOpenAIKey) {
        Serial.printf("  OpenAI key: %.7s...%s\n", apiKeyOpenAI, apiKeyOpenAI + strlen(apiKeyOpenAI) - 4);
    }
    if (hasGetimgKey) {
        Serial.printf("  getimg.ai key: %.7s...%s\n", apiKeyGetimg, apiKeyGetimg + strlen(apiKeyGetimg) - 4);
    }
    if (hasModelsLabKey) {
        Serial.printf("  ModelsLab key: %.7s...%s\n", apiKeyModelsLab, apiKeyModelsLab + strlen(apiKeyModelsLab) - 4);
    }
    
    // Prompt optimized for Spectra 6 display (6 colors: black, white, red, yellow, blue, green)
    const char* prompt = 
        "A beautiful wide landscape nature scene in 16:9 aspect ratio, "
        "designed for a 6-color e-ink display. "
        "Use ONLY these colors: pure black, pure white, bright red, bright yellow, "
        "bright blue, and bright green. No gradients, no shading, no intermediate colors. "
        "Bold graphic style like a vintage travel poster or woodblock print. "
        "High contrast with clear separation between color regions. "
        "Simple shapes, no fine details. A serene forest landscape with mountains.";
    
    if (needNewImage && hasAnyKey && WiFi.status() == WL_CONNECTED) {
        Serial.println("Generating AI background image...");
        
        // Priority order: getimg.ai (fastest) > ModelsLab > OpenAI (most expensive)
        
        // Try getimg.ai first (fastest, good quality)
        if (hasGetimgKey && aiImageData == nullptr) {
            Serial.println("  Using getimg.ai (Flux-Schnell)...");
            
            getimgai.begin(apiKeyGetimg);
            getimgai.setModel(GETIMG_FLUX_SCHNELL);  // Very fast model
            getimgai.setSize(1024, 1024);
            getimgai.setFormat(GETIMG_PNG);
            
            t0 = millis();
            GetimgResult result = getimgai.generate(prompt, &aiImageData, &aiImageLen, 90000);
            t1 = millis() - t0;
            
            if (result == GETIMG_OK && aiImageData != nullptr) {
                Serial.printf("  AI image generated: %zu bytes in %lu ms\n", aiImageLen, t1);
            } else {
                Serial.printf("  getimg.ai generation failed: %s\n", getimgai.getLastError());
                aiImageData = nullptr;
                aiImageLen = 0;
            }
        }
        
        // Try ModelsLab if getimg.ai failed or wasn't available
        if (hasModelsLabKey && aiImageData == nullptr) {
            Serial.println("  Using ModelsLab (Flux-Schnell)...");
            
            modelslab.begin(apiKeyModelsLab);
            modelslab.setModel(MODELSLAB_FLUX_SCHNELL);
            modelslab.setSize(1024, 1024);
            modelslab.setSteps(4);  // Flux-schnell uses 4 steps
            modelslab.setGuidance(3.5f);
            
            t0 = millis();
            ModelsLabResult result = modelslab.generate(prompt, &aiImageData, &aiImageLen, 90000);
            t1 = millis() - t0;
            
            if (result == MODELSLAB_OK && aiImageData != nullptr) {
                Serial.printf("  AI image generated: %zu bytes in %lu ms\n", aiImageLen, t1);
            } else {
                Serial.printf("  ModelsLab generation failed: %s\n", modelslab.getLastError());
                aiImageData = nullptr;
                aiImageLen = 0;
            }
        }
        
        // Try OpenAI as last resort (highest quality but most expensive)
        if (hasOpenAIKey && aiImageData == nullptr) {
            Serial.println("  Using OpenAI DALL-E 3...");
            
            openai.begin(apiKeyOpenAI);
            openai.setModel(DALLE_3);
            openai.setSize(DALLE_1792x1024);  // Landscape format for 1600x1200 display
            openai.setQuality(DALLE_STANDARD);
            
            t0 = millis();
            OpenAIResult result = openai.generate(prompt, &aiImageData, &aiImageLen, 90000);
            t1 = millis() - t0;
            
            if (result == OPENAI_OK && aiImageData != nullptr) {
                Serial.printf("  AI image generated: %zu bytes in %lu ms\n", aiImageLen, t1);
            } else {
                Serial.printf("  OpenAI generation failed: %s\n", openai.getLastError());
                aiImageData = nullptr;
                aiImageLen = 0;
            }
        }
    } else if (needNewImage) {
        // Explain why we're not generating
        if (!hasAnyKey) {
            Serial.println("  Skipping AI generation: No API key configured");
            Serial.println("  (Press 'c' on boot to configure getimg.ai, ModelsLab, or OpenAI key)");
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
        
        if (!hasAnyKey) {
            Serial.println("  (No API key configured - press 'c' on boot to set)");
        }
    }
#endif  // AI image generation disabled
    
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
    
    // ================================================================
    // NEXT UPDATE - Bottom left corner, outlined
    // Shows when the next display update will complete (the even minute)
    // ================================================================
    int nextDisplayHour, nextDisplayMin;
    calculateNextWakeTime(tm->tm_min, tm->tm_sec, tm->tm_hour, &nextDisplayHour, &nextDisplayMin);
    snprintf(buf, sizeof(buf), "Next: %02d:%02d", nextDisplayHour, nextDisplayMin);
    t0 = millis();
    ttf.drawTextAlignedOutlined(30, display.height() - 30, buf, 36.0,
                                 EL133UF1_WHITE, EL133UF1_BLACK,
                                 ALIGN_LEFT, ALIGN_BOTTOM, 2);
    t1 = millis() - t0;
    ttfTotal += t1;
    Serial.printf("  TTF next wake:  %lu ms\n", t1);
    
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

/**
 * @file main_esp32p4_test.cpp
 * @brief ESP32-P4 application for EL133UF1 e-ink display
 * 
 * Full-featured application for the EL133UF1 13.3" Spectra 6 e-ink display
 * on ESP32-P4. Includes WiFi, SD card support, deep sleep, and all features.
 * 
 * Build with: pio run -e esp32p4
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
#include <vector>
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "platform_hal.h"
#include "EL133UF1.h"
#include "EL133UF1_TTF.h"
#include "EL133UF1_BMP.h"
#include "EL133UF1_PNG.h"
#include "EL133UF1_Color.h"
#include "EL133UF1_TextPlacement.h"

#include "fonts/opensans.h"
#include "fonts/dancing.h"

#include "es8311_simple.h"
// DS3231 external RTC removed - using ESP32 internal RTC + NTP
#include <time.h>
#include <sys/time.h>

// ESP8266Audio for robust WAV and MP3 parsing and playback
#include "AudioOutputI2S.h"
#include "AudioGeneratorWAV.h"
#include "AudioGeneratorMP3.h"
#include "AudioFileSource.h"

// WiFi support for ESP32-P4 (via ESP32-C6 companion chip)
#if !defined(DISABLE_WIFI) || defined(ENABLE_WIFI_TEST)
#include <WiFi.h>
#include <Preferences.h>
#define WIFI_ENABLED 1
#else
#define WIFI_ENABLED 0
#endif

// MQTT support using ESP-IDF esp-mqtt component
#if WIFI_ENABLED
#include "mqtt_client.h"
#include "esp_crt_bundle.h"
#include <string.h>
#endif

// SD Card support via SDMMC
#if !defined(DISABLE_SDMMC)
#include <SD_MMC.h>
#include <FS.h>
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "esp_ldo_regulator.h"
#include "ff.h"  // FatFs for custom AudioFileSource
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
#ifndef PIN_SW_D
#define PIN_SW_D      51    // GPIO51 = Switch D (active-low)
#endif
// GPIO51 is bridged to GPIO4 for deep sleep wake capability
// GPIO4 is an LP GPIO (0-15) and can wake from deep sleep
// DISABLED: Switch D wake functionality temporarily disabled
#ifndef PIN_SW_D_BRIDGE
// #define PIN_SW_D_BRIDGE  4   // GPIO51 bridged to GPIO4 (LP GPIO) for deep sleep wake
#define PIN_SW_D_BRIDGE  -1  // Disabled - only timer wake enabled
#endif

// RTC I2C pins
// DS3231 pins removed - using internal RTC only

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

// SD Card power control (P-MOSFET Q1 gate)
// GPIO45 LOW = MOSFET ON = SD card powered
// GPIO45 HIGH = MOSFET OFF = SD card unpowered
#ifndef PIN_SD_POWER
#define PIN_SD_POWER  45
#endif

// ============================================================================
// Audio codec (ES8311) pin definitions (Waveshare ESP32-P4-WIFI6)
// ============================================================================
// Override with build flags if your wiring differs.
// ES8311 address is commonly 0x18 (7-bit). (0x30 is the 8-bit write address.)
#ifndef PIN_CODEC_I2C_SDA
#define PIN_CODEC_I2C_SDA  7
#endif
#ifndef PIN_CODEC_I2C_SCL
#define PIN_CODEC_I2C_SCL  8
#endif
#ifndef PIN_CODEC_I2C_ADDR
#define PIN_CODEC_I2C_ADDR 0x18
#endif

#ifndef PIN_CODEC_MCLK
#define PIN_CODEC_MCLK  13
#endif
#ifndef PIN_CODEC_BCLK
#define PIN_CODEC_BCLK  12   // SCLK (bit clock)
#endif
#ifndef PIN_CODEC_LRCK
#define PIN_CODEC_LRCK  10   // LRCK / WS
#endif
#ifndef PIN_CODEC_DOUT
#define PIN_CODEC_DOUT  9    // ESP32 -> codec SDIN (DSDIN)
#endif
#ifndef PIN_CODEC_DIN
#define PIN_CODEC_DIN   11   // codec DOUT (ASDOUT) -> ESP32 (optional)
#endif
#ifndef PIN_CODEC_PA_EN
#define PIN_CODEC_PA_EN 53   // PA_Ctrl (active high)
#endif

#define PIN_USER_LED 7

// ============================================================================
// Global objects
// ============================================================================

// Create display instance
// On ESP32, we typically use the default SPI bus (VSPI or HSPI)
SPIClass displaySPI(HSPI);
EL133UF1 display(&displaySPI);

// TTF font renderer
EL133UF1_TTF ttf;

// Intelligent text placement analyzer
TextPlacementAnalyzer textPlacement;

// BMP image loader
EL133UF1_BMP bmpLoader;
EL133UF1_PNG pngLoader;

// Last loaded image filename (for keep-out map lookup)
static String g_lastImagePath = "";

// Deep sleep boot counter (persists in RTC memory across deep sleep)
RTC_DATA_ATTR uint32_t sleepBootCount = 0;
RTC_DATA_ATTR uint32_t lastImageIndex = 0;  // Track last displayed image for sequential cycling
RTC_DATA_ATTR uint32_t lastMediaIndex = 0;  // Track last displayed image from media.txt
RTC_DATA_ATTR uint32_t ntpSyncCounter = 0;  // Counter for periodic NTP resync
RTC_DATA_ATTR bool usingMediaMappings = false;  // Track if we're using media.txt or scanning all PNGs
RTC_DATA_ATTR char lastAudioFile[64] = "";  // Last audio file path for instant playback on switch D wake

// ============================================================================
// Audio: ES8311 + I2S test tone
// ============================================================================
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static ES8311Simple g_codec;
static AudioOutputI2S* g_audio_output = nullptr;
static TaskHandle_t g_audio_task = nullptr;
static volatile bool g_audio_running = false;
static int g_audio_volume_pct = 50;  // UI percent (0..100), mapped into codec range below
static bool g_codec_ready = false;

static TwoWire g_codec_wire0(0);
static TwoWire g_codec_wire1(1);
static TwoWire* g_codec_wire = nullptr;

static constexpr int kCodecVolumeMinPct = 50; // inaudible below this (empirical)
static constexpr int kCodecVolumeMaxPct = 80; // too loud above this (empirical)

// Auto demo cycle settings: random PNG + clock overlay + short beep + deep sleep
static constexpr bool kAutoCycleEnabled = true;
static constexpr uint32_t kCycleSleepSeconds = 60;
static constexpr uint32_t kCycleSerialEscapeMs = 2000; // cold boot escape to interactive
RTC_DATA_ATTR uint32_t g_cycle_count = 0;
static TaskHandle_t g_auto_cycle_task = nullptr;
static bool g_config_mode_needed = false;  // Flag to indicate config mode is needed

// Forward declarations (defined later in file under SDMMC_ENABLED)
#if SDMMC_ENABLED
bool pngDrawFromMediaMappings(uint32_t* out_sd_read_ms, uint32_t* out_decode_ms);
bool pngDrawRandomToBuffer(const char* dirname, uint32_t* out_sd_read_ms, uint32_t* out_decode_ms);
bool sdInitDirect(bool mode1bit = false);

// SD card state variables (declared here for use by SD config functions)
static bool sdCardMounted = false;
static sdmmc_card_t* sd_card = nullptr;
static esp_ldo_channel_handle_t ldo_vo4_handle = nullptr;
#endif

static bool i2c_ping(TwoWire& w, uint8_t addr7) {
    w.beginTransmission(addr7);
    return (w.endTransmission() == 0);
}

static void i2c_scan(TwoWire& w) {
    int found = 0;
    for (uint8_t a = 0x03; a < 0x78; a++) {
        if (i2c_ping(w, a)) {
            Serial.printf("  - found device at 0x%02X\n", a);
            found++;
        }
    }
    if (found == 0) {
        Serial.println("  (no devices found)");
    }
}

static bool audio_i2s_init(uint32_t sample_rate_hz) {
    if (g_audio_output != nullptr) {
        return true;
    }

    // Initialize ESP8266Audio's I2S output with legacy driver
    // This will be used for WAV playback via ESP8266Audio
    g_audio_output = new AudioOutputI2S(0, AudioOutputI2S::EXTERNAL_I2S, 8, AudioOutputI2S::APLL_DISABLE);
    
    // Set pinout including MCLK (required for ES8311)
    if (!g_audio_output->SetPinout(PIN_CODEC_BCLK, PIN_CODEC_LRCK, PIN_CODEC_DOUT, PIN_CODEC_MCLK)) {
        Serial.println("I2S: SetPinout failed");
        delete g_audio_output;
        g_audio_output = nullptr;
        return false;
    }
    
    // Enable MCLK output
    if (!g_audio_output->SetMclk(true)) {
        Serial.println("I2S: SetMclk failed");
    }
    
    // Set sample rate
    if (!g_audio_output->SetRate(sample_rate_hz)) {
        Serial.printf("I2S: SetRate failed for %u Hz\n", sample_rate_hz);
        delete g_audio_output;
        g_audio_output = nullptr;
        return false;
    }
    
    // Set bits per sample
    if (!g_audio_output->SetBitsPerSample(16)) {
        Serial.println("I2S: SetBitsPerSample failed");
        delete g_audio_output;
        g_audio_output = nullptr;
        return false;
    }
    
    // Initialize I2S (this will call the legacy driver)
    if (!g_audio_output->begin()) {
        Serial.println("I2S: begin failed");
        delete g_audio_output;
        g_audio_output = nullptr;
        return false;
    }
    
    Serial.println("I2S: Initialized with legacy driver (ESP8266Audio)");
    return true;
}

static void audio_task(void* arg) {
    (void)arg;
    const uint32_t sample_rate = 44100;
    const float freq = 440.0f;
    const int16_t amp = 12000;
    const size_t frames = 256; // stereo frames
    int16_t buf[frames * 2];

    float phase = 0.0f;
    const float two_pi = 2.0f * 3.14159265358979323846f;
    const float phase_inc = two_pi * freq / (float)sample_rate;

    while (g_audio_running) {
        for (size_t i = 0; i < frames; i++) {
            float s = sinf(phase);
            phase += phase_inc;
            if (phase >= two_pi) phase -= two_pi;
            int16_t v = (int16_t)(s * amp);
            buf[i * 2 + 0] = v; // L
            buf[i * 2 + 1] = v; // R
        }
        // Write samples using ESP8266Audio's ConsumeSample
        for (size_t i = 0; i < 256; i++) {
            int16_t samples[2] = {buf[i * 2], buf[i * 2 + 1]};
            if (g_audio_output && !g_audio_output->ConsumeSample(samples)) {
                Serial.println("I2S: ConsumeSample failed");
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
        static uint32_t loops = 0;
        loops++;
        if ((loops % 400) == 0) {
            Serial.printf("I2S: streaming... (%u samples)\n", (unsigned)(256 * 2));
        }
    }
    vTaskDelete(nullptr);
}

static bool audio_start(bool verbose) {
    const uint32_t sample_rate = 44100;
    const int bits = 16;

    if (g_audio_running) {
        Serial.println("Audio: already running");
        return true;
    }

    // I2C setup for codec control (Arduino Wire only; avoid legacy esp-idf i2c driver conflicts)
    g_codec_ready = false;
    g_codec_wire = nullptr;

    // Prefer I2C0 on the specified pins
    g_codec_wire0.end();
    delay(5);
    bool ok0 = g_codec_wire0.begin(PIN_CODEC_I2C_SDA, PIN_CODEC_I2C_SCL, 100000);
    Serial.printf("I2C0 begin(SDA=%d SCL=%d): %s\n", PIN_CODEC_I2C_SDA, PIN_CODEC_I2C_SCL, ok0 ? "OK" : "FAIL");
    if (ok0 && i2c_ping(g_codec_wire0, PIN_CODEC_I2C_ADDR)) {
        g_codec_wire = &g_codec_wire0;
        Serial.printf("I2C: codec ACK on I2C0 at 0x%02X\n", PIN_CODEC_I2C_ADDR);
    } else {
        // Also try I2C1 with same pins (some cores map better on certain targets)
        g_codec_wire1.end();
        delay(5);
        bool ok1 = g_codec_wire1.begin(PIN_CODEC_I2C_SDA, PIN_CODEC_I2C_SCL, 100000);
        Serial.printf("I2C1 begin(SDA=%d SCL=%d): %s\n", PIN_CODEC_I2C_SDA, PIN_CODEC_I2C_SCL, ok1 ? "OK" : "FAIL");
        if (ok1 && i2c_ping(g_codec_wire1, PIN_CODEC_I2C_ADDR)) {
            g_codec_wire = &g_codec_wire1;
            Serial.printf("I2C: codec ACK on I2C1 at 0x%02X\n", PIN_CODEC_I2C_ADDR);
        }
    }

    if (!g_codec_wire) {
        Serial.printf("I2C: no ACK at 0x%02X on SDA=%d SCL=%d.\n", PIN_CODEC_I2C_ADDR, PIN_CODEC_I2C_SDA, PIN_CODEC_I2C_SCL);
        Serial.println("Tip: press 'K' to scan for devices.");
        return false;
    }

    ES8311Simple::Pins pins;
    pins.pa_enable_gpio = PIN_CODEC_PA_EN;
    pins.pa_active_high = true;

    ES8311Simple::Clocking clk;
    clk.master_mode = false; // ESP32 provides clocks
    clk.use_mclk = true;
    clk.invert_mclk = false;
    clk.invert_sclk = false;
    clk.digital_mic = false;
    clk.no_dac_ref = false;
    clk.mclk_div = 256;

    if (!g_codec.begin(*g_codec_wire, PIN_CODEC_I2C_ADDR, pins, clk)) {
        Serial.println("ES8311: begin/init failed - check SDA/SCL/address/power.");
        return false;
    }
    g_codec_ready = true;
    g_codec.setTrace(verbose);

    uint8_t id1 = 0, id2 = 0, ver = 0;
    if (g_codec.probe(&id1, &id2, &ver)) {
        Serial.printf("ES8311: CHIP_ID=0x%02X 0x%02X  VER=0x%02X\n", id1, id2, ver);
    } else {
        Serial.println("ES8311: probe failed");
    }

    // Initialize I2S first (ESP8266Audio legacy driver)
    if (!audio_i2s_init(sample_rate)) {
        Serial.println("Audio: I2S init failed");
        return false;
    }
    
    // Note: I2S is now initialized by ESP8266Audio, clocks should be running

    if (!g_codec.configureI2S(sample_rate, bits)) {
        Serial.println("ES8311: configure I2S failed (clocking mismatch?)");
        return false;
    }

    // Use mapped range to match your speaker/amp usable window.
    (void)g_codec.setDacVolumePercentMapped(g_audio_volume_pct, kCodecVolumeMinPct, kCodecVolumeMaxPct);
    Serial.printf("ES8311: volume UI=%d%% mapped to %d..%d%%\n", g_audio_volume_pct, kCodecVolumeMinPct, kCodecVolumeMaxPct);

    if (!g_codec.startDac()) {
        Serial.println("ES8311: start DAC failed");
        return false;
    }

    if (verbose) {
        Serial.println("ES8311: register dump 0x00..0x45 (post-init)");
        (void)g_codec.dumpRegisters(0x00, 0x45);
    }

    // Note: audio_task (440Hz test tone) is only needed for testing
    // For WAV playback, we don't need it - ESP8266Audio handles I2S directly
    // Only start audio_task if we're not in fast wake path (SW_D wake)
    // For SW_D wake, we skip the test tone task to avoid I2S conflicts
    g_audio_running = false;  // Don't start test tone task for WAV playback
    g_audio_task = nullptr;

    Serial.println("Audio: I2S and codec initialized (ready for WAV playback)");
    return true;
}

static bool audio_beep(uint32_t freq_hz, uint32_t duration_ms) {
    const uint32_t sample_rate = 44100;
    const int bits = 16;
    if (!g_codec_ready || g_audio_output == nullptr) {
        // Initialize codec + I2S quietly
        if (!audio_start(false)) {
            return false;
        }
        // Stop the continuous tone task immediately; we'll do a one-shot write below.
        g_audio_running = false;
        delay(10);
    }

    // Ensure audible volume window
    (void)g_codec.setDacVolumePercentMapped(60, kCodecVolumeMinPct, kCodecVolumeMaxPct);
    (void)g_codec.setMute(false);

    const float two_pi = 2.0f * 3.14159265358979323846f;
    float phase = 0.0f;
    const float phase_inc = two_pi * (float)freq_hz / (float)sample_rate;
    const int16_t amp = 12000;

    const uint32_t total_frames = (sample_rate * duration_ms) / 1000;
    const size_t frames_per_chunk = 256;
    int16_t buf[frames_per_chunk * 2];

    uint32_t frames_done = 0;
    while (frames_done < total_frames) {
        size_t frames = min((uint32_t)frames_per_chunk, total_frames - frames_done);
        for (size_t i = 0; i < frames; i++) {
            float s = sinf(phase);
            phase += phase_inc;
            if (phase >= two_pi) phase -= two_pi;
            int16_t v = (int16_t)(s * amp);
            buf[i * 2 + 0] = v;
            buf[i * 2 + 1] = v;
        }
        // Write samples using ESP8266Audio's ConsumeSample
        for (size_t i = 0; i < frames; i++) {
            int16_t samples[2] = {buf[i * 2], buf[i * 2 + 1]};
            if (!g_audio_output || !g_audio_output->ConsumeSample(samples)) {
                Serial.println("I2S: beep ConsumeSample failed");
                break;
            }
        }
        frames_done += (uint32_t)frames;
    }
    return true;
}

static void audio_stop() {
    g_audio_running = false;
    // task self-deletes
    // Note: ESP8266Audio's I2S is managed by the library, we don't need to delete it
    // The AudioOutputI2S object will be reused for WAV playback
    if (g_codec_ready) {
        (void)g_codec.stopAll();
        g_codec_ready = false;
    }
    Serial.println("Audio: stopped");
}

static void sleepNowSeconds(uint32_t seconds) {
    Serial.printf("Sleeping for %lu seconds...\n", (unsigned long)seconds);
    Serial.flush();
    
    // Enable timer wake
    esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
    
    // ESP32-P4 can only wake from deep sleep using LP GPIOs (0-15) via ext1
    // Switch D is on GPIO51, which is NOT an LP GPIO
    // If GPIO51 is bridged to an LP GPIO (e.g., GPIO4), use PIN_SW_D_BRIDGE to enable wake
    
    // DISABLED: Switch D wake functionality - no GPIO configuration needed
    // GPIO wake functionality completely disabled to avoid interfering with bootloader entry
    // Only timer wake is enabled (no GPIO pins are configured)
    
    // Disconnect WiFi before deep sleep (but don't shut down ESP-Hosted completely)
    // Just disconnect from network - ESP-Hosted will handle its own state
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Disconnecting WiFi before deep sleep...");
        WiFi.disconnect(true);
        delay(200);  // Give ESP-Hosted time to handle disconnection
        Serial.println("WiFi disconnected");
    }
    
    // Flush serial and ensure all operations complete before deep sleep
    // This helps prevent bootloader assertion errors after wake
    Serial.flush();
    delay(200);  // Ensure serial flush and any pending operations complete
    
    // Additional delay to ensure flash/SPI operations are fully complete
    // The bootloader needs clean state to load the app partition correctly
    delay(100);
    
    esp_deep_sleep_start();
}

static void sleepUntilNextMinuteOrFallback(uint32_t fallback_seconds = kCycleSleepSeconds) {
    time_t now = time(nullptr);
    if (now <= 1577836800) {  // time invalid
        Serial.printf("Time invalid, sleeping for fallback: %lu seconds\n", (unsigned long)fallback_seconds);
        sleepNowSeconds(fallback_seconds);
        return;  // Never returns, but makes intent clear
    }

    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);
    uint32_t sec = (uint32_t)tm_utc.tm_sec;
    
    // Calculate seconds until next minute boundary
    // If we're at :00, we want to sleep 60 seconds to reach :00 of next minute
    // If we're at :30, we want to sleep 30 seconds to reach :00 of next minute
    uint32_t sleep_s = 60 - sec;
    
    // If we're exactly at :00, sleep a full minute (60 seconds)
    // This is already handled by the calculation above, but make it explicit
    if (sleep_s == 0) {
        sleep_s = 60;  // At :00, sleep to next :00
    }
    
    // Avoid very short sleeps (USB/serial jitter); skip to next minute
    // If we have less than 5 seconds until next minute, sleep to the minute after that
    if (sleep_s < 5 && sleep_s > 0) {
        sleep_s += 60;
        Serial.printf("Sleep duration too short (%lu), adding 60 seconds\n", (unsigned long)(sleep_s - 60));
    }
    
    // Sanity clamp - if calculation is way off, use fallback
    if (sleep_s > 120) {
        Serial.printf("Sleep calculation too large (%lu), using fallback\n", (unsigned long)sleep_s);
        sleep_s = fallback_seconds;
    }

    Serial.printf("Current time: %02d:%02d:%02d, sleeping until next minute: %lu seconds\n",
                  tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec, (unsigned long)sleep_s);
    sleepNowSeconds(sleep_s);
    // Never returns
}

#if WIFI_ENABLED
// Forward declaration
void enterConfigMode();

static bool ensureTimeValid(uint32_t timeout_ms = 20000) {
    time_t now = time(nullptr);
    if (now > 1577836800) {  // 2020-01-01
        return true;
    }

    // If timeout is 0, use a default reasonable timeout
    if (timeout_ms == 0) {
        timeout_ms = 60000;  // Default 60 seconds
    }
    
    uint32_t overallStart = millis();

    // Load creds (if any) directly from NVS and try NTP.
    // (Don't call wifiLoadCredentials() here since it's defined later in this file.)
    Preferences p;
    bool nvsOpened = p.begin("wifi", true);
    if (!nvsOpened) {
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
    
    String ssid = p.getString("ssid", "");
    String psk = p.getString("psk", "");
    p.end();

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
    
    // Configure WiFi for better connection reliability
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);  // Disable WiFi sleep for better connection stability
    WiFi.setTxPower(WIFI_POWER_19_5dBm);  // Maximum power for better range
    WiFi.setAutoReconnect(true);  // Enable auto-reconnect
    
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
        uint32_t timeoutPerAttempt = 30000;  // 30 seconds per attempt
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
        bool synced = false;
        
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
            break;  // Break out to retry WiFi connection
        }
    }
    
    // If we've exhausted retries but WiFi is still connected, try a few more times
    // but respect the overall timeout to prevent infinite loops
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("NTP sync failed after all retries, but WiFi is connected.");
        Serial.println("Will try a few more times (respecting timeout)...");
        
        uint32_t overallStart = millis();
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

    Serial.println("NTP sync failed; WiFi connection lost.");
    // Keep WiFi connected if it still is - will be disconnected before deep sleep
    return false;
}
#else
static bool ensureTimeValid(uint32_t timeout_ms = 0) {
    (void)timeout_ms;
    return (time(nullptr) > 1577836800);
}
#endif

// ============================================================================
// SD Card-Based Configuration for Quotes and Audio
// ============================================================================

#if SDMMC_ENABLED

// Note: We now use ESP8266Audio's AudioOutputI2S directly (g_audio_output)
// No custom wrapper needed

// ============================================================================
// Custom AudioFileSource for FatFs
// ============================================================================
class AudioFileSourceFatFs : public AudioFileSource {
public:
    AudioFileSourceFatFs(const char* filename) : file_(nullptr), filename_(filename) {}
    
    virtual bool open(const char* filename) override {
        if (file_) {
            f_close(file_);
        }
        
        filename_ = filename;
        file_ = (FIL*)malloc(sizeof(FIL));
        if (!file_) {
            return false;
        }
        
        FRESULT res = f_open(file_, filename, FA_READ);
        if (res != FR_OK) {
            free(file_);
            file_ = nullptr;
            return false;
        }
        
        return true;
    }
    
    virtual uint32_t read(void* data, uint32_t len) override {
        if (!file_) return 0;
        
        UINT bytes_read = 0;
        FRESULT res = f_read(file_, data, len, &bytes_read);
        if (res != FR_OK) {
            return 0;
        }
        return bytes_read;
    }
    
    virtual bool seek(int32_t pos, int dir) override {
        if (!file_) return false;
        
        if (dir == SEEK_SET) {
            return (f_lseek(file_, pos) == FR_OK);
        } else if (dir == SEEK_CUR) {
            FSIZE_t current = f_tell(file_);
            return (f_lseek(file_, current + pos) == FR_OK);
        } else if (dir == SEEK_END) {
            FSIZE_t size = f_size(file_);
            return (f_lseek(file_, size + pos) == FR_OK);
        }
        return false;
    }
    
    virtual bool close() override {
        if (file_) {
            f_close(file_);
            free(file_);
            file_ = nullptr;
        }
        return true;
    }
    
    virtual bool isOpen() override {
        return (file_ != nullptr);
    }
    
    virtual uint32_t getSize() override {
        if (!file_) return 0;
        return (uint32_t)f_size(file_);
    }
    
    virtual uint32_t getPos() override {
        if (!file_) return 0;
        return (uint32_t)f_tell(file_);
    }

private:
    FIL* file_;
    const char* filename_;
};

// Structure to hold loaded quotes from SD card
struct LoadedQuote {
    String text;
    String author;
};

static std::vector<LoadedQuote> g_loaded_quotes;
static bool g_quotes_loaded = false;

// Structure to hold image-to-audio mappings
struct MediaMapping {
    String imageName;  // e.g., "sunset.png"
    String audioFile;  // e.g., "ocean.wav"
};

static std::vector<MediaMapping> g_media_mappings;
static bool g_media_mappings_loaded = false;

// Helper function to read a line from FatFs file (f_gets is not available in ESP-IDF)
static bool f_read_line(FIL* fp, char* buffer, size_t bufsize) {
    size_t pos = 0;
    UINT bytesRead;
    char ch;
    
    while (pos < bufsize - 1) {
        FRESULT res = f_read(fp, &ch, 1, &bytesRead);
        if (res != FR_OK || bytesRead == 0) {
            buffer[pos] = '\0';
            return (pos > 0);  // Return true if we read any characters
        }
        
        if (ch == '\n') {
            buffer[pos] = '\0';
            return true;
        }
        
        if (ch != '\r') {  // Skip CR characters
            buffer[pos++] = ch;
        }
    }
    
    buffer[pos] = '\0';
    return true;
}

/**
 * Load quotes from /quotes.txt on SD card
 * Format (one quote per pair of lines):
 *   quote text
 *   ~Author Name
 *   (blank line separator)
 * 
 * Returns: number of quotes loaded
 */
int loadQuotesFromSD() {
    g_loaded_quotes.clear();
    g_quotes_loaded = false;
    
    Serial.println("\n=== Loading quotes from SD card ===");
    
    if (!sdCardMounted && sd_card == nullptr) {
        Serial.println("  SD card not mounted");
        return 0;
    }
    
    String quotesPath = "0:/quotes.txt";
    
    // Check if file exists
    FILINFO fno;
    FRESULT res = f_stat(quotesPath.c_str(), &fno);
    if (res != FR_OK) {
        Serial.println("  /quotes.txt not found (using fallback hard-coded quotes)");
        return 0;
    }
    
    Serial.printf("  Found quotes.txt (%lu bytes)\n", (unsigned long)fno.fsize);
    
    // Open file
    FIL quotesFile;
    res = f_open(&quotesFile, quotesPath.c_str(), FA_READ);
    if (res != FR_OK) {
        Serial.printf("  Failed to open quotes.txt: %d\n", res);
        return 0;
    }
    
    // Read file line by line
    char line[512];
    String currentQuote = "";
    String currentAuthor = "";
    bool readingQuote = true;
    int lineNum = 0;
    
    while (f_read_line(&quotesFile, line, sizeof(line))) {
        lineNum++;
        
        String trimmed = String(line);
        trimmed.trim();
        
        // Skip empty lines between quotes
        if (trimmed.length() == 0) {
            // If we have a complete quote, save it
            if (currentQuote.length() > 0 && currentAuthor.length() > 0) {
                LoadedQuote lq;
                lq.text = currentQuote;
                lq.author = currentAuthor;
                g_loaded_quotes.push_back(lq);
                Serial.printf("  [%d] \"%s\" - %s\n", g_loaded_quotes.size(),
                             currentQuote.c_str(), currentAuthor.c_str());
                currentQuote = "";
                currentAuthor = "";
                readingQuote = true;
            }
            continue;
        }
        
        // Lines starting with ~ are authors
        if (trimmed.startsWith("~")) {
            currentAuthor = trimmed.substring(1);
            currentAuthor.trim();
            readingQuote = false;
        } else {
            // It's a quote line
            if (currentQuote.length() > 0) {
                currentQuote += " ";  // Join multi-line quotes
            }
            currentQuote += trimmed;
        }
    }
    
    // Save the last quote if there is one
    if (currentQuote.length() > 0 && currentAuthor.length() > 0) {
        LoadedQuote lq;
        lq.text = currentQuote;
        lq.author = currentAuthor;
        g_loaded_quotes.push_back(lq);
        Serial.printf("  [%d] \"%s\" - %s\n", g_loaded_quotes.size(),
                     currentQuote.c_str(), currentAuthor.c_str());
    }
    
    f_close(&quotesFile);
    
    if (g_loaded_quotes.size() > 0) {
        g_quotes_loaded = true;
        Serial.printf("  Loaded %d quotes from SD card\n", g_loaded_quotes.size());
    } else {
        Serial.println("  No quotes found in file");
    }
    Serial.println("=====================================\n");
    
    return g_loaded_quotes.size();
}

/**
 * Load image-to-audio mappings from /media.txt on SD card
 * Format (one mapping per line):
 *   image.png,audio.wav
 * 
 * Returns: number of mappings loaded
 */
int loadMediaMappingsFromSD() {
    g_media_mappings.clear();
    g_media_mappings_loaded = false;
    
    Serial.println("\n=== Loading media mappings from SD card ===");
    
    if (!sdCardMounted && sd_card == nullptr) {
        Serial.println("  SD card not mounted");
        return 0;
    }
    
    String mediaPath = "0:/media.txt";
    
    // Check if file exists
    FILINFO fno;
    FRESULT res = f_stat(mediaPath.c_str(), &fno);
    if (res != FR_OK) {
        Serial.println("  /media.txt not found (using fallback beep)");
        return 0;
    }
    
    Serial.printf("  Found media.txt (%lu bytes)\n", (unsigned long)fno.fsize);
    
    // Open file
    FIL mediaFile;
    res = f_open(&mediaFile, mediaPath.c_str(), FA_READ);
    if (res != FR_OK) {
        Serial.printf("  Failed to open media.txt: %d\n", res);
        return 0;
    }
    
    // Read file line by line
    char line[256];
    int lineNum = 0;
    
    while (f_read_line(&mediaFile, line, sizeof(line))) {
        lineNum++;
        
        String trimmed = String(line);
        trimmed.trim();
        
        // Skip empty lines and comments
        if (trimmed.length() == 0 || trimmed.startsWith("#")) {
            continue;
        }
        
        // Parse format: image.png,audio.wav
        // Also allow: image.png (no comma = no audio, will use fallback beep)
        int commaPos = trimmed.indexOf(',');
        if (commaPos > 0 && commaPos < (int)trimmed.length() - 1) {
            // Format: image.png,audio.wav
            String imageName = trimmed.substring(0, commaPos);
            String audioFile = trimmed.substring(commaPos + 1);
            imageName.trim();
            audioFile.trim();
            
            // Extract just the filename (remove path if present)
            int slashPos = imageName.lastIndexOf('/');
            if (slashPos >= 0) {
                imageName = imageName.substring(slashPos + 1);
            }
            
            MediaMapping mm;
            mm.imageName = imageName;
            mm.audioFile = audioFile;
            g_media_mappings.push_back(mm);
            
            Serial.printf("  [%d] %s -> %s\n", g_media_mappings.size(),
                         imageName.c_str(), audioFile.c_str());
        } else if (commaPos < 0 && trimmed.length() > 0) {
            // Format: image.png (no comma = image only, no audio mapping)
            // This allows explicitly listing images that should be shown but use fallback beep
            String imageName = trimmed;
            imageName.trim();
            
            // Extract just the filename (remove path if present)
            int slashPos = imageName.lastIndexOf('/');
            if (slashPos >= 0) {
                imageName = imageName.substring(slashPos + 1);
            }
            
            // Check if it looks like an image file
            if (imageName.length() > 0 && 
                (imageName.endsWith(".png") || imageName.endsWith(".bmp") || 
                 imageName.endsWith(".jpg") || imageName.endsWith(".jpeg"))) {
                MediaMapping mm;
                mm.imageName = imageName;
                mm.audioFile = "";  // Empty audio = will use fallback beep
                g_media_mappings.push_back(mm);
                
                Serial.printf("  [%d] %s -> (no audio, will use fallback beep)\n", 
                             g_media_mappings.size(), imageName.c_str());
            } else {
                Serial.printf("  Warning: Invalid format on line %d: %s (expected image filename)\n", 
                             lineNum, line);
            }
        } else {
            Serial.printf("  Warning: Invalid format on line %d: %s\n", lineNum, line);
        }
    }
    
    f_close(&mediaFile);
    
    if (g_media_mappings.size() > 0) {
        g_media_mappings_loaded = true;
        Serial.printf("  Loaded %d media mappings from SD card\n", g_media_mappings.size());
    } else {
        Serial.println("  No mappings found in file");
    }
    Serial.println("============================================\n");
    
    return g_media_mappings.size();
}

/**
 * Find audio file for a given image filename
 * Returns empty string if no mapping found
 */
String getAudioForImage(const String& imagePath) {
    if (!g_media_mappings_loaded || g_media_mappings.size() == 0) {
        return "";
    }
    
    // Extract just the filename from the path
    String fileName = imagePath;
    int slashPos = fileName.lastIndexOf('/');
    if (slashPos >= 0) {
        fileName = fileName.substring(slashPos + 1);
    }
    
    // Search for matching mapping
    for (size_t i = 0; i < g_media_mappings.size(); i++) {
        if (g_media_mappings[i].imageName.equalsIgnoreCase(fileName)) {
            return g_media_mappings[i].audioFile;
        }
    }
    
    return "";
}

/**
 * Play an audio file (WAV or MP3) from SD card using ESP8266Audio library
 * Automatically detects file format based on extension (.wav or .mp3)
 * Handles audio parsing robustly and uses existing ES8311/I2S setup
 * Returns: true if playback successful
 */
bool playWavFile(const String& audioPath) {
    // Only log for non-beep files (beep.wav is a silent fallback)
    bool isBeep = (audioPath == "beep.wav" || audioPath.endsWith("/beep.wav"));
    
    // Detect file format from extension
    String pathLower = audioPath;
    pathLower.toLowerCase();
    bool isMP3 = pathLower.endsWith(".mp3");
    bool isWAV = pathLower.endsWith(".wav");
    
    if (!isBeep) {
        Serial.printf("\n=== Playing %s: %s ===\n", isMP3 ? "MP3" : "WAV", audioPath.c_str());
    }
    
    if (!sdCardMounted && sd_card == nullptr) {
        if (!isBeep) {
            Serial.println("  SD card not mounted");
        }
        return false;
    }
    
    // Initialize ES8311 codec and I2S if needed
    // This ensures PA is powered and codec is configured
    if (!g_codec_ready || g_audio_output == nullptr) {
        if (!audio_start(false)) {
            Serial.println("  Failed to initialize ES8311 codec");
            return false;
        }
        g_audio_running = false;  // Stop any continuous tone task
        delay(10);
    }
    
    // Set volume to reasonable level and unmute
    (void)g_codec.setDacVolumePercentMapped(60, kCodecVolumeMinPct, kCodecVolumeMaxPct);
    (void)g_codec.setMute(false);
    
    // Validate file format
    if (!isMP3 && !isWAV) {
        if (!isBeep) {
            Serial.printf("  Unsupported audio format: %s (only .wav and .mp3 are supported)\n", audioPath.c_str());
        }
        return false;
    }
    
    // Build FatFs path
    String fatfsPath = "0:";
    if (!audioPath.startsWith("/")) {
        fatfsPath += "/";
    }
    fatfsPath += audioPath;
    
    // Check if file exists
    FILINFO fno;
    FRESULT res = f_stat(fatfsPath.c_str(), &fno);
    if (res != FR_OK) {
        // Silently fail for beep.wav (expected fallback), log for other files
        if (audioPath != "beep.wav" && !audioPath.endsWith("/beep.wav")) {
            Serial.printf("  Audio file not found: %s\n", audioPath.c_str());
        }
        return false;
    }
    
    // Create custom audio source and output using our existing I2S handle
    AudioFileSourceFatFs* file = new AudioFileSourceFatFs(fatfsPath.c_str());
    if (!file->open(fatfsPath.c_str())) {
        // Silently fail for beep.wav (expected fallback), log for other files
        if (audioPath != "beep.wav" && !audioPath.endsWith("/beep.wav")) {
            Serial.printf("  Failed to open audio file: %s\n", fatfsPath.c_str());
        }
        delete file;
        return false;
    }
    
    // Use the global I2S output (already initialized with ES8311 pins)
    // ESP8266Audio will use the legacy I2S driver we initialized
    AudioOutputI2S* out = g_audio_output;
    if (out == nullptr) {
        Serial.println("  I2S output not initialized");
        file->close();
        delete file;
        return false;
    }
    
    // Create appropriate audio generator based on file format
    AudioGenerator* generator = nullptr;
    if (isMP3) {
        generator = new AudioGeneratorMP3();
    } else {
        generator = new AudioGeneratorWAV();
    }
    
    // Only log for non-beep files (beep.wav is a silent fallback)
    if (!isBeep) {
        Serial.println("  Starting playback...");
    }
    uint32_t startTime = millis();
    
    // Begin playback - ESP8266Audio handles all audio parsing
    if (!generator->begin(file, out)) {
        // Silently fail for beep.wav (expected fallback), log for other files
        if (!isBeep) {
            Serial.printf("  Failed to start %s playback\n", isMP3 ? "MP3" : "WAV");
        }
        file->close();
        delete file;
        delete generator;
        return false;
    }
    
    // Play until complete
    while (generator->isRunning()) {
        if (!generator->loop()) {
            generator->stop();
            break;
        }
        // Small delay to prevent tight loop
        delay(1);
    }
    
    uint32_t duration = millis() - startTime;
    // Only log for non-beep files (beep.wav is a silent fallback)
    if (!isBeep) {
        Serial.printf("  Playback complete (%.2f seconds)\n", duration / 1000.0f);
        Serial.println("========================================\n");
    }
    
    // Cleanup (don't delete out - it's g_audio_output and will be reused)
    generator->stop();
    file->close();
    delete generator;
    delete file;
    
    return true;
}

/**
 * Handle wake from switch D - play current audio and go back to sleep
 * FAST PATH: Minimal delays, no WiFi, no NTP, no display init, no SD file reads - just audio playback
 * Uses RTC-stored last audio file path for instant playback
 */
static void handleSwitchDWake() {
    uint32_t wakeStart = millis();
    Serial.println("\n=== SW_D: Fast audio playback (wake from deep sleep) ===");
    
    // Calculate time remaining until next minute wake BEFORE playing audio
    time_t now_before = time(nullptr);
    uint32_t secondsUntilWake = kCycleSleepSeconds;  // Default fallback
    bool timeValid = (now_before > 1577836800);
    
    if (timeValid) {
        struct tm tm_utc;
        gmtime_r(&now_before, &tm_utc);
        uint32_t sec = (uint32_t)tm_utc.tm_sec;
        uint32_t sleep_s = 60 - sec;
        if (sleep_s == 0) sleep_s = 60;
        if (sleep_s < 5 && sleep_s > 0) sleep_s += 60;
        if (sleep_s > 120) sleep_s = kCycleSleepSeconds;
        secondsUntilWake = sleep_s;
        Serial.printf("Time before playback: %02d:%02d:%02d, %lu seconds until next wake\n",
                      tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec, (unsigned long)secondsUntilWake);
    }
    
#if SDMMC_ENABLED
    // Mount SD card if needed (fast path - no verbose output)
    // Only mount if we have a stored audio file to play
    bool needSD = (lastAudioFile[0] != '\0');
    Serial.printf("Stored audio file: %s\n", lastAudioFile[0] != '\0' ? lastAudioFile : "(none)");
    
    if (needSD && !sdCardMounted && sd_card == nullptr) {
        Serial.println("Mounting SD card...");
        if (!sdInitDirect(false)) {
            Serial.println("SD mount failed - going back to sleep");
            sleepUntilNextMinuteOrFallback(kCycleSleepSeconds);
            return;
        }
        Serial.println("SD card mounted");
    } else if (sdCardMounted) {
        Serial.println("SD card already mounted");
    }
    
    // After GPIO wake from deep sleep, hardware may be in a different state than timer wake
    // However, PA_EN is already HIGH from setup(), so don't power cycle it
    // (Normal wake path keeps PA_EN HIGH and doesn't power cycle - that's why it works)
    Serial.println("Re-initializing audio hardware after GPIO wake...");
    
    // Ensure PA_EN is HIGH (it should already be from setup(), but be safe)
    pinMode(PIN_CODEC_PA_EN, OUTPUT);
    digitalWrite(PIN_CODEC_PA_EN, HIGH);
    
    // Small delay to let hardware settle after GPIO wake
    delay(50);
    
    // Delete existing audio output to ensure clean I2S state
    if (g_audio_output != nullptr) {
        Serial.println("Deleting I2S output object...");
        delete g_audio_output;
        g_audio_output = nullptr;
        delay(50);  // Let I2S driver fully uninitialize
    }
    g_audio_running = false;
    g_codec_ready = false;
    g_codec_wire = nullptr;
    
    // Additional delay to let hardware fully settle after GPIO wake
    delay(50);
    
    // Re-initialize audio system from scratch (required after deep sleep)
    Serial.println("Initializing audio from scratch...");
    if (!audio_start(false)) {  // false = minimal logging for speed
        Serial.println("SW_D: Audio init failed, going back to sleep");
        sleepNowSeconds(kCycleSleepSeconds);
        return;
    }
    Serial.println("Audio hardware initialized");
    
    // Critical: I2S driver needs time to stabilize after deep sleep wake
    // The I2S DMA buffers and clock domain need to be fully ready before playback
    delay(300);  // Longer delay for I2S to fully stabilize after GPIO wake
    
    // Use stored audio file path directly from RTC memory (no SD file reads, no mapping lookup)
    String audioFile = "";
    if (lastAudioFile[0] != '\0') {
        audioFile = String(lastAudioFile);
    } else {
        // Fallback if no stored path (shouldn't happen, but be safe)
        audioFile = "beep.wav";
    }
    
    Serial.printf("Playing: %s\n", audioFile.c_str());
    uint32_t playStart = millis();
    
    // Play the audio (minimal logging inside playWavFile for beep.wav)
    bool played = playWavFile(audioFile);
    
    uint32_t playDuration = millis() - playStart;
    Serial.printf("Playback %s (took %lu ms)\n", played ? "complete" : "failed", (unsigned long)playDuration);
    
    audio_stop();
#else
    // No SD card - just go back to sleep
    Serial.println("SD card not available");
#endif

    uint32_t totalWakeTime = millis() - wakeStart;
    Serial.printf("Total wake time: %lu ms\n", (unsigned long)totalWakeTime);
    
    // Check if audio playback took longer than time remaining until next wake
    // If so, we've already passed the scheduled wake time - proceed to next cycle instead of sleeping
    if (timeValid && totalWakeTime > (secondsUntilWake * 1000)) {
        Serial.printf("Audio playback (%lu ms) exceeded wake time (%lu ms) - proceeding to next cycle\n",
                      (unsigned long)totalWakeTime, (unsigned long)(secondsUntilWake * 1000));
        
        // Advance to next media item (as if normal wake had occurred)
        if (g_media_mappings_loaded && g_media_mappings.size() > 0) {
            lastMediaIndex = (lastMediaIndex + 1) % g_media_mappings.size();
            Serial.printf("Advanced to next media item: index %lu\n", (unsigned long)lastMediaIndex);
        }
        
        // Return to setup() to continue with normal cycle (display update, etc.)
        // We'll set a flag to indicate we should skip the SW_D fast path check
        Serial.println("Returning to normal cycle path...");
        return;  // This will return to setup() and continue normal initialization
    }
    
    // Normal case: we haven't passed the wake time, so sleep until next minute
    time_t now = time(nullptr);
    if (now <= 1577836800) {
        // Time invalid - use fallback
        Serial.printf("Time invalid, sleeping for fallback: %lu seconds\n", (unsigned long)kCycleSleepSeconds);
        sleepNowSeconds(kCycleSleepSeconds);
        return;
    }
    
    // Calculate sleep until next minute
    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);
    uint32_t sec = (uint32_t)tm_utc.tm_sec;
    uint32_t sleep_s = 60 - sec;
    if (sleep_s == 0) sleep_s = 60;
    if (sleep_s < 5 && sleep_s > 0) sleep_s += 60;
    if (sleep_s > 120) sleep_s = kCycleSleepSeconds;
    
    Serial.printf("Current time: %02d:%02d:%02d, sleeping until next minute: %lu seconds\n",
                  tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec, (unsigned long)sleep_s);
    Serial.println("========================================\n");
    Serial.flush();
    
    sleepNowSeconds(sleep_s);
    // Never returns
}

#endif // SDMMC_ENABLED

// ============================================================================
// WiFi/MQTT Variables and Functions (needed by auto_cycle_task)
// ============================================================================

#if WIFI_ENABLED
// WiFi credentials - stored in NVS (persistent)
static char wifiSSID[33] = "";
static char wifiPSK[65] = "";
static Preferences wifiPrefs;

// Forward declarations for WiFi/MQTT functions
bool wifiLoadCredentials();  // Returns true if credentials loaded, false if NVS failed or missing
void enterConfigMode();  // Enter interactive configuration mode for WiFi credentials
void mqttLoadConfig();
void mqttSetConfig();
void mqttStatus();
bool mqttConnect();
bool mqttCheckMessages(uint32_t timeoutMs);
String mqttGetLastMessage();  // Get the last received message

// MQTT command handling
static String extractCommandFromMessage(const String& msg);
static String extractCommandParameter(const String& command);
static String extractFromFieldFromMessage(const String& msg);
static bool handleMqttCommand(const String& command, const String& originalMessage = "");
static bool handleClearCommand();
static bool handlePingCommand(const String& originalMessage);
static bool handleNextCommand();
static bool handleGoCommand(const String& parameter);
static bool handleTextCommand(const String& parameter);
void mqttDisconnect();
bool wifiConnectPersistent(int maxRetries = 10, uint32_t timeoutPerAttemptMs = 30000, bool required = true);
#endif // WIFI_ENABLED

static void auto_cycle_task(void* arg) {
    (void)arg;
    g_cycle_count++;
    Serial.printf("\n=== Cycle #%lu ===\n", (unsigned long)g_cycle_count);

    // Increment NTP sync counter
    ntpSyncCounter++;
    
    // Check if time is valid (with timeout to prevent infinite loops)
    // Use a shorter timeout initially to avoid blocking too long
    bool time_ok = false;
    time_t now = time(nullptr);
    if (now > 1577836800) {  // Quick check first
        time_ok = true;
    } else {
        // Only try NTP sync if we have WiFi credentials
        // Use a limited timeout to prevent infinite loops
        Serial.println("Time invalid, attempting NTP sync (with timeout)...");
        time_ok = ensureTimeValid(60000);  // 60 second max timeout
        if (!time_ok) {
            Serial.println("\n========================================");
            Serial.println("CRITICAL: Time sync failed - WiFi credentials required!");
            Serial.println("========================================");
            Serial.println("Configuration mode needed - exiting task to allow main loop to handle it.");
            Serial.println("The main loop will enter configuration mode.");
            // Set flag and exit task - let main loop handle config mode
            g_config_mode_needed = true;
            vTaskDelete(NULL);  // Delete this task - main loop will handle config
            return;  // Should never reach here, but satisfy compiler
        }
        // Re-check time after sync attempt
        now = time(nullptr);
    }
    
    // Get current time to check if it's the top of the hour
    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);
    bool isTopOfHour = (tm_utc.tm_min == 0);
    
    Serial.printf("Current time: %02d:%02d:%02d (isTopOfHour: %s)\n", 
                  tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec,
                  isTopOfHour ? "YES" : "NO");
    
    // If NOT top of hour, do MQTT check instead of display update
    if (!isTopOfHour && time_ok) {
        Serial.println("=== MQTT Check Cycle (not top of hour) ===");
        
#if WIFI_ENABLED
        // Load WiFi and MQTT credentials
        // If WiFi credentials fail to load, stop and wait for user configuration
        if (!wifiLoadCredentials()) {
            Serial.println("\n>>> CRITICAL: WiFi credentials not available <<<");
            Serial.println("Cannot proceed with MQTT check without WiFi credentials.");
            Serial.println("Configuration mode needed - exiting task to allow main loop to handle it.");
            // Set flag and exit task - let main loop handle config mode
            g_config_mode_needed = true;
            vTaskDelete(NULL);  // Delete this task - main loop will handle config
            return;  // Should never reach here, but satisfy compiler
        }
        
        mqttLoadConfig();
        
        // Connect to WiFi - REQUIRED for MQTT, so be persistent
        if (wifiConnectPersistent(10, 30000, true)) {  // 10 retries, 30s per attempt, required
            // WiFi connected - proceed with MQTT
            // Connect to MQTT and check for retained messages
            if (mqttConnect()) {
                // Wait for subscription and any retained messages (max 3 seconds)
                delay(3000);
                
                // Check if we received a retained message
                String commandToProcess = "";
                String originalMessageForCommand = "";
                if (mqttCheckMessages(100)) {
                    String msg = mqttGetLastMessage();
                    Serial.printf("New command received: %s\n", msg.c_str());
                    
                    // Extract command (but don't process yet - disconnect first)
                    String command = extractCommandFromMessage(msg);
                    if (command.length() > 0) {
                        commandToProcess = command;  // Store for processing after disconnect
                        originalMessageForCommand = msg;  // Store original message for commands that need it
                    }
                    
                    // Message already processed and cleared in event handler
                    // The blank retained message was published in the event handler
                    // Give it a moment to complete before disconnecting
                    delay(500);  // Allow time for blank retained message publish to complete
                } else {
                    Serial.println("No retained messages");
                }
                
                // Disconnect from MQTT immediately after checking for messages
                // This prevents connection issues during long-running commands (like display updates)
                mqttDisconnect();
                delay(200);
                
                // Now process the command (if any) after MQTT is fully disconnected
                if (commandToProcess.length() > 0) {
                    if (!handleMqttCommand(commandToProcess, originalMessageForCommand)) {
                        Serial.printf("Unknown command: %s\n", commandToProcess.c_str());
                    }
                }
            }
            
            // Keep WiFi connected - will be disconnected before deep sleep
            Serial.println("WiFi staying connected");
        } else {
            Serial.println("ERROR: WiFi connection failed - this should not happen (required mode)");
        }
#else
        Serial.println("WiFi disabled - cannot check MQTT");
#endif
        
        // Sleep until next minute
        Serial.println("Sleeping until next minute...");
        if (time_ok) {
            sleepUntilNextMinuteOrFallback(kCycleSleepSeconds);
        } else {
            sleepNowSeconds(kCycleSleepSeconds);
        }
        // Never returns - device enters deep sleep
        return;
    }
    
    // Top of hour: proceed with normal display update cycle
    Serial.println("=== Display Update Cycle (top of hour) ===");
    
    // Initialize display now that we know we need it (saves time/power on non-hourly wakes)
    Serial.println("Initializing display...");
    if (!display.begin(PIN_CS0, PIN_CS1, PIN_DC, PIN_RESET, PIN_BUSY)) {
        Serial.println("ERROR: Display initialization failed!");
        // On error, sleep and try again next cycle
        sleepNowSeconds(60);
        return;
    }
    Serial.println("Display initialized");
    
#if WIFI_ENABLED
    // Resync NTP every 5 wake cycles to keep time accurate
    if (ntpSyncCounter >= 5) {
        Serial.println("\n=== Periodic NTP Resync (every 5 cycles) ===");
        ntpSyncCounter = 0;  // Reset counter
        
        // Load WiFi credentials
        Preferences p;
        p.begin("wifi", true);
        String ssid = p.getString("ssid", "");
        String psk = p.getString("psk", "");
        p.end();
        
        if (ssid.length() > 0) {
            // Load credentials into global variables for wifiConnectPersistent
            strncpy(wifiSSID, ssid.c_str(), sizeof(wifiSSID) - 1);
            strncpy(wifiPSK, psk.c_str(), sizeof(wifiPSK) - 1);
            
            // Use persistent WiFi connection (NTP sync is important, so be persistent)
            if (wifiConnectPersistent(8, 30000, true)) {  // 8 retries, 30s per attempt, required
                Serial.println("WiFi connected");
                
                // Sync time via NTP (with retries for robustness)
                const int maxNtpRetries = 5;
                const uint32_t ntpTimeoutPerAttempt = 30000;  // 30 seconds per attempt
                bool ntpSynced = false;
                time_t now = time(nullptr);
                
                for (int retry = 0; retry < maxNtpRetries && !ntpSynced; retry++) {
                    if (retry > 0) {
                        Serial.printf("NTP sync retry %d of %d...\n", retry + 1, maxNtpRetries);
                        delay(2000);
                    }
                    
                configTime(0, 0, "pool.ntp.org", "time.google.com");
                
                Serial.print("Syncing NTP");
                    uint32_t start = millis();
                    while (now < 1577836800 && (millis() - start < ntpTimeoutPerAttempt)) {
                        delay(500);
                        if ((millis() - start) % 5000 == 0) {
                    Serial.print(".");
                        }
                    now = time(nullptr);
                }
                
                if (now > 1577836800) {
                    Serial.println(" OK!");
                    struct tm tm_utc;
                    gmtime_r(&now, &tm_utc);
                    char buf[32];
                    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", &tm_utc);
                    Serial.printf("Time synced: %s\n", buf);
                    time_ok = true;
                        ntpSynced = true;
                } else {
                        Serial.println(" FAILED!");
                        if (retry < maxNtpRetries - 1) {
                            Serial.println("Will retry NTP sync...");
                        }
                    }
                }
                
                if (!ntpSynced) {
                    Serial.println("WARNING: NTP sync failed after all retries, but continuing...");
                }
                
                // Don't disconnect WiFi here - we might need it for MQTT checks
                // WiFi will be disconnected after MQTT check or before sleep
                Serial.println("NTP sync complete, WiFi still connected for potential MQTT use");
            } else {
                Serial.println("WiFi connection failed");
            }
        } else {
            Serial.println("No WiFi credentials saved, skipping NTP resync");
        }
        Serial.println("==========================================\n");
    } else {
        Serial.printf("NTP resync in %lu more cycles\n", (unsigned long)(5 - ntpSyncCounter));
    }
#endif

    uint32_t sd_ms = 0, dec_ms = 0;
#if SDMMC_ENABLED
    // Mount SD card first if not already mounted
    if (!sdCardMounted && sd_card == nullptr) {
        if (!sdInitDirect(false)) {
            Serial.println("Failed to mount SD card!");
            Serial.println("SDMMC disabled; cannot load config or images. Sleeping.");
            if (time_ok) sleepUntilNextMinuteOrFallback(kCycleSleepSeconds);
            sleepNowSeconds(kCycleSleepSeconds);
        }
    }
    
    // Load configuration files from SD card (only once)
    if (!g_quotes_loaded) {
        loadQuotesFromSD();
    }
    if (!g_media_mappings_loaded) {
        loadMediaMappingsFromSD();
    }
    
    // Now load the PNG - prefer media.txt mappings if available
    bool ok = false;
    int maxRetries = 5;  // Try up to 5 different images if one fails
    
    // If media.txt has valid images, use ONLY those
    if (g_media_mappings_loaded && g_media_mappings.size() > 0) {
        Serial.println("Using images from media.txt (cycling through mapped images only)");
        usingMediaMappings = true;
        for (int retry = 0; retry < maxRetries && !ok; retry++) {
            ok = pngDrawFromMediaMappings(&sd_ms, &dec_ms);
            if (!ok && retry < maxRetries - 1) {
                Serial.printf("PNG load failed, trying next image from media.txt (attempt %d/%d)...\n", 
                             retry + 1, maxRetries);
                // Advance to next image by incrementing the index
                lastMediaIndex++;
            }
        }
    } else {
        // Fallback: scan all PNG files on SD card
        Serial.println("No media.txt mappings found, scanning all PNG files on SD card");
        usingMediaMappings = false;
        for (int retry = 0; retry < maxRetries && !ok; retry++) {
            ok = pngDrawRandomToBuffer("/", &sd_ms, &dec_ms);
            if (!ok && retry < maxRetries - 1) {
                Serial.printf("PNG load failed, trying next image (attempt %d/%d)...\n", 
                             retry + 1, maxRetries);
                // Advance to next image by incrementing the index
                lastImageIndex++;
            }
        }
    }
#else
    bool ok = false;
    Serial.println("SDMMC disabled; cannot load PNG. Sleeping.");
#endif
    Serial.printf("PNG SD read: %lu ms, decode+draw: %lu ms\n", (unsigned long)sd_ms, (unsigned long)dec_ms);
    if (!ok) {
        Serial.println("PNG draw failed after retries; sleeping anyway");
        if (time_ok) sleepUntilNextMinuteOrFallback(kCycleSleepSeconds);
        sleepNowSeconds(kCycleSleepSeconds);
    }

    // Overlay time/date with intelligent positioning
    // Reuse time variables from earlier in function
    now = time(nullptr);
    gmtime_r(&now, &tm_utc);

    char timeBuf[16];
    char dateBuf[48];  // "Saturday 13th of December 2025" needs ~35 chars
    bool timeValid = (now > 1577836800); // after 2020-01-01
    if (timeValid) {
        strftime(timeBuf, sizeof(timeBuf), "%H:%M", &tm_utc);
        
        // Format date as "Saturday 13th of December 2025"
        char dayName[12], monthName[12];
        strftime(dayName, sizeof(dayName), "%A", &tm_utc);      // Full day name
        strftime(monthName, sizeof(monthName), "%B", &tm_utc);  // Full month name
        
        int day = tm_utc.tm_mday;
        int year = tm_utc.tm_year + 1900;
        
        // Determine ordinal suffix (1st, 2nd, 3rd, 4th, etc.)
        const char* suffix;
        if (day >= 11 && day <= 13) {
            suffix = "th";  // 11th, 12th, 13th are special cases
        } else {
            switch (day % 10) {
                case 1: suffix = "st"; break;
                case 2: suffix = "nd"; break;
                case 3: suffix = "rd"; break;
                default: suffix = "th"; break;
            }
        }
        
        snprintf(dateBuf, sizeof(dateBuf), "%s %d%s of %s %d", 
                 dayName, day, suffix, monthName, year);
    } else {
        snprintf(timeBuf, sizeof(timeBuf), "--:--");
        snprintf(dateBuf, sizeof(dateBuf), "time not set");
    }

    // Set keepout margins (areas not visible to user due to bezel/frame)
    textPlacement.setKeepout(100);  // 100px margin on all sides
    
    // Clear any previous exclusion zones (fresh start for this frame)
    textPlacement.clearExclusionZones();
    
    // Get text dimensions for both time and date
    // Adaptive sizing: try smaller sizes if keep-out areas block placement
    float timeFontSize = 160.0f;
    float dateFontSize = 48.0f;
    const float minTimeFontSize = 80.0f;   // Don't go smaller than this
    const float minDateFontSize = 24.0f;
    const int16_t gapBetween = 20;
    const int16_t timeOutline = 3;
    const int16_t dateOutline = 2;
    const float minAcceptableScore = 0.25f;  // Threshold for "good enough" placement
    
    TextPlacementRegion bestPos;
    int16_t blockW, blockH;
    int16_t timeW, timeH, dateW, dateH;  // Declare outside loop
    int attempts = 0;
    const int maxAttempts = 5;  // Try up to 5 different sizes
    uint32_t analysisStart;  // Declare for reuse
    
    do {
        attempts++;
        
        timeW = ttf.getTextWidth(timeBuf, timeFontSize) + (timeOutline * 2);
        timeH = ttf.getTextHeight(timeFontSize) + (timeOutline * 2);
        dateW = ttf.getTextWidth(dateBuf, dateFontSize) + (dateOutline * 2);
        dateH = ttf.getTextHeight(dateFontSize) + (dateOutline * 2);

        // Combined block dimensions (time + gap + date)
        blockW = max(timeW, dateW);
        blockH = timeH + gapBetween + dateH;

        // Scan the entire display to find the best position for time/date block
        analysisStart = millis();
        bestPos = textPlacement.scanForBestPosition(
            &display, blockW, blockH,
            EL133UF1_WHITE, EL133UF1_BLACK);
        
        Serial.printf("Time/date placement attempt %d: size=%.0f/%.0f, score=%.2f, pos=%d,%d\n",
                      attempts, timeFontSize, dateFontSize, bestPos.score, bestPos.x, bestPos.y);
        
        // If score is good enough, we're done
        if (bestPos.score >= minAcceptableScore) {
            Serial.printf("  -> Acceptable placement found (score %.2f >= %.2f)\n", 
                         bestPos.score, minAcceptableScore);
            break;
        }
        
        // If at minimum size, have to accept what we have
        if (timeFontSize <= minTimeFontSize || dateFontSize <= minDateFontSize) {
            Serial.printf("  -> At minimum size, using best available (score=%.2f)\n", bestPos.score);
            break;
        }
        
        // Reduce font size by 15% and try again
        timeFontSize *= 0.85f;
        dateFontSize *= 0.85f;
        if (timeFontSize < minTimeFontSize) timeFontSize = minTimeFontSize;
        if (dateFontSize < minDateFontSize) dateFontSize = minDateFontSize;
        
        Serial.printf("  -> Score too low, reducing font size to %.0f/%.0f\n", 
                     timeFontSize, dateFontSize);
        
    } while (attempts < maxAttempts);
    
    Serial.printf("Time/date placement final: %.0f/%.0f size, score=%.2f after %d attempts\n",
                  timeFontSize, dateFontSize, bestPos.score, attempts);
    
    // Debug: show what area was checked for keep-out
    int16_t checkX = bestPos.x - blockW/2;
    int16_t checkY = bestPos.y - blockH/2;
    Serial.printf("[DEBUG] Time/Date block checked: x=%d, y=%d, w=%d, h=%d (center=%d,%d)\n",
                  checkX, checkY, blockW, blockH, bestPos.x, bestPos.y);

    // Calculate individual positions relative to the chosen block center
    int16_t timeY = bestPos.y - (blockH/2) + (timeH/2);
    int16_t dateY = bestPos.y + (blockH/2) - (dateH/2);

    // Draw time and date at best position
    Serial.printf("[DEBUG] Drawing time at (%d,%d) with size %.0f, outline %d\n", 
                  bestPos.x, timeY, timeFontSize, timeOutline);
    Serial.printf("[DEBUG] Drawing date at (%d,%d) with size %.0f, outline %d\n", 
                  bestPos.x, dateY, dateFontSize, dateOutline);
    
    ttf.drawTextAlignedOutlined(bestPos.x, timeY, timeBuf, timeFontSize,
                                EL133UF1_WHITE, EL133UF1_BLACK,
                                ALIGN_CENTER, ALIGN_MIDDLE, timeOutline);
    ttf.drawTextAlignedOutlined(bestPos.x, dateY, dateBuf, dateFontSize,
                                EL133UF1_WHITE, EL133UF1_BLACK,
                                ALIGN_CENTER, ALIGN_MIDDLE, dateOutline);
    
    // Add the time/date block as an exclusion zone so quote won't overlap
    // Larger padding to create visual separation between elements
    textPlacement.addExclusionZone(bestPos, 150);  // Increased from 50 to 150px

    // ================================================================
    // QUOTE - Intelligently positioned with automatic line wrapping
    // ================================================================
    
    using Quote = TextPlacementAnalyzer::Quote;
    Quote selectedQuote;
    
#if SDMMC_ENABLED
    // Try to use quotes from SD card first
    if (g_quotes_loaded && g_loaded_quotes.size() > 0) {
        // Select a random quote from SD card
        int randomIndex = random(g_loaded_quotes.size());
        selectedQuote.text = g_loaded_quotes[randomIndex].text.c_str();
        selectedQuote.author = g_loaded_quotes[randomIndex].author.c_str();
        Serial.printf("Using SD card quote: \"%s\" - %s\n", selectedQuote.text, selectedQuote.author);
    } else {
#endif
        // Fallback: use hard-coded quotes
        static const Quote fallbackQuotes[] = {
            {"Vulnerability is not weakness; it's our greatest measure of courage", "Brene Brown"},
            {"The only way to do great work is to love what you do", "Steve Jobs"},
            {"In the middle of difficulty lies opportunity", "Albert Einstein"},
            {"Be yourself; everyone else is already taken", "Oscar Wilde"},
            {"The future belongs to those who believe in the beauty of their dreams", "Eleanor Roosevelt"},
            {"It is during our darkest moments that we must focus to see the light", "Aristotle"},
            {"The best time to plant a tree was 20 years ago. The second best time is now", "Chinese Proverb"},
            {"Life is what happens when you're busy making other plans", "John Lennon"},
        };
        static const int numQuotes = sizeof(fallbackQuotes) / sizeof(fallbackQuotes[0]);
        selectedQuote = fallbackQuotes[random(numQuotes)];
        Serial.printf("Using fallback quote: \"%s\" - %s\n", selectedQuote.text, selectedQuote.author);
#if SDMMC_ENABLED
    }
#endif
    
    // Adaptive sizing for quote as well
    float quoteFontSize = 48.0f;
    float authorFontSize = 32.0f;
    const float minQuoteFontSize = 28.0f;
    const float minAuthorFontSize = 20.0f;
    
    TextPlacementAnalyzer::QuoteLayoutResult quoteLayout;
    attempts = 0;
    
    do {
        attempts++;
        
        // Scan the entire display to find the best quote position
        analysisStart = millis();
        quoteLayout = textPlacement.scanForBestQuotePosition(
            &display, &ttf, selectedQuote, quoteFontSize, authorFontSize,
            EL133UF1_WHITE, EL133UF1_BLACK,
            3,   // maxLines: try up to 3 lines
            3);  // minWordsPerLine: at least 3 words per line
        
        Serial.printf("Quote placement attempt %d: size=%.0f/%.0f, score=%.2f, pos=%d,%d, %d lines\n",
                      attempts, quoteFontSize, authorFontSize, quoteLayout.position.score,
                      quoteLayout.position.x, quoteLayout.position.y, quoteLayout.quoteLines);
        
        // If score is good enough, we're done
        if (quoteLayout.position.score >= minAcceptableScore) {
            Serial.printf("  -> Acceptable quote placement found (score %.2f >= %.2f)\n", 
                         quoteLayout.position.score, minAcceptableScore);
            break;
        }
        
        // If at minimum size, have to accept what we have
        if (quoteFontSize <= minQuoteFontSize || authorFontSize <= minAuthorFontSize) {
            Serial.printf("  -> At minimum size, using best available (score=%.2f)\n", 
                         quoteLayout.position.score);
            break;
        }
        
        // Reduce font size by 15% and try again
        quoteFontSize *= 0.85f;
        authorFontSize *= 0.85f;
        if (quoteFontSize < minQuoteFontSize) quoteFontSize = minQuoteFontSize;
        if (authorFontSize < minAuthorFontSize) authorFontSize = minAuthorFontSize;
        
        Serial.printf("  -> Score too low, reducing font size to %.0f/%.0f\n", 
                     quoteFontSize, authorFontSize);
        
    } while (attempts < maxAttempts);
    
    Serial.printf("Quote placement final: %.0f/%.0f size, score=%.2f after %d attempts\n",
                  quoteFontSize, authorFontSize, quoteLayout.position.score, attempts);
    Serial.printf("  Quote: \"%s\"\n", quoteLayout.wrappedQuote);
    Serial.printf("  Author: %s\n", selectedQuote.author);
    
    // Draw the quote with author using the helper function
    textPlacement.drawQuote(&ttf, quoteLayout, selectedQuote.author,
                            quoteFontSize, authorFontSize,
                            EL133UF1_WHITE, EL133UF1_BLACK, 2);
    
    // Add quote as exclusion zone for any future text elements (e.g., battery %)
    textPlacement.addExclusionZone(quoteLayout.position, 50);

    // Refresh display first (e-ink refresh takes 20-30 seconds)
    Serial.println("Updating display (e-ink refresh)...");
    uint32_t refreshStart = millis();
    display.update();
    uint32_t refreshMs = millis() - refreshStart;
    Serial.printf("Display refresh: %lu ms\n", (unsigned long)refreshMs);

    // ================================================================
    // AUDIO - Play WAV file for this image (or fallback to beep)
    // Now plays AFTER display refresh is complete
    // ================================================================
    
#if SDMMC_ENABLED
    String audioFile = getAudioForImage(g_lastImagePath);
    if (audioFile.length() > 0) {
        Serial.printf("Image %s has audio mapping: %s\n", g_lastImagePath.c_str(), audioFile.c_str());
        // Store in RTC memory for instant playback on switch D wake
        strncpy(lastAudioFile, audioFile.c_str(), sizeof(lastAudioFile) - 1);
        lastAudioFile[sizeof(lastAudioFile) - 1] = '\0';
        if (playWavFile(audioFile)) {
            Serial.println("Audio playback complete");
        } else {
            // Try to play beep.wav from SD root, silently fail if not available
            strncpy(lastAudioFile, "beep.wav", sizeof(lastAudioFile) - 1);
            playWavFile("beep.wav");  // Returns false if file missing/invalid, no sound
        }
    } else {
        // Try to play beep.wav from SD root, silently fail if not available
        strncpy(lastAudioFile, "beep.wav", sizeof(lastAudioFile) - 1);
        playWavFile("beep.wav");  // Returns false if file missing/invalid, no sound
    }
    audio_stop();
#else
    // No SD card - no audio available
    Serial.println("SD card not available, no audio");
#endif

    if (time_ok) {
        Serial.println("Time is valid, calculating sleep until next minute...");
        sleepUntilNextMinuteOrFallback(kCycleSleepSeconds);
        // Never returns - device enters deep sleep
    } else {
        Serial.println("Time not valid, sleeping for fallback duration (60 seconds)");
        sleepNowSeconds(kCycleSleepSeconds);
    }
}

// ============================================================================
// WiFi Functions
// ============================================================================

#if WIFI_ENABLED
// WiFi credentials already declared above (before auto_cycle_task)

// ============================================================================
// MQTT Functions
// ============================================================================

// MQTT configuration - hardcoded
#define MQTT_BROKER_HOSTNAME "mqtt.flespi.io"  // Change this to your MQTT broker
#define MQTT_BROKER_PORT 8883                        // TLS port (use 1883 for non-TLS)
#define MQTT_CLIENT_ID "esp32p4_device"              // Client ID
#define MQTT_USERNAME "e2XkCCjnqSpUIxeSKB7WR7z7BWa8B6YAqYQaSKYQd0CBavgu0qeV6c2GQ6Af4i8w"                 // MQTT username
#define MQTT_PASSWORD ""                 // MQTT password
#define MQTT_TOPIC_SUBSCRIBE "devices/twilio_sms_bridge/cmd"       // Topic to subscribe to
#define MQTT_TOPIC_PUBLISH "devices/twilio_sms_bridge/outbox"            // Topic to publish to

// MQTT runtime state
static char mqttBroker[128] = MQTT_BROKER_HOSTNAME;
static int mqttPort = MQTT_BROKER_PORT;
static char mqttClientId[64] = MQTT_CLIENT_ID;
static char mqttUsername[128] = MQTT_USERNAME;  // Increased for flespi.io tokens
static char mqttPassword[64] = MQTT_PASSWORD;
static char mqttTopicSubscribe[128] = MQTT_TOPIC_SUBSCRIBE;
static char mqttTopicPublish[128] = MQTT_TOPIC_PUBLISH;
static esp_mqtt_client_handle_t mqttClient = nullptr;
static bool mqttMessageReceived = false;
static String lastMqttMessage = "";
static bool mqttConnected = false;

// MQTT event handler
static void mqttEventHandler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    esp_mqtt_client_handle_t client = event->client;
    
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            mqttConnected = true;
            
            // Subscribe to topic if configured
            if (strlen(mqttTopicSubscribe) > 0) {
                // Subscribe with QoS 1 to ensure message delivery
                int msg_id = esp_mqtt_client_subscribe(client, mqttTopicSubscribe, 1);
                // Wait briefly for subscription confirmation and any retained messages
            }
            break;
            
        case MQTT_EVENT_SUBSCRIBED:
            Serial.printf("MQTT subscription confirmed (msg_id: %d)\n", event->msg_id);
            break;
            
        case MQTT_EVENT_UNSUBSCRIBED:
            Serial.printf("MQTT unsubscribed (msg_id: %d)\n", event->msg_id);
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            Serial.println("MQTT disconnected");
            mqttConnected = false;
            break;
            
        case MQTT_EVENT_DATA: {
            // Extract message payload
            char message[event->data_len + 1];
            if (event->data_len > 0) {
                memcpy(message, event->data, event->data_len);
                message[event->data_len] = '\0';
            } else {
                message[0] = '\0';
            }
            
            // Only process non-blank retained messages
            if (event->retain && event->data_len > 0) {
                lastMqttMessage = String(message);
                mqttMessageReceived = true;
                
                // Clear the retained message by publishing an empty message with retain flag
                if (strlen(mqttTopicSubscribe) > 0 && client != nullptr) {
                    int msg_id = esp_mqtt_client_publish(client, mqttTopicSubscribe, "", 0, 1, 1);
                    if (msg_id > 0) {
                        Serial.printf("Published blank retained message to clear (msg_id: %d)\n", msg_id);
                    }
                }
            }
            // Ignore non-retained messages and blank retained messages
            break;
        }
            
        case MQTT_EVENT_ERROR:
            {
                Serial.printf("MQTT error: %s\n", esp_err_to_name(event->error_handle->error_type));
                if (event->error_handle->error_type == MQTT_ERROR_TYPE_ESP_TLS) {
                    Serial.printf("  ESP-TLS error: 0x%x\n", event->error_handle->esp_tls_last_esp_err);
                } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                    Serial.printf("  Connection refused: 0x%x\n", event->error_handle->connect_return_code);
                }
                // Note: Don't set mqttConnected = false here - let DISCONNECT event handle it
                // This prevents race conditions
            }
            break;
            
        default:
            break;
    }
}

// Load MQTT configuration (now using hardcoded values)
void mqttLoadConfig() {
    // Configuration is now hardcoded via #defines above
    // Just ensure strings are properly initialized
    strncpy(mqttBroker, MQTT_BROKER_HOSTNAME, sizeof(mqttBroker) - 1);
    mqttPort = MQTT_BROKER_PORT;
    strncpy(mqttClientId, MQTT_CLIENT_ID, sizeof(mqttClientId) - 1);
    strncpy(mqttUsername, MQTT_USERNAME, sizeof(mqttUsername) - 1);
    strncpy(mqttPassword, MQTT_PASSWORD, sizeof(mqttPassword) - 1);
    strncpy(mqttTopicSubscribe, MQTT_TOPIC_SUBSCRIBE, sizeof(mqttTopicSubscribe) - 1);
    strncpy(mqttTopicPublish, MQTT_TOPIC_PUBLISH, sizeof(mqttTopicPublish) - 1);
    Serial.printf("MQTT config (hardcoded): broker=%s, port=%d, client_id=%s\n", 
                  mqttBroker, mqttPort, mqttClientId);
}

// Save MQTT configuration (no-op since using hardcoded values)
void mqttSaveConfig() {
    Serial.println("MQTT configuration is hardcoded - edit #defines in source code to change");
}

// Connect to MQTT broker
bool mqttConnect() {
    if (strlen(mqttBroker) == 0) {
        Serial.println("No MQTT broker configured");
        return false;
    }
    
    // Disconnect existing client if any
    if (mqttClient != nullptr) {
        esp_mqtt_client_stop(mqttClient);
        esp_mqtt_client_destroy(mqttClient);
        mqttClient = nullptr;
    }
    
    // Reset message state for new connection
    mqttMessageReceived = false;
    lastMqttMessage = "";
    
    // Generate unique client ID if not set
    if (strlen(mqttClientId) == 0) {
        snprintf(mqttClientId, sizeof(mqttClientId), "esp32p4_%08X", (unsigned int)ESP.getEfuseMac());
    }
    
    Serial.printf("Connecting to MQTT broker: %s:%d (TLS)\n", mqttBroker, mqttPort);
    
    // Configure MQTT client (ESP-IDF 5.x structure)
    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.hostname = mqttBroker;
    mqtt_cfg.broker.address.port = mqttPort;
    mqtt_cfg.credentials.client_id = mqttClientId;
    
    if (strlen(mqttUsername) > 0) {
        mqtt_cfg.credentials.username = mqttUsername;
        mqtt_cfg.credentials.authentication.password = mqttPassword;
    }
    
    // Configure TLS/SSL transport (for port 8883)
    if (mqttPort == 8883) {
        // Enable TLS transport
        mqtt_cfg.broker.address.transport = MQTT_TRANSPORT_OVER_SSL;
        
        // Use built-in CA certificate bundle for server verification (recommended)
        // This verifies the server's certificate using the built-in CA bundle
        // which includes common certificate authorities like those used by flespi.io
        mqtt_cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    } else {
        mqtt_cfg.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
    }
    
    // Session settings
    mqtt_cfg.session.keepalive = 60;
    
    // Network settings - configure timeouts and reconnection behavior
    // Disable auto-reconnect since we're managing connections manually
    // This prevents reconnection attempts after we intentionally disconnect
    mqtt_cfg.network.reconnect_timeout_ms = 0;  // Disable auto-reconnect (0 = disabled)
    mqtt_cfg.network.timeout_ms = 10000;  // Connection timeout (10 seconds)
    
    // Create and start MQTT client
    mqttClient = esp_mqtt_client_init(&mqtt_cfg);
    if (mqttClient == nullptr) {
        Serial.println("Failed to initialize MQTT client");
        return false;
    }
    
    // Register event handler
    esp_mqtt_client_register_event(mqttClient, MQTT_EVENT_ANY, mqttEventHandler, nullptr);
    
    // Start MQTT client (non-blocking)
    esp_err_t err = esp_mqtt_client_start(mqttClient);
    if (err != ESP_OK) {
        Serial.printf("Failed to start MQTT client: %s\n", esp_err_to_name(err));
        esp_mqtt_client_destroy(mqttClient);
        mqttClient = nullptr;
        return false;
    }
    
    // Wait for connection to establish (TLS can take a few seconds)
    uint32_t start = millis();
    while (!mqttConnected && (millis() - start < 10000)) {
        delay(200);
    }
    
    return mqttConnected;
}

// Check for MQTT messages (non-blocking, processes messages for a short time)
bool mqttCheckMessages(uint32_t timeoutMs) {
    if (mqttClient == nullptr || !mqttConnected) {
        return false;
    }
    
    uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        // esp-mqtt processes events in the background via event loop
        // Just check if we received a retained message
        if (mqttMessageReceived && lastMqttMessage.length() > 0) {
            return true;
        }
        
        // Check connection status - if we lost connection, exit early
        if (!mqttConnected || mqttClient == nullptr) {
            return false;
        }
        
        delay(50);
    }
    
    return false;  // No messages received
}

// Get the last received MQTT message
String mqttGetLastMessage() {
    return lastMqttMessage;
}

// ============================================================================
// MQTT Command Handling
// ============================================================================

/**
 * Extract command text from MQTT message
 * Handles both plain text and JSON messages
 * Returns lowercase, trimmed command string
 */
static String extractCommandFromMessage(const String& msg) {
    String command = msg;
    command.toLowerCase();
    command.trim();
    
    // If message is JSON, try to extract "text" field
    if (command.startsWith("{")) {
        // Simple JSON parsing - look for "text":"..." pattern
        int textStart = command.indexOf("\"text\"");
        if (textStart >= 0) {
            int colonPos = command.indexOf(':', textStart);
            int quoteStart = command.indexOf('"', colonPos);
            if (quoteStart >= 0) {
                int quoteEnd = command.indexOf('"', quoteStart + 1);
                if (quoteEnd >= 0) {
                    command = command.substring(quoteStart + 1, quoteEnd);
                    command.toLowerCase();
                    command.trim();
                }
            }
        }
    }
    
    return command;
}

/**
 * Extract parameter from command (e.g., "!go 5" -> "5")
 * Returns the parameter string, or empty string if no parameter
 */
static String extractCommandParameter(const String& command) {
    String cmd = command;
    cmd.trim();
    
    // Find the first space after the command
    int spacePos = cmd.indexOf(' ');
    if (spacePos < 0) {
        return "";  // No parameter
    }
    
    // Extract everything after the space
    String param = cmd.substring(spacePos + 1);
    param.trim();
    return param;
}

/**
 * Extract "from" field from JSON message
 * Returns the "from" field value (e.g., "+447816969344") or empty string if not found
 */
static String extractFromFieldFromMessage(const String& msg) {
    String result = "";
    
    // Only process JSON messages
    if (!msg.startsWith("{")) {
        return result;
    }
    
    // Look for "from":"..." pattern
    int fromStart = msg.indexOf("\"from\"");
    if (fromStart >= 0) {
        int colonPos = msg.indexOf(':', fromStart);
        int quoteStart = msg.indexOf('"', colonPos);
        if (quoteStart >= 0) {
            int quoteEnd = msg.indexOf('"', quoteStart + 1);
            if (quoteEnd >= 0) {
                result = msg.substring(quoteStart + 1, quoteEnd);
                result.trim();
            }
        }
    }
    
    return result;
}

/**
 * Main command dispatcher
 * Returns true if command was handled, false if unknown
 * 
 * To add a new command:
 * 1. Create a handler function: static bool handleXxxCommand()
 * 2. Add a case in this function: if (command == "!xxx") return handleXxxCommand();
 * 3. Add forward declaration near top of file (with other command handlers)
 * 
 * Example:
 *   // Forward declaration (near line 1419)
 *   static bool handleStatusCommand();
 *   
 *   // In handleMqttCommand() (this function):
 *   if (command == "!status") {
 *       return handleStatusCommand();
 *   }
 *   
 *   // Implementation (after handleClearCommand):
 *   static bool handleStatusCommand() {
 *       // Your command logic here
 *       return true;
 *   }
 */
static bool handleMqttCommand(const String& command, const String& originalMessage) {
    if (command == "!clear") {
        return handleClearCommand();
    }
    
    if (command == "!ping") {
        return handlePingCommand(originalMessage);
    }
    
    if (command == "!next") {
        return handleNextCommand();
    }
    
    if (command.startsWith("!go")) {
        String param = extractCommandParameter(command);
        return handleGoCommand(param);
    }
    
    if (command.startsWith("!text")) {
        String param = extractCommandParameter(command);
        return handleTextCommand(param);
    }
    
    // Add more commands here as needed:
    // if (command == "!status") {
    //     return handleStatusCommand();
    // }
    // if (command == "!reboot") {
    //     return handleRebootCommand();
    // }
    
    return false;  // Unknown command
}

/**
 * Handle !clear command - clear the e-ink display
 */
static bool handleClearCommand() {
    Serial.println("Processing !clear command...");
    
    // Ensure display is initialized (may not be on MQTT-only wakes)
    if (display.getBuffer() == nullptr) {
        Serial.println("Display not initialized - initializing now...");
        // Initialize SPI if needed
        displaySPI.begin(PIN_SPI_SCK, -1, PIN_SPI_MOSI, -1);
        
        if (!display.begin(PIN_CS0, PIN_CS1, PIN_DC, PIN_RESET, PIN_BUSY)) {
            Serial.println("ERROR: Display initialization failed!");
            return false;
        }
        Serial.println("Display initialized");
    }
    
    // Clear the display buffer
    Serial.println("Clearing display...");
    display.clear(EL133UF1_WHITE);
    
    // Update the display (this takes ~20-30 seconds)
    Serial.println("Updating display (this will take 20-30 seconds)...");
    display.update();
    Serial.println("Display cleared and updated");
    
    return true;  // Command handled successfully
}

/**
 * Handle !ping command - publish a ping response to MQTT with sender number
 */
static bool handlePingCommand(const String& originalMessage) {
    Serial.println("Processing !ping command...");
    
    // Extract sender number from original message
    String senderNumber = extractFromFieldFromMessage(originalMessage);
    if (senderNumber.length() == 0) {
        Serial.println("WARNING: Could not extract sender number from message, using empty number");
    } else {
        Serial.printf("Extracted sender number: %s\n", senderNumber.c_str());
    }
    
    // Reconnect to MQTT to publish response
    // (We disconnected after checking for messages, so need to reconnect)
    if (!mqttConnect()) {
        Serial.println("ERROR: Failed to connect to MQTT for ping response");
        return false;
    }
    
    // Wait for connection to be established
    delay(1000);
    
    // Build URL-encoded form response: "To=+447816969344&From=+447401492609&Body=Pong"
    // From is the device's number (hardcoded), To is the sender we're replying to
    String formResponse = "To=";
    formResponse += senderNumber;
    formResponse += "&From=+447401492609";
    formResponse += "&Body=Pong";
    
    // Publish ping response
    if (mqttClient != nullptr && strlen(mqttTopicPublish) > 0) {
        int msg_id = esp_mqtt_client_publish(mqttClient, mqttTopicPublish, formResponse.c_str(), formResponse.length(), 1, 0);
        if (msg_id > 0) {
            Serial.printf("Published ping response to %s (msg_id: %d): %s\n", mqttTopicPublish, msg_id, formResponse.c_str());
            // Give it a moment to send
            delay(500);
        } else {
            Serial.println("ERROR: Failed to publish ping response");
        }
    } else {
        Serial.println("ERROR: MQTT client not available or publish topic not set");
    }
    
    // Disconnect after publishing
    mqttDisconnect();
    delay(200);
    
    return true;  // Command handled successfully
}

/**
 * Handle !next command - advance to next media item and update display
 * This simulates what happens at the top of the hour: loads next image from media.txt,
 * displays it with time/date and quote overlays, and plays the associated audio
 */
static bool handleNextCommand() {
    Serial.println("Processing !next command...");
    
    // Ensure display is initialized (may not be on MQTT-only wakes)
    if (display.getBuffer() == nullptr) {
        Serial.println("Display not initialized - initializing now...");
        // Initialize SPI if needed
        displaySPI.begin(PIN_SPI_SCK, -1, PIN_SPI_MOSI, -1);
        
        if (!display.begin(PIN_CS0, PIN_CS1, PIN_DC, PIN_RESET, PIN_BUSY)) {
            Serial.println("ERROR: Display initialization failed!");
            return false;
        }
        Serial.println("Display initialized");
    }
    
#if SDMMC_ENABLED
    // Mount SD card if needed
    if (!sdCardMounted && sd_card == nullptr) {
        Serial.println("Mounting SD card...");
        if (!sdInitDirect(false)) {
            Serial.println("ERROR: Failed to mount SD card!");
            return false;
        }
    }
    
    // Load configuration files from SD card if needed
    if (!g_quotes_loaded) {
        loadQuotesFromSD();
    }
    if (!g_media_mappings_loaded) {
        loadMediaMappingsFromSD();
    }
    
    // Check if we have media mappings
    if (!g_media_mappings_loaded || g_media_mappings.size() == 0) {
        Serial.println("ERROR: No media.txt mappings found - cannot advance to next item");
        return false;
    }
    
    // Advance to next media item (pngDrawFromMediaMappings will increment lastMediaIndex)
    Serial.printf("Current media index: %lu (of %zu)\n", 
                  (unsigned long)lastMediaIndex, g_media_mappings.size());
    
    // Load the next PNG from media.txt (this increments lastMediaIndex automatically)
    uint32_t sd_ms = 0, dec_ms = 0;
    bool ok = pngDrawFromMediaMappings(&sd_ms, &dec_ms);
    if (!ok) {
        Serial.println("ERROR: Failed to load next image from media.txt");
        return false;
    }
    
    Serial.printf("PNG SD read: %lu ms, decode+draw: %lu ms\n", (unsigned long)sd_ms, (unsigned long)dec_ms);
    Serial.printf("Now at media index: %lu\n", (unsigned long)lastMediaIndex);
    
    // Get current time for overlay
    time_t now = time(nullptr);
    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);
    
    char timeBuf[16];
    char dateBuf[48];
    bool timeValid = (now > 1577836800); // after 2020-01-01
    if (timeValid) {
        strftime(timeBuf, sizeof(timeBuf), "%H:%M", &tm_utc);
        
        // Format date as "Saturday 13th of December 2025"
        char dayName[12], monthName[12];
        strftime(dayName, sizeof(dayName), "%A", &tm_utc);
        strftime(monthName, sizeof(monthName), "%B", &tm_utc);
        
        int day = tm_utc.tm_mday;
        int year = tm_utc.tm_year + 1900;
        
        const char* suffix;
        if (day >= 11 && day <= 13) {
            suffix = "th";
        } else {
            switch (day % 10) {
                case 1: suffix = "st"; break;
                case 2: suffix = "nd"; break;
                case 3: suffix = "rd"; break;
                default: suffix = "th"; break;
            }
        }
        
        snprintf(dateBuf, sizeof(dateBuf), "%s %d%s of %s %d", 
                 dayName, day, suffix, monthName, year);
    } else {
        snprintf(timeBuf, sizeof(timeBuf), "--:--");
        snprintf(dateBuf, sizeof(dateBuf), "time not set");
    }
    
    // Set keepout margins and clear exclusion zones
    textPlacement.setKeepout(100);
    textPlacement.clearExclusionZones();
    
    // Draw time/date overlay (simplified version - reuse logic from hourly update)
    float timeFontSize = 160.0f;
    float dateFontSize = 48.0f;
    const int16_t gapBetween = 20;
    const int16_t timeOutline = 3;
    const int16_t dateOutline = 2;
    
    int16_t timeW = ttf.getTextWidth(timeBuf, timeFontSize) + (timeOutline * 2);
    int16_t timeH = ttf.getTextHeight(timeFontSize) + (timeOutline * 2);
    int16_t dateW = ttf.getTextWidth(dateBuf, dateFontSize) + (dateOutline * 2);
    int16_t dateH = ttf.getTextHeight(dateFontSize) + (dateOutline * 2);
    
    int16_t blockW = max(timeW, dateW);
    int16_t blockH = timeH + gapBetween + dateH;
    
    TextPlacementRegion bestPos = textPlacement.scanForBestPosition(
        &display, blockW, blockH,
        EL133UF1_WHITE, EL133UF1_BLACK);
    
    int16_t timeY = bestPos.y - (blockH/2) + (timeH/2);
    int16_t dateY = bestPos.y + (blockH/2) - (dateH/2);
    
    ttf.drawTextAlignedOutlined(bestPos.x, timeY, timeBuf, timeFontSize,
                                EL133UF1_WHITE, EL133UF1_BLACK,
                                ALIGN_CENTER, ALIGN_MIDDLE, timeOutline);
    ttf.drawTextAlignedOutlined(bestPos.x, dateY, dateBuf, dateFontSize,
                                EL133UF1_WHITE, EL133UF1_BLACK,
                                ALIGN_CENTER, ALIGN_MIDDLE, dateOutline);
    
    textPlacement.addExclusionZone(bestPos, 150);
    
    // Draw quote overlay
    using Quote = TextPlacementAnalyzer::Quote;
    Quote selectedQuote;
    
    if (g_quotes_loaded && g_loaded_quotes.size() > 0) {
        int randomIndex = random(g_loaded_quotes.size());
        selectedQuote.text = g_loaded_quotes[randomIndex].text.c_str();
        selectedQuote.author = g_loaded_quotes[randomIndex].author.c_str();
    } else {
        static const Quote fallbackQuotes[] = {
            {"Vulnerability is not weakness; it's our greatest measure of courage", "Brene Brown"},
            {"The only way to do great work is to love what you do", "Steve Jobs"},
            {"In the middle of difficulty lies opportunity", "Albert Einstein"},
            {"Be yourself; everyone else is already taken", "Oscar Wilde"},
        };
        static const int numQuotes = sizeof(fallbackQuotes) / sizeof(fallbackQuotes[0]);
        selectedQuote = fallbackQuotes[random(numQuotes)];
    }
    
    float quoteFontSize = 48.0f;
    float authorFontSize = 32.0f;
    
    TextPlacementAnalyzer::QuoteLayoutResult quoteLayout = textPlacement.scanForBestQuotePosition(
        &display, &ttf, selectedQuote, quoteFontSize, authorFontSize,
        EL133UF1_WHITE, EL133UF1_BLACK,
        3, 3);
    
    textPlacement.drawQuote(&ttf, quoteLayout, selectedQuote.author,
                            quoteFontSize, authorFontSize,
                            EL133UF1_WHITE, EL133UF1_BLACK, 2);
    
    // Update display
    Serial.println("Updating display (e-ink refresh - this will take 20-30 seconds)...");
    display.update();
    Serial.println("Display updated");
    
    // Play audio file for this image
    String audioFile = getAudioForImage(g_lastImagePath);
    if (audioFile.length() > 0) {
        Serial.printf("Playing audio: %s\n", audioFile.c_str());
        strncpy(lastAudioFile, audioFile.c_str(), sizeof(lastAudioFile) - 1);
        lastAudioFile[sizeof(lastAudioFile) - 1] = '\0';
        playWavFile(audioFile);
    } else {
        Serial.println("No audio file mapped for this image, playing beep.wav");
        strncpy(lastAudioFile, "beep.wav", sizeof(lastAudioFile) - 1);
        playWavFile("beep.wav");
    }
    audio_stop();
    
    Serial.println("!next command completed successfully");
    return true;
#else
    Serial.println("ERROR: SD card support not enabled - cannot load media");
    return false;
#endif
}

/**
 * Handle !go command - jump to a specific media item by index (1-based)
 * Format: !go <number> (e.g., "!go 1" jumps to the first item, "!go 5" jumps to the 5th item)
 * Validates the index is within bounds before updating
 * Note: User input is 1-based, but internally we use 0-based indexing
 */
static bool handleGoCommand(const String& parameter) {
    Serial.println("Processing !go command...");
    
    // Check if parameter was provided
    if (parameter.length() == 0) {
        Serial.println("ERROR: !go command requires a number parameter (e.g., !go 1)");
        return false;
    }
    
    // Parse the parameter as a number (1-based from user)
    int userInput = parameter.toInt();
    if (userInput < 1) {
        Serial.println("ERROR: Number must be 1 or greater");
        return false;
    }
    
    // Convert to 0-based index
    int targetIndex = userInput - 1;
    
    // Ensure display is initialized (may not be on MQTT-only wakes)
    if (display.getBuffer() == nullptr) {
        Serial.println("Display not initialized - initializing now...");
        displaySPI.begin(PIN_SPI_SCK, -1, PIN_SPI_MOSI, -1);
        
        if (!display.begin(PIN_CS0, PIN_CS1, PIN_DC, PIN_RESET, PIN_BUSY)) {
            Serial.println("ERROR: Display initialization failed!");
            return false;
        }
        Serial.println("Display initialized");
    }
    
#if SDMMC_ENABLED
    // Mount SD card if needed
    if (!sdCardMounted && sd_card == nullptr) {
        Serial.println("Mounting SD card...");
        if (!sdInitDirect(false)) {
            Serial.println("ERROR: Failed to mount SD card!");
            return false;
        }
    }
    
    // Load configuration files from SD card if needed
    if (!g_quotes_loaded) {
        loadQuotesFromSD();
    }
    if (!g_media_mappings_loaded) {
        loadMediaMappingsFromSD();
    }
    
    // Check if we have media mappings
    if (!g_media_mappings_loaded || g_media_mappings.size() == 0) {
        Serial.println("ERROR: No media.txt mappings found - cannot jump to specific item");
        return false;
    }
    
    // Validate index is within bounds (1 to size for user, 0 to size-1 internally)
    size_t mediaCount = g_media_mappings.size();
    if (userInput > (int)mediaCount) {
        Serial.printf("ERROR: Number %d is out of bounds. Valid range: 1 to %zu\n", 
                      userInput, mediaCount);
        return false;
    }
    
    Serial.printf("Jumping to media item %d of %zu (index %d)\n", userInput, mediaCount, targetIndex);
    
    // Set the index directly (without incrementing)
    // We need to set it to targetIndex-1 so that pngDrawFromMediaMappings increments it to targetIndex
    size_t mediaCount_uint = g_media_mappings.size();
    lastMediaIndex = (targetIndex - 1 + mediaCount_uint) % mediaCount_uint;
    
    uint32_t sd_ms = 0, dec_ms = 0;
    bool ok = pngDrawFromMediaMappings(&sd_ms, &dec_ms);
    if (!ok) {
        Serial.println("ERROR: Failed to load image from media.txt");
        return false;
    }
    
    // Verify we're at the correct index
    if (lastMediaIndex != (size_t)targetIndex) {
        Serial.printf("WARNING: Expected index %d but got %lu - correcting\n", 
                      targetIndex, (unsigned long)lastMediaIndex);
        lastMediaIndex = targetIndex;
    }
    
    Serial.printf("PNG SD read: %lu ms, decode+draw: %lu ms\n", (unsigned long)sd_ms, (unsigned long)dec_ms);
    Serial.printf("Now at media index: %lu\n", (unsigned long)lastMediaIndex);
    
    // Get current time for overlay
    time_t now = time(nullptr);
    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);
    
    char timeBuf[16];
    char dateBuf[48];
    bool timeValid = (now > 1577836800); // after 2020-01-01
    if (timeValid) {
        strftime(timeBuf, sizeof(timeBuf), "%H:%M", &tm_utc);
        
        // Format date as "Saturday 13th of December 2025"
        char dayName[12], monthName[12];
        strftime(dayName, sizeof(dayName), "%A", &tm_utc);
        strftime(monthName, sizeof(monthName), "%B", &tm_utc);
        
        int day = tm_utc.tm_mday;
        int year = tm_utc.tm_year + 1900;
        
        const char* suffix;
        if (day >= 11 && day <= 13) {
            suffix = "th";
        } else {
            switch (day % 10) {
                case 1: suffix = "st"; break;
                case 2: suffix = "nd"; break;
                case 3: suffix = "rd"; break;
                default: suffix = "th"; break;
            }
        }
        
        snprintf(dateBuf, sizeof(dateBuf), "%s %d%s of %s %d", 
                 dayName, day, suffix, monthName, year);
    } else {
        snprintf(timeBuf, sizeof(timeBuf), "--:--");
        snprintf(dateBuf, sizeof(dateBuf), "time not set");
    }
    
    // Set keepout margins and clear exclusion zones
    textPlacement.setKeepout(100);
    textPlacement.clearExclusionZones();
    
    // Draw time/date overlay
    float timeFontSize = 160.0f;
    float dateFontSize = 48.0f;
    const int16_t gapBetween = 20;
    const int16_t timeOutline = 3;
    const int16_t dateOutline = 2;
    
    int16_t timeW = ttf.getTextWidth(timeBuf, timeFontSize) + (timeOutline * 2);
    int16_t timeH = ttf.getTextHeight(timeFontSize) + (timeOutline * 2);
    int16_t dateW = ttf.getTextWidth(dateBuf, dateFontSize) + (dateOutline * 2);
    int16_t dateH = ttf.getTextHeight(dateFontSize) + (dateOutline * 2);
    
    int16_t blockW = max(timeW, dateW);
    int16_t blockH = timeH + gapBetween + dateH;
    
    TextPlacementRegion bestPos = textPlacement.scanForBestPosition(
        &display, blockW, blockH,
        EL133UF1_WHITE, EL133UF1_BLACK);
    
    int16_t timeY = bestPos.y - (blockH/2) + (timeH/2);
    int16_t dateY = bestPos.y + (blockH/2) - (dateH/2);
    
    ttf.drawTextAlignedOutlined(bestPos.x, timeY, timeBuf, timeFontSize,
                                EL133UF1_WHITE, EL133UF1_BLACK,
                                ALIGN_CENTER, ALIGN_MIDDLE, timeOutline);
    ttf.drawTextAlignedOutlined(bestPos.x, dateY, dateBuf, dateFontSize,
                                EL133UF1_WHITE, EL133UF1_BLACK,
                                ALIGN_CENTER, ALIGN_MIDDLE, dateOutline);
    
    textPlacement.addExclusionZone(bestPos, 150);
    
    // Draw quote overlay
    using Quote = TextPlacementAnalyzer::Quote;
    Quote selectedQuote;
    
    if (g_quotes_loaded && g_loaded_quotes.size() > 0) {
        int randomIndex = random(g_loaded_quotes.size());
        selectedQuote.text = g_loaded_quotes[randomIndex].text.c_str();
        selectedQuote.author = g_loaded_quotes[randomIndex].author.c_str();
    } else {
        static const Quote fallbackQuotes[] = {
            {"Vulnerability is not weakness; it's our greatest measure of courage", "Brene Brown"},
            {"The only way to do great work is to love what you do", "Steve Jobs"},
            {"In the middle of difficulty lies opportunity", "Albert Einstein"},
            {"Be yourself; everyone else is already taken", "Oscar Wilde"},
        };
        static const int numQuotes = sizeof(fallbackQuotes) / sizeof(fallbackQuotes[0]);
        selectedQuote = fallbackQuotes[random(numQuotes)];
    }
    
    float quoteFontSize = 48.0f;
    float authorFontSize = 32.0f;
    
    TextPlacementAnalyzer::QuoteLayoutResult quoteLayout = textPlacement.scanForBestQuotePosition(
        &display, &ttf, selectedQuote, quoteFontSize, authorFontSize,
        EL133UF1_WHITE, EL133UF1_BLACK,
        3, 3);
    
    textPlacement.drawQuote(&ttf, quoteLayout, selectedQuote.author,
                            quoteFontSize, authorFontSize,
                            EL133UF1_WHITE, EL133UF1_BLACK, 2);
    
    // Update display
    Serial.println("Updating display (e-ink refresh - this will take 20-30 seconds)...");
    display.update();
    Serial.println("Display updated");
    
    // Play audio file for this image
    String audioFile = getAudioForImage(g_lastImagePath);
    if (audioFile.length() > 0) {
        Serial.printf("Playing audio: %s\n", audioFile.c_str());
        strncpy(lastAudioFile, audioFile.c_str(), sizeof(lastAudioFile) - 1);
        lastAudioFile[sizeof(lastAudioFile) - 1] = '\0';
        playWavFile(audioFile);
    } else {
        Serial.println("No audio file mapped for this image, playing beep.wav");
        strncpy(lastAudioFile, "beep.wav", sizeof(lastAudioFile) - 1);
        playWavFile("beep.wav");
    }
    audio_stop();
    
    Serial.printf("!go command completed successfully - now at item %lu of %zu\n", 
                  (unsigned long)(lastMediaIndex + 1), mediaCount);
    return true;
#else
    Serial.println("ERROR: SD card support not enabled - cannot load media");
    return false;
#endif
}

/**
 * Handle !text command - display text centered on screen, as large as possible
 * Format: !text <text with spaces> (e.g., "!text Hello there!")
 * Clears the display buffer and draws the text with outline, centered
 */
static bool handleTextCommand(const String& parameter) {
    Serial.println("Processing !text command...");
    
    // Check if parameter was provided
    if (parameter.length() == 0) {
        Serial.println("ERROR: !text command requires text parameter (e.g., !text Hello there!)");
        return false;
    }
    
    Serial.printf("Text to display: \"%s\"\n", parameter.c_str());
    
    // Ensure display is initialized (may not be on MQTT-only wakes)
    if (display.getBuffer() == nullptr) {
        Serial.println("Display not initialized - initializing now...");
        displaySPI.begin(PIN_SPI_SCK, -1, PIN_SPI_MOSI, -1);
        
        if (!display.begin(PIN_CS0, PIN_CS1, PIN_DC, PIN_RESET, PIN_BUSY)) {
            Serial.println("ERROR: Display initialization failed!");
            return false;
        }
        Serial.println("Display initialized");
    }
    
    // Clear the display buffer to white
    Serial.println("Clearing display buffer...");
    display.clear(EL133UF1_WHITE);
    
    // Get display dimensions
    int16_t displayWidth = display.width();
    int16_t displayHeight = display.height();
    Serial.printf("Display size: %dx%d\n", displayWidth, displayHeight);
    
    // Find maximum font size that fits the text on screen
    // Use binary search for efficiency
    const float minFontSize = 20.0f;
    const float maxFontSize = 400.0f;
    const int16_t outlineWidth = 3;  // Outline thickness
    const int16_t padding = 40;  // Padding from edges
    
    float fontSize = minFontSize;
    float low = minFontSize;
    float high = maxFontSize;
    
    // Binary search for optimal font size
    while (high - low > 1.0f) {
        fontSize = (low + high) / 2.0f;
        
        int16_t textWidth = ttf.getTextWidth(parameter.c_str(), fontSize) + (outlineWidth * 2);
        int16_t textHeight = ttf.getTextHeight(fontSize) + (outlineWidth * 2);
        
        if (textWidth <= (displayWidth - padding) && textHeight <= (displayHeight - padding)) {
            // Fits - try larger
            low = fontSize;
        } else {
            // Too large - try smaller
            high = fontSize;
        }
    }
    
    fontSize = low;  // Use the largest size that fits
    
    // Final size calculation with margins
    int16_t textWidth = ttf.getTextWidth(parameter.c_str(), fontSize) + (outlineWidth * 2);
    int16_t textHeight = ttf.getTextHeight(fontSize) + (outlineWidth * 2);
    
    // If still doesn't fit perfectly, scale it down
    if (textWidth > (displayWidth - padding) || textHeight > (displayHeight - padding)) {
        float scaleW = (float)(displayWidth - padding) / (float)textWidth;
        float scaleH = (float)(displayHeight - padding) / (float)textHeight;
        float scale = (scaleW < scaleH) ? scaleW : scaleH;
        fontSize = fontSize * scale * 0.95f;  // 95% to add small margin
        
        textWidth = ttf.getTextWidth(parameter.c_str(), fontSize) + (outlineWidth * 2);
        textHeight = ttf.getTextHeight(fontSize) + (outlineWidth * 2);
    }
    
    Serial.printf("Optimal font size: %.1f, text dimensions: %dx%d\n", fontSize, textWidth, textHeight);
    
    // Calculate centered position
    int16_t centerX = displayWidth / 2;
    int16_t centerY = displayHeight / 2;
    
    // Draw text centered with outline
    Serial.println("Drawing text...");
    ttf.drawTextAlignedOutlined(centerX, centerY, parameter.c_str(), fontSize,
                                EL133UF1_WHITE, EL133UF1_BLACK,
                                ALIGN_CENTER, ALIGN_MIDDLE, outlineWidth);
    
    // Update display
    Serial.println("Updating display (e-ink refresh - this will take 20-30 seconds)...");
    display.update();
    Serial.println("Display updated");
    
    Serial.println("!text command completed successfully");
    return true;
}

// Disconnect from MQTT
void mqttDisconnect() {
    if (mqttClient != nullptr) {
        Serial.println("Disconnecting from MQTT...");
        // Unregister event handler first to prevent events during shutdown
        esp_mqtt_client_unregister_event(mqttClient, MQTT_EVENT_ANY, mqttEventHandler);
        delay(100);
        // Stop the client (this should prevent auto-reconnect)
        esp_mqtt_client_stop(mqttClient);
        delay(300);  // Give it more time to clean up and stop reconnection attempts
        // Destroy the client
        esp_mqtt_client_destroy(mqttClient);
        mqttClient = nullptr;
        mqttConnected = false;
        Serial.println("MQTT disconnected and cleaned up");
    }
}

// Set MQTT configuration via serial input (disabled - using hardcoded values)
void mqttSetConfig() {
    Serial.println("\n=== MQTT Configuration ===");
    Serial.println("MQTT configuration is now hardcoded.");
    Serial.println("Edit the #defines in the source code to change:");
    Serial.println("  MQTT_BROKER_HOSTNAME");
    Serial.println("  MQTT_BROKER_PORT");
    Serial.println("  MQTT_USERNAME");
    Serial.println("  MQTT_PASSWORD");
    Serial.println("  MQTT_TOPIC_SUBSCRIBE");
    Serial.println("  MQTT_TOPIC_PUBLISH");
    Serial.println("==========================\n");
    mqttStatus();  // Show current config
}

// Show MQTT configuration status
void mqttStatus() {
    Serial.println("\n=== MQTT Status ===");
    mqttLoadConfig();  // Reload hardcoded config
    
    if (strlen(mqttBroker) > 0) {
        Serial.printf("Broker: %s:%d\n", mqttBroker, mqttPort);
        Serial.printf("Client ID: %s\n", strlen(mqttClientId) > 0 ? mqttClientId : "(auto-generated)");
        if (strlen(mqttUsername) > 0) {
            Serial.printf("Username: %s\n", mqttUsername);
            Serial.println("Password: ***");
        } else {
            Serial.println("Authentication: None");
        }
        if (strlen(mqttTopicSubscribe) > 0) {
            Serial.printf("Subscribe: %s\n", mqttTopicSubscribe);
        } else {
            Serial.println("Subscribe: (not configured)");
        }
        if (strlen(mqttTopicPublish) > 0) {
            Serial.printf("Publish: %s\n", mqttTopicPublish);
        } else {
            Serial.println("Publish: (not configured)");
        }
        Serial.printf("Connection: %s\n", mqttConnected ? "Connected" : "Disconnected");
    } else {
        Serial.println("MQTT not configured.");
        Serial.println("Use 'M' to configure MQTT settings.");
    }
    Serial.println("==================\n");
}

// Forward declarations
void wifiClearCredentials();

// Enter interactive configuration mode - loops until credentials are successfully set
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

// Load WiFi credentials from NVS
// Returns true if credentials were loaded successfully, false if NVS failed or credentials missing
bool wifiLoadCredentials() {
    // Clear credentials first
    wifiSSID[0] = '\0';
    wifiPSK[0] = '\0';
    
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
        Serial.printf("Loaded WiFi credentials for: %s\n", wifiSSID);
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

// Persistent WiFi connection function - keeps trying until connected
// Returns true if connected, false only if credentials are missing
bool wifiConnectPersistent(int maxRetries, uint32_t timeoutPerAttemptMs, bool required) {
    if (strlen(wifiSSID) == 0) {
        Serial.println("No WiFi credentials configured");
        return false;
    }
    
    Serial.printf("Connecting to WiFi: %s (persistent mode)\n", wifiSSID);
    
    // Configure WiFi for better connection reliability
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);  // Disable WiFi sleep for better connection stability
    WiFi.setTxPower(WIFI_POWER_19_5dBm);  // Maximum power for better range
    WiFi.setAutoReconnect(true);  // Enable auto-reconnect
    
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

// Save WiFi credentials to NVS
void wifiSaveCredentials() {
    wifiPrefs.begin("wifi", false);  // Read-write
    wifiPrefs.putString("ssid", wifiSSID);
    wifiPrefs.putString("psk", wifiPSK);
    wifiPrefs.end();
    Serial.println("WiFi credentials saved to NVS");
}

// Clear WiFi credentials from NVS
void wifiClearCredentials() {
    wifiPrefs.begin("wifi", false);
    wifiPrefs.clear();
    wifiPrefs.end();
    wifiSSID[0] = '\0';
    wifiPSK[0] = '\0';
    Serial.println("WiFi credentials cleared from NVS");
}

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
    
    // Use persistent WiFi connection for manual connect command
    if (wifiConnectPersistent(10, 30000, false)) {  // 10 retries, 30s per attempt, not required (user command)
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
    Serial.println("Enter SSID (or 'clear' to delete saved credentials):");
    
    // Wait for input
    while (!Serial.available()) delay(10);
    delay(100);  // Wait for full input
    
    String ssid = Serial.readStringUntil('\n');
    ssid.trim();
    
    if (ssid.length() == 0) {
        Serial.println("Cancelled.");
        return;
    }
    
    if (ssid == "clear") {
        wifiClearCredentials();
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
    
    // Save to NVS for persistence across reboots
    wifiSaveCredentials();
    
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
        Serial.println("Internal RTC synchronized!");
    } else {
        Serial.println(" FAILED!");
    }
    Serial.println("====================\n");
}

// ============================================================================
// SD Card Functions (SDMMC)
// ============================================================================

#if SDMMC_ENABLED
// Note: sdCardMounted, sd_card, and ldo_vo4_handle are declared earlier in the file

void sdDiagnostics() {
    Serial.println("\n=== SD Card Pin Diagnostics ===");
    
    // Check power control pin first
    Serial.printf("Power control: GPIO%d\n", PIN_SD_POWER);
    pinMode(PIN_SD_POWER, INPUT);  // Temporarily set as input to read state
    int powerState = digitalRead(PIN_SD_POWER);
    Serial.printf("  GPIO%d state: %s -> MOSFET %s -> SD card %s\n", 
                  PIN_SD_POWER,
                  powerState ? "HIGH" : "LOW",
                  powerState ? "OFF" : "ON",
                  powerState ? "UNPOWERED" : "POWERED");
    
    Serial.printf("\nData pins (IOMUX Slot 0):\n");
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
    
    Serial.println("\nTroubleshooting:");
    Serial.println("  - If GPIO45 is HIGH: SD card has no power! Press 'O' to power on");
    Serial.println("  - If all data pins HIGH: card may not be inserted");
    Serial.println("  - If CMD/D0-D3 LOW with card inserted: wiring is likely correct");
    Serial.println("================================\n");
}

// Enable LDO channel 4 (powers external pull-up resistors for SD card)
// Note: ldo_vo4_handle is declared earlier in the file
bool enableLdoVO4() {
    if (ldo_vo4_handle != nullptr) {
        Serial.println("LDO_VO4 already enabled");
        return true;
    }
    
    Serial.println("Enabling LDO_VO4 (3.3V for SD pull-ups)...");
    
    esp_ldo_channel_config_t ldo_config = {
        .chan_id = 4,
        .voltage_mv = 3300,
        .flags = {
            .adjustable = 0,
            .owned_by_hw = 0,
        }
    };
    
    esp_err_t ret = esp_ldo_acquire_channel(&ldo_config, &ldo_vo4_handle);
    if (ret != ESP_OK) {
        Serial.printf("Failed to acquire LDO_VO4: %s (0x%x)\n", esp_err_to_name(ret), ret);
        esp_ldo_dump(stdout);
        return false;
    }
    
    Serial.println("LDO_VO4 enabled at 3.3V");
    return true;
}

// Enable SD card power by driving GPIO45 LOW (turns on P-MOSFET Q1)
void sdPowerOn() {
    Serial.printf("Enabling SD card power (GPIO%d LOW)...\n", PIN_SD_POWER);
    pinMode(PIN_SD_POWER, OUTPUT);
    digitalWrite(PIN_SD_POWER, LOW);  // LOW = MOSFET ON = SD powered
    delay(10);  // Allow power to stabilize
    Serial.println("SD card power enabled");
}

// Disable SD card power by driving GPIO45 HIGH (turns off P-MOSFET Q1)
void sdPowerOff() {
    Serial.printf("Disabling SD card power (GPIO%d HIGH)...\n", PIN_SD_POWER);
    pinMode(PIN_SD_POWER, OUTPUT);
    digitalWrite(PIN_SD_POWER, HIGH);  // HIGH = MOSFET OFF = SD unpowered
    delay(10);
    Serial.println("SD card power disabled");
}

// Power cycle the SD card (useful for resetting stuck cards)
void sdPowerCycle() {
    Serial.println("Power cycling SD card...");
    sdPowerOff();
    delay(100);  // Keep power off for 100ms
    sdPowerOn();
    delay(50);   // Allow card to initialize
    Serial.println("SD card power cycled");
}

// Direct ESP-IDF SD card initialization with internal pull-ups
bool sdInitDirect(bool mode1bit) {
    if (sd_card != nullptr) {
        Serial.println("SD card already mounted (direct)");
        return true;
    }
    
    Serial.println("\n=== Initializing SD Card (ESP-IDF Direct) ===");
    Serial.printf("Pins: CLK=%d, CMD=%d, D0=%d, D1=%d, D2=%d, D3=%d\n",
                  PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3);
    Serial.printf("Power control: GPIO%d (active LOW)\n", PIN_SD_POWER);
    
    // Step 1: Enable LDO_VO4 for external pull-up resistors (5.1K to LDO_VO4)
    if (!enableLdoVO4()) {
        Serial.println("Warning: LDO_VO4 not enabled, relying on internal pull-ups only");
    }
    
    // Step 2: Enable SD card power via GPIO45 -> MOSFET Q1
    // GPIO45 LOW = MOSFET ON = SD card VDD powered from ESP_3V3
    sdPowerOn();
    
    // Configure SDMMC host
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;  // 40 MHz for faster transfers
    if (mode1bit) {
        host.flags = SDMMC_HOST_FLAG_1BIT;
    }
    
    // Configure slot with internal pull-ups (as per Waveshare docs)
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = mode1bit ? 1 : 4;
    slot_config.clk = (gpio_num_t)PIN_SD_CLK;
    slot_config.cmd = (gpio_num_t)PIN_SD_CMD;
    slot_config.d0 = (gpio_num_t)PIN_SD_D0;
    slot_config.d1 = (gpio_num_t)PIN_SD_D1;
    slot_config.d2 = (gpio_num_t)PIN_SD_D2;
    slot_config.d3 = (gpio_num_t)PIN_SD_D3;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;  // KEY: Enable internal pull-ups!
    
    Serial.println("Internal pull-ups ENABLED via SDMMC_SLOT_FLAG_INTERNAL_PULLUP");
    Serial.printf("Trying %s mode at %d kHz...\n", mode1bit ? "1-bit" : "4-bit", host.max_freq_khz);
    
    // Mount filesystem
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    
    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &sd_card);
    
    if (ret != ESP_OK) {
        Serial.printf("Mount failed: %s (0x%x)\n", esp_err_to_name(ret), ret);
        if (ret == ESP_ERR_TIMEOUT) {
            Serial.println("Timeout - check if card is inserted");
        }
        sd_card = nullptr;
        return false;
    }
    
    // Print card info
    Serial.println("\nSD card mounted successfully!");
    sdmmc_card_print_info(stdout, sd_card);
    Serial.println("==================================\n");
    
    sdCardMounted = true;
    return true;
}

void sdUnmountDirect() {
    if (sd_card == nullptr) {
        Serial.println("SD card not mounted");
        return;
    }
    
    esp_vfs_fat_sdcard_unmount("/sdcard", sd_card);
    sd_card = nullptr;
    sdCardMounted = false;
    Serial.println("SD card unmounted");
}

bool sdInit(bool mode1bit) {
    if (sdCardMounted) {
        Serial.println("SD card already mounted");
        return true;
    }
    
    Serial.println("\n=== Initializing SD Card (SDMMC - Arduino) ===");
    Serial.printf("Pins: CLK=%d, CMD=%d, D0=%d, D1=%d, D2=%d, D3=%d\n",
                  PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3);
    Serial.printf("Power control: GPIO%d (active LOW)\n", PIN_SD_POWER);
    
    // Step 1: Enable LDO_VO4 for external pull-up resistors (5.1K to LDO_VO4)
    if (!enableLdoVO4()) {
        Serial.println("Warning: LDO_VO4 not enabled, relying on internal pull-ups only");
    }
    
    // Step 2: Enable SD card power via GPIO45 -> MOSFET Q1
    // GPIO45 LOW = MOSFET ON = SD card VDD powered from ESP_3V3
    sdPowerOn();
    
    // Set custom pins (for GPIO matrix mode)
    // Note: ESP32-P4 Slot 0 uses IOMUX, so pins must match the IOMUX pins
    if (!SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3)) {
        Serial.println("SD_MMC.setPins failed!");
        return false;
    }
    
    // Use external power (power_channel = -1) since Waveshare board has its own MOSFET-switched power
    SD_MMC.setPowerChannel(-1);
    Serial.println("Using GPIO45-controlled MOSFET power");
    
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
    
    // If using direct ESP-IDF mount
    if (sd_card != nullptr) {
        Serial.printf("Card Size: %llu MB\n", 
            ((uint64_t)sd_card->csd.capacity * sd_card->csd.sector_size) / (1024 * 1024));
        Serial.printf("Sector Size: %d bytes\n", sd_card->csd.sector_size);
        Serial.printf("Speed: %d kHz\n", sd_card->max_freq_khz);
    } else {
        // Using Arduino SD_MMC wrapper
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
    }
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

// ============================================================================
// BMP Loading from SD Card
// ============================================================================

#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include "ff.h"  // FatFs native functions

// Get the SD card mount point (depends on which init method was used)
const char* sdGetMountPoint() {
    // Both methods mount at /sdcard
    return "/sdcard";
}

// Count BMP files in a directory using FatFs (returns count, stores paths in array if provided)
int bmpCountFiles(const char* dirname, String* paths = nullptr, int maxCount = 0) {
    // FatFs uses drive number prefix
    String fatfsPath = "0:";
    if (strcmp(dirname, "/") != 0) {
        fatfsPath += dirname;
    }
    
    FF_DIR dir;
    FILINFO fno;
    FRESULT res;
    
    res = f_opendir(&dir, fatfsPath.c_str());
    if (res != FR_OK) {
        // Try without drive prefix
        res = f_opendir(&dir, dirname);
        if (res != FR_OK) {
            return 0;
        }
    }
    
    int count = 0;
    
    while (true) {
        res = f_readdir(&dir, &fno);
        if (res != FR_OK || fno.fname[0] == 0) {
            break;  // Error or end of directory
        }
        
        // Skip directories
        if (fno.fattrib & AM_DIR) {
            continue;
        }
        
        // Check if it's a BMP file
        String name = String(fno.fname);
        String nameLower = name;
        nameLower.toLowerCase();
        if (nameLower.endsWith(".bmp")) {
            if (paths && count < maxCount) {
                if (strcmp(dirname, "/") == 0) {
                    paths[count] = "/" + name;
                } else {
                    paths[count] = String(dirname) + "/" + name;
                }
            }
            count++;
        }
    }
    
    f_closedir(&dir);
    return count;
}

// Load a random BMP from SD card and display it
void bmpLoadRandom(const char* dirname = "/") {
    Serial.println("\n=== Loading Random BMP ===");
    uint32_t totalStart = millis();
    
    if (!sdCardMounted && sd_card == nullptr) {
        Serial.println("SD card not mounted. Mounting...");
        if (!sdInitDirect(false)) {
            Serial.println("Failed to mount SD card!");
            return;
        }
    }
    
    // First pass: count BMP files
    int bmpCount = bmpCountFiles(dirname);
    if (bmpCount == 0) {
        Serial.printf("No BMP files found in %s\n", dirname);
        Serial.println("Tip: Place some .bmp files on the SD card root");
        return;
    }
    Serial.printf("Found %d BMP files\n", bmpCount);
    
    // Allocate array for paths (max 100)
    int maxFiles = min(bmpCount, 100);
    String* paths = new String[maxFiles];
    if (!paths) {
        Serial.println("Failed to allocate path array!");
        return;
    }
    
    // Second pass: collect paths
    bmpCountFiles(dirname, paths, maxFiles);
    
    // Pick a random file
    srand(millis());  // Seed with current time
    int randomIndex = rand() % maxFiles;
    String selectedPath = paths[randomIndex];
    delete[] paths;
    
    Serial.printf("Selected: %s\n", selectedPath.c_str());
    
    // Build FatFs path (with drive prefix)
    String fatfsPath = "0:" + selectedPath;
    
    // Get file info using FatFs
    FILINFO fno;
    FRESULT res = f_stat(fatfsPath.c_str(), &fno);
    if (res != FR_OK) {
        Serial.printf("f_stat failed for %s: %d\n", fatfsPath.c_str(), res);
        return;
    }
    size_t fileSize = fno.fsize;
    Serial.printf("File size: %zu bytes (%.2f MB)\n", fileSize, fileSize / (1024.0 * 1024.0));
    
    // Open the file using FatFs
    FIL bmpFile;
    res = f_open(&bmpFile, fatfsPath.c_str(), FA_READ);
    if (res != FR_OK) {
        Serial.printf("f_open failed for %s: %d\n", fatfsPath.c_str(), res);
        return;
    }
    
    // Allocate buffer in PSRAM for the file
    uint32_t loadStart = millis();
    uint8_t* bmpData = (uint8_t*)hal_psram_malloc(fileSize);
    if (!bmpData) {
        Serial.println("Failed to allocate PSRAM buffer for BMP!");
        f_close(&bmpFile);
        return;
    }
    
    // Read entire file into PSRAM (fast bulk read)
    UINT bytesRead;
    res = f_read(&bmpFile, bmpData, fileSize, &bytesRead);
    f_close(&bmpFile);
    
    if (res != FR_OK) {
        Serial.printf("f_read failed: %d\n", res);
        hal_psram_free(bmpData);
        return;
    }
    
    uint32_t loadTime = millis() - loadStart;
    float loadTimeSec = loadTime / 1000.0f;
    if (loadTimeSec > 0) {
        Serial.printf("SD read: %lu ms (%.2f MB/s)\n", 
                      loadTime, (fileSize / 1024.0 / 1024.0) / loadTimeSec);
    } else {
        Serial.printf("SD read: %lu ms\n", loadTime);
    }
    
    if (bytesRead != fileSize) {
        Serial.printf("Warning: Only read %zu of %zu bytes\n", bytesRead, fileSize);
    }
    
    // Get BMP info
    int32_t bmpWidth, bmpHeight;
    uint16_t bmpBpp;
    BMPResult result = bmpLoader.getInfo(bmpData, fileSize, &bmpWidth, &bmpHeight, &bmpBpp);
    if (result != BMP_OK) {
        Serial.printf("BMP parse error: %s\n", bmpLoader.getErrorString(result));
        hal_psram_free(bmpData);
        return;
    }
    Serial.printf("BMP: %ldx%ld, %d bpp\n", bmpWidth, bmpHeight, bmpBpp);
    
    // Check if image dimensions match display
    bool isLandscape = (bmpWidth > bmpHeight);
    bool displayIsPortrait = (display.width() < display.height());
    if (isLandscape && displayIsPortrait) {
        Serial.println("Note: Landscape image on portrait display - will be centered/letterboxed");
    }
    Serial.println("Acceleration: LUT color mapping, PPA rotation (in display.update())");
    
    // Clear display and draw the BMP
    uint32_t drawStart = millis();
    display.clear(EL133UF1_WHITE);
    
    // Use fullscreen draw (centers the image)
    result = bmpLoader.drawFullscreen(bmpData, fileSize);
    uint32_t drawTime = millis() - drawStart;
    
    // Free the BMP data
    hal_psram_free(bmpData);
    
    if (result != BMP_OK) {
        Serial.printf("BMP draw error: %s\n", bmpLoader.getErrorString(result));
        return;
    }
    
    Serial.printf("BMP decode+draw: %lu ms\n", drawTime);
    
    // Update display
    Serial.println("Updating display (20-30s for e-ink refresh)...");
    uint32_t refreshStart = millis();
    display.update();
    uint32_t refreshTime = millis() - refreshStart;
    
    Serial.printf("Display refresh: %lu ms\n", refreshTime);
    Serial.printf("Total time: %lu ms (%.1f s)\n", 
                  millis() - totalStart, (millis() - totalStart) / 1000.0);
    Serial.println("Done!");
}

// List all BMP files on SD card using FatFs native functions
void bmpListFiles(const char* dirname = "/") {
    Serial.println("\n=== BMP Files on SD Card (FatFs) ===");
    
    if (!sdCardMounted && sd_card == nullptr) {
        Serial.println("SD card not mounted!");
        return;
    }
    
    // FatFs uses drive number prefix: "0:" for first drive
    // When mounted via esp_vfs_fat_sdmmc_mount, drive 0 is used
    String fatfsPath = "0:";
    if (strcmp(dirname, "/") != 0) {
        fatfsPath += dirname;
    }
    
    Serial.printf("Scanning: %s\n", fatfsPath.c_str());
    
    FF_DIR dir;
    FILINFO fno;
    FRESULT res;
    
    res = f_opendir(&dir, fatfsPath.c_str());
    if (res != FR_OK) {
        Serial.printf("f_opendir failed: %d\n", res);
        
        // Try without drive prefix
        Serial.println("Trying path without drive prefix...");
        res = f_opendir(&dir, dirname);
        if (res != FR_OK) {
            Serial.printf("Also failed: %d\n", res);
            return;
        }
    }
    Serial.println("f_opendir succeeded");
    
    int count = 0;
    int totalFiles = 0;
    
    while (true) {
        res = f_readdir(&dir, &fno);
        if (res != FR_OK) {
            Serial.printf("f_readdir error: %d\n", res);
            break;
        }
        if (fno.fname[0] == 0) {
            // End of directory
            break;
        }
        
        // Skip directories
        if (fno.fattrib & AM_DIR) {
            Serial.printf("  [DIR] %s\n", fno.fname);
            continue;
        }
        
        totalFiles++;
        Serial.printf("  [FILE] %s (%lu bytes)\n", fno.fname, (unsigned long)fno.fsize);
        
        // Check if it's a BMP file
        String name = String(fno.fname);
        String nameLower = name;
        nameLower.toLowerCase();
        if (nameLower.endsWith(".bmp")) {
            Serial.printf("    -> BMP [%d] %.2f MB\n", count++, fno.fsize / (1024.0 * 1024.0));
        }
    }
    
    f_closedir(&dir);
    
    Serial.printf("\nTotal files: %d, BMP files: %d\n", totalFiles, count);
    Serial.println("=====================================\n");
}

// Count PNG files in a directory using FatFs (returns count, stores paths in array if provided)
int pngCountFiles(const char* dirname, String* paths = nullptr, int maxCount = 0) {
    String fatfsPath = "0:";
    if (strcmp(dirname, "/") != 0) {
        fatfsPath += dirname;
    }
    FF_DIR dir;
    FILINFO fno;
    FRESULT res = f_opendir(&dir, fatfsPath.c_str());
    if (res != FR_OK) {
        res = f_opendir(&dir, dirname);
        if (res != FR_OK) {
            return 0;
        }
    }

    int count = 0;
    while (true) {
        res = f_readdir(&dir, &fno);
        if (res != FR_OK || fno.fname[0] == 0) break;
        if (fno.fattrib & AM_DIR) continue;

        String name = String(fno.fname);
        String lower = name;
        lower.toLowerCase();
        if (lower.endsWith(".png")) {
            if (paths && count < maxCount) {
                if (strcmp(dirname, "/") == 0) paths[count] = "/" + name;
                else paths[count] = String(dirname) + "/" + name;
            }
            count++;
        }
    }
    f_closedir(&dir);
    return count;
}

// Load keep-out map for the currently displayed image
bool loadKeepOutMapForImage() {
    if (g_lastImagePath.isEmpty()) {
        Serial.println("[KeepOut] No image path recorded");
        return false;
    }
    
    // Generate map filename (replace .png with .map)
    String mapPath = g_lastImagePath;
    int extPos = mapPath.lastIndexOf('.');
    if (extPos > 0) {
        mapPath = mapPath.substring(0, extPos) + ".map";
    } else {
        mapPath += ".map";
    }
    
    Serial.println("\n=== Checking for keep-out map ===");
    Serial.printf("  Image: %s\n", g_lastImagePath.c_str());
    Serial.printf("  Map:   %s\n", mapPath.c_str());
    
    // Check if map file exists
    String fatfsPath = "0:" + mapPath;
    FILINFO fno;
    FRESULT res = f_stat(fatfsPath.c_str(), &fno);
    if (res != FR_OK) {
        Serial.println("  Map file not found (using fallback salience detection)");
        Serial.println("=====================================\n");
        return false;
    }
    
    Serial.printf("  Map file found: %lu bytes\n", (unsigned long)fno.fsize);
    
    // Open map file
    FIL mapFile;
    res = f_open(&mapFile, fatfsPath.c_str(), FA_READ);
    if (res != FR_OK) {
        Serial.printf("  Failed to open map file: %d\n", res);
        return false;
    }
    
    // Read header (16 bytes)
    struct __attribute__((packed)) MapHeader {
        char magic[5];
        uint8_t version;
        uint16_t width;
        uint16_t height;
        uint8_t reserved[6];
    } header;
    
    UINT bytesRead = 0;
    res = f_read(&mapFile, &header, sizeof(header), &bytesRead);
    if (res != FR_OK || bytesRead != sizeof(header)) {
        Serial.println("  Failed to read map header");
        f_close(&mapFile);
        return false;
    }
    
    // Verify magic
    if (memcmp(header.magic, "KOMAP", 5) != 0) {
        Serial.println("  Invalid map file (bad magic)");
        f_close(&mapFile);
        return false;
    }
    
    // Check version
    if (header.version != 1) {
        Serial.printf("  Unsupported map version: %d\n", header.version);
        f_close(&mapFile);
        return false;
    }
    
    Serial.printf("  Map dimensions: %dx%d\n", header.width, header.height);
    
    // Calculate bitmap size
    uint32_t bitmapSize = ((uint32_t)header.width * header.height + 7) / 8;
    
    // Allocate bitmap in PSRAM
    uint8_t* bitmap = (uint8_t*)hal_psram_malloc(bitmapSize);
    if (!bitmap) {
        Serial.println("  Failed to allocate PSRAM for map bitmap");
        f_close(&mapFile);
        return false;
    }
    
    // Read bitmap data
    res = f_read(&mapFile, bitmap, bitmapSize, &bytesRead);
    f_close(&mapFile);
    
    if (res != FR_OK || bytesRead != bitmapSize) {
        Serial.printf("  Failed to read bitmap (got %u of %lu bytes)\n",
                      (unsigned)bytesRead, (unsigned long)bitmapSize);
        hal_psram_free(bitmap);
        return false;
    }
    
    // Reconstruct the full file in memory for the buffer loader
    size_t fullSize = sizeof(header) + bitmapSize;
    uint8_t* fullFile = (uint8_t*)malloc(fullSize);
    if (!fullFile) {
        Serial.println("  Failed to allocate temp buffer");
        hal_psram_free(bitmap);
        return false;
    }
    
    memcpy(fullFile, &header, sizeof(header));
    memcpy(fullFile + sizeof(header), bitmap, bitmapSize);
    hal_psram_free(bitmap);
    
    // Load map using buffer method
    bool success = textPlacement.loadKeepOutMapFromBuffer(fullFile, fullSize);
    free(fullFile);
    
    if (success) {
        Serial.println("  Text placement will avoid ML-detected objects");
    }
    Serial.println("=====================================\n");
    
    return success;
}

// Load a random PNG from SD card and display it (timed)
void pngLoadRandom(const char* dirname = "/") {
    Serial.println("\n=== Loading Random PNG ===");
    uint32_t totalStart = millis();

    if (!sdCardMounted && sd_card == nullptr) {
        Serial.println("SD card not mounted. Mounting...");
        if (!sdInitDirect(false)) {
            Serial.println("Failed to mount SD card!");
            return;
        }
    }

    int pngCount = pngCountFiles(dirname);
    if (pngCount == 0) {
        Serial.printf("No PNG files found in %s\n", dirname);
        Serial.println("Tip: Place some .png files on the SD card root");
        return;
    }
    Serial.printf("Found %d PNG files\n", pngCount);

    int maxFiles = min(pngCount, 100);
    String* paths = new String[maxFiles];
    if (!paths) {
        Serial.println("Failed to allocate path array!");
        return;
    }
    pngCountFiles(dirname, paths, maxFiles);

    srand(millis());
    int randomIndex = rand() % maxFiles;
    String selectedPath = paths[randomIndex];
    delete[] paths;
    
    // Store path for map lookup
    g_lastImagePath = selectedPath;

    Serial.printf("Selected: %s\n", selectedPath.c_str());
    String fatfsPath = "0:" + selectedPath;

    FILINFO fno;
    FRESULT res = f_stat(fatfsPath.c_str(), &fno);
    if (res != FR_OK) {
        Serial.printf("f_stat failed for %s: %d\n", fatfsPath.c_str(), res);
        return;
    }
    size_t fileSize = fno.fsize;
    Serial.printf("File size: %zu bytes (%.2f MB)\n", fileSize, fileSize / (1024.0 * 1024.0));

    FIL pngFile;
    res = f_open(&pngFile, fatfsPath.c_str(), FA_READ);
    if (res != FR_OK) {
        Serial.printf("f_open failed for %s: %d\n", fatfsPath.c_str(), res);
        return;
    }

    uint32_t loadStart = millis();
    uint8_t* pngData = (uint8_t*)hal_psram_malloc(fileSize);
    if (!pngData) {
        Serial.println("Failed to allocate PSRAM buffer for PNG!");
        f_close(&pngFile);
        return;
    }

    UINT bytesRead = 0;
    res = f_read(&pngFile, pngData, fileSize, &bytesRead);
    f_close(&pngFile);
    if (res != FR_OK) {
        Serial.printf("f_read failed: %d\n", res);
        hal_psram_free(pngData);
        return;
    }

    uint32_t loadTime = millis() - loadStart;
    float loadTimeSec = loadTime / 1000.0f;
    Serial.printf("SD read: %lu ms (%.2f MB/s)\n",
                  loadTime,
                  loadTimeSec > 0 ? (fileSize / 1024.0 / 1024.0) / loadTimeSec : 0.0f);
    if (bytesRead != fileSize) {
        Serial.printf("Warning: Only read %u of %u bytes\n", (unsigned)bytesRead, (unsigned)fileSize);
    }

    Serial.printf("PNG dithering: %s\n", pngLoader.getDithering() ? "ON" : "off");
    Serial.println("Acceleration: row-wise mapping, PPA rotation (in display.update())");

    uint32_t drawStart = millis();
    display.clear(EL133UF1_WHITE);
    PNGResult pres = pngLoader.drawFullscreen(pngData, fileSize);
    uint32_t drawTime = millis() - drawStart;

    hal_psram_free(pngData);

    if (pres != PNG_OK) {
        Serial.printf("PNG draw error: %s\n", pngLoader.getErrorString(pres));
        return;
    }
    Serial.printf("PNG decode+draw: %lu ms\n", drawTime);
    
    // Try to load keep-out map for this image (if available)
    loadKeepOutMapForImage();

    Serial.println("Updating display (20-30s for e-ink refresh)...");
    uint32_t refreshStart = millis();
    display.update();
    uint32_t refreshTime = millis() - refreshStart;
    Serial.printf("Display refresh: %lu ms\n", refreshTime);

    Serial.printf("Total time: %lu ms (%.1f s)\n",
                  millis() - totalStart, (millis() - totalStart) / 1000.0);
    Serial.println("Done!");
}

// List all PNG files on SD card using FatFs native functions
void pngListFiles(const char* dirname = "/") {
    Serial.println("\n=== PNG Files on SD Card (FatFs) ===");

    if (!sdCardMounted && sd_card == nullptr) {
        Serial.println("SD card not mounted!");
        return;
    }

    String fatfsPath = "0:";
    if (strcmp(dirname, "/") != 0) {
        fatfsPath += dirname;
    }

    Serial.printf("Scanning: %s\n", fatfsPath.c_str());

    FF_DIR dir;
    FILINFO fno;
    FRESULT res = f_opendir(&dir, fatfsPath.c_str());
    if (res != FR_OK) {
        Serial.printf("f_opendir failed: %d\n", res);
        Serial.println("Trying path without drive prefix...");
        res = f_opendir(&dir, dirname);
        if (res != FR_OK) {
            Serial.printf("Also failed: %d\n", res);
            return;
        }
    }

    int count = 0;
    int totalFiles = 0;
    while (true) {
        res = f_readdir(&dir, &fno);
        if (res != FR_OK) {
            Serial.printf("f_readdir error: %d\n", res);
            break;
        }
        if (fno.fname[0] == 0) break;
        if (fno.fattrib & AM_DIR) continue;

        totalFiles++;
        String name = String(fno.fname);
        String lower = name;
        lower.toLowerCase();
        if (lower.endsWith(".png")) {
            Serial.printf("  [PNG] %s (%.2f MB)\n", fno.fname, fno.fsize / (1024.0 * 1024.0));
            count++;
        }
    }
    f_closedir(&dir);
    Serial.printf("\nTotal files: %d, PNG files: %d\n", totalFiles, count);
    Serial.println("=====================================\n");
}

// Draw a PNG from media.txt mappings into the display buffer (no display.update), return timing info.
bool pngDrawFromMediaMappings(uint32_t* out_sd_read_ms, uint32_t* out_decode_ms) {
    if (out_sd_read_ms) *out_sd_read_ms = 0;
    if (out_decode_ms) *out_decode_ms = 0;

    if (!g_media_mappings_loaded || g_media_mappings.size() == 0) {
        return false;
    }

    // Cycle through images from media.txt sequentially
    size_t mediaCount = g_media_mappings.size();
    lastMediaIndex = (lastMediaIndex + 1) % mediaCount;
    const MediaMapping& mapping = g_media_mappings[lastMediaIndex];
    
    Serial.printf("Image %lu of %zu from media.txt: %s\n", 
                  (unsigned long)(lastMediaIndex + 1), mediaCount, mapping.imageName.c_str());
    
    // Build full path
    String imagePath = "/" + mapping.imageName;
    if (!imagePath.startsWith("/")) {
        imagePath = "/" + imagePath;
    }
    g_lastImagePath = imagePath;
    
    String fatfsPath = "0:" + imagePath;

    FILINFO fno;
    FRESULT res = f_stat(fatfsPath.c_str(), &fno);
    if (res != FR_OK) {
        Serial.printf("f_stat failed for %s: %d\n", fatfsPath.c_str(), res);
        return false;
    }
    size_t fileSize = fno.fsize;

    FIL pngFile;
    res = f_open(&pngFile, fatfsPath.c_str(), FA_READ);
    if (res != FR_OK) {
        Serial.printf("f_open failed for %s: %d\n", fatfsPath.c_str(), res);
        return false;
    }

    uint32_t loadStart = millis();
    uint8_t* pngData = (uint8_t*)hal_psram_malloc(fileSize);
    if (!pngData) {
        Serial.println("Failed to allocate PSRAM buffer for PNG!");
        f_close(&pngFile);
        return false;
    }

    UINT bytesRead = 0;
    res = f_read(&pngFile, pngData, fileSize, &bytesRead);
    f_close(&pngFile);
    uint32_t loadTime = millis() - loadStart;
    if (out_sd_read_ms) *out_sd_read_ms = loadTime;
    if (res != FR_OK) {
        Serial.printf("f_read failed: %d\n", res);
        hal_psram_free(pngData);
        return false;
    }
    if (bytesRead != fileSize) {
        Serial.printf("Warning: only read %u/%u bytes\n", (unsigned)bytesRead, (unsigned)fileSize);
    }

    uint32_t decodeStart = millis();
    display.clear(EL133UF1_WHITE);
    PNGResult pres = pngLoader.drawFullscreen(pngData, fileSize);
    uint32_t decodeTime = millis() - decodeStart;
    if (out_decode_ms) *out_decode_ms = decodeTime;
    hal_psram_free(pngData);

    if (pres != PNG_OK) {
        Serial.printf("PNG draw error: %s\n", pngLoader.getErrorString(pres));
        return false;
    }
    
    // Try to load keep-out map for this image (if available)
    bool mapLoaded = loadKeepOutMapForImage();
    
    // // Debug: visualize keep-out areas
    // if (mapLoaded) {
    //     Serial.printf("[DEBUG] Display dimensions: %dx%d\n", display.width(), display.height());
    //     textPlacement.debugDrawKeepOutAreas(&display, EL133UF1_RED);
    // }
    
    return true;
}

// Draw a random PNG into the display buffer (no display.update), return timing info.
bool pngDrawRandomToBuffer(const char* dirname, uint32_t* out_sd_read_ms, uint32_t* out_decode_ms) {
    if (out_sd_read_ms) *out_sd_read_ms = 0;
    if (out_decode_ms) *out_decode_ms = 0;

    if (!sdCardMounted && sd_card == nullptr) {
        if (!sdInitDirect(false)) {
            Serial.println("Failed to mount SD card!");
            return false;
        }
    }

    int pngCount = pngCountFiles(dirname);
    if (pngCount == 0) {
        Serial.printf("No PNG files found in %s\n", dirname);
        return false;
    }

    int maxFiles = min(pngCount, 100);
    String* paths = new String[maxFiles];
    if (!paths) return false;
    pngCountFiles(dirname, paths, maxFiles);

    // Cycle through images sequentially (stored in RTC memory)
    // This ensures we see all images before repeating, in alphabetical order
    lastImageIndex = (lastImageIndex + 1) % maxFiles;
    String selectedPath = paths[lastImageIndex];
    
    Serial.printf("Image %lu of %d (cycling alphabetically)\n", 
                  (unsigned long)(lastImageIndex + 1), maxFiles);
    
    delete[] paths;
    
    // Store path for keep-out map lookup
    g_lastImagePath = selectedPath;

    Serial.printf("Selected PNG: %s\n", selectedPath.c_str());
    String fatfsPath = "0:" + selectedPath;

    FILINFO fno;
    FRESULT res = f_stat(fatfsPath.c_str(), &fno);
    if (res != FR_OK) {
        Serial.printf("f_stat failed: %d\n", res);
        return false;
    }
    size_t fileSize = fno.fsize;

    FIL pngFile;
    res = f_open(&pngFile, fatfsPath.c_str(), FA_READ);
    if (res != FR_OK) {
        Serial.printf("f_open failed: %d\n", res);
        return false;
    }

    uint32_t loadStart = millis();
    uint8_t* pngData = (uint8_t*)hal_psram_malloc(fileSize);
    if (!pngData) {
        Serial.println("Failed to allocate PSRAM buffer for PNG!");
        f_close(&pngFile);
        return false;
    }

    UINT bytesRead = 0;
    res = f_read(&pngFile, pngData, fileSize, &bytesRead);
    f_close(&pngFile);
    uint32_t loadTime = millis() - loadStart;
    if (out_sd_read_ms) *out_sd_read_ms = loadTime;
    if (res != FR_OK) {
        Serial.printf("f_read failed: %d\n", res);
        hal_psram_free(pngData);
        return false;
    }
    if (bytesRead != fileSize) {
        Serial.printf("Warning: only read %u/%u bytes\n", (unsigned)bytesRead, (unsigned)fileSize);
    }

    uint32_t decodeStart = millis();
    display.clear(EL133UF1_WHITE);
    PNGResult pres = pngLoader.drawFullscreen(pngData, fileSize);
    uint32_t decodeTime = millis() - decodeStart;
    if (out_decode_ms) *out_decode_ms = decodeTime;
    hal_psram_free(pngData);

    if (pres != PNG_OK) {
        Serial.printf("PNG draw error: %s\n", pngLoader.getErrorString(pres));
        return false;
    }
    
    // Try to load keep-out map for this image (if available)
    bool mapLoaded = loadKeepOutMapForImage();
    
    // Debug: visualize keep-out areas
    if (mapLoaded) {
        Serial.printf("[DEBUG] Display dimensions: %dx%d\n", display.width(), display.height());
        textPlacement.debugDrawKeepOutAreas(&display, EL133UF1_RED);
    }
    
    return true;
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
    
    if (!ttf.loadFont(dancing_otf, dancing_otf_len)) {
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
    // Check wake cause IMMEDIATELY (before any initialization)
    esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
    // ESP32-P4 uses ext1 for GPIO wake (not ext0 or gpio_wakeup)
    // Check for both GPIO and EXT1 wake causes for compatibility
    bool wokeFromSwitchD = (wakeCause == ESP_SLEEP_WAKEUP_GPIO || 
                           wakeCause == ESP_SLEEP_WAKEUP_EXT1);
    
    // FAST PATH: If switch D woke us, skip ALL initialization and go straight to audio
    if (wokeFromSwitchD) {
        // Initialize serial for debug output (but minimal delay)
        Serial.begin(115200);
        delay(50);  // Brief delay for serial to stabilize
        
        // Minimal initialization - just PA enable for audio
        pinMode(PIN_CODEC_PA_EN, OUTPUT);
        digitalWrite(PIN_CODEC_PA_EN, HIGH);
        
        // GPIO wake may need extra time for hardware to stabilize
        // (timer wake doesn't have this issue)
        delay(100);  // Let hardware stabilize after GPIO wake
        
        handleSwitchDWake();
        
        // If handleSwitchDWake() returns (instead of sleeping), it means we've passed
        // the scheduled wake time and should proceed with normal cycle
        // Continue with normal initialization below
        Serial.println("SW_D wake completed, continuing with normal cycle...");
    }
    
    // Normal boot path - initialize everything
    Serial.begin(115200);

    // Bring up PA enable early (matches known-good ESP-IDF example behavior)
    pinMode(PIN_CODEC_PA_EN, OUTPUT);
    digitalWrite(PIN_CODEC_PA_EN, HIGH);

    pinMode(PIN_USER_LED, OUTPUT);
    digitalWrite(PIN_USER_LED, LOW);    
    
    // Check if we woke from deep sleep (non-switch-D wake)
    bool wokeFromSleep = (wakeCause != ESP_SLEEP_WAKEUP_UNDEFINED);
    
    if (wokeFromSleep) {
        // Quick boot after deep sleep - skip serial wait
        // Add extra delay after deep sleep to let ESP-Hosted (ESP32-C6) stabilize
        delay(500);  // Increased delay to let ESP-Hosted module stabilize after deep sleep
        Serial.println("\n=== Woke from deep sleep ===");
        Serial.printf("Boot count: %u, Wake cause: %d\n", sleepBootCount, wakeCause);
    } else {
        // Cold boot - wait for serial
        uint32_t start = millis();
        while (!Serial && (millis() - start < 3000)) {
            delay(100);
        }
        Serial.println("\n\n========================================");
        Serial.println("EL133UF1 ESP32-P4 Port Test");
        Serial.println("========================================\n");
    }
    
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
    
    // Initialize SPI (always needed - ESP32 peripherals reset after deep sleep)
    displaySPI.begin(PIN_SPI_SCK, -1, PIN_SPI_MOSI, -1);
    
    // Display initialization deferred until we know we need it (top of hour)
    // This saves time and power on non-hourly wakes when we're just doing MQTT checks
    
    // Initialize TTF renderer and BMP loader
    ttf.begin(&display);
    bmpLoader.begin(&display);
    pngLoader.begin(&display);
    pngLoader.setDithering(false);  // keep off for speed comparison baseline
    
    // Load font once (clock overlay uses it)
    if (!ttf.fontLoaded()) {
        if (!ttf.loadFont(dancing_otf, dancing_otf_len)) {
            Serial.println("WARNING: Failed to load TTF font");
        }
    }

    // Auto cycle: random PNG + time/date overlay + beep + deep sleep
    if (kAutoCycleEnabled) {
        bool shouldRun = true;
        if (!wokeFromSleep) {
            // Drain any buffered bytes (some terminals send a newline on connect)
            while (Serial.available()) {
                (void)Serial.read();
            }
            Serial.printf("\nAuto-cycle starts in %lu ms (press '!' to cancel)...\n", (unsigned long)kCycleSerialEscapeMs);
            uint32_t startWait = millis();
            while (millis() - startWait < kCycleSerialEscapeMs) {
                if (Serial.available()) {
                    char ch = (char)Serial.read();
                    if (ch == '!') {
                        shouldRun = false;
                        break;
                    }
                }
                delay(20);
            }
        }

        if (shouldRun) {
            // Run auto-cycle in a dedicated task with a larger stack than Arduino loopTask,
            // since SD init and PNG decoding are stack-heavy.
            xTaskCreatePinnedToCore(auto_cycle_task, "auto_cycle", 16384, nullptr, 5, &g_auto_cycle_task, 0);
            return; // yield loopTask; auto_cycle_task will deep-sleep the device
        } else {
            Serial.println("Auto-cycle cancelled -> staying in interactive mode.");
        }
    }

    // Keep legacy test-pattern behavior only when auto-cycle is disabled.
    if (!wokeFromSleep && !kAutoCycleEnabled) {
        // Cold boot only: draw test pattern and update display
        Serial.printf("Display buffer at: %p\n", display.getBuffer());
        
        Serial.println("\n--- Drawing Test Pattern ---");
        drawTestPattern();
        
        Serial.println("\n--- Updating Display ---");
        Serial.println("This will take 20-30 seconds...\n");
        display.update();
        
        Serial.println("\n========================================");
        Serial.println("Test complete!");
        Serial.println("========================================");
    } else {
        // After sleep: skip test pattern - display retains last image
        Serial.println("Skipping display update (e-ink retains image)");
    }
    Serial.println("\nCommands:");
    Serial.println("  Display: 'c'=color bars, 't'=TTF, 'p'=pattern");
    Serial.println("  Time:    'r'=show time, 's'=set time, 'n'=NTP sync (after WiFi)");
    Serial.println("  System:  'i'=info");
#if WIFI_ENABLED
    Serial.println("  WiFi:    'w'=connect, 'W'=set credentials, 'q'=scan, 'd'=disconnect, 'n'=NTP sync, 'x'=status");
    Serial.println("  MQTT:    'J'=set config, 'K'=status, 'H'=connect, 'j'=disconnect");
#endif
#if SDMMC_ENABLED
    Serial.println("  SD Card: 'M'=mount(4-bit), 'm'=mount(1-bit), 'L'=list, 'I'=info, 'T'=test, 'U'=unmount, 'D'=diag, 'P'=power cycle, 'O/o'=pwr on/off");
    Serial.println("  BMP:     'B'=load random BMP, 'b'=list BMP files");
#endif
    Serial.println("  Sleep:   'z'=status, '1'=10s, '2'=30s, '3'=60s, '5'=5min deep sleep");
    
    // Check internal RTC time
    time_t now = time(nullptr);
    bool timeValid = (now > 1577836800);  // After Jan 1, 2020
    
    if (wokeFromSleep && timeValid) {
        // Fast path after deep sleep - time already valid, skip WiFi/NTP
        struct tm* timeinfo = gmtime(&now);
        Serial.printf("Time: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                      timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                      timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
#if WIFI_ENABLED
        wifiLoadCredentials();  // Still load credentials for later use
#endif
        Serial.println("Ready! Enter command...\n");
        return;  // Skip WiFi auto-connect and NTP sync
    }
    
    // Cold boot path - full initialization
    Serial.println("\n--- Time Check ---");
    if (timeValid) {
        struct tm* timeinfo = gmtime(&now);
        Serial.printf("Current time: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                      timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                      timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    } else {
        Serial.println("Time not set - need NTP sync");
    }

#if WIFI_ENABLED
    // Load saved credentials from NVS - if this fails, stop and wait for configuration
    if (!wifiLoadCredentials()) {
        Serial.println("\n>>> CRITICAL: WiFi credentials not available <<<");
        Serial.println("Cannot proceed with auto-connect without WiFi credentials.");
        Serial.println("Device will wait in interactive mode for configuration.");
        Serial.println("Use command 'W' to set WiFi credentials.");
        // Stay in interactive mode - don't proceed with auto-connect
    } else {
        mqttLoadConfig();  // Load MQTT configuration
    
    // If time not valid, try to auto-connect and sync
    if (!timeValid) {
        if (strlen(wifiSSID) > 0) {
            Serial.printf("\nAuto-connecting to: %s\n", wifiSSID);
            
            WiFi.mode(WIFI_STA);
            WiFi.begin(wifiSSID, wifiPSK);
            
            Serial.print("Connecting");
            int attempts = 0;
            while (WiFi.status() != WL_CONNECTED && attempts < 30) {
                delay(500);
                Serial.print(".");
                attempts++;
            }
            
            if (WiFi.status() == WL_CONNECTED) {
                Serial.println(" OK!");
                Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
                
                // Auto NTP sync
                Serial.println("Syncing time with NTP...");
                configTime(0, 0, "pool.ntp.org", "time.google.com");
                
                // Wait for time sync
                Serial.print("Waiting for NTP");
                now = time(nullptr);
                uint32_t start = millis();
                while (now < 1577836800 && (millis() - start < 15000)) {
                    delay(500);
                    Serial.print(".");
                    now = time(nullptr);
                }
                
                if (now > 1577836800) {
                    Serial.println(" OK!");
                    struct tm* timeinfo = gmtime(&now);
                    Serial.printf("Time set: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                                  timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                                  timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
                } else {
                    Serial.println(" FAILED!");
                }
            } else {
                Serial.println(" FAILED!");
                Serial.println("Could not connect to WiFi");
            }
        } else {
            Serial.println("\nNo WiFi credentials saved.");
            Serial.println(">>> Use 'W' to set WiFi credentials, then 'n' to sync time <<<");
        }
        }
    }
    
    // If time is valid, show WiFi status
    if (timeValid) {
        Serial.println("\n--- WiFi Status ---");
        Serial.printf("MAC: %s\n", WiFi.macAddress().c_str());
        if (strlen(wifiSSID) > 0) {
            Serial.printf("Saved network: %s (use 'w' to connect)\n", wifiSSID);
        } else {
            Serial.println("No saved credentials (use 'W' to set)");
        }
    }
#else
    if (!timeValid) {
        Serial.println("\nWiFi disabled - use 's' to set time manually");
    }
#endif

    Serial.println("\nCommands:");
    Serial.println("  Display: 'c'=color bars, 't'=TTF, 'p'=pattern");
    Serial.println("  Audio:   'A'=start 440Hz tone (logs codec regs), 'a'=stop, '+'/'-'=volume, 'K'=I2C scan");
    Serial.println("  Time:    'r'=show time, 's'=set time, 'n'=NTP sync (after WiFi)");
    Serial.println("  System:  'i'=info");
#if WIFI_ENABLED
    Serial.println("  WiFi:    'w'=connect, 'W'=set creds, 'q'=scan, 'd'=disconnect, 'x'=status");
    Serial.println("  MQTT:    'J'=set config, 'K'=status, 'H'=connect, 'j'=disconnect");
#endif
#if SDMMC_ENABLED
    Serial.println("  SD:      'M'/'m'=mount 4/1-bit, 'L'=list, 'I'=info, 'B'=rand BMP, 'G'=rand PNG");
#endif
    Serial.println();

    Serial.println("\n========================================");
    Serial.println("Ready! Enter command...");
    Serial.println("========================================\n");
}

// ============================================================================
// Deep Sleep Functions (using ESP32 internal timer)
// ============================================================================
// Uses official ESP-IDF APIs:
// - esp_sleep_enable_timer_wakeup() for timer-based wake
// - esp_deep_sleep_start() to enter deep sleep

#include "esp_sleep.h"

// sleepBootCount is declared globally above

void sleepStatus() {
    Serial.println("\n=== Deep Sleep Status ===");
    Serial.printf("Boot count (RTC memory): %u\n", sleepBootCount);
    
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    Serial.print("Last wake cause: ");
    switch (cause) {
        case ESP_SLEEP_WAKEUP_UNDEFINED: Serial.println("Power on / reset"); break;
        case ESP_SLEEP_WAKEUP_TIMER:     Serial.println("Timer"); break;
        case ESP_SLEEP_WAKEUP_EXT0:      Serial.println("EXT0 GPIO"); break;
        case ESP_SLEEP_WAKEUP_EXT1:      Serial.println("EXT1 GPIO"); break;
        case ESP_SLEEP_WAKEUP_GPIO:      Serial.println("GPIO"); break;
        default: Serial.printf("Other (%d)\n", cause); break;
    }
    Serial.println("==========================\n");
}

void sleepTest(uint32_t seconds) {
    Serial.printf("\n=== Deep Sleep Test (%d seconds) ===\n", seconds);
    Serial.println("Using ESP32 internal timer for wake");
    Serial.println("\nPress any key within 3 seconds to cancel...");
    
    // Give user a chance to cancel
    uint32_t start = millis();
    while (millis() - start < 3000) {
        if (Serial.available()) {
            Serial.read();
            Serial.println("Cancelled!");
            return;
        }
        delay(100);
    }
    
    // Configure timer wake (microseconds)
    uint64_t sleep_us = (uint64_t)seconds * 1000000ULL;
    esp_err_t err = esp_sleep_enable_timer_wakeup(sleep_us);
    if (err != ESP_OK) {
        Serial.printf("ERROR: Failed to configure timer: %s\n", esp_err_to_name(err));
        return;
    }
    
    sleepBootCount++;
    Serial.printf("Boot count will be: %u\n", sleepBootCount);
    Serial.println("\nEntering deep sleep NOW...");
    Serial.flush();
    delay(100);
    
    esp_deep_sleep_start();
    
    // Never reached
}

void loop() {
    // Check if config mode is needed (set by auto_cycle_task when credentials missing)
    if (g_config_mode_needed) {
        g_config_mode_needed = false;  // Clear flag
        Serial.println("\n>>> Entering configuration mode (requested by auto-cycle task) <<<");
        enterConfigMode();
        // After config mode, credentials should be set - task will retry on next cycle
        Serial.println("Configuration complete. Auto-cycle will retry on next wake.");
        return;  // Don't process other commands this loop iteration
    }
    
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
        else if (c == 'A') {
            Serial.println("\n--- Audio Tone Start ---");
            Serial.printf("Codec I2C: SDA=%d SCL=%d addr=0x%02X\n", PIN_CODEC_I2C_SDA, PIN_CODEC_I2C_SCL, PIN_CODEC_I2C_ADDR);
            Serial.printf("I2S pins: MCLK=%d BCLK=%d LRCK=%d DOUT=%d DIN=%d PA_EN=%d\n",
                          PIN_CODEC_MCLK, PIN_CODEC_BCLK, PIN_CODEC_LRCK, PIN_CODEC_DOUT, PIN_CODEC_DIN, PIN_CODEC_PA_EN);
            audio_start(true);
        }
        else if (c == 'K') {
            Serial.println("\n--- I2C Scan (codec pins) ---");
            Serial.printf("Using SDA=%d SCL=%d, scanning I2C0...\n", PIN_CODEC_I2C_SDA, PIN_CODEC_I2C_SCL);
            g_codec_wire0.end();
            delay(5);
            if (g_codec_wire0.begin(PIN_CODEC_I2C_SDA, PIN_CODEC_I2C_SCL, 400000)) {
                i2c_scan(g_codec_wire0);
            } else {
                Serial.println("I2C0 begin failed");
            }
            Serial.println("Scanning I2C1...");
            g_codec_wire1.end();
            delay(5);
            if (g_codec_wire1.begin(PIN_CODEC_I2C_SDA, PIN_CODEC_I2C_SCL, 400000)) {
                i2c_scan(g_codec_wire1);
            } else {
                Serial.println("I2C1 begin failed");
            }
        }
        else if (c == 'a') {
            Serial.println("\n--- Audio Tone Stop ---");
            audio_stop();
        }
        else if (c == '+' || c == '=') {
            g_audio_volume_pct += 5;
            if (g_audio_volume_pct > 100) g_audio_volume_pct = 100;
            Serial.printf("Audio volume (UI): %d%% (mapped %d..%d)\n", g_audio_volume_pct, kCodecVolumeMinPct, kCodecVolumeMaxPct);
            (void)g_codec.setDacVolumePercentMapped(g_audio_volume_pct, kCodecVolumeMinPct, kCodecVolumeMaxPct);
        }
        else if (c == '-') {
            g_audio_volume_pct -= 5;
            if (g_audio_volume_pct < 0) g_audio_volume_pct = 0;
            Serial.printf("Audio volume (UI): %d%% (mapped %d..%d)\n", g_audio_volume_pct, kCodecVolumeMinPct, kCodecVolumeMaxPct);
            (void)g_codec.setDacVolumePercentMapped(g_audio_volume_pct, kCodecVolumeMinPct, kCodecVolumeMaxPct);
        }
        else if (c == 'r' || c == 'R') {
            Serial.println("\n--- Internal RTC Status ---");
            
            time_t now = time(nullptr);
            Serial.printf("Unix timestamp: %lu\n", (unsigned long)now);
            
            struct tm* timeinfo = gmtime(&now);
            Serial.printf("UTC: %04d-%02d-%02d %02d:%02d:%02d\n",
                          timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                          timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
            
            // Check if time is valid
            if (now > 1577836800) {  // After Jan 1, 2020
                Serial.println("Time appears valid");
            } else {
                Serial.println("Time not set - use 'n' to sync with NTP after WiFi connect");
            }
            
            Serial.printf("Deep sleep boot count: %u\n", sleepBootCount);
        }
        else if (c == 's' || c == 'S') {
            // Set internal RTC time from serial input
            Serial.println("\n--- Set Internal RTC Time ---");
            Serial.println("Enter Unix timestamp (seconds since 1970):");
            Serial.println("Example: 1733673600 = 2024-12-08 12:00:00 UTC");
            
            // Wait for input
            while (!Serial.available()) delay(10);
            delay(100);  // Wait for full input
            
            String input = Serial.readStringUntil('\n');
            input.trim();
            unsigned long timestamp = input.toInt();
            
            if (timestamp > 0) {
                Serial.printf("Setting time to: %lu\n", timestamp);
                
                // Set internal RTC using settimeofday
                struct timeval tv = { .tv_sec = (time_t)timestamp, .tv_usec = 0 };
                settimeofday(&tv, nullptr);
                delay(100);
                
                // Verify
                time_t now = time(nullptr);
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
        else if (c == 'd') {
            wifiDisconnect();
        }
        else if (c == 'x' || c == 'X') {
            wifiStatus();
        }
        else if (c == 'n' || c == 'N') {
            wifiNtpSync();
        }
        else if (c == 'j' || c == 'J') {
            mqttSetConfig();
        }
        else if (c == 'k' || c == 'K') {
            mqttStatus();
        }
        else if (c == 'h' || c == 'H') {
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("WiFi not connected! Connect first with 'w'");
            } else {
                mqttConnect();
            }
        }
        else if (c == 'h' && c != 'H') {
            // 'h' lowercase is disconnect (H uppercase is connect)
            mqttDisconnect();
        }
#endif
#if SDMMC_ENABLED
        else if (c == 'M') {
            sdInitDirect(false);  // 4-bit mode via ESP-IDF with internal pull-ups
        }
        else if (c == 'm') {
            sdInitDirect(true);   // 1-bit mode via ESP-IDF with internal pull-ups
        }
        else if (c == 'A') {
            sdInit(false);  // 4-bit mode via Arduino wrapper (for comparison)
        }
        else if (c == 'a') {
            sdInit(true);   // 1-bit mode via Arduino wrapper (for comparison)
        }
        else if (c == 'L') {
            sdList("/");
        }
        else if (c == 'I') {
            sdInfo();
        }
        else if (c == 'T') {
            if (!sdCardMounted) {
                Serial.println("Mounting SD card first (4-bit mode via ESP-IDF)...");
                sdInitDirect(false);
            }
            if (sdCardMounted) {
                sdReadTest();
            }
        }
        else if (c == 'U') {
            if (sd_card != nullptr) {
                sdUnmountDirect();  // Unmount ESP-IDF direct mount
            } else {
                sdUnmount();  // Unmount Arduino mount
            }
        }
        else if (c == 'D') {
            sdDiagnostics();
        }
        else if (c == 'B') {
            // Load and display a random BMP from SD card
            bmpLoadRandom("/");
        }
        else if (c == 'b') {
            // List BMP files on SD card
            bmpListFiles("/");
        }
        else if (c == 'G') {
            // Load and display a random PNG from SD card
            pngLoadRandom("/");
        }
        else if (c == 'g') {
            // List PNG files on SD card
            pngListFiles("/");
        }
        else if (c == 'P') {
            sdPowerCycle();  // Power cycle the SD card
        }
        else if (c == 'O') {
            sdPowerOn();
        }
        else if (c == 'o') {
            sdPowerOff();
        }
        else if (c == 'V') {
            Serial.println("\n=== LDO Status ===");
            esp_ldo_dump(stdout);
            Serial.println("==================\n");
        }
#endif
        // Sleep commands (always available)
        else if (c == 'z') {
            sleepStatus();
        }
        else if (c == '1') {
            sleepTest(10);   // Sleep for 10 seconds
        }
        else if (c == '2') {
            sleepTest(30);   // Sleep for 30 seconds
        }
        else if (c == '3') {
            sleepTest(60);   // Sleep for 1 minute
        }
        else if (c == '5') {
            sleepTest(300);  // Sleep for 5 minutes
        }
    }
    
    delay(100);
}

#endif // ESP32 || ARDUINO_ARCH_ESP32

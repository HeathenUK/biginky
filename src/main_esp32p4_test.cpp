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
#include <algorithm>
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "platform_hal.h"
#include "sleep_hal.h"  // ESP32-P4 sleep functions (sleep_set_time_ms, sleep_get_time_ms)
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

// ESP8266Audio for robust WAV parsing and playback
#include "AudioOutputI2S.h"
#include "AudioGeneratorWAV.h"
#include "AudioFileSource.h"

// WiFi support for ESP32-P4 (via ESP32-C6 companion chip)
#if !defined(DISABLE_WIFI) || defined(ENABLE_WIFI_TEST)
#include <WiFi.h>
#include <Preferences.h>
#define WIFI_ENABLED 1
#else
#define WIFI_ENABLED 0
#endif

// LTE/Cellular support (SIMCom A7683E on Clipper breakout)
#if !defined(DISABLE_LTE) || defined(ENABLE_LTE_TEST)
#include "SIMCom_A7683E.h"
#define LTE_ENABLED 1
#else
#define LTE_ENABLED 0
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
#ifndef PIN_SW_D_BRIDGE
#define PIN_SW_D_BRIDGE  4   // GPIO51 bridged to GPIO4 (LP GPIO) for deep sleep wake
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

// LTE/Cellular module (SIMCom A7683E on Clipper breakout) pin definitions
// Default pins for ESP32-P4 (can be overridden via build flags)
// Note: GPIO34-38 are strapping pins - avoid for control signals
// Note: GPIO39-42 are used by SD card (SD_D0-D3)
#ifndef PIN_LTE_RST
#define PIN_LTE_RST      24    // RESET pin (output, pull high to keep active)
#endif
#ifndef PIN_LTE_PWRKEY
#define PIN_LTE_PWRKEY   46    // PWRKEY pin (output, pull LOW for 50ms to power on)
#endif
#ifndef PIN_LTE_NETLIGHT
#define PIN_LTE_NETLIGHT -1    // NETLIGHT disabled (not used)
#endif
#ifndef PIN_LTE_RX
#define PIN_LTE_RX       28    // UART RX (ESP32 ← Module TX)
#endif
#ifndef PIN_LTE_TX
#define PIN_LTE_TX       29    // UART TX (ESP32 → Module RX)
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
RTC_DATA_ATTR uint32_t ntpSyncCounter = 0;  // Counter for periodic NTP resync (deprecated - use wakeCount instead)
RTC_DATA_ATTR uint32_t wakeCount = 0;  // Total wake count (both hourly and SMS check) for periodic time sync
RTC_DATA_ATTR bool usingMediaMappings = false;  // Track if we're using media.txt or scanning all PNGs
RTC_DATA_ATTR char lastAudioFile[64] = "";  // Last audio file path for instant playback on switch D wake

// Dual wake architecture state tracking
RTC_DATA_ATTR uint8_t lastWakeType = 0;  // 0 = SMS check, 1 = Hourly cycle
RTC_DATA_ATTR uint64_t lastSMSCheckTime = 0;  // Timestamp of last SMS check (Unix time)
RTC_DATA_ATTR bool lteModuleWasRegistered = false;  // Track if LTE module was registered last time

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
static bool g_in_interactive_config = false; // Flag to pause auto-cycle during config

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
    
#if LTE_ENABLED
    // Shut down LTE module before deep sleep to save power
    // Try to send AT+CPOF command to power off the module
    Serial.println("Shutting down LTE module...");
    
    // Initialize serial if needed (module might not be initialized)
    // Always ensure Serial1 is properly initialized before sending command
    Serial1.end();
    delay(50);
    Serial1.begin(115200, SERIAL_8N1, PIN_LTE_RX, PIN_LTE_TX);
    Serial1.setTimeout(1000);
    delay(100);
    
    // Flush any pending data
    while (Serial1.available()) {
        Serial1.read();
    }
    
    // First verify module is responding (if it's on)
    Serial.print("  Checking if module is on...");
    Serial1.print("AT\r");
    Serial1.flush();
    delay(200);
    
    bool was_responding = false;
    if (Serial1.available()) {
        String response = Serial1.readStringUntil('\n');
        if (response.indexOf("OK") >= 0) {
            was_responding = true;
            Serial.println(" yes");
        }
    }
    
    if (!was_responding) {
        Serial.println(" no (already off or not connected)");
        Serial.flush();
    } else {
        // Module is on - send power-off command
        Serial.print("  Sending AT+CPOF...");
        while (Serial1.available()) {
            Serial1.read();
        }
        Serial1.print("AT+CPOF\r");
        Serial1.flush();
        delay(1000);  // Give module time to shut down
        
        // Verify shutdown by testing if module still responds
        Serial.print("  Verifying shutdown...");
        while (Serial1.available()) {
            Serial1.read();
        }
        Serial1.print("AT\r");
        Serial1.flush();
        delay(300);
        
        bool still_responding = false;
        if (Serial1.available()) {
            String response = Serial1.readStringUntil('\n');
            if (response.indexOf("OK") >= 0) {
                still_responding = true;
            }
        }
        
        if (still_responding) {
            Serial.println(" FAILED - module still responding!");
            Serial.println("  WARNING: Module may not have shut down properly");
        } else {
            Serial.println(" OK - module is off (no response to AT)");
        }
    }
    
    // Module is now off - will need PWRKEY to power on at next boot
    Serial.flush();
#endif
    
    // Enable timer wake
    esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
    
    // ESP32-P4 can only wake from deep sleep using LP GPIOs (0-15) via ext1
    // Switch D is on GPIO51, which is NOT an LP GPIO
    // If GPIO51 is bridged to an LP GPIO (e.g., GPIO4), use PIN_SW_D_BRIDGE to enable wake
    
    gpio_num_t swD_pin = (gpio_num_t)PIN_SW_D;
    gpio_num_t wake_pin = (PIN_SW_D_BRIDGE >= 0) ? (gpio_num_t)PIN_SW_D_BRIDGE : swD_pin;
    
    // Configure GPIO51 as input with pull-up (normal switch reading, even if bridged)
    gpio_config_t io_conf_sw = {};
    io_conf_sw.pin_bit_mask = (1ULL << swD_pin);
    io_conf_sw.mode = GPIO_MODE_INPUT;
    io_conf_sw.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf_sw.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf_sw.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf_sw);
    
    // Check if we can wake from deep sleep (LP GPIO 0-15)
    #ifdef CONFIG_IDF_TARGET_ESP32P4
    if (wake_pin <= 15) {
        // LP GPIO - can wake from deep sleep using ext1
        if (PIN_SW_D_BRIDGE >= 0) {
            Serial.printf("Switch D (GPIO%d) bridged to GPIO%d (LP GPIO) for deep sleep wake\n", 
                         swD_pin, wake_pin);
        } else {
            Serial.printf("Configuring GPIO%d (LP GPIO) for deep sleep wake\n", wake_pin);
        }
        
        // Configure wake pin as input with pull-up (active-low switch)
        gpio_config_t io_conf = {};
        io_conf.pin_bit_mask = (1ULL << wake_pin);
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        gpio_config(&io_conf);
        
        // Use ext1 wakeup for ESP32-P4 (ext0 not supported)
        uint64_t gpio_mask = (1ULL << wake_pin);
        esp_err_t err = esp_sleep_enable_ext1_wakeup(gpio_mask, ESP_EXT1_WAKEUP_ANY_LOW);
        if (err != ESP_OK) {
            Serial.printf("WARNING: Failed to enable ext1 wake on GPIO%d: %d\n", wake_pin, err);
        } else {
            Serial.printf("GPIO%d configured for deep sleep wake (ext1, active-low)\n", wake_pin);
            if (PIN_SW_D_BRIDGE >= 0) {
                Serial.println("  (GPIO51 bridged to this pin - Switch D will trigger wake)");
            }
        }
    } else {
        // Not an LP GPIO - cannot wake from deep sleep
        Serial.printf("WARNING: GPIO%d is not an LP GPIO (0-15) and cannot wake from deep sleep on ESP32-P4\n", wake_pin);
        if (PIN_SW_D_BRIDGE < 0) {
            Serial.println("Switch D wake from deep sleep is not supported. Only timer wake is enabled.");
            Serial.println("To enable switch wake:");
            Serial.println("  1. Bridge GPIO51 to an LP GPIO (0-15, e.g., GPIO4)");
            Serial.println("  2. Define PIN_SW_D_BRIDGE in code (e.g., #define PIN_SW_D_BRIDGE 4)");
            Serial.println("  3. Or use light sleep instead (any GPIO can wake)");
        }
    }
    #else
    // Other ESP32 variants - try gpio_wakeup API
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << swD_pin);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);
    
    esp_sleep_enable_gpio_wakeup();
    gpio_wakeup_enable(swD_pin, GPIO_INTR_LOW_LEVEL);
    #endif
    
    delay(50);
    esp_deep_sleep_start();
}

/**
 * Calculate sleep duration based on wake type
 * @param isHourlyWake true if this was an hourly wake (XX:00), false for SMS check wake
 * @param fallback_seconds fallback duration if time is invalid
 * @return sleep duration in seconds
 */
static uint32_t calculateSleepDuration(bool isHourlyWake, uint32_t fallback_seconds = kCycleSleepSeconds) {
    time_t now = time(nullptr);
    if (now <= 1577836800) {  // time invalid
        Serial.printf("Time invalid, using fallback: %lu seconds\n", (unsigned long)fallback_seconds);
        return fallback_seconds;
    }

    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);
    uint32_t sleep_s = 0;
    
    if (isHourlyWake) {
        // Hourly wake: sleep until next hour (XX:00)
        // Calculate minutes until next hour
        uint32_t minutesUntilNextHour = 60 - tm_utc.tm_min;
        sleep_s = minutesUntilNextHour * 60;
        
        // If we're exactly at XX:00, sleep a full hour
        if (sleep_s == 0) {
            sleep_s = 3600;  // 1 hour
        }
        
        Serial.printf("Hourly wake: Current time %02d:%02d:%02d, sleeping until next hour: %lu seconds (%lu minutes)\n",
                     tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec, 
                     (unsigned long)sleep_s, (unsigned long)(sleep_s / 60));
    } else {
        // SMS check wake: sleep until next minute (XX:MM+1)
        uint32_t sec = (uint32_t)tm_utc.tm_sec;
        
        // Calculate seconds until next minute boundary
        sleep_s = 60 - sec;
        
        // If we're exactly at :00, sleep a full minute (60 seconds)
        if (sleep_s == 0) {
            sleep_s = 60;
        }
        
        // Avoid very short sleeps (USB/serial jitter); skip to next minute
        // If we have less than 5 seconds until next minute, sleep to the minute after that
        if (sleep_s < 5 && sleep_s > 0) {
            sleep_s += 60;
            Serial.printf("Sleep duration too short (%lu), adding 60 seconds\n", (unsigned long)(sleep_s - 60));
        }
        
        Serial.printf("SMS check wake: Current time %02d:%02d:%02d, sleeping until next minute: %lu seconds\n",
                     tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec, (unsigned long)sleep_s);
    }
    
    // Sanity clamp - if calculation is way off, use fallback
    if (sleep_s > 7200) {  // More than 2 hours seems wrong
        Serial.printf("Sleep calculation too large (%lu), using fallback\n", (unsigned long)sleep_s);
        sleep_s = fallback_seconds;
    }
    
    return sleep_s;
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
static bool ensureTimeValid(uint32_t timeout_ms = 20000) {
    time_t now = time(nullptr);
    if (now > 1577836800) {  // 2020-01-01
        return true;
    }

    // Load creds (if any) directly from NVS and try NTP.
    // (Don't call wifiLoadCredentials() here since it's defined later in this file.)
    Preferences p;
    p.begin("wifi", true);
    String ssid = p.getString("ssid", "");
    String psk = p.getString("psk", "");
    p.end();

    if (ssid.length() == 0) {
        Serial.println("Time invalid and no WiFi credentials saved; cannot NTP sync.");
        return false;
    }

    Serial.printf("Time invalid; syncing NTP via WiFi SSID '%s'...\n", ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), psk.c_str());

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start < 15000)) {
        delay(250);
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi connect failed; cannot NTP sync.");
        return false;
    }

    configTime(0, 0, "pool.ntp.org", "time.google.com");

    start = millis();
    while ((millis() - start) < timeout_ms) {
        now = time(nullptr);
        if (now > 1577836800) {
            struct tm tm_utc;
            gmtime_r(&now, &tm_utc);
            char buf[32];
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", &tm_utc);
            Serial.printf("NTP sync OK: %s\n", buf);
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            return true;
        }
        delay(250);
    }

    Serial.println("NTP sync timed out; continuing with invalid time.");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
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
 * Play a WAV file from SD card using ESP8266Audio library
 * Handles WAV parsing robustly and uses existing ES8311/I2S setup
 * Returns: true if playback successful
 */
bool playWavFile(const String& wavPath) {
    // Only log for non-beep files (beep.wav is a silent fallback)
    bool isBeep = (wavPath == "beep.wav" || wavPath.endsWith("/beep.wav"));
    if (!isBeep) {
        Serial.printf("\n=== Playing WAV: %s ===\n", wavPath.c_str());
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
    
    // Build FatFs path
    String fatfsPath = "0:";
    if (!wavPath.startsWith("/")) {
        fatfsPath += "/";
    }
    fatfsPath += wavPath;
    
    // Check if file exists
    FILINFO fno;
    FRESULT res = f_stat(fatfsPath.c_str(), &fno);
    if (res != FR_OK) {
        // Silently fail for beep.wav (expected fallback), log for other files
        if (wavPath != "beep.wav" && !wavPath.endsWith("/beep.wav")) {
            Serial.printf("  WAV file not found: %s\n", wavPath.c_str());
        }
        return false;
    }
    
    // Create custom audio source and output using our existing I2S handle
    AudioFileSourceFatFs* file = new AudioFileSourceFatFs(fatfsPath.c_str());
    if (!file->open(fatfsPath.c_str())) {
        // Silently fail for beep.wav (expected fallback), log for other files
        if (wavPath != "beep.wav" && !wavPath.endsWith("/beep.wav")) {
            Serial.printf("  Failed to open WAV file: %s\n", fatfsPath.c_str());
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
    
    // Create WAV generator
    AudioGeneratorWAV* wav = new AudioGeneratorWAV();
    
    // Only log for non-beep files (beep.wav is a silent fallback)
    if (!isBeep) {
        Serial.println("  Starting playback...");
    }
    uint32_t startTime = millis();
    
    // Begin playback - ESP8266Audio handles all WAV parsing
    if (!wav->begin(file, out)) {
        // Silently fail for beep.wav (expected fallback), log for other files
        if (!isBeep) {
            Serial.println("  Failed to start WAV playback");
        }
        file->close();
        delete file;
        delete wav;
        return false;
    }
    
    // Play until complete
    while (wav->isRunning()) {
        if (!wav->loop()) {
            wav->stop();
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
    wav->stop();
    file->close();
    delete wav;
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

/**
 * Perform hourly cycle (full display update path)
 * This is called on hourly wakes (XX:00) to update the display with new image/quote
 * Includes: time sync, SD card, image loading, display update, audio beep, SMS check
 * 
 * Note: This function contains the core cycle logic. The auto_cycle_task FreeRTOS task
 * will be updated to call this function. For now, the logic remains in auto_cycle_task
 * and will be refactored in a later phase.
 */
static void performHourlyCycle() {
    lastWakeType = 1;  // Mark as hourly wake
    g_cycle_count++;
    Serial.printf("\n=== Hourly Cycle #%lu ===\n", (unsigned long)g_cycle_count);
    
    // For Phase 1, we'll trigger the existing auto_cycle_task logic
    // In Phase 4, we'll refactor to extract the core logic here
    // The auto_cycle_task currently handles the full cycle, so we note that
    // it should be called for hourly wakes
    
    // TODO Phase 4: Extract core cycle logic from auto_cycle_task into here
}

static void auto_cycle_task(void* arg) {
    (void)arg;
    g_cycle_count++;
    Serial.printf("\n=== Cycle #%lu ===\n", (unsigned long)g_cycle_count);

    // Check if time is valid
    bool time_ok = ensureTimeValid();
    
    // Time sync logic is now handled in setup() based on wakeCount
    // Only do periodic resync here if time is invalid (fallback)
    // Note: wakeCount is incremented in setup(), so we check if we need sync here
    bool needsTimeSync = !time_ok || (wakeCount >= 20);
    bool time_synced = false;
    
    if (needsTimeSync && time_ok) {
        // Periodic resync (20+ wakes) - reset counter
        wakeCount = 0;
    }
    
    // Only attempt time sync in hourly cycle if time is invalid (fallback)
    // Normal periodic sync happens in setup() before routing
    if (!time_ok) {
        Serial.println("\n=== Time Invalid - Attempting Resync (fallback) ===");

#if LTE_ENABLED
        // Try LTE first (preferred method) - inline version to avoid forward declaration issues
        // Load APN from NVS
        Preferences lte_p;
        lte_p.begin("lte", true);
        String apn_str = lte_p.getString("apn", "");
        lte_p.end();
        
        if (apn_str.length() > 0) {
            Serial.println("Attempting time sync via LTE (preferred)...");
            
            // Initialize serial for LTE
            Serial1.end();
            delay(50);
            Serial1.begin(115200, SERIAL_8N1, PIN_LTE_RX, PIN_LTE_TX);
            Serial1.setTimeout(2000);
            delay(200);
            
            // Flush any pending data
            while (Serial1.available()) {
                Serial1.read();
            }
            
            // Quick check if module is responding
            bool module_responding = false;
            for (int i = 0; i < 2; i++) {
                Serial1.print("AT\r");
                Serial1.flush();
                delay(200);
                if (Serial1.available()) {
                    String response = Serial1.readStringUntil('\n');
                    if (response.indexOf("OK") >= 0) {
                        module_responding = true;
                        break;
                    }
                }
                delay(100);
            }
            
            if (module_responding) {
                // Disable echo
                Serial1.print("ATE0\r");
                Serial1.flush();
                delay(200);
                while (Serial1.available()) {
                    Serial1.read();
                }
                
                // Check registration status
                Serial.print("  Checking registration...");
                Serial1.print("AT+CEREG?\r");
                Serial1.flush();
                delay(200);
                
                String reg_response = "";
                uint32_t reg_start = millis();
                while ((millis() - reg_start) < 1500) {
                    if (Serial1.available()) {
                        char c = Serial1.read();
                        reg_response += c;
                        if (reg_response.indexOf("OK") >= 0 || reg_response.indexOf("ERROR") >= 0) {
                            break;
                        }
                    }
                    delay(10);
                }
                
                // Parse registration status
                int cereg_pos = reg_response.indexOf("+CEREG:");
                bool is_registered = false;
                if (cereg_pos >= 0) {
                    int comma1 = reg_response.indexOf(",", cereg_pos);
                    if (comma1 > cereg_pos) {
                        int comma2 = reg_response.indexOf(",", comma1 + 1);
                        int end = (comma2 > comma1) ? comma2 : reg_response.indexOf("\r", comma1);
                        if (end < 0) end = reg_response.indexOf("\n", comma1);
                        if (end < 0) end = reg_response.length();
                        
                        String status_str = reg_response.substring(comma1 + 1, end);
                        status_str.trim();
                        int status = status_str.toInt();
                        is_registered = (status == 1 || status == 5);  // 1=home, 5=roaming
                    }
                }
                
                if (is_registered) {
                    // Module is registered - get time
                    Serial.print(" registered, getting time...");
                    Serial1.print("AT+CCLK?\r");
                    Serial1.flush();
                    delay(200);
                    
                    String time_response = "";
                    uint32_t time_start = millis();
                    while ((millis() - time_start) < 2000) {
                        if (Serial1.available()) {
                            char c = Serial1.read();
                            time_response += c;
                            if (time_response.indexOf("OK") >= 0 || time_response.indexOf("ERROR") >= 0) {
                                break;
                            }
                        }
                        delay(10);
                    }
                    
                    // Parse and set time (same logic as lteFastBootCheck)
                    int cclk_pos = time_response.indexOf("+CCLK: \"");
                    if (cclk_pos >= 0) {
                        cclk_pos += 8;
                        int quote_end = time_response.indexOf("\"", cclk_pos);
                        if (quote_end > cclk_pos) {
                            String time_str = time_response.substring(cclk_pos, quote_end);
                            // Format: "yy/MM/dd,hh:mm:ss±zz"
                            int year = time_str.substring(0, 2).toInt();
                            int month = time_str.substring(3, 5).toInt();
                            int day = time_str.substring(6, 8).toInt();
                            int hour = time_str.substring(9, 11).toInt();
                            int minute = time_str.substring(12, 14).toInt();
                            int second = time_str.substring(15, 17).toInt();
                            
                            // Parse timezone offset (±zz) - in quarters of an hour
                            int tz_offset_quarters = 0;
                            if (time_str.length() >= 18) {
                                char tz_sign = time_str.charAt(17);
                                if (tz_sign == '+' || tz_sign == '-') {
                                    String tz_str = time_str.substring(18);
                                    tz_str.trim();
                                    int tz_val = tz_str.toInt();
                                    tz_offset_quarters = (tz_sign == '-') ? -tz_val : tz_val;
                                }
                            }
                            
                            if (year < 100) year += 2000;
                            
                            struct tm timeinfo;
                            timeinfo.tm_year = year - 1900;
                            timeinfo.tm_mon = month - 1;
                            timeinfo.tm_mday = day;
                            timeinfo.tm_hour = hour;
                            timeinfo.tm_min = minute;
                            timeinfo.tm_sec = second;
                            timeinfo.tm_isdst = 0;
                            
                            time_t unix_time = mktime(&timeinfo);
                            // Adjust for timezone offset (convert to UTC)
                            int tz_offset_seconds = tz_offset_quarters * 900;
                            unix_time -= tz_offset_seconds;
                            if (unix_time >= 0) {
                                // Set persistent RTC time (survives deep sleep) - this also sets system time
                                uint64_t time_ms = (uint64_t)unix_time * 1000ULL;
                                sleep_set_time_ms(time_ms);
                                Serial.printf(" OK! Time set\n");
                                time_ok = true;
                                time_synced = true;
                            } else {
                                Serial.println(" failed (invalid time)");
                            }
                        } else {
                            Serial.println(" failed (parse error)");
                        }
                    } else {
                        Serial.println(" failed (no time response)");
                    }
                } else {
                    Serial.println(" not registered, skipping LTE sync");
                }
            } else {
                Serial.println("LTE module not responding, falling back to WiFi/NTP...");
            }
        } else {
            Serial.println("No LTE APN configured, using WiFi/NTP...");
        }
#endif

#if WIFI_ENABLED
        // Fallback to WiFi/NTP if LTE failed or not available
        if (!time_synced) {
            // Load WiFi credentials
            Preferences p;
            p.begin("wifi", true);
            String ssid = p.getString("ssid", "");
            String psk = p.getString("psk", "");
            p.end();
            
            if (ssid.length() > 0) {
                Serial.printf("Connecting to WiFi: %s\n", ssid.c_str());
                WiFi.mode(WIFI_STA);
                WiFi.begin(ssid.c_str(), psk.c_str());
                
                // Wait up to 10 seconds for connection
                uint32_t start = millis();
                while (WiFi.status() != WL_CONNECTED && (millis() - start < 10000)) {
                    delay(250);
                }
                
                if (WiFi.status() == WL_CONNECTED) {
                    Serial.println("WiFi connected");
                    
                    // Sync time via NTP
                    configTime(0, 0, "pool.ntp.org", "time.google.com");
                    
                    Serial.print("Syncing NTP");
                    start = millis();
                    time_t now = time(nullptr);
                    while (now < 1577836800 && (millis() - start < 10000)) {
                        delay(250);
                        Serial.print(".");
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
                    } else {
                        Serial.println(" FAILED (timeout)");
                    }
                    
                    // Disconnect WiFi and put C6 into low-power mode
                    WiFi.disconnect(true);
                    WiFi.setSleep(WIFI_PS_MAX_MODEM);  // Tell C6 to enter max power save
                    WiFi.mode(WIFI_OFF);
                    Serial.println("WiFi disconnected, C6 in low-power mode");
                } else {
                    Serial.println("WiFi connection failed");
                }
            } else {
                Serial.println("No WiFi credentials saved, skipping time resync");
            }
        }
#endif

        Serial.println("==========================================\n");
        if (time_synced) {
            time_ok = true;  // Update time_ok if sync succeeded
        }
    } else if (time_ok) {
        Serial.printf("Time valid, next resync in %lu more wakes\n", (unsigned long)(20 - wakeCount));
    }

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
    time_t now = time(nullptr);
    struct tm tm_utc;
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
        // After hourly cycle, sleep until next MINUTE (not next hour!)
        // This ensures per-minute SMS checks continue between hourly cycles
        Serial.println("Hourly cycle complete, sleeping until next minute for SMS checks...");
        uint32_t sleepDuration = calculateSleepDuration(false);  // false = minute wake (SMS check)
        Serial.printf("Sleeping for %lu seconds until next minute\n", (unsigned long)sleepDuration);
        Serial.flush();
        sleepNowSeconds(sleepDuration);
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
// WiFi credentials - stored in NVS (persistent)
static char wifiSSID[33] = "";
static char wifiPSK[65] = "";
static Preferences wifiPrefs;

// Load WiFi credentials from NVS
void wifiLoadCredentials() {
    wifiPrefs.begin("wifi", true);  // Read-only
    String ssid = wifiPrefs.getString("ssid", "");
    String psk = wifiPrefs.getString("psk", "");
    wifiPrefs.end();
    
    if (ssid.length() > 0) {
        strncpy(wifiSSID, ssid.c_str(), sizeof(wifiSSID) - 1);
        strncpy(wifiPSK, psk.c_str(), sizeof(wifiPSK) - 1);
        Serial.printf("Loaded WiFi credentials for: %s\n", wifiSSID);
    } else {
        Serial.println("No saved WiFi credentials");
    }
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
    // Suspend auto-cycle task to prevent serial spam during configuration
    if (g_auto_cycle_task != nullptr) {
        vTaskSuspend(g_auto_cycle_task);
    }
    g_in_interactive_config = true;
    
    Serial.println("\n=== Set WiFi Credentials ===");
    Serial.println("Enter SSID (or 'clear' to delete saved credentials):");
    
    // Flush any pending serial input
    delay(100);
    while (Serial.available()) {
        (void)Serial.read();
        delay(10);
    }
    
    // Wait for input
    while (!Serial.available()) delay(10);
    delay(100);  // Wait for full input
    
    String ssid = Serial.readStringUntil('\n');
    ssid.trim();
    
    if (ssid.length() == 0) {
        Serial.println("Cancelled.");
        g_in_interactive_config = false;
        if (g_auto_cycle_task != nullptr) {
            vTaskResume(g_auto_cycle_task);
        }
        return;
    }
    
    if (ssid == "clear") {
        wifiClearCredentials();
        g_in_interactive_config = false;
        if (g_auto_cycle_task != nullptr) {
            vTaskResume(g_auto_cycle_task);
        }
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
    
    // Resume auto-cycle task
    g_in_interactive_config = false;
    if (g_auto_cycle_task != nullptr) {
        vTaskResume(g_auto_cycle_task);
    }
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
    
    // Debug: visualize keep-out areas
    if (mapLoaded) {
        Serial.printf("[DEBUG] Display dimensions: %dx%d\n", display.width(), display.height());
        textPlacement.debugDrawKeepOutAreas(&display, EL133UF1_RED);
    }
    
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
// LTE/Cellular Functions
// ============================================================================

#if LTE_ENABLED
// LTE credentials - stored in NVS (persistent)
static char lteAPN[65] = "";
static char lteAPNUsername[65] = "";
static char lteAPNPassword[65] = "";
static int lteAPNAuthType = 0;  // 0=none, 1=PAP, 2=CHAP
static Preferences ltePrefs;
static SIMCom_A7683E* lteModule = nullptr;

// Store most recent SMS timestamp in NVS to detect new messages
static time_t lastSMSTimestamp = 0;

// Load most recent SMS timestamp from NVS
void lteLoadLastSMSTimestamp() {
    ltePrefs.begin("lte", true);  // Read-only
    lastSMSTimestamp = ltePrefs.getULong64("last_sms_ts", 0);
    ltePrefs.end();
}

// Save most recent SMS timestamp to NVS
void lteSaveLastSMSTimestamp(time_t timestamp) {
    ltePrefs.begin("lte", false);  // Read-write
    ltePrefs.putULong64("last_sms_ts", (uint64_t)timestamp);
    ltePrefs.end();
    lastSMSTimestamp = timestamp;
}

// Load LTE APN from NVS
void lteLoadAPN() {
    ltePrefs.begin("lte", true);  // Read-only
    String apn = ltePrefs.getString("apn", "");
    String username = ltePrefs.getString("username", "");
    String password = ltePrefs.getString("password", "");
    lteAPNAuthType = ltePrefs.getInt("auth_type", 0);
    ltePrefs.end();
    
    if (apn.length() > 0) {
        strncpy(lteAPN, apn.c_str(), sizeof(lteAPN) - 1);
        strncpy(lteAPNUsername, username.c_str(), sizeof(lteAPNUsername) - 1);
        strncpy(lteAPNPassword, password.c_str(), sizeof(lteAPNPassword) - 1);
        Serial.printf("Loaded LTE APN: %s", lteAPN);
        if (username.length() > 0) {
            Serial.printf(" (auth: %s)", (lteAPNAuthType == 1) ? "PAP" : (lteAPNAuthType == 2) ? "CHAP" : "Unknown");
        }
        Serial.println();
    } else {
        Serial.println("No saved LTE APN");
    }
}

// Save LTE APN to NVS
void lteSaveAPN() {
    ltePrefs.begin("lte", false);  // Read-write
    ltePrefs.putString("apn", lteAPN);
    ltePrefs.putString("username", lteAPNUsername);
    ltePrefs.putString("password", lteAPNPassword);
    ltePrefs.putInt("auth_type", lteAPNAuthType);
    ltePrefs.end();
    Serial.println("LTE APN credentials saved to NVS");
}

// Clear LTE APN from NVS
void lteClearAPN() {
    ltePrefs.begin("lte", false);
    ltePrefs.clear();
    ltePrefs.end();
    lteAPN[0] = '\0';
    lteAPNUsername[0] = '\0';
    lteAPNPassword[0] = '\0';
    lteAPNAuthType = 0;
    Serial.println("LTE APN credentials cleared from NVS");
}

// Initialize LTE module
void lteInit(bool skip_prompt = false) {
    if (lteModule != nullptr) {
        Serial.println("LTE module already initialized");
        return;
    }
    
    if (strlen(lteAPN) == 0) {
        Serial.println("No LTE APN configured. Use 'L' to set APN.");
        return;
    }
    
    // Suspend auto-cycle task to prevent serial spam
    if (g_auto_cycle_task != nullptr) {
        vTaskSuspend(g_auto_cycle_task);
    }
    g_in_interactive_config = true;
    
    Serial.println("\n=== Initializing LTE Module ===");
    if (!skip_prompt) {
        Serial.println("Press any key to start initialization...");
        Serial.flush();
        
        // Wait for user to press a key
        while (!Serial.available()) {
            delay(10);
        }
        
        // Clear the key press
        while (Serial.available()) {
            (void)Serial.read();
        }
    }
    Serial.printf("APN: %s\n", lteAPN);
    Serial.printf("UART: TX=GPIO%d, RX=GPIO%d\n", PIN_LTE_TX, PIN_LTE_RX);
    Serial.printf("Control: RST=GPIO%d, PWRKEY=GPIO%d\n", PIN_LTE_RST, PIN_LTE_PWRKEY);
    
    // Create module instance (using Serial1 with custom pins)
    // NETLIGHT disabled (-1), PWRKEY on GPIO46
    // Pass username/password if configured
    const char* username = (strlen(lteAPNUsername) > 0) ? lteAPNUsername : nullptr;
    const char* password = (strlen(lteAPNPassword) > 0) ? lteAPNPassword : nullptr;
    lteModule = new SIMCom_A7683E(lteAPN, &Serial1, PIN_LTE_RST, PIN_LTE_NETLIGHT, PIN_LTE_PWRKEY, false, username, password, lteAPNAuthType);
    
    // Initialize with custom UART pins
    if (lteModule->begin(PIN_LTE_RX, PIN_LTE_TX)) {
        Serial.println("LTE module initialized successfully");
    } else {
        Serial.println("LTE module initialization failed!");
        delete lteModule;
        lteModule = nullptr;
    }
    Serial.println("==============================\n");
    
    // Resume auto-cycle task
    g_in_interactive_config = false;
    if (g_auto_cycle_task != nullptr) {
        vTaskResume(g_auto_cycle_task);
    }
}

// Connect to LTE network
void lteConnect(bool skip_prompt = false) {
    if (lteModule == nullptr) {
        Serial.println("LTE module not initialized. Use 'l' to initialize first.");
        return;
    }
    
    // Suspend auto-cycle task to prevent serial spam
    if (g_auto_cycle_task != nullptr) {
        vTaskSuspend(g_auto_cycle_task);
    }
    g_in_interactive_config = true;
    
    Serial.println("\n=== Connecting to LTE Network ===");
    if (!skip_prompt) {
        Serial.println("Press any key to start connection...");
        Serial.flush();
        
        // Wait for user to press a key
        while (!Serial.available()) {
            delay(10);
        }
        
        // Clear the key press
        while (Serial.available()) {
            (void)Serial.read();
        }
    }
    
    if (lteModule->connect()) {
        Serial.println("LTE connected!");
        Serial.printf("IP: %s\n", lteModule->getIPAddress().c_str());
    } else {
        Serial.println("LTE connection failed!");
    }
    Serial.println("==================================\n");
    
    // Resume auto-cycle task
    g_in_interactive_config = false;
    if (g_auto_cycle_task != nullptr) {
        vTaskResume(g_auto_cycle_task);
    }
}

// Disconnect from LTE network
void lteDisconnect() {
    if (lteModule == nullptr) {
        Serial.println("LTE module not initialized.");
        return;
    }
    
    // Suspend auto-cycle task to prevent serial spam
    if (g_auto_cycle_task != nullptr) {
        vTaskSuspend(g_auto_cycle_task);
    }
    g_in_interactive_config = true;
    
    Serial.println("\n=== Disconnecting LTE ===");
    Serial.println("Press any key to disconnect...");
    Serial.flush();
    
    // Wait for user to press a key
    while (!Serial.available()) {
        delay(10);
    }
    
    // Clear the key press
    while (Serial.available()) {
        (void)Serial.read();
    }
    lteModule->disconnect();
    Serial.println("LTE disconnected");
    Serial.println("==========================\n");
    
    // Resume auto-cycle task
    g_in_interactive_config = false;
    if (g_auto_cycle_task != nullptr) {
        vTaskResume(g_auto_cycle_task);
    }
}

// Get LTE status
void lteStatus() {
    // Suspend auto-cycle task to prevent serial spam
    if (g_auto_cycle_task != nullptr) {
        vTaskSuspend(g_auto_cycle_task);
    }
    g_in_interactive_config = true;
    
    Serial.println("\n=== LTE Status ===");
    Serial.println("Press any key to show status...");
    Serial.flush();
    
    // Wait for user to press a key
    while (!Serial.available()) {
        delay(10);
    }
    
    // Clear the key press
    while (Serial.available()) {
        (void)Serial.read();
    }
    
    if (lteModule == nullptr) {
        Serial.println("Module: Not initialized");
        if (strlen(lteAPN) > 0) {
            Serial.printf("APN: %s (use 'l' to initialize)\n", lteAPN);
        } else {
            Serial.println("APN: Not configured (use 'L' to set)");
        }
    } else {
        Serial.printf("APN: %s\n", lteAPN);
        Serial.printf("Connected: %s\n", lteModule->isConnected() ? "Yes" : "No");
        
        if (lteModule->isConnected()) {
            Serial.printf("IP Address: %s\n", lteModule->getIPAddress().c_str());
        }
        
        // Get network status
        SIMComNetworkStatus lte_status, gsm_status;
        if (lteModule->getNetworkStatus(&lte_status, &gsm_status)) {
            Serial.printf("LTE Registration: ");
            switch (lte_status) {
                case SIMCOM_NET_NOT_REGISTERED: Serial.println("Not registered"); break;
                case SIMCOM_NET_REGISTERED_HOME: Serial.println("Registered (home)"); break;
                case SIMCOM_NET_SEARCHING: Serial.println("Searching"); break;
                case SIMCOM_NET_REGISTRATION_DENIED: Serial.println("Registration denied"); break;
                case SIMCOM_NET_UNKNOWN: Serial.println("Unknown"); break;
                case SIMCOM_NET_REGISTERED_ROAMING: Serial.println("Registered (roaming)"); break;
            }
            
            Serial.printf("GSM Registration: ");
            switch (gsm_status) {
                case SIMCOM_NET_NOT_REGISTERED: Serial.println("Not registered"); break;
                case SIMCOM_NET_REGISTERED_HOME: Serial.println("Registered (home)"); break;
                case SIMCOM_NET_SEARCHING: Serial.println("Searching"); break;
                case SIMCOM_NET_REGISTRATION_DENIED: Serial.println("Registration denied"); break;
                case SIMCOM_NET_UNKNOWN: Serial.println("Unknown"); break;
                case SIMCOM_NET_REGISTERED_ROAMING: Serial.println("Registered (roaming)"); break;
            }
        }
        
        // Get signal quality
        int rssi = lteModule->getSignalQuality();
        Serial.printf("Signal Quality: %d dBm\n", rssi);
        
        // Get ICCID
        char iccid[21];
        if (lteModule->getICCID(iccid, sizeof(iccid))) {
            Serial.printf("SIM ICCID: %s\n", iccid);
        }
    }
    
    Serial.println("==================\n");
}

// Consolidated LTE function: init, connect, check SMS
void lteFullCheck() {
    // Suspend auto-cycle task to prevent serial spam
    if (g_auto_cycle_task != nullptr) {
        vTaskSuspend(g_auto_cycle_task);
    }
    g_in_interactive_config = true;
    
    Serial.println("\n=== LTE Full Check (Init + Connect + SMS) ===");
    Serial.println("Press any key to start...");
    Serial.flush();
    
    // Wait for user to press a key
    while (!Serial.available()) {
        delay(10);
    }
    
    // Clear the key press
    while (Serial.available()) {
        (void)Serial.read();
    }
    
    // Step 1: Always reset module first for clean state
    Serial.println("\n[1/3] Resetting and initializing LTE module...");
    
    // Configure pins (both are active LOW, but idle state is HIGH)
    pinMode(PIN_LTE_RST, OUTPUT);
    pinMode(PIN_LTE_PWRKEY, OUTPUT);
    digitalWrite(PIN_LTE_RST, HIGH);   // Normal state: HIGH (reset released)
    digitalWrite(PIN_LTE_PWRKEY, HIGH);  // Idle state: HIGH (not asserting)
    
    // Do proper hardware reset sequence per A7682E manual:
    // 1. RESET pin: LOW for 2.5s (hardware reset), then HIGH
    Serial.println("Performing hardware reset via RESET pin (2.5s)...");
    digitalWrite(PIN_LTE_RST, LOW);   // Assert reset (active LOW)
    delay(2500);  // 2-2.5s per spec (use 2.5s typical)
    digitalWrite(PIN_LTE_RST, HIGH);  // Release reset - return to HIGH
    delay(1000);  // Brief delay after reset release
    
    // 2. PWRKEY power cycle: LOW for 100ms (power on), then HIGH
    Serial.println("Power cycling via PWRKEY (100ms)...");
    digitalWrite(PIN_LTE_PWRKEY, LOW);   // Assert power-on (active LOW)
    delay(100);  // 50ms minimum per spec, use 100ms for safety
    digitalWrite(PIN_LTE_PWRKEY, HIGH);  // Release - return to HIGH (idle)
    delay(3000);  // Wait for module to boot after power-on
    
    // Reinitialize serial after reset
    Serial1.end();
    delay(100);
    Serial1.begin(115200, SERIAL_8N1, PIN_LTE_RX, PIN_LTE_TX);
    Serial1.setTimeout(1000);
    delay(200);
    
    // Flush any garbage data
    while (Serial1.available()) {
        Serial1.read();
    }
    
    // Verify module is ready after reset
    Serial.println("Waiting for module ready after reset...");
    uint32_t start_wait = millis();
    bool module_ready = false;
    while ((millis() - start_wait) < 15000) {  // 15 second timeout
        Serial1.print("AT\r");
        Serial1.flush();
        delay(100);
        if (Serial1.available()) {
            String response = Serial1.readStringUntil('\n');
            response.trim();
            if (response.indexOf("OK") != -1) {
                module_ready = true;
                break;
            }
        }
        delay(150);
    }
    
    if (!module_ready) {
        Serial.println("ERROR: Module not responding after reset!");
        g_in_interactive_config = false;
        if (g_auto_cycle_task != nullptr) {
            vTaskResume(g_auto_cycle_task);
        }
        return;
    }
    
    Serial.println("Module ready after reset!");
    
    // Disable echo and flush
    Serial1.print("ATE0\r");
    Serial1.flush();
    delay(500);
    while (Serial1.available()) {
        Serial1.read();
    }
    
    // Always delete and recreate module object for clean state
    if (lteModule != nullptr) {
        Serial.println("Deleting existing module object for clean state...");
        delete lteModule;
        lteModule = nullptr;
    }
    
    // Check APN configuration
    if (strlen(lteAPN) == 0) {
        Serial.println("ERROR: No APN configured. Use 'J' to set APN first.");
        g_in_interactive_config = false;
        if (g_auto_cycle_task != nullptr) {
            vTaskResume(g_auto_cycle_task);
        }
        return;
    }
    
    // Create fresh module instance
    Serial.println("Creating new module object...");
    const char* username = (strlen(lteAPNUsername) > 0) ? lteAPNUsername : nullptr;
    const char* password = (strlen(lteAPNPassword) > 0) ? lteAPNPassword : nullptr;
    lteModule = new SIMCom_A7683E(lteAPN, &Serial1, PIN_LTE_RST, PIN_LTE_NETLIGHT, PIN_LTE_PWRKEY, true, username, password, lteAPNAuthType);
    
    // Initialize - skip hardware reset since we already did it above
    Serial.println("Initializing module (skipping hardware reset - already done)...");
    if (!lteModule->begin(PIN_LTE_RX, PIN_LTE_TX, true)) {  // skip_hardware_reset=true
        Serial.println("ERROR: Module initialization failed!");
        delete lteModule;
        lteModule = nullptr;
        g_in_interactive_config = false;
        if (g_auto_cycle_task != nullptr) {
            vTaskResume(g_auto_cycle_task);
        }
        return;
    }
    
    // Give module extra time to fully stabilize after reset and initialization
    Serial.println("Waiting for module to fully stabilize...");
    delay(2000);
    
    // Step 2: Connect to network (required for reliable SMS reception)
    Serial.println("\n[2/3] Connecting to network...");
    Serial.println("Network registration is required to receive new SMS messages");
    
    // Always try to connect - isConnected() just checks flag, not actual state
    // Module might need re-registration even if flag says connected
    Serial.println("Connecting to network (may already be registered)...");
    bool connected = lteModule->connect(60000);  // 60 second timeout
    if (!connected) {
        Serial.println("ERROR: Network connection/registration failed or timed out");
        Serial.println("Cannot reliably check for new SMS without network registration");
        g_in_interactive_config = false;
        if (g_auto_cycle_task != nullptr) {
            vTaskResume(g_auto_cycle_task);
        }
        return;
    }
    
    // Step 3: Check SMS
    Serial.println("\n[3/3] Checking SMS...");
    int used_slots = 0, total_sms = 0;
    if (lteModule->getSMSCount(&used_slots, &total_sms)) {
        Serial.printf("SMS used in current storage: %d of %d\n", used_slots, total_sms);

        Serial.println("\nListing SMS messages (max 5):");
        // Always list messages from both SM and ME storages. getSMSCount() only reports
        // usage for the current storage, so gating on it may skip messages in the other
        // storage.
        lteModule->listSMS(5);
    } else {
        Serial.println("SMS: Unable to read count");
    }
    
    Serial.println("\n=== Full Check Complete ===");
    
    // Resume auto-cycle task
    g_in_interactive_config = false;
    if (g_auto_cycle_task != nullptr) {
        vTaskResume(g_auto_cycle_task);
    }
}

// SMS message structure for sorting
struct SMSMessage {
    String text;
    String sender;
    String timestamp_str;
    time_t timestamp;  // Unix timestamp for sorting
    String storage;    // Which storage location (SM, ME, etc.)
};

// Parse SMS timestamp from format "yy/MM/dd,hh:mm:ss±zz" to time_t
time_t parseSMSTimestamp(const String& timestamp_str) {
    // Format: "yy/MM/dd,hh:mm:ss±zz"
    // Example: "25/12/20,22:52:29+0" or "25/12/20,22:52:29-4"
    if (timestamp_str.length() < 17) {
        return 0;
    }
    
    // Extract date/time components (first 17 chars: "yy/MM/dd,hh:mm:ss")
    // Format is: "yy/MM/dd,hh:mm:ss±zz" where yy is 2-digit year
    int year = timestamp_str.substring(0, 2).toInt();
    int month = timestamp_str.substring(3, 5).toInt();
    int day = timestamp_str.substring(6, 8).toInt();
    int hour = timestamp_str.substring(9, 11).toInt();
    int minute = timestamp_str.substring(12, 14).toInt();
    int second = timestamp_str.substring(15, 17).toInt();
    
    // Parse timezone offset (±zz) - in quarters of an hour
    int tz_offset_quarters = 0;
    if (timestamp_str.length() >= 18) {
        char tz_sign = timestamp_str.charAt(17);
        if (tz_sign == '+' || tz_sign == '-') {
            String tz_str = timestamp_str.substring(18);
            tz_str.trim();
            int tz_val = tz_str.toInt();
            tz_offset_quarters = (tz_sign == '-') ? -tz_val : tz_val;
        }
    }
    
    // Convert 2-digit year to 4-digit
    // According to A76XX manual, year format is "yy" where:
    // - 00-99 represents 2000-2099
    // So "25" = 2025, "21" = 2021, etc.
    int full_year = year;
    if (year < 100) {
        full_year = year + 2000;
    }
    
    struct tm timeinfo;
    timeinfo.tm_year = full_year - 1900;
    timeinfo.tm_mon = month - 1;
    timeinfo.tm_mday = day;
    timeinfo.tm_hour = hour;
    timeinfo.tm_min = minute;
    timeinfo.tm_sec = second;
    timeinfo.tm_isdst = 0;
    
    time_t unix_time = mktime(&timeinfo);
    
    // Adjust for timezone offset (convert to UTC)
    // Note: SMS timestamps are typically in local time, but we want UTC for comparison
    // The offset is in quarters of an hour (1 quarter = 15 minutes = 900 seconds)
    int tz_offset_seconds = tz_offset_quarters * 900;
    unix_time -= tz_offset_seconds; // Subtract offset to get UTC
    
    if (unix_time <= 0) {
        return 0;
    }
    
    return unix_time;
}

// Collect all SMS from a storage location and add to vector
// SIMPLE: Use AT+CMGL="ALL" and parse line by line
bool collectSMSFromStorage(HardwareSerial* serial, const String& storage_name, std::vector<SMSMessage>& messages) {
    // Keep reads bounded so the SMS check cannot appear to hang forever
    auto waitWithTimeout = [&](String& buffer, uint32_t timeout_ms) {
        uint32_t start = millis();
        while ((millis() - start) < timeout_ms) {
            bool got_char = false;
            while (serial->available()) {
                buffer += char(serial->read());
                got_char = true;
            }
            if (got_char) {
                start = millis();  // reset quiet timer when data arrives
            } else {
                delay(5);
            }
        }
    };

    // Set text mode
    serial->print("AT+CMGF=1\r");
    serial->flush();
    delay(200);
    while (serial->available()) {
        serial->read();
    }

    // Switch to storage location if needed
    if (storage_name != "current") {
        // Set all three storage areas (read, write, receive) to the specified location
        String cmd = "AT+CPMS=\"" + storage_name + "\",\"" + storage_name + "\",\"" + storage_name + "\"\r";
        serial->print(cmd);
        serial->flush();

        String cpms_response;
        waitWithTimeout(cpms_response, 2000);

        // If CPMS failed or never answered, this storage location might not be available
        if (cpms_response.length() == 0 || cpms_response.indexOf("ERROR") >= 0) {
            Serial.printf("  SMS: %s storage unavailable (CPMS timeout/ERROR)\n", storage_name.c_str());
            return false;
        }
    } else {
        // For "current", just query what's currently set (don't change it)
        // This ensures we check whatever storage is currently active
        delay(200);
        while (serial->available()) {
            serial->read();
        }
    }

    // Request all messages
    serial->print("AT+CMGL=\"ALL\"\r");
    serial->flush();

    String response;
    bool found_ok = false;
    uint32_t start = millis();

    // Read response with a hard cap so we never block more than a few seconds per storage
    while ((millis() - start) < 6000) {
        bool got_any = false;
        while (serial->available()) {
            char c = serial->read();
            response += c;
            got_any = true;

            if (response.endsWith("OK\r\n") || response.endsWith("OK\r")) {
                found_ok = true;
                break;
            }
            if (response.endsWith("ERROR\r\n") || response.endsWith("ERROR\r")) {
                break;
            }
        }
        if (found_ok) break;
        if (!got_any && (millis() - start) > 1500 && response.length() == 0) {
            // No response at all after 1.5s - treat as failure to avoid hanging
            break;
        }
        delay(10);
    }

    if (response.length() == 0) {
        Serial.printf("  SMS: No response from %s storage\n", storage_name.c_str());
        return false;
    }

    if (!found_ok && response.indexOf("+CMGL:") < 0) {
        Serial.printf("  SMS: Timeout listing %s storage (partial response shown)\n", storage_name.c_str());
        Serial.println(response);
        return false;
    }
    
    // Simple parser: find each +CMGL: line, extract header and text
    int pos = 0;
    int parsed_count = 0;
    
    while ((pos = response.indexOf("+CMGL:", pos)) >= 0) {
        // Find end of header line
        int header_end = response.indexOf("\r\n", pos);
        if (header_end < 0) header_end = response.indexOf("\n", pos);
        if (header_end < 0) break;
        
        String header = response.substring(pos, header_end);
        
        // Parse header: +CMGL: <index>,"<stat>","<oa>","","<scts>"
        // Extract sender (quotes 3-4) and timestamp (quotes 7-8)
        int quote_count = 0;
        int sender_start = -1, sender_end = -1;
        int timestamp_start = -1, timestamp_end = -1;
        
        for (int i = 0; i < header.length(); i++) {
            if (header.charAt(i) == '"') {
                quote_count++;
                if (quote_count == 3) {
                    sender_start = i + 1;
                } else if (quote_count == 4) {
                    sender_end = i;
                } else if (quote_count == 7) {
                    timestamp_start = i + 1;
                } else if (quote_count == 8) {
                    timestamp_end = i;
                    break;
                }
            }
        }
        
        if (sender_start < 0 || sender_end <= sender_start || timestamp_start < 0 || timestamp_end <= timestamp_start) {
            pos = header_end + 2;
            continue;
        }
        
        String sender = header.substring(sender_start, sender_end);
        String timestamp_str = header.substring(timestamp_start, timestamp_end);
        
        // Find message text - everything after header until next +CMGL: or OK
        int text_start = header_end;
        if (text_start < response.length() && response.charAt(text_start) == '\r') text_start++;
        if (text_start < response.length() && response.charAt(text_start) == '\n') text_start++;
        
        // Find next +CMGL: or OK
        int next_cmgl = response.indexOf("\r\n+CMGL:", text_start);
        if (next_cmgl < 0) next_cmgl = response.indexOf("\n+CMGL:", text_start);
        int next_ok = response.indexOf("\r\nOK", text_start);
        if (next_ok < 0) next_ok = response.indexOf("\nOK", text_start);
        
        int text_end = response.length();
        if (next_cmgl >= 0 && (next_ok < 0 || next_cmgl < next_ok)) {
            text_end = next_cmgl;
        } else if (next_ok >= 0) {
            text_end = next_ok;
        }
        
        // Remove trailing CRLF
        while (text_end > text_start && 
               (response.charAt(text_end - 1) == '\r' || response.charAt(text_end - 1) == '\n')) {
            text_end--;
        }
        
        String text = response.substring(text_start, text_end);
        text.trim();
        
        time_t timestamp = parseSMSTimestamp(timestamp_str);
        
        // Check for concatenated messages - look for embedded timestamp
        int split_pos = -1;
        String embedded_ts = "";
        time_t embedded_time = 0;
        
        for (int i = 0; i <= text.length() - 17; i++) {
            if (text.charAt(i) >= '0' && text.charAt(i) <= '9' &&
                text.charAt(i+1) >= '0' && text.charAt(i+1) <= '9' &&
                text.charAt(i+2) == '/' &&
                text.charAt(i+3) >= '0' && text.charAt(i+3) <= '9' &&
                text.charAt(i+4) >= '0' && text.charAt(i+4) <= '9' &&
                text.charAt(i+5) == '/' &&
                text.charAt(i+6) >= '0' && text.charAt(i+6) <= '9' &&
                text.charAt(i+7) >= '0' && text.charAt(i+7) <= '9' &&
                text.charAt(i+8) == ',' &&
                i+16 < text.length() &&
                text.charAt(i+9) >= '0' && text.charAt(i+9) <= '2' &&
                text.charAt(i+10) >= '0' && text.charAt(i+10) <= '9' &&
                text.charAt(i+11) == ':' &&
                text.charAt(i+12) >= '0' && text.charAt(i+12) <= '5' &&
                text.charAt(i+13) >= '0' && text.charAt(i+13) <= '9' &&
                text.charAt(i+14) == ':' &&
                text.charAt(i+15) >= '0' && text.charAt(i+15) <= '5' &&
                text.charAt(i+16) >= '0' && text.charAt(i+16) <= '9' &&
                i+18 < text.length() &&
                (text.charAt(i+17) == '+' || text.charAt(i+17) == '-')) {
                
                int ts_end = i + 18;
                while (ts_end < text.length() && ts_end < i + 21 && 
                       text.charAt(ts_end) >= '0' && text.charAt(ts_end) <= '9') {
                    ts_end++;
                }
                String test_ts = text.substring(i, ts_end);
                time_t test_time = parseSMSTimestamp(test_ts);
                if (test_time > timestamp && test_time > 0) {
                    split_pos = i;
                    while (split_pos > 0 && text.charAt(split_pos - 1) != '\n' && 
                           text.charAt(split_pos - 1) != '\r') {
                        split_pos--;
                    }
                    if (split_pos > 0 && text.charAt(split_pos - 1) == '\r') split_pos--;
                    embedded_ts = test_ts;
                    embedded_time = test_time;
                    break;
                }
            }
        }
        
        if (split_pos >= 0) {
            // Split message
            String first = text.substring(0, split_pos);
            first.trim();
            
            int second_start = text.indexOf(embedded_ts, split_pos);
            if (second_start >= 0) {
                second_start += embedded_ts.length();
                while (second_start < text.length() && 
                       (text.charAt(second_start) == '"' || text.charAt(second_start) == ',' ||
                        text.charAt(second_start) == '\r' || text.charAt(second_start) == '\n')) {
                    second_start++;
                }
            } else {
                second_start = split_pos;
            }
            String second = text.substring(second_start);
            second.trim();
            
            if (first.length() > 0) {
                SMSMessage msg1;
                msg1.text = first;
                msg1.sender = sender;
                msg1.timestamp_str = timestamp_str;
                msg1.timestamp = timestamp;
                msg1.storage = storage_name;
                messages.push_back(msg1);
                parsed_count++;
            }
            if (second.length() > 0) {
                SMSMessage msg2;
                msg2.text = second;
                msg2.sender = sender;
                msg2.timestamp_str = embedded_ts;
                msg2.timestamp = embedded_time;
                msg2.storage = storage_name;
                messages.push_back(msg2);
                parsed_count++;
            }
        } else {
            // Single message
            SMSMessage msg;
            msg.text = text;
            msg.sender = sender;
            msg.timestamp_str = timestamp_str;
            msg.timestamp = timestamp;
            msg.storage = storage_name;
            messages.push_back(msg);
            parsed_count++;
        }
        
        // Move to next message
        if (next_cmgl >= 0) {
            pos = next_cmgl + 2; // Skip past \r\n
        } else {
            pos = response.length();
        }
    }
    
    return parsed_count > 0;
}


// Get most recent SMS from all storage locations
// CRITICAL: We check ALL possible storage locations to ensure we never miss a message
bool getMostRecentSMS(HardwareSerial* serial, SMSMessage& most_recent) {
    std::vector<SMSMessage> all_messages;
    
    Serial.println("  Gathering SMS from SM storage...");
    collectSMSFromStorage(serial, "SM", all_messages);

    Serial.println("  Gathering SMS from ME storage...");
    collectSMSFromStorage(serial, "ME", all_messages);

    Serial.println("  Gathering SMS from current storage...");
    collectSMSFromStorage(serial, "current", all_messages);
    
    if (all_messages.empty()) {
        return false;
    }
    
    // Remove duplicates (same text + sender + timestamp)
    // This can happen if messages are stored in multiple locations
    size_t before_dedup = all_messages.size();
    std::sort(all_messages.begin(), all_messages.end(),
        [](const SMSMessage& a, const SMSMessage& b) {
            if (a.text != b.text) return a.text < b.text;
            if (a.sender != b.sender) return a.sender < b.sender;
            return a.timestamp_str < b.timestamp_str;
        });
    all_messages.erase(
        std::unique(all_messages.begin(), all_messages.end(),
            [](const SMSMessage& a, const SMSMessage& b) {
                return a.text == b.text && a.sender == b.sender && a.timestamp_str == b.timestamp_str;
            }),
        all_messages.end()
    );
    if (all_messages.empty()) {
        return false;
    }
    
    // Sort by timestamp (most recent first)
    std::sort(all_messages.begin(), all_messages.end(), 
        [](const SMSMessage& a, const SMSMessage& b) {
            return a.timestamp > b.timestamp;
        });
    
    // Show the 5 most recent messages
    Serial.println("  Most recent messages:");
    for (size_t i = 0; i < all_messages.size() && i < 5; i++) {
        String display_text = all_messages[i].text;
        display_text.replace("\r\n", " ");
        display_text.replace("\n", " ");
        display_text.replace("\r", " ");
        if (display_text.length() > 60) {
            display_text = display_text.substring(0, 57) + "...";
        }
        Serial.printf("    [%zu] %s from %s at %s\n",
                     i + 1, display_text.c_str(), all_messages[i].sender.c_str(),
                     all_messages[i].timestamp_str.c_str());
    }
    
    most_recent = all_messages[0];
    return true;
}

// Fast boot-time LTE check - minimal operations, assumes module is already on and registered
// Returns true if LTE was available and time was set, false otherwise
bool lteFastBootCheck() {
    // Quick check: Is module responding?
    Serial.println("\n[LTE Fast Boot Check]");
    
    // Initialize serial port (assume module is already powered on)
    Serial1.end();
    delay(50);
    Serial1.begin(115200, SERIAL_8N1, PIN_LTE_RX, PIN_LTE_TX);
    Serial1.setTimeout(500);
    delay(100);
    
    // Flush any garbage
    while (Serial1.available()) {
        Serial1.read();
    }
    
    // Quick AT test (500ms timeout) - be tolerant of module not plugged in
    Serial.print("  Checking module...");
    bool module_responding = false;
    for (int i = 0; i < 2; i++) {
        Serial1.print("AT\r");
        Serial1.flush();
        delay(150);
        if (Serial1.available()) {
            String response = Serial1.readStringUntil('\n');
            if (response.indexOf("OK") >= 0) {
                module_responding = true;
                break;
            }
        }
        delay(50);
    }
    
    if (!module_responding) {
        Serial.println(" not responding (module may not be plugged in)");
        return false;
    }
    Serial.println(" OK");
    
    // Disable echo quickly
    Serial1.print("ATE0\r");
    Serial1.flush();
    delay(100);
    while (Serial1.available()) {
        Serial1.read();
    }
    
    // Quick registration check (without creating module object)
    Serial.print("  Checking registration...");
    Serial1.print("AT+CEREG?\r");
    Serial1.flush();
    delay(200);
    
    String reg_response = "";
    uint32_t reg_start = millis();
    while ((millis() - reg_start) < 1500) {
        if (Serial1.available()) {
            char c = Serial1.read();
            reg_response += c;
            if (reg_response.indexOf("OK") >= 0 || reg_response.indexOf("ERROR") >= 0) {
                break;
            }
        }
        delay(10);
    }
    
    // Parse registration status
    int cereg_pos = reg_response.indexOf("+CEREG:");
    if (cereg_pos < 0) {
        Serial.println(" no response");
        return false;
    }
    
    int comma1 = reg_response.indexOf(",", cereg_pos);
    if (comma1 <= cereg_pos) {
        Serial.println(" parse error");
        return false;
    }
    
    int comma2 = reg_response.indexOf(",", comma1 + 1);
    int end = (comma2 > comma1) ? comma2 : reg_response.indexOf("\r", comma1);
    if (end < 0) end = reg_response.indexOf("\n", comma1);
    if (end < 0) end = reg_response.length();
    
    String status_str = reg_response.substring(comma1 + 1, end);
    status_str.trim();
    int status = status_str.toInt();
    
    // Only proceed if registered (status 1 = home, 5 = roaming)
    if (status != 1 && status != 5) {
        Serial.printf(" not registered (status=%d)\n", status);
        return false;
    }
    Serial.printf(" registered (status=%d)\n", status);
    
    // Module is registered! Get network time and set RTC
    Serial.print("  Getting network time...");
    Serial1.print("AT+CCLK?\r");
    Serial1.flush();
    delay(200);
    
    String time_response = "";
    uint32_t time_start = millis();
    while ((millis() - time_start) < 2000) {
        if (Serial1.available()) {
            char c = Serial1.read();
            time_response += c;
            if (time_response.indexOf("OK") >= 0 || time_response.indexOf("ERROR") >= 0) {
                break;
            }
        }
        delay(10);
    }
    
    // Parse time: Format is +CCLK: "yy/MM/dd,hh:mm:ss±zz"
    int cclk_pos = time_response.indexOf("+CCLK: \"");
    if (cclk_pos < 0) {
        Serial.println(" no response");
        return false;
    }
    
    cclk_pos += 8;
    int quote_end = time_response.indexOf("\"", cclk_pos);
    if (quote_end <= cclk_pos) {
        Serial.println(" parse error");
        return false;
    }
    
    String time_str = time_response.substring(cclk_pos, quote_end);
    // Format: "yy/MM/dd,hh:mm:ss±zz"
    // Example: "25/12/20,22:52:29+0" or "25/12/20,22:52:29-4"
    // ±zz is timezone offset in quarters of an hour (e.g., +0 = UTC, +4 = UTC+1, -4 = UTC-1)
    
    int year = time_str.substring(0, 2).toInt();
    int month = time_str.substring(3, 5).toInt();
    int day = time_str.substring(6, 8).toInt();
    int hour = time_str.substring(9, 11).toInt();
    int minute = time_str.substring(12, 14).toInt();
    int second = time_str.substring(15, 17).toInt();
    
    // Parse timezone offset (±zz) - in quarters of an hour
    int tz_offset_quarters = 0;
    if (time_str.length() >= 18) {
        char tz_sign = time_str.charAt(17);
        if (tz_sign == '+' || tz_sign == '-') {
            String tz_str = time_str.substring(18);
            tz_str.trim();
            int tz_val = tz_str.toInt();
            tz_offset_quarters = (tz_sign == '-') ? -tz_val : tz_val;
        }
    }
    
    // Convert 2-digit year to 4-digit (assume 2000s)
    if (year < 100) {
        year += 2000;
    }
    
    // Build struct tm and convert to time_t
    // Note: LTE module returns time in UTC (typically +0), but we need to account for offset
    struct tm timeinfo;
    timeinfo.tm_year = year - 1900;
    timeinfo.tm_mon = month - 1;
    timeinfo.tm_mday = day;
    timeinfo.tm_hour = hour;
    timeinfo.tm_min = minute;
    timeinfo.tm_sec = second;
    timeinfo.tm_isdst = 0;
    
    // Convert to Unix timestamp (mktime interprets as local time, but we treat as UTC)
    // Since we're setting system time, we need UTC. Use timegm if available, otherwise adjust.
    time_t unix_time = mktime(&timeinfo);
    
    // Adjust for timezone offset (convert from local timezone to UTC)
    // tz_offset_quarters is in quarters of an hour, so multiply by 15 minutes = 900 seconds
    int tz_offset_seconds = tz_offset_quarters * 900;
    unix_time -= tz_offset_seconds;  // Subtract offset to get UTC
    if (unix_time < 0) {
        Serial.println(" invalid time");
        return false;
    }
    
    // Set persistent RTC time (survives deep sleep) - this also sets system time
    uint64_t time_ms = (uint64_t)unix_time * 1000ULL;
    sleep_set_time_ms(time_ms);
    
    Serial.printf("  RTC time set to %04d-%02d-%02d %02d:%02d:%02d\n", year, month, day, hour, minute, second);
    
    // Get most recent SMS from all storage locations (sorted by timestamp)
    Serial.print("  Checking SMS...");
    SMSMessage most_recent;
    if (getMostRecentSMS(&Serial1, most_recent)) {
        // Check if this is a new message
        bool was_stale = false;
        if (lastSMSTimestamp > 0 && most_recent.timestamp < lastSMSTimestamp) {
            lteSaveLastSMSTimestamp(most_recent.timestamp);
            lastSMSCheckTime = most_recent.timestamp;
            was_stale = true;
        }
        
        bool is_new = (most_recent.timestamp > lastSMSTimestamp) || was_stale || 
                      (most_recent.timestamp == lastSMSTimestamp && lastSMSCheckTime != (uint64_t)most_recent.timestamp);
        
        String display_text = most_recent.text;
        display_text.replace("\r\n", " ");
        display_text.replace("\n", " ");
        display_text.replace("\r", " ");
        if (display_text.length() > 60) {
            display_text = display_text.substring(0, 57) + "...";
        }
        
        if (is_new) {
            Serial.printf(" NEW: %s (%s): %s\n", 
                         most_recent.sender.c_str(), 
                         most_recent.timestamp_str.c_str(), 
                         display_text.c_str());
            if (!was_stale) {
                lteSaveLastSMSTimestamp(most_recent.timestamp);
            }
            lastSMSCheckTime = most_recent.timestamp;
        } else {
            Serial.printf(" %s (%s): %s\n", most_recent.sender.c_str(), most_recent.timestamp_str.c_str(), display_text.c_str());
            lastSMSCheckTime = most_recent.timestamp;
        }
    } else {
        Serial.println(" none found");
    }
    
    Serial.println("  LTE fast boot check complete - time set, skipping WiFi/NTP");
    return true;
}

// Brief registration attempt - tries to register without full reset
// Returns true if registration succeeded and time was set, false otherwise
bool lteBriefRegistrationAttempt() {
    Serial.println("\n[LTE Brief Registration Attempt]");
    
    // Check if APN is configured
    if (strlen(lteAPN) == 0) {
        Serial.println("  No APN configured - skipping");
        return false;
    }
    
    // Initialize serial port
    Serial1.end();
    delay(50);
    Serial1.begin(115200, SERIAL_8N1, PIN_LTE_RX, PIN_LTE_TX);
    Serial1.setTimeout(2000);
    delay(200);
    
    // Flush any garbage
    while (Serial1.available()) {
        Serial1.read();
    }
    
    // Quick AT test - be tolerant of module not plugged in
    Serial.print("  Checking module...");
    bool module_responding = false;
    for (int i = 0; i < 3; i++) {
        Serial1.print("AT\r");
        Serial1.flush();
        delay(200);
        if (Serial1.available()) {
            String response = Serial1.readStringUntil('\n');
            if (response.indexOf("OK") >= 0) {
                module_responding = true;
                break;
            }
        }
        delay(100);
    }
    
    if (!module_responding) {
        Serial.println(" not responding (module may not be plugged in)");
        return false;
    }
    Serial.println(" OK");
    
    // Disable echo
    Serial1.print("ATE0\r");
    Serial1.flush();
    delay(200);
    while (Serial1.available()) {
        Serial1.read();
    }
    
    // Create module object (no hardware reset - assume module is powered on)
    const char* username = (strlen(lteAPNUsername) > 0) ? lteAPNUsername : nullptr;
    const char* password = (strlen(lteAPNPassword) > 0) ? lteAPNPassword : nullptr;
    SIMCom_A7683E* tempModule = new SIMCom_A7683E(lteAPN, &Serial1, PIN_LTE_RST, PIN_LTE_NETLIGHT, PIN_LTE_PWRKEY, true, username, password, lteAPNAuthType);
    
    // Initialize without hardware reset (skip_hardware_reset=true)
    Serial.print("  Initializing (no reset)...");
    if (!tempModule->begin(PIN_LTE_RX, PIN_LTE_TX, true)) {
        Serial.println(" failed");
        delete tempModule;
        return false;
    }
    Serial.println(" OK");
    
    // Brief connection attempt (30 second timeout - shorter than full check)
    Serial.print("  Attempting registration (30s timeout)...");
    bool connected = tempModule->connect(30000);
    
    if (!connected) {
        Serial.println(" failed");
        delete tempModule;
        return false;
    }
    Serial.println(" registered!");
    
    // Get network time and set RTC (same logic as lteFastBootCheck)
    Serial.print("  Getting network time...");
    Serial1.print("AT+CCLK?\r");
    Serial1.flush();
    delay(200);
    
    String time_response = "";
    uint32_t time_start = millis();
    while ((millis() - time_start) < 2000) {
        if (Serial1.available()) {
            char c = Serial1.read();
            time_response += c;
            if (time_response.indexOf("OK") >= 0 || time_response.indexOf("ERROR") >= 0) {
                break;
            }
        }
        delay(10);
    }
    
            // Parse time: Format is +CCLK: "yy/MM/dd,hh:mm:ss±zz"
            int cclk_pos = time_response.indexOf("+CCLK: \"");
            if (cclk_pos >= 0) {
                cclk_pos += 8;
                int quote_end = time_response.indexOf("\"", cclk_pos);
                if (quote_end > cclk_pos) {
                    String time_str = time_response.substring(cclk_pos, quote_end);
                    // Format: "yy/MM/dd,hh:mm:ss±zz"
                    // Example: "25/12/20,22:52:29+0"
                    
                    int year = time_str.substring(0, 2).toInt();
                    int month = time_str.substring(3, 5).toInt();
                    int day = time_str.substring(6, 8).toInt();
                    int hour = time_str.substring(9, 11).toInt();
                    int minute = time_str.substring(12, 14).toInt();
                    int second = time_str.substring(15, 17).toInt();
                    
                    // Parse timezone offset (±zz) - in quarters of an hour
                    int tz_offset_quarters = 0;
                    if (time_str.length() >= 18) {
                        char tz_sign = time_str.charAt(17);
                        if (tz_sign == '+' || tz_sign == '-') {
                            String tz_str = time_str.substring(18);
                            tz_str.trim();
                            int tz_val = tz_str.toInt();
                            tz_offset_quarters = (tz_sign == '-') ? -tz_val : tz_val;
                        }
                    }
                    
                    // Convert 2-digit year to 4-digit (assume 2000s)
                    if (year < 100) {
                        year += 2000;
                    }
                    
                    // Build struct tm and convert to time_t
                    struct tm timeinfo;
                    timeinfo.tm_year = year - 1900;
                    timeinfo.tm_mon = month - 1;
                    timeinfo.tm_mday = day;
                    timeinfo.tm_hour = hour;
                    timeinfo.tm_min = minute;
                    timeinfo.tm_sec = second;
                    timeinfo.tm_isdst = 0;
                    
                    time_t unix_time = mktime(&timeinfo);
                    // Adjust for timezone offset (convert to UTC)
                    int tz_offset_seconds = tz_offset_quarters * 900;
                    unix_time -= tz_offset_seconds;
                    if (unix_time >= 0) {
                        // Set persistent RTC time (survives deep sleep) - this also sets system time
                        uint64_t time_ms = (uint64_t)unix_time * 1000ULL;
                        sleep_set_time_ms(time_ms);
                        
                        Serial.printf(" %04d-%02d-%02d %02d:%02d:%02d\n", year, month, day, hour, minute, second);
                        delete tempModule;
                        return true;
                    }
        }
    }
    
    Serial.println("  Registration OK but time unavailable");
    delete tempModule;
    return false;
}

/**
 * Perform SMS check only (minimal wake path)
 * This is called on minute wakes (non-hourly) to quickly check for new SMS messages
 * Key requirements:
 * - LTE module must be registered before checking SMS
 * - Minimal initialization (skip display, SD, audio)
 * - Fast return to sleep
 */
static void performSMSCheckOnly() {
    Serial.println("\n=== SMS Check Only (Minimal Wake) ===");
    lastWakeType = 0;  // Mark as SMS check wake
    // Note: wakeCount is incremented in setup() before routing, so it counts all wakes
    
    // Load APN and last SMS timestamp from NVS
    lteLoadAPN();
    lteLoadLastSMSTimestamp();
    
    // Check if LTE is configured
    if (strlen(lteAPN) == 0) {
        Serial.println("No LTE APN configured - skipping SMS check");
        return;
    }
    
    // Initialize Serial1 for LTE (minimal - module might already be on)
    Serial1.end();
    delay(50);
    Serial1.begin(115200, SERIAL_8N1, PIN_LTE_RX, PIN_LTE_TX);
    Serial1.setTimeout(2000);
    delay(200);
    
    // Flush any garbage
    while (Serial1.available()) {
        Serial1.read();
    }
    
    // Check if module is already on and responding
    Serial.print("Checking if LTE module is on...");
    bool module_ready = false;
    for (int i = 0; i < 5; i++) {
        while (Serial1.available()) {
            Serial1.read();
        }
        
        Serial1.print("ATE0\r");
        Serial1.flush();
        delay(300);
        
        String test_resp = "";
        uint32_t resp_start = millis();
        while ((millis() - resp_start) < 500) {
            if (Serial1.available()) {
                char c = Serial1.read();
                test_resp += c;
                if (test_resp.indexOf("OK") >= 0 || test_resp.indexOf("ERROR") >= 0) {
                    break;
                }
            }
            delay(10);
        }
        
        if (test_resp.indexOf("OK") >= 0) {
            module_ready = true;
            Serial.println(" yes");
            break;
        }
        
        delay(200);
    }
    
    if (!module_ready) {
        Serial.println(" no");
        Serial.println("LTE module not responding - skipping SMS check");
        // Don't try to power on - that takes too long for a quick SMS check
        // Will retry on next minute wake
        return;
    }
    
    // Flush again after echo disable
    delay(200);
    while (Serial1.available()) {
        Serial1.read();
    }
    
    // Check registration status (CRITICAL: must be registered to receive new SMS)
    Serial.print("Checking registration...");
    Serial1.print("AT+CEREG?\r");
    Serial1.flush();
    delay(300);
    
    String reg_response = "";
    uint32_t reg_start = millis();
    while ((millis() - reg_start) < 2000) {
        if (Serial1.available()) {
            char c = Serial1.read();
            reg_response += c;
            if (reg_response.indexOf("OK") >= 0 || reg_response.indexOf("ERROR") >= 0) {
                break;
            }
        }
        delay(10);
    }
    
    // Parse registration status
    bool is_registered = false;
    int cereg_pos = reg_response.indexOf("+CEREG:");
    if (cereg_pos >= 0) {
        int comma1 = reg_response.indexOf(",", cereg_pos);
        if (comma1 > cereg_pos) {
            int comma2 = reg_response.indexOf(",", comma1 + 1);
            int end = (comma2 > comma1) ? comma2 : reg_response.indexOf("\r", comma1);
            if (end < 0) end = reg_response.indexOf("\n", comma1);
            if (end < 0) end = reg_response.length();
            
            String status_str = reg_response.substring(comma1 + 1, end);
            status_str.trim();
            int status = status_str.toInt();
            is_registered = (status == 1 || status == 5);  // 1=home, 5=roaming
        }
    }
    
    if (!is_registered) {
        Serial.println(" not registered");
        Serial.println("Attempting brief registration (required for SMS)...");
        
        // Try brief registration (30 second timeout)
        bool reg_success = lteBriefRegistrationAttempt();
        
        if (!reg_success) {
            Serial.println("Registration failed - cannot check SMS");
            lteModuleWasRegistered = false;
            return;
        }
        
        Serial.println("Registration successful");
        lteModuleWasRegistered = true;
        
        // Flush UART after registration
        delay(500);
        while (Serial1.available()) {
            Serial1.read();
        }
    } else {
        Serial.println(" registered");
        lteModuleWasRegistered = true;
    }
    
    // Now check for new SMS (module is registered)
    Serial.print("Checking for new SMS...");
    SMSMessage most_recent;
    if (getMostRecentSMS(&Serial1, most_recent)) {
        // Handle stale stored timestamp: if stored timestamp is newer than any visible message,
        // it means the message was deleted or the timestamp is wrong - reset to most recent visible
        bool was_stale = false;
        if (lastSMSTimestamp > 0 && most_recent.timestamp < lastSMSTimestamp) {
            // Reset to the most recent message we can actually see
            lteSaveLastSMSTimestamp(most_recent.timestamp);
            lastSMSCheckTime = most_recent.timestamp;
            was_stale = true;
        }
        
        // Check if this is a new message
        bool is_new = false;
        if (most_recent.timestamp > lastSMSTimestamp) {
            is_new = true;
        } else if (was_stale) {
            is_new = true;
        } else if (most_recent.timestamp == lastSMSTimestamp && lastSMSCheckTime != (uint64_t)most_recent.timestamp) {
            is_new = true;
        }
        
        if (is_new) {
            if (was_stale) {
                Serial.printf(" NEW (discovered after stale timestamp reset): %s (%s): %s\n", 
                             most_recent.sender.c_str(), 
                             most_recent.timestamp_str.c_str(), 
                             most_recent.text.c_str());
            } else {
                Serial.printf(" NEW: %s (%s): %s\n", 
                             most_recent.sender.c_str(), 
                             most_recent.timestamp_str.c_str(), 
                             most_recent.text.c_str());
            }
            
            // Save the new timestamp (if not already saved by stale reset)
            if (!was_stale) {
                lteSaveLastSMSTimestamp(most_recent.timestamp);
            }
            // Always update lastSMSCheckTime to acknowledge we've seen this message
            lastSMSCheckTime = most_recent.timestamp;
            
            // TODO: Process SMS command here (Phase 3)
            // For now, just log it
            Serial.println("  (Command processing will be added in Phase 3)");
        } else {
            Serial.printf(" (no new messages, last: %s from %s)\n",
                         most_recent.timestamp_str.c_str(),
                         most_recent.sender.c_str());
            // Update lastSMSCheckTime even if not new, so we know we've checked
            lastSMSCheckTime = most_recent.timestamp;
        }
    } else {
        Serial.println(" none found or error reading SMS");
    }
    
    Serial.println("=== SMS Check Complete ===");
}

// Run comprehensive LTE tests
void lteTest() {
    if (lteModule == nullptr) {
        Serial.println("LTE module not initialized. Use 'j' to initialize first.");
        return;
    }
    
    // Suspend auto-cycle task to prevent serial spam during test
    if (g_auto_cycle_task != nullptr) {
        vTaskSuspend(g_auto_cycle_task);
    }
    g_in_interactive_config = true;
    
    Serial.println("\n=== LTE Module Test ===");
    Serial.println("Press any key to start the test...");
    Serial.flush();
    
    // Wait for user to press a key
    while (!Serial.available()) {
        delay(10);
    }
    
    // Clear the key press
    while (Serial.available()) {
        (void)Serial.read();
    }
    
    // 1. Module Information
    Serial.println("\n--- Module Information ---");
    char version[64];
    if (lteModule->getFirmwareVersion(version, sizeof(version))) {
        Serial.printf("Firmware: %s\n", version);
    } else {
        Serial.println("Firmware: Unable to read");
    }
    
    // 2. SIM Card Information
    Serial.println("\n--- SIM Card Information ---");
    char iccid[21];
    if (lteModule->getICCID(iccid, sizeof(iccid))) {
        Serial.printf("ICCID: %s\n", iccid);
    } else {
        Serial.println("ICCID: Unable to read");
    }
    
    char imsi[16];
    if (lteModule->getIMSI(imsi, sizeof(imsi))) {
        Serial.printf("IMSI: %s\n", imsi);
    } else {
        Serial.println("IMSI: Unable to read");
    }
    
    // 3. Network Information
    Serial.println("\n--- Network Information ---");
    char operator_name[32];
    if (lteModule->getNetworkOperator(operator_name, sizeof(operator_name))) {
        Serial.printf("Operator: %s\n", operator_name);
    } else {
        Serial.println("Operator: Unable to read");
    }
    
    SIMComNetworkStatus lte_status, gsm_status;
    if (lteModule->getNetworkStatus(&lte_status, &gsm_status)) {
        Serial.printf("LTE Status: ");
        switch (lte_status) {
            case SIMCOM_NET_NOT_REGISTERED: Serial.println("Not registered"); break;
            case SIMCOM_NET_REGISTERED_HOME: Serial.println("Registered (home)"); break;
            case SIMCOM_NET_SEARCHING: Serial.println("Searching"); break;
            case SIMCOM_NET_REGISTRATION_DENIED: Serial.println("Registration denied"); break;
            case SIMCOM_NET_UNKNOWN: Serial.println("Unknown"); break;
            case SIMCOM_NET_REGISTERED_ROAMING: Serial.println("Registered (roaming)"); break;
        }
    }
    
    int rssi = lteModule->getSignalQuality();
    Serial.printf("Signal Quality: %d dBm\n", rssi);
    
    // 4. Network Time
    Serial.println("\n--- Network Time ---");
    char network_time[32];
    if (lteModule->getNetworkTime(network_time, sizeof(network_time))) {
        // Parse and format: "yy/MM/dd,hh:mm:ss±zz"
        Serial.printf("Network Time: %s\n", network_time);
        
        // Try to parse and display in readable format
        // Format is: yy/MM/dd,hh:mm:ss±zz
        if (strlen(network_time) >= 17) {
            int year = atoi(network_time);
            int month = atoi(network_time + 3);
            int day = atoi(network_time + 6);
            int hour = atoi(network_time + 9);
            int minute = atoi(network_time + 12);
            int second = atoi(network_time + 15);
            
            // Convert 2-digit year to 4-digit (assume 2000s)
            if (year < 100) {
                year += 2000;
            }
            
            Serial.printf("Parsed: %04d-%02d-%02d %02d:%02d:%02d\n", 
                         year, month, day, hour, minute, second);
        }
    } else {
        Serial.println("Network Time: Not available (module may need network registration)");
    }
    
    // 5. SMS Information
    Serial.println("\n--- SMS Information ---");
    int used_slots = 0, total_sms = 0;
    if (lteModule->getSMSCount(&used_slots, &total_sms)) {
        Serial.printf("SMS used in current storage: %d of %d\n", used_slots, total_sms);

        Serial.println("\nListing SMS messages (max 5):");
        // Always list messages from both SM and ME storages. getSMSCount() only reports
        // usage for the current storage, so gating on it may skip messages in the other
        // storage.
        lteModule->listSMS(5);
    } else {
        Serial.println("SMS: Unable to read count");
    }
    
    Serial.println("\n========================\n");
    
    // Resume auto-cycle task
    g_in_interactive_config = false;
    if (g_auto_cycle_task != nullptr) {
        vTaskResume(g_auto_cycle_task);
    }
}

// Set LTE APN
void lteSetAPN() {
    // Suspend auto-cycle task to prevent serial spam during configuration
    if (g_auto_cycle_task != nullptr) {
        vTaskSuspend(g_auto_cycle_task);
    }
    g_in_interactive_config = true;
    
    Serial.println("\n=== Set LTE APN ===");
    Serial.println("Enter APN (or 'clear' to delete saved APN):");
    Serial.println("Examples: 'internet', 'data', 'broadband', carrier-specific");
    Serial.print("> ");
    Serial.flush();
    
    // Flush any pending serial input (clear buffer completely)
    delay(100);  // Give time for any pending characters
    while (Serial.available()) {
        (void)Serial.read();
        delay(10);
    }
    
    // Wait for input with timeout (2 seconds)
    uint32_t startWait = millis();
    while (!Serial.available() && (millis() - startWait < 2000)) {
        delay(50);
    }
    
    if (!Serial.available()) {
        Serial.println("\nTimeout - no input received. Cancelled.");
        g_in_interactive_config = false;
        if (g_auto_cycle_task != nullptr) {
            vTaskResume(g_auto_cycle_task);
        }
        return;
    }
    
    // Wait a bit for full line to arrive
    delay(200);
    
    // Read until newline or timeout
    String apn = "";
    startWait = millis();
    while ((millis() - startWait < 5000)) {
        if (Serial.available()) {
            char ch = Serial.read();
            if (ch == '\n' || ch == '\r') {
                break;  // End of line
            }
            if (ch >= 32 && ch < 127) {  // Printable ASCII only
                apn += ch;
            }
        }
        delay(10);
    }
    
    apn.trim();
    
    // Flush any remaining characters (including newline)
    while (Serial.available()) {
        (void)Serial.read();
    }
    
    if (apn.length() == 0) {
        Serial.println("Cancelled.");
        g_in_interactive_config = false;
        if (g_auto_cycle_task != nullptr) {
            vTaskResume(g_auto_cycle_task);
        }
        return;
    }
    
    if (apn == "clear") {
        lteClearAPN();
        g_in_interactive_config = false;
        if (g_auto_cycle_task != nullptr) {
            vTaskResume(g_auto_cycle_task);
        }
        return;
    }
    
    strncpy(lteAPN, apn.c_str(), sizeof(lteAPN) - 1);
    Serial.printf("APN set to: %s\n", lteAPN);
    
    // Ask for username (optional)
    Serial.println("\nEnter APN username (or empty if not needed):");
    Serial.print("> ");
    Serial.flush();
    delay(100);
    while (Serial.available()) {
        (void)Serial.read();
        delay(10);
    }
    
    startWait = millis();
    while (!Serial.available() && (millis() - startWait < 30000)) {
        delay(50);
    }
    
    String username = "";
    if (Serial.available()) {
        delay(200);
        uint32_t usernameWait = millis();
        while ((millis() - usernameWait < 5000)) {
            if (Serial.available()) {
                char ch = Serial.read();
                if (ch == '\n' || ch == '\r') {
                    break;
                }
                if (ch >= 32 && ch < 127) {
                    username += ch;
                }
            }
            delay(10);
        }
        username.trim();
    }
    while (Serial.available()) {
        (void)Serial.read();
    }
    
    if (username.length() > 0) {
        strncpy(lteAPNUsername, username.c_str(), sizeof(lteAPNUsername) - 1);
        
        // Ask for password
        Serial.println("\nEnter APN password:");
        Serial.print("> ");
        Serial.flush();
        delay(100);
        while (Serial.available()) {
            (void)Serial.read();
            delay(10);
        }
        
        startWait = millis();
        while (!Serial.available() && (millis() - startWait < 2000)) {
            delay(50);
        }
        
        String password = "";
        if (Serial.available()) {
            delay(200);
            uint32_t passwordWait = millis();
            while ((millis() - passwordWait < 5000)) {
                if (Serial.available()) {
                    char ch = Serial.read();
                    if (ch == '\n' || ch == '\r') {
                        break;
                    }
                    if (ch >= 32 && ch < 127) {
                        password += ch;
                    }
                }
                delay(10);
            }
            password.trim();
        }
        while (Serial.available()) {
            (void)Serial.read();
        }
        
        strncpy(lteAPNPassword, password.c_str(), sizeof(lteAPNPassword) - 1);
        
        // Ask for auth type
        Serial.println("\nEnter authentication type:");
        Serial.println("  0 = None");
        Serial.println("  1 = PAP (Password Authentication Protocol)");
        Serial.println("  2 = CHAP (Challenge Handshake Authentication Protocol)");
        Serial.print("> ");
        Serial.flush();
        delay(100);
        while (Serial.available()) {
            (void)Serial.read();
            delay(10);
        }
        
        startWait = millis();
        while (!Serial.available() && (millis() - startWait < 2000)) {
            delay(50);
        }
        
        if (Serial.available()) {
            delay(200);
            String auth_str = Serial.readStringUntil('\n');
            auth_str.trim();
            int auth = auth_str.toInt();
            if (auth >= 0 && auth <= 2) {
                lteAPNAuthType = auth;
            } else {
                Serial.println("Invalid auth type, defaulting to PAP (1)");
                lteAPNAuthType = 1;
            }
        }
        while (Serial.available()) {
            (void)Serial.read();
        }
        
        Serial.printf("Username: %s, Auth: %s\n", lteAPNUsername, 
                     (lteAPNAuthType == 1) ? "PAP" : (lteAPNAuthType == 2) ? "CHAP" : "None");
    } else {
        // No username, clear password and auth
        lteAPNUsername[0] = '\0';
        lteAPNPassword[0] = '\0';
        lteAPNAuthType = 0;
        Serial.println("No username - authentication disabled");
    }
    
    // Save to NVS for persistence across reboots
    lteSaveAPN();
    
    Serial.println("===================\n");
    
    // Offer to test UART connection
    Serial.println("Test UART connection now? (y/n)");
    Serial.print("> ");
    Serial.flush();
    delay(100);
    while (Serial.available()) {
        (void)Serial.read();
        delay(10);
    }
    
    startWait = millis();
    while (!Serial.available() && (millis() - startWait < 10000)) {
        delay(50);
    }
    
    bool test_uart = false;
    if (Serial.available()) {
        delay(200);
        String test = Serial.readStringUntil('\n');
        test.trim();
        test.toLowerCase();
        test_uart = (test == "y" || test == "yes");
    }
    while (Serial.available()) {
        (void)Serial.read();
    }
    
    if (test_uart) {
        bool test_passed = false;
        bool swapped = false;
        
        // Configure control pins first
        pinMode(PIN_LTE_RST, OUTPUT);
        digitalWrite(PIN_LTE_RST, HIGH);  // Keep reset high (active)
        
        if (PIN_LTE_PWRKEY >= 0) {
            pinMode(PIN_LTE_PWRKEY, OUTPUT);
            digitalWrite(PIN_LTE_PWRKEY, HIGH);  // PWRKEY idle state is HIGH
        }
        
        // Try up to 2 times: once with normal pins, once with swapped
        for (int attempt = 0; attempt < 2 && !test_passed; attempt++) {
            if (attempt == 1 && !swapped) {
                Serial.println("\n=== Retrying with swapped TX/RX ===");
                Serial.println("If you swapped the wires, press any key to continue...");
                Serial.flush();
                
                // Wait for any key press
                while (!Serial.available()) {
                    delay(50);
                }
                while (Serial.available()) {
                    (void)Serial.read();  // Clear the key press
                }
                swapped = true;
            } else if (attempt == 0) {
                Serial.println("\n=== Testing UART Connection ===");
                
                // Power-on sequence before first test (matches library sequence)
                // Ensure RESET is HIGH first (keep module active)
                digitalWrite(PIN_LTE_RST, HIGH);
                
                if (PIN_LTE_PWRKEY >= 0) {
                    Serial.println("Powering on module via PWRKEY...");
                    // Pull PWRKEY LOW for ≥50ms to power on
                    digitalWrite(PIN_LTE_PWRKEY, LOW);
                    delay(100);  // 100ms (minimum is 50ms)
                    digitalWrite(PIN_LTE_PWRKEY, HIGH);
                    Serial.println("Waiting for module to boot (2 seconds)...");
                    delay(2000);
                } else {
                    // Fallback: Use RESET method (matches MicroPython driver)
                    Serial.println("Resetting module (RESET method)...");
                    digitalWrite(PIN_LTE_RST, LOW);
                    delay(1000);  // Match MicroPython: 1.0 second
                    digitalWrite(PIN_LTE_RST, HIGH);
                    Serial.println("Waiting for module to boot (2 seconds)...");
                    delay(2000);
                }
            }
            
            int test_tx = swapped ? PIN_LTE_RX : PIN_LTE_TX;
            int test_rx = swapped ? PIN_LTE_TX : PIN_LTE_RX;
            
            Serial.printf("UART: TX=GPIO%d, RX=GPIO%d\n", test_tx, test_rx);
            
            // Configure Serial1 with pins (swapped on second attempt)
            Serial1.end();
            delay(100);
            Serial1.begin(115200, SERIAL_8N1, test_rx, test_tx);
            Serial1.setTimeout(2000);
            delay(500);
            
            // Flush any pending data
            while (Serial1.available()) {
                (void)Serial1.read();
            }
            
            // Send test AT command
            Serial.println("Sending AT command...");
            Serial1.print("AT\r");
            Serial1.flush();
            
            // Wait for response
            uint32_t timeout = millis() + 3000;
            String response = "";
            bool got_response = false;
            
            while (millis() < timeout) {
                if (Serial1.available()) {
                    char ch = Serial1.read();
                    if (ch >= 32 || ch == '\r' || ch == '\n') {
                        response += ch;
                        if (response.indexOf("OK") >= 0 || response.indexOf("ERROR") >= 0) {
                            got_response = true;
                            break;
                        }
                    }
                }
                delay(10);
            }
            
            if (got_response) {
                Serial.println("✓ UART connection OK!");
                Serial.print("Response: ");
                Serial.println(response);
                if (swapped) {
                    Serial.println("NOTE: TX and RX are swapped! Update pin definitions if needed.");
                }
                test_passed = true;
            } else {
                if (attempt == 0) {
                    Serial.println("✗ No response from module");
                    Serial.println("Possible issues:");
                    Serial.println("  - TX and RX may be swapped");
                    Serial.println("  - No power to module");
                    Serial.println("  - Module not booted (wait a few seconds)");
                    if (response.length() > 0) {
                        Serial.print("  Partial response: ");
                        Serial.println(response);
                    }
                } else {
                    Serial.println("✗ Still no response after swap");
                    Serial.println("Check power and module boot status.");
                }
            }
        }
        
        Serial.println("===============================\n");
    } else {
        Serial.println("Skipping UART test.");
    }
    
    Serial.println("Use 'j' to initialize module with this APN.");
    
    // Resume auto-cycle task
    g_in_interactive_config = false;
    if (g_auto_cycle_task != nullptr) {
        vTaskResume(g_auto_cycle_task);
    }
}

#endif // LTE_ENABLED

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

#if LTE_ENABLED
    // POWER ON LTE MODULE FIRST - Module was shut down before deep sleep
    // Do this as early as possible so it can boot while we do other initialization
    Serial.println("\n[LTE Power-On]");
    pinMode(PIN_LTE_PWRKEY, OUTPUT);
    digitalWrite(PIN_LTE_PWRKEY, HIGH);  // Ensure idle state (HIGH)
    delay(50);
    
    // Initialize serial port first to check if module is already on
    Serial1.end();
    delay(50);
    Serial1.begin(115200, SERIAL_8N1, PIN_LTE_RX, PIN_LTE_TX);
    Serial1.setTimeout(1000);
    delay(200);
    
    // Check if module is already powered on and responding
    // This prevents accidentally powering off if module is already on
    // IMPORTANT: After AT+CPOF, module should be OFF, but if we're flashing firmware
    // without sleep, module might still be ON. We need to detect this correctly.
    Serial.print("  Checking if module is already on...");
    bool already_on = false;
    
    // Give serial port a moment to stabilize after initialization
    delay(200);
    
    // Try multiple times with increasing delays - module might be slow to respond
    for (int i = 0; i < 8; i++) {
        // Flush any garbage
        while (Serial1.available()) {
            Serial1.read();
        }
        
        // Send AT command
        Serial1.print("AT\r");
        Serial1.flush();
        
        // Wait for response (longer delay for first attempts)
        delay(400 + (i * 50));  // 400ms, 450ms, 500ms, etc.
        
        // Read all available response data
        String response = "";
        uint32_t resp_start = millis();
        while ((millis() - resp_start) < 600) {
            if (Serial1.available()) {
                char c = Serial1.read();
                response += c;
                // Check if we got a complete response
                if (response.indexOf("OK") >= 0 || response.indexOf("ERROR") >= 0) {
                    break;
                }
            }
            delay(10);
        }
        
        // Check for OK (even if mixed with other text)
        if (response.indexOf("OK") >= 0) {
            already_on = true;
            Serial.printf(" yes (already powered on, detected on attempt %d)\n", i + 1);
            break;
        }
        
        // Debug: show what we got on last attempt
        if (i == 7 && response.length() > 0) {
            Serial.printf("  (last response: [%s])\n", response.c_str());
        }
        
        delay(100);
    }
    
    if (!already_on) {
        Serial.println(" no (powering on via PWRKEY)");
        
        // Per A7682E Hardware Design Manual:
        // - After AT+CPOF, module is fully powered off
        // - UART interface needs ~8 seconds to be ready after power-on
        // - PWRKEY must remain HIGH after AT+CPOF (already done above)
        // - Power-on: Pull PWRKEY LOW for minimum 50ms, then return to HIGH
        // - Power-off: Pull PWRKEY LOW for 2.5 seconds (we use 100ms, so safe)
        // NOTE: If module is already on, a 100ms pulse should be harmless (power-off requires 2.5s)
        
        digitalWrite(PIN_LTE_PWRKEY, LOW);   // Assert power-on (active LOW)
        delay(100);  // 50ms minimum per spec, use 100ms for safety (well under 2.5s power-off threshold)
        digitalWrite(PIN_LTE_PWRKEY, HIGH);  // Release - MUST return to HIGH (idle state)
    } else {
        Serial.println("  Module already on - skipping PWRKEY sequence");
    }
    
    // Serial port already initialized above for the "already on" check
    // If module was already on, it's ready - skip the boot wait
    // If module was just powered on, wait for it to boot
    bool module_ready = already_on;
    
    if (!already_on) {
        // Wait for module to be ready (UART interface needs ~8 seconds per manual)
        // After AT+CPOF shutdown, module may need up to 12-15 seconds to fully boot
        // Use retries to detect readiness early if possible, but allow up to 15 seconds
        Serial.print("  Waiting for module to boot");
        for (int attempt = 0; attempt < 75; attempt++) {  // Up to 15 seconds (75 * 200ms)
            delay(200);
            if (attempt % 5 == 0) Serial.print(".");  // Print dot every second
            
            // Flush any garbage
            while (Serial1.available()) {
                Serial1.read();
            }
            
            // Try AT command to see if module responds
            Serial1.print("AT\r");
            Serial1.flush();
            delay(200);  // Give more time for response
            
            // Read all available data (module may send multiple lines)
            String response = "";
            uint32_t read_start = millis();
            while ((millis() - read_start) < 300) {
                if (Serial1.available()) {
                    char c = Serial1.read();
                    response += c;
                    // Check if we got a complete response
                    if (response.indexOf("OK") >= 0 || response.indexOf("ERROR") >= 0) {
                        break;
                    }
                }
                delay(10);
            }
            
            if (response.indexOf("OK") >= 0) {
                module_ready = true;
                Serial.printf(" ready! (after ~%lu ms)\n", 
                             (unsigned long)((attempt + 1) * 200));
                break;
            }
        }
        
        if (!module_ready) {
            Serial.println(" timeout after 15 seconds");
            Serial.println("  (Module may still be booting - will retry in fast boot check)");
        }
    } else {
        // Module was already on - verify it's actually ready
        Serial.println("  Module already on - verifying readiness...");
        
        // Give it a moment to stabilize, then verify with multiple AT commands
        delay(500);
        
        bool verified_ready = false;
        for (int i = 0; i < 3; i++) {
            // Flush any garbage
            while (Serial1.available()) {
                Serial1.read();
            }
            
            Serial1.print("AT\r");
            Serial1.flush();
            delay(300);
            
            String verify_resp = "";
            uint32_t verify_start = millis();
            while ((millis() - verify_start) < 500) {
                if (Serial1.available()) {
                    char c = Serial1.read();
                    verify_resp += c;
                    if (verify_resp.indexOf("OK") >= 0 || verify_resp.indexOf("ERROR") >= 0) {
                        break;
                    }
                }
                delay(10);
            }
            
            if (verify_resp.indexOf("OK") >= 0) {
                verified_ready = true;
                break;
            }
            delay(200);
        }
        
        if (verified_ready) {
            Serial.println("  Verified ready");
            module_ready = true;
        } else {
            Serial.println("  Verification failed - will retry in SMS check");
            module_ready = false;  // Let SMS check retry
        }
    }
    
    // FAST LTE BOOT CHECK - Run after power-on (module needs time to boot)
    // This checks if LTE module is already registered and sets time if so
    // If not registered, attempts brief registration (no full reset)
    // Load APN from NVS (needed for fast boot check)
    lteLoadAPN();
    lteLoadLastSMSTimestamp();  // Load last SMS timestamp for comparison
    
    // Increment wake count (counts all wakes - both hourly and SMS check)
    wakeCount++;
    Serial.printf("Wake count: %lu (time sync every 20 wakes)\n", (unsigned long)wakeCount);
    
    // Check if we need time sync
    // Only sync if: 1) time is invalid, OR 2) we've had 20+ wakes (periodic resync)
    time_t now = time(nullptr);
    bool timeValid = (now > 1577836800);  // After Jan 1, 2020
    bool needsTimeSync = !timeValid || (wakeCount >= 20);
    
    if (needsTimeSync && strlen(lteAPN) > 0) {
        if (!timeValid) {
            Serial.println("Time invalid - attempting time sync via LTE...");
        } else {
            Serial.println("Periodic time resync (20+ wakes) - attempting sync via LTE...");
            wakeCount = 0;  // Reset counter after sync
        }
        // First try: Fast check (module already registered)
        bool lte_time_set = lteFastBootCheck();
        
        // If fast check failed, try brief registration attempt
        if (!lte_time_set) {
            Serial.println("  Fast check failed - attempting brief registration...");
            lte_time_set = lteBriefRegistrationAttempt();
        }
        
        if (lte_time_set) {
            // Time was set by LTE - will skip WiFi/NTP later
            now = time(nullptr);
            timeValid = (now > 1577836800);
            if (timeValid) {
                Serial.println("Time sync successful via LTE");
            }
        } else {
            Serial.println("LTE time sync failed - will try WiFi/NTP if needed");
        }
    } else if (needsTimeSync && strlen(lteAPN) == 0) {
        // No LTE APN configured, but we need time sync
        if (!timeValid) {
            Serial.println("Time invalid and no LTE APN - WiFi/NTP sync will be attempted in hourly cycle");
        }
    }
    
    // ================================================================
    // Dual Wake Architecture: Determine wake type AFTER time sync
    // ================================================================
    // Now that time is (hopefully) set, determine wake type
    now = time(nullptr);
    timeValid = (now > 1577836800);
    bool isHourlyWake = false;
    
    if (timeValid) {
        struct tm tm_utc;
        gmtime_r(&now, &tm_utc);
        // Check if we're at the top of the hour (XX:00)
        isHourlyWake = (tm_utc.tm_min == 0);
        
        Serial.printf("\n=== Wake Type Detection ===\n");
        Serial.printf("Current time: %02d:%02d:%02d\n", tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec);
        Serial.printf("Wake type: %s\n", isHourlyWake ? "HOURLY (XX:00) - Full cycle" : "SMS CHECK - Minimal wake");
        Serial.println("===========================\n");
    } else {
        Serial.println("\n=== Wake Type Detection ===\n");
        Serial.println("Time invalid - defaulting to SMS check wake");
        Serial.println("===========================\n");
        isHourlyWake = false;
    }
    
    // Route to appropriate wake handler BEFORE the old SMS check code
    if (!isHourlyWake) {
        // SMS check wake: Minimal operations, fast return to sleep
        Serial.println("Routing to SMS check only (minimal wake)...");
        performSMSCheckOnly();
        
        // Calculate sleep duration and enter sleep
        uint32_t sleepDuration = calculateSleepDuration(false);  // false = SMS check wake
        Serial.printf("SMS check complete, sleeping for %lu seconds\n", (unsigned long)sleepDuration);
        Serial.flush();
        sleepNowSeconds(sleepDuration);
        // Never returns
    }
    
    // If we reach here, it's an hourly wake - continue with normal boot sequence
    // The old SMS check code below will be skipped for hourly wakes in Phase 4
    
    // Always check for latest SMS on boot (regardless of time sync status)
    // This allows monitoring new messages even when time is already valid
    // Note: Serial1 is already initialized from power-on check above
    // IMPORTANT: Module must be registered to receive new SMS messages
    // NOTE: For hourly wakes, this runs as part of the full cycle
    if (strlen(lteAPN) > 0) {
        Serial.println("\n[LTE SMS Check]");
        
        // Flush any garbage
        while (Serial1.available()) {
            Serial1.read();
        }
        
        // Ensure echo is disabled and module is ready
        // Even if module was detected as "already on", it may need a moment to stabilize
        bool module_ready = false;
        for (int i = 0; i < 8; i++) {  // More retries
            // Flush any garbage first
            while (Serial1.available()) {
                Serial1.read();
            }
            
            Serial1.print("ATE0\r");
            Serial1.flush();
            delay(400);  // Longer delay for response
            
            // Read response with longer timeout
            String test_resp = "";
            uint32_t resp_start = millis();
            while ((millis() - resp_start) < 600) {
                if (Serial1.available()) {
                    char c = Serial1.read();
                    test_resp += c;
                    if (test_resp.indexOf("OK") >= 0 || test_resp.indexOf("ERROR") >= 0) {
                        break;
                    }
                }
                delay(10);
            }
            
            if (test_resp.indexOf("OK") >= 0) {
                module_ready = true;
                break;
            }
            
            // If not ready, wait before retry
            delay(300);
        }
        
        if (module_ready) {
            // Flush again after echo disable
            delay(200);
            while (Serial1.available()) {
                Serial1.read();
            }
            
            // Check if module is registered (required to receive new SMS)
            Serial.print("  Checking registration status...");
            Serial1.print("AT+CEREG?\r");
            Serial1.flush();
            delay(300);
            
            String reg_response = "";
            uint32_t reg_start = millis();
            while ((millis() - reg_start) < 2000) {
                if (Serial1.available()) {
                    char c = Serial1.read();
                    reg_response += c;
                    if (reg_response.indexOf("OK") >= 0 || reg_response.indexOf("ERROR") >= 0) {
                        break;
                    }
                }
                delay(10);
            }
            
            // Parse registration status
            bool is_registered = false;
            int cereg_pos = reg_response.indexOf("+CEREG:");
            if (cereg_pos >= 0) {
                int comma1 = reg_response.indexOf(",", cereg_pos);
                if (comma1 > cereg_pos) {
                    int comma2 = reg_response.indexOf(",", comma1 + 1);
                    int end = (comma2 > comma1) ? comma2 : reg_response.indexOf("\r", comma1);
                    if (end < 0) end = reg_response.indexOf("\n", comma1);
                    if (end < 0) end = reg_response.length();
                    
                    String status_str = reg_response.substring(comma1 + 1, end);
                    status_str.trim();
                    int status = status_str.toInt();
                    is_registered = (status == 1 || status == 5);  // 1=home, 5=roaming
                }
            }
            
            if (is_registered) {
                Serial.println(" registered");
                Serial.print("  Checking for latest SMS...");
                
                // Get most recent SMS
                SMSMessage most_recent;
                if (getMostRecentSMS(&Serial1, most_recent)) {
                    // Handle stale stored timestamp: if stored timestamp is newer than any visible message,
                    // it means the message was deleted or the timestamp is wrong - reset to most recent visible
                    if (lastSMSTimestamp > 0 && most_recent.timestamp < lastSMSTimestamp) {
                        lteSaveLastSMSTimestamp(most_recent.timestamp);
                    }
                    
                    // Check if this is a new message (timestamp is newer than stored)
                    bool is_new = (most_recent.timestamp > lastSMSTimestamp);
                    
                    if (is_new) {
                        Serial.printf(" NEW: %s (%s): %s\n", 
                                     most_recent.sender.c_str(), 
                                     most_recent.timestamp_str.c_str(), 
                                     most_recent.text.c_str());
                        // Save the new timestamp
                        lteSaveLastSMSTimestamp(most_recent.timestamp);
                    } else {
                        Serial.printf(" (no new messages, last: %s from %s)\n",
                                     most_recent.timestamp_str.c_str(),
                                     most_recent.sender.c_str());
                    }
                } else {
                    Serial.println(" none found");
                }
            } else {
                Serial.println(" not registered");
                Serial.println("  Attempting brief registration to receive new SMS...");
                
                // Attempt brief registration (same as time sync path)
                bool reg_success = lteBriefRegistrationAttempt();
                
                if (reg_success) {
                    Serial.println("  Registration successful - checking for latest SMS...");
                    
                    // Flush UART after registration
                    delay(500);
                    while (Serial1.available()) {
                        Serial1.read();
                    }
                    
                    // Get most recent SMS
                    SMSMessage most_recent;
                    if (getMostRecentSMS(&Serial1, most_recent)) {
                        // Handle stale stored timestamp: if stored timestamp is newer than any visible message,
                        // it means the message was deleted or the timestamp is wrong - reset to most recent visible
                        if (lastSMSTimestamp > 0 && most_recent.timestamp < lastSMSTimestamp) {
                            lteSaveLastSMSTimestamp(most_recent.timestamp);
                        }
                        
                        // Check if this is a new message (timestamp is newer than stored)
                        bool is_new = (most_recent.timestamp > lastSMSTimestamp);
                        
                        if (is_new) {
                            Serial.printf(" NEW: %s (%s): %s\n", 
                                         most_recent.sender.c_str(), 
                                         most_recent.timestamp_str.c_str(), 
                                         most_recent.text.c_str());
                            // Save the new timestamp
                            lteSaveLastSMSTimestamp(most_recent.timestamp);
                        } else {
                            Serial.printf(" (no new messages, last: %s from %s)\n",
                                         most_recent.timestamp_str.c_str(),
                                         most_recent.sender.c_str());
                        }
                    } else {
                        Serial.println(" none found");
                    }
                } else {
                    Serial.println("  Registration failed - cannot receive new SMS");
                    Serial.println("  (Use 'h' command for full registration attempt)");
                }
            }
        } else {
            Serial.println("  Module not ready (may still be booting)");
        }
    }
#endif

    // Bring up PA enable early (matches known-good ESP-IDF example behavior)
    pinMode(PIN_CODEC_PA_EN, OUTPUT);
    digitalWrite(PIN_CODEC_PA_EN, HIGH);

    pinMode(PIN_USER_LED, OUTPUT);
    digitalWrite(PIN_USER_LED, LOW);

    // Check if we woke from deep sleep (non-switch-D wake)
    bool wokeFromSleep = (wakeCause != ESP_SLEEP_WAKEUP_UNDEFINED);
    
    if (wokeFromSleep) {
        // Quick boot after deep sleep - skip serial wait
        delay(100);  // Brief delay for serial to init
        Serial.println("\n=== Woke from deep sleep ===");
        Serial.printf("Boot count: %u, Wake cause: %d\n", sleepBootCount, wakeCause);
        
        // CRITICAL: Restore system time from persistent RTC (system time doesn't persist across deep sleep)
        // This ensures time(nullptr) returns the correct time
        uint64_t rtc_time_ms = sleep_get_time_ms();
        if (rtc_time_ms > 1700000000000ULL) {  // Valid time (after 2023)
            struct timeval tv;
            tv.tv_sec = rtc_time_ms / 1000;
            tv.tv_usec = (rtc_time_ms % 1000) * 1000;
            settimeofday(&tv, NULL);
            time_t restored_time = time(nullptr);
            struct tm tm_utc;
            gmtime_r(&restored_time, &tm_utc);
            Serial.printf("System time restored from RTC: %02d:%02d:%02d\n", 
                         tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec);
        } else {
            Serial.println("WARNING: RTC time invalid, system time not restored");
        }
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
    
    // Initialize display (required - PSRAM doesn't persist through deep sleep)
    Serial.println("Initializing display...");
    if (!display.begin(PIN_CS0, PIN_CS1, PIN_DC, PIN_RESET, PIN_BUSY)) {
        Serial.println("ERROR: Display initialization failed!");
        while (1) delay(1000);
    }
    Serial.println("Display initialized");
    
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

    // ================================================================
    // Auto cycle: random PNG + time/date overlay + beep + deep sleep
    // (This path is now only for hourly wakes - SMS check path exits earlier)
    // ================================================================
    if (kAutoCycleEnabled) {
        bool shouldRun = true;
        
        // ALWAYS wait for keypress before starting auto-cycle (both cold boot and wake from sleep)
        // Drain any buffered bytes (some terminals send a newline on connect)
        while (Serial.available()) {
            (void)Serial.read();
        }
        
        Serial.println("\n========================================");
        Serial.println("Press any key to start auto-cycle...");
        Serial.println("(Or press '!' to cancel and stay interactive)");
        Serial.println("========================================");
        Serial.flush();
        
        // Wait for user to press a key
        uint32_t startWait = millis();
        while (!Serial.available() && (millis() - startWait < 2000)) {  // 2 second timeout
            delay(10);
        }
        
        if (Serial.available()) {
            char ch = (char)Serial.read();
            if (ch == '!') {
                shouldRun = false;
                Serial.println("Auto-cycle cancelled -> staying in interactive mode.");
            } else {
                // Clear any remaining bytes
                while (Serial.available()) {
                    (void)Serial.read();
                }
            }
        } else {
            Serial.println("Timeout - starting auto-cycle...");
        }

        if (shouldRun) {
            // Run auto-cycle in a dedicated task with a larger stack than Arduino loopTask,
            // since SD init and PNG decoding are stack-heavy.
            xTaskCreatePinnedToCore(auto_cycle_task, "auto_cycle", 16384, nullptr, 5, &g_auto_cycle_task, 0);
            return; // yield loopTask; auto_cycle_task will deep-sleep the device
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
#endif
#if SDMMC_ENABLED
    Serial.println("  SD Card: 'M'=mount(4-bit), 'm'=mount(1-bit), 'L'=list, 'I'=info, 'T'=test, 'U'=unmount, 'D'=diag, 'P'=power cycle, 'O/o'=pwr on/off");
    Serial.println("  BMP:     'B'=load random BMP, 'b'=list BMP files");
#endif
    Serial.println("  Sleep:   'z'=status, '1'=10s, '2'=30s, '3'=60s, '5'=5min deep sleep");
    
    // Check internal RTC time (may have been set by fast LTE boot check)
    now = time(nullptr);
    timeValid = (now > 1577836800);
    
    if (wokeFromSleep && timeValid) {
        // Fast path after deep sleep - time already valid, skip WiFi/NTP
        struct tm* timeinfo = gmtime(&now);
        Serial.printf("Time: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                      timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                      timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
#if WIFI_ENABLED
        wifiLoadCredentials();  // Still load credentials for later use
#endif
#if LTE_ENABLED
        lteLoadAPN();  // Load LTE APN for later use
#endif
        Serial.println("Ready! Enter command...\n");
        return;  // Skip WiFi auto-connect and NTP sync
    }
    
    // Cold boot path - full initialization
    Serial.println("\n--- Time Check ---");
    // Re-check time (may have been set by fast LTE boot check)
    now = time(nullptr);
    timeValid = (now > 1577836800);
    if (timeValid) {
        struct tm* timeinfo = gmtime(&now);
        Serial.printf("Current time: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                      timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                      timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    } else {
        Serial.println("Time not set - need NTP sync");
    }

#if WIFI_ENABLED
    // Load saved credentials from NVS
    wifiLoadCredentials();
#endif
#if LTE_ENABLED
    // Fast LTE boot check already ran at start of setup() - check if time is now valid
    // If time is valid, assume LTE might have set it (conservative approach to skip WiFi/NTP)
    bool lte_time_set = timeValid;
#endif

#if WIFI_ENABLED
    // If time not valid, try to auto-connect and sync
    // But skip if LTE already set the time
#if LTE_ENABLED
    // lte_time_set is already declared above in LTE_ENABLED block
#else
    bool lte_time_set = false;
#endif
    if (!timeValid && !lte_time_set) {
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
    } else {
        // Time is valid, just show WiFi status
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
#endif
#if LTE_ENABLED
    Serial.println("  LTE:     'j'=init, 'J'=set APN, 'k'=connect, 'K'=disconnect, 'y'=status, 'u'=test, 'h'=full check");
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
                
                // Set persistent RTC time (survives deep sleep) - this also sets system time
                uint64_t time_ms = (uint64_t)timestamp * 1000ULL;
                sleep_set_time_ms(time_ms);
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
#endif
#if LTE_ENABLED
        else if (c == 'j') {
            lteInit();
        }
        else if (c == 'J') {
            lteSetAPN();
        }
        else if (c == 'k' && lteModule != nullptr) {
            lteConnect();
        }
        else if (c == 'K' && lteModule != nullptr) {
            lteDisconnect();
        }
        else if (c == 'y' || c == 'Y') {
            lteStatus();
        }
        else if (c == 'u' || c == 'U') {
            lteTest();
        }
        else if (c == 'h' || c == 'H') {
            lteFullCheck();
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

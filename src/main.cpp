/**
 * @file main.cpp
 * @brief ESP32-P4 application for EL133UF1 e-ink display
 * 
 * Full-featured application for the EL133UF1 13.3" Spectra 6 e-ink display
 * on ESP32-P4. Includes WiFi, SD card support, deep sleep, and all features.
 * 
 * Build with: pio run -e esp32p4
 * 
 * === PIN MAPPING FOR WAVESHARE ESP32-P4-WIFI6 ===
 * Configured via build flags in platformio.ini
 * 
 * Display SPI (GPIO pin assignments):
 *   SCLK    ->   GPIO3
 *   MOSI    ->   GPIO2
 *   CS0     ->   GPIO23
 *   CS1     ->   GPIO48
 *   DC      ->   GPIO26
 *   RESET   ->   GPIO22
 *   BUSY    ->   GPIO47
 *
 */

// Only compile this file for ESP32 builds
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <vector>
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "platform_hal.h"
#include "EL133UF1.h"
#include "EL133UF1_TTF.h"
#include "EL133UF1_BMP.h"
#include "EL133UF1_PNG.h"
#include "EL133UF1_Color.h"

#include <pngle.h>  // For direct PNG decoding to RGB buffer
#include "lodepng_psram.h"  // Custom PSRAM allocators for lodepng (must be before lodepng.h)
#include "lodepng.h"  // For PNG encoding (canvas save functionality)
#include "EL133UF1_TextPlacement.h"
#include "text_layout.h"
#include "text_elements.h"
#include "OpenAIImage.h"

#include "fonts/opensans.h"
//#include "fonts/dancing.h"

#include "es8311_simple.h"
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>  // For stat() in LittleFS directory listing
#include <dirent.h>    // For opendir/readdir/closedir in LittleFS directory listing
#include <stdio.h>
#include <stdarg.h>  // For va_list in logging functions

// ESP8266Audio for robust WAV and MP3 parsing and playback
#include "AudioOutputI2S.h"
#include "AudioGeneratorWAV.h"
#include "AudioGeneratorMP3.h"
#include "AudioFileSource.h"

// WiFi support for ESP32-P4 (via ESP32-C6 companion chip)
#if !defined(DISABLE_WIFI) || defined(ENABLE_WIFI_TEST)
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include "nvs_guard.h"  // RAII wrapper for Preferences
#include "mqtt_guard.h"  // RAII wrapper for MQTT connections
#include "wifi_guard.h"  // RAII wrapper for WiFi connections
#include "json_utils.h"  // JSON field extraction utilities
#include "webui_crypto.h"  // Web UI encryption and authentication functions
#include "nvs_manager.h"  // NVS manager for persistent settings
#include "wifi_manager.h"  // WiFi connection and NTP synchronization manager
#include "mqtt_handler.h"  // MQTT connection, publishing, and message handling
#include "command_dispatcher.h"  // Unified command dispatcher
#include "canvas_handler.h"  // Canvas display and save functionality
#include "display_manager.h"  // Unified display media with overlay
// ArduinoJson removed - using cJSON instead (via json_utils.h)
#include "cJSON.h"  // cJSON for JSON parsing (also available via json_utils.h)
#include "mbedtls/sha256.h"  // ESP32 built-in SHA256 support
#include "mbedtls/md.h"  // ESP32 built-in HMAC support
#include "mbedtls/aes.h"  // ESP32 built-in AES encryption support
#include "mbedtls/base64.h"  // ESP32 built-in base64 encoding support
#define WIFI_ENABLED 1
#else
#define WIFI_ENABLED 0
#endif

#include "esp_littlefs.h"

// MQTT support using ESP-IDF esp-mqtt component
#include "mqtt_client.h"
#include "esp_crt_bundle.h"
#include <string.h>
// ESP32-P4 hardware JPEG encoder for thumbnail generation
#if defined(SOC_JPEG_ENCODE_SUPPORTED) && SOC_JPEG_ENCODE_SUPPORTED
#include "driver/jpeg_encode.h"
#endif
// miniz for canvas compression decompression
extern "C" {
#include "miniz.h"
}
// OTA update support - custom implementation with SD card buffering
#include <WiFiServer.h>
#include <WiFiClient.h>
// Increase max request body size for canvas uploads (800x600 PNG can be ~100KB)
// PsychicHttp default is 16KB, we need at least 128KB
// Increase max request body size for large file uploads
// ESP32-P4 has PSRAM, so we can handle larger requests
// Using 512KB as a reasonable limit (can handle most files in one request)
// For larger files, we'll use chunked uploads
#ifndef MAX_REQUEST_BODY_SIZE
#define MAX_REQUEST_BODY_SIZE (1024 * 1024)  // 1MB (for canvas pixel data: 800x600 = 480KB raw, ~640KB base64)
#endif
#include <PsychicHttp.h>
#include <PsychicStreamResponse.h>
// #define this to enable SSL at build (or switch to the 'ssl' build target in vscode)
#ifdef PSY_ENABLE_SSL
  #include <PsychicHttpsServer.h>
#include "certificates.h"
#endif
#include "web_assets.h"
// Keep ESP-IDF OTA includes for OTA operations
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_partition.h"
#endif

// SD Card support via SDMMC (always enabled)
#include <SD_MMC.h>
#include <FS.h>
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "esp_ldo_regulator.h"
#include "ff.h"  // FatFs for custom AudioFileSource

// ============================================================================
// Pin definitions for ESP32-P4
// Override these with build flags or edit for your specific board
// ============================================================================

// Defaults for Waveshare ESP32-P4-WIFI6
#ifndef PIN_SPI_SCK
#define PIN_SPI_SCK   3     // GPIO3
#endif
#ifndef PIN_SPI_MOSI
#define PIN_SPI_MOSI  2     // GPIO2
#endif
#ifndef PIN_CS0
#define PIN_CS0       23    // GPIO23
#endif
#ifndef PIN_CS1
#define PIN_CS1       48    // GPIO48
#endif
#ifndef PIN_DC
#define PIN_DC        26    // GPIO26
#endif
#ifndef PIN_RESET
#define PIN_RESET     22    // GPIO22
#endif
#ifndef PIN_BUSY
#define PIN_BUSY      47    // GPIO47
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

// RTC: Using ESP32 internal RTC only

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

// GPIO54 - C6_ENABLE pin (LOW during deep sleep, HIGH when awake)
#ifndef C6_ENABLE
#define C6_ENABLE  54   // GPIO54 for C6 companion chip enable control
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

// OpenAI image generation
OpenAIImage openai;
static uint8_t* aiImageData = nullptr;
static size_t aiImageLen = 0;

// Last loaded image filename
String g_lastImagePath = "";  // Non-static for display_manager access

// Deep sleep boot counter (persists in RTC memory across deep sleep)
RTC_DATA_ATTR uint32_t sleepBootCount = 0;
RTC_DATA_ATTR uint32_t lastImageIndex = 0;  // Track last displayed image for sequential cycling
uint32_t lastMediaIndex = 0;  // Track last displayed image from media.txt (stored in NVS)
static bool showOperationInProgress = false;  // Lock to prevent concurrent show operations

// MQTT state - moved to mqtt_handler.cpp
// Access via getMqttClient() and isMqttConnected() functions from mqtt_handler.h
// RTC drift compensation: store sleep duration and target wake time
RTC_DATA_ATTR uint32_t lastSleepDurationSeconds = 0;  // How long we intended to sleep
RTC_DATA_ATTR uint8_t targetWakeHour = 255;  // Target wake hour (255 = not set)
RTC_DATA_ATTR uint8_t targetWakeMinute = 255;  // Target wake minute (255 = not set)
RTC_DATA_ATTR bool thumbnailPendingPublish = false;  // Flag to indicate thumbnail needs to be published

// Structure for passing data to show media task
struct ShowMediaTaskData {
    int index;
    bool* success;
    size_t* nextIndex;
    SemaphoreHandle_t completionSem;
};
RTC_DATA_ATTR uint32_t ntpSyncCounter = 0;  // Counter for periodic NTP resync
RTC_DATA_ATTR bool usingMediaMappings = false;  // Track if we're using media.txt or scanning all PNGs

// Cached WiFi credentials in RTC memory (survives deep sleep, avoids NVS reads)
RTC_DATA_ATTR char cachedWifiSSID[33] = "";
RTC_DATA_ATTR char cachedWifiPSK[65] = "";
RTC_DATA_ATTR bool wifiCredentialsCached = false;  // Flag to indicate if credentials are cached
RTC_DATA_ATTR char lastAudioFile[64] = "";  // Last audio file path for instant playback on switch D wake

// Font list stored in RTC memory (scanned once at cold boot)
#define MAX_FONTS_IN_RTC 32
#define MAX_FONT_NAME_LEN 63
#define MAX_FONT_FILENAME_LEN 63

struct FontInfo {
    char name[MAX_FONT_NAME_LEN + 1];      // Font family name
    char filename[MAX_FONT_FILENAME_LEN + 1];  // Font filename
    bool isBuiltin;  // true for built-in fonts (OpenSans), false for LittleFS fonts
};

RTC_DATA_ATTR FontInfo g_rtcFontList[MAX_FONTS_IN_RTC];
RTC_DATA_ATTR uint8_t g_rtcFontCount = 0;  // Number of fonts in the list

// ============================================================================
// Audio: ES8311 + I2S test tone
// ============================================================================
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <math.h>

static ES8311Simple g_codec;
static AudioOutputI2S* g_audio_output = nullptr;
static TaskHandle_t g_audio_task = nullptr;
static volatile bool g_audio_running = false;
int g_audio_volume_pct = 50;  // UI percent (0..100), mapped into codec range below (non-static for nvs_manager module access)
Preferences volumePrefs;  // NVS preferences for volume storage (non-static for nvs_manager module access)
static Preferences numbersPrefs;  // NVS preferences for allowed phone numbers
Preferences sleepPrefs;  // NVS preferences for sleep duration setting (non-static for nvs_manager module access)
static Preferences otaPrefs;  // NVS preferences for OTA version tracking
Preferences mediaPrefs;  // NVS preferences for media index storage (non-static for nvs_manager module access)
Preferences hourSchedulePrefs;  // NVS preferences for hour schedule (non-static for nvs_manager module access)
Preferences authPrefs;  // NVS preferences for web UI authentication (non-static for webui_crypto module access)
static const char* OPENAI_API_KEY = "";
static bool g_codec_ready = false;
uint8_t g_sleep_interval_minutes = 1;  // Sleep interval in minutes (must be factor of 60) (non-static for nvs_manager module access)
// Hour schedule: 24 boolean flags (one per hour, 0-23). If true, wake during that hour; if false, sleep through entire hour
bool g_hour_schedule[24];  // Default: all hours enabled (will be initialized in hourScheduleLoadFromNVS) (non-static for nvs_manager module access)

static TwoWire g_codec_wire0(0);
static TwoWire g_codec_wire1(1);
static TwoWire* g_codec_wire = nullptr;

static constexpr int kCodecVolumeMinPct = 30; // inaudible below this (silent threshold)
static constexpr int kCodecVolumeMaxPct = 80; // max volume (too loud above this)

// Auto demo cycle settings: random PNG + clock overlay + short beep + deep sleep
static constexpr bool kAutoCycleEnabled = true;
static constexpr uint32_t kCycleSleepSeconds = 60;
RTC_DATA_ATTR uint32_t g_cycle_count = 0;
static TaskHandle_t g_auto_cycle_task = nullptr;
static bool g_config_mode_needed = false;  // Flag to indicate config mode is needed
bool g_is_cold_boot = false;  // Flag to indicate this is a cold boot (not deep sleep wake) (non-static for webui_crypto module access)
static volatile bool g_ota_requested = false;  // Flag to indicate 'o' key was pressed for OTA mode
static volatile bool g_manage_requested = false;  // Flag to indicate 'm' key was pressed for web interface
static TaskHandle_t g_serial_monitor_task = nullptr;  // Task handle for serial monitor

// Forward declarations (SDMMC is always enabled)

bool pngDrawFromMediaMappings(uint32_t* out_sd_read_ms, uint32_t* out_decode_ms);
bool pngDrawRandomToBuffer(const char* dirname, uint32_t* out_sd_read_ms, uint32_t* out_decode_ms);
bool sdInit(bool mode1bit);
bool sdInitDirect(bool mode1bit = false);
// MQTT functions are now in mqtt_handler.h

// NVS storage functions (defined later)
void mediaIndexLoadFromNVS();  // Load media index from NVS (called on startup)
void mediaIndexSaveToNVS();  // Save media index to NVS

// SD card state variables (declared here for use by SD config functions and MQTT handler)
bool sdCardMounted = false;
sdmmc_card_t* sd_card = nullptr;
static esp_ldo_channel_handle_t ldo_vo4_handle = nullptr;

// Logging system - writes to both Serial and SD card
static FIL logFile;
static bool logFileOpen = false;
static const char* LOG_DIR = "0:/.logs";
static const char* LOG_FILE = "0:/.logs/log.txt";
static char LOG_ARCHIVE[64] = "0:/.logs/log_prev.txt";  // Will be updated with timestamp-based name during rotation

// Store reference to original Serial object before macro redefinition
extern HardwareSerial Serial;
static HardwareSerial* const RealSerial = &Serial;

// LogSerial class - Print subclass that writes to both Serial and log file
class LogSerial : public Print {
public:
    virtual size_t write(uint8_t c) override {
        // Always write to real Serial
        size_t result = RealSerial->write(c);
        
        // Also write to log file if open
        if (logFileOpen) {
            UINT bw;
            f_write(&logFile, &c, 1, &bw);
            // Note: We don't flush on every byte for performance
        }
        
        return result;
    }
    
    virtual size_t write(const uint8_t *buffer, size_t size) override {
        // Write to real Serial first
        size_t result = RealSerial->write(buffer, size);
        
        // Also write to log file if open
        if (logFileOpen) {
            UINT bw;
            f_write(&logFile, buffer, size, &bw);
        }
        
        return result;
    }
    
    // Forward available() to real Serial (Stream method)
    int available() {
        Stream* stream = (Stream*)RealSerial;
        return stream->available();
    }
    
    // Forward read() to real Serial (Stream method)
    int read() {
        Stream* stream = (Stream*)RealSerial;
        return stream->read();
    }
    
    // Forward peek() to real Serial (Stream method)
    int peek() {
        Stream* stream = (Stream*)RealSerial;
        return stream->peek();
    }
    
    // Forward readStringUntil() to real Serial (Stream method)
    String readStringUntil(char terminator) {
        Stream* stream = (Stream*)RealSerial;
        return stream->readStringUntil(terminator);
    }
    
    // Operator! for compatibility with "while (!Serial)" pattern
    // HardwareSerial::operator!() checks if Serial is available (not ready)
    bool operator!() const {
        HardwareSerial* hwSerial = (HardwareSerial*)RealSerial;
        return !(*hwSerial);
    }
    
    // Forward flush() to both real Serial and log file
    void flush() {
        RealSerial->flush();
        if (logFileOpen) {
            f_sync(&logFile);
        }
    }
    
    // Forward begin() to real Serial
    void begin(unsigned long baud) {
        HardwareSerial* hwSerial = (HardwareSerial*)RealSerial;
        hwSerial->begin(baud);
    }
    
    // Forward printf-like methods
    size_t printf(const char *format, ...) {
        char buffer[512];
        va_list args;
        va_start(args, format);
        int len = vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        
        if (len > 0 && len < (int)sizeof(buffer)) {
            return write((const uint8_t*)buffer, len);
        }
        return 0;
    }
};

// Global LogSerial instance
LogSerial LogSerialInstance;

// Redirect Serial to LogSerialInstance so all Serial.print/printf/etc. calls are logged to file
#define Serial LogSerialInstance

// Logging wrapper functions
void logPrint(const char* str);
void logPrintf(const char* format, ...);
void logRotate();  // Archive current log and create new one
bool logInit();    // Initialize logging (mount SD and open log file)
void logFlush();   // Flush log file to ensure data is written
void logClose();   // Close log file (call before deep sleep)

// WiFi connection state - we keep WiFi connected from boot until deep sleep
// No WiFiGuard needed - we just check WiFi.status() and connect if needed

// Helper to ensure SD is mounted (simplifies conditional mounting logic)
static inline bool ensureSDMounted() {
    if (sdCardMounted) {
        return true;
    }
    // Try to mount if not already mounted
    return sdInitDirect(false);
}

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

    // Ensure audible volume from saved setting
    (void)g_codec.setDacVolumePercentMapped(g_audio_volume_pct, kCodecVolumeMinPct, kCodecVolumeMaxPct);
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

void audio_stop() {  // Non-static for display_manager access
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

// Forward declaration
bool isHourEnabled(int hour);

/**
 * Calculate the target wake time (as time_t) based on current time and sleep interval
 * Returns the absolute time_t value for the next aligned wake time
 */
static time_t calculateTargetWakeTime(time_t now) {
    if (now <= 1577836800) {
        return 0;  // Invalid time
    }
    
    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);
    uint32_t sec = (uint32_t)tm_utc.tm_sec;
    uint32_t min = (uint32_t)tm_utc.tm_min;
    
    // Calculate seconds until next wake time based on sleep interval
    uint32_t interval_minutes = g_sleep_interval_minutes;
    if (interval_minutes == 0 || 60 % interval_minutes != 0) {
        interval_minutes = 1;
    }
    
    // Find which "slot" we're currently in (aligned to interval)
    uint32_t current_slot = (min / interval_minutes) * interval_minutes;
    uint32_t next_slot = current_slot + interval_minutes;
    
    // If we're at a slot boundary and less than 5 seconds have passed, sleep to next slot
    if (min == current_slot && sec < 5) {
        next_slot = current_slot + interval_minutes;
    }
    
    // Calculate seconds until next aligned minute mark
    uint32_t seconds_until_target;
    int target_hour = tm_utc.tm_hour;
    uint32_t target_min = 0;
    
    if (next_slot < 60) {
        // Next slot is in the same hour
        seconds_until_target = (next_slot - min) * 60 - sec;
        target_min = next_slot;
    } else {
        // Next slot wraps to next hour (which is always :00)
        seconds_until_target = (60 - min) * 60 - sec;
        target_min = 0;
        target_hour = (tm_utc.tm_hour + 1) % 24;
    }
    
    // Check if the wake hour is enabled - if not, find next enabled hour
    if (!isHourEnabled(target_hour)) {
        // Find next enabled hour
        int nextEnabledHour = -1;
        for (int i = 1; i <= 24; i++) {
            int checkHour = (target_hour + i) % 24;
            if (isHourEnabled(checkHour)) {
                nextEnabledHour = checkHour;
                break;
            }
        }
        
        if (nextEnabledHour >= 0) {
            // Calculate seconds remaining in current hour
            uint32_t secondsRemainingInHour = (60 - min) * 60 - sec;
            
            // Calculate hours until next enabled hour
            int hoursToAdd = 0;
            int currentHour = tm_utc.tm_hour;
            if (nextEnabledHour > currentHour) {
                hoursToAdd = nextEnabledHour - currentHour;
            } else {
                hoursToAdd = (24 - currentHour) + nextEnabledHour;
            }
            
            // Total seconds: remaining in current hour + full hours until next enabled hour
            seconds_until_target = secondsRemainingInHour + (hoursToAdd - 1) * 3600;
            target_hour = nextEnabledHour;
            target_min = 0;
        }
    }
    
    // Calculate absolute target time
    return now + seconds_until_target;
}

// Forward declarations
static void checkAndStartOTA();
static void checkAndStartManage();

static void sleepNowSeconds(uint32_t seconds) {
    // Note: Target wake time calculation is done by the caller (sleepUntilNextMinuteOrFallback).
    // We only recalculate it right before sleep to account for cleanup delays.
    
    // Check if OTA or web interface was requested before sleeping
    checkAndStartOTA();
    checkAndStartManage();
    
    // ESP32-P4 can only wake from deep sleep using LP GPIOs (0-15) via ext1
    // Switch D is on GPIO51, which is NOT an LP GPIO
    // If GPIO51 is bridged to an LP GPIO (e.g., GPIO4), use PIN_SW_D_BRIDGE to enable wake
    
    // DISABLED: Switch D wake functionality - no GPIO configuration needed
    // GPIO wake functionality completely disabled to avoid interfering with bootloader entry
    // Only timer wake is enabled (no GPIO pins are configured)
    

    // Close log file before deep sleep to prevent queue access issues
    // The log file uses FatFs which may have background tasks/queues
    // FatFs and SDMMC driver may have background tasks that access FreeRTOS queues
    if (logFileOpen) {
        logClose();
        // Reduced delay - logClose() should handle flushing, minimal delay for queue cleanup
        vTaskDelay(pdMS_TO_TICKS(50));  // Reduced from 300ms + 200ms = 500ms to 50ms
    }
    
    // Disconnect WiFi before deep sleep (but don't shut down ESP-Hosted completely)
    // Just disconnect from network - ESP-Hosted will handle its own state
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Disconnecting WiFi before deep sleep...");
        WiFi.disconnect(true);
        // Reduced delay - WiFi.disconnect() is asynchronous but needs some time for cleanup
        vTaskDelay(pdMS_TO_TICKS(100));  // Reduced from 500ms + 200ms = 700ms to 100ms
        Serial.println("WiFi disconnected");
    }
    
    // Pull C6_ENABLE (GPIO54) LOW before entering deep sleep
    // This pin will remain LOW during deep sleep and be pulled HIGH on wake
    Serial.println("Pulling C6_ENABLE (GPIO54) LOW before deep sleep...");
    pinMode(C6_ENABLE, OUTPUT);
    digitalWrite(C6_ENABLE, LOW);
    // No delay needed - digitalWrite is synchronous, pin state is set immediately
    
    // Configure pad hold for C6_ENABLE to maintain LOW state during deep sleep
    Serial.println("Configuring pad hold for C6_ENABLE to maintain LOW during deep sleep...");
    gpio_hold_en((gpio_num_t)C6_ENABLE);
    Serial.println("C6_ENABLE pad hold enabled - will remain LOW during deep sleep");
    
    // Flush serial and ensure all operations complete before deep sleep
    // This helps prevent bootloader assertion errors after wake
    Serial.flush();
    // Reduced delay - Serial.flush() blocks until done, minimal additional delay needed
    vTaskDelay(pdMS_TO_TICKS(50));  // Reduced from 300ms to 50ms
    
    // Allow any remaining FreeRTOS background tasks to complete
    // Critical: Give all background tasks (SD card, WiFi, MQTT cleanup) time to complete
    // Reduced combined delays - single vTaskDelay is sufficient
    vTaskDelay(pdMS_TO_TICKS(100));  // Reduced from 300ms + 300ms + 100ms = 700ms to 100ms
    
    // CRITICAL: Final time adjustment RIGHT BEFORE enabling sleep timer
    // All the delays above (log close, WiFi disconnect, serial flush, etc.) have taken time
    // Re-read time and recalculate target wake time from ACTUAL current time
    // This ensures we wake at the correct time regardless of how long cleanup took
    time_t time_before_sleep = time(nullptr);
    if (time_before_sleep > 1577836800) {
        // Recalculate target wake time based on current time (after all cleanup delays)
        time_t recalculated_target = calculateTargetWakeTime(time_before_sleep);
        
        if (recalculated_target > time_before_sleep) {
            int32_t recalc_seconds = (int32_t)(recalculated_target - time_before_sleep);
            
            if (recalc_seconds >= 5) {  // Don't sleep for less than 5 seconds
                struct tm tm_target;
                gmtime_r(&recalculated_target, &tm_target);
                // Only log if there's a significant difference (>1 second) to reduce noise
                if (abs((int32_t)recalc_seconds - (int32_t)seconds) > 1) {
                    Serial.printf("Final adjustment: %ld seconds until target %02d:%02d:00 (was %lu seconds)\n",
                                 (long)recalc_seconds, tm_target.tm_hour, tm_target.tm_min, (unsigned long)seconds);
                }
                seconds = (uint32_t)recalc_seconds;
            } else {
                Serial.printf("WARNING: Recalculation would result in sleep < 5 seconds (%ld), using original %lu seconds\n",
                             (long)recalc_seconds, (unsigned long)seconds);
            }
        } else {
            // Target time is in the past (shouldn't happen, but handle gracefully)
            Serial.printf("WARNING: Target wake time is in the past, using original %lu seconds\n", (unsigned long)seconds);
        }
    }
    
    // Enable timer wake RIGHT BEFORE sleeping (after all delays)
    esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
    
    esp_deep_sleep_start();
}

/**
 * Check if a specific hour (0-23) is enabled for waking
 * This function is defined here (before sleepUntilNextMinuteOrFallback) so it can be used there
 */
bool isHourEnabled(int hour) {
    if (hour < 0 || hour >= 24) {
        return true;  // Invalid hour, default to enabled
    }
    return g_hour_schedule[hour];
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
    uint32_t min = (uint32_t)tm_utc.tm_min;
    
    // Calculate seconds until next wake time based on sleep interval
    // Sleep interval must be a factor of 60 (1, 2, 3, 4, 5, 6, 10, 12, 15, 20, 30, 60)
    // This ensures we always wake at :00 for the hourly media cycle
    // We sleep until the NEXT aligned minute mark, never waking mid-minute
    uint32_t interval_minutes = g_sleep_interval_minutes;
    if (interval_minutes == 0 || 60 % interval_minutes != 0) {
        // Invalid interval, default to 1 minute
        interval_minutes = 1;
        Serial.println("WARNING: Invalid sleep interval, defaulting to 1 minute");
    }
    
    // Find which "slot" we're currently in (0-based, aligned to interval)
    // For interval=1: slots are :00, :01, :02, ..., :59 (every minute)
    // For interval=2: slots are :00, :02, :04, ..., :58 (every 2 minutes)
    // For interval=4: slots are :00, :04, :08, ..., :56 (every 4 minutes)
    uint32_t current_slot = (min / interval_minutes) * interval_minutes;
    uint32_t next_slot = current_slot + interval_minutes;
    
    // If we're at a slot boundary and less than 5 seconds have passed, sleep to next slot
    // (This handles the case where we just woke at an aligned minute)
    if (min == current_slot && sec < 5) {
        next_slot = current_slot + interval_minutes;
    }
    
    // Calculate target wake time using the helper function
    time_t target_wake_time = calculateTargetWakeTime(now);
    
    if (target_wake_time == 0 || target_wake_time <= now) {
        // Invalid time or target in past - use fallback
        Serial.printf("Invalid target wake time, sleeping for fallback: %lu seconds\n", (unsigned long)fallback_seconds);
        sleepNowSeconds(fallback_seconds);
        return;  // Never returns
    }
    
    // Calculate sleep duration
    int32_t sleep_s = (int32_t)(target_wake_time - now);
    
    // Sanity checks
    if (sleep_s < 5) {
        // Too short - add one interval
        sleep_s += interval_minutes * 60;
        // Recalculate target
        target_wake_time = calculateTargetWakeTime(now + interval_minutes * 60);
        if (target_wake_time > now) {
            sleep_s = (int32_t)(target_wake_time - now);
        }
    }
    
    if (sleep_s > 24 * 3600) {
        Serial.printf("WARNING: Sleep calculation exceeds 24 hours (%ld), clamping to 24 hours\n", (long)sleep_s);
        sleep_s = 24 * 3600;
    }
    
    // Get target time components for logging
    struct tm tm_target;
    gmtime_r(&target_wake_time, &tm_target);
    
    Serial.printf("Current time: %02d:%02d:%02d, sleep interval: %lu min, sleeping %ld seconds (wake at %02d:%02d:00)\n",
                  tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec, 
                  (unsigned long)interval_minutes, (long)sleep_s,
                  tm_target.tm_hour, tm_target.tm_min);
    
    // Store sleep duration and target wake time in RTC memory for drift compensation
    lastSleepDurationSeconds = (uint32_t)sleep_s;
    targetWakeHour = (uint8_t)tm_target.tm_hour;
    targetWakeMinute = (uint8_t)tm_target.tm_min;
    
    // Note: sleepNowSeconds() will recalculate the target wake time right before actually sleeping
    // to account for ALL delays (WiFi disconnect, log close, serial flush, etc.)
    // This ensures we wake at the correct time regardless of how long cleanup took
    sleepNowSeconds((uint32_t)sleep_s);
    // Never returns
}

// WiFi functions moved to wifi_manager.cpp/h module (Priority 1 refactoring)

/**
 * Perform NTP sync (extracted from ensureTimeValid for reuse)
 * This function assumes WiFi is already connected
 * @param timeout_ms Maximum time to wait for NTP sync
 * @return true if NTP sync succeeded, false otherwise
 */
// WiFi functions moved to wifi_manager.cpp/h module (Priority 1 refactoring)
// All WiFi functions are now in src/wifi_manager.cpp

// ============================================================================
// SD Card-Based Configuration for Quotes and Audio
// ============================================================================



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

std::vector<LoadedQuote> g_loaded_quotes;  // Non-static for display_manager access
bool g_quotes_loaded = false;  // Non-static for display_manager access

// Structure to hold image-to-audio mappings
struct MediaMapping {
    String imageName;  // e.g., "sunset.png"
    String audioFile;  // e.g., "ocean.wav"
};

std::vector<MediaMapping> g_media_mappings;
bool g_media_mappings_loaded = false;

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
    
    if (!sdCardMounted) {
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
int loadMediaMappingsFromSD(bool autoPublish = true) {
    g_media_mappings.clear();
    g_media_mappings_loaded = false;
    
    Serial.println("\n=== Loading media mappings from SD card ===");
    
    if (!sdCardMounted) {
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
        
        // Publish media mappings if MQTT is connected (media.txt was changed/reloaded)
        // BUT only if autoPublish is true (to avoid double-publishing when called from publishMQTTMediaMappings)
        if (autoPublish && isMqttConnected() && getMqttClient() != nullptr) {
            Serial.println("  Media mappings changed - publishing to MQTT...");
            publishMQTTMediaMappings();
        }
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
    
    if (!sdCardMounted) {
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
    
    // Set volume to saved level and unmute
    (void)g_codec.setDacVolumePercentMapped(g_audio_volume_pct, kCodecVolumeMinPct, kCodecVolumeMaxPct);
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
    

    // Mount SD card if needed (fast path - no verbose output)
    // Only mount if we have a stored audio file to play
    bool needSD = (lastAudioFile[0] != '\0');
    Serial.printf("Stored audio file: %s\n", lastAudioFile[0] != '\0' ? lastAudioFile : "(none)");
    
    if (needSD && !sdCardMounted) {
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
            mediaIndexSaveToNVS();
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
    
    // Calculate sleep until next wake time based on sleep interval
    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);
    uint32_t sec = (uint32_t)tm_utc.tm_sec;
    uint32_t min = (uint32_t)tm_utc.tm_min;
    
    uint32_t interval_minutes = g_sleep_interval_minutes;
    if (interval_minutes == 0 || 60 % interval_minutes != 0) {
        interval_minutes = 1;
    }
    
    // Find next slot
    uint32_t current_slot = (min / interval_minutes) * interval_minutes;
    uint32_t next_slot = current_slot + interval_minutes;
    
    uint32_t sleep_s;
    if (next_slot < 60) {
        sleep_s = (next_slot - min) * 60 - sec;
    } else {
        sleep_s = (60 - min) * 60 - sec;
    }
    
    if (sleep_s == 0) sleep_s = interval_minutes * 60;
    if (sleep_s < 5 && sleep_s > 0) sleep_s += interval_minutes * 60;
    if (sleep_s > interval_minutes * 60 + 60) sleep_s = kCycleSleepSeconds;
    
    // Calculate wake time for logging - use ceiling division to account for partial minutes
    // (sleep_s + 59) / 60 gives us ceiling of sleep_s / 60
    uint32_t minutes_to_add = (sleep_s + 59) / 60;  // Ceiling division
    uint32_t total_minutes = min + minutes_to_add;
    uint32_t wake_min = total_minutes % 60;
    uint32_t wake_hour = tm_utc.tm_hour + (total_minutes / 60);
    if (wake_hour >= 24) {
        wake_hour = wake_hour % 24;
    }
    
    Serial.printf("Current time: %02d:%02d:%02d, sleep interval: %lu min, sleeping %lu seconds (wake at %02lu:%02lu:00)\n",
                  tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec,
                  (unsigned long)interval_minutes, (unsigned long)sleep_s,
                  (unsigned long)wake_hour, (unsigned long)wake_min);
    Serial.println("========================================\n");
    Serial.flush();
    
    sleepNowSeconds(sleep_s);
    // Never returns
}



// ============================================================================
// WiFi/MQTT Variables and Functions (needed by auto_cycle_task)
// ============================================================================

// WiFi credentials - stored in NVS (persistent) (non-static for wifi_manager module access)
char wifiSSID[33] = "";
char wifiPSK[65] = "";
Preferences wifiPrefs;

// Forward declarations for WiFi/MQTT functions
bool wifiLoadCredentials();  // Returns true if credentials loaded, false if NVS failed or missing
void enterConfigMode();  // Enter interactive configuration mode for WiFi credentials
// MQTT functions are now in mqtt_handler.h
// Thumbnail functions are now in thumbnail_utils.h

// Web UI authentication and encryption functions
// Password is used to derive separate keys for HMAC (authentication) and AES (encryption)
// Password is stored in NVS (protected by ESP32 flash encryption if enabled)
// We derive keys using PBKDF2 so we never transmit the password
// Encryption functions moved to webui_crypto.cpp/h module (Priority 1 refactoring)

// MQTT command handling
String extractCommandFromMessage(const String& msg);  // Made non-static for thumbnail publishing
String extractCommandParameter(const String& command);  // Made non-static for unified dispatcher
static String extractFromFieldFromMessage(const String& msg);
bool handleMqttCommand(const String& command, const String& originalMessage = "");  // Made non-static for thumbnail publishing
bool handleWebInterfaceCommand(const String& jsonMessage);
// Canvas handler functions are now in canvas_handler.h/cpp module
static String decryptAndValidateWebUIMessage(const String& jsonMessage);
bool handleClearCommand();  // Made non-static for unified dispatcher
bool handlePingCommand(const String& originalMessage);  // Made non-static for unified dispatcher
bool handleIpCommand(const String& originalMessage);  // Made non-static for unified dispatcher
bool handleNextCommand();  // Made non-static for unified dispatcher
bool handleGoCommand(const String& parameter);  // Made non-static for unified dispatcher
static bool handleTextCommand(const String& parameter);
bool handleTextCommandWithColor(const String& parameter, uint8_t fillColor, uint8_t outlineColor, uint8_t bgColor = EL133UF1_WHITE, const String& backgroundImage = "", const String& fontName = "");  // Made non-static for unified dispatcher
bool handleMultiTextCommand(const String& parameter, uint8_t bgColor = EL133UF1_WHITE);  // Made non-static for unified dispatcher
// Wrapper functions for text commands with specific colors (for command registry)
static bool handleTextCommandWhite(const String& param) { return handleTextCommandWithColor(param, EL133UF1_WHITE, EL133UF1_BLACK); }
static bool handleTextCommandYellow(const String& param) { return handleTextCommandWithColor(param, EL133UF1_YELLOW, EL133UF1_BLACK); }
static bool handleTextCommandRed(const String& param) { return handleTextCommandWithColor(param, EL133UF1_RED, EL133UF1_BLACK); }
static bool handleTextCommandBlue(const String& param) { return handleTextCommandWithColor(param, EL133UF1_BLUE, EL133UF1_BLACK); }
static bool handleTextCommandGreen(const String& param) { return handleTextCommandWithColor(param, EL133UF1_GREEN, EL133UF1_BLACK); }
static bool handleTextCommandBlack(const String& param) { return handleTextCommandWithColor(param, EL133UF1_BLACK, EL133UF1_WHITE); }
// Wrapper for handleMultiTextCommand to match registry signature (removes default parameter)
static bool handleMultiTextCommandWrapper(const String& param) { return handleMultiTextCommand(param); }
// Handler for !ota command (has special hardcoded number check)
// Forward declaration - implementation is after ota_server_task is declared
bool handleOtaCommand(const String& originalMessage);  // Made non-static for unified dispatcher
static bool handleMultiFadeTextCommand(const String& parameter, uint8_t bgColor = EL133UF1_WHITE);
static bool handleGetCommand(const String& parameter);
bool handleVolumeCommand(const String& parameter);  // Made non-static for unified dispatcher
bool handleNewNumberCommand(const String& parameter);  // Made non-static for unified dispatcher
bool handleDelNumberCommand(const String& parameter);  // Made non-static for unified dispatcher
bool handleListNumbersCommand(const String& originalMessage = "");  // Made non-static for unified dispatcher
bool handleShowCommand(const String& parameter);  // Made non-static for unified dispatcher
bool handleSleepIntervalCommand(const String& parameter);  // Made non-static for unified dispatcher
bool handleOAICommand(const String& parameter);  // Made non-static for unified dispatcher
bool handleManageCommand();  // Made non-static for unified dispatcher
static bool startSdBufferedOTA();  // Start SD-buffered OTA web server
static void ota_server_task(void* arg);  // Task wrapper for OTA server
void checkAndNotifyOTAUpdate();  // Check for firmware change and notify via MQTT
void volumeLoadFromNVS();  // Load volume from NVS (called on startup)
void sleepDurationLoadFromNVS();  // Load sleep duration from NVS (called on startup)
void sleepDurationSaveToNVS();  // Save sleep duration to NVS
void volumeSaveToNVS();    // Save volume to NVS (called when volume changes)
void mediaIndexLoadFromNVS();  // Load media index from NVS (called on startup)
void mediaIndexSaveToNVS();  // Save media index to NVS
void hourScheduleLoadFromNVS();  // Load hour schedule from NVS (called on startup)
void hourScheduleSaveToNVS();  // Save hour schedule to NVS
bool isHourEnabled(int hour);  // Check if a specific hour (0-23) is enabled for waking (defined after hourScheduleLoadFromNVS)
bool isNumberAllowed(const String& number);  // Check if number is in allowed list
bool addAllowedNumber(const String& number);  // Add number to allowed list in NVS
bool removeAllowedNumber(const String& number);  // Remove number from allowed list in NVS
void numbersLoadFromNVS();  // Load allowed numbers from NVS (called on startup)
// MQTT functions are now in mqtt_handler.h
// WiFi functions moved to wifi_manager.cpp/h module (Priority 1 refactoring)

// Deferred web UI command (for heavy commands that need to run outside MQTT task context)
bool webUICommandPending = false;
String pendingWebUICommand = "";
String lastProcessedCommandId = "";  // ID of the last command that was processed (for completion status)

// ============================================================================
// Command Registry System - MIGRATED TO command_dispatcher.cpp/h
// ============================================================================
// The old command registry system has been replaced with the unified command
// dispatcher in command_dispatcher.cpp/h, which handles commands from all
// sources (MQTT/SMS, Web UI, HTTP API) through a single registry.
//
// Helper function to extract text parameter from command/message
// (kept here for backward compatibility, also used by unified dispatcher)
String extractTextParameterForCommand(const String& command, const String& originalMessage, const char* cmdName) {
    String textToDisplay = "";
    String cmdNameStr = String(cmdName);
    
    // Check if it's JSON format
    if (originalMessage.startsWith("{")) {
        // Parse JSON to extract "text" field (preserving case) using cJSON
        textToDisplay = extractJsonStringField(originalMessage, "text");
    } else {
        // Not JSON - extract parameter from original message (preserving case)
        String lowerMsg = originalMessage;
        lowerMsg.toLowerCase();
        int cmdPos = lowerMsg.indexOf(cmdNameStr);
        if (cmdPos >= 0) {
            int spacePos = originalMessage.indexOf(' ', cmdPos + cmdNameStr.length());
            if (spacePos >= 0) {
                textToDisplay = originalMessage.substring(spacePos + 1);
                textToDisplay.trim();
            }
        }
    }
    
    // Fallback to extracting from command if above failed
    if (textToDisplay.length() == 0) {
        textToDisplay = extractCommandParameter(command);
    }
    
    // Remove command prefix if present (case insensitive)
    textToDisplay.trim();
    String lowerText = textToDisplay;
    lowerText.toLowerCase();
    String prefixToRemove = cmdNameStr + " ";
    if (lowerText.startsWith(prefixToRemove)) {
        textToDisplay = textToDisplay.substring(prefixToRemove.length());
        textToDisplay.trim();
    }
    
    return textToDisplay;
}

// Schedule action types (extensible for future cron-like system)
// Note: Using SCHEDULE_ prefix to avoid conflict with ESP32 HAL GPIO macros (DISABLED/ENABLED)
enum class ScheduleAction {
    SCHEDULE_DISABLED,   // Hour is disabled - sleep until next enabled hour
    SCHEDULE_ENABLED,    // Hour is enabled - proceed with normal operations
    SCHEDULE_NTP_RESYNC  // Special action: resync NTP (e.g., at 30 minutes past hour)
    // Future: CYCLE_MEDIA, PLAY_SOUND, etc.
};

// ============================================================================
// Extracted functions from auto_cycle_task() for better maintainability
// ============================================================================

/**
 * Get schedule action for a given hour and minute
 * Extensible for future cron-like system (different actions at different times)
 * Checks both hour enablement and minute-level scheduled actions
 */
static ScheduleAction getScheduleAction(int hour, int minute) {
    // First check if hour is disabled
    if (!isHourEnabled(hour)) {
        return ScheduleAction::SCHEDULE_DISABLED;
    }
    
    // Check for minute-level scheduled actions
    // NTP resync at 30 minutes past each hour
    if (minute == 30) {
        return ScheduleAction::SCHEDULE_NTP_RESYNC;
    }
    
    // Future: Add more minute-level actions here
    // if (minute == 0) return ScheduleAction::CYCLE_MEDIA;
    // if (minute == 15) return ScheduleAction::PLAY_SOUND;
    
    // Default: hour is enabled, no special action
    return ScheduleAction::SCHEDULE_ENABLED;
}

/**
 * Check and sync time if needed
 * Returns true if time is valid, false otherwise
 * Updates now and tm_utc if time becomes valid
 */
static bool checkAndSyncTime(time_t& now, struct tm& tm_utc, bool& time_ok) {
    // Check if time is valid first (quick check)
    time_ok = false;
    now = time(nullptr);
    if (now > 1577836800) {  // Time is valid
        time_ok = true;
        
        // RTC drift compensation: If we slept for > 45 minutes, we might want to resync
        // BUT only if time appears to have drifted significantly (check after we have valid time)
        // For now, skip NTP sync if time is already valid - RTC drift is usually minimal
        // The periodic NTP resync (every 5 cycles) will handle long-term drift
        if (lastSleepDurationSeconds > 45 * 60) {  // > 45 minutes
            Serial.printf("Long sleep detected (%lu seconds, %.1f minutes), but time is valid - skipping NTP sync\n",
                         (unsigned long)lastSleepDurationSeconds, lastSleepDurationSeconds / 60.0f);
            Serial.println("(Periodic NTP resync every 5 cycles will handle long-term drift)");
        }
    } else {
        // Time is invalid - must sync NTP
        // Only try NTP sync if we have WiFi credentials
        // Use a limited timeout to prevent infinite loops
        Serial.println("Time invalid, attempting NTP sync (with timeout)...");
        time_ok = ensureTimeValid(60000);  // 60 second max timeout
        if (!time_ok) {
            // Check if we have WiFi credentials - if we do, this is a network issue, not a config issue
            bool hasCredentials = false;
            {
                NVSGuard guard("wifi", true);  // read-only
                if (guard.isOpen()) {
                    String ssid = guard.get().getString("ssid", "");
                    hasCredentials = (ssid.length() > 0);
                }
                // guard automatically calls end() in destructor
            }
            
            if (hasCredentials) {
                Serial.println("\n========================================");
                Serial.println("WARNING: Time sync failed, but WiFi credentials are configured.");
                Serial.println("This may be a temporary network issue.");
                Serial.println("Continuing with invalid time - will retry on next cycle.");
                Serial.println("========================================");
                // Don't enter config mode - we have credentials, just network issues
                // Continue with invalid time and retry next cycle
                time_ok = false;  // Keep time_ok as false, but don't enter config mode
            } else {
                Serial.println("\n========================================");
                Serial.println("CRITICAL: Time sync failed - WiFi credentials required!");
                Serial.println("========================================");
                Serial.println("Configuration mode needed - exiting task to allow main loop to handle it.");
                Serial.println("The main loop will enter configuration mode.");
                // Set flag and exit task - let main loop handle config mode
                g_config_mode_needed = true;
                vTaskDelete(NULL);  // Delete this task - main loop will handle config
                return false;  // Should never reach here, but satisfy compiler
            }
        }
        // Re-check time after sync attempt
        now = time(nullptr);
    }
    
    // Update tm_utc if time is valid
    if (time_ok && now > 1577836800) {
        gmtime_r(&now, &tm_utc);
    }
    
    return time_ok;
}

/**
 * Check for RTC drift and compensate if needed
 * If we woke early (before target wake time), sleep additional time
 */
static void checkRtcDriftCompensation(time_t now, struct tm& tm_utc, bool time_ok) {
    // RTC drift compensation: Check if we woke early and need to sleep more
    // Only check if we have a target wake time set and slept for > 45 minutes
    if (targetWakeHour != 255 && targetWakeMinute != 255 && lastSleepDurationSeconds > 45 * 60 && time_ok) {
        int currentMinute = tm_utc.tm_min;
        int currentSecond = tm_utc.tm_sec;
        int currentHour = tm_utc.tm_hour;
        
        // Calculate if we're early (before target wake time)
        bool wokeEarly = false;
        int secondsUntilTarget = 0;
        
        // Calculate time difference, handling potential midnight wrap-around
        // For long sleeps (>45 min), wrap-around is unlikely but we handle it
        int hoursDiff = targetWakeHour - currentHour;
        int minutesDiff = targetWakeMinute - currentMinute;
        
        // Handle midnight wrap-around (e.g., 23:xx -> 00:xx)
        if (hoursDiff < 0) {
            hoursDiff += 24;
        }
        if (hoursDiff == 0 && minutesDiff < 0) {
            // Same hour but target minute already passed - we're late, not early
            wokeEarly = false;
        } else if (hoursDiff == 0 && minutesDiff == 0 && currentSecond < 30) {
            // We're at the target minute but less than 30 seconds in - consider this on time
            // (allow some tolerance for NTP sync delay)
            wokeEarly = false;
        } else if (hoursDiff == 0 && minutesDiff > 0 && minutesDiff <= 2) {
            // We're within 2 minutes of target - likely on time (RTC drift within tolerance)
            wokeEarly = false;
        } else if (hoursDiff > 0 || (hoursDiff == 0 && minutesDiff > 0)) {
            // We're before the target time
            secondsUntilTarget = hoursDiff * 3600 + minutesDiff * 60 - currentSecond;
            wokeEarly = true;
        }
        
        if (wokeEarly && secondsUntilTarget > 10) {  // Only sleep more if > 10 seconds early
            Serial.printf("Woke early: Current time %02d:%02d:%02d, target %02d:%02d:00 (slept %lu seconds)\n",
                         currentHour, currentMinute, currentSecond, targetWakeHour, targetWakeMinute,
                         (unsigned long)lastSleepDurationSeconds);
            Serial.printf("Sleeping additional %d seconds to reach target wake time...\n", secondsUntilTarget);
            
            // Clear target wake time (we'll set it again when we sleep)
            targetWakeHour = 255;
            targetWakeMinute = 255;
            
            // Sleep for the remaining time
            sleepNowSeconds(secondsUntilTarget);
            return;  // Never returns
        } else if (wokeEarly) {
            Serial.printf("Woke slightly early (%d seconds) - within tolerance, continuing\n", secondsUntilTarget);
        }
        
        // Clear target wake time after checking (we've handled it)
        targetWakeHour = 255;
        targetWakeMinute = 255;
    }
}

/**
 * Perform NTP resync if needed (called when schedule action is SCHEDULE_NTP_RESYNC)
 * This handles the 30 minutes past hour NTP resync
 */
static void doNtpResyncIfNeeded(bool time_ok) {
    time_t now_check = time(nullptr);
    
    if (now_check > 1577836800) {
        Serial.println("\n=== Periodic NTP Resync (30 minutes past hour) ===");
    } else {
        Serial.println("\n=== NTP Resync (time invalid) ===");
    }
    
    // Load WiFi credentials
    String ssid = "";
    String psk = "";
    {
        NVSGuard guard("wifi", true);  // read-only
        if (guard.isOpen()) {
            ssid = guard.get().getString("ssid", "");
            psk = guard.get().getString("psk", "");
        }
        // guard automatically calls end() in destructor
    }
    
    if (ssid.length() > 0) {
        // Load credentials into global variables for wifiConnectPersistent
        strncpy(wifiSSID, ssid.c_str(), sizeof(wifiSSID) - 1);
        strncpy(wifiPSK, psk.c_str(), sizeof(wifiPSK) - 1);
        
        // Use persistent WiFi connection (NTP sync is important, so be persistent)
        if (wifiConnectPersistent(8, 30000, true)) {  // 8 retries, 30s per attempt, required
            Serial.println("WiFi connected");
            
            // Sync time via NTP using centralized function (with much more persistent retries)
            // Use longer timeout for periodic resyncs (5 minutes total)
            bool ntpSynced = performNtpSync(300000);  // 5 minute timeout (20 retries * 60s = up to 20 minutes)
            if (!ntpSynced) {
                Serial.println("WARNING: NTP sync failed after all retries, but continuing...");
            } else {
                time_t now = time(nullptr);
                struct tm tm_utc;
                gmtime_r(&now, &tm_utc);
                char buf[32];
                strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", &tm_utc);
                Serial.printf("Time synced: %s\n", buf);
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
}

/**
 * Handle disabled hour - sleep until next enabled hour
 */
static void handleDisabledHour(int currentHour, struct tm& tm_utc) {
    Serial.printf("Hour %02d is DISABLED - sleeping until next enabled hour\n", currentHour);
    
    // Find next enabled hour
    int nextEnabledHour = -1;
    for (int i = 1; i <= 24; i++) {
        int checkHour = (currentHour + i) % 24;
        if (isHourEnabled(checkHour)) {
            nextEnabledHour = checkHour;
            break;
        }
    }
    
    if (nextEnabledHour < 0) {
        // All hours disabled (shouldn't happen, but handle gracefully)
        Serial.println("WARNING: All hours disabled - sleeping for 1 hour");
        sleepNowSeconds(3600);
        return;
    }
    
    // Calculate seconds until next enabled hour
    int hoursToAdd = 0;
    if (nextEnabledHour > currentHour) {
        hoursToAdd = nextEnabledHour - currentHour;
    } else {
        // Wraps around midnight
        hoursToAdd = (24 - currentHour) + nextEnabledHour;
    }
    
    // Calculate sleep duration: seconds remaining in current hour + full hours until next enabled hour
    uint32_t secondsRemainingInHour = (60 - tm_utc.tm_min) * 60 - tm_utc.tm_sec;
    uint32_t sleepSeconds = secondsRemainingInHour + (hoursToAdd - 1) * 3600;
    
    Serial.printf("Sleeping %lu seconds until hour %02d:00 (next enabled hour)\n", 
                 (unsigned long)sleepSeconds, nextEnabledHour);
    
    // Store sleep duration and target wake time in RTC memory for drift compensation
    lastSleepDurationSeconds = sleepSeconds;
    targetWakeHour = (uint8_t)nextEnabledHour;
    targetWakeMinute = 0;
    
    sleepNowSeconds(sleepSeconds);
    // Never returns
}

// REMOVED: loadMediaForDisplay() - functionality now in displayMediaWithOverlay()
// This function was duplicating logic that's already in display_manager.cpp

// ============================================================================
// Parallel Task Structures for Top-of-Hour Optimization
// ============================================================================

// Task parameters for WiFi/MQTT thumbnail publish (runs on Core 1 during display refresh)
struct WifiMqttThumbnailParams {
    SemaphoreHandle_t doneSemaphore;
    bool success;
    String commandToProcess;
    String originalMessageForCommand;
};

// Parallel task: WiFi/MQTT thumbnail publish on Core 1 during display refresh
static void wifiMqttThumbnailTask(void* param) {
    WifiMqttThumbnailParams* p = (WifiMqttThumbnailParams*)param;
    p->success = false;
    p->commandToProcess = "";
    p->originalMessageForCommand = "";
    
    Serial.println("[Core 1] Starting WiFi/MQTT thumbnail publish task (parallel with display refresh)");
    
    // Load WiFi and MQTT credentials
    if (wifiLoadCredentials()) {
        mqttLoadConfig();
        
        // Connect to WiFi if needed (WiFi should already be connected, but check anyway)
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[Core 1] WiFi not connected, attempting connection for thumbnail publish...");
            if (!wifiConnectPersistent(5, 20000, false)) {  // 5 retries, 20s per attempt, not required
                Serial.println("[Core 1] WARNING: WiFi connection failed for thumbnail publish (continuing anyway)");
            }
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("[Core 1] WiFi connected for thumbnail publish");
            
            // Connect to MQTT
            {
                MQTTGuard guard;
                if (guard.isConnected()) {
                    // Publish thumbnail (will load from SD if available, otherwise regenerate from framebuffer)
                    Serial.println("[Core 1] Publishing thumbnail to MQTT...");
                    publishMQTTThumbnail();
                    
                    // Check for SMS bridge commands after thumbnail publish (top-of-hour MQTT check)
                    Serial.println("[Core 1] Checking for SMS bridge commands (top-of-hour)");
                    if (mqttCheckMessages(100)) {
                        String msg = mqttGetLastMessage();
                        Serial.printf("[Core 1] New command received: %s\n", msg.c_str());
                        
                        // Extract command (but don't process yet - disconnect first)
                        String command = extractCommandFromMessage(msg);
                        if (command.length() > 0) {
                            p->commandToProcess = command;  // Store for processing after disconnect
                            p->originalMessageForCommand = msg;  // Store original message for commands that need it
                        }
                        
                        // Message already processed and cleared in event handler
                        // The blank retained message was published in the event handler
                        // Note: delay(200) for blank message publish is handled by guard destructor
                    } else {
                        Serial.println("[Core 1] No retained messages");
                    }
                    // guard automatically handles delays and disconnect in destructor
                    Serial.println("[Core 1] MQTT disconnected");
                    p->success = true;
                } else {
                    Serial.println("[Core 1] WARNING: Failed to connect to MQTT for thumbnail publish");
                }
            }
            
            // WiFi stays connected - NO DISCONNECT
        } else {
            Serial.println("[Core 1] WARNING: WiFi not connected for thumbnail publish (continuing anyway)");
        }
    } else {
        Serial.println("[Core 1] WARNING: WiFi credentials not available, skipping thumbnail publish");
    }
    
    // Signal completion
    xSemaphoreGive(p->doneSemaphore);
    vTaskDelete(NULL);
}

// Forward declaration

// REMOVED: doDisplayUpdateCycle() - replaced with displayMediaWithOverlay()
// The unified function handles all display operations consistently across all command sources.
// Thumbnail publishing can be added to displayMediaWithOverlay() if needed.

// Forward declarations
static void doMqttCheckCycle(bool time_ok, bool isTopOfHour, int currentHour);

static void auto_cycle_task(void* arg) {
    (void)arg;
    g_cycle_count++;
    Serial.printf("\n=== Cycle #%lu ===\n", (unsigned long)g_cycle_count);

    // Yield to watchdog at start of task
    vTaskDelay(1);

    // Increment NTP sync counter
    ntpSyncCounter++;
    
    // Check and sync time if needed
    bool time_ok = false;
    time_t now = time(nullptr);
    struct tm tm_utc;
    
    // Yield before potentially long-running time sync
    vTaskDelay(1);
    
    if (!checkAndSyncTime(now, tm_utc, time_ok)) {
        return;  // checkAndSyncTime may have deleted the task or entered config mode
    }
    
    // Yield after time sync
    vTaskDelay(1);
    
    // Get current time to check if it's the top of the hour
    bool isTopOfHour = (tm_utc.tm_min == 0);
    int currentHour = tm_utc.tm_hour;
    int currentMinute = tm_utc.tm_min;
    
    // Check for RTC drift compensation (may sleep and return)
    checkRtcDriftCompensation(now, tm_utc, time_ok);
    
    // Yield before periodic operations
    vTaskDelay(1);
    
    // Periodic NTP resync every 5 wakes (to handle long-term drift)
    if (ntpSyncCounter > 0 && ntpSyncCounter % 5 == 0) {
        Serial.printf("\n=== Periodic NTP Resync (every 5 wakes, counter=%lu) ===\n", (unsigned long)ntpSyncCounter);
        
        // Load WiFi credentials
        if (wifiLoadCredentials()) {
            // Yield before WiFi connection
            vTaskDelay(1);
            
            // Connect to WiFi (required for NTP sync)
            if (wifiConnectPersistent(10, 30000, true)) {  // 10 retries, 30s per attempt, required
                Serial.println("WiFi connected for periodic NTP resync");
                
                // Yield before long-running NTP sync
                vTaskDelay(1);
                
                // Force NTP sync using centralized function (with very persistent retries)
                bool ntpSynced = performNtpSync(300000);  // 5 minute timeout
                
                // Yield after NTP sync
                vTaskDelay(1);
                if (ntpSynced) {
                    // Update time variables after NTP sync
                    now = time(nullptr);
                    if (now > 1577836800) {
                        gmtime_r(&now, &tm_utc);
                        isTopOfHour = (tm_utc.tm_min == 0);
                        currentHour = tm_utc.tm_hour;
                        currentMinute = tm_utc.tm_min;
                        time_ok = true;
                        Serial.println("Periodic NTP resync successful");
                    }
                } else {
                    Serial.println("WARNING: Periodic NTP resync failed after all retries, but continuing...");
                }
                
                // Don't disconnect WiFi here - might be needed for MQTT checks
            } else {
                Serial.println("WARNING: WiFi connection failed for periodic NTP resync");
            }
        } else {
            Serial.println("WARNING: No WiFi credentials for periodic NTP resync");
        }
        Serial.println("==========================================\n");
    }
    
    Serial.printf("Current time: %02d:%02d:%02d (isTopOfHour: %s, hour enabled: %s)\n", 
                  tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec,
                  isTopOfHour ? "YES" : "NO",
                  isHourEnabled(currentHour) ? "YES" : "NO");
    
    // COLD BOOT: Always do WiFi->NTP->MQTT check on first boot (not deep sleep wake)
    // This ensures we can receive !manage commands to change the hour schedule
    // Don't clear g_is_cold_boot yet - we need it later to publish media mappings
    if (g_is_cold_boot) {
        Serial.println("=== COLD BOOT: Always doing MQTT check (ignoring hour schedule) ===");
        
        // ALWAYS sync NTP on cold boot (regardless of time validity)
        // This ensures we have accurate time even if RTC drifted during power-off
        // We need to load WiFi credentials and connect before we can sync NTP
        Serial.println("Cold boot - syncing NTP (always sync on cold boot)...");
        
        // Load WiFi credentials first (required for NTP sync)
        if (!wifiLoadCredentials()) {
            Serial.println("\n>>> CRITICAL: WiFi credentials not available <<<");
            Serial.println("Cannot sync NTP without WiFi credentials.");
            Serial.println("Configuration mode needed - exiting task to allow main loop to handle it.");
            g_config_mode_needed = true;
            vTaskDelete(NULL);  // Delete this task - main loop will handle config
            return;  // Should never reach here, but satisfy compiler
        }
        
        // Connect to WiFi (required for NTP sync)
        // Note: We don't use WiFiGuard here because WiFi needs to stay connected
        // for doMqttCheckCycle() to use. The wifiConnectPersistent() function
        // will reuse an existing connection if WiFi is already connected.
        
        // Yield before WiFi connection
        vTaskDelay(1);
        
        if (wifiConnectPersistent(10, 30000, true)) {  // 10 retries, 30s per attempt, required
            Serial.println("WiFi connected for NTP sync");
            
            // Yield before NTP sync
            vTaskDelay(1);
            
            // Force NTP sync regardless of time validity (always sync on cold boot)
            // Use centralized NTP sync function
            bool ntpSynced = performNtpSync(60000);  // 60 second timeout
            
            // Yield after NTP sync
            vTaskDelay(1);
            if (ntpSynced) {
                time_ok = true;
            } else {
                Serial.println("WARNING: NTP sync failed after all retries on cold boot, but continuing...");
                time_ok = false;
            }
            
            // Don't disconnect WiFi here - doMqttCheckCycle() will use it
            // wifiConnectPersistent() in doMqttCheckCycle() will detect existing connection
        } else {
            Serial.println("WARNING: WiFi connection failed on cold boot - cannot sync NTP");
            time_ok = false;
        }
        
        // Update time variables after NTP sync
        now = time(nullptr);
        if (now > 1577836800) {
            gmtime_r(&now, &tm_utc);
            isTopOfHour = (tm_utc.tm_min == 0);
            currentHour = tm_utc.tm_hour;
            currentMinute = tm_utc.tm_min;
        }
        
        // Yield before MQTT check
        vTaskDelay(1);
        
        // Force MQTT check regardless of hour schedule or top-of-hour status
        // This gives us a window to receive !manage commands
        doMqttCheckCycle(time_ok, isTopOfHour, currentHour);
        
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
    
    // Check schedule action (extensible for future cron-like system)
    ScheduleAction action = getScheduleAction(currentHour, currentMinute);
    if (action == ScheduleAction::SCHEDULE_DISABLED) {
        handleDisabledHour(currentHour, tm_utc);
        return;  // Never returns
    }
    
    // Handle scheduled actions (e.g., NTP resync at 30 minutes past)
    if (action == ScheduleAction::SCHEDULE_NTP_RESYNC) {
        doNtpResyncIfNeeded(time_ok);
        // After NTP resync, continue with normal cycle (MQTT check or display update)
        // Update time variables after potential NTP sync
        now = time(nullptr);
        if (now > 1577836800) {
            gmtime_r(&now, &tm_utc);
            isTopOfHour = (tm_utc.tm_min == 0);
            currentHour = tm_utc.tm_hour;
            currentMinute = tm_utc.tm_min;
            time_ok = true;
        }
    }
    
    // If NOT top of hour, do MQTT check instead of display update
    if (!isTopOfHour && time_ok) {
        // Yield before MQTT check
        vTaskDelay(1);
        
        doMqttCheckCycle(time_ok, isTopOfHour, currentHour);
        
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
    
    // Yield before display update cycle
    vTaskDelay(1);
    
    // Top of hour: use unified display function (same as !go and !next commands)
    // This ensures consistent behavior across all command sources
    bool ok = displayMediaWithOverlay(-1, 100);  // -1 = sequential next, 100 = keepout margin
    if (!ok) {
        Serial.println("ERROR: Failed to display media at top of hour");
        // Sleep anyway and retry next cycle
        if (time_ok) {
            sleepUntilNextMinuteOrFallback(kCycleSleepSeconds);
        } else {
            sleepNowSeconds(kCycleSleepSeconds);
        }
        return;  // Exit task
    }
    
    // Sleep until next enabled hour (or next minute if time invalid)
    if (time_ok) {
        sleepUntilNextMinuteOrFallback(kCycleSleepSeconds);
    } else {
        sleepNowSeconds(kCycleSleepSeconds);
    }
    // Never returns - device enters deep sleep
}

/**
 * Perform MQTT check cycle (non-top-of-hour)
 * Handles WiFi connection, MQTT message checking, command processing, and status publishing
 */
static void doMqttCheckCycle(bool time_ok, bool isTopOfHour, int currentHour) {
    (void)isTopOfHour;  // Not used in MQTT check cycle
    (void)currentHour;  // Not used in MQTT check cycle
    
    Serial.println("=== MQTT Check Cycle (not top of hour) ===");
    
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
    
    // OPTIMIZATION: Start status JSON preparation on Core 1 in parallel with WiFi connection
    // This saves 150-300ms by doing CPU-bound work (JSON building + encryption) while Core 0 waits for network I/O
    Serial.println("Starting parallel status preparation on Core 1...");
    bool statusPrepStarted = prepareStatusJsonParallel();
    
    // Connect to WiFi - REQUIRED for MQTT, so be persistent (keep robust retry logic)
    // While Core 0 is connecting to WiFi (network I/O, blocking), Core 1 is preparing status JSON (CPU-bound)
    // WiFi stays connected from boot until deep sleep - no WiFiGuard needed
    {
        // Connect WiFi if not already connected
        if (WiFi.status() != WL_CONNECTED) {
            if (!wifiConnectPersistent(10, 30000, true)) {  // 10 retries, 30s per attempt, required
                Serial.println("ERROR: WiFi connection failed - this should not happen (required mode)");
                return;  // Can't proceed without WiFi
            }
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            // WiFi connected - check for OTA update notification ONLY on cold boot
            // This saves 2-5 seconds on every non-top-of-hour cycle
            if (g_is_cold_boot) {
                Serial.println("\n=== Checking for OTA firmware update (cold boot) ===");
                checkAndNotifyOTAUpdate();
                Serial.println("=== OTA check complete ===\n");
            }
            
            // WiFi connected - proceed with MQTT
            // Connect to MQTT and check for retained messages
            if (mqttConnect()) {
                // Wait for subscription and any retained messages - OPTIMIZED: reduced to 500ms
                // Retained messages should arrive almost immediately after subscription completes
                // Reduced from 1000ms to 500ms - saves 500ms per cycle without impacting functionality
                delay(500);
                
                // Check if we received a retained message (mqttCheckMessages checks connection state internally)
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
                    // Reduced delay - publish completes quickly, 100ms is sufficient
                    delay(100);  // Allow time for blank retained message publish to complete
                } else {
                    // Check if a large message is still being received
                    // For large messages (e.g., canvas_display), mqttCheckMessages(100) may return false
                    // even though the message is still downloading in the background
                    if (mqttIsMessageInProgress()) {
                        // Get message progress info (need to access static variables through helper)
                        // Wait for message to complete (with timeout based on message size)
                        // Estimate: ~100KB/s transfer rate, add 50% margin
                        uint32_t estimatedTimeMs = 10000;  // Conservative 10 second estimate for large messages
                        uint32_t maxWaitMs = 30000;  // Cap at 30 seconds
                        uint32_t waitStart = millis();
                        
                        Serial.println("Large message in progress, waiting for completion...");
                        
                        while (mqttIsMessageInProgress() && 
                               (millis() - waitStart) < maxWaitMs &&
                               isMqttConnected() && getMqttClient() != nullptr) {
                            delay(100);  // Check every 100ms
                        }
                        
                        if (!mqttIsMessageInProgress()) {
                            Serial.println("Large message completed, processing...");
                            // Message is now complete, check again
                            if (mqttCheckMessages(500)) {  // Give it 500ms to process
                                String msg = mqttGetLastMessage();
                                Serial.printf("New command received (after wait): %s\n", msg.c_str());
                                
                                String command = extractCommandFromMessage(msg);
                                if (command.length() > 0) {
                                    commandToProcess = command;
                                    originalMessageForCommand = msg;
                                }
                                delay(100);  // Allow time for blank retained message publish
                            }
                        } else {
                            Serial.println("WARNING: Large message timeout, disconnecting anyway");
                        }
                    } else {
                        Serial.println("No retained messages");
                    }
                }
                
                // PRIORITY 1 OPTIMIZATION: Publish status BEFORE processing commands while MQTT is still connected
                // This saves 1-2 seconds by avoiding wasteful disconnect/reconnect cycle
                // Status is already prepared on Core 1 in parallel with WiFi/MQTT connection
                Serial.println("Publishing status while MQTT is connected (before processing commands)");
                if (statusPrepStarted) {
                    // Use pre-prepared status (built on Core 1 in parallel with WiFi/MQTT connection)
                    if (!publishPreparedStatus()) {
                        // Fallback to normal publish if parallel preparation failed
                        Serial.println("WARNING: Parallel status publish failed, falling back to normal publish");
                        publishMQTTStatus();
                    }
                } else {
                    // Fallback if parallel preparation wasn't started
                    publishMQTTStatus();
                }
                
                // On cold boot, also publish media mappings (ONLY on cold boot, not deep sleep wake)
                if (g_is_cold_boot) {
                    Serial.println("=== COLD BOOT: Publishing media mappings ===");
                    publishMQTTMediaMappings(true);  // Wait for Core 1 to finish before disconnecting MQTT
                    g_is_cold_boot = false;  // Clear flag after publishing media mappings
                }
                
                // Check if we have commands to process
                bool hasCommands = (commandToProcess.length() > 0) || 
                                   (webUICommandPending && pendingWebUICommand.length() > 0);
                
                if (hasCommands) {
                    // Have commands - disconnect AFTER publishing status to avoid holding connection during long operations
                    // This prevents connection issues during long-running commands (like display updates)
                    // Status is already published, so we don't need to reconnect
                    Serial.println("Commands detected - disconnecting MQTT before processing (status already published)");
                    mqttDisconnect();
                    delay(50);
                        
                    // Process SMS bridge commands FIRST (before deferred web UI commands)
                    // This ensures critical commands like !ota can be executed even if web UI commands cause boot loops
                    if (commandToProcess.length() > 0) {
                        Serial.println("Processing SMS bridge command (priority) after MQTT disconnect");
                        // handleMqttCommand returns false for both "command not recognized" and "command failed"
                        // We'll handle the "unknown command" message inside handleMqttCommand itself
                        // to distinguish between unrecognized commands and command execution failures
                        handleMqttCommand(commandToProcess, originalMessageForCommand);
                    }
                    
                    // Process deferred web UI command (if any) AFTER SMS bridge commands
                    // This prevents stack overflow in the MQTT task context for heavy commands
                    // But we process it after SMS bridge commands so critical commands like !ota can run first
                    if (webUICommandPending && pendingWebUICommand.length() > 0) {
                        Serial.println("Processing deferred web UI command after MQTT disconnect");
                        
                        // Process the command (handleWebInterfaceCommand handles decryption and completion publishing)
                        // Completion publishing is now handled by the command dispatcher, so we don't need to do it here
                        bool success = handleWebInterfaceCommand(pendingWebUICommand);
                        
                        // Clear the pending command flag
                        webUICommandPending = false;
                        pendingWebUICommand = "";
                        
                        // Wait a bit to ensure the completion message is sent
                        delay(2000);  // 2 seconds should be enough for completion message to be sent
                    }
                    // Status already published above, no need to reconnect
                } else {
                    // No commands - just disconnect (status already published)
                    Serial.println("No commands - disconnecting MQTT (status already published)");
                    mqttDisconnect();
                    delay(50);
                }
                
                // Check if OTA was requested before sleeping
                checkAndStartOTA();
            }
            
            // Keep WiFi connected - will only disconnect before deep sleep
            Serial.println("Keeping WiFi connected (will disconnect only before deep sleep)");
        }
    }  // WiFi stays connected - will only disconnect before deep sleep
}

// ============================================================================
// WiFi Functions
// ============================================================================

// WiFi credentials already declared above (before auto_cycle_task)

// ============================================================================
// MQTT Functions
// ============================================================================

// MQTT configuration - hardcoded
#define MQTT_BROKER_HOSTNAME "mqtt.flespi.io"  // Change this to your MQTT broker

// ============================================================================
// MQTT Functions - Moved to mqtt_handler.cpp/h
// ============================================================================
// All MQTT functionality has been extracted to mqtt_handler module
// Use functions from mqtt_handler.h to interact with MQTT



/**
 * Check if firmware has changed after boot and notify user via MQTT
 * Compares compile date/time of current firmware with stored value in NVS
 * If different, sends success notification confirming new firmware successfully booted
 */
void checkAndNotifyOTAUpdate() {
    // Get current running partition's app description
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running == nullptr) {
        Serial.println("WARNING: Cannot get running partition for OTA check");
        return;
    }
    
    esp_app_desc_t current_app_info;
    esp_err_t err = esp_ota_get_partition_description(running, &current_app_info);
    if (err != ESP_OK) {
        Serial.printf("WARNING: Cannot read app description: %s\n", esp_err_to_name(err));
        return;
    }
    
    // Create unique identifier from compile date + time + version + first 8 chars of SHA256
    // Include SHA256 to ensure uniqueness even if compile time is identical
    char sha256_str[17] = {0};
    for (int i = 0; i < 8; i++) {
        sprintf(sha256_str + i*2, "%02x", current_app_info.app_elf_sha256[i]);
    }
    String currentBuildId = String(current_app_info.date) + " " + String(current_app_info.time) + " v" + String(current_app_info.version) + " sha:" + String(sha256_str);
    
    // Read stored build ID from NVS
    // Try read-write mode first (will create namespace if it doesn't exist)
    // Keep guard open for entire function to ensure we can update build_id if needed
    NVSGuard guard(otaPrefs, "ota", false);  // read-write
    if (!guard.isOpen()) {
        Serial.println("WARNING: Cannot open NVS for OTA version check");
        return;
    }
    
    String storedBuildId = guard.get().getString("build_id", "");
    
    // Debug output
    Serial.printf("Current build ID: '%s'\n", currentBuildId.c_str());
    Serial.printf("Stored build ID:  '%s'\n", storedBuildId.c_str());
    
    // Compare build IDs
    if (storedBuildId.length() == 0) {
        // First boot or NVS was cleared - store current build ID but don't notify
        Serial.println("First boot detected (no stored build ID) - storing current firmware info");
        Serial.printf("Current firmware: %s %s (build: %s)\n", 
                     current_app_info.project_name, current_app_info.version, currentBuildId.c_str());
        guard.get().putString("build_id", currentBuildId);
        // guard automatically calls end() in destructor
        return;
    }
    
    // Debug output to see what we're comparing
    Serial.printf("Current build ID: '%s'\n", currentBuildId.c_str());
    Serial.printf("Stored build ID:  '%s'\n", storedBuildId.c_str());
    Serial.printf("Build IDs match: %s\n", (currentBuildId == storedBuildId) ? "YES" : "NO");
    
    if (currentBuildId != storedBuildId) {
        // Firmware changed! New firmware successfully booted - notify user
        Serial.println("\n========================================");
        Serial.println("NEW FIRMWARE SUCCESSFULLY BOOTED!");
        Serial.println("========================================");
        Serial.printf("Old firmware: %s\n", storedBuildId.c_str());
        Serial.printf("New firmware: %s\n", currentBuildId.c_str());
        Serial.printf("Project: %s, Version: %s\n", current_app_info.project_name, current_app_info.version);
        Serial.println("========================================\n");
        

        // Rotate log file when firmware changes (archive old, create new)
        logRotate();
        logPrintf("=== Firmware changed ===\n");
        logPrintf("Old: %s\n", storedBuildId.c_str());
        logPrintf("New: %s\n", currentBuildId.c_str());
        logPrintf("Project: %s, Version: %s\n", current_app_info.project_name, current_app_info.version);
        logFlush();
        
        // Update stored build ID
        guard.get().putString("build_id", currentBuildId);
        
        // Check if OTA was triggered via MQTT (only send notification in that case)
        bool mqttTriggered = guard.get().getBool("mqtt_triggered", false);
        guard.get().putBool("mqtt_triggered", false);  // Clear the flag
        
        // Only send notification if OTA was triggered via MQTT (!ota command)
        if (!mqttTriggered) {
            Serial.println("OTA was triggered via 'o' key (debug) - skipping MQTT notification");
            return;
        }
        
        Serial.println("OTA was triggered via MQTT - sending notification...");
        
        // Connect to MQTT to send success notification
        if (!mqttConnect()) {
            Serial.println("WARNING: Cannot connect to MQTT for OTA success notification");
            return;
        }
        
        // Wait for connection to be established - OPTIMIZED: reduced from 1000ms to 500ms
        delay(500);
        
        // Build success message (URL-encoded form format)
        // Use char buffer instead of String to avoid heap fragmentation
        // URL-encode the build date/time (replace spaces with +)
        char encodedBuild[64];
        strncpy(encodedBuild, currentBuildId.c_str(), sizeof(encodedBuild) - 1);
        encodedBuild[sizeof(encodedBuild) - 1] = '\0';
        for (size_t i = 0; i < strlen(encodedBuild); i++) {
            if (encodedBuild[i] == ' ') {
                encodedBuild[i] = '+';
            }
        }
        
        char formResponse[256];  // Fixed-size buffer: "To=" (3) + number (15) + "&From=+447401492609" (20) + "&Body=OTA+update+successful%21+Firmware+" (40) + version (max 20) + build (max 64) + "%29+is+now+running." (20) = ~182 bytes
        snprintf(formResponse, sizeof(formResponse), "To=+447816969344&From=+447401492609&Body=OTA+update+successful%%21+Firmware+%s+%%28%s%%29+is+now+running.", 
                 current_app_info.version, encodedBuild);
        
        // Publish notification
        esp_mqtt_client_handle_t client = getMqttClient();
        const char* topic = getMqttTopicPublish();
        if (client != nullptr && topic != nullptr && strlen(topic) > 0) {
            int msg_id = esp_mqtt_client_publish(client, topic, formResponse, strlen(formResponse), 1, 0);
            if (msg_id > 0) {
                Serial.printf("Published OTA success notification to %s (msg_id: %d)\n", topic, msg_id);
                delay(200);  // OPTIMIZED: reduced from 500ms to 200ms - give it a moment to send
            } else {
                Serial.println("ERROR: Failed to publish OTA success notification");
            }
        } else {
            Serial.println("ERROR: MQTT client not available for OTA notification");
        }
        
        // Disconnect after publishing
        mqttDisconnect();
        delay(100);  // OPTIMIZED: reduced from 200ms to 100ms
    } else {
        Serial.printf("Firmware unchanged: %s\n", currentBuildId.c_str());
    }

}

// ============================================================================
// MQTT Command Handling
// ============================================================================

/**
 * Extract command text from MQTT message
 * Handles both plain text and JSON messages
 * Returns lowercase, trimmed command string
 */
String extractCommandFromMessage(const String& msg) {
    String command = msg;
    command.toLowerCase();
    command.trim();
    
    // If message is JSON, try to extract "text" field
    if (command.startsWith("{")) {
        String textField = extractJsonStringField(msg, "text");
        if (textField.length() > 0) {
            command = textField;
            command.toLowerCase();
            command.trim();
        }
    }
    
    return command;
}

/**
 * Extract parameter from command (e.g., "!go 5" -> "5")
 * Returns the parameter string, or empty string if no parameter
 */
String extractCommandParameter(const String& command) {  // Made non-static for unified dispatcher
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
    // Use the consolidated JSON parsing utility
    return extractJsonStringField(msg, "from");
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
// Return values:
//   true  = command recognized and executed successfully
//   false = command recognized but execution failed (error messages already printed)
//   The function prints "Unknown command" itself if the command is not recognized
bool handleMqttCommand(const String& command, const String& originalMessage) {
    // Check if the sender number is allowed (all commands require this, including !newno)
    // The hardcoded number +447816969344 is always allowed, so it can add numbers without locking us out
    String senderNumber = extractFromFieldFromMessage(originalMessage);
    if (senderNumber.length() == 0) {
        Serial.println("ERROR: Could not extract sender number from message - command rejected");
        return false;  // Command rejected due to validation failure (not unknown)
    }
    
    Serial.printf("Command from number: %s\n", senderNumber.c_str());
    
    // Special commands with complex logic (not in unified registry yet)
    if (command.startsWith("!get")) {
        // Extract the "text" field from JSON message first, then extract URL from that
        // This handles cases where the command is in a JSON payload
        String textContent = "";
        
        // Check if original message is JSON
        if (originalMessage.startsWith("{")) {
            // Parse JSON to extract "text" field using cJSON
            textContent = extractJsonStringField(originalMessage, "text");
        } else {
            // Not JSON - use original message directly
            textContent = originalMessage;
        }
        
        // Now extract the URL parameter from the text content (everything after "!get ")
        String param = "";
        String lowerText = textContent;
        lowerText.toLowerCase();
        int cmdPos = lowerText.indexOf("!get");
        if (cmdPos >= 0) {
            int spacePos = textContent.indexOf(' ', cmdPos + 4);
            if (spacePos >= 0) {
                param = textContent.substring(spacePos + 1);
                param.trim();
                // If there's a quote or comma at the end (from JSON), remove it
                if (param.endsWith("\"") || param.endsWith(",") || param.endsWith("}")) {
                    // Find the actual end of the parameter (before any JSON syntax)
                    int endPos = param.length() - 1;
                    while (endPos > 0 && (param.charAt(endPos) == '"' || param.charAt(endPos) == ',' || 
                                          param.charAt(endPos) == '}' || param.charAt(endPos) == ' ')) {
                        endPos--;
                    }
                    param = param.substring(0, endPos + 1);
                    param.trim();
                }
            }
        }
        
        // Fallback to extracting from command if above failed
        if (param.length() == 0) {
            param = extractCommandParameter(command);
        }
        
        return handleGetCommand(param);
    }
    
    // Handle color text commands (special prefix matching)
    if (command.startsWith("!yellow_text")) {
        String text = extractTextParameterForCommand(command, originalMessage, "!yellow_text");
        return handleTextCommandWithColor(text, EL133UF1_YELLOW, EL133UF1_BLACK);
    }
    if (command.startsWith("!red_text")) {
        String text = extractTextParameterForCommand(command, originalMessage, "!red_text");
        return handleTextCommandWithColor(text, EL133UF1_RED, EL133UF1_BLACK);
    }
    if (command.startsWith("!blue_text")) {
        String text = extractTextParameterForCommand(command, originalMessage, "!blue_text");
        return handleTextCommandWithColor(text, EL133UF1_BLUE, EL133UF1_BLACK);
    }
    if (command.startsWith("!green_text")) {
        String text = extractTextParameterForCommand(command, originalMessage, "!green_text");
        return handleTextCommandWithColor(text, EL133UF1_GREEN, EL133UF1_BLACK);
    }
    if (command.startsWith("!black_text")) {
        String text = extractTextParameterForCommand(command, originalMessage, "!black_text");
        return handleTextCommandWithColor(text, EL133UF1_BLACK, EL133UF1_WHITE);
    }
    if (command.startsWith("!multi_text")) {
        String text = extractTextParameterForCommand(command, originalMessage, "!multi_text");
        return handleMultiTextCommand(text);
    }
    
    // Use unified command dispatcher for all other commands
    CommandContext ctx;
    ctx.source = CommandSource::MQTT_SMS;
    ctx.command = command;
    ctx.originalMessage = originalMessage;
    ctx.senderNumber = senderNumber;
    ctx.commandId = "";  // MQTT/SMS commands don't have command IDs
    ctx.requiresAuth = true;
    ctx.shouldPublishCompletion = false;  // MQTT/SMS commands don't publish completion
    
    return dispatchCommand(ctx);
}

/**
 * Decrypt and validate Web UI message (extracts decryption and HMAC validation logic)
 * @param jsonMessage The encrypted or plain JSON message
 * @return Decrypted and validated message ready for processing, or empty string on failure
 */
static String decryptAndValidateWebUIMessage(const String& jsonMessage) {
    // Check if message is encrypted using consolidated JSON parsing utility
    // Note: If this is a deferred command that was already decrypted, it won't have "encrypted" field
    bool isEncrypted = extractJsonBoolField(jsonMessage, "encrypted", false);
    
    String decryptedMessage = jsonMessage;
    
    // If encrypted, decrypt the payload
    if (isEncrypted) {
        Serial.println("Message is encrypted - decrypting...");
        
        // Extract encrypted payload using consolidated JSON parsing utility
        String encryptedPayload = extractJsonStringField(jsonMessage, "payload");
        if (encryptedPayload.length() == 0) {
            Serial.println("ERROR: Encrypted message missing 'payload' field");
            return "";
        }
        
        Serial.printf("  Encrypted payload size: %d bytes (%.1f KB)\n", encryptedPayload.length(), encryptedPayload.length() / 1024.0f);
        
        // Decrypt
        decryptedMessage = decryptMessage(encryptedPayload);
        if (decryptedMessage.length() == 0) {
            Serial.println("ERROR: Failed to decrypt message");
            return "";
        }
        
        Serial.printf("  Decrypted message size: %d bytes (%.1f KB)\n", decryptedMessage.length(), decryptedMessage.length() / 1024.0f);
    } else {
        // New: Handle unencrypted messages with base64 payload
        String base64Payload = extractJsonStringField(jsonMessage, "payload");
        if (base64Payload.length() == 0) {
            Serial.println("ERROR: Unencrypted message missing 'payload' field");
            return "";
        }
        decryptedMessage = base64Decode(base64Payload);
        if (decryptedMessage.length() == 0) {
            Serial.println("ERROR: Failed to base64 decode unencrypted message");
            return "";
        }
        Serial.printf("  Unencrypted message decoded: %d bytes (%.1f KB)\n", decryptedMessage.length(), decryptedMessage.length() / 1024.0f);
    }
    
    // Extract HMAC signature from JSON message (required for authentication)
    // HMAC is computed on the message JSON structure (with or without iv field, depending on encrypted status)
    String messageForHMAC = jsonMessage;
    String providedHMAC = extractJsonStringField(messageForHMAC, "hmac");
    int hmacPos = messageForHMAC.indexOf("\"hmac\"");  // Keep for HMAC removal logic below
    
    // Compute HMAC of the message (excluding the hmac field itself)
    // Simple approach: remove "hmac":"..." from the message
    if (hmacPos >= 0) {
        // Find the end of the hmac value
        int valueStart = messageForHMAC.indexOf('"', messageForHMAC.indexOf(':', hmacPos));
        int valueEnd = messageForHMAC.indexOf('"', valueStart + 1) + 1;
        
        // Check if there's a comma before hmac
        int commaBefore = messageForHMAC.lastIndexOf(',', hmacPos);
        int commaAfter = messageForHMAC.indexOf(',', valueEnd);
        
        if (commaBefore >= 0 && commaAfter >= 0) {
            // Remove hmac field and both commas
            messageForHMAC = messageForHMAC.substring(0, commaBefore) + messageForHMAC.substring(commaAfter + 1);
        } else if (commaBefore >= 0) {
            // Remove hmac field and comma before
            messageForHMAC = messageForHMAC.substring(0, commaBefore) + messageForHMAC.substring(valueEnd);
        } else if (commaAfter >= 0) {
            // Remove hmac field and comma after
            messageForHMAC = messageForHMAC.substring(0, hmacPos) + messageForHMAC.substring(commaAfter + 1);
        } else {
            // hmac is the only field - message becomes empty object
            messageForHMAC = "{}";
        }
        
        // Clean up any formatting issues
        messageForHMAC.trim();
        if (messageForHMAC.length() == 0) {
            messageForHMAC = "{}";
        }
    }
    
    // Validate HMAC (all-or-nothing: if password is set, HMAC is required)
    if (providedHMAC.length() > 0) {
        Serial.printf("Validating HMAC: message length=%d, HMAC provided (length=%d)\n", messageForHMAC.length(), providedHMAC.length());
    } else {
        Serial.println("WARNING: No HMAC provided in message");
    }
    if (!validateWebUIHMAC(messageForHMAC, providedHMAC)) {
        Serial.println("ERROR: Web UI command rejected - HMAC validation failed");
        return "";
    }
    Serial.println("HMAC validation successful - command authenticated");
    
    // Return decoded/decrypted message for processing
    return decryptedMessage;
}

// Canvas handler functions moved to canvas_handler.cpp/h module

/**
 * Handle web interface JSON commands from GitHub Pages MQTT interface
 * Commands are sent as JSON: {"command": "text_display", "text": "...", ...}
 */
bool handleWebInterfaceCommand(const String& jsonMessage) {
    // Decrypt and validate message (extracted to separate function)
    String messageToProcess = decryptAndValidateWebUIMessage(jsonMessage);
    if (messageToProcess.length() == 0) {
        return false;  // Decryption or validation failed
    }
    
    // Debug: verify messageToProcess is set correctly
    bool isEncrypted = extractJsonBoolField(jsonMessage, "encrypted", false);
    Serial.printf("  messageToProcess length: %d, isEncrypted: %d\n", messageToProcess.length(), isEncrypted ? 1 : 0);
    
    // For large messages (like canvas_display with 640KB pixelData), we can't parse the entire JSON
    // So we first check the command type using string operations, then parse only if needed
    String command = extractJsonStringField(messageToProcess, "command");
    if (command.length() > 0) {
        command.toLowerCase();
        Serial.printf("  Extracted command: '%s'\n", command.c_str());
    } else {
        Serial.println("  ERROR: 'command' field not found in message");
    }
    
    if (command.length() == 0) {
        Serial.printf("ERROR: JSON command missing 'command' field\n");
        Serial.printf("  Decrypted message content: '%s' (length: %d)\n", messageToProcess.c_str(), messageToProcess.length());
        Serial.printf("  Message hex dump (first 100 bytes): ");
        for (int i = 0; i < messageToProcess.length() && i < 100; i++) {
            Serial.printf("%02x ", (unsigned char)messageToProcess.charAt(i));
        }
        Serial.println();
        return false;
    }
    
    if (command.length() > 0) {
        Serial.printf("Web interface command: %s\n", command.c_str());
        Serial.printf("  Total JSON message size: %d bytes (%.1f KB)%s\n", 
                     messageToProcess.length(), messageToProcess.length() / 1024.0f,
                     isEncrypted ? " (decrypted)" : "");
        
        // Debug: print first 200 chars of decrypted message to verify it's valid JSON
        if (isEncrypted && messageToProcess.length() > 0) {
            String preview = messageToProcess.substring(0, messageToProcess.length() < 200 ? messageToProcess.length() : 200);
            Serial.printf("  Decrypted message preview: %s\n", preview.c_str());
        }
    }
    
    // For canvas_display commands, extract fields directly without full JSON parsing (too large)
    // Note: These are now processed in the deferred path (after MQTT disconnect) to avoid
    // stack overflow in MQTT task context. The command is deferred in mqttEventHandler.
    // This check is here for backward compatibility but should not be reached for deferred commands.
    if (command == "canvas_display") {
        // This path should not be reached for deferred commands - they're handled in doMqttCheckCycle
        // But if it is reached (non-deferred path), process it directly and publish completion
        String cmdId = extractJsonStringField(messageToProcess, "id");
        bool success = handleCanvasDisplayCommand(messageToProcess);
        
        // Publish completion status (will connect WiFi/MQTT if needed)
        extern void publishMQTTCommandCompletion(const String& commandId, const String& commandName, bool success);
        publishMQTTCommandCompletion(cmdId, command, success);
        
        return success;
    }
    
    // For other commands, parse JSON using cJSON
    // Use messageToProcess (decrypted) not jsonMessage (encrypted)
    cJSON* root = cJSON_Parse(messageToProcess.c_str());
    if (!root) {
        const char* errorPtr = cJSON_GetErrorPtr();
        Serial.printf("ERROR: Failed to parse JSON command: %s\n", errorPtr ? errorPtr : "unknown error");
        Serial.printf("  Attempted to parse messageToProcess (length: %d): %s\n", 
                     messageToProcess.length(), messageToProcess.substring(0, 100).c_str());
        return false;
    }
    
    cJSON* commandItem = cJSON_GetObjectItem(root, "command");
    if (!commandItem || !cJSON_IsString(commandItem)) {
        Serial.println("ERROR: JSON command missing 'command' field");
        Serial.printf("  Parsed JSON (first 200 chars): %s\n", messageToProcess.substring(0, 200).c_str());
        cJSON_Delete(root);
        return false;
    }
    
    command = String(cJSON_GetStringValue(commandItem));
    command.toLowerCase();
    
    Serial.printf("Web interface command: %s (from JSON parse)\n", command.c_str());
    
    // Extract command ID for completion tracking
    String cmdId = extractJsonStringField(messageToProcess, "id");
    
    // Use unified command dispatcher
    CommandContext ctx;
    ctx.source = CommandSource::WEB_UI;
    ctx.command = command;
    ctx.originalMessage = messageToProcess;  // Use decrypted message
    ctx.senderNumber = "";  // Web UI doesn't use phone numbers
    ctx.commandId = cmdId;
    ctx.requiresAuth = true;  // Already validated by decryptAndValidateWebUIMessage
    ctx.shouldPublishCompletion = true;  // Enable completion publishing for WEB_UI
    
    cJSON_Delete(root);  // Clean up cJSON root
    return dispatchCommand(ctx);
}

/**
 * Handle !oai command - generate and display DALL-E 3 image from prompt
 * Format: !oai <prompt>
 * Example: !oai A beautiful landscape with mountains
 */
bool handleOAICommand(const String& parameter) {
    Serial.println("Processing !oai command...");
    
    // Check if prompt was provided
    if (parameter.length() == 0) {
        Serial.println("ERROR: !oai command requires a prompt parameter");
        return false;
    }
    
    Serial.printf("OpenAI prompt: \"%s\"\n", parameter.c_str());
    
    // Ensure display is initialized (may not be on MQTT-only wakes)
    if (display.getBuffer() == nullptr) {
        Serial.println("Display not initialized - initializing now...");
        displaySPI.begin(PIN_SPI_SCK, -1, PIN_SPI_MOSI, -1);
        
        if (!display.begin(PIN_CS0, PIN_CS1, PIN_DC, PIN_RESET, PIN_BUSY)) {
            Serial.println("ERROR: Display initialization failed!");
            return false;
        }
        Serial.println("Display initialized");
        
        // Initialize PNG loader after display is ready
        pngLoader.begin(&display);
        pngLoader.setDithering(true);  // Enable dithering for better image quality
    }
    

    // Ensure WiFi is connected (required for OpenAI API)
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected - connecting now...");
        if (!wifiLoadCredentials()) {
            Serial.println("ERROR: Failed to load WiFi credentials");
            return false;
        }
        
        if (!wifiConnectPersistent(10, 30000, true)) {
            Serial.println("ERROR: Failed to connect to WiFi");
            return false;
        }
    }
    
    // Check if API key is available
    const char* apiKeyOpenAI = OPENAI_API_KEY;
    if (apiKeyOpenAI == nullptr || strlen(apiKeyOpenAI) == 0) {
        Serial.println("ERROR: OpenAI API key not configured");
        return false;
    }
    
    Serial.println("Generating AI image with OpenAI DALL-E 3...");
    
    // Allocate buffer for generated image (free any existing cached image first)
    if (aiImageData != nullptr) {
        free(aiImageData);
        aiImageData = nullptr;
        aiImageLen = 0;
    }
    
    // Configure OpenAI client
    openai.begin(apiKeyOpenAI);
    openai.setModel(DALLE_3);
    openai.setSize(DALLE_1792x1024);  // Landscape format for centering on 1600x1200 display
    openai.setQuality(DALLE_STANDARD);
    
    // Generate image
    uint32_t t0 = millis();
    OpenAIResult result = openai.generate(parameter.c_str(), &aiImageData, &aiImageLen, 120000);  // 2 min timeout
    uint32_t t1 = millis() - t0;
    
    if (result == OPENAI_OK && aiImageData != nullptr && aiImageLen > 0) {
        Serial.printf("AI image generated: %zu bytes in %lu ms\n", aiImageLen, t1);
        
        // Clear display first
        display.clear(EL133UF1_WHITE);
        
        // Draw AI-generated PNG centered on display (1792x1024 on 1600x1200)
        int16_t centerX = (display.width() - 1792) / 2;  // May be negative, but PNG draw handles it
        int16_t centerY = (display.height() - 1024) / 2; // Vertically centered
        
        Serial.printf("Drawing PNG to display at offset (%d, %d)...\n", centerX, centerY);
        PNGResult pngResult = pngLoader.draw(centerX, centerY, aiImageData, aiImageLen);
        
        if (pngResult == PNG_OK) {
            Serial.printf("AI image drawn successfully to buffer at offset (%d, %d)\n", centerX, centerY);
            
            // Verify display buffer is valid before update
            if (display.getBuffer() == nullptr) {
                Serial.println("ERROR: Display buffer is null after drawing - update will fail!");
                free(aiImageData);
                aiImageData = nullptr;
                aiImageLen = 0;
                return false;
            }
            
            // Save image to SD card in separate directory to avoid mixing with media.txt images

            if (sdCardMounted || sdInitDirect(false)) {
                const char* aiDir = "/ai_generated";
                String fatfsDir = "0:" + String(aiDir);
                
                // Create directory if it doesn't exist
                FILINFO fno;
                FRESULT dirRes = f_stat(fatfsDir.c_str(), &fno);
                if (dirRes != FR_OK) {
                    Serial.printf("Creating directory: %s\n", aiDir);
                    dirRes = f_mkdir(fatfsDir.c_str());
                    if (dirRes != FR_OK && dirRes != FR_EXIST) {
                        Serial.printf("WARNING: Failed to create directory %s: %d\n", aiDir, dirRes);
                    }
                }
                
                // Generate unique filename (timestamp-based)
                time_t now = time(nullptr);
                struct tm tm_utc;
                gmtime_r(&now, &tm_utc);
                char filename[64];
                snprintf(filename, sizeof(filename), "%s/oai_%04d%02d%02d_%02d%02d%02d.png",
                        aiDir, tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
                        tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec);
                
                String fatfsPath = "0:" + String(filename);
                Serial.printf("Saving AI image to: %s\n", filename);
                
                FIL file;
                FRESULT fileRes = f_open(&file, fatfsPath.c_str(), FA_WRITE | FA_CREATE_ALWAYS);
                if (fileRes == FR_OK) {
                    UINT bytesWritten = 0;
                    fileRes = f_write(&file, aiImageData, aiImageLen, &bytesWritten);
                    f_close(&file);
                    
                    if (fileRes == FR_OK && bytesWritten == aiImageLen) {
                        Serial.printf("AI image saved successfully: %u bytes\n", bytesWritten);
                    } else {
                        Serial.printf("WARNING: Failed to save AI image completely: wrote %u of %zu bytes\n",
                                     bytesWritten, aiImageLen);
                    }
                } else {
                    Serial.printf("WARNING: Failed to open file for writing: %d\n", fileRes);
                }
            } else {
                Serial.println("WARNING: SD card not available - AI image not saved");
            }

            
            // Update display
            Serial.println("Updating display (e-ink refresh - this will take 20-30 seconds)...");
            Serial.flush();  // Ensure logs are flushed before blocking update
            
            uint32_t updateStart = millis();
            display.update();  // Uses mutex to ensure _sendBuffer() completes before returning
            uint32_t updateMs = millis() - updateStart;
            
            Serial.printf("Display update completed in %lu ms (%.1f seconds)\n", 
                         (unsigned long)updateMs, updateMs / 1000.0f);
            Serial.flush();  // Flush after update completes
            
            // Save framebuffer to storage partition after display update
            // safeDisplayUpdate() ensures _sendBuffer() has completed, so the buffer is safe to read
            
            Serial.println("!oai command completed successfully");
            return true;
        } else {
            Serial.printf("PNG draw error: %s\n", pngLoader.getErrorString(pngResult));
            free(aiImageData);
            aiImageData = nullptr;
            aiImageLen = 0;
            return false;
        }
    } else {
        Serial.printf("OpenAI generation failed: %s\n", openai.getLastError());
        if (aiImageData != nullptr) {
            free(aiImageData);
            aiImageData = nullptr;
            aiImageLen = 0;
        }
        return false;
    }
}

/**
 * Custom OTA update with SD card buffering
 * Strategy: Save firmware to SD card first, then flash from SD card
 * This avoids flash write issues during network transfer (ESP-IDF issue #4120)
 */
// Handler for !ota command implementation (defined here after ota_server_task is declared)
bool handleOtaCommand(const String& originalMessage) {
    String senderNumber = extractFromFieldFromMessage(originalMessage);
    if (senderNumber != "+447816969344") {
        Serial.println("ERROR: !ota command only allowed from hardcoded number - command rejected");
        return false;
    }
    // Mark that OTA was triggered via MQTT (so we send notification on next boot)
    {
        NVSGuard guard(otaPrefs, "ota", false);  // read-write
        if (guard.isOpen()) {
            guard.get().putBool("mqtt_triggered", true);
            Serial.println("OTA triggered via MQTT - notification will be sent after update");
        }
        // guard automatically calls end() in destructor
    }
    // Create task with sufficient stack to avoid stack protection fault
    TaskHandle_t otaTaskHandle = nullptr;
    xTaskCreatePinnedToCore(ota_server_task, "ota_server", 16384, nullptr, 5, &otaTaskHandle, 0);
    // Wait for task to complete (it will delete itself)
    while (otaTaskHandle != nullptr && eTaskGetState(otaTaskHandle) != eDeleted) {
        delay(100);
    }
    return true;
}

// Task wrapper for OTA server (prevents stack overflow)
static void ota_server_task(void* arg) {
    startSdBufferedOTA();
    g_ota_requested = false;  // Reset flag after OTA completes
    vTaskDelete(nullptr);  // Delete this task when done
}

/**
 * Background task to monitor serial input for 'o' key (OTA mode)
 * Runs continuously on Core 1, sets g_ota_requested flag when 'o' is pressed
 */
static void serial_monitor_task(void* arg) {
    (void)arg;
    Serial.println("Serial monitor task started - press 'o' at any time to enter OTA mode");
    Serial.println("  Press 'm' at any time to launch web interface");
    Serial.println("  Press 'E' for encryption status, 'e' to toggle encryption");
    
    while (true) {
        if (Serial.available()) {
            char ch = (char)Serial.read();
            if (ch == 'o' || ch == 'O') {
                g_ota_requested = true;
                Serial.println("\n>>> 'o' key detected - OTA mode will start at next safe moment <<<");
            } else if (ch == 'm' || ch == 'M') {
                g_manage_requested = true;
                Serial.println("\n>>> 'm' key detected - Web interface will start at next safe moment <<<");
            } else if (ch == 'E') {
                // Show encryption status
                bool enabled = isEncryptionEnabled();
                Serial.printf("\n>>> Encryption status: %s <<<\n", enabled ? "ENABLED" : "DISABLED (HMAC only)");
                Serial.println("  Messages will be " + String(enabled ? "encrypted" : "base64 encoded only"));
                Serial.println("  HMAC authentication is always required");
            } else if (ch == 'e') {
                // Toggle encryption
                bool current = isEncryptionEnabled();
                bool newValue = !current;
                if (setEncryptionEnabled(newValue)) {
                    Serial.printf("\n>>> Encryption %s <<<\n", newValue ? "ENABLED" : "DISABLED");
                    Serial.println("  New messages will be " + String(newValue ? "encrypted" : "base64 encoded only"));
                    Serial.println("  HMAC authentication is always required");
                } else {
                    Serial.println("\n>>> ERROR: Failed to change encryption setting <<<");
                }
            }
            // Drain any remaining characters to prevent buffer buildup
            while (Serial.available()) {
                (void)Serial.read();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));  // Check every 50ms (lightweight)
    }
}

/**
 * Check if OTA was requested and start OTA server if needed
 * Call this at safe points in the code (after initialization, before sleep, etc.)
 */
static void checkAndStartOTA() {
    if (g_ota_requested) {
        Serial.println("\n>>> Starting OTA server (requested via serial) <<<");
        g_ota_requested = false;  // Reset flag before starting
        // Create task with sufficient stack to avoid stack protection fault
        TaskHandle_t otaTaskHandle = nullptr;
        xTaskCreatePinnedToCore(ota_server_task, "ota_server", 16384, nullptr, 5, &otaTaskHandle, 0);
        // Wait for task to complete (it will delete itself)
        while (otaTaskHandle != nullptr && eTaskGetState(otaTaskHandle) != eDeleted) {
            delay(100);
        }
        // If we reach here, OTA timed out or failed - continue with normal operation
    }
}

/**
 * Check if web interface was requested and start it if needed
 * Call this at safe points in the code (after initialization, before sleep, etc.)
 */
static void checkAndStartManage() {
    if (g_manage_requested) {
        Serial.println("\n>>> Starting web interface (requested via serial) <<<");
        g_manage_requested = false;  // Reset flag before starting
        // Call handleManageCommand directly (it's blocking and handles everything)
        handleManageCommand();
        // After web interface exits, continue with normal operation
    }
}

static bool startSdBufferedOTA() {
    // OTA server with SD card buffering - v4 (fixed OTA check timing)
    Serial.println("Starting OTA server with SD card buffering...");
    

    // Ensure WiFi is connected
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected - connecting now...");
        if (!wifiLoadCredentials()) {
            Serial.println("ERROR: Failed to load WiFi credentials");
            return false;
        }
        
        if (!wifiConnectPersistent(10, 30000, true)) {
            Serial.println("ERROR: Failed to connect to WiFi");
            return false;
        }
    }
    
    // Verify WiFi is connected
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("ERROR: WiFi not connected, cannot start OTA server");
        return false;
    }
    
    // Ensure SD card is mounted

    // Check if SD card is already mounted (either via ESP-IDF direct or Arduino wrapper)
    bool cardReady = false;
    if (sd_card != nullptr) {
        // ESP-IDF direct mount is active
        Serial.println("SD card already mounted (ESP-IDF direct)");
        cardReady = true;
    } else if (SD_MMC.cardType() != CARD_NONE) {
        // Arduino SD_MMC wrapper is active
        Serial.println("SD card already mounted (Arduino SD_MMC)");
        cardReady = true;
    }
    
    // If not ready, mount it
    if (!cardReady) {
        Serial.println("Mounting SD card for OTA buffering...");
        if (!sdInitDirect(false)) {
            Serial.println("ERROR: Failed to mount SD card - OTA requires SD card");
            return false;
        }
        // After sdInitDirect, check which method was used
        if (sd_card != nullptr) {
            Serial.println("SD card mounted via ESP-IDF direct method");
            Serial.println("Using stdio file operations (fopen/fwrite/fread) - no SD_MMC wrapper needed");
            cardReady = true;
        } else if (SD_MMC.cardType() != CARD_NONE) {
            Serial.println("SD card mounted via Arduino SD_MMC wrapper");
            cardReady = true;
        }
    }
    
    // Final verification - check if filesystem is accessible
    if (!cardReady) {
        Serial.println("ERROR: SD card not ready after mount attempt");
        return false;
    }
    
    // Print card info
    if (sd_card != nullptr) {
        Serial.printf("SD card ready (ESP-IDF direct, size: %llu MB)\n", 
                      ((uint64_t)sd_card->csd.capacity * sd_card->csd.sector_size) / (1024 * 1024));
    } else if (SD_MMC.cardType() != CARD_NONE) {
        Serial.printf("SD card ready (Arduino wrapper, type: %d, size: %llu MB)\n", 
                      SD_MMC.cardType(), SD_MMC.cardSize() / (1024 * 1024));
    }
    
    // Get OTA partition info
    const esp_partition_t* running_partition = esp_ota_get_running_partition();
    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(nullptr);
    if (update_partition == nullptr) {
        Serial.println("ERROR: No OTA partition found. Check partition table.");
        return false;
    }
    
    // Debug: Show which partitions we're using
    if (running_partition != nullptr) {
        Serial.printf("Currently running from: %s (offset: 0x%08x, size: 0x%08x)\n",
                      running_partition->label, running_partition->address, running_partition->size);
    }
    Serial.printf("Will write to: %s (offset: 0x%08x, size: 0x%08x)\n",
                  update_partition->label, update_partition->address, update_partition->size);
    
    // Verify we're not writing to the running partition
    if (running_partition != nullptr && running_partition->address == update_partition->address) {
        Serial.println("ERROR: Update partition is the same as running partition!");
        return false;
    }
    
    // Start HTTP server on port 80
    WiFiServer server(80);
    server.begin();
    delay(100);
    
    Serial.println("\n========================================");
    Serial.println("OTA SERVER STARTED");
    Serial.println("========================================");
    Serial.printf("Device IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.println("Access OTA at: http://" + WiFi.localIP().toString() + "/update");
    Serial.println("Strategy: Save to SD card, then flash from SD");
    Serial.println("(Server will block until update completes or timeout)");
    Serial.println("========================================\n");
    
    uint32_t startTime = millis();
    const uint32_t timeoutMs = 600000;  // 10 minute timeout
    bool uploadComplete = false;
    String sdFilePath = "/ota_firmware.bin";
    
    // Wait for client connection
    WiFiClient client;
    while (millis() - startTime < timeoutMs && !uploadComplete) {
        client = server.available();
        
        if (client && client.connected()) {
            Serial.println("Client connected!");
            Serial.printf("Client IP: %s\n", client.remoteIP().toString().c_str());
            
            // Read HTTP request
            String request = client.readStringUntil('\n');
            request.trim();
            Serial.printf("HTTP Request: %s\n", request.c_str());
            
            if (request.indexOf("POST /update") < 0 && request.indexOf("POST /") < 0) {
                Serial.println("Not an OTA POST request");
                client.println("HTTP/1.1 404 Not Found");
                client.println("Connection: close");
                client.println();
                client.stop();
                continue;
            }
            
            // Read headers to find Content-Length and checksums
            int contentLength = 0;
            uint32_t expectedSimpleSum = 0;
            uint32_t expectedCrcLike = 0;
            bool hasChecksums = false;
            
            while (client.available()) {
                String header = client.readStringUntil('\n');
                header.trim();
                if (header.length() == 0) break;
                
                String lowerHeader = header;
                lowerHeader.toLowerCase();
                
                if (lowerHeader.startsWith("content-length:")) {
                    String lenStr = header.substring(header.indexOf(':') + 1);
                    lenStr.trim();
                    contentLength = lenStr.toInt();
                    Serial.printf("Content-Length: %d bytes (%.2f MB)\n", 
                                 contentLength, contentLength / (1024.0 * 1024.0));
                } else if (lowerHeader.startsWith("x-checksum-simple:")) {
                    String valStr = header.substring(header.indexOf(':') + 1);
                    valStr.trim();
                    expectedSimpleSum = (uint32_t)strtoul(valStr.c_str(), nullptr, 0);
                    hasChecksums = true;
                    Serial.printf("Expected simple sum: 0x%08x\n", expectedSimpleSum);
                } else if (lowerHeader.startsWith("x-checksum-crc:")) {
                    String valStr = header.substring(header.indexOf(':') + 1);
                    valStr.trim();
                    expectedCrcLike = (uint32_t)strtoul(valStr.c_str(), nullptr, 0);
                    hasChecksums = true;
                    Serial.printf("Expected CRC-like: 0x%08x\n", expectedCrcLike);
                }
            }
            
            if (contentLength <= 0) {
                Serial.println("ERROR: Invalid or missing Content-Length");
                client.println("HTTP/1.1 400 Bad Request");
                client.println("Connection: close");
                client.println();
                client.stop();
                continue;
            }
            
            // Send HTTP 200 OK response (before reading body)
            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: text/plain");
            client.println("Connection: close");
            client.println();
            delay(50);
            
            // Step 1: Save firmware to SD card
            Serial.println("\nStep 1: Saving firmware to SD card...");

            // Always use standard C file operations (stdio) since sdInitDirect() mounts via ESP-IDF VFS
            // The filesystem is mounted at /sdcard, so we can use fopen/fwrite/fread
            String fullPath = "/sdcard" + sdFilePath;
            FILE* sdFile = fopen(fullPath.c_str(), "wb");
            if (!sdFile) {
                Serial.printf("ERROR: Failed to open SD card file for writing: %s\n", fullPath.c_str());
                client.stop();
                continue;
            }
            Serial.printf("Opened file for writing: %s\n", fullPath.c_str());
            
            const size_t BUFFER_SIZE = 8192;
            uint8_t buffer[BUFFER_SIZE];
            size_t totalReceived = 0;
            uint32_t lastProgress = 0;
            bool magicVerified = false;
            
            // Checksum calculation (matching Python script)
            uint32_t receivedSimpleSum = 0;
            uint32_t receivedCrcLike = 0;
            
            Serial.println("Receiving firmware and writing to SD card...");
            while (totalReceived < (size_t)contentLength && (client.connected() || client.available())) {
                if (!client.available()) {
                    // If client disconnected and no data available, we're done
                    if (!client.connected()) {
                        break;
                    }
                    delay(10);
                    continue;
                }
                
                size_t toRead = min((size_t)BUFFER_SIZE, (size_t)(contentLength - totalReceived));
                size_t available = client.available();
                if (toRead > available) {
                    toRead = available;
                }
                
                size_t readBytes = client.readBytes(buffer, toRead);
                if (readBytes == 0) {
                    delay(10);
                    continue;
                }
                
                // Verify magic byte on first chunk
                if (!magicVerified && totalReceived == 0 && readBytes >= 1) {
                    if (buffer[0] != 0xE9) {
                        Serial.printf("ERROR: Invalid firmware magic byte: 0x%02x\n", buffer[0]);
                        fclose(sdFile);
                        String fullPath = "/sdcard" + sdFilePath;
                        remove(fullPath.c_str());
                        client.stop();
                        break;
                    }
                    Serial.println("Firmware magic byte verified (0xE9)");
                    magicVerified = true;
                }
                
                // Calculate checksums as we receive data (matching Python script algorithm)
                for (size_t i = 0; i < readBytes; i++) {
                    uint8_t byte = buffer[i];
                    // Simple sum checksum
                    receivedSimpleSum = (receivedSimpleSum + byte) & 0xFFFFFFFF;
                    // CRC-like checksum
                    receivedCrcLike = ((receivedCrcLike << 1) ^ byte) & 0xFFFFFFFF;
                    if (receivedCrcLike & 0x80000000) {
                        receivedCrcLike = (receivedCrcLike ^ 0x04C11DB7) & 0xFFFFFFFF;
                    }
                }
                
                // Write to SD card
                size_t written = fwrite(buffer, 1, readBytes, sdFile);
                if (written != readBytes) {
                    Serial.printf("ERROR: SD write failed: wrote %zu/%zu bytes\n", written, readBytes);
                    fclose(sdFile);
                    String fullPath = "/sdcard" + sdFilePath;
                    remove(fullPath.c_str());
                    client.stop();
                    break;
                }
                
                totalReceived += readBytes;
                
                // Progress reporting
                if (totalReceived - lastProgress >= 102400) {
                    float percent = (totalReceived * 100.0f) / contentLength;
                    Serial.printf("Progress: %zu/%d bytes (%.1f%%)\n", totalReceived, contentLength, percent);
                    lastProgress = totalReceived;
                }
            }
            
            // Close file
            fclose(sdFile);
            client.stop();
            
            if (totalReceived != (size_t)contentLength) {
                Serial.printf("ERROR: Incomplete download: %zu/%d bytes\n", totalReceived, contentLength);
                String fullPath = "/sdcard" + sdFilePath;
                remove(fullPath.c_str());
                continue;
            }
            
            Serial.printf("Firmware saved to SD card: %zu bytes\n", totalReceived);
            
            // Verify checksums match
            Serial.println("\nVerifying checksums...");
            Serial.printf("Received simple sum: 0x%08x (%u)\n", receivedSimpleSum, receivedSimpleSum);
            Serial.printf("Received CRC-like:   0x%08x\n", receivedCrcLike);
            
            if (hasChecksums) {
                Serial.printf("Expected simple sum: 0x%08x (%u)\n", expectedSimpleSum, expectedSimpleSum);
                Serial.printf("Expected CRC-like:   0x%08x\n", expectedCrcLike);
                
                if (receivedSimpleSum != expectedSimpleSum) {
                    Serial.printf("ERROR: Simple sum mismatch! Received 0x%08x, expected 0x%08x\n", 
                                 receivedSimpleSum, expectedSimpleSum);
                    String fullPath = "/sdcard" + sdFilePath;
                    remove(fullPath.c_str());
                    continue;
                }
                
                if (receivedCrcLike != expectedCrcLike) {
                    Serial.printf("ERROR: CRC-like checksum mismatch! Received 0x%08x, expected 0x%08x\n", 
                                 receivedCrcLike, expectedCrcLike);
                    String fullPath = "/sdcard" + sdFilePath;
                    remove(fullPath.c_str());
                    continue;
                }
                
                Serial.println("Checksums match! Data integrity verified.");
            } else {
                Serial.println("WARNING: No checksums provided by client - skipping verification");
            }
            
            // Step 2: Read from SD card and write to OTA partition
            Serial.println("\nStep 2: Flashing firmware from SD card to OTA partition...");
            
            // Begin OTA update
            esp_ota_handle_t ota_handle = 0;
            esp_err_t err = esp_ota_begin(update_partition, 0, &ota_handle);
            if (err != ESP_OK) {
                Serial.printf("ERROR: esp_ota_begin failed: %s (0x%x)\n", esp_err_to_name(err), err);
                remove(fullPath.c_str());
                continue;
            }
            
            // Open SD file for reading (reuse fullPath from earlier)
            FILE* readFile = fopen(fullPath.c_str(), "rb");
            if (!readFile) {
                Serial.printf("ERROR: Failed to open SD card file for reading: %s\n", fullPath.c_str());
                esp_ota_abort(ota_handle);
                remove(fullPath.c_str());
                continue;
            }
            
            size_t totalWritten = 0;
            lastProgress = 0;
            
            while (true) {
                size_t toRead = min((size_t)BUFFER_SIZE, (size_t)(contentLength - totalWritten));
                size_t readBytes = fread(buffer, 1, toRead, readFile);
                if (readBytes == 0) break;
                
                // Write to OTA partition
                err = esp_ota_write(ota_handle, buffer, readBytes);
                if (err != ESP_OK) {
                    Serial.printf("ERROR: esp_ota_write failed at offset %zu: %s (0x%x)\n", 
                                 totalWritten, esp_err_to_name(err), err);
                    fclose(readFile);
                    String fullPath = "/sdcard" + sdFilePath;
                    remove(fullPath.c_str());
                    esp_ota_abort(ota_handle);
                    break;
                }
                
                totalWritten += readBytes;
                
                // Progress reporting
                if (totalWritten - lastProgress >= 102400) {
                    float percent = (totalWritten * 100.0f) / contentLength;
                    Serial.printf("Flash progress: %zu/%d bytes (%.1f%%)\n", totalWritten, contentLength, percent);
                    lastProgress = totalWritten;
                    
                    // Periodic flash synchronization during write (every 100KB)
                    // Small delay to allow flash controller to process writes
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
            }
            
            // Close read file
            fclose(readFile);
            
            if (totalWritten != (size_t)contentLength) {
                Serial.printf("ERROR: Incomplete flash: %zu/%d bytes\n", totalWritten, contentLength);
                esp_ota_abort(ota_handle);
                String fullPath = "/sdcard" + sdFilePath;
                remove(fullPath.c_str());
                continue;
            }
            
            Serial.printf("Firmware flashed: %zu bytes\n", totalWritten);
            
            // Step 3: Synchronize flash and validate
            Serial.println("\nStep 3: Synchronizing flash and validating...");
            
            // Critical: Force flash cache to flush before validation
            // Read from the partition to ensure cache coherency
            Serial.println("Flushing flash cache...");
            const esp_partition_t* running = esp_ota_get_running_partition();
            const esp_partition_t* update = esp_ota_get_next_update_partition(nullptr);
            
            // Read a few bytes from the update partition to force cache flush
            if (update != nullptr) {
                uint8_t dummy[16];
                esp_partition_read(update, 0, dummy, sizeof(dummy));
                vTaskDelay(pdMS_TO_TICKS(100));
                esp_partition_read(update, update->size - 16, dummy, sizeof(dummy));
            }
            
            // Additional delay to ensure all writes are committed
            Serial.println("Waiting for flash writes to complete...");
            vTaskDelay(pdMS_TO_TICKS(5000));  // 5 second delay for flash to complete
            
            // Read from partition again to force cache coherency
            if (update != nullptr) {
                uint8_t dummy[32];
                esp_partition_read(update, 0, dummy, sizeof(dummy));
                vTaskDelay(pdMS_TO_TICKS(2000));  // 2 more seconds
            }
            
            // Finalize OTA update
            err = esp_ota_end(ota_handle);
            if (err != ESP_OK) {
                if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
                    Serial.println("ERROR: Firmware validation failed - checksum mismatch");
                } else {
                    Serial.printf("ERROR: esp_ota_end failed: %s (0x%x)\n", esp_err_to_name(err), err);
                }
                String fullPath = "/sdcard" + sdFilePath;
                remove(fullPath.c_str());
                continue;
            }
            
            // Set boot partition (this tells bootloader to boot from the new partition on next reboot)
            Serial.printf("Setting boot partition to: %s (offset: 0x%08x)\n",
                         update_partition->label, update_partition->address);
            err = esp_ota_set_boot_partition(update_partition);
            if (err != ESP_OK) {
                Serial.printf("ERROR: esp_ota_set_boot_partition failed: %s (0x%x)\n", esp_err_to_name(err), err);
                String fullPath = "/sdcard" + sdFilePath;
                remove(fullPath.c_str());
                continue;
            }
            
            // Verify boot partition was set correctly
            const esp_partition_t* boot_partition = esp_ota_get_boot_partition();
            if (boot_partition != nullptr) {
                Serial.printf("Boot partition set to: %s (offset: 0x%08x)\n",
                             boot_partition->label, boot_partition->address);
                if (boot_partition->address != update_partition->address) {
                    Serial.println("WARNING: Boot partition address doesn't match update partition!");
                }
            } else {
                Serial.println("WARNING: Could not verify boot partition after setting");
            }
            
            // Clean up SD file (reuse fullPath from earlier)
            remove(fullPath.c_str());
            Serial.println("OTA update complete - rebooting...");
            Serial.flush();
            delay(1000);
            ESP.restart();
            
            uploadComplete = true;
        }
        
        delay(50);
    }
    
    server.stop();
    
    if (!uploadComplete) {
        Serial.println("OTA server timeout - continuing with normal boot");
    }
    
    return uploadComplete;
}

// Helper functions for management web interface

static String readSDFile(const char* path) {
    String content = "";
    // Use FatFs directly for reading (consistent with loadQuotesFromSD)
    FIL file;
    FRESULT res = f_open(&file, path, FA_READ);
    if (res == FR_OK) {
        char buffer[256];
        UINT bytesRead;
        while (f_read(&file, buffer, sizeof(buffer) - 1, &bytesRead) == FR_OK && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            content += String(buffer);
            if (bytesRead < sizeof(buffer) - 1) break; // EOF
        }
        f_close(&file);
    } else {
        Serial.printf("ERROR: Failed to open file for reading: %s (error %d)\n", path, res);
    }
    return content;
}

static bool writeSDFile(const char* path, const String& content) {
    // Use FatFs directly for writing (consistent with SD card operations)
    FIL file;
    FRESULT res = f_open(&file, path, FA_WRITE | FA_CREATE_ALWAYS);
    if (res != FR_OK) {
        Serial.printf("ERROR: Failed to open file for writing: %s (error %d)\n", path, res);
        return false;
    }
    
    UINT bytesWritten;
    res = f_write(&file, content.c_str(), content.length(), &bytesWritten);
    f_close(&file);
    
    if (res != FR_OK || bytesWritten != content.length()) {
        Serial.printf("ERROR: Failed to write all data to %s (wrote %u/%zu, error %d)\n", 
                     path, bytesWritten, content.length(), res);
        return false;
    }
    
    Serial.printf("Successfully wrote %u bytes to %s\n", bytesWritten, path);
    return true;
}

static String listImageFiles() {
    String json = "[";
    bool first = true;
    
    FF_DIR dir;
    FILINFO fno;
    FRESULT res = f_opendir(&dir, "0:/");
    
    if (res == FR_OK) {
        while (true) {
            res = f_readdir(&dir, &fno);
            if (res != FR_OK || fno.fname[0] == 0) break;
            
            // Check if it's a file (not directory) and has image extension
            if (!(fno.fattrib & AM_DIR)) {
                String filename = String(fno.fname);
                filename.toLowerCase();
                if (filename.endsWith(".png") || filename.endsWith(".bmp") || 
                    filename.endsWith(".jpg") || filename.endsWith(".jpeg")) {
                    if (!first) json += ",";
                    json += "\"" + String(fno.fname) + "\"";
                    first = false;
                }
            }
        }
        f_closedir(&dir);
    }
    
    json += "]";
    return json;
}

static String listAudioFiles() {
    String json = "[";
    bool first = true;
    
    FF_DIR dir;
    FILINFO fno;
    FRESULT res = f_opendir(&dir, "0:/");
    
    if (res == FR_OK) {
        while (true) {
            res = f_readdir(&dir, &fno);
            if (res != FR_OK || fno.fname[0] == 0) break;
            
            // Check if it's a file (not directory) and has audio extension
            if (!(fno.fattrib & AM_DIR)) {
                String filename = String(fno.fname);
                filename.toLowerCase();
                if (filename.endsWith(".wav") || filename.endsWith(".mp3")) {
                    if (!first) json += ",";
                    json += "\"" + String(fno.fname) + "\"";
                    first = false;
                }
            }
        }
        f_closedir(&dir);
    }
    
    json += "]";
    return json;
}

static String listAllFiles() {
    String json = "[";
    bool first = true;
    
    FF_DIR dir;
    FILINFO fno;
    FRESULT res = f_opendir(&dir, "0:/");
    
    if (res == FR_OK) {
        while (true) {
            res = f_readdir(&dir, &fno);
            if (res != FR_OK || fno.fname[0] == 0) break;
            
            // Only list files (not directories)
            if (!(fno.fattrib & AM_DIR)) {
                if (!first) json += ",";
                json += "{\"name\":\"";
                json += String(fno.fname);
                json += "\",\"size\":";
                json += String(fno.fsize);
                
                // Extract date/time from FatFs format
                // fdate: bits 0-4 = day (1-31), bits 5-8 = month (1-12), bits 9-15 = year from 1980
                // ftime: bits 0-4 = second/2 (0-29), bits 5-10 = minute (0-59), bits 11-15 = hour (0-23)
                uint16_t year = 1980 + ((fno.fdate >> 9) & 0x7F);
                uint8_t month = (fno.fdate >> 5) & 0x0F;
                uint8_t day = fno.fdate & 0x1F;
                uint8_t hour = (fno.ftime >> 11) & 0x1F;
                uint8_t minute = (fno.ftime >> 5) & 0x3F;
                uint8_t second = (fno.ftime & 0x1F) * 2;
                
                // Convert to timestamp (milliseconds since epoch)
                // Using a simple calculation (approximate, doesn't account for leap years perfectly)
                uint32_t daysSinceEpoch = 0;
                for (uint16_t y = 1970; y < year; y++) {
                    daysSinceEpoch += ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) ? 366 : 365;
                }
                uint8_t daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
                if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) daysInMonth[1] = 29;
                for (uint8_t m = 1; m < month; m++) {
                    daysSinceEpoch += daysInMonth[m - 1];
                }
                daysSinceEpoch += (day - 1);
                
                uint64_t timestamp = (uint64_t)daysSinceEpoch * 86400000ULL;
                timestamp += (uint64_t)hour * 3600000ULL;
                timestamp += (uint64_t)minute * 60000ULL;
                timestamp += (uint64_t)second * 1000ULL;
                
                json += ",\"modified\":";
                json += String(timestamp);
                json += "}";
                first = false;
            }
        }
        f_closedir(&dir);
    }
    
    json += "]";
    return json;
}

static bool deleteSDFile(const char* filename) {
    String path = "0:/";
    path += filename;
    FRESULT res = f_unlink(path.c_str());
    if (res == FR_OK) {
        Serial.printf("Successfully deleted file: %s\n", path.c_str());
        return true;
    } else {
        Serial.printf("ERROR: Failed to delete file %s (error %d)\n", path.c_str(), res);
        return false;
    }
}

static String getDeviceSettingsJSON() {
    String json = "{";
    json += "\"volume\":" + String(g_audio_volume_pct) + ",";
    json += "\"sleepInterval\":" + String(g_sleep_interval_minutes) + ",";
    json += "\"hourSchedule\":\"";
    for (int i = 0; i < 24; i++) {
        json += (g_hour_schedule[i] ? "1" : "0");
    }
    json += "\"";
    json += "}";
    return json;
}

static bool updateDeviceSettings(const String& json) {
    // Simple JSON parsing for volume, sleepInterval, and hourSchedule
    int volumeStart = json.indexOf("\"volume\":");
    int sleepStart = json.indexOf("\"sleepInterval\":");
    int hourScheduleStart = json.indexOf("\"hourSchedule\":");
    
    if (volumeStart >= 0) {
        int colonPos = json.indexOf(':', volumeStart);
        int valueEnd = json.indexOf(',', colonPos);
        if (valueEnd < 0) valueEnd = json.indexOf('}', colonPos);
        if (valueEnd > colonPos) {
            String volumeStr = json.substring(colonPos + 1, valueEnd);
            volumeStr.trim();
            int volume = volumeStr.toInt();
            if (volume >= 0 && volume <= 100) {
                g_audio_volume_pct = volume;
                volumeSaveToNVS();
                if (g_codec_ready) {
                    (void)g_codec.setDacVolumePercentMapped(g_audio_volume_pct, kCodecVolumeMinPct, kCodecVolumeMaxPct);
                }
                Serial.printf("Volume updated to %d%%\n", volume);
            }
        }
    }
    
    if (sleepStart >= 0) {
        int colonPos = json.indexOf(':', sleepStart);
        int valueEnd = json.indexOf(',', colonPos);
        if (valueEnd < 0) valueEnd = json.indexOf('}', colonPos);
        if (valueEnd > colonPos) {
            String sleepStr = json.substring(colonPos + 1, valueEnd);
            sleepStr.trim();
            int interval = sleepStr.toInt();
            // Validate: must be a factor of 60
            if (interval > 0 && interval <= 60 && (60 % interval == 0)) {
                g_sleep_interval_minutes = interval;
                sleepDurationSaveToNVS();
                Serial.printf("Sleep interval updated to %d minutes\n", interval);
            }
        }
    }
    
    if (hourScheduleStart >= 0) {
        // Extract hourSchedule field using json_utils.h
        String scheduleStr = extractJsonStringField(json, "hourSchedule");
        if (scheduleStr.length() == 24) {
            // Parse the schedule string
            for (int i = 0; i < 24; i++) {
                g_hour_schedule[i] = (scheduleStr.charAt(i) == '1');
            }
            hourScheduleSaveToNVS();
            Serial.println("Hour schedule updated");
        }
    }
    
    return true;
}

#if 0
// OLD HTML GENERATION CODE - REMOVED (now using embedded WEB_HTML_CONTENT from web_assets.h)
// Helper function to write HTML chunk to stream
static void writeHTMLChunk(PsychicStreamResponse& stream, const char* str) {
    size_t len = strlen(str);
    stream.write((const uint8_t*)str, len);
}

// Generate and stream HTML management interface directly to response
static void generateAndStreamManagementHTML(PsychicStreamResponse& stream) {
    writeHTMLChunk(stream, "<!DOCTYPE html><html><head><meta charset='UTF-8'>");
    writeHTMLChunk(stream, "<meta name='viewport' content='width=device-width, initial-scale=1.0'>");
    writeHTMLChunk(stream, "<title>Device Management</title>");
    writeHTMLChunk(stream, "<style>");
    writeHTMLChunk(stream, "body{font-family:Arial,sans-serif;max-width:1200px;margin:0 auto;padding:20px;background:#f5f5f5;}");
    writeHTMLChunk(stream, "h1{color:#333;border-bottom:2px solid #4CAF50;padding-bottom:10px;}");
    writeHTMLChunk(stream, ".section{background:white;padding:20px;margin:20px 0;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1);}");
    writeHTMLChunk(stream, "h2{color:#4CAF50;margin-top:0;}");
    writeHTMLChunk(stream, "textarea{width:100%;min-height:200px;font-family:monospace;padding:10px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box;}");
    writeHTMLChunk(stream, "input[type='number']{width:100px;padding:8px;border:1px solid #ddd;border-radius:4px;}");
    writeHTMLChunk(stream, "select{width:100%;padding:8px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box;}");
    writeHTMLChunk(stream, "button{background:#4CAF50;color:white;border:none;padding:10px 20px;border-radius:4px;cursor:pointer;font-size:16px;margin:5px;}");
    writeHTMLChunk(stream, "button:hover{background:#45a049;}");
    writeHTMLChunk(stream, "button.delete{background:#f44336;padding:5px 10px;font-size:12px;}");
    writeHTMLChunk(stream, "button.delete:hover{background:#d32f2f;}");
    writeHTMLChunk(stream, ".status{color:#4CAF50;margin:10px 0;font-weight:bold;}");
    writeHTMLChunk(stream, ".error{color:#f44336;}");
    writeHTMLChunk(stream, "label{display:block;margin:10px 0 5px 0;font-weight:bold;}");
    writeHTMLChunk(stream, ".form-group{margin:15px 0;}");
    writeHTMLChunk(stream, "td{padding:5px;}");
    writeHTMLChunk(stream, "</style></head><body>");
    writeHTMLChunk(stream, "<h1>Device Configuration Management</h1>");
    
    // Quotes section
    writeHTMLChunk(stream, "<div class='section'><h2>Quotes Configuration (quotes.txt)</h2>");
    writeHTMLChunk(stream, "<p>Format: Quote text on one or more lines, followed by ~Author on the next line. Separate quotes with blank lines.</p>");
    writeHTMLChunk(stream, "<textarea id='quotesContent' placeholder='Loading quotes.txt...'></textarea><br>");
    writeHTMLChunk(stream, "<button onclick='loadQuotes()'>Load from Device</button>");
    writeHTMLChunk(stream, "<button onclick='saveQuotes()'>Save to Device</button>");
    writeHTMLChunk(stream, "<div id='quotesStatus'></div></div>");
    
    // Media section
    writeHTMLChunk(stream, "<div class='section'><h2>Media Mappings (media.txt)</h2>");
    writeHTMLChunk(stream, "<p>Select image and audio file combinations. Leave audio empty for no audio. Check the 'Next' box to set which item will be displayed next. Click 'Show' to display the image and play audio on the panel.</p>");
    writeHTMLChunk(stream, "<table id='mediaTable' style='width:100%;border-collapse:collapse;margin:10px 0;'>");
    writeHTMLChunk(stream, "<thead><tr style='background:#f0f0f0;'><th style='padding:10px;text-align:left;width:30%;'>Image File</th><th style='padding:10px;text-align:left;width:30%;'>Audio File</th><th style='padding:10px;text-align:center;width:10%;'>Next</th><th style='padding:10px;text-align:center;width:15%;'>Actions</th><th style='padding:10px;width:15%;'></th></tr></thead>");
    writeHTMLChunk(stream, "<tbody id='mediaRows'></tbody>");
    writeHTMLChunk(stream, "</table>");
    writeHTMLChunk(stream, "<button onclick='addMediaRow()'>Add New Row</button>");
    writeHTMLChunk(stream, "<button onclick='loadMedia()'>Load from Device</button>");
    writeHTMLChunk(stream, "<button onclick='saveMedia()'>Save to Device</button>");
    writeHTMLChunk(stream, "<div id='mediaStatus'></div></div>");
    
    // Device settings section
    writeHTMLChunk(stream, "<div class='section'><h2>Device Settings</h2>");
    writeHTMLChunk(stream, "<div class='form-group'>");
    writeHTMLChunk(stream, "<label>Volume (0-100%):</label>");
    writeHTMLChunk(stream, "<input type='number' id='volume' min='0' max='100' value='50'>");
    writeHTMLChunk(stream, "</div>");
    writeHTMLChunk(stream, "<div class='form-group'>");
    writeHTMLChunk(stream, "<label>Sleep Interval (minutes, must be factor of 60):</label>");
    writeHTMLChunk(stream, "<input type='number' id='sleepInterval' min='1' max='60' value='1'>");
    writeHTMLChunk(stream, "</div>");
    writeHTMLChunk(stream, "<div class='form-group'>");
    writeHTMLChunk(stream, "<label>Hour Schedule (check hours when device should wake):</label>");
    writeHTMLChunk(stream, "<div style='display:flex;flex-wrap:wrap;gap:10px;margin:10px 0;padding:10px;background:#f9f9f9;border-radius:4px;'>");
    for (int i = 0; i < 24; i++) {
        char hourHtml[200];
        snprintf(hourHtml, sizeof(hourHtml), 
            "<label style='display:flex;align-items:center;gap:5px;cursor:pointer;'><input type='checkbox' class='hourCheckbox' data-hour='%d' style='cursor:pointer;'>%02d:00</label>", 
            i, i);
        writeHTMLChunk(stream, hourHtml);
    }
    writeHTMLChunk(stream, "</div>");
    writeHTMLChunk(stream, "<div style='margin-top:10px;'><button onclick='selectAllHours()'>Select All</button>");
    writeHTMLChunk(stream, "<button onclick='deselectAllHours()'>Deselect All</button>");
    writeHTMLChunk(stream, "<button onclick='selectNightHours()'>Select Night (6am-11pm)</button>");
    writeHTMLChunk(stream, "<button onclick='selectDayHours()'>Select Day (11pm-6am)</button></div>");
    writeHTMLChunk(stream, "</div>");
    writeHTMLChunk(stream, "<button onclick='loadSettings()'>Load from Device</button>");
    writeHTMLChunk(stream, "<button onclick='saveSettings()'>Save to Device</button>");
    writeHTMLChunk(stream, "<div id='settingsStatus'></div></div>");
    
    // File management section
    writeHTMLChunk(stream, "<div class='section'><h2>File Management</h2>");
    writeHTMLChunk(stream, "<p>Upload, download, and delete files on the SD card.</p>");
    writeHTMLChunk(stream, "<div style='margin:10px 0;'><input type='file' id='fileUpload' style='display:none;' onchange='handleFileSelect(event)'><button onclick='document.getElementById(\"fileUpload\").click()'>Upload File</button>");
    writeHTMLChunk(stream, "<button onclick='refreshFileList()'>Refresh File List</button></div>");
    writeHTMLChunk(stream, "<div id='fileList' style='margin:10px 0;'>Loading files...</div>");
    writeHTMLChunk(stream, "<div id='fileStatus'></div></div>");
    
    // Log viewing section
    writeHTMLChunk(stream, "<div class='section'><h2>System Log</h2>");
    writeHTMLChunk(stream, "<p>View the current system log file (read-only).</p>");
    writeHTMLChunk(stream, "<button onclick='loadLog()'>Load Current Log</button>");
    writeHTMLChunk(stream, "<button onclick='loadLogArchiveList()'>Load Previous Log</button>");
    writeHTMLChunk(stream, "<div id='logArchiveList' style='margin-top:10px;'></div>");
    writeHTMLChunk(stream, "<div id='logContent' style='margin:10px 0;max-height:600px;overflow-y:auto;background:#f9f9f9;padding:10px;border:1px solid #ddd;border-radius:4px;font-family:monospace;font-size:12px;white-space:pre-wrap;word-wrap:break-word;'></div>");
    writeHTMLChunk(stream, "<div id='logStatus'></div></div>");
    
    // Close server button
    writeHTMLChunk(stream, "<div class='section'><h2>Server Control</h2>");
    writeHTMLChunk(stream, "<p>Close the management interface and return to normal operation.</p>");
    writeHTMLChunk(stream, "<button onclick='closeServer()' style='background:#f44336;'>Close Management Interface</button>");
    writeHTMLChunk(stream, "<div id='closeStatus'></div></div>");
    
    // JavaScript - write in chunks to avoid large string
    writeHTMLChunk(stream, "<script>");
    // Load all the JavaScript functions - keeping them as chunks to minimize stack usage
    writeHTMLChunk(stream, "function loadQuotes(){fetch('/api/quotes').then(r=>r.text()).then(t=>{document.getElementById('quotesContent').value=t;showStatus('quotesStatus','Loaded successfully',false);}).catch(e=>showStatus('quotesStatus','Error: '+e,true));}");
    writeHTMLChunk(stream, "function saveQuotes(){const content=document.getElementById('quotesContent').value;fetch('/api/quotes',{method:'POST',body:content}).then(r=>r.json()).then(d=>{showStatus('quotesStatus',d.success?'Saved successfully':'Error: '+d.error,d.success?false:true);if(d.success)loadQuotes();}).catch(e=>showStatus('quotesStatus','Error: '+e,true));}");
    writeHTMLChunk(stream, "let imageFiles=[];let audioFiles=[];let filesLoaded=0;");
    writeHTMLChunk(stream, "function checkAndLoadMedia(){if(filesLoaded>=2){loadMedia();}else if(filesLoaded===1){showStatus('mediaStatus','Warning: Only partial file list loaded',true);}}");
    writeHTMLChunk(stream, "function loadFileLists(){filesLoaded=0;fetch('/api/images').then(r=>r.json()).then(f=>{imageFiles=f;filesLoaded++;checkAndLoadMedia();}).catch(e=>{showStatus('mediaStatus','Error loading images: '+e,true);filesLoaded++;checkAndLoadMedia();});fetch('/api/audio').then(r=>r.json()).then(f=>{audioFiles=f;filesLoaded++;checkAndLoadMedia();}).catch(e=>{showStatus('mediaStatus','Error loading audio: '+e,true);filesLoaded++;checkAndLoadMedia();});}");
    // Continue with remaining JavaScript - this is a very long section, so we'll keep it as-is but stream it
    // For brevity, I'll include the key parts and note that the full JS should be streamed
    writeHTMLChunk(stream, "let showInProgress=false;function createMediaRow(image='',audio='',isNext=false){const row=document.createElement('tr');row.dataset.index=document.getElementById('mediaRows').children.length;const imgCell=document.createElement('td');const imgSelect=document.createElement('select');imgSelect.className='imageSelect';imgSelect.innerHTML='<option value=\"\">-- Select Image --</option>';imageFiles.forEach(f=>{const opt=document.createElement('option');opt.value=f;opt.text=f;opt.selected=(f===image);imgSelect.appendChild(opt);});imgCell.appendChild(imgSelect);const audCell=document.createElement('td');const audSelect=document.createElement('select');audSelect.className='audioSelect';audSelect.innerHTML='<option value=\"\">(none)</option>';audioFiles.forEach(f=>{const opt=document.createElement('option');opt.value=f;opt.text=f;opt.selected=(f===audio);audSelect.appendChild(opt);});audCell.appendChild(audSelect);const nextCell=document.createElement('td');nextCell.style.textAlign='center';const nextCheck=document.createElement('input');nextCheck.type='checkbox';nextCheck.className='nextCheckbox';nextCheck.checked=isNext;nextCheck.onchange=function(){document.querySelectorAll('.nextCheckbox').forEach(cb=>{if(cb!==nextCheck)cb.checked=false;});};nextCell.appendChild(nextCheck);const actionCell=document.createElement('td');actionCell.style.textAlign='center';const showBtn=document.createElement('button');showBtn.className='showBtn';showBtn.textContent='Show';showBtn.style.margin='2px';showBtn.style.padding='4px 8px';showBtn.style.fontSize='12px';showBtn.disabled=showInProgress;showBtn.onclick=function(){if(showInProgress){alert('Another show operation is in progress. Please wait.');return;}const idx=parseInt(row.dataset.index);showMediaItem(idx);};actionCell.appendChild(showBtn);const delCell=document.createElement('td');const delBtn=document.createElement('button');delBtn.className='delete';delBtn.textContent='Delete';delBtn.onclick=function(){row.remove();updateRowIndices();};delCell.appendChild(delBtn);row.appendChild(imgCell);row.appendChild(audCell);row.appendChild(nextCell);row.appendChild(actionCell);row.appendChild(delCell);return row;}");
    writeHTMLChunk(stream, "function showMediaItem(index){if(showInProgress){return;}showInProgress=true;document.querySelectorAll('.showBtn').forEach(btn=>btn.disabled=true);showStatus('mediaStatus','Displaying image and playing audio (this will take 20-30 seconds)...',false);fetch('/api/media/show?index='+index,{method:'POST'}).then(r=>r.json()).then(d=>{showInProgress=false;document.querySelectorAll('.showBtn').forEach(btn=>btn.disabled=false);if(d.success){showStatus('mediaStatus','Display updated successfully. Next item: '+(d.nextIndex+1),false);loadMedia();}else{showStatus('mediaStatus','Error: '+d.error,true);}}).catch(e=>{showInProgress=false;document.querySelectorAll('.showBtn').forEach(btn=>btn.disabled=false);showStatus('mediaStatus','Error: '+e,true);});}");
    writeHTMLChunk(stream, "function updateRowIndices(){const rows=document.querySelectorAll('#mediaRows tr');rows.forEach((row,idx)=>{row.dataset.index=idx;});}");
    writeHTMLChunk(stream, "function addMediaRow(){const tbody=document.getElementById('mediaRows');tbody.appendChild(createMediaRow());}");
    writeHTMLChunk(stream, "function loadMedia(){Promise.all([fetch('/api/media').then(r=>r.text()),fetch('/api/media/index').then(r=>r.json())]).then(([content,indexData])=>{const tbody=document.getElementById('mediaRows');tbody.innerHTML='';const lastDisplayedIndex=indexData.index||0;const lines=content.split('\\n');const mediaCount=lines.filter(l=>{l=l.trim();return l.length>0&&!l.startsWith('#');}).length;const nextIndex=(lastDisplayedIndex+1)%mediaCount;let lineIdx=0;lines.forEach((line,idx)=>{line=line.trim();if(line.length===0||line.startsWith('#'))return;const comma=line.indexOf(',');const isNext=(lineIdx===nextIndex);if(comma>0){const img=line.substring(0,comma).trim();const aud=line.substring(comma+1).trim();tbody.appendChild(createMediaRow(img,aud,isNext));}else if(line.length>0){tbody.appendChild(createMediaRow(line,'',isNext));}lineIdx++;});updateRowIndices();if(tbody.children.length===0)addMediaRow();showStatus('mediaStatus','Loaded successfully',false);}).catch(e=>{showStatus('mediaStatus','Error: '+e,true);});}");
    writeHTMLChunk(stream, "function saveMedia(){const rows=document.querySelectorAll('#mediaRows tr');let content='';let nextIndex=-1;rows.forEach((row,idx)=>{const img=row.querySelector('.imageSelect').value;const aud=row.querySelector('.audioSelect').value;const isNext=row.querySelector('.nextCheckbox').checked;if(isNext)nextIndex=idx;if(img.length>0){content+=img;if(aud.length>0)content+=','+aud;content+='\\n';}});const saveData={content:content,nextIndex:nextIndex>=0?nextIndex:null};fetch('/api/media',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(saveData)}).then(r=>r.json()).then(d=>{showStatus('mediaStatus',d.success?'Saved successfully':'Error: '+d.error,d.success?false:true);if(d.success)loadMedia();}).catch(e=>showStatus('mediaStatus','Error: '+e,true));}");
    writeHTMLChunk(stream, "function loadSettings(){fetch('/api/settings').then(r=>r.json()).then(d=>{document.getElementById('volume').value=d.volume;document.getElementById('sleepInterval').value=d.sleepInterval;if(d.hourSchedule){const schedule=d.hourSchedule;document.querySelectorAll('.hourCheckbox').forEach(cb=>{const hour=parseInt(cb.dataset.hour);cb.checked=schedule[hour]==='1'||schedule[hour]===true;});}showStatus('settingsStatus','Loaded successfully',false);}).catch(e=>showStatus('settingsStatus','Error: '+e,true));}");
    writeHTMLChunk(stream, "function saveSettings(){const hourSchedule=[];document.querySelectorAll('.hourCheckbox').forEach(cb=>{hourSchedule.push(cb.checked?'1':'0');});const json=JSON.stringify({volume:parseInt(document.getElementById('volume').value),sleepInterval:parseInt(document.getElementById('sleepInterval').value),hourSchedule:hourSchedule.join('')});fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:json}).then(r=>r.json()).then(d=>{showStatus('settingsStatus',d.success?'Saved successfully':'Error: '+d.error,d.success?false:true);if(d.success)loadSettings();}).catch(e=>showStatus('settingsStatus','Error: '+e,true));}");
    writeHTMLChunk(stream, "function selectAllHours(){document.querySelectorAll('.hourCheckbox').forEach(cb=>cb.checked=true);}");
    writeHTMLChunk(stream, "function deselectAllHours(){document.querySelectorAll('.hourCheckbox').forEach(cb=>cb.checked=false);}");
    writeHTMLChunk(stream, "function selectNightHours(){document.querySelectorAll('.hourCheckbox').forEach(cb=>{const hour=parseInt(cb.dataset.hour);cb.checked=(hour>=6&&hour<23);});}");
    writeHTMLChunk(stream, "function selectDayHours(){document.querySelectorAll('.hourCheckbox').forEach(cb=>{const hour=parseInt(cb.dataset.hour);cb.checked=(hour>=23||hour<6);});}");
    writeHTMLChunk(stream, "let fileListData=[];let fileListSortCol='name';let fileListSortDir=1;");
    writeHTMLChunk(stream, "function formatDate(timestamp){if(!timestamp||timestamp===0)return'N/A';const d=new Date(timestamp);return d.toLocaleString();}");
    writeHTMLChunk(stream, "function sortFileList(col){if(fileListSortCol===col){fileListSortDir*=-1;}else{fileListSortCol=col;fileListSortDir=1;}renderFileList();}");
    writeHTMLChunk(stream, "function escapeHtml(str){const div=document.createElement('div');div.textContent=str;return div.innerHTML;}");
    writeHTMLChunk(stream, "function escapeJs(str){return str.replace(/\\\\/g,'\\\\\\\\').replace(/'/g,\"\\\\'\").replace(/\"/g,'\\\\\"').replace(/\\n/g,'\\\\n').replace(/\\r/g,'\\\\r');}");
    writeHTMLChunk(stream, "function renderFileList(){const list=document.getElementById('fileList');if(fileListData.length===0){list.innerHTML='<p>No files found on SD card.</p>';return;}const sorted=fileListData.slice().sort((a,b)=>{let valA,valB;if(fileListSortCol==='name'){valA=a.name.toLowerCase();valB=b.name.toLowerCase();}else if(fileListSortCol==='size'){valA=a.size;valB=b.size;}else if(fileListSortCol==='modified'){valA=a.modified||0;valB=b.modified||0;}return valA<valB?-1*fileListSortDir:valA>valB?1*fileListSortDir:0;});let html='<table style=\"width:100%;border-collapse:collapse;\"><thead><tr style=\"background:#f0f0f0;\">';html+=`<th style=\"padding:8px;text-align:left;cursor:pointer;\" onclick=\"sortFileList('name')\">Filename ${fileListSortCol==='name'?(fileListSortDir>0?'▲':'▼'):''}</th>`;html+=`<th style=\"padding:8px;text-align:right;cursor:pointer;\" onclick=\"sortFileList('size')\">Size ${fileListSortCol==='size'?(fileListSortDir>0?'▲':'▼'):''}</th>`;html+=`<th style=\"padding:8px;text-align:left;cursor:pointer;\" onclick=\"sortFileList('modified')\">Last Modified ${fileListSortCol==='modified'?(fileListSortDir>0?'▲':'▼'):''}</th>`;html+='<th style=\"padding:8px;text-align:center;width:140px;\">Actions</th></tr></thead><tbody>';sorted.forEach(f=>{const size=f.size>=1024*1024?(f.size/(1024*1024)).toFixed(2)+' MB':f.size>=1024?(f.size/1024).toFixed(2)+' KB':f.size+' B';const modified=formatDate(f.modified);const nameEscaped=escapeJs(f.name);const nameHtml=escapeHtml(f.name);html+=`<tr><td style=\"padding:8px;\">${nameHtml}</td><td style=\"padding:8px;text-align:right;\">${size}</td><td style=\"padding:8px;\">${modified}</td><td style=\"padding:8px;text-align:center;\"><button onclick=\"downloadFile('${nameEscaped}')\" style=\"margin:2px;padding:4px 8px;font-size:12px;\">Download</button><button onclick=\"deleteFile('${nameEscaped}')\" class=\"delete\" style=\"margin:2px;padding:4px 8px;font-size:12px;\">Delete</button></td></tr>`;});html+='</tbody></table>';list.innerHTML=html;}");
    writeHTMLChunk(stream, "function refreshFileList(){fetch('/api/files').then(r=>r.json()).then(files=>{fileListData=files;fileListSortCol='name';fileListSortDir=1;renderFileList();showStatus('fileStatus','File list refreshed',false);}).catch(e=>{showStatus('fileStatus','Error loading files: '+e,true);});}");
    writeHTMLChunk(stream, "function downloadFile(filename){window.location.href='/api/files/'+encodeURIComponent(filename);showStatus('fileStatus','Downloading '+filename,false);}");
    writeHTMLChunk(stream, "function deleteFile(filename){if(confirm('Delete '+filename+'?')){fetch('/api/files/'+encodeURIComponent(filename),{method:'DELETE'}).then(r=>r.json()).then(d=>{showStatus('fileStatus',d.success?'Deleted successfully':'Error: '+d.error,d.success?false:true);if(d.success)refreshFileList();}).catch(e=>showStatus('fileStatus','Error: '+e,true));}}");
    writeHTMLChunk(stream, "function handleFileSelect(event){const file=event.target.files[0];if(!file)return;showStatus('fileStatus','Reading '+file.name+'...',false);const reader=new FileReader();reader.onload=function(e){const base64Data=e.target.result;const base64Content=base64Data.substring(base64Data.indexOf(',')+1);const payload=JSON.stringify({filename:file.name,data:base64Content});showStatus('fileStatus','Uploading '+file.name+'...',false);fetch('/api/files/upload',{method:'POST',headers:{'Content-Type':'application/json'},body:payload}).then(r=>r.json()).then(d=>{showStatus('fileStatus',d.success?'Uploaded successfully ('+d.size+' bytes)':'Error: '+d.error,d.success?false:true);if(d.success){refreshFileList();document.getElementById('fileUpload').value='';}}).catch(e=>showStatus('fileStatus','Error: '+e,true));};reader.onerror=function(e){showStatus('fileStatus','Error reading file',true);};reader.readAsDataURL(file);}");
    writeHTMLChunk(stream, "function loadLog(){logFlush();fetch('/api/log').then(r=>r.text()).then(content=>{document.getElementById('logContent').textContent=content;showStatus('logStatus','Log loaded',false);}).catch(e=>{showStatus('logStatus','Error loading log: '+e,true);document.getElementById('logContent').textContent='Error: '+e;});}");
    writeHTMLChunk(stream, "function loadLogArchiveList(){fetch('/api/log/list').then(r=>r.json()).then(files=>{const listDiv=document.getElementById('logArchiveList');if(files.length===0){listDiv.innerHTML='<p style=\"color:#999;\">No archived log files found. Log rotation has not occurred yet.</p>';return;}let html='<p><strong>Recent archived logs (most recent first):</strong></p><div style=\"display:flex;flex-direction:column;gap:5px;\">';files.forEach((file,idx)=>{const filenameEscaped=file.filename.replace(/\"/g,'&quot;').replace(/'/g,'&#39;');html+='<button onclick=\"loadLogArchive(\\''+filenameEscaped+'\\')\" style=\"text-align:left;padding:8px;\">'+file.filename+' ('+formatFileSize(file.size)+')</button>';});html+='</div>';listDiv.innerHTML=html;showStatus('logStatus','Found '+files.length+' archived log file(s)',false);}).catch(e=>{showStatus('logStatus','Error loading archive list: '+e,true);document.getElementById('logArchiveList').innerHTML='<p style=\"color:red;\">Error: '+e+'</p>';});}");
    writeHTMLChunk(stream, "function loadLogArchive(filename){const url=filename?'/api/log/archive?file='+encodeURIComponent(filename):'/api/log/archive';fetch(url).then(r=>{if(!r.ok){return r.text().then(text=>{throw new Error(text);});}return r.text();}).then(content=>{document.getElementById('logContent').textContent=content;showStatus('logStatus',filename?'Archive log loaded: '+filename:'Archive log loaded',false);}).catch(e=>{showStatus('logStatus','Error loading archive: '+e,true);document.getElementById('logContent').textContent='Error: '+e;});}");
    writeHTMLChunk(stream, "function formatFileSize(bytes){if(bytes<1024)return bytes+' B';if(bytes<1024*1024)return (bytes/1024).toFixed(1)+' KB';return (bytes/(1024*1024)).toFixed(1)+' MB';}");
    writeHTMLChunk(stream, "function logFlush(){fetch('/api/log/flush',{method:'POST'});}");
    writeHTMLChunk(stream, "function closeServer(){if(confirm('Close management interface and return to normal operation?')){fetch('/api/close',{method:'POST'}).then(r=>r.json()).then(d=>{showStatus('closeStatus','Management interface closed. You can close this page.',false);setTimeout(()=>{window.location.href='about:blank';},2000);}).catch(e=>showStatus('closeStatus','Error: '+e,true));}}");
    writeHTMLChunk(stream, "function showStatus(id,msg,isError){const el=document.getElementById(id);el.textContent=msg;el.className=isError?'error status':'status';}");
    writeHTMLChunk(stream, "window.onload=function(){loadQuotes();loadFileLists();loadSettings();refreshFileList();};");
    writeHTMLChunk(stream, "</script></body></html>");
}
#endif // OLD HTML GENERATION CODE

#if 0
// Legacy function kept for compatibility (but should use streaming version)
static String generateManagementHTML() {
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>Device Management</title>";
    html += "<style>";
    html += "body{font-family:Arial,sans-serif;max-width:1200px;margin:0 auto;padding:20px;background:#f5f5f5;}";
    html += "h1{color:#333;border-bottom:2px solid #4CAF50;padding-bottom:10px;}";
    html += ".section{background:white;padding:20px;margin:20px 0;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1);}";
    html += "h2{color:#4CAF50;margin-top:0;}";
    html += "textarea{width:100%;min-height:200px;font-family:monospace;padding:10px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box;}";
    html += "input[type='number']{width:100px;padding:8px;border:1px solid #ddd;border-radius:4px;}";
    html += "select{width:100%;padding:8px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box;}";
    html += "button{background:#4CAF50;color:white;border:none;padding:10px 20px;border-radius:4px;cursor:pointer;font-size:16px;margin:5px;}";
    html += "button:hover{background:#45a049;}";
    html += "button.delete{background:#f44336;padding:5px 10px;font-size:12px;}";
    html += "button.delete:hover{background:#d32f2f;}";
    html += ".status{color:#4CAF50;margin:10px 0;font-weight:bold;}";
    html += ".error{color:#f44336;}";
    html += "label{display:block;margin:10px 0 5px 0;font-weight:bold;}";
    html += ".form-group{margin:15px 0;}";
    html += "td{padding:5px;}";
    html += "</style></head><body>";
    html += "<h1>Device Configuration Management</h1>";
    
    // Quotes section
    html += "<div class='section'><h2>Quotes Configuration (quotes.txt)</h2>";
    html += "<p>Format: Quote text on one or more lines, followed by ~Author on the next line. Separate quotes with blank lines.</p>";
    html += "<textarea id='quotesContent' placeholder='Loading quotes.txt...'></textarea><br>";
    html += "<button onclick='loadQuotes()'>Load from Device</button>";
    html += "<button onclick='saveQuotes()'>Save to Device</button>";
    html += "<div id='quotesStatus'></div></div>";
    
    // Media section with dropdowns
    html += "<div class='section'><h2>Media Mappings (media.txt)</h2>";
    html += "<p>Select image and audio file combinations. Leave audio empty for no audio. Check the 'Next' box to set which item will be displayed next. Click 'Show' to display the image and play audio on the panel.</p>";
    html += "<table id='mediaTable' style='width:100%;border-collapse:collapse;margin:10px 0;'>";
    html += "<thead><tr style='background:#f0f0f0;'><th style='padding:10px;text-align:left;width:30%;'>Image File</th><th style='padding:10px;text-align:left;width:30%;'>Audio File</th><th style='padding:10px;text-align:center;width:10%;'>Next</th><th style='padding:10px;text-align:center;width:15%;'>Actions</th><th style='padding:10px;width:15%;'></th></tr></thead>";
    html += "<tbody id='mediaRows'></tbody>";
    html += "</table>";
    html += "<button onclick='addMediaRow()'>Add New Row</button>";
    html += "<button onclick='loadMedia()'>Load from Device</button>";
    html += "<button onclick='saveMedia()'>Save to Device</button>";
    html += "<div id='mediaStatus'></div></div>";
    
    // Device settings section
    html += "<div class='section'><h2>Device Settings</h2>";
    html += "<div class='form-group'>";
    html += "<label>Volume (0-100%):</label>";
    html += "<input type='number' id='volume' min='0' max='100' value='50'>";
    html += "</div>";
    html += "<div class='form-group'>";
    html += "<label>Sleep Interval (minutes, must be factor of 60):</label>";
    html += "<input type='number' id='sleepInterval' min='1' max='60' value='1'>";
    html += "</div>";
    html += "<div class='form-group'>";
    html += "<label>Hour Schedule (check hours when device should wake):</label>";
    html += "<div style='display:flex;flex-wrap:wrap;gap:10px;margin:10px 0;padding:10px;background:#f9f9f9;border-radius:4px;'>";
    for (int i = 0; i < 24; i++) {
        html += "<label style='display:flex;align-items:center;gap:5px;cursor:pointer;'><input type='checkbox' class='hourCheckbox' data-hour='";
        html += i;
        html += "' style='cursor:pointer;'>";
        html += String(i < 10 ? "0" : "") + String(i) + ":00";
        html += "</label>";
    }
    html += "</div>";
    html += "<div style='margin-top:10px;'><button onclick='selectAllHours()'>Select All</button>";
    html += "<button onclick='deselectAllHours()'>Deselect All</button>";
    html += "<button onclick='selectNightHours()'>Select Night (6am-11pm)</button>";
    html += "<button onclick='selectDayHours()'>Select Day (11pm-6am)</button></div>";
    html += "</div>";
    html += "<button onclick='loadSettings()'>Load from Device</button>";
    html += "<button onclick='saveSettings()'>Save to Device</button>";
    html += "<div id='settingsStatus'></div></div>";
    
    // File management section
    html += "<div class='section'><h2>File Management</h2>";
    html += "<p>Upload, download, and delete files on the SD card.</p>";
    html += "<div style='margin:10px 0;'><input type='file' id='fileUpload' style='display:none;' onchange='handleFileSelect(event)'><button onclick='document.getElementById(\"fileUpload\").click()'>Upload File</button>";
    html += "<button onclick='refreshFileList()'>Refresh File List</button></div>";
    html += "<div id='fileList' style='margin:10px 0;'>Loading files...</div>";
    html += "<div id='fileStatus'></div></div>";
    
    // Log viewing section
    html += "<div class='section'><h2>System Log</h2>";
    html += "<p>View the current system log file (read-only).</p>";
    html += "<button onclick='loadLog()'>Load Current Log</button>";
    html += "<button onclick='loadLogArchiveList()'>Load Previous Log</button>";
    html += "<div id='logArchiveList' style='margin-top:10px;'></div>";
    html += "<div id='logContent' style='margin:10px 0;max-height:600px;overflow-y:auto;background:#f9f9f9;padding:10px;border:1px solid #ddd;border-radius:4px;font-family:monospace;font-size:12px;white-space:pre-wrap;word-wrap:break-word;'></div>";
    html += "<div id='logStatus'></div></div>";
    
    // Close server button
    html += "<div class='section'><h2>Server Control</h2>";
    html += "<p>Close the management interface and return to normal operation.</p>";
    html += "<button onclick='closeServer()' style='background:#f44336;'>Close Management Interface</button>";
    html += "<div id='closeStatus'></div></div>";
    
    // JavaScript
    html += "<script>";
    html += "function loadQuotes(){fetch('/api/quotes').then(r=>r.text()).then(t=>{document.getElementById('quotesContent').value=t;showStatus('quotesStatus','Loaded successfully',false);}).catch(e=>showStatus('quotesStatus','Error: '+e,true));}";
    html += "function saveQuotes(){const content=document.getElementById('quotesContent').value;fetch('/api/quotes',{method:'POST',body:content}).then(r=>r.json()).then(d=>{showStatus('quotesStatus',d.success?'Saved successfully':'Error: '+d.error,d.success?false:true);if(d.success)loadQuotes();}).catch(e=>showStatus('quotesStatus','Error: '+e,true));}";
    html += "let imageFiles=[];let audioFiles=[];let filesLoaded=0;";
    html += "function checkAndLoadMedia(){if(filesLoaded>=2){loadMedia();}else if(filesLoaded===1){showStatus('mediaStatus','Warning: Only partial file list loaded',true);}}";
    html += "function loadFileLists(){filesLoaded=0;fetch('/api/images').then(r=>r.json()).then(f=>{imageFiles=f;filesLoaded++;checkAndLoadMedia();}).catch(e=>{showStatus('mediaStatus','Error loading images: '+e,true);filesLoaded++;checkAndLoadMedia();});fetch('/api/audio').then(r=>r.json()).then(f=>{audioFiles=f;filesLoaded++;checkAndLoadMedia();}).catch(e=>{showStatus('mediaStatus','Error loading audio: '+e,true);filesLoaded++;checkAndLoadMedia();});}";
    html += "let showInProgress=false;function createMediaRow(image='',audio='',isNext=false){const row=document.createElement('tr');row.dataset.index=document.getElementById('mediaRows').children.length;const imgCell=document.createElement('td');const imgSelect=document.createElement('select');imgSelect.className='imageSelect';imgSelect.innerHTML='<option value=\"\">-- Select Image --</option>';imageFiles.forEach(f=>{const opt=document.createElement('option');opt.value=f;opt.text=f;opt.selected=(f===image);imgSelect.appendChild(opt);});imgCell.appendChild(imgSelect);const audCell=document.createElement('td');const audSelect=document.createElement('select');audSelect.className='audioSelect';audSelect.innerHTML='<option value=\"\">(none)</option>';audioFiles.forEach(f=>{const opt=document.createElement('option');opt.value=f;opt.text=f;opt.selected=(f===audio);audSelect.appendChild(opt);});audCell.appendChild(audSelect);const nextCell=document.createElement('td');nextCell.style.textAlign='center';const nextCheck=document.createElement('input');nextCheck.type='checkbox';nextCheck.className='nextCheckbox';nextCheck.checked=isNext;nextCheck.onchange=function(){document.querySelectorAll('.nextCheckbox').forEach(cb=>{if(cb!==nextCheck)cb.checked=false;});};nextCell.appendChild(nextCheck);const actionCell=document.createElement('td');actionCell.style.textAlign='center';const showBtn=document.createElement('button');showBtn.className='showBtn';showBtn.textContent='Show';showBtn.style.margin='2px';showBtn.style.padding='4px 8px';showBtn.style.fontSize='12px';showBtn.disabled=showInProgress;showBtn.onclick=function(){if(showInProgress){alert('Another show operation is in progress. Please wait.');return;}const idx=parseInt(row.dataset.index);showMediaItem(idx);};actionCell.appendChild(showBtn);const delCell=document.createElement('td');const delBtn=document.createElement('button');delBtn.className='delete';delBtn.textContent='Delete';delBtn.onclick=function(){row.remove();updateRowIndices();};delCell.appendChild(delBtn);row.appendChild(imgCell);row.appendChild(audCell);row.appendChild(nextCell);row.appendChild(actionCell);row.appendChild(delCell);return row;}";
    html += "function showMediaItem(index){if(showInProgress){return;}showInProgress=true;document.querySelectorAll('.showBtn').forEach(btn=>btn.disabled=true);showStatus('mediaStatus','Displaying image and playing audio (this will take 20-30 seconds)...',false);fetch('/api/media/show?index='+index,{method:'POST'}).then(r=>r.json()).then(d=>{showInProgress=false;document.querySelectorAll('.showBtn').forEach(btn=>btn.disabled=false);if(d.success){showStatus('mediaStatus','Display updated successfully. Next item: '+(d.nextIndex+1),false);loadMedia();}else{showStatus('mediaStatus','Error: '+d.error,true);}}).catch(e=>{showInProgress=false;document.querySelectorAll('.showBtn').forEach(btn=>btn.disabled=false);showStatus('mediaStatus','Error: '+e,true);});}";
    html += "function updateRowIndices(){const rows=document.querySelectorAll('#mediaRows tr');rows.forEach((row,idx)=>{row.dataset.index=idx;});}";
    html += "function addMediaRow(){const tbody=document.getElementById('mediaRows');tbody.appendChild(createMediaRow());}";
    html += "function loadMedia(){Promise.all([fetch('/api/media').then(r=>r.text()),fetch('/api/media/index').then(r=>r.json())]).then(([content,indexData])=>{const tbody=document.getElementById('mediaRows');tbody.innerHTML='';const lastDisplayedIndex=indexData.index||0;const lines=content.split('\\n');const mediaCount=lines.filter(l=>{l=l.trim();return l.length>0&&!l.startsWith('#');}).length;const nextIndex=(lastDisplayedIndex+1)%mediaCount;let lineIdx=0;lines.forEach((line,idx)=>{line=line.trim();if(line.length===0||line.startsWith('#'))return;const comma=line.indexOf(',');const isNext=(lineIdx===nextIndex);if(comma>0){const img=line.substring(0,comma).trim();const aud=line.substring(comma+1).trim();tbody.appendChild(createMediaRow(img,aud,isNext));}else if(line.length>0){tbody.appendChild(createMediaRow(line,'',isNext));}lineIdx++;});updateRowIndices();if(tbody.children.length===0)addMediaRow();showStatus('mediaStatus','Loaded successfully',false);}).catch(e=>{showStatus('mediaStatus','Error: '+e,true);});}";
    html += "function saveMedia(){const rows=document.querySelectorAll('#mediaRows tr');let content='';let nextIndex=-1;rows.forEach((row,idx)=>{const img=row.querySelector('.imageSelect').value;const aud=row.querySelector('.audioSelect').value;const isNext=row.querySelector('.nextCheckbox').checked;if(isNext)nextIndex=idx;if(img.length>0){content+=img;if(aud.length>0)content+=','+aud;content+='\\n';}});const saveData={content:content,nextIndex:nextIndex>=0?nextIndex:null};fetch('/api/media',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(saveData)}).then(r=>r.json()).then(d=>{showStatus('mediaStatus',d.success?'Saved successfully':'Error: '+d.error,d.success?false:true);if(d.success)loadMedia();}).catch(e=>showStatus('mediaStatus','Error: '+e,true));}";
    html += "function loadSettings(){fetch('/api/settings').then(r=>r.json()).then(d=>{document.getElementById('volume').value=d.volume;document.getElementById('sleepInterval').value=d.sleepInterval;if(d.hourSchedule){const schedule=d.hourSchedule;document.querySelectorAll('.hourCheckbox').forEach(cb=>{const hour=parseInt(cb.dataset.hour);cb.checked=schedule[hour]==='1'||schedule[hour]===true;});}showStatus('settingsStatus','Loaded successfully',false);}).catch(e=>showStatus('settingsStatus','Error: '+e,true));}";
    html += "function saveSettings(){const hourSchedule=[];document.querySelectorAll('.hourCheckbox').forEach(cb=>{hourSchedule.push(cb.checked?'1':'0');});const json=JSON.stringify({volume:parseInt(document.getElementById('volume').value),sleepInterval:parseInt(document.getElementById('sleepInterval').value),hourSchedule:hourSchedule.join('')});fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:json}).then(r=>r.json()).then(d=>{showStatus('settingsStatus',d.success?'Saved successfully':'Error: '+d.error,d.success?false:true);if(d.success)loadSettings();}).catch(e=>showStatus('settingsStatus','Error: '+e,true));}";
    html += "function selectAllHours(){document.querySelectorAll('.hourCheckbox').forEach(cb=>cb.checked=true);}";
    html += "function deselectAllHours(){document.querySelectorAll('.hourCheckbox').forEach(cb=>cb.checked=false);}";
    html += "function selectNightHours(){document.querySelectorAll('.hourCheckbox').forEach(cb=>{const hour=parseInt(cb.dataset.hour);cb.checked=(hour>=6&&hour<23);});}";
    html += "function selectDayHours(){document.querySelectorAll('.hourCheckbox').forEach(cb=>{const hour=parseInt(cb.dataset.hour);cb.checked=(hour>=23||hour<6);});}";
    html += "let fileListData=[];let fileListSortCol='name';let fileListSortDir=1;";
    html += "function formatDate(timestamp){if(!timestamp||timestamp===0)return'N/A';const d=new Date(timestamp);return d.toLocaleString();}";
    html += "function sortFileList(col){if(fileListSortCol===col){fileListSortDir*=-1;}else{fileListSortCol=col;fileListSortDir=1;}renderFileList();}";
    html += "function escapeHtml(str){const div=document.createElement('div');div.textContent=str;return div.innerHTML;}";
    html += "function escapeJs(str){return str.replace(/\\\\/g,'\\\\\\\\').replace(/'/g,\"\\\\'\").replace(/\"/g,'\\\\\"').replace(/\\n/g,'\\\\n').replace(/\\r/g,'\\\\r');}";
    html += "function renderFileList(){const list=document.getElementById('fileList');if(fileListData.length===0){list.innerHTML='<p>No files found on SD card.</p>';return;}const sorted=fileListData.slice().sort((a,b)=>{let valA,valB;if(fileListSortCol==='name'){valA=a.name.toLowerCase();valB=b.name.toLowerCase();}else if(fileListSortCol==='size'){valA=a.size;valB=b.size;}else if(fileListSortCol==='modified'){valA=a.modified||0;valB=b.modified||0;}return valA<valB?-1*fileListSortDir:valA>valB?1*fileListSortDir:0;});let html='<table style=\"width:100%;border-collapse:collapse;\"><thead><tr style=\"background:#f0f0f0;\">';html+=`<th style=\"padding:8px;text-align:left;cursor:pointer;\" onclick=\"sortFileList('name')\">Filename ${fileListSortCol==='name'?(fileListSortDir>0?'▲':'▼'):''}</th>`;html+=`<th style=\"padding:8px;text-align:right;cursor:pointer;\" onclick=\"sortFileList('size')\">Size ${fileListSortCol==='size'?(fileListSortDir>0?'▲':'▼'):''}</th>`;html+=`<th style=\"padding:8px;text-align:left;cursor:pointer;\" onclick=\"sortFileList('modified')\">Last Modified ${fileListSortCol==='modified'?(fileListSortDir>0?'▲':'▼'):''}</th>`;html+='<th style=\"padding:8px;text-align:center;width:140px;\">Actions</th></tr></thead><tbody>';sorted.forEach(f=>{const size=f.size>=1024*1024?(f.size/(1024*1024)).toFixed(2)+' MB':f.size>=1024?(f.size/1024).toFixed(2)+' KB':f.size+' B';const modified=formatDate(f.modified);const nameEscaped=escapeJs(f.name);const nameHtml=escapeHtml(f.name);html+=`<tr><td style=\"padding:8px;\">${nameHtml}</td><td style=\"padding:8px;text-align:right;\">${size}</td><td style=\"padding:8px;\">${modified}</td><td style=\"padding:8px;text-align:center;\"><button onclick=\"downloadFile('${nameEscaped}')\" style=\"margin:2px;padding:4px 8px;font-size:12px;\">Download</button><button onclick=\"deleteFile('${nameEscaped}')\" class=\"delete\" style=\"margin:2px;padding:4px 8px;font-size:12px;\">Delete</button></td></tr>`;});html+='</tbody></table>';list.innerHTML=html;}";
    html += "function refreshFileList(){fetch('/api/files').then(r=>r.json()).then(files=>{fileListData=files;fileListSortCol='name';fileListSortDir=1;renderFileList();showStatus('fileStatus','File list refreshed',false);}).catch(e=>{showStatus('fileStatus','Error loading files: '+e,true);});}";
    html += "function downloadFile(filename){window.location.href='/api/files/'+encodeURIComponent(filename);showStatus('fileStatus','Downloading '+filename,false);}";
    html += "function deleteFile(filename){if(confirm('Delete '+filename+'?')){fetch('/api/files/'+encodeURIComponent(filename),{method:'DELETE'}).then(r=>r.json()).then(d=>{showStatus('fileStatus',d.success?'Deleted successfully':'Error: '+d.error,d.success?false:true);if(d.success)refreshFileList();}).catch(e=>showStatus('fileStatus','Error: '+e,true));}}";
    html += "function handleFileSelect(event){const file=event.target.files[0];if(!file)return;showStatus('fileStatus','Reading '+file.name+'...',false);const reader=new FileReader();reader.onload=function(e){const base64Data=e.target.result;const base64Content=base64Data.substring(base64Data.indexOf(',')+1);const payload=JSON.stringify({filename:file.name,data:base64Content});showStatus('fileStatus','Uploading '+file.name+'...',false);fetch('/api/files/upload',{method:'POST',headers:{'Content-Type':'application/json'},body:payload}).then(r=>r.json()).then(d=>{showStatus('fileStatus',d.success?'Uploaded successfully ('+d.size+' bytes)':'Error: '+d.error,d.success?false:true);if(d.success){refreshFileList();document.getElementById('fileUpload').value='';}}).catch(e=>showStatus('fileStatus','Error: '+e,true));};reader.onerror=function(e){showStatus('fileStatus','Error reading file',true);};reader.readAsDataURL(file);}";
    html += "function loadLog(){logFlush();fetch('/api/log').then(r=>r.text()).then(content=>{document.getElementById('logContent').textContent=content;showStatus('logStatus','Log loaded',false);}).catch(e=>{showStatus('logStatus','Error loading log: '+e,true);document.getElementById('logContent').textContent='Error: '+e;});}";
    html += "function loadLogArchiveList(){fetch('/api/log/list').then(r=>r.json()).then(files=>{const listDiv=document.getElementById('logArchiveList');if(files.length===0){listDiv.innerHTML='<p style=\"color:#999;\">No archived log files found. Log rotation has not occurred yet.</p>';return;}let html='<p><strong>Recent archived logs (most recent first):</strong></p><div style=\"display:flex;flex-direction:column;gap:5px;\">';files.forEach((file,idx)=>{const filenameEscaped=file.filename.replace(/\"/g,'&quot;').replace(/'/g,'&#39;');html+='<button onclick=\"loadLogArchive(\\''+filenameEscaped+'\\')\" style=\"text-align:left;padding:8px;\">'+file.filename+' ('+formatFileSize(file.size)+')</button>';});html+='</div>';listDiv.innerHTML=html;showStatus('logStatus','Found '+files.length+' archived log file(s)',false);}).catch(e=>{showStatus('logStatus','Error loading archive list: '+e,true);document.getElementById('logArchiveList').innerHTML='<p style=\"color:red;\">Error: '+e+'</p>';});}";
    html += "function loadLogArchive(filename){const url=filename?'/api/log/archive?file='+encodeURIComponent(filename):'/api/log/archive';fetch(url).then(r=>{if(!r.ok){return r.text().then(text=>{throw new Error(text);});}return r.text();}).then(content=>{document.getElementById('logContent').textContent=content;showStatus('logStatus',filename?'Archive log loaded: '+filename:'Archive log loaded',false);}).catch(e=>{showStatus('logStatus','Error loading archive: '+e,true);document.getElementById('logContent').textContent='Error: '+e;});}";
    html += "function formatFileSize(bytes){if(bytes<1024)return bytes+' B';if(bytes<1024*1024)return (bytes/1024).toFixed(1)+' KB';return (bytes/(1024*1024)).toFixed(1)+' MB';}";
    html += "function logFlush(){fetch('/api/log/flush',{method:'POST'});}";
    html += "function closeServer(){if(confirm('Close management interface and return to normal operation?')){fetch('/api/close',{method:'POST'}).then(r=>r.json()).then(d=>{showStatus('closeStatus','Management interface closed. You can close this page.',false);setTimeout(()=>{window.location.href='about:blank';},2000);}).catch(e=>showStatus('closeStatus','Error: '+e,true));}}";
    html += "function showStatus(id,msg,isError){const el=document.getElementById(id);el.textContent=msg;el.className=isError?'error status':'status';}";
    html += "window.onload=function(){loadQuotes();loadFileLists();loadSettings();refreshFileList();};";
    html += "</script></body></html>";
    
    return html;
}
#endif // OLD HTML GENERATION CODE



/**
 * Handle !clear command - clear the e-ink display
 */
bool handleClearCommand() {
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
    display.update();  // Uses mutex to ensure _sendBuffer() completes before returning
    Serial.println("Display cleared and updated");
    
    // Save framebuffer to storage partition after display update
    // safeDisplayUpdate() ensures _sendBuffer() has completed, so the buffer is safe to read
    
    return true;  // Command handled successfully
}

/**
 * Handle !manage command - launch blocking web interface for configuration
 */
// Task function to run show media operation in a separate task with larger stack
static void show_media_task(void* parameter) {
    ShowMediaTaskData* data = (ShowMediaTaskData*)parameter;
    
    Serial.printf("Show media task started for index %d\n", data->index);
    
    // Use unified command dispatcher
    String jsonPayload = "{\"index\":\"" + String(data->index) + "\"}";
    
        CommandContext ctx;
        ctx.source = CommandSource::HTTP_API;
        ctx.command = "/api/media/show";
        ctx.originalMessage = jsonPayload;
        ctx.senderNumber = "";
        ctx.commandId = "";  // HTTP API doesn't use command IDs
        ctx.requiresAuth = false;  // HTTP API handles auth separately if needed
        ctx.shouldPublishCompletion = false;  // HTTP API handles responses directly
        
        bool dispatchResult = dispatchCommand(ctx);
    
    // The dispatcher calls handleShowCommand, which should handle the display logic
    // But we still need to set nextIndex for the HTTP response
    // Calculate nextIndex based on current index
    size_t nextIndex = 0;
    if (g_media_mappings_loaded && g_media_mappings.size() > 0) {
        nextIndex = (data->index + 1) % g_media_mappings.size();
    }
    
    *(data->success) = dispatchResult;
    *(data->nextIndex) = nextIndex;
    
    // Signal completion
    xSemaphoreGive(data->completionSem);
    vTaskDelete(NULL);
    return;
    
    // Original implementation below (kept for reference, but now using dispatcher above)
    bool success = false;
    size_t nextIndex_old = 0;
    
    // Ensure display is initialized
    bool displayOk = true;
    if (display.getBuffer() == nullptr) {
        Serial.println("Display not initialized - initializing now...");
        displaySPI.begin(PIN_SPI_SCK, -1, PIN_SPI_MOSI, -1);
        if (!display.begin(PIN_CS0, PIN_CS1, PIN_DC, PIN_RESET, PIN_BUSY)) {
            Serial.println("ERROR: Display initialization failed!");
            displayOk = false;
        } else {
            Serial.println("Display initialized");
        }
    }
    
    if (displayOk) {

        // Mount SD card if needed
        if (!sdCardMounted) {
            Serial.println("Mounting SD card...");
            if (!sdInitDirect(false)) {
                Serial.println("ERROR: Failed to mount SD card!");
                displayOk = false;
            }
        }
        
        // Load configuration files from SD card if needed
        if (displayOk) {
            if (!g_quotes_loaded) {
                loadQuotesFromSD();
            }
            if (!g_media_mappings_loaded) {
                loadMediaMappingsFromSD(false);  // Don't auto-publish in command handlers
            }
            
            // Check if we have media mappings
            if (!g_media_mappings_loaded || g_media_mappings.size() == 0) {
                Serial.println("ERROR: No media.txt mappings found");
                displayOk = false;
            } else {
                // Validate index is within bounds
                size_t mediaCount = g_media_mappings.size();
                if (data->index >= (int)mediaCount) {
                    Serial.printf("ERROR: Index %d is out of bounds. Valid range: 0 to %zu\n", 
                                  data->index, mediaCount - 1);
                    displayOk = false;
                } else {
                    // Set the index so that pngDrawFromMediaMappings will show this item
                    lastMediaIndex = (data->index - 1 + mediaCount) % mediaCount;
                    
                    // Draw the image
                    uint32_t sd_ms = 0, dec_ms = 0;
                    bool ok = pngDrawFromMediaMappings(&sd_ms, &dec_ms);
                    if (!ok) {
                        Serial.println("ERROR: Failed to load image from media.txt");
                        displayOk = false;
                    } else {
                        // Verify we're at the correct index
                        if (lastMediaIndex != (size_t)data->index) {
                            Serial.printf("WARNING: Expected index %d but got %lu - correcting\n", 
                                          data->index, (unsigned long)lastMediaIndex);
                            lastMediaIndex = data->index;
                        }
                        
                        Serial.printf("PNG SD read: %lu ms, decode+draw: %lu ms\n", 
                                     (unsigned long)sd_ms, (unsigned long)dec_ms);
                        
                        // Add text overlay (time/date/weather/quote) - centralized function
                        addTextOverlayToDisplay(&display, &ttf, 100);
                        
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
                        
                        // Calculate next index (the one after the displayed one, wrapping)
                        nextIndex = (lastMediaIndex + 1) % mediaCount;
                        // Set lastMediaIndex to nextIndex-1 so that next call will show nextIndex
                        lastMediaIndex = (nextIndex - 1 + mediaCount) % mediaCount;
                        mediaIndexSaveToNVS();
                        
                        Serial.printf("Show operation completed - next item will be index %zu\n", nextIndex);
                        success = true;
                    }
                }
            }
        }
    }
    
    // Store results
    *data->success = success;
    *data->nextIndex = nextIndex;
    
    // Signal completion
    xSemaphoreGive(data->completionSem);
    
    // Delete this task
    vTaskDelete(nullptr);
}

bool handleManageCommand() {
    Serial.println("Processing !manage command...");
    
    // Ensure WiFi is connected
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected - attempting to connect...");
        if (!wifiConnectPersistent(5, 10000, false)) {
            Serial.println("ERROR: Failed to connect to WiFi");
            return false;
        }
    }
    
    // Ensure SD card is mounted
    if (!sdCardMounted) {
        Serial.println("SD card not mounted - attempting to mount...");
        if (!sdInitDirect()) {
            Serial.println("ERROR: Failed to mount SD card");
            return false;
        }
    }
    
    // Create server - following example pattern exactly
    #ifdef PSY_ENABLE_SSL
    PsychicHttpsServer server(443);
    // Set certificate and private key from certificates.h
    server.setCertificate(server_cert, server_key);
    Serial.println("HTTPS server configured with SSL certificate");
    #ifdef CONFIG_ESP_HTTPS_SERVER_ENABLE
    Serial.println("BUILD CHECK: PSY_ENABLE_SSL=1, CONFIG_ESP_HTTPS_SERVER_ENABLE=1");
    #else
    Serial.println("BUILD CHECK: PSY_ENABLE_SSL=1, CONFIG_ESP_HTTPS_SERVER_ENABLE=0 (WARNING!)");
    #endif
    #else
    PsychicHttpServer server(80);
    Serial.println("HTTP server (HTTPS not available - PSY_ENABLE_SSL not defined)");
    Serial.println("BUILD CHECK: PSY_ENABLE_SSL is NOT defined");
    #endif
    
    // Increase max request body size for file uploads (default is 16KB)
    // ESP32-P4 has PSRAM, so we can handle larger requests
    // 512KB allows most files to upload in one request
    // For larger files (>512KB), use chunked uploads
    server.maxRequestBodySize = MAX_REQUEST_BODY_SIZE;  // 1MB (for canvas pixel data: 800x600 = 480KB raw, ~640KB base64)
    Serial.printf("HTTP server max request body size set to %lu bytes\n", server.maxRequestBodySize);
    
    // Increase HTTP server task stack size to handle large file uploads
    // Default is 4608 bytes, increase to 32KB to prevent stack overflow
    // This is set directly on the config before server starts
    server.config.stack_size = 32 * 1024;  // 32KB stack
    Serial.printf("HTTP server task stack size set to %lu bytes\n", server.config.stack_size);
    
    // Flag to control server shutdown
    bool serverShouldClose = false;
    
    // Helper function to add CORS headers to API responses
    auto addCorsHeaders = [](PsychicResponse *response) {
        response->addHeader("Access-Control-Allow-Origin", "*");
        response->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    };
    
    // Handle OPTIONS requests for CORS preflight
    server.on("*", HTTP_OPTIONS, [addCorsHeaders](PsychicRequest *request, PsychicResponse *response) {
        addCorsHeaders(response);
        return response->send(200);
    });
    
    // Set up all route handlers
    // Root path - serve HTML management interface
    // HTML is embedded from web/index.html at compile time (no stack issues)
    server.on("/", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) {
        Serial.println("GET / - Serving embedded HTML page...");
        // HTML is embedded in flash memory, safe to send directly
        return response->send(200, "text/html", WEB_HTML_CONTENT);
    });
    
    // Handle favicon requests (browsers request this automatically)
    // Return 204 No Content to prevent handshake errors
    server.on("/favicon.ico", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) {
        return response->send(204);  // No Content - browser will use default
    });
    
    // Handle robots.txt (some browsers/crawlers request this)
    server.on("/robots.txt", HTTP_GET, [](PsychicRequest *request, PsychicResponse *response) {
        return response->send(200, "text/plain", "User-agent: *\nDisallow: /\n");
    });
    
    // GET /api/quotes - Read quotes.txt as structured JSON
    server.on("/api/quotes", HTTP_GET, [addCorsHeaders](PsychicRequest *request, PsychicResponse *response) {
        addCorsHeaders(response);
        
        // Load quotes from SD if not already loaded
        if (!g_quotes_loaded) {
            loadQuotesFromSD();
        }
        
        // Build JSON array of quotes
        String json = "[";
        bool first = true;
        for (size_t i = 0; i < g_loaded_quotes.size(); i++) {
            if (!first) json += ",";
            json += "{\"quote\":\"";
            // Escape JSON string
            String quote = g_loaded_quotes[i].text;
            quote.replace("\\", "\\\\");
            quote.replace("\"", "\\\"");
            quote.replace("\n", "\\n");
            quote.replace("\r", "\\r");
            json += quote;
            json += "\",\"author\":\"";
            String author = g_loaded_quotes[i].author;
            author.replace("\\", "\\\\");
            author.replace("\"", "\\\"");
            author.replace("\n", "\\n");
            author.replace("\r", "\\r");
            json += author;
            json += "\"}";
            first = false;
        }
        json += "]";
        
        return response->send(200, "application/json", json.c_str());
    });
    
    // GET /api/media/index - Get current media index
    server.on("/api/media/index", HTTP_GET, [addCorsHeaders](PsychicRequest *request, PsychicResponse *response) {
        addCorsHeaders(response);
        String json = "{\"index\":";
        json += String((unsigned long)lastMediaIndex);
        json += "}";
        return response->send(200, "application/json", json.c_str());
    });
    
    // POST /api/text/display - Display text with specified color
    server.on("/api/text/display", HTTP_POST, [addCorsHeaders](PsychicRequest *request, PsychicResponse *response) {
        addCorsHeaders(response);
        
        // Check if another show operation is in progress
        if (showOperationInProgress) {
            String resp = "{\"success\":false,\"error\":\"Another show operation is already in progress\"}";
            return response->send(409, "application/json", resp.c_str());
        }
        
        String jsonPayload = request->body();
        
        // Parse JSON to extract text, color, background color, and outline color
        String textToDisplay = "";
        String colorStr = "white";
        String bgColorStr = "white";
        String outlineColorStr = "black";
        
        // Extract text
        int textPos = jsonPayload.indexOf("\"text\"");
        if (textPos >= 0) {
            int colonPos = jsonPayload.indexOf(':', textPos);
            int quoteStart = jsonPayload.indexOf('"', colonPos);
            if (quoteStart >= 0) {
                quoteStart++;  // Skip opening quote
                int quoteEnd = quoteStart;
                while (quoteEnd < (int)jsonPayload.length()) {
                    if (jsonPayload.charAt(quoteEnd) == '"' && (quoteEnd == 0 || jsonPayload.charAt(quoteEnd - 1) != '\\')) {
                        break;
                    }
                    quoteEnd++;
                }
                if (quoteEnd > quoteStart) {
                    textToDisplay = jsonPayload.substring(quoteStart, quoteEnd);
                    // Unescape JSON string
                    textToDisplay.replace("\\n", "\n");
                    textToDisplay.replace("\\r", "\r");
                    textToDisplay.replace("\\t", "\t");
                    textToDisplay.replace("\\\"", "\"");
                    textToDisplay.replace("\\\\", "\\");
                }
            }
        }
        
        // Extract color
        int colorPos = jsonPayload.indexOf("\"color\"");
        if (colorPos >= 0) {
            int colonPos = jsonPayload.indexOf(':', colorPos);
            int quoteStart = jsonPayload.indexOf('"', colonPos);
            if (quoteStart >= 0) {
                quoteStart++;
                int quoteEnd = jsonPayload.indexOf('"', quoteStart);
                if (quoteEnd > quoteStart) {
                    colorStr = jsonPayload.substring(quoteStart, quoteEnd);
                    colorStr.toLowerCase();
                }
            }
        }
        
        // Extract background color (British spelling: "backgroundColour")
        int bgColorPos = jsonPayload.indexOf("\"backgroundColour\"");
        if (bgColorPos >= 0) {
            int colonPos = jsonPayload.indexOf(':', bgColorPos);
            int quoteStart = jsonPayload.indexOf('"', colonPos);
            if (quoteStart >= 0) {
                quoteStart++;
                int quoteEnd = jsonPayload.indexOf('"', quoteStart);
                if (quoteEnd > quoteStart) {
                    bgColorStr = jsonPayload.substring(quoteStart, quoteEnd);
                    bgColorStr.toLowerCase();
                }
            }
        }
        
        // Extract outline color (British spelling: "outlineColour")
        int outlineColorPos = jsonPayload.indexOf("\"outlineColour\"");
        if (outlineColorPos >= 0) {
            int colonPos = jsonPayload.indexOf(':', outlineColorPos);
            int quoteStart = jsonPayload.indexOf('"', colonPos);
            if (quoteStart >= 0) {
                quoteStart++;
                int quoteEnd = jsonPayload.indexOf('"', quoteStart);
                if (quoteEnd > quoteStart) {
                    outlineColorStr = jsonPayload.substring(quoteStart, quoteEnd);
                    outlineColorStr.toLowerCase();
                }
            }
        }
        
        if (textToDisplay.length() == 0) {
            String resp = "{\"success\":false,\"error\":\"Invalid JSON: missing text\"}";
            return response->send(400, "application/json", resp.c_str());
        }
        
        Serial.printf("Text display: text=\"%s\", color=%s, background=%s, outline=%s\n", textToDisplay.c_str(), colorStr.c_str(), bgColorStr.c_str(), outlineColorStr.c_str());
        
        // Set lock
        showOperationInProgress = true;
        
        // Prepare task data
        struct {
            String* text;
            String* color;
            String* bgColor;
            String* outlineColor;
        } taskData;
        
        String textCopy = textToDisplay;
        String colorCopy = colorStr;
        String bgColorCopy = bgColorStr;
        String outlineColorCopy = outlineColorStr;
        taskData.text = &textCopy;
        taskData.color = &colorCopy;
        taskData.bgColor = &bgColorCopy;
        taskData.outlineColor = &outlineColorCopy;
        
        // Create task to display text (runs in background)
        xTaskCreate([](void* param) {
            auto* data = (decltype(taskData)*)param;
            
            Serial.println("Text display: Starting display task...");
            
            // Initialize display if not already initialized
            if (display.getBuffer() == nullptr) {
                Serial.println("Text display: Display not initialized - initializing now...");
                displaySPI.begin(PIN_SPI_SCK, -1, PIN_SPI_MOSI, -1);
                if (!display.begin(PIN_CS0, PIN_CS1, PIN_DC, PIN_RESET, PIN_BUSY)) {
                    Serial.println("Text display: ERROR - Display initialization failed!");
                    showOperationInProgress = false;
                    vTaskDelete(NULL);
                    return;
                }
                Serial.println("Text display: Display initialized successfully");
            }
            
            // Convert background color string to e-ink color value
            uint8_t bgColor = EL133UF1_WHITE;
            if (*(data->bgColor) == "black") {
                bgColor = EL133UF1_BLACK;
            } else if (*(data->bgColor) == "yellow") {
                bgColor = EL133UF1_YELLOW;
            } else if (*(data->bgColor) == "red") {
                bgColor = EL133UF1_RED;
            } else if (*(data->bgColor) == "blue") {
                bgColor = EL133UF1_BLUE;
            } else if (*(data->bgColor) == "green") {
                bgColor = EL133UF1_GREEN;
            } else {
                // Default: white
                bgColor = EL133UF1_WHITE;
            }
            
            // Use unified command dispatcher
            // Construct JSON payload for dispatcher
            String jsonPayload = "{";
            jsonPayload += "\"text\":\"";
            // Escape quotes in text
            String escapedText = *(data->text);
            escapedText.replace("\"", "\\\"");
            jsonPayload += escapedText;
            jsonPayload += "\",\"color\":\"";
            jsonPayload += *(data->color);
            jsonPayload += "\",\"backgroundColour\":\"";
            jsonPayload += *(data->bgColor);
            jsonPayload += "\",\"outlineColour\":\"";
            jsonPayload += *(data->outlineColor);
            jsonPayload += "\"}";
            
            CommandContext ctx;
            ctx.source = CommandSource::HTTP_API;
            ctx.command = "/api/text/display";
            ctx.originalMessage = jsonPayload;
            ctx.senderNumber = "";
            ctx.commandId = "";  // HTTP API doesn't use command IDs
            ctx.requiresAuth = false;  // HTTP API handles auth separately if needed
            ctx.shouldPublishCompletion = false;  // HTTP API handles responses directly
            
            bool result = dispatchCommand(ctx);
            
            Serial.printf("Text display: Operation %s\n", result ? "completed successfully" : "failed");
            showOperationInProgress = false;
            vTaskDelete(NULL);
        }, "TextDisplayTask", 16384, &taskData, 5, NULL);
        
        // Send response immediately to avoid SSL timeout (display operation runs in background)
        String resp = "{\"success\":true,\"message\":\"Display operation started\"}";
        return response->send(200, "application/json", resp.c_str());
    });
    
    // POST /api/media/show?index=N - Show media item
    server.on("/api/media/show", HTTP_POST, [addCorsHeaders](PsychicRequest *request, PsychicResponse *response) {
        addCorsHeaders(response);
        // Extract index from query parameter
        String indexStr = request->getParam("index") ? request->getParam("index")->value() : "";
        int index = indexStr.length() > 0 ? indexStr.toInt() : -1;
        
        // Check if another show operation is in progress
        if (showOperationInProgress) {
            String resp = "{\"success\":false,\"error\":\"Another show operation is already in progress\"}";
            return response->send(409, "application/json", resp.c_str());
        }
        
        if (index < 0) {
            String resp = "{\"success\":false,\"error\":\"Invalid or missing index parameter\"}";
            return response->send(400, "application/json", resp.c_str());
        }
        
        // Set lock
        showOperationInProgress = true;
        Serial.printf("Show request for media index %d\n", index);
        
        // Create semaphore for task completion
        SemaphoreHandle_t completionSem = xSemaphoreCreateBinary();
        if (!completionSem) {
            showOperationInProgress = false;
            String resp = "{\"success\":false,\"error\":\"Failed to create semaphore\"}";
            return response->send(500, "application/json", resp.c_str());
        }
        
        // Prepare task data
        bool taskSuccess = false;
        size_t taskNextIndex = 0;
        ShowMediaTaskData taskData;
        taskData.index = index;
        taskData.success = &taskSuccess;
        taskData.nextIndex = &taskNextIndex;
        taskData.completionSem = completionSem;
        
        // Create task with large stack (16KB like OTA)
        TaskHandle_t showTaskHandle = nullptr;
        xTaskCreatePinnedToCore(show_media_task, "show_media", 16384, &taskData, 5, &showTaskHandle, 0);
        
        if (!showTaskHandle) {
            vSemaphoreDelete(completionSem);
            showOperationInProgress = false;
            String resp = "{\"success\":false,\"error\":\"Failed to create task\"}";
            return response->send(500, "application/json", resp.c_str());
        }
        
        // Wait for task to complete (with timeout - 5 minutes)
        const TickType_t timeout = pdMS_TO_TICKS(300000);
        if (xSemaphoreTake(completionSem, timeout) == pdTRUE) {
            // Task completed
            showOperationInProgress = false;
            
            String resp;
            if (taskSuccess) {
                resp = "{\"success\":true,\"nextIndex\":";
                resp += String((unsigned long)taskNextIndex);
                resp += "}";
            } else {
                resp = "{\"success\":false,\"error\":\"Failed to display image\"}";
            }
            
            vSemaphoreDelete(completionSem);
            return response->send(200, "application/json", resp.c_str());
        } else {
            // Timeout
            Serial.println("ERROR: Show media task timeout");
            showOperationInProgress = false;
            vSemaphoreDelete(completionSem);
            String resp = "{\"success\":false,\"error\":\"Operation timeout\"}";
            return response->send(408, "application/json", resp.c_str());
        }
    });
    
    // GET /api/media - Read media.txt
    server.on("/api/media", HTTP_GET, [addCorsHeaders](PsychicRequest *request, PsychicResponse *response) {
        addCorsHeaders(response);
        String content = readSDFile("0:/media.txt");
        return response->send(200, "text/plain", content.c_str());
    });
    
    // GET /api/settings - Read device settings
    server.on("/api/settings", HTTP_GET, [addCorsHeaders](PsychicRequest *request, PsychicResponse *response) {
        addCorsHeaders(response);
        String json = getDeviceSettingsJSON();
        return response->send(200, "application/json", json.c_str());
    });
    
    // GET /api/images - List image files
    server.on("/api/images", HTTP_GET, [addCorsHeaders](PsychicRequest *request, PsychicResponse *response) {
        addCorsHeaders(response);
        String json = listImageFiles();
        return response->send(200, "application/json", json.c_str());
    });
    
    // GET /api/audio - List audio files
    server.on("/api/audio", HTTP_GET, [addCorsHeaders](PsychicRequest *request, PsychicResponse *response) {
        addCorsHeaders(response);
        String json = listAudioFiles();
        return response->send(200, "application/json", json.c_str());
    });
    
    // POST /api/quotes - Write quotes.txt (with format validation)
    server.on("/api/quotes", HTTP_POST, [addCorsHeaders](PsychicRequest *request, PsychicResponse *response) {
        addCorsHeaders(response);
        
        String jsonPayload = request->body();
        
        // Parse JSON to extract content
        String content = "";
        int contentPos = jsonPayload.indexOf("\"content\"");
        if (contentPos >= 0) {
            int colonPos = jsonPayload.indexOf(':', contentPos);
            int quoteStart = jsonPayload.indexOf('"', colonPos);
            if (quoteStart >= 0) {
                quoteStart++;  // Skip opening quote
                int quoteEnd = quoteStart;
                while (quoteEnd < (int)jsonPayload.length()) {
                    if (jsonPayload.charAt(quoteEnd) == '"' && (quoteEnd == 0 || jsonPayload.charAt(quoteEnd - 1) != '\\')) {
                        break;
                    }
                    quoteEnd++;
                }
                if (quoteEnd > quoteStart) {
                    content = jsonPayload.substring(quoteStart, quoteEnd);
                    // Unescape JSON string
                    content.replace("\\n", "\n");
                    content.replace("\\r", "\r");
                    content.replace("\\t", "\t");
                    content.replace("\\\"", "\"");
                    content.replace("\\\\", "\\");
                }
            }
        }
        
        // Validate format: each quote must have text followed by ~Author, separated by blank lines
        // Parse and validate the content line by line
        std::vector<String> lines;
        int startPos = 0;
        while (startPos < (int)content.length()) {
            int endPos = content.indexOf('\n', startPos);
            if (endPos < 0) endPos = content.length();
            String line = content.substring(startPos, endPos);
            line.trim();
            lines.push_back(line);
            startPos = endPos + 1;
        }
        
        // Validate format
        bool isValid = true;
        String errorMsg = "";
        bool expectingAuthor = false;
        bool hasQuote = false;
        
        for (size_t i = 0; i < lines.size(); i++) {
            String line = lines[i];
            
            if (line.length() == 0) {
                // Blank line - if we were expecting an author, that's an error
                if (expectingAuthor) {
                    isValid = false;
                    errorMsg = "Quote text followed by blank line (missing author)";
                    break;
                }
                // Otherwise, blank line is a separator (OK)
                expectingAuthor = false;
                hasQuote = false;
                continue;
            }
            
            if (line.startsWith("~")) {
                // Author line
                if (!hasQuote) {
                    isValid = false;
                    errorMsg = "Author line (~) without preceding quote text";
                    break;
                }
                if (!expectingAuthor) {
                    isValid = false;
                    errorMsg = "Author line (~) appears without quote text";
                    break;
                }
                // Valid author line
                expectingAuthor = false;
                hasQuote = false;
            } else {
                // Quote text line
                if (expectingAuthor) {
                    // Multi-line quote - continue
                    hasQuote = true;
                } else {
                    // New quote starting
                    hasQuote = true;
                    expectingAuthor = true;
                }
            }
        }
        
        // Check if we ended expecting an author
        if (expectingAuthor) {
            isValid = false;
            errorMsg = "Quote text at end of file without author";
        }
        
        if (!isValid) {
            String resp = "{\"success\":false,\"error\":\"Invalid format: " + errorMsg + "\"}";
            return response->send(400, "application/json", resp.c_str());
        }
        
        // Format is valid, write to file
        bool success = writeSDFile("0:/quotes.txt", content);
        // Reload quotes after writing
        if (success) {
            loadQuotesFromSD();
        }
        String resp = success ? "{\"success\":true}" : "{\"success\":false,\"error\":\"Failed to write file\"}";
        return response->send(200, "application/json", resp.c_str());
    });
    
    // POST /api/media - Write media.txt and optionally update next index
    server.on("/api/media", HTTP_POST, [addCorsHeaders](PsychicRequest *request, PsychicResponse *response) {
        addCorsHeaders(response);
        String body = request->body();
        
        // Check if it's JSON format (with nextIndex)
        bool isJSON = body.startsWith("{");
        String mediaContent = "";
        int nextIndex = -1;
        
        if (isJSON) {
            // Parse JSON: {"content":"...", "nextIndex":N}
            int contentStart = body.indexOf("\"content\":");
            if (contentStart >= 0) {
                int fullContentStart = body.indexOf("\"content\":\"") + 11;
                int fullContentEnd = body.indexOf("\",\"nextIndex\"");
                if (fullContentEnd < 0) fullContentEnd = body.indexOf("\"}", fullContentStart);
                if (fullContentEnd > fullContentStart) {
                    mediaContent = body.substring(fullContentStart, fullContentEnd);
                    // Unescape
                    mediaContent.replace("\\n", "\n");
                    mediaContent.replace("\\\"", "\"");
                }
            }
            
            int indexStart = body.indexOf("\"nextIndex\":");
            if (indexStart >= 0) {
                int colonPos = body.indexOf(':', indexStart);
                int valueEnd = body.indexOf(',', colonPos);
                if (valueEnd < 0) valueEnd = body.indexOf('}', colonPos);
                if (valueEnd > colonPos) {
                    String indexStr = body.substring(colonPos + 1, valueEnd);
                    indexStr.trim();
                    if (indexStr != "null") {
                        nextIndex = indexStr.toInt();
                    }
                }
            }
        } else {
            // Plain text format (backward compatibility)
            mediaContent = body;
        }
        
        bool success = writeSDFile("0:/media.txt", mediaContent);
        
        // Update next index if provided
        if (success && nextIndex >= 0) {
            size_t mediaCount = g_media_mappings.size();
            if (mediaCount > 0) {
                lastMediaIndex = (nextIndex - 1 + mediaCount) % mediaCount;
                mediaIndexSaveToNVS();
                Serial.printf("Updated next media index: will display index %d next (lastMediaIndex=%lu)\n", 
                             nextIndex, (unsigned long)lastMediaIndex);
            }
        }
        
        // Reload media mappings after writing
        if (success) {
            loadMediaMappingsFromSD(true);  // Auto-publish when media.txt is changed
        }
        
        String resp = success ? "{\"success\":true}" : "{\"success\":false,\"error\":\"Failed to write file\"}";
        return response->send(200, "application/json", resp.c_str());
    });
    
    // POST /api/settings - Update device settings
    server.on("/api/settings", HTTP_POST, [addCorsHeaders](PsychicRequest *request, PsychicResponse *response) {
        addCorsHeaders(response);
        String body = request->body();
        bool success = updateDeviceSettings(body);
        String resp = success ? "{\"success\":true}" : "{\"success\":false,\"error\":\"Failed to update settings\"}";
        return response->send(200, "application/json", resp.c_str());
    });
    
    // POST /api/auth/password - Set web UI password (for GitHub Pages UI authentication)
    server.on("/api/auth/password", HTTP_POST, [addCorsHeaders](PsychicRequest *request, PsychicResponse *response) {
        addCorsHeaders(response);
        String body = request->body();
        
        // Parse JSON to extract password
        String password = "";
        int passwordPos = body.indexOf("\"password\"");
        if (passwordPos >= 0) {
            int colonPos = body.indexOf(':', passwordPos);
            int quoteStart = body.indexOf('"', colonPos);
            if (quoteStart >= 0) {
                quoteStart++;  // Skip opening quote
                int quoteEnd = quoteStart;
                while (quoteEnd < (int)body.length()) {
                    if (body.charAt(quoteEnd) == '"' && (quoteEnd == 0 || body.charAt(quoteEnd - 1) != '\\')) {
                        break;
                    }
                    quoteEnd++;
                }
                if (quoteEnd > quoteStart) {
                    password = body.substring(quoteStart, quoteEnd);
                    // Unescape JSON string
                    password.replace("\\n", "\n");
                    password.replace("\\r", "\r");
                    password.replace("\\t", "\t");
                    password.replace("\\\"", "\"");
                    password.replace("\\\\", "\\");
                }
            }
        }
        
        if (password.length() == 0) {
            String resp = "{\"success\":false,\"error\":\"Password field is required\"}";
            return response->send(400, "application/json", resp.c_str());
        }
        
        if (password.length() < 8) {
            String resp = "{\"success\":false,\"error\":\"Password must be at least 8 characters\"}";
            return response->send(400, "application/json", resp.c_str());
        }
        
        bool success = setWebUIPassword(password);
        String resp = success ? "{\"success\":true,\"message\":\"Password set successfully. GitHub Pages UI will now require HMAC authentication.\"}" 
                              : "{\"success\":false,\"error\":\"Failed to set password\"}";
        return response->send(success ? 200 : 500, "application/json", resp.c_str());
    });
    
    // GET /api/auth/status - Check if password is configured
    server.on("/api/auth/status", HTTP_GET, [addCorsHeaders](PsychicRequest *request, PsychicResponse *response) {
        addCorsHeaders(response);
        bool isSet = isWebUIPasswordSet();
        String resp = "{\"password_configured\":" + String(isSet ? "true" : "false") + "}";
        return response->send(200, "application/json", resp.c_str());
    });
    
    // GET /api/files - List all files
    server.on("/api/files", HTTP_GET, [addCorsHeaders](PsychicRequest *request, PsychicResponse *response) {
        addCorsHeaders(response);
        String json = listAllFiles();
        return response->send(200, "application/json", json.c_str());
    });
    
    // GET /api/files/* - Download a file
    server.on("/api/files/*", HTTP_GET, [addCorsHeaders](PsychicRequest *request, PsychicResponse *response) {
        // Extract filename from URL path
        String url = request->url();
        int pathStart = url.indexOf("/api/files/") + 11;
        String filename = url.substring(pathStart);
        filename.trim();
        
        // URL decode filename (basic)
        filename.replace("%20", " ");
        filename.replace("%2F", "/");
        
        String filepath = "0:/";
        filepath += filename;
        
        FIL file;
        FRESULT res = f_open(&file, filepath.c_str(), FA_READ);
        if (res == FR_OK) {
            FSIZE_t fileSize = f_size(&file);
            
            // Add CORS headers before creating stream response
            addCorsHeaders(response);
            
            // Create stream response for file download
            // Constructor automatically sets Content-Disposition header for download
            PsychicStreamResponse streamResp(response, "application/octet-stream", filename);
            
            if (streamResp.beginSend() == ESP_OK) {
                // Stream file content
                char buffer[512];
                UINT bytesRead;
                while (f_read(&file, buffer, sizeof(buffer), &bytesRead) == FR_OK && bytesRead > 0) {
                    streamResp.write((uint8_t*)buffer, bytesRead);
                    if (bytesRead < sizeof(buffer)) break; // EOF
                }
                streamResp.endSend();
            }
            f_close(&file);
            Serial.printf("File downloaded: %s (%lu bytes)\n", filename.c_str(), (unsigned long)fileSize);
            return ESP_OK;
        } else {
            addCorsHeaders(response);
            Serial.printf("File not found: %s (error %d)\n", filepath.c_str(), res);
            return response->send(404, "text/plain", "File not found");
        }
    });
    
    // POST /api/files/upload - File upload (base64-encoded JSON)
    // Process in separate task with large stack to avoid stack overflow
    server.on("/api/files/upload", HTTP_POST, [addCorsHeaders](PsychicRequest *request, PsychicResponse *response) {
        addCorsHeaders(response);
        
        // Get body using PsychicHttp's body() method (works in HTTP server task context)
        // This creates a String on the stack, but we'll immediately copy to heap
        String bodyStr = request->body();
        if (bodyStr.length() == 0 || bodyStr.length() > 1024 * 1024) {  // Max 1MB
            String resp = "{\"success\":false,\"error\":\"Invalid content length\"}";
            return response->send(400, "application/json", resp.c_str());
        }
        
        // Copy body to heap buffer immediately to avoid stack issues
        size_t bodyLen = bodyStr.length();
        char* jsonBuffer = (char*)malloc(bodyLen + 1);
        if (!jsonBuffer) {
            String resp = "{\"success\":false,\"error\":\"Failed to allocate buffer\"}";
            return response->send(500, "application/json", resp.c_str());
        }
        memcpy(jsonBuffer, bodyStr.c_str(), bodyLen);
        jsonBuffer[bodyLen] = '\0';
        // bodyStr goes out of scope here, freeing stack memory
        
        // Create semaphore for task completion
        SemaphoreHandle_t completionSem = xSemaphoreCreateBinary();
        if (!completionSem) {
            free(jsonBuffer);
            String resp = "{\"success\":false,\"error\":\"Failed to create semaphore\"}";
            return response->send(500, "application/json", resp.c_str());
        }
        
        // Prepare task data
        struct {
            char* jsonData;
            size_t jsonLen;
            SemaphoreHandle_t sem;
            bool* success;
            String* resultJson;
        } taskData;
        
        bool taskSuccess = false;
        String resultJson = "";
        taskData.jsonData = jsonBuffer;
        taskData.jsonLen = bodyLen;
        taskData.sem = completionSem;
        taskData.success = &taskSuccess;
        taskData.resultJson = &resultJson;
        
        // Create task with large stack (32KB) to process upload
        xTaskCreate([](void* param) {
            auto* data = (decltype(taskData)*)param;
            
            String jsonPayload = String(data->jsonData);
            free(data->jsonData);  // Free immediately after creating String
            
            // Parse JSON to extract filename and base64 data
            String filename = "";
            String base64Data = "";
            
            int filenamePos = jsonPayload.indexOf("\"filename\"");
            if (filenamePos >= 0) {
                int colonPos = jsonPayload.indexOf(":", filenamePos);
                int quoteStart = jsonPayload.indexOf("\"", colonPos);
                if (quoteStart >= 0) {
                    int quoteEnd = jsonPayload.indexOf("\"", quoteStart + 1);
                    if (quoteEnd > quoteStart) {
                        filename = jsonPayload.substring(quoteStart + 1, quoteEnd);
                    }
                }
            }
            
            int dataPos = jsonPayload.indexOf("\"data\"");
            if (dataPos >= 0) {
                int colonPos = jsonPayload.indexOf(":", dataPos);
                int quoteStart = jsonPayload.indexOf("\"", colonPos);
                if (quoteStart >= 0) {
                    int quoteEnd = quoteStart + 1;
                    while (quoteEnd < (int)jsonPayload.length()) {
                        if (jsonPayload.charAt(quoteEnd) == '"') {
                            if (quoteEnd == 0 || jsonPayload.charAt(quoteEnd - 1) != '\\') {
                                break;
                            }
                        }
                        quoteEnd++;
                    }
                    if (quoteEnd > quoteStart && quoteEnd < (int)jsonPayload.length()) {
                        base64Data = jsonPayload.substring(quoteStart + 1, quoteEnd);
                    } else {
                        quoteEnd = jsonPayload.lastIndexOf("\"");
                        if (quoteEnd > quoteStart) {
                            base64Data = jsonPayload.substring(quoteStart + 1, quoteEnd);
                        }
                    }
                }
            }
            
            // Validate we got both filename and data
            if (filename.length() == 0 || base64Data.length() == 0) {
                *(data->resultJson) = "{\"success\":false,\"error\":\"Invalid JSON: missing filename or data\"}";
                *(data->success) = false;
                xSemaphoreGive(data->sem);
                vTaskDelete(NULL);
                return;
            }
            
            // Decode base64 and write to file
            Serial.printf("Uploading file: %s (base64 length: %d)\n", filename.c_str(), base64Data.length());
            
            size_t base64Len = base64Data.length();
            size_t decodedMaxSize = (base64Len * 3) / 4 + 4;
            
            uint8_t* decodedBuffer = (uint8_t*)malloc(decodedMaxSize);
            if (!decodedBuffer) {
                *(data->resultJson) = "{\"success\":false,\"error\":\"Failed to allocate decode buffer\"}";
                *(data->success) = false;
                xSemaphoreGive(data->sem);
                vTaskDelete(NULL);
                return;
            }
            
            // Base64 decode
            static const char b64_table[] = {
                64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
                64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
                64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
                52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
                64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
                15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
                64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
                41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64
            };
            
            size_t decodedLen = 0;
            uint32_t accumulator = 0;
            int bits = 0;
            
            for (size_t i = 0; i < base64Len; i++) {
                char c = base64Data.charAt(i);
                if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
                if (c == '=') continue;
                if (c < 0 || c >= 128) continue;
                uint8_t val = b64_table[(uint8_t)c];
                if (val == 64) continue;
                
                accumulator = (accumulator << 6) | val;
                bits += 6;
                
                if (bits >= 8) {
                    bits -= 8;
                    decodedBuffer[decodedLen++] = (uint8_t)((accumulator >> bits) & 0xFF);
                }
            }
            
            Serial.printf("Decoded %zu bytes from base64\n", decodedLen);
            
            // Open file for writing
            String filepath = "0:/";
            filepath += filename;
            FIL file;
            FRESULT res = f_open(&file, filepath.c_str(), FA_WRITE | FA_CREATE_ALWAYS);
            
            if (res != FR_OK) {
                free(decodedBuffer);
                *(data->resultJson) = "{\"success\":false,\"error\":\"Failed to create file\"}";
                *(data->success) = false;
                xSemaphoreGive(data->sem);
                vTaskDelete(NULL);
                return;
            }
            
            // Write decoded data to file
            UINT bytesWritten = 0;
            FRESULT writeRes = f_write(&file, decodedBuffer, decodedLen, &bytesWritten);
            
            f_close(&file);
            free(decodedBuffer);
            
            if (writeRes != FR_OK || bytesWritten != decodedLen) {
                Serial.printf("ERROR: File write failed: res=%d, wrote=%u/%zu\n", 
                             writeRes, bytesWritten, decodedLen);
                *(data->resultJson) = "{\"success\":false,\"error\":\"File write failed\"}";
                *(data->success) = false;
                xSemaphoreGive(data->sem);
                vTaskDelete(NULL);
                return;
            }
            
            Serial.printf("File upload complete: %s (%u bytes written)\n", 
                         filename.c_str(), bytesWritten);
            
            *(data->resultJson) = "{\"success\":true,\"filename\":\"";
            *(data->resultJson) += filename;
            *(data->resultJson) += "\",\"size\":";
            *(data->resultJson) += String(bytesWritten);
            *(data->resultJson) += "}";
            *(data->success) = true;
            xSemaphoreGive(data->sem);
            vTaskDelete(NULL);
        }, "file_upload_task", 32 * 1024, &taskData, 5, NULL);  // 32KB stack
        
        // Wait for task to complete
        if (xSemaphoreTake(completionSem, pdMS_TO_TICKS(60000)) == pdTRUE) {
            vSemaphoreDelete(completionSem);
            int statusCode = taskSuccess ? 200 : 400;
            return response->send(statusCode, "application/json", resultJson.c_str());
        } else {
            vSemaphoreDelete(completionSem);
            String resp = "{\"success\":false,\"error\":\"Upload timeout\"}";
            return response->send(500, "application/json", resp.c_str());
        }
    });
    
    // POST /api/files/upload/chunk - Chunked file upload for large files
    // Format: {"filename":"name","chunkIndex":0,"totalChunks":5,"chunkData":"base64..."}
    // Process in separate task with large stack to avoid stack overflow
    server.on("/api/files/upload/chunk", HTTP_POST, [addCorsHeaders](PsychicRequest *request, PsychicResponse *response) {
        addCorsHeaders(response);
        
        // Get body using PsychicHttp's body() method (works in HTTP server task context)
        // This creates a String on the stack, but we'll immediately copy to heap
        String bodyStr = request->body();
        if (bodyStr.length() == 0 || bodyStr.length() > 1024 * 1024) {  // Max 1MB
            String resp = "{\"success\":false,\"error\":\"Invalid content length\"}";
            return response->send(400, "application/json", resp.c_str());
        }
        
        // Copy body to heap buffer immediately to avoid stack issues
        size_t bodyLen = bodyStr.length();
        char* jsonBuffer = (char*)malloc(bodyLen + 1);
        if (!jsonBuffer) {
            String resp = "{\"success\":false,\"error\":\"Failed to allocate buffer\"}";
            return response->send(500, "application/json", resp.c_str());
        }
        memcpy(jsonBuffer, bodyStr.c_str(), bodyLen);
        jsonBuffer[bodyLen] = '\0';
        // bodyStr goes out of scope here, freeing stack memory
        
        // Create semaphore for task completion
        SemaphoreHandle_t completionSem = xSemaphoreCreateBinary();
        if (!completionSem) {
            free(jsonBuffer);
            String resp = "{\"success\":false,\"error\":\"Failed to create semaphore\"}";
            return response->send(500, "application/json", resp.c_str());
        }
        
        // Prepare task data
        struct {
            char* jsonData;
            size_t jsonLen;
            SemaphoreHandle_t sem;
            bool* success;
            String* resultJson;
        } taskData;
        
        bool taskSuccess = false;
        String resultJson = "";
        taskData.jsonData = jsonBuffer;
        taskData.jsonLen = bodyLen;
        taskData.sem = completionSem;
        taskData.success = &taskSuccess;
        taskData.resultJson = &resultJson;
        
        // Create task with large stack (32KB) to process chunk upload
        xTaskCreate([](void* param) {
            auto* data = (decltype(taskData)*)param;
            
            String jsonPayload = String(data->jsonData);
            free(data->jsonData);  // Free immediately after creating String
            
            // Parse JSON
            String filename = "";
            int chunkIndex = -1;
            int totalChunks = -1;
            String chunkData = "";
            
            // Extract filename
            int filenamePos = jsonPayload.indexOf("\"filename\"");
            if (filenamePos >= 0) {
                int colonPos = jsonPayload.indexOf(":", filenamePos);
                int quoteStart = jsonPayload.indexOf("\"", colonPos);
                if (quoteStart >= 0) {
                    int quoteEnd = jsonPayload.indexOf("\"", quoteStart + 1);
                    if (quoteEnd > quoteStart) {
                        filename = jsonPayload.substring(quoteStart + 1, quoteEnd);
                    }
                }
            }
            
            // Extract chunkIndex
            int chunkIdxPos = jsonPayload.indexOf("\"chunkIndex\"");
            if (chunkIdxPos >= 0) {
                int colonPos = jsonPayload.indexOf(":", chunkIdxPos);
                int numStart = colonPos + 1;
                while (numStart < (int)jsonPayload.length() && (jsonPayload.charAt(numStart) == ' ' || jsonPayload.charAt(numStart) == '\t')) numStart++;
                int numEnd = numStart;
                while (numEnd < (int)jsonPayload.length() && jsonPayload.charAt(numEnd) >= '0' && jsonPayload.charAt(numEnd) <= '9') numEnd++;
                if (numEnd > numStart) {
                    chunkIndex = jsonPayload.substring(numStart, numEnd).toInt();
                }
            }
            
            // Extract totalChunks
            int totalChunksPos = jsonPayload.indexOf("\"totalChunks\"");
            if (totalChunksPos >= 0) {
                int colonPos = jsonPayload.indexOf(":", totalChunksPos);
                int numStart = colonPos + 1;
                while (numStart < (int)jsonPayload.length() && (jsonPayload.charAt(numStart) == ' ' || jsonPayload.charAt(numStart) == '\t')) numStart++;
                int numEnd = numStart;
                while (numEnd < (int)jsonPayload.length() && jsonPayload.charAt(numEnd) >= '0' && jsonPayload.charAt(numEnd) <= '9') numEnd++;
                if (numEnd > numStart) {
                    totalChunks = jsonPayload.substring(numStart, numEnd).toInt();
                }
            }
            
            // Extract chunkData
            int dataPos = jsonPayload.indexOf("\"chunkData\"");
            if (dataPos >= 0) {
                int colonPos = jsonPayload.indexOf(":", dataPos);
                int quoteStart = jsonPayload.indexOf("\"", colonPos);
                if (quoteStart >= 0) {
                    quoteStart++;
                    int quoteEnd = quoteStart;
                    while (quoteEnd < (int)jsonPayload.length()) {
                        if (jsonPayload.charAt(quoteEnd) == '"' && (quoteEnd == 0 || jsonPayload.charAt(quoteEnd - 1) != '\\')) {
                            break;
                        }
                        quoteEnd++;
                    }
                    if (quoteEnd > quoteStart) {
                        chunkData = jsonPayload.substring(quoteStart, quoteEnd);
                    }
                }
            }
            
            // Validate
            if (filename.length() == 0 || chunkIndex < 0 || totalChunks < 1 || chunkData.length() == 0) {
                *(data->resultJson) = "{\"success\":false,\"error\":\"Invalid chunk data\"}";
                *(data->success) = false;
                xSemaphoreGive(data->sem);
                vTaskDelete(NULL);
                return;
            }
            
            Serial.printf("Chunk upload: %s chunk %d/%d (%d bytes base64)\n", 
                         filename.c_str(), chunkIndex + 1, totalChunks, chunkData.length());
            
            // Create temp directory for chunks if it doesn't exist
            String tempDir = "0:/_upload_chunks";
            FILINFO fno;
            FRESULT dirRes = f_stat(tempDir.c_str(), &fno);
            if (dirRes != FR_OK) {
                f_mkdir(tempDir.c_str());
            }
            
            // Save chunk to temp file
            String chunkFile = tempDir + "/" + filename + ".chunk" + String(chunkIndex);
            FIL chunkFil;
            FRESULT openRes = f_open(&chunkFil, chunkFile.c_str(), FA_WRITE | FA_CREATE_ALWAYS);
            if (openRes != FR_OK) {
                *(data->resultJson) = "{\"success\":false,\"error\":\"Failed to create chunk file\"}";
                *(data->success) = false;
                xSemaphoreGive(data->sem);
                vTaskDelete(NULL);
                return;
            }
            
            // Decode base64 chunk
            size_t base64Len = chunkData.length();
            size_t decodedMaxSize = (base64Len * 3) / 4 + 4;
            uint8_t* decodedBuffer = (uint8_t*)malloc(decodedMaxSize);
            if (!decodedBuffer) {
                f_close(&chunkFil);
                *(data->resultJson) = "{\"success\":false,\"error\":\"Failed to allocate decode buffer\"}";
                *(data->success) = false;
                xSemaphoreGive(data->sem);
                vTaskDelete(NULL);
                return;
            }
            
            // Base64 decode
            static const char b64_table[] = {
                64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
                64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
                64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
                52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
                64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
                15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
                64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
                41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64
            };
            
            size_t decodedLen = 0;
            uint32_t accumulator = 0;
            int bits = 0;
            
            for (size_t i = 0; i < base64Len; i++) {
                char c = chunkData.charAt(i);
                if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
                if (c == '=') continue;
                if (c < 0 || c >= 128) continue;
                uint8_t val = b64_table[(uint8_t)c];
                if (val == 64) continue;
                
                accumulator = (accumulator << 6) | val;
                bits += 6;
                
                if (bits >= 8) {
                    bits -= 8;
                    decodedBuffer[decodedLen++] = (uint8_t)((accumulator >> bits) & 0xFF);
                }
            }
            
            // Write decoded chunk
            UINT bytesWritten = 0;
            FRESULT writeRes = f_write(&chunkFil, decodedBuffer, decodedLen, &bytesWritten);
            f_close(&chunkFil);
            free(decodedBuffer);
            
            if (writeRes != FR_OK || bytesWritten != decodedLen) {
                *(data->resultJson) = "{\"success\":false,\"error\":\"Failed to write chunk\"}";
                *(data->success) = false;
                xSemaphoreGive(data->sem);
                vTaskDelete(NULL);
                return;
            }
            
            // Check if this is the last chunk - if so, reassemble file
            if (chunkIndex == totalChunks - 1) {
                Serial.printf("Last chunk received, reassembling file: %s\n", filename.c_str());
                
                // Open destination file
                String filepath = "0:/" + filename;
                FIL destFile;
                FRESULT openRes = f_open(&destFile, filepath.c_str(), FA_WRITE | FA_CREATE_ALWAYS);
                if (openRes != FR_OK) {
                    *(data->resultJson) = "{\"success\":false,\"error\":\"Failed to create destination file\"}";
                    *(data->success) = false;
                    xSemaphoreGive(data->sem);
                    vTaskDelete(NULL);
                    return;
                }
                
                // Read and concatenate all chunks
                uint8_t* buffer = (uint8_t*)malloc(8192);  // 8KB buffer
                if (!buffer) {
                    f_close(&destFile);
                    *(data->resultJson) = "{\"success\":false,\"error\":\"Failed to allocate buffer\"}";
                    *(data->success) = false;
                    xSemaphoreGive(data->sem);
                    vTaskDelete(NULL);
                    return;
                }
                
                size_t totalBytes = 0;
                bool success = true;
                
                for (int i = 0; i < totalChunks; i++) {
                    String chunkPath = tempDir + "/" + filename + ".chunk" + String(i);
                    FIL chunkFile;
                    FRESULT res = f_open(&chunkFile, chunkPath.c_str(), FA_READ);
                    if (res != FR_OK) {
                        Serial.printf("ERROR: Failed to open chunk %d: %d\n", i, res);
                        success = false;
                        break;
                    }
                    
                    FSIZE_t chunkSize = f_size(&chunkFile);
                    size_t remaining = (size_t)chunkSize;
                    
                    while (remaining > 0) {
                        UINT toRead = (remaining > 8192) ? 8192 : remaining;
                        UINT bytesRead = 0;
                        res = f_read(&chunkFile, buffer, toRead, &bytesRead);
                        if (res != FR_OK || bytesRead == 0) {
                            success = false;
                            break;
                        }
                        
                        UINT bytesWritten = 0;
                        res = f_write(&destFile, buffer, bytesRead, &bytesWritten);
                        if (res != FR_OK || bytesWritten != bytesRead) {
                            success = false;
                            break;
                        }
                        
                        totalBytes += bytesWritten;
                        remaining -= bytesRead;
                    }
                    
                    f_close(&chunkFile);
                    
                    // Delete chunk file
                    f_unlink(chunkPath.c_str());
                    
                    if (!success) break;
                }
                
                f_close(&destFile);
                free(buffer);
                
                if (!success) {
                    *(data->resultJson) = "{\"success\":false,\"error\":\"Failed to reassemble file\"}";
                    *(data->success) = false;
                    xSemaphoreGive(data->sem);
                    vTaskDelete(NULL);
                    return;
                }
                
                Serial.printf("File reassembled: %s (%zu bytes)\n", filename.c_str(), totalBytes);
                *(data->resultJson) = "{\"success\":true,\"filename\":\"";
                *(data->resultJson) += filename;
                *(data->resultJson) += "\",\"size\":";
                *(data->resultJson) += String(totalBytes);
                *(data->resultJson) += "}";
                *(data->success) = true;
            } else {
                // Not the last chunk, just acknowledge
                *(data->resultJson) = "{\"success\":true,\"chunk\":";
                *(data->resultJson) += String(chunkIndex);
                *(data->resultJson) += "}";
                *(data->success) = true;
            }
            xSemaphoreGive(data->sem);
            vTaskDelete(NULL);
        }, "chunk_upload_task", 32 * 1024, &taskData, 5, NULL);  // 32KB stack
        
        // Wait for task to complete
        if (xSemaphoreTake(completionSem, pdMS_TO_TICKS(60000)) == pdTRUE) {
            vSemaphoreDelete(completionSem);
            int statusCode = taskSuccess ? 200 : 400;
            return response->send(statusCode, "application/json", resultJson.c_str());
        } else {
            vSemaphoreDelete(completionSem);
            String resp = "{\"success\":false,\"error\":\"Upload timeout\"}";
            return response->send(500, "application/json", resp.c_str());
        }
    });
    
    // POST /api/canvas/display - Display canvas drawing (800x600 PNG, scaled to 1600x1200)
    server.on("/api/canvas/display", HTTP_POST, [addCorsHeaders](PsychicRequest *request, PsychicResponse *response) {
        addCorsHeaders(response);
        
        // Check if another show operation is in progress
        if (showOperationInProgress) {
            String resp = "{\"success\":false,\"error\":\"Another show operation is already in progress\"}";
            return response->send(409, "application/json", resp.c_str());
        }
        
        String jsonPayload = request->body();
        
        // Parse JSON to extract pixel data (base64-encoded array of color indices 0-6)
        String base64Data = "";
        int width = 800;
        int height = 600;
        
        // Extract pixelData
        int pixelDataPos = jsonPayload.indexOf("\"pixelData\"");
        if (pixelDataPos >= 0) {
            int colonPos = jsonPayload.indexOf(':', pixelDataPos);
            int quoteStart = jsonPayload.indexOf('"', colonPos);
            if (quoteStart >= 0) {
                quoteStart++;  // Skip opening quote
                int quoteEnd = jsonPayload.indexOf('"', quoteStart);
                if (quoteEnd > quoteStart) {
                    base64Data = jsonPayload.substring(quoteStart, quoteEnd);
                }
            }
        }
        
        // Extract width and height (optional, defaults to 800x600)
        int widthPos = jsonPayload.indexOf("\"width\"");
        if (widthPos >= 0) {
            int colonPos = jsonPayload.indexOf(':', widthPos);
            int numStart = colonPos + 1;
            while (numStart < (int)jsonPayload.length() && (jsonPayload.charAt(numStart) == ' ' || jsonPayload.charAt(numStart) == '\t')) numStart++;
            int numEnd = numStart;
            while (numEnd < (int)jsonPayload.length() && jsonPayload.charAt(numEnd) >= '0' && jsonPayload.charAt(numEnd) <= '9') numEnd++;
            if (numEnd > numStart) {
                width = jsonPayload.substring(numStart, numEnd).toInt();
            }
        }
        
        int heightPos = jsonPayload.indexOf("\"height\"");
        if (heightPos >= 0) {
            int colonPos = jsonPayload.indexOf(':', heightPos);
            int numStart = colonPos + 1;
            while (numStart < (int)jsonPayload.length() && (jsonPayload.charAt(numStart) == ' ' || jsonPayload.charAt(numStart) == '\t')) numStart++;
            int numEnd = numStart;
            while (numEnd < (int)jsonPayload.length() && jsonPayload.charAt(numEnd) >= '0' && jsonPayload.charAt(numEnd) <= '9') numEnd++;
            if (numEnd > numStart) {
                height = jsonPayload.substring(numStart, numEnd).toInt();
            }
        }
        
        if (base64Data.length() == 0) {
            String resp = "{\"success\":false,\"error\":\"Invalid JSON: missing pixelData\"}";
            return response->send(400, "application/json", resp.c_str());
        }
        
        if (width <= 0 || height <= 0 || width > 1600 || height > 1200) {
            String resp = "{\"success\":false,\"error\":\"Invalid dimensions\"}";
            return response->send(400, "application/json", resp.c_str());
        }
        
        Serial.printf("Canvas display: received pixel data (%d chars, %dx%d)\n", base64Data.length(), width, height);
        
        // Decode base64 to get pixel color indices
        size_t base64Len = base64Data.length();
        size_t expectedPixels = width * height;
        size_t decodedMaxSize = (base64Len * 3) / 4 + 4;
        
        uint8_t* pixelBuffer = (uint8_t*)malloc(decodedMaxSize);
        if (!pixelBuffer) {
            String resp = "{\"success\":false,\"error\":\"Failed to allocate pixel buffer\"}";
            return response->send(500, "application/json", resp.c_str());
        }
        
        // Base64 decode
        static const char b64_table[] = {
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
            52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
            64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
            15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
            64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
            41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64
        };
        
        size_t decodedLen = 0;
        uint32_t accumulator = 0;
        int bits = 0;
        
        for (size_t i = 0; i < base64Len; i++) {
            char c = base64Data.charAt(i);
            if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
            if (c == '=') continue;
            if (c < 0 || c >= 128) continue;
            uint8_t val = b64_table[(uint8_t)c];
            if (val == 64) continue;
            
            accumulator = (accumulator << 6) | val;
            bits += 6;
            
            if (bits >= 8) {
                bits -= 8;
                pixelBuffer[decodedLen++] = (uint8_t)((accumulator >> bits) & 0xFF);
            }
        }
        
        if (decodedLen != expectedPixels) {
            free(pixelBuffer);
            String resp = "{\"success\":false,\"error\":\"Pixel count mismatch\"}";
            return response->send(400, "application/json", resp.c_str());
        }
        
        Serial.printf("Canvas display: decoded %zu pixels (%dx%d)\n", decodedLen, width, height);
        
        // Set lock
        showOperationInProgress = true;
        
        // Create semaphore for task completion
        SemaphoreHandle_t completionSem = xSemaphoreCreateBinary();
        if (!completionSem) {
            showOperationInProgress = false;
            free(pixelBuffer);
            String resp = "{\"success\":false,\"error\":\"Failed to create semaphore\"}";
            return response->send(500, "application/json", resp.c_str());
        }
        
        // Prepare task data
        struct {
            uint8_t* pixels;
            int width;
            int height;
            SemaphoreHandle_t sem;
            bool* success;
        } taskData;
        bool taskSuccess = false;
        taskData.pixels = pixelBuffer;
        taskData.width = width;
        taskData.height = height;
        taskData.sem = completionSem;
        taskData.success = &taskSuccess;
        
        // Create task to display canvas (runs in background)
        xTaskCreate([](void* param) {
            auto* data = (decltype(taskData)*)param;
            
            Serial.println("Canvas display: Starting display task...");
            
            // Initialize display if not already initialized (safe to call begin() if buffer is null)
            if (display.getBuffer() == nullptr) {
                Serial.println("Canvas display: Display not initialized - initializing now...");
                displaySPI.begin(PIN_SPI_SCK, -1, PIN_SPI_MOSI, -1);
                if (!display.begin(PIN_CS0, PIN_CS1, PIN_DC, PIN_RESET, PIN_BUSY)) {
                    Serial.println("Canvas display: ERROR - Display initialization failed!");
                    *(data->success) = false;
                    free(data->pixels);
                    xSemaphoreGive(data->sem);
                    vTaskDelete(NULL);
                    return;
                }
                Serial.println("Canvas display: Display initialized successfully");
            } else {
                Serial.println("Canvas display: Display already initialized, using existing buffer");
            }
            
            // Clear display to white first
            display.clear(EL133UF1_WHITE);
            
            // Calculate scale factor to fit on 1600x1200 display
            // For 800x600 canvas, scale 2x to 1600x1200
            int scaleX = EL133UF1_WIDTH / data->width;   // 1600 / 800 = 2
            int scaleY = EL133UF1_HEIGHT / data->height; // 1200 / 600 = 2
            
            // Center the scaled image
            int16_t offsetX = (EL133UF1_WIDTH - (data->width * scaleX)) / 2;
            int16_t offsetY = (EL133UF1_HEIGHT - (data->height * scaleY)) / 2;
            
            Serial.printf("Canvas display: Drawing %dx%d pixels, scaling %dx to %dx%d at offset (%d, %d)\n",
                         data->width, data->height, scaleX, scaleY,
                         data->width * scaleX, data->height * scaleY, offsetX, offsetY);
            
            // Count non-white pixels for debugging
            int nonWhiteCount = 0;
            int colorCounts[7] = {0};
            
            // Draw pixels directly to display with scaling
            // Each source pixel becomes a scaleX x scaleY block
            for (int sy = 0; sy < data->height; sy++) {
                for (int sx = 0; sx < data->width; sx++) {
                    uint8_t color = data->pixels[sy * data->width + sx];
                    // Ensure color is valid (0-6)
                    if (color > 6) color = EL133UF1_WHITE;
                    
                    // Track color distribution
                    if (color < 7) colorCounts[color]++;
                    if (color != EL133UF1_WHITE) nonWhiteCount++;
                    
                    // Calculate destination position
                    int16_t dx = offsetX + sx * scaleX;
                    int16_t dy = offsetY + sy * scaleY;
                    
                    // Draw scaleX x scaleY block
                    for (int py = 0; py < scaleY; py++) {
                        for (int px = 0; px < scaleX; px++) {
                            int16_t pxX = dx + px;
                            int16_t pxY = dy + py;
                            if (pxX >= 0 && pxX < EL133UF1_WIDTH && pxY >= 0 && pxY < EL133UF1_HEIGHT) {
                                display.setPixel(pxX, pxY, color);
                            }
                        }
                    }
                }
            }
            
            Serial.printf("Canvas display: Drew %d non-white pixels. Color distribution: ", nonWhiteCount);
            for (int i = 0; i < 7; i++) {
                if (colorCounts[i] > 0) {
                    Serial.printf("color%d=%d ", i, colorCounts[i]);
                }
            }
            Serial.println();
            
            // Update display
            display.update();  // Uses mutex to ensure _sendBuffer() completes before returning
            Serial.println("Canvas display: Success!");
            
            // Save framebuffer to storage partition after display update
            // safeDisplayUpdate() ensures _sendBuffer() has completed, so the buffer is safe to read
            
            *(data->success) = true;
            
            free(data->pixels);
            showOperationInProgress = false;
            xSemaphoreGive(data->sem);
            vTaskDelete(NULL);
        }, "CanvasDisplayTask", 16384, &taskData, 5, NULL);
        
        // Wait for task to complete (with timeout)
        if (xSemaphoreTake(completionSem, pdMS_TO_TICKS(60000)) == pdTRUE) {
            vSemaphoreDelete(completionSem);
            String resp = taskSuccess ? "{\"success\":true}" : "{\"success\":false,\"error\":\"Display operation failed\"}";
            return response->send(200, "application/json", resp.c_str());
        } else {
            vSemaphoreDelete(completionSem);
            showOperationInProgress = false;
            free(pixelBuffer);
            String resp = "{\"success\":false,\"error\":\"Display operation timeout\"}";
            return response->send(500, "application/json", resp.c_str());
        }
    });
    
    // DELETE /api/files/* - Delete a file
    server.on("/api/files/*", HTTP_DELETE, [addCorsHeaders](PsychicRequest *request, PsychicResponse *response) {
        addCorsHeaders(response);
        String url = request->url();
        int pathStart = url.indexOf("/api/files/") + 11;
        String filename = url.substring(pathStart);
        filename.trim();
        
        // URL decode filename (basic)
        filename.replace("%20", " ");
        filename.replace("%2F", "/");
        
        bool success = deleteSDFile(filename.c_str());
        String resp = success ? "{\"success\":true}" : "{\"success\":false,\"error\":\"Failed to delete file\"}";
        return response->send(200, "application/json", resp.c_str());
    });
    
    // GET /api/log - Get current log file
    server.on("/api/log", HTTP_GET, [addCorsHeaders](PsychicRequest *request, PsychicResponse *response) {
        addCorsHeaders(response);
        logFlush();
        String content = readSDFile(LOG_FILE);
        return response->send(200, "text/plain", content.c_str());
    });
    
    // GET /api/log/list - List recent log files
    server.on("/api/log/list", HTTP_GET, [addCorsHeaders](PsychicRequest *request, PsychicResponse *response) {
        addCorsHeaders(response);
        String json = "[";
        bool first = true;
        
        struct LogFileInfo {
            String filename;
            uint32_t mtime;
            uint32_t size;
        };
        std::vector<LogFileInfo> logFiles;
        
        FF_DIR dir;
        FILINFO fno;
        FRESULT res = f_opendir(&dir, LOG_DIR);
        
        if (res == FR_OK) {
            while (true) {
                res = f_readdir(&dir, &fno);
                if (res != FR_OK || fno.fname[0] == 0) break;
                
                if (!(fno.fattrib & AM_DIR)) {
                    String filename = String(fno.fname);
                    if (filename.startsWith("log_") && filename.endsWith(".txt")) {
                        LogFileInfo info;
                        info.filename = filename;
                        info.size = (uint32_t)fno.fsize;
                        uint16_t date = fno.fdate;
                        uint16_t time = fno.ftime;
                        uint32_t year = 1980 + ((date >> 9) & 0x7F);
                        uint32_t month = (date >> 5) & 0x0F;
                        uint32_t day = date & 0x1F;
                        uint32_t hour = (time >> 11) & 0x1F;
                        uint32_t min = (time >> 5) & 0x3F;
                        uint32_t sec = (time & 0x1F) * 2;
                        info.mtime = (year - 1980) * 365 * 24 * 3600 + month * 30 * 24 * 3600 + day * 24 * 3600 + hour * 3600 + min * 60 + sec;
                        logFiles.push_back(info);
                    }
                }
            }
            f_closedir(&dir);
            
            // Sort by modification time (newest first)
            for (size_t i = 0; i < logFiles.size(); i++) {
                for (size_t j = i + 1; j < logFiles.size(); j++) {
                    if (logFiles[i].mtime < logFiles[j].mtime) {
                        LogFileInfo temp = logFiles[i];
                        logFiles[i] = logFiles[j];
                        logFiles[j] = temp;
                    }
                }
            }
            
            // Return top 5 most recent
            int count = 0;
            for (const auto& info : logFiles) {
                if (count >= 5) break;
                if (!first) json += ",";
                json += "{\"filename\":\"";
                json += info.filename;
                json += "\",\"size\":";
                json += String(info.size);
                json += "}";
                first = false;
                count++;
            }
        }
        
        json += "]";
        return response->send(200, "application/json", json.c_str());
    });
    
    // GET /api/log/archive?file=... - Get archived log file
    server.on("/api/log/archive", HTTP_GET, [addCorsHeaders](PsychicRequest *request, PsychicResponse *response) {
        addCorsHeaders(response);
        String filename = request->getParam("file") ? request->getParam("file")->value() : "";
        
        // If no filename specified, try the default archive
        if (filename.length() == 0) {
            FILINFO fno;
            FRESULT statRes = f_stat(LOG_ARCHIVE, &fno);
            if (statRes == FR_OK) {
                filename = String(LOG_ARCHIVE);
                int lastSlash = filename.lastIndexOf('/');
                if (lastSlash >= 0) {
                    filename = filename.substring(lastSlash + 1);
                }
            }
        }
        
        if (filename.length() == 0) {
            return response->send(404, "text/plain", "No archived log file found. Log rotation has not occurred yet.");
        }
        
        // Security: ensure filename doesn't contain path traversal
        if (filename.indexOf("..") >= 0 || filename.indexOf("/") >= 0) {
            return response->send(400, "text/plain", "Invalid filename");
        }
        
        // Build full path
        String filepath = String(LOG_DIR) + "/" + filename;
        String content = readSDFile(filepath.c_str());
        if (content.length() == 0) {
            return response->send(404, "text/plain", "File not found or empty");
        }
        
        return response->send(200, "text/plain", content.c_str());
    });
    
    // POST /api/log/flush - Flush log file
    server.on("/api/log/flush", HTTP_POST, [addCorsHeaders](PsychicRequest *request, PsychicResponse *response) {
        addCorsHeaders(response);
        logFlush();
        return response->send(200, "application/json", "{\"success\":true}");
    });
    
    // POST/GET /api/close - Close server
    server.on("/api/close", HTTP_ANY, [&serverShouldClose, addCorsHeaders](PsychicRequest *request, PsychicResponse *response) {
        addCorsHeaders(response);
        Serial.println("Close request received - shutting down management interface");
        serverShouldClose = true;
        return response->send(200, "application/json", "{\"success\":true,\"message\":\"Management interface closing\"}");
    });
    
    // POST /api/activity - Update last activity time (called by JavaScript on user interaction)
    // Declare lastActivityTime before the server loop so it can be shared
    uint32_t lastActivityTime = millis();
    server.on("/api/activity", HTTP_POST, [&lastActivityTime, addCorsHeaders](PsychicRequest *request, PsychicResponse *response) {
        addCorsHeaders(response);
        lastActivityTime = millis();
        return response->send(200, "application/json", "{\"success\":true}");
    });
    
    // POST /api/ota/start - Start OTA update mode
    server.on("/api/ota/start", HTTP_POST, [addCorsHeaders](PsychicRequest *request, PsychicResponse *response) {
        addCorsHeaders(response);
        Serial.println("OTA start request received from web interface");
        
        // Start OTA server in a separate task (non-blocking)
        TaskHandle_t otaTaskHandle = nullptr;
        xTaskCreatePinnedToCore(ota_server_task, "ota_server", 16384, nullptr, 5, &otaTaskHandle, 0);
        
        if (otaTaskHandle != nullptr) {
            String ip = WiFi.localIP().toString();
            String resp = "{\"success\":true,\"message\":\"OTA server starting\",\"ip\":\"";
            resp += ip;
            resp += "\",\"url\":\"http://";
            resp += ip;
            resp += "/update\"}";
            return response->send(200, "application/json", resp.c_str());
        } else {
            return response->send(500, "application/json", "{\"success\":false,\"error\":\"Failed to start OTA server task\"}");
        }
    });
    
    // Start the server
    server.begin();
    delay(100);
    
    Serial.println("\n========================================");
    Serial.println("MANAGEMENT INTERFACE STARTED (PsychicHttp)");
    Serial.println("========================================");
    Serial.printf("Device IP: %s\n", WiFi.localIP().toString().c_str());
    #ifdef PSY_ENABLE_SSL
    Serial.println("Access management interface at: https://" + WiFi.localIP().toString() + ":443");
    #else
    Serial.println("Access management interface at: http://" + WiFi.localIP().toString());
    #endif
    Serial.println("(Server will run until timeout (5 min inactivity) or explicit close via web interface)");
    Serial.println("========================================\n");
    
    uint32_t startTime = millis();
    lastActivityTime = startTime;  // Initialize with start time (variable already declared above)
    const uint32_t timeoutMs = 300000;  // 5 minute timeout (300 seconds)
    
    // Simple loop - PsychicHttp handles requests internally
    // Check for timeout based on last activity, not just start time
    while (!serverShouldClose) {
        uint32_t now = millis();
        uint32_t timeSinceActivity = now - lastActivityTime;
        
        // Timeout after 5 minutes of inactivity
        if (timeSinceActivity >= timeoutMs) {
            Serial.println("Management interface timeout (5 minutes of inactivity)");
            break;
        }
        
        delay(100);  // Yield to other tasks
    }
    
    // Stop the server
    server.stop();
    
    if (millis() - startTime >= timeoutMs) {
        Serial.println("Management interface timeout");
    } else {
        Serial.println("Management interface closed");
    }
    
    return true;
}

// OLD CODE REMOVED - All endpoints now handled by PsychicHttp routes above

/**
 * Handle !ping command - publish a ping response to MQTT with sender number
 */
bool handlePingCommand(const String& originalMessage) {
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
    // Use char buffer instead of String to avoid heap fragmentation
    char formResponse[256];  // Fixed-size buffer: "To=" (3) + senderNumber (max ~20) + "&From=+447401492609" (20) + "&Body=Pong" (10) = ~53 bytes, plenty of room
    snprintf(formResponse, sizeof(formResponse), "To=%s&From=+447401492609&Body=Pong", senderNumber.c_str());
    
    // Publish ping response
    esp_mqtt_client_handle_t client = getMqttClient();
    const char* topic = getMqttTopicPublish();
    if (client != nullptr && topic != nullptr && strlen(topic) > 0) {
        int msg_id = esp_mqtt_client_publish(client, topic, formResponse, strlen(formResponse), 1, 0);
        if (msg_id > 0) {
            Serial.printf("Published ping response to %s (msg_id: %d): %s\n", topic, msg_id, formResponse);
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
 * Handle !ip command - publish current IP address to MQTT with sender number
 */
bool handleIpCommand(const String& originalMessage) {
    Serial.println("Processing !ip command...");
    
    // Check if WiFi is connected
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("ERROR: WiFi not connected - cannot get IP address");
        
        // Still try to send error message via MQTT if possible
        String senderNumber = extractFromFieldFromMessage(originalMessage);
        if (senderNumber.length() > 0) {
            if (mqttConnect()) {
                delay(1000);
                // Use char buffer instead of String to avoid heap fragmentation
                char formResponse[256];
                snprintf(formResponse, sizeof(formResponse), "To=%s&From=+447401492609&Body=WiFi+not+connected", senderNumber.c_str());
                
                esp_mqtt_client_handle_t client = getMqttClient();
                const char* topic = getMqttTopicPublish();
                if (client != nullptr && topic != nullptr && strlen(topic) > 0) {
                    esp_mqtt_client_publish(client, topic, formResponse, strlen(formResponse), 1, 0);
                    delay(500);
                }
                mqttDisconnect();
            }
        }
        return false;
    }
    
    // Get current IP address
    IPAddress ip = WiFi.localIP();
    String ipString = ip.toString();
    Serial.printf("Current IP address: %s\n", ipString.c_str());
    
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
        Serial.println("ERROR: Failed to connect to MQTT for IP response");
        return false;
    }
    
    // Wait for connection to be established
    delay(1000);
    
    // Build URL-encoded form response: "To=+447816969344&From=+447401492609&Body=192.168.1.100"
    // From is the device's number (hardcoded), To is the sender we're replying to
    // Use char buffer instead of String to avoid heap fragmentation
    char formResponse[256];  // Fixed-size buffer: "To=" (3) + senderNumber (max ~20) + "&From=+447401492609" (20) + "&Body=" (6) + IP (max 15) = ~64 bytes
    snprintf(formResponse, sizeof(formResponse), "To=%s&From=+447401492609&Body=%s", senderNumber.c_str(), ipString.c_str());
    
    // Publish IP response
    esp_mqtt_client_handle_t client = getMqttClient();
    const char* topic = getMqttTopicPublish();
    if (client != nullptr && topic != nullptr && strlen(topic) > 0) {
        int msg_id = esp_mqtt_client_publish(client, topic, formResponse, strlen(formResponse), 1, 0);
        if (msg_id > 0) {
            Serial.printf("Published IP address to %s (msg_id: %d): %s\n", topic, msg_id, formResponse);
            // Give it a moment to send
            delay(500);
        } else {
            Serial.println("ERROR: Failed to publish IP response");
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
bool handleNextCommand() {
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
    

    // Mount SD card if needed
    if (!sdCardMounted) {
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
        // Don't auto-publish - we only want to publish on cold boot or when media.txt actually changes
        loadMediaMappingsFromSD(false);
    }
    
    // Check if we have media mappings
    if (!g_media_mappings_loaded || g_media_mappings.size() == 0) {
        Serial.println("ERROR: No media.txt mappings found - cannot advance to next item");
        return false;
    }
    
    // Use unified function for the complete flow (-1 = sequential next)
    bool ok = displayMediaWithOverlay(-1, 100);
    if (!ok) {
        Serial.println("ERROR: Failed to display next image");
        return false;
    }
    
    Serial.println("!next command completed successfully");
    return true;
}

/**
 * Handle !go command - jump to a specific media item by index (1-based)
 * Format: !go <number> (e.g., "!go 1" jumps to the first item, "!go 5" jumps to the 5th item)
 * Validates the index is within bounds before updating
 * Note: User input is 1-based, but internally we use 0-based indexing
 */
bool handleGoCommand(const String& parameter) {
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
    

    // Mount SD card if needed
    if (!sdCardMounted) {
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
        // Don't auto-publish - we only want to publish on cold boot or when media.txt actually changes
        loadMediaMappingsFromSD(false);
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
    
    // Use unified function for the complete flow
    bool ok = displayMediaWithOverlay(targetIndex, 100);
    if (!ok) {
        return false;
    }
    
    Serial.printf("!go command completed successfully - now at item %lu of %zu\n", 
                  (unsigned long)(lastMediaIndex + 1), mediaCount);
    return true;
}

/**
 * Handle !text command - display text centered on screen, as large as possible
 * Format: !text <text with spaces> (e.g., "!text Hello there!")
 * Clears the display buffer and draws the text with outline, centered
 * Supports multi-line wrapping for optimal space usage
 */
static bool handleTextCommand(const String& parameter) {
    return handleTextCommandWithColor(parameter, EL133UF1_WHITE, EL133UF1_BLACK);
}

/**
 * Load a font by name or filename into the global ttf object
 * Returns true if font was loaded successfully, false otherwise
 * 
 * Note: For LittleFS fonts, this function allocates memory that must be kept
 * allocated for as long as the font is in use. The memory is freed when a new
 * font is loaded (if it was dynamically allocated).
 */
static bool loadFontByName(const String& fontName) {
    // Static variable to track dynamically allocated font data (from LittleFS)
    // This needs to persist across function calls so we can free it when loading a new font
    static uint8_t* s_allocatedFontData = nullptr;
    
    // Free previously allocated font data (if any) before loading a new font
    if (s_allocatedFontData != nullptr) {
        free(s_allocatedFontData);
        s_allocatedFontData = nullptr;
    }
    if (fontName.length() == 0) {
        // No font specified - use default OpenSans
        Serial.println("[FONT] No font specified, using default OpenSans");
        if (!ttf.begin(&display)) {
            Serial.println("[FONT] ERROR: Failed to initialize TTF");
            return false;
        }
        if (!ttf.loadFont(opensans_ttf, opensans_ttf_len)) {
            Serial.println("[FONT] ERROR: Failed to load OpenSans font");
            return false;
        }
        Serial.println("[FONT] Loaded default OpenSans font");
        return true;
    }
    
    // Search for font in RTC list
    for (uint8_t i = 0; i < g_rtcFontCount; i++) {
        // Match by name or filename (case-insensitive)
        String listName = String(g_rtcFontList[i].name);
        String listFilename = String(g_rtcFontList[i].filename);
        listName.toLowerCase();
        listFilename.toLowerCase();
        String searchName = fontName;
        searchName.toLowerCase();
        
        if (listName == searchName || listFilename == searchName) {
            Serial.printf("[FONT] Found font: %s (filename: %s, builtin: %s)\n", 
                         g_rtcFontList[i].name, 
                         g_rtcFontList[i].filename,
                         g_rtcFontList[i].isBuiltin ? "yes" : "no");
            
            if (!ttf.begin(&display)) {
                Serial.println("[FONT] ERROR: Failed to initialize TTF");
                return false;
            }
            
            if (g_rtcFontList[i].isBuiltin) {
                // Built-in font (OpenSans)
                if (!ttf.loadFont(opensans_ttf, opensans_ttf_len)) {
                    Serial.println("[FONT] ERROR: Failed to load OpenSans font");
                    return false;
                }
                Serial.println("[FONT] Loaded built-in OpenSans font");
                return true;
            } else {
                // Load from LittleFS
                char fullPath[128];
                snprintf(fullPath, sizeof(fullPath), "/littlefs/%s", g_rtcFontList[i].filename);
                
                FILE* fontFile = fopen(fullPath, "rb");
                if (fontFile == nullptr) {
                    Serial.printf("[FONT] ERROR: Failed to open font file: %s\n", fullPath);
                    return false;
                }
                
                // Get file size
                fseek(fontFile, 0, SEEK_END);
                long fileSize = ftell(fontFile);
                fseek(fontFile, 0, SEEK_SET);
                
                if (fileSize <= 0 || fileSize > 10 * 1024 * 1024) {  // Max 10MB
                    fclose(fontFile);
                    Serial.printf("[FONT] ERROR: Invalid font file size: %ld bytes\n", fileSize);
                    return false;
                }
                
                // Allocate buffer and read font data
                uint8_t* fontData = (uint8_t*)malloc(fileSize);
                if (fontData == nullptr) {
                    fclose(fontFile);
                    Serial.println("[FONT] ERROR: Failed to allocate memory for font");
                    return false;
                }
                
                size_t bytesRead = fread(fontData, 1, fileSize, fontFile);
                fclose(fontFile);
                
                if (bytesRead != (size_t)fileSize) {
                    free(fontData);
                    Serial.printf("[FONT] ERROR: Failed to read font file (read %zu/%ld bytes)\n", bytesRead, fileSize);
                    return false;
                }
                
                // Load font into TTF object
                if (!ttf.loadFont(fontData, fileSize)) {
                    free(fontData);
                    Serial.println("[FONT] ERROR: Failed to load font data");
                    return false;
                }
                
                // Store pointer to allocated font data so we can free it later when loading a new font
                // EL133UF1_TTF keeps a reference to this data, so we must not free it until we load a different font
                s_allocatedFontData = fontData;
                Serial.printf("[FONT] Loaded font from LittleFS: %s (%ld bytes)\n", g_rtcFontList[i].name, fileSize);
                return true;
            }
        }
    }
    
    // Font not found - fall back to default
    Serial.printf("[FONT] WARNING: Font '%s' not found, using default OpenSans\n", fontName.c_str());
    if (!ttf.begin(&display)) {
        Serial.println("[FONT] ERROR: Failed to initialize TTF");
        return false;
    }
    if (!ttf.loadFont(opensans_ttf, opensans_ttf_len)) {
        Serial.println("[FONT] ERROR: Failed to load OpenSans font");
        return false;
    }
    Serial.println("[FONT] Loaded default OpenSans font (fallback)");
    return true;
}

/**
 * Handle text command with specified fill and outline colors
 * Supports multi-line wrapping for optimal space usage
 */
bool handleTextCommandWithColor(const String& parameter, uint8_t fillColor, uint8_t outlineColor, uint8_t bgColor, const String& backgroundImage, const String& fontName) {
    Serial.println("Processing text command with color...");
    Serial.printf("[TEXT] Received colors: fillColor=%d (expected: 0=BLACK, 1=WHITE, 2=YELLOW, 3=RED, 5=BLUE, 6=GREEN), outlineColor=%d, bgColor=%d\n", 
                  fillColor, outlineColor, bgColor);
    
    // Check if parameter was provided
    if (parameter.length() == 0) {
        Serial.println("ERROR: Text command requires text parameter");
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
    
    // Load background image if provided, otherwise clear with background color
    if (backgroundImage.length() > 0) {
        Serial.printf("Loading background image: %s\n", backgroundImage.c_str());
        
        // Ensure SD card is mounted
        if (!sdCardMounted) {
            if (!sdInitDirect(false)) {
                Serial.println("ERROR: Failed to mount SD card for background image");
                return false;
            }
        }
        
        // Build full path (images are in root directory)
        String imagePath = backgroundImage;
        if (!imagePath.startsWith("/")) {
            imagePath = "/" + imagePath;
        }
        String fatfsPath = "0:" + imagePath;
        
        // Check if file exists
        FILINFO fno;
        FRESULT res = f_stat(fatfsPath.c_str(), &fno);
        if (res != FR_OK) {
            Serial.printf("ERROR: Background image file not found: %s (error: %d)\n", fatfsPath.c_str(), res);
            // Fall back to background color
            display.clear(bgColor);
        } else {
            size_t fileSize = fno.fsize;
            
            // Open and read PNG file
            FIL pngFile;
            res = f_open(&pngFile, fatfsPath.c_str(), FA_READ);
            if (res != FR_OK) {
                Serial.printf("ERROR: Failed to open background image: %s (error: %d)\n", fatfsPath.c_str(), res);
                // Fall back to background color
                display.clear(bgColor);
            } else {
                // Allocate PSRAM buffer for PNG data
                uint8_t* pngData = (uint8_t*)hal_psram_malloc(fileSize);
                if (!pngData) {
                    Serial.println("ERROR: Failed to allocate PSRAM for background image");
                    f_close(&pngFile);
                    // Fall back to background color
                    display.clear(bgColor);
                } else {
                    // Read PNG file
                    UINT bytesRead = 0;
                    res = f_read(&pngFile, pngData, fileSize, &bytesRead);
                    f_close(&pngFile);
                    
                    if (res != FR_OK || bytesRead != fileSize) {
                        Serial.printf("ERROR: Failed to read background image: %s (read %u/%zu bytes, error: %d)\n", 
                                     fatfsPath.c_str(), bytesRead, fileSize, res);
                        hal_psram_free(pngData);
                        // Fall back to background color
                        display.clear(bgColor);
                    } else {
                        // Decode PNG on Core 1 (offloads CPU-intensive decoding)
                        Serial.println("Decoding background image on Core 1...");
                        PngDecodeWorkData decodeWork = {0};
                        decodeWork.pngData = pngData;
                        decodeWork.pngDataLen = fileSize;
                        decodeWork.rgbaData = nullptr;
                        decodeWork.width = 0;
                        decodeWork.height = 0;
                        decodeWork.error = 0;
                        decodeWork.success = false;
                        
                        if (!queuePngDecodeWork(&decodeWork)) {
                            Serial.println("ERROR: Failed to decode background image on Core 1");
                            hal_psram_free(pngData);
                            display.clear(bgColor);
                        } else {
                            // Core 1 has completed - RGBA data is now available
                            unsigned char* rgbaData = decodeWork.rgbaData;
                            unsigned width = decodeWork.width;
                            unsigned height = decodeWork.height;
                            
                            if (rgbaData == nullptr || width == 0 || height == 0) {
                                Serial.printf("ERROR: Core 1 decode returned invalid data (width=%u, height=%u)\n", width, height);
                                hal_psram_free(pngData);
                                display.clear(bgColor);
                            } else {
                                hal_psram_free(pngData);  // Free PNG data, we have decoded RGBA now
                                
                                // Convert RGBA to display color format and copy to display buffer
                                Serial.printf("Converting RGBA to display format and drawing (%ux%u)...\n", width, height);
                                display.clear(EL133UF1_WHITE);  // Clear first
                                
                                // Get display dimensions for scaling/centering
                                int16_t displayWidth = display.width();
                                int16_t displayHeight = display.height();
                                
                                // Calculate scaling to fill display (maintain aspect ratio)
                                float scaleX = (float)displayWidth / width;
                                float scaleY = (float)displayHeight / height;
                                float scale = (scaleX < scaleY) ? scaleX : scaleY;  // Use smaller scale to fit
                                
                                int16_t scaledWidth = (int16_t)(width * scale);
                                int16_t scaledHeight = (int16_t)(height * scale);
                                int16_t offsetX = (displayWidth - scaledWidth) / 2;
                                int16_t offsetY = (displayHeight - scaledHeight) / 2;
                                
                                // Convert and draw pixels
                                for (int16_t y = 0; y < scaledHeight; y++) {
                                    int16_t srcY = (int16_t)(y / scale);
                                    if (srcY >= (int16_t)height) srcY = height - 1;
                                    
                                    for (int16_t x = 0; x < scaledWidth; x++) {
                                        int16_t srcX = (int16_t)(x / scale);
                                        if (srcX >= (int16_t)width) srcX = width - 1;
                                        
                                        // Get RGBA pixel
                                        size_t rgbaIdx = (srcY * width + srcX) * 4;
                                        uint8_t r = rgbaData[rgbaIdx];
                                        uint8_t g = rgbaData[rgbaIdx + 1];
                                        uint8_t b = rgbaData[rgbaIdx + 2];
                                        uint8_t a = rgbaData[rgbaIdx + 3];
                                        
                                        // Convert to display color format (using global spectra6Color mapper)
                                        uint8_t displayColor = spectra6Color.mapColorFast(r, g, b);
                                        
                                        // Draw pixel (with alpha blending - if alpha < 128, use white background)
                                        int16_t dstX = offsetX + x;
                                        int16_t dstY = offsetY + y;
                                        if (dstX >= 0 && dstX < displayWidth && dstY >= 0 && dstY < displayHeight) {
                                            if (a >= 128) {
                                                display.setPixel(dstX, dstY, displayColor);
                                            } else {
                                                // Transparent - use background color
                                                display.setPixel(dstX, dstY, bgColor);
                                            }
                                        }
                                    }
                                }
                                
                                // Free RGBA data (allocated by Core 1 using lodepng's malloc)
                                lodepng_free(rgbaData);
                                
                                Serial.println("Background image loaded and drawn successfully");
                            }
                        }
                    }
                }
            }
        }
    } else {
        // No background image - use background color
        Serial.println("Clearing display buffer with background color...");
        display.clear(bgColor);
    }
    
    // Load the requested font (or default if none specified)
    if (!loadFontByName(fontName)) {
        Serial.println("ERROR: Failed to load font, aborting text command");
        return false;
    }
    
    // Get display dimensions
    int16_t displayWidth = display.width();
    int16_t displayHeight = display.height();
    Serial.printf("Display size: %dx%d\n", displayWidth, displayHeight);
    
    // Margin requirements: at least 50 pixels on all sides
    const int16_t margin = 50;
    const int16_t outlineWidth = 3;  // Outline thickness
    const int16_t availableWidth = displayWidth - (margin * 2);
    const int16_t availableHeight = displayHeight - (margin * 2);
    
    // Find optimal font size with text wrapping
    // Use binary search for efficiency
    const float minFontSize = 20.0f;
    const float maxFontSize = 400.0f;
    
    float bestFontSize = minFontSize;
    int bestNumLines = 0;
    char wrappedText[512] = {0};
    int16_t wrappedWidth = 0;
    int16_t lineHeight = 0;
    const int16_t lineGap = 5;  // Gap between lines
    
    // Binary search for optimal font size that fits with wrapping
    float low = minFontSize;
    float high = maxFontSize;
    
    Serial.println("Finding optimal font size with text wrapping...");
    
    while (high - low > 1.0f) {
        float fontSize = (low + high) / 2.0f;
        
        // Wrap text at this font size
        int numLines = 0;
        int16_t maxLineWidth = textPlacement.wrapText(&ttf, parameter.c_str(), fontSize, 
                                                       availableWidth, wrappedText, sizeof(wrappedText),
                                                       &numLines);
        
        if (numLines == 0) {
            // Failed to wrap, try smaller
            high = fontSize;
            continue;
        }
        
        // Calculate total height needed
        int16_t textHeight = ttf.getTextHeight(fontSize);
        int16_t totalHeight = (textHeight * numLines) + (lineGap * (numLines - 1)) + (outlineWidth * 2);
        
        // Check if it fits
        if (maxLineWidth <= availableWidth && totalHeight <= availableHeight) {
            // Fits - try larger
            bestFontSize = fontSize;
            bestNumLines = numLines;
            wrappedWidth = maxLineWidth;
            lineHeight = textHeight;
            low = fontSize;
        } else {
            // Too large - try smaller
            high = fontSize;
        }
    }
    
    // Final wrap at the best size
    int numLines = 0;
    wrappedWidth = textPlacement.wrapText(&ttf, parameter.c_str(), bestFontSize,
                                          availableWidth, wrappedText, sizeof(wrappedText),
                                          &numLines);
    lineHeight = ttf.getTextHeight(bestFontSize);
    int16_t totalHeight = (lineHeight * numLines) + (lineGap * (numLines - 1)) + (outlineWidth * 2);
    
    Serial.printf("Optimal font size: %.1f, %d lines, wrapped width: %d, total height: %d\n", 
                  bestFontSize, numLines, wrappedWidth, totalHeight);
    
    // Calculate centered position
    int16_t centerX = displayWidth / 2;
    int16_t totalTextHeight = (lineHeight * numLines) + (lineGap * (numLines - 1));
    int16_t startY = margin + (availableHeight - totalTextHeight) / 2 + (lineHeight / 2);  // Top of first line baseline
    
    // Draw each line separately, centered
    Serial.println("Drawing wrapped text (line by line)...");
    
    // Make a mutable copy for line splitting
    char wrappedCopy[512];
    strncpy(wrappedCopy, wrappedText, sizeof(wrappedCopy) - 1);
    wrappedCopy[sizeof(wrappedCopy) - 1] = '\0';
    
    char* line = wrappedCopy;
    for (int i = 0; i < numLines && line && *line; i++) {
        // Find end of this line
        char* nextLine = strchr(line, '\n');
        if (nextLine) {
            *nextLine = '\0';
        }
        
        // Draw this line centered with specified colors
        int16_t lineY = startY + i * (lineHeight + lineGap);
        Serial.printf("[TEXT] Drawing line %d with fillColor=%d, outlineColor=%d\n", i, fillColor, outlineColor);
        ttf.drawTextAlignedOutlined(centerX, lineY, line, bestFontSize,
                                    fillColor, outlineColor,
                                    ALIGN_CENTER, ALIGN_MIDDLE, outlineWidth);
        
        // Move to next line
        if (nextLine) {
            line = nextLine + 1;
        } else {
            break;
        }
    }
    
    // Update display (thumbnail publishing happens automatically in display.update())
    Serial.println("Updating display (e-ink refresh - this will take 20-30 seconds)...");
    display.update();
    display.waitForUpdate();  // Wait for actual refresh to complete
    Serial.println("Display updated");
    
    Serial.println("Text command completed successfully");
    return true;
}

/**
 * Handle !multi_text command - display text with randomized colors per character
 * Format: !multi_text <text with spaces>
 * Each character gets a random color from available palette
 * Supports multi-line wrapping like !text
 */
bool handleMultiTextCommand(const String& parameter, uint8_t bgColor) {
    Serial.println("Processing !multi_text command...");
    
    // Check if parameter was provided
    if (parameter.length() == 0) {
        Serial.println("ERROR: !multi_text command requires text parameter");
        return false;
    }
    
    Serial.printf("Text to display (multi-colour): \"%s\"\n", parameter.c_str());
    
    // Ensure display is initialized
    if (display.getBuffer() == nullptr) {
        Serial.println("Display not initialized - initializing now...");
        displaySPI.begin(PIN_SPI_SCK, -1, PIN_SPI_MOSI, -1);
        
        if (!display.begin(PIN_CS0, PIN_CS1, PIN_DC, PIN_RESET, PIN_BUSY)) {
            Serial.println("ERROR: Display initialization failed!");
            return false;
        }
        Serial.println("Display initialized");
    }
    
    // Clear the display buffer with specified background colour
    Serial.printf("Clearing display buffer to colour %d...\n", bgColor);
    display.clear(bgColor);
    
    // Get display dimensions
    int16_t displayWidth = display.width();
    int16_t displayHeight = display.height();
    
    // Margin requirements: at least 50 pixels on all sides
    const int16_t margin = 50;
    const int16_t outlineWidth = 3;
    const int16_t availableWidth = displayWidth - (margin * 2);
    const int16_t availableHeight = displayHeight - (margin * 2);
    
    // Available colours for randomization (excludes BLACK - it uses white outline which looks inconsistent with other colours)
    // All included colours use black outline for visual consistency
    const uint8_t colors[] = {EL133UF1_WHITE, EL133UF1_YELLOW, EL133UF1_RED, EL133UF1_BLUE, EL133UF1_GREEN};
    const int numColors = sizeof(colors) / sizeof(colors[0]);
    
    // Find optimal font size with text wrapping (same logic as handleTextCommandWithColor)
    const float minFontSize = 20.0f;
    const float maxFontSize = 400.0f;
    float bestFontSize = minFontSize;
    int bestNumLines = 0;
    char wrappedText[512] = {0};
    int16_t wrappedWidth = 0;
    int16_t lineHeight = 0;
    const int16_t lineGap = 5;
    
    // Binary search for optimal font size that fits with wrapping
    float low = minFontSize;
    float high = maxFontSize;
    
    Serial.println("Finding optimal font size with text wrapping...");
    
    while (high - low > 1.0f) {
        float fontSize = (low + high) / 2.0f;
        
        // Wrap text at this font size
        int numLines = 0;
        int16_t maxLineWidth = textPlacement.wrapText(&ttf, parameter.c_str(), fontSize, 
                                                       availableWidth, wrappedText, sizeof(wrappedText),
                                                       &numLines);
        
        if (numLines == 0) {
            high = fontSize;
            continue;
        }
        
        // Calculate total height needed
        int16_t textHeight = ttf.getTextHeight(fontSize);
        int16_t totalHeight = (textHeight * numLines) + (lineGap * (numLines - 1)) + (outlineWidth * 2);
        
        // Check if it fits
        if (maxLineWidth <= availableWidth && totalHeight <= availableHeight) {
            bestFontSize = fontSize;
            bestNumLines = numLines;
            wrappedWidth = maxLineWidth;
            lineHeight = textHeight;
            low = fontSize;
        } else {
            high = fontSize;
        }
    }
    
    // Final wrap at the best size
    int numLines = 0;
    wrappedWidth = textPlacement.wrapText(&ttf, parameter.c_str(), bestFontSize,
                                          availableWidth, wrappedText, sizeof(wrappedText),
                                          &numLines);
    lineHeight = ttf.getTextHeight(bestFontSize);
    
    Serial.printf("Optimal font size: %.1f, %d lines\n", bestFontSize, numLines);
    
    // Calculate centered position
    int16_t centerX = displayWidth / 2;
    int16_t totalTextHeight = (lineHeight * numLines) + (lineGap * (numLines - 1));
    int16_t startY = margin + (availableHeight - totalTextHeight) / 2 + (lineHeight / 2);
    
    // Draw each line, character by character with random colours
    Serial.println("Drawing multi-colour text (character by character, line by line)...");
    
    // Make a mutable copy for line splitting
    char wrappedCopy[512];
    strncpy(wrappedCopy, wrappedText, sizeof(wrappedCopy) - 1);
    wrappedCopy[sizeof(wrappedCopy) - 1] = '\0';
    
    char* line = wrappedCopy;
    for (int lineIdx = 0; lineIdx < numLines && line && *line; lineIdx++) {
        // Find end of this line
        char* nextLine = strchr(line, '\n');
        if (nextLine) {
            *nextLine = '\0';
        }
        
        // Calculate line width and starting X position (centered)
        int16_t lineWidth = ttf.getTextWidth(line, bestFontSize);
        int16_t lineStartX = centerX - (lineWidth / 2);
        int16_t lineY = startY + lineIdx * (lineHeight + lineGap);
        
        // Draw each character in this line with random colour
        int16_t currentX = lineStartX;
        int lineLen = strlen(line);
        uint8_t lastColor = 255; // Track last colour to avoid consecutive duplicates
        
        for (int i = 0; i < lineLen; i++) {
            char ch[2] = {line[i], '\0'};
            
            // Skip spaces
            if (ch[0] == ' ') {
                int16_t spaceWidth = ttf.getTextWidth(" ", bestFontSize);
                currentX += spaceWidth;
                continue;
            }
            
            // Random colour for this character (all colours use black outline for consistency)
            // Ensure consecutive characters never have the same colour
            uint8_t fillColor;
            do {
                fillColor = colors[random(numColors)];
            } while (fillColor == lastColor && numColors > 1);
            lastColor = fillColor;
            uint8_t outlineColor = EL133UF1_BLACK;
            
            // Get width of this character
            int16_t charWidth = ttf.getTextWidth(ch, bestFontSize);
            
            // Draw character at current position
            ttf.drawTextAlignedOutlined(currentX + (charWidth / 2), lineY, ch, bestFontSize,
                                        fillColor, outlineColor,
                                        ALIGN_CENTER, ALIGN_MIDDLE, outlineWidth);
            
            // Advance X position
            currentX += charWidth;
        }
        
        // Move to next line
        if (nextLine) {
            line = nextLine + 1;
        } else {
            break;
        }
    }
    
    // Update display (thumbnail publishing happens automatically in display.update())
    Serial.println("Updating display (e-ink refresh - this will take 20-30 seconds)...");
    display.update();
    display.waitForUpdate();  // Wait for actual refresh to complete
    Serial.println("Display updated");
    
    Serial.println("!multi_text command completed successfully");
    return true;
}

/**
 * Handle multi-fade text command - displays text with smooth gradient fades between colours using dithering
 */
static bool handleMultiFadeTextCommand(const String& parameter, uint8_t bgColor) {
    Serial.println("Processing !multi_fade_text command...");
    
    // Check if parameter was provided
    if (parameter.length() == 0) {
        Serial.println("ERROR: !multi_fade_text command requires text parameter");
        return false;
    }
    
    Serial.printf("Text to display (multi-fade): \"%s\"\n", parameter.c_str());
    
    // Ensure display is initialized
    if (display.getBuffer() == nullptr) {
        Serial.println("Display not initialized - initializing now...");
        displaySPI.begin(PIN_SPI_SCK, -1, PIN_SPI_MOSI, -1);
        
        if (!display.begin(PIN_CS0, PIN_CS1, PIN_DC, PIN_RESET, PIN_BUSY)) {
            Serial.println("ERROR: Display initialization failed!");
            return false;
        }
        Serial.println("Display initialized");
    }
    
    // Clear the display buffer with specified background colour
    Serial.printf("Clearing display buffer to colour %d...\n", bgColor);
    display.clear(bgColor);
    
    // Get display dimensions
    int16_t displayWidth = display.width();
    int16_t displayHeight = display.height();
    
    // Margin requirements: at least 50 pixels on all sides
    const int16_t margin = 50;
    const int16_t outlineWidth = 3;
    const int16_t availableWidth = displayWidth - (margin * 2);
    const int16_t availableHeight = displayHeight - (margin * 2);
    
    // Create colour map for dithering
    static Spectra6ColorMap colorMap;
    colorMap.setMode(COLOR_MAP_DITHER);
    colorMap.resetDither();
    
    // Define good colour pairs for gradients (RGB values for e-ink colours)
    // Format: {startColor, endColor, startR, startG, startB, endR, endG, endB}
    struct ColorPair {
        uint8_t startColor;
        uint8_t endColor;
        uint8_t startR, startG, startB;
        uint8_t endR, endG, endB;
    };
    
    // RGB values for e-ink colours (using default palette values from EL133UF1_Color.cpp)
    // These are realistic e-ink colour values (less saturated than pure RGB)
    const uint8_t yellowR = 245, yellowG = 210, yellowB = 50;   // Warm yellow
    const uint8_t redR = 190, redG = 60, redB = 55;            // Brick/tomato red
    const uint8_t blueR = 45, blueG = 75, blueB = 160;        // Deep navy blue
    const uint8_t greenR = 55, greenG = 140, greenB = 85;     // Teal/forest green
    const uint8_t whiteR = 245, whiteG = 245, whiteB = 235;   // Slightly warm off-white
    
    // Define colour pairs for smooth gradients
    const ColorPair colorPairs[] = {
        {EL133UF1_YELLOW, EL133UF1_RED, yellowR, yellowG, yellowB, redR, redG, redB},
        {EL133UF1_RED, EL133UF1_BLUE, redR, redG, redB, blueR, blueG, blueB},
        {EL133UF1_BLUE, EL133UF1_GREEN, blueR, blueG, blueB, greenR, greenG, greenB},
        {EL133UF1_GREEN, EL133UF1_YELLOW, greenR, greenG, greenB, yellowR, yellowG, yellowB},
        {EL133UF1_WHITE, EL133UF1_YELLOW, whiteR, whiteG, whiteB, yellowR, yellowG, yellowB}
    };
    const int numPairs = sizeof(colorPairs) / sizeof(colorPairs[0]);
    
    // Find optimal font size with text wrapping (same logic as handleTextCommandWithColor)
    const float minFontSize = 20.0f;
    const float maxFontSize = 400.0f;
    float bestFontSize = minFontSize;
    int bestNumLines = 0;
    char wrappedText[512] = {0};
    int16_t wrappedWidth = 0;
    int16_t lineHeight = 0;
    const int16_t lineGap = 5;
    
    // Binary search for optimal font size that fits with wrapping
    float low = minFontSize;
    float high = maxFontSize;
    
    Serial.println("Finding optimal font size with text wrapping...");
    
    while (high - low > 1.0f) {
        float fontSize = (low + high) / 2.0f;
        
        // Wrap text at this font size
        int numLines = 0;
        int16_t maxLineWidth = textPlacement.wrapText(&ttf, parameter.c_str(), fontSize, 
                                                       availableWidth, wrappedText, sizeof(wrappedText),
                                                       &numLines);
        
        if (numLines == 0) {
            high = fontSize;
            continue;
        }
        
        // Calculate total height needed
        int16_t textHeight = ttf.getTextHeight(fontSize);
        int16_t totalHeight = (textHeight * numLines) + (lineGap * (numLines - 1)) + (outlineWidth * 2);
        
        // Check if it fits
        if (maxLineWidth <= availableWidth && totalHeight <= availableHeight) {
            bestFontSize = fontSize;
            bestNumLines = numLines;
            wrappedWidth = maxLineWidth;
            lineHeight = textHeight;
            low = fontSize;
        } else {
            high = fontSize;
        }
    }
    
    // Final wrap at the best size
    int numLines = 0;
    wrappedWidth = textPlacement.wrapText(&ttf, parameter.c_str(), bestFontSize,
                                          availableWidth, wrappedText, sizeof(wrappedText),
                                          &numLines);
    lineHeight = ttf.getTextHeight(bestFontSize);
    
    Serial.printf("Optimal font size: %.1f, %d lines\n", bestFontSize, numLines);
    
    // Calculate centered position
    int16_t centerX = displayWidth / 2;
    int16_t totalTextHeight = (lineHeight * numLines) + (lineGap * (numLines - 1));
    int16_t startY = margin + (availableHeight - totalTextHeight) / 2 + (lineHeight / 2);
    
    // Count total characters (excluding spaces) for gradient calculation
    int totalChars = 0;
    for (int i = 0; i < (int)strlen(wrappedText); i++) {
        if (wrappedText[i] != ' ' && wrappedText[i] != '\n') {
            totalChars++;
        }
    }
    
    Serial.printf("Total characters (excluding spaces): %d\n", totalChars);
    
    // Draw each line with gradient fade
    Serial.println("Drawing multi-fade text with dithering (character by character, line by line)...");
    
    // Make a mutable copy for line splitting
    char wrappedCopy[512];
    strncpy(wrappedCopy, wrappedText, sizeof(wrappedCopy) - 1);
    wrappedCopy[sizeof(wrappedCopy) - 1] = '\0';
    
    char* line = wrappedCopy;
    int charIndex = 0; // Track character position across all lines
    
    for (int lineIdx = 0; lineIdx < numLines && line && *line; lineIdx++) {
        // Reset dithering for each new line
        colorMap.resetDither();
        
        // Find end of this line
        char* nextLine = strchr(line, '\n');
        if (nextLine) {
            *nextLine = '\0';
        }
        
        // Calculate line width and starting X position (centered)
        int16_t lineWidth = ttf.getTextWidth(line, bestFontSize);
        int16_t lineStartX = centerX - (lineWidth / 2);
        int16_t lineY = startY + lineIdx * (lineHeight + lineGap);
        
        // Count characters in this line (excluding spaces)
        int lineCharCount = 0;
        for (int i = 0; i < (int)strlen(line); i++) {
            if (line[i] != ' ') {
                lineCharCount++;
            }
        }
        
        // Draw each character in this line with gradient colour
        int16_t currentX = lineStartX;
        int lineLen = strlen(line);
        int lineCharIdx = 0; // Character index within this line
        
        for (int i = 0; i < lineLen; i++) {
            char ch[2] = {line[i], '\0'};
            
            // Skip spaces
            if (ch[0] == ' ') {
                int16_t spaceWidth = ttf.getTextWidth(" ", bestFontSize);
                currentX += spaceWidth;
                continue;
            }
            
            // Calculate position in overall gradient (0.0 to 1.0)
            float gradientPos = (totalChars > 0) ? ((float)charIndex / (float)totalChars) : 0.0f;
            
            // Select colour pair based on gradient position (cycle through pairs)
            int pairIdx = (int)(gradientPos * numPairs * 2.0f) % numPairs;
            const ColorPair& pair = colorPairs[pairIdx];
            
            // Calculate position within current pair (0.0 to 1.0)
            float pairPos = fmod(gradientPos * numPairs * 2.0f, 2.0f);
            if (pairPos > 1.0f) {
                // Reverse direction for second half
                pairPos = 2.0f - pairPos;
            }
            
            // Interpolate RGB values
            uint8_t r = (uint8_t)(pair.startR + (pair.endR - pair.startR) * pairPos);
            uint8_t g = (uint8_t)(pair.startG + (pair.endG - pair.startG) * pairPos);
            uint8_t b = (uint8_t)(pair.startB + (pair.endB - pair.startB) * pairPos);
            
            // Use dithering to map RGB to e-ink colour
            // Calculate character position in line for dithering
            int16_t charX = currentX - lineStartX;
            uint8_t fillColor = colorMap.mapColorDithered(charX, lineIdx, r, g, b, lineWidth);
            uint8_t outlineColor = EL133UF1_BLACK;
            
            // Get width of this character
            int16_t charWidth = ttf.getTextWidth(ch, bestFontSize);
            
            // Draw character at current position
            ttf.drawTextAlignedOutlined(currentX + (charWidth / 2), lineY, ch, bestFontSize,
                                        fillColor, outlineColor,
                                        ALIGN_CENTER, ALIGN_MIDDLE, outlineWidth);
            
            // Advance X position and character indices
            currentX += charWidth;
            charIndex++;
            lineCharIdx++;
        }
        
        // Move to next line
        if (nextLine) {
            line = nextLine + 1;
        } else {
            break;
        }
    }
    
    // Update display (thumbnail publishing happens automatically in display.update())
    Serial.println("Updating display (e-ink refresh - this will take 20-30 seconds)...");
    display.update();
    display.waitForUpdate();  // Wait for actual refresh to complete
    Serial.println("Display updated");
    
    Serial.println("!multi_fade_text command completed successfully");
    return true;
}

/**
 * Handle !get command - download a file via HTTP/HTTPS to SD card
 * Format: !get <url> [filename]
 * Examples: 
 *   !get https://example.com/image.png
 *   !get https://example.com/data.txt downloaded.txt
 * If filename is not provided, extracts it from the URL
 */
static bool handleGetCommand(const String& parameter) {
    Serial.println("Processing !get command...");
    
    // Check if parameter was provided
    if (parameter.length() == 0) {
        Serial.println("ERROR: !get command requires URL parameter (e.g., !get https://example.com/file.png)");
        return false;
    }
    
    Serial.printf("URL to download: %s\n", parameter.c_str());
    
    // Ensure WiFi is connected
    if (!wifiLoadCredentials()) {
        Serial.println("ERROR: WiFi credentials not available");
        return false;
    }
    
    if (!wifiConnectPersistent(5, 30000, false)) {
        Serial.println("ERROR: Failed to connect to WiFi");
        return false;
    }
    
    Serial.println("WiFi connected");

    
    // Mount SD card if needed
    if (!sdCardMounted) {
        Serial.println("Mounting SD card...");
        if (!sdInitDirect(false)) {
            Serial.println("ERROR: Failed to mount SD card!");
            return false;
        }
        Serial.println("SD card mounted");
    }
    
    // Parse URL and optional filename
    String url = parameter;
    String filename = "";
    
    // Check if there's a space (URL and filename separated)
    int spacePos = url.indexOf(' ');
    if (spacePos > 0) {
        filename = url.substring(spacePos + 1);
        filename.trim();
        url = url.substring(0, spacePos);
        url.trim();
    }
    
    // If no filename provided, extract from URL
    if (filename.length() == 0) {
        // Extract filename from URL (last part after /)
        int lastSlash = url.lastIndexOf('/');
        if (lastSlash >= 0 && lastSlash < (int)url.length() - 1) {
            filename = url.substring(lastSlash + 1);
            // Remove query parameters if any
            int questionMark = filename.indexOf('?');
            if (questionMark >= 0) {
                filename = filename.substring(0, questionMark);
            }
        } else {
            // No filename in URL, use default
            filename = "downloaded_file";
        }
    }
    
    Serial.printf("Downloading: %s\n", url.c_str());
    Serial.printf("Saving to: %s\n", filename.c_str());
    

    // Download file
    HTTPClient http;
    WiFiClientSecure secureClient;
    WiFiClient plainClient;
    
    // Check if HTTPS
    bool isHttps = url.startsWith("https://");
    if (isHttps) {
        secureClient.setInsecure();  // For testing - use proper cert validation in production
        http.begin(secureClient, url);
    } else {
        http.begin(plainClient, url);
    }
    
    http.setTimeout(30000);  // 30 second timeout
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    
    Serial.println("Starting download...");
    int httpCode = http.GET();
    
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("HTTP error: %d\n", httpCode);
        String errorPayload = http.getString();
        if (errorPayload.length() > 0) {
            Serial.printf("Error response: %s\n", errorPayload.c_str());
        }
        http.end();
        return false;
    }
    
    // Get content length
    int contentLength = http.getSize();
    Serial.printf("Content length: %d bytes\n", contentLength);
    
    // Build FatFs path (0: is the drive prefix)
    String fatfsPath = "0:/";
    fatfsPath += filename;
    
    // Open file for writing
    FIL file;
    FRESULT res = f_open(&file, fatfsPath.c_str(), FA_WRITE | FA_CREATE_ALWAYS);
    if (res != FR_OK) {
        Serial.printf("ERROR: Failed to open file for writing: %d\n", res);
        http.end();
        return false;
    }
    
    // Download and write in chunks
    uint8_t buffer[512];
    uint32_t totalBytes = 0;
    uint32_t lastProgress = 0;
    
    WiFiClient* stream = http.getStreamPtr();
    
    Serial.println("Downloading and writing to SD card...");
    while (http.connected() && (contentLength == -1 || totalBytes < (uint32_t)contentLength)) {
        size_t available = stream->available();
        if (available > 0) {
            int bytesRead = stream->readBytes(buffer, min(available, (size_t)sizeof(buffer)));
            if (bytesRead > 0) {
                UINT bytesWritten = 0;
                res = f_write(&file, buffer, bytesRead, &bytesWritten);
                if (res != FR_OK || bytesWritten != (UINT)bytesRead) {
                    Serial.printf("ERROR: Failed to write to file: %d (wrote %u of %d)\n", res, bytesWritten, bytesRead);
                    f_close(&file);
                    http.end();
                    return false;
                }
                totalBytes += bytesWritten;
                
                // Print progress every 10KB
                if (totalBytes - lastProgress >= 10240) {
                    Serial.printf("Downloaded: %u bytes", totalBytes);
                    if (contentLength > 0) {
                        Serial.printf(" (%.1f%%)", (float)totalBytes * 100.0f / (float)contentLength);
                    }
                    Serial.println();
                    lastProgress = totalBytes;
                }
            }
        } else {
            delay(10);
        }
    }
    
    // Sync and close file
    f_sync(&file);
    f_close(&file);
    http.end();
    
    Serial.printf("Download complete: %u bytes written to %s\n", totalBytes, filename.c_str());
    
    return true;
}


bool handleVolumeCommand(const String& parameter) {
    Serial.println("Processing !volume command...");
    
    if (parameter.length() == 0) {
        Serial.printf("Current volume: %d%%\n", g_audio_volume_pct);
        Serial.println("Usage: !volume <0-100>");
        return false;
    }
    
    int newVolume = parameter.toInt();
    if (newVolume < 0 || newVolume > 100) {
        Serial.printf("ERROR: Volume must be between 0 and 100 (got: %d)\n", newVolume);
        return false;
    }
    
    // Update volume
    g_audio_volume_pct = newVolume;
    
    // Save to NVS
    volumeSaveToNVS();
    
    // Apply volume if codec is ready
    if (g_codec_ready) {
        (void)g_codec.setDacVolumePercentMapped(g_audio_volume_pct, kCodecVolumeMinPct, kCodecVolumeMaxPct);
        Serial.printf("Volume set to %d%% (mapped to codec range %d..%d%%)\n", 
                     g_audio_volume_pct, kCodecVolumeMinPct, kCodecVolumeMaxPct);
    } else {
        Serial.printf("Volume set to %d%% (will be applied when audio starts)\n", g_audio_volume_pct);
    }
    
    return true;
}

/**
// NVS storage functions moved to nvs_manager.cpp/h module (Priority 1 refactoring)
// All NVS load/save functions are now in src/nvs_manager.cpp

/**
 * Handle !sleep_interval command - set sleep interval between MQTT wake-ups
 * Format: !sleep_interval <minutes>
 * Example: !sleep_interval 2 (wake every 2 minutes)
 *          !sleep_interval 4 (wake every 4 minutes)
 * Must be a factor of 60: 1, 2, 3, 4, 5, 6, 10, 12, 15, 20, 30, 60
 * Default is 1 (wake every minute)
 */
bool handleSleepIntervalCommand(const String& parameter) {
    Serial.println("Processing !sleep_interval command...");
    
    if (parameter.length() == 0) {
        Serial.printf("Current sleep interval: %d minutes\n", g_sleep_interval_minutes);
        Serial.println("Usage: !sleep_interval <minutes>");
        Serial.println("Valid values (must be factors of 60): 1, 2, 3, 4, 5, 6, 10, 12, 15, 20, 30, 60");
        return false;
    }
    
    int newInterval = parameter.toInt();
    
    // Validate: must be a factor of 60
    if (newInterval <= 0 || newInterval > 60 || 60 % newInterval != 0) {
        Serial.printf("ERROR: Sleep interval must be a factor of 60 (got: %d)\n", newInterval);
        Serial.println("Valid values: 1, 2, 3, 4, 5, 6, 10, 12, 15, 20, 30, 60");
        return false;
    }
    
    g_sleep_interval_minutes = (uint8_t)newInterval;
    sleepDurationSaveToNVS();
    
    Serial.printf("Sleep interval set to %d minutes\n", g_sleep_interval_minutes);
    Serial.printf("Device will wake at: ");
    for (int i = 0; i < 60; i += newInterval) {
        Serial.printf(":%02d", i);
        if (i + newInterval < 60) Serial.print(", ");
    }
    Serial.println(" (and always at :00 for hourly media cycle)");
    
    return true;
}

/**
 * Handle !newno command - add a phone number to the allowed list
 * Format: !newno <phone_number>
 * Example: !newno +447401492609
 * Note: This command requires the sender to be in the allowed list (hardcoded number +447816969344 can always add numbers)
 */
bool handleNewNumberCommand(const String& parameter) {
    Serial.println("Processing !newno command...");
    
    if (parameter.length() == 0) {
        Serial.println("ERROR: !newno command requires phone number parameter (e.g., !newno +447401492609)");
        return false;
    }
    
    String number = parameter;
    number.trim();
    
    // Validate that it looks like a phone number (starts with + and has digits)
    if (!number.startsWith("+") || number.length() < 4) {
        Serial.printf("ERROR: Invalid phone number format: %s (must start with + and be at least 4 characters)\n", number.c_str());
        return false;
    }
    
    // Check if it's already the hardcoded number
    if (number == "+447816969344") {
        Serial.println("This number is already hardcoded as allowed - no need to add it");
        return true;
    }
    
    // Add to NVS
    if (addAllowedNumber(number)) {
        Serial.printf("Successfully added number to allowed list: %s\n", number.c_str());
        return true;
    } else {
        Serial.printf("ERROR: Failed to add number: %s\n", number.c_str());
        return false;
    }
}

/**
 * Check if a phone number is in the allowed list (hardcoded + NVS)
 */
bool isNumberAllowed(const String& number) {
    // Check hardcoded number first
    if (number == "+447816969344") {
        return true;
    }
    
    // Load and check NVS numbers
    // Try read-write mode first (creates namespace if it doesn't exist), then check
    // If namespace doesn't exist, it's fine - just means no numbers stored yet
    if (!numbersPrefs.begin("numbers", false)) {  // Read-write (creates if needed)
        // Only print error if it's a real failure (not just namespace doesn't exist)
        // For now, just return false silently - namespace doesn't exist means no numbers stored
        return false;
    }
    
    // Get count of stored numbers
    int count = numbersPrefs.getInt("count", 0);
    bool found = false;
    
    for (int i = 0; i < count && i < 100; i++) {  // Limit to 100 numbers max
        char key[16];
        snprintf(key, sizeof(key), "num%d", i);
        String storedNumber = numbersPrefs.getString(key, "");
        
        if (storedNumber == number) {
            found = true;
            break;
        }
    }
    
    numbersPrefs.end();
    return found;
}

/**
 * Add a phone number to the allowed list in NVS
 */
bool addAllowedNumber(const String& number) {
    // Check hardcoded number first
    if (number == "+447816969344") {
        Serial.println("This number is already hardcoded as allowed - no need to add it");
        return true;
    }
    
    // Open namespace for writing (creates if it doesn't exist)
    // Note: Preferences library may log an error when creating namespace for first time - this is harmless
    if (!numbersPrefs.begin("numbers", false)) {  // Read-write (creates if needed)
        Serial.println("ERROR: Failed to open NVS for saving numbers");
        return false;
    }
    
    // Check if number already exists by scanning stored numbers
    int count = numbersPrefs.getInt("count", 0);
    bool found = false;
    
    for (int i = 0; i < count && i < 100; i++) {
        char key[16];
        snprintf(key, sizeof(key), "num%d", i);
        String storedNumber = numbersPrefs.getString(key, "");
        
        if (storedNumber == number) {
            found = true;
            break;
        }
    }
    
    if (found) {
        Serial.printf("Number %s is already in the allowed list\n", number.c_str());
        numbersPrefs.end();
        return true;  // Already exists, consider it success
    }
    
    // Check limit (max 100 numbers)
    if (count >= 100) {
        Serial.println("ERROR: Maximum number of allowed numbers (100) reached");
        numbersPrefs.end();
        return false;
    }
    
    // Store the new number
    char key[16];
    snprintf(key, sizeof(key), "num%d", count);
    numbersPrefs.putString(key, number);
    
    // Update count
    count++;
    numbersPrefs.putInt("count", count);
    
    numbersPrefs.end();
    
    Serial.printf("Added number %s to allowed list (total: %d)\n", number.c_str(), count);
    return true;
}

/**
 * Load allowed numbers from NVS (called on startup for verification/debugging)
 */
void numbersLoadFromNVS() {
    if (!numbersPrefs.begin("numbers", false)) {  // Read-write (creates if needed, but we're just reading)
        Serial.println("No allowed numbers list in NVS (only hardcoded number will be allowed)");
        return;
    }
    
    int count = numbersPrefs.getInt("count", 0);
    if (count == 0) {
        if (g_is_cold_boot) {
            Serial.println("No additional allowed numbers in NVS (only hardcoded number)");
        }
        numbersPrefs.end();
        return;
    }
    
    if (g_is_cold_boot) {
        Serial.printf("Loaded %d allowed number(s) from NVS:\n", count);
        for (int i = 0; i < count && i < 100; i++) {
            char key[16];
            snprintf(key, sizeof(key), "num%d", i);
            String number = numbersPrefs.getString(key, "");
            if (number.length() > 0) {
                Serial.printf("  [%d] %s\n", i + 1, number.c_str());
            }
        }
    }
    
    numbersPrefs.end();
}

/**
 * Handle !delno command - remove a phone number from the allowed list
 * Format: !delno <phone_number>
 * Example: !delno +447401492609
 * Note: Cannot remove the hardcoded number +447816969344
 */
bool handleDelNumberCommand(const String& parameter) {
    Serial.println("Processing !delno command...");
    
    if (parameter.length() == 0) {
        Serial.println("ERROR: !delno command requires phone number parameter (e.g., !delno +447401492609)");
        return false;
    }
    
    String number = parameter;
    number.trim();
    
    // Cannot remove hardcoded number
    if (number == "+447816969344") {
        Serial.println("ERROR: Cannot remove hardcoded number +447816969344");
        return false;
    }
    
    // Remove from NVS
    if (removeAllowedNumber(number)) {
        Serial.printf("Successfully removed number from allowed list: %s\n", number.c_str());
        return true;
    } else {
        Serial.printf("ERROR: Failed to remove number or number not found: %s\n", number.c_str());
        return false;
    }
}

/**
 * Handle !list command - list all media files from media.txt
 * Returns list via serial output and publishes to MQTT outbox
 * Uses 1-indexed numbering to match !go command convention
 */
bool handleListNumbersCommand(const String& originalMessage) {
    Serial.println("Processing !list command...");
    

    // Mount SD card if needed
    if (!sdCardMounted) {
        Serial.println("Mounting SD card...");
        if (!sdInitDirect(false)) {
            Serial.println("ERROR: Failed to mount SD card!");
            return false;
        }
    }
    
    // Load media mappings if not already loaded
    if (!g_media_mappings_loaded) {
        loadMediaMappingsFromSD(false);  // Don't auto-publish in command handlers
    }
    
    // Build response message
    String responseBody = "";
    
    if (!g_media_mappings_loaded || g_media_mappings.size() == 0) {
        responseBody = "No media.txt mappings found";
        Serial.println("\n=== Media Files ===");
        Serial.println(responseBody);
        Serial.println("=============================\n");
    } else {
        Serial.println("\n=== Media Files (from media.txt) ===");
        size_t mediaCount = g_media_mappings.size();
        
        // Build response body with media list
        for (size_t i = 0; i < mediaCount; i++) {
            const MediaMapping& mapping = g_media_mappings[i];
            String line = String("[") + String(i + 1) + "] " + mapping.imageName;
            if (mapping.audioFile.length() > 0) {
                line += " -> " + mapping.audioFile;
            } else {
                line += " -> (no audio, will use beep.wav)";
            }
            
            Serial.printf("  [%zu] %s", i + 1, mapping.imageName.c_str());
            if (mapping.audioFile.length() > 0) {
                Serial.printf(" -> %s", mapping.audioFile.c_str());
            } else {
                Serial.print(" -> (no audio, will use beep.wav)");
            }
            Serial.println();
            
            // Add to response body (with newline)
            if (responseBody.length() > 0) {
                responseBody += "\n";
            }
            responseBody += line;
        }
        
        responseBody += "\n\nTotal: " + String(mediaCount) + " media file(s)";
        Serial.printf("\nTotal: %zu media file(s)\n", mediaCount);
        Serial.println("=============================\n");
    }
    
    // Publish response to MQTT outbox (similar to !ping)
    String senderNumber = extractFromFieldFromMessage(originalMessage);
    if (senderNumber.length() == 0) {
        Serial.println("WARNING: Could not extract sender number from message, cannot send MQTT response");
        return true;  // Still succeeded in listing, just couldn't send response
    }
    
    // Reconnect to MQTT to publish response
    // (We disconnected after checking for messages, so need to reconnect)
    if (!mqttConnect()) {
        Serial.println("ERROR: Failed to connect to MQTT for list response");
        return true;  // Still succeeded in listing, just couldn't send response
    }
    
    // Wait for connection to be established
    delay(1000);
    
    // Build URL-encoded form response: "To=+447816969344&From=+447401492609&Body=..."
    // Use String with reserve() to avoid multiple reallocations during URL encoding
    // Worst case: each char becomes 3 chars (%XX), so reserve 3x body length + fixed overhead
    String formResponse;
    formResponse.reserve(3 + senderNumber.length() + 20 + 6 + responseBody.length() * 3);
    formResponse = "To=";
    formResponse += senderNumber;
    formResponse += "&From=+447401492609";
    formResponse += "&Body=";
    // URL-encode the body (simple encoding - replace spaces with +, newlines with %0A)
    for (size_t i = 0; i < responseBody.length(); i++) {
        char c = responseBody.charAt(i);
        if (c == ' ') {
            formResponse += '+';
        } else if (c == '\n') {
            formResponse += "%0A";
        } else if (c == '&' || c == '=') {
            // URL-encode special characters
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02X", (unsigned char)c);
            formResponse += hex;
        } else {
            formResponse += c;
        }
    }
    
    // Publish list response
    esp_mqtt_client_handle_t client = getMqttClient();
    const char* topic = getMqttTopicPublish();
    if (client != nullptr && topic != nullptr && strlen(topic) > 0) {
        int msg_id = esp_mqtt_client_publish(client, topic, formResponse.c_str(), formResponse.length(), 1, 0);
        if (msg_id > 0) {
            Serial.printf("Published list response to %s (msg_id: %d)\n", topic, msg_id);
            // Give it a moment to send
            delay(500);
        } else {
            Serial.println("ERROR: Failed to publish list response");
        }
    } else {
        Serial.println("ERROR: MQTT client not available or publish topic not set");
    }
    
    // Disconnect after publishing
    mqttDisconnect();
    delay(200);
    
    return true;
}

/**
 * Remove a phone number from the allowed list in NVS
 */
bool removeAllowedNumber(const String& number) {
    if (!numbersPrefs.begin("numbers", false)) {  // Read-write
        Serial.println("ERROR: Failed to open NVS for removing numbers");
        return false;
    }
    
    int count = numbersPrefs.getInt("count", 0);
    if (count == 0) {
        Serial.println("No numbers in NVS to remove");
        numbersPrefs.end();
        return false;
    }
    
    // Find the number and its index
    int foundIndex = -1;
    for (int i = 0; i < count && i < 100; i++) {
        char key[16];
        snprintf(key, sizeof(key), "num%d", i);
        String storedNumber = numbersPrefs.getString(key, "");
        
        if (storedNumber == number) {
            foundIndex = i;
            break;
        }
    }
    
    if (foundIndex == -1) {
        Serial.printf("Number %s not found in allowed list\n", number.c_str());
        numbersPrefs.end();
        return false;
    }
    
    // Shift all numbers after the found one forward by one position
    for (int i = foundIndex; i < count - 1; i++) {
        char keyFrom[16], keyTo[16];
        snprintf(keyFrom, sizeof(keyFrom), "num%d", i + 1);
        snprintf(keyTo, sizeof(keyTo), "num%d", i);
        
        String nextNumber = numbersPrefs.getString(keyFrom, "");
        numbersPrefs.putString(keyTo, nextNumber);
    }
    
    // Clear the last entry
    char lastKey[16];
    snprintf(lastKey, sizeof(lastKey), "num%d", count - 1);
    numbersPrefs.remove(lastKey);
    
    // Update count
    count--;
    numbersPrefs.putInt("count", count);
    
    numbersPrefs.end();
    
    Serial.printf("Removed number %s from allowed list (remaining: %d)\n", number.c_str(), count);
    return true;
}

/**
 * Handle !show command - display a specific image file from SD card
 * Format: !show <filename>
 * Example: !show image.png
 * Displays the image directly without going through media.txt
 */
bool handleShowCommand(const String& parameter) {
    Serial.println("Processing !show command...");
    
    if (parameter.length() == 0) {
        Serial.println("ERROR: !show command requires filename parameter (e.g., !show image.png)");
        return false;
    }
    
    String filename = parameter;
    filename.trim();
    
    // Ensure display is initialized
    if (display.getBuffer() == nullptr) {
        Serial.println("Display not initialized - initializing now...");
        displaySPI.begin(PIN_SPI_SCK, -1, PIN_SPI_MOSI, -1);
        
        if (!display.begin(PIN_CS0, PIN_CS1, PIN_DC, PIN_RESET, PIN_BUSY)) {
            Serial.println("ERROR: Display initialization failed!");
            return false;
        }
        Serial.println("Display initialized");
    }
    

    // Mount SD card if needed
    if (!sdCardMounted) {
        Serial.println("Mounting SD card...");
        if (!sdInitDirect(false)) {
            Serial.println("ERROR: Failed to mount SD card!");
            return false;
        }
    }
    
    // Build full path (assume root directory if no path specified)
    String imagePath = filename;
    if (!imagePath.startsWith("/")) {
        imagePath = "/" + imagePath;
    }
    
    String fatfsPath = "0:" + imagePath;
    
    Serial.printf("Loading image: %s\n", fatfsPath.c_str());
    
    // Check if file exists
    FILINFO fno;
    FRESULT res = f_stat(fatfsPath.c_str(), &fno);
    if (res != FR_OK) {
        Serial.printf("ERROR: File not found: %s (error: %d)\n", fatfsPath.c_str(), res);
        return false;
    }
    
    size_t fileSize = fno.fsize;
    Serial.printf("File size: %u bytes\n", (unsigned)fileSize);
    
    // Open and read file
    FIL pngFile;
    res = f_open(&pngFile, fatfsPath.c_str(), FA_READ);
    if (res != FR_OK) {
        Serial.printf("ERROR: Failed to open file: %s (error: %d)\n", fatfsPath.c_str(), res);
        return false;
    }
    
    // Allocate memory for PNG data
    uint8_t* pngData = (uint8_t*)hal_psram_malloc(fileSize);
    if (!pngData) {
        Serial.println("ERROR: Failed to allocate PSRAM buffer for PNG!");
        f_close(&pngFile);
        return false;
    }
    
    // Read file
    UINT bytesRead = 0;
    res = f_read(&pngFile, pngData, fileSize, &bytesRead);
    f_close(&pngFile);
    
    if (res != FR_OK) {
        Serial.printf("ERROR: Failed to read file: %d\n", res);
        hal_psram_free(pngData);
        return false;
    }
    
    if (bytesRead != fileSize) {
        Serial.printf("WARNING: Only read %u/%u bytes\n", (unsigned)bytesRead, (unsigned)fileSize);
    }
    
    // Clear display and draw PNG
    Serial.println("Drawing image to display...");
    display.clear(EL133UF1_WHITE);
    PNGResult pres = pngLoader.drawFullscreen(pngData, fileSize);
    hal_psram_free(pngData);
    
    if (pres != PNG_OK) {
        Serial.printf("ERROR: PNG draw failed: %s\n", pngLoader.getErrorString(pres));
        return false;
    }
    
    // Add text overlay (time/date/weather/quote) - centralized function
    // This ensures !show commands also get live weather data, same as !go
    EL133UF1_TTF ttf;
    if (!ttf.begin(&display)) {
        Serial.println("WARNING: TTF initialization failed, skipping text overlay");
    } else {
        addTextOverlayToDisplay(&display, &ttf, 100);
    }
    
    // Update display
    Serial.println("Updating display (e-ink refresh - this will take 20-30 seconds)...");
    display.update();
    display.waitForUpdate();  // Wait for actual refresh to complete
    Serial.println("Display updated");
    
    Serial.println("!show command completed successfully");
    return true;
}


// Forward declarations
void wifiClearCredentials();

// Enter interactive configuration mode - loops until credentials are successfully set
// WiFi functions are now in wifi_manager.cpp - only forward declarations remain here

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
}


void sdUnmountDirect() {
    // SD card unmounting is DISABLED - SD card should never be unmounted
    Serial.println("SD card unmount DISABLED - SD card will remain mounted");
    // Do nothing - keep SD card mounted
}

// ============================================================================
// Logging System - writes to both Serial and SD card
// ============================================================================

bool logInit() {
    // Ensure SD card is mounted
    if (!sdCardMounted) {
        if (!sdInitDirect(false)) {
            return false;  // SD mount failed
        }
    }
    
    // Close existing log file if open
    if (logFileOpen) {
        f_close(&logFile);
        logFileOpen = false;
    }
    
    // Create .logs directory if it doesn't exist
    FILINFO fno;
    FRESULT dirRes = f_stat(LOG_DIR, &fno);
    if (dirRes != FR_OK) {
        Serial.printf("Creating log directory: %s\n", LOG_DIR);
        dirRes = f_mkdir(LOG_DIR);
        if (dirRes != FR_OK && dirRes != FR_EXIST) {
            Serial.printf("WARNING: Failed to create log directory %s: %d\n", LOG_DIR, dirRes);
        }
    }
    
    // Open log file in append mode
    FRESULT res = f_open(&logFile, LOG_FILE, FA_WRITE | FA_OPEN_APPEND);
    if (res != FR_OK) {
        // Try creating new file
        res = f_open(&logFile, LOG_FILE, FA_WRITE | FA_CREATE_ALWAYS);
        if (res != FR_OK) {
            Serial.printf("ERROR: Cannot open log file: %d\n", res);
            return false;
        }
    }
    
    logFileOpen = true;
    return true;
}

void logRotate() {
    // Close current log file
    if (logFileOpen) {
        f_close(&logFile);
        logFileOpen = false;
    }
    
    // Delete old archive if it exists (using previously stored archive name)
    f_unlink(LOG_ARCHIVE);
    
    // Generate timestamp-based archive filename using current RTC time
    time_t now = time(nullptr);
    if (now > 1577836800) {  // Valid time (after 2020-01-01)
        struct tm tm_utc;
        gmtime_r(&now, &tm_utc);
        snprintf(LOG_ARCHIVE, sizeof(LOG_ARCHIVE), "0:/.logs/log_%04d%02d%02d_%02d%02d%02d.txt",
                tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
                tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec);
    } else {
        // Fallback to default name if time is invalid
        strncpy(LOG_ARCHIVE, "0:/.logs/log_prev.txt", sizeof(LOG_ARCHIVE) - 1);
        LOG_ARCHIVE[sizeof(LOG_ARCHIVE) - 1] = '\0';
    }
    
    // Rename current log to timestamped archive
    f_rename(LOG_FILE, LOG_ARCHIVE);
    
    // Create new log file
    logInit();
    
    Serial.printf("Log rotated: old log archived to %s\n", LOG_ARCHIVE);
}

void logPrint(const char* str) {
    // Always print to real Serial (not LogSerialInstance to avoid double-write to file)
    RealSerial->print(str);
    
    // Write to log file if open
    if (logFileOpen) {
        UINT bw;
        f_write(&logFile, str, strlen(str), &bw);
        // Don't flush on every write for performance - caller can call logFlush() when needed
    }
}

void logPrintf(const char* format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    if (len > 0 && len < (int)sizeof(buffer)) {
        logPrint(buffer);
    } else {
        // Buffer too small - write directly to real Serial using vprintf
        va_start(args, format);
        // Print to real Serial using vprintf (formatted output)
        char largeBuffer[1024];
        int largeLen = vsnprintf(largeBuffer, sizeof(largeBuffer), format, args);
        if (largeLen > 0 && largeLen < (int)sizeof(largeBuffer)) {
            RealSerial->print(largeBuffer);
        } else {
            // Still too large - truncate
            RealSerial->print(buffer);  // At least print the truncated version
        }
        va_end(args);
    }
}

void logFlush() {
    if (logFileOpen) {
        f_sync(&logFile);
    }
}

void logClose() {
    if (logFileOpen) {
        // Flush any pending writes
        logFlush();
        // Additional sync to ensure all writes are complete
        f_sync(&logFile);
        delay(100);  // Allow file system to complete any pending operations
        f_close(&logFile);
        logFileOpen = false;
        delay(100);  // Allow file system cleanup and background tasks to complete
    }
}

// Enable LDO_VO4 for external pull-up resistors (5.1K to LDO_VO4)
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

// Enable SD card power via GPIO45 -> MOSFET Q1
// GPIO45 LOW = MOSFET ON = SD card VDD powered from ESP_3V3
void sdPowerOn() {
    gpio_set_direction((gpio_num_t)PIN_SD_POWER, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)PIN_SD_POWER, 0);  // LOW = MOSFET ON
    Serial.println("SD card power enabled (GPIO45 LOW)");
}

// Disable SD card power via GPIO45 -> MOSFET Q1
// GPIO45 HIGH = MOSFET OFF = SD card unpowered
void sdPowerOff() {
    gpio_set_level((gpio_num_t)PIN_SD_POWER, 1);  // HIGH = MOSFET OFF
    Serial.println("SD card power disabled (GPIO45 HIGH)");
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

// Initialize SD card using ESP-IDF direct method (for FatFs compatibility)
// This is a wrapper that uses the Arduino SD_MMC interface
bool sdInitDirect(bool mode1bit) {
    return sdInit(mode1bit);
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
    // SD card unmounting is DISABLED - SD card should never be unmounted
    Serial.println("SD card unmount DISABLED - SD card will remain mounted");
    // Do nothing - keep SD card mounted
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
    
    if (!sdCardMounted) {
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
    display.update();  // Uses mutex to ensure _sendBuffer() completes before returning
    uint32_t refreshTime = millis() - refreshStart;
    
    Serial.printf("Display refresh: %lu ms\n", refreshTime);
    
    // Save framebuffer to storage partition after display update
    // safeDisplayUpdate() ensures _sendBuffer() has completed, so the buffer is safe to read
    
    Serial.printf("Total time: %lu ms (%.1f s)\n", 
                  millis() - totalStart, (millis() - totalStart) / 1000.0);
    Serial.println("Done!");
}

// List all BMP files on SD card using FatFs native functions
void bmpListFiles(const char* dirname = "/") {
    Serial.println("\n=== BMP Files on SD Card (FatFs) ===");
    
    if (!sdCardMounted) {
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
// Excludes /ai_generated directory to prevent mixing AI-generated images with media.txt images
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
        
        // Skip directories (including ai_generated)
        if (fno.fattrib & AM_DIR) {
            // When scanning root directory, explicitly skip ai_generated directory
            if (strcmp(dirname, "/") == 0 && strcmp(fno.fname, "ai_generated") == 0) {
                continue;
            }
            continue;
        }

        String name = String(fno.fname);
        String lower = name;
        lower.toLowerCase();
        if (lower.endsWith(".png")) {
            // When scanning root directory, skip any files that might be in ai_generated subdirectory
            // (though they shouldn't appear here since we skip directories, but be safe)
            if (strcmp(dirname, "/") == 0 && name.startsWith("oai_")) {
                continue;  // Skip AI-generated files if they somehow appear in root
            }
            
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


// Load a random PNG from SD card and display it (timed)
void pngLoadRandom(const char* dirname = "/") {
    Serial.println("\n=== Loading Random PNG ===");
    uint32_t totalStart = millis();

    if (!sdCardMounted) {
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
    

    Serial.println("Updating display (20-30s for e-ink refresh)...");
    uint32_t refreshStart = millis();
    display.update();
    uint32_t refreshTime = millis() - refreshStart;
    Serial.printf("Display refresh: %lu ms\n", refreshTime);

    // Save framebuffer to storage partition after display update

    Serial.printf("Total time: %lu ms (%.1f s)\n",
                  millis() - totalStart, (millis() - totalStart) / 1000.0);
    Serial.println("Done!");
}

// List all PNG files on SD card using FatFs native functions
void pngListFiles(const char* dirname = "/") {
    Serial.println("\n=== PNG Files on SD Card (FatFs) ===");

    if (!sdCardMounted) {
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
    
    // Safety check: if index is out of bounds (e.g., media.txt was reloaded with fewer items), reset to 0
    if (lastMediaIndex >= mediaCount) {
        Serial.printf("WARNING: lastMediaIndex %lu is out of bounds (max %zu), resetting to 0\n",
                     (unsigned long)lastMediaIndex, mediaCount);
        lastMediaIndex = 0;
        mediaIndexSaveToNVS();
    }
    
    // Increment and wrap around
    lastMediaIndex = (lastMediaIndex + 1) % mediaCount;
    mediaIndexSaveToNVS();
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
    
    
    return true;
}

// Draw a random PNG into the display buffer (no display.update), return timing info.
bool pngDrawRandomToBuffer(const char* dirname, uint32_t* out_sd_read_ms, uint32_t* out_decode_ms) {
    if (out_sd_read_ms) *out_sd_read_ms = 0;
    if (out_decode_ms) *out_decode_ms = 0;

    if (!sdCardMounted) {
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
    
    
    return true;
}



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

// ============================================================================
// Setup and Loop
// ============================================================================

void setup() {
    // IMMEDIATELY pull C6_ENABLE (GPIO54) HIGH on wake-up (it was LOW during deep sleep)
    // This MUST be the very first thing - even before Serial.begin() or any other initialization
    // First disable pad hold to allow pin state changes
    gpio_hold_dis((gpio_num_t)C6_ENABLE);
    pinMode(C6_ENABLE, OUTPUT);
    digitalWrite(C6_ENABLE, HIGH);
    
    // Check wake cause IMMEDIATELY (before any other initialization)
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
    
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = "littlefs",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);

    if (ret != ESP_OK) {
        Serial.printf("Failed to initialize LittleFS partition (%s)\n", esp_err_to_name(ret));
    }
    
    // Print chip information at boot
    // Check if we woke from deep sleep (non-switch-D wake) - set global flag early
    g_is_cold_boot = (wakeCause == ESP_SLEEP_WAKEUP_UNDEFINED);  // Set global flag early for use in print statements
    
    // Scan for fonts only at cold boot (not after deep sleep wake)
    if (g_is_cold_boot) {
        // Initialize font list with built-in OpenSans first
        g_rtcFontCount = 0;
        memset(g_rtcFontList, 0, sizeof(g_rtcFontList));
        
        // Add built-in OpenSans font
        if (g_rtcFontCount < MAX_FONTS_IN_RTC) {
            strncpy(g_rtcFontList[g_rtcFontCount].name, "Open Sans", MAX_FONT_NAME_LEN);
            g_rtcFontList[g_rtcFontCount].name[MAX_FONT_NAME_LEN] = '\0';
            strncpy(g_rtcFontList[g_rtcFontCount].filename, "OpenSans", MAX_FONT_FILENAME_LEN);
            g_rtcFontList[g_rtcFontCount].filename[MAX_FONT_FILENAME_LEN] = '\0';
            g_rtcFontList[g_rtcFontCount].isBuiltin = true;
            g_rtcFontCount++;
            Serial.println("Fonts found on LittleFS:");
            Serial.printf("  OpenSans: Open Sans (Built-in)\n");
        }
        
        // Scan LittleFS for additional fonts
        if (ret == ESP_OK) {
            DIR* dir = opendir("/littlefs");
            if (dir != nullptr) {
                struct dirent* entry;
                std::vector<String> fontFiles;
                
                // Collect all .ttf and .otf files
                while ((entry = readdir(dir)) != nullptr) {
                    String filename = String(entry->d_name);
                    filename.toLowerCase();
                    if (filename.endsWith(".ttf") || filename.endsWith(".otf")) {
                        fontFiles.push_back(String(entry->d_name));
                    }
                }
                closedir(dir);
                
                // Process each font file
                for (const String& fontFilename : fontFiles) {
                    if (g_rtcFontCount >= MAX_FONTS_IN_RTC) {
                        Serial.printf("  WARNING: Font limit reached (%d), skipping %s\n", MAX_FONTS_IN_RTC, fontFilename.c_str());
                        continue;
                    }
                    
                    char fullPath[128];
                    snprintf(fullPath, sizeof(fullPath), "/littlefs/%s", fontFilename.c_str());
                    
                    // Open and read font file
                    FILE* fontFile = fopen(fullPath, "rb");
                    if (fontFile == nullptr) {
                        Serial.printf("  %s: Failed to open\n", fontFilename.c_str());
                        continue;
                    }
                    
                    // Get file size
                    fseek(fontFile, 0, SEEK_END);
                    long fileSize = ftell(fontFile);
                    fseek(fontFile, 0, SEEK_SET);
                    
                    if (fileSize <= 0 || fileSize > 10 * 1024 * 1024) {  // Max 10MB font file
                        fclose(fontFile);
                        Serial.printf("  %s: Invalid size (%ld bytes)\n", fontFilename.c_str(), fileSize);
                        continue;
                    }
                    
                    // Allocate buffer and read font data
                    uint8_t* fontData = (uint8_t*)malloc(fileSize);
                    if (fontData == nullptr) {
                        fclose(fontFile);
                        Serial.printf("  %s: Failed to allocate memory\n", fontFilename.c_str());
                        continue;
                    }
                    
                    size_t bytesRead = fread(fontData, 1, fileSize, fontFile);
                    fclose(fontFile);
                    
                    if (bytesRead != (size_t)fileSize) {
                        free(fontData);
                        Serial.printf("  %s: Failed to read file\n", fontFilename.c_str());
                        continue;
                    }
                    
                    // Use TTF library to validate font and get font name
                    EL133UF1_TTF tempTTF;
                    if (!tempTTF.loadFont(fontData, fileSize)) {
                        free(fontData);
                        Serial.printf("  %s: Invalid font file\n", fontFilename.c_str());
                        continue;
                    }
                    
                    // Get font name using the TTF library method
                    char fontNameBuf[256];
                    bool gotName = tempTTF.getFontName(fontNameBuf, sizeof(fontNameBuf));
                    
                    // Store in RTC memory
                    if (gotName) {
                        strncpy(g_rtcFontList[g_rtcFontCount].name, fontNameBuf, MAX_FONT_NAME_LEN);
                        g_rtcFontList[g_rtcFontCount].name[MAX_FONT_NAME_LEN] = '\0';
                        Serial.printf("  %s: %s\n", fontFilename.c_str(), fontNameBuf);
                    } else {
                        // Use filename as name if we couldn't extract the font name
                        strncpy(g_rtcFontList[g_rtcFontCount].name, fontFilename.c_str(), MAX_FONT_NAME_LEN);
                        g_rtcFontList[g_rtcFontCount].name[MAX_FONT_NAME_LEN] = '\0';
                        Serial.printf("  %s\n", fontFilename.c_str());
                    }
                    
                    strncpy(g_rtcFontList[g_rtcFontCount].filename, fontFilename.c_str(), MAX_FONT_FILENAME_LEN);
                    g_rtcFontList[g_rtcFontCount].filename[MAX_FONT_FILENAME_LEN] = '\0';
                    g_rtcFontList[g_rtcFontCount].isBuiltin = false;
                    g_rtcFontCount++;
                    
                    free(fontData);
                }
            }
        }
    } else {
        // Warm boot from deep sleep - font list already in RTC, just log it
        Serial.println("Fonts available (from RTC memory):");
        for (uint8_t i = 0; i < g_rtcFontCount; i++) {
            Serial.printf("  %s: %s%s\n", 
                         g_rtcFontList[i].filename, 
                         g_rtcFontList[i].name,
                         g_rtcFontList[i].isBuiltin ? " (Built-in)" : "");
        }
    }
    
    // Only print chip info on cold boot
    if (g_is_cold_boot) {
        esp_chip_info_t chip_info;
        esp_chip_info(&chip_info);
        Serial.println("\n=== Chip Information ===");
        Serial.printf("  Model: ESP32-P4\n");
        Serial.printf("  Cores: %d\n", chip_info.cores);
        Serial.printf("  Revision: r%d.%d\n", chip_info.revision / 100, chip_info.revision % 100);
        Serial.printf("  Features: %s%s%s%s\n",
                      (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "Embedded-Flash " : "",
                      (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi " : "",
                      (chip_info.features & CHIP_FEATURE_BT) ? "BT " : "",
                      (chip_info.features & CHIP_FEATURE_BLE) ? "BLE " : "");
        uint32_t flash_size = 0;
        esp_flash_get_size(NULL, &flash_size);
        Serial.printf("  Flash: %dMB %s\n", flash_size / (1024 * 1024),
                      (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
        Serial.println("=======================\n");
    }


    // Mount SD card as early as possible for logging
    // SD card is now always mounted - it's required for logging and all operations
    if (!sdCardMounted) {
        if (sdInitDirect(false)) {
            // Initialize logging system
            logInit();
            logPrintf("\n=== Boot: %lu ms ===\n", (unsigned long)millis());
            logPrintf("SD card mounted successfully\n");
        } else {
            Serial.println("WARNING: SD card mount failed - logging to SD disabled");
            // Continue anyway - some operations may fail but device can still function
        }
    } else if (sdCardMounted) {
        // SD already mounted, just initialize logging
        logInit();
        logPrintf("\n=== Boot: %lu ms ===\n", (unsigned long)millis());
        logPrintf("SD card already mounted\n");
    }
    
    // Initialize storage partition for framebuffer persistence
    // Storage initialization removed - not available in this branch

    // Load volume setting from NVS early (before any audio initialization)
    volumeLoadFromNVS();
    
    // Load allowed phone numbers from NVS
    numbersLoadFromNVS();
    
    // Load sleep duration interval from NVS
    sleepDurationLoadFromNVS();
    
    // Load hour schedule from NVS
    hourScheduleLoadFromNVS();
    
    // Load media index from NVS
    mediaIndexLoadFromNVS();
    
    // Initialize text placement mutex (protects textPlacement analyzer from concurrent access)
    // Text placement mutex removed - not available in this branch
    
    // Initialize Core 1 worker task for thumbnail generation and MQTT message building
    initMqttWorkerTask();
    
    // Check and require web UI password setup (for GitHub Pages UI)
    requireWebUIPasswordSetup();

    // Bring up PA enable early (matches known-good ESP-IDF example behavior)
    pinMode(PIN_CODEC_PA_EN, OUTPUT);
    digitalWrite(PIN_CODEC_PA_EN, HIGH);

    pinMode(PIN_USER_LED, OUTPUT);
    digitalWrite(PIN_USER_LED, LOW);
    
    // C6_ENABLE was already set HIGH at the very start of setup() - no need to do it again here
    Serial.println("C6_ENABLE already set HIGH at boot start - will remain HIGH during normal operation");
    
    // Check if we woke from deep sleep (non-switch-D wake)
    bool wokeFromSleep = (wakeCause != ESP_SLEEP_WAKEUP_UNDEFINED);
    g_is_cold_boot = !wokeFromSleep;  // Set global flag for auto_cycle_task to use
    
    // Start serial monitor task for continuous OTA monitoring (runs on Core 1)
    // This task monitors serial input and sets g_ota_requested flag when 'o' is pressed
    if (g_serial_monitor_task == nullptr) {
        xTaskCreatePinnedToCore(serial_monitor_task, "serial_mon", 4096, nullptr, 1, &g_serial_monitor_task, 1);
        delay(100);  // Allow task to start
    }
    
    if (wokeFromSleep) {
        // Quick boot after deep sleep - skip serial wait
        // Add extra delay after deep sleep to let ESP-Hosted (ESP32-C6) stabilize
        delay(500);  // Increased delay to let ESP-Hosted module stabilize after deep sleep
        Serial.println("\n=== Woke from deep sleep ===");
        Serial.printf("Boot count: %u, Wake cause: %d\n", sleepBootCount, wakeCause);
        
        // Check if OTA was requested (via serial monitor task)
        checkAndStartOTA();
    } else {
        // Cold boot - wait for serial
        uint32_t start = millis();
        while (!Serial && (millis() - start < 3000)) {
            delay(100);
        }
        Serial.println("\n\n========================================");
        Serial.println("EL133UF1 ESP32-P4 Port Test");
        Serial.println("========================================\n");
        
        // Check if OTA was requested (via serial monitor task)
        checkAndStartOTA();
    }
    
    // Print platform info and pin configuration only on cold boot
    if (g_is_cold_boot) {
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
    }
    
    // Check PSRAM (always check, but only print on cold boot)
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
    
    if (g_is_cold_boot) {
        Serial.printf("PSRAM OK: %lu KB available\n", (unsigned long)(hal_psram_get_size() / 1024));
    }
    
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
        if (!ttf.loadFont(opensans_ttf, opensans_ttf_len)) {
            if (g_is_cold_boot) {
                Serial.println("WARNING: Failed to load TTF font");
            }
        } else if (g_is_cold_boot) {
            Serial.println("TTF: Font loaded successfully");
        }
    }

    // Auto cycle: random PNG + time/date overlay + beep + deep sleep
    if (kAutoCycleEnabled) {
        // Check if OTA was requested before starting auto-cycle
        checkAndStartOTA();
        
        // Run auto-cycle in a dedicated task with a larger stack than Arduino loopTask,
        // since SD init and PNG decoding are stack-heavy.
        // Pin to Core 1 to avoid blocking Core 0's IDLE task (which the watchdog monitors)
        xTaskCreatePinnedToCore(auto_cycle_task, "auto_cycle", 16384, nullptr, 3, &g_auto_cycle_task, 1);
        return; // yield loopTask; auto_cycle_task will deep-sleep the device
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

    Serial.println("  WiFi:    'w'=connect, 'W'=set credentials, 'q'=scan, 'd'=disconnect, 'n'=NTP sync, 'x'=status");
    Serial.println("  MQTT:    'J'=set config, 'K'=status, 'H'=connect, 'j'=disconnect");

    Serial.println("  SD Card: 'M'=mount(4-bit), 'm'=mount(1-bit), 'L'=list, 'I'=info, 'T'=test, 'U'=unmount, 'D'=diag, 'P'=power cycle, 'O/o'=pwr on/off");
    Serial.println("  BMP:     'B'=load random BMP, 'b'=list BMP files");
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

        wifiLoadCredentials();  // Still load credentials for later use
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
                
                // Check for OTA update and notify if new firmware booted
                // Do this after WiFi is connected so MQTT can work
                Serial.println("\n=== Checking for OTA firmware update ===");
                checkAndNotifyOTAUpdate();
                Serial.println("=== OTA check complete ===\n");
                
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
                }
            }
        }
    }
    }

}

// ============================================================================
// Web UI Authentication Functions
// ============================================================================
// Encryption functions moved to webui_crypto.cpp/h module (Priority 1 refactoring)
// All encryption/decryption/HMAC functions are now in src/webui_crypto.cpp
// ============================================================================

void loop() {
    // Main loop - handled by FreeRTOS tasks
    // OTA server runs in a dedicated task with sufficient stack
}

/**
 * @file display_manager.cpp
 * @brief Display manager implementation for unified media display with text overlay
 */

#include "display_manager.h"
#include "EL133UF1.h"
#include "EL133UF1_TTF.h"
#include "text_elements.h"
#include "wifi_manager.h"  // For wifiConnectPersistent (NOT wifi_guard.h - it disconnects WiFi!)
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "cJSON.h"
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <SPI.h>

// External references to globals and functions from main.cpp
extern SPIClass displaySPI;
extern EL133UF1 display;
extern EL133UF1_TTF ttf;
extern bool sdCardMounted;
extern uint32_t lastMediaIndex;
extern String g_lastImagePath;
extern char lastAudioFile[64];

// Quote loading
struct LoadedQuote {
    String text;
    String author;
};
extern std::vector<LoadedQuote> g_loaded_quotes;
extern bool g_quotes_loaded;

// Media mappings
struct MediaMapping {
    String imageName;
    String audioFile;
};
extern std::vector<MediaMapping> g_media_mappings;
extern bool g_media_mappings_loaded;

// GPIO pin definitions
#define PIN_SPI_SCK   3
#define PIN_SPI_MOSI  2
#define PIN_CS0       23
#define PIN_CS1       48
#define PIN_DC        26
#define PIN_RESET     22
#define PIN_BUSY      47

// External function declarations
extern bool sdInitDirect(bool mode1bit);
extern void loadQuotesFromSD();
extern void loadMediaMappingsFromSD(bool autoPublish);
extern bool pngDrawFromMediaMappings(uint32_t* out_sd_read_ms, uint32_t* out_decode_ms);
extern void mediaIndexSaveToNVS();
extern String getAudioForImage(const String& imagePath);
extern bool playWavFile(const String& audioPath);
extern void audio_stop();
extern bool wifiLoadCredentials();

// Forward declarations for internal helper functions
static bool fetchWeatherData(float lat, float lon, char* tempStr, size_t tempStrSize,
                             char* conditionStr, size_t conditionStrSize);
static bool formatTimeAndDate(char* timeBuf, size_t timeBufSize, 
                              char* dayBuf, size_t dayBufSize,
                              char* dateBuf, size_t dateBufSize);
static void placeTimeDateAndQuote(EL133UF1* display, EL133UF1_TTF* ttf,
                                  const char* timeBuf, const char* dayBuf, const char* dateBuf,
                                  int16_t keepoutMargin);

/**
 * Fetch weather data from OpenWeatherMap API
 */
static bool fetchWeatherData(float lat, float lon, char* tempStr, size_t tempStrSize,
                             char* conditionStr, size_t conditionStrSize) {
    const char* apiKey = "4efd38c9e9d41e3b10724fe764541d7b";  // TODO: Replace with actual API key or load from NVS
    
    Serial.printf("Weather API: Attempting to fetch weather data (lat=%.4f, lon=%.4f)\n", lat, lon);
    
    // Yield to watchdog before starting HTTP request
    vTaskDelay(1);
    
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();  // Skip certificate verification for OpenWeatherMap API
    client.setTimeout(5000);  // 5 second timeout (reduced to avoid watchdog)
    
    // Build API URL
    char url[256];
    snprintf(url, sizeof(url), 
             "https://api.openweathermap.org/data/2.5/weather?lat=%.4f&lon=%.4f&units=metric&appid=%s",
             lat, lon, apiKey);
    
    Serial.printf("Weather API: URL: %s\n", url);
    
    http.begin(client, url);
    http.setTimeout(8000);  // 8 second timeout (increased from 5s)
    
    // Yield to watchdog before blocking HTTP call
    vTaskDelay(1);
    
    int httpCode = http.GET();
    
    // Yield immediately after HTTP call to reset watchdog
    vTaskDelay(1);
    
    Serial.printf("Weather API: HTTP response code %d\n", httpCode);
    
    if (httpCode == HTTP_CODE_OK) {
        // Yield before reading response (may be large)
        vTaskDelay(1);
        
        String payload = http.getString();
        Serial.printf("Weather API: Received payload (%d bytes): %s\n", payload.length(), payload.c_str());
        
        // Yield before JSON parsing (can be CPU intensive)
        vTaskDelay(1);
        
        // Parse JSON response using cJSON
        cJSON* json = cJSON_Parse(payload.c_str());
        if (json) {
            // Extract temperature
            cJSON* main = cJSON_GetObjectItem(json, "main");
            if (main) {
                cJSON* temp = cJSON_GetObjectItem(main, "temp");
                if (temp && cJSON_IsNumber(temp)) {
                    float tempC = (float)temp->valuedouble;
                    snprintf(tempStr, tempStrSize, "%.0f°C", tempC);
                } else {
                    strncpy(tempStr, "N/A", tempStrSize - 1);
                    tempStr[tempStrSize - 1] = '\0';
                }
            } else {
                strncpy(tempStr, "N/A", tempStrSize - 1);
                tempStr[tempStrSize - 1] = '\0';
            }
            
            // Yield before processing weather condition
            vTaskDelay(1);
            
            // Extract weather condition
            cJSON* weather = cJSON_GetObjectItem(json, "weather");
            if (weather && cJSON_IsArray(weather)) {
                cJSON* weatherItem = cJSON_GetArrayItem(weather, 0);
                if (weatherItem) {
                    cJSON* description = cJSON_GetObjectItem(weatherItem, "description");
                    if (description && cJSON_IsString(description)) {
                        // Capitalize first letter
                        const char* desc = description->valuestring;
                        strncpy(conditionStr, desc, conditionStrSize - 1);
                        conditionStr[conditionStrSize - 1] = '\0';
                        if (conditionStr[0] >= 'a' && conditionStr[0] <= 'z') {
                            conditionStr[0] = conditionStr[0] - 'a' + 'A';
                        }
                    } else {
                        strncpy(conditionStr, "Unknown", conditionStrSize - 1);
                        conditionStr[conditionStrSize - 1] = '\0';
                    }
                } else {
                    strncpy(conditionStr, "Unknown", conditionStrSize - 1);
                    conditionStr[conditionStrSize - 1] = '\0';
                }
            } else {
                strncpy(conditionStr, "Unknown", conditionStrSize - 1);
                conditionStr[conditionStrSize - 1] = '\0';
            }
            
            cJSON_Delete(json);
            http.end();
            
            // Final yield before returning
            vTaskDelay(1);
            return true;
        } else {
            Serial.printf("Weather API: Failed to parse JSON response. Payload: %s\n", payload.c_str());
            const char* error = cJSON_GetErrorPtr();
            if (error) {
                Serial.printf("Weather API: JSON parse error at: %s\n", error);
            }
        }
    } else {
        Serial.printf("Weather API: HTTP error %d\n", httpCode);
        if (httpCode < 0) {
            Serial.printf("Weather API: HTTPClient error code: %d (negative means connection/network error)\n", httpCode);
        } else {
            // Try to get error payload
            String errorPayload = http.getString();
            Serial.printf("Weather API: Error response (%d bytes): %s\n", errorPayload.length(), errorPayload.c_str());
        }
    }
    
    http.end();
    return false;
}

/**
 * Helper function to format time and date strings
 * Returns true if time is valid, false otherwise
 */
static bool formatTimeAndDate(char* timeBuf, size_t timeBufSize, 
                              char* dayBuf, size_t dayBufSize,
                              char* dateBuf, size_t dateBufSize) {
    time_t now = time(nullptr);
    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);
    
    bool timeValid = (now > 1577836800); // after 2020-01-01
    if (timeValid) {
        strftime(timeBuf, timeBufSize, "%H:%M", &tm_utc);
        
        // Format day name (e.g., "Saturday")
        strftime(dayBuf, dayBufSize, "%A", &tm_utc);
        
        // Format date as "13th of December 2025"
        char monthName[12];
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
        
        snprintf(dateBuf, dateBufSize, "%d%s of %s %d", 
                day, suffix, monthName, year);
    } else {
        snprintf(timeBuf, timeBufSize, "--:--");
        snprintf(dayBuf, dayBufSize, "time not set");
        snprintf(dateBuf, dateBufSize, "");
    }
    
    return timeValid;
}

/**
 * Simple fixed-layout function: divide screen into 3 areas (one half + two quarters)
 * Randomly assign: which half (top/bottom), which quarter gets time/date (left/right)
 */
static void placeTimeDateAndQuote(EL133UF1* display, EL133UF1_TTF* ttf, 
                                  const char* timeBuf, const char* dayBuf, const char* dateBuf,
                                  int16_t keepoutMargin) {
    int16_t screenW = display->width();
    int16_t screenH = display->height();
    
    // Randomly choose: top half or bottom half for the large area (quote)
    bool quoteOnTop = (random(2) == 0);
    
    // Randomly choose: left or right quarter for time/date
    bool timeDateOnLeft = (random(2) == 0);
    
    Serial.printf("[Layout] Quote on %s, Time/Date on %s quarter\n", 
                 quoteOnTop ? "top" : "bottom",
                 timeDateOnLeft ? "left" : "right");
    
    // Calculate area boundaries
    // Screen layout: one half (full width x half height) + two quarters (half width x half height each)
    // For 1600x1200: half = 1600x600, each quarter = 800x600
    int16_t halfH = screenH / 2;
    int16_t quarterW = screenW / 2;  // Half width = quarter of screen area
    int16_t quarterH = screenH / 2;   // Half height = quarter of screen area
    
    // Large area (half screen) - for quote: full width x half height
    int16_t quoteAreaY = quoteOnTop ? 0 : halfH;
    int16_t quoteCenterX = screenW / 2;
    int16_t quoteCenterY = quoteOnTop ? (halfH / 2) : (halfH + halfH / 2);
    
    // Two quarter areas (remaining half split horizontally)
    // Each quarter is: half width x half height (800x600 for 1600x1200)
    int16_t quarterAreaY = quoteOnTop ? halfH : 0;
    int16_t timeDateCenterX = timeDateOnLeft ? (quarterW / 2) : (quarterW + quarterW / 2);
    int16_t weatherCenterX = timeDateOnLeft ? (quarterW + quarterW / 2) : (quarterW / 2);
    int16_t quarterCenterY = quarterAreaY + (quarterH / 2);
    
    // Get quote text
    const char* quoteText = nullptr;
    const char* quoteAuthor = nullptr;
    
    if (g_quotes_loaded && g_loaded_quotes.size() > 0) {
        int randomIndex = random(g_loaded_quotes.size());
        quoteText = g_loaded_quotes[randomIndex].text.c_str();
        quoteAuthor = g_loaded_quotes[randomIndex].author.c_str();
        Serial.printf("Using SD card quote: \"%s\" - %s\n", quoteText, quoteAuthor);
    } else {
        static const struct { const char* text; const char* author; } fallbackQuotes[] = {
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
        int idx = random(numQuotes);
        quoteText = fallbackQuotes[idx].text;
        quoteAuthor = fallbackQuotes[idx].author;
        Serial.printf("Using fallback quote: \"%s\" - %s\n", quoteText, quoteAuthor);
    }
    
    // Fetch real weather data (rotating between three locations)
    static int weatherLocationIndex = 0;
    const char* weatherTemp = "N/A";
    const char* weatherCondition = "Loading...";
    const char* weatherLocation = "Unknown";
    
    struct WeatherLocation {
        const char* name;
        const char* apiName;
        float lat;
        float lon;
    };
    
    static const WeatherLocation locations[] = {
        {"Dunstable, UK", "Dunstable,GB", 51.8858, -0.5229},
        {"Brienz, CH", "Brienz,CH", 46.7542, 8.0383},
        {"Jersey", "Jersey,JE", 49.2144, -2.1312}
    };
    static const int numLocations = sizeof(locations) / sizeof(locations[0]);
    
    const WeatherLocation& loc = locations[weatherLocationIndex];
    weatherLocation = loc.name;
    weatherLocationIndex = (weatherLocationIndex + 1) % numLocations;
    
    char tempStr[16] = "N/A";
    char conditionStr[64] = "N/A";
    
    vTaskDelay(1);
    
    Serial.printf("[Weather] Attempting to fetch weather for %s (lat=%.4f, lon=%.4f)\n", 
                 loc.name, loc.lat, loc.lon);
    
    // Connect WiFi if needed, but DO NOT disconnect it - WiFi must stay connected
    // until deep sleep. NO WiFiGuard - it disconnects WiFi automatically.
    bool wifiConnectedForWeather = false;
    
    if (wifiLoadCredentials()) {
        // Check if WiFi is already connected
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[Weather] WiFi already connected (IP: %s), reusing for weather fetch...\n", WiFi.localIP().toString().c_str());
            wifiConnectedForWeather = true;
        } else {
            // Connect WiFi but DO NOT use WiFiGuard - it will disconnect when it goes out of scope
            Serial.println("[Weather] WiFi credentials loaded, attempting connection...");
            if (wifiConnectPersistent(3, 20000, false)) {
                Serial.printf("[Weather] WiFi connected (IP: %s), fetching weather data...\n", WiFi.localIP().toString().c_str());
                wifiConnectedForWeather = true;
                // WiFi stays connected - NO DISCONNECT
            }
        }
        
        if (wifiConnectedForWeather) {
            vTaskDelay(1);
            
            if (fetchWeatherData(loc.lat, loc.lon, tempStr, sizeof(tempStr), 
                                conditionStr, sizeof(conditionStr))) {
                weatherTemp = tempStr;
                weatherCondition = conditionStr;
                Serial.printf("[Weather] SUCCESS: Fetched weather for %s: %s, %s\n", 
                             loc.name, weatherTemp, weatherCondition);
            } else {
                Serial.printf("[Weather] FAILED: Failed to fetch weather for %s, using fallback\n", loc.name);
                snprintf(tempStr, sizeof(tempStr), "N/A");
                snprintf(conditionStr, sizeof(conditionStr), "No data");
                weatherTemp = tempStr;
                weatherCondition = conditionStr;
            }
        } else {
            Serial.printf("[Weather] WiFi connection failed for %s (status: %d), using fallback\n", 
                         loc.name, WiFi.status());
            weatherTemp = tempStr;
            weatherCondition = conditionStr;
        }
    } else {
        Serial.println("[Weather] WiFi credentials not available, using fallback");
        weatherTemp = tempStr;
        weatherCondition = conditionStr;
    }
    
    // Create elements
    TimeDateElement timeDateElement(ttf, timeBuf, dayBuf, dateBuf);
    WeatherElement weatherElement(ttf, weatherTemp, weatherCondition, weatherLocation);
    QuoteElement quoteElement(ttf, quoteText, quoteAuthor);
    
    // Scale elements to fit their assigned areas
    // Quarter areas: quarterW x quarterH (800x600 for 1600x1200 display)
    // Half area: screenW x halfH (1600x600 for 1600x1200 display)
    int16_t halfW = screenW;
    int16_t halfH_area = halfH;
    
    // Scale time/date to fit quarter (with reduced margins: 25px left/right, 50px top/bottom)
    int16_t timeDateW, timeDateH;
    timeDateElement.getDimensions(timeDateW, timeDateH);
    float timeDateScale = 1.0f;
    if (timeDateW > (quarterW - 50) || timeDateH > (quarterH - 100)) {  // 25px margin each side = 50px total, 50px margin top/bottom = 100px total
        float scaleW = (float)(quarterW - 50) / timeDateW;
        float scaleH = (float)(quarterH - 100) / timeDateH;
        timeDateScale = (scaleW < scaleH) ? scaleW : scaleH;
        if (timeDateScale < 0.5f) timeDateScale = 0.5f;  // Minimum 50% size
        timeDateElement.setAdaptiveSize(timeDateScale);
        Serial.printf("[Layout] Scaled time/date to %.2f%% to fit quarter area\n", timeDateScale * 100.0f);
    }
    
    // Scale weather to fit quarter (with reduced margins: 25px left/right, 50px top/bottom)
    int16_t weatherW, weatherH;
    weatherElement.getDimensions(weatherW, weatherH);
    float weatherScale = 1.0f;
    if (weatherW > (quarterW - 50) || weatherH > (quarterH - 100)) {  // 25px margin each side = 50px total, 50px margin top/bottom = 100px total
        float scaleW = (float)(quarterW - 50) / weatherW;
        float scaleH = (float)(quarterH - 100) / weatherH;
        weatherScale = (scaleW < scaleH) ? scaleW : scaleH;
        if (weatherScale < 0.5f) weatherScale = 0.5f;  // Minimum 50% size
        weatherElement.setAdaptiveSize(weatherScale);
        Serial.printf("[Layout] Scaled weather to %.2f%% to fit quarter area\n", weatherScale * 100.0f);
    }
    
    // Scale quote to fit half area (with reduced margins: 50px top/bottom, 25px left/right)
    // Important: quoteH includes both quote text AND author, so scaling considers the full element
    int16_t quoteW, quoteH;
    quoteElement.getDimensions(quoteW, quoteH);
    float quoteScale = 1.0f;
    if (quoteW > (halfW - 50) || quoteH > (halfH_area - 100)) {  // 25px margin each side = 50px total, 50px margin top/bottom = 100px total
        float scaleW = (float)(halfW - 50) / quoteW;
        float scaleH = (float)(halfH_area - 100) / quoteH;  // 50px margin top and bottom
        quoteScale = (scaleW < scaleH) ? scaleW : scaleH;
        if (quoteScale < 0.5f) quoteScale = 0.5f;  // Minimum 50% size
        quoteElement.setAdaptiveSize(quoteScale);
        Serial.printf("[Layout] Scaled quote to %.2f%% to fit half area (width margin: 25px each side, height margin: 50px top/bottom)\n", quoteScale * 100.0f);
    }
    
    // Draw at fixed positions (centered in their assigned areas)
    Serial.printf("[Layout] Drawing time/date at (%d, %d) in %s quarter\n", 
                 timeDateCenterX, quarterCenterY, timeDateOnLeft ? "left" : "right");
    timeDateElement.draw(timeDateCenterX, quarterCenterY);
    
    Serial.printf("[Layout] Drawing weather at (%d, %d) in %s quarter\n", 
                 weatherCenterX, quarterCenterY, timeDateOnLeft ? "right" : "left");
    weatherElement.draw(weatherCenterX, quarterCenterY);
    
    Serial.printf("[Layout] Drawing quote at (%d, %d) in %s half\n", 
                 quoteCenterX, quoteCenterY, quoteOnTop ? "top" : "bottom");
    quoteElement.draw(quoteCenterX, quoteCenterY);
}

/**
 * Add text overlay (time/date/weather/quote) to an already-drawn display
 * This centralizes the logic so all commands (!go, !show, web UI, etc.) use the same code
 */
void addTextOverlayToDisplay(EL133UF1* display, EL133UF1_TTF* ttf, int16_t keepoutMargin) {
    char timeBuf[16];
    char dayBuf[16];
    char dateBuf[48];
    formatTimeAndDate(timeBuf, sizeof(timeBuf), dayBuf, sizeof(dayBuf), dateBuf, sizeof(dateBuf));
    placeTimeDateAndQuote(display, ttf, timeBuf, dayBuf, dateBuf, keepoutMargin);
}

/**
 * Unified function to display media from media mappings with text overlay and audio
 */
bool displayMediaWithOverlay(int targetIndex, int16_t keepoutMargin) {
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
    
    // Ensure TTF is initialized
    if (!ttf.begin(&display)) {
        Serial.println("ERROR: TTF initialization failed!");
        return false;
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
        loadMediaMappingsFromSD(false);  // Don't auto-publish
    }
    
    // Check if we have media mappings
    if (!g_media_mappings_loaded || g_media_mappings.size() == 0) {
        Serial.println("ERROR: No media.txt mappings found");
        return false;
    }
    
    // Set target index if specified (otherwise use sequential)
    if (targetIndex >= 0) {
        size_t mediaCount = g_media_mappings.size();
        if (targetIndex >= (int)mediaCount) {
            Serial.printf("ERROR: Index %d is out of bounds (max %zu)\n", targetIndex, mediaCount);
            return false;
        }
        // Set to targetIndex-1 so pngDrawFromMediaMappings increments to targetIndex
        lastMediaIndex = (targetIndex - 1 + mediaCount) % mediaCount;
    }
    
    // Load image from media mappings (this increments lastMediaIndex to show the target/next item)
    uint32_t sd_ms = 0, dec_ms = 0;
    bool ok = pngDrawFromMediaMappings(&sd_ms, &dec_ms);
    if (!ok) {
        Serial.println("ERROR: Failed to load image from media.txt");
        return false;
    }
    
    Serial.printf("PNG SD read: %lu ms, decode+draw: %lu ms\n", (unsigned long)sd_ms, (unsigned long)dec_ms);
    Serial.printf("Now at media index: %lu\n", (unsigned long)lastMediaIndex);
    
    // For GO command: after showing target index, advance to next index for future cycles
    if (targetIndex >= 0) {
        size_t mediaCount = g_media_mappings.size();
        lastMediaIndex = (lastMediaIndex + 1) % mediaCount;
        Serial.printf("GO command: Advanced index to %lu (next item for future cycles)\n", (unsigned long)lastMediaIndex);
    }
    
    // Save updated index to NVS
    mediaIndexSaveToNVS();
    
    // Yield before text placement
    vTaskDelay(1);
    
    // Add text overlay (time/date/weather/quote) - centralized function
    addTextOverlayToDisplay(&display, &ttf, keepoutMargin);
    
    // Yield after text placement
    vTaskDelay(1);
    
    // Update display
    // Note: The EL133UF1 library automatically calls publishMQTTThumbnailAlways() 
    // in updateAsync(), so thumbnails are published automatically for all display updates
    // We keep WiFi connected (if it was connected for weather) so thumbnail publishing
    // doesn't have to reconnect, which can take 15+ seconds
    Serial.println("Updating display (e-ink refresh - this will take 20-30 seconds)...");
    display.update();
    Serial.println("Display updated");
    
    // Wait for display refresh to complete before playing audio
    display.waitForUpdate();
    
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
    
    return true;
}

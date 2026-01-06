/**
 * @file display_manager.cpp
 * @brief Display manager implementation for unified media display with text overlay
 */

#include "display_manager.h"
#include "EL133UF1.h"
#include "EL133UF1_TTF.h"
#include "EL133UF1_PNG.h"  // For loading background image
#include "EL133UF1_SVG.h"  // For loading weather icon SVGs
#include "EL133UF1_TextPlacement.h"  // For wrapText function
#include "weather_icon_mapping.h"  // For mapping OpenWeatherMap codes to SVG paths
#include "weather_background_mapping.h"  // For mapping OpenWeatherMap codes to background PNG paths
#include "text_elements.h"
#include "wifi_manager.h"  // For wifiConnectPersistent (NOT wifi_guard.h - it disconnects WiFi!)
#include "platform_hal.h"  // For hal_psram_malloc/free
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <cstring>  // For strchr, strlen, memcpy
#include "cJSON.h"
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <SPI.h>
#include <stdio.h>  // For fopen, fread, etc.

// External references to globals and functions from main.cpp
extern SPIClass displaySPI;
extern EL133UF1 display;
extern EL133UF1_TTF ttf;
extern EL133UF1_PNG pngLoader;  // For loading background image
static EL133UF1_SVG svgLoader;  // For loading weather icon SVGs
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
    String foreground;
    String outline;
    String font;
    int thickness;
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
extern bool pngDrawFromMediaMappings(int index, uint32_t* out_sd_read_ms, uint32_t* out_decode_ms);
extern int getNextMediaIndex();
extern void setMediaIndex(int index);
extern int getCurrentMediaIndex();
extern void mediaIndexSaveToNVS();
extern String getAudioForImage(const String& imagePath);
extern bool playWavFile(const String& audioPath);
extern void audio_stop();
extern bool wifiLoadCredentials();
extern bool loadFontByName(const String& fontName);

// Forward declarations for internal helper functions
static bool fetchWeatherData(float lat, float lon, char* tempStr, size_t tempStrSize,
                             char* conditionStr, size_t conditionStrSize,
                             char* tempMaxStr = nullptr, size_t tempMaxStrSize = 0,
                             char* tempMinStr = nullptr, size_t tempMinStrSize = 0,
                             float* hourlyTemps = nullptr, char hourlyDescs[][32] = nullptr, 
                             time_t* hourlyTimestamps = nullptr, int* hourlyCount = nullptr,
                             time_t* currentTime = nullptr, char hourlyIcons[][16] = nullptr,
                             char* currentIcon = nullptr, size_t currentIconSize = 0,
                             int* currentWeatherId = nullptr, int32_t* timezoneOffset = nullptr);
static bool formatTimeAndDate(char* timeBuf, size_t timeBufSize, 
                              char* dayBuf, size_t dayBufSize,
                              char* dateBuf, size_t dateBufSize);
static void placeTimeDateAndQuote(EL133UF1* display, EL133UF1_TTF* ttf,
                                  const char* timeBuf, const char* dayBuf, const char* dateBuf,
                                  int16_t keepoutMargin, uint8_t textColor, uint8_t outlineColor, int16_t outlineThickness);

/**
 * Geocode a place name to get lat/lon coordinates using OpenWeatherMap Geocoding API
 * @param placeName Place name to geocode
 * @param lat Output latitude (set on success)
 * @param lon Output longitude (set on success)
 * @param formattedLocation Output formatted location string "Name, State, Country" (optional, can be nullptr)
 * @param formattedLocationSize Size of formattedLocation buffer (ignored if formattedLocation is nullptr)
 * @return true if geocoding successful, false otherwise
 */
static bool geocodePlaceName(const char* placeName, float* lat, float* lon, char* formattedLocation = nullptr, size_t formattedLocationSize = 0) {
    const char* apiKey = "4efd38c9e9d41e3b10724fe764541d7b";  // TODO: Replace with actual API key or load from NVS
    
    if (placeName == nullptr || placeName[0] == '\0' || lat == nullptr || lon == nullptr) {
        Serial.println("Geocoding API: Invalid parameters");
        return false;
    }
    
    Serial.printf("Geocoding API: Attempting to geocode place name: %s\n", placeName);
    
    // Yield to watchdog before starting HTTP request
    vTaskDelay(1);
    
    HTTPClient http;
    WiFiClient client;  // Geocoding API uses HTTP (not HTTPS)
    client.setTimeout(5000);  // 5 second timeout
    
    // Build geocoding API URL
    char url[512];  // Increased size for URL encoding
    // URL encode the place name (simple version - replace spaces with %20)
    String encodedPlaceName = String(placeName);
    encodedPlaceName.replace(" ", "%20");
    encodedPlaceName.replace(",", "%2C");
    
    snprintf(url, sizeof(url), 
             "http://api.openweathermap.org/geo/1.0/direct?q=%s&limit=1&appid=%s",
             encodedPlaceName.c_str(), apiKey);
    
    Serial.printf("Geocoding API: URL: %s\n", url);
    
    http.begin(client, url);
    http.setTimeout(8000);  // 8 second timeout
    
    // Yield to watchdog before blocking HTTP call
    vTaskDelay(1);
    
    int httpCode = http.GET();
    
    // Yield immediately after HTTP call to reset watchdog
    vTaskDelay(1);
    
    Serial.printf("Geocoding API: HTTP response code %d\n", httpCode);
    
    if (httpCode == HTTP_CODE_OK) {
        // Yield before reading response
        vTaskDelay(1);
        
        String payload = http.getString();
        Serial.printf("Geocoding API: Received payload (%d bytes)\n", payload.length());
        
        // Yield before JSON parsing
        vTaskDelay(1);
        
        // Parse JSON response using cJSON
        cJSON* json = cJSON_Parse(payload.c_str());
        if (json && cJSON_IsArray(json)) {
            int arraySize = cJSON_GetArraySize(json);
            if (arraySize > 0) {
                // Get first result
                cJSON* firstResult = cJSON_GetArrayItem(json, 0);
                if (firstResult) {
                    // Extract lat and lon
                    cJSON* latItem = cJSON_GetObjectItem(firstResult, "lat");
                    cJSON* lonItem = cJSON_GetObjectItem(firstResult, "lon");
                    
                    if (latItem && cJSON_IsNumber(latItem) && lonItem && cJSON_IsNumber(lonItem)) {
                        *lat = (float)latItem->valuedouble;
                        *lon = (float)lonItem->valuedouble;
                        
                        // Extract name, state, and country for formatted location string
                        cJSON* nameItem = cJSON_GetObjectItem(firstResult, "name");
                        const char* resolvedName = placeName;
                        if (nameItem && cJSON_IsString(nameItem)) {
                            resolvedName = nameItem->valuestring;
                        }
                        
                        cJSON* stateItem = cJSON_GetObjectItem(firstResult, "state");
                        const char* state = "";
                        if (stateItem && cJSON_IsString(stateItem)) {
                            state = stateItem->valuestring;
                        }
                        
                        cJSON* countryItem = cJSON_GetObjectItem(firstResult, "country");
                        const char* country = "";
                        if (countryItem && cJSON_IsString(countryItem)) {
                            country = countryItem->valuestring;
                        }
                        
                        // Format location string as "Name, State, Country" (if formattedLocation buffer provided)
                        if (formattedLocation != nullptr && formattedLocationSize > 0) {
                            if (state[0] != '\0' && country[0] != '\0') {
                                snprintf(formattedLocation, formattedLocationSize, "%s, %s, %s", 
                                        resolvedName, state, country);
                            } else if (country[0] != '\0') {
                                snprintf(formattedLocation, formattedLocationSize, "%s, %s", 
                                        resolvedName, country);
                            } else {
                                snprintf(formattedLocation, formattedLocationSize, "%s", resolvedName);
                            }
                        }
                        
                        Serial.printf("Geocoding API: SUCCESS - Resolved '%s' to %s, %s, %s (%.4f, %.4f)\n",
                                     placeName, resolvedName, state, country, *lat, *lon);
                        
                        cJSON_Delete(json);
                        http.end();
                        return true;
                    }
                }
            } else {
                Serial.printf("Geocoding API: No results found for place name: %s\n", placeName);
            }
        } else {
            Serial.println("Geocoding API: Failed to parse JSON response or response is not an array");
        }
        
        if (json) {
            cJSON_Delete(json);
        }
    } else {
        Serial.printf("Geocoding API: HTTP error %d\n", httpCode);
    }
    
    http.end();
    return false;
}

/**
 * Fetch weather data from OpenWeatherMap API
 */
static bool fetchWeatherData(float lat, float lon, char* tempStr, size_t tempStrSize,
                             char* conditionStr, size_t conditionStrSize,
                             char* tempMaxStr, size_t tempMaxStrSize,
                             char* tempMinStr, size_t tempMinStrSize,
                             float* hourlyTemps, char hourlyDescs[][32], 
                             time_t* hourlyTimestamps, int* hourlyCount,
                             time_t* currentTime, char hourlyIcons[][16],
                             char* currentIcon, size_t currentIconSize,
                             int* currentWeatherId, int32_t* timezoneOffset) {
    const char* apiKey = "4efd38c9e9d41e3b10724fe764541d7b";  // TODO: Replace with actual API key or load from NVS
    
    Serial.printf("Weather API: Attempting to fetch weather data (lat=%.4f, lon=%.4f)\n", lat, lon);
    
    // Yield to watchdog before starting HTTP request
    vTaskDelay(1);
    
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();  // Skip certificate verification for OpenWeatherMap API
    client.setTimeout(5000);  // 5 second timeout (reduced to avoid watchdog)
    
    // Build API URL - One Call API 3.0
    char url[256];
    snprintf(url, sizeof(url), 
             "https://api.openweathermap.org/data/3.0/onecall?lat=%.4f&lon=%.4f&exclude=minutely,alerts&units=metric&appid=%s",
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
        Serial.printf("Weather API: Received payload (%d bytes)\n", payload.length());
        
        // Yield before JSON parsing (can be CPU intensive)
        vTaskDelay(1);
        
        // Parse JSON response using cJSON
        cJSON* json = cJSON_Parse(payload.c_str());
        if (json) {
            // Extract timezone offset (seconds from UTC) from top-level response
            if (timezoneOffset) {
                cJSON* tzOffset = cJSON_GetObjectItem(json, "timezone_offset");
                if (tzOffset && cJSON_IsNumber(tzOffset)) {
                    *timezoneOffset = (int32_t)cJSON_GetNumberValue(tzOffset);
                    Serial.printf("Weather API: Timezone offset: %ld seconds (UTC%+ld)\n", 
                                 (long)*timezoneOffset, (long)(*timezoneOffset / 3600));
                } else {
                    *timezoneOffset = 0;
                    Serial.println("Weather API: No timezone_offset in response, using UTC");
                }
            }
            
            // Extract current weather data (One Call API 3.0 structure)
            cJSON* current = cJSON_GetObjectItem(json, "current");
            if (current) {
                // Extract current time (dt)
                if (currentTime) {
                    cJSON* dt = cJSON_GetObjectItem(current, "dt");
                    if (dt && cJSON_IsNumber(dt)) {
                        *currentTime = (time_t)dt->valueint;
                    } else {
                        *currentTime = 0;
                    }
                }
                
                // Extract temperature
                cJSON* temp = cJSON_GetObjectItem(current, "temp");
                if (temp && cJSON_IsNumber(temp)) {
                    float tempC = (float)temp->valuedouble;
                    snprintf(tempStr, tempStrSize, "%.0f°C", tempC);
                } else {
                    strncpy(tempStr, "N/A", tempStrSize - 1);
                    tempStr[tempStrSize - 1] = '\0';
                }
                
                // Yield before processing weather condition
                vTaskDelay(1);
                
                // Extract weather condition
                cJSON* weather = cJSON_GetObjectItem(current, "weather");
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
                        
                        // Extract icon code for current weather
                        if (currentIcon && currentIconSize > 0) {
                            cJSON* icon = cJSON_GetObjectItem(weatherItem, "icon");
                            if (icon && cJSON_IsString(icon)) {
                                strncpy(currentIcon, icon->valuestring, currentIconSize - 1);
                                currentIcon[currentIconSize - 1] = '\0';
                            } else {
                                currentIcon[0] = '\0';
                            }
                        }
                        
                        // Extract weather ID for current weather
                        if (currentWeatherId) {
                            cJSON* id = cJSON_GetObjectItem(weatherItem, "id");
                            if (id && cJSON_IsNumber(id)) {
                                *currentWeatherId = id->valueint;
                            } else {
                                *currentWeatherId = -1;
                            }
                        }
                    } else {
                        strncpy(conditionStr, "Unknown", conditionStrSize - 1);
                        conditionStr[conditionStrSize - 1] = '\0';
                        if (currentIcon && currentIconSize > 0) {
                            currentIcon[0] = '\0';
                        }
                        if (currentWeatherId) {
                            *currentWeatherId = -1;
                        }
                    }
                } else {
                    strncpy(conditionStr, "Unknown", conditionStrSize - 1);
                    conditionStr[conditionStrSize - 1] = '\0';
                    if (currentIcon && currentIconSize > 0) {
                        currentIcon[0] = '\0';
                    }
                    if (currentWeatherId) {
                        *currentWeatherId = -1;
                    }
                }
                
                // Extract daily forecast high/low temperatures (One Call API 3.0)
                if (tempMaxStr && tempMaxStrSize > 0 && tempMinStr && tempMinStrSize > 0) {
                    cJSON* daily = cJSON_GetObjectItem(json, "daily");
                    if (daily && cJSON_IsArray(daily)) {
                        cJSON* today = cJSON_GetArrayItem(daily, 0);
                        if (today) {
                            cJSON* temp = cJSON_GetObjectItem(today, "temp");
                            if (temp) {
                                cJSON* tempMax = cJSON_GetObjectItem(temp, "max");
                                cJSON* tempMin = cJSON_GetObjectItem(temp, "min");
                                if (tempMax && cJSON_IsNumber(tempMax)) {
                                    float tempMaxC = (float)tempMax->valuedouble;
                                    snprintf(tempMaxStr, tempMaxStrSize, "%.0f°C", tempMaxC);
                                } else {
                                    strncpy(tempMaxStr, "N/A", tempMaxStrSize - 1);
                                    tempMaxStr[tempMaxStrSize - 1] = '\0';
                                }
                                if (tempMin && cJSON_IsNumber(tempMin)) {
                                    float tempMinC = (float)tempMin->valuedouble;
                                    snprintf(tempMinStr, tempMinStrSize, "%.0f°C", tempMinC);
                                } else {
                                    strncpy(tempMinStr, "N/A", tempMinStrSize - 1);
                                    tempMinStr[tempMinStrSize - 1] = '\0';
                                }
                            } else {
                                strncpy(tempMaxStr, "N/A", tempMaxStrSize - 1);
                                tempMaxStr[tempMaxStrSize - 1] = '\0';
                                strncpy(tempMinStr, "N/A", tempMinStrSize - 1);
                                tempMinStr[tempMinStrSize - 1] = '\0';
                            }
                        } else {
                            strncpy(tempMaxStr, "N/A", tempMaxStrSize - 1);
                            tempMaxStr[tempMaxStrSize - 1] = '\0';
                            strncpy(tempMinStr, "N/A", tempMinStrSize - 1);
                            tempMinStr[tempMinStrSize - 1] = '\0';
                        }
                    } else {
                        strncpy(tempMaxStr, "N/A", tempMaxStrSize - 1);
                        tempMaxStr[tempMaxStrSize - 1] = '\0';
                        strncpy(tempMinStr, "N/A", tempMinStrSize - 1);
                        tempMinStr[tempMinStrSize - 1] = '\0';
                    }
                }
                
                // Extract hourly forecast (next 8 hours, skipping current hour 0)
                if (hourlyTemps && hourlyDescs && hourlyCount) {
                    cJSON* hourly = cJSON_GetObjectItem(json, "hourly");
                    if (hourly && cJSON_IsArray(hourly)) {
                        int count = cJSON_GetArraySize(hourly);
                        // Skip hour 0 (current hour), get next 8 hours (indices 1-8)
                        int startIndex = 1;
                        int maxHours = (count - startIndex < 8) ? (count - startIndex) : 8;
                        if (maxHours < 0) maxHours = 0;
                        *hourlyCount = 0;
                        
                        for (int i = startIndex; i < startIndex + maxHours && i < count; i++) {
                            cJSON* hour = cJSON_GetArrayItem(hourly, i);
                            if (hour) {
                                // Extract timestamp (dt)
                                if (hourlyTimestamps) {
                                    cJSON* dt = cJSON_GetObjectItem(hour, "dt");
                                    if (dt && cJSON_IsNumber(dt)) {
                                        hourlyTimestamps[*hourlyCount] = (time_t)dt->valueint;
                                    } else {
                                        hourlyTimestamps[*hourlyCount] = 0;
                                    }
                                }
                                
                                // Extract temperature
                                cJSON* temp = cJSON_GetObjectItem(hour, "temp");
                                if (temp && cJSON_IsNumber(temp)) {
                                    hourlyTemps[*hourlyCount] = (float)temp->valuedouble;
                                } else {
                                    hourlyTemps[*hourlyCount] = 0.0f;
                                }
                                
                                // Extract weather description and icon
                                cJSON* weather = cJSON_GetObjectItem(hour, "weather");
                                if (weather && cJSON_IsArray(weather)) {
                                    cJSON* weatherItem = cJSON_GetArrayItem(weather, 0);
                                    if (weatherItem) {
                                        cJSON* description = cJSON_GetObjectItem(weatherItem, "description");
                                        if (description && cJSON_IsString(description)) {
                                            const char* desc = description->valuestring;
                                            strncpy(hourlyDescs[*hourlyCount], desc, 31);
                                            hourlyDescs[*hourlyCount][31] = '\0';
                                            // Capitalize first letter
                                            if (hourlyDescs[*hourlyCount][0] >= 'a' && hourlyDescs[*hourlyCount][0] <= 'z') {
                                                hourlyDescs[*hourlyCount][0] = hourlyDescs[*hourlyCount][0] - 'a' + 'A';
                                            }
                                        } else {
                                            strncpy(hourlyDescs[*hourlyCount], "N/A", 31);
                                            hourlyDescs[*hourlyCount][31] = '\0';
                                        }
                                        
                                        // Extract icon name
                                        if (hourlyIcons) {
                                            cJSON* icon = cJSON_GetObjectItem(weatherItem, "icon");
                                            if (icon && cJSON_IsString(icon)) {
                                                strncpy(hourlyIcons[*hourlyCount], icon->valuestring, 15);
                                                hourlyIcons[*hourlyCount][15] = '\0';
                                            } else {
                                                hourlyIcons[*hourlyCount][0] = '\0';
                                            }
                                        }
                                    } else {
                                        strncpy(hourlyDescs[*hourlyCount], "N/A", 31);
                                        hourlyDescs[*hourlyCount][31] = '\0';
                                        if (hourlyIcons) {
                                            hourlyIcons[*hourlyCount][0] = '\0';
                                        }
                                    }
                                } else {
                                    strncpy(hourlyDescs[*hourlyCount], "N/A", 31);
                                    hourlyDescs[*hourlyCount][31] = '\0';
                                    if (hourlyIcons) {
                                        hourlyIcons[*hourlyCount][0] = '\0';
                                    }
                                }
                                
                                (*hourlyCount)++;
                            }
                        }
                    } else {
                        *hourlyCount = 0;
                    }
                }
            } else {
                // Fallback: try old format (2.5 API) for backwards compatibility
                // Try to get current time from hourly[0] if available
                if (currentTime) {
                    cJSON* hourly = cJSON_GetObjectItem(json, "hourly");
                    if (hourly && cJSON_IsArray(hourly)) {
                        cJSON* hour0 = cJSON_GetArrayItem(hourly, 0);
                        if (hour0) {
                            cJSON* dt = cJSON_GetObjectItem(hour0, "dt");
                            if (dt && cJSON_IsNumber(dt)) {
                                *currentTime = (time_t)dt->valueint;
                            } else {
                                *currentTime = 0;
                            }
                        } else {
                            *currentTime = 0;
                        }
                    } else {
                        *currentTime = 0;
                    }
                }
                
                // Note: Old format doesn't have hourly forecast
                if (hourlyCount) {
                    *hourlyCount = 0;
                }
                // Note: Old format doesn't have daily forecast, so set high/low to N/A if requested
                if (tempMaxStr && tempMaxStrSize > 0) {
                    strncpy(tempMaxStr, "N/A", tempMaxStrSize - 1);
                    tempMaxStr[tempMaxStrSize - 1] = '\0';
                }
                if (tempMinStr && tempMinStrSize > 0) {
                    strncpy(tempMinStr, "N/A", tempMinStrSize - 1);
                    tempMinStr[tempMinStrSize - 1] = '\0';
                }
                Serial.println("Weather API: No 'current' object found, trying legacy format...");
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
                
                cJSON* weather = cJSON_GetObjectItem(json, "weather");
                if (weather && cJSON_IsArray(weather)) {
                    cJSON* weatherItem = cJSON_GetArrayItem(weather, 0);
                    if (weatherItem) {
                        cJSON* description = cJSON_GetObjectItem(weatherItem, "description");
                        if (description && cJSON_IsString(description)) {
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
            // Try to get error payload and parse One Call API 3.0 error format
            String errorPayload = http.getString();
            Serial.printf("Weather API: Error response (%d bytes): %s\n", errorPayload.length(), errorPayload.c_str());
            
            // Parse One Call API 3.0 error format: {"cod": 400, "message": "...", "parameters": [...]}
            cJSON* errorJson = cJSON_Parse(errorPayload.c_str());
            if (errorJson) {
                cJSON* cod = cJSON_GetObjectItem(errorJson, "cod");
                cJSON* message = cJSON_GetObjectItem(errorJson, "message");
                if (cod && message && cJSON_IsString(message)) {
                    Serial.printf("Weather API: Error code %d - %s\n", 
                                 cod->valueint, message->valuestring);
                }
                cJSON_Delete(errorJson);
            }
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
 * Helper function to parse color string to color constant
 */
static uint8_t parseColorString(const String& colorStr) {
    String lower = colorStr;
    lower.toLowerCase();
    if (lower == "yellow") return EL133UF1_YELLOW;
    if (lower == "red") return EL133UF1_RED;
    if (lower == "blue") return EL133UF1_BLUE;
    if (lower == "green") return EL133UF1_GREEN;
    if (lower == "black") return EL133UF1_BLACK;
    return EL133UF1_WHITE;  // Default
}

/**
 * Simple fixed-layout function: divide screen into 3 areas (one half + two quarters)
 * Randomly assign: which half (top/bottom), which quarter gets time/date (left/right)
 */
static void placeTimeDateAndQuote(EL133UF1* display, EL133UF1_TTF* ttf,
                                  const char* timeBuf, const char* dayBuf, const char* dateBuf,
                                  int16_t keepoutMargin, uint8_t textColor, uint8_t outlineColor, int16_t outlineThickness = 3) {
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
    timeDateElement.setColors(textColor, outlineColor);
    WeatherElement weatherElement(ttf, weatherTemp, weatherCondition, weatherLocation);
    weatherElement.setColors(textColor, outlineColor);
    QuoteElement quoteElement(ttf, quoteText, quoteAuthor);
    quoteElement.setColors(textColor, outlineColor);
    quoteElement.setOutlineThickness(outlineThickness);
    
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
    
    // Scale quote to fit half area (with margins: 50px top/bottom, 40px left / 30px right)
    // Important: quoteH includes both quote text AND author, so scaling considers the full element
    // When quote is on bottom, add 10px extra to bottom margin (50px top, 60px bottom = 110px total)
    int16_t quoteW, quoteH;
    quoteElement.getDimensions(quoteW, quoteH);
    float quoteScale = 1.0f;
    int16_t quoteHeightMargin = quoteOnTop ? 100 : 110;  // 50px top/bottom when on top, 50px top/60px bottom when on bottom
    int16_t quoteWidthMargin = 70;  // 40px left + 30px right = 70px total
    if (quoteW > (halfW - quoteWidthMargin) || quoteH > (halfH_area - quoteHeightMargin)) {
        float scaleW = (float)(halfW - quoteWidthMargin) / quoteW;
        float scaleH = (float)(halfH_area - quoteHeightMargin) / quoteH;
        quoteScale = (scaleW < scaleH) ? scaleW : scaleH;
        if (quoteScale < 0.5f) quoteScale = 0.5f;  // Minimum 50% size
        quoteElement.setAdaptiveSize(quoteScale);
        Serial.printf("[Layout] Scaled quote to %.2f%% to fit half area (width margin: 40px left / 30px right, height margin: %dpx total - %s)\n", 
                     quoteScale * 100.0f, quoteHeightMargin, quoteOnTop ? "50px top/bottom" : "50px top/60px bottom");
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
void addTextOverlayToDisplay(EL133UF1* display, EL133UF1_TTF* ttf, int16_t keepoutMargin, uint8_t textColor, uint8_t outlineColor, int16_t outlineThickness) {
    char timeBuf[16];
    char dayBuf[16];
    char dateBuf[48];
    formatTimeAndDate(timeBuf, sizeof(timeBuf), dayBuf, sizeof(dayBuf), dateBuf, sizeof(dateBuf));
    placeTimeDateAndQuote(display, ttf, timeBuf, dayBuf, dateBuf, keepoutMargin, textColor, outlineColor, outlineThickness);
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
    
    // Determine which index to display
    int displayIndex;
    if (targetIndex >= 0) {
        // Specific index requested (e.g., from !go command)
        setMediaIndex(targetIndex);
        displayIndex = targetIndex;
    } else {
        // Sequential: get next index based on current mode
        displayIndex = getNextMediaIndex();
    }
    
    // Draw the image at the determined index (pure function - no state management)
    uint32_t sd_ms = 0, dec_ms = 0;
    bool ok = pngDrawFromMediaMappings(displayIndex, &sd_ms, &dec_ms);
    if (!ok) {
        Serial.println("ERROR: Failed to load image from media.txt");
        return false;
    }
    
    Serial.printf("PNG SD read: %lu ms, decode+draw: %lu ms\n", (unsigned long)sd_ms, (unsigned long)dec_ms);
    Serial.printf("Now at media index: %lu\n", (unsigned long)lastMediaIndex);
    
    // Yield before text placement
    vTaskDelay(1);
    
    // Get colors, thickness, and font from current media mapping
    uint8_t textColor = EL133UF1_WHITE;
    uint8_t outlineColor = EL133UF1_BLACK;
    int16_t outlineThickness = 3;  // Default thickness
    String fontName = "";  // Empty string = use default OpenSans
    if (lastMediaIndex < g_media_mappings.size()) {
        const MediaMapping& mapping = g_media_mappings[lastMediaIndex];
        if (mapping.foreground.length() > 0) {
            textColor = parseColorString(mapping.foreground);
        }
        if (mapping.outline.length() > 0) {
            outlineColor = parseColorString(mapping.outline);
        }
        if (mapping.thickness > 0) {
            outlineThickness = mapping.thickness;
        }
        if (mapping.font.length() > 0) {
            fontName = mapping.font;
        }
    }
    
    // Load the font specified in the mapping (falls back to OpenSans if not found)
    if (!loadFontByName(fontName)) {
        Serial.println("WARNING: Failed to load font, using default OpenSans");
        // loadFontByName already falls back to OpenSans, so this is just for logging
    }
    
    // Add text overlay (time/date/weather/quote) - centralized function
    addTextOverlayToDisplay(&display, &ttf, keepoutMargin, textColor, outlineColor, outlineThickness);
    
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

// Happy weather scene configuration
/**
 * Get default Happy weather scene configuration (hardcoded fallback)
 * This function returns the original hardcoded configuration values
 */
HappyWeatherConfig getDefaultHappyWeatherConfig() {
    HappyWeatherConfig config;
    
    // Locations (hardcoded defaults)
    config.locations[0] = {"Brienz",          46.75f,   8.03f,  1};
    config.locations[1] = {"Delden",          52.30f,   6.64f,  1};
    config.locations[2] = {"Portelet Beach",  49.17f,  -2.18f,  0};
    config.locations[3] = {"The Five Arrows", 51.85f,  -0.93f,  0};
    config.locations[4] = {"Isle of Mull",    56.44f,  -6.03f,  0};
    config.locations[5] = {"Bruvik",          60.48f,   5.68f,  1};
    config.numLocations = 6;
    
    // Layout constants (hardcoded defaults)
    config.displayWidth = 1600;
    config.displayHeight = 1200;
    config.marginTop = 80;
    config.marginBottom = 100;
    config.gapBetweenPanels = 25;
    
    // Panel widths (hardcoded defaults)
    config.panelWidths[0] = 200;
    config.panelWidths[1] = 280;
    config.panelWidths[2] = 280;
    config.panelWidths[3] = 280;
    config.panelWidths[4] = 280;
    config.panelWidths[5] = 190;
    config.numPanels = 6;
    
    // Background image path (hardcoded default)
    config.backgroundImagePath = "/littlefs/happy.png";
    
    // Font and spacing configuration (hardcoded defaults)
    config.baseTimeFontSize = 200.0f;
    config.baseLocationFontSize = 64.0f;
    config.locationFontSizeOffset = 8.0f;  // Added to calculated location font size
    config.gapBetweenLocationAndTime = 7;
    config.gapBetweenTimeAndWeather = 2;
    
    // Vertical positioning (hardcoded defaults)
    config.verticalMarginTop = 10;
    config.verticalMarginBottom = 50;
    
    // Horizontal offsets per panel (hardcoded defaults)
    config.horizontalOffsets[0] = 0;
    config.horizontalOffsets[1] = 0;
    config.horizontalOffsets[2] = 0;
    config.horizontalOffsets[3] = -10;  // Fourth column: nudge 10px left
    config.horizontalOffsets[4] = -20;  // Fifth column: nudge 20px left
    config.horizontalOffsets[5] = -29;  // Sixth column: nudge 29px left
    
    // First panel left margin (hardcoded default)
    config.firstPanelLeftMargin = 13;
    
    // Panel alignment (hardcoded defaults: true = top aligned, false = bottom aligned)
    config.panelTopAligned[0] = false;  // Panel 0: bottom aligned
    config.panelTopAligned[1] = true;   // Panel 1: top aligned
    config.panelTopAligned[2] = true;   // Panel 2: top aligned
    config.panelTopAligned[3] = false;  // Panel 3: bottom aligned
    config.panelTopAligned[4] = true;   // Panel 4: top aligned
    config.panelTopAligned[5] = true;   // Panel 5: top aligned
    
    return config;
}

/**
 * Helper function to format time with timezone offset
 * Returns formatted time string (HH:MM format)
 */
static void formatTimeWithTimezone(int8_t timezoneOffset, char* timeBuf, size_t timeBufSize) {
    time_t now = time(nullptr);
    
    if (now <= 1577836800) {  // before 2020-01-01, time not set
        snprintf(timeBuf, timeBufSize, "--:--");
        return;
    }
    
    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);
    
    // Apply timezone offset to hour (simple addition with wrap-around)
    int hour = tm_utc.tm_hour + timezoneOffset;
    // Handle wrap-around (hour can be negative or >= 24)
    while (hour < 0) hour += 24;
    while (hour >= 24) hour -= 24;
    
    // Format time
    snprintf(timeBuf, timeBufSize, "%02d:%02d", hour, tm_utc.tm_min);
}

/**
 * Display the Happy weather scene
 * Shows a blank background with 6 time/weather overlays (background image to be added later)
 * Layout: 6 horizontal panels with 50px margins on left/right, 80px top, 100px bottom, 50px gaps between panels
 */
bool displayHappyWeatherScene(const HappyWeatherConfig* config) {
    Serial.println("=== Happy Weather Scene ===");
    
    // Use default configuration if none provided (fallback to hardcoded values)
    HappyWeatherConfig defaultConfig;
    if (config == nullptr) {
        defaultConfig = getDefaultHappyWeatherConfig();
        config = &defaultConfig;
    }
    
    // Extract configuration values for easier access
    const HappyWeatherConfig::Location* locations = config->locations;
    const int numLocations = config->numLocations;
    const int16_t DISPLAY_WIDTH = config->displayWidth;
    const int16_t DISPLAY_HEIGHT = config->displayHeight;
    const int16_t MARGIN_TOP = config->marginTop;
    const int16_t MARGIN_BOTTOM = config->marginBottom;
    const int16_t GAP_BETWEEN_PANELS = config->gapBetweenPanels;
    const int16_t NUM_PANELS = config->numPanels;
    const int16_t* panelWidths = config->panelWidths;
    
    // Calculate total width to verify it fits (optional - for debugging)
    int16_t totalWidth = panelWidths[0];
    for (int i = 1; i < NUM_PANELS; i++) {
        totalWidth += GAP_BETWEEN_PANELS + panelWidths[i];
    }
    
    const int16_t availableHeight = DISPLAY_HEIGHT - MARGIN_TOP - MARGIN_BOTTOM;
    
    Serial.printf("[Happy Weather] Layout: %dx%d display, %d panels (widths: %d,%d,%d,%d,%d,%d), total=%dpx, %dpx height available\n",
                 DISPLAY_WIDTH, DISPLAY_HEIGHT, NUM_PANELS, 
                 panelWidths[0], panelWidths[1], panelWidths[2], panelWidths[3], panelWidths[4], panelWidths[5],
                 totalWidth, availableHeight);
    
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
    
    // Load background image from LittleFS
    Serial.println("Loading background image from LittleFS...");
    const char* bgImagePath = "/littlefs/happy.png";
    FILE* bgFile = fopen(bgImagePath, "rb");
    if (bgFile != nullptr) {
        // Get file size
        fseek(bgFile, 0, SEEK_END);
        long fileSize = ftell(bgFile);
        fseek(bgFile, 0, SEEK_SET);
        
        if (fileSize > 0 && fileSize < 10 * 1024 * 1024) {  // Max 10MB
            // Allocate buffer in PSRAM and read PNG data
            uint8_t* pngData = (uint8_t*)hal_psram_malloc(fileSize);
            if (pngData != nullptr) {
                size_t bytesRead = fread(pngData, 1, fileSize, bgFile);
                fclose(bgFile);
                
                if (bytesRead == (size_t)fileSize) {
                    // Initialize PNG loader if not already done
                    if (!pngLoader.begin(&display)) {
                        Serial.println("[Happy Weather] WARNING: Failed to initialize PNG loader");
                        display.clear(EL133UF1_WHITE);
                    } else {
                        // Draw background image (fullscreen, starting at 0,0)
                        Serial.printf("[Happy Weather] Drawing background image (%ld bytes)...\n", fileSize);
                        PNGResult result = pngLoader.draw(0, 0, pngData, fileSize);
                        if (result == PNG_OK) {
                            Serial.println("[Happy Weather] Background image drawn successfully");
                        } else {
                            Serial.printf("[Happy Weather] WARNING: Failed to draw background image: %s\n", 
                                        pngLoader.getErrorString(result));
                            // Fall back to white background
                            display.clear(EL133UF1_WHITE);
                        }
                    }
                } else {
                    Serial.printf("[Happy Weather] WARNING: Failed to read complete background image (%zu of %ld bytes)\n", 
                                bytesRead, fileSize);
                    display.clear(EL133UF1_WHITE);
                }
                hal_psram_free(pngData);
            } else {
                Serial.println("[Happy Weather] WARNING: Failed to allocate memory for background image");
                fclose(bgFile);
                display.clear(EL133UF1_WHITE);
            }
        } else {
            Serial.printf("[Happy Weather] WARNING: Invalid background image file size: %ld bytes\n", fileSize);
            fclose(bgFile);
            display.clear(EL133UF1_WHITE);
        }
    } else {
        Serial.printf("[Happy Weather] WARNING: Failed to open background image: %s (using white background)\n", bgImagePath);
        display.clear(EL133UF1_WHITE);
    }
    
    // Connect WiFi if needed (for weather fetching)
    bool wifiConnected = false;
    if (wifiLoadCredentials()) {
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[Happy Weather] WiFi already connected (IP: %s)\n", WiFi.localIP().toString().c_str());
            wifiConnected = true;
        } else {
            Serial.println("[Happy Weather] Connecting WiFi for weather data...");
            if (wifiConnectPersistent(3, 20000, false)) {
                Serial.printf("[Happy Weather] WiFi connected (IP: %s)\n", WiFi.localIP().toString().c_str());
                wifiConnected = true;
            }
        }
    }
    
    // First pass: Calculate dimensions for all panels to find maximum sizes
    // This ensures consistent sizing across all panels, with time maximized
    struct PanelData {
        String timeText;
        String tempText;
        String conditionText;
        String locationName;
        int16_t timeWidth;
        int16_t locationWidth;
        int16_t weatherWidth;
        int16_t weatherHeight;
    };
    
    PanelData panelData[NUM_PANELS];
    int16_t maxTimeWidth = 0;
    int16_t maxLocationWidth = 0;
    int16_t maxWeatherWidth = 0;
    int16_t maxWeatherHeight = 0;
    float baseTimeFontSize = 200.0f;  // Start with larger base size to maximize time
    float baseLocationFontSize = 64.0f;  // Ideal size for location name (64px)
    const int16_t gapBetweenLocationAndTime = 7;  // Reduced to half of 15
    const int16_t gapBetweenTimeAndWeather = 2;  // Reduced to half of 5
    
    Serial.println("[Happy Weather] First pass: Calculating dimensions for all panels...");
    
    for (int i = 0; i < NUM_PANELS; i++) {
        const auto& loc = locations[i];
        PanelData& data = panelData[i];
        
        vTaskDelay(1);  // Yield to watchdog
        
        // Fetch weather data
        char tempStr[16] = "N/A";
        char conditionStr[64] = "N/A";
        
        if (wifiConnected) {
            if (fetchWeatherData(loc.lat, loc.lon, tempStr, sizeof(tempStr), 
                                conditionStr, sizeof(conditionStr))) {
                Serial.printf("[Happy Weather] Weather for %s: %s, %s\n", loc.name, tempStr, conditionStr);
            } else {
                Serial.printf("[Happy Weather] Failed to fetch weather for %s, using fallback\n", loc.name);
            }
        } else {
            Serial.printf("[Happy Weather] WiFi not connected, using fallback for %s\n", loc.name);
        }
        
        // Store weather data
        data.tempText = String(tempStr);
        data.conditionText = String(conditionStr);
        data.locationName = String(loc.name);
        
        // Format time with timezone offset
        char timeBuf[16];
        formatTimeWithTimezone(loc.timezoneOffset, timeBuf, sizeof(timeBuf));
        data.timeText = String(timeBuf);
        
        // Calculate time dimensions at base size
        data.timeWidth = ttf.getTextWidth(data.timeText.c_str(), baseTimeFontSize) + (3 * 2);  // + outline
        
        // Calculate location name dimensions at base size (will wrap if needed)
        // Check if location name fits in panel width; if not, we'll need to wrap or scale down
        data.locationWidth = ttf.getTextWidth(data.locationName.c_str(), baseLocationFontSize) + (1 * 2);  // + outline (1px)
        
        // Create weather element at base size (scale 1.0) to get dimensions
        // Note: We pass empty string for location since we'll draw it separately above time
        WeatherElement weatherElement(&ttf, data.tempText.c_str(), data.conditionText.c_str(), "");
        weatherElement.setColors(EL133UF1_BLACK, EL133UF1_BLACK);  // Black text with black outline
        weatherElement.setAdaptiveSize(1.0f);  // Base size
        
        weatherElement.getDimensions(data.weatherWidth, data.weatherHeight);
        
        // Track maximum dimensions across all panels
        if (data.timeWidth > maxTimeWidth) {
            maxTimeWidth = data.timeWidth;
        }
        if (data.locationWidth > maxLocationWidth) {
            maxLocationWidth = data.locationWidth;
        }
        if (data.weatherWidth > maxWeatherWidth) {
            maxWeatherWidth = data.weatherWidth;
        }
        if (data.weatherHeight > maxWeatherHeight) {
            maxWeatherHeight = data.weatherHeight;
        }
        
        Serial.printf("[Happy Weather] Panel %d (%s): time='%s' width=%d, weather=%dx%d\n",
                     i + 1, loc.name, data.timeText.c_str(), data.timeWidth, data.weatherWidth, data.weatherHeight);
    }
    
    Serial.printf("[Happy Weather] Maximum dimensions: location width=%d, time width=%d, weather=%dx%d\n", 
                 maxLocationWidth, maxTimeWidth, maxWeatherWidth, maxWeatherHeight);
    
    // Calculate scale factors to maximize time while ensuring everything fits
    // Find the narrowest effective panel width for scaling calculations
    // Column 6 (index 5) is treated as 18px narrower for sizing (was 14px, now 18px)
    int16_t effectivePanelWidths[NUM_PANELS];
    for (int i = 0; i < NUM_PANELS; i++) {
        effectivePanelWidths[i] = panelWidths[i];
        if (i == 5) {  // Sixth column: treat as 18px narrower
            effectivePanelWidths[i] -= 18;
        }
    }
    int16_t minPanelWidth = effectivePanelWidths[0];
    for (int i = 1; i < NUM_PANELS; i++) {
        if (effectivePanelWidths[i] < minPanelWidth) {
            minPanelWidth = effectivePanelWidths[i];
        }
    }
    
    // Location names: use ideal 64px size, but scale down if text exceeds panel width
    // (Wrapping could be implemented in the future for better text handling)
    float locationScale = 1.0f;
    if (maxLocationWidth > minPanelWidth) {
        locationScale = (float)minPanelWidth / maxLocationWidth;
    }
    
    // Start with time scale (maximize time, use narrowest panel)
    float timeScale = 1.0f;
    if (maxTimeWidth > minPanelWidth) {
        timeScale = (float)minPanelWidth / maxTimeWidth;
    }
    
    // Calculate heights at these scales
    float finalLocationFontSize = baseLocationFontSize * locationScale + config->locationFontSizeOffset;
    int16_t finalLocationHeight = ttf.getTextHeight(finalLocationFontSize);
    float finalTimeFontSize = baseTimeFontSize * timeScale;
    int16_t finalTimeHeight = ttf.getTextHeight(finalTimeFontSize);
    
    // Check if weather would fit at this time scale (considering width and total height, use narrowest panel)
    float weatherScaleForWidth = 1.0f;
    if (maxWeatherWidth > minPanelWidth) {
        weatherScaleForWidth = (float)minPanelWidth / maxWeatherWidth;
    }
    
    // Calculate total height with location, time, and weather at base size
    int16_t totalHeightAtBaseWeather = finalLocationHeight + gapBetweenLocationAndTime + 
                                       finalTimeHeight + gapBetweenTimeAndWeather + maxWeatherHeight;
    float weatherScaleForHeight = 1.0f;
    if (totalHeightAtBaseWeather > availableHeight) {
        // Need to scale weather down to fit
        int16_t availableHeightForWeather = availableHeight - finalLocationHeight - gapBetweenLocationAndTime - 
                                           finalTimeHeight - gapBetweenTimeAndWeather;
        weatherScaleForHeight = (float)availableHeightForWeather / maxWeatherHeight;
    }
    
    // Use the smaller weather scale (most constraining)
    float weatherScale = (weatherScaleForWidth < weatherScaleForHeight) ? weatherScaleForWidth : weatherScaleForHeight;
    if (weatherScale < 0.3f) weatherScale = 0.3f;  // Minimum scale
    if (weatherScale > 1.0f) weatherScale = 1.0f;
    
    // Recalculate with weather at weatherScale to get accurate total height
    int16_t scaledWeatherHeight = (int16_t)(maxWeatherHeight * weatherScale);
    int16_t finalTotalHeight = finalLocationHeight + gapBetweenLocationAndTime + 
                               finalTimeHeight + gapBetweenTimeAndWeather + scaledWeatherHeight;
    
    // If total height still doesn't fit, we need to reduce time scale (and possibly location scale)
    if (finalTotalHeight > availableHeight) {
        // Calculate what height would be needed
        int16_t availableHeightForLocationAndTime = availableHeight - gapBetweenLocationAndTime - 
                                                    gapBetweenTimeAndWeather - scaledWeatherHeight;
        // Try reducing time scale first (maximize time)
        if (availableHeightForLocationAndTime > finalLocationHeight) {
            int16_t availableHeightForTime = availableHeightForLocationAndTime - finalLocationHeight;
            if (availableHeightForTime > 0 && availableHeightForTime < finalTimeHeight) {
                float timeHeightScale = (float)availableHeightForTime / finalTimeHeight;
                timeScale *= timeHeightScale;
                finalTimeFontSize = baseTimeFontSize * timeScale;
                finalTimeHeight = ttf.getTextHeight(finalTimeFontSize);
                finalTotalHeight = finalLocationHeight + gapBetweenLocationAndTime + 
                                 finalTimeHeight + gapBetweenTimeAndWeather + scaledWeatherHeight;
            }
        } else {
            // If even location doesn't fit, scale everything proportionally
            float totalHeightScale = (float)availableHeightForLocationAndTime / (finalLocationHeight + finalTimeHeight);
            locationScale *= totalHeightScale;
            timeScale *= totalHeightScale;
            finalLocationFontSize = baseLocationFontSize * locationScale + config->locationFontSizeOffset;
            finalLocationHeight = ttf.getTextHeight(finalLocationFontSize);
            finalTimeFontSize = baseTimeFontSize * timeScale;
            finalTimeHeight = ttf.getTextHeight(finalTimeFontSize);
            finalTotalHeight = finalLocationHeight + gapBetweenLocationAndTime + 
                             finalTimeHeight + gapBetweenTimeAndWeather + scaledWeatherHeight;
        }
    }
    
    Serial.printf("[Happy Weather] Final scales: location=%.2f (font size=%.1f), time=%.2f (font size=%.1f), weather=%.2f\n", 
                 locationScale, finalLocationFontSize, timeScale, finalTimeFontSize, weatherScale);
    
    // Second pass: Draw all panels with consistent sizing
    Serial.println("[Happy Weather] Second pass: Drawing all panels with uniform scales...");
    
    for (int i = 0; i < NUM_PANELS; i++) {
        const auto& loc = locations[i];
        const PanelData& data = panelData[i];
        
        vTaskDelay(1);  // Yield to watchdog
        
        // Calculate panel boundaries using custom widths
        // First column starts with configured margin, others start at 0
        int16_t panelLeft = (i == 0) ? config->firstPanelLeftMargin : 0;
        for (int j = 0; j < i; j++) {
            panelLeft += panelWidths[j] + GAP_BETWEEN_PANELS;
        }
        
        // Horizontal offset from config
        int16_t horizontalOffset = config->horizontalOffsets[i];
        
        int16_t panelCenterX = panelLeft + panelWidths[i] / 2 + horizontalOffset;
        
        // Vertical positioning from config
        int16_t contentReferenceY;
        if (config->panelTopAligned[i]) {
            // Top-aligned: reference Y is at the top
            contentReferenceY = MARGIN_TOP + config->verticalMarginTop;
        } else {
            // Bottom-aligned: reference Y is at the bottom
            contentReferenceY = DISPLAY_HEIGHT - config->verticalMarginBottom;
        }
        
        // Create weather element with uniform weather scale (without location - we draw it separately)
        WeatherElement weatherElement(&ttf, data.tempText.c_str(), data.conditionText.c_str(), "");
        weatherElement.setColors(EL133UF1_BLACK, EL133UF1_WHITE);  // Black text with white outline
        weatherElement.setAdaptiveSize(weatherScale);
        
        int16_t weatherW, weatherH;
        weatherElement.getDimensions(weatherW, weatherH);
        
        // Position content relative to reference Y (top or bottom aligned)
        // For top-aligned panels, reference Y is at top; for bottom-aligned, it's at bottom
        bool isTopAligned = config->panelTopAligned[i];
        int16_t locationY, timeY, weatherY;
        if (isTopAligned) {
            // Top-aligned: reference Y is at top, position content from top
            locationY = contentReferenceY + finalLocationHeight / 2;
            timeY = contentReferenceY + finalLocationHeight + gapBetweenLocationAndTime + finalTimeHeight / 2;
            weatherY = contentReferenceY + finalLocationHeight + gapBetweenLocationAndTime + 
                      finalTimeHeight + gapBetweenTimeAndWeather + weatherH / 2;
        } else {
            // Bottom-aligned: reference Y is at bottom, position content from bottom
            locationY = contentReferenceY - finalTotalHeight + finalLocationHeight / 2;
            timeY = contentReferenceY - finalTotalHeight + finalLocationHeight + gapBetweenLocationAndTime + finalTimeHeight / 2;
            weatherY = contentReferenceY - finalTotalHeight + finalLocationHeight + gapBetweenLocationAndTime + 
                      finalTimeHeight + gapBetweenTimeAndWeather + weatherH / 2;
        }
        
        // Draw location name (centered horizontally in panel) with 1px outline at ideal 64px size
        // Note: If location name is too wide, it may extend beyond panel bounds (wrapping not yet implemented)
        ttf.drawTextAlignedOutlined(panelCenterX, locationY, data.locationName.c_str(), finalLocationFontSize,
                                    EL133UF1_BLACK, EL133UF1_BLACK,
                                    ALIGN_CENTER, ALIGN_MIDDLE, 1);  // 1px black outline
        
        // Draw time text (centered horizontally in panel) with maximized size
        ttf.drawTextAlignedOutlined(panelCenterX, timeY, data.timeText.c_str(), finalTimeFontSize,
                                    EL133UF1_BLACK, EL133UF1_WHITE,
                                    ALIGN_CENTER, ALIGN_MIDDLE, 3);  // 3px white outline
        
        // Draw weather element (centered horizontally in panel)
        weatherElement.draw(panelCenterX, weatherY);
        
        Serial.printf("[Happy Weather] Panel %d (%s): drawn at (%d,%d), time='%s' (size=%.1f), weather scale=%.2f\n",
                     i + 1, loc.name, panelCenterX, contentReferenceY, data.timeText.c_str(), finalTimeFontSize, weatherScale);
    }
    
    // Update display
    Serial.println("Updating display...");
    display.update();
    display.waitForUpdate();
    Serial.println("Display updated");
    
    return true;
}

/**
 * Display weather for a single location
 */
bool displayWeatherForPlace(float lat, float lon, const char* placeName) {
    // Use geocoding API to convert place name to lat/lon coordinates
    // The lat/lon parameters are kept for backwards compatibility but are now primarily used as fallback
    float actualLat = lat;
    float actualLon = lon;
    bool usedGeocoding = false;
    char formattedLocation[256] = "";  // Store formatted location string from geocoding
    
    // Always use geocoding if place name is provided
    if (placeName != nullptr && placeName[0] != '\0') {
        Serial.printf("=== Geocoding place name: %s ===\n", placeName);
        
        // Ensure WiFi is connected for geocoding
        if (!wifiLoadCredentials()) {
            Serial.println("ERROR: WiFi credentials not available for geocoding");
            return false;
        }
        
        // Connect WiFi if needed (but don't disconnect - might be needed for weather query)
        bool wifiConnected = false;
        if (WiFi.status() == WL_CONNECTED) {
            wifiConnected = true;
            Serial.printf("Geocoding: WiFi already connected (IP: %s)\n", WiFi.localIP().toString().c_str());
        } else {
            Serial.println("Geocoding: WiFi not connected, attempting connection...");
            if (wifiConnectPersistent(3, 20000, false)) {
                wifiConnected = true;
                Serial.printf("Geocoding: WiFi connected (IP: %s)\n", WiFi.localIP().toString().c_str());
            }
        }
        
        if (wifiConnected) {
            if (geocodePlaceName(placeName, &actualLat, &actualLon, formattedLocation, sizeof(formattedLocation))) {
                usedGeocoding = true;
                Serial.printf("Geocoding SUCCESS: %s -> (%.4f, %.4f)\n", placeName, actualLat, actualLon);
                if (formattedLocation[0] != '\0') {
                    Serial.printf("Formatted location: %s\n", formattedLocation);
                }
            } else {
                Serial.printf("Geocoding FAILED for: %s\n", placeName);
                // If geocoding fails and no valid lat/lon provided, return error
                if (lat == 0.0f && lon == 0.0f) {
                    Serial.println("ERROR: Geocoding failed and no coordinates provided");
                    return false;
                }
                // Otherwise, fall back to provided coordinates
                Serial.println("Falling back to provided coordinates");
            }
        } else {
            Serial.println("ERROR: WiFi connection failed for geocoding");
            // If geocoding fails and no valid lat/lon provided, return error
            if (lat == 0.0f && lon == 0.0f) {
                return false;
            }
            // Otherwise, fall back to provided coordinates
            Serial.println("Falling back to provided coordinates");
        }
    }
    
    // Use formatted location if geocoding was used and succeeded, otherwise use original placeName
    const char* displayName = (usedGeocoding && formattedLocation[0] != '\0') ? formattedLocation : placeName;
    
    Serial.printf("=== Weather for Place: %s (%.4f, %.4f)%s ===\n", 
                 displayName, actualLat, actualLon, usedGeocoding ? " [geocoded]" : "");
    
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
    
    // Reset font to default OpenSans (in case a custom font was loaded by a previous scene)
    if (!loadFontByName("")) {
        Serial.println("WARNING: Failed to load default OpenSans font");
    }
    
    // Ensure PNG loader is initialized for background images
    if (!pngLoader.begin(&display)) {
        Serial.println("ERROR: PNG loader initialization failed!");
        return false;
    }
    
    // Ensure SVG loader is initialized for weather icons
    if (!svgLoader.begin(&display)) {
        Serial.println("ERROR: SVG loader initialization failed!");
        return false;
    }
    
    // Configure SVG loader for white-on-transparent rendering (invert black to white)
    svgLoader.setInvertColors(true);
    svgLoader.setAutoCrop(false);  // Don't auto-crop weather icons
    
    // Clear display to white background
    display.clear(EL133UF1_WHITE);
    
    // Fetch weather data (we'll load background after getting current weather icon/ID)
    char tempStr[32];
    char conditionStr[64];
    char tempMaxStr[32];
    char tempMinStr[32];
    time_t currentTime = 0;
    
    // Allocate hourly forecast arrays in PSRAM (8 hours, skipping current hour)
    float* hourlyTemps = (float*)hal_psram_malloc(8 * sizeof(float));
    char (*hourlyDescs)[32] = (char(*)[32])hal_psram_malloc(8 * 32 * sizeof(char));
    time_t* hourlyTimestamps = (time_t*)hal_psram_malloc(8 * sizeof(time_t));
    char (*hourlyIcons)[16] = (char(*)[16])hal_psram_malloc(8 * 16 * sizeof(char));
    int hourlyCount = 0;
    
    if (hourlyTemps == nullptr || hourlyDescs == nullptr || hourlyTimestamps == nullptr || hourlyIcons == nullptr) {
        Serial.println("ERROR: Failed to allocate hourly forecast arrays in PSRAM");
        if (hourlyTemps) hal_psram_free(hourlyTemps);
        if (hourlyDescs) hal_psram_free(hourlyDescs);
        if (hourlyTimestamps) hal_psram_free(hourlyTimestamps);
        if (hourlyIcons) hal_psram_free(hourlyIcons);
        return false;
    }
    
    // Variables to store current weather icon and ID for background selection
    char currentIcon[16] = {0};
    int currentWeatherId = -1;
    int32_t timezoneOffset = 0;  // Timezone offset in seconds from UTC

    bool fetchSuccess = fetchWeatherData(actualLat, actualLon, tempStr, sizeof(tempStr), conditionStr, sizeof(conditionStr),
                                         tempMaxStr, sizeof(tempMaxStr), tempMinStr, sizeof(tempMinStr),
                                         hourlyTemps, hourlyDescs, hourlyTimestamps, &hourlyCount, &currentTime, hourlyIcons,
                                         currentIcon, sizeof(currentIcon), &currentWeatherId, &timezoneOffset);
    
    if (!fetchSuccess) {
        Serial.println("ERROR: Failed to fetch weather data");
        // Display error message
        ttf.drawTextAlignedOutlined(display.width() / 2, display.height() / 2,
                                    "Weather data unavailable", 64.0f,
                                    EL133UF1_BLACK, EL133UF1_WHITE,
                                    ALIGN_CENTER, ALIGN_MIDDLE, 2);
        display.update();
        hal_psram_free(hourlyTemps);
        hal_psram_free(hourlyDescs);
        hal_psram_free(hourlyTimestamps);
        hal_psram_free(hourlyIcons);
        return false;
    }
    
    Serial.printf("Weather: %s, %s (High: %s, Low: %s)\n", tempStr, conditionStr, tempMaxStr, tempMinStr);
    
    // Load and draw background image based on current weather condition
    if (currentIcon[0] != '\0') {
        char bgPath[128];
        if (getWeatherBackgroundPath(currentIcon, bgPath, sizeof(bgPath), currentWeatherId)) {
            // Load PNG background file from LittleFS
            FILE* bgFile = fopen(bgPath, "rb");
            if (bgFile != nullptr) {
                // Get file size
                fseek(bgFile, 0, SEEK_END);
                long fileSize = ftell(bgFile);
                fseek(bgFile, 0, SEEK_SET);
                
                if (fileSize > 0 && fileSize < 5000000) {  // Max 5MB for fullscreen background
                    // Allocate buffer in PSRAM and read PNG data
                    uint8_t* bgData = (uint8_t*)hal_psram_malloc(fileSize);
                    if (bgData != nullptr) {
                        size_t bytesRead = fread(bgData, 1, fileSize, bgFile);
                        fclose(bgFile);  // Close file immediately after reading
                        
                        if (bytesRead == (size_t)fileSize) {
                            // Draw background fullscreen (fullscreen mode)
                            PNGResult bgResult = pngLoader.drawFullscreen(bgData, fileSize);
                            if (bgResult != PNG_OK) {
                                Serial.printf("Failed to draw background PNG (%s): %s\n", 
                                             bgPath, pngLoader.getErrorString(bgResult));
                            } else {
                                Serial.printf("Drew background image: %s\n", bgPath);
                            }
                            
                            hal_psram_free(bgData);
                        } else {
                            Serial.printf("Failed to read background PNG file %s: read %zu of %ld bytes\n", 
                                         bgPath, bytesRead, fileSize);
                            hal_psram_free(bgData);
                        }
                    } else {
                        Serial.printf("Failed to allocate %ld bytes for background PNG %s\n", fileSize, bgPath);
                        fclose(bgFile);
                    }
                } else {
                    Serial.printf("Invalid background PNG file size %ld for %s\n", fileSize, bgPath);
                    fclose(bgFile);
                }
            } else {
                Serial.printf("Failed to open background PNG file: %s\n", bgPath);
            }
        } else {
            Serial.printf("Failed to get background path for icon code: %s\n", currentIcon);
        }
    }
    
    // Display location name at top (centered) - same size as time and temperature
    // Use formatted location if geocoding was used, otherwise use original placeName
    const float nameFontSize = 120.0f;  // Same as temp
    int16_t nameY = 150;
    ttf.drawTextAlignedOutlined(display.width() / 2, nameY, displayName, nameFontSize,
                                EL133UF1_WHITE, EL133UF1_BLACK,
                                ALIGN_CENTER, ALIGN_MIDDLE, 3);
    
    // Display current time below location name - equal spacing to time-to-temperature gap
    // Use location's timezone offset to convert UTC timestamp to local time
    int16_t timeY = nameY + 130;  // Equal spacing to time-to-temperature (130px)
    if (currentTime > 0) {
        // Convert UTC timestamp to local time using timezone offset
        time_t localTime = currentTime + timezoneOffset;
        struct tm* timeinfo = gmtime(&localTime);  // Use gmtime since we already applied offset
        char timeBuf[16];
        strftime(timeBuf, sizeof(timeBuf), "%H:%M", timeinfo);
        const float timeFontSize = 120.0f;  // Same as name and temp
        ttf.drawTextAlignedOutlined(display.width() / 2, timeY, timeBuf, timeFontSize,
                                    EL133UF1_WHITE, EL133UF1_BLACK,
                                    ALIGN_CENTER, ALIGN_MIDDLE, 3);
    }
    
    // Display temperature below time (centered, same font size as name and time)
    const float tempFontSize = 120.0f;
    int16_t tempY = timeY + 130;
    ttf.drawTextAlignedOutlined(display.width() / 2, tempY, tempStr, tempFontSize,
                                EL133UF1_WHITE, EL133UF1_BLACK,
                                ALIGN_CENTER, ALIGN_MIDDLE, 3);
    
    // Display high/low temperatures below current temperature
    const float hiLoFontSize = 96.0f;  // Doubled from 48.0f
    int16_t hiLoY = tempY + 140;  // Spacing from temperature (lowered by 10px)
    char hiLoStr[128];
    snprintf(hiLoStr, sizeof(hiLoStr), "High: %s / Low: %s", tempMaxStr, tempMinStr);
    ttf.drawTextAlignedOutlined(display.width() / 2, hiLoY, hiLoStr, hiLoFontSize,
                                EL133UF1_WHITE, EL133UF1_BLACK,
                                ALIGN_CENTER, ALIGN_MIDDLE, 2);
    
    // Display condition halfway between current temperature and high/low
    const float conditionFontSize = 96.0f;  // Doubled from 48.0f
    int16_t conditionY = tempY + 70;  // Halfway between tempY and hiLoY: (0 + 140) / 2 = 70
    ttf.drawTextAlignedOutlined(display.width() / 2, conditionY, conditionStr, conditionFontSize,
                                EL133UF1_WHITE, EL133UF1_BLACK,
                                ALIGN_CENTER, ALIGN_MIDDLE, 2);
    
    // Display hourly forecast strip (next 8 hours, skipping current hour) below high/low temperatures
    if (hourlyCount > 0) {
        const float hourlyTimeFontSize = 58.0f;  // Increased from 24.0f
        const float hourlyTempFontSize = 80.0f;  // Doubled from 40.0f
        const float hourlyDescFontSize = 40.0f;  // Reduced from 52.0f
        int16_t hourlyY = hiLoY + 130;  // Increased spacing for larger fonts above
        int16_t displayWidth = display.width();
        int16_t stripWidth = displayWidth - 40;  // 20px margin on each side
        int16_t itemWidth = stripWidth / hourlyCount;
        
        for (int i = 0; i < hourlyCount; i++) {
            int16_t itemX = 20 + (itemWidth * i) + (itemWidth / 2);  // Center of each item
            
            // Display time (from dt timestamp) - use location's timezone offset
            int16_t timeY = hourlyY - 10;  // Reduced gap from -40 to -10 (closer to icon)
            if (hourlyTimestamps && hourlyTimestamps[i] > 0) {
                // Convert UTC timestamp to local time using timezone offset
                time_t localTime = hourlyTimestamps[i] + timezoneOffset;
                struct tm* timeinfo = gmtime(&localTime);  // Use gmtime since we already applied offset
                char timeBuf[8];
                strftime(timeBuf, sizeof(timeBuf), "%H:%M", timeinfo);
                ttf.drawTextAlignedOutlined(itemX, timeY, timeBuf, hourlyTimeFontSize,
                                            EL133UF1_WHITE, EL133UF1_BLACK,
                                            ALIGN_CENTER, ALIGN_MIDDLE, 1);
            }
            
            // Display weather icon (SVG, rendered at 128x128 pixels, centered in column)
            int16_t iconY = hourlyY + 5;  // Gap from time
            if (hourlyIcons[i][0] != '\0') {
                // Get SVG path for this icon code
                char iconPath[128];
                if (getWeatherIconPath(hourlyIcons[i], iconPath, sizeof(iconPath))) {
                    // Load SVG file from LittleFS
                    FILE* iconFile = fopen(iconPath, "rb");
                    if (iconFile != nullptr) {
                        // Get file size
                        fseek(iconFile, 0, SEEK_END);
                        long fileSize = ftell(iconFile);
                        fseek(iconFile, 0, SEEK_SET);
                        
                        if (fileSize > 0 && fileSize < 200000) {  // Max 200KB SVG
                            // Allocate buffer in PSRAM and read SVG data
                            uint8_t* iconData = (uint8_t*)hal_psram_malloc(fileSize + 1);  // +1 for null terminator
                            if (iconData != nullptr) {
                                size_t bytesRead = fread(iconData, 1, fileSize, iconFile);
                                fclose(iconFile);  // Close file immediately after reading
                                
                                if (bytesRead == (size_t)fileSize) {
                                    // Null-terminate for SVG loader (accepts string or binary)
                                    iconData[fileSize] = '\0';
                                    
                                    // Icons are rendered at 128x128 pixels, center them horizontally in the item column
                                    int16_t iconX = itemX - 64;  // Center the 128px icon
                                    
                                    // Render SVG at 128x128 (SVG viewBox is 512x512, so scale = 128/512 = 0.25)
                                    // Actually, let's use a scale that makes it fit nicely - SVG icons are typically 512x512 viewBox
                                    // For 128px output from 512px viewBox: scale = 128/512 = 0.25
                                    float iconScale = 128.0f / 512.0f;  // Scale to 128px
                                    
                                    SVGResult iconResult = svgLoader.draw(iconX, iconY, iconData, fileSize, iconScale);
                                    if (iconResult != SVG_OK) {
                                        Serial.printf("  Failed to draw SVG icon %d (%s): %s\n", 
                                                     i, iconPath, svgLoader.getErrorString(iconResult));
                                    }
                                    
                                    hal_psram_free(iconData);
                                } else {
                                    Serial.printf("  Failed to read SVG file %s: read %zu of %ld bytes\n", 
                                                 iconPath, bytesRead, fileSize);
                                    hal_psram_free(iconData);
                                }
                            } else {
                                Serial.printf("  Failed to allocate %ld bytes for SVG %s\n", fileSize, iconPath);
                                fclose(iconFile);
                            }
                        } else {
                            Serial.printf("  Invalid SVG file size %ld for %s\n", fileSize, iconPath);
                            fclose(iconFile);
                        }
                    } else {
                        Serial.printf("  Failed to open SVG file: %s\n", iconPath);
                    }
                } else {
                    Serial.printf("  Failed to get SVG path for icon code: %s\n", hourlyIcons[i]);
                }
            }
            
            // Display temperature below icon
            int16_t tempY = iconY + 150;  // 128px icon + 22px gap (increased for larger font)
            char tempBuf[16];
            snprintf(tempBuf, sizeof(tempBuf), "%.0f°", hourlyTemps[i]);
            ttf.drawTextAlignedOutlined(itemX, tempY, tempBuf, hourlyTempFontSize,
                                        EL133UF1_WHITE, EL133UF1_BLACK,
                                        ALIGN_CENTER, ALIGN_MIDDLE, 1);
            
            // Display description below temperature (with text wrapping)
            // Calculate available width for description (slightly less than item width for margin)
            int16_t descMaxWidth = itemWidth - 10;  // 10px margin on each side
            const char* descText = hourlyDescs[i];
            
            // Use existing wrapText function from TextPlacementAnalyzer
            static TextPlacementAnalyzer textWrapper;  // Static instance for reuse
            char wrappedDesc[256];  // Increased buffer for larger text (was 128)
            int numLines = 0;
            textWrapper.wrapText(&ttf, descText, hourlyDescFontSize, descMaxWidth, 
                                 wrappedDesc, sizeof(wrappedDesc), &numLines);
            
            // Calculate Y position for centered multi-line text
            // Draw multi-line text by splitting on newlines and drawing each line centered
            // This ensures proper centering of each line individually
            if (numLines > 0) {
                int16_t lineHeight = ttf.getTextHeight(hourlyDescFontSize);
                int16_t descY = tempY + 70;  // Start position (gap from temperature)
                
                // descY is the top of the text block
                // For ALIGN_MIDDLE, Y position should be the middle of each line
                // First line middle = descY + lineHeight/2
                int16_t lineSpacing = lineHeight + (lineHeight / 4);  // Line height + gap between lines
                
                const char* lineStart = wrappedDesc;
                int currentLine = 0;
                while (lineStart && *lineStart && currentLine < numLines) {
                    // Find the end of current line (newline or end of string)
                    const char* lineEnd = strchr(lineStart, '\n');
                    if (!lineEnd) {
                        lineEnd = lineStart + strlen(lineStart);
                    }
                    
                    // Extract line (temporarily null-terminate)
                    size_t lineLen = lineEnd - lineStart;
                    char lineBuf[128];
                    if (lineLen < sizeof(lineBuf) - 1) {
                        memcpy(lineBuf, lineStart, lineLen);
                        lineBuf[lineLen] = '\0';
                        
                        // Calculate Y position for this line's middle (for ALIGN_MIDDLE)
                        int16_t lineY = descY + (lineHeight / 2) + (currentLine * lineSpacing);
                        ttf.drawTextAlignedOutlined(itemX, lineY, lineBuf, hourlyDescFontSize,
                                                    EL133UF1_WHITE, EL133UF1_BLACK,
                                                    ALIGN_CENTER, ALIGN_MIDDLE, 1);
                    }
                    
                    // Move to next line
                    if (*lineEnd == '\n') {
                        lineStart = lineEnd + 1;
                    } else {
                        lineStart = nullptr;
                    }
                    currentLine++;
                }
            }
        }
    }
    
    
    // Update display
    Serial.println("Updating display (e-ink refresh - this will take 20-30 seconds)...");
    display.update();
    Serial.println("Display updated");
    
    // Free PSRAM allocations
    hal_psram_free(hourlyTemps);
    hal_psram_free(hourlyDescs);
    hal_psram_free(hourlyTimestamps);
    hal_psram_free(hourlyIcons);
    
    // No icon data to free (SVGs are loaded on-demand from LittleFS and freed immediately after rendering)
    
    return true;
}

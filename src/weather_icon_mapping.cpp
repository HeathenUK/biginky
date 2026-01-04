/**
 * @file weather_icon_mapping.cpp
 * @brief Mapping implementation for OpenWeatherMap icon codes to PNG files
 */

#include "weather_icon_mapping.h"
#include <string.h>

/**
 * Mapping of OpenWeatherMap icon codes to PNG filenames
 * Format: icon_code -> png_filename (without /littlefs/weather-png/ prefix)
 * PNG files (128x128) are stored in /littlefs/weather-png/ directory
 * 
 * OpenWeatherMap codes:
 * - 01d/01n: Clear sky
 * - 02d/02n: Few clouds (11-25%)
 * - 03d/03n: Scattered clouds (25-50%)
 * - 04d/04n: Broken/Overcast clouds (51-100%)
 * - 09d/09n: Shower rain
 * - 10d/10n: Rain
 * - 11d/11n: Thunderstorm
 * - 13d/13n: Snow
 * - 50d/50n: Mist/Fog
 */
struct IconMapping {
    const char* iconCode;
    const char* pngFile;
};

static const IconMapping iconMappings[] = {
    // Clear sky
    {"01d", "clear-day.png"},
    {"01n", "clear-night.png"},
    
    // Few clouds (11-25% cloudiness) - partly cloudy
    {"02d", "partly-cloudy-day.png"},
    {"02n", "partly-cloudy-night.png"},
    
    // Scattered clouds (25-50% cloudiness) - partly cloudy
    {"03d", "partly-cloudy-day.png"},
    {"03n", "partly-cloudy-night.png"},
    
    // Broken/Overcast clouds (51-100% cloudiness)
    {"04d", "overcast-day.png"},
    {"04n", "overcast-night.png"},
    
    // Shower rain (light rain intensity)
    {"09d", "drizzle.png"},
    {"09n", "drizzle.png"},
    
    // Rain
    {"10d", "rain.png"},
    {"10n", "rain.png"},
    
    // Thunderstorm
    {"11d", "thunderstorms-day.png"},
    {"11n", "thunderstorms-night.png"},
    
    // Snow
    {"13d", "snow.png"},
    {"13n", "snow.png"},
    
    // Mist/Fog
    {"50d", "fog-day.png"},
    {"50n", "fog-night.png"},
    
    {nullptr, nullptr}  // Sentinel
};

// Additional fallback mappings for edge cases
static const IconMapping fallbackMappings[] = {
    // Generic weather types
    {"cloudy", "cloudy.png"},
    {"fog", "fog.png"},
    {"mist", "mist.png"},
    {"haze", "haze.png"},
    {"haze-day", "haze-day.png"},
    {"haze-night", "haze-night.png"},
    {"dust", "dust.png"},
    {"dust-day", "dust-day.png"},
    {"dust-night", "dust-night.png"},
    {"sleet", "sleet.png"},
    {"hail", "hail.png"},
    {"tornado", "tornado.png"},
    {"hurricane", "hurricane.png"},
    {"smoke", "smoke.png"},
    
    {nullptr, nullptr}  // Sentinel
};

bool getWeatherIconPath(const char* iconCode, char* iconPath, size_t iconPathSize) {
    if (iconCode == nullptr || iconCode[0] == '\0' || iconPath == nullptr || iconPathSize < 20) {
        return false;
    }
    
    // Search primary mappings
    for (int i = 0; iconMappings[i].iconCode != nullptr; i++) {
        if (strcmp(iconCode, iconMappings[i].iconCode) == 0) {
            snprintf(iconPath, iconPathSize, "/littlefs/weather-png/%s", iconMappings[i].pngFile);
            return true;
        }
    }
    
    // Try fallback mappings
    for (int i = 0; fallbackMappings[i].iconCode != nullptr; i++) {
        if (strcmp(iconCode, fallbackMappings[i].iconCode) == 0) {
            snprintf(iconPath, iconPathSize, "/littlefs/weather-png/%s", fallbackMappings[i].pngFile);
            return true;
        }
    }
    
    // Default fallback: try to infer from icon code
    // If it ends with 'd', try clear-day or partly-cloudy-day
    // If it ends with 'n', try clear-night or partly-cloudy-night
    size_t len = strlen(iconCode);
    if (len >= 2) {
        char lastChar = iconCode[len - 1];
        if (lastChar == 'd') {
            // Default to partly-cloudy-day for unknown day icons
            snprintf(iconPath, iconPathSize, "/littlefs/weather-png/partly-cloudy-day.png");
            return true;
        } else if (lastChar == 'n') {
            // Default to partly-cloudy-night for unknown night icons
            snprintf(iconPath, iconPathSize, "/littlefs/weather-png/partly-cloudy-night.png");
            return true;
        }
    }
    
    // Ultimate fallback
    snprintf(iconPath, iconPathSize, "/littlefs/weather-png/not-available.png");
    return true;  // Return true even for fallback so caller doesn't error
}

/**
 * @file weather_icon_mapping.cpp
 * @brief Mapping implementation for OpenWeatherMap icon codes to PNG files
 */

#include "weather_icon_mapping.h"
#include <string.h>

/**
 * Mapping of OpenWeatherMap icon codes to SVG filenames
 * Format: icon_code -> svg_filename (without /littlefs/weather-svg/ prefix)
 * SVG files are stored in /littlefs/weather-svg/ directory
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
    const char* svgFile;
};

static const IconMapping iconMappings[] = {
    // Clear sky
    {"01d", "clear-day.svg"},
    {"01n", "clear-night.svg"},
    
    // Few clouds (11-25% cloudiness) - partly cloudy
    {"02d", "partly-cloudy-day.svg"},
    {"02n", "partly-cloudy-night.svg"},
    
    // Scattered clouds (25-50% cloudiness) - partly cloudy
    {"03d", "partly-cloudy-day.svg"},
    {"03n", "partly-cloudy-night.svg"},
    
    // Broken/Overcast clouds (51-100% cloudiness)
    {"04d", "overcast-day.svg"},
    {"04n", "overcast-night.svg"},
    
    // Shower rain (light rain intensity)
    {"09d", "drizzle.svg"},
    {"09n", "drizzle.svg"},
    
    // Rain
    {"10d", "rain.svg"},
    {"10n", "rain.svg"},
    
    // Thunderstorm
    {"11d", "thunderstorms-day.svg"},
    {"11n", "thunderstorms-night.svg"},
    
    // Snow
    {"13d", "snow.svg"},
    {"13n", "snow.svg"},
    
    // Mist/Fog
    {"50d", "fog-day.svg"},
    {"50n", "fog-night.svg"},
    
    {nullptr, nullptr}  // Sentinel
};

// Additional fallback mappings for edge cases
static const IconMapping fallbackMappings[] = {
    // Generic weather types
    {"cloudy", "cloudy.svg"},
    {"fog", "fog.svg"},
    {"mist", "mist.svg"},
    {"haze", "haze.svg"},
    {"haze-day", "haze-day.svg"},
    {"haze-night", "haze-night.svg"},
    {"dust", "dust.svg"},
    {"dust-day", "dust-day.svg"},
    {"dust-night", "dust-night.svg"},
    {"sleet", "sleet.svg"},
    {"hail", "hail.svg"},
    {"tornado", "tornado.svg"},
    {"hurricane", "hurricane.svg"},
    {"smoke", "smoke.svg"},
    
    {nullptr, nullptr}  // Sentinel
};

bool getWeatherIconPath(const char* iconCode, char* iconPath, size_t iconPathSize) {
    if (iconCode == nullptr || iconCode[0] == '\0' || iconPath == nullptr || iconPathSize < 20) {
        return false;
    }
    
    // Search primary mappings
    for (int i = 0; iconMappings[i].iconCode != nullptr; i++) {
        if (strcmp(iconCode, iconMappings[i].iconCode) == 0) {
            snprintf(iconPath, iconPathSize, "/littlefs/weather-svg/%s", iconMappings[i].svgFile);
            return true;
        }
    }
    
    // Try fallback mappings
    for (int i = 0; fallbackMappings[i].iconCode != nullptr; i++) {
        if (strcmp(iconCode, fallbackMappings[i].iconCode) == 0) {
            snprintf(iconPath, iconPathSize, "/littlefs/weather-svg/%s", fallbackMappings[i].svgFile);
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
            snprintf(iconPath, iconPathSize, "/littlefs/weather-svg/partly-cloudy-day.svg");
            return true;
        } else if (lastChar == 'n') {
            // Default to partly-cloudy-night for unknown night icons
            snprintf(iconPath, iconPathSize, "/littlefs/weather-svg/partly-cloudy-night.svg");
            return true;
        }
    }
    
    // Ultimate fallback
    snprintf(iconPath, iconPathSize, "/littlefs/weather-svg/not-available.svg");
    return true;  // Return true even for fallback so caller doesn't error
}

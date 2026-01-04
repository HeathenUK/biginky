/**
 * @file weather_background_mapping.cpp
 * @brief Mapping implementation for OpenWeatherMap icon codes to background PNG files
 */

#include "weather_background_mapping.h"
#include <string.h>

/**
 * Mapping of OpenWeatherMap icon codes to background PNG filenames
 * Format: icon_code -> bg_filename (without /littlefs/weather-bg/ prefix)
 * Background images are fullscreen and stored in /littlefs/weather-bg/ directory
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
struct BackgroundMapping {
    const char* iconCode;
    const char* bgFile;
};

static const BackgroundMapping bgMappings[] = {
    // Thunderstorm
    {"11d", "01_thunderstorm.png"},
    {"11n", "01_thunderstorm.png"},
    
    // Drizzle (light rain)
    {"09d", "02_drizzle.png"},
    {"09n", "02_drizzle.png"},
    
    // Rain
    {"10d", "03_steady_rain.png"},  // Default to steady rain
    {"10n", "03_steady_rain.png"},
    
    // Heavy rain (weather ID 502, 503, 504 map to heavy rain)
    // These will need to be handled by weather ID mapping if we have it,
    // but for now we'll use icon code 10d/10n for all rain
    
    // Freezing rain (weather ID 511)
    // Will need weather ID to distinguish from regular rain
    
    // Light snow
    {"13d", "06_light_snow.png"},
    {"13n", "06_light_snow.png"},
    
    // Snow (moderate)
    // Weather ID 600 maps to light snow, 601 to snow
    // For now, use icon 13d/13n for light snow
    
    // Heavy snow
    // Weather ID 602 maps to heavy snow
    // Will need weather ID to distinguish
    
    // Mist/Haze
    {"50d", "09_mist_haze.png"},
    {"50n", "09_mist_haze.png"},
    
    // Fog
    // Weather ID 741 maps to fog, but icon code 50d/50n covers mist/fog/haze
    // Use fog for 50d/50n when weather ID indicates fog specifically
    // For now, mist_haze covers both
    
    // Dust/Sand (weather ID 731, 751, 761)
    // Icon codes 50d/50n or special codes - need weather ID
    
    // Squalls (weather ID 771)
    // Special condition - need weather ID
    
    // Clear sky
    {"01d", "13_clear.png"},
    {"01n", "13_clear.png"},
    
    // Few clouds (11-25% cloudiness)
    {"02d", "14_few_clouds.png"},
    {"02n", "14_few_clouds.png"},
    
    // Scattered clouds (25-50% cloudiness) - use few clouds
    {"03d", "14_few_clouds.png"},
    {"03n", "14_few_clouds.png"},
    
    // Broken/Overcast clouds (51-100% cloudiness)
    {"04d", "15_overcast.png"},
    {"04n", "15_overcast.png"},
    
    {nullptr, nullptr}  // Sentinel
};

// Additional mappings for weather IDs (OpenWeatherMap weather condition codes)
// These take priority over icon code mappings when available
struct WeatherIdMapping {
    int weatherId;
    const char* bgFile;
};

// OpenWeatherMap weather condition IDs:
// 200-232: Thunderstorm (various intensities)
// 300-321: Drizzle
// 500-504: Rain (light to extreme)
// 511: Freezing rain
// 520-531: Shower rain
// 600-602: Snow (light to heavy)
// 611-616: Sleet
// 620-622: Shower snow
// 701: Mist
// 711: Smoke
// 721: Haze
// 731: Sand/dust whirls
// 741: Fog
// 751: Sand
// 761: Dust
// 762: Volcanic ash
// 771: Squalls
// 781: Tornado
static const WeatherIdMapping weatherIdMappings[] = {
    // Thunderstorm
    {200, "01_thunderstorm.png"},  // Thunderstorm with light rain
    {201, "01_thunderstorm.png"},  // Thunderstorm with rain
    {202, "01_thunderstorm.png"},  // Thunderstorm with heavy rain
    {210, "01_thunderstorm.png"},  // Light thunderstorm
    {211, "01_thunderstorm.png"},  // Thunderstorm
    {212, "01_thunderstorm.png"},  // Heavy thunderstorm
    {221, "01_thunderstorm.png"},  // Ragged thunderstorm
    {230, "01_thunderstorm.png"},  // Thunderstorm with light drizzle
    {231, "01_thunderstorm.png"},  // Thunderstorm with drizzle
    {232, "01_thunderstorm.png"},  // Thunderstorm with heavy drizzle
    
    // Drizzle
    {300, "02_drizzle.png"},  // Light intensity drizzle
    {301, "02_drizzle.png"},  // Drizzle
    {302, "02_drizzle.png"},  // Heavy intensity drizzle
    {310, "02_drizzle.png"},  // Light intensity drizzle rain
    {311, "02_drizzle.png"},  // Drizzle rain
    {312, "02_drizzle.png"},  // Heavy intensity drizzle rain
    {313, "02_drizzle.png"},  // Shower rain and drizzle
    {314, "02_drizzle.png"},  // Heavy shower rain and drizzle
    {321, "02_drizzle.png"},  // Shower drizzle
    
    // Rain
    {500, "02_drizzle.png"},  // Light rain (more like drizzle)
    {501, "03_steady_rain.png"},  // Moderate rain
    {502, "04_heavy_rain.png"},  // Heavy intensity rain
    {503, "04_heavy_rain.png"},  // Very heavy rain
    {504, "04_heavy_rain.png"},  // Extreme rain
    {511, "05_freezing_rain.png"},  // Freezing rain
    {520, "02_drizzle.png"},  // Light intensity shower rain
    {521, "03_steady_rain.png"},  // Shower rain
    {522, "04_heavy_rain.png"},  // Heavy intensity shower rain
    {531, "04_heavy_rain.png"},  // Ragged shower rain
    
    // Snow
    {600, "06_light_snow.png"},  // Light snow
    {601, "07_snow.png"},  // Snow
    {602, "08_heavy_snow.png"},  // Heavy snow
    {611, "05_freezing_rain.png"},  // Sleet (freezing rain/snow mix)
    {612, "05_freezing_rain.png"},  // Light shower sleet
    {613, "05_freezing_rain.png"},  // Shower sleet
    {615, "07_snow.png"},  // Light rain and snow
    {616, "07_snow.png"},  // Rain and snow
    {620, "06_light_snow.png"},  // Light shower snow
    {621, "07_snow.png"},  // Shower snow
    {622, "08_heavy_snow.png"},  // Heavy shower snow
    
    // Atmosphere
    {701, "09_mist_haze.png"},  // Mist
    {711, "11_dust_sand.png"},  // Smoke
    {721, "09_mist_haze.png"},  // Haze
    {731, "11_dust_sand.png"},  // Sand/dust whirls
    {741, "10_fog.png"},  // Fog
    {751, "11_dust_sand.png"},  // Sand
    {761, "11_dust_sand.png"},  // Dust
    {762, "11_dust_sand.png"},  // Volcanic ash
    {771, "12_squalls.png"},  // Squalls
    {781, "01_thunderstorm.png"},  // Tornado (use thunderstorm background)
    
    {-1, nullptr}  // Sentinel
};

bool getWeatherBackgroundPath(const char* iconCode, char* bgPath, size_t bgPathSize, int weatherId) {
    if (iconCode == nullptr || iconCode[0] == '\0' || bgPath == nullptr || bgPathSize < 30) {
        return false;
    }
    
    // First, try weather ID mapping (more specific)
    if (weatherId >= 200) {
        for (int i = 0; weatherIdMappings[i].weatherId != -1; i++) {
            if (weatherIdMappings[i].weatherId == weatherId) {
                snprintf(bgPath, bgPathSize, "/littlefs/weather-bg/%s", weatherIdMappings[i].bgFile);
                return true;
            }
        }
    }
    
    // Fallback to icon code mapping
    for (int i = 0; bgMappings[i].iconCode != nullptr; i++) {
        if (strcmp(iconCode, bgMappings[i].iconCode) == 0) {
            snprintf(bgPath, bgPathSize, "/littlefs/weather-bg/%s", bgMappings[i].bgFile);
            return true;
        }
    }
    
    // Default fallback: clear sky
    snprintf(bgPath, bgPathSize, "/littlefs/weather-bg/13_clear.png");
    return true;
}

/**
 * @file weather_background_mapping.h
 * @brief Mapping between OpenWeatherMap icon codes and background PNG filenames
 * 
 * OpenWeatherMap icon codes are in the format: [number][d|n]
 * Examples: 01d (clear sky day), 01n (clear sky night), 10d (rain day), etc.
 * Background images are fullscreen and stored in /littlefs/weather-bg/ directory
 */

#ifndef WEATHER_BACKGROUND_MAPPING_H
#define WEATHER_BACKGROUND_MAPPING_H

#include <Arduino.h>

/**
 * @brief Get background PNG filename for an OpenWeatherMap icon code and/or weather ID
 * @param iconCode OpenWeatherMap icon code (e.g., "01d", "02n", "10d")
 * @param bgPath Output buffer for the full path (e.g., "/littlefs/weather-bg/13_clear.png")
 * @param bgPathSize Size of bgPath buffer
 * @param weatherId Optional OpenWeatherMap weather condition ID (e.g., 200-782). If -1, only icon code is used.
 * @return true if mapping found, false otherwise
 */
bool getWeatherBackgroundPath(const char* iconCode, char* bgPath, size_t bgPathSize, int weatherId = -1);

#endif // WEATHER_BACKGROUND_MAPPING_H

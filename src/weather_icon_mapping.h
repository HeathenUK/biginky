/**
 * @file weather_icon_mapping.h
 * @brief Mapping between OpenWeatherMap icon codes and PNG icon filenames
 * 
 * OpenWeatherMap icon codes are in the format: [number][d|n]
 * Examples: 01d (clear sky day), 01n (clear sky night), 10d (rain day), etc.
 * PNG icons are 128x128 pixels and stored in /littlefs/weather-png/ directory
 */

#ifndef WEATHER_ICON_MAPPING_H
#define WEATHER_ICON_MAPPING_H

#include <Arduino.h>

/**
 * @brief Get PNG filename for an OpenWeatherMap icon code
 * @param iconCode OpenWeatherMap icon code (e.g., "01d", "02n", "10d")
 * @param iconPath Output buffer for the full path (e.g., "/littlefs/weather-png/clear-day.png")
 * @param iconPathSize Size of iconPath buffer
 * @return true if mapping found, false otherwise
 */
bool getWeatherIconPath(const char* iconCode, char* iconPath, size_t iconPathSize);

#endif // WEATHER_ICON_MAPPING_H

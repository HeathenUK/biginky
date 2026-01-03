/**
 * @file schedule_manager.h
 * @brief Schedule management for detailed scene scheduling
 * 
 * Manages the detailed schedule system that allows scheduling different scenes
 * (media mapping, weather, images, etc.) at specific minutes within each hour.
 * 
 * Extracted from main.cpp for better code organization.
 */

#ifndef SCHEDULE_MANAGER_H
#define SCHEDULE_MANAGER_H

#include <Arduino.h>
#include <vector>

// Schedule action types (returned by getScheduleAction)
enum class ScheduleAction {
    SCHEDULE_DISABLED,      // Hour is disabled - sleep until next enabled hour
    SCHEDULE_ENABLED,       // Hour is enabled - proceed with normal operations (media mapping)
    SCHEDULE_NTP_RESYNC,    // Special action: resync NTP (e.g., at 30 minutes past hour)
    SCHEDULE_HAPPY_WEATHER, // Special action: display Happy weather scene at :30
    SCHEDULE_IMAGE,         // Display specific image (parameter: filename)
    SCHEDULE_WEATHER_PLACE  // Display weather for specific place (parameter: location)
};

// Scene types for schedule slots
enum class SceneType {
    MEDIA,          // Next Media Mapping (no parameter)
    WEATHER,        // Happy Places Weather (no parameter)
    IMAGE,          // Show specific image (parameter: filename)
    WEATHER_PLACE   // Weather for specific place (parameter: location)
};

// Schedule slot: minute + scene type + optional parameter
struct ScheduleSlot {
    int minute;          // 0-59
    SceneType scene;     // Scene type
    String parameter;    // Optional parameter (empty if not needed)
};

// Hour schedule entry: enabled flag + list of time slots
struct HourSchedule {
    bool enabled;                       // Hour enabled/disabled
    std::vector<ScheduleSlot> slots;    // Time slots within the hour
};

/**
 * Initialize default schedule (media at :00, weather at :30 for all enabled hours)
 * This matches the current hardcoded behavior
 * Requires g_hour_schedule array (external dependency)
 */
void initializeDefaultSchedule();

/**
 * Load detailed schedule from NVS (stored as JSON)
 * Falls back to default schedule if not found or invalid
 */
void detailedScheduleLoadFromNVS();

/**
 * Save detailed schedule to NVS (stored as JSON)
 */
void detailedScheduleSaveToNVS();

/**
 * Get schedule action for a given hour and minute
 * Uses the detailed schedule to lookup scene for the current time
 * Returns ScheduleAction enum value
 */
ScheduleAction getScheduleAction(int hour, int minute);

/**
 * Check if there's an explicit schedule slot at the given hour and minute
 * Returns true if a slot exists, false otherwise
 */
bool hasScheduleSlot(int hour, int minute);

/**
 * Get the parameter for a schedule slot at the given hour and minute
 * Returns empty string if no slot exists or slot has no parameter
 */
String getScheduleSlotParameter(int hour, int minute);

/**
 * Check if an hour is enabled in the detailed schedule
 * Returns true if the hour is enabled, false if disabled
 * This is the single source of truth for hour enable/disable state
 */
bool isHourEnabledInSchedule(int hour);

/**
 * Get detailed schedule as JSON string (for API)
 * Returns JSON string representation of the entire schedule
 */
String getDetailedScheduleJSON();

/**
 * Update detailed schedule from JSON string (from API)
 * Validates the JSON before applying changes
 * Returns true on success, false on failure
 */
bool updateDetailedScheduleFromJSON(const String& json);

#endif // SCHEDULE_MANAGER_H

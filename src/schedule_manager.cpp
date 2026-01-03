/**
 * @file schedule_manager.cpp
 * @brief Schedule management implementation
 * 
 * Extracted from main.cpp for better code organization.
 */

#include "schedule_manager.h"
#include "nvs_guard.h"
#include "nvs_manager.h"
#include "cJSON.h"
#include <Preferences.h>
#include <Arduino.h>

// External dependencies (defined in main.cpp or nvs_manager)
extern bool g_hour_schedule[24];
extern bool g_is_cold_boot;
extern Preferences detailedSchedulePrefs;

// Global schedule: 24 hour entries (index 0-23)
static HourSchedule g_detailed_schedule[24];

/**
 * Initialize default schedule (media at :00, weather at :30 for all enabled hours)
 * This matches the current hardcoded behavior
 */
void initializeDefaultSchedule() {
    for (int h = 0; h < 24; h++) {
        g_detailed_schedule[h].enabled = g_hour_schedule[h];  // Use hour schedule enabled state
        g_detailed_schedule[h].slots.clear();
        
        if (g_detailed_schedule[h].enabled) {
            // Add default slots: media at :00, weather at :30
            ScheduleSlot slot0;
            slot0.minute = 0;
            slot0.scene = SceneType::MEDIA;
            slot0.parameter = "";
            g_detailed_schedule[h].slots.push_back(slot0);
            
            ScheduleSlot slot30;
            slot30.minute = 30;
            slot30.scene = SceneType::WEATHER;
            slot30.parameter = "";
            g_detailed_schedule[h].slots.push_back(slot30);
        }
    }
}

/**
 * Load detailed schedule from NVS (stored as JSON)
 * Falls back to default schedule if not found or invalid
 */
void detailedScheduleLoadFromNVS() {
    // Initialize with defaults first
    initializeDefaultSchedule();
    
    NVSGuard guard(detailedSchedulePrefs, "dschedule", true);  // Read-only
    if (!guard.isOpen()) {
        // No schedule in NVS - use defaults (already initialized above)
        if (g_is_cold_boot) {
            Serial.println("INFO: No detailed schedule in NVS - using default (media at :00, weather at :30)");
        }
        return;
    }
    
    // Load schedule JSON string
    String scheduleJson = guard.get().getString("schedule", "");
    if (scheduleJson.length() == 0) {
        // Empty string - use defaults
        if (g_is_cold_boot) {
            Serial.println("INFO: Detailed schedule empty in NVS - using default");
        }
        return;
    }
    
    // Parse JSON using cJSON
    cJSON* root = cJSON_Parse(scheduleJson.c_str());
    if (!root) {
        Serial.println("WARNING: Failed to parse detailed schedule JSON - using default");
        return;
    }
    
    cJSON* scheduleArray = cJSON_GetObjectItem(root, "schedule");
    if (!scheduleArray || !cJSON_IsArray(scheduleArray)) {
        Serial.println("WARNING: Invalid detailed schedule format - using default");
        cJSON_Delete(root);
        return;
    }
    
    int arraySize = cJSON_GetArraySize(scheduleArray);
    if (arraySize != 24) {
        Serial.printf("WARNING: Detailed schedule array size %d != 24 - using default\n", arraySize);
        cJSON_Delete(root);
        return;
    }
    
    // Parse each hour entry
    for (int h = 0; h < 24; h++) {
        cJSON* hourObj = cJSON_GetArrayItem(scheduleArray, h);
        if (!hourObj || !cJSON_IsObject(hourObj)) {
            Serial.printf("WARNING: Invalid hour %d in schedule - using default\n", h);
            continue;
        }
        
        // Get enabled flag
        cJSON* enabledItem = cJSON_GetObjectItem(hourObj, "enabled");
        g_detailed_schedule[h].enabled = (enabledItem && cJSON_IsBool(enabledItem)) ? cJSON_IsTrue(enabledItem) : true;
        
        // Get slots array
        g_detailed_schedule[h].slots.clear();
        cJSON* slotsArray = cJSON_GetObjectItem(hourObj, "slots");
        if (slotsArray && cJSON_IsArray(slotsArray)) {
            int slotsSize = cJSON_GetArraySize(slotsArray);
            for (int s = 0; s < slotsSize; s++) {
                cJSON* slotObj = cJSON_GetArrayItem(slotsArray, s);
                if (!slotObj || !cJSON_IsObject(slotObj)) continue;
                
                ScheduleSlot slot;
                cJSON* minuteItem = cJSON_GetObjectItem(slotObj, "minute");
                cJSON* sceneItem = cJSON_GetObjectItem(slotObj, "scene");
                cJSON* paramItem = cJSON_GetObjectItem(slotObj, "parameter");
                
                if (minuteItem && cJSON_IsNumber(minuteItem)) {
                    slot.minute = (int)cJSON_GetNumberValue(minuteItem);
                    if (slot.minute < 0 || slot.minute >= 60) continue;  // Invalid minute
                } else {
                    continue;  // Minute is required
                }
                
                if (sceneItem && cJSON_IsString(sceneItem)) {
                    String sceneStr = String(cJSON_GetStringValue(sceneItem));
                    if (sceneStr == "media") {
                        slot.scene = SceneType::MEDIA;
                    } else if (sceneStr == "weather") {
                        slot.scene = SceneType::WEATHER;
                    } else if (sceneStr == "image") {
                        slot.scene = SceneType::IMAGE;
                    } else if (sceneStr == "weather_place") {
                        slot.scene = SceneType::WEATHER_PLACE;
                    } else {
                        continue;  // Invalid scene type
                    }
                } else {
                    continue;  // Scene is required
                }
                
                slot.parameter = "";
                if (paramItem && cJSON_IsString(paramItem)) {
                    slot.parameter = String(cJSON_GetStringValue(paramItem));
                }
                
                g_detailed_schedule[h].slots.push_back(slot);
            }
        }
    }
    
    cJSON_Delete(root);
    
    if (g_is_cold_boot) {
        Serial.println("Loaded detailed schedule from NVS");
    }
}

/**
 * Save detailed schedule to NVS (as JSON)
 */
void detailedScheduleSaveToNVS() {
    NVSGuard guard(detailedSchedulePrefs, "dschedule", false);  // Read-write
    if (!guard.isOpen()) {
        Serial.println("WARNING: Failed to open NVS for saving detailed schedule");
        return;
    }
    
    // Build JSON using cJSON
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        Serial.println("ERROR: Failed to create JSON object for schedule");
        return;
    }
    
    cJSON* scheduleArray = cJSON_CreateArray();
    if (!scheduleArray) {
        Serial.println("ERROR: Failed to create schedule array");
        cJSON_Delete(root);
        return;
    }
    
    cJSON_AddItemToObject(root, "schedule", scheduleArray);
    
    // Add each hour entry
    for (int h = 0; h < 24; h++) {
        cJSON* hourObj = cJSON_CreateObject();
        if (!hourObj) continue;
        
        cJSON_AddBoolToObject(hourObj, "enabled", g_detailed_schedule[h].enabled);
        
        cJSON* slotsArray = cJSON_CreateArray();
        if (slotsArray) {
            for (size_t s = 0; s < g_detailed_schedule[h].slots.size(); s++) {
                const ScheduleSlot& slot = g_detailed_schedule[h].slots[s];
                cJSON* slotObj = cJSON_CreateObject();
                if (!slotObj) continue;
                
                cJSON_AddNumberToObject(slotObj, "minute", slot.minute);
                
                const char* sceneStr = "";
                switch (slot.scene) {
                    case SceneType::MEDIA: sceneStr = "media"; break;
                    case SceneType::WEATHER: sceneStr = "weather"; break;
                    case SceneType::IMAGE: sceneStr = "image"; break;
                    case SceneType::WEATHER_PLACE: sceneStr = "weather_place"; break;
                }
                cJSON_AddStringToObject(slotObj, "scene", sceneStr);
                
                if (slot.parameter.length() > 0) {
                    cJSON_AddStringToObject(slotObj, "parameter", slot.parameter.c_str());
                }
                
                cJSON_AddItemToArray(slotsArray, slotObj);
            }
        }
        cJSON_AddItemToObject(hourObj, "slots", slotsArray);
        cJSON_AddItemToArray(scheduleArray, hourObj);
    }
    
    // Convert to string
    char* jsonStr = cJSON_Print(root);
    if (jsonStr) {
        guard.get().putString("schedule", String(jsonStr));
        free(jsonStr);
        Serial.println("Saved detailed schedule to NVS");
    } else {
        Serial.println("ERROR: Failed to convert schedule to JSON string");
    }
    
    cJSON_Delete(root);
}

/**
 * Check if there's an explicit schedule slot at the given hour and minute
 */
bool hasScheduleSlot(int hour, int minute) {
    // Check if there's an explicit slot at this minute
    if (hour < 0 || hour >= 24) {
        return false;
    }
    
    for (size_t i = 0; i < g_detailed_schedule[hour].slots.size(); i++) {
        if (g_detailed_schedule[hour].slots[i].minute == minute) {
            return true;
        }
    }
    
    return false;
}

/**
 * Get the parameter for a schedule slot at the given hour and minute
 */
String getScheduleSlotParameter(int hour, int minute) {
    if (hour < 0 || hour >= 24) {
        return "";
    }
    
    for (size_t i = 0; i < g_detailed_schedule[hour].slots.size(); i++) {
        if (g_detailed_schedule[hour].slots[i].minute == minute) {
            return g_detailed_schedule[hour].slots[i].parameter;
        }
    }
    
    return "";
}

/**
 * Check if an hour is enabled in the detailed schedule
 * This is the single source of truth for hour enable/disable state
 */
bool isHourEnabledInSchedule(int hour) {
    if (hour < 0 || hour >= 24) {
        return true;  // Invalid hour, default to enabled
    }
    return g_detailed_schedule[hour].enabled;
}

/**
 * Get schedule action for a given hour and minute
 * Uses the detailed schedule to lookup scene for the current time
 */
ScheduleAction getScheduleAction(int hour, int minute) {
    // First check if hour is disabled
    if (hour < 0 || hour >= 24) {
        return ScheduleAction::SCHEDULE_DISABLED;
    }
    
    if (!g_detailed_schedule[hour].enabled) {
        return ScheduleAction::SCHEDULE_DISABLED;
    }
    
    // Look up slot for this minute in the schedule
    for (size_t i = 0; i < g_detailed_schedule[hour].slots.size(); i++) {
        const ScheduleSlot& slot = g_detailed_schedule[hour].slots[i];
        if (slot.minute == minute) {
            // Found matching slot - convert scene type to action
            switch (slot.scene) {
                case SceneType::MEDIA:
                    return ScheduleAction::SCHEDULE_ENABLED;
                case SceneType::WEATHER:
                    return ScheduleAction::SCHEDULE_HAPPY_WEATHER;
                case SceneType::IMAGE:
                    return ScheduleAction::SCHEDULE_IMAGE;
                case SceneType::WEATHER_PLACE:
                    return ScheduleAction::SCHEDULE_WEATHER_PLACE;
                default:
                    return ScheduleAction::SCHEDULE_ENABLED;  // Default to media
            }
        }
    }
    
    // No slot found for this minute - default to enabled (media mapping)
    // This allows the schedule to be sparse (not every minute needs a slot)
    return ScheduleAction::SCHEDULE_ENABLED;
}

/**
 * Get detailed schedule as JSON string (for API)
 */
String getDetailedScheduleJSON() {
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return "{\"error\":\"Failed to create JSON object\"}";
    }
    
    cJSON* scheduleArray = cJSON_CreateArray();
    if (!scheduleArray) {
        cJSON_Delete(root);
        return "{\"error\":\"Failed to create schedule array\"}";
    }
    
    cJSON_AddItemToObject(root, "schedule", scheduleArray);
    
    // Add each hour entry
    for (int h = 0; h < 24; h++) {
        cJSON* hourObj = cJSON_CreateObject();
        if (!hourObj) continue;
        
        cJSON_AddBoolToObject(hourObj, "enabled", g_detailed_schedule[h].enabled);
        
        cJSON* slotsArray = cJSON_CreateArray();
        if (slotsArray) {
            for (size_t s = 0; s < g_detailed_schedule[h].slots.size(); s++) {
                const ScheduleSlot& slot = g_detailed_schedule[h].slots[s];
                cJSON* slotObj = cJSON_CreateObject();
                if (!slotObj) continue;
                
                cJSON_AddNumberToObject(slotObj, "minute", slot.minute);
                
                const char* sceneStr = "";
                switch (slot.scene) {
                    case SceneType::MEDIA: sceneStr = "media"; break;
                    case SceneType::WEATHER: sceneStr = "weather"; break;
                    case SceneType::IMAGE: sceneStr = "image"; break;
                    case SceneType::WEATHER_PLACE: sceneStr = "weather_place"; break;
                }
                cJSON_AddStringToObject(slotObj, "scene", sceneStr);
                
                if (slot.parameter.length() > 0) {
                    cJSON_AddStringToObject(slotObj, "parameter", slot.parameter.c_str());
                }
                
                cJSON_AddItemToArray(slotsArray, slotObj);
            }
        }
        cJSON_AddItemToObject(hourObj, "slots", slotsArray);
        cJSON_AddItemToArray(scheduleArray, hourObj);
    }
    
    char* jsonStr = cJSON_Print(root);
    String result = "";
    if (jsonStr) {
        result = String(jsonStr);
        free(jsonStr);
    } else {
        result = "{\"error\":\"Failed to convert to JSON string\"}";
    }
    
    cJSON_Delete(root);
    return result;
}

/**
 * Update detailed schedule from JSON string (from API)
 */
bool updateDetailedScheduleFromJSON(const String& json) {
    cJSON* root = cJSON_Parse(json.c_str());
    if (!root) {
        Serial.println("ERROR: Failed to parse schedule JSON");
        return false;
    }
    
    cJSON* scheduleArray = cJSON_GetObjectItem(root, "schedule");
    if (!scheduleArray || !cJSON_IsArray(scheduleArray)) {
        Serial.println("ERROR: Invalid schedule format - missing schedule array");
        cJSON_Delete(root);
        return false;
    }
    
    int arraySize = cJSON_GetArraySize(scheduleArray);
    if (arraySize != 24) {
        Serial.printf("ERROR: Schedule array size %d != 24\n", arraySize);
        cJSON_Delete(root);
        return false;
    }
    
    // Parse and validate each hour entry before applying
    HourSchedule tempSchedule[24];
    for (int h = 0; h < 24; h++) {
        cJSON* hourObj = cJSON_GetArrayItem(scheduleArray, h);
        if (!hourObj || !cJSON_IsObject(hourObj)) {
            Serial.printf("ERROR: Invalid hour %d in schedule\n", h);
            cJSON_Delete(root);
            return false;
        }
        
        cJSON* enabledItem = cJSON_GetObjectItem(hourObj, "enabled");
        tempSchedule[h].enabled = (enabledItem && cJSON_IsBool(enabledItem)) ? cJSON_IsTrue(enabledItem) : true;
        
        tempSchedule[h].slots.clear();
        cJSON* slotsArray = cJSON_GetObjectItem(hourObj, "slots");
        if (slotsArray && cJSON_IsArray(slotsArray)) {
            int slotsSize = cJSON_GetArraySize(slotsArray);
            for (int s = 0; s < slotsSize; s++) {
                cJSON* slotObj = cJSON_GetArrayItem(slotsArray, s);
                if (!slotObj || !cJSON_IsObject(slotObj)) continue;
                
                ScheduleSlot slot;
                cJSON* minuteItem = cJSON_GetObjectItem(slotObj, "minute");
                cJSON* sceneItem = cJSON_GetObjectItem(slotObj, "scene");
                cJSON* paramItem = cJSON_GetObjectItem(slotObj, "parameter");
                
                if (minuteItem && cJSON_IsNumber(minuteItem)) {
                    slot.minute = (int)cJSON_GetNumberValue(minuteItem);
                    if (slot.minute < 0 || slot.minute >= 60) {
                        Serial.printf("ERROR: Invalid minute %d in hour %d\n", slot.minute, h);
                        cJSON_Delete(root);
                        return false;
                    }
                } else {
                    Serial.printf("ERROR: Missing minute in hour %d slot %d\n", h, s);
                    cJSON_Delete(root);
                    return false;
                }
                
                if (sceneItem && cJSON_IsString(sceneItem)) {
                    String sceneStr = String(cJSON_GetStringValue(sceneItem));
                    if (sceneStr == "media") {
                        slot.scene = SceneType::MEDIA;
                    } else if (sceneStr == "weather") {
                        slot.scene = SceneType::WEATHER;
                    } else if (sceneStr == "image") {
                        slot.scene = SceneType::IMAGE;
                    } else if (sceneStr == "weather_place") {
                        slot.scene = SceneType::WEATHER_PLACE;
                    } else {
                        Serial.printf("ERROR: Invalid scene type '%s' in hour %d\n", sceneStr.c_str(), h);
                        cJSON_Delete(root);
                        return false;
                    }
                } else {
                    Serial.printf("ERROR: Missing scene in hour %d slot %d\n", h, s);
                    cJSON_Delete(root);
                    return false;
                }
                
                slot.parameter = "";
                if (paramItem && cJSON_IsString(paramItem)) {
                    slot.parameter = String(cJSON_GetStringValue(paramItem));
                }
                
                tempSchedule[h].slots.push_back(slot);
            }
        }
    }
    
    // All validation passed - apply to global schedule
    for (int h = 0; h < 24; h++) {
        g_detailed_schedule[h] = tempSchedule[h];
    }
    
    cJSON_Delete(root);
    
    // Save to NVS
    detailedScheduleSaveToNVS();
    
    Serial.println("Schedule updated from JSON and saved to NVS");
    return true;
}

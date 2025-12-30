/**
 * @file json_utils.h
 * @brief Utility functions for JSON field extraction using cJSON library
 * 
 * These functions provide JSON field extraction using the cJSON library.
 * cJSON is lightweight, well-maintained, and handles large payloads efficiently.
 * 
 * Usage:
 *   String text = extractJsonStringField(json, "text");
 *   String from = extractJsonStringField(json, "from");
 *   bool encrypted = extractJsonBoolField(json, "encrypted", false);
 *   int width = extractJsonIntField(json, "width", 0);
 */

#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <Arduino.h>
#include "cJSON.h"

/**
 * Extract a string field from JSON message using cJSON
 * @param json The JSON string to parse
 * @param fieldName The name of the field to extract (e.g., "text", "from", "command")
 * @return The field value, or empty string if not found
 */
static String extractJsonStringField(const String& json, const char* fieldName) {
    String result = "";
    
    // Only process JSON messages
    if (!json.startsWith("{")) {
        return result;
    }
    
    cJSON* root = cJSON_Parse(json.c_str());
    if (!root) {
        // cJSON_Parse failed, return empty string
        return result;
    }
    
    cJSON* item = cJSON_GetObjectItem(root, fieldName);
    if (item && cJSON_IsString(item)) {
        result = String(cJSON_GetStringValue(item));
    }
    
    cJSON_Delete(root);
    return result;
}

/**
 * Extract a boolean field from JSON message using cJSON
 * @param json The JSON string to parse
 * @param fieldName The name of the field to extract (e.g., "encrypted")
 * @param defaultValue Default value if field is not found
 * @return The field value, or defaultValue if not found
 */
static bool extractJsonBoolField(const String& json, const char* fieldName, bool defaultValue = false) {
    // Only process JSON messages
    if (!json.startsWith("{")) {
        return defaultValue;
    }
    
    cJSON* root = cJSON_Parse(json.c_str());
    if (!root) {
        return defaultValue;
    }
    
    cJSON* item = cJSON_GetObjectItem(root, fieldName);
    bool result = defaultValue;
    if (item && cJSON_IsBool(item)) {
        result = cJSON_IsTrue(item);
    }
    
    cJSON_Delete(root);
    return result;
}

/**
 * Extract an integer field from JSON message using cJSON
 * @param json The JSON string to parse
 * @param fieldName The name of the field to extract (e.g., "width", "height")
 * @param defaultValue Default value if field is not found
 * @return The field value, or defaultValue if not found
 */
static int extractJsonIntField(const String& json, const char* fieldName, int defaultValue = 0) {
    // Only process JSON messages
    if (!json.startsWith("{")) {
        return defaultValue;
    }
    
    cJSON* root = cJSON_Parse(json.c_str());
    if (!root) {
        return defaultValue;
    }
    
    cJSON* item = cJSON_GetObjectItem(root, fieldName);
    int result = defaultValue;
    if (item && cJSON_IsNumber(item)) {
        result = (int)cJSON_GetNumberValue(item);
    }
    
    cJSON_Delete(root);
    return result;
}

#endif // JSON_UTILS_H

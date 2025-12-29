/**
 * @file json_utils.h
 * @brief Utility functions for JSON field extraction (manual parsing for memory efficiency)
 * 
 * These functions provide lightweight JSON field extraction without requiring
 * full JSON parsing libraries, which is important for large messages (e.g., canvas_display)
 * that can be 400KB+ and would cause memory issues with full JSON parsing.
 * 
 * For small messages (<4KB), ArduinoJson is preferred and used in handleWebInterfaceCommand().
 * For large messages, these manual parsing functions are used.
 * 
 * Usage:
 *   String text = extractJsonStringField(json, "text");
 *   String from = extractJsonStringField(json, "from");
 *   bool encrypted = extractJsonBoolField(json, "encrypted", false);
 */

#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <Arduino.h>

/**
 * Extract a string field from JSON message using manual parsing
 * @param json The JSON string to parse
 * @param fieldName The name of the field to extract (e.g., "text", "from", "command")
 * @return The field value, or empty string if not found
 * 
 * Handles patterns like: "fieldName":"value"
 * Case-sensitive field name matching
 */
static String extractJsonStringField(const String& json, const char* fieldName) {
    String result = "";
    
    // Only process JSON messages
    if (!json.startsWith("{")) {
        return result;
    }
    
    // Build search pattern: "fieldName"
    String searchPattern = "\"";
    searchPattern += fieldName;
    searchPattern += "\"";
    
    // Look for "fieldName":"..." pattern
    int fieldStart = json.indexOf(searchPattern);
    if (fieldStart >= 0) {
        int colonPos = json.indexOf(':', fieldStart);
        if (colonPos >= 0) {
            int quoteStart = json.indexOf('"', colonPos);
            if (quoteStart >= 0) {
                int quoteEnd = json.indexOf('"', quoteStart + 1);
                if (quoteEnd >= 0) {
                    result = json.substring(quoteStart + 1, quoteEnd);
                    result.trim();
                }
            }
        }
    }
    
    return result;
}

/**
 * Extract a boolean field from JSON message using manual parsing
 * @param json The JSON string to parse
 * @param fieldName The name of the field to extract (e.g., "encrypted")
 * @param defaultValue Default value if field is not found
 * @return The field value, or defaultValue if not found
 * 
 * Handles patterns like: "fieldName":true or "fieldName":false
 * Case-sensitive field name matching
 */
static bool extractJsonBoolField(const String& json, const char* fieldName, bool defaultValue = false) {
    // Only process JSON messages
    if (!json.startsWith("{")) {
        return defaultValue;
    }
    
    // Build search pattern: "fieldName"
    String searchPattern = "\"";
    searchPattern += fieldName;
    searchPattern += "\"";
    
    // Look for "fieldName":true or "fieldName":false pattern
    int fieldStart = json.indexOf(searchPattern);
    if (fieldStart >= 0) {
        int colonPos = json.indexOf(':', fieldStart);
        if (colonPos >= 0) {
            // Skip whitespace after colon
            int valueStart = colonPos + 1;
            while (valueStart < json.length() && (json[valueStart] == ' ' || json[valueStart] == '\t')) {
                valueStart++;
            }
            
            // Check for "true"
            if (json.substring(valueStart, valueStart + 4) == "true") {
                return true;
            }
            // Check for "false"
            if (json.substring(valueStart, valueStart + 5) == "false") {
                return false;
            }
        }
    }
    
    return defaultValue;
}

/**
 * Extract an integer field from JSON message using manual parsing
 * @param json The JSON string to parse
 * @param fieldName The name of the field to extract (e.g., "width", "height")
 * @param defaultValue Default value if field is not found
 * @return The field value, or defaultValue if not found
 * 
 * Handles patterns like: "fieldName":123
 * Case-sensitive field name matching
 */
static int extractJsonIntField(const String& json, const char* fieldName, int defaultValue = 0) {
    // Only process JSON messages
    if (!json.startsWith("{")) {
        return defaultValue;
    }
    
    // Build search pattern: "fieldName"
    String searchPattern = "\"";
    searchPattern += fieldName;
    searchPattern += "\"";
    
    // Look for "fieldName":123 pattern
    int fieldStart = json.indexOf(searchPattern);
    if (fieldStart >= 0) {
        int colonPos = json.indexOf(':', fieldStart);
        if (colonPos >= 0) {
            // Skip whitespace after colon
            int valueStart = colonPos + 1;
            while (valueStart < json.length() && (json[valueStart] == ' ' || json[valueStart] == '\t')) {
                valueStart++;
            }
            
            // Find end of number (comma, }, or whitespace)
            int valueEnd = valueStart;
            while (valueEnd < json.length() && 
                   json[valueEnd] != ',' && 
                   json[valueEnd] != '}' && 
                   json[valueEnd] != ' ' && 
                   json[valueEnd] != '\t' &&
                   json[valueEnd] != '\n' &&
                   json[valueEnd] != '\r') {
                valueEnd++;
            }
            
            if (valueEnd > valueStart) {
                String valueStr = json.substring(valueStart, valueEnd);
                valueStr.trim();
                return valueStr.toInt();
            }
        }
    }
    
    return defaultValue;
}

#endif // JSON_UTILS_H


/**
 * Unified Command Dispatcher Implementation
 * 
 * Consolidates command handling across MQTT/SMS, Web UI, and HTTP API interfaces.
 */

#include "command_dispatcher.h"
#include "json_utils.h"
// ArduinoJson removed - using cJSON instead (via json_utils.h)

#include "display_manager.h"  // Unified display media with overlay

// Forward declarations of handler functions from main.cpp
extern bool handleClearCommand();
extern bool handleNextCommand();
extern bool handleGoCommand(const String& parameter);
extern bool handleShowCommand(const String& parameter);
extern bool handleTextCommandWithColor(const String& parameter, uint8_t fillColor, uint8_t outlineColor, uint8_t bgColor = 0, const String& backgroundImage = "");
extern bool handleMultiTextCommand(const String& parameter, uint8_t bgColor = 0);
extern bool handleListNumbersCommand(const String& originalMessage = "");
#include "canvas_handler.h"  // Canvas command handlers
extern bool handlePingCommand(const String& originalMessage);
extern bool handleIpCommand(const String& originalMessage);
extern bool handleVolumeCommand(const String& parameter);
extern bool handleNewNumberCommand(const String& parameter);
extern bool handleDelNumberCommand(const String& parameter);
extern bool handleSleepIntervalCommand(const String& parameter);
extern bool handleOAICommand(const String& parameter);
extern bool handleManageCommand();
extern bool handleOtaCommand(const String& originalMessage);
extern bool isNumberAllowed(const String& number);
extern String extractFromFieldFromMessage(const String& message);
extern String extractCommandParameter(const String& command);
extern String extractJsonStringField(const String& json, const String& fieldName);

// Media mapping structure (forward declaration - defined in main.cpp)
struct MediaMapping {
    String imageName;
    String audioFile;
};
extern std::vector<MediaMapping> g_media_mappings;
extern bool g_media_mappings_loaded;

// Color constants (from EL133UF1)
#define EL133UF1_WHITE 0
#define EL133UF1_BLACK 1
#define EL133UF1_YELLOW 2
#define EL133UF1_RED 3
#define EL133UF1_BLUE 4
#define EL133UF1_GREEN 5

// Helper to extract text parameter from command/message
static String extractTextParameterForCommand(const String& command, const String& originalMessage, const char* cmdName) {
    String textToDisplay = "";
    String cmdNameStr = String(cmdName);
    
    // Check if it's JSON format
    if (originalMessage.startsWith("{")) {
        // Parse JSON to extract "text" field (preserving case) using cJSON
        textToDisplay = extractJsonStringField(originalMessage, "text");
    } else {
        // Not JSON - extract parameter from original message (preserving case)
        String lowerMsg = originalMessage;
        lowerMsg.toLowerCase();
        int cmdPos = lowerMsg.indexOf(cmdNameStr);
        if (cmdPos >= 0) {
            int spacePos = originalMessage.indexOf(' ', cmdPos + cmdNameStr.length());
            if (spacePos >= 0) {
                textToDisplay = originalMessage.substring(spacePos + 1);
                textToDisplay.trim();
            }
        }
    }
    
    // Fallback to extracting from command if above failed
    if (textToDisplay.length() == 0) {
        textToDisplay = extractCommandParameter(command);
    }
    
    // Remove command prefix if present (case insensitive)
    textToDisplay.trim();
    String lowerText = textToDisplay;
    lowerText.toLowerCase();
    String prefixToRemove = cmdNameStr + " ";
    if (lowerText.startsWith(prefixToRemove)) {
        textToDisplay = textToDisplay.substring(prefixToRemove.length());
        textToDisplay.trim();
    }
    
    return textToDisplay;
}

// Helper to convert color string to color constant
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

// Unified command handlers
static bool handleClearUnified(const CommandContext& ctx) {
    (void)ctx;  // No parameters needed
    return handleClearCommand();
}

static bool handleNextUnified(const CommandContext& ctx) {
    (void)ctx;  // No parameters needed
    return handleNextCommand();
}

static bool handleGoUnified(const CommandContext& ctx) {
    String param = "";
    if (ctx.source == CommandSource::MQTT_SMS) {
        param = extractCommandParameter(ctx.command);
    } else if (ctx.source == CommandSource::WEB_UI) {
        param = extractJsonStringField(ctx.originalMessage, "parameter");
    } else if (ctx.source == CommandSource::HTTP_API) {
        // HTTP API would extract from query params or JSON body
        param = extractJsonStringField(ctx.originalMessage, "parameter");
    }
    return handleGoCommand(param);
}

static bool handleShowUnified(const CommandContext& ctx) {
    if (ctx.source == CommandSource::MQTT_SMS) {
        // MQTT/SMS uses filename directly (for showing arbitrary files)
        String param = extractCommandParameter(ctx.command);
        return handleShowCommand(param);
    } else if (ctx.source == CommandSource::WEB_UI) {
        // Web UI can use either index (for media mappings) or filename (for arbitrary files)
        // Check if parameter is a number (index) or a string (filename)
        String param = extractJsonStringField(ctx.originalMessage, "parameter");
        if (param.length() == 0) {
            // Try index field instead
            String indexStr = extractJsonStringField(ctx.originalMessage, "index");
            if (indexStr.length() > 0) {
                int index = indexStr.toInt();
                // Use unified function for media mappings
                return displayMediaWithOverlay(index, 100);
            }
            return false;
        }
        // Check if it's a number (index)
        bool isNumeric = true;
        for (size_t i = 0; i < param.length(); i++) {
            if (!isdigit(param.charAt(i))) {
                isNumeric = false;
                break;
            }
        }
        if (isNumeric && param.length() > 0) {
            // It's an index - use unified function
            int index = param.toInt();
            return displayMediaWithOverlay(index, 100);
        } else {
            // It's a filename - use handleShowCommand
            return handleShowCommand(param);
        }
    } else if (ctx.source == CommandSource::HTTP_API) {
        // HTTP API uses index for media mappings - use unified function
        String indexStr = extractJsonStringField(ctx.originalMessage, "index");
        if (indexStr.length() == 0) {
            return false;
        }
        int index = indexStr.toInt();
        return displayMediaWithOverlay(index, 100);
    }
    return false;
}

static bool handleTextUnified(const CommandContext& ctx) {
    String text = "";
    uint8_t fillColor = EL133UF1_BLACK;      // Default: black text
    uint8_t outlineColor = EL133UF1_BLACK;   // Default: black outline
    uint8_t bgColor = EL133UF1_WHITE;        // Default: white background (only used when no background image)
    String backgroundImage = "";
    
    if (ctx.source == CommandSource::MQTT_SMS) {
        // MQTT: Extract from command string (!text <text>)
        text = extractTextParameterForCommand(ctx.command, ctx.originalMessage, "!text");
        // MQTT text commands use default colors (black text/outline, white background)
    } else if (ctx.source == CommandSource::WEB_UI || ctx.source == CommandSource::HTTP_API) {
        // Web UI/HTTP: Extract from JSON
        text = extractJsonStringField(ctx.originalMessage, "text");
        String colorStr = extractJsonStringField(ctx.originalMessage, "color");
        String bgColorStr = extractJsonStringField(ctx.originalMessage, "backgroundColour");
        String outlineColorStr = extractJsonStringField(ctx.originalMessage, "outlineColour");
        backgroundImage = extractJsonStringField(ctx.originalMessage, "backgroundImage");
        
        // Handle multi-color text
        if (colorStr == "multi") {
            bgColor = parseColorString(bgColorStr);
            return handleMultiTextCommand(text, bgColor);
        }
        
        // Only parse colors if provided (non-empty string)
        // Defaults: black text, black outline, white background
        if (colorStr.length() > 0) {
            fillColor = parseColorString(colorStr);
        }
        if (bgColorStr.length() > 0) {
            bgColor = parseColorString(bgColorStr);
        }
        if (outlineColorStr.length() > 0) {
            outlineColor = parseColorString(outlineColorStr);
        }
    }
    
    if (text.length() == 0) {
        return false;
    }
    
    return handleTextCommandWithColor(text, fillColor, outlineColor, bgColor, backgroundImage);
}

static bool handleListUnified(const CommandContext& ctx) {
    return handleListNumbersCommand(ctx.originalMessage);
}

static bool handlePingUnified(const CommandContext& ctx) {
    return handlePingCommand(ctx.originalMessage);
}

static bool handleIpUnified(const CommandContext& ctx) {
    return handleIpCommand(ctx.originalMessage);
}

static bool handleVolumeUnified(const CommandContext& ctx) {
    String param = "";
    if (ctx.source == CommandSource::MQTT_SMS) {
        param = extractCommandParameter(ctx.command);
    } else {
        param = extractJsonStringField(ctx.originalMessage, "parameter");
    }
    return handleVolumeCommand(param);
}

static bool handleNewNumberUnified(const CommandContext& ctx) {
    String param = "";
    if (ctx.source == CommandSource::MQTT_SMS) {
        param = extractCommandParameter(ctx.command);
    } else {
        param = extractJsonStringField(ctx.originalMessage, "parameter");
    }
    return handleNewNumberCommand(param);
}

static bool handleDelNumberUnified(const CommandContext& ctx) {
    String param = "";
    if (ctx.source == CommandSource::MQTT_SMS) {
        param = extractCommandParameter(ctx.command);
    } else {
        param = extractJsonStringField(ctx.originalMessage, "parameter");
    }
    return handleDelNumberCommand(param);
}

static bool handleSleepIntervalUnified(const CommandContext& ctx) {
    String param = "";
    if (ctx.source == CommandSource::MQTT_SMS) {
        param = extractCommandParameter(ctx.command);
    } else {
        param = extractJsonStringField(ctx.originalMessage, "parameter");
    }
    return handleSleepIntervalCommand(param);
}

static bool handleOAIUnified(const CommandContext& ctx) {
    String prompt = "";
    if (ctx.source == CommandSource::MQTT_SMS) {
        prompt = extractTextParameterForCommand(ctx.command, ctx.originalMessage, "!oai");
    } else {
        prompt = extractJsonStringField(ctx.originalMessage, "prompt");
    }
    return handleOAICommand(prompt);
}

static bool handleManageUnified(const CommandContext& ctx) {
    (void)ctx;
    return handleManageCommand();
}

static bool handleOtaUnified(const CommandContext& ctx) {
    return handleOtaCommand(ctx.originalMessage);
}

static bool handleCanvasDisplayUnified(const CommandContext& ctx) {
    return handleCanvasDisplayCommand(ctx.originalMessage);
}

static bool handleCanvasDisplaySaveUnified(const CommandContext& ctx) {
    return handleCanvasDisplaySaveCommand(ctx.originalMessage);
}

static bool handleCanvasSaveUnified(const CommandContext& ctx) {
    return handleCanvasSaveCommand(ctx.originalMessage);
}

// Command registry
static const UnifiedCommandEntry commandRegistry[] = {
    // Clear display
    {
        .mqttName = "!clear",
        .webUIName = "clear",
        .httpEndpoint = nullptr,
        .handler = handleClearUnified,
        .requiresAuth = true,
        .description = "Clear the display"
    },
    
    // Next media
    {
        .mqttName = "!next",
        .webUIName = "next",
        .httpEndpoint = nullptr,
        .handler = handleNextUnified,
        .requiresAuth = true,
        .description = "Show next media item"
    },
    
    // Go to index
    {
        .mqttName = "!go",
        .webUIName = "go",
        .httpEndpoint = nullptr,
        .handler = handleGoUnified,
        .requiresAuth = true,
        .description = "Go to specific media index"
    },
    
    // Show media
    {
        .mqttName = "!show",
        .webUIName = nullptr,
        .httpEndpoint = "/api/media/show",
        .handler = handleShowUnified,
        .requiresAuth = true,
        .description = "Show media by index"
    },
    
    // Text display
    {
        .mqttName = "!text",
        .webUIName = "text_display",
        .httpEndpoint = "/api/text/display",
        .handler = handleTextUnified,
        .requiresAuth = true,
        .description = "Display text on screen"
    },
    
    // List numbers
    {
        .mqttName = "!list",
        .webUIName = nullptr,
        .httpEndpoint = nullptr,
        .handler = handleListUnified,
        .requiresAuth = true,
        .description = "List allowed phone numbers"
    },
    
    // Ping
    {
        .mqttName = "!ping",
        .webUIName = nullptr,
        .httpEndpoint = nullptr,
        .handler = handlePingUnified,
        .requiresAuth = true,
        .description = "Ping command (responds with pong)"
    },
    
    // IP
    {
        .mqttName = "!ip",
        .webUIName = nullptr,
        .httpEndpoint = nullptr,
        .handler = handleIpUnified,
        .requiresAuth = true,
        .description = "Get device IP address"
    },
    
    // Volume
    {
        .mqttName = "!volume",
        .webUIName = nullptr,
        .httpEndpoint = nullptr,
        .handler = handleVolumeUnified,
        .requiresAuth = true,
        .description = "Set audio volume"
    },
    
    // New number
    {
        .mqttName = "!newno",
        .webUIName = nullptr,
        .httpEndpoint = nullptr,
        .handler = handleNewNumberUnified,
        .requiresAuth = true,
        .description = "Add allowed phone number"
    },
    
    // Delete number
    {
        .mqttName = "!delno",
        .webUIName = nullptr,
        .httpEndpoint = nullptr,
        .handler = handleDelNumberUnified,
        .requiresAuth = true,
        .description = "Remove allowed phone number"
    },
    
    // Sleep interval
    {
        .mqttName = "!sleep_interval",
        .webUIName = nullptr,
        .httpEndpoint = nullptr,
        .handler = handleSleepIntervalUnified,
        .requiresAuth = true,
        .description = "Set sleep interval"
    },
    
    // OAI
    {
        .mqttName = "!oai",
        .webUIName = nullptr,
        .httpEndpoint = nullptr,
        .handler = handleOAIUnified,
        .requiresAuth = true,
        .description = "Generate AI image"
    },
    
    // Manage
    {
        .mqttName = "!manage",
        .webUIName = nullptr,
        .httpEndpoint = nullptr,
        .handler = handleManageUnified,
        .requiresAuth = true,
        .description = "Start management web interface"
    },
    
    // OTA
    {
        .mqttName = "!ota",
        .webUIName = nullptr,
        .httpEndpoint = nullptr,
        .handler = handleOtaUnified,
        .requiresAuth = true,
        .description = "Start OTA update"
    },
    
    // Canvas display
    {
        .mqttName = nullptr,
        .webUIName = "canvas_display",
        .httpEndpoint = nullptr,
        .handler = handleCanvasDisplayUnified,
        .requiresAuth = true,
        .description = "Display canvas (large image data)"
    },
    
    // Canvas display and save
    {
        .mqttName = nullptr,
        .webUIName = "canvas_display_save",
        .httpEndpoint = nullptr,
        .handler = handleCanvasDisplaySaveUnified,
        .requiresAuth = true,
        .description = "Display canvas and save to SD (large image data)"
    },
    
    // Canvas save only (no display)
    {
        .mqttName = nullptr,
        .webUIName = "canvas_save",
        .httpEndpoint = nullptr,
        .handler = handleCanvasSaveUnified,  // TODO: implement handleCanvasSaveUnified
        .requiresAuth = true,
        .description = "Save canvas to SD without displaying (large image data)"
    }
};

static const size_t commandRegistryCount = sizeof(commandRegistry) / sizeof(commandRegistry[0]);

// Normalize command name based on source
String normalizeCommandName(const String& command, CommandSource source) {
    String normalized = command;
    normalized.trim();
    normalized.toLowerCase();
    
    if (source == CommandSource::MQTT_SMS) {
        // Remove "!" prefix
        if (normalized.startsWith("!")) {
            normalized = normalized.substring(1);
        }
    } else if (source == CommandSource::WEB_UI) {
        // Map Web UI command names to normalized names
        if (normalized == "text_display") {
            normalized = "text";
        }
        // Other Web UI commands already match (clear, next, go)
    }
    // HTTP API commands are normalized before creating CommandContext
    
    return normalized;
}

// Extract command from MQTT message
String extractMqttCommand(const String& message) {
    // MQTT commands start with "!"
    String lower = message;
    lower.toLowerCase();
    
    // Find first "!" command
    int cmdPos = lower.indexOf('!');
    if (cmdPos < 0) {
        return "";
    }
    
    // Extract command (up to first space or end of string)
    int spacePos = message.indexOf(' ', cmdPos);
    if (spacePos < 0) {
        return message.substring(cmdPos);
    }
    return message.substring(cmdPos, spacePos);
}

// Extract command from Web UI JSON
String extractWebUICommand(const String& jsonMessage) {
    return extractJsonStringField(jsonMessage, "command");
}

// Dispatch a command
bool dispatchCommand(const CommandContext& ctx) {
    // Normalize command name
    String normalized = normalizeCommandName(ctx.command, ctx.source);
    
    // Find matching registry entry
    for (size_t i = 0; i < commandRegistryCount; i++) {
        const UnifiedCommandEntry& entry = commandRegistry[i];
        
        bool matches = false;
        
        if (ctx.source == CommandSource::MQTT_SMS && entry.mqttName != nullptr) {
            String mqttNormalized = normalizeCommandName(String(entry.mqttName), CommandSource::MQTT_SMS);
            // For MQTT, check both exact match and prefix match (for commands like !text, !go)
            matches = (normalized == mqttNormalized || ctx.command.startsWith(entry.mqttName));
        } else if (ctx.source == CommandSource::WEB_UI && entry.webUIName != nullptr) {
            String webUINormalized = normalizeCommandName(String(entry.webUIName), CommandSource::WEB_UI);
            matches = (normalized == webUINormalized);
        } else if (ctx.source == CommandSource::HTTP_API && entry.httpEndpoint != nullptr) {
            // HTTP endpoints are matched by endpoint path (set in ctx.command)
            // The endpoint path should match entry.httpEndpoint
            matches = (ctx.command == String(entry.httpEndpoint));
        }
        
        if (matches) {
            // Check authentication if required
            if (entry.requiresAuth && ctx.source == CommandSource::MQTT_SMS) {
                if (!isNumberAllowed(ctx.senderNumber)) {
                    Serial.printf("ERROR: Number %s is not in allowed list - command rejected\n", ctx.senderNumber.c_str());
                    return false;
                }
            }
            
            // Call handler
            bool success = entry.handler(ctx);
            
            // Publish completion status for WEB_UI commands (if enabled and command ID provided)
            if (ctx.shouldPublishCompletion && ctx.source == CommandSource::WEB_UI && ctx.commandId.length() > 0) {
                extern void publishMQTTCommandCompletion(const String& commandId, const String& commandName, bool success);
                String commandName = normalized;
                if (entry.webUIName != nullptr) {
                    commandName = String(entry.webUIName);
                }
                Serial.printf("[Dispatcher] Publishing completion for WEB_UI command: id='%s', name='%s', success=%d\n",
                             ctx.commandId.c_str(), commandName.c_str(), success ? 1 : 0);
                publishMQTTCommandCompletion(ctx.commandId, commandName, success);
            }
            
            return success;
        }
    }
    
    // Command not found
    Serial.printf("Unknown command: %s (source: %d)\n", ctx.command.c_str(), (int)ctx.source);
    
    // Publish completion for unknown commands too (so web UI knows it failed)
    if (ctx.shouldPublishCompletion && ctx.source == CommandSource::WEB_UI && ctx.commandId.length() > 0) {
        extern void publishMQTTCommandCompletion(const String& commandId, const String& commandName, bool success);
        publishMQTTCommandCompletion(ctx.commandId, normalized, false);
    }
    
    return false;
}

// Get command registry
const UnifiedCommandEntry* getCommandRegistry(size_t* count) {
    if (count) {
        *count = commandRegistryCount;
    }
    return commandRegistry;
}

// Initialize (currently no-op, but reserved for future use)
void initCommandDispatcher() {
    // Future: Could register commands dynamically, load from config, etc.
}


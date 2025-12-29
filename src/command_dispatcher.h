/**
 * Unified Command Dispatcher
 * 
 * This module provides a unified command system that handles commands from:
 * - MQTT/SMS (via handleMqttCommand)
 * - Web UI (via handleWebInterfaceCommand) 
 * - HTTP API (via REST endpoints)
 * 
 * Commands are normalized and routed through a single registry, eliminating
 * duplication and ensuring consistent behavior across all interfaces.
 */

#ifndef COMMAND_DISPATCHER_H
#define COMMAND_DISPATCHER_H

#include <Arduino.h>

// Command source types
enum class CommandSource {
    MQTT_SMS,      // Command from MQTT/SMS (e.g., "!clear", "!next")
    WEB_UI,         // Command from encrypted Web UI (e.g., "clear", "next")
    HTTP_API        // Command from HTTP REST API (e.g., "/api/text/display")
};

// Command parameter extraction context
struct CommandContext {
    CommandSource source;
    String command;              // Normalized command name (e.g., "clear", "next", "go")
    String originalMessage;       // Original message/JSON for parameter extraction
    String senderNumber;          // For MQTT/SMS: sender phone number
    bool requiresAuth;            // Whether authentication is required
};

// Unified command handler function signature
// Returns true on success, false on failure
typedef bool (*UnifiedCommandHandler)(const CommandContext& ctx);

// Command registry entry
struct UnifiedCommandEntry {
    const char* mqttName;        // MQTT command name (e.g., "!clear") or nullptr
    const char* webUIName;        // Web UI command name (e.g., "clear") or nullptr
    const char* httpEndpoint;     // HTTP endpoint (e.g., "/api/text/display") or nullptr
    UnifiedCommandHandler handler;
    bool requiresAuth;            // Whether authentication is required
    const char* description;      // Human-readable description
};

// Initialize the command dispatcher
void initCommandDispatcher();

// Dispatch a command from any source
// Returns true on success, false on failure
bool dispatchCommand(const CommandContext& ctx);

// Normalize command name (removes "!" prefix, converts to lowercase)
String normalizeCommandName(const String& command, CommandSource source);

// Extract command from MQTT message
String extractMqttCommand(const String& message);

// Extract command from Web UI JSON
String extractWebUICommand(const String& jsonMessage);

// Get command registry (for debugging/inspection)
const UnifiedCommandEntry* getCommandRegistry(size_t* count);

#endif // COMMAND_DISPATCHER_H


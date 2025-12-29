# Unified Command System Design

## Overview

The unified command system consolidates command handling across all interfaces:
- **MQTT/SMS** (`handleMqttCommand`) - Commands like `!clear`, `!next`, `!go`
- **Web UI** (`handleWebInterfaceCommand`) - Commands like `clear`, `next`, `go`, `text_display`
- **HTTP API** (REST endpoints) - Endpoints like `/api/text/display`, `/api/media/show`

## Benefits

1. **Eliminate Duplication**: Commands like `clear`, `next`, and `go` are currently implemented in multiple places
2. **Consistent Behavior**: All interfaces use the same command handlers
3. **Easier Maintenance**: Add new commands in one place, available everywhere
4. **Better Testing**: Test commands once, works for all interfaces

## Command Mapping

### Current Overlaps

| Command | MQTT/SMS | Web UI | HTTP API | Handler Function |
|---------|----------|--------|----------|------------------|
| Clear display | `!clear` | `clear` | - | `handleClearCommand()` |
| Next media | `!next` | `next` | - | `handleNextCommand()` |
| Go to index | `!go <n>` | `go` (with `parameter`) | - | `handleGoCommand()` |
| Show media | `!show <n>` | - | `/api/media/show?index=<n>` | `handleShowCommand()` |
| Text display | `!text <text>` | `text_display` | `/api/text/display` | `handleTextCommandWithColor()` |
| Canvas display | - | `canvas_display` | - | `handleCanvasDisplayCommand()` |

## Implementation Strategy

### Phase 1: Command Normalization

1. Create `CommandContext` structure to hold:
   - Source (MQTT_SMS, WEB_UI, HTTP_API)
   - Normalized command name
   - Original message/JSON for parameter extraction
   - Authentication context

2. Create normalization functions:
   - `normalizeCommandName()` - Converts `!clear` → `clear`, `text_display` → `text`
   - `extractCommandFromSource()` - Extracts command based on source type

### Phase 2: Unified Registry

1. Extend existing `CommandHandler` registry to support:
   - Multiple command name aliases (MQTT, Web UI, HTTP)
   - Source-specific parameter extraction
   - Unified handler functions

2. Example registry entry:
```cpp
{
    .mqttName = "!clear",
    .webUIName = "clear",
    .httpEndpoint = nullptr,
    .handler = handleClearCommandUnified,
    .requiresAuth = true,
    .description = "Clear the display"
}
```

### Phase 3: Parameter Extraction Unification

1. Create unified parameter extraction:
   - MQTT: Extract from command string (`!go 5` → parameter `5`)
   - Web UI: Extract from JSON (`{"command":"go","parameter":"5"}`)
   - HTTP API: Extract from query params or JSON body

2. Example unified handler:
```cpp
bool handleGoCommandUnified(const CommandContext& ctx) {
    String param = "";
    if (ctx.source == CommandSource::MQTT_SMS) {
        param = extractCommandParameter(ctx.command);
    } else if (ctx.source == CommandSource::WEB_UI) {
        param = extractJsonStringField(ctx.originalMessage, "parameter");
    } else if (ctx.source == CommandSource::HTTP_API) {
        param = extractFromQueryOrBody(ctx.originalMessage);
    }
    return handleGoCommand(param);
}
```

### Phase 4: Migration

1. Update `handleMqttCommand()` to use unified dispatcher
2. Update `handleWebInterfaceCommand()` to use unified dispatcher
3. Update HTTP API handlers to use unified dispatcher
4. Remove duplicate command handling code

## Command Name Normalization

| MQTT/SMS | Web UI | Normalized | Notes |
|----------|--------|------------|-------|
| `!clear` | `clear` | `clear` | Remove `!` prefix |
| `!next` | `next` | `next` | Remove `!` prefix |
| `!go <n>` | `go` (with `parameter`) | `go` | Remove `!` prefix |
| `!text <text>` | `text_display` | `text` | Map `text_display` → `text` |
| `!yellow_text` | `text_display` (with `color:yellow`) | `text` | Color handled via parameters |
| `!show <n>` | - | `show` | HTTP API uses query param |
| - | `canvas_display` | `canvas_display` | Web UI only |

## Special Cases

### Authentication

- **MQTT/SMS**: Requires sender number validation (`isNumberAllowed()`)
- **Web UI**: Requires HMAC validation (already in `decryptAndValidateWebUIMessage()`)
- **HTTP API**: May require API key or session token

### Parameter Extraction

- **MQTT/SMS**: Simple string parsing (`!go 5` → `5`)
- **Web UI**: JSON field extraction (`{"parameter":"5"}` → `5`)
- **HTTP API**: Query params or JSON body

### Large Messages

- **canvas_display**: 640KB messages need special handling (skip full JSON parse)
- Current implementation handles this separately - keep this optimization

## Migration Plan

1. **Create `command_dispatcher` module** (`.h` and `.cpp`)
2. **Define unified command registry** with all commands
3. **Create unified handler wrappers** that extract parameters based on source
4. **Update `handleMqttCommand()`** to call unified dispatcher
5. **Update `handleWebInterfaceCommand()`** to call unified dispatcher
6. **Update HTTP API handlers** to call unified dispatcher
7. **Test all interfaces** to ensure compatibility
8. **Remove old duplicate code**

## Example: Unified `clear` Command

### Before (Duplicated):
```cpp
// In handleMqttCommand():
if (command == "!clear") {
    return handleClearCommand();
}

// In handleWebInterfaceCommand():
else if (command == "clear") {
    return handleClearCommand();
}
```

### After (Unified):
```cpp
// In command_dispatcher.cpp:
static const UnifiedCommandEntry commandRegistry[] = {
    {
        .mqttName = "!clear",
        .webUIName = "clear",
        .httpEndpoint = nullptr,
        .handler = [](const CommandContext& ctx) {
            return handleClearCommand();  // No parameters needed
        },
        .requiresAuth = true,
        .description = "Clear the display"
    },
    // ... more commands
};

// In handleMqttCommand():
CommandContext ctx;
ctx.source = CommandSource::MQTT_SMS;
ctx.command = normalizeCommandName(command, CommandSource::MQTT_SMS);
ctx.originalMessage = originalMessage;
ctx.senderNumber = extractFromFieldFromMessage(originalMessage);
return dispatchCommand(ctx);

// In handleWebInterfaceCommand():
CommandContext ctx;
ctx.source = CommandSource::WEB_UI;
ctx.command = normalizeCommandName(command, CommandSource::WEB_UI);
ctx.originalMessage = messageToProcess;
ctx.requiresAuth = true;  // Already validated by decryptAndValidateWebUIMessage
return dispatchCommand(ctx);
```

## Benefits Summary

1. **Single Source of Truth**: All command logic in one registry
2. **Consistent Behavior**: Same command works identically across all interfaces
3. **Easier Testing**: Test once, works everywhere
4. **Simpler Maintenance**: Add new commands in one place
5. **Better Documentation**: Registry serves as command documentation
6. **Type Safety**: Compile-time checking of command handlers


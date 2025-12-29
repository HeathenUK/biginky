# Firmware Refactoring Plan - Prioritized To-Do List

## Executive Summary

The firmware codebase (`src/main_esp32p4_test.cpp`) is **14,491 lines** in a single file, containing 295+ function declarations. This analysis identifies critical maintainability, performance, and potential bug issues, prioritized by impact and risk.

**Highest Priority**: Maintainability (file size, function complexity)  
**Medium Priority**: Performance (String operations, code duplication)  
**Lower Priority**: Potential bugs (memory leaks, error handling)

---

## Priority 1: CRITICAL - Split Monolithic File

### Issue
- **File size**: 14,491 lines in single file
- **Function count**: 295+ function declarations
- **Main function**: `auto_cycle_task()` is ~1000 lines (2085-3086)
- **Mixed concerns**: MQTT, WiFi, display, audio, SD card, NVS, commands, encryption all in one file

### Evidence
```bash
$ wc -l src/main_esp32p4_test.cpp
14491 src/main_esp32p4_test.cpp

$ grep -c "^static\|^bool\|^void\|^int\|^String\|^struct\|^class" src/main_esp32p4_test.cpp
295
```

### Protection Strategy
1. **Create separate modules**:
   - `mqtt_handler.cpp/h` - MQTT connection, publishing, message handling
   - `wifi_manager.cpp/h` - WiFi connection, NTP sync, credentials
   - `command_dispatcher.cpp/h` - Command routing and handlers
   - `nvs_manager.cpp/h` - NVS operations wrapper
   - `display_controller.cpp/h` - Display update cycle logic
   - `audio_manager.cpp/h` - Audio playback
   - `sd_manager.cpp/h` - SD card operations

2. **Maintain exact compatibility**:
   - Use forward declarations and header files
   - Keep all global state variables in same locations (or move to shared header)
   - Preserve exact function signatures
   - Maintain same compilation order and linking

3. **Incremental approach**:
   - Extract one module at a time
   - Compile and test after each extraction
   - Use git commits per module - can revert individual extractions
   - No logic changes - pure code movement only

4. **Testing requirements**:
   - Verify all functionality works identically
   - Test deep sleep wake cycles
   - Test MQTT command processing
   - Test display updates
   - Test audio playback

---

## Priority 2: HIGH - Extract auto_cycle_task() into Smaller Functions

### Issue
- **Function size**: ~1000 lines (2085-3086)
- **Complexity**: Deeply nested conditionals, multiple responsibilities
- **Responsibilities**: Time sync, hour scheduling, MQTT checks, display updates, NTP resync, media loading

### Evidence
```cpp
static void auto_cycle_task(void* arg) {
    // Lines 2085-2424: Time sync, hour schedule checking, MQTT check cycle
    // Lines 2426-3086: Display update cycle, media loading, thumbnail publish
}
```

### Protection Strategy
1. **Extract functions**:
   - `checkAndSyncTime()` - Time validation and NTP sync
   - `checkHourSchedule()` - Hour enablement checking
   - `doMqttCheckCycle()` - MQTT connection and command processing
   - `doDisplayUpdateCycle()` - Display initialization and update
   - `loadMediaForDisplay()` - Media.txt loading and image selection

2. **Preserve execution flow**:
   - Keep exact same execution order
   - Preserve all early returns and goto statements
   - Maintain same state transitions
   - Keep same variable names and scoping

3. **Test critical paths**:
   - Time sync behavior (RTC drift compensation, NTP retries)
   - Hour schedule logic (disabled hours, wrap-around at midnight)
   - MQTT check timing (top-of-hour vs non-top-of-hour)
   - Display update cycle (media loading, thumbnail generation)

---

## Priority 3: HIGH - Consolidate Duplicate JSON Parsing Code

### Issue
- **Duplication**: Manual string parsing duplicated in 3+ places
- **Inconsistency**: ArduinoJson used in some places, manual parsing in others
- **Error handling**: Inconsistent between approaches

### Evidence
```cpp
// Pattern repeated in:
// 1. extractCommandFromMessage() - lines 5004-5028
// 2. extractFromFieldFromMessage() - lines 5054-5075
// 3. handleWebInterfaceCommand() - lines 5450-5480, 5500-5540

int textStart = command.indexOf("\"text\"");
int colonPos = command.indexOf(':', textStart);
int quoteStart = command.indexOf('"', colonPos);
int quoteEnd = command.indexOf('"', quoteStart + 1);
command = command.substring(quoteStart + 1, quoteEnd);
```

### Protection Strategy
1. **Create helper functions**:
   - `extractJsonField(message, fieldName)` - Generic JSON field extraction
   - `extractJsonString(message, fieldName)` - String field extraction
   - `extractJsonBool(message, fieldName)` - Boolean field extraction

2. **Hybrid approach**:
   - Use ArduinoJson for small messages (<4KB)
   - Keep manual parsing for large messages (canvas_display) - intentional for memory efficiency

3. **Test all parsing paths**:
   - SMS bridge commands: `!ping`, `!text`, `!get`, etc.
   - Web UI commands: `text_display`, `canvas_display`, `clear`, `next`
   - Verify JSON field extraction matches exactly (case sensitivity, whitespace)
   - Test edge cases: missing fields, malformed JSON, escaped quotes

---

## Priority 4: MEDIUM - Optimize String Operations

### Issue
- **Heap fragmentation**: 49+ String operations found
- **Inefficiency**: Repeated `.c_str()` calls, String concatenation in loops
- **Memory pressure**: String objects cause heap fragmentation on ESP32

### Evidence
```cpp
// Found 49+ instances of:
String formResponse = "To=";
formResponse += senderNumber;  // Heap allocation
formResponse += "&From=+447401492609";
formResponse += "&Body=Pong";
// Multiple .c_str() conversions
```

### Protection Strategy
1. **Use char buffers for fixed-size strings**:
   - MQTT responses (formResponse in handlePingCommand)
   - JSON building (status JSON, thumbnail JSON)
   - Pre-allocate buffers where size is known

2. **Keep String for dynamic content**:
   - Command parameters (unknown size)
   - File paths (variable length)
   - User-provided text

3. **Testing**:
   - Test all String-dependent paths: command parsing, MQTT publishing, file paths
   - Verify memory usage doesn't increase (monitor heap fragmentation)
   - Test with long command parameters, large file paths
   - Keep backward compatibility - same function signatures

---

## Priority 5: MEDIUM - Consolidate Preferences.begin()/end() Patterns

### Issue
- **Repetition**: 127 Preferences.begin()/end() calls found
- **Risk**: Forgetting `.end()` causes NVS lock
- **Global objects**: Multiple global Preferences objects may not need to be global

### Evidence
```cpp
// Pattern repeated 127 times:
Preferences p;
p.begin("namespace", readOnly);
// ... operations ...
p.end();

// Global Preferences objects:
static Preferences volumePrefs;
static Preferences numbersPrefs;
static Preferences sleepPrefs;
// ... 7 total global Preferences objects
```

### Protection Strategy
1. **Create RAII wrapper**:
   ```cpp
   class NVSGuard {
       Preferences& prefs;
       bool opened;
   public:
       NVSGuard(const char* namespace, bool readOnly);
       ~NVSGuard() { if (opened) prefs.end(); }
       Preferences& get() { return prefs; }
   };
   ```

2. **Replace manual begin/end**:
   ```cpp
   // Before:
   Preferences p;
   p.begin("volume", true);
   int vol = p.getInt("volume", 50);
   p.end();
   
   // After:
   NVSGuard guard("volume", true);
   int vol = guard.get().getInt("volume", 50);
   // Auto-closes in destructor
   ```

3. **Test all NVS operations**:
   - Volume, sleep interval, hour schedule, media index
   - Allowed numbers, authentication
   - Verify no NVS locks (test rapid operations)
   - Test error paths (NVS full, corrupted)
   - Test persistence across deep sleep

---

## Priority 6: MEDIUM - Extract MQTT Connection/Disconnection Patterns

### Issue
- **Repetition**: mqttConnect()/mqttDisconnect() called 30+ times
- **Pattern duplication**: Connection pattern repeated in multiple places
- **Error handling**: Inconsistent error handling

### Evidence
```cpp
// Pattern repeated in:
// - auto_cycle_task() - lines 2318-2391
// - handlePingCommand() - lines 9233-9265
// - publishMQTTStatus() - lines 2379-2391
// - Top-of-hour - lines 3050-3072

if (mqttConnect()) {
    delay(1000);  // Wait for connection
    // ... do work ...
    mqttDisconnect();
    delay(100);
}
```

### Protection Strategy
1. **Create RAII wrapper**:
   ```cpp
   template<typename Func>
   bool withMqttConnection(Func callback) {
       if (!mqttConnect()) return false;
       delay(1000);
       bool result = callback();
       mqttDisconnect();
       delay(100);
       return result;
   }
   ```

2. **Test all MQTT operations**:
   - Status publish, thumbnail publish
   - Command processing (SMS bridge, web UI)
   - Media mappings publish
   - Verify connection state management (mqttConnected flag)
   - Test error paths (connection failures, timeouts)
   - Test reconnection behavior

---

## Priority 7: MEDIUM - Fix Potential Memory Leaks

### Issue
- **Memory management**: Some malloc/hal_psram_malloc calls may not have corresponding free() in error paths
- **Complex error paths**: canvas_display handler has multiple allocations with complex error handling

### Evidence
```cpp
// Potential leak in publishMQTTStatus() - line 3292:
if (encryptedJson.length() == 0) {
    Serial.println("ERROR: Failed to encrypt status");
    return;  // jsonBuffer already freed at line 3290, but this is OK
}

// But if malloc fails at line 3299, we return without issue
// However, if encryptAndFormatMessage() fails, we've already freed first buffer
// This appears safe, but needs audit

// canvas_display has complex error paths with multiple allocations
```

### Protection Strategy
1. **Audit all allocations**:
   - Find all `malloc()`, `hal_psram_malloc()` calls
   - Verify `free()`, `hal_psram_free()` in ALL code paths (including error paths)
   - Use RAII wrappers where possible

2. **Add memory leak detection**:
   - Track allocations in debug builds
   - Log allocation/deallocation pairs
   - Detect leaks on deep sleep cycles

3. **Test error injection**:
   - Simulate malloc failures
   - Simulate network failures during MQTT operations
   - Test with memory pressure (fill PSRAM, then try operations)
   - Verify no double-free errors

---

## Priority 8: LOW - Extract Command Handler Registration System

### Issue
- **Large if chains**: handleMqttCommand() has 20+ if statements (lines 5123-5361)
- **Duplication**: Similar routing in handleWebInterfaceCommand()
- **Maintainability**: Adding new commands requires modifying large if chains

### Evidence
```cpp
// handleMqttCommand() - 20+ command checks:
if (command == "!clear") return handleClearCommand();
if (command == "!ping") return handlePingCommand(originalMessage);
if (command == "!ip") return handleIpCommand(originalMessage);
// ... 17 more commands ...
```

### Protection Strategy
1. **Create CommandRegistry**:
   ```cpp
   struct CommandHandler {
       const char* name;
       bool (*handler)(const String&, const String&);
       bool requiresAuth;
   };
   
   static CommandHandler mqttCommands[] = {
       {"!ping", handlePingCommand, true},
       {"!clear", handleClearCommand, true},
       // ...
   };
   ```

2. **Maintain exact matching**:
   - Case-insensitive matching
   - Prefix matching for commands like `!text`, `!yellow_text`
   - Same error messages and return values

3. **Test all commands**:
   - Verify all existing commands work identically
   - Test command priority (SMS bridge vs web UI)
   - Test unknown command handling

---

## Priority 9: LOW - Consolidate WiFi Connection Patterns

### Issue
- **Repetition**: wifiConnectPersistent() called with different parameters
- **Duplication**: WiFi.disconnect() called 6+ times with varying delays
- **NTP sync**: Code duplicated in auto_cycle_task and top-of-hour path

### Evidence
```cpp
// WiFi connection patterns:
wifiConnectPersistent(10, 30000, true);  // MQTT check
wifiConnectPersistent(8, 30000, true);   // NTP resync
wifiConnectPersistent(5, 20000, false);  // Thumbnail publish

// WiFi disconnect:
WiFi.disconnect(); delay(100);
WiFi.disconnect(true); delay(500);
```

### Protection Strategy
1. **Create withWiFiConnection() wrapper**:
   - Similar to MQTT wrapper
   - Handles connect, retry, disconnect automatically

2. **Consolidate NTP sync**:
   - Single function for NTP sync with retries
   - Used by both auto_cycle_task and top-of-hour

3. **Test WiFi operations**:
   - MQTT checks, thumbnail publish, OTA
   - Verify retry behavior (10 retries, 30s timeout)
   - Test WiFi disconnect timing (power saving)
   - Test NTP sync behavior (5-cycle counter, retry logic)

---

## Priority 10: LOW - Optimize handleWebInterfaceCommand()

### Issue
- **Function size**: 500+ lines
- **Complexity**: Handles decryption, HMAC validation, JSON parsing, command routing
- **Duplication**: Manual JSON field extraction for canvas_display (100+ lines)

### Evidence
```cpp
// handleWebInterfaceCommand() - lines 5380-5900+
// - Decryption logic: ~50 lines
// - HMAC validation: ~50 lines  
// - Command extraction: ~50 lines
// - canvas_display handling: ~250 lines
// - Other command routing: ~100 lines
```

### Protection Strategy
1. **Extract functions**:
   - `decryptAndValidateMessage()` - Decryption and HMAC validation
   - `extractWebUICommand()` - Command extraction from JSON
   - `handleCanvasDisplayCommand()` - Large canvas_display handler

2. **Test all web UI commands**:
   - text_display, canvas_display, clear, next, go
   - Verify encryption/decryption matches exactly
   - Test HMAC validation (valid, invalid, missing)
   - Test large canvas_display messages (400KB+)
   - Verify command priority and deferral logic

---

## Testing Strategy for All Refactoring

### Functional Testing
1. **Command processing**: Test all SMS bridge and web UI commands
2. **Display updates**: Verify hourly updates, media cycling, thumbnails
3. **MQTT operations**: Status publish, thumbnail publish, command processing
4. **Deep sleep**: Test wake cycles, RTC drift compensation, hour scheduling
5. **Audio playback**: Test WAV/MP3 playback with media mappings
6. **Error handling**: Test network failures, memory pressure, corrupted data

### Performance Testing
1. **Memory usage**: Monitor heap fragmentation, PSRAM usage
2. **Boot time**: Verify no regression in startup time
3. **MQTT latency**: Verify command processing time unchanged
4. **Display update time**: Verify no regression in update cycle time

### Regression Testing
1. **Compare behavior**: Before/after refactoring - same inputs produce same outputs
2. **Serial logs**: Compare serial output for identical behavior
3. **MQTT messages**: Verify published messages are identical
4. **State persistence**: Verify NVS data persists correctly

---

## Implementation Order

1. **Phase 1** (Low risk, high value):
   - Priority 5: Preferences RAII wrapper
   - Priority 6: MQTT connection wrapper
   - Priority 9: WiFi connection wrapper

2. **Phase 2** (Medium risk, high value):
   - Priority 3: JSON parsing consolidation
   - Priority 4: String optimization
   - Priority 7: Memory leak fixes

3. **Phase 3** (Higher risk, critical value):
   - Priority 2: Extract auto_cycle_task functions
   - Priority 10: Optimize handleWebInterfaceCommand

4. **Phase 4** (Highest risk, critical value):
   - Priority 1: Split monolithic file into modules
   - Priority 8: Command registration system

---

## Risk Mitigation

1. **Git workflow**: Commit after each small change, can revert individually
2. **Incremental testing**: Test after each refactoring step
3. **Behavior preservation**: No logic changes, only code organization
4. **Serial logging**: Compare logs before/after to verify identical behavior
5. **Functional equivalence**: Same inputs → same outputs, same side effects

---

## Notes

- All refactoring should maintain **exact functional equivalence**
- No changes to algorithms, timing, or behavior
- Focus on code organization and maintainability
- Performance improvements are secondary to correctness
- Test thoroughly after each change before proceeding

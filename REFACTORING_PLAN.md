# Refactoring Plan for main_esp32p4_test.cpp

**Current State:** 10,293 lines in a single file with 241+ functions/classes/structs

**Goal:** Break into logical modules while maintaining 100% functionality and zero breaking changes.

---

## Priority 1: Low-Risk, High-Impact Extractions (Start Here)

### 1.1 **NVS Storage Module** (Priority: HIGHEST)
**Location:** Lines ~7175-7712, ~7246-7319, ~7423-7712
**Size:** ~600 lines
**Risk:** Very Low - Well-isolated, minimal dependencies
**Files to create:**
- `src/storage/NVSStorage.h` / `.cpp`
- Functions: `volumeLoadFromNVS()`, `volumeSaveToNVS()`, `mediaIndexLoadFromNVS()`, `mediaIndexSaveToNVS()`, `sleepDurationLoadFromNVS()`, `sleepDurationSaveToNVS()`, `hourScheduleLoadFromNVS()`, `hourScheduleSaveToNVS()`, `numbersLoadFromNVS()`, `isNumberAllowed()`, `addAllowedNumber()`, `removeAllowedNumber()`
- **Why first:** Foundation for other modules, clear boundaries, no side effects

### 1.2 **Logging System** (Priority: HIGH)
**Location:** Lines ~317-430, ~329-417 (LogSerial class)
**Size:** ~150 lines
**Risk:** Very Low - Self-contained, only depends on SD card
**Files to create:**
- `src/logging/Logging.h` / `.cpp`
- Classes: `LogSerial`, functions: `logPrint()`, `logPrintf()`, `logRotate()`, `logInit()`, `logFlush()`, `logClose()`
- **Why second:** Used everywhere but isolated, easy to extract

### 1.3 **Pin Definitions & Hardware Config** (Priority: HIGH)
**Location:** Lines ~108-216
**Size:** ~110 lines
**Risk:** Very Low - Pure definitions
**Files to create:**
- `src/config/HardwareConfig.h` (header-only)
- All pin definitions, hardware constants
- **Why third:** Referenced everywhere but never changes, easy to extract

---

## Priority 2: Medium-Risk, Well-Isolated Modules

### 2.1 **Audio System** (Priority: HIGH)
**Location:** Lines ~261-712, ~1237-1315 (AudioFileSourceFatFs)
**Size:** ~500 lines
**Risk:** Low-Medium - Well-isolated but has some global state
**Files to create:**
- `src/audio/AudioSystem.h` / `.cpp`
- Classes: `AudioFileSourceFatFs`, functions: `audio_i2s_init()`, `audio_start()`, `audio_beep()`, `audio_stop()`, `audio_task()`, `playWavFile()`
- **Dependencies:** ES8311 codec, I2S, SD card
- **Why:** Large, self-contained, clear interface

### 2.2 **SD Card Operations** (Priority: HIGH)
**Location:** Lines ~1225-1942, ~4312-4350
**Size:** ~800 lines
**Risk:** Medium - Core functionality, many dependencies
**Files to create:**
- `src/storage/SDCardManager.h` / `.cpp`
- Functions: `sdInitDirect()`, `loadQuotesFromSD()`, `loadMediaMappingsFromSD()`, `getAudioForImage()`, `listImageFiles()`, `listAudioFiles()`, `listAllFiles()`, file read/write helpers
- **Dependencies:** FatFs, SDMMC driver
- **Why:** Large block, clear responsibilities

### 2.3 **Sleep & Power Management** (Priority: MEDIUM-HIGH)
**Location:** Lines ~715-907, ~791-797
**Size:** ~200 lines
**Risk:** Medium - Critical for operation, but well-defined
**Files to create:**
- `src/power/SleepManager.h` / `.cpp`
- Functions: `sleepNowSeconds()`, `sleepUntilNextMinuteOrFallback()`, `isHourEnabled()`
- **Dependencies:** ESP sleep APIs, hour schedule
- **Why:** Critical but isolated, clear interface

---

## Priority 3: Network & Communication Modules

### 3.1 **WiFi Management** (Priority: MEDIUM)
**Location:** Lines ~1955-1956, ~7956-7970, ~928-1224 (ensureTimeValid)
**Size:** ~400 lines
**Risk:** Medium - Network code can be tricky
**Files to create:**
- `src/network/WiFiManager.h` / `.cpp`
- Functions: `wifiLoadCredentials()`, `wifiConnectPersistent()`, `wifiClearCredentials()`, `enterConfigMode()`, `ensureTimeValid()`
- **Dependencies:** WiFi library, NVS
- **Why:** Self-contained networking logic

### 3.2 **MQTT Client** (Priority: MEDIUM)
**Location:** Lines ~2806-3058, ~7886-7955
**Size:** ~400 lines
**Risk:** Medium - Complex state machine, event-driven
**Files to create:**
- `src/network/MQTTClient.h` / `.cpp`
- Functions: `mqttLoadConfig()`, `mqttSaveConfig()`, `mqttSetConfig()`, `mqttStatus()`, `mqttConnect()`, `mqttCheckMessages()`, `mqttGetLastMessage()`, `mqttDisconnect()`, `mqttEventHandler()`
- **Dependencies:** ESP-IDF MQTT, WiFi
- **Why:** Large, complex, but well-defined interface

### 3.3 **MQTT Command Handlers** (Priority: MEDIUM)
**Location:** Lines ~3307-4698, ~1964-1985 (forward decls)
**Size:** ~1400 lines
**Risk:** Medium-High - Many commands, complex logic
**Files to create:**
- `src/network/MQTTCommands.h` / `.cpp`
- Functions: `handleMqttCommand()`, `extractCommandFromMessage()`, all `handle*Command()` functions
- **Dependencies:** MQTT, Display, Audio, Storage
- **Why:** Largest single block, but command-based so naturally modular

---

## Priority 4: Web Interface & OTA

### 4.1 **Web Management Interface** (Priority: MEDIUM-LOW)
**Location:** Lines ~4300-4698, ~3771-4202 (ota_server_task)
**Size:** ~1000 lines
**Risk:** Medium-High - Complex HTML generation, many endpoints
**Files to create:**
- `src/web/WebServer.h` / `.cpp`
- Functions: `generateManagementHTML()`, `handleManageCommand()`, `ota_server_task()`, all API endpoint handlers
- **Dependencies:** WebServer, Storage, Display, Audio
- **Why:** Large but self-contained, clear HTTP interface

### 4.2 **OTA Update System** (Priority: LOW-MEDIUM)
**Location:** Lines ~3199-3770, ~1986-1987
**Size:** ~600 lines
**Risk:** Medium - Critical functionality, complex flash operations
**Files to create:**
- `src/ota/OTAManager.h` / `.cpp`
- Functions: `checkAndNotifyOTAUpdate()`, `startSdBufferedOTA()`, OTA download/flash logic
- **Dependencies:** HTTP, SD card, ESP OTA APIs
- **Why:** Complex but isolated, clear responsibilities

---

## Priority 5: Display & Media Operations

### 5.1 **Media Management** (Priority: LOW-MEDIUM)
**Location:** Lines ~1370-1760, ~4699-5943 (show_media_task)
**Size:** ~800 lines
**Risk:** Medium - Display operations, complex state
**Files to create:**
- `src/media/MediaManager.h` / `.cpp`
- Functions: `pngDrawFromMediaMappings()`, `pngDrawRandomToBuffer()`, `show_media_task()`, media loading/display logic
- **Dependencies:** Display, SD card, Audio, Text placement
- **Why:** Large block but clear purpose

### 5.2 **Display Operations** (Priority: LOW)
**Location:** Scattered throughout, but mainly in command handlers
**Size:** ~500 lines (estimated)
**Risk:** Low-Medium - Display is already abstracted via EL133UF1
**Files to create:**
- `src/display/DisplayOps.h` / `.cpp`
- Helper functions for common display operations, text rendering, image drawing
- **Dependencies:** EL133UF1 library
- **Why:** Can be extracted incrementally

---

## Priority 6: Main Application Logic

### 6.1 **Main Setup & Loop** (Priority: LOWEST - Keep Last)
**Location:** Lines ~9766-10293
**Size:** ~500 lines
**Risk:** Very High - Core application flow
**Files to create:**
- Keep in main file, but simplify by calling module functions
- **Why:** Central orchestration, should stay in main file

### 6.2 **Auto Cycle Task** (Priority: LOW)
**Location:** Lines ~2006-2805
**Size:** ~800 lines
**Risk:** High - Core application logic, complex state
**Files to create:**
- Consider: `src/core/AutoCycle.h` / `.cpp`
- **Why:** Complex but could be extracted if other modules are clean

---

## Implementation Strategy

### Phase 1: Foundation (Week 1)
1. Extract NVS Storage Module
2. Extract Logging System
3. Extract Hardware Config
4. **Test thoroughly** - These are foundational

### Phase 2: Core Systems (Week 2)
1. Extract Audio System
2. Extract SD Card Operations
3. Extract Sleep & Power Management
4. **Test thoroughly** - Core functionality

### Phase 3: Networking (Week 3)
1. Extract WiFi Management
2. Extract MQTT Client
3. Extract MQTT Command Handlers
4. **Test thoroughly** - Network operations

### Phase 4: Web & OTA (Week 4)
1. Extract Web Management Interface
2. Extract OTA Update System
3. **Test thoroughly** - Critical for management

### Phase 5: Media & Display (Week 5)
1. Extract Media Management
2. Extract Display Operations (incremental)
3. **Test thoroughly** - Visual functionality

### Phase 6: Cleanup (Week 6)
1. Simplify main setup/loop
2. Review and optimize module interfaces
3. Final testing and documentation

---

## Key Principles

1. **Zero Breaking Changes:** Each extraction must maintain exact same behavior
2. **Incremental:** One module at a time, test after each
3. **Preserve Functionality:** Don't optimize or "improve" during extraction
4. **Clear Interfaces:** Each module should have minimal, well-defined public API
5. **Dependency Management:** Extract low-level modules first (NVS, Logging, Config)
6. **Testing:** After each extraction, verify:
   - Compiles successfully
   - All features work identically
   - No performance regressions
   - Memory usage unchanged

---

## Risk Assessment

**Low Risk (Start Here):**
- NVS Storage
- Logging System
- Hardware Config
- Audio System (well-isolated)

**Medium Risk:**
- SD Card Operations
- Sleep Management
- WiFi Management
- MQTT Client

**High Risk (Do Last):**
- MQTT Command Handlers (many dependencies)
- Web Interface (complex state)
- Auto Cycle Task (core logic)
- Main Setup/Loop (orchestration)

---

## Estimated Impact

**Before:** 10,293 lines in 1 file
**After:** ~10,293 lines across ~15-20 files
- Main file: ~1,500 lines (setup, loop, auto_cycle, orchestration)
- Module files: ~500-800 lines each
- Headers: ~100-200 lines each

**Benefits:**
- Easier navigation
- Better code organization
- Reduced merge conflicts
- Clearer module boundaries
- Easier testing of individual components
- Better maintainability


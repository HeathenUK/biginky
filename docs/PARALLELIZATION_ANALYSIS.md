# Dual-Core Parallelization Analysis for Per-Minute Wake Cycle

## Executive Summary

The ESP32-P4 has **2 CPU cores** that can run tasks simultaneously. Currently, most operations run sequentially on Core 0, leaving Core 1 idle. By parallelizing I/O-bound and CPU-intensive tasks, we can significantly reduce wake cycle time.

**PRIORITY: Per-minute wake cycle optimization** (most common case - wake, check MQTT, publish status, sleep)

## Current Wake Cycle Flow (Sequential)

### Per-Minute Wake (Non-Top-of-Hour) - **PRIORITY OPTIMIZATION TARGET**
1. **Time sync check** (~100-500ms if NTP needed)
2. **Load WiFi/MQTT credentials** (~10-50ms, NVS reads)
3. **WiFi connect** (~2-5 seconds) - **BOTTLENECK #1**
4. **MQTT connect** (~1-2 seconds) - **BOTTLENECK #2**
5. **Wait for retained messages** (~500ms delay)
6. **Check for messages** (~100ms)
7. **MQTT disconnect** (~50ms)
8. **Process commands** (if any, variable time)
9. **MQTT reconnect** (~1-2 seconds) - **BOTTLENECK #3 (WASTEFUL!)**
10. **Build status JSON** (~50-100ms, CPU-bound)
11. **Encrypt status** (~100-200ms, CPU-bound)
12. **Publish status** (~50-100ms)
13. **MQTT disconnect** (~50ms)
14. **WiFi disconnect** (~50ms)
15. **Sleep** (~60 seconds)

**Total: ~5-10 seconds** (mostly network I/O, but also wasteful reconnection)

**Key Issues:**
- **MQTT disconnects and reconnects just to publish status** - wastes 1-2 seconds!
- **Status JSON building/encryption happens sequentially** - could be done in parallel
- **WiFi connection is blocking** - Core 0 idle during network I/O

### Top-of-Hour Wake (Display Update)
1. **Time sync check** (~100-500ms)
2. **Display initialization** (~500ms)
3. **SD card mount** (if needed, ~200ms)
4. **Load media.txt/quotes.txt** (~100-300ms)
5. **PNG file read from SD** (~500-2000ms) - **BOTTLENECK**
6. **PNG decode to framebuffer** (~1000-3000ms) - **BOTTLENECK**
7. **Text placement analysis** (~500-1500ms) - **BOTTLENECK**
8. **Draw time/date overlay** (~200-500ms)
9. **Draw quote overlay** (~300-800ms)
10. **Display update (non-blocking)** (~20-30 seconds, hardware-bound)
11. **Wait for display refresh** (~20-30 seconds) - **MAJOR BOTTLENECK**
12. **Audio file read + playback** (~2000-5000ms) - **BOTTLENECK**
13. **WiFi connect** (~2-5 seconds)
14. **MQTT connect + thumbnail publish** (~2-4 seconds)
15. **Sleep**

**Total: ~30-50 seconds** (mostly waiting for display + sequential I/O)

## Parallelization Opportunities

### Core Assignment Strategy

**Core 0 (Main Control):**
- Time sync
- WiFi/MQTT connection management
- Display initialization (top-of-hour only)
- Sleep management
- Task coordination

**Core 1 (Background Processing):**
- Status JSON preparation (during WiFi/MQTT connect)
- Status encryption (during network I/O)
- SD card I/O operations (top-of-hour)
- PNG decoding (top-of-hour)
- Text placement analysis (top-of-hour)
- Thumbnail generation (top-of-hour)

### High-Impact Parallelization Opportunities (Per-Minute Cycle)

#### 1. **Keep MQTT Connected for Status Publish** (Save ~1-2 seconds) - **HIGHEST PRIORITY**
**Current:** MQTT connect → check messages → disconnect → reconnect → publish status → disconnect
**Optimized:** MQTT connect → check messages → publish status → disconnect

**Implementation:**
- Remove the MQTT disconnect/reconnect cycle
- Publish status while MQTT is still connected (after checking messages)
- Only disconnect after status is published

**Expected Savings:** 1-2 seconds per cycle (20-40% reduction in per-minute wake time)

#### 2. **Prepare Status JSON During WiFi/MQTT Connection** (Save ~150-300ms)
**Current:** WiFi connect → MQTT connect → build status JSON → encrypt → publish (sequential)
**Optimized:**
- Core 0: WiFi connect (blocking, network I/O)
- Core 1: Build status JSON + encrypt (CPU-bound, in parallel)
- Sync: Wait for both, then publish

**Implementation:**
- Create `statusPreparationTask` pinned to Core 1
- Start immediately when WiFi connection begins
- Task: Collect status data → build JSON → encrypt
- Store result in shared buffer
- Main task publishes when ready

**Expected Savings:** 150-300ms per cycle

#### 3. **Parallel Credential Loading** (Save ~10-50ms)
**Current:** Load WiFi credentials → load MQTT config (sequential NVS reads)
**Optimized:**
- Core 0: Load WiFi credentials
- Core 1: Load MQTT config (in parallel)
- Sync: Wait for both before connecting

**Expected Savings:** 10-50ms per cycle (small but easy win)

### High-Impact Parallelization Opportunities (Top-of-Hour Cycle)

#### 4. **PNG Decode While Display Initializes** (Save ~1-3 seconds)
**Current:** Display init → PNG read → PNG decode (sequential)
**Optimized:** 
- Core 0: Display initialization
- Core 1: PNG read + decode (in parallel)
- Sync: Wait for both to complete before drawing

**Implementation:**
```cpp
// Core 0: Initialize display
displaySPI.begin(...);
display.begin(...);

// Core 1: Load and decode PNG in parallel
xTaskCreatePinnedToCore(pngLoadAndDecodeTask, "png_decode", 16384, &decodeData, 5, &decodeTask, 1);
// ... display init continues ...

// Wait for PNG decode to complete
xTaskNotifyWait(0, 0, nullptr, portMAX_DELAY);
```

#### 2. **Text Placement Analysis During PNG Decode** (Save ~0.5-1.5 seconds)
**Current:** PNG decode → Text placement analysis (sequential)
**Optimized:**
- Core 1: PNG decode (CPU-intensive)
- Core 0: Text placement analysis (can run on partial framebuffer or in parallel)

**Note:** Text placement needs the full image, but we can:
- Start analysis as soon as first rows are decoded
- Or: Run analysis on Core 1 after decode completes, while Core 0 does other prep work

#### 3. **WiFi/MQTT During Display Refresh** (Save ~4-9 seconds)
**Current:** Display refresh (20-30s) → WiFi connect → MQTT → Thumbnail publish (sequential)
**Optimized:**
- Core 0: Trigger display update (non-blocking), then wait
- Core 1: WiFi connect + MQTT connect + thumbnail publish (during display refresh)
- Sync: Both complete before sleep

**Implementation:**
```cpp
// Core 0: Start display update (non-blocking)
display.update();  // Returns immediately

// Core 1: Do WiFi/MQTT work in parallel
xTaskCreatePinnedToCore(wifiMqttThumbnailTask, "wifi_mqtt", 16384, nullptr, 5, &wifiTask, 1);

// Core 0: Wait for display to finish
display.waitForUpdate();

// Wait for WiFi/MQTT task to complete
xTaskNotifyWait(0, 0, nullptr, portMAX_DELAY);
```

#### 4. **Audio Pre-load During Display Refresh** (Save ~1-2 seconds)
**Current:** Display refresh → Audio file read → Audio playback (sequential)
**Optimized:**
- Core 1: Pre-read audio file into buffer during display refresh
- Core 0: Wait for display, then play from buffer (faster)

**Implementation:**
```cpp
// During display refresh (Core 1):
String audioFile = getAudioForImage(g_lastImagePath);
uint8_t* audioBuffer = preloadAudioFile(audioFile);  // Read into PSRAM

// After display refresh (Core 0):
playAudioFromBuffer(audioBuffer);  // Instant playback
```

#### 5. **Thumbnail Generation During Display Refresh** (Save ~2-5 seconds)
**Current:** Display refresh → Thumbnail generation → MQTT publish (sequential)
**Optimized:**
- Core 1: Generate thumbnail from framebuffer during display refresh
- Core 0: Wait for display, then publish (thumbnail already ready)

**Note:** Thumbnail generation needs the framebuffer, but we can:
- Generate from framebuffer immediately after drawing (before display.update())
- Or: Generate during display refresh (framebuffer is stable)

#### 6. **Media.txt/Quotes.txt Pre-load** (Save ~100-300ms)
**Current:** Load config files on every top-of-hour wake
**Optimized:**
- Load once at startup/cold boot
- Cache in PSRAM (already done, but verify it's not reloaded unnecessarily)

#### 7. **Parallel SD Card Operations** (Save ~500-1000ms)
**Current:** Sequential SD reads (PNG, then audio, then thumbnail save)
**Optimized:**
- Core 0: Read PNG file
- Core 1: Read audio file (if known) in parallel
- Use separate SD card handles or queue SD operations

**Note:** SD card is single device, but we can:
- Pre-read next image/audio while current is processing
- Use buffering to overlap I/O with processing

## Implementation Priority

### Priority 1: Keep MQTT Connected for Status (Per-Minute - Highest Impact)
**Savings: ~1-2 seconds per per-minute cycle**
- Currently disconnects and reconnects MQTT just to publish status
- **Impact:** Reduces per-minute cycle from ~5-10s to ~4-8s (20-40% reduction)
- **Complexity:** Low (just remove disconnect/reconnect)
- **Risk:** Low (MQTT can handle multiple operations per connection)

**Implementation:**
- Remove `mqttDisconnect()` call after message check
- Publish status while MQTT is still connected
- Only disconnect after status publish completes

### Priority 2: Prepare Status During WiFi/MQTT Connection (Per-Minute - High Impact)
**Savings: ~150-300ms per per-minute cycle**
- Status JSON building and encryption is CPU-bound
- Can run in parallel with network I/O
- **Impact:** Reduces sequential processing time

**Implementation:**
- Create `statusPreparationTask` pinned to Core 1
- Start when WiFi connection begins
- Build JSON and encrypt in parallel
- Main task publishes when ready

### Priority 3: WiFi/MQTT During Display Refresh (Top-of-Hour - High Impact)
**Savings: ~4-9 seconds**
- Display refresh is 20-30 seconds (hardware-bound, can't speed up)
- WiFi/MQTT operations are 4-9 seconds (network I/O, can run in parallel)
- **Impact:** Reduces total cycle time from ~30-50s to ~25-35s

**Implementation:**
- Create `wifiMqttThumbnailTask` pinned to Core 1
- Start task immediately after `display.update()` (non-blocking)
- Task handles: WiFi connect → MQTT connect → Thumbnail publish
- Use `xTaskNotify` to signal completion
- Main task waits for both display and WiFi/MQTT to complete

### Priority 2: PNG Decode on Core 1 (High Impact)
**Savings: ~1-3 seconds**
- PNG decode is CPU-intensive (~1-3 seconds)
- Can run in parallel with display initialization
- **Impact:** Reduces sequential processing time

**Implementation:**
- Create `pngLoadAndDecodeTask` pinned to Core 1
- Task: SD read → PNG decode → signal completion
- Main task (Core 0): Display init → wait for PNG decode

### Priority 3: Audio Pre-load During Display Refresh (Medium Impact)
**Savings: ~1-2 seconds**
- Audio file read is I/O-bound (~1-2 seconds)
- Can pre-load during 20-30s display refresh
- **Impact:** Audio playback starts immediately after display refresh

**Implementation:**
- Create `audioPreloadTask` pinned to Core 1
- Start during display refresh
- Read audio file into PSRAM buffer
- Main task plays from buffer (instant)

### Priority 4: Text Placement Analysis Optimization (Medium Impact)
**Savings: ~0.5-1.5 seconds**
- Text placement is CPU-intensive
- Can run on Core 1 after PNG decode
- **Impact:** Frees Core 0 for other operations

**Implementation:**
- Move text placement analysis to Core 1 task
- Run after PNG decode completes
- Signal completion to Core 0

### Priority 5: Thumbnail Generation During Display Refresh (Lower Priority)
**Savings: ~2-5 seconds**
- Thumbnail generation is CPU-intensive
- Can run during display refresh
- **Impact:** Thumbnail ready when MQTT connects

**Note:** Already partially optimized (thumbnail can be generated before display.update())

## Detailed Implementation Plan

### Phase 1: Keep MQTT Connected (Priority 1 - Per-Minute)

**File:** `src/main.cpp` - `doMqttCheckCycle()`

**Current Flow:**
```cpp
mqttConnect();
mqttCheckMessages();
mqttDisconnect();  // ← WASTEFUL!
// ... process commands ...
mqttConnect();     // ← WASTEFUL RECONNECTION!
publishMQTTStatus();
mqttDisconnect();
```

**Optimized Flow:**
```cpp
mqttConnect();
mqttCheckMessages();
// ... process commands ...
publishMQTTStatus();  // ← Publish while still connected!
mqttDisconnect();
```

**Changes:**
1. Remove `mqttDisconnect()` after message check (line ~3088)
2. Remove `MQTTGuard` reconnection block (lines ~3118-3137)
3. Call `publishMQTTStatus()` directly after command processing
4. Only disconnect after status is published

**Expected Savings:** 1-2 seconds per per-minute cycle

### Phase 2: Parallel Status Preparation (Priority 2 - Per-Minute)

**File:** `src/main.cpp` - `doMqttCheckCycle()`

**Changes:**
1. Create `statusPreparationTask` function (pinned to Core 1)
2. Start task when WiFi connection begins
3. Task: Collect status data → build JSON → encrypt
4. Store encrypted status in shared buffer
5. Main task publishes when MQTT is connected

**Expected Savings:** 150-300ms per per-minute cycle

### Phase 3: WiFi/MQTT Parallelization (Top-of-Hour - Priority 3)

**File:** `src/main.cpp` - `doDisplayUpdateCycle()`

**Changes:**
1. Create `wifiMqttThumbnailTask` function (pinned to Core 1)
2. Start task immediately after `display.update()`
3. Task handles all WiFi/MQTT operations
4. Use semaphore/notification for synchronization
5. Main task waits for both display and task completion

**Expected Savings:** 4-9 seconds per top-of-hour cycle

### Phase 2: PNG Decode Parallelization (Priority 2)

**File:** `src/main.cpp` - `loadMediaForDisplay()`

**Changes:**
1. Create `pngLoadAndDecodeTask` function (pinned to Core 1)
2. Extract PNG read + decode into separate task
3. Start task in parallel with display initialization
4. Use shared framebuffer (protected by mutex if needed)
5. Signal completion when ready

**Expected Savings:** 1-3 seconds per top-of-hour cycle

### Phase 3: Audio Pre-load (Priority 3)

**File:** `src/main.cpp` - `doDisplayUpdateCycle()`

**Changes:**
1. Create `audioPreloadTask` function (pinned to Core 1)
2. Start during display refresh
3. Pre-read audio file into PSRAM buffer
4. Main task plays from buffer after display refresh

**Expected Savings:** 1-2 seconds per top-of-hour cycle

## Technical Considerations

### Thread Safety
- **Framebuffer access:** Display buffer is written by PNG decode, read by text placement
  - Solution: Use mutex or ensure sequential access (decode completes before text placement)
- **SD card access:** Single device, but can queue operations
  - Solution: Use FreeRTOS queue for SD operations, or ensure sequential access
- **WiFi/MQTT:** Already thread-safe (ESP-IDF handles this)

### Memory Management
- **PSRAM:** Use for large buffers (audio, thumbnail)
- **Stack sizes:** Increase for parallel tasks (16KB+)
- **Heap:** Monitor fragmentation with parallel allocations

### Task Priorities
- **Core 0 (main):** Priority 5 (current)
- **Core 1 (background):** Priority 5 (same, to prevent starvation)
- **Consider:** Lower priority for background tasks to ensure main task responsiveness

### Core Affinity
- Use `xTaskCreatePinnedToCore(task, name, stack, param, priority, handle, core)`
- Core 0: Main control flow
- Core 1: Background processing

## Expected Total Savings

### Per-Minute Cycle (PRIORITY)
- **Current:** ~5-10 seconds
- **After Priority 1 (keep MQTT connected):** ~4-8 seconds (save 1-2s)
- **After Priority 1+2 (parallel status prep):** ~3.8-7.7 seconds (save 150-300ms more)
- **After Priority 1+2+3 (parallel credentials):** ~3.7-7.6 seconds (save 10-50ms more)
- **Total potential savings:** ~1.2-2.4 seconds (20-40% reduction)

### Top-of-Hour Cycle
- **Current:** ~30-50 seconds
- **After Priority 3 (WiFi/MQTT during display):** ~25-35 seconds (save 4-9s)
- **After Priority 3+4 (PNG decode parallel):** ~22-30 seconds (save 1-3s more)
- **After Priority 3+4+5 (audio preload):** ~20-28 seconds (save 1-2s more)
- **Total potential savings:** ~6-14 seconds (20-40% reduction)

## Risks and Mitigations

### Risk 1: Race Conditions
- **Mitigation:** Use FreeRTOS synchronization primitives (semaphores, mutexes, notifications)

### Risk 2: Stack Overflow
- **Mitigation:** Increase stack sizes for parallel tasks, monitor with FreeRTOS stack watermark

### Risk 3: Memory Fragmentation
- **Mitigation:** Pre-allocate buffers, use PSRAM for large allocations

### Risk 4: SD Card Conflicts
- **Mitigation:** Ensure sequential SD access or use proper locking

### Risk 5: Display Buffer Corruption
- **Mitigation:** Ensure PNG decode completes before text drawing, or use mutex

## Next Steps

1. **Implement Priority 1** (WiFi/MQTT during display refresh) - Highest impact, lowest risk
2. **Measure actual savings** - Add timing measurements
3. **Implement Priority 2** (PNG decode parallelization) - High impact, medium complexity
4. **Iterate and optimize** - Fine-tune based on measurements

## Code Structure Recommendations

### New Module: `parallel_tasks.h/cpp`
- Encapsulate all parallel task creation and synchronization
- Provide clean API for main code
- Handle task lifecycle (create, wait, cleanup)

### Example API:
```cpp
// Start WiFi/MQTT task in parallel with display refresh
ParallelTaskHandle startWifiMqttThumbnailTask();

// Wait for parallel task to complete
bool waitForParallelTask(ParallelTaskHandle handle, uint32_t timeoutMs);

// Start PNG decode task in parallel
ParallelTaskHandle startPngDecodeTask(const String& imagePath);

// Wait for PNG decode to complete
bool waitForPngDecode(ParallelTaskHandle handle, uint32_t timeoutMs);
```


# Phase 1 Test Plan - Dual Wake Architecture

## Overview
Phase 1 implements the foundation for dual-wake architecture:
- **Hourly Wake (XX:00)**: Full display update cycle
- **SMS Check Wake (every minute)**: Minimal wake to check for new SMS

## Test Scenarios

### Test 1: Cold Boot (First Time)
**Objective**: Verify system initializes correctly on first boot

**Steps**:
1. Power on device (cold boot)
2. Observe serial output
3. Check if wake type detection runs
4. Verify time sync happens (LTE or WiFi/NTP)
5. After time is set, check if wake type is correctly determined

**Expected Behavior**:
- System initializes normally
- Wake type detection runs after time is set
- If time is invalid, defaults to SMS check wake
- If time is valid and minute == 0, routes to hourly cycle
- If time is valid and minute != 0, routes to SMS check

**Potential Issue**: Wake type detection happens BEFORE time sync in current code
**Fix Needed**: Move wake type detection to AFTER time sync

### Test 2: Hourly Wake (XX:00)
**Objective**: Verify hourly wake triggers full cycle

**Steps**:
1. Set device time to XX:00 (e.g., 14:00:00)
2. Trigger wake (or wait for scheduled wake)
3. Observe serial output
4. Verify routing to hourly cycle
5. Check that full display update happens
6. Verify sleep duration is ~60 minutes

**Expected Behavior**:
- Wake type detection shows "HOURLY (XX:00) - Full cycle"
- Routes to auto_cycle_task (full cycle)
- Display updates with new image/quote
- Sleeps for ~60 minutes (until next hour)

**Serial Output to Look For**:
```
=== Wake Type Detection ===
Current time: 14:00:00
Wake type: HOURLY (XX:00) - Full cycle
===========================
```

### Test 3: SMS Check Wake (Non-Hourly)
**Objective**: Verify SMS check wake works correctly

**Steps**:
1. Set device time to non-hourly (e.g., 14:23:45)
2. Trigger wake
3. Observe serial output
4. Verify routing to SMS check
5. Check LTE module initialization
6. Verify registration check/attempt
7. Verify SMS check happens
8. Verify sleep duration is ~60 seconds

**Expected Behavior**:
- Wake type detection shows "SMS CHECK - Minimal wake"
- Routes to performSMSCheckOnly()
- Minimal initialization (no display, SD, audio)
- LTE module checked/initialized
- Registration verified or attempted
- SMS checked for new messages
- Sleeps for ~60 seconds (until next minute)

**Serial Output to Look For**:
```
=== Wake Type Detection ===
Current time: 14:23:45
Wake type: SMS CHECK - Minimal wake
===========================

Routing to SMS check only (minimal wake)...
=== SMS Check Only (Minimal Wake) ===
Checking if LTE module is on...
Checking registration...
Checking for new SMS...
=== SMS Check Complete ===
SMS check complete, sleeping for 60 seconds
```

### Test 4: LTE Module Already On
**Objective**: Verify SMS check works when module is already powered

**Steps**:
1. Power on LTE module manually (or from previous wake)
2. Trigger SMS check wake
3. Verify module detection works
4. Verify registration check works
5. Verify SMS check completes quickly

**Expected Behavior**:
- Module detected as "already on"
- Quick registration check
- SMS check completes
- Fast return to sleep

### Test 5: LTE Module Not Registered
**Objective**: Verify registration attempt when module is not registered

**Steps**:
1. Ensure LTE module is on but not registered
2. Trigger SMS check wake
3. Verify registration check detects "not registered"
4. Verify brief registration attempt happens
5. Check if registration succeeds
6. Verify SMS check proceeds after registration

**Expected Behavior**:
- Registration check shows "not registered"
- Brief registration attempt triggered
- If successful, SMS check proceeds
- If failed, graceful exit with error message

### Test 6: Time Invalid
**Objective**: Verify behavior when time is not set

**Steps**:
1. Boot device without time sync
2. Trigger wake
3. Verify wake type detection handles invalid time
4. Check default behavior

**Expected Behavior**:
- Wake type detection shows "Time invalid - defaulting to SMS check wake"
- Routes to SMS check (safe default)
- Uses fallback sleep duration

### Test 7: Sleep Duration Calculation
**Objective**: Verify sleep duration is calculated correctly

**Test 7a: Hourly Wake Sleep Duration**
- Current time: 14:00:00
- Expected sleep: 3600 seconds (60 minutes)
- Verify calculation

**Test 7b: SMS Check Sleep Duration**
- Current time: 14:23:45
- Expected sleep: 15 seconds (until 14:24:00)
- Verify calculation

**Test 7c: Edge Case - At :00 but not hourly**
- Current time: 14:23:00
- Expected sleep: 60 seconds (until 14:24:00)
- Verify calculation

## Known Issues to Check

### Issue 1: Wake Type Detection Timing
**Problem**: Wake type detection happens before time sync
**Location**: setup() function
**Impact**: May incorrectly determine wake type if time is not yet set
**Fix**: Move wake type detection to AFTER time sync completes

### Issue 2: SMS Check Path Initialization
**Problem**: Serial1 may be initialized multiple times
**Location**: performSMSCheckOnly()
**Impact**: Potential conflicts if Serial1 was already initialized
**Fix**: Check if Serial1 is already initialized before reinitializing

### Issue 3: LTE Module Power State
**Problem**: Module may be powered off between SMS checks
**Location**: sleepNowSeconds() powers off module
**Impact**: SMS check may fail if module needs to power on
**Fix**: Phase 2 will address keeping module powered

## Debugging Tips

1. **Enable Serial Output**: Make sure Serial.begin() happens early
2. **Check Time**: Use 'r' command to check current time
3. **Monitor Sleep**: Watch serial output before sleep to see calculated duration
4. **LTE Status**: Use 'y' command to check LTE module status
5. **Wake Cause**: Check esp_sleep_get_wakeup_cause() to verify wake source

## Success Criteria

✅ Wake type detection works correctly
✅ Hourly wake routes to full cycle
✅ SMS check wake routes to minimal path
✅ Sleep duration calculated correctly for both paths
✅ LTE registration handled properly before SMS check
✅ Graceful error handling when LTE fails
✅ No compilation errors
✅ No runtime crashes

## Next Steps After Testing

1. Fix any issues found during testing
2. Optimize SMS check path (Phase 2)
3. Add SMS command processing (Phase 3)
4. Refactor hourly cycle logic (Phase 4)

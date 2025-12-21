# Dual Wake Architecture Proposal

## Overview
- **Hourly Wake (XX:00)**: Full cycle - display update, image/quote change, audio beep
- **Minute Wake (every minute)**: SMS check only - minimal operations, fast return to sleep

## Design Approach

### 1. Time-Based Wake Decision
On every wake, check the current time:
- If `minute == 0` (top of hour): **Hourly Cycle**
- Otherwise: **SMS Check Only**

### 2. Hourly Cycle (Full Update)
**Operations:**
- Full initialization (display, SD card, etc.)
- Load next image/quote from media.txt
- Update display with new image + quote overlay
- Play audio beep (if configured)
- Check SMS (as part of full cycle)
- Sleep for ~60 minutes (until next hour)

**Sleep Duration:** Calculate to next hour (XX:00)

### 3. SMS Check Only (Minimal Wake)
**Operations:**
- Minimal initialization (only what's needed for LTE)
- Power on LTE module (if not already on)
- Quick registration check (use existing registration if possible)
- Check for new SMS messages
- Parse and act on commands if new messages found
- Sleep for ~60 seconds (until next minute)

**Sleep Duration:** Calculate to next minute (XX:MM+1)

### 4. State Management

**RTC Memory Variables:**
- `lastWakeType`: Track last wake type (0=SMS, 1=Hourly)
- `lastSMSCheckTime`: Timestamp of last SMS check
- `lteModuleState`: Track if LTE module was left on (to avoid power cycling)

**LTE Module State:**
- **Option A (Recommended)**: Keep module powered between SMS checks
  - Power on once at first SMS check
  - Leave powered on (module has low-power modes)
  - Only power cycle if registration lost
  - Pros: Faster SMS checks, less power cycling
  - Cons: Slightly higher idle power

- **Option B**: Power cycle every SMS check
  - Power on → Check SMS → Power off
  - Pros: Lower idle power
  - Cons: Slower, more wear on module

### 5. SMS Command Processing

**Command Format (suggested):**
- `DISPLAY <image_name>` - Change display to specific image
- `QUOTE <quote_id>` - Show specific quote
- `REBOOT` - Reboot device
- `STATUS` - Send status SMS back
- `SLEEP <minutes>` - Change sleep duration temporarily

**Implementation:**
- Parse SMS text for commands
- Execute commands immediately
- Send acknowledgment SMS if sender number is known

### 6. Sleep Duration Calculation

```cpp
uint32_t calculateSleepDuration(bool isHourlyWake) {
    time_t now = time(nullptr);
    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);
    
    if (isHourlyWake) {
        // Sleep until next hour (XX:00)
        uint32_t minutesUntilNextHour = 60 - tm_utc.tm_min;
        return minutesUntilNextHour * 60;
    } else {
        // Sleep until next minute
        uint32_t secondsUntilNextMinute = 60 - tm_utc.tm_sec;
        // Add small buffer (5 seconds) to ensure we wake at start of minute
        return secondsUntilNextMinute + 5;
    }
}
```

### 7. Implementation Structure

**Main Loop:**
```cpp
void setup() {
    // ... initialization ...
    
    // Check wake type
    time_t now = time(nullptr);
    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);
    
    bool isHourlyWake = (tm_utc.tm_min == 0);
    
    if (isHourlyWake) {
        performHourlyCycle();
    } else {
        performSMSCheckOnly();
    }
    
    // Calculate and enter sleep
    uint32_t sleepDuration = calculateSleepDuration(isHourlyWake);
    sleepNowSeconds(sleepDuration);
}
```

**Functions:**
- `performHourlyCycle()` - Full display update cycle
- `performSMSCheckOnly()` - Minimal SMS check
- `checkAndProcessSMS()` - Check for new SMS and process commands
- `processSMSCommand(String command)` - Execute SMS commands

### 8. Power Optimization

**SMS Check Path:**
- Skip display initialization
- Skip SD card mount (unless needed for command processing)
- Skip audio initialization
- Minimal serial output
- Fast LTE module wake (if kept powered)

**Hourly Path:**
- Full initialization (as current)
- All features enabled

### 9. Error Handling

**SMS Check Failures:**
- If LTE module fails to respond: Log error, sleep normally
- If registration fails: Try brief registration, if fails sleep normally
- If SMS check fails: Sleep normally, retry next minute

**Time Sync:**
- If time invalid: Use fallback sleep duration (60 seconds)
- Try to sync time on hourly wake (more time available)

### 10. Testing Strategy

1. **Test hourly wake**: Verify full cycle works at XX:00
2. **Test minute wake**: Verify SMS check works at other times
3. **Test command processing**: Send test SMS commands
4. **Test power consumption**: Measure current draw during SMS checks
5. **Test LTE module state**: Verify module stays registered between checks

## Migration Path

1. Add wake type detection to `setup()`
2. Extract SMS check logic into `performSMSCheckOnly()`
3. Extract hourly cycle into `performHourlyCycle()`
4. Update sleep duration calculation
5. Add SMS command processing
6. Test and optimize power consumption

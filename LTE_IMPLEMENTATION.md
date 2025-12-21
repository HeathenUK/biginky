# SIMCom A7683E LTE Module Implementation

## Overview

This document describes the implementation of support for the Pimoroni Clipper LTE 4G Breakout board (SIMCom A7683E module) in the biginky project.

## What Was Implemented

### 1. Library Structure ✅
- **Location**: `lib/simcom_a7683e/`
- **Files Created**:
  - `SIMCom_A7683E.h` - Header file with class definition
  - `SIMCom_A7683E.cpp` - Implementation file
  - `library.json` - PlatformIO library metadata

### 2. Core Features ✅
- AT command communication via UART
- Module initialization and reset control
- Network registration status monitoring
- Signal quality (RSSI) reporting
- SIM card ICCID retrieval
- APN configuration
- PPP connection initiation (platform-specific implementation needed for full PPP)

### 3. Integration into Main Application ✅
- **Pin Definitions**: Added to `main_esp32p4_test.cpp`
  - `PIN_LTE_RST` (default: GPIO35)
  - `PIN_LTE_NETLIGHT` (default: GPIO34)
  - `PIN_LTE_RX` (default: GPIO33)
  - `PIN_LTE_TX` (default: GPIO32)

- **Configuration Management**: 
  - APN stored in NVS (Non-Volatile Storage) similar to WiFi credentials
  - Functions: `lteLoadAPN()`, `lteSaveAPN()`, `lteClearAPN()`

- **User Commands**:
  - `j` - Initialize LTE module
  - `J` - Set/configure APN
  - `k` - Connect to LTE network
  - `K` - Disconnect from LTE network
  - `y` - Show LTE status

### 4. Functions Implemented ✅
- `lteInit()` - Initialize the module
- `lteConnect()` - Connect to cellular network
- `lteDisconnect()` - Disconnect from network
- `lteStatus()` - Display comprehensive status information
- `lteSetAPN()` - Configure APN via serial input
- `lteLoadAPN()` - Load saved APN from NVS
- `lteSaveAPN()` - Save APN to NVS
- `lteClearAPN()` - Clear saved APN

## Usage

### Initial Setup

1. **Configure APN**:
   ```
   Press 'J' in serial monitor
   Enter your carrier's APN (e.g., "internet", "data", "broadband")
   ```

2. **Initialize Module**:
   ```
   Press 'j' to initialize the LTE module
   ```

3. **Connect**:
   ```
   Press 'k' to connect to the cellular network
   ```

4. **Check Status**:
   ```
   Press 'y' to view connection status, signal quality, and SIM info
   ```

### APN Examples

Common APN values by carrier:
- **Generic**: `internet`, `data`, `broadband`
- **Carrier-specific**: Check with your mobile carrier
- **UK carriers**: Often `internet` or carrier-specific like `three.co.uk`

## Pin Configuration

Default pins match Pimoroni Pico Plus 2 W SP/CE connector:
- **RESET**: GPIO35
- **NETLIGHT**: GPIO34  
- **RX**: GPIO33 (module TX)
- **TX**: GPIO32 (module RX)

These can be overridden via build flags in `platformio.ini`:
```ini
build_flags = 
    -DPIN_LTE_RST=35
    -DPIN_LTE_NETLIGHT=34
    -DPIN_LTE_RX=33
    -DPIN_LTE_TX=32
```

## Build Configuration

LTE support is enabled by default. To disable:
```ini
build_flags = 
    -DDISABLE_LTE=1
```

Or enable explicitly:
```ini
build_flags = 
    -DENABLE_LTE_TEST=1
```

## Current Limitations

1. **PPP Implementation**: The PPP connection is initiated but full network stack integration requires platform-specific work:
   - **ESP32-P4**: Would need ESP-IDF `esp_netif_ppp` setup for full integration
   - **RP2350**: Would need PPP library or custom implementation

2. **Network Stack**: Currently the module connects and reports IP, but full TCP/IP stack integration needs additional work to use the connection for HTTP requests, NTP, etc.

## Next Steps (Future Enhancements)

1. **Full PPP Integration**:
   - Implement proper ESP-IDF PPP netif for ESP32-P4
   - Add PPP library support for RP2350
   - Integrate with existing WiFiClient/HTTPClient code

2. **Network Stack Integration**:
   - Make LTE connection usable for NTP sync
   - Enable HTTP requests over LTE
   - Add automatic fallback WiFi → LTE

3. **Power Management**:
   - Add sleep mode support
   - Implement power cycling
   - Add low-power modes

4. **Enhanced Status**:
   - Add network operator name
   - Add data usage tracking
   - Add connection quality metrics

## Testing

To test the implementation:

1. Connect Clipper breakout to your board via SP/CE connector
2. Insert SIM card
3. Connect antenna
4. Build and flash firmware
5. Use serial commands to configure and connect

## References

- [Pimoroni Clipper Product Page](https://shop.pimoroni.com/products/clipper-breakout)
- [MicroPython Driver](https://github.com/pimoroni/pimoroni-pico-rp2350/blob/feature/can-haz-ppp-plz/micropython/modules_py/lte.py)
- [SIMCom A7683E AT Command Manual](https://www.simcom.com/product/A7683E.html)

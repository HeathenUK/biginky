# GitHub Pages Web UI Authentication

## Overview

The GitHub Pages web UI uses HMAC-SHA256 authentication to secure MQTT communication with the device. **The password is NEVER transmitted** - only HMAC signatures are sent.

## Architecture

- **Password Storage**: Stored in NVS (non-volatile storage) on the device, used as HMAC key
- **Authentication Method**: HMAC-SHA256 message signing
- **All-or-Nothing**: If password is configured, ALL web UI commands require valid HMAC signatures
- **Status/Thumbnail Verification**: Device signs status/thumbnail messages with HMAC, allowing UI to verify password correctness

## Initial Setup

1. **Set Password via Local WiFi UI**:
   - Connect to device's local WiFi interface
   - Navigate to Settings
   - Use `/api/auth/password` endpoint:
     ```json
     POST /api/auth/password
     {
       "password": "your-secure-password-here"
     }
     ```
   - Password must be at least 8 characters

2. **Check Password Status**:
   ```json
   GET /api/auth/status
   ```
   Returns: `{"password_configured": true}` or `{"password_configured": false}`

3. **Boot Behavior**:
   - If password is NOT set at boot, device prints warning message
   - GitHub Pages UI will NOT work until password is configured
   - Device continues normal operation, but web UI commands are rejected

## HMAC Implementation

### Computing HMAC (JavaScript)

```javascript
async function computeHMAC(message, password) {
    // Convert password and message to Uint8Array
    const encoder = new TextEncoder();
    const keyData = encoder.encode(password);
    const messageData = encoder.encode(message);
    
    // Import password as HMAC key
    const key = await crypto.subtle.importKey(
        'raw',
        keyData,
        { name: 'HMAC', hash: 'SHA-256' },
        false,
        ['sign']
    );
    
    // Sign the message
    const signature = await crypto.subtle.sign('HMAC', key, messageData);
    
    // Convert to hex string
    const hashArray = Array.from(new Uint8Array(signature));
    const hashHex = hashArray.map(b => b.toString(16).padStart(2, '0')).join('');
    return hashHex;
}
```

### Sending Commands with HMAC

1. **Build JSON message** (without `hmac` field)
2. **Compute HMAC** of the message using the password
3. **Add `hmac` field** to JSON with the computed signature
4. **Send via MQTT** to `devices/web-ui/cmd`

Example:
```javascript
// 1. Build message without hmac
const message = JSON.stringify({
    command: "canvas_display",
    width: 800,
    height: 600,
    pixelData: base64Data,
    compressed: true
});

// 2. Compute HMAC
const hmac = await computeHMAC(message, password);

// 3. Add hmac field
const messageWithHMAC = JSON.stringify({
    command: "canvas_display",
    width: 800,
    height: 600,
    pixelData: base64Data,
    compressed: true,
    hmac: hmac
});

// 4. Send via MQTT
mqttClient.publish('devices/web-ui/cmd', messageWithHMAC);
```

### Verifying Status/Thumbnail Messages

Device signs status and thumbnail messages with HMAC. UI can verify:

```javascript
async function verifyMessage(message, providedHMAC, password) {
    // Extract hmac field from message
    const json = JSON.parse(message);
    const messageWithoutHMAC = JSON.stringify({
        // ... all fields except hmac
    });
    
    // Compute expected HMAC
    const expectedHMAC = await computeHMAC(messageWithoutHMAC, password);
    
    // Constant-time comparison
    return expectedHMAC === providedHMAC;
}
```

## Message Format

### Commands (UI → Device)

All commands must include `hmac` field:

```json
{
  "command": "canvas_display",
  "width": 800,
  "height": 600,
  "pixelData": "...",
  "compressed": true,
  "hmac": "abc123..."
}
```

### Status Messages (Device → UI)

Device includes `hmac` field:

```json
{
  "timestamp": 1234567890,
  "current_time": "12:34:56",
  "next_media": {...},
  "next_wake": "13:00",
  "sleep_interval_minutes": 1,
  "connected": true,
  "hmac": "def456..."
}
```

### Thumbnail Messages (Device → UI)

Device includes `hmac` field:

```json
{
  "width": 400,
  "height": 300,
  "format": "rgb888",
  "data": "...",
  "hmac": "ghi789..."
}
```

## Security Notes

1. **Password Never Transmitted**: Password is only used locally to compute HMAC signatures
2. **HMAC-SHA256**: Uses industry-standard HMAC-SHA256 for message authentication
3. **Constant-Time Comparison**: Device uses constant-time comparison to prevent timing attacks
4. **All-or-Nothing**: If password is set, ALL commands require valid HMAC (no fallback)
5. **NVS Storage**: Password stored in encrypted NVS (ESP32 hardware encryption)

## Error Handling

- **No Password Set**: Device rejects all web UI commands with error message
- **Invalid HMAC**: Device rejects command with "HMAC validation failed"
- **Missing HMAC**: Device rejects command (password required but HMAC missing)

## Testing Password Correctness

The UI can test if password is correct by:
1. Sending a test command (e.g., `{"command":"ping"}`) with HMAC
2. If command succeeds, password is correct
3. Alternatively, verify HMAC on received status/thumbnail messages

## Implementation Checklist

- [ ] Implement `computeHMAC()` function in JavaScript
- [ ] Add password input field to GitHub Pages UI
- [ ] Store password securely (localStorage with encryption, or prompt each time)
- [ ] Compute HMAC for all outgoing commands
- [ ] Verify HMAC on incoming status/thumbnail messages
- [ ] Handle authentication errors gracefully
- [ ] Add password setup instructions to UI


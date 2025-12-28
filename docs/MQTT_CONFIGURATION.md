# MQTT Configuration Guide

This guide explains how to configure the MQTT broker settings for your own deployment.

## Quick Start

1. **Copy the example configuration:**
   ```bash
   cp docs/js/mqtt-config.example.js docs/js/mqtt-config.js
   ```

2. **Edit `docs/js/mqtt-config.js`** with your MQTT broker settings:
   - Update `broker` with your MQTT broker hostname
   - Update `port` with the correct port (443 for WebSocket over SSL, 8883 for SSL, 1883 for non-SSL)
   - Update `topics` to match your device's topic structure
   - Update `auth.token` with your MQTT authentication token/username

3. **The configuration will be automatically loaded** when the page loads.

## Configuration Options

### Broker Settings

- **`broker`**: MQTT broker hostname (e.g., `'mqtt.flespi.io'`, `'broker.hivemq.com'`, or your own broker)
- **`port`**: MQTT broker port
  - `443` - WebSocket over SSL (most common for web apps)
  - `8883` - MQTT over SSL
  - `1883` - MQTT without SSL (not recommended for production)
- **`path`**: WebSocket path (usually `'/mqtt'` for most brokers, `'/'` for some)

### Topics

Configure the MQTT topics your devices use:

- **`topics.command`**: Topic for sending commands to devices
- **`topics.status`**: Topic for receiving status updates from devices
- **`topics.thumbnail`**: Topic for receiving thumbnail updates from devices
- **`topics.media`**: Topic for receiving media mappings from devices

### Authentication

- **`auth.token`**: Your MQTT authentication token or username
  - For Flespi.io: Use a restricted token with publish-only permissions to the command topic
  - For other brokers: This might be your username
- **`auth.password`**: Password (if required by your broker, leave `null` if using token)
- **`auth.useTokenAsUsername`**: Set to `true` for Flespi.io and similar brokers, `false` if your broker uses separate username/password

### Connection Settings

- **`connection.maxReconnectAttempts`**: Maximum number of reconnection attempts (default: 10)
- **`connection.initialReconnectDelay`**: Initial delay before first reconnection attempt in milliseconds (default: 1000)
- **`connection.autoReconnect`**: Enable auto-reconnect on connection loss (default: `true`)

## Example Configurations

### Flespi.io

```javascript
const MQTT_CONFIG = {
    broker: 'mqtt.flespi.io',
    port: 443,
    path: '/mqtt',
    topics: {
        command: 'devices/web-ui/cmd',
        status: 'devices/web-ui/status',
        thumbnail: 'devices/web-ui/thumb',
        media: 'devices/web-ui/media'
    },
    auth: {
        token: 'YOUR_FLESPI_TOKEN_HERE',
        password: null,
        useTokenAsUsername: true
    },
    connection: {
        maxReconnectAttempts: 10,
        initialReconnectDelay: 1000,
        autoReconnect: true
    }
};
```

### HiveMQ Public Broker

```javascript
const MQTT_CONFIG = {
    broker: 'broker.hivemq.com',
    port: 8000,  // WebSocket port
    path: '/mqtt',
    topics: {
        command: 'your-device/cmd',
        status: 'your-device/status',
        thumbnail: 'your-device/thumb',
        media: 'your-device/media'
    },
    auth: {
        token: null,  // No auth for public broker
        password: null,
        useTokenAsUsername: false
    },
    connection: {
        maxReconnectAttempts: 10,
        initialReconnectDelay: 1000,
        autoReconnect: true
    }
};
```

### Custom MQTT Broker

```javascript
const MQTT_CONFIG = {
    broker: 'mqtt.example.com',
    port: 443,
    path: '/mqtt',
    topics: {
        command: 'devices/web-ui/cmd',
        status: 'devices/web-ui/status',
        thumbnail: 'devices/web-ui/thumb',
        media: 'devices/web-ui/media'
    },
    auth: {
        token: 'your-username',
        password: 'your-password',
        useTokenAsUsername: false
    },
    connection: {
        maxReconnectAttempts: 10,
        initialReconnectDelay: 1000,
        autoReconnect: true
    }
};
```

## Security Notes

- **Never commit `mqtt-config.js` to version control** - it contains your authentication credentials
- The file is automatically ignored by `.gitignore`
- Use restricted tokens with minimal permissions when possible
- For production, consider using environment variables or a secure configuration service

## Troubleshooting

### Connection Fails

1. Check that your broker hostname and port are correct
2. Verify the WebSocket path (`/mqtt` is common, but some brokers use `/`)
3. Ensure your authentication token/credentials are valid
4. Check browser console for detailed error messages

### Topics Not Working

1. Verify your topic names match exactly (case-sensitive)
2. Ensure your device is publishing to the correct topics
3. Check that your MQTT token has subscribe permissions for status/thumbnail/media topics

### Authentication Errors

1. For Flespi.io: Ensure `useTokenAsUsername` is `true`
2. For username/password brokers: Set `useTokenAsUsername` to `false` and provide both token (username) and password
3. Verify your token/credentials have the correct permissions


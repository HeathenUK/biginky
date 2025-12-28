/**
 * MQTT Configuration
 * 
 * To configure this for your own setup:
 * 1. Copy this file to mqtt-config.js
 * 2. Edit the values below to match your MQTT broker and topics
 * 3. The main config.js will automatically load your custom settings
 * 
 * Note: mqtt-config.js is in .gitignore, so your personal settings won't be committed
 */

// MQTT Broker Configuration
// This must be assigned to window.MQTT_CONFIG so config.js can access it
window.MQTT_CONFIG = {
    // MQTT broker hostname (e.g., 'mqtt.flespi.io', 'broker.hivemq.com', or your own broker)
    broker: 'mqtt.flespi.io',
    
    // MQTT broker port (typically 1883 for non-SSL, 8883 for SSL, or 443 for WebSocket over SSL)
    port: 443,
    
    // MQTT WebSocket path (usually '/mqtt' for most brokers, '/' for some)
    path: '/mqtt',
    
    // MQTT Topics
    topics: {
        // Topic for sending commands to devices
        command: 'devices/web-ui/cmd',
        
        // Topic for receiving status updates from devices
        status: 'devices/web-ui/status',
        
        // Topic for receiving thumbnail updates from devices
        thumbnail: 'devices/web-ui/thumb',
        
        // Topic for receiving media mappings from devices
        media: 'devices/web-ui/media'
    },
    
    // MQTT Authentication
    // For Flespi.io: Use a restricted token with publish-only permissions to the command topic
    // For other brokers: Set username/password or leave null if no auth required
    auth: {
        // Token/username for MQTT authentication
        // For Flespi.io: This is your restricted token
        // For other brokers: This might be a username
        token: 'YOUR_RESTRICTED_TOKEN_HERE',
        
        // Password (if required by your broker, leave null if using token)
        password: null,
        
        // Use token as username (for Flespi.io and similar brokers)
        // Set to false if your broker uses separate username/password
        useTokenAsUsername: true
    },
    
    // Connection Settings
    connection: {
        // Maximum number of reconnection attempts
        maxReconnectAttempts: 10,
        
        // Initial delay before first reconnection attempt (milliseconds)
        initialReconnectDelay: 1000,
        
        // Enable auto-reconnect on connection loss
        autoReconnect: true
    }
};


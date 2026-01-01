// Configuration system - loads custom config if available, otherwise uses defaults

// Default MQTT configuration (used if mqtt-config.js is not found)
const DEFAULT_MQTT_CONFIG = {
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
        token: 'WdTjkCQjNXodmMWLSDWhAvrE6dDmcoQY1V47HSd3M7mb9P4HI3Ph8nTnA18MioBn',
        password: null,
        useTokenAsUsername: true
    },
    connection: {
        maxReconnectAttempts: 10,
        initialReconnectDelay: 1000,
        autoReconnect: true
    }
};

// Load custom configuration if available, otherwise use defaults
// This allows users to create mqtt-config.js (from mqtt-config.example.js) with their own settings
// The custom config is loaded via a script tag before this file, so we check for it
let MQTT_CONFIG = DEFAULT_MQTT_CONFIG;

// Function to merge configs (deep merge for nested objects)
function mergeConfig(defaultConfig, customConfig) {
    const merged = { ...defaultConfig };
    if (customConfig.broker) merged.broker = customConfig.broker;
    if (customConfig.port) merged.port = customConfig.port;
    if (customConfig.path) merged.path = customConfig.path;
    if (customConfig.topics) {
        merged.topics = { ...defaultConfig.topics, ...customConfig.topics };
    }
    if (customConfig.auth) {
        merged.auth = { ...defaultConfig.auth, ...customConfig.auth };
    }
    if (customConfig.connection) {
        merged.connection = { ...defaultConfig.connection, ...customConfig.connection };
    }
    return merged;
}

// Check for custom config (loaded via script tag before this file)
// mqtt-config.js should be loaded before this file, so window.MQTT_CONFIG should be available if it exists
if (typeof window !== 'undefined' && window.MQTT_CONFIG) {
    MQTT_CONFIG = mergeConfig(DEFAULT_MQTT_CONFIG, window.MQTT_CONFIG);
    // console.log('Loaded custom MQTT configuration from mqtt-config.js');
} else {
    // console.log('Using default MQTT configuration. To customize, create mqtt-config.js from mqtt-config.example.js');
}

// Export configuration constants for backward compatibility
const MQTT_BROKER = MQTT_CONFIG.broker;
const MQTT_PORT = MQTT_CONFIG.port;
const MQTT_TOPIC = MQTT_CONFIG.topics.command;
const MQTT_TOPIC_STATUS = MQTT_CONFIG.topics.status;
const MQTT_TOPIC_THUMB = MQTT_CONFIG.topics.thumbnail;
const MQTT_TOPIC_MEDIA = MQTT_CONFIG.topics.media;
const EMBEDDED_TOKEN = MQTT_CONFIG.auth.token;

// Global state variables
let mqttClient = null;
let isConnected = false;
let webUIPassword = null;  // Stored password for HMAC computation
let sessionEncryptionKey = null;  // Session key for encrypting stored password
let isBusy = false;  // Busy state for command processing
let busyTimeoutId = null;  // Timeout ID for busy state
let busyCountdownInterval = null;  // Interval for updating countdown
let nextWakeTime = null;  // Next wake time from status message (Date object)
let sleepIntervalMinutes = null;  // Sleep interval in minutes from status message
let autoReconnectEnabled = MQTT_CONFIG.connection.autoReconnect;
let reconnectAttempts = 0;
let reconnectTimeoutId = null;
const MAX_RECONNECT_ATTEMPTS = MQTT_CONFIG.connection.maxReconnectAttempts;
const INITIAL_RECONNECT_DELAY = MQTT_CONFIG.connection.initialReconnectDelay;

// Global image files list (populated from media mappings allImages field)
let allImageFiles = [];
let currentMediaMappings = [];  // Store current media mappings for editing
let allAudioFiles = [];  // Store audio file list for media mapping editor
let allFonts = [];  // Store font list for media mapping editor

// Framebuffer tracking
let lastFramebufferUpdateTimestamp = 0;  // Last framebuffer update timestamp from status message
let currentFramebufferData = null;  // Current framebuffer PNG data for loading onto canvas

// Command tracking
let pendingCommandId = null;  // UUID of the command we're waiting for completion

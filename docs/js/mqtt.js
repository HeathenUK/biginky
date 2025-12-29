// MQTT connection and messaging functions

function connectMQTT() {
    console.log('connectMQTT() called');
    
    // Require password before connecting
    if (!webUIPassword || webUIPassword.length === 0) {
        updateConnectionStatus('disconnected', 'Error: Password required. Please set password in Authentication section above.');
        return;
    }
    
    // Use token from configuration
    const token = MQTT_CONFIG.auth.token;
    const password = MQTT_CONFIG.auth.password || '';
    
    if (!token || token === 'YOUR_RESTRICTED_TOKEN_HERE') {
        console.log('MQTT token not configured');
        updateConnectionStatus('disconnected', 'Error: MQTT token not configured. Please create mqtt-config.js from mqtt-config.example.js');
        return;
    }
    
    if (mqttClient && isConnected) {
        console.log('Already connected');
        return;
    }
    
    console.log('Starting connection to', MQTT_BROKER, 'port', MQTT_PORT);
    updateConnectionStatus('connecting', 'Connecting to MQTT broker...');
    
    // Check if Paho MQTT library is loaded
    if (!pahoLibraryLoaded && !checkPahoLoaded()) {
        console.error('Paho MQTT library not loaded!', { 
            Paho: typeof Paho, 
            Client: typeof (typeof Paho !== 'undefined' ? Paho.Client : undefined),
            Message: typeof (typeof Paho !== 'undefined' ? Paho.Message : undefined),
            pahoLibraryLoaded: pahoLibraryLoaded
        });
        updateConnectionStatus('disconnected', 'Error: MQTT library not loaded. Please wait a moment and try again.');
        // Try waiting a bit and retry
        setTimeout(function() {
            if (checkPahoLoaded()) {
                console.log('Library now available, retrying connection...');
                connectMQTT();
            } else {
                console.error('Library still not available after wait');
            }
        }, 1000);
        return;
    }
    
    const clientId = 'biginky_web_' + Math.random().toString(16).substr(2, 8);
    console.log('Creating MQTT client with ID:', clientId);
    console.log('Paho.Client available:', typeof Paho.Client);
    
    try {
        mqttClient = new Paho.Client(MQTT_BROKER, MQTT_PORT, MQTT_CONFIG.path || '/mqtt', clientId);
        console.log('MQTT client created successfully');
    } catch (e) {
        console.error('Failed to create MQTT client:', e);
        updateConnectionStatus('disconnected', 'Error creating MQTT client: ' + e.message);
        return;
    }
    
    mqttClient.onConnectionLost = function(responseObject) {
        console.log('Connection lost:', responseObject);
        isConnected = false;
        updateConnectionStatus('disconnected', 'Connection lost: ' + (responseObject.errorMessage || 'Unknown error'));
        updatePasswordStatus();  // Update button states
        logCommand('DISCONNECTED', { reason: responseObject.errorMessage });
        
        // Auto-reconnect if enabled
        if (autoReconnectEnabled && reconnectAttempts < MAX_RECONNECT_ATTEMPTS) {
            scheduleReconnect();
        }
    };
    
    mqttClient.onMessageArrived = async function(message) {
        console.log('Message received:', message);
        logCommand('MESSAGE_RECEIVED', { topic: message.destinationName });
        
        // Handle status messages
        if (message.destinationName === MQTT_TOPIC_STATUS) {
            await handleStatusMessage(message);
        } else if (message.destinationName === MQTT_TOPIC_THUMB) {
            await handleThumbnailMessage(message);
        } else if (message.destinationName === MQTT_TOPIC_MEDIA) {
            await handleMediaMessage(message);
        } else {
            console.log('Message on different topic, ignoring:', message.destinationName);
        }
    };
    
    const connectOptions = {
        userName: MQTT_CONFIG.auth.useTokenAsUsername ? token : (MQTT_CONFIG.auth.token || ''),
        password: password,
        useSSL: true,
        onSuccess: function() {
            console.log('Connection successful!');
            isConnected = true;
            cancelReconnect(); // Reset reconnect attempts on successful connection
            reconnectAttempts = 0;
            updateConnectionStatus('connected', 'Connected successfully!');
            updatePasswordStatus();  // Update button states
            logCommand('CONNECTED', { broker: MQTT_BROKER, topic: MQTT_TOPIC });
            
            // Update status display to show we're waiting for status
            document.getElementById('deviceStatus').innerHTML = '<p style="color:#ff9800;">Connected - waiting for device status...</p>';
            
            // Subscribe to status, thumbnail, and media topics
            mqttClient.subscribe(MQTT_TOPIC_STATUS, { qos: 1 });
            mqttClient.subscribe(MQTT_TOPIC_THUMB, { qos: 1 });
            mqttClient.subscribe(MQTT_TOPIC_MEDIA, { qos: 1 });
            console.log('Subscribed to status topic:', MQTT_TOPIC_STATUS);
            console.log('Subscribed to thumbnail topic:', MQTT_TOPIC_THUMB);
            console.log('Subscribed to media topic:', MQTT_TOPIC_MEDIA);
            
            // If no status message arrives within 3 seconds, show a message
            setTimeout(function() {
                const statusEl = document.getElementById('deviceStatus');
                if (statusEl.innerHTML.includes('waiting for device status')) {
                    statusEl.innerHTML = '<p style="color:#ff9800;">Connected - no status received yet (device may not have published status)</p>';
                }
            }, 3000);
        },
        onFailure: function(error) {
            console.error('Connection failed:', error);
            isConnected = false;
            const errorMsg = error.errorMessage || error.toString() || 'Unknown error';
            updateConnectionStatus('disconnected', 'Connection failed: ' + errorMsg);
            updatePasswordStatus();  // Update button states
            logCommand('CONNECTION_FAILED', { error: errorMsg });
            mqttClient = null;
        },
        timeout: 10,
        keepAliveInterval: 60
    };
    
    console.log('Attempting to connect with options:', { userName: token.substring(0, 10) + '...', useSSL: true });
    try {
        mqttClient.connect(connectOptions);
        console.log('connect() called');
    } catch (e) {
        console.error('Exception during connect():', e);
        updateConnectionStatus('disconnected', 'Error: ' + e.message);
    }
}

async function handleStatusMessage(message) {
    console.log('Status message received, parsing...');
    try {
        const payload = JSON.parse(message.payloadString);
        console.log('Parsed status:', payload);
        
        // Check if message is encrypted
        let status = payload;
        
        if (payload.encrypted && payload.payload) {
            // Validate encrypted message structure
            if (!payload.iv || !payload.payload) {
                console.error('Invalid encrypted message structure - missing iv or payload');
                document.getElementById('deviceStatus').innerHTML = '<p style="color:#f44336;">Error: Invalid encrypted message structure. Message may be incomplete.</p>';
                return;
            }
            
            // Verify HMAC first (on encrypted message)
            if (webUIPassword && payload.hmac) {
                const providedHMAC = payload.hmac;
                const messageForHMAC = JSON.stringify({ encrypted: true, iv: payload.iv, payload: payload.payload });
                
                const hmacValid = await verifyHMAC(messageForHMAC, providedHMAC);
                if (!hmacValid) {
                    console.error('Status message HMAC verification failed');
                    document.getElementById('deviceStatus').innerHTML = '<p style="color:#f44336;">Error: HMAC verification failed. Password may be incorrect.</p>';
                    return;
                }
                console.log('HMAC verification passed - password is correct for HMAC');
            } else {
                console.warn('No password or HMAC provided - cannot verify message authenticity');
            }
            
            // Decrypt the payload
            let decrypted;
            try {
                decrypted = await decryptMessage(payload.payload, payload.iv);
            } catch (decryptError) {
                console.error('Failed to decrypt status message:', decryptError);
                document.getElementById('deviceStatus').innerHTML = '<p style="color:#f44336;">Error: Failed to decrypt status message. <strong>Password mismatch detected</strong> - HMAC passed but decryption failed. Please verify the password matches the device password.</p>';
                return;
            }
            
            if (!decrypted || decrypted.length === 0) {
                console.error('Decryption returned empty result');
                document.getElementById('deviceStatus').innerHTML = '<p style="color:#f44336;">Error: Decryption failed - empty result. <strong>Password mismatch detected</strong> - HMAC passed but decryption failed. Please verify the password matches the device password.</p>';
                return;
            }
            
            // Debug: log first few characters of decrypted data
            console.log('Decrypted data (first 100 chars):', decrypted.substring(0, 100));
            console.log('Decrypted data length:', decrypted.length);
            
            // Parse decrypted JSON
            try {
                status = JSON.parse(decrypted);
            } catch (parseError) {
                console.error('Failed to parse decrypted status JSON:', parseError);
                console.error('Decrypted data (full):', decrypted);
                document.getElementById('deviceStatus').innerHTML = '<p style="color:#f44336;">Error: Invalid status data after decryption. Message may be incomplete or corrupted.</p>';
                return;
            }
            
            console.log('Decrypted status:', status);
        } else if (webUIPassword && payload.hmac) {
            // Unencrypted but has HMAC - verify it
            const providedHMAC = payload.hmac;
            delete payload.hmac;
            const messageForHMAC = JSON.stringify(payload);
            
            const hmacValid = await verifyHMAC(messageForHMAC, providedHMAC);
            if (!hmacValid) {
                console.error('Status message HMAC verification failed');
                document.getElementById('deviceStatus').innerHTML = '<p style="color:#f44336;">Error: HMAC verification failed. Password may be incorrect.</p>';
                return;
            }
            status = payload;
            console.log('Status message HMAC verified successfully (unencrypted)');
        } else {
            // No password configured or no HMAC - just display (for backward compatibility)
            if (!webUIPassword) {
                console.warn('No password configured - cannot verify HMAC');
            }
            status = payload;
        }
        
        // Display status
        updateDeviceStatus(status);
        
        // Store next wake time for busy state countdown
        if (status.next_wake) {
            // Parse next_wake time (format: "YYYY-MM-DD HH:MM:SS" or similar)
            try {
                nextWakeTime = new Date(status.next_wake);
                // If parsing fails, try alternative formats
                if (isNaN(nextWakeTime.getTime())) {
                    // Try parsing as ISO string or other formats
                    nextWakeTime = new Date(status.next_wake.replace(' ', 'T'));
                }
                if (isNaN(nextWakeTime.getTime())) {
                    nextWakeTime = null;
                }
            } catch (e) {
                console.warn('Failed to parse next_wake time:', status.next_wake, e);
                nextWakeTime = null;
            }
        }
        
        // Check for pending_action and update busy state
        if (status.pending_action) {
            setBusyState(true, 'Device is processing: ' + status.pending_action);
        } else if (isBusy) {
            // No pending action means command completed
            clearTimeout(busyTimeoutId);
            busyTimeoutId = null;
            if (busyCountdownInterval) {
                clearInterval(busyCountdownInterval);
                busyCountdownInterval = null;
            }
            nextWakeTime = null;
            setBusyState(false);
        }
    } catch (e) {
        console.error('Failed to parse/decrypt status message:', e);
        document.getElementById('deviceStatus').innerHTML = '<p style="color:#f44336;">Error parsing status message: ' + e.message + '</p>';
    }
}

async function handleThumbnailMessage(message) {
    console.log('Thumbnail message received, parsing...');
    console.log('Message retained flag:', message.retained);
    console.log('Message payload string length:', message.payloadString ? message.payloadString.length : 0);
    try {
        const payload = JSON.parse(message.payloadString);
        console.log('Parsed payload:', payload);
        console.log('Payload keys:', Object.keys(payload));
        console.log('Has encrypted flag:', payload.encrypted);
        console.log('Has iv field:', payload.iv !== undefined);
        console.log('Has payload field:', payload.payload !== undefined);
        console.log('Has hmac field:', payload.hmac !== undefined);
        
        // Check if message is encrypted
        let thumb = payload;
        let isEncrypted = false;
        
        if (payload.encrypted && payload.payload) {
            isEncrypted = true;
            console.log('Thumbnail message is encrypted, decrypting...');
            
            // Check if this is new format (separate IV) or legacy format (IV prepended)
            const isNewFormat = payload.iv !== undefined;
            const isLegacyFormat = !isNewFormat;
            
            if (isNewFormat) {
                // New format: IV and payload are separate
                if (!payload.iv || !payload.payload) {
                    console.error('Invalid encrypted message structure - missing iv or payload');
                    document.getElementById('thumbnailStatus').textContent = 'Error: Invalid encrypted message structure. Message may be incomplete.';
                    return;
                }
                
                // Verify HMAC first (on encrypted message)
                if (webUIPassword && payload.hmac) {
                    const providedHMAC = payload.hmac;
                    const messageForHMAC = JSON.stringify({ encrypted: true, iv: payload.iv, payload: payload.payload });
                    
                    console.log('Thumbnail: Verifying HMAC for new format message');
                    const hmacValid = await verifyHMAC(messageForHMAC, providedHMAC);
                    if (!hmacValid) {
                        console.error('Thumbnail message HMAC verification failed');
                        document.getElementById('thumbnailStatus').textContent = 'Error: HMAC verification failed. Password may be incorrect.';
                        return;
                    }
                    console.log('Thumbnail: HMAC verification passed');
                } else {
                    console.log('Thumbnail: Skipping HMAC verification (no password or no HMAC)');
                }
            } else {
                // Legacy format: IV is prepended to payload
                console.log('Thumbnail message is in legacy format (IV prepended)');
                if (!payload.payload) {
                    console.error('Invalid encrypted message structure - missing payload');
                    document.getElementById('thumbnailStatus').textContent = 'Error: Invalid encrypted message structure. Message may be incomplete.';
                    return;
                }
                
                // For legacy format, HMAC might be on the payload itself or not present
                if (webUIPassword && payload.hmac) {
                    const providedHMAC = payload.hmac;
                    const messageForHMAC = JSON.stringify({ encrypted: true, payload: payload.payload });
                    
                    const hmacValid = await verifyHMAC(messageForHMAC, providedHMAC);
                    if (!hmacValid) {
                        console.error('Thumbnail message HMAC verification failed (legacy format)');
                        document.getElementById('thumbnailStatus').textContent = 'Error: HMAC verification failed. Password may be incorrect.';
                        return;
                    }
                }
            }
            
            // Decrypt the payload
            // For new format: pass payload and IV separately
            // For legacy format: pass payload only (IV is prepended)
            let decrypted;
            try {
                console.log('Thumbnail: Attempting decryption, message retained:', message.retained);
                console.log('Thumbnail: IV base64:', payload.iv);
                console.log('Thumbnail: Payload base64 length:', payload.payload ? payload.payload.length : 0);
                console.log('Thumbnail: Payload base64 (first 50 chars):', payload.payload ? payload.payload.substring(0, 50) : 'null');
                decrypted = await decryptMessage(payload.payload, payload.iv);
            } catch (decryptError) {
                console.error('Failed to decrypt thumbnail message:', decryptError);
                console.error('Thumbnail decryption failed for retained message:', message.retained);
                document.getElementById('thumbnailStatus').textContent = 'Error: Failed to decrypt thumbnail message. Password may be incorrect or message corrupted.';
                return;
            }
            
            if (!decrypted || decrypted.length === 0) {
                console.error('Decryption returned empty result');
                console.error('Thumbnail decryption returned empty for retained message:', message.retained);
                
                // If this is a retained message and decryption fails, it may have been encrypted
                // with a different key/method. Since new messages work, we'll just skip this one.
                if (message.retained) {
                    console.warn('Skipping retained thumbnail message that failed to decrypt - will wait for new message');
                    document.getElementById('thumbnailStatus').textContent = 'Waiting for new thumbnail... (retained message encrypted with different key)';
                    return;
                }
                
                document.getElementById('thumbnailStatus').textContent = 'Error: Decryption failed - empty result. Message may be corrupted.';
                return;
            }
            
            // Parse decrypted JSON
            try {
                thumb = JSON.parse(decrypted);
            } catch (parseError) {
                console.error('Failed to parse decrypted thumbnail JSON:', parseError);
                document.getElementById('thumbnailStatus').textContent = 'Error: Invalid thumbnail data after decryption. Message may be incomplete or corrupted.';
                return;
            }
            
            // Validate decrypted thumbnail structure
            if (!thumb || typeof thumb.width !== 'number' || typeof thumb.height !== 'number' || !thumb.format || !thumb.data) {
                console.error('Invalid thumbnail structure after decryption:', thumb);
                document.getElementById('thumbnailStatus').textContent = 'Error: Invalid thumbnail structure. Message may be incomplete.';
                return;
            }
            
            console.log('Decrypted thumbnail:', { width: thumb.width, height: thumb.height, format: thumb.format, dataLength: thumb.data ? thumb.data.length : 0 });
        } else if (webUIPassword && payload.hmac) {
            // Unencrypted but has HMAC - verify it
            const providedHMAC = payload.hmac;
            delete payload.hmac;
            const messageForHMAC = JSON.stringify(payload);
            
            const hmacValid = await verifyHMAC(messageForHMAC, providedHMAC);
            if (!hmacValid) {
                console.error('Thumbnail message HMAC verification failed');
                document.getElementById('thumbnailStatus').textContent = 'Error: HMAC verification failed. Password may be incorrect.';
                return;
            }
            thumb = payload;
            console.log('Thumbnail message HMAC verified successfully (unencrypted)');
        } else {
            // No password configured or no HMAC - just display (for backward compatibility)
            if (!webUIPassword) {
                console.warn('No password configured - cannot verify HMAC');
            }
            thumb = payload;
        }
        
        // Validate thumbnail structure before displaying
        if (!thumb || typeof thumb.width !== 'number' || typeof thumb.height !== 'number' || !thumb.format || !thumb.data) {
            console.error('Invalid thumbnail structure:', thumb);
            document.getElementById('thumbnailStatus').textContent = 'Error: Invalid thumbnail structure. Required fields missing.';
            return;
        }
        
        // Validate data length matches expected size for the format
        if (thumb.format === 'jpeg' || thumb.format === 'rgb888') {
            const expectedDataLength = thumb.format === 'jpeg' 
                ? undefined  // JPEG can vary in size
                : thumb.width * thumb.height * 3;  // RGB888: width * height * 3 bytes
            
            if (thumb.format === 'rgb888' && thumb.data.length < expectedDataLength) {
                console.error('Thumbnail data incomplete:', { 
                    expected: expectedDataLength, 
                    actual: thumb.data.length,
                    width: thumb.width,
                    height: thumb.height
                });
                document.getElementById('thumbnailStatus').textContent = 'Error: Thumbnail data incomplete. Expected ' + expectedDataLength + ' bytes, got ' + thumb.data.length + '.';
                return;
            }
        }
        
        // All validations passed - safe to display
        updateThumbnail(thumb);
    } catch (e) {
        console.error('Failed to parse/decrypt thumbnail message:', e);
        document.getElementById('thumbnailStatus').textContent = 'Error parsing thumbnail message: ' + e.message;
    }
}

async function handleMediaMessage(message) {
    console.log('Media mappings message received, parsing...');
    console.log('Media mappings message retained flag:', message.retained);
    try {
        const payload = JSON.parse(message.payloadString);
        console.log('Parsed media payload:', payload);
        
        // Check if message is encrypted
        let mediaData = payload;
        
        if (payload.encrypted && payload.payload) {
            // Validate encrypted message structure
            if (!payload.iv || !payload.payload) {
                console.error('Invalid encrypted message structure - missing iv or payload');
                document.getElementById('mediaMappingsStatus').textContent = 'Error: Invalid encrypted message structure. Message may be incomplete.';
                return;
            }
            
            // Verify HMAC first
            if (webUIPassword && payload.hmac) {
                const providedHMAC = payload.hmac;
                const messageForHMAC = JSON.stringify({ encrypted: true, iv: payload.iv, payload: payload.payload });
                
                const hmacValid = await verifyHMAC(messageForHMAC, providedHMAC);
                if (!hmacValid) {
                    console.error('Media mappings message HMAC verification failed');
                    document.getElementById('mediaMappingsStatus').textContent = 'Error: HMAC verification failed. Password may be incorrect.';
                    return;
                }
            }
            
            // Decrypt the payload
            let decrypted;
            try {
                console.log('Media mappings: Attempting decryption, message retained:', message.retained);
                console.log('Media mappings: IV base64:', payload.iv);
                console.log('Media mappings: Payload base64 length:', payload.payload ? payload.payload.length : 0);
                decrypted = await decryptMessage(payload.payload, payload.iv);
            } catch (decryptError) {
                console.error('Failed to decrypt media mappings message:', decryptError);
                document.getElementById('mediaMappingsStatus').textContent = 'Error: Failed to decrypt media mappings. Password may be incorrect or message corrupted.';
                return;
            }
            
            if (!decrypted || decrypted.length === 0) {
                console.error('Decryption returned empty result');
                document.getElementById('mediaMappingsStatus').textContent = 'Error: Decryption failed - empty result. Message may be corrupted.';
                return;
            }
            
            // Parse decrypted JSON
            try {
                mediaData = JSON.parse(decrypted);
            } catch (parseError) {
                console.error('Failed to parse decrypted media mappings JSON:', parseError);
                document.getElementById('mediaMappingsStatus').textContent = 'Error: Invalid media mappings data after decryption. Message may be incomplete or corrupted.';
                return;
            }
            
            console.log('Decrypted media mappings:', { mappingCount: mediaData.mappings ? mediaData.mappings.length : 0 });
        } else if (webUIPassword && payload.hmac) {
            // Unencrypted but has HMAC - verify it
            const providedHMAC = payload.hmac;
            delete payload.hmac;
            const messageForHMAC = JSON.stringify(payload);
            
            const hmacValid = await verifyHMAC(messageForHMAC, providedHMAC);
            if (!hmacValid) {
                console.error('Media mappings message HMAC verification failed');
                document.getElementById('mediaMappingsStatus').textContent = 'Error: HMAC verification failed. Password may be incorrect.';
                return;
            }
            mediaData = payload;
        } else {
            // No password configured or no HMAC - just display (for backward compatibility)
            if (!webUIPassword) {
                console.warn('No password configured - cannot verify HMAC');
            }
            mediaData = payload;
        }
        
        // Validate media data structure
        if (!mediaData || !mediaData.mappings || !Array.isArray(mediaData.mappings)) {
            console.error('Invalid media mappings structure:', mediaData);
            document.getElementById('mediaMappingsStatus').textContent = 'Error: Invalid media mappings structure. Required fields missing.';
            return;
        }
        
        // Display media mappings table
        updateMediaMappingsTable(mediaData.mappings);
    } catch (e) {
        console.error('Failed to parse/decrypt media mappings message:', e);
        document.getElementById('mediaMappingsStatus').textContent = 'Error parsing media mappings message: ' + e.message;
    }
}

function disconnectMQTT() {
    if (mqttClient && isConnected) {
        mqttClient.disconnect();
        mqttClient = null;
        isConnected = false;
        updateConnectionStatus('disconnected', 'Disconnected');
        logCommand('DISCONNECTED', { reason: 'User requested' });
        
        // Clear device status
        document.getElementById('deviceStatus').innerHTML = '<p>Not connected - status appears when connected</p>';
    }
}

async function publishMessage(payload) {
    // Check if password is configured FIRST (before connection check)
    if (!webUIPassword || webUIPassword.length === 0) {
        showStatus('commandStatus', 'Error: Password required. Please set password in Authentication section above.', true);
        return false;
    }
    
    if (!mqttClient || !isConnected) {
        showStatus('commandStatus', 'Not connected to MQTT broker', true);
        return false;
    }
    
    try {
        // Encrypt the payload
        const plaintext = JSON.stringify(payload);
        const encryptedPayload = await encryptMessage(plaintext);
        
        if (!encryptedPayload) {
            showStatus('commandStatus', 'Error: Failed to encrypt message', true);
            return false;
        }
        
        // Create encrypted message structure
        const encryptedMessage = {
            encrypted: true,
            payload: encryptedPayload
        };
        
        // Compute HMAC of encrypted message (without hmac field)
        const messageForHMAC = JSON.stringify(encryptedMessage);
        const hmac = await computeHMAC(messageForHMAC);
        
        if (!hmac) {
            showStatus('commandStatus', 'Error: Failed to compute HMAC signature', true);
            return false;
        }
        
        // Add HMAC to encrypted message
        encryptedMessage.hmac = hmac;
        
        const message = new Paho.Message(JSON.stringify(encryptedMessage));
        message.destinationName = MQTT_TOPIC;
        message.qos = 1;
        message.retained = true;  // Use retained messages so device receives them when it wakes up
        mqttClient.send(message);
        logCommand('PUBLISH', { command: payload.command, encrypted: true });
        return true;
    } catch (error) {
        showStatus('commandStatus', 'Failed to send message: ' + error, true);
        return false;
    }
}

// Auto-reconnect functionality
function scheduleReconnect() {
    if (reconnectTimeoutId) {
        clearTimeout(reconnectTimeoutId);
    }
    
    reconnectAttempts++;
    const delay = Math.min(INITIAL_RECONNECT_DELAY * Math.pow(2, reconnectAttempts - 1), 30000); // Exponential backoff, max 30s
    
    updateConnectionStatus('reconnecting', `Reconnecting in ${(delay/1000).toFixed(1)}s... (attempt ${reconnectAttempts}/${MAX_RECONNECT_ATTEMPTS})`);
    
    reconnectTimeoutId = setTimeout(function() {
        console.log(`Auto-reconnect attempt ${reconnectAttempts}/${MAX_RECONNECT_ATTEMPTS}`);
        connectMQTT();
    }, delay);
}

function cancelReconnect() {
    if (reconnectTimeoutId) {
        clearTimeout(reconnectTimeoutId);
        reconnectTimeoutId = null;
    }
    reconnectAttempts = 0;
}


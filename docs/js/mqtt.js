// MQTT connection and messaging functions

function connectMQTT() {
    // console.log('connectMQTT() called');
    
    // Require password before connecting
    if (!webUIPassword || webUIPassword.length === 0) {
        updateConnectionStatus('disconnected', 'Error: Password required');
        return;
    }
    
    // Use token from configuration
    const token = MQTT_CONFIG.auth.token;
    const password = MQTT_CONFIG.auth.password || '';
    
    if (!token || token === 'YOUR_RESTRICTED_TOKEN_HERE') {
        // console.log('MQTT token not configured');
        updateConnectionStatus('disconnected', 'Error: MQTT token not configured. Please create mqtt-config.js from mqtt-config.example.js');
        return;
    }
    
    if (mqttClient && isConnected) {
        console.log('Already connected');
        return;
    }
    
    // console.log('Starting connection to', MQTT_BROKER, 'port', MQTT_PORT);
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
    // console.log('Creating MQTT client with ID:', clientId);
    console.log('Paho.Client available:', typeof Paho.Client);
    
    try {
        mqttClient = new Paho.Client(MQTT_BROKER, MQTT_PORT, MQTT_CONFIG.path || '/mqtt', clientId);
        // console.log('MQTT client created successfully');
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
        // console.log('Message received:', message);
        logCommand('MESSAGE_RECEIVED', { topic: message.destinationName });
        
        // Handle status messages
        if (message.destinationName === MQTT_TOPIC_STATUS) {
            await handleStatusMessage(message);
        } else if (message.destinationName === MQTT_TOPIC_THUMB) {
            await handleThumbnailMessage(message);
        } else if (message.destinationName === MQTT_TOPIC_MEDIA) {
            await handleMediaMessage(message);
        } else {
            // console.log('Message on different topic, ignoring:', message.destinationName);
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
            // console.log('Subscribed to status topic:', MQTT_TOPIC_STATUS);
            // console.log('Subscribed to thumbnail topic:', MQTT_TOPIC_THUMB);
            // console.log('Subscribed to media topic:', MQTT_TOPIC_MEDIA);
            
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
    // console.log('Status message received, parsing...');
    try {
        const payload = JSON.parse(message.payloadString);
        console.log('Parsed status:', payload);
        
        // Check if message is encrypted or unencrypted
        let status = payload;
        
        if (payload.encrypted === true && payload.payload) {
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
                // console.log('HMAC verification passed - password is correct for HMAC');
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
            // console.log('Decrypted data (first 100 chars):', decrypted.substring(0, 100));
            // console.log('Decrypted data length:', decrypted.length);
            
            // Parse decrypted JSON
            try {
                status = JSON.parse(decrypted);
            } catch (parseError) {
                console.error('Failed to parse decrypted status JSON:', parseError);
                console.error('Decrypted data (full):', decrypted);
                document.getElementById('deviceStatus').innerHTML = '<p style="color:#f44336;">Error: Invalid status data after decryption. Message may be incomplete or corrupted.</p>';
                return;
            }
            
            // console.log('Decrypted status:', status);
        } else if (payload.encrypted === false && payload.payload) {
            // Unencrypted message with base64-encoded payload
            // console.log('Status message is unencrypted (HMAC only), base64 decoding payload...');
            
            // Verify HMAC first (on unencrypted message structure)
            if (webUIPassword && payload.hmac) {
                const providedHMAC = payload.hmac;
                const messageForHMAC = JSON.stringify({ encrypted: false, payload: payload.payload });
                
                const hmacValid = await verifyHMAC(messageForHMAC, providedHMAC);
                if (!hmacValid) {
                    console.error('Status message HMAC verification failed');
                    document.getElementById('deviceStatus').innerHTML = '<p style="color:#f44336;">Error: HMAC verification failed. Password may be incorrect.</p>';
                    return;
                }
                // console.log('Status message HMAC verified successfully (unencrypted)');
            } else {
                console.warn('No password or HMAC provided for unencrypted message - cannot verify');
            }
            
            // Base64 decode the payload (no decryption)
            try {
                const decoded = atob(payload.payload);
                status = JSON.parse(decoded);
                console.log('Base64 decoded and parsed status:', status);
            } catch (decodeError) {
                console.error('Failed to base64 decode or parse status message:', decodeError);
                document.getElementById('deviceStatus').innerHTML = '<p style="color:#f44336;">Error: Failed to decode unencrypted status message.</p>';
                return;
            }
        } else if (webUIPassword && payload.hmac && payload.encrypted === undefined) {
            // Legacy: Unencrypted but has HMAC - verify it (backward compatibility)
            const providedHMAC = payload.hmac;
            const payloadCopy = { ...payload };
            delete payloadCopy.hmac;
            const messageForHMAC = JSON.stringify(payloadCopy);
            
            const hmacValid = await verifyHMAC(messageForHMAC, providedHMAC);
            if (!hmacValid) {
                console.error('Status message HMAC verification failed');
                document.getElementById('deviceStatus').innerHTML = '<p style="color:#f44336;">Error: HMAC verification failed. Password may be incorrect.</p>';
                return;
            }
            status = payloadCopy;
            // console.log('Status message HMAC verified successfully (legacy unencrypted)');
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
        let wakeTimeUpdated = false;
        if (status.next_wake) {
            // Parse next_wake time (format: "YYYY-MM-DD HH:MM:SS" or similar)
            try {
                const newWakeTime = new Date(status.next_wake);
                // If parsing fails, try alternative formats
                if (isNaN(newWakeTime.getTime())) {
                    // Try parsing as ISO string or other formats
                    nextWakeTime = new Date(status.next_wake.replace(' ', 'T'));
                } else {
                    nextWakeTime = newWakeTime;
                }
                if (isNaN(nextWakeTime.getTime())) {
                    nextWakeTime = null;
                } else {
                    wakeTimeUpdated = true;
                }
            } catch (e) {
                console.warn('Failed to parse next_wake time:', status.next_wake, e);
                nextWakeTime = null;
            }
        }
        
        // Store sleep interval for busy state message
        if (status.sleep_interval_minutes !== undefined) {
            sleepIntervalMinutes = status.sleep_interval_minutes;
        }
        
        // Check for command completion status (using id field)
        if (status.command_completed === true && status.id) {
            // console.log('Command completion received:', status.id, 'success:', status.success);
            
            // Check if this is the command we're waiting for
            if (pendingCommandId && status.id === pendingCommandId) {
                // console.log('Command completed! ID matches:', pendingCommandId);
                pendingCommandId = null;  // Clear pending command ID
                
                // Clear busy state
                clearTimeout(busyTimeoutId);
                busyTimeoutId = null;
                if (busyCountdownInterval) {
                    clearInterval(busyCountdownInterval);
                    busyCountdownInterval = null;
                }
                setBusyState(false);
                
                // Show success/failure message
                if (status.success === false) {
                    showStatus('commandStatus', 'Command failed: ' + (status.command || 'unknown'), true);
                } else {
                    showStatus('commandStatus', 'Command completed successfully: ' + (status.command || 'unknown'), false);
                }
            } else if (pendingCommandId) {
                // console.log('Command completion received but ID mismatch. Expected:', pendingCommandId, 'Got:', status.id);
            }
        }
        
        // Check for pending_action and update busy state (legacy method)
        if (status.pending_action) {
            setBusyState(true, 'Device is processing: ' + status.pending_action);
        } else if (isBusy && !pendingCommandId) {
            // No pending action and no pending command ID means command completed (legacy)
            clearTimeout(busyTimeoutId);
            busyTimeoutId = null;
            if (busyCountdownInterval) {
                clearInterval(busyCountdownInterval);
                busyCountdownInterval = null;
            }
            // Don't clear nextWakeTime - keep it for next command
            setBusyState(false);
        } else if (wakeTimeUpdated && isBusy) {
            // We're in busy state and just got/updated next_wake time - update message immediately
            // This happens when status arrives after command was sent
            updateBusyMessage();
            
            // Start countdown interval if not already running
            if (!busyCountdownInterval && nextWakeTime) {
                busyCountdownInterval = setInterval(() => {
                    if (isBusy) {
                        updateBusyMessage();
                    } else {
                        clearInterval(busyCountdownInterval);
                        busyCountdownInterval = null;
                    }
                }, 1000);
            }
        }
    } catch (e) {
        console.error('Failed to parse/decrypt status message:', e);
        document.getElementById('deviceStatus').innerHTML = '<p style="color:#f44336;">Error parsing status message: ' + e.message + '</p>';
    }
}

async function handleThumbnailMessage(message) {
    // console.log('Thumbnail message received, parsing...');
    // console.log('Message retained flag:', message.retained);
    // console.log('Message payload string length:', message.payloadString ? message.payloadString.length : 0);
    
    // Validate message payload is not empty or corrupted
    if (!message.payloadString || message.payloadString.length === 0) {
        console.error('Thumbnail message has empty payload');
        document.getElementById('thumbnailStatus').textContent = 'Error: Empty thumbnail message received';
        return;
    }
    
    // Check for potential corruption - very large messages might be truncated
    if (message.payloadString.length > 500000) {  // ~500KB seems reasonable max
        console.warn('Thumbnail message is unusually large:', message.payloadString.length, 'bytes');
    }
    
    try {
        // Validate JSON structure before parsing
        if (!message.payloadString.trim().startsWith('{') || !message.payloadString.trim().endsWith('}')) {
            console.error('Thumbnail message does not appear to be valid JSON (missing braces)');
            document.getElementById('thumbnailStatus').textContent = 'Error: Invalid JSON structure in thumbnail message';
            return;
        }
        
        const payload = JSON.parse(message.payloadString);
        console.log('Parsed payload:', payload);
        console.log('Payload keys:', Object.keys(payload));
        // console.log('Has encrypted flag:', payload.encrypted);
        console.log('Has iv field:', payload.iv !== undefined);
        console.log('Has payload field:', payload.payload !== undefined);
        // console.log('Has hmac field:', payload.hmac !== undefined);
        
        // Check if message is encrypted or unencrypted
        let thumb = payload;
        let isEncrypted = false;
        
        if (payload.encrypted === true && payload.payload) {
            isEncrypted = true;
            // console.log('Thumbnail message is encrypted, decrypting...');
            
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
                
                // Validate IV and payload are strings and not empty
                if (typeof payload.iv !== 'string' || payload.iv.length === 0) {
                    console.error('Thumbnail: Invalid IV field (not a string or empty)');
                    document.getElementById('thumbnailStatus').textContent = 'Error: Invalid IV in thumbnail message';
                    return;
                }
                if (typeof payload.payload !== 'string' || payload.payload.length === 0) {
                    console.error('Thumbnail: Invalid payload field (not a string or empty)');
                    document.getElementById('thumbnailStatus').textContent = 'Error: Invalid payload in thumbnail message';
                    return;
                }
                
                // Validate base64 string lengths (16 bytes IV = 24 base64 chars, payload should be multiple of 4)
                const cleanIv = payload.iv.replace(/\s/g, '');
                const cleanPayload = payload.payload.replace(/\s/g, '');
                if (cleanIv.length !== 24) {
                    console.warn('Thumbnail: IV base64 length is', cleanIv.length, '(expected 24 for 16 bytes)');
                }
                if (cleanPayload.length % 4 !== 0) {
                    console.warn('Thumbnail: Payload base64 length is', cleanPayload.length, '(not a multiple of 4, may be corrupted)');
                }
                
                // Verify HMAC first (on encrypted message)
                if (webUIPassword && payload.hmac) {
                    const providedHMAC = payload.hmac;
                    const messageForHMAC = JSON.stringify({ encrypted: true, iv: payload.iv, payload: payload.payload });
                    
                    // console.log('Thumbnail: Verifying HMAC for new format message');
                    const hmacValid = await verifyHMAC(messageForHMAC, providedHMAC);
                    if (!hmacValid) {
                        console.error('Thumbnail message HMAC verification failed');
                        document.getElementById('thumbnailStatus').textContent = 'Error: HMAC verification failed. Password may be incorrect.';
                        return;
                    }
                    // console.log('Thumbnail: HMAC verification passed');
                } else {
                    // console.log('Thumbnail: Skipping HMAC verification (no password or no HMAC)');
                }
            } else {
                // Legacy format: IV is prepended to payload
                // console.log('Thumbnail message is in legacy format (IV prepended)');
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
                // console.log('Thumbnail: Attempting decryption, message retained:', message.retained);
                // console.log('Thumbnail: IV base64:', payload.iv);
                // console.log('Thumbnail: Payload base64 length:', payload.payload ? payload.payload.length : 0);
                // console.log('Thumbnail: Payload base64 (first 50 chars):', payload.payload ? payload.payload.substring(0, 50) : 'null');
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
                // Optionally, we could request a fresh thumbnail by sending a command, but that
                // might be too aggressive - better to wait for the next automatic update.
                if (message.retained) {
                    console.warn('Skipping retained thumbnail message that failed to decrypt - will wait for new message');
                    console.warn('This usually means the retained message was encrypted with a different key/method');
                    console.warn('New thumbnails will work fine - trigger a canvas draw or wait for next automatic update');
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
                console.error('Decrypted string length:', decrypted ? decrypted.length : 0);
                console.error('First 200 chars of decrypted data:', decrypted ? decrypted.substring(0, 200) : 'null');
                document.getElementById('thumbnailStatus').textContent = 'Error: Invalid thumbnail data after decryption. Message may be incomplete or corrupted.';
                return;
            }
            
            // Validate decrypted thumbnail structure
            if (!thumb || typeof thumb.width !== 'number' || typeof thumb.height !== 'number' || !thumb.format || !thumb.data) {
                console.error('Invalid thumbnail structure after decryption:', thumb);
                document.getElementById('thumbnailStatus').textContent = 'Error: Invalid thumbnail structure. Message may be incomplete.';
                return;
            }
            
            // console.log('Decrypted thumbnail:', { 
            //     width: thumb.width, 
            //     height: thumb.height, 
            //     format: thumb.format, 
            //     dataLength: thumb.data ? thumb.data.length : 0,
            //     dataType: typeof thumb.data
            // });
            
            // Validate base64 data length is reasonable
            if (thumb.data && thumb.data.length > 0) {
                // Expected base64 length for a JPEG thumbnail: roughly width * height * quality factor
                // For 400x300 JPEG at quality 75, expect roughly 6-10KB raw, so 8-14KB base64
                const minExpectedBase64 = 4000;  // Minimum reasonable size
                const maxExpectedBase64 = 20000; // Maximum reasonable size
                if (thumb.data.length < minExpectedBase64) {
                    console.warn('Thumbnail base64 data seems too short:', thumb.data.length, 'bytes (expected at least', minExpectedBase64, ')');
                }
                if (thumb.data.length > maxExpectedBase64) {
                    console.warn('Thumbnail base64 data seems too long:', thumb.data.length, 'bytes (expected at most', maxExpectedBase64, ')');
                }
            }
        } else if (payload.encrypted === false && payload.payload) {
            // Unencrypted message with base64-encoded payload
            // console.log('Thumbnail message is unencrypted (HMAC only), base64 decoding payload...');
            
            // Verify HMAC first (on unencrypted message structure)
            if (webUIPassword && payload.hmac) {
                const providedHMAC = payload.hmac;
                const messageForHMAC = JSON.stringify({ encrypted: false, payload: payload.payload });
                
                const hmacValid = await verifyHMAC(messageForHMAC, providedHMAC);
                if (!hmacValid) {
                    console.error('Thumbnail message HMAC verification failed');
                    document.getElementById('thumbnailStatus').textContent = 'Error: HMAC verification failed. Password may be incorrect.';
                    return;
                }
                // console.log('Thumbnail message HMAC verified successfully (unencrypted)');
            } else {
                console.warn('No password or HMAC provided for unencrypted thumbnail - cannot verify');
            }
            
            // Base64 decode the payload (no decryption)
            try {
                const decoded = atob(payload.payload);
                thumb = JSON.parse(decoded);
                // console.log('Base64 decoded and parsed thumbnail:', { 
                //     width: thumb.width, 
                //     height: thumb.height, 
                //     format: thumb.format 
                // });
            } catch (decodeError) {
                console.error('Failed to base64 decode or parse thumbnail message:', decodeError);
                document.getElementById('thumbnailStatus').textContent = 'Error: Failed to decode unencrypted thumbnail message.';
                return;
            }
        } else if (webUIPassword && payload.hmac && payload.encrypted === undefined) {
            // Legacy: Unencrypted but has HMAC - verify it (backward compatibility)
            const providedHMAC = payload.hmac;
            const payloadCopy = { ...payload };
            delete payloadCopy.hmac;
            const messageForHMAC = JSON.stringify(payloadCopy);
            
            const hmacValid = await verifyHMAC(messageForHMAC, providedHMAC);
            if (!hmacValid) {
                console.error('Thumbnail message HMAC verification failed');
                document.getElementById('thumbnailStatus').textContent = 'Error: HMAC verification failed. Password may be incorrect.';
                return;
            }
            thumb = payloadCopy;
            // console.log('Thumbnail message HMAC verified successfully (legacy unencrypted)');
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
    // console.log('Media mappings message received, parsing...');
    // console.log('Media mappings message retained flag:', message.retained);
    try {
        const payload = JSON.parse(message.payloadString);
        // console.log('Parsed media payload:', payload);
        
        // Check if message is encrypted or unencrypted
        let mediaData = payload;
        
        if (payload.encrypted === true && payload.payload) {
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
                //                 // console.log('Media mappings: Attempting decryption, message retained:', message.retained);
                // console.log('Media mappings: IV base64:', payload.iv);
                // console.log('Media mappings: Payload base64 length:', payload.payload ? payload.payload.length : 0);
                decrypted = await decryptMessage(payload.payload, payload.iv);
            } catch (decryptError) {
                console.error('Failed to decrypt media mappings message:', decryptError);
                // If this is a retained message and decryption fails, it may have been encrypted
                // with a different key/method. Since new messages work, we'll just skip this one.
                if (message.retained) {
                    console.warn('Skipping retained media mappings message that failed to decrypt - will wait for new message');
                    console.warn('This usually means the retained message was encrypted with a different key/method');
                    console.warn('New media mappings will work fine - device will publish fresh mappings on next cycle');
                    document.getElementById('mediaMappingsStatus').textContent = 'Waiting for new media mappings... (retained message encrypted with different key)';
                    return;
                }
                document.getElementById('mediaMappingsStatus').textContent = 'Error: Failed to decrypt media mappings. Password may be incorrect or message corrupted.';
                return;
            }
            
            if (!decrypted || decrypted.length === 0) {
                console.error('Decryption returned empty result');
                // If this is a retained message and decryption fails, skip it
                if (message.retained) {
                    console.warn('Skipping retained media mappings message that failed to decrypt (empty result) - will wait for new message');
                    document.getElementById('mediaMappingsStatus').textContent = 'Waiting for new media mappings... (retained message encrypted with different key)';
                    return;
                }
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
            
            // console.log('Decrypted media mappings:', { mappingCount: mediaData.mappings ? mediaData.mappings.length : 0 });
        } else if (payload.encrypted === false && payload.payload) {
            // Unencrypted message with base64-encoded payload
            // console.log('Media mappings message is unencrypted (HMAC only), base64 decoding payload...');
            
            // Verify HMAC first (on unencrypted message structure)
            if (webUIPassword && payload.hmac) {
                const providedHMAC = payload.hmac;
                const messageForHMAC = JSON.stringify({ encrypted: false, payload: payload.payload });
                
                const hmacValid = await verifyHMAC(messageForHMAC, providedHMAC);
                if (!hmacValid) {
                    console.error('Media mappings message HMAC verification failed');
                    document.getElementById('mediaMappingsStatus').textContent = 'Error: HMAC verification failed. Password may be incorrect.';
                    return;
                }
                // console.log('Media mappings message HMAC verified successfully (unencrypted)');
            } else {
                console.warn('No password or HMAC provided for unencrypted media mappings - cannot verify');
            }
            
            // Base64 decode the payload (no decryption)
            try {
                const decoded = atob(payload.payload);
                mediaData = JSON.parse(decoded);
                // console.log('Base64 decoded and parsed media mappings:', { mappingCount: mediaData.mappings ? mediaData.mappings.length : 0 });
            } catch (decodeError) {
                console.error('Failed to base64 decode or parse media mappings message:', decodeError);
                document.getElementById('mediaMappingsStatus').textContent = 'Error: Failed to decode unencrypted media mappings message.';
                return;
            }
        } else if (webUIPassword && payload.hmac && payload.encrypted === undefined) {
            // Legacy: Unencrypted but has HMAC - verify it (backward compatibility)
            const providedHMAC = payload.hmac;
            const payloadCopy = { ...payload };
            delete payloadCopy.hmac;
            const messageForHMAC = JSON.stringify(payloadCopy);
            
            const hmacValid = await verifyHMAC(messageForHMAC, providedHMAC);
            if (!hmacValid) {
                console.error('Media mappings message HMAC verification failed');
                document.getElementById('mediaMappingsStatus').textContent = 'Error: HMAC verification failed. Password may be incorrect.';
                return;
            }
            mediaData = payloadCopy;
            // console.log('Media mappings message HMAC verified successfully (legacy unencrypted)');
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
        
        // Extract and store allImages array (if present) for background image dropdown
        if (mediaData.allImages && Array.isArray(mediaData.allImages)) {
            allImageFiles = mediaData.allImages;
            console.log(`Loaded ${allImageFiles.length} image files for background image selection`);
            // Populate background image dropdown if it exists
            populateBackgroundImageDropdown();
        } else {
            console.warn('Media mappings payload does not contain allImages array');
            allImageFiles = [];
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
        showStatus('commandStatus', 'Error: Password required', true);
        return false;
    }
    
    if (!mqttClient || !isConnected) {
        showStatus('commandStatus', 'Not connected to MQTT broker', true);
        return false;
    }
    
    try {
        // Generate UUID for command tracking (if command field exists)
        if (payload.command) {
            // Use built-in crypto.randomUUID() if available, otherwise fallback to manual generation
            if (typeof crypto !== 'undefined' && crypto.randomUUID) {
                pendingCommandId = crypto.randomUUID();
            } else {
                // Fallback for older browsers
                pendingCommandId = 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, function(c) {
                    const r = Math.random() * 16 | 0;
                    const v = c === 'x' ? r : (r & 0x3 | 0x8);
                    return v.toString(16);
                });
            }
            payload.id = pendingCommandId;
            // console.log('Generated command ID:', pendingCommandId, 'for command:', payload.command);
        }
        
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


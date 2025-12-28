// Password management module

// Load password from sessionStorage on page load
async function loadPassword() {
    const stored = sessionStorage.getItem('biginky_webui_password');
    if (stored) {
        try {
            // Try to decrypt (new encrypted format)
            webUIPassword = await decryptPasswordFromStorage(stored);
            if (!webUIPassword) {
                // If decryption fails, try legacy base64 format (for migration)
                try {
                    webUIPassword = atob(stored);
                } catch (e) {
                    console.error('Failed to load password (both encrypted and legacy formats failed):', e);
                    webUIPassword = null;
                }
            }
            if (webUIPassword) {
                document.getElementById('webUIPassword').value = webUIPassword;
            }
        } catch (e) {
            console.error('Failed to load password:', e);
            webUIPassword = null;
        }
    } else {
        webUIPassword = null;
    }
    updatePasswordStatus();
}

// Verify password by attempting to decrypt the retained status message
async function verifyPasswordByDecryption(password) {
    // Try to get the retained status message from MQTT
    // If we're connected, we can request it; otherwise we'll need to connect temporarily
    return new Promise(async (resolve) => {
        if (!mqttClient || !isConnected) {
            // Not connected - we can't verify yet, but we'll allow saving
            // The password will be verified when we connect and receive the first status message
            console.log('Not connected to MQTT - password will be verified on first status message');
            resolve(true);  // Allow saving, verify later
            return;
        }
        
        // We're connected - try to get retained status message
        // The status message should already be in the retained topic
        // We can subscribe and wait for it, or check if we already have it
        // For now, we'll just allow saving and verify on the next status message
        console.log('Connected - password will be verified on next status message');
        resolve(true);  // Allow saving, verify on next message
    });
}

// Save password to sessionStorage and verify it by decrypting status message
async function savePassword() {
    const password = document.getElementById('webUIPassword').value.trim();
    const passwordStatusEl = document.getElementById('passwordStatus');
    
    if (password.length === 0) {
        passwordStatusEl.innerHTML = '<div class="error" style="margin-top:10px;">Error: Password cannot be empty</div>';
        updatePasswordStatus();
        return;
    }
    if (password.length < 8) {
        passwordStatusEl.innerHTML = '<div class="error" style="margin-top:10px;">Error: Password must be at least 8 characters</div>';
        updatePasswordStatus();
        return;
    }
    
    // Show verifying message
    passwordStatusEl.innerHTML = '<div class="info" style="margin-top:10px;">Verifying password by decrypting status message...</div>';
    
    // Temporarily set password to test decryption
    const oldPassword = webUIPassword;
    webUIPassword = password;
    
    // If connected, try to verify by decrypting a status message
    // We'll subscribe to status topic and wait for retained message
    let verificationSuccess = false;
    
    if (mqttClient && isConnected) {
        // Create a one-time message handler to verify password
        const verifyHandler = async function(message) {
            if (message.destinationName === MQTT_TOPIC_STATUS) {
                try {
                    const payload = JSON.parse(message.payloadString);
                    
                    // Check if message is encrypted
                    if (payload.encrypted && payload.payload) {
                        // Try to decrypt
                        const decrypted = await decryptMessage(payload.payload, payload.iv);
                        if (decrypted) {
                            // Try to parse as JSON to verify it's valid
                            const status = JSON.parse(decrypted);
                            if (status.timestamp || status.current_time) {
                                // Valid decrypted status - password is correct!
                                verificationSuccess = true;
                                mqttClient.unsubscribe(MQTT_TOPIC_STATUS);
                                mqttClient.onMessageArrived = originalHandler;
                                
                                // Save password (encrypted)
                                try {
                                    const encrypted = await encryptPasswordForStorage(password);
                                    if (encrypted) {
                                        sessionStorage.setItem('biginky_webui_password', encrypted);
                                    } else {
                                        // Fallback to base64 if encryption fails
                                        sessionStorage.setItem('biginky_webui_password', btoa(password));
                                    }
                                    passwordStatusEl.innerHTML = '<div class="status" style="margin-top:10px;">✓ Password verified and saved successfully!</div>';
                                    updatePasswordStatus();
                                } catch (e) {
                                    passwordStatusEl.innerHTML = '<div class="error" style="margin-top:10px;">Error: Failed to save password: ' + e.message + '</div>';
                                    webUIPassword = oldPassword;
                                    updatePasswordStatus();
                                }
                                return;
                            }
                        }
                    } else if (payload.hmac) {
                        // Unencrypted but has HMAC - password might still be correct
                        // For now, allow it (backward compatibility)
                        verificationSuccess = true;
                        mqttClient.unsubscribe(MQTT_TOPIC_STATUS);
                        mqttClient.onMessageArrived = originalHandler;
                        
                        try {
                            const encrypted = await encryptPasswordForStorage(password);
                            if (encrypted) {
                                sessionStorage.setItem('biginky_webui_password', encrypted);
                            } else {
                                sessionStorage.setItem('biginky_webui_password', btoa(password));
                            }
                            passwordStatusEl.innerHTML = '<div class="status" style="margin-top:10px;">✓ Password saved (unencrypted message received - will verify on encrypted message)</div>';
                            updatePasswordStatus();
                        } catch (e) {
                            passwordStatusEl.innerHTML = '<div class="error" style="margin-top:10px;">Error: Failed to save password: ' + e.message + '</div>';
                            webUIPassword = oldPassword;
                            updatePasswordStatus();
                        }
                        return;
                    }
                } catch (e) {
                    console.error('Failed to verify password:', e);
                }
                
                // Verification failed
                mqttClient.unsubscribe(MQTT_TOPIC_STATUS);
                mqttClient.onMessageArrived = originalHandler;
                webUIPassword = oldPassword;
                passwordStatusEl.innerHTML = '<div class="error" style="margin-top:10px;">✗ Password verification failed: Could not decrypt status message. Please check your password.</div>';
                updatePasswordStatus();
            }
        };
        
        // Save original handler and set verification handler
        const originalHandler = mqttClient.onMessageArrived;
        mqttClient.onMessageArrived = verifyHandler;
        
        // Subscribe to status topic to get retained message
        mqttClient.subscribe(MQTT_TOPIC_STATUS, { qos: 1 });
        
        // Timeout after 5 seconds
        setTimeout(async () => {
            if (!verificationSuccess) {
                mqttClient.unsubscribe(MQTT_TOPIC_STATUS);
                mqttClient.onMessageArrived = originalHandler;
                webUIPassword = oldPassword;
                passwordStatusEl.innerHTML = '<div class="error" style="margin-top:10px;">✗ Password verification timeout: No status message received. Password saved but not verified. Will verify on next status message.</div>';
                // Still save the password
                try {
                    const encrypted = await encryptPasswordForStorage(password);
                    if (encrypted) {
                        sessionStorage.setItem('biginky_webui_password', encrypted);
                    } else {
                        sessionStorage.setItem('biginky_webui_password', btoa(password));
                    }
                    updatePasswordStatus();
                } catch (e) {
                    console.error('Failed to save password:', e);
                }
            }
        }, 5000);
    } else {
        // Not connected - just save password, verify later
        try {
            const encrypted = await encryptPasswordForStorage(password);
            if (encrypted) {
                sessionStorage.setItem('biginky_webui_password', encrypted);
            } else {
                sessionStorage.setItem('biginky_webui_password', btoa(password));
            }
            webUIPassword = password;
            passwordStatusEl.innerHTML = '<div class="info" style="margin-top:10px;">Password saved. Will verify when connected to MQTT and status message is received.</div>';
            updatePasswordStatus();
        } catch (e) {
            console.error('Failed to save password:', e);
            passwordStatusEl.innerHTML = '<div class="error" style="margin-top:10px;">Error: Failed to save password: ' + e.message + '</div>';
            webUIPassword = oldPassword;
            updatePasswordStatus();
        }
    }
}

// Clear password from sessionStorage and reset UI
function clearPassword() {
    if (confirm('Are you sure you want to clear the saved password? You will need to re-enter it to use the web UI.')) {
        sessionStorage.removeItem('biginky_webui_password');
        webUIPassword = null;
        document.getElementById('webUIPassword').value = '';
        document.getElementById('passwordStatus').innerHTML = '<div class="info" style="margin-top:10px;">Password cleared. Please enter a new password and click Save Password.</div>';
        updatePasswordStatus();
        
        // Disconnect from MQTT if connected
        if (mqttClient && isConnected) {
            mqttClient.disconnect();
            isConnected = false;
            updateConnectionStatus('disconnected', 'Disconnected after password cleared');
        }
    }
}


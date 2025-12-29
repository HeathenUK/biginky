// UI utility functions

function updateConnectionStatus(status, message) {
    const statusEl = document.getElementById('connectionStatus');
    const timestampEl = document.getElementById('connectionTimestamp');
    const msgEl = document.getElementById('connectionStatusMsg');
    const connectBtn = document.getElementById('connectBtn');
    const disconnectBtn = document.getElementById('disconnectBtn');
    
    statusEl.className = 'connection-status ' + status;
    statusEl.textContent = status.charAt(0).toUpperCase() + status.slice(1);
    
    // Update timestamp
    if (timestampEl) {
        const now = new Date();
        timestampEl.textContent = now.toLocaleTimeString();
    }
    
    if (message) {
        msgEl.innerHTML = '<div class="' + (status === 'connected' ? 'status' : 'error') + '">' + message + '</div>';
    } else {
        msgEl.innerHTML = '';
    }
    
    // Update button states based on connection AND password
    const hasPassword = (webUIPassword && webUIPassword.length > 0);
    connectBtn.disabled = (status === 'connected' || status === 'connecting' || status === 'reconnecting' || !hasPassword);
    disconnectBtn.disabled = (status === 'disconnected');
    
    const buttons = ['textDisplayBtn', 'canvasDisplayBtn', 'clearBtn', 'nextBtn'];
    buttons.forEach(id => {
        const btn = document.getElementById(id);
        if (btn) {
            btn.disabled = (status !== 'connected' || !hasPassword);
        }
    });
}

// Update UI state based on password status
function updatePasswordStatus() {
    const hasPassword = (webUIPassword && webUIPassword.length > 0);
    const buttons = ['textDisplayBtn', 'canvasDisplayBtn', 'clearBtn', 'nextBtn', 'connectBtn'];
    
    buttons.forEach(id => {
        const btn = document.getElementById(id);
        if (btn) {
            if (id === 'connectBtn') {
                // Connect button disabled if no password
                btn.disabled = !hasPassword;
            } else {
                // Other buttons disabled if no password OR not connected
                btn.disabled = !hasPassword || !isConnected;
            }
        }
    });
    
    // Show warning if no password
    const passwordStatusEl = document.getElementById('passwordStatus');
    if (!hasPassword) {
        passwordStatusEl.innerHTML = '<div class="error" style="margin-top:10px;">⚠️ Password required: All functionality is disabled until a password is set.</div>';
    } else {
        passwordStatusEl.innerHTML = '<div class="status" style="margin-top:10px;">✓ Password configured - interface enabled</div>';
    }
}

function logCommand(action, data) {
    const logEl = document.getElementById('commandLog');
    const timestamp = new Date().toLocaleTimeString();
    let logEntry = `[${timestamp}] `;
    
    // Simplify log messages
    if (action === 'PUBLISH' && data && data.command) {
        const command = data.command;
        // Map command names to user-friendly messages
        const commandMessages = {
            'text_display': 'Text display command sent',
            'canvas_display': 'Canvas display command sent',
            'clear': 'Clear display command sent',
            'next': 'Next media item command sent',
            'go': 'Show media item command sent'
        };
        logEntry += commandMessages[command] || `Command "${command}" sent`;
    } else if (action === 'MESSAGE_RECEIVED' && data && data.topic) {
        // Map topic to user-friendly messages
        if (data.topic === MQTT_TOPIC_STATUS) {
            logEntry += 'Status update received';
        } else if (data.topic === MQTT_TOPIC_THUMB) {
            logEntry += 'Thumbnail update received';
        } else if (data.topic === MQTT_TOPIC_MEDIA) {
            logEntry += 'Media mappings update received';
        } else {
            logEntry += 'Message received';
        }
    } else if (action === 'CONNECTED') {
        logEntry += 'Connected to MQTT broker';
    } else if (action === 'DISCONNECTED') {
        logEntry += 'Disconnected from MQTT broker';
    } else if (action === 'CONNECTION_FAILED') {
        logEntry += 'Connection failed';
    } else {
        // Fallback for unknown actions
        logEntry += action;
    }
    
    logEntry += '\n';
    logEl.textContent = logEntry + logEl.textContent;
    const lines = logEl.textContent.split('\n');
    if (lines.length > 50) {
        logEl.textContent = lines.slice(0, 50).join('\n');
    }
}

function updateDeviceStatus(status) {
    const statusEl = document.getElementById('deviceStatus');
    let html = '<div style="line-height:1.6;">';
    
    // Timestamp and current time
    if (status.timestamp) {
        const date = new Date(status.timestamp * 1000);
        html += `<p><strong>Last Update:</strong> ${date.toLocaleString()}</p>`;
    }
    if (status.current_time) {
        html += `<p><strong>Device Time:</strong> ${status.current_time}</p>`;
    }
    
    // Next media item
    if (status.next_media) {
        html += '<p><strong>Next Media:</strong> ';
        html += `<span style="color:#4CAF50;">${status.next_media.image}</span>`;
        if (status.next_media.audio) {
            html += ` <span style="color:#888;">(${status.next_media.audio})</span>`;
        }
        html += ` <span style="color:#888;font-size:11px;">[index ${status.next_media.index}]</span>`;
        html += '</p>';
    }
    
    // Next wake time
    if (status.next_wake) {
        html += `<p><strong>Next Wake:</strong> <span style="color:#2196F3;">${status.next_wake}</span>`;
        if (status.sleep_interval_minutes) {
            html += ` <span style="color:#888;font-size:11px;">(every ${status.sleep_interval_minutes} min)</span>`;
        }
        html += '</p>';
    }
    
    // Connection status
    if (status.connected !== undefined) {
        html += '<p><strong>Status:</strong> ';
        if (status.connected) {
            html += '<span style="color:#4CAF50;">Connected</span>';
        } else {
            html += '<span style="color:#f44336;">Disconnected</span>';
        }
        html += '</p>';
    }
    
    html += '</div>';
    statusEl.innerHTML = html;
}

// Format time remaining until next wake
function formatTimeUntilWake() {
    if (!nextWakeTime) return null;
    
    const now = new Date();
    const diff = nextWakeTime.getTime() - now.getTime();
    
    if (diff <= 0) {
        return 'Device should be awake now';
    }
    
    const seconds = Math.floor(diff / 1000);
    const minutes = Math.floor(seconds / 60);
    const hours = Math.floor(minutes / 60);
    const days = Math.floor(hours / 24);
    
    if (days > 0) {
        return `${days}d ${hours % 24}h ${minutes % 60}m`;
    } else if (hours > 0) {
        return `${hours}h ${minutes % 60}m ${seconds % 60}s`;
    } else if (minutes > 0) {
        return `${minutes}m ${seconds % 60}s`;
    } else {
        return `${seconds}s`;
    }
}

// Update busy state message with countdown
function updateBusyMessage() {
    if (!isBusy) return;
    
    const msgEl = document.getElementById('busyOverlay')?.querySelector('.message');
    if (!msgEl) return;
    
    const timeUntilWake = formatTimeUntilWake();
    if (timeUntilWake && nextWakeTime) {
        const now = new Date();
        const diff = nextWakeTime.getTime() - now.getTime();
        
        if (diff <= 0) {
            // Device should be awake now
            msgEl.textContent = 'Device should be awake. Command will be processed shortly...';
        } else {
            // Device is asleep - show precise countdown
            const wakeTimeStr = nextWakeTime.toLocaleTimeString();
            msgEl.textContent = `Device is asleep. Command will be processed at ${wakeTimeStr} (in ${timeUntilWake})`;
        }
    } else {
        // No next_wake time available - show generic message
        msgEl.textContent = 'Command sent. Waiting for device response...';
    }
}

function setBusyState(busy, message) {
    // Clear any existing timeout and interval
    if (busyTimeoutId) {
        clearTimeout(busyTimeoutId);
        busyTimeoutId = null;
    }
    if (busyCountdownInterval) {
        clearInterval(busyCountdownInterval);
        busyCountdownInterval = null;
    }
    
    isBusy = busy;
    const overlay = document.getElementById('busyOverlay');
    if (overlay) {
        if (busy) {
            overlay.classList.add('active');
            const msgEl = overlay.querySelector('.message');
            
            // Immediately check if we have next_wake time and show appropriate message
            if (nextWakeTime) {
                // We have next_wake time - show countdown immediately
                updateBusyMessage();
                
                // Start countdown interval to update message every second
                busyCountdownInterval = setInterval(() => {
                    if (isBusy) {
                        updateBusyMessage();
                    } else {
                        clearInterval(busyCountdownInterval);
                        busyCountdownInterval = null;
                    }
                }, 1000);
            } else if (message) {
                // No next_wake time yet - use provided message
                // Will be updated when status message arrives with next_wake
                if (msgEl) {
                    msgEl.textContent = message;
                }
                
                // Set timeout: if no status message arrives within 5 seconds, check again
                busyTimeoutId = setTimeout(() => {
                    if (isBusy) {
                        // Status message should have arrived by now - update with countdown if available
                        updateBusyMessage();
                        
                        // Start countdown interval if we now have next_wake time
                        if (nextWakeTime && !busyCountdownInterval) {
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
                }, 5000);  // 5 seconds - status should arrive quickly if device is awake
            }
        } else {
            overlay.classList.remove('active');
            // Don't clear nextWakeTime - we want to keep it for next command
        }
    }
}

function showStatus(id, msg, isError) {
    const el = document.getElementById(id);
    el.textContent = msg;
    el.className = isError ? 'error status' : 'status';
}

// Collapsible sections
function toggleSection(h2Element) {
    const section = h2Element.closest('.section');
    const content = section.querySelector('.section-content');
    if (!content) return;
    
    const isCollapsed = h2Element.classList.contains('collapsed');
    
    if (isCollapsed) {
        h2Element.classList.remove('collapsed');
        content.classList.remove('collapsed');
        localStorage.setItem('section_' + content.id, 'expanded');
    } else {
        h2Element.classList.add('collapsed');
        content.classList.add('collapsed');
        localStorage.setItem('section_' + content.id, 'collapsed');
    }
}

// Restore section states on page load (default to collapsed, except Authentication and Connection)
function restoreSectionStates() {
    document.querySelectorAll('.section-content').forEach(function(content) {
        const state = localStorage.getItem('section_' + content.id);
        const h2 = content.previousElementSibling;
        // Keep Authentication and Connection sections open by default
        if (content.id === 'section-auth' || content.id === 'section-connection') {
            // Only collapse if explicitly saved as collapsed
            if (state === 'collapsed' && h2) {
                h2.classList.add('collapsed');
                content.classList.add('collapsed');
            }
        } else {
            // Default to collapsed for all other sections if no saved state
            if (!state || state === 'collapsed') {
                if (h2) {
                    h2.classList.add('collapsed');
                    content.classList.add('collapsed');
                }
            }
        }
    });
}

// Keyboard shortcuts
function setupKeyboardShortcuts() {
    document.addEventListener('keydown', function(e) {
        // Ctrl/Cmd + Enter to send text display
        if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') {
            const textInput = document.getElementById('textInput');
            if (textInput && document.activeElement === textInput) {
                e.preventDefault();
                const textDisplayBtn = document.getElementById('textDisplayBtn');
                if (textDisplayBtn && !textDisplayBtn.disabled) {
                    sendTextDisplay();
                }
            }
        }
        
        // Ctrl/Cmd + K to focus search (if we add search later)
        if ((e.ctrlKey || e.metaKey) && e.key === 'k') {
            e.preventDefault();
            // Future: focus search input
        }
    });
}


// Main initialization code

// Load password on page load (after all functions are defined)
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', function() {
        if (typeof loadPassword === 'function') {
            loadPassword();
        }
        if (typeof restoreSectionStates === 'function') {
            restoreSectionStates();
        }
        if (typeof setupKeyboardShortcuts === 'function') {
            setupKeyboardShortcuts();
        }
        if (typeof initTextColorPickers === 'function') {
            initTextColorPickers();
        }
        
        // Initialize canvas filename with default
        const canvasFilenameInput = document.getElementById('canvasFilenameInput');
        if (canvasFilenameInput && typeof generateDefaultCanvasFilename === 'function') {
            canvasFilenameInput.value = generateDefaultCanvasFilename();
        }
        
        // Load auto-reconnect preference
        const autoReconnectPref = localStorage.getItem('autoReconnectEnabled');
        if (autoReconnectPref !== null) {
            autoReconnectEnabled = autoReconnectPref === 'true';
            const checkbox = document.getElementById('autoReconnect');
            if (checkbox) {
                checkbox.checked = autoReconnectEnabled;
            }
        }
        
        // Setup auto-reconnect checkbox handler
        const autoReconnectCheckbox = document.getElementById('autoReconnect');
        if (autoReconnectCheckbox) {
            autoReconnectCheckbox.addEventListener('change', function() {
                autoReconnectEnabled = this.checked;
                localStorage.setItem('autoReconnectEnabled', String(autoReconnectEnabled));
                if (!autoReconnectEnabled && typeof cancelReconnect === 'function') {
                    cancelReconnect();
                }
            });
        }
    });
} else {
    // DOM already loaded
    if (typeof loadPassword === 'function') {
        loadPassword();
    }
    if (typeof restoreSectionStates === 'function') {
        restoreSectionStates();
    }
    if (typeof setupKeyboardShortcuts === 'function') {
        setupKeyboardShortcuts();
    }
    if (typeof initTextColorPickers === 'function') {
        initTextColorPickers();
    }
    
    // Initialize canvas filename with default
    const canvasFilenameInput = document.getElementById('canvasFilenameInput');
    if (canvasFilenameInput && typeof generateDefaultCanvasFilename === 'function') {
        canvasFilenameInput.value = generateDefaultCanvasFilename();
    }
    
    // Load auto-reconnect preference
    const autoReconnectPref = localStorage.getItem('autoReconnectEnabled');
    if (autoReconnectPref !== null) {
        autoReconnectEnabled = autoReconnectPref === 'true';
        const checkbox = document.getElementById('autoReconnect');
        if (checkbox) {
            checkbox.checked = autoReconnectEnabled;
        }
    }
    
    // Setup auto-reconnect checkbox handler
    const autoReconnectCheckbox = document.getElementById('autoReconnect');
    if (autoReconnectCheckbox) {
        autoReconnectCheckbox.addEventListener('change', function() {
            autoReconnectEnabled = this.checked;
            localStorage.setItem('autoReconnectEnabled', String(autoReconnectEnabled));
            if (!autoReconnectEnabled && typeof cancelReconnect === 'function') {
                cancelReconnect();
            }
        });
    }
}


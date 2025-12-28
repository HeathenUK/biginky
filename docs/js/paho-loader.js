// Paho MQTT library loader
let pahoLibraryLoaded = false;

function checkPahoLoaded() {
    // Paho library exposes Paho.Client and Paho.Message, not Paho.MQTT.Client
    if (typeof Paho !== 'undefined' && typeof Paho.Client !== 'undefined') {
        pahoLibraryLoaded = true;
        console.log('Paho MQTT library loaded successfully');
        return true;
    }
    return false;
}

function loadPahoFallback() {
    console.log('Paho MQTT library failed to load from jsDelivr, trying fallback...');
    const script = document.createElement('script');
    script.src = 'https://unpkg.com/paho-mqtt@1.1.0/paho-mqtt.js';
    script.onload = function() {
        if (checkPahoLoaded()) {
            console.log('Paho MQTT library loaded from fallback (unpkg)');
        } else {
            console.error('Library loaded but structure incorrect');
        }
    };
    script.onerror = function() {
        console.error('Failed to load Paho MQTT from all sources');
        document.getElementById('connectionStatusMsg').innerHTML = '<div class="error">Error: Could not load MQTT library. Please check your internet connection or try refreshing the page.</div>';
    };
    document.head.appendChild(script);
}

// Verify Paho library loaded after page load
window.addEventListener('load', function() {
    setTimeout(function() {
        if (!checkPahoLoaded()) {
            loadPahoFallback();
        }
        // Load password from sessionStorage (after functions are defined)
        if (typeof loadPassword === 'function') {
            loadPassword();  // loadPassword is async but we don't await it here
        } else {
            // Functions not loaded yet, wait a bit
            setTimeout(function() {
                if (typeof loadPassword === 'function') {
                    loadPassword();
                }
            }, 100);
        }
    }, 100);
});


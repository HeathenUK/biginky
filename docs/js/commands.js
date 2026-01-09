// Command sending functions

async function sendTextDisplay() {
    const text = document.getElementById('textInput').value.trim();
    if (text.length === 0) {
        showStatus('textStatus', 'Please enter some text', true);
        return;
    }
    
    // Get color values from dropdowns (HTML has correct defaults selected)
    const color = document.getElementById('textColor').value;
    const bgColor = document.getElementById('textBackgroundColor').value;
    const bgImage = document.getElementById('textBackgroundImage').value;
    const outlineColor = document.getElementById('textOutlineColor').value;
    const font = document.getElementById('textFont').value;
    
    showStatus('textStatus', 'Sending text display command...', false);
    
    const payload = {
        command: 'text_display',
        text: text,
        color: color,
        backgroundColour: bgColor,
        outlineColour: outlineColor,
        font: font
    };
    
    // Add backgroundImage if an image is selected (takes precedence over backgroundColour)
    if (bgImage && bgImage.length > 0) {
        payload.backgroundImage = bgImage;
    }
    
    if (await publishMessage(payload)) {
        showStatus('textStatus', 'Text display command sent successfully!', false);
        setBusyState(true, 'Command sent, waiting for device response...');
    } else {
        showStatus('textStatus', 'Failed to send command', true);
    }
}

async function sendWeatherPlace() {
    const lat = document.getElementById('weatherLat').value.trim();
    const lon = document.getElementById('weatherLon').value.trim();
    const placeName = document.getElementById('weatherPlaceName').value.trim();
    
    // Validate: need either placeName OR (both lat and lon)
    const hasPlaceName = placeName.length > 0;
    const hasLatLon = lat.length > 0 && lon.length > 0;
    
    if (!hasPlaceName && !hasLatLon) {
        showStatus('weatherPlaceStatus', 'Please enter either a place name (for geocoding) or both latitude and longitude', true);
        return;
    }
    
    // If lat/lon are provided, validate they are numbers
    let latNum = 0.0;
    let lonNum = 0.0;
    if (hasLatLon) {
        latNum = parseFloat(lat);
        lonNum = parseFloat(lon);
        if (isNaN(latNum) || isNaN(lonNum)) {
            showStatus('weatherPlaceStatus', 'Latitude and longitude must be valid numbers', true);
            return;
        }
    }
    
    showStatus('weatherPlaceStatus', 'Sending weather place command...', false);
    
    const payload = {
        command: 'weather_place',
        placeName: hasPlaceName ? placeName : 'Location'
    };
    
    // Only include lat/lon if provided (otherwise geocoding will be used)
    if (hasLatLon) {
        payload.lat = latNum.toString();
        payload.lon = lonNum.toString();
    } else {
        // Pass 0.0, 0.0 to indicate geocoding should be used
        payload.lat = '0.0';
        payload.lon = '0.0';
    }
    
    if (await publishMessage(payload)) {
        const msg = hasPlaceName ? 
            `Weather place command sent successfully! Will geocode "${placeName}"...` :
            `Weather place command sent successfully! Using coordinates (${latNum}, ${lonNum})...`;
        showStatus('weatherPlaceStatus', msg, false);
        setBusyState(true, 'Command sent, waiting for device response...');
    } else {
        showStatus('weatherPlaceStatus', 'Failed to send command', true);
    }
}

// Fetch and populate lines for a TfL station
// Line direction mappings - which directions each line uses
const lineDirections = {
    'northern': ['Northbound', 'Southbound'],
    'victoria': ['Northbound', 'Southbound'],
    'jubilee': ['Northbound', 'Southbound'],
    'bakerloo': ['Northbound', 'Southbound'],
    'waterloo-city': ['Northbound', 'Southbound'],
    'metropolitan': ['Northbound', 'Southbound'],
    'central': ['Eastbound', 'Westbound'],
    'district': ['Eastbound', 'Westbound'],
    'circle': ['Eastbound', 'Westbound'],
    'hammersmith-city': ['Eastbound', 'Westbound'],
    'piccadilly': ['Eastbound', 'Westbound'],
    'elizabeth': ['Eastbound', 'Westbound']
};

// Update direction dropdown based on selected line
function updateTflDirections() {
    const lineSelect = document.getElementById('tflLineSelect');
    const directionSelect = document.getElementById('tflDirectionSelect');
    
    const lineId = lineSelect.value;
    
    // Reset direction dropdown
    directionSelect.innerHTML = '<option value="">All directions</option>';
    
    // If a line is selected and we know its directions, add them
    if (lineId && lineDirections[lineId]) {
        for (const direction of lineDirections[lineId]) {
            const option = document.createElement('option');
            option.value = direction;
            option.textContent = direction;
            directionSelect.appendChild(option);
        }
    } else if (lineId) {
        // Unknown line - show all four directions as fallback
        for (const direction of ['Northbound', 'Southbound', 'Eastbound', 'Westbound']) {
            const option = document.createElement('option');
            option.value = direction;
            option.textContent = direction;
            directionSelect.appendChild(option);
        }
    }
    // If no line selected, keep just "All directions"
}

async function onTflStationChange() {
    const selectEl = document.getElementById('tflStationSelect');
    const customInput = document.getElementById('tflCustomStation');
    const lineSelect = document.getElementById('tflLineSelect');
    const loadingEl = document.getElementById('tflLineLoading');
    
    const stationId = selectEl.value;
    
    // Enable/disable custom input
    customInput.disabled = (stationId !== 'custom');
    if (stationId !== 'custom') {
        customInput.value = '';
    }
    
    // Reset line dropdown and direction dropdown
    lineSelect.innerHTML = '<option value="">All lines</option>';
    updateTflDirections();  // Reset directions too
    
    // If no station selected or custom, don't fetch lines
    if (!stationId || stationId === '' || stationId === 'custom') {
        return;
    }
    
    // Show loading indicator
    loadingEl.style.display = 'block';
    
    try {
        // Fetch station details from TfL API to get lines
        const response = await fetch(`https://api.tfl.gov.uk/StopPoint/${stationId}`);
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}`);
        }
        
        const data = await response.json();
        
        // Extract unique line IDs from the station data
        const lines = new Set();
        if (data.lineModeGroups) {
            for (const group of data.lineModeGroups) {
                if (group.modeName === 'tube' && group.lineIdentifier) {
                    for (const lineId of group.lineIdentifier) {
                        lines.add(lineId);
                    }
                }
            }
        }
        
        // Also check lines array if present
        if (data.lines) {
            for (const line of data.lines) {
                if (line.id) {
                    lines.add(line.id);
                }
            }
        }
        
        // Line name mappings (TfL uses lowercase IDs)
        const lineNames = {
            'bakerloo': 'Bakerloo',
            'central': 'Central',
            'circle': 'Circle',
            'district': 'District',
            'hammersmith-city': 'Hammersmith & City',
            'jubilee': 'Jubilee',
            'metropolitan': 'Metropolitan',
            'northern': 'Northern',
            'piccadilly': 'Piccadilly',
            'victoria': 'Victoria',
            'waterloo-city': 'Waterloo & City',
            'elizabeth': 'Elizabeth',
            'dlr': 'DLR',
            'overground': 'Overground',
            'tram': 'Tramlink'
        };
        
        // Add lines to dropdown
        for (const lineId of Array.from(lines).sort()) {
            const option = document.createElement('option');
            option.value = lineId;
            option.textContent = lineNames[lineId] || lineId;
            lineSelect.appendChild(option);
        }
        
    } catch (error) {
        console.error('Failed to fetch TfL station lines:', error);
        // Silently fail - user can still use "All lines"
    } finally {
        loadingEl.style.display = 'none';
    }
}

async function sendTflDepartures() {
    const selectEl = document.getElementById('tflStationSelect');
    const customInput = document.getElementById('tflCustomStation');
    const lineSelect = document.getElementById('tflLineSelect');
    const directionSelect = document.getElementById('tflDirectionSelect');
    
    let stationId = selectEl.value;
    
    // If custom is selected, use the custom input value
    if (stationId === 'custom') {
        stationId = customInput.value.trim();
    }
    
    if (!stationId || stationId === '') {
        showStatus('tflStatus', 'Please select a station or enter a custom NaPTAN ID', true);
        return;
    }
    
    showStatus('tflStatus', 'Sending TfL departures command...', false);
    
    const payload = {
        command: 'tfl_departures',
        stationId: stationId
    };
    
    // Add lineId if a specific line is selected
    const lineId = lineSelect.value;
    if (lineId && lineId !== '') {
        payload.lineId = lineId;
    }
    
    // Add direction if a specific direction is selected
    const direction = directionSelect.value;
    if (direction && direction !== '') {
        payload.direction = direction;
    }
    
    if (await publishMessage(payload)) {
        const stationName = selectEl.options[selectEl.selectedIndex]?.text || stationId;
        const lineName = lineSelect.options[lineSelect.selectedIndex]?.text || 'All lines';
        const directionName = directionSelect.options[directionSelect.selectedIndex]?.text || 'All directions';
        let statusMsg = `TfL departures command sent for ${stationName}`;
        if (lineId) statusMsg += ` (${lineName})`;
        if (direction) statusMsg += ` - ${directionName}`;
        showStatus('tflStatus', statusMsg + '!', false);
        setBusyState(true, 'Command sent, waiting for device response...');
    } else {
        showStatus('tflStatus', 'Failed to send command', true);
    }
}

// Initialize TfL custom input state on page load
document.addEventListener('DOMContentLoaded', function() {
    // Custom input starts disabled (handled by onTflStationChange)
});

async function sendSwimConditions() {
    showStatus('swimStatus', 'Sending swim conditions command...', false);
    
    const payload = {
        command: 'swim_conditions'
    };
    
    if (await publishMessage(payload)) {
        showStatus('swimStatus', 'Swim conditions command sent for Fionphort, Isle of Mull!', false);
        setBusyState(true, 'Command sent, waiting for device response...');
    } else {
        showStatus('swimStatus', 'Failed to send command', true);
    }
}

async function sendCanvasToDisplay() {
    console.log('sendCanvasToDisplay() called');
    const canvas = document.getElementById('drawCanvas');
    if (!canvas) {
        console.error('Canvas element not found');
        showStatus('canvasStatus', 'Error: Canvas not found', true);
        return;
    }
    const ctx = canvas.getContext('2d');
    if (!ctx) {
        console.error('Could not get canvas context');
        showStatus('canvasStatus', 'Error: Could not access canvas', true);
        return;
    }
    try {
        const imageData = ctx.getImageData(0, 0, canvas.width, canvas.height);
        const data = imageData.data;
    
    // Actual e-ink display palette colors (order: Black, White, Yellow, Red, Blue, Green)
    const einkColors = [[26,26,26],[242,241,230],[240,224,80],[160,32,32],[80,128,184],[96,128,80]];
    const einkColorValues = [0,1,2,3,5,6];
    
    function findClosestColorIdx(r, g, b) {
        let minDist = Infinity;
        let closestIdx = 0;
        for (let i = 0; i < einkColors.length; i++) {
            const ec = einkColors[i];
            const dist = Math.pow(r-ec[0],2) + Math.pow(g-ec[1],2) + Math.pow(b-ec[2],2);
            if (dist < minDist) {
                minDist = dist;
                closestIdx = i;
            }
        }
        return closestIdx;
    }
    
    const pixelData = [];
    for (let i = 0; i < data.length; i += 4) {
        const r = data[i];
        const g = data[i+1];
        const b = data[i+2];
        const arrayIdx = findClosestColorIdx(r, g, b);
        const einkColorValue = einkColorValues[arrayIdx];
        pixelData.push(einkColorValue);
    }
    
    const pixelBytes = new Uint8Array(pixelData);
    const rawSizeKB = (pixelBytes.length / 1024).toFixed(1);
    showStatus('canvasStatus', 'Compressing pixel data (' + rawSizeKB + ' KB raw)...', false);
    
    // Try to compress using browser's CompressionStream API (deflate/zlib)
    if (typeof CompressionStream !== 'undefined') {
        const stream = new CompressionStream('deflate');
        const writer = stream.writable.getWriter();
        const reader = stream.readable.getReader();
        
        writer.write(pixelBytes).then(() => writer.close());
        
        const compressedChunks = [];
        function pump() {
            return reader.read().then(({done, value}) => {
                if (done) return;
                compressedChunks.push(value);
                return pump();
            });
        }
        
        pump().then(() => {
            const compressedLength = compressedChunks.reduce((sum, chunk) => sum + chunk.length, 0);
            const compressed = new Uint8Array(compressedLength);
            let offset = 0;
            compressedChunks.forEach(chunk => {
                compressed.set(chunk, offset);
                offset += chunk.length;
            });
            
            // Convert compressed Uint8Array to binary string in chunks
            let binaryString = '';
            const chunkSize = 8192;
            for (let i = 0; i < compressed.length; i += chunkSize) {
                const chunk = compressed.slice(i, i + chunkSize);
                binaryString += String.fromCharCode.apply(null, Array.from(chunk));
            }
            const base64Data = btoa(binaryString);
            
            const compressedSizeKB = (compressed.length / 1024).toFixed(1);
            const base64SizeKB = (base64Data.length / 1024).toFixed(1);
            const ratio = ((compressed.length / pixelBytes.length) * 100).toFixed(1);
            const savedKB = ((pixelBytes.length - compressed.length) / 1024).toFixed(1);
            const jsonSize = JSON.stringify({pixelData: base64Data, width: canvas.width, height: canvas.height, compressed: true}).length;
            const jsonSizeKB = (jsonSize / 1024).toFixed(1);
            
            showStatus('canvasStatus', 'Compressed: ' + compressedSizeKB + ' KB binary (' + base64SizeKB + ' KB base64), ' + ratio + '% of ' + rawSizeKB + ' KB raw. Saved: ' + savedKB + ' KB. Total JSON: ' + jsonSizeKB + ' KB', false);
            
            const payload = {
                command: 'canvas_display',
                pixelData: base64Data,
                width: canvas.width,
                height: canvas.height,
                compressed: true
            };
            
            publishMessage(payload).then(success => {
                if (success) {
                    showStatus('canvasStatus', 'Canvas display command sent successfully!', false);
                    setBusyState(true, 'Command sent, waiting for device response...');
                } else {
                    showStatus('canvasStatus', 'Failed to send command', true);
                }
            });
        }).catch(e => {
            console.error('Compression error:', e);
            showStatus('canvasStatus', 'Compression error: ' + e + ', sending uncompressed', true);
            // Fallback to uncompressed
            let binaryString = '';
            const chunkSize = 8192;
            for (let i = 0; i < pixelBytes.length; i += chunkSize) {
                const chunk = pixelBytes.slice(i, i + chunkSize);
                binaryString += String.fromCharCode.apply(null, Array.from(chunk));
            }
            const base64Data = btoa(binaryString);
            const base64SizeKB = (base64Data.length / 1024).toFixed(1);
            const jsonSize = JSON.stringify({pixelData: base64Data, width: canvas.width, height: canvas.height, compressed: false}).length;
            const jsonSizeKB = (jsonSize / 1024).toFixed(1);
            showStatus('canvasStatus', 'Uncompressed: ' + rawSizeKB + ' KB raw (' + base64SizeKB + ' KB base64). Total JSON: ' + jsonSizeKB + ' KB (compression not available)', false);
            
            const payload = {
                command: 'canvas_display',
                pixelData: base64Data,
                width: canvas.width,
                height: canvas.height,
                compressed: false
            };
            
            publishMessage(payload).then(success => {
                if (success) {
                    showStatus('canvasStatus', 'Canvas display command sent successfully!', false);
                    setBusyState(true, 'Command sent, waiting for device response...');
                } else {
                    showStatus('canvasStatus', 'Failed to send command', true);
                }
            });
        });
    } else {
        // Compression not available - send uncompressed
        let binaryString = '';
        const chunkSize = 8192;
        for (let i = 0; i < pixelBytes.length; i += chunkSize) {
            const chunk = pixelBytes.slice(i, i + chunkSize);
            binaryString += String.fromCharCode.apply(null, Array.from(chunk));
        }
        const base64Data = btoa(binaryString);
        const base64SizeKB = (base64Data.length / 1024).toFixed(1);
        const jsonSize = JSON.stringify({pixelData: base64Data, width: canvas.width, height: canvas.height, compressed: false}).length;
        const jsonSizeKB = (jsonSize / 1024).toFixed(1);
        showStatus('canvasStatus', 'Uncompressed: ' + rawSizeKB + ' KB raw (' + base64SizeKB + ' KB base64). Total JSON: ' + jsonSizeKB + ' KB (compression not available)', false);
        
        const payload = {
            command: 'canvas_display',
            pixelData: base64Data,
            width: canvas.width,
            height: canvas.height,
            compressed: false
        };
        
        publishMessage(payload).then(success => {
            if (success) {
                showStatus('canvasStatus', 'Canvas display command sent successfully!', false);
            } else {
                showStatus('canvasStatus', 'Failed to send command', true);
            }
        });
    }
    } catch (error) {
        console.error('Error in sendCanvasToDisplay:', error);
        showStatus('canvasStatus', 'Error: ' + error.message, true);
    }
}

async function sendCanvasToDisplayAndSave() {
    // Same as sendCanvasToDisplay but uses canvas_display_save command
    console.log('sendCanvasToDisplayAndSave() called');
    const canvas = document.getElementById('drawCanvas');
    if (!canvas) {
        console.error('Canvas element not found');
        showStatus('canvasStatus', 'Error: Canvas not found', true);
        return;
    }
    const ctx = canvas.getContext('2d');
    if (!ctx) {
        console.error('Could not get canvas context');
        showStatus('canvasStatus', 'Error: Could not access canvas', true);
        return;
    }
    try {
        const imageData = ctx.getImageData(0, 0, canvas.width, canvas.height);
        const data = imageData.data;
    
    // Actual e-ink display palette colors (order: Black, White, Yellow, Red, Blue, Green)
    const einkColors = [[26,26,26],[242,241,230],[240,224,80],[160,32,32],[80,128,184],[96,128,80]];
    const einkColorValues = [0,1,2,3,5,6];
    
    function findClosestColorIdx(r, g, b) {
        let minDist = Infinity;
        let closestIdx = 0;
        for (let i = 0; i < einkColors.length; i++) {
            const ec = einkColors[i];
            const dist = Math.pow(r-ec[0],2) + Math.pow(g-ec[1],2) + Math.pow(b-ec[2],2);
            if (dist < minDist) {
                minDist = dist;
                closestIdx = i;
            }
        }
        return closestIdx;
    }
    
    const pixelData = [];
    for (let i = 0; i < data.length; i += 4) {
        const r = data[i];
        const g = data[i+1];
        const b = data[i+2];
        const arrayIdx = findClosestColorIdx(r, g, b);
        const einkColorValue = einkColorValues[arrayIdx];
        pixelData.push(einkColorValue);
    }
    
    const pixelBytes = new Uint8Array(pixelData);
    const rawSizeKB = (pixelBytes.length / 1024).toFixed(1);
    showStatus('canvasStatus', 'Compressing pixel data (' + rawSizeKB + ' KB raw)...', false);
    
    // Generate filename with timestamp
    const now = new Date();
    const timestamp = now.toISOString().replace(/[:.]/g, '-').slice(0, -5);
    const filename = `canvas_${timestamp}.png`;
    
    // Try to compress using browser's CompressionStream API (deflate/zlib)
    if (typeof CompressionStream !== 'undefined') {
        const stream = new CompressionStream('deflate');
        const writer = stream.writable.getWriter();
        const reader = stream.readable.getReader();
        
        writer.write(pixelBytes).then(() => writer.close());
        
        const compressedChunks = [];
        function pump() {
            return reader.read().then(({done, value}) => {
                if (done) return;
                compressedChunks.push(value);
                return pump();
            });
        }
        
        pump().then(() => {
            const compressedLength = compressedChunks.reduce((sum, chunk) => sum + chunk.length, 0);
            const compressed = new Uint8Array(compressedLength);
            let offset = 0;
            compressedChunks.forEach(chunk => {
                compressed.set(chunk, offset);
                offset += chunk.length;
            });
            
            // Convert compressed Uint8Array to binary string in chunks
            let binaryString = '';
            const chunkSize = 8192;
            for (let i = 0; i < compressed.length; i += chunkSize) {
                const chunk = compressed.slice(i, i + chunkSize);
                binaryString += String.fromCharCode.apply(null, Array.from(chunk));
            }
            const base64Data = btoa(binaryString);
            
            const compressedSizeKB = (compressed.length / 1024).toFixed(1);
            const base64SizeKB = (base64Data.length / 1024).toFixed(1);
            const ratio = ((compressed.length / pixelBytes.length) * 100).toFixed(1);
            const savedKB = ((pixelBytes.length - compressed.length) / 1024).toFixed(1);
            const jsonSize = JSON.stringify({pixelData: base64Data, width: canvas.width, height: canvas.height, compressed: true, filename: filename}).length;
            const jsonSizeKB = (jsonSize / 1024).toFixed(1);
            
            showStatus('canvasStatus', 'Compressed: ' + compressedSizeKB + ' KB binary (' + base64SizeKB + ' KB base64), ' + ratio + '% of ' + rawSizeKB + ' KB raw. Saved: ' + savedKB + ' KB. Total JSON: ' + jsonSizeKB + ' KB. Saving as: ' + filename, false);
            
            const payload = {
                command: 'canvas_display_save',
                pixelData: base64Data,
                width: canvas.width,
                height: canvas.height,
                compressed: true,
                filename: filename
            };
            
            publishMessage(payload).then(success => {
                if (success) {
                    showStatus('canvasStatus', 'Canvas display & save command sent successfully! Saving as: ' + filename, false);
                    setBusyState(true, 'Command sent, waiting for device response...');
                } else {
                    showStatus('canvasStatus', 'Failed to send command', true);
                }
            });
        }).catch(e => {
            console.error('Compression error:', e);
            showStatus('canvasStatus', 'Compression error: ' + e + ', sending uncompressed', true);
            // Fallback to uncompressed
            let binaryString = '';
            const chunkSize = 8192;
            for (let i = 0; i < pixelBytes.length; i += chunkSize) {
                const chunk = pixelBytes.slice(i, i + chunkSize);
                binaryString += String.fromCharCode.apply(null, Array.from(chunk));
            }
            const base64Data = btoa(binaryString);
            const base64SizeKB = (base64Data.length / 1024).toFixed(1);
            const jsonSize = JSON.stringify({pixelData: base64Data, width: canvas.width, height: canvas.height, compressed: false, filename: filename}).length;
            const jsonSizeKB = (jsonSize / 1024).toFixed(1);
            showStatus('canvasStatus', 'Uncompressed: ' + rawSizeKB + ' KB raw (' + base64SizeKB + ' KB base64). Total JSON: ' + jsonSizeKB + ' KB (compression not available). Saving as: ' + filename, false);
            
            const payload = {
                command: 'canvas_display_save',
                pixelData: base64Data,
                width: canvas.width,
                height: canvas.height,
                compressed: false,
                filename: filename
            };
            
            publishMessage(payload).then(success => {
                if (success) {
                    showStatus('canvasStatus', 'Canvas display & save command sent successfully! Saving as: ' + filename, false);
                    setBusyState(true, 'Command sent, waiting for device response...');
                } else {
                    showStatus('canvasStatus', 'Failed to send command', true);
                }
            });
        });
    } else {
        // Compression not available - send uncompressed
        let binaryString = '';
        const chunkSize = 8192;
        for (let i = 0; i < pixelBytes.length; i += chunkSize) {
            const chunk = pixelBytes.slice(i, i + chunkSize);
            binaryString += String.fromCharCode.apply(null, Array.from(chunk));
        }
        const base64Data = btoa(binaryString);
        const base64SizeKB = (base64Data.length / 1024).toFixed(1);
        const jsonSize = JSON.stringify({pixelData: base64Data, width: canvas.width, height: canvas.height, compressed: false, filename: filename}).length;
        const jsonSizeKB = (jsonSize / 1024).toFixed(1);
        showStatus('canvasStatus', 'Uncompressed: ' + rawSizeKB + ' KB raw (' + base64SizeKB + ' KB base64). Total JSON: ' + jsonSizeKB + ' KB (compression not available). Saving as: ' + filename, false);
        
        const payload = {
            command: 'canvas_display_save',
            pixelData: base64Data,
            width: canvas.width,
            height: canvas.height,
            compressed: false,
            filename: filename
        };
        
        publishMessage(payload).then(success => {
            if (success) {
                showStatus('canvasStatus', 'Canvas display & save command sent successfully! Saving as: ' + filename, false);
                setBusyState(true, 'Command sent, waiting for device response...');
            } else {
                showStatus('canvasStatus', 'Failed to send command', true);
            }
        });
    }
    } catch (error) {
        console.error('Error in sendCanvasToDisplayAndSave:', error);
        showStatus('canvasStatus', 'Error: ' + error.message, true);
    }
}

async function sendCanvasToSave() {
    // Same as sendCanvasToDisplayAndSave but uses canvas_save command (no display)
    console.log('sendCanvasToSave() called');
    const canvas = document.getElementById('drawCanvas');
    if (!canvas) {
        console.error('Canvas element not found');
        showStatus('canvasStatus', 'Error: Canvas not found', true);
        return;
    }
    const ctx = canvas.getContext('2d');
    if (!ctx) {
        console.error('Could not get canvas context');
        showStatus('canvasStatus', 'Error: Could not access canvas', true);
        return;
    }
    try {
        const imageData = ctx.getImageData(0, 0, canvas.width, canvas.height);
        const data = imageData.data;
    
    // Actual e-ink display palette colors (order: Black, White, Yellow, Red, Blue, Green)
    const einkColors = [[26,26,26],[242,241,230],[240,224,80],[160,32,32],[80,128,184],[96,128,80]];
    const einkColorValues = [0,1,2,3,5,6];
    
    function findClosestColorIdx(r, g, b) {
        let minDist = Infinity;
        let closestIdx = 0;
        for (let i = 0; i < einkColors.length; i++) {
            const ec = einkColors[i];
            const dist = Math.pow(r-ec[0],2) + Math.pow(g-ec[1],2) + Math.pow(b-ec[2],2);
            if (dist < minDist) {
                minDist = dist;
                closestIdx = i;
            }
        }
        return closestIdx;
    }
    
    const pixelData = [];
    for (let i = 0; i < data.length; i += 4) {
        const r = data[i];
        const g = data[i+1];
        const b = data[i+2];
        const arrayIdx = findClosestColorIdx(r, g, b);
        const einkColorValue = einkColorValues[arrayIdx];
        pixelData.push(einkColorValue);
    }
    
    const pixelBytes = new Uint8Array(pixelData);
    const rawSizeKB = (pixelBytes.length / 1024).toFixed(1);
    showStatus('canvasStatus', 'Compressing pixel data (' + rawSizeKB + ' KB raw)...', false);
    
    // Generate filename with timestamp
    const now = new Date();
    const timestamp = now.toISOString().replace(/[:.]/g, '-').slice(0, -5);
    const filename = `canvas_${timestamp}.png`;
    
    // Try to compress using browser's CompressionStream API (deflate/zlib)
    if (typeof CompressionStream !== 'undefined') {
        const stream = new CompressionStream('deflate');
        const writer = stream.writable.getWriter();
        const reader = stream.readable.getReader();
        
        writer.write(pixelBytes).then(() => writer.close());
        
        const compressedChunks = [];
        function pump() {
            return reader.read().then(({done, value}) => {
                if (done) return;
                compressedChunks.push(value);
                return pump();
            });
        }
        
        pump().then(() => {
            const compressedLength = compressedChunks.reduce((sum, chunk) => sum + chunk.length, 0);
            const compressed = new Uint8Array(compressedLength);
            let offset = 0;
            compressedChunks.forEach(chunk => {
                compressed.set(chunk, offset);
                offset += chunk.length;
            });
            
            // Convert compressed Uint8Array to binary string in chunks
            let binaryString = '';
            const chunkSize = 8192;
            for (let i = 0; i < compressed.length; i += chunkSize) {
                const chunk = compressed.slice(i, i + chunkSize);
                binaryString += String.fromCharCode.apply(null, Array.from(chunk));
            }
            const base64Data = btoa(binaryString);
            
            const compressedSizeKB = (compressed.length / 1024).toFixed(1);
            const base64SizeKB = (base64Data.length / 1024).toFixed(1);
            const ratio = ((compressed.length / pixelBytes.length) * 100).toFixed(1);
            const savedKB = ((pixelBytes.length - compressed.length) / 1024).toFixed(1);
            const jsonSize = JSON.stringify({pixelData: base64Data, width: canvas.width, height: canvas.height, compressed: true, filename: filename}).length;
            const jsonSizeKB = (jsonSize / 1024).toFixed(1);
            
            showStatus('canvasStatus', 'Compressed: ' + compressedSizeKB + ' KB binary (' + base64SizeKB + ' KB base64), ' + ratio + '% of ' + rawSizeKB + ' KB raw. Saved: ' + savedKB + ' KB. Total JSON: ' + jsonSizeKB + ' KB. Saving as: ' + filename, false);
            
            const payload = {
                command: 'canvas_save',
                pixelData: base64Data,
                width: canvas.width,
                height: canvas.height,
                compressed: true,
                filename: filename
            };
            
            publishMessage(payload).then(success => {
                if (success) {
                    showStatus('canvasStatus', 'Canvas save command sent successfully! Saving as: ' + filename + ' (no display)', false);
                    setBusyState(true, 'Command sent, waiting for device response...');
                } else {
                    showStatus('canvasStatus', 'Failed to send command', true);
                }
            });
        }).catch(e => {
            console.error('Compression error:', e);
            showStatus('canvasStatus', 'Compression error: ' + e + ', sending uncompressed', true);
            // Fallback to uncompressed
            let binaryString = '';
            const chunkSize = 8192;
            for (let i = 0; i < pixelBytes.length; i += chunkSize) {
                const chunk = pixelBytes.slice(i, i + chunkSize);
                binaryString += String.fromCharCode.apply(null, Array.from(chunk));
            }
            const base64Data = btoa(binaryString);
            const base64SizeKB = (base64Data.length / 1024).toFixed(1);
            const jsonSize = JSON.stringify({pixelData: base64Data, width: canvas.width, height: canvas.height, compressed: false, filename: filename}).length;
            const jsonSizeKB = (jsonSize / 1024).toFixed(1);
            showStatus('canvasStatus', 'Uncompressed: ' + rawSizeKB + ' KB raw (' + base64SizeKB + ' KB base64). Total JSON: ' + jsonSizeKB + ' KB (compression not available). Saving as: ' + filename, false);
            
            const payload = {
                command: 'canvas_save',
                pixelData: base64Data,
                width: canvas.width,
                height: canvas.height,
                compressed: false,
                filename: filename
            };
            
            publishMessage(payload).then(success => {
                if (success) {
                    showStatus('canvasStatus', 'Canvas save command sent successfully! Saving as: ' + filename + ' (no display)', false);
                    setBusyState(true, 'Command sent, waiting for device response...');
                } else {
                    showStatus('canvasStatus', 'Failed to send command', true);
                }
            });
        });
    } else {
        // Compression not available - send uncompressed
        let binaryString = '';
        const chunkSize = 8192;
        for (let i = 0; i < pixelBytes.length; i += chunkSize) {
            const chunk = pixelBytes.slice(i, i + chunkSize);
            binaryString += String.fromCharCode.apply(null, Array.from(chunk));
        }
        const base64Data = btoa(binaryString);
        const base64SizeKB = (base64Data.length / 1024).toFixed(1);
        const jsonSize = JSON.stringify({pixelData: base64Data, width: canvas.width, height: canvas.height, compressed: false, filename: filename}).length;
        const jsonSizeKB = (jsonSize / 1024).toFixed(1);
        showStatus('canvasStatus', 'Uncompressed: ' + rawSizeKB + ' KB raw (' + base64SizeKB + ' KB base64). Total JSON: ' + jsonSizeKB + ' KB (compression not available). Saving as: ' + filename, false);
        
        const payload = {
            command: 'canvas_save',
            pixelData: base64Data,
            width: canvas.width,
            height: canvas.height,
            compressed: false,
            filename: filename
        };
        
        publishMessage(payload).then(success => {
            if (success) {
                showStatus('canvasStatus', 'Canvas save command sent successfully! Saving as: ' + filename + ' (no display)', false);
                setBusyState(true, 'Command sent, waiting for device response...');
            } else {
                showStatus('canvasStatus', 'Failed to send command', true);
            }
        });
    }
    } catch (error) {
        console.error('Error in sendCanvasToSave:', error);
        showStatus('canvasStatus', 'Error: ' + error.message, true);
    }
}

async function sendCommand(cmd) {
    const payload = { command: cmd };
    
    if (await publishMessage(payload)) {
        showStatus('commandStatus', cmd + ' command sent successfully!', false);
    } else {
        showStatus('commandStatus', 'Failed to send command', true);
    }
}

async function saveScheduleCommand(schedule) {
    // Save schedule via schedule_set command
    const payload = {
        command: 'schedule_set',
        schedule: schedule
    };
    
    const statusEl = document.getElementById('scheduleStatus');
    if (statusEl) {
        statusEl.textContent = 'Saving schedule...';
        statusEl.className = 'status';
    }
    
    if (await publishMessage(payload)) {
        if (statusEl) {
            statusEl.textContent = 'Schedule update sent. Waiting for confirmation...';
            statusEl.className = 'status';
        }
        if (typeof setBusyState === 'function') {
            setBusyState(true, 'Updating schedule... Please wait for device to respond.');
        }
    } else {
        if (statusEl) {
            statusEl.textContent = 'Failed to send schedule update';
            statusEl.className = 'error status';
        }
    }
}

async function triggerHappyWeather() {
    const payload = { command: 'happy_weather' };
    
    if (await publishMessage(payload)) {
        showStatus('commandStatus', 'Happy weather scene command sent successfully!', false);
        setBusyState(true, 'Command sent, waiting for device response...');
    } else {
        showStatus('commandStatus', 'Failed to send Happy weather scene command', true);
    }
}

async function triggerShuffleOn() {
    const payload = { command: 'shuffle_on' };
    
    if (await publishMessage(payload)) {
        showStatus('commandStatus', 'Shuffle mode enabled command sent successfully!', false);
        setBusyState(true, 'Command sent, waiting for device response...');
    } else {
        showStatus('commandStatus', 'Failed to send shuffle on command', true);
    }
}

async function triggerShuffleOff() {
    const payload = { command: 'shuffle_off' };
    
    if (await publishMessage(payload)) {
        showStatus('commandStatus', 'Shuffle mode disabled command sent successfully!', false);
        setBusyState(true, 'Command sent, waiting for device response...');
    } else {
        showStatus('commandStatus', 'Failed to send shuffle off command', true);
    }
}

// Generate default filename with timestamp
function generateDefaultCanvasFilename() {
    const now = new Date();
    const timestamp = now.toISOString().replace(/[:.]/g, '-').slice(0, -5);
    return `canvas_${timestamp}.png`;
}

// Unified canvas action handler (replaces the three separate functions)
async function sendCanvasAction() {
    // Clear selection before finalizing
    
    // Finalize any pending elements before sending
    if (typeof finalizePendingElements === 'function' && pendingElements && pendingElements.length > 0) {
        finalizePendingElements();
    }
    
    const actionSelect = document.getElementById('canvasActionSelect');
    const actionBtn = document.getElementById('canvasActionBtn');
    
    if (!actionSelect || !actionBtn) {
        console.error('Canvas action elements not found');
        return;
    }
    
    const action = actionSelect.value;
    const filenameInputEl = document.getElementById('canvasFilenameInput');
    let filename = filenameInputEl ? filenameInputEl.value.trim() : '';
    
    // If filename is empty, use default
    if (!filename) {
        filename = generateDefaultCanvasFilename();
        if (filenameInputEl) {
            filenameInputEl.value = filename;
        }
    }
    
    // Ensure filename ends with .png
    if (!filename.toLowerCase().endsWith('.png')) {
        filename += '.png';
        if (filenameInputEl) {
            filenameInputEl.value = filename;
        }
    }
    
    const canvas = document.getElementById('drawCanvas');
    if (!canvas) {
        console.error('Canvas element not found');
        showStatus('canvasStatus', 'Error: Canvas not found', true);
        return;
    }
    const ctx = canvas.getContext('2d');
    if (!ctx) {
        console.error('Could not get canvas context');
        showStatus('canvasStatus', 'Error: Could not access canvas', true);
        return;
    }
    
    try {
        const imageData = ctx.getImageData(0, 0, canvas.width, canvas.height);
        const data = imageData.data;
    
        // Actual e-ink display palette colors (order: Black, White, Yellow, Red, Blue, Green)
    const einkColors = [[26,26,26],[242,241,230],[240,224,80],[160,32,32],[80,128,184],[96,128,80]];
        const einkColorValues = [0,1,2,3,5,6];
        
        function findClosestColorIdx(r, g, b) {
            let minDist = Infinity;
            let closestIdx = 0;
            for (let i = 0; i < einkColors.length; i++) {
                const ec = einkColors[i];
                const dist = Math.pow(r-ec[0],2) + Math.pow(g-ec[1],2) + Math.pow(b-ec[2],2);
                if (dist < minDist) {
                    minDist = dist;
                    closestIdx = i;
                }
            }
            return closestIdx;
        }
        
        const pixelData = [];
        for (let i = 0; i < data.length; i += 4) {
            const r = data[i];
            const g = data[i+1];
            const b = data[i+2];
            const arrayIdx = findClosestColorIdx(r, g, b);
            const einkColorValue = einkColorValues[arrayIdx];
            pixelData.push(einkColorValue);
        }
        
        const pixelBytes = new Uint8Array(pixelData);
        const rawSizeKB = (pixelBytes.length / 1024).toFixed(1);
        showStatus('canvasStatus', 'Compressing pixel data (' + rawSizeKB + ' KB raw)...', false);
        
        // Determine command based on action
        let command = 'canvas_display';
        if (action === 'display-save') {
            command = 'canvas_display_save';
        } else if (action === 'save') {
            command = 'canvas_save';
        }
        
        // Try to compress using browser's CompressionStream API (deflate/zlib)
        if (typeof CompressionStream !== 'undefined') {
            const stream = new CompressionStream('deflate');
            const writer = stream.writable.getWriter();
            const reader = stream.readable.getReader();
            
            writer.write(pixelBytes).then(() => writer.close());
            
            const compressedChunks = [];
            function pump() {
                return reader.read().then(({done, value}) => {
                    if (done) return;
                    compressedChunks.push(value);
                    return pump();
                });
            }
            
            pump().then(() => {
                const compressedLength = compressedChunks.reduce((sum, chunk) => sum + chunk.length, 0);
                const compressed = new Uint8Array(compressedLength);
                let offset = 0;
                compressedChunks.forEach(chunk => {
                    compressed.set(chunk, offset);
                    offset += chunk.length;
                });
                
                // Convert compressed Uint8Array to binary string in chunks
                let binaryString = '';
                const chunkSize = 8192;
                for (let i = 0; i < compressed.length; i += chunkSize) {
                    const chunk = compressed.slice(i, i + chunkSize);
                    binaryString += String.fromCharCode.apply(null, Array.from(chunk));
                }
                const base64Data = btoa(binaryString);
                
                const compressedSizeKB = (compressed.length / 1024).toFixed(1);
                const base64SizeKB = (base64Data.length / 1024).toFixed(1);
                const ratio = ((compressed.length / pixelBytes.length) * 100).toFixed(1);
                const savedKB = ((pixelBytes.length - compressed.length) / 1024).toFixed(1);
                
                const payload = {
                    command: command,
                    pixelData: base64Data,
                    width: canvas.width,
                    height: canvas.height,
                    compressed: true
                };
                
                // Add filename for save actions
                if (command !== 'canvas_display') {
                    payload.filename = filename;
                }
                
                const jsonSize = JSON.stringify(payload).length;
                const jsonSizeKB = (jsonSize / 1024).toFixed(1);
                const actionDesc = action === 'display' ? 'Display' : (action === 'display-save' ? 'Display & Save' : 'Save');
                const filenameText = command !== 'canvas_display' ? '. Saving as: ' + filename : '';
                showStatus('canvasStatus', 'Compressed: ' + compressedSizeKB + ' KB binary (' + base64SizeKB + ' KB base64), ' + ratio + '% of ' + rawSizeKB + ' KB raw. Saved: ' + savedKB + ' KB. Total JSON: ' + jsonSizeKB + ' KB' + filenameText, false);
                
                publishMessage(payload).then(success => {
                    if (success) {
                        showStatus('canvasStatus', 'Canvas ' + actionDesc.toLowerCase() + ' command sent successfully!' + filenameText, false);
                        setBusyState(true, 'Command sent, waiting for device response...');
                    } else {
                        showStatus('canvasStatus', 'Failed to send command', true);
                    }
                });
            }).catch(e => {
                console.error('Compression error:', e);
                showStatus('canvasStatus', 'Compression error: ' + e + ', sending uncompressed', true);
                // Fallback to uncompressed
                sendCanvasActionUncompressed(canvas, pixelBytes, command, filename);
            });
        } else {
            // Compression not available - send uncompressed
            sendCanvasActionUncompressed(canvas, pixelBytes, command, filename);
        }
    } catch (error) {
        console.error('Error in sendCanvasAction:', error);
        showStatus('canvasStatus', 'Error: ' + error.message, true);
    }
}

// Helper function for uncompressed canvas action
function sendCanvasActionUncompressed(canvas, pixelBytes, command, filename) {
    let binaryString = '';
    const chunkSize = 8192;
    for (let i = 0; i < pixelBytes.length; i += chunkSize) {
        const chunk = pixelBytes.slice(i, i + chunkSize);
        binaryString += String.fromCharCode.apply(null, Array.from(chunk));
    }
    const base64Data = btoa(binaryString);
    const base64SizeKB = (base64Data.length / 1024).toFixed(1);
    const rawSizeKB = (pixelBytes.length / 1024).toFixed(1);
    
    const payload = {
        command: command,
        pixelData: base64Data,
        width: canvas.width,
        height: canvas.height,
        compressed: false
    };
    
    // Add filename for save actions
    if (command !== 'canvas_display') {
        payload.filename = filename;
    }
    
    const jsonSize = JSON.stringify(payload).length;
    const jsonSizeKB = (jsonSize / 1024).toFixed(1);
    const actionDesc = command === 'canvas_display' ? 'Display' : (command === 'canvas_display_save' ? 'Display & Save' : 'Save');
    const filenameText = command !== 'canvas_display' ? '. Saving as: ' + filename : '';
    showStatus('canvasStatus', 'Uncompressed: ' + rawSizeKB + ' KB raw (' + base64SizeKB + ' KB base64). Total JSON: ' + jsonSizeKB + ' KB (compression not available)' + filenameText, false);
    
    publishMessage(payload).then(success => {
        if (success) {
            showStatus('canvasStatus', 'Canvas ' + actionDesc.toLowerCase() + ' command sent successfully!' + filenameText, false);
            setBusyState(true, 'Command sent, waiting for device response...');
        } else {
            showStatus('canvasStatus', 'Failed to send command', true);
        }
    });
}


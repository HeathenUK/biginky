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
    
    showStatus('textStatus', 'Sending text display command...', false);
    
    const payload = {
        command: 'text_display',
        text: text,
        color: color,
        backgroundColour: bgColor,
        outlineColour: outlineColor
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
    
    // Matching firmware palette from EL133UF1_Color.cpp useDefaultPalette()
    const einkColors = [[10,10,10],[245,245,235],[245,210,50],[190,60,55],[45,75,160],[55,140,85]];
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
    
    // Matching firmware palette from EL133UF1_Color.cpp useDefaultPalette()
    const einkColors = [[10,10,10],[245,245,235],[245,210,50],[190,60,55],[45,75,160],[55,140,85]];
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
    
    // Matching firmware palette from EL133UF1_Color.cpp useDefaultPalette()
    const einkColors = [[10,10,10],[245,245,235],[245,210,50],[190,60,55],[45,75,160],[55,140,85]];
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

// Generate default filename with timestamp
function generateDefaultCanvasFilename() {
    const now = new Date();
    const timestamp = now.toISOString().replace(/[:.]/g, '-').slice(0, -5);
    return `canvas_${timestamp}.png`;
}

// Unified canvas action handler (replaces the three separate functions)
async function sendCanvasAction() {
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
    
        // Matching firmware palette from EL133UF1_Color.cpp useDefaultPalette()
    const einkColors = [[10,10,10],[245,245,235],[245,210,50],[190,60,55],[45,75,160],[55,140,85]];
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


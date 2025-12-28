// Command sending functions

async function sendTextDisplay() {
    const text = document.getElementById('textInput').value.trim();
    if (text.length === 0) {
        showStatus('textStatus', 'Please enter some text', true);
        return;
    }
    
    const color = document.getElementById('textColor').value;
    const bgColor = document.getElementById('textBackgroundColor').value;
    const outlineColor = document.getElementById('textOutlineColor').value;
    
    showStatus('textStatus', 'Sending text display command...', false);
    
    const payload = {
        command: 'text_display',
        text: text,
        color: color,
        backgroundColour: bgColor,
        outlineColour: outlineColor
    };
    
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
    
    const einkColors = [[0,0,0],[255,255,255],[255,255,0],[255,0,0],[0,0,255],[0,255,0]];
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

async function sendCommand(cmd) {
    const payload = { command: cmd };
    
    if (await publishMessage(payload)) {
        showStatus('commandStatus', cmd + ' command sent successfully!', false);
    } else {
        showStatus('commandStatus', 'Failed to send command', true);
    }
}


// Canvas drawing tools and history management

// Canvas variables
let canvas = document.getElementById('drawCanvas');
let ctx = canvas ? canvas.getContext('2d') : null;
let isDrawing = false;
let lastX = 0;
let lastY = 0;
let startX = 0;
let startY = 0;
let canvasHistory = [];
let canvasRedoHistory = []; // Track states that were undone for redo
let historyIndex = -1;
let lastUncompressedState = null; // Keep last state uncompressed for preview
let currentDrawColor = '1'; // Default to white
let currentFillColor = '1'; // Default to white
let currentOutlineColor = '0'; // Default to black

const colorMap = {
    0: '#000000',  // Black
    1: '#FFFFFF',  // White
    2: '#FFFF00',  // Yellow
    3: '#FF0000',  // Red
    5: '#0000FF',  // Blue
    6: '#00FF00'   // Green
};

function getDrawColor() {
    const val = parseInt(currentDrawColor);
    return colorMap[val] || '#000000';
}

function getDrawColorValue() {
    return currentDrawColor;
}

function setDrawColor(colorValue) {
    currentDrawColor = colorValue;
    // Hidden input is updated by color picker component
}

function getFillColor() {
    if (currentFillColor === 'transparent') {
        return 'transparent';
    }
    const numVal = parseInt(currentFillColor);
    return colorMap[numVal] || '#FFFFFF';
}

function getFillColorValue() {
    return currentFillColor;
}

function setFillColor(colorValue) {
    currentFillColor = colorValue;
    // Hidden input is updated by color picker component
}

function getOutlineColor() {
    if (currentOutlineColor === 'transparent') {
        return 'transparent';
    }
    const numVal = parseInt(currentOutlineColor);
    return colorMap[numVal] || '#000000';
}

function getOutlineColorValue() {
    return currentOutlineColor;
}

function setOutlineColor(colorValue) {
    currentOutlineColor = colorValue;
    // Hidden input is updated by color picker component
}

function getBrushSize() {
    const slider = document.getElementById('brushSize');
    return slider ? parseInt(slider.value) : 2;
}

function getLineWidth() {
    const slider = document.getElementById('lineWidth');
    return slider ? parseInt(slider.value) : 2;
}

function getRoundedRectRadius() {
    const slider = document.getElementById('roundedRectRadius');
    return slider ? parseInt(slider.value) : 20;
}

function setTool(tool) {
    const drawToolEl = document.getElementById('drawTool');
    if (drawToolEl) {
        drawToolEl.value = tool;
    }
    // Update active button
    document.querySelectorAll('.tool-btn').forEach(btn => btn.classList.remove('active'));
    const toolBtn = document.getElementById('tool-' + tool);
    if (toolBtn) {
        toolBtn.classList.add('active');
    }
    // Update canvas cursor based on tool
    if (canvas) {
        if (tool === 'eyedropper') {
            canvas.style.cursor = 'crosshair';
        } else {
            canvas.style.cursor = 'crosshair';
        }
    }
    // Trigger change event to update UI
    if (drawToolEl) {
        drawToolEl.dispatchEvent(new Event('change'));
    } else {
        // Fallback: manually update UI if element doesn't exist yet
        updateToolOptions(tool);
    }
}

function getCurrentTool() {
    const drawToolEl = document.getElementById('drawTool');
    return drawToolEl ? drawToolEl.value || 'brush' : 'brush';
}

function updateToolOptions(tool) {
    const textOptions = document.getElementById('textToolOptions');
    const textInput = document.getElementById('textInputContainer');
    const textFontFamily = document.getElementById('textFontFamilyContainer');
    const textAlign = document.getElementById('textAlignContainer');
    const fillColorContainer = document.getElementById('fillColorContainer');
    const outlineColorContainer = document.getElementById('outlineColorContainer');
    const brushSizeContainer = document.getElementById('brushSizeContainer');
    const lineWidthContainer = document.getElementById('lineWidthContainer');
    const roundedRectRadiusContainer = document.getElementById('roundedRectRadiusContainer');
    const colorLabel = document.getElementById('colorLabel');
    const colorContainer = colorLabel ? colorLabel.parentElement : null;
    
    // Show/hide text options
    if (textOptions && textInput) {
        if (tool === 'text') {
            textOptions.style.display = 'block';
            textInput.style.display = 'block';
            if (textFontFamily) textFontFamily.style.display = 'block';
            if (textAlign) textAlign.style.display = 'block';
        } else {
            textOptions.style.display = 'none';
            textInput.style.display = 'none';
            if (textFontFamily) textFontFamily.style.display = 'none';
            if (textAlign) textAlign.style.display = 'none';
        }
    }
    
    // Show/hide brush size for brush and eraser
    if (brushSizeContainer) {
        if (tool === 'brush' || tool === 'eraser') {
            brushSizeContainer.style.display = 'block';
        } else {
            brushSizeContainer.style.display = 'none';
        }
    }
    
    // Show/hide line width for shapes
    if (lineWidthContainer) {
        if (tool === 'rectangle' || tool === 'roundedRect' || tool === 'circle' || tool === 'line') {
            lineWidthContainer.style.display = 'block';
        } else {
            lineWidthContainer.style.display = 'none';
        }
    }
    
    // Show/hide corner radius for rounded rectangle
    if (roundedRectRadiusContainer) {
        if (tool === 'roundedRect') {
            roundedRectRadiusContainer.style.display = 'block';
        } else {
            roundedRectRadiusContainer.style.display = 'none';
        }
    }
    
    // Show/hide fill and outline for shapes
    if (fillColorContainer && outlineColorContainer && colorLabel && colorContainer) {
        if (tool === 'rectangle' || tool === 'roundedRect' || tool === 'circle') {
            // Hide regular color selector, show fill and outline
            colorContainer.style.display = 'none';
            fillColorContainer.style.display = 'block';
            outlineColorContainer.style.display = 'block';
        } else {
            // Show regular color selector, hide fill and outline
            colorContainer.style.display = 'block';
            fillColorContainer.style.display = 'none';
            outlineColorContainer.style.display = 'none';
        }
    }
}

// Initialize tool on page load (defer until DOM is ready)
function initializeCanvasTools() {
    if (document.getElementById('drawTool')) {
        setTool('brush');
        // Ensure UI is updated for initial tool
        updateToolOptions('brush');
    }
}

if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initializeCanvasTools);
} else {
    // DOM already loaded
    setTimeout(initializeCanvasTools, 0);
}

function getCanvasCoordinates(e) {
    const rect = canvas.getBoundingClientRect();
    const scaleX = canvas.width / rect.width;
    const scaleY = canvas.height / rect.height;
    const clientX = e.clientX !== undefined ? e.clientX : e.touches[0].clientX;
    const clientY = e.clientY !== undefined ? e.clientY : e.touches[0].clientY;
    return {
        x: (clientX - rect.left) * scaleX,
        y: (clientY - rect.top) * scaleY
    };
}

// Compress ImageData for storage (reduces memory by ~70-90% for e-ink images)
async function compressImageData(imageData) {
    if (typeof CompressionStream === 'undefined') {
        return null; // Compression not available
    }
    
    try {
        const data = new Uint8Array(imageData.data);
        const stream = new CompressionStream('deflate');
        const writer = stream.writable.getWriter();
        const reader = stream.readable.getReader();
        
        writer.write(data).then(() => writer.close());
        
        const compressedChunks = [];
        function pump() {
            return reader.read().then(({done, value}) => {
                if (done) return;
                compressedChunks.push(value);
                return pump();
            });
        }
        
        await pump();
        
        const compressedLength = compressedChunks.reduce((sum, chunk) => sum + chunk.length, 0);
        const compressed = new Uint8Array(compressedLength);
        let offset = 0;
        compressedChunks.forEach(chunk => {
            compressed.set(chunk, offset);
            offset += chunk.length;
        });
        
        return {
            compressed: compressed,
            width: imageData.width,
            height: imageData.height
        };
    } catch (e) {
        console.warn('Failed to compress canvas state:', e);
        return null;
    }
}

// Decompress ImageData from storage
async function decompressImageData(compressedState) {
    if (!compressedState || !compressedState.compressed) {
        return compressedState; // Already uncompressed ImageData
    }
    
    try {
        const stream = new DecompressionStream('deflate');
        const writer = stream.writable.getWriter();
        const reader = stream.readable.getReader();
        
        writer.write(compressedState.compressed).then(() => writer.close());
        
        const decompressedChunks = [];
        function pump() {
            return reader.read().then(({done, value}) => {
                if (done) return;
                decompressedChunks.push(value);
                return pump();
            });
        }
        
        await pump();
        
        const decompressedLength = decompressedChunks.reduce((sum, chunk) => sum + chunk.length, 0);
        const decompressed = new Uint8Array(decompressedLength);
        let offset = 0;
        decompressedChunks.forEach(chunk => {
            decompressed.set(chunk, offset);
            offset += chunk.length;
        });
        
        // Reconstruct ImageData
        const imageData = ctx.createImageData(compressedState.width, compressedState.height);
        imageData.data.set(decompressed);
        return imageData;
    } catch (e) {
        console.error('Failed to decompress canvas state:', e);
        throw e;
    }
}

function saveCanvasState() {
    // Save current canvas state for undo
    // Store uncompressed initially for fast undo, compress in background to save memory
    const imageData = ctx.getImageData(0, 0, canvas.width, canvas.height);
    
    historyIndex++;
    canvasHistory = canvasHistory.slice(0, historyIndex);
    canvasHistory.push(imageData); // Store uncompressed for now
    
    // Clear redo history when new action is performed
    canvasRedoHistory = [];
    
    // Keep the most recent state uncompressed for real-time preview
    lastUncompressedState = imageData;
    
    // Compress in background and replace (saves ~70-90% memory for e-ink images)
    compressImageData(imageData).then(compressed => {
        if (compressed) {
            // Find and replace this state with compressed version
            const idx = canvasHistory.indexOf(imageData);
            if (idx >= 0) {
                canvasHistory[idx] = compressed;
                // If this was the last state, keep it uncompressed for preview
                if (idx === historyIndex) {
                    lastUncompressedState = imageData; // Keep uncompressed copy
                }
            }
        }
    }).catch(e => {
        console.warn('Background compression failed, keeping uncompressed:', e);
    });
    
    // Increased limit to 20 states since compressed versions use much less memory
    if (canvasHistory.length > 20) {
        canvasHistory.shift();
        historyIndex--;
    }
    
    const undoBtn = document.getElementById('undoBtn');
    if (undoBtn) {
        undoBtn.disabled = historyIndex < 0;
    }
    const redoBtn = document.getElementById('redoBtn');
    if (redoBtn) {
        redoBtn.disabled = canvasRedoHistory.length === 0;
    }
}

async function restoreCanvasState() {
    if (historyIndex >= 0) {
        // Save current state to redo history
        const currentState = ctx.getImageData(0, 0, canvas.width, canvas.height);
        canvasRedoHistory.push(currentState);
        
        const state = canvasHistory[historyIndex];
        // Decompress if needed, otherwise use directly
        const imageData = await decompressImageData(state);
        ctx.putImageData(imageData, 0, 0);
        // Update lastUncompressedState for preview
        lastUncompressedState = imageData;
        historyIndex--;
        
        const undoBtn = document.getElementById('undoBtn');
        if (undoBtn) {
            undoBtn.disabled = historyIndex < 0;
        }
        const redoBtn = document.getElementById('redoBtn');
        if (redoBtn) {
            redoBtn.disabled = canvasRedoHistory.length === 0;
        }
    }
}

async function redoCanvasState() {
    if (canvasRedoHistory.length > 0) {
        // Save current state to undo history
        const currentState = ctx.getImageData(0, 0, canvas.width, canvas.height);
        historyIndex++;
        canvasHistory = canvasHistory.slice(0, historyIndex);
        canvasHistory.push(currentState);
        
        // Restore from redo history
        const state = canvasRedoHistory.pop();
        const imageData = await decompressImageData(state);
        ctx.putImageData(imageData, 0, 0);
        lastUncompressedState = imageData;
        
        const undoBtn = document.getElementById('undoBtn');
        if (undoBtn) {
            undoBtn.disabled = historyIndex < 0;
        }
        const redoBtn = document.getElementById('redoBtn');
        if (redoBtn) {
            redoBtn.disabled = canvasRedoHistory.length === 0;
        }
    }
}

// Optimized flood fill algorithm with chunked processing
function floodFill(x, y, fillColor) {
    const startX = Math.floor(x);
    const startY = Math.floor(y);
    
    if (startX < 0 || startX >= canvas.width || startY < 0 || startY >= canvas.height) {
        return;
    }
    
    const imageData = ctx.getImageData(0, 0, canvas.width, canvas.height);
    const data = imageData.data;
    const width = canvas.width;
    
    // Get target color directly from data array
    const startIdx = (startY * width + startX) * 4;
    const targetR = data[startIdx];
    const targetG = data[startIdx + 1];
    const targetB = data[startIdx + 2];
    
    // Parse fill color
    const fillR = parseInt(fillColor.substring(1, 3), 16);
    const fillG = parseInt(fillColor.substring(3, 5), 16);
    const fillB = parseInt(fillColor.substring(5, 7), 16);
    
    // Check if already filled
    if (targetR === fillR && targetG === fillG && targetB === fillB) {
        return;
    }
    
    // Use bit array for visited tracking (much faster than Set with strings)
    const visited = new Uint8Array(width * canvas.height);
    const stack = [[startX, startY]];
    let processed = 0;
    const MAX_PIXELS_PER_FRAME = 50000; // Process in chunks to avoid blocking
    
    function processChunk() {
        const startTime = performance.now();
        const maxTime = 16; // ~60fps
        
        while (stack.length > 0 && processed < MAX_PIXELS_PER_FRAME) {
            if (performance.now() - startTime > maxTime) {
                // Yield to browser, continue next frame
                requestAnimationFrame(processChunk);
                return;
            }
            
            const [cx, cy] = stack.pop();
            
            if (cx < 0 || cx >= width || cy < 0 || cy >= canvas.height) {
                continue;
            }
            
            const idx = (cy * width + cx);
            if (visited[idx]) {
                continue;
            }
            
            const pixelIdx = idx * 4;
            const r = data[pixelIdx];
            const g = data[pixelIdx + 1];
            const b = data[pixelIdx + 2];
            
            if (r !== targetR || g !== targetG || b !== targetB) {
                continue;
            }
            
            visited[idx] = 1;
            data[pixelIdx] = fillR;
            data[pixelIdx + 1] = fillG;
            data[pixelIdx + 2] = fillB;
            data[pixelIdx + 3] = 255;
            
            processed++;
            
            // Add neighbors
            if (cx > 0) stack.push([cx - 1, cy]);
            if (cx < width - 1) stack.push([cx + 1, cy]);
            if (cy > 0) stack.push([cx, cy - 1]);
            if (cy < canvas.height - 1) stack.push([cx, cy + 1]);
        }
        
        if (stack.length > 0) {
            // More to process, continue next frame
            processed = 0;
            requestAnimationFrame(processChunk);
        } else {
            // Done, update canvas
            ctx.putImageData(imageData, 0, 0);
        }
    }
    
    processChunk();
}

function getPixelColor(data, x, y, width) {
    const idx = (y * width + x) * 4;
    return {
        r: data[idx],
        g: data[idx + 1],
        b: data[idx + 2],
        a: data[idx + 3]
    };
}

// Adjust image for e-ink display: brightness +10%, contrast +20%, saturation +20%
function adjustImageForEink(imageData) {
    const data = imageData.data;
    
    // Brightness: +10% (multiply by 1.1)
    // Contrast: +20% (using standard contrast formula)
    // Saturation: +20% (convert RGB to HSL, adjust, convert back)
    
    for (let i = 0; i < data.length; i += 4) {
        let r = data[i];
        let g = data[i + 1];
        let b = data[i + 2];
        
        // Apply brightness (+10%)
        r = Math.min(255, Math.max(0, r * 1.1));
        g = Math.min(255, Math.max(0, g * 1.1));
        b = Math.min(255, Math.max(0, b * 1.1));
        
        // Apply contrast (+20%)
        // Contrast formula: new = (old - 128) * (1 + contrast) + 128
        const contrast = 0.20;
        r = Math.min(255, Math.max(0, (r - 128) * (1 + contrast) + 128));
        g = Math.min(255, Math.max(0, (g - 128) * (1 + contrast) + 128));
        b = Math.min(255, Math.max(0, (b - 128) * (1 + contrast) + 128));
        
        // Apply saturation (+20%)
        // Convert RGB to HSL
        r /= 255;
        g /= 255;
        b /= 255;
        
        const max = Math.max(r, g, b);
        const min = Math.min(r, g, b);
        let h, s, l = (max + min) / 2;
        
        if (max === min) {
            h = s = 0; // achromatic
        } else {
            const d = max - min;
            s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
            
            if (max === r) {
                h = ((g - b) / d + (g < b ? 6 : 0)) / 6;
            } else if (max === g) {
                h = ((b - r) / d + 2) / 6;
            } else {
                h = ((r - g) / d + 4) / 6;
            }
        }
        
        // Increase saturation by 20%
        s = Math.min(1, s * 1.20);
        
        // Convert HSL back to RGB
        if (s === 0) {
            r = g = b = l; // achromatic
        } else {
            const hue2rgb = (p, q, t) => {
                if (t < 0) t += 1;
                if (t > 1) t -= 1;
                if (t < 1/6) return p + (q - p) * 6 * t;
                if (t < 1/2) return q;
                if (t < 2/3) return p + (q - p) * (2/3 - t) * 6;
                return p;
            };
            
            const q = l < 0.5 ? l * (1 + s) : l + s - l * s;
            const p = 2 * l - q;
            r = hue2rgb(p, q, h + 1/3);
            g = hue2rgb(p, q, h);
            b = hue2rgb(p, q, h - 1/3);
        }
        
        // Convert back to 0-255 range
        data[i] = Math.round(r * 255);
        data[i + 1] = Math.round(g * 255);
        data[i + 2] = Math.round(b * 255);
    }
    
    return imageData;
}

// Posterize image to match e-ink colors using Atkinson dithering
// Based on: https://github.com/Toon-nooT/PhotoPainter-E-Ink-Spectra-6-image-converter
function posterizeImage(x, y, width, height) {
    const einkColors = [[0,0,0],[255,255,255],[255,255,0],[255,0,0],[0,0,255],[0,255,0]];
    
    // Precompute luma values for palette colors
    // Luma formula: (r * 250 + g * 350 + b * 400) / (255.0 * 1000)
    const paletteLuma = einkColors.map(c => (c[0] * 250 + c[1] * 350 + c[2] * 400) / (255.0 * 1000));
    
    function findClosestColorIdx(r, g, b) {
        // Calculate luma for input pixel
        const luma1 = (r * 250 + g * 350 + b * 400) / (255.0 * 1000);
        
        let minDist = Infinity;
        let closestIdx = 0;
        
        for (let i = 0; i < einkColors.length; i++) {
            const ec = einkColors[i];
            
            // RGB component distance with weighted channels
            // Weights: R*0.250, G*0.350, B*0.400 (compensates for human eye sensitivity and e-ink display)
            const diffR = r - ec[0];
            const diffG = g - ec[1];
            const diffB = b - ec[2];
            const rgbDist = (diffR * diffR * 0.250 + diffG * diffG * 0.350 + diffB * diffB * 0.400) * 0.75 / (255.0 * 255.0);
            
            // Luma distance
            const lumaDiff = luma1 - paletteLuma[i];
            const lumaDist = lumaDiff * lumaDiff;
            
            // Total distance: RGB distance weighted more heavily (hue errors are more important)
            const totalDist = 1.5 * rgbDist + 0.60 * lumaDist;
            
            if (totalDist < minDist) {
                minDist = totalDist;
                closestIdx = i;
            }
        }
        return closestIdx;
    }
    
    const imageData = ctx.getImageData(x, y, width, height);
    const data = imageData.data;
    
    // Atkinson dithering (distributes 75% of error to 6 neighbors, 1/8 each)
    for (let py = 0; py < height; py++) {
        for (let px = 0; px < width; px++) {
            const idx = (py * width + px) * 4;
            let r = data[idx];
            let g = data[idx + 1];
            let b = data[idx + 2];
            
            // Find closest e-ink color
            const closestIdx = findClosestColorIdx(r, g, b);
            const targetColor = einkColors[closestIdx];
            
            // Calculate error
            const errorR = r - targetColor[0];
            const errorG = g - targetColor[1];
            const errorB = b - targetColor[2];
            
            // Set pixel to target color
            data[idx] = targetColor[0];
            data[idx + 1] = targetColor[1];
            data[idx + 2] = targetColor[2];
            
            // Distribute error to neighboring pixels (Atkinson: 5/8 total, standard pattern)
            // Pattern: Right: 1/8, Bottom-left: 1/8, Bottom: 1/4, Bottom-right: 1/8
            // Based on: https://github.com/Toon-nooT/PhotoPainter-E-Ink-Spectra-6-image-converter
            
            // Right (1/8)
            if (px < width - 1) {
                const rightIdx = idx + 4;
                data[rightIdx] = Math.max(0, Math.min(255, data[rightIdx] + errorR * (1/8)));
                data[rightIdx + 1] = Math.max(0, Math.min(255, data[rightIdx + 1] + errorG * (1/8)));
                data[rightIdx + 2] = Math.max(0, Math.min(255, data[rightIdx + 2] + errorB * (1/8)));
            }
            
            if (py < height - 1) {
                const bottomIdx = idx + width * 4;
                
                // Bottom-left (1/8)
                if (px > 0) {
                    const bottomLeftIdx = bottomIdx - 4;
                    data[bottomLeftIdx] = Math.max(0, Math.min(255, data[bottomLeftIdx] + errorR * (1/8)));
                    data[bottomLeftIdx + 1] = Math.max(0, Math.min(255, data[bottomLeftIdx + 1] + errorG * (1/8)));
                    data[bottomLeftIdx + 2] = Math.max(0, Math.min(255, data[bottomLeftIdx + 2] + errorB * (1/8)));
                }
                
                // Bottom (1/4 - double weight)
                data[bottomIdx] = Math.max(0, Math.min(255, data[bottomIdx] + errorR * (1/4)));
                data[bottomIdx + 1] = Math.max(0, Math.min(255, data[bottomIdx + 1] + errorG * (1/4)));
                data[bottomIdx + 2] = Math.max(0, Math.min(255, data[bottomIdx + 2] + errorB * (1/4)));
                
                // Bottom-right (1/8)
                if (px < width - 1) {
                    const bottomRightIdx = bottomIdx + 4;
                    data[bottomRightIdx] = Math.max(0, Math.min(255, data[bottomRightIdx] + errorR * (1/8)));
                    data[bottomRightIdx + 1] = Math.max(0, Math.min(255, data[bottomRightIdx + 1] + errorG * (1/8)));
                    data[bottomRightIdx + 2] = Math.max(0, Math.min(255, data[bottomRightIdx + 2] + errorB * (1/8)));
                }
            }
        }
    }
    
    ctx.putImageData(imageData, x, y);
}

function startDraw(e) {
    e.preventDefault();
    const coords = getCanvasCoordinates(e);
    const tool = getCurrentTool();
    
    if (tool === 'text') {
        const text = document.getElementById('canvasTextInput').value.trim();
        if (text) {
            const fontSize = document.getElementById('textFontSize').value;
            const fontFamily = document.getElementById('textFontFamily') ? document.getElementById('textFontFamily').value : 'Arial';
            
            // Load Google Font if needed before drawing
            if (typeof loadGoogleFont === 'function') {
                loadGoogleFont(fontFamily).then(() => {
                    // Font loaded, now draw the text
                    saveCanvasState();
                    ctx.fillStyle = getDrawColor();
                    ctx.font = fontSize + 'px ' + fontFamily;
                    ctx.textAlign = document.getElementById('textAlign') ? document.getElementById('textAlign').value : 'left';
                    ctx.textBaseline = 'top';
                    ctx.fillText(text, coords.x, coords.y);
                }).catch(() => {
                    // Font loading failed, use fallback
                    saveCanvasState();
                    ctx.fillStyle = getDrawColor();
                    ctx.font = fontSize + 'px ' + (fontFamily || 'Arial');
                    ctx.textAlign = document.getElementById('textAlign') ? document.getElementById('textAlign').value : 'left';
                    ctx.textBaseline = 'top';
                    ctx.fillText(text, coords.x, coords.y);
                });
            } else {
                // Fallback if font loader not available
                saveCanvasState();
                ctx.fillStyle = getDrawColor();
                ctx.font = fontSize + 'px ' + fontFamily;
                ctx.textAlign = document.getElementById('textAlign') ? document.getElementById('textAlign').value : 'left';
                ctx.textBaseline = 'top';
                ctx.fillText(text, coords.x, coords.y);
            }
        }
        return;
    }
    
    if (tool === 'eyedropper') {
        // Pick color from canvas at click position
        const imageData = ctx.getImageData(Math.floor(coords.x), Math.floor(coords.y), 1, 1);
        const r = imageData.data[0];
        const g = imageData.data[1];
        const b = imageData.data[2];
        const rgb = `rgb(${r},${g},${b})`;
        
        // Find closest color in palette
        let closestColor = '0';
        let minDist = Infinity;
        for (const [key, value] of Object.entries(colorMap)) {
            const hexR = parseInt(value.substring(1, 3), 16);
            const hexG = parseInt(value.substring(3, 5), 16);
            const hexB = parseInt(value.substring(5, 7), 16);
            const dist = Math.sqrt(Math.pow(r - hexR, 2) + Math.pow(g - hexG, 2) + Math.pow(b - hexB, 2));
            if (dist < minDist) {
                minDist = dist;
                closestColor = key;
            }
        }
        
        setDrawColor(closestColor);
        return;
    }
    
    if (tool === 'fill') {
        saveCanvasState();
        floodFill(Math.floor(coords.x), Math.floor(coords.y), getDrawColor());
        return;
    }
    
    isDrawing = true;
    startX = lastX = coords.x;
    startY = lastY = coords.y;
    
    if (tool === 'rectangle' || tool === 'roundedRect' || tool === 'circle' || tool === 'line') {
        saveCanvasState();
    }
}

let previewCanvas = null;
let previewCtx = null;

function draw(e) {
    if (!isDrawing) return;
    e.preventDefault();
    const coords = getCanvasCoordinates(e);
    const tool = getCurrentTool();
    
    if (tool === 'brush' || tool === 'eraser') {
        const x = coords.x;
        const y = coords.y;
        ctx.strokeStyle = tool === 'eraser' ? '#FFFFFF' : getDrawColor();
        ctx.lineWidth = getBrushSize();
        ctx.lineCap = 'round';
        ctx.beginPath();
        ctx.moveTo(lastX, lastY);
        ctx.lineTo(x, y);
        ctx.stroke();
        lastX = x;
        lastY = y;
    } else if (tool === 'rectangle' || tool === 'roundedRect' || tool === 'circle' || tool === 'line') {
        // Restore last saved state and draw preview
        // Use lastUncompressedState if available (for real-time preview)
        if (lastUncompressedState) {
            ctx.putImageData(lastUncompressedState, 0, 0);
        } else if (historyIndex >= 0 && canvasHistory[historyIndex]) {
            const state = canvasHistory[historyIndex];
            // Check if it's already ImageData (uncompressed) or compressed
            if (state.data && state.width && state.height) {
                // Already ImageData - use directly
                ctx.putImageData(state, 0, 0);
            }
            // If compressed, we can't restore synchronously - shape will draw on current state
        }
        
        ctx.lineWidth = getLineWidth();
        ctx.beginPath();
        
        if (tool === 'rectangle') {
            const width = coords.x - startX;
            const height = coords.y - startY;
            const x = Math.min(startX, coords.x);
            const y = Math.min(startY, coords.y);
            const w = Math.abs(width);
            const h = Math.abs(height);
            // Fill first, then stroke
            ctx.fillStyle = getFillColor();
            ctx.fillRect(x, y, w, h);
            ctx.strokeStyle = getOutlineColor();
            ctx.strokeRect(x, y, w, h);
        } else if (tool === 'roundedRect') {
            const width = coords.x - startX;
            const height = coords.y - startY;
            const radius = getRoundedRectRadius();
            const x = Math.min(startX, coords.x);
            const y = Math.min(startY, coords.y);
            const w = Math.abs(width);
            const h = Math.abs(height);
            const r = Math.min(radius, Math.min(w, h) / 2); // Limit radius to half the smallest dimension
            
            ctx.beginPath();
            ctx.moveTo(x + r, y);
            ctx.lineTo(x + w - r, y);
            ctx.quadraticCurveTo(x + w, y, x + w, y + r);
            ctx.lineTo(x + w, y + h - r);
            ctx.quadraticCurveTo(x + w, y + h, x + w - r, y + h);
            ctx.lineTo(x + r, y + h);
            ctx.quadraticCurveTo(x, y + h, x, y + h - r);
            ctx.lineTo(x, y + r);
            ctx.quadraticCurveTo(x, y, x + r, y);
            ctx.closePath();
            
            // Fill first, then stroke
            ctx.fillStyle = getFillColor();
            ctx.fill();
            ctx.strokeStyle = getOutlineColor();
            ctx.stroke();
        } else if (tool === 'circle') {
            const radius = Math.sqrt(Math.pow(coords.x - startX, 2) + Math.pow(coords.y - startY, 2));
            ctx.arc(startX, startY, radius, 0, Math.PI * 2);
            // Fill first, then stroke
            ctx.fillStyle = getFillColor();
            ctx.fill();
            ctx.strokeStyle = getOutlineColor();
            ctx.stroke();
        } else if (tool === 'line') {
            ctx.strokeStyle = getDrawColor();
            ctx.moveTo(startX, startY);
            ctx.lineTo(coords.x, coords.y);
            ctx.stroke();
        }
    }
}

function stopDraw() {
    if (!isDrawing) return;
    isDrawing = false;
    
    const tool = getCurrentTool();
    if (tool === 'brush' || tool === 'eraser') {
        saveCanvasState();
    }
    // Rectangle, roundedRect, circle, and line already saved state in startDraw
}

async function undoCanvas() {
    await restoreCanvasState();
}

async function redoCanvas() {
    await redoCanvasState();
}

// Update tool options visibility
function setupToolChangeListener() {
    const drawToolEl = document.getElementById('drawTool');
    if (drawToolEl) {
        drawToolEl.addEventListener('change', function() {
            updateToolOptions(this.value);
        });
    }
}

// Setup listener when DOM is ready
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', setupToolChangeListener);
} else {
    setupToolChangeListener();
}

function clearCanvas() {
    if (!canvas || !ctx) return;
    saveCanvasState();
    ctx.fillStyle = '#FFFFFF';
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    showStatus('canvasStatus', 'Canvas cleared', false);
}

function loadImageToCanvas() {
    document.getElementById('imageFileInput').click();
}

function handleImageFileSelect(event) {
    const file = event.target.files[0];
    if (!file) return;
    
    if (!file.type.startsWith('image/')) {
        showStatus('canvasStatus', 'Error: Please select an image file', true);
        return;
    }
    
    const reader = new FileReader();
    reader.onload = function(e) {
        const img = new Image();
        img.onload = function() {
            saveCanvasState();
            
            // Calculate scaling to fit canvas while maintaining aspect ratio
            const canvasAspect = canvas.width / canvas.height;
            const imgAspect = img.width / img.height;
            
            let drawWidth, drawHeight, drawX, drawY;
            
            if (imgAspect > canvasAspect) {
                // Image is wider - fit to width
                drawWidth = canvas.width;
                drawHeight = canvas.width / imgAspect;
                drawX = 0;
                drawY = (canvas.height - drawHeight) / 2;
            } else {
                // Image is taller - fit to height
                drawWidth = canvas.height * imgAspect;
                drawHeight = canvas.height;
                drawX = (canvas.width - drawWidth) / 2;
                drawY = 0;
            }
            
            // Clear canvas and draw image centered
            ctx.fillStyle = '#FFFFFF';
            ctx.fillRect(0, 0, canvas.width, canvas.height);
            ctx.drawImage(img, drawX, drawY, drawWidth, drawHeight);
            
            // Adjust image for e-ink display (brightness +10%, contrast +20%, saturation +20%)
            const imageData = ctx.getImageData(drawX, drawY, drawWidth, drawHeight);
            adjustImageForEink(imageData);
            ctx.putImageData(imageData, drawX, drawY);
            
            // Posterize image to match e-ink colors using Atkinson dithering
            posterizeImage(drawX, drawY, drawWidth, drawHeight);
            
            showStatus('canvasStatus', `Image loaded and posterized: ${img.width}x${img.height} (scaled to fit ${Math.round(drawWidth)}x${Math.round(drawHeight)})`, false);
        };
        img.onerror = function() {
            showStatus('canvasStatus', 'Error: Failed to load image', true);
        };
        img.src = e.target.result;
    };
    reader.onerror = function() {
        showStatus('canvasStatus', 'Error: Failed to read file', true);
    };
    reader.readAsDataURL(file);
    
    // Reset file input so same file can be selected again
    event.target.value = '';
}

// Initialize canvas when DOM is ready
function initializeCanvas() {
    canvas = document.getElementById('drawCanvas');
    if (canvas) {
        ctx = canvas.getContext('2d');
        
        // Set up event listeners
        canvas.addEventListener('mousedown', startDraw);
        canvas.addEventListener('mousemove', draw);
        canvas.addEventListener('mouseup', stopDraw);
        canvas.addEventListener('mouseleave', stopDraw);
        canvas.addEventListener('touchstart', (e) => { e.preventDefault(); startDraw(e); });
        canvas.addEventListener('touchmove', (e) => { e.preventDefault(); draw(e); });
        canvas.addEventListener('touchend', (e) => { e.preventDefault(); stopDraw(); });
        
        // Clear canvas on initialization
        clearCanvas();
    }
    
    // Set up brush size slider
    const brushSizeSlider = document.getElementById('brushSize');
    const brushSizeDisplay = document.getElementById('brushSizeDisplay');
    if (brushSizeSlider && brushSizeDisplay) {
        brushSizeSlider.addEventListener('input', (e) => {
            brushSizeDisplay.textContent = e.target.value + 'px';
        });
    }
    
    // Set up line width slider
    const lineWidthSlider = document.getElementById('lineWidth');
    const lineWidthDisplay = document.getElementById('lineWidthDisplay');
    if (lineWidthSlider && lineWidthDisplay) {
        lineWidthSlider.addEventListener('input', (e) => {
            lineWidthDisplay.textContent = e.target.value + 'px';
        });
    }
    
    // Set up rounded rect radius slider
    const roundedRectRadiusSlider = document.getElementById('roundedRectRadius');
    const roundedRectRadiusDisplay = document.getElementById('roundedRectRadiusDisplay');
    if (roundedRectRadiusSlider && roundedRectRadiusDisplay) {
        roundedRectRadiusSlider.addEventListener('input', (e) => {
            roundedRectRadiusDisplay.textContent = e.target.value + 'px';
        });
    }
    
    // Set up unified color pickers for canvas
    if (typeof createColorPicker === 'function') {
        createColorPicker({
            buttonId: 'drawColorBtn',
            paletteId: 'drawColorPalette',
            hiddenInputId: 'drawColor',
            indicatorId: 'drawColorIndicator',
            labelId: 'drawColorLabel',
            colorType: 'numeric',
            defaultValue: '1',
            onChange: (colorValue) => setDrawColor(colorValue),
            closeOtherPickers: ['fillColorPalette', 'outlineColorPalette']
        });
        
        createColorPicker({
            buttonId: 'fillColorBtn',
            paletteId: 'fillColorPalette',
            hiddenInputId: 'fillColor',
            indicatorId: 'fillColorIndicator',
            labelId: 'fillColorLabel',
            colorType: 'numeric',
            colors: ['transparent', '0', '2', '3', '5', '6', '1'],
            defaultValue: '1',
            onChange: (colorValue) => setFillColor(colorValue),
            closeOtherPickers: ['drawColorPalette', 'outlineColorPalette']
        });
        
        createColorPicker({
            buttonId: 'outlineColorBtn',
            paletteId: 'outlineColorPalette',
            hiddenInputId: 'outlineColor',
            indicatorId: 'outlineColorIndicator',
            labelId: 'outlineColorLabel',
            colorType: 'numeric',
            colors: ['transparent', '0', '2', '3', '5', '6', '1'],
            defaultValue: '0',
            onChange: (colorValue) => setOutlineColor(colorValue),
            closeOtherPickers: ['drawColorPalette', 'fillColorPalette']
        });
    }
}

if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initializeCanvas);
} else {
    initializeCanvas();
}


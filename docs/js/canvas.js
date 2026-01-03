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
let currentDrawColor = '0'; // Default to black
let currentFillColor = '1'; // Default to white
let currentOutlineColor = '0'; // Default to black

// Pending elements system - elements that can be moved before finalization
let pendingElements = [];
let draggingElements = []; // Array of elements being dragged
let dragOffsets = new Map(); // Map of element -> drag offset

// Pending image data for processing
let pendingImageData = null; // Stores {img, drawX, drawY, drawWidth, drawHeight} when image is loaded

// Text input modal state
let pendingTextCoords = null; // Stores {x, y} coordinates where text should be placed
let isTextInputMode = false; // True when directly typing text on canvas
let currentTextElement = null; // The text element being typed
let textInputKeyHandler = null; // Keyboard event handler for direct text input

const colorMap = {
    // Actual e-ink display palette colors
    0: '#1A1A1A',  // Black (26, 26, 26)
    1: '#F2F1E6',  // White (242, 241, 230)
    2: '#F0E050',  // Yellow (240, 224, 80)
    3: '#A02020',  // Red (160, 32, 32)
    5: '#5080B8',  // Blue (80, 128, 184)
    6: '#608050'   // Green (96, 128, 80)
};

function getDrawColor() {
    const val = parseInt(currentDrawColor);
    return colorMap[val] || '#1A1A1A';
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
    return colorMap[numVal] || '#F2F1E6';
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
    return colorMap[numVal] || '#1A1A1A';
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
    // Exit text input mode if switching away from text tool
    if (isTextInputMode && currentTextElement && tool !== 'text') {
        finalizePendingTextElement();
    }
    
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
            canvas.style.cursor = 'crosshair'; // Will change to 'move' when hovering over elements
        }
        // Remove tabindex when not in text mode
        if (tool !== 'text') {
            canvas.removeAttribute('tabindex');
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
    
    // Handle both mouse and touch events
    // For touch events, only use the first touch point (ignore multi-touch)
    let clientX, clientY;
    if (e.clientX !== undefined && e.clientY !== undefined) {
        // Mouse event
        clientX = e.clientX;
        clientY = e.clientY;
    } else if (e.touches && e.touches.length > 0) {
        // Touch event - use first touch only
        clientX = e.touches[0].clientX;
        clientY = e.touches[0].clientY;
    } else if (e.changedTouches && e.changedTouches.length > 0) {
        // Touch end event - use first changed touch
        clientX = e.changedTouches[0].clientX;
        clientY = e.changedTouches[0].clientY;
    } else {
        // Fallback (shouldn't happen)
        console.warn('getCanvasCoordinates: Unable to determine coordinates', e);
        return { x: 0, y: 0 };
    }
    
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

// Adjust image for e-ink display with configurable brightness, contrast, and saturation
// brightnessPercent: -50 to 50 (default: 10)
// contrastPercent: -50 to 50 (default: 20)
// saturationPercent: -50 to 50 (default: 20)
function adjustImageForEink(imageData, brightnessPercent = 10, contrastPercent = 20, saturationPercent = 20) {
    const data = imageData.data;
    
    // Convert percentages to multipliers
    const brightness = 1.0 + (brightnessPercent / 100.0);
    const contrast = contrastPercent / 100.0;
    const saturation = 1.0 + (saturationPercent / 100.0);
    
    for (let i = 0; i < data.length; i += 4) {
        let r = data[i];
        let g = data[i + 1];
        let b = data[i + 2];
        
        // Apply brightness
        r = Math.min(255, Math.max(0, r * brightness));
        g = Math.min(255, Math.max(0, g * brightness));
        b = Math.min(255, Math.max(0, b * brightness));
        
        // Apply contrast
        // Contrast formula: new = (old - 128) * (1 + contrast) + 128
        r = Math.min(255, Math.max(0, (r - 128) * (1 + contrast) + 128));
        g = Math.min(255, Math.max(0, (g - 128) * (1 + contrast) + 128));
        b = Math.min(255, Math.max(0, (b - 128) * (1 + contrast) + 128));
        
        // Apply saturation
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
        
        // Apply saturation adjustment
        s = Math.min(1, Math.max(0, s * saturation));
        
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

// E-ink color palette - actual display colors
// Order: Black, White, Yellow, Red, Blue, Green (matching firmware indices 0, 1, 2, 3, 5, 6)
const einkColors = [[26,26,26],[242,241,230],[240,224,80],[160,32,32],[80,128,184],[96,128,80]];

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

// Posterize image to match e-ink colors (quantize without dithering)
// Based on: https://github.com/Toon-nooT/PhotoPainter-E-Ink-Spectra-6-image-converter
function posterizeImage(x, y, width, height) {
    const imageData = ctx.getImageData(x, y, width, height);
    const data = imageData.data;
    
    // Simply quantize each pixel to the closest e-ink color
    for (let i = 0; i < data.length; i += 4) {
        const r = data[i];
        const g = data[i + 1];
        const b = data[i + 2];
        
        // Find closest e-ink color
        const closestIdx = findClosestColorIdx(r, g, b);
        const targetColor = einkColors[closestIdx];
        
        // Set pixel to target color
        data[i] = targetColor[0];
        data[i + 1] = targetColor[1];
        data[i + 2] = targetColor[2];
    }
    
    ctx.putImageData(imageData, x, y);
}

// Posterize image to match e-ink colors with Atkinson dithering in one pass
// This is the original combined approach that works correctly
// Based on: https://github.com/Toon-nooT/PhotoPainter-E-Ink-Spectra-6-image-converter
function posterizeImageWithDithering(x, y, width, height) {
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
            
            // Calculate error from original continuous color
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

// Disable all processing options (reset to defaults)
function disableAllProcessing() {
    const brightnessSlider = document.getElementById('imageBrightness');
    const contrastSlider = document.getElementById('imageContrast');
    const saturationSlider = document.getElementById('imageSaturation');
    const posterizeCheckbox = document.getElementById('imagePosterize');
    const ditherCheckbox = document.getElementById('imageDither');
    const brightnessDisplay = document.getElementById('imageBrightnessDisplay');
    const contrastDisplay = document.getElementById('imageContrastDisplay');
    const saturationDisplay = document.getElementById('imageSaturationDisplay');
    
    if (brightnessSlider) {
        brightnessSlider.value = 0;
        if (brightnessDisplay) {
            brightnessDisplay.textContent = '0%';
        }
    }
    
    if (contrastSlider) {
        contrastSlider.value = 0;
        if (contrastDisplay) {
            contrastDisplay.textContent = '0%';
        }
    }
    
    if (saturationSlider) {
        saturationSlider.value = 0;
        if (saturationDisplay) {
            saturationDisplay.textContent = '0%';
        }
    }
    
    if (posterizeCheckbox) {
        posterizeCheckbox.checked = false;
    }
    
    if (ditherCheckbox) {
        ditherCheckbox.checked = false;
    }
}

// Apply image processing with configurable settings
function applyImageProcessing() {
    if (!pendingImageData) {
        showStatus('canvasStatus', 'Error: No image loaded. Please load an image first.', true);
        return;
    }
    
    // Get processing parameters from UI
    const brightnessSlider = document.getElementById('imageBrightness');
    const contrastSlider = document.getElementById('imageContrast');
    const saturationSlider = document.getElementById('imageSaturation');
    const posterizeCheckbox = document.getElementById('imagePosterize');
    const ditherCheckbox = document.getElementById('imageDither');
    
    const brightness = brightnessSlider ? parseInt(brightnessSlider.value) : 10;
    const contrast = contrastSlider ? parseInt(contrastSlider.value) : 20;
    const saturation = saturationSlider ? parseInt(saturationSlider.value) : 20;
    const enablePosterize = posterizeCheckbox ? posterizeCheckbox.checked : true;
    const enableDither = ditherCheckbox ? ditherCheckbox.checked : true;
    
    // Redraw the original image first
    ctx.fillStyle = '#F2F1E6';
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    ctx.drawImage(pendingImageData.img, pendingImageData.drawX, pendingImageData.drawY, 
                  pendingImageData.drawWidth, pendingImageData.drawHeight);
    
    // Apply adjustments
    const imageData = ctx.getImageData(pendingImageData.drawX, pendingImageData.drawY, 
                                       pendingImageData.drawWidth, pendingImageData.drawHeight);
    adjustImageForEink(imageData, brightness, contrast, saturation);
    ctx.putImageData(imageData, pendingImageData.drawX, pendingImageData.drawY);
    
    // Apply posterization and/or dithering
    if (enableDither) {
        // If dithering is enabled, do posterization + dithering in one pass
        // This is necessary because dithering needs to calculate errors from original continuous colors
        posterizeImageWithDithering(pendingImageData.drawX, pendingImageData.drawY, 
                                   pendingImageData.drawWidth, pendingImageData.drawHeight);
    } else if (enablePosterize) {
        // If only posterization is enabled (no dithering), just quantize
        posterizeImage(pendingImageData.drawX, pendingImageData.drawY, 
                      pendingImageData.drawWidth, pendingImageData.drawHeight);
    }
    
    // Clear pending image data and hide panel
    pendingImageData = null;
    const processingPanel = document.getElementById('imageProcessingPanel');
    if (processingPanel) {
        processingPanel.style.display = 'none';
    }
    
    // Build status message
    const statusParts = [
        `brightness ${brightness > 0 ? '+' : ''}${brightness}%`,
        `contrast ${contrast > 0 ? '+' : ''}${contrast}%`,
        `saturation ${saturation > 0 ? '+' : ''}${saturation}%`
    ];
    if (enableDither) {
        statusParts.push('posterized and dithered');
    } else if (enablePosterize) {
        statusParts.push('posterized');
    }
    const statusMsg = `Image processed: ${statusParts.join(', ')}`;
    
    showStatus('canvasStatus', statusMsg, false);
}

// Check for hover over pending elements (for cursor feedback)
function checkHover(e) {
    if (!canvas || isDrawing) return;
    const coords = getCanvasCoordinates(e);
    const hit = getPendingElementAt(coords.x, coords.y);
    
    if (hit) {
        canvas.style.cursor = 'move';
    } else {
        const tool = getCurrentTool();
        canvas.style.cursor = (tool === 'eyedropper') ? 'crosshair' : 'crosshair';
    }
}

// Check if click is on a pending element (with more permissive hit detection)
function getPendingElementAt(x, y) {
    const HIT_MARGIN = 30; // Extra pixels around elements for easier clicking
    
    for (let i = pendingElements.length - 1; i >= 0; i--) {
        const elem = pendingElements[i];
        if (elem.type === 'text') {
            // Approximate text bounds with margin
            ctx.font = elem.fontSize + 'px ' + elem.fontFamily;
            ctx.textAlign = elem.textAlign;
            const metrics = ctx.measureText(elem.text);
            const textWidth = metrics.width;
            const textHeight = parseInt(elem.fontSize);
            let textX = elem.x;
            if (elem.textAlign === 'center') textX -= textWidth / 2;
            else if (elem.textAlign === 'right') textX -= textWidth;
            
            if (x >= textX - HIT_MARGIN && x <= textX + textWidth + HIT_MARGIN && 
                y >= elem.y - HIT_MARGIN && y <= elem.y + textHeight + HIT_MARGIN) {
                return { element: elem, index: i };
            }
        } else if (elem.type === 'rectangle' || elem.type === 'roundedRect') {
            const x1 = Math.min(elem.x, elem.x + elem.width);
            const y1 = Math.min(elem.y, elem.y + elem.height);
            const x2 = Math.max(elem.x, elem.x + elem.width);
            const y2 = Math.max(elem.y, elem.y + elem.height);
            // Expand hit area by margin
            if (x >= x1 - HIT_MARGIN && x <= x2 + HIT_MARGIN && 
                y >= y1 - HIT_MARGIN && y <= y2 + HIT_MARGIN) {
                return { element: elem, index: i };
            }
        } else if (elem.type === 'circle') {
            const radius = Math.sqrt(Math.pow(elem.endX - elem.x, 2) + Math.pow(elem.endY - elem.y, 2));
            const dist = Math.sqrt(Math.pow(x - elem.x, 2) + Math.pow(y - elem.y, 2));
            // Expand hit area by margin
            if (dist <= radius + HIT_MARGIN) {
                return { element: elem, index: i };
            }
        } else if (elem.type === 'line') {
            // Check if click is near the line (within margin pixels)
            const dist = distanceToLineSegment(x, y, elem.x, elem.y, elem.endX, elem.endY);
            if (dist <= HIT_MARGIN) {
                return { element: elem, index: i };
            }
        }
    }
    return null;
}

function distanceToLineSegment(px, py, x1, y1, x2, y2) {
    const A = px - x1;
    const B = py - y1;
    const C = x2 - x1;
    const D = y2 - y1;
    const dot = A * C + B * D;
    const lenSq = C * C + D * D;
    let param = -1;
    if (lenSq !== 0) param = dot / lenSq;
    let xx, yy;
    if (param < 0) {
        xx = x1;
        yy = y1;
    } else if (param > 1) {
        xx = x2;
        yy = y2;
    } else {
        xx = x1 + param * C;
        yy = y1 + param * D;
    }
    const dx = px - xx;
    const dy = py - yy;
    return Math.sqrt(dx * dx + dy * dy);
}

// Finalize all pending elements (draw them permanently to canvas)
function finalizePendingElements() {
    if (pendingElements.length === 0) return;
    
    saveCanvasState();
    
    // Draw all pending elements to canvas
    for (const elem of pendingElements) {
        drawPendingElement(elem, true);
    }
    
    pendingElements = [];
    draggingElements = [];
    dragOffsets.clear();
}

// Draw a pending element (either as preview or finalized)
function drawPendingElement(elem, finalized = false) {
    if (elem.type === 'text') {
        ctx.fillStyle = getDrawColor();
        ctx.font = elem.fontSize + 'px ' + elem.fontFamily;
        ctx.textAlign = elem.textAlign;
        ctx.textBaseline = 'top';
        ctx.fillText(elem.text, elem.x, elem.y);
    } else if (elem.type === 'rectangle') {
        const x = Math.min(elem.x, elem.x + elem.width);
        const y = Math.min(elem.y, elem.y + elem.height);
        const w = Math.abs(elem.width);
        const h = Math.abs(elem.height);
        ctx.fillStyle = getFillColor();
        ctx.fillRect(x, y, w, h);
        ctx.strokeStyle = getOutlineColor();
        ctx.lineWidth = elem.lineWidth;
        ctx.strokeRect(x, y, w, h);
    } else if (elem.type === 'roundedRect') {
        const x = Math.min(elem.x, elem.x + elem.width);
        const y = Math.min(elem.y, elem.y + elem.height);
        const w = Math.abs(elem.width);
        const h = Math.abs(elem.height);
        const radius = getRoundedRectRadius();
        const r = Math.min(radius, Math.min(w, h) / 2);
        
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
        ctx.fillStyle = getFillColor();
        ctx.fill();
        ctx.strokeStyle = getOutlineColor();
        ctx.lineWidth = elem.lineWidth;
        ctx.stroke();
    } else if (elem.type === 'circle') {
        const radius = Math.sqrt(Math.pow(elem.endX - elem.x, 2) + Math.pow(elem.endY - elem.y, 2));
        ctx.beginPath();
        ctx.arc(elem.x, elem.y, radius, 0, Math.PI * 2);
        ctx.fillStyle = getFillColor();
        ctx.fill();
        ctx.strokeStyle = getOutlineColor();
        ctx.lineWidth = elem.lineWidth;
        ctx.stroke();
    } else if (elem.type === 'line') {
        ctx.strokeStyle = getDrawColor();
        ctx.lineWidth = elem.lineWidth;
        ctx.beginPath();
        ctx.moveTo(elem.x, elem.y);
        ctx.lineTo(elem.endX, elem.endY);
        ctx.stroke();
    }
}

// Redraw canvas with pending elements
function redrawCanvas() {
    // Restore last saved state
    if (lastUncompressedState) {
        ctx.putImageData(lastUncompressedState, 0, 0);
    } else if (historyIndex >= 0 && canvasHistory[historyIndex]) {
        const state = canvasHistory[historyIndex];
        if (state.data && state.width && state.height) {
            ctx.putImageData(state, 0, 0);
        }
    }
    
    // Draw all pending elements
    for (const elem of pendingElements) {
        drawPendingElement(elem, false);
    }
}

// Removed updateFinalizeButton - no longer needed since "Go" button finalizes

function startDraw(e) {
    e.preventDefault();
    const coords = getCanvasCoordinates(e);
    const tool = getCurrentTool();
    
    // Check if clicking on a pending element - if so, start dragging it
    const hit = getPendingElementAt(coords.x, coords.y);
    if (hit) {
        // Start dragging this element
        draggingElements = [hit.element];
        dragOffsets.clear();
        
        // Calculate drag offset
        let offset = { x: 0, y: 0 };
        const elem = hit.element;
        if (elem.type === 'circle' || elem.type === 'line') {
            const centerX = (elem.x + elem.endX) / 2;
            const centerY = (elem.y + elem.endY) / 2;
            offset.x = coords.x - centerX;
            offset.y = coords.y - centerY;
        } else if (elem.type === 'rectangle' || elem.type === 'roundedRect') {
            const visualX = Math.min(elem.x, elem.x + elem.width);
            const visualY = Math.min(elem.y, elem.y + elem.height);
            offset.x = coords.x - visualX;
            offset.y = coords.y - visualY;
        } else if (elem.type === 'text') {
            ctx.font = elem.fontSize + 'px ' + elem.fontFamily;
            ctx.textAlign = elem.textAlign;
            const metrics = ctx.measureText(elem.text);
            const textWidth = metrics.width;
            let textX = elem.x;
            if (elem.textAlign === 'center') textX -= textWidth / 2;
            else if (elem.textAlign === 'right') textX -= textWidth;
            offset.x = coords.x - textX;
            offset.y = coords.y - elem.y;
        } else {
            offset.x = coords.x - elem.x;
            offset.y = coords.y - elem.y;
        }
        dragOffsets.set(elem, offset);
        
        isDrawing = true;
        redrawCanvas();
        return; // Don't create new element
    }
    
    if (tool === 'text') {
        // Finalize any existing pending elements (including text) before starting new text
        if (pendingElements.length > 0) {
            finalizePendingElements();
        }
        
        // Exit any existing text input mode
        if (isTextInputMode && currentTextElement) {
            isTextInputMode = false;
            currentTextElement = null;
        }
        
        // Store coordinates for text placement
        pendingTextCoords = { x: coords.x, y: coords.y };
        
        // Get text settings
        const fontSize = document.getElementById('textFontSize') ? document.getElementById('textFontSize').value : '96';
        const fontFamily = document.getElementById('textFontFamily') ? document.getElementById('textFontFamily').value : 'Arial';
        const textAlign = document.getElementById('textAlign') ? document.getElementById('textAlign').value : 'left';
        
        // Create pending text element immediately (empty text)
        saveCanvasState();
        currentTextElement = {
            type: 'text',
            text: '',
            x: coords.x,
            y: coords.y,
            fontSize: fontSize,
            fontFamily: fontFamily,
            textAlign: textAlign
        };
        pendingElements.push(currentTextElement);
        isTextInputMode = true;
        
        // Make canvas focusable and focus it to capture keyboard input
        if (canvas) {
            canvas.setAttribute('tabindex', '0');
            canvas.focus();
        }
        
        // Set up global keyboard handler for text input
        if (!textInputKeyHandler) {
            textInputKeyHandler = (e) => {
                if (!isTextInputMode || !currentTextElement) return;
                
                // Ignore all keypresses when modifiers are pressed (allow shortcuts to work)
                if (e.ctrlKey || e.metaKey || e.altKey) {
                    return; // Let browser shortcuts and other handlers process this
                }
                
                // Only handle specific keys or single characters
                if (e.key === 'Enter') {
                    // Finish text input
                    e.preventDefault();
                    e.stopPropagation();
                    finalizePendingTextElement();
                } else if (e.key === 'Escape') {
                    // Cancel text input
                    e.preventDefault();
                    e.stopPropagation();
                    cancelPendingTextElement();
                } else if (e.key === 'Backspace') {
                    // Delete last character
                    e.preventDefault();
                    e.stopPropagation();
                    if (currentTextElement.text.length > 0) {
                        currentTextElement.text = currentTextElement.text.slice(0, -1);
                        redrawCanvas();
                    }
                } else if (e.key.length === 1) {
                    // Regular character (including shifted characters like 'R' from Shift+R)
                    // Note: ctrl/meta/alt modifiers already checked above and would have returned
                    e.preventDefault();
                    e.stopPropagation();
                    currentTextElement.text += e.key;
                    redrawCanvas();
                }
            };
            document.addEventListener('keydown', textInputKeyHandler);
        }
        
        redrawCanvas();
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

function draw(e) {
    try {
        if (!isDrawing) {
            return;
        }
        e.preventDefault();
        const coords = getCanvasCoordinates(e);
        const tool = getCurrentTool();
        
        // Handle dragging pending elements
        if (draggingElements.length > 0) {
            
            for (const draggingElement of draggingElements) {
                const dragOffset = dragOffsets.get(draggingElement);
                if (!dragOffset) continue;
                
                if (draggingElement.type === 'circle' || draggingElement.type === 'line') {
                    // For circle/line, move the center to the new position
                    const oldCenterX = (draggingElement.x + draggingElement.endX) / 2;
                    const oldCenterY = (draggingElement.y + draggingElement.endY) / 2;
                    const newCenterX = coords.x - dragOffset.x;
                    const newCenterY = coords.y - dragOffset.y;
                    const dx = newCenterX - oldCenterX;
                    const dy = newCenterY - oldCenterY;
                    draggingElement.x += dx;
                    draggingElement.y += dy;
                    draggingElement.endX += dx;
                    draggingElement.endY += dy;
                } else if (draggingElement.type === 'rectangle' || draggingElement.type === 'roundedRect') {
                    // For rectangles, move the visual top-left corner
                    const oldVisualX = Math.min(draggingElement.x, draggingElement.x + draggingElement.width);
                    const oldVisualY = Math.min(draggingElement.y, draggingElement.y + draggingElement.height);
                    const newVisualX = coords.x - dragOffset.x;
                    const newVisualY = coords.y - dragOffset.y;
                    const dx = newVisualX - oldVisualX;
                    const dy = newVisualY - oldVisualY;
                    draggingElement.x += dx;
                    draggingElement.y += dy;
                } else if (draggingElement.type === 'text') {
                    // For text, move the position accounting for text alignment
                    ctx.font = draggingElement.fontSize + 'px ' + draggingElement.fontFamily;
                    ctx.textAlign = draggingElement.textAlign;
                    const metrics = ctx.measureText(draggingElement.text);
                    const textWidth = metrics.width;
                    // Calculate the new visual x position
                    const newVisualX = coords.x - dragOffset.x;
                    // Convert back to stored x position based on alignment
                    if (draggingElement.textAlign === 'center') {
                        draggingElement.x = newVisualX + textWidth / 2;
                    } else if (draggingElement.textAlign === 'right') {
                        draggingElement.x = newVisualX + textWidth;
                    } else {
                        draggingElement.x = newVisualX;
                    }
                    draggingElement.y = coords.y - dragOffset.y;
                } else {
                    // Fallback for any other element types
                    draggingElement.x = coords.x - dragOffset.x;
                    draggingElement.y = coords.y - dragOffset.y;
                }
            }
            
            redrawCanvas();
            return;
        }
        
        if (tool === 'brush' || tool === 'eraser') {
        const x = coords.x;
        const y = coords.y;
        ctx.strokeStyle = tool === 'eraser' ? '#F2F1E6' : getDrawColor();
        ctx.lineWidth = getBrushSize();
        ctx.lineCap = 'round';
        ctx.beginPath();
        ctx.moveTo(lastX, lastY);
        ctx.lineTo(x, y);
        ctx.stroke();
        lastX = x;
        lastY = y;
    } else if (tool === 'rectangle' || tool === 'roundedRect' || tool === 'circle' || tool === 'line') {
        // Don't create new shapes if we're dragging existing elements
        if (draggingElements.length > 0) {
            return;
        }
        
        // Remove any existing pending element of this type (only one shape at a time)
        pendingElements = pendingElements.filter(e => e.type !== tool);
        
        // Create or update pending shape element
        const lineWidth = getLineWidth();
        let shapeElem = null;
        
        if (tool === 'rectangle') {
            shapeElem = {
                type: 'rectangle',
                x: startX,
                y: startY,
                width: coords.x - startX,
                height: coords.y - startY,
                lineWidth: lineWidth
            };
        } else if (tool === 'roundedRect') {
            shapeElem = {
                type: 'roundedRect',
                x: startX,
                y: startY,
                width: coords.x - startX,
                height: coords.y - startY,
                lineWidth: lineWidth
            };
        } else if (tool === 'circle') {
            shapeElem = {
                type: 'circle',
                x: startX,
                y: startY,
                endX: coords.x,
                endY: coords.y,
                lineWidth: lineWidth
            };
        } else if (tool === 'line') {
            shapeElem = {
                type: 'line',
                x: startX,
                y: startY,
                endX: coords.x,
                endY: coords.y,
                lineWidth: lineWidth
            };
        }
        
        if (shapeElem) {
            pendingElements.push(shapeElem);
            redrawCanvas();
        }
        }
        
    } catch (error) {
        console.error('Error in draw():', error);
    }
}

let previewCanvas = null;
let previewCtx = null;

function stopDraw() {
    if (!isDrawing) return;
    isDrawing = false;
    
    const tool = getCurrentTool();
    
    // Stop dragging if we were dragging
    if (draggingElements.length > 0) {
        draggingElements = [];
        dragOffsets.clear();
        return;
    }
    
    if (tool === 'brush' || tool === 'eraser') {
        saveCanvasState();
    }
    // Shapes are now in pendingElements, not finalized until user finalizes them
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
    ctx.fillStyle = '#F2F1E6';
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    pendingElements = [];
    draggingElements = [];
    dragOffsets.clear();
    pendingImageData = null;
    
    // Exit text input mode if active
    isTextInputMode = false;
    currentTextElement = null;
    pendingTextCoords = null;
    if (canvas) {
        canvas.removeAttribute('tabindex');
    }
    
    // Hide processing panel
    const processingPanel = document.getElementById('imageProcessingPanel');
    if (processingPanel) {
        processingPanel.style.display = 'none';
    }
    
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
            // Clear pending elements when loading new image
            pendingElements = [];
            draggingElements = [];
            dragOffsets.clear();
            
            // Hide processing panel if it was showing from a previous image
            let processingPanel = document.getElementById('imageProcessingPanel');
            if (processingPanel) {
                processingPanel.style.display = 'none';
            }
            
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
            
            // Clear canvas and draw image centered (unprocessed)
            ctx.fillStyle = '#F2F1E6';
            ctx.fillRect(0, 0, canvas.width, canvas.height);
            ctx.drawImage(img, drawX, drawY, drawWidth, drawHeight);
            
            // Store image data for processing
            pendingImageData = {
                img: img,
                drawX: drawX,
                drawY: drawY,
                drawWidth: drawWidth,
                drawHeight: drawHeight
            };
            
            // Show processing panel
            processingPanel = document.getElementById('imageProcessingPanel');
            if (processingPanel) {
                processingPanel.style.display = 'block';
            }
            
            showStatus('canvasStatus', `Image loaded: ${img.width}x${img.height} (scaled to fit ${Math.round(drawWidth)}x${Math.round(drawHeight)}). Adjust settings and click 'Apply Processing'.`, false);
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

function loadFramebufferToCanvas() {
    // Load the current framebuffer PNG onto the canvas
    // This is the full-resolution PNG that was published by the device
    // It's already palette-optimized, so we don't need to dither/posterize
    
    if (!currentFramebufferData) {
        showStatus('canvasStatus', 'Error: No framebuffer data available. Wait for thumbnail update from device.', true);
        return;
    }
    
    if (currentFramebufferData.format !== 'png') {
        showStatus('canvasStatus', 'Error: Framebuffer data is not in PNG format', true);
        return;
    }
    
    try {
        saveCanvasState();
        
        // Create data URL from stored PNG data
        const dataUrl = `data:image/png;base64,${currentFramebufferData.data}`;
        const img = new Image();
        
        img.onload = function() {
            // Clear pending elements when loading framebuffer
            pendingElements = [];
            draggingElements = [];
            dragOffsets.clear();
            
            // Clear canvas
            ctx.fillStyle = '#F2F1E6';
            ctx.fillRect(0, 0, canvas.width, canvas.height);
            
            // Draw framebuffer at full size (canvas is 1600x1200, framebuffer should be 1600x1200)
            // No scaling needed - framebuffer should match canvas size exactly
            ctx.drawImage(img, 0, 0, canvas.width, canvas.height);
            
            showStatus('canvasStatus', `Framebuffer loaded: ${currentFramebufferData.width}x${currentFramebufferData.height} (no dithering - already palette-optimized)`, false);
        };
        
        img.onerror = function() {
            showStatus('canvasStatus', 'Error: Failed to load framebuffer image', true);
        };
        
        img.src = dataUrl;
    } catch (e) {
        console.error('Error loading framebuffer to canvas:', e);
        showStatus('canvasStatus', 'Error: ' + e.message, true);
    }
}

// Initialize canvas when DOM is ready
function initializeCanvas() {
    canvas = document.getElementById('drawCanvas');
    if (canvas) {
        ctx = canvas.getContext('2d');
        
        // Set up event listeners
        canvas.addEventListener('mousedown', startDraw);
        canvas.addEventListener('mousemove', (e) => {
            if (!isDrawing) {
                checkHover(e);
            } else {
                draw(e);
            }
        });
        canvas.addEventListener('mouseup', stopDraw);
        canvas.addEventListener('mouseleave', stopDraw);
        // Touch event handlers with multi-touch protection
        canvas.addEventListener('touchstart', (e) => {
            // Only handle single touch - ignore if multiple touches
            if (e.touches.length === 1) {
                e.preventDefault();
                startDraw(e);
            } else if (e.touches.length > 1) {
                // Multiple touches - stop any ongoing drawing to prevent issues
                if (isDrawing) {
                    stopDraw();
                }
                e.preventDefault();
            }
        }, { passive: false });
        
        canvas.addEventListener('touchmove', (e) => {
            // Only handle single touch - ignore multi-touch gestures
            if (e.touches.length === 1 && isDrawing) {
                e.preventDefault();
                draw(e);
            } else if (e.touches.length > 1) {
                // Multi-touch detected - stop drawing
                if (isDrawing) {
                    stopDraw();
                }
                e.preventDefault();
            }
        }, { passive: false });
        
        canvas.addEventListener('touchend', (e) => {
            // Handle touch end - stop drawing if we were drawing
            if (isDrawing || e.changedTouches.length > 0) {
                e.preventDefault();
                stopDraw();
            }
        }, { passive: false });
        
        // Prevent context menu on long press (interferes with drawing)
        canvas.addEventListener('contextmenu', (e) => {
            e.preventDefault();
        });
        
        // Keyboard shortcuts
        document.addEventListener('keydown', (e) => {
            // Enter to finalize pending elements
            if (e.key === 'Enter' && pendingElements.length > 0) {
                e.preventDefault();
                finalizePendingElements();
            }
            // Escape to cancel pending elements
            if (e.key === 'Escape' && pendingElements.length > 0) {
                e.preventDefault();
                pendingElements = [];
                draggingElements = [];
                dragOffsets.clear();
                redrawCanvas();
            }
        });
        
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
    
    // Set up image processing sliders
    const imageBrightnessSlider = document.getElementById('imageBrightness');
    const imageBrightnessDisplay = document.getElementById('imageBrightnessDisplay');
    if (imageBrightnessSlider && imageBrightnessDisplay) {
        imageBrightnessSlider.addEventListener('input', (e) => {
            const val = parseInt(e.target.value);
            imageBrightnessDisplay.textContent = (val > 0 ? '+' : '') + val + '%';
        });
    }
    
    const imageContrastSlider = document.getElementById('imageContrast');
    const imageContrastDisplay = document.getElementById('imageContrastDisplay');
    if (imageContrastSlider && imageContrastDisplay) {
        imageContrastSlider.addEventListener('input', (e) => {
            const val = parseInt(e.target.value);
            imageContrastDisplay.textContent = (val > 0 ? '+' : '') + val + '%';
        });
    }
    
    const imageSaturationSlider = document.getElementById('imageSaturation');
    const imageSaturationDisplay = document.getElementById('imageSaturationDisplay');
    if (imageSaturationSlider && imageSaturationDisplay) {
        imageSaturationSlider.addEventListener('input', (e) => {
            const val = parseInt(e.target.value);
            imageSaturationDisplay.textContent = (val > 0 ? '+' : '') + val + '%';
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
            defaultValue: '0',
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

// Show text input modal for mobile-friendly text entry
function showTextInputModal(initialText = '') {
    const modal = document.getElementById('textInputModal');
    const modalInput = document.getElementById('modalTextInput');
    
    if (modal && modalInput) {
        modalInput.value = initialText;
        modal.style.display = 'flex';
        
        // Close modal when clicking outside (on overlay)
        const closeOnOverlayClick = (e) => {
            if (e.target === modal) {
                cancelTextInput();
                modal.removeEventListener('click', closeOnOverlayClick);
            }
        };
        modal.addEventListener('click', closeOnOverlayClick);
        
        // Handle Enter key to confirm (Shift+Enter for new line)
        const handleKeyDown = (e) => {
            if (e.key === 'Enter' && !e.shiftKey) {
                e.preventDefault();
                confirmTextInput();
                modalInput.removeEventListener('keydown', handleKeyDown);
            } else if (e.key === 'Escape') {
                e.preventDefault();
                cancelTextInput();
                modalInput.removeEventListener('keydown', handleKeyDown);
            }
        };
        modalInput.addEventListener('keydown', handleKeyDown);
        
        // Focus the input after a brief delay to ensure modal is visible
        setTimeout(() => {
            modalInput.focus();
            // Move cursor to end of text
            modalInput.setSelectionRange(modalInput.value.length, modalInput.value.length);
        }, 100);
    }
}

// Cancel text input modal
function cancelTextInput() {
    const modal = document.getElementById('textInputModal');
    const modalInput = document.getElementById('modalTextInput');
    
    if (modal) {
        modal.style.display = 'none';
    }
    if (modalInput) {
        modalInput.value = '';
        // Remove any event listeners by cloning and replacing
        const newInput = modalInput.cloneNode(true);
        modalInput.parentNode.replaceChild(newInput, modalInput);
    }
    pendingTextCoords = null;
}

// Finalize the currently being-typed text element
function finalizePendingTextElement() {
    if (!isTextInputMode || !currentTextElement) return;
    
    // Remove empty text elements
    if (currentTextElement.text.trim() === '') {
        cancelPendingTextElement();
        return;
    }
    
    // Finalize all pending elements (including the current text element) to canvas
    finalizePendingElements();
    
    // Exit input mode
    isTextInputMode = false;
    currentTextElement = null;
    pendingTextCoords = null;
    
    // Remove tabindex and blur canvas
    if (canvas) {
        canvas.removeAttribute('tabindex');
        canvas.blur();
    }
}

// Cancel the currently being-typed text element
async function cancelPendingTextElement() {
    if (!isTextInputMode || !currentTextElement) return;
    
    // Remove the text element from pendingElements
    const index = pendingElements.indexOf(currentTextElement);
    if (index >= 0) {
        pendingElements.splice(index, 1);
    }
    
    isTextInputMode = false;
    currentTextElement = null;
    pendingTextCoords = null;
    
    // Remove tabindex and blur canvas
    if (canvas) {
        canvas.removeAttribute('tabindex');
        canvas.blur();
    }
    
    // Restore canvas state (undo the saveCanvasState from startDraw)
    // We manually restore since we don't want to add to redo history
    if (historyIndex >= 0) {
        const state = canvasHistory[historyIndex];
        const imageData = await decompressImageData(state);
        ctx.putImageData(imageData, 0, 0);
        lastUncompressedState = imageData;
        historyIndex--;
        canvasHistory.pop(); // Remove the state we just restored from
        
        const undoBtn = document.getElementById('undoBtn');
        if (undoBtn) {
            undoBtn.disabled = historyIndex < 0;
        }
    }
    
    redrawCanvas();
}

// Confirm text input and place on canvas (for modal - kept for backwards compatibility)
function confirmTextInput() {
    const modalInput = document.getElementById('modalTextInput');
    const text = modalInput ? modalInput.value.trim() : '';
    
    if (!text || !pendingTextCoords) {
        cancelTextInput();
        return;
    }
    
    const fontSize = document.getElementById('textFontSize') ? document.getElementById('textFontSize').value : '96';
    const fontFamily = document.getElementById('textFontFamily') ? document.getElementById('textFontFamily').value : 'Arial';
    const textAlign = document.getElementById('textAlign') ? document.getElementById('textAlign').value : 'left';
    
    // Create pending text element
    saveCanvasState();
    const textElem = {
        type: 'text',
        text: text,
        x: pendingTextCoords.x,
        y: pendingTextCoords.y,
        fontSize: fontSize,
        fontFamily: fontFamily,
        textAlign: textAlign
    };
    pendingElements.push(textElem);
    redrawCanvas();
    
    // Clear modal and coordinates
    cancelTextInput();
}

if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initializeCanvas);
} else {
    initializeCanvas();
}


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
let historyIndex = -1;
let lastUncompressedState = null; // Keep last state uncompressed for preview

const colorMap = {
    0: '#000000',  // Black
    1: '#FFFFFF',  // White
    2: '#FFFF00',  // Yellow
    3: '#FF0000',  // Red
    5: '#0000FF',  // Blue
    6: '#00FF00'   // Green
};

function getDrawColor() {
    const val = parseInt(document.getElementById('drawColor').value);
    return colorMap[val] || '#000000';
}

function getFillColor() {
    const val = parseInt(document.getElementById('fillColor').value);
    return colorMap[val] || '#FFFFFF';
}

function getOutlineColor() {
    const val = parseInt(document.getElementById('outlineColor').value);
    return colorMap[val] || '#000000';
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
    const fillColorContainer = document.getElementById('fillColorContainer');
    const outlineColorContainer = document.getElementById('outlineColorContainer');
    const colorLabel = document.getElementById('colorLabel');
    const colorContainer = colorLabel ? colorLabel.parentElement : null;
    
    // Show/hide text options
    if (textOptions && textInput) {
        if (tool === 'text') {
            textOptions.style.display = 'block';
            textInput.style.display = 'block';
        } else {
            textOptions.style.display = 'none';
            textInput.style.display = 'none';
        }
    }
    
    // Show/hide fill and outline for shapes
    if (fillColorContainer && outlineColorContainer && colorLabel && colorContainer) {
        if (tool === 'rectangle' || tool === 'circle') {
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
}

async function restoreCanvasState() {
    if (historyIndex >= 0) {
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

// Posterize image to match e-ink colors using Atkinson dithering
function posterizeImage(x, y, width, height) {
    const einkColors = [[0,0,0],[255,255,255],[255,255,0],[255,0,0],[0,0,255],[0,255,0]];
    
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
            
            // Distribute error to neighboring pixels (Atkinson: 1/8 to each of 6 neighbors = 75% total)
            const errorFraction = 1/8;
            
            // Right
            if (px < width - 1) {
                const rightIdx = idx + 4;
                data[rightIdx] = Math.max(0, Math.min(255, data[rightIdx] + errorR * errorFraction));
                data[rightIdx + 1] = Math.max(0, Math.min(255, data[rightIdx + 1] + errorG * errorFraction));
                data[rightIdx + 2] = Math.max(0, Math.min(255, data[rightIdx + 2] + errorB * errorFraction));
            }
            
            // Right+1 (two positions to the right)
            if (px < width - 2) {
                const right2Idx = idx + 8;
                data[right2Idx] = Math.max(0, Math.min(255, data[right2Idx] + errorR * errorFraction));
                data[right2Idx + 1] = Math.max(0, Math.min(255, data[right2Idx + 1] + errorG * errorFraction));
                data[right2Idx + 2] = Math.max(0, Math.min(255, data[right2Idx + 2] + errorB * errorFraction));
            }
            
            if (py < height - 1) {
                const bottomIdx = idx + width * 4;
                
                // Bottom-left
                if (px > 0) {
                    const bottomLeftIdx = bottomIdx - 4;
                    data[bottomLeftIdx] = Math.max(0, Math.min(255, data[bottomLeftIdx] + errorR * errorFraction));
                    data[bottomLeftIdx + 1] = Math.max(0, Math.min(255, data[bottomLeftIdx + 1] + errorG * errorFraction));
                    data[bottomLeftIdx + 2] = Math.max(0, Math.min(255, data[bottomLeftIdx + 2] + errorB * errorFraction));
                }
                
                // Bottom
                data[bottomIdx] = Math.max(0, Math.min(255, data[bottomIdx] + errorR * errorFraction));
                data[bottomIdx + 1] = Math.max(0, Math.min(255, data[bottomIdx + 1] + errorG * errorFraction));
                data[bottomIdx + 2] = Math.max(0, Math.min(255, data[bottomIdx + 2] + errorB * errorFraction));
                
                // Bottom-right
                if (px < width - 1) {
                    const bottomRightIdx = bottomIdx + 4;
                    data[bottomRightIdx] = Math.max(0, Math.min(255, data[bottomRightIdx] + errorR * errorFraction));
                    data[bottomRightIdx + 1] = Math.max(0, Math.min(255, data[bottomRightIdx + 1] + errorG * errorFraction));
                    data[bottomRightIdx + 2] = Math.max(0, Math.min(255, data[bottomRightIdx + 2] + errorB * errorFraction));
                }
            }
            
            // Bottom+1 (two rows below)
            if (py < height - 2) {
                const bottom2Idx = idx + width * 8;
                data[bottom2Idx] = Math.max(0, Math.min(255, data[bottom2Idx] + errorR * errorFraction));
                data[bottom2Idx + 1] = Math.max(0, Math.min(255, data[bottom2Idx + 1] + errorG * errorFraction));
                data[bottom2Idx + 2] = Math.max(0, Math.min(255, data[bottom2Idx + 2] + errorB * errorFraction));
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
            saveCanvasState();
            ctx.fillStyle = getDrawColor();
            ctx.font = document.getElementById('textFontSize').value + 'px Arial';
            ctx.fillText(text, coords.x, coords.y);
        }
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
    
    if (tool === 'rectangle' || tool === 'circle' || tool === 'line') {
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
        ctx.lineWidth = 2;
        ctx.lineCap = 'round';
        ctx.beginPath();
        ctx.moveTo(lastX, lastY);
        ctx.lineTo(x, y);
        ctx.stroke();
        lastX = x;
        lastY = y;
    } else if (tool === 'rectangle' || tool === 'circle' || tool === 'line') {
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
        
        ctx.lineWidth = 2;
        ctx.beginPath();
        
        if (tool === 'rectangle') {
            const width = coords.x - startX;
            const height = coords.y - startY;
            // Fill first, then stroke
            ctx.fillStyle = getFillColor();
            ctx.fillRect(startX, startY, width, height);
            ctx.strokeStyle = getOutlineColor();
            ctx.strokeRect(startX, startY, width, height);
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
    // Rectangle, circle, and line already saved state in startDraw
}

async function undoCanvas() {
    await restoreCanvasState();
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
            
            // Posterize image to match e-ink colors
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
}

if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initializeCanvas);
} else {
    initializeCanvas();
}


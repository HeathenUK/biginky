// Media mappings and thumbnail functions

function updateMediaMappingsTable(mappings) {
    const statusEl = document.getElementById('mediaMappingsStatus');
    const tableEl = document.getElementById('mediaMappingsTable');
    
    if (!mappings || mappings.length === 0) {
        statusEl.textContent = 'No media mappings available';
        tableEl.innerHTML = '<p style="color:#888;">No media mappings found.</p>';
        return;
    }
    
    statusEl.textContent = `Loaded ${mappings.length} media mapping(s) (updated ${new Date().toLocaleTimeString()})`;
    
    // Create table with optimized column widths (more efficient space usage)
    let html = '<table style="width:100%;border-collapse:collapse;margin-top:10px;table-layout:fixed;">';
    html += '<thead><tr style="background:#333;color:#e0e0e0;">';
    html += '<th style="padding:8px;text-align:center;border:1px solid #555;width:4%;">#</th>';
    html += '<th style="padding:8px;text-align:center;border:1px solid #555;width:30%;">Thumbnail</th>';
    html += '<th style="padding:8px;text-align:left;border:1px solid #555;width:20%;">Image</th>';
    html += '<th style="padding:8px;text-align:left;border:1px solid #555;width:20%;">Audio</th>';
    html += '<th style="padding:8px;text-align:center;border:1px solid #555;width:14%;">Actions</th>';
    html += '</tr></thead><tbody>';
    
    for (let i = 0; i < mappings.length; i++) {
        const mapping = mappings[i];
        // Always use 1-based index for display and commands (matching firmware convention)
        // Firmware publishes 0-based indices in the JSON, but !go command expects 1-based input
        // So we always use (i + 1) for display, regardless of what mapping.index contains
        const displayIndex = i + 1;
        html += '<tr style="border-bottom:1px solid #555;">';
        html += `<td style="padding:8px;border:1px solid #555;text-align:center;font-weight:bold;">${displayIndex}</td>`;
        
        // Thumbnail column - make thumbnails fill the column better
        html += '<td style="padding:8px;border:1px solid #555;text-align:center;vertical-align:middle;">';
        if (mapping.thumbnail) {
            html += `<img src="data:image/jpeg;base64,${mapping.thumbnail}" class="media-thumbnail" style="max-width:100%;max-height:150px;width:auto;height:auto;border:1px solid #666;display:block;margin:0 auto;" alt="Thumbnail" />`;
        } else {
            html += '<span style="color:#888;">No thumbnail</span>';
        }
        html += '</td>';
        
        // Image column - truncate long filenames with ellipsis, show full on hover
        const imageFile = mapping.image || 'N/A';
        const imageDisplay = imageFile.length > 30 ? imageFile.substring(0, 27) + '...' : imageFile;
        html += `<td style="padding:8px;border:1px solid #555;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-family:monospace;font-size:12px;" title="${imageFile}">${imageDisplay}</td>`;
        
        // Audio column - truncate long filenames with ellipsis, show full on hover
        const audioFile = mapping.audio || '';
        if (audioFile) {
            const audioDisplay = audioFile.length > 30 ? audioFile.substring(0, 27) + '...' : audioFile;
            html += `<td style="padding:8px;border:1px solid #555;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-family:monospace;font-size:12px;" title="${audioFile}">${audioDisplay}</td>`;
        } else {
            html += `<td style="padding:8px;border:1px solid #555;color:#888;font-size:12px;">(none)</td>`;
        }
        
        // Actions column - Show, Edit, Delete buttons
        html += '<td style="padding:8px;border:1px solid #555;text-align:center;vertical-align:middle;">';
        html += `<button onclick="showMediaItem(${displayIndex})" style="padding:6px 12px;font-size:14px;background:#4CAF50;color:white;border:none;border-radius:4px;cursor:pointer;margin:2px;">Show</button>`;
        html += `<button onclick="editMediaMapping(${i})" style="padding:6px 12px;font-size:14px;background:#2196F3;color:white;border:none;border-radius:4px;cursor:pointer;margin:2px;">Edit</button>`;
        html += `<button onclick="deleteMediaMapping(${i})" style="padding:6px 12px;font-size:14px;background:#f44336;color:white;border:none;border-radius:4px;cursor:pointer;margin:2px;">Delete</button>`;
        html += '</td>';
        
        html += '</tr>';
    }
    
    html += '</tbody></table>';
    tableEl.innerHTML = html;
    
    // Enable Add Media button when mappings are loaded
    const addBtn = document.getElementById('addMediaBtn');
    if (addBtn) {
        addBtn.disabled = false;
    }
    
    console.log(`Media mappings table updated with ${mappings.length} entries`);
}

function updateThumbnail(thumb) {
    const canvas = document.getElementById('thumbnailCanvas');
    const statusEl = document.getElementById('thumbnailStatus');
    
    if (!thumb || !thumb.data || !thumb.width || !thumb.height) {
        statusEl.textContent = 'Invalid thumbnail data';
        canvas.style.display = 'none';
        return;
    }
    
    // Set canvas size
    canvas.width = thumb.width;
    canvas.height = thumb.height;
    const ctx = canvas.getContext('2d');
    
    try {
        if (thumb.format === 'jpeg' || thumb.format === 'png') {
            // JPEG/PNG format: create data URL and load as image (much simpler!)
            
            // Validate base64 data before using it
            if (!thumb.data || typeof thumb.data !== 'string') {
                console.error('Invalid thumbnail data:', typeof thumb.data);
                statusEl.textContent = 'Error: Invalid thumbnail data type';
                canvas.style.display = 'none';
                return;
            }
            
            // Validate base64 string format (should only contain valid base64 chars)
            const base64Regex = /^[A-Za-z0-9+/]*={0,2}$/;
            if (!base64Regex.test(thumb.data)) {
                console.error('Invalid base64 characters in thumbnail data');
                console.error('Data length:', thumb.data.length);
                console.error('First 100 chars:', thumb.data.substring(0, 100));
                console.error('Last 100 chars:', thumb.data.substring(Math.max(0, thumb.data.length - 100)));
                statusEl.textContent = 'Error: Invalid base64 data in thumbnail';
                canvas.style.display = 'none';
                return;
            }
            
            // Validate base64 length (should be multiple of 4, with padding)
            if (thumb.data.length % 4 !== 0) {
                console.error('Invalid base64 length (not multiple of 4):', thumb.data.length);
                statusEl.textContent = 'Error: Invalid base64 data length';
                canvas.style.display = 'none';
                return;
            }
            
            // Try to decode base64 to verify it's valid before creating data URL
            try {
                const testDecode = atob(thumb.data);
                console.log('Base64 validation: decoded length =', testDecode.length);
                console.log('Base64 validation: first 4 bytes =', 
                    Array.from(new Uint8Array(testDecode.split('').map(c => c.charCodeAt(0)).slice(0, 4)))
                        .map(b => '0x' + b.toString(16).padStart(2, '0')).join(' '));
                
                // Verify image header (JPEG: FF D8, PNG: 89 50 4E 47)
                if (thumb.format === 'jpeg') {
                    if (testDecode.length >= 2 && testDecode.charCodeAt(0) === 0xFF && testDecode.charCodeAt(1) === 0xD8) {
                        console.log('✓ Valid JPEG header detected');
                    } else {
                        console.error('✗ Invalid JPEG header:', 
                            Array.from(new Uint8Array(testDecode.split('').map(c => c.charCodeAt(0)).slice(0, 4)))
                                .map(b => '0x' + b.toString(16).padStart(2, '0')).join(' '));
                        statusEl.textContent = 'Error: Thumbnail data does not appear to be a valid JPEG';
                        canvas.style.display = 'none';
                        return;
                    }
                } else if (thumb.format === 'png') {
                    if (testDecode.length >= 8 && 
                        testDecode.charCodeAt(0) === 0x89 && 
                        testDecode.charCodeAt(1) === 0x50 && 
                        testDecode.charCodeAt(2) === 0x4E && 
                        testDecode.charCodeAt(3) === 0x47) {
                        console.log('✓ Valid PNG header detected');
                    } else {
                        console.error('✗ Invalid PNG header:', 
                            Array.from(new Uint8Array(testDecode.split('').map(c => c.charCodeAt(0)).slice(0, 8)))
                                .map(b => '0x' + b.toString(16).padStart(2, '0')).join(' '));
                        statusEl.textContent = 'Error: Thumbnail data does not appear to be a valid PNG';
                        canvas.style.display = 'none';
                        return;
                    }
                }
            } catch (decodeError) {
                console.error('Failed to decode base64 data:', decodeError);
                console.error('Base64 data length:', thumb.data.length);
                statusEl.textContent = 'Error: Failed to decode base64 data. Data may be corrupted.';
                canvas.style.display = 'none';
                return;
            }
            
            const mimeType = thumb.format === 'png' ? 'image/png' : 'image/jpeg';
            const dataUrl = `data:${mimeType};base64,${thumb.data}`;
            const img = new Image();
            img.onload = function() {
                // For preview, scale down large images to a reasonable preview size (max 800x600)
                // while maintaining aspect ratio
                const maxPreviewWidth = 800;
                const maxPreviewHeight = 600;
                let previewWidth = thumb.width;
                let previewHeight = thumb.height;
                
                if (previewWidth > maxPreviewWidth || previewHeight > maxPreviewHeight) {
                    const scale = Math.min(maxPreviewWidth / previewWidth, maxPreviewHeight / previewHeight);
                    previewWidth = Math.round(previewWidth * scale);
                    previewHeight = Math.round(previewHeight * scale);
                }
                
                // Set canvas size for preview (scaled down)
                canvas.width = previewWidth;
                canvas.height = previewHeight;
                ctx.clearRect(0, 0, canvas.width, canvas.height);
                
                // Draw image scaled to preview size
                ctx.drawImage(img, 0, 0, previewWidth, previewHeight);
                canvas.style.display = 'block';
                statusEl.textContent = 'Preview: ' + thumb.width + 'x' + thumb.height + ' ' + thumb.format.toUpperCase() + ' (scaled to ' + previewWidth + 'x' + previewHeight + ' for preview, updated ' + new Date().toLocaleTimeString() + ')';
                console.log(thumb.format.toUpperCase() + ' thumbnail updated successfully (scaled from ' + thumb.width + 'x' + thumb.height + ' to ' + previewWidth + 'x' + previewHeight + ' for preview).');
                
                // Store framebuffer data for loading onto canvas (ONLY for PNG format - full resolution)
                // JPEG format is NOT stored as it's a thumbnail, not the full framebuffer
                if (thumb.format === 'png') {
                    currentFramebufferData = {
                        format: 'png',
                        data: thumb.data,
                        width: thumb.width,
                        height: thumb.height
                    };
                    console.log('Framebuffer PNG data stored for canvas loading (full resolution: ' + thumb.width + 'x' + thumb.height + ')');
                    
                    // Enable "Load Frame" button if it exists and we're connected
                    const loadFrameBtn = document.getElementById('loadFrameBtn');
                    if (loadFrameBtn && isConnected && webUIPassword) {
                        loadFrameBtn.disabled = false;
                    }
                } else if (thumb.format === 'jpeg') {
                    // JPEG format is a thumbnail only - clear framebuffer data if it exists
                    // (shouldn't happen, but ensure we don't use stale PNG data)
                    if (currentFramebufferData) {
                        // console.log('JPEG thumbnail received - clearing stored framebuffer data (JPEG is thumbnail only, not full framebuffer)');
                        currentFramebufferData = null;
                        // Disable "Load Frame" button since we don't have full framebuffer
                        const loadFrameBtn = document.getElementById('loadFrameBtn');
                        if (loadFrameBtn) {
                            loadFrameBtn.disabled = true;
                        }
                    }
                }
            };
            img.onerror = function(err) {
                console.error('Failed to load ' + thumb.format.toUpperCase() + ' thumbnail:', err);
                console.error('Data URL length:', dataUrl.length);
                console.error('Base64 data length:', thumb.data.length);
                statusEl.textContent = 'Error loading ' + thumb.format.toUpperCase() + ' preview - image data may be corrupted';
                canvas.style.display = 'none';
            };
            img.src = dataUrl;
        } else if (thumb.format === 'rgb888') {
            // Legacy RGB888 format: decode raw RGB data
            // Validate base64 data before decoding
            if (!/^[A-Za-z0-9+/=]+$/.test(thumb.data)) {
                console.error('Invalid base64 data in RGB888 thumbnail');
                statusEl.textContent = 'Error: Invalid base64 data in thumbnail';
                canvas.style.display = 'none';
                return;
            }
            
            // Validate expected data length
            const expectedBytes = thumb.width * thumb.height * 3;
            let binaryString;
            try {
                binaryString = atob(thumb.data);
            } catch (e) {
                console.error('Failed to decode base64 data:', e);
                statusEl.textContent = 'Error: Failed to decode base64 data. Message may be incomplete.';
                canvas.style.display = 'none';
                return;
            }
            
            if (binaryString.length !== expectedBytes) {
                console.error('RGB888 data length mismatch:', { 
                    expected: expectedBytes, 
                    actual: binaryString.length,
                    width: thumb.width,
                    height: thumb.height
                });
                statusEl.textContent = 'Error: Thumbnail data incomplete. Expected ' + expectedBytes + ' bytes, got ' + binaryString.length + '.';
                canvas.style.display = 'none';
                return;
            }
            
            const bytes = new Uint8Array(binaryString.length);
            for (let i = 0; i < binaryString.length; i++) {
                bytes[i] = binaryString.charCodeAt(i);
            }
            
            // Create ImageData from RGB888 data
            const imageData = ctx.createImageData(thumb.width, thumb.height);
            const data = imageData.data;
            
            // Convert RGB888 to RGBA8888
            for (let i = 0; i < bytes.length; i += 3) {
                const pixelIdx = (i / 3) * 4;
                if (pixelIdx + 3 < data.length) {
                    data[pixelIdx + 0] = bytes[i + 0];     // R
                    data[pixelIdx + 1] = bytes[i + 1];     // G
                    data[pixelIdx + 2] = bytes[i + 2];     // B
                    data[pixelIdx + 3] = 255;               // A (opaque)
                }
            }
            
            // Draw to canvas
            try {
                ctx.putImageData(imageData, 0, 0);
                canvas.style.display = 'block';
                statusEl.textContent = 'Preview: ' + thumb.width + 'x' + thumb.height + ' RGB888 (updated ' + new Date().toLocaleTimeString() + ')';
            } catch (drawError) {
                console.error('Failed to draw RGB888 thumbnail to canvas:', drawError);
                statusEl.textContent = 'Error drawing thumbnail to canvas';
                canvas.style.display = 'none';
            }
        } else {
            statusEl.textContent = 'Unsupported thumbnail format: ' + thumb.format;
            canvas.style.display = 'none';
        }
    } catch (e) {
        console.error('Error updating thumbnail:', e);
        statusEl.textContent = 'Error displaying thumbnail: ' + e.message;
        canvas.style.display = 'none';
    }
}

async function showMediaItem(index) {
    // Send go command with parameter (1-based index)
    const payload = {
        command: 'go',
        parameter: String(index)  // Convert to string as firmware expects string parameter
    };
    
    if (await publishMessage(payload)) {
        showStatus('mediaMappingsStatus', `Show command sent for item ${index}. Display will update in 20-30 seconds...`, false);
        setBusyState(true, 'Displaying media item ' + index + '... Please wait for device to respond.');
    } else {
        showStatus('mediaMappingsStatus', 'Failed to send show command', true);
    }
}

function populateBackgroundImageDropdown() {
    const dropdown = document.getElementById('textBackgroundImage');
    if (!dropdown) {
        // Dropdown doesn't exist yet - will be created when needed
        return;
    }
    
    // Store current selection
    const currentValue = dropdown.value;
    
    // Clear existing options (except the default "(Use background color)" option)
    dropdown.innerHTML = '<option value="">(Use background color)</option>';
    
    // Add all image files
    if (allImageFiles && allImageFiles.length > 0) {
        for (let i = 0; i < allImageFiles.length; i++) {
            const option = document.createElement('option');
            option.value = allImageFiles[i];
            option.textContent = allImageFiles[i];
            dropdown.appendChild(option);
        }
        
        // Restore previous selection if it still exists
        if (currentValue) {
            dropdown.value = currentValue;
        }
    }
}

// Media mapping editor functions
function addMediaMapping() {
    editMediaMapping(-1);  // -1 means new mapping
}

function editMediaMapping(index) {
    // Create or show modal for editing
    let modal = document.getElementById('mediaMappingModal');
    if (!modal) {
        // Create modal
        modal = document.createElement('div');
        modal.id = 'mediaMappingModal';
        modal.style.cssText = 'display:none;position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.7);z-index:10000;align-items:center;justify-content:center;';
        modal.innerHTML = `
            <div style="background:#2a2a2a;padding:30px;border-radius:8px;max-width:600px;width:90%;max-height:90vh;overflow-y:auto;border:2px solid #555;">
                <h2 style="color:#4CAF50;margin-top:0;">Edit Media Mapping</h2>
                <form id="mediaMappingForm">
                    <label style="display:block;margin:10px 0 5px 0;font-weight:bold;color:#e0e0e0;">Image: *</label>
                    <select id="editMappingImage" style="width:100%;padding:8px;border:1px solid #444;border-radius:4px;box-sizing:border-box;background:#1a1a1a;color:#e0e0e0;"></select>
                    <label style="display:block;margin:10px 0 5px 0;font-weight:bold;color:#e0e0e0;">Audio:</label>
                    <select id="editMappingAudio" style="width:100%;padding:8px;border:1px solid #444;border-radius:4px;box-sizing:border-box;background:#1a1a1a;color:#e0e0e0;">
                        <option value="">(none)</option>
                    </select>
                    <label style="display:block;margin:10px 0 5px 0;font-weight:bold;color:#e0e0e0;">Foreground Color:</label>
                    <select id="editMappingForeground" style="width:100%;padding:8px;border:1px solid #444;border-radius:4px;box-sizing:border-box;background:#1a1a1a;color:#e0e0e0;">
                        <option value="">(default)</option>
                        <option value="black">Black</option>
                        <option value="white">White</option>
                        <option value="yellow">Yellow</option>
                        <option value="red">Red</option>
                        <option value="blue">Blue</option>
                        <option value="green">Green</option>
                    </select>
                    <label style="display:block;margin:10px 0 5px 0;font-weight:bold;color:#e0e0e0;">Outline Color:</label>
                    <select id="editMappingOutline" style="width:100%;padding:8px;border:1px solid #444;border-radius:4px;box-sizing:border-box;background:#1a1a1a;color:#e0e0e0;">
                        <option value="">(default)</option>
                        <option value="black">Black</option>
                        <option value="white">White</option>
                        <option value="yellow">Yellow</option>
                        <option value="red">Red</option>
                        <option value="blue">Blue</option>
                        <option value="green">Green</option>
                    </select>
                    <label style="display:block;margin:10px 0 5px 0;font-weight:bold;color:#e0e0e0;">Font:</label>
                    <select id="editMappingFont" style="width:100%;padding:8px;border:1px solid #444;border-radius:4px;box-sizing:border-box;background:#1a1a1a;color:#e0e0e0;">
                        <option value="">(default)</option>
                    </select>
                    <label style="display:block;margin:10px 0 5px 0;font-weight:bold;color:#e0e0e0;">Outline Thickness:</label>
                    <input type="number" id="editMappingThickness" min="1" max="10" value="3" style="width:100%;padding:8px;border:1px solid #444;border-radius:4px;box-sizing:border-box;background:#1a1a1a;color:#e0e0e0;">
                    <div style="margin-top:20px;display:flex;gap:10px;justify-content:flex-end;">
                        <button type="button" onclick="closeMediaMappingModal()" style="padding:10px 20px;background:#666;color:white;border:none;border-radius:4px;cursor:pointer;">Cancel</button>
                        <button type="button" onclick="saveMediaMapping()" style="padding:10px 20px;background:#4CAF50;color:white;border:none;border-radius:4px;cursor:pointer;">Save</button>
                    </div>
                    <input type="hidden" id="editMappingIndex" value="-1">
                </form>
            </div>
        `;
        document.body.appendChild(modal);
        modal.onclick = function(e) {
            if (e.target === modal) {
                closeMediaMappingModal();
            }
        };
    }
    
    // Populate dropdowns
    const imageSelect = document.getElementById('editMappingImage');
    imageSelect.innerHTML = '<option value="">Select image...</option>';
    if (allImageFiles && allImageFiles.length > 0) {
        allImageFiles.forEach(file => {
            const option = document.createElement('option');
            option.value = file;
            option.textContent = file;
            imageSelect.appendChild(option);
        });
    }
    
    const audioSelect = document.getElementById('editMappingAudio');
    audioSelect.innerHTML = '<option value="">(none)</option>';
    if (allAudioFiles && allAudioFiles.length > 0) {
        allAudioFiles.forEach(file => {
            const option = document.createElement('option');
            option.value = file;
            option.textContent = file;
            audioSelect.appendChild(option);
        });
    }
    
    const fontSelect = document.getElementById('editMappingFont');
    fontSelect.innerHTML = '<option value="">(default)</option>';
    if (allFonts && allFonts.length > 0) {
        allFonts.forEach(font => {
            const option = document.createElement('option');
            const fontName = font.name || font.filename || font.family || 'Unknown';
            option.value = fontName;
            const displayName = (font.family || font.name || font.filename) + (font.type === 'builtin' ? ' (Built-in)' : '');
            option.textContent = displayName;
            fontSelect.appendChild(option);
        });
    }
    
    // Populate form if editing existing mapping
    if (index >= 0 && index < currentMediaMappings.length) {
        const mapping = currentMediaMappings[index];
        imageSelect.value = mapping.image || '';
        audioSelect.value = mapping.audio || '';
        document.getElementById('editMappingForeground').value = mapping.foreground || '';
        document.getElementById('editMappingOutline').value = mapping.outline || '';
        document.getElementById('editMappingFont').value = mapping.font || '';
        document.getElementById('editMappingThickness').value = mapping.thickness || 3;
        document.getElementById('editMappingIndex').value = index;
    } else {
        // New mapping - clear form
        imageSelect.value = '';
        audioSelect.value = '';
        document.getElementById('editMappingForeground').value = '';
        document.getElementById('editMappingOutline').value = '';
        document.getElementById('editMappingFont').value = '';
        document.getElementById('editMappingThickness').value = 3;
        document.getElementById('editMappingIndex').value = -1;
    }
    
    modal.style.display = 'flex';
}

function closeMediaMappingModal() {
    const modal = document.getElementById('mediaMappingModal');
    if (modal) {
        modal.style.display = 'none';
    }
}

function deleteMediaMapping(index) {
    if (index < 0 || index >= currentMediaMappings.length) {
        showStatus('mediaMappingsStatus', 'Invalid mapping index', true);
        return;
    }
    
    if (!confirm(`Are you sure you want to delete mapping ${index + 1} (${currentMediaMappings[index].image || 'unknown image'})?`)) {
        return;
    }
    
    // Remove the mapping
    currentMediaMappings.splice(index, 1);
    
    // Save the updated mappings
    saveMediaMappingsToDevice();
}

async function saveMediaMapping() {
    const index = parseInt(document.getElementById('editMappingIndex').value);
    const image = document.getElementById('editMappingImage').value;
    
    if (!image || image.trim() === '') {
        alert('Image is required');
        return;
    }
    
    const mapping = {
        image: image,
        audio: document.getElementById('editMappingAudio').value || '',
        foreground: document.getElementById('editMappingForeground').value || '',
        outline: document.getElementById('editMappingOutline').value || '',
        font: document.getElementById('editMappingFont').value || '',
        thickness: parseInt(document.getElementById('editMappingThickness').value) || 3
    };
    
    if (index >= 0 && index < currentMediaMappings.length) {
        // Update existing mapping
        currentMediaMappings[index] = mapping;
    } else {
        // Add new mapping
        currentMediaMappings.push(mapping);
    }
    
    closeMediaMappingModal();
    
    // Save the updated mappings
    saveMediaMappingsToDevice();
}

async function saveMediaMappingsToDevice() {
    if (!currentMediaMappings || currentMediaMappings.length === 0) {
        showStatus('mediaMappingsStatus', 'Cannot save: no mappings to save', true);
        return;
    }
    
    // Build payload with mappings array
    const payload = {
        command: 'media_replace',
        mappings: currentMediaMappings.map(m => ({
            image: m.image || '',
            audio: m.audio || '',
            foreground: m.foreground || '',
            outline: m.outline || '',
            font: m.font || '',
            thickness: m.thickness || 3
        }))
    };
    
    showStatus('mediaMappingsStatus', 'Saving media mappings...', false);
    
    if (await publishMessage(payload)) {
        showStatus('mediaMappingsStatus', 'Media mappings update sent. Waiting for confirmation...', false);
        setBusyState(true, 'Updating media mappings... Please wait for device to respond.');
    } else {
        showStatus('mediaMappingsStatus', 'Failed to send media mappings update', true);
    }
}

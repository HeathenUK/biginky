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
    
    // Create table with optimized column widths
    let html = '<table style="width:100%;border-collapse:collapse;margin-top:10px;table-layout:fixed;">';
    html += '<thead><tr style="background:#333;color:#e0e0e0;">';
    html += '<th style="padding:8px;text-align:center;border:1px solid #555;width:5%;">#</th>';
    html += '<th style="padding:8px;text-align:center;border:1px solid #555;width:25%;">Thumbnail</th>';
    html += '<th style="padding:8px;text-align:left;border:1px solid #555;width:30%;">Image</th>';
    html += '<th style="padding:8px;text-align:left;border:1px solid #555;width:30%;">Audio</th>';
    html += '<th style="padding:8px;text-align:center;border:1px solid #555;width:10%;">Actions</th>';
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
        
        // Image column - allow text wrapping
        html += `<td style="padding:8px;border:1px solid #555;word-wrap:break-word;overflow-wrap:break-word;">${mapping.image || 'N/A'}</td>`;
        
        // Audio column - allow text wrapping
        html += `<td style="padding:8px;border:1px solid #555;word-wrap:break-word;overflow-wrap:break-word;">${mapping.audio || '<span style="color:#888;">(none)</span>'}</td>`;
        
        // Actions column - Show button
        html += '<td style="padding:8px;border:1px solid #555;text-align:center;vertical-align:middle;">';
        html += `<button onclick="showMediaItem(${displayIndex})" style="padding:6px 12px;font-size:14px;background:#4CAF50;color:white;border:none;border-radius:4px;cursor:pointer;">Show</button>`;
        html += '</td>';
        
        html += '</tr>';
    }
    
    html += '</tbody></table>';
    tableEl.innerHTML = html;
    
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
        if (thumb.format === 'jpeg') {
            // JPEG format: create data URL and load as image (much simpler!)
            const dataUrl = 'data:image/jpeg;base64,' + thumb.data;
            const img = new Image();
            img.onload = function() {
                ctx.clearRect(0, 0, canvas.width, canvas.height);
                ctx.drawImage(img, 0, 0, canvas.width, canvas.height);
                canvas.style.display = 'block';
                statusEl.textContent = 'Preview: ' + thumb.width + 'x' + thumb.height + ' JPEG (updated ' + new Date().toLocaleTimeString() + ')';
                console.log('JPEG thumbnail updated successfully.');
            };
            img.onerror = function() {
                console.error('Failed to load JPEG thumbnail');
                statusEl.textContent = 'Error loading JPEG preview';
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


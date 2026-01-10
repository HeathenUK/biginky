// Schedule management functions

const SCENE_TYPES = {
    media: { name: 'Media Mapping', paramType: 'mapping_number' },
    weather: { name: 'Happy Places Weather', paramType: null },
    image: { name: 'Show Image', paramType: 'image_dropdown' },
    weather_place: { name: 'Weather for Place', paramType: 'weather_place' },
    tfl_departures: { name: 'TfL Departures', paramType: 'tfl_station_full' },
    swim_conditions: { name: 'Swim Conditions (Fionphort)', paramType: null },
    feed: { name: 'RSS/Atom/JSON Feed', paramType: 'feed_config' }
};

// TfL line directions lookup (based on actual line routes)
const TFL_LINE_DIRECTIONS = {
    'bakerloo': ['Northbound', 'Southbound'],
    'central': ['Eastbound', 'Westbound'],
    'circle': ['Inner Rail', 'Outer Rail'],
    'district': ['Eastbound', 'Westbound'],
    'dlr': ['All'],
    'elizabeth': ['Eastbound', 'Westbound'],
    'hammersmith-city': ['Eastbound', 'Westbound'],
    'jubilee': ['Eastbound', 'Westbound'],
    'metropolitan': ['Northbound', 'Southbound'],
    'northern': ['Northbound', 'Southbound'],
    'piccadilly': ['Eastbound', 'Westbound'],
    'victoria': ['Northbound', 'Southbound'],
    'waterloo-city': ['Northbound', 'Southbound']
};

// TfL stations list
const TFL_STATIONS = [
    { id: '', name: '-- Select Station --' },
    { id: '940GZZLUEMB', name: 'Embankment' },
    { id: '940GZZLUHGT', name: 'Highgate' },
    { id: '940GZZLUACY', name: 'Archway' },
    { id: '940GZZLUEFN', name: 'East Finchley' },
    { id: '940GZZLUBST', name: 'Baker Street' },
    { id: '940GZZLUKSX', name: "King's Cross" },
    { id: '940GZZLUWLO', name: 'Waterloo' },
    { id: '940GZZLUVIC', name: 'Victoria' },
    { id: '940GZZLUPCC', name: 'Piccadilly Circus' },
    { id: '940GZZLUOXC', name: 'Oxford Circus' },
    { id: '940GZZLUGPK', name: 'Green Park' },
    { id: '940GZZLUBNK', name: 'Bank' },
    { id: '940GZZLULVT', name: 'Liverpool Street' },
    { id: '940GZZLUPAC', name: 'Paddington' }
];

// TfL lines list
const TFL_LINES = [
    { id: '', name: '-- All Lines --' },
    { id: 'bakerloo', name: 'Bakerloo' },
    { id: 'central', name: 'Central' },
    { id: 'circle', name: 'Circle' },
    { id: 'district', name: 'District' },
    { id: 'dlr', name: 'DLR' },
    { id: 'elizabeth', name: 'Elizabeth' },
    { id: 'hammersmith-city', name: 'Hammersmith & City' },
    { id: 'jubilee', name: 'Jubilee' },
    { id: 'metropolitan', name: 'Metropolitan' },
    { id: 'northern', name: 'Northern' },
    { id: 'piccadilly', name: 'Piccadilly' },
    { id: 'victoria', name: 'Victoria' },
    { id: 'waterloo-city', name: 'Waterloo & City' }
];

let currentSchedule = null;  // Store current schedule data

function getAccessibleMinutes(intervalMinutes) {
    if (!intervalMinutes || intervalMinutes <= 0 || 60 % intervalMinutes !== 0) {
        return [];
    }
    const minutes = [];
    for (let m = 0; m < 60; m += intervalMinutes) {
        minutes.push(m);
    }
    return minutes;
}

function createScheduleSlotRow(hour, slot = { minute: 0, scene: 'media', parameter: '' }) {
    const row = document.createElement('tr');
    row.className = 'schedule-slot-row';
    
    // Minute cell
    const minuteCell = document.createElement('td');
    minuteCell.style.padding = '4px';
    const minuteSelect = document.createElement('select');
    minuteSelect.className = 'slot-minute';
    minuteSelect.style.width = '80px';
    minuteSelect.style.background = '#1a1a1a';
    minuteSelect.style.color = '#e0e0e0';
    minuteSelect.style.border = '1px solid #444';
    minuteSelect.style.padding = '4px';
    
    // Get accessible minutes based on sleep interval (if available from status)
    // For now, default to 1 minute intervals if sleep interval not available
    const sleepInterval = 1; // TODO: Could get from status message if needed
    const accessibleMinutes = getAccessibleMinutes(sleepInterval);
    
    // Filter out already-used minutes in this hour
    const hourTable = document.querySelector(`#scheduleRows tr[data-hour="${hour}"] .schedule-slots-tbody`);
    const usedMinutes = [];
    if (hourTable) {
        hourTable.querySelectorAll('.schedule-slot-row').forEach(existingRow => {
            if (existingRow !== row) {
                const existingMinuteSelect = existingRow.querySelector('.slot-minute');
                if (existingMinuteSelect) {
                    const usedMin = parseInt(existingMinuteSelect.value);
                    if (!isNaN(usedMin) && usedMinutes.indexOf(usedMin) < 0) {
                        usedMinutes.push(usedMin);
                    }
                }
            }
        });
    }
    
    const availableMinutes = accessibleMinutes.filter(m => usedMinutes.indexOf(m) < 0);
    // Always include the slot's current minute, even if it's marked as "used" (for loading existing slots)
    if (slot.minute !== undefined && slot.minute !== null && accessibleMinutes.indexOf(slot.minute) >= 0 && availableMinutes.indexOf(slot.minute) < 0) {
        availableMinutes.push(slot.minute);
        availableMinutes.sort((a, b) => a - b);
    }
    availableMinutes.forEach(m => {
        const opt = document.createElement('option');
        opt.value = String(m);  // Explicitly convert to string (select values are always strings)
        opt.text = String(hour).padStart(2, '0') + ':' + String(m).padStart(2, '0');
        // Compare as numbers to ensure 0 === 0 works correctly (handle both string and number slot.minute)
        const slotMinuteNum = typeof slot.minute === 'number' ? slot.minute : parseInt(slot.minute, 10);
        opt.selected = (m === slotMinuteNum);
        minuteSelect.appendChild(opt);
    });
    minuteCell.appendChild(minuteSelect);
    
    // Scene cell
    const sceneCell = document.createElement('td');
    sceneCell.style.padding = '4px';
    const sceneSelect = document.createElement('select');
    sceneSelect.className = 'slot-scene';
    sceneSelect.style.width = '150px';
    sceneSelect.style.background = '#1a1a1a';
    sceneSelect.style.color = '#e0e0e0';
    sceneSelect.style.border = '1px solid #444';
    sceneSelect.style.padding = '4px';
    Object.keys(SCENE_TYPES).forEach(sceneKey => {
        const opt = document.createElement('option');
        opt.value = sceneKey;
        opt.text = SCENE_TYPES[sceneKey].name;
        opt.selected = (sceneKey === slot.scene);
        sceneSelect.appendChild(opt);
    });
    sceneCell.appendChild(sceneSelect);
    
    // Parameter cell
    const paramCell = document.createElement('td');
    paramCell.style.padding = '4px';
    paramCell.className = 'slot-parameter-cell';
    paramCell.style.minWidth = '400px';
    paramCell.style.width = 'auto';
    
    function updateParameterFields(selectedScene, paramValue = '') {
        paramCell.innerHTML = '';
        const paramType = SCENE_TYPES[selectedScene]?.paramType;
        if (!paramType) {
            return;
        }
        
        if (paramType === 'mapping_number') {
            const input = document.createElement('input');
            input.type = 'number';
            input.className = 'slot-parameter slot-parameter-mapping';
            input.min = '1';
            input.placeholder = 'Mapping # (optional)';
            input.value = paramValue || '';
            input.style.width = '120px';
            input.style.background = '#1a1a1a';
            input.style.color = '#e0e0e0';
            input.style.border = '1px solid #444';
            input.style.padding = '4px';
            paramCell.appendChild(input);
        } else if (paramType === 'image_dropdown') {
            const select = document.createElement('select');
            select.className = 'slot-parameter slot-parameter-image';
            select.style.width = '200px';
            select.style.background = '#1a1a1a';
            select.style.color = '#e0e0e0';
            select.style.border = '1px solid #444';
            select.style.padding = '4px';
            select.innerHTML = '<option value="">-- Select Image --</option>';
            // Use allImageFiles from config.js (global)
            if (typeof allImageFiles !== 'undefined' && Array.isArray(allImageFiles)) {
                allImageFiles.forEach(f => {
                    const opt = document.createElement('option');
                    opt.value = f;
                    opt.text = f;
                    opt.selected = (f === paramValue);
                    select.appendChild(opt);
                });
            }
            paramCell.appendChild(select);
        } else if (paramType === 'weather_place') {
            let parts = paramValue ? paramValue.split(',') : ['', '', ''];
            // Handle different parameter formats when loading:
            // - "placeName" (1 part) -> ['', '', 'placeName']
            // - "lat,lon" (2 parts) -> ['lat', 'lon', '']
            // - "lat,lon,placeName" (3 parts) -> ['lat', 'lon', 'placeName']
            if (parts.length === 1) {
                parts = ['', '', parts[0]];
            } else if (parts.length === 2) {
                parts = [parts[0], parts[1], ''];
            }
            
            const latInput = document.createElement('input');
            latInput.type = 'number';
            latInput.step = '0.0001';
            latInput.className = 'slot-parameter slot-parameter-lat';
            latInput.placeholder = 'Latitude (optional)';
            latInput.value = parts[0] || '';
            latInput.style.width = '100px';
            latInput.style.background = '#1a1a1a';
            latInput.style.color = '#e0e0e0';
            latInput.style.border = '1px solid #444';
            latInput.style.padding = '4px';
            latInput.style.marginRight = '4px';
            paramCell.appendChild(latInput);
            
            const lonInput = document.createElement('input');
            lonInput.type = 'number';
            lonInput.step = '0.0001';
            lonInput.className = 'slot-parameter slot-parameter-lon';
            lonInput.placeholder = 'Longitude (optional)';
            lonInput.value = parts[1] || '';
            lonInput.style.width = '100px';
            lonInput.style.background = '#1a1a1a';
            lonInput.style.color = '#e0e0e0';
            lonInput.style.border = '1px solid #444';
            lonInput.style.padding = '4px';
            lonInput.style.marginRight = '4px';
            paramCell.appendChild(lonInput);
            
            const nameInput = document.createElement('input');
            nameInput.type = 'text';
            nameInput.className = 'slot-parameter slot-parameter-name';
            nameInput.placeholder = 'Place Name (optional)';
            nameInput.value = parts[2] || '';
            nameInput.style.width = '150px';
            nameInput.style.background = '#1a1a1a';
            nameInput.style.color = '#e0e0e0';
            nameInput.style.border = '1px solid #444';
            nameInput.style.padding = '4px';
            paramCell.appendChild(nameInput);
        } else if (paramType === 'tfl_station_full') {
            // Parse existing JSON parameter if present
            let tflParams = { stationId: '', lineId: '', direction: '' };
            if (paramValue) {
                try {
                    const parsed = JSON.parse(paramValue);
                    tflParams = { ...tflParams, ...parsed };
                } catch (e) {
                    // If not JSON, treat as plain station ID (backwards compatibility)
                    tflParams.stationId = paramValue;
                }
            }
            
            // Station dropdown
            const stationSelect = document.createElement('select');
            stationSelect.className = 'slot-parameter slot-parameter-tfl-station';
            stationSelect.style.background = '#1a1a1a';
            stationSelect.style.color = '#e0e0e0';
            stationSelect.style.border = '1px solid #444';
            stationSelect.style.padding = '4px';
            stationSelect.style.width = '160px';
            stationSelect.style.marginRight = '4px';
            
            TFL_STATIONS.forEach(station => {
                const opt = document.createElement('option');
                opt.value = station.id;
                opt.text = station.name;
                opt.selected = (station.id === tflParams.stationId);
                stationSelect.appendChild(opt);
            });
            paramCell.appendChild(stationSelect);
            
            // Line dropdown
            const lineSelect = document.createElement('select');
            lineSelect.className = 'slot-parameter slot-parameter-tfl-line';
            lineSelect.style.background = '#1a1a1a';
            lineSelect.style.color = '#e0e0e0';
            lineSelect.style.border = '1px solid #444';
            lineSelect.style.padding = '4px';
            lineSelect.style.width = '140px';
            lineSelect.style.marginRight = '4px';
            
            TFL_LINES.forEach(line => {
                const opt = document.createElement('option');
                opt.value = line.id;
                opt.text = line.name;
                opt.selected = (line.id === tflParams.lineId);
                lineSelect.appendChild(opt);
            });
            paramCell.appendChild(lineSelect);
            
            // Direction dropdown
            const directionSelect = document.createElement('select');
            directionSelect.className = 'slot-parameter slot-parameter-tfl-direction';
            directionSelect.style.background = '#1a1a1a';
            directionSelect.style.color = '#e0e0e0';
            directionSelect.style.border = '1px solid #444';
            directionSelect.style.padding = '4px';
            directionSelect.style.width = '120px';
            
            // Populate direction based on selected line
            function updateDirectionOptions() {
                const selectedLine = lineSelect.value;
                directionSelect.innerHTML = '';
                
                const defaultOpt = document.createElement('option');
                defaultOpt.value = '';
                defaultOpt.text = '-- All --';
                defaultOpt.selected = !tflParams.direction;
                directionSelect.appendChild(defaultOpt);
                
                if (selectedLine && TFL_LINE_DIRECTIONS[selectedLine]) {
                    TFL_LINE_DIRECTIONS[selectedLine].forEach(dir => {
                        const opt = document.createElement('option');
                        opt.value = dir;
                        opt.text = dir;
                        opt.selected = (dir === tflParams.direction);
                        directionSelect.appendChild(opt);
                    });
                }
            }
            
            updateDirectionOptions();
            lineSelect.onchange = updateDirectionOptions;
            
            paramCell.appendChild(directionSelect);
        } else if (paramType === 'feed_config') {
            // Parse existing JSON parameter if present
            let feedParams = { url: '', count: 5, title: '', font: '' };
            if (paramValue) {
                try {
                    const parsed = JSON.parse(paramValue);
                    feedParams = { ...feedParams, ...parsed };
                } catch (e) {
                    // If not JSON, treat as plain URL (backwards compatibility)
                    feedParams.url = paramValue;
                }
            }
            
            // URL input
            const urlInput = document.createElement('input');
            urlInput.type = 'text';
            urlInput.className = 'slot-parameter slot-parameter-feed-url';
            urlInput.placeholder = 'Feed URL (RSS/Atom/JSON)';
            urlInput.value = feedParams.url;
            urlInput.style.background = '#1a1a1a';
            urlInput.style.color = '#e0e0e0';
            urlInput.style.border = '1px solid #444';
            urlInput.style.padding = '4px';
            urlInput.style.width = '250px';
            urlInput.style.marginRight = '4px';
            paramCell.appendChild(urlInput);
            
            // Count dropdown
            const countSelect = document.createElement('select');
            countSelect.className = 'slot-parameter slot-parameter-feed-count';
            countSelect.style.background = '#1a1a1a';
            countSelect.style.color = '#e0e0e0';
            countSelect.style.border = '1px solid #444';
            countSelect.style.padding = '4px';
            countSelect.style.width = '60px';
            countSelect.style.marginRight = '4px';
            
            for (let i = 1; i <= 10; i++) {
                const opt = document.createElement('option');
                opt.value = i;
                opt.text = i.toString();
                opt.selected = (i === feedParams.count);
                countSelect.appendChild(opt);
            }
            paramCell.appendChild(countSelect);
            
            // Title override input (optional)
            const titleInput = document.createElement('input');
            titleInput.type = 'text';
            titleInput.className = 'slot-parameter slot-parameter-feed-title';
            titleInput.placeholder = 'Title (optional)';
            titleInput.value = feedParams.title || '';
            titleInput.style.background = '#1a1a1a';
            titleInput.style.color = '#e0e0e0';
            titleInput.style.border = '1px solid #444';
            titleInput.style.padding = '4px';
            titleInput.style.width = '140px';
            titleInput.style.marginRight = '4px';
            paramCell.appendChild(titleInput);
            
            // Font dropdown (optional)
            const fontSelect = document.createElement('select');
            fontSelect.className = 'slot-parameter slot-parameter-feed-font';
            fontSelect.style.background = '#1a1a1a';
            fontSelect.style.color = '#e0e0e0';
            fontSelect.style.border = '1px solid #444';
            fontSelect.style.padding = '4px';
            fontSelect.style.width = '130px';
            
            // Add default option
            const defaultOpt = document.createElement('option');
            defaultOpt.value = '';
            defaultOpt.text = '(default font)';
            defaultOpt.selected = !feedParams.font;
            fontSelect.appendChild(defaultOpt);
            
            // Add fonts from allFonts global (populated from media mappings)
            if (typeof allFonts !== 'undefined' && Array.isArray(allFonts)) {
                allFonts.forEach(font => {
                    const opt = document.createElement('option');
                    opt.value = font.name || font.filename || '';
                    opt.text = (font.family || font.name || font.filename) + 
                              (font.type === 'builtin' ? ' (Built-in)' : '');
                    opt.selected = (opt.value === feedParams.font);
                    fontSelect.appendChild(opt);
                });
            }
            paramCell.appendChild(fontSelect);
        }
    }
    
    updateParameterFields(slot.scene, slot.parameter || '');
    sceneSelect.onchange = function() {
        // Preserve existing values when changing scene type
        const currentLatInput = paramCell.querySelector('.slot-parameter-lat');
        const currentLonInput = paramCell.querySelector('.slot-parameter-lon');
        const currentNameInput = paramCell.querySelector('.slot-parameter-name');
        let currentParam = '';
        if (currentLatInput || currentLonInput || currentNameInput) {
            const latVal = currentLatInput ? currentLatInput.value.trim() : '';
            const lonVal = currentLonInput ? currentLonInput.value.trim() : '';
            const nameVal = currentNameInput ? currentNameInput.value.trim() : '';
            const hasLatLon = latVal.length > 0 && lonVal.length > 0;
            const hasName = nameVal.length > 0;
            if (hasName) {
                if (hasLatLon) {
                    currentParam = latVal + ',' + lonVal + ',' + nameVal;
                } else {
                    currentParam = nameVal;
                }
            } else if (hasLatLon) {
                currentParam = latVal + ',' + lonVal;
            }
        }
        updateParameterFields(sceneSelect.value, currentParam);
    };
    
    // Action cell
    const actionCell = document.createElement('td');
    actionCell.style.padding = '4px';
    actionCell.style.textAlign = 'center';
    const delBtn = document.createElement('button');
    delBtn.className = 'delete';
    delBtn.textContent = '✕';
    delBtn.title = 'Delete slot';
    delBtn.style.background = '#f44336';
    delBtn.style.color = 'white';
    delBtn.style.border = 'none';
    delBtn.style.borderRadius = '4px';
    delBtn.style.padding = '4px 8px';
    delBtn.style.cursor = 'pointer';
    delBtn.onclick = function() {
        row.remove();
    };
    actionCell.appendChild(delBtn);
    
    row.appendChild(minuteCell);
    row.appendChild(sceneCell);
    row.appendChild(paramCell);
    row.appendChild(actionCell);
    return row;
}

function createScheduleHourRow(hour, enabled = true, slots = []) {
    const row = document.createElement('tr');
    row.dataset.hour = hour;
    
    // Hour cell
    const hourCell = document.createElement('td');
    hourCell.style.padding = '8px';
    hourCell.style.fontWeight = 'bold';
    hourCell.textContent = String(hour).padStart(2, '0') + ':00';
    row.appendChild(hourCell);
    
    // Enabled cell
    const enabledCell = document.createElement('td');
    enabledCell.style.padding = '8px';
    const enabledCheck = document.createElement('input');
    enabledCheck.type = 'checkbox';
    enabledCheck.className = 'hour-enabled-checkbox';
    enabledCheck.checked = enabled;
    enabledCheck.onchange = function() {
        const slotsContainer = row.querySelector('.schedule-slots-container');
        if (slotsContainer) {
            slotsContainer.style.display = enabledCheck.checked ? 'block' : 'none';
        }
    };
    enabledCell.appendChild(enabledCheck);
    row.appendChild(enabledCell);
    
    // Slots cell
    const slotsCell = document.createElement('td');
    slotsCell.style.padding = '8px';
    const slotsContainer = document.createElement('div');
    slotsContainer.className = 'schedule-slots-container';
    slotsContainer.style.display = enabled ? 'block' : 'none';
    
    const slotsTable = document.createElement('table');
    slotsTable.style.width = '100%';
    slotsTable.style.borderCollapse = 'collapse';
    slotsTable.style.marginTop = '5px';
    slotsTable.innerHTML = '<thead><tr style="background:#2a2a2a;"><th style="padding:4px;text-align:left;font-size:11px;">Minute</th><th style="padding:4px;text-align:left;font-size:11px;">Scene</th><th style="padding:4px;text-align:left;font-size:11px;">Parameter</th><th style="padding:4px;text-align:center;font-size:11px;"></th></tr></thead><tbody class="schedule-slots-tbody"></tbody>';
    
    const tbody = slotsTable.querySelector('.schedule-slots-tbody');
    slots.forEach(slot => {
        tbody.appendChild(createScheduleSlotRow(hour, slot));
    });
    
    slotsContainer.appendChild(slotsTable);
    
    const addSlotBtn = document.createElement('button');
    addSlotBtn.textContent = '+ Add Slot';
    addSlotBtn.style.marginTop = '5px';
    addSlotBtn.style.padding = '4px 8px';
    addSlotBtn.style.fontSize = '12px';
    addSlotBtn.style.background = '#2196F3';
    addSlotBtn.style.color = 'white';
    addSlotBtn.style.border = 'none';
    addSlotBtn.style.borderRadius = '4px';
    addSlotBtn.style.cursor = 'pointer';
    addSlotBtn.onclick = function() {
        const sleepInterval = 1; // TODO: Could get from status if needed
        const accessibleMinutes = getAccessibleMinutes(sleepInterval);
        const defaultMinute = accessibleMinutes.length > 0 ? accessibleMinutes[0] : 0;
        tbody.appendChild(createScheduleSlotRow(hour, { minute: defaultMinute, scene: 'media', parameter: '' }));
    };
    slotsContainer.appendChild(addSlotBtn);
    
    slotsCell.appendChild(slotsContainer);
    row.appendChild(slotsCell);
    
    // Action cell (empty for now)
    const actionCell = document.createElement('td');
    actionCell.style.padding = '8px';
    actionCell.style.textAlign = 'center';
    row.appendChild(actionCell);
    
    return row;
}

function updateScheduleTable(scheduleData) {
    const tbody = document.getElementById('scheduleRows');
    if (!tbody) {
        console.error('Schedule table body not found');
        return;
    }
    
    currentSchedule = scheduleData;
    tbody.innerHTML = '';
    
    if (scheduleData && Array.isArray(scheduleData) && scheduleData.length === 24) {
        scheduleData.forEach((hourData, idx) => {
            const slots = hourData.slots || [];
            slots.sort((a, b) => a.minute - b.minute);
            tbody.appendChild(createScheduleHourRow(idx, hourData.enabled !== false, slots));
        });
    } else {
        // Default: create 24 empty hours
        for (let h = 0; h < 24; h++) {
            tbody.appendChild(createScheduleHourRow(h, true, []));
        }
    }
    
    // Enable Save Schedule button when schedule is loaded
    const saveBtn = document.getElementById('saveScheduleBtn');
    if (saveBtn) {
        saveBtn.disabled = false;
    }
    
    console.log('Schedule table updated');
}

async function saveScheduleToDevice() {
    const schedule = [];
    const rows = document.querySelectorAll('#scheduleRows tr[data-hour]');
    
    rows.forEach(row => {
        const hour = parseInt(row.dataset.hour);
        const enabledCheck = row.querySelector('.hour-enabled-checkbox');
        const enabled = enabledCheck ? enabledCheck.checked : true;
        const slots = [];
        
        const slotRows = row.querySelectorAll('.schedule-slot-row');
        slotRows.forEach(slotRow => {
            const minuteSelect = slotRow.querySelector('.slot-minute');
            const sceneSelect = slotRow.querySelector('.slot-scene');
            if (minuteSelect && sceneSelect) {
                // Parse minute value - use Number() for better handling of "0"
                // parseInt() works fine, but be explicit about handling 0
                const minuteValue = minuteSelect.value;
                const minute = (minuteValue === '' || minuteValue === null || minuteValue === undefined) 
                    ? NaN 
                    : parseInt(minuteValue, 10);
                
                // Validate minute is a valid number (0-59, including 0!)
                if (isNaN(minute) || minute < 0 || minute >= 60) {
                    console.warn(`Invalid minute value for hour ${hour}: ${minuteValue} -> ${minute}, skipping slot`);
                    return; // Skip this slot if minute is invalid
                }
                
                const slot = {
                    minute: minute,  // Use validated minute (0 is valid!)
                    scene: sceneSelect.value
                };
                
                // DEBUG: Log every slot being created
                console.log(`[DEBUG] Hour ${hour}: Creating slot with minute=${minute} (raw value='${minuteValue}'), scene=${sceneSelect.value}`);
                
                const paramType = SCENE_TYPES[sceneSelect.value]?.paramType;
                if (paramType === 'mapping_number') {
                    const input = slotRow.querySelector('.slot-parameter-mapping');
                    if (input && input.value.trim().length > 0) {
                        const mappingNum = parseInt(input.value);
                        if (mappingNum >= 1) {
                            slot.parameter = String(mappingNum);
                        }
                    }
                    slots.push(slot);  // Always push mapping_number slots
                } else if (paramType === 'image_dropdown') {
                    const select = slotRow.querySelector('.slot-parameter-image');
                    if (select && select.value.trim().length > 0) {
                        slot.parameter = select.value.trim();
                    }
                    slots.push(slot);  // Always push image_dropdown slots
                } else if (paramType === 'weather_place') {
                    const latInput = slotRow.querySelector('.slot-parameter-lat');
                    const lonInput = slotRow.querySelector('.slot-parameter-lon');
                    const nameInput = slotRow.querySelector('.slot-parameter-name');
                    const latVal = latInput ? latInput.value.trim() : '';
                    const lonVal = lonInput ? lonInput.value.trim() : '';
                    const nameVal = nameInput ? nameInput.value.trim() : '';
                    const hasLatLon = latVal.length > 0 && lonVal.length > 0;
                    const hasName = nameVal.length > 0;
                    if (hasName) {
                        if (hasLatLon) {
                            slot.parameter = latVal + ',' + lonVal + ',' + nameVal;
                        } else {
                            slot.parameter = nameVal;
                        }
                    } else if (hasLatLon) {
                        slot.parameter = latVal + ',' + lonVal;
                    }
                    // Only push slot if at least one field is provided
                    if (hasName || hasLatLon) {
                        slots.push(slot);
                    }
                    // Skip empty weather_place slots - return early
                    return;
                } else if (paramType === 'tfl_station_full') {
                    const stationSelect = slotRow.querySelector('.slot-parameter-tfl-station');
                    const lineSelect = slotRow.querySelector('.slot-parameter-tfl-line');
                    const directionSelect = slotRow.querySelector('.slot-parameter-tfl-direction');
                    
                    const stationId = stationSelect ? stationSelect.value.trim() : '';
                    const lineId = lineSelect ? lineSelect.value.trim() : '';
                    const direction = directionSelect ? directionSelect.value.trim() : '';
                    
                    if (stationId.length > 0) {
                        // Store as JSON object
                        const tflParam = { stationId };
                        if (lineId) tflParam.lineId = lineId;
                        if (direction) tflParam.direction = direction;
                        slot.parameter = JSON.stringify(tflParam);
                        slots.push(slot);
                    }
                    // Skip empty tfl_station slots
                    return;
                } else if (paramType === 'feed_config') {
                    const urlInput = slotRow.querySelector('.slot-parameter-feed-url');
                    const countSelect = slotRow.querySelector('.slot-parameter-feed-count');
                    const titleInput = slotRow.querySelector('.slot-parameter-feed-title');
                    const fontSelect = slotRow.querySelector('.slot-parameter-feed-font');
                    
                    const url = urlInput ? urlInput.value.trim() : '';
                    const count = countSelect ? parseInt(countSelect.value) : 5;
                    const title = titleInput ? titleInput.value.trim() : '';
                    const font = fontSelect ? fontSelect.value : '';
                    
                    if (url.length > 0) {
                        // Store as JSON object
                        const feedParam = { url, count };
                        if (title) feedParam.title = title;
                        if (font) feedParam.font = font;
                        slot.parameter = JSON.stringify(feedParam);
                        slots.push(slot);
                    }
                    // Skip empty feed slots
                    return;
                } else {
                    slots.push(slot);  // Default: push all other slots (like weather)
                }
            }
        });
        
        slots.sort((a, b) => a.minute - b.minute);
        
        // DEBUG: Log all slots for this hour
        console.log(`[DEBUG] Hour ${hour}: Final slots array (${slots.length} slots):`, JSON.stringify(slots));
        
        schedule.push({ enabled: enabled, slots: slots });
    });
    
    // DEBUG: Log full schedule before sending
    console.log('[DEBUG] Full schedule being sent:', JSON.stringify(schedule, null, 2));
    
    // Send schedule_set command via MQTT
    if (typeof saveScheduleCommand === 'function') {
        await saveScheduleCommand(schedule);
    } else {
        console.error('saveScheduleCommand function not found');
        if (typeof showStatus === 'function') {
            showStatus('scheduleStatus', 'Error: Schedule command function not available', true);
        }
    }
}

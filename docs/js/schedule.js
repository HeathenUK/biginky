// Schedule management functions

const SCENE_TYPES = {
    media: { name: 'Media Mapping', paramType: 'mapping_number' },
    weather: { name: 'Happy Places Weather', paramType: null },
    image: { name: 'Show Image', paramType: 'image_dropdown' },
    weather_place: { name: 'Weather for Place', paramType: 'weather_place' }
};

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
    availableMinutes.forEach(m => {
        const opt = document.createElement('option');
        opt.value = m;
        opt.text = String(hour).padStart(2, '0') + ':' + String(m).padStart(2, '0');
        opt.selected = (m === slot.minute);
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
            const parts = paramValue ? paramValue.split(',') : ['', '', ''];
            const latInput = document.createElement('input');
            latInput.type = 'number';
            latInput.step = '0.0001';
            latInput.className = 'slot-parameter slot-parameter-lat';
            latInput.placeholder = 'Latitude';
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
            lonInput.placeholder = 'Longitude';
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
            nameInput.placeholder = 'Place Name';
            nameInput.value = parts[2] || '';
            nameInput.style.width = '150px';
            nameInput.style.background = '#1a1a1a';
            nameInput.style.color = '#e0e0e0';
            nameInput.style.border = '1px solid #444';
            nameInput.style.padding = '4px';
            paramCell.appendChild(nameInput);
        }
    }
    
    updateParameterFields(slot.scene, slot.parameter || '');
    sceneSelect.onchange = function() {
        updateParameterFields(sceneSelect.value);
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
                const slot = {
                    minute: parseInt(minuteSelect.value),
                    scene: sceneSelect.value
                };
                
                const paramType = SCENE_TYPES[sceneSelect.value]?.paramType;
                if (paramType === 'mapping_number') {
                    const input = slotRow.querySelector('.slot-parameter-mapping');
                    if (input && input.value.trim().length > 0) {
                        const mappingNum = parseInt(input.value);
                        if (mappingNum >= 1) {
                            slot.parameter = String(mappingNum);
                        }
                    }
                } else if (paramType === 'image_dropdown') {
                    const select = slotRow.querySelector('.slot-parameter-image');
                    if (select && select.value.trim().length > 0) {
                        slot.parameter = select.value.trim();
                    }
                } else if (paramType === 'weather_place') {
                    const latInput = slotRow.querySelector('.slot-parameter-lat');
                    const lonInput = slotRow.querySelector('.slot-parameter-lon');
                    const nameInput = slotRow.querySelector('.slot-parameter-name');
                    if (latInput && lonInput && nameInput && 
                        latInput.value.trim().length > 0 && 
                        lonInput.value.trim().length > 0 && 
                        nameInput.value.trim().length > 0) {
                        slot.parameter = latInput.value.trim() + ',' + lonInput.value.trim() + ',' + nameInput.value.trim();
                    }
                }
                
                slots.push(slot);
            }
        });
        
        slots.sort((a, b) => a.minute - b.minute);
        schedule.push({ enabled: enabled, slots: slots });
    });
    
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

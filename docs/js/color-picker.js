// Unified Color Picker Component
// This component can be used anywhere color selection is needed in the UI
// TEST MARKER: This file was created from scratch on 2024-12-30

// Color definitions - supports both numeric (canvas) and string (text) color values
const colorDefinitions = {
    // Numeric colors (for canvas drawing) - matching firmware palette from EL133UF1_Color.cpp
    numeric: {
        '0': { hex: '#0A0A0A', name: 'Black' },      // (10, 10, 10)
        '1': { hex: '#F5F5EB', name: 'White' },      // (245, 245, 235)
        '2': { hex: '#F5D232', name: 'Yellow' },      // (245, 210, 50)
        '3': { hex: '#BE3C37', name: 'Red' },         // (190, 60, 55)
        '5': { hex: '#2D4BA0', name: 'Blue' },        // (45, 75, 160)
        '6': { hex: '#378C55', name: 'Green' },       // (55, 140, 85)
        'transparent': { hex: 'transparent', name: 'Transparent', gradient: 'linear-gradient(45deg, #ccc 25%, transparent 25%), linear-gradient(-45deg, #ccc 25%, transparent 25%), linear-gradient(45deg, transparent 75%, #ccc 75%), linear-gradient(-45deg, transparent 75%, #ccc 75%)' }
    },
    // String colors (for text display) - matching firmware palette from EL133UF1_Color.cpp
    string: {
        'black': { hex: '#0A0A0A', name: 'Black' },      // (10, 10, 10)
        'white': { hex: '#F5F5EB', name: 'White' },      // (245, 245, 235)
        'yellow': { hex: '#F5D232', name: 'Yellow' },     // (245, 210, 50)
        'red': { hex: '#BE3C37', name: 'Red' },          // (190, 60, 55)
        'blue': { hex: '#2D4BA0', name: 'Blue' },        // (45, 75, 160)
        'green': { hex: '#378C55', name: 'Green' },      // (55, 140, 85)
        'multi': { hex: 'linear-gradient(90deg,#0A0A0A 0%,#F5D232 16.66%,#BE3C37 33.33%,#2D4BA0 50%,#378C55 66.66%,#F5F5EB 83.33%,#0A0A0A 100%)', name: 'Multi' }
    }
};

// Global click handler to close pickers when clicking outside (registered once)
// Store button-palette mappings when pickers are created
const colorPickerMappings = new Map();

/**
 * Create a unified color picker
 * @param {Object} config Configuration object
 * @param {string} config.buttonId - ID of the button that opens the picker
 * @param {string} config.paletteId - ID of the palette container
 * @param {string} config.hiddenInputId - ID of the hidden input that stores the value
 * @param {string} config.indicatorId - ID of the color indicator element (optional)
 * @param {string} config.labelId - ID of the color label element (optional)
 * @param {string} config.colorType - 'numeric' or 'string' (default: 'numeric')
 * @param {Array<string>} config.colors - Array of color values to include (default: all)
 * @param {string} config.defaultValue - Default color value
 * @param {Function} config.onChange - Callback function(colorValue) called when color changes
 * @param {Array<string>} config.closeOtherPickers - Array of palette IDs to close when this opens
 */
function createColorPicker(config) {
    const {
        buttonId,
        paletteId,
        hiddenInputId,
        indicatorId,
        labelId,
        colorType = 'numeric',
        colors = null,
        defaultValue,
        onChange = null,
        closeOtherPickers = []
    } = config;

    const button = document.getElementById(buttonId);
    const palette = document.getElementById(paletteId);
    const hiddenInput = document.getElementById(hiddenInputId);
    const indicator = indicatorId ? document.getElementById(indicatorId) : null;
    const label = labelId ? document.getElementById(labelId) : null;

    if (!button || !palette || !hiddenInput) {
        console.warn(`Color picker initialization failed: missing elements for ${buttonId}`);
        return;
    }

    // Register this button-palette mapping for the global click handler
    if (!window.colorPickerClickHandlerRegistered) {
        registerColorPickerClickHandler();
    }
    colorPickerMappings.set(palette, button);

    const colorDefs = colorDefinitions[colorType] || colorDefinitions.numeric;
    const availableColors = colors || Object.keys(colorDefs);

    // Set up button click handler
    button.addEventListener('click', (e) => {
        e.stopPropagation();
        e.preventDefault(); // TEST: Added to ensure form submission doesn't interfere
        const isVisible = palette.style.display !== 'none';
        
        // Close other pickers
        closeOtherPickers.forEach(otherId => {
            const otherPalette = document.getElementById(otherId);
            if (otherPalette) otherPalette.style.display = 'none';
        });
        
        // Toggle this palette (use 'flex' to match CSS rule)
        if (!isVisible) {
            // Get button position relative to viewport
            const buttonRect = button.getBoundingClientRect();
            
            // Use fixed positioning to avoid overflow clipping by parent containers
            palette.style.position = 'fixed';
            palette.style.zIndex = '10000';
            palette.style.display = 'flex';
            
            // Temporarily show to measure
            palette.style.visibility = 'hidden';
            const paletteRect = palette.getBoundingClientRect();
            const paletteHeight = paletteRect.height || 120;
            palette.style.visibility = '';
            
            // Check if there's enough space below, if not position above
            const spaceBelow = window.innerHeight - buttonRect.bottom;
            const spaceAbove = buttonRect.top;
            
            if (spaceBelow < paletteHeight && spaceAbove > spaceBelow) {
                // Position above the button
                palette.style.top = (buttonRect.top - paletteHeight - 4) + 'px';
            } else {
                // Position below the button (default)
                palette.style.top = (buttonRect.bottom + 4) + 'px';
            }
            palette.style.left = buttonRect.left + 'px';
        } else {
            palette.style.display = 'none';
            // Reset position for next time (will be set to fixed when shown)
            palette.style.position = '';
        }
    });

    // Set up palette button click handlers
    palette.querySelectorAll('.palette-color-btn').forEach(btn => {
        btn.addEventListener('click', (e) => {
            e.stopPropagation();
            const colorValue = btn.dataset.color;
            if (colorValue) {
                setColorValue(colorValue);
                if (onChange) onChange(colorValue);
            }
        });
    });

    // Set the color value and update UI
    function setColorValue(colorValue) {
        hiddenInput.value = colorValue;
        
        const colorDef = colorDefs[colorValue];
        if (colorDef) {
            // Update indicator
            if (indicator) {
                if (colorDef.gradient) {
                    indicator.style.background = colorDef.gradient;
                    indicator.style.backgroundSize = '8px 8px';
                } else {
                    indicator.style.background = colorDef.hex;
                    indicator.style.backgroundSize = 'auto';
                }
            }
            
            // Update label
            if (label) {
                label.textContent = colorDef.name;
            }
            
            // Update active button in palette
            palette.querySelectorAll('.palette-color-btn').forEach(btn => {
                btn.classList.remove('active');
                if (btn.dataset.color === colorValue) {
                    btn.classList.add('active');
                }
            });
        }
        
        // Close palette
        palette.style.display = 'none';
    }

    // Set initial value
    if (defaultValue !== undefined) {
        setColorValue(defaultValue);
    }

    // Return the setter function
    return setColorValue;
}

function registerColorPickerClickHandler() {
    if (window.colorPickerClickHandlerRegistered) return;
    
    document.addEventListener('click', (e) => {
        // Close all palettes if click is outside their associated buttons
        colorPickerMappings.forEach((button, palette) => {
            if (!button.contains(e.target) && !palette.contains(e.target)) {
                palette.style.display = 'none';
            }
        });
    });
    
    window.colorPickerClickHandlerRegistered = true;
}

// Initialize on DOM ready
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', registerColorPickerClickHandler);
} else {
    registerColorPickerClickHandler();
}


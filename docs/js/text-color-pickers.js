// Text color picker handlers for Text Display section

// Color map for text colors (string names to display)
const textColorMap = {
    'black': { hex: '#000000', name: 'Black' },
    'yellow': { hex: '#FFFF00', name: 'Yellow' },
    'red': { hex: '#FF0000', name: 'Red' },
    'blue': { hex: '#0000FF', name: 'Blue' },
    'green': { hex: '#00FF00', name: 'Green' },
    'white': { hex: '#FFFFFF', name: 'White' },
    'multi': { hex: 'linear-gradient(90deg,#000000 0%,#FFFF00 16.66%,#FF0000 33.33%,#0000FF 50%,#00FF00 66.66%,#FFFFFF 83.33%,#000000 100%)', name: 'Multi' }
};

// Initialize text color pickers
function initTextColorPickers() {
    // Get all elements first (in outer scope so they're accessible everywhere)
    const textColorBtn = document.getElementById('textColorBtn');
    const textColorPalette = document.getElementById('textColorPalette');
    const textColorHidden = document.getElementById('textColor');
    const textBackgroundColorBtn = document.getElementById('textBackgroundColorBtn');
    const textBackgroundColorPalette = document.getElementById('textBackgroundColorPalette');
    const textBackgroundColorHidden = document.getElementById('textBackgroundColor');
    const textOutlineColorBtn = document.getElementById('textOutlineColorBtn');
    const textOutlineColorPalette = document.getElementById('textOutlineColorPalette');
    const textOutlineColorHidden = document.getElementById('textOutlineColor');
    
    // Text Color picker
    if (textColorBtn && textColorPalette && textColorHidden) {
        textColorBtn.addEventListener('click', (e) => {
            e.stopPropagation();
            e.preventDefault();
            const isVisible = textColorPalette.style.display !== 'none';
            // Close other palettes if open
            if (textBackgroundColorPalette) textBackgroundColorPalette.style.display = 'none';
            if (textOutlineColorPalette) textOutlineColorPalette.style.display = 'none';
            // Toggle text color palette
            textColorPalette.style.display = isVisible ? 'none' : 'block';
        });
        
        document.querySelectorAll('#textColorPalette .palette-color-btn').forEach(btn => {
            btn.addEventListener('click', (e) => {
                e.stopPropagation();
                e.preventDefault();
                const colorValue = e.target.dataset.color || e.target.closest('.palette-color-btn')?.dataset.color;
                if (colorValue) {
                    setTextColor(colorValue);
                }
            });
        });
        
        // Initialize with default value
        setTextColor('black');
    }
    
    // Background Color picker
    if (textBackgroundColorBtn && textBackgroundColorPalette && textBackgroundColorHidden) {
        textBackgroundColorBtn.addEventListener('click', (e) => {
            e.stopPropagation();
            e.preventDefault();
            const isVisible = textBackgroundColorPalette.style.display !== 'none';
            // Close other palettes if open
            if (textColorPalette) textColorPalette.style.display = 'none';
            if (textOutlineColorPalette) textOutlineColorPalette.style.display = 'none';
            // Toggle background color palette
            textBackgroundColorPalette.style.display = isVisible ? 'none' : 'block';
        });
        
        document.querySelectorAll('#textBackgroundColorPalette .palette-color-btn').forEach(btn => {
            btn.addEventListener('click', (e) => {
                e.stopPropagation();
                e.preventDefault();
                const colorValue = e.target.dataset.color || e.target.closest('.palette-color-btn')?.dataset.color;
                if (colorValue) {
                    setTextBackgroundColor(colorValue);
                }
            });
        });
        
        // Initialize with default value
        setTextBackgroundColor('white');
    }
    
    // Outline Color picker
    if (textOutlineColorBtn && textOutlineColorPalette && textOutlineColorHidden) {
        textOutlineColorBtn.addEventListener('click', (e) => {
            e.stopPropagation();
            e.preventDefault();
            const isVisible = textOutlineColorPalette.style.display !== 'none';
            // Close other palettes if open
            if (textColorPalette) textColorPalette.style.display = 'none';
            if (textBackgroundColorPalette) textBackgroundColorPalette.style.display = 'none';
            // Toggle outline color palette
            textOutlineColorPalette.style.display = isVisible ? 'none' : 'block';
        });
        
        document.querySelectorAll('#textOutlineColorPalette .palette-color-btn').forEach(btn => {
            btn.addEventListener('click', (e) => {
                e.stopPropagation();
                e.preventDefault();
                const colorValue = e.target.dataset.color || e.target.closest('.palette-color-btn')?.dataset.color;
                if (colorValue) {
                    setTextOutlineColor(colorValue);
                }
            });
        });
        
        // Initialize with default value
        setTextOutlineColor('black');
    }
    
    // Close palettes when clicking outside
    document.addEventListener('click', (e) => {
        if (textColorPalette && textColorBtn && !textColorBtn.contains(e.target) && !textColorPalette.contains(e.target)) {
            textColorPalette.style.display = 'none';
        }
        if (textBackgroundColorPalette && textBackgroundColorBtn && !textBackgroundColorBtn.contains(e.target) && !textBackgroundColorPalette.contains(e.target)) {
            textBackgroundColorPalette.style.display = 'none';
        }
        if (textOutlineColorPalette && textOutlineColorBtn && !textOutlineColorBtn.contains(e.target) && !textOutlineColorPalette.contains(e.target)) {
            textOutlineColorPalette.style.display = 'none';
        }
    });
}

function setTextColor(colorValue) {
    const hiddenInput = document.getElementById('textColor');
    const indicator = document.getElementById('textColorIndicator');
    const label = document.getElementById('textColorLabel');
    
    if (hiddenInput) hiddenInput.value = colorValue;
    
    const colorInfo = textColorMap[colorValue] || textColorMap['black'];
    if (indicator) {
        indicator.style.background = colorInfo.hex;
        indicator.style.backgroundSize = 'auto';
    }
    if (label) label.textContent = colorInfo.name;
    
    // Update active button in palette
    document.querySelectorAll('#textColorPalette .palette-color-btn').forEach(btn => {
        btn.classList.remove('active');
        if (btn.dataset.color === colorValue) {
            btn.classList.add('active');
        }
    });
    
    // Close palette
    const palette = document.getElementById('textColorPalette');
    if (palette) palette.style.display = 'none';
}

function setTextBackgroundColor(colorValue) {
    const hiddenInput = document.getElementById('textBackgroundColor');
    const indicator = document.getElementById('textBackgroundColorIndicator');
    const label = document.getElementById('textBackgroundColorLabel');
    
    if (hiddenInput) hiddenInput.value = colorValue;
    
    const colorInfo = textColorMap[colorValue] || textColorMap['white'];
    if (indicator) indicator.style.background = colorInfo.hex;
    if (label) label.textContent = colorInfo.name;
    
    // Update active button in palette
    document.querySelectorAll('#textBackgroundColorPalette .palette-color-btn').forEach(btn => {
        btn.classList.remove('active');
        if (btn.dataset.color === colorValue) {
            btn.classList.add('active');
        }
    });
    
    // Close palette
    const palette = document.getElementById('textBackgroundColorPalette');
    if (palette) palette.style.display = 'none';
}

function setTextOutlineColor(colorValue) {
    const hiddenInput = document.getElementById('textOutlineColor');
    const indicator = document.getElementById('textOutlineColorIndicator');
    const label = document.getElementById('textOutlineColorLabel');
    
    if (hiddenInput) hiddenInput.value = colorValue;
    
    const colorInfo = textColorMap[colorValue] || textColorMap['black'];
    if (indicator) indicator.style.background = colorInfo.hex;
    if (label) label.textContent = colorInfo.name;
    
    // Update active button in palette
    document.querySelectorAll('#textOutlineColorPalette .palette-color-btn').forEach(btn => {
        btn.classList.remove('active');
        if (btn.dataset.color === colorValue) {
            btn.classList.add('active');
        }
    });
    
    // Close palette
    const palette = document.getElementById('textOutlineColorPalette');
    if (palette) palette.style.display = 'none';
}


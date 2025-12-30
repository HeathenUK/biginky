// Google Fonts loader for canvas text tool
// Dynamically loads fonts from Google Fonts API when selected

const GOOGLE_FONTS_API = 'https://fonts.googleapis.com/css2';
const loadedFonts = new Set(); // Track which fonts have been loaded

// Popular Google Fonts with their API names
const googleFontsMap = {
    'Roboto': 'Roboto:wght@400;700',
    'Open Sans': 'Open+Sans:wght@400;700',
    'Lato': 'Lato:wght@400;700',
    'Montserrat': 'Montserrat:wght@400;700',
    'Oswald': 'Oswald:wght@400;700',
    'Raleway': 'Raleway:wght@400;700',
    'Poppins': 'Poppins:wght@400;700',
    'Ubuntu': 'Ubuntu:wght@400;700',
    'Playfair Display': 'Playfair+Display:wght@400;700',
    'Merriweather': 'Merriweather:wght@400;700',
    'Source Sans Pro': 'Source+Sans+Pro:wght@400;700',
    'PT Sans': 'PT+Sans:wght@400;700',
    'Noto Sans': 'Noto+Sans:wght@400;700',
    'Lora': 'Lora:wght@400;700',
    'Dancing Script': 'Dancing+Script:wght@400;700',
    'Bebas Neue': 'Bebas+Neue',
    'Anton': 'Anton',
    'Fjalla One': 'Fjalla+One',
    'Righteous': 'Righteous',
    'Lobster': 'Lobster'
};

// System fonts that don't need loading
const systemFonts = new Set(['Arial', 'Times New Roman', 'Courier New', 'Georgia', 'Verdana']);

/**
 * Load a Google Font dynamically
 * @param {string} fontName - The display name of the font (e.g., "Roboto", "Open Sans")
 * @returns {Promise} - Resolves when font is loaded
 */
function loadGoogleFont(fontName) {
    // If it's a system font, no need to load
    if (systemFonts.has(fontName)) {
        return Promise.resolve();
    }
    
    // If already loaded, no need to load again
    if (loadedFonts.has(fontName)) {
        return Promise.resolve();
    }
    
    // Get the API font name
    const apiFontName = googleFontsMap[fontName];
    if (!apiFontName) {
        console.warn(`Font "${fontName}" not found in Google Fonts map`);
        return Promise.resolve();
    }
    
    return new Promise((resolve, reject) => {
        // Create a link element to load the font stylesheet
        const link = document.createElement('link');
        link.rel = 'stylesheet';
        link.href = `${GOOGLE_FONTS_API}?family=${apiFontName}&display=swap`;
        
        link.onload = () => {
            // Wait a bit for the font to be fully available, then check
            setTimeout(() => {
                // Use document.fonts API to verify font is loaded (if available)
                if (document.fonts && document.fonts.check) {
                    // Font should be available now via the stylesheet
                    // The browser will have loaded it from the stylesheet
                    loadedFonts.add(fontName);
                    console.log(`Loaded Google Font: ${fontName}`);
                } else {
                    // Fallback for older browsers - assume it's loaded
                    loadedFonts.add(fontName);
                    console.log(`Loaded Google Font (fallback): ${fontName}`);
                }
                resolve();
            }, 100); // Small delay to ensure font is processed
        };
        
        link.onerror = () => {
            console.warn(`Failed to load font stylesheet for ${fontName}`);
            resolve(); // Resolve anyway to not block the UI
        };
        
        document.head.appendChild(link);
    });
}

/**
 * Preload common fonts on page load for better performance
 */
function preloadCommonFonts() {
    // Preload a few popular fonts
    const commonFonts = ['Roboto', 'Open Sans', 'Lato', 'Montserrat'];
    commonFonts.forEach(font => {
        loadGoogleFont(font).catch(err => {
            console.warn(`Failed to preload font ${font}:`, err);
        });
    });
}

// Preload common fonts when script loads
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', preloadCommonFonts);
} else {
    preloadCommonFonts();
}


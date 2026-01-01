/**
 * @file EL133UF1_TTF.cpp
 * @brief TrueType font rendering implementation for EL133UF1 display
 * 
 * Uses stb_truetype for TTF parsing and glyph rasterization.
 * Supports RP2350 and ESP32 with PSRAM.
 */

#include "EL133UF1_TTF.h"
#include "platform_hal.h"

// Define implementation before including stb_truetype
#define STB_TRUETYPE_IMPLEMENTATION

// Use PSRAM-aware allocation via platform HAL
#define STBTT_malloc(x,u)  ((void)(u), hal_psram_malloc(x))
#define STBTT_free(x,u)    ((void)(u), hal_psram_free(x))

#include "stb_truetype.h"

// ============================================================================
// Constructor / Destructor
// ============================================================================

EL133UF1_TTF::EL133UF1_TTF() : 
    _display(nullptr),
    _fontData(nullptr),
    _fontDataSize(0),
    _fontLoaded(false), 
    _fontInfo(nullptr),
    _cachedGlyphCount(0),
    _cacheScale(0),
    _cacheFontSize(0),
    _cacheEnabled(false)
{
    memset(_glyphCache, 0, sizeof(_glyphCache));
}

EL133UF1_TTF::~EL133UF1_TTF() {
    clearGlyphCache();
    if (_fontInfo) {
        free(_fontInfo);
        _fontInfo = nullptr;
    }
}

// ============================================================================
// Glyph Caching for Fast Rendering
// ============================================================================

void EL133UF1_TTF::clearGlyphCache() {
    for (int i = 0; i < _cachedGlyphCount; i++) {
        if (_glyphCache[i].bitmap) {
            free(_glyphCache[i].bitmap);
            _glyphCache[i].bitmap = nullptr;
        }
    }
    _cachedGlyphCount = 0;
    _cacheEnabled = false;
}

bool EL133UF1_TTF::enableGlyphCache(float fontSize, const char* characters) {
    if (!_fontLoaded || _fontInfo == nullptr) return false;
    
    // Clear any existing cache
    clearGlyphCache();
    
    stbtt_fontinfo* info = (stbtt_fontinfo*)_fontInfo;
    float scale = stbtt_ScaleForPixelHeight(info, fontSize);
    _cacheScale = scale;
    _cacheFontSize = fontSize;
    
    Serial.printf("TTF: Caching glyphs for size %.0f: ", fontSize);
    
    const char* p = characters;
    while (*p && _cachedGlyphCount < MAX_CACHED_GLYPHS) {
        // Decode UTF-8
        int codepoint = (uint8_t)*p++;
        if (codepoint >= 0xC0 && codepoint < 0xE0 && *p) {
            codepoint = ((codepoint & 0x1F) << 6) | (*p++ & 0x3F);
        }
        
        // Check if already cached
        bool found = false;
        for (int i = 0; i < _cachedGlyphCount; i++) {
            if (_glyphCache[i].codepoint == codepoint) {
                found = true;
                break;
            }
        }
        if (found) continue;
        
        // Get glyph metrics
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(info, codepoint, scale, scale, &x0, &y0, &x1, &y1);
        
        int width = x1 - x0;
        int height = y1 - y0;
        
        if (width <= 0 || height <= 0) {
            // Space or non-printable - still cache metrics
            int advance, lsb;
            stbtt_GetCodepointHMetrics(info, codepoint, &advance, &lsb);
            
            CachedGlyph* g = &_glyphCache[_cachedGlyphCount++];
            g->codepoint = codepoint;
            g->width = 0;
            g->height = 0;
            g->xOffset = 0;
            g->yOffset = 0;
            g->advance = (int16_t)(advance * scale);
            g->bitmap = nullptr;
            continue;
        }
        
        // Allocate bitmap
        size_t bitmapSize = width * height;
        uint8_t* bitmap = (uint8_t*)malloc(bitmapSize);
        if (!bitmap) {
            Serial.println("\nTTF: Cache allocation failed, cleaning up");
            // Clean up any glyphs we already cached in this call
            clearGlyphCache();
            return false;
        }
        
        // Render glyph
        stbtt_MakeCodepointBitmap(info, bitmap, width, height, width, scale, scale, codepoint);
        
        // Get advance
        int advance, lsb;
        stbtt_GetCodepointHMetrics(info, codepoint, &advance, &lsb);
        
        // Store in cache
        CachedGlyph* g = &_glyphCache[_cachedGlyphCount++];
        g->codepoint = codepoint;
        g->width = width;
        g->height = height;
        g->xOffset = x0;
        g->yOffset = y0;
        g->advance = (int16_t)(advance * scale);
        g->bitmap = bitmap;
        
        Serial.printf("%c", codepoint < 128 ? (char)codepoint : '?');
    }
    
    _cacheEnabled = true;
    Serial.printf(" (%d glyphs cached)\n", _cachedGlyphCount);
    return true;
}

EL133UF1_TTF::CachedGlyph* EL133UF1_TTF::findCachedGlyph(int codepoint) {
    if (!_cacheEnabled) return nullptr;
    for (int i = 0; i < _cachedGlyphCount; i++) {
        if (_glyphCache[i].codepoint == codepoint) {
            return &_glyphCache[i];
        }
    }
    return nullptr;
}

void EL133UF1_TTF::renderCachedGlyph(CachedGlyph* glyph, int16_t x, int16_t baseline, uint8_t color) {
    if (!_display || !glyph || !glyph->bitmap) return;
    
    int16_t screenX = x + glyph->xOffset;
    int16_t screenY = baseline + glyph->yOffset;
    
    // Fast path: direct pixel writes with threshold
    for (int py = 0; py < glyph->height; py++) {
        int16_t drawY = screenY + py;
        if (drawY < 0 || drawY >= _display->height()) continue;
        
        const uint8_t* row = glyph->bitmap + py * glyph->width;
        for (int px = 0; px < glyph->width; px++) {
            int16_t drawX = screenX + px;
            if (drawX < 0 || drawX >= _display->width()) continue;
            
            if (row[px] > 127) {
                _display->setPixel(drawX, drawY, color);
            }
        }
    }
}

// ============================================================================
// Initialization
// ============================================================================

bool EL133UF1_TTF::begin(EL133UF1* display) {
    if (display == nullptr) {
        return false;
    }
    _display = display;
    return true;
}

// ============================================================================
// Font Loading
// ============================================================================

bool EL133UF1_TTF::loadFont(const uint8_t* fontData, size_t fontDataSize) {
    if (fontData == nullptr || fontDataSize == 0) {
        Serial.println("TTF: Invalid font data");
        return false;
    }
    
    _fontData = fontData;
    _fontDataSize = fontDataSize;
    
    // Free existing font info if any
    if (_fontInfo) {
        free(_fontInfo);
        _fontInfo = nullptr;
        _fontLoaded = false;
    }
    
    // Allocate font info structure
    _fontInfo = malloc(sizeof(stbtt_fontinfo));
    if (_fontInfo == nullptr) {
        Serial.println("TTF: Failed to allocate font info");
        return false;
    }
    
    // Initialize the font
    stbtt_fontinfo* info = (stbtt_fontinfo*)_fontInfo;
    int offset = stbtt_GetFontOffsetForIndex(fontData, 0);
    if (offset < 0) {
        Serial.println("TTF: Invalid font offset");
        free(_fontInfo);
        _fontInfo = nullptr;
        return false;
    }
    
    if (!stbtt_InitFont(info, fontData, offset)) {
        Serial.println("TTF: Failed to initialize font");
        free(_fontInfo);
        _fontInfo = nullptr;
        return false;
    }
    
    _fontLoaded = true;
    Serial.println("TTF: Font loaded successfully");
    return true;
}

bool EL133UF1_TTF::getFontName(char* nameBuffer, size_t bufferSize) {
    if (!_fontLoaded || _fontInfo == nullptr || nameBuffer == nullptr || bufferSize == 0) {
        return false;
    }
    
    stbtt_fontinfo* info = (stbtt_fontinfo*)_fontInfo;
    
    // Try to get font family name (nameID 1)
    // Try Microsoft Unicode first (most common)
    int nameLength = 0;
    const char* fontName = stbtt_GetFontNameString(info, &nameLength, 
                                                    3,  // STBTT_PLATFORM_ID_MICROSOFT
                                                    1,  // STBTT_MS_EID_UNICODE_BMP
                                                    0x0409,  // STBTT_MS_LANG_ENGLISH
                                                    1);  // nameID 1 = Font Family
    
    // If not found, try Unicode platform
    if (fontName == nullptr || nameLength == 0) {
        fontName = stbtt_GetFontNameString(info, &nameLength,
                                           0,  // STBTT_PLATFORM_ID_UNICODE
                                           3,  // STBTT_UNICODE_EID_UNICODE_2_0_BMP
                                           0,  // Language ID 0 for Unicode
                                           1);
    }
    
    // If still not found, try Mac platform
    if (fontName == nullptr || nameLength == 0) {
        fontName = stbtt_GetFontNameString(info, &nameLength,
                                           1,  // STBTT_PLATFORM_ID_MAC
                                           0,  // STBTT_MAC_EID_ROMAN
                                           0,  // STBTT_MAC_LANG_ENGLISH
                                           1);
    }
    
    if (fontName == nullptr || nameLength == 0) {
        return false;
    }
    
    // Convert UTF-16BE to UTF-8 or use as-is if already UTF-8
    int copyLen = (nameLength < (int)(bufferSize - 1)) ? nameLength : (int)(bufferSize - 1);
    
    // Check if it's UTF-16BE (first byte is 0 and second is non-zero)
    if (copyLen >= 2 && fontName[0] == 0 && fontName[1] != 0) {
        // UTF-16BE: convert to UTF-8
        size_t outPos = 0;
        for (int i = 0; i < copyLen - 1 && outPos < bufferSize - 1; i += 2) {
            uint16_t u16 = ((unsigned char)fontName[i] << 8) | (unsigned char)fontName[i + 1];
            if (u16 < 0x80) {
                if (outPos < bufferSize - 1) {
                    nameBuffer[outPos++] = (char)u16;
                }
            } else if (u16 < 0x800) {
                if (outPos < bufferSize - 2) {
                    nameBuffer[outPos++] = (char)(0xC0 | (u16 >> 6));
                    nameBuffer[outPos++] = (char)(0x80 | (u16 & 0x3F));
                }
            } else if (u16 < 0xD800 || u16 >= 0xE000) {  // Valid BMP (not surrogate)
                if (outPos < bufferSize - 3) {
                    nameBuffer[outPos++] = (char)(0xE0 | (u16 >> 12));
                    nameBuffer[outPos++] = (char)(0x80 | ((u16 >> 6) & 0x3F));
                    nameBuffer[outPos++] = (char)(0x80 | (u16 & 0x3F));
                }
            }
        }
        nameBuffer[outPos] = '\0';
    } else {
        // Assume it's already UTF-8 or ASCII
        memcpy(nameBuffer, fontName, copyLen);
        nameBuffer[copyLen] = '\0';
    }
    
    return true;
}

// ============================================================================
// Font Metrics
// ============================================================================

void EL133UF1_TTF::getFontMetrics(float fontSize, int16_t* ascent, int16_t* descent, int16_t* lineGap) {
    if (!_fontLoaded || _fontInfo == nullptr) {
        if (ascent) *ascent = 0;
        if (descent) *descent = 0;
        if (lineGap) *lineGap = 0;
        return;
    }
    
    stbtt_fontinfo* info = (stbtt_fontinfo*)_fontInfo;
    float scale = stbtt_ScaleForPixelHeight(info, fontSize);
    
    int asc, desc, gap;
    stbtt_GetFontVMetrics(info, &asc, &desc, &gap);
    
    if (ascent) *ascent = (int16_t)(asc * scale);
    if (descent) *descent = (int16_t)(desc * scale);
    if (lineGap) *lineGap = (int16_t)(gap * scale);
}

int16_t EL133UF1_TTF::getTextWidth(const char* text, float fontSize) {
    if (!_fontLoaded || _fontInfo == nullptr || text == nullptr) {
        return 0;
    }
    
    stbtt_fontinfo* info = (stbtt_fontinfo*)_fontInfo;
    float scale = stbtt_ScaleForPixelHeight(info, fontSize);
    
    int width = 0;
    const char* p = text;
    
    while (*p) {
        int codepoint = (uint8_t)*p++;
        
        // Handle UTF-8 (basic 2-byte sequences)
        if (codepoint >= 0xC0 && codepoint < 0xE0 && *p) {
            codepoint = ((codepoint & 0x1F) << 6) | (*p++ & 0x3F);
        }
        
        int advance, lsb;
        stbtt_GetCodepointHMetrics(info, codepoint, &advance, &lsb);
        width += (int)(advance * scale);
        
        // Add kerning if there's a next character
        if (*p) {
            int nextCodepoint = (uint8_t)*p;
            if (nextCodepoint >= 0xC0 && nextCodepoint < 0xE0 && *(p+1)) {
                nextCodepoint = ((nextCodepoint & 0x1F) << 6) | (*(p+1) & 0x3F);
            }
            int kern = stbtt_GetCodepointKernAdvance(info, codepoint, nextCodepoint);
            width += (int)(kern * scale);
        }
    }
    
    return (int16_t)width;
}

// ============================================================================
// Glyph Rendering
// ============================================================================

void EL133UF1_TTF::renderGlyph(int codepoint, int16_t x, int16_t y, float scale, 
                                uint8_t color, uint8_t bgColor) {
    if (!_fontLoaded || _fontInfo == nullptr || _display == nullptr) {
        return;
    }
    
    stbtt_fontinfo* info = (stbtt_fontinfo*)_fontInfo;
    
    // Get glyph bounding box
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(info, codepoint, scale, scale, &x0, &y0, &x1, &y1);
    
    int glyphWidth = x1 - x0;
    int glyphHeight = y1 - y0;
    
    if (glyphWidth <= 0 || glyphHeight <= 0) {
        return;  // No visible glyph (e.g., space)
    }
    
    // Allocate temporary bitmap for glyph
    // Use regular malloc - this is a small temporary buffer
    size_t bitmapSize = glyphWidth * glyphHeight;
    uint8_t* bitmap = (uint8_t*)malloc(bitmapSize);
    if (bitmap == nullptr) {
        Serial.printf("TTF: Failed to allocate glyph bitmap (%d bytes)\n", bitmapSize);
        return;
    }
    
    // Render glyph to bitmap (8-bit grayscale)
    stbtt_MakeCodepointBitmap(info, bitmap, glyphWidth, glyphHeight, 
                               glyphWidth, scale, scale, codepoint);
    
    // Calculate actual screen position
    int16_t screenX = x + x0;
    int16_t screenY = y + y0;
    
    // Draw glyph pixels to display
    // For e-ink with limited colors, we use threshold-based rendering
    // Alpha > 127 = foreground color, else background (or skip if transparent)
    bool transparentBg = (bgColor == 0xFF);
    
    for (int py = 0; py < glyphHeight; py++) {
        int16_t drawY = screenY + py;
        if (drawY < 0 || drawY >= _display->height()) continue;
        
        for (int px = 0; px < glyphWidth; px++) {
            int16_t drawX = screenX + px;
            if (drawX < 0 || drawX >= _display->width()) continue;
            
            uint8_t alpha = bitmap[py * glyphWidth + px];
            
            // Threshold-based rendering for e-ink
            // Could be enhanced with dithering for smoother edges
            if (alpha > 127) {
                _display->setPixel(drawX, drawY, color);
            } else if (!transparentBg && alpha > 32) {
                // Optional: intermediate threshold for anti-aliasing effect
                // with limited e-ink colors, might want to skip this
                _display->setPixel(drawX, drawY, bgColor);
            } else if (!transparentBg) {
                _display->setPixel(drawX, drawY, bgColor);
            }
            // If transparent background, just skip pixels with low alpha
        }
    }
    
    free(bitmap);
}

// ============================================================================
// Text Drawing
// ============================================================================

void EL133UF1_TTF::drawText(int16_t x, int16_t y, const char* text, float fontSize, 
                            uint8_t color, uint8_t bgColor) {
    if (!_fontLoaded || _fontInfo == nullptr || _display == nullptr || text == nullptr) {
        return;
    }
    
    stbtt_fontinfo* info = (stbtt_fontinfo*)_fontInfo;
    float scale = stbtt_ScaleForPixelHeight(info, fontSize);
    
    // Check if we can use the cache (size must match)
    bool useCache = _cacheEnabled && (fontSize == _cacheFontSize);
    
    // Get font metrics for baseline positioning
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(info, &ascent, &descent, &lineGap);
    int baselineOffset = (int)(ascent * scale);
    
    // Adjust y to be top of text (caller provides top-left, we need baseline)
    int16_t baseline = y + baselineOffset;
    
    int xPos = x;
    const char* p = text;
    int prevCodepoint = 0;
    
    while (*p) {
        // Decode character (handle basic UTF-8)
        int codepoint = (uint8_t)*p++;
        
        // Handle UTF-8 multi-byte sequences (2-byte only for now)
        if (codepoint >= 0xC0 && codepoint < 0xE0 && *p) {
            codepoint = ((codepoint & 0x1F) << 6) | (*p++ & 0x3F);
        } else if (codepoint >= 0xE0 && codepoint < 0xF0 && *p && *(p+1)) {
            // 3-byte UTF-8
            codepoint = ((codepoint & 0x0F) << 12) | 
                       ((*p++ & 0x3F) << 6) | 
                       (*p++ & 0x3F);
        }
        
        // Handle newline
        if (codepoint == '\n') {
            xPos = x;
            baseline += (int)((ascent - descent + lineGap) * scale);
            prevCodepoint = 0;
            continue;
        }
        
        // Apply kerning from previous character
        if (prevCodepoint) {
            int kern = stbtt_GetCodepointKernAdvance(info, prevCodepoint, codepoint);
            xPos += (int)(kern * scale);
        }
        
        // Try to use cached glyph first
        CachedGlyph* cached = useCache ? findCachedGlyph(codepoint) : nullptr;
        if (cached) {
            // Fast path: use pre-rendered glyph
            renderCachedGlyph(cached, xPos, baseline, color);
            xPos += cached->advance;
        } else {
            // Slow path: render glyph on-the-fly
            renderGlyph(codepoint, xPos, baseline, scale, color, bgColor);
            
            // Advance cursor
            int advance, lsb;
            stbtt_GetCodepointHMetrics(info, codepoint, &advance, &lsb);
            xPos += (int)(advance * scale);
        }
        
        prevCodepoint = codepoint;
    }
}

// ============================================================================
// Advanced Text Drawing with Alignment
// ============================================================================

void EL133UF1_TTF::drawTextCentered(int16_t x, int16_t y, int16_t width, 
                                     const char* text, float fontSize, uint8_t color) {
    int16_t textWidth = getTextWidth(text, fontSize);
    int16_t offsetX = (width - textWidth) / 2;
    drawText(x + offsetX, y, text, fontSize, color);
}

void EL133UF1_TTF::drawTextRight(int16_t x, int16_t y, int16_t width,
                                  const char* text, float fontSize, uint8_t color) {
    int16_t textWidth = getTextWidth(text, fontSize);
    int16_t offsetX = width - textWidth;
    drawText(x + offsetX, y, text, fontSize, color);
}

// ============================================================================
// Anchor-Based Alignment
// ============================================================================

int16_t EL133UF1_TTF::getTextHeight(float fontSize) {
    if (!_fontLoaded || _fontInfo == nullptr) return 0;
    
    stbtt_fontinfo* info = (stbtt_fontinfo*)_fontInfo;
    float scale = stbtt_ScaleForPixelHeight(info, fontSize);
    
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(info, &ascent, &descent, &lineGap);
    
    // descent is negative, so we subtract it (adding its absolute value)
    return (int16_t)((ascent - descent) * scale);
}

void EL133UF1_TTF::drawTextAligned(int16_t x, int16_t y, const char* text, float fontSize,
                                    uint8_t color, TextAlignH alignH, TextAlignV alignV,
                                    uint8_t bgColor) {
    if (!_fontLoaded || _fontInfo == nullptr || _display == nullptr || text == nullptr) return;
    
    stbtt_fontinfo* info = (stbtt_fontinfo*)_fontInfo;
    float scale = stbtt_ScaleForPixelHeight(info, fontSize);
    
    // Get font metrics
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(info, &ascent, &descent, &lineGap);
    int16_t ascentPx = (int16_t)(ascent * scale);
    int16_t descentPx = (int16_t)(descent * scale);  // negative value
    int16_t totalHeight = ascentPx - descentPx;
    
    // Calculate horizontal offset based on alignment
    int16_t drawX = x;
    if (alignH != ALIGN_LEFT) {
        int16_t textWidth = getTextWidth(text, fontSize);
        if (alignH == ALIGN_CENTER) {
            drawX = x - textWidth / 2;
        } else if (alignH == ALIGN_RIGHT) {
            drawX = x - textWidth;
        }
    }
    
    // Calculate vertical offset based on alignment
    // Note: drawText expects y to be TOP of text, so we adjust accordingly
    int16_t drawY = y;
    switch (alignV) {
        case ALIGN_TOP:
            // y is already top, no adjustment needed
            break;
        case ALIGN_BASELINE:
            // y is baseline, top is baseline minus ascent
            drawY = y - ascentPx;
            break;
        case ALIGN_BOTTOM:
            // y is bottom (descender line), top is y minus total height
            drawY = y - totalHeight;
            break;
        case ALIGN_MIDDLE:
            // y is vertical center, top is y minus half the height
            drawY = y - totalHeight / 2;
            break;
    }
    
    drawText(drawX, drawY, text, fontSize, color, bgColor);
}

void EL133UF1_TTF::drawTextAlignedOutlined(int16_t x, int16_t y, const char* text, float fontSize,
                                            uint8_t color, uint8_t outlineColor,
                                            TextAlignH alignH, TextAlignV alignV,
                                            int outlineWidth, bool exactOutline) {
    if (!_fontLoaded || _fontInfo == nullptr || _display == nullptr || text == nullptr) return;
    
    stbtt_fontinfo* info = (stbtt_fontinfo*)_fontInfo;
    float scale = stbtt_ScaleForPixelHeight(info, fontSize);
    
    // Get font metrics
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(info, &ascent, &descent, &lineGap);
    int16_t ascentPx = (int16_t)(ascent * scale);
    int16_t descentPx = (int16_t)(descent * scale);
    int16_t totalHeight = ascentPx - descentPx;
    
    // Calculate horizontal offset
    int16_t drawX = x;
    if (alignH != ALIGN_LEFT) {
        int16_t textWidth = getTextWidth(text, fontSize);
        if (alignH == ALIGN_CENTER) {
            drawX = x - textWidth / 2;
        } else if (alignH == ALIGN_RIGHT) {
            drawX = x - textWidth;
        }
    }
    
    // Calculate vertical offset
    int16_t drawY = y;
    switch (alignV) {
        case ALIGN_TOP:
            break;
        case ALIGN_BASELINE:
            drawY = y - ascentPx;
            break;
        case ALIGN_BOTTOM:
            drawY = y - totalHeight;
            break;
        case ALIGN_MIDDLE:
            drawY = y - totalHeight / 2;
            break;
    }
    
    drawTextOutlined(drawX, drawY, text, fontSize, color, outlineColor, outlineWidth, exactOutline);
}

// ============================================================================
// Outlined Text
// ============================================================================

// Render a single glyph with outline using optimized 2-pass separable dilation
// This is O(w*h*outlineWidth) instead of O(w*h*outlineWidth²)
void EL133UF1_TTF::renderGlyphOutlined(int codepoint, int16_t x, int16_t baseline,
                                        float scale, uint8_t color, uint8_t outlineColor,
                                        int outlineWidth) {
    stbtt_fontinfo* info = (stbtt_fontinfo*)_fontInfo;
    
    // Get glyph bounding box
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(info, codepoint, scale, scale, &x0, &y0, &x1, &y1);
    
    int glyphWidth = x1 - x0;
    int glyphHeight = y1 - y0;
    
    if (glyphWidth <= 0 || glyphHeight <= 0) return;
    
    // Expanded buffer size to accommodate outline
    int pad = outlineWidth;
    int bufWidth = glyphWidth + pad * 2;
    int bufHeight = glyphHeight + pad * 2;
    size_t bufSize = bufWidth * bufHeight;
    
    // Allocate glyph buffer and dilation buffer
    uint8_t* glyphBuf = (uint8_t*)malloc(bufSize);
    uint8_t* dilatedH = (uint8_t*)malloc(bufSize);  // Horizontal dilation result
    
    if (!glyphBuf || !dilatedH) {
        if (glyphBuf) free(glyphBuf);
        if (dilatedH) free(dilatedH);
        return;
    }
    
    memset(glyphBuf, 0, bufSize);
    memset(dilatedH, 0, bufSize);
    
    // Render glyph centered in padded buffer
    stbtt_MakeCodepointBitmap(info, glyphBuf + pad * bufWidth + pad,
                               glyphWidth, glyphHeight, bufWidth, scale, scale, codepoint);
    
    // Convert to binary (threshold at 127) for faster dilation
    for (size_t i = 0; i < bufSize; i++) {
        glyphBuf[i] = (glyphBuf[i] > 127) ? 1 : 0;
    }
    
    // ========================================================================
    // Pass 1: Horizontal dilation - for each pixel, check outlineWidth neighbors left/right
    // ========================================================================
    for (int py = 0; py < bufHeight; py++) {
        uint8_t* srcRow = glyphBuf + py * bufWidth;
        uint8_t* dstRow = dilatedH + py * bufWidth;
        
        for (int px = 0; px < bufWidth; px++) {
            // Check if any pixel in horizontal window is set
            int xStart = (px - outlineWidth < 0) ? 0 : px - outlineWidth;
            int xEnd = (px + outlineWidth >= bufWidth) ? bufWidth - 1 : px + outlineWidth;
            
            uint8_t found = 0;
            for (int nx = xStart; nx <= xEnd && !found; nx++) {
                found = srcRow[nx];
            }
            dstRow[px] = found;
        }
    }
    
    // ========================================================================
    // Pass 2: Vertical dilation on dilatedH + draw directly to display
    // For each pixel, check outlineWidth neighbors above/below in dilatedH
    // ========================================================================
    int16_t screenX = x + x0 - pad;
    int16_t screenY = baseline + y0 - pad;
    
    for (int py = 0; py < bufHeight; py++) {
        int16_t drawY = screenY + py;
        if (drawY < 0 || drawY >= _display->height()) continue;
        
        for (int px = 0; px < bufWidth; px++) {
            int16_t drawX = screenX + px;
            if (drawX < 0 || drawX >= _display->width()) continue;
            
            // Original glyph pixel - draw foreground color
            if (glyphBuf[py * bufWidth + px]) {
                _display->setPixel(drawX, drawY, color);
                continue;
            }
            
            // Check vertical dilation on horizontal-dilated result
            int yStart = (py - outlineWidth < 0) ? 0 : py - outlineWidth;
            int yEnd = (py + outlineWidth >= bufHeight) ? bufHeight - 1 : py + outlineWidth;
            
            bool isOutline = false;
            for (int ny = yStart; ny <= yEnd && !isOutline; ny++) {
                if (dilatedH[ny * bufWidth + px]) {
                    isOutline = true;
                }
            }
            
            if (isOutline) {
                _display->setPixel(drawX, drawY, outlineColor);
            }
        }
    }
    
    free(glyphBuf);
    free(dilatedH);
}

void EL133UF1_TTF::drawTextOutlined(int16_t x, int16_t y, const char* text, 
                                     float fontSize, uint8_t color, 
                                     uint8_t outlineColor, int outlineWidth,
                                     bool exactOutline) {
    if (!_fontLoaded || _fontInfo == nullptr || _display == nullptr || text == nullptr) return;
    
    // Exact mode: render text multiple times at offsets (slower but pixel-perfect)
    if (exactOutline) {
        for (int w = outlineWidth; w >= 1; w--) {
            for (int dy = -w; dy <= w; dy++) {
                for (int dx = -w; dx <= w; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    if (abs(dx) < w && abs(dy) < w) continue;
                    drawText(x + dx, y + dy, text, fontSize, outlineColor);
                }
            }
        }
        drawText(x, y, text, fontSize, color);
        return;
    }
    
    // Fast mode: single render with dilation
    stbtt_fontinfo* info = (stbtt_fontinfo*)_fontInfo;
    float scale = stbtt_ScaleForPixelHeight(info, fontSize);
    
    // Get font metrics for baseline
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(info, &ascent, &descent, &lineGap);
    int baseline = y + (int)(ascent * scale);
    
    int xPos = x;
    int prevCodepoint = 0;
    const char* p = text;
    
    while (*p) {
        // Decode UTF-8 (same as drawText)
        int codepoint = (uint8_t)*p++;
        if (codepoint >= 0xC0 && codepoint < 0xE0 && *p) {
            codepoint = ((codepoint & 0x1F) << 6) | (*p++ & 0x3F);
        } else if (codepoint >= 0xE0 && codepoint < 0xF0 && *p && *(p+1)) {
            codepoint = ((codepoint & 0x0F) << 12) | 
                       ((*p++ & 0x3F) << 6) | 
                       (*p++ & 0x3F);
        }
        
        // Handle newlines
        if (codepoint == '\n') {
            xPos = x;
            baseline += (int)((ascent - descent + lineGap) * scale);
            prevCodepoint = 0;
            continue;
        }
        
        // Apply kerning
        if (prevCodepoint) {
            int kern = stbtt_GetCodepointKernAdvance(info, prevCodepoint, codepoint);
            xPos += (int)(kern * scale);
        }
        
        // Render glyph with outline
        renderGlyphOutlined(codepoint, xPos, baseline, scale, color, outlineColor, outlineWidth);
        
        // Advance cursor
        int advance, lsb;
        stbtt_GetCodepointHMetrics(info, codepoint, &advance, &lsb);
        xPos += (int)(advance * scale);
        
        prevCodepoint = codepoint;
    }
}

void EL133UF1_TTF::drawTextOutlinedCentered(int16_t x, int16_t y, int16_t width,
                                             const char* text, float fontSize,
                                             uint8_t color, uint8_t outlineColor, 
                                             int outlineWidth, bool exactOutline) {
    int16_t textWidth = getTextWidth(text, fontSize);
    int16_t offsetX = (width - textWidth) / 2;
    drawTextOutlined(x + offsetX, y, text, fontSize, color, outlineColor, outlineWidth, exactOutline);
}

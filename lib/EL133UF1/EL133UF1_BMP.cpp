/**
 * @file EL133UF1_BMP.cpp
 * @brief BMP image loader implementation for EL133UF1 display
 */

#include "EL133UF1_BMP.h"
#include "EL133UF1_Color.h"

EL133UF1_BMP::EL133UF1_BMP() : _display(nullptr) {}

bool EL133UF1_BMP::begin(EL133UF1* display) {
    if (display == nullptr) return false;
    _display = display;
    return true;
}

uint8_t EL133UF1_BMP::mapToSpectra6(uint8_t r, uint8_t g, uint8_t b) {
    // Use fast LUT-based color mapping (Lab perceptual matching pre-computed)
    return spectra6Color.mapColorFast(r, g, b);
}

BMPResult EL133UF1_BMP::parseHeaders(const uint8_t* data, size_t len,
                                      const BMPFileHeader** fileHeader,
                                      const BMPInfoHeader** infoHeader) {
    if (data == nullptr || len < sizeof(BMPFileHeader) + sizeof(BMPInfoHeader)) {
        return BMP_ERR_NULL_DATA;
    }
    
    *fileHeader = (const BMPFileHeader*)data;
    
    // Check BMP signature ('BM' = 0x4D42 little-endian)
    if ((*fileHeader)->signature != 0x4D42) {
        return BMP_ERR_INVALID_SIGNATURE;
    }
    
    *infoHeader = (const BMPInfoHeader*)(data + sizeof(BMPFileHeader));
    
    // Check for supported formats
    if ((*infoHeader)->compression != 0) {
        return BMP_ERR_COMPRESSED;
    }
    
    uint16_t bpp = (*infoHeader)->bitsPerPixel;
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32) {
        return BMP_ERR_UNSUPPORTED_FORMAT;
    }
    
    return BMP_OK;
}

BMPResult EL133UF1_BMP::getInfo(const uint8_t* data, size_t len,
                                 int32_t* width, int32_t* height, uint16_t* bpp) {
    const BMPFileHeader* fh;
    const BMPInfoHeader* ih;
    
    BMPResult result = parseHeaders(data, len, &fh, &ih);
    if (result != BMP_OK) return result;
    
    if (width) *width = ih->width;
    if (height) *height = abs(ih->height);
    if (bpp) *bpp = ih->bitsPerPixel;
    
    return BMP_OK;
}

BMPResult EL133UF1_BMP::draw(int16_t x, int16_t y, const uint8_t* data, size_t len) {
    if (_display == nullptr) return BMP_ERR_NO_DISPLAY;
    
    const BMPFileHeader* fh;
    const BMPInfoHeader* ih;
    
    BMPResult result = parseHeaders(data, len, &fh, &ih);
    if (result != BMP_OK) return result;
    
    uint32_t dataOffset = fh->dataOffset;
    uint16_t bpp = ih->bitsPerPixel;
    
    // Color palette location (for indexed images)
    const uint8_t* palette = data + sizeof(BMPFileHeader) + ih->headerSize;
    
    Serial.printf("BMP: %ldx%ld, %d bpp, offset %lu\n", 
                  ih->width, ih->height, bpp, dataOffset);
    
    uint32_t t0 = millis();
    
    switch (bpp) {
        case 24:
        case 32:
            draw24bit(x, y, data, ih, dataOffset);
            break;
        case 8:
            draw8bit(x, y, data, ih, dataOffset, palette);
            break;
        case 4:
            draw4bit(x, y, data, ih, dataOffset, palette);
            break;
        case 1:
            draw1bit(x, y, data, ih, dataOffset, palette);
            break;
        default:
            return BMP_ERR_UNSUPPORTED_FORMAT;
    }
    
    Serial.printf("BMP decode+draw: %lu ms\n", millis() - t0);
    
    return BMP_OK;
}

BMPResult EL133UF1_BMP::drawFullscreen(const uint8_t* data, size_t len) {
    if (_display == nullptr) return BMP_ERR_NO_DISPLAY;
    
    int32_t width, height;
    uint16_t bpp;
    
    BMPResult result = getInfo(data, len, &width, &height, &bpp);
    if (result != BMP_OK) return result;
    
    // Center the image
    int16_t x = (_display->width() - width) / 2;
    int16_t y = (_display->height() - height) / 2;
    
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    
    return draw(x, y, data, len);
}

void EL133UF1_BMP::draw24bit(int16_t x, int16_t y, const uint8_t* data,
                              const BMPInfoHeader* info, uint32_t dataOffset) {
    int32_t width = info->width;
    int32_t height = info->height;
    bool topDown = (height < 0);
    if (topDown) height = -height;
    
    uint16_t bpp = info->bitsPerPixel;
    int bytesPerPixel = bpp / 8;
    
    // Row size padded to 4-byte boundary
    uint32_t rowSize = ((width * bytesPerPixel + 3) / 4) * 4;
    
    const uint8_t* pixelData = data + dataOffset;
    
    // Check if we can use fast row access
    bool useFastPath = _display->canUseFastRowAccess();
    
    // Allocate row color buffer for batch writes
    uint8_t* rowColors = nullptr;
    if (useFastPath) {
        rowColors = (uint8_t*)malloc(width);
        if (!rowColors) useFastPath = false;  // Fall back to per-pixel
    }
    
    for (int32_t row = 0; row < height; row++) {
        // BMP is bottom-up by default, top-down if height is negative
        int32_t srcRow = topDown ? row : (height - 1 - row);
        int16_t dstY = y + row;
        
        if (dstY < 0 || dstY >= _display->height()) continue;
        
        const uint8_t* rowPtr = pixelData + srcRow * rowSize;
        
        if (useFastPath && x >= 0 && x + width <= _display->width()) {
            // FAST PATH: Convert entire row then batch write
            for (int32_t col = 0; col < width; col++) {
                uint8_t b = rowPtr[col * bytesPerPixel + 0];
                uint8_t g = rowPtr[col * bytesPerPixel + 1];
                uint8_t r = rowPtr[col * bytesPerPixel + 2];
                rowColors[col] = mapToSpectra6(r, g, b);
            }
            _display->writeRowFast(x, dstY, rowColors, width);
        } else {
            // FALLBACK: Per-pixel with bounds checking
            for (int32_t col = 0; col < width; col++) {
                int16_t dstX = x + col;
                if (dstX < 0 || dstX >= _display->width()) continue;
                
                uint8_t b = rowPtr[col * bytesPerPixel + 0];
                uint8_t g = rowPtr[col * bytesPerPixel + 1];
                uint8_t r = rowPtr[col * bytesPerPixel + 2];
                
                uint8_t spectraColor = mapToSpectra6(r, g, b);
                _display->setPixel(dstX, dstY, spectraColor);
            }
        }
    }
    
    if (rowColors) free(rowColors);
}

void EL133UF1_BMP::draw8bit(int16_t x, int16_t y, const uint8_t* data,
                             const BMPInfoHeader* info, uint32_t dataOffset,
                             const uint8_t* palette) {
    int32_t width = info->width;
    int32_t height = info->height;
    bool topDown = (height < 0);
    if (topDown) height = -height;
    
    // Row size padded to 4-byte boundary
    uint32_t rowSize = ((width + 3) / 4) * 4;
    
    // Pre-map palette to Spectra colors
    uint8_t paletteMap[256];
    int numColors = info->colorsUsed ? info->colorsUsed : 256;
    for (int i = 0; i < numColors; i++) {
        // Palette is BGRA (4 bytes per entry)
        uint8_t b = palette[i * 4 + 0];
        uint8_t g = palette[i * 4 + 1];
        uint8_t r = palette[i * 4 + 2];
        paletteMap[i] = mapToSpectra6(r, g, b);
    }
    
    const uint8_t* pixelData = data + dataOffset;
    
    // Check if we can use fast row access
    bool useFastPath = _display->canUseFastRowAccess();
    
    // Allocate row color buffer for batch writes
    uint8_t* rowColors = nullptr;
    if (useFastPath) {
        rowColors = (uint8_t*)malloc(width);
        if (!rowColors) useFastPath = false;
    }
    
    for (int32_t row = 0; row < height; row++) {
        int32_t srcRow = topDown ? row : (height - 1 - row);
        int16_t dstY = y + row;
        
        if (dstY < 0 || dstY >= _display->height()) continue;
        
        const uint8_t* rowPtr = pixelData + srcRow * rowSize;
        
        if (useFastPath && x >= 0 && x + width <= _display->width()) {
            // FAST PATH: Map entire row then batch write
            for (int32_t col = 0; col < width; col++) {
                rowColors[col] = paletteMap[rowPtr[col]];
            }
            _display->writeRowFast(x, dstY, rowColors, width);
        } else {
            // FALLBACK: Per-pixel with bounds checking
            for (int32_t col = 0; col < width; col++) {
                int16_t dstX = x + col;
                if (dstX < 0 || dstX >= _display->width()) continue;
                
                uint8_t index = rowPtr[col];
                _display->setPixel(dstX, dstY, paletteMap[index]);
            }
        }
    }
    
    if (rowColors) free(rowColors);
}

void EL133UF1_BMP::draw4bit(int16_t x, int16_t y, const uint8_t* data,
                             const BMPInfoHeader* info, uint32_t dataOffset,
                             const uint8_t* palette) {
    int32_t width = info->width;
    int32_t height = info->height;
    bool topDown = (height < 0);
    if (topDown) height = -height;
    
    // Row size: each byte has 2 pixels, padded to 4-byte boundary
    uint32_t rowSize = (((width + 1) / 2 + 3) / 4) * 4;
    
    // Pre-map palette to Spectra colors (16 entries max)
    uint8_t paletteMap[16];
    int numColors = info->colorsUsed ? info->colorsUsed : 16;
    if (numColors > 16) numColors = 16;
    for (int i = 0; i < numColors; i++) {
        uint8_t b = palette[i * 4 + 0];
        uint8_t g = palette[i * 4 + 1];
        uint8_t r = palette[i * 4 + 2];
        paletteMap[i] = mapToSpectra6(r, g, b);
    }
    
    const uint8_t* pixelData = data + dataOffset;
    
    // Check if we can use fast row access
    bool useFastPath = _display->canUseFastRowAccess();
    
    // Allocate row color buffer for batch writes
    uint8_t* rowColors = nullptr;
    if (useFastPath) {
        rowColors = (uint8_t*)malloc(width);
        if (!rowColors) useFastPath = false;
    }
    
    for (int32_t row = 0; row < height; row++) {
        int32_t srcRow = topDown ? row : (height - 1 - row);
        int16_t dstY = y + row;
        
        if (dstY < 0 || dstY >= _display->height()) continue;
        
        const uint8_t* rowPtr = pixelData + srcRow * rowSize;
        
        if (useFastPath && x >= 0 && x + width <= _display->width()) {
            // FAST PATH: Decode entire row then batch write
            for (int32_t col = 0; col < width; col++) {
                uint8_t byteVal = rowPtr[col / 2];
                uint8_t index = (col & 1) ? (byteVal & 0x0F) : (byteVal >> 4);
                rowColors[col] = paletteMap[index];
            }
            _display->writeRowFast(x, dstY, rowColors, width);
        } else {
            // FALLBACK: Per-pixel with bounds checking
            for (int32_t col = 0; col < width; col++) {
                int16_t dstX = x + col;
                if (dstX < 0 || dstX >= _display->width()) continue;
                
                uint8_t byteVal = rowPtr[col / 2];
                uint8_t index = (col & 1) ? (byteVal & 0x0F) : (byteVal >> 4);
                _display->setPixel(dstX, dstY, paletteMap[index]);
            }
        }
    }
    
    if (rowColors) free(rowColors);
}

void EL133UF1_BMP::draw1bit(int16_t x, int16_t y, const uint8_t* data,
                             const BMPInfoHeader* info, uint32_t dataOffset,
                             const uint8_t* palette) {
    int32_t width = info->width;
    int32_t height = info->height;
    bool topDown = (height < 0);
    if (topDown) height = -height;
    
    // Row size: 8 pixels per byte, padded to 4-byte boundary
    uint32_t rowSize = (((width + 7) / 8 + 3) / 4) * 4;
    
    // Map the 2 palette entries
    uint8_t color0, color1;
    if (palette) {
        color0 = mapToSpectra6(palette[2], palette[1], palette[0]);
        color1 = mapToSpectra6(palette[6], palette[5], palette[4]);
    } else {
        color0 = EL133UF1_BLACK;
        color1 = EL133UF1_WHITE;
    }
    
    const uint8_t* pixelData = data + dataOffset;
    
    // Check if we can use fast row access
    bool useFastPath = _display->canUseFastRowAccess();
    
    // Allocate row color buffer for batch writes
    uint8_t* rowColors = nullptr;
    if (useFastPath) {
        rowColors = (uint8_t*)malloc(width);
        if (!rowColors) useFastPath = false;
    }
    
    for (int32_t row = 0; row < height; row++) {
        int32_t srcRow = topDown ? row : (height - 1 - row);
        int16_t dstY = y + row;
        
        if (dstY < 0 || dstY >= _display->height()) continue;
        
        const uint8_t* rowPtr = pixelData + srcRow * rowSize;
        
        if (useFastPath && x >= 0 && x + width <= _display->width()) {
            // FAST PATH: Decode entire row then batch write
            for (int32_t col = 0; col < width; col++) {
                uint8_t byteVal = rowPtr[col / 8];
                uint8_t bit = (byteVal >> (7 - (col & 7))) & 1;
                rowColors[col] = bit ? color1 : color0;
            }
            _display->writeRowFast(x, dstY, rowColors, width);
        } else {
            // FALLBACK: Per-pixel with bounds checking
            for (int32_t col = 0; col < width; col++) {
                int16_t dstX = x + col;
                if (dstX < 0 || dstX >= _display->width()) continue;
                
                uint8_t byteVal = rowPtr[col / 8];
                uint8_t bit = (byteVal >> (7 - (col & 7))) & 1;
                _display->setPixel(dstX, dstY, bit ? color1 : color0);
            }
        }
    }
    
    if (rowColors) free(rowColors);
}

const char* EL133UF1_BMP::getErrorString(BMPResult result) {
    switch (result) {
        case BMP_OK: return "OK";
        case BMP_ERR_NULL_DATA: return "Null or insufficient data";
        case BMP_ERR_INVALID_SIGNATURE: return "Invalid BMP signature (not 'BM')";
        case BMP_ERR_UNSUPPORTED_FORMAT: return "Unsupported BMP format";
        case BMP_ERR_COMPRESSED: return "Compressed BMPs not supported";
        case BMP_ERR_NO_DISPLAY: return "Display not initialized";
        default: return "Unknown error";
    }
}

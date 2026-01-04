/**
 * @file EL133UF1_SVG.cpp
 * @brief SVG icon renderer implementation for EL133UF1 display using NanoSVG
 * 
 * Uses NanoSVG to parse SVG files and rasterize them to RGBA, then maps
 * colors to the Spectra 6 palette and writes to the display buffer.
 */

#include "EL133UF1_SVG.h"
#include "EL133UF1_Color.h"
#include "platform_hal.h"  // For hal_psram_malloc/free
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Include NanoSVG implementation (define implementation macros)
#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg.h"
#include "nanosvgrast.h"

EL133UF1_SVG::EL133UF1_SVG()
    : _display(nullptr), _width(0), _height(0), _useDithering(false), _autoCrop(true)
{
}

EL133UF1_SVG::~EL133UF1_SVG()
{
}

bool EL133UF1_SVG::begin(EL133UF1* display) {
    if (display == nullptr) return false;
    _display = display;
    return true;
}

uint8_t EL133UF1_SVG::mapToSpectra6(uint8_t r, uint8_t g, uint8_t b) {
    // Use the global color mapper with fast LUT lookup if available
    return spectra6Color.mapColorFast(r, g, b);
}

bool EL133UF1_SVG::calculateContentBounds(void* image, float* minX, float* minY, float* maxX, float* maxY) {
    if (image == nullptr) return false;
    
    NSVGimage* svgImage = (NSVGimage*)image;
    NSVGshape* shape = svgImage->shapes;
    
    // Find union of all shape bounds
    bool foundBounds = false;
    float contentMinX = 0, contentMinY = 0, contentMaxX = 0, contentMaxY = 0;
    
    while (shape != nullptr) {
        // Only consider visible shapes
        if (shape->flags & NSVG_FLAGS_VISIBLE) {
            // Check all paths in this shape
            NSVGpath* path = shape->paths;
            while (path != nullptr) {
                if (path->npts > 0) {
                    float* bounds = path->bounds;  // [minx,miny,maxx,maxy]
                    if (!foundBounds) {
                        contentMinX = bounds[0];
                        contentMinY = bounds[1];
                        contentMaxX = bounds[2];
                        contentMaxY = bounds[3];
                        foundBounds = true;
                    } else {
                        if (bounds[0] < contentMinX) contentMinX = bounds[0];
                        if (bounds[1] < contentMinY) contentMinY = bounds[1];
                        if (bounds[2] > contentMaxX) contentMaxX = bounds[2];
                        if (bounds[3] > contentMaxY) contentMaxY = bounds[3];
                    }
                }
                path = path->next;
            }
        }
        shape = shape->next;
    }
    
    if (foundBounds) {
        *minX = contentMinX;
        *minY = contentMinY;
        *maxX = contentMaxX;
        *maxY = contentMaxY;
        return true;
    }
    
    return false;
}

SVGResult EL133UF1_SVG::rasterizeAndDraw(int16_t x, int16_t y, void* image, float scale, uint8_t bgColor) {
    if (_display == nullptr) return SVG_ERR_NO_DISPLAY;
    if (image == nullptr) return SVG_ERR_NULL_DATA;
    
    NSVGimage* svgImage = (NSVGimage*)image;
    
    // Debug: Check if SVG has shapes and their bounds
    int shapeCount = 0;
    int pathCount = 0;
    NSVGshape* shape = svgImage->shapes;
    while (shape != nullptr) {
        if (shape->flags & NSVG_FLAGS_VISIBLE) {
            shapeCount++;
            int pathsInShape = 0;
            NSVGpath* path = shape->paths;
            while (path != nullptr) {
                pathsInShape++;
                path = path->next;
            }
            pathCount += pathsInShape;
            Serial.printf("SVG: Shape %d: bounds [%.1f,%.1f,%.1f,%.1f], opacity=%.2f, paths=%d\n",
                         shapeCount, shape->bounds[0], shape->bounds[1], shape->bounds[2], shape->bounds[3],
                         shape->opacity, pathsInShape);
        }
        shape = shape->next;
    }
    Serial.printf("SVG: Image has %d visible shapes (%d total paths), size %.1fx%.1f\n", 
                 shapeCount, pathCount, svgImage->width, svgImage->height);
    
    // Calculate content bounds if auto-crop is enabled
    float contentMinX = 0, contentMinY = 0, contentMaxX = svgImage->width, contentMaxY = svgImage->height;
    float offsetX = 0, offsetY = 0;
    float adjustedScale = scale;
    bool wasCropped = false;
    
    // Calculate rasterization dimensions
    int32_t rastWidth, rastHeight;
    
    if (_autoCrop) {
        float boundsMinX, boundsMinY, boundsMaxX, boundsMaxY;
        if (calculateContentBounds(image, &boundsMinX, &boundsMinY, &boundsMaxX, &boundsMaxY)) {
            // Calculate content dimensions
            float contentWidth = boundsMaxX - boundsMinX;
            float contentHeight = boundsMaxY - boundsMinY;
            
            // If content is smaller than image (by more than 5%), we'll crop and center
            if (contentWidth > 0 && contentHeight > 0 && 
                (contentWidth < svgImage->width * 0.95f || contentHeight < svgImage->height * 0.95f)) {
                wasCropped = true;
                
                // Calculate target size based on user's scale and default icon size (100px)
                // The user's scale parameter applies to the final rendered size
                float baseTargetSize = 100.0f;  // Base target render size for weather icons
                float targetSize = baseTargetSize * scale;
                
                // Calculate scale to fit content into target size while maintaining aspect ratio
                float scaleX = targetSize / contentWidth;
                float scaleY = targetSize / contentHeight;
                float fitScale = (scaleX < scaleY) ? scaleX : scaleY;  // Use smaller to fit both dimensions
                adjustedScale = fitScale;
                
                // Calculate offset: translate so content bounds start at (0,0) in SVG space
                // Then center it in the target render area
                float scaledContentWidth = contentWidth * adjustedScale;
                float scaledContentHeight = contentHeight * adjustedScale;
                
                offsetX = -boundsMinX * adjustedScale;  // Move left edge of content to 0
                offsetY = -boundsMinY * adjustedScale;  // Move top edge of content to 0
                
                // Center in target area (if we have extra space)
                offsetX += (targetSize - scaledContentWidth) / 2.0f;
                offsetY += (targetSize - scaledContentHeight) / 2.0f;
                
                // Render at target size
                rastWidth = (int32_t)targetSize;
                rastHeight = (int32_t)targetSize;
                
                Serial.printf("SVG: Auto-cropped bounds: [%.1f,%.1f] to [%.1f,%.1f] (%.1fx%.1f)\n",
                             boundsMinX, boundsMinY, boundsMaxX, boundsMaxY, contentWidth, contentHeight);
                Serial.printf("SVG: Adjusted scale: %.2f, offset: [%.1f,%.1f], render: %ldx%ld\n", 
                             adjustedScale, offsetX, offsetY, rastWidth, rastHeight);
            } else {
                // Content fills image (no significant empty space), use normal dimensions
                rastWidth = (int32_t)(svgImage->width * scale);
                rastHeight = (int32_t)(svgImage->height * scale);
            }
        } else {
            // Could not calculate bounds, use normal dimensions
            rastWidth = (int32_t)(svgImage->width * scale);
            rastHeight = (int32_t)(svgImage->height * scale);
        }
    } else {
        // Auto-crop disabled, use normal dimensions
        rastWidth = (int32_t)(svgImage->width * scale);
        rastHeight = (int32_t)(svgImage->height * scale);
    }
    
    if (rastWidth <= 0 || rastHeight <= 0 || rastWidth > 2048 || rastHeight > 2048) {
        Serial.printf("SVG: Invalid rasterization size: %ldx%ld\n", rastWidth, rastHeight);
        return SVG_ERR_INVALID_SIZE;
    }
    
    _width = rastWidth;
    _height = rastHeight;
    
    // Allocate RGBA buffer in PSRAM for rasterization
    size_t rgbaBufferSize = rastWidth * rastHeight * 4;  // RGBA = 4 bytes per pixel
    uint8_t* rgbaBuffer = (uint8_t*)hal_psram_malloc(rgbaBufferSize);
    
    if (rgbaBuffer == nullptr) {
        Serial.printf("SVG: Failed to allocate %zu bytes for RGBA buffer\n", rgbaBufferSize);
        return SVG_ERR_ALLOC_FAILED;
    }
    
    // Initialize buffer to transparent black (alpha = 0)
    // The rasterizer will fill in the actual pixels, transparent areas will remain transparent
    memset(rgbaBuffer, 0, rgbaBufferSize);  // Clear to transparent black (RGBA=0,0,0,0)
    
    // Create rasterizer
    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (rast == nullptr) {
        hal_psram_free(rgbaBuffer);
        Serial.println("SVG: Failed to create rasterizer");
        return SVG_ERR_ALLOC_FAILED;
    }
    
    // Rasterize SVG to RGBA buffer
    // If auto-crop is enabled, use adjusted offset and scale to center cropped content
    Serial.printf("SVG: Rasterizing at offset (%.2f, %.2f), scale %.4f, size %ldx%ld, SVG size %.1fx%.1f\n",
                 offsetX, offsetY, adjustedScale, rastWidth, rastHeight, svgImage->width, svgImage->height);
    uint32_t t0 = millis();
    nsvgRasterize(rast, svgImage, offsetX, offsetY, adjustedScale, rgbaBuffer, rastWidth, rastHeight, rastWidth * 4);
    uint32_t rasterizeTime = millis() - t0;
    
    // Debug: Check first few pixels to see if rasterization produced anything
    if (rastWidth > 0 && rastHeight > 0) {
        uint8_t* firstPixel = rgbaBuffer;
        Serial.printf("SVG: First pixel RGBA: %d,%d,%d,%d\n", firstPixel[0], firstPixel[1], firstPixel[2], firstPixel[3]);
        // Check middle pixel
        uint8_t* midPixel = rgbaBuffer + ((rastHeight / 2) * rastWidth + (rastWidth / 2)) * 4;
        Serial.printf("SVG: Middle pixel RGBA: %d,%d,%d,%d\n", midPixel[0], midPixel[1], midPixel[2], midPixel[3]);
    }
    
    // Yield to watchdog after rasterization
    vTaskDelay(1);
    
    // Convert RGBA to Spectra 6 colors and write to display
    uint32_t drawTime = millis();
    int32_t pixelsDrawn = 0;
    int32_t pixelsSkippedBounds = 0;
    int32_t pixelsSkippedAlpha = 0;
    int32_t maxAlpha = 0;
    
    for (int32_t py = 0; py < rastHeight; py++) {
        int16_t dstY = y + py;
        if (dstY < 0 || dstY >= _display->height()) {
            pixelsSkippedBounds++;
            continue;
        }
        
        uint8_t* rgbaRow = rgbaBuffer + (py * rastWidth * 4);
        
        for (int32_t px = 0; px < rastWidth; px++) {
            int16_t dstX = x + px;
            if (dstX < 0 || dstX >= _display->width()) continue;
            
            // Extract RGBA
            uint8_t r = rgbaRow[px * 4 + 0];
            uint8_t g = rgbaRow[px * 4 + 1];
            uint8_t b = rgbaRow[px * 4 + 2];
            uint8_t a = rgbaRow[px * 4 + 3];
            
            // Track max alpha for debugging
            if (a > maxAlpha) maxAlpha = a;
            
            // Skip transparent pixels (alpha < 128)
            if (a < 128) {
                pixelsSkippedAlpha++;
                continue;  // Don't write, keep background
            }
            
            // Map to Spectra 6 color
            uint8_t color;
            if (_useDithering) {
                color = spectra6Color.mapColorDithered(px, py, r, g, b, rastWidth);
            } else {
                color = mapToSpectra6(r, g, b);
            }
            
            _display->setPixel(dstX, dstY, color);
            pixelsDrawn++;
        }
        
        // Yield to watchdog every 50 rows
        if (py % 50 == 0) {
            vTaskDelay(1);
        }
    }
    
    drawTime = millis() - drawTime;
    
    // Cleanup
    nsvgDeleteRasterizer(rast);
    hal_psram_free(rgbaBuffer);
    
    Serial.printf("SVG: Rasterized %ldx%ld in %lu ms, drew %ld pixels in %lu ms (pos: %d,%d, skipped: %ld bounds, %ld alpha, maxAlpha: %d)\n",
                  rastWidth, rastHeight, rasterizeTime, pixelsDrawn, drawTime, x, y, pixelsSkippedBounds, pixelsSkippedAlpha, maxAlpha);
    
    return SVG_OK;
}

SVGResult EL133UF1_SVG::draw(int16_t x, int16_t y, const char* svgData, float scale, uint8_t bgColor) {
    if (svgData == nullptr) return SVG_ERR_NULL_DATA;
    if (_display == nullptr) return SVG_ERR_NO_DISPLAY;
    
    // Parse SVG (requires null-terminated string)
    NSVGimage* image = nsvgParse((char*)svgData, "px", 96.0f);
    if (image == nullptr) {
        Serial.println("SVG: Failed to parse SVG data");
        return SVG_ERR_PARSE_FAILED;
    }
    
    SVGResult result = rasterizeAndDraw(x, y, image, scale, bgColor);
    
    // Cleanup parsed image
    nsvgDelete(image);
    
    return result;
}

SVGResult EL133UF1_SVG::draw(int16_t x, int16_t y, const uint8_t* svgData, size_t svgDataLen, 
                              float scale, uint8_t bgColor) {
    if (svgData == nullptr || svgDataLen == 0) return SVG_ERR_NULL_DATA;
    if (_display == nullptr) return SVG_ERR_NO_DISPLAY;
    
    // NanoSVG parser requires null-terminated string, so we need to copy the data
    // Allocate temporary buffer for null-terminated string (SVG files are typically small)
    // Try PSRAM first, fallback to heap
    bool usingPSRAM = true;
    char* svgString = (char*)hal_psram_malloc(svgDataLen + 1);
    if (svgString == nullptr) {
        // Fallback to heap if PSRAM allocation fails
        usingPSRAM = false;
        svgString = (char*)malloc(svgDataLen + 1);
        if (svgString == nullptr) {
            Serial.printf("SVG: Failed to allocate %zu bytes for SVG string\n", svgDataLen + 1);
            return SVG_ERR_ALLOC_FAILED;
        }
    }
    
    memcpy(svgString, svgData, svgDataLen);
    svgString[svgDataLen] = '\0';  // Null-terminate
    
    // Parse SVG
    NSVGimage* image = nsvgParse(svgString, "px", 96.0f);
    
    // Free temporary string using appropriate deallocator
    if (usingPSRAM) {
        hal_psram_free(svgString);
    } else {
        free(svgString);
    }
    
    if (image == nullptr) {
        Serial.println("SVG: Failed to parse SVG data");
        return SVG_ERR_PARSE_FAILED;
    }
    
    SVGResult result = rasterizeAndDraw(x, y, image, scale, bgColor);
    
    // Cleanup parsed image
    nsvgDelete(image);
    
    return result;
}

bool EL133UF1_SVG::getDimensions(const char* svgData, float* width, float* height) {
    if (svgData == nullptr || width == nullptr || height == nullptr) return false;
    
    NSVGimage* image = nsvgParse((char*)svgData, "px", 96.0f);
    if (image == nullptr) return false;
    
    *width = image->width;
    *height = image->height;
    
    nsvgDelete(image);
    return true;
}

bool EL133UF1_SVG::getDimensions(const uint8_t* svgData, size_t svgDataLen, float* width, float* height) {
    if (svgData == nullptr || svgDataLen == 0 || width == nullptr || height == nullptr) return false;
    
    // Allocate temporary buffer for null-terminated string
    bool usingPSRAM = true;
    char* svgString = (char*)hal_psram_malloc(svgDataLen + 1);
    if (svgString == nullptr) {
        usingPSRAM = false;
        svgString = (char*)malloc(svgDataLen + 1);
        if (svgString == nullptr) return false;
    }
    
    memcpy(svgString, svgData, svgDataLen);
    svgString[svgDataLen] = '\0';
    
    NSVGimage* image = nsvgParse(svgString, "px", 96.0f);
    
    // Free temporary string
    if (usingPSRAM) {
        hal_psram_free(svgString);
    } else {
        free(svgString);
    }
    
    if (image == nullptr) return false;
    
    *width = image->width;
    *height = image->height;
    
    nsvgDelete(image);
    return true;
}

const char* EL133UF1_SVG::getErrorString(SVGResult result) {
    switch (result) {
        case SVG_OK: return "OK";
        case SVG_ERR_NULL_DATA: return "Null data";
        case SVG_ERR_PARSE_FAILED: return "Parse failed";
        case SVG_ERR_RENDER_FAILED: return "Render failed";
        case SVG_ERR_NO_DISPLAY: return "No display";
        case SVG_ERR_ALLOC_FAILED: return "Allocation failed";
        case SVG_ERR_INVALID_SIZE: return "Invalid size";
        default: return "Unknown error";
    }
}

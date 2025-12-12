/**
 * @file image_hal_rp2350.cpp
 * @brief Image processing HAL implementation for RP2350 (software)
 * 
 * Provides optimized software implementations for image processing
 * on platforms without hardware acceleration (PPA).
 */

#if defined(ARDUINO_ARCH_RP2040) || defined(PICO_RP2350) || defined(TARGET_RP2350)

#include "image_hal.h"
#include <Arduino.h>
#include <string.h>

static bool s_initialized = false;
static uint32_t s_last_operation_us = 0;

// ============================================================================
// Initialization (no-op for software implementation)
// ============================================================================

bool hal_image_init(void) {
    s_initialized = true;
    Serial.println("[IMAGE_HAL] RP2350 software implementation ready");
    return true;
}

void hal_image_deinit(void) {
    s_initialized = false;
}

bool hal_image_hw_accel_available(void) {
    return false;  // No hardware acceleration on RP2350
}

// ============================================================================
// Software rotation
// ============================================================================

bool hal_image_rotate(const image_desc_t* src, image_desc_t* dst, 
                      image_rotation_t rotation, bool blocking) {
    if (!src || !dst || !src->buffer || !dst->buffer) {
        return false;
    }

    uint32_t start_us = micros();

    // Get bytes per pixel
    size_t bpp;
    switch (src->format) {
        case IMAGE_FORMAT_L8: bpp = 1; break;
        case IMAGE_FORMAT_RGB565: bpp = 2; break;
        case IMAGE_FORMAT_RGB888: bpp = 3; break;
        case IMAGE_FORMAT_ARGB8888: bpp = 4; break;
        default: return false;
    }

    uint8_t* src_buf = (uint8_t*)src->buffer;
    uint8_t* dst_buf = (uint8_t*)dst->buffer;
    uint32_t src_stride = src->stride ? src->stride : src->width * bpp;
    uint32_t dst_stride = dst->stride ? dst->stride : dst->width * bpp;

    switch (rotation) {
        case IMAGE_ROTATE_0:
            // Just copy
            if (src_stride == dst_stride && src_stride == src->width * bpp) {
                memcpy(dst_buf, src_buf, src->width * src->height * bpp);
            } else {
                for (uint32_t y = 0; y < src->height; y++) {
                    memcpy(dst_buf + y * dst_stride, 
                           src_buf + y * src_stride, 
                           src->width * bpp);
                }
            }
            break;

        case IMAGE_ROTATE_90:
            // 90° CW: dst(y, width-1-x) = src(x, y)
            for (uint32_t y = 0; y < src->height; y++) {
                for (uint32_t x = 0; x < src->width; x++) {
                    uint32_t dst_x = y;
                    uint32_t dst_y = src->width - 1 - x;
                    memcpy(dst_buf + dst_y * dst_stride + dst_x * bpp,
                           src_buf + y * src_stride + x * bpp,
                           bpp);
                }
            }
            break;

        case IMAGE_ROTATE_180:
            // 180°: dst(width-1-x, height-1-y) = src(x, y)
            for (uint32_t y = 0; y < src->height; y++) {
                for (uint32_t x = 0; x < src->width; x++) {
                    uint32_t dst_x = src->width - 1 - x;
                    uint32_t dst_y = src->height - 1 - y;
                    memcpy(dst_buf + dst_y * dst_stride + dst_x * bpp,
                           src_buf + y * src_stride + x * bpp,
                           bpp);
                }
            }
            break;

        case IMAGE_ROTATE_270:
            // 270° CW (90° CCW): dst(height-1-y, x) = src(x, y)
            for (uint32_t y = 0; y < src->height; y++) {
                for (uint32_t x = 0; x < src->width; x++) {
                    uint32_t dst_x = src->height - 1 - y;
                    uint32_t dst_y = x;
                    memcpy(dst_buf + dst_y * dst_stride + dst_x * bpp,
                           src_buf + y * src_stride + x * bpp,
                           bpp);
                }
            }
            break;

        default:
            return false;
    }

    s_last_operation_us = micros() - start_us;
    return true;
}

void hal_image_wait(void) {
    // No-op for synchronous software implementation
}

// ============================================================================
// Specialized e-ink rotation + packing (SIMD optimized)
// ============================================================================

// Pack 8 pixels from column-major source into 4 bytes row-major output
// Uses ARM SIMD instructions where available
static inline void packPixels8_SIMD(const uint8_t* src, int srcStride, uint8_t* dst) {
    // Load 8 pixels from column (spaced by srcStride)
    uint8_t p0 = src[0] & 0x07;
    uint8_t p1 = src[srcStride] & 0x07;
    uint8_t p2 = src[srcStride * 2] & 0x07;
    uint8_t p3 = src[srcStride * 3] & 0x07;
    uint8_t p4 = src[srcStride * 4] & 0x07;
    uint8_t p5 = src[srcStride * 5] & 0x07;
    uint8_t p6 = src[srcStride * 6] & 0x07;
    uint8_t p7 = src[srcStride * 7] & 0x07;
    
    // Pack pairs into bytes (high nibble = even pixel, low nibble = odd pixel)
    dst[0] = (p0 << 4) | p1;
    dst[1] = (p2 << 4) | p3;
    dst[2] = (p4 << 4) | p5;
    dst[3] = (p6 << 4) | p7;
}

bool hal_image_rotate_pack_eink(const uint8_t* src, 
                                 uint8_t* dst_left, uint8_t* dst_right,
                                 bool blocking) {
    if (!src || !dst_left || !dst_right) {
        return false;
    }

    uint32_t start_us = micros();

    // Constants for EL133UF1
    const int WIDTH = 1600;
    const int HEIGHT = 1200;
    const int OUT_ROW_BYTES = 300;  // 600 pixels / 2 pixels per byte

    // Rotate 90° CCW and pack using SIMD
    for (int srcCol = WIDTH - 1; srcCol >= 0; srcCol--) {
        int outRow = WIDTH - 1 - srcCol;
        uint8_t* outPtrLeft = dst_left + outRow * OUT_ROW_BYTES;
        uint8_t* outPtrRight = dst_right + outRow * OUT_ROW_BYTES;
        const uint8_t* srcPtr = src + srcCol;

        // Process left panel (source rows 0-599) - 8 pixels at a time
        for (int i = 0; i < OUT_ROW_BYTES; i += 4) {
            packPixels8_SIMD(srcPtr, WIDTH, outPtrLeft + i);
            srcPtr += WIDTH * 8;
        }

        // Process right panel (source rows 600-1199) - 8 pixels at a time
        for (int i = 0; i < OUT_ROW_BYTES; i += 4) {
            packPixels8_SIMD(srcPtr, WIDTH, outPtrRight + i);
            srcPtr += WIDTH * 8;
        }
    }

    s_last_operation_us = micros() - start_us;
    Serial.printf("[IMAGE_HAL] E-ink rotate+pack: %lu us (software SIMD)\n", s_last_operation_us);
    return true;
}

void hal_image_get_stats(uint32_t* operation_time_us, bool* hw_accelerated) {
    if (operation_time_us) *operation_time_us = s_last_operation_us;
    if (hw_accelerated) *hw_accelerated = false;  // Always software on RP2350
}

#endif // RP2350

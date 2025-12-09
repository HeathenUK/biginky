/**
 * @file image_hal_esp32.cpp
 * @brief Image processing HAL implementation for ESP32 (non-P4, software fallback)
 * 
 * For ESP32 variants without PPA (ESP32, ESP32-S2, ESP32-S3, ESP32-C3, etc.)
 * Uses optimized software implementation.
 */

// Check for PPA support
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#include "soc/soc_caps.h"
#endif

// Only compile if ESP32 but no PPA support
#if (defined(ESP32) || defined(ARDUINO_ARCH_ESP32)) && (!defined(SOC_PPA_SUPPORTED) || !SOC_PPA_SUPPORTED)

#include "image_hal.h"
#include <Arduino.h>
#include <string.h>

static bool s_initialized = false;
static uint32_t s_last_operation_us = 0;

// ============================================================================
// Initialization
// ============================================================================

bool hal_image_init(void) {
    s_initialized = true;
    Serial.println("[IMAGE_HAL] ESP32 software implementation ready (no PPA)");
    return true;
}

void hal_image_deinit(void) {
    s_initialized = false;
}

bool hal_image_hw_accel_available(void) {
    return false;  // No PPA on non-P4 ESP32 variants
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
// Specialized e-ink rotation + packing
// ============================================================================

bool hal_image_rotate_pack_eink(const uint8_t* src, 
                                 uint8_t* dst_left, uint8_t* dst_right,
                                 bool blocking) {
    if (!src || !dst_left || !dst_right) {
        return false;
    }

    uint32_t start_us = micros();

    const int WIDTH = 1600;
    const int HEIGHT = 1200;
    const int OUT_ROW_BYTES = 300;

    for (int srcCol = WIDTH - 1; srcCol >= 0; srcCol--) {
        int outRow = WIDTH - 1 - srcCol;
        uint8_t* outPtrLeft = dst_left + outRow * OUT_ROW_BYTES;
        uint8_t* outPtrRight = dst_right + outRow * OUT_ROW_BYTES;
        const uint8_t* srcPtr = src + srcCol;

        // Process left panel
        for (int i = 0; i < OUT_ROW_BYTES; i++) {
            uint8_t p0 = srcPtr[0] & 0x07;
            uint8_t p1 = srcPtr[WIDTH] & 0x07;
            outPtrLeft[i] = (p0 << 4) | p1;
            srcPtr += WIDTH * 2;
        }

        // Process right panel
        for (int i = 0; i < OUT_ROW_BYTES; i++) {
            uint8_t p0 = srcPtr[0] & 0x07;
            uint8_t p1 = srcPtr[WIDTH] & 0x07;
            outPtrRight[i] = (p0 << 4) | p1;
            srcPtr += WIDTH * 2;
        }
    }

    s_last_operation_us = micros() - start_us;
    Serial.printf("[IMAGE_HAL] E-ink rotate+pack: %lu us (software)\n", s_last_operation_us);
    return true;
}

void hal_image_get_stats(uint32_t* operation_time_us, bool* hw_accelerated) {
    if (operation_time_us) *operation_time_us = s_last_operation_us;
    if (hw_accelerated) *hw_accelerated = false;
}

#endif // ESP32 non-P4

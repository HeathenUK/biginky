/**
 * @file image_hal_esp32p4.cpp
 * @brief Image processing HAL implementation for ESP32-P4 using PPA
 * 
 * Uses the Pixel Processing Accelerator (PPA) for hardware-accelerated:
 * - Image rotation
 * - Color format conversion
 */

// Check for ESP32-P4 with PPA support
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#include "soc/soc_caps.h"
#endif

#if defined(SOC_PPA_SUPPORTED) && SOC_PPA_SUPPORTED

#include "image_hal.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <stdint.h>

// ESP-IDF PPA driver
#include "driver/ppa.h"
#include "hal/ppa_types.h"

// Cache line size for alignment requirements
// Use CONFIG_CACHE_L2_CACHE_LINE_SIZE from sdkconfig if available, otherwise default to 64
// PPA requires both buffer address and buffer_size to be aligned to cache line boundaries
#ifdef CONFIG_CACHE_L2_CACHE_LINE_SIZE
#define PPA_CACHE_LINE_SIZE CONFIG_CACHE_L2_CACHE_LINE_SIZE
#else
#define PPA_CACHE_LINE_SIZE 64  // Fallback: ESP32-P4 default is 64 bytes
#endif

// Helper macro to align size to cache line boundaries
#define ALIGN_TO_CACHE_LINE(size) (((size) + (PPA_CACHE_LINE_SIZE - 1)) & ~(PPA_CACHE_LINE_SIZE - 1))

// PPA client handles
static ppa_client_handle_t s_ppa_srm_client = nullptr;
static bool s_ppa_initialized = false;
static uint32_t s_last_operation_us = 0;
static bool s_last_hw_accel = false;

// ============================================================================
// Initialization
// ============================================================================

bool hal_image_init(void) {
    if (s_ppa_initialized) {
        return true;
    }

    // Register PPA client for Scale-Rotate-Mirror operations
    ppa_client_config_t srm_config = {
        .oper_type = PPA_OPERATION_SRM,
        .max_pending_trans_num = 1,
        .data_burst_length = PPA_DATA_BURST_LENGTH_128,
    };

    esp_err_t ret = ppa_register_client(&srm_config, &s_ppa_srm_client);
    if (ret != ESP_OK) {
        Serial.printf("[IMAGE_HAL] Failed to register PPA SRM client: %d\n", ret);
        return false;
    }

    s_ppa_initialized = true;
    Serial.println("[IMAGE_HAL] ESP32-P4 PPA initialized successfully");
    return true;
}

void hal_image_deinit(void) {
    if (s_ppa_srm_client) {
        ppa_unregister_client(s_ppa_srm_client);
        s_ppa_srm_client = nullptr;
    }
    s_ppa_initialized = false;
}

bool hal_image_hw_accel_available(void) {
    return s_ppa_initialized && (s_ppa_srm_client != nullptr);
}

// ============================================================================
// PPA-accelerated rotation
// ============================================================================

bool hal_image_rotate(const image_desc_t* src, image_desc_t* dst, 
                      image_rotation_t rotation, bool blocking) {
    if (!src || !dst || !src->buffer || !dst->buffer) {
        return false;
    }

    uint32_t start_us = micros();

    // Check if we can use PPA
    if (!hal_image_hw_accel_available()) {
        // Fall back to software
        s_last_hw_accel = false;
        // TODO: Implement software fallback
        Serial.println("[IMAGE_HAL] PPA not available, software fallback not implemented");
        return false;
    }

    // Map our format to PPA format
    ppa_srm_color_mode_t ppa_color_mode;
    switch (src->format) {
        case IMAGE_FORMAT_L8:
            // PPA doesn't directly support L8, but we can use ARGB8888 with packed data
            // For now, fall back to software for L8
            s_last_hw_accel = false;
            Serial.println("[IMAGE_HAL] L8 format requires software path");
            return false;
        case IMAGE_FORMAT_RGB565:
            ppa_color_mode = PPA_SRM_COLOR_MODE_RGB565;
            break;
        case IMAGE_FORMAT_RGB888:
            ppa_color_mode = PPA_SRM_COLOR_MODE_RGB888;
            break;
        case IMAGE_FORMAT_ARGB8888:
            ppa_color_mode = PPA_SRM_COLOR_MODE_ARGB8888;
            break;
        default:
            return false;
    }

    // Map rotation angle
    ppa_srm_rotation_angle_t ppa_rotation;
    switch (rotation) {
        case IMAGE_ROTATE_0:
            ppa_rotation = PPA_SRM_ROTATION_ANGLE_0;
            break;
        case IMAGE_ROTATE_90:
            ppa_rotation = PPA_SRM_ROTATION_ANGLE_90;
            break;
        case IMAGE_ROTATE_180:
            ppa_rotation = PPA_SRM_ROTATION_ANGLE_180;
            break;
        case IMAGE_ROTATE_270:
            ppa_rotation = PPA_SRM_ROTATION_ANGLE_270;
            break;
        default:
            return false;
    }

    // Verify input buffer address is aligned (PPA requirement)
    if (((uintptr_t)src->buffer) & (PPA_CACHE_LINE_SIZE - 1)) {
        Serial.printf("[IMAGE_HAL] ERROR: Input buffer address %p is not %d-byte aligned!\n", 
                     src->buffer, PPA_CACHE_LINE_SIZE);
        s_last_hw_accel = false;
        return false;
    }

    // Configure input picture
    ppa_in_pic_blk_config_t in_config = {
        .buffer = src->buffer,
        .pic_w = src->width,
        .pic_h = src->height,
        .block_w = src->width,
        .block_h = src->height,
        .block_offset_x = 0,
        .block_offset_y = 0,
        .srm_cm = ppa_color_mode,
    };

    // Calculate output dimensions based on rotation
    uint32_t out_w = (rotation == IMAGE_ROTATE_90 || rotation == IMAGE_ROTATE_270) 
                     ? src->height : src->width;
    uint32_t out_h = (rotation == IMAGE_ROTATE_90 || rotation == IMAGE_ROTATE_270) 
                     ? src->width : src->height;

    // Configure output picture
    size_t bytes_per_pixel = (src->format == IMAGE_FORMAT_ARGB8888) ? 4 :
                             (src->format == IMAGE_FORMAT_RGB888) ? 3 :
                             (src->format == IMAGE_FORMAT_RGB565) ? 2 : 1;
    size_t out_buffer_size = out_w * out_h * bytes_per_pixel;
    // PPA requires buffer_size to be aligned to cache line boundaries
    out_buffer_size = ALIGN_TO_CACHE_LINE(out_buffer_size);
    
    // Verify output buffer address is aligned (PPA requirement)
    if (((uintptr_t)dst->buffer) & (PPA_CACHE_LINE_SIZE - 1)) {
        Serial.printf("[IMAGE_HAL] ERROR: Output buffer address %p is not %d-byte aligned!\n", 
                     dst->buffer, PPA_CACHE_LINE_SIZE);
        s_last_hw_accel = false;
        return false;
    }
    
    // Verify buffer_size is aligned (PPA requirement)  
    if (out_buffer_size & (PPA_CACHE_LINE_SIZE - 1)) {
        Serial.printf("[IMAGE_HAL] ERROR: Output buffer_size %zu is not %d-byte aligned!\n", 
                     out_buffer_size, PPA_CACHE_LINE_SIZE);
        s_last_hw_accel = false;
        return false;
    }

    ppa_out_pic_blk_config_t out_config = {
        .buffer = dst->buffer,
        .buffer_size = out_buffer_size,
        .pic_w = out_w,
        .pic_h = out_h,
        .block_offset_x = 0,
        .block_offset_y = 0,
        .srm_cm = ppa_color_mode,
    };

    // Configure SRM operation
    ppa_srm_oper_config_t srm_config = {
        .in = in_config,
        .out = out_config,
        .rotation_angle = ppa_rotation,
        .scale_x = 1.0f,
        .scale_y = 1.0f,
        .mirror_x = false,
        .mirror_y = false,
        .rgb_swap = false,
        .byte_swap = false,
        .mode = blocking ? PPA_TRANS_MODE_BLOCKING : PPA_TRANS_MODE_NON_BLOCKING,
        .user_data = nullptr,
    };

    // Execute rotation
    esp_err_t ret = ppa_do_scale_rotate_mirror(s_ppa_srm_client, &srm_config);
    if (ret != ESP_OK) {
        Serial.printf("[IMAGE_HAL] PPA rotation failed: %d\n", ret);
        s_last_hw_accel = false;
        return false;
    }

    s_last_operation_us = micros() - start_us;
    s_last_hw_accel = true;
    return true;
}

// ============================================================================
// PPA-accelerated scaling
// ============================================================================

bool hal_image_scale(const image_desc_t* src, image_desc_t* dst, bool blocking) {
    if (!src || !dst || !src->buffer || !dst->buffer) {
        return false;
    }

    uint32_t start_us = micros();

    // Check if we can use PPA
    if (!hal_image_hw_accel_available()) {
        s_last_hw_accel = false;
        Serial.println("[IMAGE_HAL] PPA not available for scaling, software fallback not implemented");
        return false;
    }

    // Map our format to PPA format
    ppa_srm_color_mode_t ppa_color_mode;
    switch (src->format) {
        case IMAGE_FORMAT_L8:
            s_last_hw_accel = false;
            Serial.println("[IMAGE_HAL] L8 format requires software path for scaling");
            return false;
        case IMAGE_FORMAT_RGB565:
            ppa_color_mode = PPA_SRM_COLOR_MODE_RGB565;
            break;
        case IMAGE_FORMAT_RGB888:
            ppa_color_mode = PPA_SRM_COLOR_MODE_RGB888;
            break;
        case IMAGE_FORMAT_ARGB8888:
            ppa_color_mode = PPA_SRM_COLOR_MODE_ARGB8888;
            break;
        default:
            return false;
    }

    // Calculate scale factors
    float scale_x = (float)dst->width / (float)src->width;
    float scale_y = (float)dst->height / (float)src->height;

    // Configure input picture
    ppa_in_pic_blk_config_t in_config = {
        .buffer = src->buffer,
        .pic_w = src->width,
        .pic_h = src->height,
        .block_w = src->width,
        .block_h = src->height,
        .block_offset_x = 0,
        .block_offset_y = 0,
        .srm_cm = ppa_color_mode,
    };

    // Configure output picture
    size_t bytes_per_pixel = (src->format == IMAGE_FORMAT_ARGB8888) ? 4 :
                             (src->format == IMAGE_FORMAT_RGB888) ? 3 :
                             (src->format == IMAGE_FORMAT_RGB565) ? 2 : 1;
    size_t out_buffer_size = dst->width * dst->height * bytes_per_pixel;
    // PPA requires buffer_size to be aligned to cache line boundaries
    out_buffer_size = ALIGN_TO_CACHE_LINE(out_buffer_size);

    ppa_out_pic_blk_config_t out_config = {
        .buffer = dst->buffer,
        .buffer_size = out_buffer_size,
        .pic_w = dst->width,
        .pic_h = dst->height,
        .block_offset_x = 0,
        .block_offset_y = 0,
        .srm_cm = ppa_color_mode,
    };

    // Configure SRM operation (scale only, no rotation)
    ppa_srm_oper_config_t srm_config = {
        .in = in_config,
        .out = out_config,
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = scale_x,
        .scale_y = scale_y,
        .mirror_x = false,
        .mirror_y = false,
        .rgb_swap = false,
        .byte_swap = false,
        .mode = blocking ? PPA_TRANS_MODE_BLOCKING : PPA_TRANS_MODE_NON_BLOCKING,
        .user_data = nullptr,
    };

    // Execute scaling
    esp_err_t ret = ppa_do_scale_rotate_mirror(s_ppa_srm_client, &srm_config);
    if (ret != ESP_OK) {
        Serial.printf("[IMAGE_HAL] PPA scaling failed: %d\n", ret);
        s_last_hw_accel = false;
        return false;
    }

    s_last_operation_us = micros() - start_us;
    s_last_hw_accel = true;
    return true;
}

void hal_image_wait(void) {
    // For blocking mode, operation is already complete
    // For non-blocking, PPA provides callback mechanism
    // Currently we only support blocking mode
}

// ============================================================================
// Specialized e-ink rotation + packing
// ============================================================================

/**
 * For the EL133UF1 display, we need to:
 * 1. Rotate 90° CCW (1600x1200 -> 1200x1600)
 * 2. Pack 2 pixels per byte (3-bit color -> 4-bit nibbles)
 * 3. Split into left (cols 0-599) and right (cols 600-1199) panels
 * 
 * PPA can do the rotation, but packing and splitting require CPU.
 * 
 * Strategy:
 * - Use PPA for RGB565/RGB888 rotation if input is in that format
 * - For L8 (current e-ink format), use optimized software path
 *   since PPA doesn't support L8 directly
 * 
 * Future optimization: Store framebuffer as RGB565, use PPA to rotate,
 * then pack to 3-bit in a single pass during SPI transfer.
 */
bool hal_image_rotate_pack_eink(const uint8_t* src, 
                                 uint8_t* dst_left, uint8_t* dst_right,
                                 bool blocking) {
    if (!src || !dst_left || !dst_right) {
        return false;
    }

    uint32_t start_us = micros();

    // Current implementation: optimized software path
    // The e-ink buffer is L8 (1 byte per pixel, 3-bit color)
    // PPA doesn't support L8, so we use SIMD-optimized software
    
    // Constants for EL133UF1
    const int WIDTH = 1600;
    const int HEIGHT = 1200;
    const int PANEL_COLS = 600;
    const int OUT_ROW_BYTES = 300;  // 600 pixels / 2 pixels per byte
    
    // Rotate 90° CCW and pack
    // Input:  row y, col x at buffer[y * WIDTH + x]
    // Output: row (WIDTH-1-x), col y at output[(WIDTH-1-x) * OUT_ROW_BYTES + y/2]
    // Split:  cols 0-599 -> dst_left, cols 600-1199 -> dst_right
    
    for (int srcCol = WIDTH - 1; srcCol >= 0; srcCol--) {
        int outRow = WIDTH - 1 - srcCol;
        uint8_t* outPtrLeft = dst_left + outRow * OUT_ROW_BYTES;
        uint8_t* outPtrRight = dst_right + outRow * OUT_ROW_BYTES;
        const uint8_t* srcPtr = src + srcCol;
        
        // Process left panel (source rows 0-599)
        for (int i = 0; i < OUT_ROW_BYTES; i++) {
            uint8_t p0 = srcPtr[0] & 0x07;
            uint8_t p1 = srcPtr[WIDTH] & 0x07;
            outPtrLeft[i] = (p0 << 4) | p1;
            srcPtr += WIDTH * 2;
        }
        
        // Process right panel (source rows 600-1199)
        for (int i = 0; i < OUT_ROW_BYTES; i++) {
            uint8_t p0 = srcPtr[0] & 0x07;
            uint8_t p1 = srcPtr[WIDTH] & 0x07;
            outPtrRight[i] = (p0 << 4) | p1;
            srcPtr += WIDTH * 2;
        }
    }

    s_last_operation_us = micros() - start_us;
    s_last_hw_accel = false;  // Software path used
    
    Serial.printf("[IMAGE_HAL] E-ink rotate+pack: %lu us (software)\n", s_last_operation_us);
    return true;
}

void hal_image_get_stats(uint32_t* operation_time_us, bool* hw_accelerated) {
    if (operation_time_us) *operation_time_us = s_last_operation_us;
    if (hw_accelerated) *hw_accelerated = s_last_hw_accel;
}

#endif // SOC_PPA_SUPPORTED

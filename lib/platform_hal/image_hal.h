/**
 * @file image_hal.h
 * @brief Hardware Abstraction Layer for image processing acceleration
 * 
 * Provides platform-independent interface for:
 * - Image rotation (90°, 180°, 270°)
 * - Image scaling
 * - Color space conversion
 * 
 * On ESP32-P4: Uses PPA (Pixel Processing Accelerator) hardware
 */

#ifndef IMAGE_HAL_H
#define IMAGE_HAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Supported image color formats
 */
typedef enum {
    IMAGE_FORMAT_L8,        // 8-bit grayscale (1 byte per pixel)
    IMAGE_FORMAT_RGB565,    // 16-bit RGB (2 bytes per pixel)
    IMAGE_FORMAT_RGB888,    // 24-bit RGB (3 bytes per pixel)
    IMAGE_FORMAT_ARGB8888,  // 32-bit ARGB (4 bytes per pixel)
} image_format_t;

/**
 * @brief Rotation angles
 */
typedef enum {
    IMAGE_ROTATE_0 = 0,
    IMAGE_ROTATE_90 = 90,
    IMAGE_ROTATE_180 = 180,
    IMAGE_ROTATE_270 = 270,
} image_rotation_t;

/**
 * @brief Image descriptor
 */
typedef struct {
    void* buffer;           // Pointer to pixel data
    uint32_t width;         // Image width in pixels
    uint32_t height;        // Image height in pixels
    uint32_t stride;        // Bytes per row (may include padding)
    image_format_t format;  // Pixel format
} image_desc_t;

/**
 * @brief Initialize the image processing HAL
 * 
 * On ESP32-P4: Initializes PPA hardware
 * 
 * @return true if initialization successful
 */
bool hal_image_init(void);

/**
 * @brief Deinitialize the image processing HAL
 */
void hal_image_deinit(void);

/**
 * @brief Check if hardware acceleration is available
 * 
 * @return true if hardware acceleration (PPA) is available
 */
bool hal_image_hw_accel_available(void);

/**
 * @brief Rotate an image
 * 
 * Rotates the source image and writes to destination buffer.
 * Source and destination must not overlap.
 * 
 * On ESP32-P4: Uses PPA SRM (Scale-Rotate-Mirror) engine
 * 
 * @param src Source image descriptor
 * @param dst Destination image descriptor (dimensions should match rotated size)
 * @param rotation Rotation angle (90, 180, or 270 degrees)
 * @param blocking If true, wait for operation to complete before returning
 * 
 * @return true if rotation started/completed successfully
 */
bool hal_image_rotate(const image_desc_t* src, image_desc_t* dst, 
                      image_rotation_t rotation, bool blocking);

/**
 * @brief Scale an image
 * 
 * Scales the source image and writes to destination buffer.
 * Source and destination must not overlap.
 * 
 * On ESP32-P4: Uses PPA SRM (Scale-Rotate-Mirror) engine
 * 
 * @param src Source image descriptor
 * @param dst Destination image descriptor (dimensions should match scaled size)
 * @param blocking If true, wait for operation to complete before returning
 * 
 * @return true if scaling started/completed successfully
 */
bool hal_image_scale(const image_desc_t* src, image_desc_t* dst, bool blocking);

/**
 * @brief Wait for pending image operation to complete
 * 
 * Only needed if hal_image_rotate or hal_image_scale was called with blocking=false
 */
void hal_image_wait(void);

/**
 * @brief Rotate and pack image for e-ink display
 * 
 * Specialized function for EL133UF1 display:
 * - Rotates 90° CCW
 * - Packs 2 pixels per byte (4-bit color)
 * - Splits into left/right panel buffers
 * 
 * @param src Source buffer (1600x1200, 1 byte per pixel, 3-bit color in low bits)
 * @param dst_left Output buffer for left panel (1600x600 packed = 480000 bytes)
 * @param dst_right Output buffer for right panel (1600x600 packed = 480000 bytes)
 * @param blocking If true, wait for operation to complete
 * 
 * @return true if operation started/completed successfully
 */
bool hal_image_rotate_pack_eink(const uint8_t* src, 
                                 uint8_t* dst_left, uint8_t* dst_right,
                                 bool blocking);

/**
 * @brief Get performance statistics for last operation
 * 
 * @param operation_time_us Output: time taken in microseconds
 * @param hw_accelerated Output: true if hardware acceleration was used
 */
void hal_image_get_stats(uint32_t* operation_time_us, bool* hw_accelerated);

#ifdef __cplusplus
}
#endif

#endif // IMAGE_HAL_H

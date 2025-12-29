/**
 * @file platform_hal.h
 * @brief Platform Hardware Abstraction Layer
 * 
 * This file provides a unified interface for platform-specific functionality
 * for ESP32-P4.
 * 
 * Abstracts:
 * - PSRAM allocation
 * - Memory statistics
 * - DMA operations (optional)
 * - Platform identification
 */

#ifndef PLATFORM_HAL_H
#define PLATFORM_HAL_H

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

// ============================================================================
// Platform Detection
// ============================================================================

#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
    #if defined(CONFIG_IDF_TARGET_ESP32P4)
        #define PLATFORM_ESP32P4 1
        #define PLATFORM_NAME "ESP32-P4"
    #else
        #define PLATFORM_ESP32 1
        #define PLATFORM_NAME "ESP32"
    #endif
#else
    #define PLATFORM_UNKNOWN 1
    #define PLATFORM_NAME "Unknown"
    #warning "Unknown platform - ESP32-P4 expected"
#endif

// ============================================================================
// PSRAM Memory Allocation
// ============================================================================

/**
 * @brief Allocate memory from PSRAM (external RAM)
 * 
 * On platforms with PSRAM, this allocates from external RAM.
 * Falls back to regular heap if PSRAM is not available.
 * 
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or nullptr on failure
 */
void* hal_psram_malloc(size_t size);

/**
 * @brief Free memory allocated with hal_psram_malloc
 * 
 * @param ptr Pointer to memory to free
 */
void hal_psram_free(void* ptr);

/**
 * @brief Get total PSRAM size in bytes
 * 
 * @return Total PSRAM size, or 0 if no PSRAM
 */
size_t hal_psram_get_size(void);

/**
 * @brief Get free PSRAM in bytes
 * 
 * @return Free PSRAM, or 0 if no PSRAM
 */
size_t hal_psram_get_free(void);

/**
 * @brief Check if PSRAM is available
 * 
 * @return true if PSRAM is detected and usable
 */
bool hal_psram_available(void);

// ============================================================================
// General Memory Statistics
// ============================================================================

/**
 * @brief Get total heap size
 * 
 * @return Total heap in bytes
 */
size_t hal_heap_get_total(void);

/**
 * @brief Get free heap
 * 
 * @return Free heap in bytes
 */
size_t hal_heap_get_free(void);

// ============================================================================
// DMA Operations (Optional)
// ============================================================================

/**
 * @brief Initialize DMA subsystem
 * 
 * Call once at startup if DMA is needed.
 * Safe to call multiple times.
 * 
 * @return true if DMA is available
 */
bool hal_dma_init(void);

/**
 * @brief Perform DMA memory copy (blocking)
 * 
 * Falls back to memcpy if DMA is not available.
 * 
 * @param dst Destination address
 * @param src Source address  
 * @param size Number of bytes to copy
 */
void hal_dma_memcpy(void* dst, const void* src, size_t size);

/**
 * @brief Start async DMA memory copy
 * 
 * Returns immediately. Use hal_dma_wait() to wait for completion.
 * Falls back to synchronous memcpy if DMA is not available.
 * 
 * @param dst Destination address
 * @param src Source address
 * @param size Number of bytes to copy
 */
void hal_dma_memcpy_start(void* dst, const void* src, size_t size);

/**
 * @brief Wait for async DMA to complete
 */
void hal_dma_wait(void);

/**
 * @brief Check if DMA is available
 * 
 * @return true if DMA can be used
 */
bool hal_dma_available(void);

// ============================================================================
// Platform Info
// ============================================================================

/**
 * @brief Get CPU frequency in Hz
 * 
 * @return CPU frequency
 */
uint32_t hal_get_cpu_freq(void);

/**
 * @brief Get platform name string
 * 
 * @return Platform name (e.g., "RP2350", "ESP32-P4")
 */
const char* hal_get_platform_name(void);

/**
 * @brief Print platform diagnostic info to Serial
 */
void hal_print_info(void);

// ============================================================================
// Compatibility Macros
// ============================================================================

// Legacy pmalloc compatibility (used by existing code)
#ifndef pmalloc
#define pmalloc(size) hal_psram_malloc(size)
#endif

#endif // PLATFORM_HAL_H

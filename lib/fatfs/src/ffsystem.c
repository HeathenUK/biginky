/**
 * @file ffsystem.c
 * @brief FatFS SRAM buffer optimization for ESP32
 * 
 * CRITICAL PERFORMANCE NOTE:
 * This provides a linker-wrapped ff_memalloc() that uses internal SRAM
 * instead of PSRAM for ~10x better SD card I/O performance.
 * 
 * Requires: -Wl,--wrap=ff_memalloc in build flags
 */

#ifdef ESP_PLATFORM

#include <stdio.h>
#include "esp_heap_caps.h"

/**
 * @brief Wrapped ff_memalloc - allocates from internal SRAM
 * 
 * When -Wl,--wrap=ff_memalloc is used, all calls to ff_memalloc() are
 * redirected here. This forces FatFS buffers into fast internal SRAM
 * instead of slow PSRAM.
 * 
 * Expected performance:
 *   - SRAM buffers: 8-15 MB/s SD read speed
 *   - PSRAM buffers: 1-2 MB/s (default ESP-IDF behavior)
 */
void* __wrap_ff_memalloc(unsigned int msize)
{
    // Try DMA-capable internal memory first (best for SD transfers)
    void* ptr = heap_caps_malloc(msize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
    
    // Fall back to any internal memory
    if (ptr == NULL) {
        ptr = heap_caps_malloc(msize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    
    // Log once to confirm patch is active
    static int alloc_count = 0;
    if (alloc_count < 3) {
        alloc_count++;
        printf("[SRAM] ff_memalloc(%u) = %p\n", msize, ptr);
    }
    
    return ptr;
}

#endif /* ESP_PLATFORM */

/**
 * @file ffsystem.c
 * @brief FatFS system-dependent functions for ESP32
 * 
 * This file provides memory allocation and synchronization functions
 * required by FatFS.
 * 
 * CRITICAL PERFORMANCE NOTE:
 * ff_memalloc() explicitly uses internal SRAM (not PSRAM) for buffers.
 * Using PSRAM for filesystem buffers causes ~10x performance degradation
 * due to external memory bus latency. With 4KB sector buffers and typical
 * max_files=5, this uses ~20-25KB of internal SRAM - a worthwhile tradeoff
 * for the massive I/O speed improvement.
 */

#include "ff.h"
#include "ffconf.h"
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/**
 * @brief Allocate memory block for FatFS
 * 
 * Uses internal SRAM (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) instead of PSRAM
 * for maximum I/O performance. PSRAM buffers cause ~10x slowdown due to
 * external memory latency.
 * 
 * @param msize Size of memory block to allocate
 * @return Pointer to allocated memory, or NULL if allocation failed
 */
void* ff_memalloc(UINT msize)
{
    // Use internal SRAM for ~10x better SD card performance vs PSRAM
    // MALLOC_CAP_INTERNAL: Use internal RAM only (not PSRAM)
    // MALLOC_CAP_8BIT: Byte-accessible memory (required for general buffers)
    // MALLOC_CAP_DMA: DMA-capable memory (good for SD transfers)
    void* ptr = heap_caps_malloc(msize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
    
    // If internal DMA memory not available, try internal without DMA requirement
    if (ptr == NULL) {
        ptr = heap_caps_malloc(msize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    
    return ptr;
}

/**
 * @brief Free memory block
 * @param mblock Pointer to memory block to free
 */
void ff_memfree(void* mblock)
{
    free(mblock);
}

#if FF_FS_REENTRANT
/**
 * @brief Create a sync object (mutex)
 * @param vol Volume number
 * @param sobj Pointer to sync object
 * @return 1 on success, 0 on failure
 */
int ff_mutex_create(int vol, FF_SYNC_t* sobj)
{
    *sobj = xSemaphoreCreateMutex();
    return (*sobj != NULL) ? 1 : 0;
}

/**
 * @brief Delete a sync object
 * @param sobj Sync object to delete
 * @return 1 on success
 */
int ff_mutex_delete(FF_SYNC_t sobj)
{
    vSemaphoreDelete(sobj);
    return 1;
}

/**
 * @brief Request a sync object (lock mutex)
 * @param sobj Sync object
 * @return 1 on success, 0 on timeout
 */
int ff_mutex_take(FF_SYNC_t sobj)
{
    return (xSemaphoreTake(sobj, FF_FS_TIMEOUT) == pdTRUE) ? 1 : 0;
}

/**
 * @brief Release a sync object (unlock mutex)
 * @param sobj Sync object
 */
void ff_mutex_give(FF_SYNC_t sobj)
{
    xSemaphoreGive(sobj);
}
#endif /* FF_FS_REENTRANT */

#else /* !ESP_PLATFORM - Generic implementation */

void* ff_memalloc(UINT msize)
{
    return malloc(msize);
}

void ff_memfree(void* mblock)
{
    free(mblock);
}

#endif /* ESP_PLATFORM */

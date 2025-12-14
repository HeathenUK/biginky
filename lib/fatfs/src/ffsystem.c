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
/* Array of mutexes for each volume (ESP-IDF style API uses volume index) */
static SemaphoreHandle_t s_fatfs_mutex[FF_VOLUMES];

/**
 * @brief Create a sync object (mutex) for a volume
 * @param vol Volume number
 * @return 1 on success, 0 on failure
 */
int ff_mutex_create(int vol)
{
    if (vol < 0 || vol >= FF_VOLUMES) return 0;
    s_fatfs_mutex[vol] = xSemaphoreCreateMutex();
    return (s_fatfs_mutex[vol] != NULL) ? 1 : 0;
}

/**
 * @brief Delete a sync object for a volume
 * @param vol Volume number
 */
void ff_mutex_delete(int vol)
{
    if (vol >= 0 && vol < FF_VOLUMES && s_fatfs_mutex[vol] != NULL) {
        vSemaphoreDelete(s_fatfs_mutex[vol]);
        s_fatfs_mutex[vol] = NULL;
    }
}

/**
 * @brief Lock sync object for a volume
 * @param vol Volume number
 * @return 1 on success, 0 on timeout
 */
int ff_mutex_take(int vol)
{
    if (vol < 0 || vol >= FF_VOLUMES || s_fatfs_mutex[vol] == NULL) return 0;
    return (xSemaphoreTake(s_fatfs_mutex[vol], FF_FS_TIMEOUT) == pdTRUE) ? 1 : 0;
}

/**
 * @brief Unlock sync object for a volume
 * @param vol Volume number
 */
void ff_mutex_give(int vol)
{
    if (vol >= 0 && vol < FF_VOLUMES && s_fatfs_mutex[vol] != NULL) {
        xSemaphoreGive(s_fatfs_mutex[vol]);
    }
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

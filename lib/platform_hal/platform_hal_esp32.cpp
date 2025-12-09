/**
 * @file platform_hal_esp32.cpp
 * @brief Platform HAL implementation for ESP32 family (including ESP32-P4)
 */

#include "platform_hal.h"

#if defined(PLATFORM_ESP32) || defined(PLATFORM_ESP32P4)

#include <esp_heap_caps.h>
#include <esp_system.h>

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include <esp_chip_info.h>
#endif

// ============================================================================
// PSRAM Memory Allocation
// ============================================================================

void* hal_psram_malloc(size_t size) {
    // Try SPIRAM first (PSRAM)
    void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    
    if (ptr == nullptr) {
        // Fallback to regular heap
        Serial.printf("[HAL] PSRAM alloc failed for %u bytes, trying heap\n", size);
        ptr = malloc(size);
    }
    
    return ptr;
}

void hal_psram_free(void* ptr) {
    // heap_caps_free works for any allocation
    if (ptr != nullptr) {
        heap_caps_free(ptr);
    }
}

size_t hal_psram_get_size(void) {
    return heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
}

size_t hal_psram_get_free(void) {
    return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

bool hal_psram_available(void) {
    return heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0;
}

// ============================================================================
// General Memory Statistics
// ============================================================================

size_t hal_heap_get_total(void) {
    // Return internal RAM total
    return heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

size_t hal_heap_get_free(void) {
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

// ============================================================================
// DMA Operations
// ============================================================================

// Note: ESP32 DMA is more complex (tied to peripherals like SPI, I2S)
// For memory-to-memory, we use memcpy which is optimized for ESP32

static bool _dma_initialized = false;

bool hal_dma_init(void) {
    // ESP32 doesn't have general-purpose memory DMA like RP2350
    // Return true as "initialized" but operations will use memcpy
    _dma_initialized = true;
    return true;
}

void hal_dma_memcpy(void* dst, const void* src, size_t size) {
    // ESP32's memcpy is well-optimized for PSRAM
    memcpy(dst, src, size);
}

void hal_dma_memcpy_start(void* dst, const void* src, size_t size) {
    // No async DMA for memory-to-memory on ESP32
    // Just do synchronous copy
    memcpy(dst, src, size);
}

void hal_dma_wait(void) {
    // Nothing to wait for - copies are synchronous
}

bool hal_dma_available(void) {
    // Return false since we're using memcpy, not true DMA
    return false;
}

// ============================================================================
// Platform Info
// ============================================================================

uint32_t hal_get_cpu_freq(void) {
    return getCpuFrequencyMhz() * 1000000;
}

const char* hal_get_platform_name(void) {
    return PLATFORM_NAME;
}

void hal_print_info(void) {
    Serial.println("=== Platform Info (ESP32) ===");
    
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    const char* chip_name = "ESP32";
    switch (chip_info.model) {
        case CHIP_ESP32:   chip_name = "ESP32"; break;
        case CHIP_ESP32S2: chip_name = "ESP32-S2"; break;
        case CHIP_ESP32S3: chip_name = "ESP32-S3"; break;
        case CHIP_ESP32C3: chip_name = "ESP32-C3"; break;
        case CHIP_ESP32C2: chip_name = "ESP32-C2"; break;
        case CHIP_ESP32C6: chip_name = "ESP32-C6"; break;
        case CHIP_ESP32H2: chip_name = "ESP32-H2"; break;
#if defined(CHIP_ESP32P4)
        case CHIP_ESP32P4: chip_name = "ESP32-P4"; break;
#endif
        default: chip_name = "ESP32-Unknown"; break;
    }
    
    Serial.printf("  Chip Model:     %s\n", chip_name);
    Serial.printf("  Cores:          %d\n", chip_info.cores);
    Serial.printf("  Revision:       %d\n", chip_info.revision);
#endif

    Serial.printf("  CPU Frequency:  %lu MHz\n", hal_get_cpu_freq() / 1000000);
    Serial.printf("  Internal Heap:  %lu KB total, %lu KB free\n", 
                  hal_heap_get_total() / 1024, hal_heap_get_free() / 1024);
    
    if (hal_psram_available()) {
        Serial.printf("  PSRAM:          %lu KB total, %lu KB free\n",
                      hal_psram_get_size() / 1024, hal_psram_get_free() / 1024);
    } else {
        Serial.println("  PSRAM:          Not detected");
    }
    
    Serial.printf("  DMA Available:  %s (uses optimized memcpy)\n", 
                  hal_dma_available() ? "Yes" : "No");
    Serial.println("==============================");
}

#endif // PLATFORM_ESP32 || PLATFORM_ESP32P4

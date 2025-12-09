/**
 * @file platform_hal_rp2350.cpp
 * @brief Platform HAL implementation for RP2350 (Raspberry Pi Pico 2)
 */

#include "platform_hal.h"

#ifdef PLATFORM_RP2350

#include "hardware/dma.h"
#include "hardware/sync.h"

// DMA channel for memory transfers (-1 = not initialized)
static int _dma_channel = -1;

// ============================================================================
// PSRAM Memory Allocation
// ============================================================================

void* hal_psram_malloc(size_t size) {
    // arduino-pico provides pmalloc() for PSRAM allocation
    extern void* pmalloc(size_t size);
    return pmalloc(size);
}

void hal_psram_free(void* ptr) {
    // Standard free works for both PSRAM and heap
    free(ptr);
}

size_t hal_psram_get_size(void) {
    return rp2040.getPSRAMSize();
}

size_t hal_psram_get_free(void) {
    // arduino-pico doesn't provide free PSRAM directly
    // Return total as approximation (most PSRAM is usually free)
    return rp2040.getPSRAMSize();
}

bool hal_psram_available(void) {
    return rp2040.getPSRAMSize() > 0;
}

// ============================================================================
// General Memory Statistics
// ============================================================================

size_t hal_heap_get_total(void) {
    return rp2040.getTotalHeap();
}

size_t hal_heap_get_free(void) {
    return rp2040.getFreeHeap();
}

// ============================================================================
// DMA Operations
// ============================================================================

bool hal_dma_init(void) {
    if (_dma_channel < 0) {
        _dma_channel = dma_claim_unused_channel(false);
        if (_dma_channel >= 0) {
            Serial.printf("[HAL] DMA channel %d claimed\n", _dma_channel);
            return true;
        }
        return false;
    }
    return true;  // Already initialized
}

void hal_dma_memcpy(void* dst, const void* src, size_t size) {
    hal_dma_memcpy_start(dst, src, size);
    hal_dma_wait();
}

void hal_dma_memcpy_start(void* dst, const void* src, size_t size) {
    if (_dma_channel < 0) {
        // Fallback to regular memcpy
        memcpy(dst, src, size);
        return;
    }
    
    dma_channel_config c = dma_channel_get_default_config(_dma_channel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);  // 32-bit transfers
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, true);
    
    dma_channel_configure(
        _dma_channel,
        &c,
        dst,           // Write address
        src,           // Read address
        size / 4,      // Transfer count (32-bit words)
        true           // Start immediately
    );
}

void hal_dma_wait(void) {
    if (_dma_channel >= 0) {
        dma_channel_wait_for_finish_blocking(_dma_channel);
    }
}

bool hal_dma_available(void) {
    return _dma_channel >= 0;
}

// ============================================================================
// Platform Info
// ============================================================================

uint32_t hal_get_cpu_freq(void) {
    return rp2040.f_cpu();
}

const char* hal_get_platform_name(void) {
    return PLATFORM_NAME;
}

void hal_print_info(void) {
    Serial.println("=== Platform Info (RP2350) ===");
    Serial.printf("  CPU Frequency:  %lu MHz\n", hal_get_cpu_freq() / 1000000);
    Serial.printf("  Total Heap:     %lu KB\n", hal_heap_get_total() / 1024);
    Serial.printf("  Free Heap:      %lu KB\n", hal_heap_get_free() / 1024);
    Serial.printf("  PSRAM Size:     %lu KB\n", hal_psram_get_size() / 1024);
    Serial.printf("  PSRAM Avail:    %s\n", hal_psram_available() ? "Yes" : "No");
    Serial.printf("  DMA Available:  %s\n", hal_dma_available() ? "Yes" : "No");
    Serial.println("==============================");
}

#endif // PLATFORM_RP2350

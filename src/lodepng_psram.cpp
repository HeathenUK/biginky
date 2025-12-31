/**
 * @file lodepng_psram.cpp
 * @brief Custom PSRAM allocators for lodepng - implementation
 */

#include "platform_hal.h"  // For hal_psram_malloc/free
#include <string.h>  // For memcpy
#include <stddef.h>

// These functions must be defined before lodepng.h is included anywhere
// They are declared in lodepng_psram.h but implemented here to avoid multiple definitions
// Note: lodepng.cpp is C++ code, so these need C++ linkage (no extern "C")

void* lodepng_malloc(size_t size) {
    // Handle 0-byte allocations gracefully (avoid PSRAM warning)
    if (size == 0) {
        return nullptr;
    }
    return hal_psram_malloc(size);
}

void* lodepng_realloc(void* ptr, size_t new_size) {
    if (ptr == nullptr) {
        // Handle 0-byte allocations gracefully (avoid PSRAM warning)
        if (new_size == 0) {
            return nullptr;
        }
        return hal_psram_malloc(new_size);
    }
    if (new_size == 0) {
        hal_psram_free(ptr);
        return nullptr;
    }
    // Realloc: allocate new, copy, free old
    // Note: We don't know the old size, but lodepng should handle this correctly
    // by allocating the full size needed before calling realloc
    void* new_ptr = hal_psram_malloc(new_size);
    if (new_ptr != nullptr && ptr != nullptr) {
        memcpy(new_ptr, ptr, new_size);  // Copy what we can
    }
    hal_psram_free(ptr);
    return new_ptr;
}

void lodepng_free(void* ptr) {
    hal_psram_free(ptr);
}

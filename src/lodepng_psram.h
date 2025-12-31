/**
 * @file lodepng_psram.h
 * @brief Custom PSRAM allocators for lodepng
 * 
 * This file declares custom memory allocators for lodepng to use PSRAM
 * instead of SRAM. Must be included before lodepng.h.
 */

#ifndef LODEPNG_PSRAM_H
#define LODEPNG_PSRAM_H

#include <stddef.h>

// Custom allocators for lodepng to use PSRAM
// Note: lodepng.cpp is C++ code, so these need C++ linkage
void* lodepng_malloc(size_t size);
void* lodepng_realloc(void* ptr, size_t new_size);
void lodepng_free(void* ptr);

#endif // LODEPNG_PSRAM_H

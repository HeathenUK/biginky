/**
 * @file chunked_processing.h
 * @brief Centralized chunked processing utilities with automatic watchdog yielding
 * 
 * Provides reusable chunked processing iterators that automatically yield to the
 * watchdog timer, preventing timeouts during long-running image/buffer operations.
 * 
 * Usage:
 *   // 2D image processing
 *   processImageChunked(width, height, [](int x, int y) {
 *       // Process pixel at (x, y)
 *   });
 * 
 *   // 1D buffer processing
 *   processBufferChunked(size, [](size_t i) {
 *       // Process element at index i
 *   });
 */

#ifndef CHUNKED_PROCESSING_H
#define CHUNKED_PROCESSING_H

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Default chunk sizes (pixels/elements per chunk before yielding)
// These are tuned to yield frequently enough to prevent watchdog timeouts
// while not adding too much overhead
#define CHUNKED_2D_YIELD_INTERVAL 50   // Yield every 50 rows
#define CHUNKED_1D_YIELD_INTERVAL 50000  // Yield every 50k elements

/**
 * Process a 2D image in chunks with automatic watchdog yielding
 * 
 * @param width Image width
 * @param height Image height
 * @param pixelFunc Function to call for each pixel: void(int x, int y)
 * @param yieldInterval Number of rows to process before yielding (default: 50)
 */
template<typename PixelFunc>
void processImageChunked(int width, int height, PixelFunc pixelFunc, int yieldInterval = CHUNKED_2D_YIELD_INTERVAL) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            pixelFunc(x, y);
        }
        // Yield to watchdog periodically to prevent timeout
        if ((y % yieldInterval) == 0 && y > 0) {
            vTaskDelay(1);
        }
    }
}

/**
 * Process a 1D buffer in chunks with automatic watchdog yielding
 * 
 * @param size Buffer size (number of elements)
 * @param elementFunc Function to call for each element: void(size_t index)
 * @param yieldInterval Number of elements to process before yielding (default: 50000)
 */
template<typename ElementFunc>
void processBufferChunked(size_t size, ElementFunc elementFunc, size_t yieldInterval = CHUNKED_1D_YIELD_INTERVAL) {
    for (size_t i = 0; i < size; i++) {
        elementFunc(i);
        // Yield to watchdog periodically to prevent timeout
        if ((i % yieldInterval) == 0 && i > 0) {
            vTaskDelay(1);
        }
    }
}

/**
 * Process a 2D image row-by-row with automatic watchdog yielding
 * Useful when you need to process entire rows at once
 * 
 * @param height Image height
 * @param rowFunc Function to call for each row: void(int y)
 * @param yieldInterval Number of rows to process before yielding (default: 50)
 */
template<typename RowFunc>
void processRowsChunked(int height, RowFunc rowFunc, int yieldInterval = CHUNKED_2D_YIELD_INTERVAL) {
    for (int y = 0; y < height; y++) {
        rowFunc(y);
        // Yield to watchdog periodically to prevent timeout
        if ((y % yieldInterval) == 0 && y > 0) {
            vTaskDelay(1);
        }
    }
}

#endif // CHUNKED_PROCESSING_H

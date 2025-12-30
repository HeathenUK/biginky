/**
 * @file canvas_handler.h
 * @brief Canvas display and save functionality
 * 
 * Provides functions for:
 * - Displaying canvas data on e-ink display
 * - Saving canvas to SD card as PNG
 * - Handling canvas commands (display, display+save, save only)
 * 
 * Extracted from main.cpp as part of modular refactoring.
 */

#ifndef CANVAS_HANDLER_H
#define CANVAS_HANDLER_H

#include <Arduino.h>

// Forward declarations
struct CanvasDecodeWorkData;
struct PngEncodeWorkData;

/**
 * Handle canvas_display command - display canvas on e-ink panel
 * @param messageToProcess JSON message with canvas data
 * @return true if successful, false otherwise
 */
bool handleCanvasDisplayCommand(const String& messageToProcess);

/**
 * Handle canvas_display_save command - display canvas and save to SD card
 * @param messageToProcess JSON message with canvas data
 * @return true if successful, false otherwise
 */
bool handleCanvasDisplaySaveCommand(const String& messageToProcess);

/**
 * Handle canvas_save command - save canvas to SD card without displaying
 * @param messageToProcess JSON message with canvas data
 * @return true if successful, false otherwise
 */
bool handleCanvasSaveCommand(const String& messageToProcess);

/**
 * Save canvas pixel data as PNG file to SD card
 * @param pixelData Pointer to pixel data (e-ink color indices)
 * @param pixelCount Number of pixels
 * @param width Image width
 * @param height Image height
 * @param filename Filename to save (will be sanitized)
 * @return true if successful, false otherwise
 */
bool saveCanvasAsPNG(const uint8_t* pixelData, size_t pixelCount, unsigned width, unsigned height, const String& filename);

#endif // CANVAS_HANDLER_H


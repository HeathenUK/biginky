/**
 * @file thumbnail_utils.h
 * @brief Thumbnail generation and SD card operations for thumbnails
 * 
 * Provides functions for:
 * - Loading thumbnails from SD card
 * - Generating thumbnails from image files
 * 
 * Extracted from main_esp32p4_test.cpp as part of Priority 1 refactoring.
 */

#ifndef THUMBNAIL_UTILS_H
#define THUMBNAIL_UTILS_H

#include <Arduino.h>
#include <vector>

/**
 * Load JPEG thumbnail from SD card and return JSON string
 * @return Pointer to JSON string (caller must free), or nullptr on error
 */
char* loadThumbnailFromSD();

/**
 * Generate a quarter-size JPEG thumbnail from an image file on SD card
 * @param imagePath Path to image file on SD card (e.g., "sunset.png")
 * @return Base64-encoded JPEG string, or empty string on error
 * 
 * Quarter size: 200x150 for 800x600 images, or 400x300 for 1600x1200 images
 */
String generateThumbnailFromImageFile(const String& imagePath);

/**
 * List all image files on SD card
 * @return Vector of image filenames (e.g., "sunset.png", "mountain.jpg")
 */
std::vector<String> listImageFilesVector();

#endif // THUMBNAIL_UTILS_H


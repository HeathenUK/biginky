/**
 * @file display_manager.h
 * @brief Display manager for unified media display with text overlay
 * 
 * Provides a single unified function for displaying media from mappings
 * with text overlay (time/date/weather/quote) and audio playback.
 * Used by top-of-hour cycles, !go commands, web UI, and HTTP API.
 */

#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>

/**
 * Unified function to display media from media mappings with text overlay and audio
 * This is the SINGLE function used by:
 * - Top-of-hour cycle (targetIndex = -1 for sequential)
 * - !go command (targetIndex = specific index)
 * - Web UI HTTP API (targetIndex = specific index)
 * - Web UI MQTT/GitHub Pages (targetIndex = specific index via dispatcher)
 * 
 * @param targetIndex Target media index (0-based), or -1 to use next sequential index
 * @param keepoutMargin Keepout margin for text placement (default 100)
 * @return true if successful, false otherwise
 */
bool displayMediaWithOverlay(int targetIndex = -1, int16_t keepoutMargin = 100);

// Forward declarations for display and TTF types
class EL133UF1;
class EL133UF1_TTF;

/**
 * Add text overlay (time/date/weather/quote) to an already-drawn display
 * Used by !show command and show_media_task for arbitrary image files
 * 
 * @param display Display instance
 * @param ttf TTF renderer instance
 * @param keepoutMargin Keepout margin for text placement (default 50: 25px left/right, 50px top/bottom)
 */
void addTextOverlayToDisplay(EL133UF1* display, EL133UF1_TTF* ttf, int16_t keepoutMargin = 50);

#endif // DISPLAY_MANAGER_H

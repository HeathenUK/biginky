# SD Card Configuration for ESP32-P4 E-Ink Display

This document describes how to configure quotes and audio playback using files on the SD card.

## Overview

The ESP32-P4 e-ink display can now load quotes and audio files from the SD card instead of using hard-coded values. This allows you to customize the display without recompiling the firmware.

## Features

### 1. Custom Quotes (`/quotes.txt`)

Load your own collection of quotes and authors from a text file on the SD card.

**File Format:**
```
Quote text goes here. It can span multiple lines if needed.
~Author Name

Another quote text here.
~Another Author

Third quote example.
~Third Author
```

**Rules:**
- Each quote consists of the quote text followed by the author on the next line
- Author lines must start with a tilde (`~`)
- Separate quotes with a blank line
- Multi-line quotes are automatically joined with spaces
- Empty lines between quotes are ignored

**Example `/quotes.txt`:**
```
The only impossible journey is the one you never begin.
~Tony Robbins

Success is not final, failure is not fatal: it is the courage to continue that counts.
~Winston Churchill

The future belongs to those who believe in the beauty of their dreams.
~Eleanor Roosevelt
```

### 2. Image-to-Audio Mappings (`/media.txt`)

Associate specific images with audio files that will play when the image is displayed.

**File Format:**
```
image1.png,audio1.wav
image2.png,audio2.wav
sunset.png,ocean_waves.wav
```

**Rules:**
- One mapping per line
- Format: `imagename.png,audiofile.wav`
- Image names are case-insensitive
- Lines starting with `#` are treated as comments
- Empty lines are ignored
- Audio files must be in WAV format (16-bit PCM, mono or stereo)

**Example `/media.txt`:**
```
# Image to audio mappings
sunset.png,ocean_waves.wav
forest.png,bird_song.wav
city.png,traffic.wav
mountain.png,wind.wav

# More mappings
beach.png,seagulls.wav
```

## Fallback Behavior

The system gracefully falls back to default behavior if configuration files are not present:

1. **Quotes**: If `/quotes.txt` is not found or empty, the system uses hard-coded quotes
2. **Audio**: If `/media.txt` is not found or no mapping exists for an image, a simple beep tone is played

## Audio Requirements

### WAV File Specifications

- **Format**: PCM (uncompressed)
- **Bit depth**: 16-bit
- **Sample rate**: 44100 Hz recommended (other rates supported)
- **Channels**: Mono or stereo (mono is automatically converted to stereo)
- **File size**: Keep under 1 MB for best performance

### Converting Audio Files

To convert audio files to the correct format, you can use `ffmpeg`:

```bash
# Convert any audio file to compatible WAV format
ffmpeg -i input.mp3 -acodec pcm_s16le -ar 44100 -ac 1 output.wav

# For stereo output:
ffmpeg -i input.mp3 -acodec pcm_s16le -ar 44100 -ac 2 output.wav
```

## Setting Up Your SD Card

1. **Format the SD card** as FAT32
2. **Create configuration files** in the root directory:
   - `/quotes.txt` (optional)
   - `/media.txt` (optional)
3. **Add your PNG images** to the root directory or subdirectories
4. **Add your WAV audio files** to the root directory or subdirectories
5. **Insert the SD card** into the ESP32-P4 device

## Example Directory Structure

```
/
├── quotes.txt
├── media.txt
├── sunset.png
├── forest.png
├── city.png
├── ocean_waves.wav
├── bird_song.wav
├── traffic.wav
└── audio/
    ├── seagulls.wav
    └── wind.wav
```

## Monitoring and Debugging

When the system boots or cycles through images, it will log to the serial console:

```
=== Loading quotes from SD card ===
  Found quotes.txt (1234 bytes)
  [1] "The only impossible journey..." - Tony Robbins
  [2] "Success is not final..." - Winston Churchill
  Loaded 2 quotes from SD card
=====================================

=== Loading media mappings from SD card ===
  Found media.txt (256 bytes)
  [1] sunset.png -> ocean_waves.wav
  [2] forest.png -> bird_song.wav
  Loaded 2 media mappings from SD card
============================================

Image sunset.png has audio mapping: ocean_waves.wav
=== Playing WAV: ocean_waves.wav ===
  Format: 1, Channels: 1, Rate: 44100 Hz, Bits: 16
  Data size: 441000 bytes (5.00 seconds)
  Playback complete (441000 bytes played)
========================================
```

## Troubleshooting

### Quotes Not Loading

- Check that `/quotes.txt` exists in the root directory
- Verify file format (quote text, then `~Author`, then blank line)
- Check for UTF-8 encoding (no special characters that might cause parsing issues)
- Look for "Loaded X quotes" in the serial output

### Audio Not Playing

- Verify `/media.txt` exists and has correct format
- Check that the audio file path in `media.txt` matches the actual file on the SD card
- Ensure WAV files are 16-bit PCM format (use `ffmpeg` to convert if needed)
- Monitor serial output for error messages during playback
- If a specific WAV file fails, the system will fall back to a beep tone

### SD Card Not Recognized

- Ensure SD card is formatted as FAT32
- Try power-cycling the SD card (use 'P' command in serial console)
- Check physical connections
- Use 'M' command in serial console to manually mount the card

## Advanced Tips

1. **Audio File Naming**: Use descriptive names for easy identification (e.g., `ocean_waves.wav` instead of `audio1.wav`)

2. **Quote Length**: Keep quotes reasonably short (under 150 characters) for better display readability

3. **Testing**: Use the interactive commands to test:
   - `M` - Mount SD card
   - `L` - List files
   - `G` - Load random PNG (which will trigger quote and audio)

4. **Batch Conversion**: Convert multiple audio files at once:
   ```bash
   for file in *.mp3; do
       ffmpeg -i "$file" -acodec pcm_s16le -ar 44100 -ac 1 "${file%.mp3}.wav"
   done
   ```

## Performance Notes

- Configuration files are loaded once at startup and cached in memory
- WAV files are streamed directly from SD card (not loaded entirely into memory)
- Large WAV files (>5 seconds) may take longer to play
- The system can handle up to 100 quotes and media mappings efficiently

### Critical: FatFS Buffer Location (SRAM vs PSRAM)

**This project is configured to use internal SRAM for filesystem buffers instead of PSRAM.**

By default, ESP-IDF allocates FatFS buffers in PSRAM when `CONFIG_FATFS_ALLOC_PREFER_EXTRAM=y` is set. While this saves internal SRAM, it causes **~10x performance degradation** for SD card I/O because:

1. PSRAM has significantly higher access latency than internal SRAM
2. Every file read/write operation goes through these buffers
3. Even with DMA, the external memory bus becomes a bottleneck

The setting `CONFIG_FATFS_ALLOC_PREFER_EXTRAM=n` in `sdkconfig.defaults` ensures FatFS uses internal SRAM, providing much faster SD card performance (~10x speedup).

**Memory Impact:**
- Per-file buffer: 4KB (with `CONFIG_FATFS_SECTOR_4096=y`)
- With `max_files=5`: ~20-25KB of internal SRAM used
- This tradeoff is worthwhile for the massive I/O performance gain

If you're experiencing slow SD card reads, verify this setting is applied after a full rebuild.

## Future Enhancements

Potential features for future versions:
- Support for subdirectories in mappings
- Playlist support (multiple audio files per image)
- Volume control per audio file
- Background music support
- Text-to-speech for quotes

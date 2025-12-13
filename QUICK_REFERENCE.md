# SD Card Configuration - Quick Reference

## TL;DR

Place these files on your SD card root to customize your e-ink display:

- `/quotes.txt` - Your custom quotes
- `/media.txt` - Image-to-audio mappings
- `*.wav` files - Your audio files

If files are missing, it falls back to defaults (hard-coded quotes + beep).

## File Format Cheat Sheet

### quotes.txt
```
First quote text here.
~Author Name

Second quote text here.
~Another Author
```
- Quote, then `~Author`, then blank line
- Repeat for each quote

### media.txt
```
image1.png,audio1.wav
image2.png,audio2.wav
```
- One mapping per line
- `#` for comments

### WAV Requirements
- 16-bit PCM
- 44.1kHz sample rate
- Mono or stereo

## Convert Audio (One Command)

### Linux/macOS
```bash
ffmpeg -i input.mp3 -acodec pcm_s16le -ar 44100 -ac 1 output.wav
```

### Windows
```cmd
ffmpeg -i input.mp3 -acodec pcm_s16le -ar 44100 -ac 1 output.wav
```

## Example SD Card Layout
```
/
├── quotes.txt           ← Your quotes
├── media.txt            ← Image-to-audio mappings
├── sunset.png           ← Your images
├── forest.png
├── ocean_waves.wav      ← Your audio files
├── bird_song.wav
└── traffic.wav
```

## Serial Console Commands

- `M` - Mount SD card
- `L` - List files
- `G` - Load random PNG (triggers quote + audio)

## Quick Test

1. Create `/quotes.txt`:
   ```
   This is a test.
   ~Me
   ```

2. Create `/media.txt`:
   ```
   test.png,beep.wav
   ```

3. Generate test audio:
   ```bash
   ffmpeg -f lavfi -i "sine=frequency=1000:duration=1" \
          -acodec pcm_s16le -ar 44100 -ac 1 beep.wav
   ```

4. Copy all to SD card and reboot

## Troubleshooting One-Liners

| Problem | Solution |
|---------|----------|
| Files not loading | Check filenames: `quotes.txt` and `media.txt` (lowercase) |
| Audio not playing | Convert: `ffmpeg -i in.mp3 -acodec pcm_s16le -ar 44100 -ac 1 out.wav` |
| Quotes malformed | Format: `quote\n~author\n\n` (blank line matters!) |
| No audio for image | Add to `media.txt`: `yourimage.png,youraudio.wav` |

## What Gets Logged

### Success
```
Loaded 5 quotes from SD card
Loaded 3 media mappings from SD card
Using SD card quote: "..." - Author
Playing WAV: ocean.wav
Audio playback complete
```

### Fallback
```
/quotes.txt not found (using fallback)
/media.txt not found (using fallback)
Using fallback quote: "..." - Author
No audio mapping, playing fallback beep
```

## Helper Scripts

### Batch Convert Audio Files

Linux/macOS:
```bash
./scripts/prepare_audio.sh music/*.mp3
# Output in: wav_output/
```

Windows:
```cmd
scripts\prepare_audio.bat music\*.mp3
REM Output in: wav_output\
```

## Need More Help?

- Full docs: [SD_CARD_CONFIG.md](SD_CARD_CONFIG.md)
- Implementation details: [IMPLEMENTATION_SD_AUDIO.md](IMPLEMENTATION_SD_AUDIO.md)
- Example files: `example_quotes.txt`, `example_media.txt`

## Pro Tips

1. **Keep WAV files short** (< 5 seconds) for best UX
2. **Use descriptive names** (`ocean_waves.wav` not `audio1.wav`)
3. **Test with one file first** before batch converting
4. **Check serial output** to debug issues
5. **Use mono audio** to save space (auto-converted to stereo)

---

**That's it!** Drop files on SD card, reboot, enjoy. 🎉

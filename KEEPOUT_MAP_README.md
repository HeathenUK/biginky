# ML-Based Keep-Out Area Mapping for Intelligent Text Placement

This system uses machine learning (YOLO object detection) to automatically identify salient objects in images and generate "keep-out" maps that prevent text from overlapping with important image content.

## Overview

The system consists of two parts:

1. **Python Script (`prepare_eink_image.py`)**: Converts images to Spectra 6 palette and generates keep-out maps using ML
2. **Firmware Integration**: Automatically loads and uses keep-out maps when available, with fallback to salience detection

## Workflow

```
Input Image (JPG/PNG)
         |
         v
  prepare_eink_image.py
  (YOLO object detection)
         |
         +---> image.bmp (Spectra 6 palette)
         |
         +---> image.map (keep-out bitmap)
         |
         v
    Copy to SD card
         |
         v
   ESP32-P4/RP2350
   (loads map + displays)
         |
         v
 Text placement avoids
   detected objects!
```

## Python Script Usage

### Installation

```bash
# Install required packages
pip install pillow numpy torch ultralytics opencv-python
```

The first time you run the script, YOLOv8 will automatically download (~6MB for nano model).

### Basic Usage

```bash
# Convert single image with ML detection
python scripts/prepare_eink_image.py photo.jpg /output_dir/

# This creates:
#   /output_dir/photo.bmp  (converted image)
#   /output_dir/photo.map  (keep-out map)
```

### Advanced Options

```bash
# Adjust detection sensitivity (0.0-1.0, default: 0.3)
python scripts/prepare_eink_image.py photo.jpg /output_dir/ --confidence 0.5

# More sensitive = detects more objects (more keep-out areas)
# Less sensitive = only detects prominent objects

# Adjust expansion margin around detected objects (default: 50px)
python scripts/prepare_eink_image.py photo.jpg /output_dir/ --expand 100

# Skip ML detection (faster, no keep-out map generated)
python scripts/prepare_eink_image.py photo.jpg /output_dir/ --no-ml
```

### Batch Processing

```bash
# Process entire directory
for img in images/*.jpg; do
    python scripts/prepare_eink_image.py "$img" /sd_card_mount/
done
```

## Map File Format

Binary format for efficient storage and fast loading:

```
Header (16 bytes):
  - Magic: "KOMAP" (5 bytes)
  - Version: uint8 (1 byte) - currently 1
  - Width: uint16 LE (2 bytes)
  - Height: uint16 LE (2 bytes)
  - Reserved: 6 bytes (for future use)

Data:
  - Bitmap: (width * height + 7) / 8 bytes
  - 1 bit per pixel (1 = keep out, 0 = safe for text)
  - MSB first within each byte
  - Row-major order
```

**Example:** 1600x1200 image = 240,016 bytes (234 KB)
- Header: 16 bytes
- Bitmap: 240,000 bytes (1,920,000 pixels / 8)

## Firmware Usage

The firmware automatically handles keep-out maps with zero code changes needed!

### How It Works

1. **BMP Display**: When a BMP is displayed from SD card (e.g., `landscape.bmp`)
2. **Map Lookup**: System checks for corresponding map file (`landscape.map`)
3. **Auto-Load**: If map exists, it's loaded into the text placement analyzer
4. **Intelligent Placement**: Text positioning automatically avoids keep-out areas
5. **Fallback**: If no map exists, uses existing salience detection (variance/edge density)

### Text Placement Algorithm

The keep-out map is integrated into the scoring system:

```
Region Score Calculation:
  1. Check keep-out coverage:
     - > 50% overlap: Score = 0.0 (reject completely)
     - 20-50% overlap: Apply penalty (score × 0.5)
     - < 20% overlap: Minor penalty
  
  2. Apply standard metrics:
     - Contrast score (how well text stands out)
     - Uniformity score (background consistency)
     - Edge avoidance score (avoid busy areas)
  
  3. Final score = weighted sum with keep-out penalty
```

**Grid Optimization**: During candidate generation, positions with >50% keep-out overlap are skipped entirely for efficiency.

### Memory Usage

Keep-out map memory consumption:
- 1600×1200 display: ~234 KB (loaded into heap/PSRAM)
- Only one map loaded at a time
- Automatically freed when clearing/loading new map

## Example Workflow

### 1. Prepare Images on PC

```bash
# You have some photos you want to display
ls photos/
  mountain_sunset.jpg
  city_skyline.jpg
  forest_lake.jpg

# Convert them with ML detection
for img in photos/*.jpg; do
    python scripts/prepare_eink_image.py "$img" ./sd_card/
done

# Result:
ls sd_card/
  mountain_sunset.bmp    (1600×1200 Spectra 6 image)
  mountain_sunset.map    (keep-out map with mountain/sun masked)
  city_skyline.bmp
  city_skyline.map       (buildings marked as keep-out)
  forest_lake.bmp
  forest_lake.map        (trees/water marked)
```

### 2. Copy to SD Card

```bash
# Mount your SD card and copy files
cp sd_card/*.bmp /media/sdcard/
cp sd_card/*.map /media/sdcard/
```

### 3. Display on Device

The device will:
1. Pick a random BMP from SD card
2. Check for corresponding .map file
3. Load the map if available
4. Place time/date text avoiding detected objects
5. Display looks professional with no text overlap!

## Technical Details

### YOLO Model

The script uses **YOLOv8n** (nano) by default:
- **Speed**: ~20-50ms inference on CPU, ~2-5ms on GPU
- **Accuracy**: Detects 80 object classes (COCO dataset)
- **Model Size**: ~6MB
- **Classes**: person, car, bicycle, chair, cup, laptop, etc.

You can use larger models for better accuracy:
```python
# In prepare_eink_image.py, line 233:
model = YOLO('yolov8s.pt')  # Small model (better accuracy, slower)
model = YOLO('yolov8m.pt')  # Medium model (best balance)
```

### Detection Classes

The model detects 80 common objects including:
- **People & Animals**: person, cat, dog, horse, bird, etc.
- **Vehicles**: car, bus, truck, bicycle, motorcycle, boat, airplane
- **Objects**: chair, bottle, cup, book, laptop, phone, clock
- **Food**: pizza, cake, apple, banana, wine glass
- **Furniture**: couch, bed, dining table, tv
- And many more...

### Performance Benchmarks

**Image Conversion** (1600×1200 image):
- Load & resize: ~100-200ms
- Lab color mapping: ~500-800ms
- YOLO inference: ~20-50ms (CPU), ~2-5ms (GPU)
- Map generation: ~50-100ms
- BMP save: ~200-300ms
- **Total**: ~1-2 seconds per image

**Firmware Loading**:
- Map file read from SD: ~50-100ms (234KB @ ~2MB/s)
- Bitmap parsing: ~10ms
- **Total**: ~100ms per image change

**Text Placement Impact**:
- Grid scan with map: +5-10% overhead vs. no map
- Worth it for dramatically better placement!

## Troubleshooting

### Python Script Issues

**Problem**: `ImportError: No module named 'ultralytics'`
```bash
# Solution: Install ML dependencies
pip install torch ultralytics opencv-python
```

**Problem**: YOLO model fails to download
```bash
# Solution: Download manually and place in home directory
wget https://github.com/ultralytics/assets/releases/download/v0.0.0/yolov8n.pt
mv yolov8n.pt ~/.cache/yolov8/
```

**Problem**: "No objects detected" for images with obvious objects
```bash
# Solution: Lower confidence threshold
python prepare_eink_image.py photo.jpg out/ --confidence 0.2
```

**Problem**: Too many keep-out areas (text has nowhere to go)
```bash
# Solution: Increase confidence or reduce margin
python prepare_eink_image.py photo.jpg out/ --confidence 0.5 --expand 25
```

### Firmware Issues

**Problem**: "Map file not found"
- Check that .map file has same base name as .bmp
- Check file exists on SD card root directory
- Check filename case (should match exactly)

**Problem**: "Failed to read map header"
- Map file may be corrupted
- Regenerate with Python script
- Check file size (should be ~234KB for 1600×1200)

**Problem**: Text still overlaps objects despite map
- Check Serial output for "Keep-out coverage: X%"
- If coverage too high (>80%), all positions rejected
- Try images with more open space for text
- Consider using lower confidence when generating map

## Advanced Customization

### Custom YOLO Models

You can use custom-trained YOLO models:

```python
# In prepare_eink_image.py
model = YOLO('/path/to/custom_model.pt')
```

Useful for:
- Industry-specific object detection
- Face detection only (avoid placing text over faces)
- Product detection (avoid logos/brands)

### Custom Keep-Out Logic

Modify `detect_objects_yolo()` in the Python script:

```python
# Only mark faces as keep-out (ignore other objects)
for box in boxes:
    cls = int(box.cls[0].cpu().numpy())
    class_name = model.names[cls]
    
    if class_name == 'person':  # Only keep out people
        # Mark as keep-out...
```

### Firmware-Side Customization

Adjust keep-out penalties in `EL133UF1_TextPlacement.cpp`:

```cpp
// Line ~410 in analyzeRegion()

// More aggressive avoidance
if (keepOutCoverage > 0.3f) {  // Was 0.5
    metrics.overallScore = 0.0f;
    return metrics;
}

// Or less aggressive (allow some overlap)
if (keepOutCoverage > 0.7f) {  // Was 0.5
    metrics.overallScore = 0.0f;
    return metrics;
}
```

## Performance Tips

### For Faster Processing

1. **Use GPU** if available (10x faster inference):
   ```python
   # YOLO automatically uses CUDA if available
   # Check with: torch.cuda.is_available()
   ```

2. **Batch process** images overnight:
   ```bash
   nohup bash -c 'for img in photos/*.jpg; do python prepare_eink_image.py "$img" out/; done' &
   ```

3. **Use nano model** (default, fastest):
   ```python
   model = YOLO('yolov8n.pt')  # Already the default
   ```

### For Better Detection

1. **Use larger model**:
   ```python
   model = YOLO('yolov8m.pt')  # Medium model
   ```

2. **Adjust confidence** per image type:
   - Portraits: `--confidence 0.4` (focus on faces)
   - Landscapes: `--confidence 0.2` (catch subtle objects)
   - Product photos: `--confidence 0.5` (only prominent items)

3. **Tune expansion margin**:
   - Tight text placement: `--expand 25`
   - Safe spacing: `--expand 75`

## Future Enhancements

Possible improvements:
- [ ] Semantic segmentation (pixel-perfect masks instead of bounding boxes)
- [ ] Text-specific zones (mark "text-safe" areas instead of keep-out)
- [ ] Multi-level priority (some objects more important than others)
- [ ] Dynamic text sizing (larger text in open areas, smaller in tight spaces)
- [ ] Automatic quote selection based on image content (ML image captioning)

## Credits

- **YOLO**: Ultralytics YOLOv8 (https://github.com/ultralytics/ultralytics)
- **Object Detection**: COCO dataset (80 classes)
- **Color Mapping**: CIE Lab perceptual color space
- **Display**: EL133UF1 13.3" Spectra 6 E-Ink

## License

Same license as the main project.

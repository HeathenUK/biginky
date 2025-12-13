# Quick Start: ML Keep-Out Text Placement

> **TL;DR**: Use ML to automatically avoid placing text over important objects in your images.

## 1️⃣ Install Dependencies (One Time)

```bash
pip install pillow numpy torch ultralytics opencv-python
```

*First run downloads YOLOv8 model (~6MB) automatically.*

## 2️⃣ Convert Images

```bash
# Single image
python scripts/prepare_eink_image.py photo.jpg /sd_card/

# Creates:
#   /sd_card/photo.bmp  (converted image)
#   /sd_card/photo.map  (keep-out areas)
```

### Batch Processing

```bash
# All images in a folder
./scripts/example_prepare_batch.sh ~/Photos/ /sd_card/
```

## 3️⃣ Copy to SD Card

```bash
# Copy both .bmp and .map files
cp /sd_card/*.bmp /media/sdcard/
cp /sd_card/*.map /media/sdcard/
```

## 4️⃣ Use on Device

**No code changes needed!** The firmware automatically:
1. Displays a random .bmp from SD card
2. Checks for matching .map file
3. Uses map if available, falls back to salience detection otherwise
4. Places text avoiding detected objects ✨

## Tuning Detection

### More Objects Detected (More Keep-Out)
```bash
python scripts/prepare_eink_image.py photo.jpg out/ --confidence 0.2
```

### Fewer Objects (Less Keep-Out)
```bash
python scripts/prepare_eink_image.py photo.jpg out/ --confidence 0.5
```

### Tighter Text Placement
```bash
python scripts/prepare_eink_image.py photo.jpg out/ --expand 25
```

### More Space Around Objects
```bash
python scripts/prepare_eink_image.py photo.jpg out/ --expand 100
```

## Verify Map Files

```bash
# Check map is valid
python scripts/verify_map_file.py landscape.map

# Visualize keep-out areas
python scripts/verify_map_file.py landscape.map --visualize landscape.bmp
# Creates: landscape_viz.png
```

## How It Works

```
┌─────────────────────────────────────────────┐
│  1. Python script uses YOLO to detect       │
│     objects (people, cars, animals, etc.)   │
│                                             │
│  2. Generates binary map marking detected   │
│     areas as "keep-out"                     │
│                                             │
│  3. Firmware loads map from SD card         │
│                                             │
│  4. Text placement avoids keep-out pixels   │
│     - >50% overlap = reject position        │
│     - 20-50% overlap = penalty              │
│     - <20% overlap = minor penalty          │
└─────────────────────────────────────────────┘
```

## File Naming Convention

The map filename must match the image:
- Image: `mountain_sunset.bmp` → Map: `mountain_sunset.map` ✅
- Image: `beach.bmp` → Map: `ocean.map` ❌ (won't be found)

## Example Results

| Without Map | With Map |
|-------------|----------|
| Text may overlap faces, objects | Text automatically avoids detected objects |
| Only uses color/edge detection | Uses ML object detection + color/edge |
| Works everywhere | Requires map file on SD card |

## Troubleshooting

### "No objects detected"
→ Lower confidence: `--confidence 0.2`

### "Too many keep-out areas"
→ Raise confidence: `--confidence 0.5`
→ Or reduce margin: `--expand 25`

### "Map file not found"
→ Check filename matches exactly (including case)
→ Check .map file is in SD card root

### "Text still overlaps objects"
→ Check Serial output for coverage percentage
→ May need more open space in image
→ Try different image composition

## What Objects Are Detected?

YOLOv8 detects 80 classes including:
- **People**: person, face
- **Animals**: cat, dog, horse, bird, cow, etc.
- **Vehicles**: car, bus, truck, bicycle, motorcycle, boat, plane
- **Objects**: chair, bottle, cup, laptop, phone, book, clock
- **Food**: pizza, cake, apple, banana, wine glass
- And many more...

See full list: https://github.com/ultralytics/ultralytics/blob/main/ultralytics/cfg/datasets/coco.yaml

## Performance

- **Python**: 1-2 seconds per image (CPU), 0.3-0.5s (GPU)
- **Firmware**: ~100ms to load map, +5-10% text placement time
- **Memory**: ~234KB per loaded map (1600×1200)

## Documentation

- **Full Guide**: `KEEPOUT_MAP_README.md`
- **Changes**: `CHANGES_ML_KEEPOUT.md`
- **This File**: `QUICKSTART_ML_KEEPOUT.md`

## Tips

💡 **Portraits**: Use `--confidence 0.4` to focus on faces
💡 **Landscapes**: Use `--confidence 0.2` to catch subtle objects  
💡 **Products**: Use `--confidence 0.5` for only prominent items
💡 **Tight Layouts**: Use `--expand 25` for minimal margins
💡 **Safe Spacing**: Use `--expand 75` for generous margins

## Questions?

Check `KEEPOUT_MAP_README.md` for detailed documentation and troubleshooting.

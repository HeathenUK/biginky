#!/bin/bash
#
# Example: Batch process images for EL133UF1 display with ML keep-out mapping
#
# This script demonstrates how to prepare multiple images at once,
# converting them to Spectra 6 palette and generating keep-out maps.
#
# Usage:
#   ./example_prepare_batch.sh /path/to/photos/ /path/to/sd_card/
#

set -e  # Exit on error

if [ $# -lt 2 ]; then
    echo "Usage: $0 <input_dir> <output_dir>"
    echo ""
    echo "Example:"
    echo "  $0 ~/Photos/vacation/ /media/sdcard/"
    echo ""
    exit 1
fi

INPUT_DIR="$1"
OUTPUT_DIR="$2"

# Check if directories exist
if [ ! -d "$INPUT_DIR" ]; then
    echo "Error: Input directory not found: $INPUT_DIR"
    exit 1
fi

if [ ! -d "$OUTPUT_DIR" ]; then
    echo "Creating output directory: $OUTPUT_DIR"
    mkdir -p "$OUTPUT_DIR"
fi

# Get the script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREPARE_SCRIPT="$SCRIPT_DIR/prepare_eink_image.py"

if [ ! -f "$PREPARE_SCRIPT" ]; then
    echo "Error: prepare_eink_image.py not found at: $PREPARE_SCRIPT"
    exit 1
fi

echo "=========================================="
echo "  Batch Image Preparation for EL133UF1"
echo "=========================================="
echo ""
echo "Input:  $INPUT_DIR"
echo "Output: $OUTPUT_DIR"
echo ""

# Find all image files (common formats)
IMAGE_FILES=($(find "$INPUT_DIR" -maxdepth 1 -type f \( -iname "*.jpg" -o -iname "*.jpeg" -o -iname "*.png" -o -iname "*.webp" \) | sort))

if [ ${#IMAGE_FILES[@]} -eq 0 ]; then
    echo "Error: No image files found in $INPUT_DIR"
    echo "Supported formats: .jpg, .jpeg, .png, .webp"
    exit 1
fi

echo "Found ${#IMAGE_FILES[@]} image(s) to process"
echo ""

# Process each image
SUCCESS=0
FAILED=0

for img in "${IMAGE_FILES[@]}"; do
    BASENAME=$(basename "$img")
    echo "Processing: $BASENAME"
    
    if python "$PREPARE_SCRIPT" "$img" "$OUTPUT_DIR" --confidence 0.3 --expand 50; then
        SUCCESS=$((SUCCESS + 1))
        echo "  ✓ Success"
    else
        FAILED=$((FAILED + 1))
        echo "  ✗ Failed"
    fi
    echo ""
done

echo "=========================================="
echo "  Batch Processing Complete"
echo "=========================================="
echo ""
echo "Results:"
echo "  Successful: $SUCCESS"
echo "  Failed:     $FAILED"
echo "  Total:      ${#IMAGE_FILES[@]}"
echo ""
echo "Output files in: $OUTPUT_DIR"
echo ""

# List output files
echo "Generated files:"
ls -lh "$OUTPUT_DIR"/*.bmp 2>/dev/null | awk '{print "  " $9 " (" $5 ")"}'
echo ""
echo "Map files:"
ls -lh "$OUTPUT_DIR"/*.map 2>/dev/null | awk '{print "  " $9 " (" $5 ")"}'
echo ""

echo "Next steps:"
echo "  1. Copy *.bmp and *.map files to SD card root"
echo "  2. Insert SD card into device"
echo "  3. Device will automatically use maps when available!"
echo ""

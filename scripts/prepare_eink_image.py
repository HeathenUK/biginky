#!/usr/bin/env python3
"""
Prepare images for EL133UF1 13.3" Spectra 6 E-Ink display with ML-based keep-out mapping.

This script:
1. Converts images to Spectra 6 color palette (optimized for e-ink)
2. Uses YOLO object detection to identify salient objects
3. Generates a binary keep-out map file for intelligent text placement
4. Saves both the converted BMP and the keep-out map to the output directory

The keep-out map ensures text avoids overlapping with detected objects,
improving readability and visual composition.

Requirements:
    pip install pillow numpy torch ultralytics opencv-python

Usage:
    python prepare_eink_image.py input.jpg output_dir/
    python prepare_eink_image.py input.png output_dir/ --confidence 0.5
    python prepare_eink_image.py input.jpg output_dir/ --no-ml  # Skip ML detection
"""

import argparse
import os
import sys
import struct
from pathlib import Path

import numpy as np
from PIL import Image

# Optional ML imports (graceful degradation if not available)
try:
    from ultralytics import YOLO
    import torch
    import cv2
    HAS_ML = True
except ImportError:
    HAS_ML = False
    print("Warning: ML libraries not found. Install with: pip install torch ultralytics opencv-python", file=sys.stderr)

# Display dimensions (EL133UF1)
DISPLAY_WIDTH = 1600
DISPLAY_HEIGHT = 1200

# Spectra 6 palette (must match firmware DEFAULT_PALETTE)
SPECTRA_PALETTE = [
    (10, 10, 10),        # Black (0)
    (245, 245, 235),     # White (1)
    (245, 210, 50),      # Yellow (2)
    (190, 60, 55),       # Red (3)
    (45, 75, 160),       # Blue (5)
    (55, 140, 85),       # Green (6)
]

# Spectra color codes
EL133UF1_BLACK = 0
EL133UF1_WHITE = 1
EL133UF1_YELLOW = 2
EL133UF1_RED = 3
EL133UF1_BLUE = 5
EL133UF1_GREEN = 6

SPECTRA_CODES = [
    EL133UF1_BLACK,
    EL133UF1_WHITE,
    EL133UF1_YELLOW,
    EL133UF1_RED,
    EL133UF1_BLUE,
    EL133UF1_GREEN,
]


def rgb_to_lab(rgb):
    """Convert RGB array to CIE Lab color space."""
    # Normalize to 0-1
    rgb = rgb.astype(float) / 255.0
    
    # sRGB to linear RGB
    def srgb_to_linear(c):
        return np.where(c > 0.04045, ((c + 0.055) / 1.055) ** 2.4, c / 12.92)
    
    rgb_linear = srgb_to_linear(rgb)
    
    # Linear RGB to XYZ (using D65 illuminant)
    transform = np.array([
        [0.4124564, 0.3575761, 0.1804375],
        [0.2126729, 0.7151522, 0.0721750],
        [0.0193339, 0.1191920, 0.9503041]
    ])
    
    xyz = np.dot(rgb_linear, transform.T)
    
    # Normalize for D65 white point
    xyz[:, :, 0] /= 0.95047
    xyz[:, :, 1] /= 1.00000
    xyz[:, :, 2] /= 1.08883
    
    # XYZ to Lab
    epsilon = 0.008856
    kappa = 903.3
    
    def f(t):
        return np.where(t > epsilon, t ** (1/3), (kappa * t + 16.0) / 116.0)
    
    fx = f(xyz[:, :, 0])
    fy = f(xyz[:, :, 1])
    fz = f(xyz[:, :, 2])
    
    L = 116.0 * fy - 16.0
    a = 500.0 * (fx - fy)
    b = 200.0 * (fy - fz)
    
    return np.stack([L, a, b], axis=-1)


def map_to_spectra(img):
    """Map RGB image to Spectra 6 palette using Lab color space."""
    print(f"  Converting to Spectra 6 palette (Lab color space)...")
    
    # Convert image to Lab
    img_lab = rgb_to_lab(np.array(img))
    
    # Convert palette to Lab
    palette_lab = []
    for r, g, b in SPECTRA_PALETTE:
        rgb_single = np.array([[[r, g, b]]], dtype=np.uint8)
        lab_single = rgb_to_lab(rgb_single)
        palette_lab.append(lab_single[0, 0])
    palette_lab = np.array(palette_lab)
    
    # For each pixel, find nearest palette color (CIE76 Delta E)
    h, w = img_lab.shape[:2]
    mapped = np.zeros((h, w), dtype=np.uint8)
    
    # Vectorized distance calculation (much faster than loop)
    # Reshape for broadcasting: (h, w, 1, 3) - (6, 3) -> (h, w, 6)
    img_lab_expanded = img_lab[:, :, np.newaxis, :]
    palette_lab_expanded = palette_lab[np.newaxis, np.newaxis, :, :]
    
    # CIE76 Delta E (Euclidean distance in Lab space)
    distances = np.sum((img_lab_expanded - palette_lab_expanded) ** 2, axis=-1)
    
    # Find closest palette color for each pixel
    closest_indices = np.argmin(distances, axis=-1)
    
    # Map to Spectra color codes
    for i, code in enumerate(SPECTRA_CODES):
        mapped[closest_indices == i] = code
    
    return mapped


def save_as_bmp(mapped, output_path):
    """Save mapped image as 24-bit BMP using Spectra palette RGB values."""
    print(f"  Saving BMP: {output_path}")
    
    h, w = mapped.shape
    
    # Convert Spectra codes back to RGB for BMP
    rgb_img = np.zeros((h, w, 3), dtype=np.uint8)
    for i, (r, g, b) in enumerate(SPECTRA_PALETTE):
        code = SPECTRA_CODES[i]
        mask = (mapped == code)
        rgb_img[mask] = [r, g, b]
    
    # Save as BMP
    Image.fromarray(rgb_img, 'RGB').save(output_path, 'BMP')


def detect_objects_yolo(img, confidence=0.3, expand_margin=50):
    """
    Detect objects using YOLOv8 and generate keep-out masks.
    
    Args:
        img: PIL Image (RGB)
        confidence: Detection confidence threshold (0.0-1.0)
        expand_margin: Pixels to expand around detected objects
        
    Returns:
        keep_out_mask: Binary mask (0=safe, 255=keep out)
    """
    if not HAS_ML:
        print("  WARNING: ML libraries not available, skipping object detection")
        return None
    
    print(f"  Running YOLO object detection (confidence={confidence})...")
    
    # Convert PIL to numpy/cv2 format
    img_np = np.array(img)
    img_cv = cv2.cvtColor(img_np, cv2.COLOR_RGB2BGR)
    
    # Load YOLOv8 model (downloads automatically on first run)
    try:
        model = YOLO('yolov8n.pt')  # nano model (fastest)
        # model = YOLO('yolov8s.pt')  # small model (more accurate)
    except Exception as e:
        print(f"  ERROR: Failed to load YOLO model: {e}")
        return None
    
    # Run detection
    results = model(img_cv, conf=confidence, verbose=False)
    
    # Create keep-out mask
    h, w = img_np.shape[:2]
    keep_out_mask = np.zeros((h, w), dtype=np.uint8)
    
    detections = 0
    for result in results:
        boxes = result.boxes
        if boxes is not None and len(boxes) > 0:
            for box in boxes:
                # Get bounding box coordinates
                x1, y1, x2, y2 = box.xyxy[0].cpu().numpy()
                conf = box.conf[0].cpu().numpy()
                cls = int(box.cls[0].cpu().numpy())
                class_name = model.names[cls]
                
                # Expand box by margin
                x1 = max(0, int(x1) - expand_margin)
                y1 = max(0, int(y1) - expand_margin)
                x2 = min(w, int(x2) + expand_margin)
                y2 = min(h, int(y2) + expand_margin)
                
                # Mark as keep-out area
                keep_out_mask[y1:y2, x1:x2] = 255
                
                detections += 1
                print(f"    Detected: {class_name} (conf={conf:.2f}) at [{x1},{y1},{x2},{y2}]")
    
    if detections == 0:
        print("    No objects detected")
        return None
    
    print(f"    Total objects detected: {detections}")
    
    # Visualize keep-out areas (for debugging)
    coverage = (keep_out_mask > 0).sum() / (h * w) * 100
    print(f"    Keep-out coverage: {coverage:.1f}% of image")
    
    return keep_out_mask


def save_keepout_map(keep_out_mask, output_path, img_filename):
    """
    Save keep-out map in binary format.
    
    File format:
        Header (16 bytes):
            - Magic: "KOMAP" (5 bytes)
            - Version: uint8 (1 byte) - currently 1
            - Width: uint16 LE (2 bytes)
            - Height: uint16 LE (2 bytes)
            - Reserved: 6 bytes (for future use)
        Data:
            - Bitmap: (width * height + 7) / 8 bytes (1 bit per pixel)
            - 1 = keep out, 0 = safe for text
    """
    if keep_out_mask is None:
        print(f"  No keep-out map to save")
        return
    
    print(f"  Saving keep-out map: {output_path}")
    
    h, w = keep_out_mask.shape
    
    # Pack bitmap (1 bit per pixel)
    # Row-major order, MSB first within each byte
    bitmap_bytes = []
    for y in range(h):
        for x in range(0, w, 8):
            byte = 0
            for bit in range(8):
                if x + bit < w:
                    if keep_out_mask[y, x + bit] > 0:
                        byte |= (1 << (7 - bit))
            bitmap_bytes.append(byte)
    
    # Write binary file
    with open(output_path, 'wb') as f:
        # Header
        f.write(b'KOMAP')                          # Magic (5 bytes)
        f.write(struct.pack('B', 1))               # Version (1 byte)
        f.write(struct.pack('<H', w))              # Width (2 bytes LE)
        f.write(struct.pack('<H', h))              # Height (2 bytes LE)
        f.write(b'\x00' * 6)                       # Reserved (6 bytes)
        
        # Bitmap data
        f.write(bytes(bitmap_bytes))
    
    file_size = os.path.getsize(output_path)
    print(f"    Map file size: {file_size} bytes ({file_size / 1024:.1f} KB)")
    
    # Calculate theoretical size
    bitmap_size = (w * h + 7) // 8
    expected_size = 16 + bitmap_size
    print(f"    Expected size: {expected_size} bytes (header=16, bitmap={bitmap_size})")


def process_image(input_path, output_dir, use_ml=True, confidence=0.3, expand_margin=50):
    """Process a single image: convert to Spectra 6 and generate keep-out map."""
    print(f"\n{'=' * 60}")
    print(f"Processing: {input_path}")
    print(f"{'=' * 60}")
    
    # Load and resize image
    print(f"Loading image...")
    img = Image.open(input_path).convert('RGB')
    orig_size = img.size
    print(f"  Original size: {orig_size[0]}x{orig_size[1]}")
    
    # Resize to display dimensions (maintain aspect ratio, crop to fit)
    print(f"  Resizing to {DISPLAY_WIDTH}x{DISPLAY_HEIGHT}...")
    
    # Calculate scaling to fill display (crop excess)
    scale_w = DISPLAY_WIDTH / img.width
    scale_h = DISPLAY_HEIGHT / img.height
    scale = max(scale_w, scale_h)  # Scale to fill
    
    new_w = int(img.width * scale)
    new_h = int(img.height * scale)
    
    img_scaled = img.resize((new_w, new_h), Image.Resampling.LANCZOS)
    
    # Crop to display size (center crop)
    left = (new_w - DISPLAY_WIDTH) // 2
    top = (new_h - DISPLAY_HEIGHT) // 2
    img = img_scaled.crop((left, top, left + DISPLAY_WIDTH, top + DISPLAY_HEIGHT))
    
    print(f"  Final size: {img.width}x{img.height}")
    
    # ML object detection (if enabled)
    keep_out_mask = None
    if use_ml:
        keep_out_mask = detect_objects_yolo(img, confidence, expand_margin)
    
    # Convert to Spectra palette
    mapped = map_to_spectra(img)
    
    # Generate output filenames
    input_stem = Path(input_path).stem
    output_bmp = os.path.join(output_dir, f"{input_stem}.bmp")
    output_map = os.path.join(output_dir, f"{input_stem}.map")
    
    # Save BMP
    save_as_bmp(mapped, output_bmp)
    
    # Save keep-out map
    if keep_out_mask is not None:
        save_keepout_map(keep_out_mask, output_map, input_stem)
    
    print(f"\n✓ Processing complete!")
    print(f"  BMP:  {output_bmp}")
    if keep_out_mask is not None:
        print(f"  MAP:  {output_map}")
    print(f"{'=' * 60}\n")


def main():
    parser = argparse.ArgumentParser(
        description='Prepare images for EL133UF1 with ML-based keep-out mapping',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Convert single image with ML detection
  python prepare_eink_image.py photo.jpg /sd_card/

  # Adjust detection sensitivity
  python prepare_eink_image.py photo.jpg /sd_card/ --confidence 0.5

  # Skip ML detection (faster, no keep-out map)
  python prepare_eink_image.py photo.jpg /sd_card/ --no-ml

  # Process entire directory
  for img in images/*.jpg; do
      python prepare_eink_image.py "$img" /sd_card/
  done
        """
    )
    
    parser.add_argument('input', help='Input image file (JPG, PNG, etc.)')
    parser.add_argument('output_dir', help='Output directory for BMP and map files')
    parser.add_argument('--no-ml', action='store_true', 
                        help='Skip ML object detection (no keep-out map)')
    parser.add_argument('--confidence', type=float, default=0.3,
                        help='YOLO confidence threshold (0.0-1.0, default: 0.3)')
    parser.add_argument('--expand', type=int, default=50,
                        help='Pixels to expand around detected objects (default: 50)')
    
    args = parser.parse_args()
    
    # Validate inputs
    if not os.path.isfile(args.input):
        print(f"ERROR: Input file not found: {args.input}", file=sys.stderr)
        return 1
    
    if not os.path.isdir(args.output_dir):
        print(f"Creating output directory: {args.output_dir}")
        os.makedirs(args.output_dir, exist_ok=True)
    
    # Check ML availability
    if not args.no_ml and not HAS_ML:
        print("ERROR: ML libraries not installed. Either:", file=sys.stderr)
        print("  1. Install: pip install torch ultralytics opencv-python", file=sys.stderr)
        print("  2. Use --no-ml flag to skip object detection", file=sys.stderr)
        return 1
    
    # Process image
    try:
        process_image(
            args.input, 
            args.output_dir, 
            use_ml=(not args.no_ml),
            confidence=args.confidence,
            expand_margin=args.expand
        )
        return 0
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        return 1


if __name__ == '__main__':
    sys.exit(main())

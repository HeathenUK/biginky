#encoding: utf-8

import sys
import os.path
import struct
import numpy as np
from PIL import Image, ImagePalette, ImageOps, ImageEnhance, ImageFilter
import argparse
import pillow_heif
from tqdm import tqdm

pillow_heif.register_heif_opener()

# Optional ML imports (for keep-out map generation)
try:
    from ultralytics import YOLO
    import torch
    import cv2
    HAS_ML = True
except ImportError:
    HAS_ML = False

# Define the 6-color palette (black, white, yellow, red, blue, green)
PALETTE_COLORS = [
    (0, 0, 0),        # Black
    (255, 255, 255),  # White
    (255, 255, 0),    # Yellow
    (255, 0, 0),      # Red
    (0, 0, 255),      # Blue
    (0, 255, 0)       # Green
]

# Precompute palette as NumPy arrays for faster access
PALETTE_ARRAY = np.array(PALETTE_COLORS, dtype=np.float32)
PALETTE_LUMA_ARRAY = np.array(
    [r*250 + g*350 + b*400 for (r, g, b) in PALETTE_COLORS],
    dtype=np.float32
) / (255.0 * 1000)

# Target panel sizes
TARGET_SIZE_LANDSCAPE = (1600, 1200)
TARGET_SIZE_PORTRAIT = (1200, 1600)

def closest_palette_color(rgb):
    r1, g1, b1 = rgb
    luma1 = (r1 * 250 + g1 * 350 + b1 * 400) / (255.0 * 1000)

    diffR = r1 - PALETTE_ARRAY[:, 0]
    diffG = g1 - PALETTE_ARRAY[:, 1]
    diffB = b1 - PALETTE_ARRAY[:, 2]

    rgb_dist = (diffR*diffR*0.250 + diffG*diffG*0.350 + diffB*diffB*0.400) * 0.75 / (255.0*255.0)
    luma_diff = luma1 - PALETTE_LUMA_ARRAY
    luma_dist = luma_diff * luma_diff

    total_dist = 1.5*rgb_dist + 0.60*luma_dist
    return np.argmin(total_dist)

def quantize_atkinson(image):
    img_array = np.array(image.convert('RGB'))
    height, width, _ = img_array.shape
    working_img = img_array.astype(np.float32)

    for y in range(height):
        for x in range(width):
            old_pixel = working_img[y, x].copy()
            idx = closest_palette_color(tuple(np.clip(old_pixel, 0, 255).astype(int)))
            new_pixel = np.array(PALETTE_COLORS[idx], dtype=np.float32)
            working_img[y, x] = new_pixel
            error = old_pixel - new_pixel

            if x + 1 < width:
                working_img[y, x + 1] += error * (1/8)
            if y + 1 < height:
                if x - 1 >= 0:
                    working_img[y + 1, x - 1] += error * (1/8)
                working_img[y + 1, x] += error * (1/4)
                if x + 1 < width:
                    working_img[y + 1, x + 1] += error * (1/8)

    quantized_array = np.clip(working_img, 0, 255).astype(np.uint8)
    return Image.fromarray(quantized_array)


def detect_objects_yolo(img, confidence=0.3, expand_margin=50, use_segmentation=True, verbose=False):
    """
    Detect objects using YOLOv8 and generate keep-out masks.
    
    Args:
        img: PIL Image
        confidence: Detection confidence threshold (0.0-1.0)
        expand_margin: Pixels to expand around detected objects
        use_segmentation: If True, use pixel-level segmentation (precise).
                         If False, use bounding boxes (faster).
        verbose: Print detection details
    
    Returns:
        numpy array: Keep-out mask (255 = keep out, 0 = safe)
    """
    if not HAS_ML:
        if verbose:
            print("  ML libraries not available, skipping object detection")
        return None
    
    method = "segmentation" if use_segmentation else "bounding boxes"
    if verbose:
        print(f"  Running YOLO object detection ({method}, confidence={confidence})...")
    
    # Convert PIL to numpy
    img_np = np.array(img)
    
    # Run YOLO detection (try segmentation model if requested)
    try:
        if use_segmentation:
            model = YOLO('yolov8n-seg.pt')  # Segmentation model
        else:
            model = YOLO('yolov8n.pt')  # Detection model
    except Exception as e:
        if verbose:
            print(f"  ERROR: Failed to load YOLO model: {e}")
        if use_segmentation:
            if verbose:
                print(f"  Falling back to bounding boxes...")
            use_segmentation = False
            try:
                model = YOLO('yolov8n.pt')
            except:
                return None
        else:
            return None
    
    results = model(img_np, conf=confidence, verbose=False)
    
    # Create keep-out mask
    h, w = img_np.shape[:2]
    keep_out_mask = np.zeros((h, w), dtype=np.uint8)
    
    detections = 0
    for result in results:
        # Try segmentation masks first (more accurate)
        if use_segmentation and hasattr(result, 'masks') and result.masks is not None:
            masks = result.masks
            boxes = result.boxes
            for i, mask in enumerate(masks):
                # Get segmentation mask as numpy array
                mask_np = mask.data[0].cpu().numpy()
                
                # Resize mask to image size if needed
                if mask_np.shape != (h, w):
                    mask_np = cv2.resize(mask_np, (w, h), interpolation=cv2.INTER_NEAREST)
                
                # Convert to binary mask
                binary_mask = (mask_np > 0.5).astype(np.uint8) * 255
                
                # Expand mask by dilation
                if expand_margin > 0:
                    kernel = np.ones((expand_margin*2, expand_margin*2), np.uint8)
                    binary_mask = cv2.dilate(binary_mask, kernel, iterations=1)
                
                # Add to keep-out mask
                keep_out_mask = np.maximum(keep_out_mask, binary_mask)
                
                detections += 1
                if verbose and boxes is not None:
                    conf = boxes[i].conf[0].cpu().numpy()
                    cls = int(boxes[i].cls[0].cpu().numpy())
                    class_name = model.names[cls]
                    pixels = (binary_mask > 0).sum()
                    print(f"    Detected: {class_name} (conf={conf:.2f}) - {pixels} pixels")
        
        # Fallback to bounding boxes
        else:
            boxes = result.boxes
            if boxes is not None and len(boxes) > 0:
                for box in boxes:
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
                    if verbose:
                        print(f"    Detected: {class_name} (conf={conf:.2f}) at [{x1},{y1},{x2},{y2}]")
    
    if detections == 0:
        if verbose:
            print("    No objects detected")
        return None
    
    if verbose:
        coverage = (keep_out_mask > 0).sum() / (h * w) * 100
        print(f"    Total objects: {detections}, Coverage: {coverage:.1f}%")
    
    return keep_out_mask


def save_keepout_map(keep_out_mask, output_path, verbose=False):
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
        if verbose:
            print(f"  No keep-out map to save")
        return
    
    if verbose:
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
    
    if verbose:
        file_size = os.path.getsize(output_path)
        print(f"    Map size: {file_size} bytes ({file_size / 1024:.1f} KB)")


parser = argparse.ArgumentParser(description='Process images for EL133UF1 e-ink display with optional ML-based keep-out maps.')
parser.add_argument('input_paths', nargs='+', type=str, help='Input image file(s) or directory')
parser.add_argument('--dir', choices=['landscape', 'portrait'], help='Image direction')
parser.add_argument('--mode', choices=['scale', 'cut'], default='scale')
parser.add_argument('--dither', type=int, choices=[0, 1, 3], default=1)
parser.add_argument('--brightness', type=float, default=1.1)
parser.add_argument('--contrast', type=float, default=1.2)
parser.add_argument('--saturation', type=float, default=1.2)

# New ML-based keep-out map options
parser.add_argument('--generate-maps', action='store_true',
                    help='Generate keep-out maps using ML object detection')
parser.add_argument('--map-confidence', type=float, default=0.3,
                    help='YOLO confidence threshold for object detection (0.0-1.0, default: 0.3)')
parser.add_argument('--map-expand', type=int, default=50,
                    help='Pixels to expand around detected objects (default: 50)')
parser.add_argument('--map-method', choices=['segmentation', 'boxes'], default='segmentation',
                    help='Detection method: segmentation (precise, follows outline) or boxes (faster, rectangular)')
parser.add_argument('--verbose', action='store_true',
                    help='Print detailed processing information')

args = parser.parse_args()

# Check ML availability if maps requested
if args.generate_maps and not HAS_ML:
    print("ERROR: --generate-maps requires ML libraries. Install with:")
    print("  pip install torch ultralytics opencv-python")
    sys.exit(1)

input_paths = args.input_paths
display_direction = args.dir
display_mode = args.mode
display_dither = Image.Dither(args.dither)

def process_image(image_file):
    try:
        input_image = Image.open(image_file)
        width, height = input_image.size

        if display_direction:
            target_width, target_height = TARGET_SIZE_LANDSCAPE if display_direction == 'landscape' else TARGET_SIZE_PORTRAIT
        else:
            target_width, target_height = TARGET_SIZE_LANDSCAPE if width > height else TARGET_SIZE_PORTRAIT

        # Resize/pad image to target size
        if display_mode == 'scale':
            scale_ratio = max(target_width / width, target_height / height)
            resized_width = int(width * scale_ratio)
            resized_height = int(height * scale_ratio)
            output_image = input_image.resize((resized_width, resized_height))
            resized_image = Image.new('RGB', (target_width, target_height), (255, 255, 255))
            left = (target_width - resized_width) // 2
            top = (target_height - resized_height) // 2
            resized_image.paste(output_image, (left, top))
        else:
            resized_image = ImageOps.pad(
                input_image,
                size=(target_width, target_height),
                color=(255, 255, 255),
                centering=(0.5, 0.5)
            )

        # ML object detection (before color processing for better detection)
        keep_out_mask = None
        if args.generate_maps:
            keep_out_mask = detect_objects_yolo(
                resized_image,
                confidence=args.map_confidence,
                expand_margin=args.map_expand,
                use_segmentation=(args.map_method == 'segmentation'),
                verbose=args.verbose
            )

        # Apply image enhancements
        enhanced_image = ImageEnhance.Brightness(resized_image).enhance(args.brightness)
        enhanced_image = ImageEnhance.Contrast(enhanced_image).enhance(args.contrast)
        enhanced_image = ImageEnhance.Color(enhanced_image).enhance(args.saturation)
        enhanced_image = enhanced_image.filter(ImageFilter.EDGE_ENHANCE)
        enhanced_image = enhanced_image.filter(ImageFilter.SMOOTH)
        enhanced_image = enhanced_image.filter(ImageFilter.SHARPEN)

        # Create palette image
        pal_image = Image.new("P", (1, 1))
        pal_image.putpalette(
            (0,0,0, 255,255,255, 255,255,0, 255,0,0, 0,0,255, 0,255,0)
            + (0,0,0)*249
        )

        # Quantize with selected dithering method
        if args.dither == 1:
            quantized_rgb = quantize_atkinson(enhanced_image).convert('RGB')
            quantized_p = quantized_rgb.quantize(palette=pal_image, dither=Image.Dither.NONE)
        else:
            quantized_p = enhanced_image.quantize(dither=display_dither, palette=pal_image)
            quantized_rgb = quantized_p.convert('RGB')

        # Generate output filename
        dither_label = 'ATK' if args.dither == 1 else 'FS' if args.dither == 3 else ''
        base = os.path.splitext(image_file)[0] + '_' + display_mode + ('_' + dither_label if dither_label else '') + '_output'

        # Save converted image
        # quantized_rgb.save(base + '.bmp')  # Disabled - BMP no longer needed
        quantized_p.save(base + '.png', optimize=True)

        # Save keep-out map if generated
        if keep_out_mask is not None:
            save_keepout_map(keep_out_mask, base + '.map', verbose=args.verbose)
            if args.verbose:
                print(f'  Saved: {base}.png, {base}.map')
        else:
            if args.verbose:
                print(f'  Saved: {base}.png')

        print(f'Successfully converted {image_file}')

    except Exception as e:
        print(f'Error processing {image_file}: {e}')
        if args.verbose:
            import traceback
            traceback.print_exc()

# Gather all image files
image_extensions = ['.jpg', '.jpeg', '.png', '.tiff', '.tif', '.webp', '.gif', '.heic']
all_image_files = []

for input_path in input_paths:
    if os.path.isfile(input_path):
        all_image_files.append(input_path)
    elif os.path.isdir(input_path):
        for file in os.listdir(input_path):
            if any(file.lower().endswith(ext) for ext in image_extensions):
                all_image_files.append(os.path.join(input_path, file))

if not all_image_files:
    print('Error: no valid image files to process')
    sys.exit(1)

print(f'Found {len(all_image_files)} image files to process')
if args.generate_maps:
    print(f'ML keep-out maps will be generated (confidence={args.map_confidence}, expand={args.map_expand}px)')

for image_file in tqdm(all_image_files, desc="Processing images", unit="file"):
    process_image(image_file)

print('\nProcessing complete!')
if args.generate_maps:
    print('Tip: Copy both .bmp and .map files to your SD card for intelligent text placement')

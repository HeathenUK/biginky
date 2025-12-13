#!/usr/bin/env python3
"""
Verify and visualize keep-out map files.

This utility checks map file integrity and can optionally generate
a visualization showing the keep-out areas overlaid on the original image.

Usage:
    python verify_map_file.py image.map
    python verify_map_file.py image.map --visualize image.bmp
"""

import argparse
import struct
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw
    import numpy as np
    HAS_PIL = True
except ImportError:
    HAS_PIL = False
    print("Warning: PIL not found. Install with: pip install pillow", file=sys.stderr)


def read_map_file(map_path):
    """Read and parse a keep-out map file."""
    with open(map_path, 'rb') as f:
        # Read header (16 bytes)
        magic = f.read(5)
        version = struct.unpack('B', f.read(1))[0]
        width = struct.unpack('<H', f.read(2))[0]
        height = struct.unpack('<H', f.read(2))[0]
        reserved = f.read(6)
        
        # Verify magic
        if magic != b'KOMAP':
            raise ValueError(f"Invalid magic: {magic} (expected b'KOMAP')")
        
        # Verify version
        if version != 1:
            raise ValueError(f"Unsupported version: {version}")
        
        # Read bitmap
        bitmap_size = (width * height + 7) // 8
        bitmap_data = f.read(bitmap_size)
        
        if len(bitmap_data) != bitmap_size:
            raise ValueError(f"Incomplete bitmap: got {len(bitmap_data)} bytes, expected {bitmap_size}")
        
        # Check for extra data
        extra = f.read()
        if extra:
            print(f"Warning: {len(extra)} extra bytes at end of file", file=sys.stderr)
        
        return {
            'magic': magic,
            'version': version,
            'width': width,
            'height': height,
            'bitmap': bitmap_data,
            'bitmap_size': bitmap_size,
        }


def decode_bitmap(bitmap_data, width, height):
    """Decode bitmap data into a 2D numpy array."""
    bitmap = np.zeros((height, width), dtype=np.uint8)
    
    for y in range(height):
        for x in range(width):
            pixel_idx = y * width + x
            byte_idx = pixel_idx // 8
            bit_idx = 7 - (pixel_idx % 8)  # MSB first
            
            if byte_idx < len(bitmap_data):
                if bitmap_data[byte_idx] & (1 << bit_idx):
                    bitmap[y, x] = 255
    
    return bitmap


def analyze_map(map_data):
    """Analyze map statistics."""
    width = map_data['width']
    height = map_data['height']
    
    # Decode bitmap
    bitmap = decode_bitmap(map_data['bitmap'], width, height)
    
    # Count keep-out pixels
    keep_out_pixels = np.sum(bitmap > 0)
    total_pixels = width * height
    coverage = (keep_out_pixels / total_pixels) * 100
    
    # Find bounding boxes (connected components)
    from scipy import ndimage
    try:
        labeled, num_features = ndimage.label(bitmap)
        regions = []
        
        for label in range(1, num_features + 1):
            coords = np.argwhere(labeled == label)
            if len(coords) > 0:
                y_min, x_min = coords.min(axis=0)
                y_max, x_max = coords.max(axis=0)
                area = len(coords)
                regions.append({
                    'bbox': (x_min, y_min, x_max, y_max),
                    'area': area
                })
        
        # Sort by area (largest first)
        regions.sort(key=lambda r: r['area'], reverse=True)
    except ImportError:
        regions = None
        num_features = None
    
    return {
        'width': width,
        'height': height,
        'total_pixels': total_pixels,
        'keep_out_pixels': keep_out_pixels,
        'coverage': coverage,
        'num_regions': num_features,
        'regions': regions,
        'bitmap': bitmap,
    }


def visualize_map(map_data, analysis, bmp_path, output_path):
    """Generate visualization of keep-out areas over the image."""
    if not HAS_PIL:
        print("Error: PIL required for visualization. Install: pip install pillow", file=sys.stderr)
        return False
    
    # Load BMP
    img = Image.open(bmp_path).convert('RGB')
    
    # Resize map to match image if needed
    if img.size != (analysis['width'], analysis['height']):
        print(f"Warning: Image size {img.size} != map size ({analysis['width']}, {analysis['height']})")
        # Resize image to match map
        img = img.resize((analysis['width'], analysis['height']), Image.Resampling.LANCZOS)
    
    # Create overlay
    overlay = Image.new('RGBA', img.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(overlay)
    
    # Draw keep-out areas (semi-transparent red)
    bitmap = analysis['bitmap']
    for y in range(bitmap.shape[0]):
        for x in range(bitmap.shape[1]):
            if bitmap[y, x] > 0:
                draw.point((x, y), fill=(255, 0, 0, 128))
    
    # Draw bounding boxes if available
    if analysis['regions']:
        for i, region in enumerate(analysis['regions'][:10]):  # Top 10 regions
            x1, y1, x2, y2 = region['bbox']
            # Draw rectangle
            draw.rectangle([x1, y1, x2, y2], outline=(0, 255, 0, 255), width=2)
            # Draw label
            label = f"#{i+1} ({region['area']}px)"
            draw.text((x1 + 5, y1 + 5), label, fill=(0, 255, 0, 255))
    
    # Composite
    img = img.convert('RGBA')
    result = Image.alpha_composite(img, overlay)
    result = result.convert('RGB')
    
    # Save
    result.save(output_path)
    print(f"Visualization saved: {output_path}")
    
    return True


def main():
    parser = argparse.ArgumentParser(
        description='Verify and visualize keep-out map files',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Basic verification
  python verify_map_file.py landscape.map

  # Generate visualization
  python verify_map_file.py landscape.map --visualize landscape.bmp
  
  # Custom output path
  python verify_map_file.py landscape.map --visualize landscape.bmp --output viz.png
        """
    )
    
    parser.add_argument('map_file', help='Keep-out map file to verify (.map)')
    parser.add_argument('--visualize', metavar='BMP', help='Generate visualization with BMP overlay')
    parser.add_argument('--output', '-o', help='Output path for visualization (default: <map>_viz.png)')
    
    args = parser.parse_args()
    
    # Check file exists
    if not Path(args.map_file).is_file():
        print(f"Error: File not found: {args.map_file}", file=sys.stderr)
        return 1
    
    print("=" * 60)
    print("  Keep-Out Map File Verification")
    print("=" * 60)
    print(f"\nFile: {args.map_file}")
    print(f"Size: {Path(args.map_file).stat().st_size} bytes ({Path(args.map_file).stat().st_size / 1024:.1f} KB)")
    print()
    
    # Read and verify map
    try:
        map_data = read_map_file(args.map_file)
    except Exception as e:
        print(f"ERROR: Failed to read map file: {e}", file=sys.stderr)
        return 1
    
    print("✓ File format valid")
    print()
    
    # Display header info
    print("Header:")
    print(f"  Magic:    {map_data['magic'].decode('ascii')}")
    print(f"  Version:  {map_data['version']}")
    print(f"  Width:    {map_data['width']} px")
    print(f"  Height:   {map_data['height']} px")
    print(f"  Bitmap:   {map_data['bitmap_size']} bytes")
    print()
    
    # Analyze map
    print("Analyzing map...")
    try:
        analysis = analyze_map(map_data)
    except Exception as e:
        print(f"ERROR: Analysis failed: {e}", file=sys.stderr)
        return 1
    
    print()
    print("Statistics:")
    print(f"  Total pixels:     {analysis['total_pixels']:,}")
    print(f"  Keep-out pixels:  {analysis['keep_out_pixels']:,}")
    print(f"  Coverage:         {analysis['coverage']:.2f}%")
    
    if analysis['num_regions'] is not None:
        print(f"  Detected regions: {analysis['num_regions']}")
        
        if analysis['regions']:
            print()
            print("Top 5 largest regions:")
            for i, region in enumerate(analysis['regions'][:5]):
                x1, y1, x2, y2 = region['bbox']
                w = x2 - x1 + 1
                h = y2 - y1 + 1
                print(f"    #{i+1}: [{x1},{y1}]-[{x2},{y2}] ({w}×{h}, {region['area']} px)")
    
    print()
    
    # Generate visualization if requested
    if args.visualize:
        if not Path(args.visualize).is_file():
            print(f"ERROR: BMP file not found: {args.visualize}", file=sys.stderr)
            return 1
        
        output_path = args.output
        if not output_path:
            # Default: same name with _viz.png suffix
            output_path = Path(args.map_file).stem + '_viz.png'
        
        print("Generating visualization...")
        try:
            if visualize_map(map_data, analysis, args.visualize, output_path):
                print("✓ Visualization complete")
            else:
                print("✗ Visualization failed")
                return 1
        except Exception as e:
            print(f"ERROR: Visualization failed: {e}", file=sys.stderr)
            import traceback
            traceback.print_exc()
            return 1
    
    print()
    print("=" * 60)
    print("✓ Verification complete")
    print("=" * 60)
    
    return 0


if __name__ == '__main__':
    sys.exit(main())

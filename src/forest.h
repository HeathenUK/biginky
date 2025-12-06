// Forest background image for EL133UF1 display
// 
// Generate this file from a BMP image using xxd:
//   xxd -i forest.bmp > forest.h
//
// BMP requirements for best results:
// - Resolution: 1600x1200 (display native) or will be drawn at top-left
// - Color depth: 24-bit or 8-bit indexed
// - Format: Uncompressed BMP (no RLE)
// 
// Color mapping: The BMP loader maps RGB colors to the nearest of
// the 6 Spectra colors: Black, White, Yellow, Red, Blue, Green
//
// Placeholder data (white 4x4 BMP) - replace with your actual image
// This is a minimal valid BMP to prevent compilation errors

static const unsigned char forest_bmp[] = {
    // BMP Header (14 bytes)
    0x42, 0x4D,             // 'BM' magic
    0x46, 0x00, 0x00, 0x00, // File size: 70 bytes
    0x00, 0x00, 0x00, 0x00, // Reserved
    0x36, 0x00, 0x00, 0x00, // Pixel data offset: 54 bytes
    // DIB Header (40 bytes - BITMAPINFOHEADER)
    0x28, 0x00, 0x00, 0x00, // DIB header size: 40 bytes
    0x04, 0x00, 0x00, 0x00, // Width: 4 pixels
    0x04, 0x00, 0x00, 0x00, // Height: 4 pixels (positive = bottom-up)
    0x01, 0x00,             // Planes: 1
    0x18, 0x00,             // Bits per pixel: 24
    0x00, 0x00, 0x00, 0x00, // Compression: none
    0x10, 0x00, 0x00, 0x00, // Image size: 16 bytes (4*4 pixels, no padding)
    0x13, 0x0B, 0x00, 0x00, // X pixels per meter
    0x13, 0x0B, 0x00, 0x00, // Y pixels per meter
    0x00, 0x00, 0x00, 0x00, // Colors in palette
    0x00, 0x00, 0x00, 0x00, // Important colors
    // Pixel data (16 pixels, 3 bytes each = 48 bytes, but rows padded to 4-byte boundary)
    // Row 0 (bottom): 4 white pixels (BGR: FF FF FF), no padding needed (12 bytes)
    0xFF, 0xFF, 0xFF,  0xFF, 0xFF, 0xFF,  0xFF, 0xFF, 0xFF,  0xFF, 0xFF, 0xFF,
    // Row 1: 4 white pixels
    0xFF, 0xFF, 0xFF,  0xFF, 0xFF, 0xFF,  0xFF, 0xFF, 0xFF,  0xFF, 0xFF, 0xFF,
    // Row 2: 4 white pixels
    0xFF, 0xFF, 0xFF,  0xFF, 0xFF, 0xFF,  0xFF, 0xFF, 0xFF,  0xFF, 0xFF, 0xFF,
    // Row 3 (top): 4 white pixels
    0xFF, 0xFF, 0xFF,  0xFF, 0xFF, 0xFF,  0xFF, 0xFF, 0xFF,  0xFF, 0xFF, 0xFF,
};

static const unsigned int forest_bmp_len = sizeof(forest_bmp);

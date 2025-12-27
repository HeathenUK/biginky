# Web Assets Embedding

The management interface HTML/CSS/JS is now stored in `web/index.html` and automatically embedded into the firmware at compile time.

## How It Works

1. **Source File**: `web/index.html` contains the complete HTML with inline CSS and JavaScript
2. **Build Script**: `scripts/generate_web_header.py` converts the HTML file to `src/web_assets.h` at build time
3. **C++ Code**: Includes `web_assets.h` and uses `WEB_HTML_CONTENT` to serve the page

## Benefits

- ✅ No stack overflow issues (HTML is in flash, not generated on stack)
- ✅ Easy to edit (just edit `web/index.html` with any text editor)
- ✅ Syntax highlighting and validation in HTML files
- ✅ No need to escape quotes or handle string concatenation
- ✅ Cleaner, more maintainable code

## Editing the Interface

Simply edit `web/index.html` and rebuild. The build script will automatically regenerate the header file.

## File Structure

```
web/
  └── index.html          # Complete HTML with inline CSS/JS

src/
  └── web_assets.h        # Auto-generated header (do not edit manually)

scripts/
  └── generate_web_header.py  # Build script to generate header
```


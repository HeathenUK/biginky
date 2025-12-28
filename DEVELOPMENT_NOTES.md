# Development Notes - Critical Mistakes to Avoid

## Certificate Generation Fiasco (Dec 28, 2024)

### What Went Wrong

1. **Initial Stub Creation**: Created a stub `certificates.h` file instead of properly generating real certificates. This caused `mbedtls_x509_crt_parse` errors because the certificate data was invalid/empty.

2. **Overcomplicated Fix**: When fixing the certificate issue, completely rewrote `generate_cert_header.py` using PlatformIO's `Import("env")` pattern, when the real issue was just the certificate format. Should have made minimal changes to fix the PEM format only.

3. **WiFi Web UI Deletion**: The `generate_web_header.py` script had a "helpful" fallback that created a minimal default HTML file if `web/index.html` was missing. At some point the file was deleted (possibly during operations), and when the build ran, the script silently replaced the full 242-line WiFi management interface with a 12-line stub.

4. **EL133UF1.cpp Deletion**: Accidentally deleted `lib/EL133UF1/EL133UF1.cpp`, causing linker errors for undefined references to `setPixel` and `argbToColor`.

### Root Causes

- **Didn't verify file state before changes**: Should have checked what files existed and their contents before making modifications
- **Created "helpful" fallbacks that caused problems**: The default HTML generation seemed helpful but silently destroyed the real interface
- **Overcomplicated fixes**: Rewrote entire scripts when minimal targeted fixes would have sufficed
- **No verification after changes**: Didn't verify that critical files still existed after operations

### Lessons Learned

1. **NEVER create fallback/stub files that overwrite real content** - Always fail loudly if required files are missing
2. **Make minimal, targeted fixes** - Don't rewrite entire scripts unless absolutely necessary
3. **Verify file state before and after operations** - Check what exists, what's modified, what's deleted
4. **Test immediately after fixes** - Run builds to verify nothing broke
5. **Be explicit about file operations** - Know exactly which files are being read/written/deleted

### Fixes Applied

- Removed fallback HTML generation from `generate_web_header.py` - now fails if file is missing
- Fixed certificate generation to produce proper PEM format with full headers/footers
- Restored all deleted files from git history
- Added aliases in certificates.h for compatibility

### Prevention Checklist

Before making changes:
- [ ] Check what files exist in the target directory
- [ ] Verify current state of files that will be modified
- [ ] Understand the full scope of what a script does before modifying it
- [ ] Check git status to see what's already modified/deleted

After making changes:
- [ ] Verify critical files still exist
- [ ] Run a build to ensure nothing broke
- [ ] Check git status for unexpected deletions
- [ ] Verify file sizes/content match expectations


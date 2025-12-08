"""
PlatformIO pre-build script for EL133UF1 display project.

Tasks:
1. Regenerate color LUT if the generator script changed
2. Patch PSRAM speed in board variant for APS6404L-3SQR (133MHz)
"""

Import("env")
import os
import re
import subprocess
import sys

def check_and_regenerate_lut(source, target, env):
    """Check if LUT needs regeneration and regenerate if needed."""
    
    project_dir = env.subst("$PROJECT_DIR")
    script_path = os.path.join(project_dir, "scripts", "generate_color_lut.py")
    lut_path = os.path.join(project_dir, "lib", "EL133UF1", "EL133UF1_ColorLUT.h")
    
    # Check if LUT exists
    if not os.path.exists(lut_path):
        print("Color LUT not found, generating...")
        regenerate = True
    # Check if script is newer than LUT
    elif os.path.getmtime(script_path) > os.path.getmtime(lut_path):
        print("Color LUT generator updated, regenerating...")
        regenerate = True
    else:
        regenerate = False
    
    if regenerate:
        try:
            result = subprocess.run(
                [sys.executable, script_path, lut_path],
                capture_output=True,
                text=True,
                cwd=project_dir
            )
            if result.returncode == 0:
                print("Color LUT generated successfully")
            else:
                print(f"Warning: LUT generation failed: {result.stderr}")
        except Exception as e:
            print(f"Warning: Could not regenerate LUT: {e}")


def patch_psram_speed(source, target, env):
    """
    Patch the board variant's pins_arduino.h to use correct PSRAM speed.
    
    The APS6404L-3SQR PSRAM is rated for 133MHz, but the default board
    variant uses 109MHz. This patches it to use the correct speed.
    """
    # Find the variant's pins_arduino.h
    framework_dir = env.PioPlatform().get_package_dir("framework-arduinopico")
    variant_path = os.path.join(framework_dir, "variants", "pimoroni_pico_plus_2w", "pins_arduino.h")
    
    if not os.path.exists(variant_path):
        print(f"Warning: Could not find {variant_path}")
        return
    
    # Read current content
    with open(variant_path, 'r') as f:
        content = f.read()
    
    # Check if already patched or needs patching
    old_pattern = r'#define\s+RP2350_PSRAM_MAX_SCK_HZ\s+\(109\s*\*\s*1000\s*\*\s*1000\)'
    new_value = '#define RP2350_PSRAM_MAX_SCK_HZ (133*1000*1000)  // APS6404L-3SQR rated speed'
    
    if re.search(old_pattern, content):
        # Patch needed
        new_content = re.sub(old_pattern, new_value, content)
        with open(variant_path, 'w') as f:
            f.write(new_content)
        print("PSRAM speed patched: 109MHz -> 133MHz (APS6404L-3SQR)")
    elif '133*1000*1000' in content:
        print("PSRAM speed already set to 133MHz")
    else:
        print("Warning: Could not find PSRAM speed definition to patch")


# Register pre-build actions
env.AddPreAction("buildprog", check_and_regenerate_lut)
env.AddPreAction("buildprog", patch_psram_speed)

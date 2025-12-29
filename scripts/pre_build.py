"""
PlatformIO pre-build script to regenerate color LUT if needed.

This script checks if the LUT header is older than the generator script
and regenerates it if necessary.
Also touches the main source file to force build date update.
"""

Import("env")
import os
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

def touch_main_file(source, target, env):
    """Touch main source file to force build date update on every build."""
    project_dir = env.subst("$PROJECT_DIR")
    # Touch the main ESP32-P4 source file to force recompilation
    # This ensures __DATE__ and __TIME__ macros are updated
    main_file = os.path.join(project_dir, "src", "main.cpp")
    if os.path.exists(main_file):
        os.utime(main_file, None)
        print("Touched main source file to update build date")

# Register the pre-build actions
env.AddPreAction("buildprog", check_and_regenerate_lut)
env.AddPreAction("buildprog", touch_main_file)

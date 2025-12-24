"""
PlatformIO pre-build script to exclude ESP8266Audio MIDI files for ESP32-P4.

ESP8266Audio MIDI support has compilation errors on ESP32-P4 (RISC-V architecture).
This script temporarily renames the MIDI files to exclude them from compilation.
"""

Import("env")

import os
import shutil
from pathlib import Path

def exclude_midi_files(source, target, env):
    """Exclude ESP8266Audio MIDI files from compilation."""
    
    # Get the library directory
    project_dir = env.subst("$PROJECT_DIR")
    env_name = env.subst("$PIOENV")
    lib_path = Path(project_dir) / ".pio" / "libdeps" / env_name / "ESP8266Audio" / "src"
    
    print(f"Checking for ESP8266Audio library at: {lib_path}")
    
    if not lib_path.exists():
        print(f"ESP8266Audio library not found at {lib_path}")
        return
    
    # Files to exclude
    midi_files = [
        "AudioGeneratorMIDI.cpp",
        "AudioGeneratorMIDI.h"
    ]
    
    # Rename files to exclude them (add .disabled extension)
    for filename in midi_files:
        file_path = lib_path / filename
        disabled_path = lib_path / f"{filename}.disabled"
        
        if file_path.exists() and not disabled_path.exists():
            print(f"Excluding ESP8266Audio MIDI file: {filename}")
            file_path.rename(disabled_path)
        elif file_path.exists():
            # Already disabled, remove the original
            file_path.unlink()
            print(f"ESP8266Audio MIDI file already excluded: {filename}")
        elif disabled_path.exists():
            print(f"ESP8266Audio MIDI file already excluded: {filename}")

# Register the pre-build action - run before any compilation
# Use $PROGPATH which runs early in the build process
def run_before_build():
    """Run the exclusion function early in the build process."""
    exclude_midi_files(None, None, env)

# Run immediately when script is loaded (before build starts)
run_before_build()

# Also register as pre-action for buildprog as backup
env.AddPreAction("buildprog", exclude_midi_files)


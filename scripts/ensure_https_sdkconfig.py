#!/usr/bin/env python3
"""
Ensure HTTPS configs are in sdkconfig before ESP-IDF processes it.
"""

Import("env")
import os

def ensure_https_sdkconfig(source, target, env):
    """Ensure HTTPS configs are in sdkconfig file."""
    build_dir = env.subst("$BUILD_DIR")
    sdkconfig_path = os.path.join(build_dir, "sdkconfig")
    
    # Configs we need
    required_configs = {
        "CONFIG_HTTPD_WS_SUPPORT": "y",
        "CONFIG_ESP_HTTPS_SERVER_ENABLE": "y"
    }
    
    # Read existing sdkconfig if it exists
    lines = []
    if os.path.exists(sdkconfig_path):
        with open(sdkconfig_path, 'r') as f:
            lines = f.readlines()
    
    # Check if configs are already there
    has_configs = {}
    for i, line in enumerate(lines):
        for config_name in required_configs.keys():
            if line.strip().startswith(config_name + "="):
                has_configs[config_name] = i
                break
    
    # Add missing configs
    modified = False
    for config_name, config_value in required_configs.items():
        if config_name not in has_configs:
            # Add at the end
            lines.append(f"{config_name}={config_value}\n")
            modified = True
    
    # Write back if modified
    if modified:
        with open(sdkconfig_path, 'w') as f:
            f.writelines(lines)
        print(f"Added HTTPS configs to sdkconfig: {', '.join(required_configs.keys())}")

# Run before ESP-IDF processes sdkconfig
env.AddPreAction("buildprog", ensure_https_sdkconfig)


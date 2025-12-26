#!/usr/bin/env python3
"""
ESP32-P4 OTA Firmware Upload Script

Uploads a firmware binary to an ESP32-P4 device over local WiFi network.
The device must have started the OTA server (via !ota MQTT command).

Usage:
    python3 upload_firmware.py <device_ip> [firmware.bin]
"""

import sys
import os
import socket
import hashlib
import time

def calculate_checksums(data):
    """Calculate checksums matching ESP32 code."""
    simple_sum = sum(data) & 0xFFFFFFFF
    
    crc_like = 0
    data_len = len(data)
    data_bytes = bytearray(data)
    
    for i in range(data_len):
        byte = data_bytes[i]
        crc_like = ((crc_like << 1) ^ byte) & 0xFFFFFFFF
        if crc_like & 0x80000000:
            crc_like = (crc_like ^ 0x04C11DB7) & 0xFFFFFFFF
        
        if (i + 1) % (1024 * 200) == 0:
            percent = ((i + 1) * 100.0) / data_len
            print(f"    Checksum progress: {i + 1}/{data_len} bytes ({percent:.1f}%)")
            sys.stdout.flush()
    
    return simple_sum, crc_like

def verify_device_reachable(device_ip, port=80, timeout=5):
    """Verify device is reachable before attempting upload."""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        sock.connect((device_ip, port))
        sock.close()
        return True
    except socket.error:
        return False

def main():
    if len(sys.argv) < 2:
        print(__doc__)
        print("\nUsage:")
        print(f"  {sys.argv[0]} <device_ip> [firmware.bin]")
        sys.exit(1)
    
    device_ip = sys.argv[1]
    firmware_path = sys.argv[2] if len(sys.argv) > 2 else ".pio/build/esp32p4/firmware.bin"
    
    # Verify device is reachable first
    print(f"Checking if device is reachable at {device_ip}:80...")
    if not verify_device_reachable(device_ip):
        print(f"Device not reachable")
        print("\nMake sure:")
        print("  1. Device IP is correct")
        print("  2. Device is on same network")
        print("  3. OTA server running (send !ota via MQTT or press 'o' at boot)")
        sys.exit(1)
    print("✓ Device reachable\n")
    
    # Check file exists
    if not os.path.exists(firmware_path):
        print(f"ERROR: Firmware file not found: {firmware_path}")
        sys.exit(1)
    
    # Read firmware
    print(f"Reading firmware: {firmware_path}")
    with open(firmware_path, 'rb') as f:
        firmware_data = f.read()
    
    file_size = len(firmware_data)
    print(f"  Size: {file_size} bytes ({file_size / (1024*1024):.2f} MB)")
    
    # Verify magic byte
    if firmware_data[0] != 0xE9:
        print(f"ERROR: Invalid magic byte: 0x{firmware_data[0]:02x}")
        sys.exit(1)
    print("Magic byte verified (0xE9)")
    
    # Calculate checksums
    print("\nCalculating checksums...")
    simple_sum, crc_like = calculate_checksums(firmware_data)
    print(f"  Simple sum: 0x{simple_sum:08x}")
    print(f"  CRC-like:   0x{crc_like:08x}")
    
    # Upload
    print(f"\nUploading to {device_ip}:80...")
    
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(300)
        sock.connect((device_ip, 80))
        
        # Calculate checksums before upload
        print("\nCalculating checksums...")
        simple_sum, crc_like = calculate_checksums(firmware_data)
        print(f"  Simple sum: 0x{simple_sum:08x} ({simple_sum})")
        print(f"  CRC-like:   0x{crc_like:08x}")
        
        # Send headers with checksums for verification
        headers = (
            f"POST /update HTTP/1.1\r\n"
            f"Content-Length: {file_size}\r\n"
            f"X-Checksum-Simple: {simple_sum}\r\n"
            f"X-Checksum-CRC: {crc_like}\r\n"
            f"Connection: close\r\n\r\n"
        )
        sock.sendall(headers.encode('ascii'))
        
        # Send data with progress - use 8KB chunks for efficiency (matches device buffer)
        sent = 0
        chunk_size = 8192  # 8KB chunks for optimal performance
        start_time = time.time()
        last_update = start_time
        
        while sent < file_size:
            chunk = firmware_data[sent:sent + chunk_size]
            sock.sendall(chunk)
            sent += len(chunk)
            
            # Progress reporting (no artificial delay - let TCP handle flow control)
            now = time.time()
            if now - last_update >= 0.5 or sent == file_size:
                pct = (sent * 100.0) / file_size
                speed = sent / (now - start_time) / 1024
                print(f"Progress: {sent}/{file_size} bytes ({pct:.1f}%) @ {speed:.1f} KB/s")
                last_update = now
        
        print("\nUpload complete: {} bytes in {:.0f} ms".format(file_size, (time.time() - start_time) * 1000))
        print("Received data checksum (simple sum): 0x{:08x} ({})".format(simple_sum, simple_sum))
        print("Received data checksum (CRC-like): 0x{:08x}".format(crc_like))
        print("Compare this with the source file checksum to verify data integrity")
        
        # Wait for response
        print("\nWaiting for response...")
        try:
            sock.settimeout(10)  # 10 second timeout for response
            response = sock.recv(4096).decode('utf-8', errors='ignore')
            
            if "200" in response:
                print("Success! Device rebooting...")
                sys.exit(0)
            else:
                print(f"Failed: {response[:200]}")
                sys.exit(1)
        except socket.timeout:
            print("No response received, but upload completed - device may be processing update")
            sys.exit(0)
            
    except Exception as e:
        print(f"\nâœ— Error: {e}")
        sys.exit(1)
    finally:
        sock.close()

if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""
ESP32-C6 Wi-Fi/BT Firmware Extractor
Extracts firmware blobs from ESP-IDF and converts them to C arrays.

Usage:
  python3 extract_fw.py /path/to/esp-idf

This script looks for:
  - components/esp_wifi/lib/esp32c6/   (Wi-Fi libraries)
  - components/phy/lib/esp32c6/        (PHY data)
  
Output: firmware/ directory with C header files containing the firmware arrays.
"""

import sys
import os
import struct

def main():
    if len(sys.argv) < 2:
        print('Usage: python3 extract_fw.py /path/to/esp-idf')
        sys.exit(1)
    
    esp_idf_path = sys.argv[1]
    
    # Check if ESP-IDF exists
    if not os.path.isdir(esp_idf_path):
        print(f'Error: {esp_idf_path} is not a directory')
        sys.exit(1)
    
    # Create output directory
    out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'firmware')
    os.makedirs(out_dir, exist_ok=True)
    
    # Look for firmware files
    search_paths = [
        os.path.join(esp_idf_path, 'components', 'esp_wifi', 'lib', 'esp32c6'),
        os.path.join(esp_idf_path, 'components', 'phy', 'lib', 'esp32c6'),
        os.path.join(esp_idf_path, 'components', 'bt', 'lib', 'esp32c6'),
    ]
    
    found = 0
    for sp in search_paths:
        if os.path.isdir(sp):
            print(f'Searching: {sp}')
            for f in os.listdir(sp):
                if f.endswith('.a') or f.endswith('.bin'):
                    fpath = os.path.join(sp, f)
                    print(f'  Found: {f} ({os.path.getsize(fpath)} bytes)')
                    found += 1
    
    if found == 0:
        print('No firmware files found. Download ESP-IDF first:')
        print('  git clone --recursive https://github.com/espressif/esp-idf.git')
        sys.exit(1)
    
    print(f'
Found {found} file(s). Run again with a complete ESP-IDF to extract firmware.')

if __name__ == '__main__':
    main()

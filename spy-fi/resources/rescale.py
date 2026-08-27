#!/usr/bin/env python3
"""
Scale 12-bit signed ADC samples (stored as int16_t) to full 16-bit range.

Usage:
    python rescale.py input.bin output.bin
"""

import sys
import argparse
import numpy as np
from pathlib import Path
import sys
ScaleToBits = 16
ScaleFromBits = 12

def main():
    if len(sys.argv) != 3:
        print("Usage: python rescale.py <input> <output>")
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2]

    # Read input
    try:
        data = np.fromfile(input_file, dtype=np.int16)
    except Exception as e:
        print(f"Error reading input file: {e}")
        sys.exit(1)

    if len(data) == 0:
        print("Error: Empty file")
        sys.exit(1)

    #  Analysis
    dc_offset = np.median(data)
    print(f'DC offset: {dc_offset}')
    
    data_float = data.astype(np.float64)
    data_centered = data_float - dc_offset
    # Scale to full 16-bit range (12-bit → 16-bit)
    scale_factor = 2 ** (ScaleToBits - ScaleFromBits)   # = 16
    data_scaled = data_centered * scale_factor

    # Convert back to int16 (with safe clipping)
    data_16bit = np.clip(data_scaled, -32768, 32767).astype(np.int16)
    
    data_16bit.tofile(output_file)
    
    
    print(f"Processed {len(data_16bit)} samples.")
    print(f"12-bit -> 16-bit scaling complete.")
    print(f"Output written to: {output_file}")


if __name__ == "__main__":
    main()

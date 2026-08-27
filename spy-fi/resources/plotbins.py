#!/usr/bin/env python3
'''
    plotbins lets you ... plot the bins, specifically
    the binary files captured as training samples 
    from keycapturebust.py

'''
import numpy as np
import matplotlib.pyplot as plt
from pathlib import Path
import sys

def plot_uint16_files(*files, max_points=None, sharex=True):
    """Plot each raw little-endian uint16 file in its own subplot, stacked vertically."""
    n = len(files)
    if n == 0:
        print("No files given.")
        return

    fig, axes = plt.subplots(n, 1, figsize=(12, 3 * n), sharex=sharex, squeeze=False)
    fig.patch.set_facecolor('black')
    
    phosphor = '#33FF33'  
    for ax, f in zip(axes.flat, files):
        ax.set_facecolor('black')
        for spine in ax.spines.values():
            spine.set_color(phosphor)
            spine.set_linewidth(1.5)
            
        ax.xaxis.label.set_color(phosphor)
        ax.yaxis.label.set_color(phosphor)
        path = Path(f)
        data = np.fromfile(path, dtype='<u2')  # little-endian uint16
        
        if max_points and len(data) > max_points:
            data = data[:max_points]
        
        ax.plot(data, color=phosphor, linewidth=2)
        ax.set_ylabel('Value (uint16)')
        ax.set_title(path.name)
        ax.grid(True, alpha=0.3)
    
    axes[-1, 0].set_xlabel('Sample index')
    fig.tight_layout()
    plt.show()

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python plotbins.py file1.bin [file2.bin ...]")
        sys.exit(1)
    
    plot_uint16_files(*sys.argv[1:])

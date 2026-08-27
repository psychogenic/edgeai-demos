#!/usr/bin/env python3
"""
Plot selected channels from binary IMU records.

Each record is 6 x int16_t (little-endian):
    accel_x  accel_y  accel_z  gyro_x  gyro_y  gyro_z
"""

import argparse
import sys
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib import colors as mcolors
import numpy as np
import random 

colorsBuiltIn = dict(mcolors.BASE_COLORS, **mcolors.CSS4_COLORS)
colors = {
    "crimson": "#DC143C",
    "tomato": "#FF6347",
    "coral": "#FF7F50",
    "orange_red": "#FF4500",
    "dark_orange": "#FF8C00",
    "orange": "#FFA500",
    "gold": "#FFD700",
    "yellow": "#FFD700",
    "khaki": "#F0E68C",
    "lime": "#00FF00",
    "lime_green": "#32CD32",
    "spring_green": "#00FF7F",
    "medium_spring_green": "#00FA9A",
    "sea_green": "#2E8B57",
    "medium_sea_green": "#3CB371",
    "light_sea_green": "#20B2AA",
    "turquoise": "#40E0D0",
    "medium_turquoise": "#48D1CC",
    "aqua": "#00FFFF",
    "cyan": "#00FFFF",
    "deep_sky_blue": "#00BFFF",
    "dodger_blue": "#1E90FF",
    "royal_blue": "#4169E1",
    "blue": "#0000FF",
    "medium_blue": "#0000CD",
    "slate_blue": "#6A5ACD",
    "medium_slate_blue": "#7B68EE",
    "medium_purple": "#9370DB",
    "blue_violet": "#8A2BE2",
    "orchid": "#DA70D6",
    "medium_orchid": "#BA55D3",
    "violet": "#EE82EE",
    "magenta": "#FF00FF",
    "fuchsia": "#FF00FF",
    "deep_pink": "#FF1493",
    "hot_pink": "#FF69B4",
    "pink": "#FFC0CB",
    "light_pink": "#FFB6C1",
    "salmon": "#FA8072",
    "light_salmon": "#FFA07A",
}
AssignedColors = dict()
def colorForChannel(chname:str):
    global AssignedColors
    
    if not chname or not chname in AssignedColors:
        sel = random.choice(list(colors.values()))
        while sel in AssignedColors.values():
            sel = random.choice(list(colors.values()))
        #print(sel)
        AssignedColors[chname] = sel
        
    return AssignedColors[chname]

def read_records(path: Path) -> np.ndarray:
    """Read binary file (N, 6) array of int16 values."""
    raw = path.read_bytes()
    if len(raw) % 12 != 0:
        print(f"Warning: {path} size ({len(raw)} bytes) is not a multiple of 12; "
              f"truncating extra bytes.", file=sys.stderr)
    data = np.frombuffer(raw, dtype="<i2")
    n = len(data) // 6
    return data[: n * 6].reshape(n, 6)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Plot selected accelerometer / gyroscope channels from binary records."
    )
    parser.add_argument("--ax", action="store_true", help="plot accel X")
    parser.add_argument("--ay", action="store_true", help="plot accel Y")
    parser.add_argument("--az", action="store_true", help="plot accel Z")
    parser.add_argument("--gx", action="store_true", help="plot gyro X")
    parser.add_argument("--gy", action="store_true", help="plot gyro Y")
    parser.add_argument("--gz", action="store_true", help="plot gyro Z")
    parser.add_argument("--single", action="store_true", help="Single Plot")
    parser.add_argument("--unified_color", "-c", action="store_true", help="Single color per type")
    parser.add_argument("--accel", action="store_true",
                        help="plot accelerometer vector magnitude")
    parser.add_argument("--gyro", action="store_true",
                        help="plot gyroscope vector magnitude")
    parser.add_argument("input_files", nargs="+", type=Path,
                        help="one or more binary data files")
    args = parser.parse_args()


    channels = []
    if args.ax:
        channels.append(("accel_x", lambda d: d[:, 0]))
    if args.ay:
        channels.append(("accel_y", lambda d: d[:, 1]))
    if args.az:
        channels.append(("accel_z", lambda d: d[:, 2]))
    if args.gx:
        channels.append(("gyro_x",  lambda d: d[:, 3]))
    if args.gy:
        channels.append(("gyro_y",  lambda d: d[:, 4]))
    if args.gz:
        channels.append(("gyro_z",  lambda d: d[:, 5]))
    if args.accel:
        # Cast to float *before* squaring to avoid int16 overflow
        channels.append(("accel_mag",
                         lambda d: np.sqrt(np.sum(d[:, 0:3].astype(np.float64)**2, axis=1))))
    if args.gyro:
        channels.append(("gyro_mag",
                         lambda d: np.sqrt(np.sum(d[:, 3:6].astype(np.float64)**2, axis=1))))

    # Default: everything when no flags given
    if not channels:
        channels = [
            ("accel_x", lambda d: d[:, 0]),
            ("accel_y", lambda d: d[:, 1]),
            ("accel_z", lambda d: d[:, 2]),
            ("gyro_x",  lambda d: d[:, 3]),
            ("gyro_y",  lambda d: d[:, 4]),
            ("gyro_z",  lambda d: d[:, 5]),
            ("accel_mag",
             lambda d: np.sqrt(np.sum(d[:, 0:3].astype(np.float64)**2, axis=1))),
            ("gyro_mag",
             lambda d: np.sqrt(np.sum(d[:, 3:6].astype(np.float64)**2, axis=1))),
        ]

    singlePlot = args.single
    numPlots = 0
    for path in args.input_files:
        if not path.is_file():
            print(f"Error: {path} is not a file – skipping.", file=sys.stderr)
            continue

        data = read_records(path)
        if data.size == 0:
            print(f"Warning: {path} contains no complete records – skipping.",
                  file=sys.stderr)
            continue

        if singlePlot and numPlots:
            pass 
        else:
            plt.figure(figsize=(12, 8))
            numPlots += 1
        x = np.arange(len(data))

        for label, extractor in channels:
            y = extractor(data)
            lw = 1
            if label == 'accel_mag' or label == 'gyro_mag':
                lw = 1.8
            if singlePlot:
                lb = f'{label} {str(path)}'
            else:
                lb = label
                
            if args.unified_color:
                col = colorForChannel(label)
            else:
                col = colorForChannel(0)
            plt.plot(x, y, label=lb, linewidth=lw, color=col)

        if not singlePlot:
            plt.title(str(path))
        plt.xlabel("sample index")
        plt.ylabel("raw value (int16) / magnitude")
        plt.legend(loc="best")
        plt.grid(True, alpha=0.3)
        plt.tight_layout()

    plt.show()


if __name__ == "__main__":
    main()
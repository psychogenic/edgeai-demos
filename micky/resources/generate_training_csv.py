#!/usr/bin/env python3

import argparse
import csv
import re
import struct
from collections import defaultdict
from pathlib import Path

import zipfile
import os

def create_single_file_zip(src_path: str, zip_path: str | None = None) -> str:
    if not os.path.isfile(src_path):
        raise FileNotFoundError(f"Source file does not exist: {src_path}")

    basename = os.path.basename(src_path)          # "abc.csv"
    if zip_path is None:
        stem = os.path.splitext(basename)[0]       # "abc"
        zip_path = f"/tmp/{stem}.zip"

    with zipfile.ZipFile(zip_path, mode="w", compression=zipfile.ZIP_DEFLATED) as zf:
        zf.write(src_path, arcname=basename)

    return zip_path











# Counters for reporting

class StateTracker:
    def __init__(self, unique_targets:set, unique_sessions:set):
        self.target_file_count = defaultdict(int)
        self.session_file_count = defaultdict(int)
        
        self.sorted_targets = sorted(unique_targets)
        self.target_to_class = {t: i for i, t in enumerate(self.sorted_targets)}

        self.sorted_sessions = sorted(unique_sessions)
        self.session_to_id = {s: i for i, s in enumerate(self.sorted_sessions)}
        
        self.total_samples = 0
        

def write_files_to_csv(state:StateTracker, output_path, files_to_process):
    
    # Open CSV for writing
    full_output_name = f'{output_path}.csv'
    print(f"Writing samples to: {full_output_name}")
    with open(full_output_name, "w", newline="", encoding="utf-8") as csvfile:
        writer = csv.writer(csvfile)
        writer.writerow(["ax", "ay", "az", "gx", "gy", "gz", "class", "session"])

        total_samples = 0

        for bin_path, target, session in files_to_process:
            class_id = state.target_to_class[target]
            sess_id = state.session_to_id[session]

            state.target_file_count[target] += 1
            state.session_file_count[session] += 1

            try:
                with open(bin_path, "rb") as f:
                    data = f.read()

                if len(data) == 0:
                    print(f"  Warning: empty file {bin_path.name}")
                    continue

                while len(data) % (2*6) != 0:
                    print(f"  Warning: odd byte length in {bin_path.name} — truncating last byte")
                    data = data[:-1]

                num_samples = len(data) // (2*6)
                if num_samples == 0:
                    continue

                # Unpack as little-endian int16
                for chunk in struct.iter_unpack("<6h", data):
                    writer.writerow([chunk[0], chunk[1], chunk[2], 
                                     chunk[3], chunk[4], chunk[5], 
                                     class_id, sess_id])

                total_samples += num_samples

            except Exception as e:
                print(f"  Error processing {bin_path.name}: {e}")
                
    return full_output_name


def main():
    parser = argparse.ArgumentParser(
        description="Scan for *_s*_*.bin files and export samples to CSV with class/session labels."
    )
    parser.add_argument(
        "source_dir",
        help="Source directory to recursively scan for matching .bin files"
    )
    parser.add_argument(
        "output_file",
        help="Path to the output CSV file (will be overwritten)"
    )
    args = parser.parse_args()

    source_path = Path(args.source_dir).resolve()
    if not source_path.is_dir():
        print(f"Error: '{source_path}' is not a directory.")
        return

    output_path = Path(args.output_file)

    # Regex to extract TARGET and SESSION from filename
    pattern = re.compile(r'(00down|00lowguard|00guard|jab|cross|lhook|rhook|uppercut|xxxrandom)_s(\d+)_(\d+)\.bin')
    # First pass: discover all files and unique targets/sessions
    training_files_to_process = []
    unique_targets = set()
    unique_sessions = set()

    print(f"Scanning directory: {source_path} (recursive)...")
    for bin_file in source_path.rglob("*/*.bin"):
        if not bin_file.is_file():
            continue
        match = pattern.match(bin_file.name)
        if match:
            target = match.group(1)
            session = int(match.group(2))
            training_files_to_process.append((bin_file, target, session))
            unique_targets.add(target)
            unique_sessions.add(session)
        else:
            pass
            # print(f"  Skipping (does not match pattern): {bin_file.name}")
            

    if not training_files_to_process:
        print("No matching files found. Nothing to do.")
        return

    # Create consistent mappings (sorted for reproducibility)
    state = StateTracker(unique_targets, unique_sessions)


    training_files_to_process_sorted = sorted(training_files_to_process, key=lambda x: f'{x[2]:3d}-{x[1]}')
    
    train_file_name = write_files_to_csv(state, output_path, training_files_to_process_sorted)
    
    # === Reporting ===
    print("\n" + "=" * 50)
    print("Processing complete.")
    print(f"Total training files processed : {len(training_files_to_process)}")
    print(f"Total samples written : {state.total_samples:,}")

    print("\nFiles per TARGET:")
    for t in state.sorted_targets:
        print(f"  {t:20s} : {state.target_file_count[t]} file(s)")

    print("\nFiles per SESSION:")
    for s in state.sorted_sessions:
        print(f"  Session {s:3d} -> ID {state.session_to_id[s]:2d} : {state.session_file_count[s]} file(s)")

    print("\n" + "=" * 50)
    print("TARGET -> class mapping:")
    for t, c in state.target_to_class.items():
        print(f"#define {t:20s} {c}")

    print("\nSESSION -> id mapping:")
    for s, sid in state.session_to_id.items():
        print(f"  {s:3d} -> {sid}")

    print(f"\nCSV written to: {train_file_name}.  Zipping...")
    
    zip_file = create_single_file_zip(train_file_name)
    print(f"Zipped to {zip_file}")
    os.unlink(train_file_name)
        
        



if __name__ == "__main__":
    main()
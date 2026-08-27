#!/usr/bin/env python3
"""
Deep-scan a directory for "all_TARGET_sSESSION.bin" files,
map targets and sessions to integer IDs,
and export every uint16 sample as a row in a CSV file:
adc,class,session

#define 00idle               0                                                                           
#define 01random             1                                                                           
#define BACK                 2                                                                           
#define DOT                  3                                                                           
#define ENTER                4                                                                           
#define SPACE                5                                                                           
#define a                    6
#define c                    7
#define d                    8
#define e                    9
#define h                    10
#define i                    11
#define l                    12
#define n                    13
#define o                    14
#define r                    15
#define s                    16
#define t                    17


"""

import argparse
import csv
import re
import struct
from collections import defaultdict
from pathlib import Path

import zipfile
import os

def create_single_file_zip(src_path: str, zip_path: str | None = None) -> str:
    """
    Create a ZIP archive that contains exactly one file (the basename of src_path)
    with no directory path information inside the archive.

    Parameters
    ----------
    src_path : str
        Full path to the source file (e.g. "/tmp/abc.csv").
    zip_path : str | None, optional
        Desired path for the resulting ZIP file.
        If omitted, the ZIP is created in the current working directory
        with the same stem as the source file (e.g. "abc.zip").

    Returns
    -------
    str
        The path of the created ZIP file.
    """
    if not os.path.isfile(src_path):
        raise FileNotFoundError(f"Source file does not exist: {src_path}")

    basename = os.path.basename(src_path)          # "abc.csv"
    if zip_path is None:
        stem = os.path.splitext(basename)[0]       # "abc"
        zip_path = f"/tmp/{stem}.zip"

    with zipfile.ZipFile(zip_path, mode="w", compression=zipfile.ZIP_DEFLATED) as zf:
        # arcname=basename guarantees the archive contains only the file name
        # and no path components
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
        writer.writerow(["adc", "class", "session"])

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

                if len(data) % 2 != 0:
                    print(f"  Warning: odd byte length in {bin_path.name} — truncating last byte")
                    data = data[:-1]

                num_samples = len(data) // 2
                if num_samples == 0:
                    continue

                # Unpack as little-endian uint16 (change to '>' if your data is big-endian)
                samples = struct.unpack(f"<{num_samples}H", data)

                for sample in samples:
                    writer.writerow([sample, class_id, sess_id])

                total_samples += num_samples

            except Exception as e:
                print(f"  Error processing {bin_path.name}: {e}")
                
    return full_output_name


def main():
    parser = argparse.ArgumentParser(
        description="Scan for all_*_s*.bin files and export samples to CSV with class/session labels."
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
    # Matches: all_h_s5.bin, all_SPACE_s1.bin, all_my_target_s10.bin, etc.
    pattern = re.compile(r'(train|valid)_(.+)_s(\d+)\.bin')

    # First pass: discover all files and unique targets/sessions
    training_files_to_process = []
    validation_files_to_process = []
    unique_targets = set()
    unique_sessions = set()

    print(f"Scanning directory: {source_path} (recursive)...")
    for bin_file in source_path.rglob("train_*_s*.bin"):
        if not bin_file.is_file():
            continue
        match = pattern.match(bin_file.name)
        if match:
            target = match.group(2)
            session = int(match.group(3))
            training_files_to_process.append((bin_file, target, session))
            unique_targets.add(target)
            unique_sessions.add(session)
        else:
            pass
            # print(f"  Skipping (does not match pattern): {bin_file.name}")
            
    
    
    for bin_file in source_path.rglob("valid_*_s*.bin"):
        if not bin_file.is_file():
            continue
        match = pattern.match(bin_file.name)
        if match:
            target = match.group(2)
            session = int(match.group(3))
            
            if target not in unique_targets or \
                session not in unique_sessions:
                    print(f'Have a validation file ({bin_file}) for which we do not already have training????? SKIP')
                    continue 
                    
            validation_files_to_process.append((bin_file, target, session))
        else:
            pass
            

    if not training_files_to_process:
        print("No matching files found. Nothing to do.")
        return

    # Create consistent mappings (sorted for reproducibility)
    state = StateTracker(unique_targets, unique_sessions)


    training_files_to_process_sorted = sorted(training_files_to_process, key=lambda x: f'{x[2]:3d}-{x[1]}')
    
    train_file_name = write_files_to_csv(state, output_path, training_files_to_process_sorted)
    
    valid_file_name = ''
    if len(validation_files_to_process):
        validation_files_to_process_sorted = sorted(validation_files_to_process, key=lambda x: f'{x[2]:3d}-{x[1]}')
        valid_file_name = write_files_to_csv(state, f'{output_path}-valid', validation_files_to_process_sorted)
        
    # === Reporting ===
    print("\n" + "=" * 50)
    print("Processing complete.")
    print(f"Total training files processed : {len(training_files_to_process)}")
    print(f"Total validation files processed : {len(validation_files_to_process)}")
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
    
    
    if valid_file_name:
        print(f"\nCSV written to: {valid_file_name}. Zipping...")
        zip_file = create_single_file_zip(valid_file_name)
        print(f"Zipped to {zip_file}")
        os.unlink(valid_file_name)
        
if __name__ == "__main__":
    main()
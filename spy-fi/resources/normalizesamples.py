#!/usr/bin/env python3
"""Take raw sample data, find the size of bursts, determine a best
   size to capture all important data from (most of) the set, 
   then produce samples all normalized to this size"""

import argparse
import glob
import os
import sys
import numpy as np
import random


def detect_interesting_chunk(signal, pre_expand=250, post_expand=600, random_pre=0):
    """Find global highest peak (after DC removal) and expand asymmetrically."""
    n = len(signal)
    if n < (pre_expand + post_expand):
        print("NOT ENOUGH DATA FOR A CHUNK")
        raise ValueError("insufficient data")
        
    rand_extra_pre = 0
    if random_pre:
        rand_extra_pre = random.randint(0, random_pre)

    # Remove DC offset
    signal_ac = signal.astype(np.float64) - np.mean(signal)
    abs_sig = np.abs(signal_ac)

    # Global highest peak
    peak_idx = np.argmax(abs_sig)

    # Adaptive relative threshold
    peak_val = abs_sig[peak_idx]
    thresh = peak_val * 0.05

    # Expand left (smaller)
    start = peak_idx
    while start > 0 and abs_sig[start-1] > thresh:
        start -= 1
    start = max(0, start - (pre_expand + rand_extra_pre))

    # Expand right (larger - for ringing tail)
    end = peak_idx
    while end < n-1 and abs_sig[end+1] > thresh:
        end += 1
    end = min(n, end + post_expand)

    return start, end


def samples_to_time(samp_rate:int, num_samps:int):
    return 1.0*num_samps/samp_rate

def samples_to_ms(samp_rate:int, num_samps:int):
    return samples_to_time(samp_rate, num_samps)*1000.0
    
def ms_to_samples(samp_rate:int, timems:float):
    return int(samp_rate * timems/1000.0)
    
    
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('prefix', help='Filename prefix')
    parser.add_argument('-o', '--output_dir', required=False, type=str, default=None, help='Output directory')
    parser.add_argument('--suffix', type=str, default='',
                        help='suffix for output filenames')
    parser.add_argument('--margin', type=int, default=150,
                        help='Leading quiet margin before onset [150]')
    parser.add_argument('--random-pre', type=int, default=0,
                        help='Extra random samples BEFORE the main peak [0]')
    parser.add_argument('--pre-expand', type=int, default=200,
                        help='Extra samples BEFORE the main peak [200]')
    parser.add_argument('--pre-expand-ms', type=int, default=None,
                        help='Extra samples BEFORE the main peak in ms')
    parser.add_argument('--post-expand', type=int, default=800,
                        help='Extra samples AFTER the main peak (for ringing) [800]')
    parser.add_argument('--post-expand-ms', type=int, default=None,
                        help='Extra samples AFTER the main peak (for ringing) in ms')
    parser.add_argument('--percentile', type=float, default=90.0, help="Percentile to preserve [90.0]")
    parser.add_argument('--fixed-length', type=int, default=None,
                        help='Force output length')
    parser.add_argument('--fixed-length-ms', type=int, default=None,
                        help='Force output length in ms')
    parser.add_argument('--samp-rate', type=int, default=44100,
                        help='Approx sample rate of files [44.1kHz]')
    parser.add_argument('-d', '--dry-run', action="store_true",
                        help='Dry run, only stats')
    parser.add_argument('-s', '--summary', action="store_true",
                        help='Only output summary')
    parser.add_argument('-v', '--verbose', action="store_true",
                        help='Verbose output')
    args = parser.parse_args()

    pattern = args.prefix + '*'
    files = sorted([f for f in glob.glob(pattern) if os.path.isfile(f)])
    if not files:
        print("No files found", file=sys.stderr)
        sys.exit(1)
        
    if not args.dry_run and args.output_dir is None:
        print("Need an output-dir argument!")
        return 
    
    num_files = len(files)
    if not args.summary:
        print(f"Found {num_files} files.")

    file_infos = []
    file_lengths = []
    
    samp_rate = args.samp_rate
    
    pre_exp = args.pre_expand
    if args.pre_expand_ms:
        pre_exp = ms_to_samples(samp_rate, args.pre_expand_ms)
        print(f'Forcing pre-expand to {args.pre_expand_ms}ms -- {pre_exp} samples')
    
    
    post_exp = args.post_expand
    if args.post_expand_ms:
        post_exp = ms_to_samples(samp_rate, args.post_expand_ms)
        print(f'Forcing post-expand to {args.post_expand_ms}ms -- {post_exp} samples')
    
    
    
    for filepath in files:
        try:
            signal = np.fromfile(filepath, dtype='<i2')
            flen = len(signal)
            file_lengths.append(flen)
            
            
            
            try:
                start, end = detect_interesting_chunk(
                    signal,
                    pre_expand=pre_exp,
                    post_expand=post_exp,
                    random_pre=args.random_pre
                )
            except ValueError:
                # skip
                continue
            duration = end - start
            file_infos.append({
                'path': filepath,
                'signal': signal,
                'start': start,
                'duration': duration,
                'length': flen
            })
            if args.verbose:
                print(f"  {os.path.basename(filepath)}: total {flen}, interesting {duration} (onset {start})")
        except Exception as e:
            print(f"Error {filepath}: {e}", file=sys.stderr)

    if not file_infos:
        sys.exit(1)
    
    durations = np.array([info['duration'] for info in file_infos])
    if not args.summary:
        flens = np.array([info['length'] for info in file_infos])
        print(f"\n=== Interesting duration statistics ===")
        print(f"  File Min: {flens.min():.0f}/{samples_to_ms(samp_rate, flens.min()):.1f}ms")
        print(f"  File Max: {flens.max():.0f}/{samples_to_ms(samp_rate, flens.max()):.1f}ms")
        print(f"  File Avg: {flens.mean():.0f}/{samples_to_ms(samp_rate, flens.mean()):.1f}ms")
        print(f"  Min     : {durations.min():.0f}/{samples_to_ms(samp_rate, durations.min()):.1f}ms")
        print(f"  Max     : {durations.max():.0f}/{samples_to_ms(samp_rate, durations.max()):.1f}ms")
        print(f"  Mean    : {durations.mean():.0f}/{samples_to_ms(samp_rate, durations.mean()):.1f}ms")
        print(f"  25/50/75: {np.percentile(durations, [25,50,75])}")
        print(f"  90/95   : {np.percentile(durations, [90,95])}\n")

    margin = args.margin
    
    fixed_len_arg = args.fixed_length
    if args.fixed_length_ms:
        fixed_len_arg = ms_to_samples(samp_rate, args.fixed_length_ms)
    
    if fixed_len_arg is not None:
        fixed_length = fixed_len_arg
        print(f"Manual fixed length for {num_files:3d} {pattern} : {fixed_length} / {samples_to_ms(samp_rate, fixed_length):.1f}ms")
    else:
        target_dur = int(np.percentile(durations, args.percentile))
        candidate_fixed = target_dur + margin
        fixed_length = min(candidate_fixed, min(file_lengths))
        print(f"Auto fixed length for {num_files:3d} {pattern}: {fixed_length} / {samples_to_ms(samp_rate, fixed_length):.1f}ms")
    
    
    if args.dry_run:
        return
    
    os.makedirs(args.output_dir, exist_ok=True)
    
    for info in file_infos:
        signal = info['signal']
        sig_start = info['start']
        flen = info['length']

        if flen < fixed_length:
            print("Skipping -- too short")
            continue
        extract_start = max(0, sig_start - margin)
        extract_end = extract_start + fixed_length
        if extract_end > flen:
            extract_end = flen
            extract_start = max(0, flen - fixed_length)

        extracted = signal[extract_start:extract_end]
        fname, ext = os.path.splitext(os.path.basename(info['path']))
        basename = f'{fname}{args.suffix}{ext}'
        out_path = os.path.join(args.output_dir, basename)
        extracted.tofile(out_path)
        print(f"Saved {basename} ({len(extracted)} samples, offset {extract_start})")

    print(f"\nAll done -> {args.output_dir}")


if __name__ == '__main__':
    main()

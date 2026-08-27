#!/usr/bin/env python3
import subprocess
import threading
import sys
import time 
import argparse
import os
import signal

# Shared lock so only one thread prints at a time
print_lock = threading.Lock()

# ANSI colors
COLORS = {
    "left": "\033[94m",   # blue
    "right": "\033[92m",   # green
}
RESET = "\033[0m"
KeepRunning = False
MaxSamples = 0
SampCount = dict()
def stream(pipe, label):
    global KeepRunning, MaxSamples
    color = COLORS.get(label, "")
    for line in iter(pipe.readline, ""):
        with print_lock:                     # prevents mid-line mixing
            lstripped = line.rstrip()
            print(f"{color}[{label}] {lstripped}{RESET}", flush=True)
            if lstripped.find('Saved') >= 0 or lstripped.find('max samp') >= 0:
                print(f"FOUNDIT is '{lstripped}", flush=True)
                if label not in SampCount:
                    SampCount[label] = 1
                else:
                    SampCount[label] += 1
                    if SampCount[label] >= MaxSamples:
                        print("WE DONE HERE", flush=True)
                        time.sleep(0.8)
                        KeepRunning = False
                        time.sleep(0.3)
                        killallProcesses()
                        
                        
            
    pipe.close()

SubProcesses = []

def killallProcesses():
    global SubProcesses
    
    for process in SubProcesses:
        print(f"Terminating {process}", flush=True)
        process.terminate()
        time.sleep(0.6)
        
    try:
        for process in SubProcesses:
            process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        for process in SubProcesses:
            process.kill()
    
def main():
    global KeepRunning, MaxSamples
    
    prog = './mick_collect.py'
    
    args = get_args()
    basename = f'{args.target}_{args.session}'
    progArgs = {
        'left': [prog, '--left', '--max-samples', str(args.num), '--split', f'{args.output_directory}/left/{basename}_', 
                f'{args.output_directory}/left/full_{basename}.bin'],
        'right':  [prog, '--right', '--max-samples', str(args.num), '--split', f'{args.output_directory}/right/{basename}_', 
                f'{args.output_directory}/right/full_{basename}.bin'],
    
    }
    MaxSamples = args.num
    KeepRunning = True
    
    th = []
    print(f"Collecting up to {args.num} samples")
    for dev,progparms in progArgs.items():
        print(f"Setting up {dev} with: \n  {progparms}")
        sp = subprocess.Popen(
            progparms,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            )
        SubProcesses.append(sp)
        
        
        th.append(
            threading.Thread(target=stream, args=(sp.stdout, dev), daemon=True)
        
        )
        time.sleep(1)
    for athread in th:
        print(f'starting {athread}')
        athread.start()
        time.sleep(0.25)
        
    try:
        while KeepRunning:
            time.sleep(0.05)
    except KeyboardInterrupt:
        pass 
    finally:
        killallProcesses()
            
    print("Waiting for done")
            
    for athread in th:
        athread.join(timeout=1)
    

    

def get_args():
    parser = argparse.ArgumentParser(
        description="Get sample data from two BLE devices."
    )

    parser.add_argument(
        "--session",
        metavar="SESS",
        type=str,
        required=True,
        help="session name, e.g. s1",
    )
    
    parser.add_argument(
        "--target",
        metavar="TARGET",
        type=str,
        required=True,
        help="Target class, e.g 'hook'",
    )
    parser.add_argument(
        "--num",
        metavar="NUM",
        type=int,
        default=30,
        help="Number of samples to fetch",
    )
    
    
    parser.add_argument(
        "output_directory",
        type=str,
        help="Path to the output dirs",
    )
    
    return parser.parse_args()
    
if __name__ == '__main__':
    main()

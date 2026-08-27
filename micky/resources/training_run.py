#!/usr/bin/env python3

import glob
import logging
import argparse
import time
import signal
import sys
import time   
import random
import subprocess
import threading
from pathlib import Path



# Shared lock so only one thread prints at a time
print_lock = threading.Lock()
def stream(pipe, label):
    color = COLORS.get(label, "")
    for line in iter(pipe.readline, ""):
        with print_lock:                     # prevents mid-line mixing
            print(line.rstrip(), flush=True)
            
    pipe.close()


def spawnProcess(progparms):
    print(progparms)
    sp = subprocess.Popen(
            progparms
            )
    return sp
    
class FileContainer:
    
    def __init__(self, name:str, prefix:str):
        self.name = name 
        self.prefix = prefix 
        #print(f"GLOPPING {prefix}*")
        self._files = glob.glob(f"{prefix}*")
        #print(self._files)
        
    @property 
    def file(self):
        return random.choice(self._files)
        
    def play(self):
        f = self.file 
        spawnProcess(['/usr/bin/aplay', '-q', f])
    def playSync(self):
        f = self.file 
        subprocess.run(['aplay', '-q', f], check=True)
        

def get_files():
    subdir = './sounds'
    sections = [
        'jab',
        'cross',
        'leadhook',
        'rearhook',
        'uppercut'
    ]
    reps = [
        'jab',
        'cross',
        'lhook',
        'rhook',
        'uppercut'
    ]
    segways = [
        'start',
        'ready',
        'done'
    
    ]
    secFiles = []
    repFiles = []
    segwayFiles = []
    for s in sections:
        secFiles.append(FileContainer(s, f'{subdir}/titles/{s}'))
    for r in reps:
        repFiles.append(FileContainer(r, f'{subdir}/reps/{r}'))
        
    for s in segways:
        segwayFiles.append(FileContainer(s, f'{subdir}/segway/{s}'))
        
    return {
        'sets': secFiles,
        'reps': repFiles,
        'segways': segwayFiles
    }
    
def main():
    args = get_args()
    
    soundFiles = get_files()
    
    Path(f'{args.output_directory}/left').mkdir(parents=True, exist_ok=True)
    Path(f'{args.output_directory}/right').mkdir(parents=True, exist_ok=True)

    soundFiles['segways'][0].playSync()
    for sessid in range(args.session_start, args.session_start + args.num_sessions):
        print(f"Session {sessid}")
        for sid in range(len(soundFiles['sets'])):
            setfiles = soundFiles['sets'][sid]
            print(setfiles.name)
            reps = soundFiles['reps'][sid]
            
            print(f'{reps.name} {sessid}')
            samplerArgs = ['./sample_collect.py', '--session', f's{sessid}', 
                            '--target', reps.name, '--num',str(args.num_samples), 
                            args.output_directory]
            print(samplerArgs)
            subproc = spawnProcess(samplerArgs)
            
            soundFiles['segways'][1].playSync()
            setfiles.playSync()
            #time.sleep(3)
            for i in range(args.num_samples):
                reps.playSync()
                time.sleep(0.7)
            
            soundFiles['segways'][2].playSync()
            time.sleep(4)
            subproc.terminate()
            subproc.wait(timeout=5)
                     


def get_args():
    parser = argparse.ArgumentParser(
        description="Get sample data from two BLE devices."
    )

    parser.add_argument(
        "--session-start",
        metavar="SESS",
        type=int,
        required=True,
        help="session name, e.g. 1",
    )
    parser.add_argument(
        "--num-sessions",
        metavar="NUMSESS",
        type=int,
        required=False,
        default=5,
        help="number of sessions",
    )
    
    parser.add_argument(
        "--num-samples",
        metavar="NUMSAMPLES",
        type=int,
        required=False,
        default=12,
        help="Number of reps",
    )
    
    
    parser.add_argument(
        "output_directory",
        type=str,
        help="Path to the output dirs",
    )
    
    return parser.parse_args()
    
    
if __name__ == '__main__':
    main()
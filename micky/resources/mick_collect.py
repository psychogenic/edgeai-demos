#!/usr/bin/env python3
import asyncio
import logging
import argparse
import time
from ble_serial.bluetooth.ble_client import BLE_client

import os
import signal
import sys
import time 


Devices = dict(
    left='CE:27:11:58:A9:65',
    right='FC:C9:5E:86:64:08'
)

BlockSize = 256
Sensors=6
BytesPerSensorSample=2

OutFileHandle = None
RcvCount = 0
PacketCount = 0
CurrentRecord = bytearray()
LastRcv = 0
SplitFileSize = 0
SplitFileCount = 0
SplitFileSuffix = ''
MaxSamplePackets = 0

def receive_callback(value: bytes):
    global OutFileHandle, RcvCount, LastRcv, CurrentRecord, SplitFileCount, SplitFileSuffix
    global SplitFileSize, MaxSamplePackets, PacketCount
    
    tnow = time.monotonic()
    
        
    if OutFileHandle is not None:
        OutFileHandle.write(value)
        
        
    
    RcvCount += 1
    CurrentRecord += value 
    if len(CurrentRecord) >= SplitFileSize:
        PacketCount += 1
        if SplitFileSuffix:
            sfname = f'{SplitFileSuffix}{SplitFileCount}.bin'
            with open(sfname, 'wb') as sf:
                sf.write(CurrentRecord)
                sf.close()
                print(f'Saved {sfname}', flush=True)
            SplitFileCount += 1
        
        CurrentRecord = bytearray()
    
    if (tnow - LastRcv) > 0.1:
        print(f'{tnow}, {PacketCount} ({RcvCount})', flush=True)
        
    LastRcv = tnow 
    
    
    if MaxSamplePackets and PacketCount >= MaxSamplePackets:
        if (tnow - LastRcv) > 0.2:
            print("max samples rcvd, skip", flush=True)
            LastRcv = tnow 
            
        print("Doing the suicide", flush=True)
        time.sleep(0.2)
        os.kill(os.getpid(), signal.SIGTERM)
        return
        
    
        
def get_args():
    parser = argparse.ArgumentParser(
        description="Process BLE device data and write to an output file."
    )

    parser.add_argument(
        "--device",
        metavar="MAC",
        type=str,
        default="FC:C9:5E:86:64:08",
        help="Bluetooth device MAC address (default: FC:C9:5E:86:64:08) other CE:27:11:58:A9:65",
    )
    
    parser.add_argument(
        "--max-samples",
        metavar="MAX",
        type=int,
        default=0,
        help="Maximum number of samples to collect",
    )
    
    
    parser.add_argument("--left", action="store_true", help="device LEFT")
    parser.add_argument("--right", action="store_true", help="device RIGHT")
    

    parser.add_argument(
        "--adapter",
        metavar="ADAPT",
        type=str,
        default="hci0",
        help="Bluetooth adapter name (default: hci0)",
    )
    
    parser.add_argument("--split", type=str, default='', help="split records suffix")
    parser.add_argument("--recordsize", type=int, default=BlockSize*Sensors*BytesPerSensorSample, help="record size")
    parser.add_argument(
        "output_file",
        type=str,
        help="Path to the output file",
    )

    return parser.parse_args()

async def main():
    global OutFileHandle, SplitFileCount, SplitFileSize, SplitFileSuffix
    global MaxSamplePackets
    
    
    args = get_args()
    # None uses default/autodetection, insert values if needed
    ADAPTER = args.adapter
    SERVICE_UUID = None
    WRITE_UUID = None
    READ_UUID = None
    DEVICE = args.device
    WRITE_WITH_RESPONSE = False
    
    loop = asyncio.get_running_loop()

    ble = BLE_client(ADAPTER, 'ID')
    
    async def cleanup():
        print("Received termination – cleaning up…", flush=True)
        try:
            await ble.disconnect()      
        except:
            pass
        finally:
            # Now cancel everything else
            current = asyncio.current_task()
            for task in asyncio.all_tasks(loop):
                if task is not current:
                    task.cancel()
    def on_signal():
        # Schedule the async cleanup
        asyncio.create_task(cleanup())

    # Register the handlers with the event loop
    loop.add_signal_handler(signal.SIGTERM, on_signal)
    loop.add_signal_handler(signal.SIGINT,  on_signal)   # Ctrl-C too
    
    if args.left:
        if args.right:
            print("Choose ONE --left OR --right")
            return 
            
        DEVICE = Devices['left']
        
    if args.right:
        DEVICE = Devices['right']
    
    SplitFileSize = args.recordsize
    if args.split:
        SplitFileSuffix = args.split
        
    MaxSamplePackets = args.max_samples
    ble.set_receiver(receive_callback)

    try:
        await ble.connect(DEVICE, "public", SERVICE_UUID, 10.0)
        await ble.setup_chars(WRITE_UUID, READ_UUID, "rw", WRITE_WITH_RESPONSE)
        
        OutFileHandle = open(args.output_file, "wb")
        
        await ble.send_loop() 
        # await asyncio.gather(ble.send_loop(), hello_sender(ble))
    except asyncio.exceptions.CancelledError:
        print("Terminating")
    finally:
        if OutFileHandle is not None:
            OutFileHandle.close()


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("Stopping")
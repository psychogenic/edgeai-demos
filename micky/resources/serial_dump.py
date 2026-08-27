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

DefaultAdapter = 'hci0'


Devices = dict(
    left='CE:27:11:58:A9:65',
    right='FC:C9:5E:86:64:08'
)

TotalLen = 0
def receive_callback(value: bytes):
    global TotalLen
    tnow = time.monotonic()
    s = value
    TotalLen += len(s)
    try:
        s = value.decode()
    except:
        print(f'{tnow}\t{value}')
        
    try:
        ident, confs = s.split(',')
        if float(confs) > 10:
            print(f'{ident}\t{confs}')
    except:
        print(f'{tnow}({len(s)}/{TotalLen})\t{s}')
    
    
        
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
    
    parser.add_argument("--left", action="store_true", help="device LEFT")
    parser.add_argument("--right", action="store_true", help="device RIGHT")
    

    parser.add_argument(
        "--adapter",
        metavar="ADAPT",
        type=str,
        default=DefaultAdapter,
        help="Bluetooth adapter name (eg: hci0)",
    )

    return parser.parse_args()

async def main():
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
            await ble.disconnect()          # ← your async cleanup
        except:
            pass
        finally:
            # Now cancel everything else
            current = asyncio.current_task()
            for task in asyncio.all_tasks(loop):
                if task is not current:
                    task.cancel()
    def on_signal():
        # Schedule the async cleanup (this is safe from the signal handler)
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
    
    ble.set_receiver(receive_callback)

    try:
        await ble.connect(DEVICE, "public", SERVICE_UUID, 10.0)
        await ble.setup_chars(WRITE_UUID, READ_UUID, "rw", WRITE_WITH_RESPONSE)
        
        await ble.send_loop() 
    except asyncio.exceptions.CancelledError:
        print("Terminating")
    finally:
        print("Outta here")
            


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("Stopping")
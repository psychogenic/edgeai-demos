#!/usr/bin/env python3
"""
keycapture.py

Need pynput:
 
  pip install pyserial pynput
  
pynput's global listener requires an X11 session on Linux.
 It works out of the box on X11, Windows, and macOS
(macOS needs Accessibility permission granted to the terminal/app).

--now using the burst detection keycapture on the DK, so this is a somewhat 
hacked-up version of the original...

Files written to --output-dir:
  raw.bin                  - the full sample stream, sync words stripped
  blocks_index.csv         - per-block timing metadata (see BlockIndex)
  keystrokes_log.csv       - one row per key-down/key-up event, real-time,
                              best-effort sample/byte position (see KeystrokeLog)
  extraction_manifest.csv  - one row per key-up extraction attempt, with the
                              authoritative sample/byte range and outcome
                              (see ExtractionManifest)
  KEY_NNN.bin               - the extracted slice for capture NNN of key KEY



"""

from __future__ import annotations
import numpy as np
import argparse
import logging
import queue
import re
import sys
import threading
import time
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Optional, List

try:
    import serial
except ImportError:
    sys.exit("Missing dependency: pip install pyserial")

try:
    from pynput import keyboard
except ImportError:
    sys.exit("Missing dependency: pip install pynput")


# ----------------------------------------------------------------------------
# Constants / tunables
# ----------------------------------------------------------------------------

DefaultBlockSize=4096
DefaultSampleMargin=30

BITS_PER_BYTE_UART = 10          # start + 8 data + stop, no parity, no 2 stop bits
BYTES_PER_SAMPLE = 2             # 16-bit little-endian samples
SERIAL_READ_TIMEOUT_S = 0.75      # so blocking reads can notice stop_event
CAPTURE_LATENCY_S = 0.013        # fixed device-internal latency correction; see docstring

# Characters that are unsafe/awkward in filenames, mapped to readable names.
_SAFE_NAME_OVERRIDES = {
    "/": "SLASH",
    "\\": "BACKSLASH",
    ":": "COLON",
    "*": "ASTERISK",
    "?": "QUESTION",
    '"': "QUOTE",
    "<": "LT",
    ">": "GT",
    "|": "PIPE",
    " ": "SPACE",
    ".": "DOT",
    ",": "COMMA",
}


# ----------------------------------------------------------------------------
# Block bookkeeping
# ----------------------------------------------------------------------------

@dataclass
class BlockRecord:
    """Metadata for one received block of sample data (sync word already
    stripped). Enough to interpolate a per-sample timestamp and to map a
    timestamp back to a byte offset in raw.bin."""

    block_index: int
    start_sample_index: int
    num_samples: int
    arrival_time: float          # time.time() right after the block finished reading
    sample_period: float         # seconds/sample, derived from baudrate
    start_byte_offset: int       # offset of this block's first data byte in raw.bin
    num_bytes: int

    @property
    def transmit_duration(self) -> float:
        return self.num_samples * self.sample_period

    @property
    def start_transmit_time(self) -> float:
        return self.arrival_time - self.transmit_duration - CAPTURE_LATENCY_S

    @property
    def end_transmit_time(self) -> float:
        return self.start_transmit_time + self.transmit_duration

    def sample_time(self, local_sample_index: int) -> float:
        return self.start_transmit_time + local_sample_index * self.sample_period


class BlockIndex:
    """Thread-safe, append-only log of BlockRecords, mirrored to a CSV file
    on disk (so a crash doesn't lose the ability to re-derive timing later)."""

    def __init__(self, index_path: Path):
        self._cv = threading.Condition()
        self._blocks: List[BlockRecord] = []
        self._fh = open(index_path, "w", buffering=1)
        self._fh.write(
            "block_index,start_sample_index,num_samples,arrival_time,"
            "sample_period,start_byte_offset,num_bytes\n"
        )

    def append(self, rec: BlockRecord) -> None:
        with self._cv:
            self._blocks.append(rec)
            self._fh.write(
                f"{rec.block_index},{rec.start_sample_index},{rec.num_samples},"
                f"{rec.arrival_time!r},{rec.sample_period!r},"
                f"{rec.start_byte_offset},{rec.num_bytes}\n"
            )
            self._cv.notify_all()

    def wait_for_coverage(self, until_time: float, timeout: float = 5.0) -> bool:
        """Block until a block has arrived whose end_transmit_time >= until_time."""
        deadline = time.monotonic() + timeout
        with self._cv:
            while True:
                if self._blocks and self._blocks[-1].end_transmit_time >= until_time:
                    return True
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    print("Ran out of time?!")
                    return False
                self._cv.wait(timeout=remaining)

    def snapshot(self) -> List[BlockRecord]:
        with self._cv:
            return list(self._blocks)

    def close(self) -> None:
        self._fh.close()


def time_to_byte_offset(blocks: List[BlockRecord], t: float, round_up: bool) -> Optional[int]:
    """Map a timestamp to the nearest byte offset in raw.bin, given the
    current set of known blocks. Clamps to stream start/end if t falls
    outside all known blocks."""
    if not blocks:
        return None

    if t <= blocks[0].start_transmit_time:
        return blocks[0].start_byte_offset

    if t >= blocks[-1].end_transmit_time:
        last = blocks[-1]
        return last.start_byte_offset + last.num_bytes

    for b in blocks:
        if b.start_transmit_time <= t <= b.end_transmit_time:
            frac_sample = (t - b.start_transmit_time) / b.sample_period
            local_sample = int(frac_sample) + (1 if round_up and frac_sample % 1 else 0)
            local_sample = min(max(local_sample, 0), b.num_samples)
            return b.start_byte_offset + local_sample * BYTES_PER_SAMPLE

    return None  # gap between blocks (shouldn't normally happen)


def byte_offset_to_sample_index(byte_offset: Optional[int]) -> Optional[int]:
    if byte_offset is None:
        return None
    return byte_offset // BYTES_PER_SAMPLE


# ----------------------------------------------------------------------------
# Keystroke history / extraction manifest
#
# Two separate logs are kept, because they answer two different questions:
#
#   keystrokes_log.csv   - "what keys were pressed, and when?" One row per
#                           key-down and per key-up event, written the
#                           instant the event happens. The sample_idx /
#                           byte_offset columns are a *best-effort snapshot*
#                           taken at log time from whatever serial data has
#                           arrived so far - if the corresponding serial
#                           block hasn't arrived yet, the value is clamped
#                           to the latest known position (or left blank if
#                           no serial data has arrived at all) and is not
#                           authoritative.
#
#   extraction_manifest.csv - "where exactly in raw.bin does each captured
#                           slice live?" One row per key-up, written by the
#                           extraction worker *after* it has waited for the
#                           relevant serial data to arrive, so these
#                           sample/byte values are the authoritative ones -
#                           and every row records whether extraction
#                           actually succeeded (a KEY_NNN.bin file exists)
#                           or not (e.g. timed out waiting for serial data).
# ----------------------------------------------------------------------------

class KeystrokeLog:
    """Thread-safe, append-only, real-time record of every key-down and
    key-up event, independent of whether a serial slice could be extracted
    for it."""

    def __init__(self, path: Path, block_index: BlockIndex):
        self._lock = threading.Lock()
        self._block_index = block_index
        self._fh = open(path, "w", buffering=1)
        self._fh.write("wall_time,edge,key,sample_idx_estimate,byte_offset_estimate\n")

    def log_edge(self, key: str, edge: str, t: float) -> None:
        blocks = self._block_index.snapshot()
        byte_off = time_to_byte_offset(blocks, t, round_up=False) if blocks else None
        sample_idx = byte_offset_to_sample_index(byte_off)
        with self._lock:
            self._fh.write(
                f"{t!r},{edge},{key},"
                f"{'' if sample_idx is None else sample_idx},"
                f"{'' if byte_off is None else byte_off}\n"
            )

    def close(self) -> None:
        self._fh.close()


class ExtractionManifest:
    """Thread-safe, append-only record of every key-up-triggered extraction
    attempt, with the authoritative (post-wait) sample/byte range and the
    resulting filename, or the reason it failed."""

    def __init__(self, path: Path):
        self._lock = threading.Lock()
        self._fh = open(path, "w", buffering=1)
        self._fh.write(
            "key,capture_num,t_down,t_up,sample_idx_start,sample_idx_end,"
            "byte_offset_start,byte_offset_end,filename,status\n"
        )

    def log(
        self,
        key: str,
        capture_num: int,
        t_down: float,
        t_up: float,
        sample_start: Optional[int],
        sample_end: Optional[int],
        byte_start: Optional[int],
        byte_end: Optional[int],
        filename: str,
        status: str,
    ) -> None:
        def fmt(v):
            return "" if v is None else v

        with self._lock:
            self._fh.write(
                f"{key},{capture_num},{t_down!r},{t_up!r},"
                f"{fmt(sample_start)},{fmt(sample_end)},"
                f"{fmt(byte_start)},{fmt(byte_end)},{filename},{status}\n"
            )

    def close(self) -> None:
        self._fh.close()


# ----------------------------------------------------------------------------
# Serial capture
# ----------------------------------------------------------------------------

def read_exact(ser: "serial.Serial", n: int, stop_event: threading.Event) -> Optional[bytes]:
    """Read exactly n bytes, tolerating the timeout-driven partial reads
    pyserial gives us, and bailing out (returning None) if stop_event fires."""
    buf = bytearray()
    while len(buf) < n:
        if stop_event.is_set():
            return None
        chunk = ser.read(n - len(buf))
        if chunk:
            buf += chunk
    return bytes(buf)


def find_sync(ser: "serial.Serial", sync_bytes: bytes, stop_event: threading.Event) -> bool:
    """Consume bytes from the stream until the trailing window equals
    sync_bytes. Returns False if stop_event fires first."""
    n = len(sync_bytes)
    window = bytearray()
    while not stop_event.is_set():
        b = ser.read(1)
        if not b:
            continue  # read timeout; loop again so we can re-check stop_event
        window += b
        if len(window) > n:
            del window[0]
        if len(window) == n and bytes(window) == sync_bytes:
            return True
    return False


def serial_capture_loop(
    ser: "serial.Serial",
    job_queue: "queue.Queue",
    sync_bytes: bytes,
    block_size: int,
    raw_path: Path,
    block_index: BlockIndex,
    session_id: str,
    sample_margin: int,
    output_dir: Path,
    manifest: ExtractionManifest,
    
    
    stop_event: threading.Event,
) -> None:
    sample_period = (BYTES_PER_SAMPLE * BITS_PER_BYTE_UART) / ser.baudrate
    if block_size % BYTES_PER_SAMPLE != 0:
        logging.warning(
            "block-size (%d) is not a multiple of %d bytes/sample; "
            "the last partial sample in each block will be dropped from timing math.",
            block_size, BYTES_PER_SAMPLE,
        )
    num_samples_per_block = block_size // BYTES_PER_SAMPLE

    block_counter = 0
    sample_counter = 0
    byte_offset = 0
    
    counters: dict[str, int] = {}
    
    raw_fh = open(raw_path, "wb", buffering=0)
    try:
        logging.info("Waiting for initial sync word...")
        if not find_sync(ser, sync_bytes, stop_event):
            return
        logging.info("Sync acquired. Beginning block capture.")

        while not stop_event.is_set():
            data = read_exact(ser, block_size, stop_event)
            # got data
            # print(data)
            try:
                while job_queue.qsize() > 1:
                    name, t_down, t_up = job_queue.get(timeout=0.24)
                    print(f"Dropping Queued {t_down}")
                    
                name, t_down, t_up = job_queue.get(timeout=0.15)
            except queue.Empty:
                if stop_event.is_set():
                    return
                # nothing in queue, wait for next sync
                print("Have data but nothing in queue?")
                if find_sync(ser, sync_bytes, stop_event):
                    continue
                else:
                    break
            
            
            if data is None:
                break
            t_arrival = time.time()
            raw_fh.write(data)
            
            rec = BlockRecord(
                    block_index=block_counter,
                    start_sample_index=sample_counter,
                    num_samples=num_samples_per_block,
                    arrival_time=t_arrival,
                    sample_period=sample_period,
                    start_byte_offset=byte_offset,
                    num_bytes=len(data),
                )
            block_index.append(rec)
            print(f"NEW BLOCK {block_counter}")

            block_counter += 1
            sample_counter += num_samples_per_block
            byte_offset += len(data)
            
            
            blk = rec
            if blk.num_bytes != 4096:
                print("BAD BLOCK BYTE COUNT?+?????")
                break
                
            counters[name] = counters.get(name, 0) + 1
            capture_num = counters[name]
            byte_start = blk.start_byte_offset
            byte_end = byte_start + blk.num_bytes
            
            out_path = output_dir / f"{name}_{session_id}_{capture_num:03d}.bin"
            arr = np.frombuffer(data, dtype='<u2')   # little-endian uint16
            
            if np.any(arr > 4096):
                print("INVALID SERIAL DATA RECEIVED > 4096 -- no loggy")
            else:
                with open(out_path, "wb") as out_fh:
                    out_fh.write(data)
                    
            
            status = "ok" # if covered else "ok_timed_out_waiting"
            manifest.log(
                name, capture_num, t_down, t_up,
                byte_offset_to_sample_index(byte_start), byte_offset_to_sample_index(byte_end),
                byte_start, byte_end, out_path.name, status,
            )
            
            logging.info(
                "\t%s", # %d bytes (%d samples) -> %s",
                name # , len(data), len(data) // BYTES_PER_SAMPLE, out_path.name,
            )
            
            if ser.in_waiting > 10: # 4 is the sync, have extra
                if job_queue.qsize() < 1:
                    _garbagedata = read_exact(ser, block_size + len(sync_bytes), stop_event)
                    print("extra garbagio detect, flushing")

            if not find_sync(ser, sync_bytes, stop_event):
                break
    except Exception as e:
        print(e)
        raise e
    finally:
        raw_fh.close()
        logging.info("Serial capture stopped after %d blocks.", block_counter)


# ----------------------------------------------------------------------------
# Keyboard capture
# ----------------------------------------------------------------------------

def sanitize_key_name(name: str) -> str:
    if name in _SAFE_NAME_OVERRIDES:
        return _SAFE_NAME_OVERRIDES[name]
    return re.sub(r"[^A-Za-z0-9_\-]", "_", name)


def key_name(key) -> str:
    """Turn a pynput key object into a stable, filename-safe, human-readable
    label"""
    if isinstance(key, keyboard.KeyCode):
        if key.char is not None:
            ch = key.char
            # return sanitize_key_name(ch.upper() if ch.isalpha() else ch)
            return sanitize_key_name(ch)
        # No printable char resolved (e.g. some layout-dependent keys) - fall
        # back to virtual key code if available.
        vk = getattr(key, "vk", None)
        return f"VK{vk}" if vk is not None else "UNKNOWN"
    # keyboard.Key enum member
    return sanitize_key_name(key.name.upper())


class KeyboardCapture:
    """Wraps a pynput global listener."""

    def __init__(self, extraction_queue: "queue.Queue", keystroke_log: KeystrokeLog):
        self._pending: dict[str, float] = {}
        self._pending_lock = threading.Lock()
        self._queue = extraction_queue
        self._log = keystroke_log
        self._listener = keyboard.Listener(
            on_press=self._on_press, on_release=self._on_release
        )
        self.key_pressed = False
        self.last_keypress = 0

    def time_since_keypress(self):
        return time.monotonic() - self.last_keypress
        
    def start(self) -> None:
        self._listener.start()

    def stop(self) -> None:
        self._listener.stop()

    def join(self, timeout: Optional[float] = None) -> None:
        self._listener.join(timeout)


    def _on_press(self, key) -> None:
        t = time.time()
        name = key_name(key)
        with self._pending_lock:
            is_new = name not in self._pending
            self._pending.setdefault(name, t)  # ignore OS auto-repeat
        if is_new:
            self._log.log_edge(name, "DOWN", t)
            
        self.key_pressed = True
        self.last_keypress = time.monotonic()
        self._queue.put((name, t, t+46e-3))

    def _on_release(self, key) -> None:
        t = time.time()
        name = key_name(key)
        with self._pending_lock:
            t_down = self._pending.pop(name, None)
        self._log.log_edge(name, "UP", t)
        self.key_pressed = False
        if t_down is None:
            return  # release without a matching press we saw (e.g. startup edge case)
        # self._queue.put((name, t_down, t))


# ----------------------------------------------------------------------------
# Extraction worker
# ----------------------------------------------------------------------------

def extraction_worker(
    job_queue: "queue.Queue",
    block_index: BlockIndex,
    raw_path: Path,
    session_id: str,
    sample_margin: int,
    output_dir: Path,
    manifest: ExtractionManifest,
    stop_event: threading.Event,
) -> None:
    counters: dict[str, int] = {}
    raw_fh = open(raw_path, "rb")
    last_block_processed = -1
    try:
        while True:
            try:
                name, t_down, t_up = job_queue.get(timeout=0.5)
            except queue.Empty:
                if stop_event.is_set():
                    return
                continue

            # Every attempt gets its own capture_num, whether or not it
            # ultimately succeeds, so KEY_NNN.bin filenames line up 1:1
            # with rows in extraction_manifest.csv.
            counters[name] = counters.get(name, 0) + 1
            capture_num = counters[name]

            blocks = block_index.snapshot()
            if not blocks:
                logging.warning("No serial data received yet; dropping capture for %s.", name)
                manifest.log(name, capture_num, t_down, t_up, None, None, None, None, "", "no_serial_data")
                continue

            sample_period = blocks[-1].sample_period
            margin_time = sample_margin * sample_period
            t_start = t_down - margin_time
            t_end = t_up + (margin_time*2)
            # Give the serial thread a moment to catch up to t_up if it hasn't yet.
            # covered = block_index.wait_for_coverage(t_end, timeout=5.0)
            blocks = block_index.snapshot()
            print(len(blocks))
            if False:
                byte_start = time_to_byte_offset(blocks, t_start, round_up=False)
                print(f"BYTE START {byte_start}")
                byte_end = byte_start + 4096 # time_to_byte_offset(blocks, t_end, round_up=True)
                
                if byte_start is None or byte_end is None or byte_end <= byte_start:
                    logging.warning(f"Could not resolve a valid byte range for %s; skipping. ({byte_start} - {byte_end})", name)
                    manifest.log(
                        name, capture_num, t_down, t_up,
                        byte_offset_to_sample_index(byte_start), byte_offset_to_sample_index(byte_end),
                        byte_start, byte_end, "", "no_valid_range",
                    )
                    continue

                raw_fh.seek(byte_start)
                data = raw_fh.read(byte_end - byte_start)
                
                
            next_block_processed = last_block_processed + 1
            while len(blocks) < (next_block_processed + 1):
                print(f"WAIT FOR BLOCK {next_block_processed} {len(blocks)} for {name}")
                #time.sleep(0.05)
                blocks = block_index.snapshot()
                
            blk = blocks[next_block_processed]
            if blk.block_index <= last_block_processed:
                print("ALREADY PROCESSED THIS BLOCK--SKIP!!")
                continue 
                
            if blk.num_bytes != 4096:
                print("BAD BLOCK BYTE COUNT?+?????")
                continue
            last_block_processed = blk.block_index
            byte_start = blk.start_byte_offset
            byte_end = byte_start + blk.num_bytes
            raw_fh.seek(byte_start)
            data = raw_fh.read(blk.num_bytes)
            out_path = output_dir / f"{name}_{session_id}_{capture_num:03d}.bin"
            with open(out_path, "wb") as out_fh:
                out_fh.write(data)

            status = "ok" # if covered else "ok_timed_out_waiting"
            manifest.log(
                name, capture_num, t_down, t_up,
                byte_offset_to_sample_index(byte_start), byte_offset_to_sample_index(byte_end),
                byte_start, byte_end, out_path.name, status,
            )

            logging.info(
                "Captured %s: %d bytes (%d samples) -> %s",
                name, len(data), len(data) // BYTES_PER_SAMPLE, out_path.name,
            )
    finally:
        raw_fh.close()


# ----------------------------------------------------------------------------
# CLI / orchestration
# ----------------------------------------------------------------------------

def parse_sync_word(text: str) -> bytes:
    text = text.strip()
    try:
        b = bytes.fromhex(text)
    except ValueError as e:
        raise argparse.ArgumentTypeError(f"sync-word must be a hex string: {e}")
    if not (1 <= len(b) <= 4):
        raise argparse.ArgumentTypeError("sync-word must be 1-4 bytes (2-8 hex chars)")
    return b


def parse_args(argv=None) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Capture a synced serial sample stream alongside timestamped keystrokes."
    )
    p.add_argument("--port", default="/dev/ttyACM1", help="Serial port [%(default)s]")
    p.add_argument("--baudrate", type=int, default=1_000_000, help="Baud rate [%(default)s]")
    p.add_argument("--output-dir", required=True, type=Path, help="Directory for raw.bin, index, and KEY_*.bin files")
    p.add_argument(
        "--sync-word", default="DEADBEEF", type=parse_sync_word,
        help="Sync word as hex, 1-4 bytes [%(default)s]",
    )
    p.add_argument("--block-size", required=False, default=DefaultBlockSize, type=int, help="Bytes captured per block, between sync words [%(default)s]")
    p.add_argument("--sample-margin", required=False, type=int, default=DefaultSampleMargin, help="Samples of pre-roll before key-down to include in each extract [%(default)s]")
    p.add_argument("-v", "--verbose", action="store_true", help="Enable debug logging")
    p.add_argument("-s", "--session", required=True, type=str, help="Session identifier")
    return p.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(threadName)s: %(message)s",
    )

    output_dir: Path = args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)
    raw_path = output_dir / "raw.bin"
    index_path = output_dir / "blocks_index.csv"
    keystroke_log_path = output_dir / "keystrokes_log.csv"
    manifest_path = output_dir / "extraction_manifest.csv"

    if os.path.exists(raw_path):
        print(f'{raw_path} already exists, aborting')
        return 2
        
    logging.info(
        "Opening %s @ %d baud, sync=%s, block_size=%d, sample_margin=%d",
        args.port, args.baudrate, args.sync_word.hex().upper(),
        args.block_size, args.sample_margin,
    )

    try:
        ser = serial.Serial(args.port, args.baudrate, timeout=SERIAL_READ_TIMEOUT_S)
    except serial.SerialException as e:
        logging.error("Could not open serial port %s: %s", args.port, e)
        return 1

    block_index = BlockIndex(index_path)
    keystroke_log = KeystrokeLog(keystroke_log_path, block_index)
    manifest = ExtractionManifest(manifest_path)
    stop_event = threading.Event()
    extraction_q: "queue.Queue" = queue.Queue()

    kb = KeyboardCapture(extraction_q, keystroke_log)
    
    serial_thread = threading.Thread(
        target=serial_capture_loop,
        args=(ser, extraction_q, args.sync_word, args.block_size, raw_path, block_index, args.session, args.sample_margin, output_dir, manifest,
        stop_event),
        name="serial-capture",
        daemon=True,
    )
    extraction_thread = threading.Thread(
        target=extraction_worker,
        args=(extraction_q, block_index, raw_path, args.session, args.sample_margin, output_dir, manifest, stop_event),
        name="extraction",
        daemon=True,
    )


    serial_thread.start()
    # extraction_thread.start()
    kb.start()

    logging.info("Capturing. Press Ctrl+C in this terminal to stop.")
    try:
        while serial_thread.is_alive():
            time.sleep(0.2)
        logging.warning("Serial capture thread exited on its own (port closed / error).")
    except KeyboardInterrupt:
        logging.info("Stopping (Ctrl+C received)...")
    finally:
        stop_event.set()
        kb.stop()
        serial_thread.join(timeout=2)
        #extraction_thread.join(timeout=5)
        block_index.close()
        keystroke_log.close()
        manifest.close()
        try:
            ser.close()
        except Exception:
            pass

    logging.info("Done. Output in %s", output_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())

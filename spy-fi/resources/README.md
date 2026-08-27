## Spy-Fi Key Snooper Resources

This is... messy.  Sorry.

In essence, contains the scripts used to capture training sessions and package them for training.

### Capture

With keycapture app running on DK, keycaptureburst and randomtyper were used together to generate the data

The burst capture script does the heavy lifting, e.g.
   ./keycaptureburst.py --output-dir sessions/s1  --session s1

Defaults were what I used, but help shows what you can tweak.  Best to save sessions under subdirs of sessions/ here, just because that's what I did and it's prob hard-coded left and right.

```
./keycaptureburst.py --help
usage: keycaptureburst.py [-h] [--port PORT] [--baudrate BAUDRATE] --output-dir OUTPUT_DIR
                          [--sync-word SYNC_WORD] [--block-size BLOCK_SIZE]
                          [--sample-margin SAMPLE_MARGIN] [-v] -s SESSION

Capture a synced serial sample stream alongside timestamped keystrokes.

options:
  -h, --help            show this help message and exit
  --port PORT           Serial port [/dev/ttyACM1]
  --baudrate BAUDRATE   Baud rate [1000000]
  --output-dir OUTPUT_DIR
                        Directory for raw.bin, index, and KEY_*.bin files
  --sync-word SYNC_WORD
                        Sync word as hex, 1-4 bytes [DEADBEEF]
  --block-size BLOCK_SIZE
                        Bytes captured per block, between sync words [4096]
  --sample-margin SAMPLE_MARGIN
                        Samples of pre-roll before key-down to include in each extract
                        [30]
  -v, --verbose         Enable debug logging
  -s SESSION, --session SESSION
                        Session identifier
```

Random Typer script just shows you samples to type in.


### Training

Assuming sessions are under sessions/*
```
./concat_targets.sh /tmp/DESTINATION
```

Would massage things into the swap space of /tmp/DESTINATION.  Originally meant for normalizing our captures without affecting them, but whatever.  You should probably peak in there, or maybe just point the next script just to sessions/

Then
```
./generate_training_csv.py /tmp/DESTINATION /tmp/somename
```

Would generate the CSV and zip it up as /tmp/somename.zip.  That's what you throw to the Edge AI Lab.




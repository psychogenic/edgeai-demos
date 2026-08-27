## Micky's Gym Resources

For the boxing classifier/trainer/game, these resources let you capture, inspect and prepare data for training model.

### Data collection

With the mighty_mick_gym app on the tags, proj.conf flags setup correctly, namely:

```
# make these both y for collecting training data
CONFIG_DATA_COLLECTION_MODE=y
CONFIG_IMU_SEND_BURSTS=y

# make this y for collecting idle/guard continuously
CONFIG_CONTINUOUS_COLLECTION=n

```

then functionality is split up as:

  * training_run.py prompts you for punches, using 

  * sample_collect.py to collect a session of a given target for both hands, by using multiple
  
  * mick_collect.py to connect to a tag and gather data
  
Split up in this excessive way to let you work your way up the chain, and stop at any level for le debug.


The plotaccel lets you see contents of event sample bin files.

### Training 

The concat_targets.sh has some expectations, see within, then 

```
./generate_training_csv.py /tmp/DESTINATION /tmp/somename
```

Would generate the CSV and zip it up as /tmp/somename.zip.  That's what you throw to the Edge AI Lab.

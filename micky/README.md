### Mighty Mick's Boxing Gym

This is the embedded code and some resources I used to collect training data and prepare it for the Nordic Edge AI lab.


The [prj.conf](./mighty_mick_gym/configuration/nrf54l15tag_nrf54l15_cpuapp/prj.conf) file (for the tags) has entries for:

```
# make these both y for collecting training data
CONFIG_DATA_COLLECTION_MODE=n
CONFIG_IMU_SEND_BURSTS=n


# make this y for collecting idle/guard continuously
CONFIG_CONTINUOUS_COLLECTION=n

```

When training punches, the first two should be `y`.  When training guard/idle, you also want `CONFIG_CONTINUOUS_COLLECTION=y`.
Would be smart to have a way to just tell it to switch to continous or not but hey, exercise for the reader.


Build and flash [mighty_mick_gym](./mighty_mick_gym)

```
west build -p always -b nrf54l15tag/nrf54l15/cpuapp path/to/mighty_mick_gym
west flash
```

Data collection happens with the scripts in [resources](./resources).
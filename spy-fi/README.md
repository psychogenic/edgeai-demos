## Spy-Fi / BlueEar acoustic side-channel Axon demo

This is my "wireless keyboard" snooper (through the sound of my keyboard).

The [keycapture](./keycapture/) directory is what I used to train.  Install it on the DK, with something like

```
west build -p always -b nrf54lm20dk/nrf54lm20b/cpuapp path/to/keycapture
west flash
```

Once that's running and you can see binary junk on the serial port (/dev/ttyACM1 on my system), it's ready to for grabbing training data.

See the [resources](./resources/) dir for scripts to actually capture samples and prepare training data from the desktop.

The [keysnoop](./keysnoop/) directory is the actual Axon inference runner, a hacked up version of the keyword/wakeword app demo from Nordic Semi.

```
west build -p always -b nrf54lm20dk/nrf54lm20b/cpuapp path/to/keysnoop
west flash
```

I've throttled the keysnoop/src/keycap/nrf_edgeai_generated/ model in there, so as to avoid leaking it and making my (mostest favorite) keyboard vulnerable.  Go through training and dump resultant files in there, if you want to actually try it.


## Spy-Fi / BlueEar acoustic side-channel Axon demo

This is my "wireless keyboard" snooper (through the sound of my keyboard).

The keycapture/ directory is what I used to train.  Install it on the DK, see the resources/ dir for scripts to actually capture samples and prepare training data.

The keysnoop/ directory is the actual Axon inference runner, a hacked up version of the keyword/wakeword app demo from Nordic Semi.

I've throttled the keysnoop/src/keycap/nrf_edgeai_generated/ model in there, so as to avoid leaking it and making my (mostest favorite) keyboard vulnerable.  Go through training and dump resultant files in there, if you want to actually try it.


## New

## Fixed

- An attempted fix for the frame rate dropping during the opening on Android phones like the moto g24 with Frame rate Original. The check added in v0.11.12, which decides in the first moments of play whether the phone's graphics driver holds each picture until it is drawn, never finished on that phone, so the improvement it enables never turned on; the check now times each picture against the end of its drawing and the one before it, and decides.

## Fixed

- On phones with a Mali or PowerVR graphics chip, Vsync Off no longer switches to the other way of showing pictures after a few held-back ones; that switch was measured only on Qualcomm chips, and on a Mali it made the pictures wait longer.
- With Frame rate set to Original, the game no longer waits for the screen to take each picture before it goes on: on a slow graphics chip the game keeps its speed and the pictures come as fast as the chip draws them.
- The log now says how busy the processor cores and the graphics chip were, which cores each part of the game ran on, and how much the 3D drawing cost, so slow phones can be understood from one log.

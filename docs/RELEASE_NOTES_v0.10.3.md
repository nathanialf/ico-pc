## New

- Options > Gameplay is now four pages: Gameplay, Yorda, Shadows and Cutscenes, holding the Game variants rows; the Debug menu row is on the Developer page.
- Options > Picture has four new rows: Gamma, Picture brightness, Contrast and Saturation. 50 is the game's own picture; they also work with the CRT filter.
- Options > Extras has an Unused content page: two rooms the makers used for testing, the European disc's second attract film, and title-screen pictures from an early show version.

## Fixed

- On phones with a Mali or PowerVR graphics chip, Vsync Off no longer switches to the other way of showing pictures after a few held-back ones; that switch was measured only on Qualcomm chips, and on a Mali it made the pictures wait longer.
- With Frame rate set to Original, the game no longer waits for the screen to take each picture before it goes on: on a slow graphics chip the game keeps its speed and the pictures come as fast as the chip draws them.
- The log now says how busy the processor cores and the graphics chip were, which cores each part of the game ran on, and how much the 3D drawing cost, so slow phones can be understood from one log.

## New

- Options > Gameplay is now four pages: Gameplay, Yorda, Shadows and Cutscenes, holding the Game variants rows; the Debug menu row is on the Developer page.
- Options > Picture has four new rows: Gamma, Picture brightness, Contrast and Saturation. 50 is the game's own picture; they also work with the CRT filter.

## Fixed

- On phones with a Mali or PowerVR graphics chip, Vsync Off no longer switches to the other way of showing pictures after a few held-back ones; that switch was measured only on Qualcomm chips, and on a Mali it made the pictures wait longer.
- With Frame rate set to Original, the game no longer waits for the screen to take each picture before it goes on: on a slow graphics chip the game keeps its speed and the pictures come as fast as the chip draws them.

## New


## Fixed

- An attempted fix for the game pausing for a moment after a brief graphics stall on some Android phones (the opening cinematic on a Mali phone): the game's own background copies and automatic captures no longer wait for the slow picture to finish.
- An attempted fix for the unsteady frame rate in busy scenes on some phones like the moto g24, such as the boat in the opening: the game no longer asks the graphics chip to draw the tiny pieces of scenery that are too small to show up in any pixel. It checks itself on the phone at start-up and turns this off if anything looks wrong.

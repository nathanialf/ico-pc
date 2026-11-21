## New


## Fixed

- An attempted fix for the game pausing for a moment after a brief graphics stall on some Android phones (the opening cinematic on a Mali phone): the game's own background copies and automatic captures no longer wait for the slow picture to finish.
- An attempted fix for the unsteady frame rate in busy scenes on some phones like the moto g24, such as the boat in the opening: the game no longer asks the graphics chip to draw the tiny pieces of scenery that are too small to show up in any pixel. It checks itself on the phone at start-up and turns this off if anything looks wrong.
- An attempted fix for uneven speed in the opening cinematic and slowdowns in ordinary play on some Android phones (a Mali phone) with Frame rate Original: the next picture now goes to the graphics chip while the one before is still finishing, so the chip no longer waits between pictures; this also repairs a slowdown in ordinary play that the previous version brought to such phones.
- With the North American disc, the room of the dream in the opening (its walls, floor and windows) is drawn; before, Ico stood in an empty cloudy sky.

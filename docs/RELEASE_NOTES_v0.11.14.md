## New

## Fixed

- An attempted fix for the game pausing for a moment after a brief graphics stall on some Android phones (the opening cinematic on a Mali phone): the game's own background copies and automatic captures no longer wait for the slow picture to finish.
- An attempted fix for uneven speed in the opening cinematic and slowdowns in ordinary play on some Android phones (a Mali phone) with Frame rate Original: the next picture now goes to the graphics chip while the one before is still finishing, so the chip no longer waits between pictures; this also repairs a slowdown in ordinary play that the previous version brought to such phones.

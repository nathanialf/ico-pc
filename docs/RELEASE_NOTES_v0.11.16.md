## New

## Fixed

- An attempted fix for the slowdown in the boat scene and the cage scene on phones like the moto g24: a start-up check of the faster way of drawing scenery close to the camera wrongly failed on these phones, so that drawing was never used there; the check now prepares everything it compares before it starts, and the game times that way of drawing on its own while you play (never on the title screen) and keeps it only where it is faster.

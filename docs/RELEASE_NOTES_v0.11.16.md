## New

- Save states now load whatever your settings are: a state made with other settings (Hold type, Zoom type, Camera reset, the Gameplay rows and the rest) used to be refused, and now loads with the settings you have now. A state made in the other of 50 Hz and 60 Hz plays at its own until you restart the game.
- Options > Controls > Look at Yorda: choose Hold or Toggle to turn Ico and the camera toward Yorda with L3 (click the left stick in) without calling her, so she stays where she is.
- The log's summary of where each scene's time went now also compares each part of the picture with the graphics chip's speeds measured at start-up, and says when most of a part's time must go to something other than the shapes themselves.

## Fixed

- An attempted fix for the slowdown in the boat scene and the cage scene on phones like the moto g24: a start-up check of the faster way of drawing scenery close to the camera wrongly failed on these phones, so that drawing was never used there; the check now prepares everything it compares before it starts, and the game times that way of drawing on its own while you play (never on the title screen) and keeps it only where it is faster.

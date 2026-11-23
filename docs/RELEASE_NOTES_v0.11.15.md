## New

- Options > Controls > Zoom type: choose Toggle to press the zoom button (R2) once to zoom the camera in and again to zoom it out, instead of holding it.
- Options > Controls > Camera reset: turn it on to bring the camera straight back to Ico with a press of R3 (click the right stick in) after looking around.
- Pop-up messages are smaller, and several of them, the save state messages first, say the same in fewer words.
- The log now also notes the phone's temperature, whether it is slowing the processor to cool down, the battery's draw, and a short summary of where each scene's time went, so a shared log shows more clearly what to improve next.
- At its first start on a device the game times a few small test pictures, under a second, and writes in its log how fast the graphics chip does each kind of drawing, so a log from a slow phone shows what its time goes into.

## Fixed

- An attempted fix for short freezes in the title and menus on some Android phones after a slow picture, when the game made its background copy.
- An attempted fix for the stutter in the boat scene on phones like the moto g24: the skipping of tiny pieces of the picture added in the previous version did not help there, so the game now times it with and without once at the start of play (the first scene after the title) and keeps the faster for the session.
- An attempted fix for the stutter in busy scenes such as the boat on phones like the moto g24: some of the scenery is now drawn in a way that can let the graphics chip skip hidden parts sooner. It checks itself when the game starts and is timed once early in play, in the first scene where it is used: it is kept for the session only if it measured faster, and it stays off after a graphics problem.

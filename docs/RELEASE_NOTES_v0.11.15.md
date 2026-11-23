## New

- Options > Controls > Zoom type: choose Toggle to press the zoom button (R2) once to zoom the camera in and again to zoom it out, instead of holding it.
- Pop-up messages are smaller, and several of them, the save state messages first, say the same in fewer words.

## Fixed

- An attempted fix for short freezes in the title and menus on some Android phones after a slow picture, when the game made its background copy.
- An attempted fix for the stutter in the boat scene on phones like the moto g24: the skipping of tiny pieces of the picture added in the previous version did not help there, so the game now times it with and without once at the start of play (the first scene after the title) and keeps the faster for the session. A message, "Finding the best settings", shows for those few seconds and goes away by itself; Triangle hides it.
- An attempted fix for the stutter in busy scenes such as the boat on phones like the moto g24: some of the scenery is now drawn in a way that can let the graphics chip skip hidden parts sooner. It checks itself when the game starts and is timed once early in play, in the first scene where it is used: it is kept for the session only if it measured faster, and it stays off after a graphics problem.

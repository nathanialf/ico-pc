## New

- A Glow size row on the Effects page (Options > Effects): Scaled draws the glow, the blur on far things and the screen softening as finely as the rest of the picture; Original draws them at the PlayStation 2's size, which is much lighter on the graphics card. Try Original if the game is slow on an older graphics card.
- The log now writes down when each piece of music starts and ends and how loud it plays, and which way the game draws see-through things on your graphics card. If the music goes quiet or see-through things look wrong, send the log with your report.
- When the game ends on an error, it now writes one file next to the log, named report- with the date, that holds everything a report needs: the log, the previous log, the settings and what the graphics were drawing. Attach that one file to your report. Crash reports also name the place in the game's code now, so they can be acted on at once.

## Fixed

- The graphics error as the title appears on Android phones with a Mali graphics chip should no longer happen. If the graphics still stop, the game now checks the picture step by step at the next start, down to the single drawing that stops them, and writes it into the log. Please report if it still happens and attach the log.
- The music no longer goes silent after the final fight.
- See-through things are now drawn right on the AYN Odin 3 and other devices with a Qualcomm Adreno graphics chip: no more white boxes around leaves, washed-out title screen or white-on-white menus. The game now checks at start that it draws them right and uses a simpler way when it does not.
- The model viewer (Extras > Models) no longer crashes when it plays the boy's swimming animation.
- The game no longer closes by itself when it is left paused for a few minutes on Windows. When the system holds the game's window for a while (it did so for about a minute at a time after a controller turned itself off), the game waits instead of ending the run, keeps the screen awake itself, and writes what held it into the log.
- The trolley's wheels keep turning smoothly at full speed through camera cuts; before, one cut left them stepping until the trolley slowed down. The log now says which drawing stopped being smoothed and why, if it happens again.
- On Android devices with an Adreno 740 graphics chip (the AYN Thor and the Odin 2), the game no longer freezes the device or closes while preparing its graphics: on those devices it skips that preparation and builds what it needs as it goes, and it uses smaller 3D shaders there. On every other device, a start that stops while preparing the graphics makes the next start take the same path on its own. If a device still freezes once, reboot it and start the game again.
- The ripples in the puddles of the drained Waterfall room are faint rings again on wide screens and with Full pixel on, not bright pieces. The log also now says, with each F12 picture, how the water and its reflections were drawn, so a report about the puddles can be acted on.

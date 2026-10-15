## New

- A Glow size row on the Effects page (Options > Effects): Scaled draws the glow, the blur on far things and the screen softening as finely as the rest of the picture; Original draws them at the PlayStation 2's size, which is much lighter on the graphics card. Try Original if the game is slow on an older graphics card.
- The log now writes down when each piece of music starts and ends and how loud it plays, and which way the game draws see-through things on your graphics card. If the music goes quiet or see-through things look wrong, send the log with your report.

## Fixed

- The graphics error as the title appears on Android phones with a Mali graphics chip should no longer happen. If the graphics still stop, the game now checks the picture step by step at the next start, down to the single drawing that stops them, and writes it into the log. Please report if it still happens and attach the log.
- The music no longer goes silent after the final fight.
- See-through things are now drawn right on the AYN Odin 3 and other devices with a Qualcomm Adreno graphics chip: no more white boxes around leaves, washed-out title screen or white-on-white menus. The game now checks at start that it draws them right and uses a simpler way when it does not.
- The model viewer (Extras > Models) no longer crashes when it plays the boy's swimming animation.

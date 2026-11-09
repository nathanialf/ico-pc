## New

(kept current after every merge; player words only)

- On Windows the game's program file now takes the icon made from your disc: it is put into the file as the game closes, after the title screen has made the icon, so it shows from the next start. A copy in a folder you cannot write to keeps the plain icon.
- On Android the recent-apps card now shows the icon made from your disc, made at the first title screen; the home-screen icon stays the plain one.
- Holding Left or Right on a slider row in Options now keeps changing it, and L1 and R1 change the Picture colour rows in steps of 10.
- The New Game screen now shows which Mirror mode and New Game+ choices are set and starts the game from its own Start row; pressing Cross on a choice only picks it and moves down.
- The menus' words have a solid dark-grey outline that fades out quickly, like the lettering at the foot of the title screen, instead of a soft grey halo.

## Fixed

(kept current after every merge; player words only)

- On tablets and handhelds with the Adreno 740 chip running its own graphics driver, v0.11.0 drew a screen of noise and froze within seconds of the first scene. The game now draws the 3D scenery the old way on that chip and driver; a Turnip driver package keeps the faster way. When the graphics driver stops responding, the log now says which file the game is writing while it saves its report.
- On phones the pre-computation of the 3D scenery no longer goes through the screen's drawing hardware and back, where the phone's graphics driver allows it; cutscenes and busy scenes on slower phones should run faster.
- On Windows a sound device that took a long time to open (seen with a DualSense controller on USB) could stop the game from starting at all. The game now starts at once and the sound joins when the device is ready.
- With Upscaling set to FSR 3.1, the distance fog could show thin vertical lines that stayed in place while the picture moved; the fog's fine pattern now changes with each frame's tiny picture shift so the upscaler smooths it out.
- The short cut to black on the title screen while the game made its icon is gone; the title stays on screen. The icon itself is now the title's letter I instead of a plain cream square; it is made again after this update.

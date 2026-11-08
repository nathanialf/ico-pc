## New

- The New Game screen now shows which Mirror mode and New Game+ choices are set, hides the title lettering, and starts the game from its own Start row; pressing Cross on a choice only picks it and moves down.
- The menus' words have a solid dark-grey outline that fades out quickly, like the lettering at the foot of the title screen, instead of a soft grey halo.

## Fixed

- On tablets and handhelds with the Adreno 740 chip running its own graphics driver, v0.11.0 drew a screen of noise and froze within seconds of the first scene. The game now draws the 3D scenery the old way on that chip and driver; a Turnip driver package keeps the faster way. When the graphics driver stops responding, the log now says which file the game is writing while it saves its report.
- On phones and other tile-based graphics chips the pre-computation of the 3D scenery no longer goes through the screen's drawing hardware and back; cutscenes and busy scenes on slower phones should run faster again.

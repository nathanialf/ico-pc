## New

(kept current after every merge; player words only)

- The New Game screen now shows which Mirror mode and New Game+ choices are set, hides the title lettering, and starts the game from its own Start row; pressing Cross on a choice only picks it and moves down.

## Fixed

(kept current after every merge; player words only)

- On tablets and handhelds with the Adreno 740 chip running its own graphics driver, v0.11.0 drew a screen of noise and froze within seconds of the first scene. The game now draws the 3D scenery the old way on that chip and driver; a Turnip driver package keeps the faster way. When the graphics driver stops responding, the log now says which file the game is writing while it saves its report.
- With Upscaling set to FSR 3.1, the distance fog could show thin vertical lines that stayed in place while the picture moved; the fog's fine pattern now moves with the picture.

## New

- Options > Controls > Touch controls has a new choice, Always, Start only in scenes: during a scene only the Start button stays on screen, so it can be skipped.
- On phones, the log now records the graphics chip's clock speed, how warm the phone was and whether the screen was being recorded whenever a picture is held back, and which part of the game (a scene, play, a menu) each ten seconds were in, so stutters can be told apart.
- On phones, the log now records the graphics chip's clock speed, how warm the phone was and whether the screen was being recorded whenever a picture is held back, so stutters can be told apart.

## Fixed

- On phones with a Qualcomm graphics driver and Vsync off, pictures now go to the screen one after another from the start, as the chip maker advises, instead of switching to that only after a few held-back pictures; this is not expected to remove the stutter some of these phones report.
- After the game had turned on its slower, safer graphics mode following two crashes of the graphics driver, it now turns that mode off again by itself after three clean starts, or after an update.
- Options > Gameplay > Time to rescue Yorda is now set in seconds and changes the whole pull-in; before, only its last moment changed.
- On phones with only two fast processor cores, the game's own step now runs on one of them.
- The text effect's shader is a third of its old size, so phones prepare it in a fraction of the time; the picture is the same.

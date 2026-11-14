## New

- The log now says more about short freezes on phones and tablets: every freeze gets its own line with what the graphics chip was working on when it stopped, so the next log can show where they come from.
- The Options page that holds the chain settings is now called Chains (it was called Yorda).
- The log now names the objects that move without smoothing between game frames, and times the moments when the picture rate drops to one picture per game frame, so a report of slow-looking movement can be traced.

## Fixed

- On phones with the Mali-G52 graphics chip, v0.11.6 showed a black picture with flickering touch controls: the change that skipped unseen scenery there is switched off again on that chip, as in v0.11.5.
- A graphics failure that happened while the game was in the background is now noticed at the next start.
- An attempt at the short freezes on tablets and phones with Adreno graphics on Qualcomm's own driver: the game now asks Android to run its graphics work ahead of other apps' (the picture is unchanged).
- The frame rate counter no longer shows a very low number after you return to the game, turn the screen or save a picture for a bug report.
- After a short graphics freeze the game no longer mistakes its own check for a fault and saves a capture, which could cause another freeze.
- An attempt at moving objects looking slow on phones running Android 16 or later with the frame rate uncapped: when the picture rate drops to one picture per game frame, the game now tells Android it needs more processor and graphics speed (the picture itself is unchanged).

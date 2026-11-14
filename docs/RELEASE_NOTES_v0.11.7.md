## New

- The log now says more about short freezes on phones and tablets: every freeze gets its own line with what the graphics chip was working on when it stopped, so the next log can show where they come from.

## Fixed

- On phones with the Mali-G52 graphics chip, v0.11.6 showed a black picture with flickering touch controls. The change that most likely caused it is switched off there, and if the picture still does not come out complete, the game now notices within seconds and switches the other new changes off by itself, one at a time, until it does. It remembers what it switched off for the next start.
- When the graphics driver failed just as the game went to the background, the next start did not know about it. It now does, and takes the same safety steps as after any other graphics failure.
- An attempt at the short freezes on tablets and phones with Adreno graphics on Qualcomm's own driver: the game now asks Android to run its graphics work ahead of other apps' (the picture is unchanged).
- The frame rate counter no longer shows a very low number after you return to the game, turn the screen or save a picture for a bug report.
- After a short graphics freeze the game no longer mistakes its own check for a fault and saves a capture, which could cause another freeze.

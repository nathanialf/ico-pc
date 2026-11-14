## New

- The log now says more about short freezes on phones and tablets: every freeze gets its own line with what the graphics chip was working on when it stopped, so the next log can show where they come from.
- The Options page that holds the chain settings is now called Chains (it was called Yorda).
- The log now lists, at start, the adjustments the game makes for your graphics chip and driver, with the reason for each.
- The log now names the objects that move without smoothing between game frames, and times the moments when the picture rate drops to one picture per game frame, so a report of slow-looking movement can be traced.
- The log now follows, second by second, which processor cores the game's main threads ran on and how fast those cores were running whenever the picture rate drops to one picture per game frame.
- The log now follows each request the game makes to Android for more speed for five seconds, so the next log shows whether the request helped.
- Above the original resolution, characters' shadows are now drawn at the picture's resolution and no longer look blocky; they keep the original's softness. At the original resolution nothing changes.

## Fixed

- On phones with the Mali-G52 graphics chip, v0.11.6 showed a black picture with flickering touch controls: the change that skipped unseen scenery there is switched off again on that chip, as in v0.11.5.
- A graphics failure that happened while the game was in the background is now noticed at the next start.
- An attempt at the short freezes on tablets and phones with Adreno graphics on Qualcomm's own driver: the game now asks Android to run its graphics work ahead of other apps' (the picture is unchanged).
- The frame rate counter no longer shows a very low number after you return to the game, turn the screen or save a picture for a bug report.
- After a short graphics freeze the game no longer mistakes its own check for a fault and saves a capture, which could cause another freeze.
- An attempt at moving objects looking slow on phones with several fast processor cores (the frame rate uncapped): when the picture rate drops to one picture per game frame, the game now stops holding its main threads to single cores and lets Android place them, until the picture rate has recovered for ten seconds (the picture itself is unchanged).
- The log's list of objects that move only every other game frame named torches, chains and pots that stand still; it now judges movement the way the smoothing does and says how often each named object moves.
- An attempt at moving objects looking slow on phones running Android 16 or later with the frame rate uncapped: when the picture rate drops to one picture per game frame, the game now tells Android it needs more processor and graphics speed (the picture itself is unchanged).

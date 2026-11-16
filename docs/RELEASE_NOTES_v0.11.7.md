## New

- The log now says more about short freezes on phones and tablets: every freeze gets its own line with what the graphics chip was working on when it stopped, so the next log can show where they come from.
- The Options page that holds the chain settings is now called Chains (it was called Yorda).
- The log now lists, at start, the adjustments the game makes for your graphics chip and driver, with the reason for each.
- The log now names the objects that move without smoothing between game frames and says how often each of them moves, and times the moments when the picture rate drops to one picture per game frame, so a report of slow-looking movement can be traced.
- The log now follows, second by second, which processor cores the game's main threads ran on and how fast those cores were running whenever the picture rate drops to one picture per game frame.
- When the picture is drawn above the original resolution (a higher Resolution, or supersampling), characters' shadows are now drawn at the picture's resolution and no longer look blocky; they keep the original's softness. In the Original preset, and at Resolution 1x without supersampling, nothing changes.
- Reflections in water, and the picture seen through the Queen's barrier, are now always drawn as sharp as the rest of the picture, so the Water reflections option has been removed. In the Original preset, and at Resolution 1x without supersampling, nothing changes.

## Fixed

- On phones with the Mali-G52 graphics chip, v0.11.6 showed a black picture with flickering touch controls: the change that skipped unseen scenery there is switched off again on that chip, as in v0.11.5.
- A graphics failure that happened while the game was in the background is now noticed at the next start.
- An attempt at the short freezes on tablets and phones with Adreno graphics on Qualcomm's own driver: the game now asks Android to run its graphics work ahead of other apps' (the picture is unchanged).
- The frame rate counter no longer shows a very low number after you return to the game, turn the screen or save a picture for a bug report.
- After a short graphics freeze the game no longer mistakes its own check for a fault and saves a capture, which could cause another freeze.
- An attempt at moving objects looking slow on phones with several fast processor cores (the frame rate uncapped): when the picture rate drops to one picture per game frame, the game now stops holding its main threads to single cores and lets Android place them, until the picture rate has recovered for ten seconds (the picture itself is unchanged).
- On the North American disc and the two 2001 preview builds, a shadow that reached far past the edge of the picture could be cut short or lose a piece inside the picture; shadows there now reach as far as they do on the console.

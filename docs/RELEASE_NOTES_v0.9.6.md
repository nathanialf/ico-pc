## New

- Options > Gameplay > Shadow speed: shadows walk, run and fly slower or faster than on the PlayStation 2 (Slower, Original, Faster, Fastest).

## Fixed

- The game no longer pauses for a moment every half minute while you lead Yorda on a slow drive (the achievements file is now written in the background).
- The pause menu's Yorda captured count no longer counts one capture twice.
- On the North American disc and the 2001 preview discs, the rescue achievement and the captured count work.
- On the June 2001 demo, the pause menu's deaths count now counts a game over.
- After a brief hitch the game no longer stays at 30 frames per second for a minute or more with Frame rate 60 and vsync on; it tries 60 again within two seconds.
- Direct3D 11 draws the world with far fewer draw calls, as the other graphics APIs have since v0.9.4; rooms with many objects run faster on slower processors.
- On Windows the log now reports how much video memory the game uses against the card's budget, and the upload memory is given back after large loads (helps cards with 1 GB).
- The log says when the game data or the save folder sits on a network or removable drive, which can cause brief pauses and sound cut-outs during loads.
- Loading textures pauses the game for much less time on slower processors (the credits' music cut out at those moments), and the log now says what a long pause was doing when the sound cut out.

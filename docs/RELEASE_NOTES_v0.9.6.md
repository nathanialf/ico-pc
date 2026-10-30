**Please note:** Save states are new: tell us when one fails to load, with the log (the newest file in the `logs` folder that starts with `report-`, or `ico-pc.log`). Android 8 phones and other Vulkan 1.0 devices have old graphics drivers: the first start prepares the graphics for several minutes and some devices may fail; send the log.

## New

- Save states: the pause menu's Save states page keeps four snapshots of the game per disc with a picture, area and play time; load them from the pause menu or from Options > Extras on the title. F5 saves to slot 1 and F9 loads it. A state belongs to the version of the game it was saved with.
- Options > Gameplay > Shadow speed: shadows walk, run and fly slower or faster than on the PlayStation 2 (Slower, Original, Faster, Fastest).
- Android 8 and Vulkan 1.0 graphics drivers are supported (phones and handhelds from 2017 on; graphics driver packages still need Android 9).

## Fixed

- With the D-pad, Cross or Circle switched off, a tap on a menu line now picks the line even when a hidden touch button lies over it (a hidden Start no longer closes the pause menu when you tap Options); a button you can see still takes the tap.
- The frame rate counter no longer sits on a touch control you moved: with the touch controls showing it takes the first free place along the top edge, and controls you hid or switched off are ignored.
- The game no longer pauses for a moment every half minute while you lead Yorda on a slow drive (the achievements file is now written in the background).
- The pause menu's Yorda captured count no longer counts one capture twice.
- On the North American disc and the 2001 preview discs, the rescue achievement and the captured count work.
- On the June 2001 demo, the pause menu's deaths count now counts a game over.
- After a brief hitch the game no longer stays at 30 frames per second for a minute or more with Frame rate 60 and vsync on; it tries 60 again within two seconds.
- Direct3D 11 draws the world with far fewer draw calls, as the other graphics APIs have since v0.9.4; rooms with many objects run faster on slower processors.
- On Windows the log now reports how much video memory the game uses against the card's budget, and the upload memory is given back after large loads (helps cards with 1 GB).
- The log says when the game data or the save folder sits on a network or removable drive, which can cause brief pauses and sound cut-outs during loads.
- Loading textures pauses the game for much less time on slower processors (the credits' music cut out at those moments), and the log now says what a long pause was doing when the sound cut out.
- On the June 2001 demo disc, a defect in how Ico reads his surroundings is fixed: the distance at which he notices a wall in front of him was taken from the wrong data, so wall and ledge checks could trigger too near or too far.
- With Frame rate Original, a graphics card too slow to draw every picture in time no longer slows the game down or makes the sound come and go (on the title screen at higher resolutions, for example): pictures are dropped instead and the game keeps its pace.

**Please note:** Save states are new: tell us when one fails to load, with the log (the newest file in the `logs` folder that starts with `report-`, or `ico-pc.log`). Android 8 phones and other Vulkan 1.0 devices have old graphics drivers: the first start prepares the graphics for several minutes and some devices may fail; send the log.

## New

- Save states: the pause menu's Save states page keeps four snapshots of the game per disc with a picture, area and play time; load them from the Continue screen or the pause menu. F5 saves to slot 1 and F9 loads it. A state belongs to the version of the game it was saved with.
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
- On phones with Mali graphics, rooms with many objects run faster.
- On Windows the log now reports how much video memory the game uses against the card's budget, and the upload memory is given back after large loads (helps cards with 1 GB).
- The log says when the game data or the save folder sits on a network or removable drive, which can cause brief pauses and sound cut-outs during loads.
- Loading textures pauses the game for much less time on slower processors (the credits' music cut out at those moments), and the log now says what a long pause was doing when the sound cut out.
- On the June 2001 demo disc, a defect in how Ico reads his surroundings is fixed: the distance at which he notices a wall in front of him was taken from the wrong data, so wall and ledge checks could go wrong.
- With Frame rate Original, a graphics card too slow to draw every picture in time no longer slows the game down or makes the sound come and go (on the title screen at higher resolutions, for example): pictures are dropped instead and the game keeps its pace.
- On the North American disc and the 2001 preview discs, torches and candles that start out burning stay lit: before, they went out at once (no flame and no fire sound), so a stick could not be lit from them and the candle puzzle could not be finished.
- **Save a picture for a bug report** now puts a picture of what the screen showed into the `.zip` itself (at most 1920 pixels wide), next to the saved frame and the log, so the one file you send shows the same moment.
- When no game data and no disc image is found at the start, the box now says which two folders the game looked in and what to do if you played an earlier version (copy its `userdata` folder next to the new program, or extract the new version over the old folder), and it names the data file of the disc you played last. The window that asks for the disc image names the disc the game expects.
- On phones and handhelds, when the graphics driver stops answering while the game prepares its graphics, the game now closes itself after about 10 seconds with a message instead of freezing the device; when the piece it stopped on is a kind of shape the game can draw another way (seen on the Retroid Pocket 6), later starts draw those shapes that way and never prepare that piece again. The game also stops preparing graphics up front when the device's free memory runs low, and prepares the rest as you play.
- On some phones (Mali graphics, and some driver packages) parts of the world or characters could stretch across the screen as giant shapes or stripes.

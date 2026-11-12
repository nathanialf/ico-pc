## New

(kept current after every merge; player words only)

- Russian (bob-ttz): more of the menus are translated, among them upscaling, the combat options, the chain speed rows and free movement, and the difficulty screen (thanks, bob-ttz). The hardest difficulty's description stays in English for now.
- Phone speed. The game now skips the glow in rooms where it adds no light (at every resolution, the Original one included), draws several screen effects together instead of one by one, and leaves out graphics memory work it does not need. Measured on a Pixel 10 Pro Fold (Enhanced, native resolution, no frame limit): the cage room went from about 32 ms a picture (about 28 pictures a second) on v0.11.4 to about 13 ms (about 60 to 70 pictures a second), and the old bridge from about 19 ms to about 13 to 14.5 ms. The picture looks the same.
- Other phones and PCs get the changes that apply to them. Nothing was measured on Arm Mali or Qualcomm Adreno phones, on Windows, Linux or macOS, or over a long session (heat), so how much they gain is not known.
- On Android, the game now keeps its main threads on the phone's fastest processor cores and asks for them again if the phone moves them. On Android 13 and later, the game also tells the phone how long each picture may take and how long it took, so the phone can set its speed for the game's work. The picture, the resolution and the frame rate are not changed by either.
- On phones and some PCs, the menus and the pause screen are drawn together with the effects before them. A few pixels of menu text can differ by up to 3 shades.
- The log file now has a line naming the Android version and the phone, so a report shows them.

## Fixed

(kept current after every merge; player words only)

- The only message that appears by itself while you play is an achievement. The "Knocked down 1 of 3" message and the message when a Permadeath journey ends are gone (the count is still on the pause screen). Choosing a save of an ended journey still says "This journey has ended." Messages that answer something you just did in a menu, such as a saved photo or a save state, stay.
- Opening the pause menu on a phone no longer waits for the graphics chip to finish the picture it is drawing before it keeps the picture for your save.
- After a few slow pictures, the game now lowers its picture rate for at most about 2 seconds, and goes back sooner once the pictures fit again. Before, it could stay lowered for several seconds (seen on the old bridge on a phone).
- On phones with an Adreno graphics chip, the game's own check of how the characters are drawn does less extra drawing in the first minutes of play.
- Motion between game frames no longer steps backward for a moment after a few quick pictures.
- Motion between game frames is now timed from when each picture starts being drawn, not from when it was asked for.

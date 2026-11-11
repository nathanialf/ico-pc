## New

(kept current after every merge; player words only)

- Russian (bob-ttz): more of the menus are translated, among them upscaling, the combat options, the chain speed rows and free movement, and the difficulty screen (thanks, bob-ttz). The hardest difficulty's description stays in English for now.
- Above the Original resolution, the glow is not drawn in rooms where it adds no light (most rooms without the sun or the lamps in view), so those rooms take less work to draw; the picture is the same.
- Drawing the shadows takes less graphics memory work on phones with Arm Mali or Qualcomm Adreno graphics and on some PCs. The picture is exactly the same.
- The characters' shadows are drawn only where they fall, so the shadows take less graphics work on every device. The picture is exactly the same.
- On phones with PowerVR graphics (such as the Pixel 10 Pro Fold), each picture moves less data through graphics memory after a check the game makes on the phone at start. The picture is exactly the same.
- On phones, in most rooms with the glow, the fix for the thin gaps between surfaces is drawn together with the next effect instead of on its own, and in rooms with lamps the glow itself is drawn together with the next effect, so those rooms take less work to draw; the picture is exactly the same.
- On phones, a room's aura effect takes less work to draw when nothing on screen glows with it; the picture is exactly the same.
- On phones and some PCs, menus and the pause screen take less work to draw; a few pixels of menu text can differ by a shade too small to see.
- Above the Original resolution, the fix for the thin gaps between surfaces no longer copies the whole picture first where your device passes a check at start-up, and finds the places with no gap sooner, so it takes less work to draw; the picture is exactly the same.
- On Android 13 and later, the game tells the phone how long each picture may take and how long it took, so the phone can set its speed for the game's work. The picture, the resolution and the frame rate are not changed by it.

## Fixed

(kept current after every merge; player words only)

- The only message that now appears by itself while you play is an achievement. The "Knocked down 1 of 3" message and the message when a Permadeath journey ends are gone (the count is still on the pause screen). Choosing a save of an ended journey still says "This journey has ended." Messages that answer something you just did in a menu, such as a saved photo or a save state, stay.
- On phones with an Adreno graphics chip, the game's own check of how the characters are drawn does far less extra drawing in the first minutes of play.
- On phones, opening the pause menu keeps the picture for your save without first waiting for the graphics chip to finish the picture it is drawing (on a Mali phone that wait held the game for a moment at each pause).

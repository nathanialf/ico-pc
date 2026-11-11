## New

(kept current after every merge; player words only)

- Russian (bob-ttz): more of the menus are translated, among them upscaling, the combat options, the chain speed rows and free movement, and the difficulty screen (thanks, bob-ttz). The hardest difficulty's description stays in English for now.
- Above the Original resolution, the glow is not drawn in rooms where it adds no light (most rooms without the sun or the lamps in view), so those rooms take less work to draw; the picture is the same.
- Drawing the shadows takes less graphics memory work on phones with Arm Mali or Qualcomm Adreno graphics and on some PCs. The picture is exactly the same.
- On phones with PowerVR graphics (such as the Pixel 10 Pro Fold), each picture moves less data through graphics memory after a check the game makes on the phone at start. The picture is exactly the same.

## Fixed

(kept current after every merge; player words only)

- The only message that now appears by itself while you play is an achievement. The "Knocked down 1 of 3" message and the message when a Permadeath journey ends are gone (the count is still on the pause screen). Choosing a save of an ended journey still says "This journey has ended." Messages that answer something you just did in a menu, such as a saved photo or a save state, stay.
- On phones with an Adreno graphics chip, the game's own check of how the characters are drawn does far less extra drawing in the first minutes of play.
- On phones, opening the pause menu keeps the picture for your save without first waiting for the graphics chip to finish the picture it is drawing (on a Mali phone that wait held the game for a moment at each pause).

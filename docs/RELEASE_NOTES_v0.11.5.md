## New

(kept current after every merge; player words only)

- Faster on phones. Measured on a Pixel 10 Pro Fold (Enhanced, the phone's own resolution, no frame rate limit): the cage room went from about 28 pictures a second to about 60 to 70, the old bridge from about 50 to about 55 to 70, and the opening scenes before the title from about 28 to 40 up to about 43 to 55. The opening is still the slowest part.
- Other phones and PCs get the changes that apply to them, but nothing was measured on Mali or Adreno phones, or on Windows, Linux or macOS.
- The glow is skipped in rooms where it adds no light, at every resolution.
- In rooms with lamps or the sun in view, the glow is drawn in fewer steps. It looks the same, but its colours can differ by a few shades.
- On phones with PowerVR graphics, the lamps' light is drawn at the game's own resolution with smoothing. It looks the same.
- On phones, the pause screen and menus are drawn together with the effects before them. A few pixels of menu text can differ by up to 3 shades.
- On Android, the game keeps its main threads on the phone's fastest processor cores (phones with three or more fast cores), and on Android 13 and later it tells the phone how long each picture may take.
- On phones whose screen changes its refresh rate by itself, the Uncapped frame rate now asks the screen for its highest rate (Android 12 and later). The phone can still say no.
- Russian: more of the menus are translated (thanks, bob-ttz).
- The log file names the Android version and the phone.

## Fixed

(kept current after every merge; player words only)

- The only message that appears by itself while you play is an achievement. The "Knocked down" message and the message when a Permadeath journey ends are gone. Choosing a save of an ended journey still says "This journey has ended."
- With no frame rate limit, the picture rate no longer drops for seconds at a time after a few slow pictures, and the game no longer holds back pictures the phone had time to draw.
- Motion between game frames no longer steps backward or shows the same moment twice.
- Opening the pause menu on a phone no longer waits for the picture being drawn.
- On Adreno phones, the game's own check of how the characters are drawn does less extra drawing in the first minutes of play.

## New

- The log now lists, every 10 seconds, how many drawing steps the game takes for each picture, which steps take the graphics card longest, and what kind of graphics card the game found (named in the first lines). If the game is slow, send the log with your report.
- On phones and handhelds, the fog, some see-through effects and the glow blur are drawn in fewer steps, with the same picture.

## Fixed

- Blending between see-through things and what is behind them now uses the graphics card's own mixing on every computer and phone, as the game did up to v0.5.2. A few mixed colours can come out one shade lighter than on the PlayStation 2, which the eye cannot tell apart. The more exact mixing of v0.6.0 to v0.7.9 cost a lot of drawing steps on some cards and is now a developer switch. On cards that used it, the game also has fewer shaders to prepare at the first start.
- With the Original picture, textures that have several detail levels are now filtered by the graphics card on the PlayStation 2's own texel grid instead of by a slower hand-written filter. The picture is the same to the eye; at the edges of cut-out textures such as leaves and grilles a single pixel can differ from the PlayStation 2.
- The 3D scenery is drawn with less work for the graphics card, with the same picture, and every device now uses the same graphics for it. The wide-screen picture rules are back on the AYN Thor and AYN Odin 2 Portal and other Qualcomm handhelds.
- On the AYN Thor and the AYN Odin 2 Portal the game prepares its graphics at the first start again, in small steps.
- On a phone, the start after the graphics driver stopped responding now runs with extra checks.
- The game has fewer graphics to prepare at the first start.
- The settings `vu_plain_only` and `vu_extended` in `config.toml` are no longer used. You can delete them.

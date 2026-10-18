## New

- The log now lists, every 10 seconds, how many drawing steps the game takes for each picture, which steps take the graphics card longest, and what kind of graphics card the game found (named in the first lines). If the game is slow, send the log with your report.
- On Android phones and handhelds and on Apple Silicon Macs, the fog, some see-through effects and the glow blur are drawn in fewer steps, with the same picture.

## Fixed

- Blending between see-through things and what is behind them now uses the graphics card's own mixing on every computer, phone and Mac, as the game did up to v0.5.2. A few mixed colours can come out one shade lighter than on the PlayStation 2, which the eye cannot tell apart. The more exact mixing of v0.6.0 to v0.7.9 took many more drawing steps on some cards and is now a developer switch. On cards that used it, the game also has fewer graphics programs to prepare at the first start.
- With the Original picture, textures that have several detail levels are now filtered by the graphics card instead of by a hand-written filter. The picture is the same to the eye; at the edges of cut-out textures such as leaves and grilles a single pixel can differ from the PlayStation 2.
- On computers, the 3D scenery is drawn by a leaner method that gives the same picture; a driver that cannot handle it is noticed at start and the old way is taken. Phones and handhelds draw it as before.
- On the AYN Thor and the AYN Odin 2 Portal the game prepares its graphics at the first start again, in small steps.
- On Android, the start after the graphics driver stopped responding now runs with extra checks.
- The game has fewer graphics programs to prepare at the first start.
- The settings `vu_plain_only` and `vu_extended` in `config.toml` are no longer used. You can delete them.

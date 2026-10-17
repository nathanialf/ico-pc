## New

## Fixed

- Blending between see-through things and what is behind them now uses the graphics card's own mixing on every computer and phone, as the game did up to v0.5.2. A few mixed colours can come out one shade lighter than on the PlayStation 2, which the eye cannot tell apart. The more exact mixing of v0.6.0 to v0.7.9 cost a lot of drawing steps on some cards and is now a developer switch. On cards that used it, the game also has fewer shaders to prepare at the first start.
- With the Original picture, textures that have several detail levels are now filtered by the graphics card on the PlayStation 2's own texel grid instead of by a slower hand-written filter. The picture is the same to the eye; at the edges of cut-out textures such as leaves and grilles a single pixel can differ from the PlayStation 2.

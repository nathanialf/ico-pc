## New

- Full pixel now draws the picture at your screen's exact pixels when Resolution is set to your window's size, and it is on by default with the Enhanced preset. If you played an earlier version, turn it on under Options > Effects, or pick the Enhanced preset again.
- The Analogue stick fix is also on the Controls page, and Achievement pop-ups are also under Extras.
- The Anti-aliasing row goes Off, MSAA 2x, 4x, 8x, then 2x and 4x supersampling, and shows when a saved choice is not available on your graphics card.
- iOS (through LiveContainer) builds, contributed by katsifolis, and an Apple-silicon macOS app, contributed by braito4 (not notarized: the first time, right-click it and choose Open).
- The README now has a [supported platforms table](https://github.com/nathanialf/ico-pc#supported-platforms): what to download for each device and how well each one is tested.

## Fixed

- With Vertical sync on and a frame-rate limit equal to your screen's refresh rate (60, 120 or 240), the game now follows the screen, so the stutter that came back every few seconds is gone.
- The picture at 1920x1080 is sharp again, with no half-pixel softness.
- The MSAA choices no longer disappear from the Anti-aliasing row at some window sizes.
- Photo mode's button pictures show correctly with a texture pack.
- The stray shapes beside the picture for a few frames when you return to the main menu are gone.
- Walls and floors right next to the camera no longer flicker out for a moment while the camera moves.
- The wall pieces above the courtyard doorways no longer vanish as the camera turns.
- Changing the Frame rate setting no longer blacks out the screen or skips ahead.
- Windows: the program now carries version information, and the download no longer includes developer tools.

## Known issues

- Some ghosting while the camera moves (a faint trailing image) remains in every area. You can try other Frame rate settings, or the Direct3D 12 renderer on Windows (it ghosted less in our testing, but not zero), see [Choosing the renderer on Windows](https://github.com/nathanialf/ico-pc/blob/main/docs/OPTIONS.md#choosing-the-renderer).
- Seams between wall pieces can flicker briefly as the camera turns in a few places, the chandelier room and the main gate most; the courtyard with the sunbeams is mostly cured.

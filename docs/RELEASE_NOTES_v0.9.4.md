**Please note:** This version draws the picture with fewer steps on every renderer. Two picture changes are on by default. Above the Original resolution, the soft edges of shadows are drawn with the game's own simpler smoothing, which is very slightly sharper. At frame rates above Original, the in-between pictures reuse the glow from the picture before them, so the glow can trail very slightly during a fast camera turn. Both can be put back by adding `shadow_bspline = true` (shadow edges) or `glow_redraw = true` (glow) to the `[dev]` section of the config file. None of it has been timed on a real graphics card yet; if it is slower for you than v0.9.3, please send `logs/ico-pc.log`.

## New

- Above the Original resolution, the soft edges of shadows use the game's own smoothing and look very slightly sharper.
- At frame rates above Original, the in-between pictures reuse the glow of the picture before them. It can trail very slightly during a fast camera turn.
- When nothing on screen moves between two game steps, the in-between picture is shown again instead of being drawn twice.
- With Direct3D 11, the game no longer copies its whole drawing buffer to the graphics card for every picture, only the part it wrote.

## Fixed

- At wide aspect ratios on phones, tablets and Macs, the glow's left edge could keep a thin strip from an earlier blur step.

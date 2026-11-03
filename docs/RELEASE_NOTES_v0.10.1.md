## New

- Scenery outside the picture is no longer sent to the graphics chip: faster on phones and weak graphics chips, the same picture.
- The glow around bright light is drawn with one full-screen step less, so it takes less of the graphics chip's time, most of all on phones; the picture is the same.
- Phones with a Qualcomm graphics driver prepare their graphics again at the next start where an earlier start had to stop.

## Fixed

- Turning on MSAA anti-aliasing no longer makes the game restart on NVIDIA graphics cards.
- With Graphics API set to Direct3D 11, the shadows are worked out in a way every graphics card supports.
- On 16:10 screens the shadows are counted in one step again.
- On phones whose screen changes its refresh rate, the game now asks for a fixed one when a Frame rate is set.

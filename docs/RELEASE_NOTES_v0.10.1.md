## New

- Scenery that lies wholly outside the picture is no longer sent to the graphics chip. This saves work on phones and weak graphics chips; the picture is the same.
- The glow around bright light is drawn with one full-screen step less, so it takes less of the graphics chip's time, most of all on phones; the picture is the same.
- Options > Graphics driver shows each driver's name and version, refuses a driver package built for the wrong memory page size, and goes back to the phone's own driver when the graphics stop responding in the first 30 seconds with a driver you added.
- Phones with a Qualcomm graphics driver prepare their graphics again at the next start where an earlier start had to stop for lack of memory, when the game's saved graphics file is there.
- Scenery outside the picture is no longer sent to the graphics chip: faster on phones and weak graphics chips, the same picture.
- Phones with a Qualcomm graphics driver prepare their graphics again at the next start where an earlier start had to stop.
- Options > Graphics driver shows each driver's name and version, and refuses a driver package built for the wrong memory page size.

## Fixed

- Walls and water right beside the camera no longer vanish at the sides of wide screens; the 4:3 picture is unchanged.
- Turning on MSAA anti-aliasing now works out shadows in the same steps as without it, which is meant to stop the game restarting on NVIDIA graphics cards.
- With Graphics API set to Direct3D 11, the shadows are worked out in a way every graphics card supports.
- On 16:10 screens the shadows are counted in one step again.
- On phones whose screen changes its refresh rate, the game now asks for a fixed one when a Frame rate is set (Android 11 and later).
- Русский (bob-ttz): 59 more rows translated.
- Turning on MSAA anti-aliasing no longer makes the game restart on NVIDIA graphics cards.
- On phones whose screen changes its refresh rate, the game now asks for a fixed one when a Frame rate is set.
- With Language set to a translation by players, the game's own menu words (New Game, Continue, the saves' places) now follow the Menu language like the rest of the menus (fix by cobra838).
- The Russian "Continue" on the title screen fits its row (fix by cobra838).

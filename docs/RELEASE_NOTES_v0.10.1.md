## New

- Scenery that lies wholly outside the picture, or wholly behind what was already drawn in front of it, is no longer sent to the graphics chip. This saves work on phones and weak graphics chips; the picture is the same.
- The glow around bright light is drawn with one full-screen step less, so it takes less of the graphics chip's time, most of all on phones; the picture is the same.
- Options > Graphics driver shows each driver's name and version, refuses a driver package built for the wrong memory page size, and goes back to the phone's own driver when the graphics stop responding in the first 30 seconds with a driver you added.
- Phones with a Qualcomm graphics driver prepare their graphics again at the next start where an earlier start had to stop for lack of memory, when the game's saved graphics file is there and the driver accepts it; if that start has to stop too, later starts prepare the graphics as they are needed, as before.

## Fixed

- On computers, new text (subtitles, menus, pop-ups) no longer holds a picture back for a moment; the text is the same.
- With Direct3D 11 or OpenGL and Vsync on, waiting for the screen no longer delays the game's own step.
- Walls and water right beside the camera no longer vanish at the sides of wide screens; the 4:3 picture is unchanged.
- Turning on MSAA anti-aliasing now works out shadows in the same steps as without it, which is meant to stop the game restarting on NVIDIA graphics cards.
- With Graphics API set to Direct3D 11, the shadows are worked out in a way every graphics card supports.
- With Graphics API set to Direct3D 11, the movies no longer have a purple tint.
- On 16:10 screens the shadows are counted in one step again.
- On phones whose screen changes its refresh rate, the game now asks for a fixed one when a Frame rate is set, and again when you come back to the game (Android 11 and later).
- Русский (bob-ttz): 59 more rows translated.
- With Language set to a translation by players, the game's own menu words (New Game, Continue, the saves' places) now follow the Menu language like the rest of the menus (fix by cobra838).
- The Russian "Continue" on the title screen fits its row (fix by cobra838).
- With Shadow speed above Original, the shadows turn as quickly as they move, so they no longer circle a portal before entering it.
- Switching the Graphics API to OpenGL no longer leaves a black, unresponsive window if the graphics driver stalls; the game goes back to the previous API and says so.

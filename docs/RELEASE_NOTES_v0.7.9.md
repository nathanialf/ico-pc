## New

- The log now says where the graphics time goes with Direct3D 12 too, as it already did with Vulkan: the lines written every 10 seconds show the time the graphics card spent and how much work it was given.
- The log now reports how the sound output is doing: how it was opened, and, every 10 seconds while the sound plays at the sound device's own pace (the usual way), whether it ever ran short. It also notes each time the game cuts a sound off at once and how loud that sound was. If you hear clicks or stutters, send the log with your report.

## Fixed

- The sound keeps playing through short loading pauses, and it no longer clicks when the picture falls behind. When the game stops for longer, the sound pauses with it instead of running ahead. This is for Windows, Linux, Android and Mac. A short click when you leave a room can still be the game stopping its own sounds, as it does on the PlayStation 2.
- On phones and other devices that cannot draw every picture in time at the chosen settings, the game keeps its normal speed by showing fewer pictures, instead of slowing down and catching up in jumps (at every frame rate except Original).
- On a Mac the game is now fast with anti-aliasing off. It used a slower way of mixing colours, which made the game only feel smooth with anti-aliasing on. The only cost is that a few blended colours can be one shade lighter than on the PlayStation 2.
- Drawing is cheaper on slow graphics chips: the glow and the other full-screen effects do less work for the same picture, and a few see-through objects are drawn in one pass, as the PlayStation 2 orders them. The gain is on phones, handhelds and older cards; fast cards will not notice.
- The glow, the depth of field and the screen softening are now drawn at the PlayStation 2's size at every resolution, as on the console, which makes them much cheaper at 2x and above, where they now look as coarse as on the console. The Glow size setting is gone.
- Drawing the 3D scenery now does the work for each corner once instead of three times, for the same picture. If a graphics driver cannot handle the new way, the game notices at start and goes back to the old way by itself.

## New

(kept current after every merge; player words only)

- Options > Picture has a new row, Upscaling. FSR 1.0 (AMD's open upscaler) makes the picture sharper when it is drawn smaller than your screen, as at Resolution 1x or after Downsampling, on every computer and phone. A Sharpness row goes with it. Off by default, the picture as the original game showed it.
- Options > Picture > Upscaling has a second choice, FSR 3.1 (AMD's open temporal upscaler), on computers that run the game with Vulkan (Windows and Linux), with an Upscaling quality row: Native AA smooths the edges of the picture at your resolution; Quality, Balanced and Performance draw it smaller and rebuild it to size. Off as in the original game. Things the game draws flat on the screen, such as the fades, the film grain, sparks and smoke, can leave a faint trail. Where your computer cannot run it, the row says so.

- The game now has an icon: the I of the title lettering, made from your own disc the first time the title screen shows (about half a second of black). Windows and Linux show it on the window and the taskbar, Add to Steam uses it, and on a Mac it is the Dock icon while the game runs, and a new Options > Extras row puts the game in your applications menu on Linux.

## Fixed

(kept current after every merge; player words only)

- On phones and other tile-based graphics chips the game now works out each corner of the 3D scenery once a picture instead of up to nine times, as it already did on computers: cutscenes and busy scenes on slower phones should run faster. At start the game checks that the chip draws this way exactly as the slower way does, over several pictures in a row, and goes back to the slower way by itself if not.

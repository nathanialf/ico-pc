**Please note:** On phones and tablets with the Turnip graphics driver the game now uses its simpler set of shaders, which stops the streak some players saw; if you still see one, use **Save a picture for a bug report** in Options and send the zip, which now also holds the exact frame that was on screen.

## New

- Options > Effects > Glow between frames: above Frame rate Original, **Original** keeps the glow of the game's own frame for the frames shown between them; **Smooth** makes it again for every frame shown, so it follows a fast camera turn (it costs more of the graphics card's time). The Enhanced preset turns it on; Enhanced settings saved earlier get it at the next start. The row is greyed at Frame rate Original or with Glow off.
- The bug-report picture (F12 on a computer, Save a picture for a bug report in Options) also saves the exact in-between frame that was on screen, so problems that only show between frames can be studied.
- Options > Graphics has a Graphics card row on computers with two or more graphics cards, such as many laptops. Pick the card that draws the game; Apply switches at once, or the choice is used from the next start. Automatic always picks the fast card when there is one. The Windows graphics setting for each app does not change the card the game uses; this row does.

## Fixed

- On phones and handhelds using the Turnip graphics driver, a thin streak that could flash from Ico's leg across the screen between frames is no longer drawn.
- A rare case where a corner of a 3D model could be placed at the wrong spot on some graphics drivers is fixed.
- On some Android tablets and phones with Qualcomm's own graphics driver, the game stuttered every few seconds with vsync Off. After a few of these stutters the game now switches to a smoother way of showing pictures for the rest of the session; your vsync setting is not changed.
- In photo mode, the glowing trails behind the shadow creatures' eyes now stay with the eyes when you move or zoom the camera.
- The frame rate no longer dips to 30-40 while the game streams its data from the drive (most often with the game files on a slow, external or network drive): the game now reads its data in the background, and those waits no longer hold the picture rate down afterwards.
- The menus are much faster at Original resolution on phones and tablets with weaker graphics (the Options pages ran at 6-7 frames a second on a Mali-G52).
- The game now starts on phones whose graphics driver cannot draw depth with a 32-bit depth buffer alone (for example the Asus ZenFone Live L1 with an Adreno 505, on Android 8).

## New

- Android (experimental): play on a 64-bit phone or tablet with Android 10 or later, with on-screen touch controls or a gamepad; the first start asks for your disc image in the phone's file picker. Phones whose graphics lack a feature the game used to need get a slower drawing path, so more phones can run it (not yet tested on such a phone). Known for now: it runs slower than a PC, the opening movie can glitch, folding phones may not get the full screen, and the touch layout is a first draft.
- Options > Effects: Glow, Depth of field, Screen softening, Motion blur and Fog can each be turned off (issue 11).
- Photo mode: a free camera you fly with the sticks, L3 switches to the orbit camera, R3 changes the speed, and zooming is finer. Square hides the help panel, and the game remembers it.
- Model viewer: L2 and R2 zoom, and the left stick moves the model around the screen.
- Model packs: replacement 3D models go in a models folder, and Options > Display > Model pack turns them on or off. With Developer mode, pack makers can save the game's models to edit.
- ReShade and vkBasalt can be used: the game now gives them a depth buffer and waits for them to prepare their effects (checked by reading the code, not yet with ReShade installed).
- The download comes with textures, models and reshade folders, each with a note inside saying what goes there.
- The first start after this update is slower because the game rebuilds its drawing programs. Movies are shown without the effects depth.
- The guides are now short pages on separate topics, and they come with the download.

## Fixed

- The final fight: Ico's sword hits the Queen's shield again (issue 12).
- Model viewer: the Queen's face no longer darkens in waves. The game holds the shadow on her face still in every scene that shows her; the viewer let it keep moving.
- Surfaces that sit very close together are drawn in the right order (the game's depth was kept too coarsely before).
- Android: quitting the game saves your achievements, your saves and settings are part of the phone's backup, and stopping the first start with Back leaves nothing behind.

## New

- Options > Gameplay > Yorda trips: running hand in hand for three seconds makes Yorda fall, as she does after the main gate opens. Off unless you turn it on.
- On Windows and Android, save states now carry over to later versions when the part of the game they were saved in has not changed. When it has, loading the state tells you which version made it and why it cannot be resumed, instead of only saying it is from another version. A gameplay setting that a later version adds does not stop a state from carrying over while you leave that setting as it starts. Save states made by v0.9.7 or earlier cannot carry over.
- With Graphics API set to Direct3D 11, the game's 3D models are now prepared once per frame, as with the other Graphics API choices, so older graphics cards spend less time on each picture.
- The pictures are now drawn and shown by a thread of their own, so the game no longer waits for each picture to be shown before it goes on. On phones this takes some of the work that held the sound back during play off the game, and raises the frame rate where the graphics chip has time to spare. If a computer or phone shows problems with it, `render_thread = off` in the `[dev]` part of the settings file puts everything back on one thread.
- The game no longer repeats graphics commands that change nothing, so each picture takes less processor work with every Graphics API choice.
- On a keyboard, F6 opens the Save states page. During play it pauses the game first, and pressed again on the page it takes you back to the game.

- **Options > Extras > Back up your data** and **Restore from a backup**, on the title screen (Windows, Linux, the Steam Deck, Mac and Android): your memory card, save states, settings and achievements in one `.zip` file you choose where to save, and put back from such a file, to keep a copy, move to another computer or phone, or go back to an earlier point. On Android this works even where the game's folder is hidden (Android 13 and later).
- The ending's credits now also name the translators, the contributors and the special thanks.

## Fixed

- On Android, a save state made in an earlier session could not be loaded (the game closed). Save states made before you closed the app now load after you open it again.
- A save state's picture showed the pause menu and was small. It now shows the game as it was when you paused, larger and in its own shape, without the menu. A state saved right after loading another one gets the loaded state's picture.
- A wall could be seen through for an instant when the camera moved past it (with Frame rate above Original). The frames shown between the game's own frames no longer take the camera through a wall or a floor.
- On phones with a lot of memory, preparing the graphics at the first start stopped early, and the game then stuttered during play while it prepared the rest. It now finishes behind the progress bar before the game starts.
- From this version on, an update that does not change how the game draws keeps the graphics prepared before it, so its first start no longer prepares them all again. The first start of this version still prepares them once.

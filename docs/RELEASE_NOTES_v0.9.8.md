## New

- Options > Gameplay > Yorda trips: while you run pulling her by the hand, Yorda can trip and fall now and then, the way she does after the main gate opens. Off unless you turn it on.
- On Windows and Android, save states now carry over to later versions when the part of the game they were saved in has not changed. When it has, loading the state tells you which version made it and why it cannot be resumed, instead of only saying it is from another version. Save states made by v0.9.7 or earlier cannot carry over.
- With Graphics API set to Direct3D 11, the game's 3D models are now prepared once per frame, as with the other Graphics API choices, so older graphics cards spend less time on each picture.
- On a keyboard, F6 opens the Save states page. During play it pauses the game first, and pressed again on the page it takes you back to the game.

## Fixed

- On Android, a save state made in an earlier session could not be loaded (the game closed). Save states made before you closed the app now load after you open it again.
- A save state's picture showed the pause menu and was small. It now shows the game as it was when you paused, larger and in its own shape, without the menu.
- A wall could be seen through for an instant when the camera moved past it (with Frame rate above Original). The frames shown between the game's own frames no longer take the camera through a wall or a floor.

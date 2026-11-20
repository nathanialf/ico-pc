## New

- The pause menu no longer shows the Square prompt for Save states, and Square there no longer opens them. Choose Save states in the pause menu (or press F6 on a keyboard) to open them; the Continue screen keeps its Square prompt.
- The pause menu's panel on the right is shorter: play time, deaths, how often Yorda was taken, saves, enemies defeated, the difficulty and the area. New Game+, Mirror mode, the achievements count and the helpers are no longer listed there; Options > Gameplay > Difficulty and the Achievements page show them.
- Mirror mode and New Game+ are now under Customize on the New Game screen, for every difficulty. The New Game screen keeps Difficulty, Customize and Start.
- Bug reports now record what each of the last areas you played contained and what was left out of its pictures and why, and a saved picture for a bug report records the same up to that moment, so a wrong picture can be looked into from the report alone.
- Button pictures for Xbox and Nintendo (Switch, Wii U) controllers in the menus, the game's prompts, photo mode and the touch screen buttons, chosen automatically from the controller you use; Options > Controls > Button pictures picks them by hand, and also has Wii Classic Controller pictures. The pictures are by MesaBlack.

## Fixed

- Mirror mode: left and right on the stick now move the way you push in the menus, on the save and memory card screens, on the Continue screen and on the screens after the ending (the D-pad already did).
- An attempted fix for the frame rate dropping during the opening on Android phones like the moto g24 with Frame rate Original. The check added in v0.11.12, which decides in the first moments of play whether the phone's graphics driver holds each picture until it is drawn, never finished on that phone, so the improvement it enables never turned on; the check now times how long the phone takes to show each picture and whether the picture before had finished drawing by then, without slowing the pictures it checks, and decides.
- An attempted fix for slow scenes with a lot of detail on screen on some phones like the moto g24, such as the boat in the opening: the game now asks the graphics chip for less work on every device.

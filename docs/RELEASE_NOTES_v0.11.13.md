## New

- The pause menu no longer shows the Square prompt for Save states, and Square there no longer opens them. Choose Save states in the pause menu (or press F6 on a keyboard) to open them; the Continue screen keeps its Square prompt.
- Mirror mode and New Game+ are now under Customize on the New Game screen, for every difficulty. The New Game screen keeps Difficulty, Customize and Start.

## Fixed

- An attempted fix for the frame rate dropping during the opening on Android phones like the moto g24 with Frame rate Original. The check added in v0.11.12, which decides in the first moments of play whether the phone's graphics driver holds each picture until it is drawn, never finished on that phone, so the improvement it enables never turned on; the check now times how long the phone takes to show each picture and whether the picture before had finished drawing by then, without slowing the pictures it checks, and decides.

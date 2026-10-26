## New

## Fixed

- At an uncapped or high frame rate, one slow moment no longer holds the game at 60 pictures a second for the rest of the session. When the game does hold back the extra pictures because the graphics card cannot keep up, it now goes back to the full rate within about two seconds once the card has the time again.
- With OpenGL, waiting for the screen no longer counts as a slow graphics card. Before, at Frame rate 60 with Vsync on, scenes the card draws easily could still run at 30 pictures a second.

**Please note:** Direct3D 11 may still show a black screen on some graphics cards. It has now run through the game's own drawing tests on Microsoft's software renderer, and one known Direct3D 11 limitation is worked around, but it has still not been seen drawing the game on a real graphics card. If it stays black for you, choose Vulkan or Direct3D 12 in Options > Graphics API and send `logs/ico-pc.log` from a start with Direct3D 11.

## New

- When Direct3D 11 draws nothing, the game's log now says exactly how its start-up drawing checks drew, so one log shows what goes wrong.

## Fixed

- At an uncapped or high frame rate, one slow moment no longer holds the game at 60 pictures a second for the rest of the session. When the game does hold back the extra pictures because the graphics card cannot keep up, it now goes back to the full rate within about two seconds once the card has the time again.
- With OpenGL, waiting for the screen no longer counts as a slow graphics card. Before, at Frame rate 60 with Vsync on, scenes the card draws easily could still run at 30 pictures a second.

**Please note:** This version draws the picture with fewer steps on every renderer. The picture itself is unchanged. None of it has been timed on a real graphics card yet; if it is slower for you than v0.9.3, please send `logs/ico-pc.log`.

## New

## Fixed

- With Direct3D 11, the game no longer copies its whole drawing buffer to the graphics card for every picture, only the part it wrote.

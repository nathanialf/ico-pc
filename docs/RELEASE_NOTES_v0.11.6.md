## New

(kept current after every merge; player words only)

- The 3D scenery asks the graphics chip for less work on every device, with the picture unchanged.
- Shadows take less graphics work on every device, phones most: the three layers that soften a shadow share one check of the picture instead of three, and when no shadow is on screen those layers are skipped (the picture is exactly the same).

## Fixed

(kept current after every merge; player words only)

- The macOS and iOS packages build again (v0.11.5 had none).
- Phones with only one or two fast processor cores give the game and its picture a fast core each, and run the sound and background work on the other cores, instead of all of them sharing the fast ones.
- The automatic capture of a slow scene for bug reports interrupts the game for less time.

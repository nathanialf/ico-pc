## New

(kept current after every merge; player words only)

- The 3D scenery asks the graphics chip for less work on every device, with the picture unchanged.
- Shadows take less graphics work on every device, phones most: the three layers that soften a shadow share one check of the picture instead of three, and when no shadow is on screen those layers are skipped (the picture is exactly the same).
- On Windows PCs with older Intel graphics (6th to 10th generation, on Intel's final drivers), shadows can be drawn without stopping the picture partway to copy it: the first start after an install or a driver update checks once that the picture is exactly the same and not slower, and keeps it only then.
- The log says more about slow phones: what each picture costs, what a stutter happened alongside, and which of this release's changes ran on the phone.
- Phones short of memory keep less of it reserved for drawing 3D scenery: the game sets aside what its busiest scenes need instead of the most it could take, and the log now says how the game's memory is held.
- On phones whose graphics chip prepares the 3D scenery in a separate step first (the Mali-G52 among them), the parts of the scenery that are off screen or that the game itself hides are skipped before they are drawn, once the game has checked at start that the picture stays exactly the same.
- With Vulkan, the glow around lamps and the sun is added to the picture by a smaller graphics program made for that one step, once the game has checked at start that the picture comes out exactly the same.

## Fixed

(kept current after every merge; player words only)

- The macOS and iOS packages build again (v0.11.5 had none).
- Phones with only one or two fast processor cores give the game and its picture a fast core each, and run the sound and background work on the other cores, instead of all of them sharing the fast ones.
- The automatic capture of a slow scene for bug reports interrupts the game for less time.
- Phones and tablets with a Qualcomm Adreno chip on Qualcomm's own driver now set up each drawing step the driver's longest-standing way, an attempt at the short freezes seen on the Adreno 740 and 710 (the picture is exactly the same).

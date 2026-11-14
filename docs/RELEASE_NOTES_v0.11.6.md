## New

- On every device but the Adreno 740, the 3D scenery asks the graphics chip for less work, with the picture unchanged.
- Shadows take less graphics work, phones most: the three layers that soften a shadow share one check of the picture instead of three (not with MSAA; the picture is exactly the same).
- When no shadow is on screen, the layers that soften shadows are skipped (the picture is exactly the same).
- On Windows PCs with older Intel graphics (6th to 10th generation, on Intel's final drivers), shadows can be drawn without stopping the picture partway to copy it: the first start after an install or a driver update checks once that the picture is exactly the same and not slower, and keeps it only then.
- The log says more about slow phones: what each picture costs, what a stutter happened alongside, how the game's memory is held, and which of this release's changes ran on the phone.
- With the Original frame rate, the game sets aside less memory for drawing 3D scenery: what its busiest scenes need instead of the most it could take.
- On phones with the Mali-G52 or the PowerVR DXT graphics chip, the parts of the scenery that are off screen or that the game itself hides are skipped before they are drawn, once the game has checked at start that the picture stays exactly the same.
- With Vulkan, the glow around lamps and the sun is added to the picture by a smaller graphics program made for that one step, once the game has checked at start that the picture comes out exactly the same.
- With Vulkan (not with MSAA), the shapes that mark where shadows fall are drawn by a graphics program that does no colour work at all, so the graphics chip can mark them before it paints anything, once the game has checked at start that the shadows come out exactly the same.
- With Vulkan on most graphics chips (not the Mali-G52, and not with MSAA), shadows are laid over the scenery by a smaller graphics program made for that one step, once the game has checked at start that the picture comes out exactly the same.
- With Frame rate set to Uncapped and Vertical sync off on Android phones, motion is steadier: each in-between picture is drawn for the moment the screen will actually show it, where the phone reports when its pictures reach the screen.
- On phones with the PowerVR DXT graphics chip (the Google Pixel 10 Pro Fold among them), the 3D scenery is prepared by a different kind of graphics step, the one that lets the game skip scenery before drawing it (the picture is exactly the same).
- On the same phones, pictures with no shadow in them, such as the title and the pause menu, skip the whole shadow step (the picture is exactly the same).
- On the Google Pixel 10 Pro Fold the opening and the lamp-lit scenes run faster with these changes together (measured: 14 to 17 thousandths of a second a picture in the opening, where v0.11.5 took 20 to 23).
- Nothing in this release was measured on Mali or Adreno phones, or on Windows, Linux or macOS; the measurements are from a Google Pixel 10 Pro Fold.

## Fixed

- The macOS and iOS packages build again (v0.11.5 had none).
- Phones with only one or two fast processor cores keep those cores for the game and its picture, and run the sound and background work on the other cores, instead of all of them sharing the fast ones.
- The automatic capture of a slow scene for bug reports interrupts the game for less time.
- A safety setting that older versions of the game could leave switched on for good after a graphics crash, making the graphics chip check every read of the 3D scenery, is now switched off by the game itself at the first start. This also takes out the same line if you added it by hand.
- The game still switches that safety setting back on by itself if the graphics driver crashes again.
- Phones and tablets with a Qualcomm Adreno chip on Qualcomm's own driver now set up each drawing step the driver's longest-standing way, an attempt at the short freezes seen on the Adreno 740 and 710 (the picture is exactly the same).

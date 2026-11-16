## New


## Fixed

- On tablets, phones and handhelds with Adreno graphics on Qualcomm's own driver, v0.11.7's request to run the game's graphics work ahead of other apps is withdrawn: it did not reduce the freezes and may have slowed the rest of the device.
- Characters' shadows above the original resolution, which made v0.11.7 slower than v0.11.6 on tablets at higher resolutions, now skip a step of graphics work that was only needed to read the shadow back: the game times three ways of drawing them that give exactly the same picture at the first shadow it draws, and keeps the quickest on your device.

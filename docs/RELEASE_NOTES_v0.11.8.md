## New


## Fixed

- On tablets, phones and handhelds with Adreno graphics on Qualcomm's own driver, v0.11.7's request to run the game's graphics work ahead of other apps is withdrawn: it did not reduce the freezes and may have slowed the rest of the device.
- Characters' shadows above the original resolution, which made v0.11.7 slower than v0.11.6 on tablets at higher resolutions, now skip a step of graphics work that was only needed to read the shadow back: at the first shadow it draws, the game checks on your device that the quicker ways of drawing them give exactly the same picture, then times them and keeps the quickest (if the check finds any difference, shadows are drawn as in v0.11.7).

## New

- When the game closes by itself without a message, the next start now records where it was and how much memory the device had left, in the log and in the new `last-run.txt` file in the logs folder (report zips include it), so the cause can be found from one report.
- On Android 11 and later, the log and the report file now say how Android ended the game the time before (for example, low on memory, stopped responding, or closed from the recent apps list), with the system's own crash details when it kept them.
- The log now also says what is using the device's memory while the game prepares its graphics at start, when the system warns that memory is low, and every ten seconds on Android, so a report shows why a device ran out of memory.

## Fixed

- On tablets, phones and handhelds with Adreno graphics on Qualcomm's own driver, v0.11.7's request to run the game's graphics work ahead of other apps is withdrawn: it did not reduce the freezes and may have slowed the rest of the device.
- Characters' shadows above the original resolution, which made v0.11.7 slower than v0.11.6 on tablets at higher resolutions, now skip a step of graphics work that was only needed to read the shadow back: at the first shadow it draws, the game checks on your device that the quicker ways of drawing them give exactly the same picture, then times them and keeps the quickest (if the check finds any difference, shadows are drawn as in v0.11.7).

## New

- When the game closes by itself without a message, the next start now records where it was and how much memory the device had left, in the log and in the new `last-run.txt` file in the logs folder (report zips include it), so the cause can be found from one report.

## Fixed

- On phones whose graphics draw some see-through surfaces in two steps (such as Mali graphics), surfaces that can be drawn in one step with the same picture now are, which takes some work off each picture; the game checks at start that the picture comes out the same and keeps the old way if it does not.
- On tablets, phones and handhelds with Adreno graphics on Qualcomm's own driver, v0.11.7's request to run the game's graphics work ahead of other apps is withdrawn: it did not reduce the freezes and may have slowed the rest of the device.

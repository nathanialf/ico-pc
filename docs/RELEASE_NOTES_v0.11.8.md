## New

- When the game closes by itself without a message, the next start now records where it was and how much memory the device had left, in the log and in the new `last-run.txt` file in the logs folder (report zips include it), so the cause can be found from one report.
- The log now also says what is using the device's memory while the game prepares its graphics at start, when the system warns that memory is low, and every ten seconds on Android, so a report shows why a device ran out of memory.

## Fixed

- On tablets, phones and handhelds with Adreno graphics on Qualcomm's own driver, v0.11.7's request to run the game's graphics work ahead of other apps is withdrawn: it did not reduce the freezes and may have slowed the rest of the device.

## New

- When the game closes by itself without a message, the next start now records where it was and how much memory the device had left, in the log and in the new `last-run.txt` file in the logs folder (report zips include it), so the cause can be found from one report.
- On Android 11 and later, the log and the report file now say how Android ended the game the time before (for example, low on memory, stopped responding, or closed from the recent apps list), with the system's own crash details when it kept them.

## Fixed

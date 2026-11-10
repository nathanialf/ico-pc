## New

(kept current after every merge; player words only)

- A bug-report picture taken with Upscaling on now also saves the picture before the upscaler and the upscaler's own output, so a report can show where a flaw is born.
- Translations: the rows added since the community translations came in are listed at the end of each translation's file, in English until translated.

## Fixed

(kept current after every merge; player words only)

- With Upscaling set to FSR 3.1, the game's own text, save tiles, subtitles and fades are now drawn over the finished picture instead of through the upscaler, so they stay sharp; and an area change fades to black cleanly instead of smearing the picture.
- With Upscaling set to FSR 3.1, the upscaler now works in full precision on every graphics card; on some cards its reduced-precision maths could leave thin vertical lines over distant scenery.
- On phones and other devices with tile-based graphics chips (such as Macs), the game no longer spends time testing which scenery is hidden behind other scenery, a check that found almost nothing there.
- On phones whose graphics driver does not let the drawing stage write the 3D scenery's pre-computation directly (such as the Mali-G52), the game now writes it with a compute step instead of through the screen's drawing hardware; busy scenes there should run faster.
- With "Shadows wait while Ico is down" set to Off, the Shadows' swings now land while Ico is on the ground as well; before, they passed through him until he stood up.
- On Android a start that had run low on memory while preparing the graphics programs no longer leaves the next start preparing them in the middle of play, where each batch could freeze the picture for a second.
- On iPhone and iPad the game starts again; v0.11.1 stopped at once with "Cannot allocate the game stack".
- On phones the game no longer spends time testing which scenery is hidden behind other scenery, a check that found almost nothing there; and a safety setting that a crash recovery turned on is switched off again after a few clean starts.
- On phones whose graphics driver does not let the drawing stage write the 3D scenery's pre-computation directly (the Mali-G52 of issue 179), the game now writes it with a compute step instead of through the screen's drawing hardware; busy scenes there should run faster.
- On Android the recent-apps card's icon no longer sits on a cream square that blended into the card; the letter alone is shown.

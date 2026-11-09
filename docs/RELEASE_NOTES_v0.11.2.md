## New

(kept current after every merge; player words only)

- Translations: the rows added since the community translations came in are listed at the end of each translation's file, in English until translated.

## Fixed

(kept current after every merge; player words only)

- With Upscaling set to FSR 3.1, the game's own text, save tiles, subtitles and fades are now drawn over the finished picture instead of through the upscaler, so they stay sharp; and an area change fades to black cleanly instead of smearing the picture.
- On phones the game no longer spends time testing which scenery is hidden behind other scenery, a check that found almost nothing there; and a safety setting that a crash recovery turned on is switched off again after a few clean starts.
- On phones whose graphics driver does not let the drawing stage write the 3D scenery's pre-computation directly (the Mali-G52 of issue 179), the game now writes it with a compute step instead of through the screen's drawing hardware; busy scenes there should run faster.

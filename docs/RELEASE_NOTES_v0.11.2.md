## New

(kept current after every merge; player words only)


## Fixed

(kept current after every merge; player words only)

- With Upscaling set to FSR 3.1, the game's own text, save tiles, subtitles and fades are now drawn over the finished picture instead of through the upscaler, so they stay sharp; and an area change fades to black cleanly instead of smearing the picture.
- On phones whose graphics driver does not let the drawing stage write the 3D scenery's pre-computation directly (the Mali-G52 of issue 179), the game now writes it with a compute step instead of through the screen's drawing hardware; busy scenes there should run faster.

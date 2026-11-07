## New

(kept current after every merge; player words only)

- Options > Picture has a new row, Upscaling. FSR 1.0 (AMD's open upscaler) makes the picture sharper when it is drawn smaller than your screen, as at Resolution 1x or after Downsampling, on every computer and phone. A Sharpness row goes with it. Off by default, the picture as the original game showed it.

## Fixed

(kept current after every merge; player words only)

- On phones and other tile-based graphics chips the game now works out each corner of the 3D scenery once a picture instead of up to nine times, as it already did on computers: cutscenes and busy scenes on slower phones should run faster. At start the game checks that the chip draws this way exactly as the slower way does, over several pictures in a row, and goes back to the slower way by itself if not.
- On Snapdragon phones and tablets, the check that watches the characters for missing parts now steps back once it has seen nothing wrong for a while, so it costs less each picture, and the driver setting it may have saved is tried again at a later start instead of kept forever. The log now shows what the check costs each picture.

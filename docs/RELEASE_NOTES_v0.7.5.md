## Fixed

- When the graphics card stops responding, the game now restarts itself with simpler blending instead of closing.
- If the graphics driver crashes while the game prepares its graphics, the game now also tries simpler blending before switching to Direct3D 12.
- The menu text has its previous look back.
- If the game closes while preparing its graphics, the next start records where it stopped and prepares the graphics as you play instead.
- Sound no longer breaks up when the picture falls behind on a slow graphics card (thanks to braito4).

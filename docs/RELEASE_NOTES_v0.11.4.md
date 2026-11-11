## New

(kept current after every merge; player words only)

## Fixed

(kept current after every merge; player words only)

- On some phones (Mali and Adreno among them) the game closed at the start of the first 3D scene on v0.11.3. Phones now go back to v0.11.2's way of ordering and waiting for the graphics work in that scene. If the game still closes there, the next start on a phone draws the 3D models a simpler way, and on a PC with Vulkan the next start uses the older ordering, without changing your settings.

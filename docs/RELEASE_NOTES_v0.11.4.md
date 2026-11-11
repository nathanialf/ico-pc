## New

(kept current after every merge; player words only)

## Fixed

(kept current after every merge; player words only)

- An attempted fix for the game closing at the start of the first 3D scene on some phones. On a Mali phone this began with v0.11.3, so phones now go back to v0.11.2's way of ordering and waiting for the graphics work in that scene. An Adreno phone reported the same on v0.11.3; it gets the same change, but whether it helps there is not known yet. If the game still closes there, the next start on a phone draws the 3D models a simpler way, and on a PC with Vulkan the next start uses the older ordering, without changing your settings.

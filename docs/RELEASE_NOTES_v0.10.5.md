## New

(kept current after every merge; player words only)

## Fixed

(kept current after every merge; player words only)

- The next picture no longer waits for the screen before it starts drawing, on phones and with vsync on: cutscenes on slower phones run smoother.
- On Snapdragon (Adreno) graphics chips, Turnip included, the game now checks every drawing of the characters in every picture for the fault that made Yorda flicker, and saves a picture and a small file beside the log by itself the first time it sees it. No setting to change.
- With the Turnip driver on Snapdragon phones, when that check catches the fault the game switches by itself, from the next start, to another way of reading the characters' bone data (and, if that is not enough, tries one Turnip setting next), and says so in the log. No setting to change.

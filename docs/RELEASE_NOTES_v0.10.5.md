## New

(kept current after every merge; player words only)

- Options > Gameplay has a new Combat page with three rows, all On as in the original game. Automatic guard: turn it Off and Ico no longer blocks a Shadow's attack from the front. Turn to face shadows: turn it Off and Ico swings straight ahead instead of turning toward the nearest Shadow (a Shadow beside him can still be caught by the swing). Shadows wait while Ico is down: turn it Off and the Shadows no longer hold back while Ico is knocked down, so they can hit him again before he is up. None of them switches achievements off. The Combat page also holds four rows that moved from the Shadows page: One hit per swing, Shadows never dodge, Shadows attack together and Queen barrier (nothing about them changed).
- When a cutscene runs slowly, the game now saves one picture from it and a copy of the log in a file called `slow-` and the date, in the logs folder, once each time you start it. If you report a slow cutscene, attach that file with the log.

## Fixed

(kept current after every merge; player words only)

- The next picture no longer waits for the screen before it starts drawing, on phones and with vsync on: cutscenes on slower phones run smoother.
- On Snapdragon (Adreno) graphics chips, Turnip included, the game now checks every drawing of the characters in every picture for the fault that made Yorda flicker, and saves a picture and a small file beside the log by itself the first time it sees it. No setting to change.
- With the Turnip driver on Snapdragon phones, when that check catches the fault the game switches by itself, from the next start, to another way of reading the characters' bone data (and, if that is not enough, tries one Turnip setting next), and says so in the log. No setting to change.

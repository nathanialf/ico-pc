## New

(kept current after every merge; player words only)

- Options > Gameplay has a new Combat page with three rows, all On as in the original game. Automatic guard: turn it Off and Ico no longer blocks a Shadow's attack from the front. Turn to face shadows: turn it Off and Ico swings straight ahead instead of turning toward the nearest Shadow (a Shadow beside him can still be caught by the swing). Shadows wait while Ico is down: turn it Off and the Shadows no longer hold back while Ico is knocked down, so they can hit him again before he is up. None of them switches achievements off. The Combat page also holds four rows that moved from the Shadows page: One hit per swing, Shadows never dodge, Shadows attack together and Queen barrier (nothing about them changed).
- When a cutscene runs slowly, the game now saves one picture from it and a copy of the log in a file called `slow-` and the date, in the logs folder, once each time you start it. If you report a slow cutscene, attach that file with the log.
- Options > Controls > Remap controls has a new last action, Ask Yorda for a hint (on the discs that have hints: not the North American disc or the two 2001 previews). It starts with no button or key. Press the one you give it and Yorda gives the hint that is waiting in the room at once, instead of after her usual wait.
- Options > Developer has a new row, Free movement (with Developer mode on). Turn it On and pressing L2 hands Ico to the sticks, the way the people who made the game moved him about to test it: the left stick moves him, the right stick up and down, L3 sets him on the floor, and letting go of L2 gives him back. It switches achievements off while it is On.

## Fixed

(kept current after every merge; player words only)

- The next picture no longer waits for the screen before it starts drawing, on phones and with vsync on: cutscenes on slower phones run smoother.
- On Snapdragon (Adreno) graphics chips, Turnip included, the game now checks every drawing of the characters in every picture for the fault that made Yorda flicker, and saves a picture and a small file beside the log by itself the first time it sees it. No setting to change.
- With the Turnip driver on Snapdragon phones, when that check catches the fault the game switches by itself, right away and with the game going on, to another way of reading the characters' bone data (and, if that is not enough, tries one Turnip setting at the next start), and says so in the log. No setting to change.

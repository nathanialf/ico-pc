## New

(kept current after every merge; player words only)

- Difficulty modes: choose Easy, Normal or Hard when you start a new game, or change every rule yourself under Customize. Normal is the game as it shipped. The rows that made the game easier or harder (the Shadows' speed, toughness and aggression, the Combat rules, the time to rescue Yorda, Yorda trips, No hints and the Queen's barrier) have moved from Options to the New Game screen, and three new rules join them: a limit on how often Ico can be knocked down, No Shadows and Permadeath. A journey cannot change its difficulty once it has begun. From the pause menu, Options > Gameplay > Difficulty shows what the journey was started with, and the pause screen's journey lines show the mode and the knock-down count. Difficulty rows no longer switch achievements off.
- Each save keeps the difficulty its journey was started on, on either memory card. A save from an earlier version keeps the gameplay options you had chosen, from the first time you load it. If you copy your memory card to another device, copy difficulty.toml from your user folder with it.
- A knock-down limit for a journey: the game is over once Ico has been knocked down a chosen number of times, in one room or over the whole journey (three in each room on Hard). Only a hit that knocks Ico down counts, the explosions of boxes and bombs included; a stumble at a ledge never counts, and neither does the Queen's attack in the final fight, which never knocks him down. A fall counts once, until he is back on his feet; a knock-down as he gets up counts as a new one. A message shows each count short of the limit. At the limit he lies for two seconds first and cannot be hit meanwhile.
- Permadeath, a rule of its own: any game over ends the journey, from the moment the game over cannot be avoided. The game over screen offers no Continue, and the save can no longer be loaded. The memory card file itself is not touched, so this relies on your own honesty. Save states can be neither saved nor loaded on a Permadeath journey.
- Save states remember the journey's difficulty: loading one brings back the difficulty and the knock-down count it was saved with. A save state made with an earlier version that loads takes the gameplay options it was made with as its difficulty.
- Some save states made with v0.11.2 cannot be loaded in this version: those saved while Ico, Yorda or a Shadow lay knocked down, at the moment a Shadow appeared, or in the final hall. Memory card saves are not affected.
- No Shadows, a choice on a new journey's Customize page: no Shadow appears anywhere, and the story goes on as if you had won each fight, the cage room and the final hall included. Some story moments are lost or play differently without them. On the June 2001 demo the spiders still appear.

## Fixed

(kept current after every merge; player words only)

- The game no longer hitches when it saves a diagnostic capture; on phones the pictures it saves automatically for a report are written in the background, and the Settings bug-report picture now brings the newest of them along, so one attachment is enough.
- A crash on older Intel graphics (6th to 10th generation, on Intel's last drivers for them) when raising the resolution while playing.

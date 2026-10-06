# Extras

Settings > Extras is the page for what is not part of playing the game: a
music gallery, a model viewer and the credits. It sits in the Settings menu
because the title menu has no room for more rows (docs/port/SETTINGS.md,
"Extras"), and it is there only when Settings was opened from the title: the
galleries take over the stage and the pause menu has no stage to give.

| entry | what it will be | status |
| --- | --- | --- |
| Music | the music gallery: the soundtrack (the streamed music, by file), the scene sounds, the ambiences, the voices and the sound effects by bank, played through the game's own engines | live; docs/port/MUSIC.md |
| Models | a viewer for the game's character and object models (below) | live |
| Credits | the ending from the scene where the staff roll starts to the end of the roll, then the title; locked until the ending has been reached ("Finish the game to unlock") | live; "Credits" below |

The page is built in `port/ui/settings.c` (`UI_PAGE_EXTRAS`, the `extras*`
hooks) on the shared list code in `port/ui/ui_list.c` for the galleries'
lists; docs/port/UI.md, "Settings menu", describes both.

## Models

Extras > Models lists 23 of the game's models: the characters (Ico, Yorda,
the Queen, the ten shapes of shadow, the bird, a guard of the opening) and
objects worth a look (a cage, a bomb, a pot, a barrel, a floor lever, the
two swords, a stone couch). Picking one shows it alone on a grey background, lit as its
stage lights it, with its animations to play.

**Controls.**

| | |
| --- | --- |
| model list | Up and Down choose, Cross views, Triangle goes back (to Extras from the title, to the title from a model) |
| left stick | turn round the model (left and right) and over it (up and down) |
| right stick, up and down | zoom in and out |
| L1, R1 (or Up, Down) | the previous or next animation in the list |
| Cross | play the selected animation from its first frame |
| Square | loop on or off: at its last frame the animation starts again |
| Triangle | back to the model list |

The animation list at the right holds the motion names as the game's own
table spells them (`BOY STAND`, `EN1 FLY`, `D1_C9A_SA`). At the top left
are the model's name, the animation playing (with "Loop" while it loops)
and its frame, "Frame F / N"; at the bottom the prompts, "Left stick: turn",
"Right stick: zoom" and a line of the game's own button glyphs with their
words (Cross Play, Square Loop, L1 R1 Animation, Triangle Models; the model
list's Cross View, Triangle Back or Title screen). All of it is rows of the
viewer's layouts in the port font, styled as the Settings rows
(docs/port/UI.md, "Button glyphs"). An object with no animations shows
"No animations", only Triangle among the buttons, and can still be turned
and zoomed.

**How it works** (`port/game/model_viewer.c`, `model_viewer.h`). Each model
is an object one stage of the game builds when it loads: its host stage.
Picking it writes what Settings changed, fades the title music as a new
game does and loads that stage, with the boy and the girl placed at its
first entrance as a developer start stage places them. While the viewer is
up (`ico_mv_active`, port/game/options.h):

- the stage starts no script: its layout rows' script procs are not added
  (`common/src/sceneManager.c`, `initSceneGObj`), so no scene plays and
  nothing moves the camera or changes the stage; the stage's geometry,
  objects, lights and ambient sounds are loaded as usual;
- every other object is parked (not active: none of its functions or its
  display list runs; the camera's own objects, kind -1, run on), and the
  viewed object's run function is parked as the development build's
  Motion Viewer parks it (`objMenuProc`); pause is off and the fog off;
- the renderer keeps only the object's own draws (RENDER_API.md, "The
  draw filter") over a grey backdrop drawn first in the frame;
- the camera orbits the model, from in front of a skeleton: a skeleton's
  root (the middle of the body; it follows the root through the
  animations), an object's box centre where its first node puts it; the
  model's box, times its node scale, sets the distance, and the model sits
  left of the middle, clear of the animation list;
- the animations offered are the motions of the object's block that the
  stage holds: the Motion Viewer's own "NO MOTION IN THIS STAGE" test
  leaves out a motion whose memory area is not loaded. A motion plays as
  the Motion Viewer plays it (`InitMotionOrient` with the orientation
  update off) with the root turning but not travelling (its "RotOnly"
  mode), so a walk stays in view;
- a shadow still in its generator when the stage loads (all but the plain
  one, which stage 46 has out) is brought out while it is viewed: its
  layout row's display bit is set and its dead bit cleared, and the row is
  put back as it was when the viewer leaves the stage;
- achievements are suspended, as in developer mode
  (docs/port/ACHIEVEMENTS.md, "Suspension").

What is not offered: the stage animation objects (the idols, the
windmill, the gates, the lifts; `seki/src/StageAnimation.c`, drawn by
`stage_DispAnimation`, not by an object's display list) drew nothing in the
viewer: the frames dumped with the idol of stage 4 and the windmill of
stage 8 hold no mesh at all, so they are left out; the stick drew nothing
either (the one in the survey's stages is the boy's, drawn with him). No
trolley object turned up in the survey.

Triangle on the model list goes back to the title the way the pause
menu's End Game does (`la_end_confirm`'s Yes: the game flags reset, the
fight sounds closed, stage 1 loaded), where the title's own script starts
its music again. Picking another model loads its host stage afresh, even
when it is the same stage. Nothing is saved; the viewer's own buffers are
host memory, not the game's arena. If the stage changes under the viewer
(nothing in it should change it), the viewer starts again when its host
stage is up, or goes back to the title. The log has
`model_viewer: stage S loaded id N "name"` when a model is up,
`model_viewer: motion "name" frame F/N` while an animation plays (every
second), `model_viewer: the title is back`, and `model_viewer: failed:
<why>` when a model cannot be shown.

The picture keeps the host stage's own look: its lights on the model and
the stage's colour grading over the whole frame, so the grey has the
stage's tint.

**The table** (`port/game/model_viewer_table.c`). Each row: the name (a
port string, `UI_STR_MV_*`, in the five languages), the model (the
`modelData` / `charFiles` row), the object kind, the layout row (or -1 for
the first object of that kind and model, the stage animation objects),
the host stage, the motion block and its motion-orient rows. It was read
from a survey of every stage with data (1 to 63, 88, 91, 103 to 105, the
list `start_stage` accepts): each stage was booted with a temporary probe
(not kept) that, once the stage was up, listed every object with its kind,
kind name, model, model name, layout row, whether it was active, its
motion-orient rows and how many of the motions those rows reach the stage
holds, the shadows' layout-row flags, and the motion-kind table's names.
A host stage builds the model active at load, holds the most of its block,
and is not 88 or 91 (`STGBOSS_TEST` and `STG8TEST`, test stages). The motion blocks are the Motion
Viewer's `objMenu` (the boy 0..532, the girl 532..834, the shadows
834..983, the queen 1072..1134, the bird 1134..1143) and, between the
shadows' and the queen's, the opening's three guards and their horses
(983..1072, the `D1_*` motions); `model_viewer` checks every row against
them. Only numbers are in the file: names come from the port's strings and,
at run time, from the game's own tables.

| model | kind | model id | host stage | animations |
| --- | --- | --- | --- | --- |
| Ico | 1 (boy) | 0 | 8 | 314 |
| Yorda | 2 (girl) | 4 | 4 | 196 |
| The Queen | 48 (queen in a scene) | 71 | 11 | 8 |
| Shadow | 4 (shadow) | 20 | 46 | 138 |
| Winged shadow | 4 | 31 | 8 | 126 |
| Horned shadow | 4 | 32 | 8 | 126 |
| Winged horned shadow | 4 | 34 | 9 | 126 |
| Bull shadow | 4 | 35 | 14 | 126 |
| Winged bull shadow | 4 | 36 | 8 | 126 |
| Larva shadow | 4 | 23 | 4 | 132 |
| Butterfly shadow | 4 | 24 | 8 | 126 |
| Slug shadow | 4 | 21 | 14 | 126 |
| Bony shadow | 4 | 39 | 10 | 126 |
| Bird | 32 (bird) | 1547 | 11 | 9 |
| Guard | 6 (scene actor) | 53 | 42 | 15 |
| Cage | 44 (cage) | 937 | 31 | none |
| Bomb | 19 (barrel) | 99 | 8 | none |
| Pot | 19 | 101 | 5 | none |
| Barrel | 19 | 92 | 7 | none |
| Lever | 22 (floor lever) | 151 | 4 | none |
| Sword | 14 (weapon) | 77 | 10 | none |
| Magic sword | 14 | 79 | 35 | none |
| Stone couch | 16 (couch) | 160 | 8 | none |

The game never names its shadows: theirs follow the word each model's own
file name is built on (wing, horn, bull, caterpillar, butterfly, sea slug,
bone; the plain one is "boy"). The animations column is what the host stage
holds of the model's block, as the viewer counts it.

## Credits

**What plays.** Credits plays the ending from the scene where the staff
roll starts: the three staff scenes (stageData 60 STAFF1 on st13b, 61
STAFF2 on st04a, 62 STAFF3 on st13c) with the roll over them, as the real
ending shows them, and returns to the title when the roll ends. The real
ending goes on from there to the beach (39), the logo (63) and the clear
save; the playback does not.

**How it starts.** The real ending reaches STAFF1 from its last scene,
`actConte14_13` on 26b4demo2 (stage 56), through `RequestStageChange(6,
...)`: exit 216. The stage's own objects run `actStaff1` (the roll's
layout), `actStaff1Chk`, then `actStaff1Demo`, which calls
`staffRollStart(1.0f, 255)`; STAFF1 to 3 chain on through their own exits
(`ico2/script/src/end.c`). So the playback is a stage change from the title
to STAFF1, done the way the title's Load leaves for a save's stage
(`layout_action.c`, `la_load_processing`): the title theme's fade step, the
stage's environment sounds closed, `stgmgrForceSwitchWithFade(60, 8.0f,
4.0f)` and `ACTGame_SetActors_Debug(60, 0)`; then the boy's record is put at
exit 216's entrance (`ACTGame_StageChangeGObjID(54, 1, 216)`), where the
real ending puts him (the first exit into stage 60, which
`ACTGame_SetActors_Debug` uses, is exit 26, elsewhere). No script entry is
changed: STAFF1's objects start the scenes as in the real ending. The menu
closes on the game's empty layout (55) and the title will come back with the
cursor on Settings. The engine is `port/game/credits_live.c`; the flag,
the lock and the start are `port/game/credits.c` (`ico_credits.h`).

**The music.** The ending's song, "ICO -You were there-" (`adpcmFile` 47,
`event/39_8.int`, 265.6 s), starts in `actEndDemo06Chk`, six scenes before
the roll. Run headless from 13b4demo3 (stage 47, where `actEndDemo06` is)
with the flags of the scenes before it set, the real ending opened it at
Main tick 122 and started the roll at Main tick 3899: 151.1 s into the song
at PAL's 25 Hz Main tick. The playback requests it in `actStaff1Chk` (under
the flag) to open 151.2 s in (the 80 ms are the two ticks the script
daemon takes to open it), so the roll starts on the same bar; the song's
last 12 s overlap STAFF3's own piece (stream 49, "Castle in the Mist"), as in
the real ending. A stream opened part way in is a port hook in
`fumi/sound/adpcm_init.c` (`ico_adpcm_set_start`): the next open of that
stream fills its ring from the given byte (a 2 KB sector, which keeps the
0x400-byte channel interleave), the ring's next read follows it and the
skipped bytes count as played. It is unset except for this one request.

**How it ends.** Under the flag `actStaff3RollChk`, which waits for the roll
to end, fades every stream (`AdpcmFadeCloseAll(80)`, as it fades `ed6`) and
changes to the title, stage 1, with `RequestStageChangeSimple(1, 16.0f,
8.0f, 0, 0, 0)`: the roll's own fade out (16) and the fade in the real
ending's way into the title uses (`actEndingSave`, 8). The title comes back
in the mode it was left in (`opDemoMode` 1: the logo and menu), and its
theme starts again from its start, requested by `actTitleShortCut` as on
any return to the title.

**Nothing is kept.** Before the stage change the engine takes everything a
save holds, with the game's own `gamesysMemorySave` into a buffer of its own
(the flags, the object records that place the boy and Yorda in a stage, the
generators, the hints, the character record, the back stage, the second
flag set), the checkpoint image `gameSysMainSaveBuff`, `gFlagGameClear` and
`systemStatus[2..4]`. As stage 1 is entered again (`StageManager.c`,
`start_stage_Load_thread`, after `exit_stage` and before the stage's
objects are built) it puts them back with `gamesysMemoryLoad`, as the
title's Load does, and the flags bit by bit after it (the load sets flag
394). The staff scenes move the boy's record to each staff stage; without
the restore the title came back with no boy, and its camera, falling back
to him when the title's animation ended, read through a null object. No
save is written: the playback never reaches `actEndingSave` (which, under
the flag, would skip the ending signal and the save) or a save screen.
While the flag is on, achievements are suspended as in developer mode, for
the rest of that run (docs/port/ACHIEVEMENTS.md, "Suspension"). The log
has `credits: enter`, `credits: stage 60 up`, the song's request and start,
`staff roll: start ... (Extras > Credits)`, `staff roll: the port credit is
posted`, and `credits: back at the title ..., the game's state put back`.

**No skip.** Triangle and Start do nothing during the playback, as in the
real ending (its staff scenes switch to the empty layout, 55, and the
pause belongs to the in-game layout's `la_game_loop`).
It lasts about four minutes (6,030 Main ticks from the Cross to the title
in the headless run: STAFF1 and 2 about 100 s, STAFF3 to the end of the roll
about 138 s). Leaving early would mean stopping the roll, the streams and
the scene scripts part way through, which the game never does.

**The lock.** Credits is unlocked once the port has recorded the ending:
the ending achievement `finish` unlocked, or the achievements file's clear
count (`[stats] clears`) above 0 (docs/port/ACHIEVEMENTS.md). Both are
written at the ending's `ICO_GS_EV_ENDING` signal in `actEndingSave`,
whether or not the clear save is then made. The card's own clear state (a
save with flag 395) is not read: it exists only when the player saved at the
end, and reading it means opening every save file. An ending reached while
the achievements were suspended (developer mode, a start stage) does not
unlock it. Locked, the row is greyed with the value "Locked" and, with the
cursor on it, the note "Finish the game to unlock" (`UI_STR_EXTRAS_LOCKED_NOTE`,
five languages); Cross writes `credits: locked ...` to the log. The developer
key `[dev] unlock_credits = true` (ini `unlock_credits=1`) unlocks it for
tests (docs/port/CONFIG.md).

**The port credit.** Every staff roll, the real ending's and this one,
ends with a port section after the disc's last line (the copyright line):
twelve blank lines, the heading `{R}< Decompilation and PC Port > `, four
blank lines and the name `{R}Nathanial Fine `, in the roll's own heading and
name forms (docs/port/UI.md, "Staff roll"). The disc's table is not
changed.

**Tests.** `credits` (CPU, `port/game/test/credits_test.c`): the game's
`staffroll.c` over a short table in the disc's forms, the port lines last,
heading then name, the roll's end after them; the lock with and without
the key; a start with no engine. `settings` (`settings_test`): the locked
row, the unlocked row with the key, a failed start staying on the page, and
a start through a fake engine leaving for layout 55 with the title's
cursor on Settings. `achievements`: `finish` unlocks Credits; the playback's
flag suspends. `credits_headless` (the headless game and the disc image;
RUN_SERIAL, 600 s, 77 without the image; `port/ui/test/credits_headless.py`):
a run with the row locked (no playback, `credits: locked`), then one with
`unlock_credits=1` from the title through Settings > Extras > Credits: the
log lines above in order, `stage_no 62 -> 1`, no beach or achievement
unlock, 1,500 ticks of title after the return (exit code 0), Triangle and
Start pressed during the roll changing nothing, and the saves folder the
same as after the locked run.

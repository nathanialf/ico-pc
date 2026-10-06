# Extras

Settings > Extras is the page for what is not part of playing the game: a
music gallery, a model viewer and the credits. It sits in the Settings menu
because the title menu has no room for more rows (docs/port/SETTINGS.md,
"Extras"), and it is there only when Settings was opened from the title: the
galleries take over the stage and the pause menu has no stage to give.

| entry | what it will be | status |
| --- | --- | --- |
| Music | the music gallery: the soundtrack (the streamed music, with the album's titles), the scene sounds, the ambiences, the voices and the sound effects by bank, played through the game's own engines | live; docs/port/MUSIC.md |
| Models | a viewer for the game's character and object models (below) | live |
| Credits | the staff roll, locked until the ending has been reached ("Finish the game to unlock") | coming in package CRED |

Until its package lands, selecting Credits does nothing and
writes `extras: credits not available yet` to the log. Credits already has its
locked look: greyed, with the value "Locked" and the note.

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
the overlay shows the model's name, the animation playing (with "Loop" while
it loops) and its frame, "Frame F / N". An object with no animations shows
"No animations" and can still be turned and zoomed.

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
stage's tint. Without a presentation overlay (the headless build) the name,
animation and frame are rows of the viewer's layout instead.

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
and is not 88 or 91 (the ending's). The motion blocks are the Motion
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

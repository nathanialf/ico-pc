# The scene ledger

What each stage contained and what was left out of its pictures, by model name and by reason,
written to the log at the stage's end and carried in the bug report. The renderer's diagnostics
were rich on timing, devices and memory and silent on content: issue 221's report (the dream's
room without its walls, North American disc) could not say whether the wall model `st13_deja1`
was ever drawn, dropped by a cull, or never built, and the report was made minutes later in
another stage. The ledger answers those questions for every stage, always on, on every platform,
with no key.

Files: `port/render/rd_ledger.h` and `rd_ledger.c` (the tally, the text and the kept stages,
pure), `rd_ledger_live.h` and `rd_ledger_live.c` (the renderer's hooks), `rd_ledger.cmake` (the
sources and the tests), `tools/check_drop_ledger.py` (the drop sites' gate). The stage comes from
`port/platform/window_perf_rank.c` (`window_perf_rank_step`, the game's `stage_no` and its name,
once a game step); the report's entry from `port/platform/report.c` (`ico_report_set_ledger`).

## What it records

Per stage, one row per model name, at most 256 rows, the events of names past the table counted.
Every call site's name is the object's model name, `o->model->name`: the mesh built at load
(`Packet.c` `pac_makePacket` passes `obj->name`, and `pac_MakePacket` passes `o->model` as
`obj`), the mesh built at a draw (`RegistPacket.c` `regHostIdent`'s `regKeyObj->model->name`),
the box cull (`regHostCullDrop`'s `o->model->name`) and the game's hide
(`StageAnimation.c`'s `d->model->name`). The row's key is that name cut to 23 bytes (as the mesh
record cuts it), and a model pack's mesh name, `"<model> part <n>"` (`pac_hostPartName`), is
filed under its model, also when the cut falls inside `" part "` (`rd_ledger_key`;
`rd_ledger_test` checks one row from the five sources):

- **built**: meshes built under the name in the stage (`rd_create_vu_mesh`);
- **drawn**: the ticks with a draw of it recorded, its draws and their triangles
  (`rd_draw_vu_mesh`, the triangles of the batches drawn), and of them the triangles the mesh
  marks hidden by the content table (`ICO_VU_MARK_HIDE`, `rd_content_hide.c`: drawn nowhere) and
  those marked as overlapping an earlier one (`ICO_VU_MARK_LATER`: drawn over it), counted once a
  stage and a batch range from the mesh's marks;
- **left out**, by reason (`RdLedgerReason`), the draws, parts or objects left out and the ticks
  (the game's side) or pictures (the visibility test, which runs once a picture) it happened in,
  with the share that happened between ticks:

| reason | where | counted by |
| --- | --- | --- |
| the game's box cull (whole object) | `RegistPacket.c` object box (rule 2, part -1), through `rd_note_cull_drop` | ticks; "with the cameras between ticks" when the cull tested the blended cameras |
| the game's box cull (a node) | `RegistPacket.c` node box (rule 2 with a node) | ticks, as above |
| the game's box cull (a part) | `RegistPacket.c` part box (rule 3) | ticks, as above |
| its mesh could not be built | `rd_create_vu_mesh`: the stream refused (`descCount`) or no memory | ticks |
| a draw with no live mesh | `rd_draw_vu_mesh`: the mesh id has no live record | ticks |
| a batch range past its mesh | `rd_draw_vu_mesh`: the first batch past the mesh's | ticks |
| photo mode's draw filter | `rd_draw_vu_mesh`: the filter left the object out | ticks |
| no frame open to record it | `rd_draw_vu_mesh`: `pushVu` found no frame being recorded | ticks |
| the port's frustum test | `rd_vis.c` `visWalk`: off the picture or behind the eye | pictures; "between ticks" for the blended ones |
| the port's occlusion test | `rd_vis.c` `visWalk`: behind what list 0 drew before it | pictures, as above |
| an empty packet (no geometry the port can read) | `Packet.c` `pac_hostBuild`: `pac_hostDesc` found no data, no UNPACK or no vertex size, so no mesh and no draw | ticks (at load and at each draw that asks for the mesh again) |
| a packet cut short (the rest of it not drawn) | `Packet.c` `pac_hostStream` stopped before the packet's end (an UNPACK past the data, the scratch's room, the 512-batch cap) | ticks, when the mesh was built |
| hidden by the game's flag (its display flag off) | `StageAnimation.c` `stage_DispAnimation` and its twins: a stage animation object with `disp` 0 | ticks; the row reads "hidden by" |
| hidden by the game (its stage animation not playing) | the same: the object's row has stopped or not started (`entry2->mode` outside 0 and 1) | ticks; the row reads "hidden by" |

Draws without a model name left out by photo mode's draw filter are rows of their own:
`(a grid)` (`rd_draw_vu_grid`), `(particles)` (`rd_draw_vu_particles`), `(screen prims)`
(`rd_screen_prims`), `(world prims)` and `(a shadow volume)` (`pushStub`, `rd_shadow_tris`).
The `(a grid)` and `(particles)` rows also count their recorded draws (`rd__ledger_unnamed_draw`:
a grid's strips times their length less two triangles, a particle's two), so such a row reads
"drawn in N ticks" beside the port's frustum skips of the grids outside the picture (the cloth,
water and light grids). Without that count every such row read "never drawn".

The stage animation row the European build leaves out of `stage_DispAnimation`'s walk (row 66;
row 62 on the Japanese, Korean and Chinese discs; none on the North American disc, whose row 66 is
`deja_lightning.bga`, the dream's room) is the game's own choice and has no reason: its objects
read "never drawn, no reason recorded".

The game's packet box test (`RegistPacket.c` `reg_clipPacketBoundingBox`) drops nothing on the
port: a box that fails picks the clipping program instead (its 0 results are remapped), so it has
no reason of its own; the gate fails if it can return 0.

A row the game hid reads **never drawn; hidden by the game's flag ... in N ticks** and the header
counts it among the models hidden by the game. A row built (in the stage or in the stage before,
never drawn there) that was never drawn, never left out and never hidden for any recorded reason
reads **never drawn, no reason recorded**: the game never asked for it through the paths the
ledger sees (below).

Textures, by the game's name of each (the TIM2's, `Texture.c` `t->name`), at most 256 a stage:
asked for (made from the game's data on a cache miss, `rd_tex.c` `store`), made, failed by why
(a size of 0 or past 4096, its format or data could not be read or no memory, the texture cache
full, the graphics side could not make it, the graphics card refused a pack's picture) and
replaced by a pack (`rdtex_replace`, under the game's texture's name).

Beside the models, the effects the game recorded (`rd_post`, `rd_shadow_tris`): fog, fades, the
letterbox, brightness, film noise, motion blur, depth of field, the flare, the glow, the aura, the
eye blur, the anti-aliasing chain and shadow volumes, by ticks on and their last key value (fog:
its strength, `RdPostParams.scalar[0]`; fade: its alpha; letterbox: the bars' lines; shadow
volumes: the triangles of the last call; the rest: the blend's FIX).

And per stage: its ticks (frame numbers, first to last), the pictures replayed and of them those
between ticks, and the replay's events of a frame of the stage before (the render thread runs a
frame or two behind; those are not counted in the new stage).

## The lines

At a stage's end (its number or name changes), `rd: scene ledger:` lines, each under 1500 bytes:

    rd: scene ledger: stage 40 (deja) ended: ticks 1209-2036 (828), 1656 pictures (828 between
    ticks); 41 models: 39 drawn, 6 left out at times, 2 never drawn (1 with no reason recorded);
    names past the table 0; the replay's events of the stage before 2
    rd: scene ledger: stage 40 (deja) effects: fog 828 ticks (last strength 0.43), letterbox 828
    ticks (last lines 58), shadow volumes 828 ticks (last triangles 1676)
    rd: scene ledger: stage 40 (deja) models: st13_deja1: never drawn, no reason recorded, 1 mesh
    built; curtain01: drawn in 800 of 828 ticks (1600 draws, 96000 triangles); left out by the
    port's frustum test in 56 pictures (28 between ticks), 56 draws or parts; ...
    rd: scene ledger: stage 40 (deja): 3 more models not listed (3 of them drawn and never left out)

The rows come in this order: never drawn first, then those left out at times (most pictures left
out first), then the drawn ones by draws; the first 48 are listed, the rest counted. A capture
(F12, Options > Save a picture for a bug report, the skin check's capture) writes the stage in
play so far the same way ("so far, at this capture") before its zip takes the log, so the
capture's log says what the scene held and left out up to that moment.

After the models, the textures:

    rd: scene ledger: stage 13 (st13c) textures: 41 names asked for (44 times), 43 made, 1 failed
    (1 names), 0 replaced by a pack (0 names); names past the table 0
    rd: scene ledger: stage 13 (st13c) textures that failed: w01 (the texture cache was full 1 times)

How to read one: a model the reporter misses that reads **hidden by the game's flag** was hidden
by the game itself in those ticks (its script or animation turned it off); one that reads
**never drawn, no reason recorded** was never asked for through the paths the ledger sees (see
below); **left out by the game's box cull** in the ticks of the
missing picture names the game's cull at the reporter's cameras (the between-tick share says
whether the wide or blended cameras did it); **left out by the port's frustum test** or
**occlusion test** names the port's culler (replay the capture with `--cull off`); **drawn in
every tick** with its triangles says the model reached the GPU, so the cause is the draw itself
(its state, depth, blend, fog or the driver).

## The report

The bug report (`logs/report-*.zip`, `report.h`) carries `scene-ledger.txt`: the last four
stages' text as they ended, oldest first, then the stage in play so far ("so far, at this
report"). A crash handler writes it: the text of the ended stages is kept formatted in static
buffers, the stage in play is formatted with `snprintf` alone, and the replay's half is left out
(with a line saying so) when the render thread holds its lock at that moment. At most about
51 KB, deflated with the report's other entries.

Every capture zip that takes the log carries the same `scene-ledger.txt` beside it
(`dump_zip.c` `ico_dump_zip_set_ledger`): F12's, the Settings picture's, the slow-scene and
skin-check captures' and the loss dumps', so a capture made in a later stage still says what the
stages before held. A capture on the dump worker's thread reads the game's half while the game
may write it; a line may come out torn, never past its buffer.

## The cost

A draw (the game's thread): the mesh's row from a cache by the mesh's slot and the stage's epoch
(`s_meshRow`, one compare of the id and one of the epoch), then three additions; a new mesh in
the stage, one name lookup (a hash of at most 23 bytes into 512 buckets). A drop on the game's
side: one name lookup. A skip of the visibility test (the render thread): one name lookup under
a spin lock; one lock more a picture. A stage's end: the replay's half added (at most 256 rows),
the text formatted (an insertion sort of at most 256 rows) and five to ten log lines, once a
stage. A draw's marked triangles are counted once a stage and a batch range (the cache keeps
the range's counts). `rd_ledger_test` measures it: a tick of a busy scene (the opening's boat:
3000 draws of 300 meshes, 60 box cull drops, 100 skips of the visibility test) through the same
calls, the mean over 2000 ticks printed as `rd_ledger: a tick of 3000 draws, 60 box drops and
100 skips: T us` and held under 250 us; and a draw's tally alone against a control loop. Memory:
about 1 MB of static data (the mesh cache 576 KB: 36 bytes for each of the 16384 mesh slots;
four stage tables of about 90 KB with their textures: the stage in play, the replay's half, the
stage being written and the report's copy; the kept text 40 KB).

## The gate

`tools/check_drop_ledger.py` (ctests `drop_ledger_gate` and `drop_ledger_gate_selftest`) fails
when a reason (or a texture's why) has no words or the words no reason (the C file's
`_Static_assert` holds the same at compile time), when a reason has no hook call, when an early
return of a ledgered function (`rd_create_vu_mesh`, `rd_draw_vu_mesh`, `rd_tex.c` `store`) has
no `rd__ledger_` call before it in its block, when `rd_note_cull_drop`, `visWalk`,
`rd__vis_frame`, `rdtex_find`, `rdtex_replace` or `pac_hostBuild` stop calling their hooks, when a
counted drop site's count changes, when a box test, a display flag test or a draw filter call is
not followed by its hook, and when the packet box test can return 0. A new drop site therefore fails the gate until it has a reason (the
enum, the words, a hook call at the site) and a row in the script's table.

## Not in the ledger yet

- the game's own decisions outside the stage animations: an object of the object layout the
  script turns off (`scpSearchGobj` and its display bits), a character's own hide: the object
  is never walked, so it shows as never drawn when its mesh was built, without the "hidden by";
- the stage animation rows drawn by `stage_PlayBgAnimation` and the row the main twin skips by
  number (`entry1->no` 0x42 / 62), and rows whose kind mode is set: drawn elsewhere or never;
- the vertex pre-pass's early out and triangles the GPU clips away: the mesh counts as drawn;
- textures asked for but never made because the game's own slot is freed (`Texture.c`
  `texHostTexture` returns before the store) and a pack's picture the pack declined (its budget,
  `texpack.c`): the first shows as never asked for, the second as not replaced.

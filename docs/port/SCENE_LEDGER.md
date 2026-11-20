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

Per stage, one row per model name (the name the game gave the packet's mesh,
`ico2/seki/src/Packet.c` `pac_hostBuild`, which is the model table's name, or the box cull's
model name, `RegistPacket.c` `regHostCullDrop`; cut to 23 bytes as the mesh record keeps it), at
most 256 rows, the events of names past the table counted:

- **built**: meshes built under the name in the stage (`rd_create_vu_mesh`);
- **drawn**: the ticks with a draw of it recorded, its draws and their triangles
  (`rd_draw_vu_mesh`, the triangles of the batches drawn);
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

A row built (in the stage or in the stage before, never drawn there) that was never drawn and
never left out for any recorded reason reads **never drawn, no reason recorded**: the draw was
lost before the port saw it, at one of the places the ledger does not see yet (below), or the
game never asked for it.

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

How to read one: a model the reporter misses that reads **never drawn, no reason recorded** was
lost before the renderer (see below); **left out by the game's box cull** in the ticks of the
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
41 KB, deflated with the report's other entries.

## The cost

A draw (the game's thread): the mesh's row from a cache by the mesh's slot and the stage's epoch
(`s_meshRow`, one compare of the id and one of the epoch), then three additions; a new mesh in
the stage, one name lookup (a hash of at most 23 bytes into 512 buckets). A drop on the game's
side: one name lookup. A skip of the visibility test (the render thread): one name lookup under
a spin lock; one lock more a picture. A stage's end: the replay's half added (at most 256 rows),
the text formatted (an insertion sort of at most 256 rows) and five to ten log lines, once a
stage. `rd_ledger_test` times a draw's tally against a control loop of the same additions in the
same run. Memory: about 470 KB of static data (the mesh cache 192 KB, four stage tables of
56 KB: the stage in play, the replay's half, the stage being written and the report's copy;
the kept text 32 KB).

## The gate

`tools/check_drop_ledger.py` (ctests `drop_ledger_gate` and `drop_ledger_gate_selftest`) fails
when a reason has no words or the words no reason (the C file's `_Static_assert` holds the same
at compile time), when a reason has no hook call, when an early return of a ledgered function
(`rd_create_vu_mesh`, `rd_draw_vu_mesh`) has no `rd__ledger_` call before it in its block, when
`rd_note_cull_drop`, `visWalk` or `rd__vis_frame` stop calling their hooks, and when a counted
drop site's count changes. A new drop site therefore fails the gate until it has a reason (the
enum, the words, a hook call at the site) and a row in the script's table.

## Not in the ledger yet

These places leave geometry out without a hook; the gate counts each, so their number cannot
change unseen:

- the game's packet box test (`RegistPacket.c` `reg_clipPacketBoundingBox`, its three box tests
  and six callers): a material packet outside the frustum, after the part's box passed;
- a packet with no batches (`Packet.c` `pac_hostDesc` returns 0: no data, no UNPACK, no vertex
  size), after which `regHostMesh` returns at `m.id == 0` without a draw;
- a packet's stream cut at the batch cap (512) or the scratch size (`Packet.c`
  `pac_hostStream`): the rest of the packet is dropped;
- the game's own decisions not to draw an object (its keep and hide flags, an object the script
  turns off, `scpSearchGobj` and the object layout's display bits): the object is never walked,
  so it shows only as never drawn when its mesh was built;
- the draw filter in `rd_draw_vu_grid`, `rd_draw_vu`, `rd_screen_prims`, `pushStub` and
  `rd_shadow_tris` (grids, unnamed VU draws, screen prims, world prims and shadow strips carry no
  model name);
- triangles hidden inside a drawn mesh (the content table's marks, `rd_content_hide.c`), the
  vertex pre-pass's early out, and triangles the GPU clips away: the mesh counts as drawn;
- textures: which textures a stage asked for by name, loaded, failed or replaced by a pack.

The ledgered places in the game's sources (`ico2/`) need hooks at every disc twin and must add no
static (the layout rule), which is why the packet-side places wait for their own change.

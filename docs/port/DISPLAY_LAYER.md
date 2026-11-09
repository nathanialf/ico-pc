# The display layer

While a temporal upscaler is in force ([the scale split](RENDER.md#the-upscale-point): FSR 3.1, or
the developer's stand-in) the game's own 2D layer is not drawn into SCENE: SCENE holds the 3D
picture and the post chain, the upscaler runs at the layer's boundary, and every command from there
to the reduction is drawn into COMPOSITE (UPSCALED's record) at the display scale, over the finished
picture (`port/render/rd_layer.h`, `rd_layer.c`). The brightness, the keep sprite, the fades, the
letterbox, the film noise, the save tile and the game's text (list 12, `DisplayFont.c`) are then
never accumulated or sharpened by the upscaler. With the upscaler off the scan returns before
touching anything: every picture is the same bytes.

| path | what it holds |
| --- | --- |
| `port/render/rd_layer.h`, `rd_layer.c` | the plan, the boundary, the lends, the keep copy, PRE_DISPLAY, the reserved slots, the counters and the lines |
| `port/render/rd_upscale.h`, `rd_upscale.c` | the composite (`rd__upscale_composite_ensure`), the dispatch at the boundary (`rd__upscale_dispatch_at`), the reset reasons (`resetWhy`), `RdUpscaleStats` |
| `port/render/rd_velocity.h`, `rd_velocity.c` | the reset's reason in the marker's `u[2]` (`RD_UPSCALE_WHY`), `rd__velocity_keep`, `rd__velocity_forget_as` |
| `port/render/rd_interp.c` | `feedback`: the held copy PRE_DISPLAY <-> PRE_HELD; the camera snap's reset |
| `port/render/rd_core.c` | the reserved slots' names, the temporary allocator stopping below them, `rd_discard_frame`'s cut carry |
| `port/render/tools/rd_replay_tool.c` | `--list`'s markers, `--stats`' upscale line, `--repeat` as the keep loop |

## The plan

The plan is made once a replay (`rd__layer_scan(f, keep)`, first in `rd__store_plan_scan`;
`rd__layer_plan` is pure, so `rd_replay_tool --list` runs it without a device). Walking the frame as
the replay does (from `rd__first_list(keep)`, every command an ordinal): list 11's first command;
the marker (list 12's `RDC_UPSCALE_POINT` naming SCENE and DISPLAY; none, no layer, silently: the
still re-present's empty keep frame, a dump of format 16 or below); the reduction's two
`RD_POST_REDUCTION` stubs after it, with their states; the **boundary**, the first command after the
last scene-class command of lists 11-12 before the marker, list 11's first at the earliest (the
fades fold's end-of-list rule, [RENDER_POST.md](RENDER_POST.md#the-fades-fold)); the keep sprite
(`isKeepSprite`, `rd_post.c` `postKeep`: one two-vertex `RDC_SCREEN` sprite into SCENE textured by
DISPLAY's `RD_VIEW_RGB24_TA0`, MODULATE, no blend, no Z test, no DATE or AA1, no UV offset, covering
the frame); whether lists 0-10 read DISPLAY (`feedsDisplay`), copy it (`displayCopied`) or copy
PRE_DISPLAY and PRE_HELD (`preCopyOrd`).

## Scene-class commands

A scene-class command stays in SCENE, before the upscale (`rd__layer_sc_name` gives the log's
words): a VU draw (mesh, skinned, grid, particles), a shadow command, the fog, a copy or exact blend
naming SCENE, a command type the layer does not know; for a screen prim or world prims drawn into
SCENE's colour or depth: the Z test on SCENE's depth (ZTE with ZTST not ALWAYS), Z write on SCENE's
depth, DATE, a blend reading the destination alpha (the three Ad blends with ABE), PRIM.AA1; SCENE's
depth view sampled; a draw into another target that samples SCENE. A clear is never scene-class. Z
write without the Z test (the letterbox's bars) is not scene-class while "the bars are free": the
present does not read SCENE's depth (`rd__effects_depth_wanted`) and no frame has read SCENE's depth
before its clear (the walk sees it; once seen it holds for the run, logged once), so their Z goes
nowhere and is dropped with SCENE's depth (the composite has none). In the game, lists 11-12 hold
scene-class commands for Z-tested world lines (mostly debug and tool draws); such a frame begins the
layer after the last one, and what lies before it goes through the upscaler that frame (counted,
logged).

## The boundary and the lend

At the boundary (`rd__layer_due(g_rdStoreOrd)` in the list loop of `rd_replay.c`, which flushes the
screen run and the VU merge, ends the pass and takes the plain viewport as at the marker, then
`rd__upscale_point` calls `rd__layer_begin`): DISPLAY's record given back (below); a crack fill the
walk still holds drawn into SCENE now (`rd__crack_fill_late`: its place at the walk's end would find
the composite, which has no depth); on a picture, PRE_DISPLAY when lists 0-10 read DISPLAY (below),
then the dispatch (`rd__upscale_dispatch_at`, reset KEEP when keep frames were held since the last
one); on a keep frame, no dispatch (below). Then SCENE's record **lends** the composite's colour,
snapshot and texel geometry (`tw`, `th`, `sx`, `sy`, `ox`; no depth, `uintView` and `dstUsage` 0)
until `rd__layer_end` after the last pass: every later command is replayed as recorded (its target
ids, scissor, UI mirror and half pixel, SCENE's own rules for the full-frame extension and the
screen copies) at the display scale, and the reduction reads the composite through SCENE's record.
There is no new target id: the passes on the composite are named SCENE in the pass ring, the per-pass
timing and `--pass-times`. The write serial is bumped and the DATE snapshot's key cleared at both
ends. At the end, a colour and snapshot that changed places while lent (a self-sampling draw's
swap) are copied back so the composite keeps its own storage-capable texture.

## Keep frames

On a keep frame (fbKeep: lists 11-12 over the picture before, a stage switch's fade;
`gsb_KeepFrameBuffer`, `RD_POST_KEEP`) there is no dispatch; `keepHeld` counts it. When the keep
sprite lies in the layer with no draw sampling SCENE between the boundary and it, the composite (the
last picture, its layer included) is copied whole into COMPOSITE_HELD (the composite's geometry,
outside any pass) at the boundary; the head's clear, the brightness and the fades are drawn into the
composite as recorded, and at the keep sprite (`rd__layer_keep_verts` from `doScreen`) DISPLAY's
record lends COMPOSITE_HELD's for that draw, the filters are NEAREST and its two vertices are
replaced: x from (2048 - W/2 - 1) x 16 to (2048 + W/2 + 1) x 16, y the same with H (a GS pixel of
margin past the frame, as the recorded sprite's), less 8 under `RD_TARGET_HALF_Y`; the UVs the GS
position less the frame's corner, plus k x 16 with k = (s - 1) / 2s per axis at the composite's
scale s (`rd_replay.c`'s `snapAxis` takes that sample shift off a scaled sprite's UVs), so every
texel samples its own centre: a 1:1 copy at any scale and any Full pixel `ox`. The recorded z, q,
colour (112, 112, 112, 128) and state stay: the darkening is the GS MODULATE floor(c x 112 / 128),
once a tick, as on the PS2. At the next command the run is drawn and the filters and DISPLAY's
record are given back. A keep frame whose keep sprite is not in the layer holds the composite and
draws its layer over it (counted, logged). The PS2's loop also stretched its reduced picture over
the frame and reduced it again each tick; the hold does not
([DIVERGENCES.md](DIVERGENCES.md#options-that-change-behaviour), `[video] upscaling`).

## History resets

A dispatch that resets the upscaler's history counts one reason (`RD_UPSCALE_RESET_*` in
`rd_upscale.h`, `RdUpscaleStats.resetsBy[]`), the first that holds in this order: first (the first
dispatch), size (a new size), cut (the replayed frame's `rd_camera_cut`, or a cut its marker
carries: a cut on a keep frame, which never dispatches, or on a discarded frame), camera (the wall's
cut, `rd_camera_cut_wall`, or the interpolator's camera snap `RD_SNAP_CAMERA` / `RD_SNAP_WALL` at a
tick's first present), keep run (keep frames held since the last dispatch, or the marker's KEEP),
forgotten (any other mark). The marker's `u[2]` carries `RD_UPSCALE_NO_HISTORY` and the reason above
it ([RENDER.md](RENDER.md#velocity)); `--list` prints it. A cut on a keep frame is counted as the
next picture's cut, not as the keep run.

## A discarded frame's cut

A frame dropped unreplayed (`rd_discard_frame`, `port/render/rd_core.c`) drops its lists, but its
camera cut is carried to the next frame (`cutPending`; a plain cut wins over the wall's), since the
camera moved all the same. The next picture resets the upscaler's history, and the interpolator
snaps there (`RD_SNAP_CUT`) where it blended, with the upscaler off too.

## PRE_DISPLAY and the motion blur

The motion blur (`staticBlur.c`) reads DISPLAY in list 7. At a picture's boundary,
when lists 0-10 read DISPLAY, the reduction's two sprites are replayed from SCENE under their
recorded states with the exact viewport into PRE_DISPLAY (DISPLAY's GS size at SCENE's render scale
and `ox`, the vertical scale doubled under the full height), before the edge guard: what DISPLAY was
without the upscaler. In the next frame that reads DISPLAY, DISPLAY's record lends PRE_DISPLAY's to
the boundary (every lookup, `resolveTexture`, `texCropOf`, `doBlurSpriteDraw`, `rd_blur.c`, sees one
geometry), so no upscaled picture is fed back into SCENE; not on a keep frame, nor while the lists
copy DISPLAY, nor before PRE_DISPLAY holds a picture, nor while DISPLAY waits for its first clear.
Under the interpolation the held copy at the lists' head (`rd_interp.c` `feedback`) copies
PRE_DISPLAY into PRE_HELD at a tick's first present and back at the later ones, in place of DISPLAY
and DISPLAY_HELD, when `rd__layer_feedback_ids` finds both at one size (a later present only when
the tick's first did); PRE_HELD is made at the boundary beside PRE_DISPLAY while the interpolation
is on (the interpolation makes no GPU call), and the lend then begins after that copy
(`preCopyOrd`: one texture in two records over the copy would split its barrier states). The first
tick after PRE_DISPLAY is made (the first under the layer, the first after the targets are made
again) still carries the copy of DISPLAY: that tick's motion blur reads the upscaled DISPLAY
(logged). A frame that read DISPLAY in lists 0-10 and again in lists 11-12 (not seen in the game)
holds only the PRE copies. Memory: PRE_DISPLAY one render-scale target while the motion blur runs
under the layer, PRE_HELD one more with the interpolation, COMPOSITE_HELD one display-scale target
once a keep frame is held.

## Refusals and the switch

A refused frame takes the path without the layer (lists 11-12 into SCENE, the marker
dispatches). The refusals: the replay tool's pass window; a keep frame whose keep sprite lies before a
scene-class command, or after a draw sampling SCENE; at the boundary, SCENE multisampled, no
COMPOSITE, a keep frame before the composite holds a picture (the first after the targets are made
again, a keep dump replayed without `--prev`), no COMPOSITE_HELD. A dispatch the hook refuses at the
boundary leaves the reduction reading SCENE (the upscale point's own line). **`[dev] display_layer =
false`** (`ICO_RD_DISPLAY_LAYER=0`, read once, [CONFIG.md](CONFIG.md)) keeps that path for every
frame, for an A/B.

## The lines

Once a run and at each new composite size: `rd: display layer: on under <hook>: lists
11-12 after the last scene-class command are drawn into COMPOSITE WxH (display scale SxS, Full pixel
ox N) after the upscale; keep frames hold the composite 1:1 darkened, no dispatch`. Once: `rd:
display layer: off ([dev] display_layer = false): lists 11-12 are drawn into SCENE and upscaled with
it (the path before the layer, for an A/B)`; `rd: display layer: SCENE's depth is read before frame
N clears it: draws that write Z without testing it (the letterbox bars) stay in SCENE from now on
(reported once)`. The first 8 then every 100th, `rd: display layer: frame N: ... (K times so far)`
with `not begun (<why>): lists 11-12 drawn into SCENE and upscaled with it`, `the layer begun after a
scene-class command, <kind> in list L (command type T): what lies before it is upscaled with the
scene`, `a keep frame with no keep sprite: the composite held, its layer drawn over it`, or `lists
0-10 copy DISPLAY (the interpolation's held copy made before PRE_DISPLAY and PRE_HELD held a
picture): the motion blur reads the upscaled DISPLAY this tick, not PRE_DISPLAY` (once each time
PRE_DISPLAY is made; a count that keeps growing is the fault). Every 10 s while the split is on and
something was dispatched, held or refused: `rd: upscale point: 10 s: N pictures dispatched, K keep
frames held (no dispatch), resets R (first a, size b, cut c, keep run d, camera e, forgotten f),
display layer draws D (textured T, keep copies C), L frames with the layer begun after a scene-class
command, P pre-upscale reductions for the motion blur (read as DISPLAY by Q frames), refused X` (X the
dispatches and the layer's frames refused together). The fades fold's once-a-run line says `fades
fold: off while a temporal upscaler is in force; lists 11-12's sprites are drawn into the
display-scale composite after the upscale (the display layer)`. `RdPerfRecord.layerDraws` is the
replay's layer draw count. `rd_replay_tool --list` prints `[display layer begins]` before the
boundary's command, `[keep copy]` after the keep sprite the layer holds, and ` new history
(<reason>)` after a marker's `src ... dst ...` that carries a reset; `--stats` the upscale line above.

## Tests

`rd_upscale` (its OpenGL twin `rd_upscale_gl`, and `rd_upscale_layer_tiler`: the whole program
under `ICO_VK_FAKE_LIMITS=moltenvk`, the tile-based device profile's store plan, pass areas, pass
cuts and folds planning SCENE's passes before the walk while the layer lends SCENE's record
mid-walk): the layer's pixels, the keep hold (4:3 and a 16:9 Full pixel crop), the resets by reason,
the held PRE_DISPLAY under the interpolation, and the Off identity of a keep frame;
`rd_upscale_off_mali` and `rd_upscale_off_tiler` (the Off cases under a phone's limits and the
tile-based profile); `rd_replay_tool_upscale_keep` and `rd_replay_tool_fsr3_keep` (`rd_pixel`'s keep
frame replayed 20 times after its frame: one dispatch, 20 keep frames held as 1:1 copies)
([TESTING_PROGRAMS_RENDER.md](TESTING_PROGRAMS_RENDER.md)).

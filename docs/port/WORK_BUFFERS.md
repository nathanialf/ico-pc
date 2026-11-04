# The post effects' work buffers

The targets `staticBlur.c`'s effects and the screen softening draw through on their way back into
SCENE, their sizes at each frame width, the glow chain's rectangles in them, the column the glow's
blur leaves on a frame wider than the PS2's, what stays in them from one frame to the next, the
glow a tick's later presents paste from its first, the work clear that shows a texel read before it was drawn, and how the puddle's ripples sample their
reflection block between ticks, and where the crack filler is drawn (after lists 4-6), and
(not work buffers, kept here because RENDER.md is at its size cap) how a screen sprite redrawn under
a new key between ticks is paired, how desktop Vulkan decides on the local reads at start, how
the shadow count is read from the stencil in one draw (and its colour left unstored while it is
made), how the shadow levels are composited above
1x, which passes leave a depth neither loaded nor stored (the store plan), which passes load and store only the tiles their draws touch (the pass area), a source that holds only its clear read as its colour, which full-frame reads stay at a large
scale, and how the screen-prim triangles draw without the sprite planes (issue 161). The sprites themselves are drawn by the GS
sprite model ([RENDER_POST.md](RENDER_POST.md#post-passes), "The staticBlur sprites").

## Files

| File | What it holds |
| --- | --- |
| `port/render/rd_core.c` | the targets' sizes (`namedTargetDesc`), their scale (`rd__target_scale_of`: the work buffers at their GS size at every scale), their creation (`createNamedTargets`) |
| `port/render/rd_upload.c` | the clear of a new target (`rd__clear_new_targets`) and the work clear (`workClear`) |
| `port/render/rd_blur.c` | the strip over the glow's left edge (`rd__glow_edge_strip`) |
| `port/render/rd_blur_border.c` | the strip drawn after the blur's first pass (`glowEdge`, from `rd__blur_post_sprite`) |
| `port/render/rd_glow_hold.c` | the glow between ticks: WORK3 kept in WORK3_HELD at a tick's first present, the chain skipped and the copy pasted at its later ones (`rd__glow_hold`) |
| `ico2/sugipon/src/staticBlur.c` | the effects, their rectangles in the PS2's numbers and the port's widening (`SB_X`, `sbShrinkAt`, `sbShrinkSpan`, `sbWorkRect`) |
| `port/render/rd_crack_fill.c` | the crack fill armed at the fog and drawn after lists 4-6 where `rd__crack_fill_place` puts it (`rd__crack_fill`, `rd__crack_fill_at`, `rd__crack_fill_in_pass`, `rd__crack_fill_late`) |
| `port/render/rd_profile.c`, `port/render/rd_local_probe.c` | the desktop's local reads (`RdProfile.desktopLocal`) and their start-up cost probe (`rd__local_probe`) |
| `ico2/sugipon/src/puddle.c`, `port/render/rd_interp.c` | the ripples' texture shift between ticks (`drawRipples`; `reprojStShift`) |
| `port/render/rd_replay.c`, `port/render/rd_shadow_one.c`, `port/render/rd_shadow_selftest.c`, `port/shaders/blit.hlsl` | the shadow count in one draw (`shadowCountOne` picks the destination, `rd__shadow_one_draw` draws `shadow_count_ps`) and its start-up self-test (`rd__shadow_selftest`) |
| `port/render/rd_fx_plan.c`, `port/render/rd_profile.c` | the shadow composites above 1x: one bilinear tap unless `RdProfile.shadowBspline` (`rd__sprite_above1x`) |
| `port/render/rd_interp.c`, `ico2/common/src/layout_texture.c` | the same screen sprite under another key in each tick (`pairSame` in `unmatchedPass`); the load screen's bars (`LT_HOST_KEY`) |
| `port/render/rd_pass_area.c`, `port/render/rd_replay.c`, `port/rhi/vk/vk_cmd.c` | the pass area: the commands' boxes before the walk (`rd__pass_area_scan`, from `rd__store_plan_scan`), each pass's area (`rd__pass_area_begin` from `beginPass`), the end of its commands (`rd__pass_area_due`), the render area and the cut scissors (`rhi_cmd_begin_render_pass`, `rhi_cmd_set_scissor`) |
| `port/render/rd_store_plan.c`, `port/render/rd_replay.c`, `port/render/rd_profile.c` | the store plan: the scan before the walk (`rd__store_plan_scan`), each pass's depth ops (`rd__store_plan_pass` from `beginPass`), the walk's command count (`g_rdStoreOrd`), the profile's `storePlan` |

## Sizes per frame width

The frame is W GS pixels wide (the [native frame](DISPLAY.md#the-native-frame): 512 at 4:3, 688
at 16:9, 832 at 2340 x 1080, 848 at 2364 x 1080, up to 2560). The work buffers keep the PS2's
size per 512 columns of the frame (`port/render/rd_core.c` (`namedTargetDesc`)):

| target | PS2 | at W |
| --- | --- | --- |
| WORK0, WORK3 | 256 x 128 | W / 2 x 128 |
| WORK1, AA0, SHADOW0 | 256 x 256 | W / 2 x 256 (the glow uses its top 128 rows) |
| AA1, FEED128, FEED_HELD, AURA_TAP, SHADOW1 | 128 x 128 | W / 4 x 128 |
| WORK3_HELD | (none: the host's) | W / 2 x 128, WORK3's |
| WORK2_PAD | 256 x 64 | W / 2 x 64 |
| SHADOW2 | 64 x 64 | W / 8 x 64 |
| WORK2, AURA_WORK, DATE_SNAPSHOT | the frame | W x H, the scene's scale |

At 848 the W / 2 buffers are 424 wide and the W / 4 ones 212. A sprite into a buffer B GS pixels
wide covers pixel x when `x0 <= o + 16 x < x1` with `o = 16 (2048 - B / 2)` (12.4 units), and
samples `U(X) = u0 + (X - x0) (u1 - u0) / (x1 - x0)`, truncated; LINEAR weighs texels
`(U - 8) >> 4` and the next by the fraction `(U - 8) & 15`.

## The glow chain at a wide frame

`makeFullScreenFlareAfter` (list 7, the flare and the bloom) reduces WORK2 into WORK0, runs
`blur(n)` for n = 10 .. 19, blurs WORK0 into WORK3 (`eyeBlur`), and `pasteFullScreenFlare` pastes
WORK3 over SCENE. `blur(n)` is two sprites:

- pass 1, WORK0 into WORK1: the rect shrunk by `rm = n + 6` sixteenths, the UV from 8;
- pass 2, WORK1 into WORK0: the rect whole, the UV shrunk by `um = n + 6`.

On the PS2 both shrinks are at the right and bottom edges: pass 2's UV stops short of the columns
and rows pass 1 did not draw. The port keeps the PS2's slope on a wider buffer and anchors each
shrink at the 4:3 part's left edge, `a = W / 4 - 128` buffer pixels in (`sbShrinkAt`): pass 1's
rect and pass 2's UV both start `s = (16 a d + 2048) / 4096` sixteenths further right. Pass 1
then leaves WORK1's columns `0 .. ceil(s / 16) - 1` undrawn, and pass 2's pixel 0 samples
`U - 8 = s`, which weighs them:

| W | WORK1 | a | s for n = 10 .. 19 | WORK1 columns left | pass 2's pixel 0 |
| --- | --- | --- | --- | --- | --- |
| 512 | 256 | 0 | 0 | none | texel 0, which pass 1 drew |
| 688 | 344 | 44 | 3 .. 4 | 0 | texel 0 at 13/16 .. 12/16 |
| 832 | 416 | 80 | 5 .. 8 | 0 | texel 0 at 11/16 .. 8/16 |
| 848 | 424 | 84 | 5 .. 8 | 0 | texel 0 at 11/16 .. 8/16 |
| 2048 | 1024 | 384 | 24 .. 38 | 0 .. 1, 0 .. 2 | texel 1 at 8/16 .. texel 2 at 10/16 |
| 2560 | 1280 | 512 | 32 .. 50 | 0 .. 1 up to 0 .. 3 | texel 2 at 16/16 .. texel 3 at 14/16 |

The phone's dump (848 wide) records exactly these: pass 1 rects from 29381 .. 29384 with
`o = 29376`, pass 2 UVs from 13 .. 16. Every other sprite of the chain covers its buffer from pixel
0 (the reduction, pass 2, the eye blur, the WORK0 fill, the paste), and the right edge reads only
drawn texels, as on the PS2. Nothing else of the glow draws WORK1's column 0 but the sun's shrink
(`eyeBlur` with the sun on screen: WORK3 around the sun into WORK1's columns `0 .. W / 8 - 1`, rows
0 .. 31, after the blur), so on a wide frame the column holds whatever drew it last: the cleared
texture after the targets were made, or the opening's sun, kept through every room after it. Each
pass 2 weighs it into WORK0's column 0 at half or more, each pass 1 carries that a column further
in, and the paste spreads WORK3's columns 0 .. 10 over about twenty GS columns: a strip of an
earlier picture down the left edge, rows 0 .. 31 of the buffer being the top quarter of the
screen. On the phone it reached 12 output columns and rows 0 .. 277 of 1080, and a replay of the
same dump (fresh targets) did not show it.

The blur fold (`RdProfile.blurFold`) draws a pair as one pass and computes pass 1's texels in the
shader; on a wide frame it reads the column as the glow's edge strip leaves it
([The blur fold on a wide frame](#the-blur-fold-on-a-wide-frame)).

The depth of field (`depthField`) has the same anchor on its six passes and leaves columns 1 .. 2
of its buffers holding an earlier pass of the same frame (its copies draw from column 1, the
PS2's one-pixel border); it reads WORK1's column 0 (`copyToWork2`, 4/16) and WORK0's (the paste,
6/16) at every width, as the PS2 reads the VRAM there. The aura's alpha copy leaves WORK0's rows
112 .. 127 on the NTSC disc and its feedback clears FEED128's rows 112 .. 127 after reading them,
the PS2's own order. The rects and UVs of every sprite of a frame are in its
`rd_replay_tool DUMP x --list --no-device` output, from which the covered and read texels follow by
the rules above.

## The glow's left edge

After pass 1 of `blur(n)` on a wide frame, the replay draws the columns it left as its first drawn
column (`port/render/rd_blur_border.c` (`glowEdge`), `port/render/rd_blur.c`
(`rd__glow_edge_strip`)). The strip is taken for a FLARE or BLOOM sprite from one work buffer into
another, at scale 1, with its U from 8 and its rect starting 1 to 64 sixteenths right of the
target's pixel 0: its rect `{o, y0, x0, y1}` (pixels `0 .. n - 1`, `n = ceil((x0 - o) / 16)`), its
U `u0 = u1 =` the U the sprite has at pixel n, its rows and V the sprite's. In the GS model each
strip pixel reads the texels pass 1's column n reads at the same weights, so WORK1's columns
`0 .. n - 1` are column n's bytes and pass 2 reads this frame's glow there. No sprite of a 4:3
frame qualifies (s is 0), so 4:3 is unchanged by it (the camera's picture: the console's except
inside triangles it dropped, [DIVERGENCES.md](DIVERGENCES.md) VD-29); the rest of a wide frame's
WORK1 is untouched, and its 4:3 columns are as before. The strip is drawn through
`rd__replay_blur_sprite_draw` with a record of its own, in pass 1's state, so the recorded frame
and a dump of it hold the game's sprites alone and an old dump replays with the strip. One line
for each of the first four frame widths:

```
rd: glow: on a frame 848 GS pixels wide the blur's first pass leaves WORK1's column 0 undrawn (its rect anchored at the 4:3 part) and its second pass reads them: drawn as column 1 of this frame, not kept from an earlier one (issue 129; reported once a width)
```

`rd__set_glow_edge_fill(false)` (tests; `rd_replay_tool --no-glow-edge`) draws the glow as
recorded; `rd__glow_edge_count` counts the strips (ten a glow frame at a wide frame, less the pairs
the blur fold draws as one pass: those draw no pass 1 and so no strip).

## The blur fold on a wide frame

The blur fold (`RdProfile.blurFold`: every device class but Mali; `[dev] blur_fold` overrides) draws a pair of `blur(n)` as one pass into WORK0 through `fx_blur2_ps`, which computes
each WORK1 texel pass 2 weighs from WORK0 as pass 1 writes it ([RENDER.md](RENDER.md#the-replay),
"The blur pair fold"; `port/render/rd_replay_tiler.c` (`rd__replay_blur_fold`, `blurFoldFirst`,
`blurFoldOk`), `port/shaders/fx_sprite.hlsl` (`fx_blur2_ps`)). On a frame wider than 512 GS
pixels pass 1's rect starts `d` sixteenths right of WORK1's pixel 0, so it covers from column
`cx0 = ceil(d / 16)` (1 at 16:9), while pass 2's first pixel weighs column 0 (13/16 at 16:9): a
fold that took only taps inside pass 1's coverage would fold no pair there.

Those columns are not left as an earlier frame had them: the glow's edge strip
([The glow's left edge](#the-glows-left-edge)) draws columns `0 .. cx0 - 1` after every pass 1
drawn as a pass, each texel pass 1's texel at column `cx0` on its row (the strip's `n` is `cx0`
whenever its scissor starts at column 0). So the fold now counts the texels that hold pass 1's
bytes from column 0 when the strip applies (`BlurFoldPass.first`, from
`rd__replay_glow_edge_columns`), and `fx_blur2_ps` reads a tap left of `cx0` as pass 1's pixel at
`cx0` (`DrawCB.mode.w`, AREF elsewhere; the fold has no alpha test). Reading WORK1 itself there
would not do: a folded pair draws no strip, so WORK1's columns hold the previous pair's strip (or
an earlier frame's), not this pass 1's. Without the strip (`--no-glow-edge`) the taps there would
read texels no draw of this frame wrote, and such a pair is drawn as two passes.

Why WORK1 ends with the chain's bytes: a pair folds only when the next pair's pass 1 leaves its
bytes in every texel this one would have (columns `first .. cx1`, rows `cy0 .. cy1`, its own
coverage and strip), by induction in every texel of the first pair after it drawn as two passes,
whose pass 1 (and strip) writes them before its pass 2 reads WORK1; the chain's last pair is
always drawn as two passes. A texel outside is written by no folded pair either way. Equal
coverage is not needed; a subset is enough by the same induction.

The pairs a chain of ten folds (`rd_replay_tool DUMP x --stats`: `blur pairs folded N`), from the
geometry of `staticBlur.c` `blurPass` (`sbShrinkAt`, `sbShrinkSpan`) and pinned by `rd_blur_test`'s
`blur_fold_wide` shard:

| frame | WORK1 | pass 1 columns (cx0 .. cx1) | pass 2's taps | folded |
| --- | --- | --- | --- | --- |
| 512 (4:3) | 256 | 0 .. 254 | 0 .. 254 | 9 |
| 688 (16:9) | 344 | 1 .. 342, strip on column 0 | 0 .. 342 | 9 |
| 2048 (48:9) | 1024 | 2 or 3 .. 1021, then .. 1020 from blur(14) | 1 .. 1021 | 8 |
| 2560 (60:9) | 1280 | 2 to 4 .. 1276, then .. 1275 from blur(16) | 2 .. 1276 | 8 |

At 48:9 and 60:9 one pair's next pass 1 covers one column fewer on the right (blur(13) and
blur(15)); that pair is drawn as two passes, and the column it leaves stays its bytes as in the
unfolded chain.

The fold looks for a pair at every staticBlur sprite, so after a pair drawn as two passes it also
sees pass 2 of `blur(n)` and pass 1 of `blur(n + 1)`, the same shape with WORK0 as the
intermediate. Folded, that draws pass 1 of `blur(n + 1)` without the strip after it, and the next
pass 2 would read the strip's columns as an earlier pass left them; so a fold whose drawn pass the
strip would follow is refused. Without that rule the true pairs at 16:9 were refused (their tap
on column 0) and the nine shifted ones folded (`--stats` `blur pairs folded 9`, the glow's
WORK1 passes one each): exact only where the strip's columns happen to hold the same bytes
from pass to pass. With the strip off (`--no-glow-edge`) the shifted pairs may fold: nothing
draws those columns in the frame either way.

A pair drawn as two passes where the profile folds is logged once a WORK1 width and reason:

```
rd: blur fold: WORK1 344x256: a pair drawn as two passes (the chain's last pair) after 9 folded in this replay; pass 1 draws columns 1..342, holds its bytes from column 0 (reported once a width and reason)
```

(the reasons: `the chain's last pair`, `the glow's edge strip follows its pass 2`, `pass 2 weighs
a WORK1 texel pass 1 does not leave its bytes in`, `the next pair's pass 1 leaves its bytes in
fewer WORK1 texels`).

## What stays from one frame to the next

Every named target is cleared when it is made (`clearPending`, `rd__clear_new_targets`: colour 0,
depth 1.0), which happens at start, at a new scene scale (`rd__apply_display`, `recreateTargets`)
and at a new frame width (`rd_reset_scene`, from `ico2/seki/src/GsBase.c` when the game latches
another `gsFrame.w`; `port/render/rd_core.c`). One line each time, for the first eight:

```
rd: work targets: 19 named targets made and cleared (the frame 848 GS pixels wide): what the work buffers held from earlier frames is gone (reported for the first 8)
```

Between those, a target holds its last frame's texels. The frame reads them by design in DISPLAY
(the motion blur and the keep pass), FEED128 (the aura's feedback) and their held copies
DISPLAY_HELD and FEED_HELD (a tick's later presents, [DISPLAY.md](DISPLAY.md)), and WORK3_HELD, the
glow of a tick's first present that its later presents paste
([The glow between ticks](#the-glow-between-ticks)); the motion blur's
Smooth accumulator is a texture of its own, not a target, and the RGBA8 target it resolves into is
drawn before the sprite reads it ([RENDER_POST.md](RENDER_POST.md#the-motion-blur-under-smooth)). Anywhere
else, a texel read before the frame drew it is a bug of this kind.

### After a save state's load

A load ([PLATFORM.md](PLATFORM.md#saving-and-loading)) puts back the game's own copy of the
latch above with the rest of its data, so `gsb_HostSceneRelatch` clears it: the next frame's
latch calls `rd_reset_scene` and every named target is made and cleared again, as at a new frame
width, so no work buffer carries the picture from before the load. The load also signals a camera
cut (`ico_video_camera_cut`) at once and at the next poll, when the first restored frame is the
one being recorded, so the presenter does not blend the old picture into the restored one; the
loaded textures get new serials (`tex_HostReserialAll`) and every VU mesh is retired with the
mesh generations moved above the saving run's (`rd_drop_vu_meshes`, `port/render/rd_mesh.h`), so
each is built again from the restored packets.

## The glow between ticks

Above Frame rate Original every present of a tick replays the whole frame, so the glow's chain
([The glow chain at a wide frame](#the-glow-chain-at-a-wide-frame): the mask SCENE into WORK2 with
`fillWork2`'s band and the sun's fans, the reduction into WORK0, the ten blur pairs, the eye blur
into WORK3 with the sun's ghosts, shrink and tint, the WORK0 alpha fill) ran at each present, 20
passes at 4:3 2x with the blur fold, 17 of them on the work buffers (`rd_replay_tool --stats
--pass-times` on the godrays dump rd-01077: `glow 20 passes`, the three on SCENE `cleanUpFB`'s two
and the paste). `port/render/rd_glow_hold.c` (`rd__glow_hold`, from `rd__interp_frame` after the
feedback head copies):

- a tick's first present draws the chain and copies WORK3 into WORK3_HELD (WORK3's size, made with
  the named targets, never cleared by the work clear) right after the chain's last write of WORK3;
- its later presents turn the chain's draws into no-ops (the FLARE, BLOOM and EYE_BLUR sprites and
  the screen draws into WORK0, WORK1, WORK2, WORK2_PAD or WORK3 in the chain's list) and copy
  WORK3_HELD back into WORK3 in the last one's place: one 256 x 128 copy for 17 passes. The flare's
  draws into SCENE (`cleanUpFB`), the paste, the depth of field and the motion blur run as ever.

The later presents' glow is the tick's first present's: the same bytes on a still tick (the chain is
a function of the frame), and during a pan the halo half a tick's pan behind the scene, near the
glow's bright regions only, as the motion blur's old frame and the aura's feedback already are. The
chain is drawn after all at a later present when another draw reads one of its buffers before
drawing into it (the sun's glare onto SCENE from WORK1, `eyeBlur` mode 1), without this tick's copy
(a target made again since), under Options > Effects > Glow between frames: Smooth (`RdSettings.glowSmooth`, outcome
`RD_GLOW_SMOOTH`, text `glow redrawn (Glow between frames: Smooth)`), and under `[dev] glow_redraw
= true` (`ICO_RD_GLOW_REDRAW=1`), the A/B, whose first presents make no copy. The environment switch
decides when it is set to `1` or `0` (`0` holds the glow under a Smooth row); otherwise the row
decides, read at every present. The depth of field's copies leave column 0 of WORK0 and WORK1
undrawn ([The glow chain at a wide frame](#the-glow-chain-at-a-wide-frame)): at a held present it
reads the chain's column 0 of the tick's first present there, part of the held glow. That is the
general rule's one known case: any write into one of the chain's buffers counts as covering it,
because the scan cannot decide which texels a draw covers or a sampling draw reads (both follow
from the replay's transforms and UVs), so a partial write followed by a wider read reads, outside
the write, what earlier presents left there: the same bytes as a redraw on a still tick, the
tick's first present's during a pan, as the held glow itself. An exact rule would draw the chain
at every present with the depth of field. The still
re-present ([DISPLAY.md](DISPLAY.md#interpolation-between-ticks)) hashes the frame before this edit:
two presents of a tick whose frames hash alike paste the same glow, held or drawn by the first.
Event lines: the 10 s summary's

```
rd: interp: glow between ticks: held: N later presents pasted the tick's glow without its chain; the chain drawn at A first presents, B later ones with [dev] glow_redraw, F later ones with Glow between frames: Smooth, C without this tick's copy, D whose other draws read its buffers; E presents without a glow
```

and the `window:` uploaded-KB line's `; N between-tick pictures pasted the tick's glow without its
chain ([dev] glow_redraw)`. ctest `rd_replay_tool_glow_hold` and `build-host/tmp/v094/after/v2-ab.sh`
are the A/B ([TESTING_PROGRAMS.md](TESTING_PROGRAMS.md)).

## The work clear

`ICO_RD_WORK_CLEAR=RRGGBB` in the environment (the game), or `rd_replay_tool --work-clear RRGGBB`,
clears every live target's colour but SCENE, DISPLAY, DISPLAY_HELD, FEED128, FEED_HELD and WORK3_HELD to that
colour with alpha 0x80 at the head of every replay, after `rd__clear_new_targets`
(`port/render/rd_upload.c` (`workClear`); `rd__set_work_clear` overrides the environment). A texel
the frame reads before drawing it then carries the colour downstream: in a target (`--target
NAME`), in a pass's picture (`--pass-dump DIR`), or on screen. The depth and stencil stay. It costs
one clear pass a target a frame, so it is a diagnostic only. One line at its first replay:

```
rd: work clear: every target but SCENE, DISPLAY and the feedback kept across frames (DISPLAY_HELD, FEED128, FEED_HELD, WORK3_HELD) cleared to FF00FF before every frame (ICO_RD_WORK_CLEAR, a diagnostic): 13 named (SHADOW0 SHADOW1 ... WORK2_PAD) and N temporaries; a texel a frame reads before drawing it shows that colour
```

A replay with the work clear against the same replay without it differs exactly where the frame
reads a texel it did not draw. ctest `rd_blur`'s `glow_edge_wide` case runs
the flare at 512, 848 and 2048 wide over magenta and green work buffers: SCENE is the same bytes
both ways with the strip, and differs in the left eighth of the frame without it.

## The ripples between ticks

The drained room's ripple rings (issue 106) are strips projected on the CPU through the frame camera
(`ico2/sugipon/src/puddle.c` (`drawRipple`); `gif_DrawStripFST`), and their STs into the puddle's
reflection block are their projected points scaled: S = (x - 2048) 0.9 / width + 1.5 and T = (y -
2048) 0.9 / height + 1.5 a pixel, with Q 1; the outer ring's STs come from a rotated spoke point, so
they are not a function of the ring's own position. Between ticks the presenter re-projects each
vertex through the blended camera ([RENDER.md](RENDER.md#camera-blending)) while the block's
reflection, drawn under its pushed camera, moves with that camera too. With the tick's STs kept, the
rings sampled the reflection where the tick had put them, about half a tick's motion off at the
half-tick presents, all rings at once.

`drawRipples` sets `gif_HostFrameProjectedSt(0.9 / width / 16, 0.9 / height / 16)` (the scale a 12.4
unit) around its strips; the decoder ends a batch when it changes and passes it to
`rd_frame_projected_st` at the flush, and `rd_screen_prims` records it in the frame-projected draw
(RDC_SCREEN `b[5]` `RD_SCREEN_ST_SHIFT`, `f[0]`, `f[1]`; `port/render/rd_core.c`). Where the
presenter re-projects such a draw's vertex, after `reprojStq`, it adds the vertex's own move from
the tick's X and Y times the scale to S / Q and T / Q (`port/render/rd_interp.c` (`reprojStShift`):
`s += dX f[0] q`, `t += dY f[1] q`), in the draws that are the tick's alone and in matched pairs
(dX from cur's X). The STs are only shifted, never made again from XY. Dumps from before it have
`b[5]` 0 and replay as they did. Measured on the v13782 dump (`--list`'s vertices read from the
file): dS/dX = 8.1758e-05 = 0.9 / 688 / 16 and dT/dY = 1.25555e-04 = 0.9 / 448 / 16, both
positive, the inner ring's vertices within one 12.4 unit of the formula, the outer ring's up to 208
off it.

`rd_replay_tool --list` shows ` st-shift (dsx,dsy)` on such a draw; `--interp` counts the
re-projected draws with their texture shifted, and the 10 s `interp:` summary's fourth line ends
with "N re-projected draws with their texture shifted (the ripples)". ctest `rd_interp_test`'s
`testRippleSt` (the vertices: S / Q and T / Q the tick's plus the move times the scale, the tick's
without the shift, a strip under a pushed camera unmarked) and `testRippleStPixels` (on a device:
every strip pixel samples a 256 x 256 gradient where it stands within one level half way through a
pan, more than two levels off without the shift) pin it.

## Screen sprites redrawn under a new key between ticks

A screen prim (RDC_SCREEN) of one tick only, with no match in the other tick, fades with t when its
blend fades with its vertex alpha (`alphaFades`: C = As, D = Cd, ABE on, PABE off), the tick's in
and the tick before's out ([RENDER.md](RENDER.md#interpolation-and-presentation),
`port/render/rd_interp.c` (`unmatchedPass`)). The load screen's black bars are the game's own
640-wide sprites, one pair per layout (`ico2/common/src/layout_texture.c`: grid lines 0 and 200,
640 x 70 and 640 x 60, texProperty 178/179 to 431/432 in layouts 17 to 64), keyed by the layout's row
(`LT_HOST_KEY(e, 0)`). When the screen changes layout, the old bars exist in the tick before only
and the new ones in the tick only: the same sprites under other keys. Cross-faded, two identical
opaque bars cover 1 - t(1 - t) of the picture beneath, 0.75 at t = 0.5, so a quarter of it
showed through for a present or two at each change of layout and before a save loaded, at any
frame rate but Original (issue 139).

Before the fade, `pairSame` pairs each fading screen prim of the tick only with one of the tick
before only that is the same draw: the same list, the same command but for its key and payload
offset, every vertex the same in every byte (X, Y, Z, STQ, RGBA) and the same state block at the
draw (`stateDiff` has nothing to emit either way: texture, blend and its FIX, tests, scissor). The
tick's is drawn whole and the tick before's is not drawn; each draw pairs once at most; a hash of
(list, command, vertices) over the tick before's candidates and one probe for each of the tick's
keeps it linear. Everything that does not pair fades as before, and a prim that does not fade (the
switch at t = 0.5) is not paired: it is already drawn whole on either side. No game code changes.

The 10 s `interp:` summary's third line counts them after the draws not placed: "N identical
screen prims paired across a layout change (drawn whole)" (`RdInterpStats.screenPaired`), and they
are not counted in its "faded in" and "faded out". ctest `rd_interp_test`'s `testBars`
(`port/render/test/rd_interp_bars_test.inc`) pins it: two ticks whose top and bottom bars carry
different keys and the same geometry draw the tick's bars once each at full alpha at t = 0.25 and
0.5 and the tick before's not at all, 2 pairs; with the tick's bars 4 pixels taller, or drawn with
another blend FIX, nothing pairs and both fade (0x40 each half way). `testUnmatched`'s fading
sprites differ in colour between the ticks, so they still cross-fade.

## The crack fill after lists 4-6

The crack filler is described in [RENDER_POST.md](RENDER_POST.md#crack-filling-above-1x). SCENE's own fog
arms it (`rd__crack_fill` from `doFog`) and the walk draws it from the start of list 7 on, the
same replay only, where `rd__crack_fill_place` puts it (`rd__crack_fill_at` before each command of
the list loop in `port/render/rd_replay.c`: after the glow's `cleanUpFB` with RGB alone, or as the
first draw of the aura's paste pass, else before the first command that may read or write SCENE;
[RENDER_POST.md](RENDER_POST.md#the-fills-place-after-list-7)): the snapshot of SCENE's colour
(`rd__replay_take_snap`) ends the pass open there,
SCENE's depth goes to `RHI_STATE_DEPTH_READ` and is sampled where it is, and the fill's own pass
binds SCENE's colour alone, loaded and stored (`fillWork`); the next pass that binds the depth
takes it back to DEPTH_WRITE. So nothing breaks the scene pass at the fog: on the fog's local read
(`RD_FOG_LOCAL`: a tile-based GPU, and desktop Vulkan where its probe kept the local reads,
[below](#the-desktops-local-read-and-its-probe)) the fog and lists 4-6 draw in the scene pass the
draws before them opened (and the glow's `cleanUpFB` after them), and the pass ends where the
glow's, the depth of field's or the aura's passes end it anyway; in the aura's rooms the fill is
drawn in the paste's pass, no pass of its own; on the other paths the fog's own pass and lists 4-6's are as without
the fill. Against the fill before the fog (which ended the scene pass for copies of SCENE's colour
and depth and began it again with both loaded, then dropped the depth's store at its end): one copy
fewer (the depth's, 8 bytes a texel), the store and load of colour and depth at the break gone, the
fill's pass on the colour alone, and SCENE's depth stored at the scene pass's end for the fill's
read (the store plan's `PlanDepth.late`, [below](#the-store-plan)): about 40 to 60 MB a replay less
at 2366 x 1080. The fill's GPU time, the colour's copy and its pass, is the picture effect `crack
fill` (`RD_PERF_POST_CRACK`): the 10 s `effects:` line, the perf CSV's `gpu_crack_fill_ms` and the
tool's `gpu:` line. The picture differs from the fill before the fog at the cracks only (the bound:
[RENDER_POST.md](RENDER_POST.md#crack-filling-above-1x), [DIVERGENCES.md](DIVERGENCES.md) VD-20). Tests:
`rd_crack_tiler`, `rd_crack_tiler_on`, `rd_crack_tiler_fold`, `rd_crack_tiler_fold_mali` (the
local read: one pass more than without the filler, the fill's own; SCENE equal to the depth copy
path's byte for byte), `rd_crack_glow` and `rd_crack_aura` (the place: SCENE the bytes of the fill
at the start of list 7, two and one passes fewer), `rd_crack_late_ab`, `rd_glow_cheap_ab`
([TESTING_PROGRAMS.md](TESTING_PROGRAMS.md)).

At a reach of 1 texel on both axes (2x on a whole scale) the fill draws `crack_fill1_ps`
(`CRACK_REACH_1`, selected in `rd_crack_fill.c`): the four neighbours are read first and a texel
with no pair of them in front of it along an axis is left at once. A run of one texel has the
immediate neighbours for its ends, so the decision is the general one's with `l = r = u = d = 1`
and the bytes are the same; 5 depth loads a texel against 13 for `crack_fill_ps`, and a texel with
no pair in front (every plain surface texel) reads nothing more. The general shader keeps the full
loop: a texel with no neighbour in front at distance 1 can still be a crack of 2 or 3 texels, so
the early exit does not hold at a reach above 1. `rd_crack_test` replays the frame with
`rd__crack_fill_force_general` and compares every byte of colour and depth.

## The desktop's local read and its probe

The three local-read paths (the fog's depth read inside the scene pass, `fogLocal`; DATE through
the stencil bit, `dateStencil`; the motion blur's and the other post sprites' destination read,
`fxDstLocal`; [RENDER.md](RENDER.md#the-device-profile)) draw the same bytes as the snapshot paths:
their self-tests check it on the device at start (`rd_date_selftest.c`, `rd_fog_path.c`,
`rd_fx_plan.c`); the A/B of `build-host/tmp/v0710/x2-ab.sh` (lavapipe, the desktop class, the
fog's and DATE's paths forced by their `[dev]` keys) found every target of three dumps at 1x and 2x
identical but the DATE snapshot, which the stencil path does not make; and `rd_blur_test`'s case l
draws the motion blur through the local read on lavapipe with every target equal to the CPU model.
On an immediate-mode GPU they save, a picture: the three R8 DATE snapshot
passes and their pass breaks, the fog's two depth transitions and two pass breaks, the motion
blur's copy of SCENE (4.9 MB at 2x), and keep the fog and lists 4-6 in the scene pass. What they may
cost there: SCENE's colour and depth are made with the input-attachment usage and sit in the
local-read layout (`VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR`, `port/rhi/vk/vk_cmd.c`
(`rhi_cmd_begin_render_pass`)) for the whole scene pass, which a driver may answer by giving up the
colour and depth compression of lists 0-2. Which is larger on a given GPU is not known, so the
desktop class decides it on the device.

**The default.** `rd__profile_init` (`port/render/rd_profile.c`) turns `fogLocal`, `dateStencil`
and `fxDstLocal` on for the desktop Vulkan class (not a tile-based GPU, not MoltenVK) where the
device has local read (`RhiLimits.localRead`: `VK_KHR_dynamic_rendering_local_read`, or Vulkan
1.4's feature) and `rd_init` was given a window (`g_rd.windowed`: the game). D3D12, D3D11 and
OpenGL have no local read and keep the snapshots. A headless run (the replay tool, the tests) keeps
the snapshots unless `ICO_RD_LOCAL_PROBE` is set: `1` the game's default and its probe (where the
self-tests run: `rd_fog_selftest`), `0` the local reads without the probe. The `[dev]` keys
`fog_path`, `date_path` and `fx_dst_local` still set each flag; one of them set leaves the probe
out (`desktop local read on ([dev])` or `off ([dev])`).

**The probe** (`port/render/rd_local_probe.c` (`rd__local_probe`)), once a renderer, after the
first run of the self-tests (`rd_fog_selftest`: DATE's, the fx local read's, the fog's), where
the flags they left on include one of the three. A frame of its own, never recorded or presented,
on SCENE at its real size and scale: list 0 clears SCENE and draws 8 full-frame layers of 16 x 16
untextured sprites, each layer nearer than the last (Z test GEQUAL, Z written), half the cells
with alpha 0x80; list 3 three full-frame DATE sprites (DATM 1, 0, 1); list 4 the fog (`RD_POST_FOG`
with a table, as `ZFog.c` records it); list 7 the motion blur's sprite (ALPHA LERP FIX 0x20 over
the whole frame). One untimed replay first (the pipelines made, the GPU awake), then blocks of two
replays with the local reads (the flags and the fog's path as the self-tests left them) and two
through the snapshots (the three flags off, the fog through the path the desktop order tries first,
in place where the depth is sampled), SCENE's textures made again before each block
(`rd__scene_textures_remake`: the input-attachment usage follows the flags), a second round only
while the first took under 20 ms of the CPU's clock. MSAA is held off (the local reads never draw
inside the MSAA epoch); the fog's own probes do not take these replays (`rd__fog_timing`), and the
fog draws as in a game frame (the crack fill after lists 4-6 above 1x under Enhanced). Each replay's GPU
time is its record's (`RdPerfRecord.gpuMs`, the first timestamp to the last list's), read once two
more replays (a clear of SCENE: `RHI_FRAMES_IN_FLIGHT`) have brought the stamps in
(`rd__perf_gpu_ms_of`); the least of each way counts.

**The verdict** (`rd__local_probe_keep`): the local reads stay unless their least time is more than
5 % (`RD_LOCAL_PROBE_TOLERANCE`) above the snapshots'. Off: the three flags go off, each with
`rd: profile: <flag> off (probe)` and `(probe)` after it in the profile's words
(`rd__profile_probe_off`), every named target is made again without the usages
(`rd__targets_remake`), and the fog's self-test runs again in the desktop order (in place first),
so the depth path in force is one that passed. Without GPU timestamps (or with `[dev]
perf_list_stamps = false`) the probe does not run and the local reads stay. The line:

```
rd: profile: local read on the desktop: kept (probe 0.412 vs 0.530 ms): the local reads vs the snapshots, the least of 4 and 4 replays of its frame on SCENE 688x448 at 1376x896 texels; kept unless more than 5 % slower; 9.8 ms
rd: profile: local read on the desktop: off (probe: slower, 0.611 vs 0.530 ms): ...
rd: profile: local read on the desktop: kept (not probed: no GPU timestamps)
```

and the profile's start line and the `ico_pc: summary:` line end with the desktop's words
(`; desktop local read on (the probe follows the self-tests)` at start, `, desktop local read kept
(probe 0.412 vs 0.530 ms)` in the summary; `off (no window: ...)` in a headless run). The numbers
are the probe frame's, not a game picture's: they say which way is cheaper on that GPU, not by how
much a picture gains. The snapshot pipelines the start-up set left out (it was gathered with the
local reads on) are made at the probe's first snapshot replay; the probe's wall time in the line
includes them.

**Tests.** `rd_local_probe_rule` (`rd_local_probe_test`: the verdict on synthetic times, no
device); `rd_local_probe_kept` and `rd_local_probe_off` (lavapipe through `rd_fog_selftest_test`
with `ICO_RD_LOCAL_PROBE=1`, the local reads' time counted a quarter or four times by
`ICO_RD_LOCAL_PROBE_SABOTAGE=fast|slow`: kept, the fog's path local at both scene sizes; off,
`DATE stencil off (probe)` and the fog in place at both sizes). The real cost on an immediate-mode
GPU is measured by the probe line of a reporter's log; no hardware number exists before it.

## The shadow count in one draw

The shadow volumes count on the stencil of SCENE's depth, bits 0-5 holding n mod 64 for n the net
count of faces in front of the scene ([RENDER_POST.md](RENDER_POST.md#shadows)). The resolve
(`RDC_SHADOW_RESOLVE`, `port/render/rd_replay.c` (`doShadowResolve`)) turned that into the colour
the GS sum leaves in FBP 0x142 with seven full-frame draws on the count target at the scene's
scale, its colour cleared to 0 at the pass's load: draw k (0..5) `blit_ps` with colour 4 << k,
blend ONE + ONE, RGB written, the stencil test EQUAL to 1 << k under read mask 1 << k; draw 6 colour
A = 0x80, A written, the stencil test NOTEQUAL 0 under read mask 0x3F (`rd__shadow_resolve_key`,
`port/render/rd_pipeline.c` (`shadowStencil`)). With m = stencil & 0x3F the count target's texel
is therefore

    R = G = B = 4 m (0..252, the sum of 4 << k over the bits set; never saturating)
    A = 0x80 where m != 0, else 0

Above 1x `shadowReduce` then box-averages the scaled count into `s_shadowRed` at its GS size
(`box_reduce_ps`), which `resolveTexture` hands to the first blur level (the only reader of the
count: the next command reads it as SHADOW0's texture). That average is float arithmetic, not
integer: for output pixel p, lo = floor(p) * f - (ox, 0) and hi = lo + f (f = `DrawCB.param.xy`,
the source texels per pixel, `tc->sx` on x with Full pixel's left-out texels ox in
`DrawCB.scale.z`), the texels [max(floor(lo), 0), min(ceil(hi), size)) visited rows outer and
columns inner, each added as `sum += Load(x, y) * (wx * wy)` with wy = min(hi.y, y + 1) - max(lo.y,
y) and wx likewise, and the pixel `sum / (f.x * f.y)`, stored by the RGBA8 target's float to UNORM
conversion. At scale 2 every weight is 1 and the mean of four multiples of 4 (or of 0 and 0x80) is
a whole number, but at 4x a mean of 16 texels can fall on a half (two texels of 4 among 16 zeros),
so an integer rewrite would not round as the float sum does.

`shadowCountOne` (`RdProfile.shadowOne`; the draw in `port/render/rd_shadow_one.c` (`rd__shadow_one_draw`)) does both in one draw at the GS size: `shadow_count_ps`
(`port/shaders/blit.hlsl`, behind `blit_vs`) loads the stencil of each texel of the pixel's box
through the depth's stencil view (`RHI_ASPECT_STENCIL`, the depth in DEPTH_READ, the channel
`RhiLimits.stencilViewChannel` in `DrawCB.mode.x`), masks it to m and loads the texel m of a 64 x 1
RGBA8_UNORM table holding the bytes above (`rd__shadow_table_texel`, uploaded once). Reading the
table with `Load` gives each term the float the seven draws' count gives `box_reduce_ps` (an
RGBA8_UNORM texel's conversion does not depend on which texture holds it), and the loop is
`box_reduce_ps`'s, the same expressions in the same order over the same `DrawCB` (each term it adds
is `box_reduce_ps`'s term, in its order; the terms of m = 0, exact zeros, are left out: "The pass
and its reads" below); at scale 1
(f = 1) a pixel is its one table texel (v * 1 / 1 = v). So the bytes the levels read are the seven
draws' and the reduction's. At scale 1 it writes the count target itself; above 1x it writes
`s_shadowRed` and leaves the scaled count target undrawn (a per-frame temporary target nothing else
reads). A reset that no volume pass took is recorded first as its stencil clear (`endPass`), so a
frame without volumes reads zeros. Under MSAA the count's pass has already closed the epoch
([MSAA.md](MSAA.md#where-the-epoch-closes)): the stencil is SCENE's single-sampled one, so the one
draw covers every anti-aliasing setting.

**Where the seven draws stay.** A backend without the view (`RhiLimits.stencilView` false, or a
depth-stencil that cannot be sampled, `depthSampled`), `[dev] shadow_one = false`
(`ICO_RD_SHADOW_ONE=0`, the A/B), and a device whose self-test finds the one draw wrong. The
view is optional: a backend that cannot make it for a texture (Vulkan, OpenGL or Direct3D 11; a
MoltenVK stencil-aspect view or an old GL texture-view driver may refuse) keeps the texture with its
depth, logs `texture NAME: the stencil view was not made: no stencil views on this device from now
(the shadow count takes the seven draws)` once (Direct3D 11's line has no texture name) and turns
`RhiLimits.stencilView` off; the next one-draw resolve then turns `shadowOne` off for good, `[dev]`
or not (`profile: shadow count one draw off (self-test)`), and the stencil aspect is never bound
(Vulkan would otherwise sample the all-aspects view of a depth-stencil image). The self-test reports
`not run (the backend could not make a stencil view)`. Direct3D 12 has the view off
(`stencilView = false`, as `uintView`: never run on a Windows machine, and the self-test catches
wrong bytes, not a removed device) until a Windows run of the shadow self-test there; it keeps the
seven draws. The
start-up set holds the seven draws' and the reduction's pipelines as before and the one draw's
where the profile has it. The profile's start line and the summary say which: `shadow count one
draw`, `shadow count seven draws`, with ` ([dev])` or ` (self-test)` after it when that decided;
`rd_replay_tool --stats` prints `shadow count: N in one draw, M in seven draws` for the replay
(`RdPerfRecord.shadowOne`, `shadowSeven`).

**The self-test** (`port/render/rd_shadow_selftest.c`, run with DATE's on the fog self-test's first
run and again when SCENE is made at a new size): a frame of its own on SCENE (its colour as the
count target) counts 71 bands, band k at k in the top half and -k in the bottom half, the edges a
sub-pixel off the pixel grid, resolves it through both paths and compares the count at its GS size
byte for byte, with every band centre against the table (read inside the frame's exact width,
`rd__frame_wexact`: on a cropped frame, 624 wide at 16:10 with 614.40 exact, the last band's centre
fell in the left-out columns and turned a correct one draw off; a band with no column inside is not
read). The one draw is replayed twice, its
stencil read by gathers and then by loads; the gathers stay where their count is the seven draws',
else the loads where theirs is (the result is then the loads'), else the seven draws:

    rd: shadow count: one draw (self-test: equal to the seven draws over 688x448, 142 band centres ok) on SCENE 688x448 at 1376x896 texels, step 4
    rd: shadow count: the stencil read four texels a gather, equal to the seven draws byte for byte
    rd: shadow count: the stencil read a texel a load, as the gathers' count differs at 17 of 308224 texels, the first (40,300)
    rd: shadow count: seven draws (self-test: one draw differs at 17 of 308224 texels, the first (40,300): 00040480 for 04040480) on SCENE 688x448 at 1376x896 texels, step 4

A difference turns `shadowOne` off (`(self-test)`); `[dev] shadow_one = true` keeps it ("kept by
[dev] shadow_one"); `ICO_RD_SHADOW_SABOTAGE=1` makes the table wrong at m = 1 so the failure path
runs (ctest `rd_shadow_selftest_sabotage`). The self-test resolves under the running disc's step
(below).

**The pass and its reads.** The one draw's pass has one attachment, its destination at the
GS size (848 x 448 on the phone, 1.4 MB), DONT_CARE at the load (every texel is written) and
stored; no depth attachment: SCENE's depth is in DEPTH_READ and its stencil is a sampled texture
(the view), so a tile-based GPU loads and stores nothing of the 2364 x 1080 depth for it. Per
output pixel the box covers ceil(hi) - floor(lo) texels an axis, about f + 1 (3 or 4 at the
phone's 2.788 x 2.411: about 12.9 texels a pixel, 4.9 M stencil reads a picture, each of the
2.55 M stencil texels read about twice). The loop reads fewer of its table, the same sum:

- a texel whose m is 0 adds nothing: the table's texel 0 is 0 0 0 0 for both steps
  (`rd__shadow_table_texel`), so its term is +0.0, and sum + 0.0 is sum whether the driver fuses
  the multiply and add or not (sum starts at +0.0, every term is a product of non-negative
  values, so it never holds -0.0). Neither the table texel nor the column weight is computed for
  it;
- the table texel of a run of equal m is read once and held (`held`): the same texel of a
  texture nothing writes during the pass, so the same float4.

So every term that is added is the term `box_reduce_ps` adds, in its order, and the bytes are
unchanged; the reads per pixel go from about 26 (a stencil texel and its dependent table texel
each) to about 13 stencil reads and one table read per run of a non-zero m (none outside the
shadows). The self-test's sabotage (m = 1) still shows: a non-zero m is always read from the table.

The stencil is read four texels a gather (`shadow_count_gather`, `DrawCB.mode.y` 0): one
`GatherRed` (or `GatherGreen`, the view's channel) at the corner the 2x2 texels (x, y) to (x + 1,
y + 1) share, half a texel from any other footprint, so the four texels whatever the coordinate's
rounding (the sampler nearest and clamped; a texel past the stencil's edge is never used: the box
stops at it), as `font.hlsl`'s `sheet_cov4`. Two rows' m are gathered into registers (at most
`SHADOW_GATHER_SPAN`, 8, a row: the box is 4 texels at 2.8125x and 6 at the phone's 4.62x; a wider
box, above about 7x, takes the loads), then row y's terms are added left to right and then row y +
1's: the texels, the terms and their order are the loads', so the bytes are. At the phone's 2.788 x
2.411 that is about 4 gathers a pixel for about 13 loads (a box of 3 or 4 texels an axis: two
rows a gather, two columns a gather). A driver whose gathers of a stencil view are wrong is caught
by the self-test (below): its one draw then reads a texel a load (`g_rd.shadowLoad`,
`DrawCB.mode.y` 1), and the seven draws stay only where the loads are wrong too. Where the loads
are read, the draw binds `shadow_count_load_ps` (the entry built with `SHADOW_COUNT_LOAD`: the
loads alone, no gather in the module; `rd__shadow_count_loads`). A backend whose stencil view
takes no gather (`RhiLimits.stencilGather` false: Direct3D, whose `X32_TYPELESS_G8X24_UINT` has no
shader-gather support; the D3D11 debug layer stops a draw whose pixel shader merely holds such a
gather, error 416, on the WARP run of `rd_msaa`) always reads a texel a load, and its
self-test skips the gathers' replay:

    rd: shadow count: the stencil read a texel a load, as the backend's stencil view takes no gather `rd_replay_tool
--stats` ends the one draw's clause with `four texels a gather` or `a texel a load`
(`RdPerfRecord.shadowGathered`).

The pass list names it `shadow count 848x448` (`RD_PERF_ID_SHADOW_COUNT`: the label `shadow`, the
target `count`, [RENDER.md](RENDER.md); not the count target's `list 3 temporary`); the seven
draws' reduction above 1x is named the same. `rd_replay_tool --stats` prints, after the counts:

    shadow count: 1 in one draw, 0 in seven draws (profile one draw); the one draw's pass: colour 848x448 not loaded, stored (1.4 MB), no depth attachment; the stencil read as a texture, 2364x1080 (2.4 MB), four texels a gather; the count target's colour neither loaded nor stored in 1 passes (2364x1080, 9.7 MB each way)

(`RdPerfRecord.shadowOneW`, `shadowOneH`, `shadowReadW`, `shadowReadH`, `shadowColorDropped`,
`shadowColorW`, `shadowColorH`).

**Where the phone's 3.2 ms goes (not measured).** The PowerVR phone's log (stage 6, Resolution
Auto) has the
pass at 2.7-3.2 ms in every 10 s block while the scene pass after it (`list 3 SCENE`, the
composites) is 2.5-3.1 ms; lavapipe's replay of the frame has it at 16.0 ms against 1.3 ms for
SHADOW0's one draw of a quarter of the texels. Two parts, neither isolated on the device: the
shader's reads (above: about 26 a pixel without the two cuts, a dependent pair each), and the tile
work a tile-based GPU attributes to the next timed pass ("the last scene pass carries the earlier
passes' tile work"): the volumes' pass before it loads and stores the count target's 2364 x 1080
colour (9.7 MB each way, nothing read: next section) and SCENE's depth and stencil, and its stores
end inside the count pass's stamps. Both cuts are byte-identical; their share of the 3.2 ms is for
the phone's next log to say.

## The shadow count's colour

The count target (a temporary at the scene's scale, 2364 x 1080 on the phone) is the colour of the
volumes' pass, and of the reset's own pass in a frame whose reset no volume pass took (`stencil
clear` in the pass list). Its colour from the reset to the resolve is never read:

- the volumes write only the stencil (`rd__shadow_volume_key`: colour mask 0);
- the seven draws' pass clears the colour at its load (`doShadowResolve`: `RHI_LOAD_CLEAR`);
- the one draw at scale 1 writes every texel of it (its load DONT_CARE); above 1x it leaves it
  undrawn, and every texture read of the count target until the next reset takes the reduced
  count instead (`resolveTexture`, `s_shadowRedFor`; `rd__shadow_count_read` reads the reduced
  count too);
- Shadow.c is its one user (`rd_shadow_count_target`: the reset, the volumes, the resolve and the
  chain's level 0 after it), the premise the one draw already rests on.

So, under the store plan, a pass on the count target while the stencil window of its depth is
open (from the reset on; the resolve closes it) leaves the colour neither loaded nor stored when
it would have loaded it (`port/render/rd_store_plan.c` (`rd__store_plan_count_color`), called from
`beginPass` and `flushStencilClear`): `RHI_LOAD_DONT_CARE` and `RHI_STORE_DONT_CARE`. A colour the
pass clears (a pending clear, the seven draws' pass) keeps its ops, and so does a named target as
the count (the self-test's SCENE). On a tile-based GPU that is the count target's 9.7 MB read into
the tiles and 9.7 MB written back at each such pass, a replay; the new-target clear before the
walk wrote it once more (`rd__clear_new_targets`, the pool's rule for every temporary), which the
tile-based classes now leave undrawn where nothing reads it before its resolve ([the next
section](#the-shadow-volumes-in-the-scenes-pass)). `[dev] store_plan = false` keeps every colour as before (the A/B). The counts:
`storeColorDropped` (the window's `; stores: N depth and M colour stores dropped ...`, now about
1 a replay in gameplay) and `shadowColorDropped`; the log says once

    rd: store plan: the shadow count's colour (2364x1080, 9.7 MB) is neither loaded nor stored while the count is made (the volumes write only the stencil; the resolve writes the colour, or the reduced count stands for it above 1x); reported once

With `ICO_RD_STORE_POISON=1` the dropped load clears the colour to 0x5A instead (lavapipe keeps
an attachment's contents whatever its ops say), so a reader would show it: `rd_shadow_test` case p
(the count, the levels and SCENE byte for byte with the plan off and on, through both paths, at
1x, 2x and 1931 x 1093 texels; above 1x on the one draw the count target holds the poison at every
texel) and `rd_store_plan_ab` (every named target byte for byte over the corpus, the colour stores
dropped the same under the plan and the wrong plan).

## The shadow volumes in the scene's pass

The reset ended the scene's pass and the volumes drew in a pass of their own, the count target
with SCENE's depth: on a tile-based GPU that pass loaded SCENE's depth into its tiles and stored
it again with the stencil, after list 0's pass had stored the same depth (the PowerVR phone's
log of 2026-11-02, the heavy stage 6 room: `list 3 temporary 2366x1080 8 draws 1.18 ms`, about 23 MB a
replay). Now, on the tile-based classes (`RdProfile.shadowMerge`), the volumes are drawn in the
scene's pass that is open at the reset, when that pass began with the stencil cleared to 0 (the
frame's clear of the depth taken as its load op) (`port/render/rd_shadow_merge.c`, header
comment):

- **The arm.** `beginPass` (after the pass area) hands every pass to `rd__shadow_merge_arm`. A
  pass on a target with its own depth whose stencil load is a clear to 0, with a shadow reset on
  that depth later in the replay (the store plan's scan notes the resets,
  `rd__shadow_merge_scan_reset`), single-sampled, without an area, outside a pass window, the
  exact blend, GS state as shader data and the VU pass split, stores its stencil (2.6 MB at the
  phone's size; list 0's pass stored none before).
- **The take.** `doShadowReset` asks `rd__shadow_merge_take`: the reset the pass was armed for,
  that pass still open, no clear or reset pending, not a storage or MSAA pass, the count target of
  the pass target's size, scale and format. Then the reset opens the stencil window and records
  nothing: the stencil the pass cleared stands for the reset's clear.
- **The volumes.** `doShadowStrip` draws each strip on the open pass's colour target
  (`rd__shadow_merge_color`), which it never writes (`rd__shadow_volume_key`: colour mask 0),
  with the same pipeline but for that attachment's format (the same RGBA8), FrameCB and scissor
  (the same geometry). A strip under DATE or sampling a target, or a pass ended among the
  strips, sends the rest to the volumes' own pass, whose stencil is loaded (the window is open):
  what the armed pass stored.
- **The resolve** ends the pass before it reads the stencil, as before (`rd__shadow_one_draw`,
  or the seven draws' pass).

Why the same bytes: the stencil's bits 0-5 are what the reset would have left (cleared to 0 when
the pass began; the only pipelines that write those bits are the volumes', `shadowStencil`, and a
reset window on the depth before this one would have ended the pass at its resolve; DATE's set
and clear write bit 6 alone, which neither the count nor the seven draws read, and `INCR_WRAP` /
`DECR_WRAP` under the write mask 0x3F give the same bits 0-5 whatever bit 6 holds); the depth is
the same values tested the same way (stored and loaded back bit for bit before); SCENE's colour is
never written and the count target's is not touched, whose old pass contents nothing read (the
section above). The draws are the same commands in the same order. A pass armed for a reset it did
not reach stored its stencil for nothing: counted, and logged with why.

**The count target's new-target clear.** The count target is a pooled temporary taken anew each
frame, so `rd__clear_new_targets` cleared its colour before the walk (9.7 MB written at the
phone's size, in the `uploads` piece). The same scan notes, for every temporary a shadow command
draws on, whether its first touch in the replay is a reset and whether any other command (a draw
on it or on its depth, a texture naming it, a copy, a clear) touches it before its first resolve
(`rd__shadow_merge_scan_cmd`). Where the first is a reset, nothing else touches it before the
resolve and the resolve comes, the clear is left undrawn (`rd__shadow_count_clear`, the merge's
switch): the volumes never write its colour, the seven draws clear it at their load, the one draw
at scale 1 writes every texel, and above 1x every read of it after the resolve takes the reduced
count. Under `ICO_RD_STORE_POISON=1` it is cleared to 0x5A instead, so a reader would show
(`rd_shadow_merge_ab`, `rd_shadow` case m). Counted in `RdPerfRecord.shadowClearSkipped`; once:
`rd: shadow volumes: the count target's clear as a new target (2364x1080, 9.7 MB) not drawn: ...
(reported once)`.

The lines: `rd: shadow volumes: drawn in the scene's open pass where it cleared the stencil (...)`
or `... in a pass of their own (...)` at start; `rd: shadow volumes: drawn in SCENE's pass (frame
F, command N): ... (reported once)` at the first; `rd: shadow volumes: in a pass of their own in
frame F: <why> (the first 8, then every 100th: N so far)` for an armed pass not taken; the window's
10 s line `; shadow volumes in the scene's pass: M of R counts, A passes armed, C count clears
not drawn a replay` (`RdPerfRecord.shadowMerged`, `shadowMergeArmed`, `shadowClearSkipped`); the
replay tool's `--stats` `shadow volumes in the scene's pass on|off: M of R counts, A passes
armed; C count targets' clears not drawn`. There is no setting: on the
immediate-mode classes the volumes keep their own pass (it moves nothing through tiles);
`ICO_RD_SHADOW_MERGE=0|1` is the tests' way. The tests: `rd_shadow_test` case m (the count, the
levels and SCENE byte for byte off and on, one render pass fewer) and `rd_shadow_merge_ab` (every
pass dump of the corpus and the phone's frames byte for byte, one pass fewer a reset taken).

## The shadow count's step

The European, Japanese, Korean and Chinese discs draw the volume faces with the colours 0x04 and 0xFC;
the North American disc and the 2001 previews (August 6 and June 26) with 0x08 and 0xF8
(`ico2/seki/src/Shadow.c` `emitVolumeStrip` and `emitVolumeStrip_jun26`,
`PAL_NTSCAUG6JUN26`). So the GS leaves 4 n mod 256 on the first group and 8 n mod 256 = 8 (n mod
32) on the second: the step, 4 or 8 (issue 159: earlier the host hook `shadowHostStripPos`
compared the face byte with 0x04 only, so every face of the second group counted as a decrement
and the resolve wrote a near-white count, which the composite turned into a light magenta patch on
the green-grey floors).

- **Recording.** `shadowHostStripPos`'s sign compares the face byte with the disc's own front
  colour (`PAL_NTSCAUG6JUN26(0x04, 0x08)`); `shadowHostReset` calls
  `rd_shadow_reset_step(PAL_NTSCAUG6JUN26(4, 8))`. The reset and the resolve after it carry the
  step in `b[0]`: 8, or 0 for the step 4, so the European disc's commands, its dumps and every
  older dump keep their bytes (`rd__shadow_step` reads it). A dump replays with its disc's step on
  any machine; `rd_replay_tool --list` prints `step N` on both commands and `tris +1 A -1 B` on
  each volume command. A North American dump recorded before the fix has `+1 0` on every volume
  command: its signs were lost at recording, and no replay can bring its shadow back.
- **The volumes** are the same draws for both steps (write mask 0x3F): the stencil's n mod 64
  holds n mod 32 in its low five bits, 32 dividing 64.
- **The seven draws** (`rd__shadow_resolve_pass`, `port/render/rd_shadow_one.c`): step 4, passes
  k = 0..5 add 4 << k, the A pass tests the six bits (`RD_STENCIL_TEST_NONZERO`), the bytes as
  before; step 8, passes k = 0..4 add 8 << k (at most 248), bit 5 has no pass (8 << 5 is 256, 0 in
  eight bits), and the A pass tests the five bits (`RD_STENCIL_TEST_NONZERO5`, read mask 0x1F), so
  n = 32 is no shadow, as on the GS. The bit passes' keys are the same for both steps (the colour is
  the `DrawCB`'s); the step 8 adds one key, its A pass, which the start-up set holds only when the
  running disc counts by 8 (`rd__shadow_disc_step`): the European set keeps its count.
- **The one draw** reads a table of its step (`rd__shadow_table_texel(m, step)`: under the step 8
  RGB 8 (m mod 32), A 0x80 where m mod 32 is not 0), one 64 x 1 texture each, made at its first use;
  `shadow_count_ps` is unchanged.
- **The log.** The first reset of a run logs `shadow count: step 8 (the disc's face colours 0x08 /
  0xF8), five bits` (or `step 4 ... six bits`), again only if the step changes; the self-test's line
  ends with `step N`.
- **Tests.** `rd_shadow_test` case 8: Shadow.c's model recorded under the North American disc
  (colours 0x08 / 0xF8 in the packet, the recorded signs the packet's with both present, `b[0]` 8 on
  the reset and the resolve; the European recordings `b[0]` 0), the step 8's table for n = 0, 1, 31,
  32, 33, 63, 64, 255, -1, -33, and on the device the synthetic volumes' count against 8 n mod 256
  through the seven draws and the one draw, the one draw against the seven draws, every pipeline
  created enumerated.

## The shadow edges above 1x

`shadow_Draw` composites the three blur levels into SCENE in list 3, SHADOW2, SHADOW1 and SHADOW0
in that order (the v4251 frame: commands 3:42, 3:47 and 3:52), each a sprite over the whole frame,
LERP by the texel's alpha (ALPHA blend 4), the alpha test GREATER 0 and DATE against the receiver
mask ([RENDER_POST.md](RENDER_POST.md#shadows)). A level texel covers 2, 4 or 8 GS pixels on each axis
(SHADOW0 is W / 2 x 256 for a frame of W x 448, `namedTargetDesc`), and the levels stay at scale 1
at every scale ([RENDER_POST.md](RENDER_POST.md#above-1x), "Left as they are"). On a SCENE at scale s each
composite therefore steps 1 / (2 s), 1 / (4 s) or 1 / (8 s) level texels a texel, under half a
texel on both axes whenever s > 1.

Flagged `DF_ABOVE1X`, `sprite_above1x` takes the cubic B-spline for them as four bilinear taps
(`sprite_bspline4`): about 268 lane-ops a pixel against about 103 for the plain path (the pixel
diet's DXC walk of the composite), on three full-frame draws. `rd__sprite_above1x`
(`port/render/rd_fx_plan.c`, which `fillDrawCB` in `rd_replay.c` calls with the shadow count's
GS-size texture) leaves a pass unflagged when its texture
is a shadow level (`rd__shadow_level`, `port/render/rd_core.c`) and `RdProfile.shadowBspline` is
off, the default on every class. `sprite_ps` then samples as at 1x: `gs_sample`, one hardware
bilinear tap at the texel's interpolated UV, which `setUvShift` places at the GS's coordinate of
the texel's centre. TFX, TEXA, the alpha test, the blend and DATE are the same code either way, in
one shader; the work buffers' draws onto the frame are sampled the same way
(`rd__work_class`). Every other `DF_ABOVE1X` pass keeps its kernel: the softening's SCENE into AA0
(the box), the reduction's fallback sprite, and a sprite that magnifies another scale-1 target that
is not a work buffer more than twofold (a render-to-texture block at its GS size, from 3x).

**What changes on screen.** Only the penumbra. Where a level is constant over the two kernels'
joint support (level texels i0 - 1 to i0 + 2 about the sample point, i0 = floor(u - 0.5)) both give
the same bytes; inside it the bilinear is a little sharper (a level texel's spread has a standard
deviation of 0.41 texel against the B-spline's 0.58) and its slope changes at each level texel, so
along a diagonal shadow edge the steps of the level grid (4, 8 or 16 texels a level texel at 2x for SHADOW0..2)
can show where the B-spline rounds them off. The expected size is up to about 10 to 15 levels of a
channel along the edges, none elsewhere; at 1x nothing changes (no pass is flagged at scale 1).
The orchestrator's A/B measures it on the dump corpus at Enhanced 2x (`rd_replay_tool --pass-dump`
with `ICO_RD_SHADOW_BSPLINE=1` against the default); whether it is visible decides an Options row.

**The switch and the lines.** `[dev] shadow_bspline = true` (`ICO_RD_SHADOW_BSPLINE=1`,
[CONFIG.md](CONFIG.md)) sets `shadowBspline` and the composites take the B-spline again (the A/B,
[DIVERGENCES.md](DIVERGENCES.md#video) VD-18 and VD-24). The profile's start line and the summary
say `shadow edges: bilinear` or `shadow edges: b-spline ([dev])`; the first composite that takes the
bilinear above 1x logs once

    rd: shadow edges: the composites above 1x take one bilinear tap (SHADOW2 at scale 1 into a target at scale 2x2, as at 1x); [dev] shadow_bspline = true draws the cubic B-spline (reported once)

and `rd_replay_tool --stats`' `draws resampled above 1x` does not count the composites
(`RdPerfRecord.a1xDraws`).

**Tests.** `rd_shadow` (`port/render/test/rd_shadow_test.c`, case s) draws each composite alone at
scale 4: by default not flagged and within 3 levels of the float bilinear of the level at each
texel's UV, then with `shadowBspline` flagged and within 3 (4 on the fixed-function blender) of
`above1x_ref.c`'s B-spline. `rd_shadow_edges` (`rd_shadow_test edges`, case x) records the same
frames at scale 2 (the synthetic volumes, Shadow.c's model, a rectangle at +7/16) with each kernel:
the levels byte for byte, no pass above 1x with the bilinear and at least the three composites with
the B-spline, and SCENE byte for byte at every texel except where a composited level is not
constant over the joint support widened by one texel for a rounding tie (level texels i0 - 2 to
i0 + 3: N = 3 level texels, 12, 24 and 48 SCENE texels for SHADOW0..2 at 2x), with some texels
differing (the histogram of |d| is printed).

## The store plan

A tile-based GPU loads every attachment a pass loads into its tile memory when the pass begins and
writes every attachment it stores back when the pass ends. SCENE's depth (D32F with its stencil) is
about 10 MB at 2364x1080, and on the PowerVR phone each full-frame touch of the picture costs about
1 ms (its per-pass timing, `[dev] perf_passes`); every pass drawn on SCENE has SCENE's
depth bound (`prepareDrawEx` binds the state's depth whatever the Z test), so before the plan each
such pass loaded and stored it, read or not. `RdProfile.storePlan` (on for every device class,
`port/render/rd_profile.c`) drops those loads and stores where the depth is provably not needed.

**The scan.** At the head of each replay (`replayFrame`, before `rd__clear_new_targets`; an
interpolated present's blended frame too), `rd__store_plan_scan` (`port/render/rd_store_plan.c`)
walks the replayed lists with their state, numbering the commands in order (state commands
included, from the first replayed list), and records for every depth texture the last command that
touches its depth or stencil:

- a draw (screen prims, the VU draws, a post sprite) on the state's depth with the Z test on (ZTE
  and not ALWAYS), Z write on, DATE on (the stencil bit) or PRIM.AA1 (its stencil pairs); DATE on a
  post sprite other than the reduction's is no touch (`planPost`): every such sprite is drawn
  through the fx shaders, which test DATE against the destination alpha they read (the snapshot,
  or the attachment by local read: [RENDER_POST.md](RENDER_POST.md#post-passes)) with no stencil test in
  their pipelines, and only `doScreen` and the VU draws take DATE's stencil bit; the reduction's
  hardware sprite on a scaled target is a `doScreen` draw, so its DATE stays a touch;
- a draw whose texture is a target's depth view (`RD_VIEW_DEPTH`);
- the three shadow commands (the stencil count and the volumes' Z test);
- the fog: the state's depth, the colour target's and a depth view the texture names; a fog that may
  arm the crack fill (`rd__crack_fill_may`) touches SCENE's depth again from list 7 on, at the first
  command `rd__crack_fill_place` does not let the fill wait past (or the walk's end), as the command
  before it (the fill reads the depth there; the walk may draw it earlier, never later), so every
  pass begun up to there keeps the store, and the last store's cut (below) does not take the pass
  open there (`PlanDepth.late`);
- a command type the scan does not know: every depth.

Copies, exact blends and clears touch none (a clear only writes). The walk counts the same numbers
(`g_rdStoreOrd`, set for each command and past each list's last).

**The rule.** A pass begun while command k is replayed holds the work of command k and later ones
only: a screen run or a VU merge is flushed into the pass it was recorded in, a pending clear or
shadow reset is recorded no earlier than the command that needs it, and the blur fold reads ahead,
never back. So when a depth's last touch lies before k, nothing in that pass or after it reads or
writes the depth: `beginPass` (through `rd__store_plan_pass`) sets its load and store to DONT_CARE
(a pending clear stays a CLEAR load) and the stencil's with it, unless the shadow window is open on
that depth (`rd__stencil_live`). Every other pass keeps its loads and stores, but for the pass that
holds the last touch: `beginPass` cannot know where the walk ends a pass, so it still stores the
depth unless the last store (below, "Passes cut") proves the walk keeps that pass open until the
touch is done. Not planned
either: the passes into SCENE_MS (the MSAA epoch), and the passes begun outside `beginPass` (the new
targets' clears, the shadow reset's own pass, the crack filler's own pass (it binds no depth), the MSAA resolves). The
colour attachments are all stored: the colour's readers (texture reads of a target, copies, the
present, the feedback kept across ticks, a keep frame's retained DISPLAY) are not provable at a
pass's begin. The one exception is the shadow count's colour while the count is made, which no
one reads ([The shadow count's colour](#the-shadow-counts-colour)): `storeColorDropped` counts
those passes.

On the PowerVR phone's 2x frame (`tools/vufuse_dumps.txt`, read from `rd_replay_tool --list`)
the last touch of SCENE's depth is list 5's fog meshes (GEQUAL tested); after it SCENE's depth is
bound by list 11's screen sprites (a screen draw binds the state's depth whatever its Z test; a post
sprite without a Z test or Z write binds none, `rd__blur_key`), so those are the passes the plan
drops on that frame. The fog's own pass and list 5's keep their stores.

The fades fold ([RENDER_POST.md](RENDER_POST.md#the-fades-fold), `port/render/rd_fades_fold.c`) plans before
the scan from it (`rd__fades_fold_scan` at the head of `rd__store_plan_scan`): list 11's last
full-frame lerp sprites (the brightness, the fade, the letterbox bars) are applied by the reduction
and not drawn, and the scan does not count them (`rd__fades_fold_skip` on their numbers). Where
list 11 then has no other draw, no pass on SCENE begins in it at all: SCENE's colour is neither
loaded nor stored for it (the store plan only ever drops depth), and the letterbox, which writes
Z, does not make list 11 the depth's last touch. A bar folds only where nothing reads that Z: not
under the effects depth, and not once a replay found SCENE's depth read before its clear (the
fold's own once-a-run line, beside the plan's one below). The folded frame's SCENE (its pass dump's
`SCENE.png`) holds the picture without the sprites, by design, as WORK2 does under the glow's
mask; DISPLAY is the frame's picture.

**After the replay.** The plan treats a depth as touched after the replay (every pass keeps it) when:

- the present reads SCENE's depth: [video] effects_depth (`RdSettings.effectsDepth`) with an
  effects program loaded (ReShade, vkBasalt: `rhi_injector_name`; never on Android), or the tests'
  `rd__force_effects_depth` (`depthBlit` in `port/render/rd_present.c`, also on a re-shown present;
  `rd__effects_depth_wanted` is the present's own test, `depthWanted`). The setting alone reads
  nothing: it is on by default, and a plan that kept SCENE's depth on the setting alone dropped no
  store on any phone (the PowerVR phone's log said `stores: 0 depth ... dropped` in every 10 s line
  while `rd_replay_tool`, whose settings have it off, dropped list 11's on the same frame). The scan
  says which holds, once a run: `rd: store plan: [video] effects_depth is on but no effects program
  reads it: SCENE's depth stores are planned`, or `... the present reads SCENE's depth for an
  effects program ([video] effects_depth): its stores are kept`. The fades fold asks the same
  (`rd_fades_fold.c`);
- a replay found the depth read before its clear (the head clear `RDC_CLEAR` with depth, or the new
  target's clear): a frame drawing on the depth the replay before left, as a keep frame
  (`rd__first_list`) would if its lists 11 and 12 tested SCENE's depth. That depth's stores are kept
  from then on and the log says so once (for the first four a run):

      rd: store plan: SCENE's depth is read in list 11 before this replay clears it (frame 1234, a keep frame): its stores are kept from now on (the replay before had dropped its last store: this picture may show a depth left undefined)

  The last clause appears only when the replay before had dropped that depth's last store: that one
  picture may show it (in practice the memory holds what the last stored pass left, above). Every
  frame of the dump corpus that touches a depth (`tools/vustage_dumps.txt` and the phone's, read with
  `rd_replay_tool --list`) clears SCENE's depth at list 0's head before any read, and the 2D frames
  touch none; one frame (the issue-95 loss frame) Z-tests list 11's text against SCENE's depth, so a
  keep frame drawing such text would be the first case of this rule.

  A target with `clearPending` set (made, made again at a new size, or handed back by a self-test)
  is cleared by `rd__clear_new_targets` after the scan and before the walk, so no earlier replay's
  depth is in it: a read before the frame's own clear reads that clear, is not a carried read and
  keeps nothing (`planDepth` counts that clear as command 0). The log says so for the first eight a
  run, then every 100th:

      rd: store plan: SCENE's depth is read in list 3 before a clear in frame 0, the target new this replay: not a carried read (1 so far)

  The shadow count's self-test (`port/render/rd_shadow_selftest.c`) is such a frame (frame 0, list
  3: the reset, the volumes and the resolve, no clear of its own), replayed twice (the one draw, then
  the seven draws). Each replay starts SCENE from its clear (`run` sets `clearPending`): were only
  the first to find SCENE cleared, the second would read the depth the first left, a carried read,
  and the keep would hold SCENE's depth stores for the whole run on every device (the line
  `SCENE's depth is read in list 3 before this replay clears it (frame 0)` at the start, then
  `stores: 0 depth ... dropped` in every 10 s line). A self-test frame that reads a depth before
  clearing it starts the target from its clear the same way;
  `rd_store_plan` (below) holds this for the start's self-tests at two scene sizes;
- a caller read the depth back (`rd__read_target_depth`, the tests): kept from then on, logged
  `rd: store plan: SCENE's depth is read back: its stores are kept from now on`;
- the replay has a pass window (`rd_replay_tool`'s bisect reads targets mid-frame): no plan at all.

**The live path.** The window's replays run on the render thread (`rd_thread.c`
`rd__replay_frame_async`), an interpolated present's blended frame (`rd_interp.c`, then
`rd__present_blended` in `rd_reshow.c`) as a game frame: the same `replayFrame`, the scan, the walk
and the record (`g_rdPerf`, filled by the replay's thread and queued by `rd__perf_end`;
`rd_perf_pop` on the window's side sums it under the queue's lock). A re-shown present replays the
empty keep frame: no pass, nothing planned. So the live picture and `rd_replay_tool` on its dump
plan the same passes where the settings agree.

**Nothing dropped.** A planned replay that had a pass with a depth bound and dropped no store says
why at its end (`rd__store_plan_end`, the first eight a run, then every 100th), for SCENE's depth
where the replay has it, else the first depth with a pass:

    rd: store plan: nothing dropped (frame N): SCENE's depth last touched by the fog in list L at command K; M passes began after it with the depth bound (P in all): kept for an effects program ([video] effects_depth) (C so far)

The last touch is the command's type, its list and its index in the list (`not touched in the
frame` without one); the passes counted are those begun after it with the depth bound; the reason
is the first of those passes' (the drop's own order): `stencil live (a shadow window open on it)`,
`kept for an effects program`, `keep (a read before the clear or a readback) since frame F`,
`anyTouch from command type T in list L at command K` (a command the scan does not know), or `last
touch in the last pass` when no pass began after it (`more depths than the plan holds` past 64).
The record carries it (`RdPerfRecord.storeWhy`, `RD_STORE_WHY_*`), and the 10 s line's parenthesis
adds `; nothing dropped in N replays, M of them: <reason>`, the block's most frequent.

The passes the plan drops lie after the depth's last touch and draw nothing into it, so a device that
skips the write-back leaves the memory as the last stored pass wrote it; Vulkan calls the contents
undefined, which only matters to a reader the plan does not see (the next frame, above).

**The backends.** Vulkan passes the ops through (`port/rhi/vk/vk_cmd.c`: dynamic rendering's
attachment ops, or the legacy `VkRenderPass` cached by its ops, its pipelines made against the
ops-free compatible pass). Direct3D 12, Direct3D 11 and OpenGL ignore the store ops and keep the
contents (`d3d12_cmd.c`, `d3d11_cmd.c`, `gl_cmd.c`; no `DiscardResource` or
`glInvalidateFramebuffer`: those backends run on desktop GPUs, where nothing is gained). lavapipe
keeps the contents whatever the load and store ops say (measured with a two-pass probe on Mesa
25.0.7: a DONT_CARE load, store or both left the colour, the depth and the stencil as the first pass
cleared them).

**The switch and the lines.** `[dev] store_plan = false` (`ICO_RD_STORE_PLAN=0`,
[CONFIG.md](CONFIG.md)) keeps every load and store; the profile's start line and the summary say
`depth stores planned` or `depth stores kept ([dev])`. `ICO_RD_STORE_PLAN=all` (tests only) drops
every depth store and keeps the loads, a wrong plan (`depth stores all dropped (test)`). The window's
10 s line about the uploads ends with

    ; stores: N depth and M colour stores dropped a replay of P passes (planned; [dev] store_plan)

(the means per replay; `every store kept` when no replay planned). While the keep holds depths of
the last planned replay, the parenthesis also names them: `(planned; K depths' stores kept (a read
before the clear or a readback), SCENE's since frame F; [dev] store_plan)`, the target and frame of
the earliest such keep (`RdPerfRecord.storeKept`, `storeKeptTarget`, `storeKeptFrame`) and `rd_replay_tool --stats` prints
`stores: N depth and M colour stores dropped a replay of P passes (D with a depth; store plan on)`
(`RdPerfRecord.storeDepthDropped`, `storeColorDropped`, `storePasses`, `storeDepthPasses`,
`storePlanMode`; [TESTING.md](TESTING.md)).

**The tests.** `rd_store_plan` ([TESTING_PROGRAMS.md](TESTING_PROGRAMS.md),
`port/render/test/rd_store_plan_test.c`)
runs the start's self-tests at two scene sizes and synthetic frames on lavapipe: no keep after the
self-tests, a new SCENE read before the frame's clear not kept and the next frame's drop in force, a
frame reading the depth the replay before left kept from then on, its record naming the keep; the
phone's frame 5354 in shape dropping list 11's store with the effects depth setting on and no
program, none with the effects depth in force (said why), and the records' drops reaching the 10 s
text.
`rd_store_plan_ab` replays the corpus and the phone's frame
with every store kept, with the plan and with every depth store dropped, under
`ICO_RD_STORE_POISON=1`: a load the plan leaves undefined (a load it made DONT_CARE, or a load after a
store it dropped) clears the depth to 0.5 and the stencil to 0x5A instead, the garbage a tile-based
GPU may leave, since lavapipe itself keeps the contents. The plan must give every target byte for
byte; dropping every depth store must change some target.

## The pass area

A tile-based GPU loads a pass's attachments into its tiles when the pass begins and writes them
back when it ends, over the pass's render area. The render area (`RhiRenderPassDesc.area`; Vulkan
`VkRenderingInfo` and `VkRenderPassBeginInfo` `renderArea`, offset and extent) bounds the tiles a
pass loads and stores, so a pass whose draws leave whole 32 x 32 tile columns or rows of SCENE
untouched moves only the tiles they touch.

**What it was made for, and what it finds on the phone.** On the PowerVR phone at Resolution
Auto (SCENE 2364 x 1080) the aura's paste into SCENE is a pass of its own with one draw (`aura
SCENE 2366x1080 1 draws 2.83 ms` in a stage 6 block of the phone's log of 2026-11-02 at 11:42), and it was
taken for a sprite of FEED128's 212 x 128 GS pixels, 11 % of the picture. It is not: FEED128 is
the paste's texture, and the sprite is the frame's own rect (`staticBlur.c`
`pasteFeedBackAreaToFB` and `addWork1ToFB` draw `rect`, the frame from -w/2 to w/2, with FEED128's
UVs stretched over it). The phone's F12 dump of that session (frame 1186) holds it as list 8 command 55, rect
(25984,29184)-(39552,36352) at 848 x 448 GS pixels, UV 8..3400 x 8..1800 (FEED128's 212 x 112
texels); its blended twin holds the same. List 11's pass on SCENE starts with a full-frame sprite
(command 26, (25984,29152)-(39552,36384)). So every pass on SCENE in that frame either draws
over the whole picture or holds a VU draw, the fog or a clear's load, and no pass spares a tile:
that log's `0.0 passes with an area` in every 10 s block is the area doing what it should, not
a missed pass. The 10 s line and the replay tool now say why each pass on SCENE stayed whole
(below); `its draws cover the picture` is the paste's and the fades' reason.

**Why the picture is the same.** The Vulkan specification (`VkRenderPassBeginInfo`, and
`VkRenderingInfo` the same) restricts "the effects of attachment load, store, and multisample
resolve operations" to the pixels inside the render area, and asks that "the application must
ensure (using scissor if necessary) that all rendering is contained within the render area"; the pixels outside
keep what memory holds. A pass whose draws touch no pixel outside its area therefore leaves every
pixel outside as the whole pass would: a loaded and stored pixel no draw touches is written back
unchanged, and a DONT_CARE load or store (the store plan's dropped depth) is undefined in the whole
pass and untouched in the area pass, a depth nothing reads. A CLEAR load clears the whole
attachment, so a pass that takes a clear (a pending clear, a shadow reset as the stencil's load op,
the store plan's poison) keeps its whole target. The RHI cuts every scissor of the pass to the
area (`port/rhi/vk/vk_cmd.c` (`rhi_cmd_set_scissor`)), the default scissor is the area and the
default viewport the whole target (the draws' coordinates do not move); D3D11, D3D12 and OpenGL
ignore the area ([RHI.md](RHI.md#vulkan)).

**The draws a pass holds, before it begins.** As the store plan has it, a pass begun while
command k replays (`g_rdStoreOrd`) holds the work of command k and later ones only. The scan
before the walk (`rd__pass_area_scan`, called from `rd__store_plan_scan` after the fades fold's)
reads the lists with their state once and gives every command a kind (`rd__pass_area_cmd`):

| kind | commands | why |
| --- | --- | --- |
| none | state, NOP, the overlay's text, text the present draws (`g_rd.deferText`), a sprite the fades fold leaves to the reduction | nothing drawn in the walk |
| box | a screen-prim command on a target with its own depth: `rd__screen_area` (the vertices' box padded by two GS pixels, three with PRIM.AA1, through the texel scale and Full pixel's crop, in the scissor; a UI command with and without the mirror's flip; a full-frame sprite widened to the frame's width, as `extendFullFrame` may widen it); the aura's sprite (`RD_POST_AURA`): `fx_rect_vs` covers the target and `fx_sprite_ps` keeps the GS pixels inside its rect, so the rect padded by two GS pixels in the scissor, the box `blurSnapArea` takes for its snapshot | the fragments lie inside the box before the GPU runs |
| whole | the VU commands (meshes, skinned meshes, grids, particles), the shadow commands, the fog, every other post sprite, a screen command under DATE or COLCLAMP 0's wrap, a command on a target without a depth, or whose payload cannot be read | the vertex work decides the coverage; DATE's stencil setup draws over the scissor (`rd__replay_date_take`); a crack fill reaches past every uncovered run of texels anywhere; the reduction's open border, the glow's edge strips, the blur fold and Motion blur: Smooth draw past their rect |
| end | a clear, a copy, an exact blend, a command the walk does not replay | it records outside the pass |

A pass beginning on target c at command k (`rd__pass_area_begin`, from `beginPass`) takes the
union of the box commands on c from k on, up to the first command after k that ends or draws on
another target (E); a whole command on c in between, or command k not a box on c (a pass begun
for a fold, a VU flush, a DATE pass), and the pass keeps its whole target. The walk ends a pass
with an area that is still open at command E (`rd__pass_area_due`, at the top of the command
loop), so whatever E and the commands after it draw goes to a pass with its own area: the area
holds the pass's draws whatever the walk then does. E normally ends the pass anyway (it draws on
another target, or records outside a pass); when it drew nothing and the next pass is on the same
targets, that is one pass more, counted (`RdPerfRecord.passAreaEnds`, "ended early").

**Where.** A pass `beginPass` begins on a target with its own depth (SCENE; FEED128 and the work
buffers are small targets already), single-sampled (not in the MSAA epoch), not a storage pass,
without a CLEAR load, and never under the replay tool's pass window. The union is aligned out to
whole 32 x 32 tiles (`RD_PASS_AREA_TILE`) and clamped to the target, and taken when it is at least
one tile column or row narrower than the target. On by default on the tile-based classes
(`RdProfile.passArea`: Adreno, Mali, PowerVR, Apple, another tiler), off on desktop Vulkan, D3D12,
D3D11 and OpenGL; `[dev] pass_area = auto|on|off` (`ICO_RD_PASS_AREA`, [CONFIG.md](CONFIG.md)) over
it.

**Why a pass stays whole.** `rd__pass_area_begin` counts every pass on a target with its own
depth that it leaves whole, by the first reason that holds (`RdPerfRecord.passAreaWhy`, rd.h
`RD_AREA_WHY_*`; `rd__pass_area_why` for one command):

| reason | words in the lines | when |
| --- | --- | --- |
| SCAN | no scan of its commands | no scan this replay (a pass window), or the command it began at is on another target or size than the scan saw |
| CLEAR | a clear as its load | it takes a pending clear (colour, depth or stencil) as its load op |
| MSAA | MSAA | in the MSAA epoch |
| STORAGE | a storage pass | the exact blend's storage pass |
| BEGIN | begun where no area starts | begun at a command that is not a box on its target (a clear, a fold's or a flush's draw) |
| VU, FOG, SHADOW, POST, SCREEN | a 3D draw, the fog, a shadow command, a post sprite, a flat draw under DATE or the wrap | the first whole command of its run (at its start or before its end) |
| EMPTY | nothing drawn | its boxes are empty |
| LARGE | its draws cover the picture | the union, out to whole tiles, leaves no tile column or row of the target out |

**The cost and the saving.** The scan is one walk of the lists' state with a box for each screen
command and aura sprite on SCENE (none where the area is off); the passes hold the same draws. On
the phone's stage 6 frames it saves nothing (above); a pass of flat sprites that leave tile columns
or rows of SCENE untouched (a menu's or a caption's) loads and stores only the tiles they touch.

**The lines.**

      rd: pass area: on (a tile-based GPU's default; [dev] pass_area = off keeps every pass whole): a pass on the picture whose draws are all flat sprites and shapes with a known place loads and stores only the 32 x 32 tiles they touch (the same picture)
      rd: profile: ...; pass area: on
      rd: pass area: SCENE in list 8 (the aura's sprite): WxH at X,Y of 2364x1080 (N of 2516 tiles loaded and stored; the first 8 such passes a run are named)
      window: per replay ...; P passes with an area (M % of the picture's tiles loaded, K ended early); W passes kept whole a replay (A its draws cover the picture, B a 3D draw, C a clear as its load, ...)

The kept-whole clause names only the reasons that held in the block, as means a replay, and is
left out when no pass on SCENE was whole. The replay tool's `--stats` prints `pass area on|off: N
passes with an area (T of their A tiles loaded), E ended early; W passes kept whole (A its draws
cover the picture, ...)` (whole counts for the replay), and `--pass-times` adds ` area WxH at X,Y`
after a pass's size (`RdPerfPass.ax`, `ay`, `aw`, `ah`).

**The tests.** `rhi_vk` (`renderAreaCell`): a pass with an area over a cleared target leaves the
texels outside it as they were after a draw over the whole target, its scissor set to the whole
target and cut, and a CLEAR load with an area clears the area alone, on both pass paths.
`rd_state` (`rd_state_pass_area_test.inc`): the boxes of a screen sprite and the aura's sprite at 1x,
at the phone's 848 x 448 GS frame in 2364 x 1080 texels and under Full pixel's crop hold every texel
they cover within the pad, and the run of a pass at the sprite, with its reasons (none at the
sprite, a 3D draw before a grid); the phone's paste and fade as the game records them (the frame's
rect, a full-frame sprite) are boxes of the whole target whose passes stay whole because their draws
cover the picture; the reasons' words. `rd_pass_cut` (its fourth cell): on lavapipe with the area
forced on, a pass taking a clear and a pass of full-frame sprites counted as kept whole for those
reasons, none with the area off. `rd_pass_area_ab` replays the corpus and the phone's frames with
the areas off and on (`ICO_RD_PASS_AREA=0`, `1`; lavapipe clips to the area): every target byte
for byte, and on the phone's frames at its setting some pass with an area or `its draws cover the
picture` among the kept-whole reasons ([TESTING_PROGRAMS.md](TESTING_PROGRAMS.md)).

## Passes cut

Three passes of a play picture, or parts of them, moved bytes nothing looks at
(`port/render/rd_pass_cut.c`; each behind its own `[dev]` key, `auto|on|off`, auto on on the
tile-based classes, `RdProfile.passCut`). On the PowerVR phone each full-size load or store is about
1 ms per 10 MB. The pass list of the phone's F12 frame of 2026-11-02 (`rd_replay_tool
--pass-times`) shows all three: `reduction DISPLAY 0 draws` then `reduction DISPLAY 1 draws`, `aura AURA_WORK 0
draws`, and the fog's pass storing SCENE's depth though no later pass reads it.

**The clear carry** (`[dev] clear_carry`, `ICO_RD_CLEAR_CARRY`). A clear is pending until the next
pass on its target takes it as its load op (`beginPass`, `rd__take_pending_clear`) or it is recorded
as a pass of its own (`flushClear`), which `endPass` does for whoever ends a pass. A draw that samples
a target not yet in SHADER_READ ends the open pass to put it there (`resolveTexture`,
`doBlurSpriteDraw`), and that end flushed a pending clear of the draw's own target into a pass of
its own just before the draw's pass on it began. The reduction does this every play replay: its
black sprite is DISPLAY's clear (`rd__fx_reduction_clears`), its textured sprite then reads SCENE.
With the carry (`endPassFor` in `port/render/rd_replay.c`), the clear stays pending across that end
when the texture made readable is not its target, no shadow reset is pending and no MSAA epoch is
wanted, and the reduction's pass takes it as its CLEAR load: one 2364x1080 pass fewer a picture
(`reduction 1.0` in the 10 s `passes per replay` line). Exact: a pending clear is recorded before
anything else touches its target (a pass begun on the target takes or flushes it, a transition of
its textures flushes it, any later `endPass` flushes it), only the source's barrier lies between
where it was flushed and where it now goes, and a CLEAR load of the same values over the whole
target (a pass that takes a clear has no area) writes what the clear's own pass wrote.
The frame-size post sprites' plan (`fxDraw` in `port/render/rd_fx_plan.c`) and SCENE's border
repeat (`sceneRepeat` in `port/render/rd_blur_border.c`) end the open pass for SCENE the same way
(`rd__replay_end_pass_for`): the glow's WORK2 fill (`fillWork2`'s sprite, a clear) stays pending
across the end the mask's read of SCENE makes (the repeat first where the border is open, the
mask's own transition otherwise), and the mask's pass takes it: one full-size WORK2 pass fewer a
glow picture (`glow WORK2 2.0 passes 1 draws` in the PowerVR phone's 10 s line of 2026-11-02, the
fill's 0-draw pass and the mask's, is one pass). Where the sun's fans draw into WORK2 before the mask (`dispSun`), their
pass took the fill already, and the mask's own pass stays: SCENE's read ends the fans' pass.

**The clear skip** (`[dev] clear_skip`, `ICO_RD_CLEAR_SKIP`). The constant read (above) draws
WORK0's sprite in AURA_WORK's clear colour without reading AURA_WORK, and with no aura object nothing
else touches AURA_WORK in the replay, yet its clear's pass stored 10 MB. `flushClear` asks
`rd__clear_skip`, which leaves the clear unrecorded when all of these hold: the target is AURA_WORK
or AURA_TAP (no code outside the command lists draws into them or reads them: the present, the glow
hold, the water, the dumps and the interpolation read the other named targets, and the present
SCENE and DISPLAY); the action now walked is the sprite the constant read drew from it
(`rd__clear_skip_served`); the clear holds no depth; and the scan (`rd__pass_cut_scan`, from the
store plan's scan) finds no later command of the replay touching it (a draw into it or on its depth,
a texture of it, a copy or exact blend naming it, a command it does not know; a later clear of it
overwrites it and is not a touch). The memory then holds what was there before the clear while the
frame's picture of the target is the clear, so the clear is kept as a lazy clear (target, colour
texture, colour) and drawn where anything could see the difference: at the start of a later replay
whose first touch of the target is not a clear of it (`rd__clear_skip_begin`, from
`rd__clear_new_targets`), and before a read of the target outside a replay
(`rd__clear_skip_readback`, from `rd__read_target`: the replay tool's PNGs, the tests). A clear of the
target recorded or taken later, a new texture (`clearPending`) or a new context forgets it. Every
read of the target therefore sees the clear's bytes, as without the skip. Expected: one 2364x1080
pass fewer a picture in play (the phone's 0.11 ms plus a tiler's cost of a pass).

**The last store** (`[dev] last_store`, `ICO_RD_LAST_STORE`). The pass begun at command k on colour
cid and depth did whose depth's last touch t lies at k or after has its depth and stencil stored
DONT_CARE (loaded as before) when the walk is known to keep it open until command t is done; every
later pass on that depth then begins after t, where the plan neither loads nor stores it. Known, by
the walk's code, when:

- the pass is a draw's own (`rd__last_store_arm` in `bindDrawEx`), its depth loaded (not a pending clear's pass), the plan in force, no shadow
  window open on the depth, no command the plan does not know after k, the depth not kept past the
  replay;
- no MSAA epoch is wanted, the blend is fixed-function (no storage pass switch), GS state as shader
  data is off, there is no pass split and no pass window;
- command k is a screen-prim or VU draw on cid with did bound, DATE off, COLCLAMP 0's wrap off, not
  sampling its own target or a depth view (after `bindDrawEx` begins its pass such a draw records
  only into it; a texture of another target was made readable by `prepareDraw` before the pass
  began), or the fog on the local read with no probe wanted and no edge read due
  (`rd__last_store_fog`, said by `doFog` before it begins anything: those two end the pass after the
  draw to copy the depth), or a post sprite on cid with did bound (its Z test or Z write) that the
  fx plan draws without the local read (the constant fold, the premultiplied blend, the alpha
  alone: `rd__last_store_post`, said by `fxDraw` just before its `bindDraw`, whose begin is then
  followed by the sprite's draw and nothing else of the command; not for the reduction, the motion
  blur, or a textured flare's or bloom's sprite, whose callers may draw again in the command: the
  border strips, Motion blur: Smooth, the glow's edge strip);
- every command after k up to t is a state change, a NOP, overlay text, or such a draw on cid with
  did bound whose texture, if it samples another target's colour, is that target's in SHADER_READ
  when the pass begins (`drawOn`).

The last condition is exact: `resolveTexture` ends the open pass for another target's colour only
to put it in SHADER_READ (`endPassFor`, then the barrier); from the pass's begin to t only draws on
cid with did bound run, and a draw's barriers only put its own texture in SHADER_READ, so a target
already there stays there and no draw between ends the pass for it. A target not there yet (drawn
since its last read) still ends the pass, and the pass begun at that draw is the one taken.

Every other place the walk ends or begins a pass (`endPass`, `beginPass`, `takeSnap`,
`dateSnapshot`, `doScreenWrap`, `doExact`, `doCopy`, the shadow commands, the fog's other paths,
`blurSwapTarget`, `doBlurSpriteDraw`, the pass area's end, the fx plan, the blur border, the crack
filler, the aura box, Motion blur: Smooth, the shadow's own pass, the pass split, `dstPassMode`, the
MSAA epoch) lies on a path those commands do not take; the pass area ends a pass with an area only
at a command no area run holds. A guard (`rd__last_store_end`, in `endPass`) turns the cut off for
the run and logs it if such a pass ends before command t is done (a missed path; the store plan's
poison shows the lost depth in `rd_pass_cut_ab`). Above 1x under Enhanced the crack fill reads
SCENE's depth from list 7 on (after the glow's `cleanUpFB`, or in the aura's paste pass), after the
scene pass that holds the fog and lists 4-6, so that pass keeps its store (`PlanDepth.late`, the scan above); at 1x and under Original, where nothing
fills, the cut takes the pass holding the depth's last touch as before: on the phone's frame at 1x
the fog's pass, whose store (13 to 20 MB, about 1.3 to 2 ms on the phone, an estimate) goes. Like the plan's other drops, the depth is then undefined
past the replay; a later replay that reads it before its clear is kept as the plan keeps it.

**How often, on the phone.** That log's stage 6 blocks say `1.0 depth stores dropped at the
last touch a replay` with `fog 1.0` in `passes per replay`: every fogged replay. The block that
read `0.3` (`0.5 clears taken by the next pass, 0.2 clears skipped, 0.3 depth stores dropped`)
holds the title's menu and the load into stage 6, and its `passes per replay` has `fog 0.3`: the
cut fired at every fogged replay there too; the rest of the replays were menu pictures and
pictures shown again with no fog pass and no SCENE depth after its last touch. In that F12
frame the fog is list 4 command 50 and the commands between it and the depth's last touch are one
untextured screen sprite and 65 VU draws on SCENE with its depth, all of them image-textured, no
DATE: the fog's pass is the one taken. Only the title block before it (stage 1: `fog 0.3`, `0.2`
dropped) leaves a third of the fogged replays' stores kept for a reason the log did not say; the
reasons line below says it from now on.

**Post sprites and the last store.** In a frame with the glow, the glow's `cleanUpFB`
(`ico2/sugipon/src/staticBlur.c`) draws two untextured sprites into SCENE: the first (Z ALWAYS,
no Z write) writes alpha 0 over the picture, the second (Z GEQUAL at Z 1) alpha 192 where nothing
nearer was drawn; the fx plan draws both as the constant fold (the colour the destination's, the
colour mask A alone). The first binds no depth and ends the pass before it; the second, the
depth's last touch, begins a pass of its own on SCENE with its depth, which kept its store with "a
post sprite" before the post sprite's promise and drops it now (one depth store, 13 to 20 MB at
2366x1080). A post sprite that binds no depth (no Z test, no Z write) still ends the pass open on
the depth before it, so it is not taken between the pass's begin and the touch: a pass begun before
it would end there with its store dropped, and the touch after it would load what was not stored.
The aura's DATE sprites (`addWork1ToFBWithZ`'s TEST 0x34000 and the tint's 0x34003, aura modes 1
and 3) bind no depth (Z ALWAYS, no Z write) but made the depth's last touch a post sprite after the
scene's own while DATE counted as a stencil touch on them (where nothing after them reads the
depth); with `planPost` the depth's last touch is the scene's, and the pass holding it drops its
store as on frames without them. The PowerVR phone's three stage 6 F12 frames (aura mode 2)
have no DATE anywhere after the fog: the aura's paste is LERP As (ALPHA 7) at Z ALWAYS without Z
write, binding no depth.

**Why a last store is kept.** `rd__last_store_drop` notes, for each depth, the reason the last
pass begun with it bound before its last touch was done could not drop the store, and
`rd__last_store_replay_end` (from `rd__store_plan_end`) counts it once a depth a replay
(`RdPerfRecord.lastStoreWhy`, rd.h `RD_LAST_WHY_*`): no scan; a setting (MSAA, the exact blend,
GS state as shader data, the pass split, a pass window); a pass begun outside a draw on its colour
and depth (a post sprite's, a clear's, a fold's); the depth cleared, not loaded; a shadow window
open; a command the store plan does not know; and the first command at the pass's start or
before the touch the walk may end it at: a DATE draw, a draw reading its own target, a depth or a
target not ready, COLCLAMP 0's wrap, a post sprite, the fog with a probe, an edge read or not on
the local read (or not at the pass's start), a draw on another target, a clear, a copy or another
action. A depth whose last touch's pass dropped its store, or whose store the plan keeps past the
replay, is not counted.

**The lines.**

      rd: pass cut: a clear carried past a source's barrier into the next pass on its target: on (a tile-based GPU's default; [dev] clear_carry = off draws it as before)
      rd: pass cut: a clear nothing reads before it is cleared again not drawn: on (...)
      rd: pass cut: the depth not stored by the pass holding its last touch: on (...)
      rd: pass cut: DISPLAY's clear drawn as the load of its next pass, not a pass of its own (reported once)
      rd: pass cut: AURA_WORK's clear not drawn (frame's command N: nothing reads it before it is cleared again; the sprite after it took its colour) (K so far)
      rd: pass cut: a pass whose depth store was dropped at the last touch ended at command N, before the touch at command T was done: ... the cut is off for the rest of the run ([dev] last_store) (K so far)
      rd: pass cut: a post sprite's own pass (SCENE's depth, command K) holds the depth's last touch: its store dropped (reported once)
      rd: store plan: a post sprite under DATE (list L command I) tests the destination alpha in its shader, not through the stencil: DATE is no touch of its depth (reported once)
      window: ...; passes cut: C clears taken by the next pass, S clears skipped, D depth stores dropped at the last touch a replay ([dev] clear_carry, clear_skip, last_store); K last stores kept a replay (A a DATE draw, B a post sprite, ...)

The skip's line is the first 8, then every 100th; the guard's the same. The 10 s `stores:` clause
counts the last store's drops among the depth stores dropped. The replay tool's `--stats` prints
`pass cut: clear carry on|off: C clears taken by the next pass; clear skip on|off: S clears
skipped; last store on|off: D depth stores dropped at the last touch; K last stores kept (A a DATE
draw, ...)` (`RdPerfRecord.clearsCarried`, `clearsSkipped`, `storeLastDropped`, `lastStoreWhy`). The
kept clause, in the 10 s line and the tool's, names only the reasons that held and is left out
when none did.

**Not cut: the reduction into the present.** The present (`rd__present_record`,
`port/render/rd_present.c`) is not a copy of DISPLAY at its own pixels on the phone, so the
reduction cannot draw the swapchain image in its place with the same bytes: under Full pixel
DISPLAY holds `RD_FULL_PIXEL_GUARD` texels more at each side than the exact width
(`rd__target_scale_of`, `port/render/rd_core.c`), 2366 texels for the 2364-pixel window, and the box
blit leaves them out (`sourceCrop`), so the window's pixel x is DISPLAY's texel x + 1. Drawing the
reduction one pixel to the left needs another viewport, and the GPU's viewport transform rounds
`x + w / 2 + ndc * w / 2` in floating point with other operands, which is not the same position to
the bit. Two attachments in one pass (DISPLAY and the swapchain) share their pixel coordinates, so
they cannot hold the shift either. The present's pass also draws the deferred text, the overlay and
the touch controls, flips the picture for the mirror mode, and its output is BGRA or another
swapchain format the reduction's pipelines are not made for. The clear carry takes the reduction's
own clear pass instead.

**The tests.** `rd_pass_cut` (`port/render/test/rd_pass_cut_test.c`, lavapipe, under the poison): the
carry on frames built there (one pass fewer, the same bytes), the skip's decision and its lazy clear
(drawn on a read, at a later replay that draws on the target first, forgotten after one that clears
it), the last store dropped and kept where a command the cut does not take lies between, with the
DATE frame's reason counted and the others' none, a draw reading a target made readable by the
pass's first draw kept in the pass (the store dropped there, the guard not tripped), and the pass
area's reasons (its fourth cell).
`rd_pass_cut_ab` replays the corpus and the phone's frames (`tools/pass_cut_dumps.txt`) with every
cut off, each alone and all on: every target and the present byte for byte, the passes fewer by the
clears carried and skipped, the depth stores dropped more by those at the last touch, each cut
cutting something on the phone's frames ([TESTING_PROGRAMS.md](TESTING_PROGRAMS.md)).

## A cleared source read as its colour

The aura (`staticBlur.c` `auraInspireBefore`, list 8) clears AURA_WORK, the frame's size at the
scene's scale, with a black sprite over the whole of it (which the replay records as the pass's
clear, `blurSpriteClears`), and the frame's aura objects are then drawn into it. `auraInspireAfter`'s
first sprite (`reduceCopyAlphaChannelOfWork1ToWork0`) reads the whole of AURA_WORK into WORK0 through
the box above 1x. With no aura object on screen nothing is drawn into AURA_WORK between the two, and
that sprite read a 2.5 Mpix clear at the phone's 2366 x 1080 to compute one colour: the phone's
pass timing (`[dev] perf_passes`, 3176 replays) has the AURA_WORK pass with 0 draws in every one of its
1540 aura replays and the WORK0 pass after it at 1.65 ms.

`rd__const_read` (`port/render/rd_const_read.c`, called by `doBlurSpriteDraw` for a sprite sampling
another target) draws such a sprite untextured, its RGBAQ the texture function of the clear's
colour, and the source is not read. The bytes are the same:

- every texel of a cleared target holds the clear's bytes C (the load op over the whole render area,
  or the pass `flushClear` records), so every tap any kernel loads is C, whatever the wrap and the
  clamp (`fx_load`: floor(C / 255 * 255 + 0.5) = C; the UINT view returns C);
- TEXA leaves an RGBA32 texel as it is (`gs_texa_expand`); a 24- or 16-bit source format is not
  taken;
- each kernel returns C for taps that are all C: nearest the one tap; the GS bilinear `gs_bilerp`
  C + (((C - C) f) >> 4); above 1x the weights of an axis sum to its divisor D (the box's overlaps
  of [lo, lo + S) tile it, S = D; the B-spline's four sum to 24576; a GS axis 16 - f + f or 1), so
  the row R = C Dx, R' = (16 C Dx + Dx / 2) / Dx = 16 C, and t = (16 C Dy + 8 Dy) / (16 Dy) = C;
- `fx_tfx(C, RGBAQ)` is computed on the CPU in the same integers (`constTfx`), and `fx_sprite_ps`'s
  untextured colour is RGBAQ itself; the alpha test, DATE, the blend and FBA read only that colour
  after the texture function. The forms an untextured sprite can then take (the clear of
  `blurSpriteClears`, the fx plan's constant fold) are exact for an untextured sprite of that colour.

What makes the colour known: `pendClear` (`port/render/rd_replay.c`), the one place a replay's clears
are queued, notes the clear of an RGBA8_UNORM target with the count of actions walked so far in the
replay (`rd__const_cmd`, called for every command from the list loop's `rd__replay_fold_at`: every
command but the state deltas, the NOPs and the deferred text). The note serves a sprite only when
that sprite's own command is the next action: any action between (a draw of any kind, a copy, an
exact blend, a shadow resolve, the second sprite of a folded blur pair) may write the target and
ends it, whichever target it names. SCENE is never taken (the MSAA epoch moves its clear to SCENE_MS
and resolves it back), nor a target whose colour texture changed since the note (`blurSwapTarget`),
nor a sprite sampling its own target; a sprite drawn into the noted target ends the note. With an
aura object drawn, the AURA_WORK read is kept as before.

The first sprite drawn so logs once, the summary line says `constant reads on` or `constant reads
off`, the 10 s pixel diet line counts `constant reads N (M target reads kept)` a replay, and
`rd_replay_tool --stats` prints `constant reads on: N sprites drawn in a cleared source's colour, M
target reads kept (the source drawn since its clear, or not cleared this replay)`
(`RdPerfRecord.constReads`, `constKept`):

    rd: constant reads: a sprite into WORK0 read AURA_WORK, which held only its clear (0,0,0,0): drawn with its colour (0,0,0,0), the source not read (reported once; [dev] constant_reads = false reads it)

`[dev] constant_reads = false` (`ICO_RD_CONSTANT_READS=0`) reads every source as before, the A/B
of ctest `rd_constant_reads_ab` (`port/render/test/rd_constant_reads_ab.cmake`: every named
target's PNG equal byte for byte, the same target reads counted either way, the phone's frames of
`tools/constant_reads_dumps.txt` served at the phone's settings). AURA_WORK's clear itself is still
recorded here (its pass, 1.27 ms on the phone, is the store of the cleared 10 MB); the clear skip
(below, "Passes cut") leaves that pass out when the sprite the constant read drew is its target's
only reader in the replay.

## The full-frame reads at a large scale

On the phone at 2366 x 1080 (5.6 times the PS2's pixels) every pass that touches the whole frame
costs about 1 ms a 10 MB read or written. The effects' full-frame reads and what became of them:

| read | pass (the phone's timing, mean) | kept or not |
| --- | --- | --- |
| aura: AURA_WORK into WORK0 (`reduceCopyAlphaChannelOfWork1ToWork0`, box 2 x 4 GS texels a texel) | WORK0 424 x 128, 1.65 ms | served by the clear's colour when no aura object is drawn ([above](#a-cleared-source-read-as-its-colour)) |
| aura: SCENE into FEED128 (`copyCurrentFBToFeedBackArea`, the feedback, box 4 x 4 GS texels a texel) | FEED128 212 x 128, 4.9 ms (the pass after the paste; 1.6 ms was the mean of it and the alpha copy's pass before the paste) | kept: SCENE is the picture ([below](#the-auras-feedback-read)) |
| shadows: the count at its GS size | temporary 848 x 448, 2.4 ms | kept: it reads SCENE's stencil, not a colour ([the shadow count in one draw](#the-shadow-count-in-one-draw)) |
| glow: SCENE's alpha into WORK2 (`makeMaskPatternToWork2`) | WORK2 2366 x 1080, 2.9 ms | computed by the reduction where nothing else draws into WORK2 (below) |

**A reduced copy of SCENE would not give the same bytes.** The FEED128 read's kernel above 1x is
the area box of the step about the GS's own sample point (`common.hlsli` GS_RS_BOX, `fx_sprite_ps`):
the GS samples U = 64 k + 8 (12.4) for FEED128 pixel k, the centre of GS texel 4 k, so the box of
4 GS texels about it spans GS texels [4 k - 1.5, 4 k + 2.5), half a GS texel off their edges at
every scale, and it is summed in integers with the weights in 1/32 texel and two roundings (the row
to 1/16, then the texel). A GS-size copy averaged per GS texel (`box_reduce_ps`, float weights, one
rounding by the UNORM store) cannot weigh half a GS texel, and the GS's own bilinear at the GS size
would take one texel in sixteen (the PS2's point sample, which the box above 1x replaces on
purpose). At a whole scale the box's taps would line up with the copy's blocks only for a 1:1 read
of SCENE into a target at scale 1 of the GS size, which no frame has; and even there the copy's
float average rounds a half one way or the other by the driver, where the box rounds it up. So no
draw reads such a copy and none is made.

### The aura's feedback read

What the aura draws a picture while the game runs (`staticBlur.c` `auraInspireAfter` mode 2; the
PowerVR phone's pass timing at Resolution Auto, `[dev] perf_passes`, 1086 replays of this sequence,
median GPU time; its 10 s lines say the same, `aura FEED128 212x128 3.0 passes 3 draws 5.0-6.0 ms`,
`aura SCENE 1 draws 2.8 ms`):

| pass | what | median |
| --- | --- | --- |
| AURA_WORK (the frame's size), 0 draws | the clear | 0.11 ms |
| WORK0 424 x 128 | `reduceCopyAlphaChannelOfWork1ToWork0` (now served by the clear's colour: [constant reads](#a-cleared-source-read-as-its-colour)) | 2.65 ms read, under 0.05 ms served |
| FEED128, 1 draw | `copyAlphaChannelOfWork0ToFeedBackArea`, WORK0 into FEED128 | 0.09 ms |
| SCENE, 1 draw | `pasteFeedBackAreaToFB`, FEED128 over the frame: SCENE loaded and stored, 20.4 MB | 2.65 ms |
| FEED128, 1 draw | the bottom strip's clear (`systemStatus[0] == 0`) | 0.02 ms |
| FEED128, 1 draw | `copyCurrentFBToFeedBackArea`, SCENE read through the box: 10.2 MB, 27 136 texels of about 20 x 10 taps (5.4 M) | 4.9 ms |

The last one is skipped while the game is paused (`systemStatus[5]`): the pause menu's pictures
have the aura at 3.0 ms. 4.9 ms is not its 10.2 MB: the reduction reads the same SCENE 1:1 with
four taps a texel (2.5 M texels, 10 M taps, and DISPLAY's 10.2 MB written) in 0.72 ms, and the
aura's own WORK0 box read (54 k texels of about 90 taps, 4.9 M) took 2.65 ms before it was
dropped. So on the PowerVR phone the box's per-tap loop over few texels is the cost (fx_sprite.hlsl
`fx_sprite_texel`: per tap the two axes' addressing, a load, its UNORM conversion, TEXA, four
multiply-adds; per row an integer division).

What an exact cut can and cannot be:

- **A reduced copy of SCENE made once** (the candidate the post sprites' half-texel finding already
  ruled out above for the other reads): not exact,
  for three reasons. The box is separable with a rounding between its axes (each SCENE row's sum of
  `wx t` rounded to `R' = (16 R + Dx / 2) / Dx` before the column sum), so any copy that sums rows
  together first, as a GS-size (one texel a GS texel) or any 2D-reduced copy does, has lost the
  rounding the shader takes per row; the box's edges are at `2 c16 - step32 / 2` in 1/32 texel,
  half a GS texel off the GS texel grid and alternating by a 32nd between FEED128 texels (c16 =
  round(U s) with U = 64 k + 8 and s = 591/128), so no fixed partition of SCENE lines up with every
  FEED128 texel's box; and an RGBA8 copy rounds its average where the box keeps 16 times the
  precision. A copy also saves nothing for this read: it reads SCENE once either way, and there is
  no second reader of SCENE at a reduced size in a frame (the glow's mask reads it 1:1, the fog
  in place).
- **The same box in two passes, exact** (shipped: `port/render/rd_aura_box.c`, `[dev] aura_box`,
  on by default on the tile-based GPUs). The box is split at its own seam, the rounding between
  the axes:
  - **pass A**, ROWBOX: an RGBA16_UINT texture of FEED128's width by SCENE's texture height (212 x
    1080, 1.8 MB, on the phone; the renderer's own texture, made again when the size changes,
    never in the pass dump), one triangle through `fx_aura_rows_ps`: at texel (c, y) the row's
    `R' = (16 R + Dx / 2) / Dx` (`fx_kernel_row`) of FEED128 column c's own x taps on SCENE row y.
    The column's taps come from its own GS X, U and su, computed as `fx_sprite_ps` computes them
    for that column (the same DrawCB; a FrameCB with FEED128's origin, scale 1 and no crop), so the
    box edges that move by a 32nd from column to column are each column's own: ROWBOX's x is the
    FEED128 column, never a fixed partition of SCENE (why the reduced copy cannot work). Every row
    0 .. H - 1 is written, the range of `fx_axis_y`, so every addressed row a box can touch is
    there. `R'` is at most 4080 (R <= 255 Dx), which the 16-bit words hold.
  - **pass B**, FEED128's own draw through `fx_aura_sum_ps` (the UINT build, t1 ROWBOX): the
    coverage and V of `fx_sprite_ps`, then for j = 0, 1, ... `sum += ROWBOX(c, fx_axis_y(ay.first +
    j)) wy(j)`, `t = (sum + 8 Dy) / (16 Dy)`, and the texture function, alpha test, blend and FBA
    of `fx_sprite_ps` (`fx_sprite_out`).

  Why the bytes are the same, against `fx_sprite_texel`'s FXF_ABOVE1X branch: (1) both passes bind
  the one-pass draw's DrawCB; (2) at FEED128's scale 1 the X of column c is `ox + 16 c` in both,
  every float step of it exact (so no contraction or rounding mode can move it); (3) U is the exact
  truncated quotient whatever its float steps round to (`gs_div_trunc_rcp`), su one correctly
  rounded multiply and a round, so the axis's first tap, count, weights and Dx are the same
  integers; (4) a row's `R'` is the inner loop's taps (`fx_texel(int2(ax.first + i, ay.first + j))`
  is `fx_texel_at(fx_axis_x(ax.first + i), fx_axis_y(ay.first + j))`, pass A's y that
  `fx_axis_y`), weights, order and rounding; (5) pass B adds the same `R'` with the same `wy` in the
  same order j, and the sum stays under 2^32 (`sum(wy R') <= 24576 x 4080`), so the integer
  additions are exact in any order; (6) everything after the sum is the same function of the same
  t. `fx_sprite_ps` itself is unchanged (its SPIR-V the same instructions, only the ids renumbered).
  Where it applies: only that sprite (FEED128 drawn from SCENE), above 1x (at 1x the sprite takes
  the GS's own bilinear, four taps, no box and no loop to shorten), with the box (not under
  `[dev] aura_kernel = gs`), without a destination read, a self-sample or a depth target, the
  colour mask whole and FEED128 at its GS size; anything else draws the one pass, named once
  (`rd: aura box: the feedback read drawn in one pass: <reason> (reported once)`) and counted in
  the 10 s line. Its two keys (`fx_aura_rows_ps` into RGBA16_UINT, `fx_aura_sum_ps` as the
  sprite's own key) join the start-up set while it is on. The switch: `[dev] aura_box = auto`
  (the profile's, `RdProfile.auraBox`: on on the tile-based classes, Adreno, Mali, PowerVR, Apple
  and another tiler, which share the cost model here: few texels with a long loop each; off on
  desktop Vulkan, D3D12, D3D11 and OpenGL, not measured there), `on`, `off`
  (`ICO_RD_AURA_BOX=1|0`). The lines: `rd: aura box: the feedback read of the picture above 1x in
  two passes (...) (<why>)` or `... in one pass (<why>)` at the start, `; aura box: two passes` or
  `; aura box: one pass` at the end of the profile line, `rd: aura box: ROWBOX WxH made (RGBA16
  UINT, N MB)`, `rd: aura box: N feedback reads in two passes (ROWBOX WxH), M in one pass (...)`
  in the 10 s block, and the pass list's `aura ROWBOX 212x1080 1 draws` before the FEED128 pass
  (`[dev] perf_passes`, the replay tool's `--pass-times`). The cost: the same taps (4.6 M in pass
  A, 0.27 M reads in pass B against 5.4 M), one render pass more, ROWBOX stored and loaded once
  (1.8 MB each way, about 0.4 ms at the phone's ~1 ms per 10 MB); the saving is the share of the
  4.9 ms that is the long loop's serial length (229 k loops of about 20 taps and 27 k of about 10
  for 27 k of about 200), unmeasured until the phone's rc. Tests: `rd_aura_box_ab` (every target
  byte for byte on the dump corpus and the phone frames, one pass and one draw more a read) and
  `rd_blur` case `aura_box` (FEED128 byte for byte at 2x, 2364 x 1080 and 1.5x)
  ([TESTING_PROGRAMS.md](TESTING_PROGRAMS.md)).
- **The measurement**: `[dev] aura_kernel = gs` (`ICO_RD_AURA_KERNEL=gs`,
  `port/render/rd_aura_kernel.c`) draws that one sprite with the GS's own bilinear at its sample
  point (no box: the PS2's taps, the picture not the default's, the feedback shimmering above 1x;
  logged once as `rd: aura: the feedback read of SCENE into FEED128 drawn with the GS's own taps
  ...`). One session on the phone with `[dev] perf_passes = true` with and without it, in the same
  room, gives the FEED128 pass's time without the box: near the reduction's per-texel rate means
  the loop is the cost (what the two-pass box above cuts); near 4.9 ms means the pass's cost is
  elsewhere (the tiler's handling of SCENE between the paste and the read). With
  `aura_box = off` it measures the one-pass box alone.
- **The paste** (2.65 ms) is SCENE's load and store around one full-frame draw: on a tiler a pass
  of its own because the read after it samples SCENE. Drawing the paste in the pass before it
  (list 7's last SCENE pass, or the fog's) would save the pair (~2 ms) only where nothing between
  reads SCENE, and the aura's own passes between are on other targets; it is a pass-merge question
  of the store plan's kind, not this package's (not done).
- **AURA_WORK's clear** (0.11 ms) has no reader in mode 2 without an aura object (the constant read
  serves WORK0): skipping it would save its store, a tenth of a millisecond: not done.

The measurement script `build-host/tmp/v098/evidence/perf/phonegpu.sh` (orchestrator, lavapipe
under the phone's profile) prints these passes per dump with the reduction's beside them.

## The menus on the phone

The PowerVR phone's 10 s lines with `list 11 SCENE 5 draws 9.55 ms` and `list 11 SCENE 9 draws
7.44 ms` (the block after it, which ends in the game) cover the pause menu and the
port's Settings pages: the `menu screen` lines of those blocks are 57 -> 80 -> 92 -> 80 -> 88 and
88 -> 80 -> 57 -> 54 (57 the pause menu, 80 to 92 the Settings layouts opened from it, 54 the game).
In the game list 11 is 1.15 ms (the fades: brightness, fade, letterbox). The pass timing says the
same per pass: list 11 with the fades' 2 to 4 draws 1.0 to 3.0 ms, with the menus' 7 or 8 draws 9.3 to
10.0 ms. With the menus up list 11 holds the fade sprites, the layout's own sprites and the menus'
words as sheet text (`ui_draw_menu_text`): the fades fold does not take that list (the pause menu's
layout is drawn after the fade, [RENDER_POST.md](RENDER_POST.md#the-fades-fold)), and the 1x bake did not
cover it, because the phone's scene is 2.109 times the PS2's height and its strips are at scale 2
(`mfScale`: `ceil(2.109 - 0.25)`), which `font_sheet_ps` rebuilt per pixel (a gather, nine loads,
about sixteen integer divisions). 2x pages are now baked too (on the tile-based classes only, issue 126), byte for byte
([SHADERS.md](SHADERS.md#sheet_texthlsli), **The 2x bake**); the phone's menu pictures should lose
most of the difference to the game's 1.15 ms, unmeasured until the phone shows it (the same 10 s
lines with the pause menu up; `[dev] sheet_shader = true` draws them as before, the A/B).

**The glow's WORK2 is the game's mask, not a copy.** `makeFullScreenFlareBefore` fills WORK2
(`fillWork2`, recorded as WORK2's clear), draws the sun's fans into it when the sun is up
(`dispSun`), then `makeMaskPatternToWork2` draws SCENE into it 1:1 with MODULATE by RGBAQ 0 (the fx
plan's alpha-only form, `FXF_ALPHA_ONLY`) and the alpha test LESS 0x81 with AFAIL KEEP: a texel holds
SCENE's alpha (bilinear at the texel's own GS position) where that alpha is under 0x81, else the
fill. `reduceWork2ToWork0` reads all of it into WORK0 through the box (2 x 3.5 GS texels a
texel). It is not a snapshot for the paste (the paste reads WORK3) and not the blur fold's. Where
nothing else draws into WORK2 after the mask (no lamps) and nothing else reads it, the reduction
computes each WORK2 texel it weighs from SCENE instead, byte for byte (the mask's coverage, its
four alpha loads, the alpha test, the fill; `fx_frame_ps` with `FXF_GLOW_MASK`): neither the fill
nor the mask is drawn, and WORK2 holds no mask; where the sun's fans drew into WORK2 the fill and
the fans are drawn, the mask alone is not, and the reduction loads WORK2 where the mask keeps a
texel (`FXF_GLOW_KEPT`) ([The glow's mask in its
reduction](RENDER_POST.md#the-glows-mask-in-its-reduction); `[dev] glow_fold`). That is the
default on the immediate-mode GPUs only: on the PowerVR phone the saving expected (the WORK2
pass, 2.9 ms in a glow replay) was lost several times over to the reduction's extra work (the mask
and its four SCENE alpha loads at every one of about 70 taps a WORK0 texel): `glow WORK0` went
from 2.3-3.3 ms to 10.4-11.4 ms a glow replay, so every tile-based GPU draws the mask's pass by
default (the profile's `glowFold`, [RENDER.md](RENDER.md#the-device-profile)). The pass
dump's WORK2.png is the mask only where the fold is off (the replay tool's `--pass-dump` keeps it off
unless `ICO_RD_GLOW_FOLD=1`).

## The upload ring shrinks

Not a work buffer; kept here because RENDER.md is at its size cap (issue 126, finding F3). Each
frame slot has an upload ring (`g_rd.ring`, `ringCap`; `port/render/rd_upload.c` `rd__ensure_ring`)
that starts at 4 MB and doubles to the frame's need. A stage change that replaces many textures
needs tens of MB for one frame (the playthrough's lines "the upload ring of frame slot 0 grows to
128 MB (this frame needs 68 MB)"). A ring that only grows would keep two 128 MB rings
in video memory on a 1 GB card beside 19 work targets and the stage's textures, and the replays
after the stage change waited 100 to 160 ms on the frame fence with 8 to 12 ms of timed GPU work
and nothing created.

The rule, per slot (`rd__ensure_ring`, called once a replay with the replay's need):

- a replay whose need is under a quarter of the slot's capacity counts one in `g_rd.ringSmall[slot]`;
  a replay that needs a quarter or more, or a growth, sets the count to 0;
- at `RD_RING_SHRINK_REPLAYS` (300) in a row the slot's ring is remade at half its capacity
  (`newRing`, whose `rhi_destroy_buffer` of the old ring is deferred, so the frames still in flight
  keep reading it), and the count starts again; the log says
  `the upload ring of frame slot N shrinks to M MB (needed under K MB for 300 replays)`;
- never under `RD_RING_SHRINK_FLOOR` (16 MB): a ring at or near it stays. The start size (4 MB) is
  below the floor, which stops shrinking only, not the start;
- a shrink that cannot be made (`newRing` refused) keeps the old ring, and the count still starts
  again.

300 replays in a row is 5 seconds at 60 replays a second; a stage change's burst is followed by the
halvings 128, 64, 32, 16 MB over 15 seconds. The grow lines (64 MB and over) are unchanged.
`checkRingShrink` in `port/render/test/rd_present_test.c` runs the rule on a real ring: the count,
the halving at the 300th, the reset by a quarter-size frame and by a growth, and the floor.
Whether the card was short of memory is in the log: [RHI.md](RHI.md) (`videoMemBudget`).

## Without the sprite planes

Not a work buffer; kept here because RENDER.md is at its size cap (issue 161). A screen-prim triangle
command (a GIF triangle list: the title's and the boot's shapes, the lightning's STQ strips) draws
through the sprite planes builds (`sprite_plane_*_vs`, `sprite_stq_plane_*_vs` with
`sprite_ps_planes`, `sprite_texa_ps_planes`, `sprite_stq_ps_planes` and their twins;
`port/render/rd_screen_planes.c`, RENDER.md "Screen planes"): each triangle's colour and STQ
planes per GS pixel, the same values at every frame width. A device can run without them.

**The fallback.** `rd_set_sprite_planes_off(why)` (the window, from `[dev] pipeline_skip`) or
`ICO_RD_NO_SPRITE_PLANES=1` sets `g_rd.spritePlanesOff` (`rd__sprite_planes_init`, in `rd_init`, or
at once when the device is already made). Then `rd__plane_pass` converts nothing,
`rd__screen_planes_want` keeps the plain sprite build for every pass, its STQ ones too, the start-up
set holds the plain keys in place of the planes ones (`rd__enumerate_reachable_screen`: the plain
STQ keys join it), and a planes key that still reaches `rd__get_pipeline` is never created (one
line, its draw skipped). The picture: the GPU's screen-space interpolation of the corners instead
of the planes, the path a fractional scale already takes (`rd__planes_dropped`). It was
byte-identical on every dump measured (`rd_noplanes_ab`, 2026-10-30, lavapipe: the title's
rd-00200 and rd-00350 and the boot's rd-00600, 2 commands each through the plain builds, 0 of 18
targets differ; a capture without planes keys, 0 commands, 0 differ). A colour a level apart on
some pixels (the 4:3 columns of a wide frame not the 4:3 frame's) is possible in principle,
at a fractional scale, and was not seen. The lines:

    rd: pipelines: the sprite planes builds are not made on this device (the driver hung creating them on 2026-10-30, compiler E031.41.03.47; [dev] pipeline_skip); their draws take the plain builds (...)
    window: sprite no-planes: N commands in these 10 s (the sprite planes builds off)

(the second, in the 10 s block after the pixel diet line, counts the commands of those 10 s that took
the fallback, `port/platform/window_pipeline_skip.c`;
`RdPerfRecord.spriteNoPlanes`, rd_replay_tool `--stats` "sprite no-planes N").

**The record.** `[dev] pipeline_skip = "sprite_planes; <device>; <driver>; <date>"`
(`port/platform/pipeline_skip.h`): written by the window (`port/platform/window_pipeline_skip.c`,
after `rd_init`)
at the start that follows an end while preparing the graphics whose activity note names only that
family (a lazy run's single creation, or a batch whose named vertex shaders are all planes builds,
none unnamed); used on every start whose device name (`rhi_adapter_name`) and driver (the
compiler build of `RhiLimits.compilerVersion`, else the version of the driver text) match it, case
ignored, an empty field matching any; kept and not used on another device or driver (a driver
update tries the family again). `""` or a deleted line makes the family again. The lines:

    window: pipelines: the previous start ended while the graphics driver created the sprite planes builds (<note>); [dev] pipeline_skip = "<value>" written to <config>: this start and every start after it on this device and driver draw without them (pipeline_skip = "" makes them again)
    window: pipelines: the previous start's end names no one family the game can draw another way (<note>); [dev] pipeline_skip is not written
    window: [dev] pipeline_skip names <device> with driver <d>; this device is <device> with driver <d>: the sprite planes builds are made (the record is kept)

and the box on the start that writes it ("The last start closed because the graphics driver stopped
while preparing some of the shapes on the screen. ...").

**The careful order.** Under the careful policy (one thread, the cache written after each batch)
`rd_precreate_pipelines` moves the sprite planes builds after every other key (`planesLast`) and
`createKeys` makes each one a batch of its own, so the cache holds the rest of the set before the
first of them and the batch's note (`creating pipelines N of M: ...`) names one pipeline: an end
inside it is that family's for the next start. The line: `rd: pipelines: the careful set makes its
N sprite planes builds last, one a batch (...)`.

**The in-flight key.** A batch's note names its first and last key and its vertex shaders; a
stall inside a batch that mixes families named none of them on its own (issue 161's 212-219). The
note now also names the key inside the driver: `creating pipeline 217 of 587 (in batch 212-219):
<key> (<vs> N words + <fs> M words)` (`rd_pipeline.c` `noteKey`), the same form as a lazy run's
creation, so the next start's "the previous run ended while: ..." quotes it and the record above can
take its family. On the careful set's path (Android: one `rhi_create_pipelines` call a key) it is
written before the driver's call; elsewhere the Vulkan backend calls the batch's `begin` and `end`
hooks around each `vkCreateGraphicsPipelines` (`rhi.h` `RhiPipelineBatch`, `vk_pipeline.c`
`vkr_pipe_compile_hooked`), the renderer keeps which keys are inside the driver, and the calling
thread's wait (every 50 ms) names the key when exactly one is (several at once on threads keep the
batch's note until one is left). On Android the note goes to the disc before the call returns
(`diag_host.c` `act_sync_note`); the other backends do not call the hooks.

**The free-memory floor (Android).** Before each pipeline of the careful set, and after each batch
of the usual one, the set asks `ico_diag_low_memory_stop`, which now also reads MemAvailable
(`ico_diag_device_memory`, the "device N MB available of T MB" words): under the floor the set stops
as at the system's low-memory signal. The floor depends on the driver class
(`port/platform/memory_floor.c` `ico_memory_floor_driver`, from `rhi_limits()`'s vendor id and
driver text, set by the window before the set: `window_pipe_finish.c` `window_pipe_floor_driver`):

| driver | floor | a stop at it |
| --- | --- | --- |
| Qualcomm's own (vendor 0x5143, the driver text naming neither Mesa nor Turnip), and a driver not known yet | 17 % of MemTotal, at most 2048 MB on a device of more than 8 GB (696 MB on 4 GB, 1392 MB on 8 GB, 1915 MB on the Retroid Pocket 6's 11265 MB, 2048 MB on 12 and 16 GB) | as the low-memory signal's: the rest made on first use, `set: stopped at the system's low-memory signal (<words>)` in the activity file, the next start lazy |
| any other (Turnip, PowerVR, Mali, Adreno under Mesa, ...) | the set's need with a margin: 17 % of MemTotal, at most 512 MB on a device of more than 8 GB | for this start only: the rest made in the background ([PIPELINES.md](PIPELINES.md#the-background-finisher)), the next start makes the set again |

Up to 8 GB the two rules give the same 17 % (with the game running a 4 GB phone has 0.8 to 1.5 GB
available, so an absolute floor would stop every set there). `[dev] pipeline_memory_floor_mb` sets it
in MB for every class, 0 turns it off (exported as `ICO_PIPELINE_MEMORY_FLOOR_MB`). The floor in
force is logged at the set's first ask with the class, and the stop names it the same way:

    ico_pc: pipelines: the free-memory floor is 512 MB (the set's need with a margin: 17 % of total, at most 512 MB above 8 GB; Qualcomm's own driver keeps 17 % of total, at most 2048 MB above 8 GB); another driver than Qualcomm's own; N MB available of T MB now
    ico_pc: pipelines: the free-memory floor is F MB (Qualcomm's own driver: 17 % of total, at most 2048 MB above 8 GB; other drivers at most 512 MB); Qualcomm's own driver; N MB available of T MB now
    ico_pc: pipelines: the start-up set stopped at N MB available of T MB (floor F MB, <rule>; <class>): the rest made in the background and on first use
    ico_pc: the start-up set stopped at the free-memory floor (N MB available of T MB, floor F MB) on another driver than Qualcomm's own; the next start makes the set again
    ico_pc: the start-up set stopped at the free-memory floor (N MB available of T MB, floor F MB); the next start makes the pipelines as the game needs them, with no start-up set

(the last on Qualcomm's own driver, followed on a floor stop by `ico_pc: the next start makes the set again instead when the pipeline cache file for this shader set is on disk and not empty ...`; `[dev] pipeline_memory_floor_mb` in place of the rule's words
when the setting gives the floor; `window: pipelines: the start-up set stopped at the free-memory
floor on Qualcomm's own driver: the rest is made on first use, not in the background` after it).

**The retry on Qualcomm's own driver (issue 123).** A stop at the floor there made every later
start skip the set (the pipelines made on first use), which never healed: on the Xiaomi Pad 6S Pro
the set stopped at 485 of 671 with 1940 MB available of 11453 MB (floor 1947 MB) while that
driver's compiler held about 3.1 GB, and the 1.4 MB cache file the stop had written held those
485. The compiler is what ran the device low, and a pipeline the cache holds is created from it
without the compiler. So the start after a floor stop (not after the system's signal: the file's
line carries the floor's figures, `ico_memory_floor_line_is_floor`) makes the set again when the
driver is Qualcomm's own and this shader set's cache file (`pipelines-<key>.vkcache`, the window's
`ico_diag_pipeline_cache_path`) exists and is not empty and the renderer loaded it at this start
(`rhi_pipeline_cache_loaded`: a file the driver refused, another driver after a system update or
another shader set, answers nothing, so the compiler would run) (`ico_memory_floor_retry`, called
by `window_pipe_floor_retry` in `port/platform/window_pipe_finish.c` after the device is made). At
most one retry in a row: a retried start that stops at the floor again writes its stop line with
the mark ` after a retry` after the floor's figures (`ICO_MEMORY_FLOOR_RETRIED_MARK`,
`ico_memory_floor_line_retried`, `ico_diag_startup_prev_floor_retried`), and the next start makes
no set, as before the retry existed (its lazy run's file keeps the marked line, so the starts after
it stay lazy too). One
line says which way and why, whenever the previous start stopped at the floor: `window: pipelines:
the previous start stopped at the free-memory floor; this start makes the set again (the
pipeline cache file for this shader set is on disk, N bytes, loaded by the renderer: yes): <path>`,
or `this start makes no set (<why>, ...)`. The retry's start does not carry the old "stopped" line
in its own activity file; a start that ends inside the retry steps as an end in the careful set
does (`diag_host.c` `ico_diag_startup_pipeline_set`).

**Why two rules.** The 17 % rule was tuned for issue 161: the Retroid Pocket 6 (11265 MB, Adreno
740 on Qualcomm's own driver) stalled inside that driver's pipeline compiler at 1693 MB available
with no system signal and was ended 18-20 s later, so on that driver the floor guards against the
compiler itself, and the 1915 MB floor still stops that reading (`memory_floor_test`). Elsewhere
Android's MemAvailable is cautious on a phone with much memory: the PowerVR DXT-48-1536 phone of
15552 MB showed 2554 MB available at one start's set (under the plain 17 %, 2643 MB: the set
stopped after 33 of 1193 and the title waited 13.6 s), 2283 MB at another's (which ran to its end)
and 1823 MB at a third's, under the 2048 MB cap: that set stopped after 33 of 1196 too and in play three
VU pipelines made on first use took 0.63-0.81 s each on the render thread, one 2.2 s freeze with
the sound held back. What the set itself needs, from the log of the start that ran it to its end on that phone: the process held 183
MB at the set's start and 251 MB at its end (about 70 MB resident for 1193 pipelines; the cache file
is 39 MB), 415 MB at most in play with the set made, and 694 MB at most while the VU batches
compiled on 7 threads (the compilers' working memory, about 70 MB a thread, given back after each
batch). A 512 MB floor covers the set's resident growth about 7 times and the background
finisher's 4 threads (about 280 MB) close to 2 times; the system's low-memory signal (asked after
each batch, and before each of the finisher's) stays the real guard on every driver.

**The creation limit (Android).** The watchdog's limits are not in force before the game starts,
and the GPU hang limit counts waits for the GPU, not a driver call. On Android a note that names a
pipeline being created (the set's batch, a lazy run's single one) with neither the note nor the
set's count moving for 10 s before the first Main tick (the start-up set, `PIPE_STALL_START_S`) or
30 s in play (a lazy run's first-use creation, `PIPE_STALL_PLAY_S`: a Vulkan 1.0-era compiler may
take over 10 s on a VU merge shader) (`port/platform/diag_host.c` `pipe_stall_step` over
`port/platform/gpu_hang.c` `ico_pipe_note_creating` and `ico_pipe_stall_step`; off with the GPU hang
limit, `[dev] watchdog = 0`) ends the run in order. The background finisher's note (`creating
pipelines A-B of M in the background: ...`) stays up from its batch's start until the next presented
picture takes the batch, which a movie or a long load does not make, so it counts as a creation only
while some of the batch's compiles have not returned, and each compile that returns is progress too
(the Vulkan backend's count of them, `rhi.h` `RhiGpuProgress.bgPipesDone`/`bgPipesLeft`, read by the
watchdog through the window's GPU progress; the render thread's hang watch reads the note the same
way). So a movie of any length over a finished batch is not a stall, and a background compile that
does not return for the limit's time is (`gpu_hang_test` `testStall`). The line:
`ico_pc: the graphics driver has not returned from creating a pipeline for N s (pipelines D of T; the
background set's compiles: R returned, U not; the limit L s before the game starts|in play): the run
ends now, ...`, the report, the box, an exit; the activity file keeps the note for the
next start. The end also writes the stall mark `<activity file without .txt>-stall.txt` (the version,
then the note): every start of that version, on any Android, takes the VU programs' plain entries
(`ico_diag_startup_stall_end`, the window's `rd_set_vu_plain_only`; the `ico_pc: a run of ... ended
when the graphics driver did not return from creating a pipeline (<note>)` line says every start of
the version takes the plain VU vertex shaders, and the `window: a run of ... ended ...` line says the
start draws with the plain 3D vertex shaders), so a slow compiler cannot end the same pipeline every
run; a start of another version deletes the mark (`ico_pc: version V ended a run ...`: the running
version tries the extended VU vertex shaders again). Below Android 9 any counted end in the
pipelines also takes them, for the next start only. `diag_test`'s activity case holds the mark.

**What issue 161's logs show.** The Retroid Pocket 6 (Adreno 740, compiler E031.41.03.47; the
careful set) stopped inside batch 212-219 of 587 with 216 made: that batch mixed the planes
builds (`sprite_stq_plane_world_vs`, `sprite_plane_world_vs`) and a plain one (`sprite_world_vs`),
so which pipeline did not return is not known. The same `sprite_stq_ps_planes` family compiled in
0.08 to 0.10 s in batch 148-155 of that start. The device's available memory fell from 3340 MB (2 s,
147 made) to 1693 MB (23 s, 211 made) while the process stayed at 205 to 214 MB; an earlier start
on the same console drew Android's "running critical" signal at 1900 MB available at pipeline 211,
and its freeze record names a plain build (`program 11 ... sprite_world_vs + sprite_ps`). No
watchdog line ends that log: the system ended the app 18 to 20 s into the stall. The record
above protects the starts after an end that names the family alone; the careful order makes such a
note certain; the creation limit makes the end the game's own.

**Tests.** `rd_vu_frac_test` (`testSkip`, CPU): the set with the planes off holds no planes key and
the plain twin of each, no conversion, the counter, and the clear gives the set back key for key.
`pipeline_skip_test` (`port/platform/test`): the family of the issue's notes (the mixed batch
and the earlier plain build name none; a single planes build and a lazy creation name it), the value
read back, the device and driver match, the clear. `rd_noplanes_ab` (`port/render/test/
rd_noplanes_ab.cmake`, `tools/noplanes_dumps.txt`): the title's and the boot's dumps (one planes key
each, program 9 vs 22 fs 28) with and without the planes: 2 commands each through the fallback
(`expect=fallback:2`) and every target byte for byte; a capture without planes keys byte for byte
with none.

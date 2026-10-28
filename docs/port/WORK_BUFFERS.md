# The post effects' work buffers

The targets `staticBlur.c`'s effects and the screen softening draw through on their way back into
SCENE, their sizes at each frame width, the glow chain's rectangles in them, the column the glow's
blur leaves on a frame wider than the PS2's, what stays in them from one frame to the next, the
work clear that shows a texel read before it was drawn, and how the puddle's ripples sample their
reflection block between ticks, and where the crack filler is drawn on the fog's local path, and
(not work buffers, kept here because RENDER.md is at its size cap) how a screen sprite redrawn under
a new key between ticks is paired, how desktop Vulkan decides on the local reads at start and how
the shadow count is read from the stencil in one draw. The sprites themselves are drawn by the GS
sprite model ([RENDER.md](RENDER.md#post-passes), "The staticBlur sprites").

## Files

| File | What it holds |
| --- | --- |
| `port/render/rd_core.c` | the targets' sizes (`namedTargetDesc`), their scale (`rd__target_scale_of`: the work buffers at their GS size at every scale), their creation (`createNamedTargets`) |
| `port/render/rd_upload.c` | the clear of a new target (`rd__clear_new_targets`) and the work clear (`workClear`) |
| `port/render/rd_blur.c` | the strip over the glow's left edge (`rd__glow_edge_strip`) |
| `port/render/rd_blur_border.c` | the strip drawn after the blur's first pass (`glowEdge`, from `rd__blur_post_sprite`) |
| `ico2/sugipon/src/staticBlur.c` | the effects, their rectangles in the PS2's numbers and the port's widening (`SB_X`, `sbShrinkAt`, `sbShrinkSpan`, `sbWorkRect`) |
| `port/render/rd_crack_fill.c` | the crack fill drawn inside the local fog's pass (`rd__crack_fill`'s `local`) |
| `port/render/rd_profile.c`, `port/render/rd_local_probe.c` | the desktop's local reads (`RdProfile.desktopLocal`) and their start-up cost probe (`rd__local_probe`) |
| `ico2/sugipon/src/puddle.c`, `port/render/rd_interp.c` | the ripples' texture shift between ticks (`drawRipples`; `reprojStShift`) |
| `port/render/rd_replay.c`, `port/render/rd_shadow_one.c`, `port/render/rd_shadow_selftest.c`, `port/shaders/blit.hlsl` | the shadow count in one draw (`shadowCountOne` picks the destination, `rd__shadow_one_draw` draws `shadow_count_ps`) and its start-up self-test (`rd__shadow_selftest`) |
| `port/render/rd_interp.c`, `ico2/common/src/layout_texture.c` | the same screen sprite under another key in each tick (`pairSame` in `unmatchedPass`); the load screen's bars (`LT_HOST_KEY`) |

## Sizes per frame width

The frame is W GS pixels wide (the [native frame](DISPLAY.md#the-native-frame): 512 at 4:3, 688
at 16:9, 832 at 2340 x 1080, 848 at 2364 x 1080, up to 2560). The work buffers keep the PS2's
size per 512 columns of the frame (`port/render/rd_core.c` (`namedTargetDesc`)):

| target | PS2 | at W |
| --- | --- | --- |
| WORK0, WORK3 | 256 x 128 | W / 2 x 128 |
| WORK1, AA0, SHADOW0 | 256 x 256 | W / 2 x 256 (the glow uses its top 128 rows) |
| AA1, FEED128, FEED_HELD, AURA_TAP, SHADOW1 | 128 x 128 | W / 4 x 128 |
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
frame qualifies (s is 0), so 4:3 is the PS2's picture byte for byte; the rest of a wide frame's
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
rd: work targets: 18 named targets made and cleared (the frame 848 GS pixels wide): what the work buffers held from earlier frames is gone (reported for the first 8)
```

Between those, a target holds its last frame's texels. The frame reads them by design in DISPLAY
(the motion blur and the keep pass), FEED128 (the aura's feedback) and their held copies
DISPLAY_HELD and FEED_HELD (a tick's later presents, [DISPLAY.md](DISPLAY.md)); the motion blur's
Smooth accumulator is a texture of its own, not a target, and the RGBA8 target it resolves into is
drawn before the sprite reads it ([RENDER.md](RENDER.md#the-motion-blur-under-smooth)). Anywhere
else, a texel read before the frame drew it is a bug of this kind.

## The work clear

`ICO_RD_WORK_CLEAR=RRGGBB` in the environment (the game), or `rd_replay_tool --work-clear RRGGBB`,
clears every live target's colour but SCENE, DISPLAY, DISPLAY_HELD, FEED128 and FEED_HELD to that
colour with alpha 0x80 at the head of every replay, after `rd__clear_new_targets`
(`port/render/rd_upload.c` (`workClear`); `rd__set_work_clear` overrides the environment). A texel
the frame reads before drawing it then carries the colour downstream: in a target (`--target
NAME`), in a pass's picture (`--pass-dump DIR`), or on screen. The depth and stencil stay. It costs
one clear pass a target a frame, so it is a diagnostic only. One line at its first replay:

```
rd: work clear: every target but SCENE, DISPLAY and the feedback kept across frames (DISPLAY_HELD, FEED128, FEED_HELD) cleared to FF00FF before every frame (ICO_RD_WORK_CLEAR, a diagnostic): 13 named (SHADOW0 SHADOW1 ... WORK2_PAD) and N temporaries; a texel a frame reads before drawing it shows that colour
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

## The crack fill inside the local fog's pass

The crack filler is described in [RENDER.md](RENDER.md#crack-filling-above-1x). Where the fog
draws by the local read (`RD_FOG_LOCAL`: a tile-based GPU, and desktop Vulkan where its probe kept
the local reads, [below](#the-desktops-local-read-and-its-probe)), it draws inside the scene pass,
reading the depth as an input attachment. The copies end
that pass, so the fill is drawn in the pass begun again over SCENE and its depth, both loaded, the
depth readable (`rd__replay_begin_pass`, as `bindDraw` begins it), and that pass is left open:
`doFog` keeps it (`bindDrawKeep`: colour, depth and readability match) and its local-read barrier
orders the fill's depth writes before the fog reads them. One pass stores and loads SCENE once
fewer than a fill pass of its own followed by the fog's reopened pass (about 36 MB a replay at
1664 x 896). A readable depth's layout (`RENDERING_LOCAL_READ`, `port/rhi/vk/vk_cmd.c`
(`rhi_cmd_begin_render_pass`)) is an attachment the depth tests write, as every scene draw in that
pass does, so the fill's test-ALWAYS writes are allowed; the fill samples the copies, never the
attachment. The count moved before the fill (it reads the depth copy alone). A pass begun without
the depth readable is ended after the fill as on the other paths, with a line once. The fill's GPU
time, copies and draw, is the picture effect `crack fill` (`RD_PERF_POST_CRACK`): the 10 s
`effects:` line, the perf CSV's `gpu_crack_fill_ms` and the tool's `gpu:` line. In `--pass-times`
the folded pass is labelled `fog`. Tests: `rd_crack_tiler`, `rd_crack_tiler_on`,
`rd_crack_tiler_fold`, `rd_crack_tiler_fold_mali` ([TESTING.md](TESTING.md)); the fold's SCENE
equals the unfolded replay's byte for byte.

Nothing in `rd_crack_fill.c` asks for a tile-based GPU: the fold is taken whenever `doFog` draws by
the local read (`fogLocalFor`, which reads the profile's `fogLocal` and the fog's path, not the
class) and SCENE's depth was made for it (`RD_DST_USAGE_LOCAL_DEPTH`), so on desktop Vulkan it
follows the probe's verdict.

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
blur's copy of SCENE (4.9 MB at 2x), and fold the crack fill into the fog's pass. What they may
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
fog draws as in a game frame (the crack fill before it above 1x under Enhanced). Each replay's GPU
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
count of faces in front of the scene ([RENDER.md](RENDER.md#shadows)). The resolve
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
`box_reduce_ps`'s, the same expressions in the same order over the same `DrawCB` (SPIR-V and DXIL of
the two entries carry the same arithmetic instructions in the same sequence); at scale 1
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
start-up set holds the seven draws' and the reduction's pipelines as before and the one draw's
where the profile has it. The profile's start line and the summary say which: `shadow count one
draw`, `shadow count seven draws`, with ` ([dev])` or ` (self-test)` after it when that decided;
`rd_replay_tool --stats` prints `shadow count: N in one draw, M in seven draws` for the replay
(`RdPerfRecord.shadowOne`, `shadowSeven`).

**The self-test** (`port/render/rd_shadow_selftest.c`, run with DATE's on the fog self-test's first
run and again when SCENE is made at a new size): a frame of its own on SCENE (its colour as the
count target) counts 71 bands, band k at k in the top half and -k in the bottom half, the edges a
sub-pixel off the pixel grid, resolves it through both paths and compares the count at its GS size
byte for byte, with every band centre against the table:

    rd: shadow count: one draw (self-test: equal to the seven draws over 688x448, 142 band centres ok) on SCENE 688x448 at 1376x896 texels
    rd: shadow count: seven draws (self-test: one draw differs at 17 of 308224 texels, the first (40,300): 00040480 for 04040480) on SCENE 688x448 at 1376x896 texels

A difference turns `shadowOne` off (`(self-test)`); `[dev] shadow_one = true` keeps it ("kept by
[dev] shadow_one"); `ICO_RD_SHADOW_SABOTAGE=1` makes the table wrong at m = 1 so the failure path
runs (ctest `rd_shadow_selftest_sabotage`).

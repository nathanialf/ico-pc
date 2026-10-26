# The post effects' work buffers

The targets `staticBlur.c`'s effects and the screen softening draw through on their way back into
SCENE, their sizes at each frame width, the glow chain's rectangles in them, the column the glow's
blur leaves on a frame wider than the PS2's, what stays in them from one frame to the next, the
work clear that shows a texel read before it was drawn, and how the puddle's ripples sample their
reflection block between ticks, and where the crack filler is drawn on a tile-based GPU, and
(not a work buffer, kept here because RENDER.md is at its size cap) how a screen sprite redrawn under
a new key between ticks is paired. The sprites themselves are drawn by the GS
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
| `ico2/sugipon/src/puddle.c`, `port/render/rd_interp.c` | the ripples' texture shift between ticks (`drawRipples`; `reprojStShift`) |
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

The blur fold (`RdProfile.blurFold`, the tilers) draws a pair as one pass and computes pass 1's
texels in the shader with no coverage test, so it would not read the column; but its condition
(`rd__replay_blur_fold`'s `blurFoldOk`: every texel pass 2 weighs inside pass 1's coverage) fails
at every wide frame for that reason, so on a phone the pairs are drawn as two passes, as on the
desktop.

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
recorded; `rd__glow_edge_count` counts the strips (ten a glow frame at a wide frame).

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

The crack filler is described in [RENDER.md](RENDER.md#crack-filling-above-1x). On a tile-based
GPU the fog draws by the local read (`RD_FOG_LOCAL`): inside the scene pass, reading the depth as an input attachment. The copies end
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

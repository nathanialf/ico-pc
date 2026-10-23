# The post effects' work buffers

The targets `staticBlur.c`'s effects and the screen softening draw through on their way back into
SCENE, their sizes at each frame width, the glow chain's rectangles in them, the column the glow's
blur leaves on a frame wider than the PS2's, what stays in them from one frame to the next, and the
work clear that shows a texel read before it was drawn. The sprites themselves are drawn by the GS
sprite model ([RENDER.md](RENDER.md#post-passes), "The staticBlur sprites").

## Files

| File | What it holds |
| --- | --- |
| `port/render/rd_core.c` | the targets' sizes (`namedTargetDesc`), their scale (`rd__target_scale_of`: the work buffers at their GS size at every scale), their creation (`createNamedTargets`) |
| `port/render/rd_upload.c` | the clear of a new target (`rd__clear_new_targets`) and the work clear (`workClear`) |
| `port/render/rd_blur.c` | the strip over the glow's left edge (`rd__glow_edge_strip`) |
| `port/render/rd_blur_border.c` | the strip drawn after the blur's first pass (`glowEdge`, from `rd__blur_post_sprite`) |
| `ico2/sugipon/src/staticBlur.c` | the effects, their rectangles in the PS2's numbers and the port's widening (`SB_X`, `sbShrinkAt`, `sbShrinkSpan`, `sbWorkRect`) |

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

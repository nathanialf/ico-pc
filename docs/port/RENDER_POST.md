# The renderer's post passes

The passes [rd](ARCHITECTURE.md#terms) draws over a replayed frame and the effects that read its
targets: the full-frame sprites and the fades fold, the motion blur, the picture above 1x, the
shadows, the water's render-to-texture blocks, the fog path and its self-test and the crack
filling. Recording, the state, the replay, the dumps and the replay tool are
[RENDER.md](RENDER.md)'s.

## Post passes

`rd_post` records the post kinds that are plain GS sprites in the original as the register writes
and sprites the original routine issues, through the same `rd_*` calls, in the original's order.
They leak state into the rest of the list as the packets do (the fade leaves ALPHA 0x44 and PABE
0, the keep pass TEXA 80/80 and DISPLAY bound). Registers the original does not write are left as
they are; every pass that writes FRAME also records FBMSK 0 (`port/render/rd_post.c`, header
comment). The fade and letterbox sprites carry a key so the presenter can blend them between ticks;
the other post sprites snap (`port/render/rd_post.c` (`s_spriteKey`)).

| kind | source | what it records |
| --- | --- | --- |
| REDUCTION | `gsb_Reduction` | DISPLAY cleared, SCENE drawn into it at half height, bilinear, tinted, inside the border-crop scissor (2 pixels left and right; 8 lines top and bottom on a 512-line scene, 2 on 448, none on the Japanese disc: `gsb_Reduction_jpn`, `rd__reduction_crop`); both sprites through the GS sprite model |
| KEEP | `gsb_KeepFrameBuffer` | DISPLAY read as PSMCT24 drawn back over the scene at 112 grey |
| FADE | `gsb_fade` | a full-scene sprite, ALPHA 0x44, PABE 0, the fade colour |
| LETTERBOX | `gsb_scissorOnDemo` | two 58-line bars, ALPHA 0x64 with FIX the band level |
| BRIGHTNESS | `gsb_controlBrightness` | a white sprite, alpha the step, `gif_SetAlpha(1, 7, 0)` |
| AA_DOWNSAMPLE, AA_COMPOSITE | `gsb_antiAlias` (screen softening) | SCENE into AA0 (W/2 x 256), AA0 into AA1 (W/4 x 128); then AA1 and AA0 back into SCENE with LERP FIX |
| FILM_NOISE | `gsb_filmNoise` | the grain sprite over the bound texture, ALPHA 0x44 |
| FOG | `fog_DrawFog` | the fog sprite and its CLUT, drawn by `doFog` |
| COMPOSITE_FIX | effects | a textured quad with ALPHA and FIX; with `exactInt` an `RDC_EXACT_BLEND` |
| COPY | `gif_MoveImage` | a texture copy |
| MOTION_BLUR .. EYE_BLUR | `staticBlur.c` | one GS sprite each, recorded by `rd_blur.c` |

**The Japanese and Korean discs' reduction.** `gsb_Reduction_jpn` (`ico2/seki/src/GsBase.c`; the Korean build runs it too) builds the
same packet with two differences. Its border-crop scissor keeps every line (rows 0 to H/2 - 1; the
columns 2 to W - 3 as on the European disc), which the host follows: `rd__reduction_crop` returns
0 when the running disc's `reduction_crops_rows` is 0 (`port/core/region.h`), so the reduction
draws the whole height and the border mask, the deferred text's clip and the replay use no rows
either. And it writes TEST and ZBUF after the black clear sprite, not before, so on the PS2 that
sprite draws under the TEST and ZBUF the frame left; the host clears under TEST 0x30000 with the Z
write off on every disc. The textured sprite covers every pixel but the two columns at each side, so
only those columns could differ, and only if the leaked test failed the black sprite; not modelled.
The European picture is unchanged: `reduction_crops_rows` is 1 there and `rd__reduction_crop` is
the 2 or 8 it was (ctest `rd_present`'s border cases, which compute the expected rows from it).

`RD_POST_SHADOW_RESOLVE`, `RD_POST_BLUR` and `RD_POST_PRESENT_BLIT` are reserved numbers no caller
issues; `rd_post` records nothing for them (logged once) (`port/render/rd_post.c`).

**The staticBlur sprites.** `staticBlur.c`'s host path records its register writes as rd state and
each sprite as `rd_post` of its effect's kind: motion blur (DISPLAY read as RGB24, H/2 stretched to
H, LERP FIX), depth of field (SCENE to WORK1 to WORK0, six blur passes, four planes), glow (flare in
modes 0 and 1, bloom in mode 2 with HIGHLIGHT), aura (AURA_WORK, AURA_TAP, FEED128 feedback) and the
sun's eye blur (WORK0 to WORK3) (`port/render/rd.h` (`RdPostKind`)). `rd_blur.c` records each as an
`RdPostRec` with the corners, UVs, RGBAQ and Z as the GS gets them, the
[TEX0](ARCHITECTURE.md#terms) size and TFX, and `rd__blur_post_sprite` (`port/render/rd_blur_border.c`) hands it to `doBlurSpriteDraw`, which draws it through `fx_rect_vs` /
`fx_sprite_ps` in the GS integer arithmetic (`port/render/rd_blur.c`, header comment; the work
buffers, the glow's left edge on a wide frame, the work clear, a cleared source: [WORK_BUFFERS.md](WORK_BUFFERS.md)):

- coverage: pixels with `x0 <= X < x1` in 12.4, inside the scissor;
- UV stepped in integers, 12.4 (the division without the GPU's integer divide,
  [SHADERS.md](SHADERS.md#integer-forms)); nearest, or the GS bilinear on 4-bit fractions, each
  texel wrapped by CLAMP on the TEX0 size, clamped to the backing target and expanded by TEXA
  before filtering;
- TFX MODULATE, DECAL, HIGHLIGHT, HIGHLIGHT2, clamped at 255;
- the alpha test, DATE against the destination before the sprite, the Z test in hardware;
- `((A - B) C >> 7) + D` on integers with PABE, COLCLAMP, FBA, written to UNORM8 as k / 255.

The destination is read from a snapshot taken just before the sprite, so the result is the GS
integer arithmetic of every pass, feedback or not (`port/render/rd_blur.c`). A sprite that stores
every texel of its target (its coverage holds every GS pixel the target's texels stand for, the
scissor is the whole target, no alpha test but FB_ONLY or RGB_ONLY, no DATE, no Z test but ALWAYS,
every channel written) needs no copy: it draws into the snapshot texture, which becomes the
target's colour, and reads the old colour, which becomes the snapshot, the picture before the
sprite as a whole copy would hold it (issue 103). The snapshot texture is made with the target's
own usage for that. The swap is no pass of the [pass ring](RENDER.md#the-pass-ring) (a copy is);
`ICO_RD_SNAP_SWAP=0` keeps the copy, for the A/B (`rd_replay_tool_snap_swap`: the same bytes on
every blend path at 1x and 2x) (`port/render/rd_replay.c` (`blurCoversTarget`, `blurSwapTarget`,
`snapEnsure`)). With Full pixel (on,
or off in the game on a cropped frame such as 16:9, where the presenter masks the border over the
same picture: `RdSettings.borderMask`, `rd__frame_cropped`,
[DISPLAY.md](DISPLAY.md#the-native-frame); at 4:3 and 48:9 off keeps the recorded scissor) the
reduction's textured sprite is drawn with the whole target as scissor and both axes clamped, so the
border shows the picture SCENE holds (`port/render/rd_replay.c` (`borderOpen`, exported as
`rd__replay_border_open`), `port/render/rd_blur_border.c` (`rd__blur_post_sprite`)).

**The open border's outer columns (issue 129).** The PS2 never shows GS columns 0, 1, W - 2 and
W - 1: the reduction's scissor is 2 .. W - 3 (`port/render/rd_post.c` (`postReduction`)) and the
TV's overscan hides more. The game does not always draw them: in one room (the hanging chain up to
the window, before the cage room) it leaves the top of column 0 uncovered, the fog paints depth 0
with its table's entry 0 (near white in a lit room), and the glow spreads it about twenty output
pixels in, a bright band at the left edge of a 2340x1080 phone whose 832-column frame is its exact
width. Where the frame is its exact width (`rd__frame_cropped` false: 4:3, 48:9, 2340x1080 and
every aspect whose `384 * aspect` is whole) and the border is open, the reduction's sprite is
drawn again over those columns as their neighbours: `rd__blur_border_strips`
(`port/render/rd_blur.c`) makes two strips of the sprite's record, `{x0, y0, o + 32, y1}` (pixels 0
and 1; `o` the 12.4 coordinate of pixel 0) and `{o + 16 (W - 2) - 4, y0, x1, y1}`, each with
`u0 = u1` the U `fx_sprite_ps` computes at pixel 2 (or W - 3) from the recorded UVs (`U(X) = u0 +
(X - x0) (u1 - u0) / (x1 - x0)`, truncated), so in the GS model each strip pixel reads the same two
texels at the same weights as its neighbour column: the same bytes. The mirror's half texel
(`rd__blur_uv_rect`) moves both ends of a strip's U as it moves the sprite's. The hardware sprite a
scaled target draws (`rd__blur_screen_fallback`) gets the same from `rd__blur_border_verts`: each
of its four vertices' S constant, the S the pair has at that pixel's coordinate, which
`rd__blur_snap_verts` then moves as it moves the sprite's. The order is the scissor opened and
CLAMP set, the sprite, the left strip, the right strip, the state put back
(`port/render/rd_blur_border.c` (`rd__blur_post_sprite`, `drawStrips`), each strip through
`rd__replay_blur_sprite_draw`, `doBlurSpriteDraw`'s export, in a payload of its own). A cropped
frame (16:9) draws no strip; with Full pixel off the border stays closed. Measured on lavapipe
(the 48:9 deja vu dump at 3840x720, 1.875 texels a GS pixel), the hardware strips cover DISPLAY
texels 0 .. 3 and 3837 .. 3839 and are one value a row there; texel 3836 is the main sprite's. The top and bottom rows the reduction's scissor kept black are not repeated:
they show what SCENE holds. One line the first time, `rd: display: the border is open
at the frame's exact width (W GS pixels): its two outer columns a side repeat columns 2 and W-3
(reported once)`. `rd__set_blur_border_repeat(false)` draws without them (tests: the present pins measured without
the strips, `rd_present_test`'s open border cases); `rd__blur_border_strip_count` counts the
strips drawn, which `rd_log_frame_state`'s line names.

**SCENE's outer columns (issues 129 and 26).** The strips mend the reduction's own columns, but the
glow reads SCENE before the reduction: list 7's flare and bloom reduce SCENE into the work
buffers, so the fog-painted columns also spread about ten GS pixels into the picture, about twenty
output pixels at 2x. That reaches the screen on a cropped frame too: issue 26's picture (4K,
16:9, Full pixel off, the room with the hanging chain) shows the band at the left edge, rows 40 ..
260 of 964, where the present already leaves the columns out. So the fog of a frame on SCENE arms
a repeat (`rd__scene_border_arm`, the last line of `doFog`), done before the frame's first post
sprite that samples SCENE (`sceneRepeatDue` in `rd__blur_post_sprite`: the glow's first
reduction, the depth of field's, at the latest the reduction into DISPLAY), where SCENE's pass
ends anyway, so the repeat adds no pass on a tiler's local fog read and an open MSAA epoch closes
where it would have (`port/render/rd_blur_border.c` (`sceneRepeat`)). The repeat copies SCENE's
texels of GS column 2 into those of columns 0 and 1, and of column W - 3 into W - 2 and W - 1,
every row, the colour alone (the depth stays the game's, which the edge probe reads): the texels
of GS column x start at `ceil(x sx - ox)` (texel t lies at GS `(t + ox) / sx`), the left fill is
laid right to left from column 2 and the right fill left to right from W - 3, the outermost copy
cut at the edge, so at a whole scale each column is column 2 (or W - 3) texel for texel. The RHI
cannot copy a texture into itself, so the two source columns go through a small texture of their
own first (`rd border columns`, the columns' widths by SCENE's height, freed with the scene's
caches and at shutdown: `rd__scene_border_free`), and bumps `writeSerial` (DATE's snapshot). It
applies in every place of the columns (`rd__scene_border_state`) but one: the border open at the
exact width (`RD_BORDER_OPEN`), the frame cropped at present (`RD_BORDER_CROPPED`), Full pixel's
window (`RD_BORDER_WINDOW`, `ox > 0`: its crop is 0 to 8 GS pixels a side, so a 2560 x 1080
monitor's or a 2532 x 1170 phone's window holds texels of columns 0 and 1, which are copied; where
`columnTexel(2) <= 0` nothing is), and the border closed on a frame wider than 512 GS pixels
(`RD_BORDER_CLOSED` at 2340 x 1080 or 48:9 with Full pixel off: the reduction shows black there,
the glow reads SCENE). Only the closed border of the 4:3 frame (512, the GS's own DISPLAY with its
border) leaves SCENE as the game drew it (the camera's picture: the console's except inside
triangles it dropped, [DIVERGENCES.md](DIVERGENCES.md) VD-29), which the native-frame gate's 4:3
hashes hold under `[dev] region_rule = ps2`. The strips stay with it: the
reduction's column 2 blends a quarter of column 3 (the PS2's +0.75), so DISPLAY's outer columns
drawn from SCENE's repeat alone would be column 2 without that quarter, not DISPLAY's column 2.
One line the first time, `rd: border: the frame's outer two columns a side repeat columns 2 and
W-3 on SCENE after the fog (W GS pixels; <the place>; L and R texels a side; reported once)`, once
for each place; `rd__scene_border_repeat_count` counts
the repeats (`rd_log_frame_state`'s line), `rd__set_scene_border_repeat(false)` leaves SCENE as
drawn (tests).

**The frame-size sprites' plan.** Before its snapshot, `doBlurSpriteDraw` hands the sprite to
`rd__fx_plan_draw` (`RdProfile.fxPlan`, on in every class; `[dev] fx_plan`, `ICO_RD_FX_PLAN=1|0`),
which draws it inside the open pass, without the snapshot, where the same bytes come another way
(`port/render/rd_fx_plan.c`, file comment):

| sprite | condition | how it is drawn |
| --- | --- | --- |
| cleanUpFB's two sprites into SCENE, the glow's WORK0 fill | untextured, blending, no DATE, no AFAIL RGB_ONLY, and each colour channel's blend the destination for every Cd and Ad or one value for every Cd and Ad (`rd__fx_fold`, over the 256 values of each) | `fx_sprite_ps` without the blend and the destination read, the constant channels as RGBAQ, the destination channels masked (the colour mask A alone or whole), the depth state the sprite's; the alpha alone without a Z test in the open pass with that pass's depth attached, its test ALWAYS and no Z write (`fxAttachDepth`), where the open pass is on the sprite's target with the state's depth: cleanUpFB's sprites stay in the fog's pass |
| the glow's paste WORK3 into SCENE | Cs * FIX + Cd, Cs * As + Cd, Cd - Cs * FIX or Cd - Cs * As, COLCLAMP 1, no PABE, no DATE, no AFAIL RGB_ONLY | `fx_frame_ps` writing the magnitude of `(A - B) C >> 7` (`FXF_OUT_PREMUL`) through the premultiplied add (ONE, ONE) or reverse subtract, alpha ONE, ZERO: the UNORM8 sum is the GS's clamped integer sum, the subtract's term the GS's `ceil(Cs C / 128)` |
| the motion blur DISPLAY into SCENE, any other destination read | `RdProfile.fxDstLocal` and the target's passes readable (the scene class under `dateStencil`), not sampling its own target | `fx_local_ps`, the destination as the input attachment (`SubpassLoad`) inside the open pass after a by-region barrier (`RHI_LOCAL_READ_COLOR`; `RHI_LOCAL_READ_BACK` after it); `fx_rect_vs` draws one triangle, so the texel read is the one before the sprite |
| the glow's mask SCENE into WORK2 | MODULATE with RGBAQ's RGB 0, no blend, no destination read | `fx_frame_ps` computing the alpha alone (`FXF_ALPHA_ONLY`): RGB is `(t * 0) >> 7`, 0; where the glow's chain allows, not drawn at all: its reduction computes it ([The glow's mask in its reduction](#the-glows-mask-in-its-reduction)) |

DATE and AFAIL RGB_ONLY read the destination's alpha: the constant fold and the premultiplied
blend do not take a sprite under them, the local read does, and `fx_local_ps` tests DATE
(`FXF_DATE`, `fx_sprite_out`) against the alpha `SubpassLoad` returns at the fragment's own texel,
the value the snapshot holds (one triangle: no other fragment of the sprite wrote that texel). No
post sprite takes DATE's stencil bit on any path (**DATE through the stencil bit** is `doScreen`'s
and the VU draws'; the fx pipelines have no stencil test), so with `fxDstLocal` a post sprite under
DATE takes no snapshot and binds the depth only for its own Z test or Z write (`rd__blur_key`); the
store plan does not count its DATE as a touch of the depth ([WORK_BUFFERS.md](WORK_BUFFERS.md#the-store-plan)).
The 10 s pixel diet line's `local (N post sprites' DATE by local read)` and the replay tool's
`--stats` count them (`RdPerfRecord.fxLocalDate`); ctest `rd_blur` (l) checks post mode 7's DATE
subtract into SCENE by local read against the GS model. The aura's paste
(`pasteFeedBackAreaToFB`, aura mode 2, the phone's stage 6 frames) is not a DATE sprite: Z ALWAYS,
no Z write, LERP As, so its pass on SCENE binds no depth or stencil at all.
A sprite that samples its own target keeps the snapshot; so does every sprite that reads its
destination under an exact blend the player opted into or in a frame with the MSAA epoch, and one
whose key the pipeline cache cannot give (looked up before anything is recorded). The four forms
share `fx_sprite_ps`'s coverage, UV and tests (`port/shaders/fx_sprite.hlsl`). Their keys are in the
start-up set (`rd__fx_enumerate`: the colour mask A of `fx_sprite_ps`, once more with the depth
attached under Z ALWAYS without Z write, `fx_frame_ps` without blend
and with the premultiplied add and subtract, `fx_local_ps` where the profile has it, each in
`rd__enumerate_reachable_blur`'s depth states). `fxDstLocal` is on where the profile has
local read (`[dev] fx_dst_local`, `ICO_RD_FX_DST_LOCAL=1|0`); its self-test runs after DATE's
(`rd_fog_selftest`): a frame on SCENE of four cells under an untextured LERP FIX 0x20 sprite,
replayed with the local read and through the snapshot, the cells' centres read back against the
GS's arithmetic; a wrong pixel turns the flag off (`port/render/rd_fx_plan.c`
(`rd__fx_local_selftest`)). The reduction's black sprite on a scaled DISPLAY (a hardware sprite,
`rd__blur_screen_fallback`) is its clear where it covers every texel with nothing else in force
(no texture, blend, test, FBA, depth, the colour mask and the scissor whole, no Full pixel crop),
its corners on the exact viewport's texel centres counted with the top-left rule; the next pass on
DISPLAY takes the clear as its load op (`port/render/rd_fx_plan.c` (`rd__fx_reduction_clears`),
`port/render/rd_replay.c` (`doBlurSpriteDraw`)). Each replay's record counts the sprites drawn
each way (`RdPerfRecord.fxConstant` with the black sprite's clear, `fxAdditive`, `fxLocal`;
`rd_replay_tool --stats` `pixel diet:` line), and the log has one line a run per way:

```
rd: fx plan: a staticBlur sprite drawn by the constant fold (a masked draw in the open pass), without its snapshot (reported once)
rd: fx plan: the reduction's black sprite on a scaled DISPLAY drawn as its clear (reported once)
rd: fx plan: a staticBlur sprite without a Z test drawn by the constant fold in the open pass on SCENE with its depth attached and unused, not in a pass of its own (the glow's cleanUpFB in the fog's pass; reported once)
rd: fx local read: on (self-test: local ok 4, snapshot ok 4) on SCENE 512x512 at 1024x1024 texels
```

### The glow's mask in its reduction

List 7's glow (`ico2/sugipon/src/staticBlur.c` `makeFullScreenFlareBefore` and `After`) fills
WORK2 (`fillWork2`: one untextured sprite over the frame), draws the mask into it
(`makeMaskPatternToWork2`: SCENE 1:1, MODULATE by RGBAQ (0, 0, 0, 128), the GS bilinear of SCENE's
alpha, ATST LESS AREF 129 AFAIL KEEP, no blend; the fx plan's alpha alone above), then reduces
WORK2 into WORK0 (`reduceWork2ToWork0`: the box above 1x, the GS bilinear at 1x). WORK2 has the
frame's size, so at the PowerVR phone's 2364 x 1080 the mask's pass stores 10.2 MB that the
reduction loads again and nothing else reads (the title's `glow WORK2 1 draw 1.23 ms`, glow rooms
up to 4.7 ms). The fold draws neither the fill nor the mask: the reduction is drawn with SCENE as
its texture through `fx_frame_ps` with `FXF_GLOW_MASK` (`port/shaders/fx_sprite.hlsl`
`fx_glow_texel`, `fx_glow_mask`), which computes every WORK2 texel its kernel loads as the mask's
sprite stored it over the fill, where it loads it (`port/render/rd_glow_fold.c`, file comment):

- the mask's fragment at that texel (its `SV_Position`, the texel's centre) through
  `fx_sprite_ps`'s own lines in WORK2's FrameCB (the scale, the Full pixel crop's left-out texels,
  XYOFFSET as `frameRefresh` gives the mask's pass), the coverage test against the mask's rect (the
  fill outside it), U and V (`u0 + (X - x0)`: the mask's UV span is its rect's, and
  `gs_div_trunc_rcp` of `n d` by `d` is `n`), the GS bilinear of SCENE's alpha times RGBAQ's alpha
  (`fx_alpha_only`'s arithmetic, its taps in one loop), the alpha test (AFAIL KEEP and ZB_ONLY
  leave the fill, FB_ONLY writes) and FBA;
- then the reduction's own kernel over those texels (`fx_sprite_texel`'s: the above-1x box or
  B-spline, the GS bilinear or nearest; integer sums in the same order), its TFX and its tail.

The same operations on the same operands, so the same integers: what is left out is the RGBA8 store
and load between the two draws (the mask wrote `k / 255`, the reduction read
`floor(x * 255 + 0.5)`, which is `k` for every `k`). The half texel the pixel centre adds is read
from the DrawCB (`g_tex.y`), so no compiler folds it with the 0.5 the sprite's lines take off
after it; the sums before the divides are the mask's own, in its order. The mask's words ride in
DrawCB fields the reduction does not read (`aux0`, `blend.xyz`, `tex.xy`;
`port/render/rd_fx_plan.c` `fxGlowFold`). The key is the alpha alone's (the start-up set has it):
no new pipeline. One full-frame pass fewer a glow picture on a tile-based GPU (the fill is the
mask's CLEAR load there), two on a desktop (the fill's clear is a pass of its own), and WORK2 is
neither stored nor read.

**With the sun's fans.** Whenever the sun is on screen, `dispSun` draws two fans of triangles into
WORK2 between the fill and the mask (blended, Z-tested against SCENE's depth): 6 of the 14 corpus
glow frames, and most of a reporter's PC session of 2026-11-04 (its 10 s lines name the fans). The fans' texels are the
rasteriser's (their coverage and Gouraud colours), which no shader recomputes bit for bit, so they
are not folded: the fill and the fans are drawn as without the fold, only the mask is not, and the
reduction binds WORK2 in the destination's slot (t2, `FXF_GLOW_KEPT`) and loads its texel where
the mask's sprite would have kept it (outside the rect, the alpha test failing): the bytes the
reduction would have loaded there. Elsewhere it computes the mask's texel as above. The mask's
pass is the one gone (WORK2's 10.2 MB loaded and stored again for it), so one full-frame pass
fewer there too; WORK2 is drawn and read.

Where it holds (the scan, in `rd__store_plan_scan`'s one walk of the lists; `rd__glow_fold_plan`
over a frame of its own for the tests):

- the frame touches WORK2 (a draw into it or on its depth, its texture, a copy or exact blend
  naming it, a command the scan does not know) with the fill, the mask and the reduction in that
  order, a clear or a draw before the fill (which the fill covers) and any command between the
  fill and the mask (the fans: then the mask alone is folded) aside;
- the fill: a post sprite into WORK2, untextured, no blend, no alpha test that can fail, no DATE,
  the colour mask whole, no Z test or Z write, the scissor whole, every texel's GS pixel inside its
  rect: WORK2 holds its colour (with FBA's MSB) everywhere the mask leaves;
- the mask: a post sprite into WORK2 textured from SCENE's colour (RGBA32), MODULATE with RGBAQ's
  RGB 0, no blend, no DATE, AFAIL not RGB_ONLY, the colour mask whole, no Z test or Z write, the
  scissor whole, its UV span its rect's on both axes, WORK2 and SCENE of one size, scale and crop
  (so the mask is at the GS's own filter), no glow edge strip;
- nothing between the mask and the reduction writes SCENE's colour (a draw into it, a clear, a
  copy or exact blend naming it, a command the scan does not know; draws into other targets may
  come between): SCENE is read by the reduction as the mask read it (its
  border repeat is done at the mask, as without the fold; the crack fill is placed before the
  mask, its first reader of SCENE);
- the reduction: a post sprite (not the display's reduction) from WORK2's colour (RGBA32) into
  another target, no destination read, the mask's TEX0 size, no glow edge strip;
- the switch on (by default on the immediate-mode device classes only, below), the fx plan on
  with `fx_frame_ps`, a device, no MSAA, no pass window.

In 3 of the 14 corpus glow frames (the issue 163 rooms) the lamps' meshes draw into WORK2 after
the mask, blending over its texels: the chain is drawn there, and the frame counted kept with its
reason. The replay: `blurPostSprite` (`port/render/rd_blur_border.c`) leaves the planned fill
(not with the fans) and mask undrawn (`rd__glow_fold_skip`, after SCENE's border repeat), their state kept; at the reduction
`rd__fx_plan_draw` takes the fold (`rd__glow_fold_due`). A reduction that cannot take it (no
pipeline, a target made again) has the fill and the mask drawn first with their states
(`rd__glow_fold_undo`) and reads WORK2: the same bytes, and the fold off from then on.

WORK2's own bytes are then not the mask. Nothing in the frame reads them, and the next frame's fill
covers them; a frame that reads WORK2 before its fill after a folded one turns the fold off from
then on (logged). A reader outside the replay (`rd__read_target`: the tests, `rd_replay_tool
--target` and `--pass-dump`) would see them too, so without an output (a renderer whose targets
its caller reads) the fold is off unless `ICO_RD_GLOW_FOLD=1` asks for it, and a read of WORK2
after a folded replay is logged once. `[dev] glow_fold = true|false` (`ICO_RD_GLOW_FOLD=1|0`) is
the A/B ([CONFIG.md](CONFIG.md#dev)); not set, the fold follows the device profile's `glowFold`
wherever the frame is presented: on on the immediate-mode classes (desktop Vulkan, D3D12, D3D11,
OpenGL), off on every tile-based one (Adreno, Mali, PowerVR, Apple, another tiler;
[RENDER.md](RENDER.md#the-device-profile)). The profile's start line and the summary say `glow
fold on` or `glow fold off` (`([dev])` after it where the key set it).

**Why not on a tile-based GPU.** The fold trades a full-frame pass for arithmetic in a small one.
On a tile-based GPU the mask's pass is cheap: 2.55 M fragments at 2364 x 1080, each four reads of
SCENE's alpha, its tiles written on the chip and stored once. The folded reduction is a 424 x 128
pass (54 K fragments) whose box loads about 7 x 10 WORK2 texels a WORK0 texel (5.58 x 8.44 texels
of WORK2 each), and runs the mask at every one of them: about 3.8 M mask evaluations (each texel
of WORK2 about 1.5 times, the boxes overlapping), each with its four dependent SCENE reads, in a
loop with branches, over too few fragments to hide their latency. On the PowerVR phone (DXT-48,
2364 x 1080 Enhanced) every 10 s block that folded without the sun's fans has `glow WORK0
424x128 12.0 passes` at 10.42 to 11.41 ms, against 2.28 to 3.27 ms for 9.5 to 11.3 passes before
the fold and about 0.25 ms a WORK0 pass in the blocks that kept the chain: the folded reduction
pass alone about 8 ms, against the mask's pass's 1.2 to 3.7 ms it saved (`glow WORK2` before the
fold and on the title). Lavapipe, which has no tiles, shows the trade the other
way on the phone's dump of a glow room (frame 2426 at 2364 x 1080, `ICO_VK_FAKE_LIMITS=moltenvk`): the folded
reduction 66.2 ms against the mask's 50.5 ms and the plain reduction's 24.5 ms, the presented
picture the same bytes.

```
rd: glow: WORK2 fold: the glow's mask computed inside its reduction from SCENE's alpha (fx_frame_ps), WORK2 neither drawn nor read: one full-frame pass fewer (frame N; reported once)
rd: glow: WORK2 fold: the glow's mask computed inside its reduction from SCENE's alpha (fx_frame_ps), WORK2 as the fill and the sun's fans left it read where the mask keeps a texel: the mask's pass not drawn, one full-frame pass fewer (frame N; reported once)
rd: glow: WORK2 fold: the mask drawn into WORK2 and read by its reduction: <reason> (<detail>; frame N; reported once for this reason)
rd: glow: WORK2 fold: <why>: the fill and the mask drawn before the reduction after all (frame N); the fold is off from now on
window: glow: WORK2 fold: N folded (F reading the sun's fans' WORK2), M kept (K <reason>, ...)
```

The 10 s line counts the replays of its window (`RdPerfRecord.glowFolded`, `glowFoldRead`,
`glowKept`, `glowWhy`, the kept ones by reason); `rd_replay_tool --stats` prints `glow: WORK2 fold
on|off: N folded (F reading the sun's fans' WORK2), M kept (reason)`; the glow pass count (`glow N passes`, the 10 s `passes per replay`) is one fewer
where it folds. The reasons (`rd.h` `RD_GLOW_FOLD_WHY_*`): the lamps, SCENE drawn
between the mask and the reduction, WORK2 drawn or read elsewhere, the chain not in the fold's form, MSAA, the fx plan off, a reduction that
could not take it. Tests: `rd_blur` (k) and its (r) scan, `rd_glow_cheap_ab`
([TESTING_PROGRAMS.md](TESTING_PROGRAMS.md#portrendertest)).

### The fades fold

List 11 ends with the game's full-frame post sprites (`ico2/seki/src/GsBase.c`'s post pass order:
the brightness, the keep sprite, the fade, the letterbox): the brightness a white sprite, ALPHA 0x44
at alpha = the step; the fade the fade colour, ALPHA 0x44 at its level; the letterbox two black
bars, ALPHA 0x64 at FIX = the bars' level, Z write on. Each is a GS lerp `((Cs - Cd) C >> 7) + Cd`
of one colour over SCENE, and the reduction at the end of list 12 then reads the whole of SCENE
into DISPLAY. On a tile-based GPU those sprites cost a pass on SCENE of their own when nothing else
draws in list 11: SCENE's colour and depth loaded and stored, about 2.65 ms at Auto on the PowerVR
phone. The fold draws them in the reduction instead: up to four steps (the lerp's colour, its
factor C, the alpha it writes, the rows it covers) applied in the sprites' order to every SCENE
texel the reduction reads, and the sprites are not drawn (`port/render/rd_fades_fold.c`, file
comment; `port/render/rd_replay.c` (`doScreen`) asks `rd__fades_fold_skip`). It is how the game
draws them on every device and backend, with no setting (the measured bound below,
[DIVERGENCES.md](DIVERGENCES.md) VD-27).

The plan (`rd__fades_fold_plan`, the frame read once before the walk from the store plan's scan,
`rd__store_plan_scan`; the store plan does not count a folded sprite as a touch of the depth) takes
the run of such sprites at the end of list 11, after the last action that is not one: each one
sprite into SCENE, untextured, ABE with a lerp (ALPHA 0x44 or 0x64; PABE with As's MSB clear writes
Cs: factor 0x80), the factor at most 0x80 (the result stays in 0 to 255), the colour mask whole, no
alpha test that can fail, no DATE, no AA1, no Z test, the scissor the whole target, every column
covered and every row or the rows from the top or to the bottom (a bar); two bars of one colour,
factor and alpha are one step. The deferred text's quads (drawn on the output), the overlay text
and NOPs between are passed over. A sprite that writes Z (the bars) folds only while the present
does not read SCENE's depth (the effects depth with an effects program loaded,
`rd__effects_depth_wanted`; the setting alone reads nothing) and no replay found SCENE's depth read before the
frame clears it (a keep frame on the depth the frame before left; logged once, the bars drawn from
then on); such a sprite is otherwise drawn and ends the run. List 12 must hold nothing but state,
the overlay and deferred text, the reduction's black sprite into DISPLAY and its textured sprite
reading SCENE's colour (no blend, no DATE, bilinear, its tint not black), and nothing after it, so
the reduction is SCENE's only reader after the sprites (the border's column repeat copies whole
columns before it, which the steps, the same on every column, commute with). No fold under a pass
window, in a frame with MSAA (the epoch's resolve is not the lerp's), past four steps, or above 1x
for a bar while the reduction resamples SCENE through the above-1x kernels (foreseen as
`doBlurSpriteDraw` picks them, `rd__set_a1x_gs_keep` included).

The rounding (`roundingRefusal`): a step whose factor is 0 (Cd kept) or 0x80 (Cs) stores the same
level under every blend and every filter, one value over the rows it covers; a step between them
rounds. Counting, on each kind of row (between the bars, in a bar), the steps between levels after
the last 0x80 one, the plan takes any number at scale 1 under an exact blend, one at scale 1 under
the fixed-function blend (each such step a level from the blender's at most; two would add their
levels), and one above 1x under either blend (the filter rounds its sum and the step its product:
by a CPU model of a rounding and of a truncating bilinear in `rd_fades_fold_test.c` some channels
can end two levels apart for one step of a small factor; measured on the dumps, below, one level
at most on a small share of the pixels). So above 1x the steady letterbox bars, a fade at full
black and the brightness setting's step fold; a fade between levels with the bars between levels
on their rows (two steps there) is drawn. The brightness step is in every frame of play (white at
7/128 at the phone's setting), so above 1x on a tile-based GPU list 11's pass on SCENE went with it
whenever nothing else drew in list 11: 1.2-1.3 ms a picture on the PowerVR phone in play and on the
menus (its log of 2026-11-02). Whether the sprites blend exactly is the asked exact mode's own usage
bit on SCENE (`rd__fades_fold_exact`: `RD_DST_USAGE_STORAGE` under storage,
`RD_DST_USAGE_ATTACHMENT` under attachment).

In the reduction:

- at scale 1 (Original; `fx_sprite_ps` with `FXF_FOLD`, `port/render/rd_replay.c`
  (`doBlurSpriteDraw`), `rd__fades_fold_fx`) the steps are applied to each of the bilinear's four
  taps before the GS filter (`port/shaders/fx_sprite.hlsl` (`fx_fold4`),
  `port/shaders/gs_math.hlsli` (`gs_fold_ch`)): every tap is the texel SCENE would have held.
  Under an exact blend (`[dev] blend = attachment|storage`) the sprites store the GS's floor and
  so does the step: DISPLAY and the present byte for byte. Under the fixed-function blend (the
  default) the sprites store the nearest level (VD-7); the step adds 64 before its shift, the
  nearest level too, but for the blender's own ties and precision: a level on SCENE's texel, two
  at most on DISPLAY after the reduction's filter (measured below; [DIVERGENCES.md](DIVERGENCES.md)
  VD-27);
- above 1x (the hardware sprite, `rd__blur_screen_fallback`; `sprite_ps` with `ICO_DF_FOLD`,
  `port/render/rd_pixel_plan.c` (`rd__fill_draw_cb_extra`), `rd__fades_fold_screen`) the steps
  follow the filter on the sampled texel (`port/shaders/sprite.hlsl` (`sprite_fold`)): with
  factors 0 and 0x80 alone (above) the rows a step covers hold one value, which the filter keeps,
  so this is the filter of the stepped texels byte for byte. Where the filter's two rows straddle
  a bar's edge each row is sampled alone, stepped by its own steps, and the two weighed as the
  filter weighs them (the shader's weighing against the hardware's: a level at most, on those rows
  alone). The bars' rows are SCENE's texel rows the bars covered, their corners snapped to whole GS
  pixels as `snapAxis` draws them (`rd_fx_plan.c` `fxAxisCovers`'s mapping);
- DISPLAY's alpha is the last step's, what SCENE's alpha would have been.

SCENE then holds the picture without the sprites (its pass dump differs by design,
[WORK_BUFFERS.md](WORK_BUFFERS.md#the-store-plan)); DISPLAY, the present and the next frames read
DISPLAY. A reduction that does not take a planned fold (a way the plan does not foresee) leaves the
frame without the sprites; it is logged and the fold is off from then on. With the menus up (the
pause menu's layout drawn after the fade) or film noise, the keep sprite or text drawn into SCENE
after the sprites, nothing folds. The saving on a tiler is the list 11 pass's load and store of
SCENE's colour and depth only when list 11 then has no other draw; elsewhere one full-frame draw.
The fold has no setting. Measured on lavapipe against the stored pass dumps of the corpus with the
sprites drawn (`build-host/tmp/v095/after/w3/pd`, machine-local; the fixed-function blend; `build-host/tmp/v098/evidence/cheap/ab.sh`): at Original 4:3, 25 of the
30 corpus dumps fold 1 to 4 sprites and DISPLAY differs on 24 of them, on 1.0 to 15.3 % of its
pixels, by one level on most of those and two at most (none more); at Enhanced 2x 4:3 and at the phone's fractional
scales (2364x1080 at 2.189 under the PowerVR profile fakes, eight dumps wide enough; 1440x1080 at
4:3) only the 2D frame `plain/rd-00300` folds (two sprites, steady black bars) and DISPLAY and the
present are the same bytes. The brightness step above 1x, modelled the same way on main's tool (the
step applied to DISPLAY replayed without the sprites, against DISPLAY with them;
`build-host/tmp/v098/evidence/gpuround/scripts/foldsweep.sh`, the PowerVR fakes): at the phone's
Resolution Auto on its 16 F12 frames within one level on 0.17 to 15 % of DISPLAY's pixels, at
Enhanced 2x 4:3 within one level (one pixel two) on 5 to 30 %, at a 1920x1080 window's Resolution (2.26 x 2.41, the reduction again texel for texel) within one level on 0.17 to 15 % (four dumps); under the exact blend (`[dev] blend = attachment`) at the phone's setting byte for byte and at Enhanced 2x 4:3 within one level on 4.6 %. `ICO_RD_FADES_FOLD=0` is the tests' way to draw the sprites into SCENE
as before (`rd_gsbase` reads SCENE back; `rd_present`, `rd_crt`, `rd_interp`, `rd_pixel`,
`rd_boot_present` and `menu_text` hold golden bytes of frames with them; and `rd_fades_fold_ab`'s
drawn arm); it is no config key. Each replay's
record counts the sprites folded (`RdPerfRecord.fadesFolded`; `rd_replay_tool --stats`: `fades fold
on: N sprites folded into the reduction`; the window's 10 s `pixel diet` line: `fades fold N`), the
summary line says `fades fold on` or `off`, and the log says:

```
rd: fades fold: 3 of list 11's last sprites applied by the reduction instead of drawn into SCENE (frame 720; the whole frame (0, 0, 0) at 127/128, bars (0, 0, 0) at 64/128; the steps on SCENE's texels before the GS filter; SCENE 512x448 texels, rounding the nearest level (the fixed-function blend); within two levels of the sprites drawn: DIVERGENCES.md VD-27) (reported once)
rd: fades fold: list 11's last sprites are drawn into SCENE: a sprite writes SCENE's depth, which the effects depth reads (frame 720; the first 8, then every 100th: 1 so far)
rd: fades fold: list 11's last sprites are drawn into SCENE: two steps between levels on one row under the fixed-function blend (frame 720; the first 8, then every 100th: 2 so far)
rd: fades fold: list 11's last sprites are drawn into SCENE: a step between two levels after the hardware filter (above 1x), whose rounding is not the sprite's (frame 720; the first 8, then every 100th: 3 so far)
rd: fades fold: SCENE's depth is read in list 11 before this replay clears it (frame 1234): the letterbox bars, which write it, are drawn into SCENE from now on (reported once)
rd: fades fold: the reduction took another way (the hardware sprite, not as planned): frame 1234's picture lacks list 11's last 3 sprites; the fold is off from now on
```

ctest `rd_fades_fold` (the step against the GS blend, rd_gsbase's fade, brightness and letterbox
cases stepped texel by texel against the sprites' bytes, the plan on recorded frames) and
`rd_fades_fold_ab` (the fade dumps with and without the fold,
[TESTING_PROGRAMS.md](TESTING_PROGRAMS.md)). Not built:
a frame differing from the last only in a fade's level could rerun the reduction and the present
alone (the still re-present shows a picture again only on an unchanged frame hash,
`port/render/rd_reshow.c`).

### The motion blur under Smooth

Options > Effects > Motion blur: Smooth (`[video] effect_motion_blur_mode = "smooth"` with
`effect_motion_blur` on, `RdSettings.motionBlurSmooth`; issue 112). The game's motion blur
(`ico2/sugipon/src/staticBlur.c` (`MotionBlur`)) is one sprite in list 7, the previous DISPLAY
(TBP 0, RGB24, H/2 lines stretched over H) over SCENE with ALPHA mode 2, `(Cs - Cd) FIX / 128 +
Cd`, FIX the stage's `motionBlur` (typically 32). Original keeps it once a tick: every present of
a tick reads the DISPLAY the tick started from (`DISPLAY_HELD`, `rd_interp.c` (`feedback`)), so
between ticks the trail stands still while the picture moves. Under Smooth `rd__blur_post_sprite`
hands the sprite to `rd__blur_smooth_sprite` (`port/render/rd_blur_smooth.c`, file comment),
which per present i keeps

    A_i = S_i + a' (A_i-1 - S_i),   a' = FIX' / 128,   FIX' = clamp(round(128 (FIX / 128)^(1 / k)), FIX, 127)

with S_i SCENE just before the sprite, A an RGBA16F ping-pong pair at SCENE's texture size (its
scale, its Full pixel crop; an 8-bit running blend would stall up to 7 levels short at k = 10,
its floor step `(A - S) FIX' >> 7` being 0 below `128 / (128 - FIX')`), and k the presents a
tick. Each present: the sprite's pass ends; SCENE goes to a shader read (an open MSAA epoch
resolves into it there); `fx_acc_resolve_ps` rounds A_i-1 into RES, an RGBA8 scene-class
temporary target (`rd__temp_target_alloc(gsW, gsH, 0, 1)`); `fx_acc_ps` writes A_i into the
pair's other texture from A_i-1 and SCENE (both `blit_vs`, one triangle, each texel by Load);
then the game's sprite is drawn as recorded through `rd__replay_blur_sprite_draw`, in a payload of
its own, with its texture RES's RGB24 view (`RD_VIEW_RGB24_TA0`, so the alpha it writes is TA0 as
before), its V over all H lines (`uv[3] = uv[1] + 2 (uv[3] - uv[1])`: the recorded 4 .. 4100
becomes 4 .. 8196, one texel a row as U has one a column) and FIX' for FIX; the state's
texture and FIX are put back after it. The sprite's 8-bit result is A_i rounded; A keeps the
fraction. The two pipelines are a private cache (`RD_BLUR_SMOOTH_PIPES`, as the COLCLAMP 0 wrap's
`RD_WRAP_PIPES`; the pipeline pool's headroom assert counts them), so the `rd__get_pipeline` set
and `RD_PIPELINE_REACHABLE_MAX` do not change; the sprite's own key is the recorded one (FIX and
the texture are not in it).

k comes from the presenters (`rd__blur_smooth_arm`, before each replay that presents): on the timer
path `rd_present_clock_alpha`'s tick over its step (kept in `rd_interp.c` statics), under the vsync
lock the presents the tick before counted (`rd_present`), 1 for the first tick and for the
presenter without interpolation (`rd_core.c` (`replayOnHost`)); clamped to 1..64
(`RD_BLUR_SMOOTH_K_MAX`); FIX' is taken at each sprite from the tick's k. A replay nobody armed
(a self-test, a dump's, the replay tool's `--time`) draws the sprite as Original and leaves A
alone. `feedback` makes no DISPLAY / DISPLAY_HELD head copy while `rd__blur_smooth_active` (the
sprite does not read DISPLAY; FEED128's copies are unchanged).

The trail ends where an armed replay that is not a keep frame's drew no motion blur sprite (the
game's alpha 0, the switch off: `rd__blur_smooth_replay_end`, from `rd__replay_frame`): the next
sprite starts it again, `A = S` and FIX' 0, which leaves SCENE's colour as it is (no stale
ghost). The same restart follows a new scene size, the targets made again (`createNamedTargets`
frees the pair and RES before the pool's clear), and a change of the setting (`rd_begin_frame`
frees them on the host). `rd__blur_smooth_shutdown` (`rd__gpu_shutdown`) destroys the pair and the
pipelines. A texture or pipeline the GPU refuses leaves the run on Original (logged). The tiler's
fold cannot apply (the motion blur is no blur pair); the frame-size plan's local read
(`rd_fx_plan.c`) draws the sprite as it would draw the recorded one.

Against Original (DISPLAY.md): the light the passes after the sprite add (the aura, the
brightness step, the reduction's tint, a fade) is not recycled through the trail, since A
holds scenes, not DISPLAYs (spread over k presents that loop would settle at another brightness,
`rd_interp.c`'s header), and the PS2's vertical halving of the old frame is gone. A still picture
is the same within a level. At one present a tick (Frame rate Original) FIX' is FIX and the two
differ only in what the trail holds. Cost: two passes of one triangle over SCENE's texture a
present and 20 bytes a texel (50 MB at 2340x1080, 166 MB at 3840x2160), made only while Smooth
draws.

The log (`rd: ` lines):

```
motion blur: smooth (the trail moved on at every present from an accumulator of the scenes, the game's FIX spread over the presents of a tick)      (rd_init, or with ", from the next present" on a change)
motion blur: original (the game's own: the trail moved on once a tick), from the next present
motion blur: smooth: accumulator WxH (RGBA16F x2 + RGBA8, N.N MB)
motion blur: smooth: no memory for the accumulator WxH (RGBA16F x2 + RGBA8, N.N MB): the motion blur is drawn as Original for this run
motion blur: smooth: its pipelines could not be made (accumulate made|missing, resolve made|missing): the motion blur is drawn as Original for this run
motion blur: smooth: the scene is WxH texels now (the accumulator WxH): made again
motion blur: smooth: the accumulator WxH freed (why)
motion blur: smooth: the trail starts again at frame N (why): this present's scene is its first picture, no old frame in it      (the first 3 a block)
motion blur: smooth at one present a tick (FIX F as recorded): ...      (once)
motion blur: smooth: the sprite of frame N is drawn as Original: why (reported once)
motion blur: smooth: P presents, K1 to K2 presents a tick, FIX F drawn as F' (the last); D sprites through the accumulator, R restarts (M not named above), O drawn as Original (no accumulator)      (with the interp summary, when it drew)
```

`RdPerfRecord.blurSmoothPasses` counts the passes a replay. The replay tool's `--motion-blur
smooth` sets it (its replays armed; the `--prev` dumps feed the trail) and `--presents N` (with
`--interp T PREV`) replays PREV, then N presents at alpha `T (k + 1) / N` to `<out>-p<k>.png`
(TESTING.md).

### The motion blur's strength

Options > Effects > Motion blur strength (`[video] effect_motion_blur_strength`, 10..100 in tens,
`RdSettings.motionBlurStrength`; issue 181). `rd__blur_post_sprite` (`port/render/rd_blur_border.c`)
draws the motion blur's sprite with `FIX_eff = round(FIX x strength / 100)`
(`rd__blur_strength_fix`, `port/render/rd_blur_smooth.c`) in the state's `blendFix`, and puts the
recorded FIX back after it. Under Original that is the FIX the sprite's pipeline key and DrawCB
take (`doBlurSpriteDraw`); under Smooth `rd__blur_smooth_sprite` reads the state's FIX, so the
k-root above is taken from FIX_eff. The trail keeps `FIX_eff / 128` of itself a tick (the old
frame's share in `(Cs - Cd) FIX / 128 + Cd`), so at 50 % half the PS2's share and it fades
faster; the sprite's spread, its texture, Original's H/2 stretch and every other sprite are
unchanged. At 100 % (and in a zeroed `RdSettings`: 0, the dumps, the tests) the sprite is drawn
as recorded, the same bytes. The replay tool's `--motion-blur-strength N` sets it. The log:

```
motion blur: strength N % (the game's FIX scaled by N / 100 and rounded, in Original and Smooth alike: FIX 32 drawn as F, a shorter trail)      (rd_init below 100 %, or with ", from the next present" on a change)
motion blur: strength 100 % (the game's own FIX), from the next present      (a change back)
motion blur: strength N %: the game's FIX F drawn as E from frame M (the trail keeps a % of itself a tick where the PS2 keeps b %[, spread over the presents of a tick]; change C)      (a new FIX or strength: the first 8, then every 100th)
motion blur: strength N %: S sprites drawn at a lower FIX than the game's in this block, FIX F drawn as E (the last)      (with the interp summary, when it drew)
```

The Smooth summary's `FIX F drawn as F'` then names FIX_eff and its k-root; the window's
`display changed` line names `motion blur strength A % -> B %`.

## Above 1x

At scale 1 every pass samples as the GS does. On a scaled target a pass that steps over many
texels a pixel (a decimation) or spreads one texel over many pixels (a magnification) would show
the GS's few taps as a grid at the finer resolution, so those passes resample instead. The rule is
one separable kernel per axis, chosen by the step (source texels per destination texel) and
centred on the GS's own sample point, so the picture's columns land where they do at 1x and the
4:3 columns are the same at every aspect (`port/shaders/common.hlsli`, the "Above 1x" block):

| step | kernel | weights |
| --- | --- | --- |
| above 2 texels | area box of the step's width | each texel's overlap in 1/32 texel (`gs_box_w`), summing to the step |
| below half a texel, bilinear | cubic B-spline (B = 1, C = 0: no overshoot on alpha masks), four taps | from the GS's 4-bit fraction in 1/24576 (`gs_bspline_w`) |
| otherwise | the GS's own taps on that axis | bilinear 16 - f and f, or nearest |

The kernels are integers throughout, and the block is valid C as well as HLSL, so a CPU model gives
the GPU's bytes (`port/render/test/above1x_ref.c`, `port/shaders/test/shaders_pixel_test.c`
(`checkAbove1xKernels`)). Two entry points:

- Load-based: `gs_resample_axis` gives an axis's taps (`GsAxis`, with `gs_resample_weight` and
  `gs_resample_div`); the caller fetches each tap with its own addressing and combines the rows
  with four extra bits, `R' = (16 sum(wx t) + Dx / 2) / Dx`, then
  `(sum(wy R') + 8 Dy) / (16 Dy)`, within 32 bits (`port/shaders/common.hlsli`).
- Sampler-based: `gs_bspline_bilinear4` folds each axis's four B-spline weights into two hardware
  bilinear taps, four taps in all, for a sampler that addresses as the draw does; the filter's own
  weight precision makes it inexact (`port/shaders/common.hlsli` (`gs_bspline_bilinear4`)).

`gs_bayer8` is the 8x8 ordered-dither threshold, computed in the shader from the texel's
coordinates (`port/shaders/common.hlsli` (`gs_bayer8`)).

**The staticBlur sprites.** `doBlurSpriteDraw` sets `RD_FXF_ABOVE1X` on a sprite that samples a
target when the bound target or the source has a scale other than 1 (the predicate
`rd__blur_screen_fallback` takes), the source is not a work buffer (a work buffer drawn onto the
frame takes the GS's own filter: [The glow size](RENDER.md#the-glow-size)) and an axis leaves the GS's
filter: its steps, computed on the
CPU from the rectangle and UV extents and the two scales, rounded to 1/32 texel, go to the shader in
`DrawCB.scale[3]` as `step32x * 4096 + step32y` (`port/render/rd_replay.c` (`blurAbove1xSteps`,
`doBlurSpriteDraw`), `port/render/rd_internal.h` (`RD_FX_BOX_MIN32`, `RD_FX_BSPLINE_MAX32`)).
`fx_sprite_ps` then resamples about the GS's U and V in the source's 12.4 texels, each tap fetched
as `fx_texel` fetches (CLAMP or REPEAT on the TEX0 size, the backing texture's clamp, TEXA before
filtering), and TFX, the tests and the blend follow in integers as before
(`port/shaders/fx_sprite.hlsl` (`fx_sprite_ps`)). The pipelines are the same; only the per-draw
constants differ. The gate is the scale, never the step: at scale 1 the chains' own 4:1 and 3.5:1
steps stay the GS's point samples, so the out-of-the-box picture is the GS's arithmetic as before.
Images are never resampled. A sprite that samples the target it draws into reads its snapshot,
which holds the texels at its UVs widened by the kernel's reach on each axis that leaves the GS's
filter (`port/render/rd_replay.c` (`blurSnapArea`, `blurKernelReach`)). `g_rd.above1xDraws` counts
the flagged sprites since `rd_init`
(`port/render/rd_internal.h` (`RdContext`)).

At Enhanced 4x (NTSC: SCENE 4x, the work buffers at their GS sizes) the aura's chain takes the box
for SCENE and AURA_WORK into FEED128 (16 texels across, 14 down) and AURA_WORK into WORK0 (8
across, 16 down), and the GS's bilinear at each SCENE texel's GS position for the work buffers
back over the frame (the GS's step divided by 4); `rd_blur_test`'s `above1x` case compares every target with a
CPU model at scale to the bit, and every comparison at scale 1 finds no sprite flagged
(`port/render/test/rd_blur_test.c` (`checkAbove1x`, `compareAll`)).

**The sprite passes over targets.** `fillDrawCB` sets `DF_ABOVE1X` (`ICO_DF_ABOVE1X`, bit 25) on a
pass of `sprite_ps`, `sprite_aa1_ps` or `sprite_stq_ps` whose texture is a colour view of a target,
both filters linear, no GS levels, and the texture's scale is not the bound target's, with the
draw's wrap in `DF_TEXA_CLAMP_S/T` (`port/render/rd_fx_plan.c` (`rd__sprite_above1x`), `rd_replay.c` (`fillDrawCB`)).
These are the shadow composites (the levels at scale 1 into SCENE, 1/2s to 1/8s texel a pixel) with
`[dev] shadow_bspline` only, the
softening's SCENE into AA0 (a box), and the reduction's fallback sprite (SCENE
into DISPLAY on a scaled target, `rd__blur_screen_fallback`) when an axis leaves the GS's filter at
its steps as `blurAbove1xSteps` computes them; at full height and scale 1 DISPLAY has twice the GS's
rows (sy 2) and the reduction steps one SCENE row a DISPLAY row, the GS's filter on both axes, so
the flag stays off there (`Replay.a1xGs`; `port/render/rd_replay.c` (`doBlurSpriteDraw`)). The
shader takes the step of each axis, in 1/32 texel, from `DrawCB.scale[3]` (`step32x * 4096 +
step32y`): the sprite command's, which `doScreen` computes on the CPU from its expanded corners,
the texture's normalisation and size and the bound target's scale, when the frame-size plan is on
(`RdProfile.fxPlan`) and every sprite of the command has the same (`port/render/rd_fx_plan.c` (`rd__a1x_sprite_steps`),
`DrawSetup.a1xSteps`, `fillDrawCB`); 0 for any other command, whose pixels take the step from
their footprint (`ddx`/`ddy` of the texel coordinate, constant over a sprite)
(`port/shaders/sprite.hlsl` (`sprite_above1x`)). The CPU's quotient is exact where the
footprint's carries the interpolation's rounding, so the two differ only within that rounding of
a half step:

- both axes below half a texel: the B-spline as four bilinear taps of the draw's own sampler
  (`sprite_bspline4`: `gs_bspline_bilinear4` with each 4-bit fraction's two weights and two tap
  places from a 16-row table of the floats its divisions round to, `kSpriteBspline`), TEXA after,
  as `sprite_ps` applies it; within 2 LSB of the integer kernel
  ([DIVERGENCES.md](DIVERGENCES.md#video) VD-18);
- an axis above two texels: the area box on that axis, every tap loaded with the wrap reproduced on
  the texture's size and TEXA per texel (`gs_texa_load`), combined separably as `fx_sprite_ps`
  combines them, exact;
- otherwise the pass samples as before (`gs_sample`).

By default the shadow composites take one bilinear tap, as at 1x; the B-spline (a softer
penumbra) is the A/B ([WORK_BUFFERS.md](WORK_BUFFERS.md#the-shadow-edges-above-1x)). The GS
levels' filter of the Original preset (`rd__gs_filter_int`: [Textures](RENDER.md#textures)) is for images
only, so it never meets the flag (`port/render/rd_pipeline.c` (`rd__gs_filter_int`)). The flag is never set when the
two scales are equal, nor on the reduction at the GS's filter, so never at scale 1;
`rd_present_test`'s full height 1x case checks no draw is flagged there and that the frame is byte
for byte the one with the reduction flagged (`rd__set_a1x_gs_keep` off), whose shader takes the GS's
path at every pixel (`port/render/test/rd_present_test.c` (`checkFullHeight1x`)).
`g_rd.above1xDraws` counts these passes too, and each replay's record their number
(`RdPerfRecord.a1xDraws`, [Performance records](RENDER.md#performance-records)).

**The fog.** `doFog` sets `DrawCB.param[3]` to 1 when SCENE or the depth target is scaled, with
the bound target's centre texel (its texture size halved, rounded down: the native frame's centre,
`rd__target_scale_of`) in `uvRect[0..1]` (`port/render/rd_replay.c` (`doFog`)). ZFog.c builds the
table as a linear alpha ramp sampled at 256 depths with fewer alpha values than entries
(`ico2/seki/src/ZFog.c` (`fog_MakeFogClut`)), so at 1x the fog bands by the 8-bit index. Above 1x
`fog_lut_ps` reads the table between its entries: `n` the Z bits 16..23 (the GS's index) and `f`
the next eight bits interpolate `LUT[n]`..`LUT[n + 1]`, MODULATE (or DECAL) applies in the same
fixed point, and a one-unit 8x8 ordered dither carries the fraction the 8-bit alpha cannot hold,
`floor(ramp * factor / 128 + (bayer + 0.5) / 64)`, at most 255 (`port/shaders/common.hlsli`
(`gs_fog_channel`), `port/shaders/fog_lut.hlsl` (`fog_lut_ps`)). The dither is indexed from the
target's centre texel, so the 4:3 columns of a wide frame carry the pattern of the 4:3 frame. At
an entry (`f = 0`) with factor 128 the value is the entry's own. Every fog variant (the dual-source,
`_nodual`, `_dstrov` and `_dstatt` entries, `FOG_BUFFER`, `FOG_MS`) shares the code before the
output. At scale 1 `param[3]` is 0 and the entry is read as the GS reads it; so it is for the fog
self-test's draws and the probe's frame at any scale (`port/render/rd_fog_path.c`
(`rd__fog_exact`)).

**The present.** Without anti-aliasing, a scaled scene with more texels than the box has pixels on
both axes is box-averaged to the box first, as anti-aliasing does (the box blit's one bilinear tap
skips texels), never at scale 1 (`port/render/rd_present.c` (`reduceRecord`)); downsampling:
[DISPLAY.md](DISPLAY.md#downsampling).

**Left as they are.** The work buffers' chains run at the PS2's sizes and their draws back onto the
frame take the GS's filter ([The glow size](RENDER.md#the-glow-size)). The reduction SCENE to DISPLAY is never above 2:1 in texels
(`port/render/rd_blur.c` (`rd__blur_screen_fallback`)), where the GS's bilinear is already the area
average, so it keeps the GS's filter. The shadow levels stay at scale 1 (their resolution is the
penumbra, `port/render/rd_core.c` (`shadowLevel`)), and the first level reads the count already
box-averaged to its GS size (`port/render/rd_replay.c` (`shadowReduce`)), so the chain between them
is the PS2's at every scale; only the composites back into SCENE resample.

## Shadows

The PS2 adds each shadow-volume face into a scene-sized buffer (FBP 0x142) with ALPHA 0x68 FIX
0x80 and COLCLAMP 0, the face colour 0x04 or 0xFC by its facing, so a pixel ends at 4n mod 256 for
n the net count of faces in front of the scene; `shadow_Draw` then reads it as PSMCT24 under TEXA
0x80 AEM, blurs and composites it (`port/render/rd_shadow.c`, header comment).

rd counts on the stencil of the bound depth target (SCENE) instead: INCR_WRAP for 0x04, DECR_WRAP
for 0xFC, both faces, where the depth test passes, no colour or depth write, write mask 0x3F, so
the stencil holds n mod 64 and 4n mod 256 = 4 (n mod 64). The resolve writes the colour the GS sum
would have left: six passes with the stencil test EQUAL on bit k add 4 << k to RGB, a seventh
writes A = 0x80 where the count is not 0, the TEXA expansion of the PSMCT24 read. The order of the
faces does not matter on either side, so the triangles are drawn as two lists, increments then
decrements (`port/render/rd_shadow.c`; `port/render/rd.h` (`rd_shadow_count_target`,
`rd_shadow_tris`, `rd_shadow_resolve`); `port/render/rd_replay.c` (`doShadowStrip`,
`doShadowResolve`)). One draw: [WORK_BUFFERS.md](WORK_BUFFERS.md#the-shadow-count-in-one-draw). Step 8 discs:
[there](WORK_BUFFERS.md#the-shadow-counts-step).

The stencil's bits: 0-5 the count (`RD_SHADOW_STENCIL_MASK`, the only bits the count writes and
the resolve reads), 6 DATE's (`RD_DATE_STENCIL_BIT`: its setup writes it alone and its draws read
it alone, [The replay](RENDER.md#the-replay)), 7 unused. A count's INCR_WRAP or DECR_WRAP through the write
mask 0x3F leaves bit 6 as it was and gives bits 0-5 the same value whatever bit 6 holds, so the
two never meet; the shadow reset clears the whole stencil, which DATE's setup writes again in the
pass that needs it (`port/render/rd_pipeline.c` (`shadowStencil`)).

## Water and render-to-texture blocks

`puddle.c`, `pool.c` and `queen_barrier_disp.c` draw into the VRAM block `tex_AllocVramAuto` hands
out after `tex_ResetVramPri`, TBP 0x2800 in their lists, which the register decoder maps to AA0.
`rd_water.c` supplies what the decoder cannot know (`port/render/rd_water.c`, header comment):

- `rd_gs_named_block` and `rd_block_target`: a per-frame target of the block's own GS size, with
  its own depth when asked, the same target for the same (TBP, size) for the rest of the frame,
  marked as a block (`RdTargetRec.block`, `rd__block_temp_target`);
- `rd_alias_target`: from here on in the current list, `rd_set_target`, `rd_clear_target` and
  `rd_texture` of `from` record `to` (at most 4 aliases a list), so replay and dumps see an
  ordinary target;
- `rd_push_camera` / `rd_pop_camera`: the reflection camera of a mid-frame `gsb_SetVSMatrix`,
  kept for the Enhanced projection and interpolation and not dumped; nesting depth 4, 8 scopes a
  frame;
- `rd_grid_screen_st`: the formula and scales of pool.c's screen-space STs for the next
  `rd_draw_vu_grid`, written into its command so dumps carry it, so the interpolation can compute
  the STs again through the blended camera (`port/render/rd.h` (`RdGridSt`)).

The records live per frame slot and are cleared when the slot is reused
(`port/render/rd_water.c` (`rd__water_frame_reset`)).

**Water reflections** (`[video] effect_water_sharp`, `RdSettings.waterSharp`, issue 97). A block
the game sizes as the native frame (the dark volume's, `gsFrame.w x ScreenHeight`) is scene-class
and takes the scene's scale whatever the option. Every other block, the puddle's and the pool's
reflection (`bw x 256`, `ico2/sugipon/src/puddle.c` (`puddleHostBlockBegin`), `pool.c`
(`poolHostBlockBegin`)) and the queen barrier's copy of the frame (`gsFrame.w x 256`,
`ico2/ito/src/queen_barrier_disp.c` (`barrierHostBlock`)), is drawn at its own GS size under
Original, the PS2's texels and the default, so above 1x the reflection is softer than the scene
around it. Under Sharp such a block takes the scene's scale, both ways as SCENE has it
(`sceneSx`, `sceneSy`) (`port/render/rd_core.c` (`rd__target_scale_of`)). Its depth buffer
is made at the same texture size, the clears and scissors are in GS pixels through the target's
`sx`/`sy` as on any scaled target, and what samples it (the reflecting grid's GS-texel STs, pool.c's
screen STs normalised by the screen size, the barrier mesh's UVs, the copy sprites) addresses it
in GS texels, so only the texel count changes. The queen barrier's copy follows the same rule: it
is the frame seen through the barrier, as soft as the reflections under Original for the same
reason. A block at the scene's scale that does not fit comes down with the scene
(`rd__note_target_pressure`). Off, `rd__target_scale_of` takes the branch it always did, so the
picture is byte for byte the one before the option. A change applies to the blocks made from the
next frame (they are per-frame temporaries) and logs `water reflections: sharp|original`; a
Sharp start logs the line once at `rd_init`. Dumps carry the mark in the temporary target's keep
word (bit 1); the replay tool's `--water-sharp` draws them at the scene's scale, and without it they
replay at their GS size (`port/render/rd_dump.c`, header comment).

**What the log says about the water** (issue 106). The puddles are four steps in list 4
(`ico2/sugipon/src/puddle.c` (`PuddleDL`)): `baseSetup` clears SCENE's alpha with a full-frame
sprite (ALPHA mode 5, colour 0) and draws the puddle surface, whose pixels set the alpha bit;
`drawAreaSetup` and the reflection draw into the block; `leveldown` (LERP FIX 0x10), `drawRipples`
(ADD FIX 0x60, the ripple a ring of eight segments, `t` to `t + 5` from where a foot landed, its STs
the block's at the ring's projected place) and `copy` (ADD FIX 0x60, the block over the frame) test
DATE with DATM 1, so they draw only where the surface set the bit; leveldown and copy with the alpha
test NEVER and AFAIL RGB_ONLY. An F12 adds one line to the log after the box cull's,

```
rd: dump: frame N: water reflections original|sharp; scene SXxSY; block WxH GS with depth at TWxTH texels (SXxSY, the scene's scale|not the scene's scale): D draws into it, R read it (DATE off a, alpha 0 b, alpha 1 c, AFAIL rgb-only, blends 0, REPEAT r, k of them kept on REPEAT); DATE draws on SCENE: alpha 0 b, alpha 1 c, AFAIL rgb-only, blends 0 2; Full pixel on|off, border mask on|off: N render-target reads clamped, M block reads kept on REPEAT|border closed; blend exact (interlock|rov|attachment read)|fixed-function (rounds up), storage pass merge on|off; K draws sampled a texture-pack replacement (blank in the dump), P of them DATE draws; MSAA 8x (resolve: colour the mean of the samples, depth the smallest sample): the last replay's DATE snapshots of SCENE S, A of them read the resolved picture (C closed the epoch), of other targets O
```

(one line; a part with nothing to say is left out): each render-to-texture block a draw of the frame
wrote or read, at which scale it was drawn, the draws into it and the DATE, AFAIL, blends and wrap
of the draws that read it (`REPEAT r`: the reads on REPEAT on an axis; `k of them`: screen prims,
which keep the wrap under an open border: the ripples, [DISPLAY.md](DISPLAY.md)), the DATE draws by
target, the border (N, M: the open border's clamp) and the blend path in
force (Full pixel, the border mask, the destination read and the storage pass merge), and how many draws sampled a texture pack's picture,
which a dump carries blank, so a replay of that dump cannot show what they drew
(`port/render/rd_water.c` (`rd__water_dump_line`); `port/render/rd_dump.c` (`rd_dump_on_demand`)).
It walks the dumped frame once, on the key press, never per frame. The replay tool's `--list` ends
with the same summary as `water: ...`, at the replay's options. The 10 s block adds `window: water: N
draws per replay read a reflection block drawn at a scale other than the scene's (M in the block)`
when any draw did: the other-size rule above in force (Water reflections Original with the scene
above 1x), counted where the replay resolves a draw's texture (`port/render/rd_replay.c`
(`resolveTexture`); `port/render/rd_water.c` (`rd__block_read`); `RdPerfRecord.blockOtherScaleReads`; `port/platform/window_host.c`). `rd_water_test`
checks the line on the puddle's frame and the count at 2x under Original (the copy's read) and Sharp
(none).

The MSAA part (or `no MSAA: the last replay's DATE snapshots of SCENE S, of other targets O`)
tests the first of issue 106's two candidates: with MSAA a DATE draw on SCENE reads the alpha bit
through the snapshot of SCENE as the epoch resolved it, the colour the mean of the samples, so an
edge pixel's bit can differ from what each sample drew (`rd_replay.c` `dateSnapshot`, `rd_core.c`
`rd__date_msaa_last`). `P of them DATE draws` tests the second: a texture pack's picture sampled
by the puddle's mask readers themselves.

What it was (a dump from an RTX 3090 on the interlock path, Enhanced at 3840x2160, Full pixel on):
neither of the two, nor the blend path. The bright, flat ring segments were the ripples sampling
the block's corner texel: an open border clamped their read, whose STs run 1.05 to 1.95 on REPEAT. A
lavapipe replay of the same dump with `--full-pixel` drew them so (the attachment read, so not the
interlock path); with the read kept on REPEAT they are the faint reflection rings of the replay
without the option ([DISPLAY.md](DISPLAY.md), `rd__repeat_kept`). `rd_water_test` draws a ripple
and requires SCENE byte for byte the same with Full pixel on and off.

**What the GS draws from the game's own data (issue 106, the shapes).** The ring and the puddle
outline were checked against the disc and the byte-matched game code, not the port's decode. The
ring is nine spokes 45 degrees apart (`ico2/sugipon/src/puddle.c` (`InitPuddleGeo`): `i * 8192` of
a 65536 turn) drawn as two nine-point rings at `t` and `t + 5` interleaved into one strip of 18
vertices (`drawRipple`, `gif_DrawStripFST(..., 0x12, 1)`; `ico2/seki/src/GifPacket.c`
(`gif_DrawStripFST`): PRIM 0x94 | 1 << 6, a triangle strip with TME, ABE and AA1, FST 0 so ST with
Q 1 from RGBAQ): eight flat quads, an octagon on any GS, whose outer ring is turned by
`age * 10.24` so the sampled reflection twists. The mask is `object/sdf/st02a/model/../texture/
puddle.tm2` (also st13a's): TIM2 PSMT4 128 x 128, one level, a 16-entry RGBA32 CLUT whose entry 0
is `00000000` and entries 1 to 15 `00000080`, the image using indices 0 and 1 only, so every texel
is alpha 0 or 0x80 on the disc. The game's `tex_setTexReg` writes TEX0 with TCC 1 and TFX 0 for every
texture (`ico2/seki/src/Texture.c` (`tex_setTexReg`), bit 34 always set; the TIM2 header's own TEX0
field, TCC 0, is not what it writes), so the alpha test GREATER 1 (`TEST 0x3F000` family, AREF 1
from the record's TEST) runs on the bilinear blend of a 0 / 0x80 mask times the vertex alpha 127: a
pixel passes wherever the four texels under it hold any 0x80, which is the texel grid dilated by
one texel, stepped at the texel size (about 3 GS pixels at 1x on the drained room's floor, the mask
repeating about six times across the puddle mesh's STs of -3.4 to 2.9). PCSX2's software renderer
follows the same arithmetic (`GSDrawScanline.cpp` `TestAlpha`: `(ga >> 16) < aref` fails GREATER;
the bilinear `lerp16` of the four texels; AA1 only as coverage on the edges, `GSRendererSW.cpp`
`IsCoverageAlpha`), so at its native resolution it draws the same octagon and the same stepped
outline, 3 pixels a step; its display scaling softens them. Sad Origami's PCSX2 pack carries a
smooth 1024 x 1024 BC7 replacement of this very mask
(`02 - World/04 - Particles/c2d383ac71c5d893-cd153d44d14cf0c9-00001dd4.dds`), and
`texpack_disc_test` names puddle.tm2 exactly so, so with the pack installed and the option on the
port's puddle outlines follow the replacement (the pack does not touch the ring). No run with the
pack on has been dumped; the reporting run's log said `textures: no texture pack`.

**The ring's edges (issue 106, the corners and the strength).** The ring is drawn with PRIM.AA1:
in `frame-20261015-223424-v2585.rddump` it is list 4 command 147, `SCREEN tris n 240 aa1`, 80
triangles (five ripples), ALPHA `Cs * FIX + Cd` with FIX 0x60, ATE 0, DATE DATM 1, Z write, the
reflection block as its texture (TCC 1, modulate, bilinear) and vertex alpha 0x80. ALPHA mode 0
ignores As, so each of its edge pixels adds the full `Cs * 0.75`, as an interior pixel does, and
an edge two quads share gets both fringes: the ring's rim is as strong as its body, on the GS and
in PCSX2's software renderer alike (`IsCoverageAlpha`: AA1 on a line or triangle, ABE aside). The
spurs at the octagon's corners in the issue's port picture came from fringes run past their
vertices; a fringe ends at the GS's wedge and box (the **AA1** paragraph of
[The replay](RENDER.md#the-replay)).
PCSX2's hardware renderer draws the edge pixels only on a device whose features have `aa1`
(`GSRendererHW.cpp`: `draw_aa1 = !no_rt && PRIM->AA1 && features.aa1`, and `aa1` needs feedback
loops); the devices' code is not in the tree, and the investigation of the issue found it off on
Vulkan and Direct3D, where a ring looks thinner and fainter than the GS's. The renderer of the
reporter's PCSX2 picture is not known. The port's doubled rows along the shared edges were DATE's
order inside the strip (**DATE and AA1**, DIVERGENCES.md VD-17).

## The fog path and its self-test

The depth fog reads SCENE's depth: the GS copies the Z buffer, a PSMT4 view copies the third byte
of every word into the fourth, and the fog sprite reads the top byte through the 256-entry CLUT,
so the index is bits 16..23 of the 32-bit Z and only pixels with Z <= 0xFFFFFF are fogged
(`port/shaders/fog_lut.hlsl`). The replay reads the depth one of four ways
(`port/render/rd_fog_path.c` (`RdFogPath`)):

| path | how |
| --- | --- |
| local | inside the scene pass, the pass's own depth as an input attachment (Vulkan local read; offered where the profile has `fogLocal`; [The replay](RENDER.md#the-replay)) |
| inplace | the depth target sampled where it is |
| copy | a whole copy into a second depth texture, sampled |
| buffer | the depth aspect copied into a buffer and back into an R32_UINT texture (Vulkan only) |

`rd_fog_selftest` replays a frame of its own (never recorded or presented) on SCENE at its real
size and scale: a 16 x 16 grid of cells at known GS Z and the fog with a LUT whose every entry
differs, read back and compared with the GS arithmetic, for each path in the platform's order
(desktop: inplace, copy, buffer; tile-based or D24S8: copy, inplace, buffer; local first wherever
it is offered, and left out of the line where not). The first that passes is the path from then
on, and one log line `fog: depth path` names it with every result (`rd: fog: depth path local
(self-test: local ok 0, copy not tried, inplace not tried, buffer not tried) on SCENE ...`; run
again when the desktop's probe turns local off); a
local read that fails turns `fogLocal` off (`(self-test)`), and the order is then the local one
without it (desktop: inplace, copy, buffer; else copy, inplace, buffer). The local trial counts only where the fog drew through the
local read (`g_rdPerf.fogLocalDraws`). Under MSAA it never does (`port/render/rd_replay.c`
(`fogLocalFor`): the epoch), and the path draws its MSAA fallback there (the per-sample fog or the
copy), which is what the trial measured: the line says `local not measured (MSAA: its fallback ok
0)`, nothing it finds turns `fogLocal` off, and when the samples drop to 1 the self-test runs
again and measures the local read itself (`port/render/rd_fog_path.c` (`rd__fog_aa_reconcile`)).
Without MSAA a local trial that drew no local fog is `local not run`, a failure.
`ICO_RD_FOG_SABOTAGE=local` makes the local read give 0 (`rd_fog_selftest_local_sabotage`). It runs
after `rd_precreate_pipelines` and again whenever SCENE is made at a new size. `ICO_RD_FOG_PATH`
or `ICO_RD_DEPTH_COPY` names a path to try first; `ICO_RD_FOG_PATH=test` tries every path
(`port/render/rd_fog_path.c`; `port/render/rd.h` (`rd_fog_selftest`)). The probe logs five
texels' Z, index and colour on the first fogged replay after start and after a new scene size
(`fog: probe`), then the table's entry 0, the colour the fog gives Z 0, which a pixel no draw
covered keeps from the clear (`...; pixels nothing drew take the fog's colour at Z 0, table entry 0
= RRGGBBAA`): a line of that colour in a report's picture is a gap in the geometry
([Camera blending](RENDER.md#camera-blending)) (`port/render/rd_fog_path.c` (`rd__fog_probe_record`)).
At every aspect and with Full pixel either way, the edge probe copies the depth the
fog read for the frame's first 8 and last 8 GS columns (`RD_FOG_EDGE_COLS`), every row, on every
32nd fogged replay, into a readback buffer that a replay two frames on counts without a wait
(`rd__fog_edge_due`, `rd__fog_edge_record`, called by `doFog` after its draw, the pass ended
first). Each GS column is read at its first texel, `ceil(x * sx - ox)` clamped to 0 and kept only
while it is under the next column's (`rd__fog_edge_texel`), so a depth target holding only Full
pixel's window reports the columns it holds and `-` for the ones left out; the copies are two
blocks, texels 0 up to the left's last read texel and the right's first read texel up to tw. The
depth is the game's, as SCENE's repeat ([SCENE's outer columns](#post-passes)) copies the colour
alone. A probe that finds rows at GS Z 0 logs, the first three in each 10 s, `rd: fog: probe: outer
columns: depth 0 per column [n0 n1 ... n7] of M rows (rows a..b) | [n(W-8) ... n(W-1)]; frame W GS
wide, crop C columns a side, border <where they go>, table entry 0 = RRGGBB`: each n is the rows
of that GS column at Z 0, M the rows looked at, the run (left out when it has none) that of the
first held column on the left, C the GS columns the present leaves out at each side, `(W - the
exact width) / 2`, and where they go being, as `rd__scene_border_state` was at the copy, `open at
the frame's exact width: they reach the screen, SCENE repeats columns 2 and W-3 over them before
the glow`, `cropped by the present: the glow reads them, SCENE repeats columns 2 and W-3 over them
before it`, `closed: the reduction shows black there but the glow reads them, SCENE keeps them (the
PS2's picture)` (512), `closed (wider than the PS2's): ..., SCENE repeats columns 2 and W-3 over
them before it` or `in Full pixel's window: SCENE repeats columns 2 and W-3 over the texels of them
it holds before the glow`; so a 16:9 log carries the count too, and the eight counts a side say how
wide an uncovered band is, not only that column 0 has one. The rest of those 10 s is one total at the
next probe after them, `rd: fog: probe: outer columns: depth 0 there in N more probes in the last
10 s (T in all)`. Nothing is copied in the self-test's replays, on the multisampled read, or on
D3D12's depth (copied to a buffer only whole). Rows at Z 0 there mean the game left those columns
uncovered (now repeated from their neighbours where the line says so); a band that survives with
no such line is not the fog's (`port/render/rd_fog_path.c` (`rd__fog_edge_column`,
`rd__fog_edge_line`); `rd_fog_test` (o)). Both draw the fog at the exact table entry of
each index, not read between entries above 1x, since what they measure is the depth read; the
probe's frame is one game frame drawn that way (`port/render/rd_fog_path.c` (`rd__fog_exact`),
`port/render/rd_replay.c` (`doFog`)).

Its first run runs DATE's self-test first, where the profile has `dateStencil`
(`port/render/rd_date_selftest.c` (`rd_date_selftest`)): a frame of its own on SCENE clears it,
draws a cell of alpha 0x80 on the left half and one of alpha 0 on the right, then a DATE sprite
with DATM 1 over the top half and one with DATM 0 over the bottom, through the stencil bit and
through the snapshot; the quadrants' centres are read back and compared with the GS's outcome,
every channel exactly. A fifth sample is the DATM 1 command's own overlap: a second, smaller
sprite of the same command inside the top left quadrant, over the first sprite, which cleared the
alpha's MSB there; the stencil bit and the snapshot hold the alpha from before the command, so
the second sprite's colour shows (each path is judged by its own rule, the GS's per-primitive one
for the colour read). Under GS state as shader data ([GS state as shader
data](RENDER.md#gs-state-as-shader-data)) a third replay comes first, DATE through the colour read, whose
fifth sample keeps the first sprite's colour (the GS's), its draws counted
(`g_rdPerf.dateDstDraws`); its line is `rd: DATE: colour read path (self-test: colour read ok 5,
stencil ok 5, snapshot ok 5) ...` (`stencil not tried` where the profile has the snapshot), and a
failing colour read leaves DATE to the bit or the snapshot (`g_rd.uberDate`). Both replays run with MSAA held off (`g_rd.msaaBlocked`): the stencil bit
is never taken inside the MSAA epoch (`port/render/rd_replay.c` (`dateStencilFor`)), so it is
checked as it draws whatever the anti-aliasing setting, and the result holds when MSAA is turned
off later. The stencil replay must have taken the bit (`g_rdPerf.dateStencilTakes` above 0), else
it reports `stencil not run (no draw took the stencil bit)`, a failure. The stencil bit stays only
with its five right, else `dateStencil` goes off (`rd: profile: DATE stencil off (self-test)`) and
the snapshot draws DATE; `ICO_RD_DATE_PATH=stencil` keeps it whatever it finds. One line: `rd:
DATE: stencil path (self-test: stencil ok 5, snapshot ok 5) on SCENE ...`, or `rd: DATE: snapshot
path (self-test: stencil fail 2 at sample 1: 28C85000 for FAFA0A40, snapshot ok 5) ...`.
`ICO_RD_DATE_SABOTAGE=1` makes the setup write 0 into the bit where the alpha's MSB is 1 (logged:
`rd: DATE: ICO_RD_DATE_SABOTAGE: ...`; `port/render/rd_replay_tiler.c` (`rd__date_sabotaged`);
`rd_date_selftest_sabotage`).

## Crack filling above 1x

The VU truncates every vertex to the GS's 1/16 pixel grid, each part of the world through its own
node matrix, so two triangles that meet in the model can leave a crack of 1/16 GS pixel between
them ([DIVERGENCES.md](DIVERGENCES.md#video) VD-20; at a scale that is not a whole number the GPU
snaps the scaled position a second time, VD-19). A pixel whose centre falls into it keeps what
lies behind the two parts: the clear's GS Z 0 where nothing does, which the fog's table takes as
its entry 0, a dot of the full fog colour (the probe's `table entry 0`,
[the fog path](#the-fog-path-and-its-self-test)), or a farther surface, a dot of that surface
painted at its own, deeper fog level. At 1x those dots are the PS2's own. Above 1x a crack catches texel centres all along its length, and the dots run along
the seams of walls and floors (issue 26: rows of them on a doorway's edge and a platform's top at
4K).

Under the Enhanced preset, with SCENE's scale above 1 on either axis, SCENE's own fog arms the fill
(`doFog` calls `rd__crack_fill`) and the walk draws it after lists 4-6, from the start of list 7
on where `rd__crack_fill_place` puts it (`rd__crack_fill_at` before each command of the list loop,
`port/render/rd_crack_fill.c`; [its place](#the-fills-place-after-list-7)): the pass open there ends
with SCENE's colour copied into its snapshot texture (`rd__replay_take_snap`), and one triangle is
drawn over SCENE's colour alone: `crack_fill_ps` (`port/shaders/blit.hlsl`, behind `blit_vs`, key
`rd__crack_fill_key(RD_CRACK_KEY_FILL)`: no depth attached, no blend) reads that copy and SCENE's depth itself,
sampled where it is (`RHI_STATE_DEPTH_READ`), and gives a crack the colour of the nearer end of
its run; the depth is not written. A texel is a crack when a run of at most R texels along its row or its column, the texel among
them, has at each end a texel in front of it (written, and nearer by more than 1/64 of its own
depth, `RD_CRACK_BEHIND`, `crack_front`), and the texel is either at depth 0 (no draw covered it)
or drawn behind both ends by a step in depth (`crack_behind`): the two ends within half the drop
of each other (two parts of one surface, not a near and a far object), and the texel behind each
end's surface carried on to it (the end and the texel beyond it, a straight line in depth: GS Z
is linear across a triangle on the screen) by more than 1/64 of that depth, so the texels of a
concave fold between two faces, each on one of the two planes, stay. Depths grow with GS Z and
are linear in it on every format (`gs_z_to_depth`; the float form's top band, the UI's, only
ever reads as nearer), so 1/64 of a depth is 1/64 of GS Z: in the rooms of the corpus the drawn
cracks lie 4 to over 50 steps of the fog's index (Z bits 16..23) behind their ends, from about
1.3 % of the ends' Z, while the texels of one surface differ by 0 to 2 steps from texel to texel. The
shorter run of the two axes wins, the row on a tie, and within a run the end before it on a tie.
Every other texel is discarded. R on an axis is
ceil(scale / 2), at most 3, and 0 at a scale of 1 or less (`rd__crack_fill_reach`): 1 at 2x and
1.154, 2 at 2.8125 and 4x, 3 from 6x. The crack is under a texel wide at every scale up to 16x,
while a slit of a GS pixel or more (2 texels at 2x, 4 at 4x) is the game's and stays. The values
are exact copies, nothing filtered: the crack takes its neighbour's colour as the fog and lists 4-6
left it. The texels the first pass drew are not touched, so the picture differs from the one without
the filler only at the cracks.

**Where it is drawn, and the bound.** Until the fill moved after lists 4-6 it was drawn before the
fog with the colour and the depth (SV_Depth, test ALWAYS), so the fog then painted the crack at its
own place in the fog's dither (`fog_lut.hlsl`: the Bayer cell from the pixel's position above 1x)
and lists 4-6 tested and blended against the filled texel. After lists 4-6 the crack holds its
neighbour's finished colour: the neighbour's dither cell, and lists 4-6's draws as they fell on the
neighbour; the draws after list 7 that test the depth (the glow's mask, `staticBlur.c` `cleanUpFB`)
see the crack's own depth. The gain is the tile-based GPU's pass break: before the fog the copies
ended the scene pass in the middle and it was begun again (SCENE's colour and depth stored, both
copied and loaded again, about 120 MB a replay at 2366 x 1080 with 8 bytes a depth texel); after
lists 4-6 the pass ends where it ends anyway, only the colour is copied (20 MB read and written),
the depth is read in place (no copy), and the fill's pass loads and stores SCENE's colour alone (20
MB): about 40 to 60 MB a replay less, and one copy fewer. The store plan keeps the depth's store
for that read (`port/render/rd_store_plan.c`: a fog that may arm the fill touches SCENE's depth
again at list 7, `PlanDepth.late`; the last store's cut does not take the pass open there).
Measured with a model on lavapipe (main's replay tool, every command of lists 7-12 turned into a
NOP, the fill before the fog against none; `build-host/tmp/v098/evidence/cheap/crack_late_model.py`:
the picture without the fill is the late fill's before it, and each crack takes the colour of
whichever of its ends in it is nearest the earlier fill's, since the dumps hold no depth to pick the
nearer end as the rule does: a best case, not a bound; the same model's farthest end gives 111
levels on the loss-loss frame, of its 632 crack texels (0.06 %), and 231 to 244 on the godrays and forest frames (rd-00720, rd-01076 to rd-01079)): at Enhanced 2x 4:3 the corpus's
cracks are 0 to 0.15 % of SCENE's texels and 65 to 100 % of them come out the same bytes as before,
most others one to three levels apart, the model's largest 49 levels on two texels of a forest
frame. The whole picture against the earlier build's pass dumps (the per-wave gate at Enhanced 2x
4:3) measures past the model on the loss-loss frame, which has no glow, so its differences are the
cracks' colours themselves: 218 of SCENE's texels apart, up to 109 levels; 9 at 8 levels or more,
7 of them one crack's run along one row (x 138-144, y 279 of 1152 x 896), which the earlier fill
gave the light grey of one end (204, 204, 204) and the late fill the darker surface of the other
(117, 109, 95); 3 at 3 to 7 levels, 29 at 2 and 177 at 1 (the frames whose cracks did not change
hold 74 to 79 one-level texels in SCENE against the same baseline); DISPLAY 177 pixels apart and
PRESENT (1280 x 960) 544, up to 57 levels (7 and 23 of them at 8 or more: that run). On the
godrays frame rd-00720 SCENE, DISPLAY and PRESENT are up to 10 levels apart on 1.2 % of the
picture, the glow's mask flipping at the cracks (above); at the PowerVR phone's
Resolution Auto (2364 x 1080, its profile's fakes) the phone's seven F12 frames hold 112 to 285
cracks (0.004 to 0.011 %), 98 to 99.6 % of them the same bytes, at most three a frame apart, by up
to 20 levels; the deja vu 48:9 frame 469 cracks (0.018 %), most of them one to seven levels apart,
the largest 14 ([DIVERGENCES.md](DIVERGENCES.md) VD-20). ctest `rd_crack_late_ab` measures the
whole picture against the earlier build's pass dumps.

### The fill's place after list 7

The fill needs a pass of its own only where nothing else breaks SCENE's pass. Two effects break it
on nearly every frame, so the walk asks, before each command from list 7 on, whether
the fill can wait past it (`rd__crack_fill_place`; the store plan's scan asks the same, so the depth
store the fill reads is kept up to that command, `PlanDepth.late`):

- **state** (the commands `rd__apply_state` takes): it waits.
- **a staticBlur sprite into another target** that reads neither SCENE's colour nor its depth view
  as a texture, writes no Z into SCENE's depth and has no DATE (the aura's `AURA_WORK`, `WORK0`
  and `FEED128` sprites before the paste): it waits. The fill's inputs (SCENE's colour, its depth)
  and its texels are untouched, so it is the same bytes later.
- **the glow's `cleanUpFB`** (`staticBlur.c`): untextured sprites into SCENE whose blend keeps RGB
  for every destination (`rd__fx_fold`, Cs * As + Cd with RGBAQ 0) and no alpha test: they write
  SCENE's alpha alone. The first one, with no Z test, writes every texel's alpha (the walk checks
  that it covers the whole target, `alphaCovers` through `blurCoversTarget`; else the fill goes
  before it, said in the log), so the alpha the fill would have written before it is overwritten
  wherever it lay. The fill then waits past both sprites and is drawn with RGB alone (the colour
  mask, `RD_CRACK_KEY_RGB`): the same bytes as at the start of list 7. The two sprites stay in the
  fog's pass ([RENDER.md](RENDER.md#the-glows-passes): `fxAttachDepth`), so the glow keeps its cut.
  After them the fill waits for nothing else (the mask that reads SCENE follows).
- **the aura's paste** (`pasteFeedBackAreaToFB`: FEED128 blended over SCENE through its
  destination, no Z test): the fill is the first draw of the paste's own pass. Where the fx plan
  draws the paste by the local read (`rd_fx_plan.c` `fxDraw`), `rd__crack_fill_in_pass` copies
  SCENE's colour, begins the pass the paste would begin (SCENE's colour loaded, no depth) and draws
  the fill in it; the paste's draw lands in the same pass after the local read's barrier. Where it
  cannot (the snapshot path, an MSAA epoch, the scene's passes not readable, a pass area on part of
  the picture, a pass on SCENE already open), the fill takes its own pass before the paste, with a
  line in the log.
- **anything else** (a draw, a clear, a copy, a shadow command, a sprite that reads SCENE or writes
  its depth, the motion blur into SCENE): the fill before it, in its own pass, as at the start of
  list 7. At the walk's end a fill still waiting is drawn (`rd__crack_fill_late`).

The depth the fill reads is the one it read at the start of list 7: on every corpus and phone frame
the post sprites after list 7 draw with Z write off (the replay tool's `--list` prints `zwrite 1`,
`RD_ZWRITE_OFF`); the draws that write SCENE's depth after list 7 (the sun's fans and the lamps'
meshes into `WORK2`, characters drawn into the aura's buffer, list 11's screen sprites) end the wait
(`build-host/tmp/v098/evidence/crackaura/zscan.txt`). The bound is unchanged: every place gives the
bytes of the fill at the start of list 7 (`ICO_RD_CRACK_PLACE=7` draws it there, the tests' A/B).

What it saves on a tile-based GPU, from the PowerVR phone's log (UNVERIFIED as a measurement): in the
glow's rooms the two `cleanUpFB` passes on SCENE (each SCENE's colour loaded and stored, the second
with the depth loaded; about 2.4 ms by the glow's share); in the aura's rooms the fill's own pass
(`crack fill SCENE 1 draws 2.09-2.18 ms`, SCENE's colour loaded and stored, 10 MB each way at
2366 x 1080), its shader moving into the aura's pass. Elsewhere nothing changes.

What the cracks hold, measured on lavapipe with the replay tool and copies of the dumps (the
clear's colour or the fog table's entries patched, every command after the fog NOPed): in the
issue-91 4K dumps (scale 5.581 x 4.821) the sky is the clear, and 727 texels of the fog's entry
0 against it are filled, most along the lattice tower at the top left (the runs of 1, 2 and 3
such texels across a row fall from 97, 90 and 60 to 9, 7 and 7). The rooms of the corpus
(lightning rd-00200 and rd-00300, queen rd-00200 and rd-00300, at 2x and 4x) have no texel of
depth 0 after the world's draws: lightning's clear patched to magenta shows on no texel before
the fog, and the table's entry 0 patched to magenta on none after it in any of them, so their
texels of the clear's RGB 1,1,1 are the walls' dark mortar, not cracks. With a table that writes
each texel's index (and the fog sprite's Z raised so it reaches every texel), 22 to 105 texels a
frame at 2x (lightning rd-00200 22, rd-00300 105, queen rd-00200 49, at the index's precision) lie
in one-texel runs behind both neighbours, a farther surface seen through a seam: the
discontinuity form's share.

Not drawn at scale 1 on both axes, under the Original preset, while the MSAA epoch is open at the
fog (it draws after the resolve: [MSAA.md](MSAA.md#the-crack-filler-after-the-resolve)), in the fog's self-test (`rd__fog_exact` without the probe),
for a fog of any target but SCENE, without a sampleable depth (`RhiLimits.depthSampled`), and with
`[dev] crack_fill = false` (`ICO_RD_CRACK_FILL=0`; the replay tool's `--no-crack-fill`). On by
default where the device profile has `crackFill`: every class, tile-based GPUs included. Its keys
(the general and reach-1 fills, the count, each fill with RGB alone) join the fog's family in the
start-up set (`rd__enumerate_reachable_fog`). Cost above 1x: a
copy of SCENE's colour (4 bytes a texel read and written, 66 MB at 3840 x 2160) and one pass over
SCENE's colour, loaded and stored, reading thirteen depth texels per texel (the runs' ends, both
axes) and four more where a drawn texel lies behind both ends of a run (the texels beyond them).

On the local path (the fog read by the local read inside the scene pass) nothing breaks the scene
pass at the fog any more: the fog, lists 4-6 and every draw before them on SCENE share it (and the
glow's `cleanUpFB` after them), and the fill's snapshot ends it where the pass to the glow or the
aura ends it anyway.

The count, only under `rd__crack_fill_count_all` (the tests, the replay tool's `--stats`; never in
the game, and a missing count pipeline leaves the fill uncounted): `crack_count_ps` runs the
same test over SCENE's depth (read in place, before the fill's pass), 16 x 16 texels a texel of a small RGBA8 target (n & 255 in R, n >> 8
in G), which is copied into a readback buffer and summed once the GPU is past that replay
(RHI_FRAMES_IN_FLIGHT replays later, as the fog's edge probe; `rd__crack_fill_wait` waits for it).
`RdPerfRecord.crackFillPasses` and `crackCountPasses` count the passes of a replay. The lines:

- `rd: crack fill: on above 1x (scale 2x2, reach 1x1 texels: a run of texels no draw covered, or
  drawn over 1/64 of the depth behind both its ends, with a texel in front at each end takes the
  nearer end's colour after lists 4-6); [dev] crack_fill = false turns it off` (with ` (drawn after
  the MSAA resolve)` after it once a fill was drawn so), or `... on above 1x, not drawn at scale 1x1 (the GS's own grid); ...`, `... on above 1x,
  not drawn under the Original preset; ...`, `rd: crack fill: off ([dev] crack_fill = false,
  ICO_RD_CRACK_FILL=0)`: at the start and whenever SCENE is made
  again or the state changes (one line for each change);
- `rd: crack fill: T replays filled (scale SXxSY; reach RXxRY texels; not counted in the game[; F after lists 4-6[, K of them in the aura's paste pass][, G after the glow's alpha]][; A after the MSAA resolve])`
  in the 10 s summary (`rd__crack_fill_block`, `rd_interp.c` `presentLog`), when it filled; when
  counting, `rd: crack fill: N pixels filled over M replays counted of T filled (max K a replay;
  scale SXxSY; reach RXxRY texels)`;
- `rd: crack fill: not drawn on SCENE at scale SXxSY: <why> (reported once)`, once a run for each
  reason (MSAA, no sampleable depth, its pipelines missing, no memory for the colour's copy), and
  for the last three `rd: crack fill: N fogged replays above 1x not filled (...)` in the 10 s summary;
- once a run: `rd: crack fill: drawn as the first draw of the aura's paste pass on SCENE (the
  paste's own pass, its draw after it), not in a pass of its own (reported once)` and `rd: crack
  fill: drawn after the glow's sprites that write SCENE's alpha alone (staticBlur.c cleanUpFB), its
  RGB alone: the sprites stay in the fog's pass (reported once)`;
- `rd: crack fill: the aura's paste cannot take the fill as its pass's first draw (<why>): its own
  pass before the paste (...)` and `rd: crack fill: the glow's first sprite of SCENE's alpha does
  not write every texel's alpha: the fill before it, in its own pass (...)`, the first 8 then every
  100th;
- `--stats`: `<dump>: stats: crack fill on|off: P passes, N pixels filled (M replays counted;
  scale SXxSY, reach RXxRY texels; S fogged replays above 1x not filled; F fills after lists 4-6;
  A fills after the MSAA resolve; K fills in the aura's paste pass; G fills after the glow's
  alpha)` (P counts the fill's own passes: 0 for a fill in the paste's pass).

`rd_crack_test` (`rd_crack`, `rd_crack_4x`, `rd_crack_frac`, `rd_crack_backdrop`,
`rd_crack_backdrop_4x`, `rd_crack_backdrop_frac`, `rd_crack_1x`, `rd_crack_original`,
`rd_crack_msaa`, `rd_crack_off`, `rd_crack_depth_copy`, `rd_crack_buffer`, `rd_crack_d24s8`,
`rd_crack_nodual`, `rd_crack_tiler`, `rd_crack_tiler_on`, `rd_crack_tiler_fold`,
`rd_crack_tiler_fold_mali`), `rd_replay_tool_crack` and `rd_crack_late_ab` check it
([TESTING_PROGRAMS.md](TESTING_PROGRAMS.md)).

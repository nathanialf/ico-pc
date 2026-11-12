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
([TESTING_PROGRAMS.md](TESTING_PROGRAMS_RENDER.md)).

### The glow where it adds light

List 7's glow (`ico2/sugipon/src/staticBlur.c` `makeFullScreenFlareBefore`, `After`,
`pasteFullScreenFlare`) fills WORK2 (and its band WORK2_PAD) with RGBAQ (0, 0, 0, 192) in
postEffect 1, 3, 6 and 7, draws the sun's fans into it when the sun is on screen (`dispSun`), the
mask (SCENE's alpha, MODULATE by RGB 0), the lamps' meshes in the issue 163 rooms, reduces WORK2
into WORK0, blurs WORK0 through WORK1 ten times, copies WORK0 into WORK3 (`eyeBlur`, with the
sun's ghosts, shrink and tint when the sun is up), sets WORK0's alpha (`pasteWork0ToFB`) and adds
WORK3 to SCENE (Cs * FIX + Cd, FIX 128). Every sprite after the fill is MODULATE or HIGHLIGHT with
RGBAQ's alpha 0 per channel, so with RGB 0 in WORK2 every buffer's RGB stays 0 and the paste adds
0. Only the fans and the lamps put light into WORK2. On the PowerVR phone the chain is 11.6 ms of
a 32 ms picture in such a room (the mask's 2366 x 1080 pass 5.3 ms, the paste 3.1, twelve small
passes 2.9); with the paste not drawn (`rd_replay_tool --nop 7:311`) the phone's own frame
replays to the same bytes in every target and the present. What the chain does carry is an
alpha: WORK2's is the mask's (192 on the background `cleanUpFB` marks, where the mask's alpha
test keeps the fill; 0 elsewhere), and it reaches WORK1, WORK3 and, through the paste, SCENE.

Above 1x (SCENE at a scale other than 1: the predicate `blurAbove1xSteps` takes; at 1x nothing
changes, so Original keeps the PS2's bytes in every buffer), on every device class and backend,
a scan in the store plan's walk of the lists (`port/render/rd_glow_light.c`, file comment) reads
the chain in its known form, every command from the band's fill to the paste contiguous (band,
fill, fans, mask, lamps, reduction, the blur pairs, the eye blur, the sun's sprites, the glow
hold's copy, WORK0's alpha, the paste) and picks one plan:

- **dark** (the fill RGB 0, no fan on the picture, no lamp): nothing of the chain is drawn. Each
  buffer is left with the RGB the chain leaves: WORK2, its band and WORK3 as lazy clears (their
  colours kept until something reads them: a later replay that touches them before covering
  them, or a read outside a replay such as the replay tool's pictures, which draws the clear
  first), WORK3 a clear instead when the glow hold copies it; WORK1 0 over the first blur pass's
  rect and its glow edge strip (every later pass into WORK1 lies inside it) and over the sun's
  shrink, those two sprites drawn untextured in the colour their source holds in the chain
  (`rd_const_read.c`, the planned source), and not even drawn when a dark plan before left that
  region zero and no replay wrote WORK1 since (a steady dark room); WORK0 (0, 0, 0, A) as a
  small clear at the chain's place (the last blur pass covers WORK0; the aura blends over WORK0 in
  play, and a lazy clear drawn at its sprite would flush AURA_WORK's clear, which the clear skip
  keeps undrawn). A lazy clear is drawn only for its own target: before a sprite that reads or
  partly writes it (as that sprite's pending clear), at a later replay that touches it before
  covering it, at a read outside a replay; a buffer a command other than a sprite touches later in
  the frame (the glow hold's copy of WORK3) gets its clear at the chain's place instead (the
  upscale point reads SCENE alone). So a dark room's chain begins no render pass once WORK1 is
  zero, one (WORK1's) after a frame that drew the chain. The fans whose box misses the picture are not
  drawn. SCENE is as the paste leaves it in RGB; its alpha is `cleanUpFB`'s (0, and 192 on the
  marked background) where the paste wrote WORK3's;
- **lamps** (the fill RGB 0, no fan on the picture, lamps drawn into WORK2 after the mask without
  DATE or a blend on the destination's alpha): the fill drawn as (0, 0, 0, 0) and the mask not
  drawn (it writes RGB 0 over a fill of RGB 0); the lamps and the rest of the chain as before;
- **whole** (the chain as before), counted by reason: a grey fill (postEffect 4 and 5, the bloom),
  any other form, the MSAA epoch, the sun's fans on the picture, the sun's branch with the lamps,
  the sun's glare onto SCENE, a later command reading WORK1, WORK2 or WORK3, a command reading
  SCENE's alpha (a texture of SCENE, DATE, a blend on Ad, a copy) before a sprite rewrites it whole
  (the aura's paste or list 11's full-frame sprite does in every corpus frame), SCENE's alpha not
  rewritten in the frame.

The fans keep the chain whole: the sun's tint (the eye blur's last sprite, Cs * As + Cd) takes As
from one texel of WORK1 at the sun, which the shrink copies from WORK3, the eye blur from WORK0,
the blur pairs from the reduction of WORK2's alpha over their reach: so the mask decides WORK3's
RGB wherever the blur reaches the sun, not only inside the fans' box, and a mask cut to that box
would change the tint. The same holds for the lamps with the sun up.

So every target's RGB is the chain's, byte for byte, and the alpha of WORK1, WORK2, WORK3,
WORK3_HELD and SCENE (until it is rewritten) differs only where WORK2's alpha was not 0: on the
marked background and its bilinear rim ([DIVERGENCES.md](DIVERGENCES.md), VD-30). A later frame
that reads WORK1, WORK2 or WORK3 before the chain writes them, after a frame the plan left
undrawn, turns the plan off from then on (logged). The WORK2 fold (above) stands down in a frame
the plan takes. Skipping a draw does not move the crack fill, which the list loop places from
the recorded commands. In the 55 glow dumps on the maintainer's machine the scan's rules, applied to
their command lists by hand, take dark in 32 (the phone's frame among them), lamps in 6, and
keep 17 whole: 15 for the sun's fans on the picture, 2 (the issue 189 frame and the one before
it) because the anti-aliasing's copy reads SCENE before list 11's sprite rewrites its alpha;
`rd_glow_light_ab` counts the plans the code takes. `ICO_RD_GLOW_LIGHT=0` draws the chain whole, the tests' A/B
([TESTING.md](TESTING.md#environment-switches)); there is no setting.

Lines: `rd: glow: light plan: above 1x nothing in frame N puts light into the glow ...` and
`... frame N's light in the glow is the lamps' ...` once each, `rd: glow: light plan: the chain
drawn whole: <reason> (<detail>; frame N; reported once for this reason)`, `rd: glow: light plan:
frame N reads WORK1, WORK2 or WORK3 before ...: the plan is off from now on`; the 10 s block's
`window: glow: light: N dark (P passes in their chains), L lamps (mask not drawn), K whole
(counts by reason); lazy clears drawn: N (target n, ...)` (0 in a dark room of play);
`rd_replay_tool --stats` `stats: glow: light plan on|off: <plan> (<reason>); P passes in the
chain, lazy clears drawn: N (targets)` (the tool's own reads of the targets draw them too).

### The lamps' glow for less

In a frame whose light plan is lamps (the title's castle, issue 163's rooms) the light is the
lamps' meshes: list 7 draws them into WORK2 under a GEQUAL test of SCENE's depth (writing it),
after WORK2's fill drawn as (0, 0, 0, 0). On the PowerVR phone (the opening's first three 10 s
blocks of its log, 318 of 358 glow pictures lamps) that pass is WORK2 at 2366 x 1080 with its colour
cleared (the fill: a pending clear taken as the pass's load op, 9.7 MB) and stored (9.7 MB) and
SCENE's depth loaded (9.6 MB) for a dozen small draws, and WORK2_PAD's band a pass of its own
(424 x 64, no draw). `port/render/rd_lamps.c` (file comment) cuts both, the same bytes in every
target:

- **the area** (`RD_LAMPS_AREA`): the lamps' pass gets a render area, the union of the lamps'
  screen boxes and of the region the last lamps frame left lit, aligned out to 32 x 32 tiles.
  A lamp's box is the occlusion test's samples' box of its stream's bound through its matrix
  (`rd_vis_hiz.c rd__vis_hiz_occludee`: every corner in front of the eye, inside the GS region),
  padded by two GS pixels and taken through the target's texel scale and Full pixel's crop; the
  RHI cuts every scissor to the area. Outside it WORK2 already holds (0, 0, 0, 0): the tracker
  holds the region a confirmed lamps pass may have lit (its lamps' boxes), every other texel
  stored as (0, 0, 0, 0) by that pass, and the area covers that region, so the texels it leaves
  are the fill's colour before and after. SCENE's depth outside the area is untouched by the
  lamps: the whole pass loads and stores it unchanged (or drops a store nothing reads), the area
  pass leaves it as memory holds it. The tracker forgets the region at a new renderer or WORK2
  texture, after a replay that writes WORK2 any other way (a whole frame's mask, any command
  into WORK2 but the lamps plan's fill, mask and lamps, or the dark plan's undrawn chain), at any
  other pass on WORK2's colour, and when the planned pass does not begin with the fill's clear
  to (0, 0, 0, 0) and a colour store; a dark frame keeps it (its chain draws nothing into WORK2).
  So the first lamps picture after anything else is drawn whole and the area is taken from the
  second. A clear over a render area smaller than its attachment is a form no other pass took
  (the pass area gives none to a clearing pass), so the area is on only where its start-up
  self-test passed (a 64 x 64 colour target and its depth cleared whole, then a pass over a
  32 x 32 area with the colour cleared and the depth loaded, read back: the area 0, the rest and
  the depth as the first clear left them), on the PowerVR and desktop classes under Vulkan where
  the profile's pass area is on (by default the PowerVR phone alone); off on the Arm, Qualcomm
  and Apple classes and the other tile-based phones, and on OpenGL, Direct3D 11 and 12, which
  ignore a render area;
- **the band** (`RD_LAMPS_PAD`): WORK2_PAD's band (read by nothing) is left a lazy clear, as the
  dark plan leaves it (drawn where something reads it, at a later replay's begin or a read
  outside a replay), not a pass of its own: on every class and backend, where the dark plan's
  lazy clears already run.

The lamps' writes into WORK2 make the crack fill's hold impossible in these frames (the late
passes above: the fill reads SCENE's depth at its draw and the lamps write it, so a fill moved
past them would read other depths; `rd: late pass: ... N fills not held (no paste in the frame
takes them)`), and the glow's paste already shares list 11's pass (drawn early there, list 11's
draws joined to it).

`ICO_RD_LAMPS=0|1|area|pad` (the tests only, [TESTING.md](TESTING.md#environment-switches)) turns
both off, both on, or one on whatever the class (the self-test still decides the area);
`ICO_RD_LAMPS_POISON=1` clears WORK2 to magenta before every lamps pass drawn whole, so a pass
that relied on WORK2's earlier contents where it should not would show it (`rd_lamps_ab`). Lines:
`rd: lamps: area on|off (<why>); band lazy on|off: ...` when it changes, `rd: lamps: area
self-test passed|failed|not run ...`, once `rd: lamps: WORK2's lamps pass loads, clears and
stores WxH at X,Y of TWxTH ...` and `rd: lamps: the lamps' pass on WORK2 drawn whole: <why>
(reported once for this reason)`; the 10 s `window: lamps: N lamps passes on WORK2 with an area
(P % of their texels loaded and stored; X MB not cleared, Y MB of colour and Z MB of depth not
stored, W MB of depth not loaded), M whole (counts by why); B bands left lazy (WORK2_PAD: a pass
fewer each)`; `rd_replay_tool --stats` `stats: lamps <bits> (self-test <state>): N passes with
the lamps' area (WxH at X,Y of TWxTH: A of T texels; C bytes not cleared, S colour bytes not
stored, L depth bytes not loaded, D not stored), M whole (<why>); B bands left lazy (WORK2_PAD
WxH)`.

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
`doBlurSpriteDraw` picks them, `rd__set_a1x_gs_keep` included). No fold while a temporal upscaler
is in force ([RENDER.md](RENDER.md#the-upscale-point): the split, `rd__upscale_point_on`): the plan
is not made, `rd__fades_fold_skip` draws every sprite and the reduction applies none. Under the
display layer ([DISPLAY_LAYER.md](DISPLAY_LAYER.md)) the fades, the brightness and the bars are drawn
into the display-scale composite after the upscale, and the reduction reads the composite (`fades
fold: off while a temporal upscaler is in force; lists 11-12's sprites are drawn into the
display-scale composite after the upscale (the display layer)`, once a run); with `[dev]
display_layer = false` the upscaler must see them in SCENE and the reduction reads its output
(`fades fold: off while a temporal upscaler takes SCENE at the reduction; list 11's sprites are
drawn into SCENE`). Folding them over the composite is not done: the fold's list-12 rule fails on
the game's text in list 12 there.

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

### The present from SCENE (off by default until measured)

Not byte for byte, so committed off: `RD_PRESENT_SCENE_DEFAULT` (`port/render/rd_present_scene.c`,
the one constant) is 0 and the owner turns it on after seeing the measurement of the corpus
(`build-host/tmp/v0115/present_scene_measure.sh`, machine-local, its report beside it);
`ICO_RD_PRESENT_SCENE=0|1` is the A/B's override ([TESTING.md](TESTING.md)), no config key. It
has no [DIVERGENCES.md](DIVERGENCES.md) row while it is off.

**What it saves.** Above 1x the reduction (list 12: DISPLAY's black clear, then SCENE's textured
sprite, [The fades fold](#the-fades-fold)) is the hardware sprite (`port/render/rd_blur.c`
`rd__blur_screen_fallback`), and at a scale that is not whole its sample is moved onto a SCENE
texel's centre (`rd__blur_snap_verts`). With full height DISPLAY has SCENE's texels, so there the
reduction stores DISPLAY(x, y) = g(SCENE(x + dx, y + dy)): g its own arithmetic on one texel (the
fades fold's steps, `sprite.hlsl` `sprite_fold` without bars, then the texture function with the
tint), (dx, dy) whole texels. Measured on the PowerVR phone's dump at 2364 x 1080 (frame v3677 of
`build-host/tmp/v0115/evidence/pixel`): (1, 0), on every texel, the edges as the sprite's CLAMP holds
them, g the brightness's white at 7/128. The present's box blit then samples DISPLAY with the
hardware filter (2343 texels onto 2364 pixels there). The reduction's pass, 9.7 MB stored at that
size (1.32 ms a picture in the phone's log of 2026-11-10), is the cost: where nothing but that box blit
reads DISPLAY before DISPLAY is written again, the reduction is not drawn and the box blit samples
SCENE through g instead (`port/shaders/blit.hlsl` `blit_scene_ps`): the place in DISPLAY it would
have sampled, clamped to DISPLAY's texel centres (its CLAMP), moved by (dx, dy), SCENE's taps with
the same filter, then g. DISPLAY is then owed.

**The map** (`rd__present_scene_axis`, each axis): DISPLAY texel x's centre is GS coordinate
(x + od) / sd (the exact viewport puts GS coordinate g at texel g sd - od + 1/2, `rd_replay.c`
`rd__frame_group_ex`); the sprite's U there less the sample shift (sd - 1) / (2 sd) GS pixels
(`setUvShift`, `snapAxis`) lands at SCENE's texel coordinate u ss - os (`common.hlsli`
`gs_uv_norm`). For the reduction's pair (corners a quarter pixel before pixel 0, U 0.5 there, one
GS texel a pixel; V two a row) that is x + round(s / 4) + 1/2 across at a scale s that is not whole
(x + s / 4 + 1/2 at a whole one: half a texel at 2x, so 2x is not taken) and y + 1/2 down at full
height (one SCENE row a DISPLAY row; two at half height, not taken). The pair is taken when every
DISPLAY texel's centre is covered and samples a SCENE centre within 1/1024 of a texel (the float
error of the corners' UVs is about 2 x 10^-4 there: a neighbour's weight that small moves a sample
less than a quarter level), the offset the same at both ends.

**The bound.** Today's present is R(sum w_i g(S_i)), the present from SCENE g(R(sum w_i S_i)): R
the filter's rounding to a level, w_i its weights (summing to 1), S_i SCENE's taps. A step is g(c) =
A(c) + e(c) with A affine (slope a = 1 - k / 128 for the step's k) and e its rounding, in (-1/2,
1/2] under the fixed-function blend's nearest level, in (-1, 0] under an exact blend's floor: an
interval one level long either way. Then the old present is A(x) + e1 + r1 with x = sum w_i S_i, e1
the weighted mean of the taps' e (in the same interval) and |r1| <= 1/2; the new one is A(x + r2) +
e2 = A(x) + a r2 + e2 with |r2| <= 1/2 and e2 in the interval. The difference a r2 + (e2 - e1) - r1
is under a / 2 + 1 + 1/2 < 2: at most one level, wherever the sampler weighs SCENE's taps as it
weighed DISPLAY's. A step of k 0 or 128 (and the tint 0x80, the texture function's identity) has e 0
and a 0 or 1: the same level. Above 1x the fades fold takes at most one step between levels a row,
so the bound holds for every fold it makes. The moved coordinate is computed in floats: where the
sampler's weight then rounds to another step (8-bit fractions: 1/256 of a texel), the filtered
value moves by up to |S_i - S_j| / 256, under one more level: two levels at most on such hardware.
A Picture tone row scales a difference by its slope. `rd_present_scene_test` checks the model (one
level, every pair of levels and weight; a bilinear's four taps at random weights);
`rd_present_scene_ab` holds the corpus to two.

The conditions (each keeps the reduction, counted with its reason, `rd_present_scene.h`
`RD_PS_WHY_*`; when in doubt the reduction is drawn):

- the switch; a device and `blit_scene_ps` in the shader table (`g_rd.fs`); no pass window
  (`g_rdWin.first`: the tool's bisect reads targets mid-frame);
- the hardware sprite (scale 1, the Original preset and 1x keep the GS sprite model); no MSAA
  (`rd_aa_samples`); no GS state as shader data (`rd__uber_on`); no upscaler set
  (`RdSettings.upscale`: FSR 1.0 reads DISPLAY, a temporal one takes SCENE at the reduction,
  `rd__upscale_point_on`); not the display layer in force (`rd__layer_active`: SCENE lent to the
  composite this replay; `rd__layer_on` is only its switch, and the layer runs only under a
  temporal upscaler);
- the present reads DISPLAY in its box blit alone (`rd_present.c`
  `rd__present_scene_present_why`): no CRT filter, no Downsampling, no doubled lines (full height),
  no effects depth, no capture armed or pending, no box average of DISPLAY into the window
  (`reduceApplies`);
- the border open (`rd__replay_border_open`: the scissor the whole target, CLAMP on both axes)
  without the exact width's repeated outer columns (`rd_blur_border.c` `drawStrips`);
- the pair's form (`planPair`): the black sprite, then only state, NOPs and deferred text, then
  the textured sprite into DISPLAY reading SCENE's colour (PSMCT32), MODULATE or DECAL with a flat
  tint of 0x80, no blend, DATE, alpha test or FBA, the colour mask whole, no Z test or write, no
  field or UV offset, over every GS pixel (`extendFullFrame` leaves it) and every DISPLAY texel;
  the map above on both axes; the fades fold, if planned, at the textured sprite and without bars;
- nothing after the textured sprite in the frame touches DISPLAY or SCENE (a draw into or from
  either, a copy, a clear, a command not known; the deferred text's quads aside).

**The owed DISPLAY.** Every other reader gets DISPLAY drawn first from the same SCENE by the same
arithmetic (`blit_scene_ps` one texel a pixel by Load: the reduction's bytes, which
`rd_present_scene_ab` holds DISPLAY to): a later replay's frame read once at its start
(`rd__present_scene_begin`, from `replayFrame` after the store plan's scan, `rd__present_scene_settle`)
draws it in the replay's list when a command reads DISPLAY before DISPLAY is written whole (a keep
frame's KEEP sprite, the motion blur's DISPLAY, the interpolation's held copy at the head, the
water's block of FBP 0) or when the frame draws on SCENE without writing DISPLAY again; the
reduction's black sprite over the whole of DISPLAY, or a clear of it, first drops the debt; a frame
touching neither (the still re-present's empty keep frame, `rd_reshow.c`) keeps it, and its
present samples SCENE again. A read outside a replay (`rd__read_target`: the F12 dump, a save
state's picture, the tests and the replay tool's PNGs; `rd_display_probe`) draws it in a frame of
its own; a present that cannot read SCENE after all (a capture armed since the replay) draws it in
the present's list first. SCENE is that frame's then: only a replay draws on it and every replay
settles the debt before its first command; its texture and generation are checked, and a SCENE
made again with DISPLAY kept loses the picture (said; the switch off from then on). The crack
fill's swap ([The crack fill's snapshot by the swap](#the-crack-fills-snapshot-by-the-swap)) is no such recreation and takes no refusal from
it: it swaps SCENE's colour before the reduction, the plan records the colour SCENE holds at the
reduction, the present reads SCENE's record's colour when it draws, nothing after the reduction may
touch SCENE, and a later replay settles the debt before its own swap. A new context
owes nothing (`rd__present_scene_init`). The pipelines (`blit_scene_ps` on both outputs) are in
the start-up set while the switch is on; every backend draws it from the shared module (SPIR-V on
Vulkan and MoltenVK, DXIL on Direct3D 12, FXC's run-time compile of `blit.hlsl` on Direct3D 11, the
SPIRV-Cross GLSL on OpenGL). No new pass kind.

The record counts it (`RdPerfRecord.presentSceneSkipped`, `presentSceneShown`, `presentSceneKept`
and `presentSceneWhy`, `presentSceneDrawn` and `presentSceneDrawWhy`); `rd_replay_tool --stats`
prints `stats: present from SCENE on|off: S reductions not drawn, P presents from SCENE, K kept
(why), D drawn on demand (why)`; the log says:

```
rd: present from SCENE: off (the default until it is measured): the reduction is drawn into DISPLAY and the present reads DISPLAY
rd: present from SCENE: the reduction not drawn (frame N): DISPLAY is SCENE through the reduction's steps at a whole offset (1, 0) texels (SCENE 2364x1080 at 2.788x2.411, 2 fold steps); the present samples SCENE, every other reader gets DISPLAY drawn on demand (reported once)
rd: present from SCENE: the reduction drawn into DISPLAY: <why> (frame N; reported once for this reason)
rd: present from SCENE: DISPLAY drawn on demand from SCENE (frame N's): <why> (the first 8, then every 100th: K so far)
rd: present from SCENE: 0.98 a replay (0.98 presents from SCENE), 0.02 through DISPLAY (<n why>, ...); D drawn on demand (<n why>, ...) (M replays, 10 s)
```

the last every 10 s while the switch is on. Tests: `rd_present_scene` (the map, the bound's model,
every reason, the debt's verdicts on recorded frames) and `rd_present_scene_ab` (the corpus,
[TESTING_PROGRAMS_RENDER.md](TESTING_PROGRAMS_RENDER.md)).

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

    A_i = S_i + a' (A_i-1 - S_i),   a' = FIX' / 128,   FIX' = clamp(round(128 (FIX / 128)^(dt / tick)), FIX, 127)

with S_i SCENE just before the sprite, A an RGBA16F ping-pong pair at SCENE's texture size (its
scale, its Full pixel crop; an 8-bit running blend would stall up to 7 levels short at 10 presents
a tick, its floor step `(A - S) FIX' >> 7` being 0 below `128 / (128 - FIX')`), dt the time since
the present before and tick the game's tick, dt held to [tick / 64, tick]
(`rd__blur_smooth_fix_dt`). The presents of any span keep `(FIX / 128)^(span / tick)` of the
trail, the PS2's share a tick, whatever their count or spacing (1.7 presents a tick, uneven gaps,
Uncapped); the first present after a pause (dt held to the tick) draws the game's FIX. Without a
time dt is tick / k, k the presents a tick: `FIX' = clamp(round(128 (FIX / 128)^(1 / k)), FIX,
127)` (`rd__blur_smooth_fix`, equal to the time form at dt = tick / k for every FIX and k). Each
present: the sprite's pass ends; SCENE goes to a shader read (an open MSAA epoch
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

dt and k come from the presenters (`rd__blur_smooth_arm`, before each replay that presents). On the
timer path `rd_present` hands every present the present clock's time since the present before and
its tick (`rd_present_clock_alpha` notes them with its alpha, `rd__present_clock_take`; the render
thread gets them with the posted picture, `rd__thread_clock`), and FIX' is the time form. Under
the vsync lock no clock runs: k is the presents the tick before counted (`rd_present`), so dt =
tick / k is the refresh period; k is 1 for the first tick, for the presenter without interpolation
(`rd_core.c` (`replayOnHost`), `rd_thread.c` (`replayJob`)) and for the replay tool's replays
except `--presents N` (k = N), clamped to 1..64 (`RD_BLUR_SMOOTH_K_MAX`, also the time form's
floor). k is also taken on the timer path at a tick's first present, the clock's tick over its
step (`rd__present_clock_k`, `port/render/rd_present_clock.c`), for the block line. FIX' from that
k on the timer path is what issue 193 showed (an Adreno 740 at Frame rate 60): a clock step stuck
at 0.5 ms (a reset that took one doubled present's interval as the step, with nothing to learn it
again) gave k 64, FIX 32 drawn as 125 at 1.7 presents a tick, a trail keeping 96 % of itself a
tick instead of 25 % (the clock's step: [DISPLAY.md](DISPLAY.md#interpolation-between-ticks), "The
alpha"). A replay nobody armed (a self-test, a dump's, the replay tool's `--time`) draws the
sprite as Original and leaves A alone. `feedback` makes no DISPLAY / DISPLAY_HELD head copy while `rd__blur_smooth_active` (the
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
motion blur: smooth: P presents, K1 to K2 presents a tick (by time; the present clock's step S ms, N resets), FIX F drawn as F' (the last); D sprites through the accumulator, R restarts (M not named above), O drawn as Original (no accumulator)      (with the interp summary, when it drew; "(counted; no present clock)" when no present of the block was timed: the vsync lock)
motion blur: smooth: the trail is held much longer than the game's: FIX F drawn as F' for 32 presents in a row at C presents a tick (counted; k K at the tick's first present, the present clock's step S ms, this present D ms after the one before, the tick T ms), to frame N: the trail keeps X % of itself a tick where the game keeps Y % (run R)      (FIX' at least 120 for 32 presents in a row at under 8 presents a tick counted, the game's FIX under 120; a line a run, the first 8 then every 100th)
```

The block line's K1 to K2 are the ticks' k (the clock's tick over its step on the timer path), its
step and resets the present clock's (its resets in the 10 s block): a step near the presents'
real interval and k near the presented fps over the game fps of the window's line of the same
block is the healthy reading, with a reset or two a block (a pause, a change of rate). The clock
learns any slower rate (a 120 Hz FIFO at 25 presented fps: one reset, then none) and a doubled
present moves it by its own interval, so a reset at nearly every present means presents so
irregular that the clock is over half a tick from the measured time at each one (the measured
alpha passes on, unsmoothed); a step far below the interval with it would be issue 193's stuck
clock, which the step's lower bound rules out. The held line names the fault directly: under the time form FIX' 120 or more needs a present
under a 20th of a tick after the one before (at FIX 32), and 32 of those in a row do not happen at
under 8 presents a tick.

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
target's centre texel, so the 4:3 columns of a wide frame carry the pattern of the 4:3 frame, and
its phase moves with the frame's jitter (`g_jitter`, [Jitter](RENDER.md#jitter)) in sixteenths of
a texel: the cell is the pixel's plus `floor(g_jitter * 16 + 0.5)` (a jitter of 0.25 texel moves
the pattern 4 cells, 0.125 two). Under a temporal upscaler the pattern then changes frame to frame
and the accumulation averages it out (issue 203: one pattern fixed on the render grid under a
picture that moves a fraction of a texel a frame is the thin lines FSR 3.1 picks out). Indexing at
the pixel less the jitter does not do it: a pixel centre `k + 0.5` less any jitter under half a
texel floors to `k`. `g_jitter` is exactly 0.0 without an upscaler, where the term is 0, so every
other picture is the same byte for byte (`rd_fog`'s case j, the fog self-tests and the pass-dump
corpus unchanged). At
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
it alone, [The replay](RENDER.md#the-replay)), 7 the shadow tiles' mark (below). A count's INCR_WRAP or DECR_WRAP through the write
mask 0x3F leaves bit 6 as it was and gives bits 0-5 the same value whatever bit 6 holds, so the
two never meet; the shadow reset clears the whole stencil, which DATE's setup writes again in the
pass that needs it (`port/render/rd_pipeline.c` (`shadowStencil`)). So outside the shadow window
nothing reads a stencil a pass before it stored: where its passes load and store it (inside the
window only, or with the depth on every pass on most tile-based GPUs) is
[WORK_BUFFERS.md](WORK_BUFFERS.md#the-stencil-outside-the-shadow-window); the count and the
composites drawn over the volumes' box alone are
[there too](WORK_BUFFERS.md#the-shadows-over-the-volumes-box).

### The shadow tiles

The volumes' box is the volumes' extent; they sweep open space, so on the owner's phone it is the
whole picture at the bridge and in the cage while the shadow that lands is small: the bridge
frame v2809's pass dump (the phone's recipe) has alpha above 0 in SHADOW0 (144,82)-(262,192) of 424 x 256,
SHADOW1 (72,41)-(130,96) of 212 x 128, SHADOW2 (37,21)-(64,47) of 106 x 64. Where a shadow lands is
known only on the GPU once the levels are drawn, so `port/render/rd_shadow_tile.c` culls each
composite there with the stencil:

- **The masks.** Once a replay, at the first composite that takes the tiles and while no pass on
  its target is open (the levels' passes are on SHADOW0-2), one pass a level the replay's boxed
  composites read (`shadow_tile_mask_ps`, `port/shaders/blit.hlsl`): a texel per 2 x 2 level
  texels, 1 where any level texel within 3 texels of the block (5 with the B-spline composites)
  holds alpha above 0, by integer loads (an 8-bit alpha above 0 reads above 0.0 exactly; 64 loads
  a mask texel), the indices clamped, or wrapped where the first composite's sampler state repeats
  (taken as wrapped even where the replay clamps a screen copy: that only widens the mask).
- **The set-up** (`rd__shadow_tile_take`, in place of `rd__replay_date_take` for such a composite,
  in its own pass, over the same area): `RD_STENCIL_DATE_CLEAR2` writes 0 into bits 6 and 7;
  `RD_STENCIL_TILE_MARK` writes 1 into bit 7 where `shadow_tile_mark_ps` finds the mask block of the
  pixel's UV 0 (the composite's sprite map at the pixel's centre: `g_param`); after the colour
  barrier `RD_STENCIL_DATE_SET_T` writes 1 into bit 6 where the alpha's MSB is set and bit 7 is 0.
  The composite's passes test `RD_STENCIL_DATE_TEST0_T` / `TEST1_T` (read mask 0xC0, the reference
  0x40: GREATER holds for 0 alone, EQUAL for 0x40 alone), which is TEST0's or TEST1's verdict where
  bit 7 is 0. The set-up is always taken whole and never reused (`dateStFor` 0): every later DATE
  draw takes its own, which writes bit 6 over its area again.

**The same bytes.** A dead tile's pixel: the mark's UV is the affine the GPU interpolates for the
composite (up to rounding and FrameCB's half texel, under a level texel); the composite's bilinear
taps lie within one texel of its UV (two with the B-spline, plus the GS pixel's snap above 1x), all
inside the block's margin, all alpha 0, so As is 0 and its alpha test (GREATER 0, AFAIL KEEP, no Z
write: the box's composite form) writes nothing; the stencil culls it, which writes nothing either.
A live tile's pixel: bit 7 is 0, bit 6 as before, the composite's own geometry, interpolation and
shader. Bit 6 in a dead tile is 0 rather than the MSB: only that composite reads it, and it is culled
there. Bits 0-5 are never touched. So no `DIVERGENCES.md` row. The composite's geometry is not
split into tile quads: interpolated UVs at a pixel are not provably the same bits in another
triangle, so the cull is the stencil's, before the fragment shader where the device tests early.

**Where.** Off on every device by default (`RD_SHADOW_TILE_DEFAULT` 0 in
`port/render/rd_shadow_tile.c`; no start-up work, no keys, no self-test then). With that constant 1
it would be on for the PowerVR class and the desktop classes where the start-up self-test passes,
and off on the Adreno and Mali classes (issues 179, 211, 123), Apple and other tile-based GPUs.
Only where DATE takes the stencil bit (Vulkan with the local read): the snapshot path's composites
are drawn whole; the shaders are made on Vulkan alone. A composite whose box is empty, outside the
MSAA epoch's rule, or not boxed (rd_shadow_box.c refuses it) is drawn as before. Drawn whole and
counted (`RdPerfRecord.shadowTileWhy`): off on the device, a pass on the composite's target open (no
mask can be drawn without breaking it), no mask, not one sprite or a degenerate map or another wrap
than the mask's, the tiled set-up's pipelines missing. `ICO_RD_SHADOW_TILE=0|1`: the tests' switch
([TESTING.md](TESTING.md)); no setting.

**The self-test** (`rd__shadow_tile_selftest`, from `rd_fog_selftest` once a renderer after the
stencil's): a frame at SCENE's size: list 0 a ground (its top half's alpha MSB 1, its bottom
half's 0), list 3 a volume's count on WORK2, SHADOW0 from it, one composite into SCENE (the TEST
word 0x3400D, LERP_AS FIX 0), list 4 a DATE DATM 1 sprite (a later reader of bit 6); replayed with
the tiles off and on, MSAA held off: SCENE byte for byte, the composite drawn over a mask with dead
tiles. A failure keeps the tiles off.

**The lines.** Once at start: `rd: shadow tiles: off (<why>)` (by default: not measured to help) or,
with the tests' switch, `rd: shadow tiles: on (ICO_RD_SHADOW_TILE=1; self-test ok: ...)`; once: `rd: shadow tiles: the first composite over the tile mask
(...)`; a composite drawn whole: `rd: shadow tiles: a composite drawn whole: <why> ...` (first 8,
every 100th). The 10 s `window: per replay` line: `; shadow tiles: C composites a replay over the
tile mask (M masks), K whole (<why> n)`. `rd_replay_tool --stats`: `stats: shadow tiles on|off: C
composites over the tile mask (M masks), K whole (...); tiles live SHADOW2 l of n SHADOW1 ...
SHADOW0 ...` (the masks read back after the replay; the device's own log has no tile count, which
would need a readback a frame).

**Why it is off.** On the bridge frame at most 3717 of SHADOW0's 27136 mask texels, 1023 of
SHADOW1's 6784 and 289 of SHADOW2's 1696 are live, so most of the composites' and set-ups'
fragments are culled. On the owner's PowerVR phone (the self-test passed: `400 of 16384 mask
texels live`) the bridge's list 3 class was 1.7 ms slower with it. The dead fragments it culls
were already cheap there: the composite rejects its fragment after one bilinear tap (the alpha
test), and the set-up's bit-6 draw after one local read, on a GPU whose tiles stay on the chip; the
cull adds, a composite, a full-area mark draw (a texture load, a discard, a stencil write) and, for
every fragment of the clear, the set-up and the composite, a stencil test against a value the same
pass just wrote, and the mask passes. The same tiles drawn as quads whose vertex shader collapses
the dead ones would remove the fragments instead of testing them, but not exactly: a pixel's
interpolated UV in a tile's triangle is not provably the bits the whole sprite's triangle gives
(the interpolator's arithmetic is the implementation's), and a UV computed from the pixel position
in the fragment shader is not the interpolated one either, so either form can move a bilinear
weight's last bit. Nothing exact is left to cut here on such a GPU.

**The tests.** `rd_shadow_tile` and `rd_shadow_tile_selftest` (`port/render/test/rd_shadow_tile_test.c`)
and `rd_shadow_tile_ab` (`tools/shadow_tile_dumps.txt`, the bridge frame's live texels under the
bounds above) ([TESTING_PROGRAMS_RENDER.md](TESTING_PROGRAMS_RENDER.md)).

### A shadow window with no volume

Many title pictures hold a shadow window with no volume in it: the corpus boot frames rd-00400 and
rd-00600 (the castle behind the title menu) reset the stencil at list 3 command 6 and resolve it at
command 13 with no `RDC_SHADOW_STRIP` between, then draw the three levels and the three composites
as every frame does. Drawn as they are, the resolve ends SCENE's pass (the count's own pass, the
three levels' passes) and the first composite begins it again: SCENE's colour, depth and stencil
stored and loaded again for nothing. On rd-00400 at the phone's recipe (lavapipe,
`ICO_VK_FAKE_LIMITS=moltenvk ICO_RD_LATE_PASS=1`, Enhanced 2364x1080), the chain left out by
`--nop` measured 4 passes and about 44 MB a picture (colour 9.7 MB stored and 10.1 loaded, depth
and stencil 12.2 each way), every pass-dump target byte for byte
(`build-host/tmp/v0115/design/OPENING_SPLIT.md` A.4). `port/render/rd_no_volume.c` keeps the pass
open there:

- **The scan** (the store plan's walk, after the box's roles: `rd__no_volume_scan_cmd`): one window
  in the replay (a reset, then its resolve on the same count target and depth, nothing but state
  between), no strip in it; after the resolve, the levels and composites
  [the box's scan](WORK_BUFFERS.md#the-shadows-over-the-volumes-box) named (a level also writing no
  depth), and nothing else touching the count target or a level: no draw on them, no texture of
  them (a screen-prim command's with TME on, any other command's on or off), no clear, copy, exact
  blend, upscale point or post record naming them, no second window, no command the scan does not
  know; neither the count target nor a level cleared earlier in the replay (a clear still pending
  would land after the zero).
- **The walk.** The reset and the resolve record nothing and end nothing (`rd__no_volume_reset`,
  `rd__no_volume_resolve`); the levels and composites are not drawn (`rd__no_volume_skip`, at the
  top of `doScreen`: the open screen run flushed, the write serial moved as a drawn one's may); the
  count (the reduced count above 1x, the count target at 1x) and each level's target are owed a
  zero, recorded at the next end of a pass the walk makes anyway (`rd__no_volume_flush` from
  `endPass`, after the pending clears, and at the walk's end) as a pass with a CLEAR (0, 0, 0, 0)
  load and no draw, each target then put in SHADER_READ. The volumes' merge does not arm a pass
  for such a window ([WORK_BUFFERS.md](WORK_BUFFERS.md#the-shadow-volumes-in-the-scenes-pass): its
  stencil store, 2.4 MB at 2364x1080, reads nothing). Lists 0 to 6 then draw in one pass on SCENE
  where they drew in two.

**The same bytes.** With no strip the stencil's bits 0-5 are what the reset left, 0, so every count
texel is (0, 0, 0, 0) on every path (the one draw's average, the boxed one draw's CLEAR, the seven
draws' CLEAR with no texel passing, the reduction of a zero count). A level in the box's form (one
sprite over its whole target at scale 1, no blend, FBA 0, every channel, MODULATE or DECAL with TCC
1, no alpha test, DATE or Z test, no depth written) whose taps all read 0 writes (0, 0, 0, 0)
everywhere: (0 x Cv) >> 7, or 0. A composite (an alpha test failing on As 0, AFAIL KEEP, no Z
write) whose taps read alpha 0 writes nothing. So the chain's whole effect is those targets at 0
and SCENE untouched, which the zero clears write; nothing reads those targets between the resolve
and the clears (the scan), so every reader after (the pass dumps, the readbacks, the next replay)
sees the same bytes. Skipping the reset leaves the stencil's bits 0-5 unwritten; only a window's
volumes and resolve read them, and the next window resets them first (DATE's set-ups and AA1's
pairs write bit 6 alone). The next DATE draw takes its own snapshot or set-up (the write serial
moved), from the alpha as it is. No `DIVERGENCES.md` row.

**Where.** On by default on the PowerVR class and MoltenVK under Vulkan, where a pass's load and
store cost; off on the Mali and Adreno classes (issues 123, 179, 211: their pass structure stays as
it shipped), Direct3D, OpenGL and the desktop. Not taken where a composite's DATE would take the R8
snapshot or the shader's read (DATE's passes without the depth,
[WORK_BUFFERS.md](WORK_BUFFERS.md#dates-passes-without-the-depth), predict their runs from the
composites), under MSAA, the tool's pass window, the exact blend or GS state as shader data.
`ICO_RD_NO_VOLUME=0|1`: the tests' switch ([TESTING.md](TESTING.md)); no setting. No start-up
self-test: it adds no kind of GPU work (a colour pass with a CLEAR load and no draw is what
`flushClear` and the boxed empty count record on every device today) and otherwise begins fewer
passes of the kinds already drawn.

**The lines.** Once a renderer: `rd: shadows: no volume: on|off (<why>): a shadow window without a
volume keeps SCENE's pass open, ...`; once: `rd: shadows: no volume: frame F, the window at commands
R..S has no volume: its count (the reduced count) and levels owed as zero clears, SCENE's pass kept
open (the same bytes; reported once)`; a window not taken (other than off or the class):
`rd: shadows: no volume: a window not taken in frame F (reset at command N): <why> (...)`, the first
8 then every 100th. The 10 s `window: per replay` line: `; no volume: K windows a replay without a
volume kept SCENE's pass open (O with it open, up to M MB not stored and loaded again), V with
volumes, R not taken (<why> n)` (M: the open pass's colour and depth and stencil at 4 + 5 bytes a
texel each way, a bound). `rd_replay_tool --stats`: `stats: no volume on|off: K kept (O with the
pass open over WxH, depth yes|no; C zero clears, L level texels; D draws not drawn), V with
volumes, R not taken (...)`.

**The tests.** `rd_no_volume` (`port/render/test/rd_no_volume_test.c`, the scan, the gate and the
lines on the CPU) and `rd_no_volume_ab` / `_gl` (`tools/no_volume_dumps.txt`)
([TESTING_PROGRAMS_RENDER.md](TESTING_PROGRAMS_RENDER.md)).

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
edge pixel's bit can differ from what each sample drew (`rd_date_cut.c` `rd__date_snapshot`, `rd_core.c`
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
with SCENE's colour copied into its snapshot texture (`rd__replay_take_snap`; swapped with it where the start-up self-test passed, [below](#the-crack-fills-snapshot-by-the-swap)), and one triangle is
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
  After them the fill waits for nothing else (the mask that reads SCENE follows), unless the late
  pass holds its draw ([below](#the-late-passes-on-scene)).
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

### The crack fill's snapshot by the swap

`rd_crack_fill.c` (FILLSWAP): where the fill takes its snapshot it does not copy SCENE's colour
into the snapshot texture: `fillTakeSwap` calls `rd__replay_blur_swap` (`rd_replay.c`
`blurSwapTarget`, the staticBlur sprites' swap), so the old colour texture is the snapshot (the
picture before the fill, all of it, as the copy was) and the snapshot texture SCENE's colour. The
fill's pass (its own, or the aura's paste pass when the fill is that pass's first draw) then begins
SCENE's colour not loaded (`RHI_LOAD_DONT_CARE`), and the fill writes every texel: `crack_fill_ps`
and `crack_fill1_ps` with `DrawCB.g_mode.x` bit 0 (COPY) give a texel that is no crack its own
snapshot texel instead of discarding it, and with bit 1 (KEEP_A, a fill after the glow's alpha,
which writes RGB alone) every texel's alpha its own snapshot texel's; the pipeline is the fill's
with the colour mask whole (`rd__crack_fill_key(RD_CRACK_KEY_FILL)` or `FILL1`). The detector, the
reach, the snapshot's moment, the colour each crack takes and SCENE's depth are unchanged; a texel
that is no crack is its own value read with `Load` and stored into the RGBA8_UNORM attachment. That
round trip is exact only where the device keeps a UNORM8 value through the float and the store, so
the swap is on only where the start-up self-test (`rd__crack_swap_selftest`, run with the other
self-tests from `rd_fog_selftest`, and by `rd_replay_tool` at its start) passes: a 256 x 2 source of
every level in every channel (each row a permutation of 0-255 a channel), made with SCENE's usage,
drawn through both fill shaders' no-crack path (the dummy texture as the depth: no texel a crack)
with COPY, one with KEEP_A, into targets of the same usage, read back and compared texel for texel;
any difference or a part that cannot run leaves the copy (`rd: crack fill: swap: off (self-test
failed at texel N: ... for ...)` or `(self-test not run: <why>)`; passed: `rd: crack fill: swap: on
(self-test: 256 levels a channel through both fill shaders read back unchanged) ...`).

The snapshot texture has SCENE's usage (`snapEnsure` makes it as `rd__target_create_gpu` makes
SCENE's colour: the destination-read and input-attachment usage too); `rd_reshow.c` compares SCENE's
two textures as a pair (the smaller id in `Snap.tex[3]`, the larger in `sceneOther`), so a swap is
not a recreation; the fill's pass stays whole (it has no pass area; `pasteWhyNot` and `glowWhyNot`
refuse one). Refused, the snapshot copied, and counted by reason: a draw the late pass holds past
the snapshot (SCENE's colour would be the stale texture while the border repeat and the mask read
it), the upscaler's split (the display layer reads SCENE's snapshot), an
MSAA epoch wanted, the exact blend, the whole-mask pipeline missing, `ICO_RD_CRACK_SWAP=0` (the
tests only, [TESTING.md](TESTING.md); no config key). What it saves where it swaps: the colour copy
(about 9.7 MB read and as many written at 2364 x 1080) and the fill pass's colour load.

The general shader's early leave (CRACKTAP): at a reach of at most 2 on both axes `crack_fill_ps`
is drawn with `g_mode.x` bit 2 (EARLY): a texel with neither immediate neighbour in front of it
(`crack_front`) on an axis whose reach is at least 1 is no crack at once, without the run's other
taps. The verdict is the same: a run of at most 2 texels with an end at each side has one end at
distance 1 (l + r - 1 <= 2 with l, r >= 1), so a texel with no neighbour in front on an axis has no
run on it. The reach-1 shader (`crack_fill1_ps`) reads its four neighbours first already and is not
changed. `ICO_RD_CRACK_TAP2=0` turns it off (the tests only).

Counters: `RdPerfRecord.crackSwaps`, `crackSwapTexels` (SCENE's texels not copied and as many not
loaded), `crackSwapCopied`, `crackSwapWhy`, `crackTap2`. The lines: once a run `rd: crack fill:
SCENE's colour swapped with its snapshot texture where the fill takes its snapshot ...`; the 10 s
crack fill line's `; N snapshots by the swap, M copied (K <why>)`; `--stats`: `<dump>: stats: crack
fill swap on|off (self-test passed|failed|not run): N snapshots by the swap (X MB of colour not
copied and as many not loaded), M copied (last: <why>); early leave on|off: T draws`. `rd_crack_test`
(every `rd_crack_*` mode where the filler draws: `checkWork`), `rd_crack_work_ab` and
`rd_crack_work_gl` (dumps) check it ([TESTING_PROGRAMS_RENDER.md](TESTING_PROGRAMS_RENDER.md)).

### The late passes on SCENE

`port/render/rd_late_pass.c` (`rd_late_pass.h`): fewer full-size passes on SCENE after list 7. The
fill's hold and the deferred glow paste give the same bytes; list 11's join is an accepted "looks the
same" divergence ([DIVERGENCES.md](DIVERGENCES.md) VD-31, below). Every part is on by default on a
tile-based GPU (`RdProfile.tiler`) under Vulkan (MoltenVK too; `RD_LATE_DEFAULT`), where a pass loads
and stores the whole picture (10 MB each way at 2364 x 1080); on the other classes and backends the
join alone, which applies only where the fx plan reads destinations by the local read (the hold and
the deferral save a load and a store an immediate-mode GPU hardly pays for; the join's picture is then
the same on every device it can apply to). `ICO_RD_LATE_PASS=0|1|all|fill|defer|join` (the tests
only, [TESTING.md](TESTING.md); no config key) switches it off, every part (`1`, `all`) or one part
on anywhere.

**The crack fill's draw held** (`fill`). In a glow room the fill is placed after `cleanUpFB` at the
first sprite into another target (the glow's `WORK2_PAD` band), because the glow's mask, which samples
SCENE, follows; so it has a pass of its own (the PowerVR phone's cage rooms at Resolution Auto: 0.9
passes and 1.8 ms a picture, "after the glow's alpha" in its 10 s line). Under the glow's DARK plan
([The glow where it adds light](#the-glow-where-it-adds-light)) the mask and the paste are not
drawn, and nothing until the aura's paste draws on SCENE or reads its RGB; under LAMPS and WHOLE
the glow's paste (WORK3 added over SCENE by the fx plan's premultiplied add) is the next draw on
SCENE, after the lamps' meshes into `WORK2`. There `rd__crack_fill_place` says TAKE: the fill's snapshot is taken where it was (the same bytes read, the
detector, reach and arithmetic unchanged) and its draw waits, each command classed by
`rd__late_pass_class`, until it is the first draw of the aura's paste pass (`rd__crack_fill_in_pass`,
as in a room without the glow) or, where the plan draws the glow's paste (LAMPS, WHOLE), of that
paste's pass (`RD_LATE_LAND_GLOW`: `rd_fx_plan.c` `fxDraw`'s premultiplied add begins it with the
fill; `glowWhyNot` refuses it without the fx plan's add, under the exact blend or an MSAA epoch, with
a pass on SCENE open or a pass area). It waits past: state (not the upscale point), NOPs, overlay
text, a clear or a copy of another target, a staticBlur sprite into another target that writes no
depth of SCENE, takes no DATE and samples SCENE only in the alpha-only form (`rd__fx_alpha_only`:
MODULATE by RGBAQ's RGB 0 without a blend, the glow's mask: the alpha, which the fill, RGB alone
after the glow's alpha, does not write), a draw (screen prims, a VU draw) into another target that
samples no SCENE, writes no depth of SCENE and takes no DATE or PRIM.AA1 (the lamps' meshes into
`WORK2` under a Z test of SCENE's depth, which the fill reads after them: it takes the depth back to
its sampled state outside any pass), and a sprite on SCENE the light plan does not draw
(`rd__glow_light_skips`: the DARK plan's paste). The sun's fans write SCENE's depth: a fill held to
them is drawn before them (a WHOLE frame with the fans on the picture gains nothing). Anything else has the held fill drawn before it in
a pass of its own (the cost without the hold); so do SCENE's snapshot taken again or swapped
(`rd_replay.c` `takeSnap`, `blurSwapTarget`), an MSAA epoch, an aura paste whose pass cannot take
it, and the walk's end. Not held under the upscaler's split (the upscale point, the display layer),
with an MSAA epoch wanted, under the exact blend, or under the tool's pass window, nor where no
paste pass on the device can take the fill (`landPossible`: the aura's needs the fx plan's local
read, the glow's the fx plan's premultiplied add and a plan that draws it; on OpenGL the aura's
paste reads a snapshot, so in a DARK room the fill is drawn where it was due, as without the late
pass, not held to be drawn in a pass of its own later; once a run `rd: late pass: the crack fill
not held on this device: ...`). A held fill drawn past SCENE's border repeat draws the middle and
each repeated block (below): `RdPerfRecord.latePassBlockDraws` counts the draws more than one.
Nor is it held in a replay whose scan (`rd__late_pass_scan_end`, below) finds that the place it
would be held to is no paste (a draw it must go before, the walk's end: the opening cutscene has no
aura, and its lamps write SCENE's depth, so every held fill there was drawn early in its own pass
and kept its snapshot's copy, which FILLSWAP refuses for a held fill): the walk then draws it where
it is due (`rd__late_pass_no_land`; counted `RdPerfRecord.latePassNoLand`, the 10 s line's `; N
fills not held (no paste in the frame takes them)`, once a run `rd: late pass: the crack fill not
held in a frame where no paste takes it ...`). A held fill drawn early ends the open pass with
`endPassFor`: a clear pending on another target stays pending for that target's next pass to take
as its load op (the commands it was held past touch none of SCENE's textures), where `endPass` had
recorded it as a pass of its own in the crack fill's class (the opening's lamps rooms: 0.6 passes
and 6 MB cleared a picture).

One write of SCENE falls between the snapshot and the draw: SCENE's border repeat
([RENDER.md](RENDER.md), `rd_blur_border.c` `sceneRepeat`, before the first sprite that samples
SCENE: the mask, whose draw the plan skips and whose repeat it keeps), which copies GS columns 2 and
W - 3 over the outer two a side. Without the hold it copied the filled columns, and the fill had read
the outer columns, as the fog and lists 4-6 left them, as neighbours. With it the fill reads the same
snapshot, taken before the repeat; its draw is scissored to the columns the repeat left, and each
block the repeat copied (`rd__scene_border_blocks`: up to 16, more and the held fill is drawn before
the repeat) is drawn again with the test made at the block's source texel (`crack_fill_ps`'s
`DrawCB.g_param.w`, a whole number of columns; 0 for every other draw): a crack there writes its
source's fill, any other texel keeps the copy. The alpha is the copy's either way (the fill writes RGB
alone there).

The store plan's scan (`rd_store_plan.c`) asks `rd__late_pass_scan` where the fill reads SCENE's
depth; the glow's plan is decided at the scan's end, so `rd__late_pass_scan_end` then gives the
place the walk takes (the landing past the skipped paste under DARK, the first command a plan that
skips nothing on SCENE does not wait past under LAMPS and WHOLE, the snapshot's place under any
other plan) and the depth's touch (`PlanDepth.late`) lands there, every pass before it keeping the
depth's store: under LAMPS the lamps' pass keeps the depth store the last store's cut took before
(up to 5 bytes a texel stored, against the fill pass's 8 of colour loaded and stored).

What it saves: the fill's own pass, SCENE's colour loaded and stored, in each DARK room picture with
a crack fill and the aura (about 1.2 to 1.5 ms a picture estimated for the phone's cage rooms;
UNVERIFIED), and in each LAMPS picture less the lamps' depth store. Counters
`RdPerfRecord.latePassHeld` (`latePassGlowHeld` of them in the glow's paste pass), `latePassTexels`,
`latePassFlushed`, `latePassWhy` (`RD_LATE_WHY_*`). The lines:

- `rd: late pass: on (a tile-based GPU's default): the crack fill's draw held from its snapshot to
  the aura's paste pass in a frame whose glow draws nothing, to the glow's paste pass in one that
  draws the paste; the glow's paste in the aura's paste pass; list 11's draws that use no depth in
  the open pass without it (the same bytes but the join's: a looks-the-same divergence,
  DIVERGENCES.md VD-31)`, `rd: late pass: join (not a tile-based GPU: list 11's join alone, where it
  applies): ...`, or `rd: late pass: off (<why>)`, at the start and
  when it changes;
- `rd: late pass: the crack fill's draw held from its snapshot to the aura's paste pass (the glow
  drew nothing between), not in a pass of its own (reported once)`, and the same `to the glow's
  paste pass (the lamps' or the sun's draws into WORK2 between)`;
- the 10 s `window: late pass: N crack fills held into the aura's paste pass, G into the glow's, M
  drawn early in a pass of their own (K <why>, ...); D glow pastes in the aura's paste pass, E drawn
  early; J list 11 draws joined the open pass without SCENE's depth (P passes not begun)` (when any) and the crack fill's own `..., B held past SCENE's
  border repeat (its columns filled from their source columns)`;
- `--stats`: `<dump>: stats: late pass on|off|fill|join: N crack fills held into a paste pass (G
  the glow's; X MB of colour not loaded and as many not stored), M drawn early in a pass of their
  own (<why>); D glow pastes in the aura's paste pass (Y MB ...), E drawn early; J list 11 draws
  joined ...; K draws more for the border's blocks; L fills not held (no paste in the frame takes
  them)`.

**The glow's paste in the aura's pass** (`defer`). Under LAMPS and WHOLE the glow's paste begins a
pass on SCENE of its own, and the aura's paste another after the aura's sprites into its own buffers.
Where the device has the local read (the aura's paste is drawn in its pass by `fx_local_ps`), the
glow's paste is not drawn where it comes: `rd_fx_plan.c` `fxDraw` copies its arguments and inputs
(`fxDeferTake`, its source WORK3 made readable first) and `rd__fx_defer_draw` draws it in the aura's
paste pass, after a held crack fill and before the aura's paste, under its own state and with the
command order set back to its own (`g_rdStoreOrd`: the pass area and the store plan read a pass it
begins as begun at the paste; `rd_pass_area.c` names the exception). The frame's order of writes to
SCENE is kept (fill, glow's paste, aura's paste; the aura's local read sees the glow's writes after
its barrier), so the same bytes. While deferred it waits past what the fill's class waits past less
draws (their passes may bind SCENE's depth, which a held fill reads after them), sprites with a Z test
of SCENE's depth, any read of SCENE (the paste wrote its RGB and alpha) and anything on its source; any
of those, SCENE's snapshot taken or swapped, an aura's pass that is not the local read's, has an area
or follows an open pass on SCENE, and the walk's end draw it before them (a held fill first; counted
as drawn early). A held fill armed for the glow's paste waits on to the aura's paste with it
(`rd__crack_fill_unarm_glow`). Counted: `RdPerfRecord.latePassDeferred`, `latePassDeferTexels`,
`latePassDeferEarly`; once a run `rd: late pass: the glow's paste deferred to the aura's paste pass
...`; the 10 s line's `; D glow pastes in the aura's paste pass, E drawn early`; the `--stats` line's
`; D glow pastes in the aura's paste pass (X MB ...), E drawn early`. The glow's GPU time is then
counted in the aura's class.

**List 11's draws in the open pass** (`join`; [DIVERGENCES.md](DIVERGENCES.md) VD-31, accepted
2026-11-11). Not byte for byte: on lavapipe the joined replay's SCENE differs from the late pass off
on a few hundred texels inside list 11's text sprites (bilinear, blended, the sheet font's shaders):
issue 164's paused frame 235 texels by 1 level at the phone's setting and 92 at Enhanced 2x 4:3, issue
102's frame 537 by up to 2 at the phone's setting (DISPLAY 515 by up to 3, PRESENT 478 by up to 2);
OpenGL the same kind; `LP_PERF=no_linear,no_rast_linear` gives the same texels. The joined draws'
keys differ from their own pass's only in the depth format and the Z state (`rd_pipeline.c`
`rd__plan_screen_draw_ex`): the driver's sampling of the same shader in a pipeline without the depth
attachment lands those texels apart (the DATE cut found the same on lavapipe). List 11's screen prims bind SCENE's depth wherever
SCENE has one (`rd_replay.c` `prepareDrawEx`), so after the aura's paste, whose pass has no depth
(the local read), the first of them began a pass of its own with the depth, SCENE's colour loaded
and stored again: in a paused picture (no feedback read of SCENE between, `staticBlur.c`) the pause
menu's draws, on the phone 2.8 ms of a picture in a block 70 % paused. A list 11 screen prim whose
draw uses no depth (its Z test off or ALWAYS, its Z write off, no DATE, no PRIM.AA1, no depth view,
no shadow window on the depth, the same Z scale without it) takes no depth target
(`rd__late_pass_join`, after the DATE cut's depth: `tdId` 0, `depthFmt` UNKNOWN) when the pass open on
its target has none (no MSAA, no exact blend, no GS state as shader data) and the fx plan reads
destinations by the local read (the profile the join's A/B ran on byte for byte; on OpenGL the
joined passes' pictures differed in issue 102's and 164's frames, cause not found, so it is not
joined there: once a run `rd: late pass: list 11's draws not joined ...`), so it is drawn in that
pass: the mirror of `fxAttachDepth` (a sprite without a Z test drawn in a pass with the depth it
does not use). The colour bytes are the same: the key with the depth had Z ALWAYS and no write,
the key without it is given the same (`rd_pipeline.c` `rd__plan_screen_draw_ex`); the depth is not
read or written either way. Its keys are the screen family's no-depth twins, in the start-up set
beside each key with the depth (`rd__enumerate_reachable_screen`): no pipeline is made during play.
A draw after it that needs the depth begins its pass with the depth (`bindDrawEx`), as before. An
unpaused picture's list 11 cannot join: the aura's feedback read of SCENE ends the paste's pass.
Counted: `RdPerfRecord.latePassJoined` (the draws), `latePassJoinPasses` (the passes joined, each one
the pass the first such draw began before) and `latePassJoinTexels`; once a run `rd: late pass:
list 11's draws that use no depth drawn in the open pass on SCENE without its depth ...`; the 10 s
line's `; J list 11 draws joined the open pass without SCENE's depth (P passes not begun)`; the
`--stats` line's `; J list 11 draws joined the open pass without SCENE's depth (P passes not begun,
X MB of colour at most each way)`.

`rd_late_pass` (the predicate and the scan, CPU), `rd_crack_held` and `rd_crack_heldglow` (a crack
in the repeat's source column, held to the aura's or the glow's paste against the late pass off),
`rd_crack_defer` (the glow's paste deferred to the aura's paste pass), `rd_crack_join` (two list 11
commands after the aura's paste), `rd_late_pass_ab` and `rd_late_pass_gl` (dumps) check it
([TESTING_PROGRAMS_RENDER.md](TESTING_PROGRAMS_RENDER.md)).

`rd_crack_test` (`rd_crack`, `rd_crack_4x`, `rd_crack_frac`, `rd_crack_backdrop`,
`rd_crack_backdrop_4x`, `rd_crack_backdrop_frac`, `rd_crack_1x`, `rd_crack_original`,
`rd_crack_msaa`, `rd_crack_off`, `rd_crack_depth_copy`, `rd_crack_buffer`, `rd_crack_d24s8`,
`rd_crack_nodual`, `rd_crack_tiler`, `rd_crack_tiler_on`, `rd_crack_tiler_fold`,
`rd_crack_tiler_fold_mali`), `rd_replay_tool_crack` and `rd_crack_late_ab` check it
([TESTING_PROGRAMS.md](TESTING_PROGRAMS.md)).

### The crack fill in list 11's pass

`port/render/rd_fill11.c` (`rd_fill11.h`; the held draw in `rd_crack_fill.c`): on the title's
pictures without glow (the light plan's DARK) and without an aura, the crack fill had a full-size pass
of its own on SCENE (SCENE's colour alone, not loaded by the swap, stored: 9.75 MB at 2366 x 1080)
and list 11 another (SCENE's colour loaded and stored, SCENE's depth attached for draws under Z ALWAYS
without a Z write). Where nothing that runs on the GPU reads or writes SCENE's colour, its depth or
its snapshot between the fill's place and list 11's first screen draw on SCENE, the fill's snapshot
(by the swap where allowed) and its count are taken at its place as before and its draw is held to
that draw, which begins one pass on SCENE's colour with a **scratch depth** attached in place of
SCENE's ([WORK_BUFFERS.md](WORK_BUFFERS.md#the-crack-fills-scratch-depth)): the fill is its first draw,
list 11's draws follow with their own pipelines. The fill's own store and list 11's load go: 19.5 MB a
DARK title picture at 2366 x 1080. This is not list 11's join (VD-31): list 11 keeps drawing with a
depth attachment and the pipelines it has today.

Why the bytes are the same: the fill reads the snapshot and SCENE's depth it read at its place
(nothing between touched them) and writes the same texels; its pipeline is its own with a D32F_S8
depth attachment, Z ALWAYS and no Z write (`rd__fill11_twin`: no test can fail, nothing is written to
the depth, the shader does not read it), checked on the device by a start-up self-test; list 11's
draws keep their pipelines (the scratch has SCENE's depth format, size and one sample) and their
FrameCB (their depth target stays SCENE's for the Z scale), and each one neither tests nor writes the
depth (Z test off or ALWAYS, no Z write, no DATE by the stencil, the snapshot or the shader's read, no
PRIM.AA1, no shadow window live on SCENE's depth, not the COLCLAMP 0 wrap path), so its colour does
not depend on what the attached depth holds. SCENE's depth is not attached to the merged pass: it keeps
what its last pass stored (the store plan keeps that store for the fill's read at its place; no pass
between binds it); the fill samples it as in its own pass (`rhi.h` forbids sampling a depth bound
read-only, hence the scratch). The pass is whole, single-sampled, not a storage pass, its colour
readable as `beginPass` would begin list 11's.

The walk (`rd__fill11_take`, `rd__fill11_at`, `rd__fill11_draw`):

- **The take.** Where `rd__crack_fill_place` draws the fill in a pass of its own before command c of
  lists 7 to 11 (the late pass holds nothing there: no paste in the frame takes it), the draw is held
  when the switch is on (below), no MSAA epoch is wanted, the fixed-function blend draws (no exact
  blend, no GS state as shader data), the upscaler's split is off, no pass window and no deferred glow
  paste, c itself is waited past or is the landing, the scratch can be made and the fill's depth twin
  exists. Else the fill is drawn where it is due, as before (counted, `RD_FILL11_NOT_*`).
- **The wait.** Each later command (`classOf`): waited past are state (not the upscale point), NOPs,
  overlay text, a screen draw the replay does not draw (deferred text, a fan the light plan skips, a
  sprite the fades fold applies at the reduction), a sprite the light plan leaves undrawn
  (`rd__glow_light_undrawn`: skipped, or a lazy clear of its target), and what `rd__late_pass_class`
  waits past that also binds no SCENE depth at all (a pass that binds it may drop its store) and
  samples no SCENE in any form (the swap left SCENE's colour texture undefined until the fill: the
  glow's mask in its alpha-only form is not waited past here, unlike the late pass's hold). SCENE's
  border repeat (`rd_blur_border.c` `sceneRepeat`, at the first sprite that samples SCENE, drawn or
  not) draws the held fill first (`rd__fill11_border`): on a frame of the PS2's 512 GS pixels with the
  border open (Full pixel at its exact width) the title's mask brings it; the PowerVR phone's own
  title is 848 GS pixels wide and cropped, whose repeat copies nothing, and a 4:3 frame with the
  border closed repeats nothing.
- **The landing.** List 11's first screen draw on SCENE with SCENE's depth bound whose state
  qualifies, where `rd_pass_area.c` would not give list 11's pass an area, and where the look ahead
  (`laterOk`) finds every later command up to the one that ends list 11's pass anyway (a draw, sprite
  or copy on another target, the reduction, the walk's end) to be state, a NOP, overlay text, an
  undrawn screen draw or a qualifying screen draw of list 11 on SCENE (a clear, any other draw on
  SCENE, any other command: today's passes kept). In `prepareDrawEx`, after the draw's texture is
  resolved, its draw qualifying at its own level too (no DATE through the stencil or the shader, no
  shadow tile), the open pass ends (its pending clears recorded as `beginPass` records them), the
  merged pass begins (`r->passDepth` SCENE's id, so `bindDrawEx` keeps list 11's draws in it) and the
  fill is drawn first. The merged pass holds state, NOPs, overlay text and list 11's screen draws on
  SCENE; before any other command it ends, which the look ahead makes the command that ends list 11's
  pass today; a draw in it on SCENE with its depth that does not qualify ends it first (counted;
  never in the dumps' forms).
- **Drawn in its own pass after all** (`rd_crack_fill.c` `rd__crack_fill11_own`, why
  `RD_FILL11_WHY_*`): before the first command not waited past, before SCENE's border repeat, before
  SCENE's snapshot taken again, at a landing refused (the pass area, a later draw that uses SCENE's
  depth, the draw itself) and at the walk's end; the pass the fill would have had (`endPassFor`: a
  clear pending on another target keeps its carry), the same bytes as at its place.

The switch: **off by default in this release** (`RD_FILL11_DEFAULT 0`): on the owner's PowerVR phone
the right part of the title's picture flickered black from the first picture the fill was drawn in
list 11's pass, and the cause was not found by reading the code (the start line says so; nothing of
it runs, not the self-test nor the start-up set's twins, without `ICO_RD_FILL11`). With the default
on it would be on where the start-up self-test passed and the device's class is the PowerVR phone's or a
desktop one (`rd__profile_tile_class` 1, or 2 on a GPU that is not a tile-based phone's,
`RhiLimits.tilePhone`: Vulkan, OpenGL, Direct3D 11 and 12 on a PC); off on the Arm, Qualcomm and
Apple classes and other tile-based phones in this release (issues 123, 179, 211: no new kind of GPU
work there). `ICO_RD_FILL11=0|1` (the tests only, [TESTING.md](TESTING.md); no config key) keeps it
off or tries it on any class (the self-test still decides); `rd__fill11_force` is the tests'. On
OpenGL it would be on with the default where the self-test passes (llvmpipe: `rd_fill11_gl`), as on
the other desktop classes; the saving there is the passes' work, not tile memory.

The self-test (`rd__fill11_selftest`, from `rd__crack_swap_selftest`, once a renderer from
`rd_fog_selftest`; where the class or `ICO_RD_FILL11` lets it be tried): a 256 x 2 source of every
level in every channel (the swap's) drawn through each of the four depth twins in a pass with a
scratch D32F_S8 depth attached, loaded and stored DONT_CARE: the general and the reach-1 fill with
COPY (every texel written: the source read back), and each with RGB alone without COPY over a target
loaded with another pattern (every texel discarded: that pattern read back); compared texel for
texel. The dummy texture is uploaded on its own command list first (as the swap's self-test). The
pipeline keys it adds to the start-up set (`rd_pipeline.c` `rd__enumerate_reachable_fog` through
`rd__fill11_enumerate`, where it may be tried): the depth twins of the four fill keys, `FILL`,
`FILL | RGB`, `FILL1`, `FILL1 | RGB` (four; two without the reach-1 shader). No shader changes.

The tests run it with `ICO_RD_FILL11_POISON=1` ([TESTING.md](TESTING.md)): lavapipe keeps a
DONT_CARE load's old bytes where a tile-based GPU leaves them undefined (black or garbage), so a
texel the fill does not write, or a draw that depends on the scratch depth's contents, would pass
a byte test there and show on a phone; the poison clears the merged pass's and the held fill's own
pass's DONT_CARE colour to magenta and the scratch to depth 0.5, stencil 0x5A.

State that survives `rd_shutdown` is freed there (`rd__crack_fill_shutdown` calls
`rd__fill11_shutdown`): the scratch depth destroyed and its handle forgotten, the self-test's verdict
and the merged pass's serial reset (the next renderer starts its replay counter again).

The lines: `rd: crack fill: with list 11: on (self-test passed: 4 draws of 512 texels through the
fill's depth twins read back unchanged; the PowerVR class|a desktop class|ICO_RD_FILL11=1): ...` or
`... off (<why>)` (start, a change); once `rd: crack fill: with list 11: the scratch depth made
(WxH, D32F_S8, X MB; never loaded or stored)` and `rd: crack fill: drawn as the first draw of list
11's pass on SCENE over a scratch depth (frame F) ...`; the 10 s `rd: crack fill: with list 11: N
drawn in list 11's pass over the scratch depth (X MB of colour not loaded and as many not stored), K
held and drawn in a pass of their own after all (<why counts>), J kept in their own pass (<why
counts>); S merged passes ended by a draw that uses SCENE's depth` (after the crack fill's own 10 s
line, when any); `--stats`: `<dump>: stats: crack fill with list 11 on|off (self-test
passed|failed|not run): N drawn in list 11's pass (X MB of colour not loaded and as many not
stored), K drawn in a pass of their own (<why counts>), J kept in their own pass (<why counts>); S
merged passes ended by a draw that uses SCENE's depth`. The merged pass's GPU time is list 11's
class (the crack fill's effect time falls by the fill's pass). `rd_crack_fill11`,
`rd_crack_fill11_later`, `rd_crack_fill11_tiler`, `rd_crack_fill11_area` (`rd_crack_test`),
`rd_fill11_ab` and `rd_fill11_gl` (dumps) check it
([TESTING_PROGRAMS_RENDER.md](TESTING_PROGRAMS_RENDER.md)). No DIVERGENCES.md row: the same bytes.

### The aura's paste as its constant part

`port/render/rd_aura_paste.c` (`rd_aura_paste.h`): in a room with no aura object on screen AURA_WORK
holds only its clear, and the aura's paste into SCENE (`pasteFeedBackAreaToFB`, list 8 command 55
in the phone's frames: FEED128 over SCENE through (Cs - Cd) * As + Cd, MODULATE by RGBAQ (64, 64, 64,
128)) changes SCENE's RGB nowhere but its bottom band: As is 0 wherever its kernel reads FEED128's
rows 0-111, which hold alpha 0, and FEED128's rows 112-127 hold WORK0's alpha the aura's sprite left
(measured at 128; the paste's B-spline reaches row 112 near SCENE's last rows). The paste is drawn
there as the alpha alone (`rd_fx_plan.c` `fxAuraSplit`: the constant fold of the sprite untextured
with RGBAQ's alpha 0, the blend proved the identity on RGB at every Cd and Ad, the alpha 0 written:
the bytes the paste writes where As is 0, without its texture's taps or the destination's read) and
as the paste over the other rows, scissored, both in the paste's own local-read pass (after a held
crack fill and a deferred glow paste, which the band's first draw takes as the whole paste's draw
did). No new pipeline: the constant fold's key (the alpha alone, `rd__fx_enumerate`) and the paste's.

The proof is a chain of records, each a target's rows (across every column) proven alpha 0:
- started by a sprite drawn in a cleared source's colour (`rd_const_read.c` `rd__const_took`: command
  39, AURA_WORK into WORK0) whose colour's alpha is 0: the rows it covers (fx_sprite_ps's coverage at
  scale 1, in the scissor, every column);
- carried by the next action's sprite reading that target at scale 1 (command 47, WORK0 into FEED128:
  V = 16 y to the bit, the GS's bilinear taps, a fraction of 0 weighing the second tap 0, addressed as
  `fx_axis` addresses them): the rows whose taps all lie in proven rows;
- taken by the next action's sprite into SCENE reading it (the paste): each SCENE texel row's window
  Y a step either way (the float's rounding of (t + 0.5) / scale; a step lower again above the rect),
  its V, the kernel's taps (`rd__ap_taps`: the box, the B-spline or the GS's taps of FXF_ABOVE1X's
  steps), addressed (CLAMP to the TEX0 rows then the texture's; a row REPEAT moves proven only when
  every row is); the longest run of rows reading proven rows only is drawn as the alpha alone.
Each link needs its alpha written whole (no alpha test, no DATE, FBA 0, the colour mask's A, no
FBMSK), its alpha 0 at an alpha 0 texel (`rd__const_tfx`), RGBA32; the paste besides RGB kept at As 0
(`rd__fx_fold`: PABE refuses), the scissor whole and the local read (the profile's fx plan and
fxDstLocal, no exact blend, no MSAA, no GS state as shader data). Any action between two links (the
action count of `rd_const_read.c`) ends the chain. Anything not proven: the paste whole, counted by
reason (`RD_AP_WHY_*`). On the Pixel's frames (both WRAP on CLAMP): FEED128 rows 0-111, SCENE rows
[0, 1062) of 1080 as the alpha alone, 18 band rows (`rd_aura_paste_test`'s geometry; the cage frame's
measured band, rows 1072-1079, lies in it). `ICO_RD_AURA_PASTE=0` draws the paste whole (the tests;
no config key).

Counters `RdPerfRecord.auraPasteSplit`, `auraPasteRows`, `auraPasteBand`, `auraPasteKept`,
`auraPasteWhy`; once a run `rd: aura paste: the aura's paste drawn as the alpha alone over SCENE's
rows A-B (...), as the paste over the other N rows`; the 10 s line `window: aura paste: P drawn as
the alpha alone (B band rows each), K read whole (why)` (when any aura paste was seen); `--stats`
`stats: aura paste on|off: S split (R rows as the alpha alone, B band rows), K drawn whole (why)`.
`rd_aura_paste` (CPU) and `rd_aura_paste_ab` (dumps) check it
([TESTING_PROGRAMS_RENDER.md](TESTING_PROGRAMS_RENDER.md)).

## Start-up

What the renderer decides or prepares before play, and the lines that show it, beside
[RENDER.md](RENDER.md#made-at-start-up)'s allocations.

**One log line, one write.** The log streams are unbuffered (Android: fds 1 and 2 are a pipe the
log thread drains; Linux and macOS: each write goes to the file), so a line written in three calls
(its `rd: `, its text, its newline) could have another thread's line land inside it.
`rd__log` and `rd__log_once` format the whole line into one buffer (`rd_log_line.c`: on the stack
up to 2 KB, else on the heap) and hand it to the stream in one `fwrite`, which an unbuffered stream
passes on as one write; only where the heap refuses a long line are the three calls made.
`rd_log_line` checks the bytes and the one write (glibc's `fopencookie`).

**What play makes outside the key cache, named.** The slow-replay line counts a replay's
creations (`N pipelines created, A allocations, B buffers and T textures created`) and a key-cache
pipeline made on first use has its own line (`rd: pipelines: made on first use in ...`), but the
rest had no name: the Mali of issue 179 made 5 pipelines at replay 1 and the Adreno 710 of issue 211
3 at replay 47 with no first-use line, and one buffer of the Mali's first 3D picture was not the
upload ring by its size. Once the window says play begins (`rd_made_outside_watch`, at the end of
`window_pipe_skin_check`: after the start-up set, its self-tests and the start-up allocations; the
fog self-test, which follows, is watched too), the
RHI's create hook (`rhi_set_create_hook`, called after every `rhi_create_pipeline` and
`rhi_create_buffer` that made one) names each pipeline made one at a time outside the key cache
and each buffer (`rd_made_outside.c`): `rd: made in play outside the key cache in replay R (list
L | the vertex pre-pass | no draw list: the present, a check or the game thread): pipeline "NAME"
(its 1st; pipelines so far P, of N names)`, a buffer's with its size (`buffer "rd ring" of 8192
KB`). The first of each name always, then that name's first 8 and every 100th (32 names kept,
the rest counted on the last); at the end of the run `rd: made in play outside the key cache, at
the end of the run: P pipelines (N names), B buffers (M names): pipeline "NAME" C, ...`. Not here:
the batches (the start-up set, the background finisher, the skin check's background compiles: their
own lines) and textures (the stage's own and the work targets, whose `named targets made` line
says them). No GPU work, no picture change; a line only where something was made in play.
`rd_made_outside` checks the words and the limiter.

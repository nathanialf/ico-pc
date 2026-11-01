# The renderer (rd)

[rd](ARCHITECTURE.md#terms) records what the game draws as [lists](ARCHITECTURE.md#terms) of
commands and [replays](ARCHITECTURE.md#terms) them on the [RHI](ARCHITECTURE.md#terms) with the
[GS](ARCHITECTURE.md#terms)'s rules for state, sampling, blending and depth. Callers:
[GS.md](GS.md); VU shaders: [VU1_PROGRAMS.md](VU1_PROGRAMS.md); sources: [SHADERS.md](SHADERS.md).

## Files

| path | what it holds |
| --- | --- |
| `port/render/rd.h` | the API the game's render layer calls; the model in its header comment |
| `port/render/rd_internal.h` | the command records, the context `g_rd`, what the rd files and tests share |
| `port/render/rd_state.h` | the finite GS state the game uses: blend, test, TEXA, wrap, filters, the pipeline key |
| `port/render/rd_core.c` | recording: the 13 lists, the payload arena, state deltas, targets, the frame ring |
| `port/render/rd_frame.c` | the flip's frame head, the Z scale per target, the VU block, the camera in FrameCB |
| `port/render/rd_tex.h`, `rd_tex.c` | the texture cache: GS formats decoded into rd textures |
| `port/render/rd_mesh.h`, `rd_mesh.c` | the VU mesh path (VU1_PROGRAMS.md) |
| `port/render/rd_replay.c` | a frame onto the RHI: every command's replay |
| `port/render/rd_replay_vu.c`, `rd_replay.h` | the replay's VU draws and the vertex and triangle pre-passes; what they share with `rd_replay.c` (the replay's state, a draw's setup, its helpers) |
| `port/render/rd_vuprepass_selftest.c` | the vertex and triangle pre-passes' start-up self-test |
| `port/render/rd_upload.c` | the upload ring and what a replay uploads before its first pass |
| `port/render/rd_pipeline.c` | pipeline keys from the state block, the pipeline cache, the reachable set |
| `port/render/rd_dst_read.c` | the exact GS blend read from the destination in the fragment shader |
| `port/render/rd_post.c` | `rd_post`: each post kind as the GS writes and sprites GsBase.c issues |
| `port/render/rd_blur.c` | staticBlur.c's sprites and the reduction's, for `rd__blur_post_sprite`; the open border's strips |
| `port/render/rd_blur_border.c` | the post sprites' replay (`rd__blur_post_sprite`): the fold, the reduction's open border and its outer columns, SCENE's outer columns repeated after the fog |
| `port/render/rd_blur_smooth.c` | Motion blur: Smooth: the motion blur's accumulator, its two passes and the sprite drawn over it (`rd__blur_smooth_sprite`) |
| `port/render/rd_crack_fill.c` | the crack filler above 1x: the pixels no draw covered (or a farther surface seen through a seam) between drawn ones filled before the fog, and their count ([Crack filling above 1x](#crack-filling-above-1x)) |
| `port/render/rd_shadow.c` | the shadow count on the stencil |
| `port/render/rd_water.c` | render-to-texture blocks, target aliases, camera scopes, pool STs |
| `port/render/rd_fog_path.c` | how the fog reads the scene depth; the fog probe and self-test |
| `port/render/rd_fades_fold.c` | the fades fold: list 11's last full-frame lerp sprites applied by the reduction ([The fades fold](#the-fades-fold)) |
| `port/render/rd_content_hide.c` | the content table: stage faces the native frame must not show |
| `port/render/rd_content_extend.c` | the content table's sheets: stage overlays continued to the frame's edges |
| `port/render/rd_content_chain.c`, `rd_chain.h` | the chains drawn at their links' size |
| `port/render/rd_join.c` | the region rule's card test joined over a part's packets |
| `port/render/rd_screen_planes.c` | the attribute planes of screen-prim triangles |
| `port/render/rd_camera_blend.h` | the blended camera, shared with the game's cull |
| `port/render/rd_perf.c` | the per-replay performance records |
| `port/render/rd_occlusion.c` | every GPU draw of a replay (`rd__replay_draw`, `rd__replay_draw_indexed`: the pass window's take) and the hidden-draw measurement around them ([Culling](#culling)) |
| `port/render/rd_passring.h`, `rd_passring.c` | the pass ring: the last passes recorded, for the device-loss report |
| `port/render/rd_dump.c`, `rd_png.c` | frame dumps; a minimal PNG writer |
| `port/render/tools/rd_replay_tool.c` | [the replay tool](ARCHITECTURE.md#terms) |
| `port/render/vu1_ref/` | the VU1 programs' CPU references ([VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-cpu-oracles)) |
| `port/render/test/` | the renderer's tests ([TESTING.md](TESTING.md#portrendertest)) |
| `port/render/rd_interp.c`, `rd_present.c`, `rd_crt.c`, `rd_video.c` | presentation ([DISPLAY.md](DISPLAY.md)) |
| `port/render/texpack*.c`, `modelpack.c`, `gltf.c`, `xxh3.c` | texture and model packs ([PACKS.md](PACKS.md)) |

## The model

The PS2 game builds 13 priority-ordered [DMA](ARCHITECTURE.md#terms) chain lists per frame and kicks
them in order 0..12 at `dl_Swap`, and the GS is one state machine, so a register written in one list
is still in force when the next starts. rd keeps both facts: a frame is 13 ordered command lists
(`RD_LIST_COUNT`) recorded by the game [fiber](ARCHITECTURE.md#terms) and replayed in order against
one persistent state block. Within a list submission order is draw order; rd never sorts by material
(`port/render/rd.h`).

| list | what draws in it |
| --- | --- |
| 0 | opaque world and characters |
| 1 | semitransparent skinned |
| 2 | semitransparent static, points and lines |
| 3 | shadow volumes, then the shadow composite (inherits state) |
| 4 | specular and reflection passes, puddle and pool reflection, fog |
| 5 | dissolving objects (inherits state) |
| 6 | particles |
| 7 | shine level 1, then depth of field and motion blur |
| 8, 9 | shine levels 2 and 3 |
| 10 | the softening chain |
| 11 | UI, subtitles, film noise, brightness, keep, fade, letterbox |
| 12 | debug font; the reduction at its end (it applies list 11's last full-frame lerp sprites: [The fades fold](#the-fades-fold)) |

(`port/render/rd.h`, "Lists").

**Coordinates.** Call sites pass what they passed to the GS: XYZ in 12.4 fixed point about the
2048, 2048 window centre, Z as the GS value the call site computed, UVs in 12.4 texels or STQ.
rd records them as they are; the replay converts them (`port/render/rd.h`, "Coordinates";
`port/render/rd_replay.c` (`rd__frame_group_ex`)).

**Defaults and leakage.** `rd_begin_frame` records the TEST, ZBUF, FBA and
[TEXA](ARCHITECTURE.md#terms) defaults
`gsb_SetGsDefault` writes, at the heads of every list but 3 and 5, through the ordinary state
calls (`port/render/rd_core.c` (`recordDefaults`)). The values are in
[GS.md](GS.md#the-gs-defaults-per-list).

**Keep frames.** `rd_end_frame(1)` (`fbKeep`: pause and memory card screens) replays lists 11
and 12 only, on top of the retained DISPLAY (`port/render/rd.h` (`rd_end_frame`)).

**Threads.** Every `rd_*` call comes from the game fiber. The game's post passes are appended by the
same code inside the [vsync](ARCHITECTURE.md#terms) before `rd_end_frame`, so there is one hand-off
point. GPU work the game's calls cause (replay, pipeline creation, [present](ARCHITECTURE.md#terms))
runs through `rd_set_host_call`, which the window sets to `ico_sched_call_on_host` so the driver
runs on the host stack in the host FP mode (`port/render/rd.h` (`RdHostCall`);
`port/render/rd_core.c` (`rd__on_host`)).

## Recording

A list is an array of fixed-size `RdCmd` records (40 bytes: a type, seven bytes, the draw's key,
four words, two floats); variable data (vertices, transforms, post parameters) lives in the frame's
payload arena (rd's own buffer, not the EE [arena](ARCHITECTURE.md#terms)), addressed by byte
offset. Every field has a fixed width and there are no pointers, so a frame can be written to a file
as it is and a dump reads the same on 32-bit and 64-bit builds (`port/render/rd_internal.h`
(`RdCmd`, `RdCmdType`)).

| kind | commands |
| --- | --- |
| state deltas | `RDC_TEST`, `RDC_BLEND`, `RDC_ABE`, `RDC_ZWRITE`, `RDC_FBA`, `RDC_PABE`, `RDC_COLCLAMP`, `RDC_TEXA`, `RDC_FILTER`, `RDC_WRAP`, `RDC_TEXTURE`, `RDC_TEXTURE_OFF`, `RDC_UVOFFSET`, `RDC_COLORMASK`, `RDC_TARGET`, `RDC_SCISSOR`, `RDC_ALPHA`, `RDC_SHADE`, `RDC_AA1` |
| actions | `RDC_CLEAR`, `RDC_SCREEN`, `RDC_EXACT_BLEND`, `RDC_COPY` |
| VU draws | `RDC_MESH`, `RDC_SKINNED`, `RDC_GRID`, `RDC_PARTICLES` |
| shadows | `RDC_SHADOW_RESET`, `RDC_SHADOW_STRIP`, `RDC_SHADOW_RESOLVE` |
| post and text | `RDC_POST_STUB` (fog and blur sprites), `RDC_OVERLAY_TEXT` (collected by the present) |
| recorded only | `RDC_WORLD_PRIMS` (tests; no replay model) |
| no-op | `RDC_NOP` (the unused copy of the frame head; `rd_replay_tool --nop`) |

State calls record deltas and change nothing at once (`port/render/rd_core.c`). The callers of
each are listed in `port/render/rd.h` ("state deltas"); the ones that set one GS register alone
(`rd_abe`, `rd_blend_func`, `rd_scissor`, `rd_sampler_tex1`, `rd_sampler_wrap`, `rd_gouraud`,
`rd_aa1`) exist for the register decoder (`port/render/rd.h`).

**The frame ring.** The two most recent closed frames are retained for
[interpolation](ARCHITECTURE.md#terms); the frame being recorded takes a third slot
(`port/render/rd_internal.h` (`RD_FRAME_RING`, 3); `port/render/rd_core.c`).

**`rd_begin_frame`** applies pending settings (a texture pack switched off reverts its replacements;
the model pack and its dump switch; a change of the targets' scales recreates them on the host stack
and drops the interpolation history), halves the [scene scale](ARCHITECTURE.md#terms) when a
scene-sized texture did not fit the GPU since the last frame, opens the slot after the last closed
frame with the persistent state as its start state, and records the list defaults
(`port/render/rd_core.c` (`rd_begin_frame`)).

**`rd_end_frame`** keeps the frame head in the first replayed list (`rd__frame_head_resolve`),
walks the frame's state deltas in replay order from its start state to get its end state (the next
frame's start: state leaks from frame to frame as GS registers do), and with interpolation off
replays and presents the frame once on the host stack. With `ICO_RD_DUMP_EVERY` set it also writes
dumps (below) (`port/render/rd_core.c` (`rd_end_frame`, `rd__walk`)). `rd_discard_frame` drops an
open frame unreplayed, as the PS2 drops a display list it never kicks (`port/render/rd.h`).

**Keys.** `RD_KEY(obj, part, ordinal)` names the same logical draw in consecutive frames so the
presenter can pair them; key 0 snaps (`port/render/rd.h` (`RdKey`)).

**The draw filter.** The model viewer shows one object: `rd_set_draw_filter` records a world draw
only when its key's object is in the set, and `rd_draw_filter_open` learns the set from the
object's own display list. UI and full-screen prims, post passes, state and targets are never
filtered (`port/render/rd.h` ("the draw filter")).

## The state

`port/render/rd_state.h` enumerates the GS state the game reaches, from the sources, and the
pipelines are built from these enumerations, not from the whole register space.

**ALPHA.** `out = ((A - B) * C >> 7) + D`, A, B, D from Cs, Cd, 0 and C from As, Ad, FIX. The
twelve modes of `gif_SetAlpha` (`alphaTable`) are `RdBlend`; modes 3 and 8 to 11 are reachable
only through the BGA lightning record, so every entry is kept. FIX is 0..255 with 0x80 = 1.0,
passed through unchanged (`port/render/rd_state.h` (`RdBlend`)).

| RdBlend | ALPHA | equation | used by |
| --- | --- | --- | --- |
| 0 | 0x68 | Cs*FIX + Cd | shadow count, dissolve in |
| 1 | 0x62 | Cd - Cs*FIX | dissolve out |
| 2 | 0x64 | (Cs-Cd)*FIX + Cd | letterbox, motion blur, softening, fades |
| 3 | 0x29 | Cd*FIX + Cs | disc data only |
| 4, 7 | 0x44 | (Cs-Cd)*As + Cd | default material |
| 5 | 0x48 | Cs*As + Cd | additive, specular |
| 6 | 0x42 | Cd - Cs*As | subtractive material |
| 8, 9, 10 | 0x58, 0x52, 0x54 | the Ad forms | disc data only |
| 11 | 0x49 | Cd*As + Cd | disc data only |

**TEST** is decomposed into `RdTestState` (ATE, ATST, AREF, AFAIL, DATE, ZTE, ZTST); the fourteen
literal values the game writes are named `RD_TEST_*` so call sites stay greppable
(`port/render/rd_state.h` (`rd_test_from_gs`)). **TEXA** has three modes (`RdTexA`: 80/80, 7F/81
AEM, 80/80 AEM). **CLAMP** is REPEAT or CLAMP per axis. **TEX1**'s filters are nearest or linear,
and its mipmap fields pack into one word (`rd_tex1_lod`: MMIN, MXL, LCM, L, K) that picks the GS
level of a texture with GS levels. **TFX** is MODULATE or DECAL here; HIGHLIGHT reaches only the
staticBlur sprites, which carry it in their record (`port/render/rd_state.h`).

`RdDrawState` is the full block a list replays against. `RdPipelineKey` (16 bytes) is what
selects a GPU pipeline: program, blend path, Z test and write, colour mask, stencil mode, target
format, topology, AA1, the no-dual-source form and the sample count. Everything else (AREF, FIX,
sampler settings, PABE, FBA, DATE) goes into uniforms (`port/render/rd_state.h` (`RdPipelineKey`,
`RD_PIPELINE_REACHABLE_MAX`)).

## Frames and targets

**The frame head.** `rd_frame_head` records the flip's draw environment (FRAME with
[FBMSK](ARCHITECTURE.md#terms) 0, PSMZ32 Z write on, the centred XYOFFSET, COLCLAMP 1, TEST 0x50000)
and its clear at the head of both list 0 and list 11, since whether the frame replays from 0 or from
11 is known only when it closes; `rd_end_frame` turns the copy that is not first into NOPs.
`rd_frame_flip` patches the clear colour and the half offset in place (`port/render/rd_frame.c`
(`recordHead`, `rd_frame_flip`, `rd__frame_head_resolve`)).

**Named targets** stand in for fixed VRAM regions. The scene-class ones (SCENE, DISPLAY, WORK2,
AURA_WORK, the DATE snapshot, DISPLAY_HELD) are as wide as the
[native frame](ARCHITECTURE.md#terms) `W` in GS pixels; the
work buffers are the PS2's sizes per 512 columns, so a buffer pixel covers the screen pixels it
covers on the PS2 at every aspect (`port/render/rd_core.c` (`namedTargetDesc`, `sceneClass`)):

| target | size (GS pixels) | stands for |
| --- | --- | --- |
| SCENE | W x H, with depth and stencil | FBP 0x40 / TBP 0x800 |
| DISPLAY | W x H/2 | FBP 0, the shown buffer, retained across frames |
| SHADOW0, 1, 2 | W/2 x 256, W/4 x 128, W/8 x 64 | the shadow blur levels |
| WORK0, WORK3 | W/2 x 128 | staticBlur work buffers |
| WORK1, AA0 | W/2 x 256 | blur work; softening level 1 |
| AA1, FEED128, AURA_TAP | W/4 x 128 | softening level 2; aura feedback |
| WORK2, AURA_WORK | W x H | flare mask; aura buffer (with SCENE's Z) |
| WORK2_PAD | W/2 x 64 | a band the game clears and never reads |
| DATE_SNAPSHOT | W x H, R8 | the alpha MSB copy for DATE |
| FEED_HELD, DISPLAY_HELD | as FEED128, DISPLAY | host only: a [tick](ARCHITECTURE.md#terms)'s first present's copy |

**Scale.** A scene-class target's texture is its GS size times the scene scale; the work buffers
and the shadow levels keep their GS sizes, the PS2's, at every scale ([below](#the-glow-size);
`port/render/rd_core.c` (`rd__target_scale_of`)). A temporary target of any other size is drawn
at its GS size, except a render-to-texture block under Water reflections: Sharp
([below](#water-and-render-to-texture-blocks)). Under
[Full pixel](ARCHITECTURE.md#terms) a frame-wide scene target holds the frame's exact visible width
plus `RD_FULL_PIXEL_GUARD` texels each side, centred, so the window's pixels are its texels
(`port/render/rd_core.c` (`rd__target_scale_of`)). [Scene scale](ARCHITECTURE.md#terms) and the
display options are [DISPLAY.md](DISPLAY.md).

**Temporary targets** (`rd_temp_target`) live until the next `rd_begin_frame` unless kept, and
come from a pool of parked textures of the same size (`port/render/rd_core.c`
(`rd__temp_target_alloc`)). A target with a new texture starts cleared to zero colour and
stencil and depth 1.0, so a start's first frames never show the driver's old memory
(`port/render/rd_upload.c` (`rd__clear_new_targets`)). When the GPU cannot hold a scene-sized texture
the scale is halved between frames and `rd_scene_scale_lowered` reports it to the Resolution row
(`port/render/rd_core.c` (`rd__note_scene_pressure`, `rd_begin_frame`)).

**Z.** The shaders map GS Z to a depth that grows with it (`gs_z_to_depth`), so GS GEQUAL is the
GPU's GEQUAL. The scale is per target Z format: PSMZ32 is z * 2^-33 on a D32S8 buffer, z * 2^-32 on
the Vulkan D24S8 fallback; PSMZ24 2^-24; PSMZ16 2^-16 (`port/render/rd_frame.c`
(`rd__target_z_scale`, `rd__gs_depth`)).

**The VU block and the camera.** `rd_set_vu_common` keeps the last block of the open frame and
updates the VU image of all 13 lists; `rd__fill_camera_cb` puts the frame camera into FrameCB
(view, projection, their product, the eye, near, far, zoom, aspect) (`port/render/rd_frame.c`).

### The glow size

The work buffers (WORK0, WORK1, WORK3, AA0, AA1, FEED128, AURA_TAP, WORK2_PAD, FEED_HELD: the
glow's, the depth of field's, the softening's and the aura's chains) are the PS2's sizes at every
resolution, so each chain runs the PS2's passes on the PS2's texels, and a pass that draws a work
buffer back onto the scaled frame reads it with the GS's own filter at the frame's finer grid
(`port/render/rd_core.c` (`rd__target_scale_of`, `rd__work_class`); `port/render/rd_replay.c`
(`blurAbove1xSteps`), `rd_fx_plan.c` (`rd__sprite_above1x`)). The filter is the one the game sets: the glow's paste WORK3 -> SCENE
(`ico2/sugipon/src/staticBlur.c` (`pasteFullScreenFlare`); the title dump's 7:367) and the depth
of field's four pastes WORK0 -> SCENE (`pasteToFB`) draw under TEX1 0x60, MMAG and MMIN both
LINEAR (written by `eyeBlur` and `pasteToFB`; the dump's `FILTER 1 1` at 7:341), and the eye
blur's subtract into SCENE under 0x40, MMAG NEAREST, a single texel (`eyeBlur`). Above 1x each
SCENE texel therefore takes the GS's U and V at its own GS position (12.4, the texel's centre:
[Post passes](#post-passes)'s scaled-target rule in `fx_sprite_ps`) and the GS's 4-bit bilinear
between the same four work texels the PS2 blends there (`gs_bilerp`): the PS2's 256 x 128 glow
magnified as the GS magnifies it, sampled more finely; at 1x it is the GS's picture byte for byte.
The softening's AA0 and AA1 back over SCENE (`sprite_ps`) take the draw's own sampler, as at 1x
(`rd__sprite_above1x`). The draws from SCENE, WORK2 or AURA_WORK into a work buffer keep the area box
([Above 1x](#above-1x)). `rd_blur_test`'s `effects_scaled` and `above1x` cases compare every
target with a CPU model of that arithmetic at 2x and 4x to the bit, and `rd_shadow_test`'s 4x
softening the composite with the bilinear between AA0's texels
(`port/render/test/rd_blur_test.c` (`checkEffectsScaled`, `checkAbove1x`, `workBuffer`);
`port/render/test/rd_shadow_test.c` (`checkSoftening4x`)).

The glow's cost per frame on the issue 95 title dump (`loss-20261015-100428-lost.rddump`, 512 x
448 at 4:3): 33 `fx_sprite_ps` draws and two sprites drawn as clears (`blurSpriteClears`: the
WORK2_PAD band and WORK2's fill) in 35 render passes (`--stats`: `glow 35 passes` at every
scale). Four draws cover the frame (the two of `cleanUpFB` into SCENE, the alpha
mask SCENE -> WORK2, the paste), 29 a 256 x 128 work buffer (the reduction WORK2 -> WORK0, the 20
blur passes, the eye blur's five, its shrink, tint and `pasteWork0ToFB`). The fragments are the
frame-sized draws' texels plus the work draws'; the paste's texel loads are its texels times its
taps: the GS's bilinear four, against the tool of 2130df09c's work scale (2 at 2x and above) with
the B-spline down (and across at 3x) or, with its `--glow-original`, the work buffers at their GS
sizes and the B-spline both ways (16). GPU milliseconds of the glow effect, lavapipe (Mesa
25.0.7, `ICO_RD_BLEND=fixed`, the fastest of three serial runs of `rd_replay_tool DUMP out.png
--stats [--enhanced --resolution Nx --present 1376x896]`):

| scale | fragments: work scale; GS sizes | paste loads (taps): work scale; `--glow-original`; the GS's filter | glow ms: work scale; `--glow-original` | glow ms, the GS's filter |
| --- | --- | --- | --- | --- |
| 1x | 1.87 M; the same | 0.92 M (4) in all three | 46.7; the same | the same draws |
| 2x | 7.47 M; 4.62 M | 7.34 M (8); 14.68 M (16); 3.67 M (4) | 225.0; 181.4 | 93.6 |
| 3x | 12.06 M; 9.21 M | 33.03 M (16); 33.03 M (16); 8.26 M (4) | 440.7; 372.0 | 169.3 |

The work scale and `--glow-original` columns are the tool of 2130df09c; the last is the tool with
the work buffers at their GS sizes and the GS's filter at the paste. In one series on the same
host the 2130df09c tool's glow is 42.6 ms at 1x and 35.5 with the paste turned off (`--nop
7:367`), 175.6 and 68.4 at 2x with `--glow-original`, and 113.6 without the paste at 3x with it:
the 1x paste (the GS's bilinear over 229,376 texels and SCENE's snapshot) costs 7.1 ms, so at the
GS's filter it costs about 28 ms at 2x and 64 at 3x there. The last column's 2x and 3x are the
release's verification run, the tool after the change against 2130df09c's in three interleaved
pairs of replays on the same host, the fastest of each (GPU ms of the glow, lavapipe):

| frame | 2130df09c | its `--glow-original` | after |
| --- | --- | --- | --- |
| title 1x | 45.3 | 45.0 | 42.3 |
| title 2x | 221.4 | 178.8 | 93.6 |
| title 3x | 438.1 | 362.9 | 169.3 |
| Mac 2x | 253.7 | 219.6 | 106.0 |
| forest 2x | 195.7 | 192.8 | 94.0 |

At 1x every target is equal before and after on the three dumps of that run.
The other costs above 1x are the frame-sized draws (four, at the scene's scale) and the reduction
WORK2 -> WORK0, a box of 4 x 8 texels at 2x (`blurAbove1xSteps`).

## Textures

The texture cache decodes the TIM2 bytes Texture.c keeps in memory into an rd texture when first
needed and keeps it until the content changes (`port/render/rd_tex.h`; decoded on worker threads:
[TEXTURES.md](TEXTURES.md)):

- **key**: (texture id, content generation, TEXA mode); the mode is one of `RdTexA` (alpha baked)
  or `RDTEX_TEXA_REPLAY` (the source format's alpha kept and TEXA applied by the shader at replay,
  so TEXA leaks between lists as on the GS).
- **decode**: PSMCT32, 24, 16, 16S, PSMT8, PSMT4, PSMT8H, PSMT4HL, PSMT4HH and the PSMZ formats;
  CLUTs in CSM1 or index order. Output RGBA8 with the raw GS alpha (0x80 = 1.0) for 32-bit texels,
  the A bit for 16-bit ones and 0 for 24-bit ones (`rdtex_decode`).
- **size**: padded with zero texels to 2^TW x 2^TH, so STQ and REPEAT wrap as on the GS.
- **update**: a new generation of the same size and format re-expands into the same rd texture;
  otherwise the old one is retired and destroyed two `rdtex_frame_tick` calls later.
- **levels**: a mipmapped TEX1 stores the TIM2's own levels when they are authored pictures
  (`rdtex_store_levels`, `RDTEX_AUTHORED_RATIO`), sampled at the GS's level; otherwise the
  replay builds the further levels by a 2x2 box, colour weighted by alpha, with alpha coverage
  kept for alpha-tested textures (`port/render/rd_upload.c` (`uploadMips`);
  `port/render/rd_tex.h` (`rdtex_build_mip_chain`, `rdtex_keep_alpha_coverage`)).

**Sampling at the GS's level in the Original preset.** A linear-filtered draw of an image with
GS levels (`DF_GS_LOD`) is filtered as the GS filters, in one of two forms
(`port/render/rd_pipeline.c` (`rd__gs_filter_int`, `rd__texa_per_texel`)):

- **TEXA per texel**: a 24- or 16-bit source (or CLUT) under a TEXA whose alpha depends on the
  texel (AEM, or TA0 and TA1 apart: every mode but 80/80) goes through the TEXA entries
  (`sprite_texa_ps`, `vu_texa_ps`, and `sprite_stq_ps`'s four taps), whose integer filter loads
  the four texels of each level, expands TEXA, blends them with the GS's 4-bit weights across
  then down, each step truncated, and blends the two levels in 256ths (`port/shaders/common.hlsli`
  (`gs_texa_texture_lod`)).
- **The sampler on the GS's grid**: every other one (an RGBA32 texture; TEXA 80/80, which gives
  every texel the same alpha) takes the sampler (`ICO_DF_GS_BILINEAR_GRID`, `common.hlsli`
  (`gs_sample_gs_bilinear`, `gs_grid_level`)). Per level the coordinate is rounded to the GS's
  12.4 grid, `q = floor(uv * size * 16 + 0.5)`, as the integer filter rounds it, and handed to
  `SampleLevel` as `q / 16 / size`, exact in float for the power-of-two sizes of a TIM2 picture,
  so the sampler's texel pair and weight are the GS's (`(q - 8) >> 4` and `(q - 8) & 15`, four
  fraction bits, which every device's sub-texel precision holds); the sampler is nearest between
  levels (`port/render/rd_replay.c` (`gsLodSampler`)), and for MMIN 3 and 5 the shader samples
  the two levels and blends them by the LOD's fraction in 256ths, truncated, as the GS does. What
  is left is the GS's truncation after each of its three steps against the sampler's one rounded
  sum: the sampler's value is 0 to 2 levels above the GS's, never below, on a device whose filter
  is exact to within a level (`shaders_pixel`'s grid check, over every pair of 4-bit weights and
  texel values in steps of 17: 21 % of the samples +0, 74 % +1, 4 % +2; a flat area is +0, so the
  difference is a systematic half to one level where texels differ), and the blend of two levels
  keeps the bound. `rd_pixel`'s gs grid case checks it
  through the replay against the GS's arithmetic.

The replay counts both: `rd_replay_tool --stats` prints `texa draws N (TEXA per texel), M
GS-level draws on the GS's grid` (`RdPerfRecord` `texaDraws`, `gridDraws`, per planned pass, as
`a1xDraws`), and the first grid draw of a run logs `rd: GS levels (Original): ...` once.

## The replay

`rd__replay_frame` records one RHI command list per replay. The lists are walked in order 0..12
(11 and 12 for a keep frame) from the frame's start state; state commands update the state block
and actions draw (`port/render/rd_replay.c`):

| command | replay |
| --- | --- |
| `RDC_CLEAR` | a render pass with a load-op clear, or a pending clear folded into the next pass |
| `RDC_SCREEN` | GS window-space prims through `sprite_*_vs` / `sprite_ps` (`doScreen`) |
| `RDC_EXACT_BLEND` | `blend_int` on RGBA8_UINT copies of source and destination, copied back (`doExact`) |
| `RDC_COPY` | a texture copy (`doCopy`) |
| `RDC_MESH` .. `RDC_PARTICLES` | the VU1 program shaders (`rd__replay_vu`, `port/render/rd_replay_vu.c`) |
| `RDC_SHADOW_*` | the stencil count (`doShadowReset`, `doShadowStrip`, `doShadowResolve`) |
| `RDC_POST_STUB` | `RD_POST_FOG`: `doFog`; the reduction and the staticBlur kinds: `rd__blur_post_sprite` (`port/render/rd_blur_border.c`) |
| `RDC_OVERLAY_TEXT` | nothing here: the present collects it |
| anything else | `rd__not_implemented`: logged once and skipped (fatal in the tests) |

**GS sampling rules.** The GS samples a pixel at its integer coordinate and covers `x0 <= x < x1`
for a sprite; the GPU samples at x + 0.5 with the top-left rule. FrameCB's origin moves GS
coordinates half a pixel right and down, so a GS integer lands on a GPU pixel centre and the two
rules select the same pixels. The game's -4 corner nudges and +8 UV nudges stay in the data
(`port/render/rd_replay.c`, header comment).

**Screen prims.** `expand` turns sprites, fans and strips into triangle lists, points into
one-pixel sprites and line strips into line lists, after the mirror mode's UI flip. Sprites on a
scaled target have their corners snapped to whole GS pixels. Consecutive `RDC_SCREEN` commands
that bind the same things are one draw (`joinsRun`, `flushScreenRun`); every action that records
anything flushes the run first (`port/render/rd_replay.c` (`expand`, `doScreen`, `ScreenRun`)).
A triangle with STQ is drawn perspective-correct, and the screen-prim triangles of the sprite
shaders carry their own colour and STQ planes (below) (`port/render/rd_replay.c`).

**Passes.** Render passes stay open across draws to the same colour and depth pair and close on a
target change, a clear, a copy, or a draw that samples a target whose state must change. rd tracks
every texture's RHI state and records every transition; a backend adds no texture transition of its
own (D3D12 keeps a record only to fill in a transition from UNDEFINED,
[RHI.md](RHI.md#direct3d-12)). A draw that samples its own render target samples a copy taken just
before it, as the GS reads the live buffer (`port/render/rd_replay.c`, header comment).

**The exact viewport.** While a frame's lists replay, every pass has one viewport size, the
largest power of two the GPU takes, centred on the target, and FrameCB maps positions to it; the
scissor, always inside the target, bounds the drawing. Every step is exact for a GS position at an
integer scale, and the clip-space numbers are the same at every frame width, so the GPU clips the
same corners the same way whatever the width (`port/render/rd_replay.c` (`s_vpExact`)). The
present and the camera probe keep the plain mapping.

**Uploads.** Before the first pass the replay uploads pending textures and their levels through
the frame's upload ring, and the static meshes the frame draws into the device arena (once, and
again only after a morph rewrote them); the interpolation's scratch meshes stay on the ring
(`port/render/rd_upload.c` (`rd__upload_dirty_textures`, `rd__upload_meshes`)); the ring shrinks again ([WORK_BUFFERS.md](WORK_BUFFERS.md#the-upload-ring-shrinks)). Every
`rd__ring_alloc` caller checks for `~0` (the ring is full) and skips its write and its draw or
copy, the untextured draws' white texel included, which the next replay uploads
(`port/render/rd_replay.c` (`rd__ring_alloc`); `port/render/rd_upload.c`
(`rd__upload_dirty_textures`)). A text texture (a coverage format: a menu text page or a glyph
page) that was not made, that the device refused, or that found the ring full is named once in
the log, `rd: text: a menu texture could not be uploaded (...)` (`port/render/rd_tex.c`
(`rdtex_text_upload_failed`), from `rd_create_texture_sheet` and `rd__upload_dirty_textures`; a
glyph page `rd_create_texture_r8` could not make logs the same words with `ui:`,
`port/ui/font.c` (`pageTexture`)).

**Bind groups.** Bind groups are valid only in the [frame slot](ARCHITECTURE.md#terms) they were
made in, so their caches are tagged with an epoch every `rhi_wait_frame` of the renderer starts anew
(`rd__wait_frame`). Uniform blocks are written once per replay per content (FNV-1a), and texture
groups are cached by (texture, sampler, DATE snapshot) (`port/render/rd_replay.c` (`s_bindEpoch`)).

**DATE snapshots.** TEST.DATE tests the destination alpha's MSB. The replay copies the bound
target's alpha MSB into the R8 `RD_TARGET_DATE_SNAPSHOT` over the area the draw can touch, and the
shader tests it at the fragment's own texel. The snapshot is retaken when the target changed or
anything may have written alpha since, so consecutive DATE draws see each other's writes; within
one draw, overlapping primitives see the snapshot (`port/render/rd_replay.c` (`dateSnapshot`,
`rd__date_retake`)).

**DATE through the stencil bit.** Where the profile has `dateStencil` ([The device
profile](#the-device-profile)), a DATE draw of a screen-prim or VU command into a scene-class RGBA8
target with a depth of its size bound takes no snapshot: inside the draw's own pass (begun with the
colour readable, `rd__pass_wants_readable`) bit 6 of the stencil (`RD_DATE_STENCIL_BIT`; the shadow
count keeps bits 0-5, [Shadows](#shadows)) is made the destination alpha's MSB over the area the
draw can touch: `date_clear_ps` writes 0 into the bit (`RD_STENCIL_DATE_CLEAR`), a by-region
barrier makes the pass's colour writes visible (`rhi_cmd_local_read_barrier`, colour),
`date_stencil_ps` reads the alpha as an input attachment and writes 1 into the bit where the MSB is
set (`RD_STENCIL_DATE_SET`), and a second barrier orders that read before the next writes. The
draw's passes then test the bit (`RD_STENCIL_DATE_TEST1` for DATM 1, `TEST0` for DATM 0,
reference 0x40, read mask 0x40, no write) with `DF_DATE` off (`rd__date_stencil_pass`, in
`expandPasses`; `dstBind` sets the reference). The bit is kept by `rd__date_retake`'s rule on the
pass's own record (the same pass, target and write serial, the area held), so it is written again
exactly where a snapshot would be retaken, and the open screen run is drawn before the read.
Within one draw the alpha is the one before its first primitive: the snapshot's semantics, the
same bytes (not PRIM.AA1: **DATE and AA1**). Not inside the MSAA epoch, under the exact blend's storage read, on another target or
without a depth: the snapshot. The DATE self-test decides per device ([The fog path and its
self-test](#the-fog-path-and-its-self-test)) (`port/render/rd_replay.c` (`dateStencilFor`,
`prepareDrawEx`, `bindDrawEx`); `port/render/rd_replay_tiler.c` (`rd__replay_date_take`);
`port/render/rd_pipeline.c` (`shadowStencil`,
`rd__date_setup_key`, `rd__enumerate_reachable_date`); `port/shaders/sprite.hlsl`
(`date_clear_ps`, `date_stencil_ps`)). `--stats`: `DATE snapshots N, DATE stencil draws M (K
setups)`.

**DATE and AA1.** The GS tests DATE against the alpha as it writes each pixel (PCSX2
`GSDrawScanline.cpp`), so in an AA1 strip under DATM 1 the edge pixel a triangle writes along a
shared edge (alpha below 0x80) turns away the next triangle's pixel there; testing every primitive
against the alpha before the draw added both rows (the ripples' bright outlines, `puddle.c`). A
PRIM.AA1 draw under DATE that writes alpha is drawn primitive by primitive inside its pass, each
primitive as two draws, the fragments that keep the alpha's MSB as DATE wants it and then those
that change it (`DF_AA1_MSB_SET`, `DF_AA1_MSB_CLEAR`), whose stencil carries bit 6 to the next; a
triangle's split again by Z write: up to four draws a triangle, at most 320 for the 223424 dump's
ripple command 4:147, no pass break. The stencil path keeps bit 6 the MSB (`TEST1` and
`TEST1_CLR`, `TEST0` and `TEST0_SET`); the snapshot path keeps `DF_DATE`, clears the bit over the
area first (`rd__replay_date_clear`) and marks the texels the draw changed (`TEST0`,
`TEST0_SET`). The storage read, the MSAA epoch and a depth not of the target's size keep one test
per draw, logged (`aa1: a DATE draw keeps DATE's one test per draw ...`; the first command drawn
so: `aa1: a DATE draw's primitives drawn one at a time ...`) (`port/render/rd_replay_aa1.inc`
(`aa1DatePlan`, `aa1DateDraw`), which says why it is exact; ctest `rd_pixel` aa1 wedge).

**The fog's table.** The replay's upload phase copies the first `RD_POST_FOG` record's 256-entry
table of the lists it draws into the LUT texture, outside any pass; `doFog` copies a table only
when its fog's is another (`port/render/rd_replay.c` (`rd__fog_lut_upload`, `fogLutCopy`)).

**The fog's local read.** On the `local` fog path ([The fog path and its
self-test](#the-fog-path-and-its-self-test)) every pass on a scene-class target with its own
depth, the depth made for it (`RD_DST_USAGE_LOCAL_DEPTH`), binds the depth readable
(`RhiDepthAttachment.readable`, the texture in `RHI_STATE_RENDER_TARGET_READ`), and a fog whose
depth is its colour target's own (SCENE's, at its size) draws in that pass: a by-region barrier
(depth), `fog_lut_local_ps` reading the fragment's own depth texel as an input attachment, a
barrier back; the pass is neither ended nor begun again and the depth is not copied, and the
`fog copy` piece of the per-pass timing is not begun. The depth word and the arithmetic are the
copy path's (the sprite's UV addresses the fragment's own texel there), so the picture is the same
bytes. A fog on another depth, inside or before the MSAA epoch, or under an exact blend takes the
copy (`port/render/rd_replay.c` (`fogLocalFor`, `doFog`, `depthWantsReadable`);
`port/shaders/fog_lut.hlsl` (`FOG_LOCAL`)). `--stats`: `fog local draws N (fog path local)`.

**The blur pair fold.** Where the profile has `blurFold`, a pair of staticBlur.c's `blur(n)`
(pass 1 WORK0 -> WORK1 with the rect shrunk, pass 2 WORK1 -> WORK0 with the UVs shrunk) is drawn
as one pass into WORK0 through `fx_blur2_ps`, which computes each WORK1 texel pass 2 weighs from
WORK0 as pass 1 writes it (its UV, the four WORK0 taps, `gs_bilerp`, the texture function with
pass 1's colour, FBA, TEXA): the integers of the two passes in their order, so WORK0 is the same
bytes. At pass 1 the replay looks ahead over the list (`s_loop`): state commands, pass 2, state
commands, the next pass 1; it folds when both passes are textured from each other at scale 1
without a crop (the same size and origin), LINEAR, CLAMP, no blend, alpha test, DATE, destination
read or depth, the colour mask whole, the same flags, TEXA and TEX0 size, every WORK1 texel pass 2
weighs (a weight above 0) holding pass 1's bytes (its coverage, or the glow's edge strip), and the
next pass 1 leaving its bytes in those texels or more, so WORK1 ends as the unfolded chain's; the
loop then skips to pass 2 with the state applied. WORK0 is read through its snapshot (the swap
where the fold covers it whole). Of ten pairs 9 fold at 4:3 and 16:9, 8 at 48:9
([WORK_BUFFERS.md](WORK_BUFFERS.md#the-blur-fold-on-a-wide-frame)) (`port/render/rd_blur_border.c`
(`rd__blur_post_sprite`); `port/render/rd_replay_tiler.c` (`rd__replay_blur_fold`, `blurFoldPass`,
`blurFoldOk`); `port/shaders/fx_sprite.hlsl` (`fx_blur2_ps`);
`port/shaders/shader_consts.h` (`IcoDrawCB.aux0`)). Start line, once: `rd: blur fold: on (<class
or [dev] blur_fold>): each staticBlur pair but a chain's last drawn as one pass (fx_blur2_ps)`;
`--stats`: `blur pairs folded N`.

**Store ops.** Every pass stores its colour; a pass after its depth's last touch neither loads nor
stores the depth ([WORK_BUFFERS.md](WORK_BUFFERS.md#the-store-plan)); else the stencil's store
follows the shadow window (`rd__stencil_ops_packed`).

**Exact blends.** `RDC_EXACT_BLEND` (from `rd_post` with `exactInt`) runs the GS integer blend
through `blend_int` on RGBA8_UINT copies of both targets and copies the result back; source and
destination must be the same size (`port/render/rd_post.c`; `port/render/rd_replay.c`
(`doExact`)).

**AA1.** PRIM.AA1 on lines and triangles: `aa1Expand` adds a one-GS-pixel fringe along each edge
(outside it, along its minor axis) whose coverage runs from 1 on the edge to 0 and is the fragment's
alpha in `sprite_aa1_ps`; a triangle's own vertices carry `ICO_AA1_INTERIOR`. A draw whose fringes
need Z write off is split in two: every interior with the draw's Z write, then every fringe without
(`port/render/rd_replay_aa1.inc` (`aa1Band`, `aa1Fringe`, `aa1Expand`), included by
`port/render/rd_replay.c`). A triangle's fringe ends where PCSX2's software renderer ends it
(`GSRasterizer.cpp` `DrawTriangle`, `DrawEdgeTriangle`): an edge pixel is kept only where the
triangle's other two edge functions are above 0 (integer, 12.4, with the vertices sorted by y the
GS's way, `s_ysort`; negated for a clockwise triangle; plus 1 on a top-left edge:
`tl0 = y0 == y1 || !cw`, `tl1 = cw`, `tl2 = y1 != y2 && !cw`) and inside the edge's box, x from
floor(min x) to ceil(max x) and y from ceil(min y - 1) to floor(max y + 1) in GS pixels. The band is
drawn past both ends of its edge by a whole number of edge lengths, at least two GS pixels
(`aa1Reach`: the ends stay exactly on the edge's line, the attributes carried along it and clamped),
and `sprite_aa1_ps` (`aa1_wedge_keep`) keeps or discards each fragment with those tests on the
triangle's flat fields (`IcoSpriteAa1Vertex` `v0x`, `v0y`, `d1`, `d2`, `flags`: the sorted first
vertex, the other two less it as int16 pairs, the edge, clockwise and the three biases). On the
sample points' 1/16 grid the box is `X > min x - 16`, `X < max x + 16`, `Y >= min y - 16`,
`Y <= max y + 16` (12.4), which needs no XYOFFSET; the sample point is the GS integer coordinate on
the texel's centre (`aa1_pixel_xy`: XYOFFSET + 16 k at 1x exactly, the texel's own point rounded to
1/16 above 1x). Clipping the geometry instead would round the cuts to the 1/16 grid and flip pixel
centres. A triangle wider or taller than `ICO_AA1_CLIP_EXTENT` (32751 in 12.4) keeps its fringes
whole and unclipped (`flags` 0, as lines and interiors), so the integer tests stay inside 32 bits.
At 1x the pixels drawn are PCSX2's exactly (ctest `rd_pixel`, aa1 wedge: every pixel's fragment
count against a C model of `DrawTriangle` and `DrawEdgeTriangle`). A band ending in an axis-aligned
cap at each vertex, unclipped, draws pixels past the corner outside the wedge (the spurs at the
corners of the drained room's ripples, issue 106) and misses some inside it. The fringes of an edge
two triangles share are both drawn, as on the GS. DATE inside one AA1 draw is the snapshot's (**DATE
and AA1** above). The log says
`rd: aa1: triangle edges clipped to the GS's wedge and box (the first command: N triangles, N clipped, N vertices)`
once, and
`rd: aa1: a triangle W x H GS pixels is past the edge clip's reach (2046): its edges drawn unclipped (N so far)`
for the first three such triangles and then at each power of two.

**COLCLAMP 0.** Additive and subtractive screen prims under COLCLAMP 0 keep the low 8 bits of
every result (darkVolume.c's count). `doScreenWrap` draws them into an RGBA16F accumulator and then
resolves `(Cd + acc) mod 256` against a copy of the target (`port/render/rd_replay.c`
(`doScreenWrap`); `port/shaders/raw_wrap.hlsl`).

**VU draws.** `rd__replay_vu` binds the draw's VU block, the mesh stream (the device arena or the ring), the
bones for a skinned draw, and sets the draw's flags: a chain clip plane, the free camera's
`ICO_VU_BEHIND_EYE`, `ICO_VU_EXTENDED` outside [out of the box](ARCHITECTURE.md#terms)
(`rd__vu_extended`), and `ICO_VU_TICKS` for a draw seen through a camera blended between two
[ticks](ARCHITECTURE.md#terms). A mesh with marked triangles binds its mark bytes after its stream
(`vu_mark`, [VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-mesh-path)). A grid draw builds its index list
here and marks every triangle of a grid of two strips or more joined, as the draw's own mark
(`port/render/rd_replay_vu.c` (`rd__replay_vu`, `doVuDraw`); `port/render/rd_pipeline.c` (`rd__vu_extended`)). At start the renderer logs `rd: VU
index values: dense (marks in the stream)`.
A mesh, grid or particle command the pre-passes recorded draws through `vu_main_vs` (or
`vu_particle_main_vs`) and `vu_ps` over that record ([The vertex and triangle
pre-passes](PIPELINES.md#the-vertex-and-triangle-pre-passes)).
What the flags do is [VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-region-rule). `ICO_RD_VU_OFFGRID=1`, a
developer switch, lets the [VU1](ARCHITECTURE.md#terms) programs output X and Y off the GS's 1/16
grid on a scaled target under the Enhanced preset; otherwise every preset keeps the GS's
quantisation (`port/render/rd_interp.c` (`rd__vu_off_grid`); `port/render/rd_replay.c`
(`rd__frame_group_ex`)).

**The MSAA epoch.** With an MSAA setting (never under the Original
[preset](ARCHITECTURE.md#terms)), SCENE has a
multisampled twin, SCENE_MS. The epoch opens at the first pass into SCENE (with SCENE's depth or
none) and every such pass draws into SCENE_MS; it closes at the first command that reads or writes
SCENE's single-sampled colour or depth any other way (a texture read of SCENE, a DATE snapshot, a
copy or exact blend, the wrap's snapshot, the shadow count's pass, the end of the replay). Closing
resolves once under the covered-majority rule ([MSAA.md](MSAA.md)). Integer-aligned sprites drawn inside it are moved half a texel so they cover whole pixels; the
fog runs per sample inside the epoch when the device has sample-rate shading
(`port/render/rd_replay.c`, "the MSAA epoch"; `msaaPass`, `msaaEnd`, `msaaSpriteExpand`). Otherwise
the fog reads the resolved depth ([DIVERGENCES.md](DIVERGENCES.md) VD-22); the log says once a run which one ran (`fog: in the MSAA
epoch per sample ...` or `... on the resolved picture ...`, `port/render/rd_replay.c` (`doFog`)). When no
way of reading the multisampled depth passed the fog's self-test, rd draws without MSAA and
`rd_aa_msaa_blocked` says so (`port/render/rd.h` (`rd_aa_msaa_blocked`)).

### The pass ring

The replay notes what it gives the GPU in a ring of the last 2048 entries, always on (a few stores
each): every render pass (`beginPass`), target copy (`doCopy`), snapshot copy (`takeSnap`), exact
blend (`doExact`), the upload batch before the lists, the VU vertex and triangle pre-passes
(`rd_replay_vu.c` `rd__vu_prepass`: `RD_PASSK_VU_PRE`, `RD_PASSK_VU_TRI`) and the present. An entry holds its sequence
number, the replay number (the performance record's, `g_rdPerf.replay`) and frame number, the keep
flag, the list being walked, the colour and depth target ids and sizes (a copy's source and
destination; the present's DISPLAY and window sizes), the storage-pass flag, the picture effect
(`perfPost`), the last pipeline asked for while it was open (`rd__get_pipeline`) with its shaders
and destination read, and an upload batch's bytes. A table of the last 8 replays keeps each one's
first and last sequence numbers, so a number the ring has moved past still names its replay and
frame. Each entry's sequence number is also the marker the replay gives `rhi_cmd_checkpoint` right
after the pass begins (or before the copy), and every replay sets `rhi_set_submit_tag` to its replay
and frame numbers, so the backend's [loss report](RHI.md#device-loss) and the ring name the same
passes. Once the device is lost the replay returns before recording anything (`replayFrame`), so
the ring ends at the last replay given to the GPU. A replay records about 430 entries, so the ring
holds the last three replays whole while each stays under about 680: the last one recorded and the
two before it, the oldest of them the submission whose wait failed (`RHI_FRAMES_IN_FLIGHT` back).
The window prints the replay table and then the entries of those three replays (from the oldest
entry held when the ring has moved past the start of the first; at most the ring's 2048 lines)
after a device loss, before the box ([PLATFORM.md](PLATFORM.md#diagnostics))
(`port/render/rd_passring.h`, header comment; `port/render/rd_passring.c`
(`rd_passring_print_start`, `rd_passring_print`); `port/render/rd_replay.c` (`replayFrame`,
`beginPass`, `doCopy`, `takeSnap`, `doExact`); `port/render/rd_pipeline.c` (`rd__get_pipeline`)).

### The loss dump

On a device loss the window writes, after the pass ring and before the message, the frames the
renderer still holds as one zip in the logs folder, `logs/loss-<date>-<time>.zip`: the last
closed frame (the one the lost replay drew, or the tick it was blended towards) as
`loss-<date>-<time>-lost.rddump`, then the frame before it as `loss-<date>-<time>-before.rddump`
when the frame ring still holds it, then a copy of the log. They are ordinary dumps written from the CPU side
(the commands, the payload, the image texels and VU streams the renderer keeps), with the textures
as F12 writes them, so the writing needs no device; the lost frame is the zip's first `.rddump`,
the one `rd_replay_tool` and the loss bisect take. The newest three loss zips are kept, the older
ones deleted, so a device lost at every start cannot fill a phone. The activity file then records
the loss with the zip's path (or that none was written) for the next start
([PLATFORM.md](PLATFORM.md#diagnostics)); the message names the file and asks for it with the log
(`port/render/rd_dump.c` (`rd_dump_retained`); `port/platform/dump_zip.c` (`ico_loss_dump_zip`,
`ico_loss_dump_prune`); `port/platform/window_host.c` (`loss_dump`, `device_lost_quit`)).

A GPU hang writes the same zip: when the watchdog ends a run whose main thread waits for work the
GPU never finishes ([PLATFORM.md](PLATFORM.md#diagnostics), "A GPU hang"), the window writes the pass
ring and the loss dump from the watchdog thread. The main thread, the only one that writes the
frame ring, the texture and mesh records and the pass ring, is blocked inside the driver, so they
stand still while they are written; the texture pack's loader thread fills only its own queue, and
the pipeline compiler threads run only inside a batch the main thread waits for. Nothing in the
dump needs the device, so none of it calls the hung one. The lost frame is then the last closed
frame, the one whose replay the wait belongs to or the one after it. The activity file notes it with
the hang's own line, which the next start counts as a loss but does not bisect: the bisect runs
before the watchdog and waits for the GPU with no limit, and a driver that hangs at that frame would
hang it too (`port/platform/window_host.c` (`window_gpu_hang`); `port/platform/diag_host.c`
(`ico_diag_note_device_loss`)). The zip goes with the report for the developers' replay.

### The loss bisect

The start after a loss that left a loss dump (of the same version) finds the pass that loses the
device on that device, before the game; a GPU hang's dump is not bisected (see the loss dump above). Only a start with the player's own blending and backend
bisects: a run the loss restart started (its step's fixed-function blend or Direct3D 12) logs that
the bisect was not run and leaves the zip in the logs folder
([PLATFORM.md](PLATFORM.md#diagnostics)). A renderer of its own, sized to the dump's frame, loads the
lost frame and replays it with nothing presented. Its pipelines are made first as the start-up
set's are: a replay with no draw recorded names the keys it finds no pipeline for without compiling
any (`rd__pipeline_gather`), and those keys compile on worker threads in batches
(`rd__pipeline_create_gathered`, `rhi_create_pipelines`) while the window's "Preparing graphics"
screen shows and takes the window's events (`port/platform/window_host.c` (`bisect_progress`)),
again while a replay names new keys, at most four times. A close asked meanwhile stops the bisect,
and a loss in these replays is reported as a loss with no draw recorded. Then the bisect replays the
frame with no draw recorded, which counts the frame's passes (their clears and layout changes still
run, and any pipeline still missing is created), then as passes 1..k for k = 1, 2, ..., waiting for the GPU after each replay
(`rhi_wait_idle`, which reports a loss like any other call). The first replay that loses the device
names the pass, logged in the pass ring's words:
`ico_pc: loss bisect: pass 12 of 61, list 3, pipeline 409 (vs 7 fs 5 dst 0), pass colour 4
(640x448), depth 5 (640x448), 14 draws`, with a second line naming the pass whose first command
the walk stopped at (that command's work before its pass began, a fog depth copy for example, ran
too). A frame that runs to its end says so (`the whole frame (61 passes) ran one more pass at a
time without losing the device`). The bisect's renderer is then shut down, lost or not, and the game's
own starts as at any start: the bisect is diagnostics only. A driver that cannot make a device
again in the process that lost one ends that start with a box asking to start the game again
(`ico_window_open` returns -3, `port/platform/main_host.c` (`open_window`)); the bisect is done by
then, so the next start is a normal one. On Android this holds for a graphics driver the player
chose too: after a bisect that lost the device it is not recorded as failed nor turned off, and the
next start loads it again. The activity file says the bisect is
running while it runs, so a start ended inside it does not bisect again, and the file is deleted
when it is done ([PLATFORM.md](PLATFORM.md#diagnostics)) (`port/render/rd_dump.c`
(`rd_loss_bisect`); `port/platform/window_host.c` (`window_loss_bisect`, `ico_window_open`);
`port/platform/diag_host.c` (`ico_diag_loss_bisect`)).

The replay behind it is the pass window (`rd__replay_set_window`): every replay numbers its passes
in the order they begin (the [vertex and triangle pre-passes](PIPELINES.md#the-vertex-and-triangle-pre-passes) first when they run, then
render passes, copies, snapshots and exact blends: the pass ring's entries other than the uploads
and the present). With a window [first, last] the draws and the target and snapshot copies of the
passes outside it are not recorded (the pre-pass's record copies go with its draws, so the slot's
record buffer keeps what an earlier replay of the frame wrote; the uploads and the fog's depth copy
are not windowed), the passes themselves still begin, and the walk stops after the
command that began pass last + 1, which draws nothing. So a loss in the vertex pre-pass is pass 1
and one in the triangle pre-pass pass 2, not a loss with no draw recorded (`port/render/rd_replay_vu.c` (`rd__vu_prepass`,
`rd__replay_pass_push`)). [1, k] is the frame up to pass k, [k, k]
pass k alone on the targets as an earlier replay left them. `rd_replay_tool --loss-bisect` runs the
bisect on a dump, `--passes A[-B]` replays a window (`port/render/rd_replay.c` (`passPush`,
`drawCmd`, `copyCmd`, `replayFrame`); `port/render/rd_window.c`; `port/render/rd_internal.h`
(`rd__replay_set_window`, `g_rdWin`)).

**The second level: the draw.** When the first level ends on a pass that drew, the same start
narrows it to the draw, one step at a time, each step one replay of the lost frame on a renderer of
its own (`rd_init`, the step, `rd_shutdown`, lost or not: the reporter's log of issue 95 shows a
fresh device works after a loss on that driver). A step replays passes 1..P - 1 as the first level
ran them and, of pass P, the draws `drawFirst..drawLast` that pass the step's filter, numbered as
the pass asks for them (every draw call, the exact blend's second pass too: the first level's
count); then it waits for the GPU. The draw window (`rd__replay_set_window_draws`; the filters
`MASK_MARKS`, every VU draw with its triangles' marks left out, `vu_mark` zero; `VS_ONLY(vs)`;
`CLIP_ONLY(clip)`; `SKINNED_ONLY`; `TEX_ONLY(id)`) costs a normal replay nothing: the draw
counters test `s_win.first` as before. While it is set, `vuDraw` notes each VU draw of pass P (list,
command, mesh name, program, vertex and pixel shader, clip mode, batch, index range, the values'
span and marked triangles, the texture's id, size, format and mip levels, the pipeline key's words),
and the window keeps the note of the last draw it recorded (`RdDrawNote`, `rd_draw_note_text`).
The note's vertex shader is the program's own entry, also for a draw through a main shader and
for the pre-passes' draws (passes 1 and 2, noted by `rd__vu_prepass` with their vertex range), so
`VS_ONLY` separates the programs; the key's words name the pipeline drawn with. `MASK_MARKS`
masks the record stages' marks too.
The planner (`port/render/rd_bisect_plan.c`, pure) says which step comes next from the steps so far:
first the whole pass with the marks masked (the hypothesis test: clean means the marks did it),
then the prefix search, halving between the longest clean prefix (0) and the shortest lost one (N,
the first level's), 11 steps for 1,312 draws, then draw D alone, then D's classes over draws 1..D
(its vertex shader, its clip mode, the skinned draws, its texture), and, when no step lost the
whole pass again, that pass once more; at most 16 steps. Prefix verdicts that contradict each other
(a clean prefix as long as a lost one: something other than the draws loses the device) end it and
say so. Each step is logged (`ico_pc: loss bisect: step 4 of 16: pass 1, draws 1..656 of 1312:
lost in 0.6 s`), and one final line names the draw, its note and the verdict (`faults alone`, `does
not fault alone`, `not finished`), and how the masked step went (`clean with the marks masked`).
The steps' verdicts are written to `logs/loss-bisect.txt` before the next step, so a start ended
inside a step continues at the next start (that step counted as lost, `killed`); 6 starts, 90 s
of steps a start and 3 steps that lose the device a start at most (Windows restarts the system
after 5 graphics resets in 60 s; [PLATFORM.md](PLATFORM.md#diagnostics)). Between the steps the start-up
screen says "Checking the graphics problem (step 4 of 16)", drawn on the step's fresh renderer
before its replay, never on a lost device (`port/render/rd_dump.c` (`rd_loss_bisect_step`);
`port/render/rd_window.c` (`rd__win_take`, `winFilter`, `rd__win_vu_note`); `port/platform/window_host.c`
(`window_bisect_step`); `port/platform/diag_host.c` (`ico_diag_loss_bisect2`)).
`rd_replay_tool --loss-bisect --passes P --draws A-B [--mask-marks]` runs one step on a dump;
without `--loss-bisect`, `--draws` and `--mask-marks` shape a replay's picture.

**The pass split.** `ICO_RD_PASS_SPLIT=N` (`[dev] pass_split = N`; off by default) ends a render
pass and begins it again, both targets loaded, after every N VU draws, as the exact blend begins a
pass again (the scissor taken back), so a GPU that runs a pass's draws as one job gets jobs of N:
the fallback if a device still faults on a pass of many VU draws. It is refused, logged once, in a
multisampled pass, a storage pass, a pass the exact blend's attachment read made readable
(`port/render/rd_replay.c` (`rd__replay_exact_readable`)), and inside a draw that takes DATE
through the stencil bit (`Replay.dateStNow`: the bit lives in its pass only). A pass readable only
for DATE's stencil bit or the fog's local read (every SCENE pass where the profile has them) is split:
it is begun again readable, and the next DATE draw takes the bit again in it
(`port/render/rd_replay_tiler.c` (`rd__replay_date_take`)). Its passes are not
passes of the loss bisect's window. The picture is the same byte for byte
(`rd_replay_tool_pass_split`) (`port/render/rd_replay_vu.c` (`passSplit`); `port/render/rd_core.c`
(`rd_init`)).

### The stream bounds audit

`rd_stream_audit_test` walks every VU draw of the dumps in `tools/native_frame_dumps.txt` and the
boot corpus's title frame and computes on the CPU every vertex stream index the VU vertex shaders
can read for it, the way the shaders compute it (`vu_vertex_qw`, `vu_batch_qw`): the triangle's
three vertices (the clip plane's distance reads one of them), a grid's batch colour, a particle's
header and two qwords. The shipped shaders scan no strip: the card test's verdicts are bits the CPU
puts in VuBoneCB (`vu_strip_card`, its index clamped to qword 239). The audit also checks a strip
test's scans on the stream's own strip flags and its walk over the strip, taken to run for every
kick (`stripBounds`): reads the shipped shaders do not make, a superset that keeps the audit
conservative. Each index is checked against the draw's own stream,
the qwords the replay uploads for it (`doVuDraw`); a kick below 2 and a draw's index range past its
mesh's list fail too. VuCB's tick cameras and VU memory and VuBoneCB are fixed-size blocks the
indices cannot leave (static asserts; `vu_bone_mat` and `vu_strip_card` clamp). Reported, not
failed: the skinned draws' weight addresses (lowest and highest bone) and the draws whose bones fill
VuBoneCB, which leaves no room for the card bits ([VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-card-test)). Each triangle's
mark read (`vu_kick_marks`, one qword of the mark bytes after the vertices) is checked the same way.
The record stages read a triangle record for every kick from a draw's lowest to its highest, the
kicks its list does not draw included: the audit reads those kicks' marks and vertices too, and
the record's own texels are in range by construction (a command's slots are counted before the
pre-passes draw: `rd_replay_vu.c` (`vuPrepassRun`); [The vertex and triangle
pre-passes](PIPELINES.md#the-vertex-and-triangle-pre-passes)).
The audit also reports, per frame, the index span (the lowest and highest value of every indexed VU
draw, the widest, the draws past 2^24 - 1 by list, the values carrying the old mark bits), and fails
a span past 2^24 - 1 (Vulkan's least `maxDrawIndexedIndexValue`) and a kick listed twice in a draw
with different marks; the CPU mirror (the programs' references in `port/render/vu1_ref` run on each
prelit, lit and skinned batch at the draw's own VU memory and bones: clip w not finite or within
1e-6 of 0, GS X/Y past 16 bits, the scissor fans, a NaN clip w or bone failed, a NaN in VU memory
counted); and the textures list 0 binds (id, size, format, mip levels, whether the texels are
there). On the reporter's lost frame of issue 95 the widest span is 14,314 (the title scenery,
`st26a_p2`). The game measures the same at run time on one replay in 64 (`rd_core.c` `rd__span_draw`,
from each indexed VU draw's CPU index list): the first measured frame logs `rd: VU index span: frame
N (the first measured, one replay in 64 from here): the widest W over D indexed VU draws, K past
2^24 - 1; the device takes values up to M` (expected about 14,314 and 0), a later frame with a draw
past 2^24 - 1 logs its own line (the first 4), and a device loss logs `window: at the loss: VU
index span: the widest W (frame N) over F measured frames ..., K indexed VU draws past 2^24 - 1 of
D in G frames; ...` (`rd_index_span_text`), so a regression of the dense indices shows in a
player's log. `ICO_AUDIT_SPANS=1` lists each draw past the bound. A stream one vertex short fails it, in a
synthetic draw and on the first real mesh draw of the dumps (`port/render/test/rd_stream_audit_test.c`,
header comment). On the 27 listed dumps present on the maintainer's machine and the title frame
(2,964 VU draws, 115 of them the title's; 2026-10-15) no read leaves its stream.

## Pipelines

The pipeline key, the blend paths, the start-up set, the fallback entries and the vertex and triangle pre-passes have their own page: [PIPELINES.md](PIPELINES.md).

## Destination reads and blend paths

The GS blends in integers with an arithmetic shift, no clamp of C, then COLCLAMP, PABE and FBA;
the GPU's fixed-function blender computes in float and rounds to nearest, one level above the GS
on about half the channels of a LERP ([DIVERGENCES.md](DIVERGENCES.md) VD-7). The destination
read computes the GS result in the fragment shader from the pixel's own destination, read in
rasterization order, with blending off (`gs_dst_channel`, `port/shaders/gs_math.hlsli`)
(`port/render/rd_dst_read.c`). Two realisations (`port/rhi/rhi.h` (`RhiDstRead`);
`port/render/rd_dst_read.c`):

- **attachment**: the colour attachment read as an input attachment in rasterization order
  (Vulkan rasterization-order attachment access over dynamic rendering local read). Depth,
  stencil, discard and write mask stay fixed-function. Every pass on a readable RGBA8 target is
  begun readable.
- **storage**: the target as a rasterizer-ordered view (D3D12 ROV; Vulkan storage image under
  fragment shader interlock). The target is then no colour attachment, so a blended draw runs in a
  storage pass of its own with the depth attached. A draw that writes Z and can discard is split
  into colour without Z write then a depth-only pass when its Z test is ALWAYS; under
  GEQUAL/GREATER it stays on the fixed-function blend (logged once).

**The exact blend is an opt-in.** The fixed-function blend is the default on every device and
backend. Only `[dev] blend` / `ICO_RD_BLEND` ([CONFIG.md](CONFIG.md)) set to `attachment` or
`storage` (also `interlock`, `rov`) opts into a destination read, and only where the device has
it; `exact` takes the device's best (the attachment read, else the storage read) and logs
`rd: blend: [dev] blend = exact: the exact blend is an opt-in; attachment tried`; `fixed` (also
`off`, `0`) pins the default. The ask is read before the shader modules exist
(`port/render/rd_dst_read.c` (`rd__dst_asked_env`)), so `rd__gpu_init` makes the group-3 layout
and the `_dstrov` / `_dstatt` twins of the asked mode alone (`port/render/rd_replay.c`
(`rd__gpu_init`)), `g_rd.dstModes` holds at most the fixed-function blend and that mode
(`madeModes`), no RGBA8 target is made with `RHI_TEX_STORAGE` or `RHI_TEX_INPUT_ATTACHMENT` for the
blend unless asked (`rd__dst_target_usage`, `RdTargetRec.dstUsage` bits `RD_DST_USAGE_STORAGE` and
`RD_DST_USAGE_ATTACHMENT`; `RD_DST_USAGE_LOCAL_DATE` is DATE's local read, not the blend's), and
the start-up pipeline set has no destination-read key: `rd__enumerate_reachable` takes its
`enumerateAll` path when the mode is the fixed-function blend and the dual-source blend is in use
(`port/render/rd_pipeline.c`), and `rd__enumerate_dst` returns at once for it. The start line is
`rd: blend: fixed-function (the default; [dev] blend = attachment|storage opts into an exact
mode)`, `rd: blend: fixed-function ([dev] blend = fixed)` or `rd: blend: exact (attachment read)
([dev] blend)`; a word that is none of these or a mode the device lacks logs `rd: blend:
ICO_RD_BLEND=<v> is not fixed, attachment or storage; fixed-function` or `... is not on this
device; fixed-function` and keeps the default (`rd__dst_init`).

Every place that branches on the mode stays and is inert at the fixed-function blend: the pass's
mode (`dstModeFor`) is the fixed-function blend, `expandPassesMode` and `rd__expand_dst` copy the
passes, `dstPassMode` begins no storage pass (the `window: per replay` line's destination-read
draws, storage passes and pass switches read 0), `rd__pass_wants_readable` is false, the MSAA
epoch's exclusion and the fog drawn in it change nothing (`port/render/rd_replay.c`), and the
restart steps that take the fixed-function blend after a crash or a loss are skipped because the
run already drew with it (`port/platform/startup_retry.h`). A nonzero exact-blend field in
`window: per replay` means the player opted in.

What the exact blend draws when opted into: the screen prims, the VU meshes and the fog sprite
whose fixed-function blend is not the GS's (the LERPs, mode 3, the Ad forms, mode 11, PABE on the
premultiplied forms, COLCLAMP 0) on RGBA8 targets. What stays on the fixed-function blend: the
premultiplied forms, the MSAA epoch's passes, targets other than RGBA8, the port's presentation
overlay. Of the premultiplied forms the adds (modes 0 and 5) are exact on the blender (the shader
writes the GS term `(Cs F) >> 7` and the blender adds two whole levels); the subtracts (modes 1
and 6) are one level above the GS wherever `Cs F` is not a multiple of 128, because the GS's term
is `(0 - Cs) F >> 7`, an arithmetic shift that rounds the negative product down, while the blender
subtracts the truncated `(Cs F) >> 7` (`port/shaders/common.hlsli` (`gs_dual_out`);
`port/shaders/gs_math.hlsli` (`gs_blend_ch`)). The staticBlur sprites and `blend_int` keep their
own exact paths (`port/render/rd_dst_read.c`; `port/render/rd_replay.c` (`dstPassMode`,
`rd__pass_wants_readable`)).

Why the opt-in: the storage read ends and begins a pass at every switch between a shader-blended
and a hardware-blended draw (`dstPassMode`): 887 passes a replay on an RTX 3090 against 53 to 75
with the fixed-function blend, 432 against 40 on a GTX 1650, and on a contributor's M1 (MoltenVK,
interlock) 54 ms and 750 passes a replay against 14 ms and 49 with `ICO_RD_BLEND=fixed` (issue
102), for the last level of rounding on the LERP channels. Between the attachment read and the
fixed-function blend (`rd_replay_tool --pass-dump` of a corpus frame at Original 4:3 1x, each
way), about 95 % of SCENE's pixels differ, by one level on most, two on about one in eight, at
most 5 or 6 where several blend steps add up; WORK0 to WORK3 and the shadow levels are identical.

**The attachment read on Qualcomm's driver.** The attachment read is not offered on Qualcomm's own
Adreno driver (vendor 0x5143 with `VK_DRIVER_ID_QUALCOMM_PROPRIETARY`, or with the id unknown;
Mesa's Turnip on the same GPUs reports its own id and keeps it). On the Odin 3 (Adreno 830, driver
512.800.72, issue 89) it returned wrong destination colours: white boxes around the leaves' clear
texels, the title washed out, the menu panels white on white; the same title frame replays
through the same path on lavapipe with no validation error and within one level of
`ICO_RD_BLEND=fixed`, so the driver is wrong, not the port's use. The device line says so,
`rhi_vk: destination read: interlock no, rasterization-order attachment read off on this driver
(Qualcomm driver 512.800.72: wrong results on Adreno; ICO_VK_ATTACHMENT_READ=1 tries it); local
read yes`, so `[dev] blend = attachment` is refused there (`is not on this device`).
`ICO_VK_ATTACHMENT_READ=1` (`[dev] attachment_read = true`) offers the read anyway;
`ICO_VK_FAKE_ADRENO=1` takes the branch on any device, for tests (`port/rhi/vk/vk_device.c`;
`port/rhi/rhi_driver_version.h` (`rhi_driver_is_qualcomm_proprietary`); `port/rhi/rhi.h`
(`RhiLimits.rasterOrderOff`)). On MoltenVK (`RhiLimits.moltenvk`, `; MoltenVK` on the `rhi_vk:`
device line) the default is the same fixed-function blend as everywhere; an opt-in there behaves
as on any other device. `ICO_VK_FAKE_LIMITS=moltenvk` reports MoltenVK, with no attachment read, on
any device (`rd_blend_selftest_moltenvk_storage`).

**The blend self-test.** A driver whose read or dual-source blend returns the wrong colour draws a
wrong picture with no error anywhere, so `rd_blend_selftest` checks the blend in force on the
device at start, on every device, as the fog's self-test checks its depth reads ([The fog path and
its self-test](#the-fog-path-and-its-self-test)). A frame of its own (never recorded or
presented) is drawn on SCENE at its real size and scale, MSAA held off, in a 256 x 96 GS pixel
block in its middle: eight cells of PCSX2's forest pixels under the forest's tint sprite
(`rd_pixel_test.c` `kTint`), a textured LERP quad whose clear texel must leave the colour behind it
(the leaf case), two overlapping LERP draws (the second must blend over the first's result) and a
glyph through the text sheet's shader, then an opaque cell read at an even and an odd texel column
(a dual-source equation kept with blending off, [RHI.md](RHI.md#dynamic-state)). 21 samples are
read back (one readback; its pipelines are keys of the start-up set). The fixed-function
blend runs first, the reference, within 2 levels of the GS arithmetic; if it fails, the two-pass
fallback (`rd_set_no_dual`) is tried and kept when it passes. Then, only when `[dev] blend` opted
into an exact mode the device has (`rd__dst_asked`), that mode runs, within 1 level of the GS
arithmetic and of the reference's glyph, and is kept whatever its result. The window runs it
before the start-up pipeline set and again whenever SCENE is made at a new size. One line names the
blend in force and every result:

```
rd: blend: fixed-function (self-test: fixed-function ok 1; exact modes not asked for: [dev] blend = attachment|storage tries one) on SCENE 640x448 at 1920x1344 texels in T ms
rd: blend: exact (attachment read) (self-test: attachment ok 1, fixed-function ok 1; kept by [dev] blend) on SCENE 640x448 at 1920x1344 texels in T ms
rd: blend: fixed-function (self-test: fixed-function fail N -> two-pass ok 1; exact modes not asked for: [dev] blend = attachment|storage tries one) on SCENE ...
```

and the `ico_pc: summary:` line carries `blend fixed-function (self-test: fixed-function ok 1)`.
A failing entry's worst sample, and a passing one's when it is not exact, gets a line of its own,
`rd: blend: self-test: attachment fail 241 at tint cell 0 (texel 625,625): 255,255,255,4 want
21,28,14,4` (read white by `ICO_RD_BLEND_SABOTAGE=attachment`). `ICO_RD_BLEND_SABOTAGE=`
`attachment,storage,fixed,twopass` makes those entries' samples read white, for the fallback's
tests; a run that took the two-pass fallback starts from the dual-source blend again at the next
SCENE size, so each line is whole (`port/render/rd_blend_selftest.c`; `port/render/rd.h`
(`rd_blend_selftest`); `port/platform/window_host.c`).

### The storage pass merge

Under the storage read a destination-read draw needs a pass without the colour attachment, and any
other draw a pass with it, so a frame that interleaves them ends and begins a pass (with its
barrier) at every change: on an RTX 3090 the title ran 887 passes a replay against 53 to 75 on the
attachment and fixed paths, and the GPU time of the logs held follows the pass count (issue 103).
The merge (`g_rd.passMerge`, off by default) keeps an open storage pass open: a draw that
`rd__expand_dst` would leave on the fixed-function blend is drawn in it through the
destination-read shader instead when the shader stores the same bytes (`rd__dst_merge_ok`):

- no GS blending: the shader's ALPHA register is then `0x00`, `((Cs - Cs) As >> 7) + Cs`, which is
  Cs (`RdDrawPass.csOnly`, `fillDrawCB`);
- the premultiplied add, modes 0 and 5 (exact on the blender, above);
- an RGBA8 target, an entry with the destination-read twins, a mask writing RGB, no stencil,
  single-sampled;
- not a draw that writes Z and can discard: the storage pass tests Z before the shader, so such a
  draw keeps the colour/depth-only split or the fixed-function blend as before.

The premultiplied subtracts stay on the fixed-function blend, which rounds them differently
(above): through the shader they would be the GS's bytes, not the current picture's. A draw that
does not qualify still begins a pass with the colour attached, and the next destination-read draw
a storage pass again. The merge changes only the storage mode's passes, so the attachment read and
the fixed-function blend draw as before. `ICO_RD_PASS_MERGE=1` (`[dev] storage_pass_merge = true`)
turns it on and logs `rd: storage pass merge: on (ICO_RD_PASS_MERGE=1)`; the start-up pipeline
set has the merged keys while it is on (`rd__enumerate_dst`). It is off by default until the A/B
on an RTX 3090 and the shot list have been run with it: the only logs with it on (an RTX 3090,
interlock) count 645 to 1189 pass switches a replay (`window: per replay` lines).
lavapipe has no fragment shader interlock, so the tests that compare the merge's picture with the unmerged one byte for byte
(`rd_replay_tool_pass_merge`, `rd_perf`) skip that part there and run on a device with the
storage read; the release check is the shot list on an interlock card with and without the key
(`port/render/rd_dst_read.c` (`rd__dst_merge_ok`, `rd__dst_merge`); `port/render/rd_replay.c`
(`expandPasses`, `dstPassMode`)).

## The device profile

One renderer with device-class profiles: after `rd__gpu_init`, `rd__profile_init` decides
`g_rd.profile` (`RdProfile`, `port/render/rd_internal.h`) once from `RhiLimits` (`tiler`,
`localRead`, `vendorId`, `moltenvk`, `uintView`, `driverText`) and the backend, and every
vendor-dependent choice of the renderer reads one of its flags; no other file of the renderer
branches on a vendor (`port/render/rd_profile.c`). The classes: D3D12 (the backend), Apple /
MoltenVK (`RhiLimits.moltenvk`), Adreno on Qualcomm's driver and Adreno on Turnip (vendor 0x5143;
Turnip names itself in the driver text), Mali (0x13B5), PowerVR (0x1010), any other tile-based GPU
(`RhiLimits.tiler`: Samsung, Broadcom, every Android GPU) on the PowerVR row, and the desktop
immediate-mode GPUs (NVIDIA, AMD, Intel, lavapipe) on Vulkan. OpenGL and Direct3D 11:
[BACKENDS.md](BACKENDS.md#the-device-profile-on-opengl-and-direct3d-11).

| flag | desktop Vulkan | D3D12 | Adreno (Qualcomm's driver) | Adreno (Turnip) | Mali | PowerVR, other tile-based | Apple / MoltenVK |
| --- | --- | --- | --- | --- | --- | --- | --- |
| blend | fixed-function; `[dev] blend` opts into an exact mode | fixed-function; `[dev] blend = storage` opts into the ROV | fixed-function | fixed-function | fixed-function | fixed-function | fixed-function |
| `fogLocal` | on (probe) | off | on where local read is offered | same | same | same | same |
| `dateStencil` | on (probe) | off | on where local read is offered | same | same | same | same |
| `storePlan` | on | on | on | on | on | on | on |
| `blurFold` | on | on | on | on | off (the G52 is bound by its ALU) | on | on |
| robust access | off | its own bounds rules | off | off | off | off | off |
| `localRead` | as offered | none | as offered | as offered | as offered | as offered | as offered |
| `uintViewWork`, `uintViewScene` | on where the backend has the view | off (`RhiLimits.uintView` false on D3D12) | off | off | off | off | off |
| `carefulSet` | off | off | off | off | off | off | off |
| the record path (the triangle pre-pass, `[dev] vu_prepass`) | on | on | off (the pictures between two ticks shook on a PowerVR phone; `[dev] vu_prepass = true` turns it on) | off | off | off | off |
| dynamic state (`[dev] dynamic_state`) | on where offered | none | off (the same phone; `[dev] dynamic_state = true` turns it on) | off | off | off | off |
| `alphaPlan` (`[dev] alpha_plan`) | on | on | on | on | on | on | on |
| `fxPlan` (`[dev] fx_plan`) | on | on | on | on | on | on | on |
| `fxDstLocal` (`[dev] fx_dst_local`) | on (probe) | off | on where local read is offered | same | same | same | same |
| `vuPlanesFrac` (`[dev] vu_planes`) | on | on | on | on | on | on | on |
| `crackFill` (`[dev] crack_fill`) | on | on | on | on | on | on | on |
| `shadowOne` (`[dev] shadow_one`) | on | off | on | on | on | on | on |
| `shadowBspline` (`[dev] shadow_bspline`) | off | off | off | off | off | off | off |

The blend row is `rd_dst_read.c`'s ([Destination reads and blend
paths](#destination-reads-and-blend-paths)), robust access the Vulkan backend's
(`port/rhi/vk/vk_device.c`; [RHI.md](RHI.md#device-loss)); the profile reports both. The other
flags are decisions: `fogLocal` offers the fog's `local` path, tried first by its self-test,
`dateStencil` DATE through the stencil bit, decided by its self-test, `blurFold` the folded
staticBlur pairs ([The replay](#the-replay)); `storePlan` [the store plan](WORK_BUFFERS.md#the-store-plan). `alphaPlan` offers the alpha/blend
planner ([The alpha plan](#pipelines)), `fxPlan` the frame-size post sprites drawn without their snapshot
([Post passes](#post-passes)), `fxDstLocal` the post sprites that read their destination by the local read
inside the open pass (it needs `fx_local_ps` and is decided
by its self-test), and `vuPlanesFrac` the 3D pixel shaders without the attribute planes on a target at a
fractional scale ([Screen planes](#screen-planes)). A target is made with
`RHI_TEX_UINT_VIEW` by class when `uintViewWork` (the work buffers, `rd__work_class`) or
`uintViewScene` (the scene-class targets) is on, a scene-class one with
`RHI_TEX_INPUT_ATTACHMENT` and `RD_DST_USAGE_LOCAL_DATE` when `dateStencil` is on and the DATE
stencil shader is in the table (`rd__dst_target_usage`, narrowed to the scene class), and a
scene-class depth with `RHI_TEX_INPUT_ATTACHMENT` and `RD_DST_USAGE_LOCAL_DEPTH` when `fogLocal`
is on and `fog_lut_local_ps` is in the table (`port/render/rd_core.c` (`rd__target_create_gpu`)).
`uintView*` are off wherever `RhiLimits.uintView` is false, and `fogLocal` and `dateStencil`
wherever `RhiLimits.localRead` is. The local read is enabled through
`VK_KHR_dynamic_rendering_local_read`; the Vulkan 1.4 core feature is queried but the instance's
version is capped at 1.3, so a 1.4 driver that lists only the core feature has no local read
(`port/rhi/vk/vk_device.c`).

Each flag has a `[dev]` key, exported by the host ([CONFIG.md](CONFIG.md)): `fog_path = "local"`
(`ICO_RD_FOG_PATH=local`: `fogLocal` on; `inplace`, `copy` or `buffer`: off), `date_path =
"stencil"|"snapshot"` (`ICO_RD_DATE_PATH`), `store_plan` (`ICO_RD_STORE_PLAN=1|0`), `blur_fold`
(`ICO_RD_BLUR_FOLD=1|0`), `uint_view` (`ICO_RD_UINT_VIEW=1|work|0`: both views, the work buffers'
only, none), `alpha_plan` (`ICO_RD_ALPHA_PLAN=1|0`), `fx_plan` (`ICO_RD_FX_PLAN=1|0`), `fx_dst_local`
(`ICO_RD_FX_DST_LOCAL=1|0`), `vu_planes` (`ICO_RD_VU_PLANES=1|0`: 1 is `vuPlanesFrac` on, the planes dropped at a
fractional scale), `gpu_robust` (`ICO_GPU_ROBUST`). A flag a key set is marked `([dev])`; a key that
asks for what the device lacks leaves the flag off and logs `rd: profile: [dev] fog_path = local
(no local read): not on this device, the flag stays off` (`fx_dst_local` asked for without local read logs the same
line with `[dev] fx_dst_local (no local read)`). A self-test that finds its mechanism
wrong turns the flag off (`rd__profile_self_test_off`): the flag is marked `(self-test)` and the
line `rd: profile: <flag> off (self-test)` is logged. The start line, once a run (abridged):

```
rd: profile: tile-based GPU (Imagination, Vulkan): blend fixed-function; fog local on; DATE stencil; depth stores planned; blur fold on; robust access off (the default; gpu_robust = true turns it on); local read yes; uint views off; alpha plan on; fx plan on; fx local read on; VU planes whole scales only; pipelines the start-up set
```

On desktop Vulkan with local read, "(probe)" marks the three local-read flags where the start-up cost probe
found them slower, and both lines end with its words: [WORK_BUFFERS.md](WORK_BUFFERS.md#the-desktops-local-read-and-its-probe).

and the `ico_pc: summary:` line (abridged) carries the flags as in force after the blend's text
(`rd__profile_text`; `port/render/rd_core.c` (`rd_summary_text`)): `profile tile-based GPU
(Imagination, Vulkan): fog local on, DATE stencil, depth stores planned, blur fold on, local read
yes, uint views off, alpha plan on, fx plan on, fx local on, VU planes whole scales only`. The four planner flags read
`on`/`off` and `whole scales only`/`every scale` (`vuPlanesFrac` off); a flag a key or a self-test changed is marked as the others are. The Vulkan version is not in the class name (`RhiLimits` does not carry
it); the `rhi_vk:` device line has it.

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
own usage for that. The swap is no pass of the [pass ring](#the-pass-ring) (a copy is);
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
border) leaves SCENE as the game drew it, the PS2's picture, which the native-frame gate's 4:3
hashes hold. The strips stay with it: the
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
| cleanUpFB's two sprites into SCENE, the glow's WORK0 fill | untextured, blending, no DATE, no AFAIL RGB_ONLY, and each colour channel's blend the destination for every Cd and Ad or one value for every Cd and Ad (`rd__fx_fold`, over the 256 values of each) | `fx_sprite_ps` without the blend and the destination read, the constant channels as RGBAQ, the destination channels masked (the colour mask A alone or whole), the depth state the sprite's |
| the glow's paste WORK3 into SCENE | Cs * FIX + Cd, Cs * As + Cd, Cd - Cs * FIX or Cd - Cs * As, COLCLAMP 1, no PABE, no DATE, no AFAIL RGB_ONLY | `fx_frame_ps` writing the magnitude of `(A - B) C >> 7` (`FXF_OUT_PREMUL`) through the premultiplied add (ONE, ONE) or reverse subtract, alpha ONE, ZERO: the UNORM8 sum is the GS's clamped integer sum, the subtract's term the GS's `ceil(Cs C / 128)` |
| the motion blur DISPLAY into SCENE, any other destination read | `RdProfile.fxDstLocal` and the target's passes readable (the scene class under `dateStencil`), not sampling its own target | `fx_local_ps`, the destination as the input attachment (`SubpassLoad`) inside the open pass after a by-region barrier (`RHI_LOCAL_READ_COLOR`; `RHI_LOCAL_READ_BACK` after it); `fx_rect_vs` draws one triangle, so the texel read is the one before the sprite |
| the glow's mask SCENE into WORK2 | MODULATE with RGBAQ's RGB 0, no blend, no destination read | `fx_frame_ps` computing the alpha alone (`FXF_ALPHA_ONLY`): RGB is `(t * 0) >> 7`, 0 |

A sprite that samples its own target keeps the snapshot; so does every sprite that reads its
destination under an exact blend the player opted into or in a frame with the MSAA epoch, and one
whose key the pipeline cache cannot give (looked up before anything is recorded). The four forms
share `fx_sprite_ps`'s coverage, UV and tests (`port/shaders/fx_sprite.hlsl`). Their keys are in the
start-up set (`rd__fx_enumerate`: the colour mask A of `fx_sprite_ps`, `fx_frame_ps` without blend
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
rd: fx local read: on (self-test: local ok 4, snapshot ok 4) on SCENE 512x512 at 1024x1024 texels
```

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
comment; `port/render/rd_replay.c` (`doScreen`) asks `rd__fades_fold_skip`).

The plan (`rd__fades_fold_plan`, the frame read once before the walk from the store plan's scan,
`rd__store_plan_scan`; the store plan does not count a folded sprite as a touch of the depth) takes
the run of such sprites at the end of list 11, after the last action that is not one: each one
sprite into SCENE, untextured, ABE with a lerp (ALPHA 0x44 or 0x64; PABE with As's MSB clear writes
Cs: factor 0x80), the factor at most 0x80 (the result stays in 0 to 255), the colour mask whole, no
alpha test that can fail, no DATE, no AA1, no Z test, the scissor the whole target, every column
covered and every row or the rows from the top or to the bottom (a bar); two bars of one colour,
factor and alpha are one step. The deferred text's quads (drawn on the output), the overlay text
and NOPs between are passed over. A sprite that writes Z (the bars) folds only while the present
does not read SCENE's depth (the effects depth) and no replay found SCENE's depth read before the
frame clears it (a keep frame on the depth the frame before left; logged once, the bars drawn from
then on); such a sprite is otherwise drawn and ends the run. List 12 must hold nothing but state,
the overlay and deferred text, the reduction's black sprite into DISPLAY and its textured sprite
reading SCENE's colour (no blend, no DATE, bilinear, its tint not black), and nothing after it, so
the reduction is SCENE's only reader after the sprites (the border's column repeat copies whole
columns before it, which the steps, the same on every column, commute with). No fold under a pass
window, in a frame with MSAA (the epoch's resolve is not the lerp's), past four steps, or above 1x
for a bar while the reduction resamples SCENE through the above-1x kernels.

In the reduction:

- at scale 1 (Original; `fx_sprite_ps` with `FXF_FOLD`, `port/render/rd_replay.c`
  (`doBlurSpriteDraw`), `rd__fades_fold_fx`) the steps are applied to each of the bilinear's four
  taps before the GS filter (`port/shaders/fx_sprite.hlsl` (`fx_fold4`),
  `port/shaders/gs_math.hlsli` (`gs_fold_ch`)): every tap is the texel SCENE would have held.
  Under an exact blend (`[dev] blend = attachment|storage`) the sprites store the GS's floor and
  so does the step: DISPLAY and the present byte for byte. Under the fixed-function blend (the
  default) the sprites store the nearest level (VD-7); the step adds 64 before its shift, the
  nearest level too, but for the blender's own ties and precision: at most a level
  ([DIVERGENCES.md](DIVERGENCES.md) VD-27);
- above 1x (the hardware sprite, `rd__blur_screen_fallback`; `sprite_ps` with `ICO_DF_FOLD`,
  `port/render/rd_pixel_plan.c` (`rd__fill_draw_cb_extra`), `rd__fades_fold_screen`) the steps
  follow the filter on the sampled texel (`port/shaders/sprite.hlsl` (`sprite_fold`)): a step is
  affine in the texel and the weights sum to 1, so this is the filter of the stepped texels but
  for their rounding to 8 bits, a level at most. Where the filter's two rows straddle a bar's edge
  each row is sampled alone, stepped by its own steps, and the two weighed as the filter weighs
  them. The bars' rows are SCENE's texel rows the bars covered, their corners snapped to whole GS
  pixels as `snapAxis` draws them (`rd_fx_plan.c` `fxAxisCovers`'s mapping);
- DISPLAY's alpha is the last step's, what SCENE's alpha would have been.

SCENE then holds the picture without the sprites (its pass dump differs by design,
[WORK_BUFFERS.md](WORK_BUFFERS.md#the-store-plan)); DISPLAY, the present and the next frames read
DISPLAY. A reduction that does not take a planned fold (a way the plan does not foresee) leaves the
frame without the sprites; it is logged and the fold is off from then on. With the menus up (the
pause menu's layout drawn after the fade) or film noise, the keep sprite or text drawn into SCENE
after the sprites, nothing folds. The saving on a tiler is the list 11 pass's load and store of
SCENE's colour and depth only when list 11 then has no other draw; elsewhere one full-frame draw.
`[dev] fades_fold = false` (`ICO_RD_FADES_FOLD=0`) draws the sprites as before. Each replay's
record counts the sprites folded (`RdPerfRecord.fadesFolded`; `rd_replay_tool --stats`: `fades fold
on: N sprites folded into the reduction`; the window's 10 s `pixel diet` line: `fades fold N`), the
summary line says `fades fold on` or `off`, and the log says:

```
rd: fades fold: 3 of list 11's last sprites applied by the reduction instead of drawn into SCENE (frame 720; the whole frame (0, 0, 0) at 127/128, bars (0, 0, 0) at 64/128; the steps on SCENE's texels before the GS filter; SCENE 512x448 texels, rounding the nearest level (the fixed-function blend)); [dev] fades_fold = false draws them (reported once)
rd: fades fold: list 11's last sprites are drawn into SCENE: a sprite writes SCENE's depth, which the effects depth reads (frame 720; the first 8, then every 100th: 1 so far)
rd: fades fold: SCENE's depth is read in list 11 before this replay clears it (frame 1234): the letterbox bars, which write it, are drawn into SCENE from now on (reported once)
rd: fades fold: the reduction took another way (the hardware sprite, not as planned): frame 1234's picture lacks list 11's last 3 sprites; the fold is off from now on
```

ctest `rd_fades_fold` (the step against the GS blend, the plan on recorded frames) and
`rd_fades_fold_ab` (the fade dumps with and without the fold, [TESTING.md](TESTING.md)). Not built:
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
frame takes the GS's own filter: [The glow size](#the-glow-size)) and an axis leaves the GS's
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
levels' filter of the Original preset (`rd__gs_filter_int`: [Textures](#textures)) is for images
only, so it never meets the flag (`port/render/rd_pipeline.c` (`rd__gs_filter_int`)). The flag is never set when the
two scales are equal, nor on the reduction at the GS's filter, so never at scale 1;
`rd_present_test`'s full height 1x case checks no draw is flagged there and that the frame is byte
for byte the one with the reduction flagged (`rd__set_a1x_gs_keep` off), whose shader takes the GS's
path at every pixel (`port/render/test/rd_present_test.c` (`checkFullHeight1x`)).
`g_rd.above1xDraws` counts these passes too, and each replay's record their number
(`RdPerfRecord.a1xDraws`, [Performance records](#performance-records)).

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
frame take the GS's filter ([The glow size](#the-glow-size)). The reduction SCENE to DISPLAY is never above 2:1 in texels
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
it alone, [The replay](#the-replay)), 7 unused. A count's INCR_WRAP or DECR_WRAP through the write
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
[The replay](#the-replay)).
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
| local | inside the scene pass, the pass's own depth as an input attachment (Vulkan local read; offered where the profile has `fogLocal`; [The replay](#the-replay)) |
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
([Camera blending](#camera-blending)) (`port/render/rd_fog_path.c` (`rd__fog_probe_record`)).
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
every channel exactly. Both replays run with MSAA held off (`g_rd.msaaBlocked`): the stencil bit
is never taken inside the MSAA epoch (`port/render/rd_replay.c` (`dateStencilFor`)), so it is
checked as it draws whatever the anti-aliasing setting, and the result holds when MSAA is turned
off later. The stencil replay must have taken the bit (`g_rdPerf.dateStencilTakes` above 0), else
it reports `stencil not run (no draw took the stencil bit)`, a failure. The stencil bit stays only
with its four right, else `dateStencil` goes off (`rd: profile: DATE stencil off (self-test)`) and
the snapshot draws DATE; `ICO_RD_DATE_PATH=stencil` keeps it whatever it finds. One line: `rd:
DATE: stencil path (self-test: stencil ok 4, snapshot ok 4) on SCENE ...`, or `rd: DATE: snapshot
path (self-test: stencil fail 2 at sample 1: 28C85000 for FAFA0A40, snapshot ok 4) ...`.
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

Under the Enhanced preset, with SCENE's scale above 1 on either axis, `doFog` calls
`rd__crack_fill` for SCENE's own fog after the pass before it ends (the local read's scene pass
too) and before the fog reads the depth
(`port/render/rd_crack_fill.c`). It copies SCENE's colour into its snapshot texture
(`rd__replay_take_snap`) and SCENE's depth into the fog's depth copy object
(`rd__depth_copy_take`, the copy path's own texture, made on the other paths), and draws one
triangle over SCENE with its depth attached: `crack_fill_ps` (`port/shaders/blit.hlsl`, behind
`blit_vs`, key `rd__crack_fill_key(0)`: test ALWAYS with writes, no blend, the stencil loaded and
stored untouched) gives a crack the colour and the depth (SV_Depth) of the nearer end of its run.
A texel is a crack when a run of at most R texels along its row or its column, the texel among
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
are exact copies, nothing filtered; the depth goes with the colour, so the fog paints the
neighbour's fog level there and the draws after the fog see a surface. The texels the first pass
drew are not touched, so the picture differs from the one without the filler only at the cracks.

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
default where the device profile has `crackFill`: every class, tile-based GPUs included. Its two
keys join the fog's family in the start-up set (`rd__enumerate_reachable_fog`). Cost above 1x: a
copy of SCENE's colour and of its depth (12 bytes a texel copied, 99 MB at 3840 x 2160) and one
pass reading thirteen depth texels per texel (the runs' ends, both axes) and four more where a
drawn texel lies behind both ends of a run (the texels beyond them); the depth copy texture (8 bytes a texel) is the copy path's own and is made on the other paths.

On the local path the fill is drawn inside the scene pass the fog's local read keeps ([the fold](WORK_BUFFERS.md#the-crack-fill-inside-the-local-fogs-pass)).

The count, only under `rd__crack_fill_count_all` (the tests, the replay tool's `--stats`; never in
the game, and a missing count pipeline leaves the fill uncounted): `crack_count_ps` runs the
same test over the depth copy, 16 x 16 texels a texel of a small RGBA8 target (n & 255 in R, n >> 8
in G), which is copied into a readback buffer and summed once the GPU is past that replay
(RHI_FRAMES_IN_FLIGHT replays later, as the fog's edge probe; `rd__crack_fill_wait` waits for it).
`RdPerfRecord.crackFillPasses` and `crackCountPasses` count the passes of a replay. The lines:

- `rd: crack fill: on above 1x (scale 2x2, reach 1x1 texels: a run of texels no draw covered, or
  drawn over 1/64 of the depth behind both its ends, with a texel in front at each end takes the
  nearer end); [dev] crack_fill = false turns it off` (with ` (drawn inside the scene pass with the
  local fog)` after it once a fill was drawn so), or `... on above 1x, not drawn at scale 1x1 (the GS's own grid); ...`, `... on above 1x,
  not drawn under the Original preset; ...`, `rd: crack fill: off ([dev] crack_fill = false,
  ICO_RD_CRACK_FILL=0)`: at the start and whenever SCENE is made
  again or the state changes (one line for each change);
- `rd: crack fill: T replays filled (scale SXxSY; reach RXxRY texels; not counted in the game[; F inside the scene pass with the local fog])`
  in the 10 s summary (`rd__crack_fill_block`, `rd_interp.c` `presentLog`), when it filled; when
  counting, `rd: crack fill: N pixels filled over M replays counted of T filled (max K a replay;
  scale SXxSY; reach RXxRY texels)`;
- `rd: crack fill: not drawn on SCENE at scale SXxSY: <why> (reported once)`, once a run for each
  reason (MSAA, no sampleable depth, its pipelines missing, no memory for the copies) and
  `rd: crack fill: the pass after the copies was not begun with the depth readable: ...` (the fold
  refused, once), and for the
  last three `rd: crack fill: N fogged replays above 1x not filled (...)` in the 10 s summary;
- `--stats`: `<dump>: stats: crack fill on|off: P passes, N pixels filled (M replays counted;
  scale SXxSY, reach RXxRY texels; S fogged replays above 1x not filled)`.

`rd_crack_test` (`rd_crack`, `rd_crack_4x`, `rd_crack_frac`, `rd_crack_backdrop`,
`rd_crack_backdrop_4x`, `rd_crack_backdrop_frac`, `rd_crack_1x`, `rd_crack_original`,
`rd_crack_msaa`, `rd_crack_off`, `rd_crack_depth_copy`, `rd_crack_buffer`, `rd_crack_d24s8`,
`rd_crack_nodual`, `rd_crack_tiler`, `rd_crack_tiler_on`) and `rd_replay_tool_crack` check it ([TESTING.md](TESTING.md)).

## Content tables

**Hidden stage faces.** A few stage faces relied on the PS2's 4:3 picture: closing planes that seal
a model where the 4:3 camera never looks. They are listed in `rd__content_hide` by the stream bits
of their corners; `markContentHidden` marks a triangle whose three vertices are black and corners of
one entry with `ICO_VU_MARK_HIDE`, drawn nowhere. Black alone does not identify such a face, and a
model pack's replacement never matches. An entry is listed only when 4:3 dumps with it hidden equal
those without, byte for byte, in Original 1x and Enhanced 2x (`tools/content_table_check.py decide`)
(`port/render/rd_content_hide.c`; `port/render/rd_mesh.c` (`markContentHidden`)). To add an entry:
find the mesh with `rd_replay_tool --list` and `--mesh` on a dump that shows the face, list its
black corners in `rd__content_hide`, and run `tools/content_table_check.py decide`.
`ICO_RD_CONTENT_HIDE` overrides the table's verbs for the check: comma-separated `off`, `hide` or
`table` for every entry, or `N=verb` for entry N; a bad value logs
`rd: ICO_RD_CONTENT_HIDE="..." not understood: the table as listed` and keeps the table
(`port/render/rd_mesh.c` (`rd__content_hide_set`, `rd__content_hide_verb`)).

**Sheets continued to the frame's edges.** A few stage models are sheets of tiles held in front of
the camera to cover the PS2's 4:3 picture, and stop short of the sides of a wider frame: the deja
vision's grain veil (mesh `sandstorm`, four quadrant sheets of four 128 x 112 tiles, each drawn in
two layers). They are listed in `rd__content_extend` by their vertex count, layout and the xxh3 of
their stream, so a model pack's replacement never matches. When a listed mesh is made,
`extendSheet` appends whole tiles in its one batch: the outer column repeated `cols` times outward
across, then the outer row `rows` times outward down, each tile's UV continued from the tile it
repeats so the texture runs on without a seam under REPEAT (`rd__sheet_tiles`). The mesh's own
vertices and triangles come first and are unchanged; the tiles are drawn with the draw's state,
matrices and UV offset, and take the region test as the sheet does. Out of the box (the Original
preset at 4:3 and 1x, `vuExtended`) a draw of a listed sheet stops at the game's own triangles
(`rd__vu_sheet_end`, `RdMeshRec.tiledIndexFrom`), so the appended tiles are never drawn there,
wherever the sheet is placed, and the picture is the PS2's; in every other state they are drawn,
clipped by the GPU at the frame. This is decided per draw, so an Options change needs no new mesh
(`port/render/rd_replay_vu.c` (`doVuDraw`); `port/render/rd_interp.c` (`scratchFor`) carries the
counts to a blended copy). A dump stores a listed sheet as the game made it, its own vertices,
triangles and batch (`port/render/rd_dump.c` (`rd__dump_frame`)), so the replay lays the tiles
again under `ICO_RD_CONTENT_EXTEND` as it is set, and `off` removes them. A morph
(`rd_update_vu_mesh`) writes the sheet's own vertices and lays the tiles again (`retileSheet`).
The veil's sheets reach GS X 2048 -+ 702 and Y 2048 -+ 717; three tiles across (351 GS pixels
each) and one down reach 2048 -+ 1755 and -+ 1075, past the widest frame (60:9, -+ 1280) and still
inside the GS window. An entry is listed only when the appended tiles lie wholly beside the 4:3
picture: `tools/content_table_check.py decide` replays every dump that draws the mesh with the
entries off and on and requires the 4:3 picture and every target's 4:3 band byte-identical
(Original 1x and Enhanced 2x), and on a dump wider than 4:3 the sides changed by the sheet as its
middle is (`port/render/rd_content_extend.c`; `port/render/rd_mesh.c` (`extendSheet`,
`rd__sheet_tiles`, `retileSheet`)). `ICO_RD_CONTENT_EXTEND` overrides the verbs as
`ICO_RD_CONTENT_HIDE` does, with `off`, `tiles` or `table` (`port/render/rd_mesh.c`
(`rd__content_extend_set`, `rd__content_extend_verb`)).

**Natural chains.** Two stage animations let a chain down by scaling a Null's Y, which squashes
the links. With the option on the Null takes no scale along the chain, its children move along
it instead (`rd_chain_null`), and each link gets a clip plane in its own space where the disc's
squashed top link begins (`rd_chain_link`); the VU shaders discard the pixels past it. Off, the
disc's matrices and no plane (`port/render/rd_chain.h`; `port/render/rd_content_chain.c`
(`rd__chains`); `ico2/seki/src/BgAnimation.c`). The table is checked against the disc by
`tools/content_table_check.py chains`, and against the Japanese disc by `--version jpn chains`
(its boot ELF, `port/data/gen/jpn/table_desc.c` and `port/core/region_jpn.c`'s
`chain_group_st10l`/`chain_group_st19a`; the same two chains, groups 731 and 636). The same run
reports how many of the stage animation files the disc's `stageTable` names its packs hold: 805
of 969 on the European disc and 801 of 946 on the Japanese one, every file missing from the
Japanese disc missing from the European one too (the development stages' rows). The Korean disc
has its own `--version kor chains` run (its boot ELF, `port/data/gen/kor/table_desc.c` and
`port/core/region_kor.c`), the test `content_table_chains_kor`; its counts are not recorded here.

## Strip joining

The [region rule](ARCHITECTURE.md#terms)'s card test treats a strip as free-standing when it shares
no vertex position with a strip of another shape (`rd_mesh.c` `markJoinedStrips`). Packet.c makes a
packet per material and texture of a part, so a wall strip with its own material shares its edges
with the rest of the wall only through the part's other packets. Once per replayed frame, before the
meshes are uploaded, `rd__join_parts` groups the VU draws whose keys name one object and part (the
key without its ordinal byte) and `rd__mesh_join_part` marks across their meshes. Every packet of a
part the game registers is drawn, so the group is the part's whole, live and in a dump's replay. Out
of the box the rule never reads the mark (`port/render/rd_join.c`). Whether a free-standing strip is
a card is decided on the CPU when the draw is recorded and travels in its payload
([VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-card-test)); a strip the part joining marks is drawn whatever
its verdict.

## Screen planes

The GPU interpolates colour and ST from the pixel's place in the target, so its float rounding
depends on where the triangle sits. `doScreen` gives every screen-prim triangle its colour and S,
T, Q planes per GS pixel about its first corner, computed from the corners' GS positions and
values alone, and the shaders evaluate them at the pixel's offset from that corner: the same
numbers at every frame width. Sprites keep their own path, their colour being flat
(`port/render/rd_screen_planes.c` (`rd__plane_pass`); `port/shaders/sprite.hlsl`
(`sprite_planes`)). The VU programs' triangles do the same (`port/shaders/vu_common.hlsli`
(`vu_attr_planes`)).

Only such a pass carries the planes: `rd__plane_pass` turns its vertex shader into
`sprite_plane_*_vs` or `sprite_stq_plane_*_vs` and its fragment shader into `sprite_ps_planes`,
`sprite_texa_ps_planes` or `sprite_stq_ps_planes`, `sprite.hlsl`'s planes build
(`ICO_SPRITE_PLANES`, [SHADERS.md](SHADERS.md#the-families)), whose interface has the six plane
vectors after the colour and uv. Every other sprite, font, fog and wrap draw uses the plain build:
the position, the colour and uv or stq, two input locations. The plain build is the planes build
with the planes' zeros removed (the plain vertex shaders wrote zeros, and `sprite_plane_attrs`
returned at `p5.w == 0`), so the two builds draw the same bytes. The plane keys of the start-up set
are the same keys with the planes build's fragment id; their count does not change.

**Where the planes are used.** The planes' exactness holds at a whole scale: corners on the 1/16
grid and pixel centres on k + 0.5 give exact offsets only when the target's texels per GS pixel
are whole numbers. On a target whose scale is not a whole number on both axes (Enhanced at the
window's own size, 2364x1080 is 2.8125 x 2.41 a GS pixel) the planes buy nothing, and the
GPU's screen-space (noperspective) interpolation of the corners' values gives the same values
to a few ulp (a colour a level apart within about 3e-5 of a .5 boundary, an alpha-tested edge
pixel within a level of AREF on the other side; [DIVERGENCES.md](DIVERGENCES.md#video) VD-19). There, under
the Enhanced preset with the device profile's `vuPlanesFrac` (`[dev] vu_planes`), a VU pass of
`vu_ps` draws with `vu_np_ps` (its build without the planes' inputs, `ICO_VU_NO_PLANES`) and a
screen-prim triangle command with the plain sprite build. The planes stay for a draw with both
filters nearest (the GS's texel at a texel boundary), a texture with GS levels (the level by Q),
a VU draw with a chain clip plane (the discard at distance 0), `vu_texa_ps`'s draws, the STQ
screen triangles (their plain keys are not in the start-up set) and every draw under an exact
blend (the twin has no destination-read build) (`port/render/rd_pixel_plan.c`
(`rd__planes_dropped`, `rd__vu_pixel_fs`); `port/render/rd_screen_planes.c`
(`rd__screen_planes_want`)). Every whole scale, Original's 1x among them, keeps the planes
everywhere, so its pictures are unchanged. At a fractional scene scale (`g_rd.sceneSx`,
`sceneSy` at the start-up set) each `vu_ps` key of the start-up set has its `vu_np_ps` twin
(`rd__vu_pixel_fs_enum`); a later scale's are made at its change (DISPLAY.md). The log: the
profile's start line (`VU planes whole scales only`, or `every scale`), one `planes:` line a run
the first time a target drops them, and `vu no-planes N` (the VU passes drawn through `vu_np_ps`)
in `--stats` and the 10 s `window:` line.

## Camera blending

The presenter draws the pictures between two ticks through a camera blended from the two ticks'
cameras: the inverse views blended as affine matrices (the 3 x 3 split by polar decomposition
into a rotation, slerped, and a stretch, lerped; the eye lerped) and the projection lerped element
by element. The game's cull has to know that camera too, so these header-only functions are the
one implementation, included by `rd_interp.c` and `GsBase.c` in every build
(`port/render/rd_camera_blend.h` (`rdcb_blend_view`, `rdcb_blend_proj`, `rdcb_camera_jump`)). A
step past `RD_INTERP_CAMERA_TURN` (30 degrees) or `RD_INTERP_CAMERA_MOVE` (300 units) a tick is a
cut, and `RD_CULL_MARGIN` is the cull's frustum margin (`port/render/rd_camera_blend.h`).

**The eye never passes through the stage.** The game's cameras have no wall test of their own (their
sets and paths are designed), so a camera that moves less than those limits in a tick but past a
wall would put the blended eye behind the wall for the pictures in between, showing what is on the
other side for an instant. At every flip, on the game thread before `dl_Swap` closes the frame,
`gsbHostCullFlip` passes the eye (`+0x380`, the inverse view's translation) to
`ico_camera_wall_check`, which tests the step from the eye at the flip before against the stage's
collision: the walls and floors of the objects `MakeCollisionDependGObjList` gives the actors
(active, a stage layout object, displayed, with collision data), read from the live object table in
each node's frame through the block grids around the path, the walls `ClipWall`'s mode skips
skipped. A crossing either way (the game's own `ClipPlane` counts front to behind only) at a point
inside the element widened by `ICO_CAMERA_WALL_RADIUS` (4 units: the near plane's 2 and as much
again) makes `rd_camera_cut_wall` mark the frame that flip closes: `RdCamera.cut` 2, the snap
`RD_SNAP_WALL`, its pictures the tick's own as at the Original frame rate. The cut does not go
through `ico_video_camera_cut`, whose serial the window passes on after the step, to the frame the
next tick records. Not asked: a stage's first tick, a game cut pending since the flip before, photo
mode, a still eye and a step past `RD_INTERP_CAMERA_MOVE` (`RD_SNAP_CAMERA` already). The check runs
at every flip whatever the Frame rate and in the headless build, and only reads the game's memory
(`port/game/camera_wall.h`; `port/game/camera_wall.c` (`ico_camera_wall_step`,
`ico_camera_wall_cross_wall`, `ico_camera_wall_cross_floor`); `port/game/camera_wall_stage.c`
(`ico_camera_wall_check`, `stageQuery`); `ico2/seki/src/GsBase.c` (`gsbHostCullFlip`,
`gsbHostCullForget`); `port/render/rd_core.c` (`rd_camera_cut_wall`); `port/render/rd_interp.c`
(`rd__interp_snap`)). The log: `camera: between-tick blend skipped at stage N: the eye moved M cm
through a wall` (or `a floor`; rate-limited, with pictures blended) and `N where the camera's path
crossed the stage` in the 10 s `interp:` line's snaps; a dump keeps the 2 in its camera, so
`rd_replay_tool --interp` reports the snap as 9 (`RD_SNAP_WALL`).

**What the fog paints in a gap.** A pixel no draw covers keeps the clear's Z of 0, and the fog
sprite indexes its table by Z bits 16..23 (`port/shaders/fog_lut.hlsl`), so such a pixel takes
entry 0, the stage's full fog colour: near white in the light rooms. Neighbouring parts of the world
(a wall's or a floor's pieces, one draw per packet or texture) share their edge vertices, and the
vertex shader truncates each to the GS's 1/16 pixel grid (`port/shaders/vu_common.hlsli`
(`vu_ftoi4`)); two draws whose model to screen matrices differ in a bit can put a shared vertex in
two cells and leave a sliver of 1/16 GS pixel between them, which the fog paints. At a tick the
game's own matrices decide: most pieces of a stage carry the same model rows to the bit (one node
matrix), so their edges close, and pieces with other node matrices carry the rounding of their own
products, as on the PS2 ([DIVERGENCES.md](DIVERGENCES.md)). The fog probe line names entry 0
([The fog path and its self-test](#the-fog-path-and-its-self-test)).

**Still draws take the tick's block.** Between ticks a matched VU draw's block is both ticks'
blocks re-based on the blended camera and blended, and a mesh's model matrices are then composed
as a turn about its own centroid (`rotateModel`), while a draw that is the tick's alone (unmatched,
jumped, unkeyed) is the tick's block re-based (`camCurDraw`). Done to the pieces of a wall, that
made pieces with identical rows at the tick come out a few ulps apart (another pivot, or the other
path), and the set of shared vertices that changed cells moved with the alpha at every present:
white lines flickering along edges while the camera moves, never with it still (a still camera
re-bases nothing). No row of a normal program's block is the object's alone (each model row is a
camera part times the object's model to world matrix, qw 4..15 are the camera's), so the two
ticks' rows of a wall never agree in bits under a moving camera; re-based on the blended camera
they agree to the rounding of the products. `stillRows` compares the model to screen rows (qw
16..19) on X, Y and W only, each against the largest of those three in its quadword (the GS Z row
carries zf, about 1.07e9, in the translation: `ico2/seki/src/GsBase.c` (`gsb_SetVSMatrixSub`)),
and the model to clip rows (qw 20..23) whole; not the model to view rows (qw 24..27), which some
list 5 draws carry from another object, while their clip rows are the frame's clip camera times
the W of their own model to screen rows in every dump measured. So compared, a still object's
re-based rows agree within 1.8e-7 of each quadword's largest magnitude in the corpus' blended
pairs, a moving object's 5.8e-3 or more apart. A part of the world (a normal program placed
through the view) whose rows agree within `RD_INTERP_STILL_REL` (2^-17) is a still draw. The clip
rows' magnitudes are about the view distance, so the bound holds a still part to 2^-17 of its
distance, about 0.004 GS pixels at the focal length of 500 (the model to screen rows alone would
allow 2^-17 x 2048, 0.016 pixels). A still draw's camera and model rows (qw 4..27) are the tick's
block re-based exactly as `camCurDraw` re-bases one (the projection taken to the blended one
under an easing zoom, as there), not the blend and no turn; its UV scroll and lights keep their blend. Every still
draw, matched or not, is then the same function of the tick's block and the blended camera, so
identical rows give identical bits across neighbouring draws. Moving objects keep the blend and
the turn (none at alpha 0); alpha 1 has no blended camera and is the tick's payload byte for byte,
and at alpha 0 the blended camera is the tick before's to the bit (below); an object
moving less than the bound is drawn at its tick's place, at most about 0.004 pixels from its blend
(`port/render/rd_interp.c` (`stillDraw`, `stillRows`, `blendVu`, `camCurDraw`)). The 10 s summary carries a line for them, and
`rd_replay_tool --interp` prints the same counts as its third line:

```
interp: N still draws (parts of the world that did not move while the camera did), each drawn from the tick's block through the blended camera; R of them the blend of the two ticks would have re-rounded (camera and model rows not the tick's through the blended camera, to the bit); W parts of the world drawn as the tick's beside blended ones; K VU draws with the ticks' cameras
```

R counts the still draws whose element-wise blend of qw 4..27, what they were drawn with before
the still draws (less the turn), differs in bits from the block they take: the draws the fix
changed. W counts the parts of the world `camCurDraw` re-based in a blended frame, K the draws
marked `RD_VU_TICK_CAMERAS` (`RdInterpStats` `stillDraws`, `stillReRounded`, `curWorld`, `tickCams`,
`port/render/rd_internal.h`).

**Alpha 0 is a present between the ticks.** On the timer path the present clock clamps a value
below 0 to exactly 0, which a present right after a tick closes often has
([DISPLAY.md](DISPLAY.md#interpolation-between-ticks)). `camSetup` takes t = 0 like any other t
below 1, with the tick before's camera exactly: the blended view is the tick before's view
(`s_cam.vt = s_cam.vp`), its inverse the inverse of that, `E_p` the identity and `E_c = Vc^-1 Vp`,
without `rd__blend_affine` (whose t = 0 is the tick before's view to about 1e-16, not its bits,
`port/render/rd_camera_blend.h` (`rdcb_blend_affine`)); the projection's lerp is the tick before's
there. So the frame's view is the tick before's bit for bit, every draw that is the tick's alone
(unmatched, snapped, unkeyed) is re-based onto that camera by `camCurDraw`, a still draw takes the
tick's block through it, a moving matched draw the tick before's block re-based (a few ulps from the
recorded one), the CPU-projected draws are re-projected and the draws of the tick before alone are
placed as at any other alpha, and the draws carry the ticks' cameras (`RD_VU_TICK_CAMERAS`). An
object that did not move is drawn where it stood at the tick before, whichever kind of draw it is. The
ripples' STs move with their vertices ([WORK_BUFFERS.md](WORK_BUFFERS.md#the-ripples-between-ticks)).

**Payloads of two sizes.** A VU draw's payload carries its card section
([VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-card-test)) only under the region rule (`port/render/rd_mesh.c`
(`cardKind`: clip mode REGION, in a state other than out of the box, `rd__vu_extended`)), and the
game switches a packet from the region code (32) to the scissor code (36) when its box crosses the
screen's edge (`ico2/seki/src/RegistPacket.c` (`reg_clipPacketBoundingBox`)), so a part of the
world along the picture's edge gains or loses the section between ticks. `blendVu` reads both
headers first and takes a draw as mismatched by size (`RD_MISMATCH_SIZE`) only when a payload is
shorter than its header says (the header, the block, the bones, the stream and the materials:
`vuFixedSize`); the header test then makes the two fixed parts equal, every offset after the block
derives from the headers, and the card sections stay each payload's own (`rd__vu_cards_merge` takes
each side's size, and leaves the tick's section as recorded when the tick before has none). Such a
draw blends like any other, a still part of the world among them. The 10 s summary's second line counts them (`N VU draws
blended across a change of payload size (a card section at one tick only)`,
`RdInterpStats.sizeTailBlended`), and `rd_replay_tool --interp`'s second line with the draws still
mismatched by size.

**The mixed picture.** A frame that blends without a blended camera (a frame without a camera, the
blend refused, `ICO_RD_S2_LEGACY=1`: `RdInterpStats.camOffWhy`, `RD_CAM_OFF_*`) draws its matched
parts of the world from the ticks' blocks as recorded, blended element by element, and the parts
that are the tick's alone from the tick's block, `camCurDraw` having no camera to re-base them on.
Both kinds in one picture stand up to a tick of camera motion apart, and the fog paints the gaps
between them, wedges where a piece entering the view (the game's cull) is the tick's and its
neighbour the tick before's. The counts are
`worldPrevRaw` and `worldCurRaw`, and `mixed` the smaller of the two when both are above 0 (the
pieces in the minority; whether they touch is not computed). The 10 s summary carries a line for
the presents that drew one, after the alpha line ([DISPLAY.md](DISPLAY.md#interpolation-between-ticks)),
and an event line for the first three of each block:

```
interp: M presents drew a mixed picture (parts of the world from both ticks' cameras, no blended camera): P parts from the tick before, Q from the tick, R in the minority; why no blended camera: no camera a, blend failed b
interp: present of frame F at alpha 0.4000 drew 212 parts of the world from the tick before and 4 from the tick: a mixed picture
```

`rd_replay_tool --interp` prints the same counts and why the frame had no blended camera at the end
of its third line (`port/render/rd_interp.c` (`camSetup`, `camCurDraw`, `mixedNote`)). The vertex shaders keep the order of their arithmetic: every VU vertex
shader carries `NoContraction` (HLSL `precise`) and no module `FPFastMathMode`, so no driver fuses
one draw's products and not another's (`tools/check_spirv.py`, the precise gate).

## Interpolation and presentation

With `RdSettings.interpolate` on, `rd_end_frame` only closes the frame and the host calls
`rd_present(alpha)` (the [present](ARCHITECTURE.md#terms)) as often as the display allows; what
blends and how is [DISPLAY.md](DISPLAY.md#interpolation-between-ticks)'s
(`port/render/rd_interp.c`). `rd_present.c` shows DISPLAY in a box of the frame's aspect on the
output, `rd_crt.c` replaces that blit under the CRT filter, and `rd_video.c` shows the FMV picture.
All of it is [DISPLAY.md](DISPLAY.md). Re-keyed screen prims:
[WORK_BUFFERS.md](WORK_BUFFERS.md#screen-sprites-redrawn-under-a-new-key-between-ticks).

**Rotation continuity.** A model matrix or a bone blends as a slerped rotation, and the slerp's
short way (at most 180 degrees) is the motion only while the object turns less than half a turn a
tick, and a turn over `RD_INTERP_TURN_SNAP` (120 degrees) with nothing to tell its direction keeps
the tick's pose. A wheel can turn more than that: the trolley's wheels reach 140 degrees a tick at
full speed (`box.c`: a push of 0.5 a tick against a friction of 0.98 is 24.5 units a tick, on wheels
of radius 10), and a faster wheel at 190 degrees a tick would blend back 170 the short way (issue
98). So each rigid draw's model matrix and each bone of a skinned draw remember the turn they made
between two ticks (a rotation vector in the object's own frame), and the next blend takes the way,
short or long, nearer that turn, when it lies within `RD_INTERP_TURN_KEEP` (60 degrees) of it and
turns at most `RD_INTERP_TURN_LONG` (300 degrees) (`rdcb_blend_affine_turn`): a turn over the snap
that continues the one remembered blends.

The memory is kept by the draw's identity, not its place in the list: its key, type and list, its
ordinal among the draws of that key, type and list in its tick (the order the pairing pairs them
in, so two draws of one key keep their own memories), and the bone. Each identity keeps its last
two turns with the tick each ended at and the pose it ended at, in one table allocated once
(`RD_TURN_CAP` identities; one whose newer turn ended more than `RD_INTERP_TURN_GAP` + 1 ticks
before is free). A pair takes the turn that ended at its first tick (the pair before, blended);
without one, when its turn would snap, it takes the last turn that ended 1 to `RD_INTERP_TURN_GAP`
(8) ticks before its first tick, when carrying that turn on that many ticks puts the draw within 60
degrees of its pose there. The second rule is what issue 98 needed: a pair that is not blended (a
camera cut, a fade, a present that never came, a draw missing or mismatched for a tick) remembers
nothing, and a turn kept at the tick's pose is never remembered (forward 190 degrees is back 170
the short way, so its direction is unknown and remembering it could make the next pair blend
backwards). Without the second rule one camera cut on the ride at full speed left both wheels at
the tick's pose for every pair until the trolley slowed below 120 degrees a tick: the reporter's
log shows 2 cuts and 246 turns kept at the tick's pose in the block, none continued the long way
(140 degrees a tick never needs it). A turn under the snap, or one with the memory of the pair
before, blends as without the second rule, bit for bit. A wheel first seen between 120 and 180
degrees a tick with no memory keeps the tick's pose until it slows below the snap, and one that
speeds up in steps under 60 degrees a tick carries its turn through 180. The camera keeps the
short way. The tick's own present is never blended, so the tick's pose is exact either way.

The 10 s summary counts the turns that kept the tick's pose (`snapped over 120 degrees`) and, only
when there were any, adds a line saying why (no memory of the draw, its memory older than 8 ticks,
the pose not where its memory carries it, further than 60 degrees from its memory, the long way
over 300 degrees), how many turns found no room in the table, and the three draws that did it most
by mesh name and key with their largest turn and the last reason (`RD_TURN_WHY_*`,
`RdInterpStats.turnWhy`, `snapNote`; counters only, nothing per frame). `rd_replay_tool --interp`
prints the pair's long ways and turns kept at the tick's pose (`port/render/rd_interp.c` (rotation
continuity: `turnFind`, `turnRecall`, `turnRecallGap`, `turnKeep`, `blendTurn`, `turnSnaps`),
`port/render/rd_camera_blend.h`, `port/render/rd_internal.h`).

## Performance records

A record (`RdPerfRecord`) is filled while a replay runs (its CPU phases and counts), closed with the
RHI's counters, and then waits in a slot per frame in flight until its GPU timestamps can be read
without waiting. The timestamps carry the effect whose work they end, so the record has a GPU time
per list, for the uploads, for the VU vertex pre-pass (`gpuPrepassMs`, from `RD_PERF_TS_PREPASS`
after the uploads to list 0: its pass and its copies), for the present, and per picture effect
(`RD_PERF_POST_*`: reduction,
fog, motion blur, aura, glow, depth of field, softening, CRT, MSAA resolve, crack fill)
(`port/render/rd_perf.c`; `port/render/rd.h` (`RdPerfRecord`, `rd_perf_pop`)). An effect is the
one the colour target in force belongs to (the softening's and the aura's targets) or the one the
last post sprite names, and a post sprite's ends with its list: the next list starts with its
target's effect, so the motion blur, list 7's last sprite, is the sprite alone and lists 8 to 11
are no effect's (`port/render/rd_replay.c` (`perfTargetPost`, `Replay.perfTargetEffect`, the list
loop)). The same effect (`Replay.perfPost`) also decides what counts as a glow pass (`glowPasses`,
below) and labels each pass in the device-loss pass history (`passPush`), so both follow that
rule too: a pass begun in a later list is counted and labelled by its target's effect, not by the
last post sprite of an earlier list. Nothing drawn depends on it. The window sums them
into its 10 s statistics line, and `[dev] perf_log` writes one line per replay
([DISPLAY.md](DISPLAY.md#the-perf-csv)). The [replay tool](ARCHITECTURE.md#terms)'s `--stats` prints
the same record.

A present's interpolation time (`interpMs`) is measured around `rd__interp_frame` by `rd_present`
and charged to the replay that follows (`port/render/rd_perf.c` (`rd__perf_interp_ms`);
`port/render/rd_interp.c` (`rd_present`)); every present does all of its work at its alpha.

The record also counts the exact blend's cost and the glow's passes (issue 103): `dstDraws`, the
draws recorded through a destination-read shader (`dstBind`); `storagePasses`, the passes begun
without the colour attached; `dstSwitches`, the passes ended and begun again on the same targets
to change between a storage pass and one with the colour (`dstPassMode`; 0 on the attachment read
and the fixed-function blend); `glowPasses`, the render passes begun while the glow (flare, bloom,
the sun's eye blur) draws; `snapshots` and `snapSwaps`, target snapshots copied and the staticBlur
sprites' swaps ([above](#post-passes)); `a1xDraws`, the draw passes that took the above-1x kernels
(the box, the B-spline, the fog's table between its entries: [Above 1x](#above-1x); each planned
pass of a draw counts, so a draw split in two by the AFAIL split or the two-pass blend counts
twice, and the line's "draws resampled above 1x" are such passes), 0 at scale 1. The
window's per-replay line carries them after the exact blends:

```
window: per replay 1803 draws, 61 passes, ..., 0.0 DATE snapshots, 0.0 exact blends, 412 destination-read draws, 28 storage passes, 27 pass switches for the exact blend, glow 40 passes, 6.0 snapshots copied and 4.0 swapped, 3.0 draws resampled above 1x, VU pre-pass 5120 vertices in 180 draws (0.0 draws the old way); in the block ...
```

(the numbers there show the fields, not a measurement). The GPU line before it carries the glow,
fog and the other effects' milliseconds; the two together say what a slow card spends its replay
on. `--stats` prints the counts as a second line, `<dump>: stats: D destination-read draws, S
storage passes, P pass switches for the exact blend; glow N passes; snapshots C copied, W swapped;
A draws resampled above 1x`, and the VU vertex and triangle pre-passes as a third, `<dump>: stats:
VU pre-pass on: 2 passes, V vertices in D draws; F mesh and grid draws the old way; S sprites, T
triangle slots; K VU draws with the ticks' cameras` (`vuPrePasses`, `vuPreVertices`, `vuPreDraws`,
`vuPreFallbacks`, `vuPreSprites`, `vuTriSlots`, `vuTickDraws`: the draws a frame blended between
two ticks draws under the ticks' cameras, `port/render/rd_replay_vu.c` (`doVuDraw`);
[The vertex and triangle pre-passes](PIPELINES.md#the-vertex-and-triangle-pre-passes));
the GPU line (`window: GPU ms per replay: ...`) ends with `; VU pre-pass <ms>`, as `--stats`'s
`gpu:` line does
(`port/render/rd.h` (`RdPerfRecord`); `port/platform/window_host.c` (`perf_log`)).

The pixel diet's six counts are in the record too: `alphaOnePass` and `alphaOpaque` (draws the alpha plan
drew in one pass where the plan had two, and without the test and the blending), `fxConstant`,
`fxAdditive` and `fxLocal` (the post sprites drawn by the constant fold with the black sprite's clear, the
premultiplied forms and the local read: [Post passes](#post-passes)) and `vuNoPlanes` (the VU passes drawn
through the no-planes pixel shaders: [Screen planes](#screen-planes)). Each is 0 with its flag off or where
its mechanism does not apply. `rd_replay_tool --stats` prints one line, `<dump>: stats: pixel diet: alpha plan
N one-pass, M opaque; fx plan C constant, A additive, L local; vu no-planes D`, and the 10 s block prints
`window: pixel diet per replay: alpha plan N one-pass, M opaque; fx plan C constant, A additive, L local; vu
no-planes D` after the `window: per replay` line, every block
(`port/render/rd.h` (`RdPerfRecord`); `port/render/tools/rd_replay_tool.c`; `port/platform/window_host.c`
(`perf_log`)). With the profile line (`rd: profile: ... alpha plan on; fx plan on; fx local read ...; VU
planes ...`) the two say from a log alone which of the steps a device took.

### The per-pass timing

The list and effect stamps are written where a list or an effect changes, which is often inside
an open render pass (a pass on SCENE runs on across lists 0 to 2 and into list 4). A GPU that
draws a pass's pixels after the pass ends (a tile-based phone GPU; lavapipe) then puts that work
in the stretch where the pass ends, not where it was recorded (and a driver may end the pass at
the stamp and begin it again, loading and storing the target once more). Measured on lavapipe with the
issue 102 Mac frame at Original 1x: the fog effect reads 39.6 ms, while removing the fog's command
(`--nop 4:17`) leaves the frame's total where it was (248.6 against 248.7 ms) and removing it with
the sprite after it (`--nop 4:17 --nop 4:23`) takes 6.8 ms off; the list-4 stretch still reads
33.4 ms with both gone. So a list's or an effect's time on such a GPU is not its own cost.
`[dev] perf_list_stamps = false` (`ICO_RD_PERF_LIST_STAMPS=0`) writes only the stamps outside
every pass (the replay's start, the pre-pass's, the lists' start, the present's end), logging
`rd: GPU list and effect timestamps: off ...` once: the lists' and effects' times read 0 while the
GPU total of a present and the per-pass timing stay, so a phone's `window: N replays: ... GPU N
ms` with and without it says what the stamps inside passes cost there (`port/render/rd_perf.c`
(`stamp`)).

The per-pass timing measures each piece of GPU work between stamps written outside any render
pass. The replay names each piece as it begins it (`rd__perf_pass`, before the piece records
anything): the uploads at the head (`uploads`), the VU vertex pre-pass and its copies
(`VU pre-pass`), every render pass of the lists (`render pass`: `beginPass`, a clear's own pass,
the COLCLAMP 0 wrap's, the shadow count's reduction above 1x), a target copied into another
(`copy`, `doCopy`), a target copied into its snapshot (`snapshot`, `takeSnap`), the DATE
snapshot's pass (`DATE snapshot`, `dateSnapshot`), the fog's depth read made ready (`fog depth
read`: the copy path's copy, the buffer path's transfers, the table's upload, before the fog's
pass), the exact blend (`exact blend`, `doExact`), the MSAA epoch's spreads and resolves (`MSAA
resolve`, `msaaUpsample`, `msaaEnd`), a shadow reset's stencil clear as a pass of its own
(`stencil clear`) and the present (`present`: DISPLAY onto the window and the CRT filter)
(`port/render/rd.h` (`RD_PERF_PASS_*`, `RdPerfPass`); `port/render/rd_replay.c`,
`port/render/rd_replay_vu.c` (`rd__vu_prepass`)). Every replay counts its pieces by kind and its
render passes by effect (`RdPerfRecord.passKinds`, `passEffects`). A timed replay also writes a
timestamp at each piece's start, `RD_PERF_TS_PASS0` (32) plus the piece's index, keeping the
barriers the backend holds back held (so the barriers recorded are an untimed replay's), and one
after the last piece (`rd__perf_pass_end`, before the list ends); a piece's GPU time is its stamp
to the next one's, so it includes the barriers before its first command and nothing of another
piece. Its draws and pipeline binds are the RHI's counters between the two, its CPU time the
replay's recording of it. Each frame slot has 256 timestamps (`RHI_MAX_TIMESTAMPS`,
[RHI.md](RHI.md#the-interface)), so a replay times up to 223 pieces; a replay that begins more times
the rest with the last and says so (`passPartial`). The pieces wait for their times with the
record and then in a queue of 8 replays for `rd_perf_passes_of` (`port/render/rd_perf.c`, "the
passes").

Which replays are timed: every replay with `[dev] perf_passes = true` (`ICO_RD_PERF_PASSES=1`,
[CONFIG.md](CONFIG.md)); otherwise the first replay at least 250 ms after the last timed one
(`RD_PERF_PASS_EVERY_MS`), at most 40 in a 10 s block, for the window's summary. Not on iOS (as
the effect stamps). At start the renderer says which:

```
rd: GPU pass timing: one replay every 250 ms, each pass timed apart, for the 10 s summary; [dev] perf_passes = true times every replay
rd: GPU pass timing: every replay, each pass timed apart ([dev] perf_passes = true, ICO_RD_PERF_PASSES=1; the window writes ico-pc-passes.csv)
rd: GPU pass timing: off (no GPU timestamps)
```

The window's 10 s block has three lines from it, after the `GPU ms per replay` line
(`port/platform/window_host.c` (`perf_log`, `perf_passes_add`); `port/render/rd_perf.c`
(`rd_perf_pass_agg_text`, `rd_perf_pass_kinds_text`)):

```
window: GPU top passes: list 0 SCENE 848x448 420 draws 4.12 ms, glow WORK1 256x128 10.0 passes 10 draws 1.21 ms, ... (5 of 47.0 passes a replay, 16 timed replays; passes under 0.05 ms: 31.0 a replay, 0.62 ms)
window: passes per replay 47.0: render passes 32.0 (lists 9.0, reduction 1.0, fog 1.0, glow 17.0), uploads 1.0, VU pre-pass 1.0, copy 15.0, snapshot 0.5, DATE snapshot 1.5, fog depth read 1.0, present 1.0
window: CPU walk ms per replay by list 0-12: 1.10 0.05 ... 0.20; uploaded KB per replay: textures 57, meshes 27 (interpolated 20), uniform blocks 820, VU streams 30, other 61
```

(the numbers show the fields, not a measurement). The top passes are the timed replays' pieces
summed by what they are (label, target, size; the label is the effect for a render pass of one,
`list N` for another render pass, else the kind), the five most expensive, each per timed replay
with its passes (where it is not one a replay) and draws; the tail says how many pieces a replay
has and how many of them took under 0.05 ms, the size of a tile-based GPU's fixed cost of a pass.
The second line is the count by kind for every replay (no timestamps needed), from which the
fixed part of a tiler's frame can be worked out. The third splits the walk's CPU time by list
(each list from the lists' start or the list before to its end, binds included:
`RdPerfRecord.walkListMs`) and the upload ring's bytes by what they carry (`uploadTextureBytes`,
`meshUploadBytes` and of it `uploadInterpBytes`, the interpolation's scratch meshes,
`uploadUniformBytes`: every DrawCB, VuCB, VuBoneCB and FrameCB written (`uniformGroup`,
`rd_replay_vu.c` `cbCopy`), `uploadStreamBytes`: the VU streams without a mesh and the grids'
index lists; other: the screen prims' vertices, the fog's table, alignment). With `[dev]
perf_passes` the window also writes every piece of every replay to `ico-pc-passes.csv`
([DISPLAY.md](DISPLAY.md#the-perf-csv)). The replay tool's `--pass-times` (with `--stats`) times
the warm replay pass by pass and prints one line a piece, `<dump>: pass N: <label> <target> WxH,
list L, D draws, B pipeline binds, GPU <us> us, CPU <us> us`, then `<dump>: gpu top passes: ...`
and `<dump>: passes ...` as the window has them (`port/render/tools/rd_replay_tool.c`).

### The pixel cuts' records

The [integer forms](SHADERS.md#integer-forms) (REPEAT's mask, `fx_sprite_ps`'s division and its
coverage at scale 1) give the same bytes and shorten the executed path of `fx_sprite_ps`,
`sprite_texa_ps`, `vu_texa_ps` and the GS-mipmapped sprite entries. Their timings, from
`rd_replay_tool DUMP out.png --stats` on lavapipe (Mesa 25.0.7, `ICO_RD_BLEND=fixed`): the issue 95
title frame (`loss-20261015-100428-lost.rddump`, Original 1x 4:3) and the issue 102 Mac frame
(`frame-20261015-161933-v4251.rddump`, 688x448 16:9). The tool without the cuts (8c97d3cd8) and the
tool with them ran interleaved, three pairs per setting on the same host, the fastest of each: a
lavapipe timing is wall time on shared cores, so only an interleaved pair is comparable (a first
"after" run taken beside other work came out 2.4 times slower than its "before" on the Mac frame).
GPU milliseconds a replay; lists 0, 3, 4, 7 and 11; the effects glow and fog. The motion blur's
effect time is not comparable across the pair: without the cuts it took lists 8 to 11 with it
(the attribution ended at the next TARGET, [above](#performance-records)); list 7 holds the
sprite on both sides.

| frame, setting | without the cuts: total; lists 0, 3, 4, 7, 11; glow, fog | with the cuts |
| --- | --- | --- |
| title, Original 1x | 131.9; 43.6, 11.4, 8.6, 60.6, 2.0; 53.4, 8.5 | 123.2; 48.1, 11.6, 5.3, 50.6, 1.9; 44.4, 5.2 |
| title, Original 1x, `ICO_RD_NO_DUAL=1` | 158.4; 56.0, 16.8, 10.6, 62.9, 4.2; 55.1, 10.5 | 150.5; 59.8, 20.4, 9.8, 48.5, 4.7; 42.7, 9.8 |
| title, `--enhanced --resolution 2x --present 1376x896` | 467.9; 47.1, 55.5, 30.1, 308.3, 8.2; 277.9, 30.0 | 416.0; 45.4, 63.2, 24.2, 257.2, 7.4; 234.3, 24.2 |
| Mac, Original 1x | 284.9; 77.6, 15.3, 67.9, 68.4, 36.3; 57.3, 67.9 | 247.0; 90.4, 14.8, 39.6, 51.0, 36.7; 43.2, 39.6 |
| Mac, `--enhanced --resolution 2x --present 1376x896` | 809.2; 89.5, 70.7, 120.2, 343.1, 141.3; 303.1, 120.2 | 686.6; 74.7, 75.0, 72.1, 290.6, 132.8; 250.3, 72.0 |
| Mac, the same with `--glow-original` (those tools' work buffers at their GS sizes) | 771.8; 77.5, 71.6, 124.0, 322.7, 135.1; 283.6, 123.9 | 666.7; 79.6, 77.0, 70.2, 260.4, 134.4; 229.2, 70.1 |

On this proxy the glow chain (list 7) runs 16 to 25 percent shorter, the fog's list 4 (the fog
sprite and the depth copy's read) 20 to 45 percent, the whole frame 5 to 15 percent; the 3D
lists 0 and 3 move inside the run-to-run spread (no vertex shader changed; list 0's draws are
the same). The alpha-test merge (the AFAIL split under [Pipelines](#pipelines)) is in the "with" column
too: 9 fewer draws on the title frame, 42 on the Mac frame, the pictures byte for byte the same.

The module sizes (`tools/check_spirv.py --sizes`, words / instructions / blocks):
`fx_sprite_ps` 8,536 / 2,058 / 350 without the cuts, 9,323 / 2,260 / 390 with them;
`sprite_texa_ps` 10,181 / 2,401 / 378 and 11,501 / 2,761 / 468; `vu_texa_ps` 10,346 / 2,443 /
384 and 11,666 / 2,803 / 474; `sprite_ps` 7,548 / 1,792 / 269 and 7,812 / 1,864 / 287. The
modules grow (each REPEAT coordinate keeps both forms behind its branch) while a pixel runs
fewer instructions: no `OpSDiv` in `fx_sprite_ps` (a division is 19 straight-line instructions,
one of them a float divide), no `OpSRem` on a power-of-two size, no float divide at scale 1.

### Performance notes

**The UINT views of the RGBA8 targets.** `fx_sprite_ps` reads each texel as a float and turns
it back into an integer (`floor(x * 255 + 0.5)` a channel, `fx_load`), which gives the stored byte
for every byte. A target made with `RHI_TEX_UINT_VIEW` has an `RGBA8_UINT` view of the same image
as well (Vulkan: `VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT` with the two formats listed and a second,
sampled-only view, `port/rhi/vk/vk_resource.c` (`vkr_create_uint_view`); D3D12: the RGBA8 resources
are typeless already and the view is a UINT SRV, `port/rhi/d3d12/d3d12_pipeline.c`), bound with
`RHI_ASPECT_UINT`. `RhiLimits.uintView` is true on Vulkan and false on D3D12: the D3D12 view is
implemented but has not run on a Windows machine (the CI only cross-compiles it), so no D3D12 target
has it and `[dev] uint_view` asks for it there in vain (`port/rhi/d3d12/d3d12_device.c`
(`dx_fill_limits`)). Which targets have it is the
profile's (`port/render/rd_core.c` (`rd__target_create_gpu`)): the work class under
`RdProfile.uintViewWork`, the scene class under `uintViewScene` (a tiler's driver may give up its
compression on a mutable-format image, so a frame-sized target takes it only where the profile
says), the snapshot as its target (`port/render/rd_replay.c` (`snapEnsure`)). A staticBlur sprite
whose every texture has the view draws through `fx_sprite_u_ps` (`ICO_UINT_SRC=1`: `Texture2D<uint4>`
loads), the same bytes (`port/render/rd_replay.c` (`doBlurSpriteDraw`, `blurUintGroup`);
`port/render/rd_pipeline.c` (`rd__blur_key`, whose six enumerated keys get their twins while a
target can have the view)). The start line `rd: UINT view: work targets on|off, scene targets
on|off (the profile); the device's view and fx_sprite_u_ps present|absent` says what is in force,
`--stats` counts the sprites (`... staticBlur sprites through the UINT views`), and `rd_blur`'s
u case and `rd_pixel`'s uint case check the bytes against the CPU model with the views on.

**The blur sprite's per-draw constants.** `fx_sprite_ps` takes its UV step's divides from the
reciprocals the CPU rounds once a draw (`gs_div_trunc_rcp`, [SHADERS.md](SHADERS.md#integer-forms))
and `fx_texel`'s wrap size as integers (`DrawCB.aux1`, `port/render/rd_replay.c` (`blurAux1`)), the
same values the shader computed per pixel.

Measured or worked out, not done:

- **One pass for a constant-alpha lerp without dual-source blending.** On such a device a LERP
  is two passes (`rd__expand_no_dual`). Where As is the same over the draw (untextured;
  textured with TCC RGB; a mesh whose vertex alphas are all equal) the blend factors could be
  `RHI_BF_CONSTANT` and `RHI_BF_ONE_MINUS_CONSTANT` with the constant As / 128 (FIX / 128,
  or 1.0 and 0 for a PABE-unblended draw): one pass, the same bytes where the blender holds the
  constant at the fragment alpha's precision, which a phone's driver need not, so it would ship
  behind a switch, off, until a Mali device shows the same bytes both ways. It needs a blend in
  `RdPipelineKey`, `pipelineDesc`, a per-pass constant, the mesh's alpha scan at upload, and
  about a hundred more keys in the set without dual-source blending.
- **The CRT's triad gain at a large output.** Under x curvature `crt_ps` scans `2 k0 + 1` box
  pixels a pixel (`triadGain`, `k0 = ceil(box / grid) + 1`): at the 6016-pixel box of a 5K
  display over the 682-column grid of a 16:9 frame, 21 warps for the 9 that fall in the pixel's
  source column. `warpOf`'s x is affine in the box x along a row (its factor depends on y
  alone), so the columns of one source pixel are one run whose ends can be solved for; a scan
  from one before the run to one after it would visit the same hits in about half the steps.
  Not done: the ends' float rounding needs its own proof that no hit falls outside them.
- **The fog's cost per frame.** On lavapipe the fog of the Mac frame costs several times the
  title frame's at under three times the pixels; the fog pass count (`rd__fog_plan`) and its
  depth source (`rd__fog_source`) are where to look before its shader, which is about 60
  instructions a pixel.

**On a tile-based GPU** the times per list and per effect are where the work ended, not where it
was drawn: a tiler renders a pass's geometry when the pass ends, so the time lands on the timestamp
after the pass break. The fog shows it (issue 95, the Galaxy A12's log, Mali-G52, `fog: depth
path copy at start (tile-based GPU)`): it is one RD_POST_FOG a fogged frame (list 4; the title
frame's `4:42`), one draw, and on the copy path one whole copy of SCENE's depth (512 x 448 D32S8,
about 1.8 MB), after `endPass` closes the scene's pass (`port/render/rd_replay.c` (`doFog`);
`port/render/rd_core.c` (`rd__sampled_depth`)). The log's fog time equals list 4's time in every
line, and it swings with the scene, not the fog: 8.8-9.3 ms, then 70-172 ms, then 0.00 when the
scene has no fog, while lists 3 and 11 and the glow climb to 150-330 ms in the same lines: the
pass break moved, and the scene's tiles are paid at the next one. The fog's own work is a
full-screen sprite and the copy, a few MB of traffic. Every path breaks the pass there (inplace
must end the pass to sample the depth too; the copy adds only the transfer), so no cheaper path
draws the same picture with the passes as they are; one that keeps the depth on the tile would read
it in the scene's pass (a subpass input attachment, or dynamic rendering local read) and store
neither the depth nor a copy, a change to the RHI's passes, not done. On a tiler, compare the
sum of the lists, not one list or effect (the log's GPU top passes and GPU ms per replay lines now say so).

### Culling

**What the game culls.** Each object's box, then each part's box, goes through `gsb_ClipBox` against
the camera's frustum (`ico2/seki/src/RegistPacket.c` (the object at :606, every part at :1165),
`ico2/seki/src/GsBase.c` (`gsb_ClipBox`, :3107-3156)): the eight corners through the current matrix,
and a box is dropped only when every corner is outside one plane by the drop margin
(`gsbHostDropFlags`, :2892). The planes tested are +x, -x, +y, -y and the near side (`all & 0x2F`,
:3143: bit 4, the far side, is not in the mask), so nothing is dropped for its distance. A packet's
own box picks its clip program but never drops it: a packet wholly outside the view is drawn through
the clipping program and the GPU clips it away (`reg_clipPacketBoundingBox`, `RegistPacket.c`
:422-477). There is no occlusion test, no room, portal or visibility set, and one stage is resident.

**What the measurement finds.** `rd_replay_tool --occlusion` (below; `port/render/rd_occlusion.c`)
puts every GPU draw of list 0 inside an occlusion query of its own ([RHI.md](RHI.md#the-interface),
"Occlusion queries") during the picture's replay and maps each query to the command that drew it:
`g_rdStoreOrd`, the command the walk replays (its index in list 0, which the walk takes first), or
for a screen-prim run's draw, which `flushScreenRun` records after its command, the command that
opened the run (`ScreenRun.ord`, `g_rdDrawOwner`). The measurement runs under `ICO_RD_VU_MERGE=0`,
`ICO_RD_BLEND=fixed` and the screen-prim runs off (`rd__set_screen_merge`), so each draw command
makes its own GPU draws. A draw command (screen prims, mesh, skinned, grid, particles) whose draws
all passed 0 samples was hidden when it drew: left out of the replay (`--nop`) it changes no pixel,
the saving a culler that tests against the depth drawn so far could make. Hidden includes what draws
nothing for another reason: a packet outside the view (above), off the scissor, or with every
fragment discarded. The same measurement under `ICO_VK_DEPTH_ALWAYS=1` (every depth test passes,
[RHI.md](RHI.md#vulkan)) hides only those, so the commands hidden as drawn and not under the switch
are the ones the depth hid, the share an occlusion culler can take; `tools/occlusion_corpus.py`
separates the two from the tool's `--occlusion-csv` tables. Draws of other commands in list 0 (a
clear drawn as a quad, a fill) are counted apart and are not candidates. Only list 0 is measured:
the passes after it (the fog, glow, aura, fades, shadows and the present) draw the whole frame and
no test against the scene's depth leaves them out.

**The gate and the numbers.** `tools/occlusion_corpus.py` runs the dumps of
`tools/occlusion_corpus.txt` at Original 4:3 1x and Enhanced 4:3 2x: the measurement as drawn and
without the depth, then the plain replay and the replays with the hidden commands left out (`--nop`
`@FILE`) under `--pass-dump` (every target byte for byte) and `--stats --gpu-repeat K` (list 0's GPU
median of K warm replays on lavapipe). A culler of list 0 is built only when the median share the
depth hid is at least 15 % of the list-0 draws and 10 % of the list-0 triangles over the gameplay
dumps and leaving those commands out takes at least 10 % off list 0's GPU time, with every compared
pass dump identical. The report, a row a dump and setting with the medians and the verdict, is
written beside the run's logs (`--out`, by default under
`build-host/tmp/<release>/evidence/perf/occlusion.md` on the maintainer's machine); its findings go
here once measured.

## Dumps and the replay tool

A [.rddump](ARCHITECTURE.md#terms) is the in-memory frame, little-endian, versioned by
`RD_DUMP_VERSION` (14): the frame's native size and aspect, the disc it was drawn on (its region
slug (`pal`, `jpn` or `kor`), version 12; an older dump is the European disc's), its camera, start and end states, the
13 lists, the payload, the textures and temporary targets it references, its VU meshes and tick
cameras. Target
contents are not written: a frame that reads a retained target replays from what the loading
context holds. Loading creates the textures and targets anew and rewrites every id, and gives the VU
draws recorded without card verdicts theirs ([VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-card-test)), so
a dump replays alike at every setting. `rd_replay_tool` selects the dump's region
(`ico_region_select`) before it replays, so the reduction's crop and the border mask
(`rd__reduction_crop`) are that disc's. Dumps hold game assets and are never committed
(`port/render/rd_dump.c`, header comment).

Dumps are written by F12 and Shift+F12 ([PLATFORM.md](PLATFORM.md#hotkeys-and-dump-zips)) and by
`[dev] dump_every = N` (with `dump_dir`, `dump_interp`, `dump_from`), which the host hands to rd as
`ICO_RD_DUMP_EVERY`, `ICO_RD_DUMP_DIR`, `ICO_RD_DUMP_INTERP` and `ICO_RD_DUMP_FROM`; `dump_interp`
also writes the frame before (`-prev`) and the frame blended half way (`-i50`)
(`port/platform/window_host.c` (`frame_dump`); `port/platform/host_config.c` (`export_dump_keys`);
`port/render/rd_core.c` (`readDumpConfig`, `rd_end_frame`)). `rd_png.c` writes uncompressed PNGs for
the tool and the tests (`port/render/rd_png.c`).

`rd_replay_tool <dump> <out.png>` loads a dump (or the first `.rddump` in an F12 zip) and renders
it headless; `--interp T PREV` with PREV an F12 or loss zip takes the zip's second `.rddump`, the
tick before, so a report's zip rebuilds the pictures between its two ticks, and with T the alpha
F12's line names for its `-present.png` ([DISPLAY.md](DISPLAY.md#hotkeys-for-pictures)) the picture
that was presented (`port/render/tools/rd_replay_tool.c`, usage comment).

`--time N` measures the CPU side of a replay on a dump. After the picture is written the frame is
replayed N more times; with `--interp` the presenter's frame is built again before each
(`rd__interp_frame`: the first as a tick's first present, the others as its later presents, its
time charged to the replay as `rd_present` charges it). The tool prints one line from the N
records, each CPU phase's mean and least: `<dump>: time: N replays, CPU mean (min) ms: total,
interp, wait, acquire, upload, walk, bind, submit, present; per replay: ring KB,
uniform KB, interp KB, draws, pipeline binds, bind group binds`, then `<dump>: time: the first
replay: interp X ms, total T`. With `--interp` it compares DISPLAY after the
first and the last of the N replays byte for byte and prints `pictures identical`, or the bytes
that differ and exits 1: a present later in a tick must draw the tick's first present's picture
(`timeReplays`). The lavapipe numbers are for comparing two builds on one host, not a device's
times.

The options:

| option | does |
| --- | --- |
| `--target NAME` | writes a named target instead of DISPLAY |
| `--pass-dump DIR` | writes every named target to `DIR/<NAME>.png`, and with `--present` the presented picture |
| `--dump-temps DIR` | writes every per-frame temp target (the water blocks among them: puddle.c's 344x256 reflection block with its depth) to `DIR/temp-<n>-<w>x<h>.png` right after the replay, before the frame's end frees them |
| `--present WxH` | renders the presenter into a W x H output |
| `--backend vulkan\|d3d12` | picks the RHI backend, to compare backends |
| `--enhanced`, `--aspect`, `--resolution`, `--full-height`, `--full-pixel`, `--border-mask` (Full pixel off as the game has it), `--water-sharp` (Water reflections: Sharp), `--filter`, `--mirror`, `--crt` | the display options a dump does not carry; the PS2 picture by default |
| `--list` | every command of the replayed lists, with textures, prims, boxes and state, then the water summary (`water: ...`, [above](#water-and-render-to-texture-blocks)) |
| `--no-device` | loads into a record-only renderer for `--list`, `--mesh`, `--dump-textures` |
| `--nop SPEC` | turns commands into NOPs, to find the draw behind a pixel; SPEC is `L:A[-B]`, several separated by commas (`0:12-14,0:40`), or `@FILE` for a spec in a file; the state commands in a range stay |
| `--occlusion` | the hidden-draw measurement of list 0 ([Culling](#culling)): `<dump>: occlusion: hidden N of M list-0 draws, T of U triangles (index count / 3); hidden commands: SPEC` (SPEC a `--nop` spec, `none` when empty), then `<dump>: occlusion: H of K draw commands hidden; P % of the draws, Q % of the triangles; Q queries, X without a count, Y draws past the 8192 queries; Z draws of other commands not counted; measured under ...`; it sets `ICO_RD_VU_MERGE=0` and `ICO_RD_BLEND=fixed` and turns the screen-prim runs off (the `note:` line says so); Vulkan only (exit 77 elsewhere); not with `--presents`, `--loss-bisect`, `--keys` or `--no-device` |
| `--occlusion-out FILE`, `--occlusion-csv FILE` | with `--occlusion`: the spec written to FILE (for `--nop @FILE`); a line a command of list 0 that made a GPU draw, `cmd,type,draws,counted,triangles,samples` |
| `--gpu-repeat K` | with `--stats`: RHI_FRAMES_IN_FLIGHT + 1 + K replays after the picture and, after the `gpu:` line, `<dump>: gpu median of R replays: total X ms, list 0 Y ms, VU pre-pass Z ms` over the warm records that came back (the first replay after the picture left out) |
| `--mesh NAME` | a VU mesh vertex by vertex, with the kick, overlap and joined marks |
| `--dump-textures DIR` | every image texture as decoded |
| `--stats` | the replay's performance record, with GPU times |
| `--pass-times` | with `--stats`, the warm replay timed pass by pass ([The per-pass timing](#the-per-pass-timing)) |
| `--time N` | after the picture, the frame replayed N more times and the mean and least of each CPU phase of their records printed (below) |
| `--prev DUMP` | replays earlier dumps first so retained targets hold the run's pictures |
| `--no-half`, `--no-aa1` | the frame without the field's half pixel or without PRIM.AA1 |
| `--no-crack-fill` | the frame without the crack filler above 1x ([Crack filling above 1x](#crack-filling-above-1x)): its A/B; `--stats` prints the filler's passes and the pixels it filled |
| `--interp T PREV` | the frame the presenter builds between PREV and the dump at alpha T (PREV an F12 or loss zip: its second `.rddump`, the tick before) |
| `--interp-any`, `--no-reproject` | with `--interp`: PREV taken as the tick before whatever its frame number; the CPU-projected draws blended in screen space |
| `--interp-nop-prev L:A[-B]` | with `--interp`, repeatable: turns commands A..B of list L of PREV into NOPs before the blend, as `--nop` does to the dump, so their draws in the dump are the tick's alone (unmatched): chosen parts of the world made the tick's in a blended picture, the [mixed picture](#camera-blending)'s case |
| `--aa MODE` | (with `--enhanced`) the anti-aliasing setting: off, ssaa2, ssaa4, msaa2, msaa4, msaa8 |
| `--crt-strength K` | after `--crt`, the strength 0..1 |
| `--overlay-test` | (with `--present`) a test pattern and a popup drawn as a presentation overlay |
| `--vu-ref L:I` | the CPU mirror's vertices of prelit draw I of list L |
| `--loss-bisect` | the [loss bisect](#the-loss-bisect) on the dump: the frame one pass longer each time, waiting for the GPU after each |
| `--keys` | the pipelines the replay asks for against the start-up set, one line a key (`in set` or `MISSING`), exit 1 on a miss ([The start-up set](PIPELINES.md#the-start-up-set)); needs a device, `<out.png>` unused (give `-`) |
| `--passes A[-B]` | replays passes A..B only (the [pass window](#the-loss-bisect)); A alone is pass A alone |

## Shader families

The entries the replay binds are named in `s_vsNames` and `s_fsNames` (`port/render/rd_replay.c`).
The families and what each entry draws are [SHADERS.md](SHADERS.md#the-families)'s.

## Tests

The renderer's tests are `port/render/test/`, listed with their ctest names in
[TESTING.md](TESTING.md#portrendertest); the pixel gates on dumps are
[TESTING.md](TESTING.md#render-gates). The variants run the same program under `ICO_RD_NO_DUAL=1`
(`_nodual`), `ICO_VK_FAKE_LIMITS=min` (`_minlimits`) or `mali` (`_mali`), `ICO_RD_BLEND=fixed`
(`_fixed`) or `storage` (`_storage`) and `ICO_VK_FAKE_D24S8=1` (`_d24s8`)
(`port/render/CMakeLists.txt`). `ctest --test-dir <build> -R '^rd_'` runs rd's own.

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
| `port/render/rd_replay_vu.c`, `rd_replay.h` | the replay's VU draws and the vertex pre-pass; what they share with `rd_replay.c` (the replay's state, a draw's setup, its helpers) |
| `port/render/rd_vuprepass_selftest.c` | the vertex pre-pass's start-up self-test |
| `port/render/rd_upload.c` | the upload ring and what a replay uploads before its first pass |
| `port/render/rd_pipeline.c` | pipeline keys from the state block, the pipeline cache, the reachable set |
| `port/render/rd_dst_read.c` | the exact GS blend read from the destination in the fragment shader |
| `port/render/rd_post.c` | `rd_post`: each post kind as the GS writes and sprites GsBase.c issues |
| `port/render/rd_blur.c` | staticBlur.c's sprites and the reduction's, for `doBlurSprite` |
| `port/render/rd_shadow.c` | the shadow count on the stencil |
| `port/render/rd_water.c` | render-to-texture blocks, target aliases, camera scopes, pool STs |
| `port/render/rd_fog_path.c` | how the fog reads the scene depth; the fog probe and self-test |
| `port/render/rd_content_hide.c` | the content table: stage faces the native frame must not show |
| `port/render/rd_content_extend.c` | the content table's sheets: stage overlays continued to the frame's edges |
| `port/render/rd_content_chain.c`, `rd_chain.h` | the chains drawn at their links' size |
| `port/render/rd_join.c` | the region rule's card test joined over a part's packets |
| `port/render/rd_screen_planes.c` | the attribute planes of screen-prim triangles |
| `port/render/rd_camera_blend.h` | the blended camera, shared with the game's cull |
| `port/render/rd_perf.c` | the per-replay performance records |
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
| 12 | debug font; the reduction at its end |

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
(`blurAbove1xSteps`, `spriteAbove1x`)). The filter is the one the game sets: the glow's paste WORK3 -> SCENE
(`ico2/sugipon/src/staticBlur.c` (`pasteFullScreenFlare`); the title dump's 7:367) and the depth
of field's four pastes WORK0 -> SCENE (`pasteToFB`) draw under TEX1 0x60, MMAG and MMIN both
LINEAR (written by `eyeBlur` and `pasteToFB`; the dump's `FILTER 1 1` at 7:341), and the eye
blur's subtract into SCENE under 0x40, MMAG NEAREST, a single texel (`eyeBlur`). Above 1x each
SCENE texel therefore takes the GS's U and V at its own GS position (12.4, the texel's centre:
[Post passes](#post-passes)'s scaled-target rule in `fx_sprite_ps`) and the GS's 4-bit bilinear
between the same four work texels the PS2 blends there (`gs_bilerp`): the PS2's 256 x 128 glow
magnified as the GS magnifies it, sampled more finely; at 1x it is the GS's picture byte for byte.
The softening's AA0 and AA1 back over SCENE (`sprite_ps`) take the draw's own sampler, as at 1x
(`spriteAbove1x`). The draws from SCENE, WORK2 or AURA_WORK into a work buffer keep the area box
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

The texture cache decodes the TIM2 bytes Texture.c keeps in memory into an rd texture the first
time it is needed and keeps it until the content changes (`port/render/rd_tex.h`):

- **key**: (texture id, content generation, TEXA mode); the mode is one of `RdTexA` (alpha baked)
  or `RDTEX_TEXA_REPLAY` (the source format's alpha kept and TEXA applied by the shader at replay,
  so TEXA leaks between lists as on the GS).
- **decode**: PSMCT32, 24, 16, 16S, PSMT8, PSMT4, PSMT8H, PSMT4HL, PSMT4HH and the PSMZ formats;
  CLUTs in CSM1 or index order. Output RGBA8 with the raw GS alpha (0x80 = 1.0) for 32-bit texels,
  the A bit for 16-bit ones and 0 for 24-bit ones (`rdtex_decode`).
- **size**: padded with zero texels to 2^TW x 2^TH, so STQ and REPEAT wrap where the GS wraps.
- **update**: a new generation of the same size and format re-expands into the same rd texture;
  otherwise the old one is retired and destroyed two `rdtex_frame_tick` calls later.
- **levels**: a mipmapped TEX1 stores the TIM2's own levels when they are authored pictures
  (`rdtex_store_levels`, `RDTEX_AUTHORED_RATIO`), sampled at the GS's level; otherwise the
  replay builds the further levels by a 2x2 box, colour weighted by alpha, with alpha coverage
  kept for alpha-tested textures (`port/render/rd_upload.c` (`uploadMips`);
  `port/render/rd_tex.h` (`rdtex_build_mip_chain`, `rdtex_keep_alpha_coverage`)).

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
| `RDC_POST_STUB` | `RD_POST_FOG`: `doFog`; the reduction and the staticBlur kinds: `doBlurSprite` |
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
(`port/render/rd_upload.c` (`rd__upload_dirty_textures`, `rd__upload_meshes`)). Every
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

**Exact blends.** `RDC_EXACT_BLEND` (from `rd_post` with `exactInt`) runs the GS integer blend
through `blend_int` on RGBA8_UINT copies of both targets and copies the result back; source and
destination must be the same size (`port/render/rd_post.c`; `port/render/rd_replay.c`
(`doExact`)).

**AA1.** PRIM.AA1 on lines and triangles: `aa1Expand` adds a one-GS-pixel fringe along each edge
(outside it, along its minor axis) whose coverage runs from 1 on the edge to 0 and is the
fragment's alpha in `sprite_aa1_ps`; a triangle's own vertices carry `ICO_AA1_INTERIOR`. A draw
whose fringes need Z write off is split in two (`port/render/rd_replay.c` (`aa1Band`,
`aa1Fringe`, `aa1Expand`)).

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
A mesh or grid command the vertex pre-pass recorded draws its triangles through `vu_tri_vs` over
that record ([The vertex pre-pass](#the-vertex-pre-pass)).
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
resolves the colour by the samples' mean and the depth by the smallest sample (the farthest under
GEQUAL), so a surface the game draws again after the close with GEQUAL passes where it passed at
1x. Integer-aligned sprites drawn inside it are moved half a texel so they cover whole pixels; the
fog runs per sample inside the epoch when the device has sample-rate shading
(`port/render/rd_replay.c`, "the MSAA epoch"; `msaaPass`, `msaaEnd`, `msaaSpriteExpand`). When no
way of reading the multisampled depth passed the fog's self-test, rd draws without MSAA and
`rd_aa_msaa_blocked` says so (`port/render/rd.h` (`rd_aa_msaa_blocked`)).

### The pass ring

The replay notes what it gives the GPU in a ring of the last 2048 entries, always on (a few stores
each): every render pass (`beginPass`), target copy (`doCopy`), snapshot copy (`takeSnap`), exact
blend (`doExact`), the upload batch before the lists, the VU vertex pre-pass
(`rd_replay_vu.c` `rd__vu_prepass`) and the present. An entry holds its sequence
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

### The loss bisect

The start after a loss that left a loss dump (of the same version) finds the pass that loses the
device on that device, before the game. Only a start with the player's own blending and backend
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
in the order they begin (the [vertex pre-pass](#the-vertex-pre-pass) first when it runs, then
render passes, copies, snapshots and exact blends: the pass ring's entries other than the uploads
and the present). With a window [first, last] the draws and the target and snapshot copies of the
passes outside it are not recorded (the pre-pass's record copies go with its draws, so the slot's
record buffer keeps what an earlier replay of the frame wrote; the uploads and the fog's depth copy
are not windowed), the passes themselves still begin, and the walk stops after the
command that began pass last + 1, which draws nothing. So a loss in the pre-pass is pass 1, not a
loss with no draw recorded (`port/render/rd_replay_vu.c` (`rd__vu_prepass`,
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
The note's vertex shader is the program's own entry, also for a draw through `vu_tri_vs` and for
the pre-pass's draws (pass 1, noted by `rd__vu_prepass` with their vertex range), so `VS_ONLY`
separates the programs as before the pre-pass; the key's words name the pipeline drawn with.
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
multisampled pass, a storage pass or a readable one (the exact blend's), and its passes are not
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

The shaders take the alpha test, the AFAIL split, the texture function, TCC, TEXA, FBA, PABE,
DATE and the blend factor's source as uniforms, so the pipeline key is normalised: one
representative blend per blend path, and the AFAIL split carried by Z write and colour mask
(`port/render/rd_pipeline.c`, header comment). The blend paths on UNORM targets
(`port/render/rd_pipeline.c`):

| modes | path |
| --- | --- |
| 0, 1, 5, 6 (Cs*F + Cd, Cd - Cs*F) | `DF_PREMUL`: the shader writes `min((Cs*F) >> 7, 255)`, the blender adds or reverse-subtracts with ONE; exact for F up to 255 |
| 2, 4, 7 (the LERPs) | dual-source factor F/128, F above 0x80 clamped |
| 3 (Cd*FIX + Cs) | SRC1 as the destination factor, FIX clamped to 0x80 |
| 8, 9, 10 (Ad) | DST_ALPHA, which reads Ad/255 |
| 11 (Cd*As + Cd) | not representable: Cd unchanged, reported once |

PABE writes Cs unblended for a pixel whose As MSB is clear, through the second output; the
premultiplied and Ad forms cannot, and those pixels blend (reported once). On a device without
dual-source blending the LERPs and mode 3 draw in two passes: colour with the factor in c0.a, then
when the mask has A an alpha pass with blending off and no Z write under a Z test that admits the
colour pass's fragments (`port/render/rd_pipeline.c` (`rd__expand_no_dual`)). `ICO_RD_NO_DUAL=1`
forces the two-pass form on any device (`port/render/rd_core.c` (`rd_init`)). Where the destination
read is available these paths give way to it (next section).

**The AFAIL split.** An alpha-tested draw whose failing fragments write something other than its
passing ones is two draws: the passing fragments with the draw's Z write and mask, then the failing
ones under AFAIL (FB_ONLY: no Z; ZB_ONLY: no colour; RGB_ONLY: no Z, no alpha), the shader keeping
one side each (`gs_alpha_discard`, `g_mode.z` bits 16..17). Where the failing fragments write
exactly what the passing ones write the test decides nothing and the draw is one draw without it,
every fragment in the GS's primitive order: Z write off (ZBUF.ZMSK 1, or no depth attachment) with
AFAIL FB_ONLY, or with RGB_ONLY when the mask already leaves alpha out (`port/render/rd_pipeline.c`
(`rd__plan_screen_draw_ex`)). The GS User's Manual's TEST register: FB_ONLY updates the frame buffer
and not the Z buffer for a failing pixel, RGB_ONLY the frame buffer's RGB only, and with ZMSK 1 a
passing pixel updates the frame buffer only too. PCSX2 does the same: its software renderer skips
the test for FB_ONLY without Z write and for RGB_ONLY without Z write on a 24-bit frame
(`GSDrawScanline.cpp` `TestAlpha`), its hardware renderer sets ATST ALWAYS for FB_ONLY without Z
write and turns RGB_ONLY without alpha into FB_ONLY ("Failing alpha test is a NOP",
`GSRendererHW.cpp`). The split had drawn every failing fragment after every passing one, so where a
failing and a passing fragment of one draw met on a pixel the later one in the GS's order did not
always win (issues 95 and 103). DATE, FBA, PABE and the blend ride on the one draw as
on both. The merged draw's key is the passing draw's, which the failing draw
shares wherever the merge applies, so the merge adds and removes no pipeline key: the dual,
no-dual and exact-blend sets keep their counts. The scissor batches' two draws (code 36: the clipped fans with ABE forced on, then the
strip's kicks) are another state and another set of triangles, not this condition, and stay two
(`port/render/rd_replay_vu.c` (`doVuDraw`)). The split still orders the passing fragments first where
it remains: Z write on (each draw writes other buffers), RGB_ONLY with alpha written, ZB_ONLY with
colour written. In the dumps measured (2026-10-16) the merged draws are the Z-write-off alpha-tested
ones: meshes `si_hair`, `bmd-tuno`, `cloud`, `cloud2`, `wf_ab03`, `fogs`, the particles, and two
sprites of list 11 on the title frame; there 6 VU commands and 2 screen prims, of 172 VU commands
whose other 166 alpha-tested ones write Z. The log says once that the rule is in effect: `rd: alpha test: a draw with Z write off
whose failing pixels write what its passing pixels write (AFAIL FB_ONLY, RGB_ONLY without alpha) is
one draw without the test, in the GS's order (reported once)`; `rd__set_alpha_merge(false)` plans the
two draws (tests: ctest `rd_state` `testAlphaMerge`, the condition table, each merged key a key of the split plan, and the game's FB_ONLY state without Z write in the screen set; ctest `rd_pixel`
`testAlphaOrder`, an overlap the split drew in the other order).

The cache holds `RD_PIPELINE_CACHE_MAX` keys, four times `RD_PIPELINE_REACHABLE_MAX` (1024, the
ceiling the tests hold the reachable set under, `rd__enumerate_reachable`); a key past it or one
whose creation failed is logged once and drawn as nothing, never an abort (`port/render/rd_state.h`;
`port/render/rd_pipeline.c` (`rd__get_pipeline`)). `rd_precreate_pipelines` creates the whole
reachable set at start so the first frames do not stall on the driver. It hands the keys to
`rhi_create_pipelines` up to 32 at a time, so the driver compiles each batch on worker threads
([RHI.md](RHI.md#the-interface)); a batch holds keys with a destination read or keys without, never
both. The progress function runs while a batch compiles (about every 50 ms, with the count so far)
and between batches, and a nonzero return stops the set after the batch in flight; the rest is
created as the frames need it. The activity note names the batch: `creating pipelines 321-352 of
702: <first key> to <last key>; vertex <module> <words> words, ...; largest fragment <module>
<words> words` (a VU key's words end with ` ext 0` or ` ext 1`, its variant: [The VU shader
variants](#the-vu-shader-variants)), put back after any pipeline the progress screen's frame creates in
the middle of it. The log line after the set adds the compiles, the threads and the longest compile:
`pipelines: <made> of the reachable set's <n> created at start-up in <t> ms (<f> failed); <c>
compiled on <k> threads, the longest <s> s`, and two lines say where the time went, from the
workers' own times (`RhiPipelineBatch.ms`): `pipelines: the slowest: <s> s <key>; ...` (the ten
slowest compiles) and `pipelines: compile time by vertex shader: <entry> <s> s, ...` (summed over
the threads, the largest first) (`port/render/rd.h` (`rd_precreate_pipelines`,
`RdPipelineProgressFn`, `rd_precreate_longest_ms`); `port/render/rd_pipeline.c` (`preTimesLog`)).
At start the renderer logs the embedded modules' count and the largest, `shaders: <n> modules,
largest <entry> <words> words`: a driver's compile time grows with a module's size, which
`spirv_check` holds to a budget ([SHADERS.md](SHADERS.md#the-envelope); `port/render/rd_replay.c`).

### The start-up set

The window builds the reachable set with `rd_precreate_pipelines` before the first frame, behind the
start-up screen, unless one of the rules below skips it (`port/platform/window_host.c`
(`ico_window_open`)). Without the set the window calls `rd_pipelines_lazy(why)` instead, which logs
`rd: pipelines: no start-up set <why>: pipelines are made as the game needs them`; there is no
start-up screen (the title comes at once), and each pipeline is made when a frame first needs it,
on the thread that draws, the frame waiting for its compile (`rd__get_pipeline`). The summary line's
renderer part ends with `pipelines the start-up set` or `pipelines on first use (no start-up set)`
(`rd_summary_text`).

With the set made, a pipeline a draw needs later is a key the set did not hold, made on the thread
that draws while the replay waits; on a driver that takes seconds a pipeline (up to 3.4 s for a VU
one on a PowerVR DXT-48-1536) that is a stall of the picture and of the game behind it. Each such
key is logged once (the cache keeps it), the first 8 and then every 100th, with the draw list the
replay was in, the whole key (the fields of the batch notes, then the Z test, the Z write, the
colour mask, the target format, the topology, the stencil mode, PRIM.AA1 and the two-pass blend's
flag) and the time it took (`port/render/rd_pipeline.c` (`firstUseLine`)):

    rd: pipelines: made on first use in list 0: program 0 blend 4 vs 15 fs 5 fmt 1/8 msaa 0 dst 0 ext 1, ztst <z> zwrite <w> mask <m> target <t> prim <p> stencil <s> aa1 <a> nodual <n> (vu_tri_vs + vu_ps) in 769.8 ms (not in the start-up set; 2 so far)

`no draw list (the present, a check)` stands for the list outside the replay's lists. The key is the
one `rd__enumerate_reachable` would need to hold. On a PowerVR DXT-48-1536 a `vu_tri_vs`
pipeline made so takes about 750 ms (770 and 745 ms for the two keys of a room change in an rc
build's log, which named only the fields before the comma) and a program entry's up to 3.4 s, a
visible stall each; a 2D key the driver has cached, under 1 ms. That log's keys matched enumerated
keys in every field it printed, so they differed in one it did not: two `vu_tri_vs` keys (blend 4
and 12, ABE off) in list 0, and a UI-space sprite (`program 9 blend 4`, `sprite_ui_vs +
sprite_ps`, with the depth target) in list 11. The set now also holds the pre-pass's keys under
TEST Z ALWAYS (the one Z test a VU draw is seen to get that the set lacked, below; not confirmed
as those keys' state) and UI-space sprites under the TEST Z GEQUAL every list's head writes, with
the depth target (`ico2/seki/src/GsBase.c` (`gsb_setNormalReg`): a 2D draw of list 11 before
the list's own TEST write takes it); the whole key in the line names the state if one is still
missing. The program entries' keys before the pre-pass lacked the same states: every one of them
has its `vu_tri_vs` twin in the set (`rd_state_test` `testEnumerationPrepass`). A pipeline made in
the vertex pre-pass is `in the vertex pre-pass (before list 0)` (`rd__vu_prepass_running`). A
replay whose walk (the vertex pre-pass and the recording of its lists, the `walk` phase of the
[slow replay line](DISPLAY.md#the-stats-lines)) takes over 500 ms gets one line, the first 8 and
then every 100th, saying where that time went: the pipelines made at first use, the textures and
buffers made (the RHI's `createNs`: their memory and objects; [RHI.md](RHI.md#the-interface)), each
counted over the walk alone, from `RD_PERF_TS_PREPASS` to the present recorded
(`rd__perf_walk_end`), and the rest. The lists' timestamps also give the list in the first-use line
(`port/render/rd_perf.c` (`walkBreakdown`, `rd__replay_list`)):

    rd: slow walk 2639.5 ms (replay 3402, frame 2431): pipelines made 2630.1 ms (1), textures and buffers made 3.2 ms (4 and 0), the rest 6.2 ms (1 so far)

(the numbers in both lines show the fields, not a measurement). Texture decodes run in the game's
step (the `slow step` line's `texture decodes`) and the replay's texture and mesh uploads in its
`upload` phase, before the walk, so neither is in it.

| the set is skipped | `<why>` in the line |
| --- | --- |
| on a device of the table (`RhiLimits.lazyPipelines`, the lazy column of `kVuPlainOnly` in `port/rhi/vk/vk_device.c`); `[dev] pipeline_set = "full"` does not override it (a `window:` line says so) | `on this device (Adreno 740, issues 93 and 107)` |
| `[dev] pipeline_set = "lazy"` (`ICO_PIPELINE_SET=lazy`, [CONFIG.md](CONFIG.md#ico-pcini)) | `by the settings ([dev] pipeline_set = "lazy")` |
| the previous start, of any version, ended without a word in the foreground while a pipeline was being made, the first such end in a row: this start only, nothing written ([PLATFORM.md](PLATFORM.md#diagnostics)) | `(the previous start ended while preparing them; for this start only)` |
| the same, the second end in a row: the setting is written once the device is made, not on a device of the table ([PLATFORM.md](PLATFORM.md#diagnostics)) | `(the previous start ended while preparing them; [dev] pipeline_set = "lazy" from now on)` |
| the last run, of the same version, ran without the set and ended, or exited from it | `(the last run made them that way)` |
| the crash order's `lazy` step | `(a restart after a graphics driver crash)` |

The table's row (`port/rhi/vk/vk_device.c` `kVuPlainOnly`; the plain-only rule of the next section
stays on the same device):

| device | match | reason |
| --- | --- | --- |
| Qualcomm Adreno 740 on Qualcomm's own driver (not Turnip) | vendor 0x5143, device 0x43050A01 (what both devices report; not Mesa's other 0x4305xxxx ids: 0x43050A00 the Adreno A32, 0x43050B00 the 740v3, 0x43051401 the Adreno 750) | issue 93 (AYN Odin 2 Portal) and issue 107 (AYN Thor), driver 512.676.53: both logs ended inside the set's 2D keys. The set was then ordered as enumerated: the 327 screen keys, the 176 VU keys, then the 24 shadow, fog, water, CRT and blur keys (527 at Original 4:3 with the fixed-function blend). The Odin 2 Portal stopped at 201 finished (the batch of keys 193 to 224 in flight), its memory falling from 194 MB to 47 MB over 22 s without progress until the system ended the app, with no crash report; the Thor froze the whole device at 307 or later (the batch of keys 289 to 320). No one module explains it (the font sheet's fragment shader, 70,444 words, and the movie decoder's, 47,609 words, were as large in a release that started on the Thor), so on this device the set is not made at all |

**The pipeline cache.** Without the set the creations are counted into batches: a batch closes at
the window's next pump once no pipeline was made for 0.5 s, or 2 s after its first
(`rd_pipelines_lazy_tick`). The pipeline cache is then written to its file
(`rhi_pipeline_cache_save`, `rhi_vk: pipeline cache: <n> bytes written to <path> in <t> ms`), so a
start the system ends or a device that freezes keeps every batch before it and the next start
compiles only the rest, and the batch's line goes to the log and to the activity file as a
`compiled: ` line ([PLATFORM.md](PLATFORM.md#diagnostics)): `rd: pipelines: compiled lazy batch 3,
pipelines 41-57 of this run (compiler E031.41.03.47): 17 pipelines 0.02-0.61 s, mean 0.14 s, over
1.8 s; memory 210 MB (most 214 MB), pipeline cache 3104 KB; slowest 0.61 s <key> (<modules>)`. In
such a run each first-use pipeline's note goes to the disc before its compile (fsync,
`act_sync_note`), so a freeze inside one still names it. The cache's size at the start is logged
twice: by the backend (`rhi_vk: pipeline cache: <n> bytes from <path>`, or `rhi_vk: pipeline
cache: starting empty (<path> holds none for this device)`) and by `rd_pipelines_lazy` (`rd:
pipelines: the pipeline cache holds <n> KB at the start; it is written after each batch of new
pipelines ...`). On a phone or a tablet (Android, iOS) the start-up set also writes the cache after
a batch that ends 5 s or more after the last write (`RD_SET_SAVE_MS`), and once more at the set's
end by the window, so a phone's set that the system ends keeps most of its work and the next start
compiles only the rest; its batches' lines carry the cache's size (`pipeline cache 3104 KB`), so a
set the system ends shows how the cache grew batch by batch. On a desktop the start-up set writes
the cache once, at its end (the window's write), and its batches' lines say `pipeline cache not
measured`: the measure (`vkGetPipelineCacheData` over the whole cache) and the writes between
batches are kept there to the lazy path and to a device of the table (`cacheKeptPerBatch`). On an
RTX 3090 with a 21 MB cache, a measure after every batch and a write or compare every 5 s took a
warm start from 1.9 s to 16.7 s and 31.2 s, every pipeline a cache hit. The cache file is written
to `<path>.tmp`, flushed to the disk during a run, then moved over the old file in one step
(`vkr_cache_write`), so a freeze mid-write leaves the old cache or the new one, never an empty or
missing file; the exit's write is not flushed, and is skipped when the cache's size is unchanged
([RHI.md](RHI.md#vulkan)).

**The batches' lines.** Every batch of the set is logged as it completes, the 2D ones too: `rd:
pipelines: compiled 2D batch 193-224 of 527 (compiler ...): 32 pipelines 0.02-0.21 s, mean 0.08 s,
0.9 s on 8 threads; memory 185 MB (most 194 MB), pipeline cache not measured; slowest 0.21 s
<key> (<modules>); each (s): ...`; the VU ones as in the next section. The memory is the process's
resident memory and its peak (`rd_set_memory_fn`, `ico_diag_process_memory`; `memory unknown`
where the system does not say), the cache's size `rhi_pipeline_cache_size` (Vulkan's data size, 0
on Direct3D 12) on a phone or a tablet, on the lazy path and on a device of the table (`pipeline
cache 3104 KB`), `not measured` in a desktop's set (the paragraph above). A start the system ended shows whether the memory grew with the batches, and the
next start logs the last one again as `ico_pc: the previous run's last finished batch: <line>`.

### The VU shader variants

Every VU vertex shader is built twice (issue 107; `port/shaders/vu_common.hlsli`, `ICO_VU_PLAIN`):
the extended variant (`vu_X_vs`) and the plain one (`vu_X_plain_vs`). The plain one leaves out the
two steps of the region rule that only a picture other than the PS2's out of the box takes: the
card test (`VU_F_EXTENDED`, [VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-region-rule)) and the two tick
cameras (`VU_F_TICKS`, the interpolation's pictures between two ticks); every other line is the
same code, so a draw with neither flag gives the same picture through either. The plain modules
are 6,216 to 23,199 words, the extended ones 10,044 to 51,132 (the vertex pre-pass's triangle
stage 19,519 and 47,637) ([SHADERS.md](SHADERS.md#the-variants)).

- **The key.** `RdPipeKeyInt.vuExt` is 1 for the extended variant, 0 for the plain one and for
  every key without a VU vertex shader. `vuDraw` takes the plain one unless the draw has
  `VU_F_EXTENDED` or `VU_F_TICKS` (`rd__vu_draw_ext`); the camera blend still reaches a plain
  draw through its blended matrices.
- **The start-up set.** The extended VU keys when the settings can make a draw that wants them
  (`rd__vu_ext_reachable`): the Enhanced preset, an aspect or a frame wider than 4:3, a scene scale
  above 1, or the interpolation on (a framerate other than `original`, which draws through the
  tick cameras whenever the camera moves). The plain ones unless no draw can take them: in a state
  other than out of the box (`rd__vu_extended`: the Enhanced preset, a frame wider than 4:3, a
  scene scale above 1) every VU draw has `VU_F_EXTENDED` and so the extended variant, so the plain
  keys are enumerated only out of the box or where the device or the settings keep the plain ones
  (`g_rd.vuPlainOnly`; `rd__enumerate_reachable_vu`). The Original preset at 4:3 and 1x with the
  original framerate compiles only plain VU shaders; the Enhanced preset or a wide frame only
  extended ones; the Original preset at 4:3 and 1x with the interpolation on both. A setting changed later to one
  that wants them creates them as the frames first need them. That holds the other way too, and
  is accepted: a start with the Enhanced preset, a wide frame or a scene scale above 1 compiles no
  plain VU keys, so a switch in the game to the Original preset at 4:3 and 1x makes the plain ones
  as the frames first need them, up to 74 pipelines with the vertex pre-pass (90 with the exact
  blend; 176 and 220 without it, [The vertex pre-pass](#the-vertex-pre-pass)), a short pause
  here and there in the first minutes after the switch; the pipeline cache keeps them for the
  next start. The set is ordered: every 2D key
  first, then the plain VU keys, then the extended ones (`orderSet`), and a batch never mixes
  extended VU keys with others, so the activity note's ` ext 1` field names a batch of them.
  Without the vertex pre-pass (`[dev] vu_prepass = false`) and with the fixed-function blend (the
  phones' usual path) the set is 351 2D keys and 176 plain VU keys, 527 in all, and 703 with both
  variants (the interpolation at the Original preset, 4:3 and 1x); with the exact blend 438 and
  220, 658 in all, and 878 with both (`RD_PIPELINE_REACHABLE_MAX`, 1024): eleven VU vertex
  shaders, 16 keys each (20 with the exact blend): TEST Z GEQUAL (the three tests of
  `rd_mesh.c` `enumerateVuVariant` give one Z test), Z write on and off, four blends, `vu_ps` and
  `vu_texa_ps`. With the pre-pass (the default) a variant has `vu_tri_vs`'s and `vu_particle_vs`'s
  keys under TEST Z ALWAYS as well (a VU draw after the game turned the Z test off:
  `queen_barrier_disp.c` `MakeRefractTexture` before the barrier mesh, `pool.c` `copyToWork`), 32
  each (40), 64 (80), and the ten pre-pass keys join the plain ones' class, so the set is 102 keys
  smaller with one variant (130 with the exact blend) and 214 (270) with both, derived from those
  counts (`rd_state_test` prints them, `testEnumerationPrepass`). The program entries keep the Z
  GEQUAL states: doubled, both variants would pass `RD_PIPELINE_REACHABLE_MAX`. Each variant has as many keys, so a set
  with the extended ones alone has as many VU keys as the Original 4:3 set. With the
  interpolation on at 4:3 and 1x a still camera's draw has neither flag and takes the plain ones,
  so that state compiles both.
- **The device table** (`port/rhi/vk/vk_device.c` `kVuPlainOnly`, `RhiLimits.vuPlainOnly`): a
  device whose driver cannot build the extended ones draws every VU draw with the plain ones,
  logged once as `rd: VU shaders: plain only on this device (Adreno 740, issue 107): the
  wide-screen rule is off`; triangles that the extended picture would clip at the 4:3 picture's
  edge are dropped as at 4:3, and the pictures between two ticks keep the blended camera's rule.

  | device | match | reason |
  | --- | --- | --- |
  | Qualcomm Adreno 740 on Qualcomm's own driver (not Turnip) | vendor 0x5143, device 0x43050A01 (what both devices report; not Mesa's other 0x4305xxxx ids: 0x43050A00 the Adreno A32, 0x43050B00 the 740v3, 0x43051401 the Adreno 750) | issue 107 (AYN Thor) and issue 93 (AYN Odin 2 Portal), driver 512.676.53, compiler E031.41.03.47: the whole device froze, needing a reboot, while the driver compiled the first pipelines with a VU vertex shader of 46,470 words and up; a set of at most 32,709 words built ([SHADERS.md](SHADERS.md#the-envelope)) |

- **After a freeze.** A batch's note goes to the disc before the batch starts (fsync,
  `port/platform/diag_host.c` `act_sync_note`). A start that ended without a word, in the
  foreground, inside a batch of extended VU keys (`startup_retry_note_has_vu_ext` on the activity
  file's note) makes the next start draw with the plain ones for that start only, nothing written
  (`ico_pc: the previous start ended while the graphics driver compiled the largest 3D shaders (the
  VU extended variants); this start only uses the smaller ones ...`); the next start that compiles
  them and ends there again writes `[dev] vu_plain_only = true` to `config.toml` and draws with the
  plain ones at every later start (`port/platform/window_host.c`, `diag_host.h`
  `ico_diag_startup_end_persist`): `ico_pc: the previous start ended while the graphics driver
  compiled the largest 3D shaders (the VU extended variants); this start and every start after it
  use the smaller ones only ...`. Nothing is written on a device of the table (`RhiLimits.vuPlainOnly`). Those starts build
  their set as usual (the cause is out of it), not lazily. A restart after a crash report takes its
  own step, and an end in the background is not counted ([PLATFORM.md](PLATFORM.md#diagnostics)).
- **Overrides.** `ICO_RD_VU_PLAIN=1` (`[dev] vu_plain_only`) keeps any device to the plain
  ones; `ICO_RD_VU_EXTENDED=1` (`[dev] vu_extended`) gives the extended ones whatever the table or
  the setting says (`rd__vu_policy_init`).
- **The log.** At `rd_init`: `rd: device compiler: Compiler Version: E031.41.03.47` (the driver's
  own line, Vulkan; `the driver names none` without one) and `rd: VU shaders: plain for the draws
  of the Original preset at 4:3 and 1x without interpolation (largest vu_lit_plain_vs 23305
  words), extended for the others (largest vu_lit_vs 51238 words)`, or the plain-only line with its
  reason. While the set is created the heartbeat line ends with the batch in flight (`| creating
  pipelines 329-360 of 702: <first key> to <last key>; vertex vu_prelit_vs 50325 words (9801
  instructions); largest fragment vu_texa_ps <n> words; compiler E031.41.03.47`: each vertex
  shader's SPIR-V words and instructions, `rd__shader_instructions`, and on a VU batch the driver's
  compiler), and each batch with a VU vertex shader is logged as it starts (`rd: pipelines:
  compiling 329-360 of 702 (extended VU): ...`) and as it completes, with every pipeline's compile
  time as the worker measured it: `rd: pipelines: compiled VU batch 329-360 of 702 (extended,
  compiler E031.41.03.47): 32 pipelines 0.41-2.10 s, mean 1.29 s, 12.3 s on 4 threads; memory 185
  MB (most 194 MB), pipeline cache 3104 KB; vu_prelit_vs 50325 words 9801 instructions: 8 x
  1.10-2.10 s mean 1.62 s; ...; each (s): 1.10 1.22 ...` (`batchTimes`; the 2D batches' lines:
  [The start-up set](#the-start-up-set)). The same line without the times goes to the start-up activity file as a
  `compiled: ` line ([PLATFORM.md](PLATFORM.md#diagnostics), `rd_set_batch_fn`), so the start after
  a freeze logs every batch the frozen one finished: a device that compiles the plain batches and
  dies on the first extended one shows its size boundary, and one that dies on plain shows it there.
  After the set the ten slowest compiles carry their modules (`(vu_lit_vs 51238 words + vu_ps <n>
  words)`) and the time by vertex shader each module's size. A log that stops inside the set names
  the module the driver was compiling. To test the size boundary on a device by hand, `[dev]
  vu_plain_only = true` keeps it to the plain variants and `[dev] vu_extended = true` gives it the
  extended ones whatever the device table says ([CONFIG.md](CONFIG.md#ico-pcini)).

### The vertex pre-pass

A mesh program's vertex shader draws the triangle `kick - 2, kick - 1, kick` at index value
`kick * 4 + corner` ([The stream bounds audit](#the-stream-bounds-audit), `shader_consts.h`
(`ICO_VU_INDEX`)), so no index repeats and the GPU reuses nothing: each of a triangle's three
invocations evaluated all three of its corners in full (the program's vertex function: two or
three matrix products, the lighting, the divide, the conversions), and a VU command drawn twice
(the AFAIL split, the two passes without dual-source blending) or as two draws a batch (the scissor
batches' fans and kicks) evaluated them again in each draw. The pre-pass evaluates each vertex of
a VU command once, before the lists, into a record; the triangle stage reads its three corners
back and runs the triangle math on them unchanged (`port/render/rd_replay_vu.c` (`rd__vu_prepass`,
`doVuDraw`); `port/shaders/vu_common.hlsli` (`vu_pre_out`, `vu_rec_vertex`);
`port/shaders/vu_tri.hlsl`). The meshes (`RDC_MESH`, `RDC_SKINNED`) and the grids (`RDC_GRID`)
take it; the particles (six invocations a sprite, a few hundred sprites a frame) keep
`vu_particle_vs`.

**The record.** Twelve 32-bit words a vertex, in three planes of `RGBA32_UINT` texels, one texel
a vertex (`shader_consts.h` (`ICO_VU_REC_*`)):

| plane | x | y | z | w |
| --- | --- | --- | --- | --- |
| 0 | `h.x` | `h.y` | `h.z` | `h.w` (the clip w) |
| 1 | `p.x` | `p.y` | GS Z (`gz`) | RGBA, a byte each, R lowest |
| 2 | S | T | Q | bit 0 the region test (`inside`), bit 1 `gsOk`, bits 2..7 the clip-space flags (`vu_clipw`) |

The floats are their bits (`asuint`), so the record holds exactly what the vertex function
computed. GS X and Y are not stored: the triangle stage takes them from `p` as `vu_divide` does,
`vu_ftoi4(p.x)`, a multiply by 16 (exact) and a truncation. A vertex's texel is its plane's
`base + v`, 1024 texels a row (`ICO_VU_REC_W`); `VuVtx` carries the clip-space flags (`clipf`)
instead of the clip-space position, which only `vu_clipw` read.

**The pass.** After the uploads and the first clears, before list 0, `rd__vu_prepass` walks the
replayed lists' mesh and grid commands. For each it sets up what `doVuDraw` binds (the stream in
the mesh arena or the ring, VuCB's VU memory, the bones and card bits in VuBoneCB: `vuSource`,
shared by both, kept for the command's draws), finds the vertices its index range reads (the
lowest kick less 2 to the highest, from the CPU's index list; a grid's every strip), and gives
them consecutive texels. Then one render pass into the three record textures (load don't care,
store), one draw a command: `n` points through the program's pre-pass entry (`vu_X_pre_vs`, the
program's own vertex function on vertex `first + SV_VertexID`, its outputs as flat `uint4`
varyings at the point's texel, `PointSize` 1) and `vu_pre_ps`, which writes them to the three
targets. The textures go to `COPY_SRC` and each plane's rows are copied into the frame slot's
record buffer, plane after plane (`rhi_cmd_copy_texture_to_buffer`, which makes the copy visible
to the vertex shaders that read the buffer: [RHI.md](RHI.md#the-interface)). A frame slot has its
own buffer, so a replay never rewrites what the GPU may still read for the previous one; the
textures are shared, ordered by their barriers. Both grow by powers of two, from 16 rows (16,384
vertices) to 1,024, every slot's buffer at once (so the replays after the one that grew them
create nothing), logged as `rd: VU pre-pass: the record holds 32768 vertices (32 rows, 1536 KB a
frame slot)`; group 1 binds the record at t4, and a 256-byte buffer before there is one (the slot's
upload ring when even that could not be made, logged once: no shader reads t4 then). A record that
cannot be made draws that replay the old way and is asked for once more at the next replay; a
second failure in a row turns the pre-pass off for the run (`rd: VU pre-pass: off for the run: the
record (R rows, K KB a frame slot) could not be made twice; ...`), so no replay asks for it again
(`recEnsure`, `s_recFails`). `rd_stream_audit` does not model the record's reads: they are in range
by construction (a command's vertices from its lowest kick less 2, at least 0, to its highest,
under its vertex count, at consecutive texels under the record's rows: `vuPreRange`,
`rd__vu_prepass`). A
command past the record, or one whose pre-pass pipeline is missing, is drawn the old way through
its program's entry, and so is a draw whose `vu_tri_vs` pipeline cannot be made, each logged
once (`rd: VU pre-pass: a frame of N vertices past the record's M ...`, `... the pipeline of
vu_X_pre_vs could not be made ...`, `... a pipeline of vu_tri_vs could not be made ...`) and
counted (`vuPreFallbacks`); while the loss bisect only names the keys a frame needs
(`rd__pipeline_gathering`) a missing pipeline is not a fallback, so the pre-pass's and the
triangle stage's keys are the ones named. The pass ring holds the pass as `VU pre-pass, N
vertices into the record (1024xR)` with its pipeline ([The pass ring](#the-pass-ring);
`port/render/rd_replay_vu.c` (`rd__vu_prepass`, `vuSource`, `vuPreRange`, `recEnsure`)).

**The triangle stage.** `vu_tri_vs` (and `vu_tri_plain_vs`, [The VU shader
variants](#the-vu-shader-variants)) serves every mesh program: it reads the record buffer (t4 in
group 1, `StructuredBuffer<uint4>`, nine loads an invocation) for `kick - 2`, `kick - 1` and
`kick`, and calls `vu_mesh_out` as the program's entry did, with the program's own choices passed
in VuCB instead of written in the entry: `vu_rec.w` holds the clip mode the program uses (normal_c
takes VuCB's, normal_l's code 32/36 the scissor mode or the region test, codes 34 and 38, the
cluster and the grids the region test), whether the chain clip plane and the later mark apply
(normal_c and normal_l) and whether the content table's HIDE mark is tested (normal_c and
normal_l); `vu_lo` and `vu_hi` hold the region test's bounds (the cluster's VU memory 0 and 1,
the others 0 and (4094, 4094, 0, 16777214)). The marks, the card bits, the tick cameras, the
cut, the planes, the plane distance (read from the stream) and the probe are those of the
program's entry. A pipeline of `vu_tri_vs` does not depend on the program, so the key's program
is normalised (`RD_PROG_PRELIT`) and the programs share their pipelines.

**The keys.** With the pre-pass on, the VU keys are those of `vu_tri_vs` and `vu_particle_vs`
under the VU states (each variant as [The VU shader variants](#the-vu-shader-variants) says), and
the ten pre-pass keys (`rd__vu_pre_key`: the program's `vu_X_pre_vs` with `vu_pre_ps`, three
`RGBA32_UINT` targets, no depth, points, no blend); the program entries' keys are made only for a
command drawn the old way. `[dev] vu_prepass = false` (`ICO_RD_VU_PREPASS=0`,
[CONFIG.md](CONFIG.md#ico-pcini)) draws every VU command the old way and enumerates the program
entries' keys, for an A/B on a device.

**The devices.** Only what every device the port runs on has: render passes into colour targets,
point lists, vertex shaders reading a read-only storage buffer (as the stream already is), and
buffer copies. No compute (the RHI has none), no storage writes from a vertex shader
(`vertexPipelineStoresAndAtomics`, which the port never enables and phones' drivers do not all
offer), no vertex texture fetch (`tools/check_spirv.py`'s vertex-shader opcode set holds no image
instruction). `RGBA32_UINT` is a colour attachment and a sampled format on every Vulkan device
(the specification's required format support) and a render target on Direct3D 12 at feature
level 11_0; three targets are inside every device's colour attachments (at least 4 in Vulkan, 8
on Direct3D 12). The one bound the record meets is Metal's: a pixel's colour targets may take 32
bytes together on Apple A9 and A10 GPUs, 64 on A11 to A13 and 128 from A14 and M1 (Metal Feature
Set Tables, "Maximum implicit image block size, per pixel, per sample, when using multiple color
render targets"); the record's 48 bytes do not fit the first, so under MoltenVK on an A9 or A10
the pre-pass is off (`RhiLimits.colorBytesPerPixel`; `port/rhi/rhi_driver_version.c`
(`rhi_metal_color_bytes`), from the Metal device's name) and the log says so once. An Apple GPU
name the table does not read is taken as unbounded (the families it names only grow), logged as
`rd: VU pre-pass: the Apple GPU "<name>" is not one the colour-target table names: ...`; a
pipeline Metal then refuses, or a wrong picture, is the start-up self-test's to catch (below). At start the
renderer logs `rd: VU pre-pass: on: each mesh and grid vertex evaluated once before the lists, 48
bytes a vertex in 3 RGBA32_UINT targets; [dev] vu_prepass = false draws the old way`, or `rd: VU
pre-pass: off by the settings ([dev] vu_prepass = false, ICO_RD_VU_PREPASS=0): ...` or `off on
this device (its colour targets hold 32 bytes a pixel together, the record 48: an Apple A9 or A10
GPU): ...` (`port/render/rd_replay_vu.c` (`rd__vu_prepass_init`)).

**The self-test.** The pre-pass's pipelines are shapes no other draw of the port has (a vertex
shader writing `PointSize` into a point list, flat `uint4` varyings into three `RGBA32_UINT`
targets, a texture-to-buffer copy a vertex shader reads in the same list), so a driver that cannot
make one would leave every VU pipeline to be made on first use (the start-up set holds the
triangle stage's keys, not the programs'), and one that miscompiles them would draw the scenery
wrong with no error. `rd_vuprepass_selftest` checks it on the device at start, after the blend's
self-test and before the start-up set (`port/platform/window_host.c`; in a run without the set,
after `rd_pipelines_lazy`): a frame of its own, never recorded, on SCENE at its real size and
scale without MSAA, draws three meshes made for it (`rd_create_vu_mesh`: a prelit strip through
normal_c code 32, a lit one through normal_l code 32 and a skinned one through cluster code 20
with two bones, 12 vertices and 8 triangles each, overlapping at other depths, Z GEQUAL with Z
write, untextured) with `vu1_test`'s scene in VU memory, once with the pre-pass off (the programs'
entries, the reference) and once with it on, in the VU shader variant the settings draw with, and
reads the block back after each. It passes when the replay with the pre-pass drew every command
through it (`RdPerfRecord.vuPrePasses` 1, `vuPreDraws` 3, `vuPreFallbacks` 0) and the two blocks
are equal byte for byte. A difference, a pipeline the pre-pass could not make or a device error
turns the pre-pass off for the run, so the start-up set made next holds the programs' keys:

```
rd: VU pre-pass: on (self-test: prelit, lit and skin byte for byte, 36 vertices in 3 pre-pass draws, <n> of <m> pixels drawn) on SCENE <w>x<h> at <tw>x<th> texels in <t> ms
rd: VU pre-pass: off (self-test: the pictures differ: <n> of <m> bytes, the first at texel <x>,<y>: <r>,<g>,<b>,<a> want <r>,<g>,<b>,<a>) on SCENE ...
rd: VU pre-pass: off (self-test: a pipeline could not be made (<d> of 3 commands through the pre-pass, <f> draws the old way)) on SCENE ...
rd: VU pre-pass: off (self-test: the device failed in the replay with the pre-pass) on SCENE ...
```

A start whose pre-pass is already off (the settings, the device, the host's
`rd_set_vu_prepass_off`: a restart step or an end in its compile) runs nothing and logs `rd: VU pre-pass: off (self-test not run)`; a frame
it could not build, or one in which neither replay drew the meshes, leaves the pre-pass on (`on
(self-test not run: ...)`, `on (self-test inconclusive: ...)`). The summary line carries the
verdict (`VU pre-pass on (self-test: ok)`, `VU pre-pass off (self-test: <reason>)`,
[PLATFORM.md](PLATFORM.md#the-summary-line)). A crash in one of its pipelines is noted as the
start-up set's would be and takes the crash order's `noprepass` step; a start that ended without a
word in one of the pre-pass's pipelines or batches makes the next start draw without it, and the
second such end in a row writes `[dev] vu_prepass = false`; a device loss in a run drawing with
it takes the loss order's `lossnoprepass` step first ([PLATFORM.md](PLATFORM.md#diagnostics)). It
tests the variant the start draws with:
a later change of settings that moves the draws to the other variant is not tested again
(`port/render/rd_vuprepass_selftest.c`; `port/render/rd.h` (`rd_vuprepass_selftest`);
`ICO_RD_VU_PREPASS_SABOTAGE=1` draws the first command's records a texel off,
[TESTING.md](TESTING.md#environment-switches)).

**Why the picture is the same.** The pre-pass entry calls the program's vertex function, the same
source with the same `precise` operations, so each recorded word is the value the old entry
computed for that corner (the shaders keep the precise flags of `port/shaders/CMakeLists.txt`;
`precise` forbids contraction and reassociation of each operation it marks). The record carries
the bits through integer targets and a buffer copy, with no float conversion on the way, and the
triangle stage reads them back as `asfloat`. The one value recomputed, GS X and Y from `p`, is
`vu_divide`'s own operation on the same bits, for a finite `p`: a NaN `p` (only a garbage stream
gives one) converts as each module's `OpConvertFToS` does, which SPIR-V leaves undefined
(Direct3D makes it 0), so two modules may differ there. The triangle math (`vu_mesh_out`: the pick, the
region rule, the cut, the ticks, the card test, the corner rule, the planes, the plane distance,
the later bias) is unchanged and is given the same values, with the program's clip mode, bounds
and flags from VuCB equal to the constants its entry used. Within the old shaders the three
copies of a vertex (as corner `c` of triangle `k`, `b` of `k + 1`, `a` of `k + 2`) were three
inlined evaluations of one expression; the record makes them one evaluation. `vu1_test` draws
every case both ways, probe and pictures, and compares them byte for byte
([TESTING.md](TESTING.md#portshaderstest)).

**The saving.** Each vertex function runs once a vertex a command instead of three times an index
of each of the command's draws, and an invocation keeps the triangle math and nine loads. By the
modules' size (`tools/check_spirv.py --sizes`, instructions) a vertex function with its packing is
416 (`vu_grid_pre_vs`) to 627 (`vu_skin_debug_pre_vs`) instructions in 29 to 36 blocks, three of
which an invocation of a program entry ran; `vu_tri_vs` is 10,091 instructions against
`vu_prelit_vs`'s 10,727 ([SHADERS.md](SHADERS.md#the-variants)). The replay's record counts the
pre-pass (`RdPerfRecord.vuPreVertices`, `vuPreDraws`, `vuPrePasses`, `vuPreFallbacks`,
`gpuPrepassMs`); the window's 10 s lines and the replay tool's `--stats` print them, so a log
shows the pre-pass's own GPU time next to the lists' ([Performance
records](#performance-records)).

## Destination reads and blend paths

The GS blends in integers with an arithmetic shift, no clamp of C, then COLCLAMP, PABE and FBA;
the GPU's fixed-function blender computes in float and rounds to nearest, one level above the GS
on many LERP channels. The destination read computes the GS result in the fragment shader from the
pixel's own destination, read in rasterization order, with blending off
(`gs_dst_channel`, `port/shaders/gs_math.hlsli`) (`port/render/rd_dst_read.c`). Two realisations
(`port/rhi/rhi.h` (`RhiDstRead`); `port/render/rd_dst_read.c`):

- **attachment**: the colour attachment read as an input attachment in rasterization order
  (Vulkan rasterization-order attachment access over dynamic rendering local read). Depth,
  stencil, discard and write mask stay fixed-function. Every pass on a readable RGBA8 target is
  begun readable.
- **storage**: the target as a rasterizer-ordered view (D3D12 ROV; Vulkan storage image under
  fragment shader interlock). The target is then no colour attachment, so a blended draw runs in a
  storage pass of its own with the depth attached. A draw that writes Z and can discard is split
  into colour without Z write then a depth-only pass when its Z test is ALWAYS; under
  GEQUAL/GREATER it stays on the fixed-function blend (logged once).

The preference is attachment, then storage, then the fixed-function blend. What goes through it:
the screen prims, the VU meshes and the fog sprite whose fixed-function blend is not the GS's (the
LERPs, mode 3, the Ad forms, mode 11, PABE on the premultiplied forms, COLCLAMP 0) on RGBA8
targets. What stays on the fixed-function blend: the premultiplied forms, the MSAA epoch's passes,
targets other than RGBA8, the port's presentation overlay. Of the premultiplied forms the adds
(modes 0 and 5) are exact on the blender (the shader writes the GS term `(Cs F) >> 7` and the
blender adds two whole levels); the subtracts (modes 1 and 6) are one level above the GS wherever
`Cs F` is not a multiple of 128, because the GS's term is `(0 - Cs) F >> 7`, an arithmetic shift
that rounds the negative product down, while the blender subtracts the truncated `(Cs F) >> 7`
(`port/shaders/common.hlsli` (`gs_dual_out`); `port/shaders/gs_math.hlsli` (`gs_blend_ch`)). The staticBlur sprites
and `blend_int` keep their own exact paths (`port/render/rd_dst_read.c`; `port/render/rd_replay.c`
(`dstPassMode`, `rd__pass_wants_readable`)).

`ICO_RD_BLEND` (`[dev] blend`, [CONFIG.md](CONFIG.md)) names the path instead: `fixed` (also
`off`, `0`), `storage` (also `interlock`, `rov`) or `attachment`; a value that is none of these or a
path the device lacks logs `rd: blend: ICO_RD_BLEND=<v> is not fixed, storage or attachment` or
`... is not on this device` and keeps the default (`port/render/rd_dst_read.c` (`rd__dst_init`)).

**The Qualcomm rule.** The attachment read is not offered on Qualcomm's own Adreno driver
(vendor 0x5143 with `VK_DRIVER_ID_QUALCOMM_PROPRIETARY`, or with the id unknown; Mesa's Turnip on
the same GPUs reports its own id and keeps it). On the Odin 3 (Adreno 830, driver 512.800.72,
issue 89) it returned wrong destination colours: white boxes around the leaves' clear texels, the
title washed out, the menu panels white on white; the same title frame replays through the same
path on lavapipe with no validation error and within one level of `ICO_RD_BLEND=fixed`, so the
driver is wrong, not the port's use. The device line says so,
`rhi_vk: destination read: interlock no, rasterization-order attachment read off on this driver
(Qualcomm driver 512.800.72: wrong results on Adreno; ICO_VK_ATTACHMENT_READ=1 tries it)`, and the
blend line carries the reason, `rd: blend: fixed-function (rounds up) (attachment read off:
Qualcomm driver 512.800.72)`. No target is then made with the input attachment usage and no pass
is begun readable: the picture is the fixed-function blend's. `ICO_VK_ATTACHMENT_READ=1`
(`[dev] attachment_read = true`) offers the read anyway; `ICO_VK_FAKE_ADRENO=1` takes the branch on
any device, for tests (`port/rhi/vk/vk_device.c`; `port/rhi/rhi_driver_version.h`
(`rhi_driver_is_qualcomm_proprietary`); `port/rhi/rhi.h` (`RhiLimits.rasterOrderOff`)).

**The MoltenVK rule.** On MoltenVK (macOS and iOS; `VK_DRIVER_ID_MOLTENVK`, or Apple's vendor
0x106B with the id unknown on the builds that link MoltenVK; `RhiLimits.moltenvk`, shown as
`; MoltenVK` on the `rhi_vk:` device line) the default blend is the fixed-function one even though
the interlock is offered (the reporter's M2 Pro, MoltenVK 1.4.2: `interlock yes,
rasterization-order attachment read no`, issue 102). The exact path's cost is its pass switching
(`dstPassMode`, `port/render/rd_replay.c`): a contributor's M1 replay of the reporter's dump took
54 ms and 750 passes with MSAA off and the interlock, 16.7 ms and 59 with MSAA 2x (which draws
fixed-function), and 14 ms and 49 with MSAA off and `ICO_RD_BLEND=fixed`: 3 to 4 times the frame
for the exact blend's last level of rounding. iOS already had the rule (a `setenv` in
`port/platform/ios/main_ios.c`, A14: the interlock also regressed the picture); it is now this one,
in `rd__dst_init`, for both. The line is `rd: blend: fixed-function (rounds up) (MoltenVK: the exact
blend's pass switching costs 3 to 4 times the frame; [dev] blend = storage keeps it)`, and the
`ico_pc: summary:` line's blend text is the self-test's, `fixed-function (rounds up) (self-test:
exact modes not tried: MoltenVK default is fixed-function; fixed-function ok N)`: only the
fixed-function reference runs (A14's interlock regressed the picture, and the exact modes'
pipelines would be compiled for nothing). The start-up pipeline set holds no destination-read keys
on it: `rd__enumerate_reachable` adds them only when the mode in force is not OFF, and
`rd__dst_init` and the self-test settle the mode before the set is made. The destination-read
shader modules (`_dstrov`) are still created with the device, as before.
`[dev] blend` / `ICO_RD_BLEND` forces any mode as everywhere (the init line then ends `(forced:
MoltenVK's default is fixed-function)`, and the self-test tries the forced mode as everywhere).
`ICO_VK_FAKE_LIMITS=moltenvk` reports MoltenVK, with no
attachment read, on any device (`rd_blend_selftest_moltenvk`, `rd_blend_selftest_moltenvk_storage`).

**The blend self-test.** A driver whose read returns the wrong colour draws a wrong picture with no
error anywhere, so `rd_blend_selftest` checks the modes on the device at start, as the fog's
self-test checks its depth reads ([The fog path and its self-test](#the-fog-path-and-its-self-test)). A frame of its
own (never recorded or presented) is drawn on SCENE at its real size and scale, MSAA held off, in a
256 x 64 GS pixel block in its middle: eight cells of PCSX2's forest pixels under the forest's tint
sprite (`rd_pixel_test.c` `kTint`), a textured LERP quad whose clear texel must leave the colour
behind it (the leaf case), two overlapping LERP draws (the second must blend over the first's
result) and a glyph through the text sheet's shader. Nineteen samples are copied out and read back.
The fixed-function blend runs first as the reference (within 2 levels of the GS arithmetic; if it
fails, the two-pass fallback is tried and kept when it passes), then the exact modes the device has
in the order above, each within 1 level of the GS arithmetic and of the reference's glyph. The
first exact mode that passes is the one in force; none passing leaves the fixed-function blend. The
window runs it before the start-up pipeline set (so the set is the chosen mode's), and again
whenever SCENE is made at a new size. One line names the choice and every result:

```
rd: blend: exact (attachment read) (self-test: attachment ok 1, storage not on this device, fixed-function ok 1) on SCENE 640x448 at 1920x1344 texels in 318.3 ms
```

(lavapipe at 3x: the glyph one level off the fixed-function reference, the blender's rounding;
the time is mostly the first compile of its pipelines, a run at a new scene size takes 50 to 90 ms
there). A failing mode's worst sample, and a passing one's when it is not exact, gets a line of its
own, `rd: blend: self-test: attachment fail 241 at tint cell 0 (texel 625,625): 255,255,255,4
want 21,28,14,4` (read white by `ICO_RD_BLEND_SABOTAGE=attachment`). An `ICO_RD_BLEND` of `attachment` or `storage` is kept whatever its result (the
line adds `; kept by ICO_RD_BLEND`); `ICO_RD_BLEND=fixed` (also what the restart after a crash in a
destination-read pipeline sets) and a device without an exact mode run nothing
(`rd: blend: fixed-function (rounds up) (self-test not run: ...)`). `ICO_RD_BLEND_SABOTAGE=`
`attachment,storage,fixed` makes those modes' samples read white, for the fallback's tests
(`port/render/rd_blend_selftest.c`; `port/render/rd.h` (`rd_blend_selftest`);
`port/platform/window_host.c`).

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
| REDUCTION | `gsb_Reduction` | DISPLAY cleared, SCENE drawn into it at half height, bilinear, tinted, inside the border-crop scissor (2 pixels left and right; 8 lines top and bottom on a 512-line scene, 2 on 448); both sprites through the GS sprite model |
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

`RD_POST_SHADOW_RESOLVE`, `RD_POST_BLUR` and `RD_POST_PRESENT_BLIT` are reserved numbers no caller
issues; `rd_post` records nothing for them (logged once) (`port/render/rd_post.c`).

**The staticBlur sprites.** `staticBlur.c`'s host path records its register writes as rd state and
each sprite as `rd_post` of its effect's kind: motion blur (DISPLAY read as RGB24, H/2 stretched to
H, LERP FIX), depth of field (SCENE to WORK1 to WORK0, six blur passes, four planes), glow (flare in
modes 0 and 1, bloom in mode 2 with HIGHLIGHT), aura (AURA_WORK, AURA_TAP, FEED128 feedback) and the
sun's eye blur (WORK0 to WORK3) (`port/render/rd.h` (`RdPostKind`)). `rd_blur.c` records each as an
`RdPostRec` with the corners, UVs, RGBAQ and Z as the GS gets them, the
[TEX0](ARCHITECTURE.md#terms) size and TFX, and `doBlurSprite` draws it through `fx_rect_vs` /
`fx_sprite_ps` in the GS integer arithmetic (`port/render/rd_blur.c`, header comment):

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
border shows the picture SCENE holds (`port/render/rd_replay.c` (`borderOpen`, `doBlurSprite`)).

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
draw's wrap in `DF_TEXA_CLAMP_S/T` (`port/render/rd_replay.c` (`spriteAbove1x`, `fillDrawCB`)).
These are the shadow composites (the levels at scale 1 into SCENE, 1/2s to 1/8s texel a pixel), the
softening's SCENE into AA0 (a box), and the reduction's fallback sprite (SCENE
into DISPLAY on a scaled target, `rd__blur_screen_fallback`) when an axis leaves the GS's filter at
its steps as `blurAbove1xSteps` computes them; at full height and scale 1 DISPLAY has twice the GS's
rows (sy 2) and the reduction steps one SCENE row a DISPLAY row, the GS's filter on both axes, so
the flag stays off there (`Replay.a1xGs`; `port/render/rd_replay.c` (`doBlurSpriteDraw`)). The
shader takes the step of each axis
from the pixel's footprint (`ddx`/`ddy` of the texel coordinate, constant over a sprite, so a draw
of several sprites gets each one's own step), in 1/32 texel (`port/shaders/sprite.hlsl`
(`sprite_above1x`)):

- both axes below half a texel: the B-spline as four bilinear taps of the draw's own sampler
  (`gs_bspline_bilinear4`), TEXA after, as `sprite_ps` applies it; within 2 LSB of the integer
  kernel;
- an axis above two texels: the area box on that axis, every tap loaded with the wrap reproduced on
  the texture's size and TEXA per texel (`gs_texa_load`), combined separably as `fx_sprite_ps`
  combines them, exact;
- otherwise the pass samples as before (`gs_sample`).

The B-spline serves the shadow composites (a work buffer never meets it: [The glow
size](#the-glow-size)): it spreads a level texel with a
standard deviation of 0.58 texel against the bilinear's 0.41, so the penumbra is a little softer,
without the bilinear's corners along a diagonal; `rd_shadow_test` checks each composite at 4x
against the CPU B-spline and that a row across the shadow falls and rises once
(`port/render/test/rd_shadow_test.c` (`checkComposite4x`)). The GS
levels' integer filter of the Original preset (`rd__gs_filter_int`) is for images only, so it never
meets the flag (`port/render/rd_pipeline.c` (`rd__gs_filter_int`)). The flag is never set when the
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

**The present.** Without anti-aliasing, a scaled scene with more than two texels a window pixel on
an axis (a fixed resolution above the window's) is box-averaged to the box first, as
anti-aliasing does, since the box blit's one bilinear tap would skip texels there
(`port/render/rd_present.c` (`reduceRecord`)). Never at scale 1.

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
`doShadowResolve`)).

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
rd: dump: frame N: water reflections original|sharp; scene SXxSY; block WxH GS with depth at TWxTH texels (SXxSY, the scene's scale|not the scene's scale): D draws into it, R read it (DATE off a, alpha 0 b, alpha 1 c, AFAIL rgb-only, blends 0, REPEAT r, k of them wholly past the first period); DATE draws on SCENE: alpha 0 b, alpha 1 c, AFAIL rgb-only, blends 0 2; Full pixel on|off, border mask on|off; blend exact (interlock|rov|attachment read)|fixed-function (rounds up), storage pass merge on|off; K draws sampled a texture-pack replacement (blank in the dump), P of them DATE draws; MSAA 8x (resolve: colour the mean of the samples, depth the smallest sample): the last replay's DATE snapshots of SCENE S, A of them read the resolved picture (C closed the epoch), of other targets O
```

(one line; a part with nothing to say is left out): each render-to-texture block a draw of the frame
wrote or read, at which scale it was drawn, the draws into it and the DATE, AFAIL, blends and wrap
of the draws that read it (`REPEAT r`: the reads on REPEAT on an axis; `k of them`: screen prims
whose coordinates lie wholly past its first period there, which keep the wrap under an open border:
the ripples, [DISPLAY.md](DISPLAY.md)), the DATE draws by target, the border and the blend path in
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

## The fog path and its self-test

The depth fog reads SCENE's depth: the GS copies the Z buffer, a PSMT4 view copies the third byte
of every word into the fourth, and the fog sprite reads the top byte through the 256-entry CLUT,
so the index is bits 16..23 of the 32-bit Z and only pixels with Z <= 0xFFFFFF are fogged
(`port/shaders/fog_lut.hlsl`). The replay reads the depth one of three ways
(`port/render/rd_fog_path.c` (`RdFogPath`)):

| path | how |
| --- | --- |
| inplace | the depth target sampled where it is |
| copy | a whole copy into a second depth texture, sampled |
| buffer | the depth aspect copied into a buffer and back into an R32_UINT texture (Vulkan only) |

`rd_fog_selftest` replays a frame of its own (never recorded or presented) on SCENE at its real
size and scale: a 16 x 16 grid of cells at known GS Z and the fog with a LUT whose every entry
differs, read back and compared with the GS arithmetic, for each path in the platform's order
(desktop: inplace, copy, buffer; tile-based or D24S8: copy, inplace, buffer). The first that passes
is the path from then on, and one log line `fog: depth path` names it with every result. It runs
after `rd_precreate_pipelines` and again whenever SCENE is made at a new size. `ICO_RD_FOG_PATH`
or `ICO_RD_DEPTH_COPY` names a path to try first; `ICO_RD_FOG_PATH=test` tries every path
(`port/render/rd_fog_path.c`; `port/render/rd.h` (`rd_fog_selftest`)). The probe logs five
texels' Z, index and colour on the first fogged replay after start and after a new scene size
(`fog: probe`) (`port/render/rd_fog_path.c`). Both draw the fog at the exact table entry of
each index, not read between entries above 1x, since what they measure is the depth read; the
probe's frame is one game frame drawn that way (`port/render/rd_fog_path.c` (`rd__fog_exact`),
`port/render/rd_replay.c` (`doFog`)).

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
`tools/content_table_check.py chains`.

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

## Camera blending

The presenter draws the pictures between two ticks through a camera blended from the two ticks'
cameras: the inverse views blended as affine matrices (the 3 x 3 split by polar decomposition
into a rotation, slerped, and a stretch, lerped; the eye lerped) and the projection lerped element
by element. The game's cull has to know that camera too, so these header-only functions are the
one implementation, included by `rd_interp.c` and `GsBase.c` in every build
(`port/render/rd_camera_blend.h` (`rdcb_blend_view`, `rdcb_blend_proj`, `rdcb_camera_jump`)). A
step past `RD_INTERP_CAMERA_TURN` (30 degrees) or `RD_INTERP_CAMERA_MOVE` (300 units) a tick is a
cut, and `RD_CULL_MARGIN` is the cull's frustum margin (`port/render/rd_camera_blend.h`).

## Interpolation and presentation

With `RdSettings.interpolate` on, `rd_end_frame` only closes the frame and the host calls
`rd_present(alpha)` (the [present](ARCHITECTURE.md#terms)) as often as the display allows; what
blends and how is [DISPLAY.md](DISPLAY.md#interpolation-between-ticks)'s
(`port/render/rd_interp.c`). `rd_present.c` shows DISPLAY in a box of the frame's aspect on the
output, `rd_crt.c` replaces that blit under the CRT filter, and `rd_video.c` shows the FMV picture.
All of it is [DISPLAY.md](DISPLAY.md).

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
fog, motion blur, aura, glow, depth of field, softening, CRT, MSAA resolve)
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
A draws resampled above 1x`, and the VU vertex pre-pass as a third, `<dump>: stats: VU pre-pass
on: 1 passes, V vertices in D draws; F mesh and grid draws the old way` (`vuPrePasses`,
`vuPreVertices`, `vuPreDraws`, `vuPreFallbacks`, [The vertex pre-pass](#the-vertex-pre-pass));
the GPU line (`window: GPU ms per replay: ...`) ends with `; VU pre-pass <ms>`, as `--stats`'s
`gpu:` line does
(`port/render/rd.h` (`RdPerfRecord`); `port/platform/window_host.c` (`perf_log`)).

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

Measured or worked out, not done:

- **A UINT view of the UNORM targets.** `fx_sprite_ps` and the TEXA paths read each texel as a
  float and turn it back into an integer (`floor(x * 255 + 0.5)` a channel, `fx_load`,
  `gs_texa_load`). An `RGBA8_UINT` view of the same `RGBA8_UNORM` image would hand the integer
  over directly, exactly, but needs images made with a mutable format (Vulkan's
  `VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT` and a view format list; a typeless resource on D3D12),
  which the RHI does not make ([RHI.md](RHI.md)).
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
sum of the lists, not one list or effect.

## Dumps and the replay tool

A [.rddump](ARCHITECTURE.md#terms) is the in-memory frame, little-endian, versioned by
`RD_DUMP_VERSION` (10): the frame's native size and aspect, its camera, start and end states, the
13 lists, the payload, the textures and temporary targets it references, and its VU meshes. Target
contents are not written: a frame that reads a retained target replays from what the loading
context holds. Loading creates the textures and targets anew and rewrites every id, and gives the VU
draws recorded without card verdicts theirs ([VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-card-test)), so
a dump replays alike at every setting. Dumps hold game assets and are never committed
(`port/render/rd_dump.c`, header comment).

Dumps are written by F12 and Shift+F12 ([PLATFORM.md](PLATFORM.md#hotkeys-and-dump-zips)) and by
`[dev] dump_every = N` (with `dump_dir`, `dump_interp`, `dump_from`), which the host hands to rd as
`ICO_RD_DUMP_EVERY`, `ICO_RD_DUMP_DIR`, `ICO_RD_DUMP_INTERP` and `ICO_RD_DUMP_FROM`; `dump_interp`
also writes the frame before (`-prev`) and the frame blended half way (`-i50`)
(`port/platform/window_host.c` (`frame_dump`); `port/platform/host_config.c` (`export_dump_keys`);
`port/render/rd_core.c` (`readDumpConfig`, `rd_end_frame`)). `rd_png.c` writes uncompressed PNGs for
the tool and the tests (`port/render/rd_png.c`).

`rd_replay_tool <dump> <out.png>` loads a dump (or the first `.rddump` in an F12 zip) and renders
it headless (`port/render/tools/rd_replay_tool.c`, usage comment):

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
| `--nop L:A[-B]` | turns commands into NOPs, to find the draw behind a pixel |
| `--mesh NAME` | a VU mesh vertex by vertex, with the kick, overlap and joined marks |
| `--dump-textures DIR` | every image texture as decoded |
| `--stats` | the replay's performance record, with GPU times |
| `--prev DUMP` | replays earlier dumps first so retained targets hold the run's pictures |
| `--no-half`, `--no-aa1` | the frame without the field's half pixel or without PRIM.AA1 |
| `--interp T PREV` | the frame the presenter builds between PREV and the dump at alpha T |
| `--interp-any`, `--no-reproject` | with `--interp`: PREV taken as the tick before whatever its frame number; the CPU-projected draws blended in screen space |
| `--aa MODE` | (with `--enhanced`) the anti-aliasing setting: off, ssaa2, ssaa4, msaa2, msaa4, msaa8 |
| `--crt-strength K` | after `--crt`, the strength 0..1 |
| `--overlay-test` | (with `--present`) a test pattern and a popup drawn as a presentation overlay |
| `--vu-ref L:I` | the CPU mirror's vertices of prelit draw I of list L |
| `--loss-bisect` | the [loss bisect](#the-loss-bisect) on the dump: the frame one pass longer each time, waiting for the GPU after each |
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

# Shaders

The port's shaders are HLSL in `port/shaders`, compiled at build time by DXC to SPIR-V for Vulkan
and signed DXIL for Direct3D 12 and embedded in the program as byte arrays; there is no compiler at
run time. This document covers the build, every source file, the binding scheme, the constant
layouts shared with C and the tests. Which entry the [replay](ARCHITECTURE.md#terms) binds for what
is [RENDER.md](RENDER.md); the VU programs' rules are [VU1_PROGRAMS.md](VU1_PROGRAMS.md); the
interface that loads the blobs is [RHI.md](RHI.md).

## Files

| path | what it holds |
| --- | --- |
| `cmake/IcoShaders.cmake` | `ico_add_shader`, `ico_shaders_library`, the DXC flags |
| `port/shaders/CMakeLists.txt` | every entry: name, file, entry point, stage, defines, suffix |
| `port/shaders/embed_shaders.py` | the manifest to `shaders_gen.c`/`.h`, with the blob checks |
| `port/shaders/common.hlsli` | what every shader shares: FrameCB, DrawCB, the `DF_*` flags, sampling, outputs, the destination read, vertex helpers |
| `port/shaders/gs_math.hlsli` | the GS integer arithmetic, scalar, valid HLSL and C |
| `port/shaders/sheet_text.hlsli` | the sheet texel arithmetic of `font_sheet_ps`, mirrored on the CPU |
| `port/shaders/vu_common.hlsli` | what the VU program shaders share ([VU1_PROGRAMS.md](VU1_PROGRAMS.md)) |
| `port/shaders/shader_consts.h` | the C side of the byte layouts and constants |
| `port/shaders/*.hlsl` | the families, below |
| `port/shaders/test/` | the shader tests and `hlsl_shim.h` |
| `port/test/gs_blend_test.c` | a standalone CPU program on the GS blender's drift |

## The build

`cmake/IcoShaders.cmake` runs one DXC invocation per entry and target.
`ico_add_shader(<name> <file.hlsl> <entry> <stage>)` registers an entry (stage `vertex` or
`fragment`, `vs` or `ps` accepted; profiles `vs_6_0` and `ps_6_0`); `DEFINES <D=V>... SUFFIX <s>`
compiles the same entry with macros (DXC `-D`) under the key `<name><s>`.
`ico_shaders_library(<target>)`, after the last entry, writes the manifest and builds the static
library holding `shaders_gen.c` with `shaders_gen.h` on its public include path
(`cmake/IcoShaders.cmake`, header comment).

**Flags.** SPIR-V: `-spirv -fspv-target-env=vulkan1.2` with `-fvk-b-shift 0`, `-fvk-t-shift 16`,
`-fvk-s-shift 32`, `-fvk-u-shift 48` (all spaces), so a register number is the
[RHI](ARCHITECTURE.md#terms) slot and the Vulkan binding is the slot plus 0, 16, 32 or 48. Both
targets: `-O3 -WX -I port/shaders` (a shader warning fails the build; no `-Zi`, no debug info in the
blobs) (`cmake/IcoShaders.cmake` (`ICO_DXC_SPIRV_FLAGS`, `ICO_DXC_COMMON_FLAGS`)).

**Outputs.** Each entry writes `<build>/shaders/<name>.spv` and, with `ICO_SHADERS_DXIL` on (the
default), `<name>.dxil`. With Ninja or Makefiles a second DXC command writes a `-MF` dependency
file, so a header edit rebuilds only the shaders that include it; other generators depend on every
`.hlsli` (`ico_add_shader`). `ICO_SHADERS_DXIL=OFF` compiles SPIR-V only, recording `-` for the DXIL
file; the presets without the D3D12 backend set it (`linux-arm64`, `android-arm64`, `ios-moltenvk`,
`macos-moltenvk`) and the library then defines `ICO_SHADERS_NO_DXIL` for its users
(`cmake/IcoShaders.cmake`; `CMakePresets.json`).

**Embedding.** `ico_shaders_library` writes `manifest.txt` (one `name|stage|entry|spirv|dxil` line
per entry, replaced only when it changed) and runs
`embed_shaders.py MANIFEST OUTDIR "DXC <version>"` (the version from `dxc --version`), which writes
`shaders_gen.h` and `shaders_gen.c`: the byte arrays and the table `g_icoShaders` of `IcoShaderBlob`
(name, stage, SPIR-V and its length, DXIL and its length or NULL and 0, the entry point),
`g_icoShaderCount`, and `ico_find_shader(name)` (NULL for an unknown name). It also checks the
blobs: SPIR-V must be a whole number of words starting with the magic `0x07230203`; DXIL must be a
DXBC container whose hash is not zero, since D3D12 refuses an unsigned container without
experimental shader models (`port/shaders/embed_shaders.py`). The stage values equal
`RhiShaderStage`; the entry name is what Vulkan's `RhiShaderDesc.entryPoint` takes.

**No compiler.** DXC is a build tool from `tools/fetch_deps.sh` (`tools/toolchain/deps/dxc`), or the
path in `ICO_DXC`. Without it (or without Python) `ICO_SHADERS_AVAILABLE` is false, every
`ico_add_shader` does nothing, and `ico_shaders_library` logs `shaders not built: no DXC at ...`
(`cmake/IcoShaders.cmake`).

## The families

Every source in `port/shaders`, one line each (entries from `port/shaders/CMakeLists.txt`; detail in
each file's header comment):

| file | entries | what it draws |
| --- | --- | --- |
| `sprite.hlsl` | `sprite_ui_vs`, `sprite_world_vs`, `sprite_ps`, `sprite_texa_ps`, `date_snap_ps`, `sprite_aa1_*`, `sprite_stq_*`, `sprite_plane_*_vs`, `sprite_stq_plane_*_vs` | screen-space prims in GS window coordinates: TEXA per texel before the filter, the DATE snapshot, PRIM.AA1 lines and triangles with a coverage attribute, perspective-correct STQ, and screen-prim triangles evaluated from their planes the same at every frame width |
| `blit.hlsl` | `blit_vs`, `blit_ps`, `blit_depth_ps`, `box_reduce_ps`, `aa_reduce_ps`, `depth_resolve_ps`, `depth_upsample_ps`, `camera_probe_ps` | the present's box blit, the MSAA resolve's colour copy and the shadow count's resolve passes (`blit_ps`); the present's blit with the scene's depth (`blit_depth_ps`); the shadow count box-reduced to the GS size; the supersampled DISPLAY box-averaged; the MSAA depth resolved (smallest sample) and spread over samples; a test probe |
| `blend_int.hlsl` | `blend_int_vs`, `blend_int_ps` | the GS integer blend between two RGBA8 UINT textures for feedback passes, no hardware blending |
| `fx_sprite.hlsl` | `fx_rect_vs`, `fx_sprite_ps` | one GS sprite of `staticBlur.c`'s passes in the GS integer arithmetic (`port/render/rd_blur.c`) |
| `fog_lut.hlsl` | `fog_lut_ps` and its `FOG_BUFFER` (`fog_lut_buffer_ps`) and `FOG_MS` (`fog_lut_ms_ps`) builds | the depth fog: bits 16..23 of the 32-bit Z through the fog CLUT, from the depth, an R32 UINT copy of it, or per sample |
| `raw_wrap.hlsl` | `wrap_acc_ps`, `wrap_resolve_ps` | COLCLAMP 0 prims (`darkVolume.c`): an RGBA16F accumulation, then the sum modulo 256 |
| `font.hlsl` | `font_ps`, `font_sheet_ps` | R8 coverage textures (the port's font atlas) read as sprite_ps reads a texel; sheet text in the menu sheets' style |
| `yuv.hlsl` | `yuv_vs`, `yuv_ps`, `yuv_field_ps` | the FMV picture, 4:2:0 planes to RGB with the [IPU](ARCHITECTURE.md#terms)'s conversion; a field deinterlaced at the picture's size (`port/render/rd_video.c`) |
| `crt.hlsl` | `crt_vs`, `crt_bloom_ps`, `crt_blur_ps`, `crt_ps` | the CRT filter ([DISPLAY.md](DISPLAY.md)) |
| `vu_prelit.hlsl` | `vu_prelit_vs`, `vu_ps`, `vu_texa_ps`, `vu_probe_ps` | `normal_c` (codes 32, 34, 36); the pixel shader of every mesh program; the tests' probe output |
| `vu_lit.hlsl` | `vu_lit_vs`, `vu_lit_spec_vs`, `vu_reflect_vs` | `normal_l` (codes 32/36, 34, 38) |
| `vu_skin.hlsl` | `vu_skin_vs`, `vu_skin_spec_vs`, `vu_skin_debug_vs` | `cluster`, skinned (codes 20, 22, 24) |
| `vu_grid.hlsl` | `vu_grid_vs`, `vu_grid_lit_vs`, `vu_grid_spec_vs` | `mesh`, the procedural grids (codes 20, 22, 24) |
| `vu_particle.hlsl` | `vu_particle_vs` | `particle`, six vertices a particle, non-indexed |
| `common.hlsli`, `gs_math.hlsli`, `sheet_text.hlsli`, `vu_common.hlsli` | none | included headers, below |

The vertex shaders of the VU programs are one per [microprogram](ARCHITECTURE.md#terms) family; the
CPU references they are tested against are `port/render/vu1_ref/` (`port/shaders/vu_common.hlsli`,
header comment).

**Variants.** The same entries are built again under defines (`port/shaders/CMakeLists.txt`):

- `_nodual` (`ICO_NO_DUAL=1`): the entries that call `gs_dual_out` without the second output, for a
  device without dual-source blending: `sprite_ps`, `sprite_texa_ps`, `sprite_aa1_ps`,
  `sprite_stq_ps`, the three fog builds, `font_ps`, `font_sheet_ps`, `vu_ps`, `vu_texa_ps`.
  [rd](ARCHITECTURE.md#terms) draws a lerp as two passes with them (`port/render/rd_pipeline.c`
  (`rd__expand_no_dual`)).
- `_dstrov` (`ICO_DST_READ=1`) and `_dstatt` (`ICO_DST_READ=2`): the exact blend computed from the
  destination (`port/render/rd_dst_read.c`), for the sprite entries, `fog_lut_ps`,
  `fog_lut_buffer_ps`, the font entries and `vu_ps`, `vu_texa_ps`. `_dstrov` reads and writes the
  target as a rasterizer-ordered view; `_dstatt` reads the colour attachment in rasterization order
  (Vulkan only; its DXIL is the ROV build, never used).

The replay looks entries up by the names in `s_vsNames` and `s_fsNames` (`port/render/rd_replay.c`).

## Adding a shader

1. Write the entry in the family's `.hlsl` (or a new file) with `common.hlsli`'s bindings; a new
   constant block gets its C struct and offset asserts (`_Static_assert`) in `shader_consts.h`.
2. Register it with `ico_add_shader` in `port/shaders/CMakeLists.txt`, and its `_nodual` and
   `_dstrov`/`_dstatt` builds when it calls `gs_dual_out` or takes the destination read.
3. Give rd a name for it: an `RD_VS_*` or `RD_FS_*` value (`port/render/rd_internal.h` (`RdVsId`,
   `RdFsId`)) and the same position in `s_vsNames` or `s_fsNames` (`port/render/rd_replay.c`); add
   its pipeline keys to the reachable enumeration (`port/render/rd_pipeline.c`
   (`rd__enumerate_reachable`)) so `rd_precreate_pipelines` creates them.
4. Add the name to the expected lists of `port/shaders/test/shaders_table_test.c` (`expected`, or
   the list of its kind: `expectedPlanes`, `expectedAa`, `expectedVariants`, `expectedNoDual`,
   `expectedDst`).
5. Run `shaders_table`, `gs_math`, `shaders_pixel*`, `vu1*` (a VU entry) and `rhi_d3d12_plan` (every
   vertex shader's DXIL inputs against its SPIR-V locations), then the render tests
   ([TESTING.md](TESTING.md#portrendertest)).

## The binding scheme

`common.hlsli` fixes it for every program: the register space is the RHI bind group and the register
number the RHI slot (`port/shaders/common.hlsli`, header comment; `port/rhi/rhi.h`, bind groups):

| group | registers | holds |
| --- | --- | --- |
| 0 | `b0` | FrameCB, once per frame |
| 1 | `b1` | DrawCB, per draw or post pass (crt.hlsl's CrtCB takes the same slot) |
| 1 | `b2`, `b3`, `t0` | the VU programs' VuCB and VuBoneCB, and the VU vertex stream |
| 2 | `t1`..`t4`, `s1`..`s4` | textures and samplers |
| 3 | `u0` | the destination read's target (`_dstrov`); `_dstatt` reads binding 16 of set 3 as an input attachment |

Clip space is D3D's: x and y in -1..1 with +y up, depth 0..1, reversed (near 1, far 0, depth test
GEQUAL). Vulkan-only attributes (`[[vk::location]]`, `[[vk::index]]`) are behind `__spirv__`
(`VK_LOC`, `VK_DUAL`), since DXC warns on them for DXIL and warnings are errors
(`port/shaders/common.hlsli`).

## common.hlsli

**FrameCB** (b0): view, projection and their product (column-major, used as `mul(M, v)`), the eye (w
1 on a camera cut [tick](ARCHITECTURE.md#terms)), near, far, zoom and aspect, the bound target's
size in [GS](ARCHITECTURE.md#terms) pixels, its GS origin, `g_frameRegs[0]` (`g_frame`: the region
scale, the drawing camera's picture half-width and half-height about 2048, the exact viewport's half
texel) and `g_frameRegs[1]` (`g_vp`: the exact viewport, a power of two centred on the target, so
positions on the GS's 1/16 grid map to the same texel at every frame width), `g_z` (the Z scale,
texels per GS pixel), `g_misc` (frame counter, preset) and `g_vpo` (the exact viewport's centre
offset; the texels a [Full pixel](ARCHITECTURE.md#terms) target leaves out at its left). Nothing
transforms a position: a GS X is drawn where the game put it, at every aspect
(`port/shaders/common.hlsli`, FrameCB).

**DrawCB** (b1, 112 bytes): `g_col` (a constant colour), `g_mode` (the `DF_*` flags,
[TEXA](ARCHITECTURE.md#terms) mode and texel format, alpha test and AFAIL split, AREF), `g_blend`
(the ALPHA register, FIX, COLCLAMP, and TEX1's mipmap fields with `DF_GS_LOD`), `g_uvRect`, `g_tex`
(t1's size), `g_param` (per entry) and `g_scale` (t1 texels per GS texel; a Full pixel crop)
(`port/shaders/common.hlsli`, DrawCB).

**`DF_*` flags** (DrawCB `g_mode.x`): textured, DECAL, TCC, FBA, PABE, the FIX factor, the
premultiplied additive forms, DATE and DATM, the AA1 coverage alpha, the TEXA sampler state,
`Cd * c1` pipelines, the two `_nodual` passes, `DF_GS_LOD` (sampling at the GS's level from Q), the
destination read's write mask (bits 19..22), `DF_PLANE` (a clip plane) and `DF_NEAREST`
(`port/shaders/common.hlsli`).

**Functions.** Vector forms of the GS arithmetic (`gs_texture_function`, `gs_texa_expand`,
`gs_bilerp`, `gs_texa_texture`: the GS's bilinear filter with TEXA applied per texel first,
`gs_blend_int`); GS mipmapping (`gs_lod`, `gs_sample_lod_nearest`, `gs_sample`); DATE
(`gs_date_discard`). **Outputs**: `gs_dual_out` fills `DualOut` with the colour and the stored GS
alpha and, on the second output, the blend factor As/128 or FIX/128 in all four lanes; a factor
above 1.0 does not survive the blender, so `DF_PREMUL` moves the multiply into the shader for the
additive and subtractive forms. **The destination read** (`gs_dst_pixel`, `gs_dst_out`): the GS's
`((A - B) * C >> 7) + D` computed exactly from the fragment's own pixel, with COLCLAMP, PABE, FBA
and the write mask, written with blending off; `_dstrov` runs under `[earlydepthstencil]` with a
globally coherent view. `GS_PS_OUT`, `GS_PS_ATTR`, `GS_RETURN` and `GS_DISCARD` pick the output
per build. In `_dstrov` a `GS_DISCARD` marks the fragment (`g_dstKill`) instead of ending it, and
`gs_dst_out` stores the pixel it read back unchanged: Vulkan's interlock requires every invocation
to execute `OpBeginInvocationInterlockEXT` and `OpEndInvocationInterlockEXT` exactly once, in that
order (SPV_EXT_fragment_shader_interlock, validation rules), and DXC puts an
`OpEndInvocationInterlockEXT` before each `OpKill`, on paths that never began the section. The depth
and stencil tests run before the shader and the pass has no colour attachment, so the unchanged store
is the whole of a discard there. Each `_dstrov` entry compiles to one begin, one end, no kill, and a
critical section of one load, the blend arithmetic (no loop) and one store
(`port/shaders/common.hlsli`, gs_dst_out).
**Vertex helpers**: `gs_px_to_ndc`, `gs_xy_to_ndc`, `gs_uv_norm`, `gs_stq_norm`, `gs_depth`,
`fullscreen_triangle` (`port/shaders/common.hlsli`).

## shader_consts.h

The C side of the byte layouts; rd fills these, and static asserts pin every offset to the HLSL
packing (each member a 16-byte register or a 64-byte matrix, so the packing is unambiguous)
(`port/shaders/shader_consts.h`, header comment):

| struct | register | bytes | notes |
| --- | --- | --- | --- |
| `IcoFrameCB` | b0 space0 | 336 | view, proj, viewProj, cameraPos, clip, target, origin, frame, vp, z, misc, vpo |
| `IcoDrawCB` | b1 space1 | 112 | col, mode, blend, uvRect, tex, param, scale |
| `IcoCrtCB` | b1 space1 | 112 | the CRT filter's block in DrawCB's place: src, box, beam, mask, glow, tone, pass |
| `IcoSpriteVertex` | vertex | 20 | XY 12.4 (U16x2), Z (U32), RGBA (U8x4, 0x80 = 1.0), UV in texels (F32x2) |
| `IcoSpriteAa1Vertex`, `IcoSpriteStqVertex` | vertex | 24 | plus the AA1 coverage, or Q |
| `IcoSpritePlaneVertex` | vertex | 120 | the STQ vertex plus the triangle's colour and STQ planes |
| `IcoVuCB` | b2 space1 | 752 | VU1 data memory 0..35, draw, batch, strip, the two ticks' camera matrices |
| `IcoVuBoneCB` | b3 space1 | 3840 | VU memory 16..255, the cluster bones |

Constants mirrored from the HLSL keep the same values: `ICO_DF_*` (`common.hlsli`),
`ICO_DF_WMASK_SHIFT` (19), `ICO_TEXFMT_*` (`gs_math.hlsli`, asserted in `gs_math_test.c`),
`ICO_SHEET_*` (`sheet_text.hlsli`, asserted in `port/render/test/sheet_ref.c`), `ICO_VU_*` and the
index marks `ICO_VU_INDEX_LATER`, `_HIDE`, `_JOINED` (`vu_common.hlsli`), and `ICO_SH_FRAME_*`, the
[native frame](ARCHITECTURE.md#terms)'s constants, which `rd_present_test` asserts equal
`port/include/ico_frame.h`'s (`port/shaders/shader_consts.h`).

## gs_math.hlsli

The [GS](ARCHITECTURE.md#terms) integer arithmetic, scalar only, written to be valid HLSL and valid
C (scalar int, uint and float, `min`/`max`/`clamp`, `?:` and constants), so `gs_math_test.c`
compiles it through `hlsl_shim.h` (which defines `ICO_GS_MATH_C`, the `uint` type and `min`, `max`,
`clamp` as macros) and `common.hlsli` wraps it in vector forms. Conventions: colours and alpha are
0..255 with alpha 0x80 = 1.0; the texture function is `min((tex * col) >> 7, 255)`; the blend is
`((A - B) * C >> 7) + D` with an arithmetic shift (`port/shaders/gs_math.hlsli`, header comment).
Functions: `gs_tfx_mod`, `gs_texa_alpha` (the alpha of a 24- or 16-bit texel under TEXA,
`TEXA_80_80`, `TEXA_7F_81_AEM`, `TEXA_80_80_AEM`), `gs_alpha_pass` (the `ATST_*` tests),
`gs_blend_term`, `gs_blend_factor`, `gs_blend_ch` (with COLCLAMP clamping or wrapping),
`gs_blend_reg_ch` (from the ALPHA register), `gs_dst_channel` (one channel of the destination read)
and `gs_z_to_depth` (a GS Z to a depth that grows with it) (`port/shaders/gs_math.hlsli`).

## sheet_text.hlsli

The texel arithmetic of `font_sheet_ps`: the menu sheets' text style as integer steps, so the GPU
and `port/render/test/sheet_ref.c` (the tests' oracle, which includes the file with
`ICO_SHEET_TEXT_C`) agree exactly; only the bilinear weights are float. The rim is a weighted
dilation of the coverage: the largest `c(p + d) * WX[|dx|] * WY[|dy|]` (per mille) within `SHEET_RX`
(6) texels across and `SHEET_RY` (4) down, the falloff following the English sheets' rim alpha.
Opacity and the rim-to-fill mix are quantised to `SHEET_LEVELS` (8) with a 4x4 Bayer threshold; a
strip drawn at a scale of 1..`ICO_SHEET_SCALE_MAX` (4) keeps the 1x rim magnified. Functions:
`sheet_wx`, `sheet_wy`, `sheet_rim`, `sheet_threshold`, `sheet_quantise`, `sheet_texel`,
`sheet_rim_level`, `sheet_texel_scaled` (`port/shaders/sheet_text.hlsli`;
`port/shaders/shader_consts.h`, the sheet constants). What a sheet texture is: `port/render/rd.h`
(`rd_create_texture_sheet`).

## Bounded loops

Every loop in the shaders stops within a compile-time constant number of iterations, whatever its
inputs: a GPU computing a wrong value (an unstable overclock) gives a wrong pixel, never a loop that
runs until the driver resets the device. Loops with constant bounds: the B-spline's two axes
(`port/shaders/common.hlsli`, gs_bspline_bilinear4), the CRT's blur taps and channels
(`port/shaders/crt.hlsl`, crt_bloom_ps, crt_blur_ps, gainFadeOf), the deinterlacer's seven columns
(`port/shaders/yuv.hlsl`, field_sample), the two ticks (`port/shaders/vu_common.hlsli`,
vu_region_rule; `port/shaders/vu_particle.hlsl`, vu_particle_tick_shown) and the strip scans, which
stop at `VU_STRIP_SCAN` (vu_strip_of). Loops whose count comes from a constant buffer, a stream or
arithmetic are clamped to a constant that valid inputs never reach, so every picture is unchanged:

| loop | clamp | valid inputs need at most |
| --- | --- | --- |
| above 1x taps (`port/shaders/sprite.hlsl`, sprite_above1x; `port/shaders/fx_sprite.hlsl`, fx_sprite_ps) | `GS_RS_TAPS_MAX` = 33 a side (`common.hlsli`) | 33: a 32-texel box over a texel boundary (gs_rs_count) |
| strip tests (`vu_prelit.hlsl`, `vu_lit.hlsl`, `vu_skin.hlsl`, `vu_grid.hlsl`, *_strip_passes) | `vu_strip_end`, `VU_STRIP_SPAN_MAX` = 1024 vertices (`vu_common.hlsli`) | 515 on a flagged strip; a grid strip of `Mesh3D.stripLen` vertices, which VU1's 1024 data quadwords hold |
| polygon cut corners (`vu_common.hlsli`, vu_clip_corners, vu_cut_reaches) | `VU_CUT_CORNERS` = 7 | 7 |
| shadow count reduce (`port/shaders/blit.hlsl`, box_reduce_ps) | `BLIT_SPAN_MAX` = 258 texels an axis | 148 (a 32768-texel scene over 224 rows) |
| supersampling reduce (`blit.hlsl`, aa_reduce_ps) | `BLIT_SPAN_MAX` = 258 texels an axis | 258 where its 32-bit sums are exact (256 texels a pixel) |
| MSAA depth resolve (`blit.hlsl`, depth_resolve_ps) | 8 samples | 8 (`port/render/rd_core.c`, rd_aa_samples) |
| CRT triad gain (`crt.hlsl`, triadGain) | `CRT_TRIAD_REACH_MAX` = 128 | 65 (a 32768-pixel box over 512 columns) |

No shader waits on memory, spins on an atomic or uses recursion; the one ordered section is
`gs_dst_out`'s (above).

## Scalars, not arrays

No shader builds, copies or carries a whole array as a value, and no vertex shader keeps a local
array (one in SPIR-V's Function storage class). An array in a constant buffer, a storage buffer or
a stream is read one element at a time. The polygon cut (`port/shaders/vu_common.hlsli`,
vu_clip_corners, vu_cut_reaches) is written out over its seven corners with macros: a write at a
run-time position is a select over the corners (vu_corner_set), a read at one likewise (vu_corner),
so the compiled vertex shaders hold the corners in registers. The compilers of two drivers (an
NVIDIA GeForce 10-series card on Windows, a Qualcomm Adreno 740 on Android) crash on the first
pipeline whose vertex shader carries array-typed values, while every shader without them builds,
and the game cannot start without the VU vertex shaders. `spirv_check` (`tools/check_spirv.py`)
reads every compiled module and fails on either construct; the pixel shaders with a local array
indexed at run time (`font.hlsl`, font_sheet_ps; `yuv.hlsl`) build on those drivers and are allowed.

## Tests

| test | checks |
| --- | --- |
| `shaders_table` (`port/shaders/test/shaders_table_test.c`) | every embedded entry: SPIR-V with its magic, DXIL a signed DXBC container (none with `ICO_SHADERS_DXIL=OFF`), a known stage and entry, unique names, the expected names present |
| `gs_math` (`gs_math_test.c`) | `gs_math.hlsli` as C against `port/test/gs_blend_test.c`'s formulas and the GS rules for TEXA, the alpha test and the Z mapping: exhaustive where the domain is small; the shared constants pinned |
| `shaders_pixel` (`shaders_pixel_test.c`) | `sprite.hlsl`, `blit.hlsl` and `blend_int.hlsl` through the Vulkan RHI, texel by texel against `gs_math.hlsli` as C: untextured, modulate, the alpha test, dual-source lerps, additive with `DF_PREMUL`, PABE, FBA, tinted blits, three integer blends; `shaders_pixel_nodual` (`--nodual`, `ICO_VK_FAKE_NO_DUAL=1`) the two-pass fallback; `shaders_pixel_mali` under `ICO_VK_FAKE_LIMITS=mali` |
| `spirv_check` (`tools/check_spirv.py`) | every compiled SPIR-V module under the build's `shaders/`: no array-typed value (OpLoad, OpStore, OpCompositeConstruct, OpCompositeInsert, OpPhi, OpSelect, OpCopyObject, OpCopyLogical of an array type) and no local array in a vertex shader (Scalars, not arrays, above); the module's binary form decoded in Python, no SPIR-V tool needed |
| `vu1` (`vu1_test.c`) | the VU program shaders against `port/render/vu1_ref/`: hand traces through the `.vsm` code on the CPU, then on Vulkan every vertex shader's probe output, strips drawn through the shipped entries, and the region rule's clipping; `vu1_mali` under the Mali limits |
| `port/test/gs_blend_test.c` | not a ctest: a CPU program (`cc -O2`) printing how far a float blender with round-to-nearest drifts from the GS integer blender under feedback |

The Vulkan tests exit 77 (skipped) without a device (`port/shaders/test/CMakeLists.txt`; each test's
header comment). `port/rhi/test/shaders/` holds the RHI tests' own shader, compiled ahead and
committed ([RHI.md](RHI.md)).

## DXC

**The pin.** `tools/fetch_deps.sh` installs DXC at `tools/toolchain/deps/dxc/{bin,lib}`: on x86-64
Microsoft's Linux release `DXC_VERSION` v1.9.2609 (`DXC_LINUX_FILE`, checked against
`DXC_LINUX_SHA256`); on arm64, where Microsoft publishes no Linux build, the same tag built from
source at `DXC_COMMIT`, checked by the commit id and the SHA-256 of its `git archive`, with no
`libdxil.so` (the arm64 presets compile no DXIL). `SKIP_DXC=1` skips it; each value can be
overridden from the environment (`tools/fetch_deps.sh`, section 4).

**The Mac build.** `.github/workflows/ios.yml` builds a host DXC on `macos-15` for the iOS and macOS
jobs: v1.9.2602 at commit `21d28f727ad395b59394815ef76012e432f7e4e5`, cloned with its submodules,
the `dxc` target alone in Release with SPIR-V code generation, cached under
`tools/toolchain/deps/dxc-build`; the `ios-app` and `macos-app` presets point `ICO_DXC` at
`tools/toolchain/deps/dxc-build/bin/dxc` (`.github/workflows/ios.yml`; `CMakePresets.json`).

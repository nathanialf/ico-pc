# Shaders

The port's shaders are HLSL in `port/shaders`, compiled at build time by DXC to SPIR-V for Vulkan
and signed DXIL for Direct3D 12 and embedded in the program as byte arrays; the one compiler at
run time is Direct3D 11's, which compiles the embedded sources
([Direct3D 11: run-time compile](#direct3d-11-run-time-compile)). This document covers the build, every source file, the binding scheme, the constant
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

**Flags.** SPIR-V: `-spirv -fspv-target-env=vulkan1.0` with `-fvk-b-shift 0`, `-fvk-t-shift 16`,
`-fvk-s-shift 32`, `-fvk-u-shift 48` (all spaces), so a register number is the
[RHI](ARCHITECTURE.md#terms) slot and the Vulkan binding is the slot plus 0, 16, 32 or 48. Both
targets: `-O3 -WX -I port/shaders` (a shader warning fails the build; no `-Zi`, no debug info in the
blobs) (`cmake/IcoShaders.cmake` (`ICO_DXC_SPIRV_FLAGS`, `ICO_DXC_COMMON_FLAGS`)).

The SPIR-V target is Vulkan 1.0 (SPIR-V 1.0) so the blobs load on Vulkan 1.0 drivers (Android 8,
issue 158). The shaders use only Vulkan 1.0
capabilities: `Shader`, `InputAttachment`, `SampleRateShading` and `ImageQuery`, the built-ins
`Position`, `PointSize`, `VertexIndex`, `FragCoord`, `FragDepth` and `SampleId`, dual-source
`Index`, `EarlyFragmentTests`, structured buffers as `Uniform` + `BufferBlock`, storage images
with an explicit format, and the interlock pair through `SPV_EXT_fragment_shader_interlock` (no
draw parameters, wave operations, 8/16-bit types, push or specialization constants, variable
pointers, demote or memory model). DXC 1.9.2609.5 emits the same instruction stream at 1.0 as at
1.1, so only the module header's version word differs: one set serves every Vulkan version.
`tools/spirv_header_diff.py OLD NEW` compares two blob sets with that word masked (the check run
when the target moved), and the `spirv_check` test (`tools/check_spirv.py`, gate `version`) holds
every module's header to 1.0. Contingency: if a future shader needs a later SPIR-V, add a second
blob column (the 1.0-targeted blob beside it) to the manifest and an `RhiLimits.spirvMax`, and
pick the column at pipeline creation; do not raise the single target.

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
| `sprite.hlsl` | `sprite_ui_vs`, `sprite_world_vs`, `sprite_ps`, `sprite_texa_ps`, `date_snap_ps`, `sprite_aa1_*`, `sprite_stq_*`; the planes build: `sprite_plane_*_vs`, `sprite_stq_plane_*_vs`, `sprite_ps_planes`, `sprite_texa_ps_planes`, `sprite_stq_ps_planes` | screen-space prims in GS window coordinates: TEXA per texel before the filter, the DATE snapshot, PRIM.AA1 lines and triangles with a coverage attribute, perspective-correct STQ, and screen-prim triangles evaluated from their planes the same at every frame width |
| `blit.hlsl` | `blit_vs`, `blit_ps`, `blit_depth_ps`, `box_reduce_ps`, `aa_reduce_ps`, `depth_resolve_ps`, `depth_upsample_ps`, `camera_probe_ps`, `crack_fill1_ps` (`CRACK_REACH_1`: the crack fill for a reach of 1 texel, [WORK_BUFFERS.md](WORK_BUFFERS.md)) | the present's box blit, the MSAA resolve's colour copy and the shadow count's resolve passes (`blit_ps`); the present's blit with the scene's depth (`blit_depth_ps`); the shadow count box-reduced to the GS size; the supersampled DISPLAY box-averaged; the MSAA depth resolved (smallest sample) and spread over samples; a test probe |
| `blend_int.hlsl` | `blend_int_vs`, `blend_int_ps` | the GS integer blend between two RGBA8 UINT textures for feedback passes, no hardware blending |
| `fx_sprite.hlsl` | `fx_rect_vs`, `fx_sprite_ps` and its `ICO_UINT_SRC` (`fx_sprite_u_ps`), `ICO_FX_FRAME` (`fx_frame_ps`) and `ICO_FX_DST_LOCAL` (`fx_local_ps`) builds, `fx_blur2_ps` | one GS sprite of `staticBlur.c`'s passes in the GS integer arithmetic (`port/render/rd_blur.c`): through the RGBA8_UINT views; the frame-size forms the open pass draws (the alpha alone, `FXF_ALPHA_ONLY`; the premultiplied term for the fixed-function add or subtract, `FXF_OUT_PREMUL`); the destination by local read (`port/render/rd_fx_plan.c`); two blur passes folded |
| `fog_lut.hlsl` | `fog_lut_ps` and its `FOG_BUFFER` (`fog_lut_buffer_ps`) and `FOG_MS` (`fog_lut_ms_ps`) builds | the depth fog: bits 16..23 of the 32-bit Z through the fog CLUT, from the depth, an R32 UINT copy of it, or per sample |
| `raw_wrap.hlsl` | `wrap_acc_ps`, `wrap_resolve_ps` | COLCLAMP 0 prims (`darkVolume.c`): an RGBA16F accumulation, then the sum modulo 256 |
| `font.hlsl` | `font_ps`, `font_sheet_ps` | R8 coverage textures (the port's font atlas) read as sprite_ps reads a texel; sheet text in the menu sheets' style |
| `yuv.hlsl` | `yuv_vs`, `yuv_ps`, `yuv_field_ps` | the FMV picture, 4:2:0 planes to RGB with the [IPU](ARCHITECTURE.md#terms)'s conversion; a field deinterlaced at the picture's size (`port/render/rd_video.c`) |
| `crt.hlsl` | `crt_vs`, `crt_bloom_ps`, `crt_blur_ps`, `crt_ps` | the CRT filter ([DISPLAY.md](DISPLAY.md)) |
| `vu_prelit.hlsl` | `vu_prelit_vs`, `vu_prelit_pre_vs`, `vu_ps`, `vu_texa_ps`, `vu_probe_ps` | `normal_c` (codes 32, 34, 36); the pixel shader of every VU draw, through the main shaders and the programs' own entries alike (the planes as flat varyings); the tests' probe output |
| `vu_lit.hlsl` | `vu_lit_vs`, `vu_lit_spec_vs`, `vu_reflect_vs` and their `_pre_vs` | `normal_l` (codes 32/36, 34, 38) |
| `vu_skin.hlsl` | `vu_skin_vs`, `vu_skin_spec_vs`, `vu_skin_debug_vs` and their `_pre_vs` | `cluster`, skinned (codes 20, 22, 24) |
| `vu_grid.hlsl` | `vu_grid_vs`, `vu_grid_lit_vs`, `vu_grid_spec_vs` and their `_pre_vs` | `mesh`, the procedural grids (codes 20, 22, 24) |
| `vu_tri.hlsl` | `vu_main_vs`, `vu_tri_rule_pre_vs`, `vu_tri_tick_pre_vs`, `vu_tri_planes_pre_vs`, `vu_pre_ps` | the main vertex shader of every mesh program, the triangle pre-pass's record stages and the record targets' pixel shader ([RENDER.md](RENDER.md#the-vertex-and-triangle-pre-passes)) |
| `vu_particle.hlsl` | `vu_particle_vs`, `vu_particle_pre_vs`, `vu_particle_main_vs` | `particle`, six vertices a particle, non-indexed; the sprite's record stage and main shader |
| `common.hlsli`, `gs_math.hlsli`, `sheet_text.hlsli`, `vu_common.hlsli` | none | included headers, below |

The vertex shaders of the VU programs are one per [microprogram](ARCHITECTURE.md#terms) family; the
CPU references they are tested against are `port/render/vu1_ref/` (`port/shaders/vu_common.hlsli`,
header comment). Each mesh program also has a pre-pass entry, `vu_X_pre_vs`: its own vertex
function on vertex `vu_rec.z + SV_VertexID`, the result written as a record (`vu_pre_out`: twelve
flat `uint` words at the vertex's texel, `PointSize` 1 under SPIR-V) that `vu_pre_ps` stores in
three `RGBA32_UINT` targets. The record stages (`vu_tri_rule_pre_vs`, `vu_tri_tick_pre_vs`,
`vu_tri_planes_pre_vs`, `vu_particle_pre_vs`) read it back (`vu_rec_vertex`) and write a
triangle's or sprite's verdict and attribute planes as records the same way; `vu_main_vs` and
`vu_particle_main_vs` draw from the two records, the program's clip mode, chain and HIDE rule and
region bounds from VuCB, and output `VuVSOut` as the programs' own entries do (position, colour,
STQ, plane distance and the planes as 28 flat floats: the main shaders read the planes' words from
the record once a vertex, `vu_rec_planes`, where the planes hold), which `vu_ps` and `vu_texa_ps`
take from either ([RENDER.md](RENDER.md#the-vertex-and-triangle-pre-passes)).

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
- the planes build (`ICO_SPRITE_PLANES=1`): `sprite.hlsl` with the screen-prim triangles' planes in
  `SpriteVSOut` and `SpriteStqVSOut` (`SPRITE_PLANES`, six `nointerpolation float4`). The four
  plane vertex entries exist only in it, and `sprite_ps`, `sprite_texa_ps` and `sprite_stq_ps` are
  built again with it as `sprite_ps_planes`, `sprite_texa_ps_planes` and `sprite_stq_ps_planes`,
  each with its `_nodual`, `_dstrov` and `_dstatt` twin (the define and the twin's together). Every
  other sprite entry is the plain build: the position, the colour and uv or stq, two input
  locations, the layout `font.hlsl`, `fog_lut.hlsl` (`FogPSIn`) and `raw_wrap.hlsl` (`WrapAccIn`)
  declare, since D3D12 refuses a pixel input the vertex shader does not write. A pass takes both
  halves of the planes build together (`port/render/rd_screen_planes.c` (`rd__plane_pass`)). A
  device whose driver stopped creating them runs without the planes build (`[dev] pipeline_skip`,
  issue 161): those passes take the plain build, which was byte-identical on every dump measured
  (the title's and the boot's, 2 commands each, 0 of 18 targets differ); a level's difference is
  possible in principle at a fractional scale, not seen
  ([WORK_BUFFERS.md](WORK_BUFFERS.md#without-the-sprite-planes)).
- the no-planes build (`ICO_VU_NO_PLANES=1`): `vu_ps` and `vu_texa_ps` built again as `vu_np_ps`
  and `vu_texa_np_ps`, each with its `_nodual` twin, for a target at a fractional scale
  (`port/render/rd_pixel_plan.c` (`rd__vu_pixel_fs`), [RENDER.md](RENDER.md#screen-planes)). Their
  input is `VuPSIn` (`vu_common.hlsli`): locations 0 to 2 of `VuVSOut` alone (the colour, S, T, Q
  and the plane distance; 12 input components with the position, against 40), a prefix of every
  VU vertex shader's output, so the main shaders and the programs' own entries feed it unchanged
  (DXC packs the prefix as the whole signature packs it: the same D3D12 registers). No
  destination-read twins: under an exact blend the draws keep `vu_ps`. A fused scissor pair's kick
  is marked for them by the plane distance's sign (-1 where the draw has no clip plane, which every
  twin's draw is), not by `r.w`
  ([VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-fused-scissor-pair)).
- the frame-size sprite builds (`port/render/rd_fx_plan.c`): `fx_sprite_ps` built again with
  `ICO_FX_FRAME=1` as `fx_frame_ps` (the open pass's forms: `FXF_ALPHA_ONLY`, the alpha alone;
  `FXF_OUT_PREMUL`, the premultiplied term for the fixed-function add or subtract; no blend and no
  destination read of its own) and with `ICO_FX_DST_LOCAL=1` as `fx_local_ps` (the destination by
  `SubpassLoad` of the input attachment, `layoutLocal`: slot 1 the attachment, slot 2 the texture).
  Neither has a `_nodual` or destination-read twin: `fx_local_ps` is in `rd__fs_local_read`, and
  an exact blend keeps the snapshot ([RENDER.md](RENDER.md#post-passes)).

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
   `expectedDst`, `expectedPlain`, `expectedPrepass`).
5. Run `shaders_table`, `gs_math`, `shaders_pixel*`, `vu1*` (a VU entry), `rhi_d3d12_plan` (every
   vertex shader's DXIL inputs against its SPIR-V locations) and `rhi_d3d11_shader_plan`, and build
   `shaders_d3d11_check` (the entry under `ICO_D3D11`), then the render tests
   ([TESTING.md](TESTING.md#portrendertest)).

## The binding scheme

`common.hlsli` fixes it for every program: the register space is the RHI bind group and the register
number the RHI slot (`port/shaders/common.hlsli`, header comment; `port/rhi/rhi.h`, bind groups):

| group | registers | holds |
| --- | --- | --- |
| 0 | `b0` | FrameCB, once per frame |
| 1 | `b1` | DrawCB, per draw or post pass (crt.hlsl's CrtCB takes the same slot) |
| 1 | `b2`, `b3`, `t0`, `t4` | the VU programs' VuCB and VuBoneCB (bones, card bits), the VU vertex stream, and the pre-passes' record (`vu_rec_buf`, `StructuredBuffer<uint4>`, three planes); VuCB and the record are visible to the pixel stage too (`vu_ps` reads the planes) |
| 1 | `t5` | the upload ring from the replay's VU base on (`vu_ring`, `StructuredBuffer<float4>`, vertex stage; `rd_replay_vu.c` `s_ringBase`, 256-aligned, after the uploads): the pre-pass's batch builds (`vu_X_batch_pre_vs`, `ICO_VU_PRE_BATCH`) read each entry's VuCB and VuBoneCB and the batch's table there, the table found through DrawCB's `aux0` ([VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-pre-passs-batches)); the draw merge's builds (`vu_X_merge_vs`, `vu_X_merge_plain_vs`, `vu_merge_main_vs`, `ICO_VU_MERGE`) read each draw's VuCB and VuBoneCB and the run's table there, the table found through b2's `mark.w` ([VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-draw-merge)); every VU group binds it (Direct3D 11's leave it unbound only with the merge off: no batch module there), the other builds do not read it |
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
destination read's write mask (bits 19..22), `DF_PLANE` (a clip plane), `DF_NEAREST`, and
`DF_AA1_MSB_SET` / `DF_AA1_MSB_CLEAR` (bits 27 and 28: `sprite_aa1_ps` keeps only the fragments
whose written alpha, As with FBA's MSB, has its MSB set, or only those whose MSB is clear: the two
draws of a PRIM.AA1 primitive under DATE, `port/render/rd_replay_aa1.inc` `aa1DatePlan`; a
uniform, so no key of its own) (`port/shaders/common.hlsli`).

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
| `IcoSpriteAa1Vertex` | vertex | 40 | plus the AA1 coverage and a triangle edge's clip, flat: the sorted first vertex (U16x2), the other two less it (two int16 in a U32 each) and the flags (U32) |
| `IcoSpriteStqVertex` | vertex | 24 | plus Q |
| `IcoSpritePlaneVertex` | vertex | 120 | the STQ vertex plus the triangle's colour and STQ planes |
| `IcoVuCB` | b2 space1 | 832 | VU1 data memory 0..35, draw, batch, the card test's bits' place, the two ticks' camera matrices, the marks' place (`vu_mark`), the vertex record's place and the program's triangle rule (`vu_rec`), its region bounds (`vu_lo`, `vu_hi`), the triangle record's slots and a stage draw's tick (`tri`) |
| `IcoVuBoneCB` | b3 space1 | 3840 | VU memory 16..255, the cluster bones; the card bits after them |

Constants mirrored from the HLSL keep the same values: `ICO_DF_*` (`common.hlsli`),
`ICO_DF_WMASK_SHIFT` (19), `ICO_TEXFMT_*` (`gs_math.hlsli`, asserted in `gs_math_test.c`),
`ICO_SHEET_*` (`sheet_text.hlsli`, asserted in `port/render/test/sheet_ref.c`), `ICO_VU_*` and the
triangle marks `ICO_VU_MARK_LATER`, `_HIDE`, `_JOINED` (`VU_MARK_*` in `vu_common.hlsli`), the vertex
pre-pass's `ICO_VU_REC_SHIFT` (`VU_REC_SHIFT`), `ICO_VU_TRI_CHAIN` and `_HIDE` (`VU_TRI_*`), and `ICO_SH_FRAME_*`, the
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
`gs_blend_reg_ch` (from the ALPHA register), `gs_dst_channel` (one channel of the destination read),
`gs_z_to_depth` (a GS Z to a depth that grows with it), `gs_wrap_repeat` (REPEAT's residue) and
`gs_div_trunc` (an integer division) ([Integer forms](#integer-forms)) (`port/shaders/gs_math.hlsli`).

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

No vertex shader has a loop ([The envelope](#the-envelope)). Every loop in the pixel shaders stops
within a compile-time constant number of iterations, whatever its inputs: a GPU computing a wrong
value (an unstable overclock) gives a wrong pixel, never a loop that runs until the driver resets
the device. Loops with constant bounds: the B-spline's two axes (`port/shaders/common.hlsli`,
gs_bspline_bilinear4), the CRT's blur taps and channels (`port/shaders/crt.hlsl`, crt_bloom_ps,
crt_blur_ps, gainFadeOf) and the deinterlacer's seven columns (`port/shaders/yuv.hlsl`,
field_sample). Loops whose count comes from a constant buffer or arithmetic are clamped to a
constant that valid inputs never reach, so every picture is unchanged:

| loop | clamp | valid inputs need at most |
| --- | --- | --- |
| above 1x taps (`port/shaders/sprite.hlsl`, sprite_above1x; `port/shaders/fx_sprite.hlsl`, fx_sprite_ps) | `GS_RS_TAPS_MAX` = 33 a side (`common.hlsli`) | 33: a 32-texel box over a texel boundary (gs_rs_count) |
| shadow count reduce (`port/shaders/blit.hlsl`, box_reduce_ps) | `BLIT_SPAN_MAX` = 258 texels an axis | 148 (a 32768-texel scene over 224 rows) |
| supersampling reduce (`blit.hlsl`, aa_reduce_ps) | `BLIT_SPAN_MAX` = 258 texels an axis | 258 where its 32-bit sums are exact (256 texels a pixel) |
| MSAA depth resolve (`blit.hlsl`, depth_resolve_ps) | 8 samples | 8 (`port/render/rd_core.c`, rd_aa_samples) |
| CRT triad gain (`crt.hlsl`, triadGain) | `CRT_TRIAD_REACH_MAX` = 128 | 65 (a 32768-pixel box over 512 columns) |

No shader waits on memory, spins on an atomic or uses recursion; the one ordered section is
`gs_dst_out`'s (above).

## Integer forms

Integer division and remainder by a value that is not a compile-time constant (`OpSDiv`, `OpSRem`,
`OpUDiv`, `OpUMod`) have no instruction on the GPUs the port runs on: the driver expands each into
a reciprocal and correction steps, tens of instructions. A pixel shader on a hot path writes them
in these forms instead, each the same value for every input it can be given, with the argument in
the code and a CPU sweep against the plain operator in `gs_math_test`:

- **REPEAT addressing** is `gs_wrap_repeat(c, n)` (`port/shaders/gs_math.hlsli`): `c & (n - 1)`
  when `n` is a power of two, else `((c % n) + n) % n`. In two's complement the low bits of `c`
  are its residue modulo a power of two, negative `c` included, so the two sides agree for every
  `int`; the test is on a texture's size, kept a branch (`GS_BRANCH`, SPIR-V `DontFlatten`) so the
  modulo runs only for a size that is not a power of two. The size is the draw's, except on the mip
  paths, which take the size at the pixel's level (`gs_sample_lod_nearest`, `gs_texa_level` in
  `common.hlsli`), so there the test is per pixel: every level of a 2^TW x 2^TH texture is a power
  of two, and only a replacement chain whose size is not can take both sides within a wave, with
  the same values. Users:
  `gs_texa_load` and `gs_load_lv` (`common.hlsli`), `fx_texel` (`fx_sprite.hlsl`).
- **Integer division** is `gs_div_trunc(n, d)` (`gs_math.hlsli`): `n / d` truncated toward zero
  as `OpSDiv` gives it, from the float quotient of `|n|` and `d` and one integer correction each
  way, exact for `1 <= d <= 2^24` and `|n| < d * 2^21`. There the float quotient is within 0.57
  of the exact one (the conversion within one ulp, the divide within 2.5 ulp: Vulkan's `OpFDiv`
  precision, which the proof assumes of the other APIs' divides too), so its integer
  part is the floor or one off, and the remainder `|n| - q * d` (exact modulo 2^32) says which.
  The function's comment carries the bounds of `fx_sprite_ps`'s UV step
  (`(X - x0) * (u1 - u0) / (x1 - x0)`: `|n| <= (d + 30) * 65543`); a new user proves its own.
  `gs_math_test` sweeps it against `/` with the float quotient moved to the corners of those
  bounds.
- **Integer division by a per-draw divisor** is `gs_div_trunc_rcp(n, d, rd)` (`gs_math.hlsli`),
  `gs_div_trunc` with the quotient taken as `float(|n|) * rd` (`precise`), `rd` the correctly
  rounded `1.0f / d` the CPU computes once a draw, and the same correction: the conversion, `rd`
  and the product each within half an ulp, so below `2^21` the quotient is within 0.375 of the
  exact one. `fx_sprite_ps`'s UV step is its user, the reciprocals in `DrawCB.aux1.xy`
  (`port/render/rd_replay.c` (`blurAux1`)), with `fx_texel`'s wrap size in `aux1.zw` (the
  `max(1, round(size * scale))` it computed per pixel, `rintf` for the shader's `RoundEven`).
  `gs_math_test` sweeps it over `gs_div_trunc`'s operands with the conversion, `rd` and the
  product each moved a whole ulp either way (the bound there 0.9375).
- **A divide by a per-draw float that is often 1** is skipped in a branch when it is 1
  (`fx_sprite_ps`'s coverage, `fp / ts` at scale 1): the reciprocal of 1.0 is 1.0 and `x * 1.0`
  is `x`.

## The envelope

The vertex shaders are written in the forms of a set proven on every device the port has failed on
(the VU vertex shaders that built on an NVIDIA GeForce 10-series card, a Qualcomm Adreno 740 and a
PowerVR DXT-48-1536; the tree is named in `tools/check_spirv.py`), and `spirv_check` holds every
compiled vertex shader to them:

- **The shape.** No vector of booleans (`OpTypeVector` of `OpTypeBool`), no `any()` or `all()`
  (`OpAny`, `OpAll`) and no select whose result is a boolean or a vector of booleans. The PowerVR
  driver compiled every VU vertex shader written as scalar comparisons, branches and selects
  between numbers, and crashed (SIGSEGV inside its compiler, on the first pipeline with a VU vertex
  shader, and on `vu_particle_vs` alone) on a set with the same opcodes but 1,476 comparisons of
  float vectors, 1,660 selects of boolean vectors and 116 `any()`.
- **The opcodes.** Only the opcodes of that set's vertex shaders and of every 2D vertex shader,
  compiled with the pinned DXC and the build's flags, less `OpUndef` (`VS_OPCODES`). Four
  constructs lie outside it that the GeForce and Adreno drivers' compilers crash or hang on in a
  vertex shader: loops with run-time trip counts (`OpLoopMerge`), `dot()` (`OpDot`), a comparison of
  two booleans (`OpLogicalNotEqual`) and undefined values (`OpUndef`), named in the finding
  (`DENIED`).
- **Arrays.** No shader builds, copies or carries a whole array as a value, and no vertex shader
  keeps a local array (one in SPIR-V's Function storage class); an array in a constant buffer, a
  storage buffer or a stream is read one element at a time, and the pixel shaders with a local
  array indexed at run time (`font.hlsl`, font_sheet_ps; `yuv.hlsl`) build on those drivers and are
  allowed.

**How the code is written** (`port/shaders/vu_common.hlsli`, header comment). Boolean logic is
scalar comparisons joined with `&&`, `||` and `!`, which DXC makes branches; a choice is an `if` or
a `?:` between numbers; flags combined after they are computed are uints, 1 or 0 (`vu_clipw`'s
bits, `VuVtx.inside`, `vu_has_gs_position`), joined with `&`. No vector is chosen after it is
computed: DXC's optimiser makes a phi of two vectors that exist before the branch into a select on
a vector of booleans, so a vector is chosen value by value (`vu_pick`, the triangle's position in
`vu_triangle_out_pos`, the culled position in `vu_plane_out`) or computed inside its branch, and
the GS integers are three scalars in `VuVtx`. A struct is initialised whole (`(VuVtx)0`) and no
vector is built with a lane nothing reads (the optimiser leaves an `OpUndef` there); an unsigned
comparison is written with `<`, `>=`, `==` or `!=`. A mesh program evaluates its three vertices in
full, and the tests' probe vertex is the first of them (`vu_mesh_out`, the programs' `*_vs`).

**The GPU's work.** One cut a vertex, at the blended camera, for a triangle not all inside the
region (and not under the free camera); the two ticks' tests only under `VU_F_TICKS` and only for a
triangle the blended camera dropped, tick 0 then tick 1, the second skipped when the first showed
it ([VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-region-rule); `vu_region_rule`, `vu_tick_shown`).

**The cut** (`vu_cut_z`, `vu_cut_rows`). The region rule asks whether what the GPU draws of a
triangle reaches a rectangle: the triangle cut to `0 <= z <= w` and to the rectangle's rows
(Sutherland-Hodgman, plane by plane), its `x / w` extent met with the columns. A corner is four
scalars (three after the z planes); a plane's corners are registers, the polygon the last `n` of
them; a corner is appended by moving the registers down one and writing the last, each a `?:`
between numbers on the step's flag, and a plane's steps are written out for the most corners it
can be given (3, 4, 5, 6: a convex polygon cut by a plane gains at most one corner; one that
rounding has made gain more keeps its first ones, as the cut always kept its first seven). The
distance to a plane is its dot product in the VU's order with the plane's zero and unit products
folded, exact for a finite corner: `z`, `w - z`, `top w - y`, `y - bottom w`, each `precise`; the
crossing is `lerp(a, b, da / (da - db))`; the sign change a comparison of the two signs' flags. The
z planes and the rows are apart, so a tick's picture test and frame test share its z cut. The cut
is 1,073 instructions without a branch; the corner-array form with a loop over the corners was
3,098 instructions in 260 blocks, executed in part.

**The size.** Each module has a budget in 32-bit words, `VS_BUDGET` (56,000, a tripwire about a
tenth above the largest) for a vertex shader, `VS_PLAIN_BUDGET` (25,700) for a VU vertex shader's
plain variant ([The variants](#the-variants)) and `PS_BUDGET` (78,560, the largest pixel shader's
71,418 and a tenth) for a pixel shader, and `spirv_check` fails a module over it; `--sizes` prints
each module's words, instructions, basic blocks, phis, loops, dot products, vector comparisons,
boolean selects and `any()`. A phone's driver compiles the start-up set in a time about
proportional to the modules' size (the renderer logs the largest at start and the compile time per
vertex shader after the set, [RENDER.md](RENDER.md#pipelines)).

| module | words | instructions | blocks | phis |
| --- | --- | --- | --- | --- |
| `vu_prelit_vs` | 50,092 | 10,692 | 861 | 327 |
| `vu_lit_vs` | 51,005 | 10,887 | 861 | 327 |
| `vu_skin_vs` | 48,846 | 10,307 | 707 | 261 |
| `vu_grid_vs` | 46,470 | 9,801 | 716 | 264 |
| `vu_particle_vs` | 10,000 | 2,344 | 232 | 103 |

(the other entries of each file within 2,200 words of their family's; DXC v1.9.2609). More than
half of a mesh program is the two ticks' tests written out (each tick's three vertices, its z cut
and its picture and frame rows), a seventh the blended camera's cut. The same rule written with corner
arrays, loops and `dot()` compiles to 91,686 words, 19,920 instructions and 2,431 blocks in
`vu_prelit_vs`.

**The size against the Adreno 740** (issue 107). Five shipped sets of VU vertex shaders, each
compiled again from its release tag with the pinned DXC and the build's flags and measured with
`tools/check_spirv.py --sizes` (the table by tag is in that script's header), against what the
Adreno 740 (driver 512.676.53, compiler E031.41.03.47: the AYN Thor of issue 107, the AYN Odin 2
Portal of issue 93) did with the start-up set: words per module, and in brackets `vu_prelit_vs`'s
instructions and blocks (the first VU vertex shader of the set).

| set | `vu_prelit_vs` | `vu_lit_vs` | `vu_skin_vs` | `vu_grid_vs` | `vu_particle_vs` | Adreno 740 |
| --- | --- | --- | --- | --- | --- | --- |
| the PS2's rule, no cut | 15,537 (3,870 / 604) | 16,268 | 10,884 | 8,427 | 5,448 | built (issue 93) |
| the cut with loops, `dot()` and local arrays | 30,988 (7,699 / 1,180) | 32,709 | 26,530 | 20,975 | 8,200 | built (issue 107) |
| the cut and the tick cameras with loops | 47,535 (11,826 / 1,837) | 49,256 | 42,298 | 36,668 | 10,840 | hung (issue 93) |
| the same written out as select chains | 91,686 (19,920 / 2,431) | 93,407 | 86,449 | 80,819 | 10,840 | hung (issue 107) |
| the envelope's forms, no loop | 50,092 (10,692 / 861) | 51,005 | 48,846 | 46,470 | 10,000 | hung (issue 107) |

The modules that built are at most 32,709 words and 8,062 instructions; the sets that hung the
device start at 46,470 words and 9,801 instructions. Size tracks the outcome; the opcodes do not
(the second set had the loops, `dot()` and local arrays the envelope refuses and built; the last
has none of them and hung), nor do the blocks (861 in the last set's `vu_prelit_vs` against the
second's 1,180).

**The card test** is decided on the CPU ([VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-card-test)): the
vertex shader reads two bits a vertex from `VuBoneCB` (`vu_strip_card`) instead of walking the
strip.

### The variants

The record path's modules have one build each, on every device. The ten mesh program entries,
which only the fallback draws through, have two (issue 107): the extended one, `vu_X_vs`, the file
as it reads, and the plain one, `vu_X_plain_vs`, the same entry compiled with `ICO_VU_PLAIN=1`
(`port/shaders/CMakeLists.txt`, the ten mesh entries only), which leaves out the card test's step
of the region rule (`vu_region_rule_cut`'s `VU_F_EXTENDED` branch) and the two tick cameras
(`vu_tick_shown`). The renderer takes the plain ones for the fallback on a device of the table and
the extended ones everywhere else, once a run ([RENDER.md](RENDER.md#the-fallback-entries));
`vu_particle_vs` has the one build. The cut against the camera's picture stays in
both: at 4:3 a triangle wholly beside the picture is drawn, clipped, as in the extended one.

The start-up line names the set and why, first (`rd_pipeline.c` `rd__vu_policy_init`): `rd: VU
shaders: extended set (no device-table row, no lost start, no [dev] setting asks for the plain
one): the program entries (largest vu_lit_vs <words> words) and the mesh and grid draws' merge
builds of the same set; the record path's modules ...`, or `plain set by the device table on this
device (<row's reason>)`, `plain set after a lost start (<the host's reason>)`, `plain set by [dev]
(ICO_RD_VU_PLAIN=1)`, `extended set by [dev] (ICO_RD_VU_EXTENDED=1)`. The draw merge's own line
(`rd: VU merge: ...`) follows; with the merge on, its builds (`vu_X_merge_vs` or
`vu_X_merge_plain_vs`) are what the mesh and grid draws run.

What the plain set hides (issues 163 and 164): a triangle that fails the region test and reaches
into the picture is dropped by the plain set, as on the PS2, and drawn clipped by the extended
one unless it is a card. So a vertex placed far off the screen, in front of the eye, by anything
the two sets share (the program's arithmetic, the stream, VuCB, VuBoneCB) is invisible through
the plain set and drawn as a long sliver through the extended one; a clean picture through the
plain set does not place a fault in the extended-only code. The card and tick steps only choose
drop or clip; they never move a vertex. The VU vertex modules hold no Function or Private
variable and no `OpUndef` (`tools/spirv_poison.py --list`, every `vu_*_vs` of the build: none), so
a value read before it is written is not in the SPIR-V the drivers get.

| module | plain words | instructions | blocks | extended words | instructions | blocks | the set that built on the Adreno 740 (words) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `vu_prelit_vs` | 22,286 | 4,992 | 502 | 50,219 | 10,727 | 870 | 30,988 |
| `vu_lit_vs` | 23,199 | 5,187 | 502 | 51,132 | 10,922 | 870 | 32,709 |
| `vu_lit_spec_vs` | 21,088 | 4,664 | 417 | 49,021 | 10,399 | 785 | 25,494 |
| `vu_reflect_vs` | 21,584 | 4,803 | 426 | 49,517 | 10,538 | 794 | 26,406 |
| `vu_skin_vs` | 20,935 | 4,586 | 348 | 49,018 | 10,351 | 716 | 26,530 |
| `vu_skin_spec_vs` | 20,998 | 4,599 | 348 | 49,081 | 10,364 | 716 | 26,601 |
| `vu_skin_debug_vs` | 21,087 | 4,616 | 348 | 49,170 | 10,381 | 716 | 26,872 |
| `vu_grid_vs` | 18,476 | 4,052 | 346 | 46,409 | 9,787 | 714 | 20,975 |
| `vu_grid_lit_vs` | 19,293 | 4,227 | 346 | 47,226 | 9,962 | 714 | 22,576 |
| `vu_grid_spec_vs` | 19,344 | 4,237 | 346 | 47,277 | 9,972 | 714 | 22,659 |
| `vu_particle_vs` | 6,216 | 1,438 | 134 | 10,044 | 2,352 | 232 | 8,200 |

The vertex pre-pass's entries have one build each, without the triangle rule the variants differ
in: `vu_prelit_pre_vs` 2,120 words (483 instructions, 36 blocks), `vu_lit_pre_vs` 2,518 (568, 36),
`vu_lit_spec_pre_vs` 2,171 (487, 29), `vu_reflect_pre_vs` 2,356 (539, 32), `vu_skin_pre_vs` 2,729
(611, 29), `vu_skin_spec_pre_vs` 2,744 (614, 29), `vu_skin_debug_pre_vs` 2,811 (627, 29),
`vu_grid_pre_vs` 1,820 (416, 32), `vu_grid_lit_pre_vs` 2,184 (493, 32), `vu_grid_spec_pre_vs`
2,203 (497, 32); `vu_pre_ps` 183. The record stages and the main shaders have one build each
(words, instructions, blocks; this DXC and the build's flags): `vu_tri_rule_pre_vs` 16,515
(3,600, 305), `vu_tri_tick_pre_vs` 16,614 (3,462, 212), `vu_tri_planes_pre_vs` 4,269 (972, 90),
`vu_particle_pre_vs` 8,801 (2,071, 200), `vu_main_vs` 6,277 (1,481, 174), `vu_particle_main_vs`
4,249 (986, 102); `vu_ps` 5,512 (1,311, 191) and `vu_texa_ps` 11,655 (2,804, 477), the one pixel
shader of both ways, and the no-planes builds `vu_np_ps` 4,927 (1,187, 189) and `vu_texa_np_ps`
11,063 (2,679, 475), and their `_nodual` twins `vu_np_ps_nodual` 4,952 (1,196, 193) and
`vu_texa_np_ps_nodual` 11,088 (2,688, 479). The frame-size sprite modules (words, instructions,
blocks): `fx_sprite_ps` 8,557 (2,087, 360), `fx_frame_ps` 11,439 (2,771, 476), `fx_local_ps` 8,561
(2,088, 360), `fx_blur2_ps` 14,883 (3,491, 558). By the live-path walk (the SPIR-V executed for one pixel, its arithmetic
weighted by components) an opaque textured pixel of `vu_ps` runs 168 instructions and 127
lane-operations, of `vu_np_ps` 124 and 80. The pixel side's colour arithmetic runs on the GS's
integers held as floats (`vu_common.hlsli` `vu_col_int`, `vu_texel_int`,
`vu_texture_function_f`, `vu_dual_out_f`): every value an integer under 2^16, so each step is exact
and the result is the integer form's (`gs_texture_function`, `gs_dual_out`) bit for bit, without
the conversions between the two. A NaN texel (sampled at a NaN UV, where Q is 0) and a NaN colour
of the planes become 0, as the integer form's conversion gives them (`max(., 0)`; HLSL's `min`
would pass a NaN as its other operand, 255). `spirv_check` holds the pre-pass entries, the record stages (`_pre_vs`) and the main
shaders (`_main_vs`) to the plain budget and asks a plain variant only of the ten mesh programs'
entries (`tools/check_spirv.py` `PROGRAM_ENTRIES`, gates `size per variant` and `variants`). With
the pre-passes a device compiles the two main shaders, the ten pre-pass entries and the five
record stages in place of the program entries, none over 16,515 words.

The draw merge's builds (`ICO_VU_MERGE`: `vu_X_merge_vs` and `vu_X_merge_plain_vs` of the seven
mesh and grid programs but the skinned ones, `vu_merge_main_vs`;
[VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-draw-merge)) are the same entries reading VuCB and VuBoneCB
from t5 through a four-step search, about 500 words above each: `vu_lit_merge_vs` 51,950 of
`VS_BUDGET`, `vu_lit_merge_plain_vs` 23,904 and `vu_merge_main_vs` 6,827 of the plain budget (a
`_merge_plain_vs` and a `_main_vs` are held to it), none with a finding. With the merge on (every
device but Direct3D 11) a device compiles them in place of the plain builds of the same entries,
whose modules the merge leaves unchanged word for word (`VU_MERGE_ENTRY(vid)` is `vid` there).
The merge builds of `prelit`, `lit` and the main shader also set `VuVSOut.r.w` on the kick
triangles of a fused scissor pair, and the plane distance to -1 where the draw has no clip plane
(`vu_fused_out`, 93 to 108 words; the others pass a literal clip mode and are unchanged), which
`vu_ps` and `vu_texa_ps` read (`r.w`: `vu_dual_out_fused`, 142 words each) and the no-planes twins
`vu_np_ps` and `vu_texa_np_ps` (the plane's sign: one more argument of `vu_dual_out_f`, 44 words
each); their no-dual and destination-read twins are unchanged word for word
([VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-fused-scissor-pair)).

Every plain module is smaller than the same module of the last set that built on the Adreno 740 in
words, instructions and blocks.
The plain variant's budget is its own, `VS_PLAIN_BUDGET` (25,700, a tenth above `vu_lit_plain_vs`);
the extended ones keep `VS_BUDGET`, and `spirv_check` fails a build directory where a `vu_X_vs`
has no `vu_X_plain_vs` (`tools/check_spirv.py`, gates `size per variant` and `variants`). The
cut is about 9,500 of a plain mesh program's words (`vu_prelit_vs` without it: 12,866); it stays,
because at 4:3 it decides whether a triangle wholly beside the picture is drawn, and the plain
variant must give the extended one's picture.

## Direct3D 11: run-time compile

The Direct3D 11 backend takes no blob from the table: shader model 5.0's compiler, FXC, exists only
on Windows, and the build hosts are Linux, so the program compiles the HLSL itself at run time with
`d3dcompiler_47.dll` and keeps the results on disk. FXC at build time (no Linux FXC), SPIR-V
translated to HLSL (still FXC) and committed DXBC (stale with no Windows tester) were the
alternatives; each needs a Windows machine in the build.

**What the table carries.** With `ICO_SHADERS_HLSL_SOURCE` (on where `ICO_RHI_D3D11` is) every
`*.hlsl`, `*.hlsli` and `*.h` of `port/shaders` is embedded by base name in `g_icoShaderSources`, and
every entry names its file (`hlslFile`) and its macros (`defines`, `"A=1,B=2"`, the entry's
`DEFINES`); `rd__make_shader` hands them to the backend in `RhiShaderDesc` (`hlslFile`, `defines`,
`sources`, `sourceCount`, `entryPoint`) instead of a blob (`port/shaders/embed_shaders.py`;
`port/render/rd_replay.c`).

**Flat registers.** FXC has no register spaces. Every declaration goes through `ICO_REG(r, s)`
(`port/shaders/reg.hlsli`): `register(r, space s)` for DXC, `register(r)` under `ICO_D3D11`. The
register numbers are distinct across the groups (`b0`; `b1`..`b3`, `t0`, `t4`, `t5`; `t1`..`t4`,
`s1`..`s4`; `u0`), so the flat namespace has no collision. A source line that names a space in
`register(...)` outside a comment, a string, a preprocessor line or `reg.hlsli` breaks the rule
(`d3d11_shader_check_sources`).

**The compile.** `d3d11_shader_compile` (`port/rhi/d3d11/d3d11_shader.c`) runs on the backend's
pipeline workers, concurrently: it loads `d3dcompiler_47.dll` once (System32 first, then the usual
search; `InitOnceExecuteOnce`), resolves `hlslFile` among the sources, and calls `D3DCompile` on
that text with the target `vs_5_0` or `ps_5_0`, `D3DCOMPILE_OPTIMIZATION_LEVEL3`, the entry's
macros plus `ICO_D3D11=1`, and an `ID3DInclude` written as a C vtable whose `Open` hands out the
embedded text of the file named (by name, else by its last path component) and whose `Close` does
nothing. Warnings do not fail the compile; the first warning text is logged once. The DXBC is
`malloc`'d for the caller.

**The cache.** One file per shader, `<dir>/<key>.dxbc` (16 hex digits), read before compiling (a
hit skips the compile) and written after (`<path>.<n>.tmp` renamed over the file, so workers that
compile the same key at once leave one whole file). The key is FNV-1a 64 over the entry, the stage,
the file name, the macros, every embedded source's name and text in the table's order, the
compiler's file version and the program's version, each field length-prefixed
(`d3d11_shader_cache_key`); FNV, not xxh3, because `port/rhi` does not depend on `port/render`.
The file:

| offset | bytes | holds |
| --- | --- | --- |
| 0 | 8 | `ICODXBC1` |
| 8 | 8 | the key, little-endian |
| 16 | 8 | the DXBC's length, little-endian (at most 16 MiB) |
| 24 | 8 | FNV-1a 64 of the DXBC, little-endian |
| 32 | length | the DXBC container (`DXBC` ...) |

A file with another magic or key, a length that does not match the file, a wrong hash or a body
that is not a DXBC container is a miss, and the compile writes over it
(`d3d11_shader_cache_read`, `d3d11_shader_cache_write`).

**Log lines** (`stderr`, so `ico-pc.log`):

| line | when |
| --- | --- |
| `rhi_d3d11: shader compiler d3dcompiler_47.dll loaded (<file version>) from <path>` | the first compile; `version unknown` when the file carries none |
| `rhi_d3d11: d3dcompiler_47.dll not found or without D3DCompile (error <n>): the Direct3D 11 renderer cannot compile its shaders` | once, instead |
| `rhi_d3d11: shader <name>: <file> <entry> (<target>) failed to compile (HRESULT 0x...): <compiler message>` | the first eight failures (a shader with no source or malformed macros says so instead) |
| `rhi_d3d11: <n> shaders failed so far` | every 100th failure after them |
| `rhi_d3d11: shader compiler warnings (first shader with any, <name>): <text>` | once |
| `rhi_d3d11: shader cache <dir> could not be written: the shaders are compiled at every start` | once |
| `rhi_d3d11: shader cache <dir>: <n> hits, <m> compiles, <k> failed` | `d3d11_shader_stats(dir, &stats, true)`, for the backend to call after the start-up set and at shutdown |

**The DXC check.** `shaders_d3d11_check` (option `ICO_SHADERS_D3D11_CHECK`, on; outside `all`)
compiles every entry again with DXC, `-HV 2018 -D ICO_D3D11=1` and its own macros, to blobs under
`<build>/shaders/d3d11_check` that nothing embeds; the CI's linux job builds it. It proves the
D3D11 preprocessing compiles on the build hosts. Only Windows runs FXC itself, so an FXC-only
error (shader model 5.0 limits, FXC's own warnings) shows first in a Windows player's log
(`cmake/IcoShaders.cmake`).

The commonest FXC-only error is a word FXC reserves used as a name (`bool pass` failed every
Direct3D 11 shader): `hlsl_fxc_check` (`tools/check_hlsl_fxc.py`) scans the HLSL for them.
It cannot catch FXC's X4000 "potentially uninitialized variable" errors.

## GLSL for the OpenGL backend

The OpenGL backend takes GLSL 4.30 text, translated from the SPIR-V the build already has, not
written by hand: the HLSL stays the only source. `ico_add_shader` adds one more custom command per
entry under `ICO_SHADERS_GLSL` (on by default when `ICO_RHI_OPENGL` is on and
`tools/toolchain/deps/spirv-cross/bin/spirv-cross` exists; `ICO_SPIRV_CROSS` names another
executable): `tools/spirv_to_glsl.py` runs
`spirv-cross --version 430 --no-es --combined-samplers-inherit-bindings <name>.spv` and writes
`<name>.glsl`. SPIRV-Cross is a build tool like DXC (`tools/fetch_deps.sh` section 9,
`SKIP_SPIRV_CROSS=1`; [THIRD_PARTY.md](../THIRD_PARTY.md)); nothing of it is linked or shipped.
`embed_shaders.py` deflates each file (zlib) into the table's `glsl` column, with the text length in
`glsl_text_len`; the backend inflates it with `rhi_gl_inflate_glsl` (`port/rhi/gl/gl_glsl.c`, miniz)
when it creates the shader. The deflated GLSL of the 127 translatable modules is about 1.2 MB.

**The binding map** (observed on the compiled modules; the SPIR-V numbers carry over unchanged):

| HLSL | GLSL |
| --- | --- |
| `b#` constant buffer | `layout(binding = #, std140) uniform`, uniform buffer binding # |
| `t#` structured buffer | `layout(binding = 16 + #, std430) readonly buffer`, shader storage binding 16 + # |
| `t#` texture with `s#` sampler | `layout(binding = 16 + #) uniform sampler2D`: SPIRV-Cross's combined sampler inherits the texture's binding, so the texture unit is 16 + # whatever the sampler (the `s#` slot has no GLSL object) |
| `t#` texture read only with `Load` | the same combined sampler, built with a dummy sampler, at unit 16 + # |
| `u#` storage image | `layout(binding = #, rgba8) uniform image2D`, image unit # (`tools/spirv_to_glsl.py` takes the SPIR-V's 48 off: OpenGL 4.3 guarantees only 8 image units) |
| dual-source output | `layout(location = 0, index = 0)` and `layout(location = 0, index = 1)` |

Two combined samplers of one texture (one with the dummy sampler, one with `g_sampler`) get the
same binding: one unit, bound once. The vertex inputs keep their SPIR-V `location`s.

**What GLSL cannot express.** An input attachment (SPIR-V `OpTypeImage` with Dim SubpassData) has
no GLSL, so the script writes an empty file for such a module and `embed_shaders.py` reads an empty
file as none (glsl NULL, glsl_len 0). That is 17 entries: the `_dstatt` builds of the sprite, texa,
aa1, stq, font, font_sheet, fog_lut, fog_lut_buffer, vu and vu_texa pixel shaders (and the planes
variants), `fog_lut_local_ps`, `fog_lut_local_ps_nodual`, `date_stencil_ps` and `fx_local_ps`. The
`_dstrov` builds (a storage image under fragment-shader interlock) translate.
A backend without fragment-shader interlock has no blend path from these modules (see the
extensions below).

**The extensions** the GLSL asks for (`#extension` lines): `GL_EXT_control_flow_attributes` behind an
`#if defined` guard (optional) in 38 modules; in the 13 `_dstrov` modules
`GL_ARB_fragment_shader_interlock` or `GL_INTEL_fragment_shader_ordering` behind a guard that ends in
`#error` (one of the two is required: Mesa's llvmpipe has neither); `GL_ARB_texture_query_samples`
(required) in `depth_resolve_ps`. No module needs `GL_ARB_enhanced_layouts` (no `layout(offset`).

**The gates** (`tools/spirv_to_glsl.py`): the text starts with `#version 430` (`#version 450` for a module that queries a multisampled texture's sample count, `textureSamples()`: the MSAA depth resolve, `depth_resolve_ps`; a driver below OpenGL 4.5 refuses it and the renderer treats it as missing); every NoContraction
decoration of the SPIR-V surfaces as `precise` in the GLSL (a module with the decoration and no
`precise` fails the build; the counts differ by a few where SPIRV-Cross folds a decoration into one
declaration); an image uniform still at a binding of 48 or more after the u-register remap (a
declaration form the remap does not match) fails the build; `embed_shaders.py` checks a non-empty file starts with `#version`;
`shaders_table` requires that with GLSL on exactly the entries of `expectedNoGlsl` have none.
`spirv_to_glsl.py --spirv-cross <exe> --list --in <spv>...` prints name, bytes, extensions and the
two counts per module. `port/rhi/test/shaders/gen_shaders.sh` writes the RHI test shaders' GLSL
the same way into `rhi_test_glsl.h`.

## Tests

| test | checks |
| --- | --- |
| `shaders_table` (`port/shaders/test/shaders_table_test.c`) | every embedded entry: SPIR-V with its magic, DXIL a signed DXBC container (none with `ICO_SHADERS_DXIL=OFF`), a known stage and entry, unique names, the expected names present, and with GLSL on a zlib stream for every entry but the 17 of `expectedNoGlsl` |
| `gs_math` (`gs_math_test.c`) | `gs_math.hlsli` as C against `port/test/gs_blend_test.c`'s formulas and the GS rules for TEXA, the alpha test and the Z mapping: exhaustive where the domain is small; the shared constants pinned; the [integer forms](#integer-forms) against the operators they replace |
| `shaders_pixel` (`shaders_pixel_test.c`) | `sprite.hlsl`, `blit.hlsl` and `blend_int.hlsl` through the Vulkan RHI, texel by texel against `gs_math.hlsli` as C: untextured, modulate, the alpha test, dual-source lerps, additive with `DF_PREMUL`, PABE, FBA, tinted blits, three integer blends; `shaders_pixel_nodual` (`--nodual`, `ICO_VK_FAKE_NO_DUAL=1`) the two-pass fallback; `shaders_pixel_mali` under `ICO_VK_FAKE_LIMITS=mali` |
| `spirv_check` (`tools/check_spirv.py --sizes`) | every compiled SPIR-V module under the build's `shaders/`: a vertex shader without a vector of booleans, `OpAny`/`OpAll` or a boolean select, its opcodes inside the envelope's set (`OpLoopMerge`, `OpDot`, `OpLogicalNotEqual`, `OpUndef` named), every module within its size budget (a VU vertex shader's plain variant, a pre-pass entry or record stage `_pre_vs` and a main shader `_main_vs` within the plain one), every mesh program's entry with its `vu_X_plain_vs` (`PROGRAM_ENTRIES`), the largest module of each budget class printed last, no array-typed value (OpLoad, OpStore, OpCompositeConstruct, OpCompositeInsert, OpPhi, OpSelect, OpCopyObject, OpCopyLogical of an array type) and no local array in a vertex shader ([The envelope](#the-envelope)); the sizes and the shape census printed; the module's binary form decoded in Python, no SPIR-V tool needed |
| `shaders_d3d11_check` (a build target, `cmake/IcoShaders.cmake`) | every entry compiled by DXC under `ICO_D3D11=1` and HLSL 2018 ([Direct3D 11: run-time compile](#direct3d-11-run-time-compile)) |
| `rhi_gl_glsl` (`port/rhi/test/rhi_gl_glsl_test.c`) | the OpenGL backend's GLSL inflate: a miniz-deflated text round-trips NUL-terminated, a wrong text length, a corrupt, truncated or missing stream give none |
| `rhi_d3d11_shader_plan` (`port/rhi/test/rhi_d3d11_shader_plan_test.c`) | the Direct3D 11 route's portable part: macros, include lookup, the flat-register rule over the game's sources and the RHI test shader, the cache key over every table entry, the cache file |
| `vu1` (`vu1_test.c`) | the VU program shaders against `port/render/vu1_ref/`: hand traces through the `.vsm` code on the CPU, then on Vulkan every vertex shader's probe output, strips drawn through the shipped entries, and the region rule's clipping with the card verdicts handed over as the renderer does; every case whose draw has neither `VU_F_EXTENDED` nor `VU_F_TICKS` drawn again through the plain variants, its probe and pictures byte for byte the extended ones' ([The variants](#the-variants)); the CPU's card test against the reference on every vertex and strip; every case through the record (a mesh case's vertices through its pre-pass entry and its triangles through the record stages, the tick stage under the tick cameras; a particle case's sprites through `vu_particle_pre_vs`), its probe and pictures through `vu_main_vs` or `vu_particle_main_vs` and `vu_ps`, byte for byte the program entry's with `vu_legacy_ps`, the mesh, particle and tick-camera cases counted (none may be zero); the plain variants for the mesh programs' entries; `vu1_mali` under the Mali limits |
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

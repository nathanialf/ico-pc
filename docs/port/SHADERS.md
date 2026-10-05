# Shaders

HLSL sources in `port/shaders/`, compiled at build time by DXC to SPIR-V
(Vulkan) and signed DXIL (D3D12) and embedded in the program as C arrays.
There is no runtime shader compiler. Contracts: `docs/port/RENDER_API.md`
(the GS rules the shaders implement), `port/rhi/rhi.h` and
`port/rhi/vk/README.md` (bind model). Tests: `port/shaders/test/`.

## Toolchain

| piece | where |
| --- | --- |
| DXC v1.9.2609, Linux x86-64 release, SHA-256 pinned | `tools/fetch_deps.sh` into `tools/toolchain/deps/dxc/{bin,lib}` (`docs/port/THIRD_PARTY.md`) |
| `ico_add_shader`, `ico_shaders_library` | `cmake/IcoShaders.cmake` |
| the shader list | `port/shaders/CMakeLists.txt` |
| blob embedding and checks | `port/shaders/embed_shaders.py` |
| generated `shaders_gen.c/.h`, `<name>.spv`, `<name>.dxil` | `<build>/shaders/` |
| library `ico_shaders` | link it to get `shaders_gen.h`: table `g_icoShaders[]` of `{name, stage, spirv, spirv_len, dxil, dxil_len, entry}` and `ico_FindShader(name)` |

DXC is a build tool. It runs on the build host for every preset (the
`win-x64` and 32-bit presets embed the same arrays; only the C compiler
changes) and nothing links against it. Without DXC (`fetch_deps.sh` not run)
the shaders and their tests are left out with a status message, like the
other fetched dependencies; `ICO_DXC=/path/to/dxc` overrides the location.
Only the Linux x86-64 release is pinned; a Windows or macOS build host needs
its own DXC release pinned the same way (not done).

Per entry the build runs DXC twice with `-O3 -WX` (a warning fails the
build; DXC's message is the build log, with file, line and caret):

    dxc -spirv -fspv-target-env=vulkan1.2 -fvk-b-shift 0 all -fvk-t-shift 16 all \
        -fvk-s-shift 32 all -fvk-u-shift 48 all -T vs_6_0|ps_6_0 -E <entry> -I port/shaders
    dxc -T vs_6_0|ps_6_0 -E <entry> -I port/shaders

Both shader models are 6.0. Vulkan-only attributes (`[[vk::location]]`,
`vk::index`) go through `VK_LOC`/`VK_DUAL` in `common.hlsli`, which expand to
nothing for DXIL.

### DXIL signing

Decision: sign at build time, on Linux, with the `libdxil.so` that ships in
the same release. Checked on 1.9.2609: `dxc -T ps_6_0` writes a DXBC
container whose hash is non-zero (the `-Vd` build has 16 zero bytes) and the
release's `dxv` reports "Validation succeeded". `embed_shaders.py` rejects
any DXIL whose container hash is zero, so a DXC without `libdxil.so` beside
it fails the build instead of embedding unsigned blobs. Consequences: D3D12
needs neither experimental shader models at device creation nor a Windows
signing step. Not yet exercised on a D3D12 device (no backend yet): if the
runtime rejects a blob, the fallbacks are `D3D12EnableExperimentalFeatures(
D3D12ExperimentalShaderModels)` (developer-mode machines only, not for
shipping) or re-signing with Microsoft's `dxil.dll` on Windows.

### Adding a shader

1. Write entries in an `.hlsl` under `port/shaders/` and `#include "common.hlsli"`.
2. Add one line per entry to `port/shaders/CMakeLists.txt`:
   `ico_add_shader(<name> <file.hlsl> <entry> <vertex|fragment>)`. The name is
   the table key; by convention it equals the entry.
3. Create it at run time from the table:
   `RhiShaderDesc{stage, b->spirv, b->spirv_len, b->entry, b->name}` (Vulkan) or
   `b->dxil` (D3D12).
4. Add the name to `expected[]` in `test/shaders_table_test.c`.

Helpers shared with the CPU live in `gs_math.hlsli`, which must stay valid
C (scalar only): `test/gs_math_test.c` includes it through `hlsl_shim.h`.

## Binding conventions

Group = register space, slot = register number (`port/rhi/vk/README.md`).

| group | slot | HLSL | content |
| --- | --- | --- | --- |
| 0 | 0 | `b0, space0` | `FrameCB`, per frame |
| 1 | 1 | `b1, space1` | `DrawCB`, per draw or post pass |
| 1 | 0 | `t0, space1` | storage buffer (bones, particles; later waves) |
| 2 | 1..4 | `t1..t4, space2` | textures (`sprite_ps`: t1 the texture, t2 the DATE snapshot) |
| 2 | 1..4 | `s1..s4, space2` | samplers |

Vulkan bindings: slot + 0 (b), + 16 (t), + 32 (s). Clip space is D3D:
x, y in -1..1, +y up, depth 0..1, reversed-Z (near 1, far 0, GEQUAL).

Textures are RGBA8 UNORM and read back as integers by
`round(sample * 255)`; the texture function and TEXA are integer maths on
those. Exact integer paths (`blend_int`) load `Texture2D<uint4>` with no
sampler.

## Constant blocks

C mirror with static asserts: `port/shaders/shader_consts.h`. Every member
is a 16-byte register or a 64-byte matrix, so there is no packing ambiguity.
Uniform ranges are aligned by the caller to `RhiLimits.uniformAlign`.

### FrameCB (group 0, slot 0, 320 bytes)

| offset | HLSL | meaning |
| --- | --- | --- |
| 0 | `column_major float4x4 g_view` | world to view (`matrixptr+0x80`) |
| 64 | `column_major float4x4 g_proj` | view to GS window coordinates and GS Z after the divide by w: the game's screen matrix `matrixptr+0xC0` (4:3), not a D3D clip matrix (wave 2, R2c) |
| 128 | `column_major float4x4 g_viewProj` | `g_proj` x `g_view`, world to GS window/Z (the product `matrixptr+0x100` holds) |
| 192 | `float4 g_cameraPos` | xyz world eye, w = 1 on a camera-cut tick |
| 208 | `float4 g_clip` | near, far, zoom, aspect |
| 224 | `float4 g_target` | bound target size in GS pixels (xy), 1/size (zw) |
| 240 | `float4 g_origin` | xy = XYOFFSET/16, the GS window coordinate of the target's top-left pixel; zw = added after (pixel-centre convention; 0 puts GS integers on pixel edges) |
| 256 | `float4 g_space[2]` | [0] WORLD, [1] UI: `ndc = ndc * xy + zw` (mirror flip, 4:3 anchoring, wide projection) |
| 288 | `float4 g_z` | x = the GS Z scale of the bound depth buffer: 2^-32 for PSMZ32 (SCENE and every depth target the game uses; wave 2, R2c), 2^-24 for PSMZ24 and with no depth bound, 2^-16 for PSMZ16 |
| 304 | `float4 g_misc` | frame counter, preset (0 Original, 1 Enhanced), reserved |

Matrices are column-major `float[16]` (element `[column * 4 + row]`), used
as `mul(M, v)` with column vectors. Since wave 2 (R2c) replay fills them from
the frame's `RdCamera` (`gsb_MakeCommonMatrix` sets it; a frame without one
keeps the previous frame's), and `rd_gsbase_test` checks the packing:
`camera_probe_ps` evaluates `mul(g_view, p)`, `mul(g_proj, mul(g_view, p))`
and `mul(g_viewProj, p)` on the GPU, which agree with the C products to
1e-5 relative and with `sceVu0RotTransPers` through `matrixptr+0x100` to
1/16 pixel in GS X/Y. No mesh entry reads them before wave 3.

### DrawCB (group 1, slot 1, 96 bytes)

| offset | field | meaning |
| --- | --- | --- |
| 0 | `uint4 g_col` | constant colour RGBA 0..255 (blit tint, fade colour); `fog_lut_ps`: x = the fog sprite's GS Z, y = its Z test (`RdZTest`) |
| 16 | `uint4 g_mode` | x flags `DF_*`; y = TEXA mode (`RdTexA`) \| TEXFMT << 8; z = ATST \| ate << 8 \| split << 16 (split 0 none, 1 keep passing fragments, 2 keep failing: the AFAIL FB_ONLY passes); w = AREF |
| 32 | `uint4 g_blend` | x ALPHA register (A \| B << 2 \| C << 4 \| D << 6); y FIX; z COLCLAMP |
| 48 | `float4 g_uvRect` | blit source rectangle in texels (u0, v0, u1, v1) |
| 64 | `float4 g_tex` | t1 size in texels (xy), 1/size (zw) |
| 80 | `float4 g_param` | kind specific: `blend_int` pixel offset (xy); `fog_lut_ps`: x = the GS Z scale of the depth it reads (2^-32 for SCENE) |

Flags: `DF_TEXTURED` 1 (TME), `DF_DECAL` 2 (else MODULATE), `DF_TCC_RGBA` 4,
`DF_FBA` 8, `DF_PABE` 16, `DF_FIX_FACTOR` 32 (blend factor from FIX, not As),
`DF_PREMUL` 64 (see below), `DF_DATE` 128 and `DF_DATM` 256 (wave 2: TEST.DATE
and DATM; `sprite_ps` loads the R8 snapshot at t2 in target pixels and
discards where its MSB differs from DATM; t2 is fetched only under
`DF_DATE`, so a 1x1 dummy is bound otherwise).

### Vertex (sprite.hlsl, font.hlsl; `IcoSpriteVertex`, 20 bytes)

| loc | RHI format | content |
| --- | --- | --- |
| 0 | `RHI_VTX_U16x2_UINT` | XY, GS 12.4 fixed point, window coordinates |
| 1 | `RHI_VTX_U32x1` | Z, GS value (`g_z.x` scales it) |
| 2 | `RHI_VTX_U8x4_UINT` | RGBA 0..255, alpha 0x80 = 1.0 |
| 3 | `RHI_VTX_F32x2` | UV in texels of t1 |

Colour is interpolated as float (0..255) and rounded to the nearest integer
in the pixel shader (sprite); font colour is flat. UVs are normalised in the
vertex shader with `g_tex.zw`.

## common.hlsli and gs_math.hlsli

| function | what |
| --- | --- |
| `gs_xy_to_ndc(xy, space)` | 12.4 XY to clip, through `g_origin`, `g_target`, `g_space[space]` |
| `gs_z_to_depth(z, scale)`, `gs_depth(z)` | `1 - z * scale` computed as `(zmax - z + 1) * scale` (zmax = the format's largest Z), 0 above zmax: reversed-Z; exact for every Z at 2^-24 and 2^-16 and for large Z at 2^-32, so the UI's 0xFFFFFF9B and 0xFFFFFFFF stay apart under PSMZ32 (wave 2, R2c; `rd__GsDepth` is the CPU copy for clears) |
| `gs_tfx_mod`, `gs_texture_function` | `min((tex * col) >> 7, 255)`; DECAL; TCC |
| `gs_texa_alpha`, `gs_texa_expand` | the three `RdTexA` modes for PSMCT24/16 texels (`TEXFMT_*`) |
| `gs_alpha_pass`, `gs_alpha_discard` | the eight `RdAlphaTest` compares; AFAIL split passes |
| `gs_date_discard` | TEST.DATE against the snapshot texel (`DF_DATE`, `DF_DATM`) |
| `DualOut`, `gs_dual_out` | `SV_Target0` colour/255 with the stored GS alpha (0x80 stays 0x80), `SV_Target1` factor As/128 or FIX/128; FBA, PABE |
| `gs_blend_ch`, `gs_blend_reg_ch`, `gs_blend_int` | `((A - B) * C >> 7) + D`, arithmetic shift, clamp or wrap; the ALPHA-register form for feedback passes on RGBA8_UINT |
| `fullscreen_triangle` | 3 vertices from `SV_VertexID`, no vertex buffer |

### Dual-source factor above 1.0 (finding)

`RENDER_API.md` section 3 says the dual-source factor makes As above 0x80
exact. Measured on llvmpipe (`shaders_pixel_test`, cell 8): a plain
`Cs*As + Cd` with As = 0xFF and `SRC1_COLOR` gives 128 where the GS formula
gives 191, because fixed-point blend inputs are clamped to 0..1 (the Vulkan
spec says the same for UNORM attachments, so other drivers should agree; the
test reports rather than asserts it). `DF_PREMUL` is the fix: the shader
outputs `min((Cs * factor) >> 7, 255)`, the exact GS term, and the pipeline
blends with source factor `ONE`. That covers the additive and subtractive
forms (`Cs*F + Cd`, `Cd - Cs*F`). It does not cover a LERP with factor above 1.0 (negative destination
weight) or `Cd*FIX + Cs` with FIX above 0x80; those need `blend_int` or the
clamp. `rd_core` should pick the PREMUL pipeline whenever As or FIX can
exceed 0x80.

## Shaders

| name | file | stage | notes |
| --- | --- | --- | --- |
| `sprite_ui_vs`, `sprite_world_vs` | sprite.hlsl | vertex | 12.4 GS coordinates; UI and WORLD apply different `g_space` entries; any topology |
| `sprite_ps` | sprite.hlsl | fragment | untextured or textured, texture function, TEXA, alpha test, DATE (t2), dual-source output |
| `date_snap_ps` | sprite.hlsl | fragment | wave 2: the bound target (t1, `Load`) alpha MSB into the R8 DATE snapshot, 1.0 where set; drawn with `blit_vs`, viewport = the target |
| `blit_vs` | blit.hlsl | vertex | fullscreen triangle, source rectangle from `g_uvRect` |
| `blit_ps` | blit.hlsl | fragment | one output; texture function with the tint, or the tint alone without `DF_TEXTURED` |
| `blit_fix_ps` | blit.hlsl | fragment | same, dual-source with the FIX factor |
| `camera_probe_ps` | blit.hlsl | fragment | wave 2 (R2c), tests only: FrameCB's matrices applied to `g_param`, written as float bytes into a 4 x 3 RGBA8 target (`rd__CameraProbe`); not in the reachable pipeline set |
| `blend_int_vs`, `blend_int_ps` | blend_int.hlsl | vertex, fragment | t1 = Cs, t2 = Cd (RGBA8_UINT), ALPHA register in `g_blend`, writes RGBA8_UINT; no sampler |
| `fog_lut_ps` | fog_lut.hlsl | fragment | wave 4 (R4c): `fog_DrawFog`'s sprite (`RD_POST_FOG`, `RENDER_API.md` section 15) behind `sprite_ui_vs`: t1 a copy of the depth (D32F, sampled as depth, `Load` at the texel the UV addresses, nearest), t2 the 256x1 RGBA8 LUT in index order; reconstructs the GS Z (`(zmax + 1) - d / scale`, the inverse of `gs_z_to_depth`), does the Z test in the shader (GEQUAL against `g_col.x`; Z of a passing pixel capped at the sprite's), index = Z bits 16..23, MODULATE with the vertex colour, alpha test, dual-source output with the state's blend |
| `fog_lut_vs` | fog_lut.hlsl | vertex | a fullscreen triangle with `fog_lut_ps`'s inputs; unused by rd (kept in the shader table) |
| `font_vs`, `font_ps` | font.hlsl | vertex, fragment | R8 atlas coverage times vertex alpha, UI space |

Shadows (wave 4, R4b; `RENDER_API.md` section 14) add no shader. The
volumes are `sprite_world_vs` on the CPU-projected GS window coordinates
with `sprite_ps` under colour mask 0, depth-tested, stencil INCR_WRAP or
DECR_WRAP (write mask 0x3F), no depth write (`RD_PROG_SHADOW_VOLUME`). The
resolve is `blit_vs`/`blit_ps` with the tint alone: six passes with a
stencil EQUAL test on one bit add 4 << k to RGB (ONE + ONE), a seventh
writes A 0x80 where the count is not 0. The blur chain and the composites
are ordinary screen sprites (`sprite_*`). The RHI cannot sample stencil, so
the count is read through stencil tests, not in a shader.

The fog (wave 4, R4c; `RENDER_API.md` section 15) replaces the R1c
placeholder of `fog_lut.hlsl`, which took the top byte of a 24-bit Z at
2^-24: the game's Z is PSMZ32 (2^-32) and the PSMT8H read of ZFog.c's Z copy
selects bits 16..23. Precision: SCENE's depth is D32F holding
`(zmax - z + 1) * 2^-32`; for the fogged range (Z up to 0xFFFFFF) the depth
lies in [1 - 2^-8, 1], where a float steps by 2^-24, so the reconstructed Z
is Z rounded to a multiple of 256. Bits 16..23 change only within 128 of a
multiple of 65536 (0.4 % of Z values), where the index can be one off; the
cap at the sprite's Z keeps 0xFFFFFF (stored as 2^24) at index 0xFF.

## Tests

| ctest | what |
| --- | --- |
| `shaders_table` | every entry present, SPIR-V magic, DXIL signed, stages and lookups (the compile itself is the build: a wrong shader stops ninja with DXC's text) |
| `gs_math` | `gs_math.hlsli` compiled as C against the formulas of `port/test/gs_blend_test.c`: texture function over all 65,536 pairs, blend over a dense grid for both COLCLAMP modes and the twelve ALPHA registers, the eight alpha tests, the three TEXA modes, the Z mapping |
| `shaders_pixel` | through the Vulkan RHI (exit 77 without a device): sprite cells (untextured, textured modulate, alpha test fail and pass, LERP_AS at 0x80 exact and 0x40 within 1 LSB, PREMUL additive exact, PABE), `blit_ps` identity copy (exact) and tint, `blend_int_ps` for LERP_AS, LERP_FIX 0x70 and a wrapping add on RGBA8_UINT (exact); validation errors fail it. Its sprite layout declares t2 (the DATE snapshot) since wave 2; the DATE test itself is in `rd_pixel` |

## VU1 programs (wave 3, R3c)

The five VU1 microprograms as vertex shaders, one pixel shader for all of
them. What the programs compute and where the shaders differ:
`docs/port/VU1_PROGRAMS.md`; CPU references: `port/render/vu1_ref/`; the
draw interface: `port/render/rd_mesh.h`.

| name | file | stage | program, code | notes |
| --- | --- | --- | --- | --- |
| `vu_prelit_vs` | vu_prelit.hlsl | vertex | normal_c 32, 34, 36 | clip mode from `vu_draw.z` |
| `vu_lit_vs` | vu_lit.hlsl | vertex | normal_l 32, 36 | clip mode from `vu_draw.z` |
| `vu_lit_spec_vs` | vu_lit.hlsl | vertex | normal_l 34 | region test |
| `vu_reflect_vs` | vu_lit.hlsl | vertex | normal_l 38 | region test |
| `vu_skin_vs`, `vu_skin_spec_vs`, `vu_skin_debug_vs` | vu_skin.hlsl | vertex | cluster 20, 22, 24 | bones in VuBoneCB |
| `vu_grid_vs`, `vu_grid_lit_vs`, `vu_grid_spec_vs` | vu_grid.hlsl | vertex | mesh 20, 22, 24 | batches with headers |
| `vu_particle_vs` | vu_particle.hlsl | vertex | particle 18 | non-indexed, 6 vertices a particle |
| `vu_ps` | vu_prelit.hlsl | fragment | all | `sprite_ps` on STQ: texture function, TEXA, alpha test, DATE, dual-source |
| `vu_probe_ps` | vu_prelit.hlsl | fragment | tests only | writes `VU_F_PROBE` values to an RGBA8_UINT target; not in the reachable pipeline set |

Shared code: `vu_common.hlsli` (the VU operations, the stream addressing,
the per-triangle decision, the GS-to-clip conversion, the pixel side).

### Bindings

| group | slot | HLSL | content |
| --- | --- | --- | --- |
| 1 | 0 | `StructuredBuffer<float4> vu_stream : t0, space1` | the vertex quadwords as the VIF unpacked them (Vulkan binding 16) |
| 1 | 1 | `DrawCB : b1, space1` | as for every program (material state for `vu_ps`) |
| 1 | 2 | `VuCB : b2, space1` | the VU memory image (below), 608 bytes |
| 1 | 3 | `VuBoneCB : b3, space1` | cluster only: `float4 vu_bone[240]` = VU memory 16..255, 3840 bytes |
| 2 | 1, 2 | `t1`, `s1`, `t2` | texture, sampler, DATE snapshot, as `sprite_ps` |

### VuCB (group 1, slot 2, 608 bytes; `IcoVuCB`)

| offset | HLSL | meaning |
| --- | --- | --- |
| 0 | `float4 vu_mem[36]` | VU1 data memory 0..35 as the program reads it: 0..15 the common block (`RdVuCommon`, qw 2 = UV offset xy and cluster fade alpha w), 16..27 the object matrices (normal: +0x140, +0x200, +0x80 times the node; mesh: 16..19; particle: 16..23), 28..35 the light matrices L1, L2 (cluster and mesh load them into registers; they go here) |
| 576 | `uint4 vu_draw` | x first quadword of the draw in `vu_stream`, y quadwords per vertex, z flags, w vertices per batch (0: one batch) |
| 592 | `uint4 vu_batch` | x quadwords before each batch's vertices (GIF tag 1; mesh tag and colour 2, or 3 with a Mesh3D buffer's VIF qword), y quadwords after them (a Mesh3D buffer's MSCNT: 1) |

Flags (`vu_draw.z`, `ICO_VU_*`): bits 0..1 the clip mode, 0 REGION (the
loops' region test: a triangle with a vertex outside is not drawn), 1 NONE
(normal_c 34: no test, X/Y wrap to 16 bits), 2 SCISSOR (code 36); 16 PROBE
(tests); 32 CUT_ONLY and 64 KICK_ONLY, the two draws of a scissor batch
(SCISSOR_COMMON's fans come before the batch's own triangles on the PS2).

### Drawing

Mesh programs: indexed triangle list, `RHI_TOPO_TRIANGLE_LIST`, 32-bit
indices `kick * 4 + corner` (`ICO_VU_INDEX`), `vertexOffset` 0 (Vulkan
adds it to `gl_VertexIndex`; the shaders read the vertex number from the
index value). kick is the vertex whose XYZ2 draws triangle kick-2, kick-1,
kick on the GS; the index buffer holds the triangles the strip flags and
batch starts allow (`vu1ref_StaticKicks`), the shader drops the ones the
region test or the scissor's trivial reject removes (all three corners go
to a point outside the clip volume). Every invocation evaluates the three
vertices' positions, so all corners of a triangle agree. Particles:
`rhi_CmdDraw(6 * count)`.

Positions: a triangle the GS draws as sent goes out with w = 1, X/Y from
the 12.4 integers through `g_origin`, `g_target` and `g_space[WORLD]`
(`vu_ndc`, the `gs_xy_to_ndc` formula), Z from the saturated GS integer
through `gs_depth`; colour and STQ are `noperspective` (the GS interpolates
linearly in screen space and divides S/Q, T/Q per pixel). A scissor
triangle that crosses a clip-space Z plane goes out homogeneous so the GPU
clips it (`vu_homogeneous_position`: z/w = gs_depth of the unsaturated GS
Z, so the GPU clips at GS Z 0 and 2^32 for PSMZ32).

### Tests

| ctest | what |
| --- | --- |
| `vu1` | `vu1_test.c`: hand traces of every program through the CPU reference (exact); every vertex shader's probe output against the reference (lavapipe: identical in round to nearest; within 4.3e-7 / 7.8e-7 relative in Z / STQ against a round-toward-zero reference); rendered strips against the reference's triangles drawn with `sprite_world_vs` (1 LSB); the RdVuCommon / VuCB layout. Exit 77 without a Vulkan device once the CPU part passes |

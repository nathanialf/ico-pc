# Shaders

The HLSL sources in `port/shaders/` are compiled at build time by DXC to
SPIR-V (Vulkan) and signed DXIL (D3D12) and embedded in the program as C
arrays. There is no runtime shader compiler. The GS rules the shaders
implement are in `docs/port/RENDER_API.md`; the bind model in
`port/rhi/rhi.h` and `port/rhi/vk/README.md`. Tests are in
`port/shaders/test/`.

## Toolchain

| piece | where |
| --- | --- |
| DXC v1.9.2609, Linux x86-64 release, SHA-256 pinned | `tools/fetch_deps.sh` into `tools/toolchain/deps/dxc/{bin,lib}` (`docs/port/THIRD_PARTY.md`) |
| `ico_add_shader`, `ico_shaders_library` | `cmake/IcoShaders.cmake` |
| the shader list | `port/shaders/CMakeLists.txt` |
| blob embedding and checks | `port/shaders/embed_shaders.py` |
| generated `shaders_gen.c/.h`, `<name>.spv`, `<name>.dxil` | `<build>/shaders/` |
| library `ico_shaders` | link it to get `shaders_gen.h`: the table `g_icoShaders[]` of `{name, stage, spirv, spirv_len, dxil, dxil_len, entry}` and `ico_FindShader(name)` |

DXC is a build tool. It runs on the build host for every preset (the
Windows presets cross-compile from Linux and embed the same arrays) and
nothing links against it. Without DXC (`fetch_deps.sh` not run) the shaders
and their tests are left out with a status message, like the other fetched
dependencies; `ICO_DXC=/path/to/dxc` overrides the location. Only the Linux
x86-64 release is pinned, so the build host must be Linux x86-64.

Per entry the build runs DXC twice with `-O3 -WX` (a warning fails the
build; DXC's message is the build log, with file, line and caret):

    dxc -spirv -fspv-target-env=vulkan1.2 -fvk-b-shift 0 all -fvk-t-shift 16 all \
        -fvk-s-shift 32 all -fvk-u-shift 48 all -T vs_6_0|ps_6_0 -E <entry> -I port/shaders
    dxc -T vs_6_0|ps_6_0 -E <entry> -I port/shaders

Both shader models are 6.0. Vulkan-only attributes (`[[vk::location]]`,
`vk::index`) go through `VK_LOC` and `VK_DUAL` in `common.hlsli`, which
expand to nothing for DXIL.

### DXIL signing

The DXIL is signed at build time, on Linux, with the `libdxil.so` that ships
in the same DXC release: `dxc -T ps_6_0` then writes a container whose hash
is non-zero, and the release's `dxv` validates it. `embed_shaders.py`
rejects any DXIL whose container hash is zero, so a DXC without
`libdxil.so` beside it fails the build instead of embedding unsigned blobs.
D3D12 therefore needs neither experimental shader models at device creation
nor a Windows signing step. If a D3D12 runtime ever rejected a blob, the
fallbacks would be `D3D12EnableExperimentalFeatures` with
`D3D12ExperimentalShaderModels` (developer-mode machines only) or
re-signing with Microsoft's `dxil.dll` on Windows.

### Adding a shader

1. Write the entries in an `.hlsl` under `port/shaders/` that includes
   `common.hlsli` (`crt.hlsl`, whose group-1 block is not DrawCB, declares
   its own bindings instead).
2. Add one line per entry to `port/shaders/CMakeLists.txt`:
   `ico_add_shader(<name> <file.hlsl> <entry> <vertex|fragment>)`. The name
   is the table key; by convention it equals the entry.
3. Create it at run time from the table:
   `RhiShaderDesc{stage, b->spirv, b->spirv_len, b->entry, b->name}`
   (Vulkan) or `b->dxil` (D3D12).
4. Add the name to `expected[]` in `test/shaders_table_test.c`.

Helpers shared with the CPU live in `gs_math.hlsli`, which must stay valid
C (scalar only): `test/gs_math_test.c` includes it through `hlsl_shim.h`.

## Binding conventions

Group = register space, slot = register number (`port/rhi/vk/README.md`).

| group | slot | HLSL | content |
| --- | --- | --- | --- |
| 0 | 0 | `b0, space0` | `FrameCB`, per frame |
| 1 | 1 | `b1, space1` | `DrawCB`, per draw or post pass |
| 1 | 0 | `t0, space1` | storage buffer (the VU vertex stream) |
| 1 | 2, 3 | `b2, b3, space1` | `VuCB`, `VuBoneCB` (VU programs) |
| 2 | 1..4 | `t1..t4, space2` | textures (`sprite_ps`: t1 the texture, t2 the DATE snapshot) |
| 2 | 1..4 | `s1..s4, space2` | samplers |

Vulkan bindings are the slot plus 0 (b), 16 (t) or 32 (s). Clip space is
D3D's: x and y in −1..1, +y up, depth 0..1. GS Z maps to depth so that a
larger (nearer) GS Z is a smaller depth, and GS GEQUAL is drawn with
`RHI_CMP_LEQUAL` (RENDER_API.md section 4).

Textures are RGBA8 UNORM (R8 UNORM for the font atlas, `font_ps`) and read
back as integers by `round(sample * 255)`; the texture function and TEXA are integer maths on
those. Exact integer paths load texels with `Load` and no sampler.

## Constant blocks

The C mirror, with static asserts on every offset, is
`port/shaders/shader_consts.h`. Every member is a 16-byte register or a
64-byte matrix, so there is no packing ambiguity. Uniform ranges are aligned
by the caller to `RhiLimits.uniformAlign`.

### FrameCB (group 0, slot 0, 320 bytes)

| offset | HLSL | meaning |
| --- | --- | --- |
| 0 | `column_major float4x4 g_view` | world to view (`matrixptr+0x80`) |
| 64 | `column_major float4x4 g_proj` | view to GS window coordinates and GS Z after the divide by w: the game's screen matrix `matrixptr+0xC0`, not a D3D clip matrix; compressed in x for widescreen |
| 128 | `column_major float4x4 g_viewProj` | `g_proj` × `g_view` (the product `matrixptr+0x100` holds) |
| 192 | `float4 g_cameraPos` | xyz the world eye, w = 1 on a camera-cut tick |
| 208 | `float4 g_clip` | near, far, zoom, aspect |
| 224 | `float4 g_target` | the bound target's size in GS pixels (xy), 1/size (zw) |
| 240 | `float4 g_origin` | xy = XYOFFSET/16, the GS window coordinate of the target's top-left pixel; zw added after (0.5 / scale: the pixel-centre convention) |
| 256 | `float4 g_space[2]` | [0] WORLD, [1] UI: `ndc = ndc * xy + zw` (the widescreen factor) |
| 288 | `float4 g_z` | x the GS Z scale of the bound depth buffer (2^-32 for PSMZ32, every depth target the game uses; 2^-24 with none bound; 2^-16 for PSMZ16); yz the target's texels per GS pixel (1 in Original); w 1 when the VU programs output unquantised X/Y (Enhanced on a scaled target) |
| 304 | `float4 g_misc` | frame counter, preset (0 Original, 1 Enhanced), reserved |

Matrices are column-major `float[16]` (element `[column * 4 + row]`), used
as `mul(M, v)` with column vectors. Replay fills them from the frame's
`RdCamera` (a frame without one keeps the previous frame's). `rd_gsbase`
checks the packing: `camera_probe_ps` evaluates the three products on the
GPU, which agree with the C products to 1e-5 relative and with
`sceVu0RotTransPers` through `matrixptr+0x100` to 1/16 pixel in GS X/Y.

### DrawCB (group 1, slot 1, 112 bytes)

| offset | field | meaning |
| --- | --- | --- |
| 0 | `uint4 g_col` | constant colour RGBA 0..255 (blit tint, fade colour); `fog_lut_ps`: x the fog sprite's GS Z, y its Z test |
| 16 | `uint4 g_mode` | x flags `DF_*`; y TEXA mode \| TEXFMT << 8; z ATST \| ate << 8 \| split << 16 (split 0 none, 1 keep passing fragments, 2 keep failing: the AFAIL FB_ONLY passes); w AREF |
| 32 | `uint4 g_blend` | x the ALPHA register (A \| B << 2 \| C << 4 \| D << 6); y FIX; z COLCLAMP |
| 48 | `float4 g_uvRect` | blit source rectangle in texels (u0, v0, u1, v1) |
| 64 | `float4 g_tex` | t1 size in texels (xy), 1/size (zw) |
| 80 | `float4 g_param` | kind-specific: `blend_int` pixel offset; `fog_lut_ps` the GS Z scale of the depth it reads; `fx_sprite_ps` the sprite's 12.4 corners; `yuv_ps` x 1 to mirror |
| 96 | `float4 g_scale` | t1 texels per GS texel (xy): 1 for images and in Original, a scaled target's scale in Enhanced; `fx_sprite_ps` and `fog_lut_ps` address t1 with it |

`crt.hlsl` binds `CrtCB` (`IcoCrtCB`, package CRT) in DrawCB's register
instead: the same 112 bytes, so it shares the draw layout's dynamic group
(`rd__CrtGroup`), and the file does not include `common.hlsli`, whose
DrawCB would collide with it. Seven float4: the virtual source's size and
its reciprocal; the box (w, h, x, y); scanline strength, beam width min
and max, horizontal blur; mask type, strength, pitch, halation; bloom,
curvature x and y, corner radius; vignette, gamma in and out, strength; x
the mirror, zw the pass's source step. t1 the source, t2 the blurred glow
(`crt_ps`), s1 bilinear clamp. The Gaussian beam and mask follow the maths
of Timothy Lottes' public-domain crt-lottes shader; no code was taken from
it or from any GPL CRT shader.

Flags: `DF_TEXTURED` 1 (TME), `DF_DECAL` 2 (else MODULATE), `DF_TCC_RGBA` 4,
`DF_FBA` 8, `DF_PABE` 16, `DF_FIX_FACTOR` 32 (blend factor from FIX, not
As), `DF_PREMUL` 64 (below), `DF_DATE` 128 and `DF_DATM` 256 (`sprite_ps`
loads the R8 snapshot at t2 in target pixels and discards where its MSB
differs from DATM; t2 is fetched only under `DF_DATE`, so a 1×1 dummy is
bound otherwise), `DF_AA1_FULL` 512 (`sprite_aa1_ps`: PRIM.ABE 0, the
coverage alpha replaces every fragment's alpha).

### Vertex (sprite.hlsl; `IcoSpriteVertex`, 20 bytes)

| loc | RHI format | content |
| --- | --- | --- |
| 0 | `RHI_VTX_U16x2_UINT` | XY, GS 12.4 fixed point, window coordinates |
| 1 | `RHI_VTX_U32x1` | Z, the GS value (`g_z.x` scales it) |
| 2 | `RHI_VTX_U8x4_UINT` | RGBA 0..255, alpha 0x80 = 1.0 |
| 3 | `RHI_VTX_F32x2` | UV in texels of t1 |

Colour is interpolated as float (0..255) and rounded to the nearest integer
in the pixel shader. UVs are normalised in the vertex
shader with `g_tex.zw`.

`sprite_aa1_ui_vs` and `sprite_aa1_world_vs` (package AA1) take
`IcoSpriteAa1Vertex`, 24 bytes: the same four and, at loc 4
(`RHI_VTX_F32x1`), the coverage, interpolated without perspective: 0..1 on
the edge geometry `rd_replay.c` adds, `ICO_AA1_INTERIOR` (2.0) on a
triangle's own vertices (RENDER_API.md "PRIM.AA1").

`sprite_stq_ui_vs` and `sprite_stq_world_vs` (package RSMALL) take
`IcoSpriteStqVertex`, 24 bytes: the same four and, at loc 4
(`RHI_VTX_F32x1`), Q. Loc 3 holds S and T in texels of t1 (times the texture
size), not divided; the vertex shader hands (s, t, q) to `sprite_stq_ps`
without perspective and the pixel shader divides.

## common.hlsli and gs_math.hlsli

| function | what |
| --- | --- |
| `gs_xy_to_ndc(xy, space)` | 12.4 XY to clip space, through `g_origin`, `g_target` and `g_space[space]` |
| `gs_z_to_depth(z, scale)`, `gs_depth(z)` | `1 − z · scale` computed as `(zmax − z + 1) · scale` (zmax the format's largest Z), 0 above zmax; exact for every Z at 2^-24 and 2^-16 and for large Z at 2^-32, so the UI's 0xFFFFFF9B and 0xFFFFFFFF stay apart under PSMZ32 (`rd__GsDepth` is the CPU copy for clears) |
| `gs_tfx_mod`, `gs_texture_function` | `min((tex · col) >> 7, 255)`; DECAL; TCC |
| `gs_texa_alpha`, `gs_texa_expand` | the three TEXA modes for PSMCT24/16 texels |
| `gs_alpha_pass`, `gs_alpha_discard` | the eight alpha-test compares; the AFAIL split passes |
| `gs_date_discard` | TEST.DATE against the snapshot texel |
| `DualOut`, `gs_dual_out` | `SV_Target0` colour/255 with the stored GS alpha (0x80 stays 0x80), `SV_Target1` the factor As/128 or FIX/128; FBA, PABE |
| `gs_blend_ch`, `gs_blend_reg_ch`, `gs_blend_int` | `((A − B) · C >> 7) + D`, arithmetic shift, clamp or wrap; the ALPHA-register form for the exact paths |
| `fullscreen_triangle` | three vertices from `SV_VertexID`, no vertex buffer |

### Why DF_PREMUL exists

A dual-source factor above 1.0 is not exact on any backend: fixed-point
blend inputs are clamped to 0..1 for UNORM attachments (the Vulkan
specification says so; measured on llvmpipe by `shaders_pixel` cell 8, a
plain `Cs·As + Cd` with As 0xFF gives 128 where the GS formula gives 191).
With `DF_PREMUL` the shader outputs `min((Cs · factor) >> 7, 255)`, the
exact GS term, and the pipeline blends with source factor ONE. `rd_core`
uses it for the additive and subtractive forms (`Cs·F + Cd`,
`Cd − Cs·F`) always. It does not cover a LERP with a factor above 1.0 (a
negative destination weight) or `Cd·FIX + Cs` with FIX above 0x80; those
clamp (RENDER_API.md section 4).

## Shaders

| name | file | stage | notes |
| --- | --- | --- | --- |
| `sprite_ui_vs`, `sprite_world_vs` | sprite.hlsl | vertex | 12.4 GS coordinates; UI and WORLD apply different `g_space` entries; any topology |
| `sprite_ps` | sprite.hlsl | fragment | untextured or textured, texture function, TEXA, alpha test, DATE (t2), dual-source output |
| `sprite_aa1_ui_vs`, `sprite_aa1_world_vs` | sprite.hlsl | vertex | as `sprite_ui_vs` / `sprite_world_vs` with the coverage (package AA1) |
| `sprite_stq_ui_vs`, `sprite_stq_world_vs` | sprite.hlsl | vertex | as `sprite_ui_vs` / `sprite_world_vs` with Q (package RSMALL); they pass (s, t, q) without perspective |
| `sprite_stq_ps` | sprite.hlsl | fragment | `sprite_ps` with the texture coordinate divided by Q per pixel (RENDER_API.md "GS to pipeline mapping", STQ); `sprite_ps` is unchanged |
| `box_reduce_ps` | blit.hlsl | fragment | the exact box average of a scaled target down to its GS size (the shadow count; RENDER_API.md section 11) |
| `sprite_aa1_ps` | sprite.hlsl | fragment | `sprite_ps` with PRIM.AA1's coverage alpha before the alpha test (RENDER_API.md "PRIM.AA1"); `sprite_ps`'s SPIR-V and DXIL are unchanged by it |
| `date_snap_ps` | sprite.hlsl | fragment | the bound target's alpha MSB (t1, `Load`) into the R8 DATE snapshot; drawn with `blit_vs` |
| `blit_vs` | blit.hlsl | vertex | fullscreen triangle, source rectangle from `g_uvRect` |
| `blit_ps` | blit.hlsl | fragment | texture function with the tint, or the tint alone without `DF_TEXTURED`; also the shadow resolve |
| `blit_fix_ps` | blit.hlsl | fragment | the same, dual-source with the FIX factor |
| `camera_probe_ps` | blit.hlsl | fragment | tests only: FrameCB's matrices applied to `g_param`, written as float bytes into a 4×3 RGBA8 target |
| `blend_int_vs`, `blend_int_ps` | blend_int.hlsl | vertex, fragment | t1 Cs, t2 Cd (RGBA8_UINT), the ALPHA register in `g_blend`, writes RGBA8_UINT; no sampler |
| `fog_lut_ps` | fog_lut.hlsl | fragment | the fog sprite (RENDER_API.md section 12) behind `sprite_ui_vs`: t1 a copy of the depth, `Load`ed at the texel the UV addresses; t2 the 256×1 LUT; reconstructs the GS Z as `(zmax + 1) − d / scale`, does the GEQUAL test in the shader, caps a passing pixel's Z at the sprite's, index = Z bits 16..23, MODULATE, alpha test, dual-source output |
| `fog_lut_vs` | fog_lut.hlsl | vertex | a fullscreen triangle with `fog_lut_ps`'s inputs; not used by rd, kept in the table |
| `font_ps` | font.hlsl | fragment | screen prims with an R8 texture (the font atlas, RENDER_API.md section 8) behind `sprite_ui_vs` or `sprite_world_vs` with `sprite_ps`'s bind groups: the R8 byte is the alpha of a white texel, then `sprite_ps`'s texture function, alpha test, DATE and dual-source output, so its bytes equal `sprite_ps` on the same texels as RGBA8; selected by `rd__PlanScreenDraw` from the texture's format |
| `fx_rect_vs` | fx_sprite.hlsl | vertex | a fullscreen triangle at the sprite's depth, no vertex input; the scissor and `fx_sprite_ps`'s coverage test bound it |
| `fx_sprite_ps` | fx_sprite.hlsl | fragment | one `staticBlur.c` sprite in the GS's integer arithmetic (RENDER_API.md section 14): coverage from the 12.4 corners, the UV stepped in 12.4 integers, nearest or the 4-bit bilinear of `Load`ed texels with CLAMP or REPEAT and TEXA before filtering, TFX MODULATE, DECAL, HIGHLIGHT and HIGHLIGHT2, the alpha test with AFAIL, DATE and `gs_blend_int` against t2 (a copy of the target taken before the sprite), PABE, FBA, COLCLAMP; writes k / 255 with no hardware blending. Flags `FXF_*` in `g_mode.x` (`RD_FXF_*` in `rd_internal.h`) |
| `wrap_acc_ps` | raw_wrap.hlsl | fragment | COLCLAMP 0: each fragment adds its GS blend term, reduced to −128..127, into an RGBA16F accumulator and writes alpha As + 1 |
| `wrap_resolve_ps` | raw_wrap.hlsl | fragment | `(Cd + acc) mod 256` per channel, Cd from a copy of the target, A from the accumulator where a fragment landed |
| `crt_vs` | crt.hlsl | vertex | package CRT: the fullscreen triangle, 0..1 across the viewport; no constant block |
| `crt_bloom_ps`, `crt_blur_ps` | crt.hlsl | fragment | the CRT filter's glow: the virtual source in linear light at half size with a 9-tap horizontal Gaussian, then the vertical one (RGBA16F) |
| `crt_ps` | crt.hlsl | fragment | the CRT filter into the output's box: curvature and rounded corners, a Gaussian beam per source line (width with brightness), the phosphor mask in output pixels, halation and bloom, vignette, the strength against the plain picture (docs/port/DISPLAY.md "CRT filter"; RENDER_API.md "The CRT pass") |
| `yuv_vs`, `yuv_ps` | yuv.hlsl | vertex, fragment | the FMV picture: 4:2:0 planes in one R8 texture to RGB with the PS2 IPU's conversion (BT.601 limited range, coefficients in 1/64 units, chroma per 2×2 block), then bilinear between converted samples to the output (FMV.md) |

Shadows add no shader. The volumes are `sprite_world_vs` on the
CPU-projected GS coordinates with `sprite_ps` under colour mask 0,
depth-tested, stencil INCR_WRAP or DECR_WRAP (write mask 0x3F), no depth
write. The resolve is `blit_vs`/`blit_ps` with the tint alone: six passes
with a stencil EQUAL test on one bit add 4 << k to RGB, a seventh writes
alpha 0x80 where the count is not 0. The RHI cannot sample stencil, so the
count is read through stencil tests, not in a shader.

`fx_sprite_ps` does in the shader what `sprite_ps` leaves to the sampler
and the blender, so every effect pass is the GS integer formula: texels are
`Load`ed from the UNORM8 target (k / 255 reads back as k) and filtered with
the GS's 4-bit weights, and the destination comes from a copy of the bound
target, so the blend is exact for every factor, including FIX and As above
0x80 and the Ad modes. The result is stored as k / 255, which UNORM8 holds
exactly; that is why the feedback passes do not need `blend_int`'s
RGBA8_UINT copies.

## Tests

| ctest | what |
| --- | --- |
| `shaders_table` | every entry present, SPIR-V magic, DXIL signed, stages and lookups (the compile itself is the build: a wrong shader stops ninja with DXC's text) |
| `gs_math` | `gs_math.hlsli` compiled as C against the formulas of `port/test/gs_blend_test.c`: the texture function over all 65,536 pairs, the blend over a dense grid for both COLCLAMP modes and the twelve ALPHA registers, the eight alpha tests, the three TEXA modes, the Z mapping |
| `shaders_pixel` | through the Vulkan RHI (exit 77 without a device): sprite cells (untextured, textured modulate, alpha test fail and pass, LERP_AS at 0x80 exact and 0x40 within 1 LSB, PREMUL additive exact, PABE), the `blit_ps` identity copy and tint, `blend_int_ps` for LERP_AS, LERP_FIX 0x70 and a wrapping add (exact); validation errors fail it |

## VU1 programs

The five VU1 microprograms are vertex shaders, with one pixel shader for all
of them. What the programs compute and where the shaders differ:
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
| `vu_probe_ps` | vu_prelit.hlsl | fragment | tests only | writes probe values to an RGBA8_UINT target; not in the reachable pipeline set |

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
| 0 | `float4 vu_mem[36]` | VU1 data memory 0..35 as the program reads it: 0..15 the common block (`RdVuCommon`; qw 2 = the UV offset xy and the cluster fade alpha w), 16..27 the object matrices (normal: +0x140, +0x200, +0x80 times the node; mesh: 16..19; particle: 16..23), 28..35 the light matrices L1, L2 (cluster and mesh load them into registers; they go here) |
| 576 | `uint4 vu_draw` | x the draw's first quadword in `vu_stream`, y quadwords per vertex, z flags, w vertices per batch (0: one batch) |
| 592 | `uint4 vu_batch` | x quadwords before each batch's vertices (GIF tag 1; mesh tag and colour 2, or 3 with a Mesh3D buffer's VIF qword), y quadwords after them (a Mesh3D buffer's MSCNT: 1) |

Flags (`vu_draw.z`, `ICO_VU_*`): bits 0..1 the clip mode, 0 REGION (the
loops' region test: a triangle with a vertex outside is not drawn), 1 NONE
(normal_c 34: no test, X/Y wrap to 16 bits), 2 SCISSOR (code 36); 16 PROBE
(tests); 32 CUT_ONLY and 64 KICK_ONLY, the two draws of a scissor batch
(SCISSOR_COMMON's fans come before the batch's own triangles on the PS2).

### Drawing

Mesh programs draw an indexed triangle list with 32-bit indices
`kick * 4 + corner` (`ICO_VU_INDEX`) and `vertexOffset` 0 (the shaders read
the vertex number from the index value). `kick` is the vertex whose XYZ2
draws triangle kick−2, kick−1, kick on the GS; the index buffer holds the
triangles the strip flags and batch starts allow (`vu1ref_StaticKicks`),
and the shader drops the ones the region test or the scissor's trivial
reject removes (all three corners go to a point outside the clip volume).
Every invocation evaluates all three vertices' positions, so the corners of
a triangle agree. Particles: `rhi_CmdDraw(6 * count)`.

Positions: a triangle the GS draws as sent goes out with w = 1, X/Y from
the 12.4 integers through `g_origin`, `g_target` and `g_space[WORLD]`
(`vu_ndc`), Z from the saturated GS integer through `gs_depth`; colour and
STQ are `noperspective` (the GS interpolates linearly in screen space and
divides S/Q, T/Q per pixel). With `g_z.w` set, X/Y are the divided position
instead of its 12.4 value (`vu_vtx_position`), except in the NONE clip mode
and for particles. A scissor triangle that crosses a clip-space Z plane goes
out homogeneous so the GPU clips it (`vu_homogeneous_position`: z/w =
`gs_depth` of the unsaturated GS Z, so the GPU clips at GS Z 0 and 2^32 for
PSMZ32).

### Tests

| ctest | what |
| --- | --- |
| `vu1` | `vu1_test.c`: hand traces of every program through the CPU reference (exact); every vertex shader's probe output against the reference (lavapipe: identical in round to nearest; within 4.3e-7 / 7.8e-7 relative in Z / STQ against a round-toward-zero reference); rendered strips against the reference's triangles drawn with `sprite_world_vs` (1 LSB); the RdVuCommon and VuCB layout. Exit 77 without a Vulkan device once the CPU part passes |

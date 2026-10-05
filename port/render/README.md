# port/render

The native draw API the game's render layer (`ico2/seki`) calls, and its
implementation.

| file | status | what it is |
|---|---|---|
| `rd.h` | spec (wave 0), additions in wave 1 | the draw API: 13 ordered lists, state deltas, targets, meshes, immediate prims, post passes |
| `rd_state.h` | spec (wave 0) | the finite GS state the game uses, enumerated from call sites, and the pipeline key |
| `rd_internal.h` | wave 1 | command records, frame, state block, registries, the hooks tests and tools use |
| `rd_core.c` | wave 1 | recording: lists, payload arena, state deltas, list defaults, named and temporary targets, texture registry, frame retention, the state walk |
| `rd_post.c` | waves 1, 2 (R2c), 4 (R4c) | `rd_Post`: reduction, keep, fade, letterbox, brightness, anti-alias (downsample, composite), film noise as the GS writes and sprites of `GsBase.c`, in order, so they leak the same state; composite (hardware or exact), copy; the fog sprite (`RD_POST_FOG`, wave 4: the sprite and the CLUT, with ZFog.c's register writes recorded around it by ZFog.c); shadow resolve and blur recorded as stubs |
| `rd_pipeline.c` | wave 1 | pipeline key derivation from the state block (blend paths, AFAIL split, Z), the cache, the reachable-pipeline enumeration |
| `rd_replay.c` | wave 1, 3, 4 | an `RdFrame` onto the RHI: passes, state tracking, GS sampling rules, the exact `blend_int` path; the VU draws (wave 3); the shadow count's reset, volumes and resolve (`doShadow*`, R4b); the depth fog (`doFog`, R4c: a sampleable copy of the depth, the LUT, `fog_lut_ps`) |
| `rd_shadow.c` | wave 4 (R4b) | the shadow count's recording: `rd_ShadowCountTarget`, `rd_ShadowReset`, `rd_ShadowTris`, `rd_ShadowResolve`, and `rd_ShadowStrip` (replayed since R4b, no longer a stub) (`docs/port/RENDER_API.md` section 14) |
| (fog) | wave 4 (R4c) | no file of its own: `RD_POST_FOG` in `rd_post.c`, `doFog` in `rd_replay.c`, `rd__FogPlan` in `rd_pipeline.c`, `port/shaders/fog_lut.hlsl`; the game side is `ico2/seki/src/ZFog.c`'s host path (`docs/port/RENDER_API.md` section 15) |
| `rd_present.c` | wave 1, hooks in wave 2 (R2c) | DISPLAY to the output, Original preset (4:3 box, line doubling, bilinear horizontal); the Enhanced fields (interpolation, aspect, mirror, full height) present and unused |
| `rd_frame.c` | wave 2 (R2c) | the flip's draw environment and clear (`rd_FrameHead`, `rd_FrameFlip`), per-target Z scale, `gsb_MakeCommonMatrix`'s VU block, `RdCamera` into FrameCB (`docs/port/RENDER_API.md` section 12) |
| `rd_mesh.h`, `rd_mesh.c` | wave 3 (R3c interface, R3ab implementation) | the mesh path: VU meshes built from Packet.c's packets (stream without GIF tags, the strip-rule index list), the per-list VU state at record time (common block, SET_* uploads, UV offset, resident program, BEGIN code), the (program, code) table, recording of `RDC_MESH`/`RDC_SKINNED`/`RDC_GRID`/`RDC_PARTICLES`, the VU pipeline families (`docs/port/RENDER_API.md` section 13) |
| `vu1_ref/` | wave 3 (R3c) | CPU references of the five VU1 programs (test oracle; `rd_mesh.c` uses its SET_* routines as the per-list VU state) |
| `rd_dump.c` | waves 1, 3 | frame dump and load (local only: dumps hold assets, never commit one); version 3 carries the VU meshes a frame draws |
| `rd_png.c` | wave 1 | a minimal stored-deflate PNG writer (own code, no dependency) |
| `tools/rd_replay_tool.c` | wave 1 | a dump to a PNG, headless |
| `rd_tex.c`, `rd_tex.h` | wave 2 (R2b) | texture cache keyed on (texture id, content generation, TEXA mode): TIM2 decode, CLUT order, TEXA at replay, retire-after-two-frames; `Texture.c`'s host path uses it (`docs/port/RENDER_API.md` section 11) |
| (`rd_gs_shim.c`) | wave 2: folded into `ico2/seki/src/GifPacket.c`'s host path (`GifHost.h`); deleted in wave 6 | decodes the 2D layer's GS register writes into `rd_*` calls (`docs/port/RENDER_API.md` section 9) |
| `rd_interp.c` | wave 7 | interpolation between retained frames |

The backend interface is `port/rhi/rhi.h`; backends live in `port/rhi/vk`
and `port/rhi/d3d12`. Design, state inventory and open items:
`docs/port/RENDER_API.md`.

## What replays and what does not (wave 1)

Replayed: clears, screen prims (`rd_ScreenPrims`: sprites, triangles,
strips, fans, lines, line strips, points), the post kinds listed above,
texture copies, since wave 3 (R3ab) the VU program draws of `rd_mesh.h`
(`rd_DrawVuMesh`, `rd_DrawVuGrid`, `rd_DrawVuParticles`: static, skinned,
specular, reflection and dissolve passes, grids, particles), since wave 4
the shadow count (`rd_ShadowReset`, `rd_ShadowTris`, `rd_ShadowStrip`,
`rd_ShadowResolve`, R4b) and the depth fog (`RD_POST_FOG`, R4c). Recorded
with their payload and key but stopped at replay by `rd__NotImplemented`
(prints, then asserts): `rd_WorldPrims` (wave 5) and the post kinds shadow
resolve, blur, present blit (`RD_POST_SHADOW_RESOLVE` is unused since R4b:
Shadow.c calls `rd_ShadowResolve`). A draw that samples a depth view
outside the fog (the fog's TEX0 leaking) logs once and draws untextured. The wave-0
semantic mesh calls (`rd_DrawMesh`, `rd_DrawSkinned`, `rd_DrawGrid`,
`rd_DrawParticles`) record nothing since wave 3 (kept for an Enhanced
path). An ico-pc.ini `dump_every=N` makes `rd_EndFrame` dump every Nth
frame (`dump_dir=`, default `dumps`). DATE is applied since wave 2 (an R8
snapshot of the target's alpha MSB read by `sprite_ps` at t2); anti-alias
and film noise replay since wave 2 (R2c). FrameCB carries the frame's
camera and the bound depth target's Z scale since R2c.

## Tests

| ctest | what |
|---|---|
| `rd_state` | CPU only: replay order, the list defaults, leakage (list 3 inherits list 2, list 4's defaults reset only TEST/ZBUF/FBA/TEXA, list 5 inherits list 4, state crosses frames), keep frames, retention, stub recording (a VU mesh draw since wave 3), AFAIL/blend/FIX plans, dump round trip with id remapping, the reachable pipeline count (screen and post under 100, with the VU families under 256) |
| `rd_pixel` | Vulkan (exit 77 without a device, also without a Vulkan loader; lavapipe in the container, validation and synchronisation validation on): list order on the GPU, DATE (both DATM) and flat shading (wave 2), GS sprite coverage at integer, half-pixel and -4 edges, textured 1:1 with the +8 UV nudge, TEXA on an RGB24 source, `rd_UVOffset`, the reduction against a CPU reference (1 LSB), a keep frame, 100 frames of the exact feedback blend (bit-exact), dump -> load -> replay (bit-exact), the presenter, every created pipeline inside the enumerated set |
| `rd_replay_tool` | the dump `rd_pixel` leaves, through the tool and the presenter, to a PNG (77 when `rd_pixel` skipped) |
| `rd_layout` | wave 2: `GifPacket.c`, `DisplayList.c`, `DmaPacket.c` built as the window build has them (`ICO_RD`), fed `layout_texture.c`'s call sequence for a synthetic layout item (after `Texture.c`'s raw TEX0 packet); checks the recorded state, sprites, UI tags, texture seam and that no register went undecoded, then one sprite's pixels on a device (77 without one) |
| `rd_tex` | wave 2 (R2b): `Texture.c` with the three files above (`ICO_RD`), fed synthetic TIM2 files: every texel of PSMT4/PSMT8 (32- and 16-bit CLUTs, CSM1 and index order), PSMCT16/24/32, ICO block, padding, and the decoder's PSMT8H/4HL/4HH against independent references; TEXA on the CPU; sampler from the ICO block; a CLUT scroll re-expanding one texture in place; cache keys and retirement; the decoder binding the cached texture with the record's TEX1/TEST; then a PSMT8 and a PSMCT16 (TEXA 7F/81+AEM) sprite through `tex_TransTexture` give exact texels on a device (77 without one) |

| `rd_mesh` | wave 3 (R3ab): `Packet.c`, `RegistPacket.c`, `MicroCode.c`, `DisplayP2O.c`, `Primitive.c` with the 2D layer and `Matrix.c` (`ICO_RD`), the rest stubbed, fed synthetic p2o-decoded models (prelit, two VU batches; the scissor clip type; a two-bone cluster), a Mesh3D grid and a particle batch: the mesh against the packet (stream, `vu1ref_StaticKicks` indices), the recorded program/code/clip, VuCB against the uploads the EE code makes (common block, UV offset, +0x140/+0x200/+0x80 x node, bones, lights), the decoded material state; on a device each case against `vu1_ref`'s triangles drawn as screen prims in the same state (measured: 0 difference), every created pipeline enumerated (77 without a device) |
| `rd_gsbase` | wave 2 (R2c): `GsBase.c` with the three files above (`ICO_RD`), the rest of the game stubbed, driven through `gsb_InitGSSystem`, `gsb_SyncGSSystem` and `gsb_UpdateGSSystem`: the head clear takes the flip's BG colour, keep frames keep the head in list 11, the half offset follows `GS_CSR.FIELD` two flips late, post-pass state leaks, the VU block and camera, the Z formula; on a device: the camera packing through `camera_probe_ps` against the C products and `sceVu0RotTransPers` (1/16 px), PSMZ32 depth order of UI Z above 2^24, the half-offset rows, the clear, and keep, fade, brightness, anti-alias (each level, both), film noise and letterbox against CPU references within 1 LSB (2 for both anti-alias levels) |

| `rd_shadow` | wave 4 (R4b): `Shadow.c` with the 2D layer and `Matrix.c` (`ICO_RD`): the recording of shadow_Reset/RenderVolume/Draw; on a device the stencil count against the wrapped colour sum (exact), the blur chain and composites against CPU references, the DATE receiver mask, Shadow.c's own volume (77 without a device) |
| `rd_fog` | wave 4 (R4c): `ZFog.c` with the 2D layer (`ICO_RD`): a CPU model of the GS swizzle (PSMCT32, PSMZ32, PSMT4 page/block/column tables) running fog_DrawFog's transfers, the PSMT8H index = Z bits 16..23 at every pixel; the CLUT order (CSM1); the recording in list 4; on a device a grid of quads at known Z fogged against a CPU reference (1 LSB; 2 with fogOffsetA), dump -> load -> replay (exact), every created pipeline enumerated (77 without a device) |

`port/test/gs_blend_test.c` is the single-file CPU program behind
`docs/port/RENDER_API.md` section 7:

    cc -O2 -o gs_blend_test port/test/gs_blend_test.c -lm && ./gs_blend_test

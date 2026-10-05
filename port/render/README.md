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
| `rd_blur.c` | wave 5 (R5a) | staticBlur.c's sprites: `RD_POST_MOTION_BLUR`, `RD_POST_DOF`, `RD_POST_FLARE`, `RD_POST_BLOOM`, `RD_POST_AURA`, `RD_POST_EYE_BLUR` recorded as `RdPostRec`s, replayed by `rd_replay.c`'s `doBlurSprite` through `fx_sprite.hlsl` in the GS integer arithmetic (coverage, 12.4 UV step, 4-bit bilinear with TEXA before filtering, TFX incl. HIGHLIGHT, alpha test, DATE, integer blend against a copy of the target); the feedback FIX hook (`rd__BlurFeedbackFix`) and the work-buffer scale rule (`rd_WorkTargetScale`); the game side is `ico2/sugipon/src/staticBlur.c`'s host path (`docs/port/RENDER_API.md` section 17) |
| `rd_water.c` | wave 5 (R5b) | the render-to-texture surfaces (puddle.c, pool.c, queen_barrier_disp.c): `rd_BlockTarget` (a per-frame target per VRAM block and size), `rd_GsNamedBlock` (the decoder's FRAME table), `rd_AliasTarget` (record a block target where the decoder names AA0, with its own depth; hooks in `rd_core.c`), `rd_PushCamera`/`rd_PopCamera` (the reflection camera of a mid-frame `gsb_SetVSMatrix`, `rd__CameraAt`), the pipelines of these states (`rd__EnumerateReachableWater`) (`docs/port/RENDER_API.md` section 16) |
| (raw builders) | wave 5 (R5c) | no file of its own: darkVolume.c, particleEffect.c and lightning.c chain their packets through `MicroCode.c`'s `mc_HostDma` (VIF DIRECT and a GIF PACKED/REGLIST reader since R5c, FRAME PSMCT24 as FBMSK) into the GS register decoder; COLCLAMP 0 additive/subtractive screen prims wrap modulo 256 through `rd_replay.c`'s `doScreenWrap` (`port/shaders/raw_wrap.hlsl`: an RGBA16F accumulator, then (Cd + acc) mod 256) (`docs/port/RENDER_API.md` section 18) |
| `rd_present.c` | wave 1, hooks in wave 2 (R2c), options in wave 7 (R7a) | DISPLAY to the output: Original (4:3 box, line doubling, bilinear horizontal), Enhanced (the aspect's box, full height); `rd__ApplyDisplay` turns the settings into the target scales, the wide factor and the filter (`docs/port/RENDER_API.md` section 19); mirror present and unused |
| `rd_frame.c` | wave 2 (R2c) | the flip's draw environment and clear (`rd_FrameHead`, `rd_FrameFlip`), per-target Z scale, `gsb_MakeCommonMatrix`'s VU block, `RdCamera` into FrameCB (`docs/port/RENDER_API.md` section 12) |
| `rd_mesh.h`, `rd_mesh.c` | wave 3 (R3c interface, R3ab implementation) | the mesh path: VU meshes built from Packet.c's packets (stream without GIF tags, the strip-rule index list), the per-list VU state at record time (common block, SET_* uploads, UV offset, resident program, BEGIN code), the (program, code) table, recording of `RDC_MESH`/`RDC_SKINNED`/`RDC_GRID`/`RDC_PARTICLES`, the VU pipeline families (`docs/port/RENDER_API.md` section 13) |
| `vu1_ref/` | wave 3 (R3c) | CPU references of the five VU1 programs (test oracle; `rd_mesh.c` uses its SET_* routines as the per-list VU state) |
| `rd_dump.c` | waves 1, 3 | frame dump and load (local only: dumps hold assets, never commit one); version 3 carries the VU meshes a frame draws |
| `rd_png.c` | wave 1 | a minimal stored-deflate PNG writer (own code, no dependency) |
| `tools/rd_replay_tool.c` | wave 1 | a dump to a PNG, headless |
| `rd_tex.c`, `rd_tex.h` | wave 2 (R2b) | texture cache keyed on (texture id, content generation, TEXA mode): TIM2 decode, CLUT order, TEXA at replay, retire-after-two-frames; `Texture.c`'s host path uses it (`docs/port/RENDER_API.md` section 11) |
| (`rd_gs_shim.c`) | wave 2: folded into `ico2/seki/src/GifPacket.c`'s host path (`GifHost.h`); deleted in wave 6 | decodes the 2D layer's GS register writes into `rd_*` calls (`docs/port/RENDER_API.md` section 9) |
| `rd_perf.c` | package P1 | the per-replay performance records (`rd.h` `RdPerfRecord`, `rd_PerfPop`): CPU phases, the RHI's counts, the GPU timestamps collected two replays later; the window's 10 s line and `[dev] perf_log` CSV read them (`docs/port/RENDER_API.md` section 22) |
| `rd_interp.c` | wave 7 (R7b) | presentation between ticks: the current frame replayed with its keyed draws' data (VU blocks, bones, grid vertices, particles, screen prims, shadow volumes, the camera) blended from the previous frame's by alpha, the snap rules (missing key, shape change, jump, cut, fade edge, keep, gap, camera jump, recreated targets), the feedback passes per present (motion blur FIX by dt, FEED128 once per tick), `rd_Present` (`docs/port/RENDER_API.md` section 20) |

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
`rd_ShadowResolve`, R4b), the depth fog (`RD_POST_FOG`, R4c) and since
wave 5 staticBlur.c's effects (`RD_POST_MOTION_BLUR` .. `RD_POST_EYE_BLUR`,
R5a). Recorded
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
camera and the bound depth target's Z scale since R2c. Since wave 5 (R5c) a screen-prim command under
COLCLAMP 0 with an additive or subtractive equation wraps modulo 256 as on
the GS (`doScreenWrap`) instead of clamping.

## Tests

| ctest | what |
|---|---|
| `rd_state` | CPU only: replay order, the list defaults, leakage (list 3 inherits list 2, list 4's defaults reset only TEST/ZBUF/FBA/TEXA, list 5 inherits list 4, state crosses frames), keep frames, retention, stub recording (a VU mesh draw since wave 3), AFAIL/blend/FIX plans, dump round trip with id remapping, the reachable pipeline count (screen and post under 100, with the VU families under 256) |
| `rd_pixel` | Vulkan (exit 77 without a device, also without a Vulkan loader; lavapipe in the container, validation and synchronisation validation on): list order on the GPU, DATE (both DATM) and flat shading (wave 2), GS sprite coverage at integer, half-pixel and -4 edges, textured 1:1 with the +8 UV nudge, TEXA on an RGB24 source, `rd_UVOffset`, the reduction against a CPU reference (1 LSB), a keep frame, 100 frames of the exact feedback blend (bit-exact), dump -> load -> replay (bit-exact), the presenter, every created pipeline inside the enumerated set |
| `rd_replay_tool` | the dump `rd_pixel` leaves, through the tool and the presenter, to a PNG (77 when `rd_pixel` skipped) |
| `rd_layout` | wave 2: `GifPacket.c`, `DisplayList.c`, `DmaPacket.c` built as the window build has them (`ICO_RD`), fed `layout_texture.c`'s call sequence for a synthetic layout item (after `Texture.c`'s raw TEX0 packet); checks the recorded state, sprites, UI tags, texture seam and that no register went undecoded, then one sprite's pixels on a device (77 without one) |
| `rd_mip` | F2 (C1): `rdtex_MipChainBytes` against the level sum, and `rdtex_BuildMipChain` writing exactly that many bytes (guard bytes after the buffer) for 128x4 (764), 512x2 and 2x512 (2044), 16x16, 1x8 and 1x1; CPU only |
| `rd_tex` | wave 2 (R2b): `Texture.c` with the three files above (`ICO_RD`), fed synthetic TIM2 files: every texel of PSMT4/PSMT8 (32- and 16-bit CLUTs, CSM1 and index order), PSMCT16/24/32, ICO block, padding, and the decoder's PSMT8H/4HL/4HH against independent references; TEXA on the CPU; sampler from the ICO block; a CLUT scroll re-expanding one texture in place; cache keys and retirement; the decoder binding the cached texture with the record's TEX1/TEST; then a PSMT8 and a PSMCT16 (TEXA 7F/81+AEM) sprite through `tex_TransTexture` give exact texels on a device (77 without one) |

| `rd_mesh` | wave 3 (R3ab): `Packet.c`, `RegistPacket.c`, `MicroCode.c`, `DisplayP2O.c`, `Primitive.c` with the 2D layer and `Matrix.c` (`ICO_RD`), the rest stubbed, fed synthetic p2o-decoded models (prelit, two VU batches; the scissor clip type; a two-bone cluster), a Mesh3D grid and a particle batch: the mesh against the packet (stream, `vu1ref_StaticKicks` indices), the recorded program/code/clip, VuCB against the uploads the EE code makes (common block, UV offset, +0x140/+0x200/+0x80 x node, bones, lights), the decoded material state; on a device each case against `vu1_ref`'s triangles drawn as screen prims in the same state (measured: 0 difference), every created pipeline enumerated (77 without a device) |
| `rd_gsbase` | wave 2 (R2c): `GsBase.c` with the three files above (`ICO_RD`), the rest of the game stubbed, driven through `gsb_InitGSSystem`, `gsb_SyncGSSystem` and `gsb_UpdateGSSystem`: the head clear takes the flip's BG colour, keep frames keep the head in list 11, the half offset follows `GS_CSR.FIELD` two flips late, post-pass state leaks, the VU block and camera, the Z formula; on a device: the camera packing through `camera_probe_ps` against the C products and `sceVu0RotTransPers` (1/16 px), PSMZ32 depth order of UI Z above 2^24, the half-offset rows, the clear, and keep, fade, brightness, anti-alias (each level, both), film noise and letterbox against CPU references within 1 LSB (2 for both anti-alias levels) |

| `rd_shadow` | wave 4 (R4b): `Shadow.c` with the 2D layer and `Matrix.c` (`ICO_RD`): the recording of shadow_Reset/RenderVolume/Draw; on a device the stencil count against the wrapped colour sum (exact), the blur chain and composites against CPU references, the DATE receiver mask, Shadow.c's own volume (77 without a device) |
| `rd_blur` | wave 5 (R5a): `staticBlur.c` with the 2D layer and `Matrix.c` (`ICO_RD`): the recording of every entry point (kinds, buffers, HIGHLIGHT, the DoF planes, the motion blur's DISPLAY RGB24 read and H/2 to H stretch for 448 and 512 lines, read-after-write of every work buffer, the first After before any Before); on a device every target the effects touch against a CPU model of the same GS arithmetic for post modes 1..7 with the sun, 600 frames of motion blur feedback and 600 of aura feedback (modes 1..3): 0 LSB; dump -> load -> replay (77 without a device) |
| `rd_fog` | wave 4 (R4c): `ZFog.c` with the 2D layer (`ICO_RD`): a CPU model of the GS swizzle (PSMCT32, PSMZ32, PSMT4 page/block/column tables) running fog_DrawFog's transfers, the PSMT8H index = Z bits 16..23 at every pixel; the CLUT order (CSM1); the recording in list 4; on a device a grid of quads at known Z fogged against a CPU reference (1 LSB; 2 with fogOffsetA), dump -> load -> replay (exact), every created pipeline enumerated (77 without a device) |
| `rd_water` | wave 5 (R5b): `puddle.c`, `pool.c`, `queen_barrier_disp.c`, `waterDot.c`, `clothAnimation.c` with the mesh path, the 2D layer and `matrixDrive.c` (`ICO_RD`): the decoder alone maps the 0x2800 block to AA0 without depth (the finding); with the host paths the block targets, aliases, depth, camera scopes, VuCB reflection matrix (vs double precision), DATE/blend state, refraction STs (vs double precision); on a device the puddle block vs a CPU raster of the VU reference and vs the double-precision projection (0 LSB, vertices within 1/16 px), SCENE after leveldown/copy (0 LSB), the pool and barrier scene copies (exact), the refracting/reflecting grids and the barrier vs a CPU raster with STQ, bilinear, MODULATE, top-left fill and Z (0 outside the GS's 1/16-texel precision range; 1 to 2 LSB from the centre sample), water dots (exact), cloth vs the VU reference (0 LSB) (77 without a device) |
| `rd_raw` | wave 5 (R5c): `darkVolume.c`, `particleEffect.c`, `lineManager.c`, `lightning.c` with `MicroCode.c`, `Primitive.c`, `matrixDrive.c`, the 2D layer and `Matrix.c` (`ICO_RD`): the packets hand-decoded against the recording (particle SET_GSREGISTER, lightning's DIRECT REGLIST strips vertex for vertex, dark packet 1's A+D pairs), lightning's twelve ALPHA registers and the out-of-range mode, the dark volume's block target, SCENE depth, COLCLAMP 0 and PSMCT24 composite, sonic's RGB_ONLY pass; on a device the dark count vs a CPU raster with the modular sum (0 LSB), the composite (1 LSB; the GS's TEXA-before-filter order measured), sonic's alpha and RGB_ONLY zoom (0 LSB), two lightning blend modes and three particle batches (vu1_ref) vs CPU rasters with the GS blend (1 LSB a layer), flat and Gouraud lines (77 without a device) |
| `rd_present` | wave 7 (R7a): the display options with `GsBase.c` (`ICO_RD`) and `port/game/video_options.c`: the `[video]` keys from a config.toml, defaults, parsers, `auto`, save; aspect 16:9 leaves +0x80/+0xC0/+0x100 byte-identical and narrows +0x240/+0x280's x; `g_proj`'s x row; the target scales and presentation boxes; alpha-coverage mips; on a device rd_pixel's frame in Original against the pre-R7a hashes (llvmpipe) and Enhanced-neutral equal to it, 2x (SCENE blocks uniform and equal to 1x, DISPLAY block averages within 2 LSB), 16:9 (the UI in the centred 4:3 box, bars and full-width fills over the width, the 16:9 present), trilinear mips (77 without a device) |
| `rd_interp` | wave 7 (R7b): two synthetic frames with keyed draws through `rd__InterpFrame`: alpha 1 the current payload byte for byte, alpha 0 the previous draws, a translated UI sprite half way within 1/16 px, the call ordinal, snaps (missing key, count change, jump, cut, fade edge, gap, camera turn, keep), VU block matrices, the UV scroll across its wrap, a teleport, grid vertices, particles, shadow volumes, the fade level, the feedback FIX at dt 0.5 (a^0.5) and FEED128 on later presents; on a device the replays at alpha 0 and 1 equal the frames' own (0 pixels), the half-way sprite's columns, `rd_Present` per preset, history after a scale change |
| `rd_perf` | package P1: a synthetic game-shaped frame (meshes with a morph, UI sprites, world strips, a DATE draw, the shadow count, a block target, the reduction) replayed 200 times and recorded 200 times: no buffer, texture or memory created or destroyed and no texture or static mesh uploaded again in steady state, DISPLAY identical to the first replay's, under 20 ms of CPU a replay without the GPU wait; `--dump FILE` times a dump phase by phase (77 without a device) |

`port/test/gs_blend_test.c` is the single-file CPU program behind
`docs/port/RENDER_API.md` section 7:

    cc -O2 -o gs_blend_test port/test/gs_blend_test.c -lm && ./gs_blend_test

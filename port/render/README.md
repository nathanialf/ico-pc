# port/render

The native draw API the game's render layer (`ico2/seki`) calls, and its
implementation.

| file | status | what it is |
|---|---|---|
| `rd.h` | spec (wave 0), additions in wave 1 | the draw API: 13 ordered lists, state deltas, targets, meshes, immediate prims, post passes |
| `rd_state.h` | spec (wave 0) | the finite GS state the game uses, enumerated from call sites, and the pipeline key |
| `rd_internal.h` | wave 1 | command records, frame, state block, registries, the hooks tests and tools use |
| `rd_core.c` | wave 1 | recording: lists, payload arena, state deltas, list defaults, named and temporary targets, texture registry, frame retention, the state walk |
| `rd_post.c` | wave 1 | `rd_Post`: reduction, keep, fade, letterbox, brightness as the GS writes and sprites of `GsBase.c`; composite (hardware or exact), copy; the other kinds recorded as stubs |
| `rd_pipeline.c` | wave 1 | pipeline key derivation from the state block (blend paths, AFAIL split, Z), the cache, the reachable-pipeline enumeration |
| `rd_replay.c` | wave 1 | an `RdFrame` onto the RHI: passes, state tracking, GS sampling rules, the exact `blend_int` path |
| `rd_present.c` | wave 1 skeleton | DISPLAY to the output, Original preset (4:3 box, line doubling, bilinear horizontal); preset hooks |
| `rd_dump.c` | wave 1 | frame dump and load (local only: dumps hold assets, never commit one) |
| `rd_png.c` | wave 1 | a minimal stored-deflate PNG writer (own code, no dependency) |
| `tools/rd_replay_tool.c` | wave 1 | a dump to a PNG, headless |
| `rd_tex.c` | wave 2 | texture cache keyed on (texture id, generation, TEXA mode) |
| (`rd_gs_shim.c`) | wave 2: folded into `ico2/seki/src/GifPacket.c`'s host path (`GifHost.h`); deleted in wave 6 | decodes the 2D layer's GS register writes into `rd_*` calls (`docs/port/RENDER_API.md` section 9) |
| `rd_interp.c` | wave 7 | interpolation between retained frames |

The backend interface is `port/rhi/rhi.h`; backends live in `port/rhi/vk`
and `port/rhi/d3d12`. Design, state inventory and open items:
`docs/port/RENDER_API.md`.

## What replays and what does not (wave 1)

Replayed: clears, screen prims (`rd_ScreenPrims`: sprites, triangles,
strips, fans, lines, line strips, points), the post kinds listed above,
texture copies. Recorded with their payload and key but stopped at replay
by `rd__NotImplemented` (prints, then asserts): `rd_DrawMesh`,
`rd_DrawSkinned` (wave 3), `rd_DrawGrid`, `rd_DrawParticles`,
`rd_ShadowStrip` (wave 4), `rd_WorldPrims` (wave 5), and the post kinds
anti-alias, fog, shadow resolve, blur, film noise, present blit. DATE is
applied since wave 2 (an R8 snapshot of the target's alpha MSB read by
`sprite_ps` at t2).

## Tests

| ctest | what |
|---|---|
| `rd_state` | CPU only: replay order, the list defaults, leakage (list 3 inherits list 2, list 4's defaults reset only TEST/ZBUF/FBA/TEXA, list 5 inherits list 4, state crosses frames), keep frames, retention, stub recording, AFAIL/blend/FIX plans, dump round trip with id remapping, the reachable pipeline count |
| `rd_pixel` | Vulkan (exit 77 without a device, also without a Vulkan loader; lavapipe in the container, validation and synchronisation validation on): list order on the GPU, DATE (both DATM) and flat shading (wave 2), GS sprite coverage at integer, half-pixel and -4 edges, textured 1:1 with the +8 UV nudge, TEXA on an RGB24 source, `rd_UVOffset`, the reduction against a CPU reference (1 LSB), a keep frame, 100 frames of the exact feedback blend (bit-exact), dump -> load -> replay (bit-exact), the presenter, every created pipeline inside the enumerated set |
| `rd_replay_tool` | the dump `rd_pixel` leaves, through the tool and the presenter, to a PNG (77 when `rd_pixel` skipped) |
| `rd_layout` | wave 2: `GifPacket.c`, `DisplayList.c`, `DmaPacket.c` built as the window build has them (`ICO_RD`), fed `layout_texture.c`'s call sequence for a synthetic layout item (after `Texture.c`'s raw TEX0 packet); checks the recorded state, sprites, UI tags, texture seam and that no register went undecoded, then one sprite's pixels on a device (77 without one) |

`port/test/gs_blend_test.c` is the single-file CPU program behind
`docs/port/RENDER_API.md` section 7:

    cc -O2 -o gs_blend_test port/test/gs_blend_test.c -lm && ./gs_blend_test

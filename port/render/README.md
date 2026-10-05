# port/render

The native draw API the game's render layer (`ico2/seki`) calls, and its
implementation.

| file | status | what it is |
|---|---|---|
| `rd.h` | spec (wave 0) | the draw API: 13 ordered lists, state deltas, targets, meshes, immediate prims, post passes |
| `rd_state.h` | spec (wave 0) | the finite GS state the game uses, enumerated from call sites, and the pipeline key |
| `rd_core.c` | wave 1 | list recording and replay, state block, pipeline cache |
| `rd_replay.c` | wave 1 | frame dump and headless replay for `tools/verify` |
| `rd_tex.c` | wave 2 | texture cache keyed on (texture id, generation, TEXA mode) |
| `rd_present.c` | wave 2 | reduction, present blit, presets, widescreen, mirror |
| `rd_gs_shim.c` | wave 2, deleted in wave 6 | decodes `gif_SetGsReg` state and vertex writes into `rd_*` calls for files not yet hand-converted |
| `rd_interp.c` | wave 7 | interpolation between retained frames |

The backend interface is `port/rhi/rhi.h`; backends live in `port/rhi/vk`
and `port/rhi/d3d12`. Design, state inventory and open items:
`docs/port/RENDER_API.md`.

## Tests

`port/test/gs_blend_test.c` is a single-file CPU program with no
dependencies:

    cc -O2 -o gs_blend_test port/test/gs_blend_test.c -lm && ./gs_blend_test

It measures how far a float blender drifts from the GS integer blender in
the game's feedback passes. Its numbers and the decision they led to are in
`docs/port/RENDER_API.md` section 7. The CMake build (owned by the build
package) should add it as a test target; it is not wired in yet.

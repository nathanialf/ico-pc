# Testing

What the port's tests are and how they are wired: the test framework and its labels, the render
gates over saved frames, the render suite on each backend, the tests that need the player's disc,
the environment switches that force GPU paths, the frame dumps and the
[replay tool](ARCHITECTURE.md#terms), [pad scripts](ARCHITECTURE.md#terms) and
[traces](ARCHITECTURE.md#terms), the developer build helper, the documentation gate, CI, and which
tests a change needs. Every test program, by area, is
[TESTING_PROGRAMS.md](TESTING_PROGRAMS.md)'s; how to run `ctest` is
[../BUILDING.md](../BUILDING.md#tests)'s.

## Files

| path | what it holds |
| --- | --- |
| `cmake/IcoTests.cmake` | the `add_test` wrapper (labels, the `tests` target), `ico_add_sharded_test` |
| `cmake/IcoShardCheck.cmake` | the `<name>.cases` check of a sharded test |
| `port/test/ico_check.h` | `CHECK`, `CHECK_STR`, `ico_check_summary` |
| `port/test/ico_shard.h` | the shard split of a slow test program |
| `port/*/test/`, `port/platform/fpenv_test.c` | the test programs and their helpers ([TESTING_PROGRAMS.md](TESTING_PROGRAMS.md)) |
| `port/render/test/*.cmake` | script tests over the replay tool and the fog self-test |
| `tools/native_frame_check.py`, `tools/native_frame_dumps.txt`, `docs/native_frame_baseline.json` | the native-frame gates, their dump list and their baseline |
| `tools/coverage_check.py`, `tools/coverage_corpus.txt` | the regression renders and their corpus |
| `port/render/tools/rd_replay_tool.c` | the replay tool |
| `tools/vu_variant_diff.py`, `tools/vu_variant_equiv.py`, `tools/test/vu1/` | the June 2001 demo's VU1 programs against the retail ones: the diff, its per-hunk table, the symbolic comparison |
| `port/platform/hotkeys.h`, `port/platform/dump_zip.h`, `port/render/rd_dump.c` | F12, the aspect sweep, the dump format |
| `port/input/pad_script.h`, `port/input/input_record.h`, `port/platform/trace_host.h` | pad scripts, recordings, the trace |
| `tools/dev_build.sh` | build a few targets of one checkout and run a few tests |
| `tools/check_docs.py`, `tools/check_docs_allow.txt` | the documentation gate and its allow-list |
| `.github/workflows/ci.yml`, `ios.yml` | CI |

## The framework

`cmake/IcoTests.cmake` is included before the first `add_test` and wraps it: each test gets a
`LABELS` entry for the directory it is registered in, and the executable targets its command names
(the program itself, or `$<TARGET_FILE:x>` in an argument) are recorded (`cmake/IcoTests.cmake`,
`add_test`, `_ico_test_area`). The labels are `render` (`port/render`), `ui`, `audio`, `save` (their
directories), `game` (`port/game`, `port/math`, `port/null`, `port/test`), `data` (`port/data`,
`port/fmv`), `rhi` (`port/rhi`, `port/shaders`) and `platform` (`port/platform`, `port/input`,
`port/config` and the top level), plus `tools` on any test whose command runs a script under
`tools/`.

A test registered from a deferred call takes the area of its program's first source.
`ico_finalize_tests`, run deferred at the end of the top directory, marks every executable that is
a test program (a name ending in `_test`, or one a test command names) `EXCLUDE_FROM_ALL` and makes
the `tests` target depend on all of them; `rd_replay_tool`, `mc_import`, `state_recover_tool` and `ico_pc` stay in `all`
(`ICO_KEEP_IN_ALL`). A new test therefore needs only its `add_test` line. Tests run only where the
build can run them: a native build, a cross build with an emulator, or a cross build for the host's
own system (`CMakeLists.txt`, `ICO_RUN_TESTS`).

A test that needs something the machine may lack exits 77 and is registered with
`SKIP_RETURN_CODE 77`, which ctest reports as skipped; a script test prints `SKIP:` (`port/render/test/rd_replay_tool_list.cmake`) and is registered
with `SKIP_REGULAR_EXPRESSION "SKIP:"` (`port/render/CMakeLists.txt`, `rd_replay_tool_list`). A C test
uses `port/test/ico_check.h`: `CHECK(cond)` or `CHECK(cond, fmt, ...)` prints
`FAIL file:line: ...` and counts, `CHECK_STR` compares two strings, and `ico_check_summary(name)`
prints `name: ok` or the failure count and returns the exit code. The Settings tests share
`port/ui/test/settings_fixture.h`, which has its own `CHECK` over the game's headers
([CONVENTIONS.md](CONVENTIONS.md#tests)).

A slow program lists its cases once with a weight in seconds (`IcoShardCase k_cases[]`), starts
`main` with `ico_shard_start` and wraps each case in `ico_shard_take("name")`. Cases go heaviest
first to the shard with the least weight so far, so ownership is a pure function of the table and
N; a run with no `--shard` owns every case. `ico_shard_take` stops the program when a call does not
match the table's next entry, and `ico_shard_finish` fails when a case was never reached, so the
table cannot drift from `main` (`port/test/ico_shard.h`, header comment).
`ico_add_sharded_test(<name> <N> PROGRAM <target> ...)` registers `<name>.1ofN` to `<name>.NofN`
and `<name>.cases`, which runs `--list-cases` as each shard k of N and checks through
`cmake/IcoShardCheck.cmake` that every run names the same owners, every case is in exactly one
shard and no shard is empty
(`cmake/IcoTests.cmake`, `ico_add_sharded_test`). `rd_blur`, `rd_mesh`, `rd_mesh_mali` and
`rd_mesh_minlimits` are sharded four ways (`port/render/CMakeLists.txt`).

## Test programs by area

Every test program, one entry each by directory, is in [TESTING_PROGRAMS.md](TESTING_PROGRAMS.md).

## Render gates

The frame gates replay saved [.rddump](ARCHITECTURE.md#terms) files with the replay tool. Dumps
hold game assets and are never committed, so the dump lists name paths on one machine; a missing
file is reported and skipped, and every gate exits 77 when it has nothing to run
(`tools/native_frame_check.py`, `tools/coverage_check.py`, docstrings).

`tools/native_frame_check.py` has four commands (`tools/native_frame_check.py`, docstring):

- `baseline` renders every dump of the list at the gate settings (`original_4x3_1x` and
  `enhanced_4x3_2x`), every named target and the presented picture, and writes the SHA-256 of
  each target's pixels to `docs/native_frame_baseline.json` with the harness commit
  (`cmd_baseline`).
- `gate-a` renders again and compares with the manifest exactly, reporting dump, settings, target
  and, with `--ref-dir`, the first differing pixel; exit 1 on any mismatch (ctest
  `native_frame_gate_a`).
- `gate-b` takes one game state captured at 4:3, 16:9, 32:9, 48:9 and 60:9 (an aspect sweep: a
  zip, a folder or PNGs with the aspect in the name, or `ICO_SWEEPS`) and checks the centre
  columns of every aspect equal the 4:3 picture byte for byte, with `--effects R` allowing a band
  of R pixels at each edge of the 4:3 picture (ctest `native_frame_gate_b`).
- `selftest` checks the gate-b logic on synthetic pictures (ctest `native_frame_check_selftest`).

The list is `tools/native_frame_dumps.txt`, or the list or folder `ICO_DUMPS` names; the ctests
skip unless `ICO_DUMPS` is set. The baseline is updated by running `baseline` after a change whose
pictures are accepted and committing the new manifest.

`tools/coverage_check.py` (ctest `coverage_corpus`, `coverage_check_selftest`) renders every dump
of `tools/coverage_corpus.txt` at its own aspect, `--enhanced --present 1376x896`, once with
`--aa off` and once with `--aa msaa4`, plus the interpolated frame of a dump that names a `PREV`,
and compares with the previous run: any mesh drawn fewer times than before fails, and more than
`pixels` percent of pixels changed by more than `tol` fails (defaults 0.5 and 8). The previous
run's pictures and counts live in `tools/.coverage/` (ignored by git; `ICO_COVERAGE_DIR`); a
passing item rolls its baseline forward and a failing one keeps the old, so the next run still
reports it. `seed` overwrites every baseline after a build that is accepted
(`tools/coverage_check.py`, docstring). Both tools hold a shared replay lock,
`build-host/tmp/replay.lock` or `ICO_REPLAY_LOCK`, so two runs never compete.

The script tests under `port/render/test/` run the tool or a test program and check its output:
`rd_replay_tool_list.cmake`, `rd_replay_tool_pass_dump.cmake`,
`rd_replay_tool_loss_bisect.cmake`, `rd_replay_tool_pass_split.cmake`,
`rd_replay_tool_pass_merge.cmake` and `rd_replay_tool_snap_swap.cmake` on the dump `rd_pixel` leaves behind, `rd_replay_tool_glow_hold.cmake` and `rd_replay_tool_save_blended.cmake` on two godrays dumps, `rd_depth_copy_dump.cmake` on a player dump with the depth fog (`ICO_FOG_DUMP`), and
`rd_fog_selftest.cmake`, which matches the `fog: depth path` line of the self-test, and
`rd_blend_selftest.cmake`, which matches the `blend: ... (self-test: ...)` line of the blend's,
and `rd_vuprepass_selftest.cmake`, which matches the `VU pre-pass: on|off (self-test ...)` line
of the pre-passes', and `rd_crack.cmake`, which matches the crack filler's `crack fill:` lines,
and `rd_vustage_ab.cmake` and `rd_vubatch_ab.cmake` over the dump corpus (`port/render/CMakeLists.txt`). The ctest `rd_replay_tool` renders `rd_pixel`'s dump with
`--present 640x480`; it requires `rd_pixel` to have run first (`FIXTURES_REQUIRED rd_dump`), as
does `rd_replay_tool_downsample`, which adds `--enhanced --resolution 2x --downsample --stats` and
passes on `present: downsample 1024x512 -> 512x256, window reduce off`.

## The render suite on OpenGL

`port/render/CMakeLists.txt` registers a second copy of the render tests on the OpenGL backend
(issue 126). `ico_rhi_vk` links every backend and `port/rhi/rhi_backend.c` takes
`ICO_RHI_BACKEND` from the environment as the default one, so no rebuild is needed:
`ico_rd_gl_twin(<name>)` reads the test `<name>` as registered (its command line, working
directory, skip switches and environment) and adds `<name>_gl` with

- the environment `ICO_RHI_BACKEND=opengl`, `LIBGL_ALWAYS_SOFTWARE=1` (Mesa's llvmpipe) and
  `SDL_VIDEO_DRIVER=offscreen` (the shape of `rhi_gl` in `port/rhi/CMakeLists.txt`), the
  Vulkan layer variables (`VK_ADD_LAYER_PATH`, `VK_KHRONOS_VALIDATION_*`, `ICO_VK_VALIDATION`)
  dropped and the test's own `ICO_RD_*` switches and `ASAN_OPTIONS` kept;
- exit 77 as a skip, the label `gl` besides `render`, and three times the timeout (1500 s where
  the test sets none: llvmpipe compiles the GLSL of the VU vertex shaders slower than lavapipe
  takes their SPIR-V);
- a folder of its own, `<build>/port/render/gl/` (or the test's folder under it), for the working
  directory and for every argument that was the build folder, so a `_gl` test never overwrites a
  Vulkan test's files (`rd_pixel_gl` leaves its dump in `gl/`, not where the fixture
  `rd_dump` is read).

`ctest -j1 -L gl` runs them; they exist when `ICO_RHI_OPENGL` and `ICO_SHADERS_GLSL` are on
and `ico_rhi_opengl` is linked (the configure line `render tests registered again on OpenGL`
gives the count). They are skipped (77) where the sources say a test is Vulkan-only or a
context is missing.

Not twinned: any test whose environment holds `ICO_VK_FAKE_*` or `ICO_VK_ATTACHMENT_READ`
(OpenGL ignores them: `*_mali`, `*_minlimits`, `*_d24s8`, `*_static`, `rd_crack_d24s8`,
`rd_crack_tiler`, `rd_crack_tiler_on`, `rd_crack_glow`, `rd_crack_aura`, `rd_blend_selftest*`, `rd_date_selftest_sabotage`,
`rd_fog_selftest_local_sabotage`; `ico_rd_gl_twin` skips these itself), the tests that start the
game or work on the replay tool's files (`rd_replay_tool*`, `rd_fog_depth_copy_dump*`,
`rd_vustage_ab`, `rd_vubatch_ab`, `rd_fades_fold_ab`, the frame gates, `coverage_corpus`; the compare below covers the dumps), and the
tests with no device (`rd_alpha_plan`, `rd_fx_frame`, `rd_vu_frac`, `rd_fades_fold`, `rd_mip`, `rd_passring`,
`rd_filter`, `rd_bisect_plan`, `rd_cache_name`, `rd_stream_audit`, `rd_local_probe_rule`, `fog_lut*`, `gsb_cull*`, `texpack*`, `gltf`).
The two-pass blend copies (`*_nodual`, `ICO_RD_NO_DUAL=1`, the renderer's own switch) are
twinned. The sources decide what else skips: `ctest -L gl` output lists the skipped ones.

The tolerance table is `ICO_RD_GL_TOLERANCE` (entries `<name>_gl|<VARIABLE>=<value>`, added to
that twin's environment). It may name only a test's own tolerance switch on a filtered or float
path (the presenter's bilinear blit, the fog LUT, the CRT's filtering), with the reason beside
the entry; the integer paths (DISPLAY, blend_int, UINT targets, DATE) are bit-exact. No test has
such a switch today (the tolerances are constants in the sources), so the table is empty: a `_gl`
failure is a finding to report, not a tolerance to add.

`python3 -I tools/native_frame_check.py compare` renders every dump of the list (default
`tools/native_frame_dumps.txt`, or `--dumps` / `ICO_DUMPS`) at the two gate settings with two
backends (`--a vulkan --b opengl` by default, passed to the replay tool as `--backend`; an
`opengl` run sets `LIBGL_ALWAYS_SOFTWARE=1` and `SDL_VIDEO_DRIVER=offscreen` unless set) and
prints every target that is not byte for byte equal, then one total line:

    compare vulkan against opengl: N targets: E exact, L within 1 LSB, M mismatch, S size, X not rendered
  On this machine (lavapipe against llvmpipe's OpenGL, Mesa 25.0.7, the 22 dumps of
  `tools/native_frame_dumps.txt`): 396 targets, 389 exact, 6 within 1 LSB and 1 at 2 levels, all
  seven in blurred work buffers at the Enhanced 2x setting (`AURA_WORK`, `WORK0`: 1 to 3 pixels
  of 917504 each), the filtered float paths the two drivers round their own way; every integer
  path (DISPLAY, SCENE, the UINT targets) is exact. A further difference is a bug to report.

`exact` is the same RGBA bytes, `within 1 LSB` no channel differs by more than 1 (a rounding in a
filtered path), `mismatch` a larger difference (the line gives the differing pixel count, the
largest channel difference and the box they lie in), `size` a different picture size and `not
rendered` a target one side did not write. Exit 1 on any mismatch, size or not rendered, 77 when
there is no list, dump, tool or Pillow. `--keep DIR` keeps both backends' pictures
(`DIR/vulkan/<dump>/<setting>/<target>.png`, `DIR/opengl/...`) for a closer look; without it they
are in a temporary folder named on the last line. `--backend NAME` also works on `baseline` and
`gate-a`. The dumps are machine-local and never committed, so this is a local command; CI has no
ctest for it.

## The render suite on a Vulkan 1.1 driver

`port/render/CMakeLists.txt` (the `VK11` block) registers render tests again under
`ICO_VK_FAKE_API` (issue 145; the profiles in [RHI.md](RHI.md#vulkan), variable table), each in
its own folder with the Vulkan run's expectations byte for byte, exit 77 a skip, and the label
`vk11` besides `render`:

- `ICO_VK_FAKE_API=1.1` (a stock 1.1 driver: the fence ring, `VkRenderPass` objects, no local
  read, no driver properties, no image format list), folder `<build>/port/render/vk11/`:
  `rd_pixel_vk11`, `rd_present_vk11`, `rd_crt_vk11`, `rd_debug_vk11`, `rd_raw_vk11`,
  `rd_fog_vk11`, `rd_shadow_vk11`, `rd_interp_vk11`, `rd_msaa_vk11`, `rd_perf_vk11` (its pass and
  barrier counts are `rd_perf`'s: the pass path changes no count) and `rd_mesh_vk11.1of4` to
  `rd_mesh_vk11.4of4` (the disc's models; 77 without the disc image);
- `ICO_VK_FAKE_API=1.1ext` (a 1.1 driver with the extensions: the KHR timeline semaphore and
  dynamic rendering), folder `vk11ext/`: `rd_pixel_vk11ext`, `rd_present_vk11ext`.
- `ICO_VK_FAKE_API=1.1 ICO_VK_FAKE_NO_DYNAMIC=1` (the Adreno combination: the fence ring,
  `VkRenderPass` objects and all-static pipelines, which Android forces on a tiler; the plain
  `vk11` runs still offer `VK_EXT_extended_dynamic_state`), folder `vk11s/`, the `VK11S` block:
  the same tests and shards as the first list under the names `rd_pixel_vk11s` ...
  `rd_mesh_vk11s.4of4`, expectations the Vulkan runs' byte for byte.

With `port/rhi`'s `rhi_vk_vk11` and `rhi_vk_swapchain_vk11` (the RHI cells and the window path,
swapchain recreation included, on the fence ring and the `VkRenderPass` path under validation) they
are `ctest -j1 -L vk11`. The OpenGL twins leave them out (a Vulkan fake). The real 1.1 loader and
driver are not covered here: lavapipe is a 1.3 driver held to 1.1 by the switch.

## The render suite on a Vulkan 1.0 driver

`port/render/CMakeLists.txt` (the `VK10` block) registers the same render tests as `VK11`'s first
profile under `ICO_VK_FAKE_API=1.0 ICO_VK_FAKE_NO_DYNAMIC=1` (issue 158, Android 8: the instance
created at Vulkan 1.0 with `VK_KHR_get_physical_device_properties2`, the device at 1.0 with
`VK_KHR_maintenance1` and `VK_KHR_maintenance2`, the core 1.1 entry points cleared so a core 1.1
call left unguarded stops the run, the fence ring, `VkRenderPass` objects and every pipeline state
static, as on an Android 8 phone; [RHI.md](RHI.md#vulkan), variable table), folder
`<build>/port/render/vk10/`, the Vulkan run's expectations byte for byte, exit 77 a skip, label
`vk10` besides `render`: `rd_pixel_vk10`, `rd_present_vk10`, `rd_crt_vk10`, `rd_debug_vk10`,
`rd_raw_vk10`, `rd_fog_vk10`, `rd_shadow_vk10`, `rd_interp_vk10`, `rd_msaa_vk10`, `rd_perf_vk10`
(its pass and barrier counts are `rd_perf`'s) and `rd_mesh_vk10.1of4` to `rd_mesh_vk10.4of4`.
Under `ICO_VK_FAKE_NO_D32F_ATTACHMENT=1` (the D32F depth-attachment bit hidden, an Adreno 505 on a 1.0.61
driver) the twins `rd_pixel_vk10_nod32f`, `rd_present_vk10_nod32f` and `rd_interp_vk10_nod32f` run with
the same expectations; `rd_present`'s `checkNoD32Keys` finds the effects depth's two keys in the
reachable set exactly where `RhiLimits.depth32Attachment` is true, and its effects-depth cell finds the
forced pass not drawn when it is false.
`rd_present`'s `checkReduceKey` finds the present's box average (`aa_reduce_ps`, RGBA8) in the
reachable set in both blend modes on every device.
With `port/rhi`'s `rhi_vk_vk10` and `rhi_vk_swapchain_vk10` they are `ctest -j1 -L vk10`; CI's
full `ctest` runs them. The shaders are SPIR-V 1.0 for every device (`spirv_check`'s version gate,
[SHADERS.md](SHADERS.md)), so the validation layer sees no module newer than the device. A real
1.0 loader (no `vkEnumerateInstanceVersion`) and a real 1.0 driver are not covered: lavapipe is a
1.3 driver behind a 1.4 loader, held to 1.0 by the switch.

## The render suite on Direct3D 11 (WARP)

CI's `windows` job (`.github/workflows/ci.yml`, on `windows-2022`, after `linux`) runs the
renderer's tests on the Direct3D 11 backend through WARP, Microsoft's software adapter (issue
126: on a GTX 750 Ti both start-up self-tests read back their cleared target). Nothing is built on Windows: the `linux` job's `win-x64` step copies the
programs below and `SDL3.dll` (their only import beyond the system's) into
`build-host/win-x64-warp/` (`tools/ci_d3d11_warp.sh stage`) and uploads it as the artifact
`win-x64-warp-tests`; the `windows` job downloads it and `tools/ci_d3d11_warp.sh run` (Git Bash)
runs each program with `ICO_RHI_BACKEND=d3d11` (`port/rhi/rhi_backend.c` takes the default backend
from it, as the OpenGL twins take `opengl`), `ICO_D3D11_ADAPTER=warp` and `ICO_D3D11_DEBUG=1`. A
step before it tries to add the debug layer (the optional feature `Tools.Graphics.DirectX`,
Graphics Tools); it does not gate, and the device line in each log says whether the layer was
there (`debug layer on` or `asked for, not installed`). The tests carry their own expectations,
the ones their OpenGL twins pass with, so a pass means Direct3D 11 draws what Vulkan and OpenGL
draw:

| program | how it runs | what a pass shows |
| --- | --- | --- |
| `rhi_d3d11_shader_plan_test` | as ctest's `rhi_d3d11_shader_plan` | the shader route's CPU half on Windows |
| `rhi_d3d11_test --console` | the RHI cells on WARP, again with the constant buffers mapped, the written mark's byte counts, and one upload buffer bound at t0 (from 0) and t5 (from 256 to 2560 bytes) in ten groups (more views than the backend keeps), each column exact and the submit's bytes 6656; a texture copied into a storage buffer by the backend's compute shader (section 1f: three copies, one with the buffer still bound to a pixel shader, every element where it belongs and 0xEE elsewhere, two passes reading it at t4 before and after a copy, three refusals writing nothing, 5 copies counted); the hardware section skips (no GPU on the runner), so does Vulkan; the swapchain section on a hidden SDL window | the RHI's exact-texel cells on Direct3D 11 on both constant buffer paths (the bind cache cell among them: `rhi_d3d11_bind_cache`, its pass line on both runs), a submit's upload writes cut at the written mark, every debug-layer error a failure |
| `rd_blend_selftest_test fixed` | through `rd_blend_selftest.cmake` as ctest's `rd_blend_selftest`, without its Vulkan profile line | the blend self-test passes (`fixed-function ok`) at start and after a new size |
| `rd_vuprepass_selftest_test on`, `off` | through `rd_vuprepass_selftest.cmake` as ctest's `rd_vuprepass_selftest`, `rd_vuprepass_selftest_unbatched` (`ICO_RD_VU_PREPASS_BATCH=0`) and `rd_vuprepass_selftest_sabotage` (`ICO_RD_VU_PREPASS_SABOTAGE=1`, mode `off`) | the VU vertex pre-pass on Direct3D 11: its record copied by the backend's compute shader, its pictures byte for byte as the programs' entries draw them in the batches (`vu_X_batch_pre_vs`) and one an entry (`vu_X_pre_vs`), the draw merge on beside it; a record drawn a texel off turns it off with the self-test's line (FXC's first compile of every pre-pass module) |
| `rd_fog_selftest_test inplace` | through `rd_fog_selftest.cmake`, results `inplace ok [0-2], copy not tried, buffer not tried` | the fog depth self-test passes and the probe lines are there |
| `rd_pixel_test <work>` | as ctest's `rd_pixel` | the pixel cases, and the dumps `rd_pixel_frame.rddump`, `rd_pixel_vu.rddump` and `rd_pixel_vu36.rddump` (written at Original 1x by the VU seams' S4 and S3) |
| `rd_replay_tool <dump> <png> --present 640x480 --stats --backend d3d11` | on rd_pixel's dump; its log must say `rd: VU merge: on` and never `off on Direct3D 11` (the row `rd_replay_tool_merge_on`), and have the `stats: bind cache: X pipeline, Y bind group and Z state repeats skipped` line (the row `rd_replay_tool_bind_cache`) | the replay tool replays and presents on Direct3D 11, with the draw merge on |
| `rd_vumerge_ab.cmake` | on rd_pixel's `rd_pixel_vu.rddump` (64 code-32 draws of one prelit mesh under one state), `rd_pixel_vu36.rddump` (a code-36 cut and kick pair) and `rd_pixel_frame.rddump`, as ctest's `rd_vumerge_ab_pixel`; some line must say `(N draws merged into G` with N above 0 (the row `rd_vumerge_ab_joined`) | the draw merge on Direct3D 11: every target byte for byte between `ICO_RD_VU_MERGE=0` and the merge, `merge on`, fewer GPU draws, no `rhi_d3d11 debug error` or `corruption` line in either run's output (both A/B scripts fail on one) (the 16:9 setting skips for these 4:3 frames) |
| `rd_vumerge_fallback.cmake` | on `rd_pixel_vu.rddump`, as ctest's `rd_vumerge_fallback` | the fallback on Direct3D 11: under `ICO_RD_FAKE_MERGE_FAIL=1` one faked failure line naming the merge build, one `rd: VU merge: off: the driver could not compile ... (Direct3D 11)` line, and the `--time 3` replays after it drawing what `ICO_RD_VU_MERGE=0` draws (more than the merge) |
| `rd_replay_tool <rd_pixel_vu.rddump> <png> --stats --backend d3d11` | the frame of 64 code-32 draws; its log must have the stats line `VU pre-pass on: 1 or 2 passes, ... 0 mesh and grid draws the old way` and no refused copy (the row `rd_replay_tool_vu_record`), and the backend's line `texture-to-buffer copies into storage buffers: a compute shader (cs_5_0` (the row `rd_replay_tool_vu_compute_copy`); the first replay's log `rd: VU pre-pass: on: ` (the row `rd_replay_tool_prepass_on`) | the record path in the replay tool on Direct3D 11 |
| `rd_vustage_ab.cmake` | on the same three dumps, as ctest's `rd_vustage_ab_pixel`; some line `N targets equal over 3 dumps at 2 settings` (the row `rd_vustage_ab_compared`) | the pre-pass on Direct3D 11 against the programs' own entries (`ICO_RD_VU_PREPASS=0`): every target byte for byte at Original 4:3 1x and Enhanced 4:3 2x, every VU command through the record, no debug-layer error and no refused record copy in either run |
| `rd_vufuse_ab.cmake` | on the same three dumps with `-DEXTRA=` (no phone frames), as ctest's `rd_vufuse_ab_pixel` | the fused scissor pair on Direct3D 11: every target byte for byte between `ICO_RD_VU_FUSE=0` and the fuse, the merge on in both runs (the phone and 16:9 settings skip for 4:3 frames; Enhanced 4:3 2x through the entries and the record runs) |
| `rd_present_test <work>` | as ctest's `rd_present` | the presenter's frames |
| `rd_msaa_test` | as ctest's `rd_msaa` | multisampling and its resolve |
| `rd_crt_test <work>` | as ctest's `rd_crt` | the CRT passes |
| `rd_mirror_test` | as ctest's `rd_mirror` | the mirror mode |

Every log goes to the artifact `d3d11-warp-logs` (with `summary.md`, also the job's step summary:
the device line and each test's verdict, time and debug-layer error count). A failed test prints,
in a collapsed group, its device line (`rhi_d3d11: <adapter> (D3D11, feature level ...)`), its
first 40 debug-layer lines, its self-test lines (with `self-test failed on Direct3D 11; its last
draw: ...`, [RHI.md](RHI.md#direct3d-11)) and its last 150 lines. A test's exit 77 ("no usable
device") is a failure here, since WARP is always there, and so is a `SKIP:` line or any debug-layer
error or corruption line in a test's log, whatever its exit status (the runtime drops the call the
error names, so a program can pass its own checks while drawing less than it should); a test runs
at most 2400 s.

Not run there: the variants whose environment holds a Vulkan fake (`ICO_VK_FAKE_*`,
`ICO_VK_ATTACHMENT_READ`: Direct3D 11 ignores them), the variants whose expected results name a
path Direct3D 11 has not (`rd_fog_selftest_all` and `_fallback` expect the buffer path, which the
backend reports `unsupported`; `rd_fog_selftest_local` the local read; `rd_date_selftest` the
stencil DATE; the blend's exact modes: no destination read, `rov` false), the vertex pre-pass
self-tests' plain, unmerged, tick and triangle sabotage variants (the default, unbatched and
sabotaged ones run), the tests that need the disc or the machine-local
dumps, and the rest of the render suite (a longer list is a matter of adding lines to the script;
these are the ones issue 126 needs). WARP's filtering may round a filtered path an LSB away from
lavapipe's where a test compares hashes (`rd_present`, `rd_crt`); such a failure names the check,
and the integer paths are exact on every backend. The same tests on a real card are the user's
rc check (Direct3D 11 with `ICO_D3D11_DEBUG=1` once).

## Tests that need the disc

These cache variables point the tests at the player's files ([../BUILDING.md](../BUILDING.md#tests)
has their defaults; the registrations are in `port/*/CMakeLists.txt`):

- `ICO_DISC_IMAGE`, the PAL image. Without it `vfs_disc`, `archive_disc`, `appearance_disc`,
  `fmv_fields`, `gsb_cull_stage`, `texpack_disc`, `texpack_export_disc`, `df_index_disc`,
  `menu_look`, `menu_text`, `gallery`,
  `content_table_chains` and the headless game runs below skip; `sndn2`, `fmv`, `shadow_spawn` and
  `rd_mesh` run only their synthetic part, and `tables_loader` reads the base ELF instead.
- `ICO_DISC_IMAGE_KOR`, the Korean image (`baserom/Ico_KOR.iso`). Without it `vfs_disc_kor`,
  `archive_disc_kor`, `archive_manifest_kor`, `menu_look_kor` and `content_table_chains_kor` skip,
  and `gallery_kor` leaves out the disc checks, as the Japanese rows below do.
- `ICO_DISC_IMAGE_JPN`, the Japanese image (`baserom/Ico_JPN.iso`). Without it `vfs_disc_jpn`,
  `vfs_disc_jpn_bin`, `vfs_disc_jpn_cue`, `vfs_disc_jpn_chd`, `archive_disc_jpn`,
  `archive_manifest_jpn`, `menu_look_jpn` and `content_table_chains_jpn` skip, and `gallery_jpn`
  leaves out the disc checks.
- `ICO_BASE_ELF`, the PAL base ELF: with pyelftools, `tables_loader` and `tables_manifest` are
  built from it (`port/data/CMakeLists.txt`); `gallery` reads its tables.
- `ICO_BASE_ELF_JPN` and `ICO_DISC_IMAGE_JPN`, the Japanese base ELF and image (defaults
  `baserom/jpn/baseelf.elf`, `baserom/Ico_JPN.iso`): `tables_loader_jpn` (through the image when
  present) and `tables_manifest_jpn` (and `tables_loader_kor` and `tables_manifest_kor` with the Korean ELF), all skipped without the ELF; `menu_text_jpn`,
  `menu_look_jpn` and `gallery_jpn` (with the PAL ELF too) skip without it, `menu_text_kor`,
  `menu_look_kor` and `gallery_kor` likewise without `ICO_BASE_ELF_KOR` (default
  `baserom/kor/baseelf.elf`), and `model_viewer_jpn` / `model_viewer_kor`
  (`tools/gen_mv_region.py --region <slug> --check`, both ELFs through `gen_data_c.py`, so
  pyelftools) check the model viewer's Japanese column, which the Korean disc shares.
- `ICO_TEXPACK_DIR`, a PCSX2 pack's replacements folder: `texpack_disc`, `texpack_e2e`,
  `texpack_pack_files`.
- `ICO_FOG_DUMP`, a player dump with the depth fog: `rd_fog_depth_copy_dump` and `_d24s8` (skipped
  without it).
- `ICO_BOOT_DUMP_DIR`, a start-up's frame dumps: `rd_boot_present`; `ICO_BOOT_DUMP_OLD` sets
  `ICO_BOOT_OLD=1` for dumps whose frames 118 to 120 draw stage 1 (`port/render/CMakeLists.txt`).

Five tests start the headless game itself, each in a fresh folder with its own `ico-pc.ini` and a
pad script, `RUN_SERIAL` and with a long timeout:
`model_viewer_headless` (`port/game/test/model_viewer_headless.cmake` with
`model_viewer_pad.txt`), `gallery_sweep` (`port/ui/test/gallery_sweep.py`, which plays through
`ICO_GALLERY_PLAY`), `credits_headless` (`port/ui/test/credits_headless.py`, with
`unlock_credits=1`), and `state_roundtrip_headless` and `state_roundtrip_headless_settings`
(`port/game/test/state_roundtrip_headless.cmake` with `state_roundtrip_pad.txt`, the `[dev]`
`state_save_at`, `state_load_at`, `state_hash_at` and `state_dump_at` keys, [CONFIG.md](CONFIG.md#dev); also
skipped on a sanitizer build, where save states are off); the two Python scripts share
`headless_common.py`, whose pad walks the boot to the title and then Settings > Extras. A worktree
configured with `-DICO_DISC_IMAGE=/nonexistent -DICO_BASE_ELF=/nonexistent` cannot start the game
in any of them (`tools/dev_build.sh`, header); the ctest exclusion used for such runs, `-E
"^(model_viewer_headless|gallery_sweep|credits_headless|rd_replay_tool|state_roundtrip_headless|state_roundtrip_headless_settings)$"`,
also leaves out `rd_replay_tool`, whose dump comes from `rd_pixel`.

The save states' round trip is not part of any gate; run it on a build with the disc image
(`ICO_DISC_IMAGE`) after a change to save states, the registry or anything the game's memory
holds:

    tools/dev_build.sh . ico_pc -- -R state_roundtrip_headless

or, in a configured headless build folder, `ctest -R state_roundtrip_headless` (both tests; they
take a few seconds each). On a failure the message names what is missing or different and the
log (`<build>/port/game/state_roundtrip_headless_run/logs/ico-pc.log`; the settings test's two
runs are under `state_roundtrip_headless_settings_run/save` and `.../load`). The pad's ticks
assume the stage is playable by tick 400 and the pause menu up by 440 (`state_roundtrip_pad.txt`,
header comment); a `state: refused save slot 1: not on the pause menu` or `the disc is busy`
there means those ticks need moving, a hash that differs means something the game's memory
holds was not put back, or holds a host value the hash rule does not leave out yet. The message
then names it:

    state_roundtrip_headless:
      the hashes differ: "state: hash <h1> of arena+gdata+gbss at tick 700" before the load, "..." after
      state: hash masked N pointer words, M host ids at tick 700 (...)
      state: hash masked ... (the second hash's)
      state_region_diff: <dump-700-1> (tick 700, hash <h1>) against <dump-700-2> (...)
        arena: 41943040 bytes, masked hash .. / ..: R differing range(s), B byte(s)
        gdata: ...
        gbss: ...
        the first 10 differing symbols of the game's data and bss (S in all):
          gbss <symbol> (<object file>): n byte(s) differ, the first at +0x.. (gbss+0x...): <a bytes> / <b bytes>
          ...
        the arena's first 10 differing ranges:
          arena+0x... (EE 0x...): n byte(s): <a bytes> / <b bytes>

The two dumps stay in `state_roundtrip_headless_run/states/<disc>/` for a closer look
(`tools/state_region_diff.py <a> <b> <build>/ico_pc.map --elf <build>/ico_pc --max 100`); a
symbol of the game's data names the global or, with `--elf`, the static; an arena range is an
EE address, which a heap block or a model's records hold. `tools/state_region_diff.py
--selftest` checks the tool on two made-up dumps.

## Environment switches

The variants that force GPU paths a desktop driver does not take are in
[../BUILDING.md](../BUILDING.md#tests)'s table (`ICO_RD_NO_DUAL`, `ICO_VK_FAKE_NO_DUAL`,
`ICO_VK_FAKE_D24S8`, `ICO_VK_FAKE_LIMITS=mali|min|moltenvk`, `ICO_RD_BLEND=storage|fixed`). The other
switches the tests and their registrations use:

| variable | read in | effect |
| --- | --- | --- |
| `ICO_VK_FAKE_NO_DST`, `ICO_DX_FAKE_NO_DST` | `vk_device.c`, `d3d12_device.c` | a device that cannot read the pixel it draws over |
| `ICO_DX_FAKE_NO_TIMESTAMPS` | `d3d12_cmd.c` | D3D12 without GPU timestamps (the `no GPU timestamps` log text) |
| `ICO_VK_FAKE_NO_MSAA` | `vk_device.c` | single sampling only |
| `ICO_VK_FAKE_NO_INTERLOCK` | `vk_device.c` | the fragment shader interlock off, the attachment read kept (the `rd_blend_selftest*` ctests: lavapipe has the interlock from Mesa 25.2, their expectations are a device without it) |
| `ICO_VK_FAKE_LIMITS=moltenvk` | `vk_device.c` | MoltenVK: `RhiLimits.moltenvk` set, no attachment read, the interlock the device's own (`rd_blend_selftest_moltenvk_storage`: `ICO_RD_BLEND=storage` keeps the interlock; run without `ICO_VK_FAKE_NO_INTERLOCK`; it skips on a device without the interlock, as Mesa before 25.2's lavapipe) |
| `ICO_VK_FAKE_ADRENO` | `vk_device.c` | Qualcomm's Adreno driver: the attachment read not offered (`rd_blend_selftest_adreno`) |
| `ICO_VK_ATTACHMENT_READ` | `vk_device.c` | 1 offers the attachment read under `ICO_VK_FAKE_ADRENO` too (`rd_blend_selftest_adreno_forced`) |
| `ICO_VK_VALIDATION` | `vk_device.c` | the validation layer; the render and RHI tests set it with `VK_ADD_LAYER_PATH` when the layer is in the toolchain |
| `ICO_VK_GLOBAL_BARRIERS` | `vk_cmd.c` | a global barrier before every pass instead of hazard tracking, for A/B runs |
| `ICO_VK_DEPTH_ALWAYS` | `vk_cmd.c`, `vk_pipeline.c` | 1: every depth test passes; the occlusion measurement's run without the depth (`tools/occlusion_corpus.py`, `rhi_vk`'s occlusion cell; [RENDER.md](RENDER.md#culling)) |
| `ICO_RD_FOG_PATH` | `rd_fog_path.c`, `rd_profile.c` | `local` (the profile's `fogLocal` on where the device has local read), `inplace`, `copy` or `buffer` forces the fog's depth path; `test` makes the self-test try every path (`rd_fog_local`, `rd_fog_selftest_local`) |
| `ICO_RD_DATE_PATH` | `rd_profile.c`, `rd_date_selftest.c` | `stencil` DATE through the stencil bit wherever the device has local read, kept whatever the self-test finds; `snapshot` the R8 snapshot (`rd_date_selftest`) |
| `ICO_RD_BLUR_FOLD` | `rd_profile.c` | 1 folds the staticBlur pairs (`fx_blur2_ps`) on any device, 0 never (the A/B of the replay tool's `--pass-dump`) |
| `ICO_RD_DEPTH_COPY` | `rd_fog_path.c` | 0 samples the depth in place, 1 copies it |
| `ICO_RD_LOCAL_PROBE` | `rd_profile.c`, `rd_local_probe.c` | in a headless run (no window), 1 the game's desktop local reads and their start-up probe where the self-tests run, 0 the local reads without the probe; unset there, the desktop class keeps the snapshots (`rd_local_probe_kept`, `rd_local_probe_off`) |
| `ICO_RD_LOCAL_PROBE_SABOTAGE` | `rd_local_probe.c` | `slow` counts the local reads' probe time 4 times (turned off), `fast` a quarter (kept) (`rd_local_probe_off`, `rd_local_probe_kept`) |
| `ICO_RD_FOG_SABOTAGE` | `rd_fog_path.c` | makes named paths fail the self-test (`rd_fog_selftest_fallback`; `local`: `rd_fog_selftest_local_sabotage`) |
| `ICO_RD_SHADOW_ONE` | `rd_profile.c` | 0: the shadow count's seven stencil-tested draws (and the box reduction above 1x) instead of the one draw ([dev] `shadow_one`); 1 the one draw where the backend has the stencil view, kept whatever the self-test finds |
| `ICO_RD_SHADOW_BSPLINE` | `rd_profile.c` | 1: the shadow composites above 1x through the cubic B-spline instead of one bilinear tap ([dev] `shadow_bspline`; `rd_fx_plan.c` `rd__sprite_above1x`); `rd_shadow_test` flips `RdProfile.shadowBspline` itself |
| `ICO_RD_SHADOW_SABOTAGE` | `rd_shadow_one.c`, `rd_shadow_selftest.c` | 1: the one draw's table is wrong at m = 1, so the shadow count's self-test fails and the seven draws resolve the count (`rd_shadow_selftest_sabotage`) |
| `ICO_RD_DATE_SABOTAGE` | `rd_replay_tiler.c`, `rd_date_selftest.c` | 1: DATE's setup writes 0 into the stencil bit where the alpha's MSB is 1, so the self-test fails (`rd_date_selftest_sabotage`) |
| `ICO_RD_BLEND_SABOTAGE` | `rd_blend_selftest.c` | makes named blend modes read white in the blend's self-test (`rd_blend_selftest_sabotage*`) |
| `ICO_RD_VU_PREPASS_SABOTAGE` | `rd_vuprepass_selftest.c`, `rd_replay_vu.c` | 1: the pre-passes' self-test draws its first command's vertex records a texel off; `tri`: its triangles' slot A a slot off; `ticks`: the tick stage's tick 1 at tick 0's camera; each fails it and the pre-pass is turned off (`rd_vuprepass_selftest_sabotage`, `_sabotage_tri`, `_ticks`) |
| `ICO_RD_VU_PLAIN` | `rd_pipeline.c` | 1: every VU draw with the VU vertex shaders' plain variants, as on a device of the table (`kVuPlainOnly`); the replay tool's pictures at a wide aspect then show the plain-only picture ([RENDER.md](PIPELINES.md#the-fallback-entries)) |
| `ICO_RD_VU_EXTENDED` | `rd_pipeline.c` | 1: the extended variants whatever the device table or `ICO_RD_VU_PLAIN` says |
| `ICO_RD_VU_SET` | `rd_pipeline.c` | `plain` or `extended`: as `ICO_RD_VU_PLAIN=1` or `ICO_RD_VU_EXTENDED=1` (`[dev] vu_set`); `auto` or unset: the device table's |
| `ICO_RD_SHEET_SHADER` | `rd_core.c` | 1: the menus' sheet text through `font_sheet_ps` at every scale, no bake (`[dev] sheet_shader = true`; the bake's A/B) |
| `ICO_RD_PASS_AREA` | `rd_pass_area.c` | 1: a render pass on SCENE whose draws are all 2D with a known place takes the render area of the tiles they touch, 0: every pass its whole target, `auto` or unset: the profile's (on on a tile-based GPU; `[dev] pass_area`; `rd_pass_area_ab`, `rd_state` `testPassArea`) |
| `ICO_RD_CLEAR_CARRY` | `rd_pass_cut.c` | 1: a pending clear carried past a source's barrier into the next pass on its target, 0: flushed as before, `auto` or unset: the profile's (on on a tile-based GPU; `[dev] clear_carry`; `rd_pass_cut_ab`, `rd_pass_cut`) |
| `ICO_RD_CLEAR_SKIP` | `rd_pass_cut.c` | 1: a clear of AURA_WORK or AURA_TAP whose only reader is a constant read not drawn (drawn later where read), 0: drawn, `auto` or unset: the profile's (on on a tile-based GPU; `[dev] clear_skip`; `rd_pass_cut_ab`, `rd_pass_cut`) |
| `ICO_RD_LAST_STORE` | `rd_pass_cut.c` | 1: the depth store of the pass holding its last touch dropped where the walk keeps that pass open to the touch, 0: stored, `auto` or unset: the profile's (on on a tile-based GPU; `[dev] last_store`; `rd_pass_cut_ab`, `rd_pass_cut`) |
| `ICO_RD_AURA_BOX` | `rd_aura_box.c` | 1: the aura's read of SCENE into FEED128 above 1x in two passes (ROWBOX, then FEED128's sum), 0: in one, `auto` or unset: the profile's (on on a tile-based GPU; `[dev] aura_box`; `rd_aura_box_ab`, `rd_blur` case `aura_box`) |
| `ICO_RD_AURA_KERNEL` | `rd_aura_kernel.c` | `gs`: the aura's read of SCENE into FEED128 with the GS's own taps instead of the box above 1x, a measurement (`[dev] aura_kernel = gs`; the picture differs) |
| `ICO_RD_VU_PREPASS` | `rd_replay_vu.c` | 0: no VU vertex pre-pass, every VU draw the old way (`[dev] vu_prepass = false`); the tests switch it with `rd__set_vu_prepass` ([RENDER.md](PIPELINES.md#the-vertex-and-triangle-pre-passes)) |
| `ICO_RD_VU_PREPASS_BATCH` | `rd_replay_vu.c` | 0: the VU pre-pass's draws one an entry and stage instead of batched (`[dev] vu_prepass_batch = false`; `rd_vubatch_ab`, `rd_vuprepass_selftest_unbatched`; [VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-pre-passs-batches)) |
| `ICO_RD_VU_MERGE` | `rd_replay_vu.c` | 0: every VU draw a GPU draw of its own instead of the draw merge (`[dev] vu_merge = false`; `rd_vumerge_ab`, `rd_vuprepass_selftest_unmerged`; [VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-draw-merge)) |
| `ICO_RD_STORE_PLAN` | `rd_profile.c`, `rd_store_plan.c` | 0: every depth store kept (`[dev] store_plan = false`); 1 the store plan (the default); `all`: every depth store dropped, a wrong plan for `rd_store_plan_ab` ([WORK_BUFFERS.md](WORK_BUFFERS.md#the-store-plan)) |
| `ICO_RD_STORE_POISON` | `rd_store_plan.c` | 1: a depth or stencil load the store plan leaves undefined clears to 0.5 and 0x5A instead, and the shadow count's colour load it drops clears the colour to 0x5A, the garbage a tile-based GPU may leave (`rd_store_plan_ab`, `rd_shadow_test` case p) |
| `ICO_RD_FAKE_MERGE_FAIL` | `rd_pipeline.c` | 1: the first pipeline made with a draw merge build is destroyed and kept as failed, as a build the driver refused; the merge goes off from the next replay (`rd_vumerge_fallback`; [VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-draw-merge)) |
| `ICO_RD_VU_FUSE` | `rd_replay_vu.c` | 0: code 36's kick draws under their own pipelines instead of the fused scissor pair (`[dev] vu_fuse = false`; `rd_vufuse_ab`; [VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-fused-scissor-pair)) |
| `ICO_RD_CONSTANT_READS` | `rd_const_read.c` | 0: a post sprite whose source target holds only its last clear reads the target as before instead of being drawn in the clear's colour (`[dev] constant_reads = false`; `rd_constant_reads_ab`; [WORK_BUFFERS.md](WORK_BUFFERS.md#a-cleared-source-read-as-its-colour)) |
| `ICO_RD_NO_SPRITE_PLANES` | `rd_screen_planes.c` | 1: the sprite planes builds off for the run, the screen-prim triangles through the plain sprite builds, as `[dev] pipeline_skip` makes it on a device whose driver stopped creating them (issue 161; `rd_noplanes_ab`; [WORK_BUFFERS.md](WORK_BUFFERS.md#without-the-sprite-planes)) |
| `ICO_RD_PERF_LIST_STAMPS` | `rd_perf.c` | 0: no list or effect timestamp (`[dev] perf_list_stamps = false`); `rd_perf_test` switches it with `rd__set_perf_list_stamps` ([RENDER.md](RENDER.md#the-per-pass-timing)) |
| `ICO_RD_PERF_PASSES` | `rd_perf.c` | 1: every replay timed pass by pass (`[dev] perf_passes = true`; the window writes `ico-pc-passes.csv`); `rd_perf_test` and the replay tool's `--pass-times` switch it with `rd__set_perf_passes` ([RENDER.md](RENDER.md#the-per-pass-timing)) |
| `ICO_RHI_BACKEND`, `ICO_VK_DEVICE`, `ICO_D3D12_ADAPTER` | `rhi_backend.c`, `vk_device.c`, `d3d12_device.c` | pick the backend, the Vulkan device, the D3D12 adapter (`warp`) |
| `ICO_FIXED_CLOCK` | `clock.c` | the wall clock fixed at 2002-01-01 unless `0` |
| `ICO_GALLERY_PLAY` | `gallery.c` | a play script for the music gallery |
| `ICO_UI_POPUP_TEST` | `ui_host.c` | the test popup at [Main tick](ARCHITECTURE.md#terms) 100 and every 150 ticks after (`[dev] popup_test`) |
| `ICO_START_STAGE` | `ico2/common/src/debug.c` (`debug_TryToGetStartStage`), `gamestate.c` | `[dev] start_stage` handed over: a stage 2 to the disc's last (105 on the European disc, 103 on the 104-row discs, 102 on the June 2001 demo: `ico_region_start_stage`) to start in, which suspends achievements; another value logs `start_stage N refused` |
| `ICO_TTY` | `kernel_host.c` | the game's `scePrintf` to stdout |
| `ICO_FMV_DECODE` | `movie.c` | 0 skips decoding in any build; the timing is unchanged |
| `ICO_RD_DUMP_EVERY`, `_DIR`, `_INTERP`, `_FROM` | `rd_core.c` | the frame dumps `ico-pc.ini` hands to rd |
| `ICO_VU_REF_AT` | `rd_replay_tool.c` | the kicked triangles containing a GS point |
| `ICO_BOOT_DUMPS`, `ICO_BOOT_OLD` | `rd_boot_present_test.c` | the start-up dumps (the folder when no argument is given), and `ICO_BOOT_OLD=1` for dumps whose frames 118 to 120 draw stage 1 |
| `ICO_DUMPS`, `ICO_SWEEPS`, `ICO_COVERAGE_DIR`, `ICO_REPLAY_LOCK` | the gate tools | the dump list, the aspect sweeps, the coverage state, the lock |

**Running the storage tests on a lavapipe with the interlock (no root).** The CI runners' Mesa 25.2 lavapipe has the fragment shader interlock, so `rd_pixel_storage`, `rd_fog_storage`, `rd_present_storage`, `rd_replay_tool_pass_merge` and `rd_blend_selftest_moltenvk_storage` run the exact storage path there (banner `blend exact (interlock)`); a lavapipe before 25.2 keeps the fixed-function blend and passes without exercising it. To reproduce on a machine with an older Mesa: download from snapshot.debian.org the trixie-backports `mesa-vulkan-drivers` and `mesa-libgallium` 25.2.6-1~bpo13+1 `.deb`s (into `build-host/tmp/lvp-interlock/deb/`; sid's 26.x needs a newer glibc), `dpkg-deb -x` both into `build-host/tmp/lvp-interlock/root`, copy `root/usr/share/vulkan/icd.d/lvp_icd*.json` and rewrite its `library_path` to the extracted `root/usr/lib/x86_64-linux-gnu/libvulkan_lvp.so`, then run the test from its build directory with `ICO_RD_BLEND=storage VK_ICD_FILENAMES=<that json> LD_LIBRARY_PATH=<root>/usr/lib/x86_64-linux-gnu` (the log's `blend:` line names the mode in force).

`grep -rn 'getenv("' port/` and `grep -a -rn 'getenv("' ico2/` list the rest (the game sources
read `ICO_START_STAGE`, `ICO_SWITCH_TO`, `ICO_SWITCH_AT` and `ICO_HEAP_ASAN_FREE`).

## F12 dumps and the replay tool

In the [window build](ARCHITECTURE.md#terms) F12 writes an `.rddump` of the last closed frame, a PNG
of the whole DISPLAY frame and a PNG of the picture as [presented](ARCHITECTURE.md#terms) into the
[user folder](ARCHITECTURE.md#terms)'s `dumps/`, then puts the dump, the tick before's dump
(`-prev.rddump`, `rd_dump_prev_frame`) and a copy of the log into `frame-<time>-v<vsync>.zip` and
deletes the loose dumps (`rd_replay_tool frame.zip out.png --interp T frame.zip` replays a picture
between them). When the next present is drawn between the two ticks, the frame it drew follows as
the zip's third `.rddump`, `frame-<time>-v<vsync>-blended.rddump` (`rd_dump_blended_arm`): unzipped,
`rd_replay_tool frame-<time>-v<vsync>-blended.rddump out.png` replays that present as drawn, with
no `--interp`, so a picture only the device drew can be told from the tool's own rebuild; the zip
exists because issue trackers refuse
`.rddump` attachments (`port/platform/hotkeys.h`, `ico_frame_dump_paths`, `ico_frame_present_path`;
`port/platform/dump_zip.h`, `ico_frame_dump_zip`). F11 makes the stats lines come every second for
30 s (`port/platform/hotkeys.h`, header comment).

Shift+F12 in photo mode is the aspect sweep: the paused frame re-rendered at 4:3, 16:9, 32:9, 48:9
and 60:9 into `sweep-<time>.zip`, holding `<aspect>.rddump` and `<aspect>.png` for each with the
4:3 member first, so the replay tool, which takes a zip's first `.rddump`, replays it
(`port/platform/dump_zip.h`, `ico_sweep_zip`, `ico_sweep_aspect_name`). The aspect tokens are the
ones `native_frame_check.py gate-b` reads.
`[dev] dump_every = N` writes every Nth frame ([PLATFORM.md](PLATFORM.md#hotkeys-and-dump-zips)).

**The blended present's frame.** `--interp` is the tool's own blend of the two ticks, so a fault
only the device's present shows (issue 163's sliver, absent from lavapipe's rebuild of the same
pair) needs the frame the device drew. F12 arms `rd_dump_blended_arm` (`port/render/rd.h`) with
`<name>-blended.rddump` when its zip holds the tick before; the next `rd_present` takes the arm. A
present drawn between two ticks (alpha below 1, not a keep frame) writes the frame it drew,
`rd__interp_frame`'s copy of the tick with the blended blocks, bones and streams (its scratch
meshes) and the ticks' cameras (dump version 14), through `rd__dump_frame` after the present, so
the write is not in the picture's time; a present at alpha 1 or of a keep frame is the tick's own
picture and writes nothing (`port/render/rd_interp.c` (`rd_present`, `blendedDumpTake`)); an arm no
present takes expires with nothing written (`blendedDumpExpire`). `rd:
dump: the blended present of frame F at alpha A written to <path>` follows, and the window's pump
moves the file into the zip ([DISPLAY.md](DISPLAY.md#hotkeys-for-pictures)). It is an ordinary dump.
What the presenter holds outside a dump (FEED_HELD, DISPLAY_HELD and WORK3_HELD, the glow a
tick's later presents paste) starts empty in the tool, as for every dump, so a later present's
pasted glow is missing from the replay; the geometry is the device's. `--save-blended DUMP` (with
`--interp T PREV`) writes the tool's frame between two dumps with the same writer after its
replay, which `rd_replay_tool_save_blended` checks against the `--interp` run byte for byte.

The dump format is `port/render/rd_dump.c`'s header comment, and the replay tool's options are
listed in [RENDER.md](RENDER.md#dumps-and-the-replay-tool). For tests the useful ones are
`--pass-dump DIR` (every named target from one run), `--occlusion` with `--nop @FILE` (the
hidden-draw measurement and the replay without the hidden draws; `tools/occlusion_corpus.py` runs
them over `tools/occlusion_corpus.txt` with `--stats --gpu-repeat K`, under the replay lock it takes
for each run, and writes the report and the culling gate's verdict, [RENDER.md](RENDER.md#culling)), `--list --no-device` (the commands without a
GPU), `--present WxH`, `--aspect`, `--enhanced`, `--aa`, `--interp T PREV` and with it
`--save-blended DUMP` (the frame between the two written as F12's blended present's frame)
(`port/render/tools/rd_replay_tool.c`, usage).

### Replaying a phone's loss dump as the phone drew it

A loss dump from a phone (`loss-*.zip` in its report zip) replays on lavapipe with the phone's
drawing settings forced, so the commands are the ones its renderer recorded (issue 123's two
Adreno 740 hangs replayed this way with the validation layer on: 0 messages):

```
ICO_VK_FAKE_NO_DST=1 ICO_VK_FAKE_NO_DYNAMIC=1 ICO_GPU_ROBUST=1 ICO_RD_FOG_PATH=copy \
ICO_RD_DATE_PATH=snapshot ICO_RD_STORE_PLAN=0 ICO_RD_BLUR_FOLD=1 ICO_RD_UINT_VIEW=0 \
ICO_RD_FX_DST_LOCAL=0 ICO_RD_SHADOW_ONE=0 ICO_RD_VU_PREPASS=0 \
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json ICO_VK_VALIDATION=1 \
rd_replay_tool loss.zip out.png --enhanced --resolution 3x --full-height --full-pixel \
  --present 3048x2032
```

The list is issue 123's (a Qualcomm tiler, two builds); for another phone
the `rd: profile:` start line of its log says which switches its renderer ran with, the window
line its resolution and screen size (`--resolution`, `--present`), and the loss diagnostics line
whether robust access was on (`ICO_GPU_ROBUST`). `ICO_VK_VALIDATION=1` needs `VK_ADD_LAYER_PATH`
when the layer is the toolchain's, as the render tests set it. What lavapipe cannot imitate: the tiler class itself (chosen by the vendor id,
`rd_replay_tiler.c`), the phone driver's binning and shader compiler, the swapchain and present,
and frames in flight (the tool replays one frame and waits). A clean replay says the commands are
valid, not that the phone's driver runs them. The pass ring in the phone's log records what the
renderer recorded, in order, not where the GPU stopped (no checkpoint extension on Qualcomm's
driver): its last entries are the newest work given to the GPU, any of which may be the one it
hung on.

## Pad scripts, recordings and the trace

A pad script drives the pad instead of a person, in either build, through `--pad-script FILE` or
`[dev] pad_script` in `ico-pc.ini`; the [headless build](ARCHITECTURE.md#terms) also takes a
`pad-script.txt` beside the program (`port/platform/main_host.c`, `setup_pad`). Each line is `<tick> <buttons-hex> [lx ly rx ry]`:
from that [Main tick](ARCHITECTURE.md#terms) on, the pad returns the line's values until the next
line; ticks increase strictly; the buttons are the game's logical word, active high (`0x0040` Cross,
`0x0010` Triangle, `0x0800` Start, `0x1000` up, `0x4000` down, `0x8000` left, `0x2000` right); the
sticks are 0 to 255 with 128 centred (`port/input/pad_script.h`, header comment). The pad read of
tick t sees the line for tick t, and `--ticks N` stops after ticks 0 to N-1, so scripts, traces and
the tick limit share one numbering.

A pad recording ([INPUT.md](INPUT.md#recordings)) is a pad script of what the game read, so the
headless build replays a player's session with `pad_script=<the file>`; it is on by default in the
window build, in `logs/input-<time>.txt`, and off headless (`input_record=`). Its header records
the values of `video.video_mode`, `game.language`, `gameplay.mirror`, `gameplay.stick_fix`,
`gameplay.yorda_safe`, `gameplay.developer_mode`, `dev.start_stage`, `gameplay.door_fix`, `gameplay.yorda_trips`,
`video.anti_aliasing`, `gameplay.hold_type`, `gameplay.hold_type_chosen` and `gameplay.skip_opening` (the boot's ticks), the eight `variants.*` keys (the barrier's is `(unset)` until chosen), and the native frame's width and aspect (`port/platform/sim_keys.c`
`ICO_SIM_KEYS`; `port/platform/main_host.c` `record_header`; `ico_input_record_frame_lines`).

The trace (`trace=` or `--trace FILE`) writes a header naming its columns and then one line per Main
tick after the vsync in which the tick finished: `tick vsync stage sys0 sys1 gameover gf0 .. gf12
save`, the story flags as thirteen 8-digit hex words and `save` the FNV-1a 32-bit hash of the
checkpoint save image (`port/platform/trace_host.h`, header comment). A run is a function of its
input and the vsync count
([ARCHITECTURE.md](ARCHITECTURE.md#ee-kernel-threads-semaphores-interrupts)), so the same build,
script and disc give the same trace.

## The developer build helper

`tools/dev_build.sh <worktree-or-.> <target>... [-- <ctest args>]` configures
`<worktree>/build-host/linux-x64` once with the `linux-x64` preset and `-DICO_LINK_EXE=ON` (and
`-DICO_DISC_IMAGE=/nonexistent -DICO_BASE_ELF=/nonexistent` in a worktree without `baserom/`),
builds exactly the named targets at `-j4` under a shared build lock
(`build-host/tmp/build.lock`, `ICO_BUILD_LOCK`), then runs ctest with the arguments after `--`,
failing when they select no test, and prints a one-line summary (`tools/dev_build.sh`, header).

## The audits as tests

The audits over the game sources are tools, some run as ctests on native builds (`offset_audit`,
`template_audit`: `port/test/CMakeLists.txt`; `softdouble_gate`: `port/math/CMakeLists.txt`),
`const_write_audit` on native clang builds only, and the rest in the pre-commit hook and CI; what
each refuses is in [GAME_CODE.md](GAME_CODE.md#audits). The region converter's two runs are of
that kind (pre-commit step 8, CI's `No compile-time region block in the game sources`):
`python3 -I tools/region_blocks.py --selftest` converts `tools/test/region_blocks/fixture.c` (one
block per class over a six-row table, `#elif` and three-target blocks, the later targets' nesting
shapes to four deep from upstream's BUILDING.md and TARGETS.md (the prototype inside a USA arm, the
demo inside both, the three-name negative block with the prototype's exclusion nested, `e3.c`'s
side-by-side block, a nested `#elif`), a demo arm past `LARGE_LINES`, a block holding a switch's
first label, a six-argument `LN`, the deny-listed forms, both kinds of switch, a twin) and compares
it, the report and the bit rows with the files beside it, checks the full and the used-forms
`region.h`, `rename` for kor on jpn and for three rows on pal (`rename_pal_expected.c`), the parser
to `MAX_DEPTH` and a block no row compiles, then compiles the result with `gcc -fsyntax-only`
against the generated header under each row's macro; `python3 -I tools/region_blocks.py check` holds `ico2/` and `sce/` to the
region rules and the region glue and exits 0, 2 (raw blocks remain, all hand items; the hook
warns, CI fails) or 1 (a rule broken) ([GAME_CODE.md](GAME_CODE.md#regions)). Other tests check generated files and
tables: `layout_asserts` (a program compiled from the generated asserts), `layout_asserts_fresh`,
`font_audit`, `font_subset_arimo` (`tools/subset_font_arimo.py --check`: Arimo's recorded code
points are the tool's ranges and the community translations' characters, its cmap the recorded
ones and, with fontTools 4.63.0 and the download, the committed file byte for byte the tool's
output), `font_subset_ja` (`tools/subset_font_ja.py --check`: the Japanese font's recorded
code points are the tables', its cmap the recorded ones and, with fontTools 4.63.0 and the
download from `tools/fetch_deps.sh`, the committed file byte for byte the tool's output),
`font_subset_ko` (`tools/subset_font_ko.py --check`, the same for the Korean font),
`font_subset_zh` (`tools/subset_font_zh.py --check`, the same for the Chinese font) and
`content_table_chains`, `content_table_chains_jpn` (`--version jpn`: the Japanese disc's boot
ELF, tables and groups) and `content_table_chains_kor` (`--version kor`; `port/test/CMakeLists.txt`),
`data_desc_fresh` (with the audit of MotionDef's bit reads), `tables_manifest`,
`tables_manifest_jpn` and `tables_manifest_kor`, `ee_layout_fresh_jpn`, `ee_layout_fresh_kor`, `ee_layout_fresh_ntsc`, `ee_layout_fresh_aug6` and `ee_layout_fresh_jun26` (each disc's `ee_layout` file is what its recorded upstream revision gives),
`data_desc_fresh_ntsc`, `data_desc_fresh_aug6` and `data_desc_fresh_jun26` (`gen_data_desc.py --check --version ntsc|aug6|jun26`: the USA, August 2001 and June 2001 sets, written before `config/regions.txt` lists their rows, are what the table with the pending rows up to each gives; [DATA.md](DATA.md#the-usa-discs-set), [DATA.md](DATA.md#the-august-2001-preview-discs-set), [DATA.md](DATA.md#the-june-2001-magazine-demos-set)), `data_map_selftest` (`gen_ee_layout.py --selftest`: the version-block readers and `record_map`'s rules on a made-up record, each map row with and without it: a moved bit-field copied from the disc's bit, a shorter array with its `default ... =0` row, a wider integer with its `narrow` row, a missing field's default, a host-only field the disc lacks left to the record clear, a `rename` row (a field under another name) and a scalar array gathered from separately named fields, an unsigned bit-field held narrower, and the error for each row taken away, for a rename of some elements only and for a row that settles nothing; [DATA.md](DATA.md#the-map-rows))
(`port/data/CMakeLists.txt`), and the package staging (`package_textures`,
[PACKS.md](PACKS.md#tests)).

`texture_catalog` runs `tools/test/texture_catalog_test.py`: `tools/texture_catalog.py` on a 12-line
fixture (one row per name, the first user and the count of the others, the size and level notes,
the European column, `--check` returning 1 on a changed file, repeatable output). It needs Python only
([PACKS.md](PACKS.md#exporting-every-texture-of-the-disc)).

`region_literals_check` runs `tools/check_region_literals.py`: at the port's sites (every C file
under `port/` but the tests, the generated tables and the region tables, and the port's blocks in the
17 game files that carry port hooks) a layout, menu row, story flag, stage or pad bit written as a
number, or the European value of a region table field that differs between the discs, is a finding
unless the line routes it through `ico_region_cur()`, a `PAL_<SET>` selector or an `ICO_<SET>`
predicate, or sits in a disc's own arm (a function named `_ntsc`, `_jun26`, ...). Issue 147 was
this shape (`gflagOn(382)` on the North American disc). `tools/check_region_literals_allow.txt`
lists the sites checked by hand, each with the discs' numbers; a stale entry fails too. `--list`
prints every finding. `region_literals_selftest` (`tools/test/region_literals_test.py`) plants one
finding per category in a temporary tree (`tools/test/region_literals/`) and checks that each is
found once, that routed lines and the game's own lines are not, and the allow list's rules. Both
need Python only.

`docs_check` runs `tools/check_docs.py` over README.md, `docs/*.md` and `docs/port/*.md`: relative
links and their anchors, history and package-code wording, issue numbers and internal words in the
player guides, TODOs, and the package's list of player guides; `tools/check_docs_allow.txt` holds
the allowed exceptions (top-level `CMakeLists.txt`, `docs_check`; `tools/check_docs.py`,
docstring). `python3 -I tools/check_docs.py` runs it without a build.

`rc_tools_gate` runs `tools/check_rc_text.py --selftest`: the rc label rule (`-rc[0-9]+$`), the
text search in a plain file and in a zip's inflated entries (UTF-8; UTF-16 for the absent check),
`--label`, `--expect`, `--cmake-flag` and `--gradle-flag`, `strings_en.c`'s row text inside `#if
ICO_RC_TOOLS`, `CMakeLists.txt`'s and `build.gradle`'s defaults and release-label refusals, the
manifest's provider enabled by the placeholder, and each `tools/package_*.sh` taking its switch from
the checker and checking its program with `--label`, never turning the tooling on itself.
`rc_tools_binary` runs `tools/check_rc_text.py --expect absent` on this build's `ico_pc` (`present`
in a window build with `ICO_RC_TOOLS` on; not on Android or a cross build)
([BUILDING.md](../BUILDING.md#release-candidate-tools)).

`game_data_ranges_selftest` runs `tools/check_game_data_ranges.py --selftest` (made-up nm tables:
the ELF and PE bounds, a symbol on either side, a file static twice, no bounds, brackets out of
order). `game_data_ranges_binary` reads this build's `ico_pc` with the toolchain's `nm` (not on
Android or Apple; a cross build too, since nothing runs): ten of the game's globals, the file
statics `texTable` and `cdvdThreadStack` among them, inside the game data ranges and four of the
port's outside ([PLATFORM.md](PLATFORM.md#game-data-ranges)).

`hlsl_fxc_check` runs `tools/check_hlsl_fxc.py` over `port/shaders` (Python only, no build products):
it fails when a word FXC reserves (`pass`, `sample`, `line`, `point`, `linear`, `texture`, ...) is
declared as a variable, parameter or struct member, because the Direct3D 11 backend compiles the
HLSL with FXC at run time and DXC (Vulkan, OpenGL, Direct3D 12) accepts those names. It cannot see
FXC's X4000 "potentially uninitialized variable" error class; that shows in a Windows log.

`spirv_check` runs `tools/check_spirv.py --sizes` over the SPIR-V modules the build compiled
(`<build>/shaders/*.spv`): a vertex shader has no vector of booleans, no `any()`/`all()` and no
boolean select, and uses only the opcodes of the envelope's set (a loop, `dot()`, a
logical-not-equal or an undefined value named), every module stays within its stage's size budget
in words, no module carries an array as a value (a whole-array load, store, construct, insert,
phi, select or copy), no vertex shader keeps a local array
([SHADERS.md](SHADERS.md#the-envelope)) and every module's header is SPIR-V 1.0 (the Vulkan 1.0
target, [SHADERS.md](SHADERS.md)); the log lists each module's words, instructions, blocks,
phis, loops, dot products, vector comparisons, boolean selects and `any()`. The vertex pre-pass's
entries (`vu_*_pre_vs`) need no plain variant and take the plain budget. Its `intlane` gate fails
a `vu_*` module with an `OpBitcast` of a float that a buffer load produced (an integer lane read
through a float load: a bone address, mark byte, card bit, table index or VuCB `uint4` vector whose
bits are a denormal float a driver may flush; issues 163/164,
[SHADERS.md](SHADERS.md#integer-lanes-never-through-a-float-load)): the shaders with those lanes
read through `float4` loads give 84 findings, the current ones none. `python3 -I tools/check_spirv.py --sizes <build>/shaders` runs it by
hand; on single files (`check_spirv.py a.spv b.spv`) it skips the variants gate.

## Which tests a change needs

- A new Options row: `ctest -L ui`. `settings` builds every page and compares its rows with fixed
  lists (`port/ui/test/settings_test.c`, `testBuild`), so a new row updates those lists; new
  settings cases go in `port/ui/test/settings_extra_test.c` (its header). `settings_render` draws
  every screen, `menu_text` the rows in the sheets' look, and `font_audit` (every code point inside the
  font subsets, `tools/font_audit.py`) and `font_coverage` (a glyph for each) check the row's text
  in the eight languages; `strings_complete` fails until every table has the new strings. A new
  Japanese character needs `tools/subset_font_ja.py` run again (`font_subset_ja` fails until it
  is), a new Hangul syllable `tools/subset_font_ko.py` (`font_subset_ko`), a new character of the Chinese
  table `tools/subset_font_zh.py` (`font_subset_zh`), a community translation's character outside
Arimo's ranges `tools/subset_font_arimo.py` (`font_subset_arimo`). A gameplay option also needs `options`
  (`port/game/test/options_test.c`).
- A new config key: `config` (the reader, writer and precedence, `port/config/test/config_test.c`),
  `host_config` when `ico-pc.ini` maps it (`port/platform/host_config.c`, `ini_map`), `settings`
  when a row writes it, and the module's own test (`options`, `photo`, `appearance`, `input`).
- A new texture-pack format: `texpack_load` (each loader on files built in memory,
  `port/render/test/texpack_load_test.c`), `texpack`, `texpack_e2e`, and with `ICO_TEXPACK_DIR`
  `texpack_pack_files`; `package_textures` when the shipped README changes
  ([PACKS.md](PACKS.md#tests)).
- The render thread: `frame_mailbox`, `pace_loop`, `lifecycle`, `host_config` and `rhi_vk` (the
  thread submit and thread record cells; `rhi_d3d12` on Windows), `rd_submit_queue` (the submit
  thread's queue), `rd_thread_ab`, `rd_tex` (and `rd_tex_test` under
  the tsan build), `rd_present` (the deferred state picture, the slot waits by whose), `rd_replay_tool_save_blended` (the
  interpolation on the render thread); the device runs are the window's, with `[dev]
  render_thread = sync` against `off` for the same pictures ([PLATFORM.md](PLATFORM.md#the-render-thread)).
- A pacing change: `pace_policy`, `pace_loop` and `perf_block`
  (`ctest -L platform -R '^(pace_|perf_block)'`), and `rhi_vk_present_mode` for the present-mode choice. The Windows
  vblank clock (`port/platform/display_clock_win.c`) and the D3D12 waitable swap chain have no
  ctest and are checked in a Windows build, on the `lock held` and `window: display:` lines
  ([DISPLAY.md](DISPLAY.md)).
- A new shader: `shaders_table`, whose `expected[]` lists every embedded entry
  (`port/shaders/test/shaders_table_test.c`), then `ctest -L rhi` and `-L render`
  (`shaders_pixel` and its variants, `vu1` for a VU shader, `gs_math`, `rhi_d3d12_plan`); on
  Windows `rhi_d3d12` and `compare_backends.cmd` for the DXIL side. Any shader change:
  `spirv_check` (the vertex shaders' shape and envelope, the size budget, no array-typed values,
  no local array in a vertex shader, the SPIR-V 1.0 header) and `hlsl_fxc_check` (`python3 tools/check_hlsl_fxc.py`: no word
  FXC reserves used as an identifier; FXC's "potentially uninitialized variable" errors it cannot
  see). A change to a VU vertex shader or to the card test
  ([VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-card-test)): `vu1`, `vu1_mali`, `rd_native` and the frame
  gates ([Render gates](#render-gates)), whose pictures must not change.
- A new achievement: `achievements`, which fails unless one of its runs unlocks every achievement
  and each has a title and a description in every language; its count check
  (`ico_ach_count() >= 20 && ico_ach_count() <= 30`) bounds the table, which holds 30
  (`port/game/test/achievements_test.c`; `port/game/achievements.c`, `s_defs`). `font_coverage`
  checks the new strings' glyphs.
- A change to `tools/region_blocks.py`: `python3 -I tools/region_blocks.py --selftest`; a change
  in what it emits updates `tools/test/region_blocks/expected.c`, `expected_report.tsv` or
  `expected_bits.txt` after reading the difference, and a new class or pattern gets a block in
  `fixture.c`.
- A change to the renderer that either backend runs: `ctest -j1 -L gl` ([The render suite on
  OpenGL](#the-render-suite-on-opengl)) besides `-L render`, and `native_frame_check.py compare`
  when the pictures of a dump may differ between the backends.
- A port site that names a layout, menu row, story flag, stage or pad bit: `region_literals_check`
  (the number goes through `port/core/region.h`, or into the allow list with each disc's number).
- A change to these documents: `docs_check`.

## CI

| workflow | job | runner | what it runs |
| --- | --- | --- | --- |
| `ci.yml` | `linux` | `ubuntu-24.04` | the hygiene checks (no disc data, format, no `ICO_HOST` conditional, no region block, stack-layout, struct-cast and call-type audits, generated files fresh), then `linux-x64` headless and window builds with `ctest`, `linux-x64-clang` with `ctest`, and the `win-x64` cross build (tests built; the renderer's Direct3D 11 set uploaded for `windows`) |
| `ci.yml` | `windows` | `windows-2022` | after `linux`: the renderer's tests from the `win-x64` build on Direct3D 11 through WARP, the debug layer tried ([above](#the-render-suite-on-direct3d-11-warp)) |
| `ci.yml` | `extra` | `ubuntu-24.04` | a matrix of `asan` and `fptrap` (build `all tests`, `ctest`) and the `win-x64-clang` cross build |
| `ci.yml` | `linux-arm64` | `ubuntu-24.04-arm` | the no-disc-data check, the `linux-arm64` preset with `ctest`, the filtered-pixel render tests in a step that does not gate, the arm64 package on release tags |
| `ci.yml` | `android` | `ubuntu-24.04` | the debug APK (`assembleDebug`), `tools/check_android_flags.sh`, the APK structure checks |
| `ios.yml` | `build`, `publish` | `macos-15`, `ubuntu-24.04` | the unsigned iPhone IPA for a published release, attached to it |
| `ios.yml` | `build-macos`, `publish-macos` | `macos-15`, `ubuntu-24.04` | the macOS app for a published release, attached to it |

(`.github/workflows/ci.yml`, jobs `linux`, `windows`, `extra`, `linux-arm64`, `android`;
`.github/workflows/ios.yml`.)

The triggers, steps and caches are [../BUILDING.md](../BUILDING.md#continuous-integration)'s.

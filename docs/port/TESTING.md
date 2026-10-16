# Testing

What the port's tests are and how they are wired: the test framework and its labels, every test
program by area, the render gates over saved frames, the tests that need the player's disc, the
environment switches that force GPU paths, the frame dumps and the
[replay tool](ARCHITECTURE.md#terms), [pad scripts](ARCHITECTURE.md#terms) and
[traces](ARCHITECTURE.md#terms), the developer build helper, the documentation gate, CI, and which
tests a change needs. How to run `ctest` is [../BUILDING.md](../BUILDING.md#tests)'s.

## Files

| path | what it holds |
| --- | --- |
| `cmake/IcoTests.cmake` | the `add_test` wrapper (labels, the `tests` target), `ico_add_sharded_test` |
| `cmake/IcoShardCheck.cmake` | the `<name>.cases` check of a sharded test |
| `port/test/ico_check.h` | `CHECK`, `CHECK_STR`, `ico_check_summary` |
| `port/test/ico_shard.h` | the shard split of a slow test program |
| `port/*/test/`, `port/platform/fpenv_test.c` | the test programs and their helpers |
| `port/render/test/*.cmake` | script tests over the replay tool and the fog self-test |
| `tools/native_frame_check.py`, `tools/native_frame_dumps.txt`, `docs/native_frame_baseline.json` | the native-frame gates, their dump list and their baseline |
| `tools/coverage_check.py`, `tools/coverage_corpus.txt` | the regression renders and their corpus |
| `port/render/tools/rd_replay_tool.c` | the replay tool |
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
the `tests` target depend on all of them; `rd_replay_tool`, `mc_import` and `ico_pc` stay in `all`
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

One line per program, from its first comment; the ctest names follow where they differ from the
program's. Variants (`_nodual`, `_mali`, `_minlimits`, `_storage`, `_fixed`, `_d24s8`) rerun a
program under an environment switch ([Environment switches](#environment-switches)).

### port/audio/test

- `adpcm_slots_test.c`: the ADPCM request daemon with both stream slots taken; a stream already
  fading out gives its slot up (`adpcm_slots`).
- `adpcm_test.c`: SPU ADPCM block decoding against hand-worked vectors (`adpcm`).
- `libsd_irx_test.c`: the LIBSD.IRX reader on a synthetic module, its structural checks and its
  fallback (`libsd_irx`).
- `mix_gain_test.c`: the music, effects and film gains, every fixed volume word unchanged at 100 %
  (`mix_gain`).
- `pan_test.c`: mirror mode's pan swap and the per-[vsync](ARCHITECTURE.md#terms) block size
  (`audio_pan`).
- `sndn2_test.c`: the SNDN2DRV host and the Sg sequencer against what the IRX's code does; reads
  the disc when present; also `sw_log`'s bound (the longest page-full line arrives whole, a longer one ends in `...`) (`sndn2`).
- `spu2_bench.c`: the [SPU2](ARCHITECTURE.md#terms) render's cost, and with `--check` a CRC-32 of
  five 10 s scenes (`spu2_render_crc`).
- `spu2_test.c`: the software SPU2 and its [libsd](ARCHITECTURE.md#terms) front end: interpolation,
  ADSR, voices, reverb (`spu2`).
- `volume_test.c`: the output volume's mapping, clamp and integer scaling (`volume`).

### port/config/test, port/null/test, port/save/test

- `config_test.c`: `config.toml`'s reader and writer, round trips, the ini over `config.toml` over
  default precedence, the language answer, the wall clock and the [EE](ARCHITECTURE.md#terms) timers
  (`config`).
- `null_devices_test.c`: the pad as an empty console answers it (`null_devices`).
- `save_test.c`: the host memory card through the calls `mcard.c` makes, then `mcard.c` and
  `mcdata.c` themselves on the [fiber](ARCHITECTURE.md#terms) scheduler, then the card importer
  (`save`).

### port/data/test

- `archive_test.c`: the archive backend and the first-run extractor, on a synthetic image and on
  the disc (`archive_synthetic`, `archive_disc`).
- `iso_import_test.c`: the Android first start's copy of the chosen image over SDL memory streams
  (`iso_import`).
- `json_test.c`: `port/data/json.h`: numbers, strings, nesting, the depth limit, malformed input
  (`json`).
- `tables_test.c`: the runtime table loader against the PS2 build's tables generated from the base
  ELF (`tables_loader`, with `ICO_BASE_ELF`).
- `vfs_test.c`: the [VFS](ARCHITECTURE.md#terms) and its ISO9660 backend, the
  [libcdvd](ARCHITECTURE.md#terms), [SIF](ARCHITECTURE.md#terms) and [IOP](ARCHITECTURE.md#terms)
  RAM layers (`vfs_synthetic`, `vfs_disc`).
- `sha1.h` is the tests' SHA-1.

### port/fmv/test

- `fmv_fields_test.c`: with the disc, every `.pss` demuxed whole and its pictures' field flags and
  cadence counted (`fmv_fields`).
- `fmv_test.c`: the player's parts without the game: a synthetic PSS, the demuxer, the decoder
  (`fmv`).
- `rd_video_test.c`: `rd_video.c` on [rd](ARCHITECTURE.md#terms)'s headless device, pictures in and
  the present target read back (`rd_video`, `rd_video_mali`).
- `rd_video_window_test.c`: the renderer's output following the swapchain on SDL's offscreen
  driver when the window is resized (`rd_video_window`).

### port/game/test

- `achievements_test.c`: the game-state view over a synthetic snapshot, every achievement's
  condition, suspension, `achievements.toml` round trips (`achievements`).
- `appearance_disc_test.c`: the characters' colours against the disc's CLUTs (`appearance_disc`).
- `appearance_test.c`: the recolour on synthetic CLUTs: identity, coverage, swatches, randomize
  (`appearance`).
- `credits_test.c`: `staffroll.c` over a short table with the port credit appended, and the
  credits row's lock (`credits`).
- `door_grace_test.c`: the doorway fix's arm and grace and a copy of the exit check's guard
  (`door_grace`).
- `hand_ik_test.c`: the hand records as `ACTGame_ConnectHand` sets them through the real
  `handManager.c`: every target finite, a NaN into `GetTableArcCos` giving 0 and a counted fault
  (`hand_ik`).
- `ico_frame_test.c`: the [native frame](ARCHITECTURE.md#terms)'s widths for each aspect and the
  options' frame (`ico_frame`).
- `kanban_boot_test.c`: the boot's card check and signs (`kanbanBoot.c`) for a first and a later
  boot (`kanban_boot`).
- `model_viewer_cam_test.c`: the viewer's camera step without the game (`model_viewer_cam`).
- `model_viewer_test.c`: the viewer's table against its motion-kind blocks and the game's table
  sizes, its names in five languages (`model_viewer`).
- `model_viewer_effects_test.c`: a motion's effects with no stage animation loaded
  (`frameDependSequence.c` and `stageMultiBgaManager.c` compiled in over stubs): the stage
  animation entries' guard (no write, the slot kept, its log line once), and every motion of every
  viewer model stepped through `ExecFrameDependSequence` in the game (each stage animation skipped)
  and in the viewer (no stage animation or particle, the game's sounds and vibration); synthetic
  tables (`model_viewer_effects`) and the PAL base ELF's, where BOY SWIM reaches the guard
  (`model_viewer_effects_elf`, 77 without the ELF).
- `model_viewer_headless.cmake` with `model_viewer_pad.txt`: the headless game driven from boot to
  the first model, an animation and back (`model_viewer_headless`).
- `options_test.c`: the gameplay options' defaults and setters, the video effects' keys (Water
  reflections' `effect_water_sharp` off by default, saved, outside the preset; Glow size's
  `effect_glow_size` `"scaled"` by default, `"original"` read in any case, an unknown word read as
  Scaled, saved as the word, outside the preset), and
  `eBrainGetTarget` with Yorda safe off and on (`options`).
- `photo_test.c`: photo mode's state and both cameras, identity, moves, limits; the Film effect's
  cycle off, 1 to 4, Deja vu (5), off, `film_effect = 5` read back as Deja vu and a value past it
  as off (`photo`).
- `poly_flat_test.c`: `IsPointIsInScreen`'s x bound is the 4:3 half width at every native width
  (`poly_flat`).
- `shadow_spawn_test.c`: the depth a shadow rises from, read at the generator kind's word as the
  PS2 reads it; with the disc, every generator row (`shadow_spawn`).
- `title_logo_test.c`: the logo's models and the hidden state's steps (`title_logo`).

### port/input/test

- `input_record_test.c`: the pad recording's format, round trip through the script reader, the
  change-only rule and the sample point (`input_record`).
- `input_test.c`: bindings from config text, dead zone, quantisation, the stick fix, merging, the
  [libpad](ARCHITECTURE.md#terms) buffer, rumble, Escape (`input`).
- `mouse_camera_test.c`: the mouse camera's speed and range for the hand camera (`mouse_camera`).
- `pad_script_test.c`: the `--pad-script` reader and what the null pad then shows the game's pad
  code (`pad_script`).
- `touch_test.c`: the touch overlay's layouts over a table of screens, the sticks and buttons
  (`touch`).

### port/math/test

- `math_test.c`: the PS2 float helpers bit for bit, the R register, matrices, vectors and
  quaternions against references (`math`).
- `newlib_test.c`: `ico_rand`, `ico_qsort`, libm values in both FP environments (`newlib`).
- `softdouble_test.c`: `softdouble.c` against the EE's own soft float from `sce/libgcc`
  (`softdouble`); `dpbit_harness.cmake` builds that oracle on the host.

### port/platform/test

- `android_paths_test.c`: the Android file layout on any platform (`android_paths`).
- `arena_test.c`: the EE RAM [arena](ARCHITECTURE.md#terms): alignment, zero fill, address mapping,
  heap statistics (`arena`).
- `diag_test.c`: the crash report (also from a worker thread under a batch's pipeline note), assert, watchdog, progress (with the heartbeat's start-up set count), start-up activity file (the batch notes; a start ended inside a 2D batch: the next start is lazy for that start only, nothing written, and logs the line and the last finished batch with its memory; that lazy start ended inside a pipeline: the start after writes `[dev] pipeline_set = "lazy"` through a stubbed config write, for any version (another version's line then leaves the policy to the count), with a failed write (this start only, its lazy run saying so, and the count kept so the next end writes it); a count file that cannot be written, logged; the "for this start only" run's clean exit and its end between two creations, each with its own line: the next start tries the set again and the row starts over; not after a restart, an extended VU batch or with the setting already `lazy`; an end in the background, by the lifecycle's event or `onTrimMemory(20)` (the mark set and logged once when both come), or with the app in the background before the recording began, is not counted, its mark written, logged and deleted, and back in the foreground the mark is gone and the end counts; a lazy run ended in the background between two creations, its mark logged; on a device the table makes lazy the first end promises nothing and its lazy run is not "for this start only", the second writes nothing, both clearing the row; an extended VU batch's end: plain for that start only, a plain run in between keeps the row, the next such end writes `[dev] vu_plain_only = true`, a run that built the extended ones and finished clears the row, and on a device the table keeps to the plain variants nothing is written (its lazy column alone does not stop the write); the low-memory signal's line at once and as the file's `system: ` line, in the set and in a lazy run, the level-80 one marking the background) cases, device-loss restart and loss bisect (the loss noted in the activity file, the bisect stubbed: once, before the game, never again after a start ended inside it) cases, the loss bisect's second level (its state file, the step stubbed: the draw found in 4 starts of at most 3 lost steps each, draw 1 with its classes in 5 starts, killed at step 3 and continued at 4, a renderer that did not start, the step and start bounds, the time budget, files that do not parse) cases, the watchdog and a wait the system holds (the event pump held past the limit without an end, a stall in the game's code after it ended, the bound for one wait, that bound with no limit in force), each in a child process
  whose log is read back; the symbolizer (`symmap.h`) on a small GNU ld map of an ELF program (a wrapped function section, a static function in an object, an anchor not at the module's start), a Windows map excerpt, an nm table and its gzip form past inflate's 32 KB window, a cut gzip member and a map without the anchor; the crash's and the watchdog's symbol lines from `diag_test.map`, linked beside the test as `ico_pc.map` is beside the program; their report zips (`report.h`) read back with miniz (report.txt's end, summary and report block with its symbol lines, the log; the crash's report.txt kept as `diag_test_report.txt`, an example); the prune keeping the newest five; `ico_log_limited` (250 calls: the first 8, the 100th and 200th, the end summary's count; `ico_log_limited_n` with SDL's error bounds, the first 64) (`diag`).
- `dump_zip_test.c`: F12's zip, the aspect sweep's zip and the device-loss zip (the lost frame first, the pruning to the newest three), written and read back with miniz
  (`dump_zip`).
- `fiber_test.c`: [fibers](ARCHITECTURE.md#terms) switched round-robin with their stacks checked,
  deep stacks, the FP state inside a fiber (`fiber`, `fiber_guard`).
- `gpu_driver_test.c`: the Android driver packages: `meta.json`, refusals, installs from zips made
  here, the trial marker (`gpu_driver`).
- `host_config_test.c`: `ico-pc.ini` parsing and rewriting, SHA-1 vectors, path joining, the
  card folders read from the ini the game reads (`ico_host_ini_path`), portable mode, the `[dev]`
  keys exported as environment switches (`ico_dev_env_value`, `test_dev_env`); built again
  as `host_config_portable_test` (`host_config`, `host_config_portable`).
- `host_fs_test.c`: the file helpers on a UTF-8 path outside ASCII (`host_fs`).
- `hotkeys_test.c`: F12, Shift+F12 and F11, repeats ignored, no other key taken (`hotkeys`).
- `ios_chain_test.c`: the game's thread, message and memory layer on the fiber scheduler under
  simulated vsyncs (`ios_chain`).
- `lifecycle_test.c`: the mobile lifecycle's step tables on fake operations (`lifecycle`).
- `memory_test.c`: the game's allocator over the arena, [partitions](ARCHITECTURE.md#terms) at the
  EE's offsets (`memory`).
- `movie_pad_test.c`: the pad as a movie's skip check sees it from inside the player's loop
  (`movie_pad`).
- `pump_watch_test.c`: the event pump's history for its slow-pump lines: the window messages' names
  (the display's power, the power and device-change kinds, an unknown id), the noisy ones left out,
  repeats counted, the newest 16 messages and 8 SDL events kept newest first with their age, the
  message being handled named while it runs and timed, the watchdog's description, the controllers
  that came and went (a removal named from its arrival, the SDL driver from the GUID's byte 14) and
  SDL's own log ring (the newest 64 oldest first, a time bound, newlines folded, whole lines only in
  a short buffer) (`pump_watch`).
- `pace_loop_test.c`: the [vsync lock](ARCHITECTURE.md#terms)'s loop on a grid of the wall clock
  under a fake clock and a fake swapchain, with and without the display clock, at 60, 59.94 and
  120 Hz and on a display that drifts from its report (`pace_loop`).
- `pace_policy_test.c`: the slow-present decision, Resolution "auto"'s steps, the gap between
  presents, the vsync lock's n and alphas and its period's sanity, and the vblank clock's laws on a
  synthetic display, and the audio skip (`pace_policy`).
- `perf_block_test.c`: a block of replays by phase: sums, maxes, the replays over a threshold, the
  longest phase (`perf_block`).
- `sched_test.c`: the fiber scheduler against the EE kernel's rules through the SDK calls
  (`sched`).
- `startup_retry_test.c`: the steps after a graphics driver crash while building a pipeline, the
  crash chain to the box, the variable and the relaunch environment, and the `dst` field of a
  pipeline's or a start-up batch's note (`startup_retry`).
- `steam_shortcut_test.c`: Add to Steam's CRC and id, a sample `shortcuts.vdf` appended byte-true,
  the duplicate check, `loginusers.vdf` (`steam_shortcut`).
- `window_video_test.c`: fullscreen and mode requests on SDL's offscreen driver (`window_video`).
- `port/platform/fpenv_test.c`: the simulation's FP mode rounds toward zero and flushes denormals,
  the host mode is back to nearest; `trap` checks the fptrap build traps (`fpenv`, `fpenv_trap`).

### port/render/test

- `fog_lut_test.c`: the depth fog's game side, the same on every compiler (`fog_lut`;
  `fog_lut_arm64` under qemu).
- `gltf_test.c`: the glTF reader and writer, bit-exact round trips, GLB, a Blender-style file,
  refusals (`gltf`).
- `gsb_cull_test.c`: the game's cull against the blended pictures' cameras and the windows that
  scale with the width; with the disc, a stage (`gsb_cull`, `gsb_cull_stage`).
- `modelpack_test.c`: model packs end to end on synthetic models under the build tree
  (`modelpack`).
- `rd_blur_test.c`: `staticBlur.c`'s full-screen effects on rd (`rd_blur`, sharded); the
  `above1x` shard runs the aura's chain at Enhanced 4x against the CPU model with the above-1x
  kernels (`above1x_ref.c`), and every scale-1 comparison checks that no sprite took them
  (`checkAbove1x`, `compareAll`).
- `rd_boot_present_test.c`: a saved start-up through the presenter, present by present; needs
  `ICO_BOOT_DUMP_DIR` (`rd_boot_present`).
- `rd_blend_selftest_test.c` with `rd_blend_selftest.cmake`: the exact blend's start-up self-test
  and the attachment read's Qualcomm rule; the mode left in force, the forest's tint under it, the
  targets without the attachment read where it is off, the self-test again at a new scene size;
  its log lines matched plain, sabotaged, under the fake Adreno, with each `ICO_RD_BLEND` and
  without a destination read (`rd_blend_selftest*`).
- `rd_crt_test.c`: the CRT filter's options and its shader (`rd_crt`, `rd_crt_fixed`).
- `rd_debug_test.c`: `debug.c` on the host, recorded (`rd_debug`).
- `rd_filter_test.c`: the model viewer's draw filter on synthetic keys (`rd_filter`).
- `rd_fog_selftest_test.c` with `rd_fog_selftest.cmake`: the fog's self-test and probe; its log line
  matched for each path, MSAA counts, odd widths and [Full pixel](ARCHITECTURE.md#terms)
  (`rd_fog_selftest*`).
- `rd_fog_test.c`: the depth fog of `fog_DrawFog` on the CPU and on the device, every depth path
  (`rd_fog`, `rd_fog_*`); at 2x a depth ramp's fog alpha equals the above-1x formula (the table
  interpolated, the ordered dither) texel for texel, and the 1x cases count no above-1x draw
  (`checkRamp`).
- `rd_gsbase_test.c`: `GsBase.c`'s host path driving a synthetic scene (`rd_gsbase`); photo mode's
  Deja vu draws the stage's sandstorm object once a frame with the vision's two nodes, gives back
  what it borrowed, adds no film grain, and draws nothing in stage 40, with the effect off or with
  photo mode closed (`checkDejaVeil`); its film reel on a row of `film_noise_l4.bga`'s shape: the
  first photo tick starts the row, each later tick runs it once on the filter's own frames, the
  game's frames and node records are the same after every tick and draw, the noise objects are
  drawn in model order before the veil with the evaluated nodes, their fade following the
  envelope's own frame tick by tick and wrapping at its length, nothing with the effect off, in
  stage 40 or with photo mode closed, the veil alone without the row (`checkDejaReel`; the
  evaluator there stands in for the game's).
- `rd_interp_test.c`: presentation between [ticks](ARCHITECTURE.md#terms) on two synthetic frames
  (`rd_interp`); its rotation cases live in `rd_interp_turn_test.inc`, included where they stood;
  rotation continuity (`testRotationContinuity`): a rigid draw and a bone turning
  170 then 190 degrees a tick stand at 265 degrees half way (on forward the long way, not back to
  85), at 217.5 at alpha 0.25 and at the tick's pose at alpha 1; 190 degrees with no turn before
  keeps the tick's pose; a 20 degree turn after another blends bit for bit as without one. Across
  a gap (`testRotationGap`): after a pair with no present, a camera cut or a discarded frame the
  wheel turning 200 degrees a tick continues the long way from the turn two ticks before, a turn
  8 ticks old serves and 9 does not, a pose the memory does not carry to keeps the tick's, a turn
  under the snap blends the short way. The trolley's wheels (`testRotationWheels`): two draws, of
  one key and of two, at 185 degrees a tick with their places in the list changing every tick,
  both forward the long way; the ride of `box.c` (0.5 a tick of push, 0.98 friction, radius 10)
  up to 125 degrees a tick blended every pair, then a camera cut, and each pair after it forward.
- `rd_layout_test.c`: the layout frame from the game's layout calls (`rd_layout`).
- `rd_mesh_test.c`: the mesh path from `Packet.c` to the VU shaders (`rd_mesh`, sharded); the
  content table's four sandstorm sheets and the tiles appended to them (`sheetTableChecks`), one
  sheet on the device at 4:3 byte for byte the same with and without the tiles and at 16:9 the
  columns beside it drawn (`sheetPixels`), and the `photo_veil` shard: the veil's two layers at
  60:9 drawing every pixel with the tiles (`photoVeilPixels`), and the `photo_reel` shard: the film
  reel's four specks of the deja dump at their places on the 16:9 frame, none tiled, every pixel
  inside a speck's own place and in the 4:3 picture's columns (`photoReelPixels`).
- `rd_mip_test.c`: the Enhanced mip chain's size and its builder staying inside it (`rd_mip`).
- `rd_mirror_test.c`: mirror mode's flag and the flipped presenter output (`rd_mirror`).
- `rd_passring_test.c`: the pass ring the device-loss report prints: order, wrap, the replay table, the
  replays printed (the last three, whole at 430 passes a replay), the line format and the loss
  bisect's line; pure (`rd_passring`).
- `rd_msaa_test.c`: the anti-aliasing's MSAA against the frame without it (`rd_msaa`).
- `rd_native_test.c`: the native frame's centre rule on a synthetic frame at five widths
  (`rd_native`).
- `rd_perf_test.c`: the renderer's CPU cost per replay and its steady state, and the start-up set
  through `rhi_create_pipelines`: its progress calls (from 0, never back, the total once at the
  end), a stop after the first batch and the rest made by the next call; the exact blend's
  counters (`RdPerfRecord.dstDraws`, `storagePasses`, `dstSwitches`) under each blend path the
  device has (none under the fixed-function blend, no storage pass or switch under the attachment
  read) and, under the storage read, the storage pass merge's DISPLAY bytes equal to the unmerged
  ones with no more passes or switches (`rd_perf`, `rd_perf_mali`).
- `rd_pixel_test.c`: rd through the Vulkan [RHI](ARCHITECTURE.md#terms), pixel by pixel; leaves the
  dump the tool tests replay (`rd_pixel`).
- `rd_present_test.c`: the display options through `GsBase.c` and the presenter (`rd_present`);
  the targets' scales include Water reflections' block: its GS size under Original, the scene's
  scale under Sharp (past the work scale at 4x), both the GS size at 1x (`checkScales`).
- `rd_raw_test.c`: the raw packet builders outside `seki` (`rd_raw`).
- `rd_replay_tool_list.cmake`: `--list --no-device` on `rd_pixel`'s dump (`rd_replay_tool_list`).
- `rd_replay_tool_pass_dump.cmake`: `--pass-dump`, `--target` and `--aspect` on that dump
  (`rd_replay_tool_pass_dump`).
- `rd_replay_tool_loss_bisect.cmake`: `--loss-bisect` on that dump runs clean on the device with one
  replay per pass and one to count them, `--passes 1-P` (the whole frame) draws the plain replay's
  picture and `--passes 1` stops the walk at pass 2; the second level's draw window: `--passes 1-P
  --draws 1-N` (every draw of the last pass) draws the plain picture, and one step with `--draws
  1-2` and one with `--mask-marks` run clean and name their last draw (`rd_replay_tool_loss_bisect`).
- `rd_replay_tool_pass_split.cmake`: that dump with `ICO_RD_PASS_SPLIT=8` draws the plain replay's
  DISPLAY picture byte for byte (`rd_replay_tool_pass_split`).
- `rd_replay_tool_pass_merge.cmake`: that dump under `ICO_RD_BLEND=storage` with the storage pass
  merge draws `ICO_RD_PASS_MERGE=0`'s DISPLAY picture byte for byte at 1x and 2x, with no more
  passes or pass switches; skips without the storage read, as on lavapipe, which has no
  interlock (`rd_replay_tool_pass_merge`).
- `rd_replay_tool_snap_swap.cmake`: that dump on each blend path the device has, at 1x and 2x:
  the staticBlur sprites' snapshot swap draws `ICO_RD_SNAP_SWAP=0`'s (the copy's) DISPLAY picture
  byte for byte, and its copies plus swaps are the copy's snapshots (`rd_replay_tool_snap_swap`).
- `rd_bisect_plan_test.c`: the loss bisect's second-level planner against oracles: a draw that
  faults with every draw before it (1, 731 and 1,312 of 1,312) found within 12 steps, the masked
  step's verdict, a killed step counted as lost, a step without a verdict planned again,
  contradicting prefixes ending the plan, 1,000 random oracles ending within 16 steps
  (`rd_bisect_plan`).
- `rd_depth_copy_dump.cmake`: a player dump with the fog replayed with the depth in place, copied
  and through a buffer, SCENE byte for byte the same (`rd_fog_depth_copy_dump`, `_d24s8`).
- `rd_shadow_test.c`: the shadow count on the stencil (`rd_shadow`); at scene scale 4 each
  composite against the CPU B-spline within 2 LSB and a row across the shadow falling and rising
  once, the softening's AA0 (work scale 2) the area box of SCENE and its composite back the
  B-spline (`checkComposite4x`, `checkSoftening4x`).
- `rd_state_test.c`: the recording half of rd without a GPU: list order, defaults, state leakage,
  and a dump's menu text sheets loading as the renderer makes them, a sheet with its halo made on
- `diag_test.c`: the crash report (also from a worker thread under a batch's pipeline note), assert, watchdog, progress (with the heartbeat's start-up set count), start-up activity file (the batch notes), device-loss restart and loss bisect (the loss noted in the activity file, the bisect stubbed: once, before the game, never again after a start ended inside it) cases, each in a child process; the symbolizer, the report file and `ico_log_limited` (above)
- `rd_tex_test.c`: the texture cache and `Texture.c` on synthetic TIM2 files, and a text texture
  that could not be uploaded named once in the log, the game's own textures never
  (`textUploadLogChecks`) (`rd_tex`).
- `rd_water_test.c`: the surfaces that render to and sample from textures (`rd_water`).
- `texpack_disc_test.c`: the PCSX2 names of every texture on the disc, and how many of a pack's
  files they reach (`texpack_disc`).
- `texpack_e2e_test.c`: the texture pack end to end on the real pieces (`texpack_e2e`).
- `texpack_load_test.c`: the DDS and PNG loaders on files built in memory; with a pack, its files
  (`texpack_load`, `texpack_pack_files`).
- `texpack_name_test.c`: XXH3, the swizzle and the names against PCSX2's rules (`texpack_name`).
- `texpack_test.c`: the pack's index and loader on fake files (`texpack`).
- Helpers: `mesh_env.h` (the mesh tests' game stubs), `vu_models.h` (synthetic models),
  `sheet_ref.c`/`.h` (the sheet-text shader on the CPU), `above1x_ref.c`/`.h` (the above-1x
  kernels and the fog's formula on the CPU, written from their definitions).

### port/rhi/test

- `rhi_vk_test.c`: the exact-texel RHI checks on Vulkan, a hazard cell, the render-area check (a
  pass larger than its attachments clamped with no validation error, `passAreaCell`), the
  pipeline cache file (`rhi_vk`, `rhi_vk_nodual`, `rhi_vk_d24s8`, `rhi_vk_mali`).
- `rhi_d3d12_test.c`: the same checks on D3D12, a Windows GUI program that writes a log and shows
  a message box (`rhi_d3d12`).
- `rhi_d3d12_plan_test.c`: the D3D12 backend's state and barrier bookkeeping on the CPU
  (`rhi_d3d12_plan`).
- `rhi_driver_version_test.c`: the driver version text of the adapter line
  (`port/rhi/rhi_driver_version.c`) for each vendor's packing (`rhi_driver_version`).
- `rhi_vk_enum_test.c`: every `rhi.h` enumeration has a Vulkan mapping (`rhi_vk_enum`).
- `rhi_vk_present_mode_test.c`: the present-mode choice over the sets drivers offer
  (`rhi_vk_present_mode`).
- `rhi_vk_swapchain_test.c`: the window path without a display, on SDL's offscreen driver
  (`rhi_vk_swapchain`).
- Helpers: `rhi_test_common.c`/`.h` (the shared cells, seven of whose pipelines come from one
  `rhi_create_pipelines` call with a failing description among them), `shaders/`
  (`rhi_test.hlsl`, its compiled headers and `gen_shaders.sh`), and `compare_backends.cmd` with
  `compare_png.ps1`, which render every dump on Vulkan and D3D12 on Windows and compare the PNGs
  within 1 LSB.

### port/shaders/test

- `shaders_table_test.c`: every embedded entry compiled for SPIR-V and signed DXIL
  (`shaders_table`).
- `gs_math_test.c`: `gs_math.hlsli` compiled as C against the [GS](ARCHITECTURE.md#terms) formulas,
  and its integer forms against the operators they replace ([SHADERS.md](SHADERS.md#integer-forms))
  (`gs_math`); `hlsl_shim.h` makes it compile.
- `shaders_pixel_test.c`: sprite, blit and blend shaders through the Vulkan RHI
  (`shaders_pixel`, `_nodual`, `_mali`); first, on the CPU, `common.hlsli`'s above-1x kernels
  compiled as C against `above1x_ref.c`: the Bayer matrix, the B-spline's weights, the box, the
  axes and the separable combine, and the fog's `gs_fog_channel` (`checkAbove1xKernels`).
- `vu1_test.c`: the [VU1](ARCHITECTURE.md#terms) program shaders against their CPU references and
  hand traces, the card verdicts handed over as the renderer does, and the CPU's card test against
  the reference (`vu1`, `vu1_mali`).

### port/test

- `layout_asserts.c`: the generated struct layout asserts, which hold at compile time
  (`layout_asserts`).
- `gs_blend_test.c` is a standalone CPU program on the GS blender's drift, not a ctest.
- `ico_check.h` and `ico_shard.h` are the framework above.

### port/ui/test

- `credits_headless.py`: Extras > Credits in the headless game (`credits_headless`).
- `font_coverage_test.c`: every character of the five languages has a glyph (`font_coverage`);
  `font_corpus/` holds the per-language text.
- `font_edge_test.c`: the port's plain text at the output's resolution on a device (`font_edge`).
- `gallery_sweep.py`: every stream and a sample of every bank in one headless run
  (`gallery_sweep`).
- `gallery_test.c`: the music gallery's list from the base ELF's tables (`gallery`).
- `menu_look_test.c`: the menus' Arimo text against the game's menu sheets from the disc
  (`menu_look`); `menu_sheets.c`/`.h` decode the sheets.
- `menu_text_test.c`: the game's menu words and the port's rows drawn in the sheets' look
  (`menu_text`).
- `settings_test.c`, `settings_extra_test.c`, `settings_extras_test.c`: the Settings menu over
  fake tables, CPU only (`settings`, `settings_extra`, `settings_extras`); `settings_test.c` also
  presses Right in photo mode through Deja vu and checks the popup's words (`ui__photo_film_word`);
  `settings_extra_test.c` steps Effects > Water reflections between Original and Sharp, checks its
  note, its key and the page's rows (`testWaterReflections`), and Effects > Glow size between
  Scaled and Original: the row after Glow, its note, its key and the page's fifteen rows
  (`testGlowSize`).
- `settings_photo_test.c`: the Settings screens through rd on a device, written as PNGs
  (`settings_render`).
- `ui_mouse_test.c`: the pointer's arithmetic in the menus (`ui_mouse`).
- `ui_test.c`: the embedded font, UTF-8, the runtime text; each language's own Deja vu string
  (`UI_STR_PHOTO_FILM_DEJA`) (`ui`).
- Helpers: `settings_fixture.c`/`.h`, `headless_common.py`.

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
`rd_replay_tool_pass_merge.cmake` and `rd_replay_tool_snap_swap.cmake` on the dump `rd_pixel` leaves behind, `rd_depth_copy_dump.cmake` on a player dump with the depth fog (`ICO_FOG_DUMP`), and
`rd_fog_selftest.cmake`, which matches the `fog: depth path` line of the self-test, and
`rd_blend_selftest.cmake`, which matches the `blend: ... (self-test: ...)` line of the blend's
(`port/render/CMakeLists.txt`). The ctest `rd_replay_tool` renders `rd_pixel`'s dump with
`--present 640x480`; it requires `rd_pixel` to have run first (`FIXTURES_REQUIRED rd_dump`).

## Tests that need the disc

Five cache variables point the tests at the player's files ([../BUILDING.md](../BUILDING.md#tests)
has their defaults; the registrations are in `port/*/CMakeLists.txt`):

- `ICO_DISC_IMAGE`, the PAL image. Without it `vfs_disc`, `archive_disc`, `appearance_disc`,
  `fmv_fields`, `gsb_cull_stage`, `texpack_disc`, `menu_look`, `menu_text`, `gallery`,
  `content_table_chains` and the headless game runs below skip; `sndn2`, `fmv`, `shadow_spawn` and
  `rd_mesh` run only their synthetic part, and `tables_loader` reads the base ELF instead.
- `ICO_BASE_ELF`, the PAL base ELF: with pyelftools, `tables_loader` and `tables_manifest` are
  built from it (`port/data/CMakeLists.txt`); `gallery` reads its tables.
- `ICO_TEXPACK_DIR`, a PCSX2 pack's replacements folder: `texpack_disc`, `texpack_e2e`,
  `texpack_pack_files`.
- `ICO_FOG_DUMP`, a player dump with the depth fog: `rd_fog_depth_copy_dump` and `_d24s8` (skipped
  without it).
- `ICO_BOOT_DUMP_DIR`, a start-up's frame dumps: `rd_boot_present`; `ICO_BOOT_DUMP_OLD` sets
  `ICO_BOOT_OLD=1` for dumps whose frames 118 to 120 draw stage 1 (`port/render/CMakeLists.txt`).

Three tests start the headless game itself, each in a fresh folder with its own `ico-pc.ini` and a
pad script, `RUN_SERIAL` and with a long timeout:
`model_viewer_headless` (`port/game/test/model_viewer_headless.cmake` with
`model_viewer_pad.txt`), `gallery_sweep` (`port/ui/test/gallery_sweep.py`, which plays through
`ICO_GALLERY_PLAY`) and `credits_headless` (`port/ui/test/credits_headless.py`, with
`unlock_credits=1`); the two scripts share `headless_common.py`, whose pad walks the boot to the
title and then Settings > Extras. A worktree configured with `-DICO_DISC_IMAGE=/nonexistent
-DICO_BASE_ELF=/nonexistent` cannot start the game in any of them (`tools/dev_build.sh`, header);
the ctest exclusion used for such runs, `-E
"^(model_viewer_headless|gallery_sweep|credits_headless|rd_replay_tool)$"`, also leaves out
`rd_replay_tool`, whose dump comes from `rd_pixel`.

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
| `ICO_VK_FAKE_LIMITS=moltenvk` | `vk_device.c` | MoltenVK: `RhiLimits.moltenvk` set, no attachment read, the interlock the device's own (`rd_blend_selftest_moltenvk`: the default is the fixed-function blend with the MoltenVK reason; `rd_blend_selftest_moltenvk_storage`: `ICO_RD_BLEND=storage` keeps the interlock. Run without `ICO_VK_FAKE_NO_INTERLOCK`; they skip on a device without the interlock, as Mesa before 25.2's lavapipe) |
| `ICO_VK_FAKE_ADRENO` | `vk_device.c` | Qualcomm's Adreno driver: the attachment read not offered (`rd_blend_selftest_adreno`) |
| `ICO_VK_ATTACHMENT_READ` | `vk_device.c` | 1 offers the attachment read under `ICO_VK_FAKE_ADRENO` too (`rd_blend_selftest_adreno_forced`) |
| `ICO_VK_VALIDATION` | `vk_device.c` | the validation layer; the render and RHI tests set it with `VK_ADD_LAYER_PATH` when the layer is in the toolchain |
| `ICO_VK_GLOBAL_BARRIERS` | `vk_cmd.c` | a global barrier before every pass instead of hazard tracking, for A/B runs |
| `ICO_RD_FOG_PATH` | `rd_fog_path.c` | `inplace`, `copy` or `buffer` forces the fog's depth path; `test` makes the self-test try every path |
| `ICO_RD_DEPTH_COPY` | `rd_fog_path.c` | 0 samples the depth in place, 1 copies it |
| `ICO_RD_FOG_SABOTAGE` | `rd_fog_path.c` | makes named paths fail the self-test (`rd_fog_selftest_fallback`) |
| `ICO_RD_BLEND_SABOTAGE` | `rd_blend_selftest.c` | makes named blend modes read white in the blend's self-test (`rd_blend_selftest_sabotage*`) |
| `ICO_RD_VU_PLAIN` | `rd_pipeline.c` | 1: every VU draw with the VU vertex shaders' plain variants, as on a device of the table (`kVuPlainOnly`); the replay tool's pictures at a wide aspect then show the plain-only picture ([RENDER.md](RENDER.md#the-vu-shader-variants)) |
| `ICO_RD_VU_EXTENDED` | `rd_pipeline.c` | 1: the extended variants whatever the device table or `ICO_RD_VU_PLAIN` says |
| `ICO_RHI_BACKEND`, `ICO_VK_DEVICE`, `ICO_D3D12_ADAPTER` | `rhi_backend.c`, `vk_device.c`, `d3d12_device.c` | pick the backend, the Vulkan device, the D3D12 adapter (`warp`) |
| `ICO_FIXED_CLOCK` | `clock.c` | the wall clock fixed at 2002-01-01 unless `0` |
| `ICO_GALLERY_PLAY` | `gallery.c` | a play script for the music gallery |
| `ICO_UI_POPUP_TEST` | `ui_host.c` | the test popup at [Main tick](ARCHITECTURE.md#terms) 100 and every 150 ticks after (`[dev] popup_test`) |
| `ICO_START_STAGE` | `ico2/common/src/debug.c` (`debug_TryToGetStartStage`), `gamestate.c` | `[dev] start_stage` handed over: a stage 2 to 105 to start in, which suspends achievements |
| `ICO_TTY` | `kernel_host.c` | the game's `scePrintf` to stdout |
| `ICO_FMV_DECODE` | `movie.c` | 0 skips decoding in any build; the timing is unchanged |
| `ICO_RD_DUMP_EVERY`, `_DIR`, `_INTERP`, `_FROM` | `rd_core.c` | the frame dumps `ico-pc.ini` hands to rd |
| `ICO_VU_REF_AT` | `rd_replay_tool.c` | the kicked triangles containing a GS point |
| `ICO_BOOT_DUMPS`, `ICO_BOOT_OLD` | `rd_boot_present_test.c` | the start-up dumps (the folder when no argument is given), and `ICO_BOOT_OLD=1` for dumps whose frames 118 to 120 draw stage 1 |
| `ICO_DUMPS`, `ICO_SWEEPS`, `ICO_COVERAGE_DIR`, `ICO_REPLAY_LOCK` | the gate tools | the dump list, the aspect sweeps, the coverage state, the lock |

`grep -rn 'getenv("' port/` and `grep -a -rn 'getenv("' ico2/` list the rest (the game sources
read `ICO_START_STAGE`, `ICO_SWITCH_TO`, `ICO_SWITCH_AT` and `ICO_HEAP_ASAN_FREE`).

## F12 dumps and the replay tool

In the [window build](ARCHITECTURE.md#terms) F12 writes an `.rddump` of the last closed frame, a PNG
of the whole DISPLAY frame and a PNG of the picture as [presented](ARCHITECTURE.md#terms) into the
[user folder](ARCHITECTURE.md#terms)'s `dumps/`, then puts the dump and a copy of the log into
`frame-<time>-v<vsync>.zip` and deletes the loose dump; the zip exists because issue trackers refuse
`.rddump` attachments (`port/platform/hotkeys.h`, `ico_frame_dump_paths`, `ico_frame_present_path`;
`port/platform/dump_zip.h`, `ico_frame_dump_zip`). F11 makes the stats lines come every second for
30 s (`port/platform/hotkeys.h`, header comment).

Shift+F12 in photo mode is the aspect sweep: the paused frame re-rendered at 4:3, 16:9, 32:9, 48:9
and 60:9 into `sweep-<time>.zip`, holding `<aspect>.rddump` and `<aspect>.png` for each with the
4:3 member first, so the replay tool, which takes a zip's first `.rddump`, replays it
(`port/platform/dump_zip.h`, `ico_sweep_zip`, `ico_sweep_aspect_name`). The aspect tokens are the
ones `native_frame_check.py gate-b` reads.
`[dev] dump_every = N` writes every Nth frame ([PLATFORM.md](PLATFORM.md#hotkeys-and-dump-zips)).

The dump format is `port/render/rd_dump.c`'s header comment, and the replay tool's options are
listed in [RENDER.md](RENDER.md#dumps-and-the-replay-tool). For tests the useful ones are
`--pass-dump DIR` (every named target from one run), `--list --no-device` (the commands without a
GPU), `--present WxH`, `--aspect`, `--enhanced`, `--aa` and `--interp T PREV`
(`port/render/tools/rd_replay_tool.c`, usage).

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
`gameplay.yorda_safe`, `gameplay.developer_mode`, `dev.start_stage`, `gameplay.door_fix` and
`video.anti_aliasing`, and the native frame's width and aspect (`port/platform/main_host.c`,
`record_keys`, `record_header`; `ico_input_record_frame_lines`).

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
each refuses is in [GAME_CODE.md](GAME_CODE.md#audits). Other tests check generated files and
tables: `layout_asserts` (a program compiled from the generated asserts), `layout_asserts_fresh`,
`font_audit` and `content_table_chains` (`port/test/CMakeLists.txt`), `data_desc_fresh` and
`tables_manifest` (`port/data/CMakeLists.txt`), and the package staging (`package_textures`,
[PACKS.md](PACKS.md#tests)).

`docs_check` runs `tools/check_docs.py` over README.md, `docs/*.md` and `docs/port/*.md`: relative
links and their anchors, history and package-code wording, issue numbers and internal words in the
player guides, TODOs, and the package's list of player guides; `tools/check_docs_allow.txt` holds
the allowed exceptions (top-level `CMakeLists.txt`, `docs_check`; `tools/check_docs.py`,
docstring). `python3 -I tools/check_docs.py` runs it without a build.

`spirv_check` runs `tools/check_spirv.py --sizes` over the SPIR-V modules the build compiled
(`<build>/shaders/*.spv`): a vertex shader has no vector of booleans, no `any()`/`all()` and no
boolean select, and uses only the opcodes of the envelope's set (a loop, `dot()`, a
logical-not-equal or an undefined value named), every module stays within its stage's size budget
in words, no module carries an array as a value (a whole-array load, store, construct, insert,
phi, select or copy) and no vertex shader keeps a local array
([SHADERS.md](SHADERS.md#the-envelope)); the log lists each module's words, instructions, blocks,
phis, loops, dot products, vector comparisons, boolean selects and `any()`. `python3 -I tools/check_spirv.py --sizes <build>/shaders` runs it by
hand.

## Which tests a change needs

- A new Options row: `ctest -L ui`. `settings` builds every page and compares its rows with fixed
  lists (`port/ui/test/settings_test.c`, `testBuild`), so a new row updates those lists; new
  settings cases go in `port/ui/test/settings_extra_test.c` (its header). `settings_render` draws
  every screen, `menu_text` the rows in the sheets' look, and `font_audit` (every code point inside the
  font subset, `tools/font_audit.py`) and `font_coverage` (a glyph for each) check the row's text
  in the five languages. A gameplay option also needs `options`
  (`port/game/test/options_test.c`).
- A new config key: `config` (the reader, writer and precedence, `port/config/test/config_test.c`),
  `host_config` when `ico-pc.ini` maps it (`port/platform/host_config.c`, `ini_map`), `settings`
  when a row writes it, and the module's own test (`options`, `photo`, `appearance`, `input`).
- A new texture-pack format: `texpack_load` (each loader on files built in memory,
  `port/render/test/texpack_load_test.c`), `texpack`, `texpack_e2e`, and with `ICO_TEXPACK_DIR`
  `texpack_pack_files`; `package_textures` when the shipped README changes
  ([PACKS.md](PACKS.md#tests)).
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
  no local array in a vertex shader). A change to a VU vertex shader or to the card test
  ([VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-card-test)): `vu1`, `vu1_mali`, `rd_native` and the frame
  gates ([Render gates](#render-gates)), whose pictures must not change.
- A new achievement: `achievements`, which fails unless one of its runs unlocks every achievement
  and each has a title and a description in every language; its count check
  (`ico_ach_count() >= 20 && ico_ach_count() <= 30`) bounds the table, which holds 30
  (`port/game/test/achievements_test.c`; `port/game/achievements.c`, `s_defs`). `font_coverage`
  checks the new strings' glyphs.
- A change to these documents: `docs_check`.

## CI

| workflow | job | runner | what it runs |
| --- | --- | --- | --- |
| `ci.yml` | `linux` | `ubuntu-24.04` | the hygiene checks (no disc data, format, no `ICO_HOST` conditional, stack-layout, struct-cast and call-type audits, generated files fresh), then `linux-x64` headless and window builds with `ctest`, `linux-x64-clang` with `ctest`, and the `win-x64` cross build (tests built, not run) |
| `ci.yml` | `extra` | `ubuntu-24.04` | a matrix of `asan` and `fptrap` (build `all tests`, `ctest`) and the `win-x64-clang` cross build |
| `ci.yml` | `linux-arm64` | `ubuntu-24.04-arm` | the no-disc-data check, the `linux-arm64` preset with `ctest`, the filtered-pixel render tests in a step that does not gate, the arm64 package on release tags |
| `ci.yml` | `android` | `ubuntu-24.04` | the debug APK (`assembleDebug`), `tools/check_android_flags.sh`, the APK structure checks |
| `ios.yml` | `build`, `publish` | `macos-15`, `ubuntu-24.04` | the unsigned iPhone IPA for a published release, attached to it |
| `ios.yml` | `build-macos`, `publish-macos` | `macos-15`, `ubuntu-24.04` | the macOS app for a published release, attached to it |

(`.github/workflows/ci.yml`, jobs `linux`, `extra`, `linux-arm64`, `android`;
`.github/workflows/ios.yml`.)

The triggers, steps and caches are [../BUILDING.md](../BUILDING.md#continuous-integration)'s.

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
program's. Variants (`_nodual`, `_mali`, `_minlimits`, `_storage`, `_fixed`, `_d24s8`, `_static`) rerun a
program under an environment switch ([Environment switches](#environment-switches)). `_static`
(`rd_pixel_static`, `rd_mesh_static.1of4`: `ICO_VK_FAKE_NO_DYNAMIC=1`) draws with every pipeline
static, so the start-up set's membership checks (`rd__pipe_key_equal`) compare whole keys, as on
D3D12 and a Vulkan device without the dynamic state; `rd_mesh_static.1of4` is `rd_mesh`'s shard 1 of
4, the one with the `gpu_prims` case.

### port/audio/test

- `adpcm_slots_test.c`: the ADPCM request daemon with both stream slots taken; a stream already
  fading out gives its slot up (`adpcm_slots`).
- `adpcm_test.c`: SPU ADPCM block decoding against hand-worked vectors (`adpcm`).
- `audio_pull_test.c` (window build): the output on the device's clock (`out_sdl.c`'s pull path)
  on a device-less SDL stream over the real SPU2: a writer thread under the audio lock and a
  pulling thread render the frames one thread renders; 10 s free-running with the frames and the
  silence accounted; the ramp out of and into silence while the lock is held elsewhere; the 10 s
  line and the mode choice; the bound on the SPU2's lead over the game (2 s of pulls with the
  game's frames frozen render exactly 120 ms, then a ramp and silence counted as held back; the
  game stepping releases the hold 40 ms under the bound, the trim plays the stream slower while
  over 64 ms and stops under 32) (`audio_pull`). Its checks are counts, fences and samples, never wall
  time, so a loaded machine or `ctest -j` only makes it slower (the free-running part raises the
  lock's wait bound to 500 ms, the one wall-clock bound); a failing check prints its values and
  each part prints a line of its counts. It is the program to run under ThreadSanitizer, with
  `audio_queue_test`: the `asan` preset with `-DICO_HEADLESS=OFF -DICO_SANITIZE=thread` into a
  build folder of its own, `--target audio_pull_test audio_queue_test`, each run with
  `TSAN_OPTIONS=halt_on_error=1` under `setarch x86_64 -R`, which turns address-space
  randomization off for the run (the build host's kernel randomizes more than ThreadSanitizer
  accepts); a report is a failure.
- `audio_queue_test.c` (window build): the output on the game's clock (`out_sdl.c`'s push) on a
  device-less SDL stream: the cushion, the refill after a dry queue and its ramp with frames still
  queued, plain silence after a queue run empty, the trim and the drop limit (`audio_queue`).
- `libsd_irx_test.c`: the LIBSD.IRX reader on a synthetic module, its structural checks and its
  fallback (`libsd_irx`).
- `mix_gain_test.c`: the music, effects and film gains, every fixed volume word unchanged at 100 %
  (`mix_gain`).
- `pan_test.c`: mirror mode's pan swap and the per-[vsync](ARCHITECTURE.md#terms) block size
  (`audio_pan`).
- `sndn2_test.c`: the SNDN2DRV host and the Sg sequencer against what the IRX's code does; reads
  the disc when present; also `sw_log`'s bound (the longest page-full line arrives whole, a longer one ends in `...`), and the audio lock's sites with a counting fake lock (`sndn2`).
- `spu2_bench.c`: the [SPU2](ARCHITECTURE.md#terms) render's cost, and with `--check` a CRC-32 of
  five 10 s scenes (`spu2_render_crc`).
- `spu2_test.c`: the software SPU2 and its [libsd](ARCHITECTURE.md#terms) front end: interpolation,
  ADSR, voices, the voices cut short, reverb (`spu2`).
- `volume_test.c`: the output volume's mapping, clamp and integer scaling (`volume`).

### port/config/test, port/null/test, port/save/test

- `config_test.c`: `config.toml`'s reader and writer, round trips, the ini over `config.toml` over
  default precedence, the first-run template (Hold type `hold`, `effect_motion_blur_mode =
  "original"`, the touch comments naming `format = 2`, the 0.7 to 3.0 size and the words `hidden`
  and `off`; `touch.format` and a `[touch.full]` line written and read back), the language answer, `[game] disc` (`auto`, the slugs, an unknown value as
  auto logged once a load) and `[game] last_disc` (read, written, not in the template), the wall
  clock and the [EE](ARCHITECTURE.md#terms) timers (`config`).
- `null_devices_test.c`: the pad as an empty console answers it (`null_devices`).
- `save_test.c`: the host memory card through the calls `mcard.c` makes, then `mcard.c` and
  `mcdata.c` themselves on the [fiber](ARCHITECTURE.md#terms) scheduler, then the card importer,
  with the disc its first argument selects: the European disc's `BESCES-50760ico` and 0x1F0-byte
  product record (`save`), the Japanese disc's `BISCPS-11003ico` and 0x1E8-byte record, the same
  bytes up to 0x1E8 (`save_jpn`), the Korean disc's `BISCPS-56001ico` with the same 0x1E8 record
  (`save_kor`), the Asian disc's `BISCPS-55001ico` with the same record (`save_cn`), the North
  American disc's `BASCUS-97113ico` and the August 2001 preview's (the same directory, in its own
  card folder `<card>-preview`: every row's `mc_card_suffix` and `ico_host_card_dir_beside`
  checked, and the import files the directory as the North American disc's) with their
  own 0x18C-byte record and a 25,596-byte game block with no option words (`save_ntsc`,
  `save_aug6`), and the June 2001 demo's `BISCUS-97113ico` with the preview's record, a 20,480-byte
  game block and icon files that end in a 4-byte sum and are written only when absent
  (`save_jun26`); each imports a card holding its directory and one other disc's side by side and
  the other disc's `.psu` beside their own ([SAVES.md](SAVES.md#tests)). Part B lays each disc's record out byte by byte
  and pins every row's `mc_save_dir` to the game's own `iOSMcSaveSeg_*` name
  ([SAVES.md](SAVES.md#the-seven-discs)). A load reads `optionControlType` from `[gameplay]
  hold_type`, not from the card's `controlType`; on the three 2001 discs the load does not touch it,
  nor `optionScreenMode`, `girlControlMode` or the record's `fileNo` and `serial`, which the card
  does not hold.

### port/core/test

- `region_select_test.c`: the game region (`port/core/region.h`): each of the three slugs selects its row and
  `ico_region` follows, an unknown slug or none keeps the selection; the disc ids, the per-disc
  config keys and the stage rows on each disc; `ico_region_info[]` against `config/regions.txt`
  (the program's argument) row for row, and the disc table `ico_discs` (`port/data/discs.c`,
  compiled in) against the same rows: slug, disc id, boot file, product name, archive and image
  names; every value of the Japanese row, the Korean row's resolved ids and fields, every field of
  the North American, August 2001 and June 2001 rows (pinned), and `ico_region_stage` on the 2001
  discs (the demo's own order by stage name) (`region_select`).

### port/data/test

- `archive_test.c`: the archive backend and the first-run extractor, on a synthetic image (the
  refusal of an image that is none of the discs with the one wrong-disc text) and on each disc: the
  identification by `SYSTEM.CNF`'s boot file, both acceptance rules, `meta.json`'s disc id
  (`archive_synthetic`, `archive_disc`, `archive_disc_jpn`, `archive_disc_kor`), and for a row with
  a DATA.DF shift that the entry's size equals the row's `df_size`, the header's count its
  `df_count` and the raw directory's sector plus the shift the entry's; `manifest-check` compares
  the Japanese and Korean DATA.DF manifests in `discs.c` with the image (`archive_manifest_jpn`,
  `archive_manifest_kor`); `shift <dir>` builds a synthetic image whose DATA.DF entry lists the
  Korean shift and checks the ISO9660 look-up (sector plus 153,600, size minus the shift in bytes,
  refusal when the directory is not larger), with no disc (`archive_shift`); `synth` also reads a
  second archive mount (`ico_vfs_reopen`) interleaved with the first.
- `make_raw_cd.py`, `disc_raw_test.cmake`: a MODE2/2352 `.bin` and its `.cue` made from the
  Japanese `.iso` into the build folder at test time (zero EDC/ECC, which the reader ignores), and
  for the `.chd` test a `chdman createcd` of the `.cue` (skipped without `chdman` on `PATH`), each
  checked by `vfs_test same` against the `.iso` (`vfs_disc_jpn_bin`, `vfs_disc_jpn_cue`,
  `vfs_disc_jpn_chd`).
- `df_index_test.c`: `df_pack.h`'s `IcoDfIndex` over a second mount of the disc against the global
  index: the directory, a loose file's bytes, every member's name and row, a member's bytes,
  `ico_df_index_read_pack` against one read per member and its stop, the global index untouched
  (`df_index_disc`).
- `iso_import_test.c`: the Android first start's copy of the chosen image over SDL memory streams
  (`iso_import`).
- `json_test.c`: `port/data/json.h`: numbers, strings, nesting, the depth limit, malformed input
  (`json`).
- `tables_test.c`: the runtime table loader against the PS2 build's tables generated from the base
  ELF (`tables_loader`, with `ICO_BASE_ELF`); `--set jpn` the Japanese descriptor set against the
  Japanese reference, `--set kor` the Korean one, and each disc's ELF refused by the other sets
  (`tables_loader_jpn` with `ICO_BASE_ELF_JPN`, `tables_loader_kor` with `ICO_BASE_ELF_KOR`, skipped
  without them).
- The Asian disc's tests (`data_desc_fresh_cn`, `ee_layout_fresh_cn`, `tables_loader_cn`,
  `tables_manifest_cn`, `archive_disc_cn`, `archive_manifest_cn`, `vfs_disc_cn`) are the Korean
  disc's with `ICO_BASE_ELF_CN` (`baserom/cn/baseelf.elf`) and `ICO_DISC_IMAGE_CN`
  (`baserom/Ico_CN.iso`), each skipped (77) without its file; `tables_test.c` holds the set
  against the reference `gen_data_c.py --version cn` writes (`ref_sets[]`), `archive_test.c` the
  row (`SCPS-55001`, `SCPS_550.01`, 183 members, DATA.DF 153,600 sectors in, a DVD) and
  `region_select_test.c` the row's selectors (`ICO_KORCN`, `PAL_JPNKORCN`, LN's seventh
  argument), its stage rows and every value of `region_cn.c`.
- `vfs_test.c`: the [VFS](ARCHITECTURE.md#terms) and its ISO9660 backend, the
  [libcdvd](ARCHITECTURE.md#terms), [SIF](ARCHITECTURE.md#terms) and [IOP](ARCHITECTURE.md#terms)
  RAM layers, and `ico_vfs_reopen`'s second mount read interleaved with the first
  (`vfs_synthetic`, `vfs_disc`, `vfs_disc_jpn`, `vfs_disc_kor`); `same <iso> <image>`: the same
  directory records, boot ELF and DATA.DF SHA-1s and image SHA-1 in both.
- `sha1.h` is the tests' SHA-1.

### port/fmv/test

- `fmv_fields_test.c`: with the disc, every `.pss` demuxed whole and its pictures' field flags and
  cadence counted (`fmv_fields`).
- `fmv_test.c`: the player's parts without the game: a synthetic PSS, the demuxer, the decoder
  (`fmv`).
- `rd_video_test.c`: `rd_video.c` on [rd](ARCHITECTURE.md#terms)'s headless device, pictures in and
  the present target read back (`rd_video`, `rd_video_mali`).
- `rd_video_window_test.c`: the renderer's output following the swapchain on SDL's offscreen
  driver when the window is resized (`rd_video_window`); the same on an OpenGL window
  (`rd_video_window_gl`: `ICO_RHI_BACKEND=opengl`, the window made with `SDL_WINDOW_OPENGL`,
  llvmpipe; the Vulkan backend's forced out-of-date result and swapchain counts left out, since
  OpenGL's present follows the window's size). Another backend exits 77.

### port/game/test

- `achievements_test.c`: the game-state view over a synthetic snapshot, every achievement's
  condition, suspension, `achievements.toml` round trips, the popup gap (125 Main ticks at 25 Hz,
  150 at 30 Hz) and every disc's keys in one file (`stats.stages` / `stats.stages_jpn` / `stats.stages_kor`,
  `run.slot_N_*` / `run.jpn_slot_N_*` / `run.kor_slot_N_*`, one set of unlocks), with the disc its
  second argument selects (`achievements`, `achievements_jpn`, `achievements_kor`).
- `appearance_disc_test.c`: the characters' colours against the disc's CLUTs (`appearance_disc`).
- `appearance_test.c`: the recolour on synthetic CLUTs: identity, coverage, swatches, randomize
  (`appearance`).
- `credits_test.c`: `staffroll.c` over a short table with the port credit appended, and the
  credits row's lock (`credits`); with `jpn` or `kor`, the same on that disc's region: STAFF1 58,
  the song 47, the scene layout 52, the roll offset 151,050 ms (the Korean build is the Japanese
  program), the roll start's log line, which names the disc (`credits_jpn`, `credits_kor`).
- `door_grace_test.c`: the doorway fix's arm and grace and a copy of the exit check's guard
  (`door_grace`).
- `variants_test.c`: the game variants (`port/game/variants.h`) on the CPU: every default is the game as
  shipped (the barrier's number the disc's: 5, 7 on `jun26`), every `[variants]` key steps through
  all its values, saves and reads back, a word or number that is no value reads as the default, the
  setters clamp and the steps wrap, and the achievements rule (One hit per swing, Shadows never
  dodge, Cutscene cameras ignored and a Debug menu press suspend; toughness, aggression, No hints,
  the barrier and the look-around do not; a reload clears the Debug menu's mark), and the game
  hooks: the bound debug switches (untouched at the shipped values, each variant's value while it
  is on, the aggression's four battle types, the game's value put back when it is switched off or
  reloaded, written at the bind from a file, nothing written unbound), the Debug menu's opener
  (bound: opened, 1; unbound: 0), `ico_hints_on` per disc, the enemy row reader on
  `ico2/sugipon/src/enemyKind.c` (compiled in: the disc's own rows with nothing chosen, American on
  the European table = the USA life with the European dodge, model and parameter row, "no model"
  rows whole, aggression's battle type on every row, never past a table: the June 2001 demo's 18
  rows, never mapped by index), the restart life's playthrough index (a copy of
  `getEnemyRestartLife`'s: 38 with American or Not tougher later) and `test_offered` (American is
  not offered on `jun26`: the config, the setter and both step directions); one run per disc (`variants`, `variants_jpn`, `variants_kor`,
  `variants_<slug>`).
- `hand_ik_test.c`: the hand records as `ACTGame_ConnectHand` sets them through the real
  `handManager.c`: every target finite, a NaN into `GetTableArcCos` giving 0 and a counted fault
  (`hand_ik`).
- `ico_frame_test.c`: the [native frame](ARCHITECTURE.md#terms)'s widths for each aspect and the
  options' frame (`ico_frame`).
- `kanban_boot_test.c`: the boot's card check and signs (`kanbanBoot.c`) for a first and a later
  boot (`kanban_boot`); with `jpn` or `kor`, the Japanese program's check (`kanbanBootMcCheck_jpn`,
  which the Korean disc runs too): no language or video step, no read of the card's system file,
  the language word 0 and 60 Hz throughout (`kanban_boot_jpn`, `kanban_boot_kor`). Each run also
  boots with no card and with a full one: the card warning's item that checks again brings it back,
  the item that starts without a card (European 41, Japanese 11, Korean 5) lets the boot go on.
  The North American disc and the August 2001 preview (`kanban_boot_ntsc`, `kanban_boot_aug6`)
  run that check without its retries (4 card checks over the warning's two answers with no card
  as with a full one, not 22) and with the Korean disc's item 5 to start. The June 2001 demo
  (`kanban_boot_jun26`) has no card check in its boot (`kanbanBootMain_jun26`): the test pins one
  presents sign in the first tick, no card check or warning with any card, the frame buffer let go of
  in tick 1, the 90-tick wait, step 5's wait for stage 1's load and the end two ticks after, with
  and without Skip the opening.
- `model_viewer_cam_test.c`: the viewer's camera step without the game (`model_viewer_cam`).
- `model_viewer_test.c`: the viewer's table against its motion-kind blocks and the game's table
  sizes, its names in five languages (`model_viewer`); its Japanese model column is checked by
  `tools/gen_mv_region.py --check` against the PAL ELF's and the Japanese ELF's `modelData`
  (`model_viewer_jpn`) and, since the Korean disc reads the same column, the Korean ELF's
  (`model_viewer_kor`).
- `model_viewer_effects_test.c`: a motion's effects with no stage animation loaded
  (`frameDependSequence.c` and `stageMultiBgaManager.c` compiled in over stubs): the stage
  animation entries' guard (no write, the slot kept, its log line once), and every motion of every
  viewer model stepped through `ExecFrameDependSequence` in the game (each stage animation skipped)
  and in the viewer (no stage animation or particle, the game's sounds and vibration); synthetic
  tables (`model_viewer_effects`) and the PAL base ELF's, where BOY SWIM reaches the guard
  (`model_viewer_effects_elf`, 77 without the ELF). `testPlayable`: every region's motion count
  inside `MV_MOTION_TABLE_SIZE`, motion 674 left out until its partner 669 is loaded, and over
  every viewer model a node-blend motion offered exactly when its partners are loaded, with the
  dynamic pool empty and again with every motion loaded (some offered then).
- `model_viewer_headless.cmake` with `model_viewer_pad.txt`: the headless game driven from boot to
  the first model, an animation and back (`model_viewer_headless`).
- `options_test.c`: the gameplay options' defaults and setters (Hold type: `hold` by default, `toggle`
  from the file, an unknown word as `hold`), the video effects' keys (Water
  reflections' `effect_water_sharp` off by default, saved, outside the preset; Motion blur's
  `effect_motion_blur_mode`: `"original"` with no key, `"original"` and `"smooth"` read in any
  case, an unknown word as `"original"`, Smooth only with the switch on for the getter, kept
  under Off, saved and read back, outside the preset; a leftover
  `effect_glow_size` read by nothing and not written), `[ui] language` (auto by default and for
  an unknown word, each name its language), `eBrainGetTarget` with Yorda safe off and on, and the
  `[mirror]` keys of the disc its second argument selects (`slot_N`, `jpn_slot_N`, `kor_slot_N`),
  the other discs' left as they were (`options`, `options_jpn`, `options_kor`).
  The same runs exist for the other four discs (`options_ntsc`, `options_aug6`, `options_jun26`,
  `options_cn`), and likewise `variants_<slug>`, `achievements_<slug>` and `credits_<slug>` for
  `ntsc`, `aug6`, `jun26` and `cn`. `credits_test.c` holds each of the seven discs' STAFF1, scene
  layout, roll offset, song, exit, boy record and whether DATA.DF holds the staff scenes' packs as
  literals (`test_region`), checks the boy at the disc's own entrance, and on the June 2001 demo
  (no packs) checks the start is refused with nothing kept, switched or moved
  (`test_engine_refused`); `achievements_test.c` drives each disc's achievement stages, queen flag
  and beach-secret flag from its region row (on the three 2001 discs, which have no beach secret,
  it checks the European flag 354 does not unlock it and the rest of the list does unlock);
  `achievements_test.c`
  holds each disc's retail addresses, peek-row count and story-flag and preview sizes
  (`kPeekWant`, checked against `gamestate.c`'s tables, which are written from
  `config/symbol_addrs.<slug>.data.txt`: the North American, August 2001 and June 2001 discs have
  `gFlag` and no `gFlagGameClear`, so 12 rows). `kanban_boot_cn` runs the Japanese program's check
  on the Asian disc; `kanban_boot_ntsc`, `kanban_boot_aug6` and `kanban_boot_jun26` run the
  three 2001 discs' boots (above).
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
- `input_test.c`: the `[touch]` tables onto the layout edits and back (the `format` key, a file without the key reading `hidden` and `o0` as Off, rewritten as `off`), bindings from config text, dead zone, quantisation, the stick fix, merging, the
  [libpad](ARCHITECTURE.md#terms) buffer, rumble, Escape (`input`).
- `mouse_camera_test.c`: the mouse camera's speed and range for the hand camera (`mouse_camera`).
- `pad_script_test.c`: the `--pad-script` reader and what the null pad then shows the game's pad
  code (`pad_script`).
- `touch_test.c`: the touch overlay's layouts over a table of screens (`test/touch_shapes.h`), the
  sticks and buttons, every default layout's alpha 1 and `editGen` 0, an Off L2 pressing nothing,
  and `test_hidden_off`: a Hidden Start pressing Start and an Off one nothing, a Hidden stick and
  look pad starting and Off ones not, `ico_touch_takes`, `ico_touch_ctl_state`, and the menus taking
  taps only with the D-pad, Cross or Circle Off; `test_menu_tap_areas` on a 2340 x 1080 phone at
  420 dpi: with the disc's confirm (Cross, or Circle) Off a point in the look pad's area and one in
  the stick's area are taken by the mapper but do not block the menus' click, the stick's does
  while the confirm works (the D-pad Off), and a working or Hidden button always blocks (`touch`).
- `touch_edit_test.c`: the layout edits and the editor ([INPUT.md](INPUT.md#layout-edits)): an
  unedited layout equal to the default to the byte, the anchor arithmetic, the clamp (a camera
  hole's negative offsets in `test_cutout`; the region's clamp for offsets at or inside the safe
  area on four inset screens in `test_v084_forms`), Hidden and Off
  controls and the rails, the config text round trip with `hidden` and `off`, the older files'
  `hidden` and `o0` read as Off (`test_format_migration`), the editor over synthetic fingers and
  gamepad words (the opacity steps through Hidden and Off, the overlap and refuse rules counting
  Hidden and skipping Off: `test_hidden_off_editor`), sizes up to 300 % on screen in the three
  postures on a phone and a 3000 x 2000 tablet, and on the phone folded SPLIT with a 160-pixel
  inset on the left or the right edge a 300 % D-pad, stick, Cross or Select anchored on either
  side kept on its own side of the fold, the D-pad and stick marked `fitted` (`test_size_300`),
  and the toolbar clear of every
  default control (`touch_edit`).

### port/math/test

- `math_test.c`: the PS2 float helpers bit for bit, the R register, matrices, vectors and
  quaternions against references (`math`).
- `newlib_test.c`: `ico_rand`, `ico_qsort`, libm values in both FP environments (`newlib`).
- `softdouble_test.c`: `softdouble.c` against the EE's own soft float from `sce/libgcc`
  (`softdouble`); `dpbit_harness.cmake` builds that oracle on the host.

### port/platform/test

- `android_paths_test.c`: the Android file layout on any platform, the texture pack folder of
  each disc's serial, the `Ico_PAL`/`Ico_JPN`/`Ico_KOR` image names and the `drivers` folder (`android_paths`).
- `arena_test.c`: the EE RAM [arena](ARCHITECTURE.md#terms): alignment, zero fill, address mapping,
  heap statistics (`arena`).
- `diag_test.c`: the crash report (also from a worker thread under a batch's pipeline note), assert, watchdog, progress (with the heartbeat's start-up set count), start-up activity file (the batch notes; a start ended inside a 2D batch: the next start is lazy for that start only, nothing written, and logs the line and the last finished batch with its memory; that lazy start ended inside a pipeline: the start after writes `[dev] pipeline_set = "lazy"` through a stubbed config write, for any version (another version's line then leaves the policy to the count), with a failed write (this start only, its lazy run saying so, and the count kept so the next end writes it); a count file that cannot be written, logged; the "for this start only" run's clean exit and its end between two creations, each with its own line: the next start tries the set again and the row starts over; not after a restart or with the setting already `lazy`; an end in the background, by the lifecycle's event or `onTrimMemory(20)` (the mark set and logged once when both come), or with the app in the background before the recording began, is not counted, its mark written, logged and deleted, and back in the foreground the mark is gone and the end counts; a lazy run ended in the background between two creations, its mark logged; on a device the table makes lazy the first end promises nothing and its lazy run is not "for this start only", the second writes nothing, both clearing the row; the first end of a full set: the next start is careful, once, and the file reads `set: careful` after the note; a careful start that finishes clears the row and the start after it is full again; a careful start's own end makes the next start lazy; `pipeline_set = "careful"` and a careful end; the careful set's budget stop (`creating pipeline fog test` and `set: careful` in the file; its clean exit is "for that start only", then `pipeline_set = "careful"` is kept); the signals that stop the set (`actlowstop`: not `onTrimMemory` 5, not 10 before the set's first batch, 10 past it, 15 over 10) and the memory text with the device's; the set stopped at the system's low-memory signal (`set: stopped at the system's low-memory signal (running critical)` after `set: careful` in the file through the lazy run's clean exit, the batches' and the system's lines kept, the row cleared), the next start of the version lazy from it (pipeline set 7, nothing written, its own file keeping the line), that start's end inside a first-use pipeline stepping to lazy for one start (pipeline set 4), the stopped run's end between two creations (lazy from the file) and inside one (lazy for one start), another version trying the set again; a note of the retired extended variants counted as the pipelines' kind; a VU vertex pre-pass batch's end (a `vu_main_vs` batch among them): no pre-pass for that start only, the next such end writes `[dev] vu_prepass = false`, a run that built its pipelines and finished clears the row, the low-memory signal's line at once and as the file's `system: ` line, in the set and in a lazy run, the level-80 one marking the background) cases, Android's start after a device loss (`actlossnext`: robust access for that start, the second loss in a row writing `[dev] gpu_robust = true`, a loss in the robust start itself counting as the second, a start without a loss that did not run robust breaking the row, a failed write keeping the count, the player's `gpu_robust` ruling, a relaunch counting nothing), device-loss restart and loss bisect (the loss noted in the activity file, the bisect stubbed: once, before the game, never again after a start ended inside it; the restart's steps robust 7, blend 8, backend 9: `lossrobust`, `lossstep`, `lossbackend`; a loss on the renderer Options > Graphics > Apply switched to and not kept yet, `losstrial`: its line, restart 0 and no relaunch; the same kept, `losskept`: `blend:opengl`) cases, the loss bisect's second level (its state file, the step stubbed: the draw found in 4 starts of at most 3 lost steps each, draw 1 with its classes in 5 starts, killed at step 3 and continued at 4, a renderer that did not start, the step and start bounds, the time budget, files that do not parse) cases, the watchdog and a wait the system holds (the event pump held past the limit without an end, a stall in the game's code after it ended, the bound for one wait, that bound with no limit in force), a GPU hang with the backend's progress faked (`hang`: the WATCHDOG block's driver and wait lines, the heartbeat's wait, the dump before the report, report.txt's end, the device loss's box on Linux, the hang's own loss line in the activity file, which the next start counts, with Android's robust step, and does not bisect; `hangnodump`: the hang's line without a dump, still counted; `hangnot`: no wait in progress, a freeze with the GPU's line and no loss), the program a restart starts (`relaunchexe`: the AppImage file when `APPIMAGE` is trusted, the program itself when it is not or is unset), each in a child process
  whose log is read back; the symbolizer (`symmap.h`) on a small GNU ld map of an ELF program (a wrapped function section, a static function in an object, an anchor not at the module's start), a Windows map excerpt, an nm table and its gzip form past inflate's 32 KB window, a cut gzip member and a map without the anchor; the crash's and the watchdog's symbol lines from `diag_test.map`, linked beside the test as `ico_pc.map` is beside the program; their report zips (`report.h`) read back with miniz (report.txt's end, summary and report block with its symbol lines, the log; the crash's report.txt kept as `diag_test_report.txt`, an example); the prune keeping the newest five; `ico_log_limited` (250 calls: the first 8, the 100th and 200th, the end summary's count; `ico_log_limited_n` with SDL's error bounds, the first 64) (`diag`).
- `share_report_test.c`: an rc build's Share a bug report's files over a fake logs and dumps
  folder: the log, the run before's when there, the newest dump zip by modification time (a loss
  zip included, an `.rddump` left out), the newest `-present.png`, the newest `report-*.zip` (not
  another `report-` file), a tie in time to the greater name, missing folders skipped, the names
  line and a short buffer (`share_report`).
- `dump_zip_test.c`: F12's zip (with the tick before as its second `.rddump`), the aspect sweep's zip and the device-loss zip (the lost frame first, the second `.rddump` the frame before, the pruning to the newest three), written and read back with miniz
  (`dump_zip`).
- `fiber_test.c`: [fibers](ARCHITECTURE.md#terms) switched round-robin with their stacks checked,
  deep stacks, the FP state inside a fiber (`fiber`, `fiber_guard`).
- `gpu_driver_test.c`: the Android driver packages: `meta.json`, refusals, installs from zips made
  here, the trial marker, and `test_list_zips` (`ico_gpu_driver_list_zips`: `a.zip`, `B.ZIP`, `incoming.*`, a text file, a hidden zip and a folder named `.zip` give two names in byte order, the cap and a missing folder) (`gpu_driver`).
  The Android file picker ([PLATFORM.md](PLATFORM.md#the-file-picker): `IcoActivity.pickFile`,
  `ico_android_pick_file`, `nativePickDone`) has no case: it is Java and JNI, and nothing here runs
  either. It is checked on a phone: `adb logcat -s ico-pc SDL` while pressing Add a driver shows
  the `ico_pc: file picker: pick N:` lines and the picker; `adb shell pm disable-user --user 0
  com.android.documentsui` (undone with `pm enable`) gives the `GET_CONTENT` fallback or, with no
  other picker app, the no picker app popup; a zip pushed to
  `/sdcard/Android/data/com.defnf.icopc/files/drivers/` shows Add from the drivers folder.
- `host_config_test.c`: `ico-pc.ini` parsing and rewriting, SHA-1 vectors, path joining, the
  card folders read from the ini the game reads (`ico_host_ini_path`), portable mode, the `[dev]`
  keys exported as environment switches (`ico_dev_env_value`, `test_dev_env`), a restart's
  `ICO_RD_BLEND` and `ICO_RD_VU_PREPASS` over the ini's (`test_retry_blend`,
  `test_retry_prepass`), the disc choice's rules in order (`ico_host_choose_disc`,
  `test_choose_disc`), the logs folder set by `ico_host_set_logs_dir` (`test_logs_override`), and
  on Linux the AppImage's program folder: with `APPIMAGE` and `APPDIR` set in the process the
  folder, `ico-pc.ini`, `logs/` and a `userdata/` portable folder are the AppImage file's, and
  `APPDIR` not holding the program, missing, the root folder or a mere name prefix, or `APPIMAGE`
  relative, a folder or empty, change nothing (`test_appimage`); built again
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
- `pad_mirror_test.c`: the game's `iosPadGetStick` d-pad to stick vector on both disc copies, Mirror
  mode off and on: the synthesised x flips, y and a deflected stick do not (`pad_mirror`).
- `pump_watch_test.c`: the event pump's history for its slow-pump lines: the window messages' names
  (the display's power, the power and device-change kinds, an unknown id), the noisy ones left out,
  repeats counted, the newest 16 messages and 8 SDL events kept newest first with their age, the
  message being handled named while it runs and timed, the watchdog's description, the controllers
  that came and went (a removal named from its arrival, the SDL driver from the GUID's byte 14) and
  SDL's own log ring (the newest 64 oldest first, a time bound, newlines folded, whole lines only in
  a short buffer) (`pump_watch`).
- `pace_loop_test.c`: the [vsync lock](ARCHITECTURE.md#terms)'s loop on a grid of the wall clock
  under a fake clock and a fake swapchain, with and without the display clock, at 60, 59.94 and
  120 Hz and on a display that drifts from its report, and its late-present skip (39 ms presents
  of a 30 frames a second game keep real time, never two frames in a row unpresented), and the
  present allowance with the fence (`test120Budget`: at 120 Hz and 12 ms of GPU a picture one
  present a step after the first second, no step late, the alphas on the grid; a GPU-bound phone
  at 60 Hz presents its new frames only and keeps real time; both print the rule before's numbers
  beside) (`pace_loop`).
- `pace_policy_test.c`: the slow-present decision, Resolution "auto"'s steps, the gap between
  presents, the vsync lock's n and alphas and its period's sanity, and the vblank clock's laws on a
  synthetic display, and the late-present skip: a whole period behind skips, with or without
  sound, less does not, slow presents skip past the deadline, never two frames in a row while the
  same frame may stay skipped; the timer path's loop under a fake clock keeps a 30 frames a second
  game on 39 ms presents at real time with no resync, where a frame skipped for one step only fell
  behind, and a 60 frames a second game on 20 ms presents through the late arm alone; the present
  allowance's rule (`testBudgetRule`: the drop to one below a late step's presents, the hold, the
  climb and its GPU gate, the display's wait excused, a late step of new frames only changing
  nothing, never above the ceiling), the timer path's unbound start (`testBudgetUnbound`: 15
  presents a step on time never refused, a drop held to the ceiling of 8, one climb past it
  unbound again, the lock's allowance never unbound) and the timer path with a GPU queue
  (`testBudgetTimerLoop`: nothing binds at 4 or 12 ms of GPU a picture nor on back-to-back 1 ms
  presents, the in-betweens held at 20 ms so no step ends half a period late, FIFO's return past
  the deadline excused); the clamp of a cap above the refresh in
  mailbox mode (`testPresentGap`, `testCapEqualsRefresh`) (`pace_policy`).
- `perf_block_test.c`: a block of replays by phase: sums, maxes, the replays over a threshold, the
  longest phase (`perf_block`).
- `sched_test.c`: the fiber scheduler against the EE kernel's rules through the SDK calls
  (`sched`).
- `startup_retry_test.c`: the steps after a graphics driver crash while building a pipeline, the
  crash chain to the box, the variable and the relaunch environment, the `dst` field of a
  pipeline's or a start-up batch's note, its VU vertex pre-pass keys and modules, and the chains
  with the pre-pass (a crash in one of its pipelines: cache, noprepass, then the rest without it;
  a loss in a run drawing with it: lossnoprepass, blend, backend) (`startup_retry`).
- `steam_shortcut_test.c`: Add to Steam's CRC and id, a sample `shortcuts.vdf` appended byte-true,
  the duplicate check, `loginusers.vdf`, and the recorded program path: the AppImage file when
  `APPIMAGE` names one and the program lies under `APPDIR`, else the program itself
  (`test_self_exe`) (`steam_shortcut`).
- `window_video_test.c`: fullscreen and mode requests on SDL's offscreen driver (`window_video`),
  Fullscreen holding the desktop's mode (its size and refresh rate) or no mode where the driver
  takes none, as `ico_window_video_desktop_mode` returns. The swapchain's
  `VK_EXT_full_screen_exclusive` request (Windows) has no test: lavapipe lacks the extension, and
  whether a display goes exclusive is the driver's.
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
- `rd_alpha_plan_test.c`: the alpha/blend planner on the CPU ([RENDER.md](RENDER.md#pipelines),
  the alpha plan): every ATST x AREF x As range under AFAIL KEEP and the FB_ONLY split against
  `gs_alpha_pass` over every As of the range, the other AFAIL shapes pixel by pixel, the blend
  identities over every Cs and Cd (`gs_blend_reg_ch`, `gs_dst_channel`), a model of the
  fixed-function path over every blend mode, PABE, FBA, COLCLAMP, FIX and range (the planned
  passes store the pixel the plan before stores), the ranges of screen prims, meshes, grids and
  particles, and `rdtex_alpha_range` against every texel and two- and three-texel blends
  (`rd_alpha_plan`).
- `rd_blur_test.c`: `staticBlur.c`'s full-screen effects on rd (`rd_blur`, sharded); its CPU
  part checks the open border's strips (`checkBorderStrips`: pixels 0, 1 and W - 2, W - 1 alone,
  each with column 2's or W - 3's U, plain and mirrored, the hardware sprite's S the same); the
  `above1x` shard runs the aura's chain at Enhanced 4x against the CPU model with the above-1x
  box (`above1x_ref.c`) for the decimations and the GS's bilinear at the finer grid for the work
  buffers drawn back over the frame, the `effects_scaled` shard the seven post modes (the glow's
  and the depth of field's pastes) at Enhanced 2x the same way, no B-spline in either, and every
  scale-1 comparison checks that no sprite took a kernel (`checkAbove1x`, `checkEffectsScaled`,
  `compareAll`); the `blur_fold` shard runs the seven post modes with the blur pairs folded
  (`RdProfile.blurFold`, `fx_blur2_ps`), every target against the CPU model of the two passes
  exactly, nine folds a chain, every pipeline created enumerated (`checkBlurFold`); the `fx_plan`
  shard runs the seven post modes and 40 motion blur frames with the frame-size sprites' plan
  (`rd_fx_plan.c`), every target against the CPU model exactly, the constant fold and the
  premultiplied paste counted, then with the scene passes readable and the destination by local
  read (`fx_local_ps`) where the device has local read, its self-test passing (`checkFxPlan`); the
  `fix128_skip` shard holds the plan off (it counts the skip's snapshots); the
  `motion_blur_smooth` shard (`rd_blur_smooth_test.inc`) runs Motion blur: Smooth against
  Original: a still picture within 1 at 1, 4 and 10 presents a tick, a moving bar's four presents
  a tick one DISPLAY under Original and four under Smooth with the ghost after the tick within 2,
  and the restart after a tick without the motion blur holding no ghost
  (`checkMotionBlurSmooth`); `motion_blur_smooth_scaled` the accumulator and its target at
  SCENE's size at Enhanced 2x with the full-height scene, freed by `rd_shutdown`
  (`checkMotionBlurSmoothScaled`); the `glow_edge_wide` shard runs the flare at 512, 848 and 2048
  GS pixels wide: one frame against the CPU model (which draws the replay's strip over the
  columns the blur's first pass leaves, `rd__glow_edge_strip`), ten strips a frame and none at
  4:3, WORK1's left columns the bytes of its first drawn one, then a frame with nothing cleared
  in it replayed over magenta and over green work buffers (`rd__set_work_clear`): SCENE the same
  bytes both ways, and without the strip different in the left eighth of a wide frame alone
  (`checkGlowEdgeWide`; [WORK_BUFFERS.md](WORK_BUFFERS.md#the-glows-left-edge)). The
  `shrinks_wide_device` shard's model draws the strip too.
- `rd_fx_frame_test.c`: the frame-size sprites' plan on the CPU against `gs_math.hlsli`: the
  constant fold's channels over every blend mode, FIX, COLCLAMP, PABE and a set of RGBAQ against
  `gs_dst_channel` for every Cd and Ad, the premultiplied term through the fixed-function add and
  reverse subtract against `gs_blend_reg_ch` for every Cs, C and Cd of the four forms, and the keys
  `rd__fx_enumerate` adds (none with the plan off, the constant fold's six without the frame and
  local modules), with no draw made without a device (`rd_fx_frame`).
- `rd_boot_present_test.c`: a saved start-up through the presenter, present by present; needs
  `ICO_BOOT_DUMP_DIR` (`rd_boot_present`).
- `rd_blend_selftest_test.c` with `rd_blend_selftest.cmake`: the exact blend's start-up self-test
  and the attachment read's Qualcomm rule; the mode left in force, the forest's tint under it, the
  targets without the attachment read where it is off, the self-test again at a new scene size;
  its log lines matched plain, sabotaged, under the fake Adreno, with each `ICO_RD_BLEND` and
  without a destination read (`rd_blend_selftest*`). The fixed-function blend is the default
  everywhere, so the exact paths are covered by opt-in runs: `rd_blend_selftest_blend_attachment`,
  `_blend_exact` and `_blend_storage` name the mode, and `rd_pixel_attachment`,
  `rd_fog_attachment`, `rd_present_attachment`, `rd_crt_attachment` and `rd_perf_attachment` run
  `rd_pixel_test`, `rd_fog_test`, `rd_present_test`, `rd_crt_test` and `rd_perf_test` under
  `ICO_RD_BLEND=attachment` with the same expectations and hashes the exact blend had as the
  default (`port/render/CMakeLists.txt`).
- `rd_vuprepass_selftest_test.c` with `rd_vuprepass_selftest.cmake`: the VU vertex and triangle
  pre-passes' start-up self-test at Enhanced, 16:9, scale 2 and at Original, 4:3, 1x: the
  pre-pass left on (it passes on lavapipe), turned off when its first command's vertex records are
  drawn a texel off (`ICO_RD_VU_PREPASS_SABOTAGE=1`), its triangles' slot A a slot off (`=tri`) or
  the tick stage's tick 1 at tick 0's camera (`=ticks`), or not tried when it is off by the
  settings (`ICO_RD_VU_PREPASS=0`); the summary text, the reachable set made after it (the main
  shaders', the pre-pass entries' and the five record stages' keys, or none of the pre-passes'),
  SCENE left to its clear; its log line matched (`rd_vuprepass_selftest`,
  `rd_vuprepass_selftest_plain`, `rd_vuprepass_selftest_sabotage`,
  `rd_vuprepass_selftest_sabotage_tri`, `rd_vuprepass_selftest_ticks`,
  `rd_vuprepass_selftest_off`); the device profile's class ends with the backend's name (`Vulkan):`,
  `OpenGL):`) and is the desktop column on OpenGL and Direct3D 11.
- `rd_vustage_ab.cmake`: every dump of `tools/vustage_dumps.txt` (the dump corpus, machine-local
  paths, `*` globs, a missing dump named and skipped, none at all `SKIP`) replayed by
  `rd_replay_tool --pass-dump --stats` under `ICO_RD_BLEND=fixed` the
  old way (`ICO_RD_VU_PREPASS=0`: the programs' entries) and through the record
  (the pre-passes, the main shaders, `vu_ps`), at Original 4:3 1x and Enhanced 4:3 2x: every named
  target's PNG byte for byte, no VU draw the old way with the record, the stats lines printed;
  A dump with no VU command (the four 2D dumps of the corpus) records no pre-pass and passes with 0
  passes; the records' camera is the one each command's draw uses (the walk follows the field's half
  pixel as `replayFrame` does). SKIP without the list, its dumps or a device (`rd_vustage_ab`).
- `rd_dst_usage_test.c`: the blend policy's footprint: by default only the fixed-function blend
  exists (`g_rd.dstModes` OFF, no destination-read layout, shader twin or target usage, no
  readable pass, both exact modes refused); with `ICO_RD_BLEND=attachment` only that mode's
  layout, twins and `SCENE` usage, and a start-up set that grows when the mode is switched on
  (`rd_dst_usage`; 77 without a Vulkan device).
- `rd_crack_test.c` with `rd_crack.cmake`: the crack filler above 1x
  ([RENDER.md](RENDER.md#crack-filling-above-1x)). A 640 x 448 frame under Enhanced, two flat
  triangles sharing a long diagonal edge, the second one's edge one 12.4 step (1/16 GS pixel) to
  the right, two sprites 1.5 GS pixels apart, below them a concave fold (two strips whose depth
  falls steeply towards their shared line) and the fog, replayed with the filler off and then
  as the environment has it, SCENE read back: at 2x, 4x and 1800 x 1080 texels (2.8125 x 2.41;
  `rd_crack`, `rd_crack_4x`, `rd_crack_frac`) the edge shows texels of the clear's fog without
  the filler and none with it, each filled texel the colour and the depth of a drawn texel
  within the reach on its row or column, the slit keeps every texel of the clear's fog, nothing
  the first replay drew changes (the fold among it), one fill pass is drawn and its count equals
  the texels changed; with `backdrop` (`rd_crack_backdrop`, `rd_crack_backdrop_4x`,
  `rd_crack_backdrop_frac`) a far plane writes Z over the frame first and the two triangles lie
  near each other in depth, so the cracks hold the plane's depth and colour and are filled the
  same (the discontinuity form) while the slit keeps the plane;
  at 1x, under Original, under MSAA4 and with `ICO_RD_CRACK_FILL=0` (`rd_crack_1x`,
  `rd_crack_original`, `rd_crack_msaa`, `rd_crack_off`) no fill pass and the same bytes; under
  MSAA4 the frame is replayed again without anti-aliasing and the filler: along the edge the
  MSAA4 picture holds at most 5 % of its texels with the clear's depth (colours without a depth
  readback) and the slit keeps the clear's fog on every texel; the same
  2x run on the depth copy, the buffer path, D24S8 (no depth readback: colours only) and the
  two-pass blend (`rd_crack_depth_copy`, `rd_crack_buffer`, `rd_crack_d24s8`, `rd_crack_nodual`).
  Under the MoltenVK report (`ICO_VK_FAKE_LIMITS=moltenvk`, a tile-based profile) `rd_crack_tiler`
  and `rd_crack_tiler_on` (`ICO_RD_CRACK_FILL=1`) find the filler on by default, as on every
  class (the old `off on this tile-based GPU` line denied). `rd_crack_tiler_fold` (`rd_crack_test
  2 fold`, 77 without local read) checks the fold on the fog's local read: one fill, drawn inside
  the scene pass the fog keeps (`RdCrackCounts.folded`), the fog by the local read in the same
  replay (`fogLocalDraws`), one render pass more than without the filler besides the count's (not
  two), and SCENE's colour and depth byte-identical to a replay on the depth copy path (the fill in
  its own pass); `rd_crack_tiler_fold_mali` the same under `ICO_VK_FAKE_LIMITS=mali` with
  `ICO_RD_FOG_PATH=local`. Both match the state line's `(drawn inside the scene pass with the
  local fog)`.
  Each run's `rd: crack fill:` state line is matched (`-DEXPECT`, `-DEXPECT2`; `-DDENY` must not
  match); `rd_replay_tool_crack` runs the tool on `rd_pixel`'s dump with `--enhanced --resolution
  2x --stats --no-crack-fill` and matches `crack fill: off (the replay tool's --no-crack-fill)`
  and the `stats: crack fill off: 0 passes, 0 pixels filled` line.
- `rd_crt_test.c`: the CRT filter's options and its shader (`rd_crt`, `rd_crt_fixed`).
- `rd_debug_test.c`: `debug.c` on the host, recorded (`rd_debug`); with the Game variants' bind
  (`debug_VariableInit` hands `ico_variants_bind_debug` the five switches and `debug_MenuOpen`)
  and the menu opened through that opener drawn on each of three ticks with no pad (title and bug
  list), nothing while closed, Triangle closing it.
- `rd_filter_test.c`: the model viewer's draw filter on synthetic keys (`rd_filter`).
- `rd_fog_selftest_test.c` with `rd_fog_selftest.cmake`: the fog's self-test and probe; its log line
  matched for each path, MSAA counts, odd widths and [Full pixel](ARCHITECTURE.md#terms)
  (`rd_fog_selftest*`; the local read's first under `ICO_RD_FOG_PATH=local`,
  `rd_fog_selftest_local`). With `rd_date_selftest.cmake` and `ICO_RD_DATE_PATH=stencil`, DATE's
  self-test, which the fog's first run runs first: `DATE: stencil path (self-test: stencil ok 4,
  snapshot ok 4)` matched once (`rd_date_selftest`). The failure paths, under
  `ICO_VK_FAKE_LIMITS=moltenvk` (the profile's Apple row: `fogLocal` and `dateStencil` the
  profile's own, not `[dev]`'s): with `ICO_RD_DATE_SABOTAGE=1`, `DATE: snapshot path (self-test:
  stencil fail ...` and `profile: DATE stencil off (self-test)` (`rd_date_selftest_sabotage`);
  with `ICO_RD_FOG_SABOTAGE=local`, `local fail N, copy ok N`, the copy taken at both sizes and
  `profile: fog local off (self-test)` (`rd_fog_selftest_local_sabotage`). Both scripts take
  `-DEXTRA` and `-DEXTRA2`, regexes that must each match the log at least once.
- `rd_fog_test.c`: the depth fog of `fog_DrawFog` on the CPU and on the device, every depth path
  (`rd_fog`, `rd_fog_*`; the local read from the start in `rd_fog_local`, the other paths' replays
  the same bytes); at 2x a depth ramp's fog alpha equals the above-1x formula (the table
  interpolated, the ordered dither) texel for texel, and the 1x cases count no above-1x draw
  (`checkRamp`); the outer columns' probe: its count of GS Z 0 rows on D32F and D24 words, the texel
  each GS column is read at (1x, 2.8125x, and Full pixel's window with GS column 0 left out) and
  the line of the first 8 and last 8 GS columns on the CPU with each of the places those columns
  go (open, cropped, closed at 512 and at a wide 832, Full pixel's window with its `-` columns:
  `checkEdgeLine`), then with Full pixel on at 4:3 the cells' scene, two cells left at Z 0 at the
  frame's edges, gives 32 rows in each of the 16 columns and the left's run (`checkEdgeProbe`; on
  Vulkan and OpenGL, which copy a region of a depth to a buffer, else only on the buffer path), and
  with it off a line, if one was logged, says the border is closed.
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
  (`rd_interp`); the feedback head copies (`testFeedback`: `DISPLAY_HELD` and `FEED_HELD` at a
  tick's first present and the later ones, none for DISPLAY under Motion blur: Smooth, FEED128's
  unchanged, the motion blur's record as recorded; FIX' of 32 at 1, 2, 4 and 10 presents and at
  the ends); its rotation cases live in `rd_interp_turn_test.inc` and its seams cases in
  `rd_interp_seams_test.inc`, included where they stood, and its bars case in
  `rd_interp_bars_test.inc` (`testBars`: the same screen sprite under another key in each tick is
  drawn once, whole; a taller one or another blend FIX still cross-fades,
  [WORK_BUFFERS.md](WORK_BUFFERS.md#screen-sprites-redrawn-under-a-new-key-between-ticks));
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
  The seams (`testStillSeams`, drawn through the game's screen and clip matrices): two still prelit
  parts with different model to world matrices, the camera orbiting 28 degrees, are still draws at
  alpha 0.5, each bit for bit the same part unmatched (the tick's block re-based, `camCurDraw`);
  their shared vertex at a cell's middle lands in the expected cell through `vu1_ref` (placement);
  two meshes of one model matrix with other centroids have bit-equal camera and model rows, and a
  point a quarter of an ulp from a cell's edge lands in one cell in both (the mechanism); at alpha 0
  the frame's view is the tick before's bit for bit and each part's rows lie within the still bound
  of the tick before's. The seams at alpha 0 (`testStillSeamsAlpha0`): the same two parts, their
  shared vertex at a cell's middle through the tick before's camera, (1) both matched, (2) both
  unmatched, (3) A matched and B unmatched (the wedge's case); in each the frame's view is the tick
  before's, both parts' camera and model rows are those of (2) bit for bit (the tick's block
  through the tick before's camera, `camCurDraw`), no picture is mixed, and the vertex lands in the
  same cell in both; B through the tick's own block and camera lands over 16 pixels away, so (3) fails on a renderer without the alpha 0 camera; frames
  without a camera count a mixed picture (`mixed` 1). The size flap (`testSeamsSizeTail`, Enhanced):
  B drawn with the region code at one tick and the scissor code at the other, so one payload
  carries a card section and the other not, at alpha 0 and 0.5: both parts blend (none mismatched
  by size, `sizeTailBlended` 1), both are still draws with the shared vertex in one cell, and B's
  card section is the tick's. The present clock (`testPresentClock`) notes
  its value before the clamp (`rd__present_clock_raw`), below 0 for a present before the tick's
  time, and `rd__alpha_bin` sorts the alphas into the 10 s line's bins. The ticks' eyes
  (`testEyeTicks`, on a device): alpha 0 carries the ticks' cameras as every present between the
  ticks, alpha 1 none.
- `rd_layout_test.c`: the layout frame from the game's layout calls (`rd_layout`).
- `rd_mesh_test.c`: the mesh path from `Packet.c` to the VU shaders (`rd_mesh`, sharded), each
  device case's frame replayed again with the VU vertex pre-pass switched, the picture byte for
  byte the same, and every pipeline created in the set of either setting (`gpuCase`); the
  content table's four sandstorm sheets and the tiles appended to them (`sheetTableChecks`), one
  sheet on the device at 4:3 byte for byte the same with and without the tiles and at 16:9 the
  columns beside it drawn (`sheetPixels`), and the `photo_veil` shard: the veil's two layers at
  60:9 drawing every pixel with the tiles (`photoVeilPixels`), and the `photo_reel` shard: the film
  reel's four specks of the deja dump at their places on the 16:9 frame, none tiled, every pixel
  inside a speck's own place and in the 4:3 picture's columns (`photoReelPixels`).
- `rd_mip_test.c`: the Enhanced mip chain's size and its builder staying inside it (`rd_mip`).
- `rd_mirror_test.c`: mirror mode's flag and the flipped presenter output (`rd_mirror`).
- `rd_passring_test.c`: the pass ring the device-loss report prints: order, wrap, the replay table, the
  replays printed (the last three, whole at 430 passes a replay), the line format (the VU
  vertex pre-pass's entry among them, `testVuPre`) and the loss bisect's line; pure
  (`rd_passring`).
- `rd_msaa_test.c`: the anti-aliasing's MSAA against the frame without it (`rd_msaa`); with
  `msaa_resolve_ps` or `depth_resolve_ps` cleared for one call (a backend that cannot make one)
  the supported mask holds the supersampling values only; `testShadowReset`: a frame whose list 3
  holds the shadow count with a reset on SCENE's depth resolves once a replay
  (`RdPerfRecord.msaaResolves` 1, two when the reset's stencil clear closes the epoch again from
  inside `msaaEnd`), closes at list 3 (`msaaCloseAt` 4) and leaves SCENE byte for byte as the same
  frame without the reset; `testSkySlit`: two triangles of one surface over the clear with a 1/16
  pixel crack along their shared edge, the shadow count, then `cleanUpFB`'s glow mask (a sprite
  at GS Z 1, alpha 192, alpha only, GEQUAL without Z writes): MSAA4 marks no more texels along the
  crack than the frame without anti-aliasing (the covered-majority resolve,
  [MSAA.md](MSAA.md)) and some silhouette pixels' colours still differ; on any device, `testSuspend` (Options > Graphics >
  Apply's renderer switch): `rd_suspend` with a frame open, `rd_resume` carrying the scene size,
  the frame counter, the mirror mode and the GS state (its texture cleared), a texture id from
  before naming nothing, the DATE self-test's flag cleared, no frame open until
  `rd_resume_frame` opens one (once), whose temporary target is freed and which closes without
  a picture.
- `rd_native_test.c`: the native frame's centre rule on a synthetic frame at five widths
  (`rd_native`).
- `rd_perf_test.c`: the renderer's CPU cost per replay and its steady state, and the start-up set
  through `rhi_create_pipelines`: its progress calls (from 0, never back, the total once at the
  end), a stop after the first batch and the rest made by the next call; the exact blend's
  counters (`RdPerfRecord.dstDraws`, `storagePasses`, `dstSwitches`) under each blend path the
  device has (none under the fixed-function blend, no storage pass or switch under the attachment
  read) and, under the storage read, the storage pass merge's DISPLAY bytes equal to the unmerged
  ones with no more passes or switches; the replay and record phases run without the VU vertex
  pre-pass (their barrier counts), a `prepass` phase replays the frame with it (the records still
  pending from before the switch dropped by replay number, since a record is pushed
  `RHI_FRAMES_IN_FLIGHT` replays after its own; nothing created after the phase's first two
  replays, one pre-pass a replay with the same vertices, every mesh command through it, steady
  barriers, DISPLAY byte for byte the picture without it), a `passes` phase times the record
  phase's last frame pass by pass (`rd__set_perf_passes`): each timed record's pieces begin with
  the uploads (the pre-pass second where the device takes it) and end with the present, the
  pieces the pass ring also notes are its entries of that replay one for one and in order (a
  render pass at the ring's size), the pieces' draws and pipeline binds add up to the record's,
  their counts by kind are the record's `passKinds`, every piece has a GPU time and they add up
  to the record's GPU time within 10 percent and 0.5 ms; without the key a replay is timed only
  250 ms after the last timed one (`RD_PERF_PASS_EVERY_MS`), an untimed one has no pieces, and
  every replay counts one upload and one present; without the list and effect stamps
  (`rd__set_perf_list_stamps`) the lists' times are 0, the GPU total is there and the pieces add
  up to it; before any device, the CSV header and line,
  the top-passes text (with a cut to a short buffer) and the passes-by-kind text, to the byte
  (`checkPassTexts`). The reachable set is made with the pre-pass (`rd_perf`, `rd_perf_mali`).
- `rd_pixel_test.c`: rd through the Vulkan [RHI](ARCHITECTURE.md#terms), pixel by pixel; leaves the
  dump the tool tests replay (`rd_pixel`). Its pipelines case holds the reachable set under
  `RD_PIPELINE_REACHABLE_MAX` with both blends, the tile-based GPU's set too (its profile with
  local read and the fx plan, the pre-pass off, every state static, Enhanced at 2.8125 x 2.41071:
  1153 keys), that one also inside the RHI's pipeline pool
  less its headroom (`testPipelines`). Its DATE case replays one frame through the stencil bit
  and through the snapshot (where the device has local read): one DATE command of two overlapping
  sprites over cells of alpha 0x80 and 0, SCENE the same bytes both ways and the snapshot's
  semantics within the draw (`testDateStencil`; it skips under `ICO_RD_BLEND=storage` and prints
  why: the storage read has no colour attachment to read, so DATE takes the snapshot there). Its alpha plan case replays one frame with the
  alpha/blend planner off and on: prelit quads of vertex alpha 0x80, 0x70 and 0x40 under the
  railing's state, a LERP FIX 0x80 sprite and a LERP sprite of As 0, SCENE's colour and depth the
  same bytes, three draws counted one-pass (`testAlphaPlan`); the railing keeps its split. Both
  replays run with the fixed-function blend (the mode forced off for them): under the storage read
  the unplanned quads stay on that blend while the planned ones take the exact read, a rounding
  apart, so under a mode asked a third replay (the plan on, that mode) must stay within 3 per
  channel of the planned fixed-function one with the depth equal; a mismatch prints the first
  differing pixel and both values.
  Its AA1 wedge case (`testAa1Wedge`, `rd_pixel_aa1_test.inc`) draws the drained room's ripple
  state (PRIM.AA1, ABE, `Cs * FIX + Cd` with FIX 0x60, Z write, DATE DATM 1, a flat colour of 64,
  so every fragment adds 48) over SCENE: a four-vertex strip with an acute corner and an
  octagonal ring of eight quads, at alpha 0xFF and again at 0x80, and checks every pixel's red is
  48 times the fragments a C model of PCSX2's `DrawTriangle` and `DrawEdgeTriangle` draws there
  (the GS's fill for the interiors) and its DATE keeps, the destination alpha tested as each
  fragment is written (`GSDrawScanline.cpp`), the alpha 0xFF wherever one lands, the As of the
  single edge pixels at 0x80 and the alpha of the last kept fragment within 1, and that the
  model's DATE rejects more than 400 fragments at 0x80 (473 at 450 pixels; DIVERGENCES.md VD-17:
  the port draws the strips primitive by primitive through DATE's stencil bit). It runs on the
  device's own DATE path and, where the device has local read and the blend is not the storage
  read, again through the stencil bit (SCENE made with DATE's local read, setups counted, no
  snapshot), the DATE stencil draws counted both times; it prints the pixels with 1, 2 and 3 or
  more fragments and the fragments the model's DATE rejects and keeps.
  `rd_state`'s `aa1 DATE pairs` case checks the ripple state's pairs are enumerated: TEST0 and
  TEST0_SET (Z write on and off) in the screen set, TEST1 and TEST1_CLR in DATE's set with
  `dateStencil` and not without; its tiler set allows DATE's screen twins 224 + 12 and counts one
  setup key of its own (the CLEAR key is the desktop set's).
  Its VU seams case (`testVuSeams`, at Original 1x and 4x and Enhanced 2.25x and 4x) counts the
  clear's pixels along edges two draws share: S1 a code-32 triangle beside a GIF one, S2 two
  meshes 0.01 GS pixel apart, S3 a code-36 cut triangle beside a kicked one, S4 8 x 8 quads under
  a matrix each, none in any mode; S5 a code-32 triangle the region test drops, drawn clipped
  beside an exact one, their shared vertex's GS Z saturated and its X and Y off the 1/16 grid:
  none above Original 1x, where the clipped triangle is drawn, and at Original 1x not drawn
  (`vu_corner_cut_position`).
  Its present case (`testPresent`: the presenter's output pixel at the centre between its two
  DISPLAY neighbours) runs right after the reduction, before `testReductionUint` makes the
  targets again: a target made again holds whatever the driver's memory held until the next
  replay clears it (lavapipe hands the freed DISPLAY's memory back, llvmpipe's OpenGL gives
  zeros), so DISPLAY read after that is not the frame the presenter showed.
- `rd_present_test.c`: the display options through `GsBase.c` and the presenter (`rd_present`;
  the scene limit's fallback checks drive the Vulkan backend's `vkr_test_fail_texels_above` and
  are skipped on Direct3D 12, OpenGL and Direct3D 11, which have no such hook);
  the open border's outer columns (`rd_present_border_test.inc`, `checkOpenBorderEdges`): at
  2340x1080 (832 columns, the frame its exact width) and 4:3, 1x and 2x, the mirror on and off,
  SCENE red with green over GS columns 1 .. W - 2 presents green outer columns, byte for byte the
  present of SCENE green everywhere, and without the strips (`rd__set_blur_border_repeat`) only the
  outer columns differ; at 16:9 (cropped) and with Full pixel off the strips change no byte;
  DISPLAY's strips are their neighbour columns' bytes at 1x and one value a row at 2x; SCENE's
  own outer columns (`checkSceneBorderRepeat`): a fogged frame whose SCENE leaves GS columns 0, 1,
  W - 2, W - 1 at the clear (the fog paints them white) and a glow-like post sprite spreading SCENE
  eight GS pixels in from each edge presents, at 16:9 Enhanced 2x with Full pixel off (the border
  mask), at 16:9 2x with it on (whole scale: cropped), at 2340x1080 with it on (open) and off
  (closed on a wide frame), and in Full pixel's window at 2560x1080 (the window's scale 2.8125,
  `ox` 1.5: texels of columns 0 and 1 in it), byte for byte the frame drawn over every column,
  with one repeat a replay, and without the repeat (`rd__set_scene_border_repeat`) differs within
  14 GS pixels of the box's edges alone; each run's `rd__scene_border_state` is the one expected;
  at 4:3 with it off (the border closed on the PS2's frame) SCENE is kept as drawn; the 48:9 strips (`checkExactEdges`) are checked one
  value a row over the texels they cover by a whole texel (from 0 to below 2k - 0.75 and from
  k (W - 2.25) + 1 on, k texels a GS pixel); the present pins hold without the strips, and with
  them differ in the outer columns alone (`checkFullPixelOnPin`),
  an old file's `window_size` among them (ignored, kept as written);
  the targets' scales include the work buffers and shadow levels at their GS sizes at every scale
  and Water reflections' block: its GS size under Original, the scene's scale under Sharp (4x at
  4x), both the GS size at 1x (`checkScales`).
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
  passes or pass switches; skips without the storage read, as on a lavapipe before Mesa 25.2,
  which has no interlock; from 25.2 lavapipe has it and the test runs
  (`rd_replay_tool_pass_merge`).
- `rd_replay_tool_snap_swap.cmake`: that dump on each blend path the device has, at 1x and 2x:
  the staticBlur sprites' snapshot swap draws `ICO_RD_SNAP_SWAP=0`'s (the copy's) DISPLAY picture
  byte for byte, and its copies plus swaps are the copy's snapshots (`rd_replay_tool_snap_swap`).
- `rd_replay_tool_keys.cmake`: every dump of `ICO_DUMPS` (a list or a folder; skips without it)
  through `rd_replay_tool --keys` at 1x, `--enhanced --resolution 2x` and `--enhanced --resolution
  2340x1080`, on the fake phone limits (`ICO_VK_FAKE_LIMITS=mali`) and again with the tile-based
  GPU's paths forced (`ICO_RD_DATE_PATH=stencil`, `ICO_RD_FOG_PATH=local`, `ICO_RD_BLUR_FOLD=1`;
  the fake limits drop the local read, so the two cannot be combined, and a device without local
  read compares the desktop set again, which the test's output says). `--keys` walks the replay
  with nothing drawn (`rd__pipeline_gather`), makes the keys it names and compares them with
  `rd__enumerate_reachable`; the test fails on a key the set lacks, which on a device is one
  first-use stall (`firstUseLine`), never a wrong picture. The union of the asked keys is left in
  `rd_replay_tool_keys/keys-<config>-<size>.txt` (`rd_replay_tool_keys`).
- `rd_bisect_plan_test.c`: the loss bisect's second-level planner against oracles: a draw that
  faults with every draw before it (1, 731 and 1,312 of 1,312) found within 12 steps, the masked
  step's verdict, a killed step counted as lost, a step without a verdict planned again,
  contradicting prefixes ending the plan, 1,000 random oracles ending within 16 steps
  (`rd_bisect_plan`).
- `rd_depth_copy_dump.cmake`: a player dump with the fog replayed with the depth in place, copied
  and through a buffer, SCENE byte for byte the same (`rd_fog_depth_copy_dump`, `_d24s8`).
- `rd_shadow_test.c`: the shadow count on the stencil (`rd_shadow`); at scene scale 4 each
  composite against the CPU B-spline within 2 LSB and a row across the shadow falling and rising
  once, the softening's AA0 (at its GS size) the area box of SCENE and its composite back the
  bilinear between AA0's texels, not counted above 1x (`checkComposite4x`, `checkSoftening4x`).
- `rd_state_test.c`: the recording half of rd without a GPU: list order, defaults, state leakage,
  the alpha test's plan for every Z write, AFAIL, ATE and alpha mask (one draw where the test decides
  nothing, `testAlphaMerge`), the start-up set's VU keys by variant and with the VU vertex
  pre-pass (`testEnumerationVu`, `testEnumerationPrepass`: every program entry's key without the
  pre-pass has its `vu_main_vs` or `vu_particle_main_vs` twin with it, and the keys a phone made on
  first use, blend 4 and 12 under Z ALWAYS (as `vu_main_vs` keys) and the UI sprite under Z GEQUAL, are in the set), the tile-based
  profile's set against the desktop one, static, with dynamic depth and with dynamic depth and
  blend, with and without local read (`testTilerSet`: the desktop keys and exactly DATE's, the fog's
  local key and the fold key, none twice; printed beside the careful-set target; each set under
  `RD_PIPELINE_REACHABLE_MAX` and inside the RHI's pipeline pool less its headroom), the tile
  class's largest set (its profile with local read and the fx plan, stand-in modules), the
  pre-pass off and every state static at 2.8125 x 2.41071, 1153 keys: the 2x set
  and the `vu_np_ps` keys (the DATE pass's twins of them too), each with its `vu_ps` key in the
  set, under the same two bounds (`testTilerFracSet`), and a dump's menu text sheets loading as the renderer makes them, a sheet with
- `diag_test.c`: the crash report (also from a worker thread under a batch's pipeline note), assert, watchdog, progress (with the heartbeat's start-up set count), start-up activity file (the batch notes), device-loss restart and loss bisect (the loss noted in the activity file, the bisect stubbed: once, before the game, never again after a start ended inside it) cases, each in a child process; the symbolizer, the report file and `ico_log_limited` (above)
- `rd_tex_test.c`: the texture cache and `Texture.c` on synthetic TIM2 files, and a text texture
  that could not be uploaded named once in the log, the game's own textures never
  (`textUploadLogChecks`) (`rd_tex`).
- `rd_vu_frac_test.c`: the pixel side at a fractional scale, CPU only (`rd_vu_frac`): the truth
  table of `rd__vu_pixel_fs`, `rd__planes_dropped` and `rd__screen_planes_want` over the profile
  flag, the preset, the exact blend, nine scales (whole and fractional on either axis), the two
  filters, a texture with and without GS levels, the chain clip plane and the entries; the
  start-up set at 2.8125 x 2.41071 being the 2x set and one `vu_np_ps` twin per `vu_ps` key; the
  `vuNoPlanes` count; and in the embedded SPIR-V every input of `vu_np_ps`, `vu_texa_np_ps` and
  their `_nodual` builds at location 0, 1 or 2 with `vu_ps`'s type and interpolation there, and
  `vu_ps`'s inputs matching the outputs of every VU vertex shader that draws (the main shaders,
  the programs' entries and their plain builds).
- `rd_water_test.c`: the surfaces that render to and sample from textures (`rd_water`).
- `texpack_disc_test.c`: the PCSX2 names of every texture on the disc (the walk of
  `port/render/texpack_walk.h`), and how many of a pack's files they reach (`texpack_disc`).
- `texpack_e2e_test.c`: the texture pack end to end on the real pieces (`texpack_e2e`).
- `texpack_export_test.c`: the texture export's core on TIM2 files it makes: the names written are
  the level-alone candidates, each PNG at the padded size, a file already there kept, short and
  24-bit CLUTs refused, a repeated texture one file and a duplicate, the manifest's counts and
  flags, a second run writing nothing, a stopped and a dry run (`texpack_export`); `--disc` a dry
  export of the disc against `texpack_names.txt` and a cancel (`texpack_export_disc`).
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
  pipeline cache file, and the pipeline pool filled: `RHI_PIPELINE_POOL_MAX` pipelines alive at
  once, none refused, the 1025th and the last drawn with, one more refused and counted
  (`poolCell`, once, not under the fake switches) (`rhi_vk`, `rhi_vk_nodual`, `rhi_vk_d24s8`,
  `rhi_vk_mali`).
- `rhi_d3d12_test.c`: the same checks on D3D12, a Windows GUI program that writes a log and shows
  a message box (`rhi_d3d12`).
- `rhi_d3d11_test.c`: the same checks on Direct3D 11 (WARP, the hardware adapter, the swapchain on
  a hidden window, Vulkan for comparison), the cells' shaders compiled at run time from
  `rhi_test_hlsl.h`; a Windows GUI program that writes `rhi_d3d11_test.log` and shows a message box
  (`rhi_d3d11`, registered on a native Windows build only). Built only: the `win-x64` and
  `win-x64-clang` cross builds build it and do not run it, and it has not run on any Windows machine
  yet.
- `rhi_d3d12_plan_test.c`: the D3D12 backend's state and barrier bookkeeping on the CPU
  (`rhi_d3d12_plan`).
- `rhi_d3d11_shader_plan_test.c`: the Direct3D 11 backend's shader route on the CPU
  (`port/rhi/d3d11/d3d11_shader.c`): the macros text and its malformed forms, the include lookup,
  the flat-register rule on hand-made sources, on `rhi_test_hlsl.h` (which must equal
  `rhi_test.hlsl`: run `gen_shaders.sh` after editing it) and on the game's sources (the embedded
  ones, else `port/shaders` read from the source tree), the cache key's inputs and its distinctness
  over the shader table, the cache file's round trip in a folder of the build directory and the
  wrong key, changed byte, short, long and non-DXBC files it refuses, the failure log's limit and
  the counters (`rhi_d3d11_shader_plan`). The compile itself runs only on Windows; the
  `shaders_d3d11_check` build target is the build hosts' check of the D3D11 preprocessing
  ([SHADERS.md](SHADERS.md#direct3d-11-run-time-compile)).
- `rhi_gl_test.c`: the same checks on OpenGL with the driver's debug output on (any error or
  undefined-behaviour message fails), then the copies the cells do not make (a colour region into a
  device buffer the vertex stage reads and on into a readback buffer, a D32F_S8 depth as floats, an
  RGBA32_UINT target as words, each at a row pitch wider than the region; a target made after one
  whose framebuffer was dropped) and six swapchain frames on an SDL offscreen window with a resize
  (`rhi_gl`, and `rhi_gl_nodual` under `ICO_GL_FAKE_NO_DUAL=1`). ctest runs both on Mesa's llvmpipe
  (`LIBGL_ALWAYS_SOFTWARE=1`, `SDL_VIDEO_DRIVER=offscreen`); they skip (77) without an OpenGL
  context. On llvmpipe the destination-read cells skip (no fragment shader interlock; GLSL has no
  attachment read).
- `rhi_gl_glsl_test.c`: the OpenGL backend's GLSL inflate on the CPU (`port/rhi/gl/gl_glsl.c`, miniz):
  a deflated text round-trips NUL-terminated; a wrong text length, a corrupt or truncated stream and
  no stream give none (`rhi_gl_glsl`).
- `rhi_driver_version_test.c`: the driver version text of the adapter line
  (`port/rhi/rhi_driver_version.c`) for each vendor's packing, the Qualcomm and MoltenVK rules and
  the colour bytes a pixel of each Apple GPU family (`rhi_metal_color_bytes`)
  (`rhi_driver_version`).
- `rhi_vk_enum_test.c`: every `rhi.h` enumeration has a Vulkan mapping, `RGBA32_UINT` among them
  (`rhi_vk_enum`).
- `rhi_vk_present_mode_test.c`: the present-mode choice over the sets drivers offer
  (`rhi_vk_present_mode`).
- `rhi_vk_swapchain_test.c`: the window path without a display, on SDL's offscreen driver
  (`rhi_vk_swapchain`).
- Helpers: `rhi_test_common.c`/`.h` (the shared cells, seven of whose pipelines come from one
  `rhi_create_pipelines` call with a failing description among them, and the fill-rule cell:
  edges through pixel centres under the top-left rule on every backend), `shaders/`
  (`rhi_test.hlsl`, its compiled headers and `gen_shaders.sh`), and `compare_backends.cmd` with
  `compare_png.ps1`, which render every dump on Vulkan and D3D12 on Windows and compare the PNGs
  within 1 LSB.

### port/shaders/test

- `shaders_table_test.c`: every embedded entry compiled for SPIR-V and signed DXIL, the vertex
  pre-pass's among them (`expectedPrepass`) (`shaders_table`).
- `gs_math_test.c`: `gs_math.hlsli` compiled as C against the [GS](ARCHITECTURE.md#terms) formulas,
  and its integer forms against the operators they replace ([SHADERS.md](SHADERS.md#integer-forms))
  (`gs_math`); `hlsl_shim.h` makes it compile.
- `shaders_glsl_test.c`: every embedded entry that has GLSL compiled and linked by the OpenGL
  backend (a separable program of its stage) with the driver's debug output on; a compile or link
  failure, whose driver log is the backend's line, or a debug output error fails; the `_dstrov`
  builds count as skipped on a device without fragment shader interlock (llvmpipe); `--list`
  prints each entry's deflated and text sizes (`shaders_glsl`, on llvmpipe as `rhi_gl`; 77 without
  an OpenGL context or a table built without GLSL).
- `shaders_pixel_test.c`: sprite, blit and blend shaders through the Vulkan RHI
  (`shaders_pixel`, `_nodual`, `_mali`); first, on the CPU, `common.hlsli`'s above-1x kernels
  compiled as C against `above1x_ref.c`: the Bayer matrix, the B-spline's weights, the box, the
  axes and the separable combine, and the fog's `gs_fog_channel` (`checkAbove1xKernels`); on the
  device, `sprite_ps` above 1x over a magnified, a decimated and a mixed noise texture with the
  steps from `DrawCB.scale[3]` against the steps from the pixel's footprint, at most 2 levels apart
  (`checkAbove1xSteps`).
- `vu1_test.c`: the [VU1](ARCHITECTURE.md#terms) program shaders against their CPU references and
  hand traces, the card verdicts handed over as the renderer does, and the CPU's card test against
  the reference; the record: every mesh case's vertices through its pre-pass entry and its
  triangles through the record stages (the tick stage under the tick cameras), every particle
  case's sprites through `vu_particle_pre_vs`, and its probe and pictures through `vu_main_vs` or
  `vu_particle_main_vs` and `vu_ps` byte for byte the program entry's (with the same `vu_ps`), the
  probe within the CPU reference's bounds, the mesh, particle and tick-camera cases counted (part
  b (4)); the pictures use the plain mapping (no exact viewport), so the planes stay the GPU's
  there (`vu1`, `vu1_mali`).

### port/test

- `vu1_variant_table`, `vu1_variant_diff`, `vu1_variant_equiv` (`tools/vu_variant_diff.py`,
  `tools/vu_variant_equiv.py`, `tools/test/vu1/`): the June 2001 demo's `cluster`, `mesh`, `normal_c`
  and `normal_l` programs differ from the retail ones in 17 hunks, one of them semantic (the lit mesh
  loop's UV offset on every vertex). `vu1_variant_table` needs no decomp: the diff's hunks, the rows of
  `jun26_vs_pal.md`, `VU1_JUN26_SEMANTIC_DIFFS` (`vu1_ref.h`) and the draw flag (`ICO_VU_JUN26`,
  `VU_F_JUN26`) agree. The other two run the decomp's `.vsm` text through `cpp` (the cache variable
  `ICO_DECOMP_DIR`, skipped, 77, without it or without `cpp`) and compare the diff and the symbolic
  executor's per-routine verdicts (`jun26_vs_pal_equiv.txt`: same output quadwords on every clip path,
  or the first quadword that differs) byte for byte with the committed files. `vu1_test.c`'s
  `testVariantPlumbing` holds the state's variant, its names, the flag's bit and the block's size, and
  names the demo's hand traces that are still to be written.
- `layout_asserts.c`: the generated struct layout asserts, which hold at compile time
  (`layout_asserts`).
- `gs_blend_test.c` is a standalone CPU program on the GS blender's drift, not a ctest.
- `ico_check.h` and `ico_shard.h` are the framework above.

### port/ui/test

- `credits_headless.py`: Extras > Credits in the headless game (`credits_headless`).
- `font_coverage_test.c`: every character of the eight languages has a glyph in Arimo, in Noto
  Sans JP's subset, in Noto Sans KR's or in Noto Sans TC's, a Japanese character the second font's
  own, a Hangul one the third's and a Chinese-only ideograph the fourth's, the Chinese face first
  or last by the language (`font_coverage`); `font_corpus/` holds the per-language text, `ja.txt`,
  `ko.txt` and `zh.txt` the Japanese, Korean and Chinese menus' text that no table holds.
- `font_edge_test.c`: the port's plain text at the output's resolution on a device (`font_edge`).
- `gallery_sweep.py`: every stream and a sample of every bank in one headless run
  (`gallery_sweep`).
- `gallery_test.c`: the music gallery's list from the base ELF's tables (`gallery`); with
  `--region jpn` or `--region kor` (`--jpn` is `--region jpn`), the European and that disc's 105
  `adpcmFile` paths compared row for row, then the list from that disc's tables and image
  (`gallery_jpn`, `gallery_kor`).
- `menu_region_test.c` (`text|look <slug>`): the Japanese, Korean and Asian discs' menus: the region's
  layouts, rows and glyph rows against that disc's ELF tables, no row from the menu text table
  (`menu_text_jpn`, `menu_text_kor`, `menu_text_cn`); with the disc's image, the glyph rectangles'
  ink, the sheets' light ink held to `kSheetInk[UI_SHEET_JA]`, `kSheetInk[UI_SHEET_KO]` or
  `kSheetInk[UI_SHEET_ZH]`, and the port's words' boxes against the disc's rows (`menu_look_jpn`,
  `menu_look_kor`, `menu_look_cn`; the Korean ink 252.8 fill, 41.8 rim, measured on the disc; the
  Asian disc's expectation is the Japanese numbers until `menu_look_cn` has been run on it).
- `menu_look_test.c`: the menus' Arimo text against the game's menu sheets from the disc
  (`menu_look`); `menu_sheets.c`/`.h` decode the sheets.
- `menu_text_test.c`: the game's menu words and the port's rows drawn in the sheets' look; the
  Models viewer's counter drawn for 600 frames at 3x and 1x (`testCounterStream`: shelves
  reclaimed, no plain-font word, no second page, the steady rows in place; at 3x the counter's
  coverage and rim in rd's copy of the texture equal the page's over the strip and the sheet texel
  round it on every frame, and a frame that empties shelves uploads under a quarter of the
  texture); `testStaleRowReturns`: four upper rows at another size leave for 100 frames, their
  shelf is emptied meanwhile (the first row's old cell then holds other texels) and they come
  back with the same letters, no word in the plain font (`menu_text`).
- `settings_test.c`, `settings_extra_test.c`, `settings_extras_test.c`: the Settings menu over
  fake tables, CPU only (`settings`, `settings_extra`, `settings_extras`); `settings_test.c`'s
  `testEffects` steps Motion blur round Off, Original (the default) and Smooth, each step saved as
  `effect_motion_blur` and `effect_motion_blur_mode` with the getters at once, the note under the
  row and the page fitting from both menus; `settings_test.c` also
  `settings_extra_test.c`'s `testGraphicsApi` (Options > Graphics: the link hidden with no renderer names and with one, shown after Effects with four; the Graphics API row's value text and the `video.backend` key through Right round Vulkan, OpenGL, Direct3D 12, Direct3D 11 and Automatic, Left wrapping, the key saved after leaving the page, an unlinked value shown as written and stepped from Automatic) and `testGraphicsApply` (Graphics > Apply over a fake `UiGraphicsHost` running `vulkan`: Apply hidden without a host, for Automatic and Vulkan, with one renderer, shown for OpenGL with its note; Cross asks for `opengl` once; the countdown's Keep and Revert alone with the cursor on Revert and the note counting 10 then 9 s; 600 `ui_settings_vsync` calls at 60 Hz ask for `vulkan` with the timeout reason and put the value back to Automatic; Keep calls `kept()` and saves `[video] backend = "opengl"`; Revert and Triangle ask for the backend that ran and put the value back; a failed answer's popup names both renderers and puts the value the page found back (no Apply then); a refused one shows nothing and keeps the choice; a pending one is polled every frame with Cross ignored until it answers; `ui_settings_save_on_quit` during the countdown writes the value the page found, the countdown going on, and Keep then saves the choice; Automatic with the host's `automatic()` naming OpenGL offers no Apply while OpenGL runs and offers it on Vulkan; the strings translated in every language) and `settings_test.c`'s `testBuild` (the Main page's seventeen rows and the Graphics page's five: Graphics API, Apply, Keep, Revert, Back); the Main page's row lists in the settings tests read 20 rows; `settings_extra_test.c`'s Graphics driver case (`testGpuDriver`, a fake host) checks the seven answers' popups (picker not opened, no picker app, file unreadable among them) and the Add from the drivers folder row: hidden at 0 zips, shown at 2, Left and Right going round the names, Cross calling `installFileBegin` once for the shown zip with a second Cross ignored while it runs, the poll's popup; presses Right in photo mode through Deja vu and checks the popup's words (`ui__photo_film_word`)
  and holds the fixed row lists of Controls (14 rows) and Touch screen (9 rows);
  `settings_extra_test.c` opens the Touch screen page (`testTouchPage`: the link only with a touch screen, the three overlay rows' values and save, both pages fitting from both entries) and the Edit layout page over a fake editor host and a real editor (`testTouchEditorPage`: open, Back to the Edit layout row with the edits kept, locked while Touch controls is Off or without a host, Snap to grid and Grid size saved as `touch.snap` and `touch.grid`, Reset layout and Reset all layouts), and steps Controls > Hold type from the title and the pause menu, checks the saved
  `gameplay.hold_type` and that `ui_settings_install` sets `optionControlType` from it (`testHoldType`),
  steps Effects > Water reflections between Original and Sharp, checks its
  note, its key and the page's fourteen rows (`testWaterReflections`), and Main's Disc row
  (`testDiscRow`: shown with more than one disc's game data only, its values and `[game] disc`, the page
  with all fifteen rows), and Main's Save a picture for a bug report (`testReportPicture`: shown with
  a host handler only, with developer mode off and on, directly above Developer mode, Cross calls the handler once and
  shows its popup, the page with all sixteen rows, the label fitting and translated in every
  language), and Share a bug report (`testShareReport`; in the default build `rc_tools_off`: on a
  phone with a share handler and every other row shown, Main has its sixteen rows, none of them the
  row and no English string its text; with `ICO_RC_TOOLS` the row below Save a picture, its note,
  Cross calling the handler once, hidden on a desktop and without a handler, seventeen rows from
  26, the strings fitting and translated). Each also runs with `jpn`
  as the Japanese or Korean disc over the fixture's tables at that disc's numbers (`settings_jpn`,
  `settings_extra_jpn`, `settings_extras_jpn`; `kor` the same, `settings_kor`, `settings_extra_kor`,
  `settings_extras_kor`, with the numbers from the Korean region row): the rows it has not, Menu language, Circle
  confirming, Cross and Triangle going back, the title's rows, the notes' serial, the mouse,
  the prompts and the boot's hand-over to the title with Skip the opening
  (`testSkipOpeningTitle`).
- `settings_photo_test.c`: the Settings screens through rd on a device, written as PNGs
  (`settings_render`, which also draws the Touch screen page and the editor page); with `jpn` or `kor`, that disc's title and main page (`settings_render_jpn`,
  `settings_render_kor`); `cn` the same for the Asian disc (`settings_cn`, `settings_extra_cn`,
  `settings_extras_cn`, `settings_render_cn`, `ui_cn`, `gallery_cn`, `model_viewer_cn`); `ntsc`,
  `aug6` and `jun26` run settings_extra_test's Game variants page only (`settings_extra_ntsc`,
  `settings_extra_aug6`, `settings_extra_jun26`: Cross confirms, the 2001 notes, the demo's 7-hit
  barrier, its toughness row without American and no Debug menu row).
- `strings_complete_test.c`: every language's table defines every `UiStrId`, none empty but
  `UI_STR_NONE`, each with the English entry's printf conversions; the four pack notes filled
  with each disc's serial (`ui_str_serial`); the sheet language's default and setter; the menu
  language's rule (`ui_lang.c`, its two inputs stubbed); the Japanese line-break rules
  (`strings_complete`).
- `ui_mouse_test.c`: the pointer's arithmetic in the menus (`ui_mouse`).
- `ui_test.c`: the embedded fonts, UTF-8, the runtime text; each language's own Deja vu string
  (`UI_STR_PHOTO_FILM_DEJA`); a Japanese popup body wider than the panel takes a second line, an
  English one keeps its one; the popups' timing in seconds at the default 50 Hz and at 60 Hz
  (`ui_popup_set_vsync_hz`) (`ui`); with `jpn` or `kor`, the glyph rows, the table sizes and the pad
  convention on that disc (`ui_jpn`, `ui_kor`).
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
`rd_blend_selftest.cmake`, which matches the `blend: ... (self-test: ...)` line of the blend's,
and `rd_vuprepass_selftest.cmake`, which matches the `VU pre-pass: on|off (self-test ...)` line
of the pre-passes', and `rd_crack.cmake`, which matches the crack filler's `crack fill:` lines,
and `rd_vustage_ab.cmake` over the dump corpus (`port/render/CMakeLists.txt`). The ctest `rd_replay_tool` renders `rd_pixel`'s dump with
`--present 640x480`; it requires `rd_pixel` to have run first (`FIXTURES_REQUIRED rd_dump`).

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
`rd_crack_tiler`, `rd_crack_tiler_on`, `rd_blend_selftest*`, `rd_date_selftest_sabotage`,
`rd_fog_selftest_local_sabotage`; `ico_rd_gl_twin` skips these itself), the tests that start the
game or work on the replay tool's files (`rd_replay_tool*`, `rd_fog_depth_copy_dump*`,
`rd_vustage_ab`, the frame gates, `coverage_corpus`; the compare below covers the dumps), and the
tests with no device (`rd_alpha_plan`, `rd_fx_frame`, `rd_vu_frac`, `rd_mip`, `rd_passring`,
`rd_filter`, `rd_bisect_plan`, `rd_stream_audit`, `fog_lut*`, `gsb_cull*`, `texpack*`, `gltf`).
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
| `ICO_VK_FAKE_LIMITS=moltenvk` | `vk_device.c` | MoltenVK: `RhiLimits.moltenvk` set, no attachment read, the interlock the device's own (`rd_blend_selftest_moltenvk_storage`: `ICO_RD_BLEND=storage` keeps the interlock; run without `ICO_VK_FAKE_NO_INTERLOCK`; it skips on a device without the interlock, as Mesa before 25.2's lavapipe) |
| `ICO_VK_FAKE_ADRENO` | `vk_device.c` | Qualcomm's Adreno driver: the attachment read not offered (`rd_blend_selftest_adreno`) |
| `ICO_VK_ATTACHMENT_READ` | `vk_device.c` | 1 offers the attachment read under `ICO_VK_FAKE_ADRENO` too (`rd_blend_selftest_adreno_forced`) |
| `ICO_VK_VALIDATION` | `vk_device.c` | the validation layer; the render and RHI tests set it with `VK_ADD_LAYER_PATH` when the layer is in the toolchain |
| `ICO_VK_GLOBAL_BARRIERS` | `vk_cmd.c` | a global barrier before every pass instead of hazard tracking, for A/B runs |
| `ICO_RD_FOG_PATH` | `rd_fog_path.c`, `rd_profile.c` | `local` (the profile's `fogLocal` on where the device has local read), `inplace`, `copy` or `buffer` forces the fog's depth path; `test` makes the self-test try every path (`rd_fog_local`, `rd_fog_selftest_local`) |
| `ICO_RD_DATE_PATH` | `rd_profile.c`, `rd_date_selftest.c` | `stencil` DATE through the stencil bit wherever the device has local read, kept whatever the self-test finds; `snapshot` the R8 snapshot (`rd_date_selftest`) |
| `ICO_RD_BLUR_FOLD` | `rd_profile.c` | 1 folds the staticBlur pairs (`fx_blur2_ps`) on any device, 0 never (the A/B of the replay tool's `--pass-dump`) |
| `ICO_RD_DEPTH_COPY` | `rd_fog_path.c` | 0 samples the depth in place, 1 copies it |
| `ICO_RD_FOG_SABOTAGE` | `rd_fog_path.c` | makes named paths fail the self-test (`rd_fog_selftest_fallback`; `local`: `rd_fog_selftest_local_sabotage`) |
| `ICO_RD_DATE_SABOTAGE` | `rd_replay_tiler.c`, `rd_date_selftest.c` | 1: DATE's setup writes 0 into the stencil bit where the alpha's MSB is 1, so the self-test fails (`rd_date_selftest_sabotage`) |
| `ICO_RD_BLEND_SABOTAGE` | `rd_blend_selftest.c` | makes named blend modes read white in the blend's self-test (`rd_blend_selftest_sabotage*`) |
| `ICO_RD_VU_PREPASS_SABOTAGE` | `rd_vuprepass_selftest.c`, `rd_replay_vu.c` | 1: the pre-passes' self-test draws its first command's vertex records a texel off; `tri`: its triangles' slot A a slot off; `ticks`: the tick stage's tick 1 at tick 0's camera; each fails it and the pre-pass is turned off (`rd_vuprepass_selftest_sabotage`, `_sabotage_tri`, `_ticks`) |
| `ICO_RD_VU_PLAIN` | `rd_pipeline.c` | 1: every VU draw with the VU vertex shaders' plain variants, as on a device of the table (`kVuPlainOnly`); the replay tool's pictures at a wide aspect then show the plain-only picture ([RENDER.md](RENDER.md#the-fallback-entries)) |
| `ICO_RD_VU_EXTENDED` | `rd_pipeline.c` | 1: the extended variants whatever the device table or `ICO_RD_VU_PLAIN` says |
| `ICO_RD_VU_PREPASS` | `rd_replay_vu.c` | 0: no VU vertex pre-pass, every VU draw the old way (`[dev] vu_prepass = false`); the tests switch it with `rd__set_vu_prepass` ([RENDER.md](RENDER.md#the-vertex-and-triangle-pre-passes)) |
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
between them); the zip exists because issue trackers refuse
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
`gameplay.yorda_safe`, `gameplay.developer_mode`, `dev.start_stage`, `gameplay.door_fix`,
`video.anti_aliasing`, `gameplay.hold_type` and `gameplay.skip_opening` (the boot's ticks), the eight `variants.*` keys (the barrier's is `(unset)` until chosen), and the native frame's width and aspect (`port/platform/main_host.c`,
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
`font_audit`, `font_subset_ja` (`tools/subset_font_ja.py --check`: the Japanese font's recorded
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
phi, select or copy) and no vertex shader keeps a local array
([SHADERS.md](SHADERS.md#the-envelope)); the log lists each module's words, instructions, blocks,
phis, loops, dot products, vector comparisons, boolean selects and `any()`. The vertex pre-pass's
entries (`vu_*_pre_vs`) need no plain variant and take the plain budget. `python3 -I tools/check_spirv.py --sizes <build>/shaders` runs it by
hand.

## Which tests a change needs

- A new Options row: `ctest -L ui`. `settings` builds every page and compares its rows with fixed
  lists (`port/ui/test/settings_test.c`, `testBuild`), so a new row updates those lists; new
  settings cases go in `port/ui/test/settings_extra_test.c` (its header). `settings_render` draws
  every screen, `menu_text` the rows in the sheets' look, and `font_audit` (every code point inside the
  font subsets, `tools/font_audit.py`) and `font_coverage` (a glyph for each) check the row's text
  in the eight languages; `strings_complete` fails until every table has the new strings. A new
  Japanese character needs `tools/subset_font_ja.py` run again (`font_subset_ja` fails until it
  is), a new Hangul syllable `tools/subset_font_ko.py` (`font_subset_ko`), a new character of the Chinese
  table `tools/subset_font_zh.py` (`font_subset_zh`). A gameplay option also needs `options`
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
  no local array in a vertex shader) and `hlsl_fxc_check` (`python3 tools/check_hlsl_fxc.py`: no word
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
| `ci.yml` | `linux` | `ubuntu-24.04` | the hygiene checks (no disc data, format, no `ICO_HOST` conditional, no region block, stack-layout, struct-cast and call-type audits, generated files fresh), then `linux-x64` headless and window builds with `ctest`, `linux-x64-clang` with `ctest`, and the `win-x64` cross build (tests built, not run) |
| `ci.yml` | `extra` | `ubuntu-24.04` | a matrix of `asan` and `fptrap` (build `all tests`, `ctest`) and the `win-x64-clang` cross build |
| `ci.yml` | `linux-arm64` | `ubuntu-24.04-arm` | the no-disc-data check, the `linux-arm64` preset with `ctest`, the filtered-pixel render tests in a step that does not gate, the arm64 package on release tags |
| `ci.yml` | `android` | `ubuntu-24.04` | the debug APK (`assembleDebug`), `tools/check_android_flags.sh`, the APK structure checks |
| `ios.yml` | `build`, `publish` | `macos-15`, `ubuntu-24.04` | the unsigned iPhone IPA for a published release, attached to it |
| `ios.yml` | `build-macos`, `publish-macos` | `macos-15`, `ubuntu-24.04` | the macOS app for a published release, attached to it |

(`.github/workflows/ci.yml`, jobs `linux`, `extra`, `linux-arm64`, `android`;
`.github/workflows/ios.yml`.)

The triggers, steps and caches are [../BUILDING.md](../BUILDING.md#continuous-integration)'s.

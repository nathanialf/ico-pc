# Test programs by area

Every test program of the port, one entry each, by the directory it lives in: what it checks and
its ctest names. The framework, its labels and shards, the render gates, the suites per backend,
the tests that need the disc, the environment switches, the dumps, the audits, which tests a
change needs and CI are [TESTING.md](TESTING.md)'s.

## Reading the entries

One line per program, from its first comment; the ctest names follow where they differ from the
program's. Variants (`_nodual`, `_mali`, `_minlimits`, `_storage`, `_fixed`, `_d24s8`, `_static`) rerun a
program under an environment switch ([Environment switches](TESTING.md#environment-switches)). `_static`
(`rd_pixel_static`, `rd_mesh_static.1of4`: `ICO_VK_FAKE_NO_DYNAMIC=1`) draws with every pipeline
static, so the start-up set's membership checks (`rd__pipe_key_equal`) compare whole keys, as on
D3D12 and a Vulkan device without the dynamic state; `rd_mesh_static.1of4` is `rd_mesh`'s shard 1 of
4, the one with the `gpu_prims` case.

## port/audio/test

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

## port/config/test, port/null/test, port/save/test

- `config_test.c`: `config.toml`'s reader and writer, round trips, the ini over `config.toml` over
  default precedence, the first-run template (Hold type `disc`, no `hold_type_chosen`, `show_fps = false`, `effect_motion_blur_mode =
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
  own 0x18C-byte record (each file summary the host record's first four words in order) and a
  25,596-byte game block with no option words (`save_ntsc`,
  `save_aug6`), and the June 2001 demo's `BISCUS-97113ico` with the preview's record, a 20,480-byte
  game block and icon files that end in a 4-byte sum and are written only when absent
  (`save_jun26`); each imports a card holding its directory and one other disc's side by side and
  the other disc's `.psu` beside their own ([SAVES.md](SAVES.md#tests)). Part B lays each disc's record out byte by byte
  and pins every row's `mc_save_dir` to the game's own `iOSMcSaveSeg_*` name
  ([SAVES.md](SAVES.md#the-seven-discs)). Part B's `second_card_by_option` drives port 1 through
  Options > Gameplay > Second memory card's listener as `main_host.c` registers it: On puts
  `<user folder>/memcard2` (`memcard2-preview` on `save_aug6`) in port 1 at once, the game's card
  check finds it formatted with no save, a save lands there and nowhere else; Off empties port 1
  (the game's "no card", the file kept); On again lists and loads the save. Each run removes only
  its own disc's directory there (the seven runs share the folder). A load reads `optionControlType` from `[gameplay]
  hold_type`, not from the card's `controlType` (no key: the disc's own, 0 on the four A discs, 1
  on the 2001 discs; `toggle` 1 and a chosen `hold` 0 on every disc); on the three 2001 discs the
  load does not touch `optionScreenMode`, `girlControlMode` or the record's `fileNo` and `serial`,
  which the card does not hold.
- `state_recover_test.c`: the rescue of an old save state's progress as a memory-card save, with
  the disc its `unit` argument selects (`state_recover`, `state_recover_jpn`, `_kor`, `_cn`,
  `_ntsc`, `_aug6`, `_jun26`): the side file, the globals and their refusals, the card writes
  through `mcard.c` laid out byte by byte and read back through the game's loader; and for the
  European disc the script's chain: `files` writes three builds' state files with the state writer
  and their symbol tables (an ELF library in a package, GNU ld's ELF and PE maps, a functions-only
  nm table), `tools/state_recover.py` reads each (`state_recover_py_apk`, which also writes the
  card through `state_recover_tool`, `_py_map`, `_py_win`; `_py_refuse` the nm table), and `check`
  compares the side files and the card save (`state_recover_files`, `state_recover_check`)
  ([SAVES.md](SAVES.md#recovering-progress-from-an-old-save-state)).

## port/core/test

- `region_select_test.c`: the game region (`port/core/region.h`): each of the three slugs selects its row and
  `ico_region` follows, an unknown slug or none keeps the selection; the disc ids, the per-disc
  config keys and the stage rows on each disc; `ico_region_info[]` against `config/regions.txt`
  (the program's argument) row for row, and the disc table `ico_discs` (`port/data/discs.c`,
  compiled in) against the same rows: slug, disc id, boot file, product name, archive and image
  names; every value of the Japanese row, the Korean row's resolved ids and fields, every field of
  the North American, August 2001 and June 2001 rows (pinned), and `ico_region_stage` on the 2001
  discs (the demo's own order by stage name), and every row's card preview words
  (`mc_preview_play` and `mc_preview_sofa`: 2 and 3 on the European program's discs, 1 and 2 on
  the 2001 discs) (`region_select`).

## port/data/test

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
  RAM layers, and `ico_vfs_reopen`'s second mount read interleaved with the first, and two
  threads reading one mount 1000 times each under its mutex, every read equal to one thread's
  alone (`test_two_threads`) (`vfs_synthetic`, `vfs_disc`, `vfs_disc_jpn`, `vfs_disc_kor`);
  `same <iso> <image>`: the same directory records, boot ELF and DATA.DF SHA-1s and image SHA-1
  in both.
- `cdvd_host_test.c`: the disc reader thread ([DATA.md](DATA.md#the-disc-reader-thread)) over a
  fake backend whose reads sleep and note their thread: `sceCdRead` returning before a 30 ms
  read ends, the command busy until the vsync and complete there (the buffer, the error, the
  wait), a second read refused while busy, `sceCdSync(0)` from the host context, the errors final
  when busy clears, `sceCdBreak` (ABORT, the buffer whole, nothing written after), the
  read-ahead's hits, misses and end clamp, the stream's hits, the idle gate, a reset with a read
  on the thread, the sync reader (`cdvd_host`; under the TSAN preset where it is built).
- `sha1.h` is the tests' SHA-1.

## port/fmv/test

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

## port/game/test

- `achievements_test.c`: the game-state view over a synthetic snapshot, every achievement's
  condition, suspension, `achievements.toml` round trips, the popup gap (125 Main ticks at 25 Hz,
  150 at 30 Hz) and every disc's keys in one file (`stats.stages` / `stats.stages_jpn` / `stats.stages_kor`,
  `run.slot_N_*` / `run.jpn_slot_N_*` / `run.kor_slot_N_*`, one set of unlocks), with the disc its
  second argument selects (`achievements`, `achievements_jpn`, `achievements_kor`); the play time
  and the save's sofa are written to each disc's own preview words (literals: `[2]`/`[3]` on the
  European program's discs, `[1]`/`[2]` on the 2001 discs: issue 160).
- `appearance_disc_test.c`: the characters' colours against the disc's CLUTs (`appearance_disc`).
- `appearance_test.c`: the recolour on synthetic CLUTs: identity, coverage, swatches, randomize
  (`appearance`).
- `credits_test.c`: `staffroll.c` over a short table with the port credit appended, and the
  credits row's lock (`credits`); with `jpn` or `kor`, the same on that disc's region: STAFF1 58,
  the song 47, the scene layout 52, the roll offset 151,050 ms (the Korean build is the Japanese
  program), the roll start's log line, which names the disc (`credits_jpn`, `credits_kor`).
- `unused_content_test.c`: Extras > Unused content's rows, flag and start and its engine over
  stubs of the game, DATA.DF and Settings, one run per disc (`unused_content`,
  `unused_content_<slug>` for jpn, kor, cn, ntsc, aug6, jun26): each row's stage, which rows the
  disc's DATA.DF has, the engine indirection, the test rooms' start (both rooms place the boy and
  the sword, and the girl in the room whose fake `stageData` `flag0` is set, at
  `-exitData[0].pos` with `exitData[0].rot` in radians: the fake `exitData[0]` is (12, -34, 56),
  rot 90, so a copy as stored fails; a fake entrance row into the climbing stage, row 7, is not
  used; each start's line; the boy's walk in skipped once per room, after the way in, by the
  stubbed `ico_host_skip_entry_walk`), the boy's lines over a fake `boyGObj` and stubbed `GetRootPosition` /
  `GetLowerPlaneCollision` (no boy as the stage is up, the floor and its drop, no floor, the
  30-tick step, none past 300 ticks, the line at a game over), the camera-zone rule over a fake
  camera set (`TopCameraSetDataOfCurrentStage`, groups stored negated) and a stubbed
  `SetDirectRootPosition` (the arena's start inside its zone: no move, the inside line, a group
  stored at the root itself not taken, so an un-negated test fails; the climbing room's start
  outside every zone: of a zone whose floor lies below its box, two with the same box bottom and
  different floors, one higher and one with no floor, the larger floor plan of the lowest wins,
  every zone probed once from its box's top, the boy at the floor minus his fake skeleton's root
  height 72 (`GOBJ_SUB(boyGObj)->skel` node 0's `pos[1]`, the entrance height) with his root ON its
  floor (a measured 0) and with it 71 above (a measured 71: still 72), the girl at her own 60,
  moved with him when up and not when `girlGObj` is NULL, the move's line with the probe from the
  moved root, the tick line after it with the camera's zone, eye and look-at (a stubbed
  `ico_host_camera_view`) and his draw mask; the big floor or no floor under the moved root
  (`s_underMoved`): the "not the zone's" line and no second move; no zone with a floor: he stays, its line; no camera set: its line; no boy
  in the 300 ticks: the never-ran line), the return to the title with its log and the reopen of
  Options, the failures; the art row refused when the model viewer offers none of its rows, and its start
  opening the viewer's list on the viewer's first art row (a stubbed `ico_mv_models_enter_at`),
  the list's refusal and a start off the title refused, each logged.
- `charfile_image_test.c`: the early show title art's hook in the game
  (`ico2/common/src/charFileManager.c` `ico_char_file_image`) by its text, no game linked
  (`charfile_image`, the file and `ico2/fumi/ios/cdvd.c` as arguments): each of its four twins is
  its loader's body (`ReadModelFile`, `ReadModelFile_jun26`, `ReadTextureFile`,
  `ReadTextureFile_ntscaug6jun26`) line for line but for the read from the cdvd handle (a copy of
  the image), the two skips (nothing) and the call of the 2001 or demo arm (the twin's); the four
  loaders read the handle eight times; no line of the block reads a handle; the dispatcher's
  extension lookup over `initFunc`'s 26 rows, its two loaders told by address and its load count
  in order; and the pack loop it stands in for (`iosCdvdMgrPackLoad`: `getPackLoader`, the row's
  function with the directory's words and the pack's segment, `debugCdvdLoadInfoSegAdd`) as it is.
- `unused_movie_test.c`: the attract movie row (`unused_live.c` `beginMovie` and its poll) over
  stubs of the game, DATA.DF and Settings, one run per disc (`unused_movie`,
  `unused_movie_<slug>` for jpn, kor, cn, ntsc, aug6, jun26): on the European disc the start from
  the title (the return stage, the title theme's fade, the stage switch to stage 59 with op.c's
  256 / 4 fades, the flag, no state kept), the stage up, the return as "movie ended" or "skipped",
  the reopen of Options, the failed starts and their bounds; on the other discs the refusal
  before the engine.
- `camera_wall_test.c`: the blended camera's wall rule (`port/game/camera_wall.c`) without game
  sources: a segment against one wall (through it either way, past its ends and over its top
  within `ICO_CAMERA_WALL_RADIUS`, a sloped bottom, an end on the plane) and one floor (down and up
  through it, its rim, a triangle); the rule at each flip: no eye before, a crossing, a free path,
  the reverse crossing, a pending cut and photo mode answered without asking, a still eye and a step
  past `RD_INTERP_CAMERA_MOVE` not asked, a forgotten or non-finite eye (`camera_wall`).
- `camera_wall_stage_test.c`: the live wall check (`port/game/camera_wall_stage.c`) over a
  synthetic stage of the game's records in a stand-in arena: a wall placed by its node's
  translation, a floor under a turned node (the same coordinates read without the turn miss it),
  a per-node set's second node, the collision list's filter, the wall attributes `ClipWall`'s mode
  skips, records outside the arena not read, the arena and the object table byte-identical after
  every check, the `camera:` line and its Frame rate gate, a pending cut, photo mode and forgetting
  (`camera_wall_stage`).
- `collision_carrier_test.c`: a display object whose model has no parts but which carries a
  collision (`ico2/common/src/DObj.c` `initPolygonState`'s PC-port block), with DObj.c, the
  allocator over the EE RAM arena, the field collision and the matrix stack and tables compiled in
  and the character files, the motion set-up and the object table stubbed. `synth`: the carrier
  gets one 64-byte node matrix holding the layout's (identity; a moved and turned row's), its
  `dobj:` line names the model, the layout row (-1 when the newest object already has a display
  object) and the collision path, and a floor probe hits its floor (y 0; the moved row's y); the
  same object with its collision attached after the create (the original's state) reads the next
  block header's tag as its matrix and no probe finds the floor; a one-part model with a
  collision, a 0-part model without one and a 0-part model with a skeleton allocate the same
  blocks as without the collision, the carrier those plus one of 64 bytes (`collision_carrier`,
  `collision_carrier_ntsc`, `collision_carrier_jun26`). `disc`: DATA.DF holds exactly one model
  with no parts, `STG8TEST.DF`'s `st08a_p1.p2o` with the disc's model id (none on the June 2001
  demo), and the room's `st08a.cl` on its carrier finds y 0 at the origin and y -1323.67 from camera
  zone 20's box top, neither with the collision attached after the create; skipped (77) without
  the image (`collision_carrier_disc`, `collision_carrier_disc_jpn`, `_kor`, `_cn`, `_ntsc`,
  `_aug6`, `_jun26`).
- `door_grace_test.c`: the doorway fix's arm and grace and a copy of the exit check's guard
  (`door_grace`).
- `ico_rows_test.c`: Developer > Free movement and the Ask Yorda for a hint button
  ([GAME_FEATURES.md](GAME_FEATURES.md#free-movement-and-ask-yorda-for-a-hint)) on the CPU: the key
  off by default, the row on without Developer mode doing nothing and with it on, the setter, a
  reload reading the key again; the hint button's press counted once however long it is held, one
  fire for each press, a press older than 12 snapshots dropped and not kept, two presses before one
  look as one fire (`ico_rows`).
- `yorda_trip_test.c`: Yorda trips ([GAME_FEATURES.md](GAME_FEATURES.md#yorda-trips)) on the CPU,
  the rule (`yorda_trip.c`), the game's side (`yorda_trip_live.c`) and the motion reader
  (`yorda_trip_source.c`) compiled in over a stubbed game, disc and pack index: the disc's run
  loop, fall and per-stage HETARI motions found by name (decoys left out) and the fall's exits from
  its motion-orient rows; the run count, a repeated tick, a gap and a stamp behind the last; the
  main gate scene's rule at 25 and at 30 ticks a second (no trip in the first 3 s of ticks, the trip
  on the tick after, the 76th or the 91st, the count back to 0 and the next exactly 3 s later, a
  gap or a boundary with a guard failing starting the 3 s again, the caller's mail); no trip over
  20 boundaries while any one guard fails (paused, no control, a script hold, game over, a gate
  stage, another motion, the fall or an exit not loaded, this disc's HETARI bit, the option off),
  one trip a boundary with the other discs' bits set; the two save state blocks by name (none of
  the old random ones, both FRESH) and their bytes put back mid-count resuming the count; the stage load's copies (nothing with the option off,
  in a gate stage or where the stage has them; the pack's bytes relocated and counted on the stage
  segment; the pack read once and picked by its members; nothing in a partition too full); one
  argument selects the disc (`yorda_trip`, `yorda_trip_<disc>`).
- `yorda_trip_disc_test.c`: the same motions on the disc image the build names: the disc's tables
  from its ELF, the names and exits, the main gate stages' packs and their HETARI members with their
  sizes, the copies the game's side reads; nothing starts the game; 77 without the disc
  (`yorda_trip_disc`).
- `pit_dive_base_test.c`: Time to rescue Yorda's base on each disc image the build names: the
  disc's tables from its ELF, EN1 SINK_A to SINK_E by name at the numbers the `enemy_act.c` hooks
  use, each at play speed 1 (`playSpeedRatio`, `palSpeedRatio`), without a blend row and with flag
  bit 26 clear, the motion-orient chain (CARRY_LOOP's row of the dive's kind into SINK_A, that
  kind's rows A to E at each last frame, E holding), every `.mob` copy's frame count in DATA.DF's
  packs (all copies equal), and (A-1)+(B-1)+(C-1)+(D-1)+51 against `ico_pit_chain_ticks_for`
  (`port/game/pit_time.h`: 462, the June 2001 demo 463); nothing starts the game; 77 without the
  image (`pit_dive_base` for `ICO_DISC_IMAGE`, `pit_dive_base_<disc>` for each other image set).
- `weapon_kinds_disc_test.c`: Gameplay > Combat's weapon rows on each disc image the build names:
  the disc's tables from its ELF, then with Red and Insect net the kinds `ico_variant_weapon_kind`
  gives against the disc's rows: weaponKind's row count (10, 8 on ntsc and aug6, 7 on jun26), the
  stick made as the net's kind (7, 6 on jun26), the net's row (length 120, grip 40, power 0, the
  stick's weight and trail, no flags, request 0), no motion-orient row of request 0 (the swing
  never starts: the table's last row, the "no motion" row that leads to itself, answers it) while
  the stick's request has rows, rows 8 and 9 the same bytes but the trail colours (where the light
  sword exists; elsewhere Red not offered and kind 9 untouched), objLayout's weapons (sticks placed,
  the net never, the light sword placed on the four discs that have it), no other kind moved;
  nothing starts the game; 77 without the image (`weapon_kinds_disc` for `ICO_DISC_IMAGE`,
  `weapon_kinds_disc_<disc>` for each other image set).
- `env_bits_test.c`: where each disc's wall-reach read in `act-env.c` takes its two bits of a motion record (a shift of 6 on every disc, the host layout), every `config/motion_bits.*.txt` row naming the field at bit 24 for two bits, and a synthetic word read through the shift (the June 2001 demo once read the item-mode bits) (`env_bits`).
- `keep_flags_test.c`: the twenty words a load's keep and restore walk reads on each disc, exactly as that disc's objects have them (issue 160's new-game flag), with the key-config slots as at start and after a Button configuration visit; one argument selects the disc (`keep_flags`, `keep_flags_<disc>`).
- `eye_trail_test.c`: the shadows' eye trails rebuilt from their world points
  (`port/game/eye_trail.c`) against the game's own point blur (`enemyParts.c` compiled in) on each
  disc: after 12 ticks of a moving point the rebuild through the same matrices is the game's
  strip byte for byte and the strip drawn with the camera not moved is the game's; under a moved
  and zoomed camera each pair (the oldest's included) is the direct recomputation, frame-projected
  in the window build, with the game's strip and the current matrix untouched; the reset path and
  the disc's every-tick reset rule; the first update, a forgotten table and the session's end
  (`eye_trail`, `eye_trail_<disc>`).
- `torch_work_test.c`: the torch record every torch starts from on each disc, played through the game's own torch code: a torch lit at its layout stays lit for 600 frames and runs the four flame effects of the default type, putting it out deletes exactly those and no other effect, an unlit torch stays out; one argument selects the disc (`torch_work`, `torch_work_<disc>`).
- `state_host_test.c`: save states' decisions without the game (`port/game/state_rules.h`,
  [PLATFORM.md](PLATFORM.md#saving-and-loading)): every refusal of the conditions reachable and
  their order, a load without the pause, the pause menu and a port page over it; the log texts;
  the slots' paths; the `[dev]` keys' values; the listing of a missing, an empty and a filled
  folder (each validity field changed, a damaged file, a picture); the thumbnail's size (the pause
  picture's square pixels to 960 wide, a tall one to 960 lines, the fallback DISPLAY frame at the
  box's shape) and box filter; the pause picture's path, its copy to a slot's picture and its steps
  (taken once from play to the pause menu, kept over the pages, dropped on play); after a load the
  loaded slot's picture copied to `pause.png` with its size from the PNG header (none without a
  picture, no size for a header that is not a PNG's) and on to a later save's slot (`state_host`).
- `variants_test.c`: the game variants (`port/game/variants.h`) on the CPU: every default is the game as
  shipped (the barrier's number the disc's: 5, 7 on `jun26`), every `[variants]` key steps through
  all its values, saves and reads back, a word or number that is no value reads as the default, the
  setters clamp and the steps wrap, and the achievements rule (One hit per swing, Shadows never
  dodge, Cutscene cameras ignored and a Debug menu press suspend; toughness, aggression, No hints,
  the barrier and the look-around do not; Peaceful shadows does, as `peaceful_shadows`; a reload clears the Debug menu's mark), `test_peaceful` (the
  `ico_variant_shadow_brain` hook the four Shadow starts read: 1 by default, 0 with the key on, a bad
  word reads 1, a step writes the key and the achievements rule, the other rows untouched),
  the chain rows (`test_chain`: the game's 4 and 4 by default, `ico_variants_bind_debug_chain` leaves
  the switches alone at 4, every value 1 to 10 reaches its own switch, 4 puts the game's value back,
  a step wraps and writes the number, a file's value is written at the bind, 0, 11 or a word read as 4,
  nothing written unbound), Shadow toughness Third journey (`third_offered`: the step order round the offered values only,
  the key `third_journey`, a config word or a set of it giving Original on `ntsc`, `aug6` and
  `jun26`, `restart_index` 40 on every playthrough where offered), and the game
  hooks: the bound debug switches (untouched at the shipped values, each variant's value while it
  is on, the aggression's four battle types, the game's value put back when it is switched off or
  reloaded, written at the bind from a file, nothing written unbound), the Debug menu's opener
  (bound: opened, 1; unbound: 0), `ico_hints_on` per disc, the enemy row reader on
  `ico2/sugipon/src/enemyKind.c` (compiled in: the disc's own rows with nothing chosen, American on
  the European table = the USA life with the European dodge, model and parameter row, "no model"
  rows whole, aggression's battle type on every row, never past a table: the June 2001 demo's 18
  rows, never mapped by index), the restart life's playthrough index (a copy of
  `getEnemyRestartLife`'s: 38 with American or Not tougher later) and `test_offered` (American is
  not offered on `jun26`: the config, the setter and both step directions; Light sword colour Red
  offered on pal, jpn, kor and cn only) and `test_weapons` (the kinds `ico_variant_weapon_kind` and
  `ico_variant_weapon_put_kind` give: Red 9 to 8, and 8 back to 9 wherever Red is offered (Red on, and Red switched off with a kind-8 sword in play; 8 stays 8 on a disc without Red), the net's kind 6 on
  `jun26` and 7 elsewhere for the stick, every other kind untouched, the words, the step, a reload);
  one run per disc (`variants`, `variants_jpn`, `variants_kor`, `variants_<slug>`).
- `hand_ik_test.c`: the hand records as `ACTGame_ConnectHand` sets them through the real
  `handManager.c`: every target finite, a NaN into `GetTableArcCos` giving 0 and a counted fault
  (`hand_ik`).
- `ico_frame_test.c`: the [native frame](ARCHITECTURE.md#terms)'s widths for each aspect and the
  options' frame (`ico_frame`).
- `jimaku_text_test.c`: `jimakuDisp`'s community-translation hook (`jimaku.c` compiled in, the hook's
  two port calls stubbed): with no words the record's two strips as before, with words one packet in
  `display_texture`'s state and colour around the text call and no strip, the semaphores and the
  display time alike, nothing in photo mode, with subtitles off or without the picture; the Japanese
  and Korean discs' shared arm asks too, the other discs' own arms never (`jimaku_text`).
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
  sizes, its names in five languages (`model_viewer`); its three member rows (the early show
  title art) over a fake of each disc's logo pack directory, read from the ISOs
  (`model_viewer_rows_<slug>` for jpn, kor, cn, ntsc, aug6, jun26; `model_viewer` is the European
  disc's): kind 35 on the Cage's stage with no motions, each id its member's `PackEnt.id` (1595
  for the copyright on pal, 1577-1579 jpn/kor/cn, 1544-1546 ntsc, 1539-1541 aug6), the rows
  offered (one on pal, three on the other retail discs, none on jun26) and the first art row,
  nothing asked with no disc mounted, each asked once per disc and again after a region change,
  and a member found in another pack not offered; its Japanese model column is checked by
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
- `state_roundtrip_headless.cmake` with `state_roundtrip_pad.txt`: save states in the headless game,
  started in stage 22 (`[dev] start_stage`). `state_roundtrip_headless`: Start opens the pause menu
  at tick 400, `[dev] state_save_at=440:1` saves slot 1 there, the script closes the menu, the boy
  calls the girl and the camera turns, `state_hash_at=700` logs the hash of the arena and the
  game's data and bss, `state_load_at=700:1` loads slot 1 (the Main tick and the script's cursor
  go back to 440), the same presses play again and the second hash at 700 must equal the first,
  with no `state: refused` line. The hash is of the game's state, the host's pointers and handles
  left out ([PLATFORM.md](PLATFORM.md#saving-and-loading), the hash rule); `state_dump_at=700`
  writes the three regions at each hash, and when the hashes differ the failure message carries
  `tools/state_region_diff.py`'s report on the two dumps: the first ten differing symbols of the
  game's data and bss and the arena's first ten differing ranges (the dumps are deleted on a
  pass). On a pass the output ends with the `state: hash masked ...` line.
  `state_roundtrip_headless_settings`: one run saves slot 1, a
  second run over a copy of its states folder with `[gameplay] yorda_safe = true` (a simulation
  key) loads it and must log `state: refused load slot 1: settings differ: gameplay.yorda_safe=...`
  and go on to its last tick. Both skip without the disc image and on a sanitizer build ([Tests
  that need the disc](TESTING.md#tests-that-need-the-disc)).
- `options_test.c`: `test_pit_time` (Time to rescue Yorda in seconds, issue 186: the pull-in's ticks per disc, the parser's seconds, words and range, the factor seconds x 30 / ticks and the ratios 1/factor and 2/factor for 4 to 30 s with the whole pull-in lasting the seconds, Original bit for bit, and the source scan: four hooks over each copy's five SINK motions, this disc's among them, the three camera timeouts, the frame-50 test); `test_shadows_together` (Shadows attack together, issue 185): off by default
  and from the file, `ico_opt_shadow_waits` 1 for a Shadow while another attacks with the option
  off and 0 with it on, the turns it skipped counted (200) and logged (the change line, the first
  8, then the 100th and 200th, the line back off), and the source scans: `enemy_act.c`'s eleven
  `EnemyUtil_isOtherStatus(self, 0)` callers, the game's scan renamed `EnemyUtil_isOtherStatus_disc`
  and called once by the wrapper that returns its result only when the Shadow waits, and the
  simulation key; `test_shadow_speed_motions` selects each of the seven discs and checks the motions Shadow speed scales (the walk, run, fly and carry loops by their motion-table names) and the ones it leaves (the stand, the fly start, the run's end, an attack, the grab, a hit, the dissolve, the pit dive; review finding 15); the gameplay options' defaults and setters (Hold type: the disc's own by default, `toggle`
  from the file, an unknown word as the disc's own; `test_hold_discs` selects each of the seven
  discs and checks the resolve: A on pal/jpn/kor/cn, B on ntsc/aug6/jun26 for no key, `"disc"`, a
  bare `"hold"` and `hold_type_chosen = false`, A for a chosen `"hold"`, B for `"toggle"`, and the
  store's two keys), `test_video_show_fps` (`[video] show_fps`: off by default, a file's `true`
  read, a run-time 7 sanitized to 1, saved as `show_fps = true`/`false` and read back, the preset
  untouched, Enhanced's shortcut leaving it alone), `test_video_changes_text` (the window's
  `display changed` words: none for equal options, Custom 2x auto to Original as `preset Custom
  -> Original, resolution 2x -> 1x, aspect auto -> 4:3`, the fields in order, cut at the size),
  `test_players_slots` (selects each 2001 disc and checks the `[players]` count kept
  per save slot and sum, a missing entry or another sum loading as one player, a later save
  winning, each disc's prefix in the file), the video effects' keys (Water
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
- `title_logo_test.c`: the logo's models and the hidden state's steps; the application icon's
  capture (`title_icon.c`) with the renderer, the curtain and the icon files stubbed: the pass leaves
  out every model but `I` (`title_back`, `st26a_p1`, `C`, `I_f`, `I_sd`) and off leaves the old
  table; the capture starts after `ICO_TITLE_ICON_WAIT` ticks of layout 12 or 13 (counted again
  after a Settings page or the model viewer) when an icon is wanted, never when not, and a picture
  not plain (`ICO_APP_ICON_NOT_PLAIN`) leaving it idle with the count from 0 (the capture starting
  once it is plain); the shot over black after the settle, white once it came, the shot over
  white, then the pass off, the pair delivered and the curtain off at the first present past the
  pass's frames (held over older frames and over a blend with the pass's last; the rule
  `ico_title_icon_present_clean` alone; after `ICO_TITLE_ICON_HOLD_MAX` ticks without such a
  present), done for the run; an empty answer or none within
  the wait armed again and the run given up after three retries; a pair that differs (or a picture
  of another size) taken again once, then the picture over black alone; the menu left during the
  pass ending it and the next visit starting again (`title_logo`).

## port/input/test

- `input_record_test.c`: the pad recording's format, round trip through the script reader, the
  change-only rule and the sample point (`input_record`).
- `input_test.c`: the `[touch]` tables onto the layout edits and back (the `format` key, a file without the key reading `hidden` and `o0` as Off, rewritten as `off`), bindings from config text, dead zone, quantisation, the stick fix, merging, the The hint target (`ICO_T_HINT`): unbound by default, `kb.hint`/`pad.hint`/`mouse.hint` from config text, `hint_down` from a key, a mouse button or a gamepad source (not with the gamepad off), the pad frame unchanged, one source one action with the hint, the row text (`input`).
  [libpad](ARCHITECTURE.md#terms) buffer, rumble, Escape, whether Start opens the pause menu (F6's
  tap: the play layout only, not a scene) (`input`).
- `mouse_camera_test.c`: the mouse camera's speed and range for the hand camera (`mouse_camera`).
- `pad_script_test.c`: the `--pad-script` reader and what the null pad then shows the game's pad
  code (`pad_script`).
- `touch_test.c`: the menu row tap's finger mark (`touch_eaten.h`: let go at its lift, when the touch editor opens or closes, and at a new DOWN of its id, so the next finger 0 reaches the controls); the touch overlay's layouts over a table of screens (`test/touch_shapes.h`), the
  sticks and buttons, every default layout's alpha 1 and `editGen` 0, an Off L2 pressing nothing,
  and `test_hidden_off`: a Hidden Start pressing Start and an Off one nothing, a Hidden stick and
  look pad starting and Off ones not, `ico_touch_takes`, `ico_touch_ctl_state`, and the menus taking
  taps only with the D-pad, Cross or Circle Off; `test_menu_tap_areas` on a 2340 x 1080 phone at
  420 dpi: with the disc's confirm (Cross, or Circle) Off a point in the look pad's area and one in
  the stick's area are taken by the mapper but do not block the menus' click, the stick's does
  while the confirm works (the D-pad Off), and a working or Hidden button always blocks (`touch`).
- `touch_scene_test.c`: Touch controls "Always, Start only in scenes" (`touch.c`
  `ico_touch_scene_alpha`, [INPUT.md](INPUT.md)): the other modes and a scene-free fourth mode
  leave every alpha, the scene fades every control without the Start bit (D-pad, faces, shoulders,
  Select, stick, look pad) to 0 over 300 ms both ways and keeps Start (a Hidden Start stays 0, a
  dimmer one keeps its alpha, selection is by the pad bit), the original layout is untouched, the
  mode maps and draws like Always, and the Frame rate counter's place on the faded copy equals
  that with the other controls dropped (`touch_scene`).
- `touch_gap_test.c`: the Frame rate counter's place among the player's actual touch controls
  (`touch_gap.c`, [UI.md](UI.md#the-frame-rate-counter)): the default layout's old place, Start at
  the top-left drawn, Hidden and Off, every control Off, no room on the row, the stick's resting
  ring (`touch_gap`).
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

## port/math/test

- `math_test.c`: the PS2 float helpers bit for bit, the R register, matrices, vectors and
  quaternions against references (`math`).
- `newlib_test.c`: `ico_rand`, `ico_qsort`, libm values in both FP environments (`newlib`).
- `softdouble_test.c`: `softdouble.c` against the EE's own soft float from `sce/libgcc`
  (`softdouble`); `dpbit_harness.cmake` builds that oracle on the host.

## port/platform/test

- `android_paths_test.c`: the Android file layout on any platform, the texture pack folder of
  each disc's serial, the `Ico_PAL`/`Ico_JPN`/`Ico_KOR` image names and the `drivers` folder (`android_paths`).
- `arena_test.c`: the EE RAM [arena](ARCHITECTURE.md#terms): alignment, zero fill, address mapping,
  heap statistics (`arena`).
- `diag_test.c`: the crash report (also from a worker thread under a batch's pipeline note), assert, watchdog, progress (with the heartbeat's start-up set count), start-up activity file (the batch notes; a start ended inside a 2D batch: the next start is lazy for that start only, nothing written, and logs the line and the last finished batch with its memory; that lazy start ended inside a pipeline: the start after writes `[dev] pipeline_set = "lazy"` through a stubbed config write, for any version (another version's line then leaves the policy to the count), with a failed write (this start only, its lazy run saying so, and the count kept so the next end writes it); a count file that cannot be written, logged; the "for this start only" run's clean exit and its end between two creations, each with its own line: the next start tries the set again and the row starts over; not after a restart or with the setting already `lazy`; an end in the background, by the lifecycle's event or `onTrimMemory(20)` (the mark set and logged once when both come), or with the app in the background before the recording began, is not counted, its mark written, logged and deleted, and back in the foreground the mark is gone and the end counts; a lazy run ended in the background between two creations, its mark logged; on a device the table makes lazy the first end promises nothing and its lazy run is not "for this start only", the second writes nothing, both clearing the row; the first end of a full set: the next start is careful, once, and the file reads `set: careful` after the note; a careful start that finishes clears the row and the start after it is full again; a careful start's own end makes the next start lazy; `pipeline_set = "careful"` and a careful end; the careful set's budget stop (`creating pipeline fog test` and `set: careful` in the file; its clean exit is "for that start only", then `pipeline_set = "careful"` is kept); the signals that stop the set (`actlowstop`: not `onTrimMemory` 5, not 10 before the set's first batch, 10 past it, 15 over 10) and the memory text with the device's; the set stopped at the system's low-memory signal (`set: stopped at the system's low-memory signal (running critical)` after `set: careful` in the file through the lazy run's clean exit, the batches' and the system's lines kept, the row cleared), the next start of the version lazy from it (pipeline set 7, nothing written, its own file keeping the line), that start's end inside a first-use pipeline stepping to lazy for one start (pipeline set 4), the stopped run's end between two creations (lazy from the file) and inside one (lazy for one start), another version trying the set again; a stop at the free-memory floor (its figures in the line) read as a floor stop, and one whose line ends with ` after a retry` read as a retry that stopped again (`ico_diag_startup_prev_floor_retried`: no second retry in a row); a note of the retired extended variants counted as the pipelines' kind; a VU vertex pre-pass batch's end (a `vu_main_vs` batch among them): no pre-pass for that start only, the next such end writes `[dev] vu_prepass = false`, a run that built its pipelines and finished clears the row, an end inside the skin check's start-up probe (`actskinprobe`) counted as neither kind with the set as usual, the low-memory signal's line at once and as the file's `system: ` line, in the set and in a lazy run, the level-80 one marking the background) cases, Android's start after a device loss (`actlossnext`: robust access for that start, the second loss in a row writing `[dev] gpu_robust = true`, a loss in the robust start itself counting as the second, a start without a loss that did not run robust breaking the row, a failed write keeping the count, the player's `gpu_robust` ruling, a relaunch counting nothing), device-loss restart and loss bisect (the loss noted in the activity file, the bisect stubbed: once, before the game, never again after a start ended inside it; the restart's steps robust 7, blend 8, backend 9: `lossrobust`, `lossstep`, `lossbackend`; a loss on the renderer Options > Graphics > Apply switched to and not kept yet, `losstrial`: its line, restart 0 and no relaunch; the same kept, `losskept`: `blend:opengl`) cases, the loss bisect's second level (its state file, the step stubbed: the draw found in 4 starts of at most 3 lost steps each, draw 1 with its classes in 5 starts, killed at step 3 and continued at 4, a renderer that did not start, the step and start bounds, the time budget, files that do not parse) cases, the watchdog's mark (`wdmark`: a run the watchdog ended with the activity file's path known leaves `-watchdog.txt`; `actwd`: the next start reads it, logs `the previous run (version test 1) was ended by the watchdog: ...` and deletes it, the start after reads none), the watchdog and a wait the system holds (the event pump held past the limit without an end, a stall in the game's code after it ended, the bound for one wait, that bound with no limit in force), a GPU hang with the backend's progress faked (`hang`: the WATCHDOG block's driver and wait lines, `the last start-up step: window: start: up in 1.0 s; the game runs`, the heartbeat's wait, the dump before the report, report.txt's end, the device loss's box on Linux, the hang's own loss line in the activity file, which the next start counts, with Android's robust step, and does not bisect; `hangnodump`: the hang's line without a dump, still counted; `hangnot`: no wait in progress, a freeze with the GPU's line and no loss; `hanglimit`: the GPU hang limit, 2 s, ends the run before the later limit of 60 s, its WATCHDOG line naming the submission and the limit, the rest the hang's), the program a restart starts (`relaunchexe`: the AppImage file when `APPIMAGE` is trusted, the program itself when it is not or is unset), each in a child process
  whose log is read back; the symbolizer (`symmap.h`) on a small GNU ld map of an ELF program (a wrapped function section, a static function in an object, an anchor not at the module's start), a Windows map excerpt, an nm table and its gzip form past inflate's 32 KB window, a cut gzip member and a map without the anchor; the crash's and the watchdog's symbol lines from `diag_test.map`, linked beside the test as `ico_pc.map` is beside the program; their report zips (`report.h`) read back with miniz (report.txt's end, summary and report block with its symbol lines, the log; the crash's report.txt kept as `diag_test_report.txt`, an example); the prune keeping the newest five; `ico_log_limited` (250 calls: the first 8, the 100th and 200th, the end summary's count; `ico_log_limited_n` with SDL's error bounds, the first 64) (`diag`). `diag_test --switch` (`diag_switch`): the renderer switch's steps and
  watch: a child with stderr fully buffered into its log as on Windows writes a line, a step
  (`ico_diag_step`) and a second line, then hangs and is killed with SIGKILL: the log holds the
  first line before the step's and not the second; a 2 s switch watch (`ico_diag_watchdog_switch`)
  over a step that never returns ends the run with exit 4, the WATCHDOG line naming the switch and
  the step, the main thread's place, `the last step:` and the heartbeat's switch part, not the
  game's 1 s later limit; 4 s of steady progress under the same watch and limit end in exit 0
  with the watch's `over after` line, and the progress phases' lines in order at their lines'
  starts: the renderer's start step, the start-up shader set's first (`window: shaders: the
  start-up set begins, 1 of 16`), its quarters (4, 8, 12 of 16, once each; none for the shaders
  between them nor for a pipeline's call with the same count), its end (`the start-up set is
  over, 16 made in 4.0 s`, once), the pipeline set's step (`ico_diag_shader_set`).
- `share_report_test.c`: an rc build's Share a bug report's files over a fake logs and dumps
  folder: the log, the run before's when there, the newest dump zip by modification time (a loss
  zip included, an `.rddump` left out), the newest `-present.png`, the newest `report-*.zip` (not
  another `report-` file), the newest skin check capture `vu-skin-fail-*.bin` and the newest
  slow-scene zip `slow-*.zip` last (seven files at most), a tie in time
  to the greater name, missing folders skipped, the names line and a short buffer (`share_report`).
- `backup_test.c`: Options > Extras' backup and restore (`backup.h`) on a user folder of its own
  under the build folder: the names a backup may hold (no `..`, no absolute or drive path, no `\`,
  no empty part, the places only); a backup holding the card folders, `states/`, `config.toml` and
  `achievements.toml` and none of `ico-pc.ini`, `logs/`, `dumps/`, the textures, a slot's
  developer dump or a `.tmp` file, save states and pictures stored and text deflated, the manifest
  last with the version, platform, discs, time and count; inspect reading it back and saying when
  another disc made it; the restore unpacking into `backup-restore/` and changing nothing else, the
  start's step putting it in place byte for byte, files added since gone, nothing outside its
  places touched, no old copy or staging left, a second call finding nothing; the refusals (not a
  zip, a zip without the manifest, another program's manifest, a newer format, more entries than
  the manifest says, `states/../../x`, a file outside the places, an entry past a lowered size
  bound, a busy card or save state write with nothing unpacked, nothing to back up, past a lowered
  count bound); each of the first eight moves failing in turn (`ico_backup_test_fail_move`) leaving
  every file as it was, no old copy, the staging deleted; a failing move and the move back of that
  thing failing too (`ico_backup_test_fail_also`): the thing named with its old copy, which holds
  the player's data, the rest as it was, the next start naming the copy, and an old copy of a place
  beside the data named at every start while another name is not; an unfinished unpacking deleted
  at the start; `ico_remove_tree` taking hidden files; the suggested file name (`backup`).
- `dump_zip_test.c`: F12's zip (with the tick before as its second `.rddump`, and the blended present's frame moved in third by `ico_zip_move_file`, the loose file gone), the aspect sweep's zip, the device-loss zip (the lost frame first, the second `.rddump` the frame before, its entries stored with the stored flag and deflated without it, F12's deflated, the pruning to the newest three) and the slow-scene zip (its name, the frame then the log, the pruning to the newest three with the loss zips left alone), written and read back with miniz
  (`dump_zip`).
- `game_data_range_test.c`: the game data ranges ([PLATFORM.md](PLATFORM.md#game-data-ranges))
  on this toolchain's compiler and linker, over a stand-in game library (`game_data_fixture.c`)
  given `ico_game`'s section treatment and the game's options: the ranges exist; every writable
  object of the stand-in (initialised, zero-filled, a table of addresses, file and function
  statics) lies inside, the zero-filled ones in the bss range (all in the data range in clang's
  Windows build), and the program's own globals and the stand-in's constants outside; initial
  values, zero fill and addresses survive the rename; a copy of the ranges written back restores
  the stand-in's globals and leaves the program's alone (not under AddressSanitizer, whose red
  zones the ranges hold); `ico_game_data_ranges_check`'s failure texts and edges on made-up
  ranges (`game_data_range`).
- `state_fiber_test.c`: the fiber pool and a fiber's save state image ([PLATFORM.md](PLATFORM.md#save-state-images)):
  the relocation word functions (a range's end included, one past it, a negative delta, an empty
  range, a zero delta rewrites nothing, the typed form's bad index); the pool (the slot size a
  multiple of 64 KB, the pinned flag agreeing with the base, 256 slots at base + i * slot size,
  every refusal of a used, full or out-of-range slot, the stack's bottom page read back as zero after
  a destroy); a save refused for a running, finished or free fiber; three fibers saved at three
  depths with frames pointing at their callers and at a global, destroyed, put into a second pool at
  another address, and resumed with every frame found through the moved pointers (on Windows
  the restored fiber's stack bounds in the thread block are checked too); the wrong delta and the
  wrong slot refused (`state_fiber`). `state_fiber_guard` saves a fiber, restores it into a second pool,
  resumes it in a forked child and overflows its stack: the restored slot's guard page must stop it
  (not on Windows).
- `state_guard_test.c`: the saving process's thread pointer and stack guard
  ([PLATFORM.md](PLATFORM.md#relocation)), with both processes' values made up so it runs on
  x86-64: an exact match is rewritten and counted per value, near misses (one off, one bit off,
  the guard slot's address), this process's words and range words are not, a value is matched
  before a range that did not move; a value is off for an old or new word 0 and for equal words;
  a scan over a synthetic region rewrites every old word with exact counts and leaves the rest
  alone; a real fiber holding both values in live locals and in its frame is saved, destroyed and
  restored with the values on and resumes with the new ones (its other words as they were), and
  with old equal to new or no record nothing changes; `ico_state_proc_check` per platform tag
  (Android refuses a record without either value, Linux arm64 without the thread pointer, macOS
  and iOS without the guard, the rest need none); `ico_state_proc_read` on the host
  (`state_guard`).
- `state_port_test.c`: save states across versions ([PLATFORM.md](PLATFORM.md#across-versions)),
  with two synthetic builds of one program (each an image in memory with its own code, texts, data,
  game data ranges and blocks at other places, and its own nm table); nothing of the game starts.
  The x86-64 length decoder's fields on hand-checked encodings (a call, RIP-relative loads, absolute
  immediates and displacements, VEX, EVEX, the 0F 38/3A maps, nops; an unknown opcode and a cut
  instruction not decoded); the masked code hash on x86-64 and arm64 (one function at two addresses
  with other call, RIP, ADRP and low-12 offsets alike; another frame size, stack slot, internal
  branch, register or constant not; padding left out); the symbol table from a GNU ld map (a
  static function's piece, sections bounding the pieces, `*fill*` and the discarded list skipped),
  lld's map and nm (names, ordinals, uniqueness, lookups), the layout hash equal for one layout at
  two addresses and other when a game global moves, another build's file and a missing anchor
  refused; build A's record of a state's words (a return address, a function pointer, the game's
  data and its bss's end, a block, a variable, a text, a start-up value, a dead frame's word into a
  function B lacks) checked by build B and every word translated (scanned and typed), the dead
  frame's word left and counted, the blocks mapped by name in B's order; the refusals, each naming
  its blocker (a changed function a frame sits in; a changed function only pointed at loads; the
  data layout; a block missing, new or resized; a missing text; a changed start-up value; no
  symbol table; the bounds' overflow; an image below 4 GB; a damaged record); a word nothing names
  (into a repeated name, into the code outside every unit) counted and, under a high saving base,
  refused as pinned; at Windows' fixed base, a state holding floats next to the integer 1 (2.0,
  8.0 and 300.0, all inside the image's range) loading with them unchanged while a pointer into
  the game's data beside them moves, and the same three words in an image at an upper half of
  0x74 (Android's) refusing the state with the three counted, by the saving base and by the first
  word's place when no base is given (`test_windows_numbers`); a GNU map's global
  without a size: a word past it recorded with its object's input section, translated by a build
  with the section laid out alike and refused by one where the section grew
  (`test_unsized_data`); a block B adds that A's state lacks refused, loaded and
  left as it is (`ICO_XB_BLOCK_ABSENT`) when registered FRESH, still refused when FRESH with SCAN,
  FRESH on a block the state has loaded from it; a format 1 header read and refused as another
  build, a format 2 header of another build passing the other-build check, another disc still
  refused; a simulation key this build lists that the state lacks: refused without the key-default
  function, equal at its default or unset, refused when changed or when the saved text is not what
  its hash covered, a differing key named first, a key only the state lists refused, the absent
  keys named for the log (`state_port`).
- `state_reloc_test.c`: the scan and the self-checks over the game's real allocator
  (`ico2/fumi/ios/memory.c` built with the game's flags): words at each range's start, middle and end
  move by their own delta and nothing else does (near misses, 0, all ones, floats, doubles, EE word
  pairs, the one ambiguous pair moved only when the arena moved); a second scan moves nothing; the
  program's image range (this code inside the text, static data and strings inside the image, a
  heap block and the stack outside); a heap built by the allocator walks, a copy at another address
  walks only after the scan, and three corruptions are each refused; a thread entry or handler in data
  is refused (`state_reloc`).
- `state_format_test.c`: the save state file ([PLATFORM.md](PLATFORM.md#the-file)): a write read
  back field by field and byte by byte (an empty section, a 15-character name, a zero-heavy section
  packing to under an eighth), a skipped section left alone, a second write replacing the first, BUSY
  while one is in flight; each validity field changed alone gives its result and detail; every
  damaged file (magic, version, a header byte, truncation, an appended byte, a flipped packed byte) is
  `ICO_STATE_BAD_FILE`; a write into a missing folder fails on its thread and leaves no `.tmp`; the
  build id is stable and changes with the commit and with one more block (`state_format`).
- `state_registry_test.c`: registration order, the stored fields, every refusal with the table
  unchanged, a full table, reset (`state_registry`).
- `sim_keys_test.c`: the simulation keys' list (no repeats, the expected keys), the text, the hash
  changing with a value and returning when it is cleared, and `ico_sim_keys_first_diff`'s
  `key=saved (now cur)` text on a short buffer too; a key the saved text lacks read as its default
  (equal unset or at the default, `key=(absent) (now cur)` otherwise; only unset for a key without
  one fixed default); every key's default (`ico_sim_key_default`, `ico_sim_key_is_default`), and
  the first-run `config.toml` writing every listed key it holds at that default (`sim_keys`); a
  state saved before `gameplay.shadows_together` existed equal with the option off and differing
  (`gameplay.shadows_together=(absent) (now true)`) with it on.
  The same for `developer.free_movement` (`developer.free_movement=(absent) (now true)`); `config_test.c` checks the first-run file's `[developer]` block.
- `settings_states_test.c`: the Save states page, the pause menu's rows at pitch 16 and the
  Continue screen's Square prompt ([UI.md](UI.md#save-states)), the pictures' box at their own
  shape, and F6 (the page from play and from the pause menu, back to the game, a late press, each
  ignore), on each disc's layout (`settings_states`, `_jpn`, `_kor`, `_cn`, `_ntsc`, `_aug6`,
  `_jun26`).
- `fiber_test.c`: [fibers](ARCHITECTURE.md#terms) switched round-robin with their stacks checked,
  deep stacks, the FP state inside a fiber (`fiber`, `fiber_guard`).
- `ico_thread_test.c`: a thread runs and joins, a mutex and condition hand a value across, the clock does not run backwards (`ico_thread`).
- `gpu_hang_test.c`: the GPU hang limit (`gpu_hang.h`, issue 123): half the watchdog (30 s gives
  15 s, 0 off, at least 2 s), the watch over four polls a second (issue 123's wait fires once at
  15 s, not before; a finished wait or none clears it; another submission's wait starts again
  from 0; time not counted, paused or asleep, does not add; a limit of 0 never fires), and the
  render thread's watch (`ico_render_hang_step`: a picture that makes 60 pipelines of 250 ms each
  is no hang, nor one inside a single creation for 20 s; with no creation a picture fires once at
  15 s, counted from the last creation; between pictures nothing counts), and the pipeline
  limit's issue 188 rules (`ico_pipe_stall_limit_s`: 10 s before the game starts, 60 s on a driver
  whose compiles run one at a time, 30 s in play; `ico_pipe_mem_moving` over the issue 188 log's
  heartbeats: a creation from 4 s fires at 21.75 s under 10 s and 71.75 s under 60 s, counted
  from the heartbeat after the last growth; `ico_pipe_stall_next_differs`: the self-tests and a
  lazy run the same way unless a skippable family or a VU shader before the plain ones), and the
  memory rule's cap (`ico_pipe_mem_hold`: a creation growing 4 MB at every heartbeat is held for
  three times the limit, then fires at 41.75, 241.75 and 121.75 s under 10, 60 and 30 s; with
  the free memory under the floor or a low-memory level from 20 s the limit fires at 20 s plus
  the limit; a new creation starts its own hold; the limit off never fires);
  pure (`gpu_hang`).
- `gpu_driver_test.c`: the Android driver packages: `meta.json`, refusals, installs from zips made
  here, the trial marker, the ELF page-size check (`test_pages`: two synthetic ELF64 headers, 4 KB and 16 KB
  aligned, and one with a single 4 KB segment, on a 4 KB and a 16 KB phone; what cannot be read is not
  refused: not ELF, big-endian, a table past the bytes, too short, NULL; at install, a 4 KB library or a 4 KB
  helper library refused with `ICO_GPU_DRIVER_PAGES_MESSAGE` and nothing left under the root, the same zip
  installing at 4096), the last-seen driver strings (`test_seen`: written and read back for the phone's own
  driver and a package's folder, the page's words "Qualcomm's driver 512.676.1, Vulkan 1.3" and "Turnip, Mesa
  25.3.0-devel, Vulkan 1.4", a Turnip without a Mesa version, any other driver with its id, quotes in a name, a
  bad folder, a damaged file, neither file listing as a driver) and the loss window (`test_loss`: a loss under a
  package at 0, 12 s and 29.999 s counts, at 30 s and later or on the phone's own driver does not; the marker
  written again makes the next start see a crash), and `test_list_zips` (`ico_gpu_driver_list_zips`: `a.zip`, `B.ZIP`, `incoming.*`, a text file, a hidden zip and a folder named `.zip` give two names in byte order, the cap and a missing folder) (`gpu_driver`).
  The Android file picker ([ANDROID_DRIVERS.md](ANDROID_DRIVERS.md#the-file-picker): `IcoActivity.pickFile`,
  `ico_android_pick_file`, `nativePickDone`) has no case: it is Java and JNI, and nothing here runs
  either. It is checked on a phone: `adb logcat -s ico-pc SDL` while pressing Add a driver shows
  the `ico_pc: file picker: pick N:` lines and the picker; `adb shell pm disable-user --user 0
  com.android.documentsui` (undone with `pm enable`) gives the `GET_CONTENT` fallback or, with no
  other picker app, the no picker app popup; a zip pushed to
  `/sdcard/Android/data/com.defnf.icopc/files/drivers/` shows Add from the drivers folder.
- `host_config_test.c`: `ico-pc.ini` parsing and rewriting, SHA-1 vectors, path joining, the
  card folders read from the ini the game reads (`ico_host_ini_path`), the second card's folder
  (`test_saves2_second_card`: `memcard2` in the user folder while `[gameplay] second_memory_card`
  is on, from `config.toml` and then from `ico_host_set_second_card`, an explicit `saves2`
  winning, no folder made), portable mode, the `[dev]`
  keys exported as environment switches (`ico_dev_env_value`, `test_dev_env`; `present_fallback`
  auto, off or fifo; `glow_fold` as `ICO_RD_GLOW_FOLD`; `submit_thread` as `ICO_RD_SUBMIT_THREAD`,
  off and false as 0, true as 1, its ini name; `vu_bones` ring or cbuffer and `vu_bones_by` as
  it is, exported as `ICO_RD_VU_BONES` and `ICO_RD_VU_BONES_BY`, `turnip_debug` with no variable),
  a restart's
  `ICO_RD_BLEND` and `ICO_RD_VU_PREPASS` over the ini's (`test_retry_blend`,
  `test_retry_prepass`), the disc choice's rules in order (`ico_host_choose_disc`,
  `test_choose_disc`), the logs folder set by `ico_host_set_logs_dir` (`test_logs_override`), and
  on Linux the AppImage's program folder: with `APPIMAGE` and `APPDIR` set in the process the
  folder, `ico-pc.ini`, `logs/` and a `userdata/` portable folder are the AppImage file's, and
  `APPDIR` not holding the program, missing, the root folder or a mere name prefix, or `APPIMAGE`
  relative, a folder or empty, change nothing (`test_appimage`); built again
  as `host_config_portable_test` (`host_config`, `host_config_portable`).
- `host_fs_test.c`: the file helpers on a UTF-8 path outside ASCII, the drive's kind and its file
  system's name (`ico_host_drive_fs_name`: tmpfs for `/dev/shm` on Linux) (`host_fs`).
- `hotkeys_test.c`: F12, Shift+F12, F11, F5, F9 and F6 (SDL3's codes), repeats ignored, no other
  key taken (`hotkeys`).
- `ios_chain_test.c`: the game's thread, message and memory layer on the fiber scheduler under
  simulated vsyncs (`ios_chain`).
- `inflate_test.c`: the game's inflate (`fumi/ios/inflate.c`) over its allocator and code-table
  blocks (`memory.c`, `mblock.c`) on the arena (issue 151): a stream of one fixed-code deflate
  block, and one of a dynamic, an empty stored and a fixed-code block, inflate byte for byte and
  close without an `IOSFREE()` assert, the fixed-only one leaving 0 bytes in use; a pointer 8 bytes
  into a block asserts with the line naming the block's allocation (`inflate`). On the discs, the
  packs that hold a fixed-code block inflate to their end with their member count and close
  cleanly: the Asian disc's `STGST13C.DF` (stage 4) and `STGST02A.DF`, the Korean disc's
  `STGSTAFF1.DF`, the August 2001 preview's `STGST13C.DF` (`inflate_disc_cn`,
  `inflate_disc_cn_st02a`, `inflate_disc_kor`, `inflate_disc_aug6`; skipped without the image).
- `memory_floor_test.c`: the start-up set's free-memory floor (`memory_floor.h`) by driver class: on Qualcomm's own driver and a driver not known 17 % of the device's memory, at most 2048 MB on a device of more than 8 GB (696 MB on a 4 GB phone, 522 MB on a 3 GB one, 1392 MB on 8 GB, 1445 MB on 8500 MB, 1915 MB on the Retroid Pocket 6's 11265 MB, 2048 MB on 12288, 15552 and 16384 MB); on any other driver the same up to 8 GB and at most 512 MB above (8500 to 16384 MB); a setting gives the floor for every class, `0` none; the class from the vendor id and the driver text (0x5143 with Qualcomm's own text, with "turnip Mesa driver", "Turnip Mesa 26.3.0", "Mesa 25.1.0"; PowerVR, Mali, vendor 0) and its words; the PowerVR DXT-48 phone's figures (15552 MB, another driver: 1823 MB, where the 2048 MB cap stopped its set, and 2554 MB continue, 512 MB continues, 511 MB stops, a setting of 2048 stops at 1823); the issue 161 start on the Retroid Pocket 6 on Qualcomm's own driver (11265 MB: 3340 MB continues and 1693 MB, where it stalled, stops under 1915 MB; 1915 MB continues, 1914 MB stops; a driver not known stops at 1693 too; a setting of 1900 stops at 1693, 1600 does not; on Turnip 1693 continues); a 4 GB phone at 1200 MB available continues, at 600 MB stops, on every class; an unknown figure never stops; the log lines' words for each class, a setting and off (`ico_memory_floor_rule`); the `[dev] pipeline_memory_floor_mb` text ("" and "auto" unset, "0" off, "2048" a floor, "-1" and "x" not read); issue 123's retry (`ico_memory_floor_retry`): the next start's set again only after a stop at the floor (the activity line's figures tell it from the system's signal), on Qualcomm's own driver, with a cache file that is not empty and that the renderer loaded (a missing or empty one, one the renderer did not load, the system's signal, another driver and a driver not known make no set), not after a start that was itself a retry (the stop line's ` after a retry` mark, `ico_memory_floor_line_retried`), the reporter's 1940 MB of 11453 MB and 1.4 MB file (`memory_floor`).
- `lifecycle_test.c`: the mobile lifecycle's step tables on fake operations: the render thread
  parked before the GPU idles and the surface goes, going on only after the surface is made again,
  and the tables as before without that operation (`lifecycle`).
- `frame_mailbox_test.c`: the game thread's and the render thread's hand-over
  (`port/platform/frame_mailbox.h`) driven from one thread under a fake clock: the latest frame
  wins (two replaced, counted), slot pins, the lock grid, the job queue in order and full, the
  submit's handshake (the next replay waits until the job finished, not only started), park (the
  queued job first, posts refused, then parked; unpark), quit with a job queued (it runs, then the
  end; quit while parked), the heartbeat, the last present of picture jobs only, the 10 s block
  (idle share, the longest wait and job over 600 pictures; a block taken inside a job), the
  `[dev] render_thread` mode (`frame_mailbox`).
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
  present allowance with the fence (`test120Budget`: at 120 Hz and 12 ms of GPU a picture about
  one present a step after the first second, the lock's probes failing back to one with no step a
  period late, no climb refused, the alphas on the grid; a GPU-bound phone at 60 Hz presents its
  new frames and under 33 a second with the probes and keeps real time; both print the rule
  before's numbers beside), the disc's excuse (`testDiscWait126`: every 150th step 100 ms
  long waiting for the disc drops nothing, each counted in `discExcused`), and the render thread's
  asynchronous present (`testAsyncLock`: under the lock at 60 Hz a 5 ms step with a 12.5 ms
  present on the game thread falls behind, the same present split 9 ms on the game thread and
  3.5 ms on the render thread keeps every step on time, one present a step; `testRoomOffGame`:
  the lock at 60 Hz with Direct3D 11's wait for room in the present queue on the render thread (the
  render thread's whole job modelled: 1.5 ms of interpolation, 0.5 ms more of reads, 0.3 ms of
  submit and present, the wait ending at the next vblank, a full FIFO queue; a 3 ms step whose
  first renderer call waits for the picture's reads): made after the present, at the end of the
  picture, it is 13.5-15 ms a picture with no read wait, the next present waiting 0.3-1.2 ms for
  it as the display's wait, one present a step, no step past the next due time, no drop, skip or
  resync, the alphas exact; made in the acquire, inside the reads, every step carries 3.5-5 ms of
  read waits (issue 126, PACEEVAL's finding 3); `testAsyncRates`: the
  timer path paced by the GPU's time with the presents handed over, 9, 12, 17, 20 and 25 ms
  pictures give about a period over the picture time presents a step and 30 ms every new frame, a
  9 ms present with a 3 ms step two presents a step against one; `testRenderThreadPlan`: the
  whole picture on the render thread, a step waiting for a picture's reads and a present for the
  picture before, further presents planned by `pace_rt_further_late` with the render thread's
  estimates (`pace_rt_estimate`, `pace_rt_busy_left`, their own cases first): at 3, 9 and 22 ms
  pictures and a 2 s burst of 30 ms ones every frame is shown, the presents a second are what the
  picture time allows (two a step and more, one a step, the new frames), no further present makes
  a step wait, and without the plan the 9 and 22 ms pictures and the burst do; the burst's waits
  with the plan, from the new frames' own pictures, under a quarter of those without;
  `testRenderThreadBehind`: `pace_rt_further_hold`'s own cases, then the model with the driver's
  back-pressure (a present returning once the GPU finished the picture before it, the phone's
  PowerVR): the holds change nothing on the loads above or on a light stage, and in a 4 s stage
  of 26 or 20 ms GPU pictures they leave no slot or read wait where the plan alone leaves 40 and
  more slot waits, every frame shown, at least 150 in-betweens kept; the numbers from a Python
  port of the model, `build-host/tmp/v098/evidence/rthread-c/rtc_behind_sim.py`);
  `testSubmitThread`: the same model with the submit thread (`rtTimerLoopSplit`: the render
  thread's job ends with its reads, the submit and present after it on the submit thread, the next
  picture's first graphics call after its interpolation waiting for that thread): a 4.5 ms step,
  10 ms reads (3 of them the interpolation) and 8 ms of submit and present give one present a frame
  with them on the render thread and two with them on the submit thread, no step, read, slot or
  submit-thread wait; 4 ms of submit and present give two either way; a 26 ms GPU stage with the
  back-pressure keeps no wait and every frame with the holds (numbers from
  `build-host/tmp/v098/evidence/submitthread/st_sim.py`); `testBackoffCause`: the step rule's
  answer to a miss by its cause and size (`pace_step_by_pictures`, `PaceStepEst`): at the phone's
  10 ms reads one game step 20, 40 or 700 ms longer (a load, a compile, a capture) gives 698 /
  698 / 701 presents in 10 s (the long step left out of its kind's typical time) and no step planned at the whole period (the 20 ms one's next step
  started late by the new frame's own picture: the pictures', over the floor, the margin
  widened), where arming on every miss gave 660 / 647 / 679 and 2 / 4 / 1 back-offs; the reads'
  jump's miss is the pictures' and widens the margin; and a phone's log's numbers over 100 s (a 4.5 ms step,
  13 ms reads, 25 ms GPU pictures, a picture 26 ms slower once and 12-18 ms slower every 250
  steps): no step at the whole period (FIX-HANDOVER's back-off: 5819 of 6000) and 397-400
  presents a block (its 300), every miss one surprise's (13 / 16 over the floor in its two cells,
  each widening the margin); three misses over the floor within 300
  steps fall back once, for 120 steps (numbers from
  `build-host/tmp/v098/evidence/fixbackoff/backoff_cells.py` and its phone cells, re-run with the
  rule by the frame's shape in `build-host/tmp/v098/evidence/pace3/rebase_cells.py` and with the
  render thread's typical times in `build-host/tmp/v098/evidence/pace4/c_cells.py`);
  `testFrameShape`: the game's frame as two kinds of step (the one closing a frame 8-10 ms, the
  one between 0.5 ms with no renderer call; `pace_step_plan_ns`, the step after by its own slack,
  `nextBetween`), a phone's load (12 ms reads, a 7 ms submit, vsync off on a 120 Hz mailbox):
  451 / 433 / 399 / 385 presents at 21.7 / 23 / 25 / 26 ms GPU pictures where one estimate for
  every step gives 390 / 389 / 376 / 374, 349 with the driver's back-pressure (318 while the
  plan took the picture before's fence; `build-host/tmp/v098/evidence/pace5/c_cells.py`); with 8 ms GPU
  pictures, a closing step of 8.5 / 9.5 ms and reads 8 / 10 / 10.5 / 12 ms 602 / 600 / 600 / 500
  and 601 / 600 / 597 / 450; the step between calling the renderer 600 / 487 at 10 / 12 ms; no
  step missing its period, every frame shown, no start later than the closing step's slack
  (numbers from `build-host/tmp/v098/evidence/pace3/rc10_cells.py`, re-run in
  `build-host/tmp/v098/evidence/pace4/c_cells.py`); `testRc11`: the render thread's typical times
  (`PaceRtTyp`) against their recent longest at a phone's load (each picture's reads from a table
  of 61 with a mean of 10 ms and a deviation of 2.5 ms, its interpolation and its submit with
  them, the closing step 7.5-14.4 ms in turn, 9 on average, the step between about 1 ms), over
  100 s: 4098 / 3947 / 3814 presents at 24 / 25 / 26 ms GPU pictures where the longest gives 3482
  / 3435 / 3429; 14 / 22 / 2 steps missing their period, all the pictures', 7 / 8 / 0 over the
  floor and none later than 3.5 ms; one fall-back of 120 steps at 24 and 25 ms; the game's clock
  at most 8.3 ms behind; 343 (322) with the driver's back-pressure (320 / 317 while the plan took
  the picture before's fence) and 397 (341) with the step between calling the renderer, no miss
  (numbers from `build-host/tmp/v098/evidence/pace4/c_cells.py`, `rc11_cells.py`, the
  back-pressure cells re-run in `build-host/tmp/v098/evidence/pace5/c_cells.py`); `testRc12`: a
  phone's three stage-6 blocks (each picture's own reads, interpolation and submit from tables of
  61 scaled from `testRc11`'s, the closing step from 16 and the step between from 4, their
  typical times the clause's), with the pipe as before (the picture before's fence whenever the
  present waited half a refresh, `pace_rt_step_trace` given that) at presents of 1 / 5 ms 410 /
  302, 462 / 302 and 340 / 301 in 10 s (the phone's 44.6 near the first, 32.6 and 31.0 near the
  second; every ask behind at 5 ms, held by the next step's start), and the pipe now 410, 462 and
  340 whatever the present waited, B and C with no miss, A 2 (1 over the floor) and one fall-back
  of 120 steps either way; over 100 s 4274 / 4601 / 3392, the fewest in a block 410 / 459 / 339,
  no start later than 8.4 ms
  (`build-host/tmp/v098/evidence/pace5/rc12_cells.py`, `c_cells.py`); the PowerVR mailbox at 26 ms
  in `testStepRule` 376 where the picture before's fence gave 320; `testRc13`: the loop in the
  frame slots apart's world (`s_rtGame.slots`: the render thread waits for the submit thread only
  for the picture two before's job, the GPU starts a picture once the submit thread has run its
  submit, the pipe reads the job as 0 with `submitOn`; `gpuSeq` the GPU's time per picture;
  `gpuRecords` the pipe's GPU time the median of the last 8 records, two pictures late, as
  `perf_drain`) at a phone's blocks: the Original preset at 512x448 (14.8 ms GPU pictures, 8.7 ms
  reads, a 3.4 ms closing step) 460 presents in 10 s with the rule as that phone's build ran it
  (the job's 0 read as the thread off: the picture before's fence in 747 of 749 asks; the phone
  45.6 fps) and 671 now, 633 with the GPU's time alternating 21.0 / 8.6 ms, 6725 over 100 s with no
  miss; the Enhanced preset at 848x448 451 / 653 (the phone 45.5) and at 1696x896 302 / 545;
  `testRc12`'s native blocks 302 / 431, 302 / 458, 301 / 339 (the GPU makes 446 / 461 / 339)
  (`build-host/tmp/v098/evidence/pace6/c_cells_rc13.py`) (`pace_loop`).
- `cpu_place_test.c`: which core a thread runs on (`port/platform/cpu_place.h`): the histogram
  (by core, out of range and -1 as not known, the most-run core with the lowest on a tie, the take
  emptying it, the share on a mask), the cores' speeds from a fake sysfs under the build folder on
  Linux (an 8-core phone `0-3 1.8 GHz, 4-6 2.6 GHz, 7 3.1 GHz`, an offline core with no file, an
  unreadable one, one speed for all giving no slow or fast cores), the masks' text, the 10 s part
  word for word (`on cores G:4 (78 %) R:7 (64 %) S:5 (90 %), on the slowest cores (0-3) G 0 % R
  12 % S 10 %`, a thread with no samples, one with no core known, a short buffer), the split
  (`cpu_place_split`: `0-1 2.2 GHz, 2-6 3.1 GHz, 7 3.8 GHz` gives the render thread core 7
  alone and the submit thread 2-6, a 4+3+1 phone 7 and 4-6, a core of unknown speed in neither; no
  split with two fastest cores, with the fastest the only one above the slowest, with one speed
  or none known), the placement rule (`cpu_place_plan`: 2 faster of 8 gives the game and render
  threads both cores and the submit thread none; 3 and 4 faster keep render and submit with the
  game thread free; one faster the render thread alone; all equal or none known nothing), and the calling
  thread (its core inside the cores it may run on, the priority text's `nice`, a pin to no core
  refused, a pin to the cores already allowed taken), and a pin's result by the cores read back
  (`cpu_place_pin_result`: core 7 asked, the call's yes and 2-7 read back is other cores; the same
  mask kept; a refused call; nothing read back; on Linux the checked pin to the cores allowed
  kept, a core the system lacks refused with its errno), and a thread made after a pin (Linux,
  two or more cores allowed: the test thread kept to one core, an `ico_thread` made then has
  that one core with no mask kept and every allowed core once `cpu_place_keep_free` kept them;
  `cpu_place_thread_free` resets the kept thread itself, then finds it already there, and does
  nothing with no mask kept); issue 179: the thread's CPU time, core lists to masks, the threads
  line from a fake table and a fake `/proc/self/task` (`cpu_place`).
- `thread_stat_test.c`: `schedstat` parsed and read from a fake `/proc/self/task`, the block's
  difference, its words (`thread_stat`).
- `fps_counter_test.c`: the Frame rate counter's number (`port/platform/fps_counter.h`): 0 before
  the first second, 50 presents a second give 50 at every whole second, a second that ran 1.5 s long
  divides by its real length and rounds to the nearest (91 presents 61, 89 presents 59), replays
  counted through the renderer's count being zeroed after a tick (100 a second, none lost), a
  change of mode starting the second again, a time going backwards changing nothing (`fps_counter`).
- `pace_policy_test.c`: the slow-present decision, Resolution "auto"'s steps (and under a
  temporal upscaler from the window's scale over its ratio, `pace_auto_window_scale`), the gap between
  presents, the vsync lock's n and alphas and its period's sanity, and the vblank clock's laws on a
  synthetic display, and the late-present skip: a whole period behind skips, with or without
  sound, less does not, slow presents skip past the deadline, never two frames in a row while the
  same frame may stay skipped; the timer path's loop under a fake clock keeps a 30 frames a second
  game on 39 ms presents at real time with no resync, where a frame skipped for one step only fell
  behind, and a 60 frames a second game on 20 ms presents through the late arm alone; Frame rate
  Original's rule (`testOriginalRule`: `pace_original_present`'s late arm, `pace_original_gate`
  leaving a frame on time out while the render thread or the submit thread is busy with the
  picture before and a keep frame never; `testOriginalLoop`: the picture inside the step, 9 to 90
  ms a picture, the game at 50 steps a second; `testOriginalSplit`: the picture handed over, walks
  of 5 to 40 ms and presents of 1 to 300 ms on the submit thread, a frame every step or every
  second: the game at 50 steps a second within 2 % with no resync, the pictures between 1000 /
  (present + walk + a frame interval) and 1000 / (present + walk) a second, the audio's lead under
  its bound, where the same 320 ms picture inside the step resyncs and runs the game under its
  rate); the present
  allowance's rule (`testBudgetRule`: one late step arming only, a second within 60 steps dropping
  to one below its presents, a late step of new frames only confirming an armed drop, the arm's 60
  steps, the hold, the lock's climb a probe straight back to the ceiling that holds through its 60
  steps, the display's wait excused, `pace_display_wait` with and without OpenGL's fence
  waits, never above the ceiling), the timer path's unbound start (`testBudgetUnbound`: 15
  presents a step on time never refused, a drop held to the ceiling of 8, the climb unbound again,
  straight back to unbound at 4 ms of GPU, one at a time and then refused at 3 ms by the GPU gate,
  the lock's allowance never unbound), the reporters' sequences replayed from their logs
  (`testBudgetHitch130`: issue 130's one late step leaves an uncapped 6 presents a step unbound
  with nothing refused, two within 60 steps hold it to 1 and 120 steps later it is unbound again
  at 1.2 ms of GPU, at 1 with a refused climb every 60 steps at 9 ms; `testBudgetSustained`: the
  item-3 case held to 1, issue 139's GPU-bound lock held to 0 with two failed probes (302
  presents in 600 steps); `testBudgetGlWait126`: issue 126's OpenGL display wait excused, the
  same numbers on Vulkan dropping to 0 and back to 1 after 120 steps, the GPU-bound OpenGL blocks
  at 0; `testBudgetDiscWait126`: the disc's excuse, 100 ms late with 100 ms of disc wait drops
  nothing, with none or 20 ms it drops to 0, late by the wait and half a refresh is excused and a
  nanosecond more is not, the timer path paced by the GPU keeps its balance, the drop line's
  `disc wait`; `testBudgetLines`: the drop, climb, refused-climb and 10 s words of those sequences (the
  GPU figure named `GPU span`, `GPU busy` for a busy time) and the lock's clock part, compared
  word for word), the lock's probe as rates (`testBudgetProbe126`: issue 126's D3D11 drop with a
  15 ms span the old gate refused, back at 1 a step 120 steps later and 600 presents in the next
  600 steps, 60 a second; `testBudgetProbeWait`: failed probes doubling the wait to 240, 480,
  960 and 960 steps with the new frames only between, a probe failed at its 60th step, one that
  holds setting the wait back to 120), and the timer path with a GPU queue
  (`testBudgetTimerLoop`: nothing binds at 4 or 12 ms of GPU a picture nor on back-to-back 1 ms
  presents, the in-betweens held at 20 ms so no step ends half a period late, FIFO's return past
  the deadline excused), the FIFO fallback at 120 Hz (`testBudgetFifo120`: vsync off, a 28 ms GPU,
  every present waiting for the next vblank: 30 presents a second, nothing armed or dropped); the
  clamp of a cap above the refresh in mailbox mode (`testPresentGap`, `testCapEqualsRefresh`),
  and the step rule's answer to a miss (`testStepRule`: under the 2 ms floor nothing, over it the
  margin widened by the lateness and falling an eighth a step, the third over it within 300 steps
  the whole period for 120 steps, then 240, 480 at most, halved by 300 clean steps; a phone log's
  sequence of misses with no fall-back; a game's miss changing nothing and counted down inside a back-off
  as a step; `pace_step_start_late` past the later of the due time and the step before's end less
  the pacer's wake; `pace_step_by_pictures`' edges, a hold under the floor the game's), and the
  step's typical time by kind (`testStepTyp`: the decaying mean and deviation to the nanosecond, an
  isolated step over the period left out and two in a row counted, a loading step left out, a
  phone's frames giving 9.2 ms closing a frame and 0.5 ms between (the mean and half the
  deviation, `pace_typ_share_ns`; one deviation gave 9.6) where one kind for both gives 5.5 and
  the recent maximum 8.4; a 15 ms closing step among 9 ms ones raising the plan to 10.7 ms and
  falling back, a run of 6 and 16 ms steps held at the mean and a quarter;
  `pace_step_plan_ns`' fall-backs and widening; `pace_step_closes`;
  the step after the next planned by its own time, and none after a step between frames), and a
  render thread's typical time (`testRtTyp`, `PaceRtTyp`: the mean a sixteenth a picture and its
  variance to the nanosecond, a 100 ms spike among 10 ms pictures raising the plan to 11.9 ms
  (in full 19.5, the recent maximum 100) and 60 pictures later 10.2, two in a row over twice the
  mean counted in full, a 0 counted and the way back from it, a stall counted at 250 ms; a
  phone's reads (a mean of 10 ms, a deviation of 2.5) planned at 10.5-12.0 ms where the recent
  maximum read 9.7-15.9), and the rule's decision trace (`testStepTrace`, `PaceRtTrace`: the
  pipe's fence the picture before's only with the submit thread off and the presents behind
  (`pace_rt_pipe_behind`); what set a reads' end, the measured reads, the interpolation, the
  submit thread or the fence (`pace_rt_pipe_reads_end_parts`); a phone's further present 7 ms
  before the closing step held by the next step's start 11.9 ms past its slack with the picture
  before's fence and handed over with the submit thread's, every field to the nanosecond; the
  step after holding it 1.3 ms past its slack; the trace line and the block's sum line word for
  word, a block with no ask, a hold before the rule with no trace, a short buffer cut; the frame
  slots apart, the submit thread's job 0 with `submitOn`, not behind), and the GPU's time between
  timed replays (`testGpuGap`, `PaceGpuGap`: the idle floor of 0.05 ms, an overlap capped at the
  span, the `window: GPU between replays` part word for word, a short buffer; issue 123: gaps
  timed across untimed replays counted, the gaps after the presents that blocked listed, one
  stall, the first 8 of 11 kept; the longest text fits the window's `PACE_GPU_GAP_TEXT_MAX` buffer
  whole) (`pace_policy`).
- `perf_block_test.c`: a block of replays by phase: sums, maxes, the replays over a threshold, the
  longest phase (`perf_block`).
- `present_fallback_test.c`: the FIFO fallback's decision (`present_fallback.h`): the stock
  Adreno driver's blocks of 376-382 ms at 0, 9 and 14 s switch at the third and never again, the
  switch line word for word, the 10 s line's `toward FIFO` and `since presents went FIFO` words;
  two blocks 40 s apart, blocks 31 s apart and blocks under 200 ms (52 ms, 21 of 143 ms) switch
  nothing; Turnip's driver text is not eligible while the count still rises; a Mali-G52's blocks (204, 231,
  240 ms at 408, 412, 416 s) under the other-driver gate answer once, never switch, with the line
  word for word and no suffix (`testOther`; `ico_present_fallback_qualcomm_own` for Mali, AMD and
  NVIDIA no); the window's gate (`testGate`, `ico_present_fallback_gate`, which
  `window_present_fallback.c`'s `gate()` calls): Qualcomm's own driver `GATE_YES`; Mali, PowerVR,
  NVIDIA and AMD `GATE_OTHER` (a revert of the vendor check to `GATE_YES` fails here); Turnip and
  RADV texts, vsync on, a swapchain not in mailbox mode, a backend other than Vulkan, no device and
  `[dev] present_fallback=fifo` `GATE_NO`; `[dev] present_fallback=off` `GATE_KEPT`; `[dev]
  present_fallback=off` keeps mailbox with its line once; a count reached with vsync on switches at
  the next block once eligible; the driver texts and the `[dev]` values (`present_fallback`); FIFO from the first swapchain
  (`testStart`): Qualcomm's own driver (0x5143, text neither Mesa nor turnip) with auto and vsync
  off on Android yes; off, fifo, vsync on, Mesa and turnip texts, other vendors and any platform
  but Android (Windows on Snapdragon: `ico_present_fallback_start_asked`) no; the start line word
  for word. `pace_policy_test`'s `testFifoFromStart`: vsync off with a FIFO swapchain keeps the
  as-made rule, the paced allowance and the back-to-back gap.
- `display_rate_test.c`: the refresh rate's log lines and the Android frame rate pin's decisions
  (`display_rate.h`, issue 123): the watch (first rate noted, 59.94 against 59.95 not a change, an
  unknown rate not one, 144 -> 60 -> 120 two changes), the block's line and the change's line word
  for word, the 10 s poll (`display_rate_poll`: a change SDL announced written at the event's run
  time, one it did not with its own line, no change and an unknown rate nothing), `[dev] frame_rate_pin` (auto, off, false, 0, no, any case), the rate asked for each
  Frame rate row (60, 120, 144; Original, Uncapped, off and above 1000 none), the sequence set,
  nothing, set, release, nothing, a device without the call never asked, a refused ask tried
  again, a new native window or a resume forgetting the ask and the next set made again
  (`DISPLAY_PIN_SET_AGAIN`; none wanted then: nothing asked or said), the pin's lines
  (`display_rate`).
- `gpu_sysfs_test.c`: the kernel GPU driver's nodes, the pure part (`gpu_sysfs.h`, issue 123): the nodes' texts parsed (`905000000`, `42 %`, leading blanks, a sign, empty, no digit first, a 4 KB text, a buffer read to its length only, digits past a long long's room), Hz to MHz rounded; the ring of 64 (70 samples, the oldest dropped); the 10 s block (the clock's range and mean, busy's mean, the resets in the block against the last before it and from the ring's start, the thermal level's highest, a temperature, only busy read, a steady clock, an empty block); around a present (the sample before, the ones during, the one after, samples exactly at its start and end, none before, none after, nothing read); the open line (the usual readable set, every node denied, the directory missing, nothing present, only the once nodes with errnos named), the block and around lines word for word, a short buffer cut; issue 179: the devfreq name rule and lines, the CPU clocks by class and around a present (`gpu_sysfs`).
- `thermal_text_test.c`: the thermal and screen recording words (`thermal_text.h`, issue 123): the
  status name table (NONE 0 to SHUTDOWN 6, -1 and 7 UNKNOWN), the change line (`MODERATE (2) ->
  SEVERE (3) (run time 212.4 s)`) and the recording line (visible, not visible), the 10 s block's line
  with every part, without a headroom (NaN), headroom alone, status alone, recording alone and
  nothing reported (`not available (why)`), the around line's ending in the same cases (empty when
  nothing is reported), a short buffer cut and terminated, all word for word (`thermal_text`).
- `present_hist_test.c`: the 10 s histogram of the driver's present call (`present_hist.h`): a
  limit is not over itself, a call is counted in every row it is over, issue 123's block
  (750 presents of 9 to 15 ms and five of 344 to 365 ms: 5 in every row), the line word for
  word, an empty block's empty text, a small buffer cut, negative and NaN times not counted;
  the lines around a present that blocked (issue 123), word for word: the blocked line's split
  (`present_split_text`: a reporter's stall of 359 ms with a 67.9 ms GPU span against the block's
  26.7, with its `other` time, the block's first timed replay, the lock without a present call, `other`
  never under 0, a gap under 0, the call not timed apart, no gap, no GPU span) and the after line (`present_after_text`: the replay after the stall, its gap across one
  and two untimed replays, no GPU span, a short buffer cut); the stall lines' threshold
  (`present_stall_lines`: from 250 ms on, never from 50 to 250 ms nor for NaN) (`present_hist`).
- `bones_ladder_test.c`: issue 123's ladder across starts (`bones_ladder.h`,
  [VU_SKIN_CHECK.md](VU_SKIN_CHECK.md#the-ladder)): the step for each run's evidence over sequences
  of starts as `window_bones.c` applies them (step 1 written and healed over 2,000 presents; step 1
  still differing over 8 twin events (not 1 or 7): step 2 marked trial, then `turnip_debug = off`
  and a later start's stop; step 2 healed with step 1 in force: `turnip_debug = retest`, the
  re-test clean over 2,000 presents: the key removed, step 1 alone healed after; the re-test with
  8 twin events (not 3): `nobin` marked needed, then kept healed and `off` on a differing twin; the
  re-test on a driver nobin cannot reach not judged; step 2 healed without step 1 kept; step 2 out
  of reach: the stop; step 1 skipped when the ring read beside the picture did not
  clear it twice (once waits) or the scene pass's execution differed twice with no wrong probe; step 1 untested after 8
  events; nothing without a differing twin; a port race never acted on), step 1's switch in the run
  as `window_bones.c` polls it each pump (`bones_restart_step`: asked once, at the pump after the
  write, never while a movie plays or in the run's first 30 s, never again in the run), step 1 never written where the ring builds cannot be used (the extended
set: `BONES_WAIT_PLAIN`, its line) while step 2's evidence still acts, step 2 taken for in force
only when the Turnip package loaded and the mark carries trial or needed (`bones_nobin_in_force`,
  `bones_turnip_state`: a nobin without either word, an older ladder's or a hand-written one, is a
  re-test), the mark
  written and read back with its words and a hand-written value never taken for it, the start, action, step 1
  (now, waiting, next start) and switch lines word for word (`bones_ladder`).
- `robust_persist_test.c`: the port's own `[dev] gpu_robust` turning itself off (`robust_persist.h`): the mark `gpu_robust_by` written and read back and a hand-written value never taken for it, the rule over sequences of starts (three clean starts in a row turn it off; a loss or an end while preparing resets the count; a start that never reached the game leaves it; another version turns it off at once; no mark does nothing), the mark through the config text, the removal of both `[dev]` keys leaving every other line (a `gpu_robust` in another section included), the three log lines word for word (`robust_persist`).
- `pipeline_skip_test.c`: the record of a pipeline family a device's driver stopped in (`pipeline_skip.h`): the family a start-up activity note names (one sprite planes build alone, or a lazy creation, names it; the issue 161 mixed batch, a plain build, a vertex shader batch and a cut note name none; the in-flight key inside a mixed batch names it when that key is a planes build), the value written and read back, a match on the same device and driver only (not after a driver update or on another device), the clear (`pipeline_skip`).
- `sched_test.c`: the fiber scheduler against the EE kernel's rules through the SDK calls
  (`sched`); the longest single switch (`ico_sched_switch_max`: a 30 ms thread against a 1 ms
  one names the long one, its time at least 30 ms, the reset empties it, a NULL id).
- `window_scene_test.c`: the window's scene lines (`window_scene.h`, issue 179): the layout kinds
  of the European disc's ids (a movie over any layout is a scene, the title's two, the pause menu,
  the options and button configuration, anything else `other`), the block's layout share over a
  sequence of steps (`layout: play 6.2 s, scene 3.1 s, pause 0.7 s, other 0.0 s`, the empty block,
  a kind out of range ignored, a reset, a cut buffer), the slow step's game threads part (`game
  threads 199.3 in 39 switches (longest Main 171.2 ms; on core 2)`, the core or the longest switch
  left out, no name) and the VU line (pre-pass off 9 a triangle, on the recorded vertices, the k and
  M words, no replays, a cut buffer), its skin check twins' part (`; twins 42 draws, 76k vertex
  invocations a picture`, no replays, none drawn: nothing, a cut buffer), the GPU work line (the invocations 9 a triangle off and the recorded vertices on, the work less the display wait and the present, never negative, the fit word for word), the acquire line (none asked: nothing) and the images in use, word for word, and
  the slow-scene dump's trigger over a block's figures (each condition alone: kind, window, movie,
  three a run, 30 s, 8 replays, the bound off; the bound itself is not over; the display wait taken from
  the work; no negative work; a later dump at 1.5 times the last block's work: 74.9 against 50
  not due, 75 due, the count at 3 not due, the first at any last work), the replay pick (a vertex
  work at or above the block's mean) and its log line with the vertex work and "dump 2 of 3", and
  a block's share of a counter (the difference, or the value itself after a restart from 0)
  (`window_scene`).
- `startup_retry_test.c`: the steps after a graphics driver crash while building a pipeline, the
  crash chain to the box, the variable and the relaunch environment, the `dst` field of a
  pipeline's or a start-up batch's note, its VU vertex pre-pass keys and modules, the skin check's
  start-up probe's notes (`startup_retry_note_has_skin_probe`: its compile and its draw, neither
  the pre-pass's nor a destination read's), and the chains
  with the pre-pass (a crash in one of its pipelines: cache, noprepass, then the rest without it;
  a loss in a run drawing with it: lossnoprepass, blend, backend) (`startup_retry`).
- `app_icon_image_test.c`: the icon's image maths on a synthetic soft-edged shape composed over black
  and white: the matte's alpha within 2 and colour within 2 (where alpha >= 192), the box of alpha
  >= 8, the square with its 10 % margin and its shift and shrink at the frame's edge, the 256 x 256
  crop when shrinking and growing, the 48/32/16 downscales keeping colour at clear edges, the ghost
  refusal at 3 px and acceptance at 0 and 1 px, and the single-frame luma key (`app_icon_image`).
- `ico_icon_test.c`: `ico_icon_write` read back: the header (count 4), `dwBytesInRes = 40 + w*h*4 +
  mask`, the bottom-up BGRA rows, the clear column kept clear, the zero mask, the 256 entry holding
  the PNG byte for byte, contiguous offsets ending at the file's end, no `.tmp` (`ico_icon`).
- `app_icon_files_test.c`: the icon files over a fake user folder: wanted on missing, not after a
  store, wanted on another label, not on an empty label with the files present, wanted on an empty
  label without the PNG; the paths, the copies, the stamp (`dev` for an empty label), no `.tmp`
  left, the refusals, and the capture guard `ico_app_icon_picture_plain` (CRT filter on or off at
  strength 0, each of the four tone values off 50, 0 read as 50, Upscaling on) (`app_icon_files`).
- `icon_stamp_test.c`: the Windows exe's icon resources (`icon_stamp.c`) from an `.ico` that
  `ico_icon_write` made of a 256 x 256 RGBA test image: four `RT_ICON` images with ids 1..4, each
  the `.ico` image's bytes in place (the 16/32/48 `BITMAPINFOHEADER`s with height 2*h and their
  sizes, the 256 entry the given PNG with its signature), the `RT_GROUP_ICON` directory (6 + 4 x 14
  bytes, each entry the `.ico` entry's first 12 bytes and then its id); the refusals (a cursor file,
  0 or 33 images, a cut directory, a cut image, an empty image, an offset inside the directory or
  past the end, no output); the stamp text (`<label> <disc>`, `dev` for an empty label, the refusals)
  read back from the resource's bytes with no terminator, a line end or a NUL; and the command-line
  quoting of paths with spaces, a trailing backslash, quotes, backslashes before a quote and an empty
  argument, split back by the C runtime's rules, and the too-long refusal keeping the line so far;
  the stamped copy's PE check (`ico_icon_stamp_pe_check`) on a small PE32+ laid out as the game's
  exe (`.text`, `.rsrc`, `.reloc`): passing with `.rsrc` grown and `.reloc` moved, refusing a changed
  code byte, entry point, import directory, section name or flags, no resource directory, a cut file,
  a short `SizeOfImage`, another machine and a file that is not PE (`icon_stamp`). The resource update itself (`tools/ico_icon_stamp.c`) runs only on Windows.
- `rd_png_mem_test.c` (`port/render/test`): `rd_write_png_mem` against the exact bytes the file
  writer produced before the split (a 2 x 2 image, RGBA and RGB), equal to the file writer's bytes
  for a multi-block image, a round trip through `texpack_load_png` with a padded pitch, and the
  refusals (`rd_png_mem`).
- `desktop_entry_test.c`: Extras > Add to the applications menu's pure functions (`desktop_entry.c`,
  Unix hosts): the entry's exact text, the `Exec` quoting for `/games/my dir/ico_pc` and for `$`, `"`,
  `\`, a backtick and `%`, the refusals (empty, a control character, too small a buffer), the data
  folder rule (`XDG_DATA_HOME` absolute, else `HOME/.local/share`), and an install over a fake data
  folder: the PNG copied byte for byte, both files under the app id's name, no `.tmp`, a second
  install replacing the first, nothing written for a missing PNG (`desktop_entry`).
- `steam_shortcut_test.c`: Add to Steam's CRC and id, a sample `shortcuts.vdf` appended byte-true,
  the duplicate check, `loginusers.vdf`, and the recorded program path: the AppImage file when
  `APPIMAGE` names one and the program lies under `APPDIR`, else the program itself
  (`test_self_exe`), and the icon field: `ico_steam_vdf_set_icon` on an empty icon (every other byte
  identical), the program as the icon (quoted, plain, case-less), a missing field, either field order,
  the player's own icon, no entry, a file that is not a shortcuts file, and the whole job with an
  entry added earlier, a new entry, the player's icon and no icon file (`steam_shortcut`).
- `backend_switch.cmake` (window build, the disc image; skipped without it): Options > Graphics >
  Apply through the program itself, `[dev] backend_switch_test = "opengl,vulkan"` from a portable
  folder on SDL's offscreen driver, Vulkan on lavapipe, OpenGL on llvmpipe: both switches applied
  and kept through `ico_window_switch_backend` and `ico_window_reopen`, `done, 2 switches, 0
  failed`, OpenGL's compile-thread line, the switch steps through `up in`, the switch's watchdog
  line, the run's last tick, no WATCHDOG, CRASH or FAILED (`backend_switch_window`, labels
  `platform` and `window`, run alone, 1500 s). It starts the game: the orchestrator's gate, not a
  package's.
- `window_video_test.c`: fullscreen and mode requests on SDL's offscreen driver (`window_video`),
  Fullscreen holding the desktop's mode (its size and refresh rate) or no mode where the driver
  takes none, as `ico_window_video_desktop_mode` returns. The swapchain's
  `VK_EXT_full_screen_exclusive` request (Windows) has no test: lavapipe lacks the extension, and
  whether a display goes exclusive is the driver's.
- `port/platform/fpenv_test.c`: the simulation's FP mode rounds toward zero and flushes denormals,
  the host mode is back to nearest; `trap` checks the fptrap build traps (`fpenv`, `fpenv_trap`).

## port/render/test

The renderer's test programs have a page of their own:
[TESTING_PROGRAMS_RENDER.md](TESTING_PROGRAMS_RENDER.md).

## port/rhi/test

- `rhi_test_common.c` (every backend's run, after the `rhi_create_pipelines` batch): the same eight descriptions through `rhi_create_pipelines_begin` with 2 threads at low priority and `rhi_create_pipelines_end` not waited until it is done: on Vulkan the seven made, the uncreatable one alone failed (time 0), on 1 or 2 threads, ids none of the batch's pipelines has; on the other backends NULL and an end of NULL false.
- `rhi_test_common.c` (every backend's run): the thread submit cell (`threadSubmitCell`): a
  list recorded on the test's thread and ended and submitted on another reads back byte for byte
  as the same list submitted on the test's thread, and the readback after it waited for the other
  thread through the call guard (`rhi_set_call_guard`); skipped where the backend takes every call
  on one thread (OpenGL, Direct3D 11). Its sibling for the submit thread (`threadRecordCell`):
  the list recorded on a thread A, ended and submitted on a thread B, read back on the test's
  thread equal row by row to the same list made there and to the fill rule's squares, the readback
  waiting for B through the guard. The frame slots apart (`threadSlotsCell`, after it): a list of
  one slot ended and submitted on a thread B while the test's thread recycles the other slot,
  begins its list there and records it (B ends once that list has begun), with a test slot guard
  (`rhi_set_slot_guard`) freeing the test thread's slot calls while B's job is of the other slot:
  no guard wait while recycling, recording and ending the second list (the end is a slot call),
  one at its submit, and both targets
  equal row by row to the same two lists made one after the other and to their squares (red and
  green, and a blue one); skipped where the slots are not apart (`thread slots cell skipped`:
  Direct3D 12, OpenGL, Direct3D 11, and Vulkan's fence ring: `ICO_VK_FAKE_API=1.0`, `1.0bare` and
  `1.1`, which hide the timeline semaphores). The validation layers' thread checks see the two threads' use
  of the two slots' command pools and the queue (`rhi_vk`).
- `rhi_vk_test.c`: the exact-texel RHI checks on Vulkan, a hazard cell, the render-area check (a
  pass larger than its attachments clamped with no validation error, `passAreaCell`), a pass with
  a render area inside its target (`renderAreaCell`, under every switch: on a 16x16 target cleared
  blue, a pass loaded with the area (4, 6) 5x3 draws a quad over the whole target in red after a
  scissor set to the whole target, and a third with the area (10, 0) cut to 6x2 clears to green:
  15 red and 12 green texels, every other one still blue, no validation message; the specification
  applies the load and store operations inside the render area only), the
  pipeline cache file, and the pipeline pool filled: `RHI_PIPELINE_POOL_MAX` pipelines alive at
  once, none refused, the 1025th and the last drawn with, one more refused and counted
  (`poolCell`, once, not under the fake switches) (`rhi_vk`, `rhi_vk_nodual`, `rhi_vk_d24s8`,
  `rhi_vk_mali`); the occlusion queries (`occlusionCell`, under every switch: on an 8x8 target with
  the depth cleared to 0 and GEQUAL with writes, a quad at 0.5, the same quad behind it at 0.25 and
  in front at 0.75 count 64, 0 and 64 samples, read once the slot is recycled; a begin outside a
  pass dropped, the read saying 3 indices; two frames, the first making and resetting the pool at
  its first begin, the second resetting it in the slot's first list; a frame without queries reads
  none; then again under `ICO_VK_DEPTH_ALWAYS=1`: 64, 64, 64; then with the pools made
  beforehand for 4 queries (`rhi_occlusion_prepare`, twice): the same counts, and a query past the
  4 dropped, not read); the adapter cell (`adapterCell`: `rhi_set_adapter_wanted` with a name no device
  has still starts, on the device a plain start picks, one `rhi_adapter_list` entry current and
  automatic; the plain start's own name picks it again; NULL is automatic); the sync cell (`syncCell`) checks the fence ring under `ICO_VK_FAKE_API=1.1`
  and `1.0`, and under `1.0` the device's version (1.0), the `VkRenderPass` path and the UINT
  views' usage structure (`VK_KHR_maintenance2`) (`rhi_vk_vk11`, `rhi_vk_vk10`); under `1.0bare` (a
  1.0 loader without `VK_KHR_get_physical_device_properties2`, review finding 5) the same, the device
  made with no extended queries (`rhi_vk_vk10bare`); the D32F cell (`d32fCell`) checks
  `RhiLimits.depth32Attachment` against `ICO_VK_FAKE_NO_D32F_ATTACHMENT` (`rhi_vk_vk10_nod32f`).
  Every run ends with the vertex stores' cell (`rhi_test_stores.c`, `rhi_test_vertex_stores`; alone
  with the argument `stores`: `rhi_vk_stores`, `rhi_vk_stores_vk11` under `ICO_VK_FAKE_API=1.1`,
  `rhi_vk_stores_global` under `ICO_VK_GLOBAL_BARRIERS=1`): 64 points of `vs_store` (each writes
  its four words at its element) through a pipeline with no fragment shader, no target and
  `noRaster`, in a pass without attachments, inside `rhi_cmd_buffer_store_begin` / `_end`; a copy
  over the buffer, the stores again, a copy out to a readback buffer, every word read back, no
  validation message (synchronisation validation on); 77 where the device reports no
  vertex stores, after checking that the bracket refuses (`rhi_vk_nostores`, the argument
  `nostores` under `ICO_VK_FAKE_NO_VERTEX_STORES=1`, passes only on that refusal).
- `rhi_d3d12_test.c`: the same checks on D3D12, a Windows GUI program that writes a log and shows
  a message box (`rhi_d3d12`).
- `rhi_d3d11_test.c`: the same checks on Direct3D 11 (WARP; WARP again with
  `ICO_D3D11_FAKE_NO_PARTIAL_CB=1`, the upload buffers' constant buffers mapped as on a device
  without partial constant buffer updates; the upload ring's written mark on WARP, where a submit
  must write exactly the bytes under the mark that its copy reads, 4096 of 8192, then 1024 in the
  next frame, 2064 for marks 512, 2050 and 1000 in one frame, and all 8192 of a buffer never noted,
  each read back byte for byte; the video memory fields of `RhiStats` on WARP: a budget, no more
  to reserve than the budget, two reads inside 250 ms equal; the hardware adapter, the swapchain on
  a hidden window, after each present the frame latency wait made ahead of the acquire
  (`rhi_wait_present_room`) asked twice with the same answer and false with a backbuffer acquired,
  Vulkan for comparison), the cells' shaders compiled at run time from
  `rhi_test_hlsl.h`; a Windows GUI program that writes `rhi_d3d11_test.log` and shows a message box
  (`rhi_d3d11`, registered on a native Windows build only). The `win-x64` and `win-x64-clang`
  cross builds build it; CI's `windows` job runs it with `--console` on WARP
  ([below](TESTING.md#the-render-suite-on-direct3d-11-warp)).
- `rhi_test_common.c` (every backend's run, after the dynamic state cell): the bind cache cell
  (`bindCacheCell`, [RHI.md](RHI.md#the-bind-cache)): one list recorded twice, every bind once and
  then each bind and state set three times in a row; the pictures equal and exact, the recorded
  pipeline and bind group binds equal, and the second run's `RhiStats` skips exactly 6 pipeline, 8
  bind group and 16 state more (two for each repeated call); a group bound again at another
  dynamic offset draws that offset's value (not skipped), a second pass binding the first pass's
  texture group draws the texture (Direct3D 11 unbinds shader resources at a pass's start), and a
  pipeline destroyed inside the list with a new one made in its pool slot (same index, new id)
  draws with the new one's write mask, not the old one's (`rhi_vk` and its variants, `rhi_gl`,
  `rhi_gl_nodual`, `rhi_d3d12`, `rhi_d3d11`; the line `<label>: bind cache cell passed`). The
  dynamic state cell's two repeats count every command of their groups in `stateSetsSkipped`.
  `rhi_vk_nobindcache` and `rhi_gl_nobindcache` run the same cells under `ICO_RHI_BIND_CACHE=0`
  (the bind cache off, [RHI.md](RHI.md#the-bind-cache)): the bind cache cell then wants no skip,
  the repeats' pipeline binds recorded (6 more) and the group binds the same, the same exact
  pictures; the dynamic state cell's repeats send every command again (none counted) and the
  dual-source leftover cell counts 3 commands and no leftover. `rhi_vk_serial` runs them under
  `ICO_VK_FAKE_SERIAL=1` (one compile at a time, issue 188): `RhiLimits.serialPipelines` set, the
  batch and the background set on one thread each, and a pipeline made on the test's thread while
  the background set compiles (the lock taken by both).
- `rhi_d3d12_plan_test.c`: the D3D12 backend's state and barrier bookkeeping on the CPU
  (`rhi_d3d12_plan`), with the external-compute cases (the storage grant by format, the capability
  answer, what the external end puts back).
- `rhi_native_test.c`: `rhi_native.h` on Vulkan (lavapipe): the device handles, textures granted
  storage, the storage barriers, the external begin/end bracket and a two-pass picture identical
  with and without it, and the FidelityFX extensions (`RhiComputeCaps.externalExts`: true on a
  Vulkan 1.2 device in an `ICO_FFX` build, false without the upscaler) (`rhi_native`; exit 77 without
  a device). `rhi_test_native.c` runs inside the
  common cells of every backend: the handles where offered, "not offered" on Direct3D 11 and OpenGL.
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
  attachment read). `rhi_gl_stores` (the argument `stores`) is `rhi_vk_stores`' twin: it skips (77)
  while the OpenGL backend reports no vertex stores, having checked that its bracket refuses.
- `rhi_gl_sync_wait_test.c`: the OpenGL backend's fence wait with a limit on the CPU
  (`port/rhi/gl/gl_sync_wait.c`, `gl_device.c` `gl_wait_sync`) over a stub fence and a fake clock: one
  that never signals ends at the 15 s limit in 300 slices of 50 ms, the pump between them, the 2 s
  line once; one signalling in the third slice returns with neither; a failed wait returns failed;
  no limit waits out a 100 s fence in 1 s slices (`rhi_gl_sync_wait`).
- `rhi_gl_glsl_test.c`: the OpenGL backend's GLSL inflate on the CPU (`port/rhi/gl/gl_glsl.c`, miniz):
  a deflated text round-trips NUL-terminated; a wrong text length, a corrupt or truncated stream and
  no stream give none (`rhi_gl_glsl`).
- `rhi_driver_version_test.c`: the driver version text of the adapter line
  (`port/rhi/rhi_driver_version.c`) for each vendor's packing, the Qualcomm and MoltenVK rules and
  the colour bytes a pixel of each Apple GPU family (`rhi_metal_color_bytes`), and the Mesa version out of
  a driver's name and `driverInfo` (`rhi_mesa_version_parse`: `Mesa 25.3.0-devel (git-bbdd688)` and the
  issue 163 tablet's `Mesa 26.3.0-devel (git-a995eaeda4)` as devel, a release, a distribution's `25.0.7-2`
  as no devel, a release candidate (`-rc` with a number), `mesa-25.1`, `MESA v24.2.8-DEVEL`, a bare version under a Mesa name, a Mesa word with
  no number then one with it; Qualcomm's compiler text, NVIDIA's version, empty, a lone major and NULL give
  none; a short buffer), `rhi_text_one_line` (Qualcomm's driver text with newlines on one line, issue 123) and
  `rhi_driver_compiler_serial` (issue 188: the issue 188 log's PowerVR B-Series text at Vulkan 1.1
  and the driver name alone at 1.3 give `PowerVR B-Series`; a Rogue text at 1.1, the D-Series
  DXT-48's text at 1.4, an unknown version, Qualcomm naming a B-Series and Mali give none)
  (`rhi_driver_version`).
 `rhi_adapter_match_test.c`: the `[video] adapter` rules (`port/rhi/rhi_adapter.c`): the exact
  name's index, the first of two equal names, no match for another case, a part of a name, a
  longer name, `""`, NULL or a NULL entry; the device-list text with and without scores, the
  unsuitable reasons, a short buffer cut (`rhi_adapter_match`).
- `rhi_vk_enum_test.c`: every `rhi.h` enumeration has a Vulkan mapping, `RGBA32_UINT` among them
  (`rhi_vk_enum`).
- `rhi_vk_present_mode_test.c`: the present-mode choice over the sets drivers offer, and FIFO
  over every set and preference under the FIFO fallback (Android's mailbox and FIFO with vsync off,
  Mesa's four modes, vsync on with mailbox preferred, FIFO latest ready asked), and the turned
  swapchain's choice (`vkr_prerotate_turns`: 90, 270 and 180 at issue 179's 1612x720; identity, a
  mirrored turn, a turn not offered, `[dev] prerotate = false` and images past the surface's largest
  keep identity) (`rhi_vk_present_mode`).
- `ffx_link_test.c`: the FidelityFX FSR 3.1 upscaler (`port/rhi/ffx`, `ICO_FFX`) linked into a C
  program and run through raw Vulkan calls (volk, not the RHI): the first Vulkan 1.2 device with
  the features the FFX backend reads enabled where present (16-bit floats, subgroup size control,
  synchronization2, non-uniform storage buffer indexing); the Quality render size at 1280x960
  (853x640); the backend and an upscale context for a 640x480 render and a 1280x960 output; its
  provider (3.1.x) and a non-zero GPU memory figure; the context destroyed; then the upscale
  dispatch with the textures the port hands it (`port/render/rd_fsr3.c`): an RGBA8 colour, a
  `D32_SFLOAT_S8_UINT` depth read through its depth aspect (moved from the RHI's read-only depth
  layout to `SHADER_READ_ONLY_OPTIMAL` around the dispatch, as rd_fsr3.c does) and an RGBA16F
  motion, 640x480, into an RGBA8 storage image at 960x720, a context with the port's flags
  (inverted depth, auto exposure) and the debug checks, four frames of FSR's jitter (phase count
  18), each frame's output read back: not black, its centre the flat input's colour; no FFX
  message. Any validation error or warning fails it (the layer's sync validation on). Skipped (77) without a loader, a Vulkan 1.2 device or a device it can make
  (`ffx_link_test`).
- `rhi_vk_swapchain_test.c`: the window path without a display, on SDL's offscreen driver;
  `swapAcquired` per frame; the acquire's wait with the list that uses the image (issue 179,
  `acquireWaits` through `vkr_test_submit_waits`: a list of off-screen work before it waits for
  nothing, the image's list on the acquire at `COLOR_ATTACHMENT_OUTPUT`, a dropped acquire drained
  by one empty batch at `ALL_COMMANDS`; on the fence ring in `rhi_vk_swapchain_vk11`; and the
  acquire's fence: the image's list counts one picture in `acquireReadyAtSubmit` or
  `acquireNotReady` (which one depends on the machine), a dropped acquire counts none)
  (`rhi_vk_swapchain`).
- Helpers: `rhi_test_common.c`/`.h` (the shared cells, seven of whose pipelines come from one
  `rhi_create_pipelines` call with a failing description among them, and the fill-rule cell:
  edges through pixel centres under the top-left rule on every backend; and the dynamic state
  cell, `dynamicCell`, whose last case, `dualLeftoverCell`, draws an opaque quad after a SRC1
  lerp on one dynamic-blend pipeline and wants exactly 2 dynamic commands for it, `blendEqOffSets`
  and `dualLeftovers` 1 each and its colour exact at an even and an odd column, skipped without
  dual-source blending or dynamic blend, as under `rhi_vk_nodual` and `rhi_gl_nodual`), `shaders/`
  (`rhi_test.hlsl`, its compiled headers and `gen_shaders.sh`), and `compare_backends.cmd` with
  `compare_png.ps1`, which render every dump on Vulkan and D3D12 on Windows and compare the PNGs
  within 1 LSB.

## port/shaders/test

- `shaders_table_test.c`: every embedded entry compiled for SPIR-V and signed DXIL, the vertex
  pre-pass's among them (`expectedPrepass`), the colour adjustment's box blits (`expectedTone`),
  FSR 1.0's two pixel shaders (`expectedFsr1`, with AMD's `ffx_a.h` and `ffx_fsr1.h` among the
  embedded Direct3D 11 sources) and
  the cluster entries' ring builds (`vu_skin_ring_plain_vs` and its two kin in `expectedPlain`)
  (`shaders_table`).
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
  (`checkAbove1xSteps`); and `blit_vs`'s quarter turns (`DrawCB.aux1.w`, the present into a
  pre-rotated swapchain): a 3x2 picture with four coloured corners turned 90, 180 and 270 degrees,
  every pixel against an image written out by hand from the Vulkan spec's `ROTATE_90` ("rotated 90
  degrees clockwise") and Google's pre-rotation sample's `glm::rotate` by +90 degrees about Z, so a
  90 and a 270 swapped in the shader fails here even when the twin's CPU table is swapped with it
  (`checkTurns`).
- `vu1_test.c`: the [VU1](ARCHITECTURE.md#terms) program shaders against their CPU references and
  hand traces, the card verdicts handed over as the renderer does, and the CPU's card test against
  the reference; the record: every mesh case's vertices through its pre-pass entry and its
  triangles through the record stages (the tick stage under the tick cameras), every particle
  case's sprites through `vu_particle_pre_vs`, and its probe and pictures through `vu_main_vs` or
  `vu_particle_main_vs` and `vu_ps` byte for byte the program entry's (with the same `vu_ps`), the
  probe within the CPU reference's bounds, the mesh, particle and tick-camera cases counted (part
  b (4)); the side bands (part a's rule, between-ticks and facing rows, a card keeping the drop
  beside the picture; part b's `DEPART_EYE_EDGE`
  cases at the picture's right edge, turned to the eye and away; every other case drawn with
  `ICO_VU_NO_BESIDE`); the camera rule (part a's `vu1ref_region_camera` rows; part b's
  `Case.camera` cases drawn with `ICO_VU_CAMERA`: behind the eye out of the box and Enhanced, out of
  range out of the box, a card, the 48:9 edge, between two ticks, turned to the eye drawn clipped
  and away dropped; a grid card out of the box two-sided, drawn; a sprite past its window out of
  the box, drawn whole); the pictures use the plain mapping (no exact viewport), so the planes stay
  the GPU's there; every cluster case without the extended flags and tick cameras drawn again
  through its ring build (`vu_X_ring_plain_vs`, the bones through t5 from `IcoVuCB.mark[3]`, a
  layout with slot 5): probe and pictures byte for byte the plain variant's, the count printed and
  required above 0 (issue 123's ladder, [VU_SKIN_CHECK.md](VU_SKIN_CHECK.md#the-ladder)) (`vu1`,
  `vu1_mali`).

## port/test

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
- `unused_content_audit_pal`, `_jpn`, `_kor`, `_ntsc`, `_aug6`, `_jun26`, `_cn`
  (`tools/unused_content_audit.py --slug <disc> --check`; no C program): reads the disc image's
  DATA.DF directory, the packs' member directories (the head of each pack's deflate stream, no
  member's data except the animation files that decide a model's use), the folder's files and the
  boot ELF's tables (`port/data/gen/<disc>/table_desc.c`), and compares the disc's section of
  [UNUSED_CONTENT_AUDIT.md](UNUSED_CONTENT_AUDIT.md) with it: the entries no table or program string
  names, the stage rows no exit row of a reached stage leads to (roots: the boot's logo and the
  title's attract in `op.c`, whose literals the tool also checks are still in the source), the
  movie rows, the logo pack's and test rooms' models by who uses them, the stream motions and sound
  streams, the debug files. Skipped (77) without the image (`ICO_DISC_IMAGE`, `ICO_DISC_IMAGE_<DISC>`).
- `layout_asserts.c`: the generated struct layout asserts, which hold at compile time
  (`layout_asserts`).
- `gs_blend_test.c` is a standalone CPU program on the GS blender's drift, not a ctest.
- `ico_check.h` and `ico_shard.h` are the framework above.

## port/ui/test

- `credits_headless.py`: Extras > Credits in the headless game (`credits_headless`).
- `font_coverage_test.c`: every character of the eight languages and the community translations
  (their tables and subtitles) has a glyph in Arimo, in Noto
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
- `settings_desktop_test.c`: Options > Extras > Add to the applications menu over a fake handler
  (`settings_desktop`, and on the other discs `_jpn`, `_kor`, `_cn`: the row and the disc's confirm
  button): hidden without a handler and on the pause menu, shown on the title right after Add to
  Steam with its words, Down from Add to Steam lands on it, Cross runs the handler once and its
  answer is the popup (done, failed, an unknown answer as failed), the page kept, the strings
  translated in every built-in language.
- `settings_backup_test.c`: Options > Extras > Back up your data and Restore from a backup over
  a fake `UiBackupHost` (`settings_backup`, and on the other discs `_jpn`, `_kor`, `_cn`: the rows
  and the disc's confirm button): the rows hidden without a host and on the pause menu, shown on
  the title after Export textures with their words and notes, Down and Up between them, the page
  fitting; Back up: Cross asks once, the dialog's note, the progress, the host's pump on every
  `ui_settings_vsync`, the done popup once with the files and the name, the failed popup's
  reason, nothing for a closed dialog or a begin the host refused; Restore: the first press asks
  nothing of the host and shows what is replaced, the label asks again, the press runs out after
  8 s and when the cursor moves off, the second opens the dialog; the backup read: its popup
  (version, date, discs, another disc) once, the label and the note, the press that restores it,
  the progress, the done popup for a restart and for a close, nothing asked while the run ends,
  the backup dropped when the cursor moves off and after a minute, a refused one's reason; the
  strings translated and the row names fitting in every language.
- `settings_test.c`, `settings_extra_test.c`, `settings_extras_test.c`: the Settings menu over
  fake tables, CPU only (`settings`, `settings_extra`, `settings_extras`); `settings_extras_test.c`'s
  `testUnused` (Extras > Unused content over a fake `UiUnusedHost`: the link hidden without a host
  and, with one line, with no row; the rows per `available`; Cross calling `open` once and
  leaving on its layout; the reopen; nothing from the pause menu) and `settings_discs_test.c`'s
  `checkUnusedRows` (each disc's four rows and the link through the real CPU side over a fake
  DATA.DF engine); `settings_test.c`'s
  `testShowFps` steps Display > Frame rate counter (Off by default, Right then Left both switch it, the preset kept, `[video] show_fps` saved as `true`, a file's `true` read; the Display row list is nine rows with the new row after Frame rate, and the pause menu's Display page is eight with Video mode hidden, both in the Options window's slots ([UI.md](UI.md#the-window))); `ui_fps.c` itself is in the window build only and has no test (the number is `fps_counter_test`'s; a hand check: the corner, legible, off by default; on a phone with the touch controls on the top edge between the D-pad and Select);
  `testEffects` steps Motion blur round Off, Original (the default) and Smooth, each step saved as
  `effect_motion_blur` and `effect_motion_blur_mode` with the getters at once, the note under the
  row and the page fitting from both menus; `settings_test.c` also
  `testDownsample` (the Downsampling row right after Anti-aliasing on Picture and not on Display, Off by default, its note, Right and Left, the preset kept, `[video] downsample` saved and read), `testUpscale` (the Upscaling row right after Downsampling with Sharpness under it, hidden while Upscaling is Off, Off by default, its note, Right and Left, Sharpness 80 stepping by 10 and held at 0 and 100, the preset kept, `[video] upscaling` and `upscaling_sharpness` saved and read), the texture and model pack rows and the CRT rows on Picture, the dump rows and the Debug menu through the Developer page (five rows with Developer mode on, four on the June 2001 demo, two without), and `settings_photo_test.c` writing `settings_picture.png` and `settings_developer.png` beside the other pages; `settings_test.c`'s `testNotesFit` (every page's note in all eight languages wraps to at most two lines and is kept whole by the note row's 256-byte buffer); `settings_extra_test.c`'s `testGraphicsApi` (Options > Graphics: the link hidden with no renderer names and with one, shown after Effects with four; the Graphics API row's value text and the `video.backend` key through Right round Vulkan, OpenGL, Direct3D 12, Direct3D 11 and Automatic, Left wrapping, the key saved after leaving the page, an unlinked value shown as written and stepped from Automatic) and `testGraphicsApply` (Graphics > Apply over a fake `UiGraphicsHost` running `vulkan`: Apply hidden without a host, for Automatic and Vulkan, with one renderer, shown for OpenGL with its note; Cross asks for `opengl` once; the countdown's Keep and Revert alone with the cursor on Revert and the note counting 10 then 9 s; 600 `ui_settings_vsync` calls at 60 Hz ask for `vulkan` with the timeout reason and put the value back to Automatic; Keep calls `kept()` and saves `[video] backend = "opengl"`; Revert and Triangle ask for the backend that ran and put the value back; a failed answer's popup names both renderers and puts the value the page found back (no Apply then); a refused one shows nothing and keeps the choice; a pending one is polled every frame with Cross ignored until it answers; `ui_settings_save_on_quit` during the countdown writes the value the page found, the countdown going on, and Keep then saves the choice; Automatic with the host's `automatic()` naming OpenGL offers no Apply while OpenGL runs and offers it on Vulkan; the strings translated in every language) and `settings_test.c`'s `testBuild` (the Main page's sixteen rows, the Display page's nine, the Picture page's eleven, the Effects page's eight, the Developer page's five, the Gameplay page's ten with its three links before Back, the Yorda page's five, the Shadows page's five, the Combat page's eight, the Cutscenes page's four, the Extras page's eleven with the Unused content link fifth, the Unused content page's five and the Graphics page's six: Graphics API, Graphics card, Apply, Keep, Revert, Back); `settings_adapter_test.c` (`settings_adapter`, and on the other discs `_jpn`, `_kor`, `_cn`: the row alone): Options > Graphics > Graphics card over a fake `UiGraphicsHost` with a fake `runningAdapter()`: the row and, with one renderer, the link hidden with no card names and with one, both shown with two and the Graphics API row hidden, the note, the page fitting; Right round the two cards and Automatic writing `video.adapter` (`""` for Automatic), Left wrapping, the key saved; a saved name the list lacks shown as written and stepped from Automatic; Apply hidden for the card running and for Automatic on Automatic's card, shown for the other card; Cross asks for `vulkan` on that card; the countdown hides the row; 600 vsyncs ask for the card before with the timeout reason; Keep saves `[video] adapter`; Automatic on the other card asks for `""`, Revert for the card before; a failed answer's popup names both cards and puts the value found back; the strings translated in every language; the Main page's row lists in the settings tests read sixteen rows (seventeen with the rc build's Share a bug report); `settings_extra_test.c`'s Graphics driver case (`testGpuDriver`, a fake host) checks the eight answers' popups (picker not opened, no picker app, file unreadable and the wrong memory page size, whose popup holds the host's own sentence, among them), the note's last-seen line for each driver (the phone's own, the first, one never started: the line first, the Adreno note after it, a failed driver's note kept, the note alone again without a line from the host) and the Add from the drivers folder row: hidden at 0 zips, shown at 2, Left and Right going round the names, Cross calling `installFileBegin` once for the shown zip with a second Cross ignored while it runs, the poll's popup; presses Right in photo mode through Deja vu and checks the popup's words (`ui__photo_film_word`)
  and holds the fixed row lists of Controls (14 rows) and Touch screen (9 rows);
  `settings_extra_test.c` opens the Touch screen page (`testTouchPage`: the link only with a touch screen, the three overlay rows' values and save, both pages fitting from both entries) and the Edit layout page over a fake editor host and a real editor (`testTouchEditorPage`: open, Back to the Edit layout row with the edits kept, locked while Touch controls is Off or without a host, Snap to grid and Grid size saved as `touch.snap` and `touch.grid`, Reset layout and Reset all layouts), and steps Controls > Hold type from the title and the pause menu (the disc's own first, then A, then B), checks the saved
  `gameplay.hold_type` and `hold_type_chosen` and that `ui_settings_install` sets `optionControlType` from it, a bare `"hold"` (the old first-run text) as the disc's own (`testHoldType`),
  steps Effects > Water reflections between Original and Sharp, checks its
  note, its key and the page's eight rows (`testWaterReflections`), and Main's Disc row
  (`testDiscRow`: shown with more than one disc's game data only, its values and `[game] disc`, the page
  with all fourteen rows), and Main's Save a picture for a bug report (`testReportPicture`: shown with
  a host handler only, with developer mode off and on, directly above the Developer link, Cross calls the handler once and
  shows its popup, the page with all fifteen rows, the label fitting and translated in every
  language), and Share a bug report (`testShareReport`; in the default build `rc_tools_off`: on a
  phone with a share handler and every other row shown, Main has its fifteen shown rows (sixteen built), none of them the
  row and no English string its text; with `ICO_RC_TOOLS` the row below Save a picture, its note,
  Cross calling the handler once, hidden on a desktop and without a handler, sixteen shown rows (seventeen built) from
  26, the strings fitting and translated). Each also runs with `jpn`
  as the Japanese or Korean disc over the fixture's tables at that disc's numbers (`settings_jpn`,
  `settings_extra_jpn`, `settings_extras_jpn`; `kor` the same, `settings_kor`, `settings_extra_kor`,
  `settings_extras_kor`, with the numbers from the Korean region row): the rows it has not, Menu language, Circle
  confirming, Cross and Triangle going back, the title's rows, the notes' serial, the mouse,
  the prompts and the boot's hand-over to the title with Skip the opening
  (`testSkipOpeningTitle`).
- `settings_photo_test.c`: the Settings screens through rd on a device, written as PNGs
  (`settings_render`, which also draws the Touch screen page, the editor page and the main page
  scrolled by nine Downs, `settings_main_scrolled.png`, the Picture and Developer pages, `settings_picture.png` and `settings_developer.png`, and Gameplay's Yorda, Shadows and Cutscenes pages opened through Gameplay, `settings_gameplay_yorda.png`, `settings_gameplay_shadows.png` and `settings_gameplay_cutscenes.png`, also at 1080p); with `jpn` or `kor`, that disc's title and main page (`settings_render_jpn`,
  `settings_render_kor`); `cn` the same for the Asian disc (`settings_cn`, `settings_extra_cn`,
  `settings_extras_cn`, `settings_render_cn`, `ui_cn`, `gallery_cn`, `model_viewer_cn`); `ntsc`,
  `aug6` and `jun26` run settings_extra_test's game variants pages, the Options window
  (`testScroll`) and the boot's hand-over to the title with Skip the opening
  (`testSkipOpeningTitle`: the boot's layout 4 handing over to 51, the title 9; issue 160) only
  (`settings_extra_ntsc`, `settings_extra_aug6`, `settings_extra_jun26`: Cross
  confirms, the 2001 notes, the demo's 7-hit barrier, its toughness row without American and no
  Debug menu row; the window's edge moves through that disc's own cursor move).
- `settings_extra_test.c`'s `testSecondCard`: Options > Gameplay > Second memory card from the
  title and the pause menu, after the East Reflector door fix and before No hints, Off by default; a
  registered listener hears the current value, Right turns it on with one call and sets
  `gameplay.second_memory_card`, the saved file has it, its note shows on the cursor, Left turns
  it off with one more call; the cleared page still fits. `settings_test.c`'s `testBuild` reads
  the Gameplay page's ten rows, `testGameOptions` four Downs from the stick fix to Film effect, and
  `settings_discs` (every disc) shows the row from both entries.
- `settings_test.c`: Gameplay > Yorda's seven rows (the Chain swing speed and Chain swing slowdown rows
  after Natural chains), their value text ("4 (original)", else the number), ten steps round
  4..10, 1..4 with the key written as the number and the achievements untouched, and their notes after
  Natural chains' note.
- `settings_extra_test.c`'s `testNaturalChains` and `testSkipOpening`: Options > Gameplay > Yorda >
  Natural chains (after Yorda trips, before Back) and Gameplay > Cutscenes > Skip the opening (the
  page's first row), each opened through Gameplay (`openGameplayPage`, `openLink` in
  `settings_fixture.c`) from the title and the pause menu, Off by default, Right on and the key,
  saved, the note on the cursor, Left off, the page fitting, Back to Gameplay and then to the main
  page.
- `settings_extra_test.c`'s `testVariantsPage` (`testVariants`, and `testVariantsOtherDisc` on
  `jpn`, `kor`, `ntsc`, `aug6`, `jun26`), from the title and the pause menu: no Game variants row
  on Extras (ten rows); Gameplay's labels with No hints and the three links; Gameplay > Shadows'
  five rows and their shipped values, Peaceful shadows Off, On (the hook 0, the key, the note
  "Switches achievements off.", the rule suspended) and Off again, Shadow toughness round its
  values (no American on the demo, no Third journey on `ntsc`, `aug6`, `jun26`; the Third journey
  key and note), aggression's five; Gameplay > Combat's eight rows and their shipped values (the demo's
  7-hit barrier), the three combat rows Off and On with their notes, the barrier's two, One hit
  per swing and Shadows never dodge on and off with the achievements rule and the notes, the
  saved file;
  Gameplay > Cutscenes' four rows, No cutscene look-around and Ignore cutscene cameras on and off
  (the 2001 discs' own notes); `checkPageFits` on each; Developer's Debug menu hidden without
  Developer mode, shown with it (not on the demo) before Back, its note, unbound staying on
  Developer, bound opening the game's menu once and closing Options into the title or the game.
- `settings_test.c`'s `testFreeMovement`: Options > Developer > Free movement after the Debug
  menu, shown with Developer mode, Off by default, Right turning it on (`ico_free_movement`, the
  key set and saved), its note on the cursor, Developer mode off leaving the row's value and
  switching the movement off, Left turning it off; `settings_test.c`'s Developer page now lists six
  rows (five on the demo); `settings_discs_test.c` shows the row on every disc with Developer mode
  and walks Remap controls to its end: Ask Yorda for a hint after the right stick's Right on the
  hint discs (pal, jpn, kor, cn), Reset to defaults straight after it on ntsc, aug6 and jun26
  (`settings_discs`, `settings_discs_<slug>`); `strings_complete_test.c` checks the three new
  strings in the eight tables.
- `settings_extra_test.c`'s `testShadowsTogether`: Options > Gameplay > Combat > Shadows attack
  together from the title and the pause menu, after Shadows never dodge and before Queen
  barrier, Off by default; Right turns it on (`ico_opt_shadows_together`, and a Shadow does not wait:
  `ico_opt_shadow_waits(1)` is 0) and sets `gameplay.shadows_together`, the saved file has it,
  its note shows on the cursor, Left turns it off (the game's turns again); the cleared page still
  fits. `settings_discs` shows the row on every disc from both entries.
- `settings_extra_test.c`'s `testScroll` (the Options window, [UI.md](UI.md#the-window)): Controls
  from the pause menu, with a touch screen, opens at the top with the down mark only (Effects and Display fit in the window and do not scroll); Down on the window's last row scrolls one
  row, Up on its first scrolls back; Up on the first row wraps to Back and Up on Back shows the end
  on the last row; Down on Back shows the top on the first row; three wheel notches move three rows
  across the edge; a click on a mark is Up or Down on the cursor's row; Characters from the pause
  menu keeps its swatches with their rows in and out of view and its marks right of the swatches; a
  reopen on Touch screen from the title, then Back, lands on Controls' Touch screen row with the
  window scrolled to it. `checkPageFits` (the fixture) checks a page's window at every step: the
  rows in view one slot each from `UI_SETTINGS_WIN_Y0`, at most `UI_SETTINGS_WIN_SLOTS`, Back in
  view and last, every row the rule shows in view on a page that fits, the marks shown exactly
  while rows are out of view that way. `testGraphicsApply` checks the countdown (Back hidden: two
  rows, no window).
- `strings_complete_test.c`: every language's table defines every `UiStrId`, none empty but
  `UI_STR_NONE`, each with the English entry's printf conversions; the four pack notes filled
  with each disc's serial (`ui_str_serial`); the sheet language's default and setter; the menu
  language's rule (`ui_lang.c`, its inputs stubbed); a community translation's table (an id it has
  not translated NULL, English at run time, at least half translated, its name in every table;
  in the Brazilian Portuguese one, rows cobra838 corrected or filled; the Gameplay pages' three
  names, Yorda, Shadows and Cutscenes, in the eight tables' own words and NULL, English, in the
  three community tables),
  the translation in force (`ui_lang_text_resolve`: the European disc in English only), Auto
  following it, the menu words over the English sheet (`ui_menu_words_language`); the Japanese
  line-break rules (`strings_complete`).
- `subtitles_ru_test.c`: the Russian (bob-ttz) translation over the settings fixture: its 42
  subtitles (the CSV's records, Yorda's eight second-play only, two lines of 50 characters at
  most, a glyph each), the words `ui_subtitle_words` gives through `[game] text_language` and their
  layout in the band, the Language row's entry after the five (the English game data, the key,
  Auto, the menu words, back to English) and the five sheets' strips unchanged (`subtitles_ru`);
  on the Japanese, Korean and Asian discs no entry and nothing in force (`subtitles_ru_jpn`,
  `subtitles_ru_kor`, `subtitles_ru_cn`). The Language row's step after it is now Russian
  (cobra838), then Brazilian Portuguese, then English.
- `subtitles_ru_cobra838_test.c`: the same for the Russian (cobra838) translation, a copy of
  `subtitles_ru_test.c`: its own 42 subtitles (his pull request's records, his words and not
  bob-ttz's, the same eight second-play only), `[game] text_language = "ru-cobra838"`, the
  Language row's seven entries (his the seventh, the key, Auto in his Russian from the file's key,
  an untranslated row in English, eight steps back to English, Brazilian Portuguese after it) and
  the strips with his words (`subtitles_ru_cobra838`, `subtitles_ru_cobra838_jpn`, `_kor`, `_cn`,
  in the `settings_toml` lock).
- `subtitles_pt_br_test.c`: the same for the Brazilian Portuguese translation (Igor Chagas,
  ichagas7, issue 157), a copy of `subtitles_ru_cobra838_test.c`: its 42 subtitles (his CSV's
  records, the same eight second-play only, two lines of 50 characters at most, a glyph each),
  `[game] text_language = "pt-br"`, the Language row's eight entries (his the eighth, the key,
  Auto in his Portuguese from the file's key, two untranslated rows in English, eight steps back
  to English) and the strips with his words (`subtitles_pt_br`, `subtitles_pt_br_jpn`, `_kor`,
  `_cn`, in the `settings_toml` lock).
- `ui_mouse_test.c`: the pointer's arithmetic in the menus (`ui_mouse`).
- `ui_test.c`: the embedded fonts, UTF-8, the runtime text; each language's own Deja vu string
  (`UI_STR_PHOTO_FILM_DEJA`); a Japanese popup body wider than the panel takes a second line, an
  English one keeps its one; the popups' timing in seconds at the default 50 Hz and at 60 Hz
  (`ui_popup_set_vsync_hz`) (`ui`); with `jpn` or `kor`, the glyph rows, the table sizes and the pad
  convention on that disc (`ui_jpn`, `ui_kor`).
- Helpers: `settings_fixture.c`/`.h`, `headless_common.py`.

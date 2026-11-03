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
- `door_grace_test.c`: the doorway fix's arm and grace and a copy of the exit check's guard
  (`door_grace`).
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
- `options_test.c`: `test_shadow_speed_motions` selects each of the seven discs and checks the motions Shadow speed scales (the walk, run, fly and carry loops by their motion-table names) and the ones it leaves (the stand, the fly start, the run's end, an attack, the grab, a hit, the dissolve, the pit dive; review finding 15); the gameplay options' defaults and setters (Hold type: the disc's own by default, `toggle`
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
- `title_logo_test.c`: the logo's models and the hidden state's steps (`title_logo`).

## port/input/test

- `input_record_test.c`: the pad recording's format, round trip through the script reader, the
  change-only rule and the sample point (`input_record`).
- `input_test.c`: the `[touch]` tables onto the layout edits and back (the `format` key, a file without the key reading `hidden` and `o0` as Off, rewritten as `off`), bindings from config text, dead zone, quantisation, the stick fix, merging, the
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
- `diag_test.c`: the crash report (also from a worker thread under a batch's pipeline note), assert, watchdog, progress (with the heartbeat's start-up set count), start-up activity file (the batch notes; a start ended inside a 2D batch: the next start is lazy for that start only, nothing written, and logs the line and the last finished batch with its memory; that lazy start ended inside a pipeline: the start after writes `[dev] pipeline_set = "lazy"` through a stubbed config write, for any version (another version's line then leaves the policy to the count), with a failed write (this start only, its lazy run saying so, and the count kept so the next end writes it); a count file that cannot be written, logged; the "for this start only" run's clean exit and its end between two creations, each with its own line: the next start tries the set again and the row starts over; not after a restart or with the setting already `lazy`; an end in the background, by the lifecycle's event or `onTrimMemory(20)` (the mark set and logged once when both come), or with the app in the background before the recording began, is not counted, its mark written, logged and deleted, and back in the foreground the mark is gone and the end counts; a lazy run ended in the background between two creations, its mark logged; on a device the table makes lazy the first end promises nothing and its lazy run is not "for this start only", the second writes nothing, both clearing the row; the first end of a full set: the next start is careful, once, and the file reads `set: careful` after the note; a careful start that finishes clears the row and the start after it is full again; a careful start's own end makes the next start lazy; `pipeline_set = "careful"` and a careful end; the careful set's budget stop (`creating pipeline fog test` and `set: careful` in the file; its clean exit is "for that start only", then `pipeline_set = "careful"` is kept); the signals that stop the set (`actlowstop`: not `onTrimMemory` 5, not 10 before the set's first batch, 10 past it, 15 over 10) and the memory text with the device's; the set stopped at the system's low-memory signal (`set: stopped at the system's low-memory signal (running critical)` after `set: careful` in the file through the lazy run's clean exit, the batches' and the system's lines kept, the row cleared), the next start of the version lazy from it (pipeline set 7, nothing written, its own file keeping the line), that start's end inside a first-use pipeline stepping to lazy for one start (pipeline set 4), the stopped run's end between two creations (lazy from the file) and inside one (lazy for one start), another version trying the set again; a note of the retired extended variants counted as the pipelines' kind; a VU vertex pre-pass batch's end (a `vu_main_vs` batch among them): no pre-pass for that start only, the next such end writes `[dev] vu_prepass = false`, a run that built its pipelines and finished clears the row, the low-memory signal's line at once and as the file's `system: ` line, in the set and in a lazy run, the level-80 one marking the background) cases, Android's start after a device loss (`actlossnext`: robust access for that start, the second loss in a row writing `[dev] gpu_robust = true`, a loss in the robust start itself counting as the second, a start without a loss that did not run robust breaking the row, a failed write keeping the count, the player's `gpu_robust` ruling, a relaunch counting nothing), device-loss restart and loss bisect (the loss noted in the activity file, the bisect stubbed: once, before the game, never again after a start ended inside it; the restart's steps robust 7, blend 8, backend 9: `lossrobust`, `lossstep`, `lossbackend`; a loss on the renderer Options > Graphics > Apply switched to and not kept yet, `losstrial`: its line, restart 0 and no relaunch; the same kept, `losskept`: `blend:opengl`) cases, the loss bisect's second level (its state file, the step stubbed: the draw found in 4 starts of at most 3 lost steps each, draw 1 with its classes in 5 starts, killed at step 3 and continued at 4, a renderer that did not start, the step and start bounds, the time budget, files that do not parse) cases, the watchdog and a wait the system holds (the event pump held past the limit without an end, a stall in the game's code after it ended, the bound for one wait, that bound with no limit in force), a GPU hang with the backend's progress faked (`hang`: the WATCHDOG block's driver and wait lines, the heartbeat's wait, the dump before the report, report.txt's end, the device loss's box on Linux, the hang's own loss line in the activity file, which the next start counts, with Android's robust step, and does not bisect; `hangnodump`: the hang's line without a dump, still counted; `hangnot`: no wait in progress, a freeze with the GPU's line and no loss; `hanglimit`: the GPU hang limit, 2 s, ends the run before the later limit of 60 s, its WATCHDOG line naming the submission and the limit, the rest the hang's), the program a restart starts (`relaunchexe`: the AppImage file when `APPIMAGE` is trusted, the program itself when it is not or is unset), each in a child process
  whose log is read back; the symbolizer (`symmap.h`) on a small GNU ld map of an ELF program (a wrapped function section, a static function in an object, an anchor not at the module's start), a Windows map excerpt, an nm table and its gzip form past inflate's 32 KB window, a cut gzip member and a map without the anchor; the crash's and the watchdog's symbol lines from `diag_test.map`, linked beside the test as `ico_pc.map` is beside the program; their report zips (`report.h`) read back with miniz (report.txt's end, summary and report block with its symbol lines, the log; the crash's report.txt kept as `diag_test_report.txt`, an example); the prune keeping the newest five; `ico_log_limited` (250 calls: the first 8, the 100th and 200th, the end summary's count; `ico_log_limited_n` with SDL's error bounds, the first 64) (`diag`).
- `share_report_test.c`: an rc build's Share a bug report's files over a fake logs and dumps
  folder: the log, the run before's when there, the newest dump zip by modification time (a loss
  zip included, an `.rddump` left out), the newest `-present.png`, the newest `report-*.zip` (not
  another `report-` file), a tie in time to the greater name, missing folders skipped, the names
  line and a short buffer (`share_report`).
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
- `dump_zip_test.c`: F12's zip (with the tick before as its second `.rddump`, and the blended present's frame moved in third by `ico_zip_move_file`, the loose file gone), the aspect sweep's zip and the device-loss zip (the lost frame first, the second `.rddump` the frame before, the pruning to the newest three), written and read back with miniz
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
  the first-run `config.toml` writing every listed key it holds at that default (`sim_keys`).
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
  15 s, counted from the last creation; between pictures nothing counts); pure (`gpu_hang`).
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
  The Android file picker ([PLATFORM.md](PLATFORM.md#the-file-picker): `IcoActivity.pickFile`,
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
  auto, off or fifo; `glow_fold` as `ICO_RD_GLOW_FOLD`), a restart's
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
- `memory_floor_test.c`: the start-up set's free-memory floor (`memory_floor.h`) by driver class: on Qualcomm's own driver and a driver not known 17 % of the device's memory, at most 2048 MB on a device of more than 8 GB (696 MB on a 4 GB phone, 522 MB on a 3 GB one, 1392 MB on 8 GB, 1445 MB on 8500 MB, 1915 MB on the Retroid Pocket 6's 11265 MB, 2048 MB on 12288, 15552 and 16384 MB); on any other driver the same up to 8 GB and at most 512 MB above (8500 to 16384 MB); a setting gives the floor for every class, `0` none; the class from the vendor id and the driver text (0x5143 with Qualcomm's own text, with "turnip Mesa driver", "Turnip Mesa 26.3.0", "Mesa 25.1.0"; PowerVR, Mali, vendor 0) and its words; the PowerVR DXT-48 phone's figures (15552 MB, another driver: 1823 MB, where the 2048 MB cap stopped its set, and 2554 MB continue, 512 MB continues, 511 MB stops, a setting of 2048 stops at 1823); the issue 161 start on the Retroid Pocket 6 on Qualcomm's own driver (11265 MB: 3340 MB continues and 1693 MB, where it stalled, stops under 1915 MB; 1915 MB continues, 1914 MB stops; a driver not known stops at 1693 too; a setting of 1900 stops at 1693, 1600 does not; on Turnip 1693 continues); a 4 GB phone at 1200 MB available continues, at 600 MB stops, on every class; an unknown figure never stops; the log lines' words for each class, a setting and off (`ico_memory_floor_rule`); the `[dev] pipeline_memory_floor_mb` text ("" and "auto" unset, "0" off, "2048" a floor, "-1" and "x" not read); issue 123's retry (`ico_memory_floor_retry`): the next start's set again only after a stop at the floor (the activity line's figures tell it from the system's signal), on Qualcomm's own driver, with a cache file that is not empty (a missing or empty one, the system's signal, another driver and a driver not known make no set), the reporter's 1940 MB of 11453 MB and 1.4 MB file (`memory_floor`).
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
  3.5 ms on the render thread keeps every step on time, one present a step; `testAsyncRates`: the
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
  or none known), and the calling
  thread (its core inside the cores it may run on, the priority text's `nice`, a pin to no core
  refused, a pin to the cores already allowed taken), and a pin's result by the cores read back
  (`cpu_place_pin_result`: core 7 asked, the call's yes and 2-7 read back is other cores; the same
  mask kept; a refused call; nothing read back; on Linux the checked pin to the cores allowed
  kept, a core the system lacks refused with its errno) (`cpu_place`).
- `fps_counter_test.c`: the Frame rate counter's number (`port/platform/fps_counter.h`): 0 before
  the first second, 50 presents a second give 50 at every whole second, a second that ran 1.5 s long
  divides by its real length and rounds to the nearest (91 presents 61, 89 presents 59), replays
  counted through the renderer's count being zeroed after a tick (100 a second, none lost), a
  change of mode starting the second again, a time going backwards changing nothing (`fps_counter`).
- `pace_policy_test.c`: the slow-present decision, Resolution "auto"'s steps, the gap between
  presents, the vsync lock's n and alphas and its period's sanity, and the vblank clock's laws on a
  synthetic display, and the late-present skip: a whole period behind skips, with or without
  sound, less does not, slow presents skip past the deadline, never two frames in a row while the
  same frame may stay skipped; the timer path's loop under a fake clock keeps a 30 frames a second
  game on 39 ms presents at real time with no resync, where a frame skipped for one step only fell
  behind, and a 60 frames a second game on 20 ms presents through the late arm alone; the present
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
  span, the `window: GPU between replays` part word for word, a short buffer) (`pace_policy`).
- `perf_block_test.c`: a block of replays by phase: sums, maxes, the replays over a threshold, the
  longest phase (`perf_block`).
- `present_fallback_test.c`: the FIFO fallback's decision (`present_fallback.h`): the stock
  Adreno driver's blocks of 376-382 ms at 0, 9 and 14 s switch at the third and never again, the
  switch line word for word, the 10 s line's `toward FIFO` and `since presents went FIFO` words;
  two blocks 40 s apart, blocks 31 s apart and blocks under 200 ms (52 ms, 21 of 143 ms) switch
  nothing; Turnip's driver text is not eligible while the count still rises; `[dev]
  present_fallback=off` keeps mailbox with its line once; a count reached with vsync on switches at
  the next block once eligible; the driver texts and the `[dev]` values (`present_fallback`).
- `display_rate_test.c`: the refresh rate's log lines and the Android frame rate pin's decisions
  (`display_rate.h`, issue 123): the watch (first rate noted, 59.94 against 59.95 not a change, an
  unknown rate not one, 144 -> 60 -> 120 two changes), the block's line and the change's line word
  for word, `[dev] frame_rate_pin` (auto, off, false, 0, no, any case), the rate asked for each
  Frame rate row (60, 120, 144; Original, Uncapped, off and above 1000 none), the sequence set,
  nothing, set, release, nothing, a device without the call never asked, a refused ask tried
  again, the pin's lines (`display_rate`).
- `present_hist_test.c`: the 10 s histogram of the driver's present call (`present_hist.h`): a
  limit is not over itself, a call is counted in every row it is over, issue 123's block
  (750 presents of 9 to 15 ms and five of 344 to 365 ms: 5 in every row), the line word for
  word, an empty block's empty text, a small buffer cut, negative and NaN times not counted
  (`present_hist`).
- `pipeline_skip_test.c`: the record of a pipeline family a device's driver stopped in (`pipeline_skip.h`): the family a start-up activity note names (one sprite planes build alone, or a lazy creation, names it; the issue 161 mixed batch, a plain build, a vertex shader batch and a cut note name none; the in-flight key inside a mixed batch names it when that key is a planes build), the value written and read back, a match on the same device and driver only (not after a driver update or on another device), the clear (`pipeline_skip`).
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

## port/render/test

- `fog_lut_test.c`: the depth fog's game side, the same on every compiler (`fog_lut`;
  `fog_lut_arm64` under qemu).
- `gltf_test.c`: the glTF reader and writer, bit-exact round trips, GLB, a Blender-style file,
  refusals (`gltf`).
- `gsb_cull_test.c`: the game's cull against the blended pictures' cameras and the windows that
  scale with the width; with the disc, a stage (`gsb_cull`, `gsb_cull_stage`), and the disc-wide
  sweep (`gsb_cull_sweep.1of8` .. `8of8`, 77 without the disc image). `--stage DISC` is st18a at
  the camera of the reporter's dump i76 and the 768 camera steps about it (every part a picture
  between the ticks shows is registered by this tick; the one-camera control leaves some out).
  `--sweep DISC` generalises it to every stage folder of the disc (`object/sdf/stNN?/model/`, the
  member index as the `rd_mesh` disc walk builds it, numbered in index order, shard q of 8 takes
  the stages numbered q mod 8) at the camera of every line of `tools/sweep_cameras.txt` (stage,
  dump path; a .rddump or an F12 zip; the dump's view and zoom; a line whose dump is absent is
  skipped; `builtin:i76` is the i76 camera of st18a) and the 768 steps about each, every 4th pair
  by default (`--sweep-step N`, 1 is the whole sweep), at 16:9 (688 GS pixels wide) and 48:9
  (2048). Per stage and aspect it counts the mechanisms that leave an in-view part or triangle
  undrawn: the box cull (parts a picture between the ticks shows that this tick did not
  register: `box_gap` not drawn, `box_hidden` drawn as the tick before's), the blend span (parts
  registered by three cameras against one, and the one-camera control's losses), and the region
  rule's drops through the CPU oracle (`vu1ref_region_draw` with the corners' real region bits,
  `extended` on as at every aspect wider than 4:3, the strip joins as `rd_mesh.c` joinStrips and
  the card test): triangles dropped for a corner behind the eye (step 6) and cards (step 5), at
  the ticks (`tick_behind`, `tick_card`) and at the pictures between (t = 1/16 .. 15/16 through the
  blended camera, `between_behind` after the ticks' rescue, `between_card`), these four counting
  only the drops that stay dropped; and the drops the region rule's side bands draw beside the 4:3
  picture (`tick_beside`, `between_beside`: the rule's last step,
  `VU1REF_DRAW_BESIDE`, on both swept aspects): a drop faces the eye by `vu1ref_region_facing` of
  the corners' h (float) with the camera matrix's sign (`vu1ref_region_mirror`; the parts sit at the
  identity) and the triangle's winding against the p2o strip records' normals (index at +6, summed,
  against cross(b - a, c - a) in strip order: the EE's table); a part without normals is never
  beside; the ticks' rescue comes first and a tick's "shown" never counts a beside draw. The two
  beside counts are printed in the table and the BASELINE LINES and never held (the baseline's
  beside lines are read and ignored); `--eye` adds the rasteriser's hole pixels. The table prints at the end of each shard with its BASELINE LINES;
  `tools/sweep_baseline.txt` holds them (`STAGE ASPECT MECH COUNT`, `sweep_step N`): a count above
  its line fails, below prints "baseline can be lowered", a stage with no line is held to 0 once
  the file has any. `--out DIR` (the folder exists) writes the report to `DIR/sweep-KofN.txt`,
  `--only STAGE` runs one stage. The winding per stage is `rd_mesh`'s `winding_disc` cases
  (`winding stage NAME` lines), not repeated here. The part's strip joins use the p2o's
  strip records for the VU stream's strips and the models are placed at the identity; both are
  approximations the table's reader should know.
- `modelpack_test.c`: model packs end to end on synthetic models under the build tree
  (`modelpack`).
- `rd_alpha_plan_test.c`: the alpha/blend planner on the CPU ([RENDER.md](PIPELINES.md),
  the alpha plan): every ATST x AREF x As range under AFAIL KEEP and the FB_ONLY split against
  `gs_alpha_pass` over every As of the range, the other AFAIL shapes pixel by pixel, the blend
  identities over every Cs and Cd (`gs_blend_reg_ch`, `gs_dst_channel`), a model of the
  fixed-function path over every blend mode, PABE, FBA, COLCLAMP, FIX and range (the planned
  passes store the pixel the plan before stores), the ranges of screen prims, meshes, grids and
  particles (a mesh's span kept for its replay when its record has no stream serial, across
  replays while its serial holds, read again under a new one), and `rdtex_alpha_range` against
  every texel and two- and three-texel blends (`rd_alpha_plan`).
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
  read (`fx_local_ps`) where the device has local read, post mode 7's DATE subtract into SCENE among
  the local reads (`fxLocalDate` 1 or more, none without the local read), its self-test passing
  (`checkFxPlan`); the
  `fix128_skip` shard holds the plan off (it counts the skip's snapshots); the
  `motion_blur_smooth` shard (`rd_blur_smooth_test.inc`) runs Motion blur: Smooth against
  Original: a still picture within 1 at 1, 4 and 10 presents a tick, a moving bar's four presents
  a tick one DISPLAY under Original and four under Smooth with the ghost after the tick within 2,
  and the restart after a tick without the motion blur holding no ghost
  (`checkMotionBlurSmooth`); the `motion_blur_strength` shard (`rd_blur_strength_test.inc`)
  runs Motion blur strength over that bar under Original and Smooth: at 100 % the presents the
  bytes of the zeroed setting's, at 50 % of FIX 32 the bytes of the sprite recorded at FIX 16
  (every sprite counted, the last FIX 32 drawn as 16), Original's ghost after the tick within 1
  of the GS formula's at FIX 16 and 32 and Smooth's within 2 of Original's
  (`checkMotionBlurStrength`; `checkStrengthFix`, with the recording checks, the rounding's
  cases); `motion_blur_smooth_scaled` the accumulator and its target at
  SCENE's size at Enhanced 2x with the full-height scene, freed by `rd_shutdown`
  (`checkMotionBlurSmoothScaled`); the `aura_box` shard (`rd_blur_aura_box_test.inc`) draws the
  `above1x` shard's aura frames (the three feedback modes over hard edges, gradients and, every
  other frame, noise) at Enhanced 2x, at the phone's 2364 x 1080 scene (4.617 x 2.411 here, the
  box's edges off the texel grid by a fraction that moves from column to column) and at 1.5x, each
  from a fresh renderer with the aura's two-pass read forced off and on (`rd__aura_box_force`):
  every frame's FEED128 the same bytes, SCENE's and DISPLAY's hashes the same, reads in two passes
  counted with it on and none in one, none with it off, every pipeline created enumerated
  (`checkAuraBox`; [WORK_BUFFERS.md](WORK_BUFFERS.md#the-auras-feedback-read)); the `glow_edge_wide` shard runs the flare at 512, 848 and 2048
  GS pixels wide: one frame against the CPU model (which draws the replay's strip over the
  columns the blur's first pass leaves, `rd__glow_edge_strip`), ten strips a frame with the pairs
  the blur fold draws as one pass (which draw none) and none at 4:3, WORK1's left columns the bytes of its first drawn one, then a frame with nothing cleared
  in it replayed over magenta and over green work buffers (`rd__set_work_clear`): SCENE the same
  bytes both ways, and without the strip different in the left eighth of a wide frame alone
  (`checkGlowEdgeWide`; [WORK_BUFFERS.md](WORK_BUFFERS.md#the-glows-left-edge)). The
  `shrinks_wide_device` shard's model draws the strip too. The `blur_fold_wide` shard runs the
  flare at 512, 688, 2048 and 2560 GS pixels wide with the blur fold on: 9, 9, 8 and 8 pairs
  folded, every target against the CPU model (the strip in it), the same frame replayed with the
  fold off against the model again (the folded and unfolded chains the same bytes at every
  width), and with the strip off every target still the model's (`checkBlurFoldWide`;
  [WORK_BUFFERS.md](WORK_BUFFERS.md#the-blur-fold-on-a-wide-frame)). The `glow_fold` shard
  (`rd_blur_glow_fold_test.inc`) runs the seven post modes without the shine into WORK2 at
  Original, Enhanced 2x, the phone's 2364 x 1080 with the full height, and the phone's 848-pixel
  frame (NTSC, aspect 2.189) at 2364 x 1080 with Full pixel (WORK2's crop printed), each frame
  drawn with the glow's mask folded into its reduction (`rd__glow_fold_force(1)`) and the same
  frame replayed with it off: every target but WORK2 the same bytes both ways, at Original and 2x
  every target the CPU model's both ways (WORK2 too with the fold off), the fold counted in every
  frame that draws the glow and none kept, (e)'s frame with the shine kept for the lamps' reason,
  every pipeline created enumerated (`checkGlowFold`; [RENDER_POST.md](RENDER_POST.md#the-glows-mask-in-its-reduction));
  the recording part plans the chain without a device (`rd__glow_fold_plan` on post mode 7: the
  fill, the mask and the reduction in order, the fill's colour, the mask's test; the shine frame
  kept, `checkGlowFoldPlan`). In every shard a replay that folded the mask leaves WORK2 out of
  `compareAll` (the fold does not draw it; WORK0, its reduction, is compared).
- `rd_fx_frame_test.c`: the frame-size sprites' plan on the CPU against `gs_math.hlsli`: the
  constant fold's channels over every blend mode, FIX, COLCLAMP, PABE and a set of RGBAQ against
  `gs_dst_channel` for every Cd and Ad, the premultiplied term through the fixed-function add and
  reverse subtract against `gs_blend_reg_ch` for every Cs, C and Cd of the four forms, and the keys
  `rd__fx_enumerate` adds (none with the plan off, the constant fold's seven without the frame and
  local modules, one of them the depth attached under Z ALWAYS without Z write), with no draw made
  without a device (`rd_fx_frame`).
- `rd_fades_fold_test.c`: the fades fold ([RENDER_POST.md](RENDER_POST.md#the-fades-fold)) on the CPU,
  without a device: `gs_fold_ch` (`gs_math.hlsli`) with no rounding term against `gs_blend_reg_ch`
  of ALPHA 0x44 and 0x64 for every Cs, Cd and factor 0 to 0x80 (a texel stepped at scale 1 is the
  texel the sprite stored under the exact blend), with the term 64 against the nearest level;
  `gs_fold_applies`'s rows (the count, a bar step on the bars' rows only, no bar); above 1x, a
  bilinear of four taps modelled as two lerps of 8-bit weights (rounded, then truncated): a step
  of factor 0 or 0x80 the same bytes before or after it (the count of channels two levels apart
  for a step between levels printed: the model's worst case; the plan takes one such step a row
  above 1x, and refuses a fade and bars both between levels on the bars' rows there);
  `rd__fades_fold_exact`'s
  table (the attachment read's usage bit under attachment, the storage read's under storage); the
  cases of `rd_gsbase` (the fade (200, 100, 7) at 50, the brightness at 12, the letterbox at 25 over
  rows 0 to 57 and from 455 on, and their chains) recorded through `rd_post`, planned, and every
  texel of rd_gsbase's gradient stepped as `fx_fold4` steps a tap against the sprites drawn in
  turn: byte for byte under the exact blend (the plan's rounding from `rd__fades_fold_exact` of
  the attachment mode), the nearest level under the fixed-function one, the chains of two steps
  between levels drawn there; and `rd__fades_fold_plan` on frames recorded as the game's
  post passes record them: the fade alone (its step, alpha, the nearest level under the
  fixed-function blend, the floor under an exact one, its command's ordinal), the fade and the
  letterbox (the bars one step, SCENE's rows 0 to 57 and from H - 57 on, under an exact blend;
  drawn under the fixed-function blend unless the bars are black; at scale 2 black bars and a
  black fade fold with the rows the replay's corner snap gives, 116 and 2H - 114, a fade between
  levels does not; at full height 1x black bars fold unless `rd__set_a1x_gs_keep` is off), the
  brightness, the fade and the letterbox (three steps in order, under an exact blend; the
  brightness and the fade drawn under the fixed-function one), and the frames it leaves alone: the keep
  sprite or a menu after the fade, text quads drawn into SCENE after it (the deferred text's
  quads leave it), a Z-testing sprite after the letterbox (one before it leaves the letterbox
  folding), the effects depth or a depth read by a later frame (the bars drawn, a fade after them
  folds), a draw into SCENE in list 12, MSAA, a factor above 0x80 (`rd_fades_fold`).
- `rd_vis_test.c`: the scene draws' visibility test ([RENDER.md](RENDER.md#culling)) on the CPU,
  without a device (also the reason counts `rd__vis_why` of the frame case, summing to the draws
  walked, and a two-batch mesh drawn by batch ranges: the draw of the batch left of the frame
  skipped by its own box and counted `SKIP_RANGE`, the draws of the other batch and of both kept,
  the moved stream measured again): `rd__vis_box` over synthetic prelit, lit (with a sheet's tiles past its own
  count) and skinned streams (x, y, z and the pos.w range; a position not finite refused),
  `rd__vis_grid_box` over a Mesh3D buffer (its header and MSCNT qwords left out, a buffer too short
  refused), `rd__vis_skin`'s boxes by bone slot and largest `|w0 + w1 - 1|` (a weight outside
  [0, 1] refused); an orthographic block: a box 2.5 GS pixels past each side of a 512 x 448 target
  skipped beyond it, 1.5 kept, code 36's pos.w range, a box crossing w = 0 kept, one behind the
  eye skipped under code 32 and kept under code 36; random perspective cameras and boxes at four
  frame sizes in both rounding modes: every verdict seen, and every corner and 64 points inside a
  skipped box through the program's float product beyond both planes of its side; two bones (the
  blend inside the frame kept though one bone's box is outside it, outside skipped, weights summing
  to 0.6 kept near the frame) and random bones and weights through `vu_skin.hlsl`'s product; the
  VU references as the oracle: where the test skips a strip inside its box, `vu1ref_normal_c`
  (code 32, and code 36 with `mem[16..19]` the viewport times `mem[20..23]`) puts every output
  vertex in front of the eye, and every fan vertex, beyond the side (behind the eye for the near
  verdict), in both rounding modes; and `rd__vis_frame` on a frame built with
  `rd__init_record_only`: the skipped commands RDC_NOPs of a shadow copy with every other byte and
  the lists without a skip shared, code 34, particles, no target and list 11 kept,
  `rd__vis_culled` and `rd__vis_source`, the counts in the performance record, a stream written
  again bounded again, and the frame itself returned with the mode off, a pass window, number 0 or
  nothing skipped; the occlusion test (`rd_vis_hiz.c`): random cameras' vertices through the float
  product in both rounding modes, every one `rd__vis_hiz_vertex` takes inside the region test (code
  36: without a clip flag) with its ftoi4 position and its unsnapped one within `r` of the centre
  and its GS Z above the bound; random boxes, every corner and 64 points inside within
  `rd__vis_hiz_occludee`'s box and under its Z bound; 300 random triangles rasterised, the two ends
  of each row's covered run (one run a row) inside every placement of the corners within `r` (the
  64 extremes and 16 inside) by 0.25 GS pixel, the square grown by a pixel each way and half a
  pixel more up and down; the inner rule at a cell's corner (a right angle on it: the cells on its
  legs not covered, the next in covered; a leg 0.3 pixel out of a cell's grown edge covers it, 0.2
  does not); through the
  orthographic block a wall then a box behind it hidden, one GS Z unit in front, coplanar, inside
  the margin, inside the later mark's room, reaching past the wall's edge (a part-covered tile) and
  beside it kept, nothing hidden by an empty or reset record; near 2^31 the margin 20480 and a box
  hidden exactly when its bound and the margin stay under the wall's; and on a recorded frame under
  full: the box behind an opaque wall an RDC_NOP, one in front kept, kept behind a wall under an
  alpha test its As may fail (GREATER 0, alpha 0), blending, Z write off or a chain plane, under
  its own Z test ALWAYS, after a clear or a draw that may lower the depth (particles under ALWAYS
  with Z write), a wall under ALWAYS hiding, a wall under ATE GREATER 0x40 with AFAIL FB_ONLY whose
  As is 127 at every vertex hiding (the alpha plan's range), the walls themselves drawn, list 5's
  box kept; the counts (3 occluded, 4 tested, 5 resets); frustum alone hiding nothing; `ICO_RD_CULL_HIZ_TRIS` 0 (none) and 1 (the first wall's
  first triangle alone); `ICO_VK_DEPTH_ALWAYS` hiding nothing (`rd_vis`).
- `rd_boot_present_test.c`: a saved start-up through the presenter, present by present; needs
  `ICO_BOOT_DUMP_DIR` (`rd_boot_present`).
- `rd_blend_selftest_test.c` with `rd_blend_selftest.cmake`: the exact blend's start-up self-test
  and the attachment read's Qualcomm rule; the mode left in force, the forest's tint under it, the
  targets without the attachment read where it is off, the self-test again at a new scene size;
  its log lines matched plain, sabotaged, under the fake Adreno, with each `ICO_RD_BLEND` and
  without a destination read (`rd_blend_selftest*`); its 22 samples include the after cell, an
  opaque cell drawn after the blended draws and read at an even and an odd texel column (`after
  cell, even column`, `after cell, odd column` in a failure line), which a driver that keeps a
  dual-source equation with blending off fails, and the overlap of two sprites of one LERP command
  (`one draw, second over first`: the read's rasterization order inside a draw). Under GS state as
  shader data (`ICO_RD_UBER=on`): the attachment read taken for it and kept
  (`rd_blend_selftest_uber`, the profile line's and the start line's words), kept when sabotaged
  (`rd_blend_selftest_uber_sabotage`: `[dev] uber = on` keeps it as `[dev] blend` does), and
  `ICO_RD_BLEND=fixed` turning it off (`rd_blend_selftest_uber_blend_fixed`). The fixed-function blend is the default
  everywhere, so the exact paths are covered by opt-in runs: `rd_blend_selftest_blend_attachment`,
  `_blend_exact` and `_blend_storage` name the mode, and `rd_pixel_attachment`,
  `rd_fog_attachment`, `rd_present_attachment`, `rd_crt_attachment` and `rd_perf_attachment` run
  `rd_pixel_test`, `rd_fog_test`, `rd_present_test`, `rd_crt_test` and `rd_perf_test` under
  `ICO_RD_BLEND=attachment` with the same expectations and hashes the exact blend had as the
  default (`port/render/CMakeLists.txt`). `rd_pixel_uber` runs `rd_pixel_test` under
  `ICO_RD_UBER=on` (GS state as shader data, [PIPELINES.md](PIPELINES.md#gs-state-as-data)): the
  attachment read's expectations, its exact-mode matrix leaving out and counting the cells the
  read draws its own way (the premultiplied subtracts' floor, DATE per primitive) and holding the
  subtracts within the blender's rounding of the fixed-function draw (each RGB channel the same or
  a level under, two where the two quads overlap; the alpha and the depth the same),
  `testUberSubtractOracle` (`port/render/test/rd_pixel_uber_test.inc`) comparing every pixel of
  the premultiplied subtracts (ALPHA modes 1 and 6: FIX 0x10, 0x55, 0x80 and 0xC0, or As; FBA off
  and on; a noise texture over another, both drawn 1:1 so every Cd, Cs and As is a texel the CPU
  holds) against a CPU oracle of the GS's blend, per channel `clamp(Cd + (((0 - Cs) F) >> 7), 0,
  255)` with the GS's arithmetic shift (`gs_blend_reg_ch`'s arithmetic, checked against the
  formula), COLCLAMP on, PABE off, the alpha As with FBA's MSB: through the colour read 0 pixels
  differ; on the blender (`rd_pixel`, `rd_pixel_attachment`, `rd_pixel_storage`, `rd_pixel_nodual`,
  where the subtracts stay there) every pixel is `clamp(Cd - ((Cs F) >> 7), 0, 255)`, one level
  above the oracle exactly where `Cs F` is not a multiple of 128 and never below (the first
  differing pixel is printed with its cell, Cd, Cs and F), `testDateStencil` checking the
  per-primitive DATE first, `testScreenRuns`' boundary case
  expecting its two DATE sprites in one draw (no snapshot to retake) and `testPipelines` looking
  each run-time key up as the read's normalised image. The pass-dump compare of the corpus
  under `ICO_RD_UBER=on` is made against baselines made with `ICO_RD_BLEND=attachment
  ICO_RD_UBER=off`: with `ICO_RD_UBER_PREMUL=0 ICO_RD_UBER_DATE=0` every target byte for byte at
  the whole scales, as shipped only the premultiplied subtracts and the overlapping DATE draws
  apart (a script of the release's evidence runs it; no ctest, the corpus is machine-local).
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
  `OpenGL):`) and is the desktop column on OpenGL and Direct3D 11. With the pre-pass on, its draws
  are batched (`g_rd.vuPreBatch`) unless `ICO_RD_VU_PREPASS_BATCH=0`, which
  `rd_vuprepass_selftest_unbatched` sets (the self-test one draw an entry and stage). The draw
  merge is on (`g_rd.vuMerge`, the summary's `VU merge on`) unless `ICO_RD_VU_MERGE=0`, which
  `rd_vuprepass_selftest_unmerged` sets (both sides of the self-test's comparison one GPU draw a VU
  draw), or on Direct3D 11.
- `rd_vustage_ab.cmake`: every dump of `tools/vustage_dumps.txt` (the dump corpus, machine-local
  paths, `*` globs, a missing dump named and skipped, none at all `SKIP`) replayed by
  `rd_replay_tool --pass-dump --stats` under `ICO_RD_BLEND=fixed` the
  old way (`ICO_RD_VU_PREPASS=0`: the programs' entries) and through the record
  (the pre-passes, the main shaders, `vu_ps`), at Original 4:3 1x and Enhanced 4:3 2x: every named
  target's PNG byte for byte, no VU draw the old way with the record, no Direct3D 11
  debug-layer error and no refused record copy (`rhi_cmd_copy_texture_to_buffer: ... skipped`) in
  either run, the stats lines printed; a dump with no VU command (the four 2D dumps of the corpus) records no pre-pass and passes with 0
  passes; the records' camera is the one each command's draw uses (the walk follows the field's half
  pixel as `replayFrame` does). SKIP without the list, its dumps or a device (`rd_vustage_ab`).
- `rd_replay_tool --stats` also prints two counting lines (no draw or record changes): `VU merge:
  N draws in M runs (mean R a run); blocked by ...; per list: L:N/M ...` and `VU pre-pass batches:
  B distinct (stage, frame, stream, bones) of D draws`. A run is consecutive VU draws of one pass
  that could be one GPU draw: equal pipeline, texture and stream groups, DrawCB bytes (FNV-1a),
  scissor, FrameCB and target, each a single planned pass with no DATE stencil, no storage or
  destination-read pass and no skin, with no other draw between them; a draw that cannot join (or
  cannot merge at all) starts a run of its own, and the first key that differed is added to the
  `blocked by` counts. N counts every VU draw, so the mean is draws per run. The batches are the
  distinct (stage key, FrameCB, stream buffer, bones zero or not) of the pre-pass's draws; 0 with
  the pre-pass off. What the draw merge drew is the line after them: `VU merge: N draws in M GPU
  draws (K merged into G, A alone; merge on)`, N the draws through the merge builds, K of them in
  runs of two or more drawn as G GPU draws, A drawn one a GPU draw (`merge off` and zeros with
  `ICO_RD_VU_MERGE=0`; [VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-draw-merge)). Then code 36's pairs
  (one a batch): `VU fuse: N pairs fused, M kept apart (blend form K, plans differ L, passes P,
  dst/DATE D, no planes Q, no merge build B, fuse off F), S under one pipeline already; fuse on`, N
  the batches whose kick took its cut's pipeline, M those kept apart by the first reason that held,
  S those whose two draws had one plan without the fuse (`fuse off` and every pair with ABE off
  under `fuse off F` with `ICO_RD_VU_FUSE=0` or the merge off;
  [VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-fused-scissor-pair)). And the store plan's: `stores: N
  depth and M colour stores dropped a replay of P passes (D with a depth; store plan on)`, N the
  passes whose depth (and stencil) was neither loaded nor stored, M the passes whose colour was
  (the shadow count's while it is made, about one a replay; every other colour is kept),
  P the passes the walk began and D those with a depth outside the MSAA epoch (`store plan off`
  and N 0 with `ICO_RD_STORE_PLAN=0`, `all` and N = D with `ICO_RD_STORE_PLAN=all`;
  [WORK_BUFFERS.md](WORK_BUFFERS.md#the-store-plan)).
  After the shadow count's line, `constant
  reads on: N sprites drawn in a cleared source's colour, M target reads kept (the source drawn since
  its clear, or not cleared this replay)`: N the post sprites drawn untextured in the colour of a
  source target that held only its last clear, M those reading another target that were not (`off`
  and N 0 with `ICO_RD_CONSTANT_READS=0`, every read then counted in M;
  [WORK_BUFFERS.md](WORK_BUFFERS.md#a-cleared-source-read-as-its-colour)).
- `rd_vubatch_ab.cmake`: the same dumps through the record twice, one point draw an entry and
  stage (`ICO_RD_VU_PREPASS_BATCH=0`) and in the pre-pass's batches, at Original 4:3 1x, Enhanced
  4:3 2x and Enhanced 16:9 2x under `ICO_RD_BLEND=fixed`: every named target's PNG byte for byte,
  every VU command through the record in both, the same vertices, no more pre-pass draws batched
  and fewer where one an entry made two or more; each run's draws printed, and the totals
  (`rd_vubatch_ab`; [VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-pre-passs-batches)). SKIP as
  `rd_vustage_ab`.
- `rd_vumerge_ab.cmake`: the same dumps twice, every VU draw a GPU draw of its own
  (`ICO_RD_VU_MERGE=0`) and with the draw merge, through the record and through the programs' own
  entries (`ICO_RD_VU_PREPASS=0`, the tile-based GPUs' path), at Original 4:3 1x, Enhanced 4:3 2x
  and Enhanced 16:9 2x under `ICO_RD_BLEND=fixed`: every named target's PNG byte for byte, `merge
  off` with nothing merged against `merge on`, the merged run's GPU draws (the first stats line's)
  exactly the other's less what its runs joined (K less G of the `VU merge` line), and fewer over
  the whole list; each pair's draws printed (`rd_vumerge_ab`;
  [VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-draw-merge)). SKIP as `rd_vustage_ab`.
- `rd_vufuse_ab.cmake`: the same dumps and those of `tools/vufuse_dumps.txt` (the PowerVR phone's
  three frames) twice with the draw merge on, code 36's kicks under their own pipelines
  (`ICO_RD_VU_FUSE=0`) and the fused scissor pair, at five settings under `ICO_RD_BLEND=fixed`: the
  phone's profile (`ICO_RD_VU_PREPASS=0`, `--enhanced --resolution 2x --aspect 2.189 --present
  2364x1080`; a narrower dump skips it) and the same at its Resolution Auto (`--resolution 2364x1080
  --full-height --full-pixel`, a fractional scale: the no-planes twins), the programs' entries at
  Enhanced 4:3 2x, and the record at
  Enhanced 4:3 and 16:9 2x: every named target's PNG (and `PRESENT.png`) byte for byte, `fuse off`
  with no pair fused against `fuse on`, the same pairs counted by both (the `VU fuse` line's fused,
  kept apart and one-pipeline counts), the fused run's GPU draws exactly the other's less the
  difference of what the merge joined (K less G of each run's `VU merge` line), no pair kept apart
  for the pixel shader (`no planes 0`: every pixel shader a pair draws with reads the mark), at
  every setting that counted pairs with ABE off some fused and fewer GPU draws in total, and on
  every phone frame some fused at both phone settings; each pair's counts printed
  (`rd_vufuse_ab`; [VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-fused-scissor-pair)). SKIP
  as `rd_vustage_ab`.
- `rd_vumerge_ab.cmake` and `rd_vufuse_ab.cmake` take `-DBLEND_ENV=` (the blend both runs draw
  with, `ICO_RD_BLEND=fixed` by default): `rd_vumerge_ab_uber`, `rd_vufuse_ab_uber` and their
  `_pixel` twins on `rd_pixel`'s frames run them under `ICO_RD_UBER=on`, the VU draws through the
  colour read, merged and fused (`port/render/rd_replay_vu.c` (`vuMergeDst`, `vuFuseVerdict`)).
- `rd_vumerge_mali.cmake` (label `mali`): the dumps of `tools/vumerge_mali_dumps.txt` (the Mali-G52
  reporter's frame of issue 164 in `/primary/dev/ico-dumps/164/`, the two phone frames
  `rd_vufuse_ab` reads and a 2026-10-07 capture; a line is the dump, `expect=IN/GPU` and the
  tool's setting arguments) on the plain device and under `ICO_VK_FAKE_LIMITS=mali
  ICO_VK_FAKE_NO_DUAL=1` (a phone's limits, every lerp draw two passes), under
  `ICO_RD_BLEND=fixed` and `ICO_RD_CULL=off` (the `expect=` figures count every VU draw of the
  frame; the visibility test would leave some out): every named target's PNG byte for byte, both
  fakes' lines in the faked run,
  the `VU merge` figures (draws, GPU draws, merged, into, alone) and the fused pairs the plain
  device's, and `expect=` the plain run's merged figure (826 into 87 on the 164 frame)
  (`rd_vumerge_mali`; [VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-draw-merge-without-dual-source-blending)).
  SKIP as `rd_vustage_ab`.
- `rd_noplanes_ab.cmake`: the dumps of `tools/noplanes_dumps.txt` (the title's and the boot's
  frames, each drawing one screen-prim triangle key with planes, and a capture drawing none; a line
  is the dump, `expect=same|fallback:N|report` and the tool's setting arguments) with the sprite
  planes builds and under `ICO_RD_NO_SPRITE_PLANES=1` (issue 161's fallback): the second run's line
  that the builds are off, `sprite no-planes 0` in the first; `expect=same` every named target's PNG
  byte for byte and no command through the fallback, `expect=fallback:N` byte for byte with N
  commands through it (the title's and the boot's dumps: 2), `expect=report` at least one command
  through it and the targets that differ listed (`rd_noplanes_ab`;
  [WORK_BUFFERS.md](WORK_BUFFERS.md#without-the-sprite-planes)). SKIP as `rd_vustage_ab`.
- `rd_occlusion_tool.cmake`: `rd_replay_tool --occlusion` ([RENDER.md](RENDER.md#culling)) on the
  first dump of the machine-local list `tools/occlusion_corpus.txt` that is here, at Original 4:3
  1x: the line `occlusion: hidden N of M list-0 draws, T of U triangles (index count / 3); hidden
  commands: S` with M above 0, N at most M, T at most U and S `none` or a `--nop` spec of list 0,
  the second line, the settings note, `--occlusion-out` holding S and `--occlusion-csv` its header
  and a line a command; then the plain replay and the replay with `--nop @SPEC`, each `--pass-dump`
  with `--present 640x480` under `ICO_RD_BLEND=fixed`: DISPLAY.png and PRESENT.png byte for byte
  (`rd_occlusion_tool`). SKIP without the list, a dump of it or a device.
- `rd_region_stats.cmake`: `rd_replay_tool --region-stats`
  ([VU1_PROGRAMS.md](VU1_PROGRAMS.md#region-statistics)) on the first two dumps of
  `tools/occlusion_corpus.txt` that are here, each at Original 4:3 1x and Enhanced 4:3 2x with a
  present: the frame's line (draws and triangles under the rule above 0, the draws drawing nothing a
  `--nop` spec of lists 0-6 or `none`, as many items as it counts, `--region-out` holding it), a
  draw's line and a list's in their shapes; then `--pass-dump` plain and with `--nop @SPEC`, every
  PNG byte for byte (the draws the CPU oracle says draw nothing draw no pixel); at Original the same
  spec with `--no-device` (`rd_region_stats`). The frame and draw lines' patterns include the side
  bands' group (`, beside N (behind-eye b, out-of-box x, card k)` after the drop's parenthesis) and
  the draw line's closing `; beside N, dropped back M`; at the test's 4:3 settings beside is always
  0. SKIP without the list, a dump of it or a device.
- `rd_sideband_ab.cmake` (issue 29; [VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-region-rule), step 7):
  the region rule's side bands against the PS2's drop on the wide dumps of the machine-local list
  `tools/sideband_dumps.txt` (a path and the dump's own aspect a line: the PowerVR phone's 2.189
  frames, issue 179's 16:9 frame, issue 182's 16:10 frame, the phone's one 4:3 frame as the
  control, and the issue-29 48:9 and 16:9 captures when they arrive under
  `/primary/dev/ico-dumps/29/`). Every dump at its own aspect, Original 1x and Enhanced 2x:
  `rd_replay_tool --pass-dump` with `ICO_RD_VU_BESIDE=0` (every VU draw keeps the PS2's drop) and
  without, then `tools/cmp_dirs_band.py A B --gs-width W --band-files SCENE.png --fog-from-a`, W the
  replay's `frame W GS wide`: SCENE's centre 512 GS pixels (512 x scale texels) identical byte for
  byte, the side columns reported (changed pixels, largest channel difference, bounding box), and
  every side pixel B changes one A left at its fog colour (A's most common side-column colour); the
  other targets are reported, not held (the post effects read across the band's edge). At 4:3 the
  band is the frame, so the whole SCENE must be identical. A missing list or dump is named and
  skipped; none here, or no device: SKIP (77). TIMEOUT 3600; serial like `rd_region_stats`
  (`rd_sideband_ab`). The dumps from before dump version 15 carry no EE facing table, so their
  prelit meshes have no side bands: only their lit and skinned drops can change there; the fresh
  captures carry the table.
- `rd_vumerge_ab_pixel`, `rd_vufuse_ab_pixel` and `rd_vustage_ab_pixel`: the two scripts above and
  `rd_vustage_ab.cmake` on the frames `rd_pixel`
  leaves (fixture `rd_dump`; the list `rd_pixel_vu_dumps.txt` in the build folder):
  `rd_pixel_vu.rddump` (the VU seams' S4 at Original 1x: 64 code-32 draws of one prelit mesh
  under one state, a matrix each, which the merge joins), `rd_pixel_vu36.rddump` (S3's first
  case: a code-36 strip whose first triangle is cut and whose second is kicked) and
  `rd_pixel_frame.rddump` (no VU draw). Every machine has these, so unlike the two above they do
  not skip; the 16:9 and phone settings skip for the 4:3 frames by design. CI's Direct3D 11 job
  runs the same on WARP ([below](TESTING.md#the-render-suite-on-direct3d-11-warp)).
- `rd_vumerge_fallback.cmake` (ctest `rd_vumerge_fallback`, fixture `rd_dump`): `rd_replay_tool`
  on `rd_pixel_vu.rddump` with `--time 3` under `ICO_RD_BLEND=fixed`, with the merge, with
  `ICO_RD_VU_MERGE=0` and with `ICO_RD_FAKE_MERGE_FAIL=1` (the first pipeline made with a merge
  build kept as failed): the faked run logs one failure line naming the merge build and once
  `rd: VU merge: off: the driver could not compile <build> (<backend>); one GPU draw a VU draw
  from here`, and its timed replays draw what `ICO_RD_VU_MERGE=0` draws, more than the merge
  (`rd_replay_vu.c` `rd__vu_merge_fallback`).
- `rd_store_plan_test.c` (`rd_store_plan`, lavapipe, 77 without a device): the store plan's keep
  (`rd_store_plan.c`, [WORK_BUFFERS.md](WORK_BUFFERS.md#the-store-plan)) at Enhanced 16:9 2x: after
  the start's self-tests (`rd_fog_selftest`: DATE, the fx local read, the shadow count, the fog) and
  again at a new scene size (`rd_reset_scene`), SCENE's depth is not kept (the shadow count's frame
  reads it with no clear of its own and once kept it for the whole run); a new SCENE
  (`clearPending`) read in list 3 before any clear is not kept, and the next frame (list 0's clear
  and a Z-tested draw, then list 7 on WORK3 and on SCENE without touching the depth) drops at least
  one depth store; a frame reading SCENE's depth with no clear keeps it, the cleared frame then drops
  none and its record says `storeKept` 1, SCENE, since that frame and the reason
  `RD_STORE_WHY_KEEP`; no validation error. Between those, the PowerVR phone's frame 5354 in shape
  (list 0's Z-tested draws, the fog's touch in a pass of its own, the aura's passes on WORK3, list
  11's two screen sprites on SCENE without a Z test or Z write) with [video] effects_depth on and no
  effects program, as on the phone: exactly one depth store dropped (list 11's SCENE pass), as a whole
  frame and as a keep frame; with `rd__force_effects_depth` (an effects program's stand-in) none, the
  record's reason `RD_STORE_WHY_EFFECTS`, and no keep left after it; replayed with its presents,
  every record `rd_perf_pop` returns holds the drops its replay counted and `rd_perf_backend_text`
  says `stores: 1 depth` (with the effects depth `stores: 0 depth` and `nothing dropped in N
  replays, N of them: kept for an effects program`). The drops' bytes are `rd_store_plan_ab`'s
  (next).
- `rd_pass_cut_test.c` (`rd_pass_cut`, lavapipe, 77 without a device): the passes cut
  (`rd_pass_cut.c`, [WORK_BUFFERS.md](WORK_BUFFERS.md#passes-cut)) at Enhanced 16:9 2x under
  `ICO_RD_STORE_POISON=1`, each forced on against off (`rd__pass_cut_force`): a clear of WORK3 then
  a sprite on it textured from WORK2 (drawn just before): the clear taken by WORK3's pass
  (`clearsCarried` 1, one walk pass fewer), WORK3 and WORK2 byte for byte; the clear skip's
  decision on a frame's scan (AURA_WORK cleared, WORK0's sprite reading it, the constant read's note
  for it): skipped, but not with a later read, a depth or the cut off; after a skip a read of
  AURA_WORK shows the clear's colour (its green fill gone), a later replay drawing its left half
  leaves the right half the clear's colour, a later replay clearing it first leaves that clear; the
  last store: list 0 clears SCENE and draws Z-tested, list 4 draws on WORK3, then on SCENE a sprite,
  the depth's last touch and another sprite: `storeLastDropped` 1 on, 0 off, one depth store more
  dropped, SCENE byte for byte; a copy or a draw on another target between: 1 (the pass begun at
  the touch), a DATE draw between: 0 with one last store counted as kept for a DATE draw
  (`lastStoreWhy`, its words `1 last stores kept (1 a DATE draw)`), none counted for the others; a
  draw textured from WORK3 between while WORK3 is not readable: 1 (the pass begun at that draw); the
  pass's first draw and one between textured from WORK3 (readable from the first on): 1, the pass
  begun at the first draw, and the guard's frame after it drops it again; post sprites
  (`cellPost`): the glow's `cleanUpFB` pair into SCENE (constant fold; the second Z GEQUAL at Z 1,
  the depth's last touch in a pass of its own) drops that pass's store (1 on, 0 off, no reason
  counted), the same pair as a motion blur's keeps it (0, one reason: a post sprite), the aura's
  DATE sprite (TEST 0x34000) after a Z GEQUAL screen sprite leaves that sprite the last touch (1,
  no reason), SCENE byte for byte in each; the pass area forced on:
  list 0's pass (it takes SCENE's clear) and list 4's (full-frame sprites, one the frame's height)
  counted as kept whole (`passAreaWhy`: `2 passes kept whole (1 a clear as its load, 1 its draws
  cover the picture)`), none with the area off, SCENE byte for byte; no validation error.
- `rd_pass_cut_ab.cmake`: the dumps of `tools/pass_cut_dumps.txt` (`tools/pass_area_dumps.txt`'s;
  machine-local, a missing one skipped) replayed with `--pass-dump --stats` under
  `ICO_RD_BLEND=fixed`, `ICO_RD_STORE_PLAN=1`, `ICO_RD_STORE_POISON=1` and `ICO_RD_FADES_FOLD=0`
  (list 11's sprites drawn, as the gate draws them: with the fold list 11's pass goes and the
  reduction's DISPLAY clear has no barrier left to be carried past) five times: every cut off
  (`ICO_RD_CLEAR_CARRY=0`, `ICO_RD_CLEAR_SKIP=0`, `ICO_RD_LAST_STORE=0`), each alone on, all on; at
  the phone's Resolution Auto with its paths (`ICO_RD_VU_PREPASS=0`, `ICO_RD_FOG_PATH=local`,
  `ICO_RD_DATE_PATH=stencil`; a narrower dump skips it) and at Enhanced 2x 4:3: every PNG
  (`PRESENT.png` and every named target) byte for byte against the off run, the off run cutting
  nothing, the same draws, the passes fewer by the clears carried and skipped, the depth stores
  dropped more by those at the last touch and fewer by at most one a clear carried or skipped (a
  cleared pass whose depth store the plan dropped goes with its carry: queen rd-00100 at Enhanced
  2x, 2 dropped with every cut off and 1 with its two clears carried); over the phone's frames at
  its setting each cut must cut something, the last touch counted on two more runs of those frames
  with `ICO_RD_CRACK_FILL=0` (every cut off, the last store alone on; compared byte for byte with
  each other), as the crack fill's read of SCENE's depth at list 7 keeps the fog's pass's depth
  store wherever it is drawn (`rd_pass_cut_ab`). SKIP as `rd_vustage_ab`.
- `rd_store_plan_ab.cmake`: the same dumps and those of `tools/vufuse_dumps.txt` (the phone's three
  frames) three times under
  `ICO_RD_BLEND=fixed` and `ICO_RD_STORE_POISON=1` (a depth or stencil load the plan leaves
  undefined clears to 0.5 and 0x5A instead: lavapipe keeps an attachment's contents whatever its
  load and store ops say, measured on Mesa 25.0.7, so without the poison a wrong plan draws the same
  bytes there): every depth store kept (`ICO_RD_STORE_PLAN=0`), the store plan, and every depth
  store dropped (`ICO_RD_STORE_PLAN=all`, a wrong plan), at three settings: the phone's profile
  (`ICO_RD_VU_PREPASS=0`, `ICO_RD_FOG_PATH=local`, `ICO_RD_DATE_PATH=stencil`, `--enhanced
  --resolution 2x --aspect 2.189 --present 2364x1080`; a narrower dump skips it) and the record at
  Enhanced 4:3 and 16:9 2x: with the plan every named target's PNG (and `PRESENT.png`) byte for
  byte against every store kept, `store plan off` with nothing dropped, `on` and `all` with the same
  passes and passes with a depth and the same colour stores dropped (the shadow count's colour,
  poisoned to 0x5A: no named target may show it), `all` dropping every depth store, the plan dropping some over the
  whole list, and the wrong plan changing at least one target over the whole list (the poison shows
  a dropped store that a later pass reads); each run's counts printed (`rd_store_plan_ab`;
  [WORK_BUFFERS.md](WORK_BUFFERS.md#the-store-plan)). SKIP as `rd_vustage_ab`.
- `rd_cull_ab.cmake`: the dumps of `tools/occlusion_corpus.txt` and `tools/native_frame_dumps.txt`
  (the missing ones named and skipped) under `ICO_RD_BLEND=fixed` with `--pass-dump --stats`
  three times, `ICO_RD_CULL=off`, `frustum` and `full`, at Original 4:3 presented at 640x480,
  Enhanced 4:3 2x, Enhanced 2x at the dump's own aspect where `--no-device --list` says it is wider
  than 4:3, and on the first dump once with `--aa msaa4`: every named target's PNG (and
  `PRESENT.png`) of the frustum and the full runs byte for byte the off run's, the off run's `cull
  off: 0 of 0`, the frustum run's `cull frustum: N of M` with N at most M, the full run's `cull
  full: N of M (F frustum, O occluded)` with the frustum run's M, F its N and N = F + O, and draws
  skipped over the corpus (the occluded total printed); each run's counts printed;
  `-DMAX_DUMPS=n` takes the first n dumps (`rd_cull_ab`, TIMEOUT 5400;
  [RENDER.md](RENDER.md#culling)). SKIP as `rd_vustage_ab`.
- `rd_thread_ab.cmake`: the same dumps and those of `tools/vufuse_dumps.txt` through
  `rd_replay_tool --render-thread off`, `sync` and `on` (every replay, the interpolation and the
  renderer's calls on a thread of the renderer's own, `port/render/rd_thread.h`), with
  `--pass-dump` at Original (640x480) and Enhanced 4:3 2x (1280x960), and for every F12 zip the
  blended present at 0.5 between its two frames: every target's PNG of `sync` and `on` byte for
  byte `off`'s, and the threaded runs saying `render thread: sync` / `on`; the blended present is
  made three times in a row in every mode (`--repeat 3`), the `on` run handing the three over
  back to back as the window does, and must say `submit thread: ended (N` with N at least 3
  (their submit and present on the submit thread, `port/render/rd_submit_thread.h`) and, where
  the device says `rhi_vk: frame slots: apart`, `submit thread: the frame slots apart` (the second
  and third recorded while the one before is submitted, RHI.md "Frame slots and the submit
  thread"); the last picture's PNG byte for byte `off`'s
  (`rd_thread_ab`; [PLATFORM.md](PLATFORM.md#the-render-thread)). SKIP as `rd_vustage_ab`.
- `rd_constant_reads_ab.cmake`: the same dumps and those of `tools/constant_reads_dumps.txt` (the
  PowerVR phone's two frames, the aura with no aura object) twice, every post sprite reading
  its source (`ICO_RD_CONSTANT_READS=0`) and a source holding only its clear drawn as its colour, at
  four settings under `ICO_RD_BLEND=fixed`: the phone's profile (`ICO_RD_VU_PREPASS=0`) at 2x
  (`--enhanced --resolution 2x --aspect 2.189 --present 2364x1080`) and at its Resolution Auto size
  (`--resolution 2364x1080 --full-height --full-pixel`, the same aspect and present; a narrower dump
  skips both), and the record at Enhanced 4:3 and 16:9 2x: every named target's PNG (and
  `PRESENT.png`) byte for byte, `constant reads off` with none served against `on`, the same target
  reads counted by both (served plus kept against the other's kept), and every phone frame served at
  both phone settings; each pair's counts printed (`rd_constant_reads_ab`;
  [WORK_BUFFERS.md](WORK_BUFFERS.md#a-cleared-source-read-as-its-colour)). SKIP as `rd_vustage_ab`.
- `rd_fades_fold_ab.cmake`: the dumps of `tools/fades_fold_dumps.txt` (the forest's
  `rd-00720` and `rd-01076`, the queen's `rd-00100`, the PowerVR phone's F12 zip `frame-20261028-173610-v4657`; machine-local) and,
  with `ICO_BOOT_DUMP_DIR`, the start-up's frames 115 to 123, each first listed (`--list
  --no-device`: list 11's last twelve commands in the log, so it shows which dumps end with the
  fade, the letterbox or the brightness), then replayed with `--pass-dump --stats` with list 11's
  last full-frame lerp sprites drawn into SCENE (`ICO_RD_FADES_FOLD=0`, the tests' way to draw
  them as before) and with the fold (the default; its log line must name the GS's floor under the exact blend and the
  nearest level under the fixed-function one, and a reduction that did not take a planned fold
  fails), under
  `ICO_RD_BLEND=attachment` and `fixed`, at Original (`--present 640x480`), Enhanced 2x
  (`--present 1280x960`) and the phone's Resolution Auto (`--resolution 2364x1080 --full-height
  --full-pixel --aspect 2.189`, `ICO_RD_VU_PREPASS=0`; a narrower dump skips it): `fades fold off`
  with none folded against `on`; every target's PNG but SCENE's (which holds the picture without
  the sprites by design) byte for byte where nothing folded and under the exact blend where the log
  says the steps met SCENE's texels before the GS filter, elsewhere within two levels on every
  channel (the measured bound, [DIVERGENCES.md](DIVERGENCES.md) VD-27), the channels a level apart
  printed per target by `port/render/test/png_level_diff.py`;
  some dump must fold (`rd_fades_fold_ab`; [RENDER_POST.md](RENDER_POST.md#the-fades-fold),
  [DIVERGENCES.md](DIVERGENCES.md) VD-27). SKIP as `rd_vustage_ab`.
- `rd_crack_late_ab.cmake`: the crack fill after lists 4-6 against pass dumps of the build that drew
  it before the fog: each line of `tools/crack_late_dumps.txt` (`<setting> <dump> <baseline
  folder>`, machine-local: six corpus dumps at the gate's Enhanced 2x 4:3 against
  `build-host/tmp/v095/after/w3/pd`, and five of the PowerVR phone's frames at its Resolution Auto,
  2364x1080 at 2.189 under its profile's fakes, against `build-host/tmp/v098/evidence/cheap/base-phone`)
  replayed with `--pass-dump --stats` under `ICO_RD_BLEND=fixed ICO_RD_FADES_FOLD=0` (the
  baselines' own): the stats line says `crack fill on: P passes` (P 0 where the fill was the
  first draw of the aura's paste pass, else 1) and `1 fills after lists 4-6`,
  or, on a dump with no fog of SCENE above 1x (`0 passes, 0 pixels filled (0 replays counted;
  scale 0x0, ...; 0 fogged replays above 1x not filled; 0 fills after lists 4-6`: the v3086 and
  v1985 frames), every target the baseline's bytes; every target within 64 levels on at most 0.2 %
  of its pixels (the bound, the pixels apart and the largest difference printed per target;
  [DIVERGENCES.md](DIVERGENCES.md) VD-20), or, past it, the run replayed once more with
  `ICO_RD_CRACK_FILL=0` and the glow's mask (WORK2, `cleanUpFB`'s depth test: the late fill leaves
  the crack's depth) that run's byte for byte, and each target past the bound no farther from the
  baseline than the no-fill run's (no more pixels apart, no larger difference); at the phone's
  setting the stats line's copies one fewer than the baseline log's and its passes no more. Every
  run that drew the fill is replayed again with `ICO_RD_CRACK_PLACE=7` (the fill at the start of
  list 7, the place before `rd__crack_fill_place` moved it after the glow's `cleanUpFB` or
  into the aura's paste pass): every PNG that replay's bytes, the run's passes no more than its,
  and at the phone's setting a run whose stats place the fill later (`1 fills in the aura's paste
  pass` or `1 fills after the glow's alpha`) at least one pass fewer. A
  missing dump or baseline is skipped; none: SKIP. The baselines are pictures of the fill before the
  fog; another change to the picture needs baselines of its own before this test can tell the two
  apart.
- `rd_aura_box_ab.cmake`: the dumps of `tools/aura_box_dumps.txt` (`tools/vustage_dumps.txt`'s and
  the PowerVR phone's three F12 zips `frame-20261028-{105852-v4103,151709-v2199,173610-v4657}`;
  machine-local) replayed with `--pass-dump --stats` under `ICO_RD_BLEND=fixed` with the aura's
  read of the picture into FEED128 in one pass (`ICO_RD_AURA_BOX=0`) and in two
  (`ICO_RD_AURA_BOX=1`, `rd_aura_box.c`), at the phone's Resolution Auto (`--enhanced --resolution
  2364x1080 --full-height --full-pixel --aspect 2.189 --present 2364x1080`,
  `ICO_RD_VU_PREPASS=0`; a narrower dump skips it) and at Enhanced 2x 4:3 (`--present 1280x960`):
  every PNG (FEED128, SCENE, `PRESENT.png` and the other named targets; ROWBOX is the renderer's
  own texture, never written) byte for byte, `aura box off: 0` against `on`, the two-pass run's
  passes and draws the other's plus one each a read in two passes; some dump must read in two
  passes (`rd_aura_box_ab`). SKIP as `rd_vustage_ab`.
- `rd_shadow_merge_ab.cmake`: the dumps of `tools/shadow_merge_dumps.txt` (`tools/vustage_dumps.txt`'s,
  the PowerVR phone's three F12 zips and its F12 frame of the heavy stage 6 room `frame-20261102-222516-v2325.zip` in
  the effort's evidence folder; machine-local, a missing one skipped) replayed with `--pass-dump
  --stats` under `ICO_RD_BLEND=fixed` and `ICO_RD_STORE_POISON=1` with the shadow volumes in a pass
  of their own (`ICO_RD_SHADOW_MERGE=0`) and in the scene's open pass (`=1`, `rd_shadow_merge.c`),
  at the phone's Resolution Auto under the MoltenVK fakes (a tile-based class) and at Enhanced 2x
  4:3: every PNG byte for byte (the count targets' clears the on run leaves undrawn are drawn with
  the poison's 0x5A there, so a reader would show), `shadow volumes in the scene's pass off: 0 of R
  counts, 0 passes armed; 0 count targets' clears not drawn` against `on: M of R`, the same draws,
  the on run's passes the off run's less M; some reset must be taken (`rd_shadow_merge_ab`). SKIP
  as `rd_vustage_ab`.
- `rd_pass_area_ab.cmake`: the dumps of `tools/pass_area_dumps.txt` (`tools/vustage_dumps.txt`'s,
  the PowerVR phone's three F12 zips and its F12 frame of 2026-11-02
  `frame-20261102-091017-v15677.rddump` in the effort's evidence folder; machine-local, a
  missing one skipped) replayed with `--pass-dump --stats` under `ICO_RD_BLEND=fixed` with the
  passes' render areas off (`ICO_RD_PASS_AREA=0`) and on (`=1`, `rd_pass_area.c`, on whatever the
  device: lavapipe clips every draw to the area, so an area that misses a texel shows), at the
  phone's Resolution Auto (as `rd_aura_box_ab`) and at Enhanced 2x 4:3: every PNG byte for byte,
  `pass area off: 0 passes` against `on`, the same draws, the on run's passes the off run's plus
  at most its passes ended early; the phone's frames (`frame-20261028-*`, `frame-20261102-*`, the
  phone's F12 frame of 2026-11-02 11:42 and its blended twin among them) at the phone's setting
  must give some pass an area or name `its draws cover the picture` among the passes kept whole
  (the tool's `--stats` clause; the aura's paste and list 11's fades are full-frame sprites, so the
  stage 6 frames have no area to give) (`rd_pass_area_ab`). SKIP as `rd_vustage_ab`.
- `rd_glow_cheap_ab.cmake`: the glow's cheaper passes ([RENDER.md](RENDER.md#the-glows-passes):
  WORK2's fill in the mask's pass, cleanUpFB in the fog's) against stored pass dumps, as no switch
  turns them off: the 14 glow dumps of `tools/glow_cheap_dumps.txt` (machine-local, a missing one
  skipped) replayed with `--pass-dump --stats` under `ICO_RD_BLEND=fixed ICO_RD_FADES_FOLD=0` at
  the PowerVR phone's profile on lavapipe (`ICO_VK_FAKE_LIMITS=moltenvk ICO_RD_VU_PREPASS=0`,
  2364x1080, Full height, Full pixel, the dump's own aspect, presented at 2364x1080; baselines
  `build-host/tmp/v098/evidence/glow/pd-phone`, made by the tool before the cuts) and at the gate's
  Original and Enhanced 2x 4:3 (baselines `build-host/tmp/v095/after/w3/pd`, named as
  `gate-wave.sh` names them): every baseline PNG the same bytes or within `-DBOUND` levels (0,
  `png_level_diff.py`), the `glow N passes` count at least two fewer than the baseline's at the
  phone's setting and no more at the others; each run's line prints the passes before and after
  and its largest difference. The run's own count meets the rule with the crack fill drawn (it
  is drawn after `cleanUpFB`'s two sprites, which stay in the fog's pass: [the fill's
  place](RENDER_POST.md#the-fills-place-after-list-7)), and at the phone's setting a run whose
  stats say the fill was drawn says `1 fills after the glow's alpha`. The seven frames the
  crack fill changed (v4251, the loss frame, rd-00720, the two issue-91 frames, v2567 and its
  previous frame) are compared at the phone's setting and Enhanced 2x with refreshed pictures
  (`-DBASE_REFRESH`, `build-host/tmp/v098/evidence/glow/pd-w15`: main 7832a2e4f's, each first
  checked to have the no-fill replay's WORK2 and no target farther from the old baseline than
  that replay), the old baseline's log still giving the passes before. A setting without a
  baseline is skipped; nothing compared: SKIP (`rd_glow_cheap_ab`). A baseline refresh for a
  later change to the glow's drawing replaces the folders. Those runs hold the glow's mask fold
  off (`ICO_RD_GLOW_FOLD=0`), and each dump at each setting runs again with it on
  (`ICO_RD_GLOW_FOLD=1`, into `<name>-<setting>-fold`): every PNG but `WORK2.png` (which the fold
  leaves undrawn) the same bytes as the run without it, `PRESENT.png` too at the phone's setting;
  a run whose stats say `glow: WORK2 fold on: 1 folded` has at least one glow pass fewer, a kept
  one (the sun's fans or the lamps drawn into WORK2) as many; at each setting that ran, at least
  one run folded ([RENDER_POST.md](RENDER_POST.md#the-glows-mask-in-its-reduction)).
- `rd_dst_usage_test.c`: the blend policy's footprint: by default only the fixed-function blend
  exists (`g_rd.dstModes` OFF, no destination-read layout, shader twin or target usage, no
  readable pass, both exact modes refused); with `ICO_RD_BLEND=attachment` only that mode's
  layout, twins and `SCENE` usage, and a start-up set that grows when the mode is switched on
  (`rd_dst_usage`; 77 without a Vulkan device).
- `rd_crack_test.c` with `rd_crack.cmake`: the crack filler above 1x
  ([RENDER_POST.md](RENDER_POST.md#crack-filling-above-1x)). A 640 x 448 frame under Enhanced, two flat
  triangles sharing a long diagonal edge, the second one's edge one 12.4 step (1/16 GS pixel) to
  the right, two sprites 1.5 GS pixels apart, below them a concave fold (two strips whose depth
  falls steeply towards their shared line) and the fog, replayed with the filler off and then
  as the environment has it, SCENE read back: at 2x, 4x and 1800 x 1080 texels (2.8125 x 2.41;
  `rd_crack`, `rd_crack_4x`, `rd_crack_frac`) the edge shows texels of the clear's fog without
  the filler and none with it, each filled texel the colour of a drawn texel within the reach on
  its row or column after the fog (the fill drawn after lists 4-6) and its depth unchanged, the slit keeps every texel of the clear's fog, nothing
  the first replay drew changes (the fold among it), one fill pass is drawn and its count equals
  the texels changed; with `backdrop` (`rd_crack_backdrop`, `rd_crack_backdrop_4x`,
  `rd_crack_backdrop_frac`) a far plane writes Z over the frame first and the two triangles lie
  near each other in depth, so the cracks hold the plane's depth and colour and are filled the
  same (the discontinuity form) while the slit keeps the plane;
  Where the reach is 1 on both axes (`rd_crack`, `rd_crack_backdrop`, the 2x cases and their `_gl` twins) the
  frame is also replayed with the general shader (`rd__crack_fill_force_general`): every byte of colour and
  depth and the count are the reach-1 shader's (`crack_fill1_ps`), and the `rd: crack fill: the reach-1 shader`
  line is matched (`EXPECT2`).
  at 1x, under Original, under MSAA4 and with `ICO_RD_CRACK_FILL=0` (`rd_crack_1x`,
  `rd_crack_original`, `rd_crack_msaa`, `rd_crack_off`) no fill pass and the same bytes (under
  MSAA4 the frame has no shadow, so the epoch is still open at the fog and the skip line says so); under
  MSAA4 the frame is replayed again without anti-aliasing and the filler: along the edge the
  MSAA4 picture holds at most 5 % of its texels with the clear's depth (colours without a depth
  readback) and the slit keeps the clear's fog on every texel; the same
  2x run on the depth copy, the buffer path, D24S8 (no depth readback: colours only) and the
  two-pass blend (`rd_crack_depth_copy`, `rd_crack_buffer`, `rd_crack_d24s8`, `rd_crack_nodual`).
  Under the MoltenVK report (`ICO_VK_FAKE_LIMITS=moltenvk`, a tile-based profile) `rd_crack_tiler`
  and `rd_crack_tiler_on` (`ICO_RD_CRACK_FILL=1`) find the filler on by default, as on every
  class (the old `off on this tile-based GPU` line denied). `rd_crack_tiler_fold` (`rd_crack_test
  2 fold`, 77 without local read; the name is the old fold's) checks the fog's local read: one fill,
  drawn after lists 4-6 (`RdCrackCounts.late`), the fog by the local read in the same replay
  (`fogLocalDraws`), one render pass more than without the filler besides the count's (the fill's
  own: nothing breaks the scene pass at the fog), and SCENE's colour and depth byte-identical to a
  replay on the depth copy path; `rd_crack_tiler_fold_mali` the same under
  `ICO_VK_FAKE_LIMITS=mali` with `ICO_RD_FOG_PATH=local`. Both deny the old state line's `drawn
  inside the scene pass with the local fog`. The fill's place under the MoltenVK report:
  `rd_crack_glow` (`rd_crack_test 2 glow`) adds the glow's `cleanUpFB` after the fog (list 7: two
  untextured sprites into SCENE, Cs * As + Cd with RGBAQ 0, 0, 0, 0 under Z ALWAYS, then alpha 192
  at Z 1 under Z GEQUAL), so the fill is drawn after them with RGB alone (`RdCrackCounts.afterAlpha`
  1; the filled texel's RGB its neighbour's, its alpha `cleanUpFB`'s); `rd_crack_aura` (`2 aura`)
  adds the aura's paste (list 8: one colour into WORK0 by a sprite, then WORK0 over SCENE through
  (Cs - Cd) * As + Cd at As 32), so the fill is the first draw of the paste's pass (`pasted` 1, no
  fill pass of its own). Each replays the frame with the place pinned to the start of list 7
  (`rd__crack_fill_set_place(1)`): SCENE's colour and depth the same bytes, one fill pass there,
  and two render passes fewer (glow, on the fog's local read; at least one otherwise) or one (aura)
  with the place; the once lines are matched (`EXPECT2`), and `rd_crack_aura` denies the line that
  says the paste could not take the fill.
  Each run's `rd: crack fill:` state line is matched (`-DEXPECT`, `-DEXPECT2`; `-DDENY` must not
  match); `rd_replay_tool_crack` runs the tool on `rd_pixel`'s dump with `--enhanced --resolution
  2x --stats --no-crack-fill` and matches `crack fill: off (the replay tool's --no-crack-fill)`
  and the `stats: crack fill off: 0 passes, 0 pixels filled` line. `rd_crack_resolved_msaa2` and
  `rd_crack_resolved_msaa4` run the tool on the corpus room `lightning/rd-00300.rddump`
  (machine-local under `build-host/tmp/corpus/dumps/`, SKIP without it) with `--enhanced
  --resolution 2x --aa msaa2|msaa4 --stats`: its MSAA epoch closes at the shadow count's pass in
  list 3, before the fog, so the filler draws on the resolved picture: the state line ends `(drawn
  after the MSAA resolve)`, the stats line reads `crack fill on: 1 passes, M pixels filled` with M
  above 0, no replay left unfilled, `1 fills after lists 4-6` and `1 fills after the MSAA resolve`, and no `not drawn` line
  (`-DNEED_FILE`: SKIP without the dump; `-DNEED=rd: msaa: N samples`: SKIP where the device does
  not offer the count, which it then draws as off: lavapipe offers 4 only)
  ([MSAA.md](MSAA.md#the-crack-filler-after-the-resolve)).
- `rd_crt_test.c`: the CRT filter's options and its shader (`rd_crt`, `rd_crt_fixed`); downsampling
  changes no byte under the filter (`checkOff`).
- `rd_debug_test.c`: `debug.c` on the host, recorded (`rd_debug`); with the Game variants' bind
  (`debug_VariableInit` hands `ico_variants_bind_debug` the five switches and `debug_MenuOpen`)
  and the menu opened through that opener drawn on each of three ticks with no pad (title and bug
  list), nothing while closed, Triangle closing it.
- `rd_display_change_test.c`: a display change while the game runs (`rd_display_change`, and
  `rd_display_change_gl`): a frame of VU meshes under linear filters, strips, sprites and the
  reduction; a fresh start at Original (1x), then a second start at Original with the start-up
  set, switched to Enhanced at an output whose box gives a fractional scale and back to Original.
  At each state two frames are recorded and replayed: neither replay makes a pipeline
  (`RdPerfRecord.pipelineCreates` 0, `rd__pipeline_count` unchanged after the frame's
  `rd_begin_frame`), the switch to Enhanced makes the no-planes twins at its `rd_begin_frame` and
  its second replay draws VU passes through `vu_np_ps` (where the twin is loaded and the blend is
  fixed-function), and back at Original the second replay's draws, render passes, pipeline binds,
  blur folds, glow passes, no-planes passes, above-1x draws and crack fill passes are the fresh
  start's and DISPLAY is the fresh start's, byte for byte
  ([DISPLAY.md](DISPLAY.md#video-options-and-presets), run-time changes). The switch runs with the
  renderer's own thread on (not on OpenGL) and a display progress function that presents a blank
  frame as the window's screen does: every call of it is on the test's thread, none inside a
  render-thread job, and it is called when the change made pipelines; `rhi_call_guard_settle`
  makes a busy guard's wait. The window's half (the
  present allowance started again) has no test: the window is not run in the tests.
- `rd_filter_test.c`: the model viewer's draw filter on synthetic keys (`rd_filter`).
- `rd_fog_selftest_test.c` with `rd_fog_selftest.cmake`: the fog's self-test and probe; its log line
  matched for each path, MSAA counts, odd widths and [Full pixel](ARCHITECTURE.md#terms)
  (`rd_fog_selftest*`; the local read's first under `ICO_RD_FOG_PATH=local`,
  `rd_fog_selftest_local`). With `rd_date_selftest.cmake` and `ICO_RD_DATE_PATH=stencil`, DATE's
  self-test, which the fog's first run runs first: `DATE: stencil path (self-test: stencil ok 5,
  snapshot ok 5)` matched once (`rd_date_selftest`; the fifth sample a DATM 1 command's own
  overlap, the alpha before the draw), and under `ICO_RD_UBER=on` `DATE: colour read path
  (self-test: colour read ok 5, stencil ok 5, snapshot ok 5)` (`rd_date_selftest_uber`: the
  colour read's fifth sample the GS's per-primitive one). The failure paths, under
  `ICO_VK_FAKE_LIMITS=moltenvk` (the profile's Apple row: `fogLocal` and `dateStencil` the
  profile's own, not `[dev]`'s): with `ICO_RD_DATE_SABOTAGE=1`, `DATE: snapshot path (self-test:
  stencil fail ...` and `profile: DATE stencil off (self-test)` (`rd_date_selftest_sabotage`);
  with `ICO_RD_FOG_SABOTAGE=local`, `local fail N, copy ok N`, the copy taken at both sizes and
  `profile: fog local off (self-test)` (`rd_fog_selftest_local_sabotage`). Both scripts take
  `-DEXTRA` and `-DEXTRA2`, regexes that must each match the log at least once. With
  `rd_shadow_selftest.cmake`, the shadow count's self-test, which the fog's first run and its run
  at the second scene size run: every `shadow count: one draw (self-test: equal to the seven draws
  over WxH, 142 band centres ok)` line matched, at least one, and the line saying how the stencil
  is read, `four texels a gather, equal to the seven draws byte for byte` or `a texel a load, as
  the gathers' count ...` (`rd_shadow_selftest`; `rd_shadow_selftest_cropped` and
  `rd_shadow_selftest_wide` the same lines at 624 wide with the display at 16:10, exact 614.40, and
  at 2048 with 48:9, through `-DARGS` and the tool's aspect argument); with
  `ICO_RD_SHADOW_SABOTAGE=1`, `shadow count: seven draws (self-test: one draw differs ...)` and
  `profile: shadow count one draw off (self-test)` (`rd_shadow_selftest_sabotage`); both with
  `_gl` twins. The desktop's
  local-read probe after the first self-tests (`ICO_RD_LOCAL_PROBE=1`, lavapipe being the desktop
  class with local read): with `ICO_RD_LOCAL_PROBE_SABOTAGE=fast` the local reads kept, the fog
  local at both sizes and `profile: local read on the desktop: kept (probe x vs y ms)`
  (`rd_local_probe_kept`); with `slow` turned off, `DATE stencil off (probe)`, every target made
  again and the fog in place at both sizes (`rd_local_probe_off`)
  ([WORK_BUFFERS.md](WORK_BUFFERS.md#the-desktops-local-read-and-its-probe)).
- `rd_local_probe_test.c`: the local-read probe's verdict on synthetic times, no device: kept up to
  5 % slower, off above, kept with a time missing (`rd_local_probe_rule`).
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
- `rd_gsbase_test.c`: `GsBase.c`'s host path driving a synthetic scene (`rd_gsbase`; its post
  checks read SCENE back, so it draws list 11's last sprites whatever `ICO_RD_FADES_FOLD` says:
  the fold's DISPLAY is `rd_fades_fold`'s and `rd_fades_fold_ab`'s); photo mode's
  Deja vu draws the stage's sandstorm object once a frame with the vision's two nodes, gives back
  what it borrowed, adds no film grain, and draws nothing in stage 40, with the effect off or with
  photo mode closed (`checkDejaVeil`); its film reel on a row of `film_noise_l4.bga`'s shape: the
  first photo tick starts the row, each later tick runs it once on the filter's own frames, the
  game's frames and node records are the same after every tick and draw, the noise objects are
  drawn in model order before the veil with the evaluated nodes, their fade following the
  envelope's own frame tick by tick and wrapping at its length, nothing with the effect off, in
  stage 40 or with photo mode closed, the veil alone without the row (`checkDejaReel`; the
  evaluator there stands in for the game's). The wall test's flip hook on every disc's
  `gsb_UpdateGSSystem` copy, the check scripted: each flip passes `+0x380`'s translation, the stage
  and whether a game cut is pending since the flip before; a crossing marks the frame that flip
  closes (`RdCamera.cut` 2, `RD_SNAP_WALL` against the frame before), not the next;
  `gsb_ResetGSSystem` forgets the eye (`checkCameraWall`).
- `rd_still_test.c`: the still re-present's hash and decision (`rd_still`, no device): two ticks with
  the same sprites and a still camera hash the same at alpha 0.25, 0.5, 0.75 and 0.999 (the presenter's top), as a first and as
  a later present (the head copies are not in it); one colour word of the second tick, a sprite that
  moves, a moving camera and Motion blur: Smooth change or withhold it; `rd__reshow_why` for every
  input. The pictures: `rd_replay_tool DUMP out.png --interp 0.9 DUMP --interp-any --presents 3
  --present WxH` (a dump as its own tick before: nothing moves) writes the same PNGs with and without
  `ICO_RD_STILL_REPRESENT=0`, and its lines say which presents were `shown again`.
- `rd_replay_tool_still.cmake` (`rd_replay_tool_still`): the still re-present on a corpus dump as its own tick
  before, 4 presents at Enhanced 4:3 2x at 1280x960: presents 1 to 3 end `; shown again`, and all four pictures are the same bytes
  as with `ICO_RD_STILL_REPRESENT=0` (Not Run without the dump).
- `rd_replay_tool_glow_hold.cmake`: the glow held between ticks (`port/render/rd_glow_hold.c`)
  against its chain drawn at every present (`ICO_RD_GLOW_REDRAW=1`), on two consecutive godrays
  dumps (`build-host/tmp/pcsx2cmp/godrays/port2/keep/rd-01076` and `rd-01077`; Not Run without
  them) at Enhanced 4:3 2x, `--presents 2` with `ICO_RD_STILL_REPRESENT=0` (every present
  rendered) (`rd_replay_tool_glow_hold`). A still tick (rd-01076 as its own tick before): the lines
  say `glow drawn (the tick's first present)` for present 0 and `glow held` (or, with the switch,
  `glow redrawn ([dev] glow_redraw)`) for present 1, and SCENE and the presented picture of both
  presents are the same bytes held and redrawn. A moving tick (rd-01076 then rd-01077): present 0
  the same bytes both ways, and every pixel of present 1's SCENE that differs lies within 128
  texels of the glow's mask (WORK2 not at its fill level, either run's) (with Python and Pillow).
  `build-host/tmp/v094/after/v2-ab.sh` is the A/B over more pairs (statistics, crops, the region
  at 32, 64 and 128 texels, lavapipe timings).
- `rd_replay_tool_save_blended.cmake`: the blended present's frame F12 saves
  (`rd_dump_blended_arm`) replays alone as drawn: `rd_replay_tool CUR --interp T PREV
  --save-blended B` writes the frame built between the two after its replay, `rd_replay_tool B`
  replays it without `--interp`, and SCENE and the presented picture (1280x960) are the same bytes
  as the `--interp` run's. `rd_replay_tool_save_blended`: rd-01076 then rd-01077 (the godrays pair
  above) at alpha 0.5, Enhanced 4:3 2x; `rd_replay_tool_save_blended_163`: issue 163's pair
  (`build-host/tmp/v097/evidence/163/frame/`) at its present's alpha 0.999000013, 2x, full height,
  Full pixel (Not Run without the dumps). Both run the `--interp` again with `--render-thread
  sync` and `on` (`-DRT`): the interpolation and the replays on the render thread give the same
  picture and the same saved blended frame, byte for byte (the payload's alignment gaps are zeroed
  for this: the first threaded run's dump differed in 3 padding bytes the main thread's heap left).
- `rd_interp_test.c`: presentation between [ticks](ARCHITECTURE.md#terms) on two synthetic frames
  (`rd_interp`); the feedback head copies (`testFeedback`: `DISPLAY_HELD` and `FEED_HELD` at a
  tick's first present and the later ones, none for DISPLAY under Motion blur: Smooth, FEED128's
  unchanged, the motion blur's record as recorded; FIX' of 32 at 1, 2, 4 and 10 presents and at
  the ends); its rotation cases live in `rd_interp_turn_test.inc` and its seams cases in
  `rd_interp_seams_test.inc`, included where they stood, and its bars case in
  `rd_interp_bars_test.inc` (`testBars`: the same screen sprite under another key in each tick is
  drawn once, whole; a taller one or another blend FIX still cross-fades,
  [WORK_BUFFERS.md](WORK_BUFFERS.md#screen-sprites-redrawn-under-a-new-key-between-ticks)), and
  its keeps in `rd_interp_memo_test.inc` (`testStreamMemos`, `port/render/rd_interp_mesh.c`: two
  meshes of the same bytes compared once and the answer kept, a rewrite compared again, one
  buffer the same shape uncompared, a record with no serial compared at every ask; a skinned
  bone's pivot its bound vertices' centroid, the same at the next present and after another
  mesh's, moved by a rewrite; a shadow volume paired at 0.75, then drawn at 0.25 and 0.75 from the
  kept pairs, the 0.75 volume byte for byte the one paired anew);
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
  `mesh_gen` shard: a save state load's generation floor (`rd_drop_vu_meshes`) applies once a
  slot, so a slot wraps through the full 16-bit cycle after it (0xFFF1..0xFFFF, 1, 2, ...); the
  content table's four sandstorm sheets and the tiles appended to them (`sheetTableChecks`), one
  sheet on the device at 4:3 byte for byte the same with and without the tiles and at 16:9 the
  columns beside it drawn (`sheetPixels`), and the `photo_veil` shard: the veil's two layers at
  60:9 drawing every pixel with the tiles (`photoVeilPixels`), and the `photo_reel` shard: the film
  reel's four specks of the deja dump at their places on the 16:9 frame, none tiled, every pixel
  inside a speck's own place and in the 4:3 picture's columns (`photoReelPixels`); in the
  recording phase the winding marks (`windingMarkChecks`, `ICO_VU_MARK_FLIP`,
  [VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-winding-mark)): a prelit mesh without the EE's facing
  table unmarked (`facingFrom` none), with one marked on its kicks as the table says (the bits back
  through `rd__mesh_facing_bits`, a table of another vertex count refused, a copy taking the marks
  and their source), a lit strip wound one way marked at k - s = 2 and 4 from its normals, one of
  mixed normals triangle by triangle, a morph keeping the creation's marks, a table replacing a lit
  stream's own marks, the EE's builder (`rd_vu_facing_begin`, `_vertex`, `_attach`: a strip's
  bits, the table kept under the stream's hash and found again, a vertex without a normal voiding
  it, two tables under one hash leaving none), no other mark made; and the `winding_disc_0` ..
  `winding_disc_3` shards (77 without the disc image): the disc's strips' winding, a quarter of the
  models each, one line `winding: L lit triangles, A %, B %, C %; P prelit parts, E % edges
  consistent` and a line a stage in counts, each also feeding the parts' strips through the EE's
  builder and counting `facing marks M of N agree with the normals`, every one of them the check
  (`windingDiscChecks`).
- `rd_mip_test.c`: the Enhanced mip chain's size and its builder staying inside it (`rd_mip`).
- `rd_mirror_test.c`: mirror mode's flag and the flipped presenter output (`rd_mirror`).
- `rd_passring_test.c`: the pass ring the device-loss report prints: order, wrap, the replay table, the
  replays printed (the last three, whole at 430 passes a replay), the line format (the VU
  vertex pre-pass's entry among them, `testVuPre`) and the loss bisect's line; the pipeline
  note's hold (`testHold`: the shadow reduce's order, its pipeline looked up held leaving the
  seven draws' entry alone, its own entry at the GS size taking it when asked again); pure
  (`rd_passring`).
- `rd_msaa_test.c`: the anti-aliasing's MSAA against the frame without it (`rd_msaa`); with
  `msaa_resolve_ps` or `depth_resolve_ps` cleared for one call (a backend that cannot make one)
  the supported mask holds the supersampling values only; `testShadowReset`: a frame whose list 3
  holds the shadow count with a reset on SCENE's depth resolves once a replay
  (`RdPerfRecord.msaaResolves` 1, two when the reset's stencil clear closes the epoch again from
  inside `msaaEnd`), closes at list 3 (`msaaCloseAt` 4), records no shadow reset as a pass of its
  own under MSAA (`passKinds[RD_PERF_PASS_STENCIL]` 0: the volumes' pass takes it, issue 182) and
  leaves SCENE byte for byte as the same frame without the reset; `testShadowCount`: that frame's
  count (`rd__shadow_count_read`) by the seven draws, the one draw's gathers and its loads,
  without anti-aliasing and with MSAA4: the forms' counts byte for byte one count, MSAA4's equal
  to the count without anti-aliasing wherever the frame without it is one colour and one count
  over a 3x3, the epoch closed at list 3, no stencil reset pass, and gathers counted
  (`shadowGathered`) only where the stencil view takes them (`RhiLimits.stencilGather`; never on
  Direct3D, whose debug layer fails `rd_msaa` on a gather of the stencil plane); `rd_msaa_nvpath`
  runs the program with the reporter's NVIDIA profile of issue 182 forced
  (`ICO_RD_FOG_PATH=local`, `ICO_RD_DATE_PATH=stencil`, `ICO_RD_FX_DST_LOCAL=1`,
  `ICO_RD_SHADOW_ONE=0`) under the validation layer with its synchronisation checks; `testSkySlit`: two triangles of one surface over the clear with a 1/16
  pixel crack along their shared edge, the shadow count, then `cleanUpFB`'s glow mask (a sprite
  at GS Z 1, alpha 192, alpha only, GEQUAL without Z writes): MSAA4 marks no more texels along the
  crack than the frame without anti-aliasing (the covered-majority resolve,
  [MSAA.md](MSAA.md)) and some silhouette pixels' colours still differ; on any device, `testSuspend` (Options > Graphics >
  Apply's renderer switch): `rd_suspend` with a frame open, `rd_resume` carrying the scene size,
  the frame counter, the mirror mode and the GS state (its texture cleared), a texture id from
  before naming nothing, the DATE self-test's flag cleared, no frame open until
  `rd_resume_frame` opens one (once), whose temporary target is freed and which closes without
  a picture.
- `rd_native_test.c`: the native frame's centre rule on a synthetic frame at five widths, and in
  case s the region rule's strip test with the side bands (a behind-the-eye strip with the EE's
  facing table turned to the eye: pixels beside the 4:3 picture at W 688 and 2048, the picture's
  columns the frame without it, nothing at 512; turned away: nothing) (`rd_native`).
- `rd_perf_test.c`: the renderer's CPU cost per replay and its steady state, and the start-up set
  through `rhi_create_pipelines`: its progress calls (from 0, never back, the total once at the
  end), a stop after the first batch and the rest made by the next call; last, the background
  finisher (`checkBackgroundSet`): the set stopped after its first batch by the low-memory function
  is offered, `rd_pipelines_background_start` takes it once (a second start returns 0), the memory
  function is asked once before the first batch, one batch adds at most 32 pipelines, the system's
  signal (the function's 2) ends the finisher with nothing more made, and a second stopped set run
  to the end leaves every key of the reachable set with a pipeline and none made on first use
  after it; off Vulkan (`rd_perf_gl`, `rd_perf_attachment_gl`: no background creation) the start
  returns 0 with the finisher off and the memory never asked, and every key is made on first use;
  the exact blend's
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
  more fragments and the fragments the model's DATE rejects and keeps. Under GS state as shader
  data (`rd_pixel_uber`) the device's own path is the colour read: no stencil pair and no
  snapshot, the DATE draws counted as reading the alpha and the Z-writing strips drawn triangle by
  triangle, two draws each (at least 72 screen draws for the 36 triangles;
  `port/render/rd_replay_aa1.inc` (`aa1OrderDraw`)), the same bytes; the stencil cell then holds
  the colour read's DATE off (`g_rd.uberDate`), as `testDateStencil` does.
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
  the save state's picture, `checkShot`: the box of the open frame's present, CRT off and on, a
  later frame's present dropping it, one copy with the photo capture, replaced, cancelled and
  dropped at the shutdown; `checkShotDeferred`: the state host's deferred answer
  (`rd_capture_shot_arm`, `rd_capture_shot_take`) taken once with the box, no answer for a
  replaced or a cancelled request, a later frame's present dropping it as NULL, here and with the
  renderer's own thread presenting (not on OpenGL); `checkOverlayThread`: both overlay layers
  (the main one drawing through port/ui's font in overlay mode) of a picture `rd_present_post`
  hands over are collected on the test's thread before it returns, never in a render-thread job,
  once a layer a present; the test's own text in the font's overlay mode while the picture may
  still run gives all its quads; the output equals the one without the thread byte for byte;
  `rd_present_blank`'s layers are collected on the test's thread too (not on OpenGL);
  `checkFailedPresent`: with a host of the test's own that runs each picture handed over at the
  next drain, a present that presents is not counted by `rd_thread_failed_presents` and one that
  fails there (a movie shown by then) is, once; `checkWaitSplit`: with a host of the test's own
  whose picture ends at the next drain (5 ms), `rd_present_post`'s waits for the picture before
  are counted by whose (`RdThreadBlock` `freshWaits`/`furtherWaits`: one new frame's present of
  5 ms, two further presents' of 10 ms), another drain in neither, and `rd_frame_close_take`
  counts the frames closed since its last take and starts again;
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
  4x), both the GS size at 1x (`checkScales`); downsampling in force with a scale or a fixed size,
  not at the `window` resolution or under the CRT filter, and the supersampling measured against
  the GS grid (4x + SSAA2 is 4x, 2x + SSAA4 is 4x, 1x + SSAA2 is 2x; `checkScales`); without
  anti-aliasing 4x into 640 x 480 and into 1600 x 1200 both take the window's reduce, byte for byte
  the CPU's box average (`cpuReduce`), and 1x into 320 x 240 keeps the plain blit
  (`checkAaOffReduce`); with downsampling at 2x and 4x into 640 x 480 and 2x into 512 x 384,
  DISPLAY is the run's without it byte for byte, the downsampled DISPLAY
  (`rd__read_present_downsample`) is `cpuReduce` of it byte for byte with the same passes and no
  window reduce (`rd__last_present_reduce`), 4x + SSAA2 and 1x + SSAA2 present the downsampled 4x
  and 2x byte for byte, and on smooth gradients the downsampled presents are within 4 LSB of the
  1x present (`checkDownsample`).
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
  read compares the desktop set again, which the test's output says), and with those paths under
  GS state as shader data (`ICO_RD_UBER=on`: the collapsed set, config `uber`). `--keys` walks the replay
  with nothing drawn (`rd__pipeline_gather`), makes the keys it names and compares them with
  `rd__enumerate_reachable`; the test fails on a key the set lacks, which on a device is one
  first-use stall (`firstUseLine`), never a wrong picture. The union of the asked keys is left in
  `rd_replay_tool_keys/keys-<config>-<size>.txt` (`rd_replay_tool_keys`).
- `rd_submit_queue_test.c`: the submit thread's queue (`port/render/rd_submit_queue.h`): jobs run in the order posted, numbered from 1, an empty one refused; at most `RD_SUBMIT_DEPTH` queued and running, the running job counted until it finishes (one at a time, a third post refused meanwhile); the finished jobs' times taken oldest first, once, the oldest dropped and counted past the depth; the longest present since the last take, once (1 ns for a 0 ms present, 0 for a job that presents nothing); the block's means, longest, depth, waits by the render thread and the others, lists submitted on the render thread, cleared by the take with the depth carried for the jobs still queued; the jobs queued and running of each frame slot (a running job counted until it finishes, the other slot's queued job not counted for this one) and the pictures begun beside the other slot's job (`testSlots`) (`rd_submit_queue`).
- `rd_pipe_finish_test.c`: the background finisher's choices (`rd_pipe_finish.h`): a set of 1196 keys stopped after 33 gives its batches from key 33 in the set's order, 32 a batch, the keys made meanwhile and the failed ones skipped, a batch never mixing keys with and without a destination read (the other kind's key starts the next), every key not made taken exactly once, 37 batches for 1163 keys of one kind (the last of 11), none past the end; the step after a picture: a batch in flight waits, then is taken whatever the memory or a hold; the system's signal ends the finisher, no key left ends it, a hold (late step, slow picture) and a reading under the floor wait until their time, the memory is read only when nothing else holds the batch, then a batch begins; every step's words (`rd_pipe_finish`).
- `rd_cache_name_test.c`: the Vulkan pipeline cache file's name (`rd_cache_name.h`): two builds' tables of the same modules, each in its own memory, name the same file (no build in the name); one byte of a module changed (its first, a middle one, its last), a module a word longer or shorter, renamed, its stage changed, two swapped, one dropped or added, or `RD_PIPELINE_CACHE_FORMAT` raised each name another; names joined differently do not collide; the form `pipelines-<16 hex digits>.vkcache` (34 characters), a 35-byte buffer taken, 34 and NULL refused; an empty table still names a file (`rd_cache_name`).
- `rd_bisect_plan_test.c`: the loss bisect's second-level planner against oracles: a draw that
  faults with every draw before it (1, 731 and 1,312 of 1,312) found within 12 steps, the masked
  step's verdict, a killed step counted as lost, a step without a verdict planned again,
  contradicting prefixes ending the plan, 1,000 random oracles ending within 16 steps
  (`rd_bisect_plan`).
- `rd_depth_copy_dump.cmake`: a player dump with the fog replayed with the depth in place, copied
  and through a buffer, SCENE byte for byte the same (`rd_fog_depth_copy_dump`, `_d24s8`).
- `rd_shadow_test.c`: the shadow count on the stencil (`rd_shadow`); at scene scale 4 each
  composite, not flagged above 1x, against the float bilinear of its level within 3 LSB, then with
  `RdProfile.shadowBspline` ([dev] `shadow_bspline`) against the CPU B-spline within 3 (4 on the
  fixed-function blender) and a row across the shadow falling and rising once, the softening's AA0 (at its GS size) the area box of SCENE and its composite back the
  bilinear between AA0's texels, not counted above 1x (`checkComposite4x`, `checkSoftening4x`).
  Case m (with each case o run): the shadow volumes in the scene's open pass
  (`rd__shadow_merge_force` off and on) under `ICO_RD_STORE_POISON=1`, the synthetic volumes
  through both paths and Shadow.c's model through the one draw: the count, SHADOW0-2 and SCENE
  byte for byte, list 0's pass armed and the reset taken with it on (`shadowMergeArmed`,
  `shadowMerged` 1), one render pass fewer, the count target's new-target clear left undrawn
  (`shadowClearSkipped` 1; drawn as 0x5A under the poison), nothing armed, taken or left off.
  Case o: the count resolved in one draw from the stencil view against the seven stencil-tested
  draws (`RdProfile.shadowOne` flipped between two recordings of one frame), the count at its GS
  size (`rd__shadow_count_read`), SHADOW0-2 and SCENE byte for byte for the synthetic volumes,
  Shadow.c's model and a rectangle at four sub-pixel offsets, at 1x, at scale 2 and at 1931 x 1093
  texels (fractional on both axes), each recording resolved by the path asked
  (`g_rdPerf.shadowOne`, `shadowSeven`), the one draw with its stencil read four texels a gather
  and again a texel a load (`g_rd.shadowLoad`, counted by `shadowGathered`); the one draw's table for n = 0, 1, 63, 64, 128 and 255
  against 4 n mod 256 (`checkTable`, no device). Case p, with each case o run: the count target's
  colour neither loaded nor stored between the reset and the resolve
  (`rd__store_plan_count_color`), the synthetic volumes through the one draw and the seven draws
  under `ICO_RD_STORE_POISON=1` (the dropped load clears the colour to 0x5A) with the store plan
  off and on: the count, SHADOW0-2 and SCENE byte for byte, the colour dropped only with the plan
  (`shadowColorDropped`, `storeColorDropped`), and above 1x on the one draw the scaled count
  target poisoned at every texel (`poisonFrame`;
  [WORK_BUFFERS.md](WORK_BUFFERS.md#the-shadow-counts-colour)). Its `_gl` twin compares OpenGL's
  stencil view the same way; without the view the comparison prints a line and is skipped. Case x
  (`rd_shadow_test edges`, ctest `rd_shadow_edges` and its `_gl` twin): the shadow composites'
  two kernels at scale 2, the same frames (the synthetic volumes, Shadow.c's model, a rectangle at
  +7/16) recorded with one bilinear tap (the default) and with the B-spline (`shadowBspline`
  flipped between two recordings): SHADOW0-2 byte for byte, no pass above 1x with the bilinear and
  the three composites with the B-spline, SCENE byte for byte except within N of a shadow edge (a
  composited level not constant over level texels i0 - 2 to i0 + 3, the kernels' joint support and
  a tie: N = 3 level texels), some texels differing; the |d| histogram printed (`checkEdges2x`;
  [WORK_BUFFERS.md](WORK_BUFFERS.md#the-shadow-edges-above-1x)).
- `rd_state_test.c`: the recording half of rd without a GPU: the payload arena's alignment gap
  zeroed over a reused buffer's bytes (`testPayloadPad`), the VU fallback's set
  (`rd__vu_set_choose`, `testVuSetPolicy`: the stock Adreno 740's table row and two Turnip driver
  texts take the plain set with the start line's words, Qualcomm's driver without a row, lavapipe,
  RADV and NVIDIA the extended one, `[dev] vu_set = plain` (`ICO_RD_VU_SET=plain` or
  `ICO_RD_VU_PLAIN=1`) and the host's reason the plain one, `vu_set = extended` the extended one
  over each), the aura's measurement switch (`rd__aura_kernel_pick`, `testAuraKernelPick`: `[dev]
  aura_kernel = gs` drops the box of the aura's read of SCENE into FEED128 alone; unset, empty or
  `box` keeps it), the aura's two-pass switch (`rd__aura_box_pick`, `testAuraBox`: unset, empty,
  `auto` or another word the profile's, the on and off words over it, never on and no key of it in
  the set without a device, the pass list's name `ROWBOX`), the shadow count's colour
  (`rd__store_plan_count_color`, `testShadowCountColor`: under the plan the temporary count
  target's loaded colour goes DONT_CARE at its load and store, counted; a CLEAR, SCENE as the
  count, the plan off and no replay keep their ops; the pass list's `shadow count`), dump version 14's ticks' cameras (none in a
  the set without a device, the pass list's name `ROWBOX`), the pass area (`testPassArea`,
  `rd_state_pass_area_test.inc`: `rd__pass_area_cmd`'s boxes for a screen sprite and the aura's
  sprite on SCENE at 1x, at the phone's 848 x 448 GS frame in 2364 x 1080 texels and under Full
  pixel's crop hold every texel they cover and reach at most the pad past them; a VU grid, a draw
  on FEED128 and the sprite under DATE the whole target, a clear the end of a pass's commands; the
  pass begun at the sprite holds its box and the aura's, ends before the FEED128 draw, and its area
  is their union out to whole 32 x 32 tiles; none for a pass begun at a sprite the grid follows;
  `rd__pass_area_why`: none at the sprite, a 3D draw at the sprite before the grid and at the grid;
  the phone's paste (the aura's sprite over the frame's rect, FEED128's UVs) and list 11's
  full-frame fade, at 1x, the phone's frame and Full pixel: boxes of the whole target, the paste's
  run ending before the next FEED128 sprite, both kept whole because their draws cover the picture;
  `rd_perf_why_text`'s words for one replay and a mean over two, and cut to a small buffer; the
  switch's words), dump version 14's ticks' cameras (none in a
  tick's frame, a blended frame's round-trip), list order, defaults, state leakage,
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
  set, under the same two bounds (`testTilerFracSet`), GS state as shader data's planner and set
  (`testUberSet`: a LERP, a premultiplied subtract, an unblended mask-7 pass and a DATE pass one
  key through the read; the tiler set at the phone's fractional scale at least four times smaller
  than the fixed-function one, no DATE key, the VU family at most a third, no plain twin of a key
  the read draws, the counts and families printed), and a dump's menu text sheets loading as the renderer makes them, a sheet with
  The profile line's Turnip note (`rd__turnip_note`): "; turnip: Mesa 26.3.0-devel" when `RhiLimits.mesa` holds the version, "; turnip: Mesa version not named by the driver" when it does not, nothing on Qualcomm's own driver or with no limits.
- `diag_test.c`: the crash report (also from a worker thread under a batch's pipeline note), assert, watchdog, progress (with the heartbeat's start-up set count), start-up activity file (the batch notes), device-loss restart and loss bisect (the loss noted in the activity file, the bisect stubbed: once, before the game, never again after a start ended inside it) cases, each in a child process; the symbolizer, the report file and `ico_log_limited` (above)
- `rd_tex_test.c`: the texture cache and `Texture.c` on synthetic TIM2 files, and a text texture
  that could not be uploaded named once in the log, the game's own textures never
  (`textUploadLogChecks`) (`rd_tex`). `slotReloadChecks` (issue 174): a slot `tex_Init` hands the
  next stage, loaded with a picture of the same size and format, gets a new texture (one create,
  no in-place update) and the old texture keeps its texels for one frame tick and is destroyed
  after two. `threadPixels`: thirty replays handed to the renderer's own
  thread without waiting (`rd__replay_frame_async`) while the test's thread, the game's side,
  records the next frame through the texture cache and updates a texture of its own (each change
  waiting for the replay's reads, `rd__texlock_wait`): every replay run there and SCENE as drawn on
  one thread; the counts printed (not on OpenGL). On lavapipe the frame slots are apart (the
  timeline sync), so each replay's walk runs while the submit thread ends and submits the one
  before (RHI.md "Frame slots and the submit thread"): the race check for that partition. `threadSync`: in the sync mode a picture is
  waited for and not counted as handed over (`rd_submit_async` false), with the thread on it is
  until the drain (not on OpenGL). It is the render thread's program to run under
  ThreadSanitizer, as `audio_pull_test`: the `asan` preset with `-DICO_SANITIZE=thread` into a
  build folder of its own (`build-host/linux-x64-tsan`), `--target rd_tex_test`, run with
  `TSAN_OPTIONS=halt_on_error=1` under `setarch x86_64 -R`; a report is a failure.
- `rd_tex_pool_test.c`: the texture decode pool against the game thread's decode, 200 textures of
  every format and TEXA mode with levels and queued updates, the bounded queue's fallback, and on a
  device the uploads' alpha scans, byte for byte (`rd_tex_pool`; 77 without a device;
  [TEXTURES.md](TEXTURES.md#tests)).
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
  no guard wait while recycling and recording, one at the second list's end, and both targets
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
  none; then again under `ICO_VK_DEPTH_ALWAYS=1`: 64, 64, 64); the adapter cell (`adapterCell`: `rhi_set_adapter_wanted` with a name no device
  has still starts, on the device a plain start picks, one `rhi_adapter_list` entry current and
  automatic; the plain start's own name picks it again; NULL is automatic); the sync cell (`syncCell`) checks the fence ring under `ICO_VK_FAKE_API=1.1`
  and `1.0`, and under `1.0` the device's version (1.0), the `VkRenderPass` path and the UINT
  views' usage structure (`VK_KHR_maintenance2`) (`rhi_vk_vk11`, `rhi_vk_vk10`); under `1.0bare` (a
  1.0 loader without `VK_KHR_get_physical_device_properties2`, review finding 5) the same, the device
  made with no extended queries (`rhi_vk_vk10bare`); the D32F cell (`d32fCell`) checks
  `RhiLimits.depth32Attachment` against `ICO_VK_FAKE_NO_D32F_ATTACHMENT` (`rhi_vk_vk10_nod32f`).
- `rhi_d3d12_test.c`: the same checks on D3D12, a Windows GUI program that writes a log and shows
  a message box (`rhi_d3d12`).
- `rhi_d3d11_test.c`: the same checks on Direct3D 11 (WARP; WARP again with
  `ICO_D3D11_FAKE_NO_PARTIAL_CB=1`, the upload buffers' constant buffers mapped as on a device
  without partial constant buffer updates; the upload ring's written mark on WARP, where a submit
  must write exactly the bytes under the mark that its copy reads, 4096 of 8192, then 1024 in the
  next frame, 2064 for marks 512, 2050 and 1000 in one frame, and all 8192 of a buffer never noted,
  each read back byte for byte; the video memory fields of `RhiStats` on WARP: a budget, no more
  to reserve than the budget, two reads inside 250 ms equal; the hardware adapter, the swapchain on
  a hidden window, Vulkan for comparison), the cells' shaders compiled at run time from
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
  dual-source leftover cell counts 3 commands and no leftover.
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
  the colour bytes a pixel of each Apple GPU family (`rhi_metal_color_bytes`), and the Mesa version out of
  a driver's name and `driverInfo` (`rhi_mesa_version_parse`: `Mesa 25.3.0-devel (git-bbdd688)` and the
  issue 163 tablet's `Mesa 26.3.0-devel (git-a995eaeda4)` as devel, a release, a distribution's `25.0.7-2`
  as no devel, a release candidate (`-rc` with a number), `mesa-25.1`, `MESA v24.2.8-DEVEL`, a bare version under a Mesa name, a Mesa word with
  no number then one with it; Qualcomm's compiler text, NVIDIA's version, empty, a lone major and NULL give
  none; a short buffer) and `rhi_text_one_line` (Qualcomm's driver text with newlines on one line, issue 123) (`rhi_driver_version`).
 `rhi_adapter_match_test.c`: the `[video] adapter` rules (`port/rhi/rhi_adapter.c`): the exact
  name's index, the first of two equal names, no match for another case, a part of a name, a
  longer name, `""`, NULL or a NULL entry; the device-list text with and without scores, the
  unsuitable reasons, a short buffer cut (`rhi_adapter_match`).
- `rhi_vk_enum_test.c`: every `rhi.h` enumeration has a Vulkan mapping, `RGBA32_UINT` among them
  (`rhi_vk_enum`).
- `rhi_vk_present_mode_test.c`: the present-mode choice over the sets drivers offer, and FIFO
  over every set and preference under the FIFO fallback (Android's mailbox and FIFO with vsync off,
  Mesa's four modes, vsync on with mailbox preferred, FIFO latest ready asked) (`rhi_vk_present_mode`).
- `rhi_vk_swapchain_test.c`: the window path without a display, on SDL's offscreen driver
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
  b (4)); the side bands (part a's rule, between-ticks and facing rows; part b's `DEPART_EYE_EDGE`
  cases at the picture's right edge, turned to the eye and away; every other case drawn with
  `ICO_VU_NO_BESIDE`); the pictures use the plain mapping (no exact viewport), so the planes stay
  the GPU's there (`vu1`, `vu1_mali`).

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
  fake tables, CPU only (`settings`, `settings_extra`, `settings_extras`); `settings_test.c`'s
  `testShowFps` steps Display > Frame rate counter (Off by default, Right then Left both switch it, the preset kept, `[video] show_fps` saved as `true`, a file's `true` read; the Display row list is nine rows with the new row after Frame rate, and the pause menu's Display page is eight with Video mode hidden, both in the Options window's slots ([UI.md](UI.md#the-window))); `ui_fps.c` itself is in the window build only and has no test (the number is `fps_counter_test`'s; a hand check: the corner, legible, off by default; on a phone with the touch controls on the top edge between the D-pad and Select);
  `testEffects` steps Motion blur round Off, Original (the default) and Smooth, each step saved as
  `effect_motion_blur` and `effect_motion_blur_mode` with the getters at once, the note under the
  row and the page fitting from both menus; `settings_test.c` also
  `testDownsample` (the Downsampling row right after Anti-aliasing on Picture and not on Display, Off by default, its note, Right and Left, the preset kept, `[video] downsample` saved and read), the texture and model pack rows and the CRT rows on Picture, the dump rows through the Developer page (four rows with Developer mode on, two without), and `settings_photo_test.c` writing `settings_picture.png` and `settings_developer.png` beside the other pages; `settings_test.c`'s `testNotesFit` (every page's note in all eight languages wraps to at most two lines and is kept whole by the note row's 256-byte buffer); `settings_extra_test.c`'s `testGraphicsApi` (Options > Graphics: the link hidden with no renderer names and with one, shown after Effects with four; the Graphics API row's value text and the `video.backend` key through Right round Vulkan, OpenGL, Direct3D 12, Direct3D 11 and Automatic, Left wrapping, the key saved after leaving the page, an unlinked value shown as written and stepped from Automatic) and `testGraphicsApply` (Graphics > Apply over a fake `UiGraphicsHost` running `vulkan`: Apply hidden without a host, for Automatic and Vulkan, with one renderer, shown for OpenGL with its note; Cross asks for `opengl` once; the countdown's Keep and Revert alone with the cursor on Revert and the note counting 10 then 9 s; 600 `ui_settings_vsync` calls at 60 Hz ask for `vulkan` with the timeout reason and put the value back to Automatic; Keep calls `kept()` and saves `[video] backend = "opengl"`; Revert and Triangle ask for the backend that ran and put the value back; a failed answer's popup names both renderers and puts the value the page found back (no Apply then); a refused one shows nothing and keeps the choice; a pending one is polled every frame with Cross ignored until it answers; `ui_settings_save_on_quit` during the countdown writes the value the page found, the countdown going on, and Keep then saves the choice; Automatic with the host's `automatic()` naming OpenGL offers no Apply while OpenGL runs and offers it on Vulkan; the strings translated in every language) and `settings_test.c`'s `testBuild` (the Main page's sixteen rows, the Display page's nine, the Picture page's eleven, the Effects page's eight, the Developer page's four and the Graphics page's six: Graphics API, Graphics card, Apply, Keep, Revert, Back); `settings_adapter_test.c` (`settings_adapter`, and on the other discs `_jpn`, `_kor`, `_cn`: the row alone): Options > Graphics > Graphics card over a fake `UiGraphicsHost` with a fake `runningAdapter()`: the row and, with one renderer, the link hidden with no card names and with one, both shown with two and the Graphics API row hidden, the note, the page fitting; Right round the two cards and Automatic writing `video.adapter` (`""` for Automatic), Left wrapping, the key saved; a saved name the list lacks shown as written and stepped from Automatic; Apply hidden for the card running and for Automatic on Automatic's card, shown for the other card; Cross asks for `vulkan` on that card; the countdown hides the row; 600 vsyncs ask for the card before with the timeout reason; Keep saves `[video] adapter`; Automatic on the other card asks for `""`, Revert for the card before; a failed answer's popup names both cards and puts the value found back; the strings translated in every language; the Main page's row lists in the settings tests read sixteen rows (seventeen with the rc build's Share a bug report); `settings_extra_test.c`'s Graphics driver case (`testGpuDriver`, a fake host) checks the eight answers' popups (picker not opened, no picker app, file unreadable and the wrong memory page size, whose popup holds the host's own sentence, among them), the note's last-seen line for each driver (the phone's own, the first, one never started: the line first, the Adreno note after it, a failed driver's note kept, the note alone again without a line from the host) and the Add from the drivers folder row: hidden at 0 zips, shown at 2, Left and Right going round the names, Cross calling `installFileBegin` once for the shown zip with a second Cross ignored while it runs, the poll's popup; presses Right in photo mode through Deja vu and checks the popup's words (`ui__photo_film_word`)
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
  scrolled by nine Downs, `settings_main_scrolled.png`, and the Picture and Developer pages, `settings_picture.png` and `settings_developer.png`); with `jpn` or `kor`, that disc's title and main page (`settings_render_jpn`,
  `settings_render_kor`); `cn` the same for the Asian disc (`settings_cn`, `settings_extra_cn`,
  `settings_extras_cn`, `settings_render_cn`, `ui_cn`, `gallery_cn`, `model_viewer_cn`); `ntsc`,
  `aug6` and `jun26` run settings_extra_test's Game variants page, the Options window
  (`testScroll`) and the boot's hand-over to the title with Skip the opening
  (`testSkipOpeningTitle`: the boot's layout 4 handing over to 51, the title 9; issue 160) only
  (`settings_extra_ntsc`, `settings_extra_aug6`, `settings_extra_jun26`: Cross
  confirms, the 2001 notes, the demo's 7-hit barrier, its toughness row without American and no
  Debug menu row; the window's edge moves through that disc's own cursor move).
- `settings_extra_test.c`'s `testSecondCard`: Options > Gameplay > Second memory card from the
  title and the pause menu, after Skip the opening and before the Film effect, Off by default; a
  registered listener hears the current value, Right turns it on with one call and sets
  `gameplay.second_memory_card`, the saved file has it, its note shows on the cursor, Left turns
  it off with one more call; the cleared page still fits. `settings_test.c`'s `testBuild` reads
  the Gameplay page's twelve rows, `testGameOptions` nine Downs from Yorda to Film effect, and
  `settings_discs` (every disc) shows the row from both entries.
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
  in the Brazilian Portuguese one, rows cobra838 corrected or filled),
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

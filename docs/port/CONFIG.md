# Configuration, language and clock

The port has one settings file, `config.toml`, in the per-user folder. The
`ico-pc.ini` beside the executable is an override layer on top of it, for
the developer and test keys and the disc image path. The system language
that libscf reports and the clock behind `sceCdReadClock` and the EE timers
are answered from these files and from the host.

| file | what |
| --- | --- |
| `port/platform/host_config.c`, `.h` | the ini reader, the TOML-subset reader and writer (`ico_toml_*`), the per-user folder, the ini-over-toml layering at load |
| `port/config/config.c`, `config.h` | `ico_config_get_*`, `ico_config_set_*`, `ico_config_save` |
| `port/config/sysconf.c`, `sysconf.h` | `sceScfGetLanguage`, `sceScfGetTimeZone`, `sceScfGetSummerTime`, and the answers to the skipped boot screens (`ico_boot_*`, `ico_sysconf_video_mode`, the language setter) |
| `port/platform/clock.c`, `clock.h` | the wall clock for `sceCdReadClock` and the EE timer counters |
| `port/config/test/config_test.c` | the tests (`config` in ctest) |

## Where the files are

- **config.toml** is `<pref folder>/config.toml`. In the window build the
  pref folder is `SDL_GetPrefPath("ico-pc", "ico-pc")`, which is
  `%APPDATA%\ico-pc\ico-pc\` on Windows and `~/.local/share/ico-pc/ico-pc/`
  on Linux; `ico_pc` is compiled with `ICO_HOST_SDL_PREFPATH` for this
  (`port/config/CMakeLists.txt`). The headless build, the unit tests and a
  failing `SDL_GetPrefPath` use the executable's folder instead.
  `ico_host_pref_dir` is the one function that decides.
- The memory card folder (`ico_host_saves_dir`) defaults to
  `<pref folder>/memcard`, so the saves live with the config;
  `[paths] saves` or `saves=` moves them; `[paths] saves2` adds a second
  card in port 1.
- **ico-pc.ini** sits beside the executable (docs/port/TESTING.md).
- **ico.o2r**, the extracted game data (docs/port/DATA.md), is
  `<pref folder>/ico.o2r`, written on the first run. One beside the
  executable is used when the pref folder has none.
- Logs, the trace and the `dumps/` folder are written in the executable's
  folder.
- A path that does not fit in `ICO_PATH_MAX` (1024) is an error, not a cut-off
  name: `ico_path_join` returns -1 and leaves `""`, and the log, trace, dump,
  screenshot, config and archive writers log it and do not write.
- A relative path in either file (`iso`, `saves`, `saves2`, `pad_script`,
  `dump_dir`, `audio_dump`, `trace`, `input_record`) is taken from the
  executable's folder, not the pref folder.

## Precedence

`ico-pc.ini` beside the executable wins over `config.toml`, which wins over
the built-in default.

For a key that has an ini name (the table below), the ini wins when it has
the key with a non-empty value. When `ico_ini_load` loads the executable's
own ini (`ico_host_ini_path`), it fills the keys the ini lacks from the
toml (`ico_ini_load_layered`, a pure read; the environment exports and the
dump and log folders are `ico_ini_export`, called once from `ico_ini_load`
at start-up, so a later lookup such as `ico_host_saves_dir` has no side
effect), so `main_host.c`, which reads only the ini, sees the layered result; a
toml `true` or `false` reads as `1` or `0` there. Keys with no ini name
(most of `[video]`, `[game]`, `[input]`, `[gameplay]`, `audio.volume`,
`audio.music`, `audio.effects`, `audio.output`, `audio.device`, `version`) come from the toml alone. `ico_config_get_*` applies the same
order. `ico_config_set_*` changes the toml copy only, so a key the ini
overrides reads back as the ini's value. Loading any other ini path with
`ico_ini_load` gives the plain ini.

The command-line options of `ico_pc` (`--iso`, `--ticks`, ...) sit above
both. The environment variables other libraries read (`ICO_ISO`,
`ICO_AUDIO`, `ICO_AUDIO_DUMP`, `ICO_AUDIO_VOLUME`, `ICO_AUDIO_MUSIC`,
`ICO_AUDIO_EFFECTS`, `ICO_AUDIO_DEVICE`, `ICO_FIXED_CLOCK`,
`ICO_START_STAGE`, `ICO_UI_POPUP_TEST`, `ICO_RD_DUMP_*`) are exported from
the layered result at ini load; they are an internal hand-over, not a
user-facing switch.

## The key table

| `config.toml` | ini key | default | meaning |
| --- | --- | --- | --- |
| `version` | | `1` | file format version; a file with a larger one is still read and its unknown keys kept |
| `[game] classic_menu_text`, `[game] port_font` | | | retired (package TXT2): no longer read. A file that has them loads as before, keeps them (as any unknown key) and logs `config: game.classic_menu_text is no longer used; ignored` once a load; the game's own words always keep their texels and the port's text is the game's lettering with Arimo as the fallback (docs/port/UI.md, "Menu text", "The font") |
| `[paths] iso` | `iso` | `""` | the disc image |
| `[paths] saves` | `saves` | `<pref>/memcard` | the memory card folder |
| `[paths] saves2` | `saves2` | `""` | a second memory card folder, the card in port 1; empty: port 1 has no card, as on a PS2 with one card (docs/port/SAVES.md, "A second card") |
| `[video] backend` | `backend` | `"vulkan"` | the window build's renderer backend, `"vulkan"` or `"d3d12"` where the build has it; an unknown or missing backend falls back to Vulkan (`ico_window_open`, `rhi_CreateBackend`) |
| `[video] preset` | | `"original"` | `"original"` or `"enhanced"` (docs/port/DISPLAY.md) |
| `[video] resolution` | | `"window"` | Enhanced: the scene's resolution, `"window"`, `"WxH"` or `"Nx"` (1 to 8) |
| `[video] aspect` | | `"4:3"` | Enhanced: `"4:3"`, `"16:10"`, `"16:9"`, or `"auto"` (the window's, clamped to 4:3..16:9) |
| `[video] fullscreen` | | `false` | borderless fullscreen at the desktop resolution; Alt+Enter toggles it at run time without saving |
| `[video] vsync` | | `true` | the swapchain's present mode: Vulkan FIFO when on, else MAILBOX or IMMEDIATE; D3D12 sync interval 1, or 0 with tearing. With vsync on and a `framerate` other than `"original"`, MAILBOX is preferred where the surface offers it, so there is no tearing and a present never waits for the display (DISPLAY.md, "Vsync and an uncapped frame rate") |
| `[video] texture_filter` | | `"original"` | Enhanced: `"original"`, `"trilinear"`, `"anisotropic"` (with generated mips) |
| `[video] full_height` | | `false` | Enhanced: skip the reduction's vertical halving |
| `[video] framerate` | | `"uncapped"` | both presets: `"original"` presents once per game tick; `"uncapped"` presents at the display's rate, interpolating between the last two ticks; a number from 30 to 1000 caps presents per second. The Original preset keeps its PS2-exact pictures on the ticks and presents blended ones between them (RENDER_API.md, the interpolation section) |
| `[video] crt` | | `false` | both presets: the CRT filter at present time, the last thing drawn: the menus and the port's popups are shown through it, and while it is on (at a strength above 0) the scene is drawn at 1x whatever `resolution` says (docs/port/DISPLAY.md, "CRT filter") |
| `[video] crt_mode` | | `"consumer"` | `"scanlines"`, `"consumer"` (slot mask), `"trinitron"` (aperture grille), `"pvm"` (aperture grille, darker gaps) or `"shadow"` (dot-triad shadow mask); an unknown name is `"consumer"` |
| `[video] crt_strength` | | `1.0` | 0 to 1: how far the filter is eased in (0 shows the plain picture); the Settings row writes it in tenths |
| `[video] crt_scanlines`, `crt_mask`, `crt_halation`, `crt_bloom` | | `-1` | config only: replace the mode's scanline strength, mask strength (how much of the other two colours each phosphor stripe holds back, 1 = pure stripes, and the darkness of the gaps; on `"scanlines"`, adds an aperture grille), halation, bloom (0 to 1, larger clamped); negative or absent keeps the mode's. Written back by a save only when set |
| `[video] crt_curvature` | | `-1` | config only: the mode's x curvature (0 to 0.25), y 1.5 times it except on `"trinitron"`'s flat vertical; negative or absent keeps the mode's |
| `[video] video_mode` | | `"pal50"` | the answer to the boot 50/60 Hz screen, which the port skips: `"pal50"` (`systemStatus[0]` = 1, the PAL default) or `"60hz"` (0). When absent the game's own value is used (50 Hz, or the memory card's); an explicit value wins over the card's system file. The Settings menu sets it (docs/port/SETTINGS.md) |
| `[audio] enabled` | `audio` | `true` | `false` (`audio=0`) opens no audio device; the sound driver still runs |
| `[audio] volume` | | `1.0` | exported as `ICO_AUDIO_VOLUME`; the SDL output scales its blocks by it, live (docs/port/AUDIO.md, "Output") |
| `[audio] music` | | `1.0` | 0.0 to 1.0, the music's gain on the voices' volumes (background music sequences and the streams other than Yorda's hint voice); exported as `ICO_AUDIO_MUSIC`. The Settings menu steps it live (docs/port/AUDIO.md, "Gains and output mode") |
| `[audio] effects` | | `1.0` | 0.0 to 1.0, the sound effects' and Yorda's hint voice's gain; exported as `ICO_AUDIO_EFFECTS` |
| `[audio] output` | | `"auto"` | `"auto"` (the game's own stereo or mono choice, from the memory card and the Options screen), `"stereo"` or `"mono"` (wins over the card's; the Options screen's toggle is written back here). Read by `ico_opt_output_mode` (docs/port/OPTIONS.md, "Sound output") |
| `[audio] device` | | `""` | the playback device's name as SDL reports it; empty, or a name no device has (logged once), is the system's default. Exported as `ICO_AUDIO_DEVICE`; the window build only (docs/port/AUDIO.md, "Output device") |
| `[input]` and `[input.*]` | | | the bindings, dead zones and mouse settings (docs/port/INPUT.md); the Settings menu's remap screen writes `[input.kb]`, `[input.mouse]`, `[input.pad]` and `mouse_sensitivity` |
| `[gameplay] stick_fix`, `yorda_safe`, `mirror` | | `false` | the gameplay options (docs/port/OPTIONS.md). `mirror` is only the value before a run starts and at the title; a run's value comes from the New Game screen or the loaded slot |
| `[gameplay] developer_mode` | | `false` | the debug menu and option table (docs/port/DEVELOPER_MODE.md) |
| `[mirror] slot_N`, `slot_N_sum` | | none | the mirror mode of the game last saved in slot N (`game.00N`, N 0 to 9) and that save's checksum (the uint32 the game writes after the block). Written on each save and read on each load (`ico_mirror_slot_*` in `port/game/options.h`; docs/port/SAVES.md, "Mirror mode"). An entry whose sum does not match the slot's save is ignored, which means Off |
| `[game] language` | | `"auto"` | `"auto"`, `"en"`, `"fr"`, `"de"`, `"it"`, `"es"`: the game's language, since the boot language screen is skipped ("Language" below); the Settings menu sets it |
| `[game] circle_back` | | `true` | Circle (gamepad B) also works as Triangle's back action in the game's own menus (the Options screen, the pause menu, the vibration screen, the memory card screens; not the Button Configuration or Brightness screens); `false` is the PS2's behaviour, Triangle alone. The port's own screens take Circle either way. Settings > Controls, "Circle goes back"; `ico_opt_circle_back`, `lt_ext_BackButtons` (`port/ui/layout_ext.h`) |
| `[game] achievements` | | `true` | `false` turns the achievement popups off; unlocks are still recorded in `<pref>/achievements.toml` (docs/port/ACHIEVEMENTS.md) |
| `[photo] stick_speed` | | `1.0` | photo mode (docs/port/DISPLAY.md, "Photo mode"): the sticks' speeds (orbit, dolly, pan) times this, above 0, at most 10; read each time photo mode opens |
| `[photo] invert_y` | | `false` | photo mode: the left stick's up and down swapped (up lowers the camera) |
| `[photo] png_dir` | | `"screenshots"` | photo mode: the folder the pictures go to, in the pref folder (`<pref>/screenshots/ico-<date>-<time>.png`); created when missing (one level) |
| `[dev] ticks` | `ticks` | none | exit after N Main ticks |
| `[dev] watchdog` | `watchdog` | `30` | seconds without progress before the watchdog fires; 0 is off |
| `[dev] trace` | `trace` | on in the headless build, off in the window build | `true`/`1` writes `logs/trace-<time>.txt`, a path writes there, `false`/`0`/`none` writes none. The window build writes a trace only when asked, so a player's `logs/` does not grow by one per run; `logs/ico-pc.log` is always written |
| `[dev] dump_every`, `dump_dir` | `dump_every`, `dump_dir` | off, `dumps` | renderer frame dumps (window build) |
| `[dev] dump_interp` | `dump_interp` | `false` | with `dump_every`, each dump also gets the frame interpolated half way from the one before, `rd-NNNNN-i50.rddump`. `export_dump_keys` in `port/platform/host_config.c` hands it to the renderer as `ICO_RD_DUMP_INTERP` whenever `dump_every` is set, so this key decides, not a shell variable |
| `[dev] dump_from` | `dump_from` | `0` | with `dump_every`, no frame numbered below this is dumped, so a run can dump a late stretch densely. Handed over as `ICO_RD_DUMP_FROM` |
| `[dev] perf_log` | | `false` | the window build writes one CSV line per renderer replay into `logs/ico-pc-perf.csv` (`RdPerfRecord` in `port/render/rd.h`): the CPU phases, the RHI counts, the GPU times, and per line `start_ms`, `alpha` (-1 for a replay that is not a present between ticks) and `first_of_tick`. The 10-second `window:` log lines carry the same figures averaged, with or without this key (RENDER_API.md, the performance section) |
| `[dev] slow_step_ms` | | `8` | the window build logs a `window: slow step` line for any simulation step longer than this many milliseconds, with where its time went (vsync callbacks, audio, game threads and their switches, achievements, pump, other) and what ran (disc reads, texture decodes, pipelines created, memory card busy, stage, loading); at most 5 such lines per stats block. `0` turns it off |
| `[dev] input_record` | `input_record` | `true` in the window build, `false` headless | the pad recording: what the game read from the pad each Main tick, written as a pad script (`port/input/input_record.h`). `true`/`1` writes `logs/input-<time>.txt`, a path writes there, `false`/`0`/`none` turns it off. Its header names the build and the config values the simulation depends on; giving that file as `pad_script=` to the headless build replays the session (docs/port/TESTING.md) |
| `[dev] audio_dump` | `audio_dump` | none | a WAV of the mixed audio; `1` means `logs/audio.wav` |
| `[dev] pad_script` | `pad_script` | none (headless: `pad-script.txt` beside the executable if present) | a scripted pad that replaces the live controller. The window build loads one only when this key names it, so a stray `pad-script.txt` beside a player's executable is ignored |
| `[dev] verify` | `verify` | `true` | `false` skips the disc image SHA-1 when `use_iso` is on; the first-run extraction always verifies |
| `[dev] use_iso` | `use_iso` | `true` headless, `false` window build | `true` mounts the disc image directly; `false` mounts the extracted `ico.o2r`, extracting it from the image on the first run (docs/port/DATA.md) |
| `[dev] write_config` | `write_config` | `true` window build, `false` headless | write `config.toml` with its defaults when there is none (below) |
| `[dev] headless` | `headless` | `false` | marks a run for traces and tests, which fixes the clock (below). The headless build is headless regardless |
| `[dev] fixed_clock` | `fixed_clock` | see "Clock" | whether the disc clock is fixed or real |
| `[dev] start_stage` | `start_stage` | none | the stage Main starts in instead of stage 1 (boot, language, title), 1 to 105 in `stageData` order (e.g. 34 st13a ELEVATOR, 15 st09a WINDMILL, 37 st25a QUEEN). Handed over as `ICO_START_STAGE` to `debug_TryToGetStartStage` (`common/src/debug.c`), the hook the development build's start-stage file fed. It skips the boot and title flow, so the game flags are those of a fresh boot; Main then sets `systemStatus[0]` from `[video] video_mode` (`ico_boot_video_mode`) as the skipped boot would have. A stage whose data file is not on the disc (`STGNOCD_*`, `STGONLYSAMPLE_*`: 64 to 87, 89, 90, 92 to 102) is refused with a log line `start_stage N (name) refused` and the normal boot runs (`common/src/main.c`) |
| `[dev] switch_to`, `switch_at` | `switch_to`, `switch_at` | none, `0` | one forced stage change, for testing the transitions (TESTING.md, "Booting every stage"): from Main tick `switch_at`, at the first tick on which the stage is up (`systemStatus[6]` clear), the current stage's exit that leads to stage `switch_to` (1 to 105) is taken as the boy's exit floor takes it, `RequestStageChange(i, boyGObj, 0, 1.0f, 8.0f)` (`fumi/src/boyact.c`), which records the boy at that exit's entrance and fades out at 1.0 and in at 8.0; a stage with no exit to `switch_to` gets `stgmgrForceSwitchWithFade(switch_to, 1.0f, 8.0f)` and no entrance. Fires once. The log gets `dev: switch_to N at tick T: stage S exit I` and then `dev: stage N up at tick T`. Handed over as `ICO_SWITCH_TO` and `ICO_SWITCH_AT` to `ico_dev_switch_stage` (`common/src/main.c`); unset, nothing happens. Stage 1 is the boot and title, whose exit only the opening takes, so a forced switch out of it is not meaningful |
| `[dev] debug_option` | | `0` | with `[gameplay] developer_mode` on and a value other than 0, `debug_VariableInit` loads `<pref>/dev/thisIsYourDebugOption`, the debug option table the Debug Mode page saves; 0 keeps the retail values (`ico_opt_debug_option`; docs/port/DEVELOPER_MODE.md) |
| `[dev] popup_test` | `popup_test` | off | `true`/`1`/`on`/`yes` queues the test popup at Main tick 100 and every 150 ticks after (window build; handed over as `ICO_UI_POPUP_TEST` to `port/ui/ui_host.c`; docs/port/UI.md, "Popups") |
| `[dev] unlock_credits` | `unlock_credits` | `false` | `true` (ini `1` or `true`) unlocks Settings > Extras > Credits whatever the achievements say, for tests (`ico_credits_unlocked`, `port/game/credits.c`; docs/port/EXTRAS.md, "Credits"). A player unlocks it by reaching the ending |

`ico_config_save()` writes `version`, `[paths] iso`, `[video]`, `[audio]`
and `[game] language` when they are absent, and the `[dev]` and `[input]`
keys only when they are set. The Settings menu calls it (or
`ico_video_save`) when a screen is left after a change. The first run writes
the file itself (below).

## The file is written on the first run

After the ini loads, `main_host.c` calls `ico_config_write_first_run()`
(`port/config/config.c`). When `<pref folder>/config.toml` does not exist it
is written, through `config.toml.tmp` and a rename, with `version`, `[paths]
iso`, `[video]`, `[audio]` and `[game] language` at their defaults (the keys
`ico_config_save` adds) and a comment line per section, so a player who
never opens Settings has a file to edit. An existing file is never touched,
not even an empty one. The write is a window-build default only: the
headless build, whose pref folder is the build tree, writes it only with
`[dev] write_config = true` (`write_config=1` in the ini), so test runs leave
no file. Settings, `ico_video_save` and `ico_config_save` rewrite only the
lines they change, so the template's comments stay.

## The writer

`ico_toml_render(t, existing)` applies a table to a file's text, and
`ico_toml_save(t, path)` writes it to `path.tmp` and renames that over
`path` (on Windows `MoveFileExA` with `MOVEFILE_REPLACE_EXISTING`), so a
failed write leaves the old file intact. Only the lines whose key is in the
table and whose value differs are rewritten (as `key = value`; a trailing
comment on such a line is dropped). Everything else is kept byte for byte:
comments, unknown keys and sections, multi-line values the reader ignores,
CRLF line ends. A key the file lacks goes after the last key of its
section, a new section goes at the end, and a top-level key (`version`)
goes after the existing top-level keys or before the first section. Saving
an unchanged table changes nothing. Strings are written quoted with `\\`
and `\"` escaped; bools, numbers and arrays as held.

This is why a hand-edited `config.toml` survives the Settings menu: the
menu only ever touches the lines it changed.

## Language

`sceScfGetLanguage()` (libscf numbering: 0 JP, 1 EN, 2 FR, 3 ES, 4 DE, 5 IT)
decides once per run and returns:

1. `[game] language` when it is one of `en fr de it es`;
2. otherwise the host's locale: `SDL_GetPreferredLocales` in the window
   build (the first preferred locale the game has a language for), else
   `LC_ALL`, `LC_MESSAGES`, `LANG` (not `C` or `POSIX`) in the headless
   build or when SDL has none;
3. otherwise English. A locale in a language the game lacks, such as `ja`
   or `pt`, is English.

It logs `scf: language N from ...` once.

On the PS2 the console language only preselects the cursor of the boot
language screen (`common/src/kanbanBoot.c` step 101: item 26 for English,
27 French, 28 German, 29 Italian, 30 Spanish); the player confirms a sign,
and the game stores the choice in the global `NonLinearCameraMove`
(misnamed; 2 EN, 3 FR, 4 DE, 5 IT, 6 ES; step 102).

The port skips that screen and the 50/60 Hz one (`kanbanBoot.c`),
because the Settings menu holds both choices:

- step 101 stores what step 102 would have: `sceScfGetLanguage()` mapped as
  the screen maps its cursor (1 EN to 2, 2 FR to 3, 4 DE to 4, 5 IT to 5,
  3 ES to 6, anything else the default item, English, 2;
  `ico_scf_to_game_language`), marks the sign done and goes on to step 190;
- step 200 stores what step 201 would have: `[video] video_mode`
  (`ico_sysconf_video_mode`), else the value in force (the screen's default
  item, 50 Hz), calling `gsResetFunc` when it changes, and goes on to
  step 202;
- step 96 (the card's system file was read) uses the card's `cameraMove`
  and `palMode` unless `[game] language` names a language or
  `[video] video_mode` is set; then the config's value wins. The card file
  is written with the in-memory values by `product_write` at the game's
  next system save, as on the PS2.

The memory card check, its retries and the "no memory card" sign are
unchanged.

`sceScfGetTimeZone` and `sceScfGetSummerTime` return 0.

## Clock

`sceCdReadClock` (`port/data/cdvd_host.c`) calls `ico_clock_now`
(`port/platform/clock.c`), which returns the host's local time as the BCD
record libcdvd returns (`stat` 0, two-digit year), or 2002-01-01 00:00:00
when the clock is fixed. The game reads the clock in two places:
`mcMakeSerial` in `common/src/layout_action.c` packs year, day, hour, minute
and second into the serial stored in a save file, and `appendLogFile` in
`seki/src/GsBase.c` writes a line of it from the debug menu's "Save
Settings" (`gsb_SaveStageSettings`). A real clock therefore changes the
bytes of a save; a fixed clock keeps a trace's save hash reproducible.

The clock is fixed when, in this order:

1. `[dev] fixed_clock` (`fixed_clock=`) says so, true or false;
2. the build is headless (`ICO_HEADLESS`), or `headless` is true;
3. a trace is written because the config asks for one (`trace=` other than
   `0`, `none`, `false`). A `--trace` command-line option is not seen here;
4. otherwise the clock is real.

`ico_host_fixed_clock` computes this at ini load and exports
`ICO_FIXED_CLOCK`. `clock.c` reads that on first use and is fixed when it
is unset (the unit tests). `ico_clock_set_fixed` overrides it.

## EE timers

The game's EE timers are the counters at the start of the EE I/O page:
`T0_COUNT` at `ico_hw_eeio + 0x0000`, `T1_COUNT` at `+0x0800` (T2 `+0x1000`,
T3 `+0x1800`), with the mode word 0x10 above each (`port/compat/eeregs.h`,
`port/platform/hwregs.c`). `clock.c` advances them in simulated time from a
vsync hook (`ico_clock_timers_attach`, attached by `sceScfGetLanguage` on its
first call):

- a timer counts only while its mode has `CUE` (0x80) set, at the rate of
  its `CLKS` field: 0 bus clock (147.456 MHz), 1 bus clock / 16, 2 bus
  clock / 256, 3 horizontal blank (15625 Hz at 50 Hz, 15734 Hz at 59.94);
- counters are 16 bits; a wrap sets the overflow flag (mode `0x800`);
- the arithmetic is in integers over the vsync period (1/50 s, or
  1001/60000 s after `ico_clock_set_vsync_hz(60)`) with the remainder
  carried, so a run's values depend on the vsync count alone. T0 in the
  game's mode `0x82` gains exactly 11520 per PAL vsync (576 kHz / 50);
- `ico_clock_timers_step(frac)` advances by a fraction of a vsync (65536 is
  one); the hook steps a whole vsync, so a read mid-frame sees the value as
  of the last vsync;
- a store to a counter or mode word by the game is respected
  (`*T0_COUNT = 0` in `Main`).

The rates are those of the EE timer block as documented in the EE User's
Manual (not in this tree); `debug_Init`'s `T0_MODE = 0x82` is the only local
evidence for the mode the game uses.

### Who reads them

Nothing gameplay-visible depends on the timers:

| site | what | host build |
| --- | --- | --- |
| `common/src/main.c` `Main` | `*T0_COUNT = 0` once at the start | a store; nothing reads it back |
| `common/src/debug.c` `debug_Init`, `debug_BeginTimer`, `debug_GetTimerSec`, `debug_GetTimerCount`, `debug_CallbackGsFinish`, `debug_SetBar`, `debug_SetBar2`, `debug_ResetBar` | the profiler bars and on-screen timers of the debug menu | `debug_Init` and `debug_BeginTimer` start timers 0 and 1 and `debug_ResetBar` clears timer 0 each Main tick, but on the host the readers return -1 or store 0, and no DMA handler latches `drawTimerCount` |
| `fumi/src/fieldCollision.c` `ResetCollisionPC`, `DispCollisionPC` | `pcTime`, printed in the collision counters shown while paused with the debug font on | on the host they read 0 |

`drawTimerCount` and `pcTime` are only written and shown; no game logic
branches on them, so the host guards are correct whether or not the
registers move.

## Tests

`config_test` (CPU; ctest `config`) covers the reader (sections, dotted
names, quoted and escaped strings, arrays, comments); the writer (unchanged
text byte for byte, changed values, keys added to a section, to the top
level and as a new section, CRLF, a file without a final newline, escapes,
the atomic save, a failed save leaving the old file, saving twice changing
nothing, a hand-edited file's comments, unknown keys and input bindings
surviving); precedence (ini over toml over default, the layering of
`ico_ini_load`, the executable's ini only); the fixed-clock rule; the
language (names, locales, the `LC_ALL`/`LC_MESSAGES`/`LANG` order,
`[game] language` over the locale, an invalid value, the English fallback);
the BCD clock; and the timers (mode, rates, wrap and overflow flag, a store
by the game, the hook, 60 Hz, the half horizontal line carried,
determinism).

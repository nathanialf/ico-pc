# Configuration, language and clock (Phase 4F)

One settings file, `config.toml`, in the per-user folder; the `ico-pc.ini`
beside the executable stays as an override layer for the developer and test
keys and the disc image path. The system language for libscf and the clock
for `sceCdReadClock` and the EE timers are answered from it and from the host.

| file | what |
| --- | --- |
| `port/platform/host_config.c`, `.h` | the ini reader, the TOML-subset reader and writer (`ico_toml_*`), the pref folder, the ini-over-toml layering at load |
| `port/config/config.c`, `config.h` | `ico_config_get_*`, `ico_config_set_*`, `ico_config_save` |
| `port/config/sysconf.c`, `sysconf.h` | `sceScfGetLanguage`, `sceScfGetTimeZone`, `sceScfGetSummerTime` (replaces `port/null/scf_null.c`); since 6C the boot screens' choices (`ico_boot_*`, `[video] video_mode`, the language setter) |
| `port/platform/clock.c`, `clock.h` | the wall clock for `sceCdReadClock`, the EE timer counters |
| `port/config/test/config_test.c` | the tests (`config` in ctest) |

## Where the files are

- **config.toml:** `<pref folder>/config.toml`. In the window build the pref
  folder is `SDL_GetPrefPath("ico-pc", "ico-pc")` (Windows
  `%APPDATA%\ico-pc\ico-pc\`, Linux `~/.local/share/ico-pc/ico-pc/`); ico_pc
  is compiled with `ICO_HOST_SDL_PREFPATH` for this (`port/config/CMakeLists.txt`).
  The headless build, the unit tests and a failing `SDL_GetPrefPath` use the
  executable's folder. `ico_host_pref_dir` is the one place that decides.
  The memory card folder (`ico_host_saves_dir`) defaults to `<pref folder>/memcard`,
  so the window build's saves move with it; `[paths] saves` or `saves=` set it.
- **ico-pc.ini:** beside the executable, as before (docs/port/TESTING.md).
- **ico.o2r** (the extracted game data, docs/port/DATA.md): `<pref folder>/ico.o2r`,
  written on the first run; one beside the executable is also used when the
  pref folder has none. Headless, the pref folder is the executable's.
- Logs, the trace and the `dumps/` folder stay in the executable's folder.
- A relative path in either file (`iso`, `saves`, `pad_script`, `dump_dir`,
  `audio_dump`, `trace`) is taken from the executable's folder, not the pref
  folder.

## Precedence

`ico-pc.ini` beside the exe > `config.toml` > the built-in default.

For a key the ini has always had (table below) the ini wins when it has the
key with a non-empty value. `ico_ini_load` of the executable's own ini
(`ico_host_ini_path`) fills the keys the ini lacks from the toml, so
`main_host.c`, which only reads the ini, sees the layered result; the
true/false of a toml value reads as `1`/`0` there. Keys with no ini name
(`[video]`, `[game]`, `[input]`, `[gameplay]`, `audio.volume`, `version`) come
from the toml alone. `ico_config_get_*` applies the same order; `ico_config_set_*`
changes the toml copy only, so a key the ini overrides reads back as the ini's.
`ico_ini_load` of any other path is the plain ini.

The command-line options of `ico_pc` (`--iso`, `--ticks`, ...) are above both,
as before. Environment variables the other libraries read (`ICO_ISO`,
`ICO_AUDIO`, `ICO_AUDIO_DUMP`, `ICO_AUDIO_VOLUME`, `ICO_FIXED_CLOCK`,
`ICO_RD_DUMP_*`) are exported from the layered result at ini load.

## The key table

| `config.toml` | ini key | default | meaning |
| --- | --- | --- | --- |
| `version` | | `1` | file format version; a file with a larger one is read, its unknown keys kept |
| `[paths] iso` | `iso` | `""` | disc image |
| `[paths] saves` | `saves` | `<pref>/memcard` | memory card folder |
| `[video] preset` | | `"original"` | `"original"` or `"enhanced"`: docs/port/DISPLAY.md (renderer wave 7, R7a) |
| `[video] resolution` | | `"window"` | Enhanced: the scene's resolution, `"window"`, `"WxH"` or `"Nx"` (1..8) |
| `[video] aspect` | | `"4:3"` | Enhanced: `"4:3"`, `"16:10"`, `"16:9"`, `"auto"` (the window's, clamped to 4:3..16:9) |
| `[video] fullscreen` | | `false` | borderless fullscreen at the desktop resolution; Alt+Enter toggles at run time (not saved) |
| `[video] vsync` | | `true` | the swapchain's present mode (Vulkan FIFO, else MAILBOX or IMMEDIATE; D3D12 sync interval 1 or 0 with tearing). Since package P1, vsync on with a `framerate` other than `"original"` prefers MAILBOX where the surface offers it (no tearing, and a present never waits for the display: DISPLAY.md "Vsync and an uncapped frame rate") |
| `[video] texture_filter` | | `"original"` | Enhanced: `"original"`, `"trilinear"`, `"anisotropic"` (generated mips) |
| `[video] full_height` | | `false` | Enhanced: skip the reduction's vertical halving |
| `[video] framerate` | | `"uncapped"` | both presets: `"original"` (present once per tick), `"uncapped"` (present at the display's rate, interpolating between the last two ticks), or a number 30..1000 (at most that many presents a second). Until package F2 the Original preset forced `"original"`; now it keeps its PS2-exact tick pictures and presents the blended ones between them (renderer wave 7, R7b; RENDER_API.md section 20) |
| `[video] video_mode` | | `"pal50"` | Phase 6, 6C: the boot 50/60 Hz screen's choice, which ico-pc skips: `"pal50"` (`systemStatus[0]` = 1, the PAL game's default) or `"60hz"` (0). Absent: the game's own value (50 Hz, or the card's). An explicit value wins over the memory card's system file. The Settings menu sets it (docs/port/SETTINGS.md) |
| `[audio] enabled` | `audio` | `true` | `false`/`audio=0`: no audio device (the driver still runs) |
| `[audio] volume` | | `1.0` | exported as `ICO_AUDIO_VOLUME`; the SDL output scales its blocks by it, live (docs/port/AUDIO.md, "Output") |
| `[input]` | | | docs/port/INPUT.md (4C); since 6C the Settings menu's remap screen writes `[input.kb]`, `[input.mouse]`, `[input.pad]` and `mouse_sensitivity` |
| `[gameplay]` | | `false` | `stick_fix`, `yorda_safe`, `mirror`: docs/port/OPTIONS.md (6A; `mirror` is only the value before a run starts and at the title since R7c: the run's value comes from the New Game screen or the loaded slot); `developer_mode`: the debug menu and option table (R6a, docs/port/DEVELOPER_MODE.md); read through `ico_config_get_bool` on first use |
| `[mirror] slot_N`, `slot_N_sum` | | none | renderer wave 7, R7c: the mirror mode of the game last saved in slot N (`game.00N`, N 0..9; a bool) and that save's checksum (the uint32 the game writes after the block): written on each save, read on each load (`port/game/options.h` `ico_mirror_slot_*`; docs/port/SAVES.md "Mirror mode"). An entry whose sum does not match the slot's save is ignored (Off) |
| `[game] mirror_fmv` | | `true` | renderer wave 7, R7c: in mirror mode the films are flipped too; `false` plays them unflipped (`rd_VideoSetMirrorOption`; the Settings menu's Gameplay page). Replaces the `ICO_MIRROR_FMV` environment variable as the player's switch; `movie.c` still reads that variable as a developer override (`0` turns the flip off) |
| `[game] language` | | `"auto"` | `"auto"`, `"en"`, `"fr"`, `"de"`, `"it"`, `"es"`: the game's language since 6C (the boot language screen is skipped; "Language" below); the Settings menu sets it |
| `[game] classic_menu_text` | | `false` | package P3: `true` draws the game's menu text from the PS2's pre-rendered textures; `false` (the default) draws it with the port font, Arimo, at the textures' places and sizes (docs/port/UI.md, "Menu text"). Settings > Display, "Menu text: Port font / Classic"; `ico_opt_classic_menu_text` (`port/game/options.h`) |
| `[game] circle_back` | | `true` | package Q2: Circle (gamepad B) is an alias of Triangle's back action in the game's own menus (the Options screen, the pause menu, the vibration screen, the memory card screens; not the Button Configuration or Brightness screens); `false` is the PS2's behaviour, Triangle alone. The port's own screens take Circle either way. Settings > Controls, "Circle goes back"; `ico_opt_circle_back` (`port/game/options.h`), `lt_ext_BackButtons` (`port/ui/layout_ext.h`); docs/port/SETTINGS.md "Circle goes back" |
| `[game] achievements` | | `true` | Phase 6, 6E: `false` turns the achievement popups off; unlocks are still recorded in `<pref>/achievements.toml` (docs/port/ACHIEVEMENTS.md) |
| `[dev] ticks` | `ticks` | none | exit after N Main ticks |
| `[dev] watchdog` | `watchdog` | `30` | seconds, 0 off |
| `[dev] trace` | `trace` | on in the headless build, off in the window build | `true`/`1`: `logs/trace-<time>.txt`; a path writes there; `false`/`0`/`none`: no trace. The window build writes one only when asked (package F2: a player's `logs/` no longer grows by a trace per run); `logs/ico-pc.log` is always written |
| `[dev] dump_every`, `dump_dir` | `dump_every`, `dump_dir` | off, `dumps` | rd frame dumps (window build) |
| `[dev] dump_interp` | `dump_interp` | `false` | renderer wave 7 (R7b, a key since R7d): with `dump_every`, each dump also gets the frame interpolated half way from the one before, `rd-NNNNN-i50.rddump` (RENDER_API.md section 20). `port/platform/host_config.c` `export_dump_keys` hands it to the renderer as `ICO_RD_DUMP_INTERP` (`1` for `1`, `true`, `on`, `yes`; else `0`), set whenever `dump_every` is, so this key decides and a value of that variable in the shell no longer does; the variable is only the internal hand-over, like `ICO_RD_DUMP_EVERY` and `ICO_RD_DUMP_DIR` |
| `[dev] dump_from` | `dump_from` | `0` | package S2: with `dump_every`, no frame numbered below this is dumped (the frame number is about the Main tick; a session's stage changes are in its log), so a run can dump a late stretch densely without the frames before it. Handed to the renderer as `ICO_RD_DUMP_FROM` by `export_dump_keys`, like `dump_interp` |
| `[dev] perf_log` | | `false` | package P1: the window build writes one CSV line per renderer replay into `logs/ico-pc-perf.csv` beside `ico-pc.log` (the CPU phases, the RHI counts and the GPU times of RENDER_API.md section 22, rd.h `RdPerfRecord`); read by `port/platform/window_host.c` through `ico_config_get_bool` (config.toml only, no ini key). The 10 s `window:` lines carry the same figures averaged with or without it. Since package S2 each line ends with `start_ms` (the replay's start on rd's clock), `alpha` (the present's alpha, -1 for a replay that is not a present between ticks) and `first_of_tick` |
| `[dev] slow_step_ms` | | `8` | package Q1: the window build logs a `window: slow step` line for a simulation step (from one pace to the next: `ico_host_step`, the trace line, the pad recording, the log flush and `ico_window_pump`) longer than this many ms, with where its time went (vsync callbacks, audio, game threads and their switches, achievements, pump, other) and what ran (disc reads, texture decodes, pipelines created, memory card busy, stage, loading); at most 5 lines a stats block, the block's first `window:` line counts them all. `0` turns it off. Read by `port/platform/window_host.c` through `ico_config_get_float` (config.toml only); RENDER_API.md section 22 |
| `[dev] input_record` | `input_record` | `true` in the window build, `false` headless | package Q1: the pad recording, what the game read from the pad each Main tick as a pad script (`port/input/input_record.h`): `true`/`1` writes `logs/input-<time>.txt`, a path writes there (the exe's folder's when relative), `false`/`0`/`none` turns it off. Its header names the build and the config values the simulation depends on; `pad_script=` that file in the headless build replays the session (docs/port/TESTING.md "Reporting a visual bug") |
| `[dev] audio_dump` | `audio_dump` | none | WAV of the mixed audio; `1` is `logs/audio.wav` |
| `[dev] pad_script` | `pad_script` | none (headless build: `pad-script.txt` beside the exe if present) | scripted pad, replacing the live controller. The window build loads one only when this key names it (package F2: a stray `pad-script.txt` beside a player's exe is ignored) |
| `[dev] verify` | `verify` | `true` | `false`/`0`: skip the disc image SHA-1 when `use_iso` is on; the first-run extraction always verifies |
| `[dev] use_iso` | `use_iso` | `true` headless, `false` window build | `true`: mount the disc image directly (dev mode). `false`: mount the extracted `ico.o2r` (per-user folder, else beside the exe), extracting it from the image on the first run (docs/port/DATA.md, "Backend 2: the archive") |
| `[dev] headless` | `headless` | `false` | a run for traces and tests: fixes the clock (below). The headless build is headless regardless |
| `[dev] fixed_clock` | `fixed_clock` | see below | the disc clock is fixed or real |
| `[dev] start_stage` | `start_stage` | none | developer key (renderer wave 5): the stage Main starts in instead of stage 1 (boot, language, title), 1..105 (`stageData` order, e.g. 34 st13a ELEVATOR, 15 st09a WINDMILL, 37 st25a QUEEN); handed over as `ICO_START_STAGE` to `debug_TryToGetStartStage` (`common/src/debug.c`'s host branch; `port/null/debug_null.c` until R6a), the hook the development build's start-stage file fed (`common/src/main.c:147`). Skips the boot and title flow, so game flags are those of a fresh boot |
| `[dev] debug_option` | | `0` | developer key (renderer wave 6, R6a; no ini key): with `[gameplay] developer_mode` on and a value other than 0, `debug_VariableInit` loads `<pref>/dev/thisIsYourDebugOption`, the debug option table the Debug Mode page saves (TRIANGLE), in place of the development build's option file check (`debug_GetDebugOption`, `common/src/debug.c`); 0 keeps the retail values. `ico_opt_debug_option()` (docs/port/DEVELOPER_MODE.md) |
| `[dev] popup_test` | `popup_test` | off | developer key (Phase 6, 6B): `true`/`1`/`on`/`yes` queues the test popup at Main tick 100 and every 150 ticks after (window build; handed over as `ICO_UI_POPUP_TEST` to `port/ui/ui_host.c`; docs/port/UI.md, "Popups") |

`ico_config_save()` writes `version`, `[paths] iso`, `[video]`, `[audio]`
and `[game] language` when they are absent; the `[dev]` and `[input]` keys only
when set. Nothing calls it yet at startup (open item), so a first run does not
create the file; the Settings menu (6C) calls it (or `ico_video_save`) when a
screen is left after a change.

## The writer

`ico_toml_render(t, existing)` applies a table to a file's text and
`ico_toml_save(t, path)` writes it: to `path.tmp`, then `rename` over `path`
(Windows `MoveFileExA` with `MOVEFILE_REPLACE_EXISTING`), so a failed write
leaves the old file. Only the lines whose key is in the table and whose value
differs are rewritten (`key = value`; a trailing comment on such a line is
dropped); everything else is kept byte for byte: comments, unknown keys and
sections, multi-line values the reader ignores, CRLF line ends. A key the file
does not have goes after the last key of its section, a new section at the
end, a top-level key (`version`) after the existing top-level keys or before
the first section. Saving an unchanged table changes nothing. Strings are
written quoted with `\\` and `\"` escaped; bools, numbers and arrays as held.

## Language

`sceScfGetLanguage()` (libscf numbering 0 JP, 1 EN, 2 FR, 3 ES, 4 DE, 5 IT)
returns, once per run:

1. `[game] language` when it is one of `en fr de it es`;
2. else the host's locale: `SDL_GetPreferredLocales` in the window build (the
   first preferred locale the game has a language for), else `LC_ALL`,
   `LC_MESSAGES`, `LANG` (not `C` or `POSIX`) in the headless build and when
   SDL has none;
3. else English (a locale in a language the game lacks, such as `ja` or `pt`,
   is English).

It logs `scf: language N from ...` once.

The PS2 console language only preselects the cursor of the boot language
screen (`common/src/kanbanBoot.c` step 101: item 26 for English, 27 French, 28
German, 29 Italian, 30 Spanish); the player still confirms the sign, and the
game then stores the choice in the global `NonLinearCameraMove` (misnamed:
2 EN, 3 FR, 4 DE, 5 IT, 6 ES, `kanbanBoot.c` step 102).

Since Phase 6 (6C) ico-pc skips that screen and the 50/60 Hz one
(`kanbanBoot.c` under `ICO_HOST`):

- step 101 stores what step 102 would have: `sceScfGetLanguage()` mapped
  as the screen maps its cursor (libscf 1 EN -> 2, 2 FR -> 3, 4 DE -> 4,
  5 IT -> 5, 3 ES -> 6, anything else the default item, English, 2;
  `ico_scf_to_game_language`), marks the sign done and goes on to step 190
  as step 102 did;
- step 200 stores what step 201 would have: `[video] video_mode`
  (`ico_sysconf_video_mode`), else the value in force (the screen's
  default item, 50 Hz), with step 201's `gsResetFunc` when it changes, and
  goes on to step 202;
- step 96 (the card's system file was read): the card's `cameraMove` and
  `palMode` are used as before unless `[game] language` names a language
  or `[video] video_mode` is set: then the config's value wins (the
  Settings menu writes both). The card file is written with the in-memory
  values by `product_write` at the game's next system save, as before (the
  screens never wrote it themselves).

The memory card check, its retries and the "no memory card" sign are
unchanged. The boot is 40 Main ticks shorter with `pad-boot.txt`
(docs/port/UI.md, "Runs").

`sceScfGetTimeZone` and `sceScfGetSummerTime` return 0.

## Clock

`sceCdReadClock` (`port/data/cdvd_host.c`) calls `ico_clock_now`
(`port/platform/clock.c`): the host's local time as the BCD record libcdvd
returns (`stat` 0, two-digit year), or, when the clock is fixed,
2002-01-01 00:00:00 (what it always reported before this package).
The game reads it in two places: `common/src/layout_action.c` `mcMakeSerial`
packs year, day, hour, minute and second into the save serial that goes into
the save file, and `seki/src/GsBase.c` `appendLogFile` writes a line of it
from the debug menu's "Save Settings" (`gsb_SaveStageSettings`, an editor
item). A real clock therefore changes the bytes of a save made in a run; a
fixed clock keeps the trace's save hash reproducible.

The clock is fixed when, in this order:

1. `[dev] fixed_clock` / `fixed_clock=` says so (true or false);
2. the build is headless (`ICO_HEADLESS`), or `headless` is true;
3. a trace is written to a path given in the config (`trace=` other than `0`,
   `none`, `false`; `1` counts). The default trace of an unconfigured user run does
   not count, and a `--trace` command-line option is not seen here;
4. otherwise real time.

`ico_host_fixed_clock` computes this at ini load and exports `ICO_FIXED_CLOCK`;
`clock.c` reads it on first use and is fixed when it is unset (tests,
`vfs_test`). `ico_clock_set_fixed` overrides.

## EE timers

The game's EE timers are the counters at the start of the EE I/O page,
`T0_COUNT` at `ico_hw_eeio + 0x0000`, `T1_COUNT` at `+0x0800` (T2 `+0x1000`,
T3 `+0x1800`), with the mode word 0x10 above each (`port/compat/eeregs.h`,
`port/platform/hwregs.c`). Before this package they were plain memory.
`clock.c` now advances them, in simulated time, from a vsync hook
(`ico_clock_timers_attach`; `sceScfGetLanguage` attaches it on its first call):

- a timer counts only while its mode has `CUE` (0x80) set, at the rate of its
  `CLKS` field: 0 bus clock (147.456 MHz), 1 bus clock / 16, 2 bus clock / 256,
  3 horizontal blank (15625 Hz at 50 Hz, 15734 Hz at 59.94);
- 16 bits; a wrap sets the overflow flag (mode `0x800`);
- the arithmetic is integers over the vsync period (1/50 s; 1001/60000 s after
  `ico_clock_set_vsync_hz(60)`), the remainder carried, so a run's values
  depend on the vsync count alone: T0 in the game's mode `0x82` gains 11520 per
  PAL vsync (576 kHz / 50), exactly;
- `ico_clock_timers_step(frac)` advances by a fraction of a vsync (65536 is
  one), the way a caller inside a frame gives a sub-vsync estimate; the hook
  steps a whole vsync, so a read mid-frame sees the value as of the last vsync;
- a store to a counter (`*T0_COUNT = 0`, `main.c:139`) or to a mode word by the
  game is respected.

The clock rates are those of the EE timer block as documented in the EE
User's Manual (not in this tree; `debug.c`'s `T0_MODE = 0x82` is the only local
evidence for the mode the game uses).

### Who reads them

Nothing gameplay-visible depends on the timers. The readers:

| site | what | host build |
| --- | --- | --- |
| `common/src/main.c:139` | `*T0_COUNT = 0` once at the start of `Main` | a store; nothing reads the value back in a host build |
| `common/src/debug.c` `debug_Init` (`T0_MODE = T1_MODE = 0x82`), `debug_BeginTimer`, `debug_GetTimerSec/Count`, `debug_CallbackGsFinish` (`drawTimerCount`), `debug_SetBar/SetBar2` (`debugBars[].count`), `debug_ResetBar` | the profiler bars and the on-screen timers | compiled since R6a (docs/port/DEVELOPER_MODE.md): `debug_Init` and `debug_BeginTimer` start timers 0 and 1, `debug_ResetBar` clears timer 0 every Main tick; the `ICO_HOST` branches of `debug_GetTimerSec/Count`, `debug_CallbackGsFinish` and `debug_SetBar/SetBar2` still return -1 or store 0, and no DMA handler latches `drawTimerCount` (until R6a `port/null/debug_null.c` stubbed them) |
| `fumi/src/fieldCollision.c` `ResetCollisionPC`, `DispCollisionPC` | `pcTime`, printed in the collision counters shown while the game is paused with the debug font on | `#ifdef ICO_HOST` branches read 0 |

`drawTimerCount` and `pcTime` are written and shown only; no branch of game
logic reads them. The `ICO_HOST` guards in `ico2/` still return 0 and -1 and
do not read the registers; removing them is a change in `ico2/` and a later
package's. They stay correct whether or not the registers move.

## Tests

`config_test` (CPU, in ctest as `config`): the reader (sections, dotted
names, quoted and escaped strings, arrays, comments); the writer (unchanged text
byte for byte, changed values, keys added to a section, to the top level and as
a new section, CRLF, a file without a final newline, escapes, atomic save, a
failed save leaving the old file, saving twice changing nothing, a hand-edited
file's comments, unknown keys and 4C's bindings surviving); precedence (ini over
toml over default, the layering of `ico_ini_load`, the exe's ini only); the
fixed-clock rule; the language (names, locales, `LC_ALL`/`LC_MESSAGES`/`LANG`
order, `[game] language` over the locale, an invalid value, English
fallback); the BCD clock; the timers (mode, rates, wrap and overflow flag, a
store by the game, the hook, NTSC, the half horizontal line carried,
determinism).

The 4F game run is recorded in docs/port/TESTING.md ("Phase 4F").

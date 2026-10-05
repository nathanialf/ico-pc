# Configuration, language and clock (Phase 4F)

One settings file, `config.toml`, in the per-user folder; the `ico-pc.ini`
beside the executable stays as an override layer for the developer and test
keys and the disc image path. The system language for libscf and the clock
for `sceCdReadClock` and the EE timers are answered from it and from the host.

| file | what |
| --- | --- |
| `port/platform/host_config.c`, `.h` | the ini reader, the TOML-subset reader and writer (`ico_toml_*`), the pref folder, the ini-over-toml layering at load |
| `port/config/config.c`, `config.h` | `ico_config_get_*`, `ico_config_set_*`, `ico_config_save` |
| `port/config/sysconf.c`, `sysconf.h` | `sceScfGetLanguage`, `sceScfGetTimeZone`, `sceScfGetSummerTime` (replaces `port/null/scf_null.c`) |
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
| `[video] preset` | | `"original"` | Phase 6 placeholder |
| `[video] vsync` | | `true` | Phase 6 placeholder (the window build always vsyncs today) |
| `[video] fullscreen` | | `false` | Phase 6 placeholder (F11 toggles at run time) |
| `[audio] enabled` | `audio` | `true` | `false`/`audio=0`: no audio device (the driver still runs) |
| `[audio] volume` | | `1.0` | exported as `ICO_AUDIO_VOLUME`; the SDL output does not apply it yet (open item) |
| `[input]`, `[gameplay]` | | | docs/port/INPUT.md (4C), untouched |
| `[game] language` | | `"auto"` | `"auto"`, `"en"`, `"fr"`, `"de"`, `"it"`, `"es"` |
| `[dev] ticks` | `ticks` | none | exit after N Main ticks |
| `[dev] watchdog` | `watchdog` | `30` | seconds, 0 off |
| `[dev] trace` | `trace` | on | `false`/`0`/`none`: no trace; a path writes there |
| `[dev] dump_every`, `dump_dir` | `dump_every`, `dump_dir` | off, `dumps` | rd frame dumps (window build) |
| `[dev] audio_dump` | `audio_dump` | none | WAV of the mixed audio; `1` is `logs/audio.wav` |
| `[dev] pad_script` | `pad_script` | `pad-script.txt` if present | scripted pad |
| `[dev] verify` | `verify` | `true` | `false`/`0`: skip the disc image SHA-1 when `use_iso` is on; the first-run extraction always verifies |
| `[dev] use_iso` | `use_iso` | `true` headless, `false` window build | `true`: mount the disc image directly (dev mode). `false`: mount the extracted `ico.o2r` (per-user folder, else beside the exe), extracting it from the image on the first run (docs/port/DATA.md, "Backend 2: the archive") |
| `[dev] headless` | `headless` | `false` | a run for traces and tests: fixes the clock (below). The headless build is headless regardless |
| `[dev] fixed_clock` | `fixed_clock` | see below | the disc clock is fixed or real |

`ico_config_save()` writes `version`, `[paths] iso`, `[video]`, `[audio]`
and `[game] language` when they are absent; the `[dev]` and `[input]` keys only
when set. Nothing calls it yet at startup (open item), so a first run does not
create the file.

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
2 EN, 3 FR, 4 DE, 5 IT, 6 ES, `kanbanBoot.c` step 102). Phase 6 skips that
screen and uses this value directly. Until then a value in the config only
moves the cursor.

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
| `common/src/debug.c` `debug_Init` (`T0_MODE = T1_MODE = 0x82`), `debug_BeginTimer`, `debug_GetTimerSec/Count`, `debug_CallbackGsFinish` (`drawTimerCount`), `debug_SetBar/SetBar2` (`debugBars[].count`), `debug_ResetBar` | the profiler bars and the on-screen timers | `debug.c` is not compiled; `port/null/debug_null.c` stubs the functions it needs |
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

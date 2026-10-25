# Configuration: config.toml and ico-pc.ini

The port's settings live in two files: `config.toml` in the [user folder](ARCHITECTURE.md#terms),
which the Settings menu reads and writes, and `ico-pc.ini` beside the executable, an override layer
for a few keys and the home of the start-up switches. This document lists every section and key
with its type, its default and the code that reads it, the order in which the layers win, when the
files are written, and how libscf's system configuration is answered from them.

## Files

| path | what it holds |
| --- | --- |
| `port/config/config.h`, `config.c` | the settings store over both files: the typed getters and setters, the save, the first-run template (`first_run_text`), `retired_keys` |
| `port/config/sysconf.h`, `sysconf.c` | libscf (`sceScfGetLanguage`, `sceScfGetTimeZone`, `sceScfGetSummerTime`) and the boot screens' language and video mode |
| `port/platform/host_config.h`, `host_config.c` | the ini reader, `ini_map` (the toml keys the ini may override), the TOML reader and writer, the user folder, portable mode, the card folders |
| `port/game/video_options.h` | the `[video]` key table and their reader |
| `port/game/options.h` | the `[gameplay]`, `[game] circle_back`, `[ui] language`, `[audio] output`, `[mirror]` and `[dev] debug_option` keys |
| `port/input/input_config.c`, `port/input/input.h` | the `[input]` keys and the bindings |
| `port/game/photo_mode.h` | `[photo]` |
| `port/game/appearance.h` | `[characters]` |
| `port/game/achievements.h` | `[game] achievements` and `achievements.toml` |
| `port/render/texpack.h` | what the texture pack keys of `[video]` control |
| `port/platform/main_host.c` | the start-up keys of `ico-pc.ini` and the command line |
| `port/platform/window_host.c` | `[video] backend`, `[dev] perf_log` and `slow_step_ms` |
| `port/game/credits.c` | `[dev] unlock_credits` |
| `port/platform/android/android_paths.h` | the Android build's file layout |
| `tools/package_docs_lib.sh` | `pkg_write_ini`: the `ico-pc.ini` the packages ship |
| `port/config/test/config_test.c` | the `config` test |

## The two files and who wins

`config.toml` is `<user folder>/config.toml`; `ico-pc.ini` is beside the executable on Windows and
Linux and in the user folder for the macOS app bundle (`port/config/config.c` (`ensure`);
`port/platform/host_config.c` (`ico_host_ini_path`)). The values are read on first use and kept in
memory (`port/config/config.h`).

A reader asks `ico_config_get_string`, `_bool`, `_int` or `_float` with a `section.key` path and a
default. The answer is the ini's value when the path is one of the keys the ini may override and
the ini has it non-empty, else `config.toml`'s, else the caller's default (`port/config/config.c`
(`ini_value`, `ico_config_get_string`)). The keys the ini may override, and their ini names, are the
`ini_map` table (`port/platform/host_config.c` (`ini_map`, `ico_config_ini_key`)):

| config.toml | ico-pc.ini |
| --- | --- |
| `[paths] iso`, `saves`, `saves2`, `keep_image` | `iso`, `saves`, `saves2`, `keep_image` |
| `[audio] enabled` | `audio` |
| `[video] backend` | `backend` |
| `[dev] ticks`, `watchdog`, `watchdog_pump`, `trace`, `dump_every`, `dump_dir`, `dump_interp`, `dump_from`, `audio_dump`, `audio_sync`, `pad_script`, `input_record`, `verify`, `headless`, `fixed_clock`, `use_iso`, `write_config`, `start_stage`, `switch_to`, `switch_at`, `unlock_credits`, `popup_test` | the same names |

The start-up code reads the ini the other way round: `ico_ini_load` loads the ini and fills its
missing keys from those `config.toml` entries (a `true` or `false` there reads as `1` or `0`), then
exports the values other libraries take from the environment (`ICO_AUDIO`, `ICO_AUDIO_DUMP`,
`ICO_AUDIO_SYNC`,
`ICO_AUDIO_VOLUME`, `ICO_AUDIO_MUSIC`, `ICO_AUDIO_EFFECTS`, `ICO_AUDIO_DEVICE`,
`ICO_FIXED_CLOCK`, `ICO_RD_DUMP_*`, `ICO_START_STAGE`, `ICO_SWITCH_TO`, `ICO_SWITCH_AT`,
`ICO_UI_POPUP_TEST`) (`port/platform/host_config.c` (`ico_ini_load_layered`, `ico_ini_export`)).
The command line, when there is one, wins over both: `--iso`, `--pad-script`, `--trace`, `--ticks`
and `--no-verify` (`port/platform/main_host.c`, header comment, the argument loop in
`host_main`). The disc image also has `$ICO_ISO` and the fixed places of `find_iso`
([PLATFORM.md](PLATFORM.md)).

The input layer reads `config.toml` itself, at start, through the TOML reader rather than the
store: `[input]` and `[gameplay] stick_fix` (`port/input/input_sdl.c` (`ico_input_sdl_init`);
`port/input/input_config.c` (`ico_input_apply_toml`)). The achievements keep their own file,
`<user folder>/achievements.toml` (`port/game/achievements.h` (`ico_ach_init`)).

## config.toml

### The format

A small TOML subset: `[section]` or `[a.b]` headers, `key = value` lines, `"basic"` and
`'literal'` strings, `true`/`false`, numbers and one-line arrays (kept as written for the caller to
split). A key's path is `section.key`, so `[input] kb.cross` and `[input.kb] cross` are the same
entry. `#` outside quotes starts a comment and a repeated key keeps its last line; multi-line
values, inline tables, dates, arrays of tables and escapes other than `\\` and `\"` are ignored
(`port/platform/host_config.h`, the `IcoToml` comment). A bool reads `true`, `false`, `1` or `0`;
anything malformed gives the caller's default (`ico_toml_get_bool`).

The top-level `version` is `1` (`port/config/config.h` (`ICO_CONFIG_VERSION`)); a file with a
larger one is read, its unknown keys kept, and a log line says so (`port/config/config.c`
(`ensure`)). Keys in `retired_keys` (`[game] classic_menu_text`, `[game] port_font`,
`[dev] hand_probe`, `[video] effect_glow_size`) are kept in the file, read by nothing and logged once a load
(`port/config/config.c` (`retired_keys`, `log_retired`)).

### When the file is written

The game never rewrites the file wholesale. `ico_config_save` renders the keys held in memory over
the file's own text: a line whose key changed is rewritten as `key = value`, everything else
(comments, unknown keys, array layout, the lines the reader ignores) is kept byte for byte, new keys
go at the end of their section, and the result is written to `config.toml.tmp` and renamed over
the file (`port/platform/host_config.h` (`ico_toml_render`, `ico_toml_save`)). The save also adds
`version`, `[paths] iso`, `[video] preset`, `vsync`, `window_mode`, `fullscreen`, `[audio]
enabled`, `volume`, `music`, `effects`, `output`, `device` and `[game] language` at their defaults
when the file lacks them (`port/config/config.c` (`ico_config_save`)).

It is written at these moments only:

- on leaving a Settings page, or before an action that leaves the menu (Extras > Credits, the
  model viewer, Quit): `ui_settings_save` writes `[video]` through `ico_video_save`, the bindings
  through `ico_input_write_bindings`, and anything else changed through `ico_config_save`
  (`port/ui/settings.c` (`ui_settings_save`, `leaveTo`, `requestQuit`));
- when the window is closed or the title's Quit row is taken with a change still pending
  (`port/ui/settings.c` (`ui_settings_save_on_quit`); `port/platform/main_host.c`, the host loop);
- when the game's own Stereo/Mono row changes with `[audio] output` set to stereo or mono
  (`port/game/options.c` (`ico_opt_output_toggled`));
- when a save is written, for the slot's `[mirror]` entry (`port/game/options.c`
  (`ico_mirror_slot_saved`));
- in photo mode, for `hide_ui` and `film_effect` (`port/game/photo_mode.c`);
- on Android, when a graphics driver is chosen, fails or is removed
  (`port/platform/android/gpu_driver_android.c`); with unsaved changes, on Android when the app is
  terminated and on iOS when it goes to the background (`port/config/config.h` (`ico_config_dirty`);
  `port/platform/window_lifecycle.h` (`ICO_LC_CONFIG_SAVE`); `port/platform/window_host.c`);
- on the first run, when no file exists: `ico_config_write_first_run` writes the commented template
  `first_run_text` (every key `ico_config_save` adds and more, at their defaults) and never
  replaces an existing file. The [window build](ARCHITECTURE.md#terms) does it by default, the
  [headless build](ARCHITECTURE.md#terms) only with `write_config=1`
  (`port/config/config.c` (`ico_config_write_first_run`); `port/platform/main_host.c`
  (`write_config`)).

### [paths]

| key | type | default | read by |
| --- | --- | --- | --- |
| `iso` | string | `""` | the disc image (any of the seven discs: the European, Japanese, Korean, North American or Asian disc, the August 2001 preview or the June 2001 magazine demo); empty asks on the first start (`port/platform/main_host.c` (`find_iso`)) |
| `saves` | string | `<user folder>/memcard` | the card in port 0 ([SAVES.md](SAVES.md); `host_config.c`, `ico_host_saves_dir`); on the August 2001 preview the folder beside it with `-preview` appended |
| `saves2` | string | absent: no card | the card in port 1 (`host_config.c` (`ico_host_saves2_dir`)) |
| `keep_image` | bool | `false` | Android: keep the first start's copy of the disc image (`main_host.c` (`android_keep_image`)) |

### [video]

Read by `port/game/video_options.c` into `IcoVideoOptions` the first time a value is asked for, and
written back by `ico_video_save` (`port/game/video_options.h`, header comment). The meaning of each
display option, the [preset](ARCHITECTURE.md#terms) rule and the pacing belong to
[DISPLAY.md](DISPLAY.md).

| key | type | default | values |
| --- | --- | --- | --- |
| `preset` | string | `"original"` | `original`, `enhanced`, `custom` |
| `resolution` | string | `"window"` (`"auto"` on Android and iOS) | `window`, `WxH`, `Nx` (N 1..16), `auto` |
| `aspect` | string | `"4:3"` | `4:3`, `16:10`, `16:9`, `21:9`, `32:9`, `48:9`, `auto` |
| `window_mode` | string | `"windowed"` | `windowed`, `borderless`, `fullscreen` (exclusive at the desktop's mode; Windowed and Borderless are composed windows: [DISPLAY.md](DISPLAY.md), "The window modes") |
| `fullscreen` | bool | `false` | read when `window_mode` is absent |
| `vsync` | bool | `true` | |
| `texture_filter` | string | `"original"` | `original`, `trilinear`, `anisotropic` |
| `full_height` | bool | `false` | |
| `full_pixel` | bool | from the preset | true when the rows add up to Enhanced |
| `framerate` | string or int | `"uncapped"` (60 on Android and iOS) | `original`, `uncapped`, 30..1000 |
| `anti_aliasing` | string | `"off"` | `off`, `ssaa2`, `ssaa4`, `msaa2`, `msaa4`, `msaa8` |
| `crt` | bool | `false` | the CRT filter |
| `crt_mode` | string | `"consumer"` | `scanlines`, `consumer`, `trinitron`, `pvm`, `shadow` |
| `crt_strength` | float | `1.0` | 0..1 |
| `crt_scanlines`, `crt_mask`, `crt_halation`, `crt_bloom`, `crt_curvature` | float | `-1` | file only; below 0 is the mode's value |
| `texture_pack` | bool | `true` | load an installed texture pack (the disc's serial folder: SCES-50760 or SCPS-11003, [PACKS.md](PACKS.md)) |
| `dump_textures` | bool | `false` | write each texture under its PCSX2 name |
| `model_pack` | bool | `true` | load an installed model pack |
| `dump_models` | bool | `false` | save each model part as glTF |
| `texture_pack_budget_mb` | int | `2048` | file only; graphics memory for replacements, 128..65536 |
| `texture_pack_precache` | bool | `true` | file only; read the whole pack into memory at start |
| `texture_pack_cache_mb` | int | `0` | file only; memory for that read, 0 (half the computer's) or 128..65536 |
| `effect_glow`, `effect_depth_of_field`, `effect_softening`, `effect_motion_blur`, `effect_fog`, `effect_cinematic_bars` | bool | `true` | the game's own picture effects |
| `effect_motion_blur_mode` | string | `"original"` | Motion blur's way while `effect_motion_blur` is on: `"original"` (the game's sprite reading the DISPLAY the tick started from) or `"smooth"` (`RdSettings.motionBlurSmooth`: an accumulator of every present's scene, [RENDER.md](RENDER.md#the-motion-blur-under-smooth)); anything else is `"original"` with a `video:` stderr line; written with every save, never by itself |
| `effect_water_sharp` | bool | `false` | Water reflections: `true` (Sharp) draws the reflections in water and the queen barrier's picture at the scene's scale, `false` (Original) at their GS size ([RENDER.md](RENDER.md#water-and-render-to-texture-blocks)) |
| `effects_depth` | bool | `true` | file only; the scene's depth for an effects program such as ReShade |
| `video_mode` | string | absent: 60 Hz | `pal50` or `60hz`, the boot screen's choice (`port/config/sysconf.c`); ignored on every disc but the European one, which have no 50 Hz mode (`ico_boot_video_mode` returns 60 Hz and logs it once) |
| `backend` | string | `"vulkan"` | `vulkan` or `d3d12`, the window build's renderer; when the key is empty, a run restarted after a graphics driver crash takes the backend its retry step names (`port/platform/window_host.c` (`ico_window_open`); `port/platform/startup_retry.h`) |
| `gpu_driver` | string | `""` | Android: the folder of an added graphics driver, used from the next start |
| `gpu_driver_failed` | string | `""` | Android: the driver that did not start last time, written by the game |

(`port/game/video_options.h`, the key table; `port/game/video_options.c` (`read_config`);
`port/platform/android/gpu_driver_android.c`, `KEY_DRIVER`, `KEY_FAILED`.) The defaults above are
each key's own; with `preset` absent or `original` (the default), `resolution`, `aspect`,
`texture_filter` and `full_height` take the PS2's values (1x, 4:3, original, false) whatever the
file says (`port/game/video_options.c`, `read_config`; [DISPLAY.md](DISPLAY.md)). The texture pack
keys are handed to the pack loader as its budget, cache size and precache switch
(`port/render/texpack.h` (`budgetMb`, `cacheMb`, `precache`)); [PACKS.md](PACKS.md) has the loader.

### [audio]

| key | type | default | read by |
| --- | --- | --- | --- |
| `enabled` | bool | `true` | the audio device; `false` keeps the sound driver running with no output (`ICO_AUDIO`) |
| `volume`, `music`, `effects` | float | `1.0` | 0.0..1.0, the gains (`ICO_AUDIO_VOLUME`, `ICO_AUDIO_MUSIC`, `ICO_AUDIO_EFFECTS`; `port/audio/audio_host.c`) |
| `output` | string | `"auto"` | `auto` leaves the PS2's stereo/mono choice to the game and its card; `stereo` or `mono` win over it (`port/game/options.h` (`ico_opt_output_mode`)) |
| `device` | string | `""` | an output device's name; empty is the system's default (`ICO_AUDIO_DEVICE`; `port/ui/settings.c` (`stepDevice`)) |

The gains and the device are read once at start from the environment `ico_ini_export` sets; the
Settings menu changes the running values and the file (`port/platform/host_config.c`
(`ico_ini_export`)). [AUDIO.md](AUDIO.md) has the mixer.

### [game]

| key | type | default | read by |
| --- | --- | --- | --- |
| `language` | string | `"auto"` | `en`, `fr`, `de`, `it`, `es`; `auto` follows the system ([System configuration](#system-configuration-libscf)) |
| `circle_back` | bool | `true` | Circle backs out of the game's menus as Triangle does (`port/game/options.h` (`ico_opt_circle_back`)) |
| `achievements` | bool | `true` | the achievement popups (`port/game/achievements.c` (`ico_ach_init`)) |
| `disc` | string | `"auto"` | `pal` (the European disc, SCES-50760), `jpn` (the Japanese disc, SCPS-11003), `kor` (the Korean disc, SCPS-56001), `ntsc` (the North American disc, SCUS-97113), `aug6` (the August 2001 preview), `jun26` (the June 2001 magazine demo, SCUS-97158) or `cn` (the Asian disc, SCPS-55001) pins the disc a start plays when more than one is set up; `auto` lets the disc image given and the game data there decide; any other value is `auto`, logged once (`port/config/config.c` (`ico_config_disc_pin`); the rules: [PLATFORM.md](PLATFORM.md#the-disc-a-start-plays)) |
| `last_disc` | string | absent | written by the program, not in the template: one of those slugs, the disc a start last played, the choice's fallback when more than one disc's data is there (`ico_config_last_disc`, `ico_config_set_last_disc`; `main_host.c` (`select_disc`)) |

### [ui]

| key | type | default | read by |
| --- | --- | --- | --- |
| `language` | string | `"auto"` | the language of the port's own text (its menus, photo mode's help, the messages): `en`, `fr`, `de`, `it`, `es`, `ja`, `ko`, `zh`; `auto` follows the disc: its region row's `ui_lang_default` (Japanese on the Japanese disc, Korean on the Korean disc, Chinese on the Asian disc), the game's own language on the European disc; any other word is `auto`, logged (`port/game/options.h` (`ico_opt_ui_language`); `port/ui/ui_lang.h` (`ui_settings_resolve_language`)). The game's own text is always the disc's |

### [gameplay]

Read once by `port/game/options.c` the first time each is asked for; every default is the
original game's behaviour (`port/game/options.h`, header comment).
[GAME_FEATURES.md](GAME_FEATURES.md) has what each does to the game.

| key | type | default | meaning |
| --- | --- | --- | --- |
| `stick_fix` | bool | `false` | the stick fix (`ico_opt_stick_fix_default`); the input layer reads it too |
| `yorda_safe` | bool | `false` | the shadows never take Yorda |
| `door_fix` | bool | `false` | an arrival holding hands does not send the pair back through the door |
| `mirror` | bool | `false` | mirrored play, the value a run starts from until New Game or a load |
| `natural_chains` | bool | `false` | a chain let down keeps its links' size |
| `skip_opening` | bool | `false` | the boot starts at the title screen, without the Sony presents sign and the opening scene (from the next start); a pad recording's header records it |
| `hold_type` | string | `"hold"` | `"hold"`: keep R1 held to hold Yorda's hand (the game's Hold type A); `"toggle"`: press R1 once to take it and again to let go (B). Wins over the memory card's `controlType`; another word reads as `"hold"` and is logged once. A pad recording's header records it |
| `developer_mode` | bool | `false` | the development build's debug menu and options |

### [variants]

Extras > Game variants ([UI.md](UI.md#extras--game-variants), [OPTIONS.md](../OPTIONS.md#game-variants)).
Read once by `port/game/variants.c` the first time each is asked for (`ico_variant_*`); every default is
the game as the disc ships it. All eight are in a pad recording's header (`record_keys`). A word
or number that is no value reads as the default and is logged once.

| key | type | default | meaning |
| --- | --- | --- | --- |
| `shade_toughness` | string | `"original"` | `"original"` (the disc's table), `"american"` (every Shadow as tough as on the North American disc, and no tougher on later playthroughs) or `"no_plus"` (the disc's Shadows, no tougher on later playthroughs) |
| `one_hit` | bool | `false` | a swing hurts a Shadow only once (`debug_one_hit_only`); suspends achievements |
| `never_dodge` | bool | `false` | the Shadows never dodge (`debug_ignore_dodge`); suspends achievements |
| `aggression` | string | `"own"` | `"own"` (each Shadow's own), `"bold_patient"`, `"bold_quick"`, `"wary_patient"` or `"wary_quick"` (`debug_enemy_battle_type`) |
| `no_hints` | bool | `false` | no hints, as on the North American disc |
| `barrier_hits` | int | absent (not in the template) | `5` or `7`: the hits the Queen's barrier takes; absent, the disc's own (`7` on `jun26`, else `5`); the Settings row writes it |
| `no_cutscene_look` | bool | `false` | no camera look-around in cutscenes |
| `ignore_cutscene_cameras` | bool | `false` | the cutscenes' cameras are ignored (`debug_ignore_demo_camera`); suspends achievements |

The Debug menu row has no key: its press marks the Debug menu used until the options are reloaded,
which suspends achievements too (`ico_variants_suspend_achievements`, read by `gamestate.c` beside
developer mode), opens the game's debug menu and closes Options. Where the game reads each key, and
what it changes, is in [GAME_FEATURES.md](GAME_FEATURES.md#game-variants); a change on the page
takes effect at once (`ico_variants_apply`).

### [mirror]

`slot_N` (bool) and `slot_N_sum` (int, the save's game-block checksum) for save file N
(`game.00N`, 0..9): the mirror flag of each save, kept here and never in the card's files. A slot
whose checksum differs reads Off (`port/game/options.h` (`ico_mirror_slot_saved`,
`ico_mirror_slot_loaded`)).

Each disc has its own keys, since its saves are another card directory: `slot_N` and `slot_N_sum`
are the European disc's (no prefix), `jpn_slot_N` and
`jpn_slot_N_sum` the Japanese disc's, `kor_slot_N` and `kor_slot_N_sum` the Korean disc's (and `ntsc_`, `aug6_`, `jun26_`, `cn_` for the other four: the North American disc and the preview keep separate keys although they share a save folder), chosen by the disc being played; a disc's write leaves the
other disc's keys as they are (`port/core/region.h` (`key_prefix`, `ico_region_key_prefixed`);
`port/game/options.c` (`slot_keys`); [SAVES.md](SAVES.md#the-seven-discs)).

```toml
[mirror]
slot_1 = true
slot_1_sum = 4660
jpn_slot_1 = false
jpn_slot_1_sum = 22136
```

`achievements.toml`, the other per-user file, keys its per-disc entries the same way:
`run.slot_N_*`, `run.jpn_slot_N_*`, `run.kor_slot_N_*` (and the other discs' prefixes) for each save slot's run state, `stats.stages` and
`stats.stages_jpn`, `stats.stages_kor` (and the other discs' suffixes) for the visited stages; the unlocks (`[unlocked.<id>]`) and the other counters
are one set for all discs (`port/game/achievements.c` (`run_key`, `write_file`, `read_file`);
[GAME_FEATURES.md](GAME_FEATURES.md)).

### [input]

The bindings and the input switches, applied over the defaults at start
(`port/input/input_config.c` (`apply_keys`); `port/input/bindings.c` (`ico_bindings_defaults`,
`ico_bindings_set`)):

| key | type | default | range |
| --- | --- | --- | --- |
| `keyboard`, `mouse`, `gamepad` | bool | `true` | the devices that drive the pad |
| `rumble` | bool | `true` | |
| `deadzone` | float | `0.12` | 0..0.9 |
| `walk_scale` | float | `0.5` | 0.05..1, the left stick while a walk key is down |
| `mouse_sensitivity` | float | `1.0` | 0.01..100 |
| `mouse_decay` | float | `0.80` | 0..0.99, the touch look pad's decay a vsync |
| `mouse_invert_y` | bool | `false` | |
| `mouse_camera` | bool | `true` | |
| `mouse_hold` | float | `0.75` | 0..10 seconds |
| `mouse_camera_speed` | float | `1.0` | 0.5..10 |
| `mouse_full_range` | bool | `false` | |
| `mouse_return` | bool | `true` | |
| `touch_mode` | string | `"auto"` | `off`, `auto`, `always` |
| `touch_size` | string | `"medium"` | `small`, `medium`, `large` |
| `touch_opacity` | number | `75` | 10..100 |

The bindings themselves are `kb.<target>`, `mouse.<target>` and `pad.<target>` for each pad target
(`cross`, `circle`, `lstick_up`, ...; `bindings.c` (`target_names`)), plus `kb.walk`; a value is
one name or an array of names from `port/input/keys.def`, the mouse buttons and the gamepad
sources. A key or value that does not parse is logged and skipped (`ico_bindings_set`). The writer
puts back only what differs from the defaults or is already in the file
(`port/input/input.h` (`ico_input_write_bindings`)). [INPUT.md](INPUT.md) has the bindings.

### [touch]

The touch overlay's layout edits, written only when the player edits (the Edit layout page,
[UI.md](UI.md#the-touch-layout-editor)); a config nobody edited has no `[touch]` key, only a
comment block in the first-run text (`port/config/config.c` (`first_run_text`)). Read at start by
`port/input/input_config.c` (`ico_input_apply_touch_toml`) and written by `ico_input_write_touch_edits`;
`ico-pc.ini` does not map these:

| key | type | default | meaning |
| --- | --- | --- | --- |
| `format` | number | unset | `2`: the lines are in the current grammar (`hidden` keeps a control working, `off` switches it off); the editor writes it with every layout. Without it (a file written before the key existed) `hidden` and `o0` are read as `off`, what they meant in such a file, and the editor's next save writes `format = 2` and `off` |
| `snap` | bool | `true` | Snap to grid: a dropped control's offsets round to the grid |
| `grid` | number | `4` | 2, 4 or 8, millimetres of the layout; any other value is logged and ignored |
| `full.<control>`, `split.<control>`, `tabletop.<control>` | string | unset | one control's place, size and opacity in one posture ([INPUT.md](INPUT.md#layout-edits)) |

The postures are `full`, `split` (a vertical fold) and `tabletop` (a horizontal fold, or the
buttons moved below a flat crease), written as one table each (`[touch.full]`), appended when a
key in it is first set. The controls are `dpad`, `cross`, `circle`, `square`, `triangle`, `r1`,
`l1`, `l2`, `r2`, `select`, `start`, `stick` and `look`. A value is whitespace-separated tokens:

| token | meaning |
| --- | --- |
| `l<n>` or `r<n>` | the control's centre (the look pad's edge) `n` millimetres from the left or right edge of the screen's safe area (`tabletop`: of the part below the fold); negative (`l-3`, down to -500) lies past that edge, under a camera hole or a rounded corner, and the control is kept inside the window |
| `t<n>` or `b<n>` | the same from the top or bottom edge |
| `s<scale>` | size times `scale`, 0.7 to 3.0 (the default size when absent); a control too large for the screen is drawn at the largest size that fits |
| `o<percent>` | opacity times the Touch opacity row's value, 0 to 100 (100 when absent); `o0` is read as `hidden` |
| `w<n> h<n>` | the look pad only: its size in millimetres, 20 to 500 (its default size when absent) |
| `hidden` | in place of `o`: the control is not drawn but still works; alone at its default place, or after its place and size |
| `off` | in place of `o`: the control is not drawn and does not work; alone or after its place and size, as `hidden` |
| `default` | alone: the control is not edited (what a reset control that is in the file becomes) |

Numbers are in layout millimetres, the unit the Touch size setting scales, so an arrangement keeps
its real distance from the corners on another phone. The look pad's offsets are its edges on the
anchor sides: `look = "r0 t0 w60 h30"` is the upper right corner. Every control can be `hidden` or
`off`, the D-pad and Start too. A Hidden or Off control that was moved is written with its
position and size before the word (`l4 t6 s1.00 hidden`), so showing it again puts it back. With
the D-pad, Cross or Circle off, a tap on a menu row clicks it, so the menus (and Options > Controls
> Touch screen > Reset) still work by touch ([INPUT.md](INPUT.md#layout-edits)). Example:

```toml
[touch]
format = 2
snap = true
grid = 4

[touch.full]
cross = "r20 b10.5 s2.50 o100"
dpad  = "off"
start = "r40 t3 s1.00 hidden"
l2    = "hidden"
look  = "l50 t0 w60 h30 o100"
```

A line that does not parse or is out of range is logged once as `touch: ignoring full.l2 = "..."
(why)` and ignored: the control stays at its default place. Deleting the tables or any line gives
the default back.

### [photo]

| key | type | default | meaning |
| --- | --- | --- | --- |
| `stick_speed` | float | `1.0` | every rate of the photo camera times this |
| `invert_y` | bool | `false` | the looking stick's up and down swapped |
| `hide_ui` | bool | `false` | the help panel hidden at enter; Square saves it |
| `film_effect` | int | `0` | the Film effect photo mode shows, 0 (off) to 4, or 5 (Deja vu, `ICO_PHOTO_FILM_DEJA`); Right cycles and saves it |
| `png_dir` | string | `"screenshots"` | the captures' folder, inside the user folder |

(`port/game/photo_mode.h`, header comment.)

### [characters]

One string per part: `ico_skin`, `ico_poncho_navy`, `ico_poncho_pink`, `ico_poncho_light`,
`ico_poncho_dark`, `ico_tunic`, `ico_shorts`, `yorda_skin`, `yorda_dress`. The value is
`"original"` (the default, also for an absent key or an unknown value, which is logged once), one
of the 24 palette names (`red` ... `black`), or for the skin parts `tone1` ... `tone12`
(`port/game/appearance.h`, header comment).

### [dev]

Developer keys, none in the template. The ones in `ini_map` may also be given in `ico-pc.ini`
under the same name (below). Read through the store:

| key | type | default | read by |
| --- | --- | --- | --- |
| `debug_option` | int | `0` | with developer mode, non-zero loads `<user folder>/dev/thisIsYourDebugOption` (`port/game/options.c` (`ico_opt_debug_option`)) |
| `unlock_credits` | bool | `false` | Extras > Credits unlocked (`port/game/credits.c`) |
| `perf_log` | bool | `false` | `ico-pc-perf.csv` in the logs folder, one line per [replayed](ARCHITECTURE.md#terms) frame (`port/platform/window_host.c` (`perf_csv_open`)) |
| `slow_step_ms` | float | `8` | the slow-step log threshold; 0 is off (`port/platform/window_host.c` (`ico_window_open`)) |
| `blend` | string | (none: `fixed`) | the A/B switches below, as their ini keys |
| `fog_path` | string | (none) | |
| `date_path` | string | (none) | |
| `store_plan` | bool | (the device profile's) | |
| `blur_fold` | bool | (the device profile's) | |
| `uint_view` | bool or `work` | (the device profile's) | |
| `alpha_plan` | bool | (the device profile's) | |
| `fx_plan` | bool | (the device profile's) | |
| `fx_dst_local` | bool | (the device profile's) | |
| `vu_planes` | bool | (the device profile's) | |
| `dynamic_state` | bool or `depth` | `true` (off on a tile-based GPU unless set) | |
| `gpu_checkpoints` | bool | `false` | |
| `gpu_robust` | bool | `false` | |
| `pass_split` | int | `0` | |
| `attachment_read` | bool | `false` | |
| `storage_pass_merge` | bool | `false` | |
| `vu_plain_only` | bool | (retired) | |
| `vu_extended` | bool | (retired) | |
| `vu_prepass` | bool | `true` (off on a tile-based GPU unless set) | |
| `crack_fill` | bool | `true` | |
| `perf_passes` | bool | `false` | |
| `perf_list_stamps` | bool | `true` | |
| `joystick_directinput` | bool | `true` | |
| `pipeline_set` | string | `full` | |
| `audio_sync` | bool | `false` | |

## ico-pc.ini

`key=value` lines; a line whose first non-blank character is `#` or `;` is a comment; keys and
values are trimmed and a value in double quotes loses them; a relative path is taken from the
executable's folder (`port/platform/host_config.h`, header comment). At most 32 keys are held
(`ICO_INI_MAX_KEYS`). The keys the program reads, directly (`ico_ini_get` in
`port/platform/host_config.c` and `port/platform/main_host.c`) or as `ini_map` overrides of the
store (`backend`, `unlock_credits`):

| key | effect |
| --- | --- |
| `iso` | the disc image; written back after the file dialog (`main_host.c` (`ico_ini_store`)) |
| `saves`, `saves2` | the card folders ([SAVES.md](SAVES.md)) |
| `keep_image` | Android: `1` keeps the first start's copy of the disc image |
| `portable` | `1` on, `0` off; read before the user folder is first used, ini only (`host_config.c` (`ico_host_portable_value`)) |
| `audio` | `0`: no audio device (exported as `ICO_AUDIO`) |
| `audio_dump` | a WAV of the mixed output; `1` is `logs/audio.wav` (`export_audio_keys`); the audio then renders on the game's clock ([AUDIO.md](AUDIO.md#the-outputs-clock)) |
| `backend` | `vulkan` or `d3d12` |
| `ticks` | exit after N [Main ticks](ARCHITECTURE.md#terms) |
| `watchdog` | seconds, default 30; 0 is off |
| `watchdog_pump` | seconds, default 600; 0 is no limit: the longest one wait the system holds (the window's event pump) may last before the watchdog ends the run; the time inside such a wait does not count towards `watchdog`; with `watchdog = 0`, before the game starts and in the background it ends nothing ([PLATFORM.md](PLATFORM.md#diagnostics)) |
| `trace` | `0` off, `1` or a path on; on by default only in the headless build |
| `pad_script` | the [pad script](ARCHITECTURE.md#terms) to run |
| `input_record` | the pad recording: `0`/`false`/`none` off, `1`/`true` on, or a path; on by default in the window build |
| `verify` | `0` skips the disc image's SHA-1 check when it is mounted directly |
| `use_iso` | `1` mounts the disc image instead of `ico.o2r`; default 1 headless, 0 in the window build |
| `write_config` | `1` writes the first-run `config.toml`; default 1 in the window build, 0 headless |
| `headless`, `fixed_clock` | the fixed clock: `fixed_clock` when present, else on for the headless build, `headless=1` or a trace to an explicit path (`ico_host_fixed_clock`); with it the audio also renders on the game's clock ([AUDIO.md](AUDIO.md#the-outputs-clock)) |
| `dump_every`, `dump_dir`, `dump_interp`, `dump_from` | [rd](ARCHITECTURE.md#terms) frame dumps every N frames into a folder beside the ini (`export_dump_keys`) |
| `start_stage`, `switch_to`, `switch_at` | the stage Main starts in; one forced stage change (exported) |
| `popup_test` | test popups from Main tick 100 (exported as `ICO_UI_POPUP_TEST`) |
| `blend` | the GS blend's path: `fixed`, the default (nothing set exports nothing), the GPU's fixed-function blender on every device; `attachment` or `storage` opts into that exact destination read where the device has it, kept whatever the start-up blend self-test finds; `exact` opts into the best one the device has (the attachment read, else the storage read), logged `rd: blend: [dev] blend = exact: the exact blend is an opt-in; attachment tried` (exported as `ICO_RD_BLEND`) ([RENDER.md](RENDER.md#destination-reads-and-blend-paths)) |
| `fog_path` | `inplace`, `copy` or `buffer`: the fog's depth path, tried first; `local`: the device profile's `fogLocal` on (the fog reads the depth in the scene pass by local read), where the device has local read; `inplace`, `copy` and `buffer` turn `fogLocal` off (exported as `ICO_RD_FOG_PATH`) ([RENDER.md](RENDER.md#the-fog-path-and-its-self-test), [RENDER.md](RENDER.md#the-device-profile)) |
| `date_path` | `stencil`: the device profile's `dateStencil` on (DATE through the stencil bit, by local read), where the device has local read; `snapshot`: off, the R8 DATE snapshot (exported as `ICO_RD_DATE_PATH`) ([RENDER.md](RENDER.md#the-device-profile)) |
| `store_plan` | `true` / `false`: the device profile's `storePlan` (store-op DONT_CARE for depth and targets nothing reads later) on or off (exported as `ICO_RD_STORE_PLAN=1` / `0`) ([RENDER.md](RENDER.md#the-device-profile)) |
| `blur_fold` | `true` / `false`: the device profile's `blurFold` (the staticBlur passes folded in pairs) on or off (exported as `ICO_RD_BLUR_FOLD=1` / `0`) ([RENDER.md](RENDER.md#the-device-profile)) |
| `uint_view` | `true`: the RGBA8_UINT views of the work buffers and the scene-class targets; `work`: the work buffers' only; `false`: none; only where the backend has the view (exported as `ICO_RD_UINT_VIEW=1`, `work` or `0`) ([RENDER.md](RENDER.md#the-device-profile)) |
| `alpha_plan` | `true` / `false`: the device profile's `alphaPlan` (the alpha/blend planner: a draw whose alpha range decides its alpha test or its blend factor drawn without the pass, the test or the blending it cannot need) on or off (exported as `ICO_RD_ALPHA_PLAN=1` / `0`) ([RENDER.md](RENDER.md#the-device-profile)) |
| `fx_plan` | `true` / `false`: the device profile's `fxPlan` (the frame-size post sprites drawn through the fixed-function blender or inside the open pass where that gives the same bytes) on or off (exported as `ICO_RD_FX_PLAN=1` / `0`) ([RENDER.md](RENDER.md#the-device-profile)) |
| `fx_dst_local` | `true` / `false`: the device profile's `fxDstLocal` (a post sprite reads its destination inside the open pass by local read), where the device has local read (exported as `ICO_RD_FX_DST_LOCAL=1` / `0`) ([RENDER.md](RENDER.md#the-device-profile)) |
| `vu_planes` | `true` / `false`: the device profile's `vuPlanesFrac` (the 3D pixel shaders without the attribute planes on a target at a fractional scale; the planes kept at every whole scale) on or off (exported as `ICO_RD_VU_PLANES=1` / `0`) ([RENDER.md](RENDER.md#the-device-profile)) |
| `dynamic_state` | Vulkan: `true`, the default, the dynamic depth (test, write, compare) and blend (enable, equation, write mask) state where the device offers it; `depth`: the depth dynamic, the blend static; `false`: both static, every pipeline with its depth and blend compiled in (exported as `ICO_VK_FAKE_NO_DYNAMIC=0`, `blend` or `1`; the `rhi_vk: dynamic state:` start line says `no ([dev] dynamic_state = false, ICO_VK_FAKE_NO_DYNAMIC=1)` or `no ([dev] dynamic_state = depth, ICO_VK_FAKE_NO_DYNAMIC=blend)` for a group it turned off) ([RHI.md](RHI.md#dynamic-state)) |
| `gpu_checkpoints` | `true`: GPU checkpoints, resource addresses (Vulkan) and DRED (D3D12) for the device-loss report (exported as `ICO_GPU_CHECKPOINTS=1`) ([RHI.md](RHI.md#device-loss)) |
| `gpu_robust` | `true`: robust buffer and image access (Vulkan; `VK_EXT_robustness2` where offered; exported as `ICO_GPU_ROBUST=1`), with null descriptors; `false`, the default on every platform, exports `0`. The device-loss order's `robust` step exports `1` for its run whatever the key says. With the key unset, Android's next start after a device loss exports `1` for that start, and the game writes `true` to `config.toml` itself after the second loss in a row (`port/platform/window_host.c` (`gpu_robust_persist`)); with the key set, it writes nothing ([RHI.md](RHI.md#device-loss), [PLATFORM.md](PLATFORM.md#diagnostics)) |
| `pass_split` | N: a render pass of VU draws ends and begins again after every N of them, the picture the same (exported as `ICO_RD_PASS_SPLIT`; 0 or `off`, the default: none); not in a multisampled pass, a storage pass, the exact blend's readable pass or a DATE draw through the stencil bit ([RENDER.md](RENDER.md#the-loss-bisect)) |
| `attachment_read` | `true`: the attachment read offered on Qualcomm's Adreno driver too, where it is off because it draws wrong colours (exported as `ICO_VK_ATTACHMENT_READ=1`) ([RENDER.md](RENDER.md#destination-reads-and-blend-paths)) |
| `storage_pass_merge` | `true`: the storage destination read (the interlock or ROV path) keeps an open storage pass open and draws in it, through the read, the draws it does not need the read for when the shader stores the same bytes, instead of ending the pass for each of them (exported as `ICO_RD_PASS_MERGE=1`; `false`, the default, exports `0`: every such draw ends the storage pass) ([RENDER.md](RENDER.md#the-storage-pass-merge)) |
| `vu_plain_only` | retired: read and ignored, one line a load (`ico_pc: [dev] vu_plain_only = <value> is not used (every device compiles one set of 3D shaders); ignored`; `port/platform/host_config.c` (`retired_dev`, `ico_ini_export`)), not exported. Every device compiles one VU module set; the fallback's plain or extended program entries are the renderer's table's (`RhiLimits.vuFallbackPlain`) or `ICO_RD_VU_PLAIN` / `ICO_RD_VU_EXTENDED` from the environment ([RENDER.md](RENDER.md#the-fallback-entries)). The game writes it nowhere; a file that holds `true` from an end inside an extended VU batch draws as without it |
| `vu_extended` | retired: read and ignored with the same line, not exported ([RENDER.md](RENDER.md#the-fallback-entries)) |
| `vu_prepass` | `false`: no VU vertex pre-pass, every 3D (VU) draw evaluates its triangle's corners in the triangle's own vertex shader, as a comparison on a device (exported as `ICO_RD_VU_PREPASS=0`; `true`, the default, exports `1`: each mesh and grid vertex evaluated once before the lists, where the device's colour targets hold the record and its start-up self-test passes). The game writes `false` to `config.toml` itself after the second start in a row that ended without a word, in the foreground, while the graphics driver compiled the pre-pass's pipelines; the first such end keeps only the next start without it, writing nothing; a restart after a driver crash in one of its pipelines or after a device loss draws without it for that run only ([PLATFORM.md](PLATFORM.md#diagnostics), [RENDER.md](RENDER.md#the-vertex-and-triangle-pre-passes)) |
| `crack_fill` | `false`: the crack filler off: above 1x under the Enhanced preset the pixels no draw covered between two drawn ones (the 1/16 GS pixel cracks between parts of the world) keep the clear's depth and take the fog's colour, as before it (exported as `ICO_RD_CRACK_FILL=0`; `true`, the default, exports `1`: they take the nearer drawn neighbour's colour and depth before the fog); the start line says `rd: crack fill: off ([dev] crack_fill = false, ICO_RD_CRACK_FILL=0)`; unset, it is on on every GPU; on a tile-based GPU's local-read fog it is drawn inside the scene pass the fog keeps (the state line ends `(drawn inside the scene pass with the local fog)`), and its GPU time is the `crack fill` effect of the 10 s `effects:` line ([RENDER.md](RENDER.md#crack-filling-above-1x)) |
| `perf_passes` | `true`: every replay timed pass by pass (a GPU timestamp between every two pieces of its work, outside the render passes) and each piece written to `ico-pc-passes.csv` in the logs folder (exported as `ICO_RD_PERF_PASSES=1`; `false`, the default, exports `0`: one replay timed so every 250 ms, for the 10 s `window: GPU top passes` line) ([RENDER.md](RENDER.md#the-per-pass-timing), [DISPLAY.md](DISPLAY.md#the-perf-csv)) |
| `perf_list_stamps` | `false`: no list or effect timestamp, so none is written inside an open render pass; the replay's GPU total (presents), the uploads' and the pre-pass's times and the per-pass timing stay, the lists' and the effects' times read 0 (exported as `ICO_RD_PERF_LIST_STAMPS=0`; `true`, the default, exports `1`): the A/B of what those stamps cost a tile-based GPU ([RENDER.md](RENDER.md#the-per-pass-timing)) |
| `joystick_directinput` | `false`: SDL's DirectInput controller scan off on Windows (`SDL_HINT_JOYSTICK_DIRECTINPUT` = 0 before `SDL_Init`; exported as `ICO_JOYSTICK_DIRECTINPUT=0`): the test for a pump held by that scan after a controller change (issue 105); XInput, HIDAPI and Raw Input controllers are still found, a device only DirectInput knows is not. No effect elsewhere (a line says so) ([PLATFORM.md](PLATFORM.md#diagnostics)) |
| `pipeline_set` | `full`, the default: the start-up set behind the start-up screen (the careful one on a device of the renderer's table, the Adreno 740 on Qualcomm's driver: `full` does not override it, a `window:` line says so); `careful`: the set under the careful policy on any device (`rd_pipeline_policy_careful`: batches of 8 on one worker thread, the pipeline cache written after each, stopped past 128 MB of the process's growth, the rest made on first use); `lazy`: no start-up set, each pipeline made when a frame first needs it, the pipeline cache written after each batch of them. Exported as `ICO_PIPELINE_SET=<value>`. After a start that ended without a word, in the foreground, while it made the pipelines (issues 93 and 107) the next start only is careful when that start made the full set and lazy when it made the careful one, writing nothing; the second such end in a row writes `lazy` to `config.toml` ([PLATFORM.md](PLATFORM.md#diagnostics), [RENDER.md](RENDER.md#the-start-up-set)) |
| `audio_sync` | `true`: the audio renders on the game's clock, once per vsync with the queue and cushion of the push, instead of on the audio device's (exported as `ICO_AUDIO_SYNC=1`; `false`, the default, exports `0`); for comparing the two when a report is about sound ([AUDIO.md](AUDIO.md#the-outputs-clock)) |
| `unlock_credits` | as `[dev] unlock_credits` |

At start `ico_ini_export` logs the `[dev]` keys of `config.toml` the game itself writes (`pipeline_set`,
`vu_prepass`, `gpu_robust`) in one line, `ico_pc: [dev] keys kept from an earlier start: pipeline_set =
lazy, ...`, so a log shows whether an old fallback is in force; no line when none is set.

(`port/platform/host_config.c` (`ini_map`, `export_dump_keys`, `export_audio_keys`,
`ico_ini_export`, `ico_host_fixed_clock`, `ico_dev_env_value`); `port/platform/main_host.c`, header comment,
`record_open`, `use_iso_mode`, `android_keep_image`.) The program writes the ini only to store
`iso=` (`port/platform/host_config.h`, `ico_ini_store`, which keeps the file's other lines).

The packages ship an `ico-pc.ini` written by `pkg_write_ini`: notes in the platform's wording,
`iso=` (with the packager's value), `watchdog=30`, and `portable=1` either commented out or, in the
portable archive, on (`tools/package_docs_lib.sh` (`pkg_write_ini`)).

## System configuration (libscf)

`port/config/sysconf.c` answers the game's libscf calls from the settings. `sceScfGetLanguage`
returns `[game] language` when it names one of the game's five languages, else the host's: the
first of SDL's preferred locales the game has (window build), else `LC_ALL`, `LC_MESSAGES` or
`LANG` (not `C` or `POSIX`), else English; the answer is cached (`port/config/sysconf.c`
(`sceScfGetLanguage`); `port/config/sysconf.h` (`ico_sysconf_host_language`)). Japanese, Dutch and
Portuguese are never answered. `sceScfGetTimeZone` and `sceScfGetSummerTime` return 0.

The boot screens' choices come from the config: the port skips the language and 50/60 Hz screens,
and `ico2/common/src/main.c` (`Main`) sets the language (`sceScfGetLanguage` mapped onto the game's
numbers 2..6 as the boot screen maps it) and the video mode (`[video] video_mode`, else 60 Hz) on
every boot (`port/config/sysconf.h` (`ico_boot_language`, `ico_boot_video_mode`,
`ico_scf_to_game_language`)). The Settings menu's Language row writes `[game] language` and the
cached answer together (`ico_sysconf_set_language`); the boot's 50/60 Hz value is written by
`ico_sysconf_set_video_mode`.

## The user folder and portable mode

`config.toml`, `achievements.toml`, the card folders and the rest of the per-user files are in the
[user folder](ARCHITECTURE.md#terms) (`port/platform/host_config.h` (`ico_host_pref_dir`));
[PLATFORM.md](PLATFORM.md#folders-ico-pcini-and-the-user-folder) has where it is on each platform
and in portable mode, and the Android build's file layout is `IcoAndroidPaths`
(`port/platform/android/android_paths.h`).

## Adding a key

1. Read it where it is used with `ico_config_get_bool`, `_int`, `_float` or `_string` and a
   `section.key` path (`port/config/config.h`). A module with several keys keeps one reader
   (`port/game/video_options.c` (`read_config`) for `[video]`; `port/game/options.c` (`get_or`)
   for `[gameplay]`), and the default is written in that call only.
2. Write it with `ico_config_set_*` and `ico_config_save` (for `[video]`, `ico_video_save`); a
   Settings row is in `port/ui/settings.c` ([UI.md](UI.md#adding-a-row)).
3. For players to see it in a new file, add it with its default and a comment to the template
   `first_run_text` (`port/config/config.c`).
4. For `ico-pc.ini` to override it, add it to `ini_map` (`port/platform/host_config.c`); when the
   simulation depends on it, add it to `record_keys` (`port/platform/main_host.c`) so a pad
   recording carries it ([INPUT.md](INPUT.md#recordings)).
5. A key that nothing reads goes into `retired_keys` (`port/config/config.c`), so a file
   that still has it keeps the line and logs `config: <key> is not used; ignored`; a `[dev]`
   A/B switch that `ini_map` maps goes into `retired_dev` (`port/platform/host_config.c`)
   instead, out of `dev_env`, and logs `ico_pc: [dev] <key> = <value> is not used (...);
   ignored` from `ico_ini_export`.
6. Add its row to this document and, for a player-facing key, its entry to `docs/OPTIONS.md`
   (a menu row under its page, a file-only key under "Settings you can only change in a file").

An `[input]` switch goes through the input layer's own reader instead
([INPUT.md](INPUT.md#the-config-keys)).

## Tests

`port/config/test/config_test.c` is the ctest `config`: the TOML reader and writer (render and the
atomic save), round trips, the ini over `config.toml` over default precedence, the language
`sceScfGetLanguage` answers, the wall clock and the [EE](ARCHITECTURE.md#terms) timers
(`port/config/test/config_test.c`, header comment; `port/config/CMakeLists.txt`). The Settings
menu tests check what the pages write (`port/ui/test/settings_test.c`; [UI.md](UI.md#tests)). The
defaults of `[gameplay]`, `[photo]`, `[characters]` and `[input]` are also checked by `options`,
`photo`, `appearance` and `input` (`port/game/test/`, `port/input/test/`), and the ini's reader and
the `[dev]` keys' environment switches by `host_config` (`port/null/CMakeLists.txt`). Run them with
`ctest --preset linux-x64 -R '^(config|host_config|options|photo|appearance|input|settings)$'`.

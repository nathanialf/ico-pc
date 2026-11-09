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
| `port/game/options.h` | the `[gameplay]`, `[game] circle_back`, `[ui] language`, `[audio] output`, `[mirror]`, `[players]` and `[dev] debug_option` keys |
| `port/input/input_config.c`, `port/input/input.h` | the `[input]` keys and the bindings |
| `port/game/photo_mode.h` | `[photo]` |
| `port/game/appearance.h` | `[characters]` |
| `port/game/achievements.h` | `[game] achievements` and `achievements.toml` |
| `port/render/texpack.h` | what the texture pack keys of `[video]` control |
| `port/platform/main_host.c` | the start-up keys of `ico-pc.ini` and the command line |
| `port/platform/window_host.c` | `[video] backend`, `[video] adapter`, `[dev] perf_log`, `slow_step_ms` and `slow_scene_dump_ms` |
| `port/platform/render_thread.c` | `[dev] render_thread`, `big_cores` |
| `port/render/rd_submit_thread.c` | `[dev] submit_thread` (`ICO_RD_SUBMIT_THREAD`) |
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
| `[video] adapter` | `adapter` |
| `[dev] ticks`, `watchdog`, `watchdog_pump`, `trace`, `dump_every`, `dump_dir`, `dump_interp`, `dump_from`, `audio_dump`, `audio_sync`, `pad_script`, `input_record`, `verify`, `headless`, `fixed_clock`, `use_iso`, `write_config`, `start_stage`, `switch_to`, `switch_at`, `backend_switch_test`, `unlock_credits`, `popup_test`, `state_save_at`, `state_load_at`, `state_hash_at`, `state_dump_at`, `render_thread`, `submit_thread`, `big_cores` | the same names |

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
  (`ico_mirror_slot_saved`)), and on the 2001 discs its `[players]` entry
  (`ico_players_slot_saved`);
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
| `saves2` | string | absent: no card, or `memcard2/` in the user folder while `[gameplay] second_memory_card` is on | the card in port 1 (`host_config.c` (`ico_host_saves2_dir`)); it wins over the option |
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
| `gamma` | int | `50` | Picture > Gamma (`RdSettings.toneGamma`): 1..100, 50 the game's own picture; outside the range clamped, a word 50, each with a `video:` stderr line; no key (a file from before the rows) is 50; written with every save; outside the preset, with or without the CRT filter |
| `brightness` | int | `50` | Picture > Picture brightness (`RdSettings.toneBrightness`): as `gamma`. Not the game's own Brightness row (`systemStatus[11]`, the pause menu's, never written here) |
| `contrast` | int | `50` | Picture > Contrast (`RdSettings.toneContrast`): as `gamma` |
| `saturation` | int | `50` | Picture > Saturation (`RdSettings.toneSaturation`): as `gamma` |
| `texture_pack` | bool | `true` | load an installed texture pack (the disc's serial folder: SCES-50760 or SCPS-11003, [PACKS.md](PACKS.md)) |
| `dump_textures` | bool | `false` | write each texture under its PCSX2 name |
| `model_pack` | bool | `true` | load an installed model pack |
| `dump_models` | bool | `false` | save each model part as glTF |
| `show_fps` | bool | `false` | the Frame rate counter: the presented frames per second as small text in the screen's top-left corner |
| `downsample` | string | `"off"` | `off`, `box`: the scene drawn at the Resolution and box-averaged to the game's own size (the Downsampling row on Picture); outside the preset; in force at 2x or more, never with the CRT filter or at `window` |
| `upscaling` | string | `"off"` | `off`, `fsr1`, `fsr3` (FSR 3.1, below): AMD FidelityFX Super Resolution 1.0 (EASU and RCAS) at the present where the picture has fewer texels than the screen's box on both axes (the Upscaling row on Picture); outside the preset; never with the CRT filter or at `window`; `fsr3` (FSR 3.1, Vulkan 1.2 devices of a build with the FidelityFX upscaler) is read as saved and drawn as `off` where `rd_upscale_temporal_supported()` is false (every other device and build), not rewritten; an unknown word reads as `off` with a line ([DISPLAY.md](DISPLAY.md#fsr-10-upscaling)) |
| `upscaling_sharpness` | int | `80` | 0..100 (clamped, with a line): the Sharpness row, RCAS's stops `2 (1 - s / 100)`, half a stop softer past 2x a side; outside the preset |
| `upscaling_quality` | string | `"native"` | `native`, `quality`, `balanced`, `performance`: the Upscaling quality row, FSR 3.1's starting ratio 1.0 / 1.5 / 1.7 / 2.0 (Original's 1x and a scene at 1x take Native AA only); an unknown word reads as `native` with a line; outside the preset ([DISPLAY.md](DISPLAY.md#fsr-31-upscaling)) |
| `texture_pack_budget_mb` | int | `2048` | file only; graphics memory for replacements, 128..65536 |
| `texture_pack_precache` | bool | `true` | file only; read the whole pack into memory at start |
| `texture_pack_cache_mb` | int | `0` | file only; memory for that read, 0 (half the computer's) or 128..65536 |
| `effect_glow`, `effect_depth_of_field`, `effect_softening`, `effect_motion_blur`, `effect_fog`, `effect_cinematic_bars` | bool | `true` | the game's own picture effects |
| `effect_glow_mode` | string | `"original"` (`"smooth"` for a file without the key whose Display rows are Enhanced's) | Glow between frames above Frame rate Original: `"original"` (a tick's later presents paste the glow the first drew) or `"smooth"` (`RdSettings.glowSmooth`: the glow's chain drawn at every present, [WORK_BUFFERS.md](WORK_BUFFERS.md#the-glow-between-ticks)); part of the preset: `ico_video_preset` is Original with `"original"`, Enhanced with `"smooth"`, and any other mix of the four rows and this key is Custom (`ico_video_preset_rows` is the four rows alone, which the renderer's preset and the anti-aliasing lock follow); the Enhanced shortcut writes `"smooth"`, Original `"original"`; no key: derived at load from the four rows, with the `video: effect_glow_mode not in the file: ...` line, and written at the next save; an explicit key wins; anything else is `"original"` with a `video:` stderr line; written with every save |
| `effect_motion_blur_mode` | string | `"original"` | Motion blur's way while `effect_motion_blur` is on: `"original"` (the game's sprite reading the DISPLAY the tick started from) or `"smooth"` (`RdSettings.motionBlurSmooth`: an accumulator of every present's scene, [RENDER_POST.md](RENDER_POST.md#the-motion-blur-under-smooth)); anything else is `"original"` with a `video:` stderr line; written with every save, never by itself |
| `effect_motion_blur_strength` | int | `100` | Motion blur strength (`RdSettings.motionBlurStrength`): 10..100 in tens, the motion blur sprite's FIX drawn as `round(FIX x strength / 100)` under Original and Smooth alike ([RENDER_POST.md](RENDER_POST.md#the-motion-blurs-strength)); a value off the tens is rounded to the nearest (half up), outside 10..100 clamped, a word is 100, each with a `video:` stderr line; no key (a file from before the row) is 100; written with every save |
| `effect_water_sharp` | bool | `false` | Water reflections: `true` (Sharp) draws the reflections in water and the queen barrier's picture at the scene's scale, `false` (Original) at their GS size ([RENDER_POST.md](RENDER_POST.md#water-and-render-to-texture-blocks)) |
| `effects_depth` | bool | `true` | file only; the scene's depth for an effects program such as ReShade |
| `video_mode` | string | absent: 60 Hz | `pal50` or `60hz`, the boot screen's choice (`port/config/sysconf.c`); ignored on every disc but the European one, which have no 50 Hz mode (`ico_boot_video_mode` returns 60 Hz and logs it once) |
| `backend` | string | `"vulkan"` | `vulkan` or `d3d12`, the window build's renderer; when the key is empty, a run restarted after a graphics driver crash takes the backend its retry step names (`port/platform/window_host.c` (`ico_window_open`); `port/platform/startup_retry.h`) |
| `adapter` | string | `""` | the graphics card by its name exactly as the backend lists it, `""` automatic; read at the start before `rd_init` (`port/platform/window_host.c` (`ico_window_open`), `rhi_set_adapter_wanted`) and written by Options > Graphics > Graphics card; the backend's own variable (`ICO_VK_DEVICE`, `ICO_D3D12_ADAPTER`, `ICO_D3D11_ADAPTER`) wins over it, a name no runnable device has is the automatic pick with a `rhi_vk:` (`rhi_d3d12:`, `rhi_d3d11:`) line, OpenGL does not read it ([RHI.md](RHI.md#choosing-the-device)); the ini's `adapter=` too |
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
| `text_language` | string | `""` | a community translation of the game's menu words and subtitles, drawn as text over the English game ([GAME_FEATURES.md](GAME_FEATURES.md#community-translations)): `ru-bobttz` (Russian, by bob-ttz), `ru-cobra838` (Russian, by cobra838) or `pt-br` (Brazilian Portuguese, by Igor Chagas); `""` none; any other word is `""`, logged once. In force on the European disc while `language` is English (`port/game/options.h` (`ico_opt_text_language`); `port/ui/ui_lang.h` (`ui_settings_text_language`)); the Language row writes it with `language` = `en` (`port/ui/settings.c` (`setLanguageEntry`)); in the first-run file |
| `circle_back` | bool | `true` | Circle backs out of the game's menus as Triangle does (`port/game/options.h` (`ico_opt_circle_back`)) |
| `achievements` | bool | `true` | the achievement popups (`port/game/achievements.c` (`ico_ach_init`)) |
| `disc` | string | `"auto"` | `pal` (the European disc, SCES-50760), `jpn` (the Japanese disc, SCPS-11003), `kor` (the Korean disc, SCPS-56001), `ntsc` (the North American disc, SCUS-97113), `aug6` (the August 2001 preview), `jun26` (the June 2001 magazine demo, SCUS-97158) or `cn` (the Asian disc, SCPS-55001) pins the disc a start plays when more than one is set up; `auto` lets the disc image given and the game data there decide; any other value is `auto`, logged once (`port/config/config.c` (`ico_config_disc_pin`); the rules: [PLATFORM.md](PLATFORM.md#the-disc-a-start-plays)) |
| `last_disc` | string | absent | written by the program, not in the template: one of those slugs, the disc a start last played, the choice's fallback when more than one disc's data is there (`ico_config_last_disc`, `ico_config_set_last_disc`; `main_host.c` (`select_disc`)) |

### [ui]

| key | type | default | read by |
| --- | --- | --- | --- |
| `language` | string | `"auto"` | the language of the port's own text (its menus, photo mode's help, the messages): `en`, `fr`, `de`, `it`, `es`, `ja`, `ko`, `zh`, `ru-bobttz`, `ru-cobra838`, `pt-br`; `auto` follows the disc: its region row's `ui_lang_default` (Japanese on the Japanese disc, Korean on the Korean disc, Chinese on the Asian disc), on the European disc the community translation in force (`[game] text_language`), else the game's own language; any other word is `auto`, logged (`port/game/options.h` (`ico_opt_ui_language`); `port/ui/ui_lang.h` (`ui_settings_resolve_language`)). The game's own text is always the disc's |

### [gameplay]

Read once by `port/game/options.c` the first time each is asked for; every default is the
original game's behaviour (`port/game/options.h`, header comment).
[GAME_FEATURES.md](GAME_FEATURES.md) has what each does to the game.

| key | type | default | meaning |
| --- | --- | --- | --- |
| `stick_fix` | bool | `false` | the stick fix (`ico_opt_stick_fix_default`); the input layer reads it too |
| `yorda_safe` | bool | `false` | the shadows never take Yorda |
| `pit_time` | integer or string | `"original"` | how long a shadow takes to pull Yorda into a pit, in whole seconds `4` to `30` (the Settings menu writes an integer), or `"original"`: the pull-in is the five SINK motions A to E (`port/game/pit_time.h`: 462 game ticks = 15.4 s on every released disc and the August 2001 preview, 463 on the June 2001 demo), the time factor is seconds x 30 / ticks and their play speed is 1/factor, so the whole pull-in lasts the seconds set; the camera effect's 5 s timeout scales by the same factor; `15` (the base in whole seconds) reads as `"original"`; the earlier words still read: `"half"`, `"longer"`, `"double"`, `"triple"` are 1/2, 1.5, 2, 3 times the disc's time in whole seconds held to the range (8, 23, 30, 30; `"double"` and `"triple"` log `options: [gameplay] pit_time = "double" is past 30 seconds: held to 30 s`, not the unknown-value line); an unknown word or a number out of range logs and reads `"original"` or the nearest end; logs `gameplay: pit time <value> (this disc's pull-in <ticks> ticks = <s> s, shown as 15 s; x<factor>)` at load and on change; recorded in the pad recording's header |
| `shadow_speed` | string | `"original"` | how fast shadows walk, run and fly: `"slower"`, `"original"`, `"faster"` or `"fastest"` (factors 0.75, 1, 1.25, 1.5), on each disc's locomotion motions only (`port/game/options.h` `ico_opt_shadow_speed_motion`: attacks, grabs, hits, the dissolve and the pit dive keep the game's speed); an unknown word reads `"original"` and logs; recorded in the pad recording's header |
| `shadows_together` | bool | `false` | no turns between the shadows: the game holds a shadow back from its attack while another is attacking (`enemy_act.c` `EnemyUtil_isOtherStatus`); with this on none is held back ([GAME_FEATURES.md](GAME_FEATURES.md#shadows-attack-together)); read at each check, so a change acts at once; a simulation key (pad recording header, save state check) |
| `door_fix` | bool | `false` | an arrival holding hands does not send the pair back through the door |
| `yorda_trips` | bool | `false` | Yorda trips and falls as she does after the main gate opens: after 3 s of Ico running pulling her by the hand (the main gate scene's own count, again every further 3 s of it; no random roll); her fall's motions are added at each stage load, so a change takes effect from the next one ([GAME_FEATURES.md](GAME_FEATURES.md#yorda-trips)); a pad recording's header records it |
| `mirror` | bool | `false` | mirrored play, the value a run starts from until New Game or a load |
| `natural_chains` | bool | `false` | a chain let down keeps its links' size |
| `skip_opening` | bool | `false` | the boot starts at the title screen, without the Sony presents sign and the opening scene (from the next start); a pad recording's header records it |
| `second_memory_card` | bool | `false` | a second memory card in port 1, the folder `memcard2/` in the user folder when `[paths] saves2` is absent ([SAVES.md](SAVES.md#the-card)); a change puts the card in or takes it out at once (`port/save/mc_host.c` (`ico_mc_host_second_card_changed`)). Not a simulation key: the card is the host's, read by the game when a save or load screen opens, so a pad recording and a save state do not record it |
| `hold_type` | string | `"disc"` | `"disc"`: the disc's own way (A on the European, Japanese, Korean and Asian discs; B on the North American disc, the August 2001 preview and the June 2001 demo); `"hold"`: keep R1 held to hold Yorda's hand (the game's Hold type A); `"toggle"`: press R1 once to take it and again to let go (B). `"hold"` is read as a choice only beside `hold_type_chosen = true`, else as `"disc"`: a bare `"hold"` is the old first-run text, which every file written before `"disc"` existed holds. Wins over the memory card's `controlType`; another word reads as `"disc"` and is logged once. A pad recording's header records it |
| `hold_type_chosen` | bool | absent | written `true` with every Hold type choice from Options; it makes a `"hold"` the player's A rather than the old first-run text. A pad recording's header records it |
| `developer_mode` | bool | `false` | the development build's debug menu and options |

### [variants]

Options > Gameplay: No hints on the page, Chain swing speed and Chain swing slowdown on Gameplay > Yorda,
Shadow toughness, Shadow aggression and Peaceful shadows on Gameplay > Shadows, the combat rows
(Automatic guard, Turn to face shadows, Shadows wait while Ico is down, One hit per swing, Shadows never dodge,
Queen barrier and the two weapon rows) on Gameplay > Combat, the two cutscene rows on Gameplay > Cutscenes ([UI.md](UI.md#gameplay--yorda-shadows-combat-and-cutscenes),
[OPTIONS.md](../OPTIONS.md#gameplay)).
Read once by `port/game/variants.c` the first time each is asked for (`ico_variant_*`); every default is
the game as the disc ships it. All sixteen are in a pad recording's header and a save state's settings hash (`ICO_SIM_KEYS`,
`port/platform/sim_keys.c`). A word
or number that is no value reads as the default and is logged once.

| key | type | default | meaning |
| --- | --- | --- | --- |
| `shade_toughness` | string | `"original"` | `"original"` (the disc's table), `"american"` (every Shadow as tough as on the North American disc, and no tougher on later playthroughs) or `"no_plus"` (the disc's Shadows, no tougher on later playthroughs) or `"third_journey"` (the factor of a third playthrough, `gameParam[40]`, twice the first journey's; the PAL, Japanese, Korean and Chinese discs only: elsewhere it reads as `"original"` with a log line) |
| `one_hit` | bool | `false` | a swing hurts a Shadow only once (`debug_one_hit_only`); suspends achievements |
| `never_dodge` | bool | `false` | the Shadows never dodge (`debug_ignore_dodge`); suspends achievements |
| `aggression` | string | `"own"` | `"own"` (each Shadow's own), `"bold_patient"`, `"bold_quick"`, `"wary_patient"` or `"wary_quick"` (`debug_enemy_battle_type`) |
| `no_hints` | bool | `false` | no hints, as on the North American disc |
| `barrier_hits` | int | absent (not in the template) | `5` or `7`: the hits the Queen's barrier takes; absent, the disc's own (`7` on `jun26`, else `5`); the Settings row writes it |
| `no_cutscene_look` | bool | `false` | no camera look-around in cutscenes |
| `ignore_cutscene_cameras` | bool | `false` | the cutscenes' cameras are ignored (`debug_ignore_demo_camera`); suspends achievements |
| `auto_guard` | bool | `true` | Ico blocks a Shadow's attack from the front; `false` = he does not (does not suspend achievements) |
| `attack_turn` | bool | `true` | Ico's swing turns him toward the nearest Shadow; `false` = he swings straight ahead (does not suspend achievements) |
| `shadows_wait_down` | bool | `true` | the Shadows hold back while Ico is knocked down; `false` = they do not (does not suspend achievements) |
| `sword_colour` | string | `"original"` | `"original"` or `"red"`: the light sword (weapon kind 9) is made as its red twin, kind 8, from the next room; offered on pal, jpn, kor and cn only (the other tables have no kinds 8 and 9: `"red"` reads as `"original"` there, logged once); does not suspend achievements |
| `weapon` | string | `"original"` | `"original"` or `"net"`: the stick (kind 1) is made as the insect net (kind 7, 6 on jun26), from the next room: held, never swung, never lit, power 0; does not suspend achievements |
| `peaceful_shadows` | bool | `false` | a Shadow is started without its brain thread (the game's `BrainOnOff` switch, for Shadows only; Yorda's brain is untouched); suspends achievements |
| `chain_swing_speed` | int | `4` | `1` to `10`: the game's debug row `CHAIN CYCLE SPEED` (`debug_chain_cycle_speed`), the swing's period when Ico takes hold of a chain; higher is a shorter swing, `4` the game's own (leaves the game's value alone); anything else reads as `4` with a log line; does not suspend achievements |
| `chain_swing_damping` | int | `4` | `1` to `10`: the game's debug row `CHAIN SLOW SPEED` (`debug_chain_slow_speed`), how fast a swing dies down while Ico is not swinging it (`0.05` of swing amplitude per tick per step); `4` the game's own; same reading and rule as above |

The Debug menu row (Options > Developer, with Developer mode on) has no key: its press marks the Debug menu used until the options are reloaded,
which suspends achievements too (`ico_variants_suspend_achievements`, read by `gamestate.c` beside
developer mode), opens the game's debug menu and closes Options. Where the game reads each key, and
what it changes, is in [GAME_FEATURES.md](GAME_FEATURES.md#game-variants); a change on a page
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

### [players]

`slot_N` (int, 1 or 2) and `slot_N_sum` (the save's game-block checksum) for save file N, on the
North American disc, the August 2001 preview and the June 2001 demo only (`ntsc_slot_N`,
`aug6_slot_N`, `jun26_slot_N` and their `_sum`): the Players count (`girlControlMode`) each save
was made with. The other discs keep it in the card's save file after the game block; these discs'
game block has no word for it ([SAVES.md](SAVES.md#the-seven-discs)). A slot without an entry, or
whose checksum differs, loads as one player (`port/game/options.h` (`ico_players_slot_saved`,
`ico_players_slot_loaded`); [GAME_FEATURES.md](GAME_FEATURES.md#players-on-the-2001-discs)).

```toml
[players]
ntsc_slot_1 = 2
ntsc_slot_1_sum = 4660
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
| `touch_mode` | string | `"auto"` | `off`, `auto`, `always`, `always_scenes` |
| `touch_size` | string | `"medium"` | `small`, `medium`, `large` |
| `touch_opacity` | number | `75` | 10..100 |

The bindings themselves are `kb.<target>`, `mouse.<target>` and `pad.<target>` for each pad target
(`cross`, `circle`, `lstick_up`, ...; `bindings.c` (`target_names`)), plus `kb.walk`; a value is
one name or an array of names from `port/input/keys.def`, the mouse buttons and the gamepad
sources. A key or value that does not parse is logged and skipped (`ico_bindings_set`). The writer
puts back only what differs from the defaults or is already in the file
(`port/input/input.h` (`ico_input_write_bindings`)). [INPUT.md](INPUT.md) has the bindings.

One target is an action, not a pad button: `hint` (Ask Yorda for a hint; `kb.hint`, `mouse.hint`,
`pad.hint`; no default on any device; `ICO_T_HINT`). Settings > Controls > Remap controls lists it
last, and only on the discs with the hint system (not `ntsc`, `aug6`, `jun26`).

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

### [developer]

| key | type | default | meaning |
| --- | --- | --- | --- |
| `free_movement` | bool | `false` | Options > Developer > Free movement: with Developer mode on, L2 hands Ico to the game's own debug move (`ACTDebugMove`, [GAME_FEATURES.md](GAME_FEATURES.md#free-movement-and-ask-yorda-for-a-hint)); a simulation key (`port/platform/sim_keys.c`); suspends achievements (`ico_gs_achievements_suspended`) |

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
| `slow_scene_dump_ms` | float | `40` | the slow-scene dump's bound: when a 10 s block spent most of its time in a scene layout and its timed replays' GPU work (the span less the display wait and the present pass) averaged over this many ms, the game writes the frame of the first replay after it whose vertex work (the VU line's: the pre-pass's vertices on, the triangles times 9 off) is at or above that block's mean, and the log, to `logs/slow-<date>-<time>.zip`: the first such block, then up to two more, each with at least 1.5 times the work of the one before, three files kept, none before 30 s of uptime; 0 is off. Written in releases too ([ANDROID_DIAGNOSTICS.md](ANDROID_DIAGNOSTICS.md#issue-179s-lines-what-each-decides)) (`port/platform/window_host.c` (`slow_scene_check`), `window_scene.h` (`window_slow_scene_due`, `window_slow_scene_pick`)) |
| `blend` | string | (none: `fixed`) | the A/B switches below, as their ini keys |
| `uber` | string | `auto` | |
| `fog_path` | string | (none) | |
| `date_path` | string | (none) | |
| `store_plan` | bool | (the device profile's: on) | |
| `blur_fold` | bool | (the device profile's: on, off on Mali) | |
| `uint_view` | bool or `work` | (the device profile's) | |
| `alpha_plan` | bool | (the device profile's) | |
| `fx_plan` | bool | (the device profile's) | |
| `fx_dst_local` | bool | (the device profile's) | |
| `vu_planes` | bool | (the device profile's) | |
| `shadow_one` | bool | (the device profile's) | |
| `shadow_bspline` | bool | `false` | |
| `dynamic_state` | bool or `depth` | `true` (off on a tile-based GPU unless set) | |
| `bind_cache` | bool | `true` | |
| `prerotate` | bool | `true` | |
| `gpu_checkpoints` | bool | `false` | |
| `gpu_robust` | bool | `false` | |
| `gpu_robust_by` | string | (absent) | |
| `pass_split` | int | `0` | |
| `attachment_read` | bool | `false` | |
| `storage_pass_merge` | bool | `false` | |
| `vu_plain_only` | bool | (retired) | |
| `vu_extended` | bool | (retired) | |
| `vu_set` | `auto`, `plain` or `extended` | `auto` | |
| `sheet_shader` | bool | `false` | |
| `acquire_ahead` | bool | `true` | |
| `original_async` | bool | `true` | |
| `aura_kernel` | `box` or `gs` | `box` | |
| `aura_box` | `auto`, `on` or `off` | `auto` | |
| `pass_area` | `auto`, `on` or `off` | `auto` | |
| `clear_carry` | `auto`, `on` or `off` | `auto` | |
| `clear_skip` | `auto`, `on` or `off` | `auto` | |
| `last_store` | `auto`, `on` or `off` | `auto` | |
| `vu_prepass` | bool | `true` | |
| `vu_prepass_batch` | bool | `true` | |
| `vu_record` | `auto`, `targets`, `stores` or `compute` | `auto` | |
| `vu_prepass_min` | whole number | `0` | |
| `velocity_test` | bool | `false` | |
| `velocity_sign` | `1` or `-1` | `1` | |
| `upscale_test` | bool | `false` | |
| `upscale_debug_view` | bool | `false` | |
| `upscale_debug_checks` | bool | `false` | |
| `display_layer` | bool | `true` | |
| `vu_merge` | bool | `true` | |
| `vu_fuse` | bool | `true` (off with `vu_merge`) | |
| `vu_skin_check` | bool | `true` on an Adreno with a window (Qualcomm's driver or Turnip), `false` elsewhere | |
| `vu_bones` | `ring` or `cbuffer` | (absent: `cbuffer`) | |
| `vu_bones_by` | string | (absent) | |
| `turnip_debug` | `nobin`, `retest` or `off` | (absent) | |
| `turnip_debug_by` | string | (absent) | |
| `constant_reads` | bool | `true` | |
| `cull` | `off`, `frustum`, `full` | `full`; `frustum` on a tile-based GPU | |
| `region_rule` | `camera` or `ps2` | `camera` | |
| `jitter_test` | `x,y`, each -0.5 to 0.5 | (absent: no jitter) | |
| `crack_fill` | bool | `true` | |
| `texture_decode_threads` | the texture decode pool's worker threads: `auto`, the default, 2 (1 on a machine with 2 hardware threads or fewer); `0` every texture decoded on the game thread, the A/B; `1` to `4` that many (exported as `ICO_RD_TEXTURE_DECODE_THREADS`; unset without a window it is 0). The start line is `rd: texture decodes: 2 worker threads (4 hardware threads)` or `rd: texture decodes: on the game thread ([dev] texture_decode_threads = 0)`; the textures are the same byte for byte either way ([TEXTURES.md](TEXTURES.md#the-pools-size)) |
| `perf_passes` | bool | `false` | |
| `perf_list_stamps` | bool | `true` | |
| `still_represent` | bool | `true` | |
| `glow_redraw` | bool | `false` | |
| `glow_fold` | bool | the device profile's: on on immediate-mode GPUs, off on tile-based ones (phones, Macs), wherever the frame is presented | |
| `joystick_directinput` | bool | `true` | |
| `present_fallback` | `auto`, `off` or `fifo` | `auto` | |
| `frame_rate_pin` | `auto` or `off` | `auto` | |
| `render_thread` | `auto`, `on`, `off` or `sync` | `auto` | the window's render thread: `auto` and `on` make every picture on it (the interpolation, the walk, the submit and the present) with every graphics API, `off` on the game thread as before, `sync` on it with the game thread waiting for each picture (the A/B); also `render_thread` in `ico-pc.ini` (`port/platform/render_thread.c` (`ico_render_thread_start`), `port/platform/frame_mailbox.c` (`fm_mode_choose`); [PLATFORM.md](PLATFORM.md#the-render-thread)) |
| `submit_thread` | bool | `true` | `false`: the render thread ends, submits and presents each picture itself instead of handing it to the submit thread, so one picture fewer waits between the walk and the screen (exported as `ICO_RD_SUBMIT_THREAD=0`, the start line then `rd: submit thread: off (ICO_RD_SUBMIT_THREAD=0: the render thread submits and presents its pictures)`; `true` exports `1`, the default). Issue 123's A/B with `render_thread = off` (everything on the game thread): a stall whose length or spacing changes with either is in the port's queue of pictures, one that does not is below the port; also `submit_thread` in `ico-pc.ini` (`port/render/rd_submit_thread.c` (`rd__submit_thread_start`); [PLATFORM.md](PLATFORM.md#the-render-thread)) |
| `big_cores` | `true` or `false` | `true` | Android only: the render thread and the submit thread keep themselves to the cores faster than the slowest (by `cpufreq/cpuinfo_max_freq`); `false` leaves them where the system places them. A diagnostic escape for a phone where keeping to those cores goes wrong, not a performance switch; the start lines and the 10 s line's cores say where each thread ran. Also `big_cores` in `ico-pc.ini` (`port/platform/render_thread.c` (`place_init`), `port/platform/cpu_place.h`; [PLATFORM.md](PLATFORM.md#the-render-thread)) |
| `disc_reader` | `thread` or `sync` | `thread` (the window build; `sync` without a window) | |
| `disc_read_ahead_kb` | int | `256` (with the thread; `0` without one) | |
| `pipeline_set` | string | `full` | |
| `pipeline_memory_floor_mb` | string | `auto` | |
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
| `start_stage`, `switch_to`, `switch_at` | the stage Main starts in; one forced stage change (exported); a stage of the running disc (1 to 105 European, 103 on the 104-row discs, 102 June 2001 demo), another value is refused with a log line |
| `popup_test` | test popups from Main tick 100 (exported as `ICO_UI_POPUP_TEST`) |
| `backend_switch_test` | the window build: a list of Graphics API values (`"opengl,vulkan"`) switched to in turn through Options > Graphics > Apply's own path and kept, the first 300 pumps after the start, each next 120 pumps after the last returned; one line each and `window: backend switch test: done, N switches, F failed` (`port/platform/window_switch.c` (`selftestPoll`); [PLATFORM.md](PLATFORM.md#the-renderer-switched-from-the-menu)) |
| `state_save_at`, `state_load_at`, `state_hash_at` | save states for the headless round trip (`main_host.c`, `port/platform/state.h` `ico_state_dev_set`): `"<Main tick>:<slot 1-4>"` saves (on the pause menu, as any save) and loads once when the tick reaches it; `"<Main tick>"` logs `state: hash <xxh3-64> of arena+gdata+gbss at tick N` each time the tick reaches it, the hash of the game's state with the host's pointers and handles left out ([PLATFORM.md](PLATFORM.md#saving-and-loading), the hash rule); a value that does not parse is logged and ignored |
| `state_dump_at` | `"<Main tick>"`: each time the tick reaches it, the arena and the game's data and bss written raw to `<states folder>/dump-<tick>-<k>/` (k counts the run's dumps from 1) with `regions.txt` and `masked.txt`, for `tools/state_region_diff.py` (`port/platform/state.h` `ico_state_dev_set_dump`; [PLATFORM.md](PLATFORM.md#saving-and-loading)) |
| `blend` | the GS blend's path: `fixed`, the default (nothing set exports nothing), the GPU's fixed-function blender on every device; `attachment` or `storage` opts into that exact destination read where the device has it, kept whatever the start-up blend self-test finds; `exact` opts into the best one the device has (the attachment read, else the storage read), logged `rd: blend: [dev] blend = exact: the exact blend is an opt-in; attachment tried` (exported as `ICO_RD_BLEND`) ([RENDER.md](RENDER.md#destination-reads-and-blend-paths)). Where GS state as shader data is on (`uber`, next) the attachment read is in force without this key; a value other than `attachment` or `exact` turns GS state as shader data off |
| `uber` | GS state as shader data: `auto`, the default, on a tile-based GPU whose Vulkan driver reads the colour attachment in rasterization order (Mali with the ARM extension, Adreno on Turnip; never PowerVR, Qualcomm's own driver or MoltenVK), the blend, the write mask and DATE drawn in the shader through that read so the start-up pipeline set is several times smaller and the blend is the GS's integers; `on` wherever the device has the read (lavapipe, for the tests); `off` never (exported as `ICO_RD_UBER=auto`, `1` or `0`); logged `rd: pipelines: GS state as shader data: on (raster-order read): ...` or `off (<why>)` ([PIPELINES.md](PIPELINES.md#gs-state-as-data)) |
| `fog_path` | `inplace`, `copy` or `buffer`: the fog's depth path, tried first; `local`: the device profile's `fogLocal` on (the fog reads the depth in the scene pass by local read), where the device has local read; `inplace`, `copy` and `buffer` turn `fogLocal` off (exported as `ICO_RD_FOG_PATH`) ([RENDER_POST.md](RENDER_POST.md#the-fog-path-and-its-self-test), [RENDER.md](RENDER.md#the-device-profile)). On desktop Vulkan `fogLocal`, `dateStencil` and `fxDstLocal` are on by default, kept by a start-up cost probe; this key, `date_path` or `fx_dst_local` set leaves the probe out ([WORK_BUFFERS.md](WORK_BUFFERS.md#the-desktops-local-read-and-its-probe)) |
| `date_path` | `stencil`: the device profile's `dateStencil` on (DATE through the stencil bit, by local read), where the device has local read; `snapshot`: off, the R8 DATE snapshot (exported as `ICO_RD_DATE_PATH`) ([RENDER.md](RENDER.md#the-device-profile)) |
| `store_plan` | `true` / `false`: the device profile's `storePlan`, on for every device class: a pass whose depth no command from it on reads or writes neither loads nor stores the depth and its stencil (DONT_CARE; a tile-based GPU skips the memory traffic, the picture is the same), and the shadow count's colour is neither loaded nor stored while the count is made ([WORK_BUFFERS.md](WORK_BUFFERS.md#the-shadow-counts-colour)); `false` keeps every load and store (exported as `ICO_RD_STORE_PLAN=1` / `0`); the profile's start line and the summary say `depth stores planned` or `depth stores kept ([dev])` ([WORK_BUFFERS.md](WORK_BUFFERS.md#the-store-plan)) |
| `blur_fold` | `true` / `false`: the device profile's `blurFold` (the staticBlur passes folded in pairs, the same bytes; on for every device class but Mali) on or off (exported as `ICO_RD_BLUR_FOLD=1` / `0`) ([RENDER.md](RENDER.md#the-device-profile), [WORK_BUFFERS.md](WORK_BUFFERS.md#the-blur-fold-on-a-wide-frame)) |
| `uint_view` | `true`: the RGBA8_UINT views of the work buffers and the scene-class targets; `work`: the work buffers' only; `false`: none; only where the backend has the view (exported as `ICO_RD_UINT_VIEW=1`, `work` or `0`) ([RENDER.md](RENDER.md#the-device-profile)) |
| `alpha_plan` | `true` / `false`: the device profile's `alphaPlan` (the alpha/blend planner: a draw whose alpha range decides its alpha test or its blend factor drawn without the pass, the test or the blending it cannot need) on or off (exported as `ICO_RD_ALPHA_PLAN=1` / `0`) ([RENDER.md](RENDER.md#the-device-profile)) |
| `fx_plan` | `true` / `false`: the device profile's `fxPlan` (the frame-size post sprites drawn through the fixed-function blender or inside the open pass where that gives the same bytes) on or off (exported as `ICO_RD_FX_PLAN=1` / `0`) ([RENDER.md](RENDER.md#the-device-profile)) |
| `fx_dst_local` | `true` / `false`: the device profile's `fxDstLocal` (a post sprite reads its destination inside the open pass by local read), where the device has local read (exported as `ICO_RD_FX_DST_LOCAL=1` / `0`) ([RENDER.md](RENDER.md#the-device-profile)) |
| `vu_planes` | `true` / `false`: the device profile's `vuPlanesFrac` (the 3D pixel shaders without the attribute planes on a target at a fractional scale; the planes kept at every whole scale) on or off (exported as `ICO_RD_VU_PLANES=1` / `0`) ([RENDER.md](RENDER.md#the-device-profile)) |
| `shadow_one` | `true` / `false`: the device profile's `shadowOne` (the shadow count resolved in one draw reading the stencil through its view, on wherever the backend has the view) on or off; `false` draws the seven stencil-tested draws and the box reduction, the A/B; `true` keeps the one draw whatever its self-test finds (exported as `ICO_RD_SHADOW_ONE=1` / `0`) ([WORK_BUFFERS.md](WORK_BUFFERS.md#the-shadow-count-in-one-draw)) |
| `shadow_bspline` | `true`: the shadow composites above 1x through the cubic B-spline (four bilinear taps a pixel), the A/B; `false`, the default on every GPU, samples them as at 1x, one bilinear tap a pixel (exported as `ICO_RD_SHADOW_BSPLINE=1` / `0`); the profile's start line and the summary say `shadow edges: b-spline ([dev])` or `shadow edges: bilinear`; only the shadows' soft edges differ, and nothing at 1x ([WORK_BUFFERS.md](WORK_BUFFERS.md#the-shadow-edges-above-1x)) |
| `dynamic_state` | Vulkan: `true`, the default, the dynamic depth (test, write, compare) and blend (enable, equation, write mask) state where the device offers it; `depth`: the depth dynamic, the blend static; `false`: both static, every pipeline with its depth and blend compiled in (exported as `ICO_VK_FAKE_NO_DYNAMIC=0`, `blend` or `1`; the `rhi_vk: dynamic state:` start line says `no ([dev] dynamic_state = false, ICO_VK_FAKE_NO_DYNAMIC=1)` or `no ([dev] dynamic_state = depth, ICO_VK_FAKE_NO_DYNAMIC=blend)` for a group it turned off) ([RHI.md](RHI.md#dynamic-state)) |
| `bind_cache` | `true`, the default: a bind or a state set that asks for what the command list already holds records nothing (the pipeline, the bind groups and their offsets, the viewport and the scissor, the vertex and index buffers, the stencil reference and the blend constant, the dynamic depth and blend commands in force, the objects a Direct3D 11 pipeline change leaves set); `false`: every one recorded as given, on every backend (a pipeline bind on Vulkan binds every descriptor set again, whatever the layouts share), the bisect for a device whose driver mishandles a kept binding (exported as `ICO_RHI_BIND_CACHE=1` / `0`); the backend's start line says `bind cache: on` or `bind cache: off ([dev] bind_cache = false, ICO_RHI_BIND_CACHE=0)`, and the skip counts of the window's 10 s line and the replay tool's `stats: bind cache:` line are then 0 ([RHI.md](RHI.md#the-bind-cache)) |
| `prerotate` | Vulkan: `true`, the default: on a surface the system shows turned (an Android phone held sideways reports the current transform ROTATE_90 or 270) the swapchain is made in that turn (its preTransform) with its images the window's size turned, and the present draws its picture at the window's size and turns it into the image in one last pass, so the compositor shows the images as they are instead of turning each one on the GPU; `false` keeps the identity transform, the compositor turning every image (exported as `ICO_VK_PREROTATE=1` / `0`). The lines: `rhi_vk: swapchain WxH pre-rotated 90 (the surface's transform 0x2; the compositor no longer rotates): images HxW, ...` and `rd: present: the WxH picture turned 90 degrees into the swapchain's HxW images in one pass ...`, or with the key false `rhi_vk: surface transform 0x2 (offered 0x1ff): presenting with identity, the compositor rotates ([dev] prerotate = false, ICO_VK_PREROTATE=0)` ([DISPLAY.md](DISPLAY.md#the-turned-swapchain)) |
| `gpu_checkpoints` | `true`: GPU checkpoints, resource addresses (Vulkan) and DRED (D3D12) for the device-loss report (exported as `ICO_GPU_CHECKPOINTS=1`) ([RHI.md](RHI.md#device-loss)) |
| `gpu_robust` | `true`: robust buffer and image access (Vulkan; `VK_EXT_robustness2` where offered; exported as `ICO_GPU_ROBUST=1`), with null descriptors; `false`, the default on every platform, exports `0`. The device-loss order's `robust` step exports `1` for its run whatever the key says. With the key unset, Android's next start after a device loss exports `1` for that start, and the game writes `true` to `config.toml` itself after the second loss in a row (`port/platform/window_host.c` (`gpu_robust_persist`)); with the key set, it writes nothing. The key the game wrote carries the mark `gpu_robust_by = "losses; <version>; <clean starts>; <run or ->"` beside it (`port/platform/robust_persist.h`, `window_robust.c`): a start that reached the game (the start-up set made, or the self-tests done on a run that makes its pipelines on first use) and ended without a device loss, an end while preparing the graphics or a watchdog end (the game stopped responding: the `-watchdog.txt` mark beside the activity file, `ico_diag_startup_prev_watchdog`) counts one clean start, a loss or such an end sets the count back to 0, and after 3 clean starts in a row, or at the first start of another version, both lines are removed from `config.toml` and the start draws as the profile says (`ico_pc: robust access on (set by the game after 2 lost starts on <version>): N clean starts of 3 before it turns off`, then `ico_pc: robust access turned off (...)`). A `gpu_robust` without the mark (set by hand, or left by a game that wrote no mark) is never touched or dropped, and every start says so: `ico_pc: [dev] gpu_robust = true in <config.toml> has no gpu_robust_by mark: set by hand (or by a game that wrote no mark), so it is never dropped; it costs bounds checks on every 3D load (delete the line to turn it off)` (`robust_unmarked_line`; issue 179's reporter carried such a key) ([RHI.md](RHI.md#device-loss), [PLATFORM.md](PLATFORM.md#diagnostics)) |
| `pass_split` | N: a render pass of VU draws ends and begins again after every N of them, the picture the same (exported as `ICO_RD_PASS_SPLIT`; 0 or `off`, the default: none); not in a multisampled pass, a storage pass, the exact blend's readable pass or a DATE draw through the stencil bit ([RENDER.md](RENDER.md#the-loss-bisect)) |
| `attachment_read` | `true`: the attachment read offered on Qualcomm's Adreno driver too, where it is off because it draws wrong colours (exported as `ICO_VK_ATTACHMENT_READ=1`) ([RENDER.md](RENDER.md#destination-reads-and-blend-paths)) |
| `storage_pass_merge` | `true`: the storage destination read (the interlock or ROV path) keeps an open storage pass open and draws in it, through the read, the draws it does not need the read for when the shader stores the same bytes, instead of ending the pass for each of them (exported as `ICO_RD_PASS_MERGE=1`; `false`, the default, exports `0`: every such draw ends the storage pass) ([RENDER.md](RENDER.md#the-storage-pass-merge)) |
| `vu_plain_only` | retired: read and ignored, one line a load (`ico_pc: [dev] vu_plain_only = <value> is not used (every device compiles one set of 3D shaders); ignored`; `port/platform/host_config.c` (`retired_dev`, `ico_ini_export`)), not exported. Every device compiles one VU module set; the fallback's plain or extended program entries are the renderer's table's (`RhiLimits.vuFallbackPlain`) or `ICO_RD_VU_PLAIN` / `ICO_RD_VU_EXTENDED` from the environment ([RENDER.md](PIPELINES.md#the-fallback-entries)). The game writes it nowhere; a file that holds `true` from an end inside an extended VU batch draws as without it |
| `vu_extended` | retired: read and ignored with the same line, not exported ([RENDER.md](PIPELINES.md#the-fallback-entries)) |
| `vu_set` | the VU vertex shaders' set for the program entries (the 3D draws without the vertex pre-pass: with it off, after a failed self-test, or a draw the record could not take): `auto`, the default, the device table's (the plain set on Qualcomm's own Adreno 740 driver and under Turnip, the extended one elsewhere); `plain` the plain set on any device, as a device-table row would give it (no wide-screen card test and no tick cameras on those draws: a wide picture's edge at the old 4:3 border may open, and the pictures between two ticks take the blended camera's rule alone); `extended` the extended set whatever the table says (exported as `ICO_RD_VU_SET`; the older `ICO_RD_VU_PLAIN=1` and `ICO_RD_VU_EXTENDED=1` ask the same). The start line says which: `rd: VU shaders: plain set by [dev] (vu_set = plain: ...)` or `rd: VU shaders: extended set ...`; the summary line's `VU shaders one set, fallback plain ([dev] vu_set = plain)`. The A/B of the two sets' vertex cost on a phone ([SHADERS.md](SHADERS.md#the-variants)) |
| `sheet_shader` | `true`: the menus' text drawn through the sheet shader at every scale, as a comparison (exported as `ICO_RD_SHEET_SHADER=1`; `false`, the default: its texels baked on the CPU at 1x and 2x, the same bytes, four reads a pixel: at 1x everywhere, at 2x on the tile-based GPU classes only, the others drawing 2x through the shader, issue 126). The start line says `rd: text: menu text baked at 1x and 2x ...`, `rd: text: menu text baked at 1x; 2x through the sheet shader ...` or `rd: text: menu text through the sheet shader at every scale ...`, the summary line `menu text baked at 1x and 2x (the shader above)`, `menu text baked at 1x (the shader above)` or `menu text shader` ([SHADERS.md](SHADERS.md#sheet_texthlsli)) |
| `acquire_ahead` | `false`: with the render thread submitting (Direct3D 11; no submit thread), the next picture's wait for room in the present queue (the frame latency wait) stays in its acquire, inside its reads, as the A/B (exported as `ICO_RD_ACQUIRE_AHEAD=0`); `true`, the default: it is made at the end of the picture before, after its present, outside the hand-over, so the game thread's calls do not wait through the display and the next present waits for what is left as the display's wait (issue 126). The start line says `rd: render thread: the next picture's wait for room in the present queue ... is made at the end of the picture before ...` or `... waits for room in the present queue inside its reads (ICO_RD_ACQUIRE_AHEAD=0 ...)`, the 10 s line `the display's wait for room in the present queue made at the end of the picture before N times ...` where the backend has the wait (`port/render/rd_thread.c`; [PLATFORM.md](PLATFORM.md#the-render-thread)) |
| `original_async` | `false`: with Frame rate Original each frame's picture is replayed and presented inside the game step, the game thread waiting for it, as the A/B (exported as `ICO_RD_ORIGINAL_ASYNC=0`); `true`, the default: with the render thread on it is handed over as the other frame rates' pictures are, its submit and present on the submit thread, and a frame is left out while the render thread or the submit thread is still busy with the picture before (issue 179). The start line says `rd: render thread: with Frame rate Original each game frame's picture is handed over as the other frame rates' are ...` or `... is replayed and presented inside the game step, the game thread waiting ([dev] original_async = false)` (`port/render/rd_thread.c`; [DISPLAY.md](DISPLAY.md#pacing) path 1) |
| `aura_kernel` | `gs`: the aura's feedback read of the picture (above 1x the area box, 4 x 4 GS pixels a feedback texel) drawn with the GS's own taps instead, a measurement of the box's cost on a device with `perf_passes`, the picture not the default's (exported as `ICO_RD_AURA_KERNEL=gs`; `box`, the default, exports `box`); logged once, `rd: aura: the feedback read of SCENE into FEED128 drawn with the GS's own taps ...` ([WORK_BUFFERS.md](WORK_BUFFERS.md#the-auras-feedback-read)) |
| `aura_box` | the aura's feedback read of the picture into its feedback buffer above 1x (the area box, about 20 x 10 scene texels a feedback texel at a phone's Resolution Auto) drawn in two passes: the box's rows into an integer target of the feedback buffer's width and the picture's height, then the feedback buffer's draw summing them; the same bytes as the one pass, one long loop a texel made 229 k short ones and 27 k shorter ones. `auto`, the default, is the device profile's (on on a tile-based GPU: Adreno, Mali, PowerVR, Apple; off on the others); `on` and `off` (exported as `ICO_RD_AURA_BOX=1` or `0`; `auto` exports `auto`) set it. The start line says `rd: aura box: the feedback read of the picture above 1x in two passes (...) (<why>)` or `... in one pass (<why>)`, the profile line ends `; aura box: two passes` or `; aura box: one pass`, the 10 s summary `rd: aura box: N feedback reads in two passes (ROWBOX WxH), M in one pass (...)`, and the pass list names its pass `aura ROWBOX WxH` ([WORK_BUFFERS.md](WORK_BUFFERS.md#the-auras-feedback-read)) |
| `pass_area` | a render pass on the picture whose draws are all flat sprites and shapes with a known place (the screen prims, the aura's paste) loads and stores only the 32 x 32 tiles they touch: its render area, the same bytes (no draw reaches outside it, and the pixels outside are left as they are). `auto`, the default, is the device profile's (on on a tile-based GPU: Adreno, Mali, PowerVR, Apple; off on the others, where Direct3D and OpenGL ignore the area anyway); `on` and `off` (exported as `ICO_RD_PASS_AREA=1` or `0`; `auto` exports `auto`) set it. The start line says `rd: pass area: on (<why>): ...` or `rd: pass area: off (<why>)`, the profile line ends `; pass area: on` or `; pass area: off`, the first 8 kinds of pass a run that took an area are named (`rd: pass area: SCENE in list 8 (the aura's sprite): WxH at X,Y of 2364x1080 (N of 2516 tiles loaded and stored; ...)`), and the 10 s line `window: per replay ...` ends `P passes with an area (M % of the picture's tiles loaded, K ended early)` ([WORK_BUFFERS.md](WORK_BUFFERS.md#the-pass-area)) |
| `clear_carry` | a pending clear stays pending past the end of a pass for a source's barrier and is taken as the next pass's load on its target (the reduction's DISPLAY clear: one pass fewer a picture), the same bytes. `auto`, the default, is the device profile's (on on a tile-based GPU; off on the others); `on` and `off` (exported as `ICO_RD_CLEAR_CARRY=1` or `0`; `auto` exports `auto`) set it. The start line says `rd: pass cut: a clear carried past a source's barrier into the next pass on its target: on (<why>)`, the first carried clear taken is named once, and the 10 s line counts `C clears taken by the next pass` in its `passes cut:` clause ([WORK_BUFFERS.md](WORK_BUFFERS.md#passes-cut)) |
| `clear_skip` | a clear of AURA_WORK or AURA_TAP whose only reader in the replay is the sprite the constant read drew in its colour is not drawn, and drawn later where anything reads the target before clearing it again, the same bytes. `auto`, the default, is the device profile's (on on a tile-based GPU); `on` and `off` (exported as `ICO_RD_CLEAR_SKIP=1` or `0`; `auto` exports `auto`) set it. The start line says `rd: pass cut: a clear nothing reads before it is cleared again not drawn: on (<why>)`, each clear not drawn is named (the first 8, then every 100th), and the 10 s line counts `S clears skipped` ([WORK_BUFFERS.md](WORK_BUFFERS.md#passes-cut)) |
| `last_store` | the pass that holds a depth's last touch does not store the depth when the walk is known to keep that pass open until the touch is done (the fog's pass on the phone), the same bytes. `auto`, the default, is the device profile's (on on a tile-based GPU); `on` and `off` (exported as `ICO_RD_LAST_STORE=1` or `0`; `auto` exports `auto`) set it. The start line says `rd: pass cut: the depth not stored by the pass holding its last touch: on (<why>)`, the 10 s line counts `D depth stores dropped at the last touch` (among the `stores:` clause's depth stores dropped), and a pass that ends earlier than promised turns it off for the run with its line ([WORK_BUFFERS.md](WORK_BUFFERS.md#passes-cut)) |
| `vu_prepass` | `false`: no VU vertex pre-pass, every 3D (VU) draw evaluates its triangle's corners in the triangle's own vertex shader, as a comparison on a device (exported as `ICO_RD_VU_PREPASS=0`; `true` is the default and is never written, which exports nothing: each mesh and grid vertex evaluated once before the lists, where its start-up self-test passes (the record is written by the vertex shaders into a buffer wherever the driver offers vertex stores, `vu_record` below, else held in the device's colour targets), on every device, tile-based GPUs included, but the Adreno 740 under Qualcomm's own driver, which the device table keeps off (issue 123: `rd: VU pre-pass: off by the device table (...)`, [PIPELINES.md](PIPELINES.md#the-vertex-and-triangle-pre-passes)); `true` written in `config.toml` exports `on`, which also passes that device table row, for an A/B there (the start line then ends `; [dev] vu_prepass = true over the device table (<reason>)`), never the colour-target bound or a restart's step without it; the self-test replays its frame four times across both frame slots and a failure logs `rd: VU pre-pass: off (self-test: ...)` and draws the old way for the run). The game writes `false` to `config.toml` itself after the second start in a row that ended without a word, in the foreground, while the graphics driver compiled the pre-pass's pipelines; the first such end keeps only the next start without it, writing nothing; a restart after a driver crash in one of its pipelines or after a device loss draws without it for that run only ([PLATFORM.md](PLATFORM.md#diagnostics), [RENDER.md](PIPELINES.md#the-vertex-and-triangle-pre-passes)) |
| `vu_prepass_batch` | `false`: the VU pre-pass draws one point draw for each command and stage it records, as before its batches, through the pre-pass entries that read their VuCB as a uniform block, as a comparison (exported as `ICO_RD_VU_PREPASS_BATCH=0`; `true`, the default, exports `1`: one point draw a run of up to 64 entries with the same stage, FrameCB and group 1, the picture the same byte for byte). The start line `rd: VU pre-pass: on: ...` ends `its draws batched: ...` or `its draws one an entry and stage ([dev] vu_prepass_batch = false, ICO_RD_VU_PREPASS_BATCH=0)`, and the summary line says `pre-pass draws batched` or `pre-pass draws one an entry` ([VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-pre-passs-batches)) |
| `vu_record` | how the VU pre-pass writes its record (each mesh and grid vertex and triangle evaluated once before the lists): `auto`, the default, stored by the pre-pass's vertex shaders straight into the record's buffer wherever the device can (Vulkan's `vertexPipelineStoresAndAtomics`; no render targets, no copies), else stored by compute dispatches wherever the device takes compute work (Vulkan; the Mali-G52 of issue 179: no render targets, no copies, no passes), else drawn into three render targets and copied; `targets` the render targets and copies on any device; `stores` the stores where the device has them (else auto's next rung); `compute` the compute dispatches where the device has them (else the targets) (exported as `ICO_RD_VU_RECORD`). The picture is the same every way. The start line `rd: VU pre-pass: on: ...` ends with `the record stored by the vertex shaders ...`, `the record stored by compute dispatches ... (<why>)` or `the record drawn into 3 RGBA32_UINT render targets and copied ... (<why>)`; the self-test's verdict line ends `, record stores`, `, record compute` or `, record targets`, and a failure falls back one rung at a time (stores, compute, targets) and runs again, every verdict logged; the summary says `(self-test: ok; record compute)` and the like ([PIPELINES.md](PIPELINES.md#the-vertex-and-triangle-pre-passes)). The game writes `targets` to `config.toml` itself after the second start in a row that ended without a word, in the foreground, while the graphics driver compiled a store or compute build's pipelines (the first such end keeps only the next start on the targets, writing nothing); a restart after a driver crash in a store or compute build's pipeline, or after a device loss in a run drawing with the stores or the compute dispatches, draws with the targets for that run only, the pre-pass kept ([PIPELINES.md](PIPELINES.md#the-record-stores-restart-rung)) |
| `vu_prepass_min` | a frame whose VU pre-pass would record fewer than this many vertex and triangle slots draws every 3D (VU) command the old way, the same picture (exported as `ICO_RD_VU_PREPASS_MIN`; `0`, the default, never skips; the start-up self-test's frames are never skipped). The pre-pass's fixed cost (two passes, and with the render targets six copies) is worth it only past some thousands of vertices on a vertex-bound GPU: a value for a phone comes from its log's VU line. The window's 10 s VU line ends `; pre-pass skipped N frames under M slots` while it skips ([PIPELINES.md](PIPELINES.md#the-vertex-and-triangle-pre-passes)) |
| `velocity_test` | `true`: the temporal upscaler's motion vectors drawn, a developer switch (FSR 3.1 turns the pass on by itself; this key asks for it without the upscaler, no row): every VU mesh, skinned and grid draw carries its previous picture's transform and the replay draws VELOCITY after list 0 (exported as `ICO_RD_VELOCITY=1`; `false`, the default, exports `0`: nothing of it runs, the pictures the same bytes). The start line `rd: velocity: on ([dev] velocity_test, ICO_RD_VELOCITY=1) ...`, at the first need `rd: velocity: VELOCITY made, WxH RGBA16F ...`, and every 10 s `rd: velocity: N pictures, VU draws D: with their previous transform P ...` ([RENDER.md](RENDER.md#velocity)) |
| `velocity_sign` | `-1`: the temporal upscaler takes the motion vectors negated, previous to current (exported as `ICO_RD_VELOCITY_SIGN=-1`; `1`, the default, current to previous, the convention FidelityFX's upscaler takes); the shader's VELOCITY is the same either way. For the desktop check of the sign with FSR's debug view. The log says `rd: velocity: [dev] velocity_sign = -1 ...` at its first use ([RENDER.md](RENDER.md#velocity)) |
| `upscale_test` | `true`: the upscale point's test, a developer switch (no row): the scene drawn at the Resolution row's scale over 1.5 (FSR's Quality ratio, not below 1x), upscaled where the game's reduction reads it by a plain bilinear pass into UPSCALED at the row's scale, which the reduction reads in its place; supersampling, MSAA and Downsampling off, the fades fold off, the motion vectors on (as `velocity_test`). Exported as `ICO_RD_UPSCALE_TEST=1` (`false`, the default, exports `0`: nothing of it runs, the pictures the same bytes). The start line `rd: upscale point: [dev] upscale_test: the scene at the display scale over 1.50, upscaled at the reduction by the test's bilinear upscale (a developer test of the path)`; the display line adds `(render scale r), display WxH (scale d; ...)`; `rd: upscale point: SCENE WxH ... -> UPSCALED wxh ...` at the first dispatch; `rd_replay_tool --upscale-test [R]` does the same for a dump ([RENDER.md](RENDER.md#the-upscale-point), [DISPLAY.md](DISPLAY.md#the-render-and-display-scales)) |
| `upscale_debug_view` | `true`: with Upscaling = FSR 3.1 in force, FSR draws its debug view in place of the picture (`FFX_UPSCALE_FLAG_DRAW_DEBUG_VIEW`: the motion vectors, the depth and the masks it builds), the desktop check of the motion vectors' sign (`velocity_sign`) and of the depth. Exported as `ICO_RD_UPSCALE_DEBUG_VIEW=1`; one line `rd: upscaling: FSR 3.1 draws its debug view ...` at the first dispatch ([DISPLAY.md](DISPLAY.md#fsr-31-upscaling)) |
| `upscale_debug_checks` | `true`: FSR 3.1's context is made with `FFX_UPSCALE_ENABLE_DEBUG_CHECKING`: it checks the inputs each dispatch and says what it finds in the log (`rd: upscaling: FidelityFX warning: ...`, the first 8 then every 100th). Exported as `ICO_RD_UPSCALE_DEBUG_CHECKS=1`; the context's line ends `, debug checks on` ([DISPLAY.md](DISPLAY.md#fsr-31-upscaling)) |
| `display_layer` | `false`: under a temporal upscaler (FSR 3.1, `upscale_test`), the game's 2D layer (lists 11-12: the brightness, fades, letterbox, film noise, save tile, subtitles and text) is drawn into the scene and upscaled with it, and a keep frame (a stage switch's fade) sends the darkened picture through the upscaler again each tick: the path without the display layer, for an A/B (exported as `ICO_RD_DISPLAY_LAYER=0`; `true`, the default, or the key absent leaves it on: the 2D layer drawn over the upscaled picture at the display scale and keep frames held 1:1 with no dispatch). Nothing changes without a temporal upscaler. The log says `rd: display layer: off ([dev] display_layer = false): lists 11-12 are drawn into SCENE and upscaled with it (the path before the layer, for an A/B)` once, and the fades fold's line its second form ([DISPLAY_LAYER.md](DISPLAY_LAYER.md)) |
| `vu_merge` | `false`: every VU draw a GPU draw of its own, through the programs' and the main shader's builds that read their VuCB as a uniform block, as before the draw merge, as a comparison (exported as `ICO_RD_VU_MERGE=0`; `true`, the default, exports `1`: consecutive mesh and grid draws with the same pipeline, textures, DrawCB, FrameCB and scissor drawn as one indexed draw of up to 16, the picture the same byte for byte; on Direct3D 11 too, its builds compiled at their first pipeline). The start line is `rd: VU merge: on: ...` or `rd: VU merge: off ([dev] vu_merge = false, ICO_RD_VU_MERGE=0): one GPU draw a VU draw`, and the summary line says `VU merge on` or `VU merge off` ([VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-draw-merge)) |
| `vu_fuse` | `false`: code 36's cut and kick draws of a batch under their own pipelines, as before the fuse, as a comparison (exported as `ICO_RD_VU_FUSE=0`; `true`, the default, exports `1`: where the blend is a lerp or Cd*FIX + Cs and the two plans agree, the kick drawn under the cut's pipeline with its pixels written unblended, so the draw merge joins the pair and the batches after it, the picture the same byte for byte; off whenever the draw merge is). The start line's `rd: VU merge:` words end with `code 36's cut and kick draws of a batch fused: ...` or `the fuse off ([dev] vu_fuse = false, ICO_RD_VU_FUSE=0): ...`, and the summary line says `VU fuse on` or `VU fuse off` ([VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-fused-scissor-pair)) |
| `vu_bones` | `ring`: the skinned draws' bones read through the ring (a storage buffer) instead of a uniform block, at every start (exported as `ICO_RD_VU_BONES=ring`; `port/render/rd_pipeline.c` (`rd__vu_policy_init`) puts the cluster entries' ring builds in place of their plain variants, used only where the program entries are the plain set, Turnip's always); `cbuffer` or absent: the uniform block. The same picture byte for byte (`rd_vu_bones_ring_ab`). The skin check writes `ring` itself, issue 123's ladder step 1, when the twins of a skinned draw differ and the ring read beside the picture clears it, with the mark `vu_bones_by = "vu_skin_check <version> frame F; D of T twins differed"` (exported as `ICO_RD_VU_BONES_BY` for the start line `rd: VU bones: through the ring (storage buffer) on the skinned entries ([dev] vu_bones = ring, written by the skin check at <version> frame F; ...)`; without the key `rd: VU bones: through VuBoneCB`), and the window says `window: VU bones: [dev] vu_bones = ring is written (the skin check saw N twin events at bone address A over P presents); ...; in force from the next picture` and puts it in force in the same run (the choice handed to the renderer, `rd_set_vu_bones`, the renderer made again in place as Options > Graphics > Apply does: `window: VU bones: switching to the ring read now ...`; not during a movie or the run's first 30 s, once a run; only where the program entries are the plain set on Vulkan, else the ladder waits and writes nothing); the key stays until deleted ([VU_SKIN_CHECK.md](VU_SKIN_CHECK.md#the-ladder)) |
| `turnip_debug` | Android, a Turnip driver package only, from the next start (the driver library is loaded once a process): `nobin` sets `TU_DEBUG=nobin` in the process environment before the package's library (`libvulkan_freedreno.so`) loads (`port/platform/android/gpu_driver_android.c`, `tu_debug_apply`; the line `gpu driver: TU_DEBUG=nobin set before ... loads ...`, or `... TU_DEBUG is not set` with another package's library); `retest`: not set at this start, the skin check re-testing whether nobin is needed; `off`: not set, and the ladder stops. `nobin` sets the variable only when its mark `turnip_debug_by` ends with the word `trial` or `needed`; a `nobin` without either (an older ladder's, or set by hand) runs as `retest` at the next start (`gpu driver: TU_DEBUG=nobin not set at this start ([dev] turnip_debug = nobin without the word trial or needed, taken as retest ...)`). Not an environment switch of the port's own. The skin check writes `nobin` itself, issue 123's ladder step 2, after 8 twin events with the bones through the ring (or when the ring read did not clear them twice), with the mark `turnip_debug_by` in `vu_bones_by`'s form ending `; trial`, and `off` when they still differ under it ("both tried, neither cleared the twins"); healed under it with `vu_bones = ring` in force (2,000 presents with no twin differing) it writes `retest`; 2,000 clean presents at the re-test remove the key and its mark (left empty), 8 twin events there write `nobin` back ending `; needed`, which is kept and never re-tested. The start line is `window: VU skin check ladder: step 2 of 2 (TU_DEBUG=nobin) for this start; step 1 (ring read) did not clear the twins (D of T)`, or `window: VU skin check ladder: the re-test of step 2 at this start: TU_DEBUG=nobin not set (...)`. Whether the variable reaches the driver through the package loader is not verified: the twins under it are the indicator ([VU_SKIN_CHECK.md](VU_SKIN_CHECK.md#the-ladder), [ANDROID_DRIVERS.md](ANDROID_DRIVERS.md)) |
| `vu_skin_check` | `false`: the skinned draws' bone-block check off, as a comparison (exported as `ICO_RD_VU_SKIN_CHECK=0`), its twins and its capture with it; `true`: on where it is off by default, on any Vulkan device (exports `1`). On an Adreno with a window the default twins every skinned draw (Qualcomm's driver and Turnip alike) and, on Turnip, probes once a present; the environment alone (no key) sets `ICO_RD_VU_SKIN_INTERVAL_MS` (the probe's interval, `off` for the targeted probes alone) ([VU_SKIN_CHECK.md](VU_SKIN_CHECK.md#switches)). The default: on only on an Adreno with a window (Qualcomm's driver or Turnip, issue 163's devices), off elsewhere (issue 179's Mali-G52 is GPU-bound). On, one skinned draw every 100 ms, in turn, is drawn again after the picture as a probe into a target of its own through the vertex module and the bone block offset its draw used, with two variants beside it, and compared with the CPU's cluster program; its bone block's bytes as the GPU copied them and as the CPU reads them after the fence are compared with the bytes written; designed not to change the picture (`rd_vu_skin_check_ab` compares the dumps' pictures with it on and off byte for byte on lavapipe); issue 163. The start lines are `rd: VU skin probe: ok (...)` (the start-up probe) and `rd: VU skin check: on (...; on by default on an Adreno with a window (Turnip); ...)`, or `rd: VU skin check: off (not an Adreno: ICO_RD_VU_SKIN_CHECK=1 / [dev] vu_skin_check = true turns it on)` (`off ([dev] vu_skin_check = false ...)` when set); a wrong check logs `rd: VU skin check: frame F command L:I mesh ...: N vertices wrong ...` and its `variants:` line, and the 10 s line is `rd: VU skin check: P checks, W wrong, B blocks whose bytes differed ...` ([VU_SKIN_CHECK.md](VU_SKIN_CHECK.md)) |
| `cull` | the scene draws' visibility test, exported as `ICO_RD_CULL`: `full` (the default on an immediate-mode GPU; `true` too) is `frustum` with the occlusion test; the default on a tile-based GPU (a phone: the device profile's `tiler`) is `frustum`, the occlusion test off, because it cost 6.8 ms of a Mali-G52 phone's CPU a picture and hid 0 to 1 draws (issue 179), and `cull = full` turns it on there; any value set here rules on both kinds, and `off` (`false` too) `frustum` leaves out of the replay every mesh, skinned or grid command of lists 0 to 10 whose vertices all lie beyond one side of its target or behind the eye, the picture the same byte for byte; the occlusion test also leaves out a mesh or grid draw of list 0 wholly behind the depth the opaque draws before it wrote (`ICO_RD_CULL_HIZ_TRIS` occluder triangles a picture, 12000 by default; a second start line says so); replays every draw, as a comparison. The start line says `rd: cull: full ([dev] cull, ICO_RD_CULL): ...` (on a phone's default `rd: cull: frustum ([dev] cull, ICO_RD_CULL; the profile's default: the occlusion test costs more on this chip's CPU than it saves, [dev] cull = full turns it on): ...`, the profile line `rd: profile: ... cull frustum (the profile: ...)`), the summary line `cull full`, and the backend's 10 s line `; cull: N of M scene draws skipped a replay (F frustum, O occluded), T ms (occlusion H) ([dev] cull)` (`(profile default)` in place of `([dev] cull)` when the profile chose the mode), under `full` followed by the occlusion test's parts (`; occlusion: collect C ms (...), raster R ms (...), test X ms (...); occluders used U, refused: ...`) ([RENDER.md](RENDER.md#culling)) |
| `region_rule` | what the VU draws of a triangle its region test fails ([VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-region-rule), [DIVERGENCES.md](DIVERGENCES.md) VD-29), exported as `ICO_RD_REGION_RULE` and read at each renderer start: `camera` (the default) the camera decides in every preset and state, the Original preset at 4:3 and 1x included: a triangle the PS2 drops (a vertex out of range or behind the eye, a card) is drawn clipped over the whole frame when it faces the eye and stays dropped when it faces away, a sprite outside the VU's window is drawn whole; `ps2` the PS2's drops in every preset (out of the box the console's picture byte for byte; elsewhere the card rule and the side bands beside a wide picture), the A/B and the escape. The start line says `rd: region rule: the camera decides (...)` or `rd: region rule: the PS2's (...)`; the 10 s line `; region rule: the camera's: N VU draws a replay drawn by the camera rule` |
| `jitter_test` | `"x,y"`: the scene's sub-pixel jitter on every frame, x right and y down in texels of the scene's targets, each -0.5 to 0.5 (exported as `ICO_RD_JITTER_TEST`; anything else is not exported). The positions move by it after the GS's own snap to its 1/16 grid, in the passes on SCENE, WORK2 and AURA_WORK alone: the work buffers, the reduction into DISPLAY, the motion blur's copy and the present are not moved. A developer A/B of the jitter FSR 3.1 sets per present by itself (this key moves every frame by a fixed amount, no row): `0,0` draws the picture without it byte for byte. The start line says `rd: jitter: X, Y texels on the scene-class passes (SCENE, WORK2, AURA_WORK) of every frame ([dev] jitter_test, a developer A/B)`, or `rd: jitter: ICO_RD_JITTER_TEST=... is not x,y with each -0.5..0.5 ([dev] jitter_test); no jitter`; `rd_replay_tool`'s `--jitter x,y` does the same for a dump ([RENDER.md](RENDER.md#jitter)) |
| `constant_reads` | `false`: a post sprite whose source target holds nothing but its last clear reads the target as before, as a comparison (exported as `ICO_RD_CONSTANT_READS=0`; `true`, the default, exports `1`: such a sprite, the aura's read of AURA_WORK with no aura object drawn into it, is drawn untextured in the colour the texture function makes of the clear's, the picture the same byte for byte, the 2.5 Mpix read gone at 2366 x 1080). The summary line says `constant reads on` or `constant reads off`; the first sprite drawn so logs `rd: constant reads: a sprite into WORK0 read AURA_WORK, which held only its clear (...)` once, and the 10 s pixel diet line counts them a replay ([WORK_BUFFERS.md](WORK_BUFFERS.md#a-cleared-source-read-as-its-colour)) |
| `crack_fill` | `false`: the crack filler off: above 1x under the Enhanced preset the pixels no draw covered between two drawn ones (the 1/16 GS pixel cracks between parts of the world) keep the clear's depth and take the fog's colour, as before it (exported as `ICO_RD_CRACK_FILL=0`; `true`, the default, exports `1`: they take the nearer drawn neighbour's colour after lists 4-6); the start line says `rd: crack fill: off ([dev] crack_fill = false, ICO_RD_CRACK_FILL=0)`; unset, it is on on every GPU, drawn after lists 4-6 on every device (the state line's `takes the nearer end's colour after lists 4-6`), and its GPU time is the `crack fill` effect of the 10 s `effects:` line ([RENDER_POST.md](RENDER_POST.md#crack-filling-above-1x)) |
| `perf_passes` | `true`: every replay timed pass by pass (a GPU timestamp between every two pieces of its work, outside the render passes) and each piece written to `ico-pc-passes.csv` in the logs folder (exported as `ICO_RD_PERF_PASSES=1`; `false`, the default, exports `0`: one replay timed so every 250 ms, for the 10 s `window: GPU top passes` line) ([RENDER.md](RENDER.md#the-per-pass-timing), [DISPLAY.md](DISPLAY.md#the-perf-csv)) |
| `still_represent` | `false`: every between-tick picture is rendered, as before the re-show (exported as `ICO_RD_STILL_REPRESENT=0`; `true`, the default, exports `1`): with it on, a between-tick present whose frame would draw the bytes of the tick's last rendered present is shown again without a replay, the picture the same byte for byte, the A/B for that claim ([DISPLAY.md](DISPLAY.md#interpolation-between-ticks)). The 10 s `interp:` line `still re-present ([dev] still_represent = true): N of M presents shown again ...` and the `window:` uploaded-KB line's `; N presents re-shown without a replay` name it |
| `glow_redraw` | `true`: every between-tick present draws the glow (the flare and the bloom) again (the same draw as `[video] effect_glow_mode = "smooth"`, which the Glow between frames row writes; `glow_redraw` is the A/B override and wins over the row when the key is in the file, `false` holding the glow even under a Smooth row), as before the hold (exported as `ICO_RD_GLOW_REDRAW=1`; `false`, the default, exports `0`): with it off, a tick's later presents paste the glow its first present drew, the same bytes on a still tick and half a tick behind the scene during a pan, the A/B for that change ([DISPLAY.md](DISPLAY.md#interpolation-between-ticks), [WORK_BUFFERS.md](WORK_BUFFERS.md#the-glow-between-ticks)). The 10 s `interp:` line `glow between ticks: held: ...` or `glow between ticks: redrawn ([dev] glow_redraw = true): ...` and the `window:` uploaded-KB line's `; N between-tick pictures pasted the tick's glow without its chain` name it |
| `glow_fold` | `false`: the glow's mask is drawn into WORK2 and read by its reduction (exported as `ICO_RD_GLOW_FOLD=0`; the default on a tile-based GPU); `true` exports `1` (the default on an immediate-mode GPU): the reduction computes the mask in its box from SCENE's alpha, WORK2 neither drawn nor read, one full-frame pass fewer a glow picture, the picture the same byte for byte, even where the renderer has no output (the replay tool's `--pass-dump` and `--target`, whose WORK2 then holds no mask). Not set, the device profile decides wherever the frame is presented (`RdProfile.glowFold`, [RENDER.md](RENDER.md#the-device-profile)): on on desktop Vulkan, D3D12, D3D11 and OpenGL, off on every tile-based GPU (Adreno, Mali, PowerVR, Apple, another tiler), where the folded reduction cost the PowerVR phone about 8 ms a glow picture more than the mask's pass it saved; the profile's start line says `glow fold on` or `glow fold off`, with `([dev])` where this key set it. The log says the first fold once (`rd: glow: WORK2 fold: the glow's mask computed inside its reduction ...`) and each reason a glow frame keeps the chain once (the sun's fans or the lamps drawn into WORK2, ...); the 10 s line `window: glow: WORK2 fold: N folded, M kept (reason)` counts them ([RENDER_POST.md](RENDER_POST.md#the-glows-mask-in-its-reduction)) |
| `perf_list_stamps` | `false`: no list or effect timestamp, so none is written inside an open render pass; the replay's GPU total (presents), the uploads' and the pre-pass's times and the per-pass timing stay, the lists' and the effects' times read 0 (exported as `ICO_RD_PERF_LIST_STAMPS=0`; `true`, the default, exports `1`): the A/B of what those stamps cost a tile-based GPU ([RENDER.md](RENDER.md#the-per-pass-timing)) |
| `joystick_directinput` | `false`: SDL's DirectInput controller scan off on Windows (`SDL_HINT_JOYSTICK_DIRECTINPUT` = 0 before `SDL_Init`; exported as `ICO_JOYSTICK_DIRECTINPUT=0`): the test for a pump held by that scan after a controller change (issue 105); XInput, HIDAPI and Raw Input controllers are still found, a device only DirectInput knows is not. No effect elsewhere (a line says so) ([PLATFORM.md](PLATFORM.md#diagnostics)) |
| `present_fallback` | the FIFO fallback, Vulkan only: `auto`, the default: on Android, on Qualcomm's own driver (vendor id `0x5143`, text naming neither `Mesa` nor `turnip`) with vsync off the first swapchain is already FIFO with the surface's fewest images (`window: present fifo (Qualcomm's own driver: FIFO from the start; [dev] present_fallback = off keeps mailbox)`; Qualcomm's Adreno guide advises FIFO with the minimum image count; this is pacing and heat hygiene, the stalls of issue 123 continued under FIFO in three logs, so it is not expected to remove them); with vsync off and the swapchain in mailbox mode (Android offers no immediate mode) on that same driver, 3 presents that each spent 200 ms or more in the driver's present call within 30 s make every later swapchain FIFO with the surface's fewest images for the rest of the run, once; not on a Mesa driver (Turnip, RADV: the driver text names `Mesa` or `turnip`) and not on any other vendor's (a Mali-G52 got longer blocks from the switch, issue 179): there the blocks are counted and `window: present blocked 3 times (204-240 ms) in 8 s on ADAPTER (DRIVER) under mailbox: mailbox kept (presents go FIFO by themselves only on Qualcomm's own driver, where the blocks were measured; [dev] present_fallback = fifo asks for FIFO here)` is written once; the vsync option stays Off and `config.toml` is not written (`port/platform/present_fallback.h`, `port/platform/window_present_fallback.c` (`window_fifo_note`, `window_fifo_apply`)). `off`: mailbox kept for the whole run, whatever the driver and the blocks (the A/B against `auto` on Qualcomm's driver); `fifo` on another driver is the A/B the other way, the line `window: present blocked 3 times over 200 ms in N s under mailbox: mailbox kept ([dev] present_fallback=off)` once instead; `fifo`: FIFO from the start (`window: presents FIFO from the start ([dev] present_fallback=fifo)`). `false`, `0` and `no` are `off`. Exported as `ICO_PRESENT_FALLBACK=<value>` ([DISPLAY.md](DISPLAY.md#the-fifo-fallback)) |
| `submit_thread` | `false`, `0`, `off` or `no`: no submit thread; the render thread submits and presents its pictures (exported as `ICO_RD_SUBMIT_THREAD=0`; `true` exports `1`, the default: on wherever the graphics API takes calls from two threads) ([PLATFORM.md](PLATFORM.md#the-render-thread)) |
| `frame_rate_pin` | Android only: `auto` asks the system to hold the Frame rate row's number as a fixed refresh rate (`ANativeWindow_setFrameRateWithChangeStrategy`, or `ANativeWindow_setFrameRate`, looked up at run time; Android 11 and later), `off` never asks; also `frame_rate_pin` in `ico-pc.ini`; exported as `ICO_FRAME_RATE_PIN` (`port/platform/display_rate.h`, `port/platform/window_display_rate.c`; [ANDROID.md](../ANDROID.md#a-fixed-refresh-rate-for-a-frame-rate-you-set)) |
| `disc_reader` | where the game's disc reads run: `thread`, the default in the window build, on a thread of their own, each command still completing at the next simulated vsync and never before it (the game thread waits there for what is left of a read, the step's disc wait the present allowance excuses); `sync` on the game thread, as the tests, the tools and the headless build do (`false`, `off`, `0` and `no` are `sync`). Exported as `ICO_DISC_READER=<value>`; the start line is `cdvd: disc reads on a worker thread, read-ahead 256 KB; the game data on a fixed drive (NTFS)` or `cdvd: disc reads on the game thread ([dev] disc_reader=sync); ...` (`port/data/cdvd_host.c` (`reader_choose`); [DATA.md](DATA.md#the-disc-reader-thread)) |
| `disc_read_ahead_kb` | the disc reader thread's read-ahead in KB, `0` (off) to `4096`: after each read the thread reads on that much of the disc in 64 KB chunks a new read stops, and a read inside it is copied at once; `256`, the default with the thread (`false`, `off` and `no` are `0`; nothing without the thread). Exported as `ICO_DISC_READ_AHEAD_KB=<value>` (`port/data/cdvd_host.c` (`reader_choose`, `ra_fill`)) |
| `pipeline_set` | `full`, the default: the start-up set behind the start-up screen (the careful one on a device of the renderer's table, the Adreno 740 on Qualcomm's driver: `full` does not override it, a `window:` line says so); `careful`: the set under the careful policy on any device (`rd_pipeline_policy_careful`: batches of 8 on one worker thread, the pipeline cache written after each, stopped past 128 MB of the process's growth, the rest made on first use); `lazy`: no start-up set, each pipeline made when a frame first needs it, the pipeline cache written after each batch of them. Exported as `ICO_PIPELINE_SET=<value>`. After a start that ended without a word, in the foreground, while it made the pipelines (issues 93 and 107) the next start only is careful when that start made the full set and lazy when it made the careful one, writing nothing; the second such end in a row writes `lazy` to `config.toml` ([PLATFORM.md](PLATFORM.md#diagnostics), [RENDER.md](PIPELINES.md#the-start-up-set)) |
| `pipeline_skip` | written by the game, not a choice: `"sprite_planes; <device>; <driver>; <date>"` (`port/platform/pipeline_skip.h`) after a start that ended while the graphics driver created a pipeline of the sprite planes builds alone (issue 161); on the same device and driver every start then makes none of them and draws the screen-prim triangles through the plain sprite builds (`rd_set_sprite_planes_off`; the `rd: pipelines: the sprite planes builds are not made on this device (...)` line), on another device or driver it is kept and not used (a `window:` line says so); `""` or deleting the line makes them again ([WORK_BUFFERS.md](WORK_BUFFERS.md#without-the-sprite-planes)). Not exported; `ICO_RD_NO_SPRITE_PLANES=1` is the tests' switch for the same fallback |
| `pipeline_memory_floor_mb` | Android: the start-up set stops once the device's available memory (MemAvailable) is under this many MB; `auto`, the default, by the driver: on Qualcomm's own driver 17 % of the device's memory, at most 2048 MB on a device of more than 8 GB (696 MB on a 4 GB phone, 1392 MB on an 8 GB one, 2048 MB on 12 GB and 16 GB; a stop there is as at the system's low-memory signal: the rest made on first use, the next start lazy), on any other driver the same up to 8 GB and at most 512 MB above (a stop there is for that start only and the rest is made in the background, [PIPELINES.md](PIPELINES.md#the-background-finisher)); `0` off (exported as `ICO_PIPELINE_MEMORY_FLOOR_MB`; `port/platform/memory_floor.h`, issue 161; the lines `ico_pc: pipelines: the free-memory floor is F MB (<rule>); <driver class>; N MB available of T MB now` at the set's first ask and `ico_pc: pipelines: the start-up set stopped at N MB available of T MB (floor F MB, <rule>; <driver class>): the rest made ...`, `[dev] pipeline_memory_floor_mb` in place of the rule when set; [WORK_BUFFERS.md](WORK_BUFFERS.md#without-the-sprite-planes)) |
| `audio_sync` | `true`: the audio renders on the game's clock, once per vsync with the queue and cushion of the push, instead of on the audio device's (exported as `ICO_AUDIO_SYNC=1`; `false`, the default, exports `0`); for comparing the two when a report is about sound ([AUDIO.md](AUDIO.md#the-outputs-clock)) |
| `unlock_credits` | as `[dev] unlock_credits` |

The desktop's local reads and their start-up probe have no `[dev]` key of their own: `ICO_RD_LOCAL_PROBE` is an
environment switch for headless runs (see [TESTING.md](TESTING.md#environment-switches)); the three paths they
switch can still be forced with `[dev] fog_path`, `date_path` and `fx_dst_local`.

At start `ico_ini_export` logs the `[dev]` keys of `config.toml` the game itself writes (`pipeline_set`,
`vu_prepass`, `gpu_robust`, `pipeline_skip`, and issue 123's ladder's `vu_bones`, `vu_bones_by`,
`turnip_debug`, `turnip_debug_by`) in one line, `ico_pc: [dev] keys kept from an earlier start: pipeline_set =
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
   simulation depends on it, add it to `ICO_SIM_KEYS` (`port/platform/sim_keys.c`) with its
   default in `kDefaults` at the same place (the config text the game takes when the key is
   unset; `NULL` when unset is the disc's or the device's own choice): a pad recording's header
   then carries it ([INPUT.md](INPUT.md#recordings)), a save state made with another value is
   refused with the key named, and a state saved before the key existed loads while
   the key is unset or at that default ([PLATFORM.md](PLATFORM.md#validity)).
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

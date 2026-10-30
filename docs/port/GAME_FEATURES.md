# Game features

The port's own features that reach into the game: the gameplay options and the places in `ico2/`
that read them, achievements over a read-only view of the game state, photo mode, the model
viewer and the characters' colours, the music gallery, the credits, the title logo, Add to
Steam (Windows and Linux) and the Android build's graphics driver packages. The menus that show
them are [UI.md](UI.md)'s; the picture options are [DISPLAY.md](DISPLAY.md)'s.

## Files

| path | what it holds |
| --- | --- |
| `port/game/options.h`, `options.c` | the gameplay options, their config keys and defaults, the mirror flag per save slot, the stereo/mono choice |
| `port/game/door_grace.h`, `door_grace.c` | the doorway fix's arm and arrival grace |
| `port/game/variants.h`, `variants.c` | the game variants ([variants]): values, config keys, the achievements rule, the debug switches and debug-menu opener the game binds, the hint rule, the enemy row reader |
| `port/render/rd_chain.h`, `rd_content_chain.c` | Natural chains: the two chains, the Null's transform, the links' clip planes |
| `port/game/achievements.h`, `achievements.c` | the achievement table, its conditions, the counters, `achievements.toml`, the popups |
| `port/include/ico_gamestate.h`, `port/game/gamestate.c` | the read-only game-state view: the snapshot, the signals, the run, the retail address table |
| `port/game/photo_mode.h`, `photo_mode.c` | photo mode's state: the free and orbit cameras, the controls, the `[photo]` keys |
| `port/game/photo_view.h`, `photo_view.c` | photo mode's camera put into the game's own matrices |
| `port/game/model_viewer.h`, `model_viewer.c` | Extras > Models and the game side of Extras > Characters |
| `port/game/model_viewer_table.c` | the viewer's models and the motion-kind blocks |
| `port/game/model_viewer_motion.c` | which motions of a block the loaded stage can play (`ico_mv_motion_playable`) |
| `port/game/model_viewer_cam.h`, `model_viewer_cam.c` | the viewer's camera step, a pure function |
| `port/game/appearance.h`, `appearance.c` | the characters' colours: the parts, the palette, the recolour of the CLUTs |
| `port/ui/gallery.h`, `gallery.c`, `gallery_play.c` | the music gallery's list and its engine |
| `port/data/df_pack.h` | reading one member of a `DATA.DF` stage pack outside the game's disc thread |
| `port/include/ico_credits.h`, `port/game/credits.c`, `credits_live.c` | the staff roll's port credit and the Extras credits playback |
| `port/game/title_logo.h`, `title_logo.c` | the title logo hidden under the port's menus, the title's full-frame models |
| `port/platform/steam_shortcut.h` | Add to Steam |
| `port/platform/gpu_driver.h` | the Android build's graphics driver packages |

## Gameplay options

Each `[gameplay]` option, `[game] circle_back` and `[audio] output` is read once from
`config.toml` the first time it is asked for and can be set at run time; `[dev] debug_option` is
read at each call; `ico_opt_reload` forgets every cached value so the next read goes back to the
file (`port/game/options.h`, header comment; `port/game/options.c` (`get_or`,
`ico_opt_debug_option`, `ico_opt_reload`)). Every default is the original game's behaviour, Circle
back excepted. The Settings menu shows and writes every key of the table except
`[gameplay] mirror`, which the New Game screen sets for the run without writing it
([UI.md](UI.md#the-new-game-and-quit-screens)), and `[dev] debug_option`, which is set in the file
only (`port/ui/settings.c`, the `UI_OPT_*` cases of `rawValue` and `ui_settings_step`). Types and
defaults are [CONFIG.md](CONFIG.md#gameplay)'s; the differences from the PS2 are
[DIVERGENCES.md](DIVERGENCES.md)'s.

| key | read by |
| --- | --- |
| `[gameplay] stick_fix` | `port/input/pad_host.c` |
| `[gameplay] yorda_safe` | `ebrain.c`, `enemy_act.c`, `backStage.c` |
| `[gameplay] door_fix` | `door_grace.c`, from `boyact.c` |
| `[gameplay] natural_chains` | `BgAnimation.c` (window build) |
| `[gameplay] skip_opening` | `kanbanBoot.c`, `op.c`, `main_host.c` (its log line) |
| `[gameplay] hold_type` | `mcard.c`, `layout_action.c`, `settings.c` |
| `[variants] *` (Extras > Game variants) | `port/game/variants.c`; the game hooks in `debug.c`, `enemy.c`, `enemy_act.c`, `queen.c`, `lws_kyomi.c`, `girl.c` ([Game variants](#game-variants)); `settings.c` (the Debug menu row); `gamestate.c` (the achievements rule) |
| `[gameplay] mirror` | `pad_host.c`, `audio_host.c`, rd, `photo_ui.c` |
| `[gameplay] developer_mode` | `main.c`, `debug.c`, the port |
| `[game] circle_back` | `layout_action.c`, `layout_texture.c` |
| `[dev] debug_option` | `debug.c` |
| `[audio] output` | `mcard.c`, `layout_action.c` |

The game code reads the options through `extern` declarations of the `ico_opt_*` functions, never
through a port header (`ico2/common/src/backStage.c`, the `ico_opt_yorda_safe` declaration). The
hook sites, grouped by option, follow.

### Stick fix

`ico_input_stick_fix` evens out the stick's diagonal reach against the game's own divisor in
`iosPadNormalizeStick`; its arithmetic is [INPUT.md](INPUT.md#the-virtual-pad)'s. It applies to the
live sources only: `ico_input_frame` passes `ico_opt_stick_fix()` to `ico_input_vpad_to_frame`,
while a [pad script](ARCHITECTURE.md#terms)'s bytes are used as written (`port/input/pad_host.c`
(`ico_input_frame`)). The default is off (`port/game/options.c`, `ico_opt_stick_fix_default_for`,
which returns 0 for every build), and a file that sets the key keeps its value. Every reader takes
this default: `ico_opt_stick_fix` (`port/game/options.c`), `ico_input_apply_toml`
(`port/input/input_config.c`) and the recording header (`port/platform/main_host.c`
(`record_header`)). There is no hook in `ico2/`.

### Yorda safe

Seven sites under `ico2/` return early when `ico_opt_yorda_safe()` is on:

- `ico2/omori/src/ebrain.c` (`eBrainGetTarget`), four sites: the target case 2 (no enemy takes the
  girl as its target), case 6, the sight check of the girl in the target loop, and the chase that
  turns to her after 181 frames;
- `ico2/fumi/src/enemy_act.c` (`enemyKidnapCheckGirl`, `enemyPickupCheckGirl`): a shadow never
  grabs her;
- `ico2/common/src/backStage.c` (`backStageProcessMain`): the off-screen kidnap and carry timers
  do not run, so the shadows never take her while the boy is in another room.

`port/game/test/options_test.c` compiles `ebrain.c` in and drives `eBrainGetTarget` with the option
off and on over a synthetic slot set. Yorda safe does not suspend achievements
(`port/include/ico_gamestate.h` (`ico_gs_achievements_suspended`)).

### Door fix and the arrival grace

One doorway of the original game, the East Reflector's door at the top of the ladder, sends Ico
and Yorda straight back out when they arrive through it
holding hands: exits are floor attributes Ico checks every frame, the first exit floor touched
after an arrival is only remembered, and Yorda is placed on Ico's arrival point with the hand
connect starting at once, so a push onto the door-back floor fires it (`port/game/door_grace.h`,
header comment). With `door_fix` on, an exit taken holding hands arms a grace; the next stage start
begins it when that stage is the exit's destination; during the grace an exit floor leading back
to the stage just left is only remembered, as the game does with the first floor after an arrival
(`port/game/door_grace.c` (`ico_door_grace_begin`, `ico_door_grace_holds`)). Scripted stage changes
never arm it, and nothing enters a save.

The hooks are all in `ico2/fumi/src/boyact.c`:

- `CheckCollisionAttr`: `ico_door_grace_tick` once per frame of the check while a grace is active,
  before its early returns; `ico_door_grace_holds(exitData[e].nextStage)` before the game's own
  first-floor rule; `ico_door_grace_arm(stage_no, nextStage, ACTGame_FLAG_TETSUNAGI())` after a
  `RequestStageChange` that succeeded, on both the escorted and the alone path.
- `actBoyStart`: `ico_door_grace_begin(stage_no, ...)` with 3 seconds of game frames, from the
  frame-rate expression the game uses there.

`port/game/test/door_grace_test.c` runs the arm and grace with the option off and on, holding
hands or not, and a copy of the exit check's guard.

### Natural chains

Two stage animations let a chain down for the player, `st10l_chain_down` (stage 17) and
`st19a_chain` (stage 32): 21 links under a Null whose keyed Y scale grows to 1 while the chain
comes down, so the disc draws the links squashed until it is fully down (`port/render/rd_chain.h`,
header comment; the table is `port/render/rd_content_chain.c`, `rd__chains`). With the option on,
the Null takes no scale along the chain and its children are moved along it instead
(`rd_chain_null`), and each link gets a clip plane in its own space where the disc's squashed top
link begins (`rd_chain_link`); the VU shaders discard what lies above it. Nothing is drawn where
the disc's chain is not, and at full length the two pictures are the same.

The hooks are in `ico2/seki/src/BgAnimation.c`, under `ICO_RD` only (the
[window build](ARCHITECTURE.md#terms)): `bga_CalcAnimation` sets `bgaHostChain =
rd_chain_find(p->group)` and `bgaHostNatural = bgaHostChain != 0 && ico_opt_natural_chains()`;
`bga_CalcObject` reads them at the Null and at its children. The plane travels from
`RegistPacket.c`'s `regHostMesh` to `rd_draw_vu_mesh`. Being a draw-only change, the key is not
among the keys a pad recording's header records (`port/platform/main_host.c` (`record_keys`);
`port/game/options.h`, header comment). `hold_type` is recorded, as R1's meaning changes the simulation.

### Mirror

Mirror mode negates the stick X of the live sources (`port/input/pad_host.c` (`ico_input_frame`)),
swaps the audio pan (`port/audio/audio_host.c` (`ico_audio_pan_mirror`)), flips the
[presented](ARCHITECTURE.md#terms) picture with the port's UI pre-flipped (`port/render/rd.h`,
mirror mode section, `rd_set_mirror`), and mirrors the mouse look in photo mode
(`port/ui/photo_ui.c`). The renderer learns the value through the one listener the Settings module
registers (`ico_opt_set_mirror_listener`).

The value belongs to the run: `[gameplay] mirror` until the player picks at New Game or loads a
save. Each save slot's flag is kept in `config.toml`, never in the card files, as `[mirror] slot_N`
with `slot_N_sum`, the save's game-block checksum, so a slot overwritten elsewhere reads Off
(`port/game/options.h` (`ico_mirror_slot_saved`, `ico_mirror_slot_loaded`)). The hooks are in
`ico2/common/src/layout_action.c`: `la_save_processing` calls `ico_mirror_slot_saved`,
`la_load_processing` calls `ico_mirror_slot_loaded`, and `la_title_continue_or_new` and
`la_title_new_game_only` call `ico_opt_mirror_reset`, because the title belongs to no run.

### Circle back

With `[game] circle_back` on, Circle is an alias of the game menus' Triangle back action. The game
reads the back buttons through `lt_ext_back_buttons()` in place of a constant
(`ico2/common/src/layout_action.c` (`LA_BACK`); `ico2/common/src/layout_texture.c`
(`LT_BACK_BUTTONS`)), and the Settings module hands the option to `port/ui` with
`lt_ext_set_circle_back` at install and on a change (`port/ui/layout_ext.h`
(`lt_ext_back_buttons`)).

### Developer mode

Developer mode turns on what the development build had and the retail build left as stubs:

- `ico2/common/src/main.c` (`Main`) calls `debug_Menu` each [tick](ARCHITECTURE.md#terms); SELECT
  opens it.
- `ico2/common/src/debug.c` (`debug_PrintfDummy`) prints the retail build's dummy debug prints
  through the debug font.
- `debug.c` (`debug_VariableInit`) loads `<pref>/dev/thisIsYourDebugOption` when `[dev]
  debug_option` is non-zero (`ico_opt_debug_option`).
- `debug.c` (`debugSceOpen`) opens files on the development kit's `host0:`, which
  `port/data/sifdev_host.c` maps to `dev/` in the [user folder](ARCHITECTURE.md#terms); off, the
  retail `cdrom0:` path, which finds nothing.

On the port's side it suspends achievements (`port/game/gamestate.c` (`ico_gs_developer_mode`)),
is written in the [trace](ARCHITECTURE.md#terms)'s header (`port/platform/trace_host.c`
(`# developer_mode`)), gates the texture and model dumps (`port/platform/window_host.c`
(`dumpTextures`, `dumpModels`)) and the Settings rows for them (`port/ui/settings.c`
(`UI_OPT_DUMP_TEXTURES`)), adds a log line per pack replacement (`port/render/texpack.h`
(`TexpackConfig.developer`)) and enables the model viewer's "save this model's files"
(`port/game/model_viewer.c` (`saveAvailable`)).

### Stereo or mono

`[audio] output` is `auto` (the game decides, as on the PS2), `stereo` or `mono`. The memory card
hook records the card's mode and returns the mode in force (`ico2/fumi/ios/mcard.c`
(`gameblock_read`, `ico_opt_output_card`)); the game's own Stereo/Mono row records the new mode and,
with an explicit key, saves the key at once so the file and the card never disagree
(`ico2/common/src/layout_action.c` (`la_game_option`, `ico_opt_output_toggled`)).

### Hold type

`[gameplay] hold_type` is `"disc"` (the default: the disc's own, the region row's `hold_type_own`),
`"hold"` (the game's `optionControlType` 0, A: keep R1 held to hold Yorda's hand) or `"toggle"` (1,
B: a press of R1 takes her hand, another lets go; `ico2/fumi/src/act-wish.c`,
`ico2/fumi/src/commonact.c` (`handoff_heroin`), `ico2/fumi/src/boyact.c`). `ico_opt_hold_type`
resolves the choice for the selected disc and is all the game sees: A on the European, Japanese,
Korean and Asian discs, B on the North American disc, the August 2001 preview and the June 2001
demo, whose own game has no option and always toggles (`_ACTCommonMailTest_<disc>` read
`pad.trg & 8` alone). A `"hold"` counts as chosen only beside `hold_type_chosen = true` (every
Options write sets it): the first-run text held `"hold"` until `"disc"` existed, so a bare one reads
as `"disc"`, the same A on the four A discs and the toggle the 2001 discs always played; the file is
never rewritten for it. The log line `options: hold type <disc's own|hold|toggle> (A|B) on <disc
id>` is written once for each choice and disc. The key wins over the card: `gameblock_read` (and
its 2001 arms `gameblock_read_ntscaug6`, `gameblock_read_jun26`) sets `optionControlType` from
`ico_opt_hold_type`, not from the product file's `controlType`, which `product_write` still writes
from the variable on the four discs whose record has it (`ico2/fumi/ios/mcard.c`);
`ui_settings_install` sets it at boot and at every layout stage (`init_layout_texture`,
`applyHoldType`), and the Hold type row steps Disc's own, A, B and sets the key and the variable
(`port/ui/settings.c` (`ui_settings_step`)). On the 2001 discs the port reads the variable where
the European arms do: `_ACTCommonMailTest_<disc>` lets go on the European arm's condition
(`optionControlType == 1 ? trg : !now`, the release on R1 up for the boy only, since Yorda's
`subGirlControl_<disc>` calls the arm too) and `subBoyControl_<disc>`'s case 38 sends its mail on
`(optionControlType == 1 ? trg : now) & 8`; with the disc's own B both read `pad.trg & 8` as the
disc did. Their `ACTGetWish_FromPad_<disc>` keeps `pad.trg & 8` for its R1 wish: on those discs
that one bit is also the let-go while hands are held (`subBoyCollision_ntsc` case 68), so the
European arm's `now` form would let go while R1 is held. The game's own Options screen row,
reached only on a disc whose layout tables are not recognised, calls `ico_opt_hold_type_toggled`,
which writes the key and saves at once (`ico2/common/src/layout_action.c` (`la_game_option`, case
318); `port/game/options.c`). The row shows on every disc ([Options on each
disc](#options-on-each-disc)).

### Game variants

Options > Extras > Game variants (`[variants]`, [CONFIG.md](CONFIG.md#variants)). The values are
`port/game/variants.c`'s; the game reads them in three ways, and with every row at its shipped value
it runs as before (each hook below gives the game's own value or row then).

The debug switches. `debug_VariableInit` (`ico2/common/src/debug.c`), after the retail values
and the developer's option file, hands `ico_variants_bind_debug` the five switches the variants
hold and `debug_MenuOpen`. `ico_variants_apply` writes them at the bind, at every
`ico_variants_reload` (so every `ico_opt_reload`) and at every `ico_variant_set` (so every step of
the page): a variant that is on holds its switch at its value, one at its shipped value leaves the
switch alone, and switching one off puts back the value the game had when the variant took it.
`variants.c` names no game symbol, so `ico_options` links into the tests without the game, and the
program's link order (`libico_game.a` before `libico_options.a`) needs nothing from the game.

| row | switch | read by |
| --- | --- | --- |
| One hit per swing | `debug_one_hit_only` 1 | `ico2/omori/src/attackhit.c` |
| Shadows never dodge | `debug_ignore_dodge` 1 | `ico2/fumi/src/enemy_act.c` |
| Shadow aggression | `debug_enemy_battle_type`: bold/wary = Positive/Negative, patient/quick = Low/High (`debugEnemyBattleName` 0 Positive-Low, 1 Negative-Low, 2 Positive-High, 3 Negative-High); Their own: untouched | `actEnemyStart` and its twins (`enemy_act.c`) |
| Ignore cutscene cameras | `debug_ignore_demo_camera` 1 | `ico2/omori/src/camera-root.c` |
| No cutscene look-around | `debug_hand_camera` 0, the USA disc's value (it never sets it) | `camera-root.c`: the right-stick block under `handCameraLimitP/V` (`SetHandCameraLimitInDemo`) and the cutscene zoom range |

The enemy table. Every read of `enemyKind` in `ico2/sugipon/src/enemy.c` (`setEnemyObject` and its
June 2001 twin, `InitEnemyGeo`, `CanThisEnemyFly`, `GetEnemyBattleType`, `GetEnemyDefLife`,
`GetEnemyDefDodgeRange`, `GetEnemyDefParaIndex`) goes through `ENEMY_DEF(kind)`, i.e.
`ico_variant_enemy_def(enemyKind, enemyKindRows, enemyKind_ntsc, enemyKindRows_ntsc, kind)`. It
returns the disc's own row while Shadow toughness is not American and aggression is the Shadow's
own; else a copy: American (only on a table of the USA one's 45 rows, `rows == usaRows`, and never
on the June 2001 demo) takes the USA row's life, scale, fly type and battle type and keeps the
disc's model, particle, `float0C` (the fly acceleration), dodge range and parameter row, and a row
whose model is the disc's "no model" (the table's last row's) is left whole; an aggression other
than Their own sets the battle type of every row (`actEnemyRestart`, which every spawn runs, reads
it through `GetEnemyBattleType`). A kind outside the table reads its last row (logged once). The
tables are `ico2/sugipon/src/enemyKind.c`'s, one per disc in the European record, with their row
counts (`enemyKindRows_*`; the June 2001 demo's is its own 18 rows, the others 45). PC port: on the
demo American is not offered (`ico_variant_offered`): its 18 rows are kinds of its own (other
models and roles than the USA disc's first 18), and a by-index copy made ground Shadows fly and drew
a scale-4 kind at 1.1. A config `american` there reads as Original with
`variants: shade_toughness = american is not offered on the June 2001 demo (its enemy rows are its
own); using original`, `ico_variant_set` gives Original for it, `ico_variant_step` passes over it
(Original <-> Not tougher later), and the page's note is `UI_STR_OPT_VAR_TOUGHNESS_NOTE_DEMO`
(`settings.c` `variantNote`; No hints and No cutscene look-around show their `_2001` notes on the
three 2001 discs, where `ico_hints_on` is 0 and `debug_VariableInit` leaves `debug_hand_camera` 0).
`getEnemyRestartLife` (`enemy_act.c`) uses the first playthrough's factor (`gameParam[38]`) on
every playthrough when Shadow toughness is American or Not tougher later; the 2001 builds' and the
USA disc's restarts have no factor.

Local reads.

| row | site |
| --- | --- |
| No hints | `ico_hints_on()` (0 on the USA disc and the 2001 builds, and with No hints on): `LwsKyomiGeo` (`ico2/omori/src/lws_kyomi.c`) keeps the hint from being the stage's waiting one, so its timer stands still and a playing hint animation stops; `GirlAI` (`ico2/sugipon/src/girl.c`) does not ready or play the hint voice. The hints' finished and asleep flags (`FinishHint`, `SleepHint`, `WakeupHint`, set by the stage scripts), `Hint_Init` and the save's hint block are untouched, so a save keeps the scripts' progress and turning hints back on resumes them |
| Queen barrier | `ico_variant_barrier_hits()` (5, or 7 on the June 2001 demo, unless chosen): `QueenBarrierGeo` and its June 2001 twin (the hit that breaks it) and `QueenBarrierDL` (the barrier's fade divisor), `ico2/ito/src/queen.c`; the tables indexed by the hit count have 11 rows |
| Debug menu | the row's press (`port/ui/settings.c`) calls `ico_variants_debug_menu_pressed`, which opens the menu through the bound `debug_MenuOpen` (`menuState` 1 as SELECT sets it, `debugBackGroundDisableFlag` 1, the first row) and returns 1; Options then closes (`closeForDebugMenu`): from the title back to the title, from the pause menu into the game as the pause menu's Back does (`la_host_resume_play`, `ico2/common/src/layout_action.c`: the sound resumed, `la_game_loop`). `debug_Menu` runs in developer mode only (`main.c`), and the row shows only then. Unbound, the press logs, Options stays and nothing is marked used (achievements stay on). PC port: on the June 2001 demo the row is never shown (`settings.c` `optShown`): its `debug.c` option table (67 rows against the demo's 62) and stage-select flags are the preview's; `variants: the Debug menu is not offered on the June 2001 demo (its debug tables are the preview's)` once, when Developer mode would show it. SELECT in Developer mode still opens the game's menu there (game code) |

Log lines: `variants: the game's debug switches are bound (...)` once per `debug_VariableInit`;
`variants: applied shade_toughness=... ; one_hit_only=... hand_camera=...` once per apply (a
switch the variants do not hold shows as `(game's)`); `variants: enemy kind N is past this disc's M
enemy rows; its last row stands in` once; `variants: the Debug menu cannot open: the game has not
bound it`; `settings: Options closed for the Debug menu (to the title|back to the game)`.

## Options on each disc

Every row, screen and extra of the port against the seven discs (`config/regions.txt`: pal, jpn,
kor, ntsc, aug6, jun26, cn), from the code. **W** works; **H** hidden on that disc, by the
region table field named (`port/core/region.h`, `settings.c` `optShown`); **N** shown, with a note
that it changes nothing there; **K** a known limit, described for players in the README. The
`settings_discs` runs (`port/ui/test/settings_discs_test.c`, one per disc, its expectations written
out per disc in `kExpect`) hold the H cells and the region fields behind them; the other tests named
hold the rest.

| row, screen or extra | pal | jpn | kor | ntsc | aug6 | jun26 | cn | decided by; held by |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Display: 50 Hz (title only) | W | H | H | H | H | H | H | `has_50hz_row`; `settings_discs`, `settings_<slug>` |
| Main: Game language | W | H | H | H | H | H | H | `has_game_language`; `settings_discs`, `settings_<slug>` |
| Main: Menu language | W | W | W | W | W | W | W | Auto is `ui_lang_default`, the sheets `menu_sheet`; `settings_<slug>`, `settings_render_<slug>` |
| Controls: Hold type | W | W | W | W | W | W | W | `hold_type_own`, the default `"disc"`: A on pal/jpn/kor/cn, B on ntsc/aug6/jun26 (the 2001 arms `_ACTCommonMailTest_<disc>` read `optionControlType` in the port); `settings_discs`, `options_<slug>`, `save` (per disc) |
| Controls: Circle goes back | W | H | H | W | W | W | H | `pad_back` is Triangle (`lt_ext_has_circle_back`); the 2001 arms cancel through `LA_BACK` too; `settings_discs`, `settings_<slug>` |
| Picture: Brightness; Controls: Vibration, Button configuration (pause) | W | W | W | W | W | W | W | `systemStatus[11]`, `iosPadActRequestEnable`, `layout_key_config` (`la_key_config` and its 2001 arms go back to Settings); `settings_discs` |
| Picture: Downsampling | W | W | W | W | W | W | W | the port's own renderer option (`[video] downsample`, `ico_video_downsample`), no game hook; `options`, `settings` |
| Gameplay: stick fix, Yorda, door fix, natural chains | W | W | W | W | W | W | W | the hooks sit in every disc's arm (`backStage.c`, `ebrain.c`, `enemy_act.c`, `boyact.c` `CheckCollisionAttr_ntscaug6jun26`, `BgAnimation.c` `bga_CalcAnimation_<disc>`); `options_<slug>` |
| Gameplay: Time to rescue Yorda | W | W | W | W | W | W | W | one hook in each `actEnemyKidnapEnd` copy (`enemy_act.c`: motion 952 on pal/jpn/kor/cn, 903 ntsc, 905 aug6, 938 jun26): the dive's play speed is reserved at priority 0 as `1/factor` (`2/factor` under a stage camera effect, which the demo has none of) and the camera's 5 s timeout is scaled by the factor; the frame-50 test, the dissolve and the freeze (priority 9) are the game's; `options_<slug>` (the hook scan), `settings_discs` |
| Gameplay: Skip the opening | W | W | W | W | W | W | W | `kanbanBootMain`/`_jun26`, `actOpDemo01`/`_ntsc`/`_aug6jun26`; `kanban_boot_<slug>`, `settings_extra_<slug>` |
| Gameplay: Film effect (pause, once cleared) | W | W | W | H | H | H | W | `has_film_effect` (no `film_noise` in the 2001 stage tables, `reel_row` -1); `settings_discs` |
| Gameplay: Players (pause, once cleared) | W | W | W | W | W | W | W | the 2001 arms carry the European second-pad branches ([Players on the 2001 discs](#players-on-the-2001-discs)); the count kept per save beside the card there (`[players]`); `settings_discs`, `options_<slug>` |
| Audio: Sound output | W | W | W | W | W | W | W | `soundOutputModeSet` on every disc; `options_<slug>` |
| Developer: Developer mode | W | W | W | W | W | W | W | `achievements_<slug>` (the suspension) |
| Extras > Game variants: Debug menu | W | W | W | W | W | H | W | `ICO_JUN26` (the demo's debug tables are the preview's); `settings_discs`, `settings_extra_<slug>` |
| Extras > Game variants: Shadow toughness American | W | W | W | W | W | N | W | `ico_variant_offered`; `variants_<slug>`, `settings_extra_<slug>` |
| Extras > Game variants: No hints, No cutscene look-around | W | W | W | N | N | N | W | `ico_hints_on` is not in `GirlAI_ntscaug6jun26`; `debug_hand_camera` stays 0; `settings_extra_<slug>` |
| Extras > Game variants: the other rows | W | W | W | W | W | W | W | `variants_<slug>` |
| Extras > Music | W | W | W | W | W | W | W | names by asset path, the region's stream bounds; `gallery`, `gallery_jpn`/`_kor`/`_cn` (the 2001 discs have no run: the test compares 105 rows) |
| Extras > Models | W | W | W | K | K | K | W | the model table has a Japanese column only (`tools/gen_mv_region.py`); the README names the 2001 discs' wrong models and the demo's missing ones; `model_viewer_jpn`/`_kor`/`_cn` |
| Extras > Characters | W | W | W | W | W | W | W | the characters' textures by name (`appearance.c`); `appearance`, `settings_extras` (from the title it runs in the model viewer) |
| Extras > Credits (title, once unlocked) | W | W | W | W | W | H | W | `credits_staff_packs` (the demo's DATA.DF has no `STGSTAFF*.DF`); `settings_discs`, `settings_extras_<slug>`, `credits_<slug>` |
| Extras > Export textures, Add to Steam | W | W | W | W | W | W | W | the running disc's `DATA.DF`; disc-independent |
| Photo mode (Film effect 1 to 4, Deja vu) | W | W | W | W | W | W | W | the grain is `gsb_filmNoise` on every disc (the 2001 arm's call is the port's); no reel scratches where `reel_row` is -1 (logged) |
| Achievements | W | W | W | W | W | W | W | `ach_stages`, `key_suffix`; `achievements_<slug>` |
| The New Game screen (Mirror mode, New Game+) | W | W | W | W | W | W | W | the start flag `gflag_new_game` (issue 147); `settings` `testNewGameStartFlags`, `settings_discs` |
| The title's Settings and Quit rows, the quit screen | W | W | W | W | W | W | W | `row_title_*` (placeTitle); `settings_<slug>`, `settings_render_<slug>` |
| The pause menu's Options (and its Extras) | W | W | W | W | W | W | W | `row_pause_options`, `layout_pause`; `settings_<slug>`, `settings_extra_<slug>` |
| Continue and the save slots | W | W | W | W | W | W | W | `layout_mc_files`, `row_mc_*`, `la_host_mc_slot_pickable`; `settings_extra` (pointer), `save` runs per disc |

A port site that writes one of these numbers itself, instead of reading the region table, is what
`region_literals_check` (`tools/check_region_literals.py`, [TESTING.md](TESTING.md)) fails on.
The log names each hidden row once: `settings: no Film effect row on <disc id> (the game has
none)` and `extras: no Credits row on <disc id> (its disc holds no staff scenes)`.

### Players on the 2001 discs

Players 2 (`girlControlMode` 1: Yorda on the second pad) is the European program's. Every reader
of `girlControlMode` sits in a European function body that branches at its head to the North
American, August 2001 and June 2001 arms (`_ntsc`, `_aug6`, `_jun26`), and no arm reads it: their
`subGirlControl_<disc>` reads Yorda's pad only while she is the debug target, from port 0. The
port writes the European second-pad branch into each arm at its site, with the arm's own mode,
mail and flag numbers; everything else in the arm stays the disc's. The numbers come from the
discs' own tables: `actModeTbl`'s mode names (117 rows on the North American disc and the
preview, 116 on the demo, to the European 120; the demo has no `ROPETURNSPECIAL`, so its rows from 67
on sit one lower) and
`actIntrList`'s mail rows (a mail is told by the modes it leads from and to). The sites:

| European site (`ico2/fumi/src`) | with Players 2 it | 2001 arms | the port |
| --- | --- | --- | --- |
| `girl_act.c` `setNext` | refuses the brain modes whose `girlBrainModeTable` flag is set (Escape, Hide, Hesitate, HideAdvance) | inlined into `girlBrainMain_DecideMode_<disc>`, which never refused one | the gate reads `girlControlMode` on every disc (the table is the same) |
| `girl_act.c` `subGirlControl` (the pad read) | reads Yorda's pad from port 1, unless `scpBoyControlReadDisable`; keeps `girlPad` | `subGirlControl_ntsc`, `_jun26`, `_aug6`: the read is the debug target's, port 0 | the read also when `girlControlMode` and not `scpBoyControlReadDisable`, from port `girlControlMode != 0`; `girlPad` kept |
| `subGirlControl` (the slow walk) | skips the half-speed walk of the slow-walk states | the same block, the arm's own three flag bits | skipped with Players 2 |
| `subGirlControl` (the turn) | turns her by the motion's `girlDirFrames` | `dirFrames` (the 2001 motion tables have no `girlDirFrames`; the port's row holds 0) | `dirFrames`, the disc's own |
| `subGirlControl` cases 29 and 28 | no back-off mail just after the jump of mail 197 (`flags18` bit 56, `actCommonJump`) | no such mail on these discs (their STAND, WALK and RUN rows have three JUMP mails to the European four), so no flag | nothing to carry; the disc's own (its case 28 has no timer to skip) |
| `subGirlControl` case 45 (`SOFA`) | gets up on a forward stick only; no hold or push count from the boy | the same case | the same, with the arm's mails |
| `subGirlControl` (end) | `flags20` bit 26 while her R1 is held, for the boy's look | no reader of it | the boy's arms read her pad's R1 themselves (below) |
| `girl_act.c` `subGirlBrain_Pulledup` | does not turn her toward the boy at the cliff foot | `subGirlBrain_Pulledup_<disc>`, the same turn | the turn skipped with Players 2 |
| `girl_act.c` `subGirlCollision` | on stage 23, under a low ceiling, drops her from a climb (mail 428, STAND and CLIMB to FALL) | no mail leads from STAND or CLIMB to FALL | not carried (no mail to send) |
| `girl_act.c` `actGirlHand`, `act-game.c` `_ACTMotDirSmzDirect`, `boyact.c` `subBoyCollision` (5737), `actBoyReadyMove`, `enemy_act.c` `subEnemyCollision`, `actEnemyKidnapBegin`, `commonact.c` `_ACTDebugPrint` | `girlDirFrames` for Yorda | `dirFrames` | the disc's own (`dirFrames`) |
| `girl_act.c` `actGirlHang` | after a jump onto a ledge, the stand mail once she has hung 5 s | `actGirlHang_ntsc`, `_aug6jun26`: only the ditch hang, at once | the European branch: from a jump, `ACTAdjustPlane` and the arm's stand mail after 5 s |
| `act-game.c` `ActOrientTest` | Yorda takes the orient mail outside `GIRLREADYMOVE` too | `ActOrientTest_ntsc` (mail 107), `_aug6` (106), `_jun26` (105), `GIRLREADYMOVE` 113, 113, 112 | the same condition |
| `act-game.c` `ACTGame_CommonLoop` | parallel status 27 on wish bit 33 | the 2001 parallel bits run one lower from the European 28 (`enemy_act.c`'s `PAL_NTSCAUG6JUN26(0x1C, 27)`), so 27 has no row | not carried |
| `act-env.c` `ACTGetEnvironment` | on stage 26 a tighter catch height for her jump to the boy at a ditch | the arms have no such catch block | not carried |
| `act-wish.c` `ACTGetWish_FromPad` (968) | the single-player clear of her wish (`flags20` bits 37, 38) only without Players 2 | `_ntsc` (bits 21, 22), `_aug6` (14, 15); `_jun26` has none | the clear only without Players 2 |
| `act-wish.c` `ACTGetWish_FromPad` (979) | her two square wishes only while Square is held | `wish3` 0x1000 and 0x2000 (`_ntsc`, `_aug6`), `wish2` bits 41 and 42 (`_jun26`) | the same, `pad.now & 0x10` |
| `boyact.c` `subBoyCollision` (5944) | the boy looks at Yorda, and the camera turns to her when she is off screen, while her R1 is held | `subBoyCollision_ntsc`, `_jun26`, `_aug6`: only on the boy's own look | the arm's look and camera block also on Yorda's `pad.now & 8` |
| `boyact.c` `pullup_check_heroin_position` | at the 3 m wall (`cliffSel` 300) she jumps to the boy's hand (mail 89, JUMP to PULLED_GO) | no mail leads from JUMP to PULLED_GO | not carried: the disc's own pull-up |
| `boyact.c` `actBoyDitch3mReady` | no mails that walk her to the ditch | `actBoyDitch3mReady_<disc>`, the same two mails | skipped with Players 2 |
| `commonact.c` `_ACTCommonMailTest` | R1 hands off for the boy only | `_ACTCommonMailTest_<disc>`'s toggle half, any caller | the toggle half for the boy only with Players 2 |
| `ico2/script/src/op.c` `actOpDemo01` | a new game sets one player | `actOpDemo01_ntsc`, `_aug6jun26` set nothing | one player at a new game |
| `ico2/fumi/ios/mcard.c` `gameblock_write`, `gameblock_read` | the save carries `girlControlMode` after the game block | the 2001 game block has no word after it (25,596 and 20,480 bytes, all the disc's) | kept beside the save in `config.toml` (`[players]`, below) |
| `ico2/sugipon/src/frameDependSequence.c` `execVibCondition` | the motion's vibration on Yorda's pad | the 2001 motion effects have no condition ids (`execVib_ntscaug6jun26`) | none (no data) |

The count is kept with each save on those discs in `config.toml`'s `[players]` (`ntsc_slot_N`,
`aug6_slot_N`, `jun26_slot_N` = 1 or 2, with the save's game-block sum, as the mirror flag is;
[CONFIG.md](CONFIG.md#players)): `la_load_processing` (and `_jun26`) sets `girlControlMode` from
it after the card's load, and `la_save_processing` writes it after the save, on those discs only
(`ico2/common/src/layout_action.c`; `port/game/options.h` (`ico_players_slot_saved`,
`ico_players_slot_loaded`)). A save without an entry, or with another sum, loads as one player.
`actOpDemo01_ntsc` and `_aug6jun26` set one player at a new game, as the European arm does. Every
disc's girl thread reports the count each frame, and a change is logged once: `options: Players
<1|2> on <disc id> (Yorda on the second controller, port 1 | Yorda led by the game)`; the sidecar
logs `options: Players <n> kept for save slot <N> on <disc id>` and `options: Players <n> from save
slot <N> on <disc id>` (with ` (no entry for this save: 1)` when there is none).

What a tester should try, on each of the three discs, with a second controller plugged in and
Players 2 set from the pause menu of a cleared save: Yorda walks and runs on the second stick and
stops when it is let go; she does not run off or hide on her own when shadows come (the refused
brain modes); holding R1 on the second controller turns Ico's head and the camera to her; Ico's
R1 still takes and lets go of her hand in both Hold types, and her R1 never does; she sits and
gets up from a sofa on a forward stick; at a ditch she is not walked to the edge for the jump;
after a jump onto a ledge she stands up after about five seconds; at a ledge Ico pulls her up as
usual. Then Players 1 again: she must behave as before. A save made with Players 2 loads with
Players 2, a new game starts with 1.

## Achievements and the game-state view

The achievements are a data table of 30 entries, each an id (the key in `achievements.toml`, never
renamed), a title and description string, a condition and a hidden flag
(`port/game/achievements.c` (`s_defs`)):

| group | ids |
| --- | --- |
| story | `opening`, `hand_in_hand`, `gate`, `windmill`, `graveyard`, `waterfall`, `gondola`, `water_tower`, `cliff`, `east_and_west`, `queen`, `queen_defeated`, `shore`, `finish`, `finish_again` |
| shadows and Yorda | `first_shadow`, `shadows_25`, `shadows_100`, `rescue`, `hand_10_minutes`, `hand_60_minutes` |
| couches | `first_save`, `couches_5` |
| weapons | `sword`, `queen_sword`, `light_blade` (hidden) |
| challenges | `never_taken`, `unbroken`, `swift`, `shore_secret` (hidden) |

The conditions read only the game-state view and the persisted counters: a stage by its `stageData`
number, a story flag, a signalled event, a run property (fresh from a new game, no capture, no game
over, every opening part watched) or a counter (enemies, milliseconds holding hands, saves, distinct
couches) (`port/game/achievements.c`, the `c_*` functions). The host loop calls `ico_ach_host_poll`
once per [vsync](ARCHITECTURE.md#terms), which runs `ico_ach_tick` once per new
[Main tick](ARCHITECTURE.md#terms): `ico_gs_tick`, every locked condition, the popup queue, and the
counters' write every 750 ticks when changed (`port/platform/host_loop.c`;
`port/game/achievements.h` (`ICO_ACH_STATS_FLUSH_TICKS`)). Popups are at least 5 seconds of Main
ticks apart, `5 * ico_gs_tick_hz()`: 125 ticks at 25 Hz (the European disc's 50 Hz mode), 150 at
30 Hz (the Japanese and Korean discs, and the European disc's 60 Hz mode); they can be turned off with
`[game] achievements = false`, and unlocks are recorded either way
(`port/game/achievements.h` (`ico_ach_popup_gap_ticks`, `ico_ach_popups_enabled`)). The stages and
story flags the conditions name are the disc's, from the region table (`port/core/region.h`
(`ach_stages`, `gflag_queen_dead`, `gflag_beach_secret`); the Korean disc's equal the Japanese disc's.
The eleven stages are the same rows on all seven discs (each disc's `stageData` read by name). The
queen's flag is 338 on the European, Japanese, Korean and Asian discs, 307 on the North American,
306 on the August 2001 preview and 292 on the June 2001 demo (`ico2/script/src/st25a.c`
(`actSt25aQueenDeadChk` and its `_ntsc`, `_aug6`, `_jun26` arms)). The beach secret is flag 354 on
the first four; the three 2001 discs' `actSt27aEndChk` arms set no flag for a held item
(`ico2/script/src/end.c`), so their row is -1 and `shore_secret` cannot be earned there, logged once
as `achievements: this disc has no beach secret; that achievement cannot be earned`.

To add one: a row in `s_defs` (`ACH(id, NAME, c_fn, hidden)`, `port/game/achievements.c`), its
condition `c_fn` over the game-state view, and the strings `UI_STR_ACH_<NAME>` and
`UI_STR_ACH_<NAME>_DESC`, which the `ACH` macro names, in `port/ui/strings.h` and each of
`port/ui/strings_en.c`, `strings_fr.c`, `strings_de.c`, `strings_it.c` and `strings_es.c`. An event
the view does not have yet needs an `IcoGsEvent` value (`port/include/ico_gamestate.h`) and an
`ico_gs_signal` call at its site in `ico2/`. The Extras list and `achievements.toml` pick the row up
from the table. The ctest `achievements` (`port/game/test/achievements_test.c`) drives every
condition and bounds the table's size: `test_each` checks `ico_ach_count() <= 30`, and the table
has 30 entries, so the bound is raised with the 31st. `font_coverage` checks the new strings'
characters.

The file is `<user folder>/achievements.toml`: `version`, a `[stats]` table (the counters, the
couches as a list of ids, the visited stages as a 128-bit hex set) and one `[unlocked.<id>]` table
per unlock with its time and the game's play time (`port/game/achievements.c`, the file section).
The run state of each save slot is kept there too, keyed by the save's checksum as the mirror
flag is: `la_save_processing` calls `ico_ach_slot_saved` and `la_load_processing`
`ico_ach_slot_loaded` (`ico2/common/src/layout_action.c`). The visited stages and the slots are
per disc, as the stage rows and the saves are: `stats.stages` and `run.slot_N_*` for the European
disc, `stats.stages_jpn` and `run.jpn_slot_N_*` for the Japanese one, `stats.stages_kor` and `run.kor_slot_N_*` for the Korean one; the unlocks and the other
counters are one set for the player ([SAVES.md](SAVES.md#the-seven-discs); `port/game/achievements.c`
(`run_key`, `write_file`, `read_file`)). The ctests `achievements`, `achievements_jpn` and `achievements_kor` run the
same program with each disc selected (`port/game/test/achievements_test.c`, `test_discs`).

When the file is written (issue 126, finding F2): `write_file` renders the whole text on the game
thread, in memory, over the text the file had when it was read (`s_base`, read once on first use and
then kept as the text last rendered, so the keys and comments the port does not know stay without
reading the disk again), and `writer_submit` hands a copy to one writer thread
(`port/platform/ico_thread.h`), which writes `achievements.toml.tmp`, syncs and renames it
(`ico_toml_write_text`). At most one text waits: a newer one replaces it (the latest wins). An
unlock, a save slot and the counters all go through the same `commit`, so the base always holds
every key. The counters write `ICO_ACH_STATS_FLUSH_TICKS` after the last write, but while the only
change is the hand-held time (it changes every tick Yorda is led) the period is
`ICO_ACH_HAND_FLUSH_SECONDS` (60 s) at the Main tick rate (`ico_ach_flush_due_ticks`). A write that
fails on the thread makes the counters pending again at the next tick; `ico_ach_flush` (the exit
and shutdown hook) writes what is dirty, joins the thread and tries once more after a failure. A
write over 20 ms logs `achievements: the file write took N ms (<path>)` (the first 8, then every
100th). The game thread's cost per write is the render (a few hundred bytes to a few KiB), so the
slow-step line's `achievements` component no longer includes the drive. `test_write_path` in
`achievements_test.c` holds the writer (`ico_ach_writer_hold`) to check the replaced text, the two
periods and the retry after a failed write.

Achievements are suspended, no counter advancing and nothing unlocking, while developer mode or
`[dev] start_stage` is on, the model viewer is up or the Extras credits are playing, and for the
rest of a run in which any of them was; Yorda safe, the stick fix and mirror do not suspend them
(`port/include/ico_gamestate.h` (`ico_gs_achievements_suspended`, `ico_gs_run_suspended`)).

### The game-state view

`port/include/ico_gamestate.h` is a typed, read-only view of the state the achievements need.
Nothing in it writes game state: `ico_gs_tick` takes a snapshot once per Main tick, and a signal
only appends to a port-side queue (`port/include/ico_gamestate.h`, header comment;
`port/game/gamestate.c`, header comment). The snapshot holds fixed globals (`stage_no`,
`gameover_flag`, `frame_count`, the language, `gamesysTimeCount`, `gFlagGameClear`,
`current_layout_id`, `systemStatus[12]`, `IosMcPreviewInfo`, the 50 bytes of `gflags`) and, when
the boy's and girl's objects are safe to follow, values derived through the game's own accessors
(the weapon kind, holding hands, Yorda carried by an enemy) (`IcoGsSnapshot`).

`gamestate.c` is built twice: with `ICO_GS_LIVE` for the program, where the sampler reads the
game's globals, and without it for the tests, which install a synthetic sampler with
`ico_gs_set_sampler` (`port/game/CMakeLists.txt` (`ico_gamestate_live`)).

The game raises signals through one-line calls (`ico_gs_signal(event, arg)`):

| event | site |
| --- | --- |
| `STAGE_ENTER`, `CHECKPOINT` | `ico2/common/src/StageManager.c` (`start_stage_Load_thread`, `CheckPoint`) |
| `ENEMY_KILLED`, `GAME_OVER` | `ico2/fumi/src/commonact.c` |
| `YORDA_GRABBED` | `ico2/fumi/src/enemy_act.c` |
| `ENDING` | `ico2/script/src/end.c` (the argument is `gFlagGameClear`) |
| `FMV_END` | `ico2/common/src/main.c` |
| `DEMO_END` | `ico2/script/src/op.c` (three parts), `st13b.c`, `deja.c` |
| `WEAPON` | `ico2/sugipon/src/weapon.c` |

`SAVE_DONE`, `YORDA_RESCUED`, `NEW_GAME` and `LOAD` are derived from the snapshot by `ico_gs_tick`
(`IcoGsEvent`). The queue takes no lock: every caller is a [fiber](ARCHITECTURE.md#terms) on the
host thread or the host loop between scheduler runs (`ico_gs_signal`). `ico_gs_peek` serves a few
bytes at a retail PAL [EE](ARCHITECTURE.md#terms) address from the snapshot's fixed globals, with
its table from `ico_gs_peek_table`; every other address is refused (`ico_gs_peek`).

### How the pause menu's stats are counted

The panel's lines (`port/ui/settings.c` `pauseStats`) read the run's counters (`ico_gs_run_*`),
each fed by one path (issue 162):

- Yorda captured: the `YORDA_GRABBED` hook counts the capture the moment the shadow sets `carrier`
  (`actEnemyForceSwitchToCarry`, `enemy_act.c`). Her `actMode` changes only when her own fiber reads
  the reserve mail (`ACTReserveTarget`, `act.c`), a tick or so later, so the snapshot's
  `yorda_carried_by_enemy` rises after the hook. `ico_gs_tick` counts that rise only for a capture
  the hook missed: not within `CAPTURE_MAIL_TICKS` (4) of a counted hook, and not while a
  hook-counted capture is still carried (cleared when she is dropped or a stage is entered). A second
  capture after a drop fires the hook again and counts. The carried mode is per disc
  (`ico_gs_carried_act_mode`): `0x6F` on the European, Japanese, Korean and Asian discs, 107 on the
  North American and August 2001 discs, 106 on the June 2001 demo (the game's
  `PAL_NTSCAUG6_JUN26(0x6F, 107, 106)`); the rise also raises `YORDA_RESCUED` when she is freed, so
  the rescue achievement needs it right.
- Deaths: `GAME_OVER`, raised by `ACT_LAYOUT_GAMEOVER` (`commonact.c`), which runs once per stage
  entry (`gameover_layout_flag`, cleared by `InitSceneObjects`). The June 2001 demo's death paths
  switch layouts without it (`actCommonDie_jun26`, `actCommonStoneDead_jun26`, `actCommonStone_jun26`,
  `actEnemyKidnapEnd_jun26`), so they call `ico_gs_signal_game_over_once`: one signal per stage entry.
- Saves: `SAVE_DONE` on the rise of the save-complete layout, which `la_save_processing` returns
  only after the last write.
- Enemies: `ENEMY_KILLED` from the two die procs; each ends in `_ACTWait(0)`, which never returns.
- Play time: the card preview's play-time word, read, not counted.

Log lines (`stats:`): `Yorda captured N (hook|flag) at tick T, stage S` at each counted capture,
`Yorda carried (the capture counted K ticks ago)` for a rise the hook already counted, and
`game over not counted (already counted on this stage entry)`; the first 8 of each, then every
100th. The ctest `achievements` (`test_capture_once`) drives the sequences and the per-disc mode.

## Photo mode

The pause menu's Photo mode row opens a port layout ([UI.md](UI.md#photo-mode)); its proc calls
`ico_photo_enter`, then `ico_photo_update` once a Main tick with the pad, and `ico_photo_exit` on
the way back. The game stays paused and its logic reads none of photo mode's state; the module has
no renderer dependency, so the [headless build](ARCHITECTURE.md#terms) walks the same menu and logs
the same lines (`port/game/photo_mode.h`, header comment).

There are two cameras, each keeping its own state, L3 switching between them: the free camera
(moves along the view and the camera's right at 400 cm a second times the speed, R3 cycling Slow
x0.25, Normal and Fast x4; the right stick looks; Up and Down rise and sink) and the orbit camera
(the left stick orbits a pivot, the right stick dollies and pans; the pivot is the game camera's
look-at point nearest the object it follows, or 400 cm in front without one). L1 and R1 roll, L2
and R2 change the field of view between 10 and 100 degrees, Select resets the current camera,
Square hides the help panel, Right cycles the Film effect (Off, 1 to 4, Deja vu), Cross takes a capture, and Triangle,
Circle or Start leave (`port/game/photo_mode.h`, the controls). Each tick's step is held to 250
units of the eye and 25 degrees of the view, under the renderer's camera-cut thresholds, so the
presenter blends it (`port/game/photo_mode.h` (`ico_photo_camera`); `port/game/photo_mode.c`
(`STEP_MOVE`, `STEP_TURN`)).

| key | default | what |
| --- | --- | --- |
| `[photo] stick_speed` | `1.0` | every rate times this |
| `[photo] invert_y` | `false` | the looking stick's up and down swapped |
| `[photo] hide_ui` | `false` | the help panel hidden at enter; Square saves it |
| `[photo] film_effect` | `0` | the Film effect photo mode shows, 0 to 4, or 5 (Deja vu); the game's own setting is untouched |
| `[photo] png_dir` | `"screenshots"` | the captures' folder in the user folder |

Captures are named `ico-YYYYMMDD-HHMMSS.png`, then `-2`, `-3` (`ico_photo_file_name`), and taken
by the window (`port/platform/window_host.c` (`ico_photo_take_capture`)).

### The re-render from the photo camera

Photo mode does not move a picture; the game draws the paused scene from the photo camera with all
its passes, so culling, shadows, reflections, the sun, the flare and every full-screen pass follow
it (`port/game/photo_view.h`, header comment). `ico_photo_view_tick` runs once a Main tick between
the objects' update and the draw (`ico2/common/src/main.c` (`Main`)):

- enter (photo mode on, the game paused): the game camera is saved with `gsb_PushView` and kept as
  the photo camera's base (its view at `matrixptr + 0x80`, its screen matrix at `+0xC0`, its
  distance `gsb_ViewFocus`), and the stage number noted;
- each tick: the photo camera's view goes into `+0x80`, the projection is rebuilt at the saved
  distance times the zoom (`gsb_SetVSMatrix`) and the common matrices with
  `gsb_MakeCommonMatrix`;
- leave: `gsb_PopView`, `gsb_MakeCommonMatrix`, `gsb_PopView` again so the eleven matrices are the
  game's byte for byte, and a camera cut for the presenter; when the stage changed or the game runs
  again the save is dropped instead (`port/game/photo_view.c` (`enter`, `leave`)).

`ico_photo_left` is 1 for the tick that left photo mode, so the aura's feedback area
(`staticBlur.c`), which the pause does not refresh, takes the game camera's picture once
(`port/game/photo_mode.h` (`ico_photo_left`)). In the window build a camera that has moved or
zoomed is marked as a free camera (`rd_free_camera`, `RdCamera.freeCamera`): the renderer then
keeps the triangles with a vertex behind it, which the game's own cameras rely on being dropped;
the game's rule stays while the aspect sweep runs and while the photo camera is still the game's
(`port/game/photo_view.c` (`ico_photo_view_tick`, `photoIsGameCamera`)).

### The Deja vu filter

Film effect 5, `ICO_PHOTO_FILM_DEJA`, is the deja vision's grain veil (stage 40). The stage draws
it through its stage animation `deja_lightning.bga` (`stageTable` row 79): two "sandstorm" entries
of type 10 hold the model 1614 (`sandstorm.p2o`, four quadrant sheets of the 256 x 256 noise
texture under REPEAT) in the camera's space at depths 0.72907 and 0.74207 and scale 0.0039999,
their type 0 envelopes the nodes' fades resting at 0.9 and -0.95 (`ico2/seki/src/BgAnimation.c`
(`bga_calcEnvelope`)), which `reg_dispMObj` draws in list 5 through the dissolve, added at FIX 9
and subtracted at FIX 4 (`ico2/seki/src/RegistPacket.c` (`reg_setDissolve`)). Every stage pack
carries the model and its texture (the Film effect's own animations, rows 67 to 70, place it in
every stage), so with photo mode on Deja vu `gsb_PostEffect` calls `ico_photo_view_veil` after the
fog: it finds the stage's sandstorm object (model 1614 among the objects of kind 35), gives it two
nodes of those values for one `reg_DispObj` under the tick's full-screen 500 unit pair, and puts
its own nodes, that pair and the display list priority back (`port/game/photo_view.c`
(`ico_photo_view_veil`); `ico2/seki/src/GsBase.c` (`gsb_PostEffect`)). The draws are the game's
packets, so the renderer continues the sheets to a wide frame's edges as it does in the stage
(`port/render/rd_content_extend.c`), the current stage's motion blur, aura and softening passes
take them, and captures include them. Nothing is drawn in stage 40, which draws the veil itself;
Deja vu has no film grain (`gsb_filmMode`), and the veil texture's palette cycle (its TIM2 block:
entries 0 to 15, no UV scroll) keeps running in photo mode as the grain's does
(`ico2/seki/src/Texture.c` (`tex_ResetVram`)).

The vision's film reel comes with it. `deja_lightning.bga` also holds nine "noise" entries of type
10 under CAMERA_PARENT (nodes 5 to 9, 11, 12, 15 and 16) of the models 1609 to 1613
(`noiseA.p2o` to `noiseE.p2o`, `objTableScene` rows 1253 to 1257: quads 0.066 across, noiseE's
0.906 tall, each with a 32 x 32 four-bit speck texture whose TIM2 header has no ICO block, so no
palette cycle and no UV scroll). Each entry has its own keyed motion (position about the
picture's middle in the camera's space, depth 0.83 to 1.17, scale and roll; 90 to 135 frames) and
its own type 0 envelope, the node's fade (91 to 132 frames, between -1 and 1): `reg_dispMObj`
skips a node at -1 or 1 and otherwise draws it through the dissolve at FIX (1 - |fade|) x 96,
added above 0 and subtracted below (`ico2/seki/src/RegistPacket.c` (`reg_dispMObj`,
`reg_setDissolve`)). The stage steps each motion and envelope a frame a tick and wraps each at its
own length (`ico2/seki/src/BgAnimation.c` (`bga_stepMotion`, `bga_stepEnvelope`); row 79 loops),
so the flicker has no short period. The deja dump (frame v2227) holds four of these draws in list
5 before the veil, noiseB subtracted at FIX 44, noiseD added at FIX 95, noiseE added at FIX 22 and
44, at GS X 2263, 2020, 1802 and 1655; the tracks evaluated from the stage's start (at 60 Hz) give
exactly those four draws and places 81 ticks in, every other node at -1 or 1. The Film effect's fourth
animation, `film_noise_l4.bga` (`stageTable` row 70, in every stage pack, the same objects),
holds the same nine entries key for key and envelope for envelope, so with Deja vu each photo tick
runs that row through the game's own evaluator on the filter's own frames:
`ico_photo_view_tick` sets aside the row's frames (the header's, every node's motion and
envelopes) and the node records of the objects it drives, puts the filter's frames in, runs one
step (`bga_CalcAnimation` in mode 1, looping, as `stage_CalcAnimationNoParent` runs a playing row;
the first tick `bga_SetFrame` from the start, as `stage_SetAnimation` starts a looping row), keeps
the five noise objects' evaluated nodes and the advanced frames, and puts everything of the game's
back (`port/game/photo_view.c` (`reelTick`); `ico2/seki/src/StageAnimation.c`
(`stage_HostBgaHeader`) finds the row). At the draw `ico_photo_view_veil` borrows each noise
object with those nodes for one `reg_DispObj` before the veil, under the same 500 unit pair
(`reelDraw`). The specks keep the vision's places and sizes: centred on the picture, each drawn
once, never repeated across a wide frame (the content table lists the veil's sheets only). Over
the tracks they reach GS X 1648 to 2387, the 4:3 picture (1792 to 2303) and up to 144 GS pixels
left and 84 right of it; one vertical line (node 16, GS X 1648 to 1669) always lies left of it. A
wide frame shows those parts as it does in the vision.
Nothing is drawn or evaluated in stage 40, which plays its own, or with photo mode closed; a place
without row 70 logs it once and draws the veil alone.

## The model viewer and Characters

Settings > Extras > Models shows the game's character and object models on the game's own motion
system (`port/game/model_viewer.h`). Each model is an object that one stage builds when it loads,
its host stage, found again by its object kind, its model and, where the stage has several, its
layout row; its animations are a block of motion-kind ids played with the motion-orient rows of
that block, as the development build's Motion Viewer plays them (`ico2/sugipon/src/motionViewer.c`
(`objMenu`)) (`port/game/model_viewer_table.c` (`mv_models`, `mv_blocks`)). Only numbers are in the
table; the names are the port's strings.

A model's host stage is its European `stageData` row, mapped by stage name on the other discs
(`ico_region_stage`). On the June 2001 demo, whose `DATA.DF` holds 49 of the retail discs'
entries, a model whose host stage's pack (`stageData[n].dataFile` + `.DF`) is not in `DATA.DF`
(`ico_df_has`, `port/data/df_pack.h`) is refused before the stage switch, from the list, from
Characters and from the character switch (`model_viewer.c` `modelStageOnDisc`; such a stage loads
nothing and dies building its objects): `model_viewer: the model not opened: "<name>" lives on
stage N (<stage>), whose pack <FILE>.DF is not in this disc's DATA.DF (SCUS-97158)`, or `... has no
stage on this disc` for a European row the demo has not. The other discs are not asked.

The list holds the motions of the block the stage can play (`ico_mv_motion_playable`,
`port/game/model_viewer_motion.c`): a plain motion (`blendKind` 320) needs its data loaded or to be
the stage's own (`area` 0); a node-blend motion needs every partner of its `blendMotionKind` list
(from `blendKind` to the 1147 end marker, inside the loaded table) to be a plain motion that passes
the same test, because `getNodeBlendedFloatingMotion` reads each partner's data and the game only
requests these motions where it loaded them (issue 136: motion 674 on Yorda's stage, partner 669
not loaded). `play()` refuses an id that fails it and logs once per id (`model_viewer: motion
"<name>" is not loaded here (partner "<name>")`, or `(its own data)` for a plain motion); the
per-motion arrays are sized by `MV_MOTION_TABLE_SIZE` (`motionTable`'s 1150); the `stage ... loaded`
line carries the count left out. The development build's own filter
(`ico2/sugipon/src/motionViewer.c`) tests only the plain case.

Picking a model loads its host stage with `ico_mv_active` set: the stage's scripts do not start
(`ico2/common/src/sceneManager.c` (`initSceneGObj`)) and achievements are suspended. Once the object
is up every other object is parked, pause and fog are off, and the renderer keeps only the object's
own draws (`rd_set_draw_filter`) over a grey backdrop recorded in [list](ARCHITECTURE.md#terms) 0;
the camera orbits it (`port/game/model_viewer.c`, header comment, `MV_VIEW`). An animation plays
through the motion manager as in the game, its frame-dependent sequence included: its sounds and pad
vibration play, but its effects (the stage animations and particles a motion enters, such as the
water ring of the boy's swim) are not entered while `ico_mv_active` is set; the draw filter would
not show them and the host stage may not hold them
(`ico2/sugipon/src/frameDependSequence.c` (`playEff`); [DIVERGENCES.md](DIVERGENCES.md) GL-10,
issue 104). Triangle goes back to
the list, and from the list to the title the way the pause menu's End Game does. The game side runs
from `ico_mv_tick`, called by `Main` after the pad is read (`ico2/common/src/main.c`). The camera
step is a pure function of the held buttons and sticks so a CPU test drives it
(`port/game/model_viewer_cam.h` (`mv_cam_step`)). Nothing is saved, and the viewer's own buffers are
host memory, not the game's [arena](ARCHITECTURE.md#terms) (`port/game/model_viewer.c`, header
comment).

Settings > Extras > Characters from the title runs in the viewer: Ico's model is loaded, the
Characters page becomes a panel at the left with the model beside it, L1 and R1 switch characters,
and Triangle returns to the title, where Settings opens again on Extras
(`port/game/model_viewer.h` (`ico_mv_characters_enter`, `ico_mv_characters_switch`,
`ico_mv_characters_leave`)).

### The characters' colours

Nine parts of Ico and Yorda each take Original or one target colour: `ico_skin`,
`ico_poncho_navy`, `ico_poncho_pink`, `ico_poncho_light`, `ico_poncho_dark`, `ico_tunic`,
`ico_shorts`, `yorda_skin`, `yorda_dress` (`port/game/appearance.h` (`IcoAppPart`)). A clothing part
takes one of 24 named palette colours ("red" to "black", in hue families from red through blue,
green and gold to the neutrals); a skin part also takes `tone1` to `tone12`, the lightest to the
darkest (`appearance.h`, the palette and tone table). The keys live in `config.toml`'s
`[characters]` section, one string per part, absent meaning `"original"`; an unknown value reads as
Original and is logged once.

The recolour works on a copy of the texture's CLUT before the decode, in the window build's texture
upload (`ico2/seki/src/Texture.c` (`texHostTexture`, `ico_appearance_recolour`)). A rule table names
for each covered texture the part it follows, the CLUT entries that belong to the part and soft
limits on saturation and lightness; each entry moves to the target's hue, with saturation and
lightness mapped through the part's anchor colour, and alpha is never written.
The rules hold CLUT entry numbers and limits only; no colour or pixel of the disc is in the source
(`port/game/appearance.c`, header comment). Every change bumps `ico_appearance_serial`, and
`tex_ResetVram` re-decodes the covered textures when it moves (`ico2/seki/src/Texture.c`
(`tex_ResetVram`)).

`ico_appearance_randomize` sets every part to a random non-Original value from a seed, the four
poncho groups all different, and logs `appearance: randomize (seed N)`;
`ico_appearance_randomize_character` does the same for one character, and `ico_appearance_reset`
and `ico_appearance_reset_character` put parts back to Original (`port/game/appearance.h`).

## The music gallery

Settings > Extras > Music lists everything the game's sound system plays, built at run time from the
game's own tables and played through the game's own engines (`port/ui/gallery.h`, header comment).
The groups are Soundtrack (the score's streams, the title theme included), Scene sounds (the
machinery stingers), Ambience (each stage sound environment's sounds), Voice (Yorda's hint voices
and the `com_v` bank) and Sound effects (one headed group per bank). Every entry is named by its
asset, the names coming from the disc and the ELF tables: a stream by its file under
`sound/ICO_ADPCM/` without `.int` (`event/39_8`), an effect by its `seDef` name (`port/ui/gallery.c`
(`gallery_label`)). The PAL disc has no sequenced music, so there is no sequence group.

The list (`gallery.c`) holds no game code: the tables come in a `GalleryTables`, and the engine in
a `GalleryEngine`, so `port/ui/test/gallery_test.c` builds it from the base ELF's tables. The
engine (`port/ui/gallery_play.c`) fades the title theme as the opening's demo step does, requests a
stream through `scpAdpcmPlayRequestFunc` and the script daemon's thread, and plays an effect with
`soundSeDefPlay` once its bank is resident; a bank that is not resident is read from a stage pack
on the disc into sound RAM in segment 1, the closed banks being read back into their segments on
leaving (`port/ui/gallery_play.c`, header comment). A stream pauses the way the pause menu pauses
the game's streams; an effect cannot pause, so the page stops it (`port/ui/gallery_play.c`, header
comment).

`port/data/df_pack.h` reads a pack member outside the game's disc thread: `DATA.DF`'s directory
gives loose files and packs, a pack is a raw deflate stream with a 16-byte header and 0x224-byte
member entries, and the index of every pack's members is built once by inflating each header and
directory (about 70 packs) (`port/data/df_pack.h`, header comment). The gallery uses it to check
that a stream's file is on the disc (`ico_df_has`), to read a bank's header and body
(`ico_df_find_member`, `ico_df_read_member`), and to find a stream's blank tail by halving over its
sectors (`gallery_stream_blank_from`).

The environment variable `ICO_GALLERY_PLAY` holds a script of items to play and leave, which the
headless `gallery_sweep` test drives (`port/ui/gallery.c`, `scriptParse`; [TESTING.md](TESTING.md)).

## The credits

The staff roll posts the lines of the disc's `staffRollNameData` and then the port's own lines, in
every roll, the real ending's and the Extras playback's; the disc's table is not changed
(`port/include/ico_credits.h`, header comment). The port lines are twelve blank lines, the heading
`{R}< Decompilation and PC Port > `, four blank lines and the name `{R}Nathanial Fine `, in the
roll's own forms and ASCII only, because the roll draws with `font_Print`'s bitmap font
(`port/game/credits.c` (`s_lines`)). The game's side is three host lines in
`ico2/common/src/staffroll.c`: `staffRollStart` logs the roll's length through `ico_roll_started`,
`staffRollNameOut` takes `ico_roll_port_line(rollNameIdx - staffRollNameDataNum)` past the disc's
lines, and the roll ends after `staffRollNameDataNum + ico_roll_port_count()` lines.

Settings > Extras > Credits starts the ending's first staff-roll stage (60, STAFF1) from the title,
and the ending's scripts run as they do in the real ending with `ico_credits_active()` set
(`ICO_CREDITS_STAGE`). The row is unlocked by `[dev] unlock_credits`, the `finish` achievement or a
clear count above 0 (`port/game/credits.c` (`ico_credits_unlocked`)). Under the flag:

- `ico2/script/src/end.c` (`actStaff1Chk`) requests the ending's song through
  `ico_credits_song_start`, which opens it where the real ending has it when the roll starts;
- `end.c` (`actStaff3RollChk`) goes back to the title when the roll ends;
- `end.c` (`actEndingSave`) saves nothing.

The engine (`port/game/credits_live.c`) keeps everything a save holds with the game's own
`gamesysMemorySave`, plus the checkpoint image, `gFlagGameClear` and `systemStatus[2..4]`, changes
stage as a load from the title does and puts the boy at the entrance the real ending uses. As stage
1 is entered again, `ico_credits_stage_enter` (`ico2/common/src/StageManager.c`
(`start_stage_Load_thread`)) puts the state back with `gamesysMemoryLoad`. A start that never
reaches STAFF1 is logged as a failure and the title requested (`port/game/credits_live.c`, header
comment, `REACH_TICKS`). Achievements are suspended while it plays. The entrance and the boy's
record are the disc's (`credits_entrance_exit`, `credits_boy_label`: exitData 216 and label 54 on
the European, Japanese, Korean and Asian discs; exitData 212 and labels 51, 50 and 47 on the North
American disc, the August 2001 preview and the June 2001 demo, `ico2/fumi/src/act-game.c`
(`ACTGame_SetActors_Debug`)). The June 2001 demo's DATA.DF (49 entries) has no `STGSTAFF1.DF`,
`STGSTAFF2.DF` or `STGSTAFF3.DF`, so its row has `credits_staff_packs` 0 and the start is refused
before anything changes, logged as `credits: failed: this disc (SCUS-97158) has no staff roll: its
DATA.DF holds no STGSTAFF1.DF (stage 59), so the ending's staff scenes cannot be played`.

## The title logo

The logo is nine stage animations of the title's stage (I, C and O, their glows and shadows). While
a Settings or Extras page opened from the title is up, or the model list the Extras page opens, it
is not drawn; its animation runs on, so back on the title it shows settled. Once hidden it stays
hidden until the title's menu (layout 12 or 13) is back or the stage changes, so Extras > Credits
and a model's stage load do not show it while the title fades. The pause menu's Settings never
hides anything (`port/game/title_logo.h`, header comment).

`Main` calls `ico_title_logo_update` once a tick after the layouts ran (`ico2/common/src/main.c`),
and `ico2/seki/src/RegistPacket.c` (`reg_DispObj`) skips an object whose model `ico_title_logo_skip`
names. The same function widens the title's full-frame model `title_back`, built for the 4:3
screen, to the [native frame](ARCHITECTURE.md#terms) by scaling its node matrices' x row by
`gsFrame.w / 512` when `ico_title_fullframe_model` names it (`port/game/title_logo.h`
(`ico_title_fullframe_model`)).

## Add to Steam

Settings > Extras > Add to Steam writes a non-Steam shortcut named "ICO" for the program into the
most recent Steam user's library; [PLATFORM.md](PLATFORM.md#steam-shortcut) has how the file is
edited and the build detail. The result is one of `ICO_STEAM_ADDED`, `_EXISTS`, `_RUNNING`,
`_NOT_FOUND`, `_NO_USER`, `_FAILED`; the shortcut's id is the CRC-32 of the quoted Exe and the
name with the top bit set (`port/platform/steam_shortcut.h` (`ico_steam_add_this_game`,
`ico_steam_appid`)). It is built and offered on Windows and Linux (the Steam Deck included) only:
the Android and iOS builds do not compile it and the macOS build does not offer the row
(`CMakeLists.txt`, the window build section; `port/platform/window_host.c` (`steam_add_handler`)).
`port/platform/test/steam_shortcut_test.c` has the checks.

## Android driver packages

The Android build loads a community Vulkan driver for Adreno GPUs through libadrenotools from a zip
the player adds on the Settings page: a flat package of `meta.json`, the driver library it names
(`libraryName`, matching `^[A-Za-z0-9._-]+\.so$`) and that library's dependencies, at most 64
files of at most 64 MB each. An install is checked whole, unpacked into `<folder>.tmp/` and moved
into place; the folder is the package's name and driver version made safe. A trial marker names the
driver a start is trying, so a start that died inside the driver is noticed at the next one
(`port/platform/gpu_driver.h` (`ico_gpu_driver_install_zip`, `ico_gpu_driver_trial_crashed`)). The
package handling is pure C, built and tested on every platform; the loading itself and the fallback
are [PLATFORM.md](PLATFORM.md#android-graphics-driver-packages)'s.

## Tests

Each is a ctest: `settings_discs` and its six other-disc runs (the rows each disc shows,
[Options on each disc](#options-on-each-disc)), `options` (`port/game/test/options_test.c`: the defaults, the setters,
`eBrainGetTarget` with Yorda safe), `door_grace`, `achievements`, `photo`, `appearance` and
`appearance_disc`, `credits`, `title_logo` and `model_viewer_headless` (`port/game/CMakeLists.txt`);
`gallery`, `gallery_sweep`, `credits_headless`, `model_viewer`, `model_viewer_cam`,
`model_viewer_effects` and `model_viewer_effects_elf` (`port/ui/CMakeLists.txt`); `steam_shortcut` and `gpu_driver` (`port/platform/CMakeLists.txt`).
[TESTING.md](TESTING.md) has how to run them.

## Video options

The picture options (`[video]`, the [presets](ARCHITECTURE.md#terms), aspect, resolution,
anti-aliasing, the CRT filter, frame rate) are `port/game/video_options.h`'s and are described in
[DISPLAY.md](DISPLAY.md).

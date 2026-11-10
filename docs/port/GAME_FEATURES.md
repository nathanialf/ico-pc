# Game features

The port's own features that reach into the game: the gameplay options and the places in `ico2/`
that read them, achievements over a read-only view of the game state, photo mode, the model
viewer and the characters' colours, the music gallery, the credits, the title logo, the community
translations, Add to Steam (Windows and Linux) and the Android build's graphics driver packages. The menus that show
them are [UI.md](UI.md)'s; the picture options are [DISPLAY.md](DISPLAY.md)'s.

## Files

| path | what it holds |
| --- | --- |
| `port/game/options.h`, `options.c` | the gameplay options, their config keys and defaults, the mirror flag per save slot, the stereo/mono choice |
| [HARD_MODE.md](HARD_MODE.md) | design note: every lever a harder game could use (the rows above and the mechanisms not exposed yet) |
| `port/game/door_grace.h`, `door_grace.c` | the doorway fix's arm and arrival grace |
| `port/game/ico_rows.h`, `ico_rows.c` | Developer > Free movement and the Ask Yorda for a hint binding: the row's value (Developer mode ANDed in), the hint button's press latch |
| `port/game/yorda_trip.h`, `yorda_trip.c`, `yorda_trip_live.c`, `yorda_trip_source.c` | Yorda trips: the rule (the main gate scene's 3 s count), the game's side (the stage load's motions, the run-pulled tick), her fall's motions read from the disc |
| `port/game/pit_time.h` | Time to rescue Yorda: the length of each disc's pull-in (the five SINK motions), header-only so the disc test checks the numbers the option reads |
| `port/game/variants.h`, `variants.c` | the game variants ([variants]): values, config keys, the achievements rule, the debug switches and debug-menu opener the game binds, the hint rule, the enemy row reader |
| `port/render/rd_chain.h`, `rd_content_chain.c` | Natural chains: the two chains, the Null's transform, the links' clip planes |
| `port/game/achievements.h`, `achievements.c` | the achievement table, its conditions, the counters, `achievements.toml`, the popups |
| `port/include/ico_gamestate.h`, `port/game/gamestate.c` | the read-only game-state view: the snapshot, the signals, the run, the retail address table |
| `port/game/photo_mode.h`, `photo_mode.c` | photo mode's state: the free and orbit cameras, the controls, the `[photo]` keys |
| `port/game/photo_view.h`, `photo_view.c` | photo mode's camera put into the game's own matrices |
| `port/game/eye_trail.h`, `eye_trail.c` | the shadows' eye trails in the world, redrawn from photo mode's camera |
| `port/game/model_viewer.h`, `model_viewer.c` | Extras > Models and the game side of Extras > Characters |
| `port/game/model_viewer_table.c` | the viewer's models and the motion-kind blocks |
| `port/game/model_viewer_motion.c` | which motions of a block the loaded stage can play (`ico_mv_motion_playable`) |
| `port/game/model_viewer_member.c` | the early show title art's model and texture registered from the logo pack's members and its object built on the host stage ([The model viewer and Characters](#the-model-viewer-and-characters)) |
| `port/game/model_viewer_cam.h`, `model_viewer_cam.c` | the viewer's camera step, a pure function |
| `port/game/appearance.h`, `appearance.c` | the characters' colours: the parts, the palette, the recolour of the CLUTs |
| `port/ui/gallery.h`, `gallery.c`, `gallery_play.c` | the music gallery's list and its engine |
| `port/data/df_pack.h` | reading one member of a `DATA.DF` stage pack outside the game's disc thread |
| `port/include/ico_credits.h`, `port/game/credits.c`, `credits_live.c` | the staff roll's port credit and the Extras credits playback |
| `port/include/ico_unused.h`, `port/game/unused_content.c`, `unused_live.c` | Extras > Unused content: the rows, the flag and the start; the engine (what each disc holds, the test rooms) ([Unused content](#unused-content)) |
| `port/game/game_state_keep.h`, `game_state_keep.c` | the game's state kept across the credits playback and a test room, put back at the title |
| `port/game/title_logo.h`, `title_logo.c` | the title logo hidden under the port's menus, the title's full-frame models |
| `port/game/subtitles.h`, `subtitles_ru_bobttz.c`, `subtitles_ru_cobra838.c`, `subtitles_pt_br.c` | the community translations' subtitle tables: the words of each record each translates |
| `port/ui/subtitle_text.h`, `subtitle_text.c` | the subtitles of the community translation in force, drawn as text in `jimakuDisp`'s place of the strips |
| `port/ui/strings_ru_bobttz.c`, `strings_ru_cobra838.c`, `strings_pt_br.c` | the Russian (bob-ttz), Russian (cobra838) and Brazilian Portuguese (Igor Chagas, ichagas7) tables of the port's strings and the European disc's menu words |
| `port/platform/steam_shortcut.h` | Add to Steam |
| `port/platform/gpu_driver.h` | the Android build's graphics driver packages |
| `port/game/state_host.c`, `state_rules.h`, `state_rules.c` | save states: the save, the load, the slots, the picture, the [dev] keys; the conditions and the listing |

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
| `[gameplay] shadows_together` | `enemy_act.c` (`EnemyUtil_isOtherStatus`, through `ico_opt_shadow_waits`) |
| `[gameplay] door_fix` | `door_grace.c`, from `boyact.c` |
| `[gameplay] yorda_trips` | `yorda_trip_live.c`, from `icoMisc.c` and `girl_act.c` |
| `[gameplay] natural_chains` | `BgAnimation.c` (window build) |
| `[gameplay] skip_opening` | `kanbanBoot.c`, `op.c`, `main_host.c` (its log line) |
| `[gameplay] hold_type` | `mcard.c`, `layout_action.c`, `settings.c` |
| `[variants] *` (Gameplay, Gameplay > Shadows, Gameplay > Combat and Gameplay > Cutscenes) | `port/game/variants.c`; the game hooks in `debug.c`, `enemy.c`, `enemy_act.c`, `queen.c`, `lws_kyomi.c`, `girl.c`, `commonact.c`, `boyact.c`, `weapon.c` ([Game variants](#game-variants)); `settings.c` (the Debug menu row); `gamestate.c` (the achievements rule) |
| `[gameplay] mirror` | `pad_host.c`, `audio_host.c`, rd, `photo_ui.c` |
| `[developer] free_movement` | `boyact.c` (the four boy control loops), `gamestate.c` |
| `[input] kb.hint`, `mouse.hint`, `pad.hint` | `lws_kyomi.c` (`LwsKyomiGeo`), through `ico_hint_ask` |
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

### Yorda trips

In the original game Yorda trips and falls in one place, the main gate's second stage
(`st04a (GATE_2ND)`): its script counts the ticks her run-pulled bit is set and, once more than
3 seconds' worth have passed, mails her the sit-down interrupt (`ico2/script/src/st04a.c`
(`actSt04aGirlSitChk`): mail 109 on the European, Japanese, Korean and Asian discs, 102 on the
North American disc, 101 on the August 2001 preview, 100 on the June 2001 demo). `actGirlHand` sets
the bit in its pull state 2, where Ico runs pulling her (`ico2/fumi/src/girl_act.c`, one copy per
disc). The interrupt's motion-orient row from her run loop, `GIRL HAND_RUNLOOP`, plays
`GIRL SIT(HETARI)`, then `SITLOOP(HETARI)` until Ico moves on, then `SITEND(HETARI)` back into the
hand walk or run loop (`SIT3END` when he stops); she stays in the hand mode, and Ico's reaction is
the game's own. The watcher also sets her exhausted motion set's bit (paraStatus 42 on the European
program, `ico2/fumi/src/act-game.c` (`ACTParaStatus_Exec` and its 2001 copies)); the port never
sets it.

The three fall motions are per-stage data (motion area 4), which every stage change clears
(`ico2/sugipon/src/motionFileManager.c` (`ResetDynamicMotionManager`)), and only the pack of the
main gate's second stage carries them (`STG4A2ND.DF` on the European, Japanese, North American and
August 2001 discs, measured from the discs; the June 2001 demo's `DATA.DF` has no pack with them).
With the option on, each `InitIcoMisc` copy calls `ico_yorda_trip_stage_loaded` right after the
stage's pack (`ico2/common/src/icoMisc.c`), and every per-stage motion of hers named `HETARI`
that the stage did not load goes into the stage's motion memory as `ReadMotionFile` puts one
there (`ico2/common/src/charFileManager.c`): a block of `ios_partition_dmotion`, the file copied in,
`InitMotionFile` (which relocates it in place, so it lies in the arena) and `AddMotionMemorySize`
on the stage segment; the next stage change clears them with the rest
(`port/game/yorda_trip_live.c` (`ico_yorda_trip_stage_loaded`)). Nothing is added in the two main
gate stages, or while less than 1 MB of the partition would stay free. The motions (six, about
116 KB, on the European disc; five, about 120 KB, on the 2001 discs) are found by name in the disc's
motion table and read once a run from the first pack whose motion files hold `SIT(HETARI)`, on a
second mount of the disc with an index of its own (`port/game/yorda_trip_source.c`
(`ico_yorda_trip_find_ids`, `ico_yorda_trip_source_read`)); the read is the port's disc I/O on the
game thread during the stage load, and the only lock it takes is that mount's own. A save state
carries the motions with the arena and `motionTable`; nothing in it points at the host copies.

The hook is `ico_opt_yorda_trip_tick` right after each `actGirlHand` copy sets the run-pulled bit,
with that disc's mail. The rule (`port/game/yorda_trip.c` (`ico_yorda_trip_step`)) is the main
gate scene's own (`ico2/script/src/st04a.c` (`actSt04aGirlSitChk`, and the June 2001 demo's
`actSt04aGirlSitChk_jun26`)): consecutive run-pulled ticks are counted by `frame_count` (a missed
tick, the scene's tick without the bit, starts the count again); once the count passes
`((60 - systemStatus[0] * 10) / systemStatus[1]) * 3` ticks (3 seconds: 25 ticks a second at
50 Hz, 30 at 60 Hz, so the trip comes on the 76th or the 91st tick of a run), the count starts
again from 0 and the mail goes, the watcher's own `iosOmSendMail(girl, mail, girl)`, so every
further 3 seconds of running hand in hand trip her again. There is no random roll and no wait
between trips. Where a guard fails on that tick no mail goes and the count starts again all the
same, as the scene's count does after a mail that does not take effect.
The guards (`yorda_trip_live.c` (`guards`)): she is in `GIRL HAND_RUNLOOP`; the three fall motions
are loaded, and every motion their motion-orient rows lead on to is the stage's own or loaded (the
European disc's second motion set, six stages, has no `SIT3END` and no hand loops); her exhausted
bit is clear; the game is not paused (`systemStatus[5]`) and Ico is under control
(`IsAbleBoyControl`, `scpBoyControlReadDisable`); no game over; the stage is not one of the main
gate's two (its name starts `st04a `). The count and its tick stamp are save state blocks
(`yorda_trip.run_ticks`, `yorda_trip.last_tick`; `port/platform/state_port.c`), so a state resumes
the count; both are FRESH ([PLATFORM.md](PLATFORM.md#the-registry)), so a state another build made
without them still loads and the count goes on from the running game's. `gameplay.yorda_trips` is
a simulation key (`port/platform/sim_keys.c`, default `false`): a state saved before the key
existed loads while the option is off.

Log lines: `gameplay: yorda trips on|off` at the first read and on a change; at the first read of
the motions `yorda trips: N of her M motions read from STG4A2ND.DF (B bytes) in T ms`, or why
not and `she does not trip on this disc`; at each stage load with the option on `yorda trips: N
motions (K KB) added to the stage's motion memory (stage S, F KB left)`, `stage S loads her N
motions itself`, `stage S (name) plays her own fall: nothing added`, or `the stage's motion memory
is full (...): Yorda does not trip here`; on a trip (the first 8, then every 100th) `yorda trips:
tripped at stage S after 3 s of running (trip N this run)`.

`port/game/test/yorda_trip_test.c` runs the rule, the guards and the stage load on each disc;
`yorda_trip_disc_test.c` lists the motions and their pack on the configured disc image
([TESTING_PROGRAMS.md](TESTING_PROGRAMS.md)).

### Shadows attack together

The shadows take turns in the original game, and the rule is one function,
`EnemyUtil_isOtherStatus` (`ico2/fumi/src/enemy_act.c`): it scans every object of kind 4 (the
enemies) but the asking one and returns the first that is in act mode 15 (`ACTCheckFlagAttack`
in `commonact.c`: the attack, entered by `actEnemyAttack`) or has its attack-mail flag set (the
flag `enemyDodgeSendMail` raises as it mails the attack: the mail numbers are the ones
`actEnemyAttack` sends on a follow-up, 0xCD on pal/jpn/kor/cn, 189 ntsc, 186 aug6, 183 jun26).
Its eleven callers hold the asking shadow back while that is non-zero:

- `subEnemyBrain_ToBoy` (and `_ntsc`, `_aug6jun26`), three arms each: after the approach and the
  wait at the battle ring (`Battle_isCurrentStatus`), the change to the ATTACK brain
  (`ChangeBrain_ToAttack`) only when no other shadow attacks; one arm also asks to be the
  shadow nearest Ico (`isNearestEnemyToBoy`), which the option keeps;
- `enemyDodgeSendMail` (and `_jun26`): the attack mails of the ATTACK brain
  (`subEnemyBrain_Attack`, its first sixth of a second and, but on the June 2001 demo, its next
  half second) and of the strike a shadow on its way to Yorda makes when Ico, close by, turns to
  face it (`enemy_dodge`: a shadow whose enemy flags' bit 1 is clear, within 200 units on
  pal/jpn/kor/cn, and on ntsc/aug6/jun26 only one not of size class 0; the others dodge there,
  which no turn limits), only when no other shadow attacks.

So one shadow attacks at a time; the rest wait at their ring and try again each frame. Nothing
else limits them: the enemy brain (`ico2/omori/src/ebrain.c`) sends up to 31 shadows after Ico and
up to 31 after Yorda at once (`eBrainGetTarget`'s `<= 31`), and the per-disc enemy tables
(`ico2/sugipon/src/enemyKind.c`: life, scale, fly type, battle type) and the start records'
attack chances carry no turn value. Yorda has one carrier (`carrier` in her act record, the
brain's `girlHolder`): when one shadow holds her, the others chasing her become its bodyguards
(brain status 5, `BODYGUARD`). That is physical, not a turn, and the option leaves it alone.

`[gameplay] shadows_together` puts one check after the scan: `EnemyUtil_isOtherStatus` is the
game's scan under the name `EnemyUtil_isOtherStatus_disc`, and the function the callers keep
calling returns its result only when `ico_opt_shadow_waits(other != 0)` says the shadow waits
(`port/game/options.c`): off, whenever another attacks (the game's value); on, never. The approach,
the ring, the wait, the attack chances (`attackChance`, `attackChance2`), the dodges, the nearest
rule, the kidnap and the escort are the game's. The value is read at each check, so a change acts
at once. `gameplay.shadows_together` is a simulation key (`port/platform/sim_keys.c`, default
`false`): a state saved before the key existed loads while the option is off.

Log lines: `gameplay: shadows attack together: 1 at once (the game's turns)` or `...: all at once
(no turns)` at the first read and on a change; with the option on, each turn it skipped (a shadow
that the game would have held back), the first 8 then every 100th: `gameplay: shadows attack
together: a shadow did not wait for another's attack (N this run)`.

`port/game/test/options_test.c` (`test_shadows_together`) runs the answer off and on, its count
and log lines, and scans `enemy_act.c` for the eleven callers and the hook; `settings_extra`
(`testShadowsTogether`) the row.

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
among the keys a pad recording's header records (`port/platform/sim_keys.c` (`ICO_SIM_KEYS`);
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

Options > Gameplay: Light sword colour and Stick on the page, No cutscene look-around and Ignore
cutscene cameras on Gameplay > Cutscenes, and the Debug menu on Developer (`[variants]`,
[CONFIG.md](CONFIG.md#variants), [UI.md](UI.md#gameplay--yorda-and-cutscenes)); No hints, Shadow
toughness, Shadow aggression, Peaceful shadows, Automatic guard, Turn to face shadows, Shadows wait
while Ico is down, One hit per swing, Shadows never dodge and Queen barrier on the New Game screen's
Customize pages, a journey's difficulty ([UI.md](UI.md#customize-the-new-game-drafts-pages)). The values are
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
| Chain swing speed | `debug_chain_cycle_speed` (the debug row `CHAIN CYCLE SPEED`, 1..10), bound by `ico_variants_bind_debug_chain`; 4 (`debug_VariableInit`'s value) leaves it alone | `ico2/omori/src/chain.c`: `initPendulum` (from `StartPendulum`) and `InitPendulum` |
| Chain swing slowdown | `debug_chain_slow_speed` (`CHAIN SLOW SPEED`, 1..10); 4 leaves it alone | `chain.c`: `DecreasePdlChain`, called from `boyact.c` while Ico is on a chain without the swing button (`pad.now & 0x20` clear) |

The chain rows (Gameplay > Yorda, `[variants] chain_swing_speed` and `chain_swing_damping`, integers
1 to 10, index 0..9 in `ICO_VARIANT_CHAIN_SWING_SPEED` / `_DAMPING`) hold two more debug switches,
bound by a second call, `ico_variants_bind_debug_chain`, that `debug_VariableInit` makes right after
`ico_variants_bind_debug` (so the first call's signature did not change); nothing else in `ico2/`
changed. Both values are set to 4 by `debug_VariableInit` on every disc (the lines are outside the
region branches) and the table rows exist in both of `debug.c`'s row tables; `chain.c` is one source
for all seven discs (the region macros in it are in other functions), so `initPendulum`,
`InitPendulum` and `DecreasePdlChain` read the same two variables everywhere. The formulas:
swing speed `a = clamp(2 - 0.2 v, 0.1, 2)`, `cycle = clamp(int(a * 6 * sqrt(length / 2.5) * 0.8), 1,
255)` with `length = holdNode * 50` (`holdNode` >= 2, the node Ico holds), the swing's period in
ticks (`phase` runs 0 to `cycle`); slowdown `ampSpeed = v * 0.5 * -0.1 = -0.05 v` per tick while Ico
is not pumping (`IncreasePdlChain` is +0.1). `cycle` by value, in ticks: holdNode 2: 54 48 42 36 30
24 18 12 6 3; holdNode 8: 109 97 85 72 60 48 36 24 12 6; holdNode 20: 172 153 134 115 96 76 57 38 19
9 (v = 1..10; v = 10 is `a = 0` before the clamp and `0.1` after, so 10 is meaningful: the
shortest swing, not a zero period). No two values give the same period (the closest pair, 9 and
10, is three ticks apart at the shortest hold) and none reaches the minimum of 1, so all ten are
offered. The swings that end (`chain_simulate_term_*`, a rope turn, a loop, the swing-ready ramp)
set `ampSpeed` themselves and are not covered by the slowdown row. Neither row suspends
achievements.

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
every playthrough when Shadow toughness is American or Not tougher later, and the table's entry
`gameParam[40]` when it is Third journey (`ico_variant_shade_toughness() == 3`, the `idx` the clamp
to 38..43 then passes); the 2001 builds' and the USA disc's restarts have no factor.

Third journey. The game multiplies a restarted or started Shadow's table life by
`gameParam[38 + gFlagGameClear]` (clamped to 38..43), and `gFlagGameClear` is set to 1 once
(`ico2/script/src/end.c`), so entries 40..43 are never read in play. The six factors on every
disc, read from each boot ELF's `gameParam` (the table's address from `port/data/gen/<slug>/table_desc.c`):

| disc | `gameParam` entries | [38] | [39] | [40] | [41] | [42] | [43] | Third journey |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| pal | 44 | 1.0 | 1.5 | 2.0 | 2.0 | 2.0 | 2.0 | offered (x2) |
| jpn | 44 | 1.0 | 1.5 | 2.0 | 2.0 | 2.0 | 2.0 | offered (x2) |
| kor | 44 | 1.0 | 1.5 | 2.0 | 2.0 | 2.0 | 2.0 | offered (x2) |
| cn | 44 | 1.0 | 1.5 | 2.0 | 2.0 | 2.0 | 2.0 | offered (x2) |
| ntsc | 36 | none | none | none | none | none | none | hidden: no factor in the restart |
| aug6 | 36 | none | none | none | none | none | none | hidden |
| jun26 | 32 | none | none | none | none | none | none | hidden |

Entry 40 differs from entry 39 on the four discs that have the factor (2.0 against 1.5), so the row
is a real step up; on the other three the table is shorter than 38 entries and their restart code
(`actEnemyRestart_ntsc`, `_aug6`, `_jun26`, `actEnemyStart_*`) takes `GetEnemyDefLife` as it is. The
value is hidden there by `third_journey_offered` (`variants.c`, `!ICO_NTSCAUG6JUN26`):
`ico_variant_offered`, `ico_variant_set` (Original), `ico_variant_step` (passed over) and a config
`third_journey` (Original, with `variants: shade_toughness = third_journey is not offered on this
disc (its restart life has no playthrough factor); using original`).

Peaceful shadows. The game's `BrainOnOff` row sets `debug_brain_flag` (`debug.c`, 1 from
`debug_VariableInit`); it is read at the four Shadow starts in `enemy_act.c` (`actEnemyStart_ntsc`,
`_jun26`, `_aug6` and the European `actEnemyStart`: `if (debug_brain_flag != 0)
actCreateSubThread(subEnemyBrainMain, 20)`) and at Yorda's four starts in `girl_act.c`, so the
variant does not bind the flag: each Shadow site reads `debug_brain_flag != 0 &&
ico_variant_shadow_brain()` (one `extern`, `variants.c`; 1 unless `peaceful_shadows`, counted in the
log: the first 8 Shadows, then every 100th, `variants: peaceful_shadows: Shadow N starts without its
brain`). Without `subEnemyBrainMain` the Shadow keeps its `subEnemyControl`, `subEnemyCollision`
and `subCommonIdle` threads: the brain is what sets the Shadow's mode and its movement brain
(`CheckEnemyBrainMode`, `BrainMode_Requset`, `actChangeActBrain`) and sends the mails for the first
status (carry Yorda, approach, wait), so with no brain `stick` stays at rest, `subEnemyControl`
sends only the idle mails, and the attack, approach, dodge and kidnap decisions (all in the brain
modes and their `ChangeBrain_To*`) never run; the collision thread (`CommonAttackCenter`, hit
reactions) and the idle thread still run. Not read in play on a disc.

Local reads.

| row | site |
| --- | --- |
| No hints | `ico_hints_on()` (0 on the USA disc and the 2001 builds, and with No hints on): `LwsKyomiGeo` (`ico2/omori/src/lws_kyomi.c`) keeps the hint from being the stage's waiting one, so its timer stands still and a playing hint animation stops; `GirlAI` (`ico2/sugipon/src/girl.c`) does not ready or play the hint voice. The hints' finished and asleep flags (`FinishHint`, `SleepHint`, `WakeupHint`, set by the stage scripts), `Hint_Init` and the save's hint block are untouched, so a save keeps the scripts' progress and turning hints back on resumes them |
| Queen barrier | `ico_variant_barrier_hits()` (5, or 7 on the June 2001 demo, unless chosen): `QueenBarrierGeo` and its June 2001 twin (the hit that breaks it) and `QueenBarrierDL` (the barrier's fade divisor), `ico2/ito/src/queen.c`; the tables indexed by the hit count have 11 rows |
| Automatic guard | `ico_variant_auto_guard()` (key `auto_guard`, On = the game's): the guard rule of `_ACTCorrectMsg` case 13 (`ico2/fumi/src/commonact.c`: the European arm, `_ACTCorrectMsg_jun26`, `_aug6` and `_ntsc`, one copy each) is `_AbsRotyGV(orient, attackDir) < 60 && ... && !(flags18 >> n & 1)`, then `iosOmSendMail(self, 256 / 264 / 267 / 283, param)`; one conjunct `(self != boyGObj \|\| ico_variant_auto_guard())` so only Ico's guard is switched (the same rule runs for every actor). Off: the guard mail is not sent and the hit falls on to the damage message the function goes on to build (`dir + 260`, the down message 264, or the death message 265) as a hit from behind does. The stone-hit paths above it and `unguardable` are untouched. The guard act (`actCommonGuard`) is not entered, so its recoil and effect do not play |
| Turn to face shadows | `ico_variant_attack_turn()` (key `attack_turn`, On = the game's): `actBoyAttack` (`ico2/fumi/src/boyact.c`; the European, `_ntsc` and `_jun26` copies) searches for the nearest enemy within 300 units and 90 degrees (`searchEnemy`, kept so `attackTurn` is as the game sets it) and turns Ico toward it every frame with `SetMotionDirectionWithLimit` (5 degrees a frame up to 45 bare-handed, 10 up to 90 with a weapon); Off skips the turn only (`attackTurn && ico_variant_attack_turn()`). The swing's hit volume is the weapon's own and is not narrowed. `actEnemyAttack`'s own use of `attackTurn` (`enemy_act.c`) and `actCommonDodge` are not changed |
| Shadows wait while Ico is down | `ico_variant_shadows_wait_down()` (key `shadows_wait_down`, On = the game's): `IsBoyStatus_EnemyMustWait` (`ico2/fumi/src/boyact.c`), read by `ebrain.c` (`enemiesWait`, which holds every Shadow's brain in status 8) returns 1 for act modes 0x16 and 0x17, for 0x13..0x15 while the boy is stone (`stoneLevel != 0`), and for act mode 0x15 or `downTimer > 0` (`actAfterDown`, `commonact.c`: 130 frames at 60 Hz, 108 at 50 Hz, on every disc). Off drops only the last pair (mode 0x15 and the get-up timer); the stone part and modes 0x16..0x17 stay. The Shadows' dodge decisions (`IsBoyStatus_NotDanger`) are not touched. The second mechanism is the `hit` flag: the down action (`actCommonDown`, four copies: `_ntsc`, `_jun26`, `_aug6` and the European/Japanese/Korean/Chinese body, `commonact.c`) sets `GOBJ_ACT(self)->hit = 1` every frame of its loop for every actor but kind 4, and `AttackGenerate*` (`ico2/omori/src/attackhit.c`) skips a target with `hit != 0` (the game's "invincible ok"), so a swing passes through a lying Ico; the flag clears again in act mode 1 (NTSC and 2001 discs) or 1 to 3 (the European family) (`act-game.c`). The loops now set it only when `ico_variant_down_keeps_hit(self == boyGObj)` (`variants.c`): 1 for every other actor and for Ico with the row On (the game's), 0 for Ico with it Off. A second knockdown while he is down follows the game's own damage handling (`_ACTCorrectMsg` case 13: the guard check, then the down message), which can chain knockdowns. Log line (first 8, then every 600th): `variants: down_keeps_hit: Ico's down action leaves his hit flag clear (hittable while down) is off (N)`. The lever list of a hard mode is in [HARD_MODE.md](HARD_MODE.md) |
| Light sword colour | `ico_variant_weapon_kind(kind)` (key `sword_colour`, `"original"` or `"red"`): each disc's `InitWeaponGeo` (`ico2/sugipon/src/weapon.c`: the European body and `_ntsc`, `_aug6`, `_jun26`) sets `w->kind` from the layout's `lay->obj & 0xFF` (`objLayout` `initArg`, `sceneManager.c` `initSceneGObj`) through it: with Red, kind 9 (the light sword of a cleared game, `objLayout` 1772 and the makers' row 87) is made as kind 8. The two kinds share `InitWeaponGeo`'s case (the blade's two models, `setWeaponOffsetMode(g, 1)`), `WeaponGeo`'s blade branch (`kind >= 8 && kind < 10`), `dispLaserSword`, model 85 and accessary 19; their `weaponKind` rows are the same bytes but the trail's colour (8: 255, 32, 32, 80; 9: 215, 255, 248, 80), which `dispBlur` reads. Two kind tests differ: `dispBlur`'s `kind == 8` block (an empty packet with alpha mode 5, then an unmatched `gif_EndPacket` while the blade is under 12; on the host only a flush and a DMA tag no one replays) and `PutWeapon`'s `kind == 9` offset (`ico2/fumi/src/boyact.c`), which reads `ico_variant_weapon_put_kind` (8 reads as 9 on every disc that offers Red, so a red sword still in play after the row goes back to Original is set down as kind 9 is; the only other kind-8 placements, `objLayout` 12 and 86, are the makers' unused rows), so the sword set down stands as the game's. The achievements count 8 and 9 alike (`light_blade`). Only the European, Japanese, Korean and Asian discs have the kinds (10 rows); Red is not offered elsewhere (`ico_variant_offered`) and the row is hidden there (`settings_variants.c` `ui__variant_shown`). From the next room: the kind is read when the weapon object is made |
| Stick | `ico_variant_weapon_kind(kind)` (key `weapon`, `"original"` or `"net"`): the same read; with Insect net, kind 1 (the stick, the only kind that makes a torch in `InitWeaponGeo` and the only one `ACTGame_isWeaponCombustible` lights, `act-game.c`) is made as `ico_variant_net_kind()`: 7, or 6 on the June 2001 demo, whose `WeaponDL_jun26` draws `dispInsectNet` at case 6 (its table has seven rows). The net's row on every disc: length 120, grip 40, power 0 (`attackhit.c`: attackData's power times the row's: no damage), weight 1500 (the stick's), flags 0, request kind 0. `WeaponDL` (every copy) draws it with `dispInsectNet`: 17 Gouraud lines in list 11 (the handle from z -50 to 100 and a 16-segment hoop of radius 30 at z 130), grey 128, no model. It is a weapon to the boy (`ACTGame_NoWeapon` is kind 0 only; `CheckSwapableWeapon` passes over kind 0 only), so he picks it up, holds and carries it; his swing asks the motion-orient table for the weapon's request kind (`ACTGetOrientFromIntrK` cases 205-207 European, 189-191 and the 2001 twins: `ACTGame_GetMotOrientFromWeapon`), 0 for the net, and no row of any disc's table has request 0, so `getMotionOrient` answers the table's last row, the "no motion" row (id = next = 1145 European, 1084 NTSC, 1086 AUG6, 1165 JUN26), and the swing does not start (inference from the tables: no game run). No layout row of any disc places the net. From the next room, as Light sword colour |
| Debug menu | the row's press (`port/ui/settings.c`) calls `ico_variants_debug_menu_pressed`, which opens the menu through the bound `debug_MenuOpen` (`menuState` 1 as SELECT sets it, `debugBackGroundDisableFlag` 1, the first row) and returns 1; Options then closes (`closeForDebugMenu`): from the title back to the title, from the pause menu into the game as the pause menu's Back does (`la_host_resume_play`, `ico2/common/src/layout_action.c`: the sound resumed, `la_game_loop`). `debug_Menu` runs in developer mode only (`main.c`), and the row shows only then. Unbound, the press logs, Options stays and nothing is marked used (achievements stay on). PC port: on the June 2001 demo the row is never shown (`settings.c` `optShown`): its `debug.c` option table (67 rows against the demo's 62) and stage-select flags are the preview's; `variants: the Debug menu is not offered on the June 2001 demo (its debug tables are the preview's)` once, when Developer mode would show it. SELECT in Developer mode still opens the game's menu there (game code) |

Log lines: `variants: the game's debug switches are bound (...)` once per `debug_VariableInit`;
`variants: applied shade_toughness=... ; one_hit_only=... hand_camera=...` once per apply (a
switch the variants do not hold shows as `(game's)`); `variants: enemy kind N is past this disc's M
enemy rows; its last row stands in` once; `variants: the Debug menu cannot open: the game has not
bound it`; `settings: Options closed for the Debug menu (to the title|back to the game)`;
`variants: sword_colour: the light sword (kind 9) is made as kind 8, its trail red (N)` and
`variants: weapon: the stick (kind 1) is made as the insect net (kind K) (N)` at each weapon made,
the first 8 then every 100th; `variants: sword_colour = red is not offered on this disc (it has no
light sword); using original` at a load; `settings: no Light sword colour row on <disc id> (the game
has no light sword)` once per disc; the `variants: applied` line carries `sword_colour=`,
`weapon=` and `(net kind K)`.

### Free movement and Ask Yorda for a hint

Two rows built on code the discs carry and no retail path reaches (`port/game/ico_rows.h`).

**Free movement** (Options > Developer, `[developer] free_movement`, default false; needs Developer mode,
so `ico_free_movement` is the row ANDed with `ico_opt_developer_mode` and a hidden row does nothing).
`ACTDebugMove` (`ico2/fumi/src/act.c`; `ACTDebugMove_jun26` on the June 2001 demo, the same inputs) is
called from the four boy control loops (`boyact.c` `subBoyControl` and its `_ntsc`, `_aug6` and `_jun26`
copies, and `way_tool.c`'s cursor), each behind `int dbg = 0` and a test of the loop's own pad:
`self == CurrentTargetGObj && (pad.trg & 1)`, an L2 press. The port changed the four tests to
`dbg || ico_free_movement()`. Ico is already the debug target: each copy of the boy's start-up sets
`CurrentTargetGObj = self` after its first `_ACTWait(1)` (`boyact.c`, four sites), and nothing in the
game or the port resets it except the Debug menu and the model viewer, which put it back. What the
move reads, all from Ico's own pad (`iosPadRead(&ext->pad)`, the pad after the game's Button
configuration): L2 held keeps it going (`pad.now & 1`); the left stick (`iosPadGetStick` stick 0) moves
him 32 units a frame at full tilt over the ground, camera relative (`ConvertStickToAbsCoord`); the
right stick (stick 1, `dz` times `mag`) moves him up and down 32 units a frame, which also leaves the
floor-following state (`mode` 1); L3 (`pad.trg & 0x200`, `ICO_PAD_L3`) is the snap button: in the free
state it puts the root update back and the floor under him is found each frame (`ClipFloor`, then
`ClipFloorR`), in the floor state it drops him on the floor found below. It ends when the floor state
holds and L2 is let go, with a last floor fit (`AdjustMotionHeightToNearestField`). The move draws the
game's own crosshair lines (`DrawLineG`) and a dummy text. `[developer] free_movement` is a simulation
key (`sim_keys.c`), so a pad recording and a save state carry it. `ico_gs_achievements_suspended` names it
beside developer mode. Log: `developer: free movement on|off (row .., Developer mode ..)` once per change.

**Ask Yorda for a hint** (Options > Controls > Remap controls, target `hint`, `ICO_T_HINT`, the last row;
keyboard, mouse and gamepad columns, unbound by default; touch: none; [INPUT.md](INPUT.md)). The game's
`debug_hintStart` (`debug.c`) loops the objects for `IsTopHint` and calls `DebugHintStart`
(`lws_kyomi.c`), which sets that hint's timer to its trigger time; nothing calls it. The port calls it
from `LwsKyomiGeo` instead, for the hint object that is the stage's waiting one (`hint->flags & 1`, the
test of `IsTopHint`), once for each press: `ico_hint_ask` takes the press the device layer counted
(`ico_hint_ask_note`, once a vsync of the first player's snapshot; a press older than 12 snapshots is dropped
because nothing was waiting for it). The timer is then past its time on the same tick and the hint's
animation starts as it does when the wait ends. It does nothing with No hints on (`ico_hints_on` clears
the waiting flag first) or in a room with no waiting hint. The code exists in the final-PAL-family
builds only: `LwsKyomiGeo` and `CreateKyomiGObj` return early on `ICO_NTSCAUG6JUN26` (the North American
disc and the two 2001 builds have no hint objects), so the row is hidden there (`REMAP_TARGETS` in
`settings.c`; `settings_discs`). Log lines: `hint: the Ask Yorda for a hint button was pressed (n this
run)` and `hint: the waiting hint starts now, asked for (n this run)` (the first 8, then every 100th); a
press line without a start line means no hint was waiting. The press is not part of a pad recording.

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
| Main: Game language's community entries (Русский (bob-ttz), Русский (cobra838), Português (Brasil) (ichagas7)) | W | H | H | H | H | H | H | the row's own rule (`has_game_language`); in force only there with the game in English (`ui_settings_text_language`), so `[game] text_language` changes nothing on the other discs ([Community translations](#community-translations)); `subtitles_ru`, `subtitles_ru_<slug>` (jpn, kor, cn), `subtitles_ru_cobra838`, `subtitles_ru_cobra838_<slug>`, `subtitles_pt_br`, `subtitles_pt_br_<slug>`, `jimaku_text` |
| Main: Menu language | W | W | W | W | W | W | W | Auto is `ui_lang_default`, the sheets `menu_sheet`; `settings_<slug>`, `settings_render_<slug>` |
| Controls: Hold type | W | W | W | W | W | W | W | `hold_type_own`, the default `"disc"`: A on pal/jpn/kor/cn, B on ntsc/aug6/jun26 (the 2001 arms `_ACTCommonMailTest_<disc>` read `optionControlType` in the port); `settings_discs`, `options_<slug>`, `save` (per disc) |
| Controls: Circle goes back | W | H | H | W | W | W | H | `pad_back` is Triangle (`lt_ext_has_circle_back`); the 2001 arms cancel through `LA_BACK` too; `settings_discs`, `settings_<slug>` |
| Picture: Brightness; Controls: Vibration, Button configuration (pause) | W | W | W | W | W | W | W | `systemStatus[11]`, `iosPadActRequestEnable`, `layout_key_config` (`la_key_config` and its 2001 arms go back to Settings); `settings_discs` |
| Picture: Downsampling | W | W | W | W | W | W | W | the port's own renderer option (`[video] downsample`, `ico_video_downsample`), no game hook; `options`, `settings` |
| Picture: Upscaling, Upscaling quality, Sharpness | W | W | W | W | W | W | W | the port's own renderer option (`[video] upscaling`, `upscaling_quality`, `upscaling_sharpness`, `ico_video_upscale`; FSR 1.0 at the present, FSR 3.1 at the upscale point on Vulkan), no game hook; `options`, `settings`, `rd_present` |
| Extras: Add to the applications menu | W | W | W | W | W | W | W | the port's own row (Linux only, title screen): writes the desktop entry and the icon ([APP_ICON.md](APP_ICON.md#the-applications-menu)), no game hook; `desktop_entry`, `settings_desktop` |
| Gameplay: stick fix, Yorda, door fix, natural chains | W | W | W | W | W | W | W | the hooks sit in every disc's arm (`backStage.c`, `ebrain.c`, `enemy_act.c`, `boyact.c` `CheckCollisionAttr_ntscaug6jun26`, `BgAnimation.c` `bga_CalcAnimation_<disc>`); `options_<slug>` |
| Gameplay: Time to rescue Yorda | W | W | W | W | W | W | W | one hook in each `actEnemyKidnapEnd` copy (`enemy_act.c`: the five SINK motions A to E, 948-952 on pal/jpn/kor/cn, 899-903 ntsc, 901-905 aug6, 934-938 jun26, all played inside that action: the brain's per-tick mail that switches the shadow to KIDNAP_END requests the dive's motion-orient kind, CARRY_LOOP/RUN to SINK_A at once, and that kind's rows step A to E at each motion's last frame): their play speed is reserved at priority 0 as `1/factor` (`2/factor` under a stage camera effect, which the demo has none of) and the camera's 5 s timeout is scaled by the factor (so in those scenes the pull-in is quicker than the seconds set and the set time scales it in the same proportion, the game's own 5 s of 15.4: 8 s takes her at about 2.6 s, 30 s at about 9.7 s; the row's note says "quicker in some scenes, in proportion"; this keeps Original byte-identical, where seconds-direct would move Original's 5 s to 15 s and never fire under the 2/factor speed); the factor is the seconds set x 30 over the disc's pull-in ticks (`port/game/pit_time.h`: 462, jun26 463), 1 for Original; the frame-50 test, the dissolve and the freeze (priority 9) are the game's; `options_<slug>` (the hook scan), `settings`, `settings_discs`, `pit_dive_base` / `pit_dive_base_<slug>` (the base off each disc image) |
| Gameplay: Shadow speed | W | W | W | W | W | W | W | `ACTGame_SetMotionPlaySpeedRatio_Exec` (`act-game.c`, one copy for every disc) multiplies the resolved play-speed ratio of a `kind == 4` actor by the factor (0.75, 1, 1.25, 1.5), except the freeze (ratio <= 0.001) and this disc's pit dive (the SINK motions, `pit_time`'s); walking and running are root motion, so the ground speed scales in step; the forward push of `flyCoreLoop` and its two per-disc copies (`commonact.c`) is multiplied for `kind == 4`; the turn limit of `SetMotionDirectionSmooze` (all three copies in `commonact.c`) is multiplied by the same factor for a `kind == 4` actor on those motions (`ico_opt_shadow_turn_limit`), so the tightest circle keeps the original's size; brain and wait timers and path finding are untouched; log `gameplay: shadow speed <name> (x<factor>)` and, first 8 then every 100th, `gameplay: shadow turn x<factor> ...`; `options_<slug>` (the source scan), `settings` |
| Gameplay: Shadows attack together | W | W | W | W | W | W | W | `EnemyUtil_isOtherStatus` (`enemy_act.c`), one copy for every disc with the June 2001 demo's own scan (`EnemyUtil_isOtherStatus_jun26`: the attack-mail flag is `flags18` bit 62 there, `flags20` bit 10, 1 or 0 on pal/jpn/kor/cn, ntsc, aug6); the eleven callers are the same on every disc (`subEnemyBrain_ToBoy` and its two copies, both `enemyDodgeSendMail` copies); `options_<slug>` (the hook scan), `settings_discs` |
| Gameplay: Yorda trips | W | W | W | W | W | K | W | each `actGirlHand` copy's run-pulled tick with the disc's mail (109 on pal/jpn/kor/cn, 102 ntsc, 101 aug6, 100 jun26) and both `InitIcoMisc` copies; the motions found by name; the June 2001 demo's `DATA.DF` holds no pack with her fall, so nothing is added and she never trips there (logged); `options_<slug>` (the hook scan), `yorda_trip_<slug>`, `settings_discs` |
| Gameplay: Skip the opening | W | W | W | W | W | W | W | `kanbanBootMain`/`_jun26`, `actOpDemo01`/`_ntsc`/`_aug6jun26`; `kanban_boot_<slug>`, `settings_extra_<slug>` |
| Gameplay: Second memory card | W | W | W | W | W | W | W | no game hook: port 1's folder (`ico_host_saves2_dir`, `memcard2-preview` on the preview); every disc's `_la_memory_card_check` (and `_jun26`) reads both ports when a save or load screen opens; `save_<slug>`, `settings_discs_<slug>` |
| Gameplay: Film effect (pause, once cleared) | W | W | W | H | H | H | W | `has_film_effect` (no `film_noise` in the 2001 stage tables, `reel_row` -1); `settings_discs` |
| Gameplay: Players (pause, once cleared) | W | W | W | W | W | W | W | the 2001 arms carry the European second-pad branches ([Players on the 2001 discs](#players-on-the-2001-discs)); the count kept per save beside the card there (`[players]`); `settings_discs`, `options_<slug>` |
| Audio: Sound output | W | W | W | W | W | W | W | `soundOutputModeSet` on every disc; `options_<slug>` |
| Developer: Developer mode | W | W | W | W | W | W | W | `achievements_<slug>` (the suspension) |
| Developer: Debug menu | W | W | W | W | W | H | W | `ICO_JUN26` (the demo's debug tables are the preview's); `settings_discs`, `settings_extra_<slug>` |
| Developer: Free movement | W | W | W | W | W | W | W | the four `subBoyControl` copies carry the game's debug move on every disc (`ACTDebugMove_jun26` on `jun26`); `settings_discs`, `ico_rows` |
| Controls: Ask Yorda for a hint (Remap controls) | W | W | W | H | H | H | W | `ICO_NTSCAUG6JUN26` (no hint objects: `LwsKyomiGeo_ntscaug6jun26`); `settings_discs` |
| Gameplay > Shadows: Shadow toughness American | W | W | W | W | W | N | W | `ico_variant_offered`; `variants_<slug>`, `settings_extra_<slug>` |
| Gameplay > Shadows: Shadow toughness Third journey | W | W | W | N | N | N | W | `third_journey_offered` (`!ICO_NTSCAUG6JUN26`: only the PAL-family restart reads `gameParam[38 + clears]`); `variants_<slug>`, `settings_extra_<slug>` |
| Gameplay: No hints; Gameplay > Cutscenes: No cutscene look-around | W | W | W | N | N | N | W | `ico_hints_on` is not in `GirlAI_ntscaug6jun26`; `debug_hand_camera` stays 0; `settings_extra_<slug>` |
| Gameplay > Shadows, Combat and Cutscenes: the other game variants rows | W | W | W | W | W | W | W | `variants_<slug>` |
| Gameplay > Combat: Shadows wait while Ico is down (the brain wait and the down action's `hit` flag) | W | W | W | W | W | W | W | `IsBoyStatus_EnemyMustWait` (`boyact.c`, one body) and the four `actCommonDown` copies (`ntsc`, `jun26`, `aug6`, the European family) call `ico_variant_down_keeps_hit`; `variants_<slug>` |
| Gameplay > Combat: Light sword colour | W | W | W | H | H | H | W | `ico_variant_offered` (kinds 8 and 9 in the disc's `weaponKind`); `settings_discs`, `settings_extra_<slug>`, `variants_<slug>`, `weapon_kinds_disc_<slug>` |
| Gameplay > Combat: Stick (Insect net) | W | W | W | W | W | W | W | the net's kind per disc (`ico_variant_net_kind`: 6 on jun26); `weapon_draw_<slug>` (made, found, picked up, drawn), `weapon_kinds_disc_<slug>` (the rows on the image), `settings_extra_<slug>` |
| Extras > Music | W | W | W | W | W | W | W | names by asset path, the region's stream bounds; `gallery`, `gallery_jpn`/`_kor`/`_cn` (the 2001 discs have no run: the test compares 105 rows) |
| Extras > Models | W | W | W | K | K | K | W | the model table has a Japanese column only (`tools/gen_mv_region.py`); the README names the 2001 discs' wrong models and the demo's missing ones; `model_viewer_jpn`/`_kor`/`_cn` |
| Extras > Characters | W | W | W | W | W | W | W | the characters' textures by name (`appearance.c`); `appearance`, `settings_extras` (from the title it runs in the model viewer) |
| Extras > Credits (title, once unlocked) | W | W | W | W | W | H | W | `credits_staff_packs` (the demo's DATA.DF has no `STGSTAFF*.DF`); `settings_discs`, `settings_extras_<slug>`, `credits_<slug>` |
| Extras > Unused content: Test room: arena, Test room: climbing (title) | W | W | W | W | W | H | W | the stage's pack in the disc's `DATA.DF` (`STGBOSS_TEST.DF`, `STG8TEST.DF`; the demo's 49 entries hold neither), the host's `available`; `unused_content_<slug>`, `settings_discs_<slug>` |
| Extras > Unused content: Attract movie (576 lines) (title) | W | H | H | H | H | H | H | stage 59 (`memoryPAL576`) is the European `stageData`'s only (`ico_region_stage(59)` is -1 elsewhere) and `pal_advertise576.pss` its `DATA.DF`'s; `unused_content_<slug>`, `unused_movie_<slug>`, `settings_discs_<slug>` |
| Extras > Unused content: Early show title art (title) | W | W | W | W | W | H | W | the member `object/sdf/e3/st13b/model/copy_right.p2o` in a pack of the disc (`ico_df_find_member`; the demo has no `STGLOG.DF`) and a member row the model viewer offers (`mv_member_first_row`); the row opens the viewer's list there, which offers the copyright only on pal (its `STGLOG.DF` holds no other) and all three elsewhere; the link itself hidden on the demo, one line; `unused_content_<slug>`, `model_viewer_rows_<slug>`, `charfile_image`, `settings_discs_<slug>` |
| Extras > Export textures, Add to Steam | W | W | W | W | W | W | W | the running disc's `DATA.DF`; disc-independent |
| Extras > Back up your data, Restore from a backup (title) | W | W | W | W | W | W | W | the user folder's files, every disc's at once (the card folders hold each disc's directory; `states/<slug>/`); the manifest names the disc played; `backup`, `settings_backup`, `settings_backup_<slug>` (jpn, kor, cn) |
| Photo mode (Film effect 1 to 4, Deja vu) | W | W | W | W | W | W | W | the grain is `gsb_filmNoise` on every disc (the 2001 arm's call is the port's); no reel scratches where `reel_row` is -1 (logged) |
| Achievements | W | W | W | W | W | W | W | `ach_stages`, `key_suffix`; `achievements_<slug>` |
| The New Game screen (Mirror mode, New Game+) | W | W | W | W | W | W | W | the start flag `gflag_new_game` (issue 147); `settings` `testNewGameStartFlags`, `settings_discs` |
| The title's Settings and Quit rows, the quit screen | W | W | W | W | W | W | W | `row_title_*` (placeTitle); `settings_<slug>`, `settings_render_<slug>` |
| The pause menu's Options (and its Extras) | W | W | W | W | W | W | W | `row_pause_options`, `layout_pause`; `settings_<slug>`, `settings_extra_<slug>` |
| Continue and the save slots | W | W | W | W | W | W | W | `layout_mc_files`, `row_mc_*`, `la_host_mc_slot_pickable`; `settings_extra` (pointer), `save` runs per disc; the Save states prompt (`layout_load_files`, `la_host_mc_files_shown`) is unit-tested per disc (`settings_states_*`), not played |

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
slow-step line's `achievements` component holds the render alone, not the drive. `test_write_path` in
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

The shadows' eye trails are the one draw that keeps screen positions from tick to tick: each eye
trails a strip of ten point pairs, the newest built once a tick by projecting the eye's point
through `+0x80` and `+0xC0` and joining it to the point before, the older ones the earlier ticks'
pairs shifted along (`ico2/sugipon/src/enemyParts.c` (`UpdatePointBlur`, `moveDataElements`)). The
paused game does not update them, so under a moved camera the eye's model follows the camera and
its strip would stay where the game camera put it. `UpdatePointBlur` also hands each point to a
side table of 64 trails keyed by the blur: the point's world position, the world position it is
joined to, its width and the pair the game wrote; a reset (the blur's dirty flag) fills the trail
with the newest point as the game copies the newest pair (`port/game/eye_trail.c`
(`ico_eye_trail_push`)). While `ico_photo_view_moved` is 1 (this tick's photo camera is not the
game camera at enter: moved or zoomed; 0 after leave), `DispEnemyEye` draws a strip rebuilt
from those points with the same arithmetic through the matrices now in `+0x80` and `+0xC0`,
frame-projected in the window build (`gif_HostFrameProjected`), and leaves the game's strip and colours as they
are, so play resumes from them (`ico_eye_trail_rebuild`). Through the game's own matrices the
rebuild is the game's strip byte for byte. A trail the table cannot rebuild is drawn as the game
drew it: one just started (its newest point joined to no point the trail saw), one the table does
not hold (after a save state load), or one whose strip is not the one its points recorded. The
first rebuild of a session writes `photo: eye trails drawn from the photo camera (10 points
each)`, and leave writes the session's counts (`photo: eye trails: N drawn from the photo camera,
M as the game drew them (...)`, `ico_eye_trail_photo_end`). Photo mode only: in play the strip is
the game's, not frame-projected.

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
comment); the early show art's model and texture are the exception, in the arena as the stage's own
(below).

The table ends with three rows no stage builds, the early show title art of Extras > Unused
content (`MvModel.member`): the E3 2001 title's three textured quads (`e3_ver.p2o`, `p_start.p2o`,
`copy_right.p2o` and the TIM2s they name, `e3_ver.tm2`, `p_start.tm2`, `scei_cr.tm2`, in
`object/sdf/e3/st13b/`). Every retail disc's logo pack `STGLOG.DF` carries them and the game loads
them with it at every boot, but only the E3 title script draws them, as stage animation objects of
kind 35 (`ico2/script/src/e3.c`, `objTableScene`), and no retail disc has that script's pack. A
member row's model id is its member's `PackEnt.id` in the running disc's `STGLOG.DF` (1577 to 1579 on
the Japanese, Korean and Asian discs, 1544 to 1546 on the North American, 1539 to 1541 on the August
2001 preview, 1595 for the copyright on the European disc, whose pack has only that one), asked once
per disc through `ico_df_find_member` and checked to lie in that pack (`mv_model_id`,
`model_viewer_table.c`); the list offers a member row only where the pack holds it
(`mv_model_shown`): one on the European disc, three on the other retail discs, none on the June 2001
demo. Its host stage is the Cage's quiet stage (European 31).

Once the host stage is up, `setup` has `mv_member_load` (`port/game/model_viewer_member.c`) register
the row's texture and then its model, as the pack holds them (textures first: the model's material
finds its texture by name when its object is built, `ico2/seki/src/Packet.c`
(`pac_getTextureInfo`)), through the game's own hook `ico_char_file_image`
(`ico2/common/src/charFileManager.c`, the `PC port` block at its end). The hook does what the pack
loader does with one member (`ico2/fumi/ios/cdvd.c` (`iosCdvdMgrPackLoad`)): the `initFunc` row of
the member's extension (`.p2o` is `ReadModelFile`, `.tm2` `ReadTextureFile`, on every disc's ELF),
then that loader's body on the image, then the load count (`debugCdvdLoadInfoSegAdd`). The bodies are
twins of the two loaders and their 2001 and demo arms, line for line with the read from the cdvd
handle replaced by a copy of the image, which the ctest `charfile_image`
(`port/game/test/charfile_image_test.c`) holds against the loaders and the pack loop. So the model
lands in `charFiles[id]` with the next `objSerial` and the texture in the texture table, in the
stage partition and malloc partition 1 a stage pack's members get; the next stage change clears both
as it clears the logo pack's own (`ResetCharFileManager`, `tex_Init`, the partition resets), so they
are left registered for the stage's life, and each pick of an art row loads the host stage afresh
and registers them again. It runs on the game thread from Main's tick, when no pack load can be in
progress (packs are loaded only by `InitIcoMisc` at a stage change; the loaders yield only inside the
handle read the twins do not call; the game's threads are fibers on one host thread). Then the
object is built as a stage builds a layouted object (`CreateLayoutedGObj`, as
`ico2/common/src/sceneManager.c` (`initSceneGObj`)): kind 35 (MOBJ: no constructor, drawn by
`p2o_DispVU1MultiDefault`, the static objects' `reg_DispObj`), at the origin with no turn and scale
1, light 0, display group 7, no layout row and no accessary. Because the loaders stop the game on a
failed allocation, the room is checked first and the load refused with a line when it is short: the
stage partition's free list (the members' bytes and 1 MB kept free, the largest block holding the
largest member), the texture table (`Texture.c` asserts at 200), the object pool (`isysGObjInit`'s
320). Lines: `unused: title art <member> (id N from STGLOG.DF) loaded for the viewer (texture
<name>, K KB read in M ms; stage S, F KB of its memory left)`, or `unused: title art <member> not
loaded for the viewer: <why> (stage S)`, then `model_viewer: failed: the early show title art could
not be loaded` and the way back to the title.

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
(`port/include/ico_credits.h`, header comment). The port lines are four blocks, each twelve blank
lines, a heading, four blank lines and its names with two blank lines between them, as the disc's
own `< Special Thanks >` spaces its names: `{R}< Decompilation and PC Port > ` and
`{R}Nathanial Fine `; `{R}< Translation > `, `{R}bob-ttz `, `{R}cobra838 ` and `{R}ichagas7 `;
`{R}< Contributors > `, `{R}katsifolis `, `{R}braito4 `, `{R}neetgon ` and `{R}KClough `;
`{R}< Special Thanks > ` (the disc's own heading text) and `{R}Sad Origami `, 87 lines in all
(`ICO_ROLL_PORT_LAST` 86). They are in the roll's own forms and ASCII without the backtick only,
because the roll draws with `font_Print`'s bitmap font (`port/game/credits.c` (`s_lines`, a table
of the non-blank lines at their indices, the others blank)). The roll logs
`credits: the port credit is posted` at the port's first heading and
`credits: the port's last line is posted` at Sad Origami. The 87 lines take about 16 s of the roll
at its 60 units a second (11 units a line), so the ending's later scenes come that much later
([DIVERGENCES.md](DIVERGENCES.md) GL-7). The game's side is three host lines in
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

## Unused content

Settings > Extras > Unused content offers, from the title, what the retail discs carry and nothing
in play reaches (`port/include/ico_unused.h`). The rows are keyed by the European stage number and
reach each disc's own through `ico_region_stage` (`port/game/unused_content.c` (`kRows`)); the
stage numbers and pack names are the ELFs' `stageData` (`port/data/gen/<slug>/table_desc.c`
`stage-all`):

| row | European stage | 104-row discs | June 2001 demo | needs in `DATA.DF` |
| --- | --- | --- | --- | --- |
| Test room: arena | 88 `boss_test` | 86 | 85 | `STGBOSS_TEST.DF` |
| Test room: climbing | 91 `8a_TEST` | 89 | 88 | `STG8TEST.DF` |
| Attract movie (576 lines) | 59 `memoryPAL576` (its `dataFile` is `#REF!`: no pack) | none | none | the loose `pal_advertise576.pss` |
| Early show title art | 1 `logo` (the art's pack); shown on 31, the Cage's | 1; 31's row | 1 | `STGLOG.DF` and its member `object/sdf/e3/st13b/model/copy_right.p2o` |

On the PS2 only the debug Stage Select reaches the two rooms (`ico2/common/src/debug.c`
(`debug_SelectStageMain`)); the title's attract picks stage 58 or 57 only (`ico2/script/src/op.c`,
`systemStatus[0] != 0 ? 58 : 57`), so the 576-line film is never shown; the early show art is
loaded with the logo pack at every boot and drawn only by a title script whose pack no retail disc
has.

- Availability (`port/game/unused_live.c` (`available`)): the row's stage exists on the disc and
  is below its `stage_count`; the stage row's `dataFile` plus `.DF` is the table's pack and is in
  `DATA.DF` (`ico_df_has`, as the model viewer asks for a host stage's pack); the loose file
  (`ico_df_has`); the member (`ico_df_find_member`). Each answer is asked once per disc
  (`ico_unused_available`), each refusal one line (`unused: no <row> row on <disc>: ...`), each yes
  one line (`unused: <row> on <disc> (stage N)`). The disc matrix's three rows are
  [Options on each disc](#options-on-each-disc)'s.
- The test rooms' start (`beginRoom`), from the title only (stage 1, no stage change, as the
  credits): the game's state is kept (`game_state_keep.c`, the credits' recipe), the title theme
  fades as a load from the title fades it, and `ico_host_enter_test_stage(stage)`
  (`ico2/common/src/debug.c`) enters the stage as Stage Select does: the sound segments and the
  stage's sounds closed, the pause menu allowed, the boards reset, Stage Select's flag pair (on
  before the change, off after: `PAL_NTSC_AUG6JUN26`), the boy's control on,
  `stgmgrForceSwitchWithFade(stage, 8, 4)` and `ACTGame_SetActors_Debug(stage, 1)`; the port writes
  no flag number. The menu closes on the game's empty layout; the stage's own init switches to the
  play layout, whose Start opens the pause menu.
- Where the rooms start (`placeAtStart`, after the hook): both rooms start where Stage Select
  starts a stage that has no entrance row, `exitData[0]`, the origin. `ACTGame_SetActors_Debug`
  (`ico2/fumi/src/act-game.c`) takes the first `exitData` row whose `nextStage` is the stage, else
  row 0, and writes three records there: the boy (`credits_boy_label`, kind 1) and the sword
  (`credits_boy_label + 1`, kind 14) where the stage's `attrTop` is set, the girl (`girl_label`,
  148 European, 131 North American, 130 August 2001, 124 June 2001 demo; kind 2) where its `flag0`
  is set. The engine writes the same three records again at `exitData[0]` with
  `gamesysObjInfoPosNewStageSet`: the arena (`attrTop` 1, `flag0` 0) the boy and the sword, the
  climbing room (`attrTop` 1, `flag0` 1) the girl too. The title has a boy (its `attrTop` is
  set), and the hook runs with flag 1 while `boyGObj` is still he, so as the game's pass the
  engine takes his `weapon` and `curItem` (`act-game.c`): a weapon skips the sword, and the
  weapon's and the item's records go to the start with the boy's.
- The sign rule: the game stores exit rows, as it stores layout rows, negated. `EXITDATA_GetNextPosition`
  (`act-game.c`) takes `pos = -exitData[idx].pos` and the rotation in degrees times 0.017453292,
  and `initSceneGObj` builds a scene object at `-gen->pos` (`ico2/common/src/sceneManager.c:478`);
  +y is down in the world (the floor probe casts from the root to `y + 10000`, `ico2/fumi/src/act.c`).
  A row copied as stored puts the boy wrong: the arena's first shadow row as stored, (-1049, 71,
  409), has the floor under it at y 3475, a fall of 3404 where 1000 is fatal (`gameParam[2]`,
  `commonact.c`): under the room's floor, a game over at once. Negated, the shadow row is
  over an st13b script-trigger floor (attribute 0x1000000) where the shadows rise; the origin is on
  the arena's pad (y 0, attribute 0, x -300..600, z -250..250), the sword's row beside it at world
  (-427, -65, 17).
- The climbing room's own entrance row (`exitData` 260 European, Japanese, Korean and Asian, 256
  North American; stored (3080, 2606, -1899), rot 0) puts the boy at (-3080, -2606, 1899), 72.7
  above a floor of collision only, a 293 x 2327 walkway at y -2533 that no model draws (st08a_p2's
  nearest vertex is 163 away at y -3093, p3's 463, p4's 225 at y 46): he stood in the sky, and the
  entry walk or a step took him off it, 2533 above the room's floor. The origin is on the room's
  floor (y 0, attribute 0, x -6922..3621, z -1662..8549; the collision spans y -5005..0). The
  August and June 2001 discs have neither room. The floors under every candidate point on the
  five discs with the rooms, the same on each (European, North American, Japanese, Korean and
  Asian): `build-host/tmp/v0104/evidence/rooms/rooms-every-disc.txt` (read from the ISOs' packs:
  the arena's `object/sdf/st13b/collis/st13b.cl`, 129 floors; the climbing room's
  `object/sdf/8test/collis/st08a.cl`, 270 floors; the North American disc's model records are
  268 bytes, as the August 2001 disc's).
- What the climbing room is (measured on the European disc by
  `build-host/tmp/v0104/tools/rooms_compare.py`, output `evidence/rooms/rooms-compare.txt`): an
  early cut of the trolley room (stage 8, `st08a (TRUCK)`, pack `STGST08D.DF`). Every file of it is
  named `st08a` under its own `8test/` folder, it uses the trolley room's camera set (`camSetId` 67,
  `camdata/troko.gcm`), and its one entrance row (exit 260) is the point of the trolley room's main
  entrance (exits 2 and 61). It is not the shipped room: its collision has 270 floors against the
  shipped room's 436 (plus 237 in `st08a_fr.cl`), of which 37 are the same polygons (10 more match
  `st08a_fr.cl`); its box spans y -5005..0 where the shipped room's spans -4955..6200 (it goes on
  below the test room's floor) and z -1662..8549 against -4362..9549; its models are other builds
  (`st08a_p1` empty against 26 parts and 13,964 vertices; `p2` 1 part and 6,821 vertices against
  15 and 7,238; `p3` 8 and 7,796 against 17 and 5,836; `p4` 1 and 2,349 against 17 and 6,568) with
  their own texture set (`8test/texture/`); its layout has 32 rows against 78, with no shadow
  generators (kind 4), no kind 12, 19, 22, 26 or 28 rows (the shipped room's doors, switches and
  fittings), one save couch (kind 16) against three, and fifteen rows of kind 266, which no table of
  the shipped game names, laid along the walkways. The disc carries no date or version for it.
- The climbing room's floors and walls: its collision, `object/sdf/8test/collis/st08a.cl`, belongs
  to model 1140 (European; 1105 North American, 1136 Japanese, Korean and Asian, 1100 August
  2001), whose model file `object/sdf/8test/model/st08a_p1.p2o` in `STG8TEST.DF` is an empty
  48-byte stub with no parts; the room's layout row 3716 (European) makes a stage object of it, the
  collision's carrier. A display object of a model with no parts gets `nodeNum` 0, so its node
  matrices are a 0-byte block, whose address is the allocator's next block header
  (`ico2/fumi/ios/memory.c`). `_Clip` (`ico2/fumi/src/fieldCollision.c`) tests each object's
  collision through node 0's matrix whatever its node count, so the room's every floor and wall was
  tested in a frame made of that header's text and no probe met one: from the origin the boy fell
  through the world (the phone's log: `no floor below` at every tick line, his root from y 788 to
  90003 in 10 s, no camera zone with a floor under its centre). The code and the stub are the same
  on every disc that has the pack, so on the original the room has no floor either. The port gives
  such a carrier the one matrix a one-part model's node 0 holds, the layout's
  (`initPolygonState`, `ico2/common/src/DObj.c`), and keeps its `nodeNum` 0, so only the collision
  passes, which read node 0 whatever the count, see it. DATA.DF holds exactly one model with no
  parts on each disc that has `STG8TEST.DF` and none on the June 2001 demo (ctest
  `collision_carrier_disc` and its per-disc rows read every model member of every pack), so no other
  object changes ([DIVERGENCES.md](DIVERGENCES.md) GL-18). The diagnosis with its file and line
  evidence: `build-host/tmp/v0104/evidence/rooms/climb-no-collision.md`. The line, at each start of
  the room: `dobj: model 1140 (layout row 3716) has no parts but carries the collision
  object/sdf/8test/collis/st08a.cl: node 0's matrix is the layout's, at (0, 0, 0)`. With the floor
  the camera-zone move below runs as written: `unused: climbing: the game's no-entrance point (0, 0,
  0) is outside every camera zone (...; its root 0 above its floor there): the boy moved to zone
  20's floor at (1111, -1396, 81) (72 above the floor y -1324, his skeleton's root height; the probe
  from there finds the floor -1324, 72 below)` (72 is his skeleton's root height), then tick lines
  with `the floor -1324 under him, 72 below`, his y steady and fall height 0.
- The camera zones (`startInZone`, once per room as soon as the boy is up, within the log's 300
  ticks): the camera follows the boy through the stage's camera zones, the groups of the
  camera set `CameraSetCameraSet` (`ico2/omori/src/camera-ico2.c`) loads from the stage row's
  `camSetId` into `TopCameraSetDataOfCurrentStage` (a `CamSetFile` head, then `count` `CamGroup`
  records of 76 bytes, `ico2/omori/include/camera-editor.h`). A group's centre and half-size are
  stored negated as layout rows are: the game tests the stored box against the root times -1
  (`findCameraGroupContaining`), so the world box is `-centre` +- half. When no zone holds him the
  game falls back to the nearest zone by box distance (`ico2camera_GetGroupNearest`,
  `camera-ico2.c`), and that zone's pins aim at its own space. The arena (PAL stage 88,
  camera set 6, `camdata/boss.gcm`, 15 groups): the origin lies inside its group 0 (world centre
  (-375, 50, 0), half (1645, 170, 1200)), with the pad's floor y 0 inside the box, so its start is
  right and stays. The climbing room (PAL stage 91, camera set 67, `camdata/troko.gcm`, 51 groups):
  every zone lies 1355 to 4980 above its big floor (world y 0), so from the origin no zone held him
  and the nearest zone's pins aimed 1,355 or more above him: the character was not in view. The rule: a root inside a zone stays; otherwise the
  boy, and the girl when `girlGObj` is up, goes with `SetDirectRootPosition` (as `E3_StageStartBoy`
  moves him, `ico2/fumi/src/boyact.c`) to the lowest zone whose centre has a floor inside its box:
  the game's floor probe (`GetLowerPlaneCollision`) cast down from the box's top at the centre's x
  and z must hit a floor no lower than the box's bottom. The lowest is the zone whose box bottom is
  nearest the big floor; zones with the same bottom go to the one with the larger floor plan (half x
  times half z), then the set's order. He lands at the zone centre's x and z, his root at that
  floor's y minus his skeleton's root height: the game's own rule for a direct move, `act.c`'s
  debug move (`ACTDebugMove`, `ico2/fumi/src/act.c`: `pos[1] = floor - h` with `h` node 0's
  `pos[1]` of `GOBJ_SUB(self)->skel`, 0 without a skeleton; the skeleton exists from the actor's
  creation). The girl goes to the same x and z at her own root height. The height a probe measures
  where he stands is not used: the room's first poll can come before his first motion frame fits
  him, so his root lies on the origin's floor and the probe measures 0; a root put at the ramp's hit
  point lies, in the game's arithmetic, 0.000122 under the ramp's plane, every probe from it finds
  the big floor 1,324 below, and the landing probe drops him there, a fatal fall
  (`build-host/tmp/v0104/evidence/rooms/`, the climbing room's diagnosis, mode 1). After the move one probe
  from the moved root says what is under him; a miss, or a floor more than twice his root height
  below, is logged plainly and he is left there (no second move). In the climbing room that is group 20 (world centre (1111, -1355, 81), half
  (1216, 635, 529), box y -1990..-720, floor y -1324 attribute 0): a ramp of the climbing
  structure 1,324 above the room's floor, sloped about 24 degrees, 150 to 225 across
  (`build-host/tmp/v0104/evidence/review2/ledge.txt`, `ledge2.txt`). The room keeps its entrance
  row's `exit_no`, so `E3_StageStartBoy` would play the 50-frame walk in from the entrance, which
  could take him off the ramp into a fatal fall; the port skips it the game's own way
  (`ico_host_skip_entry_walk`, `ico2/common/src/debug.c`, called in `beginRoom` after the way in,
  for both rooms): the flag the game's checkpoint loads set (`scpSekizouCheckPoint`, flag 381
  European, 348 North American, 347 August 2001, 332 June 2001), which `E3_StageStartBoy` clears
  and returns on before the walk, so he stands where the room puts him. Its thin side zone 47 (centre (1847, -1355, -534), half (475, 635, 85)) has the same
  box and a floor at y -1221 (attribute 0x4), 103 lower; the floor plan decides for group 20. The
  zones and the floors under their centres, on the European disc:
  `build-host/tmp/v0104/evidence/rooms/camera-zones.txt` (from the two camera set files and the two
  collision files, the floors cast as `build-host/tmp/v0104/tools/rooms.py` casts them). Lines:
  `unused: <room>: the start is inside camera zone G of N (centre (x, y, z))`; `unused: <room>: the
  game's no-entrance point (x, y, z) is outside every camera zone (the lowest zone's centre Y above
  it, or below it when a zone lies below the start; its root M above its floor there | no floor
  under its root): the boy moved to zone G's floor at (x, y, z) (h above the floor y F, his
  skeleton's root height; the probe from there finds the floor F2, D below | finds no floor)`, then
  `; the girl with him, h above the floor` (her own root height) when she moved; when the probe
  from the moved root finds no floor or one more than 2h below, `unused: <room>: the floor under
  the moved root is not the zone's: the probe from (x, y, z) finds the floor F2, D below, more than
  twice his root height | finds no floor (the zone's floor y F, his root height h); he is left
  there`; `unused: <room>: no camera zone holds a floor under its centre: the boy stays at (x, y,
  z)`; `unused: <room>: no camera set loaded (N groups): the boy stays at (x, y, z)`; `unused:
  <room>: the camera-zone check never ran (no boy in the first 300 ticks)`. The boy's tick lines
  below show where he stands after the move.
- The start's line: `unused: <room>: the boy at the room's start (x, y, z), the game's
  no-entrance point (exitData[0])`, then `: the room has no entrance row` (the arena) or `; the
  game's own entrance row N (x, y, z) not used: it leads to a walkway with no model` (the
  climbing room), then `; placed: the boy (record R), the girl (record R), the sword (record
  R)` as placed (with `the boy's weapon (record R, kind K)` and `the boy's item (record R, kind
  K)` after the boy when the title's boy held them). For the first 300 Main ticks after the room's stage is up, a line as it comes
  up and every 30 ticks after, and at a game over in a room (`ico_unused_game_over`, through the
  engine's `game_over`): `unused: <room> tick T: the boy at (x, y, z), ground height G (the floor
  F under him, D below | no floor below), fall height H; the camera (mode M) in zone Z, its eye
  (x, y, z) looking at (x, y, z) | no camera computed yet; his draw mask 0xMMMMMMMM, display N`
  (`tick T` reads `game over` there), from the boy's root (`GetRootPosition`),
  `ctrl.groundHeight`, `ctrl.fallHeight` and the game's own floor probe (`GetLowerPlaneCollision`,
  as `act.c` calls it); the camera is the monitor camera's work (`ico_host_camera_view`,
  `ico2/omori/src/camera-ico2.c`, read only: the zone the zone search last found, -1 none since
  the stage's init, as a frame with no zone keeps the last; the eye and look-at in the roots'
  space, the work's negation; `CameraGetMode`; `CameraCalclated_f`), and he is drawn while the
  draw mask (`GObj` `drawMask`, ANDed with a camera's mask: all ones while shown) is set and
  `disp` (the display object's, nonzero while the stage animation draws it) is nonzero; `no boy (the stage has none up)`
  while the stage has no boy, `no boy (a stage change under way)` while `systemStatus[6]` is set
  (the next stage's collision list may still name the old stage's objects).
- Leaving: the rooms have no exit. Pause > End Game goes back to the title by the game's own path.
  A game over goes there with either choice: `la_game_over_continue` and its two other arms
  (`ico2/common/src/layout_action.c`) route Continue as Quit while `ico_unused_active()`
  (`gflagInit`, `nextStage = 1`, on the European arm `optionScreenMode = 0` as its Quit), and
  `ico_unused_game_over` marks the reason. As the title's stage is entered
  (`ico_unused_stage_enter`, `StageManager.c` (`start_stage_Load_thread`)) the state is put back
  and the flag cleared, and Options opens again on the page once the title's menu is up
  ([UI.md](UI.md#extras--unused-content)).
- A start that never reaches its stage (the credits' bounds, 1500 Main ticks with no stage change,
  4500 in all): on the title the state is put back at once, elsewhere the title is requested and the
  state put back as it is entered.
- While a room runs the achievements are suspended (`ico_gs_achievements_suspended`) and nothing is
  saved by the port. Saving and loading a state are both refused: the Save states page refuses
  Save and Load before its host (`port/ui/settings_states.c` (`statesPerform`): the popup
  `UI_STR_STATES_REFUSED_UNUSED`, "Not in a test room", and `unused: save state refused in a test
  room (slot N)` or `unused: save state load refused in a test room (slot N)`), and the state
  conditions refuse both too (`port/game/state_rules.c`, `IcoStateConds.unused`: `ICO_STATE_PHOTO`,
  "a test room"), so F5 and F9 cannot either. A load is refused because the room's flag is cleared
  only at the title: a state of normal play loaded in a room would keep it set (the achievements
  suspended, a game over's Continue sent to the title, Save refused) until the next title.
- The attract movie's start (`beginMovie`, `port/game/unused_live.c`), from the title only: the
  title's attract path, started from the menu. `mpegPlayReturnStage` is the title, the title theme
  fades as in the credits, the skip word `stage_after_skipping_demo` is cleared (a skip is told by
  it at the return), and `stgmgrForceSwitchWithFade(ico_unused_stage(movie), 256, 4)` is asked, the
  fades of `ico2/script/src/op.c` (mode 2). The stage manager and Main play it
  ([FMV.md](FMV.md#the-european-discs-second-attract-film)); Start skips it; the title returns by
  itself and the menu reopens on the page. No game state is touched, so none is kept; the flag
  (`ico_unused_active`) holds the achievements and save states off meanwhile. A start that never
  reaches the stage fails as the rooms' does (1500 / 4500 Main ticks). Log lines: `unused: attract
  movie (576 lines) started from the title`, `unused: the movie's stage N is up ...`, `unused: back
  at the title after X s (movie ended | skipped | fail-start)`, `unused: failed: the movie's stage
  N was not reached ...`. The film's length is read in wall seconds, as Main runs no tick while a
  film plays.
- The art row's start (`beginArt`, `port/game/unused_live.c`), from the title only: Extras >
  Models' list opened on the viewer's first art row on this disc (`ico_mv_models_enter_at`,
  `mv_member_first_row`); Back there comes to this page's art row. The viewer loads the art on its
  host stage ([The model viewer and Characters](#the-model-viewer-and-characters)). The row needs a
  member row the viewer offers as well as the member (`unused: no early show title art row on
  <disc>: the model viewer offers none of its models`). Lines: `unused: early show title art: the
  model viewer's list (layout L) on its row R`, `model_viewer: the list opened at "<name>" (row N of
  M offered)`, or `unused: failed: the model viewer's list did not open for early show title art`.
- Log lines: `unused: enter <row>`, `unused: test room <row> (stage N on this disc) started from the
  title at Main tick T`, the start's line, the camera-zone line and the boy's lines (above), `unused: stage N at Main tick T`, `unused: the test
  room's stage N is up ...`, `unused: game over in the test room <row>: both choices go to the
  title`, `unused: back at the title after X s (End Game | game over | fail-start): ...`, `unused:
  Options open again on Unused content`, the failures (`unused: failed: ...`), and Settings'
  `extras: no Unused content page on <disc id>`.

ctests `unused_content` and its six other-disc runs (`port/game/test/unused_content_test.c`),
`model_viewer` and `model_viewer_rows_<slug>` (the art's rows per disc), `charfile_image` (the hook's
twins), `settings_extras`, `settings_discs` and its runs, `keep_flags` (the game-over arms compile
with the flag's stub).

## The title logo

The logo is nine stage animations of the title's stage (I, C and O, their glows and shadows). While
a Settings or Extras page opened from the title is up, or the model list the Extras page opens, it
is not drawn; its animation runs on, so back on the title it shows settled. Once hidden it stays
hidden until the title's menu (layout 12 or 13) or the file select is back or the stage changes, so Extras > Credits
and a model's stage load do not show it while the title fades. The pause menu's Settings never
hides anything (`port/game/title_logo.h`, header comment).

`Main` calls `ico_title_logo_update` once a tick after the layouts ran (`ico2/common/src/main.c`),
and `ico2/seki/src/RegistPacket.c` (`reg_DispObj`) skips an object whose model `ico_title_logo_skip`
names. The same function widens the title's full-frame model `title_back`, built for the 4:3
screen, to the [native frame](ARCHITECTURE.md#terms) by scaling its node matrices' x row by
`gsFrame.w / 512` when `ico_title_fullframe_model` names it (`port/game/title_logo.h`
(`ico_title_fullframe_model`)).

**The icon pass.** The application icon is the logo's I, captured from the title the first time its
menu shows after a start that finds no icon for the disc, or one made by another version
([APP_ICON.md](APP_ICON.md#the-capture-on-the-title)). For a few ticks `ico_title_logo_skip` skips
every model but `I` (the backdrop `title_back`, the castle, C, O, the glows and the shadows), the
renderer leaves out the menus and clears to black and then white, and the title's last picture is
held over the output ([APP_ICON.md](APP_ICON.md#the-capture-on-the-title)): the player sees the title
still for about half a second, nothing black. No game code changes:
the name rule in `port/game/title_logo.c` is the whole game-side hook, and `ico_title_logo_update`
steps the capture (`port/game/title_icon.h`).

## Community translations

A community translation is a player's translation of the game into a language the disc does not
have, built into the port as text ([TRANSLATING.md](../TRANSLATING.md) for the contributor and the
maintainer). Russian, by bob-ttz, is the first (`UI_LANG_RU_BOBTTZ`), Russian by cobra838 the
second (`UI_LANG_RU_COBRA838`, `[game] text_language = "ru-cobra838"`, the same path with its own
two tables), Brazilian Portuguese by Igor Chagas (ichagas7) the third (`UI_LANG_PT_BR`,
`[game] text_language = "pt-br"`, likewise); the Language row offers them after the European disc's
five (entries 5, 6 and 7 of eight), and choosing one sets the game's language to English
(`NonLinearCameraMove` 2: the English subtitle files, menu sheets and stage animations, and the
English language byte in the save's product file, so the saves are the English game's) and writes
`[game] text_language = "ru-bobttz"` (`port/ui/settings.c` (`setLanguageEntry`, `languageEntry`)).
The five disc languages keep their order, their values and their path. The translation is in force
on a disc whose game has a language of its own (the European disc) while that language is English
(`port/ui/ui_lang.h` (`ui_settings_text_language`)); a `[game] language` other than English, or any
other disc, leaves the disc's own pictures, whatever the key says. In force:

- the port's text follows it while the Menu language is Auto (`ui_settings_resolve_language`);
- the game's own menu words (the European disc's menu text table, [UI.md](UI.md#the-games-own-menu-words))
  are the Menu language's `UI_STR_MT_*` strings (the translation's on Auto), laid out with the
  English sheet's geometry, so New Game, Continue and the save tiles' words agree with the port's
  rows beside them (PR 180, cobra838's fix; `port/ui/settings.c` (`ui_settings_apply_language`),
  `port/ui/strings.h` (`ui_menu_words_language`), `port/ui/menu_font.c` (an item strip's
  `textLang` apart from its sheet language `lang`, the draw key from the drawn words)); an id a
  table leaves untranslated is the English sheet's word;
- the subtitles: `jimakuDisp` (`ico2/fumi/src/jimaku.c`, the arm the European, Japanese and Korean
  discs share) asks `ui_subtitle_words` for the shown record's words before it draws the record's
  two strips (texProperty 434 and 435), under the same conditions (the record shown, its picture
  loaded, subtitles on); with words it opens `display_texture`'s packet (list 11, ALPHA
  `(1, 7, 0)`, register 74, no Z) and calls `ui_subtitle_draw` with the strips' colour (the fade's
  `~reductionCol`) instead of the two `display_texture` calls (`display_words`). The words are
  centred in the band the strips cover (grid x 64 to 576, field lines 144 to 192), one or two
  lines 44 y units apart about its middle, in the menus' look at `UI_SUBTITLE_EM` (the menu rows'
  em), set smaller as a whole when the widest line passes 496 x units (`port/ui/subtitle_text.h`).
  The record's picture is still read and made a texture, the semaphores and the display time are
  the game's, photo mode shows neither. A record the table leaves out keeps its picture, and so
  do Yorda's records on a first play: the disc's first-play files show her own script there and
  the cleared game's files English (`IcoSubtitle.secondOnly`; records 9, 29, 31, 48, 49, 86, 91,
  112). The North American disc, the 2001 previews and the Asian disc have their own `jimakuDisp`
  arms, which do not ask.

What stays in English under a translation: the pictures the menu text table leaves out (the
"Sony Computer Entertainment Europe Presents" plate and the copyright line, the logos, the
LANGUAGE and TV headers, the 50/60 Hz notes: [UI.md](UI.md#the-games-own-menu-words)), the language
versions of the title and ending logo animations (`ico2/script/src/e3.c` (`actE3TitleFrameChk`),
`ico2/script/src/end.c` (`actEndLogoChk`) pick by `NonLinearCameraMove`), and the staff roll, whose
bitmap font has ASCII only ([The credits](#the-credits)).

The log names the change of every subtitle record under a translation, the first eight and then
every hundredth (`ui: subtitles: record N (first|second play) drawn as text|keeps the disc's
picture (ru-bobttz, 42 entries)`), the Language row's step (`settings: Language ru-bobttz (game
language 2, [game] text_language "ru-bobttz")`) and the key as read (`options: [game]
text_language = "ru-bobttz": a community translation`, or the word it does not know).

Russian (cobra838) came as C tables in pull request 180 (`port/ui/strings_ru.c`,
`port/ui/subtitles_rus.c` there) rather than the two CSV files; they were converted once into
`strings_ru_cobra838.c` and `subtitles_ru_cobra838.c` by the steps and checks of
[TRANSLATING.md](../TRANSLATING.md#adding-a-translation), its log lines name `ru-cobra838`.

Brazilian Portuguese came as the two CSV files of issue 157, saved from a spreadsheet that holds
each template row in its first column (split in two where the English has a `;`) and the
translation from its third; they were converted once into `strings_pt_br.c` and
`subtitles_pt_br.c` by the same steps and checks, its log lines name `pt-br`. cobra838's copy of
the same menu table (PR 180) then corrected 7 of its rows and filled 55 of its NULL ones (marked
`[cobra838]` in the table; the subtitles are Igor Chagas's alone). The language's
letters (á â ã à ç é ê í ó ô õ ú and their capitals, …) were already in Arimo's subset (Latin-1),
so the font did not change.

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

## Save states

Four slots per disc hold a snapshot of the whole running game (`port/platform/state.h`;
[PLATFORM.md](PLATFORM.md#saving-and-loading) has the file, the sequences and the log lines).
What a player sees:

- **Saving** is done from the pause menu (its Save states row, or F5 for slot 1 in the desktop
  build) and only while nothing else is under way: no stage change, no memory card save or load,
  no disc read in flight, no movie, not photo mode, the Extras credits or the music gallery. F5
  away from the pause menu says to pause first. A popup says "State saved to slot N" once the file
  is written; the slot shows the picture of the frame shown after the save (the pause menu in it),
  the area and the play time.
- **Loading** works from the pause menu and from the title (Options > Extras > Save states), or F9
  for slot 1, under the same conditions but the pause. The game comes back exactly where it was,
  on the pause menu. A state saved by another version of the program, from another disc, on
  another platform, or with a gameplay setting that changes the simulation set otherwise
  (`port/platform/sim_keys.h`: the popup names the setting) is refused, and so is a damaged file;
  the game goes on. A state whose memory fails the checks after it is put back is undone the same
  way.
- **What a load keeps**: the achievements, their counters, the session's and the run's counts
  (fresh, captures, game overs) stay as they were (DIVERGENCES.md GL-17); the options are the
  player's, not the state's.
- **Off**: a build where the game's data cannot be found as a whole (`state: the game's data
  ranges are not bracketed ...`), a sanitizer build, or no folder to write to: the row and the keys
  say "Save states are off on this build".

The files are `<pref>/states/<disc>/slot<n>.ics` and `slot<n>.png` (on Android, the app's files
folder). The page itself is [UI.md](UI.md)'s.

## Android driver packages

The Android build loads a community Vulkan driver for Adreno GPUs through libadrenotools from a zip
the player adds on the Settings page: a flat package of `meta.json`, the driver library it names
(`libraryName`, matching `^[A-Za-z0-9._-]+\.so$`) and that library's dependencies, at most 64
files of at most 64 MB each. An install is checked whole, unpacked into `<folder>.tmp/` and moved
into place; the folder is the package's name and driver version made safe. A trial marker names the
driver a start is trying, so a start that died inside the driver is noticed at the next one
(`port/platform/gpu_driver.h` (`ico_gpu_driver_install_zip`, `ico_gpu_driver_trial_crashed`)). The
package handling is pure C, built and tested on every platform; the loading itself and the fallback
are [ANDROID_DRIVERS.md](ANDROID_DRIVERS.md)'s.

## Tests

Each is a ctest: `settings_discs` and its six other-disc runs (the rows each disc shows,
[Options on each disc](#options-on-each-disc)), `options` (`port/game/test/options_test.c`: the defaults, the setters,
`eBrainGetTarget` with Yorda safe), `door_grace`, `yorda_trip` and its six other-disc runs, `yorda_trip_disc`, `achievements`, `photo`, `appearance` and
`appearance_disc`, `credits`, `unused_content` and its six other-disc runs, `title_logo`, `state_host`, `jimaku_text` (the subtitle hook) and `model_viewer_headless` (`port/game/CMakeLists.txt`);
`gallery`, `gallery_sweep`, `credits_headless`, `model_viewer`, `model_viewer_cam`,
`model_viewer_effects`, `model_viewer_effects_elf`, `subtitles_ru` and its three other-disc runs (`port/ui/CMakeLists.txt`); `steam_shortcut` and `gpu_driver` (`port/platform/CMakeLists.txt`).
[TESTING.md](TESTING.md) has how to run them.

## Video options

The picture options (`[video]`, the [presets](ARCHITECTURE.md#terms), aspect, resolution,
anti-aliasing, the CRT filter, frame rate) are `port/game/video_options.h`'s and are described in
[DISPLAY.md](DISPLAY.md).

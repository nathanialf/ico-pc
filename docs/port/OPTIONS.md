# Gameplay options

The port's gameplay options live in one module, `port/game/options.c` and
`options.h`. Each is read from `config.toml` `[gameplay]` on the first call
that asks (`ico_config_get_bool`) and can be changed at run time with
`ico_opt_set_*`, which is what the Settings menu uses. `ico_opt_reload()`
forgets the run-time values. Every default is the original game's
behaviour, and the game code reads an option only through its getter.

| key | default | getter / setter | read by |
|---|---|---|---|
| `[gameplay] stick_fix` | `false` | `ico_opt_stick_fix` / `ico_opt_set_stick_fix` | `ico_input_frame` (`port/input/pad_host.c`); `ico_input_set_stick_fix` and `ico_input_stick_fix_enabled` forward to the module, and `ico_input_apply_toml` sets it from the file it is given. Described in INPUT.md |
| `[gameplay] yorda_safe` | `false` | `ico_opt_yorda_safe` / `ico_opt_set_yorda_safe` | the hook sites below |
| `[gameplay] mirror` | `false` | `ico_opt_mirror` / `ico_opt_set_mirror` (also `ico_opt_set_mirror_listener`, `ico_opt_mirror_reset`, `ico_mirror_slot_*`) | the run's value, chosen at New Game (below): `ico_input_frame` negates stick X through `ico_input_vpad_to_frame`; `port/audio/audio_host.c` swaps left and right; the renderer flips through the listener (`rd_SetMirror`) |
| `[gameplay] developer_mode` | `false` | `ico_opt_developer_mode` / `ico_opt_set_developer_mode` | `Main` (`common/src/main.c`) calls `debug_Menu`; `debug_PrintfDummy` draws; `debugSceOpen` uses `host0:` (`<pref>/dev/`); `debug_VariableInit` may load the saved option table; the trace's first line records it (`port/platform/trace_host.c`); achievements are suspended while it is on and for the rest of the run (docs/port/DEVELOPER_MODE.md, docs/port/ACHIEVEMENTS.md) |
| `[dev] debug_option` (int) | `0` | `ico_opt_debug_option` (read on each call, no setter) | `debug_VariableInit` in developer mode: a value other than 0 loads `<pref>/dev/thisIsYourDebugOption` |

## Mirror mode

Mirror mode flips the whole game left to right: the picture (the
renderer's present, with the UI flipped back so it reads normally;
docs/port/RENDER_API.md, the mirror section), the stick's X axis
(`ico_input_vpad_to_frame`) and the stereo channels (`ico_audio_pan_mirror`
on every rendered block in `port/audio/audio_host.c`, so both the device
and a WAV dump hear it; the SPU2 and the sound driver are untouched). The
game's logic is not touched: no state, flag or save byte depends on it.

The value belongs to a run, not to the settings:

- `[gameplay] mirror` is only the value before a run starts (a testing key;
  the Settings menu does not write it) and what the title goes back to.
  `la_title_continue_or_new` and `la_title_new_game_only` in
  `common/src/layout_action.c` call `ico_opt_mirror_reset()` when they
  start, because the title belongs to no run.
- At New Game the port's "Mirror mode" screen after the vibration screen
  (docs/port/SETTINGS.md, "Mirror mode") sets it with `ico_opt_set_mirror`;
  entering the screen resets the value first.
- A save to slot N records the run's value in the port config (`[mirror]
  slot_N` with the save's checksum), and a load from slot N sets the run's
  value from it, Off when there is no matching entry (docs/port/SAVES.md,
  "Mirror mode"). The value is kept outside the save file so that the
  memory card bytes stay exactly the PS2's.

`ico_opt_set_mirror` calls the one listener registered with
`ico_opt_set_mirror_listener`. The Settings module registers `rd_SetMirror`
in the window build when it installs, and the listener hears the current
value at once. `ico_audio_set_mirror` / `ico_audio_mirror` and
`ico_input_set_mirror` / `ico_input_mirror` forward to the module.

## yorda_safe: the shadows never take Yorda

The option is for a less stressful game: the shadow creatures never target
or carry off Yorda in free play. The hook is `ico_opt_yorda_safe()`,
declared `extern` in each game file that calls it. The sites:

| site | what it does when on |
|---|---|
| `omori/src/ebrain.c` `eBrainGetTarget`, message 2 | "chase the girl" becomes status 1 (the boy) |
| same, message 6 | "go to the girl" (status 3) is ignored |
| same, the status 0 order loop | the girl is skipped in the boy/girl order; the boy is chosen as before when seen (`eBrainCanSeeTarget` sets status 1) |
| same, case 1 | the switch to the girl after 181 chase frames is skipped |
| `common/src/backStage.c` `backStageProcessMain` | returns at once after clearing `gamesysAnotherStageTsuresari`: the off-screen kidnap countdown (`kidnapState` 1), the carry countdown (2) and `RequestStageChangeKidnapEnd` do not run |
| `fumi/src/enemy_act.c` `enemyKidnapCheckGirl`, `enemyPickupCheckGirl` | return 0 ("no girl in reach"), so an enemy that happens to be near her does not grab her. These checks call the static `actEnemyForceSwitchToCarry` without going through `eBrainGetTarget`, so they need their own hook |

Left alone on purpose:

- `ACTEnemyForceSwitchToCarry` and the static `actEnemyForceSwitchToCarry`
  in `enemy_act.c`, the grab itself, because scripts and saved-state
  restore reach it (below).
- `backStageProcessOutStage` still sets `kidnapState`; with the option on
  nothing consumes it.
- `backStageProcessInStage` moves the girl and the enemies to a random way
  point by how long the boy was away (`gamesysStageExitTime`). That is not
  a capture, so the option leaves it.
- `eBrainGetTarget` status 4 (carry to the nest, messages 9 and 7) and 5
  (follow the holder), which are reachable only after a grab.

### Story captures

Searching the stage scripts (`ico2/script/src/*.c`) for `Kidnap`,
`girlHolder`, `carried`, `ForceSwitchToCarry` and `GirlCarry`, and the
enemy mail and `scp*Enemy*` calls, finds one script call that makes an
enemy take the girl: `ACTEnemyForceSwitchToCarry(scpSearchGobj(150))` in
`actSt13cCageFallChk` (`ico2/script/src/st13c.c`), the boss cutscene with
the `bossGenerator` enemy 150. It goes through the
`ACTEnemyForceSwitchToCarry` wrapper, which no hook gates, and then
`eBrainSendMes(self, 9)` and `(self, 7)` put the slot in status 4, which the
option keeps. The option therefore does not alter that scene, and
`options_test` checks that messages 9 and 7 still reach status 4 with the
option on. The Settings note for the option says that a few scripted scenes
still show the capture.

The other occurrences in `st13c.c` (`girlCarry_mes`, `actSt13cGirlCarry*`)
are ACT mail tables for the boy carrying her and gate on game flags 27 and
30; they do not call the enemy brain. The `scpWakeupEnemyAll`,
`scpSleepEnemyOne/All` and `scpKillEnemy*` calls in the other stage scripts
wake, sleep or kill enemies and wait on `scpCheckExistAliveEnemy`; they do
not command a grab, so the enemies' own AI picks the target, and with the
option on that is Ico. The first shadow encounter (`st03t.c`,
`scpSleepEnemyOne(3757)`, stage animation 81, `scpWakeupEnemyOne(3757)`)
is such a script: `actSt03tWayOnChk` and `actSt03tWayOffChk` wait until
`scpCheckExistAliveEnemy() == 0` and the girl stands on a floor trigger, so
with the option on the encounter still ends when the enemy is killed.

The free-play kidnap AI (statuses 2 and 3) and the off-screen timers cannot
make the story fail with the option on, since no script waits for them.
The flags a capture sets (`gflagChk(390)`, `391`, `394`, `0x189`) are read
in `backStage.c` and `enemy_act.c`. The option's behaviour in play is
listed in docs/TODO.md.

## Tests

`options_test` (`port/game/test/options_test.c`; ctest `options`, CPU)
checks the defaults (false, and `debug_option` 0), the setters and
`ico_opt_reload`, the values read from a `config.toml` (including
`developer_mode` and the integer `debug_option`, where a non-number reads
0), the mirror slot entries, and `ebrain.c`, compiled into the test and
driven with a synthetic slot set: both characters in view with the girl
nearer, only the boy, only the girl, the 181-frame switch, messages 1, 2
and 6, and a 9-then-7 carry. With the option off statuses 2 and 3 are
reached; with it on status 2 is never reached, status 1 goes to the boy,
and a carry is still status 4.

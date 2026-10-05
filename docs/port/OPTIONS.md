# Gameplay options (Phase 6A)

The port's gameplay options live in one module, `port/game/options.c/.h`.
Each is read once from `config.toml` `[gameplay]` (`ico_config_get_bool`, on
the first call that asks) and can be set at run time (`ico_opt_set_*`; the
Settings menu, package 6B, uses these). `ico_opt_reload()` forgets the
run-time values. Every default is the original game's behaviour; the game
reads an option only under `#ifdef ICO_HOST` (the EE build is unchanged).

| key | default | getter / setter | read by |
|---|---|---|---|
| `[gameplay] stick_fix` | `false` | `ico_opt_stick_fix` / `ico_opt_set_stick_fix` | `ico_input_frame` (`port/input/pad_host.c`); `ico_input_set_stick_fix` and `ico_input_stick_fix_enabled` forward to the module; `ico_input_apply_toml` still sets it from the file it was given |
| `[gameplay] yorda_safe` | `false` | `ico_opt_yorda_safe` / `ico_opt_set_yorda_safe` | the hook sites below |
| `[gameplay] mirror` | `false` | `ico_opt_mirror` / `ico_opt_set_mirror` | `ico_input_frame` (negates stick X, via `ico_input_vpad_to_frame`); `ico_audio_mirror()` for the pan swap |
| `[gameplay] developer_mode` | `false` | `ico_opt_developer_mode` / `ico_opt_set_developer_mode` | renderer wave 6 (R6a), docs/port/DEVELOPER_MODE.md: `Main` (`common/src/main.c`) calls `debug_Menu`; `debug.c`'s `debug_PrintfDummy` draws, `debugSceOpen` uses `host0:` (`<pref>/dev/`), `debug_VariableInit` may load the saved option table; the trace's first line records it (`port/platform/trace_host.c`); the achievements package suspends achievements while it is on |
| `[dev] debug_option` (int) | `0` | `ico_opt_debug_option` (read each call, no setter) | `debug_VariableInit` in developer mode: not 0 loads `<pref>/dev/thisIsYourDebugOption` |

`stick_fix` is documented in INPUT.md. Mirror is plumbing only: the renderer
flip, the UI pre-flip and the choice at new game belong to the mirror package.
`ico_audio_set_mirror` / `ico_audio_mirror` (`port/audio/audio_host.c`) are the
audio stub: they set and read the module's value; nothing in the mixer swaps
the pan yet.

## yorda_safe: the shadows never take Yorda

The hook is `ico_opt_yorda_safe()`, declared `extern` in each file under
`#ifdef ICO_HOST`. Sites:

| site | what it does when on |
|---|---|
| `omori/src/ebrain.c` `eBrainGetTarget`, message 2 (`:312`) | "chase the girl" becomes status 1 (the boy) |
| same, message 6 (`:327`) | "go to the girl" (status 3) is ignored |
| same, status 0 order loop (`:412`) | the girl is skipped in the boy/girl order; the boy is chosen as before when seen (`eBrainCanSeeTarget` sets status 1) |
| same, case 1 (`:433`) | the switch to the girl after 181 chase frames is skipped |
| `common/src/backStage.c` `backStageProcessMain` (`:263`) | returns at once (after clearing `gamesysAnotherStageTsuresari`): the off-screen kidnap countdown (`kidnapState` 1), the carry countdown (2) and `RequestStageChangeKidnapEnd` do not run |
| `fumi/src/enemy_act.c` `enemyKidnapCheckGirl` (`:1235`), `enemyPickupCheckGirl` (`:1463`) | return 0 ("no girl in reach"): the enemy that is near her by chance does not grab her. Needed because the two checks call the static `actEnemyForceSwitchToCarry` without going through `eBrainGetTarget`; they were not in the brief's list |

Not touched, on purpose:

- `ACTEnemyForceSwitchToCarry` and the static `actEnemyForceSwitchToCarry`
  (`enemy_act.c:996`): the grab itself. Scripts and saved-state restore reach
  it (below).
- `backStageProcessOutStage` still sets `kidnapState`; with the option on
  nothing consumes it.
- `backStageProcessInStage` (`:389`): it moves the girl and the enemies to a
  random way point by how long the boy was away (`gamesysStageExitTime`). That
  is not a capture, so the option leaves it.
- `eBrainGetTarget` status 4 (carry to the nest, messages 9 and 7) and 5
  (follow the holder): reachable only after a grab.

### Story captures

`grep -a -n "Kidnap\|girlHolder\|carried\|ForceSwitchToCarry\|GirlCarry" ico2/script/src/*.c`
(and the enemy mail and `scp*Enemy*` calls in the same files) finds one script
call that makes an enemy take the girl:

- `ico2/script/src/st13c.c:631`, `ACTEnemyForceSwitchToCarry(scpSearchGobj(150))`
  in `actSt13cCageFallChk` (stage 13c, the boss cutscene after `stage_*Animation`
  646/647/74-76 and the `bossGenerator` enemy 150). The call is the scripted
  capture; it goes through the `ACTEnemyForceSwitchToCarry` wrapper, which none
  of the option's hooks gate, and then `eBrainSendMes(self, 9)` and `(self, 7)`
  put the slot in status 4, which the option keeps. The option does not alter
  this scene. `options_test` checks that messages 9 and 7 still reach status 4
  with the option on.

The other occurrences in `st13c.c` (`girlCarry_mes`, `actSt13cGirlCarry*`,
`:74-78`, `:997-1550`) are ACT mail tables for the boy carrying her and gate
on game flags 27 and 30; they do not call the enemy brain. The remaining
`scpWakeupEnemyAll`, `scpSleepEnemyOne/All` and `scpKillEnemy*` calls in the
other stage scripts (st03t, st05*, st07a, st08*, ...) wake, sleep or kill
enemies and wait on `scpCheckExistAliveEnemy`; they do not command a grab, so
the enemies' own AI decides what they target, and with the option on that is
Ico.

Stage 3 (`st03t.c:557-575`, the first shadow encounter: `scpSleepEnemyOne(3757)`,
the stage animation 81, `scpWakeupEnemyOne(3757)`) is such a script: it wakes the
enemy and returns to normal play. `actSt03tWayOnChk` / `actSt03tWayOffChk`
(`:581-612`) wait until `scpCheckExistAliveEnemy() == 0` and the girl stands on
a floor trigger. No grab is scripted. With the option on the enemy fights Ico,
so the encounter still ends when it is killed. This is read from the sources,
not tested in play (below).

Where a longer story check is needed: the free-play kidnap AI (statuses 2, 3)
and the off-screen timers cannot make the story fail with the option on, since
no script waits for them; the one sequence that depends on a carry is
`st13c.c:631`. Whether any later room's script waits on a state that only a
free-play capture produces is not established by grep; the flags a capture
sets (`gflagChk(390)`, `391`, `394`, `0x189`) are read in `backStage.c` and
`enemy_act.c`, and the `st13c` flags 27 and 30 above. The Settings text for the
option should say it is for a less stressful game and that a few scripted
scenes still show the capture.

## Tests

`options_test` (`port/game/test/options_test.c`, ctest `options`, CPU): the
defaults are false (and `debug_option` 0); the setters and `ico_opt_reload`;
the config values read from a `config.toml`, `developer_mode` and the int
`debug_option` among them (a non-number reads 0); and `ebrain.c`, compiled into the test, driven with
a synthetic slot set: both in view with the girl nearer, only the boy, only the
girl, the 181-frame switch, messages 1, 2 and 6, and a 9 then 7 carry. Off:
status 2 / 3 are reached; on: status 2 never, status 1 for the boy, a carry
still status 4.

## Runs (2026-10-05)

Headless `linux-x64`, `ticks=3000`, `pad_script` = `port/input/pad-boot.txt`,
`config.toml` beside the exe with `yorda_safe` false then true
(`build-host/6a-run-off`, `build-host/6a-run-on`): both ended at 3000 Main
ticks, 6007 vsyncs, stage 3; the traces are byte-identical (SHA-1
`d3df069a74427382592e3fb0d25d8a3ebf253918`). A temporary stderr counter in
`eBrainGetTarget` (removed afterwards) printed nothing in either run: no
enemy brain ran. The option is therefore untested in play. The boot script
confirms the sign and leaves Ico standing at the start of stage 3, so the first
shadows (the `st03t` encounter, generator label 3757) are not reached. A run
that exercises it needs a pad script that walks Ico to the `st03t.c:557`
trigger (`scpTriggerBall(self, boyGObj, 100)`, then animation 81) and keeps
fighting; its length is not known here.
The two enemy_act.c hooks were added after the runs (they sit behind the same
option and are not reached in the boot) and have unit coverage only through
the shared `ico_opt_yorda_safe` read.

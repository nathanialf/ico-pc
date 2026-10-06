# Achievements

The port has its own achievement set with in-game popups, built on a typed,
read-only game-state interface that an rcheevos client could also drive.
RetroAchievements itself is not wired in: RA's policy for standalone
clients excludes decompilation ports (docs/research/retroachievements.md).
The design follows Dusklight's: event signals raised at a handful of game
sites and consumed on the tick they are seen, plus state polled once per
tick. There is one kind of unlock. Developer mode and `[dev] start_stage`
suspend achievements (below).

| file | what |
| --- | --- |
| `port/include/ico_gamestate.h` | the read-only game-state view, the signal API, the retail-address peek |
| `port/game/gamestate.c` | its implementation; built with `ICO_GS_LIVE` into `ico_pc` (reads the game), without it into the test (a synthetic sampler) |
| `port/game/achievements.c`, `.h` | the table, the per-tick checks, `achievements.toml`, the popup queue |
| `port/game/test/achievements_test.c` | ctest `achievements` (CPU) |
| `port/platform/host_loop.c` | the tick hook: `ico_ach_host_poll(ico_host_main_ticks())` at the end of `ico_host_step` |
| `port/ui/strings*.{h,c}` | the 60 strings (30 titles, 30 descriptions) in the five languages |

## How it runs

- **Once per Main tick.** `ico_host_step` (one simulated vsync) ends with
  `ico_ach_host_poll(ico_host_main_ticks())`, which runs `ico_ach_tick` once
  for each Main tick completed since the last call. That point is after
  every fiber has run for the vsync, on the host context, so no game code
  is interleaved with the reads. `ico_ach_tick` calls `ico_gs_tick` (which
  takes the snapshot, makes the signals queued since the last tick current,
  and derives the polled events and counters), evaluates every achievement
  not yet unlocked, then pumps the popup queue and writes changed counters
  when due.
- **Reads only.** The live sampler copies globals and calls the game's own
  pure accessors (`gflagChk`, `CheckWeaponKind`, `GetCharHeldItem`,
  `ACTGame_FLAG_TETSUNAGI`). It follows `boyGObj` and `girlGObj` only while
  `stageManagerFreeResourceFlag == 0` and `mpegPlay == 0` and the pointers
  and their `act` records are non-null, because `stop_free_resources`
  (`common/src/StageManager.c`) frees the object partitions before zeroing
  both pointers, and the movie path zeroes them. Signals only append to a
  128-entry port-side queue; a full queue drops and counts.
- Because nothing is written to game state, a headless run's trace is the
  same with achievements as without them.

## The game-state view (`ico_gamestate.h`)

| query | game fact read | source |
| --- | --- | --- |
| `ico_gs_stage`, `ico_gs_stage_name`, `ico_gs_stage_entered` | `stage_no`; `stageData[stage_no].name` (the runtime-loaded table); STAGE_ENTER or a change | `common/src/main.c`, `StageManager.c` |
| `ico_gs_flag(n)`, `ico_gs_flag_rose(n)` | `gflagChk(n)` for n < 400 (`gflags[50]`) | `script/src/gflag.c` |
| `ico_gs_game_clear` | `gFlagGameClear` (1 in a game continued from a cleared save; `actEndingSave` in `end.c` sets it to 1 for the ending's save and back to 0) | `script/src/gflag.c` |
| `ico_gs_play_frames`, `ico_gs_play_seconds` | `IosMcPreviewInfo[2]`, counted by `la_playtime_count` while not paused; seconds as the save screen's `playTime` computes them | `layout_action.c` |
| `ico_gs_tick_hz` | `(60 - systemStatus[0] * 10) / systemStatus[1]` (25 in PAL), the game's own per-second formula | |
| `ico_gs_paused` | `systemStatus[5]` | |
| `ico_gs_current_layout` | `current_layout_id` | `layout_texture.c` |
| `ico_gs_weapon_kind` | `CheckWeaponKind(GOBJ_ACT(boyGObj)->weapon)`, 0 for none (as `scpGameStat_BoyWeaponkind` in `script.c`) | |
| `ico_gs_held_item` | `GetCharHeldItem(boyGObj)` | `sugipon/src/item.c` |
| `ico_gs_yorda_present`, `ico_gs_yorda_held` | `girlGObj`; `ACTGame_FLAG_TETSUNAGI()` (holding hands) | `fumi/src/act-game.c` |
| `ico_gs_yorda_captured` | the girl's `actMode == 0x6F` (carried) and `carrier->kind == 4` (an enemy) | `enemy_act.c` |
| `ico_gs_enemies_killed`, `_game_overs`, `_saves`, `_endings` | signal counts this session | |
| `ico_gs_run_*` | the run (below) | |
| `ico_gs_developer_mode`, `_yorda_safe`, `_stick_fix` | `port/game/options.h` | docs/port/OPTIONS.md |
| `ico_gs_start_stage_used` | `ICO_START_STAGE` 2 to 105, as `debug_TryToGetStartStage` reads it | `common/src/debug.c` |
| `ico_gs_achievements_suspended` | developer mode or a start stage (not `yorda_safe`, the stick fix or mirror mode) | |

**The run** starts at the title (entering stage 1) and is reset by a new
game. It is *fresh* when gflag 382 came on (the new-game choice,
`gflagOn(382)` in `layout_action.c`) and no load followed (layout 25,
`la_load_processing`). It counts captures and game overs, records whether
any of `op.c`'s opening parts was skipped, and whether achievements were
suspended at any tick (`ico_gs_run_suspended`, sticky for the run). The run
lives in memory only, so quitting mid-run and loading a save ends it as not
fresh; the fresh-run challenges need one session from New Game to the
ending.

### Signals

`ico_gs_signal(event, arg)` queues an event; `ico_gs_signaled(ev)` and
`ico_gs_signal_arg(ev)` read the current tick's count and last argument.
Each hook site is one call, with `#include "ico_gamestate.h"` at the top
of the file.

| event | arg | site |
| --- | --- | --- |
| STAGE_ENTER | the stage | `ico2/common/src/StageManager.c` `start_stage_Load_thread`, after `stage_no = stage` |
| CHECKPOINT | `stage_no` | `ico2/common/src/StageManager.c` `CheckPoint`, after the game block is saved |
| ENEMY_KILLED | the enemy's label | `ico2/fumi/src/commonact.c`, the enemy branch of the death action after `actEnemyFlagOnDead` |
| GAME_OVER | 0 | `ico2/fumi/src/commonact.c` `ACT_LAYOUT_GAMEOVER`, when it first switches to layout 62 (every game-over cause goes through it) |
| YORDA_GRABBED | the enemy's label | `ico2/fumi/src/enemy_act.c` `actEnemyForceSwitchToCarry`, after `carrier = self` (free play and the scripted capture in `st13c.c`) |
| ENDING | `gFlagGameClear` before the ending's save | `ico2/script/src/end.c`, the start of `actEndingSave` (after the END logo) |
| FMV_END | 1 skipped (`movie_proc` returned 1), 0 played out | `ico2/common/src/main.c`, after `movie_proc` |
| DEMO_END | `part * 2`, plus 1 if START skipped it | `ico2/script/src/op.c`, after the three opening parts' wait loops (`actOpDemo01_2`, `actOpDemo02Chk`, `actOpDemo03Chk`) |
| WEAPON | the weapon kind | `ico2/sugipon/src/weapon.c` `PickupWeapon` (NONE, which is dropped, when the holder is not the boy) |

Polled events, derived by `ico_gs_tick` with no hook:

| event | when | why polled |
| --- | --- | --- |
| SAVE_DONE (arg: the couch label, `IosMcPreviewInfo[3]`, -1 off a couch) | `current_layout_id` becomes 41 | layout 41 is `la_save_confirm_complete`, which `la_save_processing` (layout 38) reaches only on success, so watching the layout avoids another hook in `layout_action.c` |
| YORDA_RESCUED | she was carried by an enemy, now is not, same stage, no game over | the carry ends in several places (`afterCommonCarry`, the enemy's death, the nest) |
| NEW_GAME | gflag 382 rose | |
| LOAD | layout 25 entered | |

Layout ids are the retail `texLayout` rows' procs, read from the ELF
(`texLayout` at `0x00533FE8`, 56-byte rows, the proc at +32): 25
`la_load_processing`, 38 `la_save_processing`, 41
`la_save_confirm_complete`, 54 `la_game_loop`, 55 `la_game_demo`, 62
`la_game_over_continue`.

### EE address peek

`ico_gs_peek(addr, size, &out)` answers 1-, 2- or 4-byte reads at retail PAL
EE addresses from the current snapshot, the shape an rcheevos memory
callback needs. Only these addresses are served; anything else returns -1:

| address | bytes | global | source of the address |
| --- | --- | --- | --- |
| `0x002A50C0` | 50 | `gflags` (file static) | `gflagChk` at `0x00181A48`: `lui v0,0x2a; addiu v0,v0,0x50c0` (retail ELF, `mips-linux-gnu-objdump`) |
| `0x0028F4C0` | 48 | `systemStatus[12]` | `la_game_loading` (`0x001BE9D0`) stores `systemStatus[6]` at `0x290000 - 2856` |
| `0x0029B9D0` | 24 | `IosMcPreviewInfo[6]` | `la_playtime_count` (`0x001BE9E8`): `lui v1,0x2a; addiu v1,v1,-17968`, then `+8` |
| `0x00639CE0` | 4 | `frame_count` | `config/symbol_addrs.pal.data.txt` |
| `0x00639D10` | 4 | `stage_no` | same file |
| `0x00639D20` | 4 | `before_stage_no` | same file |
| `0x00639D70` | 4 | `NonLinearCameraMove` (language) | same file |
| `0x00639EB4` | 4 | `gameover_flag` | same file |
| `0x00639EB8` | 4 | `gameover_layout_flag` | same file |
| `0x00639ED4` | 4 | `current_stage_no` | same file |
| `0x0063AA00` | 4 | `gFlagGameClear` | `gflagLoad` (`0x001819F8`): `gp - 24816`, `_gp = 0x00640AF0` (`config/link.pal.ld`) |
| `0x0063AA04` | 4 | `gFlagSaveStage` | `gflagLoad`: `gp - 24812` |
| `0x0063B414` | 4 | `gamesysTimeCount` | `config/symbol_addrs.pal.data.txt` |
| `0x0063B60C` | 4 | `current_layout_id` | `lt_switch_layout` (`0x001C0CB8`): `gp - 21732` |

`baserom/pal/MAIN.MAP` is not a source for these: its addresses belong to a
different link (`stage_no` is at `0x00629C90` there and at `0x00639D10` in
the retail ELF).

**Not addressable:** anything on the heap (objects reached through
`boyGObj`, `girlGObj`, generator and enemy records; RA's `AddAddress`
pointer chains), pointer-valued globals (`boyGObj` at `0x00639EA4` returns
-1, since its host value is a host pointer), and any global not in the
table. The 64-bit build's heap keeps neither the EE layout nor EE
addresses.

## The set

30 achievements, one kind of unlock. Stage names are `stageData[].name` in
the retail tables.

| id | title | condition | game facts read |
| --- | --- | --- | --- |
| `opening` | The Sacrifice | enter stage 3 (st13b SACRIFICE) in a fresh run with gflag 4 on and none of `op.c`'s three opening parts skipped with START | STAGE_ENTER, gflag 4 (`actOpDemo03Chk`), DEMO_END args, NEW_GAME |
| `hand_in_hand` | Hand in Hand | holding hands | `ACTGame_FLAG_TETSUNAGI` |
| `gate` | The Castle Gate | in stage 11, st04a (GATE_1ST) | `stage_no` |
| `windmill` | The Windmill | stage 15, st09a (WINDMILL) | `stage_no` |
| `graveyard` | The Graveyard | stage 13, st18a (GRAVE) | `stage_no` |
| `waterfall` | The Waterfall | stage 22, st02a (WATERFALL) | `stage_no` |
| `gondola` | The Gondola | stage 25, st20a (GONDOLA) | `stage_no` |
| `water_tower` | The Water Tower | stage 26, st10r (WATERTOWER) | `stage_no` |
| `cliff` | The Cliff | stage 31, st22a (CLIFF) | `stage_no` |
| `east_and_west` | East and West | stages 18 st04b (SYMMETRY_L) and 27 st05b (SYMMETRY_R) both visited (persisted set) | `stage_no` |
| `queen` | The Queen | stage 37, st25a (QUEEN) | `stage_no` |
| `queen_defeated` | The Queen Falls | gflag 338 on | `actSt25aQueenDeadChk` in `st25a.c` sets 338 and 5 |
| `shore` | The Shore | stage 39, st27a (BEACH) | `stage_no` |
| `finish` | Ico | the ending; also unlocks Settings > Extras > Credits, as does a clear count above 0 (docs/port/EXTRAS.md, "Credits") | ENDING |
| `finish_again` | Once More | the ending with `gFlagGameClear` 1 (a game continued from a cleared save) | ENDING arg |
| `first_shadow` | Shadow Banisher | 1 enemy defeated (all time) | ENEMY_KILLED |
| `shadows_25` | Shadow Hunter | 25 | ENEMY_KILLED |
| `shadows_100` | Bane of Shadows | 100 | ENEMY_KILLED |
| `rescue` | Not Without Her | Yorda freed from an enemy's carry | YORDA_RESCUED |
| `hand_10_minutes` | Never Let Go | 10 minutes of holding hands in all, unpaused (1000 / tick_hz ms a tick) | `ACTGame_FLAG_TETSUNAGI`, `systemStatus[5]` |
| `hand_60_minutes` | Inseparable | 60 minutes | same |
| `first_save` | A Moment's Rest | a save completes | SAVE_DONE |
| `couches_5` | Weary Travellers | saves on 5 distinct couches (persisted) | SAVE_DONE arg (`IosMcPreviewInfo[3]` = `GetSaveSofaLayoutID`) |
| `sword` | The Sword | weapon kind 4 held or picked up | weapon kind, WEAPON |
| `queen_sword` | The Queen's Sword | kind 5 | same |
| `light_blade` (hidden) | A Strange Light | kind 8 or 9 | same |
| `never_taken` | Never Taken | the ending of a fresh run with no capture | ENDING, YORDA_GRABBED (and a polled carry start) |
| `unbroken` | Unbroken | the ending of a fresh run with no game over | ENDING, GAME_OVER |
| `swift` | Swift Escape | the ending with play time under 3 hours | ENDING, `IosMcPreviewInfo[2]` |
| `shore_secret` (hidden) | A Gift on the Shore | gflag 354 on | `actSt27aEndChk` in `end.c` sets 354 when the boy holds item kind 3 at the beach trigger; `actSt27aEndDemo` then plays motions 470-473 and stage animations 869 and 198 instead of 467-469 |

A hidden achievement shows as "???" in the Settings menu's list until it is
unlocked; its popup is like any other.

### Weapons

`weaponKind[10]` in the retail ELF (`0x00318EB8`, 36-byte `WeaponDef`:
length, grip, power, then ints):

| kind | length | power | what the code says |
| --- | --- | --- | --- |
| 0 | 0 | 1.0 | no draw (`WeaponDL` case 0), skipped by `CheckSwapableWeapon`: no weapon |
| 1, 2 | 80 | 1.5 | kind 1 carries a lightable torch child (`InitWeaponGeo` case 1): the stick |
| 3 | 60 | 2.0 | |
| 4 | 85 | 4.0 | required by `st04l.c` (`scpGameStat_BoyWeaponkind() != 4`) |
| 5, 6 | 110 | 8.0 | kind 5 is built by `initializeQueenzSword` and required by `st13b.c` and `queen.c`: the Queen's sword |
| 7 | 120 | 0.0 | drawn by `dispInsectNet` |
| 8, 9 | 100 | 20.0 | drawn by `dispLaserSword`; the blade grows to 270 while holding hands (`WeaponGeo`) |

Calling kind 4 "the sword" and kinds 8 and 9 "the blade of light" is an
inference from these numbers and draw functions (docs/TODO.md).

## Suspension

Achievements are **suspended** while developer mode or `[dev] start_stage`
is on, or while Settings > Extras > Credits plays the ending
(`ico_credits_active`, docs/port/EXTRAS.md "Credits"), and for the rest of
the run in which any was:
`ico_gs_run_suspended` is set at any tick where
`ico_gs_achievements_suspended` is true and cleared when the run resets at
the title or a new game. While suspended no counter advances (enemies, hand
time, couches, visited stages) and nothing unlocks; queued popups still
show. Progress resumes when a run starts without them, and unlocks that
exist stay unlocked. The check is in `ico_ach_tick`
(`port/game/achievements.c`), which skips the counters and conditions when
either query is true.

`[gameplay] yorda_safe`, the stick fix and mirror mode are not treated as
assists: progress and unlocks with them on count normally.

## The file: `<pref>/achievements.toml`

`<pref>` is `ico_host_pref_dir` (docs/port/CONFIG.md; the executable's
folder in the headless build). The file is written through `ico_toml_save`
(a `.tmp`, then a rename) at once on every unlock, otherwise when counters
changed at most every 750 Main ticks (30 s, `ICO_ACH_STATS_FLUSH_TICKS`),
and at exit (`atexit`). Unknown keys and comments are kept.

```toml
version = 2

[stats]
enemies = 3
hand_ms = 400
saves = 1
clears = 0
couches = "77"
stages = "000000000000000000002f000000800a"   # 128-bit set, stage n = bit n

[unlocked.gate]
time = "2025-10-05T12:00:00Z"   # UTC
play_time = 754                 # the game's play time at the unlock, seconds
```

Unknown keys (for instance a version 1 file's `_all`/`_normal` counters
and `category` entries) are ignored on reading and left in place by the
writer, which has no remove; the counters come from the version 2 keys
only.

## Popups

An unlock calls `ui_PopupPush(title, body)` (docs/port/UI.md, "Popups") in
the language current at push time: the title, and the description broken
at spaces into lines of at most 44 letters, because the popup does not
wrap. Unlocks queue (64 deep); at most one popup is pushed every 125 Main
ticks (5 s, `ICO_ACH_POPUP_GAP_TICKS`; a popup shows for 230 vsyncs, 115
ticks), and a push the UI queue refuses is retried on the next tick.
`[game] achievements = false` turns the popups off; unlocks are recorded
either way. The headless build has no popup step (`ui_PopupVsync` is the
window build's), so there the first push stays current and the log line is
the record.

Each unlock is logged as `achievements: unlocked "<id>" at Main tick <t>,
stage <n> <name>, play time <s>s`.

## rcheevos readiness

What an `rc_client` integration would need from this interface:

- **Memory read callback:** `ico_gs_peek` already has the shape (address,
  byte count) and serves the fixed globals above from a per-tick snapshot,
  which matches `rc_client_do_frame` being called once per frame. An RA set
  written against heap pointer chains cannot be served; a standalone set
  would be written against these addresses or, better, against named
  queries.
- **Frame hook:** `ico_ach_host_poll` is where `rc_client_do_frame` (per
  Main tick) and `rc_client_idle` would go.
- **Hardcore:** `ico_gs_achievements_suspended` is the switch that would
  force Casual mode.
- **Hash:** the extractor would compute rcheevos' PS2 hash from
  `SYSTEM.CNF` and the ELF.
- **Missing:** a memory region map for `rc_client` (only scattered
  addresses are served; the rest reads as -1, which `rc_client` would have
  to accept as 0), and big-endian or float views (the snapshot is
  little-endian ints, as on the EE).

## Tests

`achievements_test` (ctest `achievements`, CPU) covers:

- the view over a synthetic snapshot: stage, name, entered, flags and their
  rise, game clear, PAL and 60 Hz tick rates, play seconds, weapon and
  Yorda state; peek at each kind of row, sub-word and unaligned reads, a
  read past a row, a pointer global and a heap address (-1);
- signals: queued, read the next tick, consumed after it, invalid events
  ignored, the 128 bound and the drop count; the polled save (38 then 41,
  the couch id), load and new game; a capture by hook and one the hook
  missed; a rescue, and a stage change that is not one; the game-over
  counters; the run reset; the suspension query (developer mode,
  `ICO_START_STAGE`; not `yorda_safe`) and the run's sticky flag;
- every achievement: locked before its trigger, unlocked by it, one
  `[unlocked.<id>]` record after triggering it again; the near misses (an
  opening part skipped, 24 of 25 enemies, paused hand time not counted, one
  short of 10 and 60 minutes, a repeated couch and a save off a couch, a
  capture, a game over, 4 hours of play, a loaded game for the fresh-run
  challenges); every id has a title and description in all five languages;
- suspension: in developer mode nothing unlocks, no counter advances and no
  popup is pushed; it stays suspended after switching it off within the
  run; progress resumes in a new run with one record; the Extras credits'
  flag and a start stage suspend the same way; `yorda_safe` counts
  normally; `finish` unlocks the Extras credits (`ico_credits_unlocked`);
- the file: stats, states, times (with a fixed clock) and play times
  survive a write and `ico_ach_init`; an unlocked achievement gives no
  second record or popup; a missing file reads as empty;
- popups: three unlocks in three ticks give one popup, the second exactly
  125 ticks after the first; a refused push is kept and retried; the
  language at push time; a long description broken into lines of at most
  44 letters; `[game] achievements = false` records the unlock with no
  push.

The scripted boot run (`pad-boot.txt`) skips the opening and reaches no
save, enemy or hand-holding, so it unlocks nothing; docs/TODO.md lists
what a play-through should confirm.

# Achievements (Phase 6, package 6E)

A port-owned achievement set with in-game popups, behind a typed game-state
interface that an rcheevos client could drive later. RetroAchievements
wiring is research only: RA's standalone policy excludes decompilation
ports (docs/research/retroachievements.md, R6, section 1). The design takes
two ideas from Dusklight (R6, section 5): event signals raised at a handful
of game sites, consumed the tick they are seen, and a separate category for
unlocks made with help (Dusklight's "Glitched", here "assisted").

| file | what |
| --- | --- |
| `port/include/ico_gamestate.h` | the read-only game-state view, the signal API, the retail-address peek |
| `port/game/gamestate.c` | its implementation; built with `ICO_GS_LIVE` into `ico_pc` (reads the game), without it into the test (synthetic sampler) |
| `port/game/achievements.c`, `.h` | the table, the per-tick checks, `achievements.toml`, the popup queue |
| `port/game/test/achievements_test.c` | ctest `achievements` (CPU) |
| `port/platform/host_loop.c` | the tick hook: `ico_ach_host_poll(ico_host_main_ticks())` at the end of `ico_host_step` |
| `port/ui/strings*.{h,c}` | the 61 strings (30 titles, 30 descriptions, "Assisted") in the five languages |

## How it runs

- **Once per Main tick.** `ico_host_step` (one simulated vsync) ends with
  `ico_ach_host_poll(ico_host_main_ticks())`, which runs `ico_ach_tick` once
  for each Main tick that has completed since the last call. That is after
  every fiber has run for the vsync, on the host context, so no game code is
  interleaved with the reads. `ico_ach_tick` calls `ico_gs_tick` (takes the
  snapshot, makes the signals queued since the last tick current, derives
  the polled events and counters), then evaluates every achievement not yet
  unlocked as normal, then pumps the popup queue and writes changed counters
  when due.
- **Reads only.** The live sampler copies globals and calls the game's own
  pure accessors (`gflagChk`, `CheckWeaponKind`, `GetCharHeldItem`,
  `ACTGame_FLAG_TETSUNAGI`). It follows `boyGObj` / `girlGObj` only while
  `stageManagerFreeResourceFlag == 0`, `mpegPlay == 0` and the pointers and
  their `act` records are non-null: `stop_free_resources`
  (`common/src/StageManager.c`) frees the object partitions and then zeroes
  both pointers, and the movie path zeroes them. Signals only append to a
  128-entry port-side queue (a full queue drops and counts).
- **Measured:** the 3000-tick headless run below gives a trace byte-identical
  to the run before this package.

## The game-state view (`ico_gamestate.h`)

| query | game fact read | source |
| --- | --- | --- |
| `ico_gs_stage`, `ico_gs_stage_name`, `ico_gs_stage_entered` | `stage_no`; `stageData[stage_no].name` (the runtime-loaded table); STAGE_ENTER or a change | `common/src/main.c`, `StageManager.c` |
| `ico_gs_flag(n)`, `ico_gs_flag_rose(n)` | `gflagChk(n)` for n < 400 (`gflags[50]`) | `script/src/gflag.c:58` |
| `ico_gs_game_clear` | `gFlagGameClear` (1 in a game continued from a cleared save; `actEndingSave` sets it to 1 for the ending's save and back to 0, `end.c:1702-1720`) | `script/src/gflag.c:18` |
| `ico_gs_play_frames`, `ico_gs_play_seconds` | `IosMcPreviewInfo[2]`, counted by `la_playtime_count` while not paused; seconds as the save screen's `playTime` computes them | `layout_action.c:1021`, `:2566` |
| `ico_gs_tick_hz` | `(60 - systemStatus[0] * 10) / systemStatus[1]` (25 in PAL), the game's own per-second formula | |
| `ico_gs_paused` | `systemStatus[5]` | |
| `ico_gs_current_layout` | `current_layout_id` | `layout_texture.c` |
| `ico_gs_weapon_kind` | `CheckWeaponKind(GOBJ_ACT(boyGObj)->weapon)`, 0 for none (as `scpGameStat_BoyWeaponkind`, `script.c:1541`) | |
| `ico_gs_held_item` | `GetCharHeldItem(boyGObj)` | `sugipon/src/item.c:797` |
| `ico_gs_yorda_present`, `ico_gs_yorda_held` | `girlGObj`; `ACTGame_FLAG_TETSUNAGI()` (holding hands) | `fumi/src/act-game.c:353` |
| `ico_gs_yorda_captured` | girl `actMode == 0x6F` (carried) and `carrier->kind == 4` (an enemy) | `enemy_act.c:1153`, `:845` |
| `ico_gs_enemies_killed`, `_game_overs`, `_saves`, `_endings` | signal counts this session | |
| `ico_gs_run_*` | the run (below) | |
| `ico_gs_developer_mode`, `_yorda_safe`, `_stick_fix` | `port/game/options.h` | docs/port/OPTIONS.md |
| `ico_gs_start_stage_used` | `ICO_START_STAGE` 2..105, as `debug_TryToGetStartStage` reads it | `common/src/debug.c:889` |
| `ico_gs_assisted_now` | developer mode, `yorda_safe` or `start_stage` | |

**The run** starts at the title (entering stage 1) and is reset again by a
new game. It is *fresh* when gflag 382 came on (the new-game choice,
`gflagOn(382)` at `layout_action.c:738`) and no load followed (layout 25,
`la_load_processing`). It counts captures, game overs, whether any of
`op.c`'s opening parts was skipped, and whether an assist was on at any tick.
Run state is not persisted: quitting mid-run and loading a save ends the run
as not fresh (open item 3).

### Signals

`ico_gs_signal(event, arg)` queues; `ico_gs_signaled(ev)` / `ico_gs_signal_arg(ev)`
read the current tick's count and last argument. Hook sites (each the one
call under `#ifdef ICO_HOST`, plus `#include "ico_gamestate.h"` under
`#ifdef ICO_HOST` at the top of the file, inserted as ASCII lines):

| event | arg | site |
| --- | --- | --- |
| STAGE_ENTER | the stage | `ico2/common/src/StageManager.c:193`, `start_stage_Load_thread` after `stage_no = stage` |
| CHECKPOINT | `stage_no` | `ico2/common/src/StageManager.c:497`, `CheckPoint` after the game block is saved |
| ENEMY_KILLED | the enemy's label | `ico2/fumi/src/commonact.c:1603`, the enemy branch of the death action after `actEnemyFlagOnDead` |
| GAME_OVER | 0 | `ico2/fumi/src/commonact.c:3805`, `ACT_LAYOUT_GAMEOVER` when it first switches to layout 62 (every game-over cause goes through it) |
| YORDA_GRABBED | the enemy's label | `ico2/fumi/src/enemy_act.c:1030`, `actEnemyForceSwitchToCarry` after `carrier = self` (free play and the scripted `st13c.c:631` capture) |
| ENDING | `gFlagGameClear` before the ending's save | `ico2/script/src/end.c:1711`, start of `actEndingSave` (after the END logo) |
| FMV_END | 1 skipped (`movie_proc` returned 1), 0 played out | `ico2/common/src/main.c:202`, after `movie_proc` |
| DEMO_END | `part * 2`, + 1 if START skipped it | `ico2/script/src/op.c:537`, `:670`, `:847`, after the three opening parts' wait loops (`actOpDemo01_2`, `actOpDemo02Chk`, `actOpDemo03Chk`) |
| WEAPON | the weapon kind | `ico2/sugipon/src/weapon.c:950`, `PickupWeapon` (the event is NONE, i.e. dropped, when the holder is not the boy) |

Polled (derived by `ico_gs_tick`, no hook):

| event | when | why polled |
| --- | --- | --- |
| SAVE_DONE (arg: couch label, `IosMcPreviewInfo[3]`, -1 off a couch) | `current_layout_id` becomes 41 | layout 41 is `la_save_confirm_complete`, which `la_save_processing` (layout 38) returns only on success (`layout_action.c:2368`). `layout_action.c` belongs to R7a/6C, so it is not hooked |
| YORDA_RESCUED | she was carried by an enemy, now is not, same stage, no game over | the carry ends in several places (`afterCommonCarry`, the enemy's death, the nest) |
| NEW_GAME | gflag 382 rose | |
| LOAD | layout 25 entered | |

Layout ids are the retail `texLayout` rows' procs, read from the ELF
(`texLayout` at `0x00533FE8`, 56-byte rows, proc at +32): 25
`la_load_processing`, 38 `la_save_processing`, 41
`la_save_confirm_complete`, 54 `la_game_loop`, 55 `la_game_demo`, 62
`la_game_over_continue`.

### EE address peek

`ico_gs_peek(addr, size, &out)` answers 1-, 2- or 4-byte reads at retail PAL
EE addresses from the current snapshot, for a future rcheevos memory
callback. Only these are served; anything else returns -1:

| address | bytes | global | source of the address |
| --- | --- | --- | --- |
| `0x002A50C0` | 50 | `gflags` (file static) | `gflagChk` at `0x00181A48`: `lui v0,0x2a; addiu v0,v0,0x50c0` (retail ELF, `mips-linux-gnu-objdump`) |
| `0x0028F4C0` | 48 | `systemStatus[12]` | `la_game_loading` (`0x001BE9D0`) stores `systemStatus[6]` at `0x290000 - 2856` |
| `0x0029B9D0` | 24 | `IosMcPreviewInfo[6]` | `la_playtime_count` (`0x001BE9E8`): `lui v1,0x2a; addiu v1,v1,-17968`, then `+8` |
| `0x00639CE0` | 4 | `frame_count` | `config/symbol_addrs.pal.data.txt:269` |
| `0x00639D10` | 4 | `stage_no` | same file `:272` |
| `0x00639D20` | 4 | `before_stage_no` | `:273` |
| `0x00639D70` | 4 | `NonLinearCameraMove` (language) | `:278` |
| `0x00639EB4` | 4 | `gameover_flag` | `:301` |
| `0x00639EB8` | 4 | `gameover_layout_flag` | `:302` |
| `0x00639ED4` | 4 | `current_stage_no` | `:306` |
| `0x0063AA00` | 4 | `gFlagGameClear` | `gflagLoad` (`0x001819F8`): `gp - 24816`, `_gp = 0x00640AF0` (`config/link.pal.ld:28`) |
| `0x0063AA04` | 4 | `gFlagSaveStage` | `gflagLoad`: `gp - 24812` |
| `0x0063B414` | 4 | `gamesysTimeCount` | `config/symbol_addrs.pal.data.txt:355` |
| `0x0063B60C` | 4 | `current_layout_id` | `lt_switch_layout` (`0x001C0CB8`): `gp - 21732` |

`baserom/pal/MAIN.MAP` is not a source for these: its addresses belong to a
different link (`stage_no` at `0x00629C90` there, `0x00639D10` in the retail
ELF).

**Not addressable:** anything on the heap (objects reached through
`boyGObj`, `girlGObj`, generator and enemy records; RA's `AddAddress`
pointer chains), pointer-valued globals (`boyGObj` `0x00639EA4` returns -1:
its host value is a host pointer), and any global not in the table. The
64-bit build's heap does not keep EE layout or EE addresses (R6, section 4).

## The set

30 achievements. "Normal" or "assisted" is decided at the unlock (below).
Stage names are `stageData[].name` in the retail tables.

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
| `queen_defeated` | The Queen Falls | gflag 338 on | `actSt25aQueenDeadChk`, `st25a.c:565` sets 338 and 5 |
| `shore` | The Shore | stage 39, st27a (BEACH) | `stage_no` |
| `finish` | Ico | the ending | ENDING |
| `finish_again` | Once More | the ending with `gFlagGameClear` 1 (a game continued from a cleared save) | ENDING arg |
| `first_shadow` | Shadow Banisher | 1 enemy defeated (all time) | ENEMY_KILLED |
| `shadows_25` | Shadow Hunter | 25 | ENEMY_KILLED |
| `shadows_100` | Bane of Shadows | 100 | ENEMY_KILLED |
| `rescue` | Not Without Her | Yorda freed from an enemy's carry | YORDA_RESCUED |
| `hand_10_minutes` | Never Let Go | 10 minutes of holding hands in all, unpaused (1000 / tick_hz ms a tick) | `ACTGame_FLAG_TETSUNAGI`, `systemStatus[5]` |
| `hand_60_minutes` | Inseparable | 60 minutes | same |
| `first_save` | A Moment's Rest | a save completes | SAVE_DONE |
| `couches_5` | Weary Travellers | saves on 5 distinct couches (persisted) | SAVE_DONE arg (`IosMcPreviewInfo[3]` = `GetSaveSofaLayoutID`, `layout_action.c:2334`) |
| `sword` | The Sword | weapon kind 4 held or picked up | weapon kind, WEAPON |
| `queen_sword` | The Queen's Sword | kind 5 | same |
| `light_blade` (hidden) | A Strange Light | kind 8 or 9 | same |
| `never_taken` | Never Taken | the ending of a fresh run with no capture | ENDING, YORDA_GRABBED (and a polled carry start) |
| `unbroken` | Unbroken | the ending of a fresh run with no game over | ENDING, GAME_OVER |
| `swift` | Swift Escape | the ending with play time under 3 hours | ENDING, `IosMcPreviewInfo[2]` |
| `shore_secret` (hidden) | A Gift on the Shore | gflag 354 on | `actSt27aEndChk` (`end.c:1060`) sets 354 when the boy holds item kind 3 at the beach trigger; `actSt27aEndDemo` then plays motions 470-473 and stage animations 869 and 198 instead of 467-469 |

"Hidden" only marks an entry a future list view should not show before it
is unlocked; its popup is shown as any other.

### Weapons

`weaponKind[10]` in the retail ELF (`0x00318EB8`, 36-byte `WeaponDef`;
length, grip, power, then ints):

| kind | length | power | what the code says |
| --- | --- | --- | --- |
| 0 | 0 | 1.0 | no draw (`WeaponDL` case 0), skipped by `CheckSwapableWeapon`: no weapon |
| 1, 2 | 80 | 1.5 | kind 1 carries a lightable torch child (`InitWeaponGeo` case 1): the stick |
| 3 | 60 | 2.0 | |
| 4 | 85 | 4.0 | required by `st04l.c:1618` (`scpGameStat_BoyWeaponkind() != 4`) |
| 5, 6 | 110 | 8.0 | kind 5 is built by `initializeQueenzSword`, required by `st13b.c:416` and `queen.c:969`: the Queen's sword |
| 7 | 120 | 0.0 | drawn by `dispInsectNet` |
| 8, 9 | 100 | 20.0 | drawn by `dispLaserSword`; the blade grows to 270 while holding hands (`WeaponGeo`) |

Calling kind 4 "the sword" and 8/9 "the blade of light" is an inference from
these numbers and draw functions; neither was confirmed in play (open item 1).

## Categories: normal and assisted

An unlock is **assisted** when, at that tick, developer mode, `[gameplay]
yorda_safe` or `[dev] start_stage` is on, or one of them was on at any tick
of the current run. Counters that build up (enemies, hand time, couches,
visited stages) are kept twice, all and unassisted, so progress made with an
assist on never counts towards a normal unlock: reaching 25 enemies with
some killed in developer mode gives "assisted" until 25 were killed without
an assist. An assisted unlock is upgraded to normal (with a second popup)
when the condition is later met without an assist; a normal unlock is final.
The stick fix and mirror mode change input, not the game's rules, and are
not assists.

docs/port/DEVELOPER_MODE.md, docs/port/OPTIONS.md and the
`UI_STR_DEVELOPER_NOTE` string agree since 7A: achievements are recorded as
assisted in developer mode, not suspended.

## The file: `<pref>/achievements.toml`

`<pref>` is `ico_host_pref_dir` (docs/port/CONFIG.md; the executable's
folder headless). Written through `ico_toml_save` (a `.tmp` then a rename),
at once on every unlock and otherwise when counters changed, at most every
750 Main ticks (30 s), and at exit (`atexit`). Unknown keys and comments are
kept.

```toml
version = 1

[stats]
enemies_all = 3
enemies_normal = 0
hand_ms_all = 400
hand_ms_normal = 0
saves = 1
clears = 0
couches_all = "77"
couches_normal = ""
stages_all = "000000000000000000002f000000800a"   # 128-bit set, stage n = bit n
stages_normal = "..."

[unlocked.gate]
time = "2025-10-05T12:00:00Z"   # UTC
category = "normal"             # or "assisted"
play_time = 754                 # the game's play time at the unlock, seconds
```

## Popups

`ui_PopupPush(title, body)` (docs/port/UI.md, "Popups") in the current
language at push time: the title, the description broken at spaces into
lines of at most 44 letters (the popup does not wrap), and for an assisted
unlock a last line "Assisted". Unlocks queue (64 deep); one popup is pushed
at most every 125 Main ticks (5 s; a popup shows for 230 vsyncs, 115 ticks),
and a push the UI queue refuses is retried on the next tick.
`[game] achievements = false` turns popups off; unlocks are recorded either
way. The headless build has no popup step (`ui_PopupVsync` is the window
build's), so there the first push stays current; the log line is the record.

Each unlock is logged: `achievements: unlocked "<id>" (<category>) at Main
tick <t>, stage <n> <name>, play time <s>s`.

## rcheevos readiness

What an `rc_client` integration (R6, section 3) would need from this
interface:

- **Memory read callback:** `ico_gs_peek` already has the shape (address,
  byte count) and serves the fixed globals above from a per-tick snapshot,
  which matches `rc_client_do_frame` being called once per frame. An RA set
  written against heap pointer chains cannot be served; a standalone set
  would be written against these addresses or, better, against named
  queries.
- **Frame hook:** `ico_ach_host_poll` is where `rc_client_do_frame` (per
  Main tick) and `rc_client_idle` would go.
- **Hardcore:** `ico_gs_assisted_now` is the switch that would force
  Casual mode (developer mode, `yorda_safe`, `start_stage`).
- **Hash:** not here; the extractor would compute rcheevos' PS2 hash from
  `SYSTEM.CNF` and the ELF (R6, section 2).
- **Missing:** a memory region map for `rc_client` (only scattered
  addresses are served, the rest reads as -1, which `rc_client` would have
  to accept as 0), and big-endian or float views (the snapshot is
  little-endian ints, as the EE).

## Tests

`achievements_test` (`port/game/CMakeLists.txt`, ctest `achievements`, CPU):

- the view over a synthetic snapshot: stage, name, entered, flags and their
  rise, game clear, PAL and NTSC tick rates, play seconds, weapon and Yorda
  state; peek at each kind of row, sub-word and unaligned reads, a read
  past a row, a pointer global and a heap address (-1), the table;
- signals: queued, read the next tick, consumed after it, invalid events
  ignored, the 128 bound and the drop count; the polled save (38 then 41,
  the couch id), load, new game; a capture by hook and one the hook missed;
  a rescue, and a stage change that is not one; game-over counters; the run
  reset; each assist (developer mode, `yorda_safe`, `ICO_START_STAGE`);
- every achievement: locked before its trigger, unlocked as normal by it,
  one `[unlocked.<id>]` record after triggering it again; the near misses
  (an opening part skipped, 24 of 25 enemies, paused hand time not counted,
  one short of 10 and 60 minutes, a repeated couch and a save off a couch, a
  capture, a game over, 4 hours of play, a loaded game for the fresh-run
  challenges); every id has a title and description in all five languages;
- assisted: developer mode gives "assisted" and the popup's "Assisted" line;
  still assisted after switching it off within the run; upgraded to normal
  in a new run with one record; `yorda_safe` and `start_stage`; an enemy
  killed in developer mode does not count towards the normal unlock;
- the file: stats, states, times (a fixed clock: `2025-10-05T12:00:00Z`) and
  play times survive a write and `ico_ach_init`; an unlocked achievement
  gives no second record or popup; a missing file reads as empty;
- popups: three unlocks in three ticks give one popup, the second exactly
  125 ticks after the first; a refused push is kept and retried; the
  language at push time; a long description broken into lines of at most
  44 letters; `[game] achievements = false` records the unlock with no push.

Results (2026-10-05): headless `linux-x64` (`-DICO_LINK_EXE=ON`), 55 of 55
ctest tests passed, `achievements` among them; `win-x64`
(`-DICO_LINK_EXE=ON`) builds `ico_pc.exe` and `achievements_test.exe`.

## Run (2026-10-05)

Headless `linux-x64`, `timeout 600`, `ticks=3000`, `pad_script` =
`port/input/pad-boot.txt`, the R6a run's `ico-pc.ini` and `config.toml`
(developer mode off), folder `build-host/6e-run`: exit 0 at 3000 Main
ticks, 6007 vsyncs, stage 3. `trace.txt` is byte-identical to the first 3002
lines of `build-host/r6a-run-off/trace.txt` (SHA-1
`169942575289028a9260dd478e716688295807f5` for both).

No achievement unlocked, as expected for this script: the run went title
(1) -> 41 op2 -> 42 24a4demo -> 43 13a4demo -> 45 13b4demo -> 40 deja -> 3
(st13b), with gflags 2-7 and 382 set (a new game), but `pad-boot.txt`
presses START every 40 ticks, so the opening parts were skipped; no save, no
enemy, no hand-holding in 3000 ticks. `achievements.toml` was written with
the visited-stage sets only. That run's binary checked the opening at the
end of `op.c`'s third part; the condition was then changed to entering stage
3 (above). The change is port-only (a condition), so the trace claim
carries, but the run was not repeated. What the run does not show: any
signal firing (signals are not logged) and any unlock in the live game.

## Open items

1. **Identities from code, not play:** weapon kinds 4 (sword) and 8/9
   (light blade), item kind 3 at the beach, the stage of the Queen's death
   flag, and "symmetrical halls" for st04b/st05b are read from the sources
   and tables; a play-through should confirm each title fits.
2. **The opening** checks `op.c`'s three parts only. Later parts of the
   opening (stages 45 and 40, other scripts: gflags 5-7 rise there) have
   their own skip loops and are not hooked.
3. **Run state is in memory.** The fresh-run challenges (`never_taken`,
   `unbroken`) need one session from new game to ending; per-save run state
   (Dusklight keeps per-achievement state in its file) would lift that.
4. **Developer mode wording** (done in 7A): DEVELOPER_MODE.md, OPTIONS.md,
   SETTINGS.md and `UI_STR_DEVELOPER_NOTE` now say achievements are recorded
   as assisted in developer mode.
5. **`port/ui/strings.h`** was extended with the 61 ids (an enum the tables
   need); the brief named only the tables. Translations are the author's,
   as for 6B (UI.md open item 4).
6. **List view** (done in 6C): the Settings menu's Achievements page lists
   them (docs/port/SETTINGS.md).
7. **Popups in the headless build** do not advance (no `ui_PopupVsync`), as
   for every popup.

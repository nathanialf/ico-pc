# Hard mode: the levers

A design note (not player-facing; internal names are used). It lists every lever a harder game could use,
by Options page: the game's value, a harder value, which discs carry it, whether achievements stay on,
and the mechanism. It was written for issue 206's discussion (a request that Ico can be hit while he is
knocked down). No Difficulty row ships yet: the rows below are the building blocks a preset would
set. Rows live under `[variants]` and `[gameplay]` in `config.toml` ([CONFIG.md](CONFIG.md)); the hooks are
in [GAME_FEATURES.md](GAME_FEATURES.md#game-variants). Disc columns use the slugs of
[GAME_FEATURES.md](GAME_FEATURES.md#options-on-each-disc): pal, jpn, kor, cn (the European family),
ntsc, aug6, jun26.

Achievements: a row that makes the game easier or breaks a rule suspends them while it is on
(`ico_variants_suspend_achievements`); the rows that only make it harder keep them on.

## Gameplay > Shadows

| lever | the game's value | harder value | discs | achievements | mechanism |
| --- | --- | --- | --- | --- | --- |
| Shadow speed (`shadow_speed`) | Original (x1) | Faster x1.25, Fastest x1.5 | all | stay on | `ico_opt_shadow_speed_motion` scales the locomotion motions only (walk, run, fly); attacks, grabs, flinches, the dissolve and the pit dive keep the game's speed (`port/game/options.c`) |
| Shadow toughness (`shade_toughness`) | Original: the disc's `enemyKind` table; later playthroughs tougher on the European family | American: every Shadow takes the North American disc's life, scale, fly type and battle type (life 400 for the toughest rows, "American" = the NTSC table); Third journey: factor `gameParam[40]` = x2 | American: all but jun26; Third journey: pal, jpn, kor, cn only (the others have no playthrough factor) | stay on | `ico_variant_enemy_def` over `ENEMY_DEF(kind)` (`ico2/sugipon/src/enemy.c`), `getEnemyRestartLife` (`enemy_act.c`) |
| Shadow aggression (`aggression`) | Their own (each kind's battle type in `enemyKind`) | Bold (Positive) with quick (High): `bold_quick` | all | stay on | `debug_enemy_battle_type`, read by `actEnemyStart` and its twins and `GetEnemyBattleType` |
| Shadows never take Yorda (`yorda_safe`) | off | easy direction: leave off | all | stay on (it is not in the suspension mask) | `ico_opt_yorda_safe` at seven sites (`ebrain.c`, `enemy_act.c`, `backStage.c`) |
| Peaceful shadows (`peaceful_shadows`) | off | easy direction: leave off | all | suspends | `debug_brain_flag` read at the four Shadow starts (`enemy_act.c`) with `ico_variant_shadow_brain()` |

## Gameplay > Combat

| lever | the game's value | harder value | discs | achievements | mechanism |
| --- | --- | --- | --- | --- | --- |
| Automatic guard (`auto_guard`) | On: a hit from the front within 60 degrees is blocked | Off: the hit lands as from behind | all | stay on | `_ACTCorrectMsg` case 13, `commonact.c` (one conjunct `self != boyGObj \|\| ico_variant_auto_guard()` per disc copy) |
| Turn to face shadows (`attack_turn`) | On: the swing turns Ico to the nearest Shadow (300 units, 90 degrees) | Off: he swings straight ahead | all | stay on | `actBoyAttack`, `boyact.c` (European, `_ntsc`, `_jun26`) |
| Shadows wait while Ico is down (`shadows_wait_down`) | On: the Shadows' brains hold during the down act and the 130-frame get-up timer, and Ico is invincible while he lies | Off: they do not hold back, and their swings land while he lies and gets up | all (see the audit row in [GAME_FEATURES.md](GAME_FEATURES.md#options-on-each-disc)) | stay on | two mechanisms. (1) `IsBoyStatus_EnemyMustWait` (`boyact.c`), read by `ebrain.c` `enemiesWait` (status 8): Off drops act mode 0x15 and `downTimer > 0`; the stone part and modes 0x16..0x17 stay. (2) `actCommonDown` (four copies in `commonact.c`) sets `hit = 1` every frame for every actor but kind 4, and `AttackGenerate*` (`attackhit.c`: 1030, 1089, 1154) skips a target with `hit != 0`; `ico_variant_down_keeps_hit(self == boyGObj)` leaves Ico's clear with the row Off. The flag clears in act mode 1 (ntsc, aug6, jun26) or 1 to 3 (European family) (`act-game.c`). A second hit on a lying Ico follows `_ACTCorrectMsg` case 13 (guard check, then the down message), so knockdowns can chain |
| One hit per swing (`one_hit`) | off | easy direction (a swing hurts a Shadow once): leave off | all | suspends | `debug_one_hit_only`, `attackhit.c` |
| Shadows never dodge (`never_dodge`) | off | easy direction: leave off | all | suspends | `debug_ignore_dodge`, `enemy_act.c` |
| Shadows attack together (`shadows_together`) | off: they take turns | on: none waits for the others | all | stay on | `EnemyUtil_isOtherStatus`, `enemy_act.c`, eleven callers ([GAME_FEATURES.md](GAME_FEATURES.md#shadows-attack-together)) |
| Queen barrier (`barrier_hits`) | 5 (7 on jun26) | 7 (more hits to break it) | all | stay on | `ico_variant_barrier_hits()`, `QueenBarrierGeo` and `QueenBarrierDL`, `ico2/ito/src/queen.c` |

One hit per swing, Shadows never dodge and Never take Yorda are rows in the easy direction (the first two
suspend achievements, the third does not), so a Hard step leaves them Off. Light sword colour and Stick change
the weapon, not the difficulty (Stick: Insect net removes the stick's damage, so a preset would not set it).

## Gameplay > Yorda

| lever | the game's value | harder value | discs | achievements | mechanism |
| --- | --- | --- | --- | --- | --- |
| Time to rescue Yorda (`pit_time`) | 15 s (462 ticks; 463 on jun26) | 4 s (a shorter pull-in) | all | stay on | the five SINK motions' play speed, `port/game/pit_time.h` and `ico_opt_pit_time_factor` |
| Chain swing speed / slowdown | 4 / 4 | not a difficulty lever (traversal feel) | all | stay on | `chain.c`, `debug_chain_cycle_speed`, `debug_chain_slow_speed` |

## The knock-down limit

A lever the game does not have: a game over when Ico is knocked down N times (Rules > Knock-down limit,
Off or 1 to 9; Knock-downs count: each room, or the whole journey). Hard sets 3 in each room. It is a row
of the journey's difficulty record (`port/game/difficulty.h`, `knock_limit`, `knock_scope`), not a
config key; achievements stay on.

| part | what it does | where |
| --- | --- | --- |
| the hook | each copy of `actCommonDown` (European body, `_ntsc`, `_aug6`, `_jun26`) calls `ico_gs_knockdown(is_boy, landing, by_hit, down_timer, intr_kind)` at its entry, after its after-proc is set | `ico2/fumi/src/commonact.c`; `port/game/gamestate.c` |
| what counts | the boy's fall when a hit put him down (`Act.attacker` set: `AttackMail`, `ico2/omori/src/attackhit.c`, sets it with the hit's mail 13; `ACTGame_BeforeFunc` clears it while he stands). Not counted: the same fall (the in-fall latch, or the get-up timer `downTimer` still running), the landing of a fall the down act began (the European body's mails 55 and 56, which also skip its `DownFunc`), a fall without a hit (CLIFF_HESITATE's mail into the act when he loses his balance at a ledge, `boyact.c`; the motion-driven down mails of `act-game.c`), Yorda's and the Shadows' falls (the act runs for all three actor kinds) | `ico_gs_knockdown`, `ico_difficulty_knockdown` |
| the latch | set by a counted fall; cleared once the snapshot shows him out of the down act (act mode 0x15) with `downTimer` 0, polled after the achievements' tick by the host step; not while his object is unreadable | `IcoGsSnapshot.boy_act_mode`, `boy_down_timer` (`ico_gs_game_down_timer`, `commonact.c`); `port/platform/host_loop.c` `ico_difficulty_tick` |
| the limit | the fall that reaches it (the room's count, or the journey's) returns 1; the copy runs `actDownGameOver` after its own `DownFunc` instead of its loop: he lies, `hit = 1` every frame (unhittable whatever Shadows wait while Ico is down is), no get-up mail, for `((60 - systemStatus[0] * 10) / systemStatus[1]) * 2` frames, the delay `actCommonDie` gives the boy's death (2 s at 50 and 60 Hz alike), then `actDownGameOverNow` asks for the game over: `ACT_LAYOUT_GAMEOVER`, or on the June 2001 demo `ico_gs_signal_game_over_once` + the game-over layout (that disc never clears `gameover_layout_flag`, `sceneManager.c` `InitSceneObjects_jun26`). It sets no pause or control flag (the boy's death sets none either), so nothing can be left locked | `commonact.c` |
| never missing | while the game over is due every entry of his returns 1 again (`over_pending`); a mail that takes him out of the down act first runs `actAfterDown` (the after-proc `BeforeFunc` runs at every interrupt's mode change, both bodies in `act.c`), which asks for the game over at once; `ico_gs_knock_game_over_take` lets it be asked once | `actAfterDown`, `ico_gs_knock_game_over_take` |
| room entry | `ico_gs_signal(ICO_GS_EV_STAGE_ENTER)` (the stage manager's one room-entry signal, `StageManager.c` `start_stage_Load_thread`; a Continue comes through it) calls `ico_difficulty_stage_enter`: the room's count starts again | `gamestate.c` |
| game over | every `ICO_GS_EV_GAME_OVER` (`ACT_LAYOUT_GAMEOVER`'s signal and the demo's) calls `ico_difficulty_game_over` in `ico_gs_tick`: the due game over cleared, the room's count after the limit's game over, the journey's count after the limit's game over in the journey scope, Permadeath ends the journey | `gamestate.c` |

Why the room entry is not a new call beside the stage manager's two port calls: `start_stage_Load_thread`
is static and called once, from the stage manager's thread loop, so the compiler may fold it into that loop,
and every save state holds the loop's thread suspended inside it; a state made by a build whose loop
differs is refused (`port/platform/state_xbuild.h`: a return address needs its function's code hash
alike). The signal there already runs at the same moment.

Log lines (while a limit is on; the first 8 of each, then every 100th): `difficulty: Ico down N (mail M, hit
H, landing L, get-up timer T, stage S): counted | not counted: <why> | the limit's game over is due ...; R in
this room, J on this journey, limit K a room|a journey`; `difficulty: the knock-down limit's game over asked
for (he lay 2 s | a mail took Ico out of the down act first; stage S)`; `difficulty: the knock-down limit's
game over is due but Ico is in act mode A (not down, not dead; ...)` when it has been due 2 s with him neither
down nor dead; the record's own lines (`difficulty: knock-down 2 of 3 in this room ...`).

The mails per disc (read off each disc's `actIntrList` by `knockdown_disc`): the knock-down mail into DOWN in
the shared list 3 is 291 on the European family, 275 on the North American disc, 272 on the August 2001
preview, 264 on the June 2001 demo; only the European family has the landing's 55 (list 3) and 56 (FALL's
list) into DOWN, so the three 2001 copies pass no landing.

## Not exposed yet

| mechanism | the game's value | what it would do | discs | where |
| --- | --- | --- | --- | --- |
| Get-up timer length | `actAfterDown`: `((60 - systemStatus[0] * 10) / systemStatus[1]) * 130 / 60` frames: 130 at 60 Hz, 108 at 50 Hz (about 2.2 s either way), the same on every disc (the June 2001 demo's `PAL_JUN26(0x82, 130) / PAL_JUN26(0x3C, 60)` is 130 / 60 on both arms) | a shorter timer shortens the time Ico lies and can be hit (with the row Off); a longer one lengthens it | all | `commonact.c` `actAfterDown`; read by `IsBoyStatus_EnemyMustWait` as `downTimer` |
| Random-spawn tables | disc data: 84 entries on the NTSC disc, 86 on the PAL family | more or faster spawns need an edit of the table data, not a code hook | disc data per disc | `port/data/gen/ntsc/table_desc.c` (:2460), `port/data/gen/pal/table_desc.c` (:3095) |
| Dodge ranges per kind | `GetEnemyDefDodgeRange`, one value per `enemyKind` row | smaller ranges make a Shadow dodge less (easier), larger more | all | `ico2/sugipon/src/enemyKind.c`, `enemy.c` |
| Shadow life per kind | `GetEnemyDefLife`, the kind's `life` (the NTSC table's 400 is the American row) | Toughness covers the two table sets; a per-kind or arbitrary multiplier is not exposed | all | `enemyKind.c`, `getEnemyRestartLife` |
| The mid-boss stage | stage script data | a harder encounter needs a stage script edit | disc data | the stage tables |

All Shadow kinds are shared by every disc (45 kinds in the tables, two flying classes); the NTSC disc has
no Shadow kind of its own. The game's only "bird" in code is scenery (`act_bird.c`).

## What a Difficulty preset would set

The harder values of the rows that keep achievements on, as one step:

| row | Normal | Hard |
| --- | --- | --- |
| Shadow speed | Original | Faster |
| Shadow toughness | Original | American (Third journey on the four discs that offer it) |
| Shadow aggression | Their own | Bold, quick |
| Automatic guard | On | Off |
| Turn to face shadows | On | Off |
| Shadows wait while Ico is down | On | Off (now includes the down invincibility) |
| Shadows attack together | Off | On |
| Queen barrier | the disc's | 7 |
| Time to rescue Yorda | 15 s | 8 s |

The values are a proposal for discussion, not a measured balance. Rows in the easy direction (One hit per
swing, Never dodge, Peaceful, Never take Yorda) are never part of a Hard step.

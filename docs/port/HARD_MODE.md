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
| No Shadows (the journey's `no_shadows` row, Customize only) | off | easy direction (no Shadow at all): no preset sets it | all | stay on (no difficulty row switches them off) | every Shadow starts as a killed one (`actEnemyStart` x4), the pits hold their calls at 0 (`GeneratorGeo` x4, `DirectCallEnemy`, `Generator_QuickCall`, `UnlockEnemyGenerate`), the left-alone timer stops (`backStageProcessMain` x4), the cage room's forced grab is skipped (`actEnemyForceSwitchToCarry`), the final fight ends once started and its music has begun (`InqCapsuleGhostBossEnd`, `st13b.c` waiters); the census of every script wait is [GAME_FEATURES.md](GAME_FEATURES.md#no-shadows)'s |

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

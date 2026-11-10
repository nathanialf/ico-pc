/*
 * port/game/no_shadows.h
 *
 * No Shadows (a Customize row of the journey's difficulty, difficulty.h
 * ico_no_shadows()): the game-side hooks' counters and log lines.
 *
 * The hooks (docs/port/GAME_FEATURES.md "No Shadows"):
 *   ico2/fumi/src/enemy_act.c    actEnemyStart x4: every Shadow of a room starts as one
 *                                killed (dead bit 18, display bit 21 clear, out of
 *                                sight), so a script waiting for "no Shadow left"
 *                                passes; ico_no_shadows_hold counts them.
 *                                actEnemyForceSwitchToCarry: the cage room's forced
 *                                grab does not happen (ico_no_shadows_forced).
 *   ico2/omori/src/generator.c   GeneratorGeo x4 hold the pit's call requests at 0
 *                                (the pit never opens), DirectCallEnemy,
 *                                Generator_QuickCall and UnlockEnemyGenerate do
 *                                nothing (ico_no_shadows_drop counts each).
 *   ico2/common/src/backStage.c  backStageProcessMain x4: the left-alone timer does not
 *                                run (ico_no_shadows_tick, which also prints the room's
 *                                line once its Shadows have all started).
 *   ico2/ito/src/itou_boss.c     InqCapsuleGhostBossEnd: true once the final fight has
 *                                started; gene_enemy x2 release nothing.
 *   ico2/script/src/st13b.c      the post-fight waiters hold until the fight's music
 *                                has started and a few seconds have passed
 *                                (ico_no_shadows_fight_*; ico_no_shadows_forced when one
 *                                passes).
 *   ico2/sugipon/src/enemy.c     EnemyGeo (both copies, a per-frame callback): a held-back
 *                                Shadow is kept out of the world every frame (put back
 *                                at the hidden position, its cylinder off, unhittable), so
 *                                one a script moves (st13b's 2470, the June 2001 demo's
 *                                104) neither blocks Ico or Yorda nor takes a swing.
 *
 * Names no game symbol: in the ico_options library.  Counters only in the
 * per-tick paths; lines on events (a room's Shadows started, a room left with
 * calls held back, a forced condition), each kind rate-limited (the first 8,
 * then every 100th).
 */
#ifndef ICO_NO_SHADOWS_H
#define ICO_NO_SHADOWS_H

/* what ico_no_shadows_drop counts */
enum {
    ICO_NOSH_DROP_PIT = 0, /* a pit's call request held at 0 (GeneratorGeo) */
    ICO_NOSH_DROP_DIRECT,  /* DirectCallEnemy refused (a script, a pit, a spider group) */
    ICO_NOSH_DROP_QUICK,   /* Generator_QuickCall refused (st13c's skipped cutscene) */
    ICO_NOSH_DROP_UNLOCK,  /* UnlockEnemyGenerate refused (a spider group's revive) */
    ICO_NOSH_DROP_COUNT
};

/* what ico_no_shadows_forced reports */
enum {
    ICO_NOSH_FORCED_GRAB = 0,    /* a forced grab of Yorda skipped (a = the Shadow's label) */
    ICO_NOSH_FORCED_FIGHT,       /* the final fight's end let through (a = the room) */
    ICO_NOSH_FORCED_FIGHT_JUN26, /* the June 2001 demo's final fight let through (a = the room) */
    ICO_NOSH_FORCED_COUNT
};

/* actEnemyStart: returns ico_no_shadows(); when on, counts one Shadow held
   back (was_dead: it began in its pit, else it stood in the room). */
int ico_no_shadows_hold(int label, int was_dead);
/* the generator's refusals: returns ico_no_shadows(); when on, adds n to the
   kind's count (ICO_NOSH_DROP_*). */
int ico_no_shadows_drop(int what, int n);
/* backStageProcessMain, every tick: returns ico_no_shadows(); prints the
   room's held-back line once two ticks pass with no new Shadow, and the
   left room's refusal counts when the room changes. */
int ico_no_shadows_tick(int stage);
/* a forced condition fired (ICO_NOSH_FORCED_*): one line. */
void ico_no_shadows_forced(int what, int a);

/* The final fight's hold (st13b.c's post-fight waiters).  The fight has no Shadow to beat, so a
   waiter would pass at its start and close the boss stream before the code that plays it had
   played it.  reset: the room's first tick (actSt13bBoss, both copies); begun: the fight's music
   has started (or the room was entered with the fight begun), hold_ticks still to wait; held: 1
   while a waiter must keep waiting under No Shadows (polled once a tick, counting the hold down;
   0 with the row off).  Port state, not the game's: no ico2 variable is added (the game's data
   layout hash decides whether another build's save states load), and the two words are a FRESH
   save-state block so a state saved during the hold resumes it. */
void ico_no_shadows_fight_reset(void);
void ico_no_shadows_fight_begun(int hold_ticks);
int ico_no_shadows_fight_held(void);
/* the two words' save-state blocks ("no_shadows.fight_begun", "no_shadows.fight_hold",
   ICO_STATE_FRESH; port/platform/state_port.c) */
int ico_no_shadows_state_register(void);

/* tests: the counts so far (what = a drop kind) and the lines printed */
int ico_no_shadows_drop_count(int what);
int ico_no_shadows_lines(void);
void ico_no_shadows_reset(void);

#endif /* ICO_NO_SHADOWS_H */

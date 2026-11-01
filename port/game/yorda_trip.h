/*
 * port/game/yorda_trip.h
 *
 * Yorda trips ([gameplay] yorda_trips, options.h; GAME_FEATURES.md, Yorda
 * trips).
 *
 * The original game has her trip and fall in one place: the main gate's
 * second stage, st04a (GATE_2ND).  Its script (ico2/script/src/st04a.c,
 * actSt04aGirlSitChk) counts the ticks her run-pulled bit is set (actGirlHand
 * sets it while Ico runs pulling her by the hand, ico2/fumi/src/girl_act.c)
 * and, after 3 seconds of them, mails her the sit-down interrupt; from her
 * run loop (GIRL HAND_RUNLOOP) that plays GIRL SIT(HETARI), SITLOOP(HETARI)
 * and SITEND(HETARI) and goes back to the hand walk or run.  Those motions
 * are per-stage data (motion area 4): only that stage's pack carries them,
 * and every stage change clears them (motionFileManager.c,
 * ResetDynamicMotionManager).
 *
 * With the option on, each stage load copies her per-stage HETARI motions
 * into the stage's motion memory (yorda_trip_live.c,
 * ico_yorda_trip_stage_loaded, from icoMisc.c InitIcoMisc), read once from
 * the pack that holds them (yorda_trip_source.c), and actGirlHand's
 * run-pulled tick asks the rule below whether she trips now.
 *
 * The rule (this file's part, pure: no game headers, game ticks only):
 * consecutive run-pulled ticks are counted by their tick stamp, a missed
 * tick starting the count again; once the count passes 3 seconds (the
 * original's own wait), each tick on which every guard holds trips her with
 * the chance 1 / ((ICO_YT_MEAN_S - ICO_YT_REFRACTORY_S) x ticks a second),
 * and after a trip none comes for ICO_YT_REFRACTORY_S seconds of game time:
 * one trip in about ICO_YT_MEAN_S seconds of run-pulling.  The random
 * numbers are the port's own (xorshift32), seeded once from the game's VU0
 * random register without advancing it, so the game's own random sequence
 * is the original's.  The generator, the count and the refractory time are
 * save state blocks (state_port.h).
 */
#ifndef ICO_PORT_GAME_YORDA_TRIP_H
#define ICO_PORT_GAME_YORDA_TRIP_H

#include <stddef.h>
#include <stdint.h>

/* the rule's times, in seconds */
#define ICO_YT_ELIGIBLE_S 3    /* run-pulled before a trip can come (st04a.c's 3 s) */
#define ICO_YT_MEAN_S 120      /* one trip in this much run-pulling, on average */
#define ICO_YT_REFRACTORY_S 30 /* no trip this soon after the last */

/* The save state's blocks (state_port.h): yorda_trip.rng, .seeded,
   .run_ticks, .refractory, .last_tick. */
int ico_yorda_trip_state_register(void);
/* The count, the refractory time and the generator back to unseeded */
void ico_yorda_trip_reset(void);

/* One run-pulled tick: `tick` the game's tick stamp (frame_count), `tps` the
   game's ticks a second ((60 - systemStatus[0] * 10) / systemStatus[1]),
   `ready` non-zero when every guard holds (yorda_trip_live.c), `seed` the
   generator's seed when it is not seeded yet.  A second call with the same
   stamp counts nothing and rolls nothing.  1 when she trips on this tick
   (the refractory time then runs; the fall ends the run, so the next run
   counts from 1), else 0. */
int ico_yorda_trip_step(uint32_t tick, int tps, int ready, uint32_t seed);
/* The run-pulled ticks counted now, and the count at the last trip (for the log) */
int ico_yorda_trip_run_ticks(void);
int ico_yorda_trip_last_run(void);
/* Game ticks before a trip can come again (0 when none is pending) */
int ico_yorda_trip_refractory(void);
/* The generator's state, 0 before the first roll seeds it */
uint32_t ico_yorda_trip_rng(void);

/* --- the motions ------------------------------------------------------------
 * Found by name in the disc's motion table (motionKind, the decomp's
 * motion-def names), so no disc's numbers are written here. */
#define ICO_YT_MAX_MOTIONS 8
#define ICO_YT_NAME 64

typedef struct IcoYtIds {
    int n;                      /* her per-stage (area 4) motions named "GIRL ...HETARI..." */
    int id[ICO_YT_MAX_MOTIONS]; /* their numbers, in table order */
    int run_loop;               /* "GIRL HAND_RUNLOOP", -1 when the table has none */
    int sit, sit_loop, sit_end; /* "GIRL SIT(HETARI)", "SITLOOP(HETARI)", "SITEND(HETARI)" */
    /* the motions the fall's rows of the motion-orient table lead on to
       (SIT3END, the hand walk and run loops) */
    int n_exit;
    int exit[ICO_YT_MAX_MOTIONS];
} IcoYtIds;

/* The selected disc's numbers from its motion table (rows: the table's
   count, region.h mv_motion_kinds), and the fall's exits from its
   motion-orient table (motionOrient, as loaded: every row of the three
   fall motions; the fall's own motions and KEEP left out).  0, or -1 when
   the fall's three motions or the run loop are not all named in it. */
int ico_yorda_trip_find_ids(IcoYtIds *out, int rows);

/* One motion file as its pack holds it (before InitMotionFile relocates it) */
typedef struct IcoYtCopy {
    int id;
    uint32_t size;
    char name[ICO_YT_NAME]; /* the member's name, as the pack has it */
    uint8_t *data;          /* host heap, size bytes */
} IcoYtCopy;

typedef struct IcoYtSource {
    char pack[40]; /* the DATA.DF entry they came from ("STG4A2ND.DF") */
    int n;
    IcoYtCopy m[ICO_YT_MAX_MOTIONS];
    uint32_t bytes; /* their sizes added */
} IcoYtSource;

/* Reads the motions of `ids` from the first pack of DATA.DF (in the
   directory's order) that holds "GIRL SIT(HETARI)" as a motion member (a
   .mob whose id is its number): idx is an index of the caller's own
   (df_pack.h ico_df_index_open over a mount of its own).  0 with *out
   filled (every motion of ids that pack holds), 1 when no pack holds it,
   -1 on a read error or out of memory (*out empty); why gets a line for the
   log either way.  Free with ico_yorda_trip_source_free. */
struct IcoDfIndex;
int ico_yorda_trip_source_read(struct IcoDfIndex *idx, const IcoYtIds *ids, IcoYtSource *out,
                               char *why, size_t why_size);
void ico_yorda_trip_source_free(IcoYtSource *src);

/* --- the game's side (yorda_trip_live.c, the program only) ------------------ */
/* InitIcoMisc, right after the stage's pack is loaded (both copies,
   ico2/common/src/icoMisc.c): with the option on, adds the motions of
   IcoYtIds the stage did not load to the stage's motion memory. */
void ico_yorda_trip_stage_loaded(int stage);

#endif /* ICO_PORT_GAME_YORDA_TRIP_H */

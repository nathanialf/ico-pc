/*
 * port/include/ico_gamestate.h
 *
 * A typed, read-only view of the game state the port's achievements need
 * (Phase 6, package 6E), and the event signals a
 * handful of game sites raise under ICO_HOST.  Nothing here writes game
 * state: the view is a snapshot taken once per Main tick, and a signal only
 * appends to a port-side queue.  This is the interface an rcheevos client
 * could sit behind later:
 * named queries, plus ico_gs_peek for the few fixed retail globals.
 *
 * Use:
 *   ico_gs_tick();                 once per Main tick: takes the snapshot,
 *                                  makes this tick's signals current (the
 *                                  ones raised since the last tick), derives
 *                                  the polled events and the counters
 *   ico_gs_stage(), ico_gs_flag(n), ico_gs_signaled(ev), ...   then read
 *
 * The snapshot comes from a sampler: the game's globals in the program
 * (gamestate.c built with ICO_GS_LIVE), a synthetic one in the tests
 * (ico_gs_set_sampler).
 */
#ifndef ICO_GAMESTATE_H
#define ICO_GAMESTATE_H

#ifdef __cplusplus
extern "C" {
#endif

/* --- signals ------------------------------------------------------------ */

/* Raised by the game (one-line hooks under ICO_HOST) or derived by ico_gs_tick from
   the snapshot ("polled"). */
typedef enum IcoGsEvent {
    ICO_GS_EV_NONE = 0,
    ICO_GS_EV_STAGE_ENTER,   /* arg: the stage number (StageManager.c) */
    ICO_GS_EV_CHECKPOINT,    /* arg: stage_no; CheckPoint saved the game block */
    ICO_GS_EV_ENEMY_KILLED,  /* arg: the enemy's generator label (commonact.c) */
    ICO_GS_EV_YORDA_GRABBED, /* arg: the enemy's label; an enemy picked her up */
    ICO_GS_EV_GAME_OVER,     /* arg: 0; the game-over layout was requested */
    ICO_GS_EV_ENDING,        /* arg: gFlagGameClear before the ending's save */
    ICO_GS_EV_FMV_END,       /* arg: 1 skipped, 0 played out (main.c) */
    ICO_GS_EV_DEMO_END,      /* arg: part * 2, plus 1 skipped with START (op.c, st13b.c, deja.c) */
    ICO_GS_EV_WEAPON,        /* arg: the weapon kind the boy picked up (weapon.c) */
    /* polled (derived from the snapshot by ico_gs_tick) */
    ICO_GS_EV_SAVE_DONE,     /* arg: the couch's label id (IosMcPreviewInfo[3]) */
    ICO_GS_EV_YORDA_RESCUED, /* arg: 0; she was carried by an enemy and is free */
    ICO_GS_EV_NEW_GAME,      /* arg: 0; gflag 382 came on (the new-game choice) */
    ICO_GS_EV_LOAD,          /* arg: 0; the load layout (25) ran */
    ICO_GS_EV_COUNT
} IcoGsEvent;

/* Queues a signal for the next ico_gs_tick (a bounded queue, a full one
   drops and counts).  The game's hooks call this from any EE thread: those
   are fibers on the host thread (port/platform/sched.c), as is the
   ico_gs_tick caller (host_loop.c, between scheduler runs), so the queue
   takes no lock.  Not safe from another OS thread. */
void ico_gs_signal(int event, int arg);
/* This tick's signals: how many of event, and the last one's arg. */
int ico_gs_signaled(IcoGsEvent event);
int ico_gs_signal_arg(IcoGsEvent event);
/* signals dropped because the queue was full (diagnostics) */
unsigned int ico_gs_signals_dropped(void);

/* --- the snapshot ------------------------------------------------------- */

#define ICO_GS_GFLAG_BYTES 50 /* gflags[50], script/src/gflag.c:14 */

/* Raw values as the game holds them.  The first group is what ico_gs_peek
   serves; the second is derived through the game's own functions and
   pointer chains (never served by address). */
typedef struct IcoGsSnapshot {
    int valid; /* 0 before the game's main runs */
    /* fixed globals (ico_gs_peek) */
    int stage_no;
    int before_stage_no;
    int current_stage_no;
    int gameover_flag;
    int gameover_layout_flag;
    int frame_count;
    int language;   /* NonLinearCameraMove: 2 EN, 3 FR, 4 DE, 5 IT, 6 ES */
    int time_count; /* gamesysTimeCount */
    int game_clear; /* gFlagGameClear */
    int save_stage; /* gFlagSaveStage */
    int layout;     /* current_layout_id */
    int system_status[12];
    int mc_preview[6]; /* IosMcPreviewInfo: [0] stage, [1] clear, [2] play
                          time in frames, [3] couch id */
    unsigned char gflags[ICO_GS_GFLAG_BYTES];
    /* derived (only when actors_valid) */
    int actors_valid; /* the boy's and girl's objects are readable now */
    int boy_present;
    int girl_present;
    int weapon_kind;            /* scpGameStat_BoyWeaponkind: 0 bare hands */
    int hand_held;              /* ACTGame_FLAG_TETSUNAGI: holding hands */
    int yorda_carried_by_enemy; /* girl actMode 0x6F, carrier an enemy */
    const char *stage_name;     /* stageData[stage_no].name, or NULL */
} IcoGsSnapshot;

typedef void (*IcoGsSampler)(IcoGsSnapshot *out);
/* NULL restores the default (the live game in the program; nothing, i.e.
   an invalid snapshot, in a build without ICO_GS_LIVE). */
void ico_gs_set_sampler(IcoGsSampler fn);
/* The sampler over the game's globals (ICO_GS_LIVE builds only). */
void ico_gs_sample_live(IcoGsSnapshot *out);

/* Once per Main tick, before the reads below. */
void ico_gs_tick(void);
/* Forgets everything (signals, counters, the run); tests and a new profile. */
void ico_gs_reset(void);
/* Main ticks seen by ico_gs_tick */
unsigned int ico_gs_ticks(void);

/* --- typed queries ------------------------------------------------------ */

int ico_gs_valid(void);
int ico_gs_stage(void);
/* the stage's name as the game prints it ("st09a (WINDMILL)"); "" if
   unknown.  The names are the game's stageData table, read at run time. */
const char *ico_gs_stage_name(void);
/* whether the stage was entered this tick (STAGE_ENTER, or stage_no changed) */
int ico_gs_stage_entered(void);
/* story flag n (0..399), as gflagChk(n) */
int ico_gs_flag(int n);
/* gflag n changed from 0 to 1 this tick */
int ico_gs_flag_rose(int n);
/* gFlagGameClear: 1 in a game continued from a cleared save */
int ico_gs_game_clear(void);
/* play time as the save screen shows it (playTime, layout_action.c:1021) */
unsigned int ico_gs_play_frames(void);
unsigned int ico_gs_play_seconds(void);
/* Main ticks per second, ((60 - systemStatus[0] * 10) / systemStatus[1]) */
int ico_gs_tick_hz(void);
/* the game is paused (systemStatus[5]) */
int ico_gs_paused(void);
/* the boy's weapon: 0 bare hands or not known; kinds per weaponKind[10] */
int ico_gs_weapon_kind(void);
int ico_gs_yorda_present(void);
int ico_gs_yorda_held(void);     /* holding hands now */
int ico_gs_yorda_captured(void); /* carried by an enemy now */

/* counters since ico_gs_reset (this session) */
unsigned int ico_gs_enemies_killed(void);
unsigned int ico_gs_game_overs(void);
unsigned int ico_gs_saves(void);

/* The run: from the title (stage 1) to the next return to it.  "Fresh"
   when it began with a new game (gflag 382) and nothing was loaded. */
int ico_gs_run_fresh(void);
unsigned int ico_gs_run_captures(void);
unsigned int ico_gs_run_game_overs(void);
/* the saves (SAVE_DONE) and enemies defeated (ENEMY_KILLED) in this run, as
   ico_gs_saves and ico_gs_enemies_killed count the session's (the pause
   menu's journey lines) */
unsigned int ico_gs_run_saves(void);
unsigned int ico_gs_run_enemies(void);
/* the run's saves and enemies count only from its load: it came from a
   save whose slot kept no such counts (one made before v0.4.0, or with no
   entry in the achievements file); cleared by a New Game and at the title */
int ico_gs_run_partial(void);
/* a START skip of an opening part (op.c, st13b.c, deja.c) in this run */
int ico_gs_run_opening_skipped(void);
/* The opening's parts (DEMO_END's arg / 2, 1 to ICO_GS_OPENING_PARTS: op.c's
   three, st13b's conte 02, deja.c's) watched to the end in this run: bit
   part - 1 */
#define ICO_GS_OPENING_PARTS 5
unsigned int ico_gs_run_opening_parts(void);
/* achievements were suspended (below) at any tick of this run: the flag
   is sticky until the run resets */
int ico_gs_run_suspended(void);

/* The run as a value, to keep per save slot (achievements.c) */
typedef struct {
    int fresh;
    unsigned int captures;
    unsigned int game_overs;
    unsigned int opening_parts; /* ico_gs_run_opening_parts */
    int opening_skipped;
    int suspended;
    unsigned int saves;   /* ico_gs_run_saves */
    unsigned int enemies; /* ico_gs_run_enemies */
    int partial;          /* ico_gs_run_partial */
} IcoGsRun;

void ico_gs_run_get(IcoGsRun *out);
/* Replaces the run (a loaded slot's); the next tick goes on from it */
void ico_gs_run_set(const IcoGsRun *in);

/* the port's options (port/game/options.h) */
int ico_gs_developer_mode(void);
int ico_gs_yorda_safe(void);
/* [dev] start_stage put the game somewhere other than the boot */
int ico_gs_start_stage_used(void);
/* developer mode or start_stage is on, or the model viewer is up
   (options.h ico_mv_active), or the Extras credits are playing the ending
   (ico_credits.h): achievements are suspended (no counter
   advances, nothing unlocks).  yorda_safe, the stick fix and mirror
   mode do not suspend them. */
int ico_gs_achievements_suspended(void);

/* --- retail addresses ---------------------------------------------------- */

/* Reads size (1, 2 or 4) bytes at the retail PAL EE address addr into *out
   (little endian, as the EE), from this tick's snapshot: the fixed globals
   listed there.  0, or -1 for an
   address outside them (heap objects, pointers, anything else: not
   addressable).  For a future rcheevos memory callback. */
int ico_gs_peek(unsigned int addr, unsigned int size, unsigned int *out);

/* the table, for documentation and tests */
typedef struct IcoGsPeekEntry {
    unsigned int addr;
    unsigned int size;
    const char *name;
} IcoGsPeekEntry;

int ico_gs_peek_table(const IcoGsPeekEntry **out);

#ifdef __cplusplus
}
#endif

#endif /* ICO_GAMESTATE_H */

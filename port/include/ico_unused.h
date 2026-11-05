/*
 * port/include/ico_unused.h
 *
 * Settings > Extras > Unused content: what the retail discs carry and
 * nothing in play reaches, offered from the title as four rows.
 *
 *   ICO_UNUSED_ARENA  stage 88 boss_test (STGBOSS_TEST.DF): an arena on the
 *                     st13b floor with sixteen shadows, the magic sword
 *                     and two statues; no exit and no entrance row
 *   ICO_UNUSED_CLIMB  stage 91 8a_TEST (STG8TEST.DF): a climbing and boxes
 *                     room with Yorda; no exit (entrance exitData 260)
 *   ICO_UNUSED_MOVIE  stage 59 memoryPAL576 (the loose pal_advertise576.pss),
 *                     the European disc's second attract film, which the
 *                     title's attract never picks (op.c: 58 or 57)
 *   ICO_UNUSED_ART    the early show version's title art inside the logo
 *                     pack (object/sdf/e3/st13b/model/copy_right.p2o and
 *                     its kin), loaded at every boot and never drawn
 *
 * Only the debug Stage Select reaches the two rooms on the PS2
 * (common/src/debug.c debug_SelectStageMain).  The stage numbers are the
 * European disc's; every disc's own comes through ico_region_stage
 * (port/core/region.h), -1 where the disc has no such row.
 *
 * The CPU side (unused_content.c, in the achievements library: the row
 * table, the flag, the start, the engine indirection) needs no game; the
 * game's side is the engine the program installs (unused_live.c: whether
 * the running disc holds a row's data, and the start).  Settings reaches
 * both through its UiUnusedHost (port/ui/settings.h), set by the engine's
 * install.
 *
 * The game's side (ico2/) includes this header unconditionally, as it does
 * ico_credits.h.
 */
#ifndef ICO_UNUSED_H
#define ICO_UNUSED_H

#ifdef __cplusplus
extern "C" {
#endif

/* the rows, in the page's order (settings.c unusedOpts: UI_OPT_UNUSED_ARENA
   + row) */
enum { ICO_UNUSED_ARENA = 0, ICO_UNUSED_CLIMB, ICO_UNUSED_MOVIE, ICO_UNUSED_ART, ICO_UNUSED_COUNT };

/* a row's data: the European disc's stageData row, the pack that row's
   dataFile names (with .DF; NULL for the movie, whose stage row has no
   pack), the loose DATA.DF entry (the movie) and the pack member (the art)
   the row needs, and its name in the log */
typedef struct IcoUnusedRow {
    int pal_stage;
    const char *pack;
    const char *loose;
    const char *member;
    const char *name;
} IcoUnusedRow;

/* row 0 .. ICO_UNUSED_COUNT - 1, NULL past them */
const IcoUnusedRow *ico_unused_row(int row);
/* the row's stage on the running disc (ico_region_stage of pal_stage), -1
   when the disc has no such stage row */
int ico_unused_stage(int row);

/* whether the running disc has the row: its stage row exists and the
   engine finds its data in DATA.DF (0 without an engine); asked once per
   disc and kept */
int ico_unused_available(int row);

/* Settings > Extras > Unused content's press: the layout to switch to once
   the row has started, or -1 (logged "unused: failed ..."): no engine, the
   disc lacks the row, another row is running, or the engine refused */
int ico_unused_start(int row);

/* the row running (from its start until the title is entered again), -1
   none; ico_unused_active() is ico_unused_running() >= 0 */
int ico_unused_running(void);
int ico_unused_active(void);
/* the engine's: the row now running, or -1 when it ended */
void ico_unused_set_running(int row);

/* layout_action.c's game over, a choice taken while a room runs: both go
   to the title (the room has no save stage); the log's reason for the
   return is then "game over" */
void ico_unused_game_over(void);
/* why the room was left, for the engine's log: "game over" after
   ico_unused_game_over, else "End Game"; cleared by set_running */
const char *ico_unused_left_by(void);

/* the engine the program installs (unused_live.c): available answers
   whether DATA.DF holds the row's data (its stage's pack, the loose file,
   the member; 1 or 0); begin starts the row from the title and returns the
   layout to switch to, or -1 (the engine logs why); game_over (may be
   NULL) is told of ico_unused_game_over while a row runs, for its log */
typedef struct IcoUnusedEngine {
    int (*available)(int row);
    int (*begin)(int row);
    void (*game_over)(void);
} IcoUnusedEngine;

void ico_unused_set_engine(const IcoUnusedEngine *e);

/* --- the engine (unused_live.c, the program only) ------------------------ */

/* host_loop.c, before the game boots: the engine and Settings' host */
void ico_unused_engine_install(void);
/* host_loop.c, once per vsync after the game's threads */
void ico_unused_host_poll(void);
/* StageManager.c's start_stage_Load_thread, as stage_no becomes `stage`
   and before its objects are built: back at the title, the game's state
   is put back and the flag cleared */
void ico_unused_stage_enter(int stage);

/* common/src/debug.c: Stage Select's way into a stage (debug_SelectStageMain),
   with a fade: the sound segments and the stage's sounds closed, the
   pause menu allowed, the boards reset, the game's flag pair set as Stage
   Select sets it, the boy's control on, the stage change, the boy's
   records given to the stage.  The port calls it and writes no game flag
   itself. */
void ico_host_enter_test_stage(int stage);
/* common/src/debug.c: the boy's walk in from the entrance skipped the
   game's own way (the flag its checkpoint loads set, which
   E3_StageStartBoy clears and returns on), so a test room's boy stands
   where the room puts him */
void ico_host_skip_entry_walk(void);
/* omori/src/camera-ico2.c: the monitor camera for a test room's log
   lines (reads only): returns the zone the camera last found (-1 none
   since the stage's init); eye and at its eye and look-at in the roots'
   space, mode the camera mode, computed nonzero once a camera has been
   computed since the stage's init */
int ico_host_camera_view(float *eye, float *at, int *mode, int *computed);

#ifdef __cplusplus
}
#endif

#endif /* ICO_UNUSED_H */

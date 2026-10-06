/*
 * port/include/ico_credits.h
 *
 * Settings > Extras > Credits and the staff roll's port credit
 * (docs/port/EXTRAS.md, "Credits"; docs/port/UI.md, "Staff roll").
 *
 * The playback: Credits starts the ending's first staff roll stage (stage
 * 60, STAFF1) from the title, the stage the real ending enters from its
 * last scene, and the ending's scripts run as they do there (end.c) with
 * ico_credits_active() set: the roll's end goes back to the title instead
 * of the beach, nothing is saved, and the achievements are suspended.  The
 * game flags the playback changes are put back when the title is reached
 * again.
 *
 * The port credit: the roll (common/src/staffroll.c) posts the lines of
 * staffRollNameData up to staffRollNameDataNum and then the port's lines,
 * ico_roll_port_line(0 .. ico_roll_port_count() - 1), in every roll (the
 * real ending's and the playback's).  The disc's table is not changed.
 *
 * The game's side (ico2/) includes this header unconditionally, as it does
 * ico_gamestate.h.
 */
#ifndef ICO_CREDITS_H
#define ICO_CREDITS_H

#ifdef __cplusplus
extern "C" {
#endif

/* the stageData row the playback starts in: STAFF1 (st13b), where
   actStaff1 starts the roll (actStaff1Demo, staffRollStart) */
#define ICO_CREDITS_STAGE 60
/* the stage the playback returns to: the boot logo and title (stage 1), as
   the End Game path and actEndingSave go */
#define ICO_CREDITS_TITLE_STAGE 1

/* nonzero from the Extras row's start until the title is up again */
int ico_credits_active(void);

/* --- the roll's port credit (credits.c) ---------------------------------- */

/* the lines appended after staffRollNameData's: a gap of blank lines, the
   heading in the roll's heading form ("{R}< ... > "), the roll's gap
   between a heading and its name, the name ("{R}... "); each a char ** that
   lives as long as the program, as staffroll.c keeps the pointer */
int ico_roll_port_count(void);
char **ico_roll_port_line(int k);
/* the index (k) of the heading and of the name among those lines */
#define ICO_ROLL_PORT_HEADING 12
#define ICO_ROLL_PORT_NAME 17
/* staffRollStart's log line (the roll's length, the port lines' start) */
void ico_roll_started(int discLines);

/* --- the playback (credits.c, the engine in credits_live.c) -------------- */

/* the ending has been reached: [dev] unlock_credits, or the port's
   ending achievement ("finish") unlocked, or its clear count above 0 */
int ico_credits_unlocked(void);

/* the engine the program installs (credits_live.c): begin starts the
   stage change from the title, 0 or -1; poll runs once per vsync after the
   game's threads (port/platform/host_loop.c) */
typedef struct IcoCreditsEngine {
    int (*begin)(void);
} IcoCreditsEngine;

void ico_credits_set_engine(const IcoCreditsEngine *e);

/* Settings > Extras > Credits: the layout to switch to (the game's empty
   layout, 55) once the playback has started, or -1 (logged "credits:
   failed ...") */
int ico_credits_start(void);
/* the engine's: the flag on or off (the log names why) */
void ico_credits_set_active(int on);
/* the program's per-vsync poll (credits_live.c) */
void ico_credits_host_poll(void);
/* StageManager.c's start_stage_Load_thread, as stage_no becomes `stage`
   and before its objects are built (exit_stage has run): back at the
   title, the playback puts the game's state back (credits_live.c) */
void ico_credits_stage_enter(int stage);
/* end.c's actStaff1Chk, under the flag: arms the ending's song (stream 47,
   "ICO -You were there-") to open where the real ending has it when the
   roll starts, and returns its stream number for the request */
int ico_credits_song_start(void);

#ifdef __cplusplus
}
#endif

#endif /* ICO_CREDITS_H */

/*
 * port/game/ico_rows.h
 *
 * Two rows that act on Ico's side of the game, both built on code the discs
 * carry and no retail path reaches:
 *
 *   Free movement  Settings > Developer > Free movement, [developer]
 *                  free_movement (default false).  The game's own debug
 *                  move (act.c ACTDebugMove): L2 pressed while Ico is the
 *                  debug target hands his body to the left stick (over the
 *                  ground, camera relative) and the right stick (up and
 *                  down), L3 snaps him to the floor under him and L2 let go
 *                  after that gives him back.  The four boy control loops
 *                  (fumi/src/boyact.c subBoyControl and its three disc
 *                  copies) test a local switch fixed at 0; each reads
 *                  ico_free_movement instead.  It needs Developer mode (the
 *                  row shows with it, and a hidden row does nothing), and
 *                  achievements are off while it is on (gamestate.c).  The
 *                  key is a simulation key (platform/sim_keys.c).
 *
 *   Ask Yorda for a hint
 *                  Settings > Controls > Remap controls, the target "hint"
 *                  (input.h ICO_T_HINT; keyboard, mouse and gamepad, unbound
 *                  by default).  Only on the discs with the hint system (not
 *                  the North American disc or the two 2001 builds,
 *                  ICO_NTSCAUG6JUN26).  The device layer reports the
 *                  binding's state every vsync (ico_hint_ask_note); the
 *                  hint object (omori/src/lws_kyomi.c LwsKyomiGeo) takes a
 *                  fresh press (ico_hint_ask) and starts the stage's waiting
 *                  hint at once through the game's DebugHintStart.
 *
 * The first is read with the game's loop, the second with the hint object's
 * tick; neither draws or reads a file.
 */
#ifndef ICO_PORT_GAME_ICO_ROWS_H
#define ICO_PORT_GAME_ICO_ROWS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Free movement as the game's loops read it: the row on and Developer mode on.
   The first time it is read (and on every change) one line says which. */
int ico_free_movement(void);
/* The row's own value, whatever Developer mode says (the page's text) */
int ico_free_movement_row(void);
void ico_free_movement_set(int on);
/* Forget the run-time value: read from the config again (ico_opt_reload) */
void ico_rows_reload(void);

/* The hint binding's frame: down is whether any of its sources is held now,
   called once for every device snapshot (input_sdl.c).  A press (down after
   an up) is counted and logged (the first 8, then every 100th). */
void ico_hint_ask_note(int down);
/* The game's side, once a tick by the stage's waiting hint: 1 when a press
   arrived since the last call and not longer ago than ICO_HINT_ASK_AGE
   snapshots (an older one is dropped: nothing was waiting for it); the press
   is taken either way.  Logged when it fires. */
int ico_hint_ask(void);
#define ICO_HINT_ASK_AGE 12
/* The press count and the count fired, for tests and the log */
unsigned long ico_hint_ask_presses(void);
unsigned long ico_hint_ask_fired(void);
/* Back to a clean state (tests) */
void ico_hint_ask_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* ICO_PORT_GAME_ICO_ROWS_H */

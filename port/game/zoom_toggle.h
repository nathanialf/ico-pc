/*
 * port/game/zoom_toggle.h
 *
 * Zoom type ([gameplay] zoom_type, options.h; GAME_FEATURES.md "Zoom type").
 *
 * The game zooms the camera in while R2 is held: each copy of
 * SetCameraMatrix (ico2/omori/src/camera-root.c, the European body and the
 * _ntsc, _aug6 and _jun26 twins) tests the pad's R2 bit (now & 2) after a
 * script's zoom request (zoomRequest).  With Zoom type Toggle, one press zooms
 * in and the next zooms out: each copy asks ico_zoom_held for the bit it
 * tests, and the June 2001 demo's look-around (camera-ico2.c,
 * SetCameraMatrix_Ico2_jun26, which stops the stick look while R2 is held)
 * asks ico_zoom_wanted.
 *
 * The press is the R2 level against the previous tick's, not the pad's
 * trigger word: the boy's loop skips its pad read on its noStick paths
 * (ico2/fumi/src/boyact.c), and a trigger left from the last read would
 * flip the zoom every tick there; a level left from the last read stays the
 * same level and flips nothing.
 *
 * A toggled zoom ends (the camera zooms back out) when a script's zoom
 * takes over, when the bit's source cannot deliver it (no boy control, the
 * pad turned off), when the source changes (the boy's pad, the camera's own
 * pad with no boy, the European discs' cutscene look-around) and when the
 * camera's mode changes (a cutscene's path camera starting or ending); and at
 * every stage start (InitCamera).  A pause keeps it: no object ticks while
 * the game is paused (ico2/fumi/isys/obj_manager.c), the camera's included.
 *
 * The state is the port's, not the game's (a new game global would change
 * the game data's layout and refuse every older save state): one save-state
 * block, "zoom_toggle.state", FRESH (a state made before it existed loads
 * and starts unzoomed).  Under Hold every call returns what the game tests
 * today, tick for tick.
 */
#ifndef ICO_PORT_GAME_ZOOM_TOGGLE_H
#define ICO_PORT_GAME_ZOOM_TOGGLE_H

#define ICO_ZOOM_SRC_BOY 0  /* the boy's pad */
#define ICO_ZOOM_SRC_PAD 1  /* no boy: the camera's own pad */
#define ICO_ZOOM_SRC_LOOK 2 /* the European discs' cutscene look-around (useDemo) */

/* One camera tick's R2 test.  held: the pad's now & 2; override: a script's
   zoom request is in force (zoomRequest != 0); live: the source can deliver
   the bit; source: ICO_ZOOM_SRC_*; mode: the camera's mode (cameraMode).
   Hold: returns held unchanged (records the level, source and mode; no
   latch).  Toggle: on an override, !live, a new source or a new mode the
   latch clears and 0 is returned, the level recorded (R2 held through a
   script's zoom, a new source or a new mode does nothing afterwards; while
   control is off the game hands the boy's pad a zero level, and the 2001
   discs' camera pad too, so R2 still held when control returns is a press);
   otherwise a press (held now, not the tick before) flips the latch, which is
   returned. */
int ico_zoom_held(int held, int override, int live, int source, int mode);
/* The June 2001 demo's look-around test: Hold returns held, Toggle the
   latch (as camera-root.c's arm last set it). */
int ico_zoom_wanted(int held);
/* A stage start (InitCamera): no latch, nothing recorded. */
void ico_zoom_reset(void);
/* Camera reset (camera_reset.h): a toggled zoom ends (the latch only; the
   level and the source stay recorded, so R2 still held is no press). */
void ico_zoom_drop(void);
/* The save-state block (state_port.h): 0, or -1 when refused. */
int ico_zoom_state_register(void);

#endif /* ICO_PORT_GAME_ZOOM_TOGGLE_H */

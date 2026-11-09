/*
 * port/ui/settings_step.h
 *
 * Options rows that change by holding Left or Right, and the Picture colour
 * rows' coarse step (settings_step.c).  settings.c's settingsProc calls
 * these; they live in their own file so settings.c stays under the
 * repository's 256 KiB file cap.
 */
#ifndef PORT_UI_SETTINGS_STEP_H
#define PORT_UI_SETTINGS_STEP_H

#include "video_options.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ticks held before the first repeat, then a step every UI_STEP_REPEAT_EVERY
   ticks: 0.3 s at 50 Hz (0.25 s at 60), then about 17 to 20 steps a second */
#define UI_STEP_REPEAT_DELAY 15
#define UI_STEP_REPEAT_EVERY 3

/* the rows a held Left or Right keeps stepping: the value rows that are held
   at both ends (the colour rows, CRT strength, Upscaling sharpness, Motion
   blur strength, the three audio levels, the game's Brightness, Pit time and
   the two mouse rows); the enumerations that wrap step once a press */
int ui__step_repeats(int opt);

/* one tick of the hold tracker for the row `item` (its opt `opt`) under the
   cursor: `held` is pad[0].now.  Returns LT_PAD_LEFT or LT_PAD_RIGHT on a
   tick that repeats the step, else 0.  `first` (the page just opened)
   restarts it, as does letting go, another row or the other direction. */
int ui__step_repeat(int first, int item, int opt, unsigned int held);

/* Left/Right on a row as settingsProc takes it: ui_settings_step, then 1
   when a sound and a redraw are due.  A repeat (`repeat` 1) that left the
   value text as it was (held at an end) answers 0; a press always 1.  A
   repeat logs "settings: held step" (the first 8, then every 100th). */
int ui__step_row(int opt, int dir, int repeat);

/* the colour adjustment's field of o for one of its four rows */
int *ui__tone_field(IcoVideoOptions *o, int opt);
/* the colour rows' value in o moved by delta, held at 1..100 */
void ui__tone_add(IcoVideoOptions *o, int opt, int delta);
/* L1/R1: the live value of a colour row moved by delta (ICO_TONE_STEP_COARSE
   a press; 0 puts back ICO_TONE_DEFAULT) and set; 1 when opt is a colour row, else 0 (nothing done) */
int ui__tone_step(int opt, int delta);

#ifdef __cplusplus
}
#endif

#endif

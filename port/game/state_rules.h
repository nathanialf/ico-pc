/*
 * port/game/state_rules.h
 *
 * The save states' decisions that need neither the game nor the window
 * (port/platform/state.h is the interface; state_host.c does the work):
 * whether a save or a load may run now, the text of a result for the log,
 * a slot's files, the slot listing over a folder, the [dev] keys' values,
 * the thumbnail's downscale and the pause picture's steps.  Unit-tested by
 * port/game/test/state_host_test.c.
 */
#ifndef ICO_GAME_STATE_RULES_H
#define ICO_GAME_STATE_RULES_H

#include <stddef.h>
#include <stdint.h>

#include "state.h"
#include "state_file.h"

/* What the conditions look at, sampled by state_host.c at the host loop's
   quiescent point (plan "Save"). */
typedef struct IcoStateConds {
    int available;    /* ico_state_available() */
    int layout;       /* the game's current_layout_id */
    int layout_pause; /* the disc's pause menu layout (region.h layout_pause) */
    int layout_count; /* the disc's game layouts: a higher index is a port page */
    int stage;        /* stage_no */
    int stage_title;  /* the disc's title stage (region.h stage_title) */
    int paused;       /* systemStatus[5] */
    int stage_switch; /* systemStatus[6] */
    int card_busy;    /* ico_mc_host_pending() */
    int disc_idle;    /* ico_cdvd_host_idle() */
    int movie;        /* ico_movie_open() */
    int photo;        /* ico_photo_active() */
    int credits;      /* ico_credits_active() */
    int gallery;      /* gallery_playing() >= 0 */
    int unused;       /* ico_unused_active(): a test room of Extras > Unused content */
    int locked;       /* ico_difficulty_states_locked(): the journey in force has Permadeath on */
} IcoStateConds;

/* 1 when the game shows the pause menu, or a port page (Options, Save
   states) opened over it: paused, past the title, and the layout the pause
   menu or one of the port's. */
int ico_state_on_pause_menu(const IcoStateConds *c);

/* The UiStatesHost's can_save (state_host.c, the program only): the
   conditions sampled now, ico_state_on_pause_menu's answer. */
int ico_state_can_save_here(void);

/* ICO_STATE_OK, or the first refusal in this order: OFF, PERMADEATH (a
   save or a load while the journey in force has Permadeath on, before
   anything else is looked at), NOT_PAUSED (a save only: ico_state_on_pause_menu), STAGE_SWITCH,
   CARD_BUSY, DISC_BUSY, MOVIE, PHOTO (photo mode, the credits, the music
   gallery or a test room; *what, when not NULL, names which). */
IcoStateResult ico_state_conditions(const IcoStateConds *c, int saving, const char **what);

/* The log's words for a result: "not paused", "settings differ", ... ("ok"
   for ICO_STATE_OK). */
const char *ico_state_result_text(IcoStateResult r);

/* <dir>/slot<n><ext> (ext ".ics", ".png"): 0, or -1 when slot is out of
   range or the path does not fit (out then ""). */
int ico_state_slot_path(char *out, size_t size, const char *dir, int slot, const char *ext);

/* Fills out[0..ICO_STATE_SLOTS-1] from the files in dir: present, the
   header's play time, stage, time and commit, loadable when the header
   checks against `now` (ico_state_header_check; why_not otherwise, a
   damaged file's ICO_STATE_BAD_FILE included), and the picture's path
   when slot<n>.png exists.  A missing or empty dir lists four empty
   slots. */
void ico_state_list_dir(const char *dir, const IcoStateHeader *now,
                        IcoStateSlotInfo out[ICO_STATE_SLOTS]);

/* A [dev] key's value: "<tick>:<slot>" (state_save_at, state_load_at;
   slot 1..ICO_STATE_SLOTS) or "<tick>" (state_hash_at, slot NULL).  0 with
   the numbers, or -1 (both 0) for an empty, malformed or out-of-range
   value. */
int ico_state_parse_at(const char *s, unsigned int *tick, int *slot);

/* The thumbnail: w x h RGBA (pitch w * 4) box-filtered to dw x dh RGBA
   (dw, dh at least 1, dw at most w; a dh above h repeats rows), alpha set
   opaque. */
void ico_state_thumb_scale(const uint8_t *src, int w, int h, uint8_t *dst, int dw, int dh);
/* The size of the picture kept with a state, for a w x h picture shown at
   aspect (its width over its height on screen; 0 or less: w / h, square
   pixels): at most ICO_STATE_THUMB_W wide (the picture's width when
   narrower) and ICO_STATE_THUMB_H tall, the other side in proportion, at
   least 1 x 1.  The pause picture is the presentation box's pixels
   (square); the fallback, the DISPLAY frame, has half the lines it is
   shown with, so its aspect is the presentation box's.  At most 960 x 960
   RGB, the PNG (stored, rd_png.h) stays under 2.8 MB, inside the Save
   states page's 8 MB read (port/ui/settings.c statesDefaultLoad). */
#define ICO_STATE_THUMB_W 960
#define ICO_STATE_THUMB_H 960
void ico_state_thumb_size(int w, int h, double aspect, int *dw, int *dh);

/* <dir>/pause.png, the picture of the last pause (state_host.c): 0, or -1
   when dir is empty or the path does not fit (out then ""). */
int ico_state_pause_path(char *out, size_t size, const char *dir);

/* from copied to to through <to>.tmp and a rename (the slot's picture
   from the pause picture): 0, or -1 when from cannot be read, is empty or
   larger than 8 MB, or the copy cannot be written (no <to>.tmp left). */
int ico_state_copy_file(const char *from, const char *to);

/* After a load from the pause menu: the loaded slot's picture
   (<dir>/slot<n>.png) copied to <dir>/pause.png (ico_state_copy_file), so a
   save made in the same pause keeps a picture of the game as loaded, not
   the frame after the save with the menu over it.  *w and *h get its size
   from the PNG's header (0 when the header is not a PNG's).  0, or -1 when
   the slot has no picture, a path does not fit or the copy fails (nothing
   left behind; pause.png as it was). */
int ico_state_picture_to_pause(const char *dir, int slot, int *w, int *h);

/* The pause picture's step at a poll (state_host.c, once a host step):
   TAKE when the game went from play (layout_play at the last poll) to the
   pause menu (layout_pause now), so the tick that opened the menu has just
   run and its frame, the last one without the menu's dark backdrop, is
   still to be presented; DROP on the play layout (a picture taken before
   is no longer the game as it is); KEEP otherwise (the pause menu, the
   port's pages over it, a load).  *last is the layout at the last poll, -1
   before the first; it is updated. */
typedef enum IcoStateShotStep {
    ICO_STATE_SHOT_KEEP,
    ICO_STATE_SHOT_TAKE,
    ICO_STATE_SHOT_DROP
} IcoStateShotStep;

IcoStateShotStep ico_state_shot_step(int *last, int layout, int layout_play, int layout_pause);

/* 1 when the "difficulty.record" block (port/game/difficulty.h
   IcoDifficulty, n bytes at off in a state's reg section of reg_n bytes)
   is a record of this format with Permadeath on: such a state is refused
   (ICO_STATE_PERMADEATH) whatever journey is in force.  0 for a block of
   another size, outside the section, or of another record version. */
int ico_state_record_permadeath(const unsigned char *reg, uint64_t reg_n, uint64_t off, uint64_t n);

#endif /* ICO_GAME_STATE_RULES_H */

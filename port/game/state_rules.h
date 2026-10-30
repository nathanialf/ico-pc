/*
 * port/game/state_rules.h
 *
 * The save states' decisions that need neither the game nor the window
 * (port/platform/state.h is the interface; state_host.c does the work):
 * whether a save or a load may run now, the text of a result for the log,
 * a slot's files, the slot listing over a folder, the [dev] keys' values
 * and the thumbnail's downscale.  Unit-tested by
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
} IcoStateConds;

/* 1 when the game shows the pause menu, or a port page (Options, Save
   states) opened over it: paused, past the title, and the layout the pause
   menu or one of the port's. */
int ico_state_on_pause_menu(const IcoStateConds *c);

/* The UiStatesHost's can_save (state_host.c, the program only): the
   conditions sampled now, ico_state_on_pause_menu's answer. */
int ico_state_can_save_here(void);

/* ICO_STATE_OK, or the first refusal in this order: OFF, NOT_PAUSED (a
   save only: ico_state_on_pause_menu), STAGE_SWITCH, CARD_BUSY,
   DISC_BUSY, MOVIE, PHOTO (photo mode, the credits or the music gallery;
   *what, when not NULL, names which). */
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
   (dw, dh at least 1, at most w, h), alpha set opaque. */
void ico_state_thumb_scale(const uint8_t *src, int w, int h, uint8_t *dst, int dw, int dh);
/* The thumbnail's size for a w x h picture: ICO_STATE_THUMB_W wide (the
   picture's width when narrower), the height in proportion (at least 1). */
#define ICO_STATE_THUMB_W 320
void ico_state_thumb_size(int w, int h, int *dw, int *dh);

#endif /* ICO_GAME_STATE_RULES_H */

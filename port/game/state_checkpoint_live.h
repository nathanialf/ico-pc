/*
 * port/game/state_checkpoint_live.h
 *
 * The game's side of Load on a save state that cannot resume where it was
 * saved (port/save/state_checkpoint.h finds the checkpoint in the state;
 * port/game/state_host.c decides, reads the file and arms this): the
 * checkpoint goes in through the game's own load path for a card save
 * (layout_action.c la_host_checkpoint_continue: la_load_processing's steps
 * after the card read, then the stage switch), on the Main thread at the
 * end of the next Main tick (trace_host.h ico_host_set_main_tick_hook), as
 * the game's own layouts run it; nothing is written to the memory card.
 */
#ifndef ICO_GAME_STATE_CHECKPOINT_LIVE_H
#define ICO_GAME_STATE_CHECKPOINT_LIVE_H

#include <stddef.h>
#include <stdint.h>

#include "../save/state_checkpoint.h"

/* What the load puts in, copied from the state. */
typedef struct IcoCkptApply {
    int slot;
    unsigned char image[ICO_CKPT_IMAGE_BYTES];
    int have_preview, have_screen, have_girl;
    unsigned char preview[24];
    int32_t screen, girl;
    /* the state's difficulty record ("difficulty.record", this program's
       size), or none: built from its keys as for a state made before
       records (difficulty.h ico_difficulty_state_loaded) */
    int have_record;
    unsigned char record[1024];
    size_t record_n;
    char keys[4096];
    /* the result, on the Main thread: ok 1 with the stage switched to, or
       0 with why */
    void (*done)(int ok, int stage, const char *why);
} IcoCkptApply;

/* This program's own places of the globals (its layout for
   ico_ckpt_locate's third way): 0, or -1 when its game ranges are unknown.
   platform: this program's tag. */
int ico_ckpt_live_self(IcoCkptLayout *out, const char *platform, uintptr_t gdata_lo,
                       uintptr_t gdata_hi, uintptr_t gbss_lo, uintptr_t gbss_hi);

/* Arms the load for the next Main tick's end (installs the hook the first
   time).  a is copied. */
void ico_ckpt_live_arm(const IcoCkptApply *a);
/* 1 while a load is armed and not yet run */
int ico_ckpt_live_pending(void);

#endif /* ICO_GAME_STATE_CHECKPOINT_LIVE_H */

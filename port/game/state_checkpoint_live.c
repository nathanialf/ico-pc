/*
 * port/game/state_checkpoint_live.c
 *
 * The game's side of Load on a save state that cannot resume where it was
 * saved (state_checkpoint_live.h).  At the end of the Main tick after the
 * request (the Main thread, as the game's own layouts run a card load):
 *   - the stage switch must still be free (systemStatus[6] 0), else the
 *     load is refused and nothing changed;
 *   - the state's image goes into gameSysMainSaveBuff and its option words
 *     into optionScreenMode and girlControlMode, where the card load's
 *     gameblock_read puts the card's (mcard.c; the 2001 discs' block has no
 *     screen word, the caller then gives none);
 *   - la_host_checkpoint_continue (layout_action.c) runs the card load's
 *     own steps and switches to the image's stage, from the title as the
 *     load leaves it, in play as End Game leaves it;
 *   - the state's preview into IosMcPreviewInfo, where the card load puts
 *     the product file's (the play time goes on from the state's);
 *   - the journey's difficulty: the state's record put in its block and
 *     taken as a state load takes it (ico_difficulty_state_loaded), or for a
 *     state made before records one built from the state's keys;
 *   - the input recording told of the jump, as after a state load.
 * One log line says what went in.
 */
#include "state_checkpoint_live.h"

#include <stdio.h>
#include <string.h>

#include "difficulty.h"
#include "state_registry.h"
#include "trace_host.h" /* ico_host_main_ticks, ico_host_set_main_tick_hook */
#include "../input/input_record.h"

#include "gamesys.h" /* gameSysMainSaveBuff */

/* the game's side, declared here as the callers in the game do */
extern int systemStatus[];
extern int stage_no;
extern int IosMcPreviewInfo[]; /* mcard.c: int IosMcPreviewInfo[6] */
extern int optionScreenMode;
extern int girlControlMode;
extern int la_host_checkpoint_continue(int onTitle);

_Static_assert(sizeof(gameSysMainSaveBuff) == ICO_CKPT_IMAGE_BYTES,
               "the checkpoint image is gamesys.c's size");

#define TITLE_STAGE 1 /* End Game's and a game over's way back (layout_action.c nextStage = 1) */

static IcoCkptApply s_apply;
static int s_armed;
static int s_hooked;

static void place_of(IcoCkptPlace *g, const void *p, size_t n, uintptr_t gdata_lo,
                     uintptr_t gdata_hi, uintptr_t gbss_lo, uintptr_t gbss_hi)
{
    const uintptr_t a = (uintptr_t)p;

    g->range = ICO_CKPT_RANGE_NONE;
    g->off = 0;
    g->size = 0;
    if (a >= gdata_lo && a < gdata_hi && gdata_hi - a >= n) {
        g->range = 0;
        g->off = (uint32_t)(a - gdata_lo);
    } else if (a >= gbss_lo && a < gbss_hi && gbss_hi - a >= n) {
        g->range = 1;
        g->off = (uint32_t)(a - gbss_lo);
    }
    if (g->range != ICO_CKPT_RANGE_NONE) {
        g->size = (uint32_t)n;
    }
}

int ico_ckpt_live_self(IcoCkptLayout *out, const char *platform, uintptr_t gdata_lo,
                       uintptr_t gdata_hi, uintptr_t gbss_lo, uintptr_t gbss_hi)
{
    memset(out, 0, sizeof *out);
    if (gdata_hi <= gdata_lo) {
        return -1;
    }
    out->tag = "this version";
    out->commit = "";
    out->platform = platform;
    out->gdata_bytes = (uint32_t)(gdata_hi - gdata_lo);
    out->gbss_bytes = (uint32_t)(gbss_hi - gbss_lo);
    place_of(&out->g[ICO_CKPT_G_IMAGE], gameSysMainSaveBuff, sizeof gameSysMainSaveBuff, gdata_lo,
             gdata_hi, gbss_lo, gbss_hi);
    place_of(&out->g[ICO_CKPT_G_STATUS], systemStatus, ico_ckpt_global_size(ICO_CKPT_G_STATUS),
             gdata_lo, gdata_hi, gbss_lo, gbss_hi);
    place_of(&out->g[ICO_CKPT_G_PREVIEW], IosMcPreviewInfo,
             ico_ckpt_global_size(ICO_CKPT_G_PREVIEW), gdata_lo, gdata_hi, gbss_lo, gbss_hi);
    place_of(&out->g[ICO_CKPT_G_SCREEN], &optionScreenMode, sizeof optionScreenMode, gdata_lo,
             gdata_hi, gbss_lo, gbss_hi);
    place_of(&out->g[ICO_CKPT_G_GIRL], &girlControlMode, sizeof girlControlMode, gdata_lo, gdata_hi,
             gbss_lo, gbss_hi);
    place_of(&out->g[ICO_CKPT_G_STAGE], &stage_no, sizeof stage_no, gdata_lo, gdata_hi, gbss_lo,
             gbss_hi);
    out->asset = "";
    out->sha256 = "";
    return 0;
}

/* the journey's difficulty from the state: its record in the block, or
   built from its keys; 1 when the record was put in */
static int put_difficulty(void)
{
    int i;

    for (i = 0; s_apply.have_record && i < ico_state_registry_count(); i++) {
        const IcoStateEntry *e = ico_state_registry_entry(i);

        if (strcmp(e->name, "difficulty.record") == 0 && e->n == s_apply.record_n) {
            memcpy(e->p, s_apply.record, e->n);
            ico_difficulty_state_loaded(0, s_apply.keys);
            return 1;
        }
    }
    ico_difficulty_state_loaded(1, s_apply.keys);
    return 0;
}

static void tick(void)
{
    char why[96];
    int on_title, to, rec;

    if (!s_armed) {
        return;
    }
    s_armed = 0;
    if (systemStatus[6] != 0) {
        snprintf(why, sizeof why, "a stage switch began before it ran (stage %d)", stage_no);
        fprintf(stderr, "state: slot %d's checkpoint was not loaded: %s; nothing changed\n",
                s_apply.slot, why);
        if (s_apply.done != NULL) {
            s_apply.done(0, stage_no, why);
        }
        return;
    }
    on_title = stage_no == TITLE_STAGE;
    memcpy(gameSysMainSaveBuff, s_apply.image, sizeof gameSysMainSaveBuff);
    if (s_apply.have_screen) {
        optionScreenMode = s_apply.screen;
    }
    if (s_apply.have_girl) {
        girlControlMode = s_apply.girl;
    }
    to = la_host_checkpoint_continue(on_title);
    if (s_apply.have_preview) {
        memcpy(IosMcPreviewInfo, s_apply.preview, sizeof s_apply.preview);
    }
    rec = put_difficulty();
    ico_input_record_state_loaded(s_apply.slot, ico_host_main_ticks());
    fprintf(stderr,
            "state: slot %d continues from its checkpoint at Main tick %u: the game's own load "
            "from %s, to stage %d; option words %s, preview %s, difficulty %s\n",
            s_apply.slot, ico_host_main_ticks(), on_title ? "the title" : "play (as End Game)", to,
            s_apply.have_screen ? "the state's"
            : s_apply.have_girl ? "the state's (Players only)"
                                : "this run's",
            s_apply.have_preview ? "the state's" : "this run's",
            rec ? "the state's record" : "built from its keys");
    if (s_apply.done != NULL) {
        s_apply.done(1, to, "");
    }
}

void ico_ckpt_live_arm(const IcoCkptApply *a)
{
    if (!s_hooked) {
        ico_host_set_main_tick_hook(tick);
        s_hooked = 1;
    }
    memcpy(&s_apply, a, sizeof s_apply);
    s_armed = 1;
}

int ico_ckpt_live_pending(void)
{
    return s_armed;
}

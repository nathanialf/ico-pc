/*
 * port/render/rd_glow_hold.c
 *
 * The glow held between ticks (docs/port/WORK_BUFFERS.md, "The glow
 * between ticks"; DIVERGENCES.md VD-25).  Above Frame rate Original every
 * present of a tick replays the whole frame, and list 7's glow (the flare
 * and the bloom, staticBlur.c makeFullScreenFlareBefore/After and
 * pasteFullScreenFlare) is a chain of about 31 passes: the mask SCENE ->
 * WORK2 (with fillWork2's band WORK2_PAD and the sun's fans into WORK2),
 * the reduction WORK2 -> WORK0, the ten blur pairs WORK0 <-> WORK1, the eye
 * blur WORK0 -> WORK3 with the sun's ghosts, shrink and tint (WORK3 <->
 * WORK1), the WORK0 alpha fill, then the paste WORK3 -> SCENE.
 *
 * A tick's first present draws the chain as ever and copies WORK3 into
 * WORK3_HELD right after the chain's last write of it.  A later present of
 * the same tick turns the chain's draws (the FLARE, BLOOM and EYE_BLUR
 * sprites and the screen draws into WORK0, WORK1, WORK2, WORK2_PAD or
 * WORK3 in the chain's list) into RDC_NOP and copies WORK3_HELD back into
 * WORK3 in the last one's place, so the paste reads the tick's glow.  The
 * flare's own draws into SCENE (cleanUpFB), the depth of field, the motion
 * blur and everything else still run.  The later present's glow is then
 * the tick's: identical on a still tick (the chain is a function of the
 * frame), half a tick behind the scene during a pan, as the motion blur's
 * old frame and the aura's feedback already are (rd_interp.c feedback).
 *
 * Kept exact: a later present draws the chain after all when its frame's
 * other draws read one of the chain's buffers before drawing into it
 * (RD_GLOW_READ: the sun's glare onto SCENE from WORK1, eyeBlur mode 1),
 * when this tick's copy is missing or its targets were made again
 * (RD_GLOW_NO_COPY), and under [dev] glow_redraw = true
 * (ICO_RD_GLOW_REDRAW=1, RD_GLOW_DEV), the A/B, which also leaves the
 * first present without its copy, so that run draws as before.  The
 * depth of field's copies leave column 0 of WORK0 and WORK1 undrawn
 * (WORK_BUFFERS.md); a held present's depth of field reads the chain's
 * column 0 of the tick's first present there, part of the held glow.
 *
 * The still re-present (rd_reshow.c) hashes the frame before this edit,
 * as before feedback's head copies: two presents of a tick whose frames
 * hash alike draw the same glow either way (both held, or the first
 * present's chain and the copy of its result), so showing the rendered
 * one again stays exact.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"

static bool redrawOn(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("ICO_RD_GLOW_REDRAW");
        on = e && e[0] == '1';
    }
    return on != 0;
}

/* the chain's buffers */
enum { B_WORK0, B_WORK1, B_WORK2, B_PAD, B_WORK3, B_COUNT };

static void bufferIds(uint32_t id[B_COUNT])
{
    id[B_WORK0] = rd_target(RD_TARGET_WORK0).id;
    id[B_WORK1] = rd_target(RD_TARGET_WORK1).id;
    id[B_WORK2] = rd_target(RD_TARGET_WORK2).id;
    id[B_PAD] = rd_target(RD_TARGET_WORK2_PAD).id;
    id[B_WORK3] = rd_target(RD_TARGET_WORK3).id;
}

/* the bit of the chain's buffer c draws into when c is one of the chain's
 * draws (a FLARE, BLOOM or EYE_BLUR sprite; with screens, also a screen
 * draw: the sun's fans), else 0 */
static unsigned chainBit(const RdCmd *c, const RdStateBlock *st, const uint32_t id[B_COUNT],
                         bool screens)
{
    if (c->type == RDC_POST_STUB) {
        if (c->b[0] != RD_POST_FLARE && c->b[0] != RD_POST_BLOOM && c->b[0] != RD_POST_EYE_BLUR) {
            return 0;
        }
    } else if (c->type != RDC_SCREEN || !screens) {
        return 0;
    }
    for (int b = 0; b < B_COUNT; b++) {
        if (id[b] && st->color == id[b]) {
            return 1u << b;
        }
    }
    return 0;
}

static bool texOf(uint32_t id, uint32_t v[3])
{
    const RdTargetRec *t = rd__target_rec(id);
    if (!t || !t->color.id) {
        return false;
    }
    v[0] = t->color.id;
    v[1] = t->tw;
    v[2] = t->th;
    return true;
}

/* this tick's copy: the frame, and WORK3_HELD's and WORK3's textures */
static struct {
    bool valid;
    uint32_t number, held[3], work[3];
} s_copy;

static int s_last = RD_GLOW_NONE;
static uint64_t s_why[RD_GLOW_COUNT];

static int outcome(int why)
{
    s_last = why;
    s_why[why]++;
    if (why == RD_GLOW_HELD) {
        rd__perf_glow_held();
    }
    return why;
}

int rd__glow_hold(RdFrame *f, int firstOfTick)
{
    uint32_t id[B_COUNT];
    bufferIds(id);
    const uint32_t held = rd_target(RD_TARGET_WORK3_HELD).id;
    const int head = rd__first_list((int)f->keep);
    /* the chain's list and its last sprite into WORK3 (the eye blur's, or
     * its sun's tint) */
    int list = -1;
    uint32_t last = 0;
    RdStateBlock st = f->startState;
    for (int l = head; l < RD_LIST_COUNT; l++) {
        for (uint32_t i = 0; i < f->lists[l].count; i++) {
            const RdCmd *c = &f->lists[l].cmds[i];
            if (rd__apply_state(&st, c)) {
                continue;
            }
            if (c->type == RDC_POST_STUB && (chainBit(c, &st, id, false) & (1u << B_WORK3))) {
                list = l;
                last = i;
            }
        }
    }
    uint32_t heldTex[3], workTex[3];
    const bool targets = held && texOf(held, heldTex) && texOf(id[B_WORK3], workTex) &&
                         heldTex[1] == workTex[1] && heldTex[2] == workTex[2];
    if (firstOfTick) {
        s_copy.valid = false;
        if (list >= 0 && targets && !redrawOn() &&
            rd__interp_copy_at(list, last + 1, id[B_WORK3], held, id[B_WORK3])) {
            s_copy.valid = true;
            s_copy.number = f->number;
            memcpy(s_copy.held, heldTex, sizeof(heldTex));
            memcpy(s_copy.work, workTex, sizeof(workTex));
        }
        return outcome(list >= 0 ? RD_GLOW_FIRST : RD_GLOW_NONE);
    }
    if (list < 0) {
        return outcome(RD_GLOW_NONE);
    }
    if (redrawOn()) {
        return outcome(RD_GLOW_DEV);
    }
    if (!s_copy.valid || s_copy.number != f->number || !targets ||
        memcmp(s_copy.held, heldTex, sizeof(heldTex)) != 0 ||
        memcmp(s_copy.work, workTex, sizeof(workTex)) != 0) {
        return outcome(RD_GLOW_NO_COPY);
    }
    /* what the frame reads of the buffers once the chain is gone: a buffer
     * the chain drew is stale until another draw writes it (WORK3 from the
     * copy back, after the chain's last write).  Any write counts as
     * covering the buffer: this scan cannot decide a draw's covered texels
     * or the texels a sampling draw reads (both follow from the replay's
     * transforms and UVs), so a partial write followed by a wider read
     * reads, outside the write, what earlier presents left there: the
     * tick's chain on a still tick (the same bytes as a redraw), the tick's
     * first present's during a pan, as the held glow itself.  The one such
     * case known is the depth of field's column 0 of WORK0 and WORK1
     * (WORK_BUFFERS.md, VD-25); an exact rule would redraw the chain at
     * every present with the depth of field */
    unsigned stale = 0;
    st = f->startState;
    for (int l = head; l < RD_LIST_COUNT; l++) {
        for (uint32_t i = 0; i < f->lists[l].count; i++) {
            const RdCmd *c = &f->lists[l].cmds[i];
            if (rd__apply_state(&st, c)) {
                continue;
            }
            const unsigned bit = l == list ? chainBit(c, &st, id, true) : 0u;
            if (bit) {
                stale |= bit;
                if (i == last) {
                    stale &= ~(1u << B_WORK3);
                }
                continue;
            }
            for (int b = 0; b < B_COUNT; b++) {
                if ((stale & (1u << b)) && rd__interp_reads(c, &st, id[b])) {
                    return outcome(RD_GLOW_READ);
                }
            }
            for (int b = 0; b < B_COUNT; b++) {
                if (rd__interp_writes(c, &st, id[b])) {
                    stale &= ~(1u << b);
                }
            }
        }
    }
    if (!rd__interp_copy_at(list, last + 1, held, id[B_WORK3], id[B_WORK3])) {
        return outcome(RD_GLOW_NO_COPY);
    }
    st = f->startState;
    for (int l = head; l <= list; l++) {
        for (uint32_t i = 0; i < f->lists[l].count; i++) {
            RdCmd *c = &f->lists[l].cmds[i];
            if (rd__apply_state(&st, c)) {
                continue;
            }
            if (l == list && chainBit(c, &st, id, true)) {
                c->type = RDC_NOP; /* the copy back at last + 1 is an RDC_COPY: kept */
            }
        }
    }
    return outcome(RD_GLOW_HELD);
}

const char *rd__glow_hold_text(int why)
{
    switch (why) {
    case RD_GLOW_HELD:
        return "glow held";
    case RD_GLOW_FIRST:
        return "glow drawn (the tick's first present)";
    case RD_GLOW_DEV:
        return "glow redrawn ([dev] glow_redraw)";
    case RD_GLOW_NO_COPY:
        return "glow redrawn (no copy of this tick's)";
    case RD_GLOW_READ:
        return "glow redrawn (another draw reads the chain's buffers)";
    default:
        return "no glow";
    }
}

int rd__glow_hold_last(void)
{
    return s_last;
}

uint64_t rd__glow_hold_count(int why)
{
    return why >= 0 && why < RD_GLOW_COUNT ? s_why[why] : 0;
}

void rd__glow_hold_block(void)
{
    const uint64_t *w = s_why;
    if (w[RD_GLOW_HELD] + w[RD_GLOW_FIRST] + w[RD_GLOW_DEV] + w[RD_GLOW_NO_COPY] +
            w[RD_GLOW_READ] ==
        0) {
        memset(s_why, 0, sizeof(s_why));
        return; /* no glow drawn in the block */
    }
    rd__log("interp: glow between ticks: %s: %llu later presents pasted the tick's glow without "
            "its chain; the chain drawn at %llu first presents, %llu later ones with [dev] "
            "glow_redraw, %llu without this tick's copy, %llu whose other draws read its "
            "buffers; %llu presents without a glow",
            redrawOn() ? "redrawn ([dev] glow_redraw = true)" : "held",
            (unsigned long long)w[RD_GLOW_HELD], (unsigned long long)w[RD_GLOW_FIRST],
            (unsigned long long)w[RD_GLOW_DEV], (unsigned long long)w[RD_GLOW_NO_COPY],
            (unsigned long long)w[RD_GLOW_READ], (unsigned long long)w[RD_GLOW_NONE]);
    memset(s_why, 0, sizeof(s_why));
}

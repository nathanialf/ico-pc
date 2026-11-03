/* rd_shadow_merge.c: the shadow volumes drawn in the scene's open pass
 * (docs/port/WORK_BUFFERS.md, "The shadow volumes in the scene's pass";
 * RdProfile.shadowMerge, on on the tile-based classes; ICO_RD_SHADOW_MERGE=0|1
 * is the tests' way to draw them as before or to force them in, never a
 * player's or a config key).
 *
 * Shadow.c draws its volumes (list 3) into the shadow count target with
 * SCENE's depth: a reset (the stencil cleared), the volumes (INCR/DECR of
 * the stencil's bits 0-5 under the GEQUAL test against the scene's depth,
 * no colour written: rd__shadow_volume_key's colour mask is 0), then the
 * resolve that reads the stencil.  The replay ended the scene's pass at the
 * reset (rd_replay.c doShadowReset) and began one on the count target with
 * SCENE's depth for the volumes: on the PowerVR phone at 2366 x 1080 that
 * pass loaded SCENE's depth and stored it again with the stencil (about
 * 23 MB a replay, "list 3 temporary 2366x1080 8 draws 1.18 ms" in the phone's
 * log of 2026-11-02, after list 0's pass had stored the same depth).
 *
 * Here the volumes are drawn in the scene's pass that is open at the reset
 * instead, when that pass began with the stencil cleared to 0 (the frame's
 * clear of the depth taken as its load op) and nothing pending ends it.
 * Exact by construction:
 *   - the stencil: the reset clears it to 0.  The open pass cleared it to 0
 *     when it began, and nothing in it wrote bits 0-5 since: the only
 *     pipelines that write them are the volumes' (rd_pipeline.c
 *     shadowStencil, write mask RD_SHADOW_STENCIL_MASK), and a reset window
 *     on this depth before the reset would have ended the pass (its own
 *     resolve ends every pass); DATE's set and clear write bit 6 alone
 *     (RD_DATE_STENCIL_BIT), which the count never reads (the resolve's
 *     tests and shadow_count_ps mask bits 0-5, and INCR_WRAP / DECR_WRAP
 *     under the write mask 0x3F give the same bits 0-5 whatever bit 6
 *     holds).  So the volumes find the stencil the reset would have left;
 *   - the depth: the same values (the pass holds them in its tiles; the old
 *     way stored them and loaded them back, bit for bit), tested the same
 *     way (the same pipeline but for its colour attachment's target, of the
 *     same format, which the volumes never write);
 *   - the colour: SCENE's is never written (mask 0); the count target's is
 *     not touched, and nothing read what the old pass did to it (the store
 *     plan left it undefined already, rd_store_plan.c
 *     rd__store_plan_count_color; the one draw writes every texel at scale
 *     1, the seven draws clear it, above 1x the reduced count stands for
 *     it);
 *   - the pass then stores its stencil (armed when it began, below), and
 *     the resolve ends it before it reads the stencil as before.
 * The draws are the same commands in the same order: the picture is the
 * same bytes (rd_shadow_merge_ab: every pass dump of the dumps with it on
 * and off).
 *
 * The arm: a pass begun on a target with its own depth (rd_replay.c
 * beginPass, after the pass area) whose stencil is cleared to 0, with a
 * shadow reset on that depth later in the replay
 * (the store plan's scan notes the resets, rd__shadow_merge_scan_reset),
 * single-sampled, without an area, outside a pass window, the exact blend,
 * GS state as shader data and the VU pass split: its stencil is stored
 * (2.6 MB at the phone's size, where list 0's pass stored none).  The
 * take (doShadowReset): the reset the arm saw, the armed pass still open,
 * no clear or reset pending, the count target of the pass's size, scale
 * and format.  A strip under DATE or sampling a target, or a pass ended
 * in between, goes back to the volumes' own pass: its stencil is loaded
 * (the window is open), what the armed pass stored.  A pass armed for a
 * reset it did not reach stored its stencil for nothing (counted).
 *
 * The count target's new-target clear (rd__shadow_count_clear, from
 * rd_upload.c rd__clear_new_targets): the count target is a pooled
 * temporary taken anew each frame, cleared before the walk (9.7 MB written
 * at the phone's size).  The scan notes, for every temporary a shadow
 * command draws on, whether its first touch in the replay is a reset and
 * whether anything else touches it before its first resolve
 * (rd__shadow_merge_scan_cmd).  Where its first is a reset, nothing else
 * comes before the resolve and the resolve comes, nothing reads the clear:
 * the volumes never write its colour, the seven draws clear it at their
 * load, the one draw at scale 1 writes every texel and above 1x the reduced
 * count stands for it after the resolve.  So it is left undrawn (under
 * ICO_RD_STORE_POISON=1 cleared to 0x5A instead, the tests' garbage).
 *
 * The lines:
 *   rd: shadow volumes: drawn in the scene's open pass (...)|in a pass of
 *       their own (...)                                   (start)
 *   rd: shadow volumes: drawn in SCENE's pass in list L (frame F): ...
 *       (reported once)
 *   rd: shadow volumes: in a pass of their own in frame F: <why> (the first
 *       8, then every 100th: N so far)
 *   rd: shadow volumes: the count target's clear as a new target (WxH,
 *       N MB) not drawn: ... (reported once)
 *   window: ...; shadow volumes in the scene's pass: M of R counts, A
 *       passes armed, C count clears not drawn a replay   (the 10 s line)
 *   the replay tool: --stats "shadow volumes in the scene's pass on|off:
 *       M of R counts, A passes armed; C count targets' clears not drawn" */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_replay.h"

#define MERGE_RESETS 8

static struct {
    int forced; /* tests: -1 none, else 0 or 1 */
    bool on;
    /* the scan: the shadow resets of the replay replayed (by ordinal) */
    uint32_t resetOrd[MERGE_RESETS], resetColor[MERGE_RESETS], resetDepth[MERGE_RESETS];
    uint32_t nReset, replay;
    bool valid;
    /* the pass armed: its serial (rd_replay.c Replay.passSerial), targets,
     * the reset it is armed for */
    bool armed;
    uint32_t armSerial, armColor, armDepth, armOrd, armReplay;
    /* the take: the volumes go into the armed pass while it is open */
    bool active;
    uint32_t activeColor; /* the count target the strips name */
    /* the lines */
    bool saidMerged;
    uint32_t missed;
    /* the count targets' new-target clear (the scan): each target a shadow
     * command draws on, whether its first touch in the replay was a reset,
     * whether anything else touched it before its first resolve, and
     * whether it has one */
    uint32_t cntId[MERGE_RESETS];
    uint8_t cntFirstReset[MERGE_RESETS], cntTouched[MERGE_RESETS], cntResolved[MERGE_RESETS];
    uint32_t nCnt;
    bool cntFull, saidClear;
} s_sm = {.forced = -1};

static bool decide(char *why, size_t n)
{
    if (s_sm.forced >= 0) {
        snprintf(why, n, "the tests' switch");
        return s_sm.forced != 0;
    }
    const char *e = getenv("ICO_RD_SHADOW_MERGE");
    if (e && (strcmp(e, "0") == 0 || strcmp(e, "1") == 0)) {
        snprintf(why, n, "ICO_RD_SHADOW_MERGE=%s, the tests' way", e);
        return e[0] == '1';
    }
    if (g_rd.profile.shadowMerge) {
        snprintf(why, n,
                 "a tile-based GPU: the volumes' own pass loaded and stored the scene's "
                 "depth");
        return true;
    }
    snprintf(why, n, "an immediate-mode GPU: the volumes' pass moves nothing through tiles");
    return false;
}

void rd__shadow_merge_init(void)
{
    char why[160];
    s_sm.on = decide(why, sizeof why);
    s_sm.valid = s_sm.armed = s_sm.active = false;
    s_sm.saidMerged = false;
    s_sm.missed = 0;
    rd__log("shadow volumes: %s (%s)",
            s_sm.on ? "drawn in the scene's open pass where it cleared the stencil (no pass of "
                      "their own, the scene's depth not stored and loaded again for them; the "
                      "same picture)"
                    : "in a pass of their own",
            why);
}

bool rd__shadow_merge_on(void)
{
    return s_sm.on;
}

void rd__shadow_merge_force(int mode)
{
    s_sm.forced = mode < 0 ? -1 : (mode ? 1 : 0);
    char why[160];
    s_sm.on = decide(why, sizeof why);
}

/* ------------------------------------------------------------- the scan */

void rd__shadow_merge_scan_begin(void)
{
    s_sm.nReset = 0;
    s_sm.valid = false;
    s_sm.nCnt = 0;
    s_sm.cntFull = false;
}

/* the count clear's entry of target id (added when add), -1 none */
static int cntSlot(uint32_t id, bool add)
{
    for (uint32_t i = 0; i < s_sm.nCnt; i++) {
        if (s_sm.cntId[i] == id) {
            return (int)i;
        }
    }
    if (!add || !id) {
        return -1;
    }
    if (s_sm.nCnt >= MERGE_RESETS) {
        s_sm.cntFull = true; /* more targets than the scan holds: no clear left out */
        return -1;
    }
    const uint32_t i = s_sm.nCnt++;
    s_sm.cntId[i] = id;
    s_sm.cntFirstReset[i] = s_sm.cntTouched[i] = s_sm.cntResolved[i] = 0;
    return (int)i;
}

/* target id touched by a command that is not one of its shadow window's */
static void cntTouch(uint32_t id)
{
    const RdTargetRec *t = rd__target_rec(id);
    if (!t || t->named) {
        return; /* a named target is never a count target here */
    }
    const int i = cntSlot(id, true);
    if (i >= 0 && !s_sm.cntResolved[i]) {
        s_sm.cntTouched[i] = 1;
    }
}

void rd__shadow_merge_scan_cmd(const RdCmd *c, const RdStateBlock *st)
{
    switch (c->type) {
    case RDC_NOP:
    case RDC_OVERLAY_TEXT:
        return;
    case RDC_SHADOW_RESET:
    case RDC_SHADOW_STRIP:
    case RDC_SHADOW_RESOLVE: {
        const int i = cntSlot(st->color, true);
        if (i < 0) {
            return;
        }
        if (c->type == RDC_SHADOW_RESET && !s_sm.cntResolved[i] && !s_sm.cntTouched[i] &&
            !s_sm.cntFirstReset[i]) {
            s_sm.cntFirstReset[i] = 1;
        } else if (!s_sm.cntFirstReset[i]) {
            s_sm.cntTouched[i] = 1; /* a strip or a resolve before any reset */
        }
        if (c->type == RDC_SHADOW_RESOLVE) {
            s_sm.cntResolved[i] = 1;
        }
        return;
    }
    case RDC_CLEAR:
        cntTouch(c->u[0]);
        return;
    case RDC_COPY:
    case RDC_EXACT_BLEND:
        cntTouch(c->u[0]);
        cntTouch(c->u[1]);
        return;
    default:
        break;
    }
    /* a draw, a post sprite, anything else: its targets and its texture */
    cntTouch(st->color);
    cntTouch(st->depth);
    const RdTexRec *t = rd__tex_rec(st->tex);
    if (t && t->kind == RD_TEXKIND_TARGET) {
        cntTouch(t->target);
    }
}

int rd__shadow_count_clear(const RdTargetRec *t)
{
    if (!s_sm.on || !s_sm.valid || s_sm.replay != g_rd.replayCounter || s_sm.cntFull || !t ||
        t->named || t->withDepth) {
        return 0;
    }
    for (uint32_t i = 0; i < s_sm.nCnt; i++) {
        if (rd__target_rec(s_sm.cntId[i]) != t) {
            continue;
        }
        if (!s_sm.cntFirstReset[i] || s_sm.cntTouched[i] || !s_sm.cntResolved[i]) {
            return 0;
        }
        g_rdPerf.shadowClearSkipped++;
        if (!s_sm.saidClear) {
            s_sm.saidClear = true;
            rd__log("shadow volumes: the count target's clear as a new target (%ux%u, %.1f MB) "
                    "not drawn: nothing reads it before its resolve, which writes or clears it "
                    "or stands the reduced count for it (reported once)",
                    t->tw, t->th, (double)t->tw * t->th * 4.0 / 1048576.0);
        }
        /* the tests' garbage where it is left undrawn (ICO_RD_STORE_POISON=1:
         * lavapipe keeps memory as it was): a reader would show 0x5A */
        const char *poison = getenv("ICO_RD_STORE_POISON");
        return poison && strcmp(poison, "1") == 0 ? 2 : 1;
    }
    return 0;
}

void rd__shadow_merge_scan_reset(const RdStateBlock *st, uint32_t ord)
{
    if (s_sm.nReset >= MERGE_RESETS) {
        return; /* the later ones keep their own pass */
    }
    s_sm.resetOrd[s_sm.nReset] = ord;
    s_sm.resetColor[s_sm.nReset] = st->color;
    s_sm.resetDepth[s_sm.nReset] = st->depth;
    s_sm.nReset++;
}

void rd__shadow_merge_scan_end(void)
{
    s_sm.replay = g_rd.replayCounter;
    s_sm.valid = true;
    s_sm.armed = s_sm.active = false;
}

/* ------------------------------------------------------------ the walk */

static void missed(const char *why)
{
    s_sm.missed++;
    if (s_sm.missed <= 8u || s_sm.missed % 100u == 0u) {
        rd__log("shadow volumes: in a pass of their own in frame %u: %s (the first 8, then "
                "every 100th: %u so far)",
                g_rdPerf.frame, why, s_sm.missed);
    }
}

void rd__shadow_merge_arm(RhiRenderPassDesc *p, uint32_t cid, uint32_t did, uint32_t serial,
                          bool ms, bool storage)
{
    if (s_sm.armed && s_sm.armReplay == g_rd.replayCounter) {
        /* a pass begun after the armed one: the armed one ended before its
         * reset (it stored its stencil for nothing) */
        s_sm.armed = false;
        missed("the scene's pass ended before the reset");
    }
    s_sm.armed = false;
    if (!s_sm.on || !s_sm.valid || s_sm.replay != g_rd.replayCounter || ms || storage || !did ||
        p->colorCount != 1 || !p->depth.texture.id || p->area.w != 0 || g_rdWin.first ||
        rd__replay_msaa_want() > 1u || g_rd.dstMode != RD_BLEND_EXACT_OFF || rd__uber_on() ||
        g_rd.passSplit) {
        return;
    }
    if (p->depth.stencilLoad != RHI_LOAD_CLEAR || p->depth.clearStencil != 0) {
        return; /* not a pass that cleared the stencil (to 0) */
    }
    const uint32_t k = g_rdStoreOrd;
    for (uint32_t i = 0; i < s_sm.nReset; i++) {
        if (s_sm.resetOrd[i] <= k) {
            continue;
        }
        if (s_sm.resetDepth[i] != did) {
            continue;
        }
        p->depth.stencilStore = RHI_STORE_STORE;
        s_sm.armed = true;
        s_sm.armSerial = serial;
        s_sm.armColor = cid;
        s_sm.armDepth = did;
        s_sm.armOrd = s_sm.resetOrd[i];
        s_sm.armReplay = g_rd.replayCounter;
        g_rdPerf.shadowMergeArmed++;
        return;
    }
}

/* the count target c drawn into the pass's target s: the same texels, scale
 * and format (FrameCB, the scissor and the pipeline key are those of s) */
static bool sameGeometry(const RdTargetRec *c, const RdTargetRec *s)
{
    return c && s && c->color.id && s->color.id && c->w == s->w && c->h == s->h && c->tw == s->tw &&
           c->th == s->th && c->sx == s->sx && c->sy == s->sy && c->ox == s->ox &&
           c->format == s->format;
}

bool rd__shadow_merge_take(bool open, uint32_t serial, uint32_t passColor, uint32_t passDepth,
                           const RdStateBlock *st, const char *blocked)
{
    s_sm.active = false;
    if (!s_sm.armed || s_sm.armReplay != g_rd.replayCounter) {
        return false;
    }
    s_sm.armed = false;
    const char *why = NULL;
    if (g_rdStoreOrd != s_sm.armOrd) {
        why = "another reset than the one the pass was armed for";
    } else if (!open || serial != s_sm.armSerial || passColor != s_sm.armColor ||
               passDepth != s_sm.armDepth) {
        why = "the scene's pass ended before the reset";
    } else if (st->depth != s_sm.armDepth) {
        why = "the reset is on another depth";
    } else if (blocked) {
        why = blocked;
    } else if (!sameGeometry(rd__target_rec(st->color), rd__target_rec(passColor))) {
        why = "the count target's size, scale or format is not the scene's";
    }
    if (why) {
        missed(why);
        return false;
    }
    s_sm.active = true;
    s_sm.activeColor = st->color;
    g_rdPerf.shadowMerged++;
    if (!s_sm.saidMerged) {
        s_sm.saidMerged = true;
        const RdTargetRec *t = rd__target_rec(passColor);
        rd__log("shadow volumes: drawn in %s's pass (frame %u, command %u): the stencil it "
                "cleared stands for the reset's, no pass of their own (%ux%u: the depth not "
                "stored and loaded again for them); the pass stores its stencil for the "
                "count (reported once)",
                rd__target_name(passColor), g_rdPerf.frame, g_rdStoreOrd, t ? t->tw : 0u,
                t ? t->th : 0u);
    }
    return true;
}

uint32_t rd__shadow_merge_color(bool open, uint32_t serial, const RdStateBlock *st)
{
    if (!s_sm.active) {
        return 0;
    }
    if (!open || serial != s_sm.armSerial || st->color != s_sm.activeColor ||
        st->depth != s_sm.armDepth) {
        /* the pass ended (its stencil stored) or the strips moved on: the
         * volumes' own pass from here, its stencil loaded */
        s_sm.active = false;
        missed("the scene's pass ended among the volumes");
        return 0;
    }
    const RdTexRec *t = st->ds.texEnabled ? rd__tex_rec(st->tex) : NULL;
    if (st->ds.test.date != RD_DATE_OFF || (t && t->kind == RD_TEXKIND_TARGET)) {
        s_sm.active = false;
        missed("a volume under DATE or sampling a target");
        return 0;
    }
    return s_sm.armColor;
}

/* rd_date_one.c: one DATE set-up for the shadow composites (rd_date_one.h;
 * docs/port/RENDER_POST.md "One DATE set-up for the shadow composites";
 * ICO_RD_DATE_ONE=0|1|force for the tests: no setting).
 *
 * The scan (rd__date_one_scan_cmd, from rd_shadow_box.c's scan in the store
 * plan's one walk, which hands it the box scan's composites with their
 * level's largest alpha and their own vertices' largest) marks each DATE
 * composite that follows another on its target with only state between,
 * every composite of the group before it keeping the alpha's top bit
 * (rd_date_one.h rd_date_one_step, rd_date_one_keeps).  The walk: the DATE
 * snapshot (rd_date_cut.c rd__date_snapshot) and the stencil bit's set-up
 * (rd_replay_tiler.c rd__replay_date_take) ask rd__date_one_held which
 * write serial the held set-up stands for; for a marked composite whose
 * predecessor took its set-up this replay (on the same target, by the same
 * path, the stencil's in the same pass), with no write since but that
 * predecessor's own draw, the answer is the current serial, so
 * rd__date_retake reuses it where its area holds the draw's (an area not
 * held still takes the whole target again, as today).  Everything else
 * keeps today's set-up: MSAA or the exact blend in force, a level or
 * composite replayed with a brighter vertex than the scan read (the keep
 * frame's held vertices, rd_layer.c; checked in rd_shadow_box.c), a
 * predecessor skipped or not drawn.
 *
 * Why the picture is the same: a set-up holds the alpha's top bit of every
 * texel of its area as of its serial; a composite that keeps the bit leaves
 * it as it was at every texel (it draws only where the bit is 0 and writes
 * an alpha under 0x80 there, or no alpha at all), so a set-up taken again
 * after it would hold the same bits; the draws keep their keys, passes and
 * order.  The one change in what is drawn: on the snapshot path at an
 * integer scale the DATE cut's runs (rd_date_cut.c) no longer end at the
 * shared composites, so composites 1 and 2 are drawn in the pass with the
 * depth that composite 3 and list 4 use, by the screen family's key with
 * the depth, in the start-up set beside its no-depth twin
 * (rd__enumerate_reachable_screen), with the same bytes (the two twins drew
 * the same bytes at integer scales on 18 dumps: v0.11.5's DATECUT).
 *
 * What it saves: on the snapshot path (Arm, Qualcomm's driver, the PowerVR
 * B class without the in-pass read, D3D11, OpenGL) two R8 passes and two
 * breaks of SCENE's pass a picture with volumes; on the stencil path two of
 * the three set-ups (each a clear and a set draw over the area and two
 * by-region barriers).  No new kind of GPU work: fewer passes and draws of
 * the kinds the device draws already, existing pipelines: no self-test, on
 * on every device and backend (RD_DATE_ONE_DEFAULT 1).
 *
 * The lines:
 *   rd: DATE one set-up: on|off (...)                 (once a renderer)
 *   rd: DATE one set-up: the first shared ... (reported once)
 *   rd: DATE one set-up: a composite could write alpha X ...: retaken as
 *       before (the first 8, then every 100th)
 *   window: per replay ...; DATE one set-up: S composites a replay shared
 *       the set-up before them, K kept their own
 *   the replay tool's --stats: "DATE one set-up on|off|force: S shared (T
 *       texels), K kept their own" (rd__no_volume_stats's line) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_date_one.h"
#include "rd_replay.h"

/* on on every device (no new kind of GPU work: fewer passes and draws) */
#define RD_DATE_ONE_DEFAULT 1

#define D1_SHARES 16

static int s_force = -1; /* rd__date_one_force: -1 the switch's own */
static int s_env = -1;   /* ICO_RD_DATE_ONE: -1 not read yet */

void rd__date_one_force(int mode)
{
    s_force = mode < 0 ? -1 : mode > 2 ? 2 : mode;
}

int rd__date_one_mode(void)
{
    if (s_force >= 0) {
        return s_force;
    }
    if (s_env < 0) {
        const char *e = getenv("ICO_RD_DATE_ONE");
        s_env = RD_DATE_ONE_DEFAULT;
        if (e && strcmp(e, "0") == 0) {
            s_env = 0;
        } else if (e && strcmp(e, "1") == 0) {
            s_env = 1;
        } else if (e && strcmp(e, "force") == 0) {
            s_env = 2;
        } else if (e) {
            rd__log("DATE one set-up: ICO_RD_DATE_ONE=%.16s is not 0, 1 or force: the default", e);
        }
    }
    return s_env;
}

/* ------------------------------------------------------------- the scan */

static struct {
    RdDateOneScan scan;
    uint32_t replay; /* the replay scanned (g_rd.replayCounter) */
    uint32_t frame;
    int mode;
    bool setting; /* MSAA or the exact blend in force: nothing marked */
    unsigned n;

    struct {
        uint32_t ord, prev;
        uint8_t prevAlpha; /* the predecessor writes alpha: its draw moves the serial once */
    } share[D1_SHARES];

    /* the group's last DATE composite: the refusal line's numbers, the next
     * share's predecessor */
    uint32_t lastLevel, lastVertex, lastAmax;
    uint8_t lastAlpha;
    uint32_t refusals;
} s_scan;

void rd__date_one_scan_begin(const RdFrame *f)
{
    memset(&s_scan.scan, 0, sizeof(s_scan.scan));
    s_scan.replay = g_rd.replayCounter;
    s_scan.frame = f ? f->number : 0;
    s_scan.mode = rd__date_one_mode();
    /* under MSAA or the exact blend nothing is shared (rd__date_one_held
     * refuses them at the walk too): no composite marked, so the DATE cut's
     * runs end at every composite as before */
    s_scan.setting = rd_aa_samples(rd__aa_mode()) > 1u || g_rd.dstMode != RD_BLEND_EXACT_OFF;
    s_scan.n = 0;
    s_scan.lastLevel = s_scan.lastVertex = s_scan.lastAmax = 0;
    s_scan.lastAlpha = 0;
    static int s_said = -1;
    if (s_said != s_scan.mode) {
        s_said = s_scan.mode;
        rd__log("DATE one set-up: %s",
                s_scan.mode == 0 ? "off (ICO_RD_DATE_ONE=0)"
                : s_scan.mode == 2
                    ? "on, the bound ignored (ICO_RD_DATE_ONE=force: the tests' sabotage)"
                    : "on: the shadow composites share one DATE snapshot or stencil set-up where "
                      "none can set the alpha's top bit (level alpha x vertex alpha >> 7 under "
                      "0x80, checked a picture from the chain's own vertices); the same picture");
    }
}

void rd__date_one_scan_cmd(const RdCmd *c, const RdStateBlock *st, bool state, uint32_t ord,
                           bool composite, uint32_t level, uint32_t vertex)
{
    if (s_scan.mode == 0 || s_scan.setting || s_scan.replay != g_rd.replayCounter) {
        return;
    }
    RdDateOneCmd k;
    memset(&k, 0, sizeof(k));
    k.target = st->color;
    uint32_t amax = 255;
    if (state || c->type == RDC_NOP || c->type == RDC_OVERLAY_TEXT) {
        k.kind = RD_D1_STATE;
    } else if (composite && c->type == RDC_SCREEN) {
        const RdDrawState *d = &st->ds;
        k.kind = RD_D1_COMPOSITE;
        k.date = d->test.date != RD_DATE_OFF;
        amax = rd_date_one_alpha(level, vertex, d->texFn == RD_TEXFN_DECAL);
        k.keeps = rd_date_one_keeps(true, d->test.date, d->fba, rd__wrap_applies(st), d->colorMask,
                                    amax, s_scan.mode == 2);
    } else {
        k.kind = RD_D1_OTHER;
    }
    uint32_t prev = 0;
    const int did = rd_date_one_step(&s_scan.scan, &k, ord, &prev);
    if (did == RD_D1_SHARED && s_scan.n < D1_SHARES) {
        s_scan.share[s_scan.n].ord = ord;
        s_scan.share[s_scan.n].prev = prev;
        s_scan.share[s_scan.n].prevAlpha = s_scan.lastAlpha;
        s_scan.n++;
    } else if (did == RD_D1_REFUSED) {
        g_rdPerf.dateOneRefused++;
        s_scan.refusals++;
        if (s_scan.refusals <= 8u || s_scan.refusals % 100u == 0u) {
            rd__log("DATE one set-up: a composite could write alpha %u (level alpha up to %u x "
                    "vertex alpha %u, frame %u, command %u): the next one's set-up retaken as "
                    "before (%u so far, the first 8 then every 100th)",
                    s_scan.lastAmax, s_scan.lastLevel, s_scan.lastVertex, s_scan.frame, prev,
                    s_scan.refusals);
        }
    }
    if (k.kind == RD_D1_COMPOSITE && k.date) {
        s_scan.lastLevel = level;
        s_scan.lastVertex = vertex;
        s_scan.lastAmax = amax;
        s_scan.lastAlpha = (uint8_t)((st->ds.colorMask & 8u) != 0);
    }
}

static int shareIndex(uint32_t ord)
{
    if (s_scan.replay != g_rd.replayCounter) {
        return -1;
    }
    for (unsigned i = 0; i < s_scan.n; i++) {
        if (s_scan.share[i].ord == ord) {
            return (int)i;
        }
    }
    return -1;
}

bool rd__date_one_shared_at(uint32_t ord)
{
    return shareIndex(ord) >= 0;
}

/* ------------------------------------------------------------- the walk */

static struct {
    /* the last DATE set-up taken or reused this replay: its draw's ordinal,
     * target, path and the serial its bits stand for */
    bool valid, stencil;
    uint32_t replay, ord, target, serial;
    uint32_t bad;  /* the replay a vertex came out brighter than the scan's */
    bool pending;  /* rd__date_one_held answered the current serial */
    int pendingAt; /* its share */
    uint32_t refusals;
    bool saidFirst;
} s_walk;

void rd__date_one_replay_bad(void)
{
    s_walk.bad = g_rd.replayCounter;
}

uint32_t rd__date_one_held(uint32_t heldFor, uint32_t heldSerial, uint32_t tcId, uint32_t serial,
                           bool stencil)
{
    s_walk.pending = false;
    const int i = shareIndex(g_rdStoreOrd);
    if (i < 0) {
        return heldSerial;
    }
    const char *why = NULL;
    if (rd__replay_msaa_want() > 1u || g_rd.dstMode != RD_BLEND_EXACT_OFF) {
        why = "MSAA or the exact blend in force";
    } else if (s_walk.bad == g_rd.replayCounter) {
        why = "a level or composite replayed with a brighter vertex than the scan read";
    } else if (!s_walk.valid || s_walk.replay != g_rd.replayCounter ||
               s_walk.ord != s_scan.share[i].prev || s_walk.target != tcId ||
               s_walk.stencil != stencil) {
        why = "the composite before it took no set-up on this path";
    } else if (heldFor != tcId) {
        why = "the set-up not held any more (a pass ended between them)";
    } else if (serial - s_walk.serial > s_scan.share[i].prevAlpha) {
        why = "a write since the composite before it";
    }
    if (why) {
        g_rdPerf.dateOneRefused++;
        s_walk.refusals++;
        if (s_walk.refusals <= 8u || s_walk.refusals % 100u == 0u) {
            rd__log("DATE one set-up: command %u retakes its set-up as before: %s (%u so far, "
                    "the first 8 then every 100th)",
                    g_rdStoreOrd, why, s_walk.refusals);
        }
        return heldSerial;
    }
    s_walk.pending = true;
    s_walk.pendingAt = i;
    return serial;
}

void rd__date_one_took(uint32_t tcId, uint32_t serial, bool stencil, bool reused, uint64_t texels)
{
    if (s_walk.pending) {
        s_walk.pending = false;
        if (reused) {
            g_rdPerf.dateOneShared++;
            g_rdPerf.dateOneTexels += texels;
            if (!s_walk.saidFirst) {
                s_walk.saidFirst = true;
                rd__log("DATE one set-up: the first shared (frame %u, command %u with command "
                        "%u's %s): the alpha's top bit as it was; the same picture (reported "
                        "once)",
                        s_scan.frame, g_rdStoreOrd, s_scan.share[s_walk.pendingAt].prev,
                        stencil ? "stencil set-up" : "snapshot");
            }
        }
        /* else its area was not held: taken again whole, rd__date_retake's
         * own rule (counted as "an area not held") */
    }
    s_walk.valid = true;
    s_walk.stencil = stencil;
    s_walk.replay = g_rd.replayCounter;
    s_walk.ord = g_rdStoreOrd;
    s_walk.target = tcId;
    s_walk.serial = serial;
}

void rd__date_one_passed(uint32_t serial)
{
    const int i = shareIndex(g_rdStoreOrd);
    if (i < 0 || !s_walk.valid || s_walk.replay != g_rd.replayCounter ||
        s_walk.ord != s_scan.share[i].prev) {
        return; /* nothing held for it: the next one takes its own */
    }
    /* it draws nothing: the held set-up's bits are still the alpha's */
    s_walk.ord = g_rdStoreOrd;
    s_walk.serial = serial;
}

/* ------------------------------------------------------------ the lines */

static struct {
    uint64_t shared, refused;
    uint32_t replays;
} s_sum;

void rd__date_one_pop(const RdPerfRecord *rec)
{
    s_sum.shared += rec->dateOneShared;
    s_sum.refused += rec->dateOneRefused;
    s_sum.replays++;
}

bool rd__date_one_any(void)
{
    return s_sum.replays && (s_sum.shared || s_sum.refused);
}

void rd__date_one_text(char *out, size_t size, size_t *at)
{
    if (rd__date_one_any() && *at < size) {
        const double n = (double)s_sum.replays;
        const int w = snprintf(out + *at, size - *at,
                               "; DATE one set-up: %.1f composites a replay shared the set-up "
                               "before them, %.1f kept their own",
                               (double)s_sum.shared / n, (double)s_sum.refused / n);
        *at += w > 0 && (size_t)w < size - *at ? (size_t)w : 0;
    }
    memset(&s_sum, 0, sizeof(s_sum));
}

void rd__date_one_stats(const RdPerfRecord *rec, char *out, size_t size)
{
    const int m = rd__date_one_mode();
    snprintf(out, size, "DATE one set-up %s: %u shared (%llu texels), %u kept their own",
             m == 0   ? "off"
             : m == 2 ? "force"
                      : "on",
             rec->dateOneShared, (unsigned long long)rec->dateOneTexels, rec->dateOneRefused);
}

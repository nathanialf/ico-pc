/* rd_perf_hooks.c: the cost counters' increments (rd_perf_hooks.h).
 *
 * SCENE's pass ended mid-picture (rd__perf_hooks_pass, from rd__perf_pass:
 * every piece of GPU work begins there, outside any render pass).  A
 * render pass on SCENE begun after an earlier one in the same replay is a
 * break; its reason is the first piece between the two (rd_perf_more.h
 * rd_perf_break_of: a DATE snapshot, the shadow count or levels, the
 * aura, ...), AGAIN with none between.  Its bytes are what the break made
 * the tile memory move: the stores of SCENE's pass that ended (counted by
 * the RHI as that pass began: rhi_traffic.h, RhiStats.pass*StoreBytes
 * between its start and the next piece's) and the loads of the pass begun
 * again (pass*LoadBytes between it and the next piece, or the record's
 * end).  The RHI's counters are read only around SCENE's passes (a
 * snapshot copy of RhiStats, no call to the backend's device). */
#include "rd_perf_hooks.h"

#include <string.h>

#include "rd_internal.h"
#include "rd_perf_more.h"
#include "shader_consts.h" /* ICO_DF_NODUAL_* */

static struct {
    bool seen;        /* a SCENE pass began in this replay */
    bool prevScene;   /* the last piece began was a SCENE pass */
    int pending;      /* the first piece's why after the last SCENE pass, -1 none */
    int finishing;    /* a break counted: its pass's loads still to read, -1 none */
    uint64_t at[4];   /* the counters at the last SCENE pass's start (colour/depth load/store) */
    uint64_t storeKB; /* the stores of the last SCENE pass ended */
    bool lastFactor;  /* the two-pass blend: the last 2D pass filled was a colour pass */
} s_h;

static void counters(uint64_t out[4])
{
    RhiStats st;
    rhi_get_stats(&st);
    out[0] = st.passColorLoadBytes;
    out[1] = st.passDepthLoadBytes;
    out[2] = st.passColorStoreBytes;
    out[3] = st.passDepthStoreBytes;
}

void rd__perf_hooks_begin(void)
{
    memset(&s_h, 0, sizeof(s_h));
    s_h.pending = s_h.finishing = -1;
}

/* the SCENE pass begun at s_h.at: its loads (0) or stores (1), KB */
static uint32_t sinceKB(const uint64_t now[4], int stores)
{
    const int a = stores ? 2 : 0;
    const uint64_t b = (now[a] - s_h.at[a]) + (now[a + 1] - s_h.at[a + 1]);
    return (uint32_t)((b + 512u) / 1024u);
}

void rd__perf_hooks_pass(int kind, int target, int post)
{
    const bool scene = kind == RD_PERF_PASS_RENDER && target == RD_TARGET_SCENE;
    uint64_t now[4] = {0, 0, 0, 0};
    bool have = false;
    if (s_h.prevScene || s_h.finishing >= 0 || scene) {
        counters(now);
        have = true;
    }
    if (have && s_h.finishing >= 0) {
        g_rdPerf.breakKB[s_h.finishing] += sinceKB(now, 0); /* the pass begun again: its loads */
        s_h.finishing = -1;
    }
    if (have && s_h.prevScene) {
        s_h.storeKB = sinceKB(now, 1); /* the SCENE pass that ends here: its stores */
    }
    if (scene) {
        if (s_h.seen) {
            const int why = s_h.prevScene      ? RD_BREAK_AGAIN
                            : s_h.pending >= 0 ? s_h.pending
                                               : RD_BREAK_OTHER;
            if (g_rdPerf.breakN[why] < UINT16_MAX) {
                g_rdPerf.breakN[why]++;
            }
            g_rdPerf.breakKB[why] += (uint32_t)s_h.storeKB;
            s_h.finishing = why;
        }
        s_h.seen = true;
        s_h.pending = -1;
        memcpy(s_h.at, now, sizeof(s_h.at));
    } else if (s_h.seen && s_h.pending < 0) {
        s_h.pending = rd_perf_break_of(kind, target, post);
    }
    s_h.prevScene = scene;
}

void rd__perf_hooks_end(void)
{
    if (s_h.finishing >= 0) {
        uint64_t now[4] = {0, 0, 0, 0};
        counters(now);
        g_rdPerf.breakKB[s_h.finishing] += sinceKB(now, 0);
        s_h.finishing = -1;
    }
}

static bool vuFs(int fs)
{
    return fs == RD_FS_VU || fs == RD_FS_VU_TEXA || fs == RD_FS_VU_NP || fs == RD_FS_VU_TEXA_NP;
}

void rd__perf_nodual_pass(uint32_t flags, int fs)
{
    if (vuFs(fs)) {
        return; /* vuDraw's (rd__perf_vu_draw) */
    }
    if (flags & ICO_DF_NODUAL_FACTOR) {
        g_rdPerf.noDualColour++; /* one pass, until an alpha pass follows it */
        s_h.lastFactor = true;
        return;
    }
    if ((flags & ICO_DF_NODUAL_ALPHA_PASS) && s_h.lastFactor && g_rdPerf.noDualColour > 0u) {
        g_rdPerf.noDualColour--;
        g_rdPerf.noDualPairs++;
    }
    s_h.lastFactor = false;
}

void rd__perf_vu_draw(bool pair, bool colour, uint32_t tris)
{
    if (pair) {
        g_rdPerf.noDualPairs++;
        g_rdPerf.noDualTris += tris;
    } else if (colour) {
        g_rdPerf.noDualColour++;
    }
    const int l = rd__replay_list();
    if (l >= 0 && l < RD_LIST_COUNT) {
        g_rdPerf.vuListTris[l] += tris;
    }
}

void rd__perf_box_note(int list, const float (*m)[4], const float *lo, const float *hi,
                       bool scissor, double x0, double y0, double x1, double y1)
{
    if (list < 0 || list >= RD_LIST_COUNT) {
        return;
    }
    if (m == NULL) {
        if (g_rdPerf.listBoxNone[list] < UINT16_MAX) {
            g_rdPerf.listBoxNone[list]++;
        }
        return;
    }
    const double a = rd_perf_box_area(m, lo, hi, scissor, x0, y0, x1, y1);
    g_rdPerf.listBoxPx[list] += (uint64_t)(a + 0.5);
    if (g_rdPerf.listBoxDraws[list] < UINT16_MAX) {
        g_rdPerf.listBoxDraws[list]++;
    }
    const double t = (x1 - x0) * (y1 - y0);
    g_rdPerf.listBoxTargetPx[list] = t > 0.0 ? (uint32_t)(t + 0.5) : 0u;
}

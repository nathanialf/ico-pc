/* rd_window.c: the replay's pass window and the loss bisect's draw window
 * (rd_internal.h g_rdWin; docs/port/RENDER.md, "The loss bisect").
 *
 * rd__replay_set_window: the replay numbers its passes 1, 2, ... as they
 * begin (rd_replay.c passPush: the VU vertex and triangle pre-passes, 1 and
 * 2 of a replay that has both, rd_replay_vu.c; render passes, copies,
 * snapshots, exact blends), records the draws and copies of the passes inside [first, last]
 * only, and stops the walk after the command that began pass last + 1.
 * rd__replay_set_window_draws narrows pass `last` to some of its draws
 * through a filter (the second level), and notes the last draw recorded. */
#include <stdio.h>
#include <string.h>
#include "rd_internal.h"
#include "rd_passring.h"
#include "shader_consts.h"

RdReplayWindow g_rdWin;

RdDrawCtx g_rdDrawCtx;

void rd__replay_set_window(uint32_t first, uint32_t last)
{
    memset(&g_rdWin, 0, sizeof(g_rdWin));
    g_rdWin.first = first;
    g_rdWin.last = first ? last : 0;
}

void rd__replay_set_window_draws(uint32_t first, uint32_t last, uint32_t drawFirst,
                                 uint32_t drawLast, int filter, uint32_t arg)
{
    rd__replay_set_window(first, last);
    if (first) {
        g_rdWin.drawFirst = drawLast ? (drawFirst ? drawFirst : 1u) : 0u;
        g_rdWin.drawLast = drawLast;
        g_rdWin.filter = filter;
        g_rdWin.arg = arg;
    }
}

void rd__replay_window_stats(RdReplayWindowStats *out)
{
    memset(out, 0, sizeof(*out));
    out->passes = g_rdWin.passes;
    out->lastDraws = g_rdWin.lastDraws;
    out->drawn = g_rdWin.drawn;
    out->lastSeq = g_rdWin.lastSeq;
    out->nextSeq = g_rdWin.nextSeq;
    out->stopped = g_rdWin.stop;
}

void rd__replay_window_note(RdDrawNote *out)
{
    *out = g_rdWin.note;
}

void rd_draw_note_text(const RdDrawNote *n, char *out, size_t size)
{
    if (!n->valid) {
        snprintf(out, size, "no draw noted");
        return;
    }
    if (!n->vu) {
        snprintf(out, size, "list %d command %u, not a VU draw (type %u), vs %u fs %u, texture %u",
                 (int)n->list, (unsigned)n->cmd, (unsigned)n->type, (unsigned)n->vs,
                 (unsigned)n->fs, (unsigned)n->tex);
        return;
    }
    snprintf(out, size,
             "list %d command %u, %s %s, prog %u vs %u fs %u clip %u, batch %u, %u indices from "
             "%u, values %u..%u, %u marked triangles, texture %u %ux%u format %u mips %u, "
             "pipeline %s",
             (int)n->list, (unsigned)n->cmd,
             n->type == RDC_SKINNED ? "skinned"
             : n->type == RDC_GRID  ? "grid"
             : n->type == RDC_MESH  ? "mesh"
                                    : "particles",
             n->mesh[0] ? n->mesh : "-", (unsigned)n->prog, (unsigned)n->vs, (unsigned)n->fs,
             (unsigned)n->clip, (unsigned)n->batch, (unsigned)n->count, (unsigned)n->first,
             (unsigned)n->spanLo, (unsigned)n->spanHi, (unsigned)n->marked, (unsigned)n->tex,
             (unsigned)n->texW, (unsigned)n->texH, (unsigned)n->texFormat, (unsigned)n->texMips,
             n->key);
}

/* whether the draw's class passes the draw window's filter
 * (rd_bisect_plan.h RdBisectFilter) */
static bool winFilter(void)
{
    const RdDrawNote *n = &g_rdDrawCtx.note;
    switch (g_rdWin.filter) {
    case 2: /* VS_ONLY */
        return g_rdDrawCtx.set && n->vs == g_rdWin.arg;
    case 3: /* CLIP_ONLY */
        return g_rdDrawCtx.set && n->clip == g_rdWin.arg;
    case 4: /* SKINNED_ONLY */
        return g_rdDrawCtx.set && n->skinned;
    case 5: /* TEX_ONLY */
        return (g_rdDrawCtx.set ? n->tex : g_rdWin.tex) == g_rdWin.arg;
    default: /* ALL, MASK_MARKS */
        return true;
    }
}

/* the window's verdict on the draw about to be recorded (false: left out);
 * counts pass `last`'s draws and notes the last recorded one */
bool rd__win_take(void)
{
    if (rd__win_muted()) {
        g_rdDrawCtx.set = false;
        return false;
    }
    if (g_rdWin.passes != g_rdWin.last) {
        g_rdDrawCtx.set = false;
        return true;
    }
    const uint32_t n = ++g_rdWin.lastDraws;
    bool take = true;
    if (g_rdWin.drawLast) {
        take = n >= g_rdWin.drawFirst && n <= g_rdWin.drawLast && winFilter();
        if (take) {
            if (g_rdDrawCtx.set) {
                g_rdWin.note = g_rdDrawCtx.note;
            } else {
                /* not a VU draw: its place, the pipeline the pass ring has */
                memset(&g_rdWin.note, 0, sizeof(g_rdWin.note));
                const uint32_t rc = rd_passring_count();
                const RdPassRec *e = rc ? rd_passring_get(rc - 1) : NULL;
                g_rdWin.note.vs = e ? e->vs : 0;
                g_rdWin.note.fs = e ? e->fs : 0;
                g_rdWin.note.tex = g_rdWin.tex;
            }
            g_rdWin.note.valid = 1;
            g_rdWin.note.list = g_rdWin.list;
            g_rdWin.note.cmd = g_rdWin.cmd;
            g_rdWin.note.ordinal = n;
        }
    }
    g_rdWin.drawn += take;
    g_rdDrawCtx.set = false;
    return take;
}

/* the VU command being replayed, for the draw note (doVuDraw, while a
 * draw window is set: rd__replay_set_window_draws) */
RdVuCur g_rdVuCur;

/* the draw note of a VU draw about to be recorded (draws first .. count
 * of g_rdVuCur's list with key k): the window's filter and note read it */
void rd__win_vu_note(uint32_t tex, const RdPipeKeyInt *k, const struct IcoVuCB *vcb,
                     uint8_t classVs, int indexed, uint32_t first, uint32_t count)
{
    RdDrawNote *n = &g_rdDrawCtx.note;
    memset(n, 0, sizeof(*n));
    g_rdDrawCtx.set = true;
    n->vu = 1;
    n->type = g_rdVuCur.type;
    n->prog = g_rdVuCur.prog;
    n->skinned = g_rdVuCur.type == RDC_SKINNED;
    n->clip = (uint8_t)(vcb->draw[2] & 3u);
    n->vs = classVs;
    n->fs = k->fs;
    n->batch = g_rdVuCur.batch;
    n->first = first;
    n->count = count;
    snprintf(n->mesh, sizeof(n->mesh), "%.24s", g_rdVuCur.mesh ? g_rdVuCur.mesh : "");
    rd__pipe_key_text(k, n->key, sizeof(n->key));
    n->tex = tex;
    const RdTexRec *t = tex ? rd__tex_rec(tex) : NULL;
    if (t) {
        n->texW = t->w;
        n->texH = t->h;
        n->texFormat = t->format;
        n->texMips = t->mipLevels;
    }
    if (indexed && g_rdVuCur.index) {
        uint32_t lo = UINT32_MAX, hi = 0;
        for (uint32_t i = 0; i < count; i++) {
            const uint32_t v = g_rdVuCur.index[first + i];
            lo = v < lo ? v : lo;
            hi = v > hi ? v : hi;
        }
        n->spanLo = count ? lo : 0;
        n->spanHi = hi;
        /* the marked triangles: the mark bytes' (vu_kick_marks) */
        for (uint32_t i = 0; i + 2 < count; i += 3) {
            n->marked +=
                vcb->mark[1] != 0u ||
                (g_rdVuCur.marks && g_rdVuCur.index[first + i] / 4u < g_rdVuCur.markCount &&
                 g_rdVuCur.marks[g_rdVuCur.index[first + i] / 4u] != 0);
        }
    }
}

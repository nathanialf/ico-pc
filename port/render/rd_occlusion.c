/* rd_occlusion.c: the replay's GPU draw issue and the hidden-draw
 * measurement around it.
 *
 * Every GPU draw of a replay (the lists, the vertex pre-pass, the effects)
 * goes through rd__replay_draw or rd__replay_draw_indexed (rd_replay.c's
 * drawCmd and drawIndexedCmd are these two): a draw outside the pass
 * window's take is left out (rd__win_take, the loss bisect), and while the
 * measurement is armed (rd__occlusion_arm, the replay tool's --occlusion)
 * each draw of the armed list sits inside an occlusion query of its own.
 * A CPU table maps the query to the command that drew it; rd__occlusion_take
 * reads the counts once the GPU is done with the slot and sums them by
 * command.  A command whose draws all passed 0 samples was hidden at the
 * time it drew (or drew nothing at all: off the scissor, every fragment
 * discarded): leaving it out of the replay changes no pixel, which is what
 * a culler that tests against the depth drawn so far could save
 * (docs/port/RENDER.md, "Culling").
 *
 * The draw's command: g_rdStoreOrd (rd_replay.c sets it to the command it
 * replays, the command's index in list 0 when list 0 is the first walked),
 * or for a screen-prim run's draw, which flushScreenRun records later, the
 * command that opened the run (g_rdDrawOwner).  One GPU draw a command
 * needs the draw merges off: the replay tool runs the measurement under
 * ICO_RD_VU_MERGE=0 and with the screen-prim runs off (rd__set_screen_merge).
 * Disarmed, a draw costs one test of a flag. */
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"
#include "rd_replay.h"

uint32_t g_rdDrawOwner;

static struct {
    bool armed;
    int list;
    uint32_t replay;   /* g_rd.replayCounter of the measured replay, 0 before its first draw */
    uint32_t slot;     /* rhi_frame_slot of that replay */
    uint32_t *qCmd;    /* per query: the command (RHI_MAX_OCCLUSION) */
    uint32_t nq;       /* queries begun */
    uint32_t overflow; /* draws past RHI_MAX_OCCLUSION */
    RdOccCmd *cmds;
    uint32_t cmdCount, cmdCap;
} s_occ;

static void occFree(void)
{
    free(s_occ.qCmd);
    free(s_occ.cmds);
    memset(&s_occ, 0, sizeof(s_occ));
}

void rd__occlusion_arm(int list)
{
    occFree();
    if (list < 0 || list >= RD_LIST_COUNT || !g_rd.hasDevice || !rhi_occlusion_supported()) {
        return;
    }
    s_occ.qCmd = malloc(sizeof(uint32_t) * RHI_MAX_OCCLUSION);
    if (!s_occ.qCmd) {
        return;
    }
    s_occ.list = list;
    s_occ.armed = true;
}

/* the command's entry, the table grown to hold it; NULL when it cannot be */
static RdOccCmd *occCmd(uint32_t cmd)
{
    if (cmd >= s_occ.cmdCap) {
        uint32_t cap = s_occ.cmdCap ? s_occ.cmdCap : 1024u;
        while (cap <= cmd) {
            cap *= 2u;
        }
        RdOccCmd *n = realloc(s_occ.cmds, sizeof(*n) * cap);
        if (!n) {
            return NULL;
        }
        memset(n + s_occ.cmdCap, 0, sizeof(*n) * (cap - s_occ.cmdCap));
        s_occ.cmds = n;
        s_occ.cmdCap = cap;
    }
    if (cmd >= s_occ.cmdCount) {
        s_occ.cmdCount = cmd + 1u;
    }
    return &s_occ.cmds[cmd];
}

/* before a draw of count vertices or indices: its query's index, or -1
 * for a draw outside the measurement */
static int occBegin(RhiCommandList cl, uint32_t count, uint32_t instances)
{
    if (!s_occ.armed || rd__replay_list() != s_occ.list) {
        return -1;
    }
    if (s_occ.replay == 0) {
        s_occ.replay = g_rd.replayCounter;
        s_occ.slot = rhi_frame_slot();
    } else if (s_occ.replay != g_rd.replayCounter) {
        return -1; /* one replay a measurement */
    }
    const uint32_t cmd = g_rdDrawOwner ? g_rdDrawOwner - 1u : g_rdStoreOrd;
    RdOccCmd *c = occCmd(cmd);
    if (!c) {
        return -1;
    }
    c->draws++;
    c->tris += (uint64_t)(count / 3u) * (instances ? instances : 1u);
    if (s_occ.nq >= RHI_MAX_OCCLUSION) {
        s_occ.overflow++;
        return -1;
    }
    const uint32_t q = s_occ.nq++;
    s_occ.qCmd[q] = cmd;
    rhi_cmd_begin_occlusion(cl, q);
    return (int)q;
}

static void occEnd(RhiCommandList cl, int q)
{
    if (q >= 0) {
        rhi_cmd_end_occlusion(cl, (uint32_t)q);
    }
}

void rd__replay_draw(RhiCommandList cl, uint32_t vertexCount, uint32_t firstVertex,
                     uint32_t instanceCount)
{
    if (g_rdWin.first && !rd__win_take()) {
        return;
    }
    const int q = s_occ.armed ? occBegin(cl, vertexCount, instanceCount) : -1;
    rhi_cmd_draw(cl, vertexCount, firstVertex, instanceCount);
    occEnd(cl, q);
}

void rd__replay_draw_indexed(RhiCommandList cl, uint32_t indexCount, uint32_t firstIndex,
                             int32_t vertexOffset, uint32_t instanceCount)
{
    if (g_rdWin.first && !rd__win_take()) {
        return;
    }
    const int q = s_occ.armed ? occBegin(cl, indexCount, instanceCount) : -1;
    rhi_cmd_draw_indexed(cl, indexCount, firstIndex, vertexOffset, instanceCount);
    occEnd(cl, q);
}

bool rd__occlusion_take(RdOccResult *out)
{
    memset(out, 0, sizeof(*out));
    out->list = s_occ.list;
    if (!s_occ.armed || s_occ.replay == 0) {
        occFree();
        return false;
    }
    s_occ.armed = false;
    /* the measured slot recycled: its counts are then rhi_read_occlusion's */
    for (uint32_t k = 0; k < RHI_FRAMES_IN_FLIGHT; k++) {
        rd__wait_frame();
        if (rhi_frame_slot() == s_occ.slot) {
            break;
        }
    }
    uint64_t *counts = malloc(sizeof(uint64_t) * RHI_MAX_OCCLUSION);
    const uint32_t n = counts && rhi_frame_slot() == s_occ.slot
                           ? rhi_read_occlusion(counts, RHI_MAX_OCCLUSION)
                           : 0u;
    for (uint32_t q = 0; q < s_occ.nq; q++) {
        RdOccCmd *c = &s_occ.cmds[s_occ.qCmd[q]];
        if (q < n && counts[q] != RHI_OCCLUSION_NONE) {
            c->counted++;
            c->samples += counts[q];
        } else {
            out->unread++;
        }
    }
    free(counts);
    out->cmdCount = s_occ.cmdCount;
    out->cmds = s_occ.cmds; /* the caller's now */
    out->queries = s_occ.nq;
    out->overflow = s_occ.overflow;
    s_occ.cmds = NULL;
    occFree();
    return true;
}

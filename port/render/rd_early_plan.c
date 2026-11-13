/* rd_early_plan.c: rd_early_plan.h's plan (pure). */
#include "rd_early_plan.h"

#include <stdio.h>
#include <string.h>

#define MB (1024ull * 1024ull)

/* the largest power-of-two row count from minRows to maxRows above
   haveRows whose bytes fit in budget, 0 none */
static uint32_t largestRows(const RdEarlyIn *in, uint64_t budget)
{
    uint32_t best = 0;
    if (in->rowBytes == 0 || in->minRows == 0) {
        return 0;
    }
    for (uint32_t r = in->minRows; r <= in->maxRows && r != 0; r *= 2u) {
        if (r > in->haveRows && (uint64_t)r * in->rowBytes <= budget) {
            best = r;
        }
        if (r > UINT32_MAX / 2u) {
            break;
        }
    }
    return best;
}

size_t rd_vu_record_memory_text(char *out, size_t cap, const RdVuRecMem *m)
{
    if (cap == 0) {
        return 0;
    }
    char early[64] = "nothing made at start-up";
    if (m->earlyRows) {
        snprintf(early, sizeof early, "%u rows made at start-up", m->earlyRows);
    }
    const int n =
        snprintf(out, cap,
                 "window: VU record memory: %u rows held (%llu MB over the frame slots; "
                 "%s); a picture of the block needed at most %u rows (%u in the run); "
                 "%u growths in play in the run; the upload rings %llu MB",
                 m->heldRows, (unsigned long long)(m->heldBytes / MB), early, m->blockNeedRows,
                 m->runNeedRows, m->grownInPlay, (unsigned long long)(m->ringBytes / MB));
    return n < 0 ? 0 : (size_t)n >= cap ? cap - 1u : (size_t)n;
}

uint32_t rd_early_start_rows(bool ticks, bool vel, uint32_t minRows, uint32_t maxRows,
                             uint32_t rowW)
{
    if (rowW == 0 || minRows == 0 || maxRows < minRows) {
        return 0;
    }
    const uint64_t v = (uint64_t)RD_EARLY_NEED_VERTS * (vel ? 2u : 1u);
    const uint64_t t = (uint64_t)RD_EARLY_NEED_KICKS * (ticks ? 3u : 2u);
    const uint64_t need = (v + rowW - 1u) / rowW + (t + rowW - 1u) / rowW;
    uint64_t rows = minRows;
    while (rows < need && rows < maxRows) {
        rows *= 2u;
    }
    rows *= 2u; /* one growth step's margin */
    return rows > maxRows ? maxRows : (uint32_t)rows;
}

void rd_early_plan(const RdEarlyIn *in, RdEarlyPlan *out)
{
    memset(out, 0, sizeof(*out));
    switch (in->memory) {
    case RD_EARLY_MEM_SIGNAL:
        snprintf(out->why, sizeof(out->why), "the system's low-memory signal came");
        return;
    case RD_EARLY_MEM_FLOOR:
        snprintf(out->why, sizeof(out->why),
                 "%ld MB available, under the free-memory floor of %ld MB", in->availMb,
                 in->floorMb);
        return;
    case RD_EARLY_MEM_OK:
        if (in->availMb <= in->floorMb) {
            snprintf(out->why, sizeof(out->why),
                     "%ld MB available, not above the free-memory floor of %ld MB", in->availMb,
                     in->floorMb);
            return;
        }
        out->budget = (uint64_t)(in->availMb - in->floorMb) * MB / 2u;
        snprintf(out->why, sizeof(out->why),
                 "at most half of the %ld MB above the free-memory floor (%ld MB available, floor "
                 "%ld MB)",
                 in->availMb - in->floorMb, in->availMb, in->floorMb);
        break;
    default:
        if (in->gpuAvail != 0) {
            out->budget = in->gpuAvail / 2u;
            snprintf(out->why, sizeof(out->why),
                     "at most half of the adapter's %llu MB of free video memory",
                     (unsigned long long)(in->gpuAvail / MB));
        } else {
            out->budget = UINT64_MAX;
            snprintf(out->why, sizeof(out->why),
                     "the free memory is not measured here: the record's own limit");
        }
        break;
    }
    /* the record first, then the arena's chunk with what is left */
    uint64_t used = 0;
    if (in->recordWanted) {
        out->rows = out->boundRows = largestRows(in, out->budget);
        if (in->startRows != 0 && out->rows > in->startRows) {
            /* the need's size: no larger record than the run needs (play
               grows it past this as a picture needs, rd_replay_vu.c) */
            out->rows = in->startRows > in->haveRows ? in->startRows : 0u;
        }
        used = (uint64_t)out->rows * in->rowBytes;
    }
    if (!in->arenaHave && in->arenaBytes != 0 && out->budget - used >= in->arenaBytes) {
        out->arena = true;
        used += in->arenaBytes;
    }
    out->bytes = used;
}

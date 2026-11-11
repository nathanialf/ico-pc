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
        out->rows = largestRows(in, out->budget);
        used = (uint64_t)out->rows * in->rowBytes;
    }
    if (!in->arenaHave && in->arenaBytes != 0 && out->budget - used >= in->arenaBytes) {
        out->arena = true;
        used += in->arenaBytes;
    }
    out->bytes = used;
}

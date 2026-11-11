/* rd_early_plan.h: how much of what play would allocate in its first 3D
 * pictures the renderer makes at start-up instead (rd_vu_rec_early.h;
 * docs/port/RENDER.md "Made at start-up").
 *
 * Two allocations of a fixed, known kind happen in the first pictures of
 * the first 3D scene on every device:
 *   - the VU pre-pass's record (rd_replay_vu.c "the record's buffers"): one
 *     buffer a frame slot (and three render targets under the targets
 *     write), 48 KB a row and slot, grown by powers of two from 16 rows to
 *     RD_VU_REC_ROWS_MAX (1024: 1048576 slots, 48 MB a frame slot) as a
 *     picture needs; the logs show 256 rows in the first 3D scene of both
 *     v0.11.3 loss reports (issue 179 report/ico-pc.log:378, issue 211
 *     report/ico-pc.log:535), 512 later on the same Mali under v0.11.2
 *     (v0113/evidence/179/zip/ico-pc.log:792) and 1024 on desktops
 *     (v094/evidence/130/ico-pc.log:1372);
 *   - the mesh arena's first chunk (rd_mesh.c, RD_MESH_CHUNK: 32 MB), made
 *     when the first mesh is uploaded.
 * The plan takes the record first (both v0.11.3 losses came in the pictures
 * right after it grew), as large as the bound allows, then the arena's
 * chunk if it still fits.  The bound: half of the free memory above the
 * free-memory floor (port/platform/memory_floor.h) where the system says
 * how much is free (Android), else half of the adapter's free video memory
 * where it says (Direct3D; Vulkan through VK_EXT_memory_budget), else none
 * (the record's own limit).  Nothing
 * under the floor or after the system's low-memory signal: play then grows
 * the record as before.
 *
 * Pure: no device, no renderer state (rd_early_plan_test). */
#ifndef PORT_RENDER_RD_EARLY_PLAN_H
#define PORT_RENDER_RD_EARLY_PLAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* what the window's memory function said (rd_vu_rec_early.h RdEarlyMemoryFn) */
#define RD_EARLY_MEM_UNKNOWN (-1) /* the system does not say how much is free */
#define RD_EARLY_MEM_OK 0         /* availMb and floorMb hold the numbers */
#define RD_EARLY_MEM_FLOOR 1      /* free memory under the floor now */
#define RD_EARLY_MEM_SIGNAL 2     /* the system's low-memory signal came */

typedef struct RdEarlyIn {
    int memory;                /* RD_EARLY_MEM_* */
    long availMb, floorMb;     /* with RD_EARLY_MEM_OK and RD_EARLY_MEM_FLOOR */
    uint64_t gpuAvail;         /* the adapter's free video memory in bytes, 0 not said */
    uint32_t haveRows;         /* the record's rows now (16 after the self-test) */
    uint32_t minRows, maxRows; /* RD_VU_REC_ROWS_MIN, RD_VU_REC_ROWS_MAX */
    uint64_t rowBytes;         /* a row's bytes over every frame slot (and the targets) */
    bool recordWanted;         /* the pre-pass is on */
    bool arenaHave;            /* the arena's first chunk is made already */
    uint64_t arenaBytes;       /* RD_MESH_CHUNK */
} RdEarlyIn;

typedef struct RdEarlyPlan {
    uint32_t rows;   /* the record's rows to make now, 0 none */
    bool arena;      /* the arena's first chunk now */
    uint64_t budget; /* the bound in bytes, UINT64_MAX none */
    uint64_t bytes;  /* what the plan makes: rows * rowBytes and the chunk */
    char why[192];   /* the bound's words, for the start-up line */
} RdEarlyPlan;

/* the plan for in (out zeroed first) */
void rd_early_plan(const RdEarlyIn *in, RdEarlyPlan *out);

#endif

/* rd_vu_rec_early.c: rd_vu_rec_early.h's start-up preparation. */
#include "rd_vu_rec_early.h"

#include <stdio.h>
#include <string.h>
#include "rd_early_plan.h"
#include "rd_internal.h"
#include "rd_vu_record.h"

static uint32_t s_rows;
static bool s_arena;

uint32_t rd_vu_record_early_rows(void)
{
    return s_rows;
}

bool rd_vu_record_early_arena(void)
{
    return s_arena;
}

void rd_vu_record_prepare(RdEarlyMemoryFn memory)
{
    s_rows = 0;
    s_arena = false;
    if (!g_rd.hasDevice) {
        return;
    }
    const double t0 = rd__now_ms();
    RdEarlyIn in;
    memset(&in, 0, sizeof(in));
    char said[160] = "";
    in.memory = RD_EARLY_MEM_UNKNOWN;
    if (memory != NULL) {
        in.memory = memory(&in.availMb, &in.floorMb, said, sizeof(said));
    }
    if (in.memory == RD_EARLY_MEM_UNKNOWN) {
        RhiStats st;
        memset(&st, 0, sizeof(st));
        rhi_get_stats(&st);
        /* Direct3D's adapter memory, Vulkan's VK_EXT_memory_budget where it is
           enabled (not on a phone); 0 elsewhere */
        in.gpuAvail = st.videoMemAvailable;
    }
    in.haveRows = rd__vu_rec_rows();
    in.minRows = rd__vu_rec_rows_min();
    in.maxRows = rd__vu_rec_rows_max();
    in.rowBytes = rd__vu_rec_row_bytes();
    in.recordWanted = g_rd.vuPrepass;
    in.arenaHave = rd__mesh_arena_ready();
    in.arenaBytes = rd__mesh_arena_chunk_bytes();
    RdEarlyPlan p;
    rd_early_plan(&in, &p);

    bool recOk = true, arenaOk = true;
    if (p.rows != 0) {
        recOk = rd__vu_rec_presize(p.rows);
    }
    if (p.arena) {
        arenaOk = rd__mesh_arena_prepare();
    }
    s_rows = recOk ? p.rows : 0u;
    s_arena = p.arena && arenaOk;
    const double ms = rd__now_ms() - t0;
    const uint64_t made = (uint64_t)s_rows * in.rowBytes + (s_arena ? in.arenaBytes : 0u);

    /* the record's part */
    char rec[224];
    const unsigned long long mb = 1024ull * 1024ull;
    if (!in.recordWanted) {
        snprintf(rec, sizeof(rec), "the VU pre-pass's record: none (the pre-pass is off)");
    } else if (p.rows == 0) {
        snprintf(rec, sizeof(rec),
                 "the VU pre-pass's record: kept at %u rows (no larger one within the bound: play "
                 "grows it as a picture needs, up to %u rows)",
                 in.haveRows, in.maxRows);
    } else if (!recOk) {
        snprintf(rec, sizeof(rec),
                 "the VU pre-pass's record at %u rows (%llu MB) could not be made: play grows it "
                 "as before",
                 p.rows, (unsigned long long)((uint64_t)p.rows * in.rowBytes / mb));
    } else {
        snprintf(
            rec, sizeof(rec),
            "the VU pre-pass's record at %u rows (%u slots, %llu MB over the %u frame slots%s; "
            "%s)",
            p.rows, p.rows * 1024u, (unsigned long long)((uint64_t)p.rows * in.rowBytes / mb),
            (unsigned)RHI_FRAMES_IN_FLIGHT,
            rd__vu_record_buffered() ? "" : " and its three render targets",
            p.rows >= in.maxRows ? "its largest: play makes no record buffer"
                                 : "play grows it only past this");
    }
    char arena[128];
    if (in.arenaHave) {
        snprintf(arena, sizeof(arena), "the mesh arena's first chunk was made already");
    } else if (!p.arena) {
        snprintf(arena, sizeof(arena),
                 "the mesh arena's first chunk (%llu MB) left to the first mesh (not within the "
                 "bound)",
                 (unsigned long long)(in.arenaBytes / mb));
    } else if (!arenaOk) {
        snprintf(arena, sizeof(arena),
                 "the mesh arena's first chunk (%llu MB) could not be made: the first mesh makes "
                 "it",
                 (unsigned long long)(in.arenaBytes / mb));
    } else {
        snprintf(arena, sizeof(arena), "the mesh arena's first chunk (%llu MB)",
                 (unsigned long long)(in.arenaBytes / mb));
    }
    rd__log("start-up: made before play in %.1f ms: %s, %s; %llu MB in all; the bound: %s%s%s", ms,
            rec, arena, (unsigned long long)(made / mb), p.why, said[0] ? "; the system: " : "",
            said);
}

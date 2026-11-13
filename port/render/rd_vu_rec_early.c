/* rd_vu_rec_early.c: rd_vu_rec_early.h's start-up preparation. */
#include "rd_vu_rec_early.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "attempt_ledger.h"
#include "rd_early_plan.h"
#include "rd_internal.h"
#include "rd_velocity.h"
#include "rd_vu_record.h"
#include "shader_consts.h"

static uint32_t s_rows;
static bool s_arena;

/* rd__vu_rec_note's (the render thread) for rd_vu_record_memory (the
   window's): __atomic */
static uint32_t s_held, s_needBlock, s_needRun, s_grown, s_early;
static uint64_t s_heldBytes, s_ringBytes;

static void atomicMax(uint32_t *p, uint32_t v)
{
    uint32_t have = __atomic_load_n(p, __ATOMIC_RELAXED);
    while (v > have &&
           !__atomic_compare_exchange_n(p, &have, v, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}

void rd__vu_rec_note(uint32_t needRows, uint32_t heldRows, uint64_t heldBytes, uint64_t ringBytes)
{
    __atomic_store_n(&s_held, heldRows, __ATOMIC_RELAXED);
    __atomic_store_n(&s_heldBytes, heldBytes, __ATOMIC_RELAXED);
    __atomic_store_n(&s_ringBytes, ringBytes, __ATOMIC_RELAXED);
    atomicMax(&s_needBlock, needRows);
    atomicMax(&s_needRun, needRows);
}

void rd__vu_rec_grew(void)
{
    __atomic_fetch_add(&s_grown, 1u, __ATOMIC_RELAXED);
    ico_attempt_fire(ICO_ATT_REC_NEED, 1);
}

void rd_vu_record_memory(RdVuRecMem *out)
{
    memset(out, 0, sizeof(*out));
    out->heldRows = __atomic_load_n(&s_held, __ATOMIC_RELAXED);
    out->heldBytes = __atomic_load_n(&s_heldBytes, __ATOMIC_RELAXED);
    out->ringBytes = __atomic_load_n(&s_ringBytes, __ATOMIC_RELAXED);
    out->blockNeedRows = __atomic_exchange_n(&s_needBlock, 0u, __ATOMIC_RELAXED);
    out->runNeedRows = __atomic_load_n(&s_needRun, __ATOMIC_RELAXED);
    out->grownInPlay = __atomic_load_n(&s_grown, __ATOMIC_RELAXED);
    out->earlyRows = __atomic_load_n(&s_early, __ATOMIC_RELAXED);
}

/* ICO_RD_EARLY_ROWS=N (the tests): the need taken as N rows, a power of
   two from minRows to maxRows; 0 unset or not such a number (said) */
static uint32_t rowsFromEnv(uint32_t minRows, uint32_t maxRows)
{
    const char *e = getenv("ICO_RD_EARLY_ROWS");
    if (e == NULL || e[0] == '\0') {
        return 0;
    }
    char *end = NULL;
    const unsigned long v = strtoul(e, &end, 10);
    if (end == e || *end != '\0' || v < minRows || v > maxRows || (v & (v - 1u)) != 0u) {
        rd__log("start-up: ICO_RD_EARLY_ROWS=%s is not a power of two from %u to %u: the run's "
                "need",
                e, minRows, maxRows);
        return 0;
    }
    return (uint32_t)v;
}

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
    /* the record's size from need (rd_early_plan.h rd_early_start_rows):
       the slot rule as the run starts (the frame rate's interpolation, the
       temporal upscaler's velocity) */
    const bool ticks = rd_interpolation_active(), vel = rd__velocity_on();
    const uint32_t envRows = rowsFromEnv(in.minRows, in.maxRows);
    in.startRows = envRows != 0u
                       ? envRows
                       : rd_early_start_rows(ticks, vel, in.minRows, in.maxRows, ICO_VU_REC_W);
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
    __atomic_store_n(&s_early, s_rows, __ATOMIC_RELAXED);
    ico_attempt_decide_rec_need(in.recordWanted, s_rows, p.boundRows, in.startRows);
    const double ms = rd__now_ms() - t0;
    const uint64_t made = (uint64_t)s_rows * in.rowBytes + (s_arena ? in.arenaBytes : 0u);

    /* the record's part */
    char rec[224];
    /* the need's words: what sized the record, what the bound alone would
       have made */
    char need[200];
    snprintf(need, sizeof(need), "; the run's need %u rows (%s%s%s), the bound alone %u rows%s",
             in.startRows,
             envRows != 0u ? "ICO_RD_EARLY_ROWS" : "the largest recorded scene's, doubled",
             envRows == 0u && ticks ? ", the presents interpolated" : "",
             envRows == 0u && vel ? ", the velocity's record" : "", p.boundRows,
             p.boundRows > p.rows && p.rows != 0u ? ": the need's size made" : "");
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
    rd__log("start-up: made before play in %.1f ms: %s, %s; %llu MB in all; the bound: %s%s%s%s",
            ms, rec, arena, (unsigned long long)(made / mb), p.why, in.recordWanted ? need : "",
            said[0] ? "; the system: " : "", said);
}

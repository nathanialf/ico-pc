/* rhi_d3d12_plan_test.c: the D3D12 backend's bookkeeping on the CPU
 * (port/rhi/d3d12/d3d12_plan.c), on every host; no D3D12 runtime needed.
 *
 *   - the RhiState -> D3D12_RESOURCE_STATES table: write states alone,
 *     depth SHADER_READ == DEPTH_READ, PRESENT == COMMON; the barrier plan
 *     over every (before, after) pair (never a transition between equal
 *     states, which the debug layer rejects), UNDEFINED taking the tracked
 *     state, mismatch reporting, and the sequences rhi_test_common.c and the
 *     swapchain path record
 *   - buffer state tracking for copies (COMMON at list start, COPY_DEST,
 *     GENERIC_READ) and same-state copy ordering
 *   - the descriptor rings: regions per frame slot, no overlap with each
 *     other or the persistent samplers, exhaustion, reset
 *   - layouts as descriptor tables and root parameters (rd_core's layouts)
 *   - constant buffer view sizes, integer clear values, subresource indices
 *   - the DXBC input signature reader on the test shaders (rhi_test_dxil.h)
 *     and malformed input
 *   - handle pools and slot allocators
 *   - with the shader table (ICO_HAVE_SHADER_TABLE): every vertex shader of
 *     port/shaders, RHI location n -> DXIL input -> the same semantic as the
 *     SPIR-V input at location n (DXC's in.var.<SEMANTIC> names); every DXIL
 *     blob a signed DXBC container
 *
 * Exit 0 on success, 1 on a failure. */
#include "d3d12/d3d12_plan.h"
#include "rhi_test_dxil.h"
#include "rhi_test_spv.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ICO_HAVE_SHADER_TABLE
#include "shaders_gen.h"
#endif

static int failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static const uint32_t kWriteBits =
    D3DP_STATE_RENDER_TARGET | D3DP_STATE_DEPTH_WRITE | D3DP_STATE_COPY_DEST;

/* ------------------------------------------------------------ state table */
static void testStateTable(void)
{
    for (int d = 0; d < 2; d++) {
        for (int s = 0; s < RHI_STATE_COUNT; s++) {
            uint32_t v = d3dp_TextureState((RhiState)s, d != 0);
            /* a write state stands alone (D3D12: write states cannot be
             * combined with any other state) */
            if (v & kWriteBits) {
                CHECK((v & (v - 1u)) == 0, "state %d depth %d: write state 0x%x combined", s, d,
                      (unsigned)v);
            }
        }
        CHECK(d3dp_TextureState(RHI_STATE_PRESENT, d) == D3DP_STATE_COMMON, "PRESENT is COMMON");
        CHECK(d3dp_TextureState(RHI_STATE_UNDEFINED, d) == D3DP_STATE_COMMON, "UNDEFINED");
        CHECK(d3dp_TextureState(RHI_STATE_COPY_SRC, d) == D3DP_STATE_COPY_SOURCE, "COPY_SRC");
        CHECK(d3dp_TextureState(RHI_STATE_COPY_DST, d) == D3DP_STATE_COPY_DEST, "COPY_DST");
        CHECK(d3dp_TextureState(RHI_STATE_RENDER_TARGET, d) == D3DP_STATE_RENDER_TARGET, "RT");
        CHECK(d3dp_TextureState(RHI_STATE_DEPTH_WRITE, d) == D3DP_STATE_DEPTH_WRITE, "DW");
        CHECK(d3dp_TextureState(RHI_STATE_DEPTH_READ, d) ==
                  (D3DP_STATE_DEPTH_READ | D3DP_STATE_SHADER_RESOURCE),
              "DEPTH_READ is depth read + shader resource");
    }
    CHECK(d3dp_TextureState(RHI_STATE_SHADER_READ, false) == D3DP_STATE_SHADER_RESOURCE,
          "colour SHADER_READ");
    CHECK(d3dp_TextureState(RHI_STATE_SHADER_READ, true) ==
              d3dp_TextureState(RHI_STATE_DEPTH_READ, true),
          "depth SHADER_READ == DEPTH_READ");
}

static void testBarrierPlan(void)
{
    /* every pair, both kinds of format, any tracked state */
    for (int d = 0; d < 2; d++) {
        for (int b = 0; b < RHI_STATE_COUNT; b++) {
            for (int a = 0; a < RHI_STATE_COUNT; a++) {
                for (int t = 0; t < RHI_STATE_COUNT; t++) {
                    uint32_t tracked = d3dp_TextureState((RhiState)t, d);
                    uint32_t ob = 0xDEAD, oa = 0xDEAD;
                    bool mismatch = false;
                    bool emit = d3dp_PlanTextureBarrier((RhiState)b, (RhiState)a, d, &tracked, &ob,
                                                        &oa, &mismatch);
                    uint32_t want = d3dp_TextureState((RhiState)a, d);
                    uint32_t from = b == RHI_STATE_UNDEFINED ? d3dp_TextureState((RhiState)t, d)
                                                             : d3dp_TextureState((RhiState)b, d);
                    CHECK(tracked == want, "tracked after %d->%d", b, a);
                    CHECK(oa == want && ob == from, "sides of %d->%d (tracked %d)", b, a, t);
                    CHECK(emit == (from != want), "emit %d->%d (tracked %d)", b, a, t);
                    CHECK(!emit || ob != oa, "equal-state transition %d->%d", b, a);
                    CHECK(mismatch ==
                              (b != RHI_STATE_UNDEFINED && d3dp_TextureState((RhiState)b, d) !=
                                                               d3dp_TextureState((RhiState)t, d)),
                          "mismatch %d (tracked %d)", b, t);
                }
            }
        }
    }

    /* rhi_test_common.c's texture: frame 0 from creation (COMMON), frame 1
     * starts again from UNDEFINED while it is really in SHADER_READ */
    uint32_t tr = D3DP_STATE_COMMON, ob, oa;
    CHECK(d3dp_PlanTextureBarrier(RHI_STATE_UNDEFINED, RHI_STATE_COPY_DST, false, &tr, &ob, &oa,
                                  NULL) &&
              ob == D3DP_STATE_COMMON && oa == D3DP_STATE_COPY_DEST,
          "first upload");
    CHECK(d3dp_PlanTextureBarrier(RHI_STATE_COPY_DST, RHI_STATE_SHADER_READ, false, &tr, &ob, &oa,
                                  NULL),
          "to SHADER_READ");
    CHECK(d3dp_PlanTextureBarrier(RHI_STATE_UNDEFINED, RHI_STATE_COPY_DST, false, &tr, &ob, &oa,
                                  NULL) &&
              ob == D3DP_STATE_SHADER_RESOURCE && oa == D3DP_STATE_COPY_DEST,
          "re-upload from UNDEFINED uses the tracked SHADER_READ");

    /* depth: DEPTH_READ <-> SHADER_READ is no transition */
    tr = D3DP_STATE_COMMON;
    d3dp_PlanTextureBarrier(RHI_STATE_UNDEFINED, RHI_STATE_DEPTH_WRITE, true, &tr, &ob, &oa, NULL);
    CHECK(d3dp_PlanTextureBarrier(RHI_STATE_DEPTH_WRITE, RHI_STATE_DEPTH_READ, true, &tr, &ob, &oa,
                                  NULL),
          "DEPTH_WRITE -> DEPTH_READ");
    CHECK(!d3dp_PlanTextureBarrier(RHI_STATE_DEPTH_READ, RHI_STATE_SHADER_READ, true, &tr, &ob, &oa,
                                   NULL),
          "DEPTH_READ -> SHADER_READ on depth is a no-op");
    CHECK(d3dp_PlanTextureBarrier(RHI_STATE_SHADER_READ, RHI_STATE_COPY_SRC, true, &tr, &ob, &oa,
                                  NULL) &&
              ob == (D3DP_STATE_DEPTH_READ | D3DP_STATE_SHADER_RESOURCE),
          "depth SHADER_READ -> COPY_SRC leaves the depth read state");

    /* a backbuffer: acquired in PRESENT (COMMON), UNDEFINED -> RT -> COPY_SRC
     * -> PRESENT */
    tr = D3DP_STATE_PRESENT;
    CHECK(d3dp_PlanTextureBarrier(RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET, false, &tr, &ob,
                                  &oa, NULL) &&
              ob == D3DP_STATE_COMMON,
          "backbuffer to RT");
    d3dp_PlanTextureBarrier(RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC, false, &tr, &ob, &oa,
                            NULL);
    CHECK(d3dp_PlanTextureBarrier(RHI_STATE_COPY_SRC, RHI_STATE_PRESENT, false, &tr, &ob, &oa,
                                  NULL) &&
              oa == D3DP_STATE_PRESENT && tr == D3DP_STATE_PRESENT,
          "backbuffer to PRESENT");
    bool mm = false;
    tr = D3DP_STATE_COPY_DEST;
    d3dp_PlanTextureBarrier(RHI_STATE_RENDER_TARGET, RHI_STATE_SHADER_READ, false, &tr, &ob, &oa,
                            &mm);
    CHECK(mm && ob == D3DP_STATE_RENDER_TARGET, "a wrong before is reported, the caller's used");
}

/* --------------------------------------------------------------- buffers */
static void testBuffers(void)
{
    D3dpBufferTrack t = {0, 0};
    uint32_t b, a;
    CHECK(d3dp_BufferState(&t, 5) == D3DP_STATE_COMMON, "fresh buffer is COMMON");
    CHECK(d3dp_BufferBeginCopyDst(&t, 5, &b, &a) && b == D3DP_STATE_COMMON &&
              a == D3DP_STATE_COPY_DEST,
          "first copy: COMMON -> COPY_DEST");
    CHECK(!d3dp_BufferBeginCopyDst(&t, 5, &b, &a), "already COPY_DEST: no transition");
    d3dp_BufferEndCopyDst(&t, 5, &b, &a);
    CHECK(b == D3DP_STATE_COPY_DEST && a == D3DP_STATE_GENERIC_READ, "after: GENERIC_READ");
    CHECK(d3dp_BufferState(&t, 5) == D3DP_STATE_GENERIC_READ, "state within the list");
    CHECK(d3dp_BufferBeginCopyDst(&t, 5, &b, &a) && b == D3DP_STATE_GENERIC_READ,
          "a second copy in the list transitions (orders the writes)");
    d3dp_BufferEndCopyDst(&t, 5, &b, &a);
    CHECK(d3dp_BufferState(&t, 6) == D3DP_STATE_COMMON, "next list: decayed to COMMON");
    CHECK((D3DP_STATE_GENERIC_READ &
           (D3DP_STATE_VERTEX_AND_CONSTANT_BUFFER | D3DP_STATE_INDEX_BUFFER |
            D3DP_STATE_SHADER_RESOURCE | D3DP_STATE_COPY_SOURCE)) ==
              (D3DP_STATE_VERTEX_AND_CONSTANT_BUFFER | D3DP_STATE_INDEX_BUFFER |
               D3DP_STATE_SHADER_RESOURCE | D3DP_STATE_COPY_SOURCE),
          "GENERIC_READ covers vertex, index, constant, shader and copy reads");

    uint64_t mark = 0;
    CHECK(!d3dp_CopyNeedsSync(&mark, 7), "first copy into a texture");
    CHECK(d3dp_CopyNeedsSync(&mark, 7), "second copy in the same list waits");
    d3dp_CopyMarkBarrier(&mark);
    CHECK(!d3dp_CopyNeedsSync(&mark, 7), "a barrier in between orders it already");
    CHECK(!d3dp_CopyNeedsSync(&mark, 8), "a copy in the next list");
}

/* ----------------------------------------------------------------- rings */
static void testRings(void)
{
    const uint32_t total = 2u * 65536u;
    D3dpRing r[2];
    for (uint32_t f = 0; f < 2; f++) {
        d3dp_RingInit(&r[f], total, 0, 2, f);
    }
    CHECK(r[0].base == 0 && r[0].size == 65536 && r[1].base == 65536 && r[1].size == 65536,
          "resource rings");
    uint32_t idx = 0;
    CHECK(d3dp_RingAlloc(&r[1], 3, &idx) && idx == 65536, "first allocation at the base");
    CHECK(d3dp_RingAlloc(&r[1], 65533, &idx) && idx == 65539, "fill to the end");
    CHECK(!d3dp_RingAlloc(&r[1], 1, &idx), "full");
    CHECK(!d3dp_RingAlloc(&r[0], 65537, &idx), "larger than the region");
    d3dp_RingReset(&r[1]);
    CHECK(d3dp_RingAlloc(&r[1], 1, &idx) && idx == 65536, "reset");

    /* sampler heap: 256 persistent (one per RhiSampler) and the default
     * sampler of null tables, the rest split per frame (d3d12_cmd.c) */
    D3dpRing s[2];
    for (uint32_t f = 0; f < 2; f++) {
        d3dp_RingInit(&s[f], 2048, 257, 2, f);
    }
    CHECK(s[0].base == 257 && s[0].size == 895 && s[1].base == 1152 && s[1].size == 895,
          "sampler rings %u+%u, %u+%u", s[0].base, s[0].size, s[1].base, s[1].size);
    CHECK(s[1].base + s[1].size <= 2048, "inside the heap");

    /* random allocations stay inside their own region */
    srand(12345);
    for (int round = 0; round < 50; round++) {
        uint32_t frames = 1u + (uint32_t)(rand() % 3), tot = 64u + (uint32_t)(rand() % 5000);
        uint32_t pers = (uint32_t)(rand() % 64);
        for (uint32_t f = 0; f < frames; f++) {
            D3dpRing q;
            d3dp_RingInit(&q, tot, pers, frames, f);
            CHECK(q.base >= pers && q.base + q.size <= tot, "region in heap");
            while (d3dp_RingAlloc(&q, 1u + (uint32_t)(rand() % 7), &idx)) {
                CHECK(idx >= q.base && idx < q.base + q.size, "allocation in region");
            }
            CHECK(q.used <= q.size, "used <= size");
        }
    }
}

/* -------------------------------------------------------------- layouts */
static void testLayouts(void)
{
    const uint32_t VS = 1u << RHI_STAGE_VERTEX, FS = 1u << RHI_STAGE_FRAGMENT;
    /* rd_core's layouts (rd_replay.c rd__GpuInit; the uniforms are dynamic,
     * root CBVs) */
    const RhiBindSlot s0[1] = {{0, RHI_BIND_UNIFORM_BUFFER_DYNAMIC, VS | FS}};
    const RhiBindSlot s1[1] = {{1, RHI_BIND_UNIFORM_BUFFER_DYNAMIC, VS | FS}};
    const RhiBindSlot s2[3] = {{1, RHI_BIND_SAMPLED_TEXTURE, FS},
                               {1, RHI_BIND_SAMPLER, FS},
                               {2, RHI_BIND_SAMPLED_TEXTURE, FS}};
    /* the VU group with its dynamic slots out of order: the root CBVs
     * still go in ascending slot order */
    const RhiBindSlot s4[4] = {{0, RHI_BIND_STORAGE_BUFFER, VS},
                               {3, RHI_BIND_UNIFORM_BUFFER_DYNAMIC, VS},
                               {1, RHI_BIND_UNIFORM_BUFFER_DYNAMIC, VS | FS},
                               {2, RHI_BIND_UNIFORM_BUFFER_DYNAMIC, VS}};
    D3dpLayout l0, l1, l2, l4, empty;
    CHECK(d3dp_LayoutBuild(s0, 1, &l0) && l0.resCount == 0 && l0.smpCount == 0 &&
              l0.dynCount == 1 && l0.table[0] == D3DP_ROOT_CBV && l0.offset[0] == 0 &&
              l0.dynSlot[0] == 0,
          "frame");
    CHECK(d3dp_LayoutBuild(s1, 1, &l1) && l1.dynCount == 1, "draw");
    CHECK(d3dp_LayoutBuild(s2, 3, &l2) && l2.resCount == 2 && l2.smpCount == 1, "tex");
    CHECK(l2.table[0] == D3DP_TABLE_RESOURCE && l2.offset[0] == 0 &&
              l2.table[1] == D3DP_TABLE_SAMPLER && l2.offset[1] == 0 &&
              l2.table[2] == D3DP_TABLE_RESOURCE && l2.offset[2] == 1,
          "tex table offsets");
    CHECK(l2.resStages == FS && l2.smpStages == FS, "pixel-only visibility");
    CHECK(d3dp_LayoutBuild(s4, 4, &l4) && l4.resCount == 1 && l4.resStages == VS &&
              l4.dynCount == 3,
          "vu");
    CHECK(l4.dynSlot[0] == 2 && l4.dynSlot[1] == 3 && l4.dynSlot[2] == 1 && l4.offset[2] == 0 &&
              l4.offset[3] == 1 && l4.offset[1] == 2,
          "vu root CBVs in slot order (b1, b2, b3)");
    CHECK(d3dp_LayoutFind(&l4, 0, RHI_BIND_STORAGE_BUFFER) == 0 &&
              d3dp_LayoutFind(&l4, 3, RHI_BIND_UNIFORM_BUFFER_DYNAMIC) == 1 &&
              d3dp_LayoutFind(&l4, 0, RHI_BIND_UNIFORM_BUFFER_DYNAMIC) == -1,
          "find");
    const RhiBindSlot dynDup[2] = {{1, RHI_BIND_UNIFORM_BUFFER, VS},
                                   {1, RHI_BIND_UNIFORM_BUFFER_DYNAMIC, VS}};
    D3dpLayout bad0;
    CHECK(!d3dp_LayoutBuild(dynDup, 2, &bad0), "b1 static and dynamic is rejected");
    const RhiBindSlot dyn5[5] = {{0, RHI_BIND_UNIFORM_BUFFER_DYNAMIC, VS},
                                 {1, RHI_BIND_UNIFORM_BUFFER_DYNAMIC, VS},
                                 {2, RHI_BIND_UNIFORM_BUFFER_DYNAMIC, VS},
                                 {3, RHI_BIND_UNIFORM_BUFFER_DYNAMIC, VS},
                                 {4, RHI_BIND_UNIFORM_BUFFER_DYNAMIC, VS}};
    CHECK(!d3dp_LayoutBuild(dyn5, 5, &bad0), "more than RHI_MAX_DYNAMIC_OFFSETS");
    CHECK(d3dp_LayoutBuild(NULL, 0, &empty) && empty.resCount == 0 && empty.smpCount == 0,
          "empty layout");
    const RhiBindSlot dup[2] = {{1, RHI_BIND_SAMPLED_TEXTURE, FS},
                                {1, RHI_BIND_STORAGE_BUFFER, FS}};
    D3dpLayout bad;
    CHECK(!d3dp_LayoutBuild(dup, 2, &bad), "t1 twice is rejected");
    const RhiBindSlot okPair[2] = {{1, RHI_BIND_UNIFORM_BUFFER, FS},
                                   {1, RHI_BIND_SAMPLED_TEXTURE, FS}};
    CHECK(d3dp_LayoutBuild(okPair, 2, &bad), "b1 and t1 are different registers");
    const RhiBindSlot badType[1] = {{0, RHI_BIND_COUNT, FS}};
    CHECK(!d3dp_LayoutBuild(badType, 1, &bad), "bad type");

    int8_t rp[RHI_MAX_BIND_SLOTS], sp[RHI_MAX_BIND_SLOTS], dp[RHI_MAX_BIND_SLOTS];
    const D3dpLayout *screen[3] = {&l0, &l1, &l2};
    CHECK(d3dp_RootParams(screen, 3, rp, sp, dp) == 4 && dp[0] == 0 && dp[1] == 1 && rp[0] == -1 &&
              rp[1] == -1 && rp[2] == 2 && sp[2] == 3 && sp[0] == -1 && rp[3] == -1 &&
              dp[2] == -1 && dp[3] == -1,
          "screen pipeline root parameters");
    const D3dpLayout *vu[3] = {&l0, &l4, &l2};
    CHECK(d3dp_RootParams(vu, 3, rp, sp, dp) == 7 && dp[0] == 0 && rp[1] == 1 && sp[1] == -1 &&
              dp[1] == 2 && rp[2] == 5 && sp[2] == 6,
          "VU pipeline root parameters: t0's table, then b1, b2, b3");
    /* rhi_test_common.c's layouts: TestCB dynamic, no group 1 */
    const D3dpLayout *test[3] = {&l0, &empty, &l2};
    CHECK(d3dp_RootParams(test, 3, rp, sp, dp) == 3 && rp[0] == -1 && dp[0] == 0 && rp[1] == -1 &&
              sp[1] == -1 && dp[1] == -1 && rp[2] == 1 && sp[2] == 2,
          "rhi_test_common.c's layouts");
    /* a static uniform stays in the resource table */
    const RhiBindSlot st0[1] = {{0, RHI_BIND_UNIFORM_BUFFER, VS | FS}};
    D3dpLayout ls0;
    CHECK(d3dp_LayoutBuild(st0, 1, &ls0) && ls0.resCount == 1 && ls0.dynCount == 0 &&
              ls0.table[0] == D3DP_TABLE_RESOURCE,
          "static");
    const D3dpLayout *stat[1] = {&ls0};
    CHECK(d3dp_RootParams(stat, 1, rp, sp, dp) == 1 && rp[0] == 0 && dp[0] == -1,
          "static uniform root parameters");
    /* every group full: a texture, a sampler and four root CBVs each fill the
     * root parameter array rhi_CreatePipeline sizes with D3DP_MAX_ROOT_PARAMS */
    const RhiBindSlot full[6] = {
        {0, RHI_BIND_SAMPLED_TEXTURE, FS},        {0, RHI_BIND_SAMPLER, FS},
        {0, RHI_BIND_UNIFORM_BUFFER_DYNAMIC, VS}, {1, RHI_BIND_UNIFORM_BUFFER_DYNAMIC, VS},
        {2, RHI_BIND_UNIFORM_BUFFER_DYNAMIC, VS}, {3, RHI_BIND_UNIFORM_BUFFER_DYNAMIC, VS}};
    D3dpLayout lf;
    CHECK(d3dp_LayoutBuild(full, 6, &lf) && lf.resCount == 1 && lf.smpCount == 1 &&
              lf.dynCount == RHI_MAX_DYNAMIC_OFFSETS,
          "full layout");
    const D3dpLayout *fullAll[RHI_MAX_BIND_SLOTS];
    for (uint32_t g = 0; g < RHI_MAX_BIND_SLOTS; g++) {
        fullAll[g] = &lf;
    }
    CHECK(d3dp_RootParams(fullAll, RHI_MAX_BIND_SLOTS, rp, sp, dp) == D3DP_MAX_ROOT_PARAMS &&
              dp[RHI_MAX_BIND_SLOTS - 1] == D3DP_MAX_ROOT_PARAMS - RHI_MAX_DYNAMIC_OFFSETS,
          "the most root parameters a pipeline can have");
}

/* ----------------------------------------------------------- small maths */
static void testMaths(void)
{
    CHECK(d3dp_CbvSize(0, 32, 32768) == 256, "small view rounds up");
    CHECK(d3dp_CbvSize(256, 0, 1024) == 768, "to the end");
    CHECK(d3dp_CbvSize(256, 0, 1000) == 512, "to the end, whole blocks");
    CHECK(d3dp_CbvSize(100, 32, 32768) == 0, "unaligned offset");
    CHECK(d3dp_CbvSize(32512, 32, 32768) == 256, "last block");
    CHECK(d3dp_CbvSize(32512, 300, 32768) == 0, "past the end");
    CHECK(d3dp_CbvSize(0, 70000, 1u << 20) == 0, "over 64 KiB");
    CHECK(d3dp_CbvSize(0, 0, 1u << 20) == 65536, "whole buffer clamps to 64 KiB");
    CHECK(d3dp_IntClearValue(7.0f) == 7.0f && d3dp_IntClearValue(1.4f) == 1.0f &&
              d3dp_IntClearValue(1.5f) == 2.0f && d3dp_IntClearValue(-3.0f) == 0.0f &&
              d3dp_IntClearValue(255.0f) == 255.0f,
          "integer clear values");
    CHECK(d3dp_Subresource(0, 1, 1) == 1 && d3dp_Subresource(2, 1, 4) == 6, "subresources");
    CHECK(d3dp_AlignUp(8, 256) == 256 && d3dp_AlignUp(512, 512) == 512 && d3dp_AlignUp(5, 1) == 5,
          "align");
}

/* --------------------------------------------------------- DXBC signatures */
static void testSignatures(void)
{
    D3dpSigElem e[16];
    CHECK(d3dp_IsDxbc(dxil_vs_main, sizeof(dxil_vs_main)), "vs is DXBC");
    CHECK(!d3dp_IsDxbc(spv_vs_main, sizeof(spv_vs_main)), "SPIR-V is not");
    int n = d3dp_ReadInputSignature(dxil_vs_main, sizeof(dxil_vs_main), e, 16);
    CHECK(n == 3, "vs_main has 3 inputs (%d)", n);
    if (n == 3) {
        CHECK(strcmp(e[0].name, "POSITION") == 0 && e[0].reg == 0 && e[0].systemValue == 0,
              "POSITION r0");
        CHECK(strcmp(e[1].name, "COLOR") == 0 && e[1].semanticIndex == 0 && e[1].reg == 1,
              "COLOR0 r1");
        CHECK(strcmp(e[2].name, "TEXCOORD") == 0 && e[2].reg == 2 && e[2].mask == 3,
              "TEXCOORD0 r2");
        CHECK(d3dp_LocationElement(e, n, 0) == 0 && d3dp_LocationElement(e, n, 2) == 2 &&
                  d3dp_LocationElement(e, n, 3) == -1,
              "locations");
    }
    n = d3dp_ReadInputSignature(dxil_ps_dual, sizeof(dxil_ps_dual), e, 16);
    CHECK(n == 3 && e[0].systemValue != 0, "ps_dual's inputs start with SV_Position");
    if (n == 3) {
        CHECK(d3dp_LocationElement(e, n, 0) == 1, "location 0 skips the system value");
    }
    /* malformed: truncated, a chunk offset out of range */
    CHECK(d3dp_ReadInputSignature(dxil_vs_main, 40, e, 16) == -1, "truncated");
    uint8_t copy[sizeof(dxil_vs_main)];
    memcpy(copy, dxil_vs_main, sizeof(copy));
    copy[32] = 0xFF;
    copy[33] = 0xFF;
    copy[34] = 0xFF;
    CHECK(!d3dp_IsDxbc(copy, sizeof(copy)), "bad chunk offset");
    memcpy(copy, dxil_vs_main, sizeof(copy));
    copy[0] = 'X';
    CHECK(d3dp_ReadInputSignature(copy, sizeof(copy), e, 16) == -1, "bad magic");
}

/* ------------------------------------------------------------------ pools */
static void testPools(void)
{
    D3dpPool p;
    CHECK(d3dp_PoolInit(&p, "t", 2, 8), "pool");
    void *e = NULL;
    uint32_t a = d3dp_PoolAlloc(&p, &e), b = d3dp_PoolAlloc(&p, NULL);
    CHECK(a && b && a != b && e && d3dp_PoolGet(&p, a) == e, "alloc");
    CHECK(d3dp_PoolAlloc(&p, NULL) == 0, "full");
    d3dp_PoolRelease(&p, a);
    CHECK(d3dp_PoolGet(&p, a) == NULL, "stale after release");
    uint32_t c = d3dp_PoolAlloc(&p, NULL);
    CHECK(c && c != a && (c & D3DP_INDEX_MASK) == (a & D3DP_INDEX_MASK), "reused, new generation");
    CHECK(d3dp_PoolGet(&p, a) == NULL && d3dp_PoolGet(&p, c) != NULL, "generations");
    CHECK(d3dp_PoolAt(&p, 0) != NULL && d3dp_PoolAt(&p, 5) == NULL, "walk");
    d3dp_PoolFree(&p);
    CHECK(d3dp_PoolGet(&p, c) == NULL, "freed pool");

    D3dpSlots s;
    uint32_t i0, i1, i2;
    CHECK(d3dp_SlotsInit(&s, 2), "slots");
    CHECK(d3dp_SlotsAlloc(&s, &i0) && d3dp_SlotsAlloc(&s, &i1) && i0 != i1, "two slots");
    CHECK(!d3dp_SlotsAlloc(&s, &i2), "slots full");
    d3dp_SlotsRelease(&s, i0);
    CHECK(d3dp_SlotsAlloc(&s, &i2) && i2 == i0, "slot reused");
    d3dp_SlotsFree(&s);
}

/* ------------------------------------------------- the game's shader table */
#ifdef ICO_HAVE_SHADER_TABLE
typedef struct SpvInput {
    uint32_t id, location;
    bool hasLocation, builtin, input;
    char name[48];
} SpvInput;

static int findOrAdd(SpvInput *all, int *nAll, uint32_t id)
{
    for (int q = 0; q < *nAll; q++) {
        if (all[q].id == id) {
            return q;
        }
    }
    if (*nAll >= 256) {
        return -1;
    }
    all[*nAll].id = id;
    return (*nAll)++;
}

/* The Input variables of a SPIR-V module with their Location and OpName. */
static int spvInputs(const uint8_t *code, size_t size, SpvInput *out, int max)
{
    const uint32_t *w = (const uint32_t *)(const void *)code;
    size_t n = size / 4, i = 5;
    int count = 0;
    SpvInput all[256];
    memset(all, 0, sizeof(all));
    int nAll = 0;
    if (n < 5 || w[0] != 0x07230203u) {
        return -1;
    }
    while (i < n) {
        uint32_t wc = w[i] >> 16, op = w[i] & 0xFFFFu;
        if (wc == 0 || i + wc > n) {
            return -1;
        }
        if (op == 5 && wc >= 3) { /* OpName */
            int k = findOrAdd(all, &nAll, w[i + 1]);
            if (k >= 0) {
                snprintf(all[k].name, sizeof(all[k].name), "%s", (const char *)&w[i + 2]);
            }
        } else if (op == 71 && wc >= 3) { /* OpDecorate */
            int k = findOrAdd(all, &nAll, w[i + 1]);
            if (k >= 0 && w[i + 2] == 30 && wc >= 4) { /* Location */
                all[k].location = w[i + 3];
                all[k].hasLocation = true;
            } else if (k >= 0 && w[i + 2] == 11) { /* BuiltIn */
                all[k].builtin = true;
            }
        } else if (op == 59 && wc >= 4 && w[i + 3] == 1) { /* OpVariable, Input */
            int k = findOrAdd(all, &nAll, w[i + 2]);
            if (k >= 0) {
                all[k].input = true;
            }
        }
        i += wc;
    }
    for (int k = 0; k < nAll; k++) {
        if (all[k].input && !all[k].builtin && all[k].hasLocation && count < max) {
            out[count++] = all[k];
        }
    }
    return count;
}

/* "in.var.COLOR0" -> "COLOR", 0 */
static bool splitSemantic(const char *varName, char *name, size_t size, uint32_t *index)
{
    const char *p = strncmp(varName, "in.var.", 7) == 0 ? varName + 7 : NULL;
    if (!p) {
        return false;
    }
    size_t len = strlen(p), end = len;
    while (end > 0 && isdigit((unsigned char)p[end - 1])) {
        end--;
    }
    *index = end < len ? (uint32_t)atoi(p + end) : 0u;
    if (end + 1 > size) {
        return false;
    }
    memcpy(name, p, end);
    name[end] = '\0';
    return true;
}

static bool sameText(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) {
            return false;
        }
    }
    return *a == *b;
}

static void testShaderTable(void)
{
    int vertexShaders = 0, inputs = 0;
    for (unsigned i = 0; i < g_icoShaderCount; i++) {
        const IcoShaderBlob *b = &g_icoShaders[i];
        static const uint8_t zero[16] = {0};
        CHECK(d3dp_IsDxbc(b->dxil, b->dxil_len) && memcmp(b->dxil + 4, zero, 16) != 0,
              "%s: DXIL is a signed DXBC container", b->name);
        if (b->stage != ICO_SHADER_STAGE_VERTEX) {
            continue;
        }
        vertexShaders++;
        D3dpSigElem e[16];
        int n = d3dp_ReadInputSignature(b->dxil, b->dxil_len, e, 16);
        CHECK(n >= 0 && n <= 16, "%s: input signature (%d)", b->name, n);
        SpvInput sv[16];
        int ns = spvInputs(b->spirv, b->spirv_len, sv, 16);
        CHECK(ns >= 0, "%s: SPIR-V parse", b->name);
        if (n < 0 || ns < 0) {
            continue;
        }
        int plain = 0;
        for (int k = 0; k < n && k < 16; k++) {
            plain += e[k].systemValue == 0;
        }
        CHECK(plain == ns, "%s: %d DXIL inputs, %d SPIR-V inputs", b->name, plain, ns);
        for (int k = 0; k < ns; k++) {
            char sem[48];
            uint32_t idx = 0;
            CHECK(splitSemantic(sv[k].name, sem, sizeof(sem), &idx), "%s: SPIR-V input name %s",
                  b->name, sv[k].name);
            int el = d3dp_LocationElement(e, n, sv[k].location);
            CHECK(el >= 0, "%s: no DXIL input for location %u", b->name, sv[k].location);
            if (el >= 0) {
                CHECK(sameText(e[el].name, sem) && e[el].semanticIndex == idx,
                      "%s: location %u is %s%u in SPIR-V but %s%u in DXIL", b->name, sv[k].location,
                      sem, idx, e[el].name, e[el].semanticIndex);
            }
            inputs++;
        }
    }
    printf("rhi_d3d12_plan_test: %d vertex shaders, %d inputs matched between DXIL and SPIR-V\n",
           vertexShaders, inputs);
    CHECK(vertexShaders > 0, "the table has vertex shaders");
}
#endif

int main(void)
{
    testStateTable();
    testBarrierPlan();
    testBuffers();
    testRings();
    testLayouts();
    testMaths();
    testSignatures();
    testPools();
#ifdef ICO_HAVE_SHADER_TABLE
    testShaderTable();
#else
    printf("rhi_d3d12_plan_test: no shader table in this build (DXC missing); skipped that part\n");
#endif
    if (failures) {
        printf("rhi_d3d12_plan_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("rhi_d3d12_plan_test: all checks passed\n");
    return 0;
}

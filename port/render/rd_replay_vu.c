/* rd_replay_vu.c: the replay's VU draws (rd_replay.c's command walk calls
 * rd__replay_vu for RDC_MESH, RDC_SKINNED, RDC_GRID and RDC_PARTICLES):
 * the VU stream, VuCB and VuBoneCB a draw binds, their group, and the draw
 * calls under the planned passes; and the vertex and triangle pre-passes
 * before list 0 (rd__vu_prepass: each mesh and grid command's vertices and
 * triangles once, each particle command's sprites once, into the record the
 * main shaders read).  The replay's state and helpers are rd_replay.h's. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/region.h" /* ico_region: the June 2001 demo draws its own VU programs */
#include "rd_internal.h"
#include "rd_mesh.h"
#include "rd_passring.h"
#include "rd_replay.h"
#include "shader_consts.h"

/* -------------------------------------------------------------- VU draws
 * RDC_MESH / RDC_SKINNED (rd_draw_vu_mesh), RDC_GRID (rd_draw_vu_grid) and
 * RDC_PARTICLES (rd_draw_vu_particles) through the vu_*.hlsl vertex shaders
 * and vu_ps, with the GS state of the replay state block at the command
 * (the material and register packets were decoded into state commands in
 * order with the meshes).  Static meshes: indexed triangle lists over the
 * mesh's stream and index list, copied into the ring once per replay.
 * Scissor batches (code 36) are two draws per batch: the triangles
 * SCISSOR_COMMON clips (ICO_VU_CUT_ONLY) with PRIM.ABE forced on, as the
 * fans' PRIM 0x5D has it, then the strip's own kicks (ICO_VU_KICK_ONLY). */

static uint32_t s_zeroBonesFor; /* replay counter of s_zeroBones */

static uint64_t s_zeroBones;

static uint64_t ringCopy(const void *data, uint64_t size, uint64_t align)
{
    uint64_t off = rd__ring_alloc(size ? size : 16, align);
    if (off != ~0ull && size) {
        memcpy(g_rd.ringMap[rd__ring_slot()] + off, data, size);
    }
    return off;
}

/* a uniform block (DrawCB, VuCB, VuBoneCB) into the ring, counted as one
   (RdPerfRecord.uploadUniformBytes) */
static uint64_t cbCopy(const void *data, uint64_t size, uint64_t align)
{
    const uint64_t off = ringCopy(data, size, align);
    if (off != ~0ull) {
        g_rdPerf.uploadUniformBytes += size;
    }
    return off;
}

/* ------------------------------------------------ the record's buffers
 * The vertex and triangle pre-passes (docs/port/RENDER.md, "The vertex and
 * triangle pre-passes"; rd__vu_prepass below) draw a record per vertex,
 * sprite and triangle into three RGBA32_UINT textures, ICO_VU_REC_W texels
 * a row, shared by the frame slots (each replay's two passes move them to
 * RENDER_TARGET and back to COPY_SRC twice), and copy them into the frame
 * slot's record buffer, the three planes one after the other, which the
 * main shaders and vu_ps read (group 1 slot 4, vu_common.hlsli
 * vu_rec_buf).  Both grow by powers of two from
 * RD_VU_REC_ROWS_MIN rows to RD_VU_REC_ROWS_MAX.  Every VU group binds
 * slot 4: the slot's record buffer, or s_recNull before there is one. */
#define RD_VU_REC_ROWS_MIN 16u
#define RD_VU_REC_ROWS_MAX 1024u

static RhiTexture s_recTex[ICO_VU_REC_PLANES];
static RhiState s_recTexState[ICO_VU_REC_PLANES];
static uint32_t s_recTexRows;
static RhiBuffer s_recBuf[RHI_FRAMES_IN_FLIGHT];
static uint32_t s_recBufRows[RHI_FRAMES_IN_FLIGHT];
static RhiBuffer s_recNull;

/* the bytes of a record buffer of rows rows (its three planes) */
static uint64_t recBytes(uint32_t rows)
{
    return (uint64_t)rows * ICO_VU_REC_W * ICO_VU_REC_BYTES;
}

/* the record buffer group 1 binds in this replay's frame slot, and its
 * bytes: the slot's record, else the 256-byte placeholder, else (it could
 * not be made: rd__vu_prepass_init said so) the slot's upload ring, which
 * no shader reads there while the slot has no record */
static RhiBuffer recBinding(uint64_t *bytes)
{
    if (s_recBuf[rd__ring_slot()].id) {
        *bytes = recBytes(s_recBufRows[rd__ring_slot()]);
        return s_recBuf[rd__ring_slot()];
    }
    *bytes = 256;
    return s_recNull.id ? s_recNull : g_rd.ring[rd__ring_slot()];
}

/* recEnsure's failures in a row: the second turns the pre-pass off */
static uint32_t s_recFails;

/* the record's textures and buffers (not s_recNull) */
static void recFree(void)
{
    for (uint32_t i = 0; i < ICO_VU_REC_PLANES; i++) {
        if (s_recTex[i].id) {
            rhi_destroy_texture(s_recTex[i]);
        }
        s_recTex[i] = (RhiTexture){0};
        s_recTexState[i] = RHI_STATE_UNDEFINED;
    }
    s_recTexRows = 0;
    for (uint32_t i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        if (s_recBuf[i].id) {
            rhi_destroy_buffer(s_recBuf[i]);
        }
        s_recBuf[i] = (RhiBuffer){0};
        s_recBufRows[i] = 0;
    }
}

/* The textures and every slot's buffer hold rows rows (every slot's at
 * once, so the replays after the one that grew them create nothing);
 * false when this replay's could not be made (it then draws the old way) */
static bool recEnsure(uint32_t rows)
{
    if (s_recTexRows < rows) {
        for (uint32_t i = 0; i < ICO_VU_REC_PLANES; i++) {
            if (s_recTex[i].id) {
                rhi_destroy_texture(s_recTex[i]);
            }
            s_recTex[i] = rhi_create_texture(
                &(RhiTextureDesc){ICO_VU_REC_W, rows, 1, RHI_FMT_RGBA32_UINT,
                                  RHI_TEX_RENDER_TARGET | RHI_TEX_COPY_SRC, "rd vu record", 0});
            s_recTexState[i] = RHI_STATE_UNDEFINED;
        }
        s_recTexRows = rows;
        for (uint32_t i = 0; i < ICO_VU_REC_PLANES; i++) {
            if (!s_recTex[i].id) {
                recFree();
                return false;
            }
        }
        rd__log("VU pre-pass: the record holds %u slots (%u rows, %llu KB a frame slot)",
                rows * ICO_VU_REC_W, rows, (unsigned long long)(recBytes(rows) / 1024u));
    }
    for (uint32_t i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        if (s_recBufRows[i] >= rows) {
            continue;
        }
        if (s_recBuf[i].id) {
            rhi_destroy_buffer(s_recBuf[i]); /* deferred past the frames that read it */
        }
        s_recBuf[i] = rhi_create_buffer(&(RhiBufferDesc){recBytes(rows),
                                                         RHI_BUF_STORAGE_READ | RHI_BUF_COPY_DST,
                                                         RHI_MEM_DEVICE, "rd vu record"});
        s_recBufRows[i] = s_recBuf[i].id ? rows : 0u;
    }
    return s_recBuf[rd__ring_slot()].id != 0;
}

/* t5's first byte in the slot's upload ring this replay (vu_ring: the
 * pre-pass's batches and the draw merge read their VuCB, VuBoneCB and
 * tables there): the ring's offset when the replay's VU work begins
 * (rd__vu_prepass, after the texture and mesh uploads), 256-aligned, so a
 * ring that a frame's texture uploads grew past the device's largest
 * storage range still binds every block the VU draws put after it */
static uint64_t s_ringBase;

/* The bytes of the slot's upload ring t5 binds: the ring from s_ringBase,
 * to the device's largest storage range (16-byte elements) */
static uint64_t ringRange(void)
{
    const uint64_t cap = g_rd.ringCap[rd__ring_slot()];
    uint64_t range = cap > s_ringBase ? cap - s_ringBase : 0u;
    const uint64_t maxRange = rhi_limits()->maxStorageRange;
    if (maxRange && range > maxRange) {
        range = maxRange;
    }
    return range & ~(uint64_t)15u;
}

/* a block of size bytes at ring offset off as t5 reads it: its first
 * float4 from s_ringBase; false when t5 does not hold it whole */
static bool ringIndex(uint64_t off, uint64_t size, uint32_t *out)
{
    if (off == ~0ull || off < s_ringBase || off - s_ringBase + size > ringRange() ||
        (off - s_ringBase) / 16u > UINT32_MAX) {
        return false;
    }
    *out = (uint32_t)((off - s_ringBase) / 16u);
    return true;
}

/* The VU layout's group binds the stream's whole buffer (the
 * mesh arena chunk or the ring) at t0, the ring at offset 0 for the
 * three dynamic uniforms, the slot's record buffer at t4 and the ring
 * again at t5 (from s_ringBase, ringRange), so it is made once per stream
 * buffer and replay;
 * a draw selects its stream through VuCB's vu_draw.x (the qword index of
 * its first batch, vu_common.hlsli) and its blocks through the bind-time
 * offsets.  A stream whose end lies past the device's largest storage range
 * gets a group of its own bound at its offset, vu_draw.x 0. */
#define RD_VU_GROUPS (RD_MESH_CHUNKS + 2) /* every arena chunk and the ring */

typedef struct VuEntry {
    uint32_t epoch, buffer, rec, ring;
    uint64_t range, base;
    RhiBindGroup group;
} VuEntry;

static VuEntry s_vuGroups[RD_VU_GROUPS];

static RhiBindGroup vuGroupMake(RhiBuffer streamBuf, uint64_t streamOff, uint64_t streamSize)
{
    RhiBinding b[6];
    memset(b, 0, sizeof(b));
    b[0].slot = 0;
    b[0].type = RHI_BIND_STORAGE_BUFFER;
    b[0].buffer = streamBuf; /* the mesh arena, or the ring */
    b[0].offset = streamOff;
    b[0].size = streamSize ? streamSize : 16;
    static const uint32_t slot[3] = {1, 2, 3};
    static const uint32_t size[3] = {sizeof(IcoDrawCB), sizeof(IcoVuCB), sizeof(IcoVuBoneCB)};
    for (int i = 0; i < 3; i++) {
        b[1 + i].slot = slot[i];
        b[1 + i].type = RHI_BIND_UNIFORM_BUFFER_DYNAMIC;
        b[1 + i].buffer = g_rd.ring[rd__ring_slot()];
        b[1 + i].offset = 0;
        b[1 + i].size = size[i];
    }
    b[4].slot = 4;
    b[4].type = RHI_BIND_STORAGE_BUFFER;
    b[4].buffer = recBinding(&b[4].size);
    /* t5, the ring the pre-pass's batch modules stream from: the layout
     * keeps the slot everywhere, but Direct3D 11 never makes a batch
     * module (no pre-pass there) nor a merge module (vuMergeInit: the
     * merge is off there), and a bound storage range is rewritten
     * up to the written mark at every submit (d3d11_cmd.c, the SRV part's
     * touch): there the bind is skipped, so a submit writes only the bytes
     * its draws stream (the slot reads zeros, which no module reads) */
    if (!g_rd.vuPreBatch && !g_rd.vuMerge && rhi_backend() == RHI_BACKEND_D3D11) {
        return rd__replay_bind_group(g_rd.layoutVu, b, 5);
    }
    b[5].slot = 5;
    b[5].type = RHI_BIND_STORAGE_BUFFER;
    b[5].buffer = g_rd.ring[rd__ring_slot()];
    b[5].offset = s_ringBase;
    b[5].size = ringRange();
    return rd__replay_bind_group(g_rd.layoutVu, b, 6);
}

/* The group for a stream at streamOff of streamBuf (bufSize bytes); *base
 * receives the stream's first qword in the bound range (vu_draw.x). */
static RhiBindGroup vuGroup(RhiBuffer streamBuf, uint64_t bufSize, uint64_t streamOff,
                            uint64_t streamSize, uint32_t *base)
{
    uint64_t range = bufSize;
    const uint64_t maxRange = rhi_limits()->maxStorageRange;
    if (maxRange && range > maxRange) {
        range = maxRange & ~(uint64_t)15u;
    }
    if (streamOff + (streamSize ? streamSize : 16) > range || (streamOff & 15u) ||
        streamOff / 16u > UINT32_MAX) {
        *base = 0;
        return vuGroupMake(streamBuf, streamOff, streamSize);
    }
    *base = (uint32_t)(streamOff / 16u);
    uint64_t recSize;
    const uint32_t rec = recBinding(&recSize).id;
    VuEntry *free = NULL;
    for (int i = 0; i < RD_VU_GROUPS; i++) {
        VuEntry *e = &s_vuGroups[i];
        if (e->epoch == rd__replay_bind_epoch() && e->buffer == streamBuf.id && e->rec == rec &&
            e->ring == g_rd.ring[rd__ring_slot()].id && e->range == range &&
            e->base == s_ringBase) {
            return e->group;
        }
        if (!free && e->epoch != rd__replay_bind_epoch()) {
            free = e;
        }
    }
    const RhiBindGroup g = vuGroupMake(streamBuf, 0, range);
    if (g.id && free) {
        free->epoch = rd__replay_bind_epoch();
        free->buffer = streamBuf.id;
        free->rec = rec;
        free->ring = g_rd.ring[rd__ring_slot()].id;
        free->range = range;
        free->base = s_ringBase;
        free->group = g;
    }
    return g;
}

/* The VU vertex shader of a recorded (RdProg, code). */
static uint8_t vuVs(uint8_t prog, uint8_t code)
{
    switch (prog) {
    case RD_PROG_PRELIT:
        return RD_VS_VU_PRELIT;
    case RD_PROG_LIT:
        return RD_VS_VU_LIT;
    case RD_PROG_LIT_SPEC:
        return RD_VS_VU_LIT_SPEC;
    case RD_PROG_REFLECT:
        return RD_VS_VU_REFLECT;
    case RD_PROG_SKIN:
        return RD_VS_VU_SKIN;
    case RD_PROG_SKIN_SPEC:
        return code == 24 ? RD_VS_VU_SKIN_DEBUG : RD_VS_VU_SKIN_SPEC;
    case RD_PROG_GRID:
        return RD_VS_VU_GRID;
    case RD_PROG_GRID_LIT:
        return code == 24 ? RD_VS_VU_GRID_SPEC : RD_VS_VU_GRID_LIT;
    default:
        return RD_VS_VU_PARTICLE;
    }
}

/* The pass split (g_rd.passSplit, ICO_RD_PASS_SPLIT / [dev] pass_split;
 * off by default): after every N VU draws of a render pass the pass is
 * ended and begun again on the same targets, both loaded, as dstPassMode
 * begins a pass again (the scissor taken back), so a GPU that holds a
 * pass's draws in one job gets them in jobs of N.  Not a pass of the loss
 * bisect's window (g_rdWin.noCount).  Refused, logged once, in a pass the
 * split would change the picture or the exact blend of: a multisampled
 * one (the MSAA epoch), a storage pass, one the exact blend's attachment
 * read made readable, or inside a draw taking DATE through the stencil bit
 * (r->dateStNow: its bit lives in this pass only).  A pass readable for
 * DATE's stencil bit or the fog's local read alone is split: the pass
 * begun again is readable as before, and the next DATE draw takes the bit
 * again in it (rd__replay_date_take's per-pass rule). */
static uint32_t s_splitSerial, s_splitCount;
/* the As range of the VU command being drawn (doVuDraw), which its
 * vuDraw calls plan with (rd_alpha_plan.c) */
static RdAlphaRange s_vuAlpha = {0, 255};

static void passSplit(Replay *r)
{
    if (!r->passOpen) {
        return;
    }
    if (s_splitSerial != r->passSerial) {
        s_splitSerial = r->passSerial;
        s_splitCount = 0;
    }
    if (++s_splitCount < g_rd.passSplit) {
        return;
    }
    s_splitCount = 0;
    if (r->passStorage || (r->passReadable && rd__replay_exact_readable(r->passColor)) ||
        r->dateStNow || g_rd.passSamples > 1 || rd__replay_msaa_want() > 1u) {
        static bool logged; /* once a run */
        if (!logged) {
            logged = true;
            rd__log("pass split: not in a multisampled or storage pass, the exact blend's "
                    "readable one or a DATE draw through the stencil bit (those are drawn "
                    "whole)");
        }
        return;
    }
    const uint32_t cid = r->passColor, did = r->passDepth;
    RdTargetRec *c = rd__target_rec(cid);
    if (!c) {
        return;
    }
    const RhiRect sc = r->sc;
    g_rdWin.noCount = true;
    rd__replay_begin_pass(r, c, did ? rd__target_rec(did) : NULL, cid, did, RHI_LOAD_LOAD, NULL,
                          RHI_LOAD_LOAD, 0.0f, false);
    g_rdWin.noCount = false;
    r->sc = sc;
    rhi_cmd_set_scissor(rd__replay_cl(), &sc);
    s_splitSerial = r->passSerial;
}

/* The draw-merge count (RdPerfRecord vuMergeDraws, vuMergeRuns,
 * vuMergeBlocked; counting only, beside what the merge draws: vuMergeIn,
 * vuMergeGpu, vuMergeAlone).  The last VU draw's tuple: a draw equal to it
 * on every key, with the predicates a merged draw needs, could join its run
 * in one GPU draw.  A new pass, a draw of anything else between
 * (g_rd.stats.draws moved past what the VU draws drew, s_mergeExpect) or a
 * draw that cannot merge ends the run. */
typedef struct VuMergeLast {
    bool valid;
    uint32_t replay, pass, pipe, g1, g2, frame, frameOff, color;
    uint64_t cbHash;
    RhiRect sc;
} VuMergeLast;

static VuMergeLast s_mergeLast;

/* g_rd.stats.draws as it stands when nothing but the VU draws of this file
 * drew since the last note (ownDraw keeps it in step) */
static uint32_t s_mergeExpect;

/* a GPU draw of the VU draws: counted, and in step for the note */
static void ownDraw(void)
{
    g_rd.stats.draws++;
    s_mergeExpect++;
}

static uint64_t vuFnv(const void *data, size_t n)
{
    const uint8_t *b = data;
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) {
        h = (h ^ b[i]) * 1099511628211ull;
    }
    return h;
}

/* eligible: single planned pass, no DATE stencil, no destination read, not
 * skinned.  Called once a VU draw is drawn or held in the merge's run */
static void vuMergeNote(const Replay *r, bool eligible, uint32_t pipe, uint32_t g1, uint32_t g2,
                        const IcoDrawCB *cb)
{
    const int list = rd__replay_list();
    if (list < 0 || list >= RD_LIST_COUNT) {
        return;
    }
    VuMergeLast t;
    memset(&t, 0, sizeof(t));
    t.valid = eligible;
    t.replay = g_rdPerf.replay;
    t.pass = r->passSerial;
    t.pipe = pipe;
    t.g1 = g1;
    t.g2 = g2;
    t.frame = r->frameBG.group.id;
    t.frameOff = r->frameBG.offset;
    t.color = r->st.color;
    t.cbHash = vuFnv(cb, sizeof(*cb));
    t.sc = r->sc;
    const VuMergeLast *l = &s_mergeLast;
    const bool samePass = l->valid && l->replay == t.replay && l->pass == t.pass;
    /* nothing drawn between but the VU draws' own */
    const bool adjacent = samePass && g_rd.stats.draws == s_mergeExpect;
    if (adjacent && eligible && l->pipe == t.pipe && l->g2 == t.g2 && l->g1 == t.g1 &&
        l->cbHash == t.cbHash && l->frame == t.frame && l->frameOff == t.frameOff &&
        l->color == t.color && memcmp(&l->sc, &t.sc, sizeof(t.sc)) == 0) {
        g_rdPerf.vuMergeDraws[list]++;
    } else {
        g_rdPerf.vuMergeRuns[list]++;
        if (samePass) {
            uint32_t why = 4;
            if (l->pipe != t.pipe) {
                why = 0;
            } else if (l->g2 != t.g2 || l->g1 != t.g1) {
                why = 1;
            } else if (l->cbHash != t.cbHash) {
                why = 2;
            } else if (!eligible || !adjacent) {
                why = 3;
            }
            g_rdPerf.vuMergeBlocked[why]++;
        }
    }
    s_mergeLast = t;
    s_mergeExpect = g_rd.stats.draws;
}

/* ------------------------------------------------------ the merge's run
 * docs/port/VU1_PROGRAMS.md, "The draw merge".  With g_rd.vuMerge a VU
 * draw of one planned pass, indexed, through a merge build (vsMerge), with
 * no DATE through the stencil bit, no destination read, outside the loss
 * bisect's window and the pass split, is held in the open run instead of
 * drawn (vuRunTake); code 36's cut and kick draws too, since the merge
 * builds read each draw's own VuCB (its ICO_VU_CUT_ONLY / KICK_ONLY flag)
 * through the table, though a cut (ABE forced on) and its kick seldom
 * share a pipeline key.  The next
 * such draw joins it when it has the same pipeline key, group 1 (the
 * stream buffer), group 2 (the textures), DrawCB bytes, FrameCB, scissor,
 * pass and GPU timing effect, up to RD_VU_MERGE_MAX draws whose raised
 * index values stay within the device's (maxDrawIndexedIndexValue).
 * Anything else recorded draws the run first (rd__replay_vu_flush: the
 * replay's endPass and rd__replay_flush_run, the walk before every command
 * but the VU draws and the state, a change of the timing effect, the
 * list's end, a VU draw under DATE, any VU draw drawn alone).  The run is
 * one draw: a run of one its own list; a longer one its draws' index lists
 * copied into the ring one after the other, each but the first raised past
 * the values before it, and a table in the ring (vu_common.hlsli
 * vu_merge_entry) giving each draw's first raised value, VuCB, VuBoneCB
 * and raise, the merge builds reading each draw's VuCB through t5 by it.
 * The same triangles in the same order through the same words: the
 * picture is the same byte for byte. */
#define RD_VU_MERGE_MAX 16u /* vu_common.hlsli VU_MERGE_MAX */

/* A VU draw's index list: the bound list (buf at off; id 0: not indexed),
 * the same values on the CPU (a mesh's RdMeshRec.index) or, cpu NULL, a
 * grid's shape (ICO_VU_INDEX(b * vpb + v, c) for v 2 .. vpb - 1 of each
 * batch b, doVuDraw) */
typedef struct VuIdx {
    RhiBuffer buf;
    uint64_t off;
    const uint32_t *cpu;
    uint32_t gridBatches, gridVpb;
} VuIdx;

typedef struct VuRunDraw {
    IcoVuCB vcb;
    uint64_t bonesOff;
    VuIdx ix;
    uint32_t first, count;
    uint32_t lo, hi; /* its values' range (vuRunRange) */
    bool ranged;
} VuRunDraw;

static struct {
    uint32_t n; /* the draws held; 0: no run */
    uint32_t replay, pass;
    int post;
    RdPipeKeyInt key;
    RhiPipeline p;
    RhiBindGroup g1, g2;
    RdUniform frame;
    RhiRect sc;
    IcoDrawCB cb;
    uint64_t dOff;
    uint64_t top; /* the next draw's first raised value (two draws or more) */
    VuRunDraw d[RD_VU_MERGE_MAX];
} s_run;

/* the largest index value a draw may hold (never the restart value
 * 0xFFFFFFFF) */
static uint32_t maxIndexValue(void)
{
    const uint32_t m = rhi_limits()->maxDrawIndexedIndexValue;
    return m == 0u ? 0xFFFFFFu : (m < 0xFFFFFFFEu ? m : 0xFFFFFFFEu);
}

/* the span instrumentation's list or range (rd_core.c rd__span_*) of a
 * draw's index list */
static void vuSpanOf(const VuIdx *ix)
{
    if (ix->cpu) {
        rd__span_idx(ix->cpu);
    } else if (ix->gridVpb >= 3u && ix->gridBatches > 0u) {
        rd__span_range(ICO_VU_INDEX(2u, 0u), ICO_VU_INDEX(ix->gridBatches * ix->gridVpb - 1u, 2u));
    } else {
        rd__span_idx(NULL);
    }
}

/* a held draw's lowest and highest index value */
static void vuRunRange(VuRunDraw *d)
{
    if (d->ranged) {
        return;
    }
    if (d->ix.cpu) {
        uint32_t lo = UINT32_MAX, hi = 0;
        const uint32_t *v = d->ix.cpu + d->first;
        for (uint32_t i = 0; i < d->count; i++) {
            lo = v[i] < lo ? v[i] : lo;
            hi = v[i] > hi ? v[i] : hi;
        }
        d->lo = lo;
        d->hi = hi;
    } else {
        d->lo = ICO_VU_INDEX(2u, 0u);
        d->hi = ICO_VU_INDEX(d->ix.gridBatches * d->ix.gridVpb - 1u, 2u);
    }
    d->ranged = true;
}

/* a held draw's index values raised by add, into out (its count) */
static void vuRunWrite(const VuRunDraw *d, uint32_t add, uint32_t *out)
{
    if (d->ix.cpu) {
        const uint32_t *v = d->ix.cpu + d->first;
        for (uint32_t i = 0; i < d->count; i++) {
            out[i] = v[i] + add;
        }
        return;
    }
    const uint32_t vpb = d->ix.gridVpb;
    uint32_t k = 0;
    for (uint32_t b = 0; b < d->ix.gridBatches; b++) {
        for (uint32_t v = 2; v < vpb && k < d->count; v++) {
            for (uint32_t c = 0; c < 3u && k < d->count; c++) {
                out[k++] = ICO_VU_INDEX(b * vpb + v, c) + add;
            }
        }
    }
}

/* a run that could not be drawn (the ring full or past t5; a pass ended
 * without drawing it, which every endPass prevents): logged once */
static void vuRunLost(uint32_t n, const char *why)
{
    static bool logged; /* once a run */
    if (!logged) {
        logged = true;
        rd__log("VU merge: a run of %u draws not drawn (%s; reported once)", n, why);
    }
}

void rd__replay_vu_flush(Replay *r)
{
    const uint32_t n = s_run.n;
    if (n == 0) {
        return;
    }
    s_run.n = 0;
    if (s_run.replay != g_rd.replayCounter || !r->passOpen || s_run.pass != r->passSerial) {
        vuRunLost(n, "its pass ended first");
        return;
    }
    const RhiCommandList cl = rd__replay_cl();
    const uint64_t ua = rhi_limits()->uniformAlign;
    /* the table: x the draw's first raised value (draw 0: the draws), y
     * its VuCB, z its VuBoneCB (float4 indices in t5), w its raise */
    uint32_t tab[RD_VU_MERGE_MAX][4];
    uint32_t tAt;
    const uint64_t tOff = rd__ring_alloc((uint64_t)n * 16u, 16);
    if (!ringIndex(tOff, (uint64_t)n * 16u, &tAt)) {
        vuRunLost(n, "the ring full or its table past t5");
        return;
    }
    uint64_t vOff0 = 0;
    uint32_t total = 0, next = 0;
    for (uint32_t k = 0; k < n; k++) {
        VuRunDraw *d = &s_run.d[k];
        d->vcb.mark[3] = tAt; /* vu_merge_head.w: IcoVuCB.mark's lane nothing else reads */
        const uint64_t vOff = cbCopy(&d->vcb, sizeof(d->vcb), ua);
        uint32_t vAt, bAt;
        if (!ringIndex(vOff, sizeof(IcoVuCB), &vAt) ||
            !ringIndex(d->bonesOff, sizeof(IcoVuBoneCB), &bAt)) {
            vuRunLost(n, "the ring full or a VuCB past t5");
            return;
        }
        if (k == 0) {
            vOff0 = vOff;
        }
        uint32_t add = 0;
        if (n > 1) {
            vuRunRange(d); /* every draw of a run of two or more was ranged (vuRunTake) */
            if (k == 0) {
                next = d->hi + 1u;
            } else {
                add = next - d->lo;
                tab[k][0] = next;
                next += d->hi - d->lo + 1u;
            }
        }
        tab[k][1] = vAt;
        tab[k][2] = bAt;
        tab[k][3] = add;
        total += d->count;
    }
    tab[0][0] = n;
    memcpy(g_rd.ringMap[rd__ring_slot()] + tOff, tab, (size_t)n * 16u);
    /* the index list: a run of one its own, a longer one the draws' lists
     * raised one after the other */
    const VuRunDraw *d0 = &s_run.d[0];
    RhiBuffer ib = d0->ix.buf;
    uint64_t ibOff = d0->ix.off;
    uint32_t first = d0->first, count = d0->count;
    if (n > 1) {
        const uint64_t iOff = rd__ring_alloc((uint64_t)total * 4u, 16);
        if (iOff == ~0ull) {
            vuRunLost(n, "the ring full");
            return;
        }
        uint32_t *out = (uint32_t *)(g_rd.ringMap[rd__ring_slot()] + iOff);
        for (uint32_t k = 0, at = 0; k < n; k++) {
            vuRunWrite(&s_run.d[k], tab[k][3], out + at);
            at += s_run.d[k].count;
        }
        g_rdPerf.uploadStreamBytes += (uint64_t)total * 4u;
        ib = g_rd.ring[rd__ring_slot()];
        ibOff = iOff;
        first = 0;
        count = total;
        rd__span_range(d0->lo, next - 1u);
        g_rdPerf.vuMergeIn += n;
        g_rdPerf.vuMergeGpu++;
    } else {
        vuSpanOf(&d0->ix);
        g_rdPerf.vuMergeAlone++;
    }
    /* the run's scissor where the draw being bound set its own since (it
     * is set back after) */
    const RhiRect now = r->sc;
    const bool scMoved = memcmp(&now, &s_run.sc, sizeof(now)) != 0;
    if (scMoved) {
        rhi_cmd_set_scissor(cl, &s_run.sc);
    }
    rhi_cmd_set_index_buffer(cl, ib, ibOff, true);
    /* b2 the first draw's VuCB (its mark[3] the table), b3 its VuBoneCB
       (the merge builds read neither else) */
    const uint32_t offs[3] = {(uint32_t)s_run.dOff, (uint32_t)vOff0, (uint32_t)d0->bonesOff};
    rd__set_pipeline(cl, s_run.p, &s_run.key);
    rd__bind_uniform(cl, 0, s_run.frame);
    rhi_cmd_set_bind_group_offsets(cl, 1, s_run.g1, offs, 3);
    rhi_cmd_set_bind_group(cl, 2, s_run.g2);
    rd__replay_dst_bind(r, &s_run.key); /* nothing for these keys */
    rd__span_draw(first, count);
    rd__replay_draw_indexed(cl, count, first, 0, 1);
    ownDraw();
    if (scMoved) {
        rhi_cmd_set_scissor(cl, &now);
    }
}

/* Holds a draw in the open run, or (it does not join) draws the run and
 * opens one with it; false when it cannot be held (its DrawCB not in the
 * ring): the caller draws it alone */
static bool vuRunTake(Replay *r, const RdPipeKeyInt *key, RhiPipeline p, RhiBindGroup g1,
                      RhiBindGroup g2, const IcoDrawCB *cb, const IcoVuCB *vcb, uint64_t bonesOff,
                      const VuIdx *ix, uint32_t first, uint32_t count)
{
    VuRunDraw nd;
    nd.vcb = *vcb;
    nd.bonesOff = bonesOff;
    nd.ix = *ix;
    nd.first = first;
    nd.count = count;
    nd.lo = nd.hi = 0;
    nd.ranged = false;
    if (s_run.n && s_run.n < RD_VU_MERGE_MAX && s_run.replay == g_rd.replayCounter && r->passOpen &&
        s_run.pass == r->passSerial && s_run.post == r->perfPost && s_run.p.id == p.id &&
        memcmp(&s_run.key, key, sizeof(*key)) == 0 && s_run.g1.id == g1.id &&
        s_run.g2.id == g2.id && s_run.frame.group.id == r->frameBG.group.id &&
        s_run.frame.offset == r->frameBG.offset && memcmp(&s_run.sc, &r->sc, sizeof(r->sc)) == 0 &&
        memcmp(&s_run.cb, cb, sizeof(*cb)) == 0) {
        /* raised past the run's values, within the device's */
        if (s_run.n == 1) {
            vuRunRange(&s_run.d[0]);
            s_run.top = (uint64_t)s_run.d[0].hi + 1u;
        }
        vuRunRange(&nd);
        const uint64_t last = s_run.top + (nd.hi - nd.lo);
        if (last <= maxIndexValue()) {
            s_run.d[s_run.n++] = nd;
            s_run.top = last + 1u;
            return true;
        }
    }
    rd__replay_vu_flush(r);
    const uint64_t dOff = rd__draw_cb_offset(cb);
    if (dOff == ~0ull) {
        return false;
    }
    s_run.replay = g_rd.replayCounter;
    s_run.pass = r->passSerial;
    s_run.post = r->perfPost;
    s_run.key = *key;
    s_run.p = p;
    s_run.g1 = g1;
    s_run.g2 = g2;
    s_run.frame = r->frameBG;
    s_run.sc = r->sc;
    s_run.cb = *cb;
    s_run.dOff = dOff;
    s_run.top = 0;
    s_run.d[0] = nd;
    s_run.n = 1;
    return true;
}

void rd__replay_vu_flush_post(Replay *r, int post)
{
    if (s_run.n && s_run.post != post) {
        rd__replay_vu_flush(r); /* the run's draw belongs to the effect before */
    }
}

/* One VU draw call under the planned passes of state s.  vs: the vertex
 * shader, RD_VS_VU_MAIN or RD_VS_VU_PARTICLE_MAIN for a command the
 * pre-passes recorded (vu_main_vs's pipelines carry RD_PROG_PRELIT whatever
 * the program: rd_mesh.c enumerateVuDraws), else the program's own entry
 * progVs, which the draw also takes when the main shader's pipeline cannot
 * be made.  Both draw with vu_ps / vu_texa_ps (the planned passes' ids:
 * VuVSOut, the planes as flat varyings either way).  ix: its index list
 * (not indexed: buf 0), first and count in it (vertices when not indexed).
 * With the draw merge (g_rd.vuMerge) a draw that can merge is held in the
 * run (vuRunTake); any other draws the run first, then itself, a table of
 * one where a pass goes through a merge build. */
static void vuDraw(Replay *r, const RdStateBlock *s, const DrawSetup *ds, RhiBindGroup g2,
                   uint8_t prog, uint8_t vs, uint8_t progVs, RhiBuffer streamBuf,
                   uint64_t streamBufSize, uint64_t streamOff, uint64_t streamSize,
                   const IcoVuCB *vcb0, uint64_t bonesOff, const VuIdx *ix, uint32_t first,
                   uint32_t count)
{
    if (count == 0) {
        return;
    }
    const bool indexed = ix->buf.id != 0;
    const bool recorded = vs == RD_VS_VU_MAIN || vs == RD_VS_VU_PARTICLE_MAIN;
    if (g_rd.vuPrepass && !recorded && (rd__vu_pre_vs(progVs) || progVs == RD_VS_VU_PARTICLE)) {
        g_rdPerf.vuPreFallbacks++; /* a VU draw the pre-passes did not record */
    }
    /* one group per stream buffer; the stream's place in it
     * goes into vu_draw.x */
    IcoVuCB vcbAt = *vcb0;
    const IcoVuCB *vcb = &vcbAt;
    if (g_rdWin.first && g_rdWin.filter == 1) {
        /* the loss bisect's MASK_MARKS: every triangle drawn unmarked */
        memset(vcbAt.mark, 0, sizeof(vcbAt.mark));
    }
    const RhiBindGroup g1 =
        vuGroup(streamBuf, streamBufSize, streamOff, streamSize, &vcbAt.draw[0]);
    if (!g1.id) {
        return;
    }
    const uint64_t ua = rhi_limits()->uniformAlign;
    RdDrawPass pl[2], dp[8];
    int npl = rd__plan_screen_draw(s, RD_PRIM_TRIANGLES, RD_SPACE_WORLD, ds->tc->format,
                                   ds->depthFmt, pl);
    rd__plan_screen_draw_alpha(s, s_vuAlpha, pl, &npl); /* the command's As range (doVuDraw) */
    for (int i = 0; i < npl; i++) {
        /* the meshes' main shader serves every program (RD_PROG_PRELIT);
         * the sprites' keeps the particles' program */
        pl[i].key.gs.program = vs == RD_VS_VU_MAIN ? RD_PROG_PRELIT : prog;
        pl[i].key.vs = vs;
        /* the planner's sprite_texa_ps becomes vu_texa_ps */
        pl[i].key.fs = pl[i].key.fs == RD_FS_SPRITE_TEXA ? RD_FS_VU_TEXA : RD_FS_VU;
        if (vcb->batch[2] != 0u) {
            /* a clip plane (rd_chain.h): the pixels past it are discarded,
             * which the exact blend's storage path must know (rd_dst_read.c
             * passDiscards) */
            pl[i].flags |= ICO_DF_PLANE;
        }
        /* the pixel side at a fractional scale (rd_pixel_plan.c) */
        pl[i].key.fs = (uint8_t)rd__vu_pixel_fs((RdFsId)pl[i].key.fs, s, ds->tc, &pl[i]);
    }
    /* the exact blend's passes, two passes without dual-source blend */
    const int np = rd__replay_expand_passes(r, pl, npl, dp);
    /* each pass's pipeline: the main shader's, or (it could not be made)
     * the program's own entry, which ignores the record (logged once) */
    RdPipeKeyInt keys[8];
    RhiPipeline pipes[8];
    bool own = false;
    for (int i = 0; i < np; i++) {
        keys[i] = dp[i].key;
        pipes[i] = rd__get_pipeline(&keys[i]);
        if (!pipes[i].id && recorded && !rd__pipeline_gathering()) {
            static bool logged; /* once a run */
            if (!logged) {
                logged = true;
                rd__log("VU pre-pass: a pipeline of %s could not be made; its draws take their "
                        "programs' own vertex shaders (reported once)",
                        rd__vs_name(vs));
            }
            keys[i].vs = progVs;
            keys[i].gs.program = prog;
            pipes[i] = rd__get_pipeline(&keys[i]);
            g_rdPerf.vuPreFallbacks++;
            own = true;
        }
    }
    /* M0's predicates for the count (vuMergeNote) */
    const bool noteable = np == 1 && !r->dateStNow && dp[0].key.dst == RHI_DST_READ_NONE &&
                          s_zeroBonesFor == g_rd.replayCounter && bonesOff == s_zeroBones;
    const RdStateBlock saved = r->st;
    r->st = *s; /* fillDrawCB reads the state the passes were planned from */
    if (g_rd.vuMerge && np == 1 && indexed && pipes[0].id && !own && g_rd.vsMerge[keys[0].vs].id &&
        !r->dateStNow && keys[0].dst == RHI_DST_READ_NONE && !g_rdWin.first && !g_rd.passSplit &&
        r->passOpen) {
        /* the draw merge: held in the run (drawn when it ends) */
        IcoDrawCB cb;
        rd__replay_fill_draw_cb(r, &dp[0], ds, &cb);
        rd__replay_dst_pass_mode(r, &keys[0]); /* a storage pass ends first (its run drawn) */
        if (vuRunTake(r, &keys[0], pipes[0], g1, g2, &cb, vcb, bonesOff, ix, first, count)) {
            if (dp[0].key.gs.colorMask & 8) {
                r->writeSerial++;
            }
            vuMergeNote(r, noteable, pipes[0].id, g1.id, g2.id, &cb);
            r->st = saved;
            return;
        }
    }
    rd__replay_vu_flush(r); /* the run's draw before this one's */
    /* a merge build reads VuCB through a table, here of this draw alone */
    bool table = false;
    for (int i = 0; i < np; i++) {
        table = table || (g_rd.vuMerge && pipes[i].id && g_rd.vsMerge[keys[i].vs].id);
    }
    uint64_t tOff = ~0ull;
    uint32_t tAt = 0;
    if (table) {
        tOff = rd__ring_alloc(16, 16);
        if (!ringIndex(tOff, 16, &tAt)) {
            vuRunLost(1, "the ring full or its table past t5");
            r->st = saved;
            return;
        }
        vcbAt.mark[3] = tAt; /* vu_merge_head.w */
    }
    /* VuCB once a draw: every pass of it reads the same block */
    const uint64_t vOff = cbCopy(vcb, sizeof(*vcb), ua);
    if (vOff == ~0ull) {
        r->st = saved;
        return;
    }
    if (table) {
        uint32_t t[4] = {1u, 0u, 0u, 0u};
        if (!ringIndex(vOff, sizeof(IcoVuCB), &t[1]) ||
            !ringIndex(bonesOff, sizeof(IcoVuBoneCB), &t[2])) {
            vuRunLost(1, "a VuCB past t5");
            r->st = saved;
            return;
        }
        memcpy(g_rd.ringMap[rd__ring_slot()] + tOff, t, sizeof(t));
        g_rdPerf.vuMergeAlone++;
    }
    if (indexed) {
        rhi_cmd_set_index_buffer(rd__replay_cl(), ix->buf, ix->off, true);
        vuSpanOf(ix);
    }
    for (int i = 0; i < np; i++) {
        const RdPipeKeyInt key = keys[i];
        const RhiPipeline p = pipes[i];
        if (!p.id) {
            continue;
        }
        IcoDrawCB cb;
        rd__replay_fill_draw_cb(r, &dp[i], ds, &cb);
        /* PC port: through the 2D draws' content cache, so equal DrawCBs
         * (across draws and passes) share one ring copy and one b1 offset */
        const uint64_t dOff = rd__draw_cb_offset(&cb);
        if (dOff == ~0ull) {
            continue;
        }
        const uint32_t offs[3] = {(uint32_t)dOff, (uint32_t)vOff, (uint32_t)bonesOff};
        rd__replay_dst_pass_mode(r, &dp[i].key); /* a storage pass for a storage destination read */
        rd__set_pipeline(rd__replay_cl(), p, &key);
        rd__bind_uniform(rd__replay_cl(), 0, r->frameBG);
        rhi_cmd_set_bind_group_offsets(rd__replay_cl(), 1, g1, offs, 3);
        rhi_cmd_set_bind_group(rd__replay_cl(), 2, g2);
        rd__replay_dst_bind(r, &dp[i].key);
        if (g_rdWin.first && g_rdWin.drawLast && g_rdWin.passes == g_rdWin.last) {
            /* its class (the second level's VS_ONLY) the program's own
               entry, whether it draws through it or the main shader */
            rd__win_vu_note(r->st.tex, &key, vcb, progVs, indexed, first, count);
        }
        if (indexed) {
            rd__span_draw(first, i ? 0 : count);
            rd__replay_draw_indexed(rd__replay_cl(), count, first, 0, 1);
        } else {
            rd__replay_draw(rd__replay_cl(), count, first, 1);
        }
        if (dp[i].key.gs.colorMask & 8) {
            r->writeSerial++;
        }
        if (!rd__no_dual_second(dp, i)) {
            ownDraw();
        }
        if (i == 0) {
            /* PC port: counting only (vuMergeNote) */
            vuMergeNote(r, noteable, p.id, g1.id, g2.id, &cb);
        }
    }
    r->st = saved;
    if (g_rd.passSplit) {
        passSplit(r);
    }
}

/* What a VU command binds, as its payload, its mesh and the frame give it
 * (doVuDraw; rd__vu_prepass sets it up before the lists and doVuDraw takes
 * it): VuCB with the VU memory, the stream's layout and the draw's flags
 * (vu_draw.x is vuGroup's), VuBoneCB in the ring (the bones and the card
 * verdicts), the stream and the index list (the mesh arena or the ring),
 * the program's entry */
typedef struct VuSource {
    RdVuPayload p;
    RdMeshRec *m;
    IcoVuCB vcb;
    uint64_t bonesOff, streamBufSize, streamOff, streamSize, indexOff;
    RhiBuffer streamBuf, indexBuf;
    uint8_t vs;
} VuSource;

static bool vuSource(const RdFrame *f, const RdCmd *c, VuSource *s)
{
    memset(s, 0, sizeof(*s));
    RdVuPayload p;
    const uint8_t *at = f->payload + c->u[1];
    memcpy(&p, at, sizeof(p));
    at += sizeof(p);
    IcoVuCB vcb;
    memset(&vcb, 0, sizeof(vcb));
    memcpy(vcb.mem, at, sizeof(RdVuBlock));
    at += sizeof(RdVuBlock);
    const uint8_t *bones = p.boneQw ? at : NULL;
    at += (size_t)p.boneQw * 16;
    const uint8_t *stream = p.streamQw ? at : NULL;

    RdMeshRec *m = NULL;
    if (c->type == RDC_MESH || c->type == RDC_SKINNED) {
        m = rd__mesh_rec(c->u[0]);
        if (!m || !m->vu || p.firstBatch >= m->batchCount) {
            return false;
        }
    }
    const uint64_t ua = rhi_limits()->uniformAlign;
    /* the card verdicts (rd_mesh.h RdVuCards, vu_common.hlsli
     * vu_strip_card), in a state other than out of the box when a strip of
     * the draw is a card at either tick (the plain variant, the fallback's
     * on a device of the table, has no card test and never reads them): in
     * VuBoneCB after the bones a skinned mesh reads (RdMeshRec.boneQwUsed),
     * from qword 0 for the others; vu_strip = (first kick, the card test,
     * kicks covered, first qword).  A draw with more kicks than VuBoneCB
     * holds covers the first ones (the others are no card) */
    RdVuCards cards;
    const uint8_t *pay = f->payload + c->u[1];
    const uint32_t cardWords = rd__vu_cards_at(pay, c->u[2], &cards);
    const bool cardsRead = cardWords && cards.any && rd__vu_extended();
    const bool withCards = cardsRead && (c->type != RDC_SKINNED || (m && m->boneQwUsed < 240u));
    if (cardsRead && !withCards && c->type == RDC_SKINNED) {
        /* bones reaching bone 59 (address 252; the game allows 0..59,
           Packet.c pac_makeClusterStrip) or a weight address below 16
           fill VuBoneCB: the card bits have no room and the draw's strips
           are no card.  Not seen in the dumps (bones 0..58,
           rd_stream_audit reports it) */
        static bool logged; /* once a run (rd__log_once's flags are full) */
        if (!logged) {
            logged = true;
            rd__log("card verdicts: a skinned draw's bones fill VuBoneCB (%u qwords): its "
                    "strips are drawn as no card",
                    m ? m->boneQwUsed : 0u);
        }
    }
    uint32_t cardQw = 0, cardKicks = 0;
    if (withCards) {
        cardQw = c->type == RDC_SKINNED ? m->boneQwUsed : 0u;
        cardKicks = cards.kickCount;
        if (cardKicks > (240u - cardQw) * 64u) {
            static bool logged; /* once a run (rd__log_once's flags are full) */
            if (!logged) {
                logged = true;
                rd__log("card verdicts: a draw of %u kicks past VuBoneCB's room; the rest are "
                        "drawn as no card",
                        cardKicks);
            }
            cardKicks = (240u - cardQw) * 64u;
        }
    }
    /* bones: VuBoneCB (VU memory 16..255); a zero block for the others */
    uint64_t bonesOff;
    if (bones || withCards) {
        IcoVuBoneCB bc;
        memset(&bc, 0, sizeof(bc));
        if (bones) {
            memcpy(bc.bone, bones, (size_t)(p.boneQw > 240 ? 240 : p.boneQw) * 16);
        }
        if (withCards) {
            const uint32_t words = (cardKicks + 15u) / 16u;
            memcpy(bc.bone[cardQw], pay + cardWords, (size_t)words * 4u);
        }
        bonesOff = cbCopy(&bc, sizeof(bc), ua);
    } else {
        if (s_zeroBonesFor != g_rd.replayCounter) {
            static const IcoVuBoneCB zero;
            s_zeroBones = cbCopy(&zero, sizeof(zero), ua);
            s_zeroBonesFor = g_rd.replayCounter;
        }
        bonesOff = s_zeroBones;
    }
    uint64_t streamOff, streamSize, indexOff = 0;
    RhiBuffer streamBuf = g_rd.ring[rd__ring_slot()], indexBuf = g_rd.ring[rd__ring_slot()];
    uint64_t streamBufSize = g_rd.ringCap[rd__ring_slot()];
    if (m && m->gpuChunk && !m->gpuDirty && !m->transient) {
        /* the device copy rd__upload_meshes keeps */
        streamBuf = indexBuf = rd__mesh_gpu_buffer(m->gpuChunk);
        streamBufSize = rd__mesh_gpu_buffer_size(m->gpuChunk);
        streamOff = m->gpuOff;
        streamSize = ((uint64_t)m->vertexCount * m->qwPerVertex + rd__mesh_mark_qw(m)) * 16;
        indexOff = m->gpuIndexOff;
        vcb.draw[1] = m->qwPerVertex;
    } else if (m) {
        /* the stream and its mark bytes after it, as in the arena */
        const uint64_t vb = (uint64_t)m->vertexCount * m->qwPerVertex * 16;
        const uint64_t mb = (uint64_t)rd__mesh_mark_qw(m) * 16;
        if (m->replaySeen != g_rd.replayCounter) {
            m->ringStream = rd__ring_alloc((vb + mb) ? (vb + mb) : 16, ua);
            if (m->ringStream != ~0ull) {
                memcpy(g_rd.ringMap[rd__ring_slot()] + m->ringStream, m->stream, vb);
                rd__mesh_write_marks(m, g_rd.ringMap[rd__ring_slot()] + m->ringStream + vb);
            }
            m->ringIndex = ringCopy(m->index, (uint64_t)m->indexCount * 4, 16);
            m->replaySeen = g_rd.replayCounter;
            g_rdPerf.meshUploads++;
            g_rdPerf.meshUploadBytes += vb + mb + (uint64_t)m->indexCount * 4;
            if (m->transient) {
                /* the interpolation's blended copy, made for this present */
                g_rdPerf.uploadInterpBytes += vb + mb + (uint64_t)m->indexCount * 4;
            }
        }
        streamOff = m->ringStream;
        streamSize = vb + mb;
        indexOff = m->ringIndex;
        vcb.draw[1] = m->qwPerVertex;
    } else {
        streamSize = (uint64_t)p.streamQw * 16;
        streamOff = ringCopy(stream, streamSize, ua);
        g_rdPerf.uploadStreamBytes += streamSize;
        vcb.draw[1] = p.qwPerVertex;
    }
    if (bonesOff == ~0ull || streamOff == ~0ull || indexOff == ~0ull) {
        return false;
    }
    vcb.draw[0] = 0;
    vcb.draw[2] = p.clip;
    if (c->type == RDC_MESH && c->b[5] != 0) {
        /* a clip plane in the model's space (rd_chain.h, rd_mesh.c's b[5]
         * and f[0]): vu_batch.z its code, w its offset (vu_common.hlsli
         * vu_plane_distance) */
        vcb.batch[2] = c->b[5];
        memcpy(&vcb.batch[3], &c->f[0], sizeof(vcb.batch[3]));
    }
    if (f->camera.freeCamera) {
        /* photo mode: behind-eye triangles clipped, not dropped */
        vcb.draw[2] |= ICO_VU_BEHIND_EYE;
    }
    if (ico_region == ICO_REGION_JUN26) {
        /* the demo's cluster, mesh, normal_c and normal_l programs
         * (vu_common.hlsli g_vuVariant); nothing reads it yet */
        vcb.draw[2] |= ICO_VU_JUN26;
    }
    if (rd__vu_extended()) {
        /* a triangle the region test drops is clipped unless it belongs
         * to a card (vu_region_rule); a clipped triangle's corner whose GS
         * Z alone saturated keeps its GS position (vu_corner_cut_position) */
        vcb.draw[2] |= ICO_VU_EXTENDED;
        static bool logged; /* once a run (rd__log_once's flags are full) */
        if (!logged) {
            logged = true;
            rd__log("VU corners: not out of the box (Enhanced, wider than 4:3 or above 1x): a "
                    "triangle the region test drops is drawn clipped unless it is a card, and a "
                    "clipped triangle's corner whose GS Z saturated (within about 8 units of the "
                    "eye) keeps its GS position, as the exact triangle beside it has it");
        }
    }
    if (m && m->kickMark) {
        /* the triangles' marks, a byte a kick after the stream
           (vu_common.hlsli vu_kick_marks) */
        vcb.mark[0] = m->vertexCount * m->qwPerVertex;
        vcb.mark[2] = rd__mesh_mark_qw(m);
    }
    if (c->type != RDC_PARTICLES) {
        /* the card test (vu_region_rule): its verdicts in VuBoneCB above */
        vcb.strip[0] = withCards ? cards.firstKick : 0u;
        vcb.strip[1] = 1;
        vcb.strip[2] = cardKicks;
        vcb.strip[3] = cardQw;
    }
    if (f->hasTickCams && c->u[3] == RD_VU_TICK_CAMERAS) {
        /* drawn through a camera blended between two ticks'; a
         * triangle the rule drops there is clipped when the rule draws it
         * at either tick's camera (vu_region_rule) */
        vcb.draw[2] |= ICO_VU_TICKS;
        memcpy(vcb.ticks, f->tickCams, sizeof(vcb.ticks));
    }
    if (c->type == RDC_GRID) {
        /* the Mesh3D buffer as it is: per strip the VIF qword, tag and colour
         * before the vertices, MSCNT after them */
        vcb.draw[3] = p.vertsPerBatch;
        vcb.batch[0] = 3;
        vcb.batch[1] = 1;
        /* strip i is the grid's rows i and i + 1
         * (Primitive.c prim_UpdateMesh3D), so with two strips or more
         * every strip shares a row of positions with a neighbour: joined,
         * never a card (vu_region_rule), the draw's mark */
        vcb.mark[1] = p.batchCount >= 2 ? ICO_VU_MARK_JOINED : 0u;
    }
    s->p = p;
    s->m = m;
    s->vcb = vcb;
    s->bonesOff = bonesOff;
    s->streamBuf = streamBuf;
    s->streamBufSize = streamBufSize;
    s->streamOff = streamOff;
    s->streamSize = streamSize;
    s->indexBuf = indexBuf;
    s->indexOff = indexOff;
    s->vs = vuVs(p.prog, p.code);
    return true;
}

/* --------------------------------------- the vertex and triangle pre-passes
 * docs/port/RENDER.md, "The vertex and triangle pre-passes".  Before list 0
 * (replayFrame, after the uploads and the first clears) every mesh, grid
 * and particle command of the replayed lists gets its VuSource (kept for
 * its draws); a mesh or grid command the vertices its index range reads,
 * the lowest kick less 2 to the highest (a grid: every strip's), and a
 * record slot A and B for each of its kicks (and C under ICO_VU_TICKS), a
 * particle command a slot A and B for each sprite.  Pass V (the pass ring's
 * RD_PASSK_VU_PRE) draws each command's vertices as points through its
 * program's pre-pass entry (rd__vu_pre_key) at consecutive texels and each
 * particle command's sprites through vu_particle_pre_vs, twice (slots A
 * and B), into the record textures' first rows, which are copied into the
 * slot's record buffer; pass T (RD_PASSK_VU_TRI) reads them there and
 * draws each mesh or grid command's triangles as points (vu_tri_rule_pre_vs
 * into slot A, vu_tri_tick_pre_vs twice into slot C under the colour masks
 * R and G, vu_tri_planes_pre_vs into slot B: rd__vu_stage_key) into the rows
 * after them, copied the same way.  doVuDraw then draws the command
 * through vu_main_vs (vu_particle_main_vs) with VuCB's vu_rec, vu_lo, vu_hi
 * and vu_tri set (vuPreRecord).  A command past the record's
 * RD_VU_REC_ROWS_MAX rows, or one of whose pipelines is missing, keeps its
 * VuSource and is drawn the old way. */
typedef struct VuPre {
    const RdCmd *cmd;
    VuSource src;
    uint32_t first, count; /* the vertices [first, first + count); particles: count sprites */
    uint32_t base;         /* the first vertex's record texel */
    uint32_t slotA, slotB; /* slot A and B of kick first + 2 (particles: of sprite 0) */
    uint32_t slotC;        /* slot C of kick first + 2, ~0u: no tick slots */
    uint8_t preVs;         /* its pre-pass entry (0: a particle command) */
    uint8_t particle;      /* RDC_PARTICLES */
    uint8_t ticks;         /* ICO_VU_TICKS: slot C, the tick stage's draws */
    uint8_t rec;           /* drawn into the record */
    /* the FrameCB its draws bind (rd__replay_vu_frame: the walk's state
     * at the command), which the record stages read too, and the walk's
     * (target, depth, GS size, offset, picture) it was made from: the
     * draw's own must be the same (doVuDraw) */
    RdUniform frame;
    uint32_t frameKey[6];
} VuPre;

static VuPre *s_pre;
static uint32_t s_preCap, s_preN, s_preAt;
static uint32_t s_preReplay; /* g_rd.replayCounter of the entries, 0: none */

/* the pre-pass's entry for command c of this replay, NULL when it has none
 * (the commands come in the pre-pass's order: a cursor) */
static const VuPre *vuPreFind(const RdCmd *c)
{
    if (s_preReplay != g_rd.replayCounter || s_preN == 0) {
        return NULL;
    }
    for (uint32_t k = 0; k < s_preN; k++) {
        const uint32_t i = (s_preAt + k) % s_preN;
        if (s_pre[i].cmd == c) {
            s_preAt = i + 1;
            return &s_pre[i];
        }
    }
    return NULL;
}

/* the vertices command c's draws read: [*first, *first + *count) */
static void vuPreRange(const RdCmd *c, const VuSource *s, uint32_t *first, uint32_t *count)
{
    *first = *count = 0;
    if (c->type == RDC_GRID) {
        /* every strip's vertices (the index list doVuDraw builds) */
        if (s->p.vertsPerBatch >= 3u) {
            *count = s->p.batchCount * s->p.vertsPerBatch;
        }
        return;
    }
    const RdMeshRec *m = s->m;
    uint32_t last = s->p.firstBatch + s->p.batchCount;
    if (last > m->batchCount || s->p.batchCount == 0) {
        last = m->batchCount;
    }
    /* the indices doVuDraw draws (the sheet's tiles included: their
     * vertices are few, and a state change draws them) */
    const uint32_t i0 = m->batches[s->p.firstBatch].firstIndex;
    const uint32_t i1 = m->batches[last - 1].firstIndex + m->batches[last - 1].indexCount;
    uint32_t lo = UINT32_MAX, hi = 0;
    for (uint32_t i = i0; i < i1 && i < m->indexCount; i++) {
        const uint32_t kick = m->index[i] >> 2;
        lo = kick < lo ? kick : lo;
        hi = kick > hi ? kick : hi;
    }
    if (lo == UINT32_MAX || lo < 2u || hi >= m->vertexCount) {
        return; /* no triangle, or a kick past the stream: the old way */
    }
    *first = lo - 2u;
    *count = hi + 1u - *first;
}

/* the triangle stage's program rule (VuCB vu_rec.w, vu_lo, vu_hi) of a
 * program entry vs, as that entry's own main has it (vu_prelit.hlsl,
 * vu_lit.hlsl, vu_skin.hlsl, vu_grid.hlsl) */
static void vuTriRule(uint8_t vs, IcoVuCB *v)
{
    const uint32_t clip = v->draw[2] & ICO_VU_CLIP_MASK;
    uint32_t rule = ICO_VU_CLIP_REGION;
    bool chain = false, memBounds = false;
    switch (vs) {
    case RD_VS_VU_PRELIT:
        rule = clip; /* codes 32, 34 and 36 by VuCB's clip mode */
        chain = true;
        break;
    case RD_VS_VU_LIT:
        rule = clip == ICO_VU_CLIP_SCISSOR ? ICO_VU_CLIP_SCISSOR : ICO_VU_CLIP_REGION;
        chain = true;
        break;
    case RD_VS_VU_LIT_SPEC:
    case RD_VS_VU_REFLECT:
        chain = true; /* codes 34 and 38: the region test */
        break;
    case RD_VS_VU_SKIN:
    case RD_VS_VU_SKIN_SPEC:
    case RD_VS_VU_SKIN_DEBUG:
        memBounds = true; /* the cluster's bounds are VU memory 0 and 1 */
        break;
    default:
        break; /* the grids */
    }
    if (chain) {
        rule |= ICO_VU_TRI_CHAIN | ICO_VU_TRI_HIDE;
    }
    v->rec[3] = rule;
    if (memBounds) {
        memcpy(v->lo, v->mem[0], sizeof(v->lo));
        memcpy(v->hi, v->mem[1], sizeof(v->hi));
    } else {
        /* vu_common.hlsli VU_LO0, VU_HI4094 */
        static const float lo[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        static const float hi[4] = {4094.0f, 4094.0f, 0.0f, 16777214.0f};
        memcpy(v->lo, lo, sizeof(v->lo));
        memcpy(v->hi, hi, sizeof(v->hi));
    }
}

/* VuCB's pre-pass fields for entry e: vertex v's texel base - first + v,
 * kick k's slots vu_tri.xyz + k (a sprite i's vu_tri.xy + i) */
static void vuPreRecord(const VuPre *e, IcoVuCB *v)
{
    v->rec[1] = s_recBufRows[rd__ring_slot()] * ICO_VU_REC_W;
    if (e->particle) {
        v->rec[0] = v->rec[2] = v->rec[3] = 0;
        v->tri[0] = e->slotA;
        v->tri[1] = e->slotB;
        v->tri[2] = ~0u;
        v->tri[3] = 0;
        return;
    }
    v->rec[0] = e->base - e->first; /* wrapping: the shader adds the vertex */
    v->rec[2] = e->first;
    vuTriRule(e->src.vs, v);
    const uint32_t k0 = e->first + 2u; /* wrapping: the shader adds the kick */
    v->tri[0] = e->slotA - k0;
    v->tri[1] = e->slotB - k0;
    v->tri[2] = e->ticks ? e->slotC - k0 : ~0u;
    v->tri[3] = 0;
}

/* the record slots entry e takes in pass V's rows (its vertices, a
 * particle command's slots A and B) and in pass T's (A, B and C of its
 * kicks) */
static uint32_t vuPreVSlots(const VuPre *e)
{
    return e->particle ? 2u * e->count : e->count;
}

static uint32_t vuPreTSlots(const VuPre *e)
{
    return e->particle ? 0u : (e->ticks ? 3u : 2u) * (e->count - 2u);
}

/* the rows of n texels */
static uint32_t vuRows(uint64_t n)
{
    return (uint32_t)((n + ICO_VU_REC_W - 1u) / ICO_VU_REC_W);
}

/* the record stages' pipeline keys of entry e (*n of them): its pre-pass
 * entry's, the rule, tick (masks R and G) and planes stages' for a mesh or
 * grid command, the sprite stage's for a particle command */
static uint32_t vuPreKeys(const VuPre *e, RdPipeKeyInt keys[5])
{
    if (e->particle) {
        keys[0] = rd__vu_stage_key(RD_VS_VU_PARTICLE_PRE, 0xF);
        return 1;
    }
    uint32_t n = 0;
    keys[n++] = rd__vu_pre_key(e->preVs);
    keys[n++] = rd__vu_stage_key(RD_VS_VU_TRI_RULE_PRE, 0xF);
    if (e->ticks) {
        keys[n++] = rd__vu_stage_key(RD_VS_VU_TRI_TICK_PRE, 0x1);
        keys[n++] = rd__vu_stage_key(RD_VS_VU_TRI_TICK_PRE, 0x2);
    }
    keys[n++] = rd__vu_stage_key(RD_VS_VU_TRI_PLANES_PRE, 0xF);
    return n;
}

/* the pre-pass's draws' DrawCB */
static IcoDrawCB s_preDrawCB;

/* rd__vu_prepass_sabotage's mode (RD_VU_SABOTAGE_*) */
static int s_preSabotage;

/* rd__vu_prepass is recording (rd__vu_prepass_running) */
static bool s_preRunning;

/* rd__vu_prepass_entry_draws: the last pre-pass's draws one an entry and
   stage */
static uint32_t s_preEntryDraws;

bool rd__vu_prepass_running(void)
{
    return s_preRunning;
}

void rd__vu_prepass_sabotage(int mode)
{
    s_preSabotage = mode;
}

static void vuPrepassRun(const RdFrame *f, int keep);

void rd__vu_prepass(const RdFrame *f, int keep)
{
    /* t5's base this replay: past the uploads (ringRange) */
    s_ringBase = rd__ring_alloc(0, 256);
    if (s_ringBase == ~0ull || s_ringBase + 256u > g_rd.ringCap[rd__ring_slot()]) {
        s_ringBase = 0; /* the ring full: t5 from its start, as before */
    }
    s_preN = s_preAt = 0;
    s_preReplay = 0;
    s_preEntryDraws = 0;
    if (!g_rd.vuPrepass) {
        return;
    }
    s_preRunning = true;
    vuPrepassRun(f, keep);
    s_preRunning = false;
}

/* The pre-pass's batch count (RdPerfRecord prepassBatches; counting only):
 * the distinct (stage key, FrameCB group and offset, stream buffer, bones
 * class: the shared zero block or not) of the draws vuStageDraw makes, a
 * table stamped with the replay so it needs no clearing */
#define VU_BATCH_SLOTS 8192u
static uint64_t s_batchKey[VU_BATCH_SLOTS];
static uint32_t s_batchStamp[VU_BATCH_SLOTS];

static void vuBatchNote(const VuPre *e, const RdPipeKeyInt *key)
{
    const bool zero = s_zeroBonesFor == g_rd.replayCounter && e->src.bonesOff == s_zeroBones;
    uint64_t h = 1469598103934665603ull;
    const uint64_t parts[5] = {key->vs, key->gs.colorMask, e->frame.group.id,
                               ((uint64_t)e->frame.offset << 32) | e->src.streamBuf.id,
                               zero ? 1u : 0u};
    for (int i = 0; i < 5; i++) {
        h = (h ^ parts[i]) * 1099511628211ull;
        h ^= h >> 29;
    }
    h |= 1u;
    uint32_t at = (uint32_t)(h % VU_BATCH_SLOTS);
    for (uint32_t n = 0; n < VU_BATCH_SLOTS; n++, at = (at + 1u) % VU_BATCH_SLOTS) {
        if (s_batchStamp[at] != g_rdPerf.replay) {
            s_batchStamp[at] = g_rdPerf.replay;
            s_batchKey[at] = h;
            g_rdPerf.prepassBatches++;
            return;
        }
        if (s_batchKey[at] == h) {
            return;
        }
    }
}

/* one draw of entry e into the record (pass V's vertex and sprite stages,
 * pass T's stages): its VuCB with vu_tri.w = w, its draws' FrameCB, the
 * key, count points; false when it could not be recorded (the ring full, a
 * pipeline missing) */
static bool vuStageDraw(const VuPre *e, IcoVuCB *vcb, uint64_t dOff, const RdPipeKeyInt *key,
                        uint32_t w, uint32_t count)
{
    vcb->tri[3] = w;
    if (g_rdWin.first && g_rdWin.filter == 1) {
        /* the loss bisect's MASK_MARKS (vuDraw's): every triangle unmarked */
        memset(vcb->mark, 0, sizeof(vcb->mark));
    }
    const RhiBindGroup g1 = vuGroup(e->src.streamBuf, e->src.streamBufSize, e->src.streamOff,
                                    e->src.streamSize, &vcb->draw[0]);
    const uint64_t vOff = cbCopy(vcb, sizeof(*vcb), rhi_limits()->uniformAlign);
    const RhiPipeline p = rd__get_pipeline(key);
    if (!g1.id || vOff == ~0ull || (!p.id && !rd__pipeline_gathering())) {
        return false;
    }
    if (!p.id) {
        return true; /* only naming keys */
    }
    const uint32_t offs[3] = {(uint32_t)dOff, (uint32_t)vOff, (uint32_t)e->src.bonesOff};
    rd__set_pipeline(rd__replay_cl(), p, key);
    rd__bind_uniform(rd__replay_cl(), 0, e->frame); /* the command's draws' FrameCB */
    rhi_cmd_set_bind_group_offsets(rd__replay_cl(), 1, g1, offs, 3);
    if (g_rdWin.first && g_rdWin.drawLast && g_rdWin.passes == g_rdWin.last) {
        /* the draw note: the command's, its class the program's own
           entry (rd__win_vu_note) */
        g_rdVuCur.type = e->cmd->type;
        g_rdVuCur.prog = e->src.p.prog;
        g_rdVuCur.clip = e->src.p.clip;
        g_rdVuCur.batch = e->src.p.firstBatch;
        g_rdVuCur.index = NULL;
        g_rdVuCur.marks = NULL;
        g_rdVuCur.markCount = 0;
        g_rdVuCur.mesh = e->src.m ? e->src.m->name : NULL;
        rd__win_vu_note(0, key, vcb, e->src.vs, 0, e->first, count);
    }
    rd__replay_draw(rd__replay_cl(), count, 0, 1);
    vuBatchNote(e, key);
    return true;
}

/* one of the two passes into the record's textures, rows [row0, row0 +
 * rows): begun on the targets (load DONT_CARE: the rows before row0 were
 * copied already), the shaders' mapping the slot buffer's rows (vu_common.hlsli
 * vu_rec_point_out), the scissor the rows drawn */
static void vuRecPassBegin(uint32_t row0, uint32_t rows)
{
    for (uint32_t k = 0; k < ICO_VU_REC_PLANES; k++) {
        rd__transition(rd__replay_cl(), s_recTex[k], &s_recTexState[k], RHI_STATE_RENDER_TARGET);
    }
    RhiRenderPassDesc rp;
    memset(&rp, 0, sizeof(rp));
    for (uint32_t k = 0; k < ICO_VU_REC_PLANES; k++) {
        rp.color[k].texture = s_recTex[k];
        rp.color[k].load = RHI_LOAD_DONT_CARE;
        rp.color[k].store = RHI_STORE_STORE;
    }
    rp.colorCount = ICO_VU_REC_PLANES;
    rp.width = ICO_VU_REC_W;
    rp.height = row0 + rows;
    rhi_cmd_begin_render_pass(rd__replay_cl(), &rp);
    const RhiViewport vp = {0.0f, 0.0f, (float)ICO_VU_REC_W, (float)s_recBufRows[rd__ring_slot()],
                            0.0f, 1.0f};
    rhi_cmd_set_viewport(rd__replay_cl(), &vp);
    const RhiRect sc = {0, (int32_t)row0, ICO_VU_REC_W, rows};
    rhi_cmd_set_scissor(rd__replay_cl(), &sc);
}

/* the pass's rows of the planes into the slot's record buffer, plane after
 * plane (visible to the shaders after: rhi.h rhi_cmd_copy_texture_to_buffer) */
static void vuRecPassCopy(uint32_t row0, uint32_t rows, bool muted)
{
    rhi_cmd_end_render_pass(rd__replay_cl());
    const uint64_t plane = (uint64_t)s_recBufRows[rd__ring_slot()] * ICO_VU_REC_W * 16u;
    for (uint32_t k = 0; k < ICO_VU_REC_PLANES; k++) {
        rd__transition(rd__replay_cl(), s_recTex[k], &s_recTexState[k], RHI_STATE_COPY_SRC);
        const RhiRect region = {0, (int32_t)row0, ICO_VU_REC_W, rows};
        if (!muted) {
            rhi_cmd_copy_texture_to_buffer(
                rd__replay_cl(), s_recTex[k], RHI_ASPECT_COLOR, region, s_recBuf[rd__ring_slot()],
                plane * k + (uint64_t)row0 * ICO_VU_REC_W * 16u, ICO_VU_REC_W * 16u);
        }
    }
}

/* ------------------------------------------------ the pre-pass's batches
 * docs/port/VU1_PROGRAMS.md, "The pre-pass's batches".  Under g_rd.vuPreBatch
 * a pass's entry draws (vuStageDraw's: one an entry and stage) become items:
 * each still copies its VuCB (vu_tri.w its stage's) into the ring as
 * vuStageDraw does, with its VuBoneCB there already (vuSource); the items
 * are ordered by (stage key, FrameCB, group 1), the record's order kept
 * within each, and each run of up to RD_VU_PRE_BATCH_MAX items with the same
 * three is one point draw through the stage's batch build
 * (vu_X_batch_pre_vs): its table, a uint4 an item (the item's first
 * SV_VertexID in the draw, its VuCB's and VuBoneCB's float4 in the ring),
 * copied into the ring; its DrawCB (b1, which no record stage reads
 * otherwise) aux0.x the table's float4 in the ring, aux0.y its items.  The
 * shaders read the ring at t5 (vu_common.hlsli vu_ring,
 * vu_pre_batch_entry), the same bytes the cbuffers would hold, so each
 * entry writes the words it writes in its own draw, at the same texels; the
 * items of a pass write disjoint texels, so their order does not change
 * the record. */
#define RD_VU_PRE_BATCH_MAX 64u /* vu_common.hlsli VU_PRE_BATCH_MAX */

typedef struct PreItem {
    VuPre *e;
    RhiBindGroup g1;
    uint64_t vOff;  /* its VuCB in the ring */
    uint32_t count; /* its points */
    uint32_t seq;   /* its place in the record's order */
    RdPipeKeyInt key;
} PreItem;

static PreItem *s_items;
static uint32_t s_itemCap, s_itemN;
static uint32_t *s_itemOrder;

uint32_t rd__vu_prepass_entry_draws(void)
{
    return s_preEntryDraws;
}

/* room for n more items; false when it cannot be had */
static bool preItemsReserve(uint32_t n)
{
    if (s_itemN + n <= s_itemCap) {
        return true;
    }
    uint32_t cap = s_itemCap ? s_itemCap : 512u;
    while (cap < s_itemN + n) {
        cap *= 2u;
    }
    PreItem *items = realloc(s_items, (size_t)cap * sizeof(*items));
    if (!items) {
        return false;
    }
    s_items = items;
    uint32_t *order = realloc(s_itemOrder, (size_t)cap * sizeof(*order));
    if (!order) {
        return false;
    }
    s_itemOrder = order;
    s_itemCap = cap;
    return true;
}

/* entry e's draw of the stage key with VuCB vcb (vu_tri.w = w) as an item:
 * its group 1 (vu_draw.x), its VuCB in the ring; false when it could not be
 * (the ring full, its VuCB or VuBoneCB past what t5 binds, no group) */
static bool preItem(VuPre *e, IcoVuCB *vcb, const RdPipeKeyInt *key, uint32_t w, uint32_t count,
                    PreItem *out)
{
    vcb->tri[3] = w;
    if (g_rdWin.first && g_rdWin.filter == 1) {
        /* the loss bisect's MASK_MARKS (vuDraw's): every triangle unmarked */
        memset(vcb->mark, 0, sizeof(vcb->mark));
    }
    memset(out, 0, sizeof(*out));
    out->g1 = vuGroup(e->src.streamBuf, e->src.streamBufSize, e->src.streamOff, e->src.streamSize,
                      &vcb->draw[0]);
    out->vOff = cbCopy(vcb, sizeof(*vcb), rhi_limits()->uniformAlign);
    uint32_t at;
    if (!out->g1.id || !ringIndex(out->vOff, sizeof(IcoVuCB), &at) ||
        !ringIndex(e->src.bonesOff, sizeof(IcoVuBoneCB), &at)) {
        return false;
    }
    out->e = e;
    out->count = count;
    out->key = *key;
    return true;
}

/* the items' order: stage key (its vertex shader and colour mask, the only
   fields the record's keys differ in: rd__vu_stage_key), FrameCB, group 1,
   then the record's */
static int preItemCmp(const void *a, const void *b)
{
    const PreItem *x = &s_items[*(const uint32_t *)a], *y = &s_items[*(const uint32_t *)b];
    const uint32_t kx = (uint32_t)x->key.vs << 4 | x->key.gs.colorMask;
    const uint32_t ky = (uint32_t)y->key.vs << 4 | y->key.gs.colorMask;
    if (kx != ky) {
        return kx < ky ? -1 : 1;
    }
    if (x->e->frame.group.id != y->e->frame.group.id) {
        return x->e->frame.group.id < y->e->frame.group.id ? -1 : 1;
    }
    if (x->e->frame.offset != y->e->frame.offset) {
        return x->e->frame.offset < y->e->frame.offset ? -1 : 1;
    }
    if (x->g1.id != y->g1.id) {
        return x->g1.id < y->g1.id ? -1 : 1;
    }
    return x->seq < y->seq ? -1 : (x->seq > y->seq ? 1 : 0);
}

static bool preItemSameBatch(const PreItem *x, const PreItem *y)
{
    return x->key.vs == y->key.vs && x->key.gs.colorMask == y->key.gs.colorMask &&
           x->e->frame.group.id == y->e->frame.group.id &&
           x->e->frame.offset == y->e->frame.offset && x->g1.id == y->g1.id;
}

/* one batch: the items order[0 .. n) (n <= RD_VU_PRE_BATCH_MAX) as one point
 * draw: 1 drawn, 0 only its key named (rd__pipeline_gathering), -1 it could
 * not be drawn (the ring full, its pipeline missing) */
static int preBatchDraw(const uint32_t *order, uint32_t n)
{
    const PreItem *f = &s_items[order[0]];
    const RhiPipeline p = rd__get_pipeline(&f->key);
    if (!p.id) {
        return rd__pipeline_gathering() ? 0 : -1;
    }
    uint32_t tab[RD_VU_PRE_BATCH_MAX][4];
    uint32_t points = 0;
    for (uint32_t i = 0; i < n; i++) {
        const PreItem *it = &s_items[order[i]];
        tab[i][0] = points;
        tab[i][1] = (uint32_t)((it->vOff - s_ringBase) / 16u); /* in t5 (preItem) */
        tab[i][2] = (uint32_t)((it->e->src.bonesOff - s_ringBase) / 16u);
        tab[i][3] = 0;
        points += it->count;
    }
    const uint64_t ua = rhi_limits()->uniformAlign;
    const uint64_t tOff = cbCopy(tab, (uint64_t)n * 16u, 16);
    uint32_t tAt;
    if (!ringIndex(tOff, (uint64_t)n * 16u, &tAt)) {
        return -1;
    }
    IcoDrawCB cb;
    memset(&cb, 0, sizeof(cb));
    cb.aux0[0] = tAt;
    cb.aux0[1] = n;
    const uint64_t dOff = cbCopy(&cb, sizeof(cb), ua);
    if (dOff == ~0ull) {
        return -1;
    }
    /* VuCB and VuBoneCB bound at the first item's (the batch build reads
       neither: every item's are read through t5) */
    const uint32_t offs[3] = {(uint32_t)dOff, (uint32_t)f->vOff, (uint32_t)f->e->src.bonesOff};
    rd__set_pipeline(rd__replay_cl(), p, &f->key);
    rd__bind_uniform(rd__replay_cl(), 0, f->e->frame); /* the commands' draws' FrameCB */
    rhi_cmd_set_bind_group_offsets(rd__replay_cl(), 1, f->g1, offs, 3);
    if (g_rdWin.first && g_rdWin.drawLast && g_rdWin.passes == g_rdWin.last) {
        /* the draw note: the batch's first command's, its class the
           program's own entry (rd__win_vu_note), the batch's points */
        const VuPre *e = f->e;
        g_rdVuCur.type = e->cmd->type;
        g_rdVuCur.prog = e->src.p.prog;
        g_rdVuCur.clip = e->src.p.clip;
        g_rdVuCur.batch = e->src.p.firstBatch;
        g_rdVuCur.index = NULL;
        g_rdVuCur.marks = NULL;
        g_rdVuCur.markCount = 0;
        g_rdVuCur.mesh = e->src.m ? e->src.m->name : NULL;
        rd__win_vu_note(0, &f->key, &e->src.vcb, e->src.vs, 0, e->first, points);
    }
    rd__replay_draw(rd__replay_cl(), points, 0, 1);
    return 1;
}

/* the pass's items as batches; an item of a batch that could not be drawn
 * leaves its command to the old way (VuPre.rec 0); the draws made */
static uint32_t preBatchesDraw(void)
{
    for (uint32_t i = 0; i < s_itemN; i++) {
        s_itemOrder[i] = i;
    }
    qsort(s_itemOrder, s_itemN, sizeof(*s_itemOrder), preItemCmp);
    uint32_t draws = 0;
    for (uint32_t i = 0; i < s_itemN;) {
        uint32_t n = 1;
        while (i + n < s_itemN && n < RD_VU_PRE_BATCH_MAX &&
               preItemSameBatch(&s_items[s_itemOrder[i]], &s_items[s_itemOrder[i + n]])) {
            n++;
        }
        const int drawn = preBatchDraw(&s_itemOrder[i], n);
        if (drawn >= 0) {
            draws += (uint32_t)drawn;
        } else {
            for (uint32_t k = 0; k < n; k++) {
                s_items[s_itemOrder[i + k]].e->rec = 0; /* drawn the old way */
            }
        }
        i += n;
    }
    s_itemN = 0;
    return draws;
}

static void vuPrepassRun(const RdFrame *f, int keep)
{
    /* 1. every mesh, grid and particle command: its source, its vertices
     * or sprites, the FrameCB of its draws (the state block followed as
     * the walk does, replayFrame: rd__apply_state from the frame's start
     * state, and after a TARGET command the half pixel the walk adds to a
     * later SCENE target, rd__replay_scene_keeps_half; consecutive commands
     * of one target, size, offset, depth and picture share one) */
    RdStateBlock st = f->startState;
    const uint32_t headHalf = rd__replay_scene_head_half(f);
    RdUniform frame = {{0}, 0};
    uint32_t frameKey[6];
    memset(frameKey, 0xFF, sizeof(frameKey));
    for (int l = rd__first_list(keep); l < RD_LIST_COUNT; l++) {
        const RdCmdList *list = &f->lists[l];
        for (uint32_t i = 0; i < list->count; i++) {
            const RdCmd *c = &list->cmds[i];
            if (rd__apply_state(&st, c)) {
                if (c->type == RDC_TARGET) {
                    rd__replay_scene_keeps_half(&st, l, headHalf);
                }
                continue;
            }
            if (c->type != RDC_MESH && c->type != RDC_SKINNED && c->type != RDC_GRID &&
                c->type != RDC_PARTICLES) {
                continue;
            }
            if (s_preN == s_preCap) {
                const uint32_t cap = s_preCap ? s_preCap * 2u : 256u;
                VuPre *grown = realloc(s_pre, (size_t)cap * sizeof(*grown));
                if (!grown) {
                    break; /* the rest drawn the old way */
                }
                s_pre = grown;
                s_preCap = cap;
            }
            VuPre *e = &s_pre[s_preN];
            memset(e, 0, sizeof(*e));
            if (!vuSource(f, c, &e->src)) {
                continue;
            }
            e->cmd = c;
            e->slotC = ~0u;
            const uint32_t picture = (uint32_t)c->b[3] | ((uint32_t)c->b[6] << 8);
            const uint32_t fk[6] = {st.color, st.depth, st.gsW, st.gsH, st.useOffset, picture};
            if (memcmp(fk, frameKey, sizeof(fk)) != 0) {
                frame = rd__replay_vu_frame(&st, picture);
                memcpy(frameKey, fk, sizeof(fk));
            }
            e->frame = frame;
            memcpy(e->frameKey, fk, sizeof(fk));
            if (c->type == RDC_PARTICLES) {
                e->particle = 1;
                e->count = e->src.p.vertsPerBatch;
            } else {
                e->preVs = rd__vu_pre_vs(e->src.vs);
                e->ticks = (e->src.vcb.draw[2] & ICO_VU_TICKS) != 0u;
                vuPreRange(c, &e->src, &e->first, &e->count);
                if (!e->preVs || e->count < 3u) {
                    e->count = 0; /* no triangle: the old way */
                }
            }
            if (!e->frame.group.id) {
                e->count = 0; /* no FrameCB for its target: the old way */
            }
            s_preN++;
        }
    }
    s_preReplay = g_rd.replayCounter;
    /* 2. the record's rows: pass V's (vertices, then sprites) and pass T's
     * (the kicks' slots) for the commands in order while both fit
     * RD_VU_REC_ROWS_MAX rows (the rest the old way); its size powers of two
     * from the least */
    uint64_t nv = 0, nt = 0;
    uint32_t taken = 0;
    for (uint32_t i = 0; i < s_preN; i++) {
        const VuPre *e = &s_pre[i];
        if (!e->count) {
            continue;
        }
        const uint64_t v = nv + vuPreVSlots(e), t = nt + vuPreTSlots(e);
        if ((uint64_t)vuRows(v) + vuRows(t) > RD_VU_REC_ROWS_MAX) {
            break;
        }
        nv = v;
        nt = t;
        taken = i + 1u;
    }
    if (nv == 0) {
        return;
    }
    const uint32_t rowsV = vuRows(nv), rowsT = vuRows(nt);
    uint32_t rows = s_recTexRows > RD_VU_REC_ROWS_MIN ? s_recTexRows : RD_VU_REC_ROWS_MIN;
    while (rows < rowsV + rowsT && rows < RD_VU_REC_ROWS_MAX) {
        rows *= 2u;
    }
    {
        uint32_t past = 0, want = 0;
        for (uint32_t i = 0; i < s_preN; i++) {
            want += s_pre[i].count != 0u;
            past += i >= taken && s_pre[i].count != 0u;
        }
        static bool logged; /* once a run */
        if (past && !logged) {
            logged = true;
            rd__log("VU pre-pass: a frame past the record's %u rows: %u of its %u VU commands "
                    "are drawn the old way (reported once)",
                    RD_VU_REC_ROWS_MAX, past, want);
        }
    }
    if (!recEnsure(rows)) {
        /* drawn the old way; tried once more at the next replay, then the
           pre-pass is off for the run (no allocation asked for again a
           replay) */
        if (++s_recFails >= 2u) {
            g_rd.vuPrepass = false;
            snprintf(g_rd.vuPrepassWhy, sizeof(g_rd.vuPrepassWhy),
                     "for the run: its record could not be made");
            rd__log("VU pre-pass: off for the run: the record (%u rows, %llu KB a frame slot) "
                    "could not be made twice; every VU draw the old way",
                    rows, (unsigned long long)(recBytes(rows) / 1024u));
        } else {
            static uint32_t s_lines; /* the first 8 */
            if (s_lines++ < 8u) {
                rd__log("VU pre-pass: the record (%u rows, %llu KB a frame slot) could not be "
                        "made: this replay draws the old way, the next tries once more",
                        rows, (unsigned long long)(recBytes(rows) / 1024u));
            }
        }
        return;
    }
    s_recFails = 0;
    /* 3. the slots: pass V's from texel 0, pass T's from row rowsV; a
     * command whose pipelines are all made is drawn into the record.  The
     * pipelines single-sampled whatever the last pass was (rd__get_pipeline's
     * drawKey) */
    const uint8_t samples = g_rd.passSamples;
    g_rd.passSamples = 1;
    const uint32_t t0 = rowsV * ICO_VU_REC_W;
    uint32_t vAt = 0, tAt = t0, vEnd = 0, tEnd = t0;
    for (uint32_t i = 0; i < taken; i++) {
        VuPre *e = &s_pre[i];
        if (!e->count) {
            continue;
        }
        if (e->particle) {
            e->slotA = vAt;
            e->slotB = vAt + e->count;
        } else {
            const uint32_t k = e->count - 2u;
            e->base = vAt;
            e->slotA = tAt;
            e->slotB = tAt + k;
            e->slotC = e->ticks ? tAt + 2u * k : ~0u;
            tAt += vuPreTSlots(e);
        }
        vAt += vuPreVSlots(e);
        RdPipeKeyInt keys[5];
        const uint32_t nk = vuPreKeys(e, keys);
        bool made = true;
        for (uint32_t k = 0; k < nk && made; k++) {
            made = rd__get_pipeline(&keys[k]).id || rd__pipeline_gathering();
            if (!made) {
                static bool logged; /* once a run */
                if (!logged) {
                    logged = true;
                    rd__log("VU pre-pass: the pipeline of %s could not be made: its commands are "
                            "drawn the old way (reported once)",
                            rd__vs_name(keys[k].vs));
                }
            }
        }
        if (!made) {
            continue;
        }
        e->rec = 1;
        vEnd = vAt;
        tEnd = e->particle ? tEnd : tAt;
    }
    const uint32_t usedV = vuRows(vEnd), usedT = vuRows((uint64_t)tEnd - t0);
    /* the draws' DrawCB (b1 of group 1; no record stage reads it) */
    const uint64_t ua = rhi_limits()->uniformAlign;
    memset(&s_preDrawCB, 0, sizeof(s_preDrawCB));
    const uint64_t dOff = usedV ? cbCopy(&s_preDrawCB, sizeof(s_preDrawCB), ua) : ~0ull;
    if (dOff == ~0ull) {
        for (uint32_t i = 0; i < s_preN; i++) {
            s_pre[i].rec = 0; /* the ring is full: drawn the old way */
        }
        g_rd.passSamples = samples;
        return;
    }
    /* 4. pass V: a draw a mesh or grid command (its vertices), two a
     * particle command (its slots A and B).  A piece of the GPU timing (rd_perf.c
     * rd__perf_pass) with its copies; the pass ring's entry (rd_passring.h
     * RD_PASSK_VU_PRE: the rows drawn and the vertices); the loss bisect's
     * pass window numbers it pass 1 (rd_window.c): outside the window its
     * draws and copies are left out, and the slot's record buffer keeps what
     * an earlier replay of the frame wrote */
    uint32_t verts = 0, sprites = 0, draws = 0, triSlots = 0, passes = 0;
    uint32_t vertDraws = 0;
    rd__perf_pass(rd__replay_cl(), RD_PERF_PASS_PREPASS, RD_PERF_ID_RECORD, ICO_VU_REC_W, usedV,
                  -1);
    rhi_cmd_checkpoint(rd__replay_cl(), rd__replay_pass_push(RD_PASSK_VU_PRE, 0, ICO_VU_REC_W,
                                                             usedV, 0, vEnd, 0, 0, -1));
    bool muted = rd__win_muted();
    vuRecPassBegin(0, usedV);
    const bool batched = g_rd.vuPreBatch;
    uint32_t entryDraws = 0;
    for (uint32_t i = 0; batched && i < s_preN; i++) {
        /* the batches' items (one an entry, two a particle command), then
         * their draws */
        VuPre *e = &s_pre[i];
        if (!e->rec) {
            continue;
        }
        IcoVuCB vcb = e->src.vcb;
        vuPreRecord(e, &vcb);
        PreItem it[2];
        if (e->particle) {
            /* the sprite stage, slot A then slot B */
            const RdPipeKeyInt key = rd__vu_stage_key(RD_VS_VU_PARTICLE_PRE, 0xF);
            if (!preItemsReserve(2) || !preItem(e, &vcb, &key, 0, e->count, &it[0]) ||
                !preItem(e, &vcb, &key, 1, e->count, &it[1])) {
                e->rec = 0; /* drawn the old way */
                continue;
            }
            for (uint32_t k = 0; k < 2; k++) {
                it[k].seq = s_itemN;
                s_items[s_itemN++] = it[k];
            }
            continue;
        }
        if (s_preSabotage == RD_VU_SABOTAGE_VERTEX && vertDraws == 0) {
            vcb.rec[0] += 1u; /* rd_vuprepass_selftest's sabotage: written a texel on */
        }
        const RdPipeKeyInt key = rd__vu_pre_key(e->preVs);
        if (!preItemsReserve(1) || !preItem(e, &vcb, &key, 0, e->count, &it[0])) {
            e->rec = 0; /* drawn the old way */
            continue;
        }
        it[0].seq = s_itemN;
        s_items[s_itemN++] = it[0];
        vertDraws++;
    }
    if (batched) {
        draws += preBatchesDraw();
        for (uint32_t i = 0; i < s_preN; i++) {
            const VuPre *e = &s_pre[i];
            if (e->rec) {
                sprites += e->particle ? e->count : 0u;
                verts += e->particle ? 0u : e->count;
                entryDraws += e->particle ? 2u : 1u;
            }
        }
    }
    for (uint32_t i = 0; !batched && i < s_preN; i++) {
        VuPre *e = &s_pre[i];
        if (!e->rec) {
            continue;
        }
        IcoVuCB vcb = e->src.vcb;
        vuPreRecord(e, &vcb);
        if (e->particle) {
            /* the sprite stage, slot A then slot B */
            const RdPipeKeyInt key = rd__vu_stage_key(RD_VS_VU_PARTICLE_PRE, 0xF);
            if (!vuStageDraw(e, &vcb, dOff, &key, 0, e->count) ||
                !vuStageDraw(e, &vcb, dOff, &key, 1, e->count)) {
                e->rec = 0; /* drawn the old way */
                continue;
            }
            sprites += e->count;
            draws += 2;
            continue;
        }
        if (s_preSabotage == RD_VU_SABOTAGE_VERTEX && vertDraws == 0) {
            vcb.rec[0] += 1u; /* rd_vuprepass_selftest's sabotage: written a texel on */
        }
        const RdPipeKeyInt key = rd__vu_pre_key(e->preVs);
        if (!vuStageDraw(e, &vcb, dOff, &key, 0, e->count)) {
            e->rec = 0; /* drawn the old way */
            continue;
        }
        verts += e->count;
        vertDraws++;
        draws++;
    }
    vuRecPassCopy(0, usedV, muted);
    passes++;
    /* 5. pass T over the vertex records in the buffer: per mesh or grid
     * command the rule stage (slot A), the tick stage under the masks R and
     * G (slot C: ticks 0 and 1), the planes stage (slot B), a point a kick;
     * its piece, its pass ring entry (RD_PASSK_VU_TRI: the rows drawn and
     * the slots), pass 2 of the loss bisect's window; its copies */
    if (usedT) {
        rd__perf_pass(rd__replay_cl(), RD_PERF_PASS_PREPASS, RD_PERF_ID_RECORD, ICO_VU_REC_W, usedT,
                      -1);
        rhi_cmd_checkpoint(rd__replay_cl(), rd__replay_pass_push(RD_PASSK_VU_TRI, 0, ICO_VU_REC_W,
                                                                 usedT, 0, tEnd - t0, 0, 0, -1));
        muted = rd__win_muted();
        vuRecPassBegin(rowsV, usedT);
        bool firstTri = true;
        for (uint32_t i = 0; batched && i < s_preN; i++) {
            /* the batches' items: the rule, tick (R, G) and planes stages
             * of each command, then their draws */
            VuPre *e = &s_pre[i];
            if (!e->rec || e->particle) {
                continue;
            }
            IcoVuCB vcb = e->src.vcb;
            vuPreRecord(e, &vcb);
            const uint32_t kicks = e->count - 2u;
            IcoVuCB rule = vcb;
            if (s_preSabotage == RD_VU_SABOTAGE_TRI && firstTri) {
                rule.tri[0] += 1u; /* the self-test's sabotage: slot A written a slot on */
            }
            firstTri = false;
            PreItem it[4];
            uint32_t n = 0;
            const RdPipeKeyInt kr = rd__vu_stage_key(RD_VS_VU_TRI_RULE_PRE, 0xF);
            bool ok = preItemsReserve(4) && preItem(e, &rule, &kr, 0, kicks, &it[n++]);
            if (ok && e->ticks) {
                IcoVuCB tick = vcb;
                if (s_preSabotage == RD_VU_SABOTAGE_TICKS) {
                    /* the self-test's sabotage: tick 1 tested at tick 0's
                       camera */
                    memcpy(tick.ticks[4], tick.ticks[0], 4 * sizeof(tick.ticks[0]));
                }
                const RdPipeKeyInt k0 = rd__vu_stage_key(RD_VS_VU_TRI_TICK_PRE, 0x1);
                const RdPipeKeyInt k1 = rd__vu_stage_key(RD_VS_VU_TRI_TICK_PRE, 0x2);
                ok = preItem(e, &tick, &k0, 0, kicks, &it[n++]) &&
                     preItem(e, &tick, &k1, 1, kicks, &it[n++]);
            }
            const RdPipeKeyInt kp = rd__vu_stage_key(RD_VS_VU_TRI_PLANES_PRE, 0xF);
            ok = ok && preItem(e, &vcb, &kp, 0, kicks, &it[n++]);
            if (!ok) {
                e->rec = 0; /* drawn the old way (its vertices' slots unread) */
                continue;
            }
            for (uint32_t k = 0; k < n; k++) {
                it[k].seq = s_itemN;
                s_items[s_itemN++] = it[k];
            }
        }
        if (batched) {
            draws += preBatchesDraw();
            for (uint32_t i = 0; i < s_preN; i++) {
                const VuPre *e = &s_pre[i];
                if (e->rec && !e->particle) {
                    entryDraws += e->ticks ? 4u : 2u;
                    triSlots += vuPreTSlots(e);
                }
            }
        }
        for (uint32_t i = 0; !batched && i < s_preN; i++) {
            VuPre *e = &s_pre[i];
            if (!e->rec || e->particle) {
                continue;
            }
            IcoVuCB vcb = e->src.vcb;
            vuPreRecord(e, &vcb);
            const uint32_t kicks = e->count - 2u;
            IcoVuCB rule = vcb;
            if (s_preSabotage == RD_VU_SABOTAGE_TRI && firstTri) {
                rule.tri[0] += 1u; /* the self-test's sabotage: slot A written a slot on */
            }
            firstTri = false;
            const RdPipeKeyInt kr = rd__vu_stage_key(RD_VS_VU_TRI_RULE_PRE, 0xF);
            bool ok = vuStageDraw(e, &rule, dOff, &kr, 0, kicks);
            if (ok && e->ticks) {
                IcoVuCB tick = vcb;
                if (s_preSabotage == RD_VU_SABOTAGE_TICKS) {
                    /* the self-test's sabotage: tick 1 tested at tick 0's
                       camera */
                    memcpy(tick.ticks[4], tick.ticks[0], 4 * sizeof(tick.ticks[0]));
                }
                const RdPipeKeyInt k0 = rd__vu_stage_key(RD_VS_VU_TRI_TICK_PRE, 0x1);
                const RdPipeKeyInt k1 = rd__vu_stage_key(RD_VS_VU_TRI_TICK_PRE, 0x2);
                ok = vuStageDraw(e, &tick, dOff, &k0, 0, kicks) &&
                     vuStageDraw(e, &tick, dOff, &k1, 1, kicks);
                draws += ok ? 2u : 0u;
            }
            const RdPipeKeyInt kp = rd__vu_stage_key(RD_VS_VU_TRI_PLANES_PRE, 0xF);
            ok = ok && vuStageDraw(e, &vcb, dOff, &kp, 0, kicks);
            if (!ok) {
                e->rec = 0; /* drawn the old way (its vertices' slots unread) */
                continue;
            }
            draws += 2;
            triSlots += vuPreTSlots(e);
        }
        vuRecPassCopy(rowsV, usedT, muted);
        passes++;
    }
    g_rd.passSamples = samples;
    g_rdPerf.vuPrePasses = passes;
    g_rdPerf.vuPreVertices = verts;
    g_rdPerf.vuPreSprites = sprites;
    g_rdPerf.vuTriSlots = triSlots;
    /* the draws made: one a batch under g_rd.vuPreBatch, else one an
       entry and stage (s_preEntryDraws either way) */
    g_rdPerf.vuPreDraws = draws;
    s_preEntryDraws = batched ? entryDraws : draws;
}

/* rd__gpu_shutdown: the record's textures and buffers, the entries */
static void vuMergeFree(void); /* the draw merge's modules (below) */

void rd__vu_prepass_shutdown(void)
{
    recFree();
    for (int v = 0; v < RD_VS_COUNT; v++) {
        if (g_rd.vsBatch[v].id) {
            rhi_destroy_shader(g_rd.vsBatch[v]);
        }
        g_rd.vsBatch[v] = (RhiShader){0};
    }
    g_rd.vuPreBatch = false;
    vuMergeFree();
    free(s_items);
    free(s_itemOrder);
    s_items = NULL;
    s_itemOrder = NULL;
    s_itemCap = s_itemN = 0;
    if (s_recNull.id) {
        rhi_destroy_buffer(s_recNull);
    }
    s_recNull = (RhiBuffer){0};
    free(s_pre);
    s_pre = NULL;
    s_preCap = s_preN = s_preAt = 0;
    s_preReplay = 0;
}

/* rd_set_vu_prepass_off's reason ("" none) */
static char s_preHostWhy[128];

void rd_set_vu_prepass_off(const char *reason)
{
    snprintf(s_preHostWhy, sizeof(s_preHostWhy), "%s", reason != NULL ? reason : "");
}

bool rd_vu_prepass_on(void)
{
    return g_rd.vuPrepass;
}

/* how the pre-pass draws (rd__vu_prepass_init's line) */
static char s_preBatchWhy[192];

/* The pre-pass's batches (g_rd.vuPreBatch) with the pre-pass on: the batch
 * builds of the ten pre-pass entries and the four record stages
 * (vu_X_pre_vs's is vu_X_batch_pre_vs), unless [dev] vu_prepass_batch =
 * false (ICO_RD_VU_PREPASS_BATCH=0); one missing keeps one draw an entry.
 * Before any pipeline (the keys are the same either way, rd_pipeline.c) */
static void preBatchInit(void)
{
    for (int v = 0; v < RD_VS_COUNT; v++) {
        if (g_rd.vsBatch[v].id) {
            rhi_destroy_shader(g_rd.vsBatch[v]);
        }
        g_rd.vsBatch[v] = (RhiShader){0};
    }
    g_rd.vuPreBatch = false;
    const char *env = getenv("ICO_RD_VU_PREPASS_BATCH");
    if (env && strcmp(env, "0") == 0) {
        snprintf(s_preBatchWhy, sizeof(s_preBatchWhy),
                 "one an entry and stage ([dev] vu_prepass_batch = false, "
                 "ICO_RD_VU_PREPASS_BATCH=0)");
        return;
    }
    if (!g_rd.vuPrepass) {
        s_preBatchWhy[0] = '\0';
        return;
    }
    bool ok = true;
    for (int v = 0; v < RD_VS_COUNT && ok; v++) {
        if (!rd__vs_is_vu_pre((uint8_t)v) && !rd__vs_is_vu_stage((uint8_t)v)) {
            continue;
        }
        /* vu_X_pre_vs -> vu_X_batch_pre_vs */
        const char *e = rd__vs_name((uint8_t)v);
        const size_t n = strlen(e);
        char name[64];
        if (n < 7 || strcmp(e + n - 7, "_pre_vs") != 0) {
            ok = false;
            break;
        }
        snprintf(name, sizeof(name), "%.*s_batch_pre_vs", (int)(n - 7), e);
        g_rd.vsBatch[v] = rd__make_shader(name);
        if (!g_rd.vsBatch[v].id) {
            rd__log("VU pre-pass: %s could not be made: its draws are one an entry", name);
            ok = false;
        }
    }
    if (!ok) {
        for (int v = 0; v < RD_VS_COUNT; v++) {
            if (g_rd.vsBatch[v].id) {
                rhi_destroy_shader(g_rd.vsBatch[v]);
            }
            g_rd.vsBatch[v] = (RhiShader){0};
        }
        snprintf(s_preBatchWhy, sizeof(s_preBatchWhy),
                 "one an entry and stage (a batch module could not be made)");
        return;
    }
    g_rd.vuPreBatch = true;
    snprintf(s_preBatchWhy, sizeof(s_preBatchWhy),
             "batched: one point draw a run of up to %u entries with the same stage, FrameCB and "
             "group 1 ([dev] vu_prepass_batch = false: one an entry and stage)",
             RD_VU_PRE_BATCH_MAX);
}

/* ------------------------------------------------------- the draw merge
 * docs/port/VU1_PROGRAMS.md, "The draw merge".  g_rd.vuMerge: every draw
 * of a mesh or grid program's entry and of vu_main_vs goes through the
 * program's merge build (vsMerge: vu_X_merge_vs, vu_X_merge_plain_vs where
 * the fallback takes the plain variant, vu_merge_main_vs), which reads its
 * VuCB and VuBoneCB through t5 by a table (vu_common.hlsli vu_merge_entry),
 * so consecutive draws with the same state are one indexed draw (vuDraw,
 * vuRunTake, rd__replay_vu_flush).  Off: [dev] vu_merge = false
 * (ICO_RD_VU_MERGE=0), Direct3D 11 (its run-time compile and its views of
 * the ring are not set up for t5 in the main draws), or a merge module
 * missing.  Before any pipeline (the keys are the same either way,
 * rd_pipeline.c) */
static char s_mergeWhy[224];

/* the mesh and grid programs' entries and the main shader with a merge
 * build (the skinned programs and the particles have none: their draws are
 * drawn one a command) */
static const uint8_t kMergeVs[] = {RD_VS_VU_PRELIT,    RD_VS_VU_LIT,  RD_VS_VU_LIT_SPEC,
                                   RD_VS_VU_REFLECT,   RD_VS_VU_GRID, RD_VS_VU_GRID_LIT,
                                   RD_VS_VU_GRID_SPEC, RD_VS_VU_MAIN};

void rd__vu_merge_name(uint8_t vs, bool plain, char *out, size_t size)
{
    if (vs == RD_VS_VU_MAIN) {
        snprintf(out, size, "vu_merge_main_vs");
        return;
    }
    /* vu_X_vs -> vu_X_merge_vs, vu_X_merge_plain_vs */
    const char *e = rd__vs_name(vs);
    const size_t n = strlen(e);
    snprintf(out, size, "%.*s_merge%s_vs", (int)(n > 3 ? n - 3 : 0), e, plain ? "_plain" : "");
}

const char *rd__vu_merge_text(void)
{
    return g_rd.vuMerge ? "on" : "off";
}

static void vuMergeFree(void)
{
    for (int v = 0; v < RD_VS_COUNT; v++) {
        if (g_rd.vsMerge[v].id) {
            rhi_destroy_shader(g_rd.vsMerge[v]);
        }
        g_rd.vsMerge[v] = (RhiShader){0};
    }
    g_rd.vuMerge = false;
}

static void vuMergeInit(void)
{
    vuMergeFree();
    const char *env = getenv("ICO_RD_VU_MERGE");
    if (env && strcmp(env, "0") == 0) {
        snprintf(s_mergeWhy, sizeof(s_mergeWhy),
                 "off ([dev] vu_merge = false, ICO_RD_VU_MERGE=0): one GPU draw a VU draw");
    } else if (rhi_backend() == RHI_BACKEND_D3D11) {
        snprintf(s_mergeWhy, sizeof(s_mergeWhy),
                 "off on Direct3D 11 (the main draws do not bind t5 there): one GPU draw a VU "
                 "draw");
    } else {
        bool ok = true;
        for (size_t i = 0; i < sizeof(kMergeVs) && ok; i++) {
            const uint8_t v = kMergeVs[i];
            char name[64];
            rd__vu_merge_name(v, v != RD_VS_VU_MAIN && g_rd.vuFallbackPlain, name, sizeof(name));
            g_rd.vsMerge[v] = rd__make_shader(name);
            if (!g_rd.vsMerge[v].id) {
                snprintf(s_mergeWhy, sizeof(s_mergeWhy),
                         "off (%s could not be made): one GPU draw a VU draw", name);
                ok = false;
            }
        }
        if (ok) {
            g_rd.vuMerge = true;
            snprintf(s_mergeWhy, sizeof(s_mergeWhy),
                     "on: consecutive mesh and grid draws with the same pipeline, textures, "
                     "DrawCB, FrameCB and scissor drawn as one indexed draw of up to %u, through "
                     "the programs' merge builds ([dev] vu_merge = false draws one a command)",
                     RD_VU_MERGE_MAX);
        } else {
            vuMergeFree();
        }
    }
    rd__log("VU merge: %s", s_mergeWhy);
}

void rd__vu_prepass_init(const RhiLimits *lim)
{
    g_rd.vuPrepassWhy[0] = '\0';
    const char *env = getenv("ICO_RD_VU_PREPASS");
    const uint32_t bytes = lim ? lim->colorBytesPerPixel : 0u;
    if (s_preHostWhy[0] != '\0') {
        snprintf(g_rd.vuPrepassWhy, sizeof(g_rd.vuPrepassWhy), "%s", s_preHostWhy);
    } else if (env && strcmp(env, "0") == 0) {
        snprintf(g_rd.vuPrepassWhy, sizeof(g_rd.vuPrepassWhy),
                 "by the settings ([dev] vu_prepass = false, ICO_RD_VU_PREPASS=0)");
    } else if (lim != NULL && lim->tiler && !(env && strcmp(env, "1") == 0)) {
        /* a PowerVR phone's pictures shook between two ticks through the
         * record path and were steady through the programs' entries (the
         * replay is byte-identical on lavapipe both ways, so it is the
         * device's side: a visibility between the record's copies and the
         * stages' reads, not found yet); the programs' entries draw there
         * until it is */
        snprintf(g_rd.vuPrepassWhy, sizeof(g_rd.vuPrepassWhy),
                 "on a tile-based GPU (the pictures between two ticks shook through it on a "
                 "PowerVR phone; [dev] vu_prepass = true turns it on)");
    } else if (bytes != 0u && bytes < ICO_VU_REC_BYTES && rhi_backend() == RHI_BACKEND_D3D11) {
        /* d3d11_device.c reports 32 bytes: the record's read-back copies
         * are not in that backend (a texture only into a readback buffer) */
        snprintf(g_rd.vuPrepassWhy, sizeof(g_rd.vuPrepassWhy),
                 "on Direct3D 11 (the backend holds the colour targets to %u bytes a pixel, the "
                 "record %u: its copies are not in that backend)",
                 bytes, ICO_VU_REC_BYTES);
    } else if (bytes != 0u && bytes < ICO_VU_REC_BYTES) {
        snprintf(g_rd.vuPrepassWhy, sizeof(g_rd.vuPrepassWhy),
                 "on this device (its colour targets hold %u bytes a pixel together, the record "
                 "%u: an Apple A9 or A10 GPU)",
                 bytes, ICO_VU_REC_BYTES);
    }
    g_rd.vuPrepass = g_rd.vuPrepassWhy[0] == '\0';
    if (!s_recNull.id) {
        /* group 1's slot 4 before the slot has a record buffer */
        s_recNull = rhi_create_buffer(&(RhiBufferDesc){256, RHI_BUF_STORAGE_READ | RHI_BUF_COPY_DST,
                                                       RHI_MEM_DEVICE, "rd vu record (none)"});
        if (!s_recNull.id) {
            rd__log("VU pre-pass: its 256-byte placeholder buffer could not be made; group 1's "
                    "slot 4 binds the upload ring until a record is made");
        }
    }
    s_recFails = 0;
    if (lim != NULL && lim->moltenvk && bytes == 0u && rhi_adapter_name() != NULL &&
        strncmp(rhi_adapter_name(), "Apple ", 6) == 0) {
        /* rhi_metal_color_bytes does not read this Apple GPU's name: no
           bound assumed (the families it names grow, never shrink); the
           self-test turns the pre-pass off if a pipeline of it cannot be
           made or draws wrong */
        rd__log("VU pre-pass: the Apple GPU \"%s\" is not one the colour-target table names: "
                "no bound on its bytes a pixel assumed; the start-up self-test checks it",
                rhi_adapter_name());
    }
    preBatchInit();
    vuMergeInit(); /* the draw merge's modules (the fallback's variant is chosen) */
    if (g_rd.vuPrepass) {
        rd__log("VU pre-pass: on: each mesh and grid vertex and triangle and each sprite "
                "evaluated once before the lists (two passes), %u bytes a record slot in %u "
                "RGBA32_UINT targets, two slots a triangle (three under the tick cameras); [dev] "
                "vu_prepass = false draws the old way; its draws %s",
                ICO_VU_REC_BYTES, ICO_VU_REC_PLANES, s_preBatchWhy);
    } else {
        rd__log("VU pre-pass: off %s: every VU draw evaluates its triangle's corners in the "
                "triangle's own vertex shader",
                g_rd.vuPrepassWhy);
    }
}

void rd__set_vu_prepass(bool on)
{
    const uint32_t bytes = g_rd.hasDevice ? rhi_limits()->colorBytesPerPixel : 0u;
    g_rd.vuPrepass = on && (bytes == 0u || bytes >= ICO_VU_REC_BYTES);
    snprintf(g_rd.vuPrepassWhy, sizeof(g_rd.vuPrepassWhy), "%s",
             g_rd.vuPrepass ? "" : "by the tests (rd__set_vu_prepass)");
}

static void doVuDraw(Replay *r, const RdFrame *f, const RdCmd *c);

/* A VU draw under its camera's picture (Replay.picture: the frame group's
 * g_frame.yz) */
void rd__replay_vu(Replay *r, const RdFrame *f, const RdCmd *c)
{
    r->picture = (uint32_t)c->b[3] | ((uint32_t)c->b[6] << 8);
    doVuDraw(r, f, c);
    r->picture = 0;
}

static void doVuDraw(Replay *r, const RdFrame *f, const RdCmd *c)
{
    rd__span_idx(NULL);
    if (r->st.ds.test.date != RD_DATE_OFF) {
        /* DATE: its snapshot or its stencil bit is recorded as the draw is
           bound (rd__replay_prepare_draw, rd__replay_bind_draw_keep), after
           the merge's run */
        rd__replay_vu_flush(r);
    }
    /* the source rd__vu_prepass set up, or the command's own now */
    const VuPre *pre = vuPreFind(c);
    VuSource local;
    const VuSource *src = pre ? &pre->src : &local;
    if (!pre && !vuSource(f, c, &local)) {
        return;
    }
    /* a draw under the ticks' cameras (vuSource), once a draw */
    g_rdPerf.vuTickDraws += (src->vcb.draw[2] & ICO_VU_TICKS) != 0u;
    DrawSetup ds;
    if (!rd__replay_prepare_draw(r, &ds, NULL)) {
        return;
    }
    const RdVuPayload p = src->p;
    RdMeshRec *m = src->m;
    IcoVuCB vcb = src->vcb;
    rd__alpha_range_of_draw(&r->st,
                            &(RdAlphaSrc){.kind = RD_ALPHA_SRC_VU,
                                          .prog = p.prog,
                                          .cmdType = c->type,
                                          .f = f,
                                          .c = c,
                                          .mesh = m,
                                          .vcb = &vcb},
                            &s_vuAlpha);
    const uint8_t progVs = src->vs;
    uint8_t vs = progVs;
    if (pre && pre->rec) {
        /* the record's stages ran under the FrameCB of the pre-pass's walk:
         * a draw under another state (a walk that did not follow
         * replayFrame's) takes the old way, counted and logged once */
        const uint32_t fk[6] = {r->st.color, r->st.depth,     r->st.gsW,
                                r->st.gsH,   r->st.useOffset, r->picture};
        if (memcmp(fk, pre->frameKey, sizeof(fk)) != 0) {
            static bool logged; /* once a run */
            if (!logged) {
                logged = true;
                rd__log("VU pre-pass: a command's state at its draw is not the pre-pass's "
                        "(target %u/%u, offset %u/%u): drawn the old way (reported once)",
                        r->st.color, pre->frameKey[0], r->st.useOffset, pre->frameKey[4]);
            }
            pre = NULL;
        }
    }
    if (pre && pre->rec) {
        /* the main shaders over the pre-passes' record */
        vuPreRecord(pre, &vcb);
        vs = pre->particle ? RD_VS_VU_PARTICLE_MAIN : RD_VS_VU_MAIN;
    }
    const RhiBuffer streamBuf = src->streamBuf, indexBuf = src->indexBuf;
    const uint64_t streamBufSize = src->streamBufSize, streamOff = src->streamOff;
    const uint64_t streamSize = src->streamSize, indexOff = src->indexOff;
    const uint64_t bonesOff = src->bonesOff;

    if (g_rdWin.first && g_rdWin.drawLast) {
        /* the draw note's command (rd__win_vu_note) */
        g_rdVuCur.type = c->type;
        g_rdVuCur.prog = p.prog;
        g_rdVuCur.clip = p.clip;
        g_rdVuCur.batch = p.firstBatch;
        g_rdVuCur.index = m ? m->index : NULL;
        g_rdVuCur.marks = m ? m->kickMark : NULL;
        g_rdVuCur.markCount = m ? m->markCount : 0u;
        g_rdVuCur.mesh = m ? m->name : NULL;
    }
    if (c->type == RDC_GRID) {
        /* indices k = 2 .. stripLen-1 of every strip (mesh.vsm has no
         * strip flag); the stream's layout and the draw's mark: vuSource */
        const uint32_t vpb = p.vertsPerBatch;
        const uint32_t ni = p.batchCount * (vpb - 2) * 3;
        const uint64_t iOff = rd__ring_alloc((uint64_t)ni * 4, 16);
        if (iOff == ~0ull) {
            return;
        }
        g_rdPerf.uploadStreamBytes += (uint64_t)ni * 4;
        uint32_t *idx = (uint32_t *)(g_rd.ringMap[rd__ring_slot()] + iOff);
        if (g_rdWin.first && g_rdWin.drawLast) {
            g_rdVuCur.index = idx;
        }
        /* the span from the grid's shape, not from idx (the mapped ring is
         * uncached memory): ICO_VU_INDEX(2, 0) to ICO_VU_INDEX(the last
         * batch's last vertex, 2) (vuSpanOf); the merge rewrites the list
         * from the shape too (vuRunWrite) */
        const VuIdx gix = {g_rd.ring[rd__ring_slot()], iOff, NULL, p.batchCount, vpb};
        for (uint32_t b = 0, k = 0; b < p.batchCount; b++) {
            for (uint32_t v = 2; v < vpb; v++) {
                for (uint32_t cc = 0; cc < 3; cc++) {
                    idx[k++] = ICO_VU_INDEX(b * vpb + v, cc);
                }
            }
        }
        RhiBindGroup g2 = rd__replay_bind_draw_keep(r, &ds);
        if (!g2.id) {
            return;
        }
        vuDraw(r, &r->st, &ds, g2, p.prog, vs, progVs, streamBuf, streamBufSize, streamOff,
               streamSize, &vcb, bonesOff, &gix, 0, ni);
        return;
    }
    if (c->type == RDC_PARTICLES) {
        RhiBindGroup g2 = rd__replay_bind_draw_keep(r, &ds);
        if (!g2.id) {
            return;
        }
        const VuIdx none = {{0}, 0, NULL, 0, 0}; /* not indexed */
        vuDraw(r, &r->st, &ds, g2, p.prog, vs, progVs, streamBuf, streamBufSize, streamOff,
               streamSize, &vcb, bonesOff, &none, 0, 6 * p.vertsPerBatch);
        return;
    }
    /* static and skinned meshes: the device's list, the same on the CPU */
    RhiBindGroup g2 = rd__replay_bind_draw_keep(r, &ds);
    if (!g2.id) {
        return;
    }
    const VuIdx mix = {indexBuf, indexOff, m->index, 0, 0};
    uint32_t last = p.firstBatch + p.batchCount;
    if (last > m->batchCount || p.batchCount == 0) {
        last = m->batchCount;
    }
    /* a sheet of the content table out of the box: its own triangles
     * only, never the appended tiles (rd__vu_sheet_end) */
    const bool extended = rd__vu_extended();
    if (p.clip != RD_VU_CLIP_SCISSOR) {
        const RdVuBatchRec *b0 = &m->batches[p.firstBatch], *b1 = &m->batches[last - 1];
        const uint32_t end = rd__vu_sheet_end(m, b1->firstIndex + b1->indexCount, extended);
        if (end > b0->firstIndex) {
            vuDraw(r, &r->st, &ds, g2, p.prog, vs, progVs, streamBuf, streamBufSize, streamOff,
                   streamSize, &vcb, bonesOff, &mix, b0->firstIndex, end - b0->firstIndex);
        }
        return;
    }
    IcoVuCB cut = vcb, kick = vcb;
    cut.draw[2] |= ICO_VU_CUT_ONLY;
    kick.draw[2] |= ICO_VU_KICK_ONLY;
    RdStateBlock fan = r->st;
    fan.ds.abe = 1; /* the fans' PRIM is the common block's 0x5D: ABE on */
    for (uint32_t b = p.firstBatch; b < last; b++) {
        const RdVuBatchRec *br = &m->batches[b];
        const uint32_t end = rd__vu_sheet_end(m, br->firstIndex + br->indexCount, extended);
        if (end <= br->firstIndex) {
            continue;
        }
        if (g_rdWin.first && g_rdWin.drawLast) {
            g_rdVuCur.batch = b;
        }
        vuDraw(r, &fan, &ds, g2, p.prog, vs, progVs, streamBuf, streamBufSize, streamOff,
               streamSize, &cut, bonesOff, &mix, br->firstIndex, end - br->firstIndex);
        vuDraw(r, &r->st, &ds, g2, p.prog, vs, progVs, streamBuf, streamBufSize, streamOff,
               streamSize, &kick, bonesOff, &mix, br->firstIndex, end - br->firstIndex);
    }
}

/* rd_replay_vu.c: the replay's VU draws (rd_replay.c's command walk calls
 * rd__replay_vu for RDC_MESH, RDC_SKINNED, RDC_GRID and RDC_PARTICLES):
 * the VU stream, VuCB and VuBoneCB a draw binds, their group, and the draw
 * calls under the planned passes; and the vertex pre-pass before list 0
 * (rd__vu_prepass: each mesh and grid command's vertices once, into the
 * record the triangle stage reads).  The replay's state and helpers are
 * rd_replay.h's. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
 * The vertex pre-pass (docs/port/RENDER.md, "The vertex pre-pass";
 * rd__vu_prepass below) draws a record per vertex into three RGBA32_UINT
 * textures, ICO_VU_REC_W texels a row, shared by the frame slots (each
 * replay's pass moves them to RENDER_TARGET and back to COPY_SRC), and
 * copies them into the frame slot's record buffer, the three planes one
 * after the other, which the triangle stage reads (group 1 slot 4,
 * vu_common.hlsli vu_rec_buf).  Both grow by powers of two from
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
        rd__log("VU pre-pass: the record holds %u vertices (%u rows, %llu KB a frame slot)",
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

/* The VU layout's group binds the stream's whole buffer (the
 * mesh arena chunk or the ring) at t0, the ring at offset 0 for the
 * three dynamic uniforms and the slot's record buffer at t4, so it is made
 * once per stream buffer and replay;
 * a draw selects its stream through VuCB's vu_draw.x (the qword index of
 * its first batch, vu_common.hlsli) and its blocks through the bind-time
 * offsets.  A stream whose end lies past the device's largest storage range
 * gets a group of its own bound at its offset, vu_draw.x 0. */
#define RD_VU_GROUPS (RD_MESH_CHUNKS + 2) /* every arena chunk and the ring */

typedef struct VuEntry {
    uint32_t epoch, buffer, rec;
    uint64_t range;
    RhiBindGroup group;
} VuEntry;

static VuEntry s_vuGroups[RD_VU_GROUPS];

static RhiBindGroup vuGroupMake(RhiBuffer streamBuf, uint64_t streamOff, uint64_t streamSize)
{
    RhiBinding b[5];
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
    return rd__replay_bind_group(g_rd.layoutVu, b, 5);
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
            e->range == range) {
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
        free->range = range;
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
 * one (the MSAA epoch), a storage pass or a readable one (the exact
 * blend's). */
static uint32_t s_splitSerial, s_splitCount;

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
    if (r->passStorage || r->passReadable || g_rd.passSamples > 1 || rd__replay_msaa_want() > 1u) {
        static bool logged; /* once a run */
        if (!logged) {
            logged = true;
            rd__log("pass split: not in a multisampled, storage or readable pass (those are "
                    "drawn whole)");
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

/* One VU draw call under the planned passes of state s.  vs: the vertex
 * shader (RD_VS_VU_TRI for a command the pre-pass recorded: its pipelines
 * carry RD_PROG_PRELIT whatever the program, rd_mesh.c enumerateVuVariant),
 * progVs the program's own entry, taken when the triangle stage's pipeline
 * cannot be made. */
static void vuDraw(Replay *r, const RdStateBlock *s, const DrawSetup *ds, RhiBindGroup g2,
                   uint8_t prog, uint8_t vs, uint8_t progVs, RhiBuffer streamBuf,
                   uint64_t streamBufSize, uint64_t streamOff, uint64_t streamSize,
                   const IcoVuCB *vcb0, uint64_t bonesOff, int indexed, uint32_t first,
                   uint32_t count)
{
    if (count == 0) {
        return;
    }
    if (g_rd.vuPrepass && vs != RD_VS_VU_TRI && rd__vu_pre_vs(progVs)) {
        g_rdPerf.vuPreFallbacks++; /* a mesh or grid draw the pre-pass did not record */
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
    const int npl = rd__plan_screen_draw(s, RD_PRIM_TRIANGLES, RD_SPACE_WORLD, ds->tc->format,
                                         ds->depthFmt, pl);
    for (int i = 0; i < npl; i++) {
        pl[i].key.gs.program = vs == RD_VS_VU_TRI ? RD_PROG_PRELIT : prog;
        pl[i].key.vs = vs;
        /* the planner's sprite_texa_ps becomes vu_texa_ps */
        pl[i].key.fs = pl[i].key.fs == RD_FS_SPRITE_TEXA ? RD_FS_VU_TEXA : RD_FS_VU;
        if (vcb->batch[2] != 0u) {
            /* a clip plane (rd_chain.h): the pixels past it are discarded,
             * which the exact blend's storage path must know (rd_dst_read.c
             * passDiscards) */
            pl[i].flags |= ICO_DF_PLANE;
        }
    }
    /* the exact blend's passes, two passes without dual-source blend */
    const int np = rd__replay_expand_passes(r, pl, npl, dp);
    const RdStateBlock saved = r->st;
    r->st = *s; /* fillDrawCB reads the state the passes were planned from */
    for (int i = 0; i < np; i++) {
        RhiPipeline p = rd__get_pipeline(&dp[i].key);
        if (!p.id && dp[i].key.vs == RD_VS_VU_TRI && !rd__pipeline_gathering()) {
            /* the triangle stage's pipeline could not be made: the
             * program's own entry, which ignores the record (logged once) */
            static bool logged; /* once a run */
            if (!logged) {
                logged = true;
                rd__log("VU pre-pass: a pipeline of vu_tri_vs could not be made; its draws "
                        "take their programs' own vertex shaders (reported once)");
            }
            dp[i].key.vs = progVs;
            dp[i].key.gs.program = prog;
            p = rd__get_pipeline(&dp[i].key);
            g_rdPerf.vuPreFallbacks++;
        }
        if (!p.id) {
            continue;
        }
        IcoDrawCB cb;
        rd__replay_fill_draw_cb(r, &dp[i], ds, &cb);
        const uint64_t dOff = cbCopy(&cb, sizeof(cb), ua);
        const uint64_t vOff = cbCopy(vcb, sizeof(*vcb), ua);
        if (dOff == ~0ull || vOff == ~0ull) {
            continue;
        }
        const uint32_t offs[3] = {(uint32_t)dOff, (uint32_t)vOff, (uint32_t)bonesOff};
        rd__replay_dst_pass_mode(r, &dp[i].key); /* a storage pass for a storage destination read */
        rhi_cmd_set_pipeline(rd__replay_cl(), p);
        rd__bind_uniform(rd__replay_cl(), 0, r->frameBG);
        rhi_cmd_set_bind_group_offsets(rd__replay_cl(), 1, g1, offs, 3);
        rhi_cmd_set_bind_group(rd__replay_cl(), 2, g2);
        rd__replay_dst_bind(r, &dp[i].key);
        if (g_rdWin.first && g_rdWin.drawLast && g_rdWin.passes == g_rdWin.last) {
            /* its class (the second level's VS_ONLY) the program's own
               entry, whether it draws through it or vu_tri_vs */
            rd__win_vu_note(r->st.tex, &dp[i].key, vcb, progVs, indexed, first, count);
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
            g_rd.stats.draws++;
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
     * vu_strip_card), in a state other than out of the box (not with the
     * plain shaders only) when a strip of the draw is a card at either
     * tick: in VuBoneCB after the bones a skinned mesh reads
     * (RdMeshRec.boneQwUsed), from qword 0 for the others; vu_strip = (first kick, the card test, kicks covered, first
     * qword).  A draw with more kicks than VuBoneCB holds covers the first
     * ones (the others are no card) */
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
    if (rd__vu_extended()) {
        /* a triangle the region test drops is clipped unless it belongs
         * to a card (vu_region_rule) */
        vcb.draw[2] |= ICO_VU_EXTENDED;
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

/* -------------------------------------------------- the vertex pre-pass
 * docs/port/RENDER.md, "The vertex pre-pass".  Before list 0 (replayFrame,
 * after the uploads and the first clears) every mesh and grid command of
 * the replayed lists gets its VuSource (kept for its draws) and the
 * vertices its index range reads, the lowest kick less 2 to the highest
 * (a grid: every strip's); one render pass into the record textures draws
 * each command's vertices as points through its program's pre-pass entry
 * (rd__vu_pre_key) at consecutive texels, and the planes are copied into
 * the slot's record buffer.  doVuDraw then draws the command's triangles
 * through vu_tri_vs with VuCB's vu_rec, vu_lo and vu_hi set (vuPreRecord).
 * A command past the record's RD_VU_REC_ROWS_MAX rows, or whose pre-pass
 * pipeline is missing, keeps its VuSource and is drawn the old way. */
typedef struct VuPre {
    const RdCmd *cmd;
    VuSource src;
    uint32_t first, count; /* the vertices [first, first + count) */
    uint32_t base;         /* the first one's record texel */
    uint8_t preVs;         /* its pre-pass entry */
    uint8_t rec;           /* drawn into the record */
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

/* VuCB's pre-pass fields for entry e: vertex v's texel base - first + v */
static void vuPreRecord(const VuPre *e, IcoVuCB *v)
{
    v->rec[0] = e->base - e->first; /* wrapping: the shader adds the vertex */
    v->rec[1] = s_recBufRows[rd__ring_slot()] * ICO_VU_REC_W;
    v->rec[2] = e->first;
    vuTriRule(e->src.vs, v);
}

/* the pre-pass's draws' DrawCB */
static IcoDrawCB s_preDrawCB;

/* rd__vu_prepass_sabotage: the first command's records one texel off */
static bool s_preSabotage;

/* rd__vu_prepass is recording (rd__vu_prepass_running) */
static bool s_preRunning;

bool rd__vu_prepass_running(void)
{
    return s_preRunning;
}

void rd__vu_prepass_sabotage(bool on)
{
    s_preSabotage = on;
}

static void vuPrepassRun(const RdFrame *f, int keep);

void rd__vu_prepass(const RdFrame *f, int keep)
{
    s_preN = s_preAt = 0;
    s_preReplay = 0;
    if (!g_rd.vuPrepass) {
        return;
    }
    s_preRunning = true;
    vuPrepassRun(f, keep);
    s_preRunning = false;
}

static void vuPrepassRun(const RdFrame *f, int keep)
{
    /* 1. every mesh and grid command: its source and its vertices */
    uint64_t total = 0;
    for (int l = rd__first_list(keep); l < RD_LIST_COUNT; l++) {
        const RdCmdList *list = &f->lists[l];
        for (uint32_t i = 0; i < list->count; i++) {
            const RdCmd *c = &list->cmds[i];
            if (c->type != RDC_MESH && c->type != RDC_SKINNED && c->type != RDC_GRID) {
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
            e->preVs = rd__vu_pre_vs(e->src.vs);
            vuPreRange(c, &e->src, &e->first, &e->count);
            s_preN++;
            if (e->preVs && e->count) {
                e->base = (uint32_t)total;
                total += e->count;
            }
        }
    }
    s_preReplay = g_rd.replayCounter;
    if (total == 0) {
        return;
    }
    /* 2. the record's size: powers of two from the least */
    uint32_t rows = s_recTexRows > RD_VU_REC_ROWS_MIN ? s_recTexRows : RD_VU_REC_ROWS_MIN;
    while ((uint64_t)rows * ICO_VU_REC_W < total && rows < RD_VU_REC_ROWS_MAX) {
        rows *= 2u;
    }
    if ((uint64_t)rows * ICO_VU_REC_W < total) {
        static bool logged; /* once a run */
        if (!logged) {
            logged = true;
            rd__log("VU pre-pass: a frame of %llu vertices past the record's %u: the commands "
                    "past it are drawn the old way (reported once)",
                    (unsigned long long)total, rows * ICO_VU_REC_W);
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
    const uint64_t cap = (uint64_t)s_recTexRows * ICO_VU_REC_W;
    /* 3. one pass, a draw a command, single-sampled whatever the last
     * pass was (rd__get_pipeline's drawKey) */
    const uint8_t samples = g_rd.passSamples;
    g_rd.passSamples = 1;
    uint32_t used = 0;
    for (uint32_t i = 0; i < s_preN; i++) {
        VuPre *e = &s_pre[i];
        if (!e->preVs || !e->count || (uint64_t)e->base + e->count > cap) {
            continue;
        }
        const RdPipeKeyInt key = rd__vu_pre_key(e->preVs);
        if (!rd__get_pipeline(&key).id && !rd__pipeline_gathering()) {
            static bool logged; /* once a run */
            if (!logged) {
                logged = true;
                rd__log("VU pre-pass: the pipeline of %s could not be made: its commands are "
                        "drawn the old way (reported once)",
                        rd__vs_name(e->preVs));
            }
            continue;
        }
        e->rec = 1;
        used = e->base + e->count;
    }
    const uint32_t usedRows = (used + ICO_VU_REC_W - 1u) / ICO_VU_REC_W;
    /* the draws' DrawCB (b1 of group 1; no pre-pass entry reads it) */
    const uint64_t ua = rhi_limits()->uniformAlign;
    memset(&s_preDrawCB, 0, sizeof(s_preDrawCB));
    const uint64_t dOff = usedRows ? cbCopy(&s_preDrawCB, sizeof(s_preDrawCB), ua) : ~0ull;
    if (dOff == ~0ull) {
        for (uint32_t i = 0; i < s_preN; i++) {
            s_pre[i].rec = 0; /* the ring is full: drawn the old way */
        }
        g_rd.passSamples = samples;
        return;
    }
    /* a piece of the GPU timing (rd_perf.c rd__perf_pass): the pass and the
     * copies into the record buffer */
    rd__perf_pass(rd__replay_cl(), RD_PERF_PASS_PREPASS, RD_PERF_ID_RECORD, ICO_VU_REC_W, usedRows,
                  -1);
    for (uint32_t k = 0; k < ICO_VU_REC_PLANES; k++) {
        rd__transition(rd__replay_cl(), s_recTex[k], &s_recTexState[k], RHI_STATE_RENDER_TARGET);
    }
    /* the pass ring's entry: the record's rows drawn and its vertices
     * (rd_passring.h RD_PASSK_VU_PRE); the loss bisect's pass window
     * numbers it pass 1 (rd_window.c): outside the window its draws and
     * copies are left out, and the slot's record buffer keeps what an
     * earlier replay of the frame wrote */
    rhi_cmd_checkpoint(rd__replay_cl(), rd__replay_pass_push(RD_PASSK_VU_PRE, 0, ICO_VU_REC_W,
                                                             usedRows, 0, used, 0, 0, -1));
    const bool muted = rd__win_muted();
    RhiRenderPassDesc rp;
    memset(&rp, 0, sizeof(rp));
    for (uint32_t k = 0; k < ICO_VU_REC_PLANES; k++) {
        rp.color[k].texture = s_recTex[k];
        rp.color[k].load = RHI_LOAD_DONT_CARE;
        rp.color[k].store = RHI_STORE_STORE;
    }
    rp.colorCount = ICO_VU_REC_PLANES;
    rp.width = ICO_VU_REC_W;
    rp.height = usedRows;
    rhi_cmd_begin_render_pass(rd__replay_cl(), &rp);
    /* the shader's mapping is the record's rows (vu_common.hlsli
     * vu_pre_out: vu_rec.y / ICO_VU_REC_W), the pass's area the rows used */
    const RhiViewport vp = {0.0f, 0.0f, (float)ICO_VU_REC_W, (float)s_recBufRows[rd__ring_slot()],
                            0.0f, 1.0f};
    rhi_cmd_set_viewport(rd__replay_cl(), &vp);
    const RhiRect sc = {0, 0, ICO_VU_REC_W, usedRows};
    rhi_cmd_set_scissor(rd__replay_cl(), &sc);
    uint32_t verts = 0, draws = 0;
    for (uint32_t i = 0; i < s_preN; i++) {
        VuPre *e = &s_pre[i];
        if (!e->rec) {
            continue;
        }
        IcoVuCB vcb = e->src.vcb;
        vuPreRecord(e, &vcb);
        if (s_preSabotage && draws == 0) {
            vcb.rec[0] += 1u; /* rd_vuprepass_selftest's sabotage: written a texel on */
        }
        const RhiBindGroup g1 = vuGroup(e->src.streamBuf, e->src.streamBufSize, e->src.streamOff,
                                        e->src.streamSize, &vcb.draw[0]);
        const uint64_t vOff = cbCopy(&vcb, sizeof(vcb), ua);
        const RdPipeKeyInt key = rd__vu_pre_key(e->preVs);
        const RhiPipeline p = rd__get_pipeline(&key);
        if (!g1.id || vOff == ~0ull || (!p.id && !rd__pipeline_gathering())) {
            e->rec = 0; /* drawn the old way */
            continue;
        }
        if (!p.id) {
            continue; /* only naming keys: the triangle stage's are named too */
        }
        const uint32_t offs[3] = {(uint32_t)dOff, (uint32_t)vOff, (uint32_t)e->src.bonesOff};
        rhi_cmd_set_pipeline(rd__replay_cl(), p);
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
            rd__win_vu_note(0, &key, &vcb, e->src.vs, 0, e->first, e->count);
        }
        rd__replay_draw(rd__replay_cl(), e->count, 0, 1);
        verts += e->count;
        draws++;
    }
    rhi_cmd_end_render_pass(rd__replay_cl());
    g_rd.passSamples = samples;
    /* 4. the planes' rows into the slot's record buffer, plane after
     * plane (visible to the vertex shaders: rhi.h
     * rhi_cmd_copy_texture_to_buffer) */
    const uint64_t plane = (uint64_t)s_recBufRows[rd__ring_slot()] * ICO_VU_REC_W * 16u;
    for (uint32_t k = 0; k < ICO_VU_REC_PLANES; k++) {
        rd__transition(rd__replay_cl(), s_recTex[k], &s_recTexState[k], RHI_STATE_COPY_SRC);
        const RhiRect region = {0, 0, ICO_VU_REC_W, usedRows};
        if (!muted) {
            rhi_cmd_copy_texture_to_buffer(rd__replay_cl(), s_recTex[k], RHI_ASPECT_COLOR, region,
                                           s_recBuf[rd__ring_slot()], plane * k,
                                           ICO_VU_REC_W * 16u);
        }
    }
    g_rdPerf.vuPrePasses = 1;
    g_rdPerf.vuPreVertices = verts;
    g_rdPerf.vuPreDraws = draws;
}

/* rd__gpu_shutdown: the record's textures and buffers, the entries */
void rd__vu_prepass_shutdown(void)
{
    recFree();
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
    if (g_rd.vuPrepass) {
        rd__log("VU pre-pass: on: each mesh and grid vertex evaluated once before the lists, "
                "%u bytes a vertex in %u RGBA32_UINT targets; [dev] vu_prepass = false draws "
                "the old way",
                ICO_VU_REC_BYTES, ICO_VU_REC_PLANES);
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
    /* the source rd__vu_prepass set up, or the command's own now */
    const VuPre *pre = vuPreFind(c);
    VuSource local;
    const VuSource *src = pre ? &pre->src : &local;
    if (!pre && !vuSource(f, c, &local)) {
        return;
    }
    DrawSetup ds;
    if (!rd__replay_prepare_draw(r, &ds, NULL)) {
        return;
    }
    const RdVuPayload p = src->p;
    RdMeshRec *m = src->m;
    IcoVuCB vcb = src->vcb;
    const uint8_t progVs = src->vs;
    uint8_t vs = progVs;
    if (pre && pre->rec) {
        /* the triangle stage over the pre-pass's record */
        vuPreRecord(pre, &vcb);
        vs = RD_VS_VU_TRI;
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
         * batch's last vertex, 2) */
        if (vpb >= 3u && p.batchCount > 0u) {
            rd__span_range(ICO_VU_INDEX(2u, 0u), ICO_VU_INDEX(p.batchCount * vpb - 1u, 2u));
        }
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
        rhi_cmd_set_index_buffer(rd__replay_cl(), g_rd.ring[rd__ring_slot()], iOff, true);
        vuDraw(r, &r->st, &ds, g2, p.prog, vs, progVs, streamBuf, streamBufSize, streamOff,
               streamSize, &vcb, bonesOff, 1, 0, ni);
        return;
    }
    if (c->type == RDC_PARTICLES) {
        RhiBindGroup g2 = rd__replay_bind_draw_keep(r, &ds);
        if (!g2.id) {
            return;
        }
        vuDraw(r, &r->st, &ds, g2, p.prog, vs, progVs, streamBuf, streamBufSize, streamOff,
               streamSize, &vcb, bonesOff, 0, 0, 6 * p.vertsPerBatch);
        return;
    }
    /* static and skinned meshes */
    RhiBindGroup g2 = rd__replay_bind_draw_keep(r, &ds);
    if (!g2.id) {
        return;
    }
    rhi_cmd_set_index_buffer(rd__replay_cl(), indexBuf, indexOff, true);
    rd__span_idx(m->index);
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
                   streamSize, &vcb, bonesOff, 1, b0->firstIndex, end - b0->firstIndex);
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
               streamSize, &cut, bonesOff, 1, br->firstIndex, end - br->firstIndex);
        vuDraw(r, &r->st, &ds, g2, p.prog, vs, progVs, streamBuf, streamBufSize, streamOff,
               streamSize, &kick, bonesOff, 1, br->firstIndex, end - br->firstIndex);
    }
}

/* rd_replay_vu.c: the replay's VU draws (rd_replay.c's command walk calls
 * rd__replay_vu for RDC_MESH, RDC_SKINNED, RDC_GRID and RDC_PARTICLES):
 * the VU stream, VuCB and VuBoneCB a draw binds, their group, and the draw
 * calls under the planned passes.  The replay's state and helpers are
 * rd_replay.h's. */
#include <string.h>
#include "rd_internal.h"
#include "rd_mesh.h"
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

/* The VU layout's group binds the stream's whole buffer (the
 * mesh arena chunk or the ring) at t0 and the ring at offset 0 for the
 * three dynamic uniforms, so it is made once per stream buffer and replay;
 * a draw selects its stream through VuCB's vu_draw.x (the qword index of
 * its first batch, vu_common.hlsli) and its blocks through the bind-time
 * offsets.  A stream whose end lies past the device's largest storage range
 * gets a group of its own bound at its offset, vu_draw.x 0. */
#define RD_VU_GROUPS (RD_MESH_CHUNKS + 2) /* every arena chunk and the ring */

typedef struct VuEntry {
    uint32_t epoch, buffer;
    uint64_t range;
    RhiBindGroup group;
} VuEntry;

static VuEntry s_vuGroups[RD_VU_GROUPS];

static RhiBindGroup vuGroupMake(RhiBuffer streamBuf, uint64_t streamOff, uint64_t streamSize)
{
    RhiBinding b[4];
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
    return rd__replay_bind_group(g_rd.layoutVu, b, 4);
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
    VuEntry *free = NULL;
    for (int i = 0; i < RD_VU_GROUPS; i++) {
        VuEntry *e = &s_vuGroups[i];
        if (e->epoch == rd__replay_bind_epoch() && e->buffer == streamBuf.id && e->range == range) {
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

/* One VU draw call under the planned passes of state s. */
static void vuDraw(Replay *r, const RdStateBlock *s, const DrawSetup *ds, RhiBindGroup g2,
                   uint8_t prog, uint8_t vs, RhiBuffer streamBuf, uint64_t streamBufSize,
                   uint64_t streamOff, uint64_t streamSize, const IcoVuCB *vcb0, uint64_t bonesOff,
                   int indexed, uint32_t first, uint32_t count)
{
    if (count == 0) {
        return;
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
    const uint8_t vuExt = rd__vu_draw_ext(vcb->draw[2]); /* the VS variant */
    for (int i = 0; i < npl; i++) {
        pl[i].key.gs.program = prog;
        pl[i].key.vs = vs;
        pl[i].key.vuExt = vuExt;
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
        if (!p.id) {
            continue;
        }
        IcoDrawCB cb;
        rd__replay_fill_draw_cb(r, &dp[i], ds, &cb);
        const uint64_t dOff = ringCopy(&cb, sizeof(cb), ua);
        const uint64_t vOff = ringCopy(vcb, sizeof(*vcb), ua);
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
            rd__win_vu_note(r->st.tex, &dp[i].key, vcb, indexed, first, count);
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
            return;
        }
    }
    DrawSetup ds;
    if (!rd__replay_prepare_draw(r, &ds, NULL)) {
        return;
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
    const bool cardsRead = cardWords && cards.any && rd__vu_extended() && !g_rd.vuPlainOnly;
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
        bonesOff = ringCopy(&bc, sizeof(bc), ua);
    } else {
        if (s_zeroBonesFor != g_rd.replayCounter) {
            static const IcoVuBoneCB zero;
            s_zeroBones = ringCopy(&zero, sizeof(zero), ua);
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
        }
        streamOff = m->ringStream;
        streamSize = vb + mb;
        indexOff = m->ringIndex;
        vcb.draw[1] = m->qwPerVertex;
    } else {
        streamSize = (uint64_t)p.streamQw * 16;
        streamOff = ringCopy(stream, streamSize, ua);
        vcb.draw[1] = p.qwPerVertex;
    }
    if (bonesOff == ~0ull || streamOff == ~0ull || indexOff == ~0ull) {
        return;
    }
    const uint8_t vs = vuVs(p.prog, p.code);
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
        /* the Mesh3D buffer as it is: per strip the VIF qword, tag and colour
         * before the vertices, MSCNT after them; indices k = 2 .. stripLen-1
         * of every strip (mesh.vsm has no strip flag) */
        const uint32_t vpb = p.vertsPerBatch;
        vcb.draw[3] = vpb;
        vcb.batch[0] = 3;
        vcb.batch[1] = 1;
        const uint32_t ni = p.batchCount * (vpb - 2) * 3;
        const uint64_t iOff = rd__ring_alloc((uint64_t)ni * 4, 16);
        if (iOff == ~0ull) {
            return;
        }
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
        /* strip i is the grid's rows i and i + 1
         * (Primitive.c prim_UpdateMesh3D), so with two strips or more
         * every strip shares a row of positions with a neighbour: joined,
         * never a card (vu_region_rule), the draw's mark */
        vcb.mark[1] = p.batchCount >= 2 ? ICO_VU_MARK_JOINED : 0u;
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
        vuDraw(r, &r->st, &ds, g2, p.prog, vs, streamBuf, streamBufSize, streamOff, streamSize,
               &vcb, bonesOff, 1, 0, ni);
        return;
    }
    if (c->type == RDC_PARTICLES) {
        RhiBindGroup g2 = rd__replay_bind_draw_keep(r, &ds);
        if (!g2.id) {
            return;
        }
        vuDraw(r, &r->st, &ds, g2, p.prog, vs, streamBuf, streamBufSize, streamOff, streamSize,
               &vcb, bonesOff, 0, 0, 6 * p.vertsPerBatch);
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
            vuDraw(r, &r->st, &ds, g2, p.prog, vs, streamBuf, streamBufSize, streamOff, streamSize,
                   &vcb, bonesOff, 1, b0->firstIndex, end - b0->firstIndex);
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
        vuDraw(r, &fan, &ds, g2, p.prog, vs, streamBuf, streamBufSize, streamOff, streamSize, &cut,
               bonesOff, 1, br->firstIndex, end - br->firstIndex);
        vuDraw(r, &r->st, &ds, g2, p.prog, vs, streamBuf, streamBufSize, streamOff, streamSize,
               &kick, bonesOff, 1, br->firstIndex, end - br->firstIndex);
    }
}

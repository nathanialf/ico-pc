/* rd_mesh_test.c: the mesh path.
 *
 * Packet.c, RegistPacket.c, MicroCode.c, DisplayP2O.c and Primitive.c with
 * the 2D layer (GifPacket.c, DisplayList.c, DmaPacket.c) and Matrix.c,
 * compiled as the window build has them (ICO_HOST, ICO_RD); the rest of the
 * game is stubbed below (the texture module by a stand-in that writes the
 * TEX1/TEX0 packet and the UV offset packet tex_TransTexture writes).  The
 * models are synthetic p2o-decoded parts built here; the strip order case
 * alone reads the disc image, at run time.
 *
 * Cases:
 *   prelit   a normal_c model (four strips, two VU batches, a strip restart
 *            inside a batch) through p2o_MakePacket and reg_DispObj, region
 *            test (code 32)
 *   scissor  the same model with the clip type that gives code 36: two draws
 *            a batch (cut with ABE forced on, then kick)
 *   cluster  a skinned model (two bones) through reg_dispCObj (code 20)
 *   grid     a Mesh3D through prim_InitMesh3D / prim_UpdateMesh3D /
 *            prim_DispMesh3D (mesh code 20)
 *   particle prim_InitParticleByPartition / prim_DispParticle (code 18), the
 *            batch keyed by its emitter and the life of its slot: a new
 *            emitter at a deleted one's address (prim_DeleteParticle) gets
 *            a new count, and a live emitter drawn every tick keeps its
 *            count while 8192 other emitters are stamped (three in four
 *            deleted, the rest never drawn again)
 *   full-frame  the prelit model named a full-frame title model
 *            (title_logo.c ico_title_fullframe_model, stubbed): reg_DispObj
 *            widens its nodes' x axis (nodeMtx's x row) by the frame's
 *            width / 512 for the draw (the VuCB's matrices change at 688,
 *            not at 512) and puts it back; as a camera-locked node (flag 2,
 *            title_back's kind) its model-to-screen x row is the width /
 *            512 times the 4:3 one at 688 and 2048, the other rows
 *            unchanged; on the device at 4:3 its picture is byte-identical
 *            to the unflagged draw's; at 16:9 on the native frame (688 x 512)
 *            the unflagged draw is the Enhanced 1x 4:3 picture at its GS X
 *            (columns 88..599, nothing compressed), the full-frame one is
 *            wider than it, and a full-frame item the game draws at the
 *            frame's width covers all 688 columns
 *   content table  rd_content_hide.c's verbs on the top of the listed
 *            closing box (st08a_p4), below ("the content table")
 *   sheets   rd_content_extend.c's tiles appended to the deja veil's
 *            quadrant sheet, and the sheet drawn at 4:3 (the same picture
 *            with and without them) and 16:9 (the sides covered), below
 *            ("the content table's sheets")
 *   photo veil  photo mode's Deja vu (port/game/photo_view.c): the deja
 *            veil's four quadrant sheets, as the stage's packets hold them,
 *            at the two layers' places on the widest frame (60:9, 2560
 *            GS pixels across): with rd_content_extend.c's tiles every
 *            pixel of the frame is drawn, without them the sides are not,
 *            below ("the photo veil")
 *   photo reel  photo mode's Deja vu film reel (port/game/photo_view.c):
 *            the four noise specks of stage 40's frame v2227 drawn at the
 *            dump's places on the 16:9 frame: none tiled, every pixel drawn
 *            inside a speck's own place, all in the 4:3 picture's columns,
 *            the speck left of the picture off the frame, below ("the photo
 *            reel")
 *   natural chains  rd_content_chain.c's table and a let-down chain laid
 *            out as BgAnimation.c lays it out with the option on: the links
 *            at their size along the chain, the far end where the keyed
 *            scale puts it, one clip plane at the top for all of them; the
 *            planes reach the draws of the noted nodes through reg_DispObj
 *            (RDC_MESH b[5], f[0]); on the device the plane's far side is
 *            not drawn, its near side byte for byte as without it, below
 *            ("natural chains")
 *   strip order  the title logo's strips Packet.c draws in another order of
 *            their entries (pac_HostStrips): the table, the I's through
 *            p2o_MakePacket, and with the disc image given as the argument
 *            their geometry (below, "strip order"); recording only
 *   model packs  the mesh identity, replacements from a
 *            tagless stream, one drawn in the prelit packet's place,
 *            rd_vu_mesh_retire and the sweep; recording only
 *   pack hooks the game's side of model packs on a pack in
 *            rd_mesh_modelpack/ under the working folder: the dump at the
 *            draw (both parts, the cluster one with its skeleton), the
 *            dumps moved into replacements/ and the pack switched off and
 *            on: the draws record the replacements Packet.c made (the
 *            cluster one with the object's bone count); the morph path
 *            (pac_HostRefreshFor) declines a replaced mesh and draws the
 *            original again; recording only
 *   culls    a packet whose own box fails in a part that passed
 *            is drawn with the clipping program (code 36), not dropped;
 *            a part's and an object's dropped boxes are noted on
 *            the frame for F12's cull line (rules 3 and 2, with the model's
 *            name); an accessory whose box is dropped still registers its
 *            shadow volume
 *   boxes    with the disc image: every *.p2o of every pack through PObj.c's
 *            loader (AllocPObj): each vertex a strip names lies in its
 *            part's box and its object's box (items 2 and 3); one model
 *            through InitPObj with a y offset of 105 (the PAL table's
 *            shinkan_a): its object box moved with it; recording only
 *
 * Checks on the recording (no device needed): the mesh built from the
 * packet (vertex count, batches, the index list against vu1ref_static_kicks);
 * the recorded commands (program, code, clip); the VuCB against what the EE
 * packet code uploads (the common block, the UV offset, the matrices +0x140,
 * +0x200 x node and +0x80 x node, the cluster bones and lights); the GS state
 * at the draw (the material packet's ALPHA, CLAMP and FBA, the texture the
 * TEX0 bound, PRIM.ABE from the batch tag); nothing undecoded.
 * Then on a Vulkan device (exit 77 without one, after the recording checks
 * passed): each case rendered into SCENE, and the CPU reference's triangles
 * (vu1_ref.h, the same VU state built independently) drawn through the
 * sprite path (rd_screen_prims) in the same GS state; the two images agree
 * within 1 per channel and the draw covers pixels.  Every pipeline created
 * is in the enumerated reachable set. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gltf.h"
#include "host_fs.h"
#include "modelpack.h"
#include "rd_chain.h"
#include "rd_internal.h"
#include "rd_mesh.h"
#include "shader_consts.h"
#include "vk/rhi_vk.h"
#include "vu1_ref.h"
#include "xxh3.h"
/* the game's side */
#include "typedef.h"
#include "DisplayList.h"
#include "DisplayP2O.h"
#include "DmaPacket.h"
#include "GifHost.h"
#include "GifPacket.h"
#include "GsBase.h"
#include "Light.h"
#include "Matrix.h"
#include "MicroCode.h"
#include "Packet.h"
#include "Primitive.h"
#include "RegistPacket.h"
#include "eeword.h"
/* the synthetic models (shared with modelpack_test.c) */
#include "vu_models.h"
/* the disc (the strip order case) */
#include "df_pack.h"
#include "vfs.h"
/* PObj.c's loader (the boxes case) */
#include "charFileManager.h"

static int failures;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)
#include "mesh_env.h"
#include "../../test/ico_shard.h"

static Sub15C s_objA, s_objB;

static Model s_modelA, s_modelB;

static int memEq(const float *a, const float *b, int qw)
{
    return memcmp(a, b, (size_t)qw * 16) == 0;
}

/* The EE uploads of a normal object: +0x140 = +0x100 x node, +0x200 x node,
 * +0x80 x node (reg_setNMatrixPacket_setMatrix) */
static void normalMatrices(const Model *m, float out[12][4])
{
    float node[16];
    memcpy(node, m->nodeMtx[0], 64);
    _MulMatrix(out[0], matrixptr + 0x100, node);
    _MulMatrix(out[4], matrixptr + 0x200, node);
    _MulMatrix(out[8], matrixptr + 0x80, node);
}

static void checkMesh(const PacHeader *pk, int qpv)
{
    RdMesh m = {pac_HostMesh((PacHeader *)pk)};
    const RdMeshRec *r = rd__mesh_rec(m.id);
    CHECK(r && r->vu, "a VU mesh for the packet");
    if (!r) {
        return;
    }
    Batches b;
    packetBatches(pk, &b);
    CHECK(r->qwPerVertex == (uint32_t)qpv, "%u quadwords a vertex, expected %d", r->qwPerVertex,
          qpv);
    CHECK(r->vertexCount == NV, "%u vertices, expected %d", r->vertexCount, NV);
    CHECK((int)r->batchCount == b.n && b.n >= 2, "%u batches, the packet has %d", r->batchCount,
          b.n);
    /* the static kicks: vu1ref_static_kicks over the stream */
    static float stw[NV];
    static int first[NV], kicks[NV];
    int v = 0;
    for (int i = 0; i < b.n; i++) {
        uint32_t tag;
        memcpy(&tag, b.in[i][0], 4);
        for (uint32_t k = 0; k < (tag & 0x7FFF) && v < NV; k++, v++) {
            stw[v] = b.in[i][1 + k * qpv + (qpv - 2)][3];
            first[v] = v - (int)k;
        }
    }
    int nk = vu1ref_static_kicks(stw, first, v, kicks);
    CHECK(r->indexCount == (uint32_t)nk * 3, "%u indices, %d kicks", r->indexCount, nk);
    for (int i = 0; i < nk && (uint32_t)i * 3 + 2 < r->indexCount; i++) {
        for (int c = 0; c < 3; c++) {
            if (r->index[i * 3 + c] != ICO_VU_INDEX(kicks[i], c)) {
                CHECK(0, "index %d.%d %u, expected %u", i, c, r->index[i * 3 + c],
                      ICO_VU_INDEX(kicks[i], c));
                return;
            }
        }
    }
    /* the stream: the batches' vertices without tags */
    for (int i = 0, at = 0; i < b.n; i++) {
        uint32_t tag;
        memcpy(&tag, b.in[i][0], 4);
        CHECK(memcmp(r->stream[at], b.in[i][1], (size_t)(tag & 0x7FFF) * qpv * 16) == 0,
              "batch %d's vertices in the stream", i);
        at += (int)(tag & 0x7FFF) * qpv;
    }
}

static void checkState(const RdStateBlock *s, int expectAbe, int expectFba)
{
    CHECK(s->ds.texEnabled && s->tex == s_tex.id, "the TEX0 bound the test texture (%u)", s->tex);
    CHECK(s->ds.blend == RD_BLEND_LERP_AS, "material ALPHA 0x44 (%u)", s->ds.blend);
    CHECK(s->ds.abe == expectAbe, "PRIM.ABE %d from the batch tag (%d)", expectAbe, s->ds.abe);
    /* FBA: the material's fbaOff == 0; cluster materials take
     * debug_shadow_flag == 1 instead (pac_makeMaterialTable) */
    CHECK(s->ds.fba == expectFba, "material FBA %d (%d)", expectFba, s->ds.fba);
    /* Packet.c maps the material's wrap 1 to CLAMP 4: S repeat, T clamp */
    CHECK(s->ds.wrap.s == RD_WRAP_REPEAT && s->ds.wrap.t == RD_WRAP_CLAMP,
          "material CLAMP 4 (%u %u)", s->ds.wrap.s, s->ds.wrap.t);
    CHECK(s->gouraud == 1, "PRIM.IIP 1");
}

/* the scene's GS width: the native frame's (688 for the 16:9 checks) */
static uint32_t s_sceneW = 512;

static void recordPrelit(Sub15C *o, int clipRet)
{
    static const uint8_t grey[4] = {40, 40, 60, 0x80};
    s_clipRet = clipRet;
    dl_Clear();
    setCommon();
    dl_SetDLPriority(0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), s_sceneW, 512, 1);
    rd_clear_target(rd_target(RD_TARGET_SCENE), grey, 1, 0);
    reg_DispObj(o);
    dl_Swap();
}

static void checkPrelitRecording(int code, int clip)
{
    const RdFrame *f = rd__last_frame();
    Found fd;
    walkFrame(f, &fd);
    CHECK(fd.n == 1, "one mesh draw, got %d", fd.n);
    if (fd.n < 1) {
        return;
    }
    const RdCmd *c = fd.cmd[0];
    const float *mem;
    RdVuPayload p = payloadOf(f, c, &mem);
    CHECK(c->type == RDC_MESH && fd.list[0] == 0, "RDC_MESH in list 0");
    CHECK(p.prog == RD_PROG_PRELIT && p.code == code && p.clip == clip,
          "normal_c code %d clip %d (got prog %u code %u clip %u)", code, clip, p.prog, p.code,
          p.clip);
    CHECK(p.firstBatch == 0 && p.batchCount == 2, "all batches (%u from %u)", p.batchCount,
          p.firstBatch);
    /* VuCB: the common block with the UV offset in qw 2, then the matrices */
    float want[36][4];
    memcpy(want, s_common, sizeof(s_common));
    want[2][0] = kUv[0];
    want[2][1] = kUv[1];
    float nm[12][4];
    normalMatrices(&s_modelA, nm);
    memcpy(want[16], nm, sizeof(nm));
    CHECK(memEq(mem, want[0], 16), "VuCB 0..15: the common block, UV offset (%g %g)", mem[8],
          mem[9]);
    CHECK(memEq(mem + 64, want[16], 12), "VuCB 16..27: +0x140, +0x200 and +0x80 times the node");
    checkState(&fd.st[0], 0, 1);
    CHECK(gif_HostUndecodedTotal() == 0, "%u undecoded register writes", gif_HostUndecodedTotal());
}

static void recordCluster(void)
{
    static const uint8_t grey[4] = {40, 40, 60, 0x80};
    s_clipRet = 1;
    dl_Clear();
    setCommon();
    dl_SetDLPriority(0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 1);
    rd_clear_target(rd_target(RD_TARGET_SCENE), grey, 1, 0);
    reg_DispObj(&s_objB);
    dl_Swap();
}

/* the cluster bones the EE packs: nodeMtx[i] x clusterMtx[i] */
static void clusterBones(float out[8][4])
{
    for (int i = 0; i < 2; i++) {
        _MulMatrix(out[i * 4], s_modelB.nodeMtx[i], s_modelB.clusterMtx[i]);
    }
}

static void checkClusterRecording(void)
{
    const RdFrame *f = rd__last_frame();
    Found fd;
    walkFrame(f, &fd);
    CHECK(fd.n == 1, "one skinned draw, got %d", fd.n);
    if (fd.n < 1) {
        return;
    }
    const RdCmd *c = fd.cmd[0];
    const float *mem;
    RdVuPayload p = payloadOf(f, c, &mem);
    CHECK(c->type == RDC_SKINNED, "RDC_SKINNED");
    CHECK(p.prog == RD_PROG_SKIN && p.code == 20 && p.clip == RD_VU_CLIP_REGION,
          "cluster code 20 (prog %u code %u)", p.prog, p.code);
    CHECK(p.boneQw == 240, "VU memory 16..255 as bones (%u)", p.boneQw);
    float want[16][4];
    memcpy(want, s_common, sizeof(want));
    want[2][0] = kUv[0];
    want[2][1] = kUv[1];
    want[2][3] = 1.0f; /* the fade alpha of reg_setCMatrixPacket */
    CHECK(memEq(mem, want[0], 16), "VuCB 0..15: common block, UV offset, fade alpha");
    CHECK(memEq(mem + 28 * 4, &s_modelB.light.normal[0][0], 4) &&
              memEq(mem + 32 * 4, &s_modelB.light.color[0][0], 4),
          "VuCB 28..35: the light matrices (vf13..vf20)");
    float bones[8][4];
    clusterBones(bones);
    CHECK(memEq(mem + 36 * 4, bones[0], 8), "bones: nodeMtx x clusterMtx at VU 16 and 20");
    checkState(&fd.st[0], 0, 0);
}

/* ---------------------------------------------------------- the grid */

static Mesh3D *s_grid;

static void makeGrid(void)
{
    /* PRIM 0x5C: strip, IIP, TME, ABE (cloth); col2 RGBA 200 150 100 128 */
    s_grid = prim_InitMesh3D(6, 4, 0, 0x5C, 0xC8966480u, 0);
    for (int i = 0; i < s_grid->nx * s_grid->ny; i++) {
        int x = i % s_grid->nx, y = i / s_grid->nx;
        s_grid->pos[i].x = -12.0f + 4.8f * (float)x;
        s_grid->pos[i].y = -10.0f + 6.0f * (float)y;
        s_grid->pos[i].z = 2.0f + 0.05f * (float)(x + y);
        s_grid->pos[i].w = 1.0f;
        s_grid->st[i].x = (float)x / 5.0f;
        s_grid->st[i].y = (float)y / 3.0f;
        s_grid->st[i].z = 1.0f;
    }
    prim_UpdateMesh3D(s_grid, 1 | 8 | 0x10, 0);
    prim_UpdateMesh3D(s_grid, 1 | 8 | 0x10, 1);
}

static void recordGrid(void)
{
    static const uint8_t grey[4] = {40, 40, 60, 0x80};
    dl_Clear();
    setCommon();
    dl_SetDLPriority(0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 1);
    rd_clear_target(rd_target(RD_TARGET_SCENE), grey, 1, 0);
    _SetCurrentMatrix(matrixptr + 0x100);
    prim_DispMesh3D(s_grid, 0, 0, 0);
    dl_Swap();
}

static void checkGridRecording(void)
{
    const RdFrame *f = rd__last_frame();
    Found fd;
    walkFrame(f, &fd);
    CHECK(fd.n == 1 && fd.cmd[0]->type == RDC_GRID, "one grid draw (%d)", fd.n);
    if (fd.n < 1) {
        return;
    }
    const float *mem;
    RdVuPayload p = payloadOf(f, fd.cmd[0], &mem);
    CHECK(p.prog == RD_PROG_GRID && p.code == 20, "mesh code 20 (prog %u code %u)", p.prog, p.code);
    CHECK(p.batchCount == (uint32_t)s_grid->strips &&
              p.vertsPerBatch == (uint32_t)s_grid->stripLen && p.streamQw == (uint32_t)s_grid->qwc,
          "the Mesh3D buffer whole (%u strips of %u, %u qw)", p.batchCount, p.vertsPerBatch,
          p.streamQw);
    CHECK(memEq(mem + 64, (const float *)(matrixptr + 0x100), 4),
          "VuCB 16..19: SET_MESH_MATRIX's matrix (vf01..vf04)");
    CHECK(mem[8] == kUv[0] && mem[9] == kUv[1], "UV offset from the texture packet");
    CHECK(fd.st[0].ds.abe == 1 && fd.st[0].ds.texEnabled, "PRIM 0x5C: ABE and TME");
    CHECK(fd.st[0].ds.fba == 0, "FBA 0 from prim_DispMesh3D's packet");
}

/* ------------------------------------------------------ the particles */

static PrimParticle *s_part;

#define NPART 12

static void makeParticles(void)
{
    /* x, y, z of the init call: the size scale and the UV step (du, dv) */
    s_part = prim_InitParticleByPartition(NPART, 1.0f, 0.25f, 0.25f, 0, "testtex", 0, NULL);
    /* the same particles in both buffers (prim_DispParticle alternates) */
    for (int i = 0; i < NPART; i++) {
        float a[4] = {rnd(-14, 14), rnd(-12, 12), rnd(1.9f, 2.4f), rnd(0.5f, 1.5f)};
        float b[4] = {rnd(0, 0.75f), rnd(0, 0.75f), (float)(int)rnd(40, 250),
                      i == 3 ? 0.0f : (float)(int)rnd(30, 128)};
        for (int c = 0; c < 2; c++) {
            memcpy(&s_part->objs[c]->vtx[i][0], a, 16);
            memcpy(&s_part->objs[c]->vtx[i][4], b, 16);
        }
    }
}

static void recordParticles(void)
{
    static const uint8_t grey[4] = {40, 40, 60, 0x80};
    dl_Clear();
    setCommon();
    dl_SetDLPriority(6);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 1);
    rd_clear_target(rd_target(RD_TARGET_SCENE), grey, 1, 0);
    prim_DispParticle(s_part, matrixptr + 0x100);
    dl_Swap();
}

static void checkParticleRecording(void)
{
    const RdFrame *f = rd__last_frame();
    Found fd;
    walkFrame(f, &fd);
    CHECK(fd.n == 1 && fd.cmd[0]->type == RDC_PARTICLES && fd.list[0] == 6,
          "one particle draw in list 6 (%d)", fd.n);
    if (fd.n < 1) {
        return;
    }
    const float *mem;
    RdVuPayload p = payloadOf(f, fd.cmd[0], &mem);
    CHECK(p.prog == RD_PROG_PARTICLE && p.code == 18 && p.vertsPerBatch == NPART &&
              p.streamQw == 6 + 2 * NPART,
          "particle batch of %d (%u)", NPART, p.vertsPerBatch);
    CHECK(memEq(mem + 64, (const float *)(matrixptr + 0x100), 4) &&
              memEq(mem + 80, (const float *)(matrixptr + 0xC0), 4),
          "VuCB 16..23: SET_PARTICLE_MATRIX's two matrices");
    CHECK(fd.st[0].ds.abe == 1, "PRIM 0xD6: ABE");
    /* keyed by its emitter (mc_HostParticleKey), so the
     * presenter matches it whatever other emitters draw before it */
    const RdKey k = RD_KEY(s_part, 18, prim_HostParticleGen(s_part));
    CHECK(fd.cmd[0]->keyLo == (uint32_t)k && fd.cmd[0]->keyHi == (uint32_t)(k >> 32),
          "the batch is keyed by its emitter (%08x%08x)", fd.cmd[0]->keyHi, fd.cmd[0]->keyLo);
    /* a new emitter in the same slot (a one-shot effect freed, another
     * created at its address) is stamped anew and so keyed differently:
     * the presenter must not pair the two */
    const unsigned int gen = prim_HostParticleGen(s_part);
    prim_HostParticleStamp(s_part);
    CHECK(gen != 0 && prim_HostParticleGen(s_part) != gen &&
              RD_KEY(s_part, 18, prim_HostParticleGen(s_part)) != k,
          "a reused emitter slot gets a fresh key (gen %u -> %u)", gen,
          prim_HostParticleGen(s_part));
    /* an emitter deleted and another created at its address: the count
     * goes on from the deleted one's */
    {
        PrimParticle *e =
            prim_InitParticleByPartition(4, 1.0f, 0.25f, 0.25f, 0, "testtex", 0, NULL);
        CHECK(e != NULL, "a second emitter");
        if (e != NULL) {
            const unsigned int g1 = prim_HostParticleGen(e);
            prim_DeleteParticle(e);
            prim_HostParticleStamp(e); /* the next init at its address */
            const unsigned int g2 = prim_HostParticleGen(e);
            CHECK(g1 != 0 && g2 != 0 && g2 != g1,
                  "stamp, delete, stamp: the count goes on (gen %u -> %u)", g1, g2);
        }
    }
    /* a live emitter, drawn every tick, keeps its count while other
     * emitters come and go: three in four deleted, the rest left in the
     * table and never drawn (a partition freed whole), more than the table
     * holds, every slot's run of probes filled many times over (the
     * addresses are only hashed, never read) */
    {
        static __attribute__((aligned(16))) char fake[16 * 8192];
        const unsigned int ga = prim_HostParticleGen(s_part);
        int changed = 0;
        for (int i = 0; i < 8192; i++) {
            const PrimParticle *f = (const PrimParticle *)(const void *)(fake + 16 * i);
            prim_HostParticleStamp(f);
            if (i % 4 != 0) {
                prim_HostParticleForget(f);
            }
            changed += prim_HostParticleGen(s_part) != ga;
        }
        CHECK(ga != 0 && changed == 0,
              "a live emitter keeps its count (gen %u, changed on %d of 8192 ticks)", ga, changed);
    }
}

/* ------------------------------------------------------ the reference */

#define MAXREF 2048

static RdScreenVtx s_ref[MAXREF];

static int s_nref;

static void refVertex(const VuGsVertex *v)
{
    if (s_nref >= MAXREF) {
        return;
    }
    RdScreenVtx *o = &s_ref[s_nref++];
    o->x = (int32_t)vu_gs_x(v);
    o->y = (int32_t)vu_gs_y(v);
    o->z = (uint32_t)v->xyz[2];
    o->s = v->stq[0];
    o->t = v->stq[1];
    o->q = v->stq[2];
    for (int i = 0; i < 4; i++) {
        o->rgba[i] = (uint8_t)(v->rgba[i] & 255);
    }
}

static void refBatch(const VuBatchOut *out)
{
    for (int f = 0; f < out->fanCount; f++) {
        const VuGsFan *fan = &out->fans[f];
        for (int i = 2; i < fan->n; i++) {
            refVertex(&fan->v[0]);
            refVertex(&fan->v[i - 1]);
            refVertex(&fan->v[i]);
        }
    }
    static int kicks[VU_BATCH_MAX];
    int nk = vu1ref_kicks(out, kicks);
    for (int i = 0; i < nk; i++) {
        for (int j = 0; j < 3; j++) {
            refVertex(&out->v[kicks[i] - 2 + j]);
        }
    }
}

static void refCommon(Vu1Ref *r)
{
    vu1ref_init(r);
    vu1ref_load_common(r, (const float (*)[4])s_common);
    vu1ref_set_uv_offset(r, kUv);
}

static void refPrelit(const PacHeader *pk, int code)
{
    static Vu1Ref r;
    float m[12][4];
    refCommon(&r);
    normalMatrices(&s_modelA, m);
    vu1ref_normal_set_matrix(&r, (const float (*)[4])m);
    Batches b;
    packetBatches(pk, &b);
    s_nref = 0;
    static VuBatchOut out;
    for (int i = 0; i < b.n; i++) {
        vu1ref_normal_c(&r, code, b.in[i], &out);
        refBatch(&out);
    }
}

static void refCluster(const PacHeader *pk)
{
    static Vu1Ref r;
    float pk2[10][4];
    memset(pk2, 0, sizeof(pk2));
    float bones[8][4], light[8][4];
    qw4(pk2[0], ubitsF(9), 0, 0, 1.0f);
    clusterBones(bones);
    memcpy(pk2[1], bones, sizeof(bones));
    memcpy(light[0], s_modelB.light.normal, 64);
    memcpy(light[4], s_modelB.light.color, 64);
    refCommon(&r);
    vu1ref_cluster_set_matrix(&r, (const float (*)[4])pk2);
    vu1ref_cluster_set_light(&r, (const float (*)[4])light);
    Batches b;
    packetBatches(pk, &b);
    s_nref = 0;
    static VuBatchOut out;
    for (int i = 0; i < b.n; i++) {
        vu1ref_cluster(&r, 20, b.in[i], &out);
        refBatch(&out);
    }
}

static void refGrid(void)
{
    static Vu1Ref r;
    refCommon(&r);
    vu1ref_mesh_set_matrix(&r, (const float (*)[4])(matrixptr + 0x100));
    s_nref = 0;
    static VuBatchOut out;
    const float (*buf)[4] = s_grid->bufs[0];
    const int per = s_grid->stripLen * 2 + 4;
    for (int i = 0; i < s_grid->strips; i++) {
        vu1ref_mesh(&r, 20, buf + i * per + 1, &out);
        refBatch(&out);
    }
}

static void refParticles(void)
{
    static Vu1Ref r;
    float m[8][4];
    memcpy(m[0], matrixptr + 0x100, 64);
    memcpy(m[4], matrixptr + 0xC0, 64);
    refCommon(&r);
    vu1ref_particle_set_matrix(&r, (const float (*)[4])m);
    VuParticleOut out;
    vu1ref_particle(&r, (const float (*)[4])(const void *)s_part->objs[0]->num, 6 + 2 * NPART,
                    &out);
    s_nref = 0;
    for (int i = 0; i < out.count && s_nref + 2 <= MAXREF; i++) {
        const VuGsSprite *s = &out.s[i];
        for (int c = 0; c < 2; c++) {
            RdScreenVtx *o = &s_ref[s_nref++];
            o->x = s->xyz[c][0] & 0xFFFF;
            o->y = s->xyz[c][1] & 0xFFFF;
            o->z = (uint32_t)s->xyz[c][2];
            o->s = s->st[c][0];
            o->t = s->st[c][1];
            o->q = 1.0f;
            for (int k = 0; k < 4; k++) {
                o->rgba[k] = (uint8_t)(s->rgba[k] & 255);
            }
        }
    }
    CHECK(out.count == NPART - 1, "the reference draws %d particles (one has alpha 0)", out.count);
}

/* The GS state of the mesh draw, recorded as rd state for the reference. */
static void applyState(const RdStateBlock *s)
{
    rd_test(&s->ds.test);
    rd_z_write(s->ds.zwrite == RD_ZWRITE_ON);
    rd_fba(s->ds.fba);
    rd_pabe(s->ds.pabe);
    rd_col_clamp(s->ds.colclamp);
    rd_tex_a((RdTexA)s->ds.texa);
    rd_blend((RdBlend)s->ds.blend, s->ds.blendFix, s->ds.abe);
    rd_sampler_filter((RdFilter)s->ds.magFilter, (RdFilter)s->ds.minFilter);
    rd_sampler_wrap((RdWrap)s->ds.wrap.s, (RdWrap)s->ds.wrap.t);
    if (s->ds.texEnabled) {
        rd_texture((RdTex){s->tex}, (RdTexFn)s->ds.texFn, (RdTcc)s->ds.tcc);
    } else {
        rd_texture_off();
    }
    rd_gouraud((int)s->gouraud);
    rd_color_mask(s->ds.fbmsk);
    rd_uv_offset(0.0f, 0.0f);
}

static uint8_t s_imgA[512 * 512 * 4], s_imgB[512 * 512 * 4];

/* Frame 2: the reference triangles (or sprites) in the state the mesh draw
 * had, over the same clear. */
static void drawReference(const RdStateBlock *st, int list, RdPrim prim)
{
    static const uint8_t grey[4] = {40, 40, 60, 0x80};
    dl_Clear();
    dl_SetDLPriority(list);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 1);
    rd_clear_target(rd_target(RD_TARGET_SCENE), grey, 1, 0);
    applyState(st);
    rd_screen_prims(prim, s_ref, (uint32_t)s_nref, RD_SPACE_WORLD, 0, 0);
    dl_Swap();
}

static void compare(const char *what, int minCover)
{
    int bad = 0, maxd = 0, cover = 0;
    for (int i = 0; i < 512 * 512; i++) {
        int d = 0;
        for (int c = 0; c < 4; c++) {
            int e = abs((int)s_imgA[i * 4 + c] - (int)s_imgB[i * 4 + c]);
            d = e > d ? e : d;
        }
        maxd = d > maxd ? d : maxd;
        bad += d > 1;
        cover += s_imgA[i * 4] != 40 || s_imgA[i * 4 + 1] != 40 || s_imgA[i * 4 + 2] != 60;
    }
    printf("  %s: %d pixels drawn, max difference %d, %d over 1\n", what, cover, maxd, bad);
    CHECK(bad == 0, "%s: %d pixels differ from the reference by more than 1 (max %d)", what, bad,
          maxd);
    CHECK(cover >= minCover, "%s: only %d pixels drawn", what, cover);
}

/* One case on the device: the game path, then the reference. */
static void gpuCase(const char *what, void (*record)(void), void (*ref)(void), RdPrim prim,
                    int minCover)
{
    record();
    if (!readScene(s_imgA)) {
        CHECK(0, "%s: SCENE readback", what);
        return;
    }
    Found fd;
    walkFrame(rd__last_frame(), &fd);
    if (fd.n < 1) {
        CHECK(0, "%s: nothing recorded", what);
        return;
    }
    const RdStateBlock st = fd.st[0];
    const int list = fd.list[0];
    ref();
    drawReference(&st, list, prim);
    if (!readScene(s_imgB)) {
        CHECK(0, "%s: SCENE readback", what);
        return;
    }
    compare(what, minCover);
}

static PacHeader *packetA(void)
{
    return (PacHeader *)s_modelA.mdl.groups->packets;
}

static PacHeader *packetB(void)
{
    return (PacHeader *)s_modelB.mdl.groups->packets;
}

static void recPrelit(void)
{
    recordPrelit(&s_objA, 1);
}

/* the packet's clip type 2 (the model's shade) with gsb_ClipBox 2: code 36 */
static void recScissor(void)
{
    packetA()->clip = 2;
    recordPrelit(&s_objA, 2);
    packetA()->clip = 1;
}

static void refPrelit32(void)
{
    refPrelit(packetA(), 32);
}

static void refPrelit36(void)
{
    refPrelit(packetA(), 36);
}

static void refClusterA(void)
{
    refCluster(packetB());
}

/* ------------------------------------------------------------- stretch */

static void checkStretchRecording(void)
{
    /* VuCB 16..27 (the +0x140, +0x200 and +0x80 times the node matrices) of
       the one draw the last recording holds */
    float a[12 * 4], b[12 * 4], c[12 * 4], d[12 * 4];
    Found fd;
    const float *mem;
    float node0[16];
    memcpy(node0, s_modelA.nodeMtx[0], sizeof(node0));

#define RECORD_MATRICES(dst, model, width)                                                         \
    do {                                                                                           \
        s_fullFrameModel = (model);                                                                \
        gsFrame.w = (width);                                                                       \
        recordPrelit(&s_objA, 1);                                                                  \
        walkFrame(rd__last_frame(), &fd);                                                          \
        CHECK(fd.n == 1, "full-frame: one draw (%d)", fd.n);                                       \
        if (fd.n == 1) {                                                                           \
            payloadOf(rd__last_frame(), fd.cmd[0], &mem);                                          \
            memcpy((dst), mem + 64, sizeof(dst));                                                  \
        }                                                                                          \
    } while (0)
    RECORD_MATRICES(a, NULL, 512);
    RECORD_MATRICES(b, "test_prelit", 512);
    CHECK(memcmp(a, b, sizeof(a)) == 0, "full-frame: at 4:3 the model's matrices are unchanged");
    RECORD_MATRICES(c, "test_prelit", 688);
    CHECK(memcmp(a, c, sizeof(a)) != 0, "full-frame: at 688 the model is widened");
    CHECK(memcmp(s_modelA.nodeMtx[0], node0, sizeof(node0)) == 0,
          "full-frame: the node matrix is put back");
    RECORD_MATRICES(d, NULL, 688);
    CHECK(memcmp(a, d, sizeof(a)) == 0, "full-frame: another model at 688 is not widened");
    RECORD_MATRICES(d, NULL, 512);

    /* the camera-locked node (flag 2) title_back is (its draw's matrix is
       +0x640, the 500-unit projection, times the node's lengths and its
       position; the dumps' title frames): reg_setMMatrixPacket takes the
       scale from the lengths of nodeMtx's rows and never reads the node
       scale, so the widening is on nodeMtx's x row.  The model-to-screen
       matrix's x row (VuCB 16) is the frame's width / 512 times the 4:3
       one at 688 and at 2048 (48:9), the other rows unchanged, and the
       node matrix is put back */
    {
        static const int widths[] = {688, 2048};
        float saved640[32];
        float ref[12 * 4], wide[12 * 4];
        memcpy(saved640, matrixptr + 0x640, sizeof(saved640));
        memcpy(matrixptr + 0x640, matrixptr + 0x100, 64);
        identity((float *)(matrixptr + 0x680));
        s_objA.dispType = 2;
        s_modelA.nodes[0].flags.ll = 2;
        s_modelA.nodes[0].alpha = 1.0f; /* drawn: 1 - (1 - fade) x alpha = 0 */
        qw4(s_modelA.nodes[0].pos, 0.0f, 0.0f, 6.4f, 1.0f);
        RECORD_MATRICES(ref, NULL, 512);
        RECORD_MATRICES(wide, "test_prelit", 512);
        CHECK(memcmp(ref, wide, sizeof(ref)) == 0,
              "full-frame, camera-locked: at 4:3 the model's matrices are unchanged");
        CHECK(ref[0] != 0.0f && ref[1] == 0.0f && ref[2] == 0.0f,
              "full-frame, camera-locked: the model's x is the screen's x (%g %g %g)",
              (double)ref[0], (double)ref[1], (double)ref[2]);
        for (unsigned i = 0; i < sizeof(widths) / sizeof(widths[0]); i++) {
            const float k = (float)widths[i] / 512.0f;
            RECORD_MATRICES(wide, "test_prelit", widths[i]);
            int rowX = 1;
            for (int e = 0; e < 4; e++) {
                rowX &= fabsf(wide[e] - ref[e] * k) <= 1e-4f * fabsf(ref[e] * k) + 1e-6f;
            }
            CHECK(rowX,
                  "full-frame, camera-locked at %d: the x row is %g times the 4:3 one "
                  "(%g, expected %g)",
                  widths[i], (double)k, (double)wide[0], (double)(ref[0] * k));
            CHECK(memcmp(wide + 4, ref + 4, 12 * sizeof(float)) == 0,
                  "full-frame, camera-locked at %d: the y, z and position rows are unchanged",
                  widths[i]);
            CHECK(memcmp(s_modelA.nodeMtx[0], node0, sizeof(node0)) == 0,
                  "full-frame, camera-locked at %d: the node matrix is put back", widths[i]);
        }
        RECORD_MATRICES(wide, NULL, 2048);
        CHECK(memcmp(ref, wide, 16 * sizeof(float)) == 0,
              "full-frame, camera-locked: another model at 2048 is not widened");
        s_objA.dispType = 0;
        s_modelA.nodes[0].flags.ll = 0;
        s_modelA.nodes[0].alpha = 0.0f;
        qw4(s_modelA.nodes[0].pos, 0.0f, 0.0f, 0.0f, 0.0f);
        memcpy(matrixptr + 0x640, saved640, sizeof(saved640));
    }
#undef RECORD_MATRICES
    s_fullFrameModel = NULL;
    gsFrame.w = 512;
}

/* the 4:3 picture of the unflagged draw in the 16:9 run's preset
   (Enhanced 1x), for the 16:9 checks (stretch43Enhanced) */
static uint8_t s_stretch43[512 * 512 * 4];
static int s_have43;

/* the unflagged draw at 4:3 under Enhanced 1x: the 16:9 run differs from it
   in the frame's width alone (the Original run's picture may differ in the
   preset's own ways) */
static void stretch43Enhanced(void)
{
    s_fullFrameModel = NULL;
    recordPrelit(&s_objA, 1);
    s_have43 = readScene(s_stretch43);
    CHECK(s_have43, "stretch 4:3 Enhanced: SCENE readback");
}

/* at 4:3 the flag changes nothing */
static void stretchSamePixels(void)
{
    s_fullFrameModel = NULL;
    recordPrelit(&s_objA, 1);
    const int a = readScene(s_imgA);
    s_fullFrameModel = "test_prelit";
    recordPrelit(&s_objA, 1);
    const int b = readScene(s_imgB);
    s_fullFrameModel = NULL;
    CHECK(a && b, "stretch 4:3: SCENE readback");
    int cover = 0;
    for (int i = 0; i < 512 * 512; i++) {
        cover += s_imgA[i * 4] != 40 || s_imgA[i * 4 + 1] != 40 || s_imgA[i * 4 + 2] != 60;
    }
    CHECK(a && b && cover >= 2000 && memcmp(s_imgA, s_imgB, sizeof(s_imgA)) == 0,
          "stretch 4:3: the full-frame model's picture is the unflagged one's (%d pixels drawn)",
          cover);
}

#define WT 688 /* SCENE's GS width and texels at 16:9, 1x: the native frame */
#define WX0 88 /* its 4:3 columns' first, (688 - 512) / 2 */

static uint8_t s_wideImg[WT * 512 * 4], s_wideA[WT * 512 * 4];

/* the first and last columns of SCENE at 16:9 with a pixel drawn */
static int wideColumns(int *x0, int *x1)
{
    uint32_t w = 0, h = 0;
    if (!rd__read_target(rd_target(RD_TARGET_SCENE), s_wideImg, sizeof(s_wideImg), &w, &h) ||
        w != WT || h != 512) {
        return 0;
    }
    *x0 = WT;
    *x1 = -1;
    for (int y = 0; y < 512; y++) {
        for (int x = 0; x < WT; x++) {
            const uint8_t *p = &s_wideImg[(y * WT + x) * 4];
            if (p[0] != 40 || p[1] != 40 || p[2] != 60) {
                *x0 = x < *x0 ? x : *x0;
                *x1 = x > *x1 ? x : *x1;
            }
        }
    }
    return *x1 >= *x0;
}

/* a full-frame item as the game draws it: a sprite over the frame's width
   (GS 2048 -+ WT / 2), tagged full-screen */
static void recordFullFrame(void)
{
    static const uint8_t grey[4] = {40, 40, 60, 0x80}, white[4] = {255, 255, 255, 0x80};
    dl_Clear();
    setCommon();
    dl_SetDLPriority(0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), s_sceneW, 512, 1);
    rd_clear_target(rd_target(RD_TARGET_SCENE), grey, 1, 0);
    rd_test_gs(RD_TEST_Z_ALWAYS);
    rd_z_write(0);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_abe(0);
    rd_pabe(0);
    rd_fba(0);
    rd_gouraud(0);
    rd_texture_off();
    RdScreenVtx v[2];
    memset(v, 0, sizeof(v));
    v[0].x = (2048 - (int32_t)s_sceneW / 2) * 16;
    v[0].y = (2048 - 256 + 200) * 16;
    v[1].x = (2048 + (int32_t)s_sceneW / 2) * 16;
    v[1].y = (2048 - 256 + 220) * 16;
    for (int i = 0; i < 2; i++) {
        v[i].q = 1.0f;
        memcpy(v[i].rgba, white, 4);
    }
    rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_FULLFRAME, 1, 0);
    dl_Swap();
}

/* at 16:9 the scene is the native frame, 688 GS pixels across: the
   unflagged draw is the 4:3 one at its GS X (nothing compressed), the
   flagged one the same, and a full-frame item covers the frame's width.
   The same triangles 88 pixels to the right are not interpolated to the
   same bytes: the rasteriser evaluates colour and STQ at the pixel's
   position in the target (llvmpipe: a plane through the target's origin),
   and the 4:3 render itself drawn with its origin moved 88 pixels has 7
   pixels 1 LSB off (measured 2026-10-10).  So the centre's coverage is
   exact and its colour within 1 LSB per channel. */
static void stretchWidePixels(void)
{
    CHECK(g_rd.gsW == WT && s_sceneW == WT, "stretch 16:9: the frame is %u wide", g_rd.gsW);
    int a0 = 0, a1 = 0, b0 = 0, b1 = 0;
    s_fullFrameModel = NULL;
    recordPrelit(&s_objA, 1);
    const int a = wideColumns(&a0, &a1);
    memcpy(s_wideA, s_wideImg, sizeof(s_wideA));
    int same = a && s_have43, outside = 0, coverage = 0, lsb = 0, maxd = 0;
    for (int y = 0; a && y < 512; y++) {
        for (int x = 0; x < WT; x++) {
            const uint8_t *p = &s_wideA[(y * WT + x) * 4];
            if (x >= WX0 && x < WX0 + 512) {
                const uint8_t *q = &s_stretch43[(y * 512 + x - WX0) * 4];
                const int clearP = p[0] == 40 && p[1] == 40 && p[2] == 60;
                const int clearQ = q[0] == 40 && q[1] == 40 && q[2] == 60;
                coverage += clearP != clearQ;
                int d = 0;
                for (int k = 0; k < 4; k++) {
                    const int e = p[k] > q[k] ? p[k] - q[k] : q[k] - p[k];
                    d = e > d ? e : d;
                }
                lsb += d != 0;
                maxd = d > maxd ? d : maxd;
            } else {
                outside += p[0] != 40 || p[1] != 40 || p[2] != 60;
            }
        }
    }
    same &= coverage == 0 && maxd <= 1;
    printf("  stretch 16:9: the scene draw's columns %d..%d of %d; against the 4:3 picture %d "
           "pixels covered differently, %d off by up to %d\n",
           a0, a1, WT, coverage, lsb, maxd);
    CHECK(a && same && outside == 0,
          "stretch 16:9: a world draw is the Enhanced 4:3 picture at its GS X, columns %d..%d (%d "
          "covered differently, %d pixels off by up to %d, %d drawn beside)",
          WX0, WX0 + 511, coverage, lsb, maxd, outside);
    s_fullFrameModel = "test_prelit";
    gsFrame.w = WT; /* GsBase.c's latch: reg_DispObj widens by the frame's width / 512 */
    recordPrelit(&s_objA, 1);
    const int b = wideColumns(&b0, &b1);
    gsFrame.w = ICO_FRAME_BASE_W;
    s_fullFrameModel = NULL;
    CHECK(a && b && b1 - b0 > a1 - a0,
          "stretch 16:9: the full-frame model is wider than the unflagged draw (columns %d..%d, "
          "%d..%d)",
          a0, a1, b0, b1);
    recordFullFrame();
    const int w = wideColumns(&b0, &b1);
    CHECK(w && b0 == 0 && b1 == WT - 1,
          "stretch 16:9: a full-frame item covers both edge columns (columns %d..%d of %d)", b0, b1,
          WT);
}

/* ---------------------------------------------------- the content table */
/* rd_content_hide.c's verb (rd_mesh.c markContentHidden) on the top of the
 * listed closing box (st08a_p4, four black corners of entry 0, HIDE), a
 * mesh of its own as the stage has it.
 *   recording: the table's entry 0 is the box, HIDE; its two triangles
 *     carry ICO_VU_MARK_HIDE in the mesh's mark bytes (the index list
 *     untouched); the same quad grey, or black but moved half a unit, is
 *     not marked; the interpolation's copy of the marks takes the mark with
 *     it.  Overridden (rd__content_hide_set) to "off" the triangles
 *     are unmarked; a bad spec (the retired "hide43" among them) changes
 *     nothing
 *   16:9 on the native frame (688 wide) and 4:3: the HIDE box is drawn
 *     nowhere, inside the camera's picture (GS X 2000..2068), beside it
 *     (2310..2378, past the picture's 2304 and inside the frame's 2392) or
 *     across its edge; the unmarked box, the grey and the moved quads are
 *     drawn there (one draw: no split) */

static const uint32_t kBoxTop[4][3] = {{0xc518a521u, 0xc5696b2bu, 0xc431a218u},
                                       {0xc4c78446u, 0xc5696b2bu, 0xc42abb35u},
                                       {0xc518a521u, 0xc5696b2bu, 0xc4a092d2u},
                                       {0xc4c78446u, 0xc5696b2bu, 0xc45c93adu}};

static const char kClosingKey;

/* the box's top as a prelit mesh of one strip, its colour rgb, x moved by
   shift; unmark: the marks taken off (the mesh as it was before them) */
static RdMesh closingMesh(const uint8_t rgb[3], float shift, int unmark)
{
    float qw[1 + 4 * 3][4];
    memset(qw, 0, sizeof(qw));
    const uint32_t tag = 0x8000u | 4u; /* NLOOP 4, EOP */
    memcpy(&qw[0][0], &tag, 4);
    for (int k = 0; k < 4; k++) {
        memcpy(qw[1 + k * 3], kBoxTop[k], 12);
        qw[1 + k * 3][0] += shift;
        qw[1 + k * 3][3] = 1.0f;
        qw[2 + k * 3][0] = (float)(k & 1);
        qw[2 + k * 3][2] = 1.0f;
        qw[2 + k * 3][3] = k == 0 ? 0.0f : 1.0f; /* the strip starts at vertex 0 */
        for (int i = 0; i < 4; i++) {
            qw[3 + k * 3][i] = i < 3 ? (float)rgb[i] : 127.0f;
        }
    }
    const RdVuBatchDesc bd = {0, 0, 0};
    RdVuMeshDesc md;
    memset(&md, 0, sizeof(md));
    md.qw = (const float (*)[4])qw;
    md.qwCount = 1 + 4 * 3;
    md.qwPerVertex = RD_VU_QW_PRELIT;
    md.batchCount = 1;
    md.batches = &bd;
    md.materialCount = 1;
    md.debugName = "closing";
    const RdMesh m = rd_create_vu_mesh(&md);
    RdMeshRec *r = rd__mesh_rec(m.id);
    CHECK(r != NULL, "content table: rd_create_vu_mesh");
    if (r && unmark) {
        free(r->kickMark);
        r->kickMark = NULL;
        r->markCount = 0;
    }
    return m;
}

static const uint8_t kClosingBlack[3] = {0, 0, 0}, kClosingGrey[3] = {90, 90, 90};

/* every triangle of a carries mark, and its index values are dense */
static int closingMarked(const RdMeshRec *a, uint32_t mark)
{
    int ok = a->indexCount == 6 && a->kickMark != NULL;
    for (uint32_t i = 0; ok && i < a->indexCount; i++) {
        ok = (rd__mesh_kick_mark(a, a->index[i] / 4u) & mark) == mark &&
             a->index[i] < 4u * a->vertexCount;
    }
    return ok;
}

static void closingPlaneChecks(void)
{
    CHECK(rd__content_hide_count >= 1 && rd__content_hide_count <= RD_CONTENT_HIDE_MAX &&
              rd__content_hide[0].verb == RD_HIDE &&
              strncmp(rd__content_hide[0].model, "st08a_p4", 8) == 0,
          "content table: entry 0 is st08a_p4's box, HIDE (%d entries)", rd__content_hide_count);
    CHECK(rd__content_hide_set(NULL) && rd__content_hide_verb(0) == RD_HIDE,
          "content table: the table's verb applies");
    const RdMesh black = closingMesh(kClosingBlack, 0.0f, 0);
    const RdMesh grey = closingMesh(kClosingGrey, 0.0f, 0);
    const RdMesh moved = closingMesh(kClosingBlack, 0.5f, 0);
    RdMeshRec *a = rd__mesh_rec(black.id), *g = rd__mesh_rec(grey.id);
    RdMeshRec *mv = rd__mesh_rec(moved.id);
    if (a && g && mv) {
        CHECK(closingMarked(a, ICO_VU_MARK_HIDE),
              "content table: the black box top carries HIDE (%u indices)", a->indexCount);
        CHECK(!g->kickMark, "content table: the grey quad is not marked");
        CHECK(!mv->kickMark, "content table: the moved black quad is not marked");
        CHECK(rd__vu_mesh_copy_marks(g, a) && g->kickMark && g->markCount == a->markCount &&
                  memcmp(g->kickMark, a->kickMark, a->markCount) == 0,
              "content table: a copy of the marks takes the mark");
        CHECK(rd__vu_mesh_copy_marks(g, mv) && !g->kickMark,
              "content table: a copy of an unmarked mesh drops it");
    }
    rd_destroy_vu_mesh(black);
    rd_destroy_vu_mesh(grey);
    rd_destroy_vu_mesh(moved);

    /* the verb overridden: off leaves the box unmarked */
    CHECK(rd__content_hide_set("off") && rd__content_hide_verb(0) == RD_HIDE_OFF,
          "content table: \"off\" overrides every entry");
    const RdMesh off = closingMesh(kClosingBlack, 0.0f, 0);
    const RdMeshRec *o = rd__mesh_rec(off.id);
    CHECK(o && !o->kickMark, "content table: off leaves the box unmarked");
    rd_destroy_vu_mesh(off);
    CHECK(!rd__content_hide_set("hide43") && !rd__content_hide_set("0=hide43") &&
              !rd__content_hide_set("999=hide") && !rd__content_hide_set("0=") &&
              rd__content_hide_verb(0) == RD_HIDE_OFF,
          "content table: a bad spec changes nothing");
    CHECK(rd__content_hide_set("off,0=hide") && rd__content_hide_verb(0) == RD_HIDE,
          "content table: the tokens apply in order");
    rd__content_hide_set(NULL);
}

/* ------------------------------------------ the content table's sheets */
/* rd_content_extend.c's TILES (rd_mesh.c extendSheet, rd__sheet_tiles) on
 * the deja veil's upper left quadrant (mesh "sandstorm" of stage 40, as the
 * dump v2227 holds it: four 128 x 112 tiles over x -256..0, y -224..0),
 * made as the stage makes it.  Recording only:
 *   the table: four entries, every one sandstorm's, TILES, 3 across, 1 down;
 *   the mesh made gets 11 tiles appended (3 beside each of the two tiles of
 *     the outer column, then 1 above each of the 5 tiles of the top row),
 *     44 vertices and 22 triangles in its one batch, its own 16 vertices
 *     and 24 indices first and unchanged, the sheet then x -640..0,
 *     y -336..0; every appended vertex's U and V differ from those of any
 *     other tile's vertex at its position by a whole number (the texture
 *     runs on without a seam under REPEAT) and lie on the affine U(x),
 *     V(y) of the tile it repeats;
 *   rd_update_vu_mesh with the packet's own vertices lays the same tiles;
 *   the same sheet one bit off (a colour 254), the closing box's top (not
 *     a sheet of tiles) and the sheet with the table overridden "off" get
 *     none; a bad spec changes nothing */

static const float kVeilQuadrant[16][4] = {
    /* x, y, s, t; the strip starts at vertex 0 of each four (3.53157902f is
       the float 0x40620564) */
    {-256.0f, -224.0f, -3.53157902f, -2.5f},
    {-256.0f, -112.0f, -3.53157902f, 1.5f},
    {-128.0f, -224.0f, 3.5f, -2.5f},
    {-128.0f, -112.0f, 3.5f, 1.5f},
    {0.0f, -224.0f, 3.53157902f, -2.5f},
    {-128.0f, -224.0f, -3.5f, -2.5f},
    {0.0f, -112.0f, 3.53157902f, 1.5f},
    {-128.0f, -112.0f, -3.5f, 1.5f},
    {0.0f, 0.0f, 3.53157902f, 4.5f},
    {0.0f, -112.0f, 3.53157902f, 0.5f},
    {-128.0f, 0.0f, -3.5f, 4.5f},
    {-128.0f, -112.0f, -3.5f, 0.5f},
    {-256.0f, 0.0f, -3.53157902f, 4.5f},
    {-128.0f, 0.0f, 3.5f, 4.5f},
    {-256.0f, -112.0f, -3.53157902f, 0.5f},
    {-128.0f, -112.0f, 3.5f, 0.5f},
};

/* a quadrant's packet (GIF tag, then 16 prelit vertices: position z 0
   w 1, ST with the strip flag, colour 255 255 255 127; red: the colour's
   red) */
static void veilPacketOf(float qw[1 + 16 * 3][4], const float (*quad)[4], float red)
{
    memset(qw, 0, sizeof(float) * 4 * (1 + 16 * 3));
    const uint32_t tag = 0x8000u | 16u; /* NLOOP 16, EOP */
    memcpy(&qw[0][0], &tag, 4);
    for (int v = 0; v < 16; v++) {
        float *p = qw[1 + v * 3], *st = qw[2 + v * 3], *c = qw[3 + v * 3];
        p[0] = quad[v][0];
        p[1] = quad[v][1];
        p[3] = 1.0f;
        st[0] = quad[v][2];
        st[1] = quad[v][3];
        st[2] = 1.0f;
        st[3] = v % 4 == 0 ? 0.0f : 1.0f;
        c[0] = red;
        c[1] = c[2] = 255.0f;
        c[3] = 127.0f;
    }
}

/* the upper left quadrant's packet */
static void veilPacket(float qw[1 + 16 * 3][4], float red)
{
    veilPacketOf(qw, kVeilQuadrant, red);
}

static RdMesh veilMesh(float red)
{
    static float qw[1 + 16 * 3][4];
    veilPacket(qw, red);
    const RdVuBatchDesc bd = {0, 0, 0};
    RdVuMeshDesc md;
    memset(&md, 0, sizeof(md));
    md.qw = (const float (*)[4])qw;
    md.qwCount = 1 + 16 * 3;
    md.qwPerVertex = RD_VU_QW_PRELIT;
    md.batchCount = 1;
    md.batches = &bd;
    md.materialCount = 1;
    md.debugName = "sandstorm";
    return rd_create_vu_mesh(&md);
}

/* whether a - b is a whole number (to 1/10000 of a repeat) */
static int veilWhole(float a, float b)
{
    const double d = (double)a - (double)b;
    return fabs(d - floor(d + 0.5)) < 1e-4;
}

static void sheetTableChecks(void)
{
    int sheets = 0;
    for (int e = 0; e < rd__content_extend_count; e++) {
        const RdContentExtend *x = &rd__content_extend[e];
        sheets += x->verb == RD_EXTEND_TILES && strncmp(x->model, "sandstorm", 9) == 0 &&
                  x->vertexCount == 16 && x->qwPerVertex == RD_VU_QW_PRELIT && x->cols == 3 &&
                  x->rows == 1;
    }
    CHECK(rd__content_extend_count == 4 && sheets == 4,
          "sheets: four entries, sandstorm's quadrants, TILES 3 across 1 down (%d of %d)", sheets,
          rd__content_extend_count);
    CHECK(rd__content_extend_set(NULL) && rd__content_extend_verb(3) == RD_EXTEND_TILES,
          "sheets: the table's verb applies");

    static float qw[1 + 16 * 3][4];
    veilPacket(qw, 255.0f);
    const RdMesh veil = veilMesh(255.0f);
    RdMeshRec *m = rd__mesh_rec(veil.id);
    CHECK(m != NULL, "sheets: rd_create_vu_mesh");
    if (m) {
        CHECK(m->vertexCount == 60 && m->indexCount == 24 + 66 && m->tiledFrom == 16 &&
                  m->batchCount == 1 && m->batches[0].vertexCount == 60 &&
                  m->batches[0].indexCount == 90,
              "sheets: 11 tiles appended in the one batch (%u vertices, %u indices, from %u)",
              m->vertexCount, m->indexCount, m->tiledFrom);
        int own = 1;
        for (uint32_t v = 0; v < 16 && m->vertexCount >= 16; v++) {
            own &= memcmp(m->stream[v * 3], qw[1 + v * 3], 48) == 0;
        }
        static const uint32_t kOwnIndex[8] = {2, 3, 6, 7, 10, 11, 14, 15}; /* the kicks */
        for (uint32_t t = 0; t < 8; t++) {
            for (uint32_t c = 0; c < 3; c++) {
                own &= m->index[t * 3 + c] == ICO_VU_INDEX(kOwnIndex[t], c);
            }
        }
        CHECK(own, "sheets: the quadrant's own vertices and indices first, unchanged");
        float x0 = 0, x1 = 0, y0 = 0, y1 = 0;
        int seam = 1, affine = 1, kicks = 1;
        for (uint32_t v = 0; v < m->vertexCount; v++) {
            const float *p = m->stream[v * 3], *st = m->stream[v * 3 + 1];
            x0 = v == 0 || p[0] < x0 ? p[0] : x0;
            x1 = v == 0 || p[0] > x1 ? p[0] : x1;
            y0 = v == 0 || p[1] < y0 ? p[1] : y0;
            y1 = v == 0 || p[1] > y1 ? p[1] : y1;
            if (v < 16) {
                continue;
            }
            for (uint32_t w = 0; w < m->vertexCount; w++) {
                const float *q = m->stream[w * 3], *sq = m->stream[w * 3 + 1];
                if (w / 4 != v / 4 && q[0] == p[0] && q[1] == p[1]) {
                    seam &= veilWhole(st[0], sq[0]) && veilWhole(st[1], sq[1]);
                }
            }
            /* the tile it repeats: the column (outer left: x below -128) and
               row (top: y below -112) of the quadrant whose mapping it
               continues; U(x) of the outer column runs -3.53 at -256 to 3.5
               at -128, of the inner one -3.5 at -128 to 3.53 at 0; V(y) of
               the top row -2.5 at -224 to 1.5 at -112 */
            const uint32_t tile = (v - 16) / 4, first = 16 + tile * 4;
            float cx = 0, cy = 0;
            for (uint32_t k = 0; k < 4; k++) {
                cx += m->stream[(first + k) * 3][0] / 4.0f;
                cy += m->stream[(first + k) * 3][1] / 4.0f;
            }
            const double ua = cx < -128.0f ? -3.531580 : -3.5, ub = cx < -128.0f ? -256.0 : -128.0;
            const double u = ua + (p[0] - ub) * ((cx < -128.0f ? 3.5 : 3.531580) - ua) / 128.0;
            const double vv = cy < -112.0f ? -2.5 + (p[1] + 224.0) * 4.0 / 112.0
                                           : 0.5 + (p[1] + 112.0) * 4.0 / 112.0;
            affine &= fabs(st[0] - u) < 2e-4 && fabs(st[1] - vv) < 2e-4;
        }
        for (uint32_t i = 24; i < m->indexCount; i++) {
            const uint32_t kick = m->index[i] / 4;
            kicks &= kick >= 18 && kick < m->vertexCount && kick % 4 >= 2 && m->index[i] % 4 < 3;
        }
        CHECK(x0 == -640.0f && x1 == 0.0f && y0 == -336.0f && y1 == 0.0f,
              "sheets: the quadrant reaches x %g..%g, y %g..%g (-640..0, -336..0)", x0, x1, y0, y1);
        CHECK(seam, "sheets: the tiles meet with whole repeats between their UVs (no seam)");
        CHECK(affine, "sheets: each appended tile continues the UV of the tile it repeats");
        CHECK(kicks, "sheets: the appended triangles are kicks of their own 4-vertex strips");

        /* a morph's rewrite with the packet's own vertices: the same tiles */
        static float before[60 * 3][4];
        const uint32_t nv = m->vertexCount < 60 ? m->vertexCount : 60;
        memcpy(before, m->stream, (size_t)nv * 48);
        CHECK(rd_update_vu_mesh(veil, (const float (*)[4])qw) &&
                  memcmp(before, m->stream, (size_t)nv * 48) == 0,
              "sheets: rd_update_vu_mesh lays the same tiles");
    }
    rd_destroy_vu_mesh(veil);

    const RdMesh other = veilMesh(254.0f);
    const RdMeshRec *o = rd__mesh_rec(other.id);
    CHECK(o && o->vertexCount == 16 && o->tiledFrom == 0,
          "sheets: the same sheet one colour off gets no tiles");
    rd_destroy_vu_mesh(other);
    const RdMesh box = closingMesh(kClosingGrey, 0.0f, 0);
    const RdMeshRec *b = rd__mesh_rec(box.id);
    CHECK(b && b->vertexCount == 4 && b->tiledFrom == 0 &&
              rd__sheet_tiles((const float (*)[4])b->stream, 4, RD_VU_QW_PRELIT, 3, 1, NULL, 0) ==
                  0,
          "sheets: the closing box's top is not a sheet of tiles");
    rd_destroy_vu_mesh(box);

    CHECK(rd__content_extend_set("off") && rd__content_extend_verb(3) == RD_EXTEND_OFF,
          "sheets: \"off\" overrides every entry");
    const RdMesh off = veilMesh(255.0f);
    const RdMeshRec *f = rd__mesh_rec(off.id);
    CHECK(f && f->vertexCount == 16 && f->tiledFrom == 0, "sheets: off appends no tiles");
    rd_destroy_vu_mesh(off);
    CHECK(!rd__content_extend_set("hide") && !rd__content_extend_set("9=tiles") &&
              !rd__content_extend_set("3=") && rd__content_extend_verb(3) == RD_EXTEND_OFF,
          "sheets: a bad spec changes nothing");
    CHECK(rd__content_extend_set("off,3=tiles") && rd__content_extend_verb(3) == RD_EXTEND_TILES &&
              rd__content_extend_verb(0) == RD_EXTEND_OFF,
          "sheets: the tokens apply in order");
    rd__content_extend_set(NULL);
}

static void sheetDraw(RdMesh m, float kx, float ky);

/* the mesh of the first static mesh draw of frame f, NULL without one */
static const RdMeshRec *sheetFrameMesh(const RdFrame *f)
{
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        for (uint32_t i = 0; i < f->lists[l].count; i++) {
            if (f->lists[l].cmds[i].type == RDC_MESH) {
                return rd__mesh_rec(f->lists[l].cmds[i].u[0]);
            }
        }
    }
    return NULL;
}

/* A dump of a frame drawing the tiled quadrant (rd_dump.c rd__dump_frame)
 * holds the game's own sheet: loaded with the table off, the mesh is the
 * packet's 16 vertices, 24 indices and one batch of them, no tile; loaded
 * with the table, it gets its 44 vertices of tiles again (60 in all), the
 * stream the one the frame drew */
static void sheetDumpChecks(void)
{
    CHECK(rd__content_extend_set(NULL), "sheet dump: the table");
    static float qw[1 + 16 * 3][4];
    veilPacket(qw, 255.0f);
    const RdMesh veil = veilMesh(255.0f);
    const RdMeshRec *m = rd__mesh_rec(veil.id);
    CHECK(m && m->vertexCount == 60 && m->tiledFrom == 16 && m->tiledIndexFrom == 24,
          "sheet dump: the quadrant tiled before the dump");
    sheetDraw(veil, 1.171875f, 1.25f);
    const char *path = "rd_mesh_sheet.rddump";
    CHECK(rd__dump_frame(rd__last_frame(), path), "sheet dump: written");
    for (int pass = 0; pass < 2; pass++) {
        const char *spec = pass == 0 ? "off" : NULL;
        CHECK(rd__content_extend_set(spec), "sheet dump: the table %s", spec ? spec : "as listed");
        RdFrame g;
        if (!rd__load_frame(path, &g)) {
            CHECK(0, "sheet dump: loaded");
            break;
        }
        const RdMeshRec *r = sheetFrameMesh(&g);
        if (pass == 0) {
            int own = r != NULL && r->vertexCount == 16;
            for (uint32_t v = 0; own && v < 16; v++) {
                own &= memcmp(r->stream[v * 3], qw[1 + v * 3], 48) == 0;
            }
            CHECK(own && r->tiledFrom == 0 && r->indexCount == 24 && r->batchCount == 1 &&
                      r->batches[0].vertexCount == 16 && r->batches[0].indexCount == 24,
                  "sheet dump: off, the game's 16 vertices and 24 indices, no tile (%u vertices)",
                  r ? r->vertexCount : 0u);
        } else {
            CHECK(r && m && r->vertexCount == 60 && r->tiledFrom == 16 && r->indexCount == 90 &&
                      r->batches[0].vertexCount == 60 &&
                      memcmp(r->stream, m->stream, (size_t)60 * 48) == 0,
                  "sheet dump: the table, the 44 vertices of tiles laid again (%u in all)",
                  r ? r->vertexCount : 0u);
        }
        rd__frame_free(&g);
    }
    rd__content_extend_set(NULL);
    rd_destroy_vu_mesh(veil);
    (void)remove(path);
}

/* one frame: SCENE cleared grey, mesh m by code 32 at x = 0.08 x + tx,
   y = 0.08 z + 2103 GS window coordinates (GS Z 16000) */
static void closingDraw(RdMesh m, float tx)
{
    static const uint8_t grey[4] = {40, 40, 60, 0x80};
    dl_Clear();
    dl_SetDLPriority(0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), s_sceneW, 512, 1);
    rd_clear_target(rd_target(RD_TARGET_SCENE), grey, 1, 0);
    rd_test_gs(RD_TEST_Z_GEQUAL);
    rd_z_write(1);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_abe(0);
    rd_pabe(0);
    rd_fba(0);
    rd_gouraud(1);
    rd_texture_off();
    RdVuDraw d;
    memset(&d, 0, sizeof(d));
    d.prog = RD_PROG_PRELIT;
    d.code = 32;
    d.clip = RD_VU_CLIP_REGION;
    d.vu.mem[16][0] = 0.08f;
    d.vu.mem[18][1] = 0.08f;
    d.vu.mem[19][0] = tx;
    d.vu.mem[19][1] = 2103.0f;
    d.vu.mem[19][2] = 1000.0f;
    d.vu.mem[19][3] = 1.0f;
    rd_draw_vu_mesh(m, &d, RD_KEY(&kClosingKey, 0, 32));
    dl_Swap();
}

static uint8_t s_closingA[WT * 512 * 4];

static int closingRead(uint8_t *img, uint32_t *w)
{
    uint32_t h = 0;
    return rd__read_target(rd_target(RD_TARGET_SCENE), img, WT * 512 * 4, w, &h) && h == 512 &&
           *w <= WT;
}

/* the texels drawn (not the grey clear) in columns x0..x1 - 1 */
static int closingCount(const uint8_t *img, uint32_t w, int x0, int x1)
{
    int n = 0;
    for (int y = 0; y < 512; y++) {
        for (int x = x0 < 0 ? 0 : x0; x < x1 && x < (int)w; x++) {
            const uint8_t *p = &img[((size_t)y * w + (size_t)x) * 4];
            n += p[0] != 40 || p[1] != 40 || p[2] != 60;
        }
    }
    return n;
}

#define CLOSING_IN 2196.0f     /* GS X 2000..2068: inside the camera's picture */
#define CLOSING_BESIDE 2506.0f /* 2310..2378: beside it at 16:9 */
#define CLOSING_ACROSS 2466.0f /* 2270..2338: across its right edge (2304) */

/* the texels drawn by mesh m at tx, the whole target (-1: no read) */
static int closingDrawn(RdMesh m, float tx)
{
    uint32_t w = 0;
    closingDraw(m, tx);
    return closingRead(s_closingA, &w) ? closingCount(s_closingA, w, 0, (int)w) : -1;
}

static void closingPixels(int wide)
{
    const char *at = wide ? "16:9" : "4:3";
    const RdMesh black = closingMesh(kClosingBlack, 0.0f, 0);
    const RdMesh plain = closingMesh(kClosingBlack, 0.0f, 1);
    const RdMesh grey = closingMesh(kClosingGrey, 0.0f, 0);
    const RdMesh moved = closingMesh(kClosingBlack, 0.5f, 0);
    const RdTargetRec *tc = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    if (!tc) {
        CHECK(0, "content table %s: SCENE has no record", at);
        return;
    }
    if (wide) {
        CHECK(s_sceneW == WT && tc->w == WT && rd__region_scale(tc) == 512.0f / (float)WT,
              "content table 16:9: SCENE is the native frame (%u wide, region scale %g)", tc->w,
              (double)rd__region_scale(tc));
    }
    /* inside the picture, across its right edge, and (16:9) beside it */
    const float txs[3] = {CLOSING_IN, CLOSING_ACROSS, CLOSING_BESIDE};
    static const char *const where[3] = {"inside the picture", "across its edge", "beside it"};
    for (int i = 0; i < (wide ? 3 : 2); i++) {
        const int h = closingDrawn(black, txs[i]), p = closingDrawn(plain, txs[i]);
        const int g = closingDrawn(grey, txs[i]), m = closingDrawn(moved, txs[i]);
        printf("  content table %s %s: HIDE box %d texels, unmarked %d, grey %d, moved %d\n", at,
               where[i], h, p, g, m);
        CHECK(h == 0, "content table %s: HIDE is drawn nowhere (%s, %d texels)", at, where[i], h);
        CHECK(p >= 500 && g >= 500 && m >= 500,
              "content table %s: the unmarked box, the grey and the moved quads are drawn %s (%d, "
              "%d, %d)",
              at, where[i], p, g, m);
    }
    rd_destroy_vu_mesh(black);
    rd_destroy_vu_mesh(plain);
    rd_destroy_vu_mesh(grey);
    rd_destroy_vu_mesh(moved);
}

/* The deja veil's quadrant (sheetTableChecks) on the device, untextured, at
 * kx GS pixels a unit across and ky down from GS (2048, 2048).
 * The stage's placement, 1.171875 and 1.25: its tiles 150 x 140 pixels,
 * the sheet over GS X 1748..2048 and Y 1768..2048, past the 4:3 frame's
 * left edge (1792) by 44 pixels and short of the 16:9 one's (1704) by as
 * much, and past the frame's top (1792), as the veil reaches past the PS2's
 * picture on every side.
 *   4:3 (Original 1x): the picture with the tiles is the one without them,
 *     byte for byte;
 *   16:9 (688 wide, Enhanced): without the tiles the 44 columns left of the
 *     sheet are the clear's grey; with them they are drawn in every row the
 *     sheet's own columns are, and the sheet's own columns are the same.
 * A shrunk placement, 0.25 and 0.25: the tiles 32 x 28 pixels, the sheet
 * over GS X 1984..2048 and Y 1992..2048, its appended tiles over X
 * 1888..1984 and up to Y 1964, inside the 4:3 picture:
 *   4:3 (Original 1x): out of the box a sheet draws its own triangles only
 *     (rd__vu_sheet_end), so the picture with the tiles is still the one
 *     without them, byte for byte, whatever the placement;
 *   4:3 Enhanced and 16:9 Enhanced: the 96 columns left of the sheet,
 *     inside the 4:3 picture, are the clear's grey without the tiles and
 *     drawn with them in the sheet's rows; the sheet's own texels are the
 *     same, and its columns gain the row of tiles above it (64 x 28) */
static const char kSheetKey;

static void sheetDraw(RdMesh m, float kx, float ky)
{
    static const uint8_t grey[4] = {40, 40, 60, 0x80};
    dl_Clear();
    dl_SetDLPriority(0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), s_sceneW, 512, 1);
    rd_clear_target(rd_target(RD_TARGET_SCENE), grey, 1, 0);
    rd_test_gs(RD_TEST_Z_GEQUAL);
    rd_z_write(1);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_abe(0);
    rd_pabe(0);
    rd_fba(0);
    rd_gouraud(1);
    rd_texture_off();
    RdVuDraw d;
    memset(&d, 0, sizeof(d));
    d.prog = RD_PROG_PRELIT;
    d.code = 32;
    d.clip = RD_VU_CLIP_REGION;
    d.vu.mem[16][0] = kx;
    d.vu.mem[17][1] = ky;
    d.vu.mem[19][0] = 2048.0f;
    d.vu.mem[19][1] = 2048.0f;
    d.vu.mem[19][2] = 1000.0f;
    d.vu.mem[19][3] = 1.0f;
    rd_draw_vu_mesh(m, &d, RD_KEY(&kSheetKey, 0, 32));
    dl_Swap();
}

static uint8_t s_sheetB[WT * 512 * 4];

/* the plain sheet and the tiled one drawn at kx, ky into s_closingA and
   s_sheetB; the target's width, 0 on no read */
static uint32_t sheetPair(const char *at, float kx, float ky)
{
    CHECK(rd__content_extend_set("off"), "sheets %s: off", at);
    const RdMesh plain = veilMesh(255.0f);
    CHECK(rd__content_extend_set(NULL), "sheets %s: the table", at);
    const RdMesh tiled = veilMesh(255.0f);
    const RdMeshRec *pr = rd__mesh_rec(plain.id), *tr = rd__mesh_rec(tiled.id);
    CHECK(pr && tr && pr->vertexCount == 16 && tr->vertexCount == 60,
          "sheets %s: the plain sheet and the tiled one", at);
    uint32_t w = 0, w2 = 0;
    sheetDraw(plain, kx, ky);
    const int okA = closingRead(s_closingA, &w);
    sheetDraw(tiled, kx, ky);
    const int okB = closingRead(s_sheetB, &w2);
    rd_destroy_vu_mesh(plain);
    rd_destroy_vu_mesh(tiled);
    CHECK(okA && okB && w == w2, "sheets %s: SCENE read (%u, %u)", at, w, w2);
    return okA && okB && w == w2 ? w : 0u;
}

/* whether columns x0.. of the two pictures are the same in every row */
static int sheetSameFrom(uint32_t w, int x0)
{
    int same = 1;
    for (int y = 0; y < 512; y++) {
        same &= memcmp(&s_closingA[((size_t)y * w + (size_t)x0) * 4],
                       &s_sheetB[((size_t)y * w + (size_t)x0) * 4],
                       (size_t)(w - (uint32_t)x0) * 4) == 0;
    }
    return same;
}

static void sheetPixels(int wide)
{
    const char *at = wide ? "16:9" : "4:3";
    const uint32_t w = sheetPair(at, 1.171875f, 1.25f);
    CHECK(w == (wide ? WT : 512u), "sheets %s: the frame's width (%u)", at, w);
    if (w) {
        const int edge = (int)w / 2 - 300; /* the sheet's left edge, GS X 1748 */
        const int ownA = closingCount(s_closingA, w, edge, (int)w / 2);
        const int ownB = closingCount(s_sheetB, w, edge, (int)w / 2);
        const int sideA = closingCount(s_closingA, w, 0, edge);
        const int sideB = closingCount(s_sheetB, w, 0, edge);
        printf("  sheets %s: own columns %d / %d texels, beside them %d / %d (plain / tiled)\n", at,
               ownA, ownB, sideA, sideB);
        if (!wide) {
            CHECK(memcmp(s_closingA, s_sheetB, (size_t)w * 512 * 4) == 0,
                  "sheets 4:3: the picture with the tiles is the one without them, byte for byte");
        } else {
            /* the sheet's rows: those its middle column draws */
            int rowsDrawn = 0;
            for (int y = 0; y < 512; y++) {
                const uint8_t *p = &s_closingA[((size_t)y * w + (size_t)(w / 2 - 10)) * 4];
                rowsDrawn += p[0] != 40 || p[1] != 40 || p[2] != 60;
            }
            CHECK(edge == 44 && sideA == 0 && rowsDrawn > 100 && sideB >= edge * rowsDrawn,
                  "sheets 16:9: the %d columns beside the sheet are drawn with the tiles in its %d "
                  "rows (%d texels, none without)",
                  edge, rowsDrawn, sideB);
            CHECK(sheetSameFrom(w, edge) && ownA == ownB,
                  "sheets 16:9: the sheet's own columns are the same");
        }
    }
    /* the shrunk placement: the tiles inside the 4:3 picture */
    const char *shrunk = wide ? "16:9 shrunk" : "4:3 shrunk";
    const int original = !wide && g_rd.settings.preset == RD_PRESET_ORIGINAL;
    const uint32_t ws = sheetPair(shrunk, 0.25f, 0.25f);
    if (ws) {
        const int edge = (int)ws / 2 - 64; /* the sheet's left edge, GS X 1984 */
        const int pic = (int)ws / 2 - 256; /* the 4:3 picture's left edge, GS X 1792 */
        const int ownA = closingCount(s_closingA, ws, edge, (int)ws / 2);
        const int ownB = closingCount(s_sheetB, ws, edge, (int)ws / 2);
        const int sideA = closingCount(s_closingA, ws, pic, edge);
        const int sideB = closingCount(s_sheetB, ws, pic, edge);
        printf("  sheets %s (%s): own columns %d / %d texels, the 4:3 picture's columns beside "
               "them %d / %d (plain / tiled)\n",
               shrunk, original ? "Original" : "Enhanced", ownA, ownB, sideA, sideB);
        if (original) {
            CHECK(memcmp(s_closingA, s_sheetB, (size_t)ws * 512 * 4) == 0,
                  "sheets 4:3 shrunk: out of the box the picture with the tiles is the one "
                  "without them, byte for byte, though the tiles lie inside it");
        } else {
            /* the sheet's 56 rows: the 3 tiles beside each of its two rows of
               tiles, 96 x 56 texels, in the 4:3 picture */
            CHECK(sideA == 0 && sideB >= 96 * 50 && ownA > 0,
                  "sheets %s Enhanced: the tiles are drawn inside the 4:3 picture (%d texels "
                  "beside the sheet, %d without)",
                  shrunk, sideB, sideA);
            /* the sheet's own texels the same; above it, in its columns,
               the row of tiles over its 64 x 28 texels */
            int kept = 1;
            for (size_t t = 0; t < (size_t)ws * 512; t++) {
                const uint8_t *a = &s_closingA[t * 4];
                if (a[0] != 40 || a[1] != 40 || a[2] != 60) {
                    kept &= memcmp(a, &s_sheetB[t * 4], 4) == 0;
                }
            }
            CHECK(kept && ownB - ownA == 64 * 28,
                  "sheets %s Enhanced: the sheet's own texels are the same, the tiles above it "
                  "%d texels",
                  shrunk, ownB - ownA);
        }
    }
}

/* ---------------------------------------------------- the photo veil */
/* Photo mode's Deja vu (port/game/photo_view.c ico_photo_view_veil) draws
 * the stage's sandstorm object at deja_lightning.bga's two layers: its four
 * quadrant sheets (the meshes rd_content_extend.c lists), each layer held in
 * the camera's space at one depth.  The dump of stage 40 (v2227) puts the
 * first layer's model x -+256 at GS X 2048 -+ 702.3 and y -+224 at
 * Y 2048 -+ 716.9, the second's at 2048 -+ 690.0 and -+ 704.3.  Here the
 * four sheets, made from their packets as the stage holds them (rd_replay_tool
 * --mesh sandstorm), are drawn untextured at those places on the widest
 * frame, 60:9 (2560 GS pixels across, X 768..3327) and 512 lines:
 *   with the table's tiles every pixel of the frame is drawn, by each layer;
 *   with the table off the columns beside the sheets (GS X below 1345 and
 *     above 2750, 577 of them each side) are the clear's grey */
static const float kVeilLowerLeft[16][4] = {
    {-256.0f, 224.0f, -3.53157902f, 4.5f}, {-128.0f, 224.0f, 3.5f, 4.5f},
    {-256.0f, 112.0f, -3.53157902f, 0.5f}, {-128.0f, 112.0f, 3.5f, 0.5f},
    {-256.0f, 0.0f, -3.53157902f, -2.5f},  {-256.0f, 112.0f, -3.53157902f, 1.5f},
    {-128.0f, 0.0f, 3.5f, -2.5f},          {-128.0f, 112.0f, 3.5f, 1.5f},
    {0.0f, 0.0f, 3.53157902f, -2.5f},      {-128.0f, 0.0f, -3.5f, -2.5f},
    {0.0f, 112.0f, 3.53157902f, 1.5f},     {-128.0f, 112.0f, -3.5f, 1.5f},
    {0.0f, 224.0f, 3.53157902f, 4.5f},     {0.0f, 112.0f, 3.53157902f, 0.5f},
    {-128.0f, 224.0f, -3.5f, 4.5f},        {-128.0f, 112.0f, -3.5f, 0.5f},
};
static const float kVeilLowerRight[16][4] = {
    {256.0f, 224.0f, 3.53157902f, 4.5f}, {256.0f, 112.0f, 3.53157902f, 0.5f},
    {128.0f, 224.0f, -3.5f, 4.5f},       {128.0f, 112.0f, -3.5f, 0.5f},
    {0.0f, 224.0f, -3.53157902f, 4.5f},  {128.0f, 224.0f, 3.5f, 4.5f},
    {0.0f, 112.0f, -3.53157902f, 0.5f},  {128.0f, 112.0f, 3.5f, 0.5f},
    {0.0f, 0.0f, -3.53157902f, -2.5f},   {0.0f, 112.0f, -3.53157902f, 1.5f},
    {128.0f, 0.0f, 3.5f, -2.5f},         {128.0f, 112.0f, 3.5f, 1.5f},
    {256.0f, 0.0f, 3.53157902f, -2.5f},  {128.0f, 0.0f, -3.5f, -2.5f},
    {256.0f, 112.0f, 3.53157902f, 1.5f}, {128.0f, 112.0f, -3.5f, 1.5f},
};
static const float kVeilUpperRight[16][4] = {
    {256.0f, -224.0f, 3.53157902f, -2.5f}, {128.0f, -224.0f, -3.5f, -2.5f},
    {256.0f, -112.0f, 3.53157902f, 1.5f},  {128.0f, -112.0f, -3.5f, 1.5f},
    {256.0f, 0.0f, 3.53157902f, 4.5f},     {256.0f, -112.0f, 3.53157902f, 0.5f},
    {128.0f, 0.0f, -3.5f, 4.5f},           {128.0f, -112.0f, -3.5f, 0.5f},
    {0.0f, 0.0f, -3.53157902f, 4.5f},      {128.0f, 0.0f, 3.5f, 4.5f},
    {0.0f, -112.0f, -3.53157902f, 0.5f},   {128.0f, -112.0f, 3.5f, 0.5f},
    {0.0f, -224.0f, -3.53157902f, -2.5f},  {0.0f, -112.0f, -3.53157902f, 1.5f},
    {128.0f, -224.0f, 3.5f, -2.5f},        {128.0f, -112.0f, 3.5f, 1.5f},
};

#define VEIL_W 2560 /* the 60:9 frame's GS width at 1x */

static const char kVeilKey;

static uint8_t s_veilImg[VEIL_W * 512 * 4];

/* the four quadrants (made with the table as it is set) drawn at a layer's
   GS scale, into SCENE cleared grey; the pixels drawn, -1 on no read */
static int veilLayerDrawn(float kx, float ky, int *sideX0, int *sideX1)
{
    static const uint8_t grey[4] = {40, 40, 60, 0x80};
    const float (*quads[4])[4] = {kVeilQuadrant, kVeilLowerLeft, kVeilLowerRight, kVeilUpperRight};
    RdMesh m[4];
    static float qw[1 + 16 * 3][4];
    int tiled = 0;
    for (int q = 0; q < 4; q++) {
        veilPacketOf(qw, quads[q], 255.0f);
        const RdVuBatchDesc bd = {0, 0, 0};
        RdVuMeshDesc md;
        memset(&md, 0, sizeof(md));
        md.qw = (const float (*)[4])qw;
        md.qwCount = 1 + 16 * 3;
        md.qwPerVertex = RD_VU_QW_PRELIT;
        md.batchCount = 1;
        md.batches = &bd;
        md.materialCount = 1;
        md.debugName = "sandstorm";
        m[q] = rd_create_vu_mesh(&md);
        const RdMeshRec *r = rd__mesh_rec(m[q].id);
        tiled += r && r->tiledFrom == 16;
    }
    dl_Clear();
    dl_SetDLPriority(5);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), VEIL_W, 512, 1);
    rd_clear_target(rd_target(RD_TARGET_SCENE), grey, 1, 0);
    rd_test_gs(RD_TEST_Z_GEQUAL);
    rd_z_write(1);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_abe(0);
    rd_pabe(0);
    rd_fba(0);
    rd_gouraud(1);
    rd_texture_off();
    for (int q = 0; q < 4; q++) {
        RdVuDraw d;
        memset(&d, 0, sizeof(d));
        d.prog = RD_PROG_PRELIT;
        d.code = 32;
        d.clip = RD_VU_CLIP_REGION;
        d.vu.mem[16][0] = kx;
        d.vu.mem[17][1] = ky;
        d.vu.mem[19][0] = 2048.0f;
        d.vu.mem[19][1] = 2048.0f;
        d.vu.mem[19][2] = 1000.0f;
        d.vu.mem[19][3] = 1.0f;
        rd_draw_vu_mesh(m[q], &d, RD_KEY(&kVeilKey, 0, q));
    }
    dl_Swap();
    for (int q = 0; q < 4; q++) {
        rd_destroy_vu_mesh(m[q]);
    }
    uint32_t w = 0, h = 0;
    if (!rd__read_target(rd_target(RD_TARGET_SCENE), s_veilImg, sizeof(s_veilImg), &w, &h) ||
        w != VEIL_W || h != 512) {
        return -1;
    }
    int drawn = 0;
    *sideX0 = (int)w;
    *sideX1 = -1;
    for (int y = 0; y < 512; y++) {
        for (int x = 0; x < (int)w; x++) {
            const uint8_t *p = &s_veilImg[((size_t)y * w + (size_t)x) * 4];
            if (p[0] != 40 || p[1] != 40 || p[2] != 60) {
                drawn++;
            } else {
                *sideX0 = x < *sideX0 ? x : *sideX0;
                *sideX1 = x > *sideX1 ? x : *sideX1;
            }
        }
    }
    return tiled == 4 ? drawn : -2 - tiled;
}

static void photoVeilPixels(void)
{
    /* the two layers' GS scales: the dump's extents over the model's
       256 x 224 half sheet */
    static const float k[2][2] = {{702.3f / 256.0f, 716.9f / 224.0f},
                                  {690.0f / 256.0f, 704.3f / 224.0f}};
    CHECK(g_rd.gsW == VEIL_W, "photo veil: the frame is %u wide", g_rd.gsW);
    for (int layer = 0; layer < 2; layer++) {
        int x0 = 0, x1 = 0;
        CHECK(rd__content_extend_set(NULL), "photo veil: the table");
        const int all = veilLayerDrawn(k[layer][0], k[layer][1], &x0, &x1);
        CHECK(all == VEIL_W * 512,
              "photo veil, layer %d at 60:9: every pixel drawn with the tiles (%d of %d; "
              "grey columns %d..%d)",
              layer + 1, all, VEIL_W * 512, x0, x1);
        CHECK(rd__content_extend_set("off"), "photo veil: off");
        const int bare = veilLayerDrawn(k[layer][0], k[layer][1], &x0, &x1);
        /* untiled the sheets reach GS X 2048 -+ 702 (690): the frame's
           columns 0 .. 576 (588) are grey */
        const int side = 1280 - (int)(256.0f * k[layer][0]) - 2;
        CHECK(bare == -2 && x0 == 0 && x1 == VEIL_W - 1,
              "photo veil, layer %d at 60:9 without the tiles: the sides grey (%d, columns "
              "%d..%d)",
              layer + 1, bare, x0, x1);
        int sideGrey = 1;
        for (int y = 0; y < 512; y++) {
            for (int x = 0; x < side; x++) {
                const uint8_t *a = &s_veilImg[((size_t)y * VEIL_W + (size_t)x) * 4];
                const uint8_t *b = &s_veilImg[((size_t)y * VEIL_W + (size_t)(VEIL_W - 1 - x)) * 4];
                sideGrey &= a[0] == 40 && a[1] == 40 && a[2] == 60 && b[0] == 40 && b[1] == 40 &&
                            b[2] == 60;
            }
        }
        CHECK(sideGrey, "photo veil, layer %d without the tiles: the %d columns each side grey",
              layer + 1, side);
        printf("  photo veil, layer %d at 60:9: %d of %d pixels drawn with the tiles, the %d "
               "columns each side bare without\n",
               layer + 1, all, VEIL_W * 512, side);
    }
    rd__content_extend_set(NULL);
}

/* ----------------------------------------------------- the photo reel */
/* Photo mode's Deja vu film reel (port/game/photo_view.c): the noiseA..E
 * specks, each one quad of its model (noiseB, noiseD: x and y -+0.033;
 * noiseE: x -+0.033, y -+0.453; UV 0..1), held in the camera's space.  The
 * dump of stage 40 (v2227, rd_replay_tool --vu-ref 5:489, 5:502, 5:515,
 * 5:526) puts the four drawn that frame at GS X 2255.3..2271.3 (noiseB),
 * 2008.4..2031.4 (noiseD), 1800.4..1803.9 and 1652.9..1657.2 (noiseE), Y
 * 1769.4..1794.7, 1847.9..1867.0 and 1730.9..2377.2.  Here the four quads,
 * made from packets as the models hold them, are drawn untextured at those
 * places on the 16:9 frame (688 GS pixels across, X 1704..2391):
 *   none of them is tiled (rd_content_extend.c lists the veil's sheets
 *     only): each speck is drawn once at its own size;
 *   every pixel drawn lies in one of the four places (to a pixel), so in
 *     the 4:3 picture's columns 88..599: the speck at 1653 lies left of
 *     the 16:9 frame (as left of the 4:3 picture on the PS2) and draws
 *     nothing, noiseB's rows below 1792 alone are on the frame */
static const struct {
    const char *name;
    float x0, x1, y0, y1; /* the GS place */
    float hy;             /* the model's half height */
} kReelSpeck[4] = {
    {"noiseB", 2255.250f, 2271.312f, 1769.375f, 1794.688f, 0.033f},
    {"noiseD", 2008.375f, 2031.438f, 1847.875f, 1867.000f, 0.033f},
    {"noiseE", 1800.438f, 1803.875f, 1730.938f, 2377.188f, 0.453f},
    {"noiseE", 1652.938f, 1657.188f, 1730.938f, 2377.188f, 0.453f},
};

static const char kReelKey;

static uint8_t s_reelImg[WT * 512 * 4];

static void photoReelPixels(void)
{
    static const uint8_t grey[4] = {40, 40, 60, 0x80};
    static float qw[1 + 4 * 3][4];
    RdMesh m[4];
    int tiled = 0;
    CHECK(g_rd.gsW == WT, "photo reel: the frame is %u wide", g_rd.gsW);
    CHECK(rd__content_extend_set(NULL), "photo reel: the table");
    for (int q = 0; q < 4; q++) {
        /* the model's quad: a strip of four, x -+0.033, y -+hy */
        const float hy = kReelSpeck[q].hy;
        const float quad[4][4] = {
            {-0.033f, -hy, 0.0f, 0.0f},
            {-0.033f, hy, 0.0f, 1.0f},
            {0.033f, -hy, 1.0f, 0.0f},
            {0.033f, hy, 1.0f, 1.0f},
        };
        memset(qw, 0, sizeof(qw));
        const uint32_t tag = 0x8000u | 4u; /* NLOOP 4, EOP */
        memcpy(&qw[0][0], &tag, 4);
        for (int v = 0; v < 4; v++) {
            float *p = qw[1 + v * 3], *st = qw[2 + v * 3], *c = qw[3 + v * 3];
            p[0] = quad[v][0];
            p[1] = quad[v][1];
            p[3] = 1.0f;
            st[0] = quad[v][2];
            st[1] = quad[v][3];
            st[2] = 1.0f;
            st[3] = v == 0 ? 0.0f : 1.0f;
            c[0] = c[1] = c[2] = 255.0f;
            c[3] = 127.0f;
        }
        const RdVuBatchDesc bd = {0, 0, 0};
        RdVuMeshDesc md;
        memset(&md, 0, sizeof(md));
        md.qw = (const float (*)[4])qw;
        md.qwCount = 1 + 4 * 3;
        md.qwPerVertex = RD_VU_QW_PRELIT;
        md.batchCount = 1;
        md.batches = &bd;
        md.materialCount = 1;
        md.debugName = kReelSpeck[q].name;
        m[q] = rd_create_vu_mesh(&md);
        const RdMeshRec *r = rd__mesh_rec(m[q].id);
        tiled += r && r->tiledFrom != 0;
    }
    CHECK(tiled == 0, "photo reel: %d of the specks tiled", tiled);
    dl_Clear();
    dl_SetDLPriority(5);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), WT, 512, 1);
    rd_clear_target(rd_target(RD_TARGET_SCENE), grey, 1, 0);
    rd_test_gs(RD_TEST_Z_GEQUAL);
    rd_z_write(1);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_abe(0);
    rd_pabe(0);
    rd_fba(0);
    rd_gouraud(1);
    rd_texture_off();
    for (int q = 0; q < 4; q++) {
        RdVuDraw d;
        memset(&d, 0, sizeof(d));
        d.prog = RD_PROG_PRELIT;
        d.code = 32;
        d.clip = RD_VU_CLIP_REGION;
        d.vu.mem[16][0] = (kReelSpeck[q].x1 - kReelSpeck[q].x0) / 0.066f;
        d.vu.mem[17][1] = (kReelSpeck[q].y1 - kReelSpeck[q].y0) / (2.0f * kReelSpeck[q].hy);
        d.vu.mem[19][0] = 0.5f * (kReelSpeck[q].x0 + kReelSpeck[q].x1);
        d.vu.mem[19][1] = 0.5f * (kReelSpeck[q].y0 + kReelSpeck[q].y1);
        d.vu.mem[19][2] = 1000.0f;
        d.vu.mem[19][3] = 1.0f;
        rd_draw_vu_mesh(m[q], &d, RD_KEY(&kReelKey, 0, q));
    }
    dl_Swap();
    for (int q = 0; q < 4; q++) {
        rd_destroy_vu_mesh(m[q]);
    }
    uint32_t w = 0, h = 0;
    if (!rd__read_target(rd_target(RD_TARGET_SCENE), s_reelImg, sizeof(s_reelImg), &w, &h) ||
        w != WT || h != 512) {
        CHECK(0, "photo reel: SCENE read back (%ux%u)", w, h);
        return;
    }
    /* the frame's GS origin: X 2048 - 344, Y 2048 - 256 */
    const float ox = 2048.0f - (float)WT / 2.0f, oy = 2048.0f - 256.0f;
    int drawn[4] = {0, 0, 0, 0}, stray = 0, x0 = (int)w, x1 = -1;
    for (int y = 0; y < 512; y++) {
        for (int x = 0; x < (int)w; x++) {
            const uint8_t *p = &s_reelImg[((size_t)y * w + (size_t)x) * 4];
            if (p[0] == 40 && p[1] == 40 && p[2] == 60) {
                continue;
            }
            const float gx = ox + (float)x + 0.5f, gy = oy + (float)y + 0.5f;
            int in = -1;
            for (int q = 0; q < 4 && in < 0; q++) {
                if (gx >= kReelSpeck[q].x0 - 1.0f && gx <= kReelSpeck[q].x1 + 1.0f &&
                    gy >= kReelSpeck[q].y0 - 1.0f && gy <= kReelSpeck[q].y1 + 1.0f) {
                    in = q;
                }
            }
            if (in < 0) {
                stray++;
            } else {
                drawn[in]++;
            }
            x0 = x < x0 ? x : x0;
            x1 = x > x1 ? x : x1;
        }
    }
    CHECK(stray == 0, "photo reel: %d pixels drawn outside the specks' places", stray);
    CHECK(drawn[0] > 0 && drawn[1] > 0 && drawn[2] > 0 && drawn[3] == 0,
          "photo reel: noiseB %d, noiseD %d, noiseE %d and %d pixels (the last off the frame)",
          drawn[0], drawn[1], drawn[2], drawn[3]);
    /* noiseD: its 23 x 19 GS pixels whole, once */
    CHECK(drawn[1] >= 21 * 18 && drawn[1] <= 25 * 21, "photo reel: noiseD once at its size (%d)",
          drawn[1]);
    CHECK(x0 >= 88 && x1 <= 599, "photo reel: drawn in columns %d..%d (the 4:3 picture 88..599)",
          x0, x1);
    printf("  photo reel at 16:9: noiseB %d, noiseD %d, noiseE %d and %d pixels, columns %d..%d\n",
           drawn[0], drawn[1], drawn[2], drawn[3], x0, x1);
}

/* -------------------------------------------------------- strip order */
/* Packet.c's table of the title logo's strips drawn in another order of
 * their own entries (pac_HostStrips, pac_HostStripOrder):
 *   - every row's good order holds the bad order's indices once each, and
 *     pac_HostStripOrder reads them in it only for that model, that count
 *     and exactly that index sequence
 *   - the I's row through p2o_MakePacket: the mesh's vertices come in the
 *     good order (a strip one index off keeps the disc's)
 *   - with the disc image: each row's strip is found in its model (the
 *     st26a/model p2o file, ObjHdr and ObjRec, DisplayP2O.h) by count and
 *     index sequence, in a plane of constant z, and in the disc's order its triangles
 *     overlap; in the good order they wind one way, none overlaps another,
 *     and they cover the outline the disc's zig-zag bounds (even entries
 *     out, odd entries back) exactly: the sum of their areas is the
 *     outline's area.  Without the image that part is skipped. */

#define STRIP_MAX 64

static void stripTableChecks(void)
{
    int count;
    const PacHostStrip *t = pac_HostStrips(&count);
    CHECK(count >= 2, "%d rows in the strip table", count);
    for (int r = 0; r < count; r++) {
        const PacHostStrip *h = &t[r];
        short other[STRIP_MAX];
        CHECK(h->num > 2 && h->num <= STRIP_MAX, "row %d: %d entries", r, h->num);
        for (int i = 0; i < h->num; i++) {
            int inBad = 0, inGood = 0;
            for (int k = 0; k < h->num; k++) {
                inBad += h->bad[k] == h->bad[i];
                inGood += h->good[k] == h->bad[i];
            }
            CHECK(inBad == 1 && inGood == 1, "row %d: index %d once in each order (%d, %d)", r,
                  h->bad[i], inBad, inGood);
            int e = pac_HostStripOrder(h->model, h->bad, 1, h->num, i);
            CHECK(e >= 0 && e < h->num && h->bad[e] == h->good[i],
                  "row %d (%s, %d): vertex %d reads entry %d", r, h->model, h->num, i, e);
        }
        memcpy(other, h->bad, (size_t)h->num * sizeof(short));
        other[h->num / 2]++;
        for (int i = 0; i < h->num; i++) {
            CHECK(pac_HostStripOrder(h->model, other, 1, h->num, i) == i,
                  "row %d: a strip one index off keeps its order", r);
            CHECK(pac_HostStripOrder("Q", h->bad, 1, h->num, i) == i,
                  "row %d: another model keeps its order", r);
        }
    }
}

/* the I's row through the packet path: a model "I" of one strip */
static Model s_modelS;
static Sub15C s_objS;

static void stripPacketCheck(const char *name, const short *index, const short *expect, int num)
{
    makeModel(&s_modelS, &s_objS, 0, 1);
    short *p = s_modelS.strip;
    p[0] = (short)num;
    p += 8;
    for (int k = 0; k < num; k++, p += 8) {
        p[2] = p[3] = p[4] = p[5] = index[k];
        p[6] = p[7] = 0;
    }
    p[0] = -1;
    snprintf(s_modelS.mdl.name, sizeof(s_modelS.mdl.name), "%s", name);
    p2o_MakePacket(&s_objS);
    PacHeader *pk = (PacHeader *)s_modelS.mdl.groups->packets;
    const RdMeshRec *r = pk ? rd__mesh_rec(pac_HostMesh(pk)) : NULL;
    CHECK(r && r->vertexCount == (uint32_t)num, "strip model %s: a mesh of %d vertices", name, num);
    for (int i = 0; r && i < num; i++) {
        const float *v = r->stream[(size_t)i * r->qwPerVertex];
        CHECK(memcmp(v, s_modelS.vtx[expect[i]], 16) == 0, "strip model %s: vertex %d is index %d",
              name, i, expect[i]);
    }
}

static void stripPacketChecks(void)
{
    int count;
    const PacHostStrip *t = pac_HostStrips(&count);
    for (int r = 0; r < count; r++) {
        if (strcmp(t[r].model, "I") != 0) {
            continue;
        }
        short other[STRIP_MAX];
        memcpy(other, t[r].bad, (size_t)t[r].num * sizeof(short));
        other[0] = other[1];
        other[1] = t[r].bad[0];
        stripPacketCheck("I", t[r].bad, t[r].good, t[r].num);
        stripPacketCheck("I", other, other, t[r].num);
        return;
    }
    CHECK(0, "the strip table has the I's row");
}

/* ---- the geometry, from the disc */

typedef struct Pt {
    double x, y;
} Pt;

static double triArea(Pt a, Pt b, Pt c)
{
    return 0.5 * ((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y));
}

/* the area two triangles share: one clipped by the other's three edges */
static double triOverlap(Pt a0, Pt a1, Pt a2, Pt b0, Pt b1, Pt b2)
{
    Pt poly[16], out[16];
    Pt A[3] = {a0, a1, a2}, B[3] = {b0, b1, b2};
    if (triArea(a0, a1, a2) < 0) {
        A[1] = a2;
        A[2] = a1;
    }
    if (triArea(b0, b1, b2) < 0) {
        B[1] = b2;
        B[2] = b1;
    }
    int n = 3;
    memcpy(poly, A, sizeof(A));
    for (int e = 0; e < 3 && n > 0; e++) {
        Pt p0 = B[e], p1 = B[(e + 1) % 3];
        int m = 0;
        for (int i = 0; i < n; i++) {
            Pt p = poly[i], q = poly[(i + 1) % n];
            double sp = (p1.x - p0.x) * (p.y - p0.y) - (p1.y - p0.y) * (p.x - p0.x);
            double sq = (p1.x - p0.x) * (q.y - p0.y) - (p1.y - p0.y) * (q.x - p0.x);
            if (sp >= 0) {
                out[m++] = p;
            }
            if ((sp >= 0) != (sq >= 0)) {
                double f = sp / (sp - sq);
                out[m++] = (Pt){p.x + f * (q.x - p.x), p.y + f * (q.y - p.y)};
            }
        }
        n = m;
        memcpy(poly, out, (size_t)m * sizeof(Pt));
    }
    double s = 0;
    for (int i = 0; i < n; i++) {
        s += poly[i].x * poly[(i + 1) % n].y - poly[(i + 1) % n].x * poly[i].y;
    }
    return fabs(0.5 * s);
}

typedef struct StripGeo {
    double area;    /* the triangles' areas */
    double overlap; /* the areas they share, pair by pair */
    int folds;      /* triangles winding against the strip's first */
} StripGeo;

static StripGeo stripGeo(const Pt *p, int n)
{
    StripGeo g = {0, 0, 0};
    double first = 0;
    for (int k = 0; k + 2 < n; k++) {
        double a = triArea(p[k], p[k + 1], p[k + 2]) * (k & 1 ? -1 : 1);
        if (k == 0) {
            first = a;
        }
        g.area += fabs(a);
        g.folds += a * first <= 0;
        for (int j = k + 1; j + 2 < n; j++) {
            g.overlap += triOverlap(p[k], p[k + 1], p[k + 2], p[j], p[j + 1], p[j + 2]);
        }
    }
    return g;
}

static uint32_t rd32(const unsigned char *f, uint32_t at)
{
    uint32_t v;
    memcpy(&v, f + at, 4);
    return v;
}

static int16_t rd16(const unsigned char *f, uint32_t at)
{
    int16_t v;
    memcpy(&v, f + at, 2);
    return v;
}

/* Row h's strip in the model file f (size bytes): its position indices in
 * the disc's order matched, the vertex table's x, y of each entry in out
 * (the disc's order); 0, or -1 with no such strip.  *found counts the
 * strips that match. */
static int findStrip(const unsigned char *f, uint32_t size, const PacHostStrip *h, Pt *out,
                     int *found)
{
    *found = 0;
    if (size < 0x18 || memcmp(f, "PS2O", 4) != 0) {
        return -1;
    }
    const uint32_t tbl = rd32(f, 4), objNum = rd32(f, 8);
    for (uint32_t o = 0; o < objNum && tbl + o * 4 + 4 <= size; o++) {
        const uint32_t rec = rd32(f, tbl + o * 4);
        if (rec + 0x180 > size || memcmp(f + rec + 0x80, "OBJH", 4) != 0) {
            return -1;
        }
        const uint32_t vtx = rd32(f, rec + 0x90), vtxCount = rd32(f, rec + 0x94);
        const uint32_t strips = rd32(f, rec + 0x100), stripCount = rd32(f, rec + 0x104);
        for (uint32_t s = 0; s < stripCount && strips + s * 4 + 4 <= size; s++) {
            /* head records (the count first), each followed by its vertex
               records; a count of -1 ends the list */
            uint32_t at = rd32(f, strips + s * 4);
            while (at + 16 <= size && rd16(f, at) != -1) {
                const int n = rd16(f, at);
                int k = 0;
                while (k < n && n == h->num && at + 16 + (uint32_t)k * 16 + 16 <= size &&
                       rd16(f, at + 16 + (uint32_t)k * 16 + 4) == h->bad[k]) {
                    k++;
                }
                if (n == h->num && k == n) {
                    float z = 0;
                    (*found)++;
                    for (k = 0; k < n; k++) {
                        const uint32_t v = vtx + (uint32_t)h->bad[k] * 16;
                        float q[4];
                        if ((uint32_t)h->bad[k] >= vtxCount || v + 16 > size) {
                            return -1;
                        }
                        memcpy(q, f + v, 16);
                        z = k == 0 ? q[2] : z;
                        CHECK(q[2] == z && q[3] == 1.0f,
                              "%s: index %d in the strip's plane (%g, %g)", h->model, h->bad[k],
                              q[2], q[3]);
                        out[k] = (Pt){q[0], q[1]};
                    }
                }
                at += 16 + (uint32_t)(n > 0 ? n : 0) * 16;
            }
        }
    }
    return *found ? 0 : -1;
}

static void stripDiscChecks(const char *disc)
{
    IcoVfs *vfs = disc ? ico_vfs_mount(&ico_vfs_iso9660, disc) : NULL;
    if (!vfs) {
        printf("  strip order: SKIP the disc's geometry (no disc image)\n");
        return;
    }
    int count;
    const PacHostStrip *t = pac_HostStrips(&count);
    for (int r = 0; r < count; r++) {
        const PacHostStrip *h = &t[r];
        char path[64];
        IcoDfMember m;
        unsigned char *f = NULL;
        snprintf(path, sizeof(path), "object/sdf/st26a/model/%s.p2o", h->model);
        if (ico_df_find_member(vfs, path, &m) != 0 || !(f = malloc(m.size)) ||
            ico_df_read_member(vfs, &m, f) != 0) {
            CHECK(0, "row %d: %s from the disc", r, path);
            free(f);
            continue;
        }
        Pt bad[STRIP_MAX], good[STRIP_MAX], outline[STRIP_MAX];
        int found;
        if (findStrip(f, m.size, h, bad, &found) != 0 || found != 1) {
            CHECK(0, "row %d: one strip of %d entries in %s with the table's indices (%d)", r,
                  h->num, path, found);
            free(f);
            continue;
        }
        free(f);
        const int n = h->num;
        for (int i = 0; i < n; i++) {
            good[i] = bad[pac_HostStripOrder(h->model, h->bad, 1, n, i)];
        }
        /* the outline the zig-zag bounds: the even entries, then the odd
           ones back */
        int o = 0;
        for (int i = 0; i < n; i += 2) {
            outline[o++] = bad[i];
        }
        for (int i = (n - 1) | 1; i > 0; i -= 2) {
            if (i < n) {
                outline[o++] = bad[i];
            }
        }
        double shoe = 0;
        for (int i = 0; i < n; i++) {
            shoe += outline[i].x * outline[(i + 1) % n].y - outline[(i + 1) % n].x * outline[i].y;
        }
        shoe = fabs(0.5 * shoe);
        const StripGeo gb = stripGeo(bad, n), gg = stripGeo(good, n);
        printf("  strip order: %s, %d entries: outline %.6f; disc order area %.6f overlap %.6f, "
               "%d folded; drawn order area %.6f overlap %.8f, %d folded\n",
               h->model, n, shoe, gb.area, gb.overlap, gb.folds, gg.area, gg.overlap, gg.folds);
        CHECK(gb.overlap > 1e-4 * shoe, "row %d: the disc's order overlaps", r);
        CHECK(gg.overlap <= 1e-6 * shoe, "row %d: the drawn order overlaps %g", r, gg.overlap);
        CHECK(gg.folds == 0, "row %d: %d triangles of the drawn order fold", r, gg.folds);
        CHECK(fabs(gg.area - shoe) <= 1e-5 * shoe, "row %d: the drawn order covers %g of %g", r,
              gg.area, shoe);
    }
    ico_vfs_unmount(vfs);
}

/* ---------------------------------------------------------- model packs
 *
 * The mesh identity (rd_vu_mesh_desc_hash) on a synthetic prelit packet of
 * two batches (5 and 6 vertices, a strip restart at vertex 3 of the
 * second) against the byte stream rd_mesh.h specifies, and on the game's
 * packets against the hash rd_create_vu_mesh stored; replacements from a
 * tagless stream (counts, indices, PRIM/material/group from the original,
 * the rejections, rd_update_vu_mesh refusing them); one drawn through
 * reg_DispObj in the original's place; rd_vu_mesh_retire and the sweep. */
#define SYN_QW (2 + 11 * RD_VU_QW_PRELIT)

typedef struct Synth {
    float qw[SYN_QW][4];
    RdVuBatchDesc b[2];
    RdVuMeshDesc d;
} Synth;

static void synthPacket(Synth *s, uint32_t prim, uint16_t material)
{
    static const uint32_t counts[2] = {5, 6};
    memset(s, 0, sizeof(*s));
    uint32_t at = 0;
    for (uint32_t i = 0; i < 2; i++) {
        /* NLOOP, EOP; PRE, PRIM, NREG 3 in the upper word */
        const uint32_t tag[4] = {counts[i] | 0x8000u, (1u << 14) | (prim << 15) | (3u << 28), 0x512,
                                 0};
        memcpy(s->qw[at], tag, 16);
        s->b[i].firstQw = at;
        s->b[i].material = (uint16_t)(material + i);
        s->b[i].group = (uint16_t)i;
        at++;
        for (uint32_t k = 0; k < counts[i]; k++, at += RD_VU_QW_PRELIT) {
            const int restart = k == 0 || (i == 1 && k == 3);
            qw4(s->qw[at], (float)k, (float)i, 2.0f, 1.0f);
            qw4(s->qw[at + 1], 0.125f * (float)k, 0.5f, 1.0f, restart ? 0.0f : 1.0f);
            qw4(s->qw[at + 2], (float)(10 + k), (float)(20 + i), 30.0f, 127.0f);
        }
    }
    s->d.qw = (const float (*)[4])s->qw;
    s->d.qwCount = at;
    s->d.qwPerVertex = RD_VU_QW_PRELIT;
    s->d.batchCount = 2;
    s->d.batches = s->b;
    s->d.materialCount = 2;
    s->d.debugName = "synth";
}

/* the identity's bytes, written out as rd_mesh.h lists them */
static uint64_t synthHashByHand(const Synth *s)
{
    static uint8_t buf[16 + 8 + 11 * RD_VU_QW_PRELIT * 16];
    const uint32_t hdr[4] = {RD_VU_QW_PRELIT, 2, 5, 6};
    memcpy(buf, "ICOMESH1", 8);
    memcpy(buf + 8, hdr, 16);
    memcpy(buf + 24, s->qw[1], 5 * RD_VU_QW_PRELIT * 16);
    memcpy(buf + 24 + 5 * RD_VU_QW_PRELIT * 16, s->qw[2 + 5 * RD_VU_QW_PRELIT],
           6 * RD_VU_QW_PRELIT * 16);
    return xxh3_64(buf, sizeof(buf));
}

static void hashChecks(void)
{
    static Synth a, b;
    synthPacket(&a, 0x0C, 0);
    synthPacket(&b, 0x0C, 0);
    uint32_t nv = 99;
    float nw = 99.0f;
    const uint64_t ha = rd_vu_mesh_desc_hash(&a.d, &nv, &nw);
    CHECK(ha != 0 && ha == rd_vu_mesh_desc_hash(&b.d, NULL, NULL), "two builds hash alike");
    CHECK(ha == synthHashByHand(&a), "the hash is XXH3-64 of rd_mesh.h's byte stream");
    CHECK(nv == 11 && nw == 0.0f, "11 vertices (%u), prelit normal.w 0 (%g)", nv, (double)nw);
    /* one colour byte (batch 1, vertex 2's red) */
    uint8_t *c = (uint8_t *)&b.qw[2 + 5 * RD_VU_QW_PRELIT + 2 * RD_VU_QW_PRELIT + 2][0];
    c[0] ^= 1;
    CHECK(rd_vu_mesh_desc_hash(&b.d, NULL, NULL) != ha, "one colour byte changes the hash");
    /* PRIM, material and group are not in the key */
    synthPacket(&b, 0x1C, 7);
    b.b[1].group = 5;
    CHECK(rd_vu_mesh_desc_hash(&b.d, NULL, NULL) == ha, "PRIM, material and group are not hashed");
    /* malformed: the last batch runs past the stream */
    b.d.qwCount--;
    nv = 99;
    nw = 99.0f;
    CHECK(rd_vu_mesh_desc_hash(&b.d, &nv, &nw) == 0 && nv == 0 && nw == 0.0f,
          "a malformed desc hashes to 0");
    CHECK(rd_vu_mesh_desc_hash(NULL, NULL, NULL) == 0, "no desc hashes to 0");
    /* rd_create_vu_mesh keeps the same hash */
    RdMesh m = rd_create_vu_mesh(&a.d);
    CHECK(m.id && rd_vu_mesh_hash(m) == ha && !rd_vu_mesh_replaced(m), "the record's hash");
    rd_destroy_vu_mesh(m);
    /* the game's packets: the desc rebuilt from the packet hashes as the
     * mesh pac_HostMesh built; skinned: normal.w of the first vertex */
    static PkDesc pd;
    packetDesc(packetA(), RD_VU_QW_PRELIT, &pd);
    RdMesh pa = {pac_HostMesh(packetA())};
    CHECK(rd_vu_mesh_hash(pa) != 0 &&
              rd_vu_mesh_desc_hash(&pd.d, &nv, NULL) == rd_vu_mesh_hash(pa) && nv == NV,
          "the prelit packet's hash (%u vertices)", nv);
    packetDesc(packetB(), RD_VU_QW_SKIN, &pd);
    RdMesh pb = {pac_HostMesh(packetB())};
    const RdMeshRec *rb = rd__mesh_rec(pb.id);
    CHECK(rb && rd_vu_mesh_desc_hash(&pd.d, NULL, &nw) == rd_vu_mesh_hash(pb) &&
              nw == rb->stream[1][3],
          "the cluster packet's hash, normal.w %g", (double)nw);
    CHECK(rd_vu_mesh_hash(pa) != rd_vu_mesh_hash(pb), "two parts, two hashes");
}

static void replacementChecks(void)
{
    static Synth o;
    synthPacket(&o, 0x0C, 3);
    { /* batch 1's tag: PRIM 0x1C */
        uint32_t hi = (1u << 14) | (0x1Cu << 15) | (3u << 28);
        memcpy(&o.qw[o.b[1].firstQw][1], &hi, 4);
    }
    const uint64_t h = rd_vu_mesh_desc_hash(&o.d, NULL, NULL);
    /* the tagless vertices of the synthetic packet, the batches swapped */
    static float flat[11 * RD_VU_QW_PRELIT][4];
    memcpy(flat[0], o.qw[1], 5 * RD_VU_QW_PRELIT * 16);
    memcpy(flat[5 * RD_VU_QW_PRELIT], o.qw[2 + 5 * RD_VU_QW_PRELIT], 6 * RD_VU_QW_PRELIT * 16);
    RdVuReplacementBatch rb[3] = {{5, 6}, {0, 5}, {0, 0}};
    RdVuReplacement rep = {(const float (*)[4])flat, 11, RD_VU_QW_PRELIT, rb, 2};
    RdMesh m = rd_create_vu_mesh_replacement(&o.d, &rep, "synth rep");
    const RdMeshRec *r = rd__mesh_rec(m.id);
    CHECK(r && r->vu && r->replaced && r->hash == h && rd_vu_mesh_replaced(m) &&
              rd_vu_mesh_hash(m) == h && rd_vu_mesh_valid(m),
          "a replaced VU mesh with the original's hash");
    if (r) {
        CHECK(r->vertexCount == 11 && r->batchCount == 2 && r->qwPerVertex == RD_VU_QW_PRELIT &&
                  r->materialCount == 2 && r->indexCount == 15,
              "11 vertices, 2 batches, 15 indices (%u %u %u)", r->vertexCount, r->batchCount,
              r->indexCount);
        /* batch 0 draws the original's batch 1 vertices: kicks at 2 and 5
         * (3 and 4 touch the restart); batch 1 the first five: 2, 3, 4 */
        static const uint32_t kick[5] = {2, 5, 8, 9, 10};
        int ok = r->indexCount == 15;
        for (int i = 0; ok && i < 5; i++) {
            for (uint32_t c = 0; c < 3; c++) {
                ok &= r->index[i * 3 + c] == ICO_VU_INDEX(kick[i], c);
            }
        }
        CHECK(ok, "the index list from the strip flags");
        CHECK(r->batches[0].firstVertex == 0 && r->batches[0].vertexCount == 6 &&
                  r->batches[0].firstIndex == 0 && r->batches[0].indexCount == 6 &&
                  r->batches[1].firstVertex == 6 && r->batches[1].vertexCount == 5 &&
                  r->batches[1].firstIndex == 6 && r->batches[1].indexCount == 9,
              "the batch ranges");
        CHECK(r->batches[0].prim == 0x0C && r->batches[1].prim == 0x1C &&
                  r->batches[0].material == 3 && r->batches[1].material == 4 &&
                  r->batches[0].group == 0 && r->batches[1].group == 1,
              "PRIM, material and group from the original's batches (%x %x)", r->batches[0].prim,
              r->batches[1].prim);
        CHECK(memcmp(r->stream[0], flat[5 * RD_VU_QW_PRELIT], 6 * RD_VU_QW_PRELIT * 16) == 0 &&
                  memcmp(r->stream[6 * RD_VU_QW_PRELIT], flat[0], 5 * RD_VU_QW_PRELIT * 16) == 0,
              "the stream batch by batch");
    }
    /* the morph path cannot write a replaced mesh */
    float before[4];
    memcpy(before, r ? r->stream[0] : before, 16);
    CHECK(!rd_update_vu_mesh(m, (const float (*)[4])o.qw),
          "rd_update_vu_mesh refuses a replaced mesh");
    CHECK(!r || memcmp(before, r->stream[0], 16) == 0, "and writes nothing");
    RdMesh om = rd_create_vu_mesh(&o.d);
    CHECK(rd_update_vu_mesh(om, (const float (*)[4])o.qw), "rd_update_vu_mesh writes an original");
    rd_destroy_vu_mesh(om);
    rd_destroy_vu_mesh(m);
    /* fewer batches: the rest empty */
    rep.batchCount = 1;
    m = rd_create_vu_mesh_replacement(&o.d, &rep, NULL);
    r = rd__mesh_rec(m.id);
    CHECK(r && r->batchCount == 2 && r->batches[1].vertexCount == 0 &&
              r->batches[1].indexCount == 0 && r->indexCount == 6 && r->vertexCount == 6 &&
              strcmp(r->name, "synth") == 0,
          "a missing batch is empty, the name the original's");
    rd_destroy_vu_mesh(m);
    /* rejections */
    rep.batchCount = 3;
    CHECK(rd_create_vu_mesh_replacement(&o.d, &rep, NULL).id == 0,
          "more batches than the original");
    rep.batchCount = 2;
    rep.qwPerVertex = RD_VU_QW_LIT;
    CHECK(rd_create_vu_mesh_replacement(&o.d, &rep, NULL).id == 0, "another layout");
    rep.qwPerVertex = RD_VU_QW_PRELIT;
    rb[1].vertexCount = 12;
    CHECK(rd_create_vu_mesh_replacement(&o.d, &rep, NULL).id == 0, "a run outside the stream");
    rb[1].vertexCount = 5;
    o.d.qwCount--;
    CHECK(rd_create_vu_mesh_replacement(&o.d, &rep, NULL).id == 0, "a malformed original");
    o.d.qwCount++;
    CHECK(rd_create_vu_mesh_replacement(&o.d, NULL, NULL).id == 0, "no replacement");
}

static uint64_t s_retireHash;

static int s_retireCalls;

static bool retireReplaced(uint64_t hash, bool replaced, void *user)
{
    (void)user;
    s_retireCalls++;
    return replaced && hash == s_retireHash;
}

static bool retireOriginal(uint64_t hash, bool replaced, void *user)
{
    (void)user;
    return !replaced && hash == s_retireHash;
}

/* the prelit packet drawn through reg_DispObj with a replacement in its
 * place (what the game hook does), then retired */
static void replacementDrawChecks(void)
{
    PacHeader *pk = packetA();
    RdMesh orig = {pac_HostMesh(pk)};
    const RdMeshRec *orc = rd__mesh_rec(orig.id);
    if (!orc) {
        CHECK(0, "the prelit mesh");
        return;
    }
    static PkDesc pd;
    packetDesc(pk, RD_VU_QW_PRELIT, &pd);
    /* the original's tagless stream, every vertex 0.25 to the right */
    static float flat[NV * RD_VU_QW_PRELIT][4];
    memcpy(flat, orc->stream, sizeof(flat));
    for (int v = 0; v < NV; v++) {
        flat[v * RD_VU_QW_PRELIT][0] += 0.25f;
    }
    RdVuReplacementBatch rb[16];
    for (uint32_t b = 0; b < orc->batchCount && b < 16; b++) {
        rb[b].firstVertex = orc->batches[b].firstVertex;
        rb[b].vertexCount = orc->batches[b].vertexCount;
    }
    RdVuReplacement rep = {(const float (*)[4])flat, NV, RD_VU_QW_PRELIT, rb, orc->batchCount};
    RdMesh m = rd_create_vu_mesh_replacement(&pd.d, &rep, "test_prelit rep");
    const RdMeshRec *r = rd__mesh_rec(m.id);
    CHECK(r && r->hash == orc->hash && r->indexCount == orc->indexCount &&
              r->batchCount == orc->batchCount,
          "the replacement keeps the original's hash, batches and kicks");
    if (!r) {
        return;
    }
    unsigned char saved[sizeof(pk->pad9C)];
    memcpy(saved, pk->pad9C, sizeof(saved));
    memcpy(pk->pad9C, &m.id, sizeof(m.id));
    recordPrelit(&s_objA, 1);
    const RdFrame *f = rd__last_frame();
    Found fd;
    walkFrame(f, &fd);
    CHECK(fd.n == 1 && fd.cmd[0]->type == RDC_MESH && fd.cmd[0]->u[0] == m.id,
          "one RDC_MESH of the replacement");
    if (fd.n >= 1) {
        const float *mem;
        RdVuPayload p = payloadOf(f, fd.cmd[0], &mem);
        CHECK(p.prog == RD_PROG_PRELIT && p.code == 32 && p.clip == RD_VU_CLIP_REGION &&
                  p.firstBatch == 0 && p.batchCount == orc->batchCount &&
                  p.qwPerVertex == RD_VU_QW_PRELIT,
              "normal_c 32 over all %u batches (prog %u code %u, %u batches)", orc->batchCount,
              p.prog, p.code, p.batchCount);
    }
    /* retire the replacement: invalid at once, freed once three frames
     * have opened since its draw */
    const uint32_t drawn = r->lastUsed;
    s_retireHash = orc->hash;
    s_retireCalls = 0;
    rd_vu_mesh_retire(retireReplaced, NULL);
    CHECK(s_retireCalls > 0 && !rd_vu_mesh_valid(m) && rd_vu_mesh_valid(orig) && rd__mesh_rec(m.id),
          "retired: the replacement invalid, kept; the original valid");
    int kept = 1;
    for (int i = 0; i < 4; i++) {
        dl_Clear();
        dl_Swap();
        if (g_rd.frameCounter <= drawn + 3) {
            kept &= rd__mesh_rec(m.id) != NULL;
        }
    }
    CHECK(kept && rd__mesh_rec(m.id) == NULL, "kept for the retained frames, then freed");
    memcpy(pk->pad9C, saved, sizeof(saved));
    /* retiring the original: pac_HostMesh builds it again */
    rd_vu_mesh_retire(retireOriginal, NULL);
    CHECK(!rd_vu_mesh_valid(orig), "the original retired");
    RdMesh again = {pac_HostMesh(pk)};
    CHECK(again.id != 0 && again.id != orig.id && rd_vu_mesh_valid(again) &&
              rd_vu_mesh_hash(again) == s_retireHash,
          "pac_HostMesh builds a retired mesh again with the same hash");
}

/* -------------------------------------------- model packs: the game's hooks
 *
 * Packet.c / RegistPacket.c with a real pack (modelpack.c) in a folder of
 * the working directory: what regHostMesh dumps when dumping is on, and
 * what pac_HostMeshFor builds and pac_HostRefreshFor reverts once the dumps
 * are the pack's replacements. */
static const char *s_mpBase = "rd_mesh_modelpack";

static const char *mpPath(const char *rel)
{
    static char buf[4][700];
    static int k;
    char *b = buf[k = (k + 1) & 3];
    snprintf(b, sizeof(buf[0]), "%s/%s", s_mpBase, rel);
    return b;
}

typedef struct MpFiles {
    char p[64][700];
    int n;
} MpFiles;

static int mpCollect(const char *path, const char *name, void *user)
{
    MpFiles *f = user;
    (void)name;
    if (f->n < 64) {
        snprintf(f->p[f->n++], sizeof(f->p[0]), "%s", path);
    }
    return 0;
}

/* every file under the pack's folder removed (the folders stay) */
static void mpClear(void)
{
    static MpFiles f;
    f.n = 0;
    ico_dir_walk(s_mpBase, 8, mpCollect, &f);
    for (int i = 0; i < f.n; i++) {
        ico_remove(f.p[i]);
    }
}

static const char *mpHex(uint64_t h)
{
    static char b[4][17];
    static int k;
    char *o = b[k = (k + 1) & 3];
    snprintf(o, 17, "%016llx", (unsigned long long)h);
    return o;
}

static int mpExists(const char *path)
{
    return ico_path_kind(path, NULL, NULL) >= 0;
}

/* the mesh of the one draw the last frame recorded, 0 when not one */
static uint32_t mpDrawnMesh(RdCmdType type)
{
    Found fd;
    walkFrame(rd__last_frame(), &fd);
    return fd.n == 1 && fd.cmd[0]->type == type ? fd.cmd[0]->u[0] : 0;
}

static void packHookChecks(void)
{
    static PkDesc pa, pb;
    packetDesc(packetA(), RD_VU_QW_PRELIT, &pa);
    packetDesc(packetB(), RD_VU_QW_SKIN, &pb);
    const uint64_t ha = rd_vu_mesh_desc_hash(&pa.d, NULL, NULL);
    const uint64_t hb = rd_vu_mesh_desc_hash(&pb.d, NULL, NULL);
    (void)ico_mkdir(s_mpBase);
    (void)ico_mkdir(mpPath("models"));
    (void)ico_mkdir(mpPath("models/SCES-50760"));
    (void)ico_mkdir(mpPath("models/SCES-50760/replacements"));
    mpClear(); /* an earlier run's files */
    ModelpackConfig cfg = {s_mpBase, NULL, "SCES-50760", 0, 1};
    CHECK(modelpack_init(&cfg) == 0, "an empty pack");

    /* dumping on: each part drawn is written, once */
    recordPrelit(&s_objA, 1);
    recordCluster();
    char rel[200];
    snprintf(rel, sizeof(rel), "models/SCES-50760/dumps/%s.gltf", mpHex(ha));
    CHECK(mpExists(mpPath(rel)), "the prelit part dumped at its draw (%s)", rel);
    snprintf(rel, sizeof(rel), "models/SCES-50760/dumps/%s.gltf", mpHex(hb));
    CHECK(mpExists(mpPath(rel)), "the cluster part dumped at its draw (%s)", rel);
    GltfDoc doc;
    char why[256] = "";
    if (gltf_read(mpPath(rel), &doc, why, sizeof(why)) == 0) {
        CHECK(doc.skin.count == (uint32_t)s_objB.nodeNum &&
                  memcmp(doc.skin.invBind[1], s_modelB.clusterMtx[1], 64) == 0 &&
                  doc.skin.parent[0] < 0,
              "the skeleton from the object: %u bones, clusterMtx as the inverse binds",
              doc.skin.count);
        CHECK(doc.meshName && strcmp(doc.meshName, "test_cluster/part0/0") == 0,
              "the cluster part named by the draw (%s)", doc.meshName ? doc.meshName : "-");
        gltf_free(&doc);
    } else {
        CHECK(0, "the cluster dump reads: %s", why);
    }
    ModelpackStats st;
    modelpack_get_stats(&st);
    recordPrelit(&s_objA, 1);
    ModelpackStats st2;
    modelpack_get_stats(&st2);
    CHECK(st.dumped == 2 && st2.dumped == 2, "two parts dumped, once (%u, %u)", st.dumped,
          st2.dumped);

    /* the dumps as the pack's replacements */
    for (int i = 0; i < 2; i++) {
        for (int k = 0; k < 2; k++) {
            char from[200], to[200];
            snprintf(from, sizeof(from), "models/SCES-50760/dumps/%s.%s", mpHex(i ? hb : ha),
                     k ? "bin" : "gltf");
            snprintf(to, sizeof(to), "models/SCES-50760/replacements/%s.%s", mpHex(i ? hb : ha),
                     k ? "bin" : "gltf");
            CHECK(ico_rename_replace(mpPath(from), mpPath(to)) == 0, "move %s", from);
        }
    }
    cfg.dumpEnabled = 0;
    CHECK(modelpack_init(&cfg) == 2 && modelpack_lookup(ha) >= 0 && modelpack_lookup(hb) >= 0,
          "both parts indexed");
    /* the switch off and on: the originals retired, made again at the draw */
    const RdMesh oa = {pac_HostMesh(packetA())}, ob = {pac_HostMesh(packetB())};
    modelpack_set_enabled(false);
    modelpack_set_enabled(true);
    CHECK(!rd_vu_mesh_valid(oa) && !rd_vu_mesh_valid(ob), "on: the originals retired");
    recordPrelit(&s_objA, 1);
    const RdMesh ra = {mpDrawnMesh(RDC_MESH)};
    CHECK(ra.id != 0 && rd_vu_mesh_replaced(ra) && rd_vu_mesh_hash(ra) == ha &&
              pac_HostMesh(packetA()) == ra.id,
          "the prelit draw records the replacement Packet.c made");
    recordCluster();
    const RdMesh rb = {mpDrawnMesh(RDC_SKINNED)};
    CHECK(rb.id != 0 && rd_vu_mesh_replaced(rb) && rd_vu_mesh_hash(rb) == hb &&
              pac_HostMesh(packetB()) == rb.id,
          "the cluster draw records the replacement (bones from the object)");
    modelpack_get_stats(&st);
    CHECK(st.created == 2 && st.declined == 0, "two replacements made (%u), none declined (%u)",
          st.created, st.declined);

    /* the morph path: a replaced mesh cannot follow it, the original back */
    PacHostIdent id = {"test_prelit", 0, 0, NULL, s_objA.nodeNum, &s_objA};
    pac_HostRefreshFor(packetA(), &id);
    const RdMesh back = {pac_HostMesh(packetA())};
    CHECK(!rd_vu_mesh_valid(ra) && back.id != 0 && back.id != ra.id && rd_vu_mesh_valid(back) &&
              !rd_vu_mesh_replaced(back) && rd_vu_mesh_hash(back) == ha,
          "the morph path: the replacement retired, the original built");
    CHECK(modelpack_lookup(ha) < 0 && modelpack_lookup(hb) >= 0, "the morphing part declined");
    recordPrelit(&s_objA, 1);
    CHECK(mpDrawnMesh(RDC_MESH) == back.id, "the original drawn again");
    pac_HostRefreshFor(packetA(), &id);
    CHECK(pac_HostMesh(packetA()) == back.id, "an original follows the morph in place");

    /* the pack away: the other tests draw the originals */
    modelpack_set_enabled(false);
    modelpack_shutdown();
    CHECK(!rd_vu_mesh_valid(rb) && modelpack_lookup(hb) < 0, "the pack switched off and closed");
    mpClear();
}

static void modelPackChecks(void)
{
    hashChecks();
    replacementChecks();
    replacementDrawChecks();
    packHookChecks();
}

/* ----------------------------------------------------- natural chains */
/* rd_chain.h.  Recording: the table names the two let-down chains (st10l's
 * Null 21, st19a's Null 22, Y, 1000 units of links 50 apart); a chain at
 * the Stone Pillar's first keyed scale (0.1) laid out as BgAnimation.c lays
 * it out with the option on (rd_chain_null for the Null, rd_chain_link per
 * link) fills the stretch the disc's squashed chain fills, with its links
 * at unit scale 50 apart: the last link's bottom where the squashed one's
 * is (the Null's place + 0.1 x (1000 + 54.82)) and one plane for all of
 * them where the squashed top link begins (0.1 x -5.37): the links wholly
 * above it clipped whole, the last one whole, two cut; at full length the
 * layout is the disc's (no move, nothing clipped).  A prelit model
 * drawn as a stage animation draws it (dispType 2, two nodes) through
 * reg_DispObj: the noted node's draw carries its plane (b[5], f[0]), the
 * other none, and none once the note is cleared.
 * Device: a quad y -1000..1000 drawn with the plane y >= 0 is drawn below
 * the quad's middle only, those rows byte for byte the plain draw's; the
 * minus side the other half; an offset past the quad clips nothing (the
 * plain picture) and one before it draws nothing. */

/* the y of a link's node in the Null's frame and the world y of its plane,
   for the chain c at scale s: link i's node at pos (unscaled) */
static void chainLink(const RdChain *c, float s, float nullY, float pos, float *nodeY,
                      float *planeY, uint8_t *code)
{
    const float sc[3] = {1.0f, s, 1.0f};
    float so[3], t[4], off;
    rd_chain_null(c, sc, so, t);
    rd_chain_link(c, s, pos, code, &off);
    /* the Null at nullY, unscaled along Y (so[1]), moved by t; the link at
       pos below it; the plane where the link's own y is -off */
    *nodeY = nullY + so[1] * (t[1] + pos);
    *planeY = *nodeY - off;
}

static void chainTableChecks(void)
{
    const RdChain *a = rd_chain_find(731), *b = rd_chain_find(636);
    CHECK(rd__chain_count == 2 && a && b && !rd_chain_find(0) && !rd_chain_find(349),
          "natural chains: two listed (%d), by their groups", rd__chain_count);
    if (!a || !b) {
        return;
    }
    CHECK(a->null == 21 && a->axis == 1 && a->anchor == 0.0f && a->end == 1000.0f &&
              b->null == 22 && b->axis == 1 && b->anchor == 2700.0f && b->end == 3700.0f,
          "natural chains: st10l Null 21 Y 0..1000, st19a Null 22 Y 2700..3700");
    /* the Stone Pillar's chain at its first keyed scale */
    const float sc[3] = {1.0f, 0.1f, 1.0f};
    float so[3], t[4];
    rd_chain_null(a, sc, so, t);
    CHECK(so[0] == 1.0f && so[1] == 1.0f && so[2] == 1.0f && t[0] == 0.0f &&
              fabsf(t[1] + 0.9f * 1054.82f) < 1e-3f && t[2] == 0.0f && t[3] == 1.0f,
          "natural chains: the Null at scale 0.1 keeps scale 1 and moves -0.9 x 1054.82 (%g %g "
          "%g, %g)",
          (double)so[0], (double)so[1], (double)so[2], (double)t[1]);
    const float nullY = -50.0f; /* st10l_chain_down's Null */
    float first = 0.0f, prevY = 0.0f;
    int whole = 0, clipped = 0, part = 0, samePlane = 1, pitch = 1, codes = 1;
    for (int i = 0; i <= 20; i++) {
        float y, py;
        uint8_t code;
        chainLink(a, 0.1f, nullY, 50.0f * (float)i, &y, &py, &code);
        codes &= code == rd_chain_plane_code(1, 0);
        if (i == 0) {
            first = py;
        } else {
            samePlane &= fabsf(py - first) < 1e-3f;
            pitch &= fabsf(y - prevY - 50.0f) < 1e-3f;
        }
        prevY = y;
        /* chain4: y -5.37 .. 54.82 about its node */
        if (y - 5.37f >= py - 1e-3f) {
            whole++;
        } else if (y + 54.82f <= py) {
            clipped++;
        } else {
            part++;
        }
    }
    CHECK(codes && samePlane && fabsf(first - (nullY - 0.1f * 5.37f)) < 1e-3f,
          "natural chains: one plane on Y for every link, where the squashed top link begins "
          "(%g)",
          (double)first);
    CHECK(pitch && fabsf(prevY + 54.82f - (nullY + 0.1f * 1054.82f)) < 1e-3f,
          "natural chains: the links 50 apart at their size, the last one's bottom where the "
          "squashed one's is (%g)",
          (double)(prevY + 54.82f));
    CHECK(whole == 1 && part == 2 && clipped == 18,
          "natural chains: the last link whole, two cut, 18 above the top (%d %d %d)", whole, part,
          clipped);
    /* at full length: the disc's layout, nothing clipped */
    const float one[3] = {1.0f, 1.0f, 1.0f};
    rd_chain_null(b, one, so, t);
    int none = t[1] == 0.0f && so[1] == 1.0f;
    for (int i = 0; i <= 20; i++) {
        float y, py;
        uint8_t code;
        chainLink(b, 1.0f, 0.0f, 2700.0f + 50.0f * (float)i, &y, &py, &code);
        none &= y == 2700.0f + 50.0f * (float)i && y - 5.37f >= py - 1e-3f;
    }
    CHECK(none, "natural chains: at full length the disc's layout and nothing clipped");
}

static void chainDrawChecks(void)
{
    /* the prelit model as a stage animation draws it: two nodes */
    const int nodeNum = s_objA.nodeNum, dispType = s_objA.dispType;
    struct DObjNode saved[2];
    memcpy(saved, s_modelA.nodes, sizeof(saved));
    s_objA.nodeNum = 2;
    s_objA.dispType = 2;
    for (int i = 0; i < 2; i++) {
        s_modelA.nodes[i].fade = 0.0f;
        s_modelA.nodes[i].alpha = 1.0f; /* stage_Init's: drawn */
    }
    const uint8_t code = rd_chain_plane_code(1, 0);
    rd_chain_reset();
    rd_chain_note(&s_objA, 1, code, -12.5f);
    recordPrelit(&s_objA, 1);
    const RdFrame *f = rd__last_frame();
    Found fd;
    walkFrame(f, &fd);
    CHECK(fd.n == 2 && fd.cmd[0]->type == RDC_MESH && fd.cmd[1]->type == RDC_MESH,
          "natural chains: two node draws (%d)", fd.n);
    if (fd.n == 2) {
        CHECK(fd.cmd[0]->b[5] == 0 && fd.cmd[0]->f[0] == 0.0f,
              "natural chains: node 0, not noted, has no plane (%u %g)", fd.cmd[0]->b[5],
              (double)fd.cmd[0]->f[0]);
        CHECK(fd.cmd[1]->b[5] == code && fd.cmd[1]->f[0] == -12.5f,
              "natural chains: node 1 carries its plane (%u %g)", fd.cmd[1]->b[5],
              (double)fd.cmd[1]->f[0]);
    }
    rd_chain_note(&s_objA, 1, 0, 0.0f); /* the option off: cleared */
    recordPrelit(&s_objA, 1);
    f = rd__last_frame();
    walkFrame(f, &fd);
    CHECK(fd.n == 2 && fd.cmd[0]->b[5] == 0 && fd.cmd[1]->b[5] == 0,
          "natural chains: cleared, no draw has a plane");
    rd_chain_reset();
    s_objA.nodeNum = nodeNum;
    s_objA.dispType = dispType;
    memcpy(s_modelA.nodes, saved, sizeof(saved));
}

static const char kChainKey;

/* a quad x and y -1000..1000 (z 0), prelit, one strip */
static RdMesh chainQuad(void)
{
    float qw[1 + 4 * 3][4];
    memset(qw, 0, sizeof(qw));
    const uint32_t tag = 0x8000u | 4u; /* NLOOP 4, EOP */
    memcpy(&qw[0][0], &tag, 4);
    for (int k = 0; k < 4; k++) {
        qw[1 + k * 3][0] = (k & 1) ? 1000.0f : -1000.0f;
        qw[1 + k * 3][1] = (k & 2) ? 1000.0f : -1000.0f;
        qw[1 + k * 3][3] = 1.0f;
        qw[2 + k * 3][0] = (float)(k & 1);
        qw[2 + k * 3][2] = 1.0f;
        qw[2 + k * 3][3] = k == 0 ? 0.0f : 1.0f; /* the strip starts at vertex 0 */
        qw[3 + k * 3][0] = 200.0f;
        qw[3 + k * 3][1] = 120.0f;
        qw[3 + k * 3][2] = 30.0f;
        qw[3 + k * 3][3] = 127.0f;
    }
    const RdVuBatchDesc bd = {0, 0, 0};
    RdVuMeshDesc md;
    memset(&md, 0, sizeof(md));
    md.qw = (const float (*)[4])qw;
    md.qwCount = 1 + 4 * 3;
    md.qwPerVertex = RD_VU_QW_PRELIT;
    md.batchCount = 1;
    md.batches = &bd;
    md.materialCount = 1;
    md.debugName = "chain quad";
    return rd_create_vu_mesh(&md);
}

/* one frame: SCENE cleared grey, the quad by code 32 at GS 2048 + 0.08 x,
   2048 + 0.08 y (rows 176..336), with the plane code / offset (0: none);
   the picture into img, false when it cannot be read */
static int chainDraw(RdMesh m, uint8_t code, float offset, uint8_t *img, uint32_t *w)
{
    static const uint8_t grey[4] = {40, 40, 60, 0x80};
    dl_Clear();
    dl_SetDLPriority(0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), s_sceneW, 512, 1);
    rd_clear_target(rd_target(RD_TARGET_SCENE), grey, 1, 0);
    rd_test_gs(RD_TEST_Z_GEQUAL);
    rd_z_write(1);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_abe(0);
    rd_pabe(0);
    rd_fba(0);
    rd_gouraud(1);
    rd_texture_off();
    RdVuDraw d;
    memset(&d, 0, sizeof(d));
    d.prog = RD_PROG_PRELIT;
    d.code = 32;
    d.clip = RD_VU_CLIP_REGION;
    d.plane = code;
    d.planeOffset = offset;
    d.vu.mem[16][0] = 0.08f;
    d.vu.mem[17][1] = 0.08f;
    d.vu.mem[19][0] = 2048.0f;
    d.vu.mem[19][1] = 2048.0f;
    d.vu.mem[19][2] = 1000.0f;
    d.vu.mem[19][3] = 1.0f;
    rd_draw_vu_mesh(m, &d, RD_KEY(&kChainKey, 0, 32));
    dl_Swap();
    return closingRead(img, w);
}

/* the texels drawn (not the grey clear) in rows y0..y1 - 1 */
static int chainRows(const uint8_t *img, uint32_t w, int y0, int y1)
{
    int n = 0;
    for (int y = y0; y < y1; y++) {
        for (uint32_t x = 0; x < w; x++) {
            const uint8_t *p = &img[((size_t)y * w + x) * 4];
            n += p[0] != 40 || p[1] != 40 || p[2] != 60;
        }
    }
    return n;
}

static uint8_t s_chainPlain[WT * 512 * 4];

static void chainPixels(void)
{
    const RdMesh m = chainQuad();
    uint32_t w = 0, wp = 0;
    if (!chainDraw(m, 0, 0.0f, s_chainPlain, &wp)) {
        CHECK(0, "natural chains: SCENE read");
        rd_destroy_vu_mesh(m);
        return;
    }
    const int top = chainRows(s_chainPlain, wp, 0, 254),
              bottom = chainRows(s_chainPlain, wp, 258, 512);
    CHECK(top >= 5000 && bottom >= 5000,
          "natural chains: the plain quad above and below its middle "
          "(%d, %d)",
          top, bottom);
    const uint8_t plus = rd_chain_plane_code(1, 0), minus = rd_chain_plane_code(1, 1);
    /* y >= 0: the rows below the middle only, as the plain draw has them */
    if (chainDraw(m, plus, 0.0f, s_closingA, &w)) {
        const int t = chainRows(s_closingA, w, 0, 254), b = chainRows(s_closingA, w, 258, 512);
        const size_t at = (size_t)258 * w * 4;
        CHECK(t == 0 && b == bottom && w == wp &&
                  memcmp(s_closingA + at, s_chainPlain + at, (size_t)(512 - 258) * w * 4) == 0,
              "natural chains: y >= 0 draws the lower half only, as the plain draw (%d above, %d "
              "below)",
              t, b);
    }
    /* -y >= 0: the other half */
    if (chainDraw(m, minus, 0.0f, s_closingA, &w)) {
        const int t = chainRows(s_closingA, w, 0, 254), b = chainRows(s_closingA, w, 258, 512);
        CHECK(t == top && b == 0 && memcmp(s_closingA, s_chainPlain, (size_t)254 * w * 4) == 0,
              "natural chains: -y >= 0 draws the upper half only (%d above, %d below)", t, b);
    }
    /* past the quad: nothing clipped; before it: nothing drawn */
    if (chainDraw(m, plus, 1500.0f, s_closingA, &w)) {
        CHECK(w == wp && memcmp(s_closingA, s_chainPlain, (size_t)512 * w * 4) == 0,
              "natural chains: a plane past the quad clips nothing");
    }
    if (chainDraw(m, plus, -1500.0f, s_closingA, &w)) {
        CHECK(chainRows(s_closingA, w, 0, 512) == 0,
              "natural chains: a plane before the quad draws nothing");
    }
    rd_destroy_vu_mesh(m);
}

/* ----------------------------------------------------------- the setup */

/* ---- the box culls' rules in RegistPacket.c
 *
 *   packet   a packet whose own box gsb_ClipBox drops, in a part
 *            that passed: drawn with the clipping program (code 36), not
 *            dropped (the PS2 dropped it)
 *   notes    (F12's cull line) the object's box dropped (rule 2, part -1)
 *            and a part's box dropped (rule 3, part 0) noted on the frame
 *            with the model's name (rd_note_cull_drop)
 *   shadow   an accessory (reg_DispAccessoryWithShadow) whose box
 *            is dropped still registers its shadow volume (the PS2 drew it
 *            only with the accessory) */
static void visRuleChecks(void)
{
    /* packet */
    s_clipOnly = packetA()->box[0];
    s_clipOnlyRet = 0;
    recordPrelit(&s_objA, 1);
    s_clipOnly = NULL;
    checkPrelitRecording(36, RD_VU_CLIP_SCISSOR);

    /* notes: the part's box */
    s_clipOnly = (const float *)s_modelA.boxes;
    s_clipOnlyRet = 0;
    recordPrelit(&s_objA, 1);
    s_clipOnly = NULL;
    const RdFrame *f = rd__last_frame();
    Found fd;
    walkFrame(f, &fd);
    CHECK(fd.n == 0 && f->cullDropCount == 1 && f->cullDrops[0].rule == 3 &&
              f->cullDrops[0].part == 0 && strcmp(f->cullDrops[0].model, "test_prelit") == 0,
          "a part's box dropped: no draw, noted as rule 3 part 0 (%d draws, %u notes, rule %u "
          "part %d %s)",
          fd.n, f->cullDropCount, f->cullDropCount ? f->cullDrops[0].rule : 0,
          f->cullDropCount ? f->cullDrops[0].part : 0,
          f->cullDropCount ? f->cullDrops[0].model : "");
    /* with one camera, why (the stub cull's: the camera standing still),
       and F12's line says it before the list; three cameras print "/5" */
    CHECK(f->cullDropCount == 1 && f->cullDrops[0].views == 1 &&
              f->cullDrops[0].oneWhy == GSB_CULL_ONE_STILL,
          "a drop with one camera notes why (views %u, why %u)",
          f->cullDropCount ? f->cullDrops[0].views : 0,
          f->cullDropCount ? f->cullDrops[0].oneWhy : 0);
    {
        static RdFrame line3;
        char line[512];
        rd__cull_drop_line(f, line, sizeof(line));
        CHECK(strstr(line, "dropped 1 (with one camera: 1 the camera did not move): "
                           "test_prelit:0 rule 3") != NULL,
              "F12's cull line with one camera: %s", line);
        line3.number = 7;
        line3.cullDropCount = 2;
        snprintf(line3.cullDrops[0].model, sizeof(line3.cullDrops[0].model), "st18a_p1");
        line3.cullDrops[0].part = 3;
        line3.cullDrops[0].rule = 3;
        line3.cullDrops[0].views = 3;
        snprintf(line3.cullDrops[1].model, sizeof(line3.cullDrops[1].model), "isisofa");
        line3.cullDrops[1].part = -1;
        line3.cullDrops[1].rule = 2;
        line3.cullDrops[1].views = 1;
        line3.cullDrops[1].oneWhy = GSB_CULL_ONE_LOCKED;
        rd__cull_drop_line(&line3, line, sizeof(line));
        CHECK(strcmp(line, "frame 7: the box cull dropped 2 (with one camera: 1 a part fixed to "
                           "the camera): st18a_p1:3 rule 3/5 (3 cameras) isisofa:-1 rule 2") == 0,
              "F12's cull line with three cameras: %s", line);
        line3.cullDropCount = 0;
        rd__cull_drop_line(&line3, line, sizeof(line));
        CHECK(strcmp(line, "frame 7: the box cull dropped none") == 0, "F12's cull line: %s", line);
        /* a list past the buffer keeps the reasons and ends in it */
        rd__cull_drop_line(f, line, 40);
        CHECK(strlen(line) == 39, "F12's cull line cut at the buffer (%zu)", strlen(line));
    }

    /* shadow, and the object's box noted */
    static PObjModel shadowModel;
    s_objA.shadow = &shadowModel;
    s_shadowVolumes = 0;
    s_clipRet = 0;
    dl_Clear();
    setCommon();
    dl_SetDLPriority(0);
    reg_DispAccessoryWithShadow(&s_objA, &s_objB);
    dl_Swap();
    s_clipRet = 1;
    s_objA.shadow = NULL;
    f = rd__last_frame();
    walkFrame(f, &fd);
    CHECK(fd.n == 0 && s_shadowVolumes == 1,
          "an accessory outside the picture: no draw, its shadow volume registered (%d draws, "
          "%d volumes)",
          fd.n, s_shadowVolumes);
    CHECK(f->cullDropCount == 1 && f->cullDrops[0].rule == 2 && f->cullDrops[0].part == -1 &&
              strcmp(f->cullDrops[0].model, "test_prelit") == 0,
          "the object's box dropped noted as rule 2 part -1 (%u notes)", f->cullDropCount);
}

/* ---- the object and part boxes, with the disc: every model's boxes hold
 * the vertices its strips draw.
 *
 * Every *.p2o member of every pack goes through PObj.c's own loader
 * (AllocPObj, the relocation and MakeBoundingBox, its image copied into the
 * EE word arena as the game's heap has it): for each part, each vertex a
 * strip record names (a head with the count, then a record per vertex with
 * its index at +4: the display strips and the shadow silhouettes alike) is
 * one of the part's vertices and lies inside the part's box (model->boxes,
 * the part cull's, RegistPacket.c) and the whole object's (model->box, the
 * object cull's).  Then one model through InitPObj with a model-table
 * offset (the one the PAL table has, shinkan_a's y 105): the object's box
 * moves with the vertices (the PS2 left it where it was, PObj.c). */
PObjMdl modelData[2];

PObjModel *AllocPObj(void *buf, char *name, int id);
PObjModel *InitPObj(void *buf, char *name, int id);
void PObj_FreeImageTables(void);

#define BOX_IMAGE_AT (1u << 20) /* the image's place in the arena */

static int inBox(const float (*b)[4], const float *v)
{
    float lo[3] = {b[0][0], b[0][1], b[0][2]}, hi[3] = {b[0][0], b[0][1], b[0][2]};
    for (int i = 1; i < 8; i++) {
        for (int k = 0; k < 3; k++) {
            lo[k] = b[i][k] < lo[k] ? b[i][k] : lo[k];
            hi[k] = b[i][k] > hi[k] ? b[i][k] : hi[k];
        }
    }
    return v[0] >= lo[0] && v[0] <= hi[0] && v[1] >= lo[1] && v[1] <= hi[1] && v[2] >= lo[2] &&
           v[2] <= hi[2];
}

/* the model's parts' strips against its boxes: the vertices checked, the
   ones outside (or past the part's list) counted into *bad */
static long boxCheckModel(const PObjModel *m, const unsigned char *lo, const unsigned char *hi,
                          long *bad)
{
    long n = 0;
    for (int j = 0; j < m->partCount; j++) {
        const PObjPart *pt = &m->parts[j];
        const float (*pb)[4] = (const float (*)[4])(const void *)(m->boxes + j * 128);
        for (unsigned int s = 0; s < pt->stripCount; s++) {
            const unsigned char *r = ((unsigned char *const *)pt->strips)[s];
            for (;;) {
                short cnt;
                if (r < lo || r + 16 > hi) {
                    break;
                }
                memcpy(&cnt, r, 2);
                if (cnt <= 0 || r + 16 + (size_t)cnt * 16 > hi) {
                    break;
                }
                for (int k = 0; k < cnt; k++) {
                    short vi;
                    memcpy(&vi, r + 16 + k * 16 + 4, 2);
                    n++;
                    if (vi < 0 || (unsigned int)vi >= pt->vtxCount) {
                        (*bad)++;
                        continue;
                    }
                    const float *v = (const float *)(const void *)(pt->vtx + vi * 16);
                    if (!inBox(pb, v) || !inBox((const float (*)[4])m->box, v)) {
                        if (*bad < 5) {
                            printf("  boxes: %s part %d vertex %d (%g %g %g) outside\n", m->name, j,
                                   vi, (double)v[0], (double)v[1], (double)v[2]);
                        }
                        (*bad)++;
                    }
                }
                r += 16 + (size_t)cnt * 16;
            }
        }
    }
    return n;
}

#define BOX_SLICES 4

/* the disc's models whose number among the .p2o members is slice (mod BOX_SLICES): the
   slices together are every model, and each is a case of its own (ico_shard.h) */
static void boxDiscChecks(const char *disc, int slice)
{
    IcoVfs *vfs = disc ? ico_vfs_mount(&ico_vfs_iso9660, disc) : NULL;
    if (!vfs) {
        printf("  boxes: SKIP the disc's models (no disc image)\n");
        return;
    }
    IcoDfMember m;
    ico_df_find_member(vfs, "object/none.p2o", &m); /* builds the index */
    const int members = ico_df_index_members();
    unsigned char *img = ico_arena_base() + BOX_IMAGE_AT;
    const size_t room = sizeof(s_arena) - BOX_IMAGE_AT;
    long models = 0, verts = 0, bad = 0, tooBig = 0;
    int offsetDone = 0;
    int ordinal = 0;
    for (int i = 0; i < members; i++) {
        const char *name = ico_df_member_name(i);
        const size_t len = name ? strlen(name) : 0;
        if (len < 4 || name[len - 4] != '.' || (name[len - 3] | 0x20) != 'p' ||
            name[len - 2] != '2' || (name[len - 1] | 0x20) != 'o' || ico_df_member(i, &m) != 0) {
            continue;
        }
        if (ordinal++ % BOX_SLICES != slice) {
            continue;
        }
        if (m.size > room) {
            tooBig++;
            continue;
        }
        if (ico_df_read_member(vfs, &m, img) != 0) {
            CHECK(0, "boxes: %s from the disc", name);
            continue;
        }
        /* the model's name as the game's names fit PObjModel's 32 bytes:
           the file's, without its folders */
        char nm[32];
        const char *base = strrchr(name, '/');
        snprintf(nm, sizeof(nm), "%s", base ? base + 1 : name);
        const size_t heapAt = s_heapAt;
        if (!offsetDone) {
            /* the offset: shinkan_a's, through InitPObj; table kind 4
               builds no packets (PObj.c MakePacket) */
            memset(modelData, 0, sizeof(modelData));
            modelData[1].offset[1] = 105.0f;
            modelData[1].pktKind = 4;
            PObjModel *pm = InitPObj(img, nm, 1);
            long ob = 0;
            const long on = boxCheckModel(pm, img, img + m.size, &ob);
            CHECK(on > 0 && ob == 0,
                  "boxes: %s with a y offset of 105: %ld of %ld vertices outside its boxes", name,
                  ob, on);
            PObj_FreeImageTables();
            s_heapAt = heapAt;
            offsetDone = 1;
            if (ico_df_read_member(vfs, &m, img) != 0) {
                continue;
            }
        }
        PObjModel *pm = AllocPObj(img, nm, 0);
        verts += boxCheckModel(pm, img, img + m.size, &bad);
        models++;
        PObj_FreeImageTables();
        s_heapAt = heapAt;
    }
    ico_vfs_unmount(vfs);
    printf("  boxes: %ld models, %ld strip vertices, %ld outside their boxes, %ld too big for "
           "the arena\n",
           models, verts, bad, tooBig);
    CHECK(models > 100 / BOX_SLICES && verts > 0 && bad == 0 && tooBig == 0,
          "boxes: every strip vertex of the disc's %ld models inside its part's and its object's "
          "box (%ld outside, %ld models not loaded)",
          models, bad, tooBig);
}

static void setup(void)
{
    gif_HostSetTex0Resolver(texResolve);
    makeTexture();
}

static void buildModels(void)
{
    s_rng = 1234567u;
    buildScene();
    makeModel(&s_modelA, &s_objA, 0, 1);
    makeModel(&s_modelB, &s_objB, 1, 1);
    p2o_MakePacket(&s_objA);
    p2o_MakePacket(&s_objB);
    makeGrid();
    makeParticles();
}

/* ------------------------------------------------- joined strips */
/* rd_mesh.c markJoinedStrips: a strip that shares a corner
 * position with another strip of its mesh, not its double-sided copy,
 * carries ICO_VU_MARK_JOINED on every triangle (RdMeshRec.kickMark; the
 * region rule draws it as a wall outside the out-of-the-box state, never as
 * a card); a free-standing strip, or one whose only neighbour is its
 * double-sided copy, does not. */

/* a prelit mesh of one batch: n corners (x, y) at z 0, a strip starting at
   each corner with start[k] set */
static RdMesh joinMesh(const float (*xy)[2], const uint8_t *start, int n)
{
    float qw[1 + 16 * 3][4];
    memset(qw, 0, sizeof(qw));
    const uint32_t tag = 0x8000u | (uint32_t)n;
    memcpy(&qw[0][0], &tag, 4);
    for (int k = 0; k < n; k++) {
        qw[1 + k * 3][0] = xy[k][0];
        qw[1 + k * 3][1] = xy[k][1];
        qw[1 + k * 3][3] = 1.0f;
        qw[2 + k * 3][2] = 1.0f;
        qw[2 + k * 3][3] = start[k] ? 0.0f : 1.0f;
        qw[3 + k * 3][0] = qw[3 + k * 3][1] = qw[3 + k * 3][2] = 100.0f;
        qw[3 + k * 3][3] = 127.0f;
    }
    const RdVuBatchDesc bd = {0, 0, 0};
    RdVuMeshDesc md;
    memset(&md, 0, sizeof(md));
    md.qw = (const float (*)[4])qw;
    md.qwCount = (uint32_t)(1 + n * 3);
    md.qwPerVertex = RD_VU_QW_PRELIT;
    md.batchCount = 1;
    md.batches = &bd;
    md.materialCount = 1;
    md.debugName = "joined";
    const RdMesh m = rd_create_vu_mesh(&md);
    CHECK(m.id != 0, "joined strips: rd_create_vu_mesh");
    return m;
}

/* the joined mark of the triangle that kick ends in m's list: 1 marked,
   0 not, -1 no such triangle (or index values that are not dense) */
static int joinedAt(const RdMeshRec *m, uint32_t kick)
{
    for (uint32_t i = 0; i + 2 < m->indexCount; i += 3) {
        if (m->index[i] / 4u == kick) {
            const int dense = m->index[i] < 4u * m->vertexCount && m->index[i + 1] / 4u == kick &&
                              m->index[i + 2] / 4u == kick;
            return dense ? (rd__mesh_kick_mark(m, kick) & ICO_VU_MARK_JOINED) != 0 : -1;
        }
    }
    return -1;
}

static void joinedStripChecks(void)
{
    /* a quad (kicks 2, 3) and a triangle (kick 6) sharing the corner
     * (1, 1); then a free-standing quad (kicks 9, 10) */
    static const float kWall[11][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}, {1, 1}, {2, 1},
                                       {1, 2}, {5, 5}, {6, 5}, {5, 6}, {6, 6}};
    static const uint8_t kWallStart[11] = {1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0};
    const RdMesh wall = joinMesh(kWall, kWallStart, 11);
    const RdMeshRec *w = rd__mesh_rec(wall.id);
    CHECK(w && joinedAt(w, 2) == 1 && joinedAt(w, 3) == 1 && joinedAt(w, 6) == 1 &&
              joinedAt(w, 9) == 0 && joinedAt(w, 10) == 0,
          "joined strips: the quad and the triangle sharing a corner are joined, the "
          "free-standing quad is not (%d %d %d %d %d)",
          w ? joinedAt(w, 2) : -2, w ? joinedAt(w, 3) : -2, w ? joinedAt(w, 6) : -2,
          w ? joinedAt(w, 9) : -2, w ? joinedAt(w, 10) : -2);
    rd_destroy_vu_mesh(wall);

    /* a double-sided card: the same four corners twice, the second strip
     * turned (its back face): not joined (its back triangles carry
     * ICO_VU_MARK_LATER) */
    static const float kCard[8][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1},
                                      {1, 0}, {0, 0}, {1, 1}, {0, 1}};
    static const uint8_t kCardStart[8] = {1, 0, 0, 0, 1, 0, 0, 0};
    const RdMesh card = joinMesh(kCard, kCardStart, 8);
    const RdMeshRec *c = rd__mesh_rec(card.id);
    CHECK(c && joinedAt(c, 2) == 0 && joinedAt(c, 3) == 0 && joinedAt(c, 6) == 0 &&
              joinedAt(c, 7) == 0,
          "joined strips: a double-sided card is not joined");
    rd_destroy_vu_mesh(card);

    /* the same card with a third strip on one of its corners: all three
     * joined */
    static const float kCard3[11][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 0},
                                        {1, 1}, {0, 1}, {1, 1}, {2, 1}, {1, 2}};
    static const uint8_t kCard3Start[11] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0};
    const RdMesh card3 = joinMesh(kCard3, kCard3Start, 11);
    const RdMeshRec *c3 = rd__mesh_rec(card3.id);
    CHECK(c3 && joinedAt(c3, 2) == 1 && joinedAt(c3, 7) == 1 && joinedAt(c3, 10) == 1,
          "joined strips: a double-sided card touching a third strip is joined");
    rd_destroy_vu_mesh(card3);

    /* rd__mesh_join_part: the strips of a part's meshes
     * joined over all of them.  st18a_p1 part 9's wall triangle above the
     * doorway (a strip of one triangle, its own material: a packet, a mesh
     * of its own) shares its diagonal with part 6's wall and its top edge
     * with part 9's top face, another material: alone in its mesh it is
     * free-standing, a card the region rule left out of the picture while
     * a corner was above the GS window.  The y axis here is the wall's Z,
     * x its Y (-775 the top, 80 the lintel), as on the disc. */
    static const float kSlit[3][2] = {{-775, -3000}, {80, -2800}, {-775, -2800}};
    static const uint8_t kSlitStart[3] = {1, 0, 0};
    static const float kTop[4][2] = {{-775, -3000}, {-775, -3100}, {-775, -2800}, {-775, -2700}};
    static const uint8_t kTopStart[4] = {1, 0, 0, 0};
    static const float kFar[3][2] = {{500, 500}, {600, 500}, {500, 600}};
    static const uint8_t kFarStart[3] = {1, 0, 0};
    const RdMesh slit = joinMesh(kSlit, kSlitStart, 3);
    const RdMesh top = joinMesh(kTop, kTopStart, 4);
    const RdMesh farOff = joinMesh(kFar, kFarStart, 3);
    const RdMesh copy = joinMesh(kSlit, kSlitStart, 3);
    RdMeshRec *ms = rd__mesh_rec(slit.id), *mt = rd__mesh_rec(top.id),
              *mf = rd__mesh_rec(farOff.id);
    RdMeshRec *mc = rd__mesh_rec(copy.id);
    CHECK(ms && mt && mf && mc && joinedAt(ms, 2) == 0 && joinedAt(mf, 2) == 0,
          "joined strips: the wall triangle alone in its mesh is free-standing");
    if (ms && mt && mf && mc) {
        /* its own double-sided copy in another mesh joins nothing */
        RdMeshRec *pair[2] = {ms, mc};
        CHECK(rd__mesh_join_part(pair, 2, 11) == 0 && joinedAt(ms, 2) == 0 && joinedAt(mc, 2) == 0,
              "joined strips: a strip and its copy in another mesh of the part are not joined");
        /* another part's mesh far off: still a card */
        RdMeshRec *apart[2] = {ms, mf};
        CHECK(rd__mesh_join_part(apart, 2, 12) == 0 && joinedAt(ms, 2) == 0 && joinedAt(mf, 2) == 0,
              "joined strips: a strip sharing nothing with the part's other mesh stays free");
        /* the part's top face: both joined, the device copy refreshed */
        ms->gpuChunk = 1; /* as if uploaded (no device here: the mark only) */
        ms->gpuDirty = 0;
        RdMeshRec *part[3] = {ms, mt, mf};
        CHECK(rd__mesh_join_part(part, 3, 13) == 1 && joinedAt(ms, 2) == 1 &&
                  joinedAt(mt, 2) == 1 && joinedAt(mt, 3) == 1 && joinedAt(mf, 2) == 0 &&
                  ms->gpuDirty == 1,
              "joined strips: the wall triangle and the top face sharing an edge across the "
              "part's meshes are joined, the part's far strip is not (%d %d %d, dirty %d)",
              joinedAt(ms, 2), joinedAt(mt, 2), joinedAt(mf, 2), ms->gpuDirty);
        ms->gpuChunk = 0;
        ms->gpuDirty = 0;
        /* the same meshes again: nothing to do */
        CHECK(rd__mesh_join_part(part, 3, 13) == 0 && ms->partJoinSig == 13 &&
                  mf->partJoinSig == 13,
              "joined strips: a part joined under the same meshes is not joined again");
    }
    rd_destroy_vu_mesh(slit);
    rd_destroy_vu_mesh(top);
    rd_destroy_vu_mesh(farOff);
    rd_destroy_vu_mesh(copy);
}

static void recordingChecks(void)
{
    CHECK(packetA() && packetA()->next == NULL, "one packet for the prelit model");
    CHECK(packetB() && packetB()->next == NULL, "one packet for the cluster model");
    if (!packetA() || !packetB()) {
        return;
    }
    checkMesh(packetA(), RD_VU_QW_PRELIT);
    checkMesh(packetB(), RD_VU_QW_SKIN);

    recordPrelit(&s_objA, 1);
    checkPrelitRecording(32, RD_VU_CLIP_REGION);
    recScissor();
    checkPrelitRecording(36, RD_VU_CLIP_SCISSOR);
    recordCluster();
    checkClusterRecording();
    recordGrid();
    checkGridRecording();
    recordParticles();
    checkParticleRecording();
    checkStretchRecording();
    closingPlaneChecks();
    sheetTableChecks();
    sheetDumpChecks();
    chainTableChecks();
    chainDrawChecks();
    stripTableChecks();
    stripPacketChecks();
    modelPackChecks();
    visRuleChecks();
    joinedStripChecks();
}

/* the cases in the order main() runs them (ico_shard.h); the weights are seconds of a whole
   run */
static const IcoShardCase k_cases[] = {
    {"recording", 1},        {"strip_disc", 1},  {"box_disc_0", 14},      {"box_disc_1", 14},
    {"box_disc_2", 15},      {"box_disc_3", 15}, {"gpu_prims", 1},        {"stretch_same", 1},
    {"closing_original", 1}, {"chain", 1},       {"stretch_enhanced", 1}, {"photo_veil", 1},
    {"photo_reel", 1},
};

int main(int argc, char **argv)
{
    if (ico_shard_start(argc, argv, k_cases, ICO_SHARD_COUNT(k_cases))) {
        return ico_shard_exit();
    }
    rd__set_not_implemented_fatal(true); /* a stub command replayed stops the test */
    if (!rd__init_record_only(512, 512)) {
        printf("FAIL rd__init_record_only\n");
        return 1;
    }
    dl_Init();
    setup();
    buildModels();
    if (ico_shard_take("recording")) {
        recordingChecks();
    }
    if (ico_shard_take("strip_disc")) {
        stripDiscChecks(argc > 1 ? argv[1] : NULL);
    }
    static const char *const boxCases[BOX_SLICES] = {"box_disc_0", "box_disc_1", "box_disc_2",
                                                     "box_disc_3"};
    for (int q = 0; q < BOX_SLICES; q++) {
        if (ico_shard_take(boxCases[q])) {
            boxDiscChecks(argc > 1 ? argv[1] : NULL, q);
        }
    }
    rd_shutdown();
    if (failures) {
        printf("rd_mesh_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_mesh_test: recording ok\n");

    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ORIGINAL;
    if (!rd_init(512, 512, &st, NULL)) {
        printf("rd_mesh_test: SKIP the pixel checks: no usable Vulkan device\n");
        return 77;
    }
    gif_HostForgetTextures();
    gif_HostFrameReset();
    setup();
    buildScene();
    if (ico_shard_take("gpu_prims")) {
        gpuCase("prelit 32", recPrelit, refPrelit32, RD_PRIM_TRIANGLES, 2000);
        gpuCase("prelit 36 (scissor)", recScissor, refPrelit36, RD_PRIM_TRIANGLES, 2000);
        gpuCase("cluster 20", recordCluster, refClusterA, RD_PRIM_TRIANGLES, 2000);
        gpuCase("grid 20", recordGrid, refGrid, RD_PRIM_TRIANGLES, 2000);
        gpuCase("particle 18", recordParticles, refParticles, RD_PRIM_SPRITES, 200);
    }
    if (ico_shard_take("stretch_same")) {
        stretchSamePixels();
    }

    /* every pipeline created is in the enumerated reachable set */
    static RdPipeKeyInt keys[RD_PIPELINE_REACHABLE_MAX];
    const uint32_t n = rd__enumerate_reachable(keys, RD_PIPELINE_REACHABLE_MAX);
    const uint32_t ns = rd__enumerate_reachable_screen(keys, RD_PIPELINE_REACHABLE_MAX);
    rd__enumerate_reachable(keys, RD_PIPELINE_REACHABLE_MAX);
    printf("  pipelines: %u created, %u reachable (%u screen and post)\n", rd__pipeline_count(), n,
           ns);
    CHECK(n < RD_PIPELINE_REACHABLE_MAX && ns < 400, "reachable pipelines %u (screen %u)", n, ns);
    for (uint32_t i = 0; i < rd__pipeline_count(); i++) {
        const RdPipeKeyInt *k = rd__pipeline_key_at(i);
        int found = 0;
        for (uint32_t j = 0; j < n && j < RD_PIPELINE_REACHABLE_MAX; j++) {
            found |= rd__pipe_key_equal(&keys[j], k);
        }
        CHECK(found, "created pipeline %u (prog %u vs %u blend %u z %u/%u) is not enumerated", i,
              k->gs.program, k->vs, k->gs.blend, k->gs.ztst, k->gs.zwrite);
    }
    if (ico_shard_take("closing_original")) {
        closingPixels(0);
        sheetPixels(0);
    }
    if (ico_shard_take("chain")) {
        chainPixels();
    }
    CHECK(rhi_vk_validation_error_count() == 0, "%u validation errors",
          rhi_vk_validation_error_count());
    CHECK(rd__not_implemented_count() == 0, "no stubbed command replayed");
    rd_shutdown();

    /* the reference for the 16:9 run: the same preset at 4:3 */
    st.preset = RD_PRESET_ENHANCED;
    st.aspect = 4.0f / 3.0f;
    st.sceneScale = 1.0f;
    s_sceneW = 512;
    /* the 16:9 run compares with the 4:3 run's picture: one case */
    const int stretchCase = ico_shard_take("stretch_enhanced");
    if (!stretchCase) {
        /* another shard's */
    } else if (!rd_init(512, 512, &st, NULL)) {
        CHECK(0, "rd_init at 4:3 Enhanced");
    } else {
        gif_HostForgetTextures();
        gif_HostFrameReset();
        setup();
        buildScene();
        stretch43Enhanced();
        sheetPixels(0); /* Enhanced at 4:3: the tiles drawn */
        rd_shutdown();
    }

    /* a full-screen title model at 16:9, on the native frame (688 wide) */
    st.preset = RD_PRESET_ENHANCED;
    st.aspect = 16.0f / 9.0f;
    st.sceneScale = 1.0f;
    s_sceneW = WT;
    if (!stretchCase) {
        /* another shard's */
    } else if (!rd_init(WT, 512, &st, NULL)) {
        CHECK(0, "rd_init at 16:9");
    } else {
        gif_HostForgetTextures();
        gif_HostFrameReset();
        setup();
        buildScene();
        stretchWidePixels();
        closingPixels(1);
        sheetPixels(1);
        CHECK(rhi_vk_validation_error_count() == 0, "16:9: %u validation errors",
              rhi_vk_validation_error_count());
        rd_shutdown();
    }
    /* photo mode's Deja vu veil on the widest frame, 60:9 (2560 wide) */
    st.preset = RD_PRESET_ENHANCED;
    st.aspect = 60.0f / 9.0f;
    st.sceneScale = 1.0f;
    s_sceneW = VEIL_W;
    if (!ico_shard_take("photo_veil")) {
        /* another shard's */
    } else if (!rd_init(VEIL_W, 512, &st, NULL)) {
        CHECK(0, "rd_init at 60:9");
    } else {
        gif_HostForgetTextures();
        gif_HostFrameReset();
        setup();
        photoVeilPixels();
        CHECK(rhi_vk_validation_error_count() == 0, "60:9: %u validation errors",
              rhi_vk_validation_error_count());
        rd_shutdown();
    }
    /* photo mode's Deja vu film reel on the 16:9 frame (688 wide) */
    st.preset = RD_PRESET_ENHANCED;
    st.aspect = 16.0f / 9.0f;
    st.sceneScale = 1.0f;
    s_sceneW = WT;
    if (!ico_shard_take("photo_reel")) {
        /* another shard's */
    } else if (!rd_init(WT, 512, &st, NULL)) {
        CHECK(0, "rd_init at 16:9 (the reel)");
    } else {
        gif_HostForgetTextures();
        gif_HostFrameReset();
        setup();
        photoReelPixels();
        CHECK(rhi_vk_validation_error_count() == 0, "16:9 reel: %u validation errors",
              rhi_vk_validation_error_count());
        rd_shutdown();
    }
    if (ico_shard_finish() != 0) {
        return 1;
    }
    if (failures) {
        printf("rd_mesh_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_mesh_test: ok\n");
    return 0;
}

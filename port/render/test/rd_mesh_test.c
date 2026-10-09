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
 *            batch keyed by its emitter
 *   stretch  the prelit model named a full-screen title model
 *            (title_logo.c ico_title_stretch_model, stubbed): reg_DispObj
 *            records its draw with the stretch byte (b[3] 1, 0 for any
 *            other model) and ends the space override; on the device at
 *            4:3 its picture is byte-identical to the unflagged draw's, at
 *            16:9 it is drawn 4/3 wider than the unflagged (scene) draw
 *            about the centre, and three times as wide it covers both edge
 *            columns of the wide target
 *   strip order  the title logo's strips Packet.c draws in another order of
 *            their entries (pac_HostStrips): the table, the I's through
 *            p2o_MakePacket, and with the disc image given as the argument
 *            their geometry (below, "strip order"); recording only
 *   model packs  the mesh identity, replacements from a
 *            tagless stream, one drawn in the prelit packet's place,
 *            rd_VuMeshRetire and the sweep; recording only
 *   pack hooks the game's side of model packs on a pack in
 *            rd_mesh_modelpack/ under the working folder: the dump at the
 *            draw (both parts, the cluster one with its skeleton), the
 *            dumps moved into replacements/ and the pack switched off and
 *            on: the draws record the replacements Packet.c made (the
 *            cluster one with the object's bone count); the morph path
 *            (pac_HostRefreshFor) declines a replaced mesh and draws the
 *            original again; recording only
 *
 * Checks on the recording (no device needed): the mesh built from the
 * packet (vertex count, batches, the index list against vu1ref_StaticKicks);
 * the recorded commands (program, code, clip); the VuCB against what the EE
 * packet code uploads (the common block, the UV offset, the matrices +0x140,
 * +0x200 x node and +0x80 x node, the cluster bones and lights); the GS state
 * at the draw (the material packet's ALPHA, CLAMP and FBA, the texture the
 * TEX0 bound, PRIM.ABE from the batch tag); nothing undecoded.
 * Then on a Vulkan device (exit 77 without one, after the recording checks
 * passed): each case rendered into SCENE, and the CPU reference's triangles
 * (vu1_ref.h, the same VU state built independently) drawn through the
 * sprite path (rd_ScreenPrims) in the same GS state; the two images agree
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
    const RdMeshRec *r = rd__MeshRec(m.id);
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
    /* the static kicks: vu1ref_StaticKicks over the stream */
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
    int nk = vu1ref_StaticKicks(stw, first, v, kicks);
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

static void recordPrelit(Sub15C *o, int clipRet)
{
    static const uint8_t grey[4] = {40, 40, 60, 0x80};
    s_clipRet = clipRet;
    dl_Clear();
    setCommon();
    dl_SetDLPriority(0);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), grey, 1, 0);
    reg_DispObj(o);
    dl_Swap();
}

static void checkPrelitRecording(int code, int clip)
{
    const RdFrame *f = rd__LastFrame();
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
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), grey, 1, 0);
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
    const RdFrame *f = rd__LastFrame();
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
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), grey, 1, 0);
    _SetCurrentMatrix(matrixptr + 0x100);
    prim_DispMesh3D(s_grid, 0, 0, 0);
    dl_Swap();
}

static void checkGridRecording(void)
{
    const RdFrame *f = rd__LastFrame();
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
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), grey, 1, 0);
    prim_DispParticle(s_part, matrixptr + 0x100);
    dl_Swap();
}

static void checkParticleRecording(void)
{
    const RdFrame *f = rd__LastFrame();
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
    const RdKey k = RD_KEY(s_part, 18, 0);
    CHECK(fd.cmd[0]->keyLo == (uint32_t)k && fd.cmd[0]->keyHi == (uint32_t)(k >> 32),
          "the batch is keyed by its emitter (%08x%08x)", fd.cmd[0]->keyHi, fd.cmd[0]->keyLo);
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
    int nk = vu1ref_Kicks(out, kicks);
    for (int i = 0; i < nk; i++) {
        for (int j = 0; j < 3; j++) {
            refVertex(&out->v[kicks[i] - 2 + j]);
        }
    }
}

static void refCommon(Vu1Ref *r)
{
    vu1ref_Init(r);
    vu1ref_LoadCommon(r, (const float (*)[4])s_common);
    vu1ref_SetUVOffset(r, kUv);
}

static void refPrelit(const PacHeader *pk, int code)
{
    static Vu1Ref r;
    float m[12][4];
    refCommon(&r);
    normalMatrices(&s_modelA, m);
    vu1ref_NormalSetMatrix(&r, (const float (*)[4])m);
    Batches b;
    packetBatches(pk, &b);
    s_nref = 0;
    static VuBatchOut out;
    for (int i = 0; i < b.n; i++) {
        vu1ref_NormalC(&r, code, b.in[i], &out);
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
    vu1ref_ClusterSetMatrix(&r, (const float (*)[4])pk2);
    vu1ref_ClusterSetLight(&r, (const float (*)[4])light);
    Batches b;
    packetBatches(pk, &b);
    s_nref = 0;
    static VuBatchOut out;
    for (int i = 0; i < b.n; i++) {
        vu1ref_Cluster(&r, 20, b.in[i], &out);
        refBatch(&out);
    }
}

static void refGrid(void)
{
    static Vu1Ref r;
    refCommon(&r);
    vu1ref_MeshSetMatrix(&r, (const float (*)[4])(matrixptr + 0x100));
    s_nref = 0;
    static VuBatchOut out;
    const float (*buf)[4] = s_grid->bufs[0];
    const int per = s_grid->stripLen * 2 + 4;
    for (int i = 0; i < s_grid->strips; i++) {
        vu1ref_Mesh(&r, 20, buf + i * per + 1, &out);
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
    vu1ref_ParticleSetMatrix(&r, (const float (*)[4])m);
    VuParticleOut out;
    vu1ref_Particle(&r, (const float (*)[4])(const void *)s_part->objs[0]->num, 6 + 2 * NPART,
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
    rd_Test(&s->ds.test);
    rd_ZWrite(s->ds.zwrite == RD_ZWRITE_ON);
    rd_FBA(s->ds.fba);
    rd_PABE(s->ds.pabe);
    rd_ColClamp(s->ds.colclamp);
    rd_TexA((RdTexA)s->ds.texa);
    rd_Blend((RdBlend)s->ds.blend, s->ds.blendFix, s->ds.abe);
    rd_SamplerFilter((RdFilter)s->ds.magFilter, (RdFilter)s->ds.minFilter);
    rd_SamplerWrap((RdWrap)s->ds.wrap.s, (RdWrap)s->ds.wrap.t);
    if (s->ds.texEnabled) {
        rd_Texture((RdTex){s->tex}, (RdTexFn)s->ds.texFn, (RdTcc)s->ds.tcc);
    } else {
        rd_TextureOff();
    }
    rd_Gouraud((int)s->gouraud);
    rd_ColorMask(s->ds.fbmsk);
    rd_UVOffset(0.0f, 0.0f);
}

static uint8_t s_imgA[512 * 512 * 4], s_imgB[512 * 512 * 4];

/* Frame 2: the reference triangles (or sprites) in the state the mesh draw
 * had, over the same clear. */
static void drawReference(const RdStateBlock *st, int list, RdPrim prim)
{
    static const uint8_t grey[4] = {40, 40, 60, 0x80};
    dl_Clear();
    dl_SetDLPriority(list);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), grey, 1, 0);
    applyState(st);
    rd_ScreenPrims(prim, s_ref, (uint32_t)s_nref, RD_SPACE_WORLD, 0, 0);
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
    walkFrame(rd__LastFrame(), &fd);
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
    Found fd;
    s_stretchModel = NULL;
    recordPrelit(&s_objA, 1);
    walkFrame(rd__LastFrame(), &fd);
    CHECK(fd.n == 1 && fd.cmd[0]->b[3] == 0, "stretch: an ordinary model's draw is not flagged");
    s_stretchModel = "test_prelit";
    recordPrelit(&s_objA, 1);
    walkFrame(rd__LastFrame(), &fd);
    CHECK(fd.n == 1 && fd.cmd[0]->b[3] == 1, "stretch: the title model's draw is flagged");
    CHECK(rd_SetSpaceOverride(-1) == -1, "stretch: reg_DispObj ends the space override");
    s_stretchModel = NULL;
    recordPrelit(&s_objA, 1);
    walkFrame(rd__LastFrame(), &fd);
    CHECK(fd.n == 1 && fd.cmd[0]->b[3] == 0, "stretch: the next draw is not flagged");
}

/* at 4:3 the flag changes nothing */
static void stretchSamePixels(void)
{
    s_stretchModel = NULL;
    recordPrelit(&s_objA, 1);
    const int a = readScene(s_imgA);
    s_stretchModel = "test_prelit";
    recordPrelit(&s_objA, 1);
    const int b = readScene(s_imgB);
    s_stretchModel = NULL;
    CHECK(a && b, "stretch 4:3: SCENE readback");
    int cover = 0;
    for (int i = 0; i < 512 * 512; i++) {
        cover += s_imgA[i * 4] != 40 || s_imgA[i * 4 + 1] != 40 || s_imgA[i * 4 + 2] != 60;
    }
    CHECK(a && b && cover >= 2000 && memcmp(s_imgA, s_imgB, sizeof(s_imgA)) == 0,
          "stretch 4:3: the flagged draw's picture is the unflagged one's (%d pixels drawn)",
          cover);
}

#define WT 683 /* SCENE's texels across at 16:9, 1x */

static uint8_t s_wideImg[WT * 512 * 4];

/* the first and last columns of SCENE at 16:9 with a pixel drawn */
static int wideColumns(int *x0, int *x1)
{
    uint32_t w = 0, h = 0;
    if (!rd__ReadTarget(rd_Target(RD_TARGET_SCENE), s_wideImg, sizeof(s_wideImg), &w, &h) ||
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

/* at 16:9 the scene is drawn compressed about the centre by 3/4, the
   flagged model is not */
static void stretchWidePixels(void)
{
    CHECK(fabsf(g_rd.wideX - 0.75f) < 1e-6f, "stretch 16:9: the wide factor (%g)",
          (double)g_rd.wideX);
    int a0 = 0, a1 = 0, b0 = 0, b1 = 0;
    s_stretchModel = NULL;
    recordPrelit(&s_objA, 1);
    const int a = wideColumns(&a0, &a1);
    s_stretchModel = "test_prelit";
    recordPrelit(&s_objA, 1);
    const int b = wideColumns(&b0, &b1);
    CHECK(a && b, "stretch 16:9: SCENE readback with pixels drawn");
    if (a && b) {
        const double c = WT / 2.0;
        const double la = c - a0, ra = a1 + 1 - c, lb = c - b0, rb = b1 + 1 - c;
        printf("  stretch 16:9: scene draw columns %d..%d, stretched %d..%d\n", a0, a1, b0, b1);
        CHECK(la > 50.0 && ra > 50.0 && fabs(lb - la * 4.0 / 3.0) <= 2.5 &&
                  fabs(rb - ra * 4.0 / 3.0) <= 2.5,
              "stretch 16:9: the flagged draw spans 4/3 of the scene draw about the centre "
              "(left %.1f vs %.1f, right %.1f vs %.1f)",
              lb, la, rb, ra);
    }
    /* three times as wide (past the 4:3 screen's edges): across the whole
       wide target */
    const float keep = s_modelA.nodeMtx[0][0];
    s_modelA.nodeMtx[0][0] = 3.0f;
    recordPrelit(&s_objA, 1);
    const int w = wideColumns(&b0, &b1);
    s_modelA.nodeMtx[0][0] = keep;
    s_stretchModel = NULL;
    CHECK(w && b0 == 0 && b1 == WT - 1,
          "stretch 16:9: the wide plane covers both edge columns (columns %d..%d of %d)", b0, b1,
          WT);
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
    const RdMeshRec *r = pk ? rd__MeshRec(pac_HostMesh(pk)) : NULL;
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
 * The mesh identity (rd_VuMeshDescHash) on a synthetic prelit packet of
 * two batches (5 and 6 vertices, a strip restart at vertex 3 of the
 * second) against the byte stream rd_mesh.h specifies, and on the game's
 * packets against the hash rd_CreateVuMesh stored; replacements from a
 * tagless stream (counts, indices, PRIM/material/group from the original,
 * the rejections, rd_UpdateVuMesh refusing them); one drawn through
 * reg_DispObj in the original's place; rd_VuMeshRetire and the sweep. */
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
    const uint64_t ha = rd_VuMeshDescHash(&a.d, &nv, &nw);
    CHECK(ha != 0 && ha == rd_VuMeshDescHash(&b.d, NULL, NULL), "two builds hash alike");
    CHECK(ha == synthHashByHand(&a), "the hash is XXH3-64 of rd_mesh.h's byte stream");
    CHECK(nv == 11 && nw == 0.0f, "11 vertices (%u), prelit normal.w 0 (%g)", nv, (double)nw);
    /* one colour byte (batch 1, vertex 2's red) */
    uint8_t *c = (uint8_t *)&b.qw[2 + 5 * RD_VU_QW_PRELIT + 2 * RD_VU_QW_PRELIT + 2][0];
    c[0] ^= 1;
    CHECK(rd_VuMeshDescHash(&b.d, NULL, NULL) != ha, "one colour byte changes the hash");
    /* PRIM, material and group are not in the key */
    synthPacket(&b, 0x1C, 7);
    b.b[1].group = 5;
    CHECK(rd_VuMeshDescHash(&b.d, NULL, NULL) == ha, "PRIM, material and group are not hashed");
    /* malformed: the last batch runs past the stream */
    b.d.qwCount--;
    nv = 99;
    nw = 99.0f;
    CHECK(rd_VuMeshDescHash(&b.d, &nv, &nw) == 0 && nv == 0 && nw == 0.0f,
          "a malformed desc hashes to 0");
    CHECK(rd_VuMeshDescHash(NULL, NULL, NULL) == 0, "no desc hashes to 0");
    /* rd_CreateVuMesh keeps the same hash */
    RdMesh m = rd_CreateVuMesh(&a.d);
    CHECK(m.id && rd_VuMeshHash(m) == ha && !rd_VuMeshReplaced(m), "the record's hash");
    rd_DestroyVuMesh(m);
    /* the game's packets: the desc rebuilt from the packet hashes as the
     * mesh pac_HostMesh built; skinned: normal.w of the first vertex */
    static PkDesc pd;
    packetDesc(packetA(), RD_VU_QW_PRELIT, &pd);
    RdMesh pa = {pac_HostMesh(packetA())};
    CHECK(rd_VuMeshHash(pa) != 0 && rd_VuMeshDescHash(&pd.d, &nv, NULL) == rd_VuMeshHash(pa) &&
              nv == NV,
          "the prelit packet's hash (%u vertices)", nv);
    packetDesc(packetB(), RD_VU_QW_SKIN, &pd);
    RdMesh pb = {pac_HostMesh(packetB())};
    const RdMeshRec *rb = rd__MeshRec(pb.id);
    CHECK(rb && rd_VuMeshDescHash(&pd.d, NULL, &nw) == rd_VuMeshHash(pb) && nw == rb->stream[1][3],
          "the cluster packet's hash, normal.w %g", (double)nw);
    CHECK(rd_VuMeshHash(pa) != rd_VuMeshHash(pb), "two parts, two hashes");
}

static void replacementChecks(void)
{
    static Synth o;
    synthPacket(&o, 0x0C, 3);
    { /* batch 1's tag: PRIM 0x1C */
        uint32_t hi = (1u << 14) | (0x1Cu << 15) | (3u << 28);
        memcpy(&o.qw[o.b[1].firstQw][1], &hi, 4);
    }
    const uint64_t h = rd_VuMeshDescHash(&o.d, NULL, NULL);
    /* the tagless vertices of the synthetic packet, the batches swapped */
    static float flat[11 * RD_VU_QW_PRELIT][4];
    memcpy(flat[0], o.qw[1], 5 * RD_VU_QW_PRELIT * 16);
    memcpy(flat[5 * RD_VU_QW_PRELIT], o.qw[2 + 5 * RD_VU_QW_PRELIT], 6 * RD_VU_QW_PRELIT * 16);
    RdVuReplacementBatch rb[3] = {{5, 6}, {0, 5}, {0, 0}};
    RdVuReplacement rep = {(const float (*)[4])flat, 11, RD_VU_QW_PRELIT, rb, 2};
    RdMesh m = rd_CreateVuMeshReplacement(&o.d, &rep, "synth rep");
    const RdMeshRec *r = rd__MeshRec(m.id);
    CHECK(r && r->vu && r->replaced && r->hash == h && rd_VuMeshReplaced(m) &&
              rd_VuMeshHash(m) == h && rd_VuMeshValid(m),
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
    CHECK(!rd_UpdateVuMesh(m, (const float (*)[4])o.qw), "rd_UpdateVuMesh refuses a replaced mesh");
    CHECK(!r || memcmp(before, r->stream[0], 16) == 0, "and writes nothing");
    RdMesh om = rd_CreateVuMesh(&o.d);
    CHECK(rd_UpdateVuMesh(om, (const float (*)[4])o.qw), "rd_UpdateVuMesh writes an original");
    rd_DestroyVuMesh(om);
    rd_DestroyVuMesh(m);
    /* fewer batches: the rest empty */
    rep.batchCount = 1;
    m = rd_CreateVuMeshReplacement(&o.d, &rep, NULL);
    r = rd__MeshRec(m.id);
    CHECK(r && r->batchCount == 2 && r->batches[1].vertexCount == 0 &&
              r->batches[1].indexCount == 0 && r->indexCount == 6 && r->vertexCount == 6 &&
              strcmp(r->name, "synth") == 0,
          "a missing batch is empty, the name the original's");
    rd_DestroyVuMesh(m);
    /* rejections */
    rep.batchCount = 3;
    CHECK(rd_CreateVuMeshReplacement(&o.d, &rep, NULL).id == 0, "more batches than the original");
    rep.batchCount = 2;
    rep.qwPerVertex = RD_VU_QW_LIT;
    CHECK(rd_CreateVuMeshReplacement(&o.d, &rep, NULL).id == 0, "another layout");
    rep.qwPerVertex = RD_VU_QW_PRELIT;
    rb[1].vertexCount = 12;
    CHECK(rd_CreateVuMeshReplacement(&o.d, &rep, NULL).id == 0, "a run outside the stream");
    rb[1].vertexCount = 5;
    o.d.qwCount--;
    CHECK(rd_CreateVuMeshReplacement(&o.d, &rep, NULL).id == 0, "a malformed original");
    o.d.qwCount++;
    CHECK(rd_CreateVuMeshReplacement(&o.d, NULL, NULL).id == 0, "no replacement");
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
    const RdMeshRec *orc = rd__MeshRec(orig.id);
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
    RdMesh m = rd_CreateVuMeshReplacement(&pd.d, &rep, "test_prelit rep");
    const RdMeshRec *r = rd__MeshRec(m.id);
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
    const RdFrame *f = rd__LastFrame();
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
    rd_VuMeshRetire(retireReplaced, NULL);
    CHECK(s_retireCalls > 0 && !rd_VuMeshValid(m) && rd_VuMeshValid(orig) && rd__MeshRec(m.id),
          "retired: the replacement invalid, kept; the original valid");
    int kept = 1;
    for (int i = 0; i < 4; i++) {
        dl_Clear();
        dl_Swap();
        if (g_rd.frameCounter <= drawn + 3) {
            kept &= rd__MeshRec(m.id) != NULL;
        }
    }
    CHECK(kept && rd__MeshRec(m.id) == NULL, "kept for the retained frames, then freed");
    memcpy(pk->pad9C, saved, sizeof(saved));
    /* retiring the original: pac_HostMesh builds it again */
    rd_VuMeshRetire(retireOriginal, NULL);
    CHECK(!rd_VuMeshValid(orig), "the original retired");
    RdMesh again = {pac_HostMesh(pk)};
    CHECK(again.id != 0 && again.id != orig.id && rd_VuMeshValid(again) &&
              rd_VuMeshHash(again) == s_retireHash,
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
    walkFrame(rd__LastFrame(), &fd);
    return fd.n == 1 && fd.cmd[0]->type == type ? fd.cmd[0]->u[0] : 0;
}

static void packHookChecks(void)
{
    static PkDesc pa, pb;
    packetDesc(packetA(), RD_VU_QW_PRELIT, &pa);
    packetDesc(packetB(), RD_VU_QW_SKIN, &pb);
    const uint64_t ha = rd_VuMeshDescHash(&pa.d, NULL, NULL);
    const uint64_t hb = rd_VuMeshDescHash(&pb.d, NULL, NULL);
    (void)ico_mkdir(s_mpBase);
    (void)ico_mkdir(mpPath("models"));
    (void)ico_mkdir(mpPath("models/SCES-50760"));
    (void)ico_mkdir(mpPath("models/SCES-50760/replacements"));
    mpClear(); /* an earlier run's files */
    ModelpackConfig cfg = {s_mpBase, NULL, "SCES-50760", 0, 1};
    CHECK(modelpack_Init(&cfg) == 0, "an empty pack");

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
    if (gltf_Read(mpPath(rel), &doc, why, sizeof(why)) == 0) {
        CHECK(doc.skin.count == (uint32_t)s_objB.nodeNum &&
                  memcmp(doc.skin.invBind[1], s_modelB.clusterMtx[1], 64) == 0 &&
                  doc.skin.parent[0] < 0,
              "the skeleton from the object: %u bones, clusterMtx as the inverse binds",
              doc.skin.count);
        CHECK(doc.meshName && strcmp(doc.meshName, "test_cluster/part0/0") == 0,
              "the cluster part named by the draw (%s)", doc.meshName ? doc.meshName : "-");
        gltf_Free(&doc);
    } else {
        CHECK(0, "the cluster dump reads: %s", why);
    }
    ModelpackStats st;
    modelpack_GetStats(&st);
    recordPrelit(&s_objA, 1);
    ModelpackStats st2;
    modelpack_GetStats(&st2);
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
    CHECK(modelpack_Init(&cfg) == 2 && modelpack_Lookup(ha) >= 0 && modelpack_Lookup(hb) >= 0,
          "both parts indexed");
    /* the switch off and on: the originals retired, made again at the draw */
    const RdMesh oa = {pac_HostMesh(packetA())}, ob = {pac_HostMesh(packetB())};
    modelpack_SetEnabled(false);
    modelpack_SetEnabled(true);
    CHECK(!rd_VuMeshValid(oa) && !rd_VuMeshValid(ob), "on: the originals retired");
    recordPrelit(&s_objA, 1);
    const RdMesh ra = {mpDrawnMesh(RDC_MESH)};
    CHECK(ra.id != 0 && rd_VuMeshReplaced(ra) && rd_VuMeshHash(ra) == ha &&
              pac_HostMesh(packetA()) == ra.id,
          "the prelit draw records the replacement Packet.c made");
    recordCluster();
    const RdMesh rb = {mpDrawnMesh(RDC_SKINNED)};
    CHECK(rb.id != 0 && rd_VuMeshReplaced(rb) && rd_VuMeshHash(rb) == hb &&
              pac_HostMesh(packetB()) == rb.id,
          "the cluster draw records the replacement (bones from the object)");
    modelpack_GetStats(&st);
    CHECK(st.created == 2 && st.declined == 0, "two replacements made (%u), none declined (%u)",
          st.created, st.declined);

    /* the morph path: a replaced mesh cannot follow it, the original back */
    PacHostIdent id = {"test_prelit", 0, 0, NULL, s_objA.nodeNum, &s_objA};
    pac_HostRefreshFor(packetA(), &id);
    const RdMesh back = {pac_HostMesh(packetA())};
    CHECK(!rd_VuMeshValid(ra) && back.id != 0 && back.id != ra.id && rd_VuMeshValid(back) &&
              !rd_VuMeshReplaced(back) && rd_VuMeshHash(back) == ha,
          "the morph path: the replacement retired, the original built");
    CHECK(modelpack_Lookup(ha) < 0 && modelpack_Lookup(hb) >= 0, "the morphing part declined");
    recordPrelit(&s_objA, 1);
    CHECK(mpDrawnMesh(RDC_MESH) == back.id, "the original drawn again");
    pac_HostRefreshFor(packetA(), &id);
    CHECK(pac_HostMesh(packetA()) == back.id, "an original follows the morph in place");

    /* the pack away: the other tests draw the originals */
    modelpack_SetEnabled(false);
    modelpack_Shutdown();
    CHECK(!rd_VuMeshValid(rb) && modelpack_Lookup(hb) < 0, "the pack switched off and closed");
    mpClear();
}

static void modelPackChecks(void)
{
    hashChecks();
    replacementChecks();
    replacementDrawChecks();
    packHookChecks();
}

/* ----------------------------------------------------------- the setup */

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
    stripTableChecks();
    stripPacketChecks();
    modelPackChecks();
}

int main(int argc, char **argv)
{
    if (!rd__InitRecordOnly(512, 512)) {
        printf("FAIL rd__InitRecordOnly\n");
        return 1;
    }
    dl_Init();
    setup();
    buildModels();
    recordingChecks();
    stripDiscChecks(argc > 1 ? argv[1] : NULL);
    rd_Shutdown();
    if (failures) {
        printf("rd_mesh_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_mesh_test: recording ok\n");

    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ORIGINAL;
    if (!rd_Init(512, 512, &st, NULL)) {
        printf("rd_mesh_test: SKIP the pixel checks: no usable Vulkan device\n");
        return 77;
    }
    gif_HostForgetTextures();
    gif_HostFrameReset();
    setup();
    buildScene();
    gpuCase("prelit 32", recPrelit, refPrelit32, RD_PRIM_TRIANGLES, 2000);
    gpuCase("prelit 36 (scissor)", recScissor, refPrelit36, RD_PRIM_TRIANGLES, 2000);
    gpuCase("cluster 20", recordCluster, refClusterA, RD_PRIM_TRIANGLES, 2000);
    gpuCase("grid 20", recordGrid, refGrid, RD_PRIM_TRIANGLES, 2000);
    gpuCase("particle 18", recordParticles, refParticles, RD_PRIM_SPRITES, 200);
    stretchSamePixels();

    /* every pipeline created is in the enumerated reachable set */
    static RdPipeKeyInt keys[512];
    const uint32_t n = rd__EnumerateReachable(keys, 512);
    const uint32_t ns = rd__EnumerateReachableScreen(keys, 512);
    rd__EnumerateReachable(keys, 512);
    printf("  pipelines: %u created, %u reachable (%u screen and post)\n", rd__PipelineCount(), n,
           ns);
    CHECK(n < RD_PIPELINE_REACHABLE_MAX && ns < 250, "reachable pipelines %u (screen %u)", n, ns);
    for (uint32_t i = 0; i < rd__PipelineCount(); i++) {
        const RdPipeKeyInt *k = rd__PipelineKeyAt(i);
        int found = 0;
        for (uint32_t j = 0; j < n && j < 512; j++) {
            found |= rd__PipeKeyEqual(&keys[j], k);
        }
        CHECK(found, "created pipeline %u (prog %u vs %u blend %u z %u/%u) is not enumerated", i,
              k->gs.program, k->vs, k->gs.blend, k->gs.ztst, k->gs.zwrite);
    }
    CHECK(rhi_vk_ValidationErrorCount() == 0, "%u validation errors",
          rhi_vk_ValidationErrorCount());
    CHECK(rd__NotImplementedCount() == 0, "no stubbed command replayed");
    rd_Shutdown();

    /* a full-screen title model at 16:9 */
    st.preset = RD_PRESET_ENHANCED;
    st.aspect = 16.0f / 9.0f;
    st.sceneScale = 1.0f;
    if (!rd_Init(512, 512, &st, NULL)) {
        CHECK(0, "rd_Init at 16:9");
    } else {
        gif_HostForgetTextures();
        gif_HostFrameReset();
        setup();
        buildScene();
        stretchWidePixels();
        CHECK(rhi_vk_ValidationErrorCount() == 0, "16:9: %u validation errors",
              rhi_vk_ValidationErrorCount());
        rd_Shutdown();
    }
    if (failures) {
        printf("rd_mesh_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_mesh_test: ok\n");
    return 0;
}

/* rd_vuprepass_selftest.c: the VU vertex and triangle pre-passes'
 * self-test (rd.h rd_vuprepass_selftest).
 *
 * The pre-passes (rd_replay_vu.c rd__vu_prepass; docs/port/RENDER.md, "The
 * vertex and triangle pre-passes") draw with pipelines no other draw of the
 * port has: vertex shaders writing PointSize into a point list, flat uint4
 * varyings into three RGBA32_UINT targets (48 bytes a pixel) under a colour
 * mask of one channel (the tick stage), texture-to-buffer copies a vertex
 * shader of the next pass and the main shaders read in the same list, and
 * the record's reads in the pixel shader (vu_ps).  A driver that cannot make
 * one of those pipelines would leave every VU pipeline to be made on first
 * use (the start-up set holds the main shaders' keys, not the programs'),
 * and one that miscompiles them would draw the 3D scenery wrong with no
 * error anywhere.  So the pre-passes are checked on the device itself at
 * start.
 *
 * The frame, built here and never recorded (rd_fog_path.c's helpers), on
 * SCENE at its real size and scale, list 0, Z GEQUAL with Z write, no
 * blend, untextured, Gouraud: three meshes made for it
 * (rd_create_vu_mesh), each a strip of 12 vertices in two runs of 6 (8
 * triangles), side by side in a block of 256 x 96 GS pixels at the frame's
 * centre, the middle one over the other two at other depths:
 *   prelit  normal_c code 32 (RD_PROG_PRELIT, the region test)
 *   lit     normal_l code 32 (RD_PROG_LIT, the lighting)
 *   skin    cluster code 20 (RD_PROG_SKIN, two bones)
 * with vu1_test.c's scene in VU memory (model to GS screen x' = 16x +
 * 2048z, y' = 16y + 2048z, z' = 3 * 2^20 (z - 1), w' = z; its two light
 * matrices); then
 *   ticks   the prelit mesh again, drawn through a camera blended between
 *           two ticks' (RdFrame.tickCams, RD_VU_TICK_CAMERAS): at the
 *           blended camera (w' = z - 2.22, x' = 0.16x + 2048w', y' = 0.16y +
 *           2048w', z' = 4 * 10^6 z - 8.92 * 10^6) one vertex of each
 *           triangle is behind the eye, so the region rule drops it, and
 *           tick 1's camera is the scene's (tick 0's the blended one), where
 *           every triangle is inside: each is drawn clipped through the tick
 *           stage (vu_common.hlsli vu_region_rule), in front of the others;
 *           left out where the fallback is the plain variant (no tick
 *           cameras there: the two would differ by design)
 *   sprites four particles (RD_PROG_PARTICLE) across the block in front.
 * The pipelines of both kinds of replay are made first on the worker
 * threads (two replays that draw nothing name them, rd__pipeline_gather;
 * rd__pipeline_create_gathered).  The frame is built ST_REPLAYS (4) times,
 * replay k's moved: every mesh's strips ST_SHIFT_X model units a replay to
 * the right (2 GS pixels) and the ticks copy's blended camera's x and y
 * offsets (kTickCam[3][0..1]) ST_SHIFT_CAM a replay, so each replay's
 * vertex and triangle records differ from the one before.  The four
 * frames are replayed without the pre-passes (the programs' entries: the
 * references), then the four with them back to back as a run draws its
 * pictures, each replay in the next frame slot (rd__ring_slot: the
 * record's buffer is one a slot, so the four alternate between both), the
 * block copied out after each and read back after the four.  A record a
 * replay did not write, or one read before its copy landed, draws an
 * earlier replay's picture: one replay of one frame would draw the same
 * picture from it and could not see it.  The pre-passes pass when every
 * replay with them drew every command through them (two passes, the draws
 * the frame's commands make: a vertex draw a mesh, two a particle command,
 * the rule and planes stages a mesh and the tick stage's two for the ticks
 * copy, counted one an entry and stage whether or not the pre-pass's
 * batches merged them, rd__vu_prepass_entry_draws; no draw the old way,
 * vuPreFallbacks 0) and each replay's block equals its own reference's
 * byte for byte.
 * A difference, a pipeline that could not be made (its draws then take
 * their programs' entries) or a device error turns the pre-passes off for
 * the run (g_rd.vuPrepass); the window calls this before the start-up set,
 * so the set then holds the programs' keys.  MSAA is held off while it
 * runs, and SCENE is left to start from its clear.  A start whose pre-pass
 * is off (by the settings, the device or the host, rd_set_vu_prepass_off)
 * runs nothing.
 *
 *   rd: VU pre-pass: on (self-test: prelit, lit, skin, the tick cameras and
 *     particles byte for byte over 4 replays in both frame slots, 48
 *     vertices and 4 sprites in 16 pre-pass draws (2 passes, 90 triangle
 *     slots), N of M pixels drawn) on SCENE WxH at TWxTH texels in T ms
 *   (the pre-pass draws as made: 16 one an entry and stage, fewer under the
 *   batches, g_rd.vuPreBatch)
 *   rd: VU pre-pass: off (self-test: the pictures differ: in replay K of 4
 *     (frame slot S), N of M bytes, the first at texel X,Y: R,G,B,A want
 *     R,G,B,A) on SCENE ...
 *
 * ICO_RD_VU_PREPASS_SABOTAGE (tests): the replay with the pre-passes draws
 * wrong, as a driver whose record write or read went wrong would, so the
 * fallback can be run where the pre-passes are right
 * (rd__vu_prepass_sabotage): 1 the first mesh's vertex records one texel
 * off, tri its triangles' slot A one slot off, ticks the tick stage's tick
 * 1 tested at tick 0's camera (the ticks copy is then dropped), each in
 * every replay; stale the record's copies left out from the second replay
 * on (RD_VU_SABOTAGE_STALE: each such replay reads what an earlier replay
 * left in its frame slot's buffer, or nothing), which only the replays
 * after the first can show.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "rd_mesh.h"
#include "rd_vu_skin_check.h"

#define ST_W 256 /* the block, GS pixels */
#define ST_H 96
#define ST_VERTS 12 /* a mesh's vertices: two strips of 6 */

enum { M_PRELIT, M_LIT, M_SKIN, M_COUNT };

#define ST_SPRITES 4 /* the particle command's */

/* the replays with the pre-pass (and as many references), each moved */
#define ST_REPLAYS 4
#define ST_SHIFT_X 0.25f  /* model units a replay: GS X 8 x this (2 pixels) */
#define ST_SHIFT_CAM 0.25 /* the blended camera's x' and y' offsets a replay */

static const uint8_t kProg[M_COUNT] = {RD_PROG_PRELIT, RD_PROG_LIT, RD_PROG_SKIN};
static const uint8_t kCode[M_COUNT] = {32, 32, 20};
static const uint32_t kQpv[M_COUNT] = {RD_VU_QW_PRELIT, RD_VU_QW_LIT, RD_VU_QW_SKIN};
/* each strip's first x and its model depth (the lit one nearest, over the
   others' inner ends) */
static const float kX0[M_COUNT] = {-14.0f, -6.0f, 1.0f};
static const float kZ0[M_COUNT] = {2.2f, 1.9f, 2.1f};

static const uint8_t kClear[4] = {0, 0, 0, 0x80};

static float fbits(uint32_t u)
{
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static void qw(float *d, float x, float y, float z, float w)
{
    d[0] = x;
    d[1] = y;
    d[2] = z;
    d[3] = w;
}

/* The VU memory of a draw (rd_mesh.h RdVuBlock): vu1_test.c buildScene's
 * common block (0..15), the normal programs' model to GS screen, model to
 * clip and model to view matrices (16..27) and the light matrices L1 and
 * L2 (28..35; the cluster's vf13..vf20) */
static void sceneBlock(RdVuBlock *b)
{
    float (*m)[4] = b->mem;
    memset(b, 0, sizeof(*b));
    qw(m[0], 0, 0, 0, 1);
    qw(m[1], 4095, 4095, 0, 16777215);
    qw(m[3], fbits(0x8000u), fbits(0x302EC000u), fbits(0x512u), 0);
    qw(m[4], 16, 0, 0, 0);
    qw(m[5], 0, 16, 0, 0);
    qw(m[6], 2048, 2048, 3145728, 1);
    qw(m[7], 0, 0, -3145728, 0);
    qw(m[8], 1024, 0, 0, 0);
    qw(m[9], 0, 1024, 0, 0);
    qw(m[10], 0, 0, 1048576, 0);
    qw(m[11], 2048, 2048, 1048576, 1);
    for (int i = 0; i < 4; i++) {
        m[12 + i][i] = 1.0f; /* the inverse view */
        m[24 + i][i] = 1.0f; /* model to view */
    }
    memcpy(m[16], m[4], 4 * sizeof(m[0]));
    qw(m[20], 0.015625f, 0, 0, 0);
    qw(m[21], 0, 0.015625f, 0, 0);
    qw(m[22], 0, 0, 2, 1);
    qw(m[23], 0, 0, -3, 0);
    qw(m[28], 0, 1, 0, 0);
    qw(m[29], 0, 0, 1, 0);
    qw(m[30], 1, 0, 0, 0);
    qw(m[31], 0, 0, 0, 1);
    qw(m[32], 0.5f, 0.5f, 0.5f, 0);
    qw(m[33], 0.25f, 0, 0, 0);
    qw(m[34], 0, 0.25f, 0, 0);
    qw(m[35], 0.125f, 0.125f, 0.25f, 0);
}

/* the cluster's bones (VuBoneCB, VU memory 16..): bone 0 at address 16 the
 * identity, bone 1 at 20 a move of 8 along x (vu1_test.c stateCluster) */
static float s_bones[8][4];

static void sceneBones(void)
{
    memset(s_bones, 0, sizeof(s_bones));
    for (int i = 0; i < 4; i++) {
        s_bones[i][i] = 1.0f;
        s_bones[4 + i][i] = 1.0f;
    }
    s_bones[7][0] = 8.0f;
}

/* Mesh i's stream as Packet.c builds it: a GIF tag (NLOOP 12, PRIM 0x0C:
 * strip, Gouraud, no texture; ST, RGBAQ, XYZ2), then per vertex its
 * quadwords (rd_mesh.h RD_VU_QW_*): pos, [normal, [weights,]] ST with the
 * strip flag (ST.w 0 on vertices 0 and 6), colour */
static uint32_t buildStream(int i, int replay, float (*q)[4])
{
    const float x0 = kX0[i] + ST_SHIFT_X * (float)replay;
    qw(q[0], fbits(0x8000u | ST_VERTS), fbits(0x30064000u), fbits(0x512u), 0);
    uint32_t at = 1;
    for (int k = 0; k < ST_VERTS; k++) {
        const int col = k / 2, row = k % 2;
        const float z = kZ0[i] + 0.04f * (float)(k % 3);
        /* GS X = 2048 + 16 x / z, Y = 2048 + 16 y / z */
        const float x = (x0 + 2.4f * (float)col) * z / 2.0f;
        const float y = (row ? 4.5f : -4.5f) * z / 2.0f + 0.3f * (float)col;
        qw(q[at++], x, y, z, 1);
        if (i != M_PRELIT) {
            qw(q[at++], 0.3f * (float)(col - 2), row ? 0.5f : -0.5f, 1.0f, 1.0f);
        }
        if (i == M_SKIN) {
            const float w0 = 1.0f - 0.1f * (float)col;
            qw(q[at++], fbits(16u), w0, fbits(20u), 1.0f - w0);
        }
        qw(q[at++], 0.1f * (float)col, row ? 1.0f : 0.0f, 1.0f, k == 0 || k == 6 ? 0.0f : 1.0f);
        qw(q[at++], (float)(40 + 30 * col), (float)(200 - 15 * k), (float)(60 + 80 * row + 7 * i),
           127.0f);
    }
    return at;
}

static RdMesh makeMesh(int i, int replay)
{
    static float q[1 + ST_VERTS * RD_VU_QW_SKIN][4];
    static const char *const kName[M_COUNT] = {"rd vu pre-pass self-test prelit",
                                               "rd vu pre-pass self-test lit",
                                               "rd vu pre-pass self-test skin"};
    const uint32_t n = buildStream(i, replay, q);
    const RdVuBatchDesc batch = {0, 0, 0};
    const RdVuMeshDesc d = {(const float (*)[4])q, n, kQpv[i], 1, &batch, 0, kName[i], NULL, 0, 0};
    return rd_create_vu_mesh(&d);
}

/* the ticks copy's blended camera (the file comment): model to GS screen
 * as columns (VU memory 16..19, vu_matm) */
static const double kTickCam[4][4] = {{0.16, 0.0, 0.0, 0.0},
                                      {0.0, 0.16, 0.0, 0.0},
                                      {2048.0, 2048.0, 4.0e6, 1.0},
                                      {-2048.0 * 2.22, -2048.0 * 2.22, -8.92e6, -2.22}};

/* replay k's blended camera: kTickCam with its x' and y' offsets moved */
static void tickCam(int replay, double cam[4][4])
{
    memcpy(cam, kTickCam, sizeof(kTickCam));
    cam[3][0] += ST_SHIFT_CAM * (double)replay;
    cam[3][1] += ST_SHIFT_CAM * (double)replay;
}

/* the ticks' cameras (RdFrame.tickCams, vu_common.hlsli vu_ticks: R_k = S_k
 * St^-1 as four columns): tick 0 the blended camera itself (the identity),
 * tick 1 the scene's camera (sceneBlock's 16..19) times the blended one's
 * inverse, so tick 1 sees the mesh as the prelit one is drawn */
static void tickCams(int replay, float out[2][16])
{
    double kc[4][4];
    tickCam(replay, kc);
    memset(out, 0, 2 * 16 * sizeof(float));
    for (int i = 0; i < 4; i++) {
        out[0][i * 4 + i] = 1.0f;
    }
    /* St (rows r, columns c: St[r][c] = kc[c][r]) is the 2 x 2 block
     * [[0.16, 0, 2048, -4546.56 + d], [0, 0.16, 2048, -4546.56 + d], [0, 0,
     * 4e6, -8.92e6], [0, 0, 1, -2.22]] (d the replay's move); its inverse by
     * its blocks */
    double inv[4][4];
    memset(inv, 0, sizeof(inv));
    const double a = kc[2][2], b = kc[3][2], c = kc[2][3], d = kc[3][3];
    const double det = a * d - b * c;
    /* (z, 1) from (z', w') */
    const double zz = d / det, zw = -b / det, oz = -c / det, ow = a / det;
    /* x = (x' - 2048 z - x0 * 1) / 0.16, the same for y */
    for (int r = 0; r < 2; r++) {
        const double s = kc[r][r], k2 = kc[2][r], k3 = kc[3][r];
        inv[r][r] = 1.0 / s;
        inv[r][2] = -(k2 * zz + k3 * oz) / s;
        inv[r][3] = -(k2 * zw + k3 * ow) / s;
    }
    inv[2][2] = zz;
    inv[2][3] = zw;
    inv[3][2] = oz;
    inv[3][3] = ow;
    RdVuBlock vb;
    sceneBlock(&vb);
    /* R = S inv: column c of R (out[1][c * 4 + r]) */
    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            double v = 0.0;
            for (int k = 0; k < 4; k++) {
                v += (double)vb.mem[16 + k][r] * inv[k][c];
            }
            out[1][c * 4 + r] = (float)v;
        }
    }
}

/* mesh i's command: RdVuPayload, its VU memory and, for the cluster, the
 * bones (rd_mesh.h's payload; no card verdicts); ticks: the prelit mesh
 * through the blended camera, its draw RD_VU_TICK_CAMERAS */
static bool pushMesh(RdFrame *f, int i, RdMesh mesh, bool ticks, int replay)
{
    uint8_t buf[sizeof(RdVuPayload) + sizeof(RdVuBlock) + sizeof(s_bones)];
    RdVuPayload p;
    RdVuBlock vu;
    memset(&p, 0, sizeof(p));
    p.code = kCode[i];
    p.clip = RD_VU_CLIP_REGION;
    p.prog = kProg[i];
    p.batchCount = 1;
    p.boneQw = i == M_SKIN ? 8u : 0u;
    p.qwPerVertex = kQpv[i];
    sceneBlock(&vu);
    if (ticks) {
        double cam[4][4];
        tickCam(replay, cam);
        for (int k = 0; k < 4; k++) {
            for (int r = 0; r < 4; r++) {
                vu.mem[16 + k][r] = (float)cam[k][r];
            }
        }
    }
    memcpy(buf, &p, sizeof(p));
    memcpy(buf + sizeof(p), &vu, sizeof(vu));
    memcpy(buf + sizeof(p) + sizeof(vu), s_bones, sizeof(s_bones));
    const uint32_t size = (uint32_t)(sizeof(p) + sizeof(vu)) + p.boneQw * 16u;
    const uint32_t off = rd__frame_payload(f, buf, size);
    RdCmd *c = rd__st_push(f, 0, i == M_SKIN ? RDC_SKINNED : RDC_MESH);
    if (!c) {
        return false;
    }
    c->b[0] = p.prog;
    c->b[1] = p.code;
    c->b[2] = p.clip;
    c->u[0] = mesh.id;
    c->u[1] = off;
    c->u[2] = size;
    c->u[3] = ticks ? RD_VU_TICK_CAMERAS : 0u;
    c->keyLo = (uint32_t)i + 1u + (ticks ? 8u : 0u);
    return true;
}

/* the particle command (rd_mesh.c rd_draw_vu_particles' payload, the
 * stream as vu_particle.hlsl reads it): four sprites of grey 100, alpha
 * 128, 2.5 units in front, across the block at GS X 2048 + 6.4 x, half
 * extent 6 GS pixels, inside the x/y window */
static bool pushSprites(RdFrame *f)
{
    float q[6 + 2 * ST_SPRITES][4];
    memset(q, 0, sizeof(q));
    q[0][0] = fbits(ST_SPRITES);
    qw(q[3], 1024.0f, 1024.0f, 0.0f, 0.1f);      /* clip minimum: x, y, -, w */
    qw(q[4], 3071.0f, 3071.0f, 0.0f, 100000.0f); /* clip maximum */
    qw(q[5], 1.0f, 0.1f, 0.1f, 0.0f);            /* size scale, du, dv */
    for (int k = 0; k < ST_SPRITES; k++) {
        qw(q[6 + 2 * k], -6.0f + 4.0f * (float)k, 0.0f, 2.5f, 960.0f);
        qw(q[7 + 2 * k], 0.25f * (float)k, 0.0f, 100.0f, 128.0f);
    }
    RdVuPayload p;
    RdVuBlock vu;
    memset(&p, 0, sizeof(p));
    p.code = 18;
    p.clip = RD_VU_CLIP_REGION;
    p.prog = RD_PROG_PARTICLE;
    p.vertsPerBatch = ST_SPRITES;
    p.qwPerVertex = RD_VU_QW_PARTICLE;
    p.streamQw = 6 + 2 * ST_SPRITES;
    sceneBlock(&vu);
    uint8_t buf[sizeof(RdVuPayload) + sizeof(RdVuBlock) + sizeof(q)];
    memcpy(buf, &p, sizeof(p));
    memcpy(buf + sizeof(p), &vu, sizeof(vu));
    memcpy(buf + sizeof(p) + sizeof(vu), q, sizeof(q));
    const uint32_t off = rd__frame_payload(f, buf, (uint32_t)sizeof(buf));
    RdCmd *c = rd__st_push(f, 0, RDC_PARTICLES);
    if (!c) {
        return false;
    }
    c->b[0] = p.prog;
    c->b[1] = p.code;
    c->b[2] = p.clip;
    c->u[1] = off;
    c->u[2] = (uint32_t)sizeof(buf);
    c->keyLo = 16u;
    return true;
}

/* whether the old way's VU shaders are the plain ones (no tick cameras: the
 * ticks copy is left out) */
static bool fallbackPlain(void)
{
    return g_rd.vuFallbackPlain; /* rd_pipeline.c rd__vu_policy_init */
}

static bool buildFrame(RdFrame *f, uint32_t scene, uint32_t w, uint32_t h, const RdMesh *m,
                       int replay)
{
    memset(f, 0, sizeof(*f));
    rd__reset_state_block(&f->startState);
    f->gsW = w;
    f->gsH = h;
    bool ok = rd__st_target(f, 0, scene, w, h);
    RdCmd *c = rd__st_push(f, 0, RDC_CLEAR);
    if (c) {
        c->u[0] = scene;
        memcpy(c->b, kClear, 4);
        c->b[4] = 1; /* the depth too, to GS Z 0 */
        c->u[1] = 0;
    }
    RdCmd *b = ok && c ? rd__st_push(f, 0, RDC_BLEND) : NULL;
    if (b) {
        b->b[1] = 0x80; /* ABE off: no blend */
    }
    ok = ok && b && rd__st_push(f, 0, RDC_TEXTURE_OFF) && rd__st_test(f, 0, RD_TEST_Z_GEQUAL) &&
         rd__st_push1(f, 0, RDC_ZWRITE, RD_ZWRITE_ON) && rd__st_push1(f, 0, RDC_FBA, 0) &&
         rd__st_push1(f, 0, RDC_PABE, 0) && rd__st_push1(f, 0, RDC_COLCLAMP, 1) &&
         rd__st_push1(f, 0, RDC_SHADE, 1);
    for (int i = 0; i < M_COUNT && ok; i++) {
        ok = pushMesh(f, i, m[i], false, replay);
    }
    if (ok && !fallbackPlain()) {
        f->hasTickCams = 1;
        tickCams(replay, f->tickCams);
        ok = pushMesh(f, M_PRELIT, m[M_PRELIT], true, replay);
    }
    return ok && pushSprites(f);
}

/* ------------------------------------------------------------- the runs */

static struct {
    RhiTexture read[ST_REPLAYS]; /* replay k's block */
    RhiState readState[ST_REPLAYS];
    RhiRect rect; /* the block's texels in SCENE */
    int sabotage; /* RD_VU_SABOTAGE_* */
} s_vt;

/* the block's texels in SCENE (rd_blend_selftest.c texelOf's mapping) */
static RhiRect blockRect(const RdTargetRec *t)
{
    const double gx[2] = {(double)((t->w - ST_W) / 2), (double)((t->w - ST_W) / 2 + ST_W)};
    const double gy[2] = {(double)((t->h - ST_H) / 2), (double)((t->h - ST_H) / 2 + ST_H)};
    uint32_t x[2], y[2];
    for (int k = 0; k < 2; k++) {
        const double tx = t->ox > 0.0f ? gx[k] * t->sx - t->ox : gx[k] * t->tw / t->w;
        const double ty = gy[k] * t->th / t->h;
        x[k] = tx < 0.0 ? 0u : (tx >= (double)t->tw ? t->tw : (uint32_t)tx);
        y[k] = ty < 0.0 ? 0u : (ty >= (double)t->th ? t->th : (uint32_t)ty);
    }
    return (RhiRect){(int32_t)x[0], (int32_t)y[0], x[1] - x[0], y[1] - y[0]};
}

/* one replay of f, the block copied into replay k's texture (read back
 * after the four, readBlocks: no wait between the replays) */
static bool runOnce(const RdFrame *f, RdTargetRec *t, int k)
{
    if (!rd__replay_frame(f, 0, false) || rhi_device_lost()) {
        return false;
    }
    if (!s_vt.read[k].id) {
        s_vt.read[k] = rhi_create_texture(&(RhiTextureDesc){s_vt.rect.w, s_vt.rect.h, 1, t->format,
                                                            RHI_TEX_COPY_DST | RHI_TEX_COPY_SRC,
                                                            "rd vu pre-pass self-test", 0});
        s_vt.readState[k] = RHI_STATE_UNDEFINED;
        if (!s_vt.read[k].id) {
            return false;
        }
    }
    RhiCommandList cl = rhi_begin_commands();
    if (!cl.id) {
        return false;
    }
    rd__transition(cl, t->color, &t->colorState, RHI_STATE_COPY_SRC);
    rd__transition(cl, s_vt.read[k], &s_vt.readState[k], RHI_STATE_COPY_DST);
    rhi_cmd_copy_texture(cl, t->color, s_vt.rect, s_vt.read[k], 0, 0);
    rhi_end_commands(cl);
    rhi_submit(cl);
    return true;
}

/* the four blocks into px (ST_REPLAYS x size bytes, rect.w x rect.h RGBA8
 * each) */
static bool readBlocks(uint8_t *px, size_t size)
{
    for (int k = 0; k < ST_REPLAYS; k++) {
        if (!rd__read_rhi_texture(s_vt.read[k], &s_vt.readState[k], s_vt.rect.w, s_vt.rect.h,
                                  px + (size_t)k * size, size)) {
            return false;
        }
    }
    return !rhi_device_lost();
}

/* on: the pre-pass stays on; pass: it was run and passed; text: the log
 * line's words inside its parentheses after "on (" or "off (self-test: " */
typedef struct Verdict {
    bool on, pass;
    char text[320];
} Verdict;

static void readSabotage(void)
{
    static const char *const kWhat[] = {"",
                                        "the first command's vertex records are drawn one "
                                        "texel off",
                                        "the first command's triangles' slot A is drawn one "
                                        "slot off",
                                        "the tick stage tests tick 1 at tick 0's camera",
                                        "the record's copies are left out from the second "
                                        "replay on"};
    const char *sb = getenv("ICO_RD_VU_PREPASS_SABOTAGE");
    s_vt.sabotage = RD_VU_SABOTAGE_OFF;
    if (sb != NULL && strcmp(sb, "tri") == 0) {
        s_vt.sabotage = RD_VU_SABOTAGE_TRI;
    } else if (sb != NULL && strcmp(sb, "ticks") == 0) {
        s_vt.sabotage = RD_VU_SABOTAGE_TICKS;
    } else if (sb != NULL && strcmp(sb, "stale") == 0) {
        s_vt.sabotage = RD_VU_SABOTAGE_STALE;
    } else if (sb != NULL && sb[0] != '\0' && sb[0] != '0') {
        s_vt.sabotage = RD_VU_SABOTAGE_VERTEX;
    }
    if (s_vt.sabotage != RD_VU_SABOTAGE_OFF) {
        rd__log("VU pre-pass: ICO_RD_VU_PREPASS_SABOTAGE: in the self-test %s",
                kWhat[s_vt.sabotage]);
    }
}

/* the pre-pass's replay k: what it counted and the frame slot it drew in */
typedef struct PreRun {
    RdPerfRecord r;
    uint32_t entryDraws, slot;
} PreRun;

/* the verdict of the four replays against their references */
static void judge(Verdict *v, const PreRun *run, const uint8_t *ref, const uint8_t *pre,
                  size_t size, uint32_t wantDraws, bool ticks)
{
    for (int k = 0; k < ST_REPLAYS; k++) {
        const RdPerfRecord *r = &run[k].r;
        if (r->vuPrePasses != 2 || run[k].entryDraws != wantDraws || r->vuPreFallbacks != 0 ||
            r->vuPreSprites != ST_SPRITES) {
            v->on = false;
            snprintf(v->text, sizeof v->text,
                     "a pipeline could not be made (replay %d of %d: %u passes, %u of %u "
                     "pre-pass draws, %u sprites, %u draws the old way)",
                     k + 1, ST_REPLAYS, r->vuPrePasses, run[k].entryDraws, wantDraws,
                     r->vuPreSprites, r->vuPreFallbacks);
            return;
        }
    }
    size_t drawn = 0;
    for (int k = 0; k < ST_REPLAYS; k++) {
        const uint8_t *a0 = ref + (size_t)k * size, *p0 = pre + (size_t)k * size;
        size_t diff = 0, first = 0;
        for (size_t i = 0; i < size; i++) {
            if (a0[i] != p0[i] && diff++ == 0) {
                first = i / 4u;
            }
        }
        size_t d = 0;
        for (size_t i = 0; i < size / 4u; i++) {
            d += memcmp(&a0[i * 4u], kClear, 4) != 0;
        }
        drawn = k == 0 ? d : drawn;
        if (diff) {
            /* the first replay that differs, its frame slot and its first
               texel: a stale record differs from the second replay on */
            const uint8_t *a = &p0[first * 4u], *b = &a0[first * 4u];
            v->on = false;
            snprintf(v->text, sizeof v->text,
                     "the pictures differ: in replay %d of %d (frame slot %u), %zu of %zu "
                     "bytes, the first at texel %u,%u: %u,%u,%u,%u want %u,%u,%u,%u",
                     k + 1, ST_REPLAYS, run[k].slot, diff, size,
                     (unsigned)(s_vt.rect.x + (int32_t)(first % s_vt.rect.w)),
                     (unsigned)(s_vt.rect.y + (int32_t)(first / s_vt.rect.w)), a[0], a[1], a[2],
                     a[3], b[0], b[1], b[2], b[3]);
            return;
        }
        if (d == 0) {
            /* both drew nothing: no verdict on the pre-pass */
            snprintf(v->text, sizeof v->text,
                     "self-test inconclusive: neither replay drew the meshes (replay %d of %d)",
                     k + 1, ST_REPLAYS);
            return;
        }
    }
    bool both = false;
    for (int k = 1; k < ST_REPLAYS; k++) {
        both = both || run[k].slot != run[0].slot;
    }
    const RdPerfRecord *r = &run[0].r;
    v->pass = true;
    snprintf(v->text, sizeof v->text,
             "self-test: prelit, lit, skin%s and particles byte for byte over %d replays in %s, "
             "%u vertices and %u sprites in %u pre-pass draws (%u passes, %u triangle slots), "
             "%zu of %zu pixels drawn",
             ticks ? ", the tick cameras" : "", ST_REPLAYS,
             both ? "both frame slots" : "one frame slot", r->vuPreVertices, r->vuPreSprites,
             r->vuPreDraws, r->vuPrePasses, r->vuTriSlots, drawn, size / 4u);
}

static void runSelftest(RdTargetRec *t, Verdict *v)
{
    v->on = true;
    v->pass = false;
    sceneBones();
    RdMesh m[ST_REPLAYS][M_COUNT];
    bool made = true;
    for (int k = 0; k < ST_REPLAYS; k++) {
        for (int i = 0; i < M_COUNT; i++) {
            m[k][i] = makeMesh(i, k);
            made = made && m[k][i].id != 0;
        }
    }
    static RdFrame f[ST_REPLAYS];
    s_vt.rect = blockRect(t);
    const size_t size = (size_t)s_vt.rect.w * s_vt.rect.h * 4u;
    uint8_t *ref = size ? malloc(size * ST_REPLAYS) : NULL;
    uint8_t *pre = size ? malloc(size * ST_REPLAYS) : NULL;
    bool built = made && ref && pre;
    for (int k = 0; k < ST_REPLAYS; k++) {
        /* every frame built (and freed below) whatever the one before did */
        const bool ok = buildFrame(&f[k], rd_target(RD_TARGET_SCENE).id, t->w, t->h, m[k], k);
        built = built && ok;
    }
    if (!built) {
        snprintf(v->text, sizeof v->text, "self-test not run: could not build its frame");
    } else {
        const bool wasBlocked = g_rd.msaaBlocked;
        g_rd.msaaBlocked = true; /* the MSAA epoch is not what is checked */
        /* both replays' pipelines first, compiled on the worker threads the
           start-up set uses (as the loss bisect's, rd_dump.c
           bisectPipelines): a replay of each with nothing drawn names the
           keys the cache lacks (rd__pipeline_gather); on a phone's driver
           each VU pipeline takes a second or more, serial on this thread
           several.  The moved frames ask for the same keys */
        rd__replay_set_window(UINT32_MAX, 0);
        rd__pipeline_gather(true);
        g_rd.vuPrepass = false;
        (void)rd__replay_frame(&f[0], 0, false);
        g_rd.vuPrepass = true;
        (void)rd__replay_frame(&f[0], 0, false);
        rd__pipeline_gather(false);
        rd__replay_set_window(0, 0);
        rhi_wait_idle();
        if (!rhi_device_lost()) {
            bool stopped = false;
            (void)rd__pipeline_create_gathered(&stopped);
        }
        /* the references: every command through its program's entry */
        g_rd.vuPrepass = false;
        bool refOk = true;
        for (int k = 0; k < ST_REPLAYS && refOk; k++) {
            refOk = runOnce(&f[k], t, k);
        }
        refOk = refOk && readBlocks(ref, size);
        /* the pre-pass's four, back to back: replay k in the frame slot
           after replay k - 1's (rd__ring_slot), sabotaged as asked (stale:
           the record's copies left out from the second on) */
        g_rd.vuPrepass = true;
        PreRun run[ST_REPLAYS];
        memset(run, 0, sizeof(run));
        bool preOk = refOk;
        for (int k = 0; k < ST_REPLAYS && preOk; k++) {
            const bool stale = s_vt.sabotage == RD_VU_SABOTAGE_STALE;
            rd__vu_prepass_sabotage(stale && k == 0 ? RD_VU_SABOTAGE_OFF : s_vt.sabotage);
            preOk = runOnce(&f[k], t, k);
            run[k].r = g_rdPerf; /* the record the replay closed */
            run[k].entryDraws = rd__vu_prepass_entry_draws();
            run[k].slot = rd__ring_slot();
        }
        rd__vu_prepass_sabotage(RD_VU_SABOTAGE_OFF);
        preOk = preOk && readBlocks(pre, size);
        /* the draws the frame makes through the pre-passes: pass V a vertex
         * draw a mesh and two for the sprites, pass T the rule and planes
         * stages a mesh and the tick stage's two for the ticks copy, counted
         * one an entry and stage whatever the batches merged them into
         * (g_rd.vuPreBatch: r.vuPreDraws the draws made) */
        const uint32_t meshes = M_COUNT + (fallbackPlain() ? 0u : 1u);
        const uint32_t wantDraws = meshes + 2u + 2u * meshes + (meshes > M_COUNT ? 2u : 0u);
        g_rd.msaaBlocked = wasBlocked;
        if (!refOk || !preOk) {
            v->on = false;
            snprintf(v->text, sizeof v->text, "the device failed in the replays %s the pre-pass",
                     refOk ? "with" : "without");
        } else {
            judge(v, run, ref, pre, size, wantDraws, meshes > M_COUNT);
        }
    }
    free(ref);
    free(pre);
    for (int k = 0; k < ST_REPLAYS; k++) {
        rd__frame_free(&f[k]);
        for (int i = 0; i < M_COUNT; i++) {
            if (m[k][i].id) {
                rd_destroy_vu_mesh(m[k][i]);
            }
        }
        if (s_vt.read[k].id) {
            rhi_destroy_texture(s_vt.read[k]);
        }
        s_vt.read[k] = (RhiTexture){0};
    }
    /* the next replay starts SCENE from its clear, not from the block */
    t->clearPending = 1;
}

/* runSelftest is replaying (rd__vu_prepass_selftest_running) */
static bool s_vtRunning;

bool rd__vu_prepass_selftest_running(void)
{
    return s_vtRunning;
}

bool rd_vuprepass_selftest(void)
{
    if (!g_rd.inited || !g_rd.hasDevice) {
        return false;
    }
    /* first, whatever the pre-pass: the skin check's start-up probe and its
     * start line (rd_vu_skin_selftest.c; issue 163) */
    (void)rd__vu_skin_selftest();
    if (!g_rd.vuPrepass) {
        snprintf(g_rdSumPrepass, sizeof g_rdSumPrepass, "off %s (self-test not run)",
                 g_rd.vuPrepassWhy);
        rd__log("VU pre-pass: off (self-test not run)");
        return true;
    }
    RdTargetRec *t = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    if (!t || !t->color.id || t->format != RHI_FMT_RGBA8_UNORM || t->w < ST_W || t->h < ST_H) {
        snprintf(g_rdSumPrepass, sizeof g_rdSumPrepass, "on (self-test not run: no SCENE target)");
        rd__log("VU pre-pass: on (self-test not run: no SCENE target to run it on)");
        return true;
    }
    readSabotage();
    const double t0 = rd__now_ms();
    Verdict v;
    s_vtRunning = true; /* the skin check leaves these replays out */
    runSelftest(t, &v);
    s_vtRunning = false;
    if (!v.on) {
        g_rd.vuPrepass = false;
        snprintf(g_rd.vuPrepassWhy, sizeof g_rd.vuPrepassWhy, "by its self-test");
        /* on Direct3D 11 (the record copied by the backend's compute
           shader since v0.10.0, d3d11_cmd.c): the state of the test's last
           draw (RhiStats.lastDrawText, NULL on the other backends), as the
           blend's and the fog's self-tests log it; the backend's own
           "rhi_d3d11: texture-to-buffer copies ..." lines come before */
        RhiStats st;
        memset(&st, 0, sizeof(st));
        rhi_get_stats(&st);
        const char *draw = st.lastDrawText ? st.lastDrawText() : NULL;
        if (draw) {
            rd__log("VU pre-pass: self-test failed on Direct3D 11; its last draw: %s", draw);
        }
    }
    const char *head = v.on ? "on (" : "off (self-test: ";
    snprintf(g_rdSumPrepass, sizeof g_rdSumPrepass, "%s%s)", head,
             v.pass ? "self-test: ok" : v.text);
    rd__log("VU pre-pass: %s%s) on SCENE %ux%u at %ux%u texels in %.1f ms", head, v.text, t->w,
            t->h, t->tw, t->th, rd__now_ms() - t0);
    return v.on;
}

/* rd_vuprepass_selftest.c: the VU vertex pre-pass's self-test (rd.h
 * rd_vuprepass_selftest).
 *
 * The vertex pre-pass (rd_replay_vu.c rd__vu_prepass; docs/port/RENDER.md,
 * "The vertex pre-pass") draws with pipelines no other draw of the port
 * has: a vertex shader writing PointSize into a point list, flat uint4
 * varyings into three RGBA32_UINT targets (48 bytes a pixel), a
 * texture-to-buffer copy a vertex shader reads in the same list, and the
 * triangle stage's record reads.  A driver that cannot make one of those
 * pipelines would leave every VU pipeline to be made on first use (the
 * start-up set holds the triangle stage's keys, not the programs'), and one
 * that miscompiles them would draw the 3D scenery wrong with no error
 * anywhere.  So the pre-pass is checked on the device itself at start.
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
 * matrices).  The pipelines of both replays are made first on the worker
 * threads (two replays that draw nothing name them, rd__pipeline_gather;
 * rd__pipeline_create_gathered).  The frame is replayed without the
 * pre-pass (the programs' entries: the reference) and with it, in the VU shader variant the
 * settings draw with (rd__vu_draw_ext), and the block is read back after
 * each.  The pre-pass passes when its replay drew every command through it
 * (one pre-pass, a pre-pass draw a command, no draw the old way) and the
 * two blocks are equal byte for byte.  A difference, a pipeline that could
 * not be made (its draws then take their programs' entries) or a device
 * error turns the pre-pass off for the run (g_rd.vuPrepass); the window
 * calls this before the start-up set, so the set then holds the programs'
 * keys.  MSAA is held off while it runs, and SCENE is left to start from
 * its clear.  A start whose pre-pass is off (by the settings, the device or
 * the host, rd_set_vu_prepass_off) runs nothing.
 *
 *   rd: VU pre-pass: on (self-test: prelit, lit and skin byte for byte, 36
 *     vertices in 3 pre-pass draws, N of M pixels drawn) on SCENE WxH at
 *     TWxTH texels in T ms
 *   rd: VU pre-pass: off (self-test: the pictures differ: N of M bytes, the
 *     first at texel X,Y: R,G,B,A want R,G,B,A) on SCENE ...
 *
 * ICO_RD_VU_PREPASS_SABOTAGE=1 (tests): the replay with the pre-pass draws
 * its first command's records one texel off (rd__vu_prepass_sabotage), as a
 * driver whose record write or read went wrong would, so the fallback can
 * be run where the pre-pass is right.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "rd_mesh.h"

#define ST_W 256 /* the block, GS pixels */
#define ST_H 96
#define ST_VERTS 12 /* a mesh's vertices: two strips of 6 */

enum { M_PRELIT, M_LIT, M_SKIN, M_COUNT };

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
static uint32_t buildStream(int i, float (*q)[4])
{
    qw(q[0], fbits(0x8000u | ST_VERTS), fbits(0x30064000u), fbits(0x512u), 0);
    uint32_t at = 1;
    for (int k = 0; k < ST_VERTS; k++) {
        const int col = k / 2, row = k % 2;
        const float z = kZ0[i] + 0.04f * (float)(k % 3);
        /* GS X = 2048 + 16 x / z, Y = 2048 + 16 y / z */
        const float x = (kX0[i] + 2.4f * (float)col) * z / 2.0f;
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

static RdMesh makeMesh(int i)
{
    static float q[1 + ST_VERTS * RD_VU_QW_SKIN][4];
    static const char *const kName[M_COUNT] = {"rd vu pre-pass self-test prelit",
                                               "rd vu pre-pass self-test lit",
                                               "rd vu pre-pass self-test skin"};
    const uint32_t n = buildStream(i, q);
    const RdVuBatchDesc batch = {0, 0, 0};
    const RdVuMeshDesc d = {(const float (*)[4])q, n, kQpv[i], 1, &batch, 0, kName[i]};
    return rd_create_vu_mesh(&d);
}

/* mesh i's command: RdVuPayload, its VU memory and, for the cluster, the
 * bones (rd_mesh.h's payload; no card verdicts) */
static bool pushMesh(RdFrame *f, int i, RdMesh mesh)
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
    c->keyLo = (uint32_t)i + 1u;
    return true;
}

static bool buildFrame(RdFrame *f, uint32_t scene, uint32_t w, uint32_t h, const RdMesh *m)
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
        ok = pushMesh(f, i, m[i]);
    }
    return ok;
}

/* ------------------------------------------------------------- the runs */

static struct {
    RhiTexture read;
    RhiState readState;
    RhiRect rect; /* the block's texels in SCENE */
    bool sabotage;
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

/* one replay of f, the block into px (rect.w x rect.h RGBA8) */
static bool runOnce(const RdFrame *f, RdTargetRec *t, uint8_t *px, size_t size)
{
    if (!rd__replay_frame(f, 0, false) || rhi_device_lost()) {
        return false;
    }
    if (!s_vt.read.id) {
        s_vt.read = rhi_create_texture(&(RhiTextureDesc){s_vt.rect.w, s_vt.rect.h, 1, t->format,
                                                         RHI_TEX_COPY_DST | RHI_TEX_COPY_SRC,
                                                         "rd vu pre-pass self-test", 0});
        s_vt.readState = RHI_STATE_UNDEFINED;
        if (!s_vt.read.id) {
            return false;
        }
    }
    RhiCommandList cl = rhi_begin_commands();
    if (!cl.id) {
        return false;
    }
    rd__transition(cl, t->color, &t->colorState, RHI_STATE_COPY_SRC);
    rd__transition(cl, s_vt.read, &s_vt.readState, RHI_STATE_COPY_DST);
    rhi_cmd_copy_texture(cl, t->color, s_vt.rect, s_vt.read, 0, 0);
    rhi_end_commands(cl);
    rhi_submit(cl);
    return rd__read_rhi_texture(s_vt.read, &s_vt.readState, s_vt.rect.w, s_vt.rect.h, px, size) &&
           !rhi_device_lost();
}

/* on: the pre-pass stays on; pass: it was run and passed; text: the log
 * line's words inside its parentheses after "on (" or "off (self-test: " */
typedef struct Verdict {
    bool on, pass;
    char text[200];
} Verdict;

static void readSabotage(void)
{
    const char *sb = getenv("ICO_RD_VU_PREPASS_SABOTAGE");
    s_vt.sabotage = sb != NULL && sb[0] != '\0' && sb[0] != '0';
    if (s_vt.sabotage) {
        rd__log("VU pre-pass: ICO_RD_VU_PREPASS_SABOTAGE: the self-test's first command's "
                "records are drawn one texel off");
    }
}

static void runSelftest(RdTargetRec *t, Verdict *v)
{
    v->on = true;
    v->pass = false;
    sceneBones();
    RdMesh m[M_COUNT];
    bool made = true;
    for (int i = 0; i < M_COUNT; i++) {
        m[i] = makeMesh(i);
        made = made && m[i].id != 0;
    }
    static RdFrame f;
    s_vt.rect = blockRect(t);
    const size_t size = (size_t)s_vt.rect.w * s_vt.rect.h * 4u;
    uint8_t *ref = size ? malloc(size) : NULL;
    uint8_t *pre = size ? malloc(size) : NULL;
    if (!made || !ref || !pre || !buildFrame(&f, rd_target(RD_TARGET_SCENE).id, t->w, t->h, m)) {
        snprintf(v->text, sizeof v->text, "self-test not run: could not build its frame");
    } else {
        const bool wasBlocked = g_rd.msaaBlocked;
        g_rd.msaaBlocked = true; /* the MSAA epoch is not what is checked */
        /* both replays' pipelines first, compiled on the worker threads the
           start-up set uses (as the loss bisect's, rd_dump.c
           bisectPipelines): a replay of each with nothing drawn names the
           keys the cache lacks (rd__pipeline_gather); on a phone's driver
           each VU pipeline takes a second or more, serial on this thread
           several */
        rd__replay_set_window(UINT32_MAX, 0);
        rd__pipeline_gather(true);
        g_rd.vuPrepass = false;
        (void)rd__replay_frame(&f, 0, false);
        g_rd.vuPrepass = true;
        (void)rd__replay_frame(&f, 0, false);
        rd__pipeline_gather(false);
        rd__replay_set_window(0, 0);
        rhi_wait_idle();
        if (!rhi_device_lost()) {
            bool stopped = false;
            (void)rd__pipeline_create_gathered(&stopped);
        }
        /* the reference: every command through its program's entry */
        g_rd.vuPrepass = false;
        const bool refOk = runOnce(&f, t, ref, size);
        /* the pre-pass, its first command one texel off when sabotaged */
        g_rd.vuPrepass = true;
        rd__vu_prepass_sabotage(s_vt.sabotage);
        const bool preOk = refOk && runOnce(&f, t, pre, size);
        rd__vu_prepass_sabotage(false);
        const RdPerfRecord r = g_rdPerf; /* the record the replay with it closed */
        g_rd.msaaBlocked = wasBlocked;
        if (!refOk || !preOk) {
            v->on = false;
            snprintf(v->text, sizeof v->text, "the device failed in the replay %s the pre-pass",
                     refOk ? "with" : "without");
        } else if (r.vuPrePasses != 1 || r.vuPreDraws != M_COUNT || r.vuPreFallbacks != 0) {
            v->on = false;
            snprintf(v->text, sizeof v->text,
                     "a pipeline could not be made (%u of %d commands through the pre-pass, %u "
                     "draws the old way)",
                     r.vuPreDraws, M_COUNT, r.vuPreFallbacks);
        } else {
            size_t diff = 0, first = 0, drawn = 0;
            for (size_t i = 0; i < size; i++) {
                if (ref[i] != pre[i] && diff++ == 0) {
                    first = i / 4u;
                }
            }
            for (size_t i = 0; i < size / 4u; i++) {
                drawn += memcmp(&ref[i * 4u], kClear, 4) != 0;
            }
            if (diff) {
                const uint8_t *a = &pre[first * 4u], *b = &ref[first * 4u];
                v->on = false;
                snprintf(v->text, sizeof v->text,
                         "the pictures differ: %zu of %zu bytes, the first at texel %u,%u: "
                         "%u,%u,%u,%u want %u,%u,%u,%u",
                         diff, size, (unsigned)(s_vt.rect.x + (int32_t)(first % s_vt.rect.w)),
                         (unsigned)(s_vt.rect.y + (int32_t)(first / s_vt.rect.w)), a[0], a[1], a[2],
                         a[3], b[0], b[1], b[2], b[3]);
            } else if (drawn == 0) {
                /* both drew nothing: no verdict on the pre-pass */
                snprintf(v->text, sizeof v->text,
                         "self-test inconclusive: neither replay drew the meshes");
            } else {
                v->pass = true;
                snprintf(v->text, sizeof v->text,
                         "self-test: prelit, lit and skin byte for byte, %u vertices in %u "
                         "pre-pass draws, %zu of %zu pixels drawn",
                         r.vuPreVertices, r.vuPreDraws, drawn, size / 4u);
            }
        }
    }
    free(ref);
    free(pre);
    rd__frame_free(&f);
    for (int i = 0; i < M_COUNT; i++) {
        if (m[i].id) {
            rd_destroy_vu_mesh(m[i]);
        }
    }
    if (s_vt.read.id) {
        rhi_destroy_texture(s_vt.read);
    }
    s_vt.read = (RhiTexture){0};
    /* the next replay starts SCENE from its clear, not from the block */
    t->clearPending = 1;
}

bool rd_vuprepass_selftest(void)
{
    if (!g_rd.inited || !g_rd.hasDevice) {
        return false;
    }
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
    runSelftest(t, &v);
    if (!v.on) {
        g_rd.vuPrepass = false;
        snprintf(g_rd.vuPrepassWhy, sizeof g_rd.vuPrepassWhy, "by its self-test");
    }
    const char *head = v.on ? "on (" : "off (self-test: ";
    snprintf(g_rdSumPrepass, sizeof g_rdSumPrepass, "%s%s)", head,
             v.pass ? "self-test: ok" : v.text);
    rd__log("VU pre-pass: %s%s) on SCENE %ux%u at %ux%u texels in %.1f ms", head, v.text, t->w,
            t->h, t->tw, t->th, rd__now_ms() - t0);
    return v.on;
}

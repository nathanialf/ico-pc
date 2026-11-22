/* rd_nodiscard_selftest.c: the discard-free twins' start-up self-test
 * (rd_nodiscard.h rd__nd_selftest), run by rd_vuprepass_selftest after the
 * zero-area cull's, on SCENE at its real size and scale.
 *
 * Only on a device without dual-source blending (g_rd.noDual): elsewhere
 * no pass draws through a *_nodual build, so the twins are not tried and
 * nothing of them is made ("not applicable here").
 *
 * The frame (built here, never recorded; the zero-area cull's helpers'
 * form): SCENE cleared, Z GEQUAL with Z write, no blend, untextured,
 * Gouraud; three prelit meshes of two triangles each at model depth 2,
 * where the screen matrix puts a vertex at GS X = 2048 + 8 x, Y = 2048 + 8 y,
 * each drawn once at code 34 (clip mode none, the one the twins take on
 * every device, Mesa's software rasteriser included):
 *   plain   the alpha test off: the CPU gives the pass its twin
 *   alpha   ATE GREATER 0x40, AFAIL KEEP, the corners' alphas 0, 127 and
 *           127 (the alpha plan cannot decide it): its pixels below the
 *           reference are discarded by the *_nodual build, so the CPU must
 *           refuse the twin
 *   plane   a chain plane through the middle of the mesh (the MESH
 *           command's b[5], f[0]): the pixels past it are discarded, refused
 * The side band and DATE cannot be drawn under the camera rule's frame
 * here: the rule itself (rd_nd_eligible) must refuse a region test's pass
 * without ICO_VU_NO_BESIDE and a pass with DATE.  The frame is replayed
 * with the twins (the pipelines of both gathered and made first on the
 * workers) and without them; the twins pass when the pictures are the
 * same byte for byte, the plain case drew through a twin, the alpha and
 * plane cases were refused for those reasons, no twin pipeline was missing
 * and the rule refused the band and DATE.  Anything else (a module or a
 * pipeline not made, a device error) leaves them off for the run, releases
 * their modules and pipelines and writes "failed" to the install's mark.
 * Either way every pipeline the self-test made is released after it
 * (rd__pipeline_forget_since): the run's set is the one it had before (the
 * draw pattern gate's counts unchanged), and the twins' pipelines are made
 * again in the background as passes ask for them.
 *
 *   rd: discard-free twins: on (self-test: 1 pass through them, the alpha
 *     test's and the clip plane's refused (1 and 1), the side band and DATE
 *     refused by the rule, pictures byte for byte; its 2 pipelines released)
 *     on SCENE WxH at TWxTH texels in T ms
 *   rd: discard-free twins: off for the run (self-test: <reason>) ...
 *
 * ICO_RD_VU_NODISCARD_SABOTAGE (tests, rd_nodiscard_run.c): alpha, the
 * CPU's proof does not look at the alpha test (the alpha case then draws
 * through a twin and the pictures differ); module, the modules not made. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "rd_mesh.h"
#include "rd_nodiscard.h"
#include "shader_consts.h"

#define NT_CASES 3
#define NT_VERTS 6 /* two triangles a mesh, three vertices each */
#define NT_Z 2.0f  /* model depth: GS X = 2048 + 8 x */
#define NT_HALF 12 /* a triangle's legs, GS pixels */

enum { NT_PLAIN, NT_ALPHA, NT_PLANE };

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

/* the case's cell centre, GS pixels */
static float cellX(int c)
{
    return 2048.0f + 48.0f * (float)(c - 1);
}

/* a case's mesh: two triangles about its cell (the prelit layout, rd_mesh.h
   RD_VU_QW_PRELIT: position, ST with the strip flag, colour) */
static RdMesh makeMesh(int c)
{
    static float q[1 + NT_VERTS * RD_VU_QW_PRELIT][4];
    static const float kCorner[NT_VERTS][2] = {{-1, -1}, {1, -1}, {-1, 1},
                                               {1, -1},  {1, 1},  {-1, 1}};
    qw(q[0], fbits(0x8000u | NT_VERTS), fbits(0x30064000u), fbits(0x512u), 0);
    uint32_t at = 1;
    for (int v = 0; v < NT_VERTS; v++) {
        /* GS X = 2048 + 8 x at depth 2 */
        const float gx = cellX(c) + (float)NT_HALF * kCorner[v][0];
        const float gy = 2048.0f + (float)NT_HALF * kCorner[v][1];
        qw(q[at++], (gx - 2048.0f) / 8.0f, (gy - 2048.0f) / 8.0f, NT_Z, 1.0f);
        qw(q[at++], 0.0f, 0.0f, 1.0f, v % 3 == 0 ? 0.0f : 1.0f);
        /* the alpha case's first corner of each triangle at 0: alphas from 0
           to 127 across it, the test failing below 0x40 */
        const float a = c == NT_ALPHA && v % 3 == 0 ? 0.0f : 127.0f;
        qw(q[at++], (float)(70 + 50 * c + 10 * v), (float)(200 - 20 * v), 120.0f, a);
    }
    const RdVuBatchDesc batch = {0, 0, 0};
    const RdVuMeshDesc d = {(const float (*)[4])q,
                            at,
                            RD_VU_QW_PRELIT,
                            1,
                            &batch,
                            0,
                            "rd discard-free twins self-test",
                            NULL,
                            0,
                            0};
    return rd_create_vu_mesh(&d);
}

/* rd_vu_zero_selftest.c sceneBlock's VU memory (vu1_test.c buildScene) */
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
        m[12 + i][i] = 1.0f;
        m[24 + i][i] = 1.0f;
    }
    memcpy(m[16], m[4], 4 * sizeof(m[0]));
    qw(m[20], 0.015625f, 0, 0, 0);
    qw(m[21], 0, 0.015625f, 0, 0);
    qw(m[22], 0, 0, 2, 1);
    qw(m[23], 0, 0, -3, 0);
}

/* a code 34 MESH command of mesh; plane: the chain plane x >= the cell's
   middle (rd_chain.h: axis x + 1, the plus side; the offset in f[0]) */
static bool pushMesh(RdFrame *f, RdMesh mesh, bool plane, float planeAt, uint32_t key)
{
    uint8_t buf[sizeof(RdVuPayload) + sizeof(RdVuBlock)];
    RdVuPayload p;
    RdVuBlock vu;
    memset(&p, 0, sizeof(p));
    p.code = 34;
    p.clip = RD_VU_CLIP_NONE;
    p.prog = RD_PROG_PRELIT;
    p.batchCount = 1;
    p.qwPerVertex = RD_VU_QW_PRELIT;
    sceneBlock(&vu);
    memcpy(buf, &p, sizeof(p));
    memcpy(buf + sizeof(p), &vu, sizeof(vu));
    const uint32_t off = rd__frame_payload(f, buf, (uint32_t)sizeof(buf));
    RdCmd *c = rd__st_push(f, 0, RDC_MESH);
    if (!c) {
        return false;
    }
    c->b[0] = p.prog;
    c->b[1] = p.code;
    c->b[2] = p.clip;
    c->u[0] = mesh.id;
    c->u[1] = off;
    c->u[2] = (uint32_t)sizeof(buf);
    c->u[3] = 0;
    if (plane) {
        c->b[5] = 1u;       /* the x axis, the plus side: drawn where x + f[0] >= 0 */
        c->f[0] = -planeAt; /* through the middle of the case's cell */
    }
    c->keyLo = key;
    return true;
}

static bool buildFrame(RdFrame *f, uint32_t scene, uint32_t w, uint32_t h, const RdMesh *mesh)
{
    static const uint8_t kClear[4] = {0, 0, 0, 0x80};
    memset(f, 0, sizeof(*f));
    rd__reset_state_block(&f->startState);
    f->gsW = w;
    f->gsH = h;
    bool ok = rd__st_target(f, 0, scene, w, h);
    RdCmd *c = rd__st_push(f, 0, RDC_CLEAR);
    if (c) {
        c->u[0] = scene;
        memcpy(c->b, kClear, 4);
        c->b[4] = 1;
        c->u[1] = 0;
    }
    RdCmd *b = ok && c ? rd__st_push(f, 0, RDC_BLEND) : NULL;
    if (b) {
        b->b[1] = 0x80; /* ABE off */
    }
    ok = ok && b && rd__st_push(f, 0, RDC_TEXTURE_OFF) && rd__st_test(f, 0, RD_TEST_Z_GEQUAL) &&
         rd__st_push1(f, 0, RDC_ZWRITE, RD_ZWRITE_ON) && rd__st_push1(f, 0, RDC_FBA, 0) &&
         rd__st_push1(f, 0, RDC_PABE, 0) && rd__st_push1(f, 0, RDC_COLCLAMP, 1) &&
         rd__st_push1(f, 0, RDC_SHADE, 1);
    /* plain, then the alpha test (ATE GREATER 0x40, AFAIL KEEP, Z GEQUAL),
       then the plane with the test off again */
    ok = ok && pushMesh(f, mesh[NT_PLAIN], false, 0.0f, 1u) &&
         rd__st_test(f, 0, (uint64_t)RD_TEST_Z_GEQUAL | 0x40Du) &&
         pushMesh(f, mesh[NT_ALPHA], false, 0.0f, 2u) && rd__st_test(f, 0, RD_TEST_Z_GEQUAL) &&
         pushMesh(f, mesh[NT_PLANE], true, (cellX(NT_PLANE) - 2048.0f) / 8.0f, 3u);
    return ok;
}

/* one replay of f and SCENE read into px (tw x th RGBA8) */
static bool replayRead(const RdFrame *f, RdTargetRec *t, uint8_t *px, size_t size)
{
    if (!rd__replay_frame(f, 0, false) || rhi_device_lost()) {
        return false;
    }
    return rd__read_rhi_texture(t->color, &t->colorState, t->tw, t->th, px, size) &&
           !rhi_device_lost();
}

/* the rule's own refusals the frame cannot show: a region test's pass
   without ICO_VU_NO_BESIDE (a side band possible), one with DATE; and the
   same pass with the flag and without DATE given the twin (the check is not
   vacuous) */
static bool ruleRefuses(void)
{
    RdNdPass p;
    memset(&p, 0, sizeof(p));
    p.fs = RD_FS_VU;
    p.nodual = RD_NODUAL_ON;
    p.dst = RHI_DST_READ_NONE;
    p.drawFlags = ICO_VU_CLIP_REGION;
    const bool band = rd_nd_eligible(&p) == RD_ND_BAND;
    p.drawFlags = ICO_VU_CLIP_REGION | ICO_VU_NO_BESIDE;
    const bool twin = rd_nd_eligible(&p) == RD_ND_TWIN;
    p.flags = ICO_DF_DATE;
    const bool date = rd_nd_eligible(&p) == RD_ND_DATE;
    return band && twin && date;
}

static void meshesFree(RdMesh *mesh)
{
    for (int i = 0; i < NT_CASES; i++) {
        if (mesh[i].id) {
            rd_destroy_vu_mesh(mesh[i]);
        }
    }
}

void rd__nd_selftest(RdTargetRec *t)
{
    if (strcmp(rd__nd_state_text(), "off") == 0) {
        /* off before it (the environment, the loss ladder, the install's
           mark): said at the start; nothing of them made */
        rd__nd_start_line();
        return;
    }
    if (!g_rd.noDual) {
        rd__nd_selftest_skip("not applicable here: the device blends with two outputs, so no pass "
                             "draws through a *_nodual build");
        return;
    }
    const double t0 = rd__now_ms();
    char why[256] = "";
    RdMesh mesh[NT_CASES];
    for (int i = 0; i < NT_CASES; i++) {
        mesh[i] = makeMesh(i);
    }
    RdFrame f;
    memset(&f, 0, sizeof(f));
    const uint32_t scene = rd_target(RD_TARGET_SCENE).id;
    const size_t size = (size_t)t->tw * t->th * 4u;
    uint8_t *a = malloc(size), *b = malloc(size);
    if (!mesh[0].id || !mesh[1].id || !mesh[2].id || !a || !b ||
        !buildFrame(&f, scene, t->w, t->h, mesh)) {
        /* nothing of the twins tried yet: not the device's verdict */
        rd__frame_free(&f);
        meshesFree(mesh);
        free(a);
        free(b);
        rd__nd_selftest_skip("could not build its frame");
        return;
    }
    /* the twins tried from here: the mark, then their two modules; every
       pipeline the self-test makes is released after it (the run's set the
       one it had: the twins' made again in the background as passes ask) */
    const uint32_t pipeMark = rd__pipeline_mark();
    rd__nd_hold(true);
    const bool made = rd__nd_try_begin();
    uint64_t twin = 0, refused[RD_ND_REASONS];
    memset(refused, 0, sizeof(refused));
    bool pass = false;
    if (!made) {
        snprintf(why, sizeof(why), "their modules could not be made");
    } else {
        /* the pipelines first, the twins' with the others, on the workers
           (rd_vuprepass_selftest.c, rd_vu_zero_selftest.c) */
        rd__replay_set_window(UINT32_MAX, 0);
        rd__pipeline_gather(true);
        (void)rd__replay_frame(&f, 0, false);
        rd__pipeline_gather(false);
        rd__replay_set_window(0, 0);
        rhi_wait_idle();
        if (!rhi_device_lost()) {
            bool stopped = false;
            (void)rd__pipeline_create_gathered(&stopped);
        }
        rd__nd_take(&twin, refused); /* nothing from the gathering replay */
        const bool okA = replayRead(&f, t, a, size);
        rd__nd_take(&twin, refused);
        rd__nd_hold(false); /* the same frame without them */
        uint64_t twinB = 0, refusedB[RD_ND_REASONS];
        const bool okB = okA && replayRead(&f, t, b, size);
        rd__nd_take(&twinB, refusedB);
        const bool rule = ruleRefuses();
        if (!okA || !okB) {
            snprintf(why, sizeof(why), "the device failed in the replays %s them",
                     okA ? "without" : "with");
        } else if (refused[RD_ND_UNMADE] != 0u) {
            snprintf(why, sizeof(why), "%llu passes found no pipeline of their twin",
                     (unsigned long long)refused[RD_ND_UNMADE]);
        } else if (twin == 0u) {
            snprintf(why, sizeof(why), "no pass drew through them (the plain case refused: %s)",
                     refused[RD_ND_CLIPPED] ? "clipped" : "?");
        } else if (memcmp(a, b, size) != 0) {
            size_t first = 0;
            while (first < size && a[first] == b[first]) {
                first++;
            }
            snprintf(why, sizeof(why), "the pictures differ with them, the first at texel %u,%u",
                     (unsigned)((first / 4u) % t->tw), (unsigned)((first / 4u) / t->tw));
        } else if (refused[RD_ND_ALPHA] == 0u || refused[RD_ND_PLANE] == 0u) {
            snprintf(why, sizeof(why),
                     "a case the CPU must refuse was not refused (alpha %llu, plane %llu)",
                     (unsigned long long)refused[RD_ND_ALPHA],
                     (unsigned long long)refused[RD_ND_PLANE]);
        } else if (!rule) {
            snprintf(why, sizeof(why), "the rule gave a side band's or DATE's pass a twin");
        } else {
            pass = true;
        }
    }
    rd__nd_hold_end();
    const uint32_t released = rd__pipeline_forget_since(pipeMark);
    rd__frame_free(&f);
    meshesFree(mesh);
    free(a);
    free(b);
    t->clearPending = 1; /* the next replay starts SCENE from its clear */
    rd__nd_verdict(pass, why);
    if (pass) {
        rd__log("discard-free twins: on (self-test: %llu pass%s through them, the alpha test's and "
                "the clip plane's refused (%llu and %llu), the side band and DATE refused by the "
                "rule, pictures byte for byte; its %u pipelines released) on SCENE %ux%u at "
                "%ux%u texels in %.1f ms",
                (unsigned long long)twin, twin == 1u ? "" : "es",
                (unsigned long long)refused[RD_ND_ALPHA], (unsigned long long)refused[RD_ND_PLANE],
                (unsigned)released, t->w, t->h, t->tw, t->th, rd__now_ms() - t0);
    } else {
        rd__log("discard-free twins: off for the run (self-test: %s; its %u pipelines released) "
                "on SCENE %ux%u at %ux%u texels in %.1f ms",
                why, (unsigned)released, t->w, t->h, t->tw, t->th, rd__now_ms() - t0);
    }
    rd__nd_start_line();
}

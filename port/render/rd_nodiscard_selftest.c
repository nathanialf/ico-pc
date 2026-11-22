/* rd_nodiscard_selftest.c: the discard-free twins' start-up self-test
 * (rd_nodiscard.h rd__nd_selftest), run by rd_vuprepass_selftest after the
 * zero-area cull's, on SCENE at its real size and scale.
 *
 * Only on a device without dual-source blending by its own features
 * (rd__nd_applicable): elsewhere no pass draws through a *_nodual build,
 * or the two-pass blend is the fallback the blend's self-test chose on a
 * device that has dual-source blending (rd_blend_selftest.c), which stays
 * untouched; the twins are not tried and nothing of them is made ("not
 * applicable here").
 *
 * The frame (built here, never recorded; the zero-area cull's helpers'
 * form): SCENE cleared, Z GEQUAL with Z write, no blend, untextured,
 * Gouraud (the last two cases textured, below); six prelit meshes of two
 * triangles each at model depth 2,
 * where the screen matrix puts a vertex at GS X = 2048 + 8 x, Y = 2048 + 8 y,
 * each drawn once, the first three at code 34 (clip mode none, the one
 * clip mode the twins take):
 *   plain   the alpha test off: the CPU gives the pass its twin
 *   alpha   ATE GREATER 0x40, AFAIL KEEP, the corners' alphas 0, 127 and
 *           127 (the alpha plan cannot decide it): its pixels below the
 *           reference are discarded by the *_nodual build, so the CPU must
 *           refuse the twin
 *   plane   a chain plane through the middle of the mesh (the MESH
 *           command's b[5], f[0]): the pixels past it are discarded, refused
 *   texa    a 4x4 RGB24 texture under TEXA 7F/81 with AEM and a linear
 *           filter (so vu_texa_ps), the test off: through vu_texa_ps_nodual_nd
 *   texa alpha the same under ATE GREATER 0x40 (the black texels' alpha 0):
 *           refused as alpha
 * Both twin entries must draw (each is used in play only if its own case
 * drew through it here, rd__nd_entry_tested; logged per entry) and the mark
 * says "counting" once these pass (rd__nd_floor_passed), before the clipper
 * check.
 *   eye     at code 32 (the region test under the camera rule: the draw has
 *           ICO_VU_NO_BESIDE, the boat's form), a corner of each triangle
 *           behind the eye: a clipper may make the band flag non-finite
 *           there (rd_nodiscard.h, "Why clip mode none"), so the CPU must
 *           refuse it ("clipped") and the pictures stay byte for byte
 * The side band and DATE cannot be drawn under the camera rule's frame
 * here: the rule itself (rd_nd_eligible) must refuse a region test's pass
 * without ICO_VU_NO_BESIDE and a pass with DATE.  The frame is replayed
 * with the twins (the pipelines of both gathered and made first on the
 * workers) and without them; the twins pass when the pictures are the
 * same byte for byte, the plain case drew through a twin, the alpha, plane
 * and eye cases were refused for those reasons, no twin pipeline was missing
 * and the rule refused the band and DATE.  Anything else (a module or a
 * pipeline not made, a device error) leaves them off for the run, releases
 * their modules and pipelines and writes "failed" to the install's mark.
 * Either way every pipeline the self-test made is released after it
 * (rd__pipeline_forget_since): the run's set is the one it had before (the
 * draw pattern gate's counts unchanged), and the twins' pipelines are made
 * again in the background as passes ask for them.
 *
 *   rd: discard-free twins: on (self-test: 1 pass through them, the alpha
 *     test's, the clip plane's and the eye-plane crossing's refused (1, 1
 *     and 1), the side band and DATE refused by the rule, pictures byte for
 *     byte; its 2 pipelines released) on SCENE WxH at TWxTH texels in T ms
 *   rd: discard-free twins: off for the run (self-test: <reason>) ...
 *
 * ICO_RD_VU_NODISCARD_SABOTAGE (tests, rd_nodiscard_run.c): alpha, the
 * CPU's proof does not look at the alpha test (the alpha case then draws
 * through a twin and the pictures differ); module, the modules not made. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "rd_mesh.h"
#include "rd_nodiscard.h"
#include "shader_consts.h"

#define NT_CASES 6
#define NT_VERTS 6 /* two triangles a mesh, three vertices each */
#define NT_Z 2.0f  /* model depth: GS X = 2048 + 8 x */
#define NT_HALF 12 /* a triangle's legs, GS pixels */

enum { NT_PLAIN, NT_ALPHA, NT_PLANE, NT_EYE, NT_TEXA, NT_TEXA_ALPHA };

/* the textured cases' texture: 4x4 RGB24 (its alpha byte the source's, TEXA
   decides it: under 7F/81 with AEM a black texel's alpha is 0, every other
   0x7F), half of it black, so the alpha test GREATER 0x40 can fail; a
   linear filter, so the TEXA is per texel and the pass is vu_texa_ps's
   (rd_pipeline.c rd__texa_per_texel) */
static RdTex makeTexture(void)
{
    uint8_t px[4 * 4 * 4];
    for (int i = 0; i < 16; i++) {
        const bool black = ((i % 4) + (i / 4)) % 2 == 0;
        px[i * 4 + 0] = black ? 0 : (uint8_t)(90 + 40 * (i % 4));
        px[i * 4 + 1] = black ? 0 : (uint8_t)(200 - 30 * (i / 4));
        px[i * 4 + 2] = black ? 0 : 120;
        px[i * 4 + 3] = 0x80;
    }
    return rd_create_texture_src(4, 4, px, RD_TEXSRC_RGB24, "rd discard-free twins texa");
}

/* the textured state: the texture, MODULATE with TCC RGBA, TEXA 7F/81 AEM,
   a linear filter */
static bool pushTexa(RdFrame *f, RdTex tex)
{
    RdCmd *c = rd__st_push(f, 0, RDC_TEXA);
    if (c) {
        c->b[0] = RD_TEXA_7F_81_AEM;
    }
    c = c ? rd__st_push(f, 0, RDC_TEXTURE) : NULL;
    if (c) {
        c->u[0] = tex.id;
        c->b[0] = RD_TEXFN_MODULATE;
        c->b[1] = RD_TCC_RGBA;
    }
    c = c ? rd__st_push(f, 0, RDC_FILTER) : NULL;
    if (c) {
        c->b[0] = RD_FILTER_LINEAR;
        c->b[1] = RD_FILTER_LINEAR;
        c->u[0] = 0;
    }
    return c != NULL;
}

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
    return 2048.0f + 40.0f * ((float)c - 2.5f);
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
        /* the eye case's last corner of each triangle behind the eye (model
           depth -1: h.w = z for this screen matrix), the triangle crossing
           the eye plane */
        const float z = c == NT_EYE && v % 3 == 2 ? -1.0f : NT_Z;
        qw(q[at++], (gx - 2048.0f) / 8.0f, (gy - 2048.0f) / 8.0f, z, 1.0f);
        /* S and T over the texture (the textured cases read them) */
        qw(q[at++], kCorner[v][0] > 0.0f ? 1.0f : 0.0f, kCorner[v][1] > 0.0f ? 1.0f : 0.0f, 1.0f,
           v % 3 == 0 ? 0.0f : 1.0f);
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

/* a MESH command of mesh at code 34 (clip mode none) or 32 (the region
   test: the camera rule's, ICO_VU_NO_BESIDE); plane: the chain plane x >=
   the cell's middle (rd_chain.h: axis x + 1, the plus side; the offset in
   f[0]) */
static bool pushMesh(RdFrame *f, RdMesh mesh, uint8_t code, bool plane, float planeAt, uint32_t key)
{
    uint8_t buf[sizeof(RdVuPayload) + sizeof(RdVuBlock)];
    RdVuPayload p;
    RdVuBlock vu;
    memset(&p, 0, sizeof(p));
    p.code = code;
    p.clip = code == 34 ? RD_VU_CLIP_NONE : RD_VU_CLIP_REGION;
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

static bool buildFrame(RdFrame *f, uint32_t scene, uint32_t w, uint32_t h, const RdMesh *mesh,
                       RdTex tex, int skip)
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
       then the plane with the test off again, then the eye-plane crossing
       at code 32 */
    /* skip: that case left out (its state kept), to show the case drew */
    ok = ok && (skip == NT_PLAIN || pushMesh(f, mesh[NT_PLAIN], 34, false, 0.0f, 1u)) &&
         rd__st_test(f, 0, (uint64_t)RD_TEST_Z_GEQUAL | 0x40Du) &&
         pushMesh(f, mesh[NT_ALPHA], 34, false, 0.0f, 2u) && rd__st_test(f, 0, RD_TEST_Z_GEQUAL) &&
         pushMesh(f, mesh[NT_PLANE], 34, true, (cellX(NT_PLANE) - 2048.0f) / 8.0f, 3u) &&
         pushMesh(f, mesh[NT_EYE], 32, false, 0.0f, 4u);
    /* the textured cases through vu_texa_ps: the test off (its twin), then
       the alpha test on the texels' alphas 0 and 0x7F (refused) */
    ok = ok && pushTexa(f, tex) &&
         (skip == NT_TEXA || pushMesh(f, mesh[NT_TEXA], 34, false, 0.0f, 5u)) &&
         rd__st_test(f, 0, (uint64_t)RD_TEST_Z_GEQUAL | 0x40Du) &&
         pushMesh(f, mesh[NT_TEXA_ALPHA], 34, false, 0.0f, 6u) &&
         rd__st_test(f, 0, RD_TEST_Z_GEQUAL) && rd__st_push(f, 0, RDC_TEXTURE_OFF);
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
    p.clipperClean = true; /* a band refused even where the clipper is clean */
    const bool band = rd_nd_eligible(&p) == RD_ND_BAND;
    /* not vacuous: the same pass with the flag takes the twin on a clean
       clipper, is refused as clipped on any other, and clip mode none takes
       it either way */
    p.drawFlags = ICO_VU_CLIP_REGION | ICO_VU_NO_BESIDE;
    const bool twin = rd_nd_eligible(&p) == RD_ND_TWIN;
    p.clipperClean = false;
    const bool clipped = rd_nd_eligible(&p) == RD_ND_CLIPPED;
    p.drawFlags = ICO_VU_CLIP_NONE;
    const bool none = rd_nd_eligible(&p) == RD_ND_TWIN;
    p.flags = ICO_DF_DATE;
    const bool date = rd_nd_eligible(&p) == RD_ND_DATE;
    return band && twin && clipped && none && date;
}

/* ------------------------------------------------- the clipper check
 * (rd_nodiscard.h, "The clipper check"): camera rule meshes whose
 * triangles cross the eye plane, each drawn at code 32 (the region test with
 * ICO_VU_NO_BESIDE under the camera rule: a dropped triangle facing the eye
 * drawn clipped) and at code 36 (the scissor: its flagged corners in the
 * homogeneous form), every triangle in both windings (the rule draws the
 * one facing the eye), then a depth probe: a code 34 quad over the whole
 * target at GS Z 0 with Z write off, drawn where the depth is still the
 * clear's, so a depth the twin wrote and the build did not shows in the
 * colour.  Each case is a frame of its own (a later case drawn over an
 * earlier one could hide a difference), replayed through the twins and
 * without them.  Every frame runs whatever an earlier one found; each case
 * logs what it should cover and what it drew.  Corners sized to the frame
 * (caseCorners); the screen matrix (sceneBlock) makes h = (16 x + 2048 z,
 * 16 y + 2048 z, 3145728 (z - 1), z): h.w = z, GS X = 2048 + 16 x / z, GS
 * Z below 0 for 0 < z < 1. */
#define NC_VERTS 6

/* A corner: in front of or behind the eye by its screen direction (u, v,
   fractions of R, the frame's half-size in GS pixels: min(w, h) / 2) and
   its model depth z, model x = u R z / 16 and y = v R z / 16, so it lands
   (or, behind the eye, projects through it) at (u, v) R from the frame's
   centre whatever the frame's size; or (NC_M) model space as given.  The
   cases are sized to the frame at run time (issue 179, the second review's
   run: SCENE 912x512, where constants made for 512x448 drew nothing) */
#define NC_M 1.0f
#define NC_R 0.0f

typedef struct NcCase {
    const char *name;
    float c[3][4]; /* u or x, v or y, z, NC_R or NC_M */
} NcCase;

/* the "nearly" offset, a fraction of R: a triangle with an edge on a view
   ray has its plane through the eye, so it covers no pixel on any device,
   and one with an edge near one covers a sliver as wide as the offset; at
   a quarter of R every case covers 57 texel centres or more at 320x224 and
   313 or more at 912x512 (worked out on the CPU with the clip and the
   projection below) */
#define NC_D 0.25f

static const NcCase kClip[] = {
    {"an edge nearly on the centre view ray",
     {{0, 0, 2, NC_R}, {NC_D, NC_D, -1, NC_R}, {0.7f, 0.5f, 2, NC_R}}},
    {"an edge on a view ray, divided x alike",
     {{0, 0.3f, 2, NC_R}, {0, 0.6f, -1, NC_R}, {0.6f, -0.3f, 2, NC_R}}},
    {"an edge on a view ray, divided y alike",
     {{0.3f, 0, 2, NC_R}, {0.6f, 0, -1, NC_R}, {-0.3f, 0.6f, 2, NC_R}}},
    {"an edge nearly on an off-centre view ray",
     {{0.3f, 0.3f, 2, NC_R}, {0.3f + NC_D, 0.3f - NC_D, -1, NC_R}, {-0.4f, 0.6f, 2, NC_R}}},
    {"a corner at w = 0", {{0, 0, 2, NC_R}, {3, 2, 0, NC_M}, {-0.4f, 0.3f, 2, NC_R}}},
    {"a corner just behind the eye (w = -0.001)",
     {{-0.4f, -0.4f, 2, NC_R}, {0.4f, -0.4f, 2, NC_R}, {0, 2, -0.001f, NC_M}}},
    {"a corner far behind the eye (w = -1290)",
     {{-0.4f, -0.4f, 2, NC_R}, {0.4f, -0.4f, 2, NC_R}, {100, 50, -1290, NC_M}}},
    {"two corners behind the eye",
     {{0, 0, 2, NC_R}, {0.8f, -0.4f, -1, NC_R}, {-0.6f, 0.8f, -0.5f, NC_R}}},
    {"every w above 0, clipped at GS Z 0",
     {{-0.5f, -0.5f, 0.5f, NC_R}, {0.5f, -0.5f, 2, NC_R}, {0, 0.5f, 2, NC_R}}},
    {"every w above 0, an edge nearly on a view ray clipped at GS Z 0",
     {{NC_D, 0, 0.25f, NC_R}, {0, 0, 2, NC_R}, {0.6f, 0.4f, 2, NC_R}}},
    {"a corner off the GS range in X (ftoi4 saturated)",
     {{-0.4f, -0.4f, 2, NC_R}, {1.0e6f, 0, 2, NC_M}, {0, 0.4f, 2, NC_R}}},
    {"a sliver nearly on the centre view ray",
     {{0, 0, 2, NC_R}, {NC_D / 4, NC_D, -1, NC_R}, {0.05f, 0, 2, NC_R}}},
    {"a large triangle across the frame and the eye",
     {{-1.5f, -0.3f, 2, NC_R}, {1.5f, -0.3f, 2, NC_R}, {0, 0.6f, -3, NC_R}}},
};
#define NC_CASES ((int)(sizeof(kClip) / sizeof(kClip[0])))
/* the frames: every case at codes 32 and 36, untextured (vu_ps) and textured
   under TEXA (vu_texa_ps) */
#define NC_FRAMES (NC_CASES * 4)
#define NC_CASE(k) ((k) / 4)
#define NC_CODE(k) (((k) / 2) % 2)
#define NC_TEXA(k) ((k) % 2)
static const char *const kFormWords[4] = {"32", "32t", "36", "36t"};

/* case k's three corners in model space on a frame w x h GS pixels (the
   tests' sabotage =empty: case 1 moved off the frame) */
static void caseCorners(int k, uint32_t w, uint32_t h, float out[3][3])
{
    const float r = (float)(w < h ? w : h) * 0.5f;
    for (int v = 0; v < 3; v++) {
        const float *c = kClip[k].c[v];
        if (c[3] == NC_M) {
            out[v][0] = c[0];
            out[v][1] = c[1];
        } else {
            out[v][0] = c[0] * r * c[2] / 16.0f;
            out[v][1] = c[1] * r * c[2] / 16.0f;
        }
        out[v][2] = c[2];
        if (k == 0 && rd__nd_sabotage_empty()) {
            out[v][1] += 1000.0f; /* the tests' case off the frame */
        }
    }
}

/* The texel centres case corners c should cover on SCENE t, by the vertex
   stage's maths: h = (16 x + 2048 z, 16 y + 2048 z, 3145728 (z - 1), z)
   (sceneBlock), clipped where the depth is below GS Z 0 (h.z < 0: the
   GPU's near plane; h.w = z >= 1 > 0 there), projected GS X = h.x / h.w
   and mapped to texels about the frame's centre (GS 2048, the draw offset
   2048 - w / 2: rd_replay.c).  Not the rasteriser's own rules (its grid,
   its tie-breaking, a back-facing winding's drop): an estimate, logged
   beside what was drawn so a mismatch shows itself */
static uint64_t expectedTexels(const float c[3][3], const RdTargetRec *t)
{
    float p[4][2];
    int n = 0;
    for (int i = 0; i < 3; i++) {
        const float *a = c[i], *b = c[(i + 1) % 3];
        const bool ain = a[2] >= 1.0f, bin = b[2] >= 1.0f;
        if (ain && n < 4) {
            p[n][0] = 16.0f * a[0] / a[2];
            p[n][1] = 16.0f * a[1] / a[2];
            n++;
        }
        if (ain != bin && n < 4) {
            const float s = (1.0f - a[2]) / (b[2] - a[2]);
            p[n][0] = 16.0f * (a[0] + s * (b[0] - a[0]));
            p[n][1] = 16.0f * (a[1] + s * (b[1] - a[1]));
            n++;
        }
    }
    if (n < 3 || t->w == 0 || t->h == 0) {
        return 0;
    }
    const double sx = (double)t->tw / t->w, sy = (double)t->th / t->h;
    double lo[2] = {1e30, 1e30}, hi[2] = {-1e30, -1e30};
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < 2; j++) {
            const double g = (double)p[i][j] + (j ? t->h : t->w) * 0.5;
            lo[j] = g < lo[j] ? g : lo[j];
            hi[j] = g > hi[j] ? g : hi[j];
        }
    }
    const double x0 = lo[0] * sx < 0.0 ? 0.0 : lo[0] * sx, y0 = lo[1] * sy < 0.0 ? 0.0 : lo[1] * sy;
    const double x1 = hi[0] * sx > t->tw ? t->tw : hi[0] * sx;
    const double y1 = hi[1] * sy > t->th ? t->th : hi[1] * sy;
    uint64_t count = 0;
    for (uint32_t ty = (uint32_t)y0; (double)ty < y1; ty++) {
        const double gy = ((double)ty + 0.5) / sy - t->h * 0.5;
        for (uint32_t tx = (uint32_t)x0; (double)tx < x1; tx++) {
            const double gx = ((double)tx + 0.5) / sx - t->w * 0.5;
            int sign = 0;
            bool in = true;
            for (int i = 0; i < n && in; i++) {
                const float *a = p[i], *b = p[(i + 1) % n];
                const double e =
                    ((double)b[0] - a[0]) * (gy - a[1]) - ((double)b[1] - a[1]) * (gx - a[0]);
                const int s = e > 0.0 ? 1 : e < 0.0 ? -1 : 0;
                if (s != 0 && sign != 0 && s != sign) {
                    in = false;
                }
                sign = sign != 0 ? sign : s;
            }
            count += in && sign != 0;
        }
    }
    return count;
}

/* a mesh of the prelit layout from corners (NC_VERTS: a triangle in both
   windings, two strips of one triangle each, or the probe's two) */
static RdMesh clipMesh(const float (*c)[3], int n, float shade)
{
    static float q[1 + NC_VERTS * RD_VU_QW_PRELIT][4];
    qw(q[0], fbits(0x8000u | (uint32_t)n), fbits(0x30064000u), fbits(0x512u), 0);
    uint32_t at = 1;
    for (int v = 0; v < n; v++) {
        qw(q[at++], c[v][0], c[v][1], c[v][2], 1.0f);
        /* S and T over the texture (the textured frames read them) */
        qw(q[at++], (float)(v % 2), (float)((v / 2) % 2), 1.0f, v % 3 == 0 ? 0.0f : 1.0f);
        qw(q[at++], shade + (float)(12 * v), 230.0f - shade, (float)(40 + 30 * v), 127.0f);
    }
    const RdVuBatchDesc batch = {0, 0, 0};
    const RdVuMeshDesc d = {(const float (*)[4])q,
                            at,
                            RD_VU_QW_PRELIT,
                            1,
                            &batch,
                            0,
                            "rd discard-free twins clipper check",
                            NULL,
                            0,
                            0};
    return rd_create_vu_mesh(&d);
}

/* case k's frame: the clear, the state, the case at code (32 or 36), the
   probe; mesh.id 0: no case; withProbe false: no probe */
static bool clipFrame(RdFrame *f, uint32_t scene, uint32_t w, uint32_t h, RdMesh mesh, uint8_t code,
                      RdMesh probe, RdTex tex, bool withProbe)
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
    /* the textured form (tex): through vu_texa_ps; the probe untextured */
    if (tex.id) {
        ok = ok && pushTexa(f, tex);
    }
    ok = ok && (mesh.id == 0 || pushMesh(f, mesh, code, false, 0.0f, 1u)) &&
         rd__st_push(f, 0, RDC_TEXTURE_OFF);
    if (withProbe) {
        ok = ok && rd__st_push1(f, 0, RDC_ZWRITE, RD_ZWRITE_OFF) &&
             pushMesh(f, probe, 34, false, 0.0f, 2u);
    }
    return ok;
}

/* the texels of two pictures that differ, and the first (its byte offset
   into *first when not NULL and none was set) */
static uint64_t texelsDiffer(const uint8_t *a, const uint8_t *b, size_t size, size_t *first)
{
    uint64_t n = 0;
    for (size_t i = 0; i + 4u <= size; i += 4u) {
        if (memcmp(a + i, b + i, 4u) != 0) {
            if (n == 0u && first != NULL) {
                *first = i;
            }
            n++;
        }
    }
    return n;
}

/* the texels of a picture that are not the colour at px (the clear's) */
static uint64_t texelsNot(const uint8_t *a, size_t size, const uint8_t px[4])
{
    uint64_t n = 0;
    for (size_t i = 0; i + 4u <= size; i += 4u) {
        n += memcmp(a + i, px, 4u) != 0;
    }
    return n;
}

/* text appended at at (kept below size), the new end */
static size_t appendf(char *out, size_t size, size_t at, const char *fmt, ...)
{
    if (at + 1u >= size) {
        return at;
    }
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(out + at, size - at, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return at;
    }
    return at + (size_t)n < size ? at + (size_t)n : size - 1u;
}

/* a frame replayed without the twins, read into px */
static bool plainRead(RdFrame *f, RdTargetRec *t, uint8_t *px, size_t size)
{
    rd__nd_hold(false);
    const bool ok = replayRead(f, t, px, size);
    rd__frame_free(f);
    return ok;
}

/* The check on SCENE t (a, b: its two pictures' buffers): every frame (13
   cases x codes 32 and 36 x untextured and textured, each case's triangle
   in both windings) is drawn, whatever an earlier one found.  Per case one
   line: the texels it should cover (expectedTexels), the texels each frame
   drew through the ordinary entry (its picture without the twins against
   the probe's alone), the texels drawn at code 32 without the probe
   (against the clear: whether the probe hid the case), and the texels each
   frame's pictures differ by with the twins.  True when every frame drew
   and none differs; else *why names every frame that drew nothing ("not
   tested") and the first that differs.  Only a frame whose case drew
   counts its passes. */
static bool clipperCheck(RdTargetRec *t, uint8_t *a, uint8_t *b, size_t size, RdTex tex, char *why,
                         size_t whySize, uint64_t *clipDraws)
{
    static const uint8_t kCodes[2] = {32, 36};
    static const RdTex kNone = {0};
    static const RdMesh kNoCase = {0};
    RdMesh mesh[NC_CASES];
    uint64_t expect[NC_CASES];
    bool ok = true;
    for (int k = 0; k < NC_CASES; k++) {
        float c[NC_VERTS][3];
        caseCorners(k, t->w, t->h, c);
        expect[k] = expectedTexels((const float (*)[3])c, t);
        /* the other winding, a strip of its own: corners 0, 2, 1 */
        memcpy(c[3], c[0], sizeof(c[3]));
        memcpy(c[4], c[2], sizeof(c[4]));
        memcpy(c[5], c[1], sizeof(c[5]));
        mesh[k] = clipMesh((const float (*)[3])c, NC_VERTS, (float)(30 + 9 * k));
        ok = ok && mesh[k].id;
    }
    /* the probe: the whole target at model depth 1 (GS Z 0), GS X = 2048 +
       16 x there */
    const float hx = (float)(t->w / 2u + 8u) / 16.0f, hy = (float)(t->h / 2u + 8u) / 16.0f;
    const float pc[NC_VERTS][3] = {{-hx, -hy, 1}, {hx, -hy, 1}, {-hx, hy, 1},
                                   {hx, -hy, 1},  {hx, hy, 1},  {-hx, hy, 1}};
    const RdMesh probe = clipMesh(pc, NC_VERTS, 200.0f);
    ok = ok && probe.id;
    const uint32_t scene = rd_target(RD_TARGET_SCENE).id;
    uint8_t *alone = malloc(size), *aloneTex = NULL;
    bool clean = ok && alone != NULL;
    if (!clean) {
        snprintf(why, whySize, "its frames could not be built");
    }
    /* the pipelines first, every frame's, through the twins and not */
    rd__nd_hold(true);
    rd__nd_clipper_check(true);
    for (int k = 0; clean && k < NC_FRAMES; k++) {
        RdFrame f;
        if (clipFrame(&f, scene, t->w, t->h, mesh[NC_CASE(k)], kCodes[NC_CODE(k)], probe,
                      NC_TEXA(k) ? tex : kNone, true)) {
            rd__replay_set_window(UINT32_MAX, 0);
            rd__pipeline_gather(true);
            (void)rd__replay_frame(&f, 0, false);
            rd__pipeline_gather(false);
            rd__replay_set_window(0, 0);
        }
        rd__frame_free(&f);
    }
    rhi_wait_idle();
    if (clean && !rhi_device_lost()) {
        bool stopped = false;
        (void)rd__pipeline_create_gathered(&stopped);
    }
    uint64_t tw = 0, rf[RD_ND_REASONS];
    uint64_t ent[2] = {0, 0}, entAll[2] = {0, 0};
    /* the probe alone, untextured and textured form (the same picture: the
       probe is drawn untextured), and the clear alone (its colour), without
       the twins */
    uint8_t clearPx[4] = {0, 0, 0, 0};
    for (int x = 0; clean && x < 3; x++) {
        RdFrame f;
        uint8_t *dst = x == 0 ? alone : x == 1 ? b : a;
        const bool built = clipFrame(&f, scene, t->w, t->h, kNoCase, kCodes[0], probe,
                                     x == 1 ? tex : kNone, x < 2);
        if (!built) {
            rd__frame_free(&f);
        }
        if (!built || !plainRead(&f, t, dst, size)) {
            snprintf(why, whySize, "its frame of the %s could not be %s",
                     x < 2 ? "probe alone" : "clear alone", built ? "drawn" : "built");
            clean = false;
        } else if (x == 1 && memcmp(alone, b, size) != 0) {
            /* the textured form's own (the probe is untextured in both:
               not expected, kept and said) */
            aloneTex = malloc(size);
            if (aloneTex == NULL) {
                snprintf(why, whySize, "no memory for the probe's textured picture");
                clean = false;
            } else {
                memcpy(aloneTex, b, size);
                rd__log("discard-free twins: the clipper check's probe alone draws differently "
                        "in the textured form (%llu texels): each form compared with its own",
                        (unsigned long long)texelsDiffer(alone, b, size, NULL));
            }
        } else if (x == 2) {
            memcpy(clearPx, a, 4);
        }
    }
    (void)rd__nd_take_clip();
    rd__nd_take_entries_clip(ent);
    rd__nd_take(&tw, rf);
    uint64_t drawn[NC_FRAMES], differ[NC_FRAMES], bare[NC_CASES];
    memset(drawn, 0, sizeof(drawn));
    memset(differ, 0, sizeof(differ));
    memset(bare, 0, sizeof(bare));
    bool device = clean, empty = false, differs = false, unmade = false;
    char diffWhy[96] = "", unmadeWhy[64] = "";
    for (int k = 0; device && k < NC_FRAMES; k++) {
        RdFrame f;
        if (!clipFrame(&f, scene, t->w, t->h, mesh[NC_CASE(k)], kCodes[NC_CODE(k)], probe,
                       NC_TEXA(k) ? tex : kNone, true)) {
            rd__frame_free(&f);
            snprintf(why, whySize, "its frame of case %d could not be built", NC_CASE(k) + 1);
            device = false;
            break;
        }
        rd__nd_hold(true);
        rd__nd_clipper_check(true);
        const bool okA = replayRead(&f, t, a, size);
        const uint64_t frameClip = rd__nd_take_clip();
        rd__nd_take_entries_clip(ent);
        rd__nd_take(&tw, rf);
        if (rf[RD_ND_UNMADE] != 0u && !unmade) {
            snprintf(unmadeWhy, sizeof(unmadeWhy), "case %d at %s: no pipeline of its twin",
                     NC_CASE(k) + 1, kFormWords[k % 4]);
            unmade = true;
        }
        rd__nd_clipper_check(false);
        const bool okB = okA && plainRead(&f, t, b, size);
        rd__frame_free(&f); /* (plainRead freed it when it ran) */
        if (okA && k == 0 && rd__nd_sabotage_clipper()) {
            a[size / 2u] ^= 0x40u; /* the tests' positive control: a difference */
        }
        if (!okA || !okB) {
            snprintf(why, whySize, "the device failed in its replays (case %d at %s)",
                     NC_CASE(k) + 1, kFormWords[k % 4]);
            device = false;
            break;
        }
        size_t first = 0;
        drawn[k] = texelsDiffer(b, NC_TEXA(k) && aloneTex != NULL ? aloneTex : alone, size, NULL);
        differ[k] = texelsDiffer(a, b, size, &first);
        empty = empty || drawn[k] == 0u;
        if (differ[k] != 0u && !differs) {
            snprintf(diffWhy, sizeof(diffWhy), "case %d at %s differs, the first at texel %u,%u",
                     NC_CASE(k) + 1, kFormWords[k % 4], (unsigned)((first / 4u) % t->tw),
                     (unsigned)((first / 4u) / t->tw));
            differs = true;
        }
        if (drawn[k] != 0u) {
            /* the passes of a frame whose case drew (the probe, code 34,
               is not of this class) */
            *clipDraws += frameClip;
            entAll[0] += ent[0];
            entAll[1] += ent[1];
        }
        if (k % 4 == 3) {
            /* the case at code 32 without the probe, against the clear:
               whether the case reached the target at all */
            RdFrame g;
            const int c = NC_CASE(k);
            if (clipFrame(&g, scene, t->w, t->h, mesh[c], kCodes[0], probe, kNone, false) &&
                plainRead(&g, t, b, size)) {
                bare[c] = texelsNot(b, size, clearPx);
            } else {
                rd__frame_free(&g);
            }
            rd__log("discard-free twins: the clipper check, case %d of %d (%s) on SCENE %ux%u at "
                    "%ux%u texels: expected about %llu texels; drawn %llu, %llu, %llu, %llu "
                    "(codes 32, 32 textured, 36, 36 textured, against the probe alone), %llu "
                    "without the probe (code 32, against the clear); differing with the twins "
                    "%llu, %llu, %llu, %llu",
                    c + 1, NC_CASES, kClip[c].name, t->w, t->h, t->tw, t->th,
                    (unsigned long long)expect[c], (unsigned long long)drawn[k - 3],
                    (unsigned long long)drawn[k - 2], (unsigned long long)drawn[k - 1],
                    (unsigned long long)drawn[k], (unsigned long long)bare[c],
                    (unsigned long long)differ[k - 3], (unsigned long long)differ[k - 2],
                    (unsigned long long)differ[k - 1], (unsigned long long)differ[k]);
        }
    }
    if (device && (empty || differs || unmade)) {
        /* every frame that drew nothing, then the first difference */
        size_t at = 0;
        why[0] = '\0';
        if (empty) {
            at = appendf(why, whySize, at, "drew nothing (not tested):");
            bool any = false;
            for (int c = 0; c < NC_CASES; c++) {
                bool named = false;
                for (int x = 0; x < 4; x++) {
                    if (drawn[c * 4 + x] != 0u) {
                        continue;
                    }
                    if (!named) {
                        at = appendf(why, whySize, at, "%scase %d at %s", any ? "; " : " ", c + 1,
                                     kFormWords[x]);
                    } else {
                        at = appendf(why, whySize, at, ", %s", kFormWords[x]);
                    }
                    named = any = true;
                }
            }
        }
        if (differs) {
            at = appendf(why, whySize, at, "%s%s", at ? "; " : "", diffWhy);
        }
        if (unmade) {
            at = appendf(why, whySize, at, "%s%s", at ? "; " : "", unmadeWhy);
        }
        clean = false;
    } else if (!device) {
        clean = false;
    }
    if (clean && *clipDraws == 0u) {
        snprintf(why, whySize, "no camera rule pass of its frames drew through a twin");
        clean = false;
    } else if (clean && (entAll[0] == 0u || entAll[1] == 0u)) {
        snprintf(why, whySize, "no case of its frames drew through %s",
                 entAll[0] == 0u ? "vu_ps_nodual_nd" : "vu_texa_ps_nodual_nd");
        clean = false;
    }
    rd__nd_clipper_check(false);
    free(alone);
    free(aloneTex);
    for (int k = 0; k < NC_CASES; k++) {
        if (mesh[k].id) {
            rd_destroy_vu_mesh(mesh[k]);
        }
    }
    if (probe.id) {
        rd_destroy_vu_mesh(probe);
    }
    t->clearPending = 1;
    return clean;
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
    if (rd__nd_not_applicable_text() != NULL) {
        rd__nd_selftest_skip(rd__nd_not_applicable_text());
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
    const RdTex tex = makeTexture();
    bool meshes = true;
    for (int i = 0; i < NT_CASES; i++) {
        meshes = meshes && mesh[i].id;
    }
    if (!meshes || !tex.id || !a || !b || !buildFrame(&f, scene, t->w, t->h, mesh, tex, -1)) {
        /* nothing of the twins tried yet: not the device's verdict */
        rd__frame_free(&f);
        meshesFree(mesh);
        if (tex.id) {
            rd_destroy_texture(tex);
        }
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
    uint64_t twin = 0, refused[RD_ND_REASONS], entries[2] = {0, 0};
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
        rd__nd_take_entries(entries);
        const bool okA = replayRead(&f, t, a, size);
        rd__nd_take_entries(entries);
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
        } else if (refused[RD_ND_ALPHA] < 2u || refused[RD_ND_PLANE] == 0u ||
                   refused[RD_ND_CLIPPED] == 0u) {
            snprintf(why, sizeof(why),
                     "a case the CPU must refuse was not refused (alpha %llu, plane %llu, "
                     "clipped %llu)",
                     (unsigned long long)refused[RD_ND_ALPHA],
                     (unsigned long long)refused[RD_ND_PLANE],
                     (unsigned long long)refused[RD_ND_CLIPPED]);
        } else if (!rule) {
            snprintf(why, sizeof(why), "the rule gave a side band's or DATE's pass a twin");
        } else {
            pass = true;
        }
        /* the cases through a twin drew (a case that drew nothing compares
           equal and tests nothing): the frame without each, without the
           twins, must differ from b */
        static const int kDrew[2] = {NT_PLAIN, NT_TEXA};
        for (int i = 0; pass && i < 2; i++) {
            RdFrame g;
            const bool built = buildFrame(&g, scene, t->w, t->h, mesh, tex, kDrew[i]);
            const bool read = built && replayRead(&g, t, a, size);
            rd__frame_free(&g);
            if (!read) {
                snprintf(why, sizeof(why), "the device failed in the replay without its %s case",
                         i ? "textured" : "plain");
                pass = false;
            } else if (memcmp(a, b, size) == 0) {
                snprintf(why, sizeof(why), "its %s case drew nothing: not tested",
                         i ? "textured" : "plain");
                pass = false;
            }
        }
        rd__nd_take(&twinB, refusedB);
    }
    /* the floor passed: the device's clipper measured (a property of the
       device, never a fault: "not clean" keeps the floor alone) */
    char clipWhy[400] = "";
    uint64_t clipDraws = 0;
    bool clipClean = false;
    if (pass) {
        /* each entry's twin is used in play only if its own case drew
           through it here (vu_ps: the plain case, vu_texa_ps: the textured
           one); the mark says "counting" from here */
        for (int e = 0; e < 2; e++) {
            rd__nd_entry_tested(e, entries[e] != 0u);
            if (entries[e] == 0u) {
                rd__log("discard-free twins: %s drew nothing in the self-test: not used in play",
                        rd_nd_module_name(e == 0 ? (uint8_t)RD_FS_VU : (uint8_t)RD_FS_VU_TEXA));
            }
        }
        rd__nd_floor_passed();
        clipClean = clipperCheck(t, a, b, size, tex, clipWhy, sizeof(clipWhy), &clipDraws);
        rd__nd_clipper_verdict(clipClean, clipClean ? "" : clipWhy);
    }
    rd__nd_hold_end();
    const uint32_t released = rd__pipeline_forget_since(pipeMark);
    rd__frame_free(&f);
    meshesFree(mesh);
    rd_destroy_texture(tex);
    free(a);
    free(b);
    t->clearPending = 1; /* the next replay starts SCENE from its clear */
    rd__nd_verdict(pass, why);
    if (pass) {
        rd__log("discard-free twins: on (self-test: %llu pass%s through them, %llu through "
                "vu_ps_nodual_nd and %llu through vu_texa_ps_nodual_nd, the alpha test's, "
                "the clip plane's and the eye-plane crossing's refused (%llu, %llu and %llu), "
                "the side band and DATE refused by the rule, pictures byte for byte; its %u "
                "pipelines released) on SCENE %ux%u at %ux%u texels in %.1f ms",
                (unsigned long long)twin, twin == 1u ? "" : "es", (unsigned long long)entries[0],
                (unsigned long long)entries[1], (unsigned long long)refused[RD_ND_ALPHA],
                (unsigned long long)refused[RD_ND_PLANE],
                (unsigned long long)refused[RD_ND_CLIPPED], (unsigned)released, t->w, t->h, t->tw,
                t->th, rd__now_ms() - t0);
        const bool clean = rd__nd_clipper() == RD_ND_CLIP_CLEAN;
        if (clean && clipClean) {
            rd__log("discard-free twins: the clipper check: clean (%d cases crossing the eye "
                    "plane at codes 32 and 36, untextured and textured under TEXA, %llu passes "
                    "through both twins, the pictures and their depth byte for byte): the camera "
                    "rule's passes take them too",
                    NC_CASES, (unsigned long long)clipDraws);
        } else if (clean) {
            rd__log("discard-free twins: the clipper check: clean (ICO_RD_VU_NODISCARD_CLIPPER; "
                    "measured not clean: %s): the camera rule's passes take them too",
                    clipWhy);
        } else {
            rd__log("discard-free twins: the clipper check: not clean (%s): clip mode none alone "
                    "takes them (a property of this device, not a fault: nothing written as "
                    "failed)",
                    clipClean ? "ICO_RD_VU_NODISCARD_CLIPPER; measured clean" : clipWhy);
        }
    } else {
        rd__log("discard-free twins: off for the run (self-test: %s; its %u pipelines released) "
                "on SCENE %ux%u at %ux%u texels in %.1f ms",
                why, (unsigned)released, t->w, t->h, t->tw, t->th, rd__now_ms() - t0);
    }
    rd__nd_start_line();
}

/* rd_ratecard_run.c: the rate card on the device (rd_ratecard.h; docs/port/
 * RENDER_PERF.md, "The rate card").
 *
 * The frames, each on SCENE at its real size and scale, cleared (colour and
 * depth 0), Z GEQUAL, Z written, ABE off, the twins' self-test's state
 * (rd_nodiscard_selftest.c buildFrame: so every pipeline is one that start
 * already made on this device), prelit code 34 meshes under its screen
 * matrix (GS X = 2048 + 16 x / z):
 *   E      the clear alone: a pass with nothing drawn
 *   L2     two full-target layers at the same depth (both pass GEQUAL)
 *   L2t    the same under TEXA 7F/81 AEM, a linear filter (vu_texa_ps)
 *   L1     one full-target layer
 *   H2     a near layer, then a far one (every fragment of it fails depth)
 *   Bl     one full-target layer blended (LERP As, Z not written): on a
 *          device without dual-source blending the two-pass blend
 *   V      RC_REPEAT draws of RC_TRIS triangles 100 GS pixels wide, left of
 *          the target (no coverage, kept by the zero-area cull: their boxes
 *          span more than its 4 samples)
 *   S, B   RC_REPEAT draws of a 64 x 64 GS pixel square at the target's
 *          centre in 2 x 2 cells (2048 triangles), or in 2 triangles
 * Each frame once untimed (its pipelines gathered and made on the workers,
 * the twins' with them), then RD_RC_SAMPLES timed replays; the twin frames
 * (L2, L1 and H2 again) under the twins' hold (rd__nd_hold(true): only where the
 * twins are on), the others with it off.  The times: each replay's GPU time
 * (rd__perf_gpu_ms_of: its first timestamp to its last), read once
 * RHI_FRAMES_IN_FLIGHT more replays of E brought them in.  The units:
 *   a shaded fragment         (L2 - E) / (2 A), A = SCENE's texels
 *   through the twin          the same, L2 under the hold
 *   a hidden fragment         (H2 - L1) / A, with the hold off and on (H2 and
 *                             L1 both through the twin: the visible layer
 *                             subtracted costs what H2's own does)
 *   a vertex                  (V - E) / (3 RC_TRIS RC_REPEAT)
 *   a triangle binned         (S - B) / ((2048 - 2) RC_REPEAT)
 *   the 2x2-quad factor       (S - E) / (B - E)
 *   a pass with nothing drawn E
 *   a blended fragment        (Bl - E) / A, over a shaded one
 * Its own pipelines are released after it (rd__pipeline_forget_since), its
 * draws not counted as the twins' (the hold), SCENE starts from its clear
 * after it.  The block's lines and the result file: rd_ratecard.h. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <io.h> /* _commit */
#include <process.h>
#else
#include <unistd.h> /* fsync, _exit */
#endif

#include "rd_fastps.h"
#include "host_fs.h" /* ico_fopen: UTF-8 paths on Windows */
#include "rd_internal.h"
#include "rd_mesh.h"
#include "rd_nodiscard.h"
#include "rd_onepass.h" /* held at the two-pass blend through the card */
#include "rd_ratecard.h"
#include "shader_consts.h"

#define RC_TRIS 2048 /* a vertex or small-triangle mesh's triangles */
#define RC_BATCH 32  /* triangles a batch */
#define RC_REPEAT 8  /* draws of it a frame */
#define RC_SQUARE 64 /* the small and large meshes' side, GS pixels */
#define RC_CELL 2    /* the small mesh's cell, GS pixels */
#define RC_BUDGET_MS 1500.0
#define RC_EXIT_END 87
#define RC_LAYER_Z_NEAR 3.0f
#define RC_LAYER_Z_FAR 2.0f

enum {
    F_E,
    F_L2,
    F_L2T,
    F_L1,
    F_H2,
    F_BL,
    F_V,
    F_S,
    F_B,
    F_L2_TWIN,
    F_L1_TWIN, /* the twin hidden fragment's baseline: L1 under the twins' hold */
    F_H2_TWIN,
    F_L2R,        /* two layers on the fast build's form (RGBA32, MODULATE, TCC), its hold off */
    F_L2R_FAST,   /* the same, the fast pixel build's hold on */
    F_L2R_FASTND, /* the same, the twins' hold on too (vu_ps_nodual_fast_nd) */
    F_COUNT
};

static const char *const kFrameWords[F_COUNT] = {"the clear",
                                                 "two layers",
                                                 "two textured layers",
                                                 "one layer",
                                                 "near then far",
                                                 "a blended layer",
                                                 "off-target vertices",
                                                 "small triangles",
                                                 "large triangles",
                                                 "two layers through the twin",
                                                 "one layer through the twin",
                                                 "near then far through the twin",
                                                 "two RGBA32 layers",
                                                 "two RGBA32 layers through the fast build",
                                                 "two RGBA32 layers through its discard-free form"};

static RdRateCard s_card;

/* a frame drawn under the twins' hold (only where the twins are on) */
static bool twinFrameOf(int k)
{
    return k == F_L2_TWIN || k == F_L1_TWIN || k == F_H2_TWIN || k == F_L2R_FASTND;
}

/* a frame drawn under the fast pixel build's hold (only where it is on) */
static bool fastFrameOf(int k)
{
    return k == F_L2R_FAST || k == F_L2R_FASTND;
}

bool rd_rate_card_get(RdRateCard *out)
{
    if (!s_card.have || out == NULL) {
        return false;
    }
    *out = s_card;
    return true;
}

/* ------------------------------------------------------------ the meshes */

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

/* rd_nodiscard_selftest.c sceneBlock's VU memory (h = (16 x + 2048 z,
   16 y + 2048 z, 3145728 (z - 1), z)) */
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

/* a prelit mesh of n triangles from GS corners (gx, gy) at model depth z,
   RC_BATCH triangles a batch, each triangle a strip of its own */
static RdMesh meshOf(const float (*gs)[2], uint32_t n, float z, const char *name)
{
    const uint32_t nb = (n + RC_BATCH - 1u) / RC_BATCH;
    const uint32_t qwCount = nb + n * 3u * RD_VU_QW_PRELIT;
    float (*q)[4] = calloc(qwCount, sizeof(*q));
    RdVuBatchDesc *bd = calloc(nb, sizeof(*bd));
    RdMesh m = {0};
    if (q == NULL || bd == NULL) {
        free(q);
        free(bd);
        return m;
    }
    uint32_t at = 0;
    for (uint32_t b = 0; b < nb; b++) {
        const uint32_t t0 = b * RC_BATCH, tn = n - t0 < RC_BATCH ? n - t0 : RC_BATCH;
        bd[b].firstQw = at;
        qw(q[at++], fbits(0x8000u | (tn * 3u)), fbits(0x30064000u), fbits(0x512u), 0);
        for (uint32_t v = 0; v < tn * 3u; v++) {
            const float *p = gs[t0 * 3u + v];
            /* GS X = 2048 + 16 x / z at depth z */
            qw(q[at++], (p[0] - 2048.0f) * z / 16.0f, (p[1] - 2048.0f) * z / 16.0f, z, 1.0f);
            qw(q[at++], (float)(v % 2), (float)((v / 2) % 2), 1.0f, v % 3 == 0 ? 0.0f : 1.0f);
            qw(q[at++], 160.0f, 120.0f, 80.0f, 127.0f);
        }
    }
    const RdVuMeshDesc d = {
        (const float (*)[4])q, at, RD_VU_QW_PRELIT, nb, bd, 0, name, NULL, 0, 0};
    m = rd_create_vu_mesh(&d);
    free(q);
    free(bd);
    return m;
}

/* the full target at depth z: two triangles 8 GS pixels past each side */
static RdMesh layerMesh(uint32_t w, uint32_t h, float z)
{
    const float x0 = 2048.0f - (float)(w / 2u + 8u), x1 = 2048.0f + (float)(w / 2u + 8u);
    const float y0 = 2048.0f - (float)(h / 2u + 8u), y1 = 2048.0f + (float)(h / 2u + 8u);
    const float c[6][2] = {{x0, y0}, {x1, y0}, {x0, y1}, {x1, y0}, {x1, y1}, {x0, y1}};
    return meshOf(c, 2, z, "rd rate card layer");
}

/* RC_TRIS triangles 100 GS pixels wide, left of every target (GS X 200 to
   300: a target's left edge is at 2048 - w / 2 for w up to 3496) */
static RdMesh vertexMesh(void)
{
    static float c[RC_TRIS * 3][2];
    for (uint32_t t = 0; t < RC_TRIS; t++) {
        const float y = 1900.0f + (float)(t % 64u) * 4.0f;
        c[t * 3 + 0][0] = 200.0f, c[t * 3 + 0][1] = y;
        c[t * 3 + 1][0] = 300.0f, c[t * 3 + 1][1] = y;
        c[t * 3 + 2][0] = 200.0f, c[t * 3 + 2][1] = y + 100.0f;
    }
    return meshOf((const float (*)[2])c, RC_TRIS, 2.0f, "rd rate card vertices");
}

/* the centre's RC_SQUARE square in RC_CELL cells (small) or in two
   triangles (large) */
static RdMesh squareMesh(bool small)
{
    static float c[RC_TRIS * 3][2];
    const float o = 2048.0f - (float)(RC_SQUARE / 2);
    if (!small) {
        const float x0 = o, x1 = o + RC_SQUARE, y0 = o, y1 = o + RC_SQUARE;
        const float q[6][2] = {{x0, y0}, {x1, y0}, {x0, y1}, {x1, y0}, {x1, y1}, {x0, y1}};
        return meshOf(q, 2, 2.0f, "rd rate card large triangles");
    }
    const uint32_t side = RC_SQUARE / RC_CELL;
    uint32_t t = 0;
    for (uint32_t j = 0; j < side; j++) {
        for (uint32_t i = 0; i < side && t + 2u <= RC_TRIS; i++) {
            const float x0 = o + (float)(i * RC_CELL), y0 = o + (float)(j * RC_CELL);
            const float x1 = x0 + RC_CELL, y1 = y0 + RC_CELL;
            const float q[6][2] = {{x0, y0}, {x1, y0}, {x0, y1}, {x1, y0}, {x1, y1}, {x0, y1}};
            memcpy(c[t * 3], q, sizeof(q));
            t += 2u;
        }
    }
    return meshOf((const float (*)[2])c, t, 2.0f, "rd rate card small triangles");
}

/* ------------------------------------------------------------ the frames */

static bool pushMesh(RdFrame *f, RdMesh mesh, uint32_t batches, uint32_t key)
{
    uint8_t buf[sizeof(RdVuPayload) + sizeof(RdVuBlock)];
    RdVuPayload p;
    RdVuBlock vu;
    memset(&p, 0, sizeof(p));
    p.code = 34;
    p.clip = RD_VU_CLIP_NONE;
    p.prog = RD_PROG_PRELIT;
    p.batchCount = batches;
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
    c->keyLo = key;
    return true;
}

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

/* a 4x4 RGB24 texture, no black texel: under TEXA 7F/81 with AEM every
   alpha 0x7F (opaque to the test, which is off) */
static RdTex makeTexture(void)
{
    uint8_t px[4 * 4 * 4];
    for (int i = 0; i < 16; i++) {
        px[i * 4 + 0] = (uint8_t)(90 + 40 * (i % 4));
        px[i * 4 + 1] = (uint8_t)(200 - 30 * (i / 4));
        px[i * 4 + 2] = 120;
        px[i * 4 + 3] = 0x80;
    }
    return rd_create_texture_src(4, 4, px, RD_TEXSRC_RGB24, "rd rate card texa");
}

/* a 4x4 RGBA32 texture, alpha 0x80: the fast pixel build's own form */
static RdTex makeTextureRgba(void)
{
    uint8_t px[4 * 4 * 4];
    for (int i = 0; i < 16; i++) {
        px[i * 4 + 0] = (uint8_t)(70 + 40 * (i % 4));
        px[i * 4 + 1] = (uint8_t)(210 - 30 * (i / 4));
        px[i * 4 + 2] = 100;
        px[i * 4 + 3] = 0x80;
    }
    return rd_create_texture_src(4, 4, px, RD_TEXSRC_RGBA32, "rd rate card rgba");
}

/* the textured state of the fast build's form: MODULATE with TCC, linear */
static bool pushRgba(RdFrame *f, RdTex tex)
{
    RdCmd *c = rd__st_push(f, 0, RDC_TEXTURE);
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

typedef struct RcMeshes {
    RdMesh nearL, farL, vert, small, large;
    uint32_t vertBatches, smallBatches;
    RdTex tex, texRgba;
} RcMeshes;

/* frame k (F_*) on SCENE (w x h GS pixels) */
static bool buildFrame(RdFrame *f, int k, uint32_t scene, uint32_t w, uint32_t h, const RcMeshes *m)
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
    switch (k) {
    case F_E:
        break;
    case F_L2:
    case F_L2_TWIN:
        ok = ok && pushMesh(f, m->nearL, 1, 1u) && pushMesh(f, m->nearL, 1, 2u);
        break;
    case F_L2T:
        ok = ok && pushTexa(f, m->tex) && pushMesh(f, m->nearL, 1, 1u) &&
             pushMesh(f, m->nearL, 1, 2u) && rd__st_push(f, 0, RDC_TEXTURE_OFF);
        break;
    case F_L1:
    case F_L1_TWIN:
        ok = ok && pushMesh(f, m->nearL, 1, 1u);
        break;
    case F_L2R:
    case F_L2R_FAST:
    case F_L2R_FASTND:
        ok = ok && pushRgba(f, m->texRgba) && pushMesh(f, m->nearL, 1, 1u) &&
             pushMesh(f, m->nearL, 1, 2u) && rd__st_push(f, 0, RDC_TEXTURE_OFF);
        break;
    case F_H2:
    case F_H2_TWIN:
        ok = ok && pushMesh(f, m->nearL, 1, 1u) && pushMesh(f, m->farL, 1, 2u);
        break;
    case F_BL: {
        RdCmd *bl = ok ? rd__st_push(f, 0, RDC_BLEND) : NULL;
        if (bl) {
            bl->b[0] = RD_BLEND_LERP_AS;
            bl->b[1] = 0x80;
            bl->b[2] = 1;
        }
        ok = ok && bl && rd__st_push1(f, 0, RDC_ZWRITE, RD_ZWRITE_OFF) &&
             pushMesh(f, m->nearL, 1, 1u);
        break;
    }
    case F_V:
    case F_S:
    case F_B:
        for (uint32_t r = 0; ok && r < RC_REPEAT; r++) {
            ok = k == F_V   ? pushMesh(f, m->vert, m->vertBatches, 1u + r)
                 : k == F_S ? pushMesh(f, m->small, m->smallBatches, 1u + r)
                            : pushMesh(f, m->large, 1, 1u + r);
        }
        break;
    default:
        ok = false;
        break;
    }
    return ok;
}

/* ------------------------------------------------------------ the file */

static bool readFile(const char *path, char *out, size_t cap)
{
    out[0] = '\0';
    FILE *f = path ? ico_fopen(path, "rb") : NULL;
    if (f == NULL) {
        return false;
    }
    const size_t n = fread(out, 1, cap - 1u, f);
    out[n] = '\0';
    fclose(f);
    return true;
}

static void writeFile(const char *path, const char *text)
{
    FILE *f = path ? ico_fopen(path, "wb") : NULL;
    if (f == NULL) {
        if (path) {
            rd__log("rate card: its file %s could not be written", path);
        }
        return;
    }
    (void)fputs(text, f);
    (void)fflush(f);
#if defined(_WIN32)
    (void)_commit(_fileno(f));
#else
    (void)fsync(fileno(f));
#endif
    fclose(f);
}

static void nowText(char *out, size_t cap)
{
    const time_t t = time(NULL);
    struct tm tmv;
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    if (strftime(out, cap, "%Y-%m-%d %H:%M", &tmv) == 0 && cap > 0) {
        out[0] = '\0';
    }
}

/* each line of text logged, suffix after it */
static void logLines(const char *text, const char *suffix)
{
    const char *p = text;
    while (p && *p) {
        const size_t n = strcspn(p, "\n");
        if (n > 0) {
            rd__log("%.*s%s", (int)n, p, suffix);
        }
        p += n;
        if (*p == '\n') {
            p++;
        }
    }
}

/* ------------------------------------------------------------ the card */

static void label(RdRcItem *it, const char *text, const char *unit)
{
    snprintf(it->label, sizeof(it->label), "%s", text);
    snprintf(it->unit, sizeof(it->unit), "%s", unit);
}

/* the card measured into c (an item that could not be timed says why) */
static void measure(RdRateCard *c, RdTargetRec *t, double budgetMs)
{
    const uint32_t scene = rd_target(RD_TARGET_SCENE).id;
    const bool noDual = g_rd.noDual;
    const char *sfx = noDual ? "_nodual" : "";
    char text[112];
    snprintf(text, sizeof(text), "a shaded fragment, vu_ps%s", sfx);
    label(&c->item[RD_RC_SHADE], text, "ns");
    snprintf(text, sizeof(text), "a shaded fragment, vu_texa_ps%s (TEXA, a linear filter)", sfx);
    label(&c->item[RD_RC_SHADE_TEXA], text, "ns");
    label(&c->item[RD_RC_SHADE_TWIN], "a shaded fragment, the twin vu_ps_nodual_nd", "ns");
    /* the fast pixel build (rd_fastps.h): its form through the ordinary
       build, then through it, then through its discard-free form */
    snprintf(text, sizeof(text), "a shaded fragment, vu_ps%s on the fast build's form (RGBA32)",
             sfx);
    label(&c->item[RD_RC_SHADE_RGBA], text, "ns");
    label(&c->item[RD_RC_SHADE_FAST], "a shaded fragment, the fast pixel build vu_ps_nodual_fast",
          "ns");
    label(&c->item[RD_RC_SHADE_FAST_ND],
          "a shaded fragment, its discard-free form vu_ps_nodual_fast_nd", "ns");
    snprintf(text, sizeof(text), "a fragment hidden by depth, vu_ps%s with its discard", sfx);
    label(&c->item[RD_RC_HIDDEN], text, "ns");
    label(&c->item[RD_RC_HIDDEN_TWIN], "a fragment hidden by depth, through the twin", "ns");
    snprintf(text, sizeof(text), "a vertex (a corner drawn off the target, the VU pre-pass %s)",
             rd_vu_prepass_on() ? "on" : "off");
    label(&c->item[RD_RC_VERTEX], text, "ns");
    label(&c->item[RD_RC_TRIANGLE],
          "a triangle binned (2048 in 2x2 GS pixel cells against 2, the same 64x64 square)", "ns");
    label(&c->item[RD_RC_QUAD],
          "the 2x2-quad factor (the small triangles' cost a covered texel over the large ones')",
          "x");
    label(&c->item[RD_RC_PASS], "a pass with nothing drawn (SCENE cleared, a replay's span)", "ms");
    label(&c->item[RD_RC_PASS_MB], "a MB loaded or stored", "ms");
    snprintf(text, sizeof(text), "a blended fragment (LERP As, %s)",
             noDual ? "the two-pass blend" : "the dual-source blend");
    label(&c->item[RD_RC_BLEND], text, "ns");
    label(&c->item[RD_RC_BLEND_RATIO], "the blended fragment over the opaque one", "x");
    c->item[RD_RC_PASS_MB].u = rd_rc_none(
        "one target size: the load and store are not separable from the pass's own cost");

    /* the twins: their frames only where they are on (their self-test
       passed on this device) */
    const bool twins = rd__nd_on();
    const char *twinWhy = twins ? "" : rd__nd_not_applicable_text();
    char noTwin[120];
    snprintf(noTwin, sizeof(noTwin), "the twins %s%s%.80s", rd__nd_state_text(),
             twinWhy && twinWhy[0] ? ": " : "", twinWhy ? twinWhy : "");
    /* the fast pixel build's frames only where it is on (its self-test
       passed), its discard-free form's only where the twins are on too */
    const bool fast = rd__fps_on();
    const char *fastWhy = fast ? "" : rd__fps_not_applicable_text();
    char noFast[120];
    snprintf(noFast, sizeof(noFast), "the fast pixel build %s%s%.80s", rd__fps_state_text(),
             fastWhy && fastWhy[0] ? ": " : "", fastWhy ? fastWhy : "");

    RcMeshes m;
    memset(&m, 0, sizeof(m));
    m.nearL = layerMesh(t->w, t->h, RC_LAYER_Z_NEAR);
    m.farL = layerMesh(t->w, t->h, RC_LAYER_Z_FAR);
    m.vert = vertexMesh();
    m.small = squareMesh(true);
    m.large = squareMesh(false);
    m.vertBatches = (RC_TRIS + RC_BATCH - 1u) / RC_BATCH;
    m.smallBatches = m.vertBatches;
    m.tex = makeTexture();
    m.texRgba = makeTextureRgba();
    const bool made = m.nearL.id && m.farL.id && m.vert.id && m.small.id && m.large.id &&
                      m.tex.id != 0 && m.texRgba.id != 0;
    const uint32_t pipeMark = rd__pipeline_mark();
    const bool wasBlocked = g_rd.msaaBlocked;
    g_rd.msaaBlocked = true;
    rd__nd_hold(false);
    rd__fps_hold(false);
    /* the one-pass blend held at today's form (rd_onepass.h: its check's
       reference, every pair in two passes): the blended layer is the
       two-pass blend's, as the card says */
    rd__op_check_arm(RD_OP_ARM_REF, false);
    double ms[F_COUNT][RD_RC_SAMPLES];
    uint32_t got[F_COUNT];
    uint32_t ids[F_COUNT][RD_RC_SAMPLES];
    memset(got, 0, sizeof(got));
    bool twinOk[F_COUNT];
    memset(twinOk, 0, sizeof(twinOk));
    const char *stop = made ? NULL : "its meshes could not be made";
    RdRcStat st[F_COUNT];
    memset(st, 0, sizeof(st));
    /* the clear's frame, replayed to bring each frame's stamps in */
    RdFrame e;
    const bool fe = made && buildFrame(&e, F_E, scene, t->w, t->h, &m);
    /* every frame's pipelines first, untimed and outside the budget (REV4R
       N3): the ordinary ones gathered and made on the workers (the twins'
       and the fast build's with them under their holds), then the fast
       build's by play's own path (its keys are asked for at a pass's
       DrawCB in the draw, which the gathering replay does not reach for
       every pass: a plain replay asks, the queue is made at once,
       rd__pipeline_twin_tick(true), twice) */
    const double tm0 = rd__now_ms();
    uint32_t madeFrames = 0;
    for (int k = 0; stop == NULL && k < F_COUNT; k++) {
        const bool fastFrame = fastFrameOf(k);
        const bool twinFrame = twinFrameOf(k);
        if ((twinFrame && !twins) || (fastFrame && !fast)) {
            continue;
        }
        RdFrame f;
        if (!buildFrame(&f, k, scene, t->w, t->h, &m)) {
            rd__frame_free(&f);
            stop = "its frames could not be built";
            break;
        }
        rd__nd_hold(twinFrame);
        rd__fps_hold(fastFrame);
        rd__replay_set_window(UINT32_MAX, 0);
        rd__pipeline_gather(true);
        (void)rd__replay_frame(&f, 0, false);
        rd__pipeline_gather(false);
        rd__replay_set_window(0, 0);
        rhi_wait_idle();
        bool stopped = false;
        (void)rd__pipeline_create_gathered(&stopped);
        for (int round = 0; fastFrame && round < 2 && !rhi_device_lost(); round++) {
            (void)rd__replay_frame(&f, 0, false);
            rhi_wait_idle();
            rd__pipeline_twin_tick(true);
        }
        rd__frame_free(&f);
        rd__fps_hold(false);
        rd__nd_hold(false);
        madeFrames++;
        if (rhi_device_lost()) {
            stop = "the device failed in its replays";
        }
    }
    const double tb = rd__now_ms();
    rd__log("rate card: the pipelines of its %u frames made in %.1f ms before the timed frames "
            "(outside its budget of %.0f ms, which is checked before each timed frame: a frame "
            "begun under it runs to its end, %u timed replays and %u more)",
            (unsigned)madeFrames, tb - tm0, budgetMs, (unsigned)RD_RC_SAMPLES,
            (unsigned)RHI_FRAMES_IN_FLIGHT);
    for (int k = 0; stop == NULL && k < F_COUNT; k++) {
        const bool fastFrame = fastFrameOf(k);
        const bool twinFrame = twinFrameOf(k);
        if ((twinFrame && !twins) || (fastFrame && !fast)) {
            continue;
        }
        if (rd__now_ms() - tb > budgetMs) {
            stop = "the card's time budget ran out";
            break;
        }
        RdFrame f;
        if (!buildFrame(&f, k, scene, t->w, t->h, &m)) {
            rd__frame_free(&f);
            stop = "its frames could not be built";
            break;
        }
        rd__nd_hold(twinFrame);
        rd__fps_hold(fastFrame);
        uint64_t tw = 0, rf[RD_ND_REASONS];
        rd__nd_take(&tw, rf);
        uint64_t fw = 0, ff[RD_FPS_REASONS], fe2[2];
        rd__fps_take(&fw, ff);
        rd__fps_take_entries(fe2);
        twinOk[k] = true;
        for (uint32_t s = 0; s < RD_RC_SAMPLES; s++) {
            if (!rd__replay_frame(&f, 0, false) || rhi_device_lost()) {
                stop = "the device failed in its replays";
                break;
            }
            ids[k][got[k]++] = g_rdPerf.replay;
            rd__nd_take(&tw, rf);
            rd__fps_take(&fw, ff);
            rd__fps_take_entries(fe2);
            /* a twin frame drew through its twins, every other none; a fast
               frame through its module alone, every other through neither */
            if (twinFrame ? (tw == 0u || rf[RD_ND_UNMADE] != 0u) : tw != 0u) {
                twinOk[k] = false;
            }
            const int fm = k == F_L2R_FASTND ? 1 : 0;
            if (fastFrame ? (fe2[fm] == 0u || fe2[1 - fm] != 0u || ff[RD_FPS_UNMADE] != 0u)
                          : fw != 0u) {
                twinOk[k] = false;
            }
        }
        rd__frame_free(&f);
        rd__fps_hold(false);
        /* this frame's stamps come in RHI_FRAMES_IN_FLIGHT replays later:
           read them now, while their records are still queued (rd_perf.c
           keeps the last RD_PERF_QUEUE, 64: a card's whole run is more) */
        rd__nd_hold(false);
        for (int r = 0; stop == NULL && fe && r < RHI_FRAMES_IN_FLIGHT; r++) {
            if (!rd__replay_frame(&e, 0, false) || rhi_device_lost()) {
                stop = "the device failed in its replays";
            }
        }
        uint32_t n = 0;
        for (uint32_t s = 0; s < got[k]; s++) {
            double v;
            if (rd__perf_gpu_ms_of(ids[k][s], &v) && v > 0.0) {
                ms[k][n++] = v;
            }
        }
        st[k] = rd_rc_stat(ms[k], n);
        if (n < got[k] && stop == NULL) {
            rd__log("rate card: %u of %u replays of %s gave no GPU time", (unsigned)(got[k] - n),
                    (unsigned)got[k], kFrameWords[k]);
        }
    }
    rd__frame_free(&e);
    rd__nd_hold(false);
    rd__nd_hold_end();
    rd__fps_hold_end(); /* the fast build's counts put back: the card's draws are not play */
    rd__op_check_arm(-1, false);
    g_rd.msaaBlocked = wasBlocked;
    const uint32_t released = rd__pipeline_forget_since(pipeMark);
    if (m.nearL.id) {
        rd_destroy_vu_mesh(m.nearL);
    }
    if (m.farL.id) {
        rd_destroy_vu_mesh(m.farL);
    }
    if (m.vert.id) {
        rd_destroy_vu_mesh(m.vert);
    }
    if (m.small.id) {
        rd_destroy_vu_mesh(m.small);
    }
    if (m.large.id) {
        rd_destroy_vu_mesh(m.large);
    }
    if (m.tex.id) {
        rd_destroy_texture(m.tex);
    }
    if (m.texRgba.id) {
        rd_destroy_texture(m.texRgba);
    }
    t->clearPending = 1; /* the next replay starts SCENE from its clear */

    /* the units: fragments are SCENE's texels */
    const double a = (double)t->tw * (double)t->th;
    const RdRcStat none = rd_rc_stat(NULL, 0);
    RdRcItem *it = c->item;
    it[RD_RC_SHADE].u = rd_rc_unit(st[F_L2], st[F_E], 2.0 * a, 1.0e6);
    it[RD_RC_SHADE_TEXA].u = rd_rc_unit(st[F_L2T], st[F_E], 2.0 * a, 1.0e6);
    it[RD_RC_HIDDEN].u = rd_rc_unit(st[F_H2], st[F_L1], a, 1.0e6);
    it[RD_RC_SHADE_RGBA].u = twinOk[F_L2R] ? rd_rc_unit(st[F_L2R], st[F_E], 2.0 * a, 1.0e6)
                                           : rd_rc_none("its layers drew through the fast build");
    if (!fast) {
        it[RD_RC_SHADE_FAST].u = rd_rc_none(noFast);
        it[RD_RC_SHADE_FAST_ND].u = rd_rc_none(noFast);
    } else {
        it[RD_RC_SHADE_FAST].u =
            twinOk[F_L2R_FAST] ? rd_rc_unit(st[F_L2R_FAST], st[F_E], 2.0 * a, 1.0e6)
                               : rd_rc_none("its layers did not draw through vu_ps_nodual_fast");
        it[RD_RC_SHADE_FAST_ND].u =
            !twins ? rd_rc_none(noTwin)
            : twinOk[F_L2R_FASTND]
                ? rd_rc_unit(st[F_L2R_FASTND], st[F_E], 2.0 * a, 1.0e6)
                : rd_rc_none("its layers did not draw through vu_ps_nodual_fast_nd");
    }
    if (!twins) {
        it[RD_RC_SHADE_TWIN].u = rd_rc_none(noTwin);
        it[RD_RC_HIDDEN_TWIN].u = rd_rc_none(noTwin);
    } else {
        it[RD_RC_SHADE_TWIN].u = twinOk[F_L2_TWIN]
                                     ? rd_rc_unit(st[F_L2_TWIN], st[F_E], 2.0 * a, 1.0e6)
                                     : rd_rc_none("its layers did not draw through the twin");
        /* against one layer through the twin, not the ordinary build's: the
           visible layer subtracted must cost what H2_TWIN's own did (the
           reporter's v0.11.15 card subtracted vu_ps_nodual's 3.76 ns layer
           from the twin's 1.58 ns one and printed -0.94); a frame not reached
           (the budget) says so below, not "did not draw through the twin" */
        const bool hOk = twinOk[F_H2_TWIN] || got[F_H2_TWIN] == 0u;
        const bool lOk = twinOk[F_L1_TWIN] || got[F_L1_TWIN] == 0u;
        it[RD_RC_HIDDEN_TWIN].u = hOk && lOk
                                      ? rd_rc_unit(st[F_H2_TWIN], st[F_L1_TWIN], a, 1.0e6)
                                      : rd_rc_none("its layers did not draw through the twin");
    }
    it[RD_RC_VERTEX].u = rd_rc_unit(st[F_V], st[F_E], 3.0 * RC_TRIS * RC_REPEAT, 1.0e6);
    it[RD_RC_TRIANGLE].u = rd_rc_unit(st[F_S], st[F_B], (double)(RC_TRIS - 2) * RC_REPEAT, 1.0e6);
    it[RD_RC_QUAD].u =
        rd_rc_ratio(rd_rc_unit(st[F_S], st[F_E], 1.0, 1.0), rd_rc_unit(st[F_B], st[F_E], 1.0, 1.0));
    it[RD_RC_PASS].u = rd_rc_unit(st[F_E], none, 1.0, 1.0);
    it[RD_RC_BLEND].u = rd_rc_unit(st[F_BL], st[F_E], a, 1.0e6);
    it[RD_RC_BLEND_RATIO].u = rd_rc_ratio(it[RD_RC_BLEND].u, it[RD_RC_SHADE].u);
    for (int i = 0; i < RD_RC_ITEMS && stop != NULL; i++) {
        if (!it[i].u.ok && strstr(it[i].u.why, "timed replays") != NULL) {
            it[i].u = rd_rc_none(stop); /* the reason it stopped, not the count */
        }
    }
    snprintf(c->sized, sizeof(c->sized),
             "2 full-target layers (%.0f texels each), %u x %u triangles off the target and in "
             "a %ux%u GS pixel square; %u of its pipelines released",
             a, (unsigned)RC_REPEAT, (unsigned)RC_TRIS, (unsigned)RC_SQUARE, (unsigned)RC_SQUARE,
             (unsigned)released);
}

void rd_rate_card(void)
{
    const char *file = getenv("ICO_RATECARD_FILE");
    const char *force = getenv("ICO_RD_RATECARD");
    const bool forced = force != NULL && strcmp(force, "1") == 0;
    if ((file == NULL || file[0] == '\0') && !forced) {
        return; /* not the game (no results file) and not the tests */
    }
    if (file != NULL && file[0] == '\0') {
        file = NULL;
    }
    if (!g_rd.inited || !g_rd.hasDevice) {
        return;
    }
    /* not on a start after a bad end or a device loss (REV4R N3): a card
       measured then is not this device's steady state, and a second bad end
       inside it would be charged to it again */
    const char *skipWhy = getenv("ICO_RATECARD_SKIP");
    const char *row = getenv("ICO_LOSS_ROW");
    const long lost = row != NULL ? strtol(row, NULL, 10) : 0;
    if ((skipWhy != NULL && skipWhy[0] != '\0') || lost > 0) {
        if (skipWhy != NULL && skipWhy[0] != '\0') {
            rd__log("rate card: not measured this start (%.200s)", skipWhy);
        } else {
            rd__log("rate card: not measured this start (the loss ladder: ICO_LOSS_ROW %ld)", lost);
        }
        return;
    }
    if (!rhi_timestamps_supported() || !rd__perf_list_stamps_on()) {
        rd__log("rate card: not measured (no GPU timestamps on this device or [dev] "
                "perf_list_stamps = false)");
        return;
    }
    RdTargetRec *t = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    if (!t || !t->withDepth || !t->color.id || !t->depth.id) {
        rd__log("rate card: not measured (no SCENE target to run it on)");
        return;
    }
    const RhiLimits *lim = rhi_limits();
    const char *version = getenv("ICO_RATECARD_VERSION");
    char device[160], key[320];
    snprintf(device, sizeof(device), "%s (%s %s, %s)", rhi_adapter_name(),
             lim && lim->driverName && lim->driverName[0] ? lim->driverName : "driver ?",
             lim && lim->driverVersion ? lim->driverVersion : "",
             rhi_backend_kind_name(rhi_backend()));
    snprintf(key, sizeof(key), "%s | %ux%u | %s", device, (unsigned)t->tw, (unsigned)t->th,
             version && version[0] ? version : "?");
    static char text[8192];
    char date[32];
    const char *lines = NULL;
    const int decided = file && readFile(file, text, sizeof(text))
                            ? rd_rc_decide(text, key, date, sizeof(date), &lines)
                            : RD_RC_MEASURE;
    if (decided == RD_RC_REPRINT && lines != NULL) {
        char suffix[64];
        snprintf(suffix, sizeof(suffix), " (from the run of %s)", date);
        logLines(lines, suffix);
        (void)rd_rc_parse(lines, &s_card);
        return;
    }
    if (decided == RD_RC_SKIP) {
        rd__log("rate card: skipped (the run of %s ended while measuring it on this device: not "
                "measured again until the device, driver, backend, size or version changes)",
                date);
        static char out[1024];
        (void)rd_rc_file_text("blocked", date, key, NULL, out, sizeof(out));
        writeFile(file, out);
        return;
    }
    char now[32];
    nowText(now, sizeof(now));
    if (file) {
        static char out[1024];
        (void)rd_rc_file_text("measuring", now, key, NULL, out, sizeof(out));
        writeFile(file, out); /* a bad end inside it leaves this for the next start */
    }
    const char *sab = getenv("ICO_RD_RATECARD_SABOTAGE");
    if (sab && strcmp(sab, "end") == 0) {
        rd__log("rate card: ICO_RD_RATECARD_SABOTAGE=end: the start ends inside it (exit %d), as "
                "a GPU hang there would",
                RC_EXIT_END);
        fflush(stdout);
        fflush(stderr);
        _exit(RC_EXIT_END);
    }
    const double t0 = rd__now_ms();
    RdRateCard c;
    memset(&c, 0, sizeof(c));
    snprintf(c.device, sizeof(c.device), "%s", device);
    c.w = t->w;
    c.h = t->h;
    c.tw = t->tw;
    c.th = t->th;
    snprintf(c.date, sizeof(c.date), "%s", now);
    /* the tests' switch lifts the budget (lavapipe draws on the CPU) */
    measure(&c, t, forced ? 60000.0 : RC_BUDGET_MS);
    c.totalMs = rd__now_ms() - t0;
    c.have = true;
    static char block[4096];
    (void)rd_rc_text(&c, block, sizeof(block));
    logLines(block, "");
    s_card = c;
    if (file) {
        static char out[8192];
        (void)rd_rc_file_text("done", now, key, block, out, sizeof(out));
        writeFile(file, out);
    }
}

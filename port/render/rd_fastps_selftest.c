/* rd_fastps_selftest.c: the fast pixel build's start-up self-test
 * (rd_fastps.h rd__fps_selftest), run by rd_vuprepass_selftest after the
 * discard-free twins', on SCENE at its real size and scale.
 *
 * Only on a device without dual-source blending by its own features (the
 * twins' rd__nd_applicable): elsewhere no pass draws through vu_ps_nodual,
 * and nothing of the build is made ("not applicable here").
 *
 * The frame (built here, never recorded; the twins' helpers' form): SCENE
 * cleared, Z GEQUAL with Z write, Gouraud; eleven prelit meshes of two
 * triangles each at code 34 and model depth 2 (GS X = 2048 + 8 x), each in
 * a cell of its own, all but two textured with a 4x4 RGBA32 texture (alpha
 * 0x80, a linear filter, MODULATE with TCC):
 *   the fast set's forms, each of which must draw through the build:
 *     opaque     ABE off
 *     lerp       (Cs - Cd) As + Cd: the two-pass blend's colour and alpha
 *                passes (DF_NODUAL_FACTOR, DF_NODUAL_ALPHA_PASS)
 *     add        Cs As + Cd (DF_PREMUL)
 *     fix        Cs FIX + Cd, FIX 0x40 (DF_FIX_FACTOR, DF_PREMUL)
 *     fba        opaque with FBA (DF_FBA)
 *   passes the CPU must refuse:
 *     decal      DECAL (texture)
 *     untex      untextured (texture)
 *     nearest    both filters nearest (levels: DF_NEAREST)
 *     alpha      ATE GREATER 0x40, AFAIL KEEP, the corners' alphas 0, 127,
 *                127 (the alpha plan cannot decide it: alpha)
 *     plane      a chain plane through the cell's middle (plane)
 *     untex2     untextured with FBA (texture)
 * then the depth probe: an untextured code 34 quad over every cell at GS Z
 * 0 with Z write off, drawn only where the depth is still the clear's, so a
 * depth one build wrote and the other did not shows in the colours.  The
 * frame is replayed with the build and without it (its pipelines gathered
 * and made first on the workers), with the discard-free twins held off
 * (the pair through vu_ps_nodual_fast) and, where the twins are on for the
 * run, held on (the pair through vu_ps_nodual_fast_nd: the twins' own rule
 * gives their code 34 passes their twin, the fast build's discard-free form
 * takes the ones in the set).  The build passes when every pair's pictures
 * are the same byte for byte, every form of the set drew through it (at
 * least six passes a pair), every refusal case was refused for its reason,
 * no pipeline of it was missing and the frame without the opaque case
 * differs (the case drew).  A module whose pair was not drawn (the twins off
 * for the run) is not used in play ("module not tested").  Anything else
 * leaves the build off for the run, releases its modules and pipelines and
 * writes "failed" to the install's mark.  Every pipeline the self-test made
 * is released after it (rd__pipeline_forget_since).
 *
 *   rd: fast pixel build: on (self-test: 6 passes through vu_ps_nodual_fast
 *     and 6 through vu_ps_nodual_fast_nd, the texture's, the nearest
 *     filter's, the alpha test's and the clip plane's refused (4, 1, 1 and
 *     1), the colours and the depth (through the probe) byte for byte; its N
 *     pipelines released) on SCENE WxH at TWxTH texels in T ms
 *   rd: fast pixel build: off for the run (self-test: <reason>) ...
 *
 * ICO_RD_VU_FASTPS_SABOTAGE (tests, rd_fastps_run.c): decal, the CPU's rule
 * does not look at DECAL (the decal case then draws through the build, which
 * reads MODULATE, and the pictures differ); module, the modules not made. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_fastps.h"
#include "rd_internal.h"
#include "rd_mesh.h"
#include "rd_nodiscard.h"
#include "shader_consts.h"

#define FT_VERTS 6 /* two triangles a mesh */
#define FT_HALF 12 /* a triangle's legs, GS pixels */

enum {
    FT_OPAQUE,
    FT_LERP,
    FT_ADD,
    FT_FIX,
    FT_FBA,
    FT_DECAL,
    FT_UNTEX,
    FT_NEAREST,
    FT_ALPHA,
    FT_PLANE,
    FT_UNTEX2,
    FT_PROBE,
    FT_CASES
};

/* the passes the set's forms draw: one each, two for the lerp */
#define FT_FAST_PASSES 6u

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

static float cellX(int c)
{
    return 2048.0f + 36.0f * ((float)c - 5.0f);
}

/* the texture: 4x4 RGBA32, alpha 0x80 */
static RdTex makeTexture(void)
{
    uint8_t px[4 * 4 * 4];
    for (int i = 0; i < 16; i++) {
        px[i * 4 + 0] = (uint8_t)(60 + 45 * (i % 4));
        px[i * 4 + 1] = (uint8_t)(220 - 40 * (i / 4));
        px[i * 4 + 2] = (uint8_t)(90 + 10 * i);
        px[i * 4 + 3] = 0x80;
    }
    return rd_create_texture_src(4, 4, px, RD_TEXSRC_RGBA32, "rd fast pixel build self-test");
}

/* case c's mesh (the probe: one quad over every cell at model depth 1, GS
   Z 0) */
static RdMesh makeMesh(int c)
{
    static float q[1 + FT_VERTS * RD_VU_QW_PRELIT][4];
    static const float kCorner[FT_VERTS][2] = {{-1, -1}, {1, -1}, {-1, 1},
                                               {1, -1},  {1, 1},  {-1, 1}};
    qw(q[0], fbits(0x8000u | FT_VERTS), fbits(0x30064000u), fbits(0x512u), 0);
    uint32_t at = 1;
    const bool probe = c == FT_PROBE;
    for (int v = 0; v < FT_VERTS; v++) {
        const float hx = probe ? 220.0f : (float)FT_HALF, hy = probe ? 20.0f : (float)FT_HALF;
        const float gx = (probe ? 2048.0f : cellX(c)) + hx * kCorner[v][0];
        const float gy = 2048.0f + hy * kCorner[v][1];
        const float z = probe ? 1.0f : 2.0f; /* GS X = 2048 + 16 x / z */
        qw(q[at++], (gx - 2048.0f) * z / 16.0f, (gy - 2048.0f) * z / 16.0f, z, 1.0f);
        qw(q[at++], kCorner[v][0] > 0.0f ? 1.0f : 0.0f, kCorner[v][1] > 0.0f ? 1.0f : 0.0f, 1.0f,
           v % 3 == 0 ? 0.0f : 1.0f);
        const float a = c == FT_ALPHA && v % 3 == 0 ? 0.0f : 127.0f;
        qw(q[at++], probe ? 30.0f : (float)(50 + 17 * c + 9 * v), (float)(210 - 15 * v),
           probe ? 240.0f : (float)(100 + 11 * c), a);
    }
    const RdVuBatchDesc batch = {0, 0, 0};
    const RdVuMeshDesc d = {(const float (*)[4])q,
                            at,
                            RD_VU_QW_PRELIT,
                            1,
                            &batch,
                            0,
                            "rd fast pixel build self-test",
                            NULL,
                            0,
                            0};
    return rd_create_vu_mesh(&d);
}

/* the twins' self-test's VU memory (rd_nodiscard_selftest.c sceneBlock) */
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

/* a MESH command of mesh at code 34; plane: the chain plane x >= the cell's
   middle */
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
        c->b[5] = 1u;
        c->f[0] = -planeAt;
    }
    c->keyLo = key;
    return true;
}

/* the state of a case: blend (abe, mode, fix), FBA, the texture (tex.id 0:
   off) with its function and filter */
static bool pushState(RdFrame *f, bool abe, uint8_t mode, uint8_t fix, uint8_t fba, RdTex tex,
                      uint8_t texFn, uint8_t filter)
{
    RdCmd *b = rd__st_push(f, 0, RDC_BLEND);
    if (!b) {
        return false;
    }
    b->b[0] = mode;
    b->b[1] = fix;
    b->b[2] = abe ? 1u : 0u;
    if (!rd__st_push1(f, 0, RDC_FBA, fba)) {
        return false;
    }
    if (!tex.id) {
        return rd__st_push(f, 0, RDC_TEXTURE_OFF) != NULL;
    }
    RdCmd *c = rd__st_push(f, 0, RDC_TEXTURE);
    if (!c) {
        return false;
    }
    c->u[0] = tex.id;
    c->b[0] = texFn;
    c->b[1] = RD_TCC_RGBA;
    c = rd__st_push(f, 0, RDC_FILTER);
    if (!c) {
        return false;
    }
    c->b[0] = filter;
    c->b[1] = filter;
    c->u[0] = 0;
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
    RdCmd *c = ok ? rd__st_push(f, 0, RDC_CLEAR) : NULL;
    if (c) {
        c->u[0] = scene;
        memcpy(c->b, kClear, 4);
        c->b[4] = 1;
        c->u[1] = 0;
    }
    ok = ok && c && rd__st_test(f, 0, RD_TEST_Z_GEQUAL) &&
         rd__st_push1(f, 0, RDC_ZWRITE, RD_ZWRITE_ON) && rd__st_push1(f, 0, RDC_PABE, 0) &&
         rd__st_push1(f, 0, RDC_COLCLAMP, 1) && rd__st_push1(f, 0, RDC_SHADE, 1) &&
         rd__st_push1(f, 0, RDC_TEXA, RD_TEXA_80_80);
    const RdTex none = {0};
    const uint8_t mod = RD_TEXFN_MODULATE, lin = RD_FILTER_LINEAR;
    for (int k = 0; ok && k < FT_PROBE; k++) {
        switch (k) {
        case FT_OPAQUE:
            ok = pushState(f, false, RD_BLEND_LERP_AS, 0, 0, tex, mod, lin);
            break;
        case FT_LERP:
            ok = pushState(f, true, RD_BLEND_LERP_AS, 0, 0, tex, mod, lin);
            break;
        case FT_ADD:
            ok = pushState(f, true, RD_BLEND_CS_AS_ADD_CD, 0, 0, tex, mod, lin);
            break;
        case FT_FIX:
            ok = pushState(f, true, RD_BLEND_CS_FIX_ADD_CD, 0x40, 0, tex, mod, lin);
            break;
        case FT_FBA:
            ok = pushState(f, false, RD_BLEND_LERP_AS, 0, 1, tex, mod, lin);
            break;
        case FT_DECAL:
            ok = pushState(f, false, RD_BLEND_LERP_AS, 0, 0, tex, RD_TEXFN_DECAL, lin);
            break;
        case FT_UNTEX:
            ok = pushState(f, false, RD_BLEND_LERP_AS, 0, 0, none, mod, lin);
            break;
        case FT_NEAREST:
            ok = pushState(f, false, RD_BLEND_LERP_AS, 0, 0, tex, mod, RD_FILTER_NEAREST);
            break;
        case FT_ALPHA:
            /* the alpha test on the corners' alphas 0..127 (As = Av x 0x80 >> 7) */
            ok = pushState(f, false, RD_BLEND_LERP_AS, 0, 0, tex, mod, lin) &&
                 rd__st_test(f, 0, (uint64_t)RD_TEST_Z_GEQUAL | 0x40Du);
            break;
        case FT_PLANE:
            ok = rd__st_test(f, 0, RD_TEST_Z_GEQUAL) &&
                 pushState(f, false, RD_BLEND_LERP_AS, 0, 0, tex, mod, lin);
            break;
        default:
            ok = pushState(f, false, RD_BLEND_LERP_AS, 0, 1, none, mod, lin);
            break;
        }
        if (ok && k != skip) {
            ok = pushMesh(f, mesh[k], k == FT_PLANE, (cellX(FT_PLANE) - 2048.0f) / 8.0f,
                          (uint32_t)k + 1u);
        }
    }
    /* the depth probe: untextured, no blend, Z write off, GS Z 0 */
    ok = ok && pushState(f, false, RD_BLEND_LERP_AS, 0, 0, none, mod, lin) &&
         rd__st_push1(f, 0, RDC_ZWRITE, RD_ZWRITE_OFF) &&
         pushMesh(f, mesh[FT_PROBE], false, 0.0f, (uint32_t)FT_PROBE + 1u);
    return ok;
}

static bool replayRead(const RdFrame *f, RdTargetRec *t, uint8_t *px, size_t size)
{
    if (!rd__replay_frame(f, 0, false) || rhi_device_lost()) {
        return false;
    }
    return rd__read_rhi_texture(t->color, &t->colorState, t->tw, t->th, px, size) &&
           !rhi_device_lost();
}

/* the frame's pipelines gathered and made on the workers: twice, as a pass
   reaches its DrawCB (where the fast build's key is asked for) only once its
   ordinary pipeline is made */
static void gatherAll(const RdFrame *f)
{
    for (int round = 0; round < 2 && !rhi_device_lost(); round++) {
        rd__replay_set_window(UINT32_MAX, 0);
        rd__pipeline_gather(true);
        (void)rd__replay_frame(f, 0, false);
        rd__pipeline_gather(false);
        rd__replay_set_window(0, 0);
        rhi_wait_idle();
        if (!rhi_device_lost()) {
            bool stopped = false;
            (void)rd__pipeline_create_gathered(&stopped);
        }
    }
}

/* one pair: the frame with the build (a) and without it (b), the counts of
   the replay with it; false with why when the device failed or they differ */
typedef struct FtPair {
    uint64_t fast, refused[RD_FPS_REASONS], entries[2];
} FtPair;

static bool pair(const RdFrame *f, RdTargetRec *t, uint8_t *a, uint8_t *b, size_t size, FtPair *o,
                 char *why, size_t whySize)
{
    uint64_t junk, jr[RD_FPS_REASONS], je[2];
    memset(o, 0, sizeof(*o));
    rd__fps_hold(true);
    gatherAll(f);
    rd__fps_take(&junk, jr); /* nothing from the gathering replays */
    rd__fps_take_entries(je);
    const bool okA = replayRead(f, t, a, size);
    rd__fps_take(&o->fast, o->refused);
    rd__fps_take_entries(o->entries);
    rd__fps_hold(false);
    const bool okB = okA && replayRead(f, t, b, size);
    rd__fps_take(&junk, jr);
    rd__fps_take_entries(je);
    if (!okA || !okB) {
        snprintf(why, whySize, "the device failed in the replays %s it", okA ? "without" : "with");
        return false;
    }
    if (o->refused[RD_FPS_UNMADE] != 0u) {
        snprintf(why, whySize, "%llu passes found no pipeline of it",
                 (unsigned long long)o->refused[RD_FPS_UNMADE]);
        return false;
    }
    if (memcmp(a, b, size) != 0) {
        size_t first = 0;
        while (first < size && a[first] == b[first]) {
            first++;
        }
        snprintf(why, whySize, "the pictures differ with it, the first at texel %u,%u",
                 (unsigned)((first / 4u) % t->tw), (unsigned)((first / 4u) / t->tw));
        return false;
    }
    return true;
}

void rd__fps_selftest(RdTargetRec *t)
{
    if (strcmp(rd__fps_state_text(), "off") == 0) {
        rd__fps_start_line();
        return;
    }
    if (rd__fps_not_applicable_text() != NULL) {
        rd__fps_selftest_skip(rd__fps_not_applicable_text());
        return;
    }
    const double t0 = rd__now_ms();
    char why[256] = "";
    RdMesh mesh[FT_CASES];
    bool meshes = true;
    for (int i = 0; i < FT_CASES; i++) {
        mesh[i] = makeMesh(i);
        meshes = meshes && mesh[i].id;
    }
    RdFrame f;
    memset(&f, 0, sizeof(f));
    const uint32_t scene = rd_target(RD_TARGET_SCENE).id;
    const size_t size = (size_t)t->tw * t->th * 4u;
    uint8_t *a = malloc(size), *b = malloc(size);
    const RdTex tex = makeTexture();
    if (!meshes || !tex.id || !a || !b || !buildFrame(&f, scene, t->w, t->h, mesh, tex, -1)) {
        rd__frame_free(&f);
        for (int i = 0; i < FT_CASES; i++) {
            if (mesh[i].id) {
                rd_destroy_vu_mesh(mesh[i]);
            }
        }
        if (tex.id) {
            rd_destroy_texture(tex);
        }
        free(a);
        free(b);
        rd__fps_selftest_skip("could not build its frame");
        return;
    }
    const uint32_t pipeMark = rd__pipeline_mark();
    const bool ndOn = rd__nd_on(); /* the twins on for the run: the second pair */
    const bool made = rd__fps_try_begin();
    FtPair p[2];
    memset(p, 0, sizeof(p));
    bool pass = false, drew[2] = {false, false};
    if (!made) {
        snprintf(why, sizeof(why), "its modules could not be made");
    } else {
        pass = true;
        for (int k = 0; pass && k < 2; k++) {
            if (k == 1 && !ndOn) {
                break; /* no twins: vu_ps_nodual_fast_nd untested, not used in play */
            }
            rd__nd_hold(k == 1); /* the twins off, then on (their own rule) */
            pass = pair(&f, t, a, b, size, &p[k], why, sizeof(why));
            rd__nd_hold_end();
            if (!pass) {
                break;
            }
            const uint64_t *r = p[k].refused;
            if (p[k].entries[k] < FT_FAST_PASSES) {
                snprintf(why, sizeof(why),
                         "%llu passes drew through %s, the set's forms draw %u (with the twins %s)",
                         (unsigned long long)p[k].entries[k],
                         k ? RD_FPS_MODULE_NAME_ND : RD_FPS_MODULE_NAME, FT_FAST_PASSES,
                         k ? "on" : "off");
                pass = false;
            } else if (r[RD_FPS_TEXTURE] < 3u || r[RD_FPS_LEVELS] == 0u || r[RD_FPS_ALPHA] == 0u ||
                       r[RD_FPS_PLANE] == 0u) {
                snprintf(why, sizeof(why),
                         "a case the CPU must refuse was not refused (texture %llu, levels %llu, "
                         "alpha %llu, plane %llu)",
                         (unsigned long long)r[RD_FPS_TEXTURE],
                         (unsigned long long)r[RD_FPS_LEVELS], (unsigned long long)r[RD_FPS_ALPHA],
                         (unsigned long long)r[RD_FPS_PLANE]);
                pass = false;
            }
            drew[k] = pass;
        }
        /* the opaque case drew (a case that draws nothing compares equal and
           tests nothing): the frame without it, without the build, differs */
        if (pass) {
            RdFrame g;
            const bool built = buildFrame(&g, scene, t->w, t->h, mesh, tex, FT_OPAQUE);
            rd__nd_hold(false);
            const bool read = built && replayRead(&g, t, a, size);
            rd__nd_hold_end();
            rd__frame_free(&g);
            if (!read) {
                snprintf(why, sizeof(why), "the device failed in the replay without its case");
                pass = false;
            } else if (memcmp(a, b, size) == 0) {
                snprintf(why, sizeof(why), "its opaque case drew nothing: not tested");
                pass = false;
            }
        }
    }
    rd__fps_hold_end();
    const uint32_t released = rd__pipeline_forget_since(pipeMark);
    rd__frame_free(&f);
    for (int i = 0; i < FT_CASES; i++) {
        rd_destroy_vu_mesh(mesh[i]);
    }
    rd_destroy_texture(tex);
    free(a);
    free(b);
    t->clearPending = 1; /* the next replay starts SCENE from its clear */
    if (pass) {
        for (int e = 0; e < 2; e++) {
            rd__fps_entry_tested(e, drew[e]);
        }
    }
    rd__fps_verdict(pass, why);
    if (pass) {
        rd__log("fast pixel build: on (self-test: %llu passes through vu_ps_nodual_fast and %llu "
                "through vu_ps_nodual_fast_nd%s, the texture's, the nearest filter's, the alpha "
                "test's and the clip plane's refused (%llu, %llu, %llu and %llu), the colours "
                "and the depth (through the probe) byte for byte; its %u pipelines released) on "
                "SCENE %ux%u at %ux%u texels in %.1f ms",
                (unsigned long long)p[0].entries[0], (unsigned long long)p[1].entries[1],
                drew[1] ? "" : " (the discard-free twins off: that module not used in play)",
                (unsigned long long)p[0].refused[RD_FPS_TEXTURE],
                (unsigned long long)p[0].refused[RD_FPS_LEVELS],
                (unsigned long long)p[0].refused[RD_FPS_ALPHA],
                (unsigned long long)p[0].refused[RD_FPS_PLANE], (unsigned)released, t->w, t->h,
                t->tw, t->th, rd__now_ms() - t0);
    } else {
        rd__log("fast pixel build: off for the run (self-test: %s; its %u pipelines released) on "
                "SCENE %ux%u at %ux%u texels in %.1f ms",
                why, (unsigned)released, t->w, t->h, t->tw, t->th, rd__now_ms() - t0);
    }
    rd__fps_start_line();
}

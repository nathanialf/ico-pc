/* rd_vu_zero_selftest.c: the zero-area cull's start-up self-test
 * (rd_vu_zero.h rd__vu_zero_selftest), run by rd_vuprepass_selftest after
 * the VU pre-pass passed its own, on SCENE at its real size and scale.
 *
 * The frame (built here, never recorded; rd_vuprepass_selftest.c's
 * helpers): SCENE cleared, Z GEQUAL with Z write, no blend, untextured,
 * Gouraud; one prelit mesh of separate triangles (a strip of three
 * vertices each, ST.w 0 on the first) at model depth 2, where the screen
 * matrix (vu1_test.c's) puts a vertex at GS X = 2048 + 8 x, Y = 2048 + 8 y
 * exactly, drawn twice: code 34 (clip mode none) and code 32 (the region
 * test, every corner inside).  Its triangles sit at the target's sample
 * points (the lattice word the run gives SCENE, rd_vu_zero_word), one case
 * a cell:
 *   inside      a triangle between four sample points, both windings (culled)
 *   corner      a corner on a sample point (kept: the closed test)
 *   edge        an edge through a sample point (kept)
 *   flat        no area, between the points (culled) and through one (kept)
 *   big         covering several points (kept)
 *   near        a corner within the grown margin of a point (kept)
 *   sliver      across a row of points, its box wider than ICO_VU_ZERO_SPAN
 *               points (kept untested)
 *   left, top   inside a cell at the target's left and top edges (culled)
 * The CPU's answers: rd_vu_zero_test over every triangle slot of the mesh
 * (the drawn triangles and the slots between the runs, which the rule
 * stage evaluates too) at the corners placed here.  The frame is replayed
 * with the cull (every entry's record read back and checked against the
 * CPU's answers from the recorded corners, rd_vu_zero_check) and without
 * it; the cull passes when the GPU's flags equal the CPU's answers for
 * every slot, the culled count is the CPU's (the corners were where the
 * CPU put them), at least one triangle was culled, and the two pictures
 * are the same byte for byte (the device's own rasteriser covered no
 * sample of a culled triangle).  Anything else (a difference, an answer not
 * read back, a pipeline missing, a device error) leaves it off for the
 * run.
 *
 *   rd: VU zero-area cull: on (self-test: 26 of 92 triangle slots culled as
 *     the CPU answers, code 34 and code 32, pictures byte for byte; lattice
 *     0x10100) on SCENE WxH at TWxTH texels in T ms
 *   rd: VU zero-area cull: off for the run (self-test: <reason>) ...
 *
 * ICO_RD_VU_ZERO_SABOTAGE (tests, rd_vu_zero_run.c): lattice, the GPU
 * given a lattice half a texel off (its flags then differ from the CPU's);
 * check, the run-time check's answers from no lattice (the self-test is not
 * affected); missing, no record copied. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "rd_mesh.h"
#include "rd_vu_zero.h"
#include "shader_consts.h"

#define ZT_CASES 11
#define ZT_VERTS (ZT_CASES * 3)
#define ZT_Z 2.0f /* model depth: GS X = 2048 + 8 x */

/* the cases' corners in texels from their cell's sample point */
static const float kCase[ZT_CASES][3][2] = {
    {{0.3f, 0.3f}, {0.7f, 0.3f}, {0.3f, 0.7f}},     /* inside */
    {{0.3f, 0.3f}, {0.3f, 0.7f}, {0.7f, 0.3f}},     /* inside, the other winding */
    {{0.0f, 0.0f}, {0.5f, 0.2f}, {0.2f, 0.5f}},     /* corner */
    {{-0.4f, -0.4f}, {0.4f, 0.4f}, {-0.4f, 0.4f}},  /* edge */
    {{0.2f, 0.5f}, {0.5f, 0.5f}, {0.8f, 0.5f}},     /* flat, between */
    {{-0.3f, 0.0f}, {0.0f, 0.0f}, {0.3f, 0.0f}},    /* flat, through */
    {{-2.0f, -2.0f}, {2.0f, -2.0f}, {-2.0f, 2.0f}}, /* big */
    {{0.06f, 0.06f}, {0.6f, 0.1f}, {0.1f, 0.6f}},   /* near */
    {{0.3f, -0.2f}, {6.3f, 0.1f}, {0.3f, 0.3f}},    /* sliver */
    {{0.3f, 0.3f}, {0.7f, 0.3f}, {0.3f, 0.7f}},     /* left edge */
    {{0.3f, 0.3f}, {0.7f, 0.3f}, {0.3f, 0.7f}},     /* top edge */
};

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

/* the first g >= from (1/16 GS pixels) whose U = g s + k is a multiple of
   16: a sample point's place */
static int32_t samplePlace(int32_t from, int32_t s, int32_t k)
{
    for (int32_t g = from; g < from + 16; g++) {
        if ((g * s + k) % 16 == 0) {
            return g;
        }
    }
    return from;
}

/* the corners (1/16 GS pixels) of every case for target t under word */
static void corners(const RdTargetRec *t, uint32_t word, int32_t g[ZT_VERTS][2])
{
    const int32_t kx = (int32_t)(word & 15u), ky = (int32_t)((word >> 4) & 15u);
    const int32_t sx = (int32_t)((word >> 8) & 15u), sy = (int32_t)((word >> 12) & 15u);
    /* the frame's GS window: origin 2048 - w / 2 (rd__replay_vu_frame) */
    const int32_t left = (2048 - (int32_t)(t->w >> 1)) * 16,
                  top = (2048 - (int32_t)(t->h >> 1)) * 16;
    for (int c = 0; c < ZT_CASES; c++) {
        /* the cells: two rows of five about the centre, 32 GS pixels apart;
           the edge cases in the target's first texel column and row */
        int32_t bx = (2048 - 80 + 32 * (c % 5)) * 16, by = (2048 - 32 + 32 * ((c / 5) % 2)) * 16;
        if (c == 9) {
            bx = left;
        } else if (c == 10) {
            by = top;
        }
        const int32_t sxg = samplePlace(bx, sx, kx), syg = samplePlace(by, sy, ky);
        for (int k = 0; k < 3; k++) {
            /* a texel is 16 / s sixteenths of a GS pixel */
            const float dx = kCase[c][k][0] * 16.0f / (float)sx;
            const float dy = kCase[c][k][1] * 16.0f / (float)sy;
            g[c * 3 + k][0] = sxg + (int32_t)(dx < 0.0f ? dx - 0.5f : dx + 0.5f);
            g[c * 3 + k][1] = syg + (int32_t)(dy < 0.0f ? dy - 0.5f : dy + 0.5f);
        }
    }
}

/* the mesh: the prelit layout (rd_mesh.h RD_VU_QW_PRELIT: position, ST
   with the strip flag, colour) */
static RdMesh makeMesh(const int32_t g[ZT_VERTS][2])
{
    static float q[1 + ZT_VERTS * RD_VU_QW_PRELIT][4];
    qw(q[0], fbits(0x8000u | ZT_VERTS), fbits(0x30064000u), fbits(0x512u), 0);
    uint32_t at = 1;
    for (int v = 0; v < ZT_VERTS; v++) {
        /* GS X = 2048 + 16 x / z at depth 2: x = (g / 16 - 2048) / 8 */
        const float x = ((float)g[v][0] - 32768.0f) / 128.0f;
        const float y = ((float)g[v][1] - 32768.0f) / 128.0f;
        qw(q[at++], x, y, ZT_Z, 1.0f);
        qw(q[at++], 0.0f, 0.0f, 1.0f, v % 3 == 0 ? 0.0f : 1.0f);
        qw(q[at++], (float)(60 + 15 * (v % 11)), (float)(220 - 5 * v), 140.0f, 127.0f);
    }
    const RdVuBatchDesc batch = {0, 0, 0};
    const RdVuMeshDesc d = {(const float (*)[4])q,
                            at,
                            RD_VU_QW_PRELIT,
                            1,
                            &batch,
                            0,
                            "rd vu zero-area self-test",
                            NULL,
                            0,
                            0};
    return rd_create_vu_mesh(&d);
}

/* rd_vuprepass_selftest.c sceneBlock's VU memory (vu1_test.c buildScene) */
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

static bool pushMesh(RdFrame *f, RdMesh mesh, uint8_t code, uint8_t clip, uint32_t key)
{
    uint8_t buf[sizeof(RdVuPayload) + sizeof(RdVuBlock)];
    RdVuPayload p;
    RdVuBlock vu;
    memset(&p, 0, sizeof(p));
    p.code = code;
    p.clip = clip;
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
    c->keyLo = key;
    return true;
}

static bool buildFrame(RdFrame *f, uint32_t scene, uint32_t w, uint32_t h, RdMesh mesh)
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
    return ok && pushMesh(f, mesh, 34, RD_VU_CLIP_NONE, 1u) &&
           pushMesh(f, mesh, 32, RD_VU_CLIP_REGION, 2u);
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

/* the CPU's answer: the slots of one command whose triangle (its corners
   placed by corners()) covers no sample point; the slots between the runs
   (kicks 3 i + 3, 3 i + 4) are evaluated by the rule stage too */
static uint32_t cpuCulled(const int32_t g[ZT_VERTS][2], uint32_t word)
{
    uint32_t n = 0;
    for (int k = 2; k < ZT_VERTS; k++) {
        n += (uint32_t)rd_vu_zero_test(g[k - 2][0], g[k - 2][1], g[k - 1][0], g[k - 1][1], g[k][0],
                                       g[k][1], word);
    }
    return n;
}

void rd__vu_zero_selftest(RdTargetRec *t)
{
    if (strcmp(rd__vu_zero_state_text(), "off") == 0) {
        /* off before it (the environment, the loss ladder, the install's
           mark): said at the start; nothing of the cull made */
        return;
    }
    const double t0 = rd__now_ms();
    char why[256] = "";
    /* the cull tried from here: the mark, then its one module
       (rd__vu_zero_try_begin); SCENE's word as the replay's pre-pass will
       give it */
    rd__vu_zero_hold(true);
    const bool made = rd__vu_zero_try_begin();
    RdStateBlock st;
    rd__reset_state_block(&st);
    st.color = rd_target(RD_TARGET_SCENE).id;
    st.gsW = t->w;
    st.gsH = t->h;
    int wordWhy = -1;
    const uint32_t word = rd__vu_zero_entry_word(&st, &wordWhy);
    int32_t g[ZT_VERTS][2];
    RdFrame f;
    memset(&f, 0, sizeof(f));
    RdMesh mesh = {0};
    const size_t size = (size_t)t->tw * t->th * 4u;
    uint8_t *a = NULL, *b = NULL;
    RdVuZeroCheck took;
    memset(&took, 0, sizeof(took));
    uint32_t want = 0;
    bool pass = false;
    if (!made) {
        snprintf(why, sizeof(why), "its rule stage's zero build could not be made");
    } else if (word == 0u) {
        snprintf(why, sizeof(why), "SCENE gives no lattice (%s)", rd_vu_zero_reason_name(wordWhy));
    } else {
        corners(t, word, g);
        want = 2u * cpuCulled(g, word);
        mesh = makeMesh(g);
        a = malloc(size);
        b = malloc(size);
        if (!mesh.id || !a || !b || !buildFrame(&f, st.color, t->w, t->h, mesh)) {
            snprintf(why, sizeof(why), "could not build its frame");
        } else {
            const bool wasBlocked = g_rd.msaaBlocked;
            g_rd.msaaBlocked = true; /* single-sampled, as the cull asks */
            /* the pipelines first, on the workers (rd_vuprepass_selftest.c) */
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
            rd__vu_zero_take(&took); /* nothing from the gathering replay */
            const bool okA = replayRead(&f, t, a, size);
            rd__vu_zero_collect_now();
            rd__vu_zero_take(&took);
            rd__vu_zero_hold(false); /* the same frame without the cull */
            const bool okB = okA && replayRead(&f, t, b, size);
            g_rd.msaaBlocked = wasBlocked;
            if (!okA || !okB) {
                snprintf(why, sizeof(why), "the device failed in the replays %s the cull",
                         okA ? "without" : "with");
            } else if (took.slots == 0u) {
                snprintf(why, sizeof(why), "no answer read back from the GPU");
            } else if (took.mismatches != 0u) {
                snprintf(why, sizeof(why),
                         "the GPU's answers differ from the CPU's in %llu of %llu "
                         "triangle slots: %.120s",
                         (unsigned long long)took.mismatches, (unsigned long long)took.slots,
                         took.first);
            } else if (took.culled != want || want == 0u) {
                snprintf(why, sizeof(why),
                         "%llu triangle slots culled, the CPU's answer %u (the corners not where "
                         "the CPU put them)",
                         (unsigned long long)took.culled, (unsigned)want);
            } else if (memcmp(a, b, size) != 0) {
                size_t first = 0;
                while (first < size && a[first] == b[first]) {
                    first++;
                }
                snprintf(why, sizeof(why),
                         "the pictures differ with the cull, the first at texel %u,%u",
                         (unsigned)((first / 4u) % t->tw), (unsigned)((first / 4u) / t->tw));
            } else {
                pass = true;
            }
        }
    }
    rd__vu_zero_hold_end();
    rd__frame_free(&f);
    if (mesh.id) {
        rd_destroy_vu_mesh(mesh);
    }
    free(a);
    free(b);
    t->clearPending = 1; /* the next replay starts SCENE from its clear */
    rd__vu_zero_verdict(pass, why);
    if (pass) {
        rd__log("VU zero-area cull: on (self-test: %llu of %llu triangle slots culled as the CPU "
                "answers, code 34 and code 32, pictures byte for byte; lattice 0x%05X) on SCENE "
                "%ux%u at %ux%u texels in %.1f ms",
                (unsigned long long)took.culled, (unsigned long long)took.slots, (unsigned)word,
                t->w, t->h, t->tw, t->th, rd__now_ms() - t0);
    } else {
        rd__log("VU zero-area cull: off for the run (self-test: %s) on SCENE %ux%u at %ux%u "
                "texels in %.1f ms",
                why, t->w, t->h, t->tw, t->th, rd__now_ms() - t0);
    }
}

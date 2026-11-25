/* rd_onepass_selftest.c: the one-pass blend's device check (rd_onepass.h
 * rd__op_selftest), run by rd_vuprepass_selftest after the fast pixel
 * build's, on SCENE at its start size.
 *
 * Only on a Vulkan device without dual-source blending by its own features
 * (rd__op_not_applicable_text): elsewhere no pair is drawn in two passes, or
 * the two-pass blend is the fallback a device with dual-source blending
 * chose, which stays untouched.
 *
 * The frames (built here, never recorded): SCENE cleared to 0 0 0 0x80 and
 * depth 0, Z GEQUAL with Z write, (Cs - Cd) As + Cd, Gouraud, COLCLAMP;
 * one mesh of the discard-free twins' clipper check's case k (its 13
 * triangles across and near the eye plane, rd__nd_clip_case: the triangles
 * code 36's clipping exists for) in both windings, its corners' alphas 9
 * to 124 (the alpha plan's range at most 128), with the facing table every
 * prelit part has on the disc, in one of seven forms:
 *   32   the region test (the camera rule), ABE on: the plain class's pairs
 *   32t  the same textured: an RGBA32 texture (alphas 0..128), MODULATE,
 *        TCC, linear (vu_ps; the fast pixel build's form)
 *   34   clip mode none, ABE on;  34t  textured
 *   36   the scissor with the state's ABE off: the cut pair under ABE on,
 *        the kick one pass, fused where the device fuses (the reporter's
 *        pattern);  36t  textured
 *   36a  the scissor with ABE on: cut and kick both pairs, drawn apart
 * then the depth probe: a code 34 quad over the whole target at GS Z 0, Z
 * write off, blending off, so a depth one form wrote and another did not
 * shows in the colour.  Each frame drawn under each arm that changes
 * something on it (rd_onepass.h: REF; A on 32/34; B and C on 36; C on
 * 36a) and compared with REF byte for byte.  The VU record at the size play
 * makes it at start-up (rd_early_start_rows, made here and kept: v0.11.6's
 * compaction passed a 16-row record while play's 512 rows came back
 * black); the discard-free twins and the fast pixel build held in their run
 * states (the keys drawn are play's: the one-pass key with a twin's or the
 * fast build's bit where play would take one).  Every pipeline the check
 * made is released after it (rd__pipeline_forget_since): play makes its
 * own in the background as pairs ask.
 *
 *   rd: one-pass blend: the check, case 1 of 13 (...) on SCENE WxH at
 *     TWxTH texels: 32 A P pairs, 0 texels differ; 32t ...; 34 ...; 34t ...;
 *     36 fused F, B unfused U, 0 texels differ, C unfused U, P pairs, 0
 *     texels differ; 36t ...; 36a C P pairs, 0 texels differ
 *   rd: one-pass blend: the check's arm A (one pass on the pairs outside
 *     code 36): the same bytes (...)
 *   rd: one-pass blend: on (check: in use ...; A ..., B ..., C ..., D
 *     none ...) on SCENE ...
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_early_plan.h" /* rd_early_start_rows: the play record's size */
#include "rd_fastps.h"
#include "rd_internal.h"
#include "rd_mesh.h"
#include "rd_nodiscard.h"
#include "rd_onepass.h"
#include "rd_velocity.h" /* rd__velocity_on */
#include "rd_vu_rec_early.h"
#include "shader_consts.h"

#define OC_VERTS 6 /* a triangle in both windings, two strips of one */
#define OC_FORMS 7
#define OC_CASES_MAX 16

static const struct {
    uint8_t code;
    bool abe, tex;
    const char *word;
} kForm[OC_FORMS] = {
    {32, true, false, "32"},  {32, true, true, "32t"},  {34, true, false, "34"},
    {34, true, true, "34t"},  {36, false, false, "36"}, {36, false, true, "36t"},
    {36, true, false, "36a"},
};

/* the arms a form is drawn under (REF always): A where it has no code 36,
   B where its code 36 fuses (ABE off), C on every code 36 form */
static bool formArm(int x, int arm)
{
    const bool c36 = kForm[x].code == 36;
    switch (arm) {
    case RD_OP_ARM_REF:
        return true;
    case RD_OP_ARM_A:
        return !c36;
    case RD_OP_ARM_B:
        return c36 && !kForm[x].abe;
    case RD_OP_ARM_C:
        return c36;
    default:
        return false;
    }
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

/* rd_nodiscard_selftest.c sceneBlock's VU memory (vu1_test.c buildScene):
   h = (16 x + 2048 z, 16 y + 2048 z, 3145728 (z - 1), z) */
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

/* a mesh of the prelit layout from corners (the clipper check's clipMesh),
   each corner's alpha 9 to 124 (alpha 255 for the probe: it blends nothing) */
static RdMesh makeMesh(const float (*c)[3], int n, float shade, bool probe)
{
    static float q[1 + OC_VERTS * RD_VU_QW_PRELIT][4];
    qw(q[0], fbits(0x8000u | (uint32_t)n), fbits(0x30064000u), fbits(0x512u), 0);
    uint32_t at = 1;
    for (int v = 0; v < n; v++) {
        qw(q[at++], c[v][0], c[v][1], c[v][2], 1.0f);
        qw(q[at++], (float)(v % 2), (float)((v / 2) % 2), 1.0f, v % 3 == 0 ? 0.0f : 1.0f);
        qw(q[at++], shade + (float)(12 * v), 230.0f - shade, (float)(40 + 30 * v),
           probe    ? 127.0f
           : v == 5 ? 127.0f
                    : (float)((23 * v + 9) % 128));
    }
    const RdVuBatchDesc batch = {0, 0, 0};
    const RdVuMeshDesc d = {(const float (*)[4])q,
                            at,
                            RD_VU_QW_PRELIT,
                            1,
                            &batch,
                            0,
                            "rd one-pass blend check",
                            NULL,
                            0,
                            0};
    return rd_create_vu_mesh(&d);
}

/* an 8 x 8 RGBA32 texture of random colours, its alphas 0..128 */
static RdTex makeTexture(void)
{
    uint8_t px[8 * 8 * 4];
    for (uint32_t i = 0; i < sizeof(px); i++) {
        uint32_t x = i * 2654435761u + 0x7F4A7C15u;
        x ^= x >> 15;
        x *= 2246822519u;
        x ^= x >> 13;
        px[i] = (uint8_t)(x >> 24);
        if (i % 4u == 3u) {
            px[i] = (uint8_t)(px[i] % 129u);
        }
    }
    return rd_create_texture(8, 8, px, RD_TEXA_80_80, "rd one-pass blend check RGBA32");
}

/* a MESH command of mesh at code 32 (the region test), 34 (clip mode none)
   or 36 (the scissor) */
static bool pushMesh(RdFrame *f, RdMesh mesh, uint8_t code, uint32_t key)
{
    uint8_t buf[sizeof(RdVuPayload) + sizeof(RdVuBlock)];
    RdVuPayload p;
    RdVuBlock vu;
    memset(&p, 0, sizeof(p));
    p.code = code;
    p.clip = code == 34 ? RD_VU_CLIP_NONE : code == 36 ? RD_VU_CLIP_SCISSOR : RD_VU_CLIP_REGION;
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

static bool pushBlend(RdFrame *f, bool abe)
{
    RdCmd *b = rd__st_push(f, 0, RDC_BLEND);
    if (b) {
        b->b[0] = RD_BLEND_LERP_AS;
        b->b[1] = 0x80;
        b->b[2] = abe ? 1u : 0u;
    }
    return b != NULL;
}

static bool pushTexture(RdFrame *f, RdTex tex)
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
    c = c ? rd__st_push(f, 0, RDC_WRAP) : NULL;
    if (c) {
        c->b[0] = RD_WRAP_REPEAT;
        c->b[1] = RD_WRAP_REPEAT;
    }
    return c != NULL;
}

/* case mesh (id 0: none) in form x, then the probe */
static bool buildFrame(RdFrame *f, uint32_t scene, uint32_t w, uint32_t h, RdMesh mesh, int x,
                       RdTex tex, RdMesh probe)
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
    ok = ok && c && pushBlend(f, kForm[x].abe) && rd__st_push(f, 0, RDC_TEXTURE_OFF) &&
         rd__st_test(f, 0, RD_TEST_Z_GEQUAL) && rd__st_push1(f, 0, RDC_ZWRITE, RD_ZWRITE_ON) &&
         rd__st_push1(f, 0, RDC_FBA, 0) && rd__st_push1(f, 0, RDC_PABE, 0) &&
         rd__st_push1(f, 0, RDC_COLCLAMP, 1) && rd__st_push1(f, 0, RDC_SHADE, 1);
    if (kForm[x].tex) {
        ok = ok && pushTexture(f, tex);
    }
    ok = ok && (mesh.id == 0 || pushMesh(f, mesh, kForm[x].code, 1u));
    /* the probe: untextured, blending off, Z write off */
    ok = ok && rd__st_push(f, 0, RDC_TEXTURE_OFF) && pushBlend(f, false) &&
         rd__st_push1(f, 0, RDC_ZWRITE, RD_ZWRITE_OFF) && pushMesh(f, probe, 34, 2u);
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

/* one arm's replay of a frame read into px: its counts; a replay that found
   a pipeline not made (its own, a twin's, the fast build's) drawn again
   once the background queue is made.  0 drawn, 1 still without a pipeline
   or a replay or read that failed without a device loss (not checked), -1
   the device lost */
typedef struct OcTake {
    uint64_t one[2], unfused, fused;
    bool retried;
} OcTake;

/* the tests' =read: the next replay's read taken as failed, no loss */
static bool s_failNextRead;

static int armRead(const RdFrame *f, RdTargetRec *t, uint8_t *px, size_t size, int arm, bool sab,
                   OcTake *o)
{
    uint64_t one[2], unf, unmade, nd, ndr[RD_ND_REASONS], fps, fpr[RD_FPS_REASONS];
    memset(o, 0, sizeof(*o));
    rd__op_check_take(one, &unf, &unmade);
    rd__nd_take(&nd, ndr);
    rd__fps_take(&fps, fpr);
    rd__op_check_arm(arm, sab);
    int rc = 0;
    for (int round = 0; round < 2; round++) {
        const bool sabRead = s_failNextRead;
        s_failNextRead = false;
        if (sabRead || !replayRead(f, t, px, size)) {
            /* a device loss is the device's (measured); any other failed
               replay or read compares nothing: not checked */
            rc = !sabRead && rhi_device_lost() ? -1 : 1;
            break;
        }
        o->fused = g_rdPerf.vuFusePairs;
        rd__op_check_take(o->one, &o->unfused, &unmade);
        rd__nd_take(&nd, ndr);
        rd__fps_take(&fps, fpr);
        if (unmade == 0u && ndr[RD_ND_UNMADE] == 0u && fpr[RD_FPS_UNMADE] == 0u) {
            rc = 0;
            break;
        }
        rc = 1;
        if (round == 0) {
            /* the replay queued the keys (rd__pipeline_twin): the queue made
               now and waited for, then the frame once more */
            o->retried = true;
            rhi_wait_idle();
            rd__pipeline_twin_tick(true);
        }
    }
    rd__op_check_arm(-1, false);
    return rc;
}

/* an arm's verdict over its frames */
typedef struct OcArm {
    uint32_t frames, drawn, differ, unchecked;
    uint32_t empty;    /* drawn through neither, the geometry expected to cover nothing */
    uint32_t untested; /* drawn through neither, expected to cover something */
    uint64_t took, unfused, fused;
    int firstK;     /* the first differing frame (-1 none) */
    size_t firstAt; /* its first differing byte */
    uint8_t ref[4], got[4];
    bool control; /* its control (127/255) differed somewhere */
    int controlK; /* where */
    uint64_t controlDiff;
    char uncheckedAt[160];
} OcArm;

static int armVerdict(const OcArm *a, int arm, bool device, bool noFuse)
{
    if (!device) {
        return RD_OP_V_DEVICE;
    }
    if (a->differ != 0u) {
        return RD_OP_V_DIFFERS;
    }
    if (a->unchecked != 0u) {
        return RD_OP_V_UNCHECKED;
    }
    if (arm == RD_OP_ARM_B) {
        if (a->fused == 0u) {
            /* nothing fused: "nothing to unfuse" only where the device fuses
               no command at all (the fuse or the merge builds off); else the
               check's frames did not fuse where play's may: not tested, and
               the cut class stays off */
            return noFuse ? RD_OP_V_NOTHING : RD_OP_V_UNTESTED;
        }
        return a->unfused != 0u ? RD_OP_V_PASS : RD_OP_V_UNTESTED;
    }
    if (a->took == 0u || (arm == RD_OP_ARM_C && a->unfused == 0u && a->fused != 0u)) {
        return RD_OP_V_UNTESTED;
    }
    return a->control ? RD_OP_V_PASS : RD_OP_V_BLIND;
}

static const char *const kArmWords[RD_OP_ARMS] = {
    "", "A (one pass on the pairs outside code 36)",
    "B (code 36 unfused where C unfuses it, its two passes kept)",
    "C (A, and code 36 unfused with its cut and kick pairs in one pass)"};

static void armLine(int arm, const OcArm *a, int v, int nCases, uint32_t tw, char *sum, size_t ss)
{
    char tail[400];
    size_t at = 0;
    tail[0] = '\0';
    if (v == RD_OP_V_DIFFERS && a->firstK >= 0) {
        const int x = a->firstK % OC_FORMS;
        at = appendf(tail, sizeof(tail), at,
                     ": %u of %u frames, the first case %d at %s, texel %u,%u: today %u %u %u %u, "
                     "the arm %u %u %u %u (RGBA)",
                     a->differ, a->frames, a->firstK / OC_FORMS + 1, kForm[x].word,
                     (unsigned)((a->firstAt / 4u) % tw), (unsigned)((a->firstAt / 4u) / tw),
                     a->ref[0], a->ref[1], a->ref[2], a->ref[3], a->got[0], a->got[1], a->got[2],
                     a->got[3]);
    } else if (v == RD_OP_V_UNCHECKED) {
        at = appendf(tail, sizeof(tail), at,
                     ": %u frames still without a pipeline after a retry (%s), no frame differing",
                     a->unchecked, a->uncheckedAt);
    }
    (void)appendf(tail, sizeof(tail), at,
                  " (%u frames, %u of them drawn, %u drawing nothing as expected, %u drawing "
                  "nothing not tested; %llu pairs in one pass, %llu code 36 commands fused "
                  "today, %llu unfused%s",
                  a->frames, a->drawn, a->empty, a->untested, (unsigned long long)a->took,
                  (unsigned long long)a->fused, (unsigned long long)a->unfused,
                  arm == RD_OP_ARM_B ? ")"
                  : a->control       ? "; its control differed"
                                     : ")");
    if (arm != RD_OP_ARM_B && a->control) {
        const size_t n = strlen(tail);
        (void)appendf(tail, sizeof(tail), n, " at case %d at %s, %llu texels)",
                      a->controlK / OC_FORMS + 1, kForm[a->controlK % OC_FORMS].word,
                      (unsigned long long)a->controlDiff);
    }
    rd__log("one-pass blend: the check's arm %s over %d cases: %s%s", kArmWords[arm], nCases,
            rd_op_verdict_name(v), tail);
    const size_t n = strlen(sum);
    (void)appendf(sum, ss, n, "%s%c %s", n ? ", " : "", "-ABC"[arm], rd_op_verdict_name(v));
}

void rd__op_selftest(RdTargetRec *t)
{
    if (strcmp(rd__op_state_text(), "off") == 0) {
        rd__op_start_line();
        return;
    }
    if (rd__op_forced()) {
        rd__op_start_line(); /* the tests' ICO_RD_ONEPASS=plain|full: no check */
        return;
    }
    const char *na = rd__op_not_applicable_text();
    if (na != NULL) {
        rd__op_selftest_skip(na);
        return;
    }
    if (t == NULL || !t->color.id || !t->withDepth || t->format != RHI_FMT_RGBA8_UNORM) {
        rd__op_selftest_skip("no SCENE target to run it on");
        return;
    }
    const double t0 = rd__now_ms();
    const int nCases =
        rd__nd_clip_case_count() < OC_CASES_MAX ? rd__nd_clip_case_count() : OC_CASES_MAX;
    const int nFrames = nCases * OC_FORMS;
    /* the record at play's size (rd_vu_rec_early.h: made here, kept for
       play, which then grows it only past this) */
    uint32_t rows = rd__vu_rec_rows();
    if (g_rd.vuPrepass) {
        const uint32_t want =
            rd_early_start_rows(rd_interpolation_active(), rd__velocity_on(), rd__vu_rec_rows_min(),
                                rd__vu_rec_rows_max(), ICO_VU_REC_W);
        if (want > rows && rd__vu_rec_presize(want)) {
            rows = want;
        }
    }
    const size_t size = (size_t)t->tw * t->th * 4u;
    uint8_t *buf[RD_OP_ARMS + 3];
    bool mem = true;
    for (int i = 0; i < RD_OP_ARMS + 3; i++) {
        buf[i] = malloc(size);
        mem = mem && buf[i] != NULL;
    }
    uint8_t *alone[2] = {buf[RD_OP_ARMS], buf[RD_OP_ARMS + 1]};
    uint8_t *sabPx = buf[RD_OP_ARMS + 2];
    RdMesh mesh[OC_CASES_MAX];
    memset(mesh, 0, sizeof(mesh));
    bool made = mem;
    for (int k = 0; k < nCases && made; k++) {
        float c[OC_VERTS][3];
        (void)rd__nd_clip_case(k, t->w, t->h, c);
        /* the other winding, a strip of its own: corners 0, 2, 1 */
        memcpy(c[3], c[0], sizeof(c[3]));
        memcpy(c[4], c[2], sizeof(c[4]));
        memcpy(c[5], c[1], sizeof(c[5]));
        mesh[k] = makeMesh((const float (*)[3])c, OC_VERTS, (float)(30 + 9 * k), false);
        /* the facing table every prelit part has on the disc (the clipper
           check's: without one a mesh keeps the PS2's drops) */
        static const uint8_t kNoFlip[(OC_VERTS + 7) / 8] = {0};
        RdMeshRec *rec = mesh[k].id ? rd__mesh_rec(mesh[k].id) : NULL;
        made = mesh[k].id && rec != NULL && rd__mesh_set_facing(rec, kNoFlip, OC_VERTS);
    }
    const float hx = (float)(t->w / 2u + 8u) / 16.0f, hy = (float)(t->h / 2u + 8u) / 16.0f;
    const float pc[OC_VERTS][3] = {{-hx, -hy, 1}, {hx, -hy, 1}, {-hx, hy, 1},
                                   {hx, -hy, 1},  {hx, hy, 1},  {-hx, hy, 1}};
    const RdMesh probe = makeMesh(pc, OC_VERTS, 200.0f, true);
    const RdTex tex = makeTexture();
    made = made && probe.id && tex.id;
    const uint32_t scene = rd_target(RD_TARGET_SCENE).id;
    const uint32_t pipeMark = rd__pipeline_mark();
    const bool ndOn = rd__nd_on(), fpsOn = rd__fps_on();
    char why[512] = "";
    OcArm arm[RD_OP_ARMS];
    memset(arm, 0, sizeof(arm));
    for (int a = 0; a < RD_OP_ARMS; a++) {
        arm[a].firstK = -1;
    }
    bool device = true, held = false;
    char caseText[1024]; /* a case's seven frames, one line */
    size_t caseAt = 0;
    caseText[0] = '\0';
    uint32_t replays = 0, retried = 0;
    const int sab = rd__op_sabotage(); /* 1 constant, 2 cut, 3 unmade, 4 sliver, 5 read */
    /* the device fuses no code 36 command at all (vuFuseVerdict's OFF and
       BUILD): B's "nothing fused" is then a property of the device */
    const bool noFuse = !g_rd.vuFuse || !g_rd.vuMerge || !g_rd.vsMerge[RD_VS_VU_MAIN].id ||
                        !g_rd.vsMerge[RD_VS_VU_PRELIT].id;
    bool lost = false;      /* a device loss (measured), not a failed read */
    uint32_t slivered = 0u; /* the tests' =sliver: C's frames changed */
    if (!made) {
        snprintf(why, sizeof(why), "its frames could not be built");
        device = false;
    } else if (!rd__op_try_begin()) {
        snprintf(why, sizeof(why), "its mark could not be written");
        device = false;
    }
    if (device) {
        /* the twins and the fast build as play has them (their counts
           apart) */
        rd__nd_hold(ndOn);
        rd__fps_hold(fpsOn);
        held = true;
        /* every frame's and arm's pipelines first, in ONE gathering (the
           clipper check's lesson: a gathering empties the list), made on
           the start-up set's workers */
        rd__replay_set_window(UINT32_MAX, 0);
        rd__pipeline_gather(true);
        for (int k = 0; k < nFrames; k++) {
            RdFrame f;
            if (buildFrame(&f, scene, t->w, t->h, mesh[k / OC_FORMS], k % OC_FORMS, tex, probe)) {
                for (int a = 0; a < RD_OP_ARMS; a++) {
                    if (formArm(k % OC_FORMS, a)) {
                        rd__op_check_arm(a, false);
                        replays++;
                        (void)rd__replay_frame(&f, 0, false);
                    }
                }
                rd__op_check_arm(-1, false);
            }
            rd__frame_free(&f);
        }
        rd__pipeline_gather(false);
        rd__replay_set_window(0, 0);
        rhi_wait_idle();
        if (!rhi_device_lost()) {
            bool stopped = false;
            (void)rd__pipeline_create_gathered(&stopped);
        }
        /* the probe alone, untextured and textured state */
        for (int x = 0; x < 2 && device; x++) {
            RdFrame f;
            static const RdMesh kNone = {0};
            const bool built = buildFrame(&f, scene, t->w, t->h, kNone, x, tex, probe);
            OcTake o;
            replays++;
            const int prc = built ? armRead(&f, t, alone[x], size, RD_OP_ARM_REF, false, &o) : 1;
            if (prc != 0) {
                lost = prc < 0;
                snprintf(why, sizeof(why), "%s in the probe's frame",
                         lost ? "the device was lost" : "a replay or read failed (not the device)");
                device = false;
            }
            rd__frame_free(&f);
        }
    }
    for (int k = 0; k < nFrames && device; k++) {
        const int c = k / OC_FORMS, x = k % OC_FORMS;
        RdFrame f;
        if (!buildFrame(&f, scene, t->w, t->h, mesh[c], x, tex, probe)) {
            rd__frame_free(&f);
            snprintf(why, sizeof(why), "its frame of case %d at %s could not be built", c + 1,
                     kForm[x].word);
            device = false;
            break;
        }
        if (x == 0) {
            caseAt = 0;
            caseText[0] = '\0';
        }
        OcTake take[RD_OP_ARMS];
        int rc[RD_OP_ARMS];
        memset(take, 0, sizeof(take));
        for (int a = 0; a < RD_OP_ARMS && device; a++) {
            rc[a] = 0;
            if (!formArm(x, a)) {
                continue;
            }
            /* the tests' sabotage: the constant 127/255 in A and C, or in C
               on the code 36 frames */
            const bool s = (sab == 1 && (a == RD_OP_ARM_A || a == RD_OP_ARM_C)) ||
                           (sab == 2 && a == RD_OP_ARM_C);
            replays++;
            s_failNextRead = sab == 5 && k == 0 && a == RD_OP_ARM_REF;
            rc[a] = armRead(&f, t, buf[a], size, a, s, &take[a]);
            retried += take[a].retried ? 1u : 0u;
            if (rc[a] < 0) {
                snprintf(why, sizeof(why), "the device was lost in its replays (case %d at %s)",
                         c + 1, kForm[x].word);
                lost = true;
                device = false;
            }
        }
        if (!device || rc[RD_OP_ARM_REF] != 0) {
            if (device) {
                /* the reference without a pipeline: every arm of the frame
                   not checked */
                for (int a = 1; a < RD_OP_ARMS; a++) {
                    if (formArm(x, a)) {
                        rc[a] = 1;
                    }
                }
            } else {
                rd__frame_free(&f);
                break;
            }
        }
        const uint8_t *only = alone[kForm[x].tex ? 1 : 0];
        const bool refDrawn = texelsDiffer(buf[RD_OP_ARM_REF], only, size, NULL) != 0u;
        /* the texels the case should cover (the clipper check's estimate):
           0 for the cases whose plane runs through the eye, by design */
        const uint64_t expect = rd__nd_clip_case_expected(c, t, kForm[x].code == 32);
        bool drawn = refDrawn;
        uint64_t differ[RD_OP_ARMS] = {0, 0, 0, 0};
        for (int a = 1; a < RD_OP_ARMS; a++) {
            if (!formArm(x, a)) {
                continue;
            }
            OcArm *o = &arm[a];
            o->frames++;
            o->fused += take[RD_OP_ARM_REF].fused;
            if (rc[a] != 0) {
                o->unchecked++;
                const size_t n = strlen(o->uncheckedAt);
                (void)appendf(o->uncheckedAt, sizeof(o->uncheckedAt), n, "%scase %d at %s",
                              n ? ", " : "", c + 1, kForm[x].word);
                continue;
            }
            if (sab == 4 && a == RD_OP_ARM_C && !refDrawn) {
                /* the tests' sliver: C draws where today's drew nothing */
                buf[a][size / 2u] ^= 0x40u;
                slivered++;
            }
            /* compared whenever either form drew (the clipper check's rule:
               nothing through today's two passes and something through the
               arm is a difference, the shape of v0.11.10's slivers) */
            const bool armDrawn = texelsDiffer(buf[a], only, size, NULL) != 0u;
            if (!refDrawn && !armDrawn) {
                if (expect == 0u) {
                    o->empty++; /* the same: neither covers a texel, as expected */
                } else {
                    o->untested++; /* expected to draw, drew through neither: not tested */
                }
                continue;
            }
            drawn = true;
            o->drawn++;
            o->took += take[a].one[0] + take[a].one[1];
            o->unfused += take[a].unfused;
            size_t first = 0;
            differ[a] = texelsDiffer(buf[a], buf[RD_OP_ARM_REF], size, &first);
            if (differ[a] != 0u) {
                if (o->differ++ == 0u) {
                    o->firstK = k;
                    o->firstAt = first;
                    memcpy(o->ref, buf[RD_OP_ARM_REF] + first, 4);
                    memcpy(o->got, buf[a] + first, 4);
                }
            }
            /* the positive control: the arm once more with the constant
               127/255 where it took a pair, until it is seen to differ */
            if (a != RD_OP_ARM_B && !o->control && take[a].one[0] + take[a].one[1] != 0u &&
                differ[a] == 0u) {
                OcTake s;
                replays++;
                const int src = armRead(&f, t, sabPx, size, a, true, &s);
                if (src < 0) {
                    snprintf(why, sizeof(why), "the device was lost in its control (case %d at %s)",
                             c + 1, kForm[x].word);
                    lost = true;
                    device = false;
                    break;
                }
                const uint64_t d =
                    src == 0 ? texelsDiffer(sabPx, buf[RD_OP_ARM_REF], size, NULL) : 0u;
                if (d != 0u) {
                    o->control = true;
                    o->controlK = k;
                    o->controlDiff = d;
                }
            }
        }
        rd__frame_free(&f);
        /* the frame's part of its case's line */
        const char *sep = caseAt ? "; " : "";
        const char *w = kForm[x].word;
        const unsigned long long oneA = take[1].one[0] + take[1].one[1];
        const unsigned long long oneC = take[3].one[0] + take[3].one[1];
        if (rc[RD_OP_ARM_REF] != 0) {
            caseAt = appendf(caseText, sizeof(caseText), caseAt, "%s%s not checked", sep, w);
        } else if (!drawn) {
            caseAt = appendf(caseText, sizeof(caseText), caseAt, "%s%s drew nothing%s", sep, w,
                             expect == 0u ? " (none expected)" : " (not tested)");
        } else if (kForm[x].code != 36) {
            caseAt = appendf(caseText, sizeof(caseText), caseAt,
                             "%s%s A %llu pairs, %llu texels differ%s", sep, w, oneA,
                             (unsigned long long)differ[1], rc[1] == 1 ? " (not checked)" : "");
        } else if (!kForm[x].abe) {
            caseAt =
                appendf(caseText, sizeof(caseText), caseAt,
                        "%s%s fused %llu, B unfused %llu, %llu texels differ%s, C unfused "
                        "%llu, %llu pairs, %llu texels differ%s",
                        sep, w, (unsigned long long)take[0].fused,
                        (unsigned long long)take[2].unfused, (unsigned long long)differ[2],
                        rc[2] == 1 ? " (not checked)" : "", (unsigned long long)take[3].unfused,
                        oneC, (unsigned long long)differ[3], rc[3] == 1 ? " (not checked)" : "");
        } else {
            caseAt = appendf(caseText, sizeof(caseText), caseAt,
                             "%s%s C %llu pairs, %llu texels differ%s", sep, w, oneC,
                             (unsigned long long)differ[3], rc[3] == 1 ? " (not checked)" : "");
        }
        if (x == OC_FORMS - 1 || !device) {
            float cc[3][3];
            rd__log("one-pass blend: the check, case %d of %d (%s) on SCENE %ux%u at %ux%u "
                    "texels: %s",
                    c + 1, nCases, rd__nd_clip_case(c, t->w, t->h, cc), t->w, t->h, t->tw, t->th,
                    caseText);
        }
    }
    if (held) {
        rd__nd_hold_end();
        rd__fps_hold_end();
    }
    rd__op_check_arm(-1, false);
    /* the verdicts */
    int v[RD_OP_ARMS] = {RD_OP_V_NONE, RD_OP_V_NONE, RD_OP_V_NONE, RD_OP_V_NONE};
    char sum[256] = "";
    if (device) {
        for (int a = 1; a < RD_OP_ARMS; a++) {
            v[a] = armVerdict(&arm[a], a, device, noFuse);
            armLine(a, &arm[a], v[a], nCases, t->tw, sum, sizeof(sum));
        }
        rd__log("one-pass blend: the check's arm D (one pass on a fused code 36 pair): none: a "
                "fused kick's pixel carries the factor 1.0 in the one output the blender reads, "
                "and its stored alpha would need a second value; a fused pair keeps its two "
                "passes");
    }
    const uint32_t classes = device ? rd_op_classes(v[1], v[2], v[3]) : 0u;
    /* measured: an arm differed, its control did not, nothing of its
       class drawn, the device failed; only arms not checked: no verdict */
    bool measured = !device && lost;
    for (int a = 1; a < RD_OP_ARMS && device; a++) {
        measured = measured ||
                   (v[a] != RD_OP_V_PASS && v[a] != RD_OP_V_NOTHING && v[a] != RD_OP_V_UNCHECKED);
    }
    rd__op_verdict(classes, measured, device ? (sum[0] ? sum : "nothing in use") : why);
    const uint32_t released = rd__pipeline_forget_since(pipeMark);
    for (int k = 0; k < nCases; k++) {
        if (mesh[k].id) {
            rd_destroy_vu_mesh(mesh[k]);
        }
    }
    if (probe.id) {
        rd_destroy_vu_mesh(probe);
    }
    if (tex.id) {
        rd_destroy_texture(tex);
    }
    for (int i = 0; i < RD_OP_ARMS + 3; i++) {
        free(buf[i]);
    }
    t->clearPending = 1; /* the next replay starts SCENE from its clear */
    if (sab == 4) {
        rd__log("one-pass blend: ICO_RD_ONEPASS_SABOTAGE=sliver: arm C's picture changed on %u "
                "frames where today's two passes drew nothing",
                (unsigned)slivered);
    }
    /* the state the verdict left (a background fault during the check
       turns it off whatever the arms said) */
    const bool inUse = classes != 0u && strcmp(rd__op_state_text(), "on") == 0;
    if (!device) {
        rd__log("one-pass blend: off for the run (check: %s%s) on SCENE %ux%u at %ux%u texels in "
                "%.1f ms; its %u pipelines released",
                why,
                lost ? ""
                     : "; no verdict: its mark removed, nothing written as failed, the check "
                       "runs again at the next start",
                t->w, t->h, t->tw, t->th, rd__now_ms() - t0, (unsigned)released);
    } else {
        rd__log("one-pass blend: %s (check: %s%s; arms %s, D none%s) on SCENE %ux%u at %ux%u "
                "texels, the VU record at %u rows (play's), %u replays (%d frames gathered once, "
                "%u drawn again once the background queue was made) in %.1f ms; its %u pipelines "
                "released",
                inUse ? "on" : "off for the run", inUse ? "in use: " : "nothing in use",
                inUse ? rd_op_classes_name(classes) : "", sum,
                inUse || measured ? ""
                                  : "; no verdict: its mark removed, nothing written as "
                                    "failed, the check runs again at the next start",
                t->w, t->h, t->tw, t->th, (unsigned)rows, (unsigned)replays, nFrames,
                (unsigned)retried, rd__now_ms() - t0, (unsigned)released);
    }
    rd__op_start_line();
}

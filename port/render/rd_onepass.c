/* rd_onepass.c: a LERP As VU draw of the two-pass blend in one pass where the
 * CPU proves the same bytes (rd_onepass.h says what and why;
 * docs/port/RENDER_POST.md, "The two-pass blend in one pass"; issue 179).
 *
 *   rd_onepass_why          the proof for a pair, pure
 *   rd_onepass_alpha_model  the stored alpha under a blender model, pure
 *   rd__onepass_carry       rd_pipeline.c rd__expand_no_dual: the colour
 *                           pass carries A
 *   rd__onepass_bind        rd_pipeline.c rd__set_pipeline: the constant
 *   rd__onepass_take        rd_replay_vu.c vuDraw: counted, the proven
 *                           pairs' alpha passes dropped
 *   rd__onepass_selftest    both ways on SCENE, byte for byte (the end) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "attempt_ledger.h"
#include "rd_health.h"
#include "rd_mesh.h"
#include "rd_onepass.h"
#include "rd_replay.h"
#include "shader_consts.h"

static const char *const kWhy[RD_ONEPASS_WHY_COUNT] = {
    [RD_ONEPASS_WHY_NONE] = "drawn in one pass",
    [RD_ONEPASS_WHY_ALPHA] = "alpha above 128",
    [RD_ONEPASS_WHY_FBA] = "FBA",
    [RD_ONEPASS_WHY_PABE] = "PABE",
    [RD_ONEPASS_WHY_FIX] = "a FIX factor",
    [RD_ONEPASS_WHY_OTHER] = "other",
    [RD_ONEPASS_WHY_OFF] = "the one-pass rule not in force",
};

const char *rd__onepass_why_text(int why)
{
    return why >= 0 && why < RD_ONEPASS_WHY_COUNT ? kWhy[why] : "?";
}

int rd_onepass_why(const RdDrawPass *colour, const RdDrawPass *alpha, uint8_t hi, uint32_t vuBits)
{
    const RdPipeKeyInt *k = &colour->key;
    if (rd__blend_path(k->gs.blend) != RD_BP_LERP || (vuBits & (uint32_t)ICO_VU_FUSED) != 0u ||
        (k->gs.ztst == RD_ZTST_GREATER && k->gs.zwrite == RD_ZWRITE_ON) ||
        k->colorFmt != RHI_FMT_RGBA8_UNORM || k->dst != RHI_DST_READ_NONE ||
        alpha->key.gs.colorMask != 8u) {
        return RD_ONEPASS_WHY_OTHER;
    }
    if (colour->flags & (uint32_t)ICO_DF_FIX_FACTOR) {
        return RD_ONEPASS_WHY_FIX;
    }
    if (colour->flags & (uint32_t)ICO_DF_PABE) {
        return RD_ONEPASS_WHY_PABE;
    }
    if (colour->flags & (uint32_t)ICO_DF_FBA) {
        return RD_ONEPASS_WHY_FBA;
    }
    if (hi > RD_ONEPASS_ALPHA_MAX) {
        return RD_ONEPASS_WHY_ALPHA;
    }
    return RD_ONEPASS_WHY_NONE;
}

/* ------------------------------------------------------------ the model */

/* x in [0, 1] quantised: bits 8..24 an unsigned normalised fixed point,
 * 116 a binary16, 132 a binary32; mode 0 nearest, 1 down, 2 up */
static double quant(double x, int bits, int mode)
{
    if (x <= 0.0) {
        return 0.0;
    }
    if (bits == 132) {
        float v = (float)x;
        if (mode == 1 && (double)v > x) {
            v = nextafterf(v, 0.0f);
        } else if (mode == 2 && (double)v < x) {
            v = nextafterf(v, 2.0f);
        }
        return (double)v;
    }
    if (bits == 116) {
        int e;
        (void)frexp(x, &e);                   /* x = m 2^e, m in [0.5, 1) */
        int ulpExp = e - 11;                  /* 11 significant bits */
        ulpExp = ulpExp < -24 ? -24 : ulpExp; /* the subnormals' step */
        const double ulp = ldexp(1.0, ulpExp);
        const double r = x / ulp;
        const double k = mode == 0 ? floor(r + 0.5) : (mode == 1 ? floor(r) : ceil(r));
        return k * ulp;
    }
    const double m = (double)((1u << bits) - 1u);
    const double r = x * m;
    double k = mode == 0 ? floor(r + 0.5) : (mode == 1 ? floor(r) : ceil(r));
    k = k > m ? m : k;
    return k / m;
}

static int storeByte(double x, int mode)
{
    const double r = (x > 1.0 ? 1.0 : x) * 255.0;
    return (int)(mode == 0 ? floor(r + 0.5) : (mode == 1 ? floor(r) : ceil(r)));
}

int rd_onepass_alpha_model(int as, int bs, int bk, int bp, int round, int store, int *two)
{
    /* the alpha pass: vu_dual_out_f's float4(col.rgb, a) * (1.0 / 255.0),
     * blending off */
    const float c0 = (float)as * (float)(1.0 / 255.0);
    *two = storeByte(quant((double)c0, bs, round), store);
    /* the carry: c0.a = f * (1.0 / 128.0) (exact), clamped to 1 as the
     * blender's source; times the constant's alpha 128/255 */
    double s = (double)((float)as * (float)(1.0 / 128.0));
    s = s > 1.0 ? 1.0 : s;
    const double k = quant((double)(float)(128.0 / 255.0), bk, round);
    return storeByte(quant(quant(s, bs, round) * k, bp, round), store);
}

/* -------------------------------------------------------------- the rule */

static struct {
    int env;      /* ICO_RD_ONEPASS: -1 unset, 0, 1, 2 sabotage */
    int test;     /* 0 not run, 1 passed, 2 failed or not run with a reason */
    int arm;      /* -1 none; the self-test's replays: 0 kept, 1 one pass */
    bool testing; /* inside the self-test's replays (their counts apart) */
    bool sabotageNow;
    char words[256];
    uint32_t tw, th, w, h;
    uint32_t replay, rOne, rPairs;
    uint32_t tOne[2], tPairs[2];
    RdOnepassCounts run;
    bool said[RD_ONEPASS_WHY_COUNT], firstSaid;
    char stateSaid[384];
} s_op = {.env = -1, .arm = -1};

/* the 10 s block's (__atomic: the render thread adds, the window takes) */
static struct {
    uint64_t pairs, onePass, kept[RD_ONEPASS_WHY_COUNT], saved;
} s_blk;

static int envOf(const char *e)
{
    if (!e || !e[0]) {
        return -1;
    }
    if (strcmp(e, "sabotage") == 0) {
        return 2;
    }
    return e[0] == '0' ? 0 : 1;
}

static bool wanted(void)
{
    return s_op.env >= 1 || (s_op.env < 0 && RD_ONEPASS_DEFAULT);
}

bool rd__onepass_on(void)
{
    return wanted() && g_rd.noDual && s_op.test == 1;
}

bool rd__onepass_carry(void)
{
    return s_op.arm >= 0 || rd__onepass_on();
}

void rd__onepass_bind(RhiCommandList cl, const RdPipeKeyInt *k)
{
    if (k == NULL || k->gs.nodual != RD_NODUAL_CARRY || (k->gs.colorMask & 8u) == 0u) {
        return;
    }
    /* the alpha factor's constant (RHI_BF_CONSTANT reads the constant's
     * alpha for the alpha channel); the sabotage's one lower */
    const float a = s_op.sabotageNow ? 127.0f / 255.0f : 128.0f / 255.0f;
    const float c[4] = {a, a, a, a};
    rhi_cmd_set_blend_constant(cl, c);
}

void rd__onepass_force_arm(int arm)
{
    s_op.arm = arm < 0 ? -1 : (arm ? 1 : 0);
}

bool rd__onepass_unfuse(const RdDrawPass *cut, int nc, uint8_t hi)
{
    const bool allowed = s_op.arm >= 0 ? s_op.arm == 1 : rd__onepass_on();
    return allowed && g_rd.noDual && g_rd.passSamples <= 1 && nc == 2 &&
           (cut[0].flags & (uint32_t)ICO_DF_NODUAL_FACTOR) != 0u && rd__no_dual_second(cut, 1) &&
           cut[0].key.gs.nodual == RD_NODUAL_CARRY &&
           rd_onepass_why(&cut[0], &cut[1], hi, 0) == RD_ONEPASS_WHY_NONE;
}

static void blkAdd(uint64_t *v, uint64_t n)
{
    __atomic_fetch_add(v, n, __ATOMIC_RELAXED);
}

void rd__onepass_take(RdDrawPass *dp, int *np, uint8_t hi, uint32_t vuBits, uint32_t count)
{
    if (!g_rd.noDual) {
        return;
    }
    if (s_op.replay != g_rd.replayCounter) {
        s_op.replay = g_rd.replayCounter;
        s_op.rOne = s_op.rPairs = 0;
    }
    for (int i = 0; i + 1 < *np; i++) {
        if (!(dp[i].flags & (uint32_t)ICO_DF_NODUAL_FACTOR) || !rd__no_dual_second(dp, i + 1)) {
            continue;
        }
        int why = rd_onepass_why(&dp[i], &dp[i + 1], hi, vuBits);
        if (why == RD_ONEPASS_WHY_NONE && g_rd.passSamples > 1) {
            /* a multisampled pass: a pixel's planes may be evaluated outside
               its triangle, past the alpha plan's range */
            why = RD_ONEPASS_WHY_OTHER;
        }
        const bool carried = dp[i].key.gs.nodual == RD_NODUAL_CARRY;
        const bool allowed = s_op.arm >= 0 ? s_op.arm == 1 : rd__onepass_on();
        if (why == RD_ONEPASS_WHY_NONE && (!carried || !allowed)) {
            why = RD_ONEPASS_WHY_OFF;
        }
        s_op.rPairs++;
        if (s_op.testing) {
            const int a = s_op.arm == 1 ? 1 : 0;
            s_op.tPairs[a]++;
            s_op.tOne[a] += why == RD_ONEPASS_WHY_NONE ? 1u : 0u;
        } else {
            s_op.run.pairs++;
            blkAdd(&s_blk.pairs, 1);
        }
        if (why != RD_ONEPASS_WHY_NONE) {
            if (!s_op.testing) {
                s_op.run.kept[why]++;
                blkAdd(&s_blk.kept[why], 1);
                ico_attempt_fire2(ICO_ATT_ONEPASS, 1);
            }
            continue;
        }
        /* the alpha pass dropped: the passes after it moved down */
        for (int j = i + 1; j + 1 < *np; j++) {
            dp[j] = dp[j + 1];
        }
        (*np)--;
        s_op.rOne++;
        if (!s_op.testing) {
            s_op.run.onePass++;
            s_op.run.saved += count;
            blkAdd(&s_blk.onePass, 1);
            blkAdd(&s_blk.saved, count);
            ico_attempt_fire(ICO_ATT_ONEPASS, 1);
            if (!s_op.firstSaid) {
                s_op.firstSaid = true;
                rd__log("two-pass blend: a pair drawn in one pass (the colour pass's alpha "
                        "through the constant), frame %u (reported once)",
                        g_rd.replayCounter);
            }
        }
    }
}

static void stateNote(void)
{
    char line[sizeof(s_op.stateSaid)];
    const char *how = s_op.env == 0   ? "ICO_RD_ONEPASS=0"
                      : s_op.env == 1 ? "ICO_RD_ONEPASS=1"
                      : s_op.env == 2
                          ? "ICO_RD_ONEPASS=sabotage"
                          : (RD_ONEPASS_DEFAULT ? "the build's default" : "RD_ONEPASS_DEFAULT 0");
    if (!wanted()) {
        snprintf(line, sizeof(line), "off (%s)", how);
    } else if (s_op.test == 1) {
        snprintf(line, sizeof(line), "on (%s; self-test %s)", how, s_op.words);
    } else {
        snprintf(line, sizeof(line), "off until its self-test passes (%s; self-test %s)", how,
                 s_op.words[0] ? s_op.words : "not run yet");
    }
    if (strcmp(line, s_op.stateSaid) != 0) {
        snprintf(s_op.stateSaid, sizeof(s_op.stateSaid), "%s", line);
        rd__log("two-pass blend: a LERP As pair in one pass where its alpha is proven at most "
                "128 (no FBA, no PABE): %s",
                line);
    }
    (void)ico_attempt_decide_onepass(g_rd.hasDevice && g_rd.noDual,
                                     s_op.env < 0 ? -1 : (s_op.env ? 1 : 0), RD_ONEPASS_DEFAULT,
                                     s_op.test == 1 ? 1 : (s_op.test == 2 ? 0 : -1));
}

void rd__onepass_init(void)
{
    memset(&s_op, 0, sizeof(s_op));
    s_op.arm = -1;
    s_op.env = envOf(getenv("ICO_RD_ONEPASS")); /* the tests only: no config key */
    stateNote();
}

int rd__onepass_test_state(const char **words)
{
    if (words) {
        *words = s_op.words[0] ? s_op.words : "not run";
    }
    return s_op.test;
}

void rd__onepass_counts(RdOnepassCounts *out)
{
    *out = s_op.run;
}

void rd__onepass_replay(uint32_t *onePass, uint32_t *pairs)
{
    const bool now = s_op.replay == g_rd.replayCounter;
    *onePass = now ? s_op.rOne : 0u;
    *pairs = now ? s_op.rPairs : 0u;
}

void rd__onepass_line(void)
{
    const uint64_t pairs = __atomic_exchange_n(&s_blk.pairs, 0, __ATOMIC_RELAXED);
    const uint64_t one = __atomic_exchange_n(&s_blk.onePass, 0, __ATOMIC_RELAXED);
    const uint64_t saved = __atomic_exchange_n(&s_blk.saved, 0, __ATOMIC_RELAXED);
    uint64_t w[RD_ONEPASS_WHY_COUNT];
    for (int i = 0; i < RD_ONEPASS_WHY_COUNT; i++) {
        w[i] = __atomic_exchange_n(&s_blk.kept[i], 0, __ATOMIC_RELAXED);
    }
    if (pairs == 0) {
        return;
    }
    fprintf(stderr,
            "window: two-pass blend: %llu pairs drawn in one pass of %llu, kept two-pass: alpha "
            "above 128 %llu, FBA %llu, PABE %llu, a FIX factor %llu, other %llu, the rule not in "
            "force %llu; %llu vertex invocations saved\n",
            (unsigned long long)one, (unsigned long long)pairs,
            (unsigned long long)w[RD_ONEPASS_WHY_ALPHA], (unsigned long long)w[RD_ONEPASS_WHY_FBA],
            (unsigned long long)w[RD_ONEPASS_WHY_PABE], (unsigned long long)w[RD_ONEPASS_WHY_FIX],
            (unsigned long long)w[RD_ONEPASS_WHY_OTHER], (unsigned long long)w[RD_ONEPASS_WHY_OFF],
            (unsigned long long)saved);
}

/* ------------------------------------------------------------ the self-test
 * One frame on SCENE at its own size and scale (at the start, then at each
 * new size: the play's), list 0, replayed from SCENE's clear twice with the
 * colour passes carrying A: arm 0 every pair's alpha pass kept, arm 1 the
 * proven pairs in one pass (s_op.arm); SCENE read back after each and
 * compared byte for byte.  Both arms draw through the same pipelines (the
 * carrying colour pass's and the alpha pass's keys), made first on the
 * start-up set's workers.  The frame: one prelit mesh (code 32, 12
 * vertices, 8 triangles about 96 x 72 GS pixels, vertex alphas 0..127, so
 * the alpha plan's range is at most 128 with its rounding margin) at places
 * on a grid 96 x 64 GS pixels apart over the whole frame (at most OP_PLACES),
 * at each place once in each variant, overlapping, each a little nearer
 * (Z GEQUAL, Z write), all (Cs - Cd) As + Cd:
 *   0  untextured
 *   1  MODULATE with TCC from an RGBA32 texture whose alphas are 0..128
 *   2  DECAL with TCC from it, nearest
 *   3  untextured, the alpha test GREATER 0x40 with AFAIL FB_ONLY (two
 *      pairs)
 *   4  untextured, DATE (DATM 1; every OP_DATE_EVERY-th place)
 *   5  an RGB24 texture under TEXA AEM (TA0 0x7F), bilinear (vu_texa_ps)
 *   6  untextured, Z ALWAYS without Z write
 *   7  untextured with FBA (kept two-pass in both arms)
 * Passed: every byte equal, at least OP_MIN_ONE pairs in one pass in arm 1,
 * none in arm 0, the same pairs in both, and with GPU timestamps on Vulkan
 * at least one picture collected and none with the GPU's own sign of a
 * reset (rd_health.h's window; two more replays collect the last records).
 * MSAA held off.  ICO_RD_ONEPASS=sabotage: arm 1's constant alpha 127/255. */
#define OP_VARIANTS 8u
#define OP_PLACES 96u
#define OP_STEP_X 96u
#define OP_STEP_Y 64u
#define OP_DATE_EVERY 8u
#define OP_VERTS 12u
#define OP_MIN_ONE 150u

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

static uint8_t opLevel(uint32_t i, uint32_t seed)
{
    uint32_t x = i * 2654435761u + seed * 0x9E3779B9u + 0x7F4A7C15u;
    x ^= x >> 15;
    x *= 2246822519u;
    x ^= x >> 13;
    return (uint8_t)(x >> 24);
}

/* rd_vuprepass_selftest.c sceneBlock's block, moved by dx, dy GS pixels
 * and dz in Z */
static void opBlock(RdVuBlock *b, float dx, float dy, float dz)
{
    float (*m)[4] = b->mem;
    memset(b, 0, sizeof(*b));
    qw(m[0], 0, 0, 0, 1);
    qw(m[1], 4095, 4095, 0, 16777215);
    qw(m[3], fbits(0x8000u), fbits(0x302EC000u), fbits(0x512u), 0);
    qw(m[4], 16, 0, 0, 0);
    qw(m[5], 0, 16, 0, 0);
    qw(m[6], 2048 + dx, 2048 + dy, 3145728, 1);
    qw(m[7], 0, 0, -3145728 + dz, 0);
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

/* GS X = 2048 + 8 (-6 + 2.4 col), Y = 2048 +- 36; vertex alphas 0..127 */
static RdMesh opMesh(void)
{
    float q[1 + OP_VERTS * RD_VU_QW_PRELIT][4];
    qw(q[0], fbits(0x8000u | OP_VERTS), fbits(0x30064000u), fbits(0x512u), 0);
    uint32_t at = 1;
    for (uint32_t k = 0; k < OP_VERTS; k++) {
        const uint32_t col = k / 2u, row = k % 2u;
        const float z = 2.0f + 0.04f * (float)(k % 3u);
        qw(q[at++], (-6.0f + 2.4f * (float)col) * z / 2.0f,
           (row ? 4.5f : -4.5f) * z / 2.0f + 0.3f * (float)col, z, 1);
        qw(q[at++], 0.1f * (float)col, row ? 1.0f : 0.0f, 1.0f, k == 0 || k == 6 ? 0.0f : 1.0f);
        qw(q[at++], (float)(40u + 30u * col), (float)(200u - 15u * k), (float)(60u + 80u * row),
           (float)((23u * k + 9u) % 128u));
    }
    const RdVuBatchDesc batch = {0, 0, 0};
    const RdVuMeshDesc d = {(const float (*)[4])q,
                            at,
                            RD_VU_QW_PRELIT,
                            1,
                            &batch,
                            0,
                            "rd one-pass self-test",
                            NULL,
                            0,
                            0};
    return rd_create_vu_mesh(&d);
}

static bool opPushMesh(RdFrame *f, RdMesh mesh, float dx, float dy, float dz)
{
    uint8_t buf[sizeof(RdVuPayload) + sizeof(RdVuBlock)];
    RdVuPayload p;
    RdVuBlock vu;
    memset(&p, 0, sizeof(p));
    p.code = 32;
    p.clip = RD_VU_CLIP_REGION;
    p.prog = RD_PROG_PRELIT;
    p.batchCount = 1;
    p.qwPerVertex = RD_VU_QW_PRELIT;
    opBlock(&vu, dx, dy, dz);
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
    c->keyLo = 1u;
    return true;
}

static bool opTex(RdFrame *f, RdTex t, RdTexFn fn, RdFilter filter)
{
    RdCmd *c = rd__st_push(f, 0, RDC_TEXTURE);
    if (c) {
        c->u[0] = t.id;
        c->b[0] = (uint8_t)fn;
        c->b[1] = RD_TCC_RGBA;
    }
    c = c ? rd__st_push(f, 0, RDC_FILTER) : NULL;
    if (c) {
        c->b[0] = (uint8_t)filter;
        c->b[1] = (uint8_t)filter;
    }
    c = c ? rd__st_push(f, 0, RDC_WRAP) : NULL;
    if (c) {
        c->b[0] = RD_WRAP_REPEAT;
        c->b[1] = RD_WRAP_REPEAT;
    }
    return c != NULL;
}

/* variant v's state (each sets every state the others change) */
static bool opVariant(RdFrame *f, uint32_t v, RdTex t32, RdTex t24)
{
    RdCmd *b = rd__st_push(f, 0, RDC_BLEND);
    if (b) {
        b->b[0] = RD_BLEND_LERP_AS;
        b->b[1] = 0;
        b->b[2] = 1;
    }
    bool ok = b && rd__st_push1(f, 0, RDC_FBA, (uint8_t)(v == 7u)) &&
              rd__st_push1(f, 0, RDC_PABE, 0) &&
              rd__st_push1(f, 0, RDC_TEXA, v == 5u ? RD_TEXA_7F_81_AEM : RD_TEXA_80_80) &&
              rd__st_push1(f, 0, RDC_ZWRITE, v == 6u ? RD_ZWRITE_OFF : RD_ZWRITE_ON) &&
              rd__st_test(f, 0,
                          v == 3u   ? RD_TEST_AT_GT64_FBONLY
                          : v == 4u ? RD_TEST_DATE1_Z_GEQUAL
                          : v == 6u ? RD_TEST_Z_ALWAYS
                                    : RD_TEST_Z_GEQUAL);
    switch (v) {
    case 1:
        ok = ok && opTex(f, t32, RD_TEXFN_MODULATE, RD_FILTER_LINEAR);
        break;
    case 2:
        ok = ok && opTex(f, t32, RD_TEXFN_DECAL, RD_FILTER_NEAREST);
        break;
    case 5:
        ok = ok && opTex(f, t24, RD_TEXFN_MODULATE, RD_FILTER_LINEAR);
        break;
    default:
        ok = ok && rd__st_push(f, 0, RDC_TEXTURE_OFF);
        break;
    }
    return ok;
}

static uint32_t opPlaces(uint32_t w, uint32_t h, float dx[OP_PLACES], float dy[OP_PLACES])
{
    const uint32_t nx = (w + OP_STEP_X - 1u) / OP_STEP_X, ny = (h + OP_STEP_Y - 1u) / OP_STEP_Y;
    uint32_t n = 0;
    for (uint32_t y = 0; y < ny; y++) {
        for (uint32_t x = 0; x < nx && n < OP_PLACES; x++) {
            dx[n] = (float)x * (float)OP_STEP_X - (float)w * 0.5f + (float)OP_STEP_X * 0.5f;
            dy[n] = (float)y * (float)OP_STEP_Y - (float)h * 0.5f + (float)OP_STEP_Y * 0.5f;
            n++;
        }
    }
    return n;
}

static bool opFrame(RdFrame *f, uint32_t scene, uint32_t w, uint32_t h, RdMesh mesh, RdTex t32,
                    RdTex t24, uint32_t *draws)
{
    memset(f, 0, sizeof(*f));
    rd__reset_state_block(&f->startState);
    f->gsW = w;
    f->gsH = h;
    bool ok = rd__st_target(f, 0, scene, w, h) && rd__st_push1(f, 0, RDC_COLCLAMP, 1) &&
              rd__st_push1(f, 0, RDC_SHADE, 1);
    float dx[OP_PLACES], dy[OP_PLACES];
    const uint32_t n = opPlaces(w, h, dx, dy);
    *draws = 0;
    for (uint32_t p = 0; p < n && ok; p++) {
        for (uint32_t v = 0; v < OP_VARIANTS && ok; v++) {
            if (v == 4u && p % OP_DATE_EVERY != 0u) {
                continue; /* DATE's snapshot a draw: a few */
            }
            ok = opVariant(f, v, t32, t24) &&
                 opPushMesh(f, mesh, dx[p] + 6.0f * (float)v, dy[p] + 4.0f * (float)v,
                            20000.0f * (float)(v + 1u));
            *draws += ok ? 1u : 0u;
        }
    }
    return ok;
}

/* a 64 x 64 texture of random texels, the alphas 0..128 (RGBA32) */
static RdTex opTexture(uint32_t seed, RdTexSrc src, const char *name)
{
    const uint32_t w = 64, h = 64;
    uint8_t *px = malloc((size_t)w * h * 4u);
    if (!px) {
        return (RdTex){0};
    }
    for (uint32_t i = 0; i < w * h * 4u; i++) {
        px[i] = opLevel(i, seed);
        if (i % 4u == 3u) {
            px[i] = (uint8_t)(px[i] % 129u);
        }
    }
    const RdTex t = src == RD_TEXSRC_RGBA32 ? rd_create_texture(w, h, px, RD_TEXA_80_80, name)
                                            : rd_create_texture_src(w, h, px, src, name);
    free(px);
    return t;
}

static size_t diffCount(const uint8_t *a, const uint8_t *b, size_t texels, size_t *first)
{
    size_t n = 0;
    *first = 0;
    for (size_t i = 0; i < texels; i++) {
        if (memcmp(&a[i * 4u], &b[i * 4u], 4) != 0) {
            if (!n++) {
                *first = i;
            }
        }
    }
    return n;
}

static bool s_opRunning;

static bool runSelftest(void)
{
    s_op.test = 0;
    s_op.words[0] = '\0';
    if (!g_rd.inited || !g_rd.hasDevice || !wanted() || !g_rd.noDual) {
        if (g_rd.hasDevice && wanted() && !g_rd.noDual) {
            snprintf(s_op.words, sizeof(s_op.words),
                     "not run: dual-source blending, no two-pass blend here");
        }
        stateNote();
        return true; /* not asked or nothing to draw */
    }
    s_op.test = 2;
    const uint32_t scene = rd_target(RD_TARGET_SCENE).id;
    RdTargetRec *t = scene ? rd__target_rec(scene) : NULL;
    if (!t || !t->withDepth || !t->color.id || !t->depth.id || t->format != RHI_FMT_RGBA8_UNORM ||
        t->w < OP_STEP_X || t->h < OP_STEP_Y) {
        snprintf(s_op.words, sizeof(s_op.words), "not run: no SCENE target to run it on");
        rd__log("two-pass blend: the one-pass rule's self-test not run (no SCENE target to run "
                "it on): every pair in two passes");
        stateNote();
        return false;
    }
    const double t0 = rd__now_ms();
    s_op.tw = t->tw;
    s_op.th = t->th;
    s_op.w = t->w;
    s_op.h = t->h;
    /* the targets owed their first clear are owed it again afterwards
       (rd_comp_spec.c's rule: the frame draws SCENE alone) */
    static uint32_t owed[RD_MAX_TARGETS];
    for (int i = 0; i < RD_MAX_TARGETS; i++) {
        const RdTargetRec *o = &g_rd.targets[i];
        owed[i] = o->live && o->clearPending ? o->color.id : 0u;
    }
    const uint32_t w = t->w, h = t->h;
    const size_t texels = (size_t)t->tw * t->th, size = texels * 4u;
    uint8_t *px[2] = {malloc(size), malloc(size)};
    const RdTex t32 = opTexture(1u, RD_TEXSRC_RGBA32, "rd one-pass self-test RGBA32");
    const RdTex t24 = opTexture(2u, RD_TEXSRC_RGB24, "rd one-pass self-test RGB24");
    const RdMesh mesh = opMesh();
    const bool sabotage = s_op.env == 2;
    const bool wasBlocked = g_rd.msaaBlocked;
    g_rd.msaaBlocked = true;
    static RdFrame f;
    uint32_t draws = 0, made = 0, stBad = 0, stN = 0;
    const char *err = NULL;
    if (!px[0] || !px[1]) {
        err = "no memory for its readbacks";
    } else if (!t32.id || !t24.id || !mesh.id) {
        err = "no memory for its textures or mesh";
    } else if (!opFrame(&f, scene, w, h, mesh, t32, t24, &draws)) {
        err = "could not build its frame";
    }
    if (!err) {
        /* the pipelines first, on the start-up set's worker threads (both
           arms ask for the same keys) */
        rd__replay_set_window(UINT32_MAX, 0);
        rd__pipeline_gather(true);
        s_op.testing = true;
        s_op.arm = 0;
        (void)rd__replay_frame(&f, 0, false);
        s_op.testing = false;
        s_op.arm = -1;
        rd__pipeline_gather(false);
        rd__replay_set_window(0, 0);
        rhi_wait_idle();
        if (!rhi_device_lost()) {
            bool stopped = false;
            made = rd__pipeline_create_gathered(&stopped);
        }
        s_op.tOne[0] = s_op.tOne[1] = s_op.tPairs[0] = s_op.tPairs[1] = 0;
        rd__health_selftest_begin(); /* every record collected from here */
        for (int arm = 0; arm < 2 && !err; arm++) {
            uint32_t rw = 0, rh = 0;
            t->clearPending = 1; /* each replay from SCENE's clear */
            s_op.testing = true;
            s_op.arm = arm;
            s_op.sabotageNow = sabotage && arm == 1;
            const bool done = rd__replay_frame(&f, 0, false) && !rhi_device_lost();
            s_op.testing = false;
            s_op.sabotageNow = false;
            s_op.arm = -1;
            if (!done || !rd__read_target(rd_target(RD_TARGET_SCENE), px[arm], size, &rw, &rh) ||
                (size_t)rw * rh != texels) {
                err = "replay or readback error";
            }
        }
        /* the last replays' records are collected by the replays after them
           (rd_perf.c: a slot's at its next use) */
        const uint32_t one1 = s_op.tOne[1], pairs1 = s_op.tPairs[1], pairs0 = s_op.tPairs[0],
                       one0 = s_op.tOne[0];
        for (int k = 0; k < RHI_FRAMES_IN_FLIGHT && !err; k++) {
            t->clearPending = 1;
            s_op.testing = true;
            s_op.arm = 0;
            if (!rd__replay_frame(&f, 0, false) || rhi_device_lost()) {
                err = "replay error";
            }
            s_op.testing = false;
            s_op.arm = -1;
        }
        stBad = rd__health_selftest_end(&stN);
        s_op.tOne[0] = one0;
        s_op.tOne[1] = one1;
        s_op.tPairs[0] = pairs0;
        s_op.tPairs[1] = pairs1;
    }
    rd__frame_free(&f);
    g_rd.msaaBlocked = wasBlocked;
    t->clearPending = 1;
    for (int i = 0; i < RD_MAX_TARGETS; i++) {
        RdTargetRec *o = &g_rd.targets[i];
        if (owed[i] && o->live && o->color.id == owed[i]) {
            o->clearPending = 1;
        }
    }
    if (t32.id) {
        rd_destroy_texture(t32);
    }
    if (t24.id) {
        rd_destroy_texture(t24);
    }
    if (mesh.id) {
        rd_destroy_vu_mesh(mesh);
    }
    size_t nd = 0, first = 0;
    if (!err) {
        nd = diffCount(px[0], px[1], texels, &first);
    }
    const uint32_t one1 = s_op.tOne[1], pairs1 = s_op.tPairs[1], one0 = s_op.tOne[0],
                   pairs0 = s_op.tPairs[0];
    const bool stampsAsked = rhi_timestamps_supported() && rhi_backend() == RHI_BACKEND_VULKAN;
    const double ms = rd__now_ms() - t0;
    if (err) {
        snprintf(s_op.words, sizeof(s_op.words), "not run: %s", err);
        rd__log("two-pass blend: the one-pass rule's self-test not run (%s): every pair in two "
                "passes",
                err);
    } else if (one1 < OP_MIN_ONE || one0 != 0u || pairs0 != pairs1) {
        snprintf(s_op.words, sizeof(s_op.words),
                 "not run: the pairs not taken as asked, %u of %u in one pass, %u of %u in the "
                 "kept replay",
                 one1, pairs1, one0, pairs0);
        rd__log("two-pass blend: the one-pass rule's self-test %s (at least %u wanted): every "
                "pair in two passes",
                s_op.words, OP_MIN_ONE);
    } else if (nd) {
        const uint8_t *a = &px[1][first * 4u], *b = &px[0][first * 4u];
        snprintf(s_op.words, sizeof(s_op.words), "failed at texel %zu", first);
        rd__log("two-pass blend: the one-pass rule's self-test failed: SCENE differs at %zu of %zu "
                "texels, the first (%zu,%zu): %02X%02X%02X%02X for %02X%02X%02X%02X%s: every pair "
                "in two passes",
                nd, texels, first % t->tw, first / t->tw, a[0], a[1], a[2], a[3], b[0], b[1], b[2],
                b[3], sabotage ? " (ICO_RD_ONEPASS=sabotage)" : "");
    } else if (stBad != 0u || (stampsAsked && stN == 0u)) {
        snprintf(s_op.words, sizeof(s_op.words),
                 "failed: %u of its %u pictures with recorded stamps unwritten", stBad, stN);
        rd__log("two-pass blend: the one-pass rule's self-test: the pictures matched, but %u of "
                "its %u pictures came back with the GPU's own sign of a reset (none collected "
                "counts as one): every pair in two passes",
                stBad, stN);
    } else {
        s_op.test = 1;
        /* no brackets in the words: the tool's stats line holds them */
        snprintf(s_op.words, sizeof(s_op.words),
                 "passed: SCENE the same at %zu texels of %ux%u, %u of %u pairs in one pass, %u "
                 "pictures' stamps written",
                 texels, t->tw, t->th, one1, pairs1, stN);
        rd__log("two-pass blend: the one-pass rule (self-test: SCENE the same at %zu texels, %ux%u "
                "at %ux%u texels, %u VU draws, %u of %u pairs in one pass, %u pictures' stamps "
                "written, %u pipelines made for it, in %.1f ms): the same bytes",
                texels, w, h, t->tw, t->th, draws, one1, pairs1, stN, made, ms);
    }
    free(px[0]);
    free(px[1]);
    stateNote();
    return s_op.test == 1;
}

bool rd__onepass_selftest(void)
{
    if (s_opRunning) {
        return true;
    }
    s_opRunning = true;
    const bool ok = runSelftest();
    s_opRunning = false;
    return ok;
}

void rd__onepass_scene_remade(void)
{
    if (!g_rd.hasDevice || s_opRunning || s_op.test == 0) {
        return;
    }
    const RdTargetRec *t = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    if (!t || (t->w == s_op.w && t->h == s_op.h && t->tw == s_op.tw && t->th == s_op.th)) {
        return;
    }
    (void)rd__onepass_selftest();
}

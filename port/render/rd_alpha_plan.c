/* rd_alpha_plan.c: the alpha/blend planner (rd_internal.h RdAlphaRange,
 * rd__alpha_range_of_draw, rd__plan_screen_draw_alpha; RdProfile.alphaPlan).
 *
 * A draw's planned passes (rd_pipeline.c rd__plan_screen_draw_ex) assume
 * any source alpha.  The range of As a draw's pixels can take
 * (rd__alpha_range_of_draw) can decide the alpha test or the blend for
 * every pixel at once; the planner then draws only what that outcome
 * needs (rd__plan_screen_draw_alpha), and every byte it writes is the byte
 * the planned passes write:
 *
 *   pass  the test passes for every As of the range: a pass that draws
 *         the passing pixels draws them all without the test, a pass that
 *         draws the failing ones draws nothing and goes
 *   fail  the test fails for every As: the other way round
 *   one   a LERP whose factor is exactly 0x80 (FIX 0x80, or As 0x80 over
 *         the whole range), or whose pixels PABE leaves unblended (As
 *         below 0x80 over the whole range): the GS stores Cs, and so does
 *         the pass with blending off
 *   zero  a LERP or a premultiplied add or subtract whose factor is 0:
 *         the GS stores Cd in R, G and B and As in alpha, and so does the
 *         pass with R, G and B masked and blending off (only where the
 *         device takes the blend and the mask as dynamic state,
 *         g_rd.dynBlend, so the mask makes no pipeline the start-up set
 *         lacks)
 *
 * docs/port/RENDER.md ("Pipelines", the alpha plan) has the arithmetic.
 * Not planned: DATE (its own passes), PRIM.AA1 and the text shaders (their
 * alpha is not As), and, for the factor rules, PABE over a range with and
 * without the MSB, COLCLAMP 0, mode 3, the Ad forms, mode 11 and the exact
 * blend (g_rd.dstMode: its passes compute the blend in the shader).
 *
 * The range: As is the texture function (TFX, TCC) of the texel's alpha
 * and the vertex's.  The texel's lies within the least and the largest
 * alpha of every texel of every level the device holds (rd_tex.h
 * rdtex_alpha_range: rd_upload.c scans each upload), under the TEXA in
 * force; the vertex's within the least and the largest of the draw's
 * vertices (the colour each program's vertex shader computes, vuAlphaOf),
 * since the GPU's interpolation and the planes' (vu_common.hlsli
 * vu_pixel_attrs) are convex combinations of them; a range whose ends
 * differ is widened by one each way against the float rounding of the
 * interpolation.  MODULATE is monotonic in both.  Anything else (a render
 * target sampled, a texture not uploaded, a program or a stream not known
 * here) is [0, 255], which decides nothing. */
#include <math.h>
#include <string.h>
#include "rd_internal.h"
#include "rd_mesh.h"
#include "rd_tex.h"
#include "shader_consts.h"

/* ------------------------------------------------------------ the range */

typedef struct Span {
    uint8_t lo, hi;
    bool any;
} Span;

static void spanAdd(Span *s, uint8_t a)
{
    if (!s->any) {
        s->lo = s->hi = a;
        s->any = true;
        return;
    }
    s->lo = a < s->lo ? a : s->lo;
    s->hi = a > s->hi ? a : s->hi;
}

/* The alpha lane of vu_rgba (vu_common.hlsli: vu_ftoi, bits 0..7) of x,
 * after min(x, 255) where the program clamps (vu_lit_colour,
 * vu_skin_vertex, the lit grid).  False for a NaN, whose conversion the
 * GPU leaves undefined. */
static bool vuAlphaOf(float x, bool clampHi, uint8_t *a)
{
    if (isnan(x)) {
        return false;
    }
    if (clampHi && !(x < 255.0f)) {
        x = 255.0f;
    }
    int32_t r;
    if (x >= 2147483648.0f) {
        r = INT32_MAX;
    } else if (x <= -2147483648.0f) {
        r = INT32_MIN;
    } else {
        r = (int32_t)x;
    }
    *a = (uint8_t)((uint32_t)r & 255u);
    return true;
}

/* lane of quadword at of a payload's stream (8-byte aligned only) */
static float qwLane(const uint8_t *stream, uint64_t at, int lane)
{
    float v;
    memcpy(&v, stream + at * 16u + (uint64_t)lane * 4u, sizeof(v));
    return v;
}

/* vu_skin_vertex's alpha under code 24 (the cluster's debug specular):
 * min(w * mem[2].w, 255), the product precise.  A device's float product
 * is the float on one side or the other of the exact one (both, unless the
 * exact one is a float): both are added. */
static bool fadedAlphaAdd(float w, float fade, Span *s)
{
    if (!isfinite(w) || !isfinite(fade)) {
        return false;
    }
    const double e = (double)w * (double)fade; /* exact: 24 + 24 bits */
    const float p = (float)e;
    float q = p;
    if ((double)p < e) {
        q = nextafterf(p, INFINITY);
    } else if ((double)p > e) {
        q = nextafterf(p, -INFINITY);
    }
    uint8_t a, b;
    if (!vuAlphaOf(p, true, &a) || !vuAlphaOf(q, true, &b)) {
        return false;
    }
    spanAdd(s, a);
    spanAdd(s, b);
    return true;
}

/* A static or skinned mesh's vertex alpha under its program: every vertex
 * of its stream (the batches, a sheet's tiles and the joined strips read
 * no other), the colour quadword of the program's layout (rd_mesh.h
 * RD_VU_QW_*: prelit qw 2, vu_prelit_vertex; lit, specular and reflection
 * qw 3, vu_lit_vertex; skinned qw 4, vu_skin_vertex), under code 24 of the
 * cluster program scaled by mem[2].w, fade (the cluster's fade alpha). */
static bool meshSpan(const RdMeshRec *m, uint8_t prog, uint8_t code, float fade, Span *s)
{
    uint32_t qpv, col;
    bool clampHi;
    switch (prog) {
    case RD_PROG_PRELIT:
        qpv = RD_VU_QW_PRELIT;
        col = 2;
        clampHi = false;
        break;
    case RD_PROG_LIT:
    case RD_PROG_LIT_SPEC:
        qpv = RD_VU_QW_LIT;
        col = 3;
        clampHi = true;
        break;
    case RD_PROG_REFLECT:
        qpv = RD_VU_QW_LIT;
        col = 3;
        clampHi = false; /* the colour as sent */
        break;
    case RD_PROG_SKIN:
    case RD_PROG_SKIN_SPEC:
        qpv = RD_VU_QW_SKIN;
        col = 4;
        clampHi = true;
        break;
    default:
        return false;
    }
    if (!m || !m->stream || m->qwPerVertex != qpv || m->vertexCount == 0) {
        return false;
    }
    const bool faded = prog == RD_PROG_SKIN_SPEC && code == 24;
    for (uint32_t v = 0; v < m->vertexCount; v++) {
        const float w = m->stream[(size_t)v * qpv + col][3];
        uint8_t a;
        if (faded) {
            if (!fadedAlphaAdd(w, fade, s)) {
                return false;
            }
        } else if (!vuAlphaOf(w, clampHi, &a)) {
            return false;
        } else {
            spanAdd(s, a);
        }
    }
    return true;
}

/* A mesh's span is the same for every draw of it in one replay: the
 * device copy (rd_upload.c rd__upload_meshes) and the ring copy
 * (rd_replay_vu.c vuSource) are both taken from the stream as the replay
 * finds it, and nothing rewrites a stream while a replay runs (the morphs
 * and the interpolation's blended copies are written before it).  Kept
 * per (mesh, replay, program, debug code and its fade) in a small
 * direct-mapped table. */
#define MESH_MEMO 256

typedef struct MeshMemo {
    uint32_t mesh, replay, fade;
    uint8_t prog, debug, ok, lo, hi;
} MeshMemo;

static MeshMemo s_meshMemo[MESH_MEMO];

static bool meshSpanMemo(uint32_t id, const RdMeshRec *m, uint8_t prog, uint8_t code, float fade,
                         Span *s)
{
    const uint8_t debug = (uint8_t)(code == 24);
    uint32_t fadeBits = 0;
    if (debug) {
        memcpy(&fadeBits, &fade, sizeof(fadeBits));
    }
    MeshMemo *e = &s_meshMemo[(id * 2654435761u) >> 24];
    if (e->replay == 0 || e->replay != g_rd.replayCounter || e->mesh != id || e->prog != prog ||
        e->debug != debug || e->fade != fadeBits) {
        Span t = {0, 0, false};
        e->ok = (uint8_t)(meshSpan(m, prog, code, fade, &t) && t.any);
        e->lo = t.lo;
        e->hi = t.hi;
        e->mesh = id;
        e->replay = g_rd.replayCounter;
        e->fade = fadeBits;
        e->prog = prog;
        e->debug = debug;
    }
    if (!e->ok) {
        return false;
    }
    spanAdd(s, e->lo);
    spanAdd(s, e->hi);
    return true;
}

/* A grid's or a particle batch's vertex alpha from the frame's copy of its
 * stream (rd_mesh.h, the recorded payload): a grid's strips each carry
 * their colour as the last of three header quadwords (vu_grid_vertex;
 * rd_replay_vu.c vuSource sets vu_batch (3, 1)), a particle its alpha in
 * the w of the second of its two quadwords after the six of the header
 * (vu_particle). */
static bool payloadSpan(const RdFrame *f, const RdCmd *c, uint8_t prog, Span *s)
{
    if (!f || !c) {
        return false;
    }
    RdVuPayload p;
    const uint8_t *at = f->payload + c->u[1];
    if (c->u[2] < sizeof(p)) {
        return false;
    }
    memcpy(&p, at, sizeof(p));
    const uint64_t head = sizeof(p) + sizeof(RdVuBlock) + (uint64_t)p.boneQw * 16u;
    if (head + (uint64_t)p.streamQw * 16u > c->u[2]) {
        return false;
    }
    const uint8_t *stream = at + head;
    const uint64_t nq = p.streamQw;
    if (c->type == RDC_GRID && (prog == RD_PROG_GRID || prog == RD_PROG_GRID_LIT)) {
        const uint64_t stride = 3u + (uint64_t)p.vertsPerBatch * p.qwPerVertex + 1u;
        for (uint32_t b = 0; b < p.batchCount; b++) {
            const uint64_t q = b * stride + 2u;
            uint8_t a;
            if (q >= nq || !vuAlphaOf(qwLane(stream, q, 3), prog == RD_PROG_GRID_LIT, &a)) {
                return false;
            }
            spanAdd(s, a);
        }
        return s->any;
    }
    if (c->type == RDC_PARTICLES && prog == RD_PROG_PARTICLE) {
        for (uint32_t i = 0; i < p.vertsPerBatch; i++) {
            const uint64_t q = 7u + 2u * (uint64_t)i;
            uint8_t a;
            if (q >= nq || !vuAlphaOf(qwLane(stream, q, 3), false, &a)) {
                return false;
            }
            spanAdd(s, a);
        }
        return s->any;
    }
    return false;
}

/* The vertex alpha of the draw: its vertex shader's (VU) or its vertices'
 * as drawn (screen prims) */
static bool vertexSpan(const RdAlphaSrc *src, Span *s)
{
    if (src->kind == RD_ALPHA_SRC_SCREEN) {
        if (!src->v || src->n == 0) {
            return false;
        }
        for (uint32_t i = 0; i < src->n; i++) {
            spanAdd(s, src->v[i].rgba[3]);
        }
        return true;
    }
    if (src->kind != RD_ALPHA_SRC_VU || !src->f || !src->c) {
        return false;
    }
    if (src->cmdType == RDC_MESH || src->cmdType == RDC_SKINNED) {
        RdVuPayload p;
        if (src->c->u[2] < sizeof(p) + sizeof(RdVuBlock)) {
            return false;
        }
        const uint8_t *at = src->f->payload + src->c->u[1];
        memcpy(&p, at, sizeof(p));
        /* mem[2].w of the VU block after the payload's fixed part */
        float fade;
        memcpy(&fade, at + sizeof(p) + 2u * 16u + 3u * 4u, sizeof(fade));
        return meshSpanMemo(src->c->u[0], src->mesh, src->prog, p.code, fade, s);
    }
    return payloadSpan(src->f, src->c, src->prog, s);
}

/* min((t * c) >> 7, 255): gs_tfx_mod (gs_math.hlsli) */
static uint8_t tfxMod(uint8_t t, uint8_t c)
{
    const uint32_t v = ((uint32_t)t * c) >> 7;
    return (uint8_t)(v > 255u ? 255u : v);
}

void rd__alpha_range_of_draw(const RdStateBlock *s, const RdAlphaSrc *src, RdAlphaRange *out)
{
    out->lo = 0;
    out->hi = 255;
    if (!g_rd.profile.alphaPlan || !s || !src) {
        return;
    }
    Span v = {0, 0, false};
    if (!vertexSpan(src, &v) || !v.any) {
        return;
    }
    if (v.lo != v.hi) {
        /* the interpolation's float rounding (vu_pixel_attrs, the GPU's) */
        v.lo = v.lo ? (uint8_t)(v.lo - 1u) : 0u;
        v.hi = v.hi < 255u ? (uint8_t)(v.hi + 1u) : 255u;
    }
    const RdDrawState *d = &s->ds;
    if (!d->texEnabled || d->tcc != RD_TCC_RGBA) {
        /* gs_texture_function: As is the vertex's */
        out->lo = v.lo;
        out->hi = v.hi;
        return;
    }
    uint8_t tlo = 0, thi = 255;
    const RdTexRec *t = rd__tex_rec(s->tex);
    RdTexAlphaScan scan;
    const bool known = t && t->kind == RD_TEXKIND_IMAGE && t->format == RD_TEXEL_RGBA8 &&
                       rd__tex_alpha_scan(s->tex, &scan) &&
                       rdtex_alpha_range(&scan, (RdTexSrc)t->src, d->texa, &tlo, &thi);
    if (!known) {
        tlo = 0;
        thi = 255;
    }
    if (d->texFn == RD_TEXFN_DECAL) {
        out->lo = tlo;
        out->hi = thi;
    } else {
        out->lo = tfxMod(tlo, v.lo);
        out->hi = tfxMod(thi, v.hi);
    }
    if (!known) {
        /* a texture not known here may draw untextured (rd_replay.c
         * resolveTexture's placeholder, DF_TEXTURED off): As the vertex's */
        out->lo = v.lo < out->lo ? v.lo : out->lo;
        out->hi = v.hi > out->hi ? v.hi : out->hi;
    }
}

/* ------------------------------------------------------------- the plan */

enum { AT_MIXED = 0, AT_PASS = 1, AT_FAIL = 2 };

/* gs_alpha_pass (gs_math.hlsli) over every a in lo..hi: AT_PASS when it
 * passes for all of them, AT_FAIL when it fails for all, else AT_MIXED */
static int testOutcome(uint32_t atst, uint32_t aref, uint32_t lo, uint32_t hi)
{
    switch (atst) {
    case RD_ATST_NEVER:
        return AT_FAIL;
    case RD_ATST_ALWAYS:
        return AT_PASS;
    case RD_ATST_LESS:
        return hi < aref ? AT_PASS : (lo >= aref ? AT_FAIL : AT_MIXED);
    case RD_ATST_LEQUAL:
        return hi <= aref ? AT_PASS : (lo > aref ? AT_FAIL : AT_MIXED);
    case RD_ATST_EQUAL:
        return lo == aref && hi == aref ? AT_PASS : (hi < aref || lo > aref ? AT_FAIL : AT_MIXED);
    case RD_ATST_GEQUAL:
        return lo >= aref ? AT_PASS : (hi < aref ? AT_FAIL : AT_MIXED);
    case RD_ATST_GREATER:
        return lo > aref ? AT_PASS : (hi <= aref ? AT_FAIL : AT_MIXED);
    case RD_ATST_NOTEQUAL:
        return hi < aref || lo > aref ? AT_PASS : (lo == aref && hi == aref ? AT_FAIL : AT_MIXED);
    default:
        return AT_MIXED;
    }
}

enum { BLEND_KEEP = 0, BLEND_OFF = 1, BLEND_RGB_OFF = 2 };

/* the factor rules (one, zero) for pass p of state d over As in lo..hi */
static int blendRule(const RdDrawPass *p, const RdDrawState *d, uint8_t lo, uint8_t hi)
{
    const RdBlendPath bp = rd__blend_path(p->key.gs.blend);
    if ((bp != RD_BP_LERP && bp != RD_BP_PREMUL_ADD && bp != RD_BP_PREMUL_REVSUB) || !d->colclamp ||
        g_rd.dstMode != RD_BLEND_EXACT_OFF) {
        return BLEND_KEEP;
    }
    const bool pabe = (p->flags & ICO_DF_PABE) != 0;
    const bool msbAll = lo >= 0x80u, msbNone = hi < 0x80u;
    if (pabe && !msbAll && !msbNone) {
        return BLEND_KEEP; /* some pixels blend, some write Cs */
    }
    /* gs_dual_out: under PABE an As whose MSB is clear writes Cs unblended */
    const bool unblended = pabe && msbNone;
    int f = -1; /* the factor C, where it is one value for the whole draw */
    if (p->flags & ICO_DF_FIX_FACTOR) {
        f = d->blendFix; /* the GS's FIX (the pass's is clamped to 0x80) */
    } else if (lo == hi) {
        f = lo;
    }
    int r = BLEND_KEEP;
    if (bp == RD_BP_LERP) {
        if (unblended || f == 0x80) {
            r = BLEND_OFF;
        } else if (f == 0) {
            r = BLEND_RGB_OFF;
        }
    } else if (!unblended && f == 0) {
        /* (Cs * 0) >> 7 added to or taken from Cd; unblended, the
         * premultiplied forms write Cs + Cd or Cd - Cs (gs_dual_out) */
        r = BLEND_RGB_OFF;
    }
    if (r == BLEND_RGB_OFF && !g_rd.dynBlend) {
        r = BLEND_KEEP; /* the mask would be a pipeline of its own */
    }
    return r;
}

static bool passTests(const RdDrawPass *p)
{
    return (p->modeZ & (1u << 8)) != 0;
}

bool rd__plan_screen_draw_alpha(const RdStateBlock *s, RdAlphaRange as, RdDrawPass out[2], int *n)
{
    if (!g_rd.profile.alphaPlan || !s || *n <= 0 || *n > 2 || as.lo > as.hi) {
        return false;
    }
    const RdDrawState *d = &s->ds;
    if (d->test.date != RD_DATE_OFF) {
        return false;
    }
    bool before = true; /* every planned pass draws without the test and the blending */
    for (int i = 0; i < *n; i++) {
        const RdPipeKeyInt *k = &out[i].key;
        if (k->gs.aa1 || (k->fs != RD_FS_SPRITE && k->fs != RD_FS_SPRITE_TEXA)) {
            return false;
        }
        before = before && !passTests(&out[i]) && k->gs.blend == RD_BLEND_COUNT;
    }
    RdDrawPass res[2];
    int m = 0;
    bool changed = false;
    for (int i = 0; i < *n; i++) {
        RdDrawPass p = out[i];
        if (passTests(&p)) {
            /* gs_alpha_discard: split 2 (the failing pixels' pass) discards
             * the passing pixels, split 0 (AFAIL KEEP) and 1 the failing */
            const uint32_t split = (p.modeZ >> 16) & 3u;
            const int o = testOutcome(p.modeZ & 0xFFu, p.aref, as.lo, as.hi);
            if (o != AT_MIXED) {
                changed = true;
                if (split == 2u ? o == AT_PASS : o == AT_FAIL) {
                    continue; /* pass, fail: every pixel discarded */
                }
                p.modeZ = 0; /* pass, fail: none discarded */
            }
        }
        const int b = blendRule(&p, d, as.lo, as.hi);
        if (b != BLEND_KEEP) {
            changed = true;
            /* the pass as rd_pipeline.c rd__plan_screen_draw_ex plans it
             * with ABE off */
            p.key.gs.blend = RD_BLEND_COUNT;
            p.flags &= ~(uint32_t)(ICO_DF_FIX_FACTOR | ICO_DF_PREMUL | ICO_DF_C1_DST);
            p.fix = d->blendFix;
            p.dstWant = 0;
            if (b == BLEND_RGB_OFF) {
                p.key.gs.colorMask &= 8u; /* zero: Cd kept, As written */
                if (p.key.gs.colorMask == 0 && p.key.gs.zwrite != RD_ZWRITE_ON) {
                    continue; /* it writes nothing */
                }
            }
        }
        res[m++] = p;
    }
    if (!changed) {
        return false;
    }
    bool after = m > 0;
    for (int i = 0; i < m; i++) {
        after = after && !passTests(&res[i]) && res[i].key.gs.blend == RD_BLEND_COUNT;
    }
    if (*n == 2 && m == 1) {
        g_rdPerf.alphaOnePass++;
    }
    if (after && !before) {
        g_rdPerf.alphaOpaque++;
    }
    for (int i = 0; i < m; i++) {
        out[i] = res[i];
    }
    *n = m;
    return true;
}

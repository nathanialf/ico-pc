/* rd_fixture.c: the fixture file, its generic patterns and generators, the
 * payload roles and the expansion to a frame dump (rd_fixture.h,
 * docs/port/TESTING_FIXTURES.md).
 *
 * Every generated value is built from integers: a float is written from its
 * bits (rd_fx_f32), a texel from integer arithmetic on its coordinates, so the
 * expansion is the same bytes on every compiler, CPU and byte order (every
 * word is written little-endian byte by byte). */
#include "rd_fixture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "miniz.h"
#include "rd_mesh.h"
#include "rd_velocity.h"
#include "shader_consts.h"

/* ------------------------------------------------------------ bytes */

void rd_fx_put(RdFxBuf *b, const void *p, size_t n)
{
    if (b->bad || n == 0) {
        return;
    }
    if (b->n + n > b->cap) {
        size_t cap = b->cap ? b->cap : 65536u;
        while (cap < b->n + n) {
            cap *= 2;
        }
        uint8_t *q = realloc(b->p, cap);
        if (!q) {
            b->bad = true;
            return;
        }
        b->p = q;
        b->cap = cap;
    }
    if (p) {
        memcpy(b->p + b->n, p, n);
    } else {
        memset(b->p + b->n, 0, n);
    }
    b->n += n;
}

void rd_fx_put32(RdFxBuf *b, uint32_t v)
{
    uint8_t q[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    rd_fx_put(b, q, 4);
}

void rd_fx_free(RdFxBuf *b)
{
    free(b->p);
    memset(b, 0, sizeof(*b));
}

uint32_t rd_fx_get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

void rd_fx_set32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

uint32_t rd_fx_f32(int32_t m, int e)
{
    if (m == 0) {
        return 0u;
    }
    const uint32_t sign = m < 0 ? 0x80000000u : 0u;
    uint32_t a = m < 0 ? (uint32_t)(-(int64_t)m) : (uint32_t)m;
    int p = 31;
    while (!(a >> p)) {
        p--;
    }
    const int ex = p + e + 127;
    if (ex <= 0 || ex >= 255 || p > 23) {
        return sign; /* never asked: |m| < 2^24 and small exponents */
    }
    const uint32_t mant = (a << (23 - p)) & 0x7FFFFFu;
    return sign | (uint32_t)ex << 23 | mant;
}

/* ------------------------------------------------------------ SHA-256 */

typedef struct Sha {
    uint32_t h[8];
    uint8_t blk[64];
    uint64_t len;
    uint32_t fill;
} Sha;

static const uint32_t s_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u};

static uint32_t ror(uint32_t x, int n)
{
    return x >> n | x << (32 - n);
}

static void shaBlock(Sha *s, const uint8_t *p)
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 |
               (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    }
    for (int i = 16; i < 64; i++) {
        const uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4], f = s->h[5],
             g = s->h[6], h = s->h[7];
    for (int i = 0; i < 64; i++) {
        const uint32_t S1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t t1 = h + S1 + ch + s_k[i] + w[i];
        const uint32_t S0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
        const uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = S0 + mj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    s->h[0] += a;
    s->h[1] += b;
    s->h[2] += c;
    s->h[3] += d;
    s->h[4] += e;
    s->h[5] += f;
    s->h[6] += g;
    s->h[7] += h;
}

void rd_fx_sha256(const void *data, size_t n, uint8_t out[32])
{
    static const uint32_t init[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                     0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    Sha s;
    memset(&s, 0, sizeof(s));
    memcpy(s.h, init, sizeof(init));
    const uint8_t *p = data;
    s.len = (uint64_t)n * 8u;
    while (n >= 64) {
        shaBlock(&s, p);
        p += 64;
        n -= 64;
    }
    uint8_t last[128];
    memset(last, 0, sizeof(last));
    if (n) {
        memcpy(last, p, n);
    }
    last[n] = 0x80;
    const size_t total = n + 1 + 8 <= 64 ? 64 : 128;
    for (int i = 0; i < 8; i++) {
        last[total - 1 - i] = (uint8_t)(s.len >> (8 * i));
    }
    shaBlock(&s, last);
    if (total == 128) {
        shaBlock(&s, last + 64);
    }
    for (int i = 0; i < 8; i++) {
        out[4 * i] = (uint8_t)(s.h[i] >> 24);
        out[4 * i + 1] = (uint8_t)(s.h[i] >> 16);
        out[4 * i + 2] = (uint8_t)(s.h[i] >> 8);
        out[4 * i + 3] = (uint8_t)s.h[i];
    }
}

void rd_fx_sha_hex(const uint8_t sha[32], char out[65])
{
    static const char hx[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[2 * i] = hx[sha[i] >> 4];
        out[2 * i + 1] = hx[sha[i] & 15];
    }
    out[64] = '\0';
}

/* ------------------------------------------------------------ patterns */

typedef struct Pattern {
    const char *name;
    int alpha; /* a one-byte format takes the alpha (1) or the red (0) */
} Pattern;

static const Pattern s_patterns[] = {
    {"grey", 0},       {"gradient", 0}, {"checker", 0}, {"bright", 0}, {"alpha_ramp", 1},
    {"date_split", 1}, {"opaque", 0},   {"clear", 1},   {"ramp", 0},   {"glyphs", 0},
};
#define PATTERN_COUNT ((int)(sizeof(s_patterns) / sizeof(s_patterns[0])))

static int patternIndex(const char *name)
{
    for (int i = 0; i < PATTERN_COUNT; i++) {
        if (strcmp(s_patterns[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

bool rd_fx_pattern_known(const char *name)
{
    return name && patternIndex(name) >= 0;
}

const char *rd_fx_pattern_name(int i)
{
    return i >= 0 && i < PATTERN_COUNT ? s_patterns[i].name : NULL;
}

/* x of w across 0..255 */
static uint8_t across(uint32_t x, uint32_t w)
{
    return w > 1 ? (uint8_t)((uint64_t)x * 255u / (w - 1u)) : 0u;
}

static void texelOf(int pat, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t o[4])
{
    uint8_t r = 128, g = 128, b = 128, a = 128;
    switch (pat) {
    case 1: /* gradient */
        r = across(x, w);
        g = across(y, h);
        break;
    case 2: /* checker: 8-texel cells */
        r = g = b = ((x >> 3) ^ (y >> 3)) & 1u ? 192u : 64u;
        break;
    case 3: { /* bright: a white patch over the middle half on a dark field */
        const int in = x >= w / 4 && x < w - w / 4 && y >= h / 4 && y < h - h / 4;
        r = g = b = in ? 255u : 32u;
        break;
    }
    case 4: /* alpha_ramp */
        r = g = b = 160;
        a = across(x, w);
        break;
    case 5: /* date_split: bit 7 set on the left half, clear on the right */
        a = x < (w + 1u) / 2u ? 0x80u : 0x40u;
        break;
    case 6: /* opaque */
        r = 96;
        g = 160;
        b = 224;
        break;
    case 7: /* clear */
        a = 0;
        break;
    case 8: /* ramp */
        r = g = b = w + h > 2 ? (uint8_t)((uint64_t)(x + y) * 255u / (w + h - 2u)) : 0u;
        break;
    case 9: /* glyphs: 4-texel coverage cells */
        r = g = b = ((x >> 2) ^ (y >> 2)) & 1u ? 255u : 0u;
        break;
    default: /* grey */
        break;
    }
    o[0] = r;
    o[1] = g;
    o[2] = b;
    o[3] = a;
}

void rd_fx_pattern_texel(const char *name, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                         uint8_t rgba[4])
{
    const int i = patternIndex(name ? name : "");
    texelOf(i < 0 ? 0 : i, x, y, w, h, rgba);
}

static void putPattern(RdFxBuf *o, int pat, uint32_t w, uint32_t h, uint32_t bpp)
{
    uint8_t row[4 * 1024];
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x0 = 0; x0 < w; x0 += 1024u) {
            const uint32_t n = w - x0 < 1024u ? w - x0 : 1024u;
            for (uint32_t i = 0; i < n; i++) {
                uint8_t t[4];
                texelOf(pat, x0 + i, y, w, h, t);
                if (bpp == 1) {
                    row[i] = s_patterns[pat].alpha ? t[3] : t[0];
                } else {
                    memcpy(row + 4 * i, t, 4);
                }
            }
            rd_fx_put(o, row, (size_t)n * bpp);
        }
    }
}

/* ------------------------------------------------------------ payload roles */

typedef struct SpanCtx {
    RdFxSpanFn fn;
    void *user;
    const RdCmd *c;
    bool ok;
} SpanCtx;

static void emit(SpanCtx *x, uint32_t off, uint32_t len, uint8_t role, uint8_t gen, uint32_t a,
                 uint32_t b, uint32_t vuOff)
{
    if (!x->ok || len == 0) {
        return;
    }
    RdFxSpan s = {off, len, role, gen, a, b, vuOff};
    if (!x->fn(x->user, x->c, &s)) {
        x->ok = false;
    }
}

static void gridSpans(SpanCtx *x, const RdVuPayload *h, uint32_t p, uint32_t vuOff)
{
    const uint32_t per = h->vertsPerBatch * h->qwPerVertex + 4u;
    for (uint32_t i = 0; i < h->batchCount; i++) {
        const uint32_t s0 = p + i * per * 16u;
        emit(x, s0, 32, RD_FX_KEEP, 0, 0, 0, 0); /* the VIF qword and the GIF tag */
        emit(x, s0 + 32u, (1u + h->vertsPerBatch * h->qwPerVertex) * 16u, RD_FX_GEN,
             RD_FX_GEN_GRIDSTRIP, i, h->vertsPerBatch, vuOff);
        emit(x, s0 + (per - 1u) * 16u, 16, RD_FX_KEEP, 0, 0, 0, 0); /* the MSCNT qword */
    }
}

bool rd_fx_payload_spans(const uint8_t *payload, uint32_t psz, const RdCmd *c, uint32_t ver,
                         RdFxSpanFn fn, void *user, uint32_t *vuSize)
{
    if (vuSize) {
        *vuSize = 0;
    }
    if (!rd__dump_cmd_valid(payload, psz, c)) {
        return false;
    }
    SpanCtx x = {fn, user, c, true};
    switch (c->type) {
    case RDC_SCREEN: {
        const uint32_t len = c->u[1] * (uint32_t)sizeof(RdScreenVtx);
        if (c->b[0] == RD_PRIM_SPRITES && c->b[3] == 0) {
            /* the effect code's rectangles: structure */
            emit(&x, c->u[0], len, RD_FX_KEEP, 0, 0, 0, 0);
        } else {
            emit(&x, c->u[0], len, RD_FX_GEN, c->b[3] ? RD_FX_GEN_TEXTQ : RD_FX_GEN_SCREEN, c->u[1],
                 0, 0);
        }
        break;
    }
    case RDC_COPY:
        emit(&x, c->u[2], (uint32_t)sizeof(RdCopyRec), RD_FX_KEEP, 0, 0, 0, 0);
        break;
    case RDC_MESH:
    case RDC_SKINNED:
    case RDC_GRID:
    case RDC_PARTICLES: {
        const uint32_t off = c->u[1], end = c->u[1] + c->u[2];
        const uint32_t head = (uint32_t)(sizeof(RdVuPayload) + sizeof(RdVuBlock));
        RdVuPayload h;
        memcpy(&h, payload + off, sizeof(h));
        emit(&x, off, head, RD_FX_KEEP, 0, 0, 0, 0);
        uint32_t p = off + head;
        emit(&x, p, h.boneQw * 16u, RD_FX_KEEP, 0, 0, 0, 0); /* the pose */
        p += h.boneQw * 16u;
        if (c->type == RDC_GRID) {
            gridSpans(&x, &h, p, off);
        } else if (c->type == RDC_PARTICLES) {
            emit(&x, p, 96, RD_FX_KEEP, 0, 0, 0, 0); /* the 6 header qwords */
            for (uint32_t i = 0; i < h.vertsPerBatch; i++) {
                const uint32_t q = p + 96u + i * 32u;
                emit(&x, q, 12, RD_FX_GEN, RD_FX_GEN_PARTICLE, i, 0, off);
                emit(&x, q + 12u, 20, RD_FX_KEEP, 0, 0, 0, 0); /* size, atlas cell, play state */
            }
        }
        p += h.streamQw * 16u;
        const uint64_t mats = (uint64_t)h.materialCount * sizeof(RdMaterial);
        if (p + mats <= end) {
            emit(&x, p, (uint32_t)mats, RD_FX_KEEP, 0, 0, 0, 0);
            p += (uint32_t)mats;
        }
        if (vuSize) {
            *vuSize = p - off;
        }
        if (ver >= 16u && c->type != RDC_PARTICLES && (c->u[3] & RD_VU_PREV) != 0u) {
            const uint32_t po = c->u[3] & RD_VU_PREV_OFF;
            const uint64_t size = rd__velocity_prev_size(&h);
            if (po <= psz && size <= (uint64_t)(psz - po)) {
                RdVuPayload ph;
                memcpy(&ph, payload + po, sizeof(ph));
                const bool same = ph.boneQw == h.boneQw && ph.streamQw == h.streamQw &&
                                  ph.batchCount == h.batchCount &&
                                  ph.vertsPerBatch == h.vertsPerBatch &&
                                  ph.qwPerVertex == h.qwPerVertex;
                if (same) {
                    emit(&x, po, head + h.boneQw * 16u, RD_FX_KEEP, 0, 0, 0, 0);
                    if (c->type == RDC_GRID) {
                        gridSpans(&x, &h, po + head + h.boneQw * 16u, off);
                    }
                } else {
                    emit(&x, po, head, RD_FX_KEEP, 0, 0, 0, 0);
                }
            }
        }
        break;
    }
    case RDC_SHADOW_STRIP:
        if (c->b[0] == RD_SHADOW_TRIS) {
            emit(&x, c->u[1], (c->u[0] + c->u[3]) * (uint32_t)sizeof(RdScreenVtx), RD_FX_GEN,
                 RD_FX_GEN_SHTRIS, c->u[0], c->u[3], 0);
        } else {
            emit(&x, c->u[1], c->u[0] * 16u, RD_FX_GEN, RD_FX_GEN_SHSTRIP, c->u[0], 0, 0);
        }
        break;
    case RDC_POST_STUB: {
        RdPostRec r;
        memcpy(&r, payload + c->u[1], sizeof(r));
        emit(&x, c->u[1], (uint32_t)sizeof(RdPostRec), RD_FX_KEEP, 0, 0, 0, 0);
        if (r.lutOffset != ~0u) {
            /* the reduction's four corners, or the fog's table (from its parameters) */
            emit(&x, r.lutOffset,
                 c->b[0] == RD_POST_REDUCTION ? 4u * (uint32_t)sizeof(RdScreenVtx) : 1024u,
                 RD_FX_KEEP, 0, 0, 0, 0);
        }
        break;
    }
    case RDC_OVERLAY_TEXT:
        if (c->b[0] == RD_OTEXT_ITEM) {
            emit(&x, c->u[1], RD_TEXT_BYTES, RD_FX_TEXT, 0, 0, 0, 0);
            emit(&x, c->u[1] + RD_TEXT_BYTES, c->u[2] - RD_TEXT_BYTES, RD_FX_KEEP, 0, 0, 0, 0);
        } else {
            emit(&x, c->u[1], c->u[2], RD_FX_KEEP, 0, 0, 0, 0);
        }
        break;
    default: /* RDC_WORLD_PRIMS: recorded, never replayed: left zero */
        break;
    }
    return x.ok;
}

/* ------------------------------------------------------------ the file */

void rd_fixture_free(RdFixture *fx)
{
    if (fx) {
        free(fx->skel);
        fx->skel = NULL;
        fx->skelBytes = 0;
    }
}

const char *rd_fixture_note(const RdFixture *fx, const char *key)
{
    const size_t k = strlen(key);
    for (uint32_t i = 0; i < fx->noteCount; i++) {
        if (strncmp(fx->notes[i], key, k) == 0 && fx->notes[i][k] == '=') {
            return fx->notes[i] + k + 1;
        }
    }
    return NULL;
}

bool rd_fixture_add_note(RdFixture *fx, const char *key, const char *value)
{
    if (fx->noteCount >= RD_FIXTURE_NOTES_MAX ||
        strlen(key) + 1u + strlen(value) >= RD_FIXTURE_NOTE_BYTES) {
        return false;
    }
    snprintf(fx->notes[fx->noteCount++], RD_FIXTURE_NOTE_BYTES, "%s=%s", key, value);
    return true;
}

void rd_fixture_drop_notes(RdFixture *fx, const char *key)
{
    const size_t k = strlen(key);
    uint32_t n = 0;
    for (uint32_t i = 0; i < fx->noteCount; i++) {
        if (!(strncmp(fx->notes[i], key, k) == 0 && fx->notes[i][k] == '=')) {
            if (n != i) {
                memcpy(fx->notes[n], fx->notes[i], RD_FIXTURE_NOTE_BYTES);
            }
            n++;
        }
    }
    fx->noteCount = n;
}

static void setWhy(char *why, size_t size, const char *text)
{
    if (why && size) {
        snprintf(why, size, "%s", text);
    }
}

bool rd_fixture_read_mem(const uint8_t *p, size_t n, RdFixture *fx, char *why, size_t whySize)
{
    memset(fx, 0, sizeof(*fx));
    size_t o = 0;
    if (n < 8 + 4 + 4 + 32 + 8 + 4 || memcmp(p, RD_FIXTURE_MAGIC, 8) != 0) {
        setWhy(why, whySize, "not a fixture (no ICORDFX1 head)");
        return false;
    }
    o = 8;
    fx->format = rd_fx_get32(p + o);
    fx->dumpVersion = rd_fx_get32(p + o + 4);
    o += 8;
    if (fx->format != RD_FIXTURE_FORMAT || fx->dumpVersion < RD_FIXTURE_DUMP_MIN ||
        fx->dumpVersion > RD_DUMP_VERSION) {
        char t[96];
        snprintf(t, sizeof(t), "format %u / dump version %u: not one this build reads", fx->format,
                 fx->dumpVersion);
        setWhy(why, whySize, t);
        return false;
    }
    memcpy(fx->sha, p + o, 32);
    o += 32;
    fx->expandedBytes = (uint64_t)rd_fx_get32(p + o) | (uint64_t)rd_fx_get32(p + o + 4) << 32;
    o += 8;
    const uint32_t notes = rd_fx_get32(p + o);
    o += 4;
    if (notes > RD_FIXTURE_NOTES_MAX) {
        setWhy(why, whySize, "too many notes");
        return false;
    }
    for (uint32_t i = 0; i < notes; i++) {
        if (o + 2 > n) {
            setWhy(why, whySize, "truncated in the notes");
            return false;
        }
        const uint32_t len = (uint32_t)p[o] | (uint32_t)p[o + 1] << 8;
        o += 2;
        if (len >= RD_FIXTURE_NOTE_BYTES || o + len > n) {
            setWhy(why, whySize, "a note too long or truncated");
            return false;
        }
        memcpy(fx->notes[i], p + o, len);
        fx->notes[i][len] = '\0';
        o += len;
    }
    fx->noteCount = notes;
    if (o + 8 > n) {
        setWhy(why, whySize, "truncated before the skeleton");
        return false;
    }
    const uint32_t raw = rd_fx_get32(p + o), z = rd_fx_get32(p + o + 4);
    o += 8;
    if ((uint64_t)o + z != n) {
        setWhy(why, whySize, "the deflated skeleton does not end the file");
        return false;
    }
    size_t got = 0;
    void *sk = tinfl_decompress_mem_to_heap(p + o, z, &got, 0);
    if (!sk || got != raw) {
        free(sk);
        setWhy(why, whySize, "the skeleton does not inflate to its size");
        return false;
    }
    fx->skel = sk;
    fx->skelBytes = got;
    return true;
}

bool rd_fixture_read(const char *path, RdFixture *fx, char *why, size_t whySize)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        setWhy(why, whySize, "cannot open the fixture");
        memset(fx, 0, sizeof(*fx));
        return false;
    }
    RdFxBuf b = {0};
    uint8_t blk[65536];
    size_t r;
    while ((r = fread(blk, 1, sizeof(blk), fp)) > 0) {
        rd_fx_put(&b, blk, r);
    }
    fclose(fp);
    bool ok = !b.bad && rd_fixture_read_mem(b.p, b.n, fx, why, whySize);
    rd_fx_free(&b);
    return ok;
}

bool rd_fixture_write(const char *path, const RdFixture *fx, char *why, size_t whySize)
{
    size_t z = 0;
    void *zb = tdefl_compress_mem_to_heap(fx->skel, fx->skelBytes, &z, TDEFL_MAX_PROBES_MASK);
    if (!zb) {
        setWhy(why, whySize, "deflate failed");
        return false;
    }
    RdFxBuf b = {0};
    rd_fx_put(&b, RD_FIXTURE_MAGIC, 8);
    rd_fx_put32(&b, RD_FIXTURE_FORMAT);
    rd_fx_put32(&b, fx->dumpVersion);
    rd_fx_put(&b, fx->sha, 32);
    rd_fx_put32(&b, (uint32_t)fx->expandedBytes);
    rd_fx_put32(&b, (uint32_t)(fx->expandedBytes >> 32));
    rd_fx_put32(&b, fx->noteCount);
    for (uint32_t i = 0; i < fx->noteCount; i++) {
        const size_t len = strlen(fx->notes[i]);
        const uint8_t l2[2] = {(uint8_t)len, (uint8_t)(len >> 8)};
        rd_fx_put(&b, l2, 2);
        rd_fx_put(&b, fx->notes[i], len);
    }
    rd_fx_put32(&b, (uint32_t)fx->skelBytes);
    rd_fx_put32(&b, (uint32_t)z);
    rd_fx_put(&b, zb, z);
    mz_free(zb);
    bool ok = !b.bad;
    if (ok) {
        FILE *fp = fopen(path, "wb");
        ok = fp && fwrite(b.p, 1, b.n, fp) == b.n;
        ok = fp && fclose(fp) == 0 && ok;
        if (!ok) {
            setWhy(why, whySize, "cannot write the fixture");
        }
    } else {
        setWhy(why, whySize, "out of memory");
    }
    rd_fx_free(&b);
    return ok;
}

/* ------------------------------------------------------------ the skeleton */

typedef struct SkTex {
    uint32_t hdr[8];
    char pattern[32];
    uint32_t levels;
} SkTex;

typedef struct SkMesh {
    uint32_t id, nv, qpv, ni, nb;
    char gen[32];
    int32_t sizeExp;
    uint32_t bones;
    int32_t centre[3]; /* the placement section's: the cube's centre in lattice steps (else 0) */
    uint32_t form;     /* the placement section's RD_FX_FORM_* (RD_FX_PLACE_TAG2; else the cube) */
    const uint8_t *idx, *batches;
} SkMesh;

typedef struct SkFacing {
    uint32_t id, nv;
} SkFacing;

typedef struct SkExp {
    uint32_t off;
    int32_t e;
} SkExp;

/* the skeleton, parsed (pointers into fx->skel) */
typedef struct Sk {
    uint32_t ver;
    const uint8_t *head;
    size_t headBytes;
    RdStateBlock start;
    uint32_t gsW, gsH;
    const uint8_t *listCmds[RD_LIST_COUNT];
    uint32_t listCount[RD_LIST_COUNT];
    const uint8_t *payload;
    uint32_t psz;
    SkTex *tex;
    uint32_t nt;
    const uint8_t *temps;
    uint32_t nr;
    SkMesh *mesh;
    uint32_t nm;
    const uint8_t *tick; /* version 14: 4 + 128 bytes */
    SkFacing *facing;
    uint32_t nf;
    SkExp *exps;
    uint32_t ne;
    uint32_t placed;     /* the placement section's meshes */
    uint32_t sheets;     /* of them generated as sheets (RD_FX_PLACE_TAG2) */
    bool scene;          /* the section is RD_FX_PLACE_TAG2's (@scene place) */
    const uint8_t *offs; /* and its prims generated beside the target: payload offsets */
    uint32_t noff;
} Sk;

typedef struct Rd {
    const uint8_t *p;
    size_t n, o;
    bool bad;
} Rd;

static uint32_t r32(Rd *r)
{
    if (r->bad || r->o + 4 > r->n) {
        r->bad = true;
        return 0;
    }
    const uint32_t v = rd_fx_get32(r->p + r->o);
    r->o += 4;
    return v;
}

static const uint8_t *rraw(Rd *r, size_t n)
{
    if (r->bad || n > r->n - r->o) {
        r->bad = true;
        return NULL;
    }
    const uint8_t *p = r->p + r->o;
    r->o += n;
    return p;
}

static bool rname(Rd *r, char *out, size_t size)
{
    const uint8_t *l = rraw(r, 1);
    const uint8_t *s = l ? rraw(r, *l) : NULL;
    if (!s || *l >= size) {
        r->bad = true;
        return false;
    }
    memcpy(out, s, *l);
    out[*l] = '\0';
    return true;
}

uint32_t rd_fx_state_bytes(uint32_t ver)
{
    return ver == 3u ? RD_STATE_BLOCK_V3_SIZE
                     : (ver <= 8u ? RD_STATE_BLOCK_V8_SIZE : (uint32_t)sizeof(RdStateBlock));
}

size_t rd_fx_head_bytes(uint32_t ver)
{
    return 8 + 16 + 8 + (ver >= 10u ? 12u : 0u) + (ver >= 12u ? (size_t)RD_DUMP_REGION_BYTES : 0u) +
           12 + sizeof(RdCamera) + 2 * (size_t)rd_fx_state_bytes(ver);
}

static void skFree(Sk *s)
{
    free(s->tex);
    free(s->mesh);
    free(s->facing);
    free(s->exps);
    memset(s, 0, sizeof(*s));
}

static bool skParse(const RdFixture *fx, Sk *s, char *why, size_t whySize)
{
    memset(s, 0, sizeof(*s));
    Rd r = {fx->skel, fx->skelBytes, 0, false};
    s->ver = fx->dumpVersion;
    s->headBytes = rd_fx_head_bytes(s->ver);
    s->head = rraw(&r, s->headBytes);
    if (!s->head || memcmp(s->head, RD_DUMP_MAGIC, 8) != 0 || rd_fx_get32(s->head + 8) != s->ver ||
        rd_fx_get32(s->head + 12) != sizeof(RdCmd) ||
        rd_fx_get32(s->head + 16) != rd_fx_state_bytes(s->ver) ||
        rd_fx_get32(s->head + 20) != sizeof(RdScreenVtx)) {
        setWhy(why, whySize, "the skeleton's dump head is not this build's layout");
        return false;
    }
    s->gsW = rd_fx_get32(s->head + 24);
    s->gsH = rd_fx_get32(s->head + 28);
    memset(&s->start, 0, sizeof(s->start)); /* an older block: the fields after it 0, as loaded */
    memcpy(&s->start, s->head + s->headBytes - 2 * (size_t)rd_fx_state_bytes(s->ver),
           rd_fx_state_bytes(s->ver));
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        s->listCount[l] = r32(&r);
        if (s->listCount[l] >= (1u << 24)) {
            r.bad = true;
        }
        s->listCmds[l] = rraw(&r, (size_t)s->listCount[l] * sizeof(RdCmd));
    }
    s->psz = r32(&r);
    s->payload = rraw(&r, s->psz);
    s->nt = r32(&r);
    if (s->nt > 4096) {
        r.bad = true;
    }
    s->tex = calloc(s->nt ? s->nt : 1, sizeof(SkTex));
    for (uint32_t i = 0; s->tex && !r.bad && i < s->nt; i++) {
        SkTex *t = &s->tex[i];
        for (int k = 0; k < 8; k++) {
            t->hdr[k] = r32(&r);
        }
        if (t->hdr[1] == RD_TEXKIND_IMAGE) {
            rname(&r, t->pattern, sizeof(t->pattern));
            t->levels = r32(&r);
            if (!rd_fx_pattern_known(t->pattern) || t->levels < 1 || t->levels > 16 ||
                t->hdr[4] == 0 || t->hdr[5] == 0 || t->hdr[4] > 8192 || t->hdr[5] > 8192) {
                r.bad = true;
            }
        }
    }
    s->temps = NULL;
    s->nr = r32(&r);
    s->temps = rraw(&r, (size_t)s->nr * 20u);
    s->nm = r32(&r);
    if (s->nm > 4096) {
        r.bad = true;
    }
    s->mesh = calloc(s->nm ? s->nm : 1, sizeof(SkMesh));
    for (uint32_t i = 0; s->mesh && !r.bad && i < s->nm; i++) {
        SkMesh *m = &s->mesh[i];
        m->id = r32(&r);
        m->nv = r32(&r);
        m->qpv = r32(&r);
        m->ni = r32(&r);
        m->nb = r32(&r);
        rname(&r, m->gen, sizeof(m->gen));
        m->sizeExp = (int32_t)r32(&r);
        m->bones = r32(&r);
        if (strcmp(m->gen, RD_FX_MESH_GEN) != 0 || m->nv >= (1u << 22) || m->qpv > 8 ||
            m->ni >= (1u << 24) || m->nb >= (1u << 20) || m->sizeExp < -40 || m->sizeExp > 40) {
            r.bad = true;
            break;
        }
        m->idx = rraw(&r, (size_t)m->ni * 4u);
        m->batches = rraw(&r, (size_t)m->nb * sizeof(RdVuBatchRec));
    }
    if (s->ver >= 14u) {
        s->tick = rraw(&r, 4 + 128);
    }
    if (s->ver >= 15u) {
        s->nf = r32(&r);
        if (s->nf > 4096) {
            r.bad = true;
        }
        s->facing = calloc(s->nf ? s->nf : 1, sizeof(SkFacing));
        for (uint32_t i = 0; s->facing && !r.bad && i < s->nf; i++) {
            s->facing[i].id = r32(&r);
            s->facing[i].nv = r32(&r);
            if (s->facing[i].nv >= (1u << 22)) {
                r.bad = true;
            }
        }
    }
    s->ne = r32(&r);
    if (s->ne > (1u << 20)) {
        r.bad = true;
    }
    s->exps = calloc(s->ne ? s->ne : 1, sizeof(SkExp));
    for (uint32_t i = 0; s->exps && !r.bad && i < s->ne; i++) {
        s->exps[i].off = r32(&r);
        s->exps[i].e = (int32_t)r32(&r);
    }
    s->placed = s->noff = s->sheets = 0;
    s->scene = false;
    s->offs = NULL;
    if (!r.bad && s->mesh && r.o != r.n) {
        /* the placement section (a fixture made with @lamps place, @fans off
         * or @scene place); never empty, so a fixture has one encoding */
        const uint32_t tag = r32(&r);
        s->scene = tag == RD_FX_PLACE_TAG2;
        if (tag != RD_FX_PLACE_TAG && !s->scene) {
            r.bad = true;
        }
        s->placed = r32(&r);
        if (s->placed > s->nm) {
            r.bad = true;
        }
        for (uint32_t i = 0; !r.bad && i < s->placed; i++) {
            const uint32_t id = r32(&r);
            int32_t c[3];
            for (int a = 0; a < 3; a++) {
                c[a] = (int32_t)r32(&r);
                if (c[a] > RD_FX_PLACE_MAX || c[a] < -RD_FX_PLACE_MAX) {
                    r.bad = true;
                }
            }
            const uint32_t form = s->scene ? r32(&r) : RD_FX_FORM_CUBE;
            if (form > RD_FX_FORM_SHEET + 2u) {
                r.bad = true;
            }
            s->sheets += form != RD_FX_FORM_CUBE;
            SkMesh *m = NULL;
            for (uint32_t k = 0; k < s->nm; k++) {
                if (s->mesh[k].id == id) {
                    m = &s->mesh[k];
                    break;
                }
            }
            if (!m || r.bad) {
                r.bad = true;
                break;
            }
            memcpy(m->centre, c, sizeof(c));
            m->form = form;
        }
        s->noff = r32(&r);
        if (s->noff > (1u << 20) || (s->placed == 0 && s->noff == 0)) {
            r.bad = true;
        }
        s->offs = rraw(&r, (size_t)s->noff * 4u);
    }
    if (r.bad || !s->tex || !s->mesh || (s->ver >= 15u && !s->facing) || !s->exps || r.o != r.n) {
        setWhy(why, whySize,
               r.o != r.n && !r.bad
                   ? "bytes after the skeleton's end"
                   : "the skeleton does not parse (a section holds art, or is cut)");
        skFree(s);
        return false;
    }
    return true;
}

/* ------------------------------------------------------------ generators */

#define F_ONE 0x3F800000u
#define F_HALF 0x3F000000u

/* the generated colour: (112, 120, 136, 127), GS 0x80 = 1.0 with the
 * alpha as the packets send it */
static void putColour(uint8_t *q)
{
    rd_fx_set32(q, rd_fx_f32(112, 0));
    rd_fx_set32(q + 4, rd_fx_f32(120, 0));
    rd_fx_set32(q + 8, rd_fx_f32(136, 0));
    rd_fx_set32(q + 12, rd_fx_f32(127, 0));
}

/* vertex j of strip s on the lattice of step 2^(e-3) inside [-2^e, 2^e]^3
 * about the centre c (lattice steps; the model's origin: 0, 0, 0).  A sheet
 * (form RD_FX_FORM_SHEET + f, @scene place): every strip zigzags over the
 * square of half side 2^e across the axes f + 1 and f + 2 (mod 3), flat at
 * the centre along f, its corners in the order A (-, -), B (-, +), C (+, -),
 * D (+, +), A, ...: the triangles ABC, BCD, CDA, DAB, the square split along
 * both diagonals (sheetPos) */
static void putPos(uint8_t *q, uint32_t s, uint32_t j, int e, const int32_t c[3], uint32_t form)
{
    const uint32_t col = j >> 1, side = j & 1u;
    int32_t p[3];
    if (form >= RD_FX_FORM_SHEET) {
        const uint32_t f = form - RD_FX_FORM_SHEET;
        p[f] = c[f];
        p[(f + 1u) % 3u] = c[(f + 1u) % 3u] + (col % 2u ? 8 : -8);
        p[(f + 2u) % 3u] = c[(f + 2u) % 3u] + (side ? 8 : -8);
    } else {
        p[0] = c[0] - 8 + (int32_t)(col % 16u);
        p[1] = c[1] - 8 + (int32_t)((s + col / 16u) % 15u) + (int32_t)side;
        p[2] = c[2] - 8 + (int32_t)((s * 5u) % 17u);
    }
    rd_fx_set32(q, rd_fx_f32(p[0], e - 3));
    rd_fx_set32(q + 4, rd_fx_f32(p[1], e - 3));
    rd_fx_set32(q + 8, rd_fx_f32(p[2], e - 3));
    rd_fx_set32(q + 12, F_ONE);
}

static void putNormal(uint8_t *q, uint32_t s)
{
    static const uint32_t axes[3][3] = {{0, 0, F_ONE}, {0, F_ONE, 0}, {F_ONE, 0, 0}};
    const uint32_t *a = axes[s % 3u];
    rd_fx_set32(q, a[0]);
    rd_fx_set32(q + 4, a[1]);
    rd_fx_set32(q + 8, a[2]);
    rd_fx_set32(q + 12, F_ONE);
}

static void putSt(uint8_t *q, uint32_t s, uint32_t j, bool first)
{
    rd_fx_set32(q, rd_fx_f32((int32_t)((j >> 1) % 17u), -4));
    rd_fx_set32(q + 4, rd_fx_f32((int32_t)(s % 16u + (j & 1u)), -4));
    rd_fx_set32(q + 8, F_ONE);
    rd_fx_set32(q + 12, first ? 0u : F_ONE); /* the strip flag: ST.w < 1 starts a strip */
}

/* a mesh's stream: nv x qpv qwords */
static bool genMesh(RdFxBuf *o, const SkMesh *m)
{
    const uint32_t nv = m->nv, qpv = m->qpv;
    uint8_t *kick = calloc(nv ? nv : 1, 1);
    uint8_t *first = malloc(nv ? nv : 1);
    if (!kick || !first) {
        free(kick);
        free(first);
        return false;
    }
    for (uint32_t i = 0; i < m->ni; i++) {
        const uint32_t k = (rd_fx_get32(m->idx + 4u * i) & ICO_VU_INDEX_MASK) / 4u;
        if (k < nv) {
            kick[k] = 1;
        }
    }
    memset(first, 1, nv ? nv : 1);
    /* a strip starts where neither the vertex nor the next one kicks (the
     * kicks are the index list's; rd_mesh.h: vertex k >= 2 kicks unless k or
     * k - 1 carries the flag) */
    for (uint32_t b = 0; b < m->nb; b++) {
        const uint8_t *br = m->batches + (size_t)b * sizeof(RdVuBatchRec);
        const uint32_t fv = rd_fx_get32(br + 8), vc = rd_fx_get32(br + 12);
        for (uint32_t k = 0; k < vc && (uint64_t)fv + k < nv; k++) {
            const uint32_t v = fv + k;
            const bool next = k + 1u < vc && v + 1u < nv && kick[v + 1u];
            first[v] = (uint8_t)(!kick[v] && !next);
        }
    }
    const uint32_t nb = m->bones ? m->bones : 1u;
    uint8_t qw[8 * 16];
    uint32_t s = 0, j = 0;
    for (uint32_t v = 0; v < nv; v++) {
        if (first[v] && v > 0) {
            s++;
            j = 0;
        } else if (v > 0) {
            j++;
        }
        memset(qw, 0, sizeof(qw));
        putPos(qw, s, j, m->sizeExp, m->centre, m->form);
        if (qpv >= 4) {
            putNormal(qw + 16, s);
        }
        if (qpv >= 5) {
            rd_fx_set32(qw + 32, 16u + 4u * (s % nb));
            rd_fx_set32(qw + 36, F_HALF);
            rd_fx_set32(qw + 40, 16u + 4u * ((s + 1u) % nb));
            rd_fx_set32(qw + 44, F_HALF);
        }
        if (qpv >= 3) {
            putSt(qw + 16u * (qpv - 2u), s, j, first[v] != 0);
            putColour(qw + 16u * (qpv - 1u));
        } else if (qpv == 2) {
            putSt(qw + 16, s, j, first[v] != 0);
        }
        rd_fx_put(o, qw, 16u * qpv);
    }
    free(kick);
    free(first);
    return true;
}

typedef struct GenCtx {
    const Sk *sk;
    uint8_t *pay;         /* the expansion's payload */
    uint32_t w, h;        /* the target's GS size at the command */
    uint32_t shadowIndex; /* the frame's shadow commands so far */
} GenCtx;

static int expOf(const Sk *sk, uint32_t vuOff)
{
    for (uint32_t i = 0; i < sk->ne; i++) {
        if (sk->exps[i].off == vuOff) {
            return sk->exps[i].e;
        }
    }
    return 0;
}

static int32_t gsX(uint32_t w, uint32_t px)
{
    return (2048 - (int32_t)(w / 2u)) * 16 + (int32_t)px * 16;
}

static void putVtx(uint8_t *q, int32_t x, int32_t y, uint32_t z, uint32_t s, uint32_t t,
                   const uint8_t rgba[4])
{
    rd_fx_set32(q, (uint32_t)x);
    rd_fx_set32(q + 4, (uint32_t)y);
    rd_fx_set32(q + 8, z);
    rd_fx_set32(q + 12, s);
    rd_fx_set32(q + 16, t);
    rd_fx_set32(q + 20, F_ONE);
    memcpy(q + 24, rgba, 4);
}

/* the footprint of shadow command j: a quarter of the frame each way, about
 * the middle, moved by j along x */
static void footprint(uint32_t w, uint32_t h, uint32_t j, uint32_t *x0, uint32_t *y0, uint32_t *fw,
                      uint32_t *fh)
{
    *fw = w / 4u > 2u ? w / 4u : 2u;
    *fh = h / 4u > 2u ? h / 4u : 2u;
    int32_t x =
        (int32_t)(w / 2u) - (int32_t)(*fw / 2u) + ((int32_t)(j % 5u) - 2) * (int32_t)(w / 16u);
    if (x < 0) {
        x = 0;
    }
    if ((uint32_t)x + *fw > w) {
        x = (int32_t)(w > *fw ? w - *fw : 0u);
    }
    *x0 = (uint32_t)x;
    *y0 = h / 2u > *fh / 2u ? h / 2u - *fh / 2u : 0u;
}

/* n vertices of triangles tiling the footprint at depth z, tagged from tag0 */
static void shadowSet(uint8_t *q, uint32_t n, uint32_t w, uint32_t h, uint32_t j, uint32_t z,
                      uint32_t tag0)
{
    uint32_t x0, y0, fw, fh;
    footprint(w, h, j, &x0, &y0, &fw, &fh);
    const uint32_t ntri = n / 3u;
    uint32_t cols = 1;
    while (cols * cols * 2u < ntri) {
        cols++;
    }
    const uint32_t cells = (ntri + 1u) / 2u;
    const uint32_t rows = cells ? (cells + cols - 1u) / cols : 1u;
    for (uint32_t t = 0; t < ntri; t++) {
        const uint32_t cell = t / 2u, half = t & 1u, r = cell / cols, c = cell % cols;
        const uint32_t gx0 = x0 + fw * c / cols, gx1 = x0 + fw * (c + 1u) / cols;
        const uint32_t gy0 = y0 + fh * r / rows, gy1 = y0 + fh * (r + 1u) / rows;
        const uint32_t px[3] = {half ? gx1 : gx0, gx1, gx0};
        const uint32_t py[3] = {gy0, half ? gy1 : gy0, gy1};
        const uint32_t tag = tag0 + t + 1u;
        const uint8_t rgba[4] = {(uint8_t)tag, (uint8_t)(tag >> 8), (uint8_t)(tag >> 16),
                                 (uint8_t)(tag >> 24)};
        for (int k = 0; k < 3; k++) {
            putVtx(q + (size_t)(3u * t + (uint32_t)k) * sizeof(RdScreenVtx), gsX(w, px[k]),
                   gsX(h, py[k]), z, 0, 0, rgba);
        }
    }
    for (uint32_t v = ntri * 3u; v < n; v++) {
        static const uint8_t none[4] = {0, 0, 0, 0};
        putVtx(q + (size_t)v * sizeof(RdScreenVtx), gsX(w, x0), gsX(h, y0), z, 0, 0, none);
    }
}

static bool genSpan(void *user, const RdCmd *c, const RdFxSpan *s)
{
    GenCtx *g = user;
    if (s->role != RD_FX_GEN) {
        return true;
    }
    uint8_t *q = g->pay + s->off;
    const uint32_t w = g->w ? g->w : g->sk->gsW, h = g->h ? g->h : g->sk->gsH;
    static const uint8_t grey[4] = {128, 128, 128, 128};
    switch (s->gen) {
    case RD_FX_GEN_SCREEN:
    case RD_FX_GEN_TEXTQ: {
        const bool fixedUv = c->b[2] != 0;
        for (uint32_t v = 0; v < s->a; v++) {
            uint8_t *o = q + (size_t)v * sizeof(RdScreenVtx);
            if (c->b[0] == RD_PRIM_SPRITES) {
                /* a row of 8-pixel cells from the top left, each cell's
                 * texels the sheet's first 8 x 8 */
                const uint32_t p = v >> 1, k = v & 1u;
                const uint32_t cx = 8u + (p % 32u) * 8u + 8u * k,
                               cy = 8u + (p / 32u % 32u) * 8u + 8u * k;
                const uint32_t st =
                    fixedUv ? rd_fx_f32((int32_t)(128u * k), 0) : rd_fx_f32((int32_t)k, -3);
                putVtx(o, gsX(w, cx), gsX(h, cy), 0xFFFFFFFFu, st, st, grey);
            } else {
                /* a ribbon over the middle half of the target; one the
                 * placement section names: 64 GS pixels wide, 8 past the
                 * target's right edge (the fans of a sun off the picture) */
                const uint32_t col = v >> 1, side = v & 1u;
                bool beside = false;
                for (uint32_t k = 0; s->gen == RD_FX_GEN_SCREEN && k < g->sk->noff; k++) {
                    beside = beside || rd_fx_get32(g->sk->offs + 4u * k) == s->off;
                }
                const uint32_t px =
                    beside ? w + 8u + (col % 16u) * 4u : w / 4u + (col % 16u) * (w / 2u) / 16u;
                const uint32_t py = h / 4u + ((col / 16u) % 8u + side) * (h / 2u) / 8u;
                const uint32_t ss = fixedUv ? rd_fx_f32((int32_t)((col % 16u) * 64u), 0)
                                            : rd_fx_f32((int32_t)(col % 16u), -4);
                const uint32_t tt =
                    fixedUv ? rd_fx_f32((int32_t)(side * 64u), 0) : (side ? F_ONE : 0u);
                putVtx(o, gsX(w, px), gsX(h, py), 0xFFFFFFFFu, ss, tt, grey);
            }
        }
        break;
    }
    case RD_FX_GEN_SHTRIS: {
        /* a volume over the footprint: its front faces in front of
         * everything (they always pass the count's GEQUAL), its back faces
         * behind everything (they pass only where nothing was drawn): the
         * count lands 1 where the frame drew the receiving surface */
        const uint32_t j = g->shadowIndex++;
        shadowSet(q, s->a, w, h, j, 0xFFFFFFFFu, 0);
        shadowSet(q + (size_t)s->a * sizeof(RdScreenVtx), s->b, w, h, j, 0u, s->a / 3u);
        break;
    }
    case RD_FX_GEN_SHSTRIP: {
        const uint32_t j = g->shadowIndex++;
        uint32_t x0, y0, fw, fh;
        footprint(w, h, j, &x0, &y0, &fw, &fh);
        uint32_t fb;
        memcpy(&fb, &c->f[0], 4);
        const bool inc = (fb & 0x80000000u) == 0 && (fb & 0x7FFFFFFFu) != 0;
        for (uint32_t v = 0; v < s->a; v++) {
            const uint32_t col = v >> 1, side = v & 1u;
            const uint32_t px = x0 + fw * (col % 32u) / 32u, py = y0 + side * fh;
            uint8_t *o = q + 16u * v;
            rd_fx_set32(o, rd_fx_f32(gsX(w, px), 0));
            rd_fx_set32(o + 4, rd_fx_f32(gsX(h, py), 0));
            rd_fx_set32(o + 8, inc ? rd_fx_f32(0xFFFFFF, 8) : 0u);
            rd_fx_set32(o + 12, F_ONE);
        }
        break;
    }
    case RD_FX_GEN_GRIDSTRIP: {
        RdVuPayload hd;
        memcpy(&hd, g->pay + s->vuOff, sizeof(hd));
        const int e = expOf(g->sk, s->vuOff);
        putColour(q);
        for (uint32_t v = 0; v < s->b; v++) {
            uint8_t *o = q + 16u + (size_t)v * hd.qwPerVertex * 16u;
            static const int32_t origin[3] = {0, 0, 0};
            putPos(o, s->a, v, e, origin, RD_FX_FORM_CUBE);
            if (hd.qwPerVertex >= 3) {
                putNormal(o + 16, s->a);
            }
            if (hd.qwPerVertex >= 2) {
                putSt(o + 16u * (hd.qwPerVertex - 1u), s->a, v, false);
            }
        }
        break;
    }
    case RD_FX_GEN_PARTICLE: {
        const int e = expOf(g->sk, s->vuOff) - 2;
        const uint32_t i = s->a;
        rd_fx_set32(q, rd_fx_f32((int32_t)(i % 5u) - 2, e));
        rd_fx_set32(q + 4, rd_fx_f32((int32_t)(i / 5u % 5u) - 2, e));
        rd_fx_set32(q + 8, rd_fx_f32((int32_t)(i / 25u % 5u) - 2, e));
        break;
    }
    default:
        break;
    }
    return true;
}

static void putTexture(RdFxBuf *o, const SkTex *t, uint32_t ver)
{
    for (int k = 0; k < 8; k++) {
        rd_fx_put32(o, t->hdr[k]);
    }
    if (t->hdr[1] != RD_TEXKIND_IMAGE) {
        return;
    }
    const int pat = patternIndex(t->pattern);
    const uint8_t fmt = (uint8_t)(t->hdr[7] & 0xFFu);
    const uint32_t bpp = fmt == RD_TEXEL_R8 || fmt == RD_TEXEL_SHEET ? 1u : 4u;
    uint32_t w = t->hdr[4], h = t->hdr[5];
    putPattern(o, pat, w, h, bpp);
    if (ver < 9u) {
        return; /* no levels field (and no levels) before version 9 */
    }
    rd_fx_put32(o, t->levels);
    for (uint32_t l = 1; l < t->levels; l++) {
        /* each GS level is the pattern at its own size (RGBA8: the loader
           takes levels only for RGBA8 images) */
        w = w > 1 ? w / 2u : 1u;
        h = h > 1 ? h / 2u : 1u;
        putPattern(o, pat, w, h, 4);
    }
}

bool rd_fixture_expand(const RdFixture *fx, bool checkSha, uint8_t **out, size_t *outLen, char *why,
                       size_t whySize)
{
    *out = NULL;
    *outLen = 0;
    Sk sk;
    if (!skParse(fx, &sk, why, whySize)) {
        return false;
    }
    RdFxBuf o = {0};
    rd_fx_put(&o, sk.head, sk.headBytes);
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        rd_fx_put32(&o, sk.listCount[l]);
        rd_fx_put(&o, sk.listCmds[l], (size_t)sk.listCount[l] * sizeof(RdCmd));
    }
    uint8_t *pay = malloc(sk.psz ? sk.psz : 1u);
    bool ok = pay != NULL;
    if (ok && sk.psz) {
        memcpy(pay, sk.payload, sk.psz);
    }
    GenCtx g = {&sk, pay, sk.start.gsW, sk.start.gsH, 0};
    for (int l = 0; ok && l < RD_LIST_COUNT; l++) {
        for (uint32_t i = 0; ok && i < sk.listCount[l]; i++) {
            RdCmd c;
            memcpy(&c, sk.listCmds[l] + (size_t)i * sizeof(RdCmd), sizeof(c));
            if (c.type == RDC_TARGET) {
                g.w = c.u[2] & 0xFFFFu;
                g.h = c.u[2] >> 16;
            }
            /* the roles read the skeleton's payload (structure intact) */
            ok = rd_fx_payload_spans(sk.payload, sk.psz, &c, sk.ver, genSpan, &g, NULL);
        }
    }
    if (!ok) {
        setWhy(why, whySize, "a command's payload range does not hold (the skeleton is corrupt)");
    }
    rd_fx_put32(&o, sk.psz);
    rd_fx_put(&o, pay, sk.psz);
    free(pay);
    rd_fx_put32(&o, sk.nt);
    for (uint32_t i = 0; ok && i < sk.nt; i++) {
        putTexture(&o, &sk.tex[i], sk.ver);
    }
    rd_fx_put32(&o, sk.nr);
    rd_fx_put(&o, sk.temps, (size_t)sk.nr * 20u);
    rd_fx_put32(&o, sk.nm);
    for (uint32_t i = 0; ok && i < sk.nm; i++) {
        const SkMesh *m = &sk.mesh[i];
        rd_fx_put32(&o, m->id);
        rd_fx_put32(&o, m->nv);
        rd_fx_put32(&o, m->qpv);
        rd_fx_put32(&o, m->ni);
        rd_fx_put32(&o, m->nb);
        char name[24];
        memset(name, 0, sizeof(name));
        snprintf(name, sizeof(name), "m%04u", (unsigned)(i + 1u));
        rd_fx_put(&o, name, 24);
        ok = genMesh(&o, m);
        rd_fx_put(&o, m->idx, (size_t)m->ni * 4u);
        rd_fx_put(&o, m->batches, (size_t)m->nb * sizeof(RdVuBatchRec));
    }
    if (sk.ver >= 14u) {
        rd_fx_put(&o, sk.tick, 4 + 128);
    }
    if (sk.ver >= 15u) {
        rd_fx_put32(&o, sk.nf);
        for (uint32_t i = 0; i < sk.nf; i++) {
            rd_fx_put32(&o, sk.facing[i].id);
            rd_fx_put32(&o, sk.facing[i].nv);
            rd_fx_put(&o, NULL, (sk.facing[i].nv + 7u) / 8u); /* no triangle winds against */
        }
    }
    skFree(&sk);
    if (!ok || o.bad) {
        if (ok) {
            setWhy(why, whySize, "out of memory");
        }
        rd_fx_free(&o);
        return false;
    }
    if (checkSha) {
        uint8_t sha[32];
        rd_fx_sha256(o.p, o.n, sha);
        if (memcmp(sha, fx->sha, 32) != 0 || o.n != fx->expandedBytes) {
            char a[65], b[65], t[256];
            rd_fx_sha_hex(sha, a);
            rd_fx_sha_hex(fx->sha, b);
            snprintf(t, sizeof(t), "the expansion is %s (%zu bytes), the fixture pins %s (%llu)", a,
                     o.n, b, (unsigned long long)fx->expandedBytes);
            setWhy(why, whySize, t);
            rd_fx_free(&o);
            return false;
        }
    }
    *out = o.p;
    *outLen = o.n;
    return true;
}

/* ------------------------------------------------------------ the check */

typedef struct CheckCtx {
    uint8_t *named; /* per payload byte: 1 structure, 2 text */
    bool bad;
} CheckCtx;

static bool markSpan(void *user, const RdCmd *c, const RdFxSpan *s)
{
    (void)c;
    CheckCtx *k = user;
    if (s->role == RD_FX_KEEP || s->role == RD_FX_TEXT) {
        for (uint32_t i = 0; i < s->len; i++) {
            k->named[s->off + i] |= (uint8_t)(s->role == RD_FX_KEEP ? 1u : 2u);
        }
    }
    return true;
}

bool rd_fixture_check(const RdFixture *fx, char *why, size_t whySize, char *stats, size_t statsSize)
{
    Sk sk;
    if (!skParse(fx, &sk, why, whySize)) {
        return false;
    }
    CheckCtx k = {calloc(sk.psz ? sk.psz : 1u, 1), false};
    bool ok = k.named != NULL;
    uint32_t cmds = 0;
    for (int l = 0; ok && l < RD_LIST_COUNT; l++) {
        for (uint32_t i = 0; ok && i < sk.listCount[l]; i++) {
            RdCmd c;
            memcpy(&c, sk.listCmds[l] + (size_t)i * sizeof(RdCmd), sizeof(c));
            ok = rd_fx_payload_spans(sk.payload, sk.psz, &c, sk.ver, markSpan, &k, NULL);
            cmds++;
        }
    }
    uint64_t kept = 0, zero = 0, text = 0;
    for (uint32_t i = 0; ok && i < sk.psz; i++) {
        if (k.named[i] & 1u) {
            kept++;
        } else if (k.named[i] & 2u) {
            text++;
            if (sk.payload[i] != 'x' && sk.payload[i] != 0) {
                setWhy(why, whySize, "a text item holds characters other than 'x'");
                ok = false;
            }
        } else if (sk.payload[i] != 0) {
            char t[160];
            snprintf(t, sizeof(t),
                     "payload byte %u is not zero and no command names it as structure (art "
                     "left in the fixture)",
                     i);
            setWhy(why, whySize, t);
            ok = false;
        } else {
            zero++;
        }
    }
    if (!ok && why && why[0] == '\0') {
        setWhy(why, whySize, "a command's payload range does not hold");
    }
    if (stats && statsSize) {
        uint32_t images = 0;
        for (uint32_t i = 0; i < sk.nt; i++) {
            images += sk.tex[i].hdr[1] == RD_TEXKIND_IMAGE;
        }
        const int at = snprintf(
            stats, statsSize,
            "dump version %u, %u commands, payload %u bytes (%llu structure, %llu text, %llu "
            "zero), %u textures (%u images as patterns), %u meshes generated, %u sized draws",
            sk.ver, cmds, sk.psz, (unsigned long long)kept, (unsigned long long)text,
            (unsigned long long)zero, sk.nt, images, sk.nm, sk.ne);
        if (sk.scene && at > 0 && (size_t)at < statsSize) {
            snprintf(stats + at, statsSize - (size_t)at,
                     ", %u meshes placed (%u sheets), %u prims beside WORK2", sk.placed, sk.sheets,
                     sk.noff);
        } else if ((sk.placed || sk.noff) && at > 0 && (size_t)at < statsSize) {
            snprintf(stats + at, statsSize - (size_t)at,
                     ", %u meshes placed on WORK2, %u prims beside it", sk.placed, sk.noff);
        }
    }
    free(k.named);
    skFree(&sk);
    return ok;
}

/* rd_tex.c: the texture cache, and the sheet-text textures (rd.h
 * rd_create_texture_sheet).  rd_tex.h describes the decoding and the cache. */
#include "rd_tex.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"
#include "shader_consts.h"
#include "texpack.h"

/* ------------------------------------------------------------ decoding */

void rdtex_clut_to_csm1(void *clut, uint32_t colors, uint32_t entryBytes)
{
    uint8_t tmp[256 * 4];
    uint8_t *c = clut;

    if (colors != 256 || entryBytes == 0 || entryBytes > 4 || clut == NULL) {
        return;
    }
    memcpy(tmp, c, 256 * entryBytes);
    for (uint32_t i = 0; i < 256; i++) {
        memcpy(c + rdtex_csm1_index(i, 256) * entryBytes, tmp + i * entryBytes, entryBytes);
    }
}

size_t rdtex_image_bytes(uint32_t psm, uint32_t w, uint32_t h)
{
    size_t n = (size_t)w * h;

    switch (psm) {
    case RDTEX_PSMCT32:
    case RDTEX_PSMZ32:
    case RDTEX_PSMT8H:
    case RDTEX_PSMT4HL:
    case RDTEX_PSMT4HH:
        return n * 4;
    case RDTEX_PSMCT24:
    case RDTEX_PSMZ24:
        return n * 3;
    case RDTEX_PSMCT16:
    case RDTEX_PSMCT16S:
    case RDTEX_PSMZ16:
    case RDTEX_PSMZ16S:
        return n * 2;
    case RDTEX_PSMT8:
        return n;
    case RDTEX_PSMT4:
        return (n + 1) / 2;
    default:
        return 0;
    }
}

/* a 16-bit texel: 5:5:5 shifted up by 3 (the GS does not replicate the
   high bits), the A bit as 0/1 */
static void put16(uint8_t *o, const uint8_t *p)
{
    uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8);

    o[0] = (uint8_t)((v & 0x1F) << 3);
    o[1] = (uint8_t)(((v >> 5) & 0x1F) << 3);
    o[2] = (uint8_t)(((v >> 10) & 0x1F) << 3);
    o[3] = (uint8_t)(v >> 15);
}

static void put24(uint8_t *o, const uint8_t *p)
{
    o[0] = p[0];
    o[1] = p[1];
    o[2] = p[2];
    o[3] = 0;
}

/* CLUT entry for index i */
static void putClut(uint8_t *o, const RdTexImage *im, uint32_t i)
{
    uint32_t pos = im->clutLinear ? i : rdtex_csm1_index(i, im->clutColors);
    const uint8_t *c = im->clut;

    switch (im->cpsm) {
    case RDTEX_PSMCT16:
    case RDTEX_PSMCT16S:
        put16(o, c + pos * 2);
        break;
    case RDTEX_PSMCT24:
        put24(o, c + pos * 3);
        break;
    default:
        memcpy(o, c + pos * 4, 4);
        break;
    }
}

static int clutSrc(uint32_t cpsm, RdTexSrc *src)
{
    switch (cpsm) {
    case RDTEX_PSMCT32:
        *src = RD_TEXSRC_RGBA32;
        return 0;
    case RDTEX_PSMCT24:
        *src = RD_TEXSRC_RGB24;
        return 0;
    case RDTEX_PSMCT16:
    case RDTEX_PSMCT16S:
        *src = RD_TEXSRC_RGBA16;
        return 0;
    default:
        return -1;
    }
}

int rdtex_decode(const RdTexImage *im, uint8_t *out, RdTexSrc *src)
{
    const uint8_t *p = im->pixels;
    uint32_t pw = im->padW ? im->padW : im->w;
    uint32_t ph = im->padH ? im->padH : im->h;
    int indexed = 0;

    if (p == NULL || im->w == 0 || im->h == 0 || pw < im->w || ph < im->h) {
        return -1;
    }
    switch (im->psm) {
    case RDTEX_PSMCT32:
    case RDTEX_PSMZ32:
        *src = RD_TEXSRC_RGBA32;
        break;
    case RDTEX_PSMCT24:
    case RDTEX_PSMZ24:
        *src = RD_TEXSRC_RGB24;
        break;
    case RDTEX_PSMCT16:
    case RDTEX_PSMCT16S:
    case RDTEX_PSMZ16:
    case RDTEX_PSMZ16S:
        *src = RD_TEXSRC_RGBA16;
        break;
    case RDTEX_PSMT8:
    case RDTEX_PSMT4:
    case RDTEX_PSMT8H:
    case RDTEX_PSMT4HL:
    case RDTEX_PSMT4HH:
        if (im->clut == NULL || clutSrc(im->cpsm, src) != 0) {
            return -1;
        }
        indexed = 1;
        break;
    default:
        return -1;
    }
    memset(out, 0, (size_t)pw * ph * 4);
    for (uint32_t y = 0; y < im->h; y++) {
        uint8_t *o = out + (size_t)y * pw * 4;
        size_t row = (size_t)y * im->w;

        for (uint32_t x = 0; x < im->w; x++, o += 4) {
            size_t n = row + x;
            uint32_t idx;

            if (!indexed) {
                switch (*src) {
                case RD_TEXSRC_RGBA32:
                    memcpy(o, p + n * 4, 4);
                    break;
                case RD_TEXSRC_RGB24:
                    put24(o, p + n * 3);
                    break;
                default:
                    put16(o, p + n * 2);
                    break;
                }
                continue;
            }
            switch (im->psm) {
            case RDTEX_PSMT8:
                idx = p[n];
                break;
            case RDTEX_PSMT4:
                idx = (p[n >> 1] >> ((n & 1) * 4)) & 0xF;
                break;
            case RDTEX_PSMT8H:
                idx = p[n * 4 + 3];
                break;
            case RDTEX_PSMT4HL:
                idx = p[n * 4 + 3] & 0xF;
                break;
            default: /* PSMT4HH */
                idx = p[n * 4 + 3] >> 4;
                break;
            }
            if (idx >= im->clutColors) {
                idx &= im->clutColors ? im->clutColors - 1 : 0;
            }
            putClut(o, im, idx);
        }
    }
    return 0;
}

void rdtex_apply_texa(uint8_t *rgba, size_t n, RdTexSrc src, RdTexA mode)
{
    uint8_t ta0 = 0x80, ta1 = 0x80;
    int aem = 0;

    if (src == RD_TEXSRC_RGBA32) {
        return;
    }
    if (mode == RD_TEXA_7F_81_AEM) {
        ta0 = 0x7F;
        ta1 = 0x81;
        aem = 1;
    } else if (mode == RD_TEXA_80_80_AEM) {
        aem = 1;
    }
    for (size_t i = 0; i < n; i++, rgba += 4) {
        uint8_t a = (src == RD_TEXSRC_RGBA16 && rgba[3] != 0) ? ta1 : ta0;

        if (aem && (rgba[0] | rgba[1] | rgba[2]) == 0) {
            a = 0;
        }
        rgba[3] = a;
    }
}

size_t rdtex_mip_chain_bytes(uint32_t w, uint32_t h)
{
    size_t bytes = 0;

    while (w > 1 || h > 1) {
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
        bytes += (size_t)w * h * 4;
    }
    return bytes;
}

uint32_t rdtex_build_mip_chain(const uint8_t *rgba, uint32_t w, uint32_t h, uint8_t *out,
                               int alphaWeighted)
{
    const uint8_t *s = rgba;
    uint32_t levels = 0;

    while (w > 1 || h > 1) {
        uint32_t nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;

        for (uint32_t y = 0; y < nh; y++) {
            for (uint32_t x = 0; x < nw; x++) {
                uint32_t x0 = x * 2, y0 = y * 2;
                uint32_t x1 = w > 1 ? x0 + 1 : x0, y1 = h > 1 ? y0 + 1 : y0;

                const uint8_t *q[4] = {s + (y0 * w + x0) * 4, s + (y0 * w + x1) * 4,
                                       s + (y1 * w + x0) * 4, s + (y1 * w + x1) * 4};
                const uint32_t asum = (uint32_t)q[0][3] + q[1][3] + q[2][3] + q[3][3];
                uint8_t *o = out + (y * nw + x) * 4;

                for (int c = 0; c < 3; c++) {
                    if (alphaWeighted && asum > 0) {
                        /* the premultiplied average, divided
                           back by the alpha: a texel that is not there
                           (alpha 0, often black) gives no colour, so a
                           lattice's wires keep theirs in the distance */
                        uint32_t sum = 0;
                        for (int k = 0; k < 4; k++) {
                            sum += (uint32_t)q[k][c] * q[k][3];
                        }
                        uint32_t v = (sum + asum / 2) / asum;
                        o[c] = (uint8_t)(v > 255 ? 255 : v);
                    } else {
                        uint32_t sum = (uint32_t)q[0][c] + q[1][c] + q[2][c] + q[3][c];
                        o[c] = (uint8_t)((sum + 2) / 4);
                    }
                }
                o[3] = (uint8_t)((asum + 2) / 4);
            }
        }
        s = out;
        out += (size_t)nw * nh * 4;
        w = nw;
        h = nh;
        levels++;
    }
    return levels;
}

/* The share of texels whose alpha passes "a > ref". */
static double coverage(const uint8_t *px, size_t n, uint8_t ref, double scale)
{
    size_t pass = 0;

    for (size_t i = 0; i < n; i++) {
        if ((double)px[i * 4 + 3] * scale > (double)ref) {
            pass++;
        }
    }
    return n ? (double)pass / (double)n : 0.0;
}

void rdtex_keep_alpha_coverage(const uint8_t *base, uint32_t w, uint32_t h, uint8_t *chain,
                               uint32_t levels, uint8_t ref)
{
    const double c0 = coverage(base, (size_t)w * h, ref, 1.0);
    uint8_t amax = 0;

    if (c0 <= 0.0 || c0 >= 1.0) {
        return; /* nothing alpha-tested away, or nothing kept */
    }
    for (size_t i = 0; i < (size_t)w * h; i++) {
        amax = base[i * 4 + 3] > amax ? base[i * 4 + 3] : amax;
    }
    for (uint32_t l = 0; l < levels; l++) {
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
        const size_t n = (size_t)w * h;
        if (coverage(chain, n, ref, 1.0) < c0) {
            /* the smallest alpha scale in [1, 4] that keeps the coverage */
            double lo = 1.0, hi = 4.0;
            for (int it = 0; it < 12; it++) {
                const double mid = (lo + hi) * 0.5;
                if (coverage(chain, n, ref, mid) >= c0) {
                    hi = mid;
                } else {
                    lo = mid;
                }
            }
            for (size_t i = 0; i < n; i++) {
                double a = ceil((double)chain[i * 4 + 3] * hi);
                chain[i * 4 + 3] = (uint8_t)(a > (double)amax ? amax : a);
            }
        }
        chain += n * 4;
    }
}

/* --------------------------------------------------------------- cache */

#define RDTEX_MAX_ENTRIES 1024
#define RDTEX_MAX_RETIRED 1024

typedef struct RdTexEntry {
    uint32_t id, gen;
    uint16_t texa;
    uint8_t used, src;
    uint32_t w, h; /* the rd texture's (padded) size */
    RdTex tex;
    uint8_t replaced; /* texture packs: tex is a pack replacement (rdtex_replace) */
    RdTexSampler smp;
} RdTexEntry;

static struct {
    RdTexEntry e[RDTEX_MAX_ENTRIES];

    struct {
        RdTex tex;
        uint32_t tick;
    } retired[RDTEX_MAX_RETIRED];

    uint32_t nRetired;
    uint32_t tick;
    RdTexCacheStats stats;
    RdTexReleaseFn release; /* texture packs: rdtex_set_release_hook */
    int bcSingleLogged;     /* "a compressed replacement without mips", once */
} s_tc;

static RdTexEntry *entryOf(uint32_t id, int texa)
{
    for (uint32_t i = 0; i < RDTEX_MAX_ENTRIES; i++) {
        RdTexEntry *e = &s_tc.e[i];

        if (e->used && e->id == id && e->texa == (uint16_t)texa) {
            return e;
        }
    }
    return NULL;
}

static void retire(RdTex t)
{
    if (t.id == 0) {
        return;
    }
    if (s_tc.nRetired == RDTEX_MAX_RETIRED) {
        /* the queue is full: the oldest goes now */
        rd_destroy_texture(s_tc.retired[0].tex);
        memmove(&s_tc.retired[0], &s_tc.retired[1],
                (RDTEX_MAX_RETIRED - 1) * sizeof(s_tc.retired[0]));
        s_tc.nRetired--;
    }
    s_tc.retired[s_tc.nRetired].tex = t;
    s_tc.retired[s_tc.nRetired].tick = s_tc.tick;
    s_tc.nRetired++;
    s_tc.stats.retired++;
}

/* Texture packs: the entry gives up its texture (retired, or forgotten
 * by rdtex_reset); a replacement's accounts are settled. */
static void releaseReplaced(RdTexEntry *e)
{
    if (e->replaced) {
        e->replaced = 0;
        if (s_tc.release && e->tex.id) {
            s_tc.release(e->tex);
        }
    }
}

static void freeEntry(RdTexEntry *e)
{
    releaseReplaced(e);
    retire(e->tex);
    memset(e, 0, sizeof(*e));
    s_tc.stats.entries--;
}

RdTex rdtex_find(uint32_t id, uint32_t gen, int texa)
{
    RdTexEntry *e = entryOf(id, texa);
    const RdTexRec *r = e && e->gen == gen ? rd__tex_rec(e->tex.id) : NULL;

    if (r != NULL && e->replaced && r->refused) {
        /* the graphics card refused the replacement: the entry is
           forgotten (the release hook hears of it, so the pack does not
           offer that file again) and the caller decodes the game's own */
        freeEntry(e);
        r = NULL;
    }
    if (r != NULL) {
        s_tc.stats.hits++;
        return e->tex;
    }
    s_tc.stats.misses++;
    return (RdTex){0};
}

RdTex rdtex_store(uint32_t id, uint32_t gen, int texa, const RdTexImage *im,
                  const RdTexSampler *smp, const char *debugName)
{
    uint32_t pw = im->padW ? im->padW : im->w;
    uint32_t ph = im->padH ? im->padH : im->h;
    RdTexEntry *e = entryOf(id, texa);
    uint8_t *px;
    RdTexSrc src;

    if (pw == 0 || ph == 0 || pw > 4096 || ph > 4096) {
        s_tc.stats.failures++;
        return (RdTex){0};
    }
    px = malloc((size_t)pw * ph * 4);
    if (px == NULL || rdtex_decode(im, px, &src) != 0) {
        free(px);
        s_tc.stats.failures++;
        return (RdTex){0};
    }
    if (texa != RDTEX_TEXA_REPLAY) {
        rdtex_apply_texa(px, (size_t)pw * ph, src, (RdTexA)texa);
        src = RD_TEXSRC_RGBA32;
    }
    s_tc.stats.decodes++;
    if (e == NULL) {
        for (uint32_t i = 0; i < RDTEX_MAX_ENTRIES; i++) {
            if (!s_tc.e[i].used) {
                e = &s_tc.e[i];
                break;
            }
        }
        if (e == NULL) {
            rd__log("the texture cache is full (%d entries)", RDTEX_MAX_ENTRIES);
            free(px);
            s_tc.stats.failures++;
            return (RdTex){0};
        }
        memset(e, 0, sizeof(*e));
        e->used = 1;
        e->id = id;
        e->texa = (uint16_t)texa;
        s_tc.stats.entries++;
    }
    if (e->tex.id != 0 && !e->replaced && rd__tex_rec(e->tex.id) != NULL && e->w == pw &&
        e->h == ph && e->src == (uint8_t)src) {
        /* same shape: re-expand in place */
        rd_update_texture(e->tex, px);
        s_tc.stats.updates++;
    } else {
        RdTex t;

        if (texa != RDTEX_TEXA_REPLAY) {
            t = rd_create_texture(pw, ph, px, (RdTexA)texa, debugName);
        } else {
            t = rd_create_texture_src(pw, ph, px, src, debugName);
        }
        if (t.id == 0) {
            free(px);
            freeEntry(e);
            s_tc.stats.failures++;
            return (RdTex){0};
        }
        /* a replaced entry's new generation gets a texture of the game's
           own: the replacement was for the old one */
        releaseReplaced(e);
        if (rd__tex_rec(e->tex.id) != NULL) {
            retire(e->tex);
        }
        e->tex = t;
        e->w = pw;
        e->h = ph;
        e->src = (uint8_t)src;
        s_tc.stats.creates++;
    }
    e->gen = gen;
    if (smp) {
        e->smp = *smp;
    }
    free(px);
    /* stored as one level: GS levels an earlier store gave the same
       texture go (rdtex_store_levels adds them back after this) */
    rd__tex_set_levels(e->tex, NULL, 0);
    return e->tex;
}

/* Authored levels: a TIM2's further levels are drawn at the GS's level only
 * when they are pictures of their own, not reductions of the base.  The
 * test is the brightness: each level's mean (R + G + B) / 3 over its texels
 * against the mean of the level above over the texels it reduces from (the
 * mean of its 2x2 box reduction), as (b + 1) / (a + 1) either way up; past
 * RDTEX_AUTHORED_RATIO on any level the levels are authored.  On the disc's
 * 294 mipmapped TIM2 pictures (841 files) the ratio splits in two: the fog
 * puffs (fog1, st25a_fog1: 12.1), the light shafts (fractal and its copies:
 * 9.0, then 2.0) and the waterfall and room mists (taki_fog, fog: 2.2), and
 * everything else at 1.52 or below (st13a_renga_wall 1.52, penki_rain 1.47,
 * the walls about 1.3), most under 1.1.  A per-texel difference does not
 * split them: the walls' own reductions are sharpened and differ from a
 * box by up to 24 per texel against fog1's 14.  1.8 sits in the gap. */
#define RDTEX_AUTHORED_RATIO 1.8

/* The mean (R + G + B) / 3 of the w x h texels at px, rows of pitch texels */
static double meanRgb(const uint8_t *px, uint32_t pitch, uint32_t w, uint32_t h)
{
    uint64_t sum = 0;
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *row = px + (size_t)y * pitch * 4;
        for (uint32_t x = 0; x < w; x++) {
            sum += (uint64_t)row[x * 4] + row[x * 4 + 1] + row[x * 4 + 2];
        }
    }
    return w && h ? (double)sum / (3.0 * (double)w * (double)h) : 0.0;
}

/* Whether the levels 1..levels-1 in chain (each padded as rdtex_store_levels
 * lays them out under the base's padded w x h at base) are authored; *worst
 * gets the largest ratio.  ims: their sizes in the TIM2. */
static int levelsAuthored(const uint8_t *base, uint32_t w, uint32_t h, const uint8_t *chain,
                          uint32_t levels, const RdTexImage *ims, double *worst)
{
    const uint8_t *above = base;
    uint32_t aw = w, ah = h;
    *worst = 1.0;
    for (uint32_t l = 1; l < levels; l++) {
        const uint32_t lw = aw > 1 ? aw / 2 : 1, lh = ah > 1 ? ah / 2 : 1;
        const uint32_t iw = ims[l].w, ih = ims[l].h;
        const uint32_t rw = 2 * iw < ims[l - 1].w ? 2 * iw : ims[l - 1].w;
        const uint32_t rh = 2 * ih < ims[l - 1].h ? 2 * ih : ims[l - 1].h;
        const double a = meanRgb(above, aw, rw, rh), b = meanRgb(chain, lw, iw, ih);
        double r = (b + 1.0) / (a + 1.0);
        r = r < 1.0 ? 1.0 / r : r;
        *worst = r > *worst ? r : *worst;
        above = chain;
        chain += (size_t)lw * lh * 4;
        aw = lw;
        ah = lh;
    }
    return *worst > RDTEX_AUTHORED_RATIO;
}

/* Each texture's decision named once in the log */
static void logAuthored(const char *name, int authored, double worst, uint32_t levels)
{
    static uint32_t seen[256];
    static uint32_t nSeen;
    uint32_t hash = 2166136261u;
    for (const char *c = name ? name : "?"; *c; c++) {
        hash = (hash ^ (uint8_t)*c) * 16777619u;
    }
    for (uint32_t i = 0; i < nSeen; i++) {
        if (seen[i] == hash) {
            return;
        }
    }
    if (nSeen < 256) {
        seen[nSeen++] = hash;
    }
    rd__log("textures: \"%s\" %u mipmap levels, brightness ratio %.2f: %s", name ? name : "?",
            levels, worst,
            authored ? "authored, drawn at the GS's level"
                     : "reductions of the base, drawn as before");
}

RdTex rdtex_store_levels(uint32_t id, uint32_t gen, int texa, const RdTexImage *ims, uint32_t n,
                         const RdTexSampler *smp, const char *debugName)
{
    if (!ims || n == 0) {
        return (RdTex){0};
    }
    RdTex t = rdtex_store(id, gen, texa, &ims[0], smp, debugName);
    const RdTexRec *r = rd__tex_rec(t.id);
    if (!r || n < 2 || r->format != RD_TEXEL_RGBA8 || r->replacement || !r->pixels) {
        rd__tex_set_levels(t, NULL, 0);
        return t;
    }
    /* each level padded as the base is: the GS addresses level l at
       2^(TW - l) x 2^(TH - l), the base's padded size halved */
    const size_t bytes = rd__gs_chain_bytes(r->w, r->h, n);
    uint8_t *chain = malloc(bytes ? bytes : 1);
    if (!chain) {
        rd__tex_set_levels(t, NULL, 0);
        return t;
    }
    uint32_t w = r->w, h = r->h, levels = 1;
    uint8_t *at = chain;
    for (uint32_t l = 1; l < n; l++) {
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
        RdTexImage im = ims[l];
        RdTexSrc src;
        im.padW = w;
        im.padH = h;
        if (im.w > w || im.h > h || im.w == 0 || im.h == 0 || rdtex_decode(&im, at, &src) != 0) {
            break;
        }
        if (texa != RDTEX_TEXA_REPLAY) {
            rdtex_apply_texa(at, (size_t)w * h, src, (RdTexA)texa);
        }
        at += (size_t)w * h * 4;
        levels++;
    }
    /* only authored levels take the GS's level; reductions of the base keep
       the texture as rdtex_store left it (one level; the Enhanced filter's
       box mips) */
    double worst = 1.0;
    const int authored =
        levels > 1 && levelsAuthored(r->pixels, r->w, r->h, chain, levels, ims, &worst);
    if (levels > 1) {
        logAuthored(debugName, authored, worst, levels);
    }
    rd__tex_set_levels(t, authored ? chain : NULL, authored ? levels : 0);
    free(chain);
    return t;
}

const RdTexSampler *rdtex_sampler(uint32_t id, int texa)
{
    RdTexEntry *e = entryOf(id, texa);

    return e ? &e->smp : NULL;
}

void rdtex_drop(uint32_t id)
{
    for (uint32_t i = 0; i < RDTEX_MAX_ENTRIES; i++) {
        if (s_tc.e[i].used && s_tc.e[i].id == id) {
            freeEntry(&s_tc.e[i]);
        }
    }
}

void rdtex_frame_tick(void)
{
    uint32_t keep = 0;

    s_tc.tick++;
    for (uint32_t i = 0; i < s_tc.nRetired; i++) {
        if (s_tc.tick - s_tc.retired[i].tick >= 2) {
            rd_destroy_texture(s_tc.retired[i].tex);
        } else {
            s_tc.retired[keep++] = s_tc.retired[i];
        }
    }
    s_tc.nRetired = keep;
}

void rdtex_reset(void)
{
    for (uint32_t i = 0; i < RDTEX_MAX_ENTRIES; i++) {
        if (s_tc.e[i].used) {
            releaseReplaced(&s_tc.e[i]);
        }
    }
    memset(s_tc.e, 0, sizeof(s_tc.e));
    s_tc.nRetired = 0;
    memset(&s_tc.stats, 0, sizeof(s_tc.stats));
}

/* ------------------------------------------------------- texture packs */

int rdtex_replacement_mips_from(const TexpackImage *src, TexpackImage *dst)
{
    if (!src || !dst || !src->blob || src->fmt != RD_TEXEL_RGBA8 || src->levels != 1 ||
        src->w == 0 || src->h == 0 || src->lv[0].data == NULL || src->lv[0].pitch != src->w * 4u) {
        return -1;
    }
    const uint32_t w = src->w, h = src->h;
    const size_t base = (size_t)w * h * 4;
    const size_t chain = rdtex_mip_chain_bytes(w, h);
    if (chain == 0) {
        return -1; /* 1 x 1: nothing to add */
    }
    uint8_t *blob = malloc(base + chain);
    if (!blob) {
        return -1;
    }
    memcpy(blob, src->lv[0].data, base);
    /* the pack's alpha is raw GS alpha (RD_TEXSRC_RGBA32): colour weighted
       by it, as the game's own Enhanced mips.  No alpha coverage is kept:
       PCSX2 keeps none for a pack's mips and the authors tune their alpha
       for that */
    const uint32_t n = rdtex_build_mip_chain(blob, w, h, blob + base, 1);
    TexpackImage *img = dst;
    *img = *src;
    img->blob = blob;
    img->bytes = base + chain;
    img->lv[0].data = blob;
    const uint8_t *p = blob + base;
    uint32_t lw = w, lh = h, levels = 1;
    for (uint32_t l = 1; l <= n && levels < TEXPACK_IMAGE_LEVELS; l++) {
        lw = lw > 1 ? lw / 2 : 1;
        lh = lh > 1 ? lh / 2 : 1;
        TexpackImageLevel *lv = &img->lv[levels++];
        lv->data = p;
        lv->w = lw;
        lv->h = lh;
        lv->pitch = lw * 4;
        lv->size = (size_t)lw * lh * 4;
        p += lv->size;
    }
    img->levels = levels;
    return 0;
}

int rdtex_replacement_mips(TexpackImage *img)
{
    TexpackImage out;

    if (rdtex_replacement_mips_from(img, &out) != 0) {
        return -1;
    }
    free(img->blob);
    *img = out;
    return 0;
}

int rdtex_replacement_refused(RdTex t)
{
    const RdTexRec *r = rd__tex_rec(t.id);

    return r != NULL && r->replacement && r->refused;
}

RdTex rdtex_create_replacement(TexpackImage *img, uint32_t uvW, uint32_t uvH, const char *debugName)
{
    if (!g_rd.inited || !img || !img->blob || img->levels == 0 || img->w == 0 || img->h == 0) {
        return (RdTex){0};
    }
    const int block = rd__texel_is_block(img->fmt);
    if (!block && img->fmt != RD_TEXEL_RGBA8) {
        return (RdTex){0};
    }
    if (block && (!g_rd.hasDevice || !rhi_limits()->bcTextures)) {
        return (RdTex){0};
    }
    if (g_rd.hasDevice &&
        (img->w > rhi_limits()->maxTextureSize || img->h > rhi_limits()->maxTextureSize)) {
        rd__log("textures: \"%s\" is %ux%u, larger than this graphics card takes (%u)",
                debugName ? debugName : "?", img->w, img->h, rhi_limits()->maxTextureSize);
        return (RdTex){0};
    }
    /* the levels the device gets: never more than the full chain */
    uint32_t full = 1;
    for (uint32_t m = img->w > img->h ? img->w : img->h; m > 1; m >>= 1) {
        full++;
    }
    if (img->levels > full) {
        img->levels = full;
    }
    if (img->levels == 1 && (img->w > 1 || img->h > 1)) {
        if (!block) {
            /* minified replacements need levels to sample from (the pack
               is drawn mipmapped); a failure leaves one level */
            rdtex_replacement_mips(img);
        } else if (!s_tc.bcSingleLogged) {
            s_tc.bcSingleLogged = 1;
            rd__log("textures: compressed textures without mipmaps are drawn without them "
                    "(\"%s\" is the first)",
                    debugName ? debugName : "?");
        }
    }
    return rd__create_texture_replacement(img, uvW, uvH, debugName);
}

int rdtex_replace(uint32_t id, uint32_t gen, int texa, RdTex rep)
{
    RdTexEntry *e = entryOf(id, texa);

    if (e == NULL || e->gen != gen || rep.id == 0 || rd__tex_rec(rep.id) == NULL) {
        return -1;
    }
    if (e->tex.id != rep.id) {
        releaseReplaced(e); /* a replacement replaced again */
        if (rd__tex_rec(e->tex.id) != NULL) {
            retire(e->tex);
        }
    }
    e->tex = rep;
    e->replaced = 1;
    s_tc.stats.replaced++;
    return 0;
}

void rdtex_revert_replacements(void)
{
    for (uint32_t i = 0; i < RDTEX_MAX_ENTRIES; i++) {
        if (s_tc.e[i].used && s_tc.e[i].replaced) {
            /* forgotten: the next bind misses (rdtex_find) and decodes the
               game's texture again */
            freeEntry(&s_tc.e[i]);
        }
    }
}

void rdtex_set_release_hook(RdTexReleaseFn fn)
{
    s_tc.release = fn;
}

const RdTexCacheStats *rdtex_stats(void)
{
    return &s_tc.stats;
}

/* --------------------------------------------------------------- sheet text
 * rd.h rd_create_texture_sheet: an R8 image of coverage with the style the
 * replay hands font_sheet_ps (rd_replay.c fillDrawCB, rd__overlay_draw). */

static void sheetStyleSet(RdTexRec *t, const RdSheetStyle *style)
{
    static const RdSheetStyle kDefault = {1, 0, 0xFF, 1, 0, 1};
    const RdSheetStyle *s = style ? style : &kDefault;
    t->sheet[0] = s->rimOn ? (s->rimWeight && s->rimWeight < 64 ? s->rimWeight : 64) : 0;
    t->sheet[1] = s->rimLevel;
    t->sheet[2] = s->fillLevel;
    t->sheet[3] = s->dither ? 1 : 0;
    t->sheetScale = s->scale > ICO_SHEET_SCALE_MAX ? ICO_SHEET_SCALE_MAX : s->scale ? s->scale : 1;
}

RdTex rd_create_texture_sheet(uint32_t w, uint32_t h, const uint8_t *coverage,
                              const RdSheetStyle *style, const char *name)
{
    RdTex t = rd__create_texture_fmt(w, h, coverage, RD_TEXEL_SHEET, RD_TEXSRC_RGBA32,
                                     name ? name : "sheet");
    RdTexRec *r = rd__tex_rec(t.id);
    if (r) {
        sheetStyleSet(r, style);
    }
    return t;
}

/* The rim at sheet resolution, as the 1x shader makes it, over the sheet
   texels that the rectangle touches */
void rd_sheet_rim(const uint8_t *cov, uint32_t w, uint32_t h, uint32_t scale, int32_t x, int32_t y,
                  int32_t rw, int32_t rh, uint8_t *rim)
{
    static const uint32_t kWx[ICO_SHEET_RX + 1] = {ICO_SHEET_WX_0, ICO_SHEET_WX_1, ICO_SHEET_WX_2,
                                                   ICO_SHEET_WX_3, ICO_SHEET_WX_4, ICO_SHEET_WX_5,
                                                   ICO_SHEET_WX_6};
    static const uint32_t kWy[ICO_SHEET_RY + 1] = {ICO_SHEET_WY_0, ICO_SHEET_WY_1, ICO_SHEET_WY_2,
                                                   ICO_SHEET_WY_3, ICO_SHEET_WY_4};
    const int32_t s = scale < 1                     ? 1
                      : scale > ICO_SHEET_SCALE_MAX ? ICO_SHEET_SCALE_MAX
                                                    : (int)scale;
    const int32_t x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    const int32_t x1 = x + rw > (int32_t)w ? (int32_t)w : x + rw;
    const int32_t y1 = y + rh > (int32_t)h ? (int32_t)h : y + rh;
    if (!cov || !rim || x0 >= x1 || y0 >= y1) {
        return;
    }
    /* the sheet texels: those the rectangle touches (qx0..qx1 - 1 across,
       qy0..qy1 - 1 down), and their coverage's reach round them, clipped to
       the sheet (sw x sh) */
    const int32_t sw = ((int32_t)w + s - 1) / s, sh = ((int32_t)h + s - 1) / s;
    const int32_t qx0 = x0 / s, qx1 = (x1 + s - 1) / s, qy0 = y0 / s, qy1 = (y1 + s - 1) / s;
    const int32_t cx0 = qx0 - ICO_SHEET_RX < 0 ? 0 : qx0 - ICO_SHEET_RX;
    const int32_t cx1 = qx1 + ICO_SHEET_RX > sw ? sw : qx1 + ICO_SHEET_RX;
    const int32_t cy0 = qy0 - ICO_SHEET_RY < 0 ? 0 : qy0 - ICO_SHEET_RY;
    const int32_t cy1 = qy1 + ICO_SHEET_RY > sh ? sh : qy1 + ICO_SHEET_RY;
    const int32_t cw = cx1 - cx0, ch = cy1 - cy0, ow = qx1 - qx0;
    uint32_t *c = malloc(sizeof(uint32_t) * (size_t)cw * (size_t)ch);
    uint32_t *hm = malloc(sizeof(uint32_t) * (size_t)ow * (size_t)ch);
    if (!c || !hm) {
        free(c);
        free(hm);
        return;
    }
    /* each sheet texel's coverage: the mean of its s x s texels (0 outside
       the coverage), rounded */
    const uint32_t n = (uint32_t)(s * s);
    for (int32_t qy = cy0; qy < cy1; qy++) {
        for (int32_t qx = cx0; qx < cx1; qx++) {
            uint32_t sum = 0;
            for (int32_t yy = qy * s; yy < qy * s + s && yy < (int32_t)h; yy++) {
                for (int32_t xx = qx * s; xx < qx * s + s && xx < (int32_t)w; xx++) {
                    sum += cov[(size_t)yy * w + (size_t)xx];
                }
            }
            c[(size_t)(qy - cy0) * (size_t)cw + (size_t)(qx - cx0)] = (sum + n / 2u) / n;
        }
    }
    /* the 1x dilation, separable (a max commutes with a non-negative
       factor): each row's largest c * wx across, then down with wy */
    for (int32_t qy = cy0; qy < cy1; qy++) {
        for (int32_t qx = qx0; qx < qx1; qx++) {
            uint32_t m = 0;
            for (int32_t k = qx - ICO_SHEET_RX; k <= qx + ICO_SHEET_RX; k++) {
                if (k >= cx0 && k < cx1) {
                    const uint32_t v = c[(size_t)(qy - cy0) * (size_t)cw + (size_t)(k - cx0)] *
                                       kWx[k < qx ? qx - k : k - qx];
                    m = v > m ? v : m;
                }
            }
            hm[(size_t)(qy - cy0) * (size_t)ow + (size_t)(qx - qx0)] = m;
        }
    }
    for (int32_t qy = qy0; qy < qy1; qy++) {
        for (int32_t qx = qx0; qx < qx1; qx++) {
            uint32_t m = 0;
            for (int32_t k = qy - ICO_SHEET_RY; k <= qy + ICO_SHEET_RY; k++) {
                if (k >= cy0 && k < cy1) {
                    const uint32_t v = hm[(size_t)(k - cy0) * (size_t)ow + (size_t)(qx - qx0)] *
                                       kWy[k < qy ? qy - k : k - qy];
                    m = v > m ? v : m;
                }
            }
            /* the value over the sheet texel's texels */
            const uint8_t r = (uint8_t)((m + 500000u) / 1000000u);
            for (int32_t yy = qy * s; yy < qy * s + s && yy < (int32_t)h; yy++) {
                for (int32_t xx = qx * s; xx < qx * s + s && xx < (int32_t)w; xx++) {
                    rim[(size_t)yy * w + (size_t)xx] = r;
                }
            }
        }
    }
    free(c);
    free(hm);
}

void rd_set_texture_sheet_style(RdTex t, const RdSheetStyle *style)
{
    RdTexRec *r = rd__tex_rec(t.id);
    if (r && r->kind == RD_TEXKIND_IMAGE && r->format == RD_TEXEL_SHEET) {
        sheetStyleSet(r, style);
    }
}

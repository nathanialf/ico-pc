/* rd_tex.c: the texture cache (renderer wave 2, package R2b).  rd_tex.h
 * describes the decoding and the cache. */
#include "rd_tex.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"
#include "texpack.h"

/* ------------------------------------------------------------ decoding */

void rdtex_ClutToCsm1(void *clut, uint32_t colors, uint32_t entryBytes)
{
    uint8_t tmp[256 * 4];
    uint8_t *c = clut;

    if (colors != 256 || entryBytes == 0 || entryBytes > 4 || clut == NULL) {
        return;
    }
    memcpy(tmp, c, 256 * entryBytes);
    for (uint32_t i = 0; i < 256; i++) {
        memcpy(c + rdtex_Csm1Index(i, 256) * entryBytes, tmp + i * entryBytes, entryBytes);
    }
}

size_t rdtex_ImageBytes(uint32_t psm, uint32_t w, uint32_t h)
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
    uint32_t pos = im->clutLinear ? i : rdtex_Csm1Index(i, im->clutColors);
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

int rdtex_Decode(const RdTexImage *im, uint8_t *out, RdTexSrc *src)
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

void rdtex_ApplyTexa(uint8_t *rgba, size_t n, RdTexSrc src, RdTexA mode)
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

size_t rdtex_MipChainBytes(uint32_t w, uint32_t h)
{
    size_t bytes = 0;

    while (w > 1 || h > 1) {
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
        bytes += (size_t)w * h * 4;
    }
    return bytes;
}

uint32_t rdtex_BuildMipChain(const uint8_t *rgba, uint32_t w, uint32_t h, uint8_t *out)
{
    const uint8_t *s = rgba;
    uint32_t levels = 0;

    while (w > 1 || h > 1) {
        uint32_t nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;

        for (uint32_t y = 0; y < nh; y++) {
            for (uint32_t x = 0; x < nw; x++) {
                uint32_t x0 = x * 2, y0 = y * 2;
                uint32_t x1 = w > 1 ? x0 + 1 : x0, y1 = h > 1 ? y0 + 1 : y0;

                for (int c = 0; c < 4; c++) {
                    uint32_t sum = s[(y0 * w + x0) * 4 + c] + s[(y0 * w + x1) * 4 + c] +
                                   s[(y1 * w + x0) * 4 + c] + s[(y1 * w + x1) * 4 + c];
                    out[(y * nw + x) * 4 + c] = (uint8_t)((sum + 2) / 4);
                }
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

/* Wave 7 (R7a): the share of texels whose alpha passes "a > ref". */
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

void rdtex_KeepAlphaCoverage(const uint8_t *base, uint32_t w, uint32_t h, uint8_t *chain,
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
    uint8_t replaced; /* texture packs: tex is a pack replacement (rdtex_Replace) */
    RdTexSampler smp;
    uint8_t *mips; /* the Enhanced CPU chain, or null */
} RdTexEntry;

static struct {
    RdTexEntry e[RDTEX_MAX_ENTRIES];

    struct {
        RdTex tex;
        uint32_t tick;
    } retired[RDTEX_MAX_RETIRED];

    uint32_t nRetired;
    uint32_t tick;
    int enhancedMips;
    RdTexCacheStats stats;
    RdTexReleaseFn release; /* texture packs: rdtex_SetReleaseHook */
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
        rd_DestroyTexture(s_tc.retired[0].tex);
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
 * by rdtex_Reset); a replacement's accounts are settled. */
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
    free(e->mips);
    memset(e, 0, sizeof(*e));
    s_tc.stats.entries--;
}

RdTex rdtex_Find(uint32_t id, uint32_t gen, int texa)
{
    RdTexEntry *e = entryOf(id, texa);

    if (e && e->gen == gen && rd__TexRec(e->tex.id) != NULL) {
        s_tc.stats.hits++;
        return e->tex;
    }
    s_tc.stats.misses++;
    return (RdTex){0};
}

RdTex rdtex_Store(uint32_t id, uint32_t gen, int texa, const RdTexImage *im,
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
    if (px == NULL || rdtex_Decode(im, px, &src) != 0) {
        free(px);
        s_tc.stats.failures++;
        return (RdTex){0};
    }
    if (texa != RDTEX_TEXA_REPLAY) {
        rdtex_ApplyTexa(px, (size_t)pw * ph, src, (RdTexA)texa);
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
            rd__Log("rd_tex: the cache is full (%d entries)", RDTEX_MAX_ENTRIES);
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
    if (e->tex.id != 0 && !e->replaced && rd__TexRec(e->tex.id) != NULL && e->w == pw &&
        e->h == ph && e->src == (uint8_t)src) {
        /* same shape: re-expand in place */
        rd_UpdateTexture(e->tex, px);
        s_tc.stats.updates++;
    } else {
        RdTex t;

        if (texa != RDTEX_TEXA_REPLAY) {
            t = rd_CreateTexture(pw, ph, px, (RdTexA)texa, debugName);
        } else {
            t = rd_CreateTextureSrc(pw, ph, px, src, debugName);
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
        if (rd__TexRec(e->tex.id) != NULL) {
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
    free(e->mips);
    e->mips = NULL;
    if (s_tc.enhancedMips && (pw & (pw - 1)) == 0 && (ph & (ph - 1)) == 0) {
        e->mips = malloc(rdtex_MipChainBytes(pw, ph) + 4);
        if (e->mips) {
            rdtex_BuildMipChain(px, pw, ph, e->mips);
        }
    }
    free(px);
    return e->tex;
}

const RdTexSampler *rdtex_Sampler(uint32_t id, int texa)
{
    RdTexEntry *e = entryOf(id, texa);

    return e ? &e->smp : NULL;
}

void rdtex_Drop(uint32_t id)
{
    for (uint32_t i = 0; i < RDTEX_MAX_ENTRIES; i++) {
        if (s_tc.e[i].used && s_tc.e[i].id == id) {
            freeEntry(&s_tc.e[i]);
        }
    }
}

void rdtex_FrameTick(void)
{
    uint32_t keep = 0;

    s_tc.tick++;
    for (uint32_t i = 0; i < s_tc.nRetired; i++) {
        if (s_tc.tick - s_tc.retired[i].tick >= 2) {
            rd_DestroyTexture(s_tc.retired[i].tex);
        } else {
            s_tc.retired[keep++] = s_tc.retired[i];
        }
    }
    s_tc.nRetired = keep;
}

void rdtex_Reset(void)
{
    for (uint32_t i = 0; i < RDTEX_MAX_ENTRIES; i++) {
        if (s_tc.e[i].used) {
            releaseReplaced(&s_tc.e[i]);
        }
        free(s_tc.e[i].mips);
    }
    memset(s_tc.e, 0, sizeof(s_tc.e));
    s_tc.nRetired = 0;
    memset(&s_tc.stats, 0, sizeof(s_tc.stats));
}

/* ------------------------------------------------------- texture packs */

/* the alpha the coverage of the replacement's box chain keeps: as the
   Enhanced mips (rd_replay.c RD_MIP_COVERAGE_REF, alpha > 64) */
#define RDTEX_REPLACEMENT_COVERAGE_REF 64

int rdtex_ReplacementMips(TexpackImage *img)
{
    if (!img || !img->blob || img->fmt != RD_TEXEL_RGBA8 || img->levels != 1 || img->w == 0 ||
        img->h == 0 || img->lv[0].data == NULL || img->lv[0].pitch != img->w * 4u) {
        return -1;
    }
    const uint32_t w = img->w, h = img->h;
    const size_t base = (size_t)w * h * 4;
    const size_t chain = rdtex_MipChainBytes(w, h);
    if (chain == 0) {
        return -1; /* 1 x 1: nothing to add */
    }
    uint8_t *blob = malloc(base + chain);
    if (!blob) {
        return -1;
    }
    memcpy(blob, img->lv[0].data, base);
    const uint32_t n = rdtex_BuildMipChain(blob, w, h, blob + base);
    rdtex_KeepAlphaCoverage(blob, w, h, blob + base, n, RDTEX_REPLACEMENT_COVERAGE_REF);
    free(img->blob);
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

RdTex rdtex_CreateReplacement(TexpackImage *img, uint32_t uvW, uint32_t uvH, const char *debugName)
{
    if (!g_rd.inited || !img || !img->blob || img->levels == 0 || img->w == 0 || img->h == 0) {
        return (RdTex){0};
    }
    const int block = rd__TexelIsBlock(img->fmt);
    if (!block && img->fmt != RD_TEXEL_RGBA8) {
        return (RdTex){0};
    }
    if (block && (!g_rd.hasDevice || !rhi_Limits()->bcTextures)) {
        return (RdTex){0};
    }
    if (g_rd.hasDevice &&
        (img->w > rhi_Limits()->maxTextureSize || img->h > rhi_Limits()->maxTextureSize)) {
        rd__Log("texture pack: \"%s\" is %ux%u, larger than this graphics card takes (%u)",
                debugName ? debugName : "?", img->w, img->h, rhi_Limits()->maxTextureSize);
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
            rdtex_ReplacementMips(img);
        } else if (!s_tc.bcSingleLogged) {
            s_tc.bcSingleLogged = 1;
            rd__Log("texture pack: compressed textures without mipmaps are drawn without them "
                    "(\"%s\" is the first)",
                    debugName ? debugName : "?");
        }
    }
    return rd__CreateTextureReplacement(img, uvW, uvH, debugName);
}

int rdtex_Replace(uint32_t id, uint32_t gen, int texa, RdTex rep)
{
    RdTexEntry *e = entryOf(id, texa);

    if (e == NULL || e->gen != gen || rep.id == 0 || rd__TexRec(rep.id) == NULL) {
        return -1;
    }
    if (e->tex.id != rep.id) {
        releaseReplaced(e); /* a replacement replaced again */
        if (rd__TexRec(e->tex.id) != NULL) {
            retire(e->tex);
        }
    }
    e->tex = rep;
    e->replaced = 1;
    /* the Enhanced CPU chain was the original's */
    free(e->mips);
    e->mips = NULL;
    s_tc.stats.replaced++;
    return 0;
}

void rdtex_RevertReplacements(void)
{
    for (uint32_t i = 0; i < RDTEX_MAX_ENTRIES; i++) {
        if (s_tc.e[i].used && s_tc.e[i].replaced) {
            /* forgotten: the next bind misses (rdtex_Find) and decodes the
               game's texture again */
            freeEntry(&s_tc.e[i]);
        }
    }
}

void rdtex_SetReleaseHook(RdTexReleaseFn fn)
{
    s_tc.release = fn;
}

void rdtex_SetEnhancedMips(int on)
{
    s_tc.enhancedMips = on != 0;
}

const RdTexCacheStats *rdtex_Stats(void)
{
    return &s_tc.stats;
}

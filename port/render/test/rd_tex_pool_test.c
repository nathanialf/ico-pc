/* rd_tex_pool_test.c: the texture decode pool (rd_tex_decode.h) against the
 * game thread's decode, byte for byte.
 *
 *   - 200 textures of every format the cache decodes (PSMCT32/24/16/16S,
 *     PSMT8/T4/T8H/T4HL/T4HH with 32-, 24- and 16-bit CLUTs in both
 *     orders and short CLUTs, the PSMZ formats), padded and not, under
 *     RDTEX_TEXA_REPLAY and the three TEXA modes, one in five stored with
 *     two further TIM2 levels (rdtex_store_levels; authored and reduced
 *     ones), then new generations of the same shape (re-expanded in place:
 *     changed texels, the same texels again) and a new shape, all stored
 *     before one rdtex_settle so the updates queue behind the creates of
 *     the same textures; the caller's bytes are overwritten after every
 *     store.  With 2 worker threads every texture's texels, source format,
 *     baked TEXA, GS levels and the alpha range of its texels equal the
 *     run with the pool off (threads 0), and so do the cache's counts;
 *   - the bounded queue: 300 stores with 1 worker and no settle, the first
 *     RDTEX_DECODE_QUEUE_MAX queued and the rest decoded on the game thread
 *     (RdTexCacheStats.fallbacks), the result the same as with the pool off;
 *   - on a device (Vulkan; 77 without one): the same textures uploaded by
 *     a replay, the alpha scan the upload keeps (rd__tex_alpha_scan) the
 *     same for every texture with the pool on and off.
 * Exit 0, 1 on a mismatch, 77 when the CPU checks pass and no device is
 * there for the upload check. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"
#include "rd_tex.h"
#include "rd_tex_decode.h"

static int failures;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static uint32_t hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

#define NTEX 200
#define NLEV 3

/* one texture as the game would hand it over: the master bytes of each
   level and its CLUT (the stores read copies, overwritten after) */
typedef struct Spec {
    RdTexImage im[NLEV];
    uint32_t n; /* images: 1, or NLEV for rdtex_store_levels */
    int texa;
    uint8_t *pix[NLEV];
    size_t pixBytes[NLEV];
    uint8_t *clut;
    size_t clutBytes;
} Spec;

static const uint32_t kPsm[] = {
    RDTEX_PSMCT32, RDTEX_PSMCT24, RDTEX_PSMCT16, RDTEX_PSMCT16S, RDTEX_PSMT8,
    RDTEX_PSMT4,   RDTEX_PSMT8H,  RDTEX_PSMT4HL, RDTEX_PSMT4HH,  RDTEX_PSMZ32,
    RDTEX_PSMZ24,  RDTEX_PSMZ16,  RDTEX_PSMZ16S,
};
static const uint32_t kCpsm[] = {RDTEX_PSMCT32, RDTEX_PSMCT16, RDTEX_PSMCT16S, RDTEX_PSMCT24};
static const int kTexa[] = {RDTEX_TEXA_REPLAY, RD_TEXA_80_80, RD_TEXA_7F_81_AEM, RD_TEXA_80_80_AEM};

static int indexedPsm(uint32_t psm)
{
    return psm == RDTEX_PSMT8 || psm == RDTEX_PSMT4 || psm == RDTEX_PSMT8H ||
           psm == RDTEX_PSMT4HL || psm == RDTEX_PSMT4HH;
}

static uint32_t entryBytes(uint32_t cpsm)
{
    return cpsm == RDTEX_PSMCT32 ? 4u : cpsm == RDTEX_PSMCT24 ? 3u : 2u;
}

static uint32_t pow2(uint32_t v)
{
    uint32_t p = 1;
    while (p < v) {
        p <<= 1;
    }
    return p;
}

/* texture i of the set, its bytes from seed */
static void makeSpec(Spec *s, uint32_t i, uint32_t seed)
{
    memset(s, 0, sizeof(*s));
    const uint32_t r = hash(i * 7919u + 17u);
    const uint32_t psm = kPsm[r % (sizeof(kPsm) / sizeof(kPsm[0]))];
    const uint32_t levels = i % 5 == 0 ? NLEV : 1;
    uint32_t w = 1 + hash(r) % 64, h = 1 + hash(r + 1) % 64;
    if (levels > 1) {
        w = 8u << (r % 3); /* powers of two: each level half the one before */
        h = 8u << ((r >> 4) % 3);
    }
    const int pad = levels > 1 || (r >> 8) % 3 != 0;
    s->n = levels;
    s->texa = kTexa[(r >> 12) % 4];
    s->im[0].w = w;
    s->im[0].h = h;
    s->im[0].padW = pad ? pow2(w) : 0;
    s->im[0].padH = pad ? pow2(h) : 0;
    s->im[0].psm = psm;
    if (indexedPsm(psm)) {
        const int big = psm == RDTEX_PSMT8 || psm == RDTEX_PSMT8H;
        const uint32_t full = big ? 256u : 16u;
        s->im[0].cpsm = kCpsm[(r >> 16) % 4];
        /* a short CLUT now and then (an index past it is masked) */
        s->im[0].clutColors = (r >> 20) % 4 == 0 ? full - (big ? 56u : 4u) : full;
        s->im[0].clutLinear = (int)((r >> 24) & 1);
        s->clutBytes = (size_t)s->im[0].clutColors * entryBytes(s->im[0].cpsm);
        s->clut = malloc(s->clutBytes);
        for (size_t k = 0; k < s->clutBytes; k++) {
            s->clut[k] = (uint8_t)hash((uint32_t)k * 31u + seed + i);
        }
    }
    for (uint32_t l = 0; l < levels; l++) {
        if (l > 0) {
            s->im[l] = s->im[0];
            s->im[l].w = w >> l;
            s->im[l].h = h >> l;
        }
        s->pixBytes[l] = rdtex_image_bytes(psm, s->im[l].w, s->im[l].h);
        s->pix[l] = malloc(s->pixBytes[l] ? s->pixBytes[l] : 1);
        /* level 2 of every other set much brighter than level 1: authored */
        const int bright = l == 2 && (i / 5) % 2 == 0;
        for (size_t k = 0; k < s->pixBytes[l]; k++) {
            s->pix[l][k] = bright ? 0xF0u : (uint8_t)hash((uint32_t)k + seed * 131u + i * 7u + l);
        }
    }
}

static void freeSpec(Spec *s)
{
    for (uint32_t l = 0; l < NLEV; l++) {
        free(s->pix[l]);
    }
    free(s->clut);
}

/* The caller's side of a store: the bytes copied out of the spec, stored,
   then overwritten (the game may change them once the call returns) */
static RdTex storeSpec(const Spec *s, uint32_t id, uint32_t gen)
{
    uint8_t *pix[NLEV] = {0};
    uint8_t *clut = NULL;
    RdTexImage ims[NLEV];
    if (s->clut) {
        clut = malloc(s->clutBytes);
        memcpy(clut, s->clut, s->clutBytes);
    }
    for (uint32_t l = 0; l < s->n; l++) {
        pix[l] = malloc(s->pixBytes[l] ? s->pixBytes[l] : 1);
        memcpy(pix[l], s->pix[l], s->pixBytes[l]);
        ims[l] = s->im[l];
        ims[l].pixels = pix[l];
        ims[l].clut = clut;
    }
    char name[24];
    snprintf(name, sizeof(name), "pool %u", id);
    const RdTex t = s->n > 1 ? rdtex_store_levels(id, gen, s->texa, ims, s->n, NULL, name)
                             : rdtex_store(id, gen, s->texa, ims, NULL, name);
    for (uint32_t l = 0; l < s->n; l++) {
        memset(pix[l], 0xA5, s->pixBytes[l]);
        free(pix[l]);
    }
    if (clut) {
        memset(clut, 0x5A, s->clutBytes);
        free(clut);
    }
    return t;
}

/* what a texture holds once settled */
typedef struct Snap {
    int live;
    uint32_t w, h, src, bakedTexa, gsLevels;
    uint8_t *pixels, *chain;
    size_t chainBytes;
    RdTexAlphaScan scan; /* of the base and the GS levels, on the CPU */
    int devOk;           /* rd__tex_alpha_scan after an upload */
    RdTexAlphaScan dev;
} Snap;

static void snap(Snap *o, uint32_t id, uint32_t gen, int texa)
{
    memset(o, 0, sizeof(*o));
    const RdTex t = rdtex_find(id, gen, texa);
    const RdTexRec *r = rd__tex_rec(t.id);
    if (!r || !r->pixels) {
        return;
    }
    o->live = 1;
    o->w = r->w;
    o->h = r->h;
    o->src = r->src;
    o->bakedTexa = r->bakedTexa;
    o->gsLevels = r->gsLevels;
    o->pixels = malloc((size_t)r->w * r->h * 4);
    memcpy(o->pixels, r->pixels, (size_t)r->w * r->h * 4);
    rdtex_alpha_scan_begin(&o->scan);
    rdtex_alpha_scan(&o->scan, r->pixels, (size_t)r->w * r->h);
    if (r->gsLevels > 1 && r->gsChain) {
        o->chainBytes = rd__gs_chain_bytes(r->w, r->h, r->gsLevels);
        o->chain = malloc(o->chainBytes);
        memcpy(o->chain, r->gsChain, o->chainBytes);
        rdtex_alpha_scan(&o->scan, r->gsChain, o->chainBytes / 4);
    }
    o->devOk = rd__tex_alpha_scan(t.id, &o->dev) ? 1 : 0;
}

static void freeSnaps(Snap *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        free(s[i].pixels);
        free(s[i].chain);
    }
}

#define BASE_ID 3000u
#define NUPD 30  /* new texels, same shape */
#define NSAME 10 /* the same texels again */

static Spec s_set[NTEX], s_upd[NUPD];
static Snap s_snap[2][NTEX];
static RdTexCacheStats s_stats[2];

/* The set through rdtex_store with threads workers; *gens: the generation
   each texture ends at */
static void storeSet(uint32_t gens[NTEX])
{
    for (uint32_t i = 0; i < NTEX; i++) {
        CHECK(storeSpec(&s_set[i], BASE_ID + i, 1).id != 0, "store %u", i);
        gens[i] = 1;
    }
    /* before any settle: the same shapes again, behind their creates */
    for (uint32_t i = 0; i < NUPD; i++) {
        CHECK(storeSpec(&s_upd[i], BASE_ID + i, 2).id != 0, "update %u", i);
        gens[i] = 2;
    }
    for (uint32_t i = NUPD; i < NUPD + NSAME; i++) {
        CHECK(storeSpec(&s_set[i], BASE_ID + i, 2).id != 0, "same texels %u", i);
        gens[i] = 2;
    }
    /* and a new shape for the last: a new texture, the old one retired */
    {
        Spec big;
        makeSpec(&big, NTEX + 7, 99);
        big.im[0].w = 32;
        big.im[0].h = 16;
        big.im[0].padW = big.im[0].padH = 0;
        big.n = 1;
        free(big.pix[0]);
        big.pixBytes[0] = rdtex_image_bytes(big.im[0].psm, 32, 16);
        big.pix[0] = malloc(big.pixBytes[0]);
        for (size_t k = 0; k < big.pixBytes[0]; k++) {
            big.pix[0][k] = (uint8_t)hash((uint32_t)k + 4242u);
        }
        big.texa = s_set[NTEX - 1].texa;
        CHECK(storeSpec(&big, BASE_ID + NTEX - 1, 3).id != 0, "new shape");
        gens[NTEX - 1] = 3;
        freeSpec(&big);
    }
}

static void runCpu(int k, int threads)
{
    uint32_t gens[NTEX];
    CHECK(rd__init_record_only(512, 512), "record-only context");
    rdtex_set_decode_threads(threads);
    CHECK(rdtex_decode_threads() == threads, "threads %d in force (%d)", threads,
          rdtex_decode_threads());
    storeSet(gens);
    if (threads > 0) {
        CHECK(rdtex__decode_pending() > 0, "the jobs wait for the settle");
    }
    rdtex_settle();
    CHECK(rdtex__decode_pending() == 0, "settled");
    for (uint32_t i = 0; i < NTEX; i++) {
        snap(&s_snap[k][i], BASE_ID + i, gens[i], s_set[i].texa);
        CHECK(s_snap[k][i].live, "texture %u live (threads %d)", i, threads);
    }
    s_stats[k] = *rdtex_stats();
    rd_shutdown();
    rdtex_reset();
}

static void compare(const char *what)
{
    int bad = 0;
    for (uint32_t i = 0; i < NTEX; i++) {
        const Snap *a = &s_snap[0][i], *b = &s_snap[1][i];
        const int same = a->live == b->live && a->w == b->w && a->h == b->h && a->src == b->src &&
                         a->bakedTexa == b->bakedTexa && a->gsLevels == b->gsLevels &&
                         a->chainBytes == b->chainBytes &&
                         (!a->live || memcmp(a->pixels, b->pixels, (size_t)a->w * a->h * 4) == 0) &&
                         (!a->chain || memcmp(a->chain, b->chain, a->chainBytes) == 0) &&
                         memcmp(&a->scan, &b->scan, sizeof(a->scan)) == 0 && a->devOk == b->devOk &&
                         (!a->devOk || memcmp(&a->dev, &b->dev, sizeof(a->dev)) == 0);
        if (!same && bad++ < 5) {
            printf("  %s: texture %u differs (%ux%u src %u levels %u)\n", what, i, a->w, a->h,
                   a->src, a->gsLevels);
        }
    }
    CHECK(bad == 0, "%s: %d textures differ", what, bad);
}

/* 300 stores, no settle: the queue holds RDTEX_DECODE_QUEUE_MAX, the game
   thread decodes the rest, in order with them */
#define NFALL 300u

static void runFallback(int k, int threads, Snap *out, RdTexCacheStats *st)
{
    CHECK(rd__init_record_only(512, 512), "record-only context");
    rdtex_set_decode_threads(threads);
    for (uint32_t i = 0; i < NFALL; i++) {
        CHECK(storeSpec(&s_set[i % NTEX], 5000u + i, 1).id != 0, "fallback store %u", i);
    }
    rdtex_settle();
    for (uint32_t i = 0; i < NFALL; i++) {
        snap(&out[i], 5000u + i, 1, s_set[i % NTEX].texa);
    }
    *st = *rdtex_stats();
    (void)k;
    rd_shutdown();
    rdtex_reset();
}

static void fallbackChecks(void)
{
    static Snap a[NFALL], b[NFALL];
    RdTexCacheStats sa, sb;
    runFallback(0, 0, a, &sa);
    runFallback(1, 1, b, &sb);
    CHECK(sa.pooled == 0 && sa.fallbacks == 0, "pool off: nothing queued (%u, %u)", sa.pooled,
          sa.fallbacks);
    CHECK(sb.pooled == RDTEX_DECODE_QUEUE_MAX && sb.fallbacks == NFALL - RDTEX_DECODE_QUEUE_MAX,
          "the bounded queue: %u queued, %u on the game thread (want %u, %u)", sb.pooled,
          sb.fallbacks, RDTEX_DECODE_QUEUE_MAX, NFALL - RDTEX_DECODE_QUEUE_MAX);
    int bad = 0;
    for (uint32_t i = 0; i < NFALL; i++) {
        bad += !(a[i].live && b[i].live && a[i].w == b[i].w && a[i].h == b[i].h &&
                 memcmp(a[i].pixels, b[i].pixels, (size_t)a[i].w * a[i].h * 4) == 0 &&
                 a[i].gsLevels == b[i].gsLevels &&
                 (!a[i].chain || memcmp(a[i].chain, b[i].chain, a[i].chainBytes) == 0));
    }
    CHECK(bad == 0, "the bounded queue's result: %d textures differ from the game thread's", bad);
    freeSnaps(a, NFALL);
    freeSnaps(b, NFALL);
}

/* the set on a device, uploaded by one replay; 0 when there is no device */
static int runDevice(int k, int threads)
{
    uint32_t gens[NTEX];
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ORIGINAL;
    if (!rd_init(512, 512, &st, NULL)) {
        return 0;
    }
    rdtex_set_decode_threads(threads);
    storeSet(gens);
    rd_begin_frame();
    rd_end_frame(0); /* the replay settles and uploads every dirty texture */
    for (uint32_t i = 0; i < NTEX; i++) {
        snap(&s_snap[k][i], BASE_ID + i, gens[i], s_set[i].texa);
    }
    rd_shutdown();
    rdtex_reset();
    return 1;
}

int main(void)
{
    rd__set_not_implemented_fatal(true);
    for (uint32_t i = 0; i < NTEX; i++) {
        makeSpec(&s_set[i], i, 1);
    }
    for (uint32_t i = 0; i < NUPD; i++) {
        makeSpec(&s_upd[i], i, 2); /* the same shape, other bytes */
    }

    runCpu(0, 0);
    runCpu(1, 2);
    compare("2 workers against the game thread");
    CHECK(s_stats[0].decodes == s_stats[1].decodes && s_stats[0].creates == s_stats[1].creates &&
              s_stats[0].updates == s_stats[1].updates &&
              s_stats[0].failures == s_stats[1].failures && s_stats[0].failures == 0,
          "the counts: decodes %u/%u, creates %u/%u, updates %u/%u, failures %u/%u",
          s_stats[0].decodes, s_stats[1].decodes, s_stats[0].creates, s_stats[1].creates,
          s_stats[0].updates, s_stats[1].updates, s_stats[0].failures, s_stats[1].failures);
    CHECK(s_stats[0].pooled == 0 && s_stats[1].pooled == s_stats[1].decodes &&
              s_stats[1].fallbacks == 0,
          "every store queued with the pool on (%u of %u, %u on the game thread)",
          s_stats[1].pooled, s_stats[1].decodes, s_stats[1].fallbacks);
    {
        int levels = 0;
        for (uint32_t i = 0; i < NTEX; i++) {
            levels += s_snap[0][i].gsLevels > 1;
        }
        CHECK(levels > 0, "some textures keep authored GS levels (%d)", levels);
    }
    freeSnaps(s_snap[0], NTEX);
    freeSnaps(s_snap[1], NTEX);

    fallbackChecks();
    if (failures) {
        printf("rd_tex_pool_test: %d failures\n", failures);
        return 1;
    }

    if (!runDevice(0, 0)) {
        printf("rd_tex_pool_test: CPU checks ok; SKIP the upload check: no usable Vulkan device\n");
        return 77;
    }
    runDevice(1, 2);
    compare("the uploads' alpha scans, 2 workers against the game thread");
    {
        int scanned = 0;
        for (uint32_t i = 0; i < NTEX; i++) {
            scanned += s_snap[0][i].devOk;
        }
        CHECK(scanned > 0, "the upload scanned textures (%d)", scanned);
    }
    freeSnaps(s_snap[0], NTEX);
    freeSnaps(s_snap[1], NTEX);
    rdtex_set_decode_threads(0);
    for (uint32_t i = 0; i < NTEX; i++) {
        freeSpec(&s_set[i]);
    }
    for (uint32_t i = 0; i < NUPD; i++) {
        freeSpec(&s_upd[i]);
    }
    if (failures) {
        printf("rd_tex_pool_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_tex_pool_test: ok\n");
    return 0;
}

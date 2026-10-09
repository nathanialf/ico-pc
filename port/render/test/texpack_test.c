/* texpack_test: the texture pack's index and loader (texpack.c) on their
 * own.
 *
 *   index   a folder tree of fake files under argv[1]: the standard layout
 *           of the user folder and of the program's folder, the tolerant
 *           ones (textures/replacements, files directly under textures/),
 *           a name in both roots (the user folder's wins, the other is a
 *           duplicate), upper-case extensions, a "-mip1" file, a region
 *           name, the two malformed names of Sad Origami's pack (a nine
 *           digit bits field, a " - copia" copy), a readme, and the dumps
 *           and other folders under textures/ that are not walked;
 *           portable mode (both roots the same folder) walks it once.
 *   loader  the thread against a fake texture cache: a request installed
 *           by a pump with the first root's file, a second request for the
 *           same texture refused as queued, a generation changed while the
 *           file loaded dropped, a file that fails remembered, the budget
 *           declining and remembering, the RAM cache installing at once,
 *           the cache keeping images without their mip chain (the copy
 *           installed gets it), the precache reading the PNGs first and
 *           passing over a file that does not fit its own memory limit
 *           (not the graphics budget) to cache the next, the pack switched
 *           off with loads in flight (nothing goes in), a replacement the
 *           graphics card refused not offered again, shutdown joining a
 *           busy thread.
 *   links   a folder link back up inside a pack is not followed.
 *
 * The texture cache (rd_tex.h), the PNG writer and the file loaders are
 * fakes here, so nothing needs a device; the names are texpack_name.c's.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_fs.h"
#include "rd_internal.h"
#include "rd_tex.h"
#include "texpack.h"

#if ICO_TEXPACK_THREAD
#include <SDL3/SDL.h>
#endif
#ifndef _WIN32
#include <unistd.h>
#endif

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

/* ------------------------------------------------------- fake loaders */

#if ICO_TEXPACK_THREAD
static SDL_AtomicInt s_active, s_started;

static void sleepMs(int ms)
{
    SDL_Delay((Uint32)ms);
}

static void activeAdd(int v)
{
    SDL_AddAtomicInt(&s_active, v);
    if (v > 0) {
        SDL_AddAtomicInt(&s_started, 1);
    }
}

static int activeNow(void)
{
    return SDL_GetAtomicInt(&s_active);
}
#else
static int s_activeN;

static void sleepMs(int ms)
{
    (void)ms;
}

static void activeAdd(int v)
{
    s_activeN += v;
}

static int activeNow(void)
{
    return s_activeN;
}
#endif

#define BIG_BYTES (1536u * 1024u) /* over a 1 MB budget */

/* a file "IMG:<tag>" loads as a 1x1 RGBA8 image whose blob starts with the
   tag (256 bytes, or BIG_BYTES when the tag says "big"; "slow" takes 40
   ms); anything else fails */
static int fakeLoad(const uint8_t *data, size_t size, TexpackImage *out)
{
    char tag[64];
    int r = -1;

    activeAdd(1);
    memset(out, 0, sizeof(*out));
    if (size > 4 && size < sizeof(tag) + 4 && memcmp(data, "IMG:", 4) == 0) {
        memcpy(tag, data + 4, size - 4);
        tag[size - 4] = 0;
        if (strstr(tag, "slow")) {
            sleepMs(40);
        }
        size_t bytes = strstr(tag, "big") ? BIG_BYTES : 256;
        out->blob = calloc(1, bytes);
        if (out->blob) {
            strcpy((char *)out->blob, tag);
            out->bytes = bytes;
            out->fmt = RD_TEXEL_RGBA8;
            out->w = out->h = 1;
            out->levels = 1;
            out->lv[0].data = (const uint8_t *)out->blob + 64;
            out->lv[0].w = out->lv[0].h = 1;
            out->lv[0].pitch = 4;
            out->lv[0].size = 4;
            r = 0;
        }
    }
    activeAdd(-1);
    return r;
}

int texpack_load_png(const uint8_t *data, size_t size, const char *file, TexpackImage *out)
{
    (void)file;
    return fakeLoad(data, size, out);
}

int texpack_load_dds(const uint8_t *data, size_t size, int bcSupported, const char *file,
                     TexpackImage *out)
{
    (void)bcSupported;
    (void)file;
    return fakeLoad(data, size, out);
}

void texpack_free_image(TexpackImage *img)
{
    if (img) {
        free(img->blob);
        memset(img, 0, sizeof(*img));
    }
}

static uint32_t s_maxSide;

void texpack_set_max_side(uint32_t side)
{
    s_maxSide = side;
}

uint32_t texpack_max_side(void)
{
    return s_maxSide;
}

/* the renderer's settings: the pack on unless a test turns it off */
static RdSettings s_rs = {.texturePack = 1};

const RdSettings *rd_get_settings(void)
{
    return &s_rs;
}

/* -------------------------------------------------- fake texture cache */

#define IDS 64

static uint32_t s_lastUvW, s_lastUvH;

static struct {
    uint32_t gen[IDS];
    int current[IDS];
    uint32_t installed[IDS]; /* the replacement's RdTex id */
    char tag[IDS][64];
    uint32_t uvW[IDS], uvH[IDS];
    uint32_t levels[IDS]; /* the installed image's levels */
    uint32_t nextRep;
    int destroyed;
    char lastTag[64];
    uint32_t lastLevels;
    uint32_t refusedRep; /* the replacement the "card" refused */
} s_tc;

static void setCurrent(uint32_t id, uint32_t gen)
{
    s_tc.current[id] = 1;
    s_tc.gen[id] = gen;
}

RdTex rdtex_find(uint32_t id, uint32_t gen, int texa)
{
    (void)texa;
    return id < IDS && s_tc.current[id] && s_tc.gen[id] == gen ? (RdTex){1000 + id} : (RdTex){0};
}

RdTex rdtex_create_replacement(struct TexpackImage *img, uint32_t uvW, uint32_t uvH,
                               const char *debugName)
{
    (void)debugName;
    s_lastUvW = uvW;
    s_lastUvH = uvH;
    if (!img || !img->blob) {
        return (RdTex){0};
    }
    snprintf(s_tc.lastTag, sizeof(s_tc.lastTag), "%s", (const char *)img->blob);
    s_tc.lastLevels = img->levels;
    texpack_free_image(img); /* moved into the texture */
    return (RdTex){++s_tc.nextRep};
}

int rdtex_replace(uint32_t id, uint32_t gen, int texa, RdTex rep)
{
    (void)texa;
    if (id >= IDS || !s_tc.current[id] || s_tc.gen[id] != gen) {
        return -1;
    }
    s_tc.installed[id] = rep.id;
    snprintf(s_tc.tag[id], sizeof(s_tc.tag[id]), "%s", s_tc.lastTag);
    s_tc.uvW[id] = s_lastUvW;
    s_tc.uvH[id] = s_lastUvH;
    s_tc.levels[id] = s_tc.lastLevels;
    return 0;
}

/* the fake chain: a second level as large as the first, so its bytes show
   where a chain was added */
int rdtex_replacement_mips_from(const struct TexpackImage *src, struct TexpackImage *dst)
{
    if (!src || !src->blob || src->fmt != RD_TEXEL_RGBA8 || src->levels != 1) {
        return -1;
    }
    uint8_t *blob = calloc(1, src->bytes * 2);
    if (!blob) {
        return -1;
    }
    memcpy(blob, src->blob, src->bytes);
    *dst = *src;
    dst->blob = blob;
    dst->bytes = src->bytes * 2;
    dst->lv[0].data = blob + (src->lv[0].data - (const uint8_t *)src->blob);
    dst->lv[1] = dst->lv[0];
    dst->levels = 2;
    return 0;
}

int rdtex_replacement_mips(struct TexpackImage *img)
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
    return t.id != 0 && t.id == s_tc.refusedRep;
}

static RdTexReleaseFn s_releaseHook;

void rdtex_set_release_hook(RdTexReleaseFn fn)
{
    s_releaseHook = fn;
}

void rd_destroy_texture(RdTex t)
{
    (void)t;
    s_tc.destroyed++;
}

int rdtex_decode(const RdTexImage *im, uint8_t *out, RdTexSrc *src)
{
    (void)im;
    (void)out;
    (void)src;
    return -1;
}

void rdtex_apply_texa(uint8_t *rgba, size_t n, RdTexSrc src, RdTexA mode)
{
    (void)rgba;
    (void)n;
    (void)src;
    (void)mode;
}

bool rd_write_png(const char *path, const uint8_t *rgba, uint32_t w, uint32_t h, uint32_t pitch,
                  int withAlpha)
{
    (void)path;
    (void)rgba;
    (void)w;
    (void)h;
    (void)pitch;
    (void)withAlpha;
    return false;
}

/* ---------------------------------------------------------- the tree */

static char s_user[1024], s_prog[1024];

/* mkdir -p of the folder part of path below base, then the file */
static void put(const char *base, const char *rel, const char *text)
{
    char path[2048];
    snprintf(path, sizeof(path), "%s/%s", base, rel);
    for (char *s = path + strlen(base) + 1; *s; s++) {
        if (*s == '/') {
            *s = 0;
            (void)ico_mkdir(path);
            *s = '/';
        }
    }
    FILE *f = ico_fopen(path, "wb");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

/* the names: hex hashes as Sad Origami's pack has them (unpadded) */
#define N1 "e997f3381dbc55e-1a2b3c4d5e6f7081-00002513"
#define N2 "abc123-def456-00002514"
#define N3 "1234567890abcdef-fedcba0987654321-00002913"
#define N4 "77-88-00002513"
#define N5 "5555-6666-00002513"
#define N6 "aaaa-bbbb-00002513"
#define N7 "cccc-dddd-00002513"
#define N8 "eeee-ffff-00002513"
#define ND "123abc-00002400" /* a direct format: no CLUT part */

static void buildTree(const char *dir)
{
    snprintf(s_user, sizeof(s_user), "%s/texpack_user", dir);
    snprintf(s_prog, sizeof(s_prog), "%s/texpack_prog", dir);
    (void)ico_mkdir(s_user);
    (void)ico_mkdir(s_prog);
    /* the user folder's standard layout */
    put(s_user, "textures/SCES-50760/replacements/a/" N1 ".dds", "IMG:user-n1");
    put(s_user, "textures/SCES-50760/replacements/a/" N2 ".PNG", "IMG:user-n2-slow");
    put(s_user, "textures/SCES-50760/replacements/a/" N2 "-mip1.png", "IMG:mip");
    put(s_user, "textures/SCES-50760/replacements/b/sub/deeper/" N3 ".png", "IMG:user-n3");
    put(s_user, "textures/SCES-50760/replacements/b/" ND ".Dds", "IMG:user-direct");
    put(s_user, "textures/SCES-50760/replacements/b/9999-8888-r40x20-00002513.png", "IMG:region");
    put(s_user, "textures/SCES-50760/replacements/c/e997f3381dbc55e-1a2b3c4d5e6f7081-000026530.dds",
        "IMG:nine");
    put(s_user, "textures/SCES-50760/replacements/c/" N1 " - copia.dds", "IMG:copia");
    put(s_user, "textures/SCES-50760/replacements/Mods/Instructions.txt", "read me");
    /* the note the release packages ship in this folder */
    put(s_prog, "textures/SCES-50760/replacements/README.txt", "where a texture pack goes\r\n");
    /* the program folder's: N1 again (the user folder's wins), N4 fails */
    put(s_prog, "textures/SCES-50760/replacements/" N1 ".png", "IMG:prog-n1");
    put(s_prog, "textures/SCES-50760/replacements/" N4 ".dds", "BAD");
    /* the tolerant layouts */
    put(s_user, "textures/replacements/x/" N5 ".png", "IMG:user-n5-big");
    put(s_prog, "textures/" N6 ".dds", "IMG:prog-n6");
    /* not walked: the dumps, another folder under textures/ */
    put(s_prog, "textures/SCES-50760/dumps/" N7 ".png", "IMG:dump");
    put(s_prog, "textures/other/" N8 ".png", "IMG:other");
}

static int entryOf(const char *name)
{
    char file[128];
    TexpackName tn;
    snprintf(file, sizeof(file), "%s.png", name);
    if (texpack_parse_name(file, &tn) != 0) {
        return -2;
    }
    return texpack_lookup(&tn);
}

static void initPackCache(const char *user, const char *prog, uint32_t budgetMb, int precache,
                          uint32_t cacheMb)
{
    TexpackConfig c;
    memset(&c, 0, sizeof(c));
    c.userDir = user;
    c.programDir = prog;
    c.serial = "SCES-50760";
    c.budgetMb = budgetMb;
    c.cacheMb = cacheMb;
    c.precache = precache;
    c.bcSupported = 1;
    texpack_init(&c);
}

static void initPack(const char *user, const char *prog, uint32_t budgetMb, int precache)
{
    initPackCache(user, prog, budgetMb, precache, 0);
}

static TexpackStats stats(void)
{
    TexpackStats s;
    texpack_get_stats(&s);
    return s;
}

/* pumps (as the game fiber does once a frame) until done says so, or
   about three seconds */
#define WAIT_FOR(cond)                                                                             \
    do {                                                                                           \
        for (int w_ = 0; w_ < 3000 && !(cond); w_++) {                                             \
            texpack_pump();                                                                        \
            if (!(cond)) {                                                                         \
                sleepMs(1);                                                                        \
            }                                                                                      \
        }                                                                                          \
    } while (0)

/* --------------------------------------------------------------- index */

static void testIndex(void)
{
    initPack(s_user, s_prog, 0, 0);
    TexpackStats s = stats();
    CHECK(texpack_count() == 7, "7 replacements (N1..N6 and the direct one), got %d",
          texpack_count());
    /* the .txt files (Instructions.txt, the packaged README.txt) are neither counted
     * as files nor malformed */
    CHECK(s.files == 12, "12 png/dds files in the walked folders, got %u", s.files);
    CHECK(s.duplicates == 1, "the program folder's N1 a duplicate, got %u", s.duplicates);
    CHECK(s.mipFiles == 1 && s.regions == 1 && s.malformed == 2,
          "one -mip file, one region, two malformed: %u %u %u", s.mipFiles, s.regions, s.malformed);
    const char *found[] = {N1, N2, N3, N4, N5, N6, ND};
    for (int i = 0; i < 7; i++) {
        CHECK(entryOf(found[i]) >= 0, "%s indexed", found[i]);
    }
    CHECK(entryOf(N7) == -1, "the dumps folder is not walked");
    CHECK(entryOf(N8) == -1, "a folder under textures/ is not walked");
    TexpackName rn;
    CHECK(texpack_parse_name("9999-8888-r40x20-00002513.png", &rn) == 1 && rn.regionW != 0 &&
              rn.regionH != 0 && texpack_lookup(&rn) == -1,
          "a region name parses and never matches");
    TexpackName dn;
    CHECK(texpack_parse_name(ND ".png", &dn) == 0 && !dn.hasClut && texpack_lookup(&dn) >= 0,
          "a direct name (no CLUT part) matches");
    TexpackName wrong = dn;
    wrong.bits ^= 1;
    CHECK(texpack_lookup(&wrong) == -1, "other bits: no match");

    /* portable mode: the user folder is the program's, walked once */
    initPack(s_user, s_user, 0, 0);
    s = stats();
    CHECK(texpack_count() == 5 && s.duplicates == 0,
          "one root once: 5 replacements, no duplicates (%d, %u)", texpack_count(), s.duplicates);
    /* no pack anywhere */
    char none[1100];
    snprintf(none, sizeof(none), "%s/missing", s_prog);
    initPack(none, NULL, 0, 0);
    CHECK(texpack_count() == 0 && texpack_lookup(&dn) == -1, "no pack: nothing indexed");
    CHECK(texpack_request(0, 1, 1, RDTEX_TEXA_REPLAY, 1, 1) == -1, "no pack: requests refused");
    texpack_pump();
    texpack_shutdown();
}

/* -------------------------------------------------------------- loader */

static void testLoader(void)
{
    memset(&s_tc, 0, sizeof(s_tc));
    initPack(s_user, s_prog, 0, 0);
    const int e1 = entryOf(N1), e2 = entryOf(N2), e3 = entryOf(N3), e4 = entryOf(N4),
              e5 = entryOf(N5), e6 = entryOf(N6);
    CHECK(e1 >= 0 && e2 >= 0 && e3 >= 0 && e4 >= 0 && e5 >= 0 && e6 >= 0, "the entries");

    /* a request, installed by a pump, with the user folder's file */
    setCurrent(5, 40);
    CHECK(texpack_request(e1, 5, 40, RDTEX_TEXA_REPLAY, 64, 32) == 0, "request queued");
    CHECK(texpack_request(e1, 5, 40, RDTEX_TEXA_REPLAY, 64, 32) == 1, "the same: already queued");
    WAIT_FOR(s_tc.installed[5] != 0);
    CHECK(s_tc.installed[5] != 0 && strcmp(s_tc.tag[5], "user-n1") == 0,
          "installed from the user folder's file (%s)", s_tc.tag[5]);
    CHECK(s_tc.uvW[5] == 64 && s_tc.uvH[5] == 32, "the GS size passed on (%ux%u)", s_tc.uvW[5],
          s_tc.uvH[5]);

    /* the texture changed while the file loaded: dropped, never created */
    setCurrent(6, 48);
    CHECK(texpack_request(e3, 6, 48, RDTEX_TEXA_REPLAY, 64, 64) == 0, "request (gen 48)");
    s_tc.gen[6] = 56; /* a CLUT scroll re-stored it */
    WAIT_FOR(stats().discarded == 1);
    CHECK(stats().discarded == 1 && s_tc.installed[6] == 0 && s_tc.destroyed == 0,
          "a stale generation dropped before a texture was made");

    /* a file that does not load: remembered */
    setCurrent(7, 8);
    CHECK(texpack_request(e4, 7, 8, RDTEX_TEXA_REPLAY, 8, 8) == 0, "request (a bad file)");
    WAIT_FOR(stats().loadFailed == 1);
    CHECK(stats().loadFailed == 1 && s_tc.installed[7] == 0, "the bad file failed");
    CHECK(texpack_request(e4, 7, 9, RDTEX_TEXA_REPLAY, 8, 8) == -1, "and is not tried again");

    /* the pack switched off with loads in flight: N2 is slow (loading
       when the switch comes), N6 waits behind it; neither goes in, and both
       count as discarded */
    setCurrent(30, 1);
    setCurrent(31, 1);
    const uint32_t discarded = stats().discarded;
    CHECK(texpack_request(e2, 30, 1, RDTEX_TEXA_REPLAY, 8, 8) == 0 &&
              texpack_request(e6, 31, 1, RDTEX_TEXA_REPLAY, 8, 8) == 0,
          "two requests");
    sleepMs(5);
    s_rs.texturePack = 0;
    WAIT_FOR(stats().discarded == discarded + 2);
    for (int i = 0; i < 100; i++) {
        texpack_pump();
        sleepMs(1);
    }
    CHECK(s_tc.installed[30] == 0 && s_tc.installed[31] == 0 && stats().discarded == discarded + 2,
          "pack off: the load in flight and the queued one dropped (%u discarded)",
          stats().discarded - discarded);
    s_rs.texturePack = 1;
    texpack_shutdown();

    /* the budget: 1 MB; N5 is 1.5 MB */
    memset(&s_tc, 0, sizeof(s_tc));
    initPack(s_user, s_prog, 1, 0);
    CHECK(texpack_budget_limit() == 1u << 20, "budget 1 MB");
    setCurrent(8, 16);
    CHECK(texpack_request(e5, 8, 16, RDTEX_TEXA_REPLAY, 8, 8) == 0, "request (over budget)");
    WAIT_FOR(stats().declined == 1);
    CHECK(stats().declined == 1 && s_tc.installed[8] == 0 && texpack_budget_used() == 0,
          "declined, nothing charged");
    CHECK(texpack_request(e5, 8, 16, RDTEX_TEXA_REPLAY, 8, 8) == -1, "declined: remembered");
    setCurrent(9, 1);
    CHECK(texpack_request(e1, 9, 1, RDTEX_TEXA_REPLAY, 8, 8) == 0, "request (fits)");
    WAIT_FOR(s_tc.installed[9] != 0);
    CHECK(s_tc.installed[9] != 0 && texpack_budget_used() == 512 && s_tc.levels[9] == 2,
          "charged 512 bytes, the image with its chain (%llu, %u levels)",
          (unsigned long long)texpack_budget_used(), s_tc.levels[9]);
    CHECK(s_releaseHook == texpack_budget_release, "the cache's release hook is the budget's");
    s_releaseHook((RdTex){s_tc.installed[9]}); /* the cache gives it up */
    CHECK(texpack_budget_used() == 0, "released with its texture");
    texpack_budget_release((RdTex){12345});
    CHECK(texpack_budget_used() == 0, "releasing a texture never charged: nothing");

    /* the graphics card refused a replacement: rdtex_find forgets the
       entry (the release hook), and the file is not offered again */
    setCurrent(10, 1);
    CHECK(texpack_request(e3, 10, 1, RDTEX_TEXA_REPLAY, 8, 8) == 0, "request (refused later)");
    WAIT_FOR(s_tc.installed[10] != 0);
    s_tc.refusedRep = s_tc.installed[10];
    s_releaseHook((RdTex){s_tc.installed[10]});
    CHECK(stats().refused == 1 && texpack_budget_used() == 0, "refused: counted, released");
    setCurrent(10, 2);
    CHECK(texpack_request(e3, 10, 2, RDTEX_TEXA_REPLAY, 8, 8) == -1,
          "a refused file is not offered again");
    s_tc.refusedRep = 0;
    texpack_shutdown();

    /* shutdown joins a busy thread: ten slow loads queued */
    initPack(s_user, s_prog, 0, 0);
    for (uint32_t id = 10; id < 20; id++) {
        setCurrent(id, 1);
        (void)texpack_request(e2, id, 1, RDTEX_TEXA_REPLAY, 8, 8);
    }
    sleepMs(5);
    texpack_shutdown();
    CHECK(activeNow() == 0, "no load running after shutdown");
    CHECK(s_releaseHook == NULL, "shutdown takes the release hook back");
    CHECK(texpack_count() == 0, "shutdown forgets the index");
}

static void testPrecache(void)
{
    /* everything read ahead: a request goes in at the next pump */
    memset(&s_tc, 0, sizeof(s_tc));
    initPack(s_user, s_prog, 0, 1);
    WAIT_FOR(stats().precacheDone);
    TexpackStats s = stats();
    CHECK(s.cached == 6 && s.loadFailed == 1 && s.skipped == 0,
          "precache: 6 cached, the bad file failed (%u, %u, %u skipped)", s.cached, s.loadFailed,
          s.skipped);
    CHECK(s.cacheBytes == 5 * 256 + BIG_BYTES,
          "the cache holds the files as loaded, no mip chain (%llu bytes)",
          (unsigned long long)s.cacheBytes);
    setCurrent(20, 4);
    const uint32_t installed = stats().installed;
    CHECK(texpack_request(entryOf(N3), 20, 4, RDTEX_TEXA_REPLAY, 16, 16) == 2,
          "request (cached): installed now");
    CHECK(s_tc.installed[20] != 0 && strcmp(s_tc.tag[20], "user-n3") == 0 && s_tc.uvW[20] == 16 &&
              stats().installed == installed + 1,
          "a cached image is in place before any pump");
    CHECK(s_tc.levels[20] == 2 && stats().cacheBytes == s.cacheBytes,
          "the copy installed has its chain, the cache still none (%u levels)", s_tc.levels[20]);
    /* the cache keeps its copy: the texture loaded again gets it again */
    setCurrent(21, 4);
    CHECK(texpack_request(entryOf(N3), 21, 4, RDTEX_TEXA_REPLAY, 16, 16) == 2, "again");
    CHECK(s_tc.installed[21] != 0 && strcmp(s_tc.tag[21], "user-n3") == 0 && stats().cached == 6,
          "the cache kept it");
    /* a stale generation (the texture changed since) is not installed */
    setCurrent(23, 4);
    s_tc.gen[23] = 5;
    CHECK(texpack_request(entryOf(N3), 23, 4, RDTEX_TEXA_REPLAY, 16, 16) == -1 &&
              s_tc.installed[23] == 0,
          "cached, stale generation: dropped");
    CHECK(texpack_budget_used() == 2 * 512, "the two installed now are charged (%llu)",
          (unsigned long long)texpack_budget_used());
    texpack_shutdown();

    /* the RAM cache's own limit, 1 MB, apart from the graphics budget
       (none here): the precache passes over N5 (1.5 MB) and caches every
       other file; N5 loads on request */
    memset(&s_tc, 0, sizeof(s_tc));
    initPackCache(s_user, s_prog, 0, 1, 1);
    WAIT_FOR(stats().precacheDone);
    s = stats();
    CHECK(s.cacheLimit == 1u << 20 && texpack_budget_limit() == 0,
          "the cache's limit is its own (%llu), the budget none", (unsigned long long)s.cacheLimit);
    CHECK(s.cached == 5 && s.skipped == 1 && s.loadFailed == 1 && s.cacheBytes <= (1u << 20),
          "precache within 1 MB: %u files, %u skipped, %llu bytes", s.cached, s.skipped,
          (unsigned long long)s.cacheBytes);
    CHECK(!texpack_entry_cached(entryOf(N5)) && texpack_entry_cached(entryOf(N6)),
          "N5 left out, N6 (after it) cached");
    setCurrent(22, 1);
    CHECK(texpack_request(entryOf(N5), 22, 1, RDTEX_TEXA_REPLAY, 8, 8) == 0, "request (N5)");
    WAIT_FOR(s_tc.installed[22] != 0);
    CHECK(s_tc.installed[22] != 0 && strcmp(s_tc.tag[22], "user-n5-big") == 0 &&
              s_tc.levels[22] == 2 && stats().cached == 5,
          "past the limit: loaded on request, with its chain, not cached");
    /* low memory lets go of the cached images (no copy is
       being made of any) and holds the cache at what is left */
    const uint64_t held = texpack_low_memory();
    s = stats();
    CHECK(held == 0 && s.cacheBytes == 0 && s.cacheLimit == 0 && s.cached == 0,
          "low memory: the cache let go of everything (%llu bytes held, limit %llu, %u files)",
          (unsigned long long)held, (unsigned long long)s.cacheLimit, s.cached);
    CHECK(!texpack_entry_cached(entryOf(N6)), "N6 no longer cached");
    CHECK(texpack_low_memory() == held && stats().cacheLimit == held, "again: the same");
    /* a dropped file loads from disk on request and is not kept */
    setCurrent(24, 1);
    CHECK(texpack_request(entryOf(N6), 24, 1, RDTEX_TEXA_REPLAY, 8, 8) == 0, "request (N6)");
    WAIT_FOR(s_tc.installed[24] != 0);
    CHECK(s_tc.installed[24] != 0 && stats().cached == 0, "after low memory: loaded, not kept");
    texpack_shutdown();
}

/* The precache's order and its limit on a pack of its own: three DDS
   files in the standard layout (1.5 MB, 1.5 MB, 256 bytes) and a PNG in
   the tolerant layout, so listed after every DDS file; a 2 MB cache.  The
   PNG is read first, the second large DDS file does not fit and is passed
   over, and the small one after it is still cached. */
static void testPrecacheOrder(const char *dir)
{
    char root[1024];
    snprintf(root, sizeof(root), "%s/texpack_order", dir);
    (void)ico_mkdir(root);
    put(root, "textures/SCES-50760/replacements/a/" N1 ".dds", "IMG:dds-a-big");
    put(root, "textures/SCES-50760/replacements/b/" N3 ".dds", "IMG:dds-b-big");
    put(root, "textures/SCES-50760/replacements/c/" N4 ".dds", "IMG:dds-c");
    put(root, "textures/replacements/" N2 ".png", "IMG:png-p");
    memset(&s_tc, 0, sizeof(s_tc));
    initPackCache(root, NULL, 0, 1, 2);
    WAIT_FOR(stats().precacheDone);
    TexpackStats s = stats();
    CHECK(texpack_count() == 4, "4 replacements (%d)", texpack_count());
    CHECK(texpack_entry_cached(entryOf(N2)), "the PNG listed after the DDS files is cached");
    CHECK(texpack_entry_cached(entryOf(N1)) && !texpack_entry_cached(entryOf(N3)) &&
              texpack_entry_cached(entryOf(N4)),
          "the DDS file that does not fit passed over, the next one cached");
    CHECK(s.cached == 3 && s.skipped == 1 && s.cacheBytes == BIG_BYTES + 2 * 256,
          "3 cached, 1 skipped, %llu bytes", (unsigned long long)s.cacheBytes);
    texpack_shutdown();
}

/* A folder link inside a pack that points back up is not followed: the
   walk sees the pack's file once and ends. */
static void testLinks(const char *dir)
{
#ifdef _WIN32
    (void)dir;
#else
    char root[1024], link[1200];
    snprintf(root, sizeof(root), "%s/texpack_links", dir);
    (void)ico_mkdir(root);
    put(root, "textures/SCES-50760/replacements/a/" N1 ".png", "IMG:one");
    snprintf(link, sizeof(link), "%s/textures/SCES-50760/replacements/a/up", root);
    (void)remove(link);
    if (symlink("..", link) != 0) {
        printf("texpack_test: cannot make a folder link; links not tested\n");
        return;
    }
    snprintf(link, sizeof(link), "%s/textures/SCES-50760/replacements/a/up2", root);
    (void)remove(link);
    if (symlink("..", link) != 0) {
        printf("texpack_test: cannot make the second folder link; links not tested\n");
        return;
    }
    initPack(root, NULL, 0, 0);
    TexpackStats s = stats();
    CHECK(texpack_count() == 1 && s.files == 1 && s.duplicates == 0,
          "folder links not followed: 1 file seen (%u, %u duplicates)", s.files, s.duplicates);
    texpack_shutdown();
#endif
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    buildTree(dir);
    testIndex();
    testLoader();
    testPrecache();
    testPrecacheOrder(dir);
    testLinks(dir);
    if (failures) {
        printf("texpack_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("texpack_test: ok\n");
    return 0;
}

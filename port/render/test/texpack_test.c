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
 *           declining and remembering, the RAM cache installing at the
 *           next pump, the precache stopping at the memory limit, a device
 *           reset dropping the queue, shutdown joining a busy thread.
 *
 * The texture cache (rd_tex.h), the PNG writer and the file loaders are
 * fakes here, so nothing needs a device; until texpack_name.c is in the
 * tree (ICO_TEXPACK_HAVE_NAMES) the name parser is a local one.
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

int texpack_LoadPng(const uint8_t *data, size_t size, const char *file, TexpackImage *out)
{
    (void)file;
    return fakeLoad(data, size, out);
}

int texpack_LoadDds(const uint8_t *data, size_t size, int bcSupported, const char *file,
                    TexpackImage *out)
{
    (void)bcSupported;
    (void)file;
    return fakeLoad(data, size, out);
}

void texpack_FreeImage(TexpackImage *img)
{
    if (img) {
        free(img->blob);
        memset(img, 0, sizeof(*img));
    }
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
    uint32_t nextRep;
    int destroyed;
    char lastTag[64];
} s_tc;

static void setCurrent(uint32_t id, uint32_t gen)
{
    s_tc.current[id] = 1;
    s_tc.gen[id] = gen;
}

RdTex rdtex_Find(uint32_t id, uint32_t gen, int texa)
{
    (void)texa;
    return id < IDS && s_tc.current[id] && s_tc.gen[id] == gen ? (RdTex){1000 + id} : (RdTex){0};
}

RdTex rdtex_CreateReplacement(struct TexpackImage *img, uint32_t uvW, uint32_t uvH,
                              const char *debugName)
{
    (void)debugName;
    s_lastUvW = uvW;
    s_lastUvH = uvH;
    if (!img || !img->blob) {
        return (RdTex){0};
    }
    snprintf(s_tc.lastTag, sizeof(s_tc.lastTag), "%s", (const char *)img->blob);
    texpack_FreeImage(img); /* moved into the texture */
    return (RdTex){++s_tc.nextRep};
}

int rdtex_Replace(uint32_t id, uint32_t gen, int texa, RdTex rep)
{
    (void)texa;
    if (id >= IDS || !s_tc.current[id] || s_tc.gen[id] != gen) {
        return -1;
    }
    s_tc.installed[id] = rep.id;
    snprintf(s_tc.tag[id], sizeof(s_tc.tag[id]), "%s", s_tc.lastTag);
    s_tc.uvW[id] = s_lastUvW;
    s_tc.uvH[id] = s_lastUvH;
    return 0;
}

int rdtex_ReplacementMips(struct TexpackImage *img)
{
    (void)img;
    return -1; /* the fake images stay one level */
}

static RdTexReleaseFn s_releaseHook;

void rdtex_SetReleaseHook(RdTexReleaseFn fn)
{
    s_releaseHook = fn;
}

void rd_DestroyTexture(RdTex t)
{
    (void)t;
    s_tc.destroyed++;
}

int rdtex_Decode(const RdTexImage *im, uint8_t *out, RdTexSrc *src)
{
    (void)im;
    (void)out;
    (void)src;
    return -1;
}

void rdtex_ApplyTexa(uint8_t *rgba, size_t n, RdTexSrc src, RdTexA mode)
{
    (void)rgba;
    (void)n;
    (void)src;
    (void)mode;
}

bool rd_WritePng(const char *path, const uint8_t *rgba, uint32_t w, uint32_t h, uint32_t pitch,
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

/* ------------------------------------------------- names (before T1) */

#if !ICO_TEXPACK_HAVE_NAMES
static int hexRun(const char **p, uint64_t *v, int maxDigits)
{
    int n = 0;
    *v = 0;
    for (;; n++) {
        char c = **p;
        int d = c >= '0' && c <= '9'   ? c - '0'
                : c >= 'a' && c <= 'f' ? c - 'a' + 10
                : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                       : -1;
        if (d < 0 || n == maxDigits) {
            break;
        }
        *v = *v << 4 | (uint64_t)d;
        (*p)++;
    }
    return n;
}

/* PCSX2's forms: hash-bits., hash-clut-bits., and either with -rWxH-
   before the bits; the bits at most eight digits and a '.' after */
int texpack_ParseName(const char *fileName, TexpackName *out)
{
    const char *p = fileName;
    uint64_t v[3], rw = 0, rh = 0;
    int n = 0, region = 0;

    memset(out, 0, sizeof(*out));
    if (strstr(fileName, "-mip")) {
        return -1;
    }
    for (;;) {
        if (*p == 'r' && n >= 1) {
            p++;
            if (!hexRun(&p, &rw, 8) || *p != 'x') {
                return -1;
            }
            p++;
            if (!hexRun(&p, &rh, 8) || *p != '-') {
                return -1;
            }
            p++;
            region = 1;
            continue;
        }
        if (n == 3 || !hexRun(&p, &v[n], n == 2 ? 8 : 16)) {
            return -1;
        }
        n++;
        if (*p == '-') {
            p++;
            continue;
        }
        break;
    }
    if (*p != '.' || n < 2) {
        return -1;
    }
    out->tex0Hash = v[0];
    out->hasClut = n == 3;
    out->clutHash = n == 3 ? v[1] : 0;
    out->bits = (uint32_t)v[n - 1] & ~(1u << 14);
    out->regionW = (uint32_t)rw;
    out->regionH = (uint32_t)rh;
    return region;
}

int texpack_Candidates(const TexpackSource *src, uint32_t boundLevel, TexpackName *out, int max)
{
    (void)src;
    (void)boundLevel;
    (void)out;
    (void)max;
    return -1;
}

int texpack_FormatName(const TexpackName *n, char *buf, size_t size)
{
    (void)n;
    (void)buf;
    (void)size;
    return -1;
}
#endif

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
    if (texpack_ParseName(file, &tn) != 0) {
        return -2;
    }
    return texpack_Lookup(&tn);
}

static void initPack(const char *user, const char *prog, uint32_t budgetMb, int precache)
{
    TexpackConfig c;
    memset(&c, 0, sizeof(c));
    c.userDir = user;
    c.programDir = prog;
    c.serial = "SCES-50760";
    c.budgetMb = budgetMb;
    c.precache = precache;
    c.bcSupported = 1;
    texpack_Init(&c);
}

static TexpackStats stats(void)
{
    TexpackStats s;
    texpack_GetStats(&s);
    return s;
}

/* pumps (as the game fiber does once a frame) until done says so, or
   about three seconds */
#define WAIT_FOR(cond)                                                                             \
    do {                                                                                           \
        for (int w_ = 0; w_ < 3000 && !(cond); w_++) {                                             \
            texpack_Pump();                                                                        \
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
    CHECK(texpack_Count() == 7, "7 replacements (N1..N6 and the direct one), got %d",
          texpack_Count());
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
    CHECK(texpack_ParseName("9999-8888-r40x20-00002513.png", &rn) == 1 && rn.regionW != 0 &&
              rn.regionH != 0 && texpack_Lookup(&rn) == -1,
          "a region name parses and never matches");
    TexpackName dn;
    CHECK(texpack_ParseName(ND ".png", &dn) == 0 && !dn.hasClut && texpack_Lookup(&dn) >= 0,
          "a direct name (no CLUT part) matches");
    TexpackName wrong = dn;
    wrong.bits ^= 1;
    CHECK(texpack_Lookup(&wrong) == -1, "other bits: no match");

    /* portable mode: the user folder is the program's, walked once */
    initPack(s_user, s_user, 0, 0);
    s = stats();
    CHECK(texpack_Count() == 5 && s.duplicates == 0,
          "one root once: 5 replacements, no duplicates (%d, %u)", texpack_Count(), s.duplicates);
    /* no pack anywhere */
    char none[1100];
    snprintf(none, sizeof(none), "%s/missing", s_prog);
    initPack(none, NULL, 0, 0);
    CHECK(texpack_Count() == 0 && texpack_Lookup(&dn) == -1, "no pack: nothing indexed");
    CHECK(texpack_Request(0, 1, 1, RDTEX_TEXA_REPLAY, 1, 1) == -1, "no pack: requests refused");
    texpack_Pump();
    texpack_Shutdown();
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
    CHECK(texpack_Request(e1, 5, 40, RDTEX_TEXA_REPLAY, 64, 32) == 0, "request queued");
    CHECK(texpack_Request(e1, 5, 40, RDTEX_TEXA_REPLAY, 64, 32) == 1, "the same: already queued");
    WAIT_FOR(s_tc.installed[5] != 0);
    CHECK(s_tc.installed[5] != 0 && strcmp(s_tc.tag[5], "user-n1") == 0,
          "installed from the user folder's file (%s)", s_tc.tag[5]);
    CHECK(s_tc.uvW[5] == 64 && s_tc.uvH[5] == 32, "the GS size passed on (%ux%u)", s_tc.uvW[5],
          s_tc.uvH[5]);

    /* the texture changed while the file loaded: dropped, never created */
    setCurrent(6, 48);
    CHECK(texpack_Request(e3, 6, 48, RDTEX_TEXA_REPLAY, 64, 64) == 0, "request (gen 48)");
    s_tc.gen[6] = 56; /* a CLUT scroll re-stored it */
    WAIT_FOR(stats().discarded == 1);
    CHECK(stats().discarded == 1 && s_tc.installed[6] == 0 && s_tc.destroyed == 0,
          "a stale generation dropped before a texture was made");

    /* a file that does not load: remembered */
    setCurrent(7, 8);
    CHECK(texpack_Request(e4, 7, 8, RDTEX_TEXA_REPLAY, 8, 8) == 0, "request (a bad file)");
    WAIT_FOR(stats().loadFailed == 1);
    CHECK(stats().loadFailed == 1 && s_tc.installed[7] == 0, "the bad file failed");
    CHECK(texpack_Request(e4, 7, 9, RDTEX_TEXA_REPLAY, 8, 8) == -1, "and is not tried again");

    /* a device reset forgets the queue: N2 is slow, N6 waits behind it */
    setCurrent(30, 1);
    setCurrent(31, 1);
    CHECK(texpack_Request(e2, 30, 1, RDTEX_TEXA_REPLAY, 8, 8) == 0 &&
              texpack_Request(e6, 31, 1, RDTEX_TEXA_REPLAY, 8, 8) == 0,
          "two requests");
    texpack_ResetDevice();
    for (int i = 0; i < 100; i++) {
        texpack_Pump();
        sleepMs(1);
    }
    CHECK(s_tc.installed[31] == 0, "the queued request was dropped by the reset");
    texpack_Shutdown();

    /* the budget: 1 MB; N5 is 1.5 MB */
    memset(&s_tc, 0, sizeof(s_tc));
    initPack(s_user, s_prog, 1, 0);
    CHECK(texpack_BudgetLimit() == 1u << 20, "budget 1 MB");
    setCurrent(8, 16);
    CHECK(texpack_Request(e5, 8, 16, RDTEX_TEXA_REPLAY, 8, 8) == 0, "request (over budget)");
    WAIT_FOR(stats().declined == 1);
    CHECK(stats().declined == 1 && s_tc.installed[8] == 0 && texpack_BudgetUsed() == 0,
          "declined, nothing charged");
    CHECK(texpack_Request(e5, 8, 16, RDTEX_TEXA_REPLAY, 8, 8) == -1, "declined: remembered");
    setCurrent(9, 1);
    CHECK(texpack_Request(e1, 9, 1, RDTEX_TEXA_REPLAY, 8, 8) == 0, "request (fits)");
    WAIT_FOR(s_tc.installed[9] != 0);
    CHECK(s_tc.installed[9] != 0 && texpack_BudgetUsed() == 256, "charged 256 bytes (%llu)",
          (unsigned long long)texpack_BudgetUsed());
    CHECK(s_releaseHook == texpack_BudgetRelease, "the cache's release hook is the budget's");
    s_releaseHook((RdTex){s_tc.installed[9]}); /* the cache gives it up */
    CHECK(texpack_BudgetUsed() == 0, "released with its texture");
    texpack_BudgetRelease((RdTex){12345});
    CHECK(texpack_BudgetUsed() == 0, "releasing a texture never charged: nothing");
    texpack_Shutdown();

    /* shutdown joins a busy thread: ten slow loads queued */
    initPack(s_user, s_prog, 0, 0);
    for (uint32_t id = 10; id < 20; id++) {
        setCurrent(id, 1);
        (void)texpack_Request(e2, id, 1, RDTEX_TEXA_REPLAY, 8, 8);
    }
    sleepMs(5);
    texpack_Shutdown();
    CHECK(activeNow() == 0, "no load running after shutdown");
    CHECK(s_releaseHook == NULL, "shutdown takes the release hook back");
    CHECK(texpack_Count() == 0, "shutdown forgets the index");
}

static void testPrecache(void)
{
    /* everything read ahead: a request goes in at the next pump */
    memset(&s_tc, 0, sizeof(s_tc));
    initPack(s_user, s_prog, 0, 1);
    WAIT_FOR(stats().cached + stats().loadFailed == 7);
    TexpackStats s = stats();
    CHECK(s.cached == 6 && s.loadFailed == 1, "precache: 6 cached, the bad file failed (%u, %u)",
          s.cached, s.loadFailed);
    setCurrent(20, 4);
    CHECK(texpack_Request(entryOf(N3), 20, 4, RDTEX_TEXA_REPLAY, 16, 16) == 0, "request (cached)");
    texpack_Pump();
    CHECK(s_tc.installed[20] != 0 && strcmp(s_tc.tag[20], "user-n3") == 0,
          "a cached image goes in at the next pump");
    /* the cache keeps its copy: the texture loaded again gets it again */
    setCurrent(21, 4);
    CHECK(texpack_Request(entryOf(N3), 21, 4, RDTEX_TEXA_REPLAY, 16, 16) == 0, "again");
    texpack_Pump();
    CHECK(s_tc.installed[21] != 0, "the cache kept it");
    texpack_Shutdown();

    /* the memory limit: 1 MB; the precache stops at N5 (1.5 MB), and what
       is left loads on request */
    memset(&s_tc, 0, sizeof(s_tc));
    initPack(s_user, s_prog, 1, 1);
    for (int i = 0; i < 300; i++) {
        texpack_Pump();
        sleepMs(1);
    }
    s = stats();
    CHECK(s.cached < 6 && s.cacheBytes <= (1u << 20), "precache within 1 MB: %u files, %llu bytes",
          s.cached, (unsigned long long)s.cacheBytes);
    setCurrent(22, 1);
    CHECK(texpack_Request(entryOf(N6), 22, 1, RDTEX_TEXA_REPLAY, 8, 8) == 0, "request (N6)");
    WAIT_FOR(s_tc.installed[22] != 0);
    CHECK(s_tc.installed[22] != 0 && strcmp(s_tc.tag[22], "prog-n6") == 0,
          "past the limit: loaded on request");
    texpack_Shutdown();
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    buildTree(dir);
    testIndex();
    testLoader();
    testPrecache();
    if (failures) {
        printf("texpack_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("texpack_test: ok\n");
    return 0;
}

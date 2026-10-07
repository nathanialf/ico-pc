/* texpack.c: PCSX2 texture-replacement packs: the folders, the index, the
 * loader thread, the RAM cache, the pump and the replacement budget
 * (texpack.h).  The names come from texpack_name.c, the files' texels from
 * texpack_png.c / texpack_dds.c, and the texture cache (rd_tex.c) puts a
 * loaded image in place of the game's texture.
 *
 * Threads: everything here runs on the game fiber except the loader
 * thread, which touches only the request queue, the done list, the RAM
 * cache's entries (under s_tp.lock) and the files.  A cached image is
 * written once by the thread and read-only after (state CACHED), so the
 * game fiber copies it without holding the lock.  Without SDL (a build
 * with no window library) the same work runs inside texpack_Pump, one
 * request a call.
 */
#include "texpack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_fs.h"
#include "rd_internal.h"
#include "rd_tex.h"

#if ICO_TEXPACK_THREAD
#include <SDL3/SDL.h>
#endif

/* how deep under a replacements folder the walk goes (packs sort files
   into a few levels of folders; this bounds a loop of links) */
#define TEXPACK_WALK_DEPTH 16
/* (texId, gen) pairs the budget declined, remembered so the hook does not
   queue them again */
#define TEXPACK_DECLINED_MAX 256

enum { ENTRY_NONE = 0, ENTRY_CACHED = 1, ENTRY_FAILED = 2 };

typedef struct PackEntry {
    uint64_t tex0Hash, clutHash;
    uint32_t bits;
    uint8_t kind;   /* TexpackKind */
    uint8_t region; /* a region name: kept, never in the table */
    uint8_t state;  /* ENTRY_*, under the lock */
    char *path;
    TexpackImage cache; /* ENTRY_CACHED: the image as loaded */
} PackEntry;

typedef struct PackReq {
    struct PackReq *next;
    int entry;
    uint32_t texId, gen;
    int texa;
    uint32_t uvW, uvH;
    int failed;
    TexpackImage img;
} PackReq;

typedef struct BudgetCharge {
    uint32_t id;
    uint64_t bytes;
} BudgetCharge;

static struct {
    int inited;
    char userDir[1024];
    char serial[32];
    int bc;
    int developer;
    int precache;

    PackEntry *e;
    int n, cap;
    int *table; /* open addressing: entry index + 1, 0 empty */
    uint32_t tableMask;
    int count;

    TexpackStats stats;

    /* loader */
    PackReq *reqHead, *reqTail;   /* for the thread, oldest first */
    PackReq *doneHead, *doneTail; /* for the pump */
    PackReq *busy;                /* the request the thread is loading */
    int precacheNext;             /* the next entry the precache reads */
    int precacheDone;
    int quit;
    uint64_t cacheLimit;
    uint64_t precacheStart;
#if ICO_TEXPACK_THREAD
    SDL_Mutex *lock;
    SDL_Condition *wake;
    SDL_Thread *thread;
#endif

    /* budget (game fiber) */
    uint64_t budgetLimit, budgetUsed;
    BudgetCharge *charges;
    int nCharges, capCharges;
    int budgetLogged;

    struct {
        uint32_t texId, gen;
    } declined[TEXPACK_DECLINED_MAX];

    int nDeclined, declinedNext;
    int createLogged;

    /* dumps */
    int dumpDirReady;
    char dumpDir[1100];
    uint32_t dumped;
} s_tp;

/* ------------------------------------------------------------ locking */

static uint64_t nowMs(void)
{
#if ICO_TEXPACK_THREAD
    return SDL_GetTicks();
#else
    return 0;
#endif
}

static void lockTp(void)
{
#if ICO_TEXPACK_THREAD
    if (s_tp.lock) {
        SDL_LockMutex(s_tp.lock);
    }
#endif
}

static void unlockTp(void)
{
#if ICO_TEXPACK_THREAD
    if (s_tp.lock) {
        SDL_UnlockMutex(s_tp.lock);
    }
#endif
}

static void wakeTp(void)
{
#if ICO_TEXPACK_THREAD
    if (s_tp.wake) {
        SDL_SignalCondition(s_tp.wake);
    }
#endif
}

/* -------------------------------------------------------------- index */

static uint32_t keyHash(uint64_t tex0, uint64_t clut, uint32_t bits)
{
    uint64_t h = tex0 ^ (clut * 0x9E3779B97F4A7C15ull) ^ ((uint64_t)bits << 17);
    h ^= h >> 30;
    h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 27;
    h *= 0x94D049BB133111EBull;
    h ^= h >> 31;
    return (uint32_t)h;
}

static int findKey(uint64_t tex0, uint64_t clut, uint32_t bits)
{
    if (!s_tp.table) {
        return -1;
    }
    for (uint32_t i = keyHash(tex0, clut, bits) & s_tp.tableMask;; i = (i + 1) & s_tp.tableMask) {
        int v = s_tp.table[i];
        if (v == 0) {
            return -1;
        }
        const PackEntry *e = &s_tp.e[v - 1];
        if (e->tex0Hash == tex0 && e->clutHash == clut && e->bits == bits) {
            return v - 1;
        }
    }
}

static int growTable(void)
{
    uint32_t size = 64;
    while (size < (uint32_t)s_tp.count * 2 + 2) {
        size *= 2;
    }
    if (s_tp.table && size <= s_tp.tableMask + 1) {
        return 0;
    }
    int *t = calloc(size, sizeof(*t));
    if (!t) {
        return -1;
    }
    free(s_tp.table);
    s_tp.table = t;
    s_tp.tableMask = size - 1;
    for (int k = 0; k < s_tp.n; k++) {
        const PackEntry *e = &s_tp.e[k];
        if (e->region) {
            continue;
        }
        uint32_t i = keyHash(e->tex0Hash, e->clutHash, e->bits) & s_tp.tableMask;
        while (t[i] != 0) {
            i = (i + 1) & s_tp.tableMask;
        }
        t[i] = k + 1;
    }
    return 0;
}

/* "png" / "dds" in any case after the last '.', else -1 */
static int fileKind(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot || strlen(dot) != 4) {
        return -1;
    }
    char ext[4];
    for (int i = 0; i < 3; i++) {
        char c = dot[1 + i];
        ext[i] = (char)(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    ext[3] = 0;
    if (strcmp(ext, "png") == 0) {
        return TEXPACK_KIND_PNG;
    }
    if (strcmp(ext, "dds") == 0) {
        return TEXPACK_KIND_DDS;
    }
    return -1;
}

/* a level file of another: the base name ends in "-mip<digits>" */
static int isMipFile(const char *name)
{
    const char *dot = strrchr(name, '.');
    const char *end = dot ? dot : name + strlen(name);
    const char *p = end;
    while (p > name && p[-1] >= '0' && p[-1] <= '9') {
        p--;
    }
    return p < end && p - name >= 4 && memcmp(p - 4, "-mip", 4) == 0;
}

typedef struct WalkCtx {
    uint32_t added;
} WalkCtx;

static int addFile(const char *path, const char *name, void *user)
{
    WalkCtx *w = (WalkCtx *)user;
    int kind = fileKind(name);
    TexpackName tn;

    if (kind < 0) {
        return 0; /* a readme, an archive, a picture of the author's: not ours */
    }
    s_tp.stats.files++;
    if (isMipFile(name)) {
        s_tp.stats.mipFiles++; /* read beside its base file, never indexed */
        return 0;
    }
    int r = texpack_ParseName(name, &tn);
    if (r < 0) {
        s_tp.stats.malformed++;
        return 0;
    }
    if (r == 0 && findKey(tn.tex0Hash, tn.clutHash, tn.bits) >= 0) {
        s_tp.stats.duplicates++;
        return 0;
    }
    if (s_tp.n == s_tp.cap) {
        int ncap = s_tp.cap ? s_tp.cap * 2 : 256;
        PackEntry *g = realloc(s_tp.e, (size_t)ncap * sizeof(*g));
        if (!g) {
            return 1;
        }
        s_tp.e = g;
        s_tp.cap = ncap;
    }
    PackEntry *e = &s_tp.e[s_tp.n];
    memset(e, 0, sizeof(*e));
    e->path = malloc(strlen(path) + 1);
    if (!e->path) {
        return 1;
    }
    strcpy(e->path, path);
    e->tex0Hash = tn.tex0Hash;
    e->clutHash = tn.clutHash;
    e->bits = tn.bits;
    e->kind = (uint8_t)kind;
    e->region = r == 1;
    s_tp.n++;
    if (e->region) {
        s_tp.stats.regions++;
        return 0;
    }
    s_tp.count++;
    if (growTable() != 0) {
        s_tp.count--;
        s_tp.n--;
        free(e->path);
        return 1;
    }
    /* growTable rebuilt the table with this entry when it grew; otherwise
       put it in */
    if (findKey(e->tex0Hash, e->clutHash, e->bits) < 0) {
        uint32_t i = keyHash(e->tex0Hash, e->clutHash, e->bits) & s_tp.tableMask;
        while (s_tp.table[i] != 0) {
            i = (i + 1) & s_tp.tableMask;
        }
        s_tp.table[i] = s_tp.n;
    }
    w->added++;
    return 0;
}

/* a/b; 0, or -1 when it does not fit */
static int joinPath(char *out, size_t size, const char *a, const char *b)
{
    size_t la = strlen(a);
    const char *sep = la > 0 && a[la - 1] != '/' && a[la - 1] != '\\' ? "/" : "";
    int n = snprintf(out, size, "%s%s%s", a, sep, b);
    return n < 0 || (size_t)n >= size ? -1 : 0;
}

/* walks one folder (depth levels down) into the index and logs what it
   added */
static void walkFolder(const char *dir, int depth)
{
    WalkCtx w = {0};
    if (ico_dir_walk(dir, depth, addFile, &w) < 0) {
        return;
    }
    if (w.added > 0) {
        fprintf(stderr, "textures: %u replacements from %s\n", w.added, dir);
    }
}

/* ----------------------------------------------------------- loading */

static int readFile(const char *path, uint8_t **data, size_t *size)
{
    FILE *f = ico_fopen(path, "rb");
    *data = NULL;
    *size = 0;
    if (!f) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long n = ftell(f);
    if (n <= 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    uint8_t *p = malloc((size_t)n);
    if (!p || fread(p, 1, (size_t)n, f) != (size_t)n) {
        free(p);
        fclose(f);
        return -1;
    }
    fclose(f);
    *data = p;
    *size = (size_t)n;
    return 0;
}

/* the entry's file into img (the loaders log why a file is refused) */
static int loadEntry(const PackEntry *e, TexpackImage *img)
{
    uint8_t *data;
    size_t size;
    int r;

    memset(img, 0, sizeof(*img));
    if (readFile(e->path, &data, &size) != 0) {
        fprintf(stderr, "textures: cannot read %s\n", e->path);
        return -1;
    }
    if (e->kind == TEXPACK_KIND_DDS) {
        r = texpack_LoadDds(data, size, s_tp.bc, e->path, img);
    } else {
        r = texpack_LoadPng(data, size, e->path, img);
    }
    free(data);
    /* an RGBA8 file without its own mips gets the box chain here, on the
       loader thread, so the game fiber's rdtex_CreateReplacement does not
       build it (a failure leaves one level, which it then tries again) */
    if (r == 0 && img->fmt == RD_TEXEL_RGBA8 && img->levels == 1) {
        (void)rdtex_ReplacementMips(img);
    }
    return r;
}

/* a copy of src with its own blob */
static int copyImage(const TexpackImage *src, TexpackImage *dst)
{
    *dst = *src;
    dst->blob = malloc(src->bytes ? src->bytes : 1);
    if (!dst->blob) {
        memset(dst, 0, sizeof(*dst));
        return -1;
    }
    memcpy(dst->blob, src->blob, src->bytes);
    for (uint32_t i = 0; i < src->levels && i < TEXPACK_IMAGE_LEVELS; i++) {
        if (src->lv[i].data) {
            dst->lv[i].data =
                (const uint8_t *)dst->blob + (src->lv[i].data - (const uint8_t *)src->blob);
        }
    }
    return 0;
}

static void pushDone(PackReq *r)
{
    r->next = NULL;
    if (s_tp.doneTail) {
        s_tp.doneTail->next = r;
    } else {
        s_tp.doneHead = r;
    }
    s_tp.doneTail = r;
}

/* the RAM cache takes img when there is room (caller holds the lock):
   1 kept (img now the entry's), 0 not */
static int cacheImage(PackEntry *e, TexpackImage *img)
{
    if (e->state != ENTRY_NONE || s_tp.stats.cacheBytes + img->bytes > s_tp.cacheLimit) {
        return 0;
    }
    e->cache = *img;
    e->state = ENTRY_CACHED;
    s_tp.stats.cached++;
    s_tp.stats.cacheBytes += img->bytes;
    memset(img, 0, sizeof(*img));
    return 1;
}

/* One unit of the loader's work (caller holds the lock; it is released
   around the file reads): a request first, else one precache file.
   0 when there was nothing to do. */
static int loaderStep(void)
{
    PackReq *r = s_tp.reqHead;
    if (r) {
        s_tp.reqHead = r->next;
        if (!s_tp.reqHead) {
            s_tp.reqTail = NULL;
        }
        s_tp.busy = r;
        PackEntry *e = &s_tp.e[r->entry];
        int state = e->state;
        unlockTp();
        int ok;
        if (state == ENTRY_CACHED) {
            ok = copyImage(&e->cache, &r->img) == 0;
        } else {
            ok = state != ENTRY_FAILED && loadEntry(e, &r->img) == 0;
        }
        lockTp();
        s_tp.busy = NULL;
        if (!ok) {
            if (state != ENTRY_CACHED) {
                e->state = ENTRY_FAILED;
                s_tp.stats.loadFailed++;
            }
            r->failed = 1;
        } else if (state != ENTRY_CACHED && s_tp.precache) {
            /* keep it for the next time the game loads this texture: the
               request gets a copy */
            TexpackImage copy;
            if (copyImage(&r->img, &copy) == 0 && !cacheImage(e, &copy)) {
                texpack_FreeImage(&copy);
            }
        }
        pushDone(r);
        return 1;
    }
    if (!s_tp.precache || s_tp.precacheDone) {
        return 0;
    }
    while (s_tp.precacheNext < s_tp.n &&
           (s_tp.e[s_tp.precacheNext].region || s_tp.e[s_tp.precacheNext].state != ENTRY_NONE)) {
        s_tp.precacheNext++;
    }
    if (s_tp.precacheNext >= s_tp.n) {
        s_tp.precacheDone = 1;
        fprintf(stderr, "textures: %u replacements read into memory (%llu MB) in %.1f s\n",
                s_tp.stats.cached, (unsigned long long)(s_tp.stats.cacheBytes >> 20),
                (double)(nowMs() - s_tp.precacheStart) / 1000.0);
        return 0;
    }
    PackEntry *e = &s_tp.e[s_tp.precacheNext++];
    unlockTp();
    TexpackImage img;
    int ok = loadEntry(e, &img) == 0;
    lockTp();
    if (!ok) {
        e->state = ENTRY_FAILED;
        s_tp.stats.loadFailed++;
    } else if (!cacheImage(e, &img)) {
        /* the memory limit: what is left loads when the game asks */
        texpack_FreeImage(&img);
        s_tp.precacheDone = 1;
        fprintf(stderr,
                "textures: %u replacements read into memory (%llu MB, the limit); the rest "
                "load when the game shows them\n",
                s_tp.stats.cached, (unsigned long long)(s_tp.stats.cacheBytes >> 20));
    }
    return 1;
}

#if ICO_TEXPACK_THREAD
static int SDLCALL loaderThread(void *arg)
{
    (void)arg;
    lockTp();
    while (!s_tp.quit) {
        if (!loaderStep()) {
            SDL_WaitCondition(s_tp.wake, s_tp.lock);
        }
    }
    unlockTp();
    return 0;
}
#endif

/* ------------------------------------------------------------- public */

void texpack_GetStats(TexpackStats *out)
{
    lockTp();
    *out = s_tp.stats;
    unlockTp();
}

int texpack_Init(const TexpackConfig *cfg)
{
    char dirs[6][1100];
    char std[2][1100];
    int nStd = 0;

    texpack_Shutdown();
    memset(&s_tp, 0, sizeof(s_tp));
    if (!cfg) {
        return 0;
    }
    snprintf(s_tp.serial, sizeof(s_tp.serial), "%s", cfg->serial ? cfg->serial : "SCES-50760");
    snprintf(s_tp.userDir, sizeof(s_tp.userDir), "%s", cfg->userDir ? cfg->userDir : "");
    s_tp.bc = cfg->bcSupported != 0;
    s_tp.developer = cfg->developer != 0;
    s_tp.inited = 1;
    texpack_BudgetSet((uint64_t)cfg->budgetMb << 20);
    /* the cache tells the budget when it gives a replacement up */
    rdtex_SetReleaseHook(texpack_BudgetRelease);

    /* the folders, in order: the standard layout of the user folder, then
       of the program's folder, then the tolerant layouts (a pack copied one
       level too high or without its serial folder) of both; a root given
       twice (portable mode: the user folder is the program's) once */
    const char *roots[2] = {cfg->userDir, cfg->programDir};
    int nRoots = 0;
    const char *uniq[2];
    for (int i = 0; i < 2; i++) {
        if (roots[i] && roots[i][0] && !(nRoots == 1 && strcmp(uniq[0], roots[i]) == 0)) {
            uniq[nRoots++] = roots[i];
        }
    }
    char tail[64];
    snprintf(tail, sizeof(tail), "textures/%s/replacements", s_tp.serial);
    for (int i = 0; i < nRoots; i++) {
        if (joinPath(std[nStd], sizeof(std[0]), uniq[i], tail) == 0) {
            walkFolder(std[nStd], TEXPACK_WALK_DEPTH);
            nStd++;
        }
    }
    for (int i = 0; i < nRoots; i++) {
        if (joinPath(dirs[0], sizeof(dirs[0]), uniq[i], "textures/replacements") == 0) {
            walkFolder(dirs[0], TEXPACK_WALK_DEPTH);
        }
        /* files directly under textures/ only: below it are the serial
           folders (walked above) and the dumps */
        if (joinPath(dirs[1], sizeof(dirs[1]), uniq[i], "textures") == 0) {
            walkFolder(dirs[1], 0);
        }
    }
    s_tp.stats.indexed = (uint32_t)s_tp.count;
    if (s_tp.count == 0) {
        fprintf(stderr, "textures: no texture pack (looked in %s%s%s)\n",
                nStd > 0 ? std[0] : "no folder", nStd > 1 ? ", " : "", nStd > 1 ? std[1] : "");
    }
    if (s_tp.stats.duplicates || s_tp.stats.regions || s_tp.stats.mipFiles ||
        s_tp.stats.malformed) {
        fprintf(stderr,
                "textures: skipped %u files named like another, %u region textures, %u extra "
                "mip levels, %u files whose names are not texture names\n",
                s_tp.stats.duplicates, s_tp.stats.regions, s_tp.stats.mipFiles,
                s_tp.stats.malformed);
    }
    if (s_tp.count == 0) {
        return 0;
    }
    if (!s_tp.bc) {
        fprintf(stderr, "textures: the graphics card has no compressed (BC) textures; the pack's "
                        "DDS files of that kind are skipped\n");
    }
    s_tp.precache = cfg->precache != 0;
    s_tp.cacheLimit = s_tp.budgetLimit ? s_tp.budgetLimit : UINT64_MAX;
    s_tp.precacheStart = nowMs();
#if ICO_TEXPACK_THREAD
    s_tp.lock = SDL_CreateMutex();
    s_tp.wake = SDL_CreateCondition();
    if (s_tp.lock && s_tp.wake) {
        s_tp.thread = SDL_CreateThread(loaderThread, "texpack", NULL);
    }
    if (!s_tp.thread) {
        fprintf(stderr, "textures: no loader thread (%s); replacements load between frames\n",
                SDL_GetError());
        if (s_tp.wake) {
            SDL_DestroyCondition(s_tp.wake);
        }
        if (s_tp.lock) {
            SDL_DestroyMutex(s_tp.lock);
        }
        s_tp.wake = NULL;
        s_tp.lock = NULL;
        s_tp.precache = 0;
    }
#else
    s_tp.precache = 0; /* no thread to read ahead */
#endif
    return s_tp.count;
}

int texpack_Count(void)
{
    return s_tp.count;
}

int texpack_Lookup(const TexpackName *name)
{
    if (!name || s_tp.count == 0) {
        return -1;
    }
    return findKey(name->tex0Hash, name->hasClut ? name->clutHash : 0, name->bits);
}

const char *texpack_EntryPath(int entry)
{
    return entry >= 0 && entry < s_tp.n ? s_tp.e[entry].path : NULL;
}

static int wasDeclined(uint32_t texId, uint32_t gen)
{
    for (int i = 0; i < s_tp.nDeclined; i++) {
        if (s_tp.declined[i].texId == texId && s_tp.declined[i].gen == gen) {
            return 1;
        }
    }
    return 0;
}

static int inList(const PackReq *r, uint32_t texId, uint32_t gen, int texa)
{
    for (; r; r = r->next) {
        if (r->texId == texId && r->gen == gen && r->texa == texa) {
            return 1;
        }
    }
    return 0;
}

int texpack_Request(int entry, uint32_t texId, uint32_t gen, int texa, uint32_t uvW, uint32_t uvH)
{
    if (entry < 0 || entry >= s_tp.n || s_tp.e[entry].region) {
        return -1;
    }
    if (wasDeclined(texId, gen)) {
        return -1;
    }
    lockTp();
    PackEntry *e = &s_tp.e[entry];
    int state = e->state;
    int queued = inList(s_tp.reqHead, texId, gen, texa) ||
                 inList(s_tp.doneHead, texId, gen, texa) ||
                 (s_tp.busy && s_tp.busy->texId == texId && s_tp.busy->gen == gen &&
                  s_tp.busy->texa == texa);
    unlockTp();
    if (state == ENTRY_FAILED) {
        return -1;
    }
    if (queued) {
        return 1;
    }
    PackReq *r = calloc(1, sizeof(*r));
    if (!r) {
        return -1;
    }
    r->entry = entry;
    r->texId = texId;
    r->gen = gen;
    r->texa = texa;
    r->uvW = uvW;
    r->uvH = uvH;
    if (state == ENTRY_CACHED) {
        /* read ahead already: installed by the next pump (the cached image
           is never written again, so it is copied without the lock) */
        if (copyImage(&e->cache, &r->img) != 0) {
            free(r);
            return -1;
        }
        lockTp();
        pushDone(r);
        s_tp.stats.requested++;
        unlockTp();
        return 0;
    }
    lockTp();
    /* requests go before the precache, oldest first */
    if (s_tp.reqTail) {
        s_tp.reqTail->next = r;
    } else {
        s_tp.reqHead = r;
    }
    s_tp.reqTail = r;
    s_tp.stats.requested++;
    wakeTp();
    unlockTp();
    return 0;
}

static void remember(uint32_t texId, uint32_t gen)
{
    s_tp.declined[s_tp.declinedNext].texId = texId;
    s_tp.declined[s_tp.declinedNext].gen = gen;
    s_tp.declinedNext = (s_tp.declinedNext + 1) % TEXPACK_DECLINED_MAX;
    if (s_tp.nDeclined < TEXPACK_DECLINED_MAX) {
        s_tp.nDeclined++;
    }
}

/* a finished load in place of its texture, or dropped */
static void install(PackReq *r)
{
    const PackEntry *e = &s_tp.e[r->entry];
    uint64_t bytes = r->img.bytes;
    const char *name = strrchr(e->path, '/');
    name = name ? name + 1 : e->path;

    /* the texture changed (a CLUT scroll, another level) or was freed while
       the file loaded */
    if (rdtex_Find(r->texId, r->gen, r->texa).id == 0) {
        s_tp.stats.discarded++;
        return;
    }
    if (s_tp.budgetLimit && s_tp.budgetUsed + bytes > s_tp.budgetLimit) {
        s_tp.stats.declined++;
        remember(r->texId, r->gen);
        if (!s_tp.budgetLogged) {
            s_tp.budgetLogged = 1;
            fprintf(stderr,
                    "textures: the texture pack's memory limit (%llu MB, "
                    "texture_pack_budget_mb) is reached; textures past it stay the game's own\n",
                    (unsigned long long)(s_tp.budgetLimit >> 20));
        }
        return;
    }
    /* refused (no BC on this device, larger than it takes): nothing was
       charged or replaced, the game's own texture stays */
    RdTex rep = rdtex_CreateReplacement(&r->img, r->uvW, r->uvH, name);
    if (rep.id == 0) {
        s_tp.stats.discarded++;
        if (!s_tp.createLogged) {
            s_tp.createLogged = 1;
            fprintf(stderr, "textures: %s could not be made into a texture (reported once)\n",
                    name);
        }
        return;
    }
    if (texpack_BudgetCharge(rep, bytes) != 0) {
        /* over by the bookkeeping's own count: as declined */
        rd_DestroyTexture(rep);
        s_tp.stats.declined++;
        remember(r->texId, r->gen);
        return;
    }
    if (rdtex_Replace(r->texId, r->gen, r->texa, rep) != 0) {
        texpack_BudgetRelease(rep);
        rd_DestroyTexture(rep);
        s_tp.stats.discarded++;
        return;
    }
    s_tp.stats.installed++;
    if (s_tp.developer) {
        fprintf(stderr, "textures: texture %u replaced by %s\n", r->texId, name);
    }
}

void texpack_Pump(void)
{
    if (s_tp.count == 0) {
        return;
    }
    lockTp();
#if !ICO_TEXPACK_THREAD
    loaderStep();
#else
    if (!s_tp.thread) {
        loaderStep();
    }
#endif
    PackReq *r = s_tp.doneHead;
    s_tp.doneHead = s_tp.doneTail = NULL;
    unlockTp();
    while (r) {
        PackReq *next = r->next;
        if (!r->failed) {
            install(r);
        }
        texpack_FreeImage(&r->img);
        free(r);
        r = next;
    }
}

static void freeList(PackReq *r)
{
    while (r) {
        PackReq *next = r->next;
        texpack_FreeImage(&r->img);
        free(r);
        r = next;
    }
}

void texpack_ResetDevice(void)
{
    lockTp();
    freeList(s_tp.reqHead);
    freeList(s_tp.doneHead);
    s_tp.reqHead = s_tp.reqTail = NULL;
    s_tp.doneHead = s_tp.doneTail = NULL;
    unlockTp();
    /* the request the thread is loading lands on the done list and is
       dropped by the pump (its texture is gone) */
    free(s_tp.charges);
    s_tp.charges = NULL;
    s_tp.nCharges = s_tp.capCharges = 0;
    s_tp.budgetUsed = 0;
    s_tp.nDeclined = s_tp.declinedNext = 0;
}

void texpack_Shutdown(void)
{
    if (s_tp.inited) {
        rdtex_SetReleaseHook(NULL);
    }
#if ICO_TEXPACK_THREAD
    if (s_tp.thread) {
        lockTp();
        s_tp.quit = 1;
        wakeTp();
        unlockTp();
        SDL_WaitThread(s_tp.thread, NULL);
        s_tp.thread = NULL;
    }
    if (s_tp.wake) {
        SDL_DestroyCondition(s_tp.wake);
        s_tp.wake = NULL;
    }
    if (s_tp.lock) {
        SDL_DestroyMutex(s_tp.lock);
        s_tp.lock = NULL;
    }
#endif
    freeList(s_tp.reqHead);
    freeList(s_tp.doneHead);
    for (int i = 0; i < s_tp.n; i++) {
        if (s_tp.e[i].state == ENTRY_CACHED) {
            texpack_FreeImage(&s_tp.e[i].cache);
        }
        free(s_tp.e[i].path);
    }
    free(s_tp.e);
    free(s_tp.table);
    free(s_tp.charges);
    memset(&s_tp, 0, sizeof(s_tp));
}

/* ------------------------------------------------------------- budget */

void texpack_BudgetSet(uint64_t bytes)
{
    s_tp.budgetLimit = bytes;
}

uint64_t texpack_BudgetLimit(void)
{
    return s_tp.budgetLimit;
}

uint64_t texpack_BudgetUsed(void)
{
    return s_tp.budgetUsed;
}

int texpack_BudgetCharge(RdTex t, uint64_t bytes)
{
    if (t.id == 0) {
        return -1;
    }
    if (s_tp.budgetLimit && s_tp.budgetUsed + bytes > s_tp.budgetLimit) {
        if (!s_tp.budgetLogged) {
            s_tp.budgetLogged = 1;
            fprintf(stderr,
                    "textures: the texture pack's memory limit (%llu MB, "
                    "texture_pack_budget_mb) is reached; textures past it stay the game's own\n",
                    (unsigned long long)(s_tp.budgetLimit >> 20));
        }
        return -1;
    }
    for (int i = 0; i < s_tp.nCharges; i++) {
        if (s_tp.charges[i].id == t.id) {
            s_tp.budgetUsed += bytes;
            s_tp.charges[i].bytes += bytes;
            return 0;
        }
    }
    if (s_tp.nCharges == s_tp.capCharges) {
        int ncap = s_tp.capCharges ? s_tp.capCharges * 2 : 64;
        BudgetCharge *g = realloc(s_tp.charges, (size_t)ncap * sizeof(*g));
        if (!g) {
            return -1;
        }
        s_tp.charges = g;
        s_tp.capCharges = ncap;
    }
    s_tp.charges[s_tp.nCharges].id = t.id;
    s_tp.charges[s_tp.nCharges].bytes = bytes;
    s_tp.nCharges++;
    s_tp.budgetUsed += bytes;
    return 0;
}

void texpack_BudgetRelease(RdTex t)
{
    for (int i = 0; i < s_tp.nCharges; i++) {
        if (s_tp.charges[i].id == t.id) {
            s_tp.budgetUsed -= s_tp.charges[i].bytes;
            s_tp.charges[i] = s_tp.charges[--s_tp.nCharges];
            /* room again: a later replacement may fit */
            s_tp.nDeclined = s_tp.declinedNext = 0;
            return;
        }
    }
}

/* -------------------------------------------------------------- dumps */

static int ensureDumpDir(void)
{
    if (s_tp.dumpDirReady) {
        return s_tp.dumpDirReady > 0 ? 0 : -1;
    }
    char p[1100];
    s_tp.dumpDirReady = -1;
    if (!s_tp.inited || s_tp.userDir[0] == '\0' ||
        joinPath(p, sizeof(p), s_tp.userDir, "textures") != 0) {
        return -1;
    }
    (void)ico_mkdir(p);
    if (joinPath(s_tp.dumpDir, sizeof(s_tp.dumpDir), p, s_tp.serial) != 0) {
        return -1;
    }
    (void)ico_mkdir(s_tp.dumpDir);
    snprintf(p, sizeof(p), "%s", s_tp.dumpDir);
    if (joinPath(s_tp.dumpDir, sizeof(s_tp.dumpDir), p, "dumps") != 0) {
        return -1;
    }
    (void)ico_mkdir(s_tp.dumpDir);
    if (ico_path_kind(s_tp.dumpDir, NULL, NULL) != 1) {
        fprintf(stderr, "textures: cannot make %s; no texture dumps\n", s_tp.dumpDir);
        return -1;
    }
    fprintf(stderr, "textures: writing texture dumps to %s\n", s_tp.dumpDir);
    s_tp.dumpDirReady = 1;
    return 0;
}

int texpack_Dump(const TexpackSource *src, uint32_t boundLevel, const RdTexImage *im)
{
    static TexpackName names[TEXPACK_MAX_CANDIDATES];
    int written = 0;

    if (!src || !im || ensureDumpDir() != 0) {
        return 0;
    }
    int n = texpack_Candidates(src, boundLevel, names, TEXPACK_MAX_CANDIDATES);
    uint32_t w = im->padW ? im->padW : im->w;
    uint32_t h = im->padH ? im->padH : im->h;
    uint8_t *rgba = NULL, *texa = NULL;
    RdTexSrc fmt = RD_TEXSRC_RGBA32;

    for (int i = 0; i < n; i++) {
        const TexpackName *tn = &names[i];
        char base[TEXPACK_NAME_MAX], file[TEXPACK_NAME_MAX + 8], path[1200];

        /* the level as drawn, alone: what PCSX2 with mipmapping off names */
        if (tn->startLevel != boundLevel || tn->mipChain) {
            continue;
        }
        if (texpack_FormatName(tn, base, sizeof(base)) < 0) {
            continue;
        }
        snprintf(file, sizeof(file), "%s.png", base);
        if (joinPath(path, sizeof(path), s_tp.dumpDir, file) != 0 ||
            ico_path_kind(path, NULL, NULL) >= 0) {
            continue;
        }
        if (!rgba) {
            rgba = malloc((size_t)w * h * 4);
            if (!rgba || rdtex_Decode(im, rgba, &fmt) != 0) {
                break;
            }
        }
        const uint8_t *out = rgba;
        if (tn->texa != TEXPACK_TEXA_ANY) {
            if (!texa) {
                texa = malloc((size_t)w * h * 4);
                if (!texa) {
                    break;
                }
            }
            memcpy(texa, rgba, (size_t)w * h * 4);
            rdtex_ApplyTexa(texa, (size_t)w * h, fmt, (RdTexA)tn->texa);
            out = texa;
        }
        if (rd_WritePng(path, out, w, h, w * 4, 1)) {
            written++;
            s_tp.dumped++;
        }
    }
    free(rgba);
    free(texa);
    return written;
}

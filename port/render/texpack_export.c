/* texpack_export.c: every texture on the disc as PNG files (texpack_export.h).
 *
 * Per item, the rules of the runtime's hook and dump (Texture.c texHostPack
 * and texHostTexture, the file is Latin-1: grep -a; texpack.c texpack_dump):
 * a 24-bit or short CLUT names nothing; every level b the game can bind is
 * named by texpack_candidates(src, b), keeping the names whose startLevel
 * is b and that are no mip chain; level b is decoded from the RdTexImage
 * the hook builds (w, h the level's, padW x padH 2^TW x 2^TH, the CLUT in
 * CSM1 order, clutColors the format's 16 or 256) and each TEXA-dependent
 * name gets its own copy through rdtex_apply_texa.
 */
#include "texpack_export.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "host_fs.h"
#include "rd_png.h"
#include "rd_tex.h"
#include "texpack.h"

#if ICO_TEXPACK_EXPORT_THREAD
#include <SDL3/SDL.h>
#endif

#define PATH_MAX_ 1200
/* the failed writes logged: the first ones, then every LOG_EVERY-th */
#define LOG_FIRST 8
#define LOG_EVERY 100
#define PROGRESS_EVERY 1000

typedef struct NameKey {
    uint64_t tex0, clut;
    uint32_t bits;
    uint32_t used;
} NameKey;

typedef struct Buf {
    char *p;
    size_t len, cap;
} Buf;

struct TexpackExport {
    char dir[sizeof(((TexpackExportStatus *)0)->dir)];
    char serial[32];
    unsigned flags;
    TexpackExportStatus st;
    NameKey *keys; /* the names this run gave: open addressing */
    uint32_t keyCap, keyCount;
    Buf rows;             /* the manifest's rows */
    Buf names;            /* the current row's names */
    uint8_t *rgba, *texa; /* the decoded level and its TEXA copy */
    size_t pixCap;
};

/* ------------------------------------------------------------ helpers */

static void bufAdd(Buf *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0 || b->cap == (size_t)-1) {
        return;
    }
    if (b->len + (size_t)n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 4096;
        while (cap < b->len + (size_t)n + 1) {
            cap *= 2;
        }
        char *p = realloc(b->p, cap);
        if (p == NULL) {
            b->cap = (size_t)-1; /* out of memory: the rest is dropped */
            return;
        }
        b->p = p;
        b->cap = cap;
    }
    va_start(ap, fmt);
    vsnprintf(b->p + b->len, b->cap - b->len, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
}

static int joinPath(char *out, size_t size, const char *a, const char *b)
{
    size_t la = strlen(a);
    const char *sep = la > 0 && a[la - 1] != '/' && a[la - 1] != '\\' ? "/" : "";
    int n = snprintf(out, size, "%s%s%s", a, sep, b);
    return n < 0 || (size_t)n >= size ? -1 : 0;
}

static uint64_t keyHash(const TexpackName *n)
{
    uint64_t h = n->tex0Hash ^ (n->clutHash * 0x9E3779B97F4A7C15ull) ^ ((uint64_t)n->bits << 17);
    h ^= h >> 33;
    h *= 0xFF51AFD7ED558CCDull;
    h ^= h >> 33;
    return h;
}

/* the slot of n's key: its own, or the empty one where it goes */
static NameKey *keySlot(NameKey *keys, uint32_t cap, const TexpackName *n)
{
    uint32_t i = (uint32_t)keyHash(n) & (cap - 1);
    while (keys[i].used && (keys[i].tex0 != n->tex0Hash || keys[i].clut != n->clutHash ||
                            keys[i].bits != n->bits)) {
        i = (i + 1) & (cap - 1);
    }
    return &keys[i];
}

static int keySeen(const TexpackExport *x, const TexpackName *n)
{
    return x->keyCap != 0 && keySlot(x->keys, x->keyCap, n)->used;
}

static void keyAdd(TexpackExport *x, const TexpackName *n)
{
    if ((x->keyCount + 1) * 2 > x->keyCap) {
        uint32_t cap = x->keyCap ? x->keyCap * 2 : 4096;
        NameKey *keys = calloc(cap, sizeof(*keys));
        if (keys == NULL) {
            return; /* out of memory: a later duplicate is written again */
        }
        for (uint32_t i = 0; i < x->keyCap; i++) {
            if (x->keys[i].used) {
                TexpackName k;
                memset(&k, 0, sizeof(k));
                k.tex0Hash = x->keys[i].tex0;
                k.clutHash = x->keys[i].clut;
                k.bits = x->keys[i].bits;
                *keySlot(keys, cap, &k) = x->keys[i];
            }
        }
        free(x->keys);
        x->keys = keys;
        x->keyCap = cap;
    }
    NameKey *s = keySlot(x->keys, x->keyCap, n);
    if (!s->used) {
        s->tex0 = n->tex0Hash;
        s->clut = n->clutHash;
        s->bits = n->bits;
        s->used = 1;
        x->keyCount++;
    }
}

/* "SCES-50760" from ".../textures/SCES-50760/dumps" */
static void serialOfDir(const char *dir, char *out, size_t size)
{
    char tmp[sizeof(((TexpackExportStatus *)0)->dir)];
    snprintf(tmp, sizeof(tmp), "%s", dir);
    size_t n = strlen(tmp);
    while (n > 0 && (tmp[n - 1] == '/' || tmp[n - 1] == '\\')) {
        tmp[--n] = '\0';
    }
    while (n > 0 && tmp[n - 1] != '/' && tmp[n - 1] != '\\') {
        n--; /* drop "dumps" */
    }
    while (n > 0 && (tmp[n - 1] == '/' || tmp[n - 1] == '\\')) {
        tmp[--n] = '\0';
    }
    tmp[n] = '\0';
    size_t s = n;
    while (s > 0 && tmp[s - 1] != '/' && tmp[s - 1] != '\\') {
        s--;
    }
    snprintf(out, size, "%s", tmp[s] ? tmp + s : "unknown");
}

static const char *kindWord(int kind)
{
    return kind == TEXPACK_WALK_TM2 ? "tm2" : kind == TEXPACK_WALK_JIM_LOOSE ? "jim" : "jim-member";
}

/* ------------------------------------------------------------ the core */

TexpackExport *texpack_export_open(const char *dumpDir, const char *serial, unsigned flags)
{
    if (dumpDir == NULL || dumpDir[0] == '\0') {
        return NULL;
    }
    TexpackExport *x = calloc(1, sizeof(*x));
    if (x == NULL) {
        return NULL;
    }
    snprintf(x->dir, sizeof(x->dir), "%s", dumpDir);
    snprintf(x->serial, sizeof(x->serial), "%s", serial && serial[0] ? serial : "unknown");
    x->flags = flags;
    x->st.state = TEXPACK_EXPORT_RUNNING;
    snprintf(x->st.dir, sizeof(x->st.dir), "%s", dumpDir);
    return x;
}

void texpack_export_counts(const TexpackExport *x, TexpackExportStatus *out)
{
    if (x != NULL && out != NULL) {
        *out = x->st;
    }
}

/* the decode buffers for one level of padW x padH */
static int pixels(TexpackExport *x, size_t bytes)
{
    if (bytes <= x->pixCap) {
        return 0;
    }
    free(x->rgba);
    free(x->texa);
    x->rgba = malloc(bytes);
    x->texa = malloc(bytes);
    x->pixCap = x->rgba && x->texa ? bytes : 0;
    return x->pixCap ? 0 : -1;
}

/* <name>.png.tmp, then renamed over <name>.png: 0, or -1 with errno */
static int writePng(const char *path, const uint8_t *rgba, uint32_t w, uint32_t h)
{
    char tmp[PATH_MAX_ + 8];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    errno = 0;
    if (!rd_write_png(tmp, rgba, w, h, w * 4, 1)) {
        int e = errno ? errno : EIO;
        (void)ico_remove(tmp);
        errno = e;
        return -1;
    }
    if (ico_rename_replace(tmp, path) != 0) {
        int e = errno ? errno : EIO;
        (void)ico_remove(tmp);
        errno = e;
        return -1;
    }
    return 0;
}

static void writeFailed(TexpackExport *x, const char *path, int e)
{
    x->st.failed++;
    if (x->st.failed <= LOG_FIRST || x->st.failed % LOG_EVERY == 0) {
        fprintf(stderr, "textures: export cannot write %s: %s (errno %d, failed write %d)\n", path,
                e ? strerror(e) : "no decoder for the format", e, x->st.failed);
    }
}

/* the files level b of src gets; its tokens into x->names.  -1 when the
   names are refused */
static int doLevel(TexpackExport *x, const TexpackWalkItem *it, const TexpackSource *src,
                   uint32_t b, int *flags)
{
    TexpackName names[TEXPACK_MAX_CANDIDATES];
    int n = texpack_candidates(src, b, names, TEXPACK_MAX_CANDIDATES);
    int written = 0;
    int decoded = 0;
    RdTexSrc fmt = RD_TEXSRC_RGBA32;
    RdTexImage im;
    const TexpackLevel *lv = &src->lv[b];

    if (n < 0) {
        return -1;
    }
    /* texHostTexture's image of level b */
    memset(&im, 0, sizeof(im));
    im.w = it->w >> b;
    im.h = it->h >> b;
    im.padW = (1u << lv->tw) >= im.w ? 1u << lv->tw : im.w;
    im.padH = (1u << lv->th) >= im.h ? 1u << lv->th : im.h;
    im.psm = src->psm;
    im.pixels = lv->pixels;
    if (src->clut != NULL) {
        im.cpsm = src->cpsm;
        im.clutColors = src->clutColors;
        im.clutLinear = 0;
        im.clut = src->clut;
    }
    size_t count = (size_t)im.padW * im.padH;

    for (int i = 0; i < n; i++) {
        const TexpackName *tn = &names[i];
        char base[TEXPACK_NAME_MAX];
        char file[TEXPACK_NAME_MAX + 8];
        char path[PATH_MAX_];
        char mark;

        /* the level as drawn, alone: what PCSX2 with mipmapping off names */
        if (tn->startLevel != b || tn->mipChain) {
            continue;
        }
        /* two TEXA modes of one level can give one name (no entry the
           alpha rule tells apart): one file, one token, not a duplicate */
        int own = 0;
        for (int j = 0; j < i && !own; j++) {
            const TexpackName *o = &names[j];
            own = o->startLevel == b && !o->mipChain && o->tex0Hash == tn->tex0Hash &&
                  o->clutHash == tn->clutHash && o->bits == tn->bits;
        }
        if (own) {
            continue;
        }
        if (texpack_format_name(tn, base, sizeof(base)) < 0) {
            continue;
        }
        if (tn->unstable) {
            *flags |= 1; /* unstable */
        }
        snprintf(file, sizeof(file), "%s.png", base);
        if (keySeen(x, tn)) {
            x->st.duplicates++;
            *flags |= 2;
            mark = '~';
        } else if (joinPath(path, sizeof(path), x->dir, file) != 0) {
            writeFailed(x, file, ENAMETOOLONG);
            *flags |= 8;
            mark = '!';
        } else if (ico_path_kind(path, NULL, NULL) >= 0) {
            x->st.skipped++;
            keyAdd(x, tn);
            *flags |= 4;
            mark = '=';
        } else if (x->flags & TEXPACK_EXPORT_DRY) {
            x->st.written++;
            written++;
            keyAdd(x, tn);
            mark = '+';
        } else {
            int ok = 0;
            int e = 0;
            if (!decoded) {
                decoded =
                    pixels(x, count * 4) == 0 && rdtex_decode(&im, x->rgba, &fmt) == 0 ? 1 : -1;
            }
            if (decoded > 0) {
                const uint8_t *out = x->rgba;
                if (tn->texa != TEXPACK_TEXA_ANY) {
                    memcpy(x->texa, x->rgba, count * 4);
                    rdtex_apply_texa(x->texa, count, fmt, (RdTexA)tn->texa);
                    out = x->texa;
                }
                ok = writePng(path, out, im.padW, im.padH) == 0;
                e = ok ? 0 : errno;
            }
            if (ok) {
                x->st.written++;
                written++;
                keyAdd(x, tn);
                mark = '+';
            } else {
                writeFailed(x, path, e);
                *flags |= 8;
                mark = '!';
            }
        }
        bufAdd(&x->names, "%s%c%s@%u", x->names.len ? " " : "", mark, base, b);
    }
    return written;
}

int texpack_export_item(TexpackExport *x, const TexpackWalkItem *it)
{
    char clut[32];
    char why[32];
    int flags = 0; /* 1 unstable, 2 duplicate, 4 on-disk, 8 failed */
    int written = 0;
    int refused = 0;

    if (x == NULL || it == NULL) {
        return -1;
    }
    x->st.done++;
    if (it->kind == TEXPACK_WALK_TM2) {
        x->st.tim2++;
    } else {
        x->st.jimRecords++;
    }
    x->names.len = 0;
    why[0] = '\0';
    if (it->type == 4 || it->type == 5) {
        snprintf(clut, sizeof(clut), "%u/%uB", it->clutColors, it->clutSize);
    } else {
        snprintf(clut, sizeof(clut), "-");
    }

    switch (it->bad) {
    case TEXPACK_WALK_OK:
        break;
    case TEXPACK_WALK_NOT_TIM2:
        snprintf(why, sizeof(why), "not-tim2");
        break;
    case TEXPACK_WALK_BAD_HEADER:
        snprintf(why, sizeof(why), "header");
        break;
    case TEXPACK_WALK_BAD_LEVEL:
        snprintf(why, sizeof(why), "level-%u", it->badLevel);
        break;
    default:
        snprintf(why, sizeof(why), "unreadable");
        break;
    }
    if (it->bad == TEXPACK_WALK_OK) {
        x->st.scroll += it->scroll != 0;
        if (it->clut24) {
            x->st.refusedClut24++;
            refused = 1;
        } else if (it->shortClut) {
            x->st.refusedShortClut++;
            refused = 1;
        }
    }
    if (it->bad == TEXPACK_WALK_OK && !refused) {
        /* the hook's source: the CLUT as the format's 16 or 256 entries
           (texHostTexture's clutColors) */
        TexpackSource src = it->src;
        if (src.clut != NULL) {
            src.clutColors = it->type == 4 ? 16 : 256;
        }
        /* the levels texHostPack gives the names: up to the first whose
           GS size or image size would be below one texel */
        uint32_t valid = 0;
        while (valid < src.levels && (int)src.lv[0].tw - (int)valid >= 0 &&
               (int)src.lv[0].th - (int)valid >= 0 && (it->w >> valid) != 0 &&
               (it->h >> valid) != 0) {
            valid++;
        }
        src.levels = valid;
        for (uint32_t b = 0; b < src.levels; b++) {
            int w = doLevel(x, it, &src, b, &flags);
            if (w < 0) {
                if (b == 0) {
                    snprintf(why, sizeof(why), "no-names");
                }
                break;
            }
            written += w;
        }
    }
    if (why[0] != '\0') {
        x->st.bad++;
    }
    x->st.refused = x->st.refusedClut24 + x->st.refusedShortClut;

    /* the row */
    Buf f = {0};
    if (flags & 1) {
        bufAdd(&f, ",unstable");
    }
    if (it->bad == TEXPACK_WALK_OK && it->scroll) {
        bufAdd(&f, ",scroll");
    }
    if (it->bad == TEXPACK_WALK_OK && it->clut24) {
        bufAdd(&f, ",clut24");
    }
    if (it->bad == TEXPACK_WALK_OK && it->shortClut) {
        bufAdd(&f, ",short-clut");
    }
    if (flags & 2) {
        bufAdd(&f, ",duplicate");
    }
    if (flags & 4) {
        bufAdd(&f, ",on-disk");
    }
    if (why[0] != '\0') {
        bufAdd(&f, ",bad:%s", why);
    }
    if (flags & 8) {
        bufAdd(&f, ",failed");
    }
    bufAdd(&x->rows, "%s\t%s\t%s\t%zu\t%ux%u\t%u\t%s\t%s\t%s\n", it->member, kindWord(it->kind),
           it->bad == TEXPACK_WALK_NOT_TIM2 || it->bad == TEXPACK_WALK_UNREADABLE
               ? "-"
               : texpack_walk_type_name(it->type),
           it->bytes ? it->bytes : it->size, it->w, it->h, it->levels, clut, f.len ? f.p + 1 : "-",
           x->names.len ? x->names.p : "-");
    free(f.p);
    return why[0] != '\0' || refused ? -1 : written;
}

static void today(char *out, size_t size)
{
    time_t now = time(NULL);
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    if (strftime(out, size, "%Y-%m-%d", &tm) == 0) {
        snprintf(out, size, "unknown");
    }
}

/* the manifest's lines before the rows */
static void header(const TexpackExport *x, int stopped, Buf *h)
{
    const TexpackExportStatus *s = &x->st;
    char date[32];
    today(date, sizeof(date));
    if (stopped) {
        bufAdd(h, "# ICO texture export %s %s stopped after %d of %d%s\n", x->serial, date, s->done,
               s->total, x->flags & TEXPACK_EXPORT_DRY ? " dry run" : "");
    } else {
        bufAdd(h, "# ICO texture export %s %s complete%s\n", x->serial, date,
               x->flags & TEXPACK_EXPORT_DRY ? " dry run" : "");
    }
    bufAdd(h,
           "# items %d  tim2 %d  jim-records %d  written %d  already-on-disk %d  duplicates %d  "
           "refused-clut24 %d  refused-short-clut %d  bad %d  failed %d\n",
           s->done, s->tim2, s->jimRecords, s->written, s->skipped, s->duplicates, s->refusedClut24,
           s->refusedShortClut, s->bad, s->failed);
    bufAdd(h,
           "# Not in this export, because the disc does not hold them as pictures: what the game "
           "draws while it runs (the fog, the glow, the blur and the depth of field), the movies, "
           "the later colours of the %d textures whose colours cycle (only the first colours are "
           "on the disc), and names made of several sizes of one texture together (PCSX2 with "
           "mipmapping on). To save those, play with Dump textures on.\n",
           s->scroll);
    bufAdd(h, "# member\tkind\ttype\tsize\tWxH\tlevels\tclut\tflags\tnames\n");
}

static int writeManifest(const TexpackExport *x, const Buf *h, char *path, size_t size)
{
    char tmp[PATH_MAX_ + 8];
    if (joinPath(path, size, x->dir, "manifest.txt") != 0) {
        errno = ENAMETOOLONG;
        return -1;
    }
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = ico_fopen(tmp, "wb");
    if (f == NULL) {
        return -1;
    }
    int ok = h->cap != (size_t)-1 && x->rows.cap != (size_t)-1 &&
             (h->len == 0 || fwrite(h->p, 1, h->len, f) == h->len) &&
             (x->rows.len == 0 || fwrite(x->rows.p, 1, x->rows.len, f) == x->rows.len);
    int e = ok ? 0 : (errno ? errno : ENOMEM);
    if (fclose(f) != 0 && ok) {
        ok = 0;
        e = errno ? errno : EIO;
    }
    if (ok && ico_rename_replace(tmp, path) != 0) {
        ok = 0;
        e = errno ? errno : EIO;
    }
    if (!ok) {
        (void)ico_remove(tmp);
        errno = e;
        return -1;
    }
    return 0;
}

int texpack_export_close(TexpackExport *x, int stopped, int total, TexpackExportStatus *status)
{
    char path[PATH_MAX_];
    Buf h = {0};
    int state;

    if (x == NULL) {
        if (status != NULL) {
            memset(status, 0, sizeof(*status));
            status->state = TEXPACK_EXPORT_FAILED;
        }
        return TEXPACK_EXPORT_FAILED;
    }
    x->st.total = total > x->st.done ? total : x->st.done;
    header(x, stopped, &h);
    path[0] = '\0';
    int manifest = writeManifest(x, &h, path, sizeof(path));
    if (manifest != 0) {
        int e = errno;
        fprintf(stderr, "textures: export cannot write %s: %s (errno %d)\n",
                path[0] ? path : x->dir, strerror(e), e);
    }
    if (manifest != 0 || (x->st.written == 0 && x->st.failed > 0)) {
        state = TEXPACK_EXPORT_FAILED;
    } else {
        state = stopped ? TEXPACK_EXPORT_STOPPED : TEXPACK_EXPORT_DONE;
    }
    char word[64];
    if (state == TEXPACK_EXPORT_FAILED) {
        snprintf(word, sizeof(word), "failed");
    } else if (stopped) {
        snprintf(word, sizeof(word), "stopped after %d of %d", x->st.done, x->st.total);
    } else {
        snprintf(word, sizeof(word), "complete");
    }
    const TexpackExportStatus *s = &x->st;
    fprintf(stderr,
            "textures: export %s%s: items %d, tim2 %d, jim-records %d, written %d, "
            "already-on-disk %d, duplicates %d, refused-clut24 %d, refused-short-clut %d, bad %d, "
            "failed %d, manifest %s\n",
            word, x->flags & TEXPACK_EXPORT_DRY ? " (dry run)" : "", s->done, s->tim2,
            s->jimRecords, s->written, s->skipped, s->duplicates, s->refusedClut24,
            s->refusedShortClut, s->bad, s->failed, manifest == 0 ? path : "not written");
    x->st.state = state;
    if (status != NULL) {
        *status = x->st;
    }
    free(h.p);
    free(x->keys);
    free(x->rows.p);
    free(x->names.p);
    free(x->rgba);
    free(x->texa);
    free(x);
    return state;
}

typedef struct Run {
    TexpackExport *x;
    const TexpackExportHooks *hooks;
} Run;

static int runItem(void *user, const TexpackWalkItem *it)
{
    Run *r = user;
    const TexpackExportHooks *h = r->hooks;
    if (h != NULL && h->cancelled != NULL && h->cancelled(h->user)) {
        return 1;
    }
    texpack_export_item(r->x, it);
    if (r->x->st.done % PROGRESS_EVERY == 0) {
        fprintf(stderr, "textures: export %d of %d items, %d written\n", r->x->st.done,
                r->x->st.total, r->x->st.written);
    }
    if (h != NULL && h->progress != NULL) {
        h->progress(h->user, &r->x->st);
    }
    return 0;
}

int texpack_export_run(IcoVfs *vfs, const char *dumpDir, const char *serial, unsigned flags,
                       const TexpackExportHooks *hooks, TexpackExportStatus *status)
{
    IcoDfIndex *idx = vfs != NULL ? ico_df_index_open(vfs) : NULL;
    TexpackExport *x = idx != NULL ? texpack_export_open(dumpDir, serial, flags) : NULL;
    Run r;

    if (x == NULL) {
        fprintf(stderr, "textures: export failed: %s\n",
                idx == NULL ? "the disc's DATA.DF could not be read" : "no folder or no memory");
        ico_df_index_close(idx);
        if (status != NULL) {
            memset(status, 0, sizeof(*status));
            snprintf(status->dir, sizeof(status->dir), "%s", dumpDir ? dumpDir : "");
            status->state = TEXPACK_EXPORT_FAILED;
        }
        return TEXPACK_EXPORT_FAILED;
    }
    x->st.total = texpack_walk_total(idx);
    fprintf(stderr, "textures: export started, %d items, folder %s%s\n", x->st.total, x->dir,
            flags & TEXPACK_EXPORT_DRY ? " (dry run)" : "");
    if (hooks != NULL && hooks->progress != NULL) {
        hooks->progress(hooks->user, &x->st);
    }
    r.x = x;
    r.hooks = hooks;
    int rc = texpack_walk_disc(vfs, idx, runItem, &r, NULL);
    ico_df_index_close(idx);
    return texpack_export_close(x, rc != 0, x->st.total, status);
}

/* ------------------------------------------------------------ the job */

#if ICO_TEXPACK_EXPORT_THREAD

/* the status's int fields, in its order (state first) */
enum {
    J_STATE,
    J_DONE,
    J_TOTAL,
    J_TIM2,
    J_JIM,
    J_WRITTEN,
    J_SKIPPED,
    J_DUPS,
    J_REFUSED,
    J_CLUT24,
    J_SHORT,
    J_BAD,
    J_FAILED,
    J_SCROLL,
    J_COUNT
};

static struct {
    SDL_Thread *thread;
    IcoVfs *vfs; /* the worker's mount: unmounted by the game thread at the join */
    SDL_AtomicInt v[J_COUNT];
    SDL_AtomicInt cancel;
    char dir[sizeof(((TexpackExportStatus *)0)->dir)]; /* written only with no worker */
    char serial[32];
} s_job;

static int *field(TexpackExportStatus *s, int i)
{
    int *const f[J_COUNT] = {&s->state,
                             &s->done,
                             &s->total,
                             &s->tim2,
                             &s->jimRecords,
                             &s->written,
                             &s->skipped,
                             &s->duplicates,
                             &s->refused,
                             &s->refusedClut24,
                             &s->refusedShortClut,
                             &s->bad,
                             &s->failed,
                             &s->scroll};
    return f[i];
}

/* the counters (not the state: the worker sets it last) */
static void jobStore(const TexpackExportStatus *now)
{
    TexpackExportStatus c = *now;
    for (int i = J_DONE; i < J_COUNT; i++) {
        SDL_SetAtomicInt(&s_job.v[i], *field(&c, i));
    }
}

static int jobCancelled(void *user)
{
    (void)user;
    return SDL_GetAtomicInt(&s_job.cancel) != 0;
}

static void jobProgress(void *user, const TexpackExportStatus *now)
{
    (void)user;
    jobStore(now);
}

static int SDLCALL jobMain(void *arg)
{
    TexpackExportHooks h = {jobCancelled, jobProgress, NULL};
    TexpackExportStatus st;
    (void)arg;
    (void)SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW);
    int state = texpack_export_run(s_job.vfs, s_job.dir, s_job.serial, 0, &h, &st);
    jobStore(&st);
    SDL_SetAtomicInt(&s_job.v[J_STATE], state); /* last */
    return 0;
}

/* a worker that finished (or is told to stop) joined, its mount let go */
static void jobJoin(void)
{
    if (s_job.thread != NULL) {
        SDL_WaitThread(s_job.thread, NULL);
        s_job.thread = NULL;
    }
    if (s_job.vfs != NULL) {
        ico_vfs_unmount(s_job.vfs);
        s_job.vfs = NULL;
    }
}

int texpack_export_begin(void)
{
    char dir[sizeof(s_job.dir)];
    if (s_job.thread != NULL && SDL_GetAtomicInt(&s_job.v[J_STATE]) == TEXPACK_EXPORT_RUNNING) {
        fprintf(stderr, "textures: export not started: one is running\n");
        return -1;
    }
    jobJoin();
    if (texpack_dump_dir(dir, sizeof(dir)) != 0) {
        fprintf(stderr, "textures: export not started: no dump folder (texture packs not started "
                        "or no user folder)\n");
        return -1;
    }
    IcoVfs *disc = ico_vfs_disc();
    if (disc == NULL) {
        fprintf(stderr, "textures: export not started: no disc\n");
        return -1;
    }
    IcoVfs *vfs = ico_vfs_reopen(disc);
    if (vfs == NULL) {
        fprintf(stderr, "textures: export not started: the disc %s cannot be opened again\n",
                ico_vfs_location(disc));
        return -1;
    }
    rd_png_prepare();
    memcpy(s_job.dir, dir, sizeof(s_job.dir));
    serialOfDir(dir, s_job.serial, sizeof(s_job.serial));
    s_job.vfs = vfs;
    int before = SDL_GetAtomicInt(&s_job.v[J_STATE]);
    for (int i = J_DONE; i < J_COUNT; i++) {
        SDL_SetAtomicInt(&s_job.v[i], 0);
    }
    SDL_SetAtomicInt(&s_job.cancel, 0);
    SDL_SetAtomicInt(&s_job.v[J_STATE], TEXPACK_EXPORT_RUNNING);
    s_job.thread = SDL_CreateThread(jobMain, "ico_texexport", NULL);
    if (s_job.thread == NULL) {
        fprintf(stderr, "textures: export not started: no thread (%s)\n", SDL_GetError());
        SDL_SetAtomicInt(&s_job.v[J_STATE], before);
        ico_vfs_unmount(vfs);
        s_job.vfs = NULL;
        return -1;
    }
    return 0;
}

int texpack_export_poll(TexpackExportStatus *out)
{
    int state = SDL_GetAtomicInt(&s_job.v[J_STATE]);
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
        for (int i = J_DONE; i < J_COUNT; i++) {
            *field(out, i) = SDL_GetAtomicInt(&s_job.v[i]);
        }
        out->state = state;
        if (state != TEXPACK_EXPORT_IDLE) {
            memcpy(out->dir, s_job.dir, sizeof(out->dir));
        }
    }
    return state;
}

void texpack_export_cancel(void)
{
    if (SDL_GetAtomicInt(&s_job.v[J_STATE]) == TEXPACK_EXPORT_RUNNING) {
        SDL_SetAtomicInt(&s_job.cancel, 1);
    }
}

void texpack_export_shutdown(void)
{
    if (s_job.thread != NULL) {
        SDL_SetAtomicInt(&s_job.cancel, 1);
    }
    jobJoin();
}

#else /* no SDL threads: the job is not offered */

int texpack_export_begin(void)
{
    fprintf(stderr, "textures: export not started: no thread (a build without SDL)\n");
    return -1;
}

int texpack_export_poll(TexpackExportStatus *out)
{
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
    return TEXPACK_EXPORT_IDLE;
}

void texpack_export_cancel(void) {}

void texpack_export_shutdown(void) {}

#endif

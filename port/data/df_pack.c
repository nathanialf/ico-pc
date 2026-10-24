/*
 * port/data/df_pack.c
 *
 * One member of a DATA.DF stage pack, read through the VFS (df_pack.h).
 */
#include "df_pack.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "miniz_tinfl.h"
#include "../include/ico_endian.h"

#define DF_ENTRY 40
#define PACK_HEADER 16
#define PACK_ENTRY 0x224
#define PACK_NAME (PACK_ENTRY - 16)
#define IN_CHUNK 65536

typedef struct DfEntry {
    char name[33];
    uint32_t off, size;
    int first, count; /* the entry's members in the index (a pack), else -1, 0 */
} DfEntry;

typedef struct DfIndexed {
    uint32_t name; /* into pool */
    IcoDfMember m;
} DfIndexed;

/* One volume's directory and member index (df_pack.h IcoDfIndex). */
struct IcoDfIndex {
    IcoVfs *vfs;
    DfEntry *dir;
    int dirCount; /* -1 until read */
    DfIndexed *members;
    int memberCount, memberCap, packs;
    char *pool;
    size_t poolLen, poolCap;
    int indexed;
};

/* the index behind the global calls (the game thread's: gallery_play.c) */
static IcoDfIndex s_global = {.dirCount = -1};

static void dfFree(IcoDfIndex *x)
{
    free(x->dir);
    free(x->members);
    free(x->pool);
    memset(x, 0, sizeof(*x));
    x->dirCount = -1;
}

static int nameEq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
    }
    return *a == *b;
}

/* x's DATA.DF opened into *df and its directory read (once) */
static int dirLoad(IcoDfIndex *x, IcoVfsFile *df)
{
    /* DATA.DF where the volume's disc keeps it: DFDATAS/, or under the game's folder
       (vfs.h ico_vfs_df_path) */
    if (x->vfs == NULL || ico_vfs_open(x->vfs, ico_vfs_df_path(x->vfs), df) != 0) {
        return -1;
    }
    if (x->dirCount >= 0) {
        return 0;
    }
    uint8_t w[4];
    if (ico_vfs_read(df, 0, w, 4) != 4) {
        return -1;
    }
    uint32_t n = ico_le32(w);
    if (n == 0 || n > 4096) {
        return -1;
    }
    uint8_t *raw = malloc((size_t)n * DF_ENTRY);
    x->dir = calloc(n, sizeof(*x->dir));
    if (raw == NULL || x->dir == NULL ||
        ico_vfs_read(df, 4, raw, (size_t)n * DF_ENTRY) != (int64_t)n * DF_ENTRY) {
        free(raw);
        free(x->dir);
        x->dir = NULL;
        return -1;
    }
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *e = raw + (size_t)i * DF_ENTRY;
        memcpy(x->dir[i].name, e, 32);
        x->dir[i].name[32] = '\0';
        x->dir[i].off = ico_le32(e + 32);
        x->dir[i].size = ico_le32(e + 36);
        x->dir[i].first = -1;
        x->dir[i].count = 0;
    }
    free(raw);
    x->dirCount = (int)n;
    return 0;
}

/* the global index's directory, read once per volume: another vfs starts
   it over */
static int loadDir(IcoVfs *vfs, IcoVfsFile *df)
{
    if (s_global.vfs != vfs) {
        dfFree(&s_global);
        s_global.vfs = vfs;
    }
    return dirLoad(&s_global, df);
}

/* x's directory entry `name`, or -1 */
static int dirFind(const IcoDfIndex *x, const char *name)
{
    for (int i = 0; i < x->dirCount; i++) {
        if (nameEq(x->dir[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static int64_t readEntry(const IcoDfIndex *x, const IcoVfsFile *df, int i, uint64_t off, void *dst,
                         size_t n)
{
    if (off >= x->dir[i].size) {
        return 0;
    }
    if (n > x->dir[i].size - off) {
        n = (size_t)(x->dir[i].size - off);
    }
    return ico_vfs_read(df, (uint64_t)x->dir[i].off + off, dst, n);
}

int ico_df_has(IcoVfs *vfs, const char *name)
{
    IcoVfsFile df;
    if (loadDir(vfs, &df) != 0) {
        return -1;
    }
    return dirFind(&s_global, name) >= 0;
}

const char *ico_df_entry_name(IcoVfs *vfs, int i)
{
    IcoVfsFile df;
    if (loadDir(vfs, &df) != 0 || i < 0 || i >= s_global.dirCount) {
        return NULL;
    }
    return s_global.dir[i].name;
}

int64_t ico_df_size(IcoVfs *vfs, const char *name)
{
    IcoVfsFile df;
    if (loadDir(vfs, &df) != 0) {
        return -1;
    }
    int i = dirFind(&s_global, name);
    return i >= 0 ? (int64_t)s_global.dir[i].size : -1;
}

int64_t ico_df_read(IcoVfs *vfs, const char *name, uint64_t off, void *dst, size_t n)
{
    IcoVfsFile df;
    if (loadDir(vfs, &df) != 0) {
        return -1;
    }
    int i = dirFind(&s_global, name);
    return i >= 0 ? readEntry(&s_global, &df, i, off, dst, n) : -1;
}

/* --- inflating a pack ------------------------------------------------------ */

typedef struct Inflater {
    const IcoVfsFile *df;
    uint64_t inPos, inEnd; /* DATA.DF byte range of the pack's stream */
    uint8_t in[IN_CHUNK];
    size_t inAvail, inOff;
    uint8_t dict[TINFL_LZ_DICT_SIZE];
    size_t dictOff;
    uint64_t outPos;     /* inflated bytes produced so far */
    const uint8_t *last; /* the last run (still in dict) and where it starts */
    uint64_t lastStart;
    size_t lastLen;
    tinfl_decompressor d;
    int done;
} Inflater;

static void infOpen(Inflater *f, const IcoVfsFile *df, const DfEntry *e)
{
    f->df = df;
    f->inPos = e->off;
    f->inEnd = (uint64_t)e->off + e->size;
    f->inAvail = f->inOff = 0;
    f->dictOff = 0;
    f->outPos = 0;
    f->last = NULL;
    f->lastStart = 0;
    f->lastLen = 0;
    f->done = 0;
    tinfl_init(&f->d);
}

/* The next run of inflated bytes: *p and the count, 0 at the end, -1 on an
   error.  The run stays valid until the next call. */
static int64_t infNext(Inflater *f, const uint8_t **p)
{
    for (;;) {
        if (f->done) {
            return 0;
        }
        if (f->inAvail == 0 && f->inPos < f->inEnd) {
            uint64_t want = f->inEnd - f->inPos;
            if (want > IN_CHUNK) {
                want = IN_CHUNK;
            }
            int64_t got = ico_vfs_read(f->df, f->inPos, f->in, (size_t)want);
            if (got <= 0) {
                return -1;
            }
            f->inPos += (uint64_t)got;
            f->inAvail = (size_t)got;
            f->inOff = 0;
        }
        size_t inSize = f->inAvail;
        size_t outSize = TINFL_LZ_DICT_SIZE - f->dictOff;
        int more = f->inPos < f->inEnd ? TINFL_FLAG_HAS_MORE_INPUT : 0;
        tinfl_status st = tinfl_decompress(&f->d, f->in + f->inOff, &inSize, f->dict,
                                           f->dict + f->dictOff, &outSize, (mz_uint32)more);
        f->inOff += inSize;
        f->inAvail -= inSize;
        if (st < TINFL_STATUS_DONE) {
            return -1;
        }
        if (st == TINFL_STATUS_DONE) {
            f->done = 1;
        }
        if (st == TINFL_STATUS_NEEDS_MORE_INPUT && f->inAvail == 0 && f->inPos >= f->inEnd) {
            f->done = 1; /* truncated: what came out is all there is */
        }
        if (outSize > 0) {
            *p = f->dict + f->dictOff;
            f->last = *p;
            f->lastStart = f->outPos;
            f->lastLen = outSize;
            f->dictOff = (f->dictOff + outSize) & (TINFL_LZ_DICT_SIZE - 1);
            f->outPos += outSize;
            return (int64_t)outSize;
        }
    }
}

/* Copies the inflated bytes [from, from + n) into dst; 0 or -1. */
static int infCopy(Inflater *f, uint64_t from, uint8_t *dst, size_t n)
{
    uint64_t to = from + n;
    if (f->last != NULL && from < f->lastStart + f->lastLen && to > f->lastStart) {
        /* the part of the last run the range takes */
        uint64_t lo = from > f->lastStart ? from : f->lastStart;
        uint64_t hi = to < f->lastStart + f->lastLen ? to : f->lastStart + f->lastLen;
        memcpy(dst + (lo - from), f->last + (lo - f->lastStart), (size_t)(hi - lo));
    }
    while (f->outPos < to) {
        const uint8_t *p;
        uint64_t start = f->outPos;
        int64_t got = infNext(f, &p);
        if (got <= 0) {
            return -1;
        }
        uint64_t end = start + (uint64_t)got;
        uint64_t lo = start > from ? start : from;
        uint64_t hi = end < to ? end : to;
        if (lo < hi) {
            memcpy(dst + (lo - from), p + (lo - start), (size_t)(hi - lo));
        }
    }
    return 0;
}

/* --- the index -------------------------------------------------------------- */

static int isPack(const char *name)
{
    size_t n = strlen(name);
    return n > 3 && nameEq(name + n - 3, ".DF");
}

static int addMember(IcoDfIndex *x, const char *name, const IcoDfMember *m)
{
    size_t len = strlen(name) + 1;
    if (x->poolLen + len > x->poolCap) {
        size_t cap = x->poolCap ? x->poolCap * 2 : 65536;
        while (cap < x->poolLen + len) {
            cap *= 2;
        }
        char *p = realloc(x->pool, cap);
        if (p == NULL) {
            return -1;
        }
        x->pool = p;
        x->poolCap = cap;
    }
    if (x->memberCount == x->memberCap) {
        int cap = x->memberCap ? x->memberCap * 2 : 1024;
        DfIndexed *p = realloc(x->members, (size_t)cap * sizeof(*p));
        if (p == NULL) {
            return -1;
        }
        x->members = p;
        x->memberCap = cap;
    }
    memcpy(x->pool + x->poolLen, name, len);
    x->members[x->memberCount].name = (uint32_t)x->poolLen;
    x->members[x->memberCount].m = *m;
    x->memberCount++;
    x->poolLen += len;
    return 0;
}

static int buildIndex(IcoDfIndex *x, const IcoVfsFile *df)
{
    Inflater *f = malloc(sizeof(*f));
    if (f == NULL) {
        return -1;
    }
    for (int i = 0; i < x->dirCount; i++) {
        if (!isPack(x->dir[i].name)) {
            continue;
        }
        uint8_t hdr[PACK_HEADER];
        infOpen(f, df, &x->dir[i]);
        if (infCopy(f, 0, hdr, PACK_HEADER) != 0) {
            fprintf(stderr, "df_pack: %s: cannot inflate the header\n", x->dir[i].name);
            continue;
        }
        uint32_t n = ico_le32(hdr);
        if (n == 0 || n > 20000) {
            continue;
        }
        uint8_t *ent = malloc((size_t)n * PACK_ENTRY);
        if (ent == NULL || infCopy(f, PACK_HEADER, ent, (size_t)n * PACK_ENTRY) != 0) {
            fprintf(stderr, "df_pack: %s: cannot inflate the directory\n", x->dir[i].name);
            free(ent);
            continue;
        }
        uint64_t pos = PACK_HEADER + (uint64_t)n * PACK_ENTRY;
        x->dir[i].first = x->memberCount;
        for (uint32_t k = 0; k < n; k++) {
            const uint8_t *e = ent + (size_t)k * PACK_ENTRY;
            char name[PACK_NAME + 1];
            memcpy(name, e + 16, PACK_NAME);
            name[PACK_NAME] = '\0';
            IcoDfMember m;
            m.pack = i;
            m.off = (uint32_t)pos;
            m.size = ico_le32(e + 12);
            m.id = (int)ico_le32(e + 0);
            m.kind = (int)ico_le32(e + 4);
            if (addMember(x, name, &m) != 0) {
                free(ent);
                free(f);
                return -1;
            }
            x->dir[i].count++;
            pos += m.size;
        }
        free(ent);
        x->packs++;
    }
    free(f);
    x->indexed = 1;
    return 0;
}

static int findMember(const IcoDfIndex *x, const char *name, IcoDfMember *out)
{
    for (int i = 0; i < x->memberCount; i++) {
        if (nameEq(x->pool + x->members[i].name, name)) {
            *out = x->members[i].m;
            return 0;
        }
    }
    return -1;
}

static int readMember(const IcoDfIndex *x, const IcoVfsFile *df, const IcoDfMember *m, void *dst)
{
    if (m == NULL || m->pack < 0 || m->pack >= x->dirCount) {
        return -1;
    }
    Inflater *f = malloc(sizeof(*f));
    if (f == NULL) {
        return -1;
    }
    infOpen(f, df, &x->dir[m->pack]);
    int rc = infCopy(f, m->off, dst, m->size);
    free(f);
    return rc;
}

int ico_df_find_member(IcoVfs *vfs, const char *name, IcoDfMember *out)
{
    IcoVfsFile df;
    if (loadDir(vfs, &df) != 0) {
        return -1;
    }
    if (!s_global.indexed && buildIndex(&s_global, &df) != 0) {
        return -1;
    }
    return findMember(&s_global, name, out);
}

int ico_df_read_member(IcoVfs *vfs, const IcoDfMember *m, void *dst)
{
    IcoVfsFile df;
    if (loadDir(vfs, &df) != 0) {
        return -1;
    }
    return readMember(&s_global, &df, m, dst);
}

int ico_df_index_packs(void)
{
    return s_global.packs;
}

const char *ico_df_member_name(int i)
{
    return ico_df_index_member_name(&s_global, i);
}

int ico_df_member(int i, IcoDfMember *out)
{
    return ico_df_index_member_at(&s_global, i, out);
}

int ico_df_index_members(void)
{
    return s_global.memberCount;
}

/* --- an index of its own --------------------------------------------------- */

IcoDfIndex *ico_df_index_open(IcoVfs *vfs)
{
    IcoVfsFile df;
    IcoDfIndex *x = calloc(1, sizeof(*x));
    if (x == NULL) {
        return NULL;
    }
    x->dirCount = -1;
    x->vfs = vfs;
    if (dirLoad(x, &df) != 0 || buildIndex(x, &df) != 0) {
        ico_df_index_close(x);
        return NULL;
    }
    return x;
}

void ico_df_index_close(IcoDfIndex *x)
{
    if (x == NULL || x == &s_global) {
        return;
    }
    dfFree(x);
    free(x);
}

int ico_df_index_entries(const IcoDfIndex *x)
{
    return x != NULL && x->dirCount > 0 ? x->dirCount : 0;
}

const char *ico_df_index_entry_name(const IcoDfIndex *x, int i)
{
    return x != NULL && i >= 0 && i < x->dirCount ? x->dir[i].name : NULL;
}

int64_t ico_df_index_entry_size(const IcoDfIndex *x, int i)
{
    return x != NULL && i >= 0 && i < x->dirCount ? (int64_t)x->dir[i].size : -1;
}

int ico_df_index_entry_find(const IcoDfIndex *x, const char *name)
{
    return x != NULL && name != NULL ? dirFind(x, name) : -1;
}

int64_t ico_df_index_read_entry(IcoDfIndex *x, int i, uint64_t off, void *dst, size_t n)
{
    IcoVfsFile df;
    if (x == NULL || i < 0 || i >= x->dirCount || dirLoad(x, &df) != 0) {
        return -1;
    }
    return readEntry(x, &df, i, off, dst, n);
}

int ico_df_index_pack_count(const IcoDfIndex *x)
{
    return x != NULL ? x->packs : 0;
}

int ico_df_index_member_count(const IcoDfIndex *x)
{
    return x != NULL ? x->memberCount : 0;
}

const char *ico_df_index_member_name(const IcoDfIndex *x, int i)
{
    return x != NULL && i >= 0 && i < x->memberCount ? x->pool + x->members[i].name : NULL;
}

int ico_df_index_member_at(const IcoDfIndex *x, int i, IcoDfMember *out)
{
    if (x == NULL || i < 0 || i >= x->memberCount || out == NULL) {
        return -1;
    }
    *out = x->members[i].m;
    return 0;
}

int ico_df_index_pack_members(const IcoDfIndex *x, int pack, int *first)
{
    if (x == NULL || pack < 0 || pack >= x->dirCount || x->dir[pack].first < 0) {
        return 0;
    }
    if (first != NULL) {
        *first = x->dir[pack].first;
    }
    return x->dir[pack].count;
}

int ico_df_index_find(const IcoDfIndex *x, const char *name, IcoDfMember *out)
{
    if (x == NULL || name == NULL || out == NULL) {
        return -1;
    }
    return findMember(x, name, out);
}

int ico_df_index_read_member(IcoDfIndex *x, const IcoDfMember *m, void *dst)
{
    IcoVfsFile df;
    if (x == NULL || dirLoad(x, &df) != 0) {
        return -1;
    }
    return readMember(x, &df, m, dst);
}

int ico_df_index_read_pack(IcoDfIndex *x, int pack, IcoDfMemberFn fn, void *user)
{
    IcoVfsFile df;
    int first = 0;
    int count = ico_df_index_pack_members(x, pack, &first);
    if (count == 0) {
        return x != NULL && pack >= 0 && pack < x->dirCount && isPack(x->dir[pack].name) ? 0 : -1;
    }
    if (fn == NULL || dirLoad(x, &df) != 0) {
        return -1;
    }
    uint32_t largest = 1;
    for (int k = 0; k < count; k++) {
        uint32_t size = x->members[first + k].m.size;
        largest = size > largest ? size : largest;
    }
    Inflater *f = malloc(sizeof(*f));
    uint8_t *buf = malloc(largest);
    if (f == NULL || buf == NULL) {
        free(f);
        free(buf);
        return -1;
    }
    /* the members lie back to back in the inflated stream: one pass */
    infOpen(f, &df, &x->dir[pack]);
    int rc = 0;
    for (int k = 0; k < count && rc == 0; k++) {
        const DfIndexed *e = &x->members[first + k];
        if (infCopy(f, e->m.off, buf, e->m.size) != 0) {
            rc = -1;
            break;
        }
        if (fn(user, first + k, x->pool + e->name, &e->m, buf) != 0) {
            rc = 1;
        }
    }
    free(buf);
    free(f);
    return rc;
}

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

#define DF_PATH "DFDATAS/DATA.DF"
#define DF_ENTRY 40
#define PACK_HEADER 16
#define PACK_ENTRY 0x224
#define PACK_NAME (PACK_ENTRY - 16)
#define IN_CHUNK 65536

typedef struct DfEntry {
    char name[33];
    uint32_t off, size;
} DfEntry;

typedef struct DfIndexed {
    uint32_t name; /* into s_pool */
    IcoDfMember m;
} DfIndexed;

static IcoVfs *s_vfs;
static DfEntry *s_dir;
static int s_dirCount = -1;
static DfIndexed *s_members;
static int s_memberCount, s_memberCap, s_packs;
static char *s_pool;
static size_t s_poolLen, s_poolCap;
static int s_indexed;

void ico_df_reset(void)
{
    free(s_dir);
    free(s_members);
    free(s_pool);
    s_dir = NULL;
    s_members = NULL;
    s_pool = NULL;
    s_dirCount = -1;
    s_memberCount = s_memberCap = s_packs = 0;
    s_poolLen = s_poolCap = 0;
    s_indexed = 0;
    s_vfs = NULL;
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

/* DATA.DF's directory, read once per volume */
static int loadDir(IcoVfs *vfs, IcoVfsFile *df)
{
    if (s_vfs != vfs) {
        ico_df_reset();
        s_vfs = vfs;
    }
    if (vfs == NULL || ico_vfs_open(vfs, DF_PATH, df) != 0) {
        return -1;
    }
    if (s_dirCount >= 0) {
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
    s_dir = calloc(n, sizeof(*s_dir));
    if (raw == NULL || s_dir == NULL ||
        ico_vfs_read(df, 4, raw, (size_t)n * DF_ENTRY) != (int64_t)n * DF_ENTRY) {
        free(raw);
        free(s_dir);
        s_dir = NULL;
        return -1;
    }
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *e = raw + (size_t)i * DF_ENTRY;
        memcpy(s_dir[i].name, e, 32);
        s_dir[i].name[32] = '\0';
        s_dir[i].off = ico_le32(e + 32);
        s_dir[i].size = ico_le32(e + 36);
    }
    free(raw);
    s_dirCount = (int)n;
    return 0;
}

int ico_df_has(IcoVfs *vfs, const char *name)
{
    IcoVfsFile df;
    if (loadDir(vfs, &df) != 0) {
        return -1;
    }
    for (int i = 0; i < s_dirCount; i++) {
        if (nameEq(s_dir[i].name, name)) {
            return 1;
        }
    }
    return 0;
}

const char *ico_df_entry_name(IcoVfs *vfs, int i)
{
    IcoVfsFile df;
    if (loadDir(vfs, &df) != 0 || i < 0 || i >= s_dirCount) {
        return NULL;
    }
    return s_dir[i].name;
}

int64_t ico_df_size(IcoVfs *vfs, const char *name)
{
    IcoVfsFile df;
    if (loadDir(vfs, &df) != 0) {
        return -1;
    }
    for (int i = 0; i < s_dirCount; i++) {
        if (nameEq(s_dir[i].name, name)) {
            return (int64_t)s_dir[i].size;
        }
    }
    return -1;
}

int64_t ico_df_read(IcoVfs *vfs, const char *name, uint64_t off, void *dst, size_t n)
{
    IcoVfsFile df;
    if (loadDir(vfs, &df) != 0) {
        return -1;
    }
    for (int i = 0; i < s_dirCount; i++) {
        if (nameEq(s_dir[i].name, name)) {
            if (off >= s_dir[i].size) {
                return 0;
            }
            if (n > s_dir[i].size - off) {
                n = (size_t)(s_dir[i].size - off);
            }
            return ico_vfs_read(&df, (uint64_t)s_dir[i].off + off, dst, n);
        }
    }
    return -1;
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

static int addMember(const char *name, const IcoDfMember *m)
{
    size_t len = strlen(name) + 1;
    if (s_poolLen + len > s_poolCap) {
        size_t cap = s_poolCap ? s_poolCap * 2 : 65536;
        while (cap < s_poolLen + len) {
            cap *= 2;
        }
        char *p = realloc(s_pool, cap);
        if (p == NULL) {
            return -1;
        }
        s_pool = p;
        s_poolCap = cap;
    }
    if (s_memberCount == s_memberCap) {
        int cap = s_memberCap ? s_memberCap * 2 : 1024;
        DfIndexed *p = realloc(s_members, (size_t)cap * sizeof(*p));
        if (p == NULL) {
            return -1;
        }
        s_members = p;
        s_memberCap = cap;
    }
    memcpy(s_pool + s_poolLen, name, len);
    s_members[s_memberCount].name = (uint32_t)s_poolLen;
    s_members[s_memberCount].m = *m;
    s_memberCount++;
    s_poolLen += len;
    return 0;
}

static int buildIndex(const IcoVfsFile *df)
{
    Inflater *f = malloc(sizeof(*f));
    if (f == NULL) {
        return -1;
    }
    for (int i = 0; i < s_dirCount; i++) {
        if (!isPack(s_dir[i].name)) {
            continue;
        }
        uint8_t hdr[PACK_HEADER];
        infOpen(f, df, &s_dir[i]);
        if (infCopy(f, 0, hdr, PACK_HEADER) != 0) {
            fprintf(stderr, "df_pack: %s: cannot inflate the header\n", s_dir[i].name);
            continue;
        }
        uint32_t n = ico_le32(hdr);
        if (n == 0 || n > 20000) {
            continue;
        }
        uint8_t *ent = malloc((size_t)n * PACK_ENTRY);
        if (ent == NULL || infCopy(f, PACK_HEADER, ent, (size_t)n * PACK_ENTRY) != 0) {
            fprintf(stderr, "df_pack: %s: cannot inflate the directory\n", s_dir[i].name);
            free(ent);
            continue;
        }
        uint64_t pos = PACK_HEADER + (uint64_t)n * PACK_ENTRY;
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
            if (addMember(name, &m) != 0) {
                free(ent);
                free(f);
                return -1;
            }
            pos += m.size;
        }
        free(ent);
        s_packs++;
    }
    free(f);
    s_indexed = 1;
    return 0;
}

int ico_df_find_member(IcoVfs *vfs, const char *name, IcoDfMember *out)
{
    IcoVfsFile df;
    if (loadDir(vfs, &df) != 0) {
        return -1;
    }
    if (!s_indexed && buildIndex(&df) != 0) {
        return -1;
    }
    for (int i = 0; i < s_memberCount; i++) {
        if (nameEq(s_pool + s_members[i].name, name)) {
            *out = s_members[i].m;
            return 0;
        }
    }
    return -1;
}

int ico_df_read_member(IcoVfs *vfs, const IcoDfMember *m, void *dst)
{
    IcoVfsFile df;
    if (loadDir(vfs, &df) != 0 || m->pack < 0 || m->pack >= s_dirCount) {
        return -1;
    }
    Inflater *f = malloc(sizeof(*f));
    if (f == NULL) {
        return -1;
    }
    infOpen(f, &df, &s_dir[m->pack]);
    int rc = infCopy(f, m->off, dst, m->size);
    free(f);
    return rc;
}

int ico_df_index_packs(void)
{
    return s_packs;
}

const char *ico_df_member_name(int i)
{
    return i >= 0 && i < s_memberCount ? s_pool + s_members[i].name : NULL;
}

int ico_df_member(int i, IcoDfMember *out)
{
    if (i < 0 || i >= s_memberCount || out == NULL) {
        return -1;
    }
    *out = s_members[i].m;
    return 0;
}

int ico_df_index_members(void)
{
    return s_memberCount;
}

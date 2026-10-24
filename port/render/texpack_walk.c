/* texpack_walk.c: every TIM2 texture on the disc (texpack_walk.h).
 *
 * The parse follows Texture.c's tex_makeTexturePacket and texHostTexture
 * (the file is Latin-1: grep -a): the levels from the picture header and
 * the mipmap header, the GS sizes from getTWTH, TBW as texTBW rounds it,
 * the CLUT through tex_convertClutCSM2ToCSM1 when the file holds it in
 * index order.  Moved here from texpack_disc_test.c, whose output it keeps.
 */
#include "texpack_walk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_tex.h" /* the RDTEX_PSM* values and rdtex_csm1_index (inline) */

enum { TYPE_COUNT = 6 };

static const char *const kType[TYPE_COUNT] = {"NONE",    "PSMCT16", "PSMCT24",
                                              "PSMCT32", "PSMT4",   "PSMT8"};

static uint32_t rd16(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

static uint32_t rd32(const uint8_t *p)
{
    return rd16(p) | rd16(p + 2) << 16;
}

/* Texture.c getTWTH: the smallest n with 2^n >= size, -1 past 1024 */
static int twth(uint32_t size)
{
    for (int i = 0; i < 11; i++) {
        if ((1u << i) >= size) {
            return i;
        }
    }
    return -1;
}

/* Texture.c texTBW: 64-texel units, PSMT8/PSMT4 rounded up to even */
static uint32_t tbwOf(uint32_t psm, uint32_t w)
{
    uint32_t n = (w + 63) >> 6;
    return psm == RDTEX_PSMT8 || psm == RDTEX_PSMT4 ? n + (n & 1) : n;
}

/* rd_tex.c rdtex_image_bytes for the five TIM2 formats (rd_tex.c is in
   ico_render, which needs a device library; the walk does not) */
static size_t imageBytes(uint32_t psm, uint32_t w, uint32_t h)
{
    size_t n = (size_t)w * h;
    switch (psm) {
    case RDTEX_PSMCT32:
        return n * 4;
    case RDTEX_PSMCT24:
        return n * 3;
    case RDTEX_PSMCT16:
        return n * 2;
    case RDTEX_PSMT8:
        return n;
    default:
        return (n + 1) / 2;
    }
}

/* Texture.c tex_convertClutCSM2ToCSM1 (rd_tex.c rdtex_clut_to_csm1) */
static void clutToCsm1(uint8_t *clut, uint32_t entry)
{
    uint8_t tmp[256 * 4];
    memcpy(tmp, clut, 256 * entry);
    for (uint32_t i = 0; i < 256; i++) {
        memcpy(clut + rdtex_csm1_index(i, 256) * entry, tmp + i * entry, entry);
    }
}

const char *texpack_walk_type_name(uint32_t type)
{
    return kType[type < TYPE_COUNT ? type : 0];
}

static void setMember(TexpackWalkItem *out, const char *member, int kind)
{
    snprintf(out->member, sizeof(out->member), "%s", member ? member : "");
    out->kind = kind;
}

int texpack_walk_tim2(const char *member, int kind, const uint8_t *d, size_t size,
                      TexpackWalkItem *out)
{
    static const uint32_t psmOf[TYPE_COUNT] = {
        0, RDTEX_PSMCT16, RDTEX_PSMCT24, RDTEX_PSMCT32, RDTEX_PSMT4, RDTEX_PSMT8};

    memset(out, 0, sizeof(*out));
    setMember(out, member, kind);
    out->data = d;
    out->size = size;
    if (d == NULL || size < 16 + 0x30 || memcmp(d, "TIM2", 4) != 0) {
        out->bad = TEXPACK_WALK_NOT_TIM2;
        return out->bad;
    }
    const uint8_t *pic = d + 16;
    uint32_t clutSize = rd32(pic + 0x04);
    uint32_t imageSize = rd32(pic + 0x08);
    uint32_t headerSize = rd16(pic + 0x0C);
    uint32_t clutColors = rd16(pic + 0x0E);
    uint32_t levels = pic[0x11];
    uint32_t clutType = pic[0x12];
    uint32_t type = pic[0x13];
    uint32_t w = rd16(pic + 0x14);
    uint32_t h = rd16(pic + 0x16);
    const uint8_t *image = pic + headerSize;

    out->type = type;
    out->clutType = clutType;
    out->clutColors = clutColors;
    out->clutSize = clutSize;
    out->imageSize = imageSize;
    out->w = w;
    out->h = h;
    out->levels = levels;
    if (type < 1 || type >= TYPE_COUNT || levels < 1 || levels > TEXPACK_MAX_LEVELS ||
        twth(w) < 0 || twth(h) < 0 || 16 + (size_t)headerSize + imageSize + clutSize > size) {
        out->bad = TEXPACK_WALK_BAD_HEADER;
        return out->bad;
    }
    out->bytes = 16 + (size_t)headerSize + imageSize + clutSize;
    out->nonPow2 = w != (1u << twth(w)) || h != (1u << twth(h));

    TexpackSource *src = &out->src;
    src->psm = psmOf[type];
    src->levels = levels;
    size_t at = 0;
    for (uint32_t i = 0; i < levels; i++) {
        TexpackLevel *l = &src->lv[i];
        size_t bytes = levels == 1 ? imageSize : rd32(pic + 0x30 + 0x10 + 4 * i);
        l->tw = (uint8_t)(twth(w) - (int)i);
        l->th = (uint8_t)(twth(h) - (int)i);
        l->w = w >> i;
        l->h = h >> i;
        l->tbw = tbwOf(src->psm, l->w);
        l->pixels = image + at;
        if (at + bytes > imageSize || bytes < imageBytes(src->psm, l->w, l->h)) {
            out->bad = TEXPACK_WALK_BAD_LEVEL;
            out->badLevel = i;
            out->badBytes = bytes;
            return out->bad;
        }
        at += bytes;
    }

    if (type == 4 || type == 5) {
        uint32_t entry = (clutType & 0x3F) == 1 ? 2 : (clutType & 0x3F) == 2 ? 3 : 4;
        uint32_t full = type == 4 ? 16 : 256;
        src->cpsm = entry == 2 ? RDTEX_PSMCT16 : entry == 3 ? RDTEX_PSMCT24 : RDTEX_PSMCT32;
        out->clut24 = src->cpsm == RDTEX_PSMCT24;
        out->shortClut = (clutColors != 0 && clutColors < full) || clutSize < full * entry;
        /* tex_transVramClutTex uploads an 8x2 image for 16 entries, else
           16x16 (whatever the file holds) */
        src->clutColors = clutColors == 16 ? 16 : 256;
        memcpy(out->clut, image + imageSize,
               clutSize < sizeof(out->clut) ? clutSize : sizeof(out->clut));
        /* tex_convertClutCSM2ToCSM1 */
        if ((clutType >> 7) != 0 && clutColors == 256) {
            clutToCsm1(out->clut, entry);
        }
        src->clut = out->clut;
    }

    /* the ICO block after the headers (tex_makeTexturePacket): a CLUT
       scroll (csSpd, csStp, csBgn != csEnd: tex_textureAnimation) rewrites
       the CLUT while the game runs, so the names of the frames after the
       first cannot be made from the disc */
    static const uint32_t kMipHeader[8] = {0, 0, 32, 32, 32, 48, 48, 48};
    const uint8_t *ext = pic + 0x30 + kMipHeader[levels];
    out->scroll = ext + 0x24 <= image && memcmp(ext, "ICO", 4) == 0 && rd32(ext + 0x1C) != 0 &&
                  rd32(ext + 0x20) != 0 && rd32(ext + 0x14) != rd32(ext + 0x18) &&
                  (type == 4 || type == 5);
    return TEXPACK_WALK_OK;
}

int texpack_walk_records(const char *file, int kind, const uint8_t *d, size_t size,
                         TexpackWalkFn fn, void *user, int *stopped)
{
    int n = 0;
    TexpackWalkItem *it = malloc(sizeof(*it));
    if (stopped != NULL) {
        *stopped = 0;
    }
    if (it == NULL || d == NULL || fn == NULL) {
        free(it);
        return 0;
    }
    for (size_t off = 0; off + 16 <= size; off += 16) {
        if (memcmp(d + off, "TIM2", 4) == 0) {
            char name[TEXPACK_WALK_NAME_MAX];
            snprintf(name, sizeof(name), "%s@0x%zx", file, off);
            texpack_walk_tim2(name, kind, d + off, size - off, it);
            n++;
            if (fn(user, it) != 0) {
                if (stopped != NULL) {
                    *stopped = 1;
                }
                break;
            }
        }
    }
    free(it);
    return n;
}

/* ------------------------------------------------------------ the disc */

static int endsWith(const char *name, const char *lower, const char *upper)
{
    size_t n = strlen(name);
    size_t k = strlen(lower);
    return n >= k && (strcmp(name + n - k, lower) == 0 || strcmp(name + n - k, upper) == 0);
}

static int isTm2(const char *name)
{
    return endsWith(name, ".tm2", ".TM2");
}

static int isJim(const char *name)
{
    return endsWith(name, ".jim", ".JIM");
}

static int recordsOf(int64_t bytes)
{
    return bytes > 0 ? (int)((bytes + TEXPACK_WALK_RECORD - 1) / TEXPACK_WALK_RECORD) : 0;
}

int texpack_walk_total(const IcoDfIndex *idx)
{
    int total = 0;
    int members = ico_df_index_member_count(idx);
    for (int i = 0; i < members; i++) {
        const char *name = ico_df_index_member_name(idx, i);
        IcoDfMember m;
        if (isTm2(name)) {
            total++;
        } else if (isJim(name) && ico_df_index_member_at(idx, i, &m) == 0) {
            total += recordsOf(m.size);
        }
    }
    int entries = ico_df_index_entries(idx);
    for (int i = 0; i < entries; i++) {
        if (isJim(ico_df_index_entry_name(idx, i))) {
            total += recordsOf(ico_df_index_entry_size(idx, i));
        }
    }
    return total;
}

typedef struct Walk {
    TexpackWalkFn fn;
    void *user;
    TexpackWalkStats *st;
    TexpackWalkItem *item;
    int pass;       /* TEXPACK_WALK_TM2 or TEXPACK_WALK_JIM_MEMBER */
    int nextMember; /* the next member the pass wants (for the unreadable rest) */
    int stop;
} Walk;

static int wants(const Walk *w, const char *name)
{
    return w->pass == TEXPACK_WALK_TM2 ? isTm2(name) : isJim(name);
}

static int hand(Walk *w)
{
    w->st->items++;
    if (w->fn(w->user, w->item) != 0) {
        w->stop = 1;
    }
    return w->stop;
}

static int unreadable(Walk *w, const char *name, int kind)
{
    memset(w->item, 0, sizeof(*w->item));
    setMember(w->item, name, kind);
    w->item->bad = TEXPACK_WALK_UNREADABLE;
    w->st->unreadable++;
    return hand(w);
}

static int onMember(void *user, int member, const char *name, const IcoDfMember *m,
                    const uint8_t *data)
{
    Walk *w = user;
    w->nextMember = member + 1;
    if (!wants(w, name)) {
        return 0;
    }
    if (w->pass == TEXPACK_WALK_TM2) {
        texpack_walk_tim2(name, TEXPACK_WALK_TM2, data, m->size, w->item);
        w->st->tm2++;
        return hand(w);
    }
    int stopped = 0;
    w->st->jimFiles++;
    int n = texpack_walk_records(name, TEXPACK_WALK_JIM_MEMBER, data, m->size, w->fn, w->user,
                                 &stopped);
    w->st->jimRecords += n;
    w->st->items += n;
    w->stop = stopped;
    return stopped;
}

/* one pass over the packs: the members w->pass wants, a pack inflated
   only when it holds one */
static void packPass(Walk *w, IcoDfIndex *idx)
{
    int entries = ico_df_index_entries(idx);
    for (int p = 0; p < entries && !w->stop; p++) {
        int first = 0;
        int count = ico_df_index_pack_members(idx, p, &first);
        int any = 0;
        for (int k = 0; k < count && !any; k++) {
            any = wants(w, ico_df_index_member_name(idx, first + k));
        }
        if (!any) {
            continue;
        }
        w->nextMember = first;
        if (ico_df_index_read_pack(idx, p, onMember, w) < 0) {
            /* the members from the failed one on would not inflate */
            for (int i = w->nextMember; i < first + count && !w->stop; i++) {
                const char *name = ico_df_index_member_name(idx, i);
                if (wants(w, name)) {
                    if (w->pass == TEXPACK_WALK_JIM_MEMBER) {
                        w->st->jimFiles++;
                    }
                    unreadable(w, name, w->pass);
                }
            }
        }
    }
}

int texpack_walk_disc(IcoVfs *vfs, IcoDfIndex *idx, TexpackWalkFn fn, void *user,
                      TexpackWalkStats *stats)
{
    TexpackWalkStats local;
    Walk w;
    IcoDfIndex *own = NULL;

    if (stats == NULL) {
        stats = &local;
    }
    memset(stats, 0, sizeof(*stats));
    if (fn == NULL) {
        return -1;
    }
    if (idx == NULL) {
        idx = own = ico_df_index_open(vfs);
        if (idx == NULL) {
            return -1;
        }
    }
    memset(&w, 0, sizeof(w));
    w.fn = fn;
    w.user = user;
    w.st = stats;
    w.item = malloc(sizeof(*w.item));
    if (w.item == NULL) {
        ico_df_index_close(own);
        return -1;
    }

    /* 1. the .tm2 members */
    w.pass = TEXPACK_WALK_TM2;
    packPass(&w, idx);

    /* 2. the subtitles, loose in DATA.DF */
    int entries = ico_df_index_entries(idx);
    for (int i = 0; i < entries && !w.stop; i++) {
        const char *name = ico_df_index_entry_name(idx, i);
        if (!isJim(name)) {
            continue;
        }
        int64_t n = ico_df_index_entry_size(idx, i);
        uint8_t *d = n > 0 ? malloc((size_t)n) : NULL;
        stats->jimFiles++;
        if (d == NULL || ico_df_index_read_entry(idx, i, 0, d, (size_t)n) != n) {
            free(d);
            unreadable(&w, name, TEXPACK_WALK_JIM_LOOSE);
            continue;
        }
        int stopped = 0;
        int k =
            texpack_walk_records(name, TEXPACK_WALK_JIM_LOOSE, d, (size_t)n, fn, user, &stopped);
        stats->jimRecords += k;
        stats->items += k;
        w.stop = stopped;
        free(d);
    }

    /* 3. the subtitles in the packs */
    w.pass = TEXPACK_WALK_JIM_MEMBER;
    if (!w.stop) {
        packPass(&w, idx);
    }

    stats->stopped = w.stop;
    free(w.item);
    ico_df_index_close(own);
    return w.stop ? 1 : 0;
}

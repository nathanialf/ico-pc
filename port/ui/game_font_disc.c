/*
 * port/ui/game_font_disc.c
 *
 * The game face from the player's disc (game_font.h; docs/port/UI.md, "The
 * font", and docs/port/DATA.md, "Backend 2: the archive"): the menu sheets
 * read from DATA.DF's packs (df_pack.h) and decoded (TIM2), the word
 * rectangles of the menu text table handed to the builder with their
 * transcribed words in the five languages, and the result kept beside the
 * extracted archive in the per-user folder (UI_GF_FILE_FMT: the format
 * version and the disc's SHA-1 in the name).
 *
 * Which sheet a rectangle is on: the table's first texProperty row that draws
 * it names a texFile row (its texFileNo); that path's language folder
 * (text/menu_PAL_FR/...) is replaced by each language's, as the game loads
 * the current language's sheet by the base name (layout_texture.c
 * lt_texture_no_of_property).  title.tm2 is one sheet for every language.
 */
#include "game_font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "charFileManager.h" /* texFile */
#include "df_pack.h"
#include "font.h"
#include "host_fs.h"
#include "menu_text.h"
#include "strings.h"
#include "gen/table_desc.h"
#include "vfs.h"
#include "../include/ico_endian.h"

static const char *const s_langDir[5] = {"EG", "FR", "GR", "IT", "SP"};

/* ------------------------------------------------------------- TIM2 */

/* GS alpha (0x80 opaque) to 0..255 */
static uint8_t gsAlpha(unsigned a)
{
    const unsigned v = a * 255u / 0x80u;
    return (uint8_t)(v > 255u ? 255u : v);
}

static void clutColour(const uint8_t *raw, size_t rawSize, unsigned i, int bits, uint8_t out[4])
{
    memset(out, 0, 4);
    if (bits == 32 && (size_t)i * 4 + 4 <= rawSize) {
        out[0] = raw[i * 4];
        out[1] = raw[i * 4 + 1];
        out[2] = raw[i * 4 + 2];
        out[3] = gsAlpha(raw[i * 4 + 3]);
    } else if (bits == 24 && (size_t)i * 3 + 3 <= rawSize) {
        out[0] = raw[i * 3];
        out[1] = raw[i * 3 + 1];
        out[2] = raw[i * 3 + 2];
        out[3] = 255;
    } else if (bits == 16 && (size_t)i * 2 + 2 <= rawSize) {
        const unsigned v = ico_le16(raw + i * 2);
        out[0] = (uint8_t)((v & 31u) << 3);
        out[1] = (uint8_t)(((v >> 5) & 31u) << 3);
        out[2] = (uint8_t)(((v >> 10) & 31u) << 3);
        out[3] = (v & 0x8000u) ? 255 : 0;
    }
}

/* The first picture of a TIM2 file as RGBA8 (malloc'd): PSMT4 / PSMT8 with a
   16-, 24- or 32-bit CLUT (CSM1 blocks swapped for 8-bit), or direct 16 / 24
   / 32-bit. */
static uint8_t *tim2Decode(const uint8_t *d, size_t n, int *w, int *h)
{
    if (n < 0x10 + 0x30 || memcmp(d, "TIM2", 4) != 0) {
        return NULL;
    }
    const size_t p = d[5] == 1 ? 0x80 : 0x10;
    if (p + 0x30 > n) {
        return NULL;
    }
    const uint8_t *hd = d + p;
    const uint32_t clutSize = ico_le32(hd + 4), imageSize = ico_le32(hd + 8);
    const unsigned headerSize = ico_le16(hd + 12), clutColours = ico_le16(hd + 14);
    /* {total, clutSize, imageSize, headerSize, clutColours, format, mips,
       clutType, imageType, width, height} */
    const unsigned clutType = hd[18], imageType = hd[19];
    const int pw = (int)ico_le16(hd + 20), ph = (int)ico_le16(hd + 22);
    if (pw <= 0 || ph <= 0 || pw > 4096 || ph > 4096 ||
        p + headerSize + (size_t)imageSize + (size_t)clutSize > n) {
        return NULL;
    }
    const uint8_t *img = hd + headerSize;
    const uint8_t *clut = img + imageSize;
    uint8_t *out = calloc((size_t)pw * (size_t)ph, 4);
    if (!out) {
        return NULL;
    }
    const size_t texels = (size_t)pw * (size_t)ph;
    if (imageType == 4 || imageType == 5) {
        const int cbits = (clutType & 0x3F) == 1 ? 16 : (clutType & 0x3F) == 2 ? 24 : 32;
        uint8_t pal[256][4];
        const unsigned nc = clutColours > 256 ? 256 : clutColours;
        memset(pal, 0, sizeof(pal));
        for (unsigned i = 0; i < nc; i++) {
            unsigned j = i;
            if (imageType == 5 && !(clutType & 0x80) && nc >= 32) {
                /* CSM1: 8..15 and 16..23 of each 32 swapped */
                const unsigned k = i & 31u;
                j = (k >= 8 && k < 16) ? i + 8 : (k >= 16 && k < 24) ? i - 8 : i;
            }
            clutColour(clut, clutSize, j, cbits, pal[i]);
        }
        if ((imageType == 5 && imageSize < texels) ||
            (imageType == 4 && imageSize < (texels + 1) / 2)) {
            free(out);
            return NULL;
        }
        for (size_t k = 0; k < texels; k++) {
            const unsigned ix = imageType == 5 ? img[k]
                                : (k & 1)      ? (img[k >> 1] >> 4)
                                               : (img[k >> 1] & 15u);
            if (ix < nc) {
                memcpy(out + k * 4, pal[ix], 4);
            }
        }
    } else if (imageType >= 1 && imageType <= 3) {
        const int bits = imageType == 1 ? 16 : imageType == 2 ? 24 : 32;
        if (imageSize < texels * (size_t)(bits / 8)) {
            free(out);
            return NULL;
        }
        for (size_t k = 0; k < texels; k++) {
            clutColour(img, imageSize, (unsigned)k, bits, out + k * 4);
        }
    } else {
        free(out);
        return NULL;
    }
    *w = pw;
    *h = ph;
    return out;
}

/* ------------------------------------------------------------ sheets */

typedef struct Sheet {
    char name[UI_GF_SHEET_NAME];
    uint8_t *rgba;
    int w, h;
    int id; /* the builder's */
} Sheet;

#define MAX_SHEETS 40

static Sheet *sheetFor(IcoVfs *vfs, UiGfBuilder *b, Sheet *sh, int *nsh, const char *name)
{
    for (int i = 0; i < *nsh; i++) {
        if (strcmp(sh[i].name, name) == 0) {
            return sh[i].rgba ? &sh[i] : NULL;
        }
    }
    if (*nsh == MAX_SHEETS) {
        return NULL;
    }
    Sheet *s = &sh[(*nsh)++];
    memset(s, 0, sizeof(*s));
    snprintf(s->name, sizeof(s->name), "%s", name);
    IcoDfMember m;
    if (ico_df_find_member(vfs, name, &m) != 0) {
        fprintf(stderr, "ui: game font: %s is not on the disc\n", name);
        return NULL;
    }
    uint8_t *raw = malloc(m.size ? m.size : 1);
    if (!raw || ico_df_read_member(vfs, &m, raw) != 0) {
        free(raw);
        fprintf(stderr, "ui: game font: %s cannot be read\n", name);
        return NULL;
    }
    s->rgba = tim2Decode(raw, m.size, &s->w, &s->h);
    free(raw);
    if (!s->rgba) {
        fprintf(stderr, "ui: game font: %s does not decode\n", name);
        return NULL;
    }
    s->id = ui_GfBuilderSheet(b, name);
    if (s->id < 0) {
        free(s->rgba);
        s->rgba = NULL;
        fprintf(stderr, "ui: game font: out of memory naming %s\n", name);
        return NULL;
    }
    return s;
}

/* the sheet of language lang for a texFile path */
static void sheetName(const char *path, int lang, char *out, size_t size)
{
    const char *dir = strstr(path, "menu_PAL_");
    snprintf(out, size, "%s", path);
    if (dir && dir[9] && dir[10] && dir[11] == '/') {
        const size_t at = (size_t)(dir - path) + 9;
        if (at + 2 < size) {
            out[at] = s_langDir[lang][0];
            out[at + 1] = s_langDir[lang][1];
        }
    }
}

/* texFile's rows (the table descriptor's count: the array is defined in
   port/data/gen/table_defs.c, its size unknown here) */
static int texFileCount(void)
{
    for (uint32_t i = 0; i < ico_table_row_count; i++) {
        const IcoTableRow *r = &ico_table_rows[i];
        if (r->host == (void *)texFile && r->record) {
            return (int)r->count;
        }
    }
    return 0;
}

int ui_GameFontBuild(IcoVfs *vfs, uint8_t **blob, size_t *size, UiGfStats *stats, char *why,
                     size_t whysize)
{
    Sheet sh[MAX_SHEETS];
    int nsh = 0, rc = -1, added = 0;
    UiGfBuilder *b = ui_GfBuilderNew();
    if (!b || !vfs) {
        snprintf(why, whysize, "%s", b ? "no disc" : "out of memory");
        ui_GfBuilderFree(b);
        return -1;
    }
    const int texFiles = texFileCount();
    for (int i = 0; i < ui_menu_text_item_count; i++) {
        const UiMenuTextItem *it = &ui_menu_text_items[i];
        const LtProperty *e = NULL;
        for (int r = 0; r < ui_menu_text_row_count && !e; r++) {
            if (ui_menu_text_rows[r].item == i) {
                e = &texProperty[ui_menu_text_rows[r].row];
            }
        }
        if (!e || e->texU != it->u || e->texV != it->v || e->texW != it->w || e->texH != it->h ||
            e->texFileNo < 0 || e->texFileNo >= texFiles) {
            continue;
        }
        const char *path = texFile[e->texFileNo].path;
        for (int lang = 0; lang < 5; lang++) {
            char name[UI_GF_SHEET_NAME];
            sheetName(path, lang, name, sizeof(name));
            const Sheet *s = sheetFor(vfs, b, sh, &nsh, name);
            if (!s) {
                continue;
            }
            UiGfSource src;
            memset(&src, 0, sizeof(src));
            src.rgba = s->rgba;
            src.sheetW = s->w;
            src.sheetH = s->h;
            src.u = it->u;
            src.v = it->v;
            src.w = it->w;
            src.h = it->h;
            src.dark = it->ink == UI_INK_DARK;
            src.em = it->em;
            src.capMid = it->y[lang];
            src.pitch = it->pitch;
            src.text = ui_StrIn((UiLang)lang, (UiStrId)it->str);
            src.sheet = s->id;
            src.lang = lang;
            if (ui_GfBuilderAdd(b, &src) == 0) {
                added++;
            }
        }
    }
    if (added == 0) {
        snprintf(why, whysize, "no sheet of the menu text table could be read");
    } else if (ui_GfBuilderFinish(b, 13.5f, blob, size, stats) != 0) {
        snprintf(why, whysize, "the sheets' letters could not be segmented");
    } else {
        rc = 0;
    }
    for (int i = 0; i < nsh; i++) {
        free(sh[i].rgba);
    }
    ui_GfBuilderFree(b);
    return rc;
}

/* the sidecar's bytes (malloc'd), or NULL */
static uint8_t *readFile(const char *path, size_t *size)
{
    FILE *f = ico_fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    uint8_t *buf = NULL;
    long n = -1;
    if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) > 0 && n <= UI_GF_FILE_MAX &&
        fseek(f, 0, SEEK_SET) == 0) {
        buf = malloc((size_t)n);
        if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
            free(buf);
            buf = NULL;
        }
    }
    fclose(f);
    *size = buf ? (size_t)n : 0;
    return buf;
}

int ui_GameFontWriteFile(const char *path, const void *blob, size_t size, char *why, size_t whysize)
{
    char tmp[1100];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) {
        snprintf(why, whysize, "the path is too long");
        return -1;
    }
    FILE *f = ico_fopen(tmp, "wb");
    if (!f) {
        snprintf(why, whysize, "cannot create %s", tmp);
        return -1;
    }
    const bool wrote = fwrite(blob, 1, size, f) == size;
    const bool synced = wrote && ico_fsync(f) == 0;
    if (fclose(f) != 0 || !synced) {
        ico_remove(tmp);
        snprintf(why, whysize, "cannot write %s", tmp);
        return -1;
    }
    if (ico_rename_replace(tmp, path) != 0) {
        ico_remove(tmp);
        snprintf(why, whysize, "cannot move %s over %s", tmp, path);
        return -1;
    }
    return 0;
}

int ui_GameFontPrepare(const char *cachePath)
{
    char why[1200];
    if (cachePath) {
        size_t n = 0;
        uint8_t *item = readFile(cachePath, &n);
        if (item) {
            const bool ok = ui_GameFaceLoad(item, n);
            free(item);
            if (ok) {
                fprintf(stderr, "ui: the game's lettering from %s (%zu bytes)\n", cachePath, n);
                return 0;
            }
            fprintf(stderr, "ui: %s does not load: extracting the game's lettering again\n",
                    cachePath);
        } else {
            fprintf(stderr, "ui: no %s: extracting the game's lettering from the disc\n",
                    cachePath);
        }
    }
    uint8_t *blob = NULL;
    size_t size = 0;
    UiGfStats st;
    memset(&st, 0, sizeof(st));
    const clock_t t0 = clock();
    if (ui_GameFontBuild(ico_vfs_disc(), &blob, &size, &st, why, sizeof(why)) != 0) {
        fprintf(stderr,
                "ui: the game's lettering could not be extracted (%s); the text is Arimo's\n", why);
        return -1;
    }
    fprintf(stderr,
            "ui: the game's lettering: %d rectangles, %d lines (%d one component a letter, %d with "
            "%d touching pairs split, %d left out), %d letters measured, %d characters, %d kerning "
            "pairs, %zu bytes, %.2f s\n",
            st.sources, st.lines, st.exact, st.aligned, st.splits, st.unaligned, st.instances,
            st.glyphs, st.kerns, size, (double)(clock() - t0) / CLOCKS_PER_SEC);
    const bool ok = ui_GameFaceLoad(blob, size);
    if (ok && cachePath) {
        if (ui_GameFontWriteFile(cachePath, blob, size, why, sizeof(why)) == 0) {
            fprintf(stderr, "ui: the game's lettering written to %s\n", cachePath);
        } else {
            fprintf(stderr,
                    "ui: the game's lettering not kept (%s); it is extracted again next "
                    "time\n",
                    why);
        }
    }
    free(blob);
    return ok ? 0 : -1;
}

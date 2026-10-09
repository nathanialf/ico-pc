/*
 * port/ui/test/menu_sheets.c
 *
 * The game's menu sheets for the menu_look test: TIM2 pictures read from the
 * player's disc (DATA.DF's packs, df_pack.h) and decoded to RGBA8 (grey and
 * the GS alpha scaled to 0..255), by name, with the sheet of a language for a
 * texFile path.  Test-only: the port itself does not read the sheets (the
 * menus are drawn in Arimo).
 *
 * Which sheet a rectangle is on: the table's first texProperty row that draws
 * it names a texFile row (its texFileNo); that path's language folder
 * (text/menu_PAL_FR/...) is replaced by each language's, as the game loads
 * the current language's sheet by the base name (layout_texture.c
 * lt_texture_no_of_property).  title.tm2 is one sheet for every language.
 */
#include "menu_sheets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "charFileManager.h" /* texFile */
#include "df_pack.h"
#include "gen/table_desc.h"
#include "../../include/ico_endian.h"

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

#define MAX_SHEETS 40

static MsSheet s_sheets[MAX_SHEETS];
static int s_nsheets;

const MsSheet *ms_SheetFor(IcoVfs *vfs, const char *name)
{
    for (int i = 0; i < s_nsheets; i++) {
        if (strcmp(s_sheets[i].name, name) == 0) {
            return s_sheets[i].rgba ? &s_sheets[i] : NULL;
        }
    }
    if (s_nsheets == MAX_SHEETS) {
        return NULL;
    }
    MsSheet *s = &s_sheets[s_nsheets++];
    memset(s, 0, sizeof(*s));
    snprintf(s->name, sizeof(s->name), "%s", name);
    IcoDfMember m;
    if (ico_df_find_member(vfs, name, &m) != 0) {
        fprintf(stderr, "menu_look: %s is not on the disc\n", name);
        return NULL;
    }
    uint8_t *raw = malloc(m.size ? m.size : 1);
    if (!raw || ico_df_read_member(vfs, &m, raw) != 0) {
        free(raw);
        fprintf(stderr, "menu_look: %s cannot be read\n", name);
        return NULL;
    }
    s->rgba = tim2Decode(raw, m.size, &s->w, &s->h);
    free(raw);
    if (!s->rgba) {
        fprintf(stderr, "menu_look: %s does not decode\n", name);
        return NULL;
    }
    return s;
}

void ms_SheetsFree(void)
{
    for (int i = 0; i < s_nsheets; i++) {
        free(s_sheets[i].rgba);
    }
    memset(s_sheets, 0, sizeof(s_sheets));
    s_nsheets = 0;
}

/* the sheet of language lang for a texFile path */
void ms_SheetName(const char *path, int lang, char *out, size_t size)
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
int ms_TexFileCount(void)
{
    for (uint32_t i = 0; i < ico_table_row_count; i++) {
        const IcoTableRow *r = &ico_table_rows[i];
        if (r->host == (void *)texFile && r->record) {
            return (int)r->count;
        }
    }
    return 0;
}

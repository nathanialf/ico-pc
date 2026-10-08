/* menu_text_test.c: the game's menu words and the port's rows drawn as text
 * in the sheets' look (packages P3, TXT2; v0.4.2 F-B).
 *
 * Without a device:
 *   - the table (menu_text.h): every row a
 *     texProperty index with a non-empty texel rectangle, every item used,
 *     the anchors inside their rectangles;
 *   - with the user's disc (argv[1], the PAL image; skipped when absent):
 *     every table row is the texProperty row of the boot ELF with that
 *     rectangle, on a text sheet;
 *   - every string id exists in all five languages and is drawable;
 *   - the hook through the real layout_texture.c (GifPacket.c,
 *     DisplayList.c, DmaPacket.c as the window build has them, the rest of
 *     the game stubbed, rows shaped like the PAL title's), v0.4.2 (package
 *     F-B): every table row draws a sheet strip (menu_font.h) and no
 *     texture sprite, a row outside the table (the copyright line) or with
 *     another rectangle keeps its sprite, every row's texture is still
 *     transferred; the port rows are strips too; the save screens' figures
 *     are strips on the plain pages in their inks' colours;
 *   - a port row on a game row's box puts its capitals where the sheet's
 *     lettering has them;
 *   - no deferral: under the Enhanced preset the layout records no
 *     RDC_OVERLAY_TEXT item or op and no tagged glyph quads, everything in
 *     the scene list at 1x.
 * Then on a Vulkan device (exit 77 without one; lavapipe here): a port row
 * "New Game" in the title's place through exec_layout_texture into SCENE:
 * the text covers pixels only inside the row's rectangle (with the rim's
 * margin), none elsewhere.  Writes menu_text_scene.png beside itself.
 *
 * Exit 0, 1 on a mismatch, 77 when there is no device (after the CPU checks).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "vk/rhi_vk.h"
/* the game's side */
#include "typedef.h"
#include "main.h"
#include "layout_texture.h"
#include "layout_action.h"
#include "charFileManager.h"
#include "StageManager.h"
#include "DisplayList.h"
#include "DmaPacket.h"
#include "GifHost.h"
#include "GifPacket.h"
/* port/ui */
#include "font.h"
#include "layout_ext.h"
#include "menu_font.h"
#include "menu_text.h"
#include "strings.h"
#include "ui_internal.h"

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

/* ------------------------------------- what the game files import (stubs) */
int ScreenWidth = 512, ScreenHeight = 512;
float center_X = 2048.0f, center_Y = 2048.0f;
int screenOffsetX, screenOffsetY;
int fbKeep;
void *ios_partition_common;
void *dmaVif;

/* the game's tables: rows shaped like the PAL title's, no disc data */
LtProp texLayout[LT_GAME_LAYOUT_COUNT];
LtProperty texProperty[LT_GAME_PROPERTY_COUNT];
TexRec texFile[1];
const StgPre stageData[1];
PadState pad[16];
StageSetting GlobalStageSetting;
int gFlagGameClear;
int systemStatus[12] = {1, 1};
int frame_count = 100;
int layout_boot_flag;
int title_demo_mode;
unsigned int stage_after_skipping_demo;
int mpegPlayReturnStage;

static int s_texTransfers;

void tex_TransTexture(int no, int pri)
{
    (void)no;
    (void)pri;
    s_texTransfers++;
}

int tex_GetTextureNo(char *name)
{
    (void)name;
    return 0;
}

void *tex_GetTextureData(int no)
{
    (void)no;
    return NULL;
}

void tex_SetSamplingType(void *t, int mag, int min)
{
    (void)t;
    (void)mag;
    (void)min;
}

void soundSeDefPlay(int no, unsigned int a, int b, int c)
{
    (void)no;
    (void)a;
    (void)b;
    (void)c;
}

void gflagInit(void) {}

void *iosMallocDebug(void *part, int size, const char *file, int line)
{
    (void)part;
    (void)file;
    (void)line;
    return calloc(1, (size_t)size);
}

void iosFree(void *p)
{
    free(p);
}

void debug_StdPrintfDummy(const char *fmt, ...)
{
    (void)fmt;
}

void debug_assert(const char *file, int line)
{
    printf("debug_assert %s:%d\n", file, line);
    abort();
}

void ico_assert(const char *file, int line, const char *e)
{
    printf("assert %s:%d %s\n", file, line, e);
    abort();
}

void __assert(const char *file, int line, const char *e)
{
    printf("__assert %s:%d %s\n", file, line, e);
    abort();
}

void mc_Reset(void) {}

void ui_SettingsInstall(void) {}

float GetTableSin(short angle)
{
    return sinf((float)angle * 3.14159265f / 32768.0f);
}

float GetTableCos(short angle)
{
    return cosf((float)angle * 3.14159265f / 32768.0f);
}

void FlushCache(int op)
{
    (void)op;
}

void sceDmaSend(void *ch, void *addr)
{
    (void)ch;
    (void)addr;
}

/* ---------------------------------------------------------------- table */

static const UiMenuTextItem *itemOfRow(int row)
{
    for (int i = 0; i < ui_menu_text_row_count; i++) {
        if (ui_menu_text_rows[i].row == row) {
            return &ui_menu_text_items[ui_menu_text_rows[i].item];
        }
    }
    return NULL;
}

static void testTable(void)
{
    CHECK(ui_menu_text_item_count > 0 && ui_menu_text_row_count >= ui_menu_text_item_count,
          "%d items, %d rows", ui_menu_text_item_count, ui_menu_text_row_count);
    int *used = calloc((size_t)ui_menu_text_item_count, sizeof(int));
    for (int i = 0; i < ui_menu_text_row_count; i++) {
        const UiMenuTextRow *r = &ui_menu_text_rows[i];
        CHECK(r->row >= 0 && r->row < LT_GAME_PROPERTY_COUNT, "row %d is a texProperty index",
              r->row);
        CHECK(i == 0 || r->row > ui_menu_text_rows[i - 1].row, "rows sorted, once each (%d)",
              r->row);
        CHECK(r->item >= 0 && r->item < ui_menu_text_item_count, "row %d item %d", r->row, r->item);
        if (r->item >= 0 && r->item < ui_menu_text_item_count && used) {
            used[r->item]++;
        }
    }
    for (int i = 0; i < ui_menu_text_item_count; i++) {
        const UiMenuTextItem *it = &ui_menu_text_items[i];
        CHECK(used && used[i] > 0, "item %d is drawn by a row", i);
        CHECK(it->w > 0 && it->h > 0 && it->u + it->w <= 512 && it->v + it->h <= 256,
              "item %d: a non-empty rectangle on a sheet (%u,%u %ux%u)", i, it->u, it->v, it->w,
              it->h);
        /* package TXT: a digit tile's figure fills it (capitals 13 of 15
           texels: an em of 18) */
        for (int l = 0; l < UI_LANG_COUNT; l++) {
            CHECK(it->x[l] >= 0.0f && it->x[l] <= (float)it->w && it->em[l] > 0.0f &&
                      it->em[l] <= (float)it->h * 1.25f && it->wx[l] > 0.7f && it->wx[l] < 1.4f,
                  "item %d language %d: anchor %g, em %g, width %g inside %ux%u", i, l, it->x[l],
                  it->em[l], it->wx[l], it->w, it->h);
        }
        CHECK(it->ink <= UI_INK_GREY, "item %d: ink %u", i, it->ink);
        CHECK(it->align == UI_ALIGN_LEFT || it->align == UI_ALIGN_CENTER ||
                  it->align == UI_ALIGN_RIGHT,
              "item %d: alignment %u", i, it->align);
        for (int l = 0; l < UI_LANG_COUNT; l++) {
            const char *s = ui_StrIn((UiLang)l, (UiStrId)it->str);
            int lines = 1;
            for (const char *p = s; *p; p++) {
                lines += *p == '\n';
            }
            const float last = it->y[l] + (float)(lines - 1) * it->pitch;
            CHECK(it->y[l] > 0.0f && last < (float)it->h + 0.5f,
                  "item %d language %d: %d lines from %g by %g inside %u texels", i, l, lines,
                  it->y[l], it->pitch, it->h);
        }
    }
    free(used);
    printf("menu_text_test: %d rows from %d texel rectangles\n", ui_menu_text_row_count,
           ui_menu_text_item_count);
}

static void testStrings(void)
{
    for (int i = 0; i < ui_menu_text_item_count; i++) {
        const int id = ui_menu_text_items[i].str;
        CHECK(id > UI_STR_NONE && id < UI_STR_COUNT, "item %d: string id %d", i, id);
        for (int l = 0; l < UI_LANG_COUNT; l++) {
            /* each language's own table, no fallback to English */
            const char *const *tables[UI_LANG_COUNT] = {ui_strings_en, ui_strings_fr, ui_strings_de,
                                                        ui_strings_it, ui_strings_es};
            const char *s = tables[l][id];
            CHECK(s != NULL && s[0] != '\0', "item %d: string %d missing in language %d", i, id, l);
            uint32_t cp;
            while (s && (cp = ui_Utf8Next(&s)) != 0) {
                CHECK(cp == '\n' || (cp != 0xFFFD && ui_FontHasGlyph(cp)),
                      "string %d language %d: U+%04X not drawable", id, l, cp);
            }
        }
    }
    CHECK(strcmp(ui_StrIn(UI_LANG_EN, UI_STR_MT_NEW_GAME), "New Game") == 0 &&
              strcmp(ui_StrIn(UI_LANG_DE, UI_STR_MT_NEW_GAME), "Neues Spiel") == 0,
          "New Game / Neues Spiel");
}

/* ------------------------------------------------------- the disc's rows */

/* texProperty in the PAL boot ELF (port/data/gen/table_desc.c,
   "tex-property": 436 rows of 0x70 bytes); texFileNo, texU, texH, texW and
   texV at 0x58..0x68 (layout_texture.h) */
#define EE_TEX_PROPERTY 0x0030CFF8u
#define EE_ROW_SIZE 0x70u

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* SCES_507.60 from the image's root directory (ISO9660), malloc'd */
static uint8_t *readBootElf(const char *iso, size_t *size)
{
    FILE *f = fopen(iso, "rb");
    if (!f) {
        return NULL;
    }
    uint8_t pvd[2048];
    uint8_t *elf = NULL;
    if (fseek(f, 16L * 2048, SEEK_SET) == 0 && fread(pvd, 1, 2048, f) == 2048 && pvd[0] == 1 &&
        memcmp(pvd + 1, "CD001", 5) == 0) {
        const uint8_t *root = pvd + 156;
        uint32_t lsn = le32(root + 2), len = le32(root + 10);
        uint8_t *dir = malloc(len);
        if (dir && fseek(f, (long)lsn * 2048, SEEK_SET) == 0 && fread(dir, 1, len, f) == len) {
            for (uint32_t o = 0; o < len;) {
                const uint8_t *e = dir + o;
                if (e[0] == 0) {
                    o = (o / 2048 + 1) * 2048;
                    continue;
                }
                if (e[32] >= 11 && memcmp(e + 33, "SCES_507.60", 11) == 0) {
                    uint32_t flsn = le32(e + 2), fsz = le32(e + 10);
                    elf = malloc(fsz);
                    if (elf && (fseek(f, (long)flsn * 2048, SEEK_SET) != 0 ||
                                fread(elf, 1, fsz, f) != fsz)) {
                        free(elf);
                        elf = NULL;
                    }
                    *size = fsz;
                    break;
                }
                o += e[0];
            }
        }
        free(dir);
    }
    fclose(f);
    return elf;
}

/* the file offset of an EE address (the PT_LOAD segment holding it), 0 if none */
static size_t elfOffset(const uint8_t *elf, size_t size, uint32_t addr, uint32_t len)
{
    if (size < 52 || memcmp(elf,
                            "\x7f"
                            "ELF",
                            4) != 0) {
        return 0;
    }
    uint32_t phoff = le32(elf + 28);
    uint32_t phentsize = elf[42] | elf[43] << 8, phnum = elf[44] | elf[45] << 8;
    for (uint32_t i = 0; i < phnum; i++) {
        const uint8_t *ph = elf + phoff + i * phentsize;
        if ((size_t)(ph - elf) + 32 > size || le32(ph) != 1) {
            continue;
        }
        uint32_t off = le32(ph + 4), vaddr = le32(ph + 8), filesz = le32(ph + 16);
        if (addr >= vaddr && addr + len <= vaddr + filesz && off + (addr - vaddr) + len <= size) {
            return off + (addr - vaddr);
        }
    }
    return 0;
}

static void testDiscRows(const char *iso)
{
    size_t size = 0;
    uint8_t *elf = iso ? readBootElf(iso, &size) : NULL;
    if (!elf) {
        printf("menu_text_test: SKIP the disc check: no disc image at %s\n", iso ? iso : "(none)");
        return;
    }
    size_t base = elfOffset(elf, size, EE_TEX_PROPERTY, LT_GAME_PROPERTY_COUNT * EE_ROW_SIZE);
    CHECK(base != 0, "texProperty in the boot ELF");
    if (base) {
        for (int i = 0; i < ui_menu_text_row_count; i++) {
            const UiMenuTextRow *r = &ui_menu_text_rows[i];
            const UiMenuTextItem *it = &ui_menu_text_items[r->item];
            const uint8_t *p = elf + base + (size_t)r->row * EE_ROW_SIZE;
            uint32_t file = le32(p + 0x58), u = le32(p + 0x5C), h = le32(p + 0x60),
                     w = le32(p + 0x64), v = le32(p + 0x68);
            CHECK(u == it->u && v == it->v && w == it->w && h == it->h && w > 0 && h > 0,
                  "row %d: the disc's rectangle %u,%u %ux%u, the table's %u,%u %ux%u", r->row, u, v,
                  w, h, it->u, it->v, it->w, it->h);
            /* a text sheet: scei, title, menu_PAL_01..04 (texture-path's 0..5,
               10..29), not buttons (8), exp (9) or a TEX/ sign */
            CHECK(file <= 5 || (file >= 10 && file <= 29), "row %d: texFile %u is a text sheet",
                  r->row, file);
        }
        printf("menu_text_test: %d rows match the disc's texProperty\n", ui_menu_text_row_count);
    }
    free(elf);
}

/* ------------------------------------------------------------- the hook */

typedef struct Walk {
    int n;
    const RdCmd *cmd[256];
    RdStateBlock st[256];
} Walk;

static void collect(void *user, int list, uint32_t index, const RdCmd *c, const RdStateBlock *s)
{
    Walk *w = user;
    (void)index;
    if (list == 11 && c->type == RDC_SCREEN && c->b[0] == RD_PRIM_SPRITES && w->n < 256) {
        w->cmd[w->n] = c;
        w->st[w->n] = *s;
        w->n++;
    }
}

/* a game row from the table's item for row, placed as the PAL row is */
static void setRow(int row, int dispX, int dispY, int dispH, int centerX)
{
    LtProperty *e = &texProperty[row];
    memset(e, 0, sizeof(*e));
    e->word0 = -1;
    e->ownerItem = -1;
    e->up = e->down = e->left = e->right = -1;
    e->rightItem = e->leftItem = e->upItem = e->downItem = -1;
    e->dispX = dispX;
    e->dispY = dispY;
    e->dispH = dispH;
    e->centerX = centerX;
    e->selectable = 1;
    const UiMenuTextItem *it = itemOfRow(row);
    if (it) {
        e->texU = it->u;
        e->texV = it->v;
        e->texW = it->w;
        e->texH = it->h;
    }
}

#define TITLE_LAYOUT 13

/* the title as the PAL tables have it: the copyright line (48, not in the
   table), Continue (49) and New Game (50) */
static void buildTitle(int withCopyright)
{
    memset(texLayout, 0, sizeof(texLayout));
    setRow(48, 0, 195, 40, 1);
    texProperty[48].texU = 0;
    texProperty[48].texV = 205;
    texProperty[48].texW = 512;
    texProperty[48].texH = 20;
    setRow(49, 0, 135, 40, 1);
    setRow(50, 0, 165, 40, 1);
    texProperty[49].downItem = 50;
    texProperty[50].upItem = 49;
    LtProp *l = &texLayout[TITLE_LAYOUT];
    l->first = withCopyright ? 48 : 50;
    l->last = 51;
    l->defaultItem = l->curItem = 50;
    l->link = -1;
    current_layout_id = TITLE_LAYOUT;
}

static void layoutFrame(void)
{
    dl_SetDLPriority(0);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
    exec_layout_texture();
    dl_Swap();
}

/* a port row (layout_ext.c) with label text at (dispX, dispY) in a dispW x
   dispH box (centred when centerX), selectable */
static int addPortRow(const char *text, int dispX, int dispY, int dispW, int dispH, int centerX,
                      int align)
{
    LtProperty pr;
    memset(&pr, 0, sizeof(pr));
    pr.word0 = -1;
    pr.ownerItem = -1;
    pr.up = pr.down = pr.left = pr.right = -1;
    pr.rightItem = pr.leftItem = pr.upItem = pr.downItem = -1;
    pr.dispX = dispX;
    pr.dispY = dispY;
    pr.dispW = dispW;
    pr.dispH = dispH;
    pr.centerX = centerX;
    pr.selectable = 1;
    LtExtText t = {0, text, UI_MENU_TEXT_SIZE, align};
    return lt_ext_AddProperty(&pr, &t);
}

/* rows first..last (port indices) as a layout without a cursor, linked
   after the title layout so the title draws them */
static void linkPortRows(int first, int last)
{
    LtProp pl;
    memset(&pl, 0, sizeof(pl));
    pl.first = first;
    pl.last = last + 1;
    pl.defaultItem = pl.curItem = -1;
    pl.link = -1;
    texLayout[TITLE_LAYOUT].link = lt_ext_AddLayout(&pl);
}

/* the title's game rows and, below New Game, two port rows on the title's
   pitch (as settings.c placeTitle adds Settings and Quit to desktop) */
static void buildTitleWithPortRows(int withCopyright)
{
    lt_ext_Reset();
    buildTitle(withCopyright);
    const int a = addPortRow("Settings", 0, 185, 0, 40, 1, UI_ALIGN_CENTER);
    const int b = addPortRow("Quit to desktop", 0, 205, 0, 40, 1, UI_ALIGN_CENTER);
    linkPortRows(a, b);
}

/* the sheet strips and the texture sprites of list 11, the backdrop left
   out: a strip is a sprite batch from a menu text page (menu_font.h), a
   texture row one sprite (two vertices) from anything else.  (The stubbed
   tex_TransTexture sends no TEX0, so a texture sprite may record whatever
   texture came before it.)  cols: the strips' vertex colours, in order */
static void countSprites(int *strips, int *texture, uint8_t cols[][4], int maxCols)
{
    const RdFrame *f = rd__LastFrame();
    Walk *w = calloc(1, sizeof(Walk));
    RdStateBlock s = f->startState;
    rd__Walk(f, 0, &s, collect, w);
    *strips = *texture = 0;
    for (int i = 0; i < w->n; i++) {
        const int page = w->st[i].ds.texEnabled && ui_MenuFontIsPage(w->st[i].tex);
        if (page && w->cmd[i]->u[1] == 2) {
            if (cols && *strips < maxCols) {
                const RdScreenVtx *v = (const RdScreenVtx *)(f->payload + w->cmd[i]->u[0]);
                memcpy(cols[*strips], v[1].rgba, 4);
            }
            (*strips)++;
        } else if (!page && w->cmd[i]->u[1] == 2 && w->cmd[i]->b[1] != RD_SPACE_FULLSCREEN) {
            (*texture)++;
        }
    }
    free(w);
}

/* ------------------------------------ scaled strips (v0.4.2, package F-G) */

typedef struct StripQuad {
    int32_t x0, y0, x1, y1; /* GS 12.4 */
    float du, dv;           /* the UV's span, texels */
} StripQuad;

/* the last frame's strip sprites, in order */
static int stripQuads(StripQuad *q, int max)
{
    const RdFrame *f = rd__LastFrame();
    Walk *w = calloc(1, sizeof(Walk));
    RdStateBlock st = f->startState;
    rd__Walk(f, 0, &st, collect, w);
    int n = 0;
    for (int i = 0; i < w->n && n < max; i++) {
        if (w->st[i].ds.texEnabled && ui_MenuFontIsPage(w->st[i].tex) && w->cmd[i]->u[1] == 2) {
            const RdScreenVtx *v = (const RdScreenVtx *)(f->payload + w->cmd[i]->u[0]);
            q[n].x0 = v[0].x;
            q[n].y0 = v[0].y;
            q[n].x1 = v[1].x;
            q[n].y1 = v[1].y;
            q[n].du = (v[1].s - v[0].s) / 16.0f;
            q[n].dv = (v[1].t - v[0].t) / 16.0f;
            n++;
        }
    }
    free(w);
    return n;
}

/* The strips at the scene's scale: forced to 2, the title's game rows and
   port rows are strips of twice the texels across and down, drawn with the
   same quads, their UVs spanning twice the texels; back at the scene's own
   scale (1 here) the strips are the 1x ones again */
static void testScaledStrips(void)
{
    enum { MAXQ = 16 };

    StripQuad a[MAXQ], b[MAXQ];
    buildTitleWithPortRows(1);
    ui__MenuForceScale(1);
    layoutFrame();
    const int na = stripQuads(a, MAXQ);
    UiMenuStrip one;
    const int drawn1 = ui_MenuFontLastStrip(&one);
    ui__MenuForceScale(2);
    layoutFrame();
    const int nb = stripQuads(b, MAXQ);
    UiMenuStrip last;
    const int drawn = ui_MenuFontLastStrip(&last);
    int same = 0, doubled = 0;
    for (int i = 0; i < na && i < nb; i++) {
        same +=
            a[i].x0 == b[i].x0 && a[i].y0 == b[i].y0 && a[i].x1 == b[i].x1 && a[i].y1 == b[i].y1;
        doubled +=
            fabsf(b[i].du - 2.0f * a[i].du) < 1e-3f && fabsf(b[i].dv - 2.0f * a[i].dv) < 1e-3f;
    }
    printf("menu_text_test: scale 2: %d strips (%d at 1x), %d with the 1x quad, %d with twice the "
           "texels; the last %d x %d at scale %d\n",
           nb, na, same, doubled, last.w, last.h, last.scale);
    CHECK(na == 4 && nb == na && same == na && doubled == na,
          "scale 2: %d strips (%d at 1x), %d quads the 1x ones, %d UVs twice the 1x ones", nb, na,
          same, doubled);
    CHECK(drawn && drawn1 && one.scale == 1 && last.scale == 2 && last.w == 2 * one.w &&
              last.h == 2 * one.h,
          "scale 2: the last strip %d x %d at scale %d (%d x %d at 1)", last.w, last.h, last.scale,
          one.w, one.h);
    ui__MenuForceScale(0);
    layoutFrame();
    CHECK(ui_MenuFontLastStrip(&last) && last.scale == 1 && last.w == one.w && last.h == one.h,
          "back at 1x: %d x %d at scale %d", last.w, last.h, last.scale);
    lt_ext_Reset();
}

/* ------------------------------------- no deferral (v0.4.2, package F-B) */

typedef struct TextWalk {
    int items, ops, tagged, atlas;
} TextWalk;

static void textWalk(void *user, int list, uint32_t index, const RdCmd *c, const RdStateBlock *s)
{
    TextWalk *w = user;
    (void)index;
    if (list != 11) {
        return;
    }
    if (c->type == RDC_OVERLAY_TEXT && c->b[0] == RD_OTEXT_ITEM) {
        w->items++;
    } else if (c->type == RDC_OVERLAY_TEXT && c->b[0] == RD_OTEXT_OP) {
        w->ops++;
    } else if (c->type == RDC_SCREEN) {
        w->tagged += c->b[3] == RD_SCREEN_TEXT_QUADS;
        for (int px = 1; px < 64; px++) {
            for (int pg = 0; pg < 4; pg++) {
                const uint32_t t = ui_FontPageTex(px, pg);
                w->atlas += s->ds.texEnabled && t != 0 && t == s->tex;
            }
        }
    }
}

static void walkText(TextWalk *w)
{
    memset(w, 0, sizeof(*w));
    const RdFrame *f = rd__LastFrame();
    RdStateBlock s = f->startState;
    rd__Walk(f, f->keep, &s, textWalk, w);
}

/* the layout frame with a fade after it in list 11, as gsb_PostEffect
   appends it */
static void layoutFrameWithFade(int fade)
{
    dl_SetDLPriority(0);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
    exec_layout_texture();
    dl_SetDLPriority(11);
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.rgba[3] = (uint8_t)fade;
    rd_Post(RD_POST_FADE, &pp);
    dl_Swap();
}

/* Everything the layout draws is in the scene list at 1x in every preset:
   under the Enhanced preset at 1920 x 1080 the title with its port rows
   records no RDC_OVERLAY_TEXT item or op, no glyph quads tagged for a
   present to skip and no Arimo atlas draw: the port rows and the game's
   menu words are sheet strips (four), the copyright line its texture */
static void testNoDeferral(void)
{
    const RdSettings saved = g_rd.settings;
    g_rd.settings.preset = RD_PRESET_ENHANCED;
    g_rd.settings.outputWidth = 1920;
    g_rd.settings.outputHeight = 1080;
    ui_SetScale(ui_ScaleFor(1, 1080));
    buildTitleWithPortRows(1);
    layoutFrameWithFade(0x40);
    TextWalk w;
    walkText(&w);
    int strips = 0, texture = 0;
    countSprites(&strips, &texture, NULL, 0);
    CHECK(w.items == 0 && w.ops == 0 && w.tagged == 0 && w.atlas == 0,
          "Enhanced: %d text items, %d ops, %d tagged batches, %d atlas draws (0, 0, 0, 0)",
          w.items, w.ops, w.tagged, w.atlas);
    CHECK(strips == 4 && texture == 1,
          "Enhanced: %d strips, %d texture sprites (4: two port rows, Continue, New Game; 1: the "
          "copyright line)",
          strips, texture);
    printf("menu_text_test: no deferral: Enhanced 1080p records %d strips, %d texture sprite, "
           "%d text items\n",
           strips, texture, w.items);
    ui_SetScale(1.0f);
    g_rd.settings = saved;
}

/* ------------------------------------------------- the save figures */

/* the strings visitor reaches the menu words; the subtitles are not port
   strings (the game draws them as its pictures) */
static int s_visits, s_visitSub;

static void visit(UiLang lang, const char *s, void *user)
{
    (void)user;
    s_visits++;
    s_visitSub += lang == UI_LANG_IT && strcmp(s, "Prendi la spada") == 0;
}

/* the save screens' values: a play-time digit (row 76, plain ink), an
   empty file's slot number (52, grey) and a used file's (62, dark), all in
   the menu text table, are sheet strips on the plain pages (no rim), in
   their inks' colours (the row's 0x7F: grey 151 / 255 of it, dark black,
   plain as it is), their textures still transferred */
static void testDigits(void)
{
    lt_ext_Reset();
    memset(texLayout, 0, sizeof(texLayout));
    setRow(52, 210, 70, 30, 0);
    setRow(62, 210, 90, 30, 0);
    setRow(76, 240, 160, 30, 0);
    for (int r = 52; r <= 76; r += 1) {
        texProperty[r].selectable = 0;
    }
    LtProp *l = &texLayout[TITLE_LAYOUT];
    l->first = 52;
    l->last = 77;
    l->defaultItem = l->curItem = -1;
    l->link = -1;
    for (int r = 53; r < 76; r++) {
        if (r != 62) {
            texProperty[r].masked = 1;
        }
    }
    for (int r = 52; r < 77; r++) {
        texProperty[r].defaultMask = texProperty[r].masked; /* kept by a layout switch */
    }
    current_layout_id = TITLE_LAYOUT;
    s_texTransfers = 0;
    layoutFrame();
    int strips = 0, texture = 0;
    uint8_t cols[3][4];
    memset(cols, 0, sizeof(cols));
    countSprites(&strips, &texture, cols, 3);
    UiMenuStrip last;
    CHECK(itemOfRow(52) && itemOfRow(62) && itemOfRow(76), "the three rows are in the table");
    CHECK(strips == 3 && texture == 0 && s_texTransfers == 3,
          "three digit rows: %d strips, %d texture sprites, %d transfers (3, 0, 3)", strips,
          texture, s_texTransfers);
    CHECK(ui_MenuFontLastStrip(&last) && last.cls == 2 && last.w == 20 && last.h == 15,
          "the digits on the plain pages, a strip the tile's 20 x 15 (%d, %d x %d)", last.cls,
          last.w, last.h);
    CHECK(cols[0][0] == 75 && cols[1][0] == 0 && cols[2][0] == 0x7F,
          "the inks' colours: grey %u, dark %u, plain %u (75, 0, 127)", cols[0][0], cols[1][0],
          cols[2][0]);
    for (int r = 52; r < 77; r++) {
        texProperty[r].masked = texProperty[r].defaultMask = 0;
    }
    printf("menu_text_test: digits: %d strips, %d texture sprites\n", strips, texture);
}

/* A port row placed on a game menu row's box, with the same label and
   size, puts its capitals where the sheet's lettering has them: OK (181, a
   20-texel row whose capitals sit at texel 9.0) and a port row "OK" at size
   27 in the same 20-field-line box.  The sheet's capital middle is mapped
   through the sprite's box and texels as display_texture draws them; the
   port row's anchor is snapped to whole texels (two y units), so within a
   y unit.  OK itself, a table row, is a strip of its item's rectangle drawn
   with the sprite's box */
static void testPortRowAnchor(void)
{
    memset(texLayout, 0, sizeof(texLayout));
    lt_ext_Reset();
    setRow(181, 270, 100, 40, 0);
    LtProp *l = &texLayout[TITLE_LAYOUT];
    l->first = 181;
    l->last = 182;
    l->defaultItem = l->curItem = 181;
    l->link = -1;
    current_layout_id = TITLE_LAYOUT;
    const int port = addPortRow("OK", 270, 100, 100, 40, 0, UI_ALIGN_LEFT);
    linkPortRows(port, port);
    layoutFrame();
    const UiMenuTextItem *it = itemOfRow(181);
    UiMenuStrip last;
    const int drawn = ui_MenuFontLastStrip(&last);
    CHECK(drawn && it, "the port row drawn last as a strip (%d)", drawn);
    if (!drawn || !it) {
        lt_ext_Reset();
        return;
    }
    /* display_texture's sprite: box y (dispY - 113) * 16 + 4, h dispH * 8 - 16
       (1/16 field line); texels v * 16 + 8, h * 16 - 16 (1/16 texel) */
    const LtProperty *e = &texProperty[181];
    const float by = (float)((e->dispY - 113) * 16 + 4) / 8.0f + UI_GRID_CY;
    const float bh = (float)(e->dispH * 8 - 16) / 8.0f;
    const float sy = bh * 16.0f / (float)(e->texH * 16 - 16);
    const float oy = by - 8.0f / 16.0f * sy;
    const float gameY = oy + it->y[UI_LANG_EN] * sy;
    CHECK(fabsf(last.anchorY - gameY) <= 1.0f && last.cls == 0,
          "the port row's capitals at y %.3f, the sheet's at %.3f (light ink)", last.anchorY,
          gameY);
    printf("menu_text_test: port row vs the sheet: capitals at %.3f and %.3f\n", last.anchorY,
           gameY);
    lt_ext_Reset();
}

static void testHook(void)
{
    CHECK(itemOfRow(50) && itemOfRow(49) && !itemOfRow(48),
          "New Game and Continue in the table, the copyright line not");
    if (!rd__InitRecordOnly(512, 512)) {
        CHECK(0, "rd__InitRecordOnly");
        return;
    }
    ui_FontForgetTextures();
    ui__SetRecordHook(gif_HostFlush);
    dl_Init();
    lt_ext_Reset();
    GlobalStageSetting.reductionCol[0] = GlobalStageSetting.reductionCol[1] =
        GlobalStageSetting.reductionCol[2] = 0x80;
    pad[0].ana[2] = pad[0].ana[3] = 128;
    pad[0].flags = 0;

    /* v0.4.2 (package F-B): every table row is a sheet strip and no texture
       sprite; the copyright line (not in the table) keeps its sprite; every
       row's texture is still transferred */
    buildTitle(1);
    CHECK(lt_ext_IsTextRow(&texProperty[50]) && lt_ext_IsTextRow(&texProperty[49]) &&
              !lt_ext_IsTextRow(&texProperty[48]),
          "New Game and Continue are text rows, the copyright line not");
    CHECK(ui_MenuTextItemOf(&texProperty[50]) == itemOfRow(50) &&
              ui_MenuTextItemOf(&texProperty[48]) == NULL,
          "ui_MenuTextItemOf: New Game's item, none for the copyright line");
    s_texTransfers = 0;
    layoutFrame();
    int strips = 0, texture = 0;
    countSprites(&strips, &texture, NULL, 0);
    CHECK(s_texTransfers == 3, "every row's texture transferred (%d of 3)", s_texTransfers);
    CHECK(strips == 2 && texture == 1, "game rows: %d strips, %d texture sprites (2, 1)", strips,
          texture);
    UiMenuStrip last;
    CHECK(ui_MenuFontLastStrip(&last) && last.cls == 0 && last.w == 172 && last.h == 20,
          "New Game: a strip of its item's 172 x 20 texels on the light pages (%d x %d, %d)",
          last.w, last.h, last.cls);
    printf("menu_text_test: game rows: %d strips, %d texture sprites, %d transfers\n", strips,
           texture, s_texTransfers);

    /* not the PAL rectangle: the row is its texture again */
    texProperty[50].texW = 171;
    CHECK(!lt_ext_IsTextRow(&texProperty[50]), "a rectangle not the table's: the texture");
    texProperty[50].texW = 172;

    /* with two port rows linked: those are strips too */
    buildTitleWithPortRows(1);
    s_texTransfers = 0;
    layoutFrame();
    countSprites(&strips, &texture, NULL, 0);
    CHECK(s_texTransfers == 3 && strips == 4 && texture == 1,
          "with port rows: %d transfers, %d strips, %d texture sprites (3, 4, 1)", s_texTransfers,
          strips, texture);
    testNoDeferral();
    testScaledStrips();
    testPortRowAnchor();
    testDigits();
    CHECK(gif_HostUndecodedTotal() == 0, "%u undecoded writes", gif_HostUndecodedTotal());
    ui__SetRecordHook(NULL);
    rd_Shutdown();
    ui_FontForgetTextures();
}

/* --------------------------------------------------------------- pixels */

static const uint8_t kBg[4] = {20, 40, 60, 0x80};

static int toPixX(float gx)
{
    return (int)floorf((2048.0f * 16.0f + (gx - UI_GRID_CX) * 16.0f * 512.0f / 640.0f) / 16.0f) -
           1792;
}

static int toPixY(float gy)
{
    return (int)floorf((2048.0f * 16.0f + (gy - UI_GRID_CY) * 8.0f * 512.0f / 224.0f) / 16.0f) -
           1792;
}

static void testPixels(void)
{
    ui__SetRecordHook(gif_HostFlush);
    dl_Init();
    /* a port row "New Game" in the title's New Game box (row 50 itself is a
       texture, which the stubbed tex_TransTexture leaves unbound) */
    lt_ext_Reset();
    buildTitle(0);
    texProperty[50].masked = texProperty[50].defaultMask = 1;
    {
        const int r = addPortRow("New Game", 0, texProperty[50].dispY, texProperty[50].texW,
                                 texProperty[50].dispH, 1, UI_ALIGN_CENTER);
        linkPortRows(r, r);
    }
    /* the frame: SCENE cleared in list 0, the layout in list 11 */
    rd_SelectList(0);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), kBg, 1, 0);
    layoutFrame();
    uint32_t w = 0, h = 0;
    uint8_t *px = malloc(512 * 512 * 4);
    if (!px || !rd__ReadTarget(rd_Target(RD_TARGET_SCENE), px, 512 * 512 * 4, &w, &h) || w != 512) {
        CHECK(0, "SCENE readback");
        free(px);
        ui__SetRecordHook(NULL);
        return;
    }
    rd_WritePng("menu_text_scene.png", px, 512, 512, 512 * 4, 0);
    /* the row's box (display_texture: centred, New Game's 172 texels
       wide, 20 field lines from dispY 165), plus the reach of the sheet
       strip's rim and its bilinear read: a texel each, 2 x units and 4 y
       units, and a unit for the anchor's snap */
    const LtProperty *e = &texProperty[50];
    const float gx0 = UI_GRID_CX - (float)e->texW * 0.5f, gx1 = UI_GRID_CX + (float)e->texW * 0.5f;
    const float gy0 = (float)e->dispY * 2.0f, gy1 = gy0 + (float)e->dispH;
    const float mx = 3.0f, my = 5.0f;
    const int x0 = toPixX(gx0 - mx), x1 = toPixX(gx1 + mx);
    const int y0 = toPixY(gy0 - my), y1 = toPixY(gy1 + my);
    int inside = 0, outside = 0, bright = 0;
    for (int y = 0; y < 512; y++) {
        for (int x = 0; x < 512; x++) {
            const uint8_t *p = &px[(y * 512 + x) * 4];
            if (p[0] == kBg[0] && p[1] == kBg[1] && p[2] == kBg[2]) {
                continue;
            }
            if (x >= x0 && x <= x1 && y >= y0 && y <= y1) {
                inside++;
                bright += p[0] > 200 && p[1] > 200 && p[2] > 200;
            } else {
                outside++;
            }
        }
    }
    CHECK(inside > 300, "New Game covers %d pixels in its rectangle", inside);
    CHECK(bright > 50, "light letters: %d near-white pixels", bright);
    CHECK(outside == 0, "%d pixels drawn outside the row's rectangle", outside);
    printf("menu_text_test: New Game in %d,%d-%d,%d: %d pixels (%d near white), %d outside\n", x0,
           y0, x1, y1, inside, bright, outside);
    free(px);
    texProperty[50].masked = texProperty[50].defaultMask = 0;
    lt_ext_Reset();
    ui__SetRecordHook(NULL);
}

int main(int argc, char **argv)
{
    ui_SetLanguage(UI_LANG_EN);
    testTable();
    testStrings();
    ui_StringsForEach(visit, NULL);
    CHECK(s_visits > 5 * 300 && s_visitSub == 0,
          "ui_StringsForEach: %d strings, the subtitles not among them (%d)", s_visits, s_visitSub);
    testDiscRows(argc > 1 ? argv[1] : NULL);
    testHook();
    if (failures) {
        printf("menu_text_test: %d failures\n", failures);
        return 1;
    }

    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ORIGINAL;
    if (!rd_Init(512, 512, &st, NULL)) {
        printf("menu_text_test: CPU checks ok; SKIP the pixel check: no usable Vulkan device\n");
        return 77;
    }
    ui_FontForgetTextures();
    UiGsFrame fr = {512, 512, 2048.0f, 2048.0f, UI_LAYOUT_Z};
    ui_SetGsFrame(&fr);
    ui_SetScale(1.0f);
    testPixels();
    CHECK(rhi_vk_ValidationErrorCount() == 0, "%u validation errors",
          rhi_vk_ValidationErrorCount());
    ui_FontShutdown();
    rd_Shutdown();
    if (failures) {
        printf("menu_text_test: %d failures\n", failures);
        return 1;
    }
    printf("menu_text_test: ok\n");
    return 0;
}

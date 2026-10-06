/* menu_text_test.c: the game's menu words keep their texels; the port's
 * rows are text (packages P3, TXT2; docs/port/UI.md, "Menu text").
 *
 * Without a device:
 *   - the table (the game face's source, menu_text.h): every row a
 *     texProperty index with a non-empty texel rectangle, every item used,
 *     the anchors inside their rectangles;
 *   - with the user's disc (argv[1], the PAL image; skipped when absent):
 *     every table row is the texProperty row of the boot ELF with that
 *     rectangle, on a text sheet;
 *   - every string id exists in all five languages and is drawable;
 *   - the subtitle transcriptions (test/subtitles.h, test data for the font
 *     coverage corpus): sorted, one or two lines, drawable, the lookup;
 *   - the hook through the real layout_texture.c (GifPacket.c,
 *     DisplayList.c, DmaPacket.c as the window build has them, the rest of
 *     the game stubbed, rows shaped like the PAL title's): every game row,
 *     in the table or not, is its texture sprite and records no text item;
 *     every row's texture is transferred; the save screens' figures too;
 *   - a port row on a game row's box puts its capitals where the sheet's
 *     lettering has them;
 *   - package DEF: each port row records an RDC_OVERLAY_TEXT item before its
 *     glyph quads, which carry RD_SCREEN_TEXT_QUADS; the present's deferred
 *     renderer lays the items out at 1920 x 1080; a fade after the rows
 *     records an op after the items; a keep frame's rows before its KEEP
 *     give nothing; the game rows alone record neither items nor ops.
 * Then on a Vulkan device (exit 77 without one; lavapipe here): a port row
 * "New Game" in the title's place through exec_layout_texture into SCENE:
 * the text covers pixels only inside the row's rectangle (with the rim's
 * margin), none elsewhere.  Writes menu_text_scene.png beside itself.  With
 * gamefont.bin beside it (font_coverage writes it from the disc; package
 * GFONT) the row is drawn in the game's own lettering.
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
#include "menu_text.h"
#include "strings.h"
#include "subtitles.h"
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
        CHECK(it->x >= 0.0f && it->x <= (float)it->w && it->em > 0.0f &&
                  it->em <= (float)it->h * 1.25f,
              "item %d: anchor %g, em %g inside %ux%u", i, it->x, it->em, it->w, it->h);
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

/* the text and the texture sprites of list 11, the backdrop left out */
static void countSprites(int *text, int *texture)
{
    const RdFrame *f = rd__LastFrame();
    Walk *w = calloc(1, sizeof(Walk));
    RdStateBlock s = f->startState;
    rd__Walk(f, 0, &s, collect, w);
    *text = *texture = 0;
    for (int i = 0; i < w->n; i++) {
        int atlas = 0;
        for (int px = 1; px < 64 && !atlas; px++) {
            for (int pg = 0; pg < 4 && !atlas; pg++) {
                uint32_t t = ui_FontPageTex(px, pg);
                atlas = t != 0 && w->st[i].tex == t;
            }
        }
        /* a texture row is one sprite (two vertices); a label one sprite a
           glyph from an atlas page.  (The stubbed tex_TransTexture sends no
           TEX0, so a texture sprite may record whatever texture came
           before it.) */
        if (atlas && w->cmd[i]->u[1] > 2) {
            (*text)++;
        } else if (w->cmd[i]->u[1] == 2 && w->cmd[i]->b[1] != RD_SPACE_FULLSCREEN) {
            (*texture)++;
        }
    }
    free(w);
}

/* ------------------------------------------- deferred text (package DEF) */

typedef struct TextWalk {
    int items, ops, tagged, taggedVerts, untaggedAtlas, opAfterItems;
    int opKind;
    RdTextItem item[8];
} TextWalk;

static int isAtlas(uint32_t tex)
{
    for (int px = 1; px < 64; px++) {
        for (int pg = 0; pg < 4; pg++) {
            const uint32_t t = ui_FontPageTex(px, pg);
            if (t != 0 && t == tex) {
                return 1;
            }
        }
    }
    return 0;
}

static void textWalk(void *user, int list, uint32_t index, const RdCmd *c, const RdStateBlock *s)
{
    TextWalk *w = user;
    const RdFrame *f = rd__LastFrame();
    (void)index;
    if (list != 11) {
        return;
    }
    if (c->type == RDC_OVERLAY_TEXT && c->b[0] == RD_OTEXT_ITEM) {
        if (w->items < 8) {
            memcpy(&w->item[w->items], f->payload + c->u[1], sizeof(RdTextItem));
        }
        w->items++;
    } else if (c->type == RDC_OVERLAY_TEXT && c->b[0] == RD_OTEXT_OP) {
        w->ops++;
        w->opKind = c->b[1];
        w->opAfterItems = w->items;
    } else if (c->type == RDC_SCREEN && s->ds.texEnabled && isAtlas(s->tex)) {
        if (c->b[3] == RD_SCREEN_TEXT_QUADS) {
            w->tagged++;
            w->taggedVerts += (int)c->u[1];
        } else {
            w->untaggedAtlas++;
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

/* the layout frame with a fade (alpha a) after it in list 11, as
   gsb_PostEffect appends it; keep: KEEP after the rows and a keep frame */
static void layoutFrameWithPosts(int fade, int keep)
{
    fbKeep = keep;
    dl_SetDLPriority(0);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
    exec_layout_texture();
    dl_SetDLPriority(11);
    RdPostParams pp;
    if (keep) {
        memset(&pp, 0, sizeof(pp));
        rd_Post(RD_POST_KEEP, &pp);
    }
    if (fade) {
        memset(&pp, 0, sizeof(pp));
        pp.rgba[3] = (uint8_t)fade;
        rd_Post(RD_POST_FADE, &pp);
    }
    dl_Swap();
    fbKeep = 0;
}

static int s_sinkPrims;

static void countSink(RdPrim type, const RdScreenVtx *v, uint32_t n, RdTex tex, RdBlend blend)
{
    (void)type;
    (void)v;
    (void)tex;
    (void)blend;
    s_sinkPrims += (int)n;
}

/* the prims the deferred renderer gives for the last frame at an Enhanced
   1920 x 1080 present (rd__OverlayCollect, its callback into font.c's
   overlay mode, the prims caught before rd_OverlayPrims) */
static int collectPrimsCrt(int crt)
{
    const RdSettings saved = g_rd.settings;
    g_rd.settings.preset = RD_PRESET_ENHANCED;
    g_rd.settings.outputWidth = 1920;
    g_rd.settings.outputHeight = 1080;
    rd_CrtSettings(&g_rd.settings, crt ? RD_CRT_TRINITRON : RD_CRT_OFF, 1.0f);
    s_sinkPrims = 0;
    ui_InstallDeferredText(1);
    ui__SetOverlaySink(countSink);
    const RdFrame *f = rd__LastFrame();
    rd__OverlayCollect(f, (int)f->keep);
    const int active = rd_DeferredTextActive();
    ui__SetOverlaySink(NULL);
    ui_InstallDeferredText(0);
    rd__OverlayCollect(NULL, 0); /* forget the batches */
    g_rd.settings = saved;
    return active ? s_sinkPrims : -1;
}

static int collectPrims(void)
{
    return collectPrimsCrt(0);
}

/* Package DEF: every port row records, in place before its glyph quads, an
   RDC_OVERLAY_TEXT item with its string, anchor, size and colour, and its
   quads carry RD_SCREEN_TEXT_QUADS (an Enhanced present skips them and
   draws the item on the output); the game's rows record neither (package
   TXT2: their textures); a fade after the rows records an op after the
   items, and none without text; a keep frame's rows before its KEEP give
   the present nothing, as their quads are drawn over */
static void testDeferred(void)
{
    buildTitleWithPortRows(1); /* the copyright line, Continue, New Game; two port rows */
    layoutFrame();
    TextWalk w;
    walkText(&w);
    CHECK(w.items == 2, "two port rows: %d items (2)", w.items);
    CHECK(w.tagged == 2 * 9 && w.untaggedAtlas == 0,
          "every glyph batch is an item's quads: %d tagged, %d not (18, 0)", w.tagged,
          w.untaggedAtlas);
    CHECK(w.ops == 0, "no post pass, no op (%d)", w.ops);
    int haveSet = 0, haveQuit = 0;
    for (int i = 0; i < w.items && i < 8; i++) {
        const RdTextItem *it = &w.item[i];
        haveSet |= strcmp(it->utf8, "Settings") == 0;
        haveQuit |= strcmp(it->utf8, "Quit to desktop") == 0;
        CHECK((it->flags & UI_HALO) && (it->flags & UI_ALIGN_MASK) == UI_ALIGN_CENTER &&
                  !it->additive && it->size > 1.0f && it->rgba[3] > 0,
              "item %d (\"%s\"): halo, centred, lerp, size %.1f, alpha %u", i, it->utf8, it->size,
              it->rgba[3]);
    }
    CHECK(haveSet && haveQuit, "the items are the port rows, not New Game or Continue");
    const int prims = collectPrims();
    /* the same glyphs (the eight halo copies and the letters, a sprite a
       glyph) as the quads */
    CHECK(prims == w.taggedVerts && prims > 0,
          "the present lays both items out: %d vertices (the quads have %d)", prims, w.taggedVerts);
    /* package CRT2: under the CRT filter nothing is deferred (the quads
       draw into the scene and go through the filter) */
    const int crtPrims = collectPrimsCrt(1);
    CHECK(crtPrims == -1, "the CRT filter on: no deferred text (%d)", crtPrims);

    /* a fade after the rows: an op after both items */
    layoutFrameWithPosts(0x40, 0);
    walkText(&w);
    CHECK(w.ops == 1 && w.opKind == RD_POST_FADE && w.opAfterItems == 2,
          "a fade after the rows: %d op(s) of kind %d after %d items (1, %d, 2)", w.ops, w.opKind,
          w.opAfterItems, RD_POST_FADE);

    /* a keep frame: the rows, then KEEP (gsb_PostEffect's order): the
       present lays nothing out (the quads are drawn over too) */
    layoutFrameWithPosts(0, 1);
    walkText(&w);
    CHECK(rd__LastFrame()->keep && w.items == 2 && w.ops == 1 && w.opKind == RD_POST_KEEP,
          "a keep frame: %d items, then KEEP (%d ops, kind %d)", w.items, w.ops, w.opKind);
    const int keptPrims = collectPrims();
    CHECK(keptPrims == 0, "a keep frame's rows before its KEEP give no prims (%d)", keptPrims);

    /* the game's rows alone: no items, no tagged quads, no ops */
    lt_ext_Reset();
    buildTitle(1);
    layoutFrameWithPosts(0x40, 0);
    walkText(&w);
    CHECK(w.items == 0 && w.tagged == 0 && w.ops == 0,
          "game rows alone: %d items, %d tagged batches, %d ops (0, 0, 0)", w.items, w.tagged,
          w.ops);
    printf("menu_text_test: deferred: 2 port items before 18 tagged batches, %d vertices laid out "
           "at 1920x1080; fade op after the items; keep frame 0; game rows none\n",
           prims);
}

/* ------------------------------------- the subtitles and the save figures */

/* every RDC_OVERLAY_TEXT item of the last frame in list `list`, with its key */
typedef struct ItemWalk {
    int n;
    RdTextItem item[8];
    uint64_t key[8];
} ItemWalk;

static void itemWalk(ItemWalk *w, int list)
{
    memset(w, 0, sizeof(*w));
    const RdFrame *f = rd__LastFrame();
    for (uint32_t i = 0; f && i < f->lists[list].count; i++) {
        const RdCmd *c = &f->lists[list].cmds[i];
        if (c->type == RDC_OVERLAY_TEXT && c->b[0] == RD_OTEXT_ITEM) {
            if (w->n < 8) {
                memcpy(&w->item[w->n], f->payload + c->u[1], sizeof(RdTextItem));
                w->key[w->n] = (uint64_t)c->keyLo | (uint64_t)c->keyHi << 32;
            }
            w->n++;
        }
    }
}

static int validUtf8Drawable(const char *s)
{
    uint32_t cp;
    while ((cp = ui_Utf8Next(&s)) != 0) {
        if (cp != '\n' && (cp == 0xFFFD || !ui_FontHasGlyph(cp))) {
            return 0;
        }
    }
    return 1;
}

/* the subtitle tables: sorted, one or two lines, every code point drawable,
   the centres on the strip; the lookup by language, set and block, with
   the picture kept (NULL) for Yorda's script and empty blocks */
static void testSubtitleTables(void)
{
    int total = 0;
    for (int l = 0; l < UI_LANG_COUNT; l++) {
        for (int set = 0; set < 2; set++) {
            int n = 0;
            const UiSubtitle *t = ui_SubtitleTable((UiLang)l, set, &n);
            CHECK(t && n >= 30, "language %d set %d: %d subtitles", l, set, n);
            for (int i = 0; t && i < n; i++) {
                int lines = 1;
                for (const char *p = t[i].text; *p; p++) {
                    lines += *p == '\n';
                }
                CHECK(i == 0 || t[i].block > t[i - 1].block, "language %d set %d: sorted at %d", l,
                      set, t[i].block);
                CHECK(t[i].block >= 0 && t[i].block < UI_SUB_BLOCKS && lines <= 2 && t[i].text[0] &&
                          validUtf8Drawable(t[i].text),
                      "language %d set %d block %d: \"%s\"", l, set, t[i].block, t[i].text);
                for (int k = 0; k < lines; k++) {
                    CHECK(t[i].x[k] > 32.0f && t[i].x[k] < UI_SUB_STRIP_W - 32.0f,
                          "language %d set %d block %d line %d: centre %g", l, set, t[i].block, k,
                          t[i].x[k]);
                }
                CHECK(ui_SubtitleFind((UiLang)l, set, t[i].block) == &t[i], "lookup of block %d",
                      t[i].block);
            }
            total += n;
        }
        const UiSubtitleFace *fc = ui_SubtitleFace((UiLang)l);
        CHECK(fc->em > 12.0f && fc->em < 20.0f && fc->y[0] < fc->y[1] && fc->y[1] < UI_SUB_STRIP_H,
              "language %d: face em %g, slots %g %g", l, fc->em, fc->y[0], fc->y[1]);
    }
    const UiSubtitle *a = ui_SubtitleFind(UI_LANG_EN, 0, 0);
    CHECK(a && strcmp(a->text, "Get the sword.") == 0, "English block 0");
    /* block 9 is Yorda's script on the first run, the Queen's words once
       the game is cleared */
    CHECK(ui_SubtitleFind(UI_LANG_EN, 0, 9) == NULL,
          "English first run block 9: no entry (Yorda's script)");
    a = ui_SubtitleFind(UI_LANG_EN, 1, 9);
    CHECK(a && strcmp(a->text, "Who are you ?\nHow did you get in here ?") == 0,
          "English after the clear, block 9");
    /* the French file keeps block 91 in Yorda's script after the clear */
    CHECK(ui_SubtitleFind(UI_LANG_FR, 1, 91) == NULL && ui_SubtitleFind(UI_LANG_DE, 1, 91),
          "block 91 after the clear: no French entry, German text");
    a = ui_SubtitleFind(UI_LANG_ES, 0, 2);
    CHECK(a && strcmp(a->text, "\xC2\xBFHay alguien ah\xC3\xAD? \xC2\xBFQui\xC3\xA9n eres?") == 0,
          "Spanish block 2");
    CHECK(ui_SubtitleFind(UI_LANG_EN, 0, 3) == NULL && ui_SubtitleFind(UI_LANG_EN, 0, -1) == NULL &&
              ui_SubtitleFind(UI_LANG_EN, 0, 115) == NULL &&
              ui_SubtitleFind(UI_LANG_EN, 2, 0) == NULL,
          "no entry: an empty block, out of range, a third set");
    printf("menu_text_test: %d subtitles over 5 languages and 2 sets\n", total);
}

/* the strings visitor reaches the menu words; the subtitles are not port
   strings (TXT2: test data) */
static int s_visits, s_visitSub;

static void visit(UiLang lang, const char *s, void *user)
{
    (void)user;
    s_visits++;
    s_visitSub += lang == UI_LANG_IT && strcmp(s, "Prendi la spada") == 0;
}

/* the save screens' values (package TXT2): a play-time digit (row 76), an
   empty file's slot number (52) and a used file's (62), all in the menu
   text table, are their texture sprites and record no text item */
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
    ItemWalk w;
    itemWalk(&w, 11);
    int text = 0, texture = 0;
    countSprites(&text, &texture);
    CHECK(itemOfRow(52) && itemOfRow(62) && itemOfRow(76), "the three rows are in the table");
    CHECK(w.n == 0 && text == 0 && texture == 3 && s_texTransfers == 3,
          "three digit rows: %d items, %d text batches, %d texture sprites, %d transfers "
          "(0, 0, 3, 3)",
          w.n, text, texture, s_texTransfers);
    for (int r = 52; r < 77; r++) {
        texProperty[r].masked = texProperty[r].defaultMask = 0;
    }
    printf("menu_text_test: digits: %d texture sprites, %d items\n", texture, w.n);
}

/* A port row placed on a game menu row's box, with the same label and
   size, puts its capitals where the sheet's lettering has them: OK (181, a
   20-texel row whose capitals sit at texel 9.0) and a port row "OK" at size
   27 in the same 20-field-line box.  The game row is its texture; the
   sheet's capital middle is mapped through the sprite's box and texels as
   display_texture draws them (docs/port/UI.md, open item 10). */
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
    ItemWalk w;
    itemWalk(&w, 11);
    const UiMenuTextItem *it = itemOfRow(181);
    CHECK(w.n == 1 && it, "one item (the port row): %d", w.n);
    if (w.n != 1 || !it) {
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
    CHECK(fabsf(w.item[0].y - gameY) < 0.01f && strcmp(w.item[0].utf8, "OK") == 0,
          "the port row's capitals at y %.3f, the sheet's at %.3f", w.item[0].y, gameY);
    printf("menu_text_test: port row vs the sheet: capitals at %.3f and %.3f\n", w.item[0].y,
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

    /* package TXT2: every game row is its texture, in the table or not */
    buildTitle(1);
    CHECK(!lt_ext_IsTextRow(&texProperty[50]) && !lt_ext_IsTextRow(&texProperty[48]),
          "New Game and the copyright line are not text rows");
    s_texTransfers = 0;
    layoutFrame();
    int text = 0, texture = 0;
    countSprites(&text, &texture);
    CHECK(s_texTransfers == 3, "every row's texture transferred (%d of 3)", s_texTransfers);
    CHECK(text == 0 && texture == 3, "game rows: %d text batches, %d texture sprites (0, 3)", text,
          texture);
    printf("menu_text_test: game rows: %d text batches, %d texture sprites, %d transfers\n", text,
           texture, s_texTransfers);

    /* with two port rows linked: those are text (per row eight halo copies
       and the letters), the game rows still their textures */
    buildTitleWithPortRows(1);
    s_texTransfers = 0;
    layoutFrame();
    countSprites(&text, &texture);
    CHECK(s_texTransfers == 3 && text == 2 * 9 && texture == 3,
          "with port rows: %d transfers, %d text batches, %d texture sprites (3, 18, 3)",
          s_texTransfers, text, texture);
    testDeferred();
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
       wide, 20 field lines from dispY 165), plus the rim's reach: Arimo's
       halo 1.5 y units; the game face's rim (GFONT) its glow, 6 texels round
       the letters (6 x units, 12 y units), as the sheet's own glow reaches
       past the letters */
    const LtProperty *e = &texProperty[50];
    const float gx0 = UI_GRID_CX - (float)e->texW * 0.5f, gx1 = UI_GRID_CX + (float)e->texW * 0.5f;
    const float gy0 = (float)e->dispY * 2.0f, gy1 = gy0 + (float)e->dispH;
    const float mx = ui_GameFaceLoaded() ? 7.0f : 2.0f, my = ui_GameFaceLoaded() ? 13.0f : 2.0f;
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

static void loadGameFace(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (!f) {
        printf("menu_text_test: no %s (no disc): the pixels in Arimo\n", p);
        return;
    }
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *blob = n > 0 ? malloc((size_t)n) : NULL;
    if (blob && fread(blob, 1, (size_t)n, f) == (size_t)n) {
        CHECK(ui_GameFaceLoad(blob, (size_t)n), "the game face in %s loads", p);
        printf("menu_text_test: the pixels in the game face from %s\n", p);
    }
    free(blob);
    fclose(f);
}

int main(int argc, char **argv)
{
    ui_SetLanguage(UI_LANG_EN);
    testTable();
    testStrings();
    testSubtitleTables();
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
    /* GFONT: the pixels in the game's lettering when font_coverage built it
       from the disc (gamefont.bin, fixture gamefont); the CPU checks above
       count Arimo's halo draws and ran without it */
    loadGameFace("gamefont.bin");
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

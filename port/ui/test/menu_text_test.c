/* menu_text_test.c: the game's menu text drawn with the port font (package
 * P3; docs/port/UI.md, "Menu text").
 *
 * Without a device:
 *   - the table: every row a texProperty index with a non-empty texel
 *     rectangle, every item used, the anchors inside their rectangles;
 *   - with the user's disc (argv[1], the PAL image; skipped when absent):
 *     every table row is the texProperty row of the boot ELF with that
 *     rectangle, on a text sheet;
 *   - every string id exists in all five languages and is drawable;
 *   - the hook through the real layout_texture.c (GifPacket.c,
 *     DisplayList.c, DmaPacket.c as the window build has them, the rest of
 *     the game stubbed, rows shaped like the PAL title's): a table row is
 *     drawn as atlas text and a row not in the table (the copyright line)
 *     as its texture sprite; every row's texture is still transferred;
 *     classic mode draws the textures; a row whose rectangle differs from
 *     the table's (tables that are not the PAL ones) keeps its texture;
 *   - package DEF: each text row records an RDC_OVERLAY_TEXT item before its
 *     glyph quads, which carry RD_SCREEN_TEXT_QUADS; the present's deferred
 *     renderer lays the items out at 1920 x 1080; a fade after the rows
 *     records an op after the items; a keep frame's rows before its KEEP
 *     give nothing; classic mode records neither items nor ops.
 * Then on a Vulkan device (exit 77 without one; lavapipe here): the title's
 * "New Game" row through exec_layout_texture into SCENE: the text covers
 * pixels only inside the row's rectangle (with the rim's margin), none
 * elsewhere.  Writes menu_text_scene.png beside itself.
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

/* the frame's atlas text batches in list 11, in draw order */
static int textBatches(const RdCmd **cmd, int max)
{
    const RdFrame *f = rd__LastFrame();
    Walk *w = calloc(1, sizeof(Walk));
    RdStateBlock s = f->startState;
    rd__Walk(f, 0, &s, collect, w);
    int n = 0;
    for (int i = 0; i < w->n && n < max; i++) {
        for (int px = 1; px < 64; px++) {
            if (w->st[i].tex == ui_FontPageTex(px, 0) && w->st[i].tex != 0) {
                cmd[n++] = w->cmd[i];
                break;
            }
        }
    }
    free(w);
    return n;
}

/* A port row (layout_ext.c) placed where a game menu row is, with the same
   label and size, draws its letters at the same height: OK (181, a
   20-texel row whose capitals sit at texel 9.0) and a port row "OK" at
   size 27 in the same 20-field-line box, drawn by one layout and its
   link.  Before the fix the port row centred the capitals in the box, a
   field line (2 y units) lower than the game's rows (docs/port/UI.md, open
   item 10). */
static void testPortRowAnchor(void)
{
    memset(texLayout, 0, sizeof(texLayout));
    lt_ext_Reset();
    setRow(181, 270, 100, 40, 0);
    LtProperty pr;
    memset(&pr, 0, sizeof(pr));
    pr.word0 = -1;
    pr.ownerItem = -1;
    pr.up = pr.down = pr.left = pr.right = -1;
    pr.rightItem = pr.leftItem = pr.upItem = pr.downItem = -1;
    pr.dispX = 270;
    pr.dispY = 100;
    pr.dispW = 100;
    pr.dispH = 40;
    pr.selectable = 1;
    LtExtText t = {0, "OK", UI_MENU_TEXT_SIZE, UI_ALIGN_LEFT};
    const int port = lt_ext_AddProperty(&pr, &t);
    LtProp pl;
    memset(&pl, 0, sizeof(pl));
    pl.first = port;
    pl.last = port + 1;
    pl.defaultItem = pl.curItem = -1;
    pl.link = -1;
    LtProp *l = &texLayout[TITLE_LAYOUT];
    l->first = 181;
    l->last = 182;
    l->defaultItem = l->curItem = 181;
    l->link = lt_ext_AddLayout(&pl);
    current_layout_id = TITLE_LAYOUT;
    layoutFrame();
    const RdCmd *cmd[32];
    const int n = textBatches(cmd, 32);
    CHECK(n == 18, "two text rows: %d atlas batches, expected 18", n);
    if (n != 18) {
        return;
    }
    /* the halo copies and the letters, batch by batch: the same glyphs at
       the same y (x differs: the game row's lettering starts 7 texels in) */
    const RdFrame *f = rd__LastFrame();
    int same = 1;
    float worst = 0.0f;
    for (int b = 0; b < 9; b++) {
        const RdScreenVtx *g = (const RdScreenVtx *)(f->payload + cmd[b]->u[0]);
        const RdScreenVtx *p = (const RdScreenVtx *)(f->payload + cmd[9 + b]->u[0]);
        same = same && cmd[b]->u[1] == cmd[9 + b]->u[1];
        for (uint32_t k = 0; same && k < cmd[b]->u[1]; k++) {
            const float d = fabsf((float)p[k].y - (float)g[k].y) / 16.0f;
            worst = d > worst ? d : worst;
        }
    }
    CHECK(same && worst <= 1.0f / 16.0f,
          "the port row's letters at the game row's height (worst %.3f GS pixels)", worst);
    printf("menu_text_test: port row vs game row OK: %d batches each, worst y difference %.3f\n",
           n / 2, worst);
    lt_ext_Reset();
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
static int collectPrims(void)
{
    const RdSettings saved = g_rd.settings;
    g_rd.settings.preset = RD_PRESET_ENHANCED;
    g_rd.settings.outputWidth = 1920;
    g_rd.settings.outputHeight = 1080;
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

/* Package DEF: every text row records, in place before its glyph quads, an
   RDC_OVERLAY_TEXT item with its string, anchor, size and colour, and its
   quads carry RD_SCREEN_TEXT_QUADS (an Enhanced present skips them and
   draws the item on the output); classic mode records neither; a fade after
   the rows records an op after the items, and none without text; a keep
   frame's rows before its KEEP give the present nothing, as their quads
   are drawn over */
static void testDeferred(void)
{
    ui_MenuTextSetClassic(0);
    buildTitle(1); /* the copyright line (a texture), Continue, New Game */
    layoutFrame();
    TextWalk w;
    walkText(&w);
    CHECK(w.items == 2, "two text rows: %d items (2)", w.items);
    CHECK(w.tagged == 2 * 9 && w.untaggedAtlas == 0,
          "every glyph batch is an item's quads: %d tagged, %d not (18, 0)", w.tagged,
          w.untaggedAtlas);
    CHECK(w.ops == 0, "no post pass, no op (%d)", w.ops);
    int haveNew = 0, haveCont = 0;
    for (int i = 0; i < w.items && i < 8; i++) {
        const RdTextItem *it = &w.item[i];
        haveNew |= strcmp(it->utf8, "New Game") == 0;
        haveCont |= strcmp(it->utf8, "Continue") == 0;
        CHECK((it->flags & UI_HALO) && (it->flags & UI_ALIGN_MASK) == UI_ALIGN_CENTER &&
                  !it->additive && it->size > 1.0f && it->rgba[3] > 0,
              "item %d (\"%s\"): halo, centred, lerp, size %.1f, alpha %u", i, it->utf8, it->size,
              it->rgba[3]);
    }
    CHECK(haveNew && haveCont, "the items are New Game and Continue");
    const int prims = collectPrims();
    /* the same glyphs (the eight halo copies and the letters, a sprite a
       glyph) as the quads */
    CHECK(prims == w.taggedVerts && prims > 0,
          "the present lays both items out: %d vertices (the quads have %d)", prims, w.taggedVerts);

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

    /* classic: no items, no tagged quads, no ops */
    ui_MenuTextSetClassic(1);
    layoutFrameWithPosts(0x40, 0);
    walkText(&w);
    CHECK(w.items == 0 && w.tagged == 0 && w.ops == 0,
          "classic: %d items, %d tagged batches, %d ops (0, 0, 0)", w.items, w.tagged, w.ops);
    ui_MenuTextSetClassic(0);
    printf("menu_text_test: deferred: 2 items before 18 tagged batches, %d vertices laid out at "
           "1920x1080; fade op after the items; keep frame 0; classic none\n",
           prims);
}

/* ------------------------------------------- package TXT: the game's text */

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
    CHECK(ui_SubtitleFind(UI_LANG_EN, 0, 9) == NULL, "English first run block 9 keeps its picture");
    a = ui_SubtitleFind(UI_LANG_EN, 1, 9);
    CHECK(a && strcmp(a->text, "Who are you ?\nHow did you get in here ?") == 0,
          "English after the clear, block 9");
    /* the French file keeps block 91 in Yorda's script after the clear */
    CHECK(ui_SubtitleFind(UI_LANG_FR, 1, 91) == NULL && ui_SubtitleFind(UI_LANG_DE, 1, 91),
          "block 91 after the clear: French picture, German text");
    a = ui_SubtitleFind(UI_LANG_ES, 0, 2);
    CHECK(a && strcmp(a->text, "\xC2\xBFHay alguien ah\xC3\xAD? \xC2\xBFQui\xC3\xA9n eres?") == 0,
          "Spanish block 2");
    CHECK(ui_SubtitleFind(UI_LANG_EN, 0, 3) == NULL && ui_SubtitleFind(UI_LANG_EN, 0, -1) == NULL &&
              ui_SubtitleFind(UI_LANG_EN, 0, 115) == NULL &&
              ui_SubtitleFind(UI_LANG_EN, 2, 0) == NULL,
          "no entry: an empty block, out of range, a third set");
    /* the hook's lookup follows the language and the classic switch */
    ui_SetLanguage(UI_LANG_DE);
    a = lt_ext_SubtitleFind(1, 112);
    CHECK(a && strcmp(a->text, "Auf Wiedersehen.") == 0, "German block 112 after the clear");
    ui_MenuTextSetClassic(1);
    CHECK(lt_ext_SubtitleFind(0, 0) == NULL && !lt_ext_PortText(), "classic: no subtitle text");
    ui_MenuTextSetClassic(0);
    ui_SetLanguage(UI_LANG_EN);
    printf("menu_text_test: %d subtitles over 5 languages and 2 sets\n", total);
}

/* the strings visitor reaches the menu words and the subtitles */
static int s_visits, s_visitSub;

static void visit(UiLang lang, const char *s, void *user)
{
    (void)user;
    s_visits++;
    s_visitSub += lang == UI_LANG_IT && strcmp(s, "Prendi la spada") == 0;
}

/* jimaku.c's display_texture for row 434 (dispX 64, dispY 144, the strip's
   left 256 x 48 texels): the rectangle and texels it gives the sprite */
static void jimakuBox(int dst[4], int src[4])
{
    src[0] = (0 << 4) + 8;
    src[1] = (0 << 4) + 8;
    src[2] = 256 << 4;
    src[3] = 48 << 4;
    dst[2] = src[2];
    dst[3] = (src[3] >> 1) * 2;
    dst[0] = (64 - 320) << 4;
    dst[1] = (144 - 112) << 4;
}

static void testSubtitleDraw(void)
{
    int dst[4], src[4];
    jimakuBox(dst, src);
    const unsigned char col[4] = {0x80, 0x80, 0x80, 0x80};
    static const int ring[2]; /* stands for jimaku.c's ring groups */
    ui_SetLanguage(UI_LANG_EN);
    const UiSubtitle *one = ui_SubtitleFind(UI_LANG_EN, 0, 0),
                     *two = ui_SubtitleFind(UI_LANG_EN, 0, 1);
    CHECK(one && two, "blocks 0 and 1");
    if (!one || !two) {
        return;
    }
    dl_SetDLPriority(11);
    lt_ext_DrawSubtitle(two, &ring[0], dst, src, col);
    lt_ext_DrawSubtitle(one, &ring[1], dst, src, col);
    dl_Swap();
    ItemWalk w;
    itemWalk(&w, 11);
    CHECK(w.n == 3, "a two-line and a one-line subtitle: %d items (3)", w.n);
    if (w.n == 3) {
        const UiSubtitleFace *fc = ui_SubtitleFace(UI_LANG_EN);
        /* the strip's texel (x, y) is grid (63.5 + x, 289 + 2 y): row 434's
           box from x 64, y 290 (dispY 144 - 112 field lines below the
           centre), a field line a texel, the sprite's half-texel inset */
        CHECK(strcmp(w.item[0].utf8, "Do not be angry with us.") == 0 &&
                  strcmp(w.item[1].utf8, "This is for the good of the village.") == 0 &&
                  strcmp(w.item[2].utf8, "Get the sword.") == 0,
              "the lines: \"%s\" \"%s\" \"%s\"", w.item[0].utf8, w.item[1].utf8, w.item[2].utf8);
        CHECK(fabsf(w.item[0].x - (63.5f + two->x[0])) < 0.01f &&
                  fabsf(w.item[0].y - (289.0f + 2.0f * fc->y[0])) < 0.01f &&
                  fabsf(w.item[1].y - (289.0f + 2.0f * fc->y[1])) < 0.01f &&
                  fabsf(w.item[2].x - (63.5f + one->x[0])) < 0.01f &&
                  fabsf(w.item[2].y - (289.0f + 2.0f * fc->y[1])) < 0.01f,
              "placed on the strip: (%.2f %.2f) (%.2f %.2f) (%.2f %.2f)", w.item[0].x, w.item[0].y,
              w.item[1].x, w.item[1].y, w.item[2].x, w.item[2].y);
        CHECK(w.item[2].size == 32.0f && w.item[0].size <= 32.0f && w.item[0].size >= 19.0f,
              "the em, 16 texels at 2 y units a texel: %g, %g", w.item[2].size, w.item[0].size);
        CHECK(w.item[0].size == w.item[1].size, "one size a subtitle");
        CHECK((w.item[0].flags & UI_HALO) && (w.item[0].flags & UI_ALIGN_MASK) == UI_ALIGN_CENTER &&
                  (w.item[0].flags & UI_VALIGN_MASK) == UI_VALIGN_MIDDLE &&
                  w.item[0].rgba[3] == 0x80,
              "halo, centred, middle of the capitals, the sprite's alpha");
        CHECK(w.key[0] != w.key[1] && w.key[0] != w.key[2] && w.key[0] && w.key[2],
              "keyed per group and line");
    }
    printf("menu_text_test: subtitles: %d items from two blocks\n", w.n);
}

/* the staff roll's line hook: the codes skipped, the bitmap font's signs
   mapped, font_Print's place, alignment and colour, a key per line slot;
   blank lines draw nothing, classic draws nothing and returns 0 */
static void testRollLine(void)
{
    const unsigned char white[4] = {255, 255, 255, 128};
    const unsigned char plain[4] = {128, 128, 128, 128};
    dl_SetDLPriority(12);
    int r1 = lt_ext_DrawRollLine(3, "{#FFFFFF80}{R} ICO Staff  ", 0.0f, 112.0f, 2, white,
                                 0x80u | 0x70707000u);
    int r2 = lt_ext_DrawRollLine(7, "@ 2002 Sony Computer Entertainment Inc.", 0.0f, 300.0f, 0,
                                 plain, 0x40u | 0x70707000u);
    int r3 = lt_ext_DrawRollLine(8, " ", 0.0f, 200.0f, 0, plain, 0x80u | 0x70707000u);
    /* the same name on two lines: two keys */
    int r4 = lt_ext_DrawRollLine(10, "Kei Kuwabara ", 0.0f, 150.0f, 1, plain, 0x80u | 0x70707000u);
    int r5 = lt_ext_DrawRollLine(11, "Kei Kuwabara ", 0.0f, 170.0f, 1, plain, 0x80u | 0x70707000u);
    dl_Swap();
    CHECK(r1 && r2 && r3 && r4 && r5, "drawn by the port font");
    ItemWalk w;
    itemWalk(&w, 12);
    CHECK(w.n == 4, "four items, the blank line none: %d", w.n);
    if (w.n == 4) {
        CHECK(strcmp(w.item[0].utf8, " ICO Staff  ") == 0 &&
                  strcmp(w.item[1].utf8, "\xC2\xA9 2002 Sony Computer Entertainment Inc.") == 0,
              "codes skipped, '@' the copyright sign: \"%s\", \"%s\"", w.item[0].utf8,
              w.item[1].utf8);
        /* 512 x 512 GS frame: right-aligned 4 GS pixels (5 x units) in
           from the right edge; centred on x + 320; capitals' middle 12.3 GS
           lines below y, y 128 the centre line: grid 226 + (y + 12.3 - 128)
           x 0.875 */
        CHECK((w.item[0].flags & UI_ALIGN_MASK) == UI_ALIGN_RIGHT &&
                  fabsf(w.item[0].x - 635.0f) < 0.01f &&
                  fabsf(w.item[0].y - (226.0f + (112.0f + 12.3125f - 128.0f) * 0.875f)) < 0.01f,
              "right edge %.2f, middle %.2f", w.item[0].x, w.item[0].y);
        CHECK((w.item[1].flags & UI_ALIGN_MASK) == UI_ALIGN_CENTER &&
                  fabsf(w.item[1].x - 320.0f) < 0.01f &&
                  (w.item[2].flags & UI_ALIGN_MASK) == UI_ALIGN_LEFT &&
                  fabsf(w.item[2].x - 5.0f) < 0.01f,
              "centre %.2f, left %.2f", w.item[1].x, w.item[2].x);
        CHECK(w.item[0].rgba[0] == 0x70 && w.item[0].rgba[3] == 0x80 && w.item[1].rgba[0] == 0x38 &&
                  w.item[1].rgba[3] == 0x40,
              "font_Print's colours: %u/%u, %u/%u", w.item[0].rgba[0], w.item[0].rgba[3],
              w.item[1].rgba[0], w.item[1].rgba[3]);
        CHECK(w.item[0].size == 22.0f && !(w.item[0].flags & UI_HALO),
              "the bitmap font's capitals: size %g, no rim", w.item[0].size);
        CHECK(strcmp(w.item[2].utf8, w.item[3].utf8) == 0 && w.key[2] != w.key[3] && w.key[2],
              "the same name on two lines, two keys");
    }
    ui_MenuTextSetClassic(1);
    dl_SetDLPriority(12);
    const int rc = lt_ext_DrawRollLine(3, "Fumito Ueda ", 0.0f, 112.0f, 2, white, 0x80u);
    dl_Swap();
    itemWalk(&w, 12);
    CHECK(rc == 0 && w.n == 0, "classic: font_Print's turn (%d), no item (%d)", rc, w.n);
    ui_MenuTextSetClassic(0);
    printf("menu_text_test: staff roll: 4 lines, keyed per slot; classic none\n");
}

/* the save screens' values through the menu text path: a play-time digit
   (row 76, white without a rim), an empty file's slot number (52, grey) and
   a used file's (62, black) */
static void testDigits(void)
{
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
    current_layout_id = TITLE_LAYOUT;
    layoutFrame();
    ItemWalk w;
    itemWalk(&w, 11);
    CHECK(w.n == 3, "three digit rows: %d items", w.n);
    if (w.n == 3) {
        CHECK(strcmp(w.item[0].utf8, "1") == 0 && strcmp(w.item[1].utf8, "1") == 0 &&
                  strcmp(w.item[2].utf8, "1") == 0,
              "the figure 1 three times");
        /* grey: 151 / 255 of the sprite's colour, no rim; black; white,
           no rim */
        CHECK(!(w.item[0].flags & UI_HALO) && w.item[0].rgba[0] < w.item[2].rgba[0] &&
                  w.item[0].rgba[0] > 0 && w.item[1].rgba[0] == 0 && !(w.item[2].flags & UI_HALO),
              "inks: grey %u, black %u, white %u", w.item[0].rgba[0], w.item[1].rgba[0],
              w.item[2].rgba[0]);
    }
    for (int r = 52; r < 77; r++) {
        texProperty[r].masked = 0;
    }
    printf("menu_text_test: digits: %d items\n", w.n);
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

    ui_MenuTextSetClassic(0);
    buildTitle(1);
    CHECK(lt_ext_IsTextRow(&texProperty[50]) && !lt_ext_IsTextRow(&texProperty[48]),
          "the hook's test: New Game is text, the copyright line a texture");
    s_texTransfers = 0;
    layoutFrame();
    int text = 0, texture = 0;
    countSprites(&text, &texture);
    CHECK(s_texTransfers == 3, "every row's texture transferred (%d of 3)", s_texTransfers);
    /* per text row: eight halo copies and the letters */
    CHECK(text == 2 * 9, "two text rows: %d atlas batches, expected 18", text);
    CHECK(texture == 1, "one texture sprite (the copyright line): %d", texture);
    printf("menu_text_test: port font: %d text batches, %d texture sprites, %d transfers\n", text,
           texture, s_texTransfers);

    /* classic: the textures again */
    ui_MenuTextSetClassic(1);
    CHECK(!lt_ext_IsTextRow(&texProperty[50]) && ui_MenuTextItemOf(&texProperty[50]) == NULL,
          "classic: no text row");
    s_texTransfers = 0;
    layoutFrame();
    countSprites(&text, &texture);
    CHECK(s_texTransfers == 3 && text == 0 && texture == 3,
          "classic: %d transfers, %d text batches, %d texture sprites (3, 0, 3)", s_texTransfers,
          text, texture);
    ui_MenuTextSetClassic(0);

    /* tables that are not the PAL ones: a row whose rectangle differs keeps
       its texture */
    buildTitle(1);
    texProperty[50].texV += 1;
    CHECK(ui_MenuTextItemOf(&texProperty[50]) == NULL, "a moved rectangle is not menu text");
    layoutFrame();
    countSprites(&text, &texture);
    CHECK(text == 9 && texture == 2, "one text row, two textures (%d, %d)", text, texture);
    testDeferred();
    testPortRowAnchor();
    testSubtitleDraw();
    testRollLine();
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
    buildTitle(0);
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
    /* the row's sprite rectangle (display_texture: centred, 172 texels
       wide, 20 field lines from dispY 165), plus the rim's 1.5 y units */
    const LtProperty *e = &texProperty[50];
    const float gx0 = UI_GRID_CX - (float)e->texW * 0.5f, gx1 = UI_GRID_CX + (float)e->texW * 0.5f;
    const float gy0 = (float)e->dispY * 2.0f, gy1 = gy0 + (float)e->dispH;
    const int x0 = toPixX(gx0 - 2.0f), x1 = toPixX(gx1 + 2.0f);
    const int y0 = toPixY(gy0 - 2.0f), y1 = toPixY(gy1 + 2.0f);
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
    ui__SetRecordHook(NULL);
}

int main(int argc, char **argv)
{
    ui_SetLanguage(UI_LANG_EN);
    testTable();
    testStrings();
    testSubtitleTables();
    ui_StringsForEach(visit, NULL);
    CHECK(s_visits > 5 * 300 && s_visitSub == 2,
          "ui_StringsForEach: %d strings, Italian \"Prendi la spada\" in both sets (%d)", s_visits,
          s_visitSub);
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

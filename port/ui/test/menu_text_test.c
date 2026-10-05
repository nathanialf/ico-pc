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
 *     the table's (tables that are not the PAL ones) keeps its texture.
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
        CHECK(it->x >= 0.0f && it->x <= (float)it->w && it->em > 0.0f && it->em <= (float)it->h,
              "item %d: anchor %g, em %g inside %ux%u", i, it->x, it->em, it->w, it->h);
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

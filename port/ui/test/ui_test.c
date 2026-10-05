/* ui_test.c: the port's runtime text (Phase 6, 6B; docs/port/UI.md).
 *
 * Without a device:
 *   - the embedded font parses; glyph bitmaps land in the atlas with the
 *     typeface's metrics (cap height, advance, kerning through GPOS);
 *   - UTF-8 decoding of the five languages' accented letters, and of
 *     malformed input; every one has a glyph;
 *   - ui_MeasureText against the glyph advances and kerning;
 *   - the string tables: every id in every language, valid UTF-8, drawable;
 *   - the layout extension through the real layout_texture.c (with
 *     GifPacket.c, DisplayList.c, DmaPacket.c as the window build has them):
 *     the fall-through lookups, a port layout with two port rows run by
 *     exec_layout_texture, its labels recorded as atlas sprites in list 11
 *     with the colours the texture path would have used, no texture lookup
 *     for a port row, the glow (additive) after a cursor move and the
 *     sparkle on an unselectable selected row;
 *   - the popup queue's timing and panel.
 * Then on a Vulkan device (exit 77 without one; lavapipe here): "ICO" and
 * "Éléphant" drawn through rd into SCENE: coverage only inside the bounds
 * the glyph quads give, the acute above the capitals, and the blend of a
 * known colour over a known background (exact at alpha 0x80, the GS
 * formula within one step at 0x40).
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
#include "popup.h"
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

/* the game's tables: fake rows, no disc data */
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

/* lt_glow_sprite's pulse: sin(pi t) as the game's table gives it */
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

/* --------------------------------------------------------------- font */

static void testGlyphs(void)
{
    CHECK(ui_FontInit(), "the embedded font parses");
    UiGlyph g;
    CHECK(ui_FontGlyph('H', 40, &g), "glyph H at 40 px");
    /* EB Garamond: units per em 1000, capital height 650 */
    CHECK(g.h >= 25 && g.h <= 28, "H at 40 px is %d px tall (cap height 0.65 em)", g.h);
    CHECK(g.yoff <= -25.0f && g.yoff >= -28.0f, "H sits on the baseline (top %g)", g.yoff);
    CHECK(g.advance > 20.0f && g.advance < 40.0f, "H advance %g", g.advance);
    int w, h;
    const uint8_t *cov = ui_FontPage(40, g.page, &w, &h);
    CHECK(cov != NULL && w == 512 && h == 512, "the 40 px atlas page");
    if (cov) {
        int full = 0, sum = 0;
        for (int y = g.y; y < g.y + g.h; y++) {
            for (int x = g.x; x < g.x + g.w; x++) {
                full += cov[y * w + x] == 255;
                sum += cov[y * w + x] != 0;
            }
        }
        CHECK(full > 50 && sum > full, "H has solid stems (%d full, %d covered texels)", full, sum);
        /* the gutter: the texel left of the cell is empty */
        CHECK(g.x == 0 || cov[g.y * w + g.x - 1] == 0, "a clear gutter");
    }
    UiGlyph s;
    CHECK(ui_FontGlyph(' ', 40, &s) && s.w == 0 && s.advance > 0.0f, "space: no bitmap, advance");
    /* the same glyph twice is one atlas cell */
    UiGlyph g2;
    CHECK(ui_FontGlyph('H', 40, &g2) && g2.x == g.x && g2.y == g.y, "the glyph is cached");
    /* kerning comes from GPOS pair adjustment */
    float kAV = ui_FontKern('A', 'V', 40), kTo = ui_FontKern('T', 'o', 40);
    CHECK(kAV < 0.0f || kTo < 0.0f, "kerning: AV %g, To %g", kAV, kTo);
    printf("ui_test: H at 40 px %dx%d, advance %.2f; kern AV %.2f To %.2f px\n", g.w, g.h,
           g.advance, kAV, kTo);
}

static const char kAccents[] = "ÀÂÄÆÇÈÉÊËÎÏÔÖŒÙÛÜŸàâäæçèéêëîïôöœùûüÿßÁÍÑÓÚáíñóú¿¡ÌÒìò«»€’";
static const uint32_t kAccentCps[] = {
    0xC0, 0xC2,  0xC4,  0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCE, 0xCF,   0xD4,  0xD6, 0x152, 0xD9,
    0xDB, 0xDC,  0x178, 0xE0, 0xE2, 0xE4, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA,   0xEB,  0xEE, 0xEF,  0xF4,
    0xF6, 0x153, 0xF9,  0xFB, 0xFC, 0xFF, 0xDF, 0xC1, 0xCD, 0xD1, 0xD3,   0xDA,  0xE1, 0xED,  0xF1,
    0xF3, 0xFA,  0xBF,  0xA1, 0xCC, 0xD2, 0xEC, 0xF2, 0xAB, 0xBB, 0x20AC, 0x2019};

static void testUtf8(void)
{
    const char *s = kAccents;
    size_t n = 0, want = sizeof(kAccentCps) / sizeof(kAccentCps[0]);
    uint32_t cp;
    while ((cp = ui_Utf8Next(&s)) != 0) {
        if (n < want) {
            CHECK(cp == kAccentCps[n], "code point %zu: U+%04X, expected U+%04X", n, cp,
                  kAccentCps[n]);
        }
        CHECK(ui_FontHasGlyph(cp), "the font has U+%04X", cp);
        n++;
    }
    CHECK(n == want, "%zu code points decoded, expected %zu", n, want);

    /* malformed input: one byte consumed, U+FFFD */
    static const struct {
        const char *in;
        uint32_t first;
        int used;
    } bad[] = {
        {"\xC3", 0xFFFD, 1},             /* truncated */
        {"\xC0\xAF", 0xFFFD, 1},         /* overlong '/' */
        {"\xED\xA0\x80", 0xFFFD, 1},     /* a surrogate */
        {"\xF4\x90\x80\x80", 0xFFFD, 1}, /* above U+10FFFF */
        {"\x80", 0xFFFD, 1},             /* a lone continuation byte */
        {"\xE2\x82\xAC", 0x20AC, 3},     /* the euro, well formed */
    };

    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        const char *p = bad[i].in;
        uint32_t c = ui_Utf8Next(&p);
        CHECK(c == bad[i].first && p - bad[i].in == bad[i].used, "case %zu: U+%04X, %d bytes", i, c,
              (int)(p - bad[i].in));
    }
    const char *empty = "";
    CHECK(ui_Utf8Next(&empty) == 0 && *empty == '\0', "the terminator");
}

static void testMeasure(void)
{
    ui_SetScale(1.0f);
    const float size = 40.0f;
    UiGlyph i, c, o;
    ui_FontGlyph('I', 40, &i);
    ui_FontGlyph('C', 40, &c);
    ui_FontGlyph('O', 40, &o);
    float expect = (i.advance + c.advance + o.advance + ui_FontKern('I', 'C', 40) +
                    ui_FontKern('C', 'O', 40)) *
                   UI_X_PER_Y;
    float got = ui_MeasureText(size, "ICO");
    CHECK(fabsf(got - expect) < 0.01f, "ICO measures %g, expected %g", got, expect);
    float av = ui_MeasureText(size, "AV"), a = ui_MeasureText(size, "A"),
          v = ui_MeasureText(size, "V");
    CHECK(av < a + v, "AV is kerned (%g < %g)", av, a + v);
    float twice = ui_MeasureText(80.0f, "ICO");
    CHECK(fabsf(twice - 2.0f * got) < 2.0f, "twice the size, twice the width (%g, %g)", twice, got);
    CHECK(ui_MeasureText(size, "") == 0.0f, "the empty string");
    float two = ui_MeasureText(size, "ICO\nI");
    CHECK(fabsf(two - got) < 0.01f, "the widest line (%g)", two);
    /* scale 2 (Enhanced at 896 lines): the same width in grid units */
    ui_SetScale(2.0f);
    float scaled = ui_MeasureText(size, "ICO");
    CHECK(fabsf(scaled - got) < 1.5f, "scale 2: %g grid units (scale 1: %g)", scaled, got);
    ui_SetScale(1.0f);
    CHECK(ui_ScaleFor(0, 2160) == 1.0f && ui_ScaleFor(1, 896) == 2.0f &&
              ui_ScaleFor(1, 300) == 1.0f,
          "ui_ScaleFor");
    printf("ui_test: \"ICO\" at %g: %.2f grid units\n", size, got);
}

static void testStrings(void)
{
    for (int l = 0; l < UI_LANG_COUNT; l++) {
        for (int id = 1; id < UI_STR_COUNT; id++) {
            const char *s = ui_StrIn((UiLang)l, (UiStrId)id);
            CHECK(s != NULL && s[0] != '\0', "language %d string %d is empty", l, id);
            uint32_t cp;
            while (s && (cp = ui_Utf8Next(&s)) != 0) {
                CHECK(cp != 0xFFFD && ui_FontHasGlyph(cp),
                      "language %d string %d: U+%04X not drawable", l, id, cp);
            }
        }
    }
    CHECK(ui_LangFromGame(2) == UI_LANG_EN && ui_LangFromGame(3) == UI_LANG_FR &&
              ui_LangFromGame(4) == UI_LANG_DE && ui_LangFromGame(5) == UI_LANG_IT &&
              ui_LangFromGame(6) == UI_LANG_ES && ui_LangFromGame(0) == UI_LANG_EN,
          "the game's language numbers");
    ui_SetLanguage(UI_LANG_DE);
    CHECK(strcmp(ui_Str(UI_STR_BACK), "Zurück") == 0, "German Back: %s", ui_Str(UI_STR_BACK));
    ui_SetLanguage(UI_LANG_EN);
    CHECK(strcmp(ui_Str(UI_STR_COUNT), "") == 0, "an unknown id");
}

/* ------------------------------------------------- the layout extension */

typedef struct Walk {
    int n;
    const RdCmd *cmd[64];
    RdStateBlock st[64];
} Walk;

static void collect(void *user, int list, uint32_t index, const RdCmd *c, const RdStateBlock *s)
{
    Walk *w = user;
    (void)index;
    if (list == 11 && c->type == RDC_SCREEN && w->n < 64) {
        w->cmd[w->n] = c;
        w->st[w->n] = *s;
        w->n++;
    }
}

static int s_rowA, s_rowB, s_layout;

static void buildPortLayout(int selectable)
{
    lt_ext_Reset();
    LtProperty row;
    memset(&row, 0, sizeof(row));
    row.word0 = -1;
    row.ownerItem = -1;
    row.up = row.down = row.left = row.right = -1;
    row.rightItem = row.leftItem = row.upItem = row.downItem = -1;
    row.dispX = 200;
    row.dispY = 60;
    row.dispW = 240;
    row.dispH = 40;
    row.selectable = selectable;
    LtExtText t = {UI_STR_SECTION_DISPLAY, NULL, 0.0f, UI_ALIGN_LEFT};
    s_rowA = lt_ext_AddProperty(&row, &t);
    row.dispY = 80;
    t.strId = 0;
    t.text = "Éléphant";
    s_rowB = lt_ext_AddProperty(&row, &t);
    lt_ext_Prop(s_rowA)->downItem = s_rowB;
    lt_ext_Prop(s_rowB)->upItem = s_rowA;
    LtProp lay;
    memset(&lay, 0, sizeof(lay));
    lay.first = s_rowA;
    lay.last = s_rowB + 1;
    lay.colA = 0.5f;
    lay.defaultItem = s_rowA;
    lay.curItem = s_rowA;
    lay.link = -1;
    s_layout = lt_ext_AddLayout(&lay);
}

static void layoutFrame(void)
{
    dl_SetDLPriority(0);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
    exec_layout_texture();
    dl_Swap();
}

static void testLayoutExtension(void)
{
    /* the plain lookups */
    lt_ext_Reset();
    CHECK(lt_ext_Prop(5) == &texProperty[5] && lt_ext_Layout(7) == &texLayout[7],
          "game rows fall through to the game's tables");
    buildPortLayout(1);
    CHECK(s_rowA == LT_GAME_PROPERTY_COUNT && s_rowB == LT_GAME_PROPERTY_COUNT + 1,
          "port rows after the game's (%d, %d)", s_rowA, s_rowB);
    CHECK(s_layout == LT_GAME_LAYOUT_COUNT, "the port layout after the game's (%d)", s_layout);
    CHECK(lt_ext_IsPortProp(lt_ext_Prop(s_rowB)) && !lt_ext_IsPortProp(&texProperty[3]),
          "isPortRow");
    CHECK(lt_ext_PropIndex(lt_ext_Prop(s_rowB)) == s_rowB && lt_ext_PropIndex(&texProperty[9]) == 9,
          "row indices");
    CHECK(strcmp(lt_ext_RowText(s_rowA), "Display") == 0 &&
              strcmp(lt_ext_RowText(s_rowB), "Éléphant") == 0,
          "row labels");
    LtProperty bare;
    memset(&bare, 0, sizeof(bare));
    int r = lt_ext_AddProperty(&bare, NULL);
    CHECK(lt_ext_Prop(r)->dispW == 400 && lt_ext_Prop(r)->dispH == 40, "default row box");

    /* exec_layout_texture on the port layout, recording only */
    if (!rd__InitRecordOnly(512, 512)) {
        CHECK(0, "rd__InitRecordOnly");
        return;
    }
    ui_FontForgetTextures();
    ui__SetRecordHook(gif_HostFlush);
    dl_Init();
    buildPortLayout(1);
    GlobalStageSetting.reductionCol[0] = GlobalStageSetting.reductionCol[1] =
        GlobalStageSetting.reductionCol[2] = 0x80;
    current_layout_id = s_layout;
    s_texTransfers = 0;
    pad[0].ana[2] = pad[0].ana[3] = 128;
    pad[0].flags = 0x4000; /* down: the cursor moves to row B after drawing */
    layoutFrame();
    const RdFrame *f = rd__LastFrame();
    CHECK(s_texTransfers == 0, "no texture transfer for a port row (%d)", s_texTransfers);
    CHECK(lt_ext_Layout(s_layout)->curItem == s_rowB, "the cursor moved to row B");
    Walk w;
    memset(&w, 0, sizeof(w));
    RdStateBlock s = f->startState;
    rd__Walk(f, 0, &s, collect, &w);
    /* the backdrop sprite, then per row its halo (eight black copies) and
       its label */
    CHECK(w.n == 1 + 2 * 9, "list 11 holds %d screen batches, expected 19", w.n);
    const int px = (int)lrintf(UI_MENU_TEXT_SIZE);
    const uint32_t atlas = ui_FontPageTex(px, 0);
    CHECK(atlas != 0, "the %d px atlas is a texture", px);
    int labels = 0, halos = 0;
    for (int i = 1; i < w.n; i++) {
        const RdScreenVtx *v = (const RdScreenVtx *)(f->payload + w.cmd[i]->u[0]);
        const RdStateBlock *st = &w.st[i];
        CHECK(w.cmd[i]->b[0] == RD_PRIM_SPRITES && w.cmd[i]->b[1] == RD_SPACE_UI &&
                  w.cmd[i]->b[2] == 1,
              "batch %d: UI sprites with texel UVs", i);
        CHECK(st->ds.texEnabled && st->tex == atlas && st->ds.texFn == RD_TEXFN_MODULATE &&
                  st->ds.tcc == RD_TCC_RGBA,
              "batch %d: the atlas, MODULATE, TCC RGBA", i);
        CHECK(st->ds.abe == 1 && st->ds.blend == RD_BLEND_LERP_AS, "batch %d: ALPHA 0x44", i);
        CHECK(st->ds.zwrite == RD_ZWRITE_OFF, "batch %d: Z write off (the game's packet)", i);
        CHECK(v[1].z == UI_LAYOUT_Z, "batch %d: the layout's Z", i);
        if (v[1].rgba[0] == 0) {
            CHECK(v[1].rgba[3] == 127 / 4, "halo alpha %u", v[1].rgba[3]);
            halos++;
            continue;
        }
        /* display_texture's colour: ~reductionCol = 0x7F, the fade at 127;
           row A is selected, row B dimmed by half */
        uint8_t want = labels == 0 ? 0x7F : 0x3F;
        CHECK(v[1].rgba[0] == want && v[1].rgba[3] == 127, "row %d colour %u alpha %u", labels,
              v[1].rgba[0], v[1].rgba[3]);
        /* the label starts at the row's dispX: x = (dispX - 320) * 16 + 4 in 1/16 px, through
           gif_SpriteSensitiveOffset's 512 / 640 */
        int x0 = 0x8000 + ((200 - 320) * 16 + 4) * 512 / 640;
        CHECK(abs(v[0].x - x0) < 16 * 6, "row %d starts at x %d (box %d)", labels, v[0].x, x0);
        labels++;
    }
    CHECK(labels == 2 && halos == 16, "%d labels, %d halo copies", labels, halos);
    /* the next frame: row B glows (Cs * As + Cd) */
    pad[0].flags = 0;
    layoutFrame();
    f = rd__LastFrame();
    memset(&w, 0, sizeof(w));
    s = f->startState;
    rd__Walk(f, 0, &s, collect, &w);
    int glow = 0;
    for (int i = 0; i < w.n; i++) {
        glow += w.st[i].ds.blend == RD_BLEND_CS_AS_ADD_CD && w.st[i].tex == atlas;
    }
    CHECK(glow == 1, "the glow is the label, additive (%d batches)", glow);
    /* unselectable rows: the cursor sparkle on the selected one */
    buildPortLayout(0);
    current_layout_id = s_layout;
    layoutFrame();
    f = rd__LastFrame();
    int points = 0;
    for (uint32_t i = 0; i < f->lists[11].count; i++) {
        const RdCmd *c = &f->lists[11].cmds[i];
        points += c->type == RDC_SCREEN && c->b[0] == RD_PRIM_POINTS;
    }
    CHECK(points > 0, "the sparkle around the selected row (%d point batches)", points);
    CHECK(gif_HostUndecodedTotal() == 0, "%u undecoded writes", gif_HostUndecodedTotal());
    ui__SetRecordHook(NULL);
    rd_Shutdown();
    ui_FontForgetTextures();
    lt_ext_Reset();
}

/* --------------------------------------------------------------- popups */

static void testPopups(void)
{
    ui_PopupReset();
    float r[4];
    CHECK(!ui_PopupActive() && !ui_PopupPanel(r), "empty");
    ui_PopupSetDevTest(1);
    ui_PopupDevTick(99);
    CHECK(!ui_PopupActive(), "not before tick 100");
    ui_PopupDevTick(100);
    CHECK(ui_PopupActive(), "the test popup at tick 100");
    ui_PopupDevTick(101);
    CHECK(ui_PopupPush("A", "B") == 0, "a second one queues");
    ui_PopupDevTick(249);
    CHECK(ui_PopupPanel(r) && r[0] >= UI_GRID_W, "starts off the right edge (%g)", r[0]);
    for (int i = 0; i < UI_POPUP_SLIDE_VSYNCS; i++) {
        ui_PopupVsync();
    }
    CHECK(ui_PopupPanel(r) && r[2] <= UI_GRID_W && r[0] > 0.0f && r[1] > 0.0f && r[3] < 120.0f,
          "slid in: %g,%g %g,%g", r[0], r[1], r[2], r[3]);
    for (int i = 0; i < UI_POPUP_HOLD_VSYNCS + UI_POPUP_SLIDE_VSYNCS; i++) {
        ui_PopupVsync();
    }
    CHECK(ui_PopupActive() && ui_PopupPanel(r) && r[0] >= UI_GRID_W, "the second waits off screen");
    for (int i = 0; i < 2 * UI_POPUP_SLIDE_VSYNCS + UI_POPUP_HOLD_VSYNCS; i++) {
        ui_PopupVsync();
    }
    CHECK(!ui_PopupActive(), "both shown once");
    ui_PopupDevTick(250);
    CHECK(ui_PopupActive(), "the test popup again 150 ticks later");
    ui_PopupReset();
    for (int i = 0; i < UI_POPUP_QUEUE; i++) {
        ui_PopupPush("x", "y");
    }
    CHECK(ui_PopupPush("x", "y") == -1, "the queue is bounded");
    ui_PopupReset();
    ui_PopupSetDevTest(0);
}

/* --------------------------------------------------------------- pixels */

static const uint8_t kBg[4] = {20, 40, 60, 0x80};

static int toPixX(float gx)
{
    /* GS 12.4 x to SCENE pixels: XYOFFSET is 2048 - 256 */
    return (int)floorf((2048.0f * 16.0f + (gx - UI_GRID_CX) * 16.0f * 512.0f / 640.0f) / 16.0f) -
           1792;
}

static int toPixY(float gy)
{
    return (int)floorf((2048.0f * 16.0f + (gy - UI_GRID_CY) * 8.0f * 512.0f / 224.0f) / 16.0f) -
           1792;
}

/* the drawn word's bounds: x from the measure, y from the line metrics */
typedef struct Box {
    int x0, y0, x1, y1;
} Box;

static Box wordBox(float x, float y, float size, const char *s)
{
    float asc, desc;
    ui_FontMetrics(size, &asc, &desc, NULL);
    Box b;
    float w = ui_MeasureText(size, s);
    b.x0 = toPixX(x - w * 0.5f) - 3;
    b.x1 = toPixX(x + w * 0.5f) + 3;
    b.y0 = toPixY(y) - 2;
    b.y1 = toPixY(y + asc + desc) + 2;
    return b;
}

static int differs(const uint8_t *p)
{
    return p[0] != kBg[0] || p[1] != kBg[1] || p[2] != kBg[2];
}

static void drawFrame(const uint8_t col[4], const uint8_t colI[4])
{
    rd_BeginFrame();
    rd_SelectList(0);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), kBg, 1, 0);
    rd_SelectList(11);
    ui_DrawText(160.0f, 60.0f, 60.0f, col, "ICO", UI_ALIGN_CENTER);
    ui_DrawText(320.0f, 200.0f, 60.0f, col, "Éléphant", UI_ALIGN_CENTER);
    /* a big I for the blend: alpha 0x80 left, 0x40 right */
    ui_DrawText(480.0f, 40.0f, 150.0f, col, "I", UI_ALIGN_CENTER);
    ui_DrawText(560.0f, 40.0f, 150.0f, colI, "I", UI_ALIGN_CENTER);
    rd_EndFrame(0);
}

static void testPixels(void)
{
    const uint8_t col[4] = {100, 50, 25, 0x80};
    const uint8_t colI[4] = {100, 50, 25, 0x40};
    drawFrame(col, colI);
    uint32_t w = 0, h = 0;
    uint8_t *px = malloc(512 * 512 * 4);
    if (!px || !rd__ReadTarget(rd_Target(RD_TARGET_SCENE), px, 512 * 512 * 4, &w, &h) || w != 512) {
        CHECK(0, "SCENE readback");
        free(px);
        return;
    }
    /* the frame as a PNG beside the test, for a look */
    rd_WritePng("ui_test_scene.png", px, 512, 512, 512 * 4, 0);
    Box ico = wordBox(160.0f, 60.0f, 60.0f, "ICO");
    Box ele = wordBox(320.0f, 200.0f, 60.0f, "Éléphant");
    int inIco = 0, inEle = 0, outside = 0, above = 0;
    float asc, cap;
    ui_FontMetrics(60.0f, &asc, NULL, &cap);
    const int capTop = toPixY(200.0f + asc - cap);
    /* the first letter's columns: only its acute rises above the capitals */
    const int eAcuteX1 = ele.x0 + 3 + (toPixX(ui_MeasureText(60.0f, "É")) - toPixX(0.0f));
    Box iL = wordBox(480.0f, 40.0f, 150.0f, "I"), iR = wordBox(560.0f, 40.0f, 150.0f, "I");
    for (int y = 0; y < 512; y++) {
        for (int x = 0; x < 512; x++) {
            const uint8_t *p = &px[(y * 512 + x) * 4];
            if (!differs(p)) {
                continue;
            }
            int a = x >= ico.x0 && x <= ico.x1 && y >= ico.y0 && y <= ico.y1;
            int b = x >= ele.x0 && x <= ele.x1 && y >= ele.y0 && y <= ele.y1;
            int c = (x >= iL.x0 && x <= iL.x1 && y >= iL.y0 && y <= iL.y1) ||
                    (x >= iR.x0 && x <= iR.x1 && y >= iR.y0 && y <= iR.y1);
            inIco += a;
            inEle += b;
            above += b && x < eAcuteX1 && y < capTop - 1;
            outside += !a && !b && !c;
        }
    }
    CHECK(inIco > 300, "ICO covers %d pixels", inIco);
    CHECK(inEle > 600, "Éléphant covers %d pixels", inEle);
    CHECK(above > 10, "the acute above the capital E (%d pixels)", above);
    CHECK(outside == 0, "%d pixels drawn outside the words' bounds", outside);
    printf("ui_test: ICO box %d,%d-%d,%d %d px; Éléphant box %d,%d-%d,%d %d px, %d of the acute "
           "above the capitals\n",
           ico.x0, ico.y0, ico.x1, ico.y1, inIco, ele.x0, ele.y0, ele.x1, ele.y1, inEle, above);

    /* the blend in the stems of the two I: the texel is white at full
       coverage (alpha 0x80), MODULATE gives (255 * c) >> 7 */
    int fullHits = 0, halfHits = 0, worst = 0;
    for (int side = 0; side < 2; side++) {
        const Box *bx = side ? &iR : &iL;
        const int cx = (bx->x0 + bx->x1) / 2;
        for (int y = bx->y0 + 2; y <= bx->y1 - 2; y++) {
            const uint8_t *p = &px[(y * 512 + cx) * 4];
            const uint8_t *l = &px[(y * 512 + cx - 2) * 4];
            const uint8_t *r = &px[(y * 512 + cx + 2) * 4];
            const uint8_t *u = &px[((y - 2) * 512 + cx) * 4];
            const uint8_t *d = &px[((y + 2) * 512 + cx) * 4];
            /* inside the stem (full coverage): the four neighbours two
               pixels away are painted the same */
            if (!differs(p) || memcmp(p, l, 3) != 0 || memcmp(p, r, 3) != 0 ||
                memcmp(p, u, 3) != 0 || memcmp(p, d, 3) != 0) {
                continue;
            }
            for (int ch = 0; ch < 3; ch++) {
                int cs = (255 * col[ch]) >> 7;
                int want = side ? (((cs - kBg[ch]) * 0x40) >> 7) + kBg[ch] : cs;
                int d = abs((int)p[ch] - want);
                if (d > worst) {
                    worst = d;
                }
                if (side == 0) {
                    CHECK(d == 0, "alpha 0x80 at %d,%d ch %d: %u, expected %d", cx, y, ch, p[ch],
                          want);
                } else {
                    CHECK(d <= 1, "alpha 0x40 at %d,%d ch %d: %u, expected %d", cx, y, ch, p[ch],
                          want);
                }
            }
            if (side) {
                halfHits++;
            } else {
                fullHits++;
            }
        }
    }
    CHECK(fullHits > 20 && halfHits > 20, "stem pixels: %d at 0x80, %d at 0x40", fullHits,
          halfHits);
    printf("ui_test: blend over %d + %d stem pixels, worst difference %d\n", fullHits, halfHits,
           worst);
    free(px);
}

int main(void)
{
    testGlyphs();
    testUtf8();
    testMeasure();
    testStrings();
    testLayoutExtension();
    testPopups();
    if (failures) {
        printf("ui_test: %d failures\n", failures);
        return 1;
    }

    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ORIGINAL;
    if (!rd_Init(512, 512, &st, NULL)) {
        printf("ui_test: CPU checks ok; SKIP the pixel checks: no usable Vulkan device\n");
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
        printf("ui_test: %d failures\n", failures);
        return 1;
    }
    printf("ui_test: ok\n");
    return 0;
}

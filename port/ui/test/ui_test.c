/* ui_test.c: the port's runtime text.
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
 *     exec_layout_texture, its labels recorded as one sheet strip each
 *     (menu_font.h) in list 11 with the colours the texture path
 *     would have used, no texture lookup for a port row, the glow
 *     (additive) after a cursor move and the sparkle on an unselectable
 *     selected row;
 *   - the popup queue's timing and panel;
 *   - overlay mode (ui_BeginOverlay): the grid mapped onto the
 *     4:3 picture of a 1080p and a 4K output (and of a 16:9 box), glyph
 *     quads on whole output pixels at the bitmap's size, rasterised at
 *     round(size * box.h / 448), rects snapped, the scale restored; a
 *     popup drawn on the overlay (its text one magnified strip a line)
 *     records nothing into the open frame's lists (list 12 included), sits
 *     at the 4:3 picture's right, and is the same with the mirror on;
 *   - the size sets: 40 sizes of "IHL" measured (no
 *     set made) and then drawn over five frames: each draw has its own
 *     set, the least recently drawn sets go, "the nearest" is never reused.
 * Then on a Vulkan device (exit 77 without one; lavapipe here): "ICO" and
 * "Éléphant" drawn as sheet strips through rd into SCENE: coverage only
 * inside the bounds the measure gives (with the rim), the acute above the
 * capitals, and the blend of a known colour over a known background (exact
 * at alpha 0x80, the GS formula within one step at 0x40); the menu rows at
 * an Enhanced 4x scene against font_sheet_ps's CPU reference (sheet_ref.c),
 * plain and light ink.
 *
 * Last: a popup on the presentation overlay of a 1920 x 1080
 * Original present: at the 4:3 picture's right, text pixels in the panel,
 * nothing changed outside it (ui_test_popup.png, the panel's crop).
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
#include "sheet_ref.h"
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

/* init_layout_texture's Settings hook (settings_test covers it) */
void ui_SettingsInstall(void) {}

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
    /* Arimo: units per em 2048, capital height 1409 (0.688 em, 27.5 px) */
    CHECK(g.h >= 27 && g.h <= 29, "H at 40 px is %d px tall (cap height 0.688 em)", g.h);
    CHECK(g.yoff <= -27.0f && g.yoff >= -29.0f, "H sits on the baseline (top %g)", g.yoff);
    CHECK(g.advance > 20.0f && g.advance < 40.0f, "H advance %g", g.advance);
    int w, h;
    const uint8_t *cov = ui_FontPage(40, g.page, &w, &h);
    /* 512 wide, not a power of two high (no Enhanced mip chain) */
    CHECK(cov != NULL && w == 512 && h > 256 && h < 512 && (h & (h - 1)) != 0,
          "the 40 px atlas page (%d x %d)", w, h);
    if (cov) {
        int full = 0, sum = 0;
        for (int y = g.y; y < g.y + g.h; y++) {
            for (int x = g.x; x < g.x + g.w; x++) {
                full += cov[y * w + x] == 255;
                sum += cov[y * w + x] != 0;
            }
        }
        CHECK(full > 50 && sum > full, "H has solid stems (%d full, %d covered texels)", full, sum);
        /* the gutter, two texels on every side of the cell, is empty */
        int gut = 0;
        for (int y = g.y - 2; y < g.y + g.h + 2; y++) {
            for (int x = g.x - 2; x < g.x + g.w + 2; x++) {
                const int edge = y < g.y || y >= g.y + g.h || x < g.x || x >= g.x + g.w;
                gut += edge && (x < 0 || y < 0 || cov[y * w + x] != 0);
            }
        }
        CHECK(gut == 0, "a clear two-texel gutter (%d texels)", gut);
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
                if (cp == '\n') {
                    continue; /* a line break (the menu text's prompts) */
                }
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
    /* the backdrop sprite, then per row its label: one sheet strip in the
       menus' look (no halo copies) */
    CHECK(w.n == 1 + 2, "list 11 holds %d screen batches, expected 3", w.n);
    UiMenuStrip strip;
    CHECK(ui_MenuFontLastStrip(&strip) && strip.tex != 0 && strip.cls == 0,
          "the labels are strips of the light pages (class %d)", strip.cls);
    const RdTexRec *page = rd__TexRec(strip.tex);
    CHECK(page && page->format == RD_TEXEL_SHEET && page->sheet[0] == 64,
          "the page is a sheet texture with the rim on");
    int labels = 0;
    for (int i = 1; i < w.n; i++) {
        const RdScreenVtx *v = (const RdScreenVtx *)(f->payload + w.cmd[i]->u[0]);
        const RdStateBlock *st = &w.st[i];
        CHECK(w.cmd[i]->b[0] == RD_PRIM_SPRITES && w.cmd[i]->b[1] == RD_SPACE_UI &&
                  w.cmd[i]->b[2] == RD_UV_FIXED_CONTINUOUS && w.cmd[i]->u[1] == 2,
              "batch %d: one UI sprite with texel UVs, continuous (T1)", i);
        CHECK(st->ds.texEnabled && ui_MenuFontIsPage(st->tex) &&
                  st->ds.texFn == RD_TEXFN_MODULATE && st->ds.tcc == RD_TCC_RGBA,
              "batch %d: a menu text page, MODULATE, TCC RGBA", i);
        CHECK(st->ds.abe == 1 && st->ds.blend == RD_BLEND_LERP_AS, "batch %d: ALPHA 0x44", i);
        CHECK(st->ds.zwrite == RD_ZWRITE_OFF, "batch %d: Z write off (the game's packet)", i);
        CHECK(v[1].z == UI_LAYOUT_Z, "batch %d: the layout's Z", i);
        /* display_texture's colour: ~reductionCol = 0x7F, the fade at 127;
           row A is selected, row B dimmed by half */
        uint8_t want = labels == 0 ? 0x7F : 0x3F;
        CHECK(v[1].rgba[0] == want && v[1].rgba[3] == 127, "row %d colour %u alpha %u", labels,
              v[1].rgba[0], v[1].rgba[3]);
        /* the label starts at the row's dispX: x = (dispX - 320) * 16 + 4 in 1/16 px, through
           gif_SpriteSensitiveOffset's 512 / 640; the strip starts its margin
           (the glyphs' overhang and the rim, UI_MENU_RIM_X 7 since the
           rim fades out over the sheets' 6 texels: 11 texels at this size,
           145 measured; 119 with the hard 4-texel rim) before */
        int x0 = 0x8000 + ((200 - 320) * 16 + 4) * 512 / 640;
        CHECK(v[0].x < x0 && x0 - v[0].x < 16 * 10, "row %d starts at x %d (box %d)", labels,
              v[0].x, x0);
        labels++;
    }
    CHECK(labels == 2, "%d labels", labels);
    /* the next frame: row B glows (Cs * As + Cd) */
    pad[0].flags = 0;
    layoutFrame();
    f = rd__LastFrame();
    memset(&w, 0, sizeof(w));
    s = f->startState;
    rd__Walk(f, 0, &s, collect, &w);
    int glow = 0;
    for (int i = 0; i < w.n; i++) {
        glow += w.st[i].ds.blend == RD_BLEND_CS_AS_ADD_CD && ui_MenuFontIsPage(w.st[i].tex);
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

/* ---------------------------------------------------------- the overlay */

typedef struct OvCap {
    int n;
    RdPrim type[64];
    uint32_t count[64], tex[64];
    RdBlend blend[64];
    RdScreenVtx v[64][2];
} OvCap;

static OvCap s_cap;

static void capSink(RdPrim type, const RdScreenVtx *v, uint32_t n, RdTex tex, RdBlend blend)
{
    if (s_cap.n == 64) {
        return;
    }
    const int i = s_cap.n++;
    s_cap.type[i] = type;
    s_cap.count[i] = n;
    s_cap.tex[i] = tex.id;
    s_cap.blend[i] = blend;
    memcpy(s_cap.v[i], v, sizeof(RdScreenVtx) * (n < 2 ? n : 2));
}

static RdOverlayCtx ovCtx(uint32_t w, uint32_t h, float aspect)
{
    RhiRect b;
    rd__PresentBox(w, h, aspect, &b);
    RdOverlayCtx c;
    memset(&c, 0, sizeof(c));
    c.outW = w;
    c.outH = h;
    c.box = (RdRect){b.x, b.y, b.w, b.h};
    c.boxScale = (float)b.h / 448.0f;
    return c;
}

static void checkOverlayMap(uint32_t w, uint32_t h, float aspect)
{
    const RdOverlayCtx c = ovCtx(w, h, aspect);
    /* the 4:3 picture the game's UI is in */
    const float pw = (float)c.box.h * 4.0f / 3.0f;
    const float left = (float)c.box.x + ((float)c.box.w - pw) * 0.5f;
    const float before = ui_GetScale();
    ui_BeginOverlay(&c);
    CHECK(ui_OverlayActive() && ui_GetScale() == c.boxScale, "%ux%u: overlay scale %g", w, h,
          (double)ui_GetScale());
    float x16, y16, x16b, y16b;
    ui_OverlayMap(0.0f, UI_GRID_CY - 224.0f, &x16, &y16);
    ui_OverlayMap(UI_GRID_W, UI_GRID_CY + 224.0f, &x16b, &y16b);
    CHECK(fabsf(x16 - left * 16.0f) < 0.5f && fabsf(y16 - (float)c.box.y * 16.0f) < 0.5f &&
              fabsf(x16b - (left + pw) * 16.0f) < 0.5f &&
              fabsf(y16b - (float)(c.box.y + (int32_t)c.box.h) * 16.0f) < 0.5f,
          "%ux%u: the frame's 640 x 448 grid onto the 4:3 picture (%g,%g .. %g,%g)", w, h,
          (double)x16 / 16, (double)y16 / 16, (double)x16b / 16, (double)y16b / 16);
    /* a glyph: on whole pixels, the bitmap's size, a texel a pixel */
    memset(&s_cap, 0, sizeof(s_cap));
    ui__SetOverlaySink(capSink);
    static const uint8_t white[4] = {0x80, 0x80, 0x80, 0x80};
    ui_DrawText(100.3f, 100.7f, 26.0f, white, "H", UI_VALIGN_BASELINE);
    const int px = (int)lrintf(26.0f * c.boxScale);
    UiGlyph g;
    const bool have = ui_FontGlyph('H', px, &g);
    CHECK(have && s_cap.n == 1 && s_cap.type[0] == RD_PRIM_SPRITES && s_cap.count[0] == 2 &&
              s_cap.tex[0] == ui_FontPageTex(px, g.page) && s_cap.tex[0] != 0 &&
              s_cap.blend[0] == RD_BLEND_LERP_AS,
          "%ux%u: one sprite of the %d px atlas, ALPHA 0x44 (%d batches)", w, h, px, s_cap.n);
    if (have && s_cap.n == 1) {
        const RdScreenVtx *a = &s_cap.v[0][0], *b = &s_cap.v[0][1];
        CHECK(a->x % 16 == 0 && a->y % 16 == 0 && b->x % 16 == 0 && b->y % 16 == 0,
              "%ux%u: glyph corners on whole pixels (%d,%d %d,%d)", w, h, a->x, a->y, b->x, b->y);
        CHECK((b->x - a->x) / 16 == g.w && (b->y - a->y) / 16 == g.h &&
                  (int)(b->s - a->s) / 16 == g.w && (int)(b->t - a->t) / 16 == g.h,
              "%ux%u: the quad is the %dx%d bitmap, a texel a pixel", w, h, g.w, g.h);
        /* where the grid puts it: the pen at 100.3, the baseline at 100.7 */
        const float xs = UI_X_PER_Y / c.boxScale, ys = 1.0f / c.boxScale;
        float ex, ey;
        ui_OverlayMap(100.3f + g.xoff * xs, 100.7f + g.yoff * ys, &ex, &ey);
        CHECK(fabsf((float)a->x - ex) <= 8.0f && fabsf((float)a->y - ey) <= 8.0f,
              "%ux%u: the glyph at %d,%d (12.4), the grid says %g,%g", w, h, a->x, a->y, (double)ex,
              (double)ey);
    }
    /* a rect: both corners rounded */
    memset(&s_cap, 0, sizeof(s_cap));
    ui_DrawRect(10.2f, 20.6f, 50.5f, 30.1f, white);
    float rx0, ry0, rx1, ry1;
    ui_OverlayMap(10.2f, 20.6f, &rx0, &ry0);
    ui_OverlayMap(50.5f, 30.1f, &rx1, &ry1);
    CHECK(s_cap.n == 1 && s_cap.tex[0] == 0 && s_cap.v[0][0].x == lrintf(rx0 / 16.0f) * 16 &&
              s_cap.v[0][0].y == lrintf(ry0 / 16.0f) * 16 &&
              s_cap.v[0][1].x == lrintf(rx1 / 16.0f) * 16 &&
              s_cap.v[0][1].y == lrintf(ry1 / 16.0f) * 16,
          "%ux%u: the rect on whole pixels", w, h);
    /* a sync while drawing changes the scale after, not now */
    ui_SetScale(3.0f);
    CHECK(ui_GetScale() == c.boxScale, "%ux%u: ui_SetScale defers in overlay mode", w, h);
    ui__SetOverlaySink(NULL);
    ui_EndOverlay();
    CHECK(!ui_OverlayActive() && ui_GetScale() == 3.0f, "%ux%u: the deferred scale", w, h);
    ui_SetScale(before);
}

/* the popup on the overlay: nothing recorded into the game's frame */
static void checkOverlayPopup(void)
{
    if (!rd__InitRecordOnly(512, 512)) {
        CHECK(0, "rd__InitRecordOnly");
        return;
    }
    ui_FontForgetTextures();
    ui_PopupReset();
    ui_PopupPush("Title", "Body line");
    for (int i = 0; i < UI_POPUP_SLIDE_VSYNCS; i++) {
        ui_PopupVsync();
    }
    /* a frame without the popup, then the same with it drawn */
    rd_BeginFrame();
    rd_SelectList(5);
    rd_EndFrame(0);
    uint32_t before[RD_LIST_COUNT];
    const RdFrame *f = rd__LastFrame();
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        before[l] = f ? f->lists[l].count : 0;
    }
    rd_BeginFrame();
    rd_SelectList(5);
    static OvCap plain;
    for (int mirror = 0; mirror < 2; mirror++) {
        RdOverlayCtx c = ovCtx(1920, 1080, 4.0f / 3.0f);
        c.mirror = mirror;
        memset(&s_cap, 0, sizeof(s_cap));
        ui__SetOverlaySink(capSink);
        ui_PopupDrawOverlay(&c);
        ui__SetOverlaySink(NULL);
        CHECK(!ui_OverlayActive(), "the popup ends overlay mode");
        /* the panel, its hairline, the title and the body (one 1x sheet
           strip each, magnified) */
        CHECK(s_cap.n == 4 && s_cap.tex[0] == 0 && s_cap.count[0] == 2 &&
                  ui_MenuFontIsPage(s_cap.tex[2]) && ui_MenuFontIsPage(s_cap.tex[3]) &&
                  s_cap.count[2] == 2 && s_cap.count[3] == 2,
              "the popup on the overlay: %d batches, the text on menu pages", s_cap.n);
        if (s_cap.n >= 1) {
            /* the panel at the 4:3 picture's right, 18 x units in */
            const int32_t right = (c.box.x + (int32_t)c.box.w) * 16;
            const int32_t mid = (c.box.x + (int32_t)c.box.w / 2) * 16;
            const RdScreenVtx *a = &s_cap.v[0][0], *b = &s_cap.v[0][1];
            CHECK(b->x < right && b->x > right - 30 * 16 * 3 && a->x > mid,
                  "mirror %d: the panel %d..%d at the picture's right (%d)", mirror, a->x, b->x,
                  right);
        }
        if (mirror == 0) {
            memcpy(&plain, &s_cap, sizeof(plain));
        } else {
            CHECK(memcmp(&plain, &s_cap, sizeof(plain)) == 0,
                  "the popup is drawn the same with the mirror on");
        }
    }
    CHECK(rd_CurrentList() == 5, "the current list untouched (%d)", rd_CurrentList());
    rd_EndFrame(0);
    f = rd__LastFrame();
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        CHECK(f && f->lists[l].count == before[l], "list %d: %u commands, %u without the popup", l,
              f ? f->lists[l].count : 0, before[l]);
    }
    rd_Shutdown();
    ui_FontForgetTextures();
    ui_PopupReset();
}

/* the atlas pages are R8 (one-channel) coverage; a page is created whole
   when first drawn, then each new glyph is one rectangle update and no page
   is uploaded whole again, in the frame and on the overlay */
static void checkAtlasPage(const char *what, int px, int page)
{
    int w = 0, h = 0;
    const uint8_t *cov = ui_FontPage(px, page, &w, &h);
    const RdTexRec *t = rd__TexRec(ui_FontPageTex(px, page));
    int bad = !cov || !t || t->format != RD_TEXEL_R8 || (int)t->w != w || (int)t->h != h;
    for (size_t i = 0; !bad && i < (size_t)w * (size_t)h; i++) {
        bad = t->pixels[i] != (cov[i] * 128 + 127) / 255;
    }
    CHECK(!bad, "%s: the %d px page %d is R8, the coverage in GS units", what, px, page);
}

static void checkAtlasUploads(const char *what, float size, int px)
{
    static const uint8_t white[4] = {0x80, 0x80, 0x80, 0x80};
    ui_DrawText(100.0f, 100.0f, size, white, "AB", 0);
    CHECK(ui_FontPageTex(px, 0) != 0, "%s: the %d px page drawn", what, px);
    const uint32_t rect0 = g_rd.texRectUpdates, full0 = g_rd.texFullUpdates;
    ui_DrawText(100.0f, 140.0f, size, white, "CDEFG", 0);
    CHECK(g_rd.texRectUpdates - rect0 == 5 && g_rd.texFullUpdates == full0,
          "%s: 5 new glyphs, %u rectangle updates, %u whole-page updates", what,
          g_rd.texRectUpdates - rect0, g_rd.texFullUpdates - full0);
    ui_DrawText(100.0f, 180.0f, size, white, "GFEDCBA", 0);
    CHECK(g_rd.texRectUpdates - rect0 == 5 && g_rd.texFullUpdates == full0,
          "%s: no new glyph, no update (%u)", what, g_rd.texRectUpdates - rect0);
    checkAtlasPage(what, px, 0);
}

static void testAtlasUploads(void)
{
    if (!rd__InitRecordOnly(512, 512)) {
        CHECK(0, "rd__InitRecordOnly");
        return;
    }
    ui_FontForgetTextures();
    const float before = ui_GetScale();
    ui_SetScale(1.0f);
    rd_BeginFrame();
    rd_SelectList(11);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
    checkAtlasUploads("in the frame", 33.0f, 33);
    rd_EndFrame(0);
    const RdOverlayCtx c = ovCtx(1280, 720, 4.0f / 3.0f);
    memset(&s_cap, 0, sizeof(s_cap));
    ui__SetOverlaySink(capSink);
    ui_BeginOverlay(&c);
    const int px = (int)lrintf(31.0f * c.boxScale);
    checkAtlasUploads("on the overlay", 31.0f, px);
    CHECK(s_cap.n == 3 && s_cap.tex[0] == ui_FontPageTex(px, 0), "on the overlay: %d batches",
          s_cap.n);
    ui_EndOverlay();
    ui__SetOverlaySink(NULL);
    ui_SetScale(before);
    rd_Shutdown();
    ui_FontForgetTextures();
}

static void testOverlay(void)
{
    if (!rd__InitRecordOnly(512, 512)) {
        CHECK(0, "rd__InitRecordOnly");
        return;
    }
    ui_FontForgetTextures();
    checkOverlayMap(1920, 1080, 4.0f / 3.0f);
    checkOverlayMap(3840, 2160, 4.0f / 3.0f);
    checkOverlayMap(1920, 1080, 16.0f / 9.0f);
    checkOverlayMap(2560, 1080, 64.0f / 27.0f);
    rd_Shutdown();
    ui_FontForgetTextures();
    checkOverlayPopup();
}

/* The popup on the device: a 1920 x 1080 Original present
   with and without the popup, the overlay registered as ui_host.c does. */
static void popupOverlay(const RdOverlayCtx *ctx, void *user)
{
    (void)user;
    ui_PopupDrawOverlay(ctx);
}

static bool popupPresent(int popup, uint8_t *dst)
{
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ORIGINAL;
    st.outputWidth = 1920;
    st.outputHeight = 1080;
    if (!rd_Init(512, 512, &st, NULL)) {
        return false;
    }
    ui_FontForgetTextures();
    rd_SetPresentOverlay(popupOverlay, NULL);
    ui_PopupReset();
    if (popup) {
        ui_PopupPush("Achievement unlocked", "Éléphant, Größe, señor");
        for (int i = 0; i < UI_POPUP_SLIDE_VSYNCS; i++) {
            ui_PopupVsync();
        }
    }
    static const uint8_t grey[4] = {90, 100, 110, 0x80};
    rd_BeginFrame();
    rd_SelectList(0);
    rd_ClearTarget(rd_Target(RD_TARGET_DISPLAY), grey, 0, 0);
    rd_EndFrame(0);
    uint32_t w = 0, h = 0;
    const bool ok = rd_ReadPresented(dst, &w, &h) && w == 1920 && h == 1080;
    CHECK(ok, "popup: the presented output");
    rd_SetPresentOverlay(NULL, NULL);
    CHECK(rhi_vk_ValidationErrorCount() == 0, "popup: %u validation errors",
          rhi_vk_ValidationErrorCount());
    ui_FontShutdown();
    rd_Shutdown();
    return ok;
}

static void testPopupPixels(void)
{
    const size_t n = (size_t)1920 * 1080 * 4;
    uint8_t *plain = malloc(n), *pop = malloc(n);
    float r[4];
    if (!plain || !pop || !popupPresent(0, plain) || !popupPresent(1, pop) || !ui_PopupPanel(r)) {
        CHECK(0, "popup: the presents");
        free(plain);
        free(pop);
        ui_PopupReset();
        return;
    }
    /* the panel's grid rectangle on the output (the overlay's mapping) */
    const RdOverlayCtx c = ovCtx(1920, 1080, 4.0f / 3.0f);
    ui_BeginOverlay(&c);
    float x0, y0, x1, y1;
    ui_FontInit();
    ui_PopupPanel(r); /* measured at the overlay's scale */
    ui_OverlayMap(r[0], r[1], &x0, &y0);
    ui_OverlayMap(r[2], r[3], &x1, &y1);
    ui_EndOverlay();
    const int px0 = (int)lrintf(x0 / 16.0f), py0 = (int)lrintf(y0 / 16.0f);
    const int px1 = (int)lrintf(x1 / 16.0f), py1 = (int)lrintf(y1 / 16.0f);
    int outside = 0, panelPx = 0, bright = 0;
    for (int y = 0; y < 1080; y++) {
        for (int x = 0; x < 1920; x++) {
            const size_t i = ((size_t)y * 1920 + (size_t)x) * 4;
            const int in = x >= px0 && x < px1 && y >= py0 && y < py1;
            if (!in) {
                outside += memcmp(&pop[i], &plain[i], 3) != 0;
                continue;
            }
            panelPx++;
            bright += pop[i] > 200 && pop[i + 1] > 200;
        }
    }
    printf("ui_test: popup panel %d,%d .. %d,%d on 1920 x 1080 (box %d..%d), %d bright text "
           "pixels\n",
           px0, py0, px1, py1, c.box.x, c.box.x + (int)c.box.w, bright);
    /* 18 x units (40 pixels here) in from the 4:3 picture's right edge */
    const int right = c.box.x + (int)c.box.w;
    CHECK(px1 <= right && px1 >= right - 41 && px0 > c.box.x + (int)c.box.w / 4,
          "popup: the panel at the picture's right");
    CHECK(outside == 0, "popup: %d pixels changed outside the panel", outside);
    CHECK(bright > 200, "popup: %d bright text pixels", bright);
    /* for the eye: the panel and a margin */
    const int cx0 = px0 - 8 < 0 ? 0 : px0 - 8, cy0 = py0 - 8 < 0 ? 0 : py0 - 8;
    const int cx1 = px1 + 8 > 1920 ? 1920 : px1 + 8, cy1 = py1 + 8 > 1080 ? 1080 : py1 + 8;
    rd_WritePng("ui_test_popup.png", pop + ((size_t)cy0 * 1920 + (size_t)cx0) * 4,
                (uint32_t)(cx1 - cx0), (uint32_t)(cy1 - cy0), 1920 * 4, 0);
    free(plain);
    free(pop);
    ui_PopupReset();
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

/* the drawn word's bounds: x from the measure, y from the line metrics
   (the menus' text, menu_font.h), plus the strip's rim and its bilinear
   read (UI_MENU_RIM_X and UI_MENU_RIM_Y texels: a texel is an x unit and
   2 y units) and the anchor's snap to whole texels (an x unit, a y unit).
   Written for a rim of 1 x 1 texels (3 and 5 units); the sheets' survey
   set 4 x 3, and 44 pixels of rim fell outside the old bounds; the faded
   rim reaches 6 x 4. */
typedef struct Box {
    int x0, y0, x1, y1;
} Box;

static Box wordBox(float x, float y, float size, const char *s)
{
    float asc, desc;
    ui_MenuFontMetrics(size, &asc, &desc, NULL);
    Box b;
    float w = ui_MeasureMenuText(size, s);
    const float mx = (float)UI_MENU_RIM_X + 1.0f, my = 2.0f * (float)UI_MENU_RIM_Y + 1.0f;
    b.x0 = toPixX(x - w * 0.5f - mx) - 1;
    b.x1 = toPixX(x + w * 0.5f + mx) + 1;
    b.y0 = toPixY(y - my) - 1;
    b.y1 = toPixY(y + asc + desc + my) + 1;
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
    /* the menus' text, sheet strips in the light ink */
    ui_DrawMenuText(160.0f, 60.0f, 60.0f, col, "ICO", UI_ALIGN_CENTER, UI_INK_LIGHT, NULL);
    ui_DrawMenuText(320.0f, 200.0f, 60.0f, col, "Éléphant", UI_ALIGN_CENTER, UI_INK_LIGHT, NULL);
    /* a big I for the blend: alpha 0x80 left, 0x40 right */
    ui_DrawMenuText(480.0f, 40.0f, 150.0f, col, "I", UI_ALIGN_CENTER, UI_INK_LIGHT, NULL);
    ui_DrawMenuText(560.0f, 40.0f, 150.0f, colI, "I", UI_ALIGN_CENTER, UI_INK_LIGHT, NULL);
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
    ui_MenuFontMetrics(60.0f, &asc, NULL, &cap);
    const int capTop = toPixY(200.0f + asc - cap);
    /* the first letter's columns: only its acute rises above the capitals
       (the capitals' top less the snap and the rim, a few lines) */
    const int eAcuteX1 = ele.x0 + 4 + (toPixX(ui_MeasureMenuText(60.0f, "É")) - toPixX(0.0f));
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
            above += b && x < eAcuteX1 && y < capTop - 6;
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

    /* the blend in the stems of the two I: the sheet texel is the fill
       (white) at full coverage (alpha 0x80), MODULATE gives (255 * c) >> 7 */
    int fullHits = 0, halfHits = 0, worst = 0;
    /* the stems' rows only: the capitals' top to the baseline, 3 lines in
       (the rim's band under the stem is a flat run of the rim grey too) */
    float iAsc, iCap;
    ui_MenuFontMetrics(150.0f, &iAsc, NULL, &iCap);
    const int stemY0 = toPixY(40.0f + iAsc - iCap) + 3, stemY1 = toPixY(40.0f + iAsc) - 3;
    for (int side = 0; side < 2; side++) {
        const Box *bx = side ? &iR : &iL;
        const int cx = (bx->x0 + bx->x1) / 2;
        for (int y = stemY0; y <= stemY1; y++) {
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

/* ------------------------------------------------------- pixels at 4x
 *
 * The Enhanced preset at 4x (SCENE 2048 x 2048 for the 512 x 512 GS frame),
 * full height: the menu rows as the title and the vibration screen draw
 * them (size 27 and 22) as sheet strips, the same 1x
 * coverage whatever the output (menu_font.h).  Checked against the CPU
 * reference of font_sheet_ps (port/render/test/sheet_ref.c): each SCENE
 * texel whose centre lies in a strip's quad samples the page at its own
 * position (the texel-centre convention), MODULATE with the vertex colour,
 * blended as the GS does.  A bleed line (a neighbouring strip sampled
 * through a too-thin gap) and a clipped edge (a quad moved or cut to whole
 * GS pixels) both show as texels that differ from the reference; painted
 * texels outside every quad are the bleed's lines past the strip. */

#define S4 4
#define W4 (512 * S4)

typedef struct Q4 {
    float x0, y0, x1, y1; /* SCENE texels */
    float u0, v0, u1, v1; /* page texels */
    uint32_t tex;
    uint8_t rgba[4]; /* vertex colour, GS */
} Q4;

static void collectAll(void *user, int list, uint32_t index, const RdCmd *c, const RdStateBlock *s)
{
    Walk *w = user;
    (void)index;
    if (list == 11 && c->type == RDC_SCREEN && w->n < 64) {
        w->cmd[w->n] = c;
        w->st[w->n] = *s;
        w->n++;
    }
}

static const char *const kRows4[] = {"New Game",  "Settings", "Quit to desktop",
                                     "Vibration", "Activate", "Deactivate"};
static const float kRowY4[] = {165.0f * 2.0f, 175.0f * 2.0f, 185.0f * 2.0f,
                               60.0f * 2.0f,  80.0f * 2.0f,  100.0f * 2.0f};
static const float kRowSize4[] = {27.0f, 22.0f, 22.0f, 27.0f, 27.0f, 27.0f};

static void drawRows4(const uint8_t bg[4], const uint8_t col[4], int ink)
{
    rd_BeginFrame();
    rd_SelectList(0);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), bg, 1, 0);
    rd_SelectList(11);
    for (unsigned i = 0; i < sizeof(kRows4) / sizeof(kRows4[0]); i++) {
        ui_DrawMenuText(320.0f, kRowY4[i], kRowSize4[i], col, kRows4[i],
                        UI_ALIGN_CENTER | UI_VALIGN_MIDDLE, ink, NULL);
    }
    rd_EndFrame(0);
}

/* the frame's strip quads in SCENE texels (XYOFFSET 2048 - 256) */
static int textQuads4(Q4 *q, int max)
{
    const RdFrame *f = rd__LastFrame();
    Walk w;
    memset(&w, 0, sizeof(w));
    RdStateBlock s = f->startState;
    rd__Walk(f, 0, &s, collectAll, &w);
    int n = 0;
    for (int i = 0; i < w.n; i++) {
        const RdCmd *c = w.cmd[i];
        const RdScreenVtx *v = (const RdScreenVtx *)(f->payload + c->u[0]);
        for (uint32_t k = 0; k + 1 < c->u[1] && n < max; k += 2) {
            Q4 *o = &q[n++];
            o->x0 = ((float)v[k].x / 16.0f - 1792.0f) * S4;
            o->y0 = ((float)v[k].y / 16.0f - 1792.0f) * S4;
            o->x1 = ((float)v[k + 1].x / 16.0f - 1792.0f) * S4;
            o->y1 = ((float)v[k + 1].y / 16.0f - 1792.0f) * S4;
            o->u0 = v[k].s / 16.0f;
            o->v0 = v[k].t / 16.0f;
            o->u1 = v[k + 1].s / 16.0f;
            o->v1 = v[k + 1].t / 16.0f;
            o->tex = w.st[i].tex;
            memcpy(o->rgba, v[k + 1].rgba, 4);
        }
    }
    return n;
}

/* the rows' part of SCENE as a PNG beside the test */
static void writeCrop4(const char *name, const uint8_t *px)
{
    const int cx0 = 160 * S4, cy0 = 120 * S4, cw = 192 * S4, ch = 310 * S4;
    uint8_t *crop = malloc((size_t)cw * ch * 4);
    if (!crop) {
        return;
    }
    for (int y = 0; y < ch; y++) {
        memcpy(&crop[(size_t)y * cw * 4], &px[((size_t)(cy0 + y) * W4 + cx0) * 4], (size_t)cw * 4);
    }
    rd_WritePng(name, crop, (uint32_t)cw, (uint32_t)ch, (uint32_t)cw * 4, 0);
    free(crop);
}

/* the reference of the frame drawRows4 drew over bg: per channel, each
   quad's texels through sheetref_Sample with the page's style, MODULATE
   ((texel * vertex) >> 7) and the 0x44 blend; in: inside some quad */
static int reference4(const uint8_t bg[4], float *ref, uint8_t *in, Q4 *q, int max)
{
    const int nq = textQuads4(q, max);
    for (size_t i = 0; i < (size_t)W4 * W4; i++) {
        for (int ch = 0; ch < 3; ch++) {
            ref[i * 3 + (size_t)ch] = (float)bg[ch];
        }
        in[i] = 0;
    }
    for (int i = 0; i < nq; i++) {
        const Q4 *g = &q[i];
        const RdTexRec *t = rd__TexRec(g->tex);
        if (!t || !t->pixels || t->format != RD_TEXEL_SHEET) {
            CHECK(0, "4x: quad %d's sheet page", i);
            continue;
        }
        const RdSheetStyle st = {t->sheet[0] != 0, t->sheet[1], t->sheet[2],
                                 t->sheet[3],      t->sheet[0], t->sheetScale};
        /* a scaled page's coverage is its top half; its texels under
           the quad made once (sheetref_Texel dilates four sheet texels a texel)
           and blended as sheetref_Sample blends them */
        const uint32_t th = t->sheetScale > 1 ? t->h / 2 : t->h;
        const int tx0 = (int)floorf(g->u0) - 2, ty0 = (int)floorf(g->v0) - 2;
        const int tcw = (int)ceilf(g->u1) + 2 - tx0, tch = (int)ceilf(g->v1) + 2 - ty0;
        uint8_t *tg = malloc((size_t)tcw * (size_t)tch * 2);
        if (!tg) {
            CHECK(0, "4x: memory");
            continue;
        }
        for (int ty = 0; ty < tch; ty++) {
            for (int tx = 0; tx < tcw; tx++) {
                uint8_t *e = &tg[((size_t)ty * (size_t)tcw + (size_t)tx) * 2];
                sheetref_Texel(t->pixels, t->w, th, tx0 + tx, ty0 + ty, &st, &e[0], &e[1]);
            }
        }
        for (int y = (int)floorf(g->y0); y <= (int)ceilf(g->y1); y++) {
            for (int x = (int)floorf(g->x0); x <= (int)ceilf(g->x1); x++) {
                /* rd's convention on a scaled target:
                 * texel i of a GS pixel's block samples at GS
                   p + (i mod s) / s, as the GS samples pixel p at p */
                const float cx = (float)x, cy = (float)y;
                if (x < 0 || y < 0 || x >= W4 || y >= W4 || cx < g->x0 || cx >= g->x1 ||
                    cy < g->y0 || cy >= g->y1) {
                    continue;
                }
                const float u = g->u0 + (cx - g->x0) / (g->x1 - g->x0) * (g->u1 - g->u0);
                const float v = g->v0 + (cy - g->y0) / (g->y1 - g->y0) * (g->v1 - g->v0);
                uint8_t texel[4];
                {
                    /* sheetref_Sample on the texels made above */
                    const float px = (u / (float)t->w) * (float)t->w - 0.5f;
                    const float py = (v / (float)th) * (float)th - 0.5f;
                    const float bx = floorf(px), by = floorf(py), fx = px - bx, fy = py - by;
                    const int ix = (int)bx - tx0, iy = (int)by - ty0;
                    float gg[2][2], al[2][2];
                    for (int k = 0; k < 4; k++) {
                        const uint8_t *e =
                            &tg[((size_t)(iy + (k >> 1)) * (size_t)tcw + (size_t)(ix + (k & 1))) *
                                2];
                        gg[k >> 1][k & 1] = (float)e[0];
                        al[k >> 1][k & 1] = (float)e[1];
                    }
                    const float g0 = gg[0][0] + (gg[0][1] - gg[0][0]) * fx,
                                g1 = gg[1][0] + (gg[1][1] - gg[1][0]) * fx;
                    const float a0 = al[0][0] + (al[0][1] - al[0][0]) * fx,
                                a1 = al[1][0] + (al[1][1] - al[1][0]) * fx;
                    texel[0] = texel[1] = texel[2] = (uint8_t)floorf(g0 + (g1 - g0) * fy + 0.5f);
                    texel[3] = (uint8_t)floorf(a0 + (a1 - a0) * fy + 0.5f);
                }
                const int as = (texel[3] * g->rgba[3]) >> 7;
                float *d = &ref[((size_t)y * W4 + (size_t)x) * 3];
                for (int ch = 0; ch < 3; ch++) {
                    int cs = (texel[ch] * g->rgba[ch]) >> 7;
                    cs = cs > 255 ? 255 : cs;
                    d[ch] += ((float)cs - d[ch]) * (float)as / 128.0f;
                }
                in[(size_t)y * W4 + (size_t)x] = 1;
            }
        }
        free(tg);
    }
    return nq;
}

/* the median width of the letters' edges across, in SCENE
   texels: on every row, the texels strictly between one at most 10 % of
   the full white (255) and the next at least 90 %, or back (a stem's left
   and right edge), the run in between neither */
static int edgeWidth4(const uint8_t *px)
{
    enum { LO = 25, HI = 230, MAXW = 32 };

    int hist[MAXW + 1];
    memset(hist, 0, sizeof(hist));
    int n = 0;
    for (int y = 0; y < W4; y++) {
        int last = -1, lastHi = 0;
        for (int x = 0; x < W4; x++) {
            const int v = px[((size_t)y * W4 + (size_t)x) * 4];
            if (v <= LO || v >= HI) {
                const int hi = v >= HI;
                if (last >= 0 && hi != lastHi) {
                    const int k = x - last - 1;
                    hist[k < MAXW ? k : MAXW]++;
                    n++;
                }
                last = x;
                lastHi = hi;
            }
        }
    }
    for (int k = 0, acc = 0; k <= MAXW; k++) {
        acc += hist[k];
        if (2 * acc >= n) {
            return n ? k : -1;
        }
    }
    return -1;
}

static void testPixels4x(RdFilterUpgrade filter, uint32_t outputHeight, int pngs)
{
    RdSettings e = *rd_GetSettings();
    e.preset = RD_PRESET_ENHANCED;
    e.sceneScale = (float)S4;
    e.aspect = 4.0f / 3.0f;
    e.fullHeightScene = 1;
    e.filterUpgrade = (uint8_t)filter;
    e.outputWidth = outputHeight * 4 / 3;
    e.outputHeight = outputHeight;
    rd_SetSettings(&e);
    ui_SetScale(ui_ScaleFor(1, e.outputHeight));

    uint8_t *px = malloc((size_t)W4 * W4 * 4);
    float *ref = calloc((size_t)W4 * W4 * 3, sizeof(float));
    uint8_t *in = calloc((size_t)W4 * W4, 1);
    Q4 *q = malloc(sizeof(Q4) * 1024);
    if (!px || !ref || !in || !q) {
        CHECK(0, "4x: memory");
        goto done;
    }
    /* 1: white letters without the rim (the plain ink) over black, then
       2: the menu look (the light ink: its rim) over a mid grey, each
       against the reference */
    static const uint8_t black[4] = {0, 0, 0, 0x80}, white[4] = {0x80, 0x80, 0x80, 0x80};
    static const uint8_t mid[4] = {96, 88, 76, 0x80}, light[4] = {0x70, 0x70, 0x70, 0x80};
    for (int pass = 0; pass < 2; pass++) {
        const uint8_t *bg = pass ? mid : black;
        drawRows4(bg, pass ? light : white, pass ? UI_INK_LIGHT : UI_INK_PLAIN);
        uint32_t w = 0, h = 0;
        if (!rd__ReadTarget(rd_Target(RD_TARGET_SCENE), px, (size_t)W4 * W4 * 4, &w, &h) ||
            w != W4 || h != W4) {
            CHECK(0, "4x SCENE readback (%ux%u)", w, h);
            goto done;
        }
        const int nq = reference4(bg, ref, in, q, 1024);
        CHECK(nq == 6, "4x: %d strips (a row each: 6)", nq);
        int bad = 0, bleed = 0, worst = 0, inked = 0;
        for (int y = 0; y < W4; y++) {
            for (int x = 0; x < W4; x++) {
                const size_t i = (size_t)y * W4 + (size_t)x;
                const uint8_t *p = &px[i * 4];
                int d = 0;
                for (int ch = 0; ch < 3; ch++) {
                    const int want = (int)lrintf(ref[i * 3 + (size_t)ch]);
                    const int dc = abs((int)p[ch] - want);
                    d = dc > d ? dc : d;
                }
                inked += abs((int)p[0] - (int)bg[0]) > 64;
                if (!in[i]) {
                    bleed += p[0] != bg[0] || p[1] != bg[1] || p[2] != bg[2];
                } else if (d > 6) {
                    bad++;
                }
                if (d > worst) {
                    worst = d;
                }
            }
        }
        printf("ui_test: 4x %s (filter %d, output %u lines): %d strips, %d inked texels, %d off "
               "the reference by more than 6 (worst %d), %d painted outside the quads\n",
               pass ? "light ink" : "plain ink", (int)filter, outputHeight, nq, inked, bad, worst,
               bleed);
        if (pngs) {
            writeCrop4(pass ? "ui_test_scene4x.png" : "ui_test_scene4x_plain.png", px);
        }
        CHECK(inked > 20000, "4x: %d inked texels", inked);
        CHECK(bleed == 0, "4x: %d texels painted outside the strips' quads (bleed)", bleed);
        CHECK(bad == 0, "4x: %d texels differ from the reference (clipped or shifted strips)", bad);
        if (pass == 0) {
            /* the strips are rasterised at the scene's scale
               (4), so a stem's edge is a texel or two wide, where the 1x
               strip magnified (forced) spreads it over its magnified texel */
            const int crisp = edgeWidth4(px);
            ui__MenuForceScale(1);
            drawRows4(bg, white, UI_INK_PLAIN);
            int soft = -1;
            if (rd__ReadTarget(rd_Target(RD_TARGET_SCENE), px, (size_t)W4 * W4 * 4, &w, &h)) {
                soft = edgeWidth4(px);
            }
            ui__MenuForceScale(0);
            printf("ui_test: 4x edges across (filter %d): median %d texels with the strips at 4x, "
                   "%d with the 1x strips magnified\n",
                   (int)filter, crisp, soft);
            CHECK(crisp >= 0 && crisp <= 2, "4x: the letters' edges are %d texels wide (2 allowed)",
                  crisp);
            CHECK(soft > crisp, "4x: the 1x strips' edges (%d) wider than the 4x strips' (%d)",
                  soft, crisp);
        }
        if (pass == 1 && pngs) {
            /* the menu look with the 1x strips magnified,
               beside ui_test_scene4x.png: the rims alike, the letters
               crisper at 4x */
            ui__MenuForceScale(1);
            drawRows4(bg, light, UI_INK_LIGHT);
            if (rd__ReadTarget(rd_Target(RD_TARGET_SCENE), px, (size_t)W4 * W4 * 4, &w, &h)) {
                writeCrop4("ui_test_scene4x_1x.png", px);
            }
            ui__MenuForceScale(0);
        }
    }
done:
    free(px);
    free(ref);
    free(in);
    free(q);
}

/* The size sets: 40 sizes of "IHL" measured (no Arimo
   set made) and then drawn over five frames: each draw has its own set at
   its exact pixel size, the least recently drawn sets go and "the nearest"
   is never reused (the plain glyph path alone). */
static void testSizeSets(void)
{
    enum { N = 40, PER_FRAME = 8 };

    static const uint8_t white[4] = {0x80, 0x80, 0x80, 0x80};
    if (!rd__InitRecordOnly(512, 512)) {
        CHECK(0, "rd__InitRecordOnly");
        return;
    }
    ui_FontShutdown();
    const float before = ui_GetScale();
    ui_SetScale(1.0f);
    CHECK(ui_FontInit() && ui__FontSizeSets(NULL, 0) == 0, "no size set to start");
    float sizes[N];
    int pxs[N];
    for (int i = 0; i < N; i++) {
        sizes[i] = 8.0f + 2.0f * (float)i;
        pxs[i] = (int)lrintf(sizes[i]);
        CHECK(ui_MeasureText(sizes[i], "IHL") > 0.0f, "size %.0f measured", (double)sizes[i]);
    }
    CHECK(ui__FontSizeSets(NULL, 0) == 0, "measuring made %d size sets (none)",
          ui__FontSizeSets(NULL, 0));
    for (int f = 0; f < N / PER_FRAME; f++) {
        rd_BeginFrame();
        rd_SelectList(11);
        rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
        for (int k = 0; k < PER_FRAME; k++) {
            const int i = f * PER_FRAME + k;
            const float w = ui_MeasureText(sizes[i], "IHL");
            ui_DrawText(100.0f, 100.0f, sizes[i], white, "IHL", 0);
            int w0 = 0, h0 = 0;
            CHECK(ui_FontPage(pxs[i], 0, &w0, &h0) != NULL && ui_FontPageTex(pxs[i], 0) != 0,
                  "size %.0f: drawn from its own %d px set", (double)sizes[i], pxs[i]);
            CHECK(ui_MeasureText(sizes[i], "IHL") == w, "size %.0f: the measure unchanged",
                  (double)sizes[i]);
        }
        rd_EndFrame(0);
    }
    const int live = ui__FontSizeSets(NULL, 0);
    CHECK(!ui__FontReusedNearest(), "no draw reused the nearest size (%d sets alive)", live);
    ui_SetScale(before);
    rd_Shutdown();
    ui_FontForgetTextures();
}

int main(void)
{
    testGlyphs();
    testUtf8();
    testMeasure();
    testStrings();
    testLayoutExtension();
    testPopups();
    testOverlay();
    testAtlasUploads();
    testSizeSets();
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
    /* 4x at a 960-line output (the atlas magnified into SCENE), and
       trilinear at 2160 lines (the atlas minified: no mip chain may blur
       neighbouring glyphs in) */
    testPixels4x(RD_FILTER_UPGRADE_OFF, 960, 1);
    testPixels4x(RD_FILTER_UPGRADE_TRILINEAR, 2160, 0);
    CHECK(rhi_vk_ValidationErrorCount() == 0, "%u validation errors",
          rhi_vk_ValidationErrorCount());
    ui_FontShutdown();
    rd_Shutdown();
    testPopupPixels();
    if (failures) {
        printf("ui_test: %d failures\n", failures);
        return 1;
    }
    printf("ui_test: ok\n");
    return 0;
}

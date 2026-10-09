/* settings_photo_test.c: settings_render, the Settings screens run through rd
 * on a Vulkan device (lavapipe here; exit 77 without one) with the window
 * build's GifPacket.c, DisplayList.c and DmaPacket.c, over the shared
 * fixture (settings_fixture.h, compiled with SETTINGS_RENDER).  Each
 * screen's SCENE is written as a PNG beside the test for a look
 * (settings_<screen>.png): no game data, the backdrop over a flat colour;
 * then each screen presented at Enhanced 1920 x 1080
 * (settings_<screen>_1080.png).  The port's rows and the game's menu words
 * are sheet strips in the scene list at 1x (no RDC_OVERLAY_TEXT item on any
 * screen), and photo mode's help panel is laid out in every language. */
#include "settings_fixture.h"

/* package K-D: model_viewer.c's characters calls for the viewer's panel
   (Ico on screen; the model itself needs the game) */
static int renderCharsEnter(void)
{
    return 0;
}

static int renderCharsShown(void)
{
    return 0;
}

static void renderCharsSwitch(int character)
{
    (void)character;
}

static void renderCharsLeave(void) {}

/* SCENE (512 x 512 for the 640 x 448 grid) to a PNG at 4:3, 683 x 512 */
static void snap(const char *name)
{
    char p[1100];
    uint32_t w = 0, h = 0;
    uint8_t *px = malloc(512 * 512 * 4), *out = malloc(683 * 512 * 4);
    if (!px || !out || !rd__ReadTarget(rd_Target(RD_TARGET_SCENE), px, 512 * 512 * 4, &w, &h)) {
        CHECK(0, "SCENE readback for %s", name);
        free(px);
        free(out);
        return;
    }
    for (int y = 0; y < 512; y++) {
        for (int x = 0; x < 683; x++) {
            memcpy(&out[(y * 683 + x) * 4], &px[(y * 512 + x * 512 / 683) * 4], 4);
        }
    }
    path(p, sizeof(p), name);
    rd_WritePng(p, out, 683, 512, 683 * 4, 0);
    printf("settings_render: %s\n", p);
    free(px);
    free(out);
}

/* T1: SCENE at the Enhanced 4x scale (2048 x 2048) to a PNG at 4:3,
   2731 x 2048: the port font's rows as a 4x output shows them */
static void snap4(const char *name)
{
    char p[1100];
    uint32_t w = 0, h = 0;
    const size_t n = (size_t)2048 * 2048 * 4;
    uint8_t *px = malloc(n), *out = malloc((size_t)2731 * 2048 * 4);
    if (!px || !out || !rd__ReadTarget(rd_Target(RD_TARGET_SCENE), px, n, &w, &h) || w != 2048 ||
        h != 2048) {
        CHECK(0, "4x SCENE readback for %s (%ux%u)", name, w, h);
        free(px);
        free(out);
        return;
    }
    for (int y = 0; y < 2048; y++) {
        for (int x = 0; x < 2731; x++) {
            memcpy(&out[((size_t)y * 2731 + (size_t)x) * 4],
                   &px[((size_t)y * 2048 + (size_t)x * 2048 / 2731) * 4], 4);
        }
    }
    path(p, sizeof(p), name);
    rd_WritePng(p, out, 2731, 2048, 2731 * 4, 0);
    printf("settings_render: %s\n", p);
    free(px);
    free(out);
}

/* The button glyphs' sheets: no disc data here, so drawn stand-ins with the
   real sheets' layout (buttons.tm2's triangle, square, circle and cross in
   32 x 30 cells; menu_PAL_01 / 02's L1, R1, L2, R2 and the value arrows at
   their rectangles), bound by tex_TransTexture through a TEX0 the resolver maps:
   the snapshots show where and how large the glyphs are, the window build's
   dumps show the game's own. */
#define FAKE_TBP 0x1000
static RdTex s_sheet[2];

static void plot(uint8_t *px, int w, int x, int y, const uint8_t c[3])
{
    uint8_t *p = &px[((size_t)y * (size_t)w + (size_t)x) * 4];
    p[0] = c[0];
    p[1] = c[1];
    p[2] = c[2];
    p[3] = 0x80;
}

/* a thick segment (a, b) inside the box x0..x1, y0..y1 */
static void segment(uint8_t *px, int w, float ax, float ay, float bx, float by, float th,
                    const uint8_t c[3])
{
    int x0 = (int)(ax < bx ? ax : bx) - 3, x1 = (int)(ax > bx ? ax : bx) + 3;
    int y0 = (int)(ay < by ? ay : by) - 3, y1 = (int)(ay > by ? ay : by) + 3;
    float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float t = l2 > 0.0f ? ((x + 0.5f - ax) * dx + (y + 0.5f - ay) * dy) / l2 : 0.0f;
            t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
            float ex = ax + t * dx - (x + 0.5f), ey = ay + t * dy - (y + 0.5f);
            if (ex * ex + ey * ey <= th * th * 0.25f) {
                plot(px, w, x, y, c);
            }
        }
    }
}

static void makeSheets(void)
{
    static uint8_t b[64 * 64 * 4], m[512 * 256 * 4];
    static const uint8_t green[3] = {0x30, 0xE0, 0x90}, pink[3] = {0xE8, 0xA0, 0xC0},
                         red[3] = {0xF0, 0x78, 0x60}, blue[3] = {0x78, 0x98, 0xE8},
                         white[3] = {0xE8, 0xE8, 0xE8};
    memset(b, 0, sizeof(b));
    memset(m, 0, sizeof(m));
    segment(b, 64, 16, 5, 4, 26, 3, green);
    segment(b, 64, 4, 26, 28, 26, 3, green);
    segment(b, 64, 28, 26, 16, 5, 3, green);
    segment(b, 64, 38, 5, 58, 5, 3, pink);
    segment(b, 64, 58, 5, 58, 25, 3, pink);
    segment(b, 64, 58, 25, 38, 25, 3, pink);
    segment(b, 64, 38, 25, 38, 5, 3, pink);
    for (int k = 0; k < 32; k++) {
        float a0 = (float)k * 6.2831853f / 32.0f, a1 = (float)(k + 1) * 6.2831853f / 32.0f;
        segment(b, 64, 16 + 10 * cosf(a0), 45 + 10 * sinf(a0), 16 + 10 * cosf(a1),
                45 + 10 * sinf(a1), 3, red);
    }
    segment(b, 64, 38, 35, 58, 55, 3, blue);
    segment(b, 64, 58, 35, 38, 55, 3, blue);
    /* L1 (420, 240), R1 (340, 240), L2 (460, 240) and R2 (380, 240), 40 x 15:
       the letters as strokes */
    for (int k = 0; k < 4; k++) {
        static const float xs[4] = {428.0f, 348.0f, 468.0f, 388.0f};
        const float x = xs[k];
        if (k == 0 || k == 2) {
            segment(m, 512, x, 242, x, 252, 2, white); /* L */
            segment(m, 512, x, 252, x + 7, 252, 2, white);
        } else {
            segment(m, 512, x, 242, x, 252, 2, white); /* R */
            segment(m, 512, x, 242, x + 6, 242, 2, white);
            segment(m, 512, x + 6, 242, x + 6, 247, 2, white);
            segment(m, 512, x + 6, 247, x, 247, 2, white);
            segment(m, 512, x + 2, 247, x + 7, 252, 2, white);
        }
        if (k < 2) {
            segment(m, 512, x + 14, 244, x + 17, 242, 2, white); /* 1 */
            segment(m, 512, x + 17, 242, x + 17, 252, 2, white);
        } else {
            segment(m, 512, x + 13, 242, x + 19, 242, 2, white); /* 2 */
            segment(m, 512, x + 19, 242, x + 19, 247, 2, white);
            segment(m, 512, x + 19, 247, x + 13, 247, 2, white);
            segment(m, 512, x + 13, 247, x + 13, 252, 2, white);
            segment(m, 512, x + 13, 252, x + 19, 252, 2, white);
        }
    }
    /* the value arrows (490, 130) and (490, 150), 20 x 20 */
    segment(m, 512, 504, 133, 496, 140, 2, white);
    segment(m, 512, 496, 140, 504, 147, 2, white);
    segment(m, 512, 496, 153, 504, 160, 2, white);
    segment(m, 512, 504, 160, 496, 167, 2, white);
    /* the pause menu's Options (384, 120), 128 x 20: the English word's
       extent on the sheet (texels 7 to 84), a framed bar with a cross */
    segment(m, 512, 392, 124, 468, 124, 2, white);
    segment(m, 512, 392, 136, 468, 136, 2, white);
    segment(m, 512, 392, 124, 392, 136, 2, white);
    segment(m, 512, 468, 124, 468, 136, 2, white);
    segment(m, 512, 392, 124, 468, 136, 2, red);
    s_sheet[0] = rd_CreateTexture(64, 64, b, RD_TEXA_80_80, "settings_render buttons");
    s_sheet[1] = rd_CreateTexture(512, 256, m, RD_TEXA_80_80, "settings_render menu sheet");
}

/* seki/src/Texture.c (the photo panel's pictures, photo_ui.c): the fake
   tables' texture numbers 1 (buttons.tm2) and 2, 3 (menu_PAL_02, 01) are
   the stand-in sheets */
unsigned int tex_HostTextureId(int idx)
{
    return idx == 1 ? s_sheet[0].id : idx == 2 || idx == 3 ? s_sheet[1].id : 0u;
}

static RdTex sheetResolve(unsigned long long tex0, int list)
{
    (void)list;
    unsigned tbp = (unsigned)(tex0 & 0x3FFF);
    return tbp == FAKE_TBP + 0x40                             ? s_sheet[0]
           : tbp >= FAKE_TBP + 0x80 && tbp <= FAKE_TBP + 0xC0 ? s_sheet[1]
                                                              : (RdTex){0};
}

void bindFakeSheet(int no)
{
    if (no < 1 || no > 3) {
        return;
    }
    const unsigned long long w = no == 1 ? 64 : 512, h = no == 1 ? 64 : 256;
    const unsigned long long tw = no == 1 ? 6 : 9, th = no == 1 ? 6 : 8;
    unsigned long long tex0 = (unsigned long long)(FAKE_TBP + 0x40 * no) | (w / 64) << 14 |
                              tw << 26 | th << 30 | 1ull << 34;
    (void)h;
    gif_StartPacketPri(11);
    gif_SetGsReg(0x06, (long long)tex0);
    gif_EndPacket();
}

/* the model viewer's prompts and top-left rows as port/game/model_viewer.c
   lays them out (its layout needs the game; the prompt lines are
   ui_hint.c's own), over no model */
static int viewerLayout(void)
{
    static UiHint sticks, keys;
    int first = LT_GAME_PROPERTY_COUNT + lt_ext_PropCount();
    int name = ui_SettingsAddRow(24, 10, 360, 30, 0, -1, 0, "Ico", 24.0f, UI_ALIGN_LEFT);
    ui_SettingsAddRow(24, 26, 360, 30, 0, -1, 0, "Animation: BOY STAND  \xC2\xB7  Loop", 19.0f,
                      UI_ALIGN_LEFT);
    ui_SettingsAddRow(24, 38, 360, 30, 0, -1, 0, "Frame 117 / 299", 19.0f, UI_ALIGN_LEFT);
    ui_HintBuild(&sticks, 180, 19.0f, ui_hint_mv_sticks, UI_HINT_MV_STICKS_COUNT);
    ui_HintBuild(&keys, 196, 19.0f, ui_hint_mv_keys, UI_HINT_MV_KEYS_COUNT);
    LtProp l;
    memset(&l, 0, sizeof(l));
    l.first = first;
    l.last = LT_GAME_PROPERTY_COUNT + lt_ext_PropCount();
    l.colA = 0.0f;
    l.procFirst = 1;
    l.defaultItem = l.curItem = -1;
    l.link = -1;
    (void)name;
    return lt_ext_AddLayout(&l);
}

/* package DEF: the RDC_OVERLAY_TEXT items of the last frame */
static int textItems(void)
{
    const RdFrame *f = rd__LastFrame();
    int n = 0;
    for (int l = 0; f && l < RD_LIST_COUNT; l++) {
        for (uint32_t i = 0; i < f->lists[l].count; i++) {
            n += f->lists[l].cmds[i].type == RDC_OVERLAY_TEXT &&
                 f->lists[l].cmds[i].b[0] == RD_OTEXT_ITEM;
        }
    }
    return n;
}

/* the screen presented at Enhanced 1920 x 1080 (16:9) to name_1080.png.
   v0.4.2 (package F-B): nothing is deferred any more, the port's rows and
   the game's menu words alike are sheet strips in the scene list at 1x, so
   the frame records no RDC_OVERLAY_TEXT item */
static void snap1080(const char *name)
{
    const uint32_t w = 1920, h = 1080;
    uint8_t *px = malloc((size_t)w * h * 4);
    char file[256], p[1400];
    frame(0);
    const int items = textItems();
    CHECK(items == 0, "%s: %d deferred items (none)", name, items);
    uint32_t ow = 0, oh = 0;
    if (!px || !rd_ReadPresented(px, &ow, &oh) || ow != w || oh != h) {
        CHECK(0, "%s: the presented output", name);
        free(px);
        return;
    }
    snprintf(file, sizeof(file), "%s_1080.png", name);
    path(p, sizeof(p), file);
    rd_WritePng(p, px, w, h, w * 4, 0);
    printf("settings_render: %s\n", p);
    free(px);
}

/* the save screen's values: layout 14's
   slot numbers (files 1, 2 and 5 used, the others empty) and layout 15's
   play time "12:34:56", at the PAL rows' places, their texel rectangles the
   menu text table's, everything else of the two layouts masked.  v0.4.2
   (package F-B): rows of the menu text table, so sheet strips in their
   inks (the fake tables need no sheet for them) */
static void fakeSaveRows(void)
{
    for (int r = 52; r < 176; r++) {
        setRow(r, -1, -1, -1, -1, -1, -1, 0);
        texProperty[r].masked = 1;
        texProperty[r].selectable = 0;
    }
    for (int i = 0; i < ui_menu_text_row_count; i++) {
        const int r = ui_menu_text_rows[i].row;
        if (r >= 52 && r < 176) {
            const UiMenuTextItem *it = &ui_menu_text_items[ui_menu_text_rows[i].item];
            texProperty[r].texU = it->u;
            texProperty[r].texV = it->v;
            texProperty[r].texW = it->w;
            texProperty[r].texH = it->h;
            texProperty[r].dispW = 0;
            texProperty[r].dispH = 30;
        }
    }
    static const int slotX[5] = {210, 260, 310, 360, 410};
    for (int i = 0; i < 10; i++) {
        const int used = i == 0 || i == 1 || i == 4;
        LtProperty *e = &texProperty[(used ? 62 : 52) + i];
        e->dispX = i == 9 ? 404 : slotX[i % 5];
        e->dispY = i < 5 ? 70 : 90;
        e->masked = 0;
    }
    /* layout_action.c _la_set_preview_info: digit d of a pair at row base +
       (d + 9) % 10; hours 76 / 86, minutes 96 / 106, seconds 116 / 126,
       the colons 74 and 75 */
    static const int base[6] = {76, 86, 96, 106, 116, 126};
    static const int pos[6] = {240, 260, 300, 320, 360, 380};
    static const int digits[6] = {1, 2, 3, 4, 5, 6};
    for (int k = 0; k < 6; k++) {
        LtProperty *e = &texProperty[base[k] + (digits[k] + 9) % 10];
        e->dispX = pos[k];
        e->dispY = 160;
        e->masked = 0;
    }
    texProperty[74].dispX = 280;
    texProperty[75].dispX = 340;
    texProperty[74].dispY = texProperty[75].dispY = 160;
    texProperty[74].masked = texProperty[75].masked = 0;
    for (int r = 52; r < 176; r++) {
        texProperty[r].defaultMask = texProperty[r].masked; /* kept by the switch */
    }
    setLayout(14, 52, 74, -1, 15);
    setLayout(15, 74, 176, -1, -1);
    texLayout[14].fadeInTime = texLayout[15].fadeInTime = 0.0f;
}

/* ------------------------------------------------ package PHOTO: the panel */

/* the last frame's draws on the menu text pages (menu_font.h) */
static void countPageDraw(void *user, int list, uint32_t index, const RdCmd *c,
                          const RdStateBlock *st)
{
    (void)list;
    (void)index;
    if (c->type == RDC_SCREEN && st->ds.texEnabled && ui_MenuFontIsPage(st->tex)) {
        (*(int *)user)++;
    }
}

static int menuTextDraws(void)
{
    const RdFrame *f = rd__LastFrame();
    int n = 0;
    if (f) {
        RdStateBlock s = f->startState;
        rd__Walk(f, 0, &s, countPageDraw, &n);
    }
    return n;
}

/* what the overlay is given while the panel draws: the sprites (the panel's
   rectangle first, then the words and the sheets' pictures), with each
   vertex's texture */
static RdScreenVtx s_pv[4096];
static uint32_t s_ptex[4096];
static uint32_t s_pn;

static void panelSink(RdPrim type, const RdScreenVtx *v, uint32_t n, RdTex tex, RdBlend blend)
{
    (void)type;
    (void)blend;
    for (uint32_t i = 0; i < n && s_pn < 4096; i++) {
        s_ptex[s_pn] = tex.id;
        s_pv[s_pn++] = v[i];
    }
}

/* the texture of a button picture in the panel: one of the stand-in sheets */
static int isSheet(uint32_t tex)
{
    return tex != 0 && (tex == s_sheet[0].id || tex == s_sheet[1].id);
}

/* an icon's word, "" for a picture */
static const char *iconWord(const UiHudIcon *ic)
{
    return ic->glyph >= 0 || !ic->word ? "" : ic->word;
}

/* one camera, one input device, one language: the items placed, no item
   over another, every part inside its item, the widest line within the
   room after the shrink; on the pad no key names, on the keys every button
   a key in square brackets (the default bindings name one for each) */
static void checkPanelLayout(int freeCam, int keyboard, UiLang lang, UiHudSet *set)
{
    static UiHudPlaced at[UI_HUD_ITEMS];
    char what[96];

    snprintf(what, sizeof(what), "panel (%s, %s, language %d)", freeCam ? "free" : "orbit",
             keyboard ? "keys" : "pad", (int)lang);
    ui_SetLanguage(lang);
    ui__PhotoHudBuild(set, freeCam, keyboard, "Photo mode: Free camera, speed Normal",
                      "Field of view 60");
    float size = 17.0f;
    float w = ui__PhotoHudFit(set->item, set->n, &size, UI_PHOTO_HUD_ROOM, at);
    CHECK(w <= UI_PHOTO_HUD_ROOM + 0.5f, "%s: the widest line is %.1f of %.1f at size %.1f", what,
          (double)w, (double)UI_PHOTO_HUD_ROOM, (double)size);
    int lines[UI_HUD_LINES] = {0};
    for (int i = 0; i < set->n; i++) {
        const UiHudItem *it = &set->item[i];
        const UiHudPlaced *p = &at[i];
        lines[it->line]++;
        CHECK(it->text && it->text[0], "%s: item %d has no word", what, i);
        CHECK(p->x1 > p->x0 && p->x0 >= 0.0f, "%s: item %d spans %.1f .. %.1f", what, i,
              (double)p->x0, (double)p->x1);
        if (i > 0 && set->item[i - 1].line == it->line) {
            CHECK(p->x0 >= at[i - 1].x1, "%s: item %d (at %.1f) overlaps the one before (to %.1f)",
                  what, i, (double)p->x0, (double)at[i - 1].x1);
        }
        float right = p->x0;
        for (int k = 0; k < it->nicon; k++) {
            const UiHudIcon *ic = &it->icon[k];
            CHECK(p->iconX[k] >= right - 0.01f && p->iconW[k] > 0.0f,
                  "%s: item %d button %d at %.1f (free from %.1f)", what, i, k, (double)p->iconX[k],
                  (double)right);
            right = p->iconX[k] + p->iconW[k];
            CHECK(ic->glyph >= 0 ? ic->glyph < LT_GLYPH_COUNT : ic->word && ic->word[0],
                  "%s: item %d button %d is neither a picture nor a word", what, i, k);
            const int bracket = iconWord(ic)[0] == '[';
            CHECK(keyboard ? bracket : !bracket, "%s: item %d button %d is \"%s\"", what, i, k,
                  iconWord(ic));
        }
        if (it->text[0] && it->nicon > 0) {
            CHECK(p->textX >= right, "%s: item %d word at %.1f over its buttons (to %.1f)", what, i,
                  (double)p->textX, (double)right);
        }
        CHECK(p->textX + p->textW <= p->x1 + 0.01f, "%s: item %d word leaves its item", what, i);
    }
    for (int l = 0; l < UI_HUD_LINES; l++) {
        CHECK(lines[l] > 0, "%s: line %d is empty", what, l);
    }
}

/* the photo panel drawn through the overlay into the 1080p snapshot */
static void photoOverlay(const RdOverlayCtx *ctx, void *user)
{
    (void)user;
    ui_PhotoDrawOverlay(ctx);
}

static void testPhotoPanel(void)
{
    static UiHudSet set;
    IcoBindings *b = ico_input_live_bindings();

    /* the PAL tables' glyph rows (fakeTables): the sheets' pictures */
    stage_no = 11;
    ico_photo_reset();
    lt_ext_Reset();
    ui_SettingsReset();
    fakeTables();
    useConfig("version = 1\n");
    ico_input_reload_bindings(b);
    for (int lang = 0; lang < UI_LANG_COUNT; lang++) {
        for (int cam = 0; cam < 2; cam++) {
            for (int kb = 0; kb < 2; kb++) {
                checkPanelLayout(cam, kb, (UiLang)lang, &set);
            }
        }
    }
    ui_SetLanguage(UI_LANG_EN);

    /* the pad: the game's pictures where its sheets have one, the names of
       the rest */
    ui__PhotoHudBuild(&set, 1, 0, "t", "f");
    CHECK(set.n == 13, "the free camera's panel: 13 items (%d)", set.n);

    static const struct {
        int item, glyph0, glyph1;
        const char *word0, *word1;
    } kPad[] = {
        {1, -1, -1, "Left stick", NULL},
        {2, -1, -1, "Right stick", NULL},
        {3, -1, -1, "Up", "Down"},
        {4, LT_GLYPH_L1, LT_GLYPH_R1, NULL, NULL},
        {5, LT_GLYPH_L2, LT_GLYPH_R2, NULL, NULL},
        {6, -1, -1, "L3", NULL},
        {7, -1, -1, "R3", NULL},
        {8, -1, -1, "Select", NULL},
        {9, LT_GLYPH_CROSS, -1, NULL, NULL},
        {10, LT_GLYPH_SQUARE, -1, NULL, NULL},
        {11, LT_GLYPH_TRIANGLE, -1, NULL, NULL},
    };

    for (size_t i = 0; i < sizeof(kPad) / sizeof(kPad[0]); i++) {
        const UiHudItem *it = &set.item[kPad[i].item];
        const int want = kPad[i].glyph1 >= 0 || kPad[i].word1 ? 2 : 1;
        int ok = it->nicon == want;
        for (int k = 0; ok && k < want; k++) {
            const int g = k ? kPad[i].glyph1 : kPad[i].glyph0;
            const char *wd = k ? kPad[i].word1 : kPad[i].word0;
            ok = it->icon[k].glyph == g &&
                 (g >= 0 || (it->icon[k].word && strcmp(it->icon[k].word, wd) == 0));
        }
        CHECK(ok, "pad item %d (\"%s\"): %d buttons, the first %d \"%s\"", kPad[i].item, it->text,
              it->nicon, it->icon[0].glyph, iconWord(&it->icon[0]));
    }
    ui_SetLanguage(UI_LANG_DE);
    ui__PhotoHudBuild(&set, 1, 0, "t", "f");
    CHECK(strcmp(iconWord(&set.item[1].icon[0]), "Linker Stick") == 0 &&
              strcmp(iconWord(&set.item[3].icon[1]), "Unten") == 0 &&
              strcmp(iconWord(&set.item[6].icon[0]), "L3") == 0,
          "German: \"%s\", \"%s\", \"%s\"", iconWord(&set.item[1].icon[0]),
          iconWord(&set.item[3].icon[1]), iconWord(&set.item[6].icon[0]));
    ui_SetLanguage(UI_LANG_EN);
    /* tables that are not the PAL ones: a picture's name instead */
    texProperty[182].texU++;
    ui__PhotoHudBuild(&set, 1, 0, "t", "f");
    CHECK(set.item[9].icon[0].glyph == -1 && strcmp(iconWord(&set.item[9].icon[0]), "Cross") == 0,
          "Cross without its PAL rectangle: the word (%s)", iconWord(&set.item[9].icon[0]));
    texProperty[182].texU--;

    /* the keys: W A S D for the stick, the first of "Tab, Backquote" */
    ui__PhotoHudBuild(&set, 1, 1, "t", "f");
    const UiHudItem *move = &set.item[1], *roll = &set.item[4], *save = &set.item[9];
    CHECK(move->nicon == 1 && strcmp(iconWord(&move->icon[0]), "[W A S D]") == 0,
          "the left stick: [W A S D] (%s)", iconWord(&move->icon[0]));
    CHECK(roll->nicon == 2 && strcmp(iconWord(&roll->icon[0]), "[Tab]") == 0 &&
              strcmp(iconWord(&roll->icon[1]), "[F]") == 0,
          "L1 R1: [Tab] [F] (%s %s)", iconWord(&roll->icon[0]), iconWord(&roll->icon[1]));
    CHECK(save->nicon == 1 && strcmp(iconWord(&save->icon[0]), "[Space]") == 0,
          "Cross: [Space] (%s)", iconWord(&save->icon[0]));
    /* a key unbound: the mouse's button; neither: the pad's picture */
    memset(b->kb[ICO_T_CROSS], 0, sizeof(b->kb[ICO_T_CROSS]));
    ui__PhotoHudBuild(&set, 1, 1, "t", "f");
    CHECK(strcmp(iconWord(&set.item[9].icon[0]), "[Mouse left]") == 0,
          "Cross without a key: the mouse (%s)", iconWord(&set.item[9].icon[0]));
    ico_bindings_clear(b, ICO_T_CROSS);
    ui__PhotoHudBuild(&set, 1, 1, "t", "f");
    CHECK(set.item[9].icon[0].glyph == LT_GLYPH_CROSS, "Cross unbound: the pad's picture");
    memset(b->kb[ICO_T_LSTICK_DOWN], 0, sizeof(b->kb[ICO_T_LSTICK_DOWN]));
    ui__PhotoHudBuild(&set, 1, 1, "t", "f");
    CHECK(set.item[1].nicon == 1 && strcmp(iconWord(&set.item[1].icon[0]), "Left stick") == 0,
          "a stick with a key missing: its name (%s)", iconWord(&set.item[1].icon[0]));
    ico_input_reload_bindings(b);

    /* with the photo layout current the game's pause rows are not drawn
       (the deferred-text check needs the 1080p setup this runs inside) */
    ico_photo_reset();
    lt_ext_Reset();
    ui_SettingsReset();
    fakeTables();
    memset(pad, 0, sizeof(pad));
    pad[0].ana[0] = pad[0].ana[1] = pad[0].ana[2] = pad[0].ana[3] = 128;
    init_layout_texture(2);
    settle(54, 4);
    lt_switch_layout(57);
    CHECK(settle(57, 60), "the pause menu");
    frame(0);
    /* v0.4.2 (package F): nothing is deferred; the rows are menu text
       strips in the scene, counted as draws on the menu text pages */
    const int pauseItems = menuTextDraws();
    CHECK(pauseItems > 0, "the pause menu has menu text (%d draws)", pauseItems);
    const int ph = ui_SettingsPhotoRow(), pl = ui_PhotoLayout();
    texLayout[57].curItem = ph;
    press(0x40);
    CHECK(settle(pl, 60) && ico_photo_active(), "photo mode (%d)", current_layout_id);
    frame(0);
    CHECK(menuTextDraws() == 0, "photo mode: the pause layout draws no rows (%d)", menuTextDraws());

    /* the panel through the overlay sink: the pad's buttons (seven
       pictures from the sheets), then your keys (none) */
    RdOverlayCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.outW = 1920;
    ctx.outH = 1080;
    ctx.box.w = 1920;
    ctx.box.h = 1080;
    ctx.boxScale = 1080.0f / 448.0f;
    ui__SetOverlaySink(panelSink);
    for (int keys = 0; keys < 2; keys++) {
        if (keys) {
            ico_input_note_press(ICO_SRC_KEY, 4);
        }
        s_pn = 0;
        CHECK(ico_photo_hud(), "the panel is shown");
        ui_PhotoDrawOverlay(&ctx);
        CHECK(s_pn > 8, "%s: %u vertices drawn", keys ? "keys" : "pad", s_pn);
        /* the first sprite is the panel's rectangle; all the rest lies on it */
        const float x0 = (float)s_pv[0].x / 16.0f - 1.0f, y0 = (float)s_pv[0].y / 16.0f - 1.0f;
        const float x1 = (float)s_pv[1].x / 16.0f + 1.0f, y1 = (float)s_pv[1].y / 16.0f + 1.0f;
        const float left = (1920.0f - 1440.0f) * 0.5f, k = 1440.0f / 640.0f;
        CHECK(x0 >= left + (22.0f - 8.0f) * k - 2.0f && x1 <= left + (618.0f + 8.0f) * k + 2.0f &&
                  y1 <= 1080.0f,
              "%s: the panel is x %.0f .. %.0f, y %.0f .. %.0f", keys ? "keys" : "pad", (double)x0,
              (double)x1, (double)y0, (double)y1);
        /* v0.4.2 (package F): the words are menu text strips, whose quads
           carry a transparent margin round the ink (ceil(0.12 em) plus
           UI_MENU_RIM_X / UI_MENU_RIM_Y texels: 7 field lines, 14 units, at
           the panel's size), so a word's quad may pass the panel by 16
           units (measured 6.4 above and 0.8 right); the pictures may not */
        const float textOver = 16.0f * ctx.boxScale;
        int out = 0, pictures = 0;
        for (uint32_t i = 2; i < s_pn; i++) {
            const float x = (float)s_pv[i].x / 16.0f, y = (float)s_pv[i].y / 16.0f;
            const int sheet = isSheet(s_ptex[i]);
            const float m = sheet ? 0.0f : textOver;
            out += x < x0 - m || x > x1 + m || y < y0 - m || y > y1 + m;
            pictures += sheet;
        }
        CHECK(out == 0, "%s: %d vertices outside the panel", keys ? "keys" : "pad", out);
        CHECK(pictures == (keys ? 0 : 2 * 7), "%s: %d picture vertices (%d)", keys ? "keys" : "pad",
              pictures, keys ? 0 : 2 * 7);
    }
    ui__SetOverlaySink(NULL);

    /* the snapshots, the panel on the output as the window draws it (the
       sheets are the stand-ins makeSheets draws) */
    void *prevUser = NULL;
    RdOverlayFn prev = rd_GetPresentOverlay(&prevUser);
    rd_SetPresentOverlay(photoOverlay, NULL);
    ico_input_note_press(ICO_SRC_PAD, 0);
    snap1080("settings_photo");
    ico_input_note_press(ICO_SRC_KEY, 4);
    snap1080("settings_photo_keys");
    rd_SetPresentOverlay(prev, prevUser);
    ico_photo_reset();
    stage_no = 0;
}

static int render(void)
{
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ORIGINAL;
    if (!rd_Init(512, 512, &st, NULL)) {
        printf("settings_render: SKIP: no usable Vulkan device\n");
        return 77;
    }
    ui_FontForgetTextures();
    UiGsFrame fr = {512, 512, 2048.0f, 2048.0f, UI_LAYOUT_Z};
    ui_SetGsFrame(&fr);
    ui_SetScale(1.0f);
    ui__SetRecordHook(gif_HostFlush);
    makeSheets();
    gif_HostSetTex0Resolver(sheetResolve);
    dl_Init();
    GlobalStageSetting.reductionCol[0] = GlobalStageSetting.reductionCol[1] =
        GlobalStageSetting.reductionCol[2] = 0x80;

    useConfig("version = 1\n");
    fakeTables();
    lt_ext_Reset();
    ui_SettingsReset();
    memset(pad, 0, sizeof(pad));
    pad[0].ana[0] = pad[0].ana[1] = pad[0].ana[2] = pad[0].ana[3] = 128;
    NonLinearCameraMove = 2;
    init_layout_texture(2);
    settle(54, 4);
    int mainL = ui_SettingsPageLayout(UI_PAGE_MAIN);
    lt_switch_layout(mainL);
    CHECK(settle(mainL, 60), "the menu");
    for (int i = 0; i < 4; i++) {
        press(0x4000); /* to Language: its note */
    }
    frame(0);
    snap("settings_main.png");

    static const struct {
        int row; /* main page row */
        int downs;
        const char *name;
    } pages[] = {{0, 0, "settings_display.png"},     {0, 8, "settings_display_videomode.png"},
                 {1, 0, "settings_effects.png"},     {3, 0, "settings_audio.png"},
                 {4, 1, "settings_controls.png"},    {5, 0, "settings_gameplay.png"},
                 {7, 0, "settings_achievements.png"}};

    int labels[16];
    ui_SettingsPageRows(UI_PAGE_MAIN, labels, NULL, NULL, 16);
    for (unsigned i = 0; i < sizeof(pages) / sizeof(pages[0]); i++) {
        lt_ext_Layout(mainL)->curItem = labels[pages[i].row];
        press(0x40);
        for (int k = 0; k < 30; k++) {
            frame(0);
        }
        for (int k = 0; k < pages[i].downs; k++) {
            press(0x4000);
        }
        frame(0);
        snap(pages[i].name);
        press(0x10);
        CHECK(settle(mainL, 60), "back to the menu from %s", pages[i].name);
    }
    /* Audio with a device name too long for its box (cut with an
       ellipsis) and Mono set, the cursor on the device row */
    ico_config_set_string("audio.device",
                          "Speakers (USB Audio Interface with a very long product name)");
    ico_config_set_string("audio.output", "mono");
    ico_opt_reload();
    lt_ext_Layout(mainL)->curItem = labels[3];
    press(0x40);
    for (int k = 0; k < 30; k++) {
        frame(0);
    }
    for (int k = 0; k < 4; k++) {
        press(0x4000);
    }
    frame(0);
    snap("settings_audio_device.png");
    press(0x10);
    CHECK(settle(mainL, 60), "back to the menu from Audio");
    ico_config_set_string("audio.device", "");
    ico_config_set_string("audio.output", "auto");
    ico_opt_reload();
    /* Controls -> Remap, then a capture in progress */
    lt_ext_Layout(mainL)->curItem = labels[4];
    press(0x40);
    CHECK(settle(ui_SettingsPageLayout(UI_PAGE_CONTROLS), 60), "Controls");
    press(0x40);
    CHECK(settle(ui_SettingsPageLayout(UI_PAGE_REMAP), 60), "Remap");
    frame(0);
    snap("settings_remap.png");
    NonLinearCameraMove = 3; /* French: the gamepad column's names */
    frame(0);
    snap("settings_remap_fr.png");
    NonLinearCameraMove = 2;
    frame(0);
    press(0x4000);
    press(0x40);
    frame(0);
    snap("settings_remap_capture.png");
    /* R7c: the New Game screen, the cursor on Mirror mode's On, New Game+
       Off lit; then the cursor on New Game+ (its note), Mirror mode On lit */
    int ml = ui_NewGameScreenEnter();
    lt_switch_layout(ml);
    CHECK(settle(ml, 60), "the New Game screen");
    press(0x2000);
    frame(0);
    snap("settings_new_game_screen.png");
    press(0x4000);
    frame(0);
    snap("settings_new_game_screen_ngp.png");
    NonLinearCameraMove = 3; /* French: the longest note */
    frame(0);
    frame(0);
    snap("settings_new_game_screen_ngp_fr.png");
    NonLinearCameraMove = 2;
    frame(0);
    /* Q2: the quit confirmation, the cursor on Yes */
    int ql = ui_QuitScreenLayout();
    lt_switch_layout(ql);
    CHECK(settle(ql, 60), "the quit screen");
    press(0x8000);
    frame(0);
    snap("settings_quit_screen.png");
    /* the title (New Game only): Options (the sheets' word as text in the
       menus' look, v0.4.2) centred, Quit to desktop under it */
    lt_switch_layout(13);
    CHECK(settle(13, 60), "the title");
    {
        /* shown, as the title's proc (la_title) shows them once its menu
           is up */
        const int opt = ui_SettingsEntryRow(13), quit = ui_SettingsQuitRow(13);
        lt_ext_Prop(opt)->defaultMask = lt_ext_Prop(quit)->defaultMask = 0;
        ui_SettingsTitleMask(0);
        texLayout[13].curItem = opt;
        frame(0);
        snap("settings_title_entry.png");
        lt_ext_Prop(opt)->defaultMask = lt_ext_Prop(quit)->defaultMask = 1;
        ui_SettingsTitleMask(1);
    }
    /* T1: the Settings and Display screens at Enhanced 4x, full height, the
       atlas at a 960-line output's scale (settings_main_4x.png,
       settings_display_4x.png) */
    {
        RdSettings e = *rd_GetSettings();
        e.preset = RD_PRESET_ENHANCED;
        e.sceneScale = 4.0f;
        e.aspect = 4.0f / 3.0f;
        e.fullHeightScene = 1;
        e.outputWidth = 1280;
        e.outputHeight = 960;
        rd_SetSettings(&e);
        ui_SetScale(ui_ScaleFor(1, e.outputHeight));
        lt_switch_layout(mainL);
        CHECK(settle(mainL, 60), "the menu at 4x");
        lt_ext_Layout(mainL)->curItem = labels[6]; /* Language: its note */
        frame(0);
        frame(0);
        snap4("settings_main_4x.png");
        lt_ext_Layout(mainL)->curItem = labels[0];
        press(0x40);
        CHECK(settle(ui_SettingsPageLayout(UI_PAGE_DISPLAY), 60), "Display at 4x");
        frame(0);
        snap4("settings_display_4x.png");
    }
    /* the title entry (last: it makes Extras part of the main page): the
       main page with its Extras row, and the Extras page with the cursor on
       the locked Credits row, at the same 4x */
    {
        lt_switch_layout(13);
        CHECK(settle(13, 60), "the title");
        press(0x4000);
        press(0x40);
        CHECK(settle(mainL, 60), "the menu from the title");
        int ml[16];
        ui_SettingsPageRows(UI_PAGE_MAIN, ml, NULL, NULL, 16);
        lt_ext_Layout(mainL)->curItem = ml[7];
        frame(0);
        frame(0);
        snap4("settings_main_title_4x.png");
        lt_ext_Layout(mainL)->curItem = ml[8];
        press(0x40);
        int exL = ui_SettingsPageLayout(UI_PAGE_EXTRAS);
        CHECK(settle(exL, 60), "Extras at 4x");
        press(0x4000);
        press(0x4000);
        frame(0);
        snap4("settings_extras_4x.png");
        /* the music gallery over the fake engine, the cursor on an entry
           that plays */
        gallery_SetEngine(&kFakeEngine);
        lt_ext_Layout(exL)->curItem = ui_SettingsRowOf(UI_PAGE_EXTRAS, UI_OPT_EXTRAS_MUSIC);
        frame(0);
        press(0x40);
        int galL = ui_SettingsPageLayout(UI_PAGE_MUSIC);
        CHECK(settle(galL, 60), "the gallery at 4x");
        press(0x40);
        frame(0);
        snap4("settings_music_4x.png");
        press(0x10); /* Back leaves for Extras, the Music row under the cursor */
        CHECK(settle(exL, 60), "back to Extras from the gallery at 4x");
        gallery_SetEngine(NULL);
        /* v0.4.2 (K): Extras > Characters at the same 4x, the colours from
           a fixed seed, the cursor on Ico's tunic, its swatch beside the
           value */
        {
            int el[8], cl[16];
            ui_SettingsPageRows(UI_PAGE_EXTRAS, el, NULL, NULL, 8);
            ico_appearance_randomize(12345u);
            lt_ext_Layout(exL)->curItem = el[3];
            frame(0);
            press(0x40);
            const int chL = ui_SettingsPageLayout(UI_PAGE_CHARACTERS);
            CHECK(settle(chL, 60), "Characters at 4x");
            ui_SettingsPageRows(UI_PAGE_CHARACTERS, cl, NULL, NULL, 16);
            lt_ext_Layout(chL)->curItem = cl[5];
            for (int k = 0; k < 4; k++) {
                frame(0); /* the page refreshes */
            }
            snap4("settings_characters_4x.png");
            press(0x10);
            CHECK(settle(exL, 60), "back to Extras from Characters at 4x");
            /* package K-D: the same page from the title inside the model
               viewer, a panel at the left (the model, the game's, is not
               drawn here: the flat colour shows where it stands) */
            static const UiCharactersHost host = {renderCharsEnter, renderCharsShown,
                                                  renderCharsSwitch, renderCharsLeave};
            ui_SettingsSetCharactersHost(&host);
            lt_ext_Layout(exL)->curItem = el[3];
            frame(0);
            press(0x40);
            CHECK(settle(chL, 60), "Characters in the viewer at 4x");
            lt_ext_Layout(chL)->curItem = cl[5];
            for (int k = 0; k < 4; k++) {
                frame(0);
            }
            snap4("settings_characters_viewer_4x.png");
            ui_SettingsSetCharactersHost(NULL);
            ico_appearance_reset();
            lt_switch_layout(exL);
            CHECK(settle(exL, 60), "back to Extras from the viewer's Characters at 4x");
        }
    }
    /* every screen presented at Enhanced 1080p (snap1080) */
    {
        RdSettings e = *rd_GetSettings();
        e.preset = RD_PRESET_ENHANCED;
        e.sceneScale = 0.0f;
        e.sceneWidth = e.sceneHeight = 0;
        e.aspect = 16.0f / 9.0f;
        e.fullHeightScene = 0;
        e.outputWidth = 1920;
        e.outputHeight = 1080;
        rd_SetSettings(&e);
        ui_SetScale(ui_ScaleFor(1, e.outputHeight));
        s_reduce = 1;
        int ml[16];
        ui_SettingsPageRows(UI_PAGE_MAIN, ml, NULL, NULL, 16);
        lt_switch_layout(mainL);
        CHECK(settle(mainL, 60), "the menu at 1080p");
        lt_ext_Layout(mainL)->curItem = ml[6];
        frame(0);
        snap1080("settings_main");

        static const struct {
            int row, downs;
            const char *name;
        } pg[] = {{0, 0, "settings_display"},  {1, 0, "settings_effects"},
                  {3, 0, "settings_audio"},    {4, 1, "settings_controls"},
                  {5, 0, "settings_gameplay"}, {7, 0, "settings_achievements"},
                  {8, 2, "settings_extras"}};

        for (unsigned i = 0; i < sizeof(pg) / sizeof(pg[0]); i++) {
            lt_ext_Layout(mainL)->curItem = ml[pg[i].row];
            press(0x40);
            for (int k = 0; k < 30; k++) {
                frame(0);
            }
            for (int k = 0; k < pg[i].downs; k++) {
                press(0x4000);
            }
            snap1080(pg[i].name);
            press(0x10);
            CHECK(settle(mainL, 60), "back to the menu from %s at 1080p", pg[i].name);
        }
        int mir = ui_NewGameScreenEnter();
        lt_switch_layout(mir);
        CHECK(settle(mir, 60), "the New Game screen at 1080p");
        snap1080("settings_new_game_screen");
        press(0x4000);
        snap1080("settings_new_game_screen_ngp");
        /* P6: the pause menu with the journey's lines on its right (a
           stage running, an assist on, New Game+ on), then in French (the
           longest labels) */
        {
            ico_gs_reset();
            ico_gs_set_sampler(gsSampler);
            memset(&s_gs, 0, sizeof(s_gs));
            s_gs.valid = 1;
            s_gs.stage_no = 11;
            s_gs.system_status[0] = 1;
            s_gs.system_status[1] = 2;
            s_gs.layout = 57;
            s_gs.mc_preview[2] = (2 * 3600 + 12 * 60 + 34) * 50;
            ico_gs_tick();
            for (int i = 0; i < 3; i++) {
                ico_gs_signal(ICO_GS_EV_GAME_OVER, 0);
            }
            for (int i = 0; i < 41; i++) {
                ico_gs_signal(ICO_GS_EV_ENEMY_KILLED, i);
            }
            ico_gs_signal(ICO_GS_EV_YORDA_GRABBED, 0);
            ico_gs_tick();
            stage_no = 11;
            gFlagGameClear = 1;
            ico_opt_set_yorda_safe(1);
            lt_switch_layout(57);
            CHECK(settle(57, 60), "the pause menu at 1080p");
            snap1080("settings_pause_stats");
            NonLinearCameraMove = 3;
            frame(0);
            snap1080("settings_pause_stats_fr");
            NonLinearCameraMove = 2;
            ico_opt_set_yorda_safe(0);
            gFlagGameClear = 0;
            stage_no = 0;
            ico_gs_set_sampler(NULL);
            ico_gs_reset();
        }
        int ql = ui_QuitScreenLayout();
        lt_switch_layout(ql);
        CHECK(settle(ql, 60), "the quit screen at 1080p");
        press(0x8000);
        snap1080("settings_quit_screen");
        /* the music gallery with a stream playing (the fake engine: 42 s
           of 4:25), its bar and transport; then the model viewer's rows
           and prompts */
        {
            gallery_SetEngine(&kFakeEngine);
            lt_switch_layout(13);
            CHECK(settle(13, 60), "the title at 1080p");
            press(0x4000);
            press(0x40);
            CHECK(settle(mainL, 60), "the menu from the title at 1080p");
            lt_ext_Layout(mainL)->curItem = ml[8];
            press(0x40);
            int exL = ui_SettingsPageLayout(UI_PAGE_EXTRAS);
            CHECK(settle(exL, 60), "Extras at 1080p");
            lt_ext_Layout(exL)->curItem = ui_SettingsRowOf(UI_PAGE_EXTRAS, UI_OPT_EXTRAS_MUSIC);
            frame(0);
            press(0x40);
            int galL = ui_SettingsPageLayout(UI_PAGE_MUSIC);
            CHECK(settle(galL, 60), "the gallery at 1080p");
            press(0x40);
            snap1080("settings_music");
            press(0x10);
            CHECK(settle(exL, 60), "back to Extras at 1080p");
            gallery_SetEngine(NULL);
            int vl = viewerLayout();
            lt_switch_layout(vl);
            CHECK(settle(vl, 60), "the viewer's rows at 1080p");
            snap1080("settings_viewer");
        }
        /* package TXT: the save screen's slot numbers and play time */
        fakeSaveRows();
        lt_switch_layout(14);
        CHECK(settle(14, 60), "the save screen at 1080p");
        snap1080("settings_save_preview");
        testPhotoPanel();
        s_reduce = 0;
    }
    CHECK(gif_HostUndecodedTotal() == 0, "%u undecoded writes", gif_HostUndecodedTotal());
    ui__SetRecordHook(NULL);
    ui_FontShutdown();
    rd_Shutdown();
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    snprintf(s_dir, sizeof(s_dir), "%s", argc > 1 ? argv[1] : ".");
    setEnv("LC_ALL", NULL);
    setEnv("LANG", "en_GB.UTF-8");
    return render();
}

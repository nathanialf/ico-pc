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

#include "pad_glyphs.h"
#include "touch.h"
#include "touch_ui.h"

/* model_viewer.c's characters calls for the viewer's panel
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
/* a touch screen, for Controls' Touch screen row and its page */
static int renderTouch(void)
{
    return 1;
}

static void snap(const char *name)
{
    char p[1100];
    uint32_t w = 0, h = 0;
    uint8_t *px = malloc(512 * 512 * 4), *out = malloc(683 * 512 * 4);
    if (!px || !out || !rd__read_target(rd_target(RD_TARGET_SCENE), px, 512 * 512 * 4, &w, &h)) {
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
    rd_write_png(p, out, 683, 512, 683 * 4, 0);
    printf("settings_render: %s\n", p);
    free(px);
    free(out);
}

/* SCENE at the Enhanced 4x scale (2048 x 2048) to a PNG at 4:3,
   2731 x 2048: the port font's rows as a 4x output shows them */
static void snap4(const char *name)
{
    char p[1100];
    uint32_t w = 0, h = 0;
    const size_t n = (size_t)2048 * 2048 * 4;
    uint8_t *px = malloc(n), *out = malloc((size_t)2731 * 2048 * 4);
    if (!px || !out || !rd__read_target(rd_target(RD_TARGET_SCENE), px, n, &w, &h) || w != 2048 ||
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
    rd_write_png(p, out, 2731, 2048, 2731 * 4, 0);
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
    s_sheet[0] = rd_create_texture(64, 64, b, RD_TEXA_80_80, "settings_render buttons");
    s_sheet[1] = rd_create_texture(512, 256, m, RD_TEXA_80_80, "settings_render menu sheet");
}

/* seki/src/Texture.c (the photo panel's pictures, photo_ui.c): the fake
   tables' texture numbers 1 (buttons.tm2) and 2, 3 (menu_PAL_02, 01) are
   the stand-in sheets */
unsigned int tex_HostTextureId(int idx)
{
    return idx == 1 ? s_sheet[0].id : idx == 2 || idx == 3 ? s_sheet[1].id : 0u;
}

/* seki/src/Texture.c's slot table, for the touch overlay's sheet check
   (touch_ui.c sheetLoaded): no slot here, so the touch overlay draws the
   disc's pictures nowhere and the button pictures' art (a set in force)
   wherever a zone has a picture */
char *tex_GetTextureName(int idx)
{
    (void)idx;
    return NULL;
}

int tex_GetTextureNum(void)
{
    return 0;
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
    int first = LT_GAME_PROPERTY_COUNT + lt_ext_prop_count();
    int name = ui_settings_add_row(24, 10, 360, 30, 0, -1, 0, "Ico", 24.0f, UI_ALIGN_LEFT);
    ui_settings_add_row(24, 26, 360, 30, 0, -1, 0, "Animation: BOY STAND  \xC2\xB7  Loop", 19.0f,
                        UI_ALIGN_LEFT);
    ui_settings_add_row(24, 38, 360, 30, 0, -1, 0, "Frame 117 / 299", 19.0f, UI_ALIGN_LEFT);
    ui_hint_build(&sticks, 180, 19.0f, ui_hint_mv_sticks, UI_HINT_MV_STICKS_COUNT);
    ui_hint_build(&keys, 196, 19.0f, ui_hint_mv_keys, UI_HINT_MV_KEYS_COUNT);
    LtProp l;
    memset(&l, 0, sizeof(l));
    l.first = first;
    l.last = LT_GAME_PROPERTY_COUNT + lt_ext_prop_count();
    l.colA = 0.0f;
    l.procFirst = 1;
    l.defaultItem = l.curItem = -1;
    l.link = -1;
    (void)name;
    return lt_ext_add_layout(&l);
}

/* the RDC_OVERLAY_TEXT items of the last frame */
static int textItems(void)
{
    const RdFrame *f = rd__last_frame();
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
   Nothing is deferred: the port's rows and
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
    if (!px || !rd_read_presented(px, &ow, &oh) || ow != w || oh != h) {
        CHECK(0, "%s: the presented output", name);
        free(px);
        return;
    }
    snprintf(file, sizeof(file), "%s_1080.png", name);
    path(p, sizeof(p), file);
    rd_write_png(p, px, w, h, w * 4, 0);
    printf("settings_render: %s\n", p);
    free(px);
}

/* the save screen's values: layout 14's
   slot numbers (files 1, 2 and 5 used, the others empty) and layout 15's
   play time "12:34:56", at the PAL rows' places, their texel rectangles the
   menu text table's, everything else of the two layouts masked.  They are
   rows of the menu text table, so sheet strips in their
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

/* ------------------------------------------------ photo mode: the panel */

/* the last frame's draws on the menu text pages (menu_font.h) */
static void countPageDraw(void *user, int list, uint32_t index, const RdCmd *c,
                          const RdStateBlock *st)
{
    (void)list;
    (void)index;
    if (c->type == RDC_SCREEN && st->ds.texEnabled && ui_menu_font_is_page(st->tex)) {
        (*(int *)user)++;
    }
}

static int menuTextDraws(void)
{
    const RdFrame *f = rd__last_frame();
    int n = 0;
    if (f) {
        RdStateBlock s = f->startState;
        rd__walk(f, 0, &s, countPageDraw, &n);
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

/* a texture of the button pictures' atlases (pad_glyphs.c names them) */
static int isArtTex(uint32_t tex)
{
    const RdTexRec *t = tex ? rd__tex_rec(tex) : NULL;
    return t != NULL && strncmp(t->name, "ui pad glyphs", 13) == 0;
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
    ui_set_language(lang);
    ui__photo_hud_build(set, freeCam, keyboard, "Photo mode: Free camera, speed Normal",
                        "Field of view 60");
    float size = 17.0f;
    float w = ui__photo_hud_fit(set->item, set->n, &size, UI_PHOTO_HUD_ROOM, at);
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
    ui_photo_draw_overlay(ctx);
}

static void testPhotoPanel(void)
{
    static UiHudSet set;
    IcoBindings *b = ico_input_live_bindings();

    /* the PAL tables' glyph rows (fakeTables): the sheets' pictures */
    stage_no = 11;
    ico_photo_reset();
    lt_ext_reset();
    ui_settings_reset();
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
    ui_set_language(UI_LANG_EN);

    /* the pad: the game's pictures where its sheets have one, the names of
       the rest */
    ui__photo_hud_build(&set, 1, 0, "t", "f");
    CHECK(set.n == 14, "the free camera's panel: 14 items (%d)", set.n);

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
        {12, -1, -1, "Right", NULL},
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
    ui_set_language(UI_LANG_DE);
    ui__photo_hud_build(&set, 1, 0, "t", "f");
    CHECK(strcmp(iconWord(&set.item[1].icon[0]), "Linker Stick") == 0 &&
              strcmp(iconWord(&set.item[3].icon[1]), "Unten") == 0 &&
              strcmp(iconWord(&set.item[6].icon[0]), "L3") == 0,
          "German: \"%s\", \"%s\", \"%s\"", iconWord(&set.item[1].icon[0]),
          iconWord(&set.item[3].icon[1]), iconWord(&set.item[6].icon[0]));
    ui_set_language(UI_LANG_EN);
    /* tables that are not the PAL ones: a picture's name instead */
    texProperty[182].texU++;
    ui__photo_hud_build(&set, 1, 0, "t", "f");
    CHECK(set.item[9].icon[0].glyph == -1 && strcmp(iconWord(&set.item[9].icon[0]), "Cross") == 0,
          "Cross without its PAL rectangle: the word (%s)", iconWord(&set.item[9].icon[0]));
    /* a set of button pictures in force: the picture (the art) all the same */
    pad_glyphs_set_style(ICO_PAD_GLYPHS_XBOX);
    ui__photo_hud_build(&set, 1, 0, "t", "f");
    CHECK(set.item[9].icon[0].glyph == LT_GLYPH_CROSS,
          "Cross without its PAL rectangle, Xbox in force: the picture (%d)",
          set.item[9].icon[0].glyph);
    pad_glyphs_set_style(ICO_PAD_GLYPHS_PLAYSTATION);
    texProperty[182].texU--;

    /* the keys: W A S D for the stick, the first of "Tab, Backquote" */
    ui__photo_hud_build(&set, 1, 1, "t", "f");
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
    ui__photo_hud_build(&set, 1, 1, "t", "f");
    CHECK(strcmp(iconWord(&set.item[9].icon[0]), "[Mouse left]") == 0,
          "Cross without a key: the mouse (%s)", iconWord(&set.item[9].icon[0]));
    ico_bindings_clear(b, ICO_T_CROSS);
    ui__photo_hud_build(&set, 1, 1, "t", "f");
    CHECK(set.item[9].icon[0].glyph == LT_GLYPH_CROSS, "Cross unbound: the pad's picture");
    memset(b->kb[ICO_T_LSTICK_DOWN], 0, sizeof(b->kb[ICO_T_LSTICK_DOWN]));
    ui__photo_hud_build(&set, 1, 1, "t", "f");
    CHECK(set.item[1].nicon == 1 && strcmp(iconWord(&set.item[1].icon[0]), "Left stick") == 0,
          "a stick with a key missing: its name (%s)", iconWord(&set.item[1].icon[0]));
    ico_input_reload_bindings(b);

    /* with the photo layout current the game's pause rows are not drawn
       (the deferred-text check needs the 1080p setup this runs inside) */
    ico_photo_reset();
    lt_ext_reset();
    ui_settings_reset();
    fakeTables();
    memset(pad, 0, sizeof(pad));
    pad[0].ana[0] = pad[0].ana[1] = pad[0].ana[2] = pad[0].ana[3] = 128;
    init_layout_texture(2);
    settle(54, 4);
    lt_switch_layout(57);
    CHECK(settle(57, 60), "the pause menu");
    frame(0);
    /* nothing is deferred; the rows are menu text
       strips in the scene, counted as draws on the menu text pages */
    const int pauseItems = menuTextDraws();
    CHECK(pauseItems > 0, "the pause menu has menu text (%d draws)", pauseItems);
    const int ph = ui_settings_photo_row(), pl = ui_photo_layout();
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
    ui__set_overlay_sink(panelSink);
    /* the disc's pictures' boxes on the pad (output pixels: x0, y0, x1, y1):
       a sheet's quad is its picture's box exactly (photo_ui.c hudGlyph) */
    float psBox[7][4];
    int psN = 0;
    for (int keys = 0; keys < 2; keys++) {
        if (keys) {
            ico_input_note_press(ICO_SRC_KEY, 4);
        }
        s_pn = 0;
        CHECK(ico_photo_hud(), "the panel is shown");
        ui_photo_draw_overlay(&ctx);
        CHECK(s_pn > 8, "%s: %u vertices drawn", keys ? "keys" : "pad", s_pn);
        /* the first sprite is the panel's rectangle; all the rest lies on it */
        const float x0 = (float)s_pv[0].x / 16.0f - 1.0f, y0 = (float)s_pv[0].y / 16.0f - 1.0f;
        const float x1 = (float)s_pv[1].x / 16.0f + 1.0f, y1 = (float)s_pv[1].y / 16.0f + 1.0f;
        const float left = (1920.0f - 1440.0f) * 0.5f, k = 1440.0f / 640.0f;
        CHECK(x0 >= left + (22.0f - 8.0f) * k - 2.0f && x1 <= left + (618.0f + 8.0f) * k + 2.0f &&
                  y1 <= 1080.0f,
              "%s: the panel is x %.0f .. %.0f, y %.0f .. %.0f", keys ? "keys" : "pad", (double)x0,
              (double)x1, (double)y0, (double)y1);
        /* the words are menu text strips, whose quads
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
            if (!keys && sheet && i + 1 < s_pn && s_ptex[i + 1] == s_ptex[i] && psN < 7) {
                psBox[psN][0] = x;
                psBox[psN][1] = y;
                psBox[psN][2] = (float)s_pv[i + 1].x / 16.0f;
                psBox[psN][3] = (float)s_pv[i + 1].y / 16.0f;
                psN++;
                pictures++;
                i++;
                const float x2 = (float)s_pv[i].x / 16.0f, y2 = (float)s_pv[i].y / 16.0f;
                out += x2 < x0 || x2 > x1 || y2 < y0 || y2 > y1;
            }
        }
        CHECK(out == 0, "%s: %d vertices outside the panel", keys ? "keys" : "pad", out);
        CHECK(pictures == (keys ? 0 : 2 * 7), "%s: %d picture vertices (%d)", keys ? "keys" : "pad",
              pictures, keys ? 0 : 2 * 7);
    }
    /* the pad with Xbox: the seven pictures from the button pictures'
       atlas (none from the sheets), on the panel, each the art's aspect
       (on the output's square pixels), each centred on one of the disc
       pictures' boxes within half an output pixel, inside it (the panel's
       gaps kept); the faces one height and the shoulders one height (the
       group's one scale: every Xbox piece is a capital, so one scale is
       one height), and in each group some picture filling its box's width
       or height (as large as fits) */
    ico_input_note_press(ICO_SRC_PAD, 0);
    pad_glyphs_set_style(ICO_PAD_GLYPHS_XBOX);
    s_pn = 0;
    ui_photo_draw_overlay(&ctx);
    {
        int sheets = 0, art = 0, shape = 0, placed = 0, used[7] = {0};
        /* per group (by the box's width: the faces' boxes are narrower):
           the box width, the least and most height, a picture filling */
        float grpW[2] = {0, 0}, grpLo[2] = {1e9f, 1e9f}, grpHi[2] = {0, 0};
        int grpFill[2] = {0, 0}, grpN[2] = {0, 0};
        CHECK(psN == 7, "the pad's seven disc pictures' boxes (%d)", psN);
        for (uint32_t i = 2; i + 1 < s_pn; i++) {
            sheets += isSheet(s_ptex[i]);
            if (isArtTex(s_ptex[i]) && s_ptex[i + 1] == s_ptex[i]) {
                const float dw = (float)(s_pv[i + 1].x - s_pv[i].x);
                const float dh = (float)(s_pv[i + 1].y - s_pv[i].y);
                const float du = s_pv[i + 1].s - s_pv[i].s, dv = s_pv[i + 1].t - s_pv[i].t;
                /* the quad's shape on screen is the cell's */
                shape += dh > 0.0f && dv > 0.0f && fabsf(dw / dh - du / dv) <= 0.06f * (du / dv);
                const float cx = (float)(s_pv[i].x + s_pv[i + 1].x) / 32.0f;
                const float cy = (float)(s_pv[i].y + s_pv[i + 1].y) / 32.0f;
                for (int k = 0; k < psN; k++) {
                    const float *bx = psBox[k];
                    const float bw = bx[2] - bx[0], bh = bx[3] - bx[1];
                    if (used[k] || fabsf(cx - (bx[0] + bx[2]) * 0.5f) > 0.5f ||
                        fabsf(cy - (bx[1] + bx[3]) * 0.5f) > 0.5f) {
                        continue;
                    }
                    used[k] = 1;
                    const int inside = dw / 16.0f <= bw + 0.5f && dh / 16.0f <= bh + 0.5f;
                    CHECK(inside,
                          "pad with Xbox: the picture %.1f x %.1f inside the box %.1f x %.1f",
                          (double)(dw / 16.0f), (double)(dh / 16.0f), (double)bw, (double)bh);
                    int gi = grpN[0] && fabsf(bw - grpW[0]) > 0.5f ? 1 : 0;
                    grpW[gi] = bw;
                    grpN[gi]++;
                    grpLo[gi] = fminf(grpLo[gi], dh / 16.0f);
                    grpHi[gi] = fmaxf(grpHi[gi], dh / 16.0f);
                    grpFill[gi] |= fabsf(dh / 16.0f - bh) <= 1.0f || fabsf(dw / 16.0f - bw) <= 1.0f;
                    placed++;
                    break;
                }
                art += 2;
                i++;
            }
        }
        for (int gi = 0; gi < 2; gi++) {
            CHECK(grpN[gi] > 0 && grpHi[gi] - grpLo[gi] <= 1.0f && grpFill[gi],
                  "pad with Xbox: the %d pictures in %.1f-wide boxes %.1f .. %.1f tall, one "
                  "filling its box %d",
                  grpN[gi], (double)grpW[gi], (double)grpLo[gi], (double)grpHi[gi], grpFill[gi]);
        }
        CHECK(sheets == 0 && art == 2 * 7 && shape == 7 && placed == 7,
              "pad with Xbox: %d sheet vertices (0), %d art vertices (%d), %d of the art's shape, "
              "%d centred on a disc picture's box",
              sheets, art, 2 * 7, shape, placed);
    }
    pad_glyphs_set_style(ICO_PAD_GLYPHS_PLAYSTATION);
    ui__set_overlay_sink(NULL);

    /* the snapshots, the panel on the output as the window draws it (the
       sheets are the stand-ins makeSheets draws) */
    void *prevUser = NULL;
    RdOverlayFn prev = rd_get_present_overlay(&prevUser);
    rd_set_present_overlay(photoOverlay, NULL);
    ico_input_note_press(ICO_SRC_PAD, 0);
    snap1080("settings_photo");
    ico_input_note_press(ICO_SRC_KEY, 4);
    snap1080("settings_photo_keys");
    rd_set_present_overlay(prev, prevUser);
    ico_photo_reset();
    stage_no = 0;
}

/* ------------------------------------------------ the button pictures */

typedef struct ArtQuad {
    RdScreenVtx v[2];
    uint32_t tex;
} ArtQuad;

static ArtQuad s_artQ[32];
static int s_artN;

static void collectArt(void *user, int list, uint32_t index, const RdCmd *c, const RdStateBlock *st)
{
    (void)user;
    (void)list;
    (void)index;
    if (c->type != RDC_SCREEN || !st->ds.texEnabled || !isArtTex(st->tex) || c->u[1] != 2 ||
        s_artN >= 32) {
        return;
    }
    const RdFrame *f = rd__last_frame();
    memcpy(s_artQ[s_artN].v, f->payload + c->u[0], sizeof(s_artQ[0].v));
    s_artQ[s_artN].tex = st->tex;
    s_artN++;
}

/* the last frame's sprites from the atlases */
static int artQuads(void)
{
    const RdFrame *f = rd__last_frame();
    s_artN = 0;
    if (f) {
        RdStateBlock st = f->startState;
        rd__walk(f, 0, &st, collectArt, NULL);
    }
    return s_artN;
}

/* GS 12.4 (the 512 x 512 frame) to the grid and to SCENE pixels at 1x */
static float artGridX(int32_t x16)
{
    return UI_GRID_CX + ((float)x16 - 32768.0f) * 640.0f / (16.0f * 512.0f);
}

static float artGridY(int32_t y16)
{
    return UI_GRID_CY + ((float)y16 - 32768.0f) * 224.0f / (8.0f * 512.0f);
}

static float artPix(int32_t v16)
{
    return (float)v16 / 16.0f - 1792.0f;
}

/* The model viewer's prompt lines (ui_hint.c: seven glyph rows among the
   words) at 1x over the fixture's PAL tables: with PlayStation the glyph
   rows draw the stand-in sheets; with Xbox, Nintendo and Wii Classic each
   shown glyph row is one sprite from an atlas inside its disc box, the
   pixels there are not the stand-in sheet's, and where the art's texel and
   its neighbours are one opaque colour the SCENE pixel is that texel
   MODULATEd by the sprite's colour (0x7F: ~reductionCol) over the
   background at alpha 127 of 128.  settings_pad_glyphs_<set>.png. */
static void testArtPixels(void)
{
    const size_t n = (size_t)512 * 512 * 4;
    uint8_t *ps = malloc(n), *px = malloc(n);
    uint32_t w = 0, h = 0;
    if (!ps || !px) {
        CHECK(0, "the button pictures' buffers");
        free(ps);
        free(px);
        return;
    }
    pad_glyphs_set_style(ICO_PAD_GLYPHS_PLAYSTATION);
    const int first = LT_GAME_PROPERTY_COUNT + lt_ext_prop_count();
    const int vl = viewerLayout();
    const int last = LT_GAME_PROPERTY_COUNT + lt_ext_prop_count();
    lt_switch_layout(vl);
    CHECK(settle(vl, 60), "the viewer's prompts at 1x");
    frame(0);
    int glyphRows = 0;
    for (int i = first; i < last; i++) {
        glyphRows += lt_ext_is_glyph_row(lt_ext_prop(i)) && !lt_ext_prop(i)->defaultMask;
    }
    CHECK(glyphRows == 7, "the viewer's prompt lines show 7 button glyphs (%d)", glyphRows);
    CHECK(artQuads() == 0, "PlayStation: no sprite from an atlas (%d)", s_artN);
    snap("settings_pad_glyphs_playstation.png");
    if (!rd__read_target(rd_target(RD_TARGET_SCENE), ps, n, &w, &h) || w != 512 || h != 512) {
        CHECK(0, "SCENE readback (PlayStation)");
    }
    static const int kStyles[] = {ICO_PAD_GLYPHS_XBOX, ICO_PAD_GLYPHS_NINTENDO,
                                  ICO_PAD_GLYPHS_WII_CLASSIC};
    static const char *const kPng[] = {"settings_pad_glyphs_xbox.png",
                                       "settings_pad_glyphs_nintendo.png",
                                       "settings_pad_glyphs_wii_classic.png"};
    int flatAll = 0;
    for (size_t si = 0; si < sizeof(kStyles) / sizeof(kStyles[0]); si++) {
        const char *name = pad_glyphs_style_name(kStyles[si]);
        pad_glyphs_set_style(kStyles[si]);
        CHECK(pad_glyphs_active(), "%s in force", name);
        frame(0);
        frame(0);
        const int quads = artQuads();
        CHECK(quads == glyphRows, "%s: %d sprites from an atlas (%d)", name, quads, glyphRows);
        snap(kPng[si]);
        if (!rd__read_target(rd_target(RD_TARGET_SCENE), px, n, &w, &h) || w != 512 || h != 512) {
            CHECK(0, "SCENE readback (%s)", name);
            continue;
        }
        int checked = 0, worst = 0;
        for (int q = 0; q < quads; q++) {
            const RdScreenVtx *v = s_artQ[q].v;
            const RdTexRec *tr = rd__tex_rec(s_artQ[q].tex);
            /* inside a shown glyph row's disc box (display_texture's:
               dispX + 1/4, dispW - 1 wide; 2 dispY + 1/2, dispH - 2 high) */
            const float gx0 = artGridX(v[0].x), gx1 = artGridX(v[1].x);
            const float gy0 = artGridY(v[0].y), gy1 = artGridY(v[1].y);
            int inBox = 0, centred = 0;
            for (int i = first; i < last; i++) {
                const LtProperty *e = lt_ext_prop(i);
                if (!lt_ext_is_glyph_row(e) || e->defaultMask) {
                    continue;
                }
                const float bx = (float)e->dispX + 0.25f, by = 2.0f * (float)e->dispY + 0.5f;
                const float bw = (float)(e->dispW - 1), bh = (float)(e->dispH - 2);
                const int in = gx0 >= bx - 0.1f && gx1 <= bx + bw + 0.1f && gy0 >= by - 0.1f &&
                               gy1 <= by + bh + 0.1f;
                inBox += in;
                /* the ink centred on the box: a tenth of a unit, under
                   half a SCENE pixel (1.25 x units, 0.875 y units a pixel) */
                centred += in && fabsf((gx0 + gx1) * 0.5f - (bx + bw * 0.5f)) < 0.1f &&
                           fabsf((gy0 + gy1) * 0.5f - (by + bh * 0.5f)) < 0.1f;
            }
            CHECK(inBox == 1 && centred == 1,
                  "%s: sprite %d (%.1f,%.1f-%.1f,%.1f) inside one glyph row's box, centred on it",
                  name, q, (double)gx0, (double)gy0, (double)gx1, (double)gy1);
            const float X0 = artPix(v[0].x), X1 = artPix(v[1].x);
            const float Y0 = artPix(v[0].y), Y1 = artPix(v[1].y);
            const float u0 = v[0].s / 16.0f, t0 = v[0].t / 16.0f;
            const float u1 = v[1].s / 16.0f, t1 = v[1].t / 16.0f;
            int area = 0, differ = 0;
            for (int y = (int)ceilf(Y0); y + 1 <= (int)floorf(Y1) && y < 512; y++) {
                for (int x = (int)ceilf(X0); x + 1 <= (int)floorf(X1) && x < 512; x++) {
                    if (x < 0 || y < 0) {
                        continue;
                    }
                    const uint8_t *a = &ps[((size_t)y * 512 + (size_t)x) * 4];
                    const uint8_t *b = &px[((size_t)y * 512 + (size_t)x) * 4];
                    area++;
                    differ += memcmp(a, b, 3) != 0;
                }
            }
            /* the art's colour arrives: the cell's brightest flat opaque
               texel (3 x 3 of one colour: the letter's fill) is some pixel
               of the sprite, MODULATE (t * 0x7F) >> 7 blended at 127 / 128
               (at most 2 off).  One pixel a sprite, not every flat one: the
               sprite is drawn smaller than its cell (the atlas's size
               bucket), so a pixel beside an edge mixes texels the 3 x 3
               does not hold (merge check 1: 46 such pixels, up to 45 off) */
            if (tr && tr->pixels && tr->format == RD_TEXEL_RGBA8) {
                const uint8_t *fill = NULL;
                int fillSum = -1;
                for (int ty = (int)floorf(t0) + 1; ty + 1 < (int)ceilf(t1) && ty + 1 < (int)tr->h;
                     ty++) {
                    for (int tx = (int)floorf(u0) + 1;
                         tx + 1 < (int)ceilf(u1) && tx + 1 < (int)tr->w; tx++) {
                        const uint8_t *c = &tr->pixels[((size_t)ty * tr->w + (size_t)tx) * 4];
                        int flat = tx >= 1 && ty >= 1 && c[3] == 0x80;
                        for (int dy = -1; flat && dy <= 1; dy++) {
                            for (int dx = -1; flat && dx <= 1; dx++) {
                                flat = memcmp(c,
                                              &tr->pixels[((size_t)(ty + dy) * tr->w +
                                                           (size_t)(tx + dx)) *
                                                          4],
                                              4) == 0;
                            }
                        }
                        if (flat && c[0] + c[1] + c[2] > fillSum) {
                            fillSum = c[0] + c[1] + c[2];
                            fill = c;
                        }
                    }
                }
                if (fill && fillSum > 0) {
                    int best = 255;
                    for (int y = (int)ceilf(Y0); y + 1 <= (int)floorf(Y1) && y < 512; y++) {
                        for (int x = (int)ceilf(X0); x + 1 <= (int)floorf(X1) && x < 512; x++) {
                            if (x < 0 || y < 0) {
                                continue;
                            }
                            const uint8_t *b = &px[((size_t)y * 512 + (size_t)x) * 4];
                            int d = 0;
                            for (int ch = 0; ch < 3; ch++) {
                                const int e = abs((int)b[ch] - ((fill[ch] * 0x7F) >> 7));
                                d = e > d ? e : d;
                            }
                            best = d < best ? d : best;
                        }
                    }
                    worst = best > worst ? best : worst;
                    checked++;
                }
            }
            CHECK(area > 0 && differ * 8 >= area,
                  "%s: sprite %d: %d of %d pixels differ from the stand-in sheet's", name, q,
                  differ, area);
        }
        CHECK(worst <= 4,
              "%s: %d sprites show their fill colour, the worst %d off the texel times the colour",
              name, checked, worst);
        flatAll += checked;
        printf("settings_render: %s: %d art sprites, %d with their fill colour checked, worst %d\n",
               name, quads, checked, worst);
    }
    CHECK(flatAll >= 15, "the three sets: %d sprites' fill colours checked", flatAll);
    pad_glyphs_set_style(ICO_PAD_GLYPHS_PLAYSTATION);
    frame(0);
    free(ps);
    free(px);
}

/* the touch overlay (touch_ui.c) on a 1920 x 1080 output: the default
   medium layout, fully shown */
static float s_touchK = 1.0f; /* every button scaled by this about its centre */

/* the picture a touch zone shows (touch_ui.c glyphOf) */
static int glyphOfZone(int z)
{
    switch (z) {
    case ICO_TOUCH_B_CROSS:
        return LT_GLYPH_CROSS;
    case ICO_TOUCH_B_CIRCLE:
        return LT_GLYPH_CIRCLE;
    case ICO_TOUCH_B_SQUARE:
        return LT_GLYPH_SQUARE;
    case ICO_TOUCH_B_TRIANGLE:
        return LT_GLYPH_TRIANGLE;
    case ICO_TOUCH_B_L1:
        return LT_GLYPH_L1;
    case ICO_TOUCH_B_R1:
        return LT_GLYPH_R1;
    case ICO_TOUCH_B_L2:
        return LT_GLYPH_L2;
    default:
        return LT_GLYPH_R2;
    }
}

static int touchSource(IcoTouchOverlay *out)
{
    IcoTouchInsets none = {0.0f, 0.0f, 0.0f, 0.0f};
    memset(out, 0, sizeof(*out));
    out->layout = ico_touch_layout(1920, 1080, none, ICO_TOUCH_MEDIUM);
    out->opacity = 1.0f;
    for (int z = 0; z < ICO_TOUCH_BUTTONS; z++) {
        IcoTouchRect *r = &out->layout.button[z].rect;
        r->x += r->w * (1.0f - s_touchK) * 0.5f;
        r->y += r->h * (1.0f - s_touchK) * 0.5f;
        r->w *= s_touchK;
        r->h *= s_touchK;
    }
    return 1;
}

static void touchOverlay(const RdOverlayCtx *ctx, void *user)
{
    (void)user;
    ui_touch_draw_overlay(ctx);
}

/* With PlayStation and no disc sheet in a slot the touch overlay draws no
   picture (the words); with Xbox the eight zones that have a picture take
   the art (ui__touch_art_zones), and the presented pixels on Cross differ.
   settings_touch_playstation_1080.png, settings_touch_xbox_1080.png. */
static void testTouchArt(void)
{
    const uint32_t W = 1920, H = 1080;
    uint8_t *a = malloc((size_t)W * H * 4), *b = malloc((size_t)W * H * 4);
    uint32_t ow = 0, oh = 0;
    void *prevUser = NULL;
    RdOverlayFn prev = rd_get_present_overlay(&prevUser);
    if (!a || !b) {
        CHECK(0, "the touch overlay's buffers");
        free(a);
        free(b);
        return;
    }
    ui_touch_set_source(touchSource);
    rd_set_present_overlay(touchOverlay, NULL);
    pad_glyphs_set_style(ICO_PAD_GLYPHS_PLAYSTATION);
    snap1080("settings_touch_playstation");
    CHECK(ui__touch_art_zones() == 0, "PlayStation: no zone with the art (%#x)",
          ui__touch_art_zones());
    int okA = rd_read_presented(a, &ow, &oh) && ow == W && oh == H;
    pad_glyphs_set_style(ICO_PAD_GLYPHS_XBOX);
    snap1080("settings_touch_xbox");
    IcoTouchOverlay o;
    touchSource(&o);
    unsigned want = 0;
    static const int kZones[] = {ICO_TOUCH_B_CROSS,    ICO_TOUCH_B_CIRCLE, ICO_TOUCH_B_SQUARE,
                                 ICO_TOUCH_B_TRIANGLE, ICO_TOUCH_B_L1,     ICO_TOUCH_B_R1,
                                 ICO_TOUCH_B_L2,       ICO_TOUCH_B_R2};
    for (size_t i = 0; i < sizeof(kZones) / sizeof(kZones[0]); i++) {
        const IcoTouchButton *bt = &o.layout.button[kZones[i]];
        want |= bt->rect.w > 0.0f && bt->rect.h > 0.0f && bt->alpha > 0.0f ? 1u << kZones[i] : 0u;
    }
    CHECK(want != 0 && ui__touch_art_zones() == want,
          "Xbox: the zones with a picture take the art (%#x, %#x)", ui__touch_art_zones(), want);
    /* each picture wholly inside its button and centred on it within half
       an output pixel (the layout is the output's size: layout pixels are
       output pixels): a round button's corners inside the circle of
       UI_TOUCH_ART_ROUND of its radius (and a pixel inside the ring, th
       the unit's 0.4, at least 2), a key's inside UI_TOUCH_ART_KEY_W x
       UI_TOUCH_ART_KEY_H of it (and inside its frame); as large as fits
       for its group: the four faces one height and the three shoulder
       keys one height (every Xbox piece a capital), and each group's
       tightest picture at its limit within a pixel.  Then every button at
       0.5 and 2 times its size: every picture 0.5 and 2 times as large,
       within a pixel.  For each of the three sets (the group's scale read
       as the height over the piece's own, pad_glyphs_height, so the Wii
       Classic's short letters count too). */
    float th = o.layout.unit * 0.4f;
    th = th < 2.0f ? 2.0f : th;
    float size1[ICO_TOUCH_BUTTONS][2];
    static const int kSets[] = {ICO_PAD_GLYPHS_XBOX, ICO_PAD_GLYPHS_NINTENDO,
                                ICO_PAD_GLYPHS_WII_CLASSIC};
    for (int pass = 0; pass < 9; pass++) {
        static const float kK[3] = {1.0f, 0.5f, 2.0f};
        const int style = kSets[pass / 3];
        s_touchK = kK[pass % 3];
        pad_glyphs_set_style(style);
        if (pass > 0) {
            frame(0);
        }
        touchSource(&o);
        float lo[2] = {1e9f, 1e9f}, hi[2] = {0, 0};
        int atLimit[2] = {0, 0};
        for (size_t i = 0; i < sizeof(kZones) / sizeof(kZones[0]); i++) {
            const int z = kZones[i];
            const IcoTouchButton *bt = &o.layout.button[z];
            float q[4];
            if (!((want >> z) & 1u)) {
                continue;
            }
            if (!ui__touch_art_quad(z, q)) {
                CHECK(0, "%s at %.1fx: zone %d's art quad", pad_glyphs_style_name(style),
                      (double)s_touchK, z);
                continue;
            }
            const float cx = bt->rect.x + bt->rect.w * 0.5f, cy = bt->rect.y + bt->rect.h * 0.5f;
            const float w = q[2] - q[0], h = q[3] - q[1];
            int inside, limit;
            if (bt->round) {
                const float r = bt->rect.w * 0.5f;
                const float R = fminf(UI_TOUCH_ART_ROUND * r, r - th - 1.0f);
                const float half = 0.5f * hypotf(w, h);
                inside = half <= R + 0.5f;
                limit = fabsf(half - R) <= 1.0f;
            } else {
                const float bw =
                    fminf(UI_TOUCH_ART_KEY_W * bt->rect.w, bt->rect.w - 2.0f * th - 2.0f);
                const float bh =
                    fminf(UI_TOUCH_ART_KEY_H * bt->rect.h, bt->rect.h - 2.0f * th - 2.0f);
                inside = w <= bw + 0.5f && h <= bh + 0.5f;
                limit = fabsf(w - bw) <= 1.0f || fabsf(h - bh) <= 1.0f;
            }
            CHECK(fabsf((q[0] + q[2]) * 0.5f - cx) <= 0.5f &&
                      fabsf((q[1] + q[3]) * 0.5f - cy) <= 0.5f && inside,
                  "%s at %.1fx: zone %d's art %.1f,%.1f-%.1f,%.1f inside and centred on the %s "
                  "%.1f,%.1f %.1fx%.1f",
                  pad_glyphs_style_name(style), (double)s_touchK, z, (double)q[0], (double)q[1],
                  (double)q[2], (double)q[3], bt->round ? "circle" : "key", (double)bt->rect.x,
                  (double)bt->rect.y, (double)bt->rect.w, (double)bt->rect.h);
            /* the groups: the four round faces, the three shoulder keys
               (R1, a round shoulder, is a button of its own size) */
            const int gi = z == ICO_TOUCH_B_R1 ? -1
                                               : (z == ICO_TOUCH_B_L1 || z == ICO_TOUCH_B_L2 ||
                                                  z == ICO_TOUCH_B_R2);
            /* the group's one scale: the height over the piece's own */
            const float L = h / pad_glyphs_height(style, glyphOfZone(z));
            if (gi >= 0) {
                lo[gi] = fminf(lo[gi], L);
                hi[gi] = fmaxf(hi[gi], L);
                atLimit[gi] |= limit;
            }
            if (pass % 3 == 0) {
                size1[z][0] = w;
                size1[z][1] = h;
            } else {
                CHECK(fabsf(w - s_touchK * size1[z][0]) <= 1.0f &&
                          fabsf(h - s_touchK * size1[z][1]) <= 1.0f,
                      "%s: zone %d's art %.1f x %.1f at %.1fx (%.1f x %.1f at 1x)",
                      pad_glyphs_style_name(style), z, (double)w, (double)h, (double)s_touchK,
                      (double)size1[z][0], (double)size1[z][1]);
            }
        }
        for (int gi = 0; gi < 2; gi++) {
            CHECK(hi[gi] > 0.0f && hi[gi] - lo[gi] <= 1.0f && atLimit[gi],
                  "%s at %.1fx: the %s' scale %.1f .. %.1f, one at its button's limit %d",
                  pad_glyphs_style_name(style), (double)s_touchK,
                  gi ? "shoulder keys' pictures" : "faces' pictures", (double)lo[gi],
                  (double)hi[gi], atLimit[gi]);
        }
    }
    /* back at 1x with Xbox, presented again for the pixel check below */
    s_touchK = 1.0f;
    pad_glyphs_set_style(ICO_PAD_GLYPHS_XBOX);
    touchSource(&o);
    frame(0);
    int okB = rd_read_presented(b, &ow, &oh) && ow == W && oh == H;
    CHECK(okA && okB, "the touch overlay's presented outputs");
    if (okA && okB) {
        const IcoTouchRect *r = &o.layout.button[ICO_TOUCH_B_CROSS].rect;
        int differ = 0;
        for (int y = (int)(r->y + r->h * 0.3f); y < (int)(r->y + r->h * 0.7f); y++) {
            for (int x = (int)(r->x + r->w * 0.3f); x < (int)(r->x + r->w * 0.7f); x++) {
                if (x >= 0 && y >= 0 && x < (int)W && y < (int)H) {
                    const size_t k = ((size_t)y * W + (size_t)x) * 4;
                    differ += memcmp(&a[k], &b[k], 3) != 0;
                }
            }
        }
        CHECK(differ > 0, "Xbox: Cross's middle differs from PlayStation's (%d pixels)", differ);
    }
    pad_glyphs_set_style(ICO_PAD_GLYPHS_PLAYSTATION);
    rd_set_present_overlay(prev, prevUser);
    ui_touch_set_source(NULL);
    free(a);
    free(b);
}

static int render(void)
{
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ORIGINAL;
    if (!rd_init(512, 512, &st, NULL)) {
        printf("settings_render: SKIP: no usable Vulkan device\n");
        return 77;
    }
    ui_font_forget_textures();
    UiGsFrame fr = {512, 512, 2048.0f, 2048.0f, UI_LAYOUT_Z};
    ui_set_gs_frame(&fr);
    ui_set_scale(1.0f);
    ui__set_record_hook(gif_HostFlush);
    makeSheets();
    gif_HostSetTex0Resolver(sheetResolve);
    dl_Init();
    GlobalStageSetting.reductionCol[0] = GlobalStageSetting.reductionCol[1] =
        GlobalStageSetting.reductionCol[2] = 0x80;

    useConfig("version = 1\n");
    fakeTables();
    lt_ext_reset();
    ui_settings_reset();
    memset(pad, 0, sizeof(pad));
    pad[0].ana[0] = pad[0].ana[1] = pad[0].ana[2] = pad[0].ana[3] = 128;
    NonLinearCameraMove = 2;
    init_layout_texture(2);
    settle(54, 4);
    int mainL = ui_settings_page_layout(UI_PAGE_MAIN);
    lt_switch_layout(mainL);
    CHECK(settle(mainL, 60), "the menu");
    for (int i = 0; i < 4; i++) {
        press(0x4000); /* to Language: its note */
    }
    frame(0);
    snap("settings_main.png");
    /* the window scrolled: nine Downs from the first row (on a disc whose
       Main page has more rows than the window's slots, the rows above out
       of view and the up mark shown) */
    {
        int labels[24], off = -1, shown = 0;
        ui_settings_page_rows(UI_PAGE_MAIN, labels, NULL, NULL, 24);
        lt_ext_layout(mainL)->curItem = labels[0];
        frame(0);
        for (int i = 0; i < 9; i++) {
            press(LT_PAD_DOWN);
        }
        frame(0);
        const int win = ui_settings_page_scroll(UI_PAGE_MAIN, &shown, &off, NULL);
        CHECK(win == (shown > WIN_SLOTS) && (win == 0 || off > 0),
              "Main after nine Downs: the window %s (%d rows, offset %d)",
              win ? "scrolled" : "not needed", shown, off);
        checkPageFits(UI_PAGE_MAIN, "Main scrolled (render)");
        snap("settings_main_scrolled.png");
    }

    /* each page by the main page's row that opens it (mainLink: the rows
       move when one is added, as the Graphics link after Effects did) */
    static const struct {
        UiSettingsPage page;
        int downs;
        const char *name;
    } pages[] = {{UI_PAGE_DISPLAY, 0, "settings_display.png"},
                 {UI_PAGE_DISPLAY, 10, "settings_display_videomode.png"},
                 {UI_PAGE_PICTURE, 0, "settings_picture.png"},
                 {UI_PAGE_EFFECTS, 0, "settings_effects.png"},
                 {UI_PAGE_AUDIO, 0, "settings_audio.png"},
                 {UI_PAGE_CONTROLS, 1, "settings_controls.png"},
                 {UI_PAGE_GAMEPLAY, 0, "settings_gameplay.png"},
                 {UI_PAGE_ACHIEVEMENTS, 0, "settings_achievements.png"},
                 {UI_PAGE_DEVELOPER, 0, "settings_developer.png"}};

    for (unsigned i = 0; i < sizeof(pages) / sizeof(pages[0]); i++) {
        lt_ext_layout(mainL)->curItem = mainLink(pages[i].page);
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
    lt_ext_layout(mainL)->curItem = mainLink(UI_PAGE_AUDIO);
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
    /* Controls -> Touch screen (a touch screen there), the cursor on Edit
       layout (its note: no editor here, so the locked one) */
    {
        ui_settings_set_touch_query(renderTouch);
        lt_ext_layout(mainL)->curItem = mainLink(UI_PAGE_CONTROLS);
        press(0x40);
        const int ctlL = ui_settings_page_layout(UI_PAGE_CONTROLS);
        CHECK(settle(ctlL, 60), "Controls (touch)");
        int cl[24];
        const int cn = ui_settings_page_rows(UI_PAGE_CONTROLS, cl, NULL, NULL, 24);
        for (int i = 0; i < cn; i++) {
            if (lt_ext_prop(cl[i])->right == ui_settings_page_layout(UI_PAGE_TOUCH)) {
                lt_ext_layout(ctlL)->curItem = cl[i];
            }
        }
        frame(0);
        snap("settings_controls_touch.png");
        press(0x40);
        CHECK(settle(ui_settings_page_layout(UI_PAGE_TOUCH), 60), "Touch screen");
        frame(0);
        snap("settings_touch.png");
        for (int k = 0; k < 3; k++) {
            press(0x4000); /* to Edit layout */
        }
        frame(0);
        snap("settings_touch_edit.png");
        press(0x10);
        CHECK(settle(ctlL, 60), "back to Controls from Touch screen");
        press(0x10);
        CHECK(settle(mainL, 60), "back to the menu from Controls (touch)");
        ui_settings_set_touch_query(NULL);
    }
    /* Gameplay -> Chains and Cutscenes (its two links before Back), each
       with the cursor on its first row */
    {
        static const struct {
            UiSettingsPage page;
            const char *name;
        } sub[] = {{UI_PAGE_CHAINS, "settings_gameplay_chains.png"},
                   {UI_PAGE_CUTSCENES, "settings_gameplay_cutscenes.png"}};

        for (unsigned i = 0; i < sizeof(sub) / sizeof(sub[0]); i++) {
            int gameL = -1;
            openGameplayPage(mainL, sub[i].page, &gameL);
            for (int k = 0; k < 30; k++) {
                frame(0);
            }
            snap(sub[i].name);
            press(0x10);
            CHECK(settle(gameL, 60), "back to Gameplay from %s", sub[i].name);
            press(0x10);
            CHECK(settle(mainL, 60), "back to the menu from Gameplay (%s)", sub[i].name);
        }
    }
    /* Controls -> Remap, then a capture in progress */
    lt_ext_layout(mainL)->curItem = mainLink(UI_PAGE_CONTROLS);
    press(0x40);
    CHECK(settle(ui_settings_page_layout(UI_PAGE_CONTROLS), 60), "Controls");
    press(0x40);
    CHECK(settle(ui_settings_page_layout(UI_PAGE_REMAP), 60), "Remap");
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
    /* the New Game screen, the cursor on Difficulty (Normal, its note),
       then Hard; then Customize, the cursor on New Game+ (its note, in
       English and French) and its Shadows, Combat and Rules pages, back to
       the screen */
    gFlagGameClear = 0;
    int ml = ui_new_game_screen_enter();
    lt_switch_layout(ml);
    CHECK(settle(ml, 60), "the New Game screen");
    frame(0);
    snap("settings_new_game_screen.png");
    press(0x2000);
    frame(0);
    snap("settings_new_game_screen_hard.png");
    press(0x4000);
    press(0x40); /* Customize */
    {
        const int custL = ui_settings_page_layout(UI_PAGE_CUSTOMIZE);
        CHECK(settle(custL, 60), "Customize from the New Game screen (%d)", current_layout_id);
        frame(0);
        snap("settings_customize.png");
        lt_ext_layout(custL)->curItem = ui_settings_row_of(UI_PAGE_CUSTOMIZE, UI_OPT_NEWGAME_PLUS);
        frame(0);
        frame(0);
        snap("settings_customize_ngp.png");
        NonLinearCameraMove = 3; /* French: the longest note */
        frame(0);
        frame(0);
        snap("settings_customize_ngp_fr.png");
        NonLinearCameraMove = 2;
        frame(0);

        static const struct {
            UiSettingsPage page;
            const char *name;
        } cust[] = {{UI_PAGE_SHADOWS, "settings_customize_shadows.png"},
                    {UI_PAGE_COMBAT, "settings_customize_combat.png"},
                    {UI_PAGE_RULES, "settings_customize_rules.png"}};

        for (unsigned i = 0; i < sizeof(cust) / sizeof(cust[0]); i++) {
            openLink(custL, UI_PAGE_CUSTOMIZE, cust[i].page);
            for (int k = 0; k < 30; k++) {
                frame(0);
            }
            snap(cust[i].name);
            press(0x10);
            CHECK(settle(custL, 60), "back to Customize from %s", cust[i].name);
        }
        press(0x10);
        CHECK(settle(ml, 60), "Customize: back to the New Game screen");
    }
    ico_difficulty_reset();
    /* the quit confirmation, the cursor on Yes */
    int ql = ui_quit_screen_layout();
    lt_switch_layout(ql);
    CHECK(settle(ql, 60), "the quit screen");
    press(0x8000);
    frame(0);
    snap("settings_quit_screen.png");
    /* the title (New Game only): Options (the sheets' word as text in the
       menus' look) centred, Quit to desktop under it */
    lt_switch_layout(13);
    CHECK(settle(13, 60), "the title");
    {
        /* shown, as the title's proc (la_title) shows them once its menu
           is up */
        const int opt = ui_settings_entry_row(13), quit = ui_settings_quit_row(13);
        lt_ext_prop(opt)->defaultMask = lt_ext_prop(quit)->defaultMask = 0;
        ui_settings_title_mask(0);
        texLayout[13].curItem = opt;
        frame(0);
        snap("settings_title_entry.png");
        lt_ext_prop(opt)->defaultMask = lt_ext_prop(quit)->defaultMask = 1;
        ui_settings_title_mask(1);
    }
    /* the button pictures in the viewer's prompt lines at 1x */
    testArtPixels();
    /* the Settings and Display screens at Enhanced 4x, full height, the
       atlas at a 960-line output's scale (settings_main_4x.png,
       settings_display_4x.png) */
    {
        RdSettings e = *rd_get_settings();
        e.preset = RD_PRESET_ENHANCED;
        e.sceneScale = 4.0f;
        e.aspect = 4.0f / 3.0f;
        e.fullHeightScene = 1;
        e.outputWidth = 1280;
        e.outputHeight = 960;
        rd_set_settings(&e);
        ui_set_scale(ui_scale_for(1, e.outputHeight));
        lt_switch_layout(mainL);
        CHECK(settle(mainL, 60), "the menu at 4x");
        lt_ext_layout(mainL)->curItem =
            ui_settings_row_of(UI_PAGE_MAIN, UI_OPT_LANGUAGE); /* Language: its note */
        frame(0);
        frame(0);
        snap4("settings_main_4x.png");
        lt_ext_layout(mainL)->curItem = mainLink(UI_PAGE_DISPLAY);
        press(0x40);
        CHECK(settle(ui_settings_page_layout(UI_PAGE_DISPLAY), 60), "Display at 4x");
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
        lt_ext_layout(mainL)->curItem = mainLink(UI_PAGE_ACHIEVEMENTS);
        frame(0);
        frame(0);
        snap4("settings_main_title_4x.png");
        lt_ext_layout(mainL)->curItem = mainLink(UI_PAGE_EXTRAS);
        press(0x40);
        int exL = ui_settings_page_layout(UI_PAGE_EXTRAS);
        CHECK(settle(exL, 60), "Extras at 4x");
        press(0x4000);
        press(0x4000);
        frame(0);
        snap4("settings_extras_4x.png");
        /* the music gallery over the fake engine, the cursor on an entry
           that plays */
        gallery_set_engine(&kFakeEngine);
        lt_ext_layout(exL)->curItem = ui_settings_row_of(UI_PAGE_EXTRAS, UI_OPT_EXTRAS_MUSIC);
        frame(0);
        press(0x40);
        int galL = ui_settings_page_layout(UI_PAGE_MUSIC);
        CHECK(settle(galL, 60), "the gallery at 4x");
        press(0x40);
        frame(0);
        snap4("settings_music_4x.png");
        press(0x10); /* Back leaves for Extras, the Music row under the cursor */
        CHECK(settle(exL, 60), "back to Extras from the gallery at 4x");
        gallery_set_engine(NULL);
        /* Extras > Characters at the same 4x, the colours from
           a fixed seed, the cursor on Ico's tunic, its swatch beside the
           value */
        {
            int el[8], cl[16];
            ui_settings_page_rows(UI_PAGE_EXTRAS, el, NULL, NULL, 8);
            ico_appearance_randomize(12345u);
            lt_ext_layout(exL)->curItem = el[3];
            frame(0);
            press(0x40);
            const int chL = ui_settings_page_layout(UI_PAGE_CHARACTERS);
            CHECK(settle(chL, 60), "Characters at 4x");
            ui_settings_page_rows(UI_PAGE_CHARACTERS, cl, NULL, NULL, 16);
            lt_ext_layout(chL)->curItem = cl[5];
            for (int k = 0; k < 4; k++) {
                frame(0); /* the page refreshes */
            }
            snap4("settings_characters_4x.png");
            press(0x10);
            CHECK(settle(exL, 60), "back to Extras from Characters at 4x");
            /* the same page from the title inside the model
               viewer, a panel at the left (the model, the game's, is not
               drawn here: the flat colour shows where it stands) */
            static const UiCharactersHost host = {renderCharsEnter, renderCharsShown,
                                                  renderCharsSwitch, renderCharsLeave};
            ui_settings_set_characters_host(&host);
            lt_ext_layout(exL)->curItem = el[3];
            frame(0);
            press(0x40);
            CHECK(settle(chL, 60), "Characters in the viewer at 4x");
            lt_ext_layout(chL)->curItem = cl[5];
            for (int k = 0; k < 4; k++) {
                frame(0);
            }
            snap4("settings_characters_viewer_4x.png");
            ui_settings_set_characters_host(NULL);
            ico_appearance_reset();
            lt_switch_layout(exL);
            CHECK(settle(exL, 60), "back to Extras from the viewer's Characters at 4x");
        }
    }
    /* every screen presented at Enhanced 1080p (snap1080) */
    {
        RdSettings e = *rd_get_settings();
        e.preset = RD_PRESET_ENHANCED;
        e.sceneScale = 0.0f;
        e.sceneWidth = e.sceneHeight = 0;
        e.aspect = 16.0f / 9.0f;
        e.fullHeightScene = 0;
        e.outputWidth = 1920;
        e.outputHeight = 1080;
        rd_set_settings(&e);
        ui_set_scale(ui_scale_for(1, e.outputHeight));
        s_reduce = 1;
        lt_switch_layout(mainL);
        CHECK(settle(mainL, 60), "the menu at 1080p");
        lt_ext_layout(mainL)->curItem = ui_settings_row_of(UI_PAGE_MAIN, UI_OPT_LANGUAGE);
        frame(0);
        snap1080("settings_main");

        static const struct {
            UiSettingsPage page;
            int downs;
            const char *name;
        } pg[] = {{UI_PAGE_DISPLAY, 0, "settings_display"},
                  {UI_PAGE_PICTURE, 0, "settings_picture"},
                  {UI_PAGE_EFFECTS, 0, "settings_effects"},
                  {UI_PAGE_AUDIO, 0, "settings_audio"},
                  {UI_PAGE_CONTROLS, 1, "settings_controls"},
                  {UI_PAGE_GAMEPLAY, 0, "settings_gameplay"},
                  {UI_PAGE_ACHIEVEMENTS, 0, "settings_achievements"},
                  {UI_PAGE_EXTRAS, 2, "settings_extras"},
                  {UI_PAGE_DEVELOPER, 0, "settings_developer"}};

        for (unsigned i = 0; i < sizeof(pg) / sizeof(pg[0]); i++) {
            lt_ext_layout(mainL)->curItem = mainLink(pg[i].page);
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
        /* Gameplay's two pages at 1080p, through Gameplay */
        {
            static const struct {
                UiSettingsPage page;
                const char *name;
            } sub[] = {{UI_PAGE_CHAINS, "settings_gameplay_chains"},
                       {UI_PAGE_CUTSCENES, "settings_gameplay_cutscenes"}};

            for (unsigned i = 0; i < sizeof(sub) / sizeof(sub[0]); i++) {
                int gameL = -1;
                openGameplayPage(mainL, sub[i].page, &gameL);
                for (int k = 0; k < 30; k++) {
                    frame(0);
                }
                snap1080(sub[i].name);
                press(0x10);
                CHECK(settle(gameL, 60), "back to Gameplay from %s at 1080p", sub[i].name);
                press(0x10);
                CHECK(settle(mainL, 60), "back to the menu from Gameplay (%s) at 1080p",
                      sub[i].name);
            }
        }
        int mir = ui_new_game_screen_enter();
        lt_switch_layout(mir);
        CHECK(settle(mir, 60), "the New Game screen at 1080p");
        snap1080("settings_new_game_screen");
        /* Customize on New Game+, then Customize > Rules (the dimmed
           Knock-downs count at Normal) */
        press(0x4000);
        press(0x40);
        {
            const int custL = ui_settings_page_layout(UI_PAGE_CUSTOMIZE);
            CHECK(settle(custL, 60), "Customize at 1080p");
            lt_ext_layout(custL)->curItem =
                ui_settings_row_of(UI_PAGE_CUSTOMIZE, UI_OPT_NEWGAME_PLUS);
            frame(0);
            frame(0);
            snap1080("settings_customize_ngp");
            openLink(custL, UI_PAGE_CUSTOMIZE, UI_PAGE_RULES);
            for (int k = 0; k < 30; k++) {
                frame(0);
            }
            snap1080("settings_customize_rules");
            press(0x10);
            CHECK(settle(custL, 60), "back to Customize at 1080p");
            press(0x10);
            CHECK(settle(mir, 60), "back to the New Game screen at 1080p");
        }
        /* the pause menu with the journey's lines on its right (a
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
        int ql = ui_quit_screen_layout();
        lt_switch_layout(ql);
        CHECK(settle(ql, 60), "the quit screen at 1080p");
        press(0x8000);
        snap1080("settings_quit_screen");
        /* the music gallery with a stream playing (the fake engine: 42 s
           of 4:25), its bar and transport; then the model viewer's rows
           and prompts */
        {
            gallery_set_engine(&kFakeEngine);
            lt_switch_layout(13);
            CHECK(settle(13, 60), "the title at 1080p");
            press(0x4000);
            press(0x40);
            CHECK(settle(mainL, 60), "the menu from the title at 1080p");
            lt_ext_layout(mainL)->curItem = mainLink(UI_PAGE_EXTRAS);
            press(0x40);
            int exL = ui_settings_page_layout(UI_PAGE_EXTRAS);
            CHECK(settle(exL, 60), "Extras at 1080p");
            lt_ext_layout(exL)->curItem = ui_settings_row_of(UI_PAGE_EXTRAS, UI_OPT_EXTRAS_MUSIC);
            frame(0);
            press(0x40);
            int galL = ui_settings_page_layout(UI_PAGE_MUSIC);
            CHECK(settle(galL, 60), "the gallery at 1080p");
            press(0x40);
            snap1080("settings_music");
            press(0x10);
            CHECK(settle(exL, 60), "back to Extras at 1080p");
            gallery_set_engine(NULL);
            int vl = viewerLayout();
            lt_switch_layout(vl);
            CHECK(settle(vl, 60), "the viewer's rows at 1080p");
            snap1080("settings_viewer");
        }
        /* the save screen's slot numbers and play time */
        fakeSaveRows();
        lt_switch_layout(14);
        CHECK(settle(14, 60), "the save screen at 1080p");
        snap1080("settings_save_preview");
        testPhotoPanel();
        testTouchArt();
        s_reduce = 0;
    }
    CHECK(gif_HostUndecodedTotal() == 0, "%u undecoded writes", gif_HostUndecodedTotal());
    ui__set_record_hook(NULL);
    ui_font_shutdown();
    rd_shutdown();
    return failures ? 1 : 0;
}

/* settings_render DIR SLUG (ctests settings_render_jpn, settings_render_kor):
   the Japanese or Korean disc's title and main page over the fixture's
   tables at that disc's numbers (the region row's): the title's Settings
   and Quit rows on the menu's pitch under Continue and New Game, the
   copyright's two halves below; the port's text in the disc's language
   (Noto Sans JP's or KR's subset) in the disc's sheets' look; the menu
   text table not drawn (the game's words keep their texels). Pictures
   settings_title_<slug>.png, settings_main_<slug>.png. */
static int renderOtherDisc(const char *slug)
{
    const IcoRegionInfo *ri = ico_region_cur();
    char png[64];
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ORIGINAL;
    if (!rd_init(512, 512, &st, NULL)) {
        printf("settings_render: SKIP: no usable Vulkan device\n");
        return 77;
    }
    ui_font_forget_textures();
    UiGsFrame fr = {512, 512, 2048.0f, 2048.0f, UI_LAYOUT_Z};
    ui_set_gs_frame(&fr);
    ui_set_scale(1.0f);
    ui__set_record_hook(gif_HostFlush);
    makeSheets();
    gif_HostSetTex0Resolver(sheetResolve);
    dl_Init();
    GlobalStageSetting.reductionCol[0] = GlobalStageSetting.reductionCol[1] =
        GlobalStageSetting.reductionCol[2] = 0x80;
    const int mainL = enterMain(1);
    CHECK(ui_get_language() == ri->ui_lang_default && ui_sheet_language() == ri->menu_sheet,
          "the port's text the disc's, the sheets the disc's (%d, %d)", ui_get_language(),
          ui_sheet_language());
    for (int i = 0; i < 4; i++) {
        frame(0);
    }
    snprintf(png, sizeof(png), "settings_main_%s.png", slug);
    snap(png);
    /* the face that does not confirm: back to the title (Cross on the
       Japanese program's discs, Circle on the 2001 discs) */
    press((LT_PAD_CROSS | LT_PAD_CIRCLE) & ~lt_ext_confirm_button());
    const int t = fxLayout(13);
    CHECK(settle(t, 60), "the title (%d)", current_layout_id);
    const int opt = ui_settings_entry_row(t), quit = ui_settings_quit_row(t);
    lt_ext_prop(opt)->defaultMask = lt_ext_prop(quit)->defaultMask = 0;
    ui_settings_title_mask(0);
    texLayout[t].curItem = opt;
    frame(0);
    snprintf(png, sizeof(png), "settings_title_%s.png", slug);
    snap(png);
    const int pitch = lt_ext_prop(quit)->dispY - lt_ext_prop(opt)->dispY;
    CHECK(
        pitch > 0 && lt_ext_prop(opt)->dispY - texProperty[ri->row_title_new_only].dispY == pitch &&
            texProperty[ri->row_title_new_only].dispY - texProperty[ri->row_title_continue].dispY ==
                pitch &&
            texProperty[ri->row_title_copyright].dispY ==
                texProperty[ri->row_title_copyright2].dispY &&
            texProperty[ri->row_title_copyright].dispY > lt_ext_prop(quit)->dispY,
        "the title's rows: Continue %d, New Game %d, Settings %d, Quit %d, copyright %d %d",
        texProperty[ri->row_title_continue].dispY, texProperty[ri->row_title_new_only].dispY,
        lt_ext_prop(opt)->dispY, lt_ext_prop(quit)->dispY,
        texProperty[ri->row_title_copyright].dispY, texProperty[ri->row_title_copyright2].dispY);
    CHECK(menuTextDraws() > 0, "the port's rows drawn in the sheets' look (%d)", menuTextDraws());
    CHECK(ui_menu_text_item_of(&texProperty[fxRow(294)]) == NULL,
          "no row of the disc's own from the menu text table");
    lt_ext_prop(opt)->defaultMask = lt_ext_prop(quit)->defaultMask = 1;
    ui_settings_title_mask(1);
    rd_shutdown();
    printf("settings_render %s: %s\n", slug, failures ? "failed" : "ok");
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    snprintf(s_dir, sizeof(s_dir), "%s", argc > 1 ? argv[1] : ".");
    setEnv("LC_ALL", NULL);
    setEnv("LANG", "en_GB.UTF-8");
    if (argc > 2) {
        CHECK(ico_region_select(argv[2]) == 0, "the %s region", argv[2]);
        return renderOtherDisc(argv[2]);
    }
    return render();
}

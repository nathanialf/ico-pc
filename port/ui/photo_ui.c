/*
 * port/ui/photo_ui.c
 *
 * Photo mode's screen (photo_ui.h): a port layout of one masked row whose
 * proc feeds port/game/photo_mode.h, and the HUD on the overlay.
 */
#include "photo_ui.h"

#include <stdio.h>
#include <string.h>

#include "font.h"
#include "input.h" /* the mouse's sensitivity and invert */
#include "layout_ext.h"
#include "menu_font.h"
#include "mouse_look.h"
#include "options.h" /* ico_opt_mirror */
#include "photo_mode.h"
#include "popup.h"
#include "settings.h"
#include "strings.h"
#include "ui_list.h" /* ui_SettingsAddRow */

#ifdef ICO_RD
#include "rd.h"
#include "ui_internal.h" /* ui__DrawTexQuads */

/* seki/src/Texture.c: the rd texture of a texture table entry (0: none) */
extern unsigned int tex_HostTextureId(int idx);
#endif

/* the photo camera's degrees per mouse count at sensitivity 1 */
#define PHOTO_MOUSE_DEG 0.08f

/* the game's side */
extern PadState pad[16];
extern int stage_no;            /* common/src/main.c: 1 is the boot and title */
extern int NonLinearCameraMove; /* the language, 2..6 */
extern int systemStatus[12];    /* [0]: 1 PAL 50 Hz, 0 60 Hz; [1]: the frame step */
extern void NEGATIVE_SE(void);
extern void la_host_leave(void);
/* the object the game camera follows (omori/src/camera-root.c) and its root
   position (sugipon/src/geometryManager.c): photo mode's pivot, read once */
extern void *default_cameratarget_gobj;
extern void GetRootPosition(void *pos, void *obj);

static int s_layout = -1, s_row = -1;

static int photoProc(int first, int item);

int ui_PhotoBuild(void)
{
    /* one row, masked: nothing drawn, no cursor (the layout's curItem -1),
       and a texel rectangle of its own as every port row has */
    s_row =
        ui_SettingsAddRow(120, 200, 400, 40, 0, -1, UI_STR_PHOTO_MODE, NULL, 0.0f, UI_ALIGN_CENTER);
    if (s_row < 0) {
        return -1;
    }
    lt_ext_Prop(s_row)->defaultMask = 1;
    LtProp l;
    memset(&l, 0, sizeof(l));
    l.first = s_row;
    l.last = s_row + 1;
    l.fadeInTime = 0.3f;
    l.fadeOutTime = 0.1f;
    l.colA = 0.0f; /* no dimming: the scene as it is */
    l.proc = photoProc;
    l.procFirst = 1;
    l.defaultItem = -1;
    l.curItem = -1;
    l.link = -1;
    s_layout = lt_ext_AddLayout(&l);
    return s_layout;
}

int ui_PhotoLayout(void)
{
    return s_layout;
}

void ui_PhotoReset(void)
{
    s_layout = s_row = -1;
    ico_photo_exit();
}

int ui_PhotoAvailable(void)
{
    return stage_no > 1;
}

static int photoProc(int first, int item)
{
    (void)item;
    if (first) {
        /* 25 or 30 Main ticks a second: the frame step over the field rate */
        const int step = systemStatus[1] > 0 ? systemStatus[1] : 2;
        ico_photo_set_tick_hz((systemStatus[0] ? 50 : 60) / step);
        if (default_cameratarget_gobj != NULL) {
            float pos[4];
            GetRootPosition(pos, default_cameratarget_gobj);
            ico_photo_set_subject(pos);
        } else {
            ico_photo_set_subject(NULL);
        }
        ico_photo_enter();
    }
    /* the mouse's motion since the last tick (the window captures it
       while photo mode is open), taken every tick and dropped while the
       screen fades */
    float mdx, mdy;
    const int moved = ico_mouse_look_take(&mdx, &mdy);
    if (lt_fade_status() != 2) {
        return -1;
    }
    IcoPhotoPad p;
    p.held = (unsigned int)pad[0].now;
    p.pressed = (unsigned int)pad[0].flags;
    memcpy(p.ana, pad[0].ana, sizeof(p.ana));
    if (!ico_photo_active()) {
        ico_photo_enter(); /* came back without a first call (a reset) */
    }
    if (moved) {
        /* right turns right and up looks up, as the stick; the picture
           mirrored, so is the turn (the stick's x is negated the same way,
           ico_input_mirror); the mouse's own invert */
        const IcoBindings *b = ico_input_live_bindings();
        const float k = PHOTO_MOUSE_DEG * (b->mouse_sens > 0.0f ? b->mouse_sens : 1.0f);
        ico_photo_mouse_look((ico_opt_mirror() ? -mdx : mdx) * k,
                             (b->mouse_invert_y ? mdy : -mdy) * k);
    }
    if (ico_photo_update(&p)) {
        ico_photo_exit();
        NEGATIVE_SE();
        la_host_leave();
        /* the pause menu, the cursor on the row (settings.c) */
        return ui_SettingsPhotoBack();
    }
    return -1;
}

void ui_PhotoCaptureDone(int ok, const char *name)
{
    ui_SetLanguage(ui_LangFromGame(NonLinearCameraMove));
    ui_PopupPush(ui_Str(ok ? UI_STR_PHOTO_SAVED : UI_STR_PHOTO_FAILED), name ? name : "");
    fprintf(stderr, "photo: %s %s\n", ok ? "saved" : "not saved", name ? name : "");
}

/* ------------------------------------------------------------ the HUD */

#define HUD_SIZE 17.0f
#define HUD_X UI_PHOTO_HUD_X
#define HUD_BOTTOM 438.0f
#define HUD_PITCH 21.0f
/* the widest line's room: the 640-wide grid less the margins either side */
#define HUD_ROOM UI_PHOTO_HUD_ROOM

#ifdef ICO_RD
/* t with its first "%s" or "%d" replaced by arg (the translations are data,
   not formats), into buf */
static void fill(char *buf, size_t size, const char *t, const char *mark, const char *arg)
{
    const char *at = strstr(t, mark);
    if (at) {
        snprintf(buf, size, "%.*s%s%s", (int)(at - t), t, arg, at + strlen(mark));
    } else {
        snprintf(buf, size, "%s %s", t, arg);
    }
}

/* line 0: "Photo mode: Free camera, speed Normal" (the speed only for the
   free camera, the one it changes) */
static void hudTitle(char *buf, size_t size, const IcoPhotoState *st)
{
    const int freeCam = st->mode == ICO_PHOTO_CAM_FREE;
    /* French sets its colon apart */
    const char *colon = ui_GetLanguage() == UI_LANG_FR ? " : " : ": ";
    const char *cam = ui_Str(freeCam ? UI_STR_PHOTO_CAM_FREE : UI_STR_PHOTO_CAM_ORBIT);
    if (!freeCam) {
        snprintf(buf, size, "%s%s%s", ui_Str(UI_STR_PHOTO_MODE), colon, cam);
        return;
    }
    static const UiStrId words[3] = {UI_STR_PHOTO_SPEED_SLOW, UI_STR_PHOTO_SPEED_NORMAL,
                                     UI_STR_PHOTO_SPEED_FAST};
    const int sp = st->speed >= 0 && st->speed < 3 ? st->speed : ICO_PHOTO_SPEED_NORMAL;
    char speed[64];
    fill(speed, sizeof(speed), ui_Str(UI_STR_PHOTO_SPEED), "%s", ui_Str(words[sp]));
    snprintf(buf, size, "%s%s%s, %s", ui_Str(UI_STR_PHOTO_MODE), colon, cam, speed);
}

/* ---------------------------------------- the panel's items */

/* the buttons the panel names */
typedef enum HudBtn {
    BTN_LSTICK,
    BTN_RSTICK,
    BTN_UP,
    BTN_DOWN,
    BTN_L1,
    BTN_R1,
    BTN_L2,
    BTN_R2,
    BTN_L3,
    BTN_R3,
    BTN_SELECT,
    BTN_CROSS,
    BTN_SQUARE,
    BTN_TRIANGLE,
    BTN_NONE
} HudBtn;

/* each button's picture on the game's sheets (-1: none), its name (a
   string, else a literal: the sheets' L1 .. R2 say the same) for when it
   has no picture or the loaded tables are not the PAL ones, and the
   binding target it stands for (a stick: its up; the four follow) */
static const struct {
    int glyph;
    int str;
    const char *literal;
    int target;
} kBtn[BTN_NONE] = {
    {-1, UI_STR_STICK_LEFT, NULL, ICO_T_LSTICK_UP},
    {-1, UI_STR_STICK_RIGHT, NULL, ICO_T_RSTICK_UP},
    {-1, UI_STR_PHOTO_UP, NULL, ICO_T_UP},
    {-1, UI_STR_PHOTO_DOWN, NULL, ICO_T_DOWN},
    {LT_GLYPH_L1, -1, "L1", ICO_T_L1},
    {LT_GLYPH_R1, -1, "R1", ICO_T_R1},
    {LT_GLYPH_L2, -1, "L2", ICO_T_L2},
    {LT_GLYPH_R2, -1, "R2", ICO_T_R2},
    {-1, -1, "L3", ICO_T_L3},
    {-1, -1, "R3", ICO_T_R3},
    {-1, UI_STR_BTN_SELECT, NULL, ICO_T_SELECT},
    {LT_GLYPH_CROSS, UI_STR_BTN_CROSS, NULL, ICO_T_CROSS},
    {LT_GLYPH_SQUARE, UI_STR_BTN_SQUARE, NULL, ICO_T_SQUARE},
    {LT_GLYPH_TRIANGLE, UI_STR_BTN_TRIANGLE, NULL, ICO_T_TRIANGLE},
};

/* the name of the first key (or else the first mouse button) bound to
   target, into out; 0 when neither device has one */
static int keyName(int target, char *out, size_t size)
{
    static const int kinds[2] = {ICO_SRC_KEY, ICO_SRC_MOUSE};
    char row[64];

    for (int k = 0; k < 2; k++) {
        const char *t = ico_bindings_row_text(ico_input_live_bindings(), kinds[k], target, row,
                                              (unsigned)sizeof(row));
        if (!t || t[0] == '\0' || strcmp(t, "none") == 0) {
            continue;
        }
        const char *comma = strchr(t, ',');
        const int len = (int)(comma ? (size_t)(comma - t) : strlen(t));
        if (k == 1) {
            snprintf(out, size, "%s %.*s", ui_Str(UI_STR_PHOTO_MOUSE_PREFIX), len, t);
        } else {
            snprintf(out, size, "%.*s", len, t);
        }
        return 1;
    }
    return 0;
}

/* your key for button b in square brackets, stored in the set ("[W A S D]"
   for a stick: up, left, down, right); NULL when one is missing */
static const char *keysFor(UiHudSet *set, HudBtn b)
{
    if (set->nkeys >= UI_HUD_KEYS) {
        return NULL;
    }
    char *dst = set->keys[set->nkeys];
    const size_t size = sizeof(set->keys[0]);
    char name[4][24];
    if (b == BTN_LSTICK || b == BTN_RSTICK) {
        /* the targets run up, down, left, right */
        static const int order[4] = {0, 2, 1, 3};
        for (int i = 0; i < 4; i++) {
            if (!keyName(kBtn[b].target + order[i], name[i], sizeof(name[i]))) {
                return NULL;
            }
        }
        snprintf(dst, size, "[%s %s %s %s]", name[0], name[1], name[2], name[3]);
    } else {
        if (!keyName(kBtn[b].target, name[0], sizeof(name[0]))) {
            return NULL;
        }
        snprintf(dst, size, "[%s]", name[0]);
    }
    set->nkeys++;
    return dst;
}

static void addIcon(UiHudSet *set, UiHudItem *it, HudBtn b, int keyboard)
{
    if (it->nicon >= UI_HUD_ICONS) {
        return;
    }
    UiHudIcon *ic = &it->icon[it->nicon++];
    ic->glyph = -1;
    ic->word = keyboard ? keysFor(set, b) : NULL;
    if (ic->word) {
        return;
    }
    if (kBtn[b].glyph >= 0 && lt_ext_GlyphTexture(kBtn[b].glyph) >= 0) {
        ic->glyph = kBtn[b].glyph;
        return;
    }
    ic->word = kBtn[b].str >= 0 ? ui_Str((UiStrId)kBtn[b].str) : kBtn[b].literal;
}

static void addItem(UiHudSet *set, int line, UiStrId word, HudBtn a, HudBtn b, int keyboard)
{
    if (set->n >= UI_HUD_ITEMS) {
        return;
    }
    UiHudItem *it = &set->item[set->n++];
    memset(it, 0, sizeof(*it));
    it->line = line;
    it->text = ui_Str(word);
    addIcon(set, it, a, keyboard);
    if (b != BTN_NONE) {
        addIcon(set, it, b, keyboard);
    }
}

void ui__PhotoHudBuild(UiHudSet *set, int freeCam, int keyboard, const char *title, const char *fov)
{
    memset(set, 0, sizeof(*set));
    set->item[set->n].line = 0;
    set->item[set->n++].text = title;
    if (freeCam) {
        addItem(set, 1, UI_STR_PHOTO_ACT_MOVE, BTN_LSTICK, BTN_NONE, keyboard);
        addItem(set, 1, UI_STR_PHOTO_ACT_LOOK, BTN_RSTICK, BTN_NONE, keyboard);
        addItem(set, 1, UI_STR_PHOTO_ACT_RISE, BTN_UP, BTN_DOWN, keyboard);
    } else {
        addItem(set, 1, UI_STR_PHOTO_ACT_CIRCLE, BTN_LSTICK, BTN_NONE, keyboard);
        addItem(set, 1, UI_STR_PHOTO_ACT_NEARFAR, BTN_RSTICK, BTN_NONE, keyboard);
        addItem(set, 1, UI_STR_PHOTO_ACT_ZOOM, BTN_UP, BTN_DOWN, keyboard);
    }
    addItem(set, 2, UI_STR_PHOTO_ACT_ROLL, BTN_L1, BTN_R1, keyboard);
    addItem(set, 2, UI_STR_PHOTO_ACT_ZOOM, BTN_L2, BTN_R2, keyboard);
    addItem(set, 2, UI_STR_PHOTO_ACT_SWITCH, BTN_L3, BTN_NONE, keyboard);
    addItem(set, 2, UI_STR_PHOTO_ACT_SPEED, BTN_R3, BTN_NONE, keyboard);
    addItem(set, 2, UI_STR_PHOTO_ACT_RESET, BTN_SELECT, BTN_NONE, keyboard);
    addItem(set, 3, UI_STR_PHOTO_ACT_SAVE, BTN_CROSS, BTN_NONE, keyboard);
    addItem(set, 3, UI_STR_PHOTO_ACT_HIDE, BTN_SQUARE, BTN_NONE, keyboard);
    addItem(set, 3, UI_STR_PHOTO_ACT_BACK, BTN_TRIANGLE, BTN_NONE, keyboard);
    set->item[set->n].line = 4;
    set->item[set->n++].text = fov;
}

/* an icon's width at size: a picture's box beside a label of that em (the
   game's own pairing, layout_ext.h lt_ext_GlyphBox), else its word's */
static float iconWidth(const UiHudIcon *ic, float size)
{
    if (ic->glyph >= 0) {
        int w = 0;
        lt_ext_GlyphBox(ic->glyph, size, &w, NULL);
        return (float)w;
    }
    return ic->word && ic->word[0] ? ui_MeasureMenuText(size, ic->word) : 0.0f;
}

/* the gaps, in sizes: between an item's pictures, picture to word, item to
   item (squeezed to this fraction at the most when a line is too wide) */
#define GAP_ICON 0.16f
#define GAP_WORD 0.34f
#define GAP_ITEM 1.1f
#define GAP_MIN 0.4f

float ui__PhotoHudLayout(const UiHudItem *items, int n, float size, float room, UiHudPlaced *out)
{
    float widest = 0.0f;

    memset(out, 0, sizeof(out[0]) * (size_t)n);
    for (int line = 0; line < UI_HUD_LINES; line++) {
        float natural = 0.0f, gaps = 0.0f;
        int count = 0;
        /* each item's own width, then the line's */
        float w[UI_HUD_ITEMS] = {0};
        for (int i = 0; i < n; i++) {
            if (items[i].line != line) {
                continue;
            }
            float iw = 0.0f;
            for (int k = 0; k < items[i].nicon; k++) {
                const float cw = iconWidth(&items[i].icon[k], size);
                out[i].iconW[k] = cw;
                iw += cw + (k > 0 ? GAP_ICON * size : 0.0f);
            }
            const float tw =
                items[i].text && items[i].text[0] ? ui_MeasureMenuText(size, items[i].text) : 0.0f;
            out[i].textW = tw;
            iw += tw + (items[i].nicon > 0 && tw > 0.0f ? GAP_WORD * size : 0.0f);
            w[i] = iw;
            natural += iw;
            gaps += count > 0 ? GAP_ITEM * size : 0.0f;
            count++;
        }
        float gapScale = 1.0f;
        if (natural + gaps > room && gaps > 0.0f) {
            gapScale = 1.0f - (natural + gaps - room) / gaps;
            gapScale = gapScale < GAP_MIN ? GAP_MIN : gapScale;
        }
        float x = 0.0f;
        int first = 1;
        for (int i = 0; i < n; i++) {
            if (items[i].line != line) {
                continue;
            }
            x += first ? 0.0f : GAP_ITEM * size * gapScale;
            first = 0;
            out[i].x0 = x;
            float ix = x;
            for (int k = 0; k < items[i].nicon; k++) {
                out[i].iconX[k] = ix;
                ix += out[i].iconW[k] + GAP_ICON * size;
            }
            if (items[i].nicon > 0) {
                ix += GAP_WORD * size - GAP_ICON * size;
            }
            out[i].textX = ix;
            x += w[i];
            out[i].x1 = x;
        }
        widest = x > widest ? x : widest;
    }
    return widest;
}

float ui__PhotoHudFit(const UiHudItem *items, int n, float *size, float room, UiHudPlaced *out)
{
    float w = ui__PhotoHudLayout(items, n, *size, room, out);
    for (int i = 0; i < 8 && w > room; i++) {
        /* after the first step a little under the ratio, so it converges */
        *size *= room / w * (i > 0 ? 0.99f : 1.0f);
        w = ui__PhotoHudLayout(items, n, *size, room, out);
    }
    return w;
}

/* one picture of the game's button sheets, its box's left edge at x and its
   middle on mid (grid units): the texture the menus draw it from (the
   table's texNo, layout_ext.h lt_ext_GlyphTexture) magnified onto the
   output with bilinear filtering as the menus' text is, the texel
   rectangle half a texel in on each side as display_texture samples it */
static void hudGlyph(int glyph, float x, float mid, float size)
{
    const int no = lt_ext_GlyphTexture(glyph);
    const unsigned tex = no >= 0 ? tex_HostTextureId(no) : 0u;
    if (!tex) {
        return;
    }
    int uvwh[4], w = 0, h = 0;
    lt_ext_GlyphSource(glyph, uvwh);
    lt_ext_GlyphBox(glyph, size, &w, &h);
    UiTexQuad q;
    q.x0 = x;
    q.x1 = x + (float)w;
    q.y0 = mid - (float)h * 0.5f;
    q.y1 = q.y0 + (float)h;
    q.u0 = (float)uvwh[0] + 0.5f;
    q.v0 = (float)uvwh[1] + 0.5f;
    q.u1 = (float)(uvwh[0] + uvwh[2]) - 0.5f;
    q.v1 = (float)(uvwh[1] + uvwh[3]) - 0.5f;
    static const uint8_t grey[4] = {0x80, 0x80, 0x80, 0x80};
    ui__DrawTexQuads(tex, &q, 1, grey, 0, NULL, 0);
}
#endif

void ui_PhotoDrawOverlay(const struct RdOverlayCtx *ctx)
{
#ifdef ICO_RD
    if (!ctx || !ico_photo_hud() || !ui_FontInit()) {
        return;
    }
    ui_SetLanguage(ui_LangFromGame(NonLinearCameraMove));
    IcoPhotoState st;
    ico_photo_get(&st);
    char fov[96] = "";
    const float fovNow = ico_photo_fov_now();
    if (fovNow > 0.0f) {
        char deg[16];
        snprintf(deg, sizeof(deg), "%d", (int)(fovNow + 0.5f));
        fill(fov, sizeof(fov), ui_Str(UI_STR_PHOTO_FOV), "%d", deg);
    }
    char title[160];
    hudTitle(title, sizeof(title), &st);
    /* your keys once a key or the mouse was the last thing pressed, the
       pad's buttons before that */
    int kind = ICO_SRC_NONE;
    ico_input_last_press(&kind, NULL);
    static UiHudSet set;
    static UiHudPlaced at[UI_HUD_ITEMS];
    ui__PhotoHudBuild(&set, st.mode == ICO_PHOTO_CAM_FREE,
                      kind == ICO_SRC_KEY || kind == ICO_SRC_MOUSE, title, fov);
    ui_BeginOverlay(ctx);
    /* the text smaller when the widest line (a long translation) would
       leave the screen */
    float size = HUD_SIZE;
    float w = ui__PhotoHudFit(set.item, set.n, &size, HUD_ROOM, at);
    w = w > HUD_ROOM ? HUD_ROOM : w;
    const float top = HUD_BOTTOM - (float)UI_HUD_LINES * HUD_PITCH;
    static const uint8_t panel[4] = {6, 6, 9, 0x50};
    ui_DrawRect(HUD_X - 8.0f, top - 6.0f, HUD_X + w + 8.0f, HUD_BOTTOM + 4.0f, panel);
    static const uint8_t titleCol[4] = {0x80, 0x7C, 0x70, 0x80}, body[4] = {0x6A, 0x68, 0x62, 0x80},
                         pic[4] = {0x7A, 0x78, 0x70, 0x80};
    float asc, desc, cap;
    ui_MenuFontMetrics(size, &asc, &desc, &cap);
    for (int i = 0; i < set.n; i++) {
        const UiHudItem *it = &set.item[i];
        const float mid = top + (float)it->line * HUD_PITCH + asc - cap * 0.5f;
        for (int k = 0; k < it->nicon; k++) {
            const UiHudIcon *ic = &it->icon[k];
            if (ic->glyph >= 0) {
                hudGlyph(ic->glyph, HUD_X + at[i].iconX[k], mid, size);
            } else if (ic->word && ic->word[0]) {
                ui_DrawMenuText(HUD_X + at[i].iconX[k], mid, size, pic, ic->word, UI_VALIGN_MIDDLE,
                                UI_INK_LIGHT, NULL);
            }
        }
        if (it->text && it->text[0]) {
            /* the menus' look, the 1x strip magnified */
            ui_DrawMenuText(HUD_X + at[i].textX, mid, size, it->line == 0 ? titleCol : body,
                            it->text, UI_VALIGN_MIDDLE, UI_INK_LIGHT, NULL);
        }
    }
    ui_EndOverlay();
#else
    (void)ctx;
#endif
}

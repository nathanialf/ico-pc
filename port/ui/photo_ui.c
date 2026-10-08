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
#include "layout_ext.h"
#include "menu_font.h"
#include "photo_mode.h"
#include "popup.h"
#include "settings.h"
#include "strings.h"
#include "ui_list.h" /* ui_SettingsAddRow */

#ifdef ICO_RD
#include "input.h"
#include "rd.h"
#endif

/* the game's side */
extern PadState pad[16];
extern int stage_no;            /* common/src/StageManager.c: 1 is the boot and title */
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

/* the binding target a button picture stands for (the stick pictures
   stand for four, up, left, down, right) */
static int targetOf(UiBtnGlyph g)
{
    switch (g) {
    case UI_BTN_UP:
        return ICO_T_UP;
    case UI_BTN_DOWN:
        return ICO_T_DOWN;
    case UI_BTN_LEFT:
        return ICO_T_LEFT;
    case UI_BTN_RIGHT:
        return ICO_T_RIGHT;
    case UI_BTN_CROSS:
        return ICO_T_CROSS;
    case UI_BTN_CIRCLE:
        return ICO_T_CIRCLE;
    case UI_BTN_SQUARE:
        return ICO_T_SQUARE;
    case UI_BTN_TRIANGLE:
        return ICO_T_TRIANGLE;
    case UI_BTN_L1:
        return ICO_T_L1;
    case UI_BTN_R1:
        return ICO_T_R1;
    case UI_BTN_L2:
        return ICO_T_L2;
    case UI_BTN_R2:
        return ICO_T_R2;
    case UI_BTN_L3:
        return ICO_T_L3;
    case UI_BTN_R3:
        return ICO_T_R3;
    case UI_BTN_SELECT:
        return ICO_T_SELECT;
    case UI_BTN_START:
        return ICO_T_START;
    default:
        return -1;
    }
}

/* the name of the first key (or else the first mouse button) bound to
   target, stored in the set; NULL when neither device has one */
static const char *keyNameFor(UiHudSet *set, int target)
{
    static const int kinds[2] = {ICO_SRC_KEY, ICO_SRC_MOUSE};
    char row[64];

    for (int k = 0; k < 2 && target >= 0 && set->nkeys < UI_HUD_KEYS; k++) {
        const char *t = ico_bindings_row_text(ico_input_live_bindings(), kinds[k], target, row,
                                              (unsigned)sizeof(row));
        if (!t || t[0] == '\0' || strcmp(t, "none") == 0) {
            continue;
        }
        char *dst = set->keys[set->nkeys];
        const char *comma = strchr(t, ',');
        const size_t len = comma ? (size_t)(comma - t) : strlen(t);
        snprintf(dst, sizeof(set->keys[0]), "%s%.*s", k == 1 ? "Mouse " : "", (int)len, t);
        set->nkeys++;
        return dst;
    }
    return NULL;
}

static void addIcon(UiHudSet *set, UiHudItem *it, UiBtnGlyph g, int keyboard)
{
    if (it->nicon >= UI_HUD_ICONS) {
        return;
    }
    if (keyboard && (g == UI_BTN_LSTICK || g == UI_BTN_RSTICK) && set->nkeys + 4 <= UI_HUD_KEYS &&
        it->nicon + 4 <= UI_HUD_ICONS) {
        /* four caps, up, left, down, right (W A S D) */
        const int base = g == UI_BTN_LSTICK ? ICO_T_LSTICK_UP : ICO_T_RSTICK_UP;
        const int order[4] = {0, 2, 1, 3}; /* the enum runs up, down, left, right */
        const char *name[4];
        const int mark = set->nkeys;
        int ok = 1;
        for (int i = 0; i < 4 && ok; i++) {
            name[i] = keyNameFor(set, base + order[i]);
            ok = name[i] != NULL;
        }
        if (ok) {
            for (int i = 0; i < 4; i++) {
                it->icon[it->nicon].glyph = g;
                it->icon[it->nicon++].key = name[i];
            }
            return;
        }
        set->nkeys = mark;
    } else if (keyboard && targetOf(g) >= 0) {
        const char *name = keyNameFor(set, targetOf(g));
        if (name) {
            it->icon[it->nicon].glyph = g;
            it->icon[it->nicon++].key = name;
            return;
        }
    }
    it->icon[it->nicon].glyph = g;
    it->icon[it->nicon++].key = NULL;
}

static UiHudItem *addItem(UiHudSet *set, int line, UiStrId word, UiBtnGlyph a, UiBtnGlyph b,
                          int keyboard)
{
    if (set->n >= UI_HUD_ITEMS) {
        return NULL;
    }
    UiHudItem *it = &set->item[set->n++];
    memset(it, 0, sizeof(*it));
    it->line = line;
    it->text = ui_Str(word);
    addIcon(set, it, a, keyboard);
    if (b != UI_BTN_COUNT) {
        addIcon(set, it, b, keyboard);
    }
    return it;
}

void ui__PhotoHudBuild(UiHudSet *set, int freeCam, int keyboard, const char *title, const char *fov)
{
    const UiBtnGlyph none = UI_BTN_COUNT;

    memset(set, 0, sizeof(*set));
    set->item[set->n].line = 0;
    set->item[set->n++].text = title;
    if (freeCam) {
        addItem(set, 1, UI_STR_PHOTO_ACT_MOVE, UI_BTN_LSTICK, none, keyboard);
        addItem(set, 1, UI_STR_PHOTO_ACT_LOOK, UI_BTN_RSTICK, none, keyboard);
        addItem(set, 1, UI_STR_PHOTO_ACT_RISE, UI_BTN_UP, UI_BTN_DOWN, keyboard);
    } else {
        addItem(set, 1, UI_STR_PHOTO_ACT_CIRCLE, UI_BTN_LSTICK, none, keyboard);
        addItem(set, 1, UI_STR_PHOTO_ACT_NEARFAR, UI_BTN_RSTICK, none, keyboard);
        addItem(set, 1, UI_STR_PHOTO_ACT_ZOOM, UI_BTN_UP, UI_BTN_DOWN, keyboard);
    }
    addItem(set, 2, UI_STR_PHOTO_ACT_ROLL, UI_BTN_L1, UI_BTN_R1, keyboard);
    addItem(set, 2, UI_STR_PHOTO_ACT_ZOOM, UI_BTN_L2, UI_BTN_R2, keyboard);
    addItem(set, 2, UI_STR_PHOTO_ACT_SWITCH, UI_BTN_L3, none, keyboard);
    addItem(set, 2, UI_STR_PHOTO_ACT_SPEED, UI_BTN_R3, none, keyboard);
    addItem(set, 2, UI_STR_PHOTO_ACT_RESET, UI_BTN_SELECT, none, keyboard);
    addItem(set, 3, UI_STR_PHOTO_ACT_SAVE, UI_BTN_CROSS, none, keyboard);
    addItem(set, 3, UI_STR_PHOTO_ACT_HIDE, UI_BTN_SQUARE, none, keyboard);
    addItem(set, 3, UI_STR_PHOTO_ACT_BACK, UI_BTN_TRIANGLE, none, keyboard);
    set->item[set->n].line = 4;
    set->item[set->n++].text = fov;
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
                const float cw = items[i].icon[k].key ? ui_KeyCapWidth(items[i].icon[k].key, size)
                                                      : ui_GlyphWidth(items[i].icon[k].glyph, size);
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
       pad's pictures before that */
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
    /* the pictures in the overlay's grid, mapped to the output as the text is */
    float ax, ay, bx, by;
    ui_OverlayMap(0.0f, 0.0f, &ax, &ay);
    ui_OverlayMap(1.0f, 1.0f, &bx, &by);
    UiGlyphXf xf;
    xf.kx = (bx - ax) / 16.0f;
    xf.ky = (by - ay) / 16.0f;
    xf.ox = ax / 16.0f;
    xf.oy = ay / 16.0f;
    xf.maxX = (float)(ctx->outW < 4095u ? ctx->outW : 4095u) * 16.0f;
    xf.maxY = (float)(ctx->outH < 4095u ? ctx->outH : 4095u) * 16.0f;
    xf.opacity = 1.0f;
    ui_GlyphBegin(&xf);
    float asc, desc, cap;
    ui_MenuFontMetrics(size, &asc, &desc, &cap);
    for (int i = 0; i < set.n; i++) {
        const UiHudItem *it = &set.item[i];
        const float mid = top + (float)it->line * HUD_PITCH + asc - cap * 0.5f;
        for (int k = 0; k < it->nicon; k++) {
            const float cx = HUD_X + at[i].iconX[k] + at[i].iconW[k] * 0.5f;
            if (it->icon[k].key) {
                ui_DrawKeyCap(it->icon[k].key, cx, mid, size, pic);
            } else {
                ui_DrawButtonGlyph(it->icon[k].glyph, cx, mid, size, pic);
            }
        }
        if (it->text && it->text[0]) {
            /* v0.4.2 (package F-B): the menus' look, the 1x strip magnified */
            ui_DrawMenuText(HUD_X + at[i].textX, mid, size, it->line == 0 ? titleCol : body,
                            it->text, UI_VALIGN_MIDDLE, UI_INK_LIGHT, NULL);
        }
    }
    ui_GlyphFlush();
    ui_EndOverlay();
#else
    (void)ctx;
#endif
}

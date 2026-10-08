/*
 * port/ui/popup.c
 *
 * The popup queue and its drawing (popup.h).
 */
#include "popup.h"

#include <string.h>

#ifdef ICO_RD
#include "rd.h"
#endif

#include "font.h"
#include "menu_font.h"
#include "strings.h"

/* the panel, in the layout grid (font.h) */
#define TITLE_SIZE 26.0f
#define BODY_SIZE 21.0f
#define PAD_X 14.0f
#define PAD_Y 9.0f
#define GAP_Y 3.0f
#define MARGIN_X 18.0f /* from the right edge of the 4:3 picture */
#define TOP_Y 30.0f
#define MIN_W 200.0f
#define MAX_W (UI_GRID_W - 2.0f * MARGIN_X)
#define DEV_TEST_TICK 100u
#define DEV_TEST_PERIOD 150u /* ticks: 6 s at 25 Hz, longer than one popup */

typedef struct Popup {
    char title[UI_POPUP_TEXT];
    char body[UI_POPUP_TEXT];
} Popup;

static Popup s_queue[UI_POPUP_QUEUE];
static int s_head, s_count;
static unsigned int s_age; /* vsyncs the head popup has been shown */
static int s_devTest;
static unsigned int s_devNext = DEV_TEST_TICK;

static void copyText(char *dst, const char *src)
{
    size_t n = src ? strlen(src) : 0;
    if (n >= UI_POPUP_TEXT) {
        n = UI_POPUP_TEXT - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) {
            n--;
        }
    }
    if (n) {
        memcpy(dst, src, n);
    }
    dst[n] = '\0';
}

int ui_PopupPush(const char *title, const char *body)
{
    if (s_count == UI_POPUP_QUEUE) {
        return -1;
    }
    Popup *p = &s_queue[(s_head + s_count) % UI_POPUP_QUEUE];
    copyText(p->title, title);
    copyText(p->body, body);
    if (s_count == 0) {
        s_age = 0;
    }
    s_count++;
    return 0;
}

void ui_PopupReset(void)
{
    s_head = s_count = 0;
    s_age = 0;
    s_devNext = DEV_TEST_TICK;
}

int ui_PopupActive(void)
{
    return s_count > 0;
}

void ui_PopupVsync(void)
{
    if (s_count == 0) {
        return;
    }
    s_age++;
    if (s_age >= 2 * UI_POPUP_SLIDE_VSYNCS + UI_POPUP_HOLD_VSYNCS) {
        s_head = (s_head + 1) % UI_POPUP_QUEUE;
        s_count--;
        s_age = 0;
    }
}

void ui_PopupSetDevTest(int on)
{
    s_devTest = on != 0;
}

void ui_PopupDevTick(unsigned int mainTick)
{
    if (s_devTest && mainTick >= s_devNext) {
        while (s_devNext <= mainTick) {
            s_devNext += DEV_TEST_PERIOD;
        }
        ui_PopupPush(ui_Str(UI_STR_POPUP_TEST_TITLE), ui_Str(UI_STR_POPUP_TEST_BODY));
    }
}

/* 0 .. 1: how far in the panel is (smoothstep in, hold, smoothstep out) */
static float shown(void)
{
    float t;
    if (s_age < UI_POPUP_SLIDE_VSYNCS) {
        t = (float)s_age / (float)UI_POPUP_SLIDE_VSYNCS;
    } else if (s_age < UI_POPUP_SLIDE_VSYNCS + UI_POPUP_HOLD_VSYNCS) {
        t = 1.0f;
    } else {
        t = (float)(2 * UI_POPUP_SLIDE_VSYNCS + UI_POPUP_HOLD_VSYNCS - s_age) /
            (float)UI_POPUP_SLIDE_VSYNCS;
    }
    if (t < 0.0f) {
        t = 0.0f;
    }
    return t * t * (3.0f - 2.0f * t);
}

typedef struct Panel {
    float x0, y0, x1, y1;
    float titleBase, bodyTop;
    float alpha;
} Panel;

static int panel(Panel *pn)
{
    if (s_count == 0) {
        return 0;
    }
    const Popup *p = &s_queue[s_head];
    float ta, td, ba, bd;
    /* v0.4.2 (package F-B): the menus' text (menu_font.h), measured as its
       strips lay it out */
    ui_MenuFontMetrics(TITLE_SIZE, &ta, &td, NULL);
    ui_MenuFontMetrics(BODY_SIZE, &ba, &bd, NULL);
    float w = ui_MeasureMenuText(TITLE_SIZE, p->title);
    float bw = ui_MeasureMenuText(BODY_SIZE, p->body);
    if (bw > w) {
        w = bw;
    }
    w += 2.0f * PAD_X;
    if (w < MIN_W) {
        w = MIN_W;
    }
    if (w > MAX_W) {
        w = MAX_W;
    }
    int bodyLines = p->body[0] ? 1 : 0;
    for (const char *c = p->body; *c; c++) {
        bodyLines += *c == '\n';
    }
    const float h =
        PAD_Y + ta + td + (bodyLines ? GAP_Y + (float)bodyLines * (ba + bd) : 0.0f) + PAD_Y;
    const float k = shown();
    const float right = UI_GRID_W - MARGIN_X + (1.0f - k) * (w + MARGIN_X + 4.0f);
    pn->x1 = right;
    pn->x0 = right - w;
    pn->y0 = TOP_Y;
    pn->y1 = TOP_Y + h;
    pn->titleBase = TOP_Y + PAD_Y + ta;
    pn->bodyTop = pn->titleBase + td + GAP_Y;
    pn->alpha = k;
    return 1;
}

int ui_PopupPanel(float rect[4])
{
    Panel pn;
    if (!panel(&pn)) {
        return 0;
    }
    rect[0] = pn.x0;
    rect[1] = pn.y0;
    rect[2] = pn.x1;
    rect[3] = pn.y1;
    return 1;
}

#ifdef ICO_RD
static void scaled(uint8_t out[4], uint8_t r, uint8_t g, uint8_t b, uint8_t a, float k)
{
    out[0] = r;
    out[1] = g;
    out[2] = b;
    out[3] = (uint8_t)((float)a * k + 0.5f);
}
#endif

void ui_PopupDrawOverlay(const struct RdOverlayCtx *ctx)
{
#ifdef ICO_RD
    if (s_count == 0 || !ctx || !ui_FontInit()) {
        return;
    }
    /* package OV: on the output (font.h ui_BeginOverlay), at this present;
       measured at the overlay's scale, so the panel fits its text */
    ui_BeginOverlay(ctx);
    Panel pn;
    if (panel(&pn) && pn.alpha > 0.0f) {
        const Popup *p = &s_queue[s_head];
        uint8_t c[4];
        /* the panel: dark, translucent, a hairline in the menu's warm grey
           on top */
        scaled(c, 6, 6, 9, 0x5C, pn.alpha);
        ui_DrawRect(pn.x0, pn.y0, pn.x1, pn.y1, c);
        scaled(c, 0x5E, 0x58, 0x4C, 0x80, pn.alpha);
        ui_DrawRect(pn.x0, pn.y0, pn.x1, pn.y0 + 1.5f, c);
        /* v0.4.2 (package F-B): in the menus' look, the same 1x strip as
           the scene's text magnified onto the output */
        scaled(c, 0x80, 0x7C, 0x70, 0x80, pn.alpha);
        ui_DrawMenuText(pn.x0 + PAD_X, pn.titleBase, TITLE_SIZE, c, p->title, UI_VALIGN_BASELINE,
                        UI_INK_LIGHT, NULL);
        if (p->body[0]) {
            scaled(c, 0x66, 0x64, 0x5E, 0x80, pn.alpha);
            ui_DrawMenuText(pn.x0 + PAD_X, pn.bodyTop, BODY_SIZE, c, p->body, UI_VALIGN_TOP,
                            UI_INK_LIGHT, NULL);
        }
    }
    ui_EndOverlay();
#else
    (void)ctx;
#endif
}

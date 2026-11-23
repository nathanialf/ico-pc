/*
 * port/ui/popup.c
 *
 * The popup queue and its drawing (popup.h).
 */
#include "popup.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef ICO_RD
#include "rd.h"
#endif

#include "font.h"
#include "menu_font.h"
#include "strings.h"

/* the panel, in the layout grid (font.h): popup.h's UI_POPUP_* sizes */
#define TITLE_SIZE UI_POPUP_TITLE_SIZE
#define BODY_SIZE UI_POPUP_BODY_SIZE
#define PAD_X UI_POPUP_PAD_X
#define PAD_Y UI_POPUP_PAD_Y
#define GAP_Y UI_POPUP_GAP_Y
#define MARGIN_X UI_POPUP_MARGIN_X
#define TOP_Y 30.0f
#define MIN_W UI_POPUP_MIN_W
#define MAX_W (UI_GRID_W - 2.0f * MARGIN_X)
/* the queue's panel below the standing notice's while that is up */
#define STACK_GAP 6.0f
#define DEV_TEST_TICK 100u
#define DEV_TEST_PERIOD 150u /* ticks: 6 s at 25 Hz, longer than one popup */

typedef struct Popup {
    char title[UI_POPUP_TEXT];
    char body[UI_POPUP_TEXT];
} Popup;

static Popup s_queue[UI_POPUP_QUEUE];

/* the standing notice (ui_popup_notice_show): its own slot, never in the
   queue; its line's text and the button word after it (the drawer's
   picture or key in its place where one is set) */
static struct {
    int on;      /* shown (sliding in or held) */
    int leaving; /* sliding out */
    unsigned int age;
    char title[UI_POPUP_TEXT];
    char body[UI_POPUP_TEXT];
    char hint[UI_POPUP_TEXT];
} s_note;

static UiPopupButtonFn s_button; /* the Triangle's picture or key (ui_popup_set_button_drawer) */
static int s_head, s_count;
static unsigned int s_age;             /* vsyncs the head popup has been shown */
static int s_hz = UI_POPUP_DEFAULT_HZ; /* the rate ui_popup_vsync runs at */
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

/* The Japanese and Korean text's line breaks in a body (UI_POPUP_TEXT
   bytes): a line wider than the panel's widest body takes a line break at
   the last place between two characters where one may go (strings.h
   ui_break_before), or, Korean, at the last space after a Hangul character
   (the space becomes the break), or, a Korean word alone wider than the
   panel, between two of its Hangul characters (ui_hangul_char).  A space
   becomes the break in place, so it needs no room; an inserted break needs
   a byte the text has room for.  Latin text keeps its lines as written:
   ui_break_before gives no place between two Latin characters. */
static void wrapWide(char *s)
{
    const float width = MAX_W - 2.0f * PAD_X;
    size_t n = strlen(s), lineStart = 0, lastBreak = 0, lastSpace = 0, lastInWord = 0;
    uint32_t prev = 0;
    for (size_t i = 0; i < n;) {
        const char *q = s + i;
        const uint32_t cp = ui_utf8_next(&q);
        const size_t len = (size_t)(q - (s + i));
        if (cp == '\n') {
            lineStart = i + 1;
            prev = 0;
            i += len;
            continue;
        }
        if (i > lineStart && ui_break_before(prev, cp)) {
            lastBreak = i;
        }
        if (i > lineStart && cp == ' ' && ui_hangul_char(prev)) {
            lastSpace = i;
        }
        if (i > lineStart && ui_hangul_char(prev) && ui_hangul_char(cp)) {
            lastInWord = i;
        }
        prev = cp;
        i += len;
        if (lastBreak > lineStart || lastSpace > lineStart || lastInWord > lineStart) {
            const char saved = s[i];
            s[i] = '\0';
            const float w = ui_measure_menu_text(BODY_SIZE, s + lineStart);
            s[i] = saved;
            if (w > width && lastSpace > lineStart && lastSpace >= lastBreak) {
                s[lastSpace] = '\n';
                lineStart = lastSpace + 1;
            } else if (w > width && n + 1 < UI_POPUP_TEXT) {
                const size_t at = lastBreak > lineStart ? lastBreak : lastInWord;
                memmove(s + at + 1, s + at, n - at + 1);
                s[at] = '\n';
                n++;
                i++;
                lineStart = at + 1;
            }
        }
    }
}

int ui_popup_push(const char *title, const char *body)
{
    if (s_count == UI_POPUP_QUEUE) {
        return -1;
    }
    Popup *p = &s_queue[(s_head + s_count) % UI_POPUP_QUEUE];
    copyText(p->title, title);
    copyText(p->body, body);
    wrapWide(p->body);
    if (s_count == 0) {
        s_age = 0;
    }
    s_count++;
    return 0;
}

void ui_popup_reset(void)
{
    memset(&s_note, 0, sizeof(s_note));
    s_head = s_count = 0;
    s_age = 0;
    s_devNext = DEV_TEST_TICK;
}

int ui_popup_active(void)
{
    return s_count > 0;
}

void ui_popup_set_vsync_hz(int hz)
{
    s_hz = hz > 0 ? hz : UI_POPUP_DEFAULT_HZ;
}

int ui_popup_vsync_hz(void)
{
    return s_hz;
}

int ui_popup_slide_vsyncs(void)
{
    return (s_hz * UI_POPUP_SLIDE_TENTHS + 5) / 10;
}

int ui_popup_hold_vsyncs(void)
{
    return s_hz * UI_POPUP_HOLD_SECONDS;
}

void ui_popup_vsync(void)
{
    if (s_note.on || s_note.leaving) {
        s_note.age++;
        if (s_note.leaving && s_note.age >= (unsigned int)ui_popup_slide_vsyncs()) {
            memset(&s_note, 0, sizeof(s_note)); /* slid out */
        }
    }
    if (s_count == 0) {
        return;
    }
    s_age++;
    if (s_age >= (unsigned int)(2 * ui_popup_slide_vsyncs() + ui_popup_hold_vsyncs())) {
        s_head = (s_head + 1) % UI_POPUP_QUEUE;
        s_count--;
        s_age = 0;
    }
}

void ui_popup_set_dev_test(int on)
{
    s_devTest = on != 0;
}

void ui_popup_dev_tick(unsigned int mainTick)
{
    if (s_devTest && mainTick >= s_devNext) {
        while (s_devNext <= mainTick) {
            s_devNext += DEV_TEST_PERIOD;
        }
        ui_popup_push(ui_str(UI_STR_POPUP_TEST_TITLE), ui_str(UI_STR_POPUP_TEST_BODY));
    }
}

/* 0 .. 1: how far in the panel is (smoothstep in, hold, smoothstep out) */
static float shown(void)
{
    const int slide = ui_popup_slide_vsyncs(), hold = ui_popup_hold_vsyncs();
    const int age = (int)s_age;
    float t;
    if (age < slide) {
        t = (float)age / (float)slide;
    } else if (age < slide + hold) {
        t = 1.0f;
    } else {
        t = (float)(2 * slide + hold - age) / (float)slide;
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

/* the gaps of the notice's line, in body sizes: its text to the button,
   the button to its word (photo mode's HUD's, photo_ui.c GAP_WORD) */
#define NOTE_GAP_TEXT 1.0f
#define NOTE_GAP_WORD 0.34f

/* the button's width at the body's size: the drawer's (its picture or key)
   or else its word */
static float buttonWidth(void)
{
    if (s_button != NULL) {
        const float w = s_button(0.0f, 0.0f, BODY_SIZE, 0);
        if (w > 0.0f) {
            return w;
        }
    }
    return ui_measure_menu_text(BODY_SIZE, ui_str(UI_STR_BTN_TRIANGLE));
}

/* a panel at top whose body is lines of body (and, the notice, one more
   width on its one line: the button and its word), slid in by k */
static void layoutPanel(Panel *pn, const char *title, const char *body, float extraW, float top,
                        float k)
{
    float ta, td, ba, bd;
    /* the menus' text (menu_font.h), measured as its
       strips lay it out */
    ui_menu_font_metrics(TITLE_SIZE, &ta, &td, NULL);
    ui_menu_font_metrics(BODY_SIZE, &ba, &bd, NULL);
    float w = ui_measure_menu_text(TITLE_SIZE, title);
    float bw = ui_measure_menu_text(BODY_SIZE, body) + extraW;
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
    int bodyLines = body[0] || extraW > 0.0f ? 1 : 0;
    for (const char *c = body; *c; c++) {
        bodyLines += *c == '\n';
    }
    const float h =
        PAD_Y + ta + td + (bodyLines ? GAP_Y + (float)bodyLines * (ba + bd) : 0.0f) + PAD_Y;
    const float right = UI_GRID_W - MARGIN_X + (1.0f - k) * (w + MARGIN_X + 4.0f);
    pn->x1 = right;
    pn->x0 = right - w;
    pn->y0 = top;
    pn->y1 = top + h;
    pn->titleBase = top + PAD_Y + ta;
    pn->bodyTop = pn->titleBase + td + GAP_Y;
    pn->alpha = k;
}

/* the notice's line's extra width: the gap, the button, its word */
static float noteExtra(void)
{
    return NOTE_GAP_TEXT * BODY_SIZE + buttonWidth() + NOTE_GAP_WORD * BODY_SIZE +
           ui_measure_menu_text(BODY_SIZE, s_note.hint);
}

/* 0 .. 1: how far in the notice is (smoothstep in, held until hidden,
   smoothstep out) */
static float noteShown(void)
{
    const int slide = ui_popup_slide_vsyncs();
    float t = (int)s_note.age < slide ? (float)s_note.age / (float)slide : 1.0f;
    if (s_note.leaving) {
        t = 1.0f - t;
    }
    t = t < 0.0f ? 0.0f : t;
    return t * t * (3.0f - 2.0f * t);
}

static int notePanel(Panel *pn)
{
    if (!s_note.on && !s_note.leaving) {
        return 0;
    }
    layoutPanel(pn, s_note.title, s_note.body, noteExtra(), TOP_Y, noteShown());
    return 1;
}

static int panel(Panel *pn)
{
    if (s_count == 0) {
        return 0;
    }
    const Popup *p = &s_queue[s_head];
    /* below the standing notice while it is up (its full height, so the
       queue's panel does not move while the notice slides) */
    float top = TOP_Y;
    Panel note;
    if (s_note.on || s_note.leaving) {
        layoutPanel(&note, s_note.title, s_note.body, noteExtra(), TOP_Y, 1.0f);
        top = note.y1 + STACK_GAP;
    }
    layoutPanel(pn, p->title, p->body, 0.0f, top, shown());
    return 1;
}

const char *ui_popup_title(void)
{
    return s_count > 0 ? s_queue[s_head].title : "";
}

const char *ui_popup_body(void)
{
    return s_count > 0 ? s_queue[s_head].body : "";
}

int ui_popup_panel(float rect[4])
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

void ui_popup_set_button_drawer(UiPopupButtonFn fn)
{
    s_button = fn;
}

void ui_popup_notice_show(const char *title, const char *body, const char *hint)
{
    if (s_note.on) {
        return; /* already up */
    }
    const unsigned int age = s_note.leaving ? (unsigned int)ui_popup_slide_vsyncs() - s_note.age
                                            : 0u; /* back in from where it was */
    copyText(s_note.title, title);
    copyText(s_note.body, body);
    copyText(s_note.hint, hint);
    s_note.on = 1;
    s_note.leaving = 0;
    s_note.age = age;
}

void ui_popup_notice_hide(void)
{
    if (!s_note.on) {
        return;
    }
    const int slide = ui_popup_slide_vsyncs();
    s_note.on = 0;
    s_note.leaving = 1;
    /* out from where it is: a notice still sliding in turns back */
    s_note.age = (int)s_note.age < slide ? (unsigned int)slide - s_note.age : 0u;
}

int ui_popup_notice_active(void)
{
    return s_note.on || s_note.leaving;
}

int ui_popup_notice_shown(void)
{
    return s_note.on;
}

int ui_popup_notice_panel(float rect[4])
{
    Panel pn;
    if (!notePanel(&pn)) {
        return 0;
    }
    rect[0] = pn.x0;
    rect[1] = pn.y0;
    rect[2] = pn.x1;
    rect[3] = pn.y1;
    return 1;
}

const char *ui_popup_notice_title(void)
{
    return ui_popup_notice_active() ? s_note.title : "";
}

const char *ui_popup_notice_line(char *out, size_t n)
{
    if (n == 0) {
        return out;
    }
    out[0] = '\0';
    if (ui_popup_notice_active()) {
        snprintf(out, n, "%s %s %s", s_note.body, ui_str(UI_STR_BTN_TRIANGLE), s_note.hint);
    }
    return out;
}

#ifdef ICO_RD
static void scaled(uint8_t out[4], uint8_t r, uint8_t g, uint8_t b, uint8_t a, float k)
{
    out[0] = r;
    out[1] = g;
    out[2] = b;
    out[3] = (uint8_t)((float)a * k + 0.5f);
}

/* a panel and its title and body (the notice: its button and word after
   the body on its line) */
static void drawPanel(const Panel *pn, const char *title, const char *body, int notice)
{
    uint8_t c[4];
    /* the panel: dark, translucent, a hairline in the menu's warm grey
       on top */
    scaled(c, 6, 6, 9, 0x5C, pn->alpha);
    ui_draw_rect(pn->x0, pn->y0, pn->x1, pn->y1, c);
    scaled(c, 0x5E, 0x58, 0x4C, 0x80, pn->alpha);
    ui_draw_rect(pn->x0, pn->y0, pn->x1, pn->y0 + 1.5f, c);
    /* in the menus' look, the same 1x strip as
       the scene's text magnified onto the output */
    scaled(c, 0x80, 0x7C, 0x70, 0x80, pn->alpha);
    ui_draw_menu_text(pn->x0 + PAD_X, pn->titleBase, TITLE_SIZE, c, title, UI_VALIGN_BASELINE,
                      UI_INK_LIGHT, NULL);
    scaled(c, 0x66, 0x64, 0x5E, 0x80, pn->alpha);
    if (body[0]) {
        ui_draw_menu_text(pn->x0 + PAD_X, pn->bodyTop, BODY_SIZE, c, body, UI_VALIGN_TOP,
                          UI_INK_LIGHT, NULL);
    }
    if (!notice) {
        return;
    }
    float ba, bd, cap;
    ui_menu_font_metrics(BODY_SIZE, &ba, &bd, &cap);
    const float mid = pn->bodyTop + ba - cap * 0.5f;
    float x = pn->x0 + PAD_X + ui_measure_menu_text(BODY_SIZE, body) + NOTE_GAP_TEXT * BODY_SIZE;
    float bw = 0.0f;
    if (s_button != NULL) {
        bw = s_button(x, mid, BODY_SIZE, 1); /* the picture or the key, where one is set */
    }
    if (bw <= 0.0f) {
        const char *word = ui_str(UI_STR_BTN_TRIANGLE);
        ui_draw_menu_text(x, mid, BODY_SIZE, c, word, UI_VALIGN_MIDDLE, UI_INK_LIGHT, NULL);
        bw = ui_measure_menu_text(BODY_SIZE, word);
    }
    x += bw + NOTE_GAP_WORD * BODY_SIZE;
    ui_draw_menu_text(x, mid, BODY_SIZE, c, s_note.hint, UI_VALIGN_MIDDLE, UI_INK_LIGHT, NULL);
}
#endif

void ui_popup_draw_overlay(const struct RdOverlayCtx *ctx)
{
#ifdef ICO_RD
    if ((s_count == 0 && !ui_popup_notice_active()) || !ctx || !ui_font_init()) {
        return;
    }
    /* on the output (font.h ui_begin_overlay), at this present;
       measured at the overlay's scale, so the panel fits its text */
    ui_begin_overlay(ctx);
    Panel pn;
    if (notePanel(&pn) && pn.alpha > 0.0f) {
        drawPanel(&pn, s_note.title, s_note.body, 1);
    }
    if (panel(&pn) && pn.alpha > 0.0f) {
        const Popup *p = &s_queue[s_head];
        drawPanel(&pn, p->title, p->body, 0);
    }
    ui_end_overlay();
#else
    (void)ctx;
#endif
}

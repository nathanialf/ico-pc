/*
 * port/ui/touch_edit_ui.c
 *
 * The Edit layout page's side of the touch layout editor (touch_edit_ui.h):
 * the host's begin and end around the page, the editor's tick with the
 * menus' pad word, the way back.
 */
#include "touch_edit_ui.h"

#include <stdio.h>
#include <string.h>

#include "input.h" /* ICO_PAD_*, ico_input_live_touch_edits */
#include "layout_ext.h"
#include "settings.h"
#include "touch_edit.h"

/* the bits of the menus' pad word (keyInput.c's logical word) that
   layout_ext.h has no name for */
#define LT_PAD_L2 0x0001
#define LT_PAD_R2 0x0002
#define LT_PAD_SELECT 0x0100

/* the menus' pad word to the editor's ICO_PAD_* word */
static const struct {
    unsigned int lt, ico;
} kPad[] = {
    {LT_PAD_L2, ICO_PAD_L2},
    {LT_PAD_R2, ICO_PAD_R2},
    {LT_PAD_L1, ICO_PAD_L1},
    {LT_PAD_R1, ICO_PAD_R1},
    {LT_PAD_TRIANGLE, ICO_PAD_TRIANGLE},
    {LT_PAD_CIRCLE, ICO_PAD_CIRCLE},
    {LT_PAD_CROSS, ICO_PAD_CROSS},
    {LT_PAD_SQUARE, ICO_PAD_SQUARE},
    {LT_PAD_SELECT, ICO_PAD_SELECT},
    {LT_PAD_START, ICO_PAD_START},
    {LT_PAD_UP, ICO_PAD_UP},
    {LT_PAD_RIGHT, ICO_PAD_RIGHT},
    {LT_PAD_DOWN, ICO_PAD_DOWN},
    {LT_PAD_LEFT, ICO_PAD_LEFT},
};

static int s_editing;        /* begun, not ended */
static int s_left;           /* ended: the page leaves once it has faded in */
static unsigned int s_armed; /* the buttons seen up since the page opened */

static unsigned int padFromFlags(unsigned int flags)
{
    unsigned int out = 0;
    for (size_t i = 0; i < sizeof(kPad) / sizeof(kPad[0]); i++) {
        if (flags & kPad[i].lt) {
            out |= kPad[i].ico;
        }
    }
    return out;
}

/* the controls of a posture the edits move, resize or hide */
static int editedCount(const IcoTouchEdits *e, int rule)
{
    int n = 0;
    if (e == NULL || rule < 0 || rule >= ICO_TOUCH_POSTURES) {
        return 0;
    }
    for (int c = 0; c < ICO_TOUCH_CTL_COUNT; c++) {
        n += e->ctl[rule][c].set != 0;
    }
    return n;
}

static const char *postureName(int rule)
{
    const char *n = ico_touch_posture_name(rule);
    return n != NULL ? n : "?";
}

static void finish(const UiTouchEditHost *h)
{
    const int rule = h->rule();
    h->end(1);
    ui_settings_touch_dirty();
    s_editing = 0;
    s_left = 1;
    fprintf(stderr, "touch: edit layout: done (%s, %d controls edited)\n", postureName(rule),
            editedCount(ico_input_live_touch_edits(), rule));
}

int ui_touch_edit_proc(int first, unsigned int flags, unsigned int held)
{
    UiTouchEditHost h;

    if (first) {
        /* each opening begins anew (the host's begin restarts an editor a
           reset left running) */
        s_left = 0;
        s_editing = 0;
    }
    /* the port screens' back, but Cross (the editor's Opacity +: the
       Japanese and Korean discs' back) */
    const unsigned int back =
        (unsigned int)lt_ext_port_back_buttons() & ~(unsigned int)LT_PAD_CROSS;
    if (!ui_settings_touch_edit_host(&h)) {
        /* the row is locked without a host, so the page is reached only by
           a switch to it (tests): nothing to edit, a back press leaves */
        if (s_editing) {
            fprintf(stderr, "touch: edit layout: the editor went away\n");
            s_editing = 0;
        }
        return s_left || (flags & back) != 0;
    }
    if (s_left) {
        return 1;
    }
    if (!s_editing) {
        /* the first call (or a page that came back without one: a reset) */
        h.begin();
        s_editing = 1;
        s_armed = 0;
        fprintf(stderr, "touch: edit layout: begin (%s, %d controls edited)\n",
                postureName(h.rule()), editedCount(ico_input_live_touch_edits(), h.rule()));
    }
    /* a button still held from the press that opened the page is not one
       for the editor until it has been let go */
    s_armed |= ~held;
    const unsigned int padHeld = padFromFlags(held & s_armed);
    struct IcoTouchEditor *ed = h.editor();
    const IcoTouchLayout *l = h.layout();
    const int r = ed == NULL  ? ICO_TOUCH_ED_LEAVE
                  : l != NULL ? ico_touch_edit_tick(ed, l, padHeld)
                              : ICO_TOUCH_ED_NONE;
    if (r == ICO_TOUCH_ED_LEAVE || (flags & back & s_armed)) {
        finish(&h);
        return 1;
    }
    return 0;
}

void ui_touch_edit_page_reset(void)
{
    s_editing = s_left = 0;
    s_armed = 0;
}

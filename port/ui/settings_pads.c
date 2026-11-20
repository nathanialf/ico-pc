/*
 * port/ui/settings_pads.c
 *
 * Options > Controls > Button pictures and the set in force once a frame
 * (settings_pads.h).
 */
#include "settings_pads.h"

#include <stdio.h>

#include "input.h"   /* ico_input_last_pad_kind */
#include "options.h" /* [input] button_pictures */
#include "pad_glyphs.h"
#include "strings.h"

_Static_assert(ICO_BUTTON_PICTURES_PLAYSTATION == ICO_PAD_GLYPHS_PLAYSTATION &&
                   ICO_BUTTON_PICTURES_XBOX == ICO_PAD_GLYPHS_XBOX &&
                   ICO_BUTTON_PICTURES_NINTENDO == ICO_PAD_GLYPHS_NINTENDO &&
                   ICO_BUTTON_PICTURES_WII_CLASSIC == ICO_PAD_GLYPHS_WII_CLASSIC &&
                   ICO_BUTTON_PICTURES_COUNT == ICO_PAD_GLYPHS_COUNT,
               "the option's numbers are the pictures' styles");

static struct {
    int style;    /* the style last resolved, -1 before the first */
    int fromAuto; /* and whether Auto chose it */
    unsigned changes;
    unsigned char refusedLogged[ICO_BUTTON_PICTURES_COUNT];
} s_pads = {-1, 0, 0, {0}};

int ui__pads_choice(void)
{
    const int c = ico_opt_button_pictures();

    if (c > ICO_BUTTON_PICTURES_PLAYSTATION && c < ICO_BUTTON_PICTURES_COUNT &&
        !pad_glyphs_set_complete(c)) {
        if (!s_pads.refusedLogged[c]) {
            s_pads.refusedLogged[c] = 1;
            fprintf(stderr, "controls: button pictures: %s is not a complete set: Auto\n",
                    pad_glyphs_style_name(c));
        }
        return ICO_BUTTON_PICTURES_AUTO;
    }
    return c;
}

/* Auto's style: the last controller's kind; Wii Classic Controller never */
static int autoStyle(void)
{
    const int k = ico_input_last_pad_kind();
    const int s = k == ICO_PAD_KIND_XBOX       ? ICO_PAD_GLYPHS_XBOX
                  : k == ICO_PAD_KIND_NINTENDO ? ICO_PAD_GLYPHS_NINTENDO
                                               : ICO_PAD_GLYPHS_PLAYSTATION;

    return pad_glyphs_set_complete(s) ? s : ICO_PAD_GLYPHS_PLAYSTATION;
}

int ui_settings_pads_frame(void)
{
    const int c = ui__pads_choice();
    const int fromAuto = c == ICO_BUTTON_PICTURES_AUTO;
    const int style = fromAuto ? autoStyle() : c;

    pad_glyphs_set_style(style);
    if (style != s_pads.style || fromAuto != s_pads.fromAuto) {
        s_pads.style = style;
        s_pads.fromAuto = fromAuto;
        s_pads.changes++;
        if (s_pads.changes <= 8 || s_pads.changes % 100 == 0) {
            fprintf(stderr, "controls: button pictures: %s (%s), change %u\n",
                    pad_glyphs_style_name(style),
                    fromAuto ? "Auto, the last pad used" : "chosen in Options", s_pads.changes);
        }
    }
    return style;
}

const char *ui__pads_text(char *buf, unsigned size)
{
    const int c = ui__pads_choice();

    if (c == ICO_BUTTON_PICTURES_AUTO) {
        snprintf(buf, size, "%s (%s)", ui_str(UI_STR_VAL_AUTO), pad_glyphs_style_name(autoStyle()));
        return buf;
    }
    return pad_glyphs_style_name(c);
}

void ui__pads_step(int dir)
{
    /* the row's order: Auto, then the styles in their order */
    enum { N = ICO_BUTTON_PICTURES_COUNT + 1 };

    int i = ui__pads_choice() + 1; /* Auto (-1) is 0 */

    for (int tries = 0; tries < N; tries++) {
        i = ((i + (dir < 0 ? -1 : 1)) % N + N) % N;
        if (i == 0 || pad_glyphs_set_complete(i - 1)) {
            break;
        }
    }
    ico_opt_set_button_pictures(i - 1);
    if (ico_opt_button_pictures_store() != 0) {
        fprintf(stderr, "controls: cannot write [input] button_pictures\n");
    }
    ui_settings_pads_frame();
}

unsigned ui__pads_changes(void)
{
    return s_pads.changes;
}

void ui__pads_reset(void)
{
    s_pads.style = -1;
    s_pads.fromAuto = 0;
    s_pads.changes = 0;
    for (int i = 0; i < ICO_BUTTON_PICTURES_COUNT; i++) {
        s_pads.refusedLogged[i] = 0;
    }
}

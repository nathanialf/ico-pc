/*
 * port/ui/settings_step.c
 *
 * Holding Left or Right on an Options value row keeps stepping it, and
 * L1/R1 move the Picture colour rows by 10 (settings_step.h).
 *
 * The repeat is keyed on pad[0].now, the held buttons, not on the game's
 * `rep` word: that fires every tick once past its delay, and the stick's
 * d-pad emulation (lt_analog2Pad) sets now and flags only.  The synthetic
 * bit goes into settingsProc's local step word, never into `flags`, so the
 * cursor moves, Back and the page handlers never repeat.
 */
#include "settings_step.h"

#include <stdio.h>
#include <string.h>

#include "layout_ext.h"
#include "settings.h"

static struct {
    int dir, item, ticks;
} s_rep;

static unsigned s_repeats, s_repeatsIdle; /* repeats made; of them held at an end */

int ui__step_repeats(int opt)
{
    switch (opt) {
    case UI_OPT_TONE_GAMMA:
    case UI_OPT_TONE_BRIGHTNESS:
    case UI_OPT_TONE_CONTRAST:
    case UI_OPT_TONE_SATURATION:
    case UI_OPT_CRT_STRENGTH:
    case UI_OPT_UPSCALE_SHARPNESS:
    case UI_OPT_EFFECT_MOTION_BLUR_STRENGTH:
    case UI_OPT_VOLUME:
    case UI_OPT_MUSIC:
    case UI_OPT_EFFECTS:
    case UI_OPT_BRIGHTNESS:
    case UI_OPT_PIT_TIME:
    case UI_OPT_MOUSE_SENS:
    case UI_OPT_MOUSE_SPEED:
        return 1;
    default:
        return 0;
    }
}

int ui__step_repeat(int first, int item, int opt, unsigned int held)
{
    const unsigned int lr = held & (LT_PAD_LEFT | LT_PAD_RIGHT);
    const int dir = lr == LT_PAD_LEFT ? -1 : lr == LT_PAD_RIGHT ? 1 : 0;

    if (first || dir == 0) {
        memset(&s_rep, 0, sizeof(s_rep));
        return 0;
    }
    if (dir == s_rep.dir && item == s_rep.item) {
        s_rep.ticks++;
    } else {
        s_rep.dir = dir;
        s_rep.item = item;
        s_rep.ticks = 0;
    }
    if (s_rep.ticks >= UI_STEP_REPEAT_DELAY &&
        (s_rep.ticks - UI_STEP_REPEAT_DELAY) % UI_STEP_REPEAT_EVERY == 0 && ui__step_repeats(opt)) {
        return dir < 0 ? LT_PAD_LEFT : LT_PAD_RIGHT;
    }
    return 0;
}

int ui__step_row(int opt, int dir, int repeat)
{
    char before[96];

    if (!repeat) {
        ui_settings_step((UiSettingsOpt)opt, dir);
        return 1;
    }
    snprintf(before, sizeof(before), "%s", ui_settings_value_text((UiSettingsOpt)opt));
    ui_settings_step((UiSettingsOpt)opt, dir);
    const int same = strcmp(before, ui_settings_value_text((UiSettingsOpt)opt)) == 0;
    s_repeatsIdle += same;
    if (++s_repeats <= 8 || s_repeats % 100 == 0) {
        fprintf(stderr, "settings: held step %u (row %d, dir %d, %s; %u at an end)\n", s_repeats,
                opt, dir, same ? "unchanged" : "changed", s_repeatsIdle);
    }
    return !same;
}

int *ui__tone_field(IcoVideoOptions *o, int opt)
{
    return opt == UI_OPT_TONE_GAMMA        ? &o->toneGamma
           : opt == UI_OPT_TONE_BRIGHTNESS ? &o->toneBrightness
           : opt == UI_OPT_TONE_CONTRAST   ? &o->toneContrast
                                           : &o->toneSaturation;
}

void ui__tone_add(IcoVideoOptions *o, int opt, int delta)
{
    int *const f = ui__tone_field(o, opt);

    *f = ico_video_tone_clamp(*f + delta);
}

int ui__tone_step(int opt, int delta)
{
    IcoVideoOptions v;

    if (opt < UI_OPT_TONE_GAMMA || opt > UI_OPT_TONE_SATURATION) {
        return 0;
    }
    ico_video_get(&v);
    if (delta == 0) {
        *ui__tone_field(&v, opt) = ICO_TONE_DEFAULT;
    } else {
        ui__tone_add(&v, opt, delta);
    }
    ico_video_set(&v);
    return 1;
}

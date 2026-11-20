/* settings_step_test.c: issue 202, stepping on Options > Picture
 * (settings_step.c), over the shared fixture (settings_fixture.h):
 * - holding Left or Right on a colour row steps it once at the press, again
 *   after UI_STEP_REPEAT_DELAY ticks and then every UI_STEP_REPEAT_EVERY;
 * - the stick's d-pad emulation (pad[0].ana) repeats the same;
 * - letting go and holding again starts at the press; the held step is
 *   held at 1 and 100; presses with gaps never repeat;
 * - a wrapping row (Filter) steps once a press however long it is held;
 * - L1 and R1 move a colour row by 10, held at 1 and 100, and do nothing on
 *   other rows; Square still puts back 50;
 * - the rows that repeat (ui__step_repeats);
 * - Mirror mode: the stick's d-pad emulation (lt_analog2Pad, the game's
 *   layout_texture.c) takes the frame's negated stick x back through
 *   ico_pad_menu_stick_x, so on the unflipped menus the stick left is Left
 *   and right is Right with the mode on and off, the stick's byte itself
 *   untouched, the d-pad as pressed, and the reflection counted.
 * CPU only.  Exit 0 on success, 1 on a failure. */
#include "settings_fixture.h"
#include "settings_step.h"

static int gamma_(void)
{
    IcoVideoOptions o;
    ico_video_get(&o);
    return o.toneGamma;
}

static void setGamma(int v)
{
    IcoVideoOptions o;
    ico_video_get(&o);
    o.toneGamma = v;
    ico_video_set(&o);
}

static int s_picL;

static void onRow(UiSettingsOpt opt)
{
    lt_ext_layout(s_picL)->curItem = ui_settings_row_of(UI_PAGE_PICTURE, opt);
    frame(0);
}

static void testHold(void)
{
    const int D = UI_STEP_REPEAT_DELAY, E = UI_STEP_REPEAT_EVERY;
    char before[64];

    useConfig("version = 1\n");
    gFlagGameClear = 0;
    stage_no = 1;
    s_picL = openPage(enterMain(1), 1, UI_PAGE_PICTURE);
    onRow(UI_OPT_TONE_GAMMA);

    /* the press alone, and the ticks before the first repeat */
    setGamma(50);
    hold(LT_PAD_RIGHT, D);
    CHECK(gamma_() == 51, "hold Right for %d ticks: 51 (%d)", D, gamma_());
    frame(0);
    /* the first repeat, then one every E ticks */
    for (int k = 0; k <= 4; k++) {
        setGamma(50);
        hold(LT_PAD_RIGHT, D + 1 + E * k);
        CHECK(gamma_() == 52 + k, "hold Right for %d ticks: %d (%d)", D + 1 + E * k, 52 + k,
              gamma_());
        frame(0);
    }
    /* just short of the next repeat */
    setGamma(50);
    hold(LT_PAD_RIGHT, D + E);
    CHECK(gamma_() == 52, "hold Right for %d ticks: 52 (%d)", D + E, gamma_());
    frame(0);
    /* Left the same way */
    setGamma(50);
    hold(LT_PAD_LEFT, D + 1 + 2 * E);
    CHECK(gamma_() == 46, "hold Left for %d ticks: 46 (%d)", D + 1 + 2 * E, gamma_());
    frame(0);
    /* let go and hold again: it starts at the press */
    setGamma(50);
    hold(LT_PAD_RIGHT, D + 1);
    frame(0);
    hold(LT_PAD_RIGHT, D);
    CHECK(gamma_() == 50 + 2 + 1, "hold, release, hold again: 53 (%d)", gamma_());
    frame(0);
    /* the other direction restarts it too */
    setGamma(50);
    hold(LT_PAD_RIGHT, D - 1);
    hold(LT_PAD_LEFT, D);
    CHECK(gamma_() == 50, "Right then Left, both short: 50 (%d)", gamma_());
    frame(0);

    /* the stick right and left emulate the d-pad: the same steps */
    setGamma(50);
    pad[0].ana[2] = 255;
    for (int i = 0; i < D + 1 + E * 2; i++) {
        frame(0);
    }
    pad[0].ana[2] = 128;
    frame(0);
    CHECK(gamma_() == 50 + 1 + 1 + 2, "stick right for %d ticks: 54 (%d)", D + 1 + E * 2, gamma_());
    setGamma(50);
    pad[0].ana[2] = 0;
    for (int i = 0; i < D; i++) {
        frame(0);
    }
    pad[0].ana[2] = 128;
    frame(0);
    CHECK(gamma_() == 49, "stick left for %d ticks: 49 (%d)", D, gamma_());

    /* held at both ends under a long hold */
    setGamma(95);
    hold(LT_PAD_RIGHT, 300);
    CHECK(gamma_() == 100, "long hold Right stops at 100 (%d)", gamma_());
    frame(0);
    setGamma(5);
    hold(LT_PAD_LEFT, 300);
    CHECK(gamma_() == 1, "long hold Left stops at 1 (%d)", gamma_());
    frame(0);

    /* a full run 1..100 takes about 6 s at 50 Hz */
    setGamma(1);
    hold(LT_PAD_RIGHT, D + E * 98 + 1);
    CHECK(gamma_() == 100, "hold from 1 reaches 100 in %d ticks (%d)", D + E * 98 + 1, gamma_());
    frame(0);

    /* presses with gaps: one step each, no repeat */
    setGamma(50);
    for (int i = 0; i < 40; i++) {
        frame(LT_PAD_RIGHT);
        frame(0);
        frame(0);
    }
    CHECK(gamma_() == 90, "40 presses with gaps: 90 (%d)", gamma_());
    for (int i = 0; i < 40; i++) {
        hold(LT_PAD_LEFT, D - 1); /* a press and a hold short of the delay */
        frame(0);
    }
    CHECK(gamma_() == 50, "40 short holds: 50 (%d)", gamma_());

    /* Square puts back 50 */
    setGamma(77);
    press(LT_PAD_SQUARE);
    CHECK(gamma_() == 50, "Square: 50 (%d)", gamma_());

    /* a wrapping row steps once a press, however long it is held */
    onRow(UI_OPT_FILTER);
    snprintf(before, sizeof(before), "%s", ui_settings_value_text(UI_OPT_FILTER));
    press(LT_PAD_RIGHT);
    {
        char one[64];
        snprintf(one, sizeof(one), "%s", ui_settings_value_text(UI_OPT_FILTER));
        CHECK(strcmp(one, before) != 0, "Filter: a press changes it (%s)", one);
        press(LT_PAD_LEFT);
        CHECK(strcmp(ui_settings_value_text(UI_OPT_FILTER), before) == 0, "Filter: back (%s)",
              ui_settings_value_text(UI_OPT_FILTER));
        hold(LT_PAD_RIGHT, 5 * D);
        frame(0);
        CHECK(strcmp(ui_settings_value_text(UI_OPT_FILTER), one) == 0,
              "Filter: held for %d ticks is still one step (%s, want %s)", 5 * D,
              ui_settings_value_text(UI_OPT_FILTER), one);
    }
}

static void testCoarse(void)
{
    onRow(UI_OPT_TONE_GAMMA);
    setGamma(50);
    press(LT_PAD_L1);
    CHECK(gamma_() == 40, "L1: 40 (%d)", gamma_());
    press(LT_PAD_R1);
    press(LT_PAD_R1);
    CHECK(gamma_() == 60, "R1 twice: 60 (%d)", gamma_());
    CHECK(strcmp(ui_settings_value_text(UI_OPT_TONE_GAMMA), "60") == 0, "the row reads 60 (%s)",
          ui_settings_value_text(UI_OPT_TONE_GAMMA));
    setGamma(95);
    press(LT_PAD_R1);
    CHECK(gamma_() == 100, "R1 at 95: held at 100 (%d)", gamma_());
    setGamma(5);
    press(LT_PAD_L1);
    CHECK(gamma_() == 1, "L1 at 5: held at 1 (%d)", gamma_());
    /* the other three colour rows */
    {
        static const UiSettingsOpt kRows[3] = {UI_OPT_TONE_BRIGHTNESS, UI_OPT_TONE_CONTRAST,
                                               UI_OPT_TONE_SATURATION};
        setGamma(50);
        for (int i = 0; i < 3; i++) {
            IcoVideoOptions o;
            onRow(kRows[i]);
            ico_video_get(&o);
            o.toneBrightness = o.toneContrast = o.toneSaturation = 50;
            ico_video_set(&o);
            press(LT_PAD_R1);
            CHECK(strcmp(ui_settings_value_text(kRows[i]), "60") == 0, "row %d R1: 60 (%s)", i,
                  ui_settings_value_text(kRows[i]));
            CHECK(gamma_() == 50, "row %d R1 leaves Gamma (%d)", i, gamma_());
        }
    }
    /* another row: L1 and R1 do nothing */
    onRow(UI_OPT_FILTER);
    {
        char t[64];
        snprintf(t, sizeof(t), "%s", ui_settings_value_text(UI_OPT_FILTER));
        press(LT_PAD_R1);
        press(LT_PAD_L1);
        CHECK(strcmp(ui_settings_value_text(UI_OPT_FILTER), t) == 0, "L1/R1 on Filter: unchanged");
    }
    CHECK(gamma_() == 50, "L1/R1 on Filter leave Gamma (%d)", gamma_());
    /* leaving the page writes the keys */
    onRow(UI_OPT_TONE_GAMMA);
    press(LT_PAD_R1);
    press(0x10); /* Triangle: back */
    CHECK(settle(ui_settings_page_layout(UI_PAGE_MAIN), 60), "Picture: back");
    {
        char p[1100];
        path(p, sizeof(p), "settings_test.toml");
        IcoToml *t = ico_toml_load(p);
        CHECK(t != NULL && ico_toml_get_int(t, "video.gamma", 0) == 60,
              "leaving Picture wrote gamma 60");
        ico_toml_free(t);
    }
}

static void testRows(void)
{
    static const int kYes[] = {UI_OPT_TONE_GAMMA,
                               UI_OPT_TONE_BRIGHTNESS,
                               UI_OPT_TONE_CONTRAST,
                               UI_OPT_TONE_SATURATION,
                               UI_OPT_CRT_STRENGTH,
                               UI_OPT_UPSCALE_SHARPNESS,
                               UI_OPT_EFFECT_MOTION_BLUR_STRENGTH,
                               UI_OPT_VOLUME,
                               UI_OPT_MUSIC,
                               UI_OPT_EFFECTS,
                               UI_OPT_BRIGHTNESS,
                               UI_OPT_PIT_TIME,
                               UI_OPT_MOUSE_SENS,
                               UI_OPT_MOUSE_SPEED};
    static const int kNo[] = {UI_OPT_PRESET, UI_OPT_FILTER,     UI_OPT_TOUCH_OPACITY,
                              UI_OPT_CRT,    UI_OPT_ANTI_ALIAS, UI_OPT_OUTPUT};

    for (size_t i = 0; i < sizeof(kYes) / sizeof(kYes[0]); i++) {
        CHECK(ui__step_repeats(kYes[i]), "row %d repeats", kYes[i]);
    }
    for (size_t i = 0; i < sizeof(kNo) / sizeof(kNo[0]); i++) {
        CHECK(!ui__step_repeats(kNo[i]), "row %d does not repeat", kNo[i]);
    }
}

/* the left stick's x byte the game holds in pad[0].ana[2] for a push of
   x (-1 left, 1 right), as scePadRead hands it out: the frame's own
   arithmetic, with Mirror mode as it is now */
static unsigned char stickByte(float x)
{
    IcoVirtualPad v;
    IcoPadFrame f;
    memset(&v, 0, sizeof(v));
    v.lx = x;
    ico_input_vpad_to_frame(&v, 0, ico_input_mirror(), &f);
    return f.lx;
}

/* the d-pad bits lt_analog2Pad sets for the stick byte x (a centred call
   before, so no bit is held over) */
static unsigned int analogFlags(unsigned char x)
{
    pad[0].now = pad[0].flags = 0;
    pad[0].ana[2] = pad[0].ana[3] = 128;
    lt_analog2Pad();
    pad[0].now = pad[0].flags = 0;
    pad[0].ana[2] = x;
    lt_analog2Pad();
    const unsigned int fl = (unsigned int)pad[0].flags & (LT_PAD_LEFT | LT_PAD_RIGHT);
    CHECK(pad[0].ana[2] == x, "lt_analog2Pad leaves the stick's byte as the frame gave it");
    pad[0].now = pad[0].flags = 0;
    pad[0].ana[2] = 128;
    lt_analog2Pad();
    pad[0].now = pad[0].flags = 0;
    return fl;
}

/* one tick of the left stick at byte x on the open page, then let go */
static void stickTap(unsigned char x)
{
    pad[0].ana[2] = x;
    frame(0);
    pad[0].ana[2] = 128;
    frame(0);
    frame(0);
}

/* Mirror mode and the menus: the frame's stick x is negated for the flipped
   3D picture, the layouts are drawn unflipped, so the stick's d-pad
   emulation (lt_analog2Pad, every layout's Left and Right: the game's own
   menus, the ending's save prompt, the card screens, these pages) takes it
   as pushed.  Mirror off: the bytes as they are, as before. */
static void testMirror(void)
{
    const unsigned int r0 = ico_pad_menu_stick_x_reflected();

    ico_opt_set_mirror(0);
    CHECK(stickByte(-1.0f) < 20 && stickByte(1.0f) >= 236, "mirror off: the frame as pushed");
    CHECK(analogFlags(stickByte(-1.0f)) == LT_PAD_LEFT, "mirror off: the stick left is Left");
    CHECK(analogFlags(stickByte(1.0f)) == LT_PAD_RIGHT, "mirror off: the stick right is Right");
    CHECK(analogFlags(128) == 0 && analogFlags(20) == 0 && analogFlags(235) == 0,
          "mirror off: no bit inside the thresholds");
    CHECK(analogFlags(19) == LT_PAD_LEFT && analogFlags(236) == LT_PAD_RIGHT,
          "mirror off: the thresholds as the game's");
    CHECK(ico_pad_menu_stick_x_reflected() == r0, "mirror off: nothing reflected");
    for (int x = 0; x < 256; x++) {
        CHECK(ico_pad_menu_stick_x((unsigned char)x) == x, "mirror off: %d as it is", x);
    }

    ico_opt_set_mirror(1);
    /* the 3D scene's view: the frame's x negated (the picture is flipped) */
    CHECK(stickByte(-1.0f) >= 236 && stickByte(1.0f) < 20, "mirror on: the frame's x negated");
    CHECK(analogFlags(stickByte(-1.0f)) == LT_PAD_LEFT, "mirror on: the stick left is Left");
    CHECK(analogFlags(stickByte(1.0f)) == LT_PAD_RIGHT, "mirror on: the stick right is Right");
    CHECK(analogFlags(stickByte(-0.9f)) == LT_PAD_LEFT &&
              analogFlags(stickByte(0.9f)) == LT_PAD_RIGHT,
          "mirror on: a push short of the end the same");
    CHECK(analogFlags(stickByte(0.0f)) == 0 && analogFlags(stickByte(0.5f)) == 0 &&
              analogFlags(stickByte(-0.5f)) == 0,
          "mirror on: no bit inside the thresholds");
    CHECK(ico_pad_menu_stick_x_reflected() > r0, "mirror on: the menus' view ran (%u)",
          ico_pad_menu_stick_x_reflected() - r0);
    for (int x = 0; x < 256; x++) {
        CHECK(ico_pad_menu_stick_x((unsigned char)x) == 255 - x, "mirror on: %d reflected", x);
    }

    /* a value row through the page (useConfig reloads the option, so the
       mode is set again once the page is open) */
    useConfig("version = 1\n");
    gFlagGameClear = 0;
    stage_no = 1;
    s_picL = openPage(enterMain(1), 1, UI_PAGE_PICTURE);
    onRow(UI_OPT_TONE_GAMMA);
    ico_opt_set_mirror(1);
    frame(0);
    {
        const unsigned int r1 = ico_pad_menu_stick_x_reflected();
        setGamma(50);
        stickTap(stickByte(1.0f));
        CHECK(gamma_() == 51, "mirror on: the stick right on Gamma: 51 (%d)", gamma_());
        setGamma(50);
        stickTap(stickByte(-1.0f));
        CHECK(gamma_() == 49, "mirror on: the stick left on Gamma: 49 (%d)", gamma_());
        CHECK(ico_pad_menu_stick_x_reflected() > r1, "mirror on: the page's ticks reflected");
        /* the d-pad is not the stick: as pressed, mirror or not */
        setGamma(50);
        press(LT_PAD_RIGHT);
        CHECK(gamma_() == 51, "mirror on: the d-pad Right on Gamma: 51 (%d)", gamma_());
        press(LT_PAD_LEFT);
        CHECK(gamma_() == 50, "mirror on: the d-pad Left on Gamma: 50 (%d)", gamma_());
    }
    ico_opt_set_mirror(0);
    frame(0);
    setGamma(50);
    stickTap(stickByte(1.0f));
    CHECK(gamma_() == 51, "mirror off again: the stick right on Gamma: 51 (%d)", gamma_());
    setGamma(50);
    stickTap(stickByte(-1.0f));
    CHECK(gamma_() == 49, "mirror off again: the stick left on Gamma: 49 (%d)", gamma_());
    pad[0].ana[2] = pad[0].ana[3] = 128;
    frame(0);
}

int main(int argc, char **argv)
{
    snprintf(s_dir, sizeof(s_dir), "%s", argc > 1 ? argv[1] : ".");
    setEnv("LC_ALL", NULL);
    setEnv("LANG", "en_GB.UTF-8");
    if (argc > 2) {
        /* the other discs: the same steps on their numbers */
        CHECK(ico_region_select(argv[2]) == 0, "the %s region", argv[2]);
        testHold();
        testCoarse();
        CHECK(ico_region_select("pal") == 0, "the European region again");
        printf("settings_step_test %s: %s\n", argv[2], failures ? "failed" : "ok");
        return failures ? 1 : 0;
    }
    testRows();
    testHold();
    testCoarse();
    testMirror();
    if (failures) {
        printf("settings_step_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("settings_step_test: ok\n");
    return 0;
}

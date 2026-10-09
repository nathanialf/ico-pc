/* settings_extra_test.c: the Settings menu's tests added from v0.4.3 on, over
 * the shared fixture (settings_fixture.h: fake tables, frame/settle/press,
 * useConfig, labelsAre, enterMain/openPage).  settings_test.c sits close to
 * the source size cap (tools/check_no_rom.sh), so new settings tests go here.
 *
 * Each v0.4.3 package appends, in this order:
 *   - above main: a comment headed "v0.4.3 <pkg>" holding ONE test
 *     function (void test<Name>(void));
 *   - in main: one call to it, under the same heading comment.
 * Adjacent blocks from different packages merge cleanly; keep both sides.
 */
#include "settings_fixture.h"

/* v0.4.3 ST-SPLIT: the fixture builds a layout and the menu opens */
static void testFixture(void)
{
    useConfig("version = 1\n");
    const int mainL = enterMain(1);
    CHECK(mainL == ui_SettingsPageLayout(UI_PAGE_MAIN), "title: the main page (%d)", mainL);
    CHECK(current_layout_id == mainL, "the main page is current");
    const int viaPause = enterMain(0);
    CHECK(viaPause == mainL, "pause menu: the main page (%d)", viaPause);
}

/* v0.4.3 I17c: the Display > Window mode row */
static int s_i17cAnswer;

static int fakeI17cMode(void)
{
    return s_i17cAnswer;
}

static int i17cDisplayHasWindowMode(void)
{
    int rows[16], opts[16];
    const int n = ui_SettingsPageRows(UI_PAGE_DISPLAY, rows, opts, NULL, 16);

    for (int i = 0; i < n; i++) {
        if (opts[i] == UI_OPT_WINDOW_MODE) {
            return 1;
        }
    }
    return 0;
}

static void testWindowMode(void)
{
    IcoVideoOptions o;

    /* the step wraps through Windowed, Borderless, Fullscreen */
    enterMain(0);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_WINDOW_MODE), "Windowed") == 0, "default: Windowed");
    ui_SettingsStep(UI_OPT_WINDOW_MODE, 1);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_WINDOW_MODE), "Borderless") == 0, "step: Borderless");
    ui_SettingsStep(UI_OPT_WINDOW_MODE, 1);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_WINDOW_MODE), "Fullscreen") == 0, "step: Fullscreen");
    ui_SettingsStep(UI_OPT_WINDOW_MODE, 1);
    ico_video_get(&o);
    CHECK(o.windowMode == ICO_WINDOW_WINDOWED, "step wraps to Windowed (%d)", o.windowMode);
    ui_SettingsStep(UI_OPT_WINDOW_MODE, -1);
    ico_video_get(&o);
    CHECK(o.windowMode == ICO_WINDOW_FULLSCREEN, "step back wraps to Fullscreen (%d)",
          o.windowMode);

    /* the window's answer is shown and stepped from */
    s_i17cAnswer = ICO_WINDOW_BORDERLESS;
    ui_SettingsSetWindowModeQuery(fakeI17cMode);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_WINDOW_MODE), "Borderless") == 0, "query: Borderless");
    ui_SettingsStep(UI_OPT_WINDOW_MODE, 1);
    ico_video_get(&o);
    CHECK(o.windowMode == ICO_WINDOW_FULLSCREEN, "step from the query's Borderless (%d)",
          o.windowMode);
    s_i17cAnswer = ICO_WINDOW_WINDOWED;
    ui_SettingsStep(UI_OPT_WINDOW_MODE, -1);
    ico_video_get(&o);
    CHECK(o.windowMode == ICO_WINDOW_FULLSCREEN, "step back from the query's Windowed (%d)",
          o.windowMode);
    ui_SettingsSetWindowModeQuery(NULL);

    /* the save writes window_mode, and fullscreen for older builds */
    enterMain(0);
    ui_SettingsStep(UI_OPT_WINDOW_MODE, 1);
    CHECK(ui_SettingsSave() == 0, "save");
    {
        char p[1100];
        IcoToml *t;

        path(p, sizeof(p), "settings_test.toml");
        t = ico_toml_load(p);
        CHECK(t != NULL, "the file");
        if (t) {
            const char *m = ico_toml_get(t, "video.window_mode");

            CHECK(m != NULL && strcmp(m, "borderless") == 0, "[video] window_mode");
            CHECK(ico_toml_get_bool(t, "video.fullscreen", 1) == 0, "[video] fullscreen false");
            ico_toml_free(t);
        }
    }

    /* the phone's window is the screen: no row there, one everywhere else */
    enterMain(0);
    CHECK(i17cDisplayHasWindowMode(), "the row shows");
    ico_video_set_android(1);
    CHECK(!i17cDisplayHasWindowMode(), "Android: the row is hidden");
    ico_video_set_android(0);
    CHECK(i17cDisplayHasWindowMode(), "the row shows again");
}

/* v0.4.3 R27: Effects > Cinematic bars, the last switch before Back, On by
   default, a step flips it live and ui_SettingsSave writes the key; the other
   effects keep their value */
static void testCinematicBars(void)
{
    int rows[16], opts[16];
    char p[1100];

    useConfig("version = 1\n");
    enterMain(0);
    const int n = ui_SettingsPageRows(UI_PAGE_EFFECTS, rows, opts, NULL, 16);
    CHECK(n >= 2 && opts[n - 1] == UI_OPT_BACK && opts[n - 2] == UI_OPT_EFFECT_CINEMATIC_BARS &&
              opts[n - 3] == UI_OPT_EFFECT_FOG,
          "bars: the row sits after Fog, before Back (%d rows)", n);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_EFFECT_CINEMATIC_BARS), "On") == 0,
          "bars: On by default");
    CHECK(ico_video_effect_cinematic_bars() == 1, "bars: the getter reads On");
    ui_SettingsStep(UI_OPT_EFFECT_CINEMATIC_BARS, 1);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_EFFECT_CINEMATIC_BARS), "Off") == 0, "bars: Off");
    CHECK(ico_video_effect_cinematic_bars() == 0, "bars: the getter reads Off at once");
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_EFFECT_FOG), "On") == 0, "bars: Fog untouched");
    CHECK(ui_SettingsSave() == 0, "bars: save");
    path(p, sizeof(p), "settings_test.toml");
    IcoToml *t = ico_toml_load(p);
    CHECK(t != NULL, "bars: config");
    if (t) {
        CHECK(ico_toml_get_bool(t, "video.effect_cinematic_bars", 1) == 0,
              "bars: [video] effect_cinematic_bars false");
        ico_toml_free(t);
    }
    ui_SettingsStep(UI_OPT_EFFECT_CINEMATIC_BARS, -1);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_EFFECT_CINEMATIC_BARS), "On") == 0, "bars: On again");
    useConfig("version = 1\n");
}

/* v0.4.3 AN-20: default_item_select hands the select callback the current
   item (issue 20: it passed nothing, and la_mc_saved_file_select indexed the
   card's file table with whatever the argument register held) */
static int s_an20Calls, s_an20Arg, s_an20Ret;

static int an20Spy(int item)
{
    s_an20Calls++;
    s_an20Arg = item;
    return s_an20Ret;
}

static void testItemSelectArg(void)
{
    /* the vibration screen's layout: rows 44 and 45 are its own, no proc */
    const int layout = 9;
    const int items[3] = {44, 45, 46};

    fakeTables();
    lt_ext_Reset();
    init_layout_texture(2);
    settle(54, 4);
    lt_switch_layout(layout);
    CHECK(settle(layout, 60), "item select: the layout settles (%d)", current_layout_id);
    for (int i = 0; i < 3; i++) {
        const int item = items[i];
        const int ret = items[(i + 1) % 3];

        texLayout[layout].curItem = item;
        frame(0); /* no callback: ltCurrentItem follows curItem */
        CHECK(texLayout[layout].curItem == item, "item select: idle frame keeps %d", item);
        const int before = lt_current_property_item();
        CHECK(before == item && before != layout, "item select: current item %d (%d)", item,
              before);
        s_an20Calls = 0;
        s_an20Arg = -12345;
        s_an20Ret = ret;
        lt_set_item_select_func(an20Spy);
        frame(0);
        CHECK(s_an20Calls == 1, "item select: the spy ran once (%d)", s_an20Calls);
        CHECK(s_an20Arg == before, "item select: the spy got %d, not %d", s_an20Arg, before);
        CHECK(texLayout[layout].curItem == ret, "item select: curItem is the spy's %d (%d)", ret,
              texLayout[layout].curItem);
        frame(0);
        CHECK(s_an20Calls == 1, "item select: the callback was cleared (%d calls)", s_an20Calls);
    }
}

int main(int argc, char **argv)
{
    snprintf(s_dir, sizeof(s_dir), "%s", argc > 1 ? argv[1] : ".");
    setEnv("LC_ALL", NULL);
    setEnv("LANG", "en_GB.UTF-8");
    /* v0.4.3 ST-SPLIT */
    testFixture();
    /* v0.4.3 I17c */
    testWindowMode();
    /* v0.4.3 R27 */
    testCinematicBars();
    /* v0.4.3 AN-20 */
    testItemSelectArg();
    if (failures) {
        printf("settings_extra_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("settings_extra_test: ok\n");
    return 0;
}

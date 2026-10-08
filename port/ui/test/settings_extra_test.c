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

int main(int argc, char **argv)
{
    snprintf(s_dir, sizeof(s_dir), "%s", argc > 1 ? argv[1] : ".");
    setEnv("LC_ALL", NULL);
    setEnv("LANG", "en_GB.UTF-8");
    /* v0.4.3 ST-SPLIT */
    testFixture();
    /* v0.4.3 I17c */
    testWindowMode();
    if (failures) {
        printf("settings_extra_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("settings_extra_test: ok\n");
    return 0;
}

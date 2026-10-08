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

int main(int argc, char **argv)
{
    snprintf(s_dir, sizeof(s_dir), "%s", argc > 1 ? argv[1] : ".");
    setEnv("LC_ALL", NULL);
    setEnv("LANG", "en_GB.UTF-8");
    /* v0.4.3 ST-SPLIT */
    testFixture();
    if (failures) {
        printf("settings_extra_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("settings_extra_test: ok\n");
    return 0;
}

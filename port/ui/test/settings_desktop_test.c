/* settings_desktop_test.c: Options > Extras > Add to the applications menu
 * (settings_desktop.c, ui_settings_set_desktop_handler), over the shared
 * fixture (settings_fixture.h) with a fake handler:
 * - no handler: the row is hidden; a handler on the pause menu: hidden; on
 *   the title: shown, right after Add to Steam, with its words, Down from
 *   Add to Steam lands on it;
 * - Cross: the handler once, its answer as the popup (the done one, the
 *   failed one, an unknown answer as failed), the page kept;
 * - the rows' strings translated in every built-in language;
 * - the other discs (argv[2]): the row on their numbers and their confirm
 *   button.
 * CPU only.  Exit 0 on success, 1 on a failure. */
#include "settings_fixture.h"
#include "popup.h"

static int s_calls, s_answer, s_steamCalls;

static int fakeDesktop(void)
{
    s_calls++;
    return s_answer;
}

static int fakeSteam(void)
{
    s_steamCalls++;
    return UI_STEAM_ADDED;
}

static int shown(UiSettingsOpt opt)
{
    const int row = ui_settings_row_of(UI_PAGE_EXTRAS, opt);
    return row >= 0 && ui_settings_row_shown(row);
}

static void testRow(void)
{
    static const struct {
        int answer;
        const char *title;
    } kAnswers[] = {{UI_DESKTOP_DONE, "Added to the menu"},
                    {UI_DESKTOP_FAILED, "Could not add to the menu"},
                    {99, "Could not add to the menu"}};

    int mainL, exL, row;

    ui_popup_reset();
    ui_settings_set_steam_handler(NULL);
    ui_settings_set_desktop_handler(NULL);

    /* no handler (every other platform): the row is hidden */
    mainL = enterMain(1);
    exL = openPage(mainL, -1, UI_PAGE_EXTRAS);
    CHECK(ui_settings_row_of(UI_PAGE_EXTRAS, UI_OPT_EXTRAS_DESKTOP) >= 0 &&
              !shown(UI_OPT_EXTRAS_DESKTOP),
          "no handler: the row is hidden");

    /* a handler, on the pause menu: still hidden */
    ui_settings_set_desktop_handler(fakeDesktop);
    mainL = enterMain(0);
    exL = openPage(mainL, -1, UI_PAGE_EXTRAS);
    CHECK(!shown(UI_OPT_EXTRAS_DESKTOP), "pause menu: the row is hidden");

    /* a handler, on the title: shown, Down from Add to Steam lands on it */
    ui_settings_set_steam_handler(fakeSteam);
    mainL = enterMain(1);
    exL = openPage(mainL, -1, UI_PAGE_EXTRAS);
    row = ui_settings_row_of(UI_PAGE_EXTRAS, UI_OPT_EXTRAS_DESKTOP);
    CHECK(shown(UI_OPT_EXTRAS_DESKTOP), "title: the row shows");
    CHECK(strcmp(lt_ext_row_text(row), "Add to the applications menu") == 0,
          "the row's text \"%s\"", lt_ext_row_text(row));
    {
        int el[12];
        const int en = ui_settings_page_rows(UI_PAGE_EXTRAS, el, NULL, NULL, 12);

        CHECK(en == 12 && el[5] == ui_settings_row_of(UI_PAGE_EXTRAS, UI_OPT_EXTRAS_STEAM) &&
                  el[6] == row,
              "twelve rows, the row right after Add to Steam (%d)", en);
        lt_ext_layout(exL)->curItem = el[5];
        frame(0);
        press(0x4000);
        CHECK(lt_ext_layout(exL)->curItem == row, "Down from Add to Steam lands on it");
    }
    ui_settings_set_steam_handler(NULL);

    /* Cross: the handler once, its answer as the popup, the page kept */
    for (unsigned i = 0; i < sizeof(kAnswers) / sizeof(kAnswers[0]); i++) {
        ui_popup_reset();
        s_calls = 0;
        s_answer = kAnswers[i].answer;
        lt_ext_layout(exL)->curItem = row;
        frame(0);
        press(0x40);
        CHECK(s_calls == 1, "answer %d: the handler ran once (%d)", kAnswers[i].answer, s_calls);
        CHECK(ui_popup_active() && strcmp(ui_popup_title(), kAnswers[i].title) == 0,
              "answer %d: popup \"%s\"", kAnswers[i].answer, ui_popup_title());
        CHECK(current_layout_id == exL, "answer %d: stays on the page", kAnswers[i].answer);
    }
    CHECK(s_steamCalls == 0, "Add to Steam was not run");
    ui_popup_reset();
    ui_settings_set_desktop_handler(NULL);
}

static void testStrings(void)
{
    static const UiStrId kIds[] = {UI_STR_EXTRAS_DESKTOP, UI_STR_DESKTOP_ADDED,
                                   UI_STR_DESKTOP_ADDED_BODY, UI_STR_DESKTOP_FAILED,
                                   UI_STR_DESKTOP_FAILED_BODY};

    for (int l = 1; l < UI_LANG_COUNT; l++) {
        for (unsigned i = 0; i < sizeof(kIds) / sizeof(kIds[0]); i++) {
            const char *t = ui_str_in((UiLang)l, kIds[i]);

            CHECK(t[0] != '\0' && strcmp(t, ui_str_in(UI_LANG_EN, kIds[i])) != 0,
                  "language %d, string %d translated", l, kIds[i]);
        }
    }
}

static void testOtherDisc(const char *slug)
{
    int mainL, exL;

    CHECK(ico_region_select(slug) == 0, "the %s region", slug);
    ui_popup_reset();
    ui_settings_set_desktop_handler(fakeDesktop);
    s_calls = 0;
    s_answer = UI_DESKTOP_DONE;
    mainL = enterMain(1);
    exL = openPage(mainL, -1, UI_PAGE_EXTRAS);
    CHECK(shown(UI_OPT_EXTRAS_DESKTOP), "%s: the row shows", slug);
    lt_ext_layout(exL)->curItem = ui_settings_row_of(UI_PAGE_EXTRAS, UI_OPT_EXTRAS_DESKTOP);
    frame(0);
    press(lt_ext_confirm_button());
    CHECK(s_calls == 1 && ui_popup_active(), "%s: the disc's confirm runs it", slug);
    ui_popup_reset();
    ui_settings_set_desktop_handler(NULL);
    CHECK(ico_region_select("pal") == 0, "the European region again");
}

int main(int argc, char **argv)
{
    snprintf(s_dir, sizeof(s_dir), "%s", argc > 1 ? argv[1] : ".");
    setEnv("LC_ALL", NULL);
    setEnv("LANG", "en_GB.UTF-8");
    if (argc > 2) {
        testOtherDisc(argv[2]);
        printf("settings_desktop_test %s: %s\n", argv[2], failures ? "failed" : "ok");
        return failures ? 1 : 0;
    }
    testRow();
    testStrings();
    if (failures) {
        printf("settings_desktop_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("settings_desktop_test: ok\n");
    return 0;
}

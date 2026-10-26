/* settings_discs_test.c: the Options rows each disc has, over the shared
 * fixture (settings_fixture.h), one disc per run: no argument the European
 * disc, else the slug (ctests settings_discs, settings_discs_<slug> for the
 * other six).  It holds the per-disc matrix of docs/port/GAME_FEATURES.md
 * ("Options on each disc"): which rows a disc shows, from the title and
 * from the pause menu, by the region table field that decides it (and the
 * Hold type row's starting value, the disc's own).  The
 * expectations are written out per disc (kExpect), not read from the
 * table, so a change to a field is a change made here on purpose.  CPU
 * only. */
#include "settings_fixture.h"

typedef struct {
    const char *slug;
    int hz50;       /* 50 Hz (Display, title only): has_50hz_row */
    int language;   /* Game language (Main): has_game_language */
    int circleBack; /* Circle goes back (Controls): pad_back is Triangle */
    /* Film effect (Gameplay, the pause menu, once cleared): has_film_effect;
       Players (the same page and rule) and Hold type (Controls) show on every
       disc */
    int film;
    /* the disc's own Hold type, what the row starts at (no key): 0 A, 1 B
       (hold_type_own) */
    int holdOwn;
    int credits;   /* Extras > Credits (title): credits_staff_packs */
    int debugMenu; /* Extras > Game variants > Debug menu (Developer mode): not jun26 */
    int startFlag; /* the New Game screen's start flag: gflag_new_game */
} Expect;

static const Expect kExpect[] = {
    {"pal", 1, 1, 1, 1, 0, 1, 1, 382},  {"jpn", 0, 0, 0, 1, 0, 1, 1, 382},
    {"kor", 0, 0, 0, 1, 0, 1, 1, 382},  {"ntsc", 0, 0, 1, 0, 1, 1, 1, 349},
    {"aug6", 0, 0, 1, 0, 1, 1, 1, 348}, {"jun26", 0, 0, 1, 0, 1, 0, 0, 333},
    {"cn", 0, 0, 0, 1, 0, 1, 1, 382},
};

static const Expect *expectFor(const char *slug)
{
    for (unsigned i = 0; i < sizeof(kExpect) / sizeof(kExpect[0]); i++) {
        if (strcmp(kExpect[i].slug, slug) == 0) {
            return &kExpect[i];
        }
    }
    return NULL;
}

/* the rows every disc has, from both entries */
static const UiSettingsOpt kEveryDisc[] = {
    UI_OPT_MENU_LANGUAGE, UI_OPT_BRIGHTNESS,   UI_OPT_VIBRATION, UI_OPT_STICK_FIX, UI_OPT_YORDA,
    UI_OPT_DOOR_FIX,      UI_OPT_SKIP_OPENING, UI_OPT_OUTPUT,    UI_OPT_DEVELOPER,
};

/* the rows the title shows and the pause menu does not (on every disc) */
static void checkTitleRows(const Expect *e)
{
    useConfig("version = 1\n[ui]\nlanguage = \"en\"\n");
    stage_no = 1;
    gFlagGameClear = 1; /* the game rows' rule is the pause menu's, not the clear */
    enterMainKeep(1);
    CHECK(ui_settings_opt_shown(UI_OPT_VIDEO_MODE) == e->hz50, "%s title: 50 Hz %d, want %d",
          e->slug, ui_settings_opt_shown(UI_OPT_VIDEO_MODE), e->hz50);
    CHECK(ui_settings_opt_shown(UI_OPT_LANGUAGE) == e->language,
          "%s title: Game language %d, want %d", e->slug, ui_settings_opt_shown(UI_OPT_LANGUAGE),
          e->language);
    CHECK(ui_settings_opt_shown(UI_OPT_CIRCLE_BACK) == e->circleBack,
          "%s title: Circle goes back %d, want %d", e->slug,
          ui_settings_opt_shown(UI_OPT_CIRCLE_BACK), e->circleBack);
    CHECK(ui_settings_opt_shown(UI_OPT_HOLD_TYPE), "%s title: Hold type", e->slug);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_HOLD_TYPE),
                 e->holdOwn ? "Disc’s own (B)" : "Disc’s own (A)") == 0 &&
              optionControlType == e->holdOwn,
          "%s title: Hold type %s (%d), want the disc's own %d", e->slug,
          ui_settings_value_text(UI_OPT_HOLD_TYPE), optionControlType, e->holdOwn);
    /* the game's own rows are the pause menu's */
    CHECK(!ui_settings_opt_shown(UI_OPT_FILM_EFFECT) && !ui_settings_opt_shown(UI_OPT_PLAYERS) &&
              !ui_settings_opt_shown(UI_OPT_BRIGHTNESS),
          "%s title: no Film effect, Players or Brightness", e->slug);
    CHECK(ui_settings_opt_shown(UI_OPT_EXTRAS_MUSIC) && ui_settings_opt_shown(UI_OPT_EXTRAS_MODELS),
          "%s title: Extras > Music and Models", e->slug);
    CHECK(ui_settings_opt_shown(UI_OPT_EXTRAS_CREDITS) == e->credits,
          "%s title: Extras > Credits %d, want %d", e->slug,
          ui_settings_opt_shown(UI_OPT_EXTRAS_CREDITS), e->credits);
    CHECK(ui_settings_opt_shown(UI_OPT_MENU_LANGUAGE) && ui_settings_opt_shown(UI_OPT_STICK_FIX) &&
              ui_settings_opt_shown(UI_OPT_SKIP_OPENING) && ui_settings_opt_shown(UI_OPT_YORDA),
          "%s title: Menu language, stick fix, Skip the opening, Yorda", e->slug);
    ico_opt_set_developer_mode(1);
    CHECK(ui_settings_opt_shown(UI_OPT_VAR_DEBUG_MENU) == e->debugMenu,
          "%s: the Debug menu row %d, want %d", e->slug,
          ui_settings_opt_shown(UI_OPT_VAR_DEBUG_MENU), e->debugMenu);
    ico_opt_set_developer_mode(0);
    CHECK(!ui_settings_opt_shown(UI_OPT_VAR_DEBUG_MENU),
          "%s: no Debug menu row without Developer mode", e->slug);
}

static void checkPauseRows(const Expect *e)
{
    useConfig("version = 1\n[ui]\nlanguage = \"en\"\n");
    stage_no = 11;
    gFlagGameClear = 1;
    const int mainL = enterMainKeep(0);
    for (unsigned i = 0; i < sizeof(kEveryDisc) / sizeof(kEveryDisc[0]); i++) {
        CHECK(ui_settings_opt_shown(kEveryDisc[i]), "%s pause: row %d shown", e->slug,
              (int)kEveryDisc[i]);
    }
    CHECK(!ui_settings_opt_shown(UI_OPT_VIDEO_MODE) &&
              !ui_settings_opt_shown(UI_OPT_EXTRAS_MUSIC) &&
              !ui_settings_opt_shown(UI_OPT_EXTRAS_CREDITS),
          "%s pause: no 50 Hz, Music or Credits (the title's)", e->slug);
    CHECK(ui_settings_opt_shown(UI_OPT_HOLD_TYPE) &&
              ui_settings_opt_shown(UI_OPT_FILM_EFFECT) == e->film &&
              ui_settings_opt_shown(UI_OPT_PLAYERS),
          "%s pause, cleared: Hold type %d (want 1), Film effect %d (want %d), Players %d (want 1)",
          e->slug, ui_settings_opt_shown(UI_OPT_HOLD_TYPE),
          ui_settings_opt_shown(UI_OPT_FILM_EFFECT), e->film,
          ui_settings_opt_shown(UI_OPT_PLAYERS));
    /* a row that is not shown is not reached: on the page as it opens, its
       row is masked (layoutPage) */
    openPage(mainL, -1, UI_PAGE_GAMEPLAY);
    const int film = ui_settings_row_of(UI_PAGE_GAMEPLAY, UI_OPT_FILM_EFFECT);
    CHECK(film >= 0 && lt_ext_prop(film)->defaultMask == !e->film,
          "%s pause: the Film effect row masked %d, want %d", e->slug,
          film >= 0 ? lt_ext_prop(film)->defaultMask : -1, !e->film);
    /* Players is reached on every disc once cleared (the 2001 discs too:
       the port carries the second-pad Yorda into their arms) */
    const int players = ui_settings_row_of(UI_PAGE_GAMEPLAY, UI_OPT_PLAYERS);
    CHECK(players >= 0 && lt_ext_prop(players)->defaultMask == 0,
          "%s pause: the Players row masked %d, want 0", e->slug,
          players >= 0 ? lt_ext_prop(players)->defaultMask : -1);
    gFlagGameClear = 0;
    CHECK(!ui_settings_opt_shown(UI_OPT_FILM_EFFECT) && !ui_settings_opt_shown(UI_OPT_PLAYERS),
          "%s pause, not cleared: no Film effect or Players", e->slug);
}

int main(int argc, char **argv)
{
    snprintf(s_dir, sizeof(s_dir), "%s", argc > 1 ? argv[1] : ".");
    setEnv("LC_ALL", NULL);
    setEnv("LANG", "en_GB.UTF-8");
    const char *slug = argc > 2 ? argv[2] : "pal";
    const Expect *e = expectFor(slug);
    CHECK(e != NULL && ico_region_select(slug) == 0, "the %s disc", slug);
    if (e != NULL) {
        CHECK(ico_region_cur()->has_film_effect == e->film &&
                  ico_region_cur()->hold_type_own == e->holdOwn &&
                  ico_region_cur()->credits_staff_packs == e->credits &&
                  ico_region_cur()->has_50hz_row == e->hz50 &&
                  ico_region_cur()->has_game_language == e->language &&
                  ico_region_cur()->gflag_new_game == e->startFlag,
              "%s: the region row's fields as the matrix says", slug);
        checkTitleRows(e);
        checkPauseRows(e);
    }
    gFlagGameClear = 0;
    ico_opt_set_ui_language(-1);
    CHECK(ico_region_select("pal") == 0, "the European disc again");
    printf("settings_discs_test %s: %s\n", slug, failures ? "failed" : "ok");
    return failures ? 1 : 0;
}

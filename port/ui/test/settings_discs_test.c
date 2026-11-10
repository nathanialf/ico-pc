/* settings_discs_test.c: the Options rows each disc has, over the shared
 * fixture (settings_fixture.h), one disc per run: no argument the European
 * disc, else the slug (ctests settings_discs, settings_discs_<slug> for the
 * other six).  It holds the per-disc matrix of docs/port/GAME_FEATURES.md
 * ("Options on each disc"): which rows a disc shows, from the title and
 * from the pause menu, by the region table field that decides it (and the
 * Hold type row's starting value, the disc's own), and the New Game
 * draft's rows on Customize's pages (every one on every disc, Hard's Shadow
 * toughness, the toughness values the disc offers).  The
 * expectations are written out per disc (kExpect), not read from the
 * table, so a change to a field is a change made here on purpose.  CPU
 * only. */
#include "settings_fixture.h"

#include "ico_unused.h"
#include "settings_difficulty.h" /* the New Game draft */

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
    int debugMenu; /* Developer > Debug menu (Developer mode): not jun26 */
    int startFlag; /* the New Game screen's start flag: gflag_new_game */
    /* Extras > Unused content (title): the arena, the climbing room, the
       attract movie, the early show art, and the page's link */
    int unused[4];
    int unusedPage;
    /* Gameplay > Light sword colour (both entries): the disc has the
       light sword's kinds 8 and 9 (ico_variant_offered); Stick on every disc */
    int lightSword;
    /* Controls > Remap controls > Ask Yorda for a hint: the discs with the
       hint system (not the North American disc or the 2001 builds) */
    int hints;
    /* Hard's Shadow toughness (plan "Presets"); whether the disc offers
       American (not the June 2001 demo) and Third journey (not the 2001
       discs) on Customize > Shadows */
    int hardTough;
    int american, third;
} Expect;

static const Expect kExpect[] = {
    {"pal", 1, 1, 1, 1, 0, 1, 1, 382, {1, 1, 1, 1}, 1, 1, 1, ICO_TOUGHNESS_THIRD_JOURNEY, 1, 1},
    {"jpn", 0, 0, 0, 1, 0, 1, 1, 382, {1, 1, 0, 1}, 1, 1, 1, ICO_TOUGHNESS_THIRD_JOURNEY, 1, 1},
    {"kor", 0, 0, 0, 1, 0, 1, 1, 382, {1, 1, 0, 1}, 1, 1, 1, ICO_TOUGHNESS_THIRD_JOURNEY, 1, 1},
    {"ntsc", 0, 0, 1, 0, 1, 1, 1, 349, {1, 1, 0, 1}, 1, 0, 0, ICO_TOUGHNESS_AMERICAN, 1, 0},
    {"aug6", 0, 0, 1, 0, 1, 1, 1, 348, {1, 1, 0, 1}, 1, 0, 0, ICO_TOUGHNESS_AMERICAN, 1, 0},
    {"jun26", 0, 0, 1, 0, 1, 0, 0, 333, {0, 0, 0, 0}, 0, 0, 0, ICO_TOUGHNESS_ORIGINAL, 0, 0},
    {"cn", 0, 0, 0, 1, 0, 1, 1, 382, {1, 1, 0, 1}, 1, 1, 1, ICO_TOUGHNESS_THIRD_JOURNEY, 1, 1},
};

/* Extras > Unused content through the program's CPU side (unused_content.c:
   the row's stage on this disc by ico_region_stage, asked once per disc)
   over a fake engine for DATA.DF: the June 2001 demo's holds none of the
   rows' data (region_jun26.c's 49 entries), every other disc's answers 1
   for every row it is asked about, so the 104-row discs' movie is hidden
   by the stage mapping (no memoryPAL576 row) before the engine is asked */
static const char *s_discSlug;
static int s_engineAsked[4];

static int fakeEngineAvailable(int row)
{
    if (row >= 0 && row < 4) {
        s_engineAsked[row]++;
    }
    return strcmp(s_discSlug, "jun26") != 0;
}

static int fakeEngineBegin(int row)
{
    (void)row;
    return -1;
}

static int hostAvailable(int row)
{
    return ico_unused_available(row);
}

static int hostOpen(int row)
{
    return ico_unused_start(row);
}

static void checkUnusedRows(const Expect *e)
{
    static const IcoUnusedEngine kEngine = {fakeEngineAvailable, fakeEngineBegin};
    static const UiUnusedHost kHost = {hostAvailable, hostOpen};
    static const UiSettingsOpt kRows[4] = {UI_OPT_UNUSED_ARENA, UI_OPT_UNUSED_CLIMB,
                                           UI_OPT_UNUSED_MOVIE, UI_OPT_UNUSED_ART};
    s_discSlug = e->slug;
    memset(s_engineAsked, 0, sizeof(s_engineAsked));
    ico_unused_set_engine(&kEngine);
    ui_settings_set_unused_host(&kHost);
    useConfig("version = 1\n[ui]\nlanguage = \"en\"\n");
    stage_no = 1;
    const int mainL = enterMainKeep(1);
    const int exL = openPage(mainL, -1, UI_PAGE_EXTRAS);
    for (int k = 0; k < 4; k++) {
        frame(0);
    }
    for (int r = 0; r < 4; r++) {
        CHECK(ui_settings_opt_shown(kRows[r]) == e->unused[r],
              "%s title: Unused content row %d %d, want %d", e->slug, r,
              ui_settings_opt_shown(kRows[r]), e->unused[r]);
        CHECK(s_engineAsked[r] <= 1, "%s: row %d asked of the engine %d times (once per disc)",
              e->slug, r, s_engineAsked[r]);
    }
    CHECK(ico_unused_stage(ICO_UNUSED_MOVIE) > 0 || s_engineAsked[ICO_UNUSED_MOVIE] == 0,
          "%s: no movie stage, the engine not asked (%d)", e->slug,
          s_engineAsked[ICO_UNUSED_MOVIE]);
    int el[12];
    const int n = ui_settings_page_rows(UI_PAGE_EXTRAS, el, NULL, NULL, 12);
    CHECK(n == 12 && ui_settings_row_shown(el[4]) == e->unusedPage &&
              lt_ext_prop(el[4])->defaultMask == !e->unusedPage,
          "%s title: the Unused content link shown %d, want %d", e->slug,
          n == 12 ? ui_settings_row_shown(el[4]) : -1, e->unusedPage);
    if (e->unusedPage) {
        openLink(exL, UI_PAGE_EXTRAS, UI_PAGE_UNUSED);
        for (int k = 0; k < 4; k++) {
            frame(0);
        }
        checkPageFits(UI_PAGE_UNUSED, "Unused content");
    }
    /* the pause menu: never */
    stage_no = 11;
    enterMainKeep(0);
    for (int r = 0; r < 4; r++) {
        CHECK(!ui_settings_opt_shown(kRows[r]), "%s pause: no Unused content row %d", e->slug, r);
    }
    ui_settings_set_unused_host(NULL);
    ico_unused_set_engine(NULL);
}

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
    UI_OPT_MENU_LANGUAGE, UI_OPT_BRIGHTNESS,  UI_OPT_VIBRATION, UI_OPT_STICK_FIX, UI_OPT_DOOR_FIX,
    UI_OPT_SKIP_OPENING,  UI_OPT_SECOND_CARD, UI_OPT_OUTPUT,    UI_OPT_DEVELOPER, UI_OPT_VAR_WEAPON,
};

/* the New Game draft's rows (Customize's three pages), every one on every
   disc; Hard's toughness the disc's; the toughness row steps only through
   the values the disc offers (from Original: American where offered, Not
   tougher later, Third journey where offered, Original again); the demo's
   own toughness note, the 2001 discs' No hints note */
static void checkDraftRows(const Expect *e)
{
    static const UiSettingsPage kPages[3] = {UI_PAGE_SHADOWS, UI_PAGE_COMBAT, UI_PAGE_RULES};
    IcoDifficulty hard;
    int total = 0;

    useConfig("version = 1\n[ui]\nlanguage = \"en\"\n");
    stage_no = 1;
    enterMainKeep(1);
    for (int p = 0; p < 3; p++) {
        int lb[16], op[16];
        const int n = ui_settings_page_rows(kPages[p], lb, op, NULL, 16);
        for (int i = 0; i < n; i++) {
            if (op[i] != UI_OPT_BACK) {
                total++;
                CHECK(ui_settings_row_shown(lb[i]), "%s: Customize page %d row %d shown", e->slug,
                      kPages[p], i);
            }
        }
    }
    CHECK(total == ICO_DIFF_ROW_COUNT, "%s: every draft row on a page (%d of %d)", e->slug, total,
          ICO_DIFF_ROW_COUNT);
    ico_difficulty_preset(ICO_DIFF_HARD, &hard);
    CHECK(hard.row[ICO_DIFF_SHADE_TOUGHNESS] == e->hardTough, "%s: Hard's toughness %d, want %d",
          e->slug, (int)hard.row[ICO_DIFF_SHADE_TOUGHNESS], e->hardTough);
    ui__diff_draft_start(NULL);
    static const char *const kTough[4] = {"Original", "American", "Not tougher later",
                                          "Third journey"};
    int seen[4] = {0, 0, 0, 0};
    for (int k = 0; k < 4; k++) {
        ui_settings_step(UI_OPT_VAR_TOUGHNESS, 1);
        for (int v = 0; v < 4; v++) {
            seen[v] += strcmp(ui_settings_value_text(UI_OPT_VAR_TOUGHNESS), kTough[v]) == 0;
        }
    }
    CHECK(seen[0] >= 1 && seen[2] >= 1 && (seen[1] > 0) == e->american && (seen[3] > 0) == e->third,
          "%s: the toughness values offered (American %d, Third journey %d)", e->slug, seen[1],
          seen[3]);
    CHECK(ui__diff_note(UI_OPT_VAR_TOUGHNESS) ==
                  (ICO_JUN26 ? UI_STR_OPT_VAR_TOUGHNESS_NOTE_DEMO
                   : ui__diff_draft()->row[ICO_DIFF_SHADE_TOUGHNESS] == ICO_TOUGHNESS_THIRD_JOURNEY
                       ? UI_STR_OPT_VAR_TOUGHNESS_NOTE_THIRD
                       : UI_STR_OPT_VAR_TOUGHNESS_NOTE) &&
              ui__diff_note(UI_OPT_VAR_NO_HINTS) ==
                  (e->hints ? UI_STR_OPT_VAR_NO_HINTS_NOTE : UI_STR_OPT_VAR_NO_HINTS_NOTE_2001),
          "%s: the disc's toughness and No hints notes", e->slug);
    ui__diff_draft_start(NULL);
}

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
              ui_settings_opt_shown(UI_OPT_SKIP_OPENING) && ui_settings_opt_shown(UI_OPT_YORDA) &&
              ui_settings_opt_shown(UI_OPT_SECOND_CARD),
          "%s title: Menu language, stick fix, Skip the opening, Yorda, Second memory card",
          e->slug);
    CHECK(ui_settings_opt_shown(UI_OPT_VAR_SWORD_COLOUR) == e->lightSword &&
              ui_settings_opt_shown(UI_OPT_VAR_WEAPON),
          "%s title: Light sword colour %d (want %d), Stick %d (want 1)", e->slug,
          ui_settings_opt_shown(UI_OPT_VAR_SWORD_COLOUR), e->lightSword,
          ui_settings_opt_shown(UI_OPT_VAR_WEAPON));
    ico_opt_set_developer_mode(1);
    CHECK(ui_settings_opt_shown(UI_OPT_VAR_DEBUG_MENU) == e->debugMenu,
          "%s: the Debug menu row %d, want %d", e->slug,
          ui_settings_opt_shown(UI_OPT_VAR_DEBUG_MENU), e->debugMenu);
    /* Developer > Free movement: Developer mode's, on every disc (all four
       boy control loops carry the game's debug move) */
    CHECK(ui_settings_opt_shown(UI_OPT_FREE_MOVEMENT),
          "%s: the Free movement row with Developer mode", e->slug);
    ico_opt_set_developer_mode(0);
    CHECK(!ui_settings_opt_shown(UI_OPT_VAR_DEBUG_MENU) &&
              !ui_settings_opt_shown(UI_OPT_FREE_MOVEMENT),
          "%s: no Debug menu or Free movement row without Developer mode", e->slug);
}

/* Controls > Remap controls ends with Ask Yorda for a hint on the discs with
   hints, then Reset to defaults and Back; the other discs go from the right
   stick's last direction straight to Reset */
static void checkHintRemap(const Expect *e)
{
    useConfig("version = 1\n[ui]\nlanguage = \"en\"\n");
    stage_no = 1;
    const int mainL = enterMainKeep(1);
    const int ctlL = openPage(mainL, -1, UI_PAGE_CONTROLS);
    const int remapL = openLink(ctlL, UI_PAGE_CONTROLS, UI_PAGE_REMAP);
    CHECK(!ICO_NTSCAUG6JUN26 == e->hints, "%s: the hint discs as the matrix says", e->slug);
    for (int i = 0; i < ICO_T_RSTICK_RIGHT; i++) {
        press(0x4000); /* from Cross to the right stick's Right: 23 steps */
    }
    CHECK(strcmp(lt_ext_row_text(lt_ext_layout(remapL)->curItem), "Right stick right") == 0,
          "%s: the 24th row is the right stick's Right (\"%s\")", e->slug,
          lt_ext_row_text(lt_ext_layout(remapL)->curItem));
    press(0x4000);
    CHECK(strcmp(lt_ext_row_text(lt_ext_layout(remapL)->curItem),
                 e->hints ? "Ask Yorda for a hint" : "Reset to defaults") == 0,
          "%s: after the right stick: %s (\"%s\")", e->slug,
          e->hints ? "Ask Yorda for a hint" : "Reset to defaults",
          lt_ext_row_text(lt_ext_layout(remapL)->curItem));
    press(0x4000);
    CHECK(strcmp(lt_ext_row_text(lt_ext_layout(remapL)->curItem),
                 e->hints ? "Reset to defaults" : "Back") == 0,
          "%s: then %s (\"%s\")", e->slug, e->hints ? "Reset to defaults" : "Back",
          lt_ext_row_text(lt_ext_layout(remapL)->curItem));
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
    CHECK(ui_settings_opt_shown(UI_OPT_VAR_SWORD_COLOUR) == e->lightSword,
          "%s pause: Light sword colour %d, want %d", e->slug,
          ui_settings_opt_shown(UI_OPT_VAR_SWORD_COLOUR), e->lightSword);
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
       row is masked (layoutPage).  The cleared Gameplay page has its seven
       rows, the two links and Back, inside the window's slots; a disc
       with the row has it shown by its rule and in view, unmasked, with the
       cursor on it (and on a page with more rows than the slots, a row out
       of view is masked as the window masks it) */
    const int gpL = openPage(mainL, -1, UI_PAGE_GAMEPLAY);
    const int film = ui_settings_row_of(UI_PAGE_GAMEPLAY, UI_OPT_FILM_EFFECT);
    if (e->film) {
        CHECK(film >= 0 && ui_settings_row_shown(film), "%s pause: the Film effect row shown",
              e->slug);
        lt_ext_layout(gpL)->curItem = film;
        frame(0);
        CHECK(film >= 0 && lt_ext_prop(film)->defaultMask == 0,
              "%s pause: the Film effect row masked %d with the cursor on it, want 0", e->slug,
              film >= 0 ? lt_ext_prop(film)->defaultMask : -1);
    } else {
        CHECK(film >= 0 && !ui_settings_row_shown(film) && lt_ext_prop(film)->defaultMask == 1,
              "%s pause: the Film effect row shown %d, masked %d, want 0 and 1", e->slug,
              film >= 0 ? ui_settings_row_shown(film) : -1,
              film >= 0 ? lt_ext_prop(film)->defaultMask : -1);
    }
    /* Players is reached on every disc once cleared (the 2001 discs too:
       the port carries the second-pad Yorda into their arms) */
    const int players = ui_settings_row_of(UI_PAGE_GAMEPLAY, UI_OPT_PLAYERS);
    CHECK(players >= 0 && ui_settings_row_shown(players), "%s pause: the Players row shown",
          e->slug);
    lt_ext_layout(gpL)->curItem = players;
    frame(0);
    CHECK(players >= 0 && lt_ext_prop(players)->defaultMask == 0,
          "%s pause: the Players row masked %d with the cursor on it, want 0", e->slug,
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
        checkHintRemap(e);
        checkPauseRows(e);
        checkUnusedRows(e);
        checkDraftRows(e);
    }
    gFlagGameClear = 0;
    ico_opt_set_ui_language(-1);
    CHECK(ico_region_select("pal") == 0, "the European disc again");
    printf("settings_discs_test %s: %s\n", slug, failures ? "failed" : "ok");
    return failures ? 1 : 0;
}

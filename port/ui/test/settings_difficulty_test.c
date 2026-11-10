/* settings_difficulty_test.c: the New Game screen's Difficulty row and the
 * Customize pages over the shared fixture (settings_fixture.h), run by the
 * real layout code, one disc per run (no argument the European disc, else
 * the slug: ctests settings_difficulty, settings_difficulty_<slug>):
 *
 *   - each preset's draft from the Difficulty row (Normal, Hard, Easy around,
 *     the row's text and note), with nothing in force before the confirm;
 *   - Customize: opened from the screen on its first row, Shadows, Combat and
 *     Rules from it, Back to Customize on the link and to the screen on
 *     Customize; an edit there turns the mode Custom (the draft's row, no
 *     config key, the journey untouched), the Difficulty row steps to a
 *     preset and back to the kept Custom draft, an edit back to a preset's
 *     rows shows that preset again;
 *   - Knock-downs count greyed while the limit is Off, Permadeath never;
 *     Hard's limit and count;
 *   - the confirm hands the draft to the journey (ico_difficulty_begin:
 *     its rows, its mode, a New Game's origin and journey);
 *   - a cleared save's New Game+: the draft starts from that save's rows,
 *     kept through Triangle and back; a title New Game starts at Normal;
 *   - the pause menu's read-only view: Gameplay's Difficulty link (hidden
 *     from the title) opens Customize and its pages on the journey in force,
 *     every row greyed with "Chosen when the journey began", a step changes
 *     nothing, Back returns to Gameplay on the link; the New Game screen's
 *     pages are the draft's again afterwards;
 *   - the pause menu's journey line and the toasts (the difficulty
 *     listener: the Continue list's refusal, each counted knock-down short of
 *     the limit);
 *   - the screen's notes fit two lines in every table; the moved rows' notes
 *     carry no achievements tail.
 * CPU only. */
#include "settings_fixture.h"

#include "settings_difficulty.h"
#include "popup.h"

/* the screen up, the cursor on Difficulty */
static int ngOpen(int cleared)
{
    gFlagGameClear = cleared;
    const int ml = ui_new_game_screen_enter();
    CHECK(ml >= 0, "the New Game screen is built");
    lt_switch_layout(ml);
    CHECK(settle(ml, 60), "the New Game screen (%d)", current_layout_id);
    return ml;
}

static void ngSetup(void)
{
    useConfig("version = 1\n[ui]\nlanguage = \"en\"\n");
    fakeTables();
    lt_ext_reset();
    ui_settings_reset();
    memset(pad, 0, sizeof(pad));
    pad[0].ana[0] = pad[0].ana[1] = pad[0].ana[2] = pad[0].ana[3] = 128;
    NonLinearCameraMove = 2;
    gFlagGameClear = 0;
    init_layout_texture(2);
    settle(fxLayout(54), 4);
    ico_difficulty_reset();
}

static const char *ngValue(void)
{
    return lt_ext_row_text(ui_new_game_screen_difficulty() + 1);
}

/* the draft's rows are preset mode's */
static int draftIs(IcoDiffMode mode)
{
    IcoDifficulty p;
    ico_difficulty_preset(mode, &p);
    return memcmp(ui_new_game_screen_draft()->row, p.row, sizeof(p.row)) == 0;
}

static void testPresets(void)
{
    ngSetup();
    const int ml = ngOpen(0);
    const LtProp *l = lt_ext_layout(ml);
    const int note = l->last - 1;
    CHECK(l->curItem == ui_new_game_screen_difficulty() && strcmp(ngValue(), "Normal") == 0 &&
              draftIs(ICO_DIFF_NORMAL),
          "Normal to begin with (%s)", ngValue());
    press(LT_PAD_RIGHT);
    CHECK(strcmp(ngValue(), "Hard") == 0 && draftIs(ICO_DIFF_HARD) &&
              strncmp(lt_ext_row_text(note), "Faster, tougher", 15) == 0,
          "Right: Hard, its rows and note (%s)", lt_ext_row_text(note));
    CHECK(ui_new_game_screen_draft()->row[ICO_DIFF_KNOCK_LIMIT] == 3 &&
              ui_new_game_screen_draft()->row[ICO_DIFF_KNOCK_SCOPE] == ICO_DIFF_SCOPE_ROOM,
          "Hard: three knock-downs in a room");
    press(LT_PAD_RIGHT);
    CHECK(strcmp(ngValue(), "Easy") == 0 && draftIs(ICO_DIFF_EASY) &&
              strncmp(lt_ext_row_text(note), "Slower, gentler", 15) == 0,
          "Right: Easy around (no Custom yet) (%s)", ngValue());
    press(LT_PAD_RIGHT);
    CHECK(strcmp(ngValue(), "Normal") == 0 && draftIs(ICO_DIFF_NORMAL), "Right: Normal");
    press(LT_PAD_LEFT);
    CHECK(strcmp(ngValue(), "Easy") == 0, "Left: Easy");
    CHECK(!ui__diff_has_custom() && !ico_difficulty_has_journey() &&
              ico_difficulty_active()->mode == ICO_DIFF_NORMAL,
          "nothing in force before the confirm");
    press(LT_PAD_TRIANGLE);
    CHECK(settle(fxLayout(9), 60), "Triangle: the vibration screen");
}

/* Customize from the screen: its layout, the cursor on its first row */
static int custOpen(int ml)
{
    lt_ext_layout(ml)->curItem = ui_new_game_screen_customize();
    frame(0);
    press(lt_ext_confirm_button());
    const int cl = ui_settings_page_layout(UI_PAGE_CUSTOMIZE);
    int labels[8];
    ui_settings_page_rows(UI_PAGE_CUSTOMIZE, labels, NULL, NULL, 8);
    CHECK(settle(cl, 60) && lt_ext_layout(cl)->curItem == labels[0],
          "Customize opens on Shadows (%d)", current_layout_id);
    return cl;
}

static void testCustomize(void)
{
    char p[1100];
    ngSetup();
    int ml = ngOpen(0);
    int cl = custOpen(ml);
    const int sl = openLink(cl, UI_PAGE_CUSTOMIZE, UI_PAGE_SHADOWS);
    for (int k = 0; k < 4; k++) {
        frame(0);
    }
    int lb[16], op[16], lv[16];
    const int sn = ui_settings_page_rows(UI_PAGE_SHADOWS, lb, op, lv, 16);
    CHECK(sn == 8 && lt_ext_layout(sl)->curItem == lb[0] && op[0] == UI_OPT_SHADOW_SPEED &&
              strcmp(lt_ext_row_text(lv[0]), "Original") == 0,
          "Shadows opens on Shadow speed, Original");
    checkPageFits(UI_PAGE_SHADOWS, "Customize > Shadows");
    press(LT_PAD_RIGHT);
    CHECK(strcmp(lt_ext_row_text(lv[0]), "Faster") == 0 &&
              ui_new_game_screen_draft()->row[ICO_DIFF_SHADOW_SPEED] == ICO_SPEED_FASTER &&
              ui__diff_draft_mode() == ICO_DIFF_CUSTOM && ui__diff_has_custom(),
          "Right on Shadow speed: Faster, the draft Custom");
    CHECK(ico_opt_shadow_speed() == ICO_SPEED_ORIGINAL && !ico_difficulty_has_journey() &&
              ico_config_get_string("gameplay.shadow_speed", NULL) == NULL,
          "the journey in force and the config untouched");
    /* No Shadows: the page's own row, Off, its note */
    for (int i = 0; i < sn; i++) {
        if (op[i] == UI_OPT_DIFF_NO_SHADOWS) {
            lt_ext_layout(sl)->curItem = lb[i];
            frame(0);
            CHECK(strcmp(lt_ext_row_text(lv[i]), "Off") == 0 &&
                      noteStarting(UI_PAGE_SHADOWS, "On: no Shadows ever appear") >= 0 &&
                      !lt_ext_prop(noteStarting(UI_PAGE_SHADOWS, "On: no Shadows ever appear"))
                           ->masked,
                  "No Shadows: Off, its note on the cursor");
        }
    }
    press(LT_PAD_TRIANGLE);
    CHECK(settle(cl, 60) &&
              lt_ext_layout(cl)->curItem == ui_settings_row_of(UI_PAGE_CUSTOMIZE, UI_OPT_LINK),
          "Back: Customize, on the Shadows link");
    press(LT_PAD_TRIANGLE);
    CHECK(settle(ml, 60) && lt_ext_layout(ml)->curItem == ui_new_game_screen_customize() &&
              strcmp(ngValue(), "Custom") == 0,
          "Back: the screen on Customize, Difficulty Custom (%s)", ngValue());
    /* Difficulty: Custom, then the presets and back to the kept draft */
    lt_ext_layout(ml)->curItem = ui_new_game_screen_difficulty();
    frame(0);
    press(LT_PAD_RIGHT);
    CHECK(strcmp(ngValue(), "Easy") == 0 && draftIs(ICO_DIFF_EASY), "Right from Custom: Easy (%s)",
          ngValue());
    press(LT_PAD_LEFT);
    CHECK(strcmp(ngValue(), "Custom") == 0 &&
              ui_new_game_screen_draft()->row[ICO_DIFF_SHADOW_SPEED] == ICO_SPEED_FASTER,
          "Left: Custom again, the kept rows (%s)", ngValue());
    press(LT_PAD_LEFT);
    CHECK(strcmp(ngValue(), "Hard") == 0 && draftIs(ICO_DIFF_HARD), "Left: Hard");
    press(LT_PAD_RIGHT);
    CHECK(strcmp(ngValue(), "Custom") == 0, "Right: Custom");
    /* an edit back to Normal's rows: Normal again */
    cl = custOpen(ml);
    openLink(cl, UI_PAGE_CUSTOMIZE, UI_PAGE_SHADOWS);
    press(LT_PAD_LEFT);
    CHECK(ui__diff_draft_mode() == ICO_DIFF_NORMAL, "Left on Shadow speed: Normal's rows");
    press(LT_PAD_TRIANGLE);
    settle(cl, 60);
    /* Rules: the count greyed while the limit is Off, Permadeath never */
    const int rl = openLink(cl, UI_PAGE_CUSTOMIZE, UI_PAGE_RULES);
    for (int k = 0; k < 4; k++) {
        frame(0);
    }
    const int rn = ui_settings_page_rows(UI_PAGE_RULES, lb, op, lv, 16);
    checkPageFits(UI_PAGE_RULES, "Customize > Rules");
    int lim = -1, scope = -1, perma = -1;
    for (int i = 0; i < rn; i++) {
        lim = op[i] == UI_OPT_DIFF_KNOCK_LIMIT ? i : lim;
        scope = op[i] == UI_OPT_DIFF_KNOCK_SCOPE ? i : scope;
        perma = op[i] == UI_OPT_DIFF_PERMADEATH ? i : perma;
    }
    CHECK(lim >= 0 && scope == lim + 1 && perma == scope + 1,
          "Rules: the limit, its count, Permadeath");
    if (lim >= 0 && scope >= 0 && perma >= 0) {
        CHECK(strcmp(lt_ext_row_text(lv[lim]), "Off") == 0 &&
                  strcmp(lt_ext_row_text(lv[scope]), "Each room") == 0 &&
                  lt_ext_row_dim(lb[scope]) && lt_ext_row_dim(lv[scope]) &&
                  !lt_ext_row_dim(lb[perma]),
              "the limit Off: the count greyed, Permadeath not");
        lt_ext_layout(rl)->curItem = lb[lim];
        frame(0);
        press(LT_PAD_RIGHT);
        CHECK(strcmp(lt_ext_row_text(lv[lim]), "1") == 0 && !lt_ext_row_dim(lb[scope]),
              "Right: a limit of 1, the count no longer greyed");
        press(LT_PAD_LEFT);
        press(LT_PAD_LEFT);
        CHECK(strcmp(lt_ext_row_text(lv[lim]), "9") == 0, "Left twice: 9 (around)");
        lt_ext_layout(rl)->curItem = lb[scope];
        frame(0);
        press(LT_PAD_RIGHT);
        CHECK(strcmp(lt_ext_row_text(lv[scope]), "Whole journey") == 0, "the count: Whole journey");
        lt_ext_layout(rl)->curItem = lb[perma];
        frame(0);
        press(LT_PAD_RIGHT);
        CHECK(strcmp(lt_ext_row_text(lv[perma]), "On") == 0 &&
                  noteStarting(UI_PAGE_RULES, "On: any game over ends this journey") >= 0,
              "Permadeath On, its note");
    }
    press(LT_PAD_TRIANGLE);
    settle(cl, 60);
    press(LT_PAD_TRIANGLE);
    CHECK(settle(ml, 60), "back on the screen");
    /* the confirm: the draft is the journey's */
    IcoDifficulty want = *ui_new_game_screen_draft();
    const int games = s_newGames;
    press(LT_PAD_START);
    CHECK(s_newGames == games + 1, "START: the game starts");
    const IcoDifficulty *a = ico_difficulty_active();
    CHECK(memcmp(a->row, want.row, sizeof(want.row)) == 0 && a->mode == ICO_DIFF_CUSTOM &&
              a->origin == ICO_DIFF_ORIGIN_NEW && ico_difficulty_has_journey() &&
              a->knocks_room == 0 && a->knocks_journey == 0 && a->finished == 0,
          "the journey has the draft's rows, Custom, a New Game's");
    CHECK(ico_difficulty_row(ICO_DIFF_KNOCK_LIMIT) == 9 && ico_difficulty_row(ICO_DIFF_PERMADEATH),
          "the limit and Permadeath in force");
    /* the moved rows never wrote a config key */
    CHECK(ui_settings_save() == 0, "save");
    path(p, sizeof(p), "settings_test.toml");
    IcoToml *t = ico_toml_load(p);
    if (t != NULL) {
        CHECK(!ico_toml_has(t, "gameplay.shadow_speed") && !ico_toml_has(t, "variants.no_hints"),
              "no journey row in the config");
        ico_toml_free(t);
    }
    ico_difficulty_reset();
}

/* a cleared save's New Game+ starts from its rows, kept through the
   vibration screen and back; the title's New Game starts at Normal */
static void testClearedSave(void)
{
    IcoDifficulty hard, custom;
    ngSetup();
    ico_difficulty_preset(ICO_DIFF_HARD, &hard);
    ico_difficulty_begin(&hard); /* the loaded save's record: a journey */
    int ml = ngOpen(1);
    CHECK(strcmp(ngValue(), "Hard") == 0 && draftIs(ICO_DIFF_HARD) && !ui__diff_has_custom(),
          "a cleared Hard save: the draft Hard (%s)", ngValue());
    CHECK(!ico_difficulty_has_journey(), "the screen's reset: Normal in force, no journey");
    press(LT_PAD_TRIANGLE);
    CHECK(settle(fxLayout(9), 60), "Triangle: the vibration screen");
    ml = ngOpen(1);
    CHECK(strcmp(ngValue(), "Hard") == 0, "again from the vibration screen: still Hard");
    press(LT_PAD_TRIANGLE);
    settle(fxLayout(9), 60);
    /* a customized save: Custom offered from the start */
    ico_difficulty_preset(ICO_DIFF_NORMAL, &custom);
    custom.row[ICO_DIFF_YORDA_TRIPS] = 1;
    ico_difficulty_begin(&custom);
    ml = ngOpen(1);
    CHECK(strcmp(ngValue(), "Custom") == 0 && ui__diff_has_custom() &&
              ui_new_game_screen_draft()->row[ICO_DIFF_YORDA_TRIPS] == 1,
          "a cleared Custom save: Custom, its rows (%s)", ngValue());
    press(LT_PAD_TRIANGLE);
    settle(fxLayout(9), 60);
    /* the title's New Game */
    ml = ngOpen(0);
    CHECK(strcmp(ngValue(), "Normal") == 0 && draftIs(ICO_DIFF_NORMAL) && !ui__diff_has_custom(),
          "the title's New Game: Normal (%s)", ngValue());
    (void)ml;
    press(LT_PAD_TRIANGLE);
    settle(fxLayout(9), 60);
    gFlagGameClear = 0;
    ico_difficulty_reset();
}

/* the screen's notes (the four modes', Customize's) fit its two lines in
   every table; the moved rows' notes on Customize's pages carry no
   achievements tail */
static void testNotes(void)
{
    static const int kNotes[] = {UI_STR_DIFF_EASY_NOTE,      UI_STR_DIFF_NORMAL_NOTE,
                                 UI_STR_DIFF_HARD_NOTE,      UI_STR_DIFF_CUSTOM_NOTE,
                                 UI_STR_DIFF_CUSTOMIZE_NOTE, UI_STR_DIFF_LOCKED_NOTE};
    static const UiSettingsPage kPages[3] = {UI_PAGE_SHADOWS, UI_PAGE_COMBAT, UI_PAGE_RULES};
    ngSetup();
    for (int lang = 0; lang < UI_LANG_TABLES; lang++) {
        ui_set_language((UiLang)lang);
        for (unsigned k = 0; k < sizeof(kNotes) / sizeof(kNotes[0]); k++) {
            char buf[256];
            ui_settings_wrap_note(kNotes[k], buf, sizeof(buf));
            const char *nl = strchr(buf, '\n');
            CHECK(nl == NULL || strchr(nl + 1, '\n') == NULL,
                  "language %d note %d: at most two lines (%s)", lang, kNotes[k], buf);
        }
    }
    ui_set_language(UI_LANG_EN);
    for (int p = 0; p < 3; p++) {
        for (int i = 0; i < 16; i++) {
            const int str = ui_settings_page_note_str(kPages[p], i);
            CHECK(str < 0 || (strstr(ui_str((UiStrId)str), "chievements") == NULL),
                  "page %d note %d: no achievements tail (%s)", kPages[p], i,
                  str < 0 ? "" : ui_str((UiStrId)str));
        }
    }
}

/* the Difficulty link's label on Gameplay, -1 for none */
static int gameLink(void)
{
    int labels[24], opts[24];
    const int n = ui_settings_page_rows(UI_PAGE_GAMEPLAY, labels, opts, NULL, 24);
    for (int i = 0; i < n; i++) {
        if (opts[i] == UI_OPT_LINK &&
            lt_ext_prop(labels[i])->right == ui_settings_page_layout(UI_PAGE_CUSTOMIZE)) {
            return labels[i];
        }
    }
    return -1;
}

static void testReadOnly(void)
{
    IcoDifficulty hard;
    int lb[16], op[16], lv[16];

    /* from the title: no link */
    int mainL = enterMain(1);
    int gameL = openPage(mainL, -1, UI_PAGE_GAMEPLAY);
    for (int k = 0; k < 4; k++) {
        frame(0);
    }
    CHECK(gameLink() >= 0 && !ui_settings_row_shown(gameLink()),
          "the title: Gameplay has no Difficulty link");

    /* from the pause menu, a Hard journey in force */
    ico_difficulty_reset();
    mainL = enterMain(0);
    ico_difficulty_preset(ICO_DIFF_HARD, &hard);
    hard.row[ICO_DIFF_SHADOW_SPEED] = ICO_SPEED_FASTEST;
    ico_difficulty_begin(&hard);
    gameL = openPage(mainL, -1, UI_PAGE_GAMEPLAY);
    for (int k = 0; k < 4; k++) {
        frame(0);
    }
    const int link = gameLink();
    CHECK(link >= 0 && ui_settings_row_shown(link) &&
              strcmp(lt_ext_row_text(link), "Difficulty") == 0,
          "the pause menu: Gameplay has the Difficulty link");
    const int cl = openLink(gameL, UI_PAGE_GAMEPLAY, UI_PAGE_CUSTOMIZE);
    CHECK(ui__diff_readonly(), "opened from Gameplay: read-only");
    const int sl = openLink(cl, UI_PAGE_CUSTOMIZE, UI_PAGE_SHADOWS);
    for (int k = 0; k < 4; k++) {
        frame(0);
    }
    const int sn = ui_settings_page_rows(UI_PAGE_SHADOWS, lb, op, lv, 16);
    CHECK(sn == 8 && op[0] == UI_OPT_SHADOW_SPEED && strcmp(lt_ext_row_text(lv[0]), "Fastest") == 0,
          "the journey's rows (Fastest, not the draft's Original): %s",
          sn > 0 ? lt_ext_row_text(lv[0]) : "");
    for (int i = 0; i + 1 < sn; i++) {
        CHECK(lt_ext_row_dim(lb[i]) && lt_ext_row_dim(lv[i]), "row %d greyed", i);
    }
    int n = noteStarting(UI_PAGE_SHADOWS, "Chosen when the journey began");
    CHECK(n >= 0 && !lt_ext_prop(n)->masked, "the note: chosen when the journey began");
    press(LT_PAD_RIGHT);
    press(LT_PAD_LEFT);
    CHECK(ico_difficulty_row(ICO_DIFF_SHADOW_SPEED) == ICO_SPEED_FASTEST &&
              strcmp(lt_ext_row_text(lv[0]), "Fastest") == 0 &&
              ico_difficulty_active()->mode == ICO_DIFF_CUSTOM,
          "steps are ignored (%s)", lt_ext_row_text(lv[0]));
    /* every page: the journey's values (Hard's limit of 3 on Rules), all greyed */
    press(LT_PAD_TRIANGLE);
    CHECK(settle(cl, 60), "Back: Customize");
    const int rl = openLink(cl, UI_PAGE_CUSTOMIZE, UI_PAGE_RULES);
    for (int k = 0; k < 4; k++) {
        frame(0);
    }
    const int rn = ui_settings_page_rows(UI_PAGE_RULES, lb, op, lv, 16);
    for (int i = 0; i < rn; i++) {
        if (op[i] == UI_OPT_DIFF_KNOCK_LIMIT) {
            CHECK(strcmp(lt_ext_row_text(lv[i]), "3") == 0, "Hard's limit, 3 (%s)",
                  lt_ext_row_text(lv[i]));
        }
        if (op[i] == UI_OPT_DIFF_KNOCK_SCOPE || op[i] == UI_OPT_DIFF_PERMADEATH) {
            CHECK(lt_ext_row_dim(lb[i]) && lt_ext_row_dim(lv[i]), "Rules row %d greyed", i);
        }
    }
    (void)rl;
    checkPageFits(UI_PAGE_RULES, "Customize > Rules (read-only)");
    press(LT_PAD_TRIANGLE);
    CHECK(settle(cl, 60), "Back: Customize");
    press(LT_PAD_TRIANGLE);
    CHECK(settle(gameL, 60) && lt_ext_layout(gameL)->curItem == link,
          "Back from Customize: Gameplay, on the Difficulty link (%d)", current_layout_id);
    (void)sl;

    /* the New Game screen's pages are the draft's again */
    ngSetup();
    ico_difficulty_begin(&hard);
    const int ml = ngOpen(0);
    const int ncl = custOpen(ml);
    CHECK(!ui__diff_readonly(), "from the New Game screen: not read-only");
    const int nsl = openLink(ncl, UI_PAGE_CUSTOMIZE, UI_PAGE_SHADOWS);
    for (int k = 0; k < 4; k++) {
        frame(0);
    }
    ui_settings_page_rows(UI_PAGE_SHADOWS, lb, op, lv, 16);
    CHECK(strcmp(lt_ext_row_text(lv[0]), "Original") == 0 && !lt_ext_row_dim(lb[0]) &&
              !lt_ext_row_dim(lv[0]),
          "the draft's Original, not greyed (%s)", lt_ext_row_text(lv[0]));
    (void)nsl;
    ico_difficulty_reset();
}

/* the pause menu's journey line text and the listener's toasts */
static void testToasts(void)
{
    IcoDifficulty d;
    char line[96];

    ngSetup();
    ui_settings_install(); /* registers the listener (a no-op when built) */
    ico_difficulty_reset();
    CHECK(ui__diff_line_text(line, sizeof(line)) && strcmp(line, "Normal") == 0, "Normal: %s",
          line);
    ico_difficulty_preset(ICO_DIFF_HARD, &d);
    d.row[ICO_DIFF_KNOCK_LIMIT] = 2;
    ico_difficulty_begin(&d);
    ui__diff_line_text(line, sizeof(line));
    CHECK(strcmp(line, "Hard 0 / 2") == 0, "Hard: %s", line);
    ui_popup_reset();
    CHECK(!ui_popup_active(), "no toast to begin with");
    /* the first knock-down is counted short of the limit: a toast */
    CHECK(ico_difficulty_knockdown(1, 0, 0) == 0, "counted");
    CHECK(ui_popup_active() && strstr(ui_popup_title(), "Knocked down 1 of 2") != NULL,
          "the toast: Knocked down 1 of 2 (%s)", ui_popup_active() ? ui_popup_title() : "");
    ui_popup_reset();
    /* the one at the limit: no toast, the game over follows */
    ico_difficulty_tick(1, 0, 0);
    CHECK(ico_difficulty_knockdown(1, 0, 0) == 1, "the limit");
    CHECK(!ui_popup_active(), "no toast at the limit");
    ico_difficulty_game_over();
    /* the Continue list's refusal */
    ui_popup_reset();
    ico_difficulty_notify(ICO_DIFF_EV_REFUSED, 0, 0);
    CHECK(ui_popup_active() && strstr(ui_popup_title(), "This journey has ended.") != NULL,
          "the refusal's toast (%s)", ui_popup_active() ? ui_popup_title() : "");
    ui_popup_reset();
    ico_difficulty_reset();
}

int main(int argc, char **argv)
{
    snprintf(s_dir, sizeof(s_dir), "%s", argc > 1 ? argv[1] : ".");
    setEnv("LC_ALL", NULL);
    setEnv("LANG", "en_GB.UTF-8");
    const char *slug = argc > 2 ? argv[2] : "pal";
    CHECK(ico_region_select(slug) == 0, "the %s disc", slug);
    testPresets();
    testCustomize();
    testClearedSave();
    testReadOnly();
    testToasts();
    testNotes();
    gFlagGameClear = 0;
    ico_opt_set_ui_language(-1);
    CHECK(ico_region_select("pal") == 0, "the European disc again");
    printf("settings_difficulty_test %s: %s\n", slug, failures ? "failed" : "ok");
    return failures ? 1 : 0;
}

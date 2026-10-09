/* settings_extras_test.c: the Settings menu's Extras page (its rows from the
 * title and the pause menu, Credits, Models, Characters with the model
 * viewer's panel faked) and the pages that hide the title's logo, over the
 * shared fixture (settings_fixture.h).  CPU only. */
#include "settings_fixture.h"

/* stderr to a file for a stretch: the log lines the menus write */
static int s_errSaved = -1;

static void errCapture(void)
{
    char p[1100];
    path(p, sizeof(p), "settings_test_stderr.txt");
    fflush(stderr);
    s_errSaved = dup(2);
    FILE *f = freopen(p, "wb", stderr);
    (void)f;
}

static void errRelease(char *out, size_t n)
{
    char p[1100];
    path(p, sizeof(p), "settings_test_stderr.txt");
    fflush(stderr);
    dup2(s_errSaved, 2);
    close(s_errSaved);
    clearerr(stderr);
    out[0] = '\0';
    FILE *f = fopen(p, "rb");
    if (f) {
        size_t got = fread(out, 1, n - 1, f);
        out[got] = '\0';
        fclose(f);
    }
}

/* package MV: a stand-in for the model viewer's list (ui_SettingsSetModelsHandler) */
static int s_modelsCalls;

static int fakeModels(void)
{
    s_modelsCalls++;
    return ui_SettingsPageLayout(UI_PAGE_ACHIEVEMENTS);
}

/* package CRED: the Credits row unlocked by [dev] unlock_credits (the
   ending achievement is the player's way; credits_test and achievements_test
   check those), its locked look gone, and Cross with an engine that starts:
   the menu leaves for the game's empty layout (55) with the flag on and the
   title's cursor kept on Settings; with no engine, a failed start stays. */
static int s_fakeBegins;

static int fakeCreditsBegin(void)
{
    s_fakeBegins++;
    ico_credits_set_active(1);
    return 0;
}

static void testCredits(int mainL, int exL)
{
    static const IcoCreditsEngine kFake = {fakeCreditsBegin};
    char log[512];
    int el[8], eo[8], ev[8];

    useConfig("version = 1\n[dev]\nunlock_credits = true\n");
    press(0x40);
    CHECK(settle(exL, 60), "Extras, unlocked");
    ui_SettingsPageRows(UI_PAGE_EXTRAS, el, eo, ev, 8);
    lt_ext_Layout(exL)->curItem = el[2];
    frame(0);
    int note = rowWithText(UI_PAGE_EXTRAS, "Finish the game to unlock");
    CHECK(ev[2] >= 0 && strcmp(lt_ext_RowText(ev[2]), "") == 0 && lt_ext_RowDim(el[2]) == 0 &&
              lt_ext_RowDim(ev[2]) == 0,
          "Credits unlocked: value \"%s\", not greyed", ev[2] >= 0 ? lt_ext_RowText(ev[2]) : "-");
    CHECK(note >= 0 && lt_ext_Prop(note)->masked == 1, "no locked note on the unlocked Credits");
    /* no engine (this program has none): a failed start, the page stays */
    ico_credits_set_engine(NULL);
    errCapture();
    press(0x40);
    errRelease(log, sizeof(log));
    CHECK(strstr(log, "credits: enter") == NULL && strstr(log, "credits: failed") != NULL &&
              current_layout_id == exL && !ico_credits_active(),
          "Credits with no engine: fails and stays (\"%s\")", log);
    /* an engine: the playback starts and the menu leaves for layout 55 (the
       game's empty layout; here an empty one shaped like 54) */
    setLayout(55, 292, 292, -1, -1);
    texLayout[55].fadeInTime = 0.0f;
    texLayout[55].fadeOutTime = 0.0f;
    ico_credits_set_engine(&kFake);
    s_fakeBegins = 0;
    errCapture();
    press(0x40);
    errRelease(log, sizeof(log));
    CHECK(s_fakeBegins == 1 && strstr(log, "credits: enter") != NULL && ico_credits_active(),
          "Credits starts the playback (\"%s\")", log);
    CHECK(settle(55, 60), "the menu leaves for the game's empty layout (%d)", current_layout_id);
    CHECK(texLayout[13].defaultItem == ui_SettingsEntryRow(13),
          "the title comes back on Settings (%d)", texLayout[13].defaultItem);
    ico_credits_set_engine(NULL);
    ico_credits_set_active(0);
    useConfig("version = 1\n");
    (void)mainL;
}

/* Settings > Extras: a row of the main page after Achievements, from both
   entries (v0.4.2: Characters); Music, Models, Credits (from the title
   only), Characters and Back; the entries are placeholders that log;
   Credits shows the locked style. */
static void testExtras(void)
{
    char log[512];
    for (int title = 0; title < 2; title++) {
        int mainL = enterMain(title);
        int labels[16], opts[16];
        int n = ui_SettingsPageRows(UI_PAGE_MAIN, labels, opts, NULL, 16);
        int ex = ui_SettingsRowOf(UI_PAGE_MAIN, UI_OPT_LINK), idx = 0;
        for (int i = 0; i < n; i++) {
            if (strcmp(lt_ext_RowText(labels[i]), "Extras") == 0) {
                ex = labels[i];
                idx = i;
            }
        }
        /* v0.4.0: Dump textures (developer mode only, hidden here) before
           Back */
        CHECK(n == 13 && idx == 8, "Extras is the row after Achievements (index %d of %d)", idx, n);
        CHECK(lt_ext_Prop(ex)->right == ui_SettingsPageLayout(UI_PAGE_EXTRAS), "Extras opens");
        CHECK(lt_ext_Prop(ex)->defaultMask == 0, "title %d: the Extras row is shown", title);
        /* the cursor: Achievements, Down */
        lt_ext_Layout(mainL)->curItem = labels[7];
        press(0x4000);
        CHECK(lt_ext_Layout(mainL)->curItem == ex,
              "title %d: Down from Achievements lands on Extras", title);
        /* the rows below follow: Back's y, ten rows on both entries */
        CHECK(lt_ext_Prop(labels[12])->dispY == 40 + 15 * 9, "title %d: Back at y %d", title,
              lt_ext_Prop(labels[12])->dispY);
        if (!title) {
            continue;
        }
        /* open it */
        press(0x40);
        int exL = ui_SettingsPageLayout(UI_PAGE_EXTRAS);
        CHECK(settle(exL, 60), "the Extras page");
        int el[8], eo[8], ev[8];
        int en = ui_SettingsPageRows(UI_PAGE_EXTRAS, el, eo, ev, 8);
        CHECK(en == 5 && lt_ext_Layout(exL)->curItem == el[0], "five rows, the cursor on Music");
        CHECK(strcmp(lt_ext_RowText(el[0]), "Music") == 0 &&
                  strcmp(lt_ext_RowText(el[1]), "Models") == 0 &&
                  strcmp(lt_ext_RowText(el[2]), "Credits") == 0 &&
                  strcmp(lt_ext_RowText(el[3]), "Character Customization") == 0 &&
                  strcmp(lt_ext_RowText(el[4]), "Back") == 0,
              "Music, Models, Credits, Character Customization, Back");
        /* the row's name fits its box of 300 at 60 % or more */
        for (int g = 0; g < UI_LANG_COUNT; g++) {
            ui_SetLanguage((UiLang)g);
            const float w =
                ui_MeasureMenuText(UI_MENU_TEXT_SIZE, ui_Str(UI_STR_SECTION_CHARACTERS));
            CHECK(w * 0.6f <= 300.0f, "language %d: the row fits (%.1f)", g, (double)w);
        }
        ui_SetLanguage(UI_LANG_EN);
        /* the locked style on Credits: greyed label and value, the note on
           the cursor only */
        CHECK(ev[2] >= 0 && strcmp(lt_ext_RowText(ev[2]), "Locked") == 0 &&
                  lt_ext_RowDim(el[2]) == 1 && lt_ext_RowDim(ev[2]) == 1 &&
                  lt_ext_RowDim(el[0]) == 0,
              "Credits: locked value \"%s\", greyed", ev[2] >= 0 ? lt_ext_RowText(ev[2]) : "-");
        int note = rowWithText(UI_PAGE_EXTRAS, "Finish the game to unlock");
        CHECK(note >= 0, "the locked note");
        frame(0);
        CHECK(note >= 0 && lt_ext_Prop(note)->masked == 1, "no note on Music");
        press(0x4000);
        press(0x4000);
        CHECK(lt_ext_Layout(exL)->curItem == el[2], "on Credits");
        CHECK(note >= 0 && lt_ext_Prop(note)->masked == 0, "the note on Credits");
        /* Music opens the gallery (testGallery has the page itself) */
        lt_ext_Layout(exL)->curItem = el[0];
        frame(0);
        press(0x40);
        int galL = ui_SettingsPageLayout(UI_PAGE_MUSIC);
        CHECK(galL >= 0 && settle(galL, 60), "Cross on Music opens the gallery");
        press(0x10);
        CHECK(settle(exL, 60) && lt_ext_Layout(exL)->curItem == el[0],
              "Triangle: back on the Music row");
        /* Models only logs, and stays on the page; Credits (locked) too */
        const char *want[3] = {"music", "models", "credits"};
        for (int k = 1; k < 3; k++) {
            lt_ext_Layout(exL)->curItem = el[k];
            frame(0);
            errCapture();
            press(0x40);
            errRelease(log, sizeof(log));
            char line[64];
            snprintf(line, sizeof(line), "extras: %s not available", want[k]);
            /* Models: "not available"; the locked Credits: "credits: locked"
               alone (review nit: no false "not available") */
            CHECK((k == 2 ? strstr(log, line) == NULL : strstr(log, line) != NULL) &&
                      current_layout_id == exL,
                  "Cross on %s logs and stays (\"%s\")", want[k], log);
            CHECK(k != 2 || (strstr(log, "credits: locked") != NULL && !ico_credits_active()),
                  "Cross on the locked Credits: \"%s\"", log);
        }
        /* package MV: with the model viewer's handler (port/game/
           model_viewer.c registers its list), Models opens the layout it
           returns; here the achievements page stands in for the list */
        s_modelsCalls = 0;
        ui_SettingsSetModelsHandler(fakeModels);
        lt_ext_Layout(exL)->curItem = el[1];
        frame(0);
        press(0x40);
        CHECK(s_modelsCalls == 1 && settle(ui_SettingsPageLayout(UI_PAGE_ACHIEVEMENTS), 60),
              "Models opens the handler's layout (%d calls)", s_modelsCalls);
        ui_SettingsSetModelsHandler(NULL);
        press(0x10);
        CHECK(settle(mainL, 60), "back from the handler's layout");
        lt_ext_Layout(mainL)->curItem = ex;
        frame(0);
        press(0x40);
        CHECK(settle(exL, 60), "Extras after the handler's layout");
        /* Back and Triangle return to the main page, the cursor on Extras */
        lt_ext_Layout(exL)->curItem = el[4];
        press(0x40);
        CHECK(settle(mainL, 60) && lt_ext_Layout(mainL)->curItem == ex,
              "Back: the cursor on Extras");
        press(0x40);
        CHECK(settle(exL, 60), "Extras again");
        press(0x10);
        CHECK(settle(mainL, 60), "Triangle: the menu");
        testCredits(mainL, exL);
    }

    /* the strings, five languages */
    static const char *const want[5][5] = {
        {"Extras", "Music", "Models", "Credits", "Finish the game to unlock"},
        {"Extras", "Musique", "Mod\xC3\xA8les",
         "Cr\xC3\xA9"
         "dits",
         NULL},
        {"Extras", "Musik", "Modelle", "Mitwirkende", NULL},
        {"Extra", "Musica", "Modelli", "Crediti", NULL},
        {"Extras", "M\xC3\xBAsica", "Modelos",
         "Cr\xC3\xA9"
         "ditos",
         NULL}};
    static const int ids[5] = {UI_STR_EXTRAS, UI_STR_EXTRAS_MUSIC, UI_STR_EXTRAS_MODELS,
                               UI_STR_EXTRAS_CREDITS, UI_STR_EXTRAS_LOCKED_NOTE};
    for (int l = 0; l < UI_LANG_COUNT; l++) {
        for (int i = 0; i < 5; i++) {
            const char *got = ui_StrIn((UiLang)l, (UiStrId)ids[i]);
            CHECK(want[l][i]
                      ? strcmp(got, want[l][i]) == 0
                      : (got[0] != '\0' && strcmp(got, ui_StrIn(UI_LANG_EN, (UiStrId)ids[i])) != 0),
                  "string %d in language %d: \"%s\"", i, l, got);
        }
    }

    /* the layout extension's budget (layout_ext.h): what is used, and the
       developer-mode line */
    useConfig("version = 1\n[gameplay]\ndeveloper_mode = true\n");
    lt_ext_Reset();
    ui_SettingsReset();
    errCapture();
    ui_SettingsInstall();
    errRelease(log, sizeof(log));
    printf("settings_test: %d of %d properties, %d of %d layouts used\n", lt_ext_PropCount(),
           LT_EXT_MAX_PROPERTIES, lt_ext_LayoutCount(), LT_EXT_MAX_LAYOUTS);
    CHECK(lt_ext_PropCount() < LT_EXT_MAX_PROPERTIES, "property budget (%d of %d)",
          lt_ext_PropCount(), LT_EXT_MAX_PROPERTIES);
    CHECK(lt_ext_LayoutCount() < LT_EXT_MAX_LAYOUTS, "layout budget (%d of %d)",
          lt_ext_LayoutCount(), LT_EXT_MAX_LAYOUTS);
    char want_line[96];
    snprintf(want_line, sizeof(want_line), "layout extension: %d of %d properties",
             lt_ext_PropCount(), LT_EXT_MAX_PROPERTIES);
    CHECK(strstr(log, want_line) != NULL, "developer mode prints the budget (\"%s\")", log);
}

/* package L1: ui_SettingsCoversTitle, which hides the title's logo
   (port/game/title_logo.c): every page opened from the title, Extras and
   its Music page among them; not the title's own layouts, the mirror or
   quit screens, nor any page opened from the pause menu */
static void testCoversTitle(void)
{
    for (int title = 0; title < 2; title++) {
        int mainL = enterMain(title);
        CHECK(ui_SettingsCoversTitle() == title, "title %d: the main page", title);
        for (int p = 0; p < UI_PAGE_COUNT; p++) {
            int l = ui_SettingsPageLayout((UiSettingsPage)p);
            CHECK(l >= 0, "page %d has a layout", p);
            lt_switch_layout(l);
            CHECK(settle(l, 60), "page %d up", p);
            CHECK(ui_SettingsCoversTitle() == title, "title %d: page %d covers %d", title, p,
                  ui_SettingsCoversTitle());
        }
        int others[3] = {title ? 13 : 57, ui_QuitScreenLayout(), ui_NewGameScreenLayout()};
        for (int k = 0; k < 3; k++) {
            if (others[k] < 0) {
                continue;
            }
            lt_switch_layout(others[k]);
            CHECK(settle(others[k], 60), "layout %d up", others[k]);
            CHECK(!ui_SettingsCoversTitle(), "title %d: layout %d does not cover", title,
                  others[k]);
        }
        /* the menu again from where it was entered: covering again */
        lt_switch_layout(mainL);
        CHECK(settle(mainL, 60) && ui_SettingsCoversTitle() == title, "title %d: the menu again",
              title);
    }
}

/* ------------------------------------------------------ Characters
 * v0.4.2 package K: Settings > Extras > Characters (port/game/appearance.h
 * under nine stepped rows with a swatch each, Randomize, Reset to original,
 * a note), the title's Characters inside the model viewer (package K-D:
 * a fake UiCharactersHost stands in for model_viewer.c), and Extras on the
 * pause menu with Characters and Back only. */
static const char *const kColourNames[ICO_APP_COLOURS] = {
    "Red",  "Crimson", "Rose", "Pink",  "Magenta", "Plum",  "Violet", "Indigo",
    "Navy", "Blue",    "Sky",  "Teal",  "Cyan",    "Green", "Moss",   "Olive",
    "Gold", "Orange",  "Rust", "Brown", "Sand",    "White", "Grey",   "Black"};
static const char *const kCharKeys[ICO_APP_PART_COUNT] = {
    "ico_skin",  "ico_poncho_navy", "ico_poncho_pink", "ico_poncho_light", "ico_poncho_dark",
    "ico_tunic", "ico_shorts",      "yorda_skin",      "yorda_dress"};

/* model_viewer.c's characters calls, faked: shown is what the test sets
   (-2 off, -1 loading or leaving, 0 Ico, 1 Yorda) */
static int s_hostEnters, s_hostFail, s_hostShown = -2, s_hostSwitch = -1, s_hostLeaves;

static int fakeCharsEnter(void)
{
    s_hostEnters++;
    if (s_hostFail) {
        return -1;
    }
    s_hostShown = -1; /* Ico's stage loading */
    return 0;
}

static int fakeCharsShown(void)
{
    return s_hostShown;
}

static void fakeCharsSwitch(int character)
{
    s_hostSwitch = character;
    s_hostShown = -1;
}

static void fakeCharsLeave(void)
{
    s_hostLeaves++;
    s_hostShown = -1;
}

static const UiCharactersHost kFakeHost = {fakeCharsEnter, fakeCharsShown, fakeCharsSwitch,
                                           fakeCharsLeave};

/* Settings (title or pause menu) > Extras > Characters, settled; keep:
   on the config file as it is */
static int openCharactersOn(int title, int keep)
{
    const int mainL = keep ? enterMainKeep(title) : enterMain(title);
    const int exL = openPage(mainL, 8, UI_PAGE_EXTRAS);
    int el[8];
    ui_SettingsPageRows(UI_PAGE_EXTRAS, el, NULL, NULL, 8);
    lt_ext_Layout(exL)->curItem = el[3];
    frame(0);
    press(0x40);
    const int l = ui_SettingsPageLayout(UI_PAGE_CHARACTERS);
    CHECK(settle(l, 60), "title %d: the Characters page opens (%d)", title, current_layout_id);
    for (int k = 0; k < 4; k++) {
        frame(0); /* the page refreshes */
    }
    return l;
}

static int openCharacters(int title)
{
    return openCharactersOn(title, 0);
}

/* the cursor on `label`, one Left (-1) or Right (+1) */
static void charStep(int l, int label, int dir)
{
    lt_ext_Layout(l)->curItem = label;
    frame(0);
    press(dir < 0 ? 0x8000 : 0x2000);
}

static const char *textNow(int row)
{
    frame(0);
    return lt_ext_RowText(row);
}

/* the swatch rect of a colour row: a rect row at x 578 on the label's
   middle */
static int swatchOf(int label)
{
    const LtProp *l = lt_ext_Layout(ui_SettingsPageLayout(UI_PAGE_CHARACTERS));
    unsigned char c[4];
    for (int j = l->first; j < l->last; j++) {
        if (lt_ext_RectColor(j, c) == 0 && lt_ext_Prop(j)->dispX == 578 &&
            lt_ext_Prop(j)->dispY == lt_ext_Prop(label)->dispY + 0) {
            return j;
        }
    }
    return -1;
}

static const char *savedChar(IcoToml *t, int part)
{
    char key[64];
    snprintf(key, sizeof(key), "characters.%s", kCharKeys[part]);
    return t ? ico_toml_get(t, key) : NULL;
}

/* the end of a run and a new start: what main_host calls when the window
   closes (Escape, the close button, Quit to desktop's event), then a new
   process's config: the file read again, the options and the colours
   forgotten (ico_opt_reload -> ico_appearance_reload) */
static void quitGame(char *log, size_t n)
{
    errCapture();
    ui_SettingsSaveOnQuit();
    errRelease(log, n);
}

static void startAgain(const char *p)
{
    ico_config_reset(p, "");
    ico_sysconf_reset();
    ico_opt_reload();
    ico_video_reload();
}

/* [characters] ico_tunic and yorda_dress in the file at p */
static int savedTunicDress(const char *p, const char *tunic, const char *dress)
{
    IcoToml *t = ico_toml_load(p);
    const char *a = savedChar(t, ICO_APP_ICO_TUNIC);
    const char *b = savedChar(t, ICO_APP_YORDA_DRESS);
    const int ok = a != NULL && b != NULL && strcmp(a, tunic) == 0 && strcmp(b, dress) == 0;
    ico_toml_free(t);
    return ok;
}

/* start 2: the page (pause menu, then the title's viewer) shows Red and
   Gold, the colours are read back, and the textures are recoloured */
static void checkSecondStart(const char *p, const char *who)
{
    int lb[16], lo[16], lv[16];
    startAgain(p);
    CHECK(ico_appearance_get(ICO_APP_ICO_TUNIC) == 1 &&
              ico_appearance_get(ICO_APP_YORDA_DRESS) == 17,
          "%s: start 2 reads Ico's tunic %d (want 1, Red) and Yorda's dress %d (want 17, Gold)",
          who, ico_appearance_get(ICO_APP_ICO_TUNIC), ico_appearance_get(ICO_APP_YORDA_DRESS));
    {
        /* a brown CLUT of 16 (RDTEX_PSMCT32 is 0): b_suit and fuku03 are
           recoloured, a texture of no character is not */
        unsigned char clut[16 * 4], out[16 * 4];
        for (int i = 0; i < 16; i++) {
            clut[i * 4 + 0] = (unsigned char)(0x60 + i * 4);
            clut[i * 4 + 1] = (unsigned char)(0x30 + i * 3);
            clut[i * 4 + 2] = (unsigned char)(0x20 + i * 2);
            clut[i * 4 + 3] = 0x80;
        }
        CHECK(ico_appearance_recolour("b_suit", clut, 16, 0, out) == 1 &&
                  ico_appearance_recolour("fuku03", clut, 16, 0, out) == 1 &&
                  ico_appearance_recolour("b_face2", clut, 16, 0, out) == 0,
              "%s: start 2 recolours the tunic's and the dress's textures", who);
    }
    ui_SettingsSetCharactersHost(NULL);
    openCharactersOn(0, 1);
    ui_SettingsPageRows(UI_PAGE_CHARACTERS, lb, lo, lv, 16);
    CHECK(strcmp(textNow(lv[5]), "Red") == 0 && strcmp(textNow(lv[8]), "Gold") == 0,
          "%s: start 2, the pause menu's page shows \"%s\" and \"%s\" (want Red, Gold)", who,
          lt_ext_RowText(lv[5]), lt_ext_RowText(lv[8]));
    press(0x10);
    ui_SettingsSetCharactersHost(&kFakeHost);
    s_hostShown = -2;
    s_hostFail = 0;
    const int l = openCharactersOn(1, 1);
    s_hostShown = 0;
    for (int k = 0; k < 4; k++) {
        frame(0);
    }
    const int n = ui_SettingsPageRows(UI_PAGE_CHARACTERS, lb, lo, lv, 16);
    CHECK(ui_SettingsCharactersInViewer() && n == 13 && current_layout_id == l &&
              strcmp(textNow(lv[5]), "Red") == 0 && strcmp(textNow(lv[8]), "Gold") == 0 &&
              strcmp(textNow(lb[9]), "Switch to Yorda") == 0,
          "%s: start 2, the viewer's page shows \"%s\", \"%s\" and \"%s\"", who,
          lt_ext_RowText(lv[5]), lt_ext_RowText(lv[8]), lt_ext_RowText(lb[9]));
    press(0x10);
    ui_SettingsSetCharactersHost(NULL);
    s_hostShown = -2;
}

/* v0.4.2 (K-E): the player's report "the colours are not kept after a
   restart": start 1 picks Ico's tunic Red and Yorda's dress Gold, the
   game closes, start 2 has them.  The file each start begins with is the
   one a first run writes (ico_config_write_first_run), so [characters] is
   a table the file does not have yet.  Four ways the first start ends:
   the viewer's Triangle then Escape at the title; Escape inside the
   viewer (no Triangle: before the quit save the picks were never written);
   the pause menu's Characters left with Back; Escape on the pause menu's
   Characters page. */
static void testCharactersRestart(void)
{
    char p[1100], log[512];
    int lb[16], lo[16], lv[16];
    path(p, sizeof(p), "settings_test.toml");

    for (int way = 0; way < 4; way++) {
        static const char *const kWho[4] = {"viewer, Triangle", "viewer, Escape", "pause, Back",
                                            "pause, Escape"};
        const char *who = kWho[way];
        const int viewer = way < 2;
        const int leave = way == 0 || way == 2;

        /* start 1: the first run's file */
        remove(p);
        ico_config_reset(p, "");
        CHECK(ico_config_write_first_run() == 0, "%s: the first run's file", who);
        startAgain(p);
        ui_SettingsSetCharactersHost(viewer ? &kFakeHost : NULL);
        s_hostEnters = s_hostLeaves = s_hostFail = 0;
        s_hostShown = -2;
        s_hostSwitch = -1;
        const int l = openCharactersOn(viewer, 1);
        if (viewer) {
            CHECK(s_hostEnters == 1 && ui_SettingsCharactersInViewer(),
                  "%s: Characters opens in the viewer (%d enters)", who, s_hostEnters);
            s_hostShown = 0; /* Ico's model is up */
        }
        ui_SettingsPageRows(UI_PAGE_CHARACTERS, lb, lo, lv, 16);
        charStep(l, lb[5], 1); /* Tunic: Red */
        if (viewer) {
            press(0x0008); /* R1: Yorda */
            CHECK(s_hostSwitch == 1, "%s: R1 loads Yorda", who);
            s_hostShown = 1;
        }
        for (int k = 0; k < 17; k++) {
            charStep(l, lb[8], 1); /* Dress: Original, Red, ... Gold */
        }
        CHECK(strcmp(textNow(lv[5]), "Red") == 0 && strcmp(textNow(lv[8]), "Gold") == 0,
              "%s: start 1 shows \"%s\" and \"%s\"", who, lt_ext_RowText(lv[5]),
              lt_ext_RowText(lv[8]));
        if (leave) {
            press(0x10); /* Triangle: the viewer's leave, or Back to Extras */
            if (viewer) {
                CHECK(s_hostLeaves == 1, "%s: the viewer leaves (%d)", who, s_hostLeaves);
                s_hostShown = -2; /* the title is back */
            } else {
                CHECK(settle(ui_SettingsPageLayout(UI_PAGE_EXTRAS), 60), "%s: Back to Extras", who);
            }
            CHECK(savedTunicDress(p, "red", "gold"), "%s: leaving the page writes both", who);
        } else {
            CHECK(!savedTunicDress(p, "red", "gold"),
                  "%s: nothing left the page, nothing written yet", who);
        }
        quitGame(log, sizeof(log));
        CHECK(savedTunicDress(p, "red", "gold"), "%s: the file has both after the quit", who);
        CHECK(leave ? strstr(log, "on quit") == NULL : strstr(log, "on quit") != NULL,
              "%s: the quit's log (\"%s\")", who, log);
        {
            /* the first run's lines kept, [characters] added */
            FILE *f = fopen(p, "rb");
            char text[8192];
            size_t got = f ? fread(text, 1, sizeof(text) - 1, f) : 0;
            text[got] = '\0';
            if (f) {
                fclose(f);
            }
            CHECK(strstr(text, "# ico-pc settings.") == text && strstr(text, "[photo]") != NULL &&
                      strstr(text, "[characters]\n") != NULL &&
                      strstr(text, "ico_tunic = \"red\"") != NULL &&
                      strstr(text, "yorda_dress = \"gold\"") != NULL,
                  "%s: the first run's file with [characters] added", who);
        }
        if (viewer) {
            ui_SettingsSetCharactersHost(NULL);
            s_hostShown = -2;
        }
        checkSecondStart(p, who);
    }
    /* nothing pending: the quit writes nothing and says nothing */
    quitGame(log, sizeof(log));
    CHECK(log[0] == '\0', "a quit with nothing changed is quiet (\"%s\")", log);
    ico_appearance_reset();
    ui_SettingsSave();
    useConfig("version = 1\n");
}

static void testCharacters(void)
{
    static const int opts[] = {UI_OPT_CHAR_ICO_SKIN,
                               UI_OPT_CHAR_ICO_PONCHO_NAVY,
                               UI_OPT_CHAR_ICO_PONCHO_PINK,
                               UI_OPT_CHAR_ICO_PONCHO_LIGHT,
                               UI_OPT_CHAR_ICO_PONCHO_DARK,
                               UI_OPT_CHAR_ICO_TUNIC,
                               UI_OPT_CHAR_ICO_SHORTS,
                               UI_OPT_CHAR_YORDA_SKIN,
                               UI_OPT_CHAR_YORDA_DRESS,
                               UI_OPT_CHAR_SWITCH,
                               UI_OPT_CHAR_RANDOMIZE,
                               UI_OPT_CHAR_RESET,
                               UI_OPT_BACK};
    static const int strs[] = {UI_STR_CHAR_ICO_SKIN,
                               UI_STR_CHAR_ICO_PONCHO_NAVY,
                               UI_STR_CHAR_ICO_PONCHO_PINK,
                               UI_STR_CHAR_ICO_PONCHO_LIGHT,
                               UI_STR_CHAR_ICO_PONCHO_DARK,
                               UI_STR_CHAR_ICO_TUNIC,
                               UI_STR_CHAR_ICO_SHORTS,
                               UI_STR_CHAR_YORDA_SKIN,
                               UI_STR_CHAR_YORDA_DRESS,
                               UI_STR_CHAR_SWITCH_YORDA,
                               UI_STR_CHAR_RANDOMIZE,
                               UI_STR_CHAR_RESET,
                               UI_STR_BACK};
    char p[1100], want[32], log[256];
    int lb[16], lo[16], lv[16];

    path(p, sizeof(p), "settings_test.toml");
    ui_SettingsSetTexturePackCount(NULL);
    ui_SettingsSetCharactersHost(NULL);

    for (int title = 1; title >= 0; title--) {
        const char *who = title ? "title" : "pause";
        const int l = openCharacters(title);
        const int n = ui_SettingsPageRows(UI_PAGE_CHARACTERS, lb, lo, lv, 16);
        CHECK(labelsAre(UI_PAGE_CHARACTERS, opts, strs, 13), "%s: Characters rows", who);
        CHECK(n == 13, "%s: thirteen rows (%d)", who, n);
        checkPageFits(UI_PAGE_CHARACTERS, title ? "Characters (title)" : "Characters (pause)");
        CHECK(lt_ext_Prop(lb[9])->defaultMask, "%s: Switch only inside the viewer", who);
        CHECK(lt_ext_Prop(lb[12])->dispY == 40 + 13 * 11, "%s: Back at y %d", who,
              lt_ext_Prop(lb[12])->dispY);

        /* every value starts Original, a swatch on each colour row, 9 apart
           from the label's top */
        for (int i = 0; i < ICO_APP_PART_COUNT; i++) {
            CHECK(strcmp(textNow(lv[i]), "Original") == 0, "%s: row %d starts Original (%s)", who,
                  i, lt_ext_RowText(lv[i]));
            const int sw = swatchOf(lb[i]);
            CHECK(sw >= 0, "%s: row %d has a swatch", who, i);
        }

        /* Ico: Skin: Original, Tone 1 ... Tone 12, Red ... Black (K-F),
           Original; Left wraps */
        for (int k = 1; k <= ICO_APP_TONES + ICO_APP_COLOURS; k++) {
            charStep(l, lb[0], 1);
            if (k <= ICO_APP_TONES) {
                snprintf(want, sizeof(want), "Tone %d", k);
            } else {
                snprintf(want, sizeof(want), "%s", kColourNames[k - ICO_APP_TONES - 1]);
            }
            CHECK(strcmp(textNow(lv[0]), want) == 0 && ico_appearance_get(ICO_APP_ICO_SKIN) == k,
                  "%s: Ico skin step %d is \"%s\" (\"%s\", %d)", who, k, want,
                  lt_ext_RowText(lv[0]), ico_appearance_get(ICO_APP_ICO_SKIN));
        }
        charStep(l, lb[0], 1);
        CHECK(strcmp(textNow(lv[0]), "Original") == 0 && ico_appearance_get(ICO_APP_ICO_SKIN) == 0,
              "%s: Black, Right: Original (37 values)", who);
        charStep(l, lb[0], -1);
        CHECK(strcmp(textNow(lv[0]), "Black") == 0, "%s: Original, Left: Black (%s)", who,
              lt_ext_RowText(lv[0]));
        for (int k = 0; k < ICO_APP_COLOURS; k++) {
            charStep(l, lb[0], -1);
        }
        CHECK(strcmp(textNow(lv[0]), "Tone 12") == 0, "%s: Left from Red: Tone 12 (%s)", who,
              lt_ext_RowText(lv[0]));
        charStep(l, lb[7], 1);
        CHECK(strcmp(textNow(lv[7]), "Tone 1") == 0 && ico_appearance_get(ICO_APP_YORDA_SKIN) == 1,
              "%s: Yorda skin, Right: Tone 1 (%s)", who, lt_ext_RowText(lv[7]));

        /* Ico: Tunic: 25 values with the palette's names, wrapping both ways */
        for (int k = 1; k <= ICO_APP_COLOURS; k++) {
            charStep(l, lb[5], 1);
            CHECK(strcmp(textNow(lv[5]), kColourNames[k - 1]) == 0 &&
                      ico_appearance_get(ICO_APP_ICO_TUNIC) == k,
                  "%s: Tunic step %d is %s (\"%s\")", who, k, kColourNames[k - 1],
                  lt_ext_RowText(lv[5]));
        }
        charStep(l, lb[5], 1);
        CHECK(strcmp(textNow(lv[5]), "Original") == 0, "%s: Black, Right: Original (25 values)",
              who);
        charStep(l, lb[5], -1);
        CHECK(strcmp(textNow(lv[5]), "Black") == 0, "%s: Original, Left: Black (%s)", who,
              lt_ext_RowText(lv[5]));
        charStep(l, lb[5], 1);
        CHECK(ico_appearance_get(ICO_APP_ICO_TUNIC) == 0, "%s: Right: Original again", who);

        /* the swatch follows the value: the part's colour at the GS scale,
           on the label's middle */
        {
            const int sw = swatchOf(lb[5]);
            unsigned char before[4] = {0}, after[4] = {0};
            CHECK(sw >= 0 && lt_ext_RectColor(sw, before) == 0, "%s: the tunic swatch", who);
            charStep(l, lb[5], 1);
            frame(0);
            CHECK(sw >= 0 && lt_ext_RectColor(sw, after) == 0 && memcmp(before, after, 3) != 0,
                  "%s: the tunic swatch changes with a step", who);
            const unsigned c = ico_appearance_swatch(ICO_APP_ICO_TUNIC);
            CHECK(after[0] == ((c >> 16) & 0xFFu) * 0x80u / 255u &&
                      after[1] == ((c >> 8) & 0xFFu) * 0x80u / 255u &&
                      after[2] == (c & 0xFFu) * 0x80u / 255u,
                  "%s: the swatch is the part's colour times 0x80/255 (%d %d %d, %06X)", who,
                  after[0], after[1], after[2], c);
            CHECK(sw >= 0 && lt_ext_Prop(sw)->dispY == lt_ext_Prop(lb[5])->dispY + 0 &&
                      lt_ext_Prop(sw)->dispX == 578,
                  "%s: the swatch at the label's y + 0 (%d vs %d)", who,
                  sw >= 0 ? lt_ext_Prop(sw)->dispY : -1, lt_ext_Prop(lb[5])->dispY);
            /* Square: back to Original, the swatch to the original colour */
            charStep(l, lb[5], 1);
            lt_ext_Layout(l)->curItem = lb[5];
            frame(0);
            press(0x80);
            CHECK(ico_appearance_get(ICO_APP_ICO_TUNIC) == 0 &&
                      strcmp(textNow(lv[5]), "Original") == 0,
                  "%s: Square puts the colour back (%s)", who, lt_ext_RowText(lv[5]));
            unsigned char orig[4] = {0};
            CHECK(sw >= 0 && lt_ext_RectColor(sw, orig) == 0 && memcmp(orig, before, 3) == 0,
                  "%s: the swatch is the original colour again", who);
        }

        /* the note: this entry's text; a texture pack's while one is on */
        CHECK(noteStarting(UI_PAGE_CHARACTERS,
                           title ? "The colours apply" : "The colours change") >= 0,
              "%s: the note", who);
        s_packCount = 3;
        ui_SettingsSetTexturePackCount(fakePackCount);
        for (int k = 0; k < 4; k++) {
            frame(0);
        }
        CHECK(noteStarting(UI_PAGE_CHARACTERS, "A texture pack is on") >= 0,
              "%s: the texture pack note with a pack installed", who);
        s_packCount = 0;
        for (int k = 0; k < 4; k++) {
            frame(0);
        }
        CHECK(noteStarting(UI_PAGE_CHARACTERS,
                           title ? "The colours apply" : "The colours change") >= 0,
              "%s: no pack, no pack note", who);
        ui_SettingsSetTexturePackCount(NULL);
    }

    /* one step, then Back: [characters] ico_tunic = "red" in the saved
       file, read back by a reload */
    for (int title = 1; title >= 0; title--) {
        const int l = openCharacters(title);
        ui_SettingsPageRows(UI_PAGE_CHARACTERS, lb, lo, lv, 16);
        charStep(l, lb[5], 1);
        lt_ext_Layout(l)->curItem = lb[12];
        frame(0);
        press(0x40);
        CHECK(settle(ui_SettingsPageLayout(UI_PAGE_EXTRAS), 60), "title %d: Back to Extras", title);
        IcoToml *t = ico_toml_load(p);
        const char *v = savedChar(t, ICO_APP_ICO_TUNIC);
        CHECK(v != NULL && strcmp(v, "red") == 0, "title %d: [characters] ico_tunic = \"red\" (%s)",
              title, v ? v : "none");
        ico_toml_free(t);
        ico_config_reset(p, "");
        ico_opt_reload();
        CHECK(ico_appearance_get(ICO_APP_ICO_TUNIC) == 1, "title %d: the reload reads it back",
              title);
    }

    /* Randomize, Reset */
    for (int title = 1; title >= 0; title--) {
        const char *who = title ? "title" : "pause";
        const int l = openCharacters(title);
        ui_SettingsPageRows(UI_PAGE_CHARACTERS, lb, lo, lv, 16);
        const unsigned serial = ico_appearance_serial();
        lt_ext_Layout(l)->curItem = lb[10];
        frame(0);
        press(0x40);
        int allSet = 1, distinct = 1;
        for (int i = 0; i < ICO_APP_PART_COUNT; i++) {
            allSet &=
                ico_appearance_get((IcoAppPart)i) != 0 && strcmp(textNow(lv[i]), "Original") != 0;
        }
        for (int a = ICO_APP_ICO_PONCHO_NAVY; a <= ICO_APP_ICO_PONCHO_DARK; a++) {
            for (int b = a + 1; b <= ICO_APP_ICO_PONCHO_DARK; b++) {
                distinct &= ico_appearance_get((IcoAppPart)a) != ico_appearance_get((IcoAppPart)b);
            }
        }
        CHECK(allSet, "%s: Randomize leaves no part Original", who);
        CHECK(distinct, "%s: the four poncho rows differ", who);
        CHECK(ico_appearance_serial() != serial, "%s: Randomize advances the serial", who);
        /* Reset */
        const unsigned serial2 = ico_appearance_serial();
        lt_ext_Layout(l)->curItem = lb[11];
        frame(0);
        press(0x40);
        int allOrig = 1;
        for (int i = 0; i < ICO_APP_PART_COUNT; i++) {
            allOrig &=
                ico_appearance_get((IcoAppPart)i) == 0 && strcmp(textNow(lv[i]), "Original") == 0;
        }
        CHECK(allOrig, "%s: Reset to original puts every part back", who);
        CHECK(ico_appearance_serial() != serial2, "%s: Reset advances the serial", who);
        /* Randomize again and leave: the nine keys are in the file */
        lt_ext_Layout(l)->curItem = lb[10];
        frame(0);
        press(0x40);
        lt_ext_Layout(l)->curItem = lb[12];
        frame(0);
        press(0x40);
        CHECK(settle(ui_SettingsPageLayout(UI_PAGE_EXTRAS), 60), "%s: Back to Extras", who);
        IcoToml *t = ico_toml_load(p);
        int saved = 1;
        for (int i = 0; i < ICO_APP_PART_COUNT; i++) {
            const char *v = savedChar(t, i);
            saved &= v != NULL && strcmp(v, "original") != 0;
        }
        CHECK(saved, "%s: Randomize is in the file on leaving", who);
        ico_toml_free(t);
    }

    /* package K-D: Characters from the title inside the model viewer.
       Extras' Characters row calls the host's enter and opens the page as
       the viewer's panel; it takes no input while a model loads; Left /
       Right, Square, Randomize and Reset as on the Options page; Switch
       and L1 / R1 load the other character; Triangle and Back leave
       through the host, the config written; the title then reopens
       Settings on Extras.  From the pause menu nothing of this. */
    {
        ui_SettingsSetCharactersHost(&kFakeHost);
        s_hostEnters = s_hostLeaves = s_hostFail = 0;
        s_hostShown = -2;
        s_hostSwitch = -1;
        const int mainL = enterMain(1);
        const int exL = openPage(mainL, 8, UI_PAGE_EXTRAS);
        const int l = ui_SettingsPageLayout(UI_PAGE_CHARACTERS);
        int el[8], ml[16], ex = -1;
        ui_SettingsPageRows(UI_PAGE_EXTRAS, el, NULL, NULL, 8);
        const int mn = ui_SettingsPageRows(UI_PAGE_MAIN, ml, NULL, NULL, 16);
        for (int i = 0; i < mn; i++) {
            if (strcmp(lt_ext_RowText(ml[i]), "Extras") == 0) {
                ex = ml[i];
            }
        }
        lt_ext_Layout(exL)->curItem = el[3];
        frame(0);
        CHECK(lt_ext_Prop(el[3])->right == -1,
              "title with the viewer: the Characters row is not a plain link (%d)",
              lt_ext_Prop(el[3])->right);
        CHECK(!ui_SettingsCharactersInViewer(), "the viewer not running Characters yet");

        /* the viewer cannot start: Extras stays, a log line */
        s_hostFail = 1;
        errCapture();
        press(0x40);
        errRelease(log, sizeof(log));
        CHECK(s_hostEnters == 1 && current_layout_id == exL && strstr(log, "appearance:") != NULL,
              "enter fails: Extras stays and logs (\"%s\")", log);
        s_hostFail = 0;

        /* Cross: the host's enter, the page as the viewer's panel */
        const int leaves = s_leaves;
        lt_ext_Layout(exL)->curItem = el[3];
        frame(0);
        press(0x40);
        CHECK(s_hostEnters == 2 && s_leaves == leaves + 1 && settle(l, 60),
              "Cross on Characters: the viewer's enter, then the page (%d enters, layout %d)",
              s_hostEnters, current_layout_id);
        for (int k = 0; k < 4; k++) {
            frame(0);
        }
        const int n = ui_SettingsPageRows(UI_PAGE_CHARACTERS, lb, lo, lv, 16);
        CHECK(n == 13 && ui_SettingsCharactersInViewer(), "the viewer's Characters (%d rows)", n);
        /* Ico's rows (he loads first), then Switch to Back */
        int shown = 0, prevY = -1, spaced = 1;
        for (int i = 0; i < n; i++) {
            const LtProperty *r = lt_ext_Prop(lb[i]);
            if (!r->defaultMask) {
                shown++;
                spaced &= prevY < 0 || r->dispY == prevY + 11;
                prevY = r->dispY;
            }
        }
        CHECK(shown == 11 && spaced && lt_ext_Prop(lb[0])->dispY == 30 &&
                  lt_ext_Prop(lb[12])->dispY == 30 + 11 * 10,
              "in the viewer: Ico's eleven rows 11 apart from 30 (%d shown, Back at %d)", shown,
              lt_ext_Prop(lb[12])->dispY);
        {
            int sws = 0;
            unsigned char c[4];
            for (int j = lt_ext_Layout(l)->first; j < lt_ext_Layout(l)->last; j++) {
                sws += lt_ext_RectColor(j, c) == 0 && !lt_ext_Prop(j)->defaultMask;
            }
            CHECK(sws == 7 && lt_ext_Prop(lv[8])->defaultMask, "Ico's 7 swatches (%d)", sws);
        }
        CHECK(lt_ext_Prop(lb[12])->dispY + lt_ext_Prop(lb[12])->dispH <= 226,
              "in the viewer: Back's box ends at %d",
              lt_ext_Prop(lb[12])->dispY + lt_ext_Prop(lb[12])->dispH);
        /* the panel at the left: label, arrows, value and swatch end by x
           348; no shade over the model */
        {
            unsigned char c[4];
            const LtProperty *lab = lt_ext_Prop(lb[5]), *val = lt_ext_Prop(lv[5]);
            int sw = -1;
            for (int j = lt_ext_Layout(l)->first; j < lt_ext_Layout(l)->last; j++) {
                /* on the letters' middle: 3 lines below the row's top */
                if (lt_ext_RectColor(j, c) == 0 && lt_ext_Prop(j)->dispY == lab->dispY + 3) {
                    sw = j;
                }
            }
            const LtProperty *ar = lt_ext_Prop(lv[5] + 2);
            CHECK(lab->dispX + lab->dispW <= val->dispX && val->dispX + val->dispW <= ar->dispX &&
                      ar->dispX + ar->dispW <= 348,
                  "in the viewer: the row in the panel (label %d+%d, value %d+%d, arrow %d+%d)",
                  lab->dispX, lab->dispW, val->dispX, val->dispW, ar->dispX, ar->dispW);
            CHECK(sw >= 0 && lt_ext_Prop(sw)->dispX >= ar->dispX + ar->dispW &&
                      lt_ext_Prop(sw)->dispX + lt_ext_Prop(sw)->dispW <= 348,
                  "in the viewer: the swatch beside the value (x %d, y %d)", lt_ext_Prop(sw)->dispX,
                  lt_ext_Prop(sw)->dispY);
            CHECK(lt_ext_Layout(l)->colA == 0.0f, "in the viewer: no shade (%g)",
                  (double)lt_ext_Layout(l)->colA);
        }
        /* the prompts: six words and eight of the game's glyphs, shown */
        {
            const LtProp *lay = lt_ext_Layout(l);
            int glyphs = 0, colour = 0;
            for (int j = lay->first; j < lay->last; j++) {
                const LtProperty *e = lt_ext_Prop(j);
                if (e->defaultMask) {
                    continue;
                }
                glyphs += lt_ext_IsGlyphRow(e) != 0;
                colour += strcmp(lt_ext_RowText(j), "Colour") == 0 ||
                          strcmp(lt_ext_RowText(j), "Right stick: turn") == 0;
            }
            CHECK(glyphs == 8 && colour == 2, "in the viewer: the prompts (%d glyphs, %d words)",
                  glyphs, colour);
        }

        /* while the model loads: no input */
        charStep(l, lb[5], 1);
        press(0x10);
        CHECK(ico_appearance_get(ICO_APP_ICO_TUNIC) == 0 && s_hostLeaves == 0 &&
                  current_layout_id == l,
              "loading: Right and Triangle do nothing (%d, %d leaves)",
              ico_appearance_get(ICO_APP_ICO_TUNIC), s_hostLeaves);

        /* Ico up: the Switch row names Yorda; a step changes the colour
           at once (the serial the textures follow), Square puts it back */
        s_hostShown = 0;
        const unsigned serial = ico_appearance_serial();
        charStep(l, lb[5], 1);
        CHECK(strcmp(textNow(lb[9]), "Switch to Yorda") == 0, "Ico shown: \"%s\"",
              lt_ext_RowText(lb[9]));
        CHECK(ico_appearance_get(ICO_APP_ICO_TUNIC) == 1 && strcmp(textNow(lv[5]), "Red") == 0 &&
                  ico_appearance_serial() != serial,
              "in the viewer: Right on Tunic is Red at once (%d, \"%s\")",
              ico_appearance_get(ICO_APP_ICO_TUNIC), lt_ext_RowText(lv[5]));
        charStep(l, lb[0], -1);
        CHECK(ico_appearance_get(ICO_APP_ICO_SKIN) == ICO_APP_TONES + ICO_APP_COLOURS,
              "in the viewer: Left on Skin wraps to Black (%d)",
              ico_appearance_get(ICO_APP_ICO_SKIN));
        lt_ext_Layout(l)->curItem = lb[0];
        frame(0);
        press(0x80);
        CHECK(ico_appearance_get(ICO_APP_ICO_SKIN) == 0, "in the viewer: Square is Original");
        /* Randomize and Reset: the character shown only */
        ico_appearance_set(ICO_APP_YORDA_DRESS, 3);
        lt_ext_Layout(l)->curItem = lb[10];
        frame(0);
        press(0x40);
        CHECK(ico_appearance_get(ICO_APP_ICO_SKIN) && ico_appearance_get(ICO_APP_ICO_SHORTS) &&
                  ico_appearance_get(ICO_APP_YORDA_DRESS) == 3,
              "Ico: Randomize, his only");
        lt_ext_Layout(l)->curItem = lb[11];
        frame(0);
        press(0x40);
        CHECK(!ico_appearance_get(ICO_APP_ICO_SKIN) && ico_appearance_get(ICO_APP_YORDA_DRESS) == 3,
              "Ico: Reset, his only");

        /* R1: Yorda; then the row names Ico, and Cross on it loads Ico */
        press(0x0008);
        CHECK(s_hostSwitch == 1 && current_layout_id == l, "R1: the viewer loads Yorda (%d)",
              s_hostSwitch);
        CHECK(!lt_ext_Prop(lb[7])->defaultMask && lt_ext_Prop(lb[0])->defaultMask,
              "R1: her rows as she loads");
        s_hostShown = 1;
        CHECK(strcmp(textNow(lb[9]), "Switch to Ico") == 0, "Yorda shown: \"%s\"",
              lt_ext_RowText(lb[9]));
        {
            /* her two rows, then Switch to Back: six from 30 */
            int k = 0;
            for (int i = 0; i < n; i++) {
                k += !lt_ext_Prop(lb[i])->defaultMask;
            }
            CHECK(k == 6 && lt_ext_Prop(lb[7])->dispY == 30 && lt_ext_Prop(lb[12])->dispY == 85,
                  "Yorda: six rows (%d, Back at %d)", k, lt_ext_Prop(lb[12])->dispY);
            ico_appearance_set(ICO_APP_ICO_TUNIC, 5);
            ico_appearance_set(ICO_APP_YORDA_SKIN, 0);
            lt_ext_Layout(l)->curItem = lb[10];
            frame(0);
            press(0x40);
            CHECK(ico_appearance_get(ICO_APP_YORDA_SKIN) &&
                      ico_appearance_get(ICO_APP_ICO_TUNIC) == 5,
                  "Yorda: Randomize, hers only");
            lt_ext_Layout(l)->curItem = lb[11];
            frame(0);
            press(0x40);
            CHECK(!ico_appearance_get(ICO_APP_YORDA_DRESS) &&
                      ico_appearance_get(ICO_APP_ICO_TUNIC) == 5,
                  "Yorda: Reset, hers only");
            ico_appearance_set(ICO_APP_ICO_TUNIC, 0);
        }
        lt_ext_Layout(l)->curItem = lb[9];
        frame(0);
        press(0x40);
        CHECK(s_hostSwitch == 0, "Cross on Switch: the viewer loads Ico (%d)", s_hostSwitch);
        s_hostShown = 0;
        press(0x0004);
        CHECK(s_hostSwitch == 1, "L1: Yorda again (%d)", s_hostSwitch);
        s_hostShown = 1;

        /* Triangle: the host's leave, the colours in the file */
        charStep(l, lb[8], 1);
        press(0x10);
        CHECK(s_hostLeaves == 1 && current_layout_id == l,
              "Triangle: the viewer leaves to the title (%d)", s_hostLeaves);
        {
            IcoToml *t = ico_toml_load(p);
            const char *v = savedChar(t, ICO_APP_YORDA_DRESS);
            CHECK(v != NULL && strcmp(v, "red") == 0,
                  "leaving the viewer writes [characters] yorda_dress = \"red\" (%s)",
                  v ? v : "none");
            ico_toml_free(t);
        }
        /* Cross on Back leaves too */
        s_hostShown = 0;
        lt_ext_Layout(l)->curItem = lb[12];
        frame(0);
        press(0x40);
        CHECK(s_hostLeaves == 2, "Cross on Back: the viewer leaves (%d)", s_hostLeaves);

        /* the title back: Settings on Extras, on its Characters row */
        s_hostShown = -2;
        CHECK(ui_SettingsReopenPage(UI_PAGE_EXTRAS) == -1,
              "Reopen from a layout that is not the title: -1");
        lt_switch_layout(13);
        CHECK(settle(13, 60), "the title again");
        CHECK(ui_SettingsReopenPage(UI_PAGE_EXTRAS) == exL, "Reopen Extras on the title");
        CHECK(ui_SettingsReopenPage(-1) == -1 && ui_SettingsReopenPage(UI_PAGE_COUNT) == -1,
              "Reopen of a bad page: -1");
        CHECK(lt_ext_Layout(mainL)->defaultItem == ex && lt_ext_Layout(exL)->defaultItem == el[3],
              "Reopen: Main on Extras (%d, want %d), Extras on Characters (%d, want %d)",
              lt_ext_Layout(mainL)->defaultItem, ex, lt_ext_Layout(exL)->defaultItem, el[3]);
        lt_switch_layout(exL);
        CHECK(settle(exL, 60) && lt_ext_Layout(exL)->curItem == el[3],
              "Extras opens on Characters");
        /* Reopen on the Characters page: Extras on its Characters row */
        CHECK(ui_SettingsReopenPage(UI_PAGE_CHARACTERS) == -1, "Reopen off the title layouts: -1");
        lt_switch_layout(13);
        CHECK(settle(13, 60) && ui_SettingsReopenPage(UI_PAGE_CHARACTERS) == l &&
                  lt_ext_Layout(exL)->defaultItem == el[3] &&
                  lt_ext_Layout(l)->defaultItem == lb[0],
              "Reopen Characters: Extras on Characters, the page on its first row");
        lt_switch_layout(exL);
        CHECK(settle(exL, 60), "Extras");
        press(0x10);
        CHECK(settle(mainL, 60) && lt_ext_Layout(mainL)->curItem == ex, "Triangle: Main on Extras");
        press(0x10);
        CHECK(settle(13, 60), "Triangle: the title");

        /* the pause menu with the host set: the Options page as before */
        const int enters = s_hostEnters;
        const int pl = openCharacters(0);
        ui_SettingsPageRows(UI_PAGE_CHARACTERS, lb, lo, lv, 16);
        CHECK(s_hostEnters == enters && pl == l && !ui_SettingsCharactersInViewer(),
              "pause: Characters is the page, not the viewer (%d enters)", s_hostEnters);
        {
            int el2[8];
            ui_SettingsPageRows(UI_PAGE_EXTRAS, el2, NULL, NULL, 8);
            CHECK(lt_ext_Prop(el2[3])->right == l, "pause: the Characters row links the page");
        }
        CHECK(lt_ext_Prop(lb[9])->defaultMask && lt_ext_Prop(lb[0])->dispX == 44 &&
                  lt_ext_Layout(l)->colA == 0.6f && lt_ext_Prop(lb[12])->dispY == 40 + 13 * 11,
              "pause: the Options page's places (Switch hidden, label x %d, Back y %d)",
              lt_ext_Prop(lb[0])->dispX, lt_ext_Prop(lb[12])->dispY);
        press(0x0008);
        CHECK(s_hostSwitch == 1 && current_layout_id == l, "pause: R1 does nothing");
        lt_ext_Layout(l)->curItem = lb[0];
        frame(0);
        press(0x40);
        CHECK(current_layout_id == l, "pause: Cross on a colour row does nothing");
        ui_SettingsSetCharactersHost(NULL);
        s_hostShown = -2;
    }

    /* Extras from the pause menu: Characters and Back only; the others
       masked; the cursor steps between the two */
    {
        const int mainL = enterMain(0);
        const int exL = openPage(mainL, 8, UI_PAGE_EXTRAS);
        int el[8];
        const int en = ui_SettingsPageRows(UI_PAGE_EXTRAS, el, NULL, NULL, 8);
        for (int k = 0; k < 4; k++) {
            frame(0);
        }
        CHECK(en == 5, "pause: Extras has its five rows (%d)", en);
        CHECK(lt_ext_Prop(el[0])->defaultMask && lt_ext_Prop(el[1])->defaultMask &&
                  lt_ext_Prop(el[2])->defaultMask,
              "pause: Music, Models and Credits are hidden");
        CHECK(!lt_ext_Prop(el[3])->defaultMask && !lt_ext_Prop(el[4])->defaultMask,
              "pause: Characters and Back are shown");
        lt_ext_Layout(exL)->curItem = el[3];
        frame(0);
        press(0x4000);
        CHECK(lt_ext_Layout(exL)->curItem == el[4], "pause: Down from Characters lands on Back");
        /* the budget keeps its spare (testBudget has the count) */
        printf("settings_extras_test: Characters: %d of %d properties used\n", lt_ext_PropCount(),
               LT_EXT_MAX_PROPERTIES);
    }

    /* Reopen with nothing built */
    ui_SettingsReset();
    CHECK(ui_SettingsReopenPage(UI_PAGE_CHARACTERS) == -1, "Reopen before the menu is built: -1");
    ui_SettingsSetTexturePackCount(NULL);
    useConfig("version = 1\n");
}

int main(int argc, char **argv)
{
    snprintf(s_dir, sizeof(s_dir), "%s", argc > 1 ? argv[1] : ".");
    setEnv("LC_ALL", NULL);
    setEnv("LANG", "en_GB.UTF-8");
    testExtras();
    testCoversTitle();
    testCharacters();
    testCharactersRestart();
    if (failures) {
        printf("settings_extras_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("settings_extras_test: ok\n");
    return 0;
}

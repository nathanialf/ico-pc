/* settings_extra_test.c: the newer Settings menu tests, over the shared
 * fixture (settings_fixture.h: fake tables, frame/settle/press, useConfig,
 * labelsAre, enterMain/openPage).  Each settings test file stays well under
 * the source size cap (tools/check_no_rom.sh), so new settings tests go here.
 *
 * One test function per feature, headed by what it tests, with its call in
 * main in the same order.
 */
#include "settings_fixture.h"
#include "popup.h"
#include "pointer.h"
#include "ui_mouse.h"

/* the fixture builds a layout and the menu opens */
static void testFixture(void)
{
    useConfig("version = 1\n");
    const int mainL = enterMain(1);
    CHECK(mainL == ui_SettingsPageLayout(UI_PAGE_MAIN), "title: the main page (%d)", mainL);
    CHECK(current_layout_id == mainL, "the main page is current");
    const int viaPause = enterMain(0);
    CHECK(viaPause == mainL, "pause menu: the main page (%d)", viaPause);
}

/* the Display > Window mode row */
static int s_i17cAnswer;

static int fakeI17cMode(void)
{
    return s_i17cAnswer;
}

/* the Display page's Window mode row is shown: the pages keep every row
   and mask the hidden ones when the page is laid out, so open it first */
static int i17cDisplayHasWindowMode(void)
{
    const int mainL = enterMain(0);

    openPage(mainL, 0, UI_PAGE_DISPLAY);
    const int row = ui_SettingsRowOf(UI_PAGE_DISPLAY, UI_OPT_WINDOW_MODE);
    return row >= 0 && !lt_ext_Prop(row)->defaultMask && !lt_ext_Prop(row)->masked;
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

/* Effects > Cinematic bars, the last switch before Back, On by
   default, a step flips it live and ui_SettingsSave writes the key; the other
   effects keep their value */
static void testCinematicBars(void)
{
    int rows[16], opts[16];
    char p[1100];

    useConfig("version = 1\n");
    enterMain(0);
    const int n = ui_SettingsPageRows(UI_PAGE_EFFECTS, rows, opts, NULL, 16);
    CHECK(n >= 3 && opts[n - 1] == UI_OPT_BACK && opts[n - 2] == UI_OPT_EFFECT_CINEMATIC_BARS &&
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

/* default_item_select hands the select callback the current
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

/* Settings > Graphics driver (a fake host), the Quit game label */
static int s_gpuN = 0, s_gpuSel = -1, s_gpuFailed = -1, s_gpuAdreno = 1;
static int s_gpuBegins, s_gpuPolls, s_gpuRemoved = -99, s_gpuSelects;
static int s_gpuResult[8], s_gpuResultN, s_gpuResultAt;
static const char *const kGpuNames[3] = {"Turnip 24.1", "Turnip 25.0", "Mesa"};

static int gpuCount(void)
{
    return s_gpuN;
}

static const char *gpuName(int i)
{
    return kGpuNames[i];
}

static int gpuSelected(void)
{
    return s_gpuSel;
}

static void gpuSelect(int i)
{
    s_gpuSel = i;
    s_gpuSelects++;
    s_gpuFailed = -1; /* as the real host: a new choice forgets the failure */
}

static int gpuLastFailed(int i)
{
    return i == s_gpuFailed;
}

static int gpuAdreno(void)
{
    return s_gpuAdreno;
}

static int gpuBegin(void)
{
    s_gpuBegins++;
    return 0;
}

static int gpuPoll(void)
{
    s_gpuPolls++;
    return s_gpuResultAt < s_gpuResultN ? s_gpuResult[s_gpuResultAt++] : UI_GPU_INSTALL_PENDING;
}

static void gpuRemove(int i)
{
    s_gpuRemoved = i;
    s_gpuN--;
}

static const UiGpuDriverHost kGpuHost = {gpuCount,  gpuName,  gpuSelected, gpuSelect, gpuLastFailed,
                                         gpuAdreno, gpuBegin, gpuPoll,     gpuRemove};

static int an22bShown(UiSettingsPage page, UiSettingsOpt opt)
{
    const int row = ui_SettingsRowOf(page, opt);
    return row >= 0 && !lt_ext_Prop(row)->defaultMask && !lt_ext_Prop(row)->masked;
}

/* the main page's Graphics driver link: its row, -1 */
static int an22bLink(void)
{
    int labels[16];
    const int n = ui_SettingsPageRows(UI_PAGE_MAIN, labels, NULL, NULL, 16);
    for (int i = 0; i < n; i++) {
        if (strcmp(lt_ext_RowText(labels[i]), "Graphics driver") == 0) {
            return labels[i];
        }
    }
    return -1;
}

static void an22bResult(int a, int b)
{
    s_gpuResult[0] = a;
    s_gpuResult[1] = b;
    s_gpuResultN = 2;
    s_gpuResultAt = 0;
}

static int s_an22bQuits;
static int s_an22bGame;

static void an22bQuit(void)
{
    s_an22bQuits++;
}

static int an22bIsGame(void)
{
    return s_an22bGame;
}

/* the text of a row of layout l starting with prefix, -1 */
static int an22bTextIn(int l, const char *text)
{
    const LtProp *lay = lt_ext_Layout(l);
    for (int j = lay->first; j < lay->last; j++) {
        if (strcmp(lt_ext_RowText(j), text) == 0) {
            return j;
        }
    }
    return -1;
}

static void testGpuDriver(void)
{
    ui_SettingsSetGpuDriverHost(NULL);
    ui_PopupReset();

    /* no host: no link; the Main page is the twelve it was */
    enterMain(1);
    frame(0);
    CHECK(an22bLink() >= 0 && lt_ext_Prop(an22bLink())->defaultMask,
          "no host: the Graphics driver link is hidden");

    /* a host: the link shows after Effects and opens the page */
    s_gpuN = 2;
    s_gpuSel = -1;
    s_gpuFailed = -1;
    s_gpuAdreno = 1;
    ui_SettingsSetGpuDriverHost(&kGpuHost);
    int mainL = enterMain(1);
    frame(0);
    {
        int labels[16];
        ui_SettingsPageRows(UI_PAGE_MAIN, labels, NULL, NULL, 16);
        const int link = an22bLink();
        CHECK(link == labels[2] && !lt_ext_Prop(link)->defaultMask,
              "host: the link shows after Effects (%d)", link);
        CHECK(lt_ext_Prop(link)->right == ui_SettingsPageLayout(UI_PAGE_GPU_DRIVER),
              "the link opens the page");
    }
    const int gpuL = openPage(mainL, 2, UI_PAGE_GPU_DRIVER);
    const int driver = ui_SettingsRowOf(UI_PAGE_GPU_DRIVER, UI_OPT_GPU_DRIVER);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_GPU_DRIVER), "Built-in") == 0, "Built-in at first");
    CHECK(an22bShown(UI_PAGE_GPU_DRIVER, UI_OPT_GPU_ADD), "Add shows on an Adreno");
    CHECK(!an22bShown(UI_PAGE_GPU_DRIVER, UI_OPT_GPU_REMOVE), "Remove is hidden on Built-in");

    /* Right steps Built-in, the first, the second, around; each choice goes to the host */
    lt_ext_Layout(gpuL)->curItem = driver;
    press(0x2000);
    CHECK(s_gpuSel == 0 && strcmp(ui_SettingsValueText(UI_OPT_GPU_DRIVER), "Turnip 24.1") == 0,
          "Right: the first driver (%d)", s_gpuSel);
    CHECK(an22bShown(UI_PAGE_GPU_DRIVER, UI_OPT_GPU_REMOVE), "Remove shows on a driver");
    ui_SettingsStep(UI_OPT_GPU_DRIVER, 1);
    CHECK(s_gpuSel == 1, "Right: the second driver (%d)", s_gpuSel);
    ui_SettingsStep(UI_OPT_GPU_DRIVER, 1);
    CHECK(s_gpuSel == -1, "Right wraps to Built-in (%d)", s_gpuSel);
    ui_SettingsStep(UI_OPT_GPU_DRIVER, -1);
    CHECK(s_gpuSel == 1, "Left wraps to the last driver (%d)", s_gpuSel);

    /* the one that did not start keeps its name on the row; the page's note
       says so (a sentence fits there, not in the value box) */
    s_gpuFailed = 1;
    frame(0);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_GPU_DRIVER), "Turnip 25.0") == 0, "failed: %s",
          ui_SettingsValueText(UI_OPT_GPU_DRIVER));
    {
        const int note = ui_SettingsNoteRowOf(UI_PAGE_GPU_DRIVER, UI_OPT_GPU_DRIVER);
        CHECK(note >= 0 && strstr(lt_ext_RowText(note), "did not start") != NULL,
              "failed: the note says so (%s)", note >= 0 ? lt_ext_RowText(note) : "-");
    }
    s_gpuFailed = -1;
    frame(0);
    {
        const int note = ui_SettingsNoteRowOf(UI_PAGE_GPU_DRIVER, UI_OPT_GPU_DRIVER);
        CHECK(note >= 0 && strstr(lt_ext_RowText(note), "Adreno") != NULL,
              "the note is back to the Adreno line");
    }
    /* as the real host leaves it after a failure (gpu_driver_android.c
       driver_failed): the choice back on Built-in, the driver remembered as
       failed; the note says so until a driver is chosen again */
    s_gpuSel = -1;
    s_gpuFailed = 1;
    frame(0);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_GPU_DRIVER), "Built-in") == 0,
          "after a failure: Built-in (%s)", ui_SettingsValueText(UI_OPT_GPU_DRIVER));
    {
        const int note = ui_SettingsNoteRowOf(UI_PAGE_GPU_DRIVER, UI_OPT_GPU_DRIVER);
        CHECK(note >= 0 && strstr(lt_ext_RowText(note), "did not start") != NULL,
              "after a failure: the note says so on Built-in (%s)",
              note >= 0 ? lt_ext_RowText(note) : "-");
    }
    ui_SettingsStep(UI_OPT_GPU_DRIVER, 1);
    frame(0);
    CHECK(s_gpuSel == 0 && s_gpuFailed == -1, "a new choice forgets the failure (%d %d)", s_gpuSel,
          s_gpuFailed);
    {
        const int note = ui_SettingsNoteRowOf(UI_PAGE_GPU_DRIVER, UI_OPT_GPU_DRIVER);
        CHECK(note >= 0 && strstr(lt_ext_RowText(note), "Adreno") != NULL,
              "a new choice: the note is the Adreno line again");
    }

    /* Add: the picker opens, the page polls every frame until it answers */
    frame(0);
    const int add = ui_SettingsRowOf(UI_PAGE_GPU_DRIVER, UI_OPT_GPU_ADD);
    lt_ext_Layout(gpuL)->curItem = add;
    s_gpuBegins = s_gpuPolls = 0;
    an22bResult(UI_GPU_INSTALL_PENDING, UI_GPU_INSTALL_ADDED);
    press(0x40);
    CHECK(s_gpuBegins == 1, "Cross on Add opens the picker (%d)", s_gpuBegins);
    for (int k = 0; k < 4; k++) {
        frame(0);
    }
    CHECK(s_gpuPolls == 2, "polled until the answer, then stopped (%d)", s_gpuPolls);
    CHECK(strcmp(ui_PopupTitle(), "Driver added") == 0, "added: \"%s\"", ui_PopupTitle());

    static const struct {
        int result;
        const char *title;
    } kAnswers[] = {{UI_GPU_INSTALL_BAD, "This file is not a driver package."},
                    {UI_GPU_INSTALL_NOSPACE, "Not enough space to add this driver."},
                    {UI_GPU_INSTALL_CANCELLED, ""}};

    for (unsigned i = 0; i < sizeof(kAnswers) / sizeof(kAnswers[0]); i++) {
        ui_PopupReset();
        an22bResult(kAnswers[i].result, kAnswers[i].result);
        press(0x40);
        frame(0);
        frame(0);
        CHECK(strcmp(ui_PopupTitle(), kAnswers[i].title) == 0, "answer %d: \"%s\"",
              kAnswers[i].result, ui_PopupTitle());
        CHECK((kAnswers[i].title[0] != '\0') == ui_PopupActive(), "answer %d: popup or silence",
              kAnswers[i].result);
    }

    /* Remove: Built-in is chosen, the host deletes the chosen one, a box says so */
    ui_PopupReset();
    s_gpuSel = 1;
    frame(0);
    lt_ext_Layout(gpuL)->curItem = ui_SettingsRowOf(UI_PAGE_GPU_DRIVER, UI_OPT_GPU_REMOVE);
    press(0x40);
    CHECK(s_gpuRemoved == 1 && s_gpuSel == -1, "Remove: driver 1 deleted, Built-in chosen (%d %d)",
          s_gpuRemoved, s_gpuSel);
    CHECK(strcmp(ui_PopupTitle(), "Driver removed") == 0, "removed: \"%s\"", ui_PopupTitle());
    frame(0);
    CHECK(!an22bShown(UI_PAGE_GPU_DRIVER, UI_OPT_GPU_REMOVE), "Remove hides again");

    /* a phone without Adreno graphics: no Add on the page, and the page's
       link on the main menu is hidden */
    s_gpuAdreno = 0;
    frame(0);
    CHECK(!an22bShown(UI_PAGE_GPU_DRIVER, UI_OPT_GPU_ADD), "non-Adreno: Add is hidden");
    s_gpuBegins = 0;
    lt_ext_Layout(gpuL)->curItem = add;
    press(0x40);
    CHECK(s_gpuBegins == 0, "a hidden Add does nothing");
    press(0x10);
    CHECK(settle(mainL, 60), "Triangle: back to the menu");
    for (int k = 0; k < 4; k++) {
        frame(0); /* the main page refreshes */
    }
    CHECK(an22bLink() >= 0 && lt_ext_Prop(an22bLink())->defaultMask,
          "non-Adreno: the Graphics driver link is hidden");
    s_gpuAdreno = 1;
    for (int k = 0; k < 4; k++) {
        frame(0);
    }
    CHECK(an22bLink() >= 0 && !lt_ext_Prop(an22bLink())->defaultMask,
          "Adreno again: the link shows");

    /* the Main page with the link and Developer mode: thirteen rows fit */
    ico_opt_set_developer_mode(1);
    for (int k = 0; k < 4; k++) {
        frame(0);
    }
    {
        int labels[16], prev = -1, shown = 0, last = -1;
        const int n = ui_SettingsPageRows(UI_PAGE_MAIN, labels, NULL, NULL, 16);
        for (int i = 0; i < n; i++) {
            const LtProperty *r = lt_ext_Prop(labels[i]);
            if (r->defaultMask) {
                continue;
            }
            CHECK(prev < 0 ? r->dispY >= 34 : r->dispY >= prev + 12, "row %d at y %d after %d", i,
                  r->dispY, prev);
            prev = r->dispY;
            last = labels[i];
            shown++;
        }
        CHECK(shown == 13, "thirteen rows shown (%d)", shown);
        CHECK(last >= 0 && lt_ext_Prop(last)->dispY == 40 + 12 * 12 &&
                  lt_ext_Prop(last)->dispY + lt_ext_Prop(last)->dispH <= 226,
              "Back at %d ends at %d", last >= 0 ? lt_ext_Prop(last)->dispY : -1,
              last >= 0 ? lt_ext_Prop(last)->dispY + lt_ext_Prop(last)->dispH : -1);
    }
    ico_opt_set_developer_mode(0);
    ui_SettingsSetGpuDriverHost(NULL);
    frame(0);
    CHECK(lt_ext_Prop(an22bLink())->defaultMask, "the host removed: the link hides again");

    /* the strings, in all five languages */
    static const UiStrId kIds[] = {
        UI_STR_SECTION_GPU_DRIVER, UI_STR_OPT_GPU_DRIVER,     UI_STR_VAL_GPU_BUILTIN,
        UI_STR_GPU_DRIVER_ADD,     UI_STR_GPU_DRIVER_REMOVE,  UI_STR_GPU_DRIVER_NOTE,
        UI_STR_GPU_DRIVER_ADDED,   UI_STR_GPU_DRIVER_BAD,     UI_STR_GPU_DRIVER_REMOVED,
        UI_STR_GPU_DRIVER_FAILED,  UI_STR_GPU_DRIVER_NOSPACE, UI_STR_QUIT_GAME,
        UI_STR_QUIT_GAME_CONFIRM};
    static const UiLang kLangs[5] = {UI_LANG_EN, UI_LANG_FR, UI_LANG_DE, UI_LANG_IT, UI_LANG_ES};
    for (int l = 0; l < 5; l++) {
        for (unsigned i = 0; i < sizeof(kIds) / sizeof(kIds[0]); i++) {
            CHECK(ui_StrIn(kLangs[l], kIds[i])[0] != '\0', "language %d, string %d", l, kIds[i]);
        }
    }
    CHECK(strcmp(ui_StrIn(UI_LANG_ES, UI_STR_QUIT_GAME_CONFIRM), "\xC2\xBFSalir del juego?") == 0,
          "Spanish question");
}

static void testQuitGame(void)
{
    static const char *const kAsk[2] = {"Quit to desktop?", "Quit the game?"};
    static const char *const kRow[2] = {"Quit to desktop", "Quit game"};

    for (int game = 0; game < 2; game++) {
        useConfig("version = 1\n");
        fakeTables();
        lt_ext_Reset();
        ui_SettingsReset();
        memset(pad, 0, sizeof(pad));
        pad[0].ana[0] = pad[0].ana[1] = pad[0].ana[2] = pad[0].ana[3] = 128;
        NonLinearCameraMove = 2;
        s_an22bQuits = 0;
        s_an22bGame = game;
        ui_SettingsSetQuitHandler(an22bQuit);
        ui_SettingsSetQuitIsGame(game ? an22bIsGame : NULL);
        init_layout_texture(2);
        settle(54, 4);
        const int q13 = ui_SettingsQuitRow(13), ql = ui_QuitScreenLayout();
        CHECK(strcmp(lt_ext_RowText(q13), kRow[game]) == 0, "game %d: the row is \"%s\"", game,
              lt_ext_RowText(q13));
        CHECK(an22bTextIn(ql, kAsk[game]) >= 0, "game %d: the question \"%s\"", game, kAsk[game]);

        lt_switch_layout(13);
        CHECK(settle(13, 60), "the title");
        press(0x4000);
        press(0x4000);
        press(0x40);
        CHECK(settle(ql, 60), "the confirmation");
        press(0x8000); /* Yes */
        press(0x40);
        CHECK(s_an22bQuits == 1, "game %d: Yes still quits (%d)", game, s_an22bQuits);
    }

    /* a hook installed after the menu was built: the labels follow */
    const int q13 = ui_SettingsQuitRow(13), ql = ui_QuitScreenLayout();
    s_an22bGame = 0;
    ui_SettingsSetQuitIsGame(an22bIsGame);
    CHECK(strcmp(lt_ext_RowText(q13), "Quit to desktop") == 0 &&
              an22bTextIn(ql, "Quit to desktop?") >= 0,
          "a hook answering 0 keeps the desktop words");
    s_an22bGame = 1;
    ui_SettingsSetQuitIsGame(an22bIsGame);
    CHECK(strcmp(lt_ext_RowText(q13), "Quit game") == 0 && an22bTextIn(ql, "Quit the game?") >= 0,
          "a late hook answering 1 changes the words");
    ui_SettingsSetQuitIsGame(NULL);
    ui_SettingsSetQuitHandler(NULL);
    CHECK(strcmp(lt_ext_RowText(q13), "Quit to desktop") == 0,
          "no hook: back to the desktop words");
}

/* Controls' mouse rows: Mouse camera and Invert mouse up/down are On and
   Off on the live table, saved as [input] mouse_camera and mouse_invert_y;
   Mouse camera speed, Mouse camera range and Camera swings back follow
   Invert (mouse_camera_speed, mouse_full_range, mouse_return, written only
   when changed); the six mouse rows come after Hold type, hidden on
   Android; fifteen rows (the pause menu with a touch screen) fit 11 lines
   apart */
static int s_i17aTouch;

static int i17aTouch(void)
{
    return s_i17aTouch;
}

static void testMouseCamera(void)
{
    static const UiSettingsOpt kMouse[6] = {UI_OPT_MOUSE_CAMERA, UI_OPT_MOUSE_SENS,
                                            UI_OPT_MOUSE_INVERT, UI_OPT_MOUSE_SPEED,
                                            UI_OPT_MOUSE_RANGE,  UI_OPT_MOUSE_RETURN};
    IcoBindings *b = ico_input_live_bindings();
    char p[1100];

    useConfig("version = 1\n");
    ico_input_reload_bindings(b);
    CHECK(b->mouse_camera == 1 && b->mouse_invert_y == 0 && b->mouse_hold == 0.75f,
          "defaults: camera on, not inverted, hold 0.75 (%d, %d, %.2f)", b->mouse_camera,
          b->mouse_invert_y, (double)b->mouse_hold);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_MOUSE_CAMERA), "On") == 0 &&
              strcmp(ui_SettingsValueText(UI_OPT_MOUSE_INVERT), "Off") == 0,
          "values: On, Off (%s)", ui_SettingsValueText(UI_OPT_MOUSE_CAMERA));
    CHECK(strcmp(ui_Str(UI_STR_OPT_MOUSE_INVERT), "Invert mouse up/down") == 0,
          "the Invert row's label");
    CHECK(b->mouse_camera_speed == 1.0f && b->mouse_full_range == 0 && b->mouse_return == 1,
          "defaults: speed 1, normal range, swings back");
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_MOUSE_SPEED), "1.0x") == 0 &&
              strcmp(ui_SettingsValueText(UI_OPT_MOUSE_RANGE), "Normal") == 0 &&
              strcmp(ui_SettingsValueText(UI_OPT_MOUSE_RETURN), "On") == 0,
          "values: 1.0x, Normal, On");
    CHECK(strcmp(ui_Str(UI_STR_OPT_MOUSE_SPEED), "Mouse camera speed") == 0 &&
              strcmp(ui_Str(UI_STR_OPT_MOUSE_RANGE), "Mouse camera range") == 0 &&
              strcmp(ui_Str(UI_STR_OPT_MOUSE_RETURN), "Camera swings back") == 0,
          "the three new labels");

    /* each step flips the live table at once; the save writes both keys */
    ui_SettingsStep(UI_OPT_MOUSE_CAMERA, 1);
    CHECK(b->mouse_camera == 0 && strcmp(ui_SettingsValueText(UI_OPT_MOUSE_CAMERA), "Off") == 0,
          "Right: the mouse camera off");
    ui_SettingsStep(UI_OPT_MOUSE_INVERT, -1);
    CHECK(b->mouse_invert_y == 1 && strcmp(ui_SettingsValueText(UI_OPT_MOUSE_INVERT), "On") == 0,
          "Left: inverted");
    CHECK(ui_SettingsSave() == 0, "save");
    path(p, sizeof(p), "settings_test.toml");
    IcoToml *t = ico_toml_load(p);
    CHECK(t && ico_toml_get_bool(t, "input.mouse_camera", 1) == 0 &&
              ico_toml_get_bool(t, "input.mouse_invert_y", 0) == 1,
          "saved: mouse_camera = false, mouse_invert_y = true");
    CHECK(t && ico_toml_get(t, "input.mouse_hold") == NULL, "the hold not written (default)");
    CHECK(t && ico_toml_get(t, "input.mouse_camera_speed") == NULL &&
              ico_toml_get(t, "input.mouse_full_range") == NULL &&
              ico_toml_get(t, "input.mouse_return") == NULL,
          "the three new keys not written (defaults)");
    ico_toml_free(t);
    ico_input_reload_bindings(b);
    CHECK(b->mouse_camera == 0 && b->mouse_invert_y == 1, "read back");
    ui_SettingsStep(UI_OPT_MOUSE_CAMERA, 1);
    ui_SettingsStep(UI_OPT_MOUSE_INVERT, 1);
    CHECK(b->mouse_camera == 1 && b->mouse_invert_y == 0, "stepped back");

    /* the speed steps 0.5, 1, 1.5, 2, 3, 5, Instant and stops at the ends */
    {
        static const char *const kText[7] = {"0.5x", "1.0x", "1.5x",   "2.0x",
                                             "3.0x", "5.0x", "Instant"};
        for (int i = 2; i < 7; i++) {
            ui_SettingsStep(UI_OPT_MOUSE_SPEED, 1);
            CHECK(strcmp(ui_SettingsValueText(UI_OPT_MOUSE_SPEED), kText[i]) == 0,
                  "speed step up to %s (%s)", kText[i], ui_SettingsValueText(UI_OPT_MOUSE_SPEED));
        }
        CHECK(b->mouse_camera_speed == 10.0f, "Instant is 10");
        ui_SettingsStep(UI_OPT_MOUSE_SPEED, 1);
        CHECK(b->mouse_camera_speed == 10.0f, "speed stops at Instant");
        for (int i = 5; i >= 0; i--) {
            ui_SettingsStep(UI_OPT_MOUSE_SPEED, -1);
            CHECK(strcmp(ui_SettingsValueText(UI_OPT_MOUSE_SPEED), kText[i]) == 0,
                  "speed step down to %s (%s)", kText[i], ui_SettingsValueText(UI_OPT_MOUSE_SPEED));
        }
        ui_SettingsStep(UI_OPT_MOUSE_SPEED, -1);
        CHECK(b->mouse_camera_speed == 0.5f, "speed stops at 0.5x");
        ui_SettingsStep(UI_OPT_MOUSE_RANGE, 1);
        ui_SettingsStep(UI_OPT_MOUSE_RETURN, 1);
        CHECK(b->mouse_full_range == 1 && b->mouse_return == 0 &&
                  strcmp(ui_SettingsValueText(UI_OPT_MOUSE_RANGE), "Full") == 0 &&
                  strcmp(ui_SettingsValueText(UI_OPT_MOUSE_RETURN), "Off") == 0,
              "range Full, swings back Off");
        CHECK(ui_SettingsSave() == 0, "save the three");
        t = ico_toml_load(p);
        CHECK(t && ico_toml_get_float(t, "input.mouse_camera_speed", 1.0f) == 0.5f &&
                  ico_toml_get_bool(t, "input.mouse_full_range", 0) == 1 &&
                  ico_toml_get_bool(t, "input.mouse_return", 1) == 0,
              "saved: mouse_camera_speed = 0.5, mouse_full_range = true, mouse_return = false");
        ico_toml_free(t);
        ico_input_reload_bindings(b);
        CHECK(b->mouse_camera_speed == 0.5f && b->mouse_full_range == 1 && b->mouse_return == 0,
              "the three read back");
        /* back to the defaults: the keys the file now names are kept */
        ui_SettingsStep(UI_OPT_MOUSE_SPEED, 1);
        ui_SettingsStep(UI_OPT_MOUSE_RANGE, 1);
        ui_SettingsStep(UI_OPT_MOUSE_RETURN, 1);
        CHECK(b->mouse_camera_speed == 1.0f && b->mouse_full_range == 0 && b->mouse_return == 1,
              "the three back at their defaults");
    }

    /* the rows: after Hold type from the pause menu, after Remap on the
       title; hidden on Android */
    for (int android = 0; android < 2; android++) {
        ico_video_set_android(android);
        for (int title = 1; title >= 0; title--) {
            ui_SettingsSetTouchQuery(NULL);
            const int mainL = enterMain(title);
            const int ctlL = openPage(mainL, 4, UI_PAGE_CONTROLS);
            for (int i = 0; i < 6; i++) {
                const int row = ui_SettingsRowOf(UI_PAGE_CONTROLS, kMouse[i]);
                const int shown =
                    row >= 0 && !lt_ext_Prop(row)->defaultMask && !lt_ext_Prop(row)->masked;
                CHECK(shown == !android, "android %d, title %d: mouse row %d %s", android, title, i,
                      android ? "hidden" : "shown");
            }
            const int remap = ui_SettingsRowOf(UI_PAGE_CONTROLS, UI_OPT_LINK);
            const int below = lt_ext_Prop(remap)->downItem;
            const UiSettingsOpt want = android
                                           ? (title ? UI_OPT_CIRCLE_BACK : UI_OPT_BUTTON_CONFIG)
                                           : (title ? UI_OPT_MOUSE_CAMERA : UI_OPT_BUTTON_CONFIG);
            CHECK(below == ui_SettingsRowOf(UI_PAGE_CONTROLS, want),
                  "android %d, title %d: down from Remap (%d)", android, title, below);
            if (!title && !android) {
                const int hold = ui_SettingsRowOf(UI_PAGE_CONTROLS, UI_OPT_HOLD_TYPE);
                CHECK(lt_ext_Prop(hold)->downItem ==
                          ui_SettingsRowOf(UI_PAGE_CONTROLS, UI_OPT_MOUSE_CAMERA),
                      "pause: Mouse camera after Hold type");
            }
            if (!android) {
                /* Invert, then Speed, Range, Swings back, then Circle back */
                static const UiSettingsOpt kOrder[5] = {UI_OPT_MOUSE_INVERT, UI_OPT_MOUSE_SPEED,
                                                        UI_OPT_MOUSE_RANGE, UI_OPT_MOUSE_RETURN,
                                                        UI_OPT_CIRCLE_BACK};
                for (int i = 0; i < 4; i++) {
                    CHECK(lt_ext_Prop(ui_SettingsRowOf(UI_PAGE_CONTROLS, kOrder[i]))->downItem ==
                              ui_SettingsRowOf(UI_PAGE_CONTROLS, kOrder[i + 1]),
                          "title %d: row order after mouse row %d", title, i);
                }
            }
            (void)ctlL;
            press(0x10);
            CHECK(settle(mainL, 60), "Controls: back");
        }
    }
    ico_video_set_android(0);

    /* fifteen rows with a touch screen from the pause menu: 11 lines apart
       from 30, the last box inside the 226 lines */
    s_i17aTouch = 1;
    ui_SettingsSetTouchQuery(i17aTouch);
    const int mainL = enterMain(0);
    openPage(mainL, 4, UI_PAGE_CONTROLS);
    for (int k = 0; k < 4; k++) {
        frame(0);
    }
    int labels[16], n = 0, prev = -1, last = -1;
    const int c = ui_SettingsPageRows(UI_PAGE_CONTROLS, labels, NULL, NULL, 16);
    for (int i = 0; i < c; i++) {
        const LtProperty *r = lt_ext_Prop(labels[i]);
        if (r->defaultMask) {
            continue;
        }
        CHECK(prev < 0 ? r->dispY >= 28 : r->dispY >= prev + 11,
              "fifteen rows: row %d at y %d (the one above at %d)", i, r->dispY, prev);
        prev = r->dispY;
        last = labels[i];
        n++;
    }
    CHECK(n == 15, "fifteen rows shown (%d)", n);
    CHECK(last >= 0 && lt_ext_Prop(last)->dispY + lt_ext_Prop(last)->dispH <= 226,
          "fifteen rows: the last box ends at %d",
          last >= 0 ? lt_ext_Prop(last)->dispY + lt_ext_Prop(last)->dispH : -1);
    press(0x10);
    CHECK(settle(mainL, 60), "Controls: back");
    ui_SettingsSetTouchQuery(NULL);
}

/* the title run unseen while Options is to reopen */
static int rowsMasked(int layout, int want)
{
    const LtProp *lp = lt_ext_Layout(layout);
    for (int r = lp->first; r < lp->last; r++) {
        if (lt_ext_Prop(r)->masked != want) {
            return 0;
        }
    }
    return 1;
}

static void testTitleReturn(void)
{
    for (int title = 12; title <= 13; title++) {
        enterMain(1);
        lt_switch_layout(title);
        CHECK(settle(title, 60), "the title %d", title);
        const int opt = ui_SettingsEntryRow(title), quit = ui_SettingsQuitRow(title);

        /* nothing pending: the entry rows follow the mask, nothing else */
        CHECK(!ui_SettingsTitleReturnPending() && !ui_SettingsCoversTitle(),
              "%d: nothing pending, the logo shows", title);
        ui_SettingsTitleMask(1);
        CHECK(!ui_SettingsTitleDecided() && lt_ext_Prop(opt)->masked && lt_ext_Prop(quit)->masked,
              "%d: unchanged: the entry rows masked", title);
        ui_SettingsTitleMask(0);
        CHECK(ui_SettingsTitleDecided() && !lt_ext_Prop(opt)->masked && !lt_ext_Prop(quit)->masked,
              "%d: unchanged: unmasked once decided", title);
        CHECK(ui_SettingsEntryItem(51) == 0 && ui_SettingsEntryItem(opt) == 1,
              "%d: unchanged: only the entry rows", title);

        /* armed: the logo is down from this frame, the rows masked, no confirm */
        ui_SettingsTitleReturn(UI_PAGE_EXTRAS);
        CHECK(ui_SettingsTitleReturnPending() && ui_SettingsCoversTitle(), "%d: armed covers",
              title);
        CHECK(ui_SettingsEntryItem(51) == 1 && ui_SettingsEntryItem(0) == 1,
              "%d: every item is an entry item while pending", title);
        lt_item_select_disable = 0;
        ui_SettingsTitleMask(1);
        CHECK(rowsMasked(title, 1) && lt_ext_Prop(opt)->masked && lt_ext_Prop(quit)->masked &&
                  lt_item_select_disable == 1 && !ui_SettingsTitleDecided(),
              "%d: TitleMask(1): rows masked, selection disabled, undecided", title);
        lt_item_select_disable = 0;
        ui_SettingsTitleMask(0);
        CHECK(rowsMasked(title, 1) && lt_ext_Prop(opt)->masked && lt_item_select_disable == 1 &&
                  ui_SettingsTitleDecided(),
              "%d: TitleMask(0): still masked, decided", title);

        /* cancelled */
        ui_SettingsTitleReturn(-1);
        CHECK(!ui_SettingsTitleReturnPending() && !ui_SettingsCoversTitle() &&
                  ui_SettingsEntryItem(51) == 0,
              "%d: cancelled: the logo and the confirms are back", title);

        /* the reopen ends it; Back from Main returns to this title layout */
        ui_SettingsTitleReturn(UI_PAGE_EXTRAS);
        ui_SettingsTitleMask(0);
        const int exL = ui_SettingsReopenPage(UI_PAGE_EXTRAS);
        CHECK(exL >= 0 && !ui_SettingsTitleReturnPending(), "%d: the reopen clears pending", title);
        CHECK(ui_SettingsCoversTitle(), "%d: the logo stays down through the switch", title);
        lt_switch_layout(exL);
        CHECK(settle(exL, 60) && ui_SettingsCoversTitle(), "%d: Extras up", title);
        press(0x10);
        CHECK(settle(ui_SettingsPageLayout(UI_PAGE_MAIN), 60), "%d: Back: Main", title);
        press(0x10);
        CHECK(settle(title, 60), "%d: Back: the title layout (%d)", title, current_layout_id);
    }
}

/* the mouse pointer in the menus (ui_mouse.h), through the real
   layout code: a 4:3 view of 640 x 480 at 0,0 (the grid's 640 x 448 is the
   4:3 picture, font.h), so a point of the grid is the pointer at gx / 640
   across and (gy - 2) / 448 down */
static int s_i17bDeciding;

/* a title proc as layout_action.c's while the memory card check runs: the
   item select off; the rows shown (the real procs mask the Settings rows
   too while deciding, ui_SettingsTitleMask, which would hide the flag's
   effect here) */
static int i17bTitleProc(int first, int item)
{
    (void)first;
    (void)item;
    lt_mask_property(51, 0);
    ui_SettingsTitleMask(0);
    if (s_i17bDeciding) {
        lt_item_select_disable = 1;
    }
    return -1;
}

/* one Main tick as main.c runs it: the pad read (flags), the pointer's
   tick, the layouts */
static void mouseFrameWith(int flags)
{
    pad[0].flags = flags;
    pad[0].now = flags;
    ui_MouseTick();
    frame(pad[0].flags);
}

static void mouseFrame(void)
{
    mouseFrameWith(0);
}

/* the pointer on the middle of row j (or dx grid pixels off it) */
static void pointAtOff(int j, float dx)
{
    const LtProperty *e = lt_ext_Prop(j);
    const int w = e->dispW ? e->dispW : e->texW;
    const int h = e->dispH ? e->dispH : e->texH;
    const float gx = (e->centerX ? 320.0f : (float)e->dispX + (float)w * 0.5f) + dx;
    const float gy = 2.0f * (float)e->dispY + (float)h * 0.5f;
    ico_pointer_move(gx / 640.0f, (gy - 2.0f) / 448.0f); /* a 640 x 480 view */
}

static void pointAt(int j)
{
    pointAtOff(j, 0.0f);
}

static void click(void)
{
    ico_pointer_button(1);
    ico_pointer_button(0);
}

/* the main page's row labelled strId, among all its rows (openPage's index) */
static int i17bMainIndex(int strId)
{
    int labels[16];
    const int n = ui_SettingsPageRows(UI_PAGE_MAIN, labels, NULL, NULL, 16);
    for (int i = 0; i < n; i++) {
        if (strcmp(lt_ext_RowText(labels[i]), ui_Str((UiStrId)strId)) == 0) {
            return i;
        }
    }
    return -1;
}

/* the arrow of the value of label in layout l with role, -1 */
static int i17bArrow(int l, int label, int role)
{
    const LtProp *lay = lt_ext_Layout(l);
    for (int j = lay->first; j < lay->last; j++) {
        if (lt_ext_Prop(j)->ownerItem == label && lt_ext_PointerRole(j) == role) {
            return j;
        }
    }
    return -1;
}

static void testPointer(void)
{
    char before[64], after[64];
    int labels[16];

    ui_MouseReset();
    ui_MouseSetView(640, 480, 0, 0, 640, 480);

    /* play (layout 54) is no menu */
    useConfig("version = 1\n");
    fakeTables();
    lt_ext_Reset();
    ui_SettingsReset();
    memset(pad, 0, sizeof(pad));
    pad[0].ana[0] = pad[0].ana[1] = pad[0].ana[2] = pad[0].ana[3] = 128;
    init_layout_texture(2);
    settle(54, 4);
    ico_pointer_move(0.5f, 0.5f);
    mouseFrame();
    CHECK(!ui_MouseMenuActive() && !ico_pointer_menu(), "layout 54: no menu");

    /* the title while its memory card check runs: the item select off, so
       pointing at the Settings row moves nothing until it is decided */
    lt_switch_layout(13);
    CHECK(settle(13, 60), "pointer: the title");
    texLayout[13].proc = i17bTitleProc;
    s_i17bDeciding = 1;
    mouseFrame();
    mouseFrame();
    const int entry = ui_SettingsEntryRow(13);
    CHECK(texLayout[13].curItem == 51, "the title on New Game (%d)", texLayout[13].curItem);
    CHECK(ico_pointer_menu() == 1, "the title is a menu");
    pointAt(entry);
    mouseFrame();
    CHECK(texLayout[13].curItem == 51, "deciding: the pointer moves nothing (%d)",
          texLayout[13].curItem);
    s_i17bDeciding = 0;
    mouseFrame(); /* the tick that decided */
    pointAt(entry);
    mouseFrame();
    CHECK(texLayout[13].curItem == entry, "decided: the pointer on the Settings row (%d, want %d)",
          texLayout[13].curItem, entry);
    texLayout[13].proc = NULL;

    /* the main page: hover moves the cursor, a click opens the page */
    const int mainL = enterMain(1);
    const int iDisp = i17bMainIndex(UI_STR_SECTION_DISPLAY);
    const int iAch = i17bMainIndex(UI_STR_SECTION_ACHIEVEMENTS);
    const int iCtl = i17bMainIndex(UI_STR_SECTION_CONTROLS);
    ui_SettingsPageRows(UI_PAGE_MAIN, labels, NULL, NULL, 16);
    CHECK(iDisp >= 0 && iAch >= 0 && iCtl >= 0, "the main page's rows (%d %d %d)", iDisp, iAch,
          iCtl);
    lt_ext_Layout(mainL)->curItem = labels[iCtl];
    mouseFrame();
    mouseFrame();
    CHECK(ico_pointer_menu() == 1 && ui_MouseMenuActive(), "the main page is a menu");
    pointAt(labels[iDisp]);
    mouseFrame();
    CHECK(lt_ext_Layout(mainL)->curItem == labels[iDisp], "hover: the cursor on Display (%d)",
          lt_ext_Layout(mainL)->curItem);
    CHECK(current_layout_id == mainL, "hover opens nothing");
    click();
    mouseFrame();
    CHECK(lt_fade_status() != 2, "the click starts the fade (%d)", lt_fade_status());
    /* nothing while the layout fades */
    pointAt(labels[iCtl]);
    mouseFrame();
    CHECK(lt_ext_Layout(mainL)->curItem == labels[iDisp], "fading: the cursor stays (%d)",
          lt_ext_Layout(mainL)->curItem);
    const int dispL = ui_SettingsPageLayout(UI_PAGE_DISPLAY);
    CHECK(settle(dispL, 60), "click: Display opens (%d)", current_layout_id);

    /* Aspect's arrows: Right as the pad's Right, Left back; the value
       itself steps as Right */
    const int aspect = ui_SettingsRowOf(UI_PAGE_DISPLAY, UI_OPT_ASPECT);
    const int left = i17bArrow(dispL, aspect, LT_POINTER_LEFT);
    const int right = i17bArrow(dispL, aspect, LT_POINTER_RIGHT);
    CHECK(aspect >= 0 && left >= 0 && right >= 0, "Aspect and its arrows (%d %d %d)", aspect, left,
          right);
    mouseFrame();
    mouseFrame();
    snprintf(before, sizeof(before), "%s", ui_SettingsValueText(UI_OPT_ASPECT));
    pointAt(right);
    click();
    mouseFrame();
    frame(0);
    frame(0);
    snprintf(after, sizeof(after), "%s", ui_SettingsValueText(UI_OPT_ASPECT));
    CHECK(lt_ext_Layout(dispL)->curItem == aspect, "the arrow's click: the cursor on Aspect (%d)",
          lt_ext_Layout(dispL)->curItem);
    CHECK(strcmp(before, after) != 0, "the right arrow steps Aspect (%s)", after);
    pointAt(left);
    click();
    mouseFrame();
    frame(0);
    frame(0);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_ASPECT), before) == 0,
          "the left arrow steps it back (%s, want %s)", ui_SettingsValueText(UI_OPT_ASPECT),
          before);
    press(0x2000);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_ASPECT), after) == 0,
          "the pad's Right gives what the right arrow gave (%s, want %s)",
          ui_SettingsValueText(UI_OPT_ASPECT), after);
    press(0x8000);
    int opts[16], values[16];
    const int nd = ui_SettingsPageRows(UI_PAGE_DISPLAY, labels, opts, values, 16);
    int value = -1;
    for (int i = 0; i < nd; i++) {
        if (labels[i] == aspect) {
            value = values[i];
        }
    }
    CHECK(value >= 0 && lt_ext_PointerRole(value) == LT_POINTER_STEP, "Aspect's value (%d)", value);
    pointAt(value);
    click();
    mouseFrame();
    frame(0);
    frame(0);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_ASPECT), after) == 0,
          "a click on the value steps as Right (%s, want %s)", ui_SettingsValueText(UI_OPT_ASPECT),
          after);
    pointAt(left);
    click();
    mouseFrame();
    frame(0);
    frame(0);

    /* a click on empty space: nothing */
    snprintf(before, sizeof(before), "%s", ui_SettingsValueText(UI_OPT_ASPECT));
    const int cur = lt_ext_Layout(dispL)->curItem;
    ico_pointer_move(4.0f / 640.0f, 0.5f);
    click();
    pad[0].flags = 0;
    ui_MouseTick();
    CHECK(pad[0].flags == 0, "empty space: no button (0x%x)", (unsigned)pad[0].flags);
    frame(pad[0].flags);
    frame(0);
    CHECK(current_layout_id == dispL && lt_ext_Layout(dispL)->curItem == cur &&
              strcmp(ui_SettingsValueText(UI_OPT_ASPECT), before) == 0,
          "empty space: nothing changed (%d, %d)", current_layout_id,
          lt_ext_Layout(dispL)->curItem);

    /* a key or pad press hides the pointer, a move shows it again */
    mouseFrame();
    mouseFrame();
    CHECK(ui_MouseMenuActive(), "shown before the press");
    mouseFrameWith(0x4000);
    CHECK(!ui_MouseMenuActive() && ico_pointer_menu(), "a pad press hides the pointer");
    mouseFrame();
    CHECK(!ui_MouseMenuActive(), "still hidden");
    ico_pointer_move(0.5f, 0.5f);
    mouseFrame();
    CHECK(ui_MouseMenuActive(), "a move shows it");

    /* the wheel on Achievements: Down, a notch a move */
    press(0x10);
    CHECK(settle(mainL, 60), "Display: back");
    openPage(mainL, iAch, UI_PAGE_ACHIEVEMENTS);
    mouseFrame();
    mouseFrame();
    ico_pointer_wheel(-1.0f);
    pad[0].flags = 0;
    ui_MouseTick();
    CHECK(pad[0].flags == 0x4000, "the wheel down: Down (0x%x)", (unsigned)pad[0].flags);
    frame(pad[0].flags);
    ico_pointer_wheel(-1.0f);
    pad[0].flags = 0;
    ui_MouseTick();
    CHECK(pad[0].flags == 0, "the next notch waits a tick (0x%x)", (unsigned)pad[0].flags);
    frame(pad[0].flags);
    pad[0].flags = 0;
    ui_MouseTick();
    CHECK(pad[0].flags == 0x4000, "then Down (0x%x)", (unsigned)pad[0].flags);
    frame(pad[0].flags);
    frame(0); /* the two frames after a move clear the pad */
    frame(0);
    press(0x10);
    CHECK(settle(mainL, 60), "Achievements: back");

    /* the remap screen waiting for a press: a click is bound, not chosen */
    openPage(mainL, iCtl, UI_PAGE_CONTROLS);
    press(0x40); /* Remap controls */
    const int remapL = ui_SettingsPageLayout(UI_PAGE_REMAP);
    CHECK(settle(remapL, 60), "pointer: the remap screen");
    press(0x40); /* the capture */
    frame(0);
    CHECK(ui_SettingsCapturing(), "the capture waits");
    ui_SettingsPageRows(UI_PAGE_REMAP, labels, NULL, NULL, 16);
    const int capCur = lt_ext_Layout(remapL)->curItem;
    pointAt(labels[2]);
    click();
    pad[0].flags = 0;
    ui_MouseTick();
    CHECK(pad[0].flags == 0 && lt_ext_Layout(remapL)->curItem == capCur,
          "capturing: no Cross, no move (0x%x, %d)", (unsigned)pad[0].flags,
          lt_ext_Layout(remapL)->curItem);
    frame(0);

    ui_SettingsReset();
    ui_MouseReset();
}

/* Gameplay > Achievement pop-ups: the last switch before Back, On by
   default, a step flips the pop-ups live, ui_SettingsSave writes [game]
   achievements only after a change, and a step back reads On again */
static void testAchievementPopups(void)
{
    int rows[16], opts[16];
    char p[1100];
    IcoToml *t;

    useConfig("version = 1\n");
    ico_ach_set_popups(1);
    enterMain(0);
    const int n = ui_SettingsPageRows(UI_PAGE_GAMEPLAY, rows, opts, NULL, 16);
    CHECK(n >= 3 && opts[n - 1] == UI_OPT_BACK && opts[n - 2] == UI_OPT_ACH_POPUPS &&
              opts[n - 3] == UI_OPT_PLAYERS,
          "pop-ups: the row sits after Players, before Back (%d rows)", n);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_ACH_POPUPS), "On") == 0, "pop-ups: On by default");
    CHECK(ui_SettingsSave() == 0, "pop-ups: save without a change");
    path(p, sizeof(p), "settings_test.toml");
    t = ico_toml_load(p);
    if (t) {
        CHECK(!ico_toml_has(t, "game.achievements"), "pop-ups: no key written unchanged");
        ico_toml_free(t);
    }
    ui_SettingsStep(UI_OPT_ACH_POPUPS, 1);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_ACH_POPUPS), "Off") == 0, "pop-ups: Off");
    CHECK(ico_ach_popups_enabled() == 0, "pop-ups: the getter reads Off at once");
    CHECK(ui_SettingsSave() == 0, "pop-ups: save");
    t = ico_toml_load(p);
    CHECK(t != NULL, "pop-ups: config");
    if (t) {
        CHECK(ico_toml_has(t, "game.achievements") &&
                  ico_toml_get_bool(t, "game.achievements", 1) == 0,
              "pop-ups: [game] achievements false");
        ico_toml_free(t);
    }
    ui_SettingsStep(UI_OPT_ACH_POPUPS, -1);
    CHECK(strcmp(ui_SettingsValueText(UI_OPT_ACH_POPUPS), "On") == 0, "pop-ups: On again");
    CHECK(ico_ach_popups_enabled() == 1, "pop-ups: the getter reads On again");
    useConfig("version = 1\n");
    ico_ach_set_popups(1);
}

int main(int argc, char **argv)
{
    snprintf(s_dir, sizeof(s_dir), "%s", argc > 1 ? argv[1] : ".");
    setEnv("LC_ALL", NULL);
    setEnv("LANG", "en_GB.UTF-8");
    testFixture();
    /* Display > Window mode */
    testWindowMode();
    /* Effects > Cinematic bars */
    testCinematicBars();
    /* the select callback's argument */
    testItemSelectArg();
    /* Settings > Graphics driver, the Quit game label */
    testGpuDriver();
    testQuitGame();
    /* Controls > the mouse camera rows */
    testMouseCamera();
    /* the title kept unseen while Options reopens */
    testTitleReturn();
    /* the mouse pointer in the menus */
    testPointer();
    /* Gameplay > Achievement pop-ups */
    testAchievementPopups();
    if (failures) {
        printf("settings_extra_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("settings_extra_test: ok\n");
    return 0;
}

/* settings_extra_test.c: more Settings menu tests, over the shared
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
#include "ui_internal.h" /* ui__font_chinese_first */
#include "ui_lang.h"
#include "../../core/region.h"
#include "photo_mode.h"
#include "touch_edit.h"

/* the fixture builds a layout and the menu opens */
static void testFixture(void)
{
    useConfig("version = 1\n");
    const int mainL = enterMain(1);
    CHECK(mainL == ui_settings_page_layout(UI_PAGE_MAIN), "title: the main page (%d)", mainL);
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
    const int row = ui_settings_row_of(UI_PAGE_DISPLAY, UI_OPT_WINDOW_MODE);
    return row >= 0 && !lt_ext_prop(row)->defaultMask && !lt_ext_prop(row)->masked;
}

static void testWindowMode(void)
{
    IcoVideoOptions o;

    /* the step wraps through Windowed, Borderless, Fullscreen */
    enterMain(0);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_WINDOW_MODE), "Windowed") == 0, "default: Windowed");
    ui_settings_step(UI_OPT_WINDOW_MODE, 1);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_WINDOW_MODE), "Borderless") == 0,
          "step: Borderless");
    ui_settings_step(UI_OPT_WINDOW_MODE, 1);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_WINDOW_MODE), "Fullscreen") == 0,
          "step: Fullscreen");
    ui_settings_step(UI_OPT_WINDOW_MODE, 1);
    ico_video_get(&o);
    CHECK(o.windowMode == ICO_WINDOW_WINDOWED, "step wraps to Windowed (%d)", o.windowMode);
    ui_settings_step(UI_OPT_WINDOW_MODE, -1);
    ico_video_get(&o);
    CHECK(o.windowMode == ICO_WINDOW_FULLSCREEN, "step back wraps to Fullscreen (%d)",
          o.windowMode);

    /* the window's answer is shown and stepped from */
    s_i17cAnswer = ICO_WINDOW_BORDERLESS;
    ui_settings_set_window_mode_query(fakeI17cMode);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_WINDOW_MODE), "Borderless") == 0,
          "query: Borderless");
    ui_settings_step(UI_OPT_WINDOW_MODE, 1);
    ico_video_get(&o);
    CHECK(o.windowMode == ICO_WINDOW_FULLSCREEN, "step from the query's Borderless (%d)",
          o.windowMode);
    s_i17cAnswer = ICO_WINDOW_WINDOWED;
    ui_settings_step(UI_OPT_WINDOW_MODE, -1);
    ico_video_get(&o);
    CHECK(o.windowMode == ICO_WINDOW_FULLSCREEN, "step back from the query's Windowed (%d)",
          o.windowMode);
    ui_settings_set_window_mode_query(NULL);

    /* the save writes window_mode, and fullscreen for older builds */
    enterMain(0);
    ui_settings_step(UI_OPT_WINDOW_MODE, 1);
    CHECK(ui_settings_save() == 0, "save");
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
   default, a step flips it live and ui_settings_save writes the key; the other
   effects keep their value */
static void testCinematicBars(void)
{
    int rows[16], opts[16];
    char p[1100];

    useConfig("version = 1\n");
    enterMain(0);
    const int n = ui_settings_page_rows(UI_PAGE_EFFECTS, rows, opts, NULL, 16);
    CHECK(n >= 5 && opts[n - 1] == UI_OPT_BACK && opts[n - 2] == UI_OPT_FULL_PIXEL &&
              opts[n - 3] == UI_OPT_EFFECT_CINEMATIC_BARS && opts[n - 4] == UI_OPT_EFFECT_WATER &&
              opts[n - 5] == UI_OPT_EFFECT_FOG,
          "bars: the row sits after Fog and Water reflections, before Full pixel and Back (%d "
          "rows)",
          n);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_FULL_PIXEL), "Off") == 0,
          "full pixel: Off by default");
    ui_settings_step(UI_OPT_FULL_PIXEL, 1);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_FULL_PIXEL), "On") == 0, "full pixel: On");
    CHECK(ui_settings_save() == 0, "full pixel: save");
    path(p, sizeof(p), "settings_test.toml");
    IcoToml *fp = ico_toml_load(p);
    CHECK(fp != NULL && ico_toml_get_bool(fp, "video.full_pixel", 0) == 1,
          "full pixel: [video] full_pixel true");
    ico_toml_free(fp);
    ui_settings_step(UI_OPT_FULL_PIXEL, -1);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_FULL_PIXEL), "Off") == 0, "full pixel: Off again");
    CHECK(strcmp(ui_settings_value_text(UI_OPT_EFFECT_CINEMATIC_BARS), "On") == 0,
          "bars: On by default");
    CHECK(ico_video_effect_cinematic_bars() == 1, "bars: the getter reads On");
    ui_settings_step(UI_OPT_EFFECT_CINEMATIC_BARS, 1);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_EFFECT_CINEMATIC_BARS), "Off") == 0, "bars: Off");
    CHECK(ico_video_effect_cinematic_bars() == 0, "bars: the getter reads Off at once");
    CHECK(strcmp(ui_settings_value_text(UI_OPT_EFFECT_FOG), "On") == 0, "bars: Fog untouched");
    CHECK(ui_settings_save() == 0, "bars: save");
    path(p, sizeof(p), "settings_test.toml");
    IcoToml *t = ico_toml_load(p);
    CHECK(t != NULL, "bars: config");
    if (t) {
        CHECK(ico_toml_get_bool(t, "video.effect_cinematic_bars", 1) == 0,
              "bars: [video] effect_cinematic_bars false");
        ico_toml_free(t);
    }
    ui_settings_step(UI_OPT_EFFECT_CINEMATIC_BARS, -1);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_EFFECT_CINEMATIC_BARS), "On") == 0,
          "bars: On again");
    useConfig("version = 1\n");
}

/* Effects > Water reflections: Original by default, a step makes it Sharp
   (the getter at once, [video] effect_water_sharp true when saved), its note
   on the page, and the page's fourteen rows fit from the pause menu */
static void testWaterReflections(void)
{
    char p[1100];

    useConfig("version = 1\n");
    const int mainL = enterMain(0);
    openPage(mainL, 1, UI_PAGE_EFFECTS);
    CHECK(ui_settings_row_of(UI_PAGE_EFFECTS, UI_OPT_EFFECT_WATER) >= 0, "water: the row");
    CHECK(noteStarting(UI_PAGE_EFFECTS, "Sharp draws") >= 0, "water: the note");
    checkPageFits(UI_PAGE_EFFECTS, "Effects with Water reflections (pause)");
    press(0x10);
    CHECK(settle(mainL, 60), "water: back from Effects");
    CHECK(strcmp(ui_settings_value_text(UI_OPT_EFFECT_WATER), "Original") == 0,
          "water: Original by default");
    CHECK(ico_video_effect_water_sharp() == 0, "water: the getter reads Original");
    ui_settings_step(UI_OPT_EFFECT_WATER, 1);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_EFFECT_WATER), "Sharp") == 0, "water: Sharp");
    CHECK(ico_video_effect_water_sharp() == 1, "water: the getter reads Sharp at once");
    CHECK(strcmp(ui_settings_value_text(UI_OPT_EFFECT_FOG), "On") == 0, "water: Fog untouched");
    CHECK(ui_settings_save() == 0, "water: save");
    path(p, sizeof(p), "settings_test.toml");
    IcoToml *t = ico_toml_load(p);
    CHECK(t != NULL && ico_toml_get_bool(t, "video.effect_water_sharp", 0) == 1,
          "water: [video] effect_water_sharp true");
    ico_toml_free(t);
    ui_settings_step(UI_OPT_EFFECT_WATER, -1);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_EFFECT_WATER), "Original") == 0,
          "water: Original again");
    useConfig("version = 1\n");
}

/* default_item_select hands the select callback the current
   item (la_mc_saved_file_select indexes the card's file table with it; with
   nothing passed it would read whatever the argument register held) */
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
    lt_ext_reset();
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
static int s_gpuFiles, s_gpuFileBegins, s_gpuFileBegun = -99;
static const char *const kGpuFileNames[2] = {"turnip-a.zip", "B.ZIP"};
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

static int gpuFileCount(void)
{
    return s_gpuFiles;
}

static const char *gpuFileName(int i)
{
    return kGpuFileNames[i];
}

static int gpuFileBegin(int i)
{
    s_gpuFileBegins++;
    s_gpuFileBegun = i;
    return 0;
}

static const UiGpuDriverHost kGpuHost = {gpuCount,      gpuName,      gpuSelected, gpuSelect,
                                         gpuLastFailed, gpuAdreno,    gpuBegin,    gpuPoll,
                                         gpuRemove,     gpuFileCount, gpuFileName, gpuFileBegin};

static int an22bShown(UiSettingsPage page, UiSettingsOpt opt)
{
    const int row = ui_settings_row_of(page, opt);
    return row >= 0 && !lt_ext_prop(row)->defaultMask && !lt_ext_prop(row)->masked;
}

/* the main page's Graphics driver link: its row, -1 */
static int an22bLink(void)
{
    int labels[20];
    const int n = ui_settings_page_rows(UI_PAGE_MAIN, labels, NULL, NULL, 20);
    for (int i = 0; i < n; i++) {
        if (strcmp(lt_ext_row_text(labels[i]), "Graphics driver") == 0) {
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
    const LtProp *lay = lt_ext_layout(l);
    for (int j = lay->first; j < lay->last; j++) {
        if (strcmp(lt_ext_row_text(j), text) == 0) {
            return j;
        }
    }
    return -1;
}

static void testGpuDriver(void)
{
    ui_settings_set_gpu_driver_host(NULL);
    ui_popup_reset();

    /* no host: no link; the Main page is the twelve it was */
    enterMain(1);
    frame(0);
    CHECK(an22bLink() >= 0 && lt_ext_prop(an22bLink())->defaultMask,
          "no host: the Graphics driver link is hidden");

    /* a host: the link shows after Effects and opens the page */
    s_gpuN = 2;
    s_gpuSel = -1;
    s_gpuFailed = -1;
    s_gpuAdreno = 1;
    ui_settings_set_gpu_driver_host(&kGpuHost);
    int mainL = enterMain(1);
    frame(0);
    {
        int labels[20];
        ui_settings_page_rows(UI_PAGE_MAIN, labels, NULL, NULL, 20);
        const int link = an22bLink();
        CHECK(link == labels[3] && !lt_ext_prop(link)->defaultMask,
              "host: the link shows after Effects and Graphics (%d)", link);
        CHECK(lt_ext_prop(link)->right == ui_settings_page_layout(UI_PAGE_GPU_DRIVER),
              "the link opens the page");
    }
    const int gpuL = openPage(mainL, 3, UI_PAGE_GPU_DRIVER);
    const int driver = ui_settings_row_of(UI_PAGE_GPU_DRIVER, UI_OPT_GPU_DRIVER);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_GPU_DRIVER), "Built-in") == 0, "Built-in at first");
    CHECK(an22bShown(UI_PAGE_GPU_DRIVER, UI_OPT_GPU_ADD), "Add shows on an Adreno");
    CHECK(!an22bShown(UI_PAGE_GPU_DRIVER, UI_OPT_GPU_REMOVE), "Remove is hidden on Built-in");

    /* Right steps Built-in, the first, the second, around; each choice goes to the host */
    lt_ext_layout(gpuL)->curItem = driver;
    press(0x2000);
    CHECK(s_gpuSel == 0 && strcmp(ui_settings_value_text(UI_OPT_GPU_DRIVER), "Turnip 24.1") == 0,
          "Right: the first driver (%d)", s_gpuSel);
    CHECK(an22bShown(UI_PAGE_GPU_DRIVER, UI_OPT_GPU_REMOVE), "Remove shows on a driver");
    ui_settings_step(UI_OPT_GPU_DRIVER, 1);
    CHECK(s_gpuSel == 1, "Right: the second driver (%d)", s_gpuSel);
    ui_settings_step(UI_OPT_GPU_DRIVER, 1);
    CHECK(s_gpuSel == -1, "Right wraps to Built-in (%d)", s_gpuSel);
    ui_settings_step(UI_OPT_GPU_DRIVER, -1);
    CHECK(s_gpuSel == 1, "Left wraps to the last driver (%d)", s_gpuSel);

    /* the one that did not start keeps its name on the row; the page's note
       says so (a sentence fits there, not in the value box) */
    s_gpuFailed = 1;
    frame(0);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_GPU_DRIVER), "Turnip 25.0") == 0, "failed: %s",
          ui_settings_value_text(UI_OPT_GPU_DRIVER));
    {
        const int note = ui_settings_note_row_of(UI_PAGE_GPU_DRIVER, UI_OPT_GPU_DRIVER);
        CHECK(note >= 0 && strstr(lt_ext_row_text(note), "did not start") != NULL,
              "failed: the note says so (%s)", note >= 0 ? lt_ext_row_text(note) : "-");
    }
    s_gpuFailed = -1;
    frame(0);
    {
        const int note = ui_settings_note_row_of(UI_PAGE_GPU_DRIVER, UI_OPT_GPU_DRIVER);
        CHECK(note >= 0 && strstr(lt_ext_row_text(note), "Adreno") != NULL,
              "the note is back to the Adreno line");
    }
    /* as the real host leaves it after a failure (gpu_driver_android.c
       driver_failed): the choice back on Built-in, the driver remembered as
       failed; the note says so until a driver is chosen again */
    s_gpuSel = -1;
    s_gpuFailed = 1;
    frame(0);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_GPU_DRIVER), "Built-in") == 0,
          "after a failure: Built-in (%s)", ui_settings_value_text(UI_OPT_GPU_DRIVER));
    {
        const int note = ui_settings_note_row_of(UI_PAGE_GPU_DRIVER, UI_OPT_GPU_DRIVER);
        CHECK(note >= 0 && strstr(lt_ext_row_text(note), "did not start") != NULL,
              "after a failure: the note says so on Built-in (%s)",
              note >= 0 ? lt_ext_row_text(note) : "-");
    }
    ui_settings_step(UI_OPT_GPU_DRIVER, 1);
    frame(0);
    CHECK(s_gpuSel == 0 && s_gpuFailed == -1, "a new choice forgets the failure (%d %d)", s_gpuSel,
          s_gpuFailed);
    {
        const int note = ui_settings_note_row_of(UI_PAGE_GPU_DRIVER, UI_OPT_GPU_DRIVER);
        CHECK(note >= 0 && strstr(lt_ext_row_text(note), "Adreno") != NULL,
              "a new choice: the note is the Adreno line again");
    }

    /* Add: the picker opens, the page polls every frame until it answers */
    frame(0);
    const int add = ui_settings_row_of(UI_PAGE_GPU_DRIVER, UI_OPT_GPU_ADD);
    lt_ext_layout(gpuL)->curItem = add;
    s_gpuBegins = s_gpuPolls = 0;
    an22bResult(UI_GPU_INSTALL_PENDING, UI_GPU_INSTALL_ADDED);
    press(0x40);
    CHECK(s_gpuBegins == 1, "Cross on Add opens the picker (%d)", s_gpuBegins);
    for (int k = 0; k < 4; k++) {
        frame(0);
    }
    CHECK(s_gpuPolls == 2, "polled until the answer, then stopped (%d)", s_gpuPolls);
    CHECK(strcmp(ui_popup_title(), "Driver added") == 0, "added: \"%s\"", ui_popup_title());

    static const struct {
        int result;
        const char *title;
    } kAnswers[] = {
        {UI_GPU_INSTALL_BAD, "This file is not a driver package."},
        {UI_GPU_INSTALL_NOSPACE, "Not enough space to add this driver."},
        {UI_GPU_INSTALL_NO_PICKER,
         "The file picker could not be opened. You can copy the driver\xE2\x80\x99s zip "
         "file into the game\xE2\x80\x99s drivers folder instead."},
        {UI_GPU_INSTALL_NO_PICKER_APP,
         "No app on this device can choose a file. Copy the driver\xE2\x80\x99s zip file "
         "into the game\xE2\x80\x99s drivers folder instead."},
        {UI_GPU_INSTALL_UNREADABLE,
         "The chosen file could not be read. Copy it onto this device first, then "
         "choose it again."},
        {UI_GPU_INSTALL_CANCELLED, ""}};

    for (unsigned i = 0; i < sizeof(kAnswers) / sizeof(kAnswers[0]); i++) {
        ui_popup_reset();
        an22bResult(kAnswers[i].result, kAnswers[i].result);
        press(0x40);
        frame(0);
        frame(0);
        CHECK(strcmp(ui_popup_title(), kAnswers[i].title) == 0, "answer %d: \"%s\"",
              kAnswers[i].result, ui_popup_title());
        CHECK((kAnswers[i].title[0] != '\0') == ui_popup_active(), "answer %d: popup or silence",
              kAnswers[i].result);
    }

    /* Add from the drivers folder: hidden with no zip, shown with two, Left
       and Right go round them, Cross adds the shown one and the same poll
       answers; a second Cross while the copy runs is ignored */
    ui_popup_reset();
    s_gpuFiles = 0;
    frame(0);
    CHECK(!an22bShown(UI_PAGE_GPU_DRIVER, UI_OPT_GPU_ADD_FILE), "no zip: the folder row is hidden");
    s_gpuFiles = 2;
    frame(0);
    CHECK(an22bShown(UI_PAGE_GPU_DRIVER, UI_OPT_GPU_ADD_FILE), "two zips: the folder row shows");
    CHECK(strcmp(ui_settings_value_text(UI_OPT_GPU_ADD_FILE), "turnip-a.zip") == 0, "first zip: %s",
          ui_settings_value_text(UI_OPT_GPU_ADD_FILE));
    ui_settings_step(UI_OPT_GPU_ADD_FILE, 1);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_GPU_ADD_FILE), "B.ZIP") == 0, "Right: %s",
          ui_settings_value_text(UI_OPT_GPU_ADD_FILE));
    ui_settings_step(UI_OPT_GPU_ADD_FILE, 1);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_GPU_ADD_FILE), "turnip-a.zip") == 0,
          "Right wraps: %s", ui_settings_value_text(UI_OPT_GPU_ADD_FILE));
    ui_settings_step(UI_OPT_GPU_ADD_FILE, -1);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_GPU_ADD_FILE), "B.ZIP") == 0, "Left wraps: %s",
          ui_settings_value_text(UI_OPT_GPU_ADD_FILE));
    lt_ext_layout(gpuL)->curItem = ui_settings_row_of(UI_PAGE_GPU_DRIVER, UI_OPT_GPU_ADD_FILE);
    s_gpuFileBegins = 0;
    s_gpuFileBegun = -99;
    s_gpuBegins = 0;
    for (int k = 0; k < 4; k++) {
        s_gpuResult[k] = UI_GPU_INSTALL_PENDING; /* the polls of the two presses' frames */
    }
    s_gpuResult[4] = UI_GPU_INSTALL_UNREADABLE;
    s_gpuResultN = 5;
    s_gpuResultAt = 0;
    press(0x40);
    press(0x40); /* still copying: ignored */
    CHECK(s_gpuFileBegins == 1 && s_gpuFileBegun == 1 && s_gpuBegins == 0,
          "Cross adds the shown zip once (%d, zip %d, picker %d)", s_gpuFileBegins, s_gpuFileBegun,
          s_gpuBegins);
    for (int k = 0; k < 4; k++) {
        frame(0);
    }
    CHECK(strcmp(ui_popup_title(), "The chosen file could not be read. Copy it onto this device "
                                   "first, then choose it again.") == 0,
          "unreadable: \"%s\"", ui_popup_title());
    s_gpuFiles = 0;

    /* Remove: Built-in is chosen, the host deletes the chosen one, a box says so */
    ui_popup_reset();
    s_gpuSel = 1;
    frame(0);
    lt_ext_layout(gpuL)->curItem = ui_settings_row_of(UI_PAGE_GPU_DRIVER, UI_OPT_GPU_REMOVE);
    press(0x40);
    CHECK(s_gpuRemoved == 1 && s_gpuSel == -1, "Remove: driver 1 deleted, Built-in chosen (%d %d)",
          s_gpuRemoved, s_gpuSel);
    CHECK(strcmp(ui_popup_title(), "Driver removed") == 0, "removed: \"%s\"", ui_popup_title());
    frame(0);
    CHECK(!an22bShown(UI_PAGE_GPU_DRIVER, UI_OPT_GPU_REMOVE), "Remove hides again");

    /* a phone without Adreno graphics: no Add on the page, and the page's
       link on the main menu is hidden */
    s_gpuAdreno = 0;
    frame(0);
    CHECK(!an22bShown(UI_PAGE_GPU_DRIVER, UI_OPT_GPU_ADD), "non-Adreno: Add is hidden");
    s_gpuBegins = 0;
    lt_ext_layout(gpuL)->curItem = add;
    press(0x40);
    CHECK(s_gpuBegins == 0, "a hidden Add does nothing");
    press(0x10);
    CHECK(settle(mainL, 60), "Triangle: back to the menu");
    for (int k = 0; k < 4; k++) {
        frame(0); /* the main page refreshes */
    }
    CHECK(an22bLink() >= 0 && lt_ext_prop(an22bLink())->defaultMask,
          "non-Adreno: the Graphics driver link is hidden");
    s_gpuAdreno = 1;
    for (int k = 0; k < 4; k++) {
        frame(0);
    }
    CHECK(an22bLink() >= 0 && !lt_ext_prop(an22bLink())->defaultMask,
          "Adreno again: the link shows");

    /* the Main page with the link and Developer mode: fourteen rows fit */
    ico_opt_set_developer_mode(1);
    for (int k = 0; k < 4; k++) {
        frame(0);
    }
    {
        int labels[20], prev = -1, shown = 0, last = -1;
        const int n = ui_settings_page_rows(UI_PAGE_MAIN, labels, NULL, NULL, 20);
        for (int i = 0; i < n; i++) {
            const LtProperty *r = lt_ext_prop(labels[i]);
            if (r->defaultMask) {
                continue;
            }
            CHECK(prev < 0 ? r->dispY >= 34 : r->dispY >= prev + 11, "row %d at y %d after %d", i,
                  r->dispY, prev);
            prev = r->dispY;
            last = labels[i];
            shown++;
        }
        CHECK(shown == 14, "fourteen rows shown (%d)", shown);
        CHECK(last >= 0 && lt_ext_prop(last)->dispY == 40 + 11 * 13 &&
                  lt_ext_prop(last)->dispY + lt_ext_prop(last)->dispH <= 226,
              "Back at %d ends at %d", last >= 0 ? lt_ext_prop(last)->dispY : -1,
              last >= 0 ? lt_ext_prop(last)->dispY + lt_ext_prop(last)->dispH : -1);
    }
    ico_opt_set_developer_mode(0);
    ui_settings_set_gpu_driver_host(NULL);
    frame(0);
    CHECK(lt_ext_prop(an22bLink())->defaultMask, "the host removed: the link hides again");

    /* the strings, in all five languages */
    static const UiStrId kIds[] = {UI_STR_SECTION_GPU_DRIVER,       UI_STR_OPT_GPU_DRIVER,
                                   UI_STR_VAL_GPU_BUILTIN,          UI_STR_GPU_DRIVER_ADD,
                                   UI_STR_GPU_DRIVER_REMOVE,        UI_STR_GPU_DRIVER_NOTE,
                                   UI_STR_GPU_DRIVER_ADDED,         UI_STR_GPU_DRIVER_BAD,
                                   UI_STR_GPU_DRIVER_REMOVED,       UI_STR_GPU_DRIVER_FAILED,
                                   UI_STR_GPU_DRIVER_NOSPACE,       UI_STR_QUIT_GAME,
                                   UI_STR_QUIT_GAME_CONFIRM,        UI_STR_GPU_DRIVER_NO_PICKER,
                                   UI_STR_GPU_DRIVER_NO_PICKER_APP, UI_STR_GPU_DRIVER_UNREADABLE,
                                   UI_STR_GPU_DRIVER_ADD_FILE,      UI_STR_SECTION_GRAPHICS,
                                   UI_STR_OPT_GRAPHICS_API,         UI_STR_VAL_GRAPHICS_AUTOMATIC,
                                   UI_STR_GRAPHICS_API_NOTE};
    static const UiLang kLangs[5] = {UI_LANG_EN, UI_LANG_FR, UI_LANG_DE, UI_LANG_IT, UI_LANG_ES};
    for (int l = 0; l < 5; l++) {
        for (unsigned i = 0; i < sizeof(kIds) / sizeof(kIds[0]); i++) {
            CHECK(ui_str_in(kLangs[l], kIds[i])[0] != '\0', "language %d, string %d", l, kIds[i]);
        }
    }
    CHECK(strcmp(ui_str_in(UI_LANG_ES, UI_STR_QUIT_GAME_CONFIRM), "\xC2\xBFSalir del juego?") == 0,
          "Spanish question");
}

/* Options > Graphics, the Graphics API row: the link and the page show with
   two or more renderers named by the host; the row goes round Automatic and
   the names, shows the config's value and writes [video] backend on leaving */
static int graphicsLink(void)
{
    int labels[20];
    const int n = ui_settings_page_rows(UI_PAGE_MAIN, labels, NULL, NULL, 20);
    for (int i = 0; i < n; i++) {
        if (lt_ext_prop(labels[i])->right == ui_settings_page_layout(UI_PAGE_GRAPHICS)) {
            return labels[i];
        }
    }
    return -1;
}

static void testGraphicsApi(void)
{
    static const char *const kNames[] = {"vulkan", "opengl", "d3d12", "d3d11"};
    char p[1100];
    int labels[20];

    ui_settings_set_backend_names(NULL, 0);
    useConfig("version = 1\n");
    int mainL = enterMain(1);
    frame(0);
    int link = graphicsLink();
    CHECK(link >= 0 && lt_ext_prop(link)->defaultMask,
          "no renderer names: the Graphics link hides");
    ui_settings_set_backend_names(kNames, 1);
    mainL = enterMain(1);
    frame(0);
    link = graphicsLink();
    CHECK(link >= 0 && lt_ext_prop(link)->defaultMask, "one renderer: the Graphics link hides");

    /* two or more: the link shows right after Effects */
    ui_settings_set_backend_names(kNames, 4);
    mainL = enterMain(1);
    frame(0);
    link = graphicsLink();
    const int n = ui_settings_page_rows(UI_PAGE_MAIN, labels, NULL, NULL, 20);
    CHECK(n > 3 && labels[2] == link && !lt_ext_prop(link)->defaultMask &&
              strcmp(lt_ext_row_text(link), "Graphics") == 0,
          "four renderers: the Graphics link shows after Effects");
    const int pageL = openPage(mainL, 2, UI_PAGE_GRAPHICS);
    const int row = ui_settings_row_of(UI_PAGE_GRAPHICS, UI_OPT_GRAPHICS_API);
    CHECK(row >= 0 && strcmp(lt_ext_row_text(row), "Graphics API") == 0, "the Graphics API row");
    CHECK(noteStarting(UI_PAGE_GRAPHICS, "Apply switches now; without Apply") >= 0,
          "the note (two lines: its start)");
    CHECK(strcmp(ui_settings_value_text(UI_OPT_GRAPHICS_API), "Automatic") == 0,
          "no key: Automatic");

    /* Right goes round the names in the order given, back to Automatic */
    static const char *const kShown[] = {"Vulkan", "OpenGL", "Direct3D 12", "Direct3D 11"};
    static const char *const kKeys[] = {"vulkan", "opengl", "d3d12", "d3d11"};
    for (int i = 0; i < 4; i++) {
        ui_settings_step(UI_OPT_GRAPHICS_API, 1);
        CHECK(strcmp(ui_settings_value_text(UI_OPT_GRAPHICS_API), kShown[i]) == 0,
              "Right %d shows %s (%s)", i, kShown[i], ui_settings_value_text(UI_OPT_GRAPHICS_API));
        CHECK(strcmp(ico_config_get_string("video.backend", "?"), kKeys[i]) == 0,
              "Right %d writes %s", i, kKeys[i]);
    }
    ui_settings_step(UI_OPT_GRAPHICS_API, 1);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_GRAPHICS_API), "Automatic") == 0,
          "Right wraps to Automatic");
    CHECK(strcmp(ico_config_get_string("video.backend", "?"), "") == 0,
          "Automatic writes an empty backend");
    ui_settings_step(UI_OPT_GRAPHICS_API, -1);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_GRAPHICS_API), "Direct3D 11") == 0,
          "Left wraps to the last");

    /* leaving the page writes the file */
    ui_settings_step(UI_OPT_GRAPHICS_API, -1);
    ui_settings_step(UI_OPT_GRAPHICS_API, -1);
    press(LT_PAD_TRIANGLE);
    CHECK(settle(mainL, 60), "Back leaves the page (%d)", pageL);
    CHECK(ui_settings_save() == 0, "save");
    path(p, sizeof(p), "settings_test.toml");
    IcoToml *t = ico_toml_load(p);
    CHECK(t != NULL, "the file");
    if (t) {
        const char *b = ico_toml_get(t, "video.backend");
        CHECK(b != NULL && strcmp(b, "opengl") == 0, "[video] backend = opengl (%s)", b ? b : "-");
        ico_toml_free(t);
    }

    /* a value the build does not link shows as written, and steps from
       Automatic (enterMainKeep: enterMain would write a fresh config over
       the key) */
    useConfig("version = 1\n[video]\nbackend = \"d3d12\"\n");
    ui_settings_set_backend_names(kNames, 2);
    enterMainKeep(1);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_GRAPHICS_API), "Direct3D 12") == 0,
          "an unlinked value shows as written");
    ui_settings_step(UI_OPT_GRAPHICS_API, 1);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_GRAPHICS_API), "Vulkan") == 0,
          "an unlinked value steps from Automatic");

    /* the aliases rhi_create_backend takes ("gl", "vk", any case) show by
       their names and step from their place */
    useConfig("version = 1\n[video]\nbackend = \"gl\"\n");
    ui_settings_set_backend_names(kNames, 4);
    enterMainKeep(1);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_GRAPHICS_API), "OpenGL") == 0,
          "gl shows as OpenGL (%s)", ui_settings_value_text(UI_OPT_GRAPHICS_API));
    ui_settings_step(UI_OPT_GRAPHICS_API, 1);
    CHECK(strcmp(ico_config_get_string("video.backend", "?"), "d3d12") == 0,
          "Right from gl writes d3d12");
    useConfig("version = 1\n[video]\nbackend = \"VK\"\n");
    enterMainKeep(1);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_GRAPHICS_API), "Vulkan") == 0,
          "VK shows as Vulkan (%s)", ui_settings_value_text(UI_OPT_GRAPHICS_API));
    ui_settings_step(UI_OPT_GRAPHICS_API, 1);
    CHECK(strcmp(ico_config_get_string("video.backend", "?"), "opengl") == 0,
          "Right from VK writes opengl");
    ui_settings_set_backend_names(NULL, 0);
}

/* Graphics > Apply: a fake host running "vulkan".  Apply shows while the
   row's value is a renderer the build has and not the one running, never
   without a host or with one renderer; Cross asks the host once; the
   countdown's Keep and Revert replace the page, the cursor on Revert, the
   note counting down; 10 s of vsyncs at 60 Hz switch back with nothing
   written; Keep writes the choice; Revert and Circle switch back; a
   renderer that did not start is a popup and the value the page found
   back; a switch the host answers later is polled, the presses meanwhile
   ignored; a save during the countdown (the close button's) writes the
   value the page found; Automatic on a run whose Automatic started OpenGL
   offers no Apply on OpenGL */
static char s_gfxRunning[16] = "vulkan";
static char s_gfxAsked[16][16];
static int s_gfxWhy[16];
static int s_gfxBegins, s_gfxPolls, s_gfxKepts;
static int s_gfxAnswer = UI_GFX_SWITCH_DONE; /* switchBegin's answer */
static int s_gfxPollAnswers[4], s_gfxPollN, s_gfxPollAt;

static const char *fakeGfxRunning(void)
{
    return s_gfxRunning;
}

static int fakeGfxBegin(const char *name, int why)
{
    const int i = s_gfxBegins < 16 ? s_gfxBegins : 15;
    snprintf(s_gfxAsked[i], sizeof(s_gfxAsked[i]), "%s", name);
    s_gfxWhy[i] = why;
    s_gfxBegins++;
    if (s_gfxAnswer == UI_GFX_SWITCH_DONE) {
        snprintf(s_gfxRunning, sizeof(s_gfxRunning), "%s", name);
    }
    return s_gfxAnswer;
}

static int fakeGfxPoll(void)
{
    s_gfxPolls++;
    const int r = s_gfxPollAt < s_gfxPollN ? s_gfxPollAnswers[s_gfxPollAt++] : UI_GFX_SWITCH_DONE;
    if (r == UI_GFX_SWITCH_DONE && s_gfxBegins > 0) {
        snprintf(s_gfxRunning, sizeof(s_gfxRunning), "%s", s_gfxAsked[s_gfxBegins - 1]);
    }
    return r;
}

static void fakeGfxKept(void)
{
    s_gfxKepts++;
}

/* the renderer Automatic started this run, "" for none (Vulkan) */
static char s_gfxAuto[16];

static const char *fakeGfxAutomatic(void)
{
    return s_gfxAuto[0] != '\0' ? s_gfxAuto : NULL;
}

static const UiGraphicsHost kGfxHost = {fakeGfxRunning, fakeGfxBegin, fakeGfxPoll, fakeGfxKept,
                                        fakeGfxAutomatic};

/* the Graphics page's row for opt is on screen (its label not masked) */
static int gfxRowOn(UiSettingsOpt opt)
{
    const int row = ui_settings_row_of(UI_PAGE_GRAPHICS, opt);
    return row >= 0 && !lt_ext_prop(row)->defaultMask;
}

static void gfxVsyncs(int n)
{
    for (int i = 0; i < n; i++) {
        ui_settings_vsync();
    }
}

static void testGraphicsApply(void)
{
    static const char *const kNames[] = {"vulkan", "opengl"};
    char p[1100];

    snprintf(s_gfxRunning, sizeof(s_gfxRunning), "vulkan");
    s_gfxAnswer = UI_GFX_SWITCH_DONE;
    ui_popup_reset();
    ui_popup_set_vsync_hz(60);
    ui_settings_set_graphics_host(NULL);
    ui_settings_set_backend_names(kNames, 2);
    useConfig("version = 1\n");
    const int mainL = enterMain(1);
    frame(0);
    int pageL = openPage(mainL, 2, UI_PAGE_GRAPHICS);
    {
        int labels[8];
        const int n = ui_settings_page_rows(UI_PAGE_GRAPHICS, labels, NULL, NULL, 8);
        CHECK(n == 5, "the page holds Graphics API, Apply, Keep, Revert, Back (%d)", n);
    }

    /* no host: no Apply, whatever the value */
    ui_settings_step(UI_OPT_GRAPHICS_API, 1);
    ui_settings_step(UI_OPT_GRAPHICS_API, 1);
    frame(0);
    CHECK(strcmp(ico_config_get_string("video.backend", "?"), "opengl") == 0, "OpenGL chosen");
    CHECK(!ui_settings_opt_shown(UI_OPT_GRAPHICS_APPLY) && !gfxRowOn(UI_OPT_GRAPHICS_APPLY),
          "no host: Apply is hidden");
    ui_settings_step(UI_OPT_GRAPHICS_API, 1);
    CHECK(strcmp(ico_config_get_string("video.backend", "?"), "") == 0, "back to Automatic");

    /* a host: Automatic and Vulkan running, and Vulkan itself, show none */
    ui_settings_set_graphics_host(&kGfxHost);
    frame(0);
    CHECK(!gfxRowOn(UI_OPT_GRAPHICS_APPLY), "Automatic on Vulkan: no Apply");
    ui_settings_step(UI_OPT_GRAPHICS_API, 1);
    frame(0);
    CHECK(!gfxRowOn(UI_OPT_GRAPHICS_APPLY), "Vulkan on Vulkan: no Apply");
    CHECK(gfxRowOn(UI_OPT_GRAPHICS_API) && gfxRowOn(UI_OPT_BACK) &&
              !gfxRowOn(UI_OPT_GRAPHICS_KEEP) && !gfxRowOn(UI_OPT_GRAPHICS_REVERT),
          "the countdown's rows are hidden");
    /* one renderer: none either */
    ui_settings_step(UI_OPT_GRAPHICS_API, 1);
    ui_settings_set_backend_names(kNames, 1);
    CHECK(!ui_settings_opt_shown(UI_OPT_GRAPHICS_APPLY), "one renderer: no Apply");
    ui_settings_set_backend_names(kNames, 2);

    /* OpenGL chosen while Vulkan runs: Apply shows, with its note */
    frame(0);
    CHECK(gfxRowOn(UI_OPT_GRAPHICS_APPLY), "OpenGL on Vulkan: Apply shows");
    CHECK(noteStarting(UI_PAGE_GRAPHICS, "Switches now, without leaving the game.") >= 0,
          "Apply's note");
    const int apply = ui_settings_row_of(UI_PAGE_GRAPHICS, UI_OPT_GRAPHICS_APPLY);
    const int keep = ui_settings_row_of(UI_PAGE_GRAPHICS, UI_OPT_GRAPHICS_KEEP);
    const int revert = ui_settings_row_of(UI_PAGE_GRAPHICS, UI_OPT_GRAPHICS_REVERT);
    CHECK(strcmp(lt_ext_row_text(apply), "Apply") == 0 &&
              strcmp(lt_ext_row_text(keep), "Keep") == 0 &&
              strcmp(lt_ext_row_text(revert), "Revert") == 0,
          "the rows' words");

    /* Cross on Apply: the host asked once; the countdown, on Revert */
    s_gfxBegins = 0;
    lt_ext_layout(pageL)->curItem = apply;
    press(0x40);
    CHECK(s_gfxBegins == 1 && strcmp(s_gfxAsked[0], "opengl") == 0 &&
              s_gfxWhy[0] == UI_GFX_WHY_APPLY,
          "Cross on Apply asks for opengl once (%d, %s)", s_gfxBegins, s_gfxAsked[0]);
    CHECK(gfxRowOn(UI_OPT_GRAPHICS_KEEP) && gfxRowOn(UI_OPT_GRAPHICS_REVERT) &&
              !gfxRowOn(UI_OPT_GRAPHICS_API) && !gfxRowOn(UI_OPT_BACK) &&
              !gfxRowOn(UI_OPT_GRAPHICS_APPLY),
          "the countdown: Keep and Revert alone");
    CHECK(lt_ext_layout(pageL)->curItem == revert, "the cursor on Revert");
    {
        const int note = noteStarting(UI_PAGE_GRAPHICS, "Keep these settings?");
        CHECK(note >= 0 && strstr(lt_ext_row_text(note), "Reverting in 10 s") != NULL,
              "the countdown's note: %s", note >= 0 ? lt_ext_row_text(note) : "-");
        gfxVsyncs(60);
        frame(0);
        CHECK(note >= 0 && strstr(lt_ext_row_text(note), "Reverting in 9 s") != NULL,
              "a second later: %s", note >= 0 ? lt_ext_row_text(note) : "-");
    }
    /* the countdown runs out: Vulkan again, the value as it was, nothing kept */
    gfxVsyncs(600 - 60 - 1);
    CHECK(s_gfxBegins == 1, "599 vsyncs: still counting (%d)", s_gfxBegins);
    gfxVsyncs(1);
    CHECK(s_gfxBegins == 2 && strcmp(s_gfxAsked[1], "vulkan") == 0 &&
              s_gfxWhy[1] == UI_GFX_WHY_TIMEOUT,
          "600 vsyncs at 60 Hz: back to vulkan (%d, %s)", s_gfxBegins, s_gfxAsked[1]);
    CHECK(strcmp(ico_config_get_string("video.backend", "?"), "") == 0,
          "the value back to Automatic (%s)", ico_config_get_string("video.backend", "?"));
    CHECK(s_gfxKepts == 0, "nothing kept");
    frame(0);
    CHECK(gfxRowOn(UI_OPT_GRAPHICS_API) && gfxRowOn(UI_OPT_BACK) &&
              !gfxRowOn(UI_OPT_GRAPHICS_KEEP) && !gfxRowOn(UI_OPT_GRAPHICS_APPLY),
          "the page as before");

    /* Keep: the choice written */
    ui_settings_step(UI_OPT_GRAPHICS_API, 1);
    ui_settings_step(UI_OPT_GRAPHICS_API, 1);
    frame(0);
    lt_ext_layout(pageL)->curItem = apply;
    press(0x40);
    lt_ext_layout(pageL)->curItem = keep;
    press(0x40);
    CHECK(s_gfxKepts == 1 && s_gfxBegins == 3 && strcmp(s_gfxRunning, "opengl") == 0,
          "Keep: kept on opengl (%d, %d, %s)", s_gfxKepts, s_gfxBegins, s_gfxRunning);
    CHECK(!gfxRowOn(UI_OPT_GRAPHICS_KEEP) && !gfxRowOn(UI_OPT_GRAPHICS_APPLY),
          "kept: no countdown, no Apply");
    gfxVsyncs(700);
    CHECK(s_gfxBegins == 3, "kept: the time runs out on nothing");
    press(LT_PAD_TRIANGLE);
    CHECK(settle(mainL, 60), "Back leaves the page");
    CHECK(ui_settings_save() == 0, "save");
    path(p, sizeof(p), "settings_test.toml");
    IcoToml *t = ico_toml_load(p);
    CHECK(t != NULL, "the file");
    if (t) {
        const char *b = ico_toml_get(t, "video.backend");
        CHECK(b != NULL && strcmp(b, "opengl") == 0, "Keep: [video] backend = opengl (%s)",
              b ? b : "-");
        ico_toml_free(t);
    }

    /* Revert: OpenGL again, the value back to OpenGL */
    pageL = openPage(mainL, 2, UI_PAGE_GRAPHICS);
    ui_settings_step(UI_OPT_GRAPHICS_API, -1);
    frame(0);
    CHECK(gfxRowOn(UI_OPT_GRAPHICS_APPLY), "Vulkan on OpenGL: Apply shows");
    lt_ext_layout(pageL)->curItem = apply;
    press(0x40);
    CHECK(s_gfxBegins == 4 && strcmp(s_gfxAsked[3], "vulkan") == 0, "Apply asks for vulkan");
    press(0x40); /* the cursor is on Revert */
    CHECK(s_gfxBegins == 5 && strcmp(s_gfxAsked[4], "opengl") == 0 &&
              s_gfxWhy[4] == UI_GFX_WHY_REVERT,
          "Revert asks for opengl (%d, %s)", s_gfxBegins, s_gfxAsked[4]);
    CHECK(strcmp(ico_config_get_string("video.backend", "?"), "opengl") == 0,
          "Revert: the value back to OpenGL (%s)", ico_config_get_string("video.backend", "?"));

    /* Circle in the countdown is Revert, and stays on the page */
    ui_settings_step(UI_OPT_GRAPHICS_API, -1);
    frame(0);
    lt_ext_layout(pageL)->curItem = apply;
    press(0x40);
    press(LT_PAD_TRIANGLE);
    CHECK(s_gfxBegins == 7 && strcmp(s_gfxAsked[6], "opengl") == 0 &&
              s_gfxWhy[6] == UI_GFX_WHY_REVERT && current_layout_id == pageL,
          "Circle reverts on the page (%d, %s)", s_gfxBegins, s_gfxAsked[6]);

    /* a renderer that did not start: the popup, the value as chosen */
    ui_popup_reset();
    ui_settings_step(UI_OPT_GRAPHICS_API, -1);
    frame(0);
    s_gfxAnswer = UI_GFX_SWITCH_FAILED;
    lt_ext_layout(pageL)->curItem = apply;
    press(0x40);
    CHECK(strcmp(ui_popup_title(), "The Vulkan renderer could not start; OpenGL is kept") == 0,
          "failed: \"%s\"", ui_popup_title());
    frame(0);
    CHECK(strcmp(ico_config_get_string("video.backend", "?"), "opengl") == 0 &&
              !gfxRowOn(UI_OPT_GRAPHICS_APPLY) && !gfxRowOn(UI_OPT_GRAPHICS_KEEP),
          "failed: the value back to OpenGL, as the page found it (%s), no Apply",
          ico_config_get_string("video.backend", "?"));
    ui_settings_step(UI_OPT_GRAPHICS_API, -1);
    frame(0);
    lt_ext_layout(pageL)->curItem = apply;

    /* refused: nothing, the value as chosen */
    ui_popup_reset();
    s_gfxAnswer = UI_GFX_SWITCH_REFUSED;
    press(0x40);
    CHECK(!ui_popup_active() && !gfxRowOn(UI_OPT_GRAPHICS_KEEP), "refused: no popup, no countdown");
    CHECK(strcmp(ico_config_get_string("video.backend", "?"), "vulkan") == 0,
          "refused: the value stays Vulkan (%s)", ico_config_get_string("video.backend", "?"));

    /* answered later: polled every frame, Cross ignored meanwhile */
    s_gfxAnswer = UI_GFX_SWITCH_PENDING;
    s_gfxPollAnswers[0] = s_gfxPollAnswers[1] = s_gfxPollAnswers[2] = UI_GFX_SWITCH_PENDING;
    s_gfxPollAnswers[3] = UI_GFX_SWITCH_DONE;
    s_gfxPollN = 4;
    s_gfxPollAt = 0;
    s_gfxPolls = 0;
    const int before = s_gfxBegins;
    press(0x40); /* Apply, then two frames of polls */
    CHECK(!gfxRowOn(UI_OPT_GRAPHICS_KEEP) && s_gfxPolls == 2, "pending: no countdown yet (%d)",
          s_gfxPolls);
    press(0x40); /* the third poll's frame: ignored; the fourth answers */
    CHECK(s_gfxBegins == before + 1 && s_gfxPolls == 4 && gfxRowOn(UI_OPT_GRAPHICS_KEEP) &&
              lt_ext_layout(pageL)->curItem == revert,
          "pending: polled until done, one switch, then the countdown (%d polls, %d)", s_gfxPolls,
          s_gfxBegins - before);
    s_gfxAnswer = UI_GFX_SWITCH_DONE;
    lt_ext_layout(pageL)->curItem = keep;
    press(0x40);
    CHECK(s_gfxKepts == 2 && strcmp(s_gfxRunning, "vulkan") == 0, "kept on vulkan");

    /* a save during the countdown (the close button ends the run with
       ui_settings_save_on_quit): the value the page found, not the one on
       trial; the countdown goes on, and Keep then writes the choice */
    ui_settings_step(UI_OPT_GRAPHICS_API, 1);
    frame(0);
    lt_ext_layout(pageL)->curItem = apply;
    press(0x40);
    CHECK(gfxRowOn(UI_OPT_GRAPHICS_KEEP) && strcmp(s_gfxRunning, "opengl") == 0,
          "on trial: opengl");
    CHECK(ui_settings_save_on_quit() == 0, "save on quit in the countdown");
    {
        IcoToml *q = ico_toml_load(p);
        const char *b = q != NULL ? ico_toml_get(q, "video.backend") : NULL;
        CHECK(b != NULL && strcmp(b, "vulkan") == 0,
              "save in the countdown: [video] backend = vulkan, not the trial (%s)", b ? b : "-");
        ico_toml_free(q);
    }
    CHECK(gfxRowOn(UI_OPT_GRAPHICS_KEEP), "the countdown goes on after the save");
    lt_ext_layout(pageL)->curItem = keep;
    press(0x40);
    CHECK(ui_settings_save() == 0, "save after Keep");
    {
        IcoToml *q = ico_toml_load(p);
        const char *b = q != NULL ? ico_toml_get(q, "video.backend") : NULL;
        CHECK(b != NULL && strcmp(b, "opengl") == 0,
              "Keep then save: [video] backend = opengl (%s)", b ? b : "-");
        ico_toml_free(q);
    }

    /* Automatic on a run whose Automatic went on to OpenGL: no Apply on
       OpenGL (it would start Vulkan, which did not); on Vulkan, Apply
       starts OpenGL */
    snprintf(s_gfxAuto, sizeof(s_gfxAuto), "opengl");
    ico_config_set_string("video.backend", "");
    frame(0);
    CHECK(!gfxRowOn(UI_OPT_GRAPHICS_APPLY), "Automatic after a fallback to OpenGL: no Apply");
    snprintf(s_gfxRunning, sizeof(s_gfxRunning), "vulkan");
    frame(0);
    CHECK(gfxRowOn(UI_OPT_GRAPHICS_APPLY), "Automatic (OpenGL this run) on Vulkan: Apply");
    s_gfxAuto[0] = '\0';

    /* the strings, translated in every language */
    static const UiStrId kIds[] = {UI_STR_GRAPHICS_APPLY,    UI_STR_GRAPHICS_APPLY_NOTE,
                                   UI_STR_GRAPHICS_KEEP_ASK, UI_STR_GRAPHICS_KEEP,
                                   UI_STR_GRAPHICS_REVERT,   UI_STR_GRAPHICS_FAILED,
                                   UI_STR_GRAPHICS_API_NOTE};
    for (int l = 1; l < UI_LANG_COUNT; l++) {
        for (unsigned i = 0; i < sizeof(kIds) / sizeof(kIds[0]); i++) {
            const char *tr = ui_str_in((UiLang)l, kIds[i]);
            CHECK(tr[0] != '\0' && strcmp(tr, ui_str_in(UI_LANG_EN, kIds[i])) != 0,
                  "Apply: language %d, string %d translated", l, kIds[i]);
        }
    }
    ui_settings_set_graphics_host(NULL);
    ui_settings_set_backend_names(NULL, 0);
    ui_popup_set_vsync_hz(0);
    ui_popup_reset();
}

/* Extras > Add to Steam: hidden without a host handler, on the pause menu,
   and shown on the title with one; Cross runs the handler once and its
   answer is the popup; the strings exist in five languages */
static int s_steamCalls, s_steamAnswer;

static int fakeSteam(void)
{
    s_steamCalls++;
    return s_steamAnswer;
}

/* the Disc row's host: bit i, row i of config/regions.txt's game data */
static unsigned s_discHave;

static int fakeDiscPresent(const char *archiveName)
{
    for (int i = 0; i < ico_region_count(); i++) {
        if (strcmp(ico_region_at(i)->archive_name, archiveName) == 0) {
            return (int)((s_discHave >> i) & 1u);
        }
    }
    return 0;
}

/* Main > Disc: hidden without a host and with one disc's game data, shown
   with two or more (the step visits only discs with game data), after Menu language; European disc / Japanese disc, written
   to [game] disc (read at the next start) with its note; with no pin it
   shows the disc playing.  With every Main row shown (the Graphics driver
   link, Language, Disc and the two developer rows: fifteen) the rows are
   10 apart and the page fits. */
static void testDiscRow(void)
{
    useConfig("version = 1\n");
    ui_settings_set_disc_handler(NULL);
    enterMain(1);
    frame(0);
    CHECK(ui_settings_row_of(UI_PAGE_MAIN, UI_OPT_DISC) >= 0 &&
              !an22bShown(UI_PAGE_MAIN, UI_OPT_DISC),
          "disc: no host, the row is hidden");
    s_discHave = 1u;
    ui_settings_set_disc_handler(fakeDiscPresent);
    enterMain(1);
    frame(0);
    CHECK(!an22bShown(UI_PAGE_MAIN, UI_OPT_DISC) && !ui_settings_opt_shown(UI_OPT_DISC),
          "disc: one disc's game data, hidden");
    s_discHave = 3u;
    ui_settings_set_disc_handler(fakeDiscPresent);
    enterMain(1);
    frame(0);
    CHECK(an22bShown(UI_PAGE_MAIN, UI_OPT_DISC), "disc: both discs' game data, shown");
    {
        int labels[20];
        const int n = ui_settings_page_rows(UI_PAGE_MAIN, labels, NULL, NULL, 20);
        int at = -1, menu = -1;
        for (int i = 0; i < n; i++) {
            at = labels[i] == ui_settings_row_of(UI_PAGE_MAIN, UI_OPT_DISC) ? i : at;
            menu = labels[i] == ui_settings_row_of(UI_PAGE_MAIN, UI_OPT_MENU_LANGUAGE) ? i : menu;
        }
        CHECK(at >= 0 && at == menu + 1, "disc: the row after Menu language (%d, %d)", at, menu);
        CHECK(strcmp(lt_ext_row_text(ui_settings_row_of(UI_PAGE_MAIN, UI_OPT_DISC)), "Disc") == 0,
              "disc: the label \"%s\"",
              lt_ext_row_text(ui_settings_row_of(UI_PAGE_MAIN, UI_OPT_DISC)));
    }
    CHECK(ui_settings_note_row_of(UI_PAGE_MAIN, UI_OPT_DISC) >= 0, "disc: the row has its note");
    /* in every language the label fits its box (300) and both values the
       value box (176) at 60 % or more (layout_ext.c sets a wider one
       smaller, down to 60 %) */
    for (int g = 0; g < UI_LANG_COUNT; g++) {
        const float lw =
            ui_measure_menu_text(UI_MENU_TEXT_SIZE, ui_str_in((UiLang)g, UI_STR_OPT_DISC));
        const float pw =
            ui_measure_menu_text(UI_MENU_TEXT_SIZE, ui_str_in((UiLang)g, UI_STR_DISC_PAL));
        const float jw =
            ui_measure_menu_text(UI_MENU_TEXT_SIZE, ui_str_in((UiLang)g, UI_STR_DISC_JPN));
        const float kw =
            ui_measure_menu_text(UI_MENU_TEXT_SIZE, ui_str_in((UiLang)g, UI_STR_DISC_KOR));
        CHECK(lw * 0.6f <= 300.0f && pw * 0.6f <= 176.0f && jw * 0.6f <= 176.0f &&
                  kw * 0.6f <= 176.0f,
              "disc: language %d: the label and values fit (%.1f, %.1f, %.1f, %.1f)", g, (double)lw,
              (double)pw, (double)jw, (double)kw);
    }
    /* and the other four rows' values (the North American disc, the August
       2001 preview, the June 2001 demo, the Asian disc) */
    for (int g = 0; g < UI_LANG_COUNT; g++) {
        for (int id = UI_STR_DISC_NTSC; id <= UI_STR_DISC_CN; id++) {
            const float w =
                ui_measure_menu_text(UI_MENU_TEXT_SIZE, ui_str_in((UiLang)g, (UiStrId)id));
            CHECK(w * 0.6f <= 176.0f, "disc: language %d: value %d fits (%.1f)", g, id, (double)w);
        }
    }
    CHECK(strcmp(ui_settings_value_text(UI_OPT_DISC), "Europe (SCES-50760)") == 0,
          "disc: no pin, the disc playing: %s", ui_settings_value_text(UI_OPT_DISC));
    ui_settings_step(UI_OPT_DISC, 1);
    CHECK(strcmp(ico_config_get_string("game.disc", ""), "jpn") == 0 &&
              ico_config_disc_pin() == ICO_REGION_JPN &&
              strcmp(ui_settings_value_text(UI_OPT_DISC), "Japan (SCPS-11003)") == 0,
          "disc: Right pins the Japanese disc (%s, %s)", ico_config_get_string("game.disc", ""),
          ui_settings_value_text(UI_OPT_DISC));
    ui_settings_step(UI_OPT_DISC, 1);
    CHECK(strcmp(ico_config_get_string("game.disc", ""), "pal") == 0 &&
              strcmp(ui_settings_value_text(UI_OPT_DISC), "Europe (SCES-50760)") == 0,
          "disc: and wraps to the European one (%s)", ico_config_get_string("game.disc", ""));
    ui_settings_step(UI_OPT_DISC, -1);
    CHECK(strcmp(ico_config_get_string("game.disc", ""), "jpn") == 0, "disc: Left steps back");
    /* no pin on the Japanese disc: the row shows it */
    useConfig("version = 1\n");
    CHECK(ico_region_select("jpn") == 0, "disc: the Japanese disc selected");
    CHECK(strcmp(ui_settings_value_text(UI_OPT_DISC), "Japan (SCPS-11003)") == 0,
          "disc: no pin on the Japanese disc: %s", ui_settings_value_text(UI_OPT_DISC));
    CHECK(ico_region_select("pal") == 0, "disc: the European disc again");

    /* all three discs' game data: pal -> jpn -> kor -> pal in the table's order */
    s_discHave = 7u;
    ui_settings_set_disc_handler(fakeDiscPresent);
    useConfig("version = 1\n");
    enterMain(1);
    frame(0);
    ui_settings_step(UI_OPT_DISC, 1);
    ui_settings_step(UI_OPT_DISC, 1);
    CHECK(strcmp(ico_config_get_string("game.disc", ""), "kor") == 0 &&
              ico_config_disc_pin() == ICO_REGION_KOR &&
              strcmp(ui_settings_value_text(UI_OPT_DISC), "Korea (SCPS-56001)") == 0,
          "disc: two steps pin the Korean disc (%s, %s)", ico_config_get_string("game.disc", ""),
          ui_settings_value_text(UI_OPT_DISC));
    ui_settings_step(UI_OPT_DISC, 1);
    CHECK(strcmp(ico_config_get_string("game.disc", ""), "pal") == 0,
          "disc: and the third wraps to the European one (%s)",
          ico_config_get_string("game.disc", ""));
    useConfig("version = 1\n");
    CHECK(ico_region_select("kor") == 0 &&
              strcmp(ui_settings_value_text(UI_OPT_DISC), "Korea (SCPS-56001)") == 0,
          "disc: no pin on the Korean disc: %s", ui_settings_value_text(UI_OPT_DISC));
    CHECK(ico_region_select("pal") == 0, "disc: the European disc once more");
    s_discHave = 3u;
    ui_settings_set_disc_handler(fakeDiscPresent);

    /* every Main row: fifteen, 10 apart, Back's box inside the page */
    s_gpuN = 0;
    s_gpuSel = -1;
    s_gpuFailed = -1;
    s_gpuAdreno = 1;
    ui_settings_set_gpu_driver_host(&kGpuHost);
    enterMain(1);
    ico_opt_set_developer_mode(1); /* after enterMain, which reloads the options */
    for (int k = 0; k < 4; k++) {
        frame(0);
    }
    {
        int labels[20], shown = 0;
        const int n = ui_settings_page_rows(UI_PAGE_MAIN, labels, NULL, NULL, 20);
        for (int i = 0; i < n; i++) {
            const int on = !lt_ext_prop(labels[i])->defaultMask && !lt_ext_prop(labels[i])->masked;
            shown += on;
            if (!on) {
                printf("  disc: hidden: %s\n", lt_ext_row_text(labels[i]));
            }
        }
        const LtProperty *back = lt_ext_prop(labels[n - 1]);
        CHECK(shown == 15, "disc: every Main row shown (%d of %d)", shown, n);
        CHECK(back->dispY == 40 + 14 * 10, "disc: fifteen rows 10 apart, Back at %d", back->dispY);
        CHECK(back->dispY + back->dispH <= 226, "disc: Back's box ends at %d",
              back->dispY + back->dispH);
        checkPageFits(UI_PAGE_MAIN, "Main (every row)");
    }
    ico_opt_set_developer_mode(0);
    ui_settings_set_gpu_driver_host(NULL);
    ui_settings_set_disc_handler(NULL);
    useConfig("version = 1\n");
    enterMain(1);
    frame(0);
}

static void testAddToSteam(void)
{
    static const struct {
        int answer;
        const char *title;
    } kAnswers[] = {{UI_STEAM_ADDED, "Added to Steam"},
                    {UI_STEAM_EXISTS, "Already in your Steam library"},
                    {UI_STEAM_RUNNING, "Close Steam first"},
                    {UI_STEAM_NOT_FOUND, "Steam was not found"},
                    {UI_STEAM_NO_USER, "No Steam account found"},
                    {UI_STEAM_FAILED, "Could not add to Steam"},
                    {99, "Could not add to Steam"}};

    int mainL, exL, row;

    ui_popup_reset();
    ui_settings_set_steam_handler(NULL);

    /* no handler (Android, or a build without it): the row is hidden */
    mainL = enterMain(1);
    exL = openPage(mainL, 8, UI_PAGE_EXTRAS);
    CHECK(ui_settings_row_of(UI_PAGE_EXTRAS, UI_OPT_EXTRAS_STEAM) >= 0 &&
              !an22bShown(UI_PAGE_EXTRAS, UI_OPT_EXTRAS_STEAM),
          "no handler: the row is hidden");

    /* a handler, on the pause menu: still hidden (Extras leaves the stage) */
    ui_settings_set_steam_handler(fakeSteam);
    mainL = enterMain(0);
    exL = openPage(mainL, 8, UI_PAGE_EXTRAS);
    CHECK(!an22bShown(UI_PAGE_EXTRAS, UI_OPT_EXTRAS_STEAM), "pause menu: the row is hidden");

    /* a handler, on the title: shown, after Characters and before Back */
    mainL = enterMain(1);
    exL = openPage(mainL, 8, UI_PAGE_EXTRAS);
    row = ui_settings_row_of(UI_PAGE_EXTRAS, UI_OPT_EXTRAS_STEAM);
    CHECK(an22bShown(UI_PAGE_EXTRAS, UI_OPT_EXTRAS_STEAM), "title: the row shows");
    CHECK(strcmp(lt_ext_row_text(row), "Add to Steam") == 0, "the row's text \"%s\"",
          lt_ext_row_text(row));
    {
        int el[9];
        const int en = ui_settings_page_rows(UI_PAGE_EXTRAS, el, NULL, NULL, 9);

        CHECK(en == 9 && el[5] == row,
              "nine rows, Add to Steam then Export textures, the pop-ups and Back (%d)", en);
    }

    /* Cross: the handler once, its answer as the popup */
    for (unsigned i = 0; i < sizeof(kAnswers) / sizeof(kAnswers[0]); i++) {
        ui_popup_reset();
        s_steamCalls = 0;
        s_steamAnswer = kAnswers[i].answer;
        lt_ext_layout(exL)->curItem = row;
        frame(0);
        press(0x40);
        CHECK(s_steamCalls == 1, "answer %d: the handler ran once (%d)", kAnswers[i].answer,
              s_steamCalls);
        CHECK(ui_popup_active() && strcmp(ui_popup_title(), kAnswers[i].title) == 0,
              "answer %d: popup \"%s\"", kAnswers[i].answer, ui_popup_title());
        CHECK(current_layout_id == exL, "answer %d: stays on the page", kAnswers[i].answer);
    }
    ui_popup_reset();
    ui_settings_set_steam_handler(NULL);

    static const UiStrId kIds[] = {
        UI_STR_EXTRAS_STEAM,       UI_STR_STEAM_ADDED,          UI_STR_STEAM_ADDED_BODY,
        UI_STR_STEAM_EXISTS,       UI_STR_STEAM_RUNNING,        UI_STR_STEAM_RUNNING_BODY,
        UI_STR_STEAM_NOT_FOUND,    UI_STR_STEAM_NOT_FOUND_BODY, UI_STR_STEAM_NO_USER,
        UI_STR_STEAM_NO_USER_BODY, UI_STR_STEAM_FAILED,         UI_STR_STEAM_FAILED_BODY};
    for (int l = 1; l < UI_LANG_COUNT; l++) {
        for (unsigned i = 0; i < sizeof(kIds) / sizeof(kIds[0]); i++) {
            const char *t = ui_str_in((UiLang)l, kIds[i]);

            CHECK(t[0] != '\0' && strcmp(t, ui_str_in(UI_LANG_EN, kIds[i])) != 0,
                  "language %d, string %d translated", l, kIds[i]);
        }
    }
}

/* Extras > Export textures (issue 137) through a fake host: hidden without
   one and on the pause menu; on the title after Add to Steam, with its note;
   Cross begins once, the label reads Stop exporting and the note the
   progress; Cross again cancels; the end popup comes from ui_settings_vsync,
   also after leaving the page; a start that fails shows its popup at once;
   the strings are translated in every language */
static int s_texBegins, s_texCancels, s_texBeginAnswer;
static UiTextureExportStatus s_tex;

static int fakeTexBegin(void)
{
    s_texBegins++;
    if (s_texBeginAnswer == 0) {
        s_tex.state = UI_TEXEXPORT_RUNNING;
    }
    return s_texBeginAnswer;
}

static int fakeTexPoll(UiTextureExportStatus *out)
{
    *out = s_tex;
    snprintf(out->dir, sizeof(out->dir), "textures/SCES-50760/dumps");
    return out->state;
}

static void fakeTexCancel(void)
{
    s_texCancels++;
}

static void testExportTextures(void)
{
    static const UiTextureExportHost kHost = {fakeTexBegin, fakeTexPoll, fakeTexCancel};
    int mainL, exL, row;

    ui_popup_reset();
    memset(&s_tex, 0, sizeof(s_tex));
    s_texBegins = s_texCancels = s_texBeginAnswer = 0;
    ui_settings_set_texture_export_host(NULL);

    /* no host (headless, tests): hidden */
    mainL = enterMain(1);
    exL = openPage(mainL, 8, UI_PAGE_EXTRAS);
    CHECK(ui_settings_row_of(UI_PAGE_EXTRAS, UI_OPT_EXTRAS_EXPORT_TEXTURES) >= 0 &&
              !an22bShown(UI_PAGE_EXTRAS, UI_OPT_EXTRAS_EXPORT_TEXTURES),
          "export: no host, the row is hidden");

    /* a host, on the pause menu: hidden */
    ui_settings_set_texture_export_host(&kHost);
    mainL = enterMain(0);
    exL = openPage(mainL, 8, UI_PAGE_EXTRAS);
    CHECK(!an22bShown(UI_PAGE_EXTRAS, UI_OPT_EXTRAS_EXPORT_TEXTURES),
          "export: pause menu, the row is hidden");

    /* on the title: shown after Add to Steam, before the pop-ups */
    ui_settings_set_steam_handler(fakeSteam);
    mainL = enterMain(1);
    exL = openPage(mainL, 8, UI_PAGE_EXTRAS);
    row = ui_settings_row_of(UI_PAGE_EXTRAS, UI_OPT_EXTRAS_EXPORT_TEXTURES);
    CHECK(an22bShown(UI_PAGE_EXTRAS, UI_OPT_EXTRAS_EXPORT_TEXTURES),
          "export: title, the row shows");
    CHECK(strcmp(lt_ext_row_text(row), "Export textures") == 0, "export: the row's text \"%s\"",
          lt_ext_row_text(row));
    {
        int el[9];
        const int en = ui_settings_page_rows(UI_PAGE_EXTRAS, el, NULL, NULL, 9);

        CHECK(en == 9 && el[5] == ui_settings_row_of(UI_PAGE_EXTRAS, UI_OPT_EXTRAS_STEAM) &&
                  el[6] == row && el[7] == ui_settings_row_of(UI_PAGE_EXTRAS, UI_OPT_ACH_POPUPS),
              "export: nine rows, Add to Steam, Export textures, the pop-ups (%d)", en);
        /* Down from Add to Steam lands on it, Down again on the pop-ups */
        lt_ext_layout(exL)->curItem = el[5];
        frame(0);
        press(0x4000);
        CHECK(lt_ext_layout(exL)->curItem == row, "export: Down from Add to Steam lands on it");
        press(0x4000);
        CHECK(lt_ext_layout(exL)->curItem == el[7], "export: Down from it lands on the pop-ups");
    }
    ui_settings_set_steam_handler(NULL);
    lt_ext_layout(exL)->curItem = row;
    frame(0);
    {
        const int note = noteStarting(UI_PAGE_EXTRAS, "For pack makers: saves every");
        CHECK(note >= 0 && lt_ext_prop(note)->masked == 0, "export: the note under the row");
    }

    /* Cross: begin once, Stop exporting, the progress after a frame */
    s_tex.done = 3;
    s_tex.total = 10;
    press(0x40);
    CHECK(s_texBegins == 1 && s_texCancels == 0, "export: Cross began it (%d, %d)", s_texBegins,
          s_texCancels);
    CHECK(!ui_popup_active(), "export: no popup while it runs");
    frame(0);
    CHECK(strcmp(lt_ext_row_text(row), "Stop exporting") == 0, "export: running, \"%s\"",
          lt_ext_row_text(row));
    {
        const int note = noteStarting(UI_PAGE_EXTRAS, "Exporting textures: 3 of 10");
        CHECK(note >= 0 && lt_ext_prop(note)->masked == 0, "export: the progress note");
    }
    s_tex.done = 4;
    frame(0);
    CHECK(noteStarting(UI_PAGE_EXTRAS, "Exporting textures: 4 of 10") >= 0,
          "export: the progress follows");
    ui_settings_vsync();
    CHECK(!ui_popup_active(), "export: the vsync shows nothing while it runs");

    /* Cross again: cancel, no second begin */
    press(0x40);
    CHECK(s_texBegins == 1 && s_texCancels == 1, "export: Cross again cancelled (%d, %d)",
          s_texBegins, s_texCancels);

    /* it ends: the popup from the vsync, once */
    s_tex.state = UI_TEXEXPORT_DONE;
    s_tex.written = 7;
    ui_settings_vsync();
    CHECK(ui_popup_active() && strcmp(ui_popup_title(), "Textures exported") == 0 &&
              strcmp(ui_popup_body(), "Saved 7 pictures to\ntextures/SCES-50760/dumps") == 0,
          "export: done popup \"%s\" / \"%s\"", ui_popup_title(), ui_popup_body());
    ui_popup_reset();
    ui_settings_vsync();
    CHECK(!ui_popup_active(), "export: the end popup comes once");
    frame(0);
    CHECK(strcmp(lt_ext_row_text(row), "Export textures") == 0 &&
              noteStarting(UI_PAGE_EXTRAS, "For pack makers: saves every") >= 0,
          "export: ended, the row and its note again (\"%s\")", lt_ext_row_text(row));

    /* started, the page left, then stopped: the popup still comes */
    s_tex.written = 0;
    press(0x40);
    CHECK(s_texBegins == 2 && s_tex.state == UI_TEXEXPORT_RUNNING, "export: a second start");
    press(0x10);
    CHECK(settle(mainL, 60), "export: back on Main");
    s_tex.state = UI_TEXEXPORT_STOPPED;
    s_tex.written = 4;
    ui_settings_vsync();
    CHECK(ui_popup_active() && strcmp(ui_popup_title(), "Export stopped") == 0 &&
              strcmp(ui_popup_body(), "Stopped after 4 pictures") == 0,
          "export: stopped popup off the page \"%s\" / \"%s\"", ui_popup_title(), ui_popup_body());
    ui_popup_reset();

    /* a start that fails: its popup at once, nothing pending */
    s_texBeginAnswer = -1;
    exL = openPage(mainL, 8, UI_PAGE_EXTRAS);
    lt_ext_layout(exL)->curItem = row;
    frame(0);
    press(0x40);
    CHECK(s_texBegins == 3 && ui_popup_active() &&
              strcmp(ui_popup_title(), "Could not export textures") == 0 &&
              strcmp(ui_popup_body(),
                     "The folder textures/SCES-50760/dumps\ncould not be written") == 0,
          "export: failed popup \"%s\" / \"%s\"", ui_popup_title(), ui_popup_body());
    CHECK(current_layout_id == exL, "export: stays on the page");
    ui_popup_reset();
    s_tex.state = UI_TEXEXPORT_FAILED;
    ui_settings_vsync();
    CHECK(!ui_popup_active(), "export: a start that failed leaves nothing pending");
    ui_settings_set_texture_export_host(NULL);
    s_texBeginAnswer = 0;

    static const UiStrId kIds[] = {
        UI_STR_EXTRAS_EXPORT_TEXTURES,  UI_STR_EXPORT_TEXTURES_STOP,
        UI_STR_EXPORT_TEXTURES_NOTE,    UI_STR_EXPORT_TEXTURES_PROGRESS,
        UI_STR_EXPORT_TEXTURES_DONE,    UI_STR_EXPORT_TEXTURES_DONE_BODY,
        UI_STR_EXPORT_TEXTURES_STOPPED, UI_STR_EXPORT_TEXTURES_STOPPED_BODY,
        UI_STR_EXPORT_TEXTURES_FAILED,  UI_STR_EXPORT_TEXTURES_FAILED_BODY};
    for (int l = 1; l < UI_LANG_COUNT; l++) {
        for (unsigned i = 0; i < sizeof(kIds) / sizeof(kIds[0]); i++) {
            const char *t = ui_str_in((UiLang)l, kIds[i]);

            CHECK(t[0] != '\0' && strcmp(t, ui_str_in(UI_LANG_EN, kIds[i])) != 0,
                  "export: language %d, string %d translated", l, kIds[i]);
        }
    }
}

/* Main > Save a picture for a bug report (issue 129): hidden without a
   handler (the headless build); with one shown with developer mode off and
   on, directly above Developer mode, with its note, and turning developer
   mode on or off does not hide it; Cross calls the handler once (the host
   arms its frame dump) and shows the popup, the cursor staying on the
   page.
   With every Main row shown (the Graphics driver link, Disc and the three
   developer rows: sixteen) the rows are 10 apart from 30 and the page
   fits.  The label fits its box in every language and every string is
   translated. */
static int s_reportCalls;

static void fakeReportPicture(void)
{
    s_reportCalls++;
}

static void testReportPicture(void)
{
    useConfig("version = 1\n");
    ui_popup_reset();
    ui_settings_set_frame_dump_handler(NULL);
    int mainL = enterMain(1);
    ico_opt_set_developer_mode(1);
    for (int k = 0; k < 4; k++) {
        frame(0);
    }
    CHECK(ui_settings_row_of(UI_PAGE_MAIN, UI_OPT_REPORT_PICTURE) >= 0 &&
              !an22bShown(UI_PAGE_MAIN, UI_OPT_REPORT_PICTURE),
          "report picture: no handler, the row is hidden");
    ico_opt_set_developer_mode(0);
    ui_settings_set_frame_dump_handler(fakeReportPicture);
    mainL = enterMain(1);
    frame(0);
    CHECK(an22bShown(UI_PAGE_MAIN, UI_OPT_REPORT_PICTURE) && !ico_opt_developer_mode(),
          "report picture: developer mode off, the row is shown");
    {
        int labels[20];
        const int n = ui_settings_page_rows(UI_PAGE_MAIN, labels, NULL, NULL, 20);
        const int rp = ui_settings_row_of(UI_PAGE_MAIN, UI_OPT_REPORT_PICTURE);
        const int dev = ui_settings_row_of(UI_PAGE_MAIN, UI_OPT_DEVELOPER);
        int at = -1, devAt = -1;
        for (int i = 0; i < n; i++) {
            at = labels[i] == rp ? i : at;
            devAt = labels[i] == dev ? i : devAt;
        }
        CHECK(at >= 0 && at + 1 == devAt, "report picture: directly above Developer mode (%d, %d)",
              at, devAt);
        checkPageFits(UI_PAGE_MAIN, "Main (Save a picture, developer mode off)");
    }
    ico_opt_set_developer_mode(1); /* after enterMain, which reloads the options */
    for (int k = 0; k < 4; k++) {
        frame(0);
    }
    const int row = ui_settings_row_of(UI_PAGE_MAIN, UI_OPT_REPORT_PICTURE);
    CHECK(an22bShown(UI_PAGE_MAIN, UI_OPT_REPORT_PICTURE),
          "report picture: shown with developer mode on");
    CHECK(row >= 0 && strcmp(lt_ext_row_text(row), "Save a picture for a bug report") == 0,
          "report picture: the label \"%s\"", row >= 0 ? lt_ext_row_text(row) : "");
    CHECK(ui_settings_note_row_of(UI_PAGE_MAIN, UI_OPT_REPORT_PICTURE) >= 0,
          "report picture: the row has its note");
    {
        int labels[20];
        const int n = ui_settings_page_rows(UI_PAGE_MAIN, labels, NULL, NULL, 20);
        int at = -1, dev = -1;
        for (int i = 0; i < n; i++) {
            at = labels[i] == row ? i : at;
            dev = labels[i] == ui_settings_row_of(UI_PAGE_MAIN, UI_OPT_DEVELOPER) ? i : dev;
        }
        CHECK(at >= 0 && at + 1 == dev, "report picture: directly above Developer mode (%d, %d)",
              at, dev);
    }
    s_reportCalls = 0;
    ui_popup_reset();
    lt_ext_layout(mainL)->curItem = row;
    frame(0);
    press(0x40);
    CHECK(s_reportCalls == 1, "report picture: Cross calls the handler once (%d)", s_reportCalls);
    CHECK(ui_popup_active() && strcmp(ui_popup_title(), "Picture in 3 seconds") == 0,
          "report picture: popup \"%s\"", ui_popup_title());
    CHECK(current_layout_id == mainL, "report picture: stays on the page");
    ui_popup_reset();

    /* every Main row: sixteen, from 30, 10 apart, Back's box inside */
    s_gpuN = 0;
    s_gpuSel = -1;
    s_gpuFailed = -1;
    s_gpuAdreno = 1;
    ui_settings_set_gpu_driver_host(&kGpuHost);
    s_discHave = 3u;
    ui_settings_set_disc_handler(fakeDiscPresent);
    enterMain(1);
    ico_opt_set_developer_mode(1);
    for (int k = 0; k < 4; k++) {
        frame(0);
    }
    {
        int labels[20], shown = 0;
        const int n = ui_settings_page_rows(UI_PAGE_MAIN, labels, NULL, NULL, 20);
        for (int i = 0; i < n; i++) {
            shown += !lt_ext_prop(labels[i])->defaultMask && !lt_ext_prop(labels[i])->masked;
        }
        const LtProperty *back = lt_ext_prop(labels[n - 1]);
        CHECK(shown == 16, "report picture: every Main row shown (%d of %d)", shown, n);
        CHECK(back->dispY == 30 + 15 * 10, "report picture: sixteen rows 10 apart, Back at %d",
              back->dispY);
        CHECK(back->dispY + back->dispH <= 226, "report picture: Back's box ends at %d",
              back->dispY + back->dispH);
        checkPageFits(UI_PAGE_MAIN, "Main (every row, Save a picture)");
    }
    ico_opt_set_developer_mode(0);
    ui_settings_set_gpu_driver_host(NULL);
    ui_settings_set_disc_handler(NULL);
    ui_settings_set_frame_dump_handler(NULL);
    useConfig("version = 1\n");

    for (int g = 0; g < UI_LANG_COUNT; g++) {
        const float lw = ui_measure_menu_text(UI_MENU_TEXT_SIZE,
                                              ui_str_in((UiLang)g, UI_STR_OPT_REPORT_PICTURE));
        CHECK(lw * 0.6f <= 300.0f, "report picture: language %d: the label fits (%.1f)", g,
              (double)lw);
    }
    static const UiStrId kIds[] = {UI_STR_OPT_REPORT_PICTURE, UI_STR_REPORT_PICTURE_NOTE,
                                   UI_STR_REPORT_PICTURE_ARMED, UI_STR_REPORT_PICTURE_ARMED_BODY};
    for (int l = 1; l < UI_LANG_COUNT; l++) {
        for (unsigned i = 0; i < sizeof(kIds) / sizeof(kIds[0]); i++) {
            const char *t = ui_str_in((UiLang)l, kIds[i]);
            CHECK(t[0] != '\0' && strcmp(t, ui_str_in(UI_LANG_EN, kIds[i])) != 0,
                  "report picture: language %d, string %d translated", l, kIds[i]);
        }
    }
}

/* Main > Share a bug report, an rc build's row alone (ICO_RC_TOOLS, the
   release candidates' tooling; docs/BUILDING.md).  rc_tools_off: in the
   default build, on a phone with every other Main row shown and a share
   handler set, the page has its sixteen rows shown (seventeen built: the
   Graphics link hides, a phone's build linking Vulkan alone), none of them
   the share row,
   and no string of the English table is its text (a release has no such
   row, CMakeLists.txt's rc_tools_binary checks the program too).  With
   ICO_RC_TOOLS: the row shows on a phone with a handler, directly below
   Save a picture for a bug report, with its note; not on a desktop, not
   without a handler; Cross calls the handler once and stays on the page;
   the seventeen rows are 10 apart from 26 and the page fits; the label
   fits and both strings are translated. */
static int s_shareCalls;

static void fakeShareReport(void)
{
    s_shareCalls++;
}

static int mainRowsShown(int *labels, int max, int *n)
{
    int shown = 0;
    *n = ui_settings_page_rows(UI_PAGE_MAIN, labels, NULL, NULL, max);
    for (int i = 0; i < *n; i++) {
        shown += !lt_ext_prop(labels[i])->defaultMask && !lt_ext_prop(labels[i])->masked;
    }
    return shown;
}

static void testShareReport(void)
{
    static const char kText[] = "Share a bug report";

    useConfig("version = 1\n");
    ui_popup_reset();
    ico_video_set_android(1);
    s_gpuN = 0;
    s_gpuSel = -1;
    s_gpuFailed = -1;
    s_gpuAdreno = 1;
    ui_settings_set_gpu_driver_host(&kGpuHost);
    s_discHave = 3u;
    ui_settings_set_disc_handler(fakeDiscPresent);
    ui_settings_set_frame_dump_handler(fakeReportPicture);
    ui_settings_set_share_report_handler(fakeShareReport);
    /* a phone's build links Vulkan alone: no Graphics link */
    static const char *const kPhoneBackends[] = {"vulkan"};
    ui_settings_set_backend_names(kPhoneBackends, 1);
    int mainL = enterMain(1);
    ico_opt_set_developer_mode(1);
    for (int k = 0; k < 4; k++) {
        frame(0);
    }
    int labels[24], n = 0, textRow = -1;
    const int shown = mainRowsShown(labels, 24, &n);
    for (int i = 0; i < n; i++) {
        textRow = strcmp(lt_ext_row_text(labels[i]), kText) == 0 ? i : textRow;
    }
#if !ICO_RC_TOOLS
    CHECK(n == 17 && shown == 16,
          "rc_tools_off: Main has its sixteen rows shown on a phone with a share handler (%d, %d "
          "shown)",
          n, shown);
    CHECK(textRow < 0, "rc_tools_off: no Main row is \"%s\" (row %d)", kText, textRow);
    for (int id = 0; id < UI_STR_COUNT; id++) {
        CHECK(strcmp(ui_str_in(UI_LANG_EN, (UiStrId)id), kText) != 0,
              "rc_tools_off: string %d of the English table is \"%s\"", id, kText);
    }
    (void)mainL;
#else
    const int row = ui_settings_row_of(UI_PAGE_MAIN, UI_OPT_SHARE_REPORT);
    const int pic = ui_settings_row_of(UI_PAGE_MAIN, UI_OPT_REPORT_PICTURE);
    int at = -1, picAt = -1;
    for (int i = 0; i < n; i++) {
        at = labels[i] == row ? i : at;
        picAt = labels[i] == pic ? i : picAt;
    }
    CHECK(n == 18 && shown == 17, "rc tools: seventeen Main rows shown on a phone (%d, %d)", n,
          shown);
    CHECK(row >= 0 && an22bShown(UI_PAGE_MAIN, UI_OPT_SHARE_REPORT) && at == textRow,
          "rc tools: the row shows as \"%s\" (%d, %d)", kText, at, textRow);
    CHECK(picAt >= 0 && at == picAt + 1, "rc tools: directly below Save a picture (%d, %d)", picAt,
          at);
    CHECK(ui_settings_note_row_of(UI_PAGE_MAIN, UI_OPT_SHARE_REPORT) >= 0,
          "rc tools: the row has its note");
    {
        const LtProperty *back = lt_ext_prop(labels[n - 1]);
        CHECK(back->dispY == 26 + 16 * 10, "rc tools: seventeen rows 10 apart, Back at %d",
              back->dispY);
        CHECK(back->dispY + back->dispH <= 226, "rc tools: Back's box ends at %d",
              back->dispY + back->dispH);
        checkPageFits(UI_PAGE_MAIN, "Main (every row, Share a bug report)");
    }
    s_shareCalls = 0;
    lt_ext_layout(mainL)->curItem = row;
    frame(0);
    press(0x40);
    CHECK(s_shareCalls == 1, "rc tools: Cross calls the handler once (%d)", s_shareCalls);
    CHECK(current_layout_id == mainL, "rc tools: stays on the page");
    ui_popup_reset();
    /* a desktop, and a phone without a handler: no row */
    ico_video_set_android(0);
    enterMain(1);
    frame(0);
    CHECK(!an22bShown(UI_PAGE_MAIN, UI_OPT_SHARE_REPORT), "rc tools: hidden on a desktop");
    ico_video_set_android(1);
    ui_settings_set_share_report_handler(NULL);
    enterMain(1);
    frame(0);
    CHECK(!an22bShown(UI_PAGE_MAIN, UI_OPT_SHARE_REPORT), "rc tools: hidden without a handler");
    for (int g = 0; g < UI_LANG_COUNT; g++) {
        const float lw =
            ui_measure_menu_text(UI_MENU_TEXT_SIZE, ui_str_in((UiLang)g, UI_STR_OPT_SHARE_REPORT));
        CHECK(lw * 0.6f <= 300.0f, "rc tools: language %d: the label fits (%.1f)", g, (double)lw);
    }
    static const UiStrId kIds[] = {UI_STR_OPT_SHARE_REPORT, UI_STR_SHARE_REPORT_NOTE};
    for (int l = 1; l < UI_LANG_COUNT; l++) {
        for (unsigned i = 0; i < sizeof(kIds) / sizeof(kIds[0]); i++) {
            const char *t = ui_str_in((UiLang)l, kIds[i]);
            CHECK(t[0] != '\0' && strcmp(t, ui_str_in(UI_LANG_EN, kIds[i])) != 0,
                  "rc tools: language %d, string %d translated", l, kIds[i]);
        }
    }
#endif
    ico_opt_set_developer_mode(0);
    ico_video_set_android(0);
    ui_settings_set_gpu_driver_host(NULL);
    ui_settings_set_disc_handler(NULL);
    ui_settings_set_frame_dump_handler(NULL);
    ui_settings_set_share_report_handler(NULL);
    ui_settings_set_backend_names(NULL, 0);
    useConfig("version = 1\n");
}

static void testQuitGame(void)
{
    static const char *const kAsk[2] = {"Quit to desktop?", "Quit the game?"};
    static const char *const kRow[2] = {"Quit to desktop", "Quit game"};

    for (int game = 0; game < 2; game++) {
        useConfig("version = 1\n");
        fakeTables();
        lt_ext_reset();
        ui_settings_reset();
        memset(pad, 0, sizeof(pad));
        pad[0].ana[0] = pad[0].ana[1] = pad[0].ana[2] = pad[0].ana[3] = 128;
        NonLinearCameraMove = 2;
        s_an22bQuits = 0;
        s_an22bGame = game;
        ui_settings_set_quit_handler(an22bQuit);
        ui_settings_set_quit_is_game(game ? an22bIsGame : NULL);
        init_layout_texture(2);
        settle(54, 4);
        const int q13 = ui_settings_quit_row(13), ql = ui_quit_screen_layout();
        CHECK(strcmp(lt_ext_row_text(q13), kRow[game]) == 0, "game %d: the row is \"%s\"", game,
              lt_ext_row_text(q13));
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
    const int q13 = ui_settings_quit_row(13), ql = ui_quit_screen_layout();
    s_an22bGame = 0;
    ui_settings_set_quit_is_game(an22bIsGame);
    CHECK(strcmp(lt_ext_row_text(q13), "Quit to desktop") == 0 &&
              an22bTextIn(ql, "Quit to desktop?") >= 0,
          "a hook answering 0 keeps the desktop words");
    s_an22bGame = 1;
    ui_settings_set_quit_is_game(an22bIsGame);
    CHECK(strcmp(lt_ext_row_text(q13), "Quit game") == 0 && an22bTextIn(ql, "Quit the game?") >= 0,
          "a late hook answering 1 changes the words");
    ui_settings_set_quit_is_game(NULL);
    ui_settings_set_quit_handler(NULL);
    CHECK(strcmp(lt_ext_row_text(q13), "Quit to desktop") == 0,
          "no hook: back to the desktop words");
}

/* Controls' mouse rows: Mouse camera and Invert mouse up/down are On and
   Off on the live table, saved as [input] mouse_camera and mouse_invert_y;
   Mouse camera speed, Mouse camera range and Camera swings back follow
   Invert (mouse_camera_speed, mouse_full_range, mouse_return, written only
   when changed); the six mouse rows come after Hold type, hidden on
   Android; fourteen rows (the pause menu with a touch screen: its Touch
   screen row) fit 11 lines apart */
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
    CHECK(strcmp(ui_settings_value_text(UI_OPT_MOUSE_CAMERA), "On") == 0 &&
              strcmp(ui_settings_value_text(UI_OPT_MOUSE_INVERT), "Off") == 0,
          "values: On, Off (%s)", ui_settings_value_text(UI_OPT_MOUSE_CAMERA));
    CHECK(strcmp(ui_str(UI_STR_OPT_MOUSE_INVERT), "Invert mouse up/down") == 0,
          "the Invert row's label");
    CHECK(b->mouse_camera_speed == 1.0f && b->mouse_full_range == 0 && b->mouse_return == 1,
          "defaults: speed 1, normal range, swings back");
    CHECK(strcmp(ui_settings_value_text(UI_OPT_MOUSE_SPEED), "1.0x") == 0 &&
              strcmp(ui_settings_value_text(UI_OPT_MOUSE_RANGE), "Normal") == 0 &&
              strcmp(ui_settings_value_text(UI_OPT_MOUSE_RETURN), "On") == 0,
          "values: 1.0x, Normal, On");
    CHECK(strcmp(ui_str(UI_STR_OPT_MOUSE_SPEED), "Mouse camera speed") == 0 &&
              strcmp(ui_str(UI_STR_OPT_MOUSE_RANGE), "Mouse camera range") == 0 &&
              strcmp(ui_str(UI_STR_OPT_MOUSE_RETURN), "Camera swings back") == 0,
          "the three new labels");

    /* each step flips the live table at once; the save writes both keys */
    ui_settings_step(UI_OPT_MOUSE_CAMERA, 1);
    CHECK(b->mouse_camera == 0 && strcmp(ui_settings_value_text(UI_OPT_MOUSE_CAMERA), "Off") == 0,
          "Right: the mouse camera off");
    ui_settings_step(UI_OPT_MOUSE_INVERT, -1);
    CHECK(b->mouse_invert_y == 1 && strcmp(ui_settings_value_text(UI_OPT_MOUSE_INVERT), "On") == 0,
          "Left: inverted");
    CHECK(ui_settings_save() == 0, "save");
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
    ui_settings_step(UI_OPT_MOUSE_CAMERA, 1);
    ui_settings_step(UI_OPT_MOUSE_INVERT, 1);
    CHECK(b->mouse_camera == 1 && b->mouse_invert_y == 0, "stepped back");

    /* the speed steps 0.5, 1, 1.5, 2, 3, 5, Instant and stops at the ends */
    {
        static const char *const kText[7] = {"0.5x", "1.0x", "1.5x",   "2.0x",
                                             "3.0x", "5.0x", "Instant"};
        for (int i = 2; i < 7; i++) {
            ui_settings_step(UI_OPT_MOUSE_SPEED, 1);
            CHECK(strcmp(ui_settings_value_text(UI_OPT_MOUSE_SPEED), kText[i]) == 0,
                  "speed step up to %s (%s)", kText[i], ui_settings_value_text(UI_OPT_MOUSE_SPEED));
        }
        CHECK(b->mouse_camera_speed == 10.0f, "Instant is 10");
        ui_settings_step(UI_OPT_MOUSE_SPEED, 1);
        CHECK(b->mouse_camera_speed == 10.0f, "speed stops at Instant");
        for (int i = 5; i >= 0; i--) {
            ui_settings_step(UI_OPT_MOUSE_SPEED, -1);
            CHECK(strcmp(ui_settings_value_text(UI_OPT_MOUSE_SPEED), kText[i]) == 0,
                  "speed step down to %s (%s)", kText[i],
                  ui_settings_value_text(UI_OPT_MOUSE_SPEED));
        }
        ui_settings_step(UI_OPT_MOUSE_SPEED, -1);
        CHECK(b->mouse_camera_speed == 0.5f, "speed stops at 0.5x");
        ui_settings_step(UI_OPT_MOUSE_RANGE, 1);
        ui_settings_step(UI_OPT_MOUSE_RETURN, 1);
        CHECK(b->mouse_full_range == 1 && b->mouse_return == 0 &&
                  strcmp(ui_settings_value_text(UI_OPT_MOUSE_RANGE), "Full") == 0 &&
                  strcmp(ui_settings_value_text(UI_OPT_MOUSE_RETURN), "Off") == 0,
              "range Full, swings back Off");
        CHECK(ui_settings_save() == 0, "save the three");
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
        ui_settings_step(UI_OPT_MOUSE_SPEED, 1);
        ui_settings_step(UI_OPT_MOUSE_RANGE, 1);
        ui_settings_step(UI_OPT_MOUSE_RETURN, 1);
        CHECK(b->mouse_camera_speed == 1.0f && b->mouse_full_range == 0 && b->mouse_return == 1,
              "the three back at their defaults");
    }

    /* the rows: after Hold type from the pause menu, after Remap on the
       title; hidden on Android */
    for (int android = 0; android < 2; android++) {
        ico_video_set_android(android);
        for (int title = 1; title >= 0; title--) {
            ui_settings_set_touch_query(NULL);
            const int mainL = enterMain(title);
            const int ctlL = openPage(mainL, 4, UI_PAGE_CONTROLS);
            for (int i = 0; i < 6; i++) {
                const int row = ui_settings_row_of(UI_PAGE_CONTROLS, kMouse[i]);
                const int shown =
                    row >= 0 && !lt_ext_prop(row)->defaultMask && !lt_ext_prop(row)->masked;
                CHECK(shown == !android, "android %d, title %d: mouse row %d %s", android, title, i,
                      android ? "hidden" : "shown");
            }
            const int remap = ui_settings_row_of(UI_PAGE_CONTROLS, UI_OPT_LINK);
            const int below = lt_ext_prop(remap)->downItem;
            /* Hold type shows on the title too ([gameplay] hold_type) */
            const UiSettingsOpt want = title ? UI_OPT_HOLD_TYPE : UI_OPT_BUTTON_CONFIG;
            CHECK(below == ui_settings_row_of(UI_PAGE_CONTROLS, want),
                  "android %d, title %d: down from Remap (%d)", android, title, below);
            if (!android) {
                const int hold = ui_settings_row_of(UI_PAGE_CONTROLS, UI_OPT_HOLD_TYPE);
                CHECK(lt_ext_prop(hold)->downItem ==
                          ui_settings_row_of(UI_PAGE_CONTROLS, UI_OPT_STICK_FIX),
                      "title %d: Stick fix after Hold type", title);
                CHECK(
                    lt_ext_prop(ui_settings_row_of(UI_PAGE_CONTROLS, UI_OPT_STICK_FIX))->downItem ==
                        ui_settings_row_of(UI_PAGE_CONTROLS, UI_OPT_MOUSE_CAMERA),
                    "title %d: Mouse camera after Stick fix", title);
            }
            if (!android) {
                /* Invert, then Speed, Range, Swings back, then Circle back */
                static const UiSettingsOpt kOrder[5] = {UI_OPT_MOUSE_INVERT, UI_OPT_MOUSE_SPEED,
                                                        UI_OPT_MOUSE_RANGE, UI_OPT_MOUSE_RETURN,
                                                        UI_OPT_CIRCLE_BACK};
                for (int i = 0; i < 4; i++) {
                    CHECK(lt_ext_prop(ui_settings_row_of(UI_PAGE_CONTROLS, kOrder[i]))->downItem ==
                              ui_settings_row_of(UI_PAGE_CONTROLS, kOrder[i + 1]),
                          "title %d: row order after mouse row %d", title, i);
                }
            }
            (void)ctlL;
            press(0x10);
            CHECK(settle(mainL, 60), "Controls: back");
        }
    }
    ico_video_set_android(0);

    /* fourteen rows with a touch screen from the pause menu: 11 lines apart
       from 30, the last box inside the 226 lines */
    s_i17aTouch = 1;
    ui_settings_set_touch_query(i17aTouch);
    const int mainL = enterMain(0);
    openPage(mainL, 4, UI_PAGE_CONTROLS);
    for (int k = 0; k < 4; k++) {
        frame(0);
    }
    int labels[16], n = 0, prev = -1, last = -1;
    const int c = ui_settings_page_rows(UI_PAGE_CONTROLS, labels, NULL, NULL, 16);
    for (int i = 0; i < c; i++) {
        const LtProperty *r = lt_ext_prop(labels[i]);
        if (r->defaultMask) {
            continue;
        }
        CHECK(prev < 0 ? r->dispY >= 28 : r->dispY >= prev + 11,
              "fourteen rows: row %d at y %d (the one above at %d)", i, r->dispY, prev);
        prev = r->dispY;
        last = labels[i];
        n++;
    }
    CHECK(n == 14, "fourteen rows shown (%d)", n);
    CHECK(last >= 0 && lt_ext_prop(last)->dispY + lt_ext_prop(last)->dispH <= 226,
          "fourteen rows: the last box ends at %d",
          last >= 0 ? lt_ext_prop(last)->dispY + lt_ext_prop(last)->dispH : -1);
    press(0x10);
    CHECK(settle(mainL, 60), "Controls: back");
    ui_settings_set_touch_query(NULL);
}

/* the title run unseen while Options is to reopen */
static int rowsMasked(int layout, int want)
{
    const LtProp *lp = lt_ext_layout(layout);
    for (int r = lp->first; r < lp->last; r++) {
        if (lt_ext_prop(r)->masked != want) {
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
        const int opt = ui_settings_entry_row(title), quit = ui_settings_quit_row(title);

        /* nothing pending: the entry rows follow the mask, nothing else */
        CHECK(!ui_settings_title_return_pending() && !ui_settings_covers_title(),
              "%d: nothing pending, the logo shows", title);
        ui_settings_title_mask(1);
        CHECK(!ui_settings_title_decided() && lt_ext_prop(opt)->masked && lt_ext_prop(quit)->masked,
              "%d: unchanged: the entry rows masked", title);
        ui_settings_title_mask(0);
        CHECK(ui_settings_title_decided() && !lt_ext_prop(opt)->masked &&
                  !lt_ext_prop(quit)->masked,
              "%d: unchanged: unmasked once decided", title);
        CHECK(ui_settings_entry_item(51) == 0 && ui_settings_entry_item(opt) == 1,
              "%d: unchanged: only the entry rows", title);

        /* armed: the logo is down from this frame, the rows masked, no confirm */
        ui_settings_title_return(UI_PAGE_EXTRAS);
        CHECK(ui_settings_title_return_pending() && ui_settings_covers_title(), "%d: armed covers",
              title);
        CHECK(ui_settings_entry_item(51) == 1 && ui_settings_entry_item(0) == 1,
              "%d: every item is an entry item while pending", title);
        lt_item_select_disable = 0;
        ui_settings_title_mask(1);
        CHECK(rowsMasked(title, 1) && lt_ext_prop(opt)->masked && lt_ext_prop(quit)->masked &&
                  lt_item_select_disable == 1 && !ui_settings_title_decided(),
              "%d: TitleMask(1): rows masked, selection disabled, undecided", title);
        lt_item_select_disable = 0;
        ui_settings_title_mask(0);
        CHECK(rowsMasked(title, 1) && lt_ext_prop(opt)->masked && lt_item_select_disable == 1 &&
                  ui_settings_title_decided(),
              "%d: TitleMask(0): still masked, decided", title);

        /* cancelled */
        ui_settings_title_return(-1);
        CHECK(!ui_settings_title_return_pending() && !ui_settings_covers_title() &&
                  ui_settings_entry_item(51) == 0,
              "%d: cancelled: the logo and the confirms are back", title);

        /* the reopen ends it; Back from Main returns to this title layout */
        ui_settings_title_return(UI_PAGE_EXTRAS);
        ui_settings_title_mask(0);
        const int exL = ui_settings_reopen_page(UI_PAGE_EXTRAS);
        CHECK(exL >= 0 && !ui_settings_title_return_pending(), "%d: the reopen clears pending",
              title);
        CHECK(ui_settings_covers_title(), "%d: the logo stays down through the switch", title);
        lt_switch_layout(exL);
        CHECK(settle(exL, 60) && ui_settings_covers_title(), "%d: Extras up", title);
        press(0x10);
        CHECK(settle(ui_settings_page_layout(UI_PAGE_MAIN), 60), "%d: Back: Main", title);
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
   too while deciding, ui_settings_title_mask, which would hide the flag's
   effect here) */
static int i17bTitleProc(int first, int item)
{
    (void)first;
    (void)item;
    lt_mask_property(51, 0);
    ui_settings_title_mask(0);
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
    ui_mouse_tick();
    frame(pad[0].flags);
}

static void mouseFrame(void)
{
    mouseFrameWith(0);
}

/* the pointer on the middle of row j (or dx grid pixels off it) */
static void pointAtOff(int j, float dx)
{
    const LtProperty *e = lt_ext_prop(j);
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
    int labels[20];
    const int n = ui_settings_page_rows(UI_PAGE_MAIN, labels, NULL, NULL, 20);
    for (int i = 0; i < n; i++) {
        if (strcmp(lt_ext_row_text(labels[i]), ui_str((UiStrId)strId)) == 0) {
            return i;
        }
    }
    return -1;
}

/* the arrow of the value of label in layout l with role, -1 */
static int i17bArrow(int l, int label, int role)
{
    const LtProp *lay = lt_ext_layout(l);
    for (int j = lay->first; j < lay->last; j++) {
        if (lt_ext_prop(j)->ownerItem == label && lt_ext_pointer_role(j) == role) {
            return j;
        }
    }
    return -1;
}

static void testPointer(void)
{
    char before[64], after[64];
    int labels[20];

    ui_mouse_reset();
    ui_mouse_set_view(640, 480, 0, 0, 640, 480);

    /* play (layout 54) is no menu */
    useConfig("version = 1\n");
    fakeTables();
    lt_ext_reset();
    ui_settings_reset();
    memset(pad, 0, sizeof(pad));
    pad[0].ana[0] = pad[0].ana[1] = pad[0].ana[2] = pad[0].ana[3] = 128;
    init_layout_texture(2);
    settle(54, 4);
    ico_pointer_move(0.5f, 0.5f);
    mouseFrame();
    CHECK(!ui_mouse_menu_active() && !ico_pointer_menu(), "layout 54: no menu");

    /* the title while its memory card check runs: the item select off, so
       pointing at the Settings row moves nothing until it is decided */
    lt_switch_layout(13);
    CHECK(settle(13, 60), "pointer: the title");
    texLayout[13].proc = i17bTitleProc;
    s_i17bDeciding = 1;
    mouseFrame();
    mouseFrame();
    const int entry = ui_settings_entry_row(13);
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
    ui_settings_page_rows(UI_PAGE_MAIN, labels, NULL, NULL, 20);
    CHECK(iDisp >= 0 && iAch >= 0 && iCtl >= 0, "the main page's rows (%d %d %d)", iDisp, iAch,
          iCtl);
    lt_ext_layout(mainL)->curItem = labels[iCtl];
    mouseFrame();
    mouseFrame();
    CHECK(ico_pointer_menu() == 1 && ui_mouse_menu_active(), "the main page is a menu");
    pointAt(labels[iDisp]);
    mouseFrame();
    CHECK(lt_ext_layout(mainL)->curItem == labels[iDisp], "hover: the cursor on Display (%d)",
          lt_ext_layout(mainL)->curItem);
    CHECK(current_layout_id == mainL, "hover opens nothing");
    click();
    mouseFrame();
    CHECK(lt_fade_status() != 2, "the click starts the fade (%d)", lt_fade_status());
    /* nothing while the layout fades */
    pointAt(labels[iCtl]);
    mouseFrame();
    CHECK(lt_ext_layout(mainL)->curItem == labels[iDisp], "fading: the cursor stays (%d)",
          lt_ext_layout(mainL)->curItem);
    const int dispL = ui_settings_page_layout(UI_PAGE_DISPLAY);
    CHECK(settle(dispL, 60), "click: Display opens (%d)", current_layout_id);

    /* Aspect's arrows: Right as the pad's Right, Left back; the value
       itself steps as Right */
    const int aspect = ui_settings_row_of(UI_PAGE_DISPLAY, UI_OPT_ASPECT);
    const int left = i17bArrow(dispL, aspect, LT_POINTER_LEFT);
    const int right = i17bArrow(dispL, aspect, LT_POINTER_RIGHT);
    CHECK(aspect >= 0 && left >= 0 && right >= 0, "Aspect and its arrows (%d %d %d)", aspect, left,
          right);
    mouseFrame();
    mouseFrame();
    snprintf(before, sizeof(before), "%s", ui_settings_value_text(UI_OPT_ASPECT));
    pointAt(right);
    click();
    mouseFrame();
    frame(0);
    frame(0);
    snprintf(after, sizeof(after), "%s", ui_settings_value_text(UI_OPT_ASPECT));
    CHECK(lt_ext_layout(dispL)->curItem == aspect, "the arrow's click: the cursor on Aspect (%d)",
          lt_ext_layout(dispL)->curItem);
    CHECK(strcmp(before, after) != 0, "the right arrow steps Aspect (%s)", after);
    pointAt(left);
    click();
    mouseFrame();
    frame(0);
    frame(0);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_ASPECT), before) == 0,
          "the left arrow steps it back (%s, want %s)", ui_settings_value_text(UI_OPT_ASPECT),
          before);
    press(0x2000);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_ASPECT), after) == 0,
          "the pad's Right gives what the right arrow gave (%s, want %s)",
          ui_settings_value_text(UI_OPT_ASPECT), after);
    press(0x8000);
    int opts[16], values[16];
    const int nd = ui_settings_page_rows(UI_PAGE_DISPLAY, labels, opts, values, 16);
    int value = -1;
    for (int i = 0; i < nd; i++) {
        if (labels[i] == aspect) {
            value = values[i];
        }
    }
    CHECK(value >= 0 && lt_ext_pointer_role(value) == LT_POINTER_STEP, "Aspect's value (%d)",
          value);
    pointAt(value);
    click();
    mouseFrame();
    frame(0);
    frame(0);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_ASPECT), after) == 0,
          "a click on the value steps as Right (%s, want %s)",
          ui_settings_value_text(UI_OPT_ASPECT), after);
    pointAt(left);
    click();
    mouseFrame();
    frame(0);
    frame(0);

    /* a click on empty space: nothing */
    snprintf(before, sizeof(before), "%s", ui_settings_value_text(UI_OPT_ASPECT));
    const int cur = lt_ext_layout(dispL)->curItem;
    ico_pointer_move(4.0f / 640.0f, 0.5f);
    click();
    pad[0].flags = 0;
    ui_mouse_tick();
    CHECK(pad[0].flags == 0, "empty space: no button (0x%x)", (unsigned)pad[0].flags);
    frame(pad[0].flags);
    frame(0);
    CHECK(current_layout_id == dispL && lt_ext_layout(dispL)->curItem == cur &&
              strcmp(ui_settings_value_text(UI_OPT_ASPECT), before) == 0,
          "empty space: nothing changed (%d, %d)", current_layout_id,
          lt_ext_layout(dispL)->curItem);

    /* a key or pad press hides the pointer, a move shows it again */
    mouseFrame();
    mouseFrame();
    CHECK(ui_mouse_menu_active(), "shown before the press");
    mouseFrameWith(0x4000);
    CHECK(!ui_mouse_menu_active() && ico_pointer_menu(), "a pad press hides the pointer");
    mouseFrame();
    CHECK(!ui_mouse_menu_active(), "still hidden");
    ico_pointer_move(0.5f, 0.5f);
    mouseFrame();
    CHECK(ui_mouse_menu_active(), "a move shows it");

    /* the wheel on Achievements: Down, a notch a move */
    press(0x10);
    CHECK(settle(mainL, 60), "Display: back");
    openPage(mainL, iAch, UI_PAGE_ACHIEVEMENTS);
    mouseFrame();
    mouseFrame();
    ico_pointer_wheel(-1.0f);
    pad[0].flags = 0;
    ui_mouse_tick();
    CHECK(pad[0].flags == 0x4000, "the wheel down: Down (0x%x)", (unsigned)pad[0].flags);
    frame(pad[0].flags);
    ico_pointer_wheel(-1.0f);
    pad[0].flags = 0;
    ui_mouse_tick();
    CHECK(pad[0].flags == 0, "the next notch waits a tick (0x%x)", (unsigned)pad[0].flags);
    frame(pad[0].flags);
    pad[0].flags = 0;
    ui_mouse_tick();
    CHECK(pad[0].flags == 0x4000, "then Down (0x%x)", (unsigned)pad[0].flags);
    frame(pad[0].flags);
    frame(0); /* the two frames after a move clear the pad */
    frame(0);
    press(0x10);
    CHECK(settle(mainL, 60), "Achievements: back");

    /* the remap screen waiting for a press: a click is bound, not chosen */
    openPage(mainL, iCtl, UI_PAGE_CONTROLS);
    press(0x40); /* Remap controls */
    const int remapL = ui_settings_page_layout(UI_PAGE_REMAP);
    CHECK(settle(remapL, 60), "pointer: the remap screen");
    press(0x40); /* the capture */
    frame(0);
    CHECK(ui_settings_capturing(), "the capture waits");
    ui_settings_page_rows(UI_PAGE_REMAP, labels, NULL, NULL, 16);
    const int capCur = lt_ext_layout(remapL)->curItem;
    pointAt(labels[2]);
    click();
    pad[0].flags = 0;
    ui_mouse_tick();
    CHECK(pad[0].flags == 0 && lt_ext_layout(remapL)->curItem == capCur,
          "capturing: no Cross, no move (0x%x, %d)", (unsigned)pad[0].flags,
          lt_ext_layout(remapL)->curItem);
    frame(0);

    ui_settings_reset();
    ui_mouse_reset();
}

/* the memory card file select (layout 14) with files 1, 2 and 5 used: the
   proc records the item a Cross lands on */
static int s_slotCross;

static int slotProc(int first, int item)
{
    (void)first;
    (void)item;
    if (pad[0].flags & 0x40) {
        s_slotCross = texLayout[14].curItem;
    }
    return -1;
}

/* the Save screen's empty slots: the game masks an empty slot's item row
   62 + i and shows its picture row 52 + i in the same place.  On Save the
   mouse picks the picture as the item (the pad can reach and save in it); on
   Load it does not (the pad steps past it) */
static void testSaveSlotPointer(void)
{
    static const int slotX[5] = {210, 260, 310, 360, 410};

    ui_mouse_reset();
    ui_mouse_set_view(640, 480, 0, 0, 640, 480);
    useConfig("version = 1\n");
    fakeTables();
    lt_ext_reset();
    ui_settings_reset();
    memset(pad, 0, sizeof(pad));
    pad[0].ana[0] = pad[0].ana[1] = pad[0].ana[2] = pad[0].ana[3] = 128;
    init_layout_texture(2);
    settle(54, 4);
    for (int r = 52; r < 74; r++) {
        setRow(r, -1, -1, -1, -1, -1, -1, 0);
        texProperty[r].masked = texProperty[r].defaultMask = 1;
        texProperty[r].selectable = 0;
    }
    for (int i = 0; i < 10; i++) {
        const int used = i == 0 || i == 1 || i == 4;
        LtProperty *item = &texProperty[62 + i];
        LtProperty *pic = &texProperty[52 + i];
        setRow(62 + i, -1, -1, -1, -1, -1, -1, i < 5 ? 70 : 90);
        setRow(52 + i, -1, -1, -1, -1, -1, -1, i < 5 ? 70 : 90);
        item->dispX = pic->dispX = slotX[i % 5];
        item->dispW = pic->dispW = 40;
        item->dispH = pic->dispH = 30;
        item->leftItem = i % 5 > 0 ? 62 + i - 1 : -1;
        item->rightItem = i % 5 < 4 ? 62 + i + 1 : -1;
        item->upItem = i >= 5 ? 62 + i - 5 : -1;
        item->downItem = i < 5 ? 62 + i + 5 : -1;
        pic->selectable = 0;
        item->masked = item->defaultMask = used ? 0 : 1;
        pic->masked = pic->defaultMask = used ? 1 : 0;
    }
    setLayout(14, 52, 74, 62, -1);
    texLayout[14].fadeInTime = 0.0f;
    texLayout[14].proc = slotProc;
    lt_switch_layout(14);
    CHECK(settle(14, 60), "save slots: the screen (%d)", current_layout_id);
    mouseFrame();
    mouseFrame();

    /* Save: the empty slot 4's picture is item 65 */
    s_mcSaveSlots = 1;
    s_slotCross = -1;
    pointAt(55);
    mouseFrame();
    CHECK(texLayout[14].curItem == 65, "save: the cursor on the empty slot (%d)",
          texLayout[14].curItem);
    click();
    mouseFrame();
    CHECK(s_slotCross == 65, "save: a click is Cross on it (%d)", s_slotCross);
    s_slotCross = -1;
    pointAt(63);
    mouseFrame();
    CHECK(texLayout[14].curItem == 63, "save: a used slot still lands on its item (%d)",
          texLayout[14].curItem);

    /* Load: the empty slot gives nothing, the used one works */
    s_mcSaveSlots = 0;
    texLayout[14].curItem = 62;
    s_slotCross = -1;
    pointAt(55);
    click();
    mouseFrame();
    CHECK(texLayout[14].curItem == 62 && s_slotCross == -1,
          "load: the empty slot is no target (%d, %d)", texLayout[14].curItem, s_slotCross);
    pointAt(63);
    click();
    mouseFrame();
    CHECK(texLayout[14].curItem == 63 && s_slotCross == 63, "load: the used slot works (%d, %d)",
          texLayout[14].curItem, s_slotCross);

    texLayout[14].proc = NULL;
    s_mcSaveSlots = 0;
    ui_settings_reset();
    ui_mouse_reset();
}

/* Achievements > Achievement pop-ups: the list's first item, not on
   Gameplay; the page opens on it, Up wraps to Back and Down from Back
   lands on it again, On by default, its value clicks as a step; Right
   flips the pop-ups live,
   leaving the page writes [game] achievements only after a change, and Left
   reads On again */
static void testAchievementPopups(void)
{
    int labels[16], values[16];
    char p[1100];
    IcoToml *t;

    useConfig("version = 1\n");
    ico_ach_set_popups(1);
    const int mainL = enterMain(0);
    CHECK(ui_settings_row_of(UI_PAGE_GAMEPLAY, UI_OPT_ACH_POPUPS) < 0, "pop-ups: no Gameplay row");
    const int iAch = i17bMainIndex(UI_STR_SECTION_ACHIEVEMENTS);
    const int n = ico_ach_count();
    CHECK(iAch >= 0 && n + 2 > UI_LIST_SLOTS, "pop-ups: the list scrolls (%d achievements)", n);
    const int achL = openPage(mainL, iAch, UI_PAGE_ACHIEVEMENTS);
    const int c = ui_settings_page_rows(UI_PAGE_ACHIEVEMENTS, labels, NULL, values, 16);
    CHECK(c == UI_LIST_SLOTS && lt_ext_layout(achL)->curItem == labels[0],
          "pop-ups: the page opens on its first slot (%d slots)", c);
    CHECK(strcmp(lt_ext_row_text(labels[0]), ui_str(UI_STR_OPT_ACH_POPUPS)) == 0,
          "pop-ups: the first slot is the switch (\"%s\")", lt_ext_row_text(labels[0]));
    press(LT_PAD_UP);
    CHECK(lt_ext_layout(achL)->curItem == labels[UI_LIST_SLOTS - 1] &&
              strcmp(lt_ext_row_text(labels[UI_LIST_SLOTS - 1]), ui_str(UI_STR_BACK)) == 0,
          "pop-ups: Up wraps to Back (\"%s\")", lt_ext_row_text(labels[UI_LIST_SLOTS - 1]));
    press(LT_PAD_DOWN);
    const int s = 0;
    CHECK(lt_ext_layout(achL)->curItem == labels[s] &&
              strcmp(lt_ext_row_text(labels[s]), ui_str(UI_STR_OPT_ACH_POPUPS)) == 0 &&
              strcmp(lt_ext_row_text(values[s]), "On") == 0,
          "pop-ups: Down from Back, the switch first (\"%s\" \"%s\")", lt_ext_row_text(labels[s]),
          lt_ext_row_text(values[s]));
    CHECK(lt_ext_pointer_role(values[s]) == LT_POINTER_STEP &&
              lt_ext_pointer_role(values[1]) == LT_POINTER_AUTO,
          "pop-ups: its value steps on a click, an achievement's does not");
    CHECK(strcmp(ui_settings_value_text(UI_OPT_ACH_POPUPS), "On") == 0, "pop-ups: On by default");
    CHECK(ui_settings_save() == 0, "pop-ups: save without a change");
    path(p, sizeof(p), "settings_test.toml");
    t = ico_toml_load(p);
    if (t) {
        CHECK(!ico_toml_has(t, "game.achievements"), "pop-ups: no key written unchanged");
        ico_toml_free(t);
    }
    press(LT_PAD_RIGHT);
    CHECK(lt_ext_layout(achL)->curItem == labels[s] &&
              strcmp(lt_ext_row_text(values[s]), "Off") == 0,
          "pop-ups: Right shows Off on the row (\"%s\")", lt_ext_row_text(values[s]));
    CHECK(strcmp(ui_settings_value_text(UI_OPT_ACH_POPUPS), "Off") == 0, "pop-ups: Off");
    CHECK(ico_ach_popups_enabled() == 0, "pop-ups: the getter reads Off at once");
    press(LT_PAD_TRIANGLE);
    int mainLabels[20];
    ui_settings_page_rows(UI_PAGE_MAIN, mainLabels, NULL, NULL, 20);
    CHECK(settle(mainL, 60) && lt_ext_layout(mainL)->curItem == mainLabels[iAch],
          "pop-ups: Triangle back to the menu, on Achievements");
    t = ico_toml_load(p);
    CHECK(t != NULL, "pop-ups: config");
    if (t) {
        CHECK(ico_toml_has(t, "game.achievements") &&
                  ico_toml_get_bool(t, "game.achievements", 1) == 0,
              "pop-ups: [game] achievements false");
        ico_toml_free(t);
    }
    openPage(mainL, iAch, UI_PAGE_ACHIEVEMENTS);
    CHECK(lt_ext_layout(achL)->curItem == labels[0],
          "pop-ups: the page opens on its first slot again");
    press(LT_PAD_LEFT);
    CHECK(strcmp(lt_ext_row_text(values[s]), "On") == 0 &&
              strcmp(ui_settings_value_text(UI_OPT_ACH_POPUPS), "On") == 0,
          "pop-ups: Left reads On again (\"%s\")", lt_ext_row_text(values[s]));
    CHECK(ico_ach_popups_enabled() == 1, "pop-ups: the getter reads On again");
    press(LT_PAD_TRIANGLE);
    CHECK(settle(mainL, 60), "pop-ups: back to the menu again");
    t = ico_toml_load(p);
    if (t) {
        CHECK(ico_toml_get_bool(t, "game.achievements", 0) == 1,
              "pop-ups: [game] achievements true");
        ico_toml_free(t);
    }
    ui_settings_reset();
    useConfig("version = 1\n");
    ico_ach_set_popups(1);
}

/* a touch screen for the fullest (14-row) Controls page */
static int rowsFakeTouch(void)
{
    return 1;
}

/* the value of the row of `opt` on `page`: its text */
static const char *rowsValueOf(UiSettingsPage page, UiSettingsOpt opt)
{
    int labels[16], opts[16], values[16];
    const int n = ui_settings_page_rows(page, labels, opts, values, 16);
    for (int i = 0; i < n; i++) {
        if (opts[i] == (int)opt) {
            return lt_ext_row_text(values[i]);
        }
    }
    return "";
}

/* Analogue stick fix is on Controls (after Hold type) and on Gameplay,
   Achievement pop-ups on Extras and first on the Achievements list: each a
   second row of the one option, so the value, the config key and the save
   are the same from either page */
static void testDuplicatedRows(void)
{
    static const UiLang kLangs[5] = {UI_LANG_EN, UI_LANG_DE, UI_LANG_ES, UI_LANG_FR, UI_LANG_IT};
    int labels[20], opts[20];
    char p[1100];
    IcoToml *t;

    gFlagGameClear = 0;
    for (int title = 1; title >= 0; title--) {
        stage_no = title ? 1 : 11;
        useConfig("version = 1\n");
        ico_opt_set_stick_fix(0);
        ico_ach_set_popups(1);
        const int mainL = enterMain(title);

        /* Controls: after Hold type (shown on the title too) */
        const int ctlL = openPage(mainL, 4, UI_PAGE_CONTROLS);
        const int sf = ui_settings_row_of(UI_PAGE_CONTROLS, UI_OPT_STICK_FIX);
        CHECK(sf >= 0 && an22bShown(UI_PAGE_CONTROLS, UI_OPT_STICK_FIX),
              "title %d: stick fix is on Controls", title);
        const int n = ui_settings_page_rows(UI_PAGE_CONTROLS, labels, opts, NULL, 16);
        int at = -1;
        for (int i = 0; i < n; i++) {
            at = opts[i] == UI_OPT_STICK_FIX ? i : at;
        }
        CHECK(at > 0 && opts[at - 1] == UI_OPT_HOLD_TYPE, "title %d: stick fix follows Hold type",
              title);
        CHECK(noteStarting(UI_PAGE_CONTROLS, "Also under Gameplay") >= 0,
              "title %d: the Controls row says it is also under Gameplay", title);
        lt_ext_layout(ctlL)->curItem = sf;
        press(LT_PAD_RIGHT);
        CHECK(ico_opt_stick_fix() == 1 &&
                  strcmp(rowsValueOf(UI_PAGE_CONTROLS, UI_OPT_STICK_FIX), "On") == 0,
              "title %d: Right on Controls turns stick fix on", title);
        press(LT_PAD_TRIANGLE);
        CHECK(settle(mainL, 60), "title %d: Controls: back", title);
        path(p, sizeof(p), "settings_test.toml");
        t = ico_toml_load(p);
        CHECK(t != NULL && ico_toml_get_bool(t, "gameplay.stick_fix", 0) == 1,
              "title %d: leaving Controls saved gameplay.stick_fix", title);
        if (t) {
            ico_toml_free(t);
        }

        /* Gameplay: the same value, stepped from there */
        const int gameL = openPage(mainL, 5, UI_PAGE_GAMEPLAY);
        CHECK(an22bShown(UI_PAGE_GAMEPLAY, UI_OPT_STICK_FIX) &&
                  strcmp(rowsValueOf(UI_PAGE_GAMEPLAY, UI_OPT_STICK_FIX), "On") == 0,
              "title %d: Gameplay shows the value set on Controls", title);
        lt_ext_layout(gameL)->curItem = ui_settings_row_of(UI_PAGE_GAMEPLAY, UI_OPT_STICK_FIX);
        press(LT_PAD_LEFT);
        CHECK(ico_opt_stick_fix() == 0, "title %d: Left on Gameplay turns it off", title);
        press(LT_PAD_TRIANGLE);
        CHECK(settle(mainL, 60), "title %d: Gameplay: back", title);
        openPage(mainL, 4, UI_PAGE_CONTROLS);
        CHECK(strcmp(rowsValueOf(UI_PAGE_CONTROLS, UI_OPT_STICK_FIX), "Off") == 0,
              "title %d: Controls shows the value set on Gameplay", title);
        press(LT_PAD_TRIANGLE);
        CHECK(settle(mainL, 60), "title %d: Controls: back again", title);

        /* Extras: the pop-ups before Back, the same value as the list's */
        const int exL = openPage(mainL, 8, UI_PAGE_EXTRAS);
        const int en = ui_settings_page_rows(UI_PAGE_EXTRAS, labels, opts, NULL, 16);
        CHECK(en >= 2 && opts[en - 2] == UI_OPT_ACH_POPUPS && opts[en - 1] == UI_OPT_BACK,
              "title %d: Extras ends with the pop-ups and Back (%d rows)", title, en);
        CHECK(an22bShown(UI_PAGE_EXTRAS, UI_OPT_ACH_POPUPS), "title %d: pop-ups shown on Extras",
              title);
        CHECK(noteStarting(UI_PAGE_EXTRAS, "Also under Achievements") >= 0,
              "title %d: the Extras row says it is also under Achievements", title);
        lt_ext_layout(exL)->curItem = ui_settings_row_of(UI_PAGE_EXTRAS, UI_OPT_ACH_POPUPS);
        press(LT_PAD_RIGHT);
        CHECK(ico_ach_popups_enabled() == 0 &&
                  strcmp(rowsValueOf(UI_PAGE_EXTRAS, UI_OPT_ACH_POPUPS), "Off") == 0,
              "title %d: Right on Extras turns the pop-ups off", title);
        press(LT_PAD_TRIANGLE);
        CHECK(settle(mainL, 60), "title %d: Extras: back", title);
        t = ico_toml_load(p);
        CHECK(t != NULL && ico_toml_has(t, "game.achievements") &&
                  ico_toml_get_bool(t, "game.achievements", 1) == 0,
              "title %d: leaving Extras saved game.achievements", title);
        if (t) {
            ico_toml_free(t);
        }
        const int achL =
            openPage(mainL, i17bMainIndex(UI_STR_SECTION_ACHIEVEMENTS), UI_PAGE_ACHIEVEMENTS);
        int al[16], av[16];
        ui_settings_page_rows(UI_PAGE_ACHIEVEMENTS, al, NULL, av, 16);
        CHECK(lt_ext_layout(achL)->curItem == al[0] &&
                  strcmp(lt_ext_row_text(al[0]), ui_str(UI_STR_OPT_ACH_POPUPS)) == 0 &&
                  strcmp(lt_ext_row_text(av[0]), "Off") == 0,
              "title %d: the Achievements list still starts with the pop-ups, Off", title);
        press(LT_PAD_TRIANGLE);
        CHECK(settle(mainL, 60), "title %d: Achievements: back", title);
        CHECK(ui_settings_row_of(UI_PAGE_GAMEPLAY, UI_OPT_ACH_POPUPS) < 0,
              "title %d: pop-ups are not on Gameplay", title);
    }

    /* the first match, where an option stands on a page more than once */
    {
        const int n = ui_settings_page_rows(UI_PAGE_MAIN, labels, opts, NULL, 20);
        int first = -1;
        for (int i = 0; i < n && first < 0; i++) {
            first = opts[i] == UI_OPT_LINK ? labels[i] : first;
        }
        CHECK(first >= 0 && ui_settings_row_of(UI_PAGE_MAIN, UI_OPT_LINK) == first,
              "row_of returns the first of several rows of one option");
    }

    /* the fullest Controls page (pause menu, mouse and touch): fourteen rows
       fit in every language */
    useConfig("version = 1\n");
    ico_video_set_android(0);
    ui_settings_set_touch_query(rowsFakeTouch);
    stage_no = 11;
    for (int i = 0; i < 5; i++) {
        ui_set_language(kLangs[i]);
        const int mainL = enterMain(0);
        openPage(mainL, 4, UI_PAGE_CONTROLS);
        for (int k = 0; k < 4; k++) {
            frame(0);
        }
        int shown = 0;
        const int n = ui_settings_page_rows(UI_PAGE_CONTROLS, labels, opts, NULL, 16);
        for (int j = 0; j < n; j++) {
            shown += !lt_ext_prop(labels[j])->defaultMask;
        }
        CHECK(shown == 14, "language %d: Controls shows fourteen rows (%d)", i, shown);
        CHECK(strcmp(lt_ext_row_text(ui_settings_row_of(UI_PAGE_CONTROLS, UI_OPT_STICK_FIX)),
                     ui_str(UI_STR_OPT_STICK_FIX)) == 0,
              "language %d: the stick fix label", i);
        checkPageFits(UI_PAGE_CONTROLS, "Controls, fourteen rows");
        press(0x10);
        CHECK(settle(mainL, 60), "language %d: Controls: back", i);
    }
    ui_set_language(UI_LANG_EN);
    ui_settings_set_touch_query(NULL);
    ui_settings_reset();
    useConfig("version = 1\n");
    ico_opt_set_stick_fix(0);
    ico_ach_set_popups(1);
}

/* Remap controls: giving a source to a held target swaps the two rows
   (through the capture flow), device by device. Square clears a row outright,
   and a cleared row takes a source back with nothing to swap. */
static void testRemapSwap(void)
{
    IcoBindings *b = ico_input_live_bindings();
    UiRemapCapture c;

    useConfig("version = 1\n");
    ico_input_reload_bindings(b);
    ui_remap_capture_start(&c, ICO_T_TRIANGLE);
    ico_input_note_press(ICO_SRC_PAD, ICO_GP_SOUTH);
    CHECK(ui_remap_capture_step(&c, b) == UI_CAPTURE_BOUND, "swap: bound");
    CHECK(b->gp[ICO_T_TRIANGLE][0] == ICO_GP_SOUTH && b->gp[ICO_T_CROSS][0] == ICO_GP_NORTH,
          "swap: south on Triangle, Cross has north (%d, %d)", b->gp[ICO_T_TRIANGLE][0],
          b->gp[ICO_T_CROSS][0]);
    CHECK(b->kb[ICO_T_CROSS][0] == ICO_KEY_SPACE, "swap: the keyboard rows are kept");
    /* a keyboard press swaps the keyboard rows only */
    ui_remap_capture_start(&c, ICO_T_CIRCLE);
    ico_input_note_press(ICO_SRC_KEY, ICO_KEY_SPACE);
    CHECK(ui_remap_capture_step(&c, b) == UI_CAPTURE_BOUND, "swap: key bound");
    CHECK(b->kb[ICO_T_CIRCLE][0] == ICO_KEY_SPACE && b->kb[ICO_T_CROSS][0] == ICO_KEY_E,
          "swap: Space on Circle, Cross has E");
    CHECK(b->gp[ICO_T_TRIANGLE][0] == ICO_GP_SOUTH, "swap: the pad rows are kept");
    /* Square clears a row; a source nobody holds then fills it with no swap */
    ico_bindings_clear(b, ICO_T_SQUARE);
    CHECK(b->gp[ICO_T_SQUARE][0] == 0, "cleared");
    ui_remap_capture_start(&c, ICO_T_SQUARE);
    ico_input_note_press(ICO_SRC_PAD, ICO_GP_WEST);
    CHECK(ui_remap_capture_step(&c, b) == UI_CAPTURE_BOUND && b->gp[ICO_T_SQUARE][0] == ICO_GP_WEST,
          "a cleared row takes its source back");
    ico_input_reload_bindings(b);
}

/* The Remap controls page says the menus keep the gamepad by position: one
   row on the page, a text in every language */
static void testRemapMenuNote(void)
{
    static const UiLang kLangs[5] = {UI_LANG_EN, UI_LANG_DE, UI_LANG_ES, UI_LANG_FR, UI_LANG_IT};
    const char *seen[5];
    int found = 0;

    useConfig("version = 1\n");
    enterMain(0);
    for (int i = 0; i < 5; i++) {
        ui_set_language(kLangs[i]);
        seen[i] = ui_str(UI_STR_REMAP_MENU_NOTE);
        CHECK(seen[i] != NULL && seen[i][0] != '\0', "menu note in language %d", i);
        for (int j = 0; j < i; j++) {
            CHECK(strcmp(seen[i], seen[j]) != 0, "menu note differs between languages %d, %d", i,
                  j);
        }
    }
    ui_set_language(UI_LANG_EN);
    CHECK(strcmp(ui_str(UI_STR_REMAP_MENU_NOTE),
                 "In the menus the gamepad's face buttons and d-pad always go by position.") == 0,
          "menu note text");
    for (int i = LT_GAME_PROPERTY_COUNT; i < LT_GAME_PROPERTY_COUNT + lt_ext_prop_count(); i++) {
        const char *t = lt_ext_row_text(i);

        if (t != NULL && strcmp(t, ui_str(UI_STR_REMAP_MENU_NOTE)) == 0) {
            found++;
        }
    }
    CHECK(found == 1, "the Remap page has the menu note row (%d)", found);
    CHECK(strstr(ui_str(UI_STR_BUTTON_CONFIG_NOTE), "on top of Remap controls") != NULL,
          "the Button configuration note says it applies on top");
}

/* Escape outside play presses Triangle, never Start (mouse_look.h
   ico_escape_target): on the New Game screen, reached from the title,
   Start confirms and starts the game while Triangle goes back to the
   vibration screen without starting one. */
static void testEscapeNewGame(void)
{
    useConfig("version = 1\n");
    fakeTables();
    lt_ext_reset();
    ui_settings_reset();
    memset(pad, 0, sizeof(pad));
    pad[0].ana[0] = pad[0].ana[1] = pad[0].ana[2] = pad[0].ana[3] = 128;
    NonLinearCameraMove = 2;
    gFlagGameClear = 0;
    init_layout_texture(2);
    settle(54, 4);
    const int games = s_newGames;
    int ml = ui_new_game_screen_enter();
    lt_switch_layout(ml);
    CHECK(settle(ml, 60), "the New Game screen (%d)", current_layout_id);
    press(0x10); /* Triangle: Escape's button off the play layouts */
    CHECK(settle(9, 60), "Triangle: the vibration screen (%d)", current_layout_id);
    CHECK(s_newGames == games, "Triangle: no game started");
    lt_switch_layout(54);
    settle(54, 60);
    ml = ui_new_game_screen_enter();
    lt_switch_layout(ml);
    CHECK(settle(ml, 60), "the New Game screen again (%d)", current_layout_id);
    press(0x800); /* Start confirms here, so Escape must not be Start */
    CHECK(s_newGames == games + 1, "Start: the game starts");
}

/* Display > Resolution cycles Window, 1x, 2x, 3x, 4x, 6x, 8x, 12x, 16x,
   Auto; a scale from the file that is not on the list steps to its neighbours.
   Aspect goes 32:9, 48:9, Auto */
static void testResolutionCycle(void)
{
    static const int want[] = {1, 2, 3, 4, 6, 8, 12, 16};
    IcoVideoOptions o;

    useConfig("version = 1\n[video]\npreset = \"custom\"\nresolution = \"5x\"\n"
              "aspect = \"32:9\"\n");
    enterMainKeep(0);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_RESOLUTION), "5x") == 0,
          "5x from the file reads 5x");
    /* 5x is not in the list: Right goes to the next entry above it, 6x,
       then on through the list */
    for (int i = 4; i < 8; i++) {
        ui_settings_step(UI_OPT_RESOLUTION, 1);
        ico_video_get(&o);
        CHECK(o.resScale == want[i], "Right: step %d is %dx (%d)", i, want[i], o.resScale);
    }
    CHECK(strcmp(ui_settings_value_text(UI_OPT_RESOLUTION), "16x") == 0, "16x reads 16x");
    ui_settings_step(UI_OPT_RESOLUTION, 1);
    ico_video_get(&o);
    CHECK(o.resScale == ICO_RES_AUTO, "Right from 16x is Auto (%d)", o.resScale);
    ui_settings_step(UI_OPT_RESOLUTION, 1);
    ico_video_get(&o);
    CHECK(o.resScale == 0, "Right from Auto wraps to Window (%d)", o.resScale);

    useConfig("version = 1\n[video]\npreset = \"custom\"\nresolution = \"5x\"\n"
              "aspect = \"32:9\"\n");
    enterMainKeep(0);
    ui_settings_step(UI_OPT_RESOLUTION, -1);
    ico_video_get(&o);
    CHECK(o.resScale == 4, "Left from 5x is the entry below it, 4x (%d)", o.resScale);
    /* Window, then the whole list from 1x */
    ui_settings_step(UI_OPT_RESOLUTION, 1);
    ui_settings_step(UI_OPT_RESOLUTION, 1);
    ui_settings_step(UI_OPT_RESOLUTION, 1);
    ui_settings_step(UI_OPT_RESOLUTION, 1);
    ui_settings_step(UI_OPT_RESOLUTION, 1);
    ui_settings_step(UI_OPT_RESOLUTION, 1);
    ico_video_get(&o);
    CHECK(o.resScale == 0 && o.resW == 0, "Right from 16x, Auto, then Window (%d)", o.resScale);
    for (int i = 0; i < 8; i++) {
        ui_settings_step(UI_OPT_RESOLUTION, 1);
        ico_video_get(&o);
        CHECK(o.resScale == want[i], "Right from Window: step %d is %dx (%d)", i, want[i],
              o.resScale);
    }

    CHECK(strcmp(ui_settings_value_text(UI_OPT_ASPECT), "32:9") == 0, "aspect 32:9 from the file");
    ui_settings_step(UI_OPT_ASPECT, 1);
    ico_video_get(&o);
    CHECK(o.aspect == ICO_ASPECT_48_9 && strcmp(ui_settings_value_text(UI_OPT_ASPECT), "48:9") == 0,
          "Right from 32:9 is 48:9");
    ui_settings_step(UI_OPT_ASPECT, 1);
    ico_video_get(&o);
    CHECK(o.aspect == ICO_ASPECT_AUTO, "Right from 48:9 is Auto");
    ui_settings_step(UI_OPT_ASPECT, -1);
    ico_video_get(&o);
    CHECK(o.aspect == ICO_ASPECT_48_9, "Left from Auto is 48:9");
}

/* Display > Anti-aliasing ([video] anti_aliasing): a row on Display (the
   effect rows are on Effects), Off by default, stepping
   only through the values the renderer's mask lists, saved as its name,
   greyed and Off under the Original preset, with its note; both pages fit
   with and without the game finished */
static unsigned s_aaMask;

static unsigned fakeAaMask(void)
{
    return s_aaMask;
}

static int s_aaBlocked;

static int fakeAaBlocked(void)
{
    return s_aaBlocked;
}

static int rowDimmed(UiSettingsPage page, UiSettingsOpt opt)
{
    const int row = ui_settings_row_of(page, opt);
    return row >= 0 && lt_ext_row_dim(row);
}

static void testAntiAlias(void)
{
    IcoVideoOptions o;
    char p[1100];

    ui_settings_set_aa_mask_query(fakeAaMask);
    ui_settings_set_aa_blocked_query(fakeAaBlocked);
    s_aaBlocked = 0;
    s_aaMask =
        (1u << ICO_AA_OFF) | (1u << ICO_AA_SSAA2) | (1u << ICO_AA_SSAA4) | (1u << ICO_AA_MSAA4);

    /* the pages */
    for (int title = 1; title >= 0; title--) {
        useConfig("version = 1\n[video]\npreset = \"enhanced\"\n");
        stage_no = title ? 1 : 11;
        const int mainL = enterMainKeep(title);
        openPage(mainL, 0, UI_PAGE_DISPLAY);
        const int row = ui_settings_row_of(UI_PAGE_DISPLAY, UI_OPT_ANTI_ALIAS);
        CHECK(row >= 0 && !lt_ext_prop(row)->defaultMask && !lt_ext_prop(row)->masked,
              "title %d: the Anti-aliasing row is on Display", title);
        CHECK(row >= 0 && strcmp(lt_ext_row_text(row), "Anti-aliasing") == 0,
              "title %d: its label \"%s\"", title, row >= 0 ? lt_ext_row_text(row) : "");
        {
            int l[16], op[16];
            const int n = ui_settings_page_rows(UI_PAGE_DISPLAY, l, op, NULL, 16);
            CHECK(n > 4 && op[2] == UI_OPT_ASPECT && op[3] == UI_OPT_ANTI_ALIAS &&
                      op[4] == UI_OPT_WINDOW_MODE,
                  "title %d: Anti-aliasing sits between Aspect ratio and Window mode", title);
        }
        CHECK(ui_settings_row_of(UI_PAGE_EFFECTS, UI_OPT_ANTI_ALIAS) < 0,
              "title %d: not on Effects", title);
        CHECK(ui_settings_row_of(UI_PAGE_DISPLAY, UI_OPT_FILTER) < 0 &&
                  ui_settings_row_of(UI_PAGE_DISPLAY, UI_OPT_FULL_HEIGHT) < 0 &&
                  ui_settings_row_of(UI_PAGE_DISPLAY, UI_OPT_BRIGHTNESS) < 0,
              "title %d: Texture filtering, Full-height picture and Brightness left Display",
              title);
        CHECK(ui_settings_row_of(UI_PAGE_EFFECTS, UI_OPT_FILTER) >= 0 &&
                  ui_settings_row_of(UI_PAGE_EFFECTS, UI_OPT_FULL_HEIGHT) >= 0 &&
                  ui_settings_row_of(UI_PAGE_EFFECTS, UI_OPT_BRIGHTNESS) >= 0,
              "title %d: they are on Effects", title);
        for (int cleared = 0; cleared < 2; cleared++) {
            gFlagGameClear = cleared;
            frame(0);
            checkPageFits(UI_PAGE_DISPLAY, cleared ? "Display (game finished)" : "Display");
            press(0x10);
            CHECK(settle(mainL, 60), "Display: back");
            openPage(mainL, 1, UI_PAGE_EFFECTS);
            checkPageFits(UI_PAGE_EFFECTS, cleared ? "Effects (game finished)" : "Effects");
            press(0x10);
            CHECK(settle(mainL, 60), "Effects: back");
            openPage(mainL, 0, UI_PAGE_DISPLAY);
        }
        gFlagGameClear = 0;
        press(0x10);
        CHECK(settle(mainL, 60), "Display: back");
    }

    /* the note, on the cursor */
    {
        useConfig("version = 1\n[video]\npreset = \"enhanced\"\n");
        const int mainL = enterMainKeep(1);
        const int dispL = openPage(mainL, 0, UI_PAGE_DISPLAY);
        const int note = noteStarting(UI_PAGE_DISPLAY, "Smooths jagged edges.");
        CHECK(note >= 0 && strstr(lt_ext_row_text(note),
                                  "Supersampling draws the picture larger and shrinks it") != NULL,
              "the note text");
        lt_ext_layout(dispL)->curItem = ui_settings_row_of(UI_PAGE_DISPLAY, UI_OPT_ANTI_ALIAS);
        frame(0);
        CHECK(note >= 0 && !lt_ext_prop(note)->masked, "the note shows on the cursor");
        lt_ext_layout(dispL)->curItem = ui_settings_row_of(UI_PAGE_DISPLAY, UI_OPT_PRESET);
        frame(0);
        CHECK(note >= 0 && lt_ext_prop(note)->masked == 1, "the note hides elsewhere");
        press(0x10);
    }

    /* Enhanced: Off by default; Right steps through the mask only */
    useConfig("version = 1\n[video]\npreset = \"enhanced\"\n");
    enterMainKeep(1);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_ANTI_ALIAS), "Off") == 0, "default: Off (%s)",
          ui_settings_value_text(UI_OPT_ANTI_ALIAS));
    CHECK(!rowDimmed(UI_PAGE_DISPLAY, UI_OPT_ANTI_ALIAS), "Enhanced: the row is not greyed");

    static const struct {
        int aa;
        const char *text;
        const char *name;
    } kUp[] = {{ICO_AA_MSAA4, "MSAA 4x", "msaa4"},
               {ICO_AA_SSAA2, "2x supersampling", "ssaa2"},
               {ICO_AA_SSAA4, "4x supersampling", "ssaa4"},
               {ICO_AA_OFF, "Off", "off"}};

    for (unsigned i = 0; i < sizeof(kUp) / sizeof(kUp[0]); i++) {
        ui_settings_step(UI_OPT_ANTI_ALIAS, 1);
        ico_video_get(&o);
        CHECK(o.antiAlias == kUp[i].aa && ico_video_anti_alias() == kUp[i].aa &&
                  strcmp(ui_settings_value_text(UI_OPT_ANTI_ALIAS), kUp[i].text) == 0,
              "Right, step %u: %s (%d, \"%s\")", i, kUp[i].name, o.antiAlias,
              ui_settings_value_text(UI_OPT_ANTI_ALIAS));
        CHECK(ui_settings_save() == 0, "save");
        path(p, sizeof(p), "settings_test.toml");
        IcoToml *t = ico_toml_load(p);
        const char *s = t ? ico_toml_get(t, "video.anti_aliasing") : NULL;
        CHECK(s != NULL && strcmp(s, kUp[i].name) == 0, "saved: video.anti_aliasing = \"%s\" (%s)",
              kUp[i].name, s ? s : "(none)");
        if (t != NULL) {
            ico_toml_free(t);
        }
    }
    /* the order is Off, MSAA 2x, 4x, 8x, 2x supersampling, 4x supersampling
       (not the numbers'); Left from Off wraps to the last listed value */
    ui_settings_step(UI_OPT_ANTI_ALIAS, -1);
    ico_video_get(&o);
    CHECK(o.antiAlias == ICO_AA_SSAA4, "Left from Off is 4x supersampling (%d)", o.antiAlias);
    ui_settings_step(UI_OPT_ANTI_ALIAS, -1);
    ico_video_get(&o);
    CHECK(o.antiAlias == ICO_AA_SSAA2, "Left from 4x supersampling is 2x supersampling (%d)",
          o.antiAlias);
    /* a mask that lists everything: all six in order; MSAA 8x included */
    s_aaMask = (1u << ICO_AA_COUNT) - 1;
    static const int kDown[] = {ICO_AA_MSAA8, ICO_AA_MSAA4, ICO_AA_MSAA2, ICO_AA_OFF};
    for (unsigned i = 0; i < sizeof(kDown) / sizeof(kDown[0]); i++) {
        ui_settings_step(UI_OPT_ANTI_ALIAS, -1);
        ico_video_get(&o);
        CHECK(o.antiAlias == kDown[i], "full mask: Left gives %d (%d)", kDown[i], o.antiAlias);
        if (kDown[i] == ICO_AA_MSAA8) {
            CHECK(strcmp(ui_settings_value_text(UI_OPT_ANTI_ALIAS), "MSAA 8x") == 0,
                  "MSAA 8x reads so");
        }
    }
    ui_settings_step(UI_OPT_ANTI_ALIAS, 1);
    ui_settings_step(UI_OPT_ANTI_ALIAS, 1);
    ico_video_get(&o);
    CHECK(o.antiAlias == ICO_AA_MSAA4, "full mask: Right twice from Off is MSAA 4x (%d)",
          o.antiAlias);
    /* a mask with only the always-there values: the stored MSAA 4x is drawn
       without (rd__aa_mode), so the row shows it with the note, and steps
       from it, not from Off */
    s_aaMask = (1u << ICO_AA_OFF) | (1u << ICO_AA_SSAA2) | (1u << ICO_AA_SSAA4);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_ANTI_ALIAS), "MSAA 4x (off: not available)") == 0,
          "no MSAA listed: the stored 4x reads so with the note (%s)",
          ui_settings_value_text(UI_OPT_ANTI_ALIAS));
    ui_settings_step(UI_OPT_ANTI_ALIAS, 1);
    ico_video_get(&o);
    CHECK(o.antiAlias == ICO_AA_SSAA2,
          "no MSAA listed: Right from the stored MSAA 4x is 2x supersampling (%d)", o.antiAlias);
    ui_settings_step(UI_OPT_ANTI_ALIAS, -1);
    ico_video_get(&o);
    CHECK(o.antiAlias == ICO_AA_OFF, "no MSAA listed: Left from 2x supersampling is Off (%d)",
          o.antiAlias);
    ui_settings_step(UI_OPT_ANTI_ALIAS, -1);
    ico_video_get(&o);
    CHECK(o.antiAlias == ICO_AA_SSAA4, "no MSAA listed: Left from Off is 4x supersampling (%d)",
          o.antiAlias);

    /* the value from the file: shown when the device offers it; the value
       actually used (Off) when it does not, the file kept */
    useConfig("version = 1\n[video]\npreset = \"enhanced\"\nanti_aliasing = \"msaa2\"\n");
    enterMainKeep(1);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_ANTI_ALIAS), "MSAA 2x (off: not available)") == 0,
          "msaa2 from the file, not offered: reads so with the note (%s)",
          ui_settings_value_text(UI_OPT_ANTI_ALIAS));
    ico_video_get(&o);
    CHECK(o.antiAlias == ICO_AA_MSAA2, "msaa2 from the file, not offered: kept (%d)", o.antiAlias);
    s_aaMask |= 1u << ICO_AA_MSAA2;
    CHECK(strcmp(ui_settings_value_text(UI_OPT_ANTI_ALIAS), "MSAA 2x") == 0,
          "msaa2 from the file, offered: reads MSAA 2x (%s)",
          ui_settings_value_text(UI_OPT_ANTI_ALIAS));
    /* offered by the device but no depth path works with it (the renderer
       draws without): the stored value with the note, never plain */
    s_aaBlocked = 1;
    CHECK(strcmp(ui_settings_value_text(UI_OPT_ANTI_ALIAS), "MSAA 2x (off: not available)") == 0,
          "msaa2 blocked by the renderer: reads so with the note (%s)",
          ui_settings_value_text(UI_OPT_ANTI_ALIAS));
    ui_settings_step(UI_OPT_ANTI_ALIAS, 1);
    ico_video_get(&o);
    CHECK(o.antiAlias == ICO_AA_SSAA2, "msaa2 blocked: Right steps the stored value (%d)",
          o.antiAlias);
    s_aaBlocked = 0;

    /* Original: Off, greyed, no step.  Loading preset "original" applies the
       Original shortcut, which puts the stored option off too (options_test
       test_video_anti_alias), so the stored ssaa2 is set at run time under
       the Original rows: kept, off in force */
    useConfig("version = 1\n[video]\npreset = \"original\"\n");
    {
        const int mainL = enterMainKeep(1);
        ico_video_get(&o);
        o.antiAlias = ICO_AA_SSAA2;
        ico_video_set(&o);
        const int dispL = openPage(mainL, 0, UI_PAGE_DISPLAY);
        frame(0);
        CHECK(strcmp(ui_settings_value_text(UI_OPT_ANTI_ALIAS), "Off") == 0,
              "Original: reads Off (%s)", ui_settings_value_text(UI_OPT_ANTI_ALIAS));
        CHECK(rowDimmed(UI_PAGE_DISPLAY, UI_OPT_ANTI_ALIAS), "Original: the row is greyed");
        lt_ext_layout(dispL)->curItem = ui_settings_row_of(UI_PAGE_DISPLAY, UI_OPT_ANTI_ALIAS);
        press(0x2000);
        ico_video_get(&o);
        CHECK(o.antiAlias == ICO_AA_SSAA2 && ico_video_anti_alias() == ICO_AA_OFF,
              "Original: Right does not step (%d)", o.antiAlias);
        ui_settings_step(UI_OPT_ANTI_ALIAS, 1);
        ico_video_get(&o);
        CHECK(o.antiAlias == ICO_AA_SSAA2, "Original: the step call does not change it");
        /* the Preset to Enhanced frees it */
        ui_settings_step(UI_OPT_PRESET, 1);
        frame(0);
        CHECK(!rowDimmed(UI_PAGE_DISPLAY, UI_OPT_ANTI_ALIAS), "Enhanced preset: not greyed");
        press(0x10);
    }
    ui_settings_set_aa_mask_query(NULL);
    ui_settings_set_aa_blocked_query(NULL);
}

/* Gameplay > East Reflector door fix ([gameplay] door_fix): a row from the title and
   the pause menu, Off by default; a step turns it on and sets the key, the
   saved file has it, its note shows on the cursor, and the page with the
   game finished (every row shown) still fits */
static void testDoorFix(void)
{
    char p[1100];

    gFlagGameClear = 0;
    for (int title = 1; title >= 0; title--) {
        stage_no = title ? 1 : 11;
        const int mainL = enterMain(title);
        openPage(mainL, 5, UI_PAGE_GAMEPLAY);
        const int row = ui_settings_row_of(UI_PAGE_GAMEPLAY, UI_OPT_DOOR_FIX);
        CHECK(row >= 0 && !lt_ext_prop(row)->defaultMask && !lt_ext_prop(row)->masked,
              "title %d: the East Reflector door fix row", title);
        CHECK(row >= 0 && strcmp(lt_ext_row_text(row), "East Reflector door fix") == 0,
              "title %d: its label \"%s\"", title, row >= 0 ? lt_ext_row_text(row) : "");
        CHECK(ico_opt_door_fix() == 0 &&
                  strcmp(ui_settings_value_text(UI_OPT_DOOR_FIX), "Off") == 0,
              "title %d: Off by default", title);
        press(0x10);
        CHECK(settle(mainL, 60), "Gameplay: back");
    }

    /* the game finished, from the pause menu: all six rows and Back */
    gFlagGameClear = 1;
    stage_no = 11;
    const int mainL = enterMain(0);
    const int gameL = openPage(mainL, 5, UI_PAGE_GAMEPLAY);
    checkPageFits(UI_PAGE_GAMEPLAY, "Gameplay (cleared, with East Reflector door fix)");
    const int row = ui_settings_row_of(UI_PAGE_GAMEPLAY, UI_OPT_DOOR_FIX);
    lt_ext_layout(gameL)->curItem = row;
    press(0x2000);
    CHECK(ico_opt_door_fix() == 1 && strcmp(ui_settings_value_text(UI_OPT_DOOR_FIX), "On") == 0 &&
              ico_config_get_bool("gameplay.door_fix", 0) == 1,
          "Right: East Reflector door fix On and the key set");
    const int note = noteStarting(UI_PAGE_GAMEPLAY, "Holding hands");
    CHECK(note >= 0 && !lt_ext_prop(note)->masked,
          "the East Reflector door fix note on the cursor");
    CHECK(ui_settings_save() == 0, "save");
    path(p, sizeof(p), "settings_test.toml");
    IcoToml *t = ico_toml_load(p);
    CHECK(t != NULL && ico_toml_get_bool(t, "gameplay.door_fix", 0) == 1,
          "saved: gameplay.door_fix = true");
    if (t != NULL) {
        ico_toml_free(t);
    }
    press(0x8000);
    CHECK(ico_opt_door_fix() == 0 && ico_config_get_bool("gameplay.door_fix", 1) == 0,
          "Left: East Reflector door fix Off again");
    press(0x10);
    CHECK(settle(mainL, 60), "Gameplay: back");
    ico_opt_set_door_fix(0);
    gFlagGameClear = 0;
}

/* Controls > Hold type ([gameplay] hold_type): the row shows from the title
   and the pause menu; a step sets the key and leaving Controls writes it to
   config.toml; ui_settings_install (run from init_layout_texture at boot)
   sets the game's optionControlType from the key, whatever the card held */
static void testHoldType(void)
{
    char p[1100];

    gFlagGameClear = 0;
    for (int title = 1; title >= 0; title--) {
        stage_no = title ? 1 : 11;
        const int mainL = enterMain(title);
        const int ctlL = openPage(mainL, 4, UI_PAGE_CONTROLS);
        const int row = ui_settings_row_of(UI_PAGE_CONTROLS, UI_OPT_HOLD_TYPE);
        CHECK(row >= 0 && !lt_ext_prop(row)->defaultMask && !lt_ext_prop(row)->masked,
              "title %d: the Hold type row is shown", title);
        CHECK(ico_opt_hold_type() == 0 && optionControlType == 0 &&
                  strcmp(ui_settings_value_text(UI_OPT_HOLD_TYPE), "A (hold)") == 0,
              "title %d: A by default", title);
        lt_ext_layout(ctlL)->curItem = row;
        press(LT_PAD_RIGHT);
        CHECK(ico_opt_hold_type() == 1 && optionControlType == 1 &&
                  strcmp(ico_config_get_string("gameplay.hold_type", ""), "toggle") == 0,
              "title %d: Right: B, the key set, the game's variable follows", title);
        press(LT_PAD_TRIANGLE);
        CHECK(settle(mainL, 60), "title %d: Controls: back", title);
        path(p, sizeof(p), "settings_test.toml");
        IcoToml *t = ico_toml_load(p);
        const char *saved = t != NULL ? ico_toml_get(t, "gameplay.hold_type") : NULL;
        CHECK(saved != NULL && strcmp(saved, "toggle") == 0,
              "title %d: leaving Controls saved gameplay.hold_type = \"toggle\"", title);
        if (t != NULL) {
            ico_toml_free(t);
        }
        optionControlType = 0;
        ico_opt_set_hold_type(0);
    }

    /* at boot: the key wins over what the game's variable held */
    useConfig("version = 1\n[gameplay]\nhold_type = \"toggle\"\n");
    optionControlType = 0;
    enterMainKeep(1);
    CHECK(optionControlType == 1, "install: hold_type = toggle sets optionControlType (%d)",
          optionControlType);
    optionControlType = 0;
    ui_settings_install();
    CHECK(optionControlType == 1, "install again: the same");
    useConfig("version = 1\n");
    optionControlType = 1;
    ui_settings_install();
    CHECK(optionControlType == 0, "install: no key is hold (%d)", optionControlType);
    optionControlType = 0;
}

/* Gameplay > Natural chains ([gameplay] natural_chains): a row from the title
   and the pause menu after the East Reflector door fix, Off by default; a
   step turns it on and sets the key, the saved file has it, its note shows
   on the cursor, a step back turns it off, and the page with the game
   finished (every row shown) still fits */
static void testNaturalChains(void)
{
    char p[1100];

    gFlagGameClear = 0;
    for (int title = 1; title >= 0; title--) {
        stage_no = title ? 1 : 11;
        const int mainL = enterMain(title);
        openPage(mainL, 5, UI_PAGE_GAMEPLAY);
        const int row = ui_settings_row_of(UI_PAGE_GAMEPLAY, UI_OPT_NATURAL_CHAINS);
        CHECK(row >= 0 && !lt_ext_prop(row)->defaultMask && !lt_ext_prop(row)->masked,
              "title %d: the Natural chains row", title);
        CHECK(row >= 0 && strcmp(lt_ext_row_text(row), "Natural chains") == 0,
              "title %d: its label \"%s\"", title, row >= 0 ? lt_ext_row_text(row) : "");
        /* between the door fix (and its note row) and the Film effect */
        CHECK(row > ui_settings_row_of(UI_PAGE_GAMEPLAY, UI_OPT_DOOR_FIX) &&
                  row < ui_settings_row_of(UI_PAGE_GAMEPLAY, UI_OPT_FILM_EFFECT),
              "title %d: after the East Reflector door fix, before the Film effect", title);
        CHECK(ico_opt_natural_chains() == 0 &&
                  strcmp(ui_settings_value_text(UI_OPT_NATURAL_CHAINS), "Off") == 0,
              "title %d: Off by default", title);
        press(0x10);
        CHECK(settle(mainL, 60), "Gameplay: back");
    }

    gFlagGameClear = 1;
    stage_no = 11;
    const int mainL = enterMain(0);
    const int gameL = openPage(mainL, 5, UI_PAGE_GAMEPLAY);
    checkPageFits(UI_PAGE_GAMEPLAY, "Gameplay (cleared, with Natural chains)");
    const int row = ui_settings_row_of(UI_PAGE_GAMEPLAY, UI_OPT_NATURAL_CHAINS);
    lt_ext_layout(gameL)->curItem = row;
    press(0x2000);
    CHECK(ico_opt_natural_chains() == 1 &&
              strcmp(ui_settings_value_text(UI_OPT_NATURAL_CHAINS), "On") == 0 &&
              ico_config_get_bool("gameplay.natural_chains", 0) == 1,
          "Right: Natural chains On and the key set");
    const int note = noteStarting(UI_PAGE_GAMEPLAY, "A chain that comes down");
    CHECK(note >= 0 && !lt_ext_prop(note)->masked, "the Natural chains note on the cursor");
    CHECK(ui_settings_save() == 0, "save");
    path(p, sizeof(p), "settings_test.toml");
    IcoToml *t = ico_toml_load(p);
    CHECK(t != NULL && ico_toml_get_bool(t, "gameplay.natural_chains", 0) == 1,
          "saved: gameplay.natural_chains = true");
    if (t != NULL) {
        ico_toml_free(t);
    }
    press(0x8000);
    CHECK(ico_opt_natural_chains() == 0 && ico_config_get_bool("gameplay.natural_chains", 1) == 0,
          "Left: Natural chains Off again");
    press(0x10);
    CHECK(settle(mainL, 60), "Gameplay: back");
    ico_opt_set_natural_chains(0);
    gFlagGameClear = 0;
}

/* Gameplay > Skip the opening ([gameplay] skip_opening): a row from the title
   and the pause menu after Natural chains, Off by default; a step turns it
   on and sets the key, the saved file has it, its note shows on the cursor,
   a step back turns it off, and the page with the game finished (every row
   shown) still fits */
static void testSkipOpening(void)
{
    char p[1100];

    gFlagGameClear = 0;
    for (int title = 1; title >= 0; title--) {
        stage_no = title ? 1 : 11;
        const int mainL = enterMain(title);
        openPage(mainL, 5, UI_PAGE_GAMEPLAY);
        const int row = ui_settings_row_of(UI_PAGE_GAMEPLAY, UI_OPT_SKIP_OPENING);
        CHECK(row >= 0 && !lt_ext_prop(row)->defaultMask && !lt_ext_prop(row)->masked,
              "title %d: the Skip the opening row", title);
        CHECK(row >= 0 && strcmp(lt_ext_row_text(row), "Skip the opening") == 0,
              "title %d: its label \"%s\"", title, row >= 0 ? lt_ext_row_text(row) : "");
        /* between Natural chains (and its note row) and the Film effect */
        CHECK(row > ui_settings_row_of(UI_PAGE_GAMEPLAY, UI_OPT_NATURAL_CHAINS) &&
                  row < ui_settings_row_of(UI_PAGE_GAMEPLAY, UI_OPT_FILM_EFFECT),
              "title %d: after Natural chains, before the Film effect", title);
        CHECK(ico_opt_skip_opening() == 0 &&
                  strcmp(ui_settings_value_text(UI_OPT_SKIP_OPENING), "Off") == 0,
              "title %d: Off by default", title);
        press(0x10);
        CHECK(settle(mainL, 60), "Gameplay: back");
    }

    gFlagGameClear = 1;
    stage_no = 11;
    const int mainL = enterMain(0);
    const int gameL = openPage(mainL, 5, UI_PAGE_GAMEPLAY);
    checkPageFits(UI_PAGE_GAMEPLAY, "Gameplay (cleared, with Skip the opening)");
    const int row = ui_settings_row_of(UI_PAGE_GAMEPLAY, UI_OPT_SKIP_OPENING);
    lt_ext_layout(gameL)->curItem = row;
    press(0x2000);
    CHECK(ico_opt_skip_opening() == 1 &&
              strcmp(ui_settings_value_text(UI_OPT_SKIP_OPENING), "On") == 0 &&
              ico_config_get_bool("gameplay.skip_opening", 0) == 1,
          "Right: Skip the opening On and the key set");
    const int note = noteStarting(UI_PAGE_GAMEPLAY, "The game starts at the title screen");
    CHECK(note >= 0 && !lt_ext_prop(note)->masked, "the Skip the opening note on the cursor");
    CHECK(ui_settings_save() == 0, "save");
    path(p, sizeof(p), "settings_test.toml");
    IcoToml *t = ico_toml_load(p);
    CHECK(t != NULL && ico_toml_get_bool(t, "gameplay.skip_opening", 0) == 1,
          "saved: gameplay.skip_opening = true");
    if (t != NULL) {
        ico_toml_free(t);
    }
    press(0x8000);
    CHECK(ico_opt_skip_opening() == 0 && ico_config_get_bool("gameplay.skip_opening", 1) == 0,
          "Left: Skip the opening Off again");
    press(0x10);
    CHECK(settle(mainL, 60), "Gameplay: back");
    ico_opt_set_skip_opening(0);
    gFlagGameClear = 0;
}

/* Skip the opening at the boot (ico2/script/src/op.c actOpDemo01, GL-14) on
   the real layout_texture.c, with the disc's fade times (texLayout, the same
   on the three discs): the boot's layout (PAL 7, la_boot_memory_card_check:
   -1 until kanbanBootEnd, then the scene layout) fades in and out in no
   time, the scene layout (PAL 55, la_game_demo) in and out over 1 s, the
   title (PAL 12, la_title_continue_or_new) in over 1.5 s.  A Main tick runs
   the layouts (ExecIcoMisc's exec_layout_texture), then kanbanBootMain
   (whose step 7 ends the boot), then the scripts (iosOmMain).  Without a
   wait the script asks for the title two ticks after the end (its wait for
   scpFadeOut(255)), while the scene layout fades in, and lt_switch_layout
   drops the request: the title has no menu (the skip's first form).  The
   script waits on lt_host_boot_settled first; the switch is taken, the
   title comes up and its rows take the pad.  The Start skip's earliest
   press finds the same state.  Not modelled: actOpDemo01's thread itself
   (its ticks are written out here), the title's card check
   (la_title_continue_or_new shows Continue and New Game once it has
   decided) */
static int s_bootEnded, s_sceneFirst;

static int bootCheckProc(int first, int item)
{
    (void)first;
    (void)item;
    return s_bootEnded ? PAL_JPNKOR(0x37, 0x34) : -1; /* la_boot_memory_card_check */
}

static int sceneProc(int first, int item)
{
    (void)item;
    s_sceneFirst += first != 0;
    return -1; /* la_game_demo, no Start */
}

/* stage 1's layouts at the end of its load, then the ticks up to the boot's
   end: the boot's layout up and settled, the end set after the end tick's
   layouts */
static void bootToEnd(const char *slug)
{
    const int boot = PAL_JPNKOR(7, 4);
    const int scene = fxLayout(55);

    fakeTables();
    lt_ext_reset();
    ui_settings_reset();
    memset(pad, 0, sizeof(pad));
    pad[0].ana[0] = pad[0].ana[1] = pad[0].ana[2] = pad[0].ana[3] = 128;
    setLayout(boot, 0, 0, -1, -1);
    texLayout[boot].fadeInTime = texLayout[boot].fadeOutTime = 0.0f;
    texLayout[boot].colA = 1.0f;
    texLayout[boot].proc = bootCheckProc;
    setLayout(scene, fxRow(292), fxRow(292), -1, -1);
    texLayout[scene].fadeInTime = texLayout[scene].fadeOutTime = 1.0f;
    texLayout[scene].proc = sceneProc;
    texLayout[fxLayout(11)].fadeInTime = texLayout[fxLayout(11)].fadeOutTime = 0.5f;
    for (int l = 12; l <= 13; l++) {
        texLayout[fxLayout(l)].fadeInTime = 1.5f;
        texLayout[fxLayout(l)].fadeOutTime = 0.5f;
    }
    s_bootEnded = s_sceneFirst = 0;
    layout_boot_flag = 0;
    mpegPlayReturnStage = 0;
    init_layout_texture(1);
    CHECK(current_layout_id == boot, "%s: stage 1 starts on the boot's layout (%d)", slug,
          current_layout_id);
    for (int i = 0; i < 5; i++) {
        frame(0);
    }
    CHECK(current_layout_id == boot && lt_fade_status() == 2,
          "%s: the boot's layout up and settled (%d)", slug, lt_fade_status());
    s_bootEnded = 1; /* kanbanBootMain step 7, after the end tick's layouts */
}

static void testSkipOpeningTitle(const char *slug)
{
    CHECK(ico_region_select(slug) == 0, "the %s disc selected", slug);
    useConfig("version = 1\n");
    const int scene = fxLayout(55);
    const int title = fxLayout(12);

    CHECK(PAL_JPNKOR(0x37, 0x34) == scene, "%s: the boot's layout hands over to %d", slug, scene);
    /* 50 Hz and 60 Hz: 25 and 30 ticks a second */
    for (int hz60 = 0; hz60 < 2; hz60++) {
        systemStatus[0] = hz60 ? 0 : 1;
        const int second = (60 - systemStatus[0] * 10) / systemStatus[1];

        /* without the wait: the title asked for two ticks after the end */
        bootToEnd(slug);
        CHECK(lt_fade_status() == 2 && lt_host_boot_settled() == 0,
              "%s %d: the end tick: settled, but not handed over", slug, second);
        frame(0);
        CHECK(current_layout_id == scene && lt_fade_status() == 0,
              "%s %d: the tick after: the scene layout, fading in (%d, %d)", slug, second,
              current_layout_id, lt_fade_status());
        frame(0);
        CHECK(lt_host_boot_settled() == 0, "%s %d: two ticks after: still fading in", slug, second);
        lt_switch_layout(title);
        for (int i = 0; i < 4 * second; i++) {
            frame(0);
        }
        CHECK(current_layout_id == scene,
              "%s %d: that switch is dropped and the title never comes (%d)", slug, second,
              current_layout_id);

        /* the fix: the script waits a tick at a time on lt_host_boot_settled,
           then mode 1 asks for the title in the same tick */
        bootToEnd(slug);
        int waited = 0;
        while (lt_host_boot_settled() == 0 && waited < 10 * second) {
            frame(0);
            waited++;
        }
        CHECK(current_layout_id == scene && lt_fade_status() == 2 && waited == 1 + second,
              "%s %d: the wait ends on the scene layout settled, %d ticks after the end (want %d)",
              slug, second, waited, 1 + second);
        CHECK(s_sceneFirst == 1, "%s %d: the scene layout's proc had its first call (%d)", slug,
              second, s_sceneFirst);
        lt_switch_layout(title);
        CHECK(lt_fade_status() == 3, "%s %d: the title's switch is taken (%d)", slug, second,
              lt_fade_status());
        CHECK(settle(title, 4 * second), "%s %d: the title comes up (%d, %d)", slug, second,
              current_layout_id, lt_fade_status());
        CHECK(texLayout[title].curItem == fxRow(49), "%s %d: the cursor on Continue (%d)", slug,
              second, texLayout[title].curItem);
        press(0x4000);
        CHECK(texLayout[title].curItem == fxRow(50), "%s %d: Down: New Game (%d)", slug, second,
              texLayout[title].curItem);

        /* the Start skip of the opening: mode 0 starts two ticks after the
           end, waits a tick at least for its stream and 30 more, and reads
           Start in the tick after */
        bootToEnd(slug);
        for (int i = 0; i < 2 + 1 + 30 + 1; i++) {
            frame(0);
        }
        CHECK(lt_host_boot_settled() && current_layout_id == scene,
              "%s %d: the Start skip's earliest press finds the scene layout settled", slug,
              second);
        lt_switch_layout(title);
        CHECK(settle(title, 4 * second), "%s %d: Start: the title comes up (%d)", slug, second,
              current_layout_id);
    }
    systemStatus[0] = 1;
    ico_region_select("pal");
}

/* the rows of layout page whose text is str */
static int rowsWithText(UiSettingsPage page, const char *str)
{
    const LtProp *l = lt_ext_layout(ui_settings_page_layout(page));
    int n = 0;
    for (int j = l->first; j < l->last; j++) {
        n += strcmp(lt_ext_row_text(j), str) == 0;
    }
    return n;
}

/* The version in the Main page's top-right corner: the package
   label, small, greyed, right-aligned, above the header; on no other page;
   nothing in a development build */
static void testVersion(void)
{
    static const char kLabel[] = "v0.7.1-rc1";

    ui_settings_set_version(kLabel);
    enterMain(0);
    const int v = ui_settings_version_row();
    CHECK(v >= 0 && rowWithText(UI_PAGE_MAIN, kLabel) == v, "Main: the version row (%d)", v);
    CHECK(rowsWithText(UI_PAGE_MAIN, kLabel) == 1, "Main: one version row (%d)",
          rowsWithText(UI_PAGE_MAIN, kLabel));
    for (int p = 0; p < UI_PAGE_COUNT; p++) {
        if (p != UI_PAGE_MAIN) {
            CHECK(rowsWithText((UiSettingsPage)p, kLabel) == 0, "page %d: no version row", p);
        }
    }
    if (v >= 0) {
        const LtProperty *r = lt_ext_prop(v);
        /* the header: the page's first row */
        const int header = lt_ext_layout(ui_settings_page_layout(UI_PAGE_MAIN))->first;
        CHECK(!r->selectable && r->ownerItem < 0, "the version row is not selectable");
        CHECK(lt_ext_row_dim(v) == 1, "the version row is greyed");
        CHECK(lt_ext_row_align(v) == UI_ALIGN_RIGHT, "the version row is right-aligned (%d)",
              lt_ext_row_align(v));
        CHECK(lt_ext_row_size(v) == 17.0f, "the version row's size (%.1f)",
              (double)lt_ext_row_size(v));
        CHECK(r->dispX + r->dispW == 620, "the version row ends at x 620 (%d)",
              r->dispX + r->dispW);
        CHECK(r->dispY >= 2 && r->dispY + r->dispH / 2 <= 12, "above the header (y %d, h %d)",
              r->dispY, r->dispH);
        CHECK(header != v && r->dispY * 2 + r->dispH <= lt_ext_prop(header)->dispY * 2,
              "the version box ends where the header's starts (header y %d)",
              lt_ext_prop(header)->dispY);
        /* the page's own re-layout (a step, a refresh) leaves it in place */
        const int x = r->dispX, y = r->dispY;
        ui_settings_step(UI_OPT_LANGUAGE, 1);
        ui_settings_step(UI_OPT_LANGUAGE, -1);
        CHECK(r->dispX == x && r->dispY == y && lt_ext_row_dim(v) == 1,
              "the version row is not moved");
        CHECK(strcmp(lt_ext_row_text(v), kLabel) == 0, "the version row keeps its text");
    }

    /* a development build (no label, or ""): nothing */
    for (int k = 0; k < 2; k++) {
        ui_settings_set_version(k == 0 ? NULL : "");
        enterMain(0);
        CHECK(ui_settings_version_row() == -1, "no label %d: no version row", k);
        const LtProp *l = lt_ext_layout(ui_settings_page_layout(UI_PAGE_MAIN));
        for (int j = l->first; j < l->last; j++) {
            const char *t = lt_ext_row_text(j);
            CHECK(!(t[0] == 'v' && t[1] >= '0' && t[1] <= '9'), "no label %d: row %d reads %s", k,
                  j, t);
        }
    }
    ui_settings_set_version(NULL);
}

/* the Menu language row ([ui] language): Auto follows the game's language
   on the European disc, a language of its own does not; the game's words
   (the sheet language) follow the game whatever the menu language is */
static void testMenuLanguage(void)
{
    useConfig("version = 1\n");
    const int mainL = enterMain(1);
    (void)mainL;
    NonLinearCameraMove = 4; /* German */
    ico_opt_set_ui_language(-1);
    frame(0);
    CHECK(ui_get_language() == UI_LANG_DE && ui_sheet_language() == UI_LANG_DE,
          "Auto: the game's German (%d, sheets %d)", ui_get_language(), ui_sheet_language());
    CHECK(strcmp(ui_settings_value_text(UI_OPT_MENU_LANGUAGE), "Automatisch") == 0 ||
              ui_settings_value_text(UI_OPT_MENU_LANGUAGE)[0] != '\0',
          "the row's value: %s", ui_settings_value_text(UI_OPT_MENU_LANGUAGE));
    /* Auto, en, fr, de, it, es, ja, ko, zh, Auto */
    ui_settings_step(UI_OPT_MENU_LANGUAGE, 1);
    CHECK(ico_opt_ui_language() == UI_LANG_EN && ui_get_language() == UI_LANG_EN &&
              ui_sheet_language() == UI_LANG_DE,
          "English menus over the German game (%d, sheets %d)", ui_get_language(),
          ui_sheet_language());
    for (int i = 0; i < 5; i++) {
        ui_settings_step(UI_OPT_MENU_LANGUAGE, 1);
    }
    CHECK(ico_opt_ui_language() == UI_LANG_JA && ui_get_language() == UI_LANG_JA,
          "Japanese menus (%d)", ui_get_language());
    CHECK(strcmp(ui_settings_value_text(UI_OPT_MENU_LANGUAGE),
                 "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E") == 0,
          "the row names Japanese in Japanese: %s", ui_settings_value_text(UI_OPT_MENU_LANGUAGE));
    ui_settings_step(UI_OPT_LANGUAGE, 1); /* the game to Italian: the menus stay Japanese */
    CHECK(NonLinearCameraMove == 5 && ui_get_language() == UI_LANG_JA &&
              ui_sheet_language() == UI_LANG_IT,
          "the game's Italian under Japanese menus (%d, %d)", ui_get_language(),
          ui_sheet_language());
    ui_settings_step(UI_OPT_MENU_LANGUAGE, 1);
    CHECK(ico_opt_ui_language() == UI_LANG_KO && ui_get_language() == UI_LANG_KO &&
              ui_sheet_language() == UI_LANG_IT,
          "Korean menus over the Italian game (%d, %d)", ui_get_language(), ui_sheet_language());
    CHECK(strcmp(ui_settings_value_text(UI_OPT_MENU_LANGUAGE),
                 "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4") == 0,
          "the row names Korean in Korean: %s", ui_settings_value_text(UI_OPT_MENU_LANGUAGE));
    ui_settings_step(UI_OPT_MENU_LANGUAGE, 1);
    CHECK(ico_opt_ui_language() == UI_LANG_ZH && ui_get_language() == UI_LANG_ZH &&
              ui_sheet_language() == UI_LANG_IT && ui__font_chinese_first(),
          "Chinese menus over the Italian game (%d, %d)", ui_get_language(), ui_sheet_language());
    CHECK(strcmp(ui_settings_value_text(UI_OPT_MENU_LANGUAGE),
                 "\xE7\xB9\x81\xE9\xAB\x94\xE4\xB8\xAD\xE6\x96\x87") == 0,
          "the row names Chinese in Chinese: %s", ui_settings_value_text(UI_OPT_MENU_LANGUAGE));
    ui_settings_step(UI_OPT_MENU_LANGUAGE, 1);
    CHECK(!ui__font_chinese_first(), "Auto: the Japanese face is first again");
    CHECK(ico_opt_ui_language() == -1 && ui_get_language() == UI_LANG_IT, "Auto again: Italian");
    CHECK(ui_settings_save() == 0, "saved");
    NonLinearCameraMove = 2;
    ico_opt_set_ui_language(-1);
    ui_settings_apply_language();
    CHECK(ui_get_language() == UI_LANG_EN, "back to English");
}

/* the Japanese or Korean disc (port/core/region.h; both are the Japanese
   program): its pad convention, the rows it has not, the packs' folder;
   switched back to the European disc */
static void testOtherDisc(const char *slug)
{
    char note[256];
    CHECK(lt_ext_confirm_button() == 0x40 && lt_ext_port_back_buttons() == 0x30 &&
              lt_ext_confirm_glyph() == LT_GLYPH_CROSS && lt_ext_has_circle_back(),
          "the European disc: Cross confirms, Triangle and Circle go back");
    CHECK(ico_region_select(slug) == 0, "the %s disc selected", slug);
    const IcoRegionInfo *ri = ico_region_cur();
    lt_ext_set_circle_back(1);
    CHECK(lt_ext_back_buttons() == 0x40, "the game menus cancel with Cross (%#x)",
          lt_ext_back_buttons());
    lt_ext_set_circle_back(0);
    CHECK(lt_ext_back_buttons() == 0x40, "circle_back does not apply (%#x)", lt_ext_back_buttons());
    lt_ext_set_circle_back(1);
    CHECK(lt_ext_confirm_button() == 0x20 && lt_ext_port_back_buttons() == 0x50 &&
              lt_ext_confirm_glyph() == LT_GLYPH_CIRCLE && !lt_ext_has_circle_back(),
          "Circle confirms, Triangle and Cross go back on the port's pages");
    CHECK(ico_photo_save_button() == 0x20 && ico_photo_back_button() == 0x40,
          "photo mode: Circle saves, Cross leaves");
    CHECK(!ui_settings_opt_shown(UI_OPT_CIRCLE_BACK) && !ui_settings_opt_shown(UI_OPT_LANGUAGE) &&
              !ui_settings_opt_shown(UI_OPT_VIDEO_MODE) &&
              ui_settings_opt_shown(UI_OPT_MENU_LANGUAGE),
          "no Circle goes back, game language or 50 Hz row; the Menu language row");
    ico_opt_set_ui_language(-1);
    CHECK(ui_settings_resolve_language() == ri->ui_lang_default,
          "Auto: the disc's language on the disc (%d)", ui_settings_resolve_language());
    ui_settings_apply_language();
    CHECK(ui_get_language() == ri->ui_lang_default && ui_sheet_language() == ri->menu_sheet,
          "the port's text the disc's, the sheets' look the disc's sheets' (%d, %d)",
          ui_get_language(), ui_sheet_language());
    ico_opt_set_ui_language(UI_LANG_EN);
    ui_settings_apply_language();
    CHECK(ui_get_language() == UI_LANG_EN && ui_sheet_language() == ri->menu_sheet,
          "English chosen: the sheets' look stays the disc's sheets'");
    ico_opt_set_ui_language(-1);
    ui_str_serial(UI_STR_TEXTURE_PACK_NOTE, note, sizeof(note));
    CHECK(strstr(note, ri->pack_serial) != NULL && strstr(note, "%s") == NULL,
          "the pack note names the disc's folder: %s", note);
    CHECK(ico_region_select("pal") == 0 && lt_ext_confirm_button() == 0x40 &&
              lt_ext_back_buttons() == 0x30 && ui_settings_opt_shown(UI_OPT_CIRCLE_BACK) &&
              ui_settings_opt_shown(UI_OPT_LANGUAGE),
          "the European disc again");
    ui_settings_apply_language();
}

/* the prompts on the Japanese or Korean disc: a Cross in a prompt line is the
   disc's confirm, drawn as Circle's picture (ui_hint.c), and Triangle
   stays Triangle */
static void testOtherDiscHints(const char *slug)
{
    CHECK(ico_region_select(slug) == 0, "the %s disc selected", slug);
    enterMain(1);
    static const UiHintItem items[] = {{LT_GLYPH_CROSS, -1, UI_STR_HINT_PLAY},
                                       {LT_GLYPH_TRIANGLE, -1, UI_STR_BACK}};
    UiHint h;
    ui_hint_build(&h, 200, 18.0f, items, 2);
    int uv[4];
    const int circleRow = lt_ext_glyph_source(LT_GLYPH_CIRCLE, uv);
    const LtProperty *g = lt_ext_prop(h.glyphRow[0][0]);
    CHECK(circleRow == ico_region_cur()->glyph[ICO_GLYPH_CIRCLE].row &&
              h.item[0].glyph == LT_GLYPH_CIRCLE && g->texU == uv[0] && g->texV == uv[1] &&
              g->texW == uv[2] && g->texH == uv[3],
          "the play prompt shows Circle (row %d, %d,%d %dx%d)", circleRow, g->texU, g->texV,
          g->texW, g->texH);
    CHECK(h.item[1].glyph == LT_GLYPH_TRIANGLE, "Back keeps Triangle");
    CHECK(ico_region_select("pal") == 0, "the European disc again");
}

/* the mouse on the Japanese or Korean disc: a click on an item is its confirm
   (Circle, 0x20: the page opens rather than the menu going back), and the
   mouse's Cross and Circle trade places while a menu is up (pointer.h
   ico_pointer_swap_face), not in play */
static void testOtherDiscPointer(const char *slug)
{
    CHECK(ico_region_select(slug) == 0, "the %s disc selected", slug);
    ui_mouse_reset();
    ui_mouse_set_view(640, 480, 0, 0, 640, 480);
    const int mainL = enterMain(1);
    int labels[20];
    ui_settings_page_rows(UI_PAGE_MAIN, labels, NULL, NULL, 20);
    const int iDisp = i17bMainIndex(UI_STR_SECTION_DISPLAY);
    CHECK(iDisp >= 0, "the Display row");
    mouseFrame();
    mouseFrame();
    CHECK(ui_mouse_menu_active() && ico_pointer_swap_face() == 1,
          "the main page: the mouse's Cross and Circle traded (%d)", ico_pointer_swap_face());
    pointAt(labels[iDisp]);
    mouseFrame();
    click();
    mouseFrame();
    CHECK(settle(ui_settings_page_layout(UI_PAGE_DISPLAY), 60),
          "a click on Display opens it (layout %d)", current_layout_id);
    lt_switch_layout(fxLayout(54));
    settle(fxLayout(54), 60);
    mouseFrame();
    CHECK(!ui_mouse_menu_active() && ico_pointer_swap_face() == 0,
          "in play the mouse keeps its buttons (%d)", ico_pointer_swap_face());
    (void)mainL;
    CHECK(ico_region_select("pal") == 0, "the European disc again");
    mouseFrame();
    CHECK(ico_pointer_swap_face() == 0, "the European disc never trades them");
}

/* Controls > Touch screen (UI_PAGE_TOUCH): the link only with a touch
   screen; Controls' fourteen rows and the page's nine fit from both
   entries; the overlay's three rows moved there (their values, steps, save
   and read back as [input] touch_mode, touch_size, touch_opacity); the
   overlay's note on Touch controls; Back to Controls on its Touch screen
   row */
static int s_tpTouch;

static int tpTouch(void)
{
    return s_tpTouch;
}

/* Controls' Touch screen row, -1 */
static int tpLink(void)
{
    int labels[16];
    const int n = ui_settings_page_rows(UI_PAGE_CONTROLS, labels, NULL, NULL, 16);
    for (int i = 0; i < n; i++) {
        if (lt_ext_prop(labels[i])->right == ui_settings_page_layout(UI_PAGE_TOUCH)) {
            return labels[i];
        }
    }
    return -1;
}

/* the main page to Controls, then Cross on Touch screen: the page's layout */
static int tpOpen(int mainL)
{
    const int ctlL = openPage(mainL, 4, UI_PAGE_CONTROLS);
    for (int k = 0; k < 4; k++) {
        frame(0);
    }
    lt_ext_layout(ctlL)->curItem = tpLink();
    press(lt_ext_confirm_button());
    const int l = ui_settings_page_layout(UI_PAGE_TOUCH);
    CHECK(settle(l, 60), "Touch screen opens (%d)", current_layout_id);
    return l;
}

static void testTouchPage(void)
{
    static const UiSettingsOpt kRows[3] = {UI_OPT_TOUCH_MODE, UI_OPT_TOUCH_SIZE,
                                           UI_OPT_TOUCH_OPACITY};
    IcoBindings *b = ico_input_live_bindings();
    char p[1100];

    for (int title = 1; title >= 0; title--) {
        s_tpTouch = 1;
        ui_settings_set_touch_query(tpTouch);
        const int mainL = enterMain(title);
        const int ctlL = openPage(mainL, 4, UI_PAGE_CONTROLS);
        for (int k = 0; k < 4; k++) {
            frame(0);
        }
        CHECK(tpLink() >= 0 && !lt_ext_prop(tpLink())->defaultMask,
              "title %d: the Touch screen row shown", title);
        for (int i = 0; i < 3; i++) {
            CHECK(ui_settings_row_of(UI_PAGE_CONTROLS, kRows[i]) < 0,
                  "title %d: touch row %d is not on Controls", title, i);
        }
        checkPageFits(UI_PAGE_CONTROLS,
                      title ? "Controls, touch (title)" : "Controls, touch (pause)");
        press(LT_PAD_TRIANGLE);
        CHECK(settle(mainL, 60), "title %d: Controls: back", title);
        const int touchL = tpOpen(mainL);
        for (int k = 0; k < 4; k++) {
            frame(0);
        }
        int labels[16], shown = 0;
        const int n = ui_settings_page_rows(UI_PAGE_TOUCH, labels, NULL, NULL, 16);
        for (int i = 0; i < n; i++) {
            shown += !lt_ext_prop(labels[i])->defaultMask;
        }
        CHECK(n == 9 && shown == 9, "title %d: Touch screen shows nine rows (%d of %d)", title,
              shown, n);
        CHECK(lt_ext_layout(touchL)->curItem ==
                  ui_settings_row_of(UI_PAGE_TOUCH, UI_OPT_TOUCH_MODE),
              "title %d: the page opens on Touch controls", title);
        checkPageFits(UI_PAGE_TOUCH, title ? "Touch screen (title)" : "Touch screen (pause)");
        CHECK(noteStarting(UI_PAGE_TOUCH, "On-screen buttons.") >= 0,
              "title %d: the overlay's note", title);
        press(LT_PAD_TRIANGLE);
        CHECK(settle(ctlL, 60), "title %d: Touch screen: back to Controls", title);
        CHECK(lt_ext_layout(ctlL)->curItem == tpLink(), "title %d: back on Touch screen", title);
        /* no touch screen: the row hides again */
        s_tpTouch = 0;
        for (int k = 0; k < 4; k++) {
            frame(0);
        }
        CHECK(lt_ext_prop(tpLink())->defaultMask, "title %d: no touch screen, no row", title);
        press(LT_PAD_TRIANGLE);
        CHECK(settle(mainL, 60), "title %d: Controls: back again", title);
    }

    /* the defaults, and a step of each row */
    useConfig("version = 1\n");
    ico_input_reload_bindings(b);
    CHECK(b->touch_mode == 1 && b->touch_size == 1 && b->touch_opacity == 75,
          "defaults: auto, medium, 75 (%d, %d, %d)", b->touch_mode, b->touch_size,
          b->touch_opacity);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_TOUCH_MODE), "Auto") == 0, "mode: Auto (%s)",
          ui_settings_value_text(UI_OPT_TOUCH_MODE));
    CHECK(strcmp(ui_settings_value_text(UI_OPT_TOUCH_SIZE), "Medium") == 0, "size: Medium");
    CHECK(strcmp(ui_settings_value_text(UI_OPT_TOUCH_OPACITY), "75 %") == 0, "opacity: 75 %%");
    ui_settings_step(UI_OPT_TOUCH_MODE, 1);
    ui_settings_step(UI_OPT_TOUCH_SIZE, 1);
    ui_settings_step(UI_OPT_TOUCH_OPACITY, 1);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_TOUCH_MODE), "Always") == 0, "Right: Always");
    CHECK(strcmp(ui_settings_value_text(UI_OPT_TOUCH_SIZE), "Large") == 0, "Right: Large");
    CHECK(strcmp(ui_settings_value_text(UI_OPT_TOUCH_OPACITY), "100 %") == 0, "Right: 100 %%");
    CHECK(b->touch_mode == 2 && b->touch_size == 2 && b->touch_opacity == 100,
          "the live table: always, large, 100");
    CHECK(ui_settings_save() == 0, "save the touch rows");
    path(p, sizeof(p), "settings_test.toml");
    IcoToml *t = ico_toml_load(p);
    CHECK(t &&
              strcmp(ico_toml_get(t, "input.touch_mode") ? ico_toml_get(t, "input.touch_mode") : "",
                     "always") == 0,
          "saved: touch_mode = always (%s)", t ? ico_toml_get(t, "input.touch_mode") : "");
    CHECK(t && ico_toml_get(t, "input.touch_size") &&
              strcmp(ico_toml_get(t, "input.touch_size"), "large") == 0,
          "saved: touch_size = large");
    CHECK(t && ico_toml_get_int(t, "input.touch_opacity", 0) == 100, "saved: touch_opacity = 100");
    CHECK(t && ico_toml_get(t, "touch.snap") == NULL && ico_toml_get(t, "touch.grid") == NULL,
          "no [touch] keys: nothing of the layout changed");
    ico_toml_free(t);
    CHECK(b->touch_mode == 2 && b->touch_size == 2 && b->touch_opacity == 100,
          "reloaded after the save: always, large, 100");
    /* around: Right from Always is Off, from Large Small, from 100 % 25 % */
    ui_settings_step(UI_OPT_TOUCH_MODE, 1);
    ui_settings_step(UI_OPT_TOUCH_SIZE, 1);
    ui_settings_step(UI_OPT_TOUCH_OPACITY, 1);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_TOUCH_MODE), "Off") == 0 &&
              strcmp(ui_settings_value_text(UI_OPT_TOUCH_SIZE), "Small") == 0 &&
              strcmp(ui_settings_value_text(UI_OPT_TOUCH_OPACITY), "25 %") == 0,
          "around: Off, Small, 25 %%");
    CHECK(ui_settings_save() == 0, "save again");

    /* read back from a file: a value between the steps steps from the
       nearest one */
    useConfig("version = 1\n[input]\ntouch_mode = \"off\"\ntouch_size = \"small\"\n"
              "touch_opacity = 60\n");
    ico_input_reload_bindings(b);
    CHECK(b->touch_mode == 0 && b->touch_size == 0 && b->touch_opacity == 60,
          "read back: off, small, 60 (%d, %d, %d)", b->touch_mode, b->touch_size, b->touch_opacity);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_TOUCH_MODE), "Off") == 0 &&
              strcmp(ui_settings_value_text(UI_OPT_TOUCH_OPACITY), "60 %") == 0,
          "the rows: Off, 60 %%");
    ui_settings_step(UI_OPT_TOUCH_OPACITY, 1);
    CHECK(b->touch_opacity == 75, "60 %% Right: 75 %% (%d)", b->touch_opacity);
    ui_settings_step(UI_OPT_TOUCH_MODE, -1);
    CHECK(b->touch_mode == 2, "Off Left: Always");
    /* a bad value keeps the default */
    useConfig("version = 1\n[input]\ntouch_mode = \"sometimes\"\ntouch_opacity = 5\n");
    ico_input_reload_bindings(b);
    CHECK(b->touch_mode == 1 && b->touch_opacity == 75, "bad values: the defaults (%d, %d)",
          b->touch_mode, b->touch_opacity);
    /* the defaults are not written when unchanged */
    useConfig("version = 1\n");
    ico_input_reload_bindings(b);
    CHECK(ico_input_write_bindings(b) == 0 &&
              ico_config_get_string("input.touch_mode", NULL) == NULL,
          "defaults: no touch keys written");

    ui_settings_set_touch_query(NULL);
    s_tpTouch = 0;
    useConfig("version = 1\n");
    ico_input_reload_bindings(b);
}

/* Touch screen > Edit layout and the layout editor's rows.  A fake
   UiTouchEditHost over a real IcoTouchEditor (touch_edit.h) and a layout
   from ico_touch_layout_edits: Cross on Edit layout opens the page (one
   masked row, no cursor) and begins the editor; Triangle comes back with
   the cursor on Edit layout, the editor ended keeping its work; the row is
   dimmed and inert with its note while Touch controls is Off or there is
   no host; Snap to grid and Grid size step the live edits and are saved
   as touch.snap and touch.grid; Reset layout clears the posture the host
   says, Reset all layouts every posture.  The editor's own Done (a tap on
   its toolbar key) and its gamepad selection run once the editor is the
   real one (the contract's placeholders give no toolbar: a NOTE line says
   they were skipped). */
static IcoTouchEditor s_ted;
static IcoTouchLayout s_tedLayout;
static int s_tedRule, s_tedBegins, s_tedEnds, s_tedKeep = -1;

static IcoTouchEditor *tedEditor(void)
{
    return &s_ted;
}

static void tedBegin(void)
{
    ico_touch_edit_begin(&s_ted, ico_input_live_touch_edits(), s_tedRule);
    s_tedBegins++;
}

static void tedEnd(int keep)
{
    ico_touch_edit_end(&s_ted, keep);
    s_tedEnds++;
    s_tedKeep = keep;
}

static int tedRule(void)
{
    return s_tedRule;
}

#define TED_W 2340
#define TED_H 1080

static const IcoTouchLayout *tedLayout(void)
{
    const IcoTouchInsets none = {0.0f, 0.0f, 0.0f, 0.0f};
    s_tedLayout = ico_touch_layout_edits(TED_W, TED_H, none, ICO_TOUCH_MEDIUM, NULL,
                                         s_ted.active ? &s_ted.work : ico_input_live_touch_edits());
    return &s_tedLayout;
}

static const UiTouchEditHost kTedHost = {tedEditor, tedBegin, tedEnd, tedRule, tedLayout};

/* Cross on Edit layout from the Touch screen page: whether the editor page
   came up */
static int tedOpen(int touchL)
{
    lt_ext_layout(touchL)->curItem = ui_settings_row_of(UI_PAGE_TOUCH, UI_OPT_TOUCH_EDIT);
    frame(0);
    press(lt_ext_confirm_button());
    return settle(ui_settings_page_layout(UI_PAGE_TOUCH_LAYOUT), 60);
}

static void testTouchEditorPage(void)
{
    IcoBindings *b = ico_input_live_bindings();
    IcoTouchEdits *e = ico_input_live_touch_edits();
    char p[1100];

    for (int title = 1; title >= 0; title--) {
        memset(&s_ted, 0, sizeof(s_ted));
        s_tedRule = ICO_TOUCH_RULE_FULL;
        s_tedBegins = s_tedEnds = 0;
        s_tedKeep = -1;
        s_tpTouch = 1;
        ui_settings_set_touch_query(tpTouch);
        ui_settings_set_touch_edit_host(&kTedHost);
        const int mainL = enterMain(title);
        ico_input_reload_bindings(b);
        ico_touch_edits_defaults(e);
        CHECK(ui_settings_touch_edit_host(NULL) == 1, "the host is installed");
        const int touchL = tpOpen(mainL);
        const int edit = ui_settings_row_of(UI_PAGE_TOUCH, UI_OPT_TOUCH_EDIT);
        const int edL = ui_settings_page_layout(UI_PAGE_TOUCH_LAYOUT);
        frame(0);
        CHECK(edit >= 0 && lt_ext_prop(edit)->right == edL && lt_ext_row_dim(edit) == 0,
              "title %d: Edit layout opens the editor's page", title);

        /* open, then Triangle */
        CHECK(tedOpen(touchL), "title %d: the editor's page opens (%d)", title, current_layout_id);
        CHECK(s_tedBegins == 1 && s_ted.active, "title %d: the editor begun (%d)", title,
              s_tedBegins);
        {
            const LtProp *lp = lt_ext_layout(edL);
            IcoTouchEditView v;
            CHECK(lp->curItem == -1 && lp->last == lp->first + 1 && lt_ext_prop(lp->first)->masked,
                  "title %d: one masked row, no cursor", title);
            ico_touch_edit_view(&s_ted, tedLayout(), &v);
            CHECK(v.active == 1 && v.rule == ICO_TOUCH_RULE_FULL && v.selected == -1,
                  "title %d: the view: active, full, nothing selected (%d %d %d)", title, v.active,
                  v.rule, v.selected);
            CHECK(ui_settings_covers_title() == title, "title %d: the page covers the title's logo",
                  title);
        }
        for (int k = 0; k < 8; k++) {
            frame(0); /* the page stays while nothing is pressed */
        }
        CHECK(current_layout_id == edL && s_tedEnds == 0, "title %d: the editor's page stays",
              title);
        press(LT_PAD_TRIANGLE);
        CHECK(settle(touchL, 60), "title %d: Triangle: back to Touch screen (%d)", title,
              current_layout_id);
        CHECK(lt_ext_layout(touchL)->curItem == edit, "title %d: back on Edit layout", title);
        CHECK(s_tedEnds == 1 && s_tedKeep == 1 && !s_ted.active,
              "title %d: the editor ended, its work kept (%d, %d)", title, s_tedEnds, s_tedKeep);

        /* the editor's Done and its gamepad (the real editor only) */
        {
            IcoTouchRect tb[ICO_TOUCH_ED_TB_COUNT], cf[ICO_TOUCH_ED_CF_COUNT];
            ico_touch_edit_toolbar(tedLayout(), tb, cf);
            if (tb[ICO_TOUCH_ED_TB_DONE].w > 0.0f) {
                CHECK(tedOpen(touchL), "title %d: the page again", title);
                press(LT_PAD_R1);
                IcoTouchEditView v;
                ico_touch_edit_view(&s_ted, tedLayout(), &v);
                CHECK(v.selected >= 0, "title %d: R1 selects a control (%d)", title, v.selected);
                const float nx = (tb[ICO_TOUCH_ED_TB_DONE].x + tb[ICO_TOUCH_ED_TB_DONE].w * 0.5f) /
                                 (float)TED_W,
                            ny = (tb[ICO_TOUCH_ED_TB_DONE].y + tb[ICO_TOUCH_ED_TB_DONE].h * 0.5f) /
                                 (float)TED_H;
                ico_touch_edit_event(&s_ted, 7, nx, ny, ICO_TOUCH_DOWN, 1000000ull);
                ico_touch_edit_event(&s_ted, 7, nx, ny, ICO_TOUCH_UP, 2000000ull);
                frame(0);
                CHECK(settle(touchL, 60), "title %d: Done: back to Touch screen", title);
                CHECK(s_tedEnds == 2 && s_tedKeep == 1 && lt_ext_layout(touchL)->curItem == edit,
                      "title %d: Done ended the editor, on Edit layout", title);
            } else {
                printf("NOTE: %s: the editor's toolbar is empty (the contract's placeholders): "
                       "the Done tap and the gamepad selection were skipped\n",
                       __func__);
            }
        }

        /* locked while Touch controls is Off: dimmed, its note, Cross inert */
        b->touch_mode = ICO_TOUCH_MODE_OFF;
        for (int k = 0; k < 4; k++) {
            frame(0);
        }
        CHECK(lt_ext_row_dim(edit) == 1 && lt_ext_prop(edit)->right == -1,
              "title %d: Off: Edit layout dimmed and locked", title);
        CHECK(noteStarting(UI_PAGE_TOUCH, "Turn Touch controls on") >= 0,
              "title %d: Off: the note says why", title);
        const int begins = s_tedBegins;
        lt_ext_layout(touchL)->curItem = edit;
        press(lt_ext_confirm_button());
        for (int k = 0; k < 20; k++) {
            frame(0);
        }
        CHECK(current_layout_id == touchL && s_tedBegins == begins,
              "title %d: Off: Cross does not open the editor", title);
        b->touch_mode = ICO_TOUCH_MODE_AUTO;
        /* and without a host */
        ui_settings_set_touch_edit_host(NULL);
        for (int k = 0; k < 4; k++) {
            frame(0);
        }
        CHECK(lt_ext_row_dim(edit) == 1 && lt_ext_prop(edit)->right == -1,
              "title %d: no host: Edit layout locked", title);
        ui_settings_set_touch_edit_host(&kTedHost);
        for (int k = 0; k < 4; k++) {
            frame(0);
        }
        CHECK(lt_ext_row_dim(edit) == 0 && lt_ext_prop(edit)->right == edL,
              "title %d: Auto with a host: open again", title);

        /* Snap to grid and Grid size: the live edits, saved on leaving */
        CHECK(strcmp(ui_settings_value_text(UI_OPT_TOUCH_SNAP), "On") == 0 &&
                  strcmp(ui_settings_value_text(UI_OPT_TOUCH_GRID), "4 mm") == 0,
              "title %d: defaults: snap On, grid 4 mm (%s, %s)", title,
              ui_settings_value_text(UI_OPT_TOUCH_SNAP), ui_settings_value_text(UI_OPT_TOUCH_GRID));
        const unsigned gen0 = e->gen;
        lt_ext_layout(touchL)->curItem = ui_settings_row_of(UI_PAGE_TOUCH, UI_OPT_TOUCH_SNAP);
        press(LT_PAD_RIGHT);
        CHECK(e->snap == 0 && strcmp(ui_settings_value_text(UI_OPT_TOUCH_SNAP), "Off") == 0 &&
                  e->gen != gen0,
              "title %d: Right: snap Off", title);
        lt_ext_layout(touchL)->curItem = ui_settings_row_of(UI_PAGE_TOUCH, UI_OPT_TOUCH_GRID);
        press(LT_PAD_RIGHT);
        CHECK(e->grid == 8.0f && strcmp(ui_settings_value_text(UI_OPT_TOUCH_GRID), "8 mm") == 0,
              "title %d: Right: grid 8 mm", title);
        press(LT_PAD_RIGHT);
        CHECK(e->grid == 2.0f, "title %d: Right again: around to 2 mm", title);
        press(LT_PAD_LEFT);
        CHECK(e->grid == 8.0f, "title %d: Left: back to 8 mm", title);

        /* Reset layout: the host's posture; Reset all layouts: every one */
        e->ctl[ICO_TOUCH_RULE_SPLIT][ICO_TOUCH_CTL_CROSS].set = 1;
        e->ctl[ICO_TOUCH_RULE_FULL][ICO_TOUCH_CTL_CROSS].set = 1;
        s_tedRule = ICO_TOUCH_RULE_SPLIT;
        lt_ext_layout(touchL)->curItem = ui_settings_row_of(UI_PAGE_TOUCH, UI_OPT_TOUCH_RESET);
        press(lt_ext_confirm_button());
        CHECK(!ico_touch_edits_any(e, ICO_TOUCH_RULE_SPLIT) &&
                  ico_touch_edits_any(e, ICO_TOUCH_RULE_FULL),
              "title %d: Reset layout cleared the split posture only", title);
        CHECK(current_layout_id == touchL, "title %d: Reset stays on the page", title);
        lt_ext_layout(touchL)->curItem = ui_settings_row_of(UI_PAGE_TOUCH, UI_OPT_TOUCH_RESET_ALL);
        press(lt_ext_confirm_button());
        CHECK(!ico_touch_edits_any(e, -1), "title %d: Reset all layouts cleared every posture",
              title);
        s_tedRule = ICO_TOUCH_RULE_FULL;

        /* leaving the page writes [touch] */
        const int ctlL = ui_settings_page_layout(UI_PAGE_CONTROLS);
        press(LT_PAD_TRIANGLE);
        CHECK(settle(ctlL, 60), "title %d: Touch screen: back", title);
        path(p, sizeof(p), "settings_test.toml");
        IcoToml *t = ico_toml_load(p);
        CHECK(t && ico_toml_has(t, "touch.snap") && ico_toml_get_bool(t, "touch.snap", 1) == 0 &&
                  ico_toml_get_int(t, "touch.grid", 0) == 8,
              "title %d: saved: touch.snap = false, touch.grid = 8", title);
        if (t) {
            ico_toml_free(t);
        }
        CHECK(e->snap == 0 && e->grid == 8.0f, "title %d: the edits read back", title);
        press(LT_PAD_TRIANGLE);
        CHECK(settle(mainL, 60), "title %d: Controls: back", title);
    }
    ui_settings_set_touch_edit_host(NULL);
    ui_settings_set_touch_query(NULL);
    s_tpTouch = 0;
    useConfig("version = 1\n");
    ico_input_reload_bindings(b);
    ico_input_reload_touch_edits(e);
    memset(&s_ted, 0, sizeof(s_ted));
}

/* ---------------------------------------------------- Extras > Game variants */

/* stderr to a file for a stretch: the log lines the page writes */
static int s_varErrSaved = -1;

static void varErrCapture(void)
{
    char p[1100];
    path(p, sizeof(p), "settings_test_stderr.txt");
    fflush(stderr);
    s_varErrSaved = dup(2);
    FILE *f = freopen(p, "wb", stderr);
    (void)f;
}

static void varErrRelease(char *out, size_t n)
{
    char p[1100];
    path(p, sizeof(p), "settings_test_stderr.txt");
    fflush(stderr);
    dup2(s_varErrSaved, 2);
    close(s_varErrSaved);
    clearerr(stderr);
    out[0] = '\0';
    FILE *f = fopen(p, "rb");
    if (f) {
        size_t got = fread(out, 1, n - 1, f);
        out[got] = '\0';
        fclose(f);
    }
}

/* the note row starting with prefix, its text on one line contains want */
static int varNoteHas(const char *prefix, const char *want)
{
    const int note = noteStarting(UI_PAGE_VARIANTS, prefix);
    char flat[512];
    if (note < 0) {
        return 0;
    }
    snprintf(flat, sizeof(flat), "%s", lt_ext_row_text(note));
    for (char *c = flat; *c; c++) {
        *c = *c == '\n' ? ' ' : *c;
    }
    return strstr(flat, want) != NULL;
}

/* Extras > Game variants from the title or the pause menu: the link after
   Characters, the page's rows and values (the game as shipped), a step on
   every row (the value text, the [variants] key, the saved file), each
   row's note and whether it says achievements go off, the achievements rule,
   and the Debug menu row (hidden without Developer mode; a press marks the
   menu used and calls the hook, whose stub logs once).  who: the disc */
static void testVariantsPage(const char *who, int title, int *debugPressed)
{
    static const char *const kLabels[10] = {"Shade toughness",
                                            "One hit per swing",
                                            "Shades never dodge",
                                            "Shade aggression",
                                            "No hints",
                                            "Queen barrier",
                                            "No cutscene look-around",
                                            "Ignore cutscene cameras",
                                            "Debug menu",
                                            "Back"};
    static const char *const kDefaults[8] = {"Original", "Off",    "Off", "Their own",
                                             "Off",      "5 hits", "Off", "Off"};
    char p[1100], log[1024];

    /* the rows are read in English on every disc (the Japanese and Korean
       discs default to their own text; the title's boot re-applies the
       config's choice, so it is the config's) */
    useConfig("version = 1\n[ui]\nlanguage = \"en\"\n");
    ui_settings_apply_language();
    stage_no = title ? 1 : 11;
    const int mainL = enterMain(title);
    const int exL = openPage(mainL, 8, UI_PAGE_EXTRAS);
    int el[9];
    const int en = ui_settings_page_rows(UI_PAGE_EXTRAS, el, NULL, NULL, 9);
    CHECK(en == 9 && strcmp(lt_ext_row_text(el[4]), "Game variants") == 0 &&
              !lt_ext_prop(el[4])->defaultMask &&
              lt_ext_prop(el[4])->right == ui_settings_page_layout(UI_PAGE_VARIANTS),
          "%s %s: the Game variants link after Characters, on both entries", who,
          title ? "title" : "pause");
    lt_ext_layout(exL)->curItem = el[4];
    frame(0);
    press(0x40);
    const int varL = ui_settings_page_layout(UI_PAGE_VARIANTS);
    CHECK(settle(varL, 60), "%s: Cross on Game variants opens the page", who);
    for (int k = 0; k < 4; k++) {
        frame(0);
    }

    int lb[16], lo[16], lv[16];
    const int n = ui_settings_page_rows(UI_PAGE_VARIANTS, lb, lo, lv, 16);
    CHECK(n == 10, "%s: ten rows (%d)", who, n);
    for (int i = 0; i < n && i < 10; i++) {
        CHECK(strcmp(lt_ext_row_text(lb[i]), kLabels[i]) == 0, "%s: row %d is \"%s\" (\"%s\")", who,
              i, kLabels[i], lt_ext_row_text(lb[i]));
    }
    /* the game as shipped: every value, and every key unset */
    for (int i = 0; i < 8; i++) {
        CHECK(strcmp(lt_ext_row_text(lv[i]), kDefaults[i]) == 0,
              "%s: row %d starts \"%s\" (\"%s\")", who, i, kDefaults[i], lt_ext_row_text(lv[i]));
    }
    CHECK(ico_variant_shade_toughness() == ICO_TOUGHNESS_ORIGINAL && !ico_variant_one_hit() &&
              !ico_variant_never_dodge() && ico_variant_aggression() == ICO_AGGRESSION_OWN &&
              !ico_variant_no_hints() && ico_variant_barrier_hits() == 5 &&
              !ico_variant_no_cutscene_look() && !ico_variant_ignore_cutscene_cameras() &&
              !ico_variants_suspend_achievements(),
          "%s: the accessors answer the shipped game", who);
    /* the Debug menu row is Developer mode's */
    CHECK(lt_ext_prop(lb[8])->defaultMask && !lt_ext_prop(lb[9])->defaultMask,
          "%s: no Debug menu row without Developer mode", who);
    checkPageFits(UI_PAGE_VARIANTS, "Game variants (nine rows)");

    /* Shade toughness: Original, American, Not tougher later, around, both ways */
    lt_ext_layout(varL)->curItem = lb[0];
    frame(0);
    press(0x2000);
    CHECK(strcmp(lt_ext_row_text(lv[0]), "American") == 0 &&
              ico_variant_shade_toughness() == ICO_TOUGHNESS_AMERICAN &&
              strcmp(ico_config_get_string("variants.shade_toughness", ""), "american") == 0,
          "%s: Right: American and its key", who);
    press(0x2000);
    CHECK(strcmp(lt_ext_row_text(lv[0]), "Not tougher later") == 0 &&
              ico_variant_shade_toughness() == ICO_TOUGHNESS_NO_PLUS &&
              strcmp(ico_config_get_string("variants.shade_toughness", ""), "no_plus") == 0,
          "%s: Right: Not tougher later and its key", who);
    press(0x2000);
    CHECK(strcmp(lt_ext_row_text(lv[0]), "Original") == 0, "%s: Right wraps to Original", who);
    press(0x8000);
    CHECK(strcmp(lt_ext_row_text(lv[0]), "Not tougher later") == 0,
          "%s: Left wraps to Not tougher later", who);
    CHECK(!ico_variants_suspend_achievements(), "%s: Toughness keeps the achievements", who);
    press(0x2000);
    CHECK(varNoteHas("American: every Shade as tough as on the North American disc",
                     "no tougher on later playthroughs. Achievements stay on."),
          "%s: the toughness note, achievements stay on", who);

    /* the aggression row: five values around */
    lt_ext_layout(varL)->curItem = lb[3];
    frame(0);
    {
        static const char *const kAgg[5] = {"Bold, patient", "Bold, quick", "Wary, patient",
                                            "Wary, quick", "Their own"};
        static const char *const kAggKey[5] = {"bold_patient", "bold_quick", "wary_patient",
                                               "wary_quick", "own"};
        for (int k = 0; k < 5; k++) {
            press(0x2000);
            CHECK(strcmp(lt_ext_row_text(lv[3]), kAgg[k]) == 0 &&
                      strcmp(ico_config_get_string("variants.aggression", ""), kAggKey[k]) == 0,
                  "%s: aggression step %d is \"%s\" (\"%s\")", who, k, kAgg[k],
                  lt_ext_row_text(lv[3]));
        }
    }
    CHECK(!ico_variants_suspend_achievements(), "%s: aggression keeps the achievements", who);
    press(0x8000);
    CHECK(strcmp(lt_ext_row_text(lv[3]), "Wary, quick") == 0 &&
              ico_variant_aggression() == ICO_AGGRESSION_WARY_QUICK,
          "%s: Left from Their own: Wary, quick", who);
    press(0x2000);

    /* the barrier: 5 hits, 7 hits, around; written as a number */
    lt_ext_layout(varL)->curItem = lb[5];
    frame(0);
    press(0x2000);
    CHECK(strcmp(lt_ext_row_text(lv[5]), "7 hits") == 0 && ico_variant_barrier_hits() == 7 &&
              ico_config_get_int("variants.barrier_hits", 0) == 7,
          "%s: Right: 7 hits and the key", who);
    CHECK(!ico_variants_suspend_achievements(), "%s: the barrier keeps the achievements", who);
    CHECK(varNoteHas("How many hits it takes to break the Queen", "Achievements stay on."),
          "%s: the barrier note", who);
    press(0x2000);
    CHECK(strcmp(lt_ext_row_text(lv[5]), "5 hits") == 0 && ico_variant_barrier_hits() == 5 &&
              ico_config_get_int("variants.barrier_hits", 0) == 5,
          "%s: Right again: 5 hits, named in the file", who);

    /* the switches: On and the key; the notes; the achievements rule */
    static const struct {
        int row;
        IcoVariantId id;
        const char *key;
        const char *notePrefix;
        int suspends;
    } kSw[] = {
        {1, ICO_VARIANT_ONE_HIT, "variants.one_hit", "Each swing of Ico", 1},
        {2, ICO_VARIANT_NEVER_DODGE, "variants.never_dodge", "The Shades never step aside", 1},
        {4, ICO_VARIANT_NO_HINTS, "variants.no_hints", "Yorda gives no hints", 0},
        {6, ICO_VARIANT_NO_CUTSCENE_LOOK, "variants.no_cutscene_look", "The right stick no longer",
         0},
        {7, ICO_VARIANT_IGNORE_CUTSCENE_CAMERAS, "variants.ignore_cutscene_cameras",
         "Cutscenes can look wrong", 1},
    };

    for (int k = 0; k < (int)(sizeof(kSw) / sizeof(kSw[0])); k++) {
        lt_ext_layout(varL)->curItem = lb[kSw[k].row];
        frame(0);
        press(0x2000);
        CHECK(strcmp(lt_ext_row_text(lv[kSw[k].row]), "On") == 0 &&
                  ico_variant_get(kSw[k].id) == 1 && ico_config_get_bool(kSw[k].key, 0) == 1,
              "%s: %s: On and the key", who, kLabels[kSw[k].row]);
        CHECK(ico_variants_suspend_achievements() == kSw[k].suspends, "%s: %s: achievements %s",
              who, kLabels[kSw[k].row], kSw[k].suspends ? "off" : "on");
        CHECK(varNoteHas(kSw[k].notePrefix,
                         kSw[k].suspends ? "Switches achievements off." : "Achievements stay on."),
              "%s: %s: the note says %s", who, kLabels[kSw[k].row],
              kSw[k].suspends ? "off" : "stay on");
        press(0x8000);
        CHECK(strcmp(lt_ext_row_text(lv[kSw[k].row]), "Off") == 0 &&
                  ico_variant_get(kSw[k].id) == 0 && ico_config_get_bool(kSw[k].key, 1) == 0,
              "%s: %s: Left: Off", who, kLabels[kSw[k].row]);
        CHECK(!ico_variants_suspend_achievements(), "%s: %s: achievements back on", who,
              kLabels[kSw[k].row]);
    }

    /* the saved file keeps what was chosen */
    lt_ext_layout(varL)->curItem = lb[1];
    frame(0);
    press(0x2000);
    lt_ext_layout(varL)->curItem = lb[0];
    frame(0);
    press(0x2000);
    CHECK(ui_settings_save() == 0, "%s: save", who);
    path(p, sizeof(p), "settings_test.toml");
    IcoToml *t = ico_toml_load(p);
    CHECK(t != NULL && ico_toml_get_bool(t, "variants.one_hit", 0) == 1 &&
              ico_toml_has(t, "variants.shade_toughness") &&
              strcmp(ico_toml_get(t, "variants.shade_toughness"), "american") == 0 &&
              ico_toml_get_bool(t, "variants.never_dodge", 1) == 0,
          "%s: saved: variants.one_hit = true, shade_toughness = \"american\"", who);
    if (t != NULL) {
        ico_toml_free(t);
    }

    /* Developer mode shows the Debug menu row; a press marks it used and
       calls the hook (ico_variants_open_debug_menu), whose stub logs once */
    press(0x10);
    CHECK(settle(exL, 60), "%s: Back to Extras", who);
    ico_opt_set_developer_mode(1);
    lt_ext_layout(exL)->curItem = el[4];
    frame(0);
    press(0x40);
    CHECK(settle(varL, 60), "%s: Game variants again", who);
    for (int k = 0; k < 4; k++) {
        frame(0);
    }
    CHECK(!lt_ext_prop(lb[8])->defaultMask, "%s: the Debug menu row with Developer mode on", who);
    checkPageFits(UI_PAGE_VARIANTS, "Game variants (ten rows)");
    CHECK(strcmp(lt_ext_row_text(lb[8]), "Debug menu") == 0 && lv[8] < 0,
          "%s: the Debug menu is a press row, no value", who);
    lt_ext_layout(varL)->curItem = lb[8];
    frame(0);
    CHECK(varNoteHas("The game\xE2\x80\x99s own debug menu", "Switches achievements off."),
          "%s: the Debug menu note", who);
    ico_variants_debug_menu_clear();
    ico_variant_set(ICO_VARIANT_ONE_HIT, 0);
    ico_variant_set(ICO_VARIANT_SHADE_TOUGHNESS, 0);
    CHECK(!ico_variants_suspend_achievements() || ico_opt_developer_mode(),
          "%s: nothing but Developer mode suspends before the press", who);
    varErrCapture();
    press(0x40);
    varErrRelease(log, sizeof(log));
    CHECK(strstr(log, "variants: the Debug menu is opened from Options") != NULL,
          "%s: the press is logged (\"%s\")", who, log);
    if (!*debugPressed) {
        CHECK(strstr(log, "variants: the debug menu hook is not wired yet") != NULL,
              "%s: the stub hook logs once (\"%s\")", who, log);
        *debugPressed = 1;
    } else {
        CHECK(strstr(log, "not wired yet") == NULL, "%s: the stub does not log twice", who);
    }
    CHECK(ico_variants_suspend_achievements() == 1, "%s: the Debug menu opened: achievements off",
          who);
    ico_variants_debug_menu_clear();
    ico_opt_set_developer_mode(0);
    press(0x10);
    CHECK(settle(exL, 60), "%s: Back to Extras at the end", who);
    useConfig("version = 1\n");
    ico_opt_reload();
}

static void testVariants(void)
{
    int debugPressed = 0;
    testVariantsPage("pal", 1, &debugPressed);
    testVariantsPage("pal", 0, &debugPressed);
}

/* the same on the Japanese or Korean disc (ctests settings_extra_jpn,
   settings_extra_kor): the page is every disc's */
static void testVariantsOtherDisc(const char *slug)
{
    int debugPressed = 0;
    CHECK(ico_region_select(slug) == 0, "the %s disc selected", slug);
    testVariantsPage(slug, 1, &debugPressed);
    testVariantsPage(slug, 0, &debugPressed);
    ico_opt_set_ui_language(-1);
    CHECK(ico_region_select("pal") == 0, "the European disc again");
    ui_settings_apply_language();
}

int main(int argc, char **argv)
{
    snprintf(s_dir, sizeof(s_dir), "%s", argc > 1 ? argv[1] : ".");
    setEnv("LC_ALL", NULL);
    setEnv("LANG", "en_GB.UTF-8");
    if (argc > 2) {
        /* ctests settings_extra_jpn, settings_extra_kor: the European disc,
           then the other disc's pad, rows, languages and notes, then back */
        testOtherDisc(argv[2]);
        testOtherDiscPointer(argv[2]);
        testOtherDiscHints(argv[2]);
        /* Skip the opening: the title's menu after the boot, on that disc */
        testSkipOpeningTitle(argv[2]);
        /* Extras > Game variants on that disc */
        testVariantsOtherDisc(argv[2]);
        printf("settings_extra_test %s: %s\n", argv[2], failures ? "failed" : "ok");
        return failures ? 1 : 0;
    }
    testFixture();
    /* Display > Window mode */
    testWindowMode();
    /* Effects > Cinematic bars */
    testCinematicBars();
    /* Effects > Water reflections */
    testWaterReflections();
    /* the select callback's argument */
    testItemSelectArg();
    /* Settings > Graphics driver, the Quit game label */
    testGpuDriver();
    testGraphicsApi();
    testGraphicsApply();
    testAddToSteam();
    testExportTextures();
    /* Main > Disc */
    testDiscRow();
    testReportPicture();
    testShareReport();
    testQuitGame();
    /* Controls > the mouse camera rows */
    testMouseCamera();
    /* the title kept unseen while Options reopens */
    testTitleReturn();
    /* the mouse pointer in the menus */
    testPointer();
    testSaveSlotPointer();
    /* Gameplay > Achievement pop-ups */
    testAchievementPopups();
    testDuplicatedRows();
    /* Remap controls: swap on conflict, the menu note */
    testRemapSwap();
    testRemapMenuNote();
    /* Escape's button on the New Game screen */
    testEscapeNewGame();
    /* Display > Resolution and Aspect ratio cycles */
    testResolutionCycle();
    /* Display > Anti-aliasing, the Display/Effects split */
    testAntiAlias();
    /* Gameplay > East Reflector door fix */
    testDoorFix();
    /* Controls > Hold type */
    testHoldType();
    /* Controls > Touch screen, Edit layout */
    testTouchPage();
    testTouchEditorPage();
    testNaturalChains();
    testSkipOpening();
    testSkipOpeningTitle("pal");
    /* Extras > Game variants */
    testVariants();
    /* the version in the Options menu */
    testVersion();
    /* Menu language, the Japanese and Korean discs */
    testMenuLanguage();
    testOtherDisc("jpn");
    testOtherDisc("kor");
    if (failures) {
        printf("settings_extra_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("settings_extra_test: ok\n");
    return 0;
}

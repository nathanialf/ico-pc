/* settings_test.c: the Settings menu.  CPU only.
 *
 *   - the menu builds over fake game tables shaped like the PAL ones: the
 *     pages and their rows, in order, with the expected labels;
 *   - the navigation repoint: the pause menu's Options (294) opens the
 *     menu, the title's 50/51 lead to the entry rows, the link
 *     chains, idempotence;
 *   - the entry rows' places from the table data: the title's evenly
 *     spaced between New Game and the copyright line, masked by default as
 *     New Game; the pause menu's Photo mode row under Options;
 *   - through the real layout_texture.c: the cursor in layout 57 moves onto
 *     Options, Cross opens the menu, a right press on Language changes the
 *     language, Triangle goes back to 57 with the cursor on Options;
 *   - the game's Options screen's settings on the pages: their game
 *     variables, the pause-only and cleared-only rows, Button
 *     configuration's way to the game's screen and back;
 *   - value cycling: each option's setter and its text;
 *   - the save: config.toml holds what changed;
 *   - the remap capture: a key, a gamepad source, the duplicate taken off
 *     the other target, the timeout, and the screen's Cross / capture /
 *     write path;
 *   - the boot skip's mapping over synthetic configs (language codes,
 *     video_mode, the card override).
 *
 * The Extras page and the Characters page are settings_extras_test.c's;
 * the screens drawn on a Vulkan device are settings_photo_test.c's
 * (settings_render).
 */
#include "settings_fixture.h"

static void testBuild(void)
{
    static const int mainOpts[] = {
        UI_OPT_LINK,          UI_OPT_LINK,        UI_OPT_LINK, UI_OPT_LINK, UI_OPT_LINK,
        UI_OPT_LINK,          UI_OPT_LANGUAGE,    UI_OPT_LINK, UI_OPT_LINK, UI_OPT_DEVELOPER,
        UI_OPT_DUMP_TEXTURES, UI_OPT_DUMP_MODELS, UI_OPT_BACK};
    static const int fxOpts[] = {UI_OPT_CRT,
                                 UI_OPT_CRT_STRENGTH,
                                 UI_OPT_EFFECT_GLOW,
                                 UI_OPT_EFFECT_DEPTH_OF_FIELD,
                                 UI_OPT_EFFECT_SOFTENING,
                                 UI_OPT_EFFECT_MOTION_BLUR,
                                 UI_OPT_EFFECT_FOG,
                                 UI_OPT_EFFECT_CINEMATIC_BARS,
                                 UI_OPT_BACK};
    static const int fxStrs[] = {UI_STR_OPT_CRT,
                                 UI_STR_OPT_CRT_STRENGTH,
                                 UI_STR_OPT_EFFECT_GLOW,
                                 UI_STR_OPT_EFFECT_DEPTH_OF_FIELD,
                                 UI_STR_OPT_EFFECT_SOFTENING,
                                 UI_STR_OPT_EFFECT_MOTION_BLUR,
                                 UI_STR_OPT_EFFECT_FOG,
                                 UI_STR_OPT_EFFECT_CINEMATIC_BARS,
                                 UI_STR_BACK};
    static const int mainStrs[] = {UI_STR_SECTION_DISPLAY,
                                   UI_STR_SECTION_EFFECTS,
                                   UI_STR_SECTION_GPU_DRIVER,
                                   UI_STR_SECTION_AUDIO,
                                   UI_STR_SECTION_CONTROLS,
                                   UI_STR_SECTION_GAMEPLAY,
                                   UI_STR_SECTION_LANGUAGE,
                                   UI_STR_SECTION_ACHIEVEMENTS,
                                   UI_STR_EXTRAS,
                                   UI_STR_OPT_DEVELOPER_MODE,
                                   UI_STR_OPT_DUMP_TEXTURES,
                                   UI_STR_OPT_DUMP_MODELS,
                                   UI_STR_BACK};
    static const int dispOpts[] = {UI_OPT_PRESET,       UI_OPT_RESOLUTION, UI_OPT_ASPECT,
                                   UI_OPT_WINDOW_MODE,  UI_OPT_VSYNC,      UI_OPT_FILTER,
                                   UI_OPT_TEXTURE_PACK, UI_OPT_MODEL_PACK, UI_OPT_FULL_HEIGHT,
                                   UI_OPT_FRAMERATE,    UI_OPT_BRIGHTNESS, UI_OPT_VIDEO_MODE,
                                   UI_OPT_BACK};
    static const int dispStrs[] = {UI_STR_OPT_PRESET,
                                   UI_STR_OPT_RESOLUTION,
                                   UI_STR_OPT_ASPECT,
                                   UI_STR_OPT_WINDOW_MODE,
                                   UI_STR_OPT_VSYNC,
                                   UI_STR_OPT_FILTERING,
                                   UI_STR_OPT_TEXTURE_PACK,
                                   UI_STR_OPT_MODEL_PACK,
                                   UI_STR_OPT_FULL_HEIGHT,
                                   UI_STR_OPT_FRAMERATE,
                                   UI_STR_OPT_BRIGHTNESS,
                                   UI_STR_OPT_VIDEO_MODE,
                                   UI_STR_BACK};
    static const int audioOpts[] = {UI_OPT_VOLUME, UI_OPT_MUSIC,  UI_OPT_EFFECTS,
                                    UI_OPT_OUTPUT, UI_OPT_DEVICE, UI_OPT_BACK};
    static const int audioStrs[] = {UI_STR_OPT_VOLUME, UI_STR_OPT_MUSIC_VOL, UI_STR_OPT_EFFECTS_VOL,
                                    UI_STR_OPT_OUTPUT, UI_STR_OPT_DEVICE,    UI_STR_BACK};
    static const int ctlOpts[] = {UI_OPT_LINK,         UI_OPT_BUTTON_CONFIG, UI_OPT_VIBRATION,
                                  UI_OPT_HOLD_TYPE,    UI_OPT_MOUSE_CAMERA,  UI_OPT_MOUSE_SENS,
                                  UI_OPT_MOUSE_INVERT, UI_OPT_MOUSE_SPEED,   UI_OPT_MOUSE_RANGE,
                                  UI_OPT_MOUSE_RETURN, UI_OPT_CIRCLE_BACK,   UI_OPT_TOUCH_MODE,
                                  UI_OPT_TOUCH_SIZE,   UI_OPT_TOUCH_OPACITY, UI_OPT_BACK};
    static const int ctlStrs[] = {
        UI_STR_OPT_REMAP,        UI_STR_OPT_BUTTON_CONFIG, UI_STR_OPT_VIBRATION,
        UI_STR_OPT_HOLD_TYPE,    UI_STR_OPT_MOUSE_CAMERA,  UI_STR_OPT_MOUSE_SENS,
        UI_STR_OPT_MOUSE_INVERT, UI_STR_OPT_MOUSE_SPEED,   UI_STR_OPT_MOUSE_RANGE,
        UI_STR_OPT_MOUSE_RETURN, UI_STR_OPT_CIRCLE_BACK,   UI_STR_OPT_TOUCH_MODE,
        UI_STR_OPT_TOUCH_SIZE,   UI_STR_OPT_TOUCH_OPACITY, UI_STR_BACK};
    static const int gameOpts[] = {UI_OPT_YORDA,   UI_OPT_STICK_FIX,  UI_OPT_FILM_EFFECT,
                                   UI_OPT_PLAYERS, UI_OPT_ACH_POPUPS, UI_OPT_BACK};
    static const int gameStrs[] = {UI_STR_OPT_YORDA,       UI_STR_OPT_STICK_FIX,
                                   UI_STR_OPT_FILM_EFFECT, UI_STR_OPT_PLAYERS,
                                   UI_STR_OPT_ACH_POPUPS,  UI_STR_BACK};
    static const int listOpts[8] = {UI_OPT_LIST, UI_OPT_LIST, UI_OPT_LIST, UI_OPT_LIST,
                                    UI_OPT_LIST, UI_OPT_LIST, UI_OPT_LIST, UI_OPT_LIST};
    static const int listStrs[8] = {-1, -1, -1, -1, -1, -1, -1, -1};

    useConfig("version = 1\n");
    fakeTables();
    lt_ext_reset();
    ui_settings_reset();
    ui_set_language(UI_LANG_EN);
    ui_settings_install();
    CHECK(labelsAre(UI_PAGE_MAIN, mainOpts, mainStrs, 13), "main page rows");
    CHECK(labelsAre(UI_PAGE_EFFECTS, fxOpts, fxStrs, 9), "effects rows");
    {
        /* Characters (a link) before Back */
        static const int extrasOpts[] = {UI_OPT_EXTRAS_MUSIC, UI_OPT_EXTRAS_MODELS,
                                         UI_OPT_EXTRAS_CREDITS, UI_OPT_LINK, UI_OPT_BACK};
        static const int extrasStrs[] = {UI_STR_EXTRAS_MUSIC, UI_STR_EXTRAS_MODELS,
                                         UI_STR_EXTRAS_CREDITS, UI_STR_SECTION_CHARACTERS,
                                         UI_STR_BACK};
        CHECK(labelsAre(UI_PAGE_EXTRAS, extrasOpts, extrasStrs, 5), "Extras page rows");
    }
    CHECK(labelsAre(UI_PAGE_DISPLAY, dispOpts, dispStrs, 13),
          "display rows (Frame rate without a framerate key)");
    CHECK(labelsAre(UI_PAGE_AUDIO, audioOpts, audioStrs, 6), "audio rows");
    CHECK(labelsAre(UI_PAGE_CONTROLS, ctlOpts, ctlStrs, 15), "controls rows");
    CHECK(labelsAre(UI_PAGE_GAMEPLAY, gameOpts, gameStrs, 6), "gameplay rows");
    CHECK(labelsAre(UI_PAGE_ACHIEVEMENTS, listOpts, listStrs, 8), "achievement slots");
    CHECK(labelsAre(UI_PAGE_REMAP, listOpts, listStrs, 8), "remap slots");

    /* sections open their pages; the rows loop like the Options rows */
    int main = ui_settings_row_of(UI_PAGE_MAIN, UI_OPT_LINK);
    CHECK(lt_ext_prop(main)->right == ui_settings_page_layout(UI_PAGE_DISPLAY), "Display opens");
    int labels[16], n = ui_settings_page_rows(UI_PAGE_MAIN, labels, NULL, NULL, 16);
    CHECK(lt_ext_prop(labels[n - 1])->downItem == labels[0] &&
              lt_ext_prop(labels[0])->upItem == labels[n - 1],
          "main rows wrap");
    /* each page is a contiguous range of port rows, its own layout */
    for (int p = 0; p < UI_PAGE_COUNT; p++) {
        LtProp *l = lt_ext_layout(ui_settings_page_layout((UiSettingsPage)p));
        CHECK(ui_settings_page_layout((UiSettingsPage)p) >= LT_GAME_LAYOUT_COUNT &&
                  l->first >= LT_GAME_PROPERTY_COUNT && l->last > l->first && l->proc != NULL,
              "page %d layout", p);
    }
    /* the gameplay option's explanation */
    int yorda = ui_settings_row_of(UI_PAGE_GAMEPLAY, UI_OPT_YORDA);
    CHECK(yorda >= 0, "the Yorda row");
    /* the Gameplay page holds the stick fix beside Yorda's */
    CHECK(ui_settings_row_of(UI_PAGE_GAMEPLAY, UI_OPT_STICK_FIX) >= 0, "the stick fix row");
    CHECK(ui_settings_row_of(UI_PAGE_CONTROLS, UI_OPT_STICK_FIX) < 0, "not on Controls");
    /* no Menu text or Font row (one behaviour: the game's
       words are the table's text, the port's own Arimo) */
    {
        int rows[16];
        const int n = ui_settings_page_rows(UI_PAGE_DISPLAY, rows, NULL, NULL, 16);
        for (int k = 0; k < n; k++) {
            CHECK(strcmp(lt_ext_row_text(rows[k]), "Menu text") != 0 &&
                      strcmp(lt_ext_row_text(rows[k]), "Font") != 0,
                  "Display row %d: \"%s\"", k, lt_ext_row_text(rows[k]));
        }
    }
    /* the New Game screen */
    int ml = ui_new_game_screen_layout();
    CHECK(ml >= LT_GAME_LAYOUT_COUNT && lt_ext_layout(ml)->proc != NULL, "New Game screen layout");
    static const char *const kNgLabel[2] = {"Mirror mode", "New Game+"};
    for (int r = 0; r < 2; r++) {
        const int off = ui_new_game_screen_row(r, 0), on = ui_new_game_screen_row(r, 1);
        CHECK(off >= 0 && on >= 0 && strcmp(lt_ext_row_text(off - 1), kNgLabel[r]) == 0 &&
                  strcmp(lt_ext_row_text(off), "Off") == 0 &&
                  strcmp(lt_ext_row_text(on), "On") == 0,
              "New Game screen row %d: %s Off / On", r, kNgLabel[r]);
        CHECK(lt_ext_prop(off)->rightItem == on && lt_ext_prop(on)->leftItem == off,
              "row %d: Off and On side by side", r);
        CHECK(lt_ext_prop(off)->dispY == lt_ext_prop(on)->dispY &&
                  lt_ext_prop(off - 1)->dispY == lt_ext_prop(off)->dispY,
              "row %d: the label, Off and On on one line", r);
    }
    CHECK(lt_ext_prop(ui_new_game_screen_row(1, 0))->dispY >
              lt_ext_prop(ui_new_game_screen_row(0, 0))->dispY,
          "New Game+ under Mirror mode");
    CHECK(ui_new_game_screen_row(2, 0) == -1 && ui_new_game_screen_row(-1, 1) == -1,
          "no third row");
    {
        /* every row of the screen inside the layout, the note's bottom
           at 212 at most */
        const LtProp *l = lt_ext_layout(ml);
        for (int k = l->first; k < l->last; k++) {
            CHECK(lt_ext_prop(k)->dispY >= 100 &&
                      lt_ext_prop(k)->dispY + lt_ext_prop(k)->dispH <= 212,
                  "New Game screen row %d in 100..212 (%d+%d)", k, lt_ext_prop(k)->dispY,
                  lt_ext_prop(k)->dispH);
        }
    }

    /* the Frame rate row in either preset, its value from the file */
    useConfig("[video]\npreset = \"enhanced\"\nframerate = \"144\"\n");
    lt_ext_reset();
    ui_settings_reset();
    ui_settings_install();
    CHECK(labelsAre(UI_PAGE_DISPLAY, dispOpts, dispStrs, 13), "display rows (Enhanced)");
    CHECK(strcmp(ui_settings_value_text(UI_OPT_FRAMERATE), "144 fps") == 0, "framerate 144 (%s)",
          ui_settings_value_text(UI_OPT_FRAMERATE));
}

/* Review finding 1: Settings (its Music page and the photo, quit and
   New Game screens included) and the model viewer's two screens built
   together stay inside the layout extension, with room to spare, and no
   row or layout came back -1.  The viewer's rows are built as
   port/game/model_viewer.c build() builds them (a heading, a list of 8
   slots with a right column, its hint; a list of 8 slots, two hints, three
   rows; two layouts). */
static int budgetCount(void *user)
{
    (void)user;
    return 3;
}

static void budgetFill(void *user, int d, UiListSlot *out)
{
    (void)user;
    (void)d;
    out->label = "model";
}

static void testBudget(void)
{
    enum { MARGIN = 64 };

    static const UiListDef def = {budgetCount, budgetFill, NULL, NULL, NULL};
    static UiList list, anim;
    static UiHint listHint, sticks, keys;
    UiListStyle st;

    useConfig("version = 1\n");
    fakeTables();
    lt_ext_reset();
    ui_settings_reset();
    ui_set_language(UI_LANG_EN);
    ui_settings_install();
    const int settingsRows = lt_ext_prop_count(), settingsLayouts = lt_ext_layout_count();
    for (int p = 0; p < UI_PAGE_COUNT; p++) {
        int rows[64];
        const int n = ui_settings_page_rows((UiSettingsPage)p, rows, NULL, NULL, 64);
        CHECK(ui_settings_page_layout((UiSettingsPage)p) >= LT_GAME_LAYOUT_COUNT,
              "page %d has its layout", p);
        for (int k = 0; k < n && k < 64; k++) {
            CHECK(rows[k] >= LT_GAME_PROPERTY_COUNT, "page %d row %d: index %d", p, k, rows[k]);
        }
    }
    /* the model viewer's screens, after Settings as in the game */
    const int first = LT_GAME_PROPERTY_COUNT + lt_ext_prop_count();
    const int header = ui_settings_add_row(20, 12, 600, 40, 0, -1, UI_STR_EXTRAS_MODELS, NULL,
                                           30.0f, UI_ALIGN_CENTER);
    memset(&st, 0, sizeof(st));
    st.y0 = 40;
    st.pitch = 18;
    st.label = (UiListCol){40, 400, 24.0f, UI_ALIGN_LEFT};
    st.colA = (UiListCol){440, 160, 21.0f, UI_ALIGN_RIGHT};
    st.statusY = 196;
    ui_list_build(&list, &def, NULL, &st);
    ui_hint_build(&listHint, 196, 19.0f, ui_hint_mv_list, UI_HINT_MV_LIST_COUNT);
    const int l1 = lt_ext_add_layout(&(LtProp){
        .first = first, .last = LT_GAME_PROPERTY_COUNT + lt_ext_prop_count(), .link = -1});
    const int first2 = LT_GAME_PROPERTY_COUNT + lt_ext_prop_count();
    memset(&st, 0, sizeof(st));
    st.y0 = 30;
    st.pitch = 15;
    st.label = (UiListCol){404, 216, 18.0f, UI_ALIGN_LEFT};
    st.colA = (UiListCol){620, 4, 18.0f, UI_ALIGN_RIGHT};
    st.statusY = 196;
    ui_list_build(&anim, &def, NULL, &st);
    ui_hint_build(&sticks, 180, 19.0f, ui_hint_mv_sticks, UI_HINT_MV_STICKS_COUNT);
    ui_hint_build(&keys, 196, 19.0f, ui_hint_mv_keys, UI_HINT_MV_KEYS_COUNT);
    int last = -1;
    for (int k = 0; k < 3; k++) {
        last = ui_settings_add_row(24, 10 + 14 * k, 360, 30, 0, -1, 0, " ", 19.0f, UI_ALIGN_LEFT);
        CHECK(last >= LT_GAME_PROPERTY_COUNT, "viewer row %d: index %d", k, last);
    }
    const int l2 = lt_ext_add_layout(&(LtProp){
        .first = first2, .last = LT_GAME_PROPERTY_COUNT + lt_ext_prop_count(), .link = -1});
    CHECK(header >= 0 && l1 >= 0 && l2 >= 0, "the viewer's heading and layouts (%d, %d, %d)",
          header, l1, l2);
    for (int i = 0; i < UI_LIST_SLOTS; i++) {
        CHECK(list.label[i] >= 0 && list.colA[i] >= 0 && anim.label[i] >= 0 && anim.colA[i] >= 0,
              "viewer list slot %d", i);
    }
    CHECK(list.status >= 0 && anim.status >= 0, "the viewer lists' status rows");
    const int rows = lt_ext_prop_count(), layouts = lt_ext_layout_count();
    printf("settings_test: layout extension: Settings %d rows, %d layouts; with the model viewer "
           "%d of %d rows, %d of %d layouts\n",
           settingsRows, settingsLayouts, rows, LT_EXT_MAX_PROPERTIES, layouts, LT_EXT_MAX_LAYOUTS);
    CHECK(rows + MARGIN <= LT_EXT_MAX_PROPERTIES, "%d rows leave %d of %d spare (at least %d)",
          rows, LT_EXT_MAX_PROPERTIES - rows, LT_EXT_MAX_PROPERTIES, MARGIN);
    CHECK(layouts + 4 <= LT_EXT_MAX_LAYOUTS, "%d layouts leave %d of %d spare (at least 4)",
          layouts, LT_EXT_MAX_LAYOUTS - layouts, LT_EXT_MAX_LAYOUTS);
    /* a bad index reads and writes a scratch row, never memory outside the
       tables */
    LtProperty *bad = lt_ext_prop(-1);
    CHECK(bad != NULL && lt_ext_prop_index(bad) == -1, "lt_ext_prop(-1) is a scratch row");
    bad->downItem = 5;
    CHECK(lt_ext_prop(LT_GAME_PROPERTY_COUNT + rows)->downItem == 0,
          "past the last row: a fresh scratch row");
    CHECK(lt_ext_prop_index(lt_ext_prop(LT_GAME_PROPERTY_COUNT - 1)) == LT_GAME_PROPERTY_COUNT - 1,
          "the game's last row is the game's");
}

/* the Frame rate row steps original, uncapped, 60, 120, 144, 240 */
static void testFramerate(void)
{
    static const int want[] = {ICO_FRAMERATE_UNCAPPED, 60, 120, 144, 240, ICO_FRAMERATE_ORIGINAL};
    static const char *const text[] = {"Uncapped", "60 fps",  "120 fps",
                                       "144 fps",  "240 fps", "Original"};
    char p[1100];
    IcoVideoOptions o;

    useConfig("[video]\nframerate = \"original\"\n");
    fakeTables();
    lt_ext_reset();
    ui_settings_reset();
    ui_set_language(UI_LANG_EN);
    ui_settings_install();
    CHECK(ui_settings_row_of(UI_PAGE_DISPLAY, UI_OPT_FRAMERATE) >= 0, "the row (Original preset)");
    CHECK(strcmp(ui_settings_value_text(UI_OPT_FRAMERATE), "Original") == 0, "framerate Original");
    for (int i = 0; i < 6; i++) {
        ui_settings_step(UI_OPT_FRAMERATE, 1);
        ico_video_get(&o);
        CHECK(o.framerate == want[i] &&
                  strcmp(ui_settings_value_text(UI_OPT_FRAMERATE), text[i]) == 0,
              "Right %d: %d \"%s\"", i + 1, o.framerate, ui_settings_value_text(UI_OPT_FRAMERATE));
    }
    ui_settings_step(UI_OPT_FRAMERATE, -1);
    ico_video_get(&o);
    CHECK(o.framerate == 240, "Left from original wraps to 240");
    /* the preset is not changed by the row; the rate is in force in the
       Original preset too */
    CHECK(ico_video_preset(&o) == ICO_VIDEO_ORIGINAL && ico_video_framerate() == 240,
          "Original preset: the row's rate in force");

    /* a cap the list does not hold steps to its neighbours */
    useConfig("[video]\nframerate = \"100\"\n");
    ui_settings_step(UI_OPT_FRAMERATE, 1);
    ico_video_get(&o);
    CHECK(o.framerate == 120, "100 Right: 120 (%d)", o.framerate);
    useConfig("[video]\nframerate = \"100\"\n");
    ui_settings_step(UI_OPT_FRAMERATE, -1);
    ico_video_get(&o);
    CHECK(o.framerate == 60, "100 Left: 60 (%d)", o.framerate);
    useConfig("[video]\nframerate = \"30\"\n");
    ui_settings_step(UI_OPT_FRAMERATE, -1);
    ico_video_get(&o);
    CHECK(o.framerate == ICO_FRAMERATE_UNCAPPED, "30 Left: uncapped (%d)", o.framerate);
    useConfig("[video]\nframerate = \"500\"\n");
    ui_settings_step(UI_OPT_FRAMERATE, 1);
    ico_video_get(&o);
    CHECK(o.framerate == ICO_FRAMERATE_ORIGINAL, "500 Right: original (%d)", o.framerate);
    useConfig("[video]\nframerate = \"500\"\n");
    ui_settings_step(UI_OPT_FRAMERATE, -1);
    ico_video_get(&o);
    CHECK(o.framerate == 240, "500 Left: 240 (%d)", o.framerate);

    /* persisted as [video] framerate, the string ico_video_parse_framerate reads */
    ui_settings_step(UI_OPT_FRAMERATE, -1); /* 240 -> 144 */
    CHECK(ui_settings_save() == 0, "save");
    path(p, sizeof(p), "settings_test.toml");
    IcoToml *t = ico_toml_load(p);
    CHECK(t != NULL && ico_toml_get(t, "video.framerate") != NULL &&
              strcmp(ico_toml_get(t, "video.framerate"), "144") == 0,
          "[video] framerate = \"144\"");
    ico_toml_free(t);
    ui_settings_step(UI_OPT_FRAMERATE, -1); /* 120 */
    ui_settings_step(UI_OPT_FRAMERATE, -1); /* 60 */
    ui_settings_step(UI_OPT_FRAMERATE, -1); /* uncapped */
    CHECK(ui_settings_save() == 0, "save");
    t = ico_toml_load(p);
    CHECK(t != NULL && ico_toml_get(t, "video.framerate") != NULL &&
              strcmp(ico_toml_get(t, "video.framerate"), "uncapped") == 0,
          "[video] framerate = \"uncapped\"");
    ico_toml_free(t);
}

/* the Preset row reads Original, Enhanced or Custom, and its step is the
   shortcut; the CRT note under Resolution; the Window mode row's query */
static int s_fsAnswer, s_fsAsked;

static int fakeWindowMode(void)
{
    s_fsAsked++;
    return s_fsAnswer;
}

static int rowWithPrefix(UiSettingsPage page, const char *prefix)
{
    LtProp *l = lt_ext_layout(ui_settings_page_layout(page));
    for (int j = l->first; j < l->last; j++) {
        if (strncmp(lt_ext_row_text(j), prefix, strlen(prefix)) == 0) {
            return j;
        }
    }
    return -1;
}

static void checkSavedPreset(const char *want)
{
    char p[1100];
    CHECK(ui_settings_save() == 0, "save");
    path(p, sizeof(p), "settings_test.toml");
    IcoToml *t = ico_toml_load(p);
    const char *v = t ? ico_toml_get(t, "video.preset") : NULL;
    CHECK(v != NULL && strcmp(v, want) == 0, "[video] preset = \"%s\" (%s)", want, v ? v : "none");
    ico_toml_free(t);
}

static void testPreset(void)
{
    IcoVideoOptions o;

    useConfig("version = 1\n");
    fakeTables();
    lt_ext_reset();
    ui_settings_reset();
    ui_set_language(UI_LANG_EN);
    ui_settings_install();
    ico_video_get(&o);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_PRESET), "Original") == 0 && o.resScale == 1 &&
              o.resW == 0 && o.aspect == ICO_ASPECT_4_3 && o.filter == ICO_FILTER_ORIGINAL &&
              !o.fullHeight,
          "fresh: Original, 1x, 4:3, original filter, half height");
    ui_settings_step(UI_OPT_PRESET, 1);
    ico_video_get(&o);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_PRESET), "Enhanced") == 0 && o.resScale == 0 &&
              o.aspect == ICO_ASPECT_AUTO && o.filter == ICO_FILTER_ANISOTROPIC && o.fullHeight,
          "Right: Enhanced, window, auto, anisotropic, full height");
    checkSavedPreset("enhanced");
    ui_settings_step(UI_OPT_ASPECT, 1);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_PRESET), "Custom") == 0, "an edited row: Custom");
    checkSavedPreset("enhanced"); /* Custom is stored as the four rows */
    ui_settings_step(UI_OPT_PRESET, 1);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_PRESET), "Enhanced") == 0,
          "Custom, Right: Enhanced");
    ui_settings_step(UI_OPT_PRESET, -1);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_PRESET), "Original") == 0,
          "Enhanced, Left: Original");
    checkSavedPreset("original");
    ui_settings_step(UI_OPT_ASPECT, 1);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_PRESET), "Custom") == 0, "Custom again");
    ui_settings_step(UI_OPT_PRESET, -1);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_PRESET), "Original") == 0, "Custom, Left: Original");

    /* the shortcut writes the four rows while the CRT filter locks Resolution */
    ui_settings_step(UI_OPT_CRT, 1);
    ui_settings_step(UI_OPT_PRESET, 1);
    ico_video_get(&o);
    CHECK(o.resScale == 0 && o.crt == 1 &&
              strcmp(ui_settings_value_text(UI_OPT_RESOLUTION), "1x (CRT)") == 0,
          "Preset under the CRT filter: resolution kept for later, row still 1x (CRT)");
    ui_settings_step(UI_OPT_CRT, -1);
    ico_video_get(&o);
    CHECK(o.crt == 0, "crt off again (%d)", o.crt);

    /* the Window mode row follows the query when one is installed */
    ico_video_get(&o);
    o.windowMode = ICO_WINDOW_FULLSCREEN;
    ico_video_set(&o);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_WINDOW_MODE), "Fullscreen") == 0,
          "no query: option");
    s_fsAnswer = ICO_WINDOW_WINDOWED;
    ui_settings_set_window_mode_query(fakeWindowMode);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_WINDOW_MODE), "Windowed") == 0,
          "the query: Windowed");
    s_fsAsked = 0;
    ui_settings_step(UI_OPT_WINDOW_MODE, 1);
    ico_video_get(&o);
    CHECK(s_fsAsked > 0 && o.windowMode == ICO_WINDOW_BORDERLESS, "step from the query (%d)",
          o.windowMode);
    ui_settings_set_window_mode_query(NULL);

    /* the note: the CRT one under Resolution, only while locked (one line,
       clear of the Back row below) */
    for (int title = 1; title >= 0; title--) {
        int mainL = enterMain(title);
        int dispL = openPage(mainL, 0, UI_PAGE_DISPLAY);
        int rows[16], n = ui_settings_page_rows(UI_PAGE_DISPLAY, rows, NULL, NULL, 16);
        int preset = n > 0 ? rows[0] : -1, res = n > 1 ? rows[1] : -1;
        int cn = rowWithPrefix(UI_PAGE_DISPLAY, "CRT filter:");
        CHECK(cn >= 0, "title %d: the CRT note exists", title);
        CHECK(cn >= 0 && strchr(lt_ext_row_text(cn), '\n') == NULL,
              "title %d: the note is one line", title);
        lt_ext_layout(dispL)->curItem = preset;
        frame(0);
        CHECK(cn >= 0 && lt_ext_prop(cn)->masked == 1, "title %d: the cursor on Preset: no note",
              title);
        lt_ext_layout(dispL)->curItem = res;
        frame(0);
        CHECK(cn >= 0 && lt_ext_prop(cn)->masked == 1,
              "title %d: CRT off, the cursor on Resolution: no note", title);
        ico_video_get(&o);
        o.crt = 1;
        o.crtStrength = 1.0f;
        ico_video_set(&o);
        frame(0);
        CHECK(cn >= 0 && lt_ext_prop(cn)->masked == 0, "title %d: CRT on, on Resolution: the note",
              title);
        lt_ext_layout(dispL)->curItem = preset;
        frame(0);
        CHECK(cn >= 0 && lt_ext_prop(cn)->masked == 1, "title %d: CRT on, cursor elsewhere", title);
        o.crt = 0;
        ico_video_set(&o);
    }
}

static void testRepoint(void)
{
    useConfig("version = 1\n");
    fakeTables();
    lt_ext_reset();
    ui_settings_reset();
    stage_no = 0; /* no stage: no Photo mode row */
    ui_settings_install();
    int s57 = ui_settings_entry_row(57), s12 = ui_settings_entry_row(12),
        s13 = ui_settings_entry_row(13);
    int ph = ui_settings_photo_row(), mainL = ui_settings_page_layout(UI_PAGE_MAIN);
    CHECK(s57 == 294 && ph >= LT_GAME_PROPERTY_COUNT && s12 > ph && s13 > s12, "entry rows");
    CHECK(ui_settings_entry_row(58) == -1 && ui_settings_entry_layout(58) == -1,
          "nothing on the Options screen");
    /* the pause menu's Options opens the menu, not the Options screen */
    CHECK(texProperty[294].right == mainL, "294: Cross opens the menu (%d)",
          texProperty[294].right);
    CHECK(texLayout[57].link == ui_settings_entry_layout(57) &&
              lt_ext_layout(ui_settings_entry_layout(57))->link == -1 &&
              lt_ext_layout(ui_settings_entry_layout(57))->first == ph,
          "57 -> the Photo mode row's layout");
    CHECK(texProperty[294].downItem == 295 && texProperty[295].upItem == 294 &&
              texProperty[295].dispY == 70 && lt_ext_prop(ph)->defaultMask,
          "no stage: the row masked, Options <-> Back as in the PAL data");
    CHECK(texLayout[58].link == -1, "the Options screen's chain untouched");
    CHECK(texProperty[50].downItem == s12 && lt_ext_prop(s12)->upItem == 50, "title 12");
    CHECK(texProperty[51].downItem == s13 && lt_ext_prop(s13)->upItem == 51, "title 13");
    CHECK(texLayout[12].link == ui_settings_entry_layout(12) &&
              lt_ext_layout(ui_settings_entry_layout(12))->link == 11 &&
              texLayout[13].link == ui_settings_entry_layout(13) &&
              lt_ext_layout(ui_settings_entry_layout(13))->link == 11,
          "12 and 13 -> entry -> 11");
    CHECK(ui_settings_entry_item(s12) && !ui_settings_entry_item(294) &&
              !ui_settings_entry_item(ph) && !ui_settings_entry_item(50),
          "entry items: the title's port rows");
    int count = lt_ext_prop_count();
    ui_settings_install();
    CHECK(lt_ext_prop_count() == count && texLayout[57].link == ui_settings_entry_layout(57) &&
              lt_ext_layout(ui_settings_entry_layout(57))->link == -1 &&
              lt_ext_layout(ui_settings_entry_layout(12))->link == 11,
          "a second install changes nothing");
    /* tables that are not the PAL ones are left alone */
    texLayout[57].first = 0;
    texProperty[294].right = 58;
    ui_settings_install();
    CHECK(texProperty[294].right == 58, "unexpected tables: no repoint");
}

/* The copyright line's capitals (row 48, a texture): 25 output pixels at
   960 x 720 on a window run's title, in y units (720 / 448 pixels each) */
#define COPYRIGHT_CAPS 15.5f

/* The entry rows on the game's grid: the title laid out by the port, Continue (49), New
   Game (50, and 51 at the same y), Settings and "Quit to desktop" one pitch
   apart, the same space between their capitals, and between Quit's and
   the copyright line's (48; its capitals 15.5 y units, measured; the
   others' through ui_font_metrics) to within a field line, the copyright
   line at most 5 field lines below the PAL one (the room measured below
   it); the port rows in New Game's box height, at the game rows'
   size, centred, masked by default as 49..51; a reinstall over reloaded
   PAL rows places them again; in Options the row continues the labels'
   pitch (323 -> 324 -> 325) and ends where their letters end (the menu
   text table's right anchor). */
static void testPlacement(void)
{
    useConfig("version = 1\n");
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 0) {
            fakeTables();
            lt_ext_reset();
            ui_settings_reset();
        } else {
            /* the tables reloaded from the disc: the PAL rows again */
            texProperty[49].dispY = 135;
            texProperty[50].dispY = texProperty[51].dispY = 165;
            texProperty[48].dispY = 195;
        }
        ui_settings_install();
        if (pass == 1) {
            CHECK(texProperty[50].dispY != 165, "a reinstall places the title again");
            ui_settings_install();
        }
    }
    const int copyright = texProperty[48].dispY;
    CHECK(copyright > 195 - 20 && copyright <= 195 + 5,
          "the copyright line at %d: inside the picture (at most 5 below 195)", copyright);
    CHECK(texProperty[51].dispY == texProperty[50].dispY,
          "New Game at the same y on both title layouts");
    for (int g = 12; g <= 13; g++) {
        const LtProperty *ng = &texProperty[g == 12 ? 50 : 51];
        const int si = ui_settings_entry_row(g), qi = ui_settings_quit_row(g);
        const LtProperty *s = lt_ext_prop(si);
        const LtProperty *q = lt_ext_prop(qi);
        const LtProperty *rows[5] = {&texProperty[49], ng, s, q, &texProperty[48]};
        /* capitals' middles (2 y units a field line; the game's and the
           port's rows share the anchor, layout_ext.c) and heights */
        float a, d, capGame, capS, capQ;
        ui_font_metrics(UI_MENU_TEXT_SIZE, &a, &d, &capGame);
        ui_font_metrics(lt_ext_row_size(si), &a, &d, &capS);
        ui_font_metrics(lt_ext_row_size(qi), &a, &d, &capQ);
        const float cap[5] = {capGame, capGame, capS, capQ, COPYRIGHT_CAPS};
        const int pitch = ng->dispY - rows[0]->dispY;
        float gaps[4];
        int even = pitch > 0;
        for (int i = 0; i < 4; i++) {
            even = even && (i == 3 || rows[i + 1]->dispY - rows[i]->dispY == pitch);
            gaps[i] = (2.0f * (float)rows[i + 1]->dispY - cap[i + 1] * 0.5f) -
                      (2.0f * (float)rows[i]->dispY + cap[i] * 0.5f);
        }
        CHECK(even && rows[4]->dispY > q->dispY,
              "title %d: Continue %d, New Game %d, Settings %d, Quit %d one pitch apart, the "
              "copyright %d below",
              g, rows[0]->dispY, ng->dispY, s->dispY, q->dispY, rows[4]->dispY);
        CHECK(gaps[0] > 0.0f && gaps[0] == gaps[1] && gaps[1] == gaps[2] &&
                  gaps[3] - gaps[2] < 2.0f && gaps[2] - gaps[3] < 2.0f,
              "title %d: the space between the capitals %.2f, %.2f, %.2f, to the copyright %.2f "
              "y units",
              g, gaps[0], gaps[1], gaps[2], gaps[3]);
        CHECK(lt_ext_row_size(si) == UI_MENU_TEXT_SIZE && lt_ext_row_size(qi) == UI_MENU_TEXT_SIZE,
              "title %d: the port rows at the game rows' size", g);
        CHECK(s->dispH == ng->dispH && q->dispH == ng->dispH && s->centerX == ng->centerX &&
                  q->centerX == ng->centerX,
              "title %d: New Game's box height and centring", g);
        CHECK(ng->defaultMask && s->defaultMask && q->defaultMask,
              "title %d: masked by default as New Game", g);
    }
    /* the pause menu's Photo mode row one pitch under Options while a
       stage runs, Back one pitch lower, End Game where it was; its letters
       start where Options' do (display_texture's box: x from dispX + 1/4;
       the game row's letters at dispX - 1/4 + the item's left anchor) */
    stage_no = 11;
    ui_settings_install();
    const LtProperty *r294 = &texProperty[294];
    const LtProperty *ph = lt_ext_prop(ui_settings_photo_row());
    CHECK(ph->dispY == r294->dispY + 20 && texProperty[295].dispY == r294->dispY + 40 &&
              texProperty[296].dispY == 120 && ph->dispH == r294->dispH && !ph->defaultMask,
          "pause: Options %d, Photo mode %d, Back %d, End Game %d", r294->dispY, ph->dispY,
          texProperty[295].dispY, texProperty[296].dispY);
    CHECK(lt_ext_row_size(ui_settings_photo_row()) == UI_MENU_TEXT_SIZE,
          "pause: the row at the game rows' size");
    const UiMenuTextItem *it = NULL;
    for (int i = 0; i < ui_menu_text_row_count; i++) {
        if (ui_menu_text_rows[i].row == 294) {
            it = &ui_menu_text_items[ui_menu_text_rows[i].item];
        }
    }
    CHECK(it && it->align == UI_ALIGN_LEFT, "Options (294) in the menu text table");
    if (it) {
        const float portStart = (float)ph->dispX + 0.25f;
        const float gameStart = (float)r294->dispX + it->x[UI_LANG_EN] - 0.25f;
        CHECK(portStart - gameStart <= 0.5f && gameStart - portStart <= 0.5f,
              "pause: the row starts at x %.2f, the rows' letters at %.2f", portStart, gameStart);
    }
    stage_no = 0;
    ui_settings_install();
    CHECK(texProperty[295].dispY == r294->dispY + 20 &&
              lt_ext_prop(ui_settings_photo_row())->masked,
          "no stage: Back in its own place (%d)", texProperty[295].dispY);
}

static void testNavigation(void)
{
    useConfig("version = 1\n");
    fakeTables();
    lt_ext_reset();
    ui_settings_reset();
    memset(pad, 0, sizeof(pad));
    pad[0].ana[0] = pad[0].ana[1] = pad[0].ana[2] = pad[0].ana[3] = 128;
    NonLinearCameraMove = 2;
    gFlagGameClear = 0;
    stage_no = 0;
    init_layout_texture(2); /* installs; layout 54 */
    CHECK(ui_settings_entry_layout(57) >= 0, "installed by init_layout_texture");
    settle(54, 4);
    /* the pause menu opens on Back (295); Up is Options, whose Cross
       opens the Settings menu */
    lt_switch_layout(57);
    CHECK(settle(57, 40), "the pause menu");
    CHECK(texLayout[57].curItem == 295, "on Back (%d)", texLayout[57].curItem);
    press(0x1000);
    CHECK(texLayout[57].curItem == 294, "up: Options (%d)", texLayout[57].curItem);
    press(0x40); /* Cross */
    int mainL = ui_settings_page_layout(UI_PAGE_MAIN);
    CHECK(settle(mainL, 60), "Cross opens the Settings menu (%d)", current_layout_id);
    /* down to Language (row 6), right: French */
    for (int i = 0; i < 5; i++) {
        press(0x4000);
    }
    CHECK(lt_ext_layout(mainL)->curItem == ui_settings_row_of(UI_PAGE_MAIN, UI_OPT_LANGUAGE),
          "on Language");
    int cur0 = s_sounds[0];
    press(0x2000);
    CHECK(NonLinearCameraMove == 3 && ui_get_language() == UI_LANG_FR, "right: French (%d)",
          NonLinearCameraMove);
    CHECK(s_sounds[0] > cur0, "the cursor sound");
    int labels[16], values[16];
    ui_settings_page_rows(UI_PAGE_MAIN, labels, NULL, values, 16);
    CHECK(strstr(lt_ext_row_text(values[6]), "Français") != NULL, "the value row: %s",
          lt_ext_row_text(values[6]));
    CHECK(strcmp(lt_ext_row_text(labels[0]), "Affichage") == 0, "labels follow the language");
    CHECK(strcmp(ico_config_get_string("game.language", ""), "fr") == 0, "[game] language");
    press(0x8000); /* left: English again */
    CHECK(NonLinearCameraMove == 2, "left: English");
    /* Triangle: back to the pause menu, the cursor on Options; its own
       default (Back) again for the next pause */
    press(0x10);
    CHECK(settle(57, 60), "Triangle: the pause menu again (%d)", current_layout_id);
    CHECK(texLayout[57].curItem == 294, "the cursor on Options (%d)", texLayout[57].curItem);
    frame(0);
    CHECK(texLayout[57].defaultItem == 295, "the pause menu's own default again (%d)",
          texLayout[57].defaultItem);

    /* into Controls -> Remap, capture a key on the first row (Cross) */
    press(0x40);
    CHECK(settle(mainL, 60), "the menu again");
    CHECK(lt_ext_layout(mainL)->curItem == labels[0], "opens on its first row");
    press(0x4000);
    press(0x4000);
    press(0x4000);
    press(0x40); /* Controls */
    int ctlL = ui_settings_page_layout(UI_PAGE_CONTROLS);
    CHECK(settle(ctlL, 60), "Controls");
    press(0x40); /* Remap controls */
    int remapL = ui_settings_page_layout(UI_PAGE_REMAP);
    CHECK(settle(remapL, 60), "the remap screen");
    ui_settings_page_rows(UI_PAGE_REMAP, labels, NULL, values, 16);
    CHECK(strcmp(lt_ext_row_text(labels[0]), "Cross") == 0 &&
              strstr(lt_ext_row_text(values[0]), "Space") != NULL,
          "row 1: Cross, Space (%s, %s)", lt_ext_row_text(labels[0]), lt_ext_row_text(values[0]));
    press(0x40); /* capture */
    frame(0);
    ico_input_note_press(ICO_SRC_KEY, ICO_KEY_K);
    frame(0);
    IcoBindings *b = ico_input_live_bindings();
    CHECK(b->kb[ICO_T_CROSS][0] == ICO_KEY_K && b->kb[ICO_T_CROSS][1] == 0, "Cross is K");
    CHECK(b->kb[ICO_T_RSTICK_DOWN][0] == 0, "K is off the right stick");
    frame(0);
    frame(0);
    frame(0);
    CHECK(strstr(lt_ext_row_text(values[0]), "K") != NULL, "the row shows K: %s",
          lt_ext_row_text(values[0]));
    /* scrolling: down past the eighth row */
    for (int i = 0; i < 9; i++) {
        press(0x4000);
    }
    CHECK(strcmp(lt_ext_row_text(labels[0]), "Circle") == 0 ||
              strcmp(lt_ext_row_text(labels[0]), "Square") == 0,
          "the list scrolled (%s)", lt_ext_row_text(labels[0]));
    press(0x10); /* back to Controls: the bindings are written */
    CHECK(settle(ctlL, 60), "Triangle: Controls");
    CHECK(strcmp(ico_config_get_string("input.kb.cross", ""), "K") == 0, "input.kb.cross = K");
    CHECK(strcmp(ico_config_get_string("input.kb.rstick_down", ""), "none") == 0,
          "input.kb.rstick_down = none");
    CHECK(ico_config_get_string("input.kb.circle", NULL) == NULL, "unchanged rows not written");
    ico_input_reload_bindings(b);
    CHECK(b->kb[ICO_T_CROSS][0] == ICO_KEY_K, "reloaded from the config");
}

/* Photo mode (in the pause menu): the "Photo mode" row exists only
 * while a stage runs (masked and stepped over on stage 0 or 1, the
 * title's); with one, it sits under Options, Cross opens the photo layout
 * (no dimming, the row masked), whose proc turns the left stick into an
 * orbit, Cross into a capture, Square into the HUD's toggle, and Triangle
 * back to the pause menu with the cursor on the row. */
static void testPhoto(void)
{
    useConfig("version = 1\n[photo]\nstick_speed = 2.0\n");
    fakeTables();
    lt_ext_reset();
    ui_settings_reset();
    ico_photo_reset();
    memset(pad, 0, sizeof(pad));
    pad[0].ana[0] = pad[0].ana[1] = pad[0].ana[2] = pad[0].ana[3] = 128;
    gFlagGameClear = 0;
    stage_no = 1; /* the title's stage */
    init_layout_texture(2);
    const int ph = ui_settings_photo_row(), pl = ui_photo_layout();
    CHECK(ph >= LT_GAME_PROPERTY_COUNT && pl >= LT_GAME_LAYOUT_COUNT &&
              lt_ext_prop(ph)->right == pl,
          "the row (%d) opens the photo layout (%d)", ph, pl);
    CHECK(strcmp(lt_ext_row_text(ph), ui_str(UI_STR_PHOTO_MODE)) == 0, "labelled \"%s\"",
          lt_ext_row_text(ph));
    CHECK(lt_ext_prop(ph)->left == -1, "Triangle on the row: the pause menu's own (it resumes)");
    settle(54, 4);
    lt_switch_layout(57);
    CHECK(settle(57, 40), "the pause menu");
    CHECK(lt_ext_prop(ph)->masked && texProperty[294].downItem == 295 &&
              texProperty[295].upItem == 294,
          "no stage: masked and stepped over");
    texLayout[57].curItem = 294;
    press(0x4000);
    CHECK(texLayout[57].curItem == 295, "no stage: down from Options is Back (%d)",
          texLayout[57].curItem);
    stage_no = 11; /* st04a: a stage runs */
    frame(0);
    CHECK(!lt_ext_prop(ph)->masked && texProperty[294].downItem == ph &&
              lt_ext_prop(ph)->upItem == 294 && lt_ext_prop(ph)->downItem == 295 &&
              texProperty[295].upItem == ph,
          "a stage: shown under Options");
    CHECK(lt_ext_prop(ph)->dispY == texProperty[294].dispY + 20 &&
              texProperty[295].dispY == texProperty[294].dispY + 40,
          "one pitch below Options, Back one lower (%d, %d)", lt_ext_prop(ph)->dispY,
          texProperty[295].dispY);
    texLayout[57].curItem = 294;
    press(0x4000);
    CHECK(texLayout[57].curItem == ph, "down from Options: the row (%d)", texLayout[57].curItem);
    press(0x4000);
    CHECK(texLayout[57].curItem == 295, "down from the row: Back (%d)", texLayout[57].curItem);
    press(0x1000);
    CHECK(texLayout[57].curItem == ph, "up from Back: the row (%d)", texLayout[57].curItem);
    press(0x40);
    CHECK(settle(pl, 60) && ico_photo_active(), "Cross: photo mode (%d)", current_layout_id);
    CHECK(lt_ext_layout(pl)->colA == 0.0f && lt_ext_prop(lt_ext_layout(pl)->first)->masked,
          "no dimming, nothing drawn");
    CHECK(ico_photo_mode() == ICO_PHOTO_CAM_FREE, "the free camera first");
    pad[0].ana[0] = 255; /* the right stick right: the free camera looks right */
    for (int i = 0; i < 25; i++) {
        frame(0);
    }
    pad[0].ana[0] = 128;
    IcoPhotoState st;
    ico_photo_get(&st);
    /* a second at full deflection past the dead zone: 90 x 2 degrees */
    CHECK(st.fyaw > 3.0f && st.fyaw < 3.3f && st.fpitch == 0.0f && st.yaw == 0.0f,
          "free: yaw %.3f, pitch %.3f", (double)st.fyaw, (double)st.fpitch);
    press(0x40);
    CHECK(ico_photo_take_capture() == 1 && ico_photo_take_capture() == 0 && current_layout_id == pl,
          "Cross: one capture, the mode stays");
    CHECK(ico_photo_hud(), "the HUD shown");
    press(0x80);
    CHECK(!ico_photo_hud() && ico_photo_active(), "Square: the HUD hidden");
    CHECK(ico_config_get_bool("photo.hide_ui", 0) == 1, "Square: hide_ui saved");
    press(0x10);
    CHECK(settle(57, 60) && !ico_photo_active(), "Triangle: the pause menu again (%d)",
          current_layout_id);
    CHECK(texLayout[57].curItem == ph, "the cursor on the row (%d)", texLayout[57].curItem);
    frame(0);
    CHECK(texLayout[57].defaultItem == 295, "the pause menu's own default again (%d)",
          texLayout[57].defaultItem);
    stage_no = 0;
}

/* the journey's lines on the pause menu's right.  The game state comes
 * through the view's sampler (ico_gamestate.h) as the program's comes from
 * the game: twelve minutes 34 s of play, three game overs, a capture, five
 * enemies defeated and a save in this run.  Shown while a stage runs, the
 * labels from x 300 and every line between the black bars (field lines 38
 * to 198), the values the port's figures; hidden on the title's stage and
 * in photo mode; the assists' line only with one on; the area only where
 * the save screen names one; the game's rows where they were. */
/* the pause entry layout's label row with this text (shown or not), -1 */
static int statLabel(const char *text)
{
    const LtProp *l = lt_ext_layout(ui_settings_entry_layout(57));
    for (int r = l->first; r < l->last; r++) {
        const char *t = lt_ext_row_text(r);
        if (r != ui_settings_photo_row() && t && strcmp(t, text) == 0) {
            return r;
        }
    }
    return -1;
}

/* the value beside a shown label (its row follows the label's), NULL when
   the line is hidden */
static const char *statValue(int strId)
{
    const int r = statLabel(ui_str((UiStrId)strId));
    if (r < 0 || lt_ext_prop(r)->masked || lt_ext_prop(r + 1)->masked) {
        return NULL;
    }
    return lt_ext_row_text(r + 1);
}

/* the shown stats rows (labels and values), and whether every one sits in
   the panel's place */
static int statsShown(int *placed)
{
    const LtProp *l = lt_ext_layout(ui_settings_entry_layout(57));
    int n = 0;
    *placed = 1;
    for (int r = ui_settings_photo_row() + 1; r < l->last; r++) {
        const LtProperty *e = lt_ext_prop(r);
        if (e->masked) {
            continue;
        }
        n++;
        /* y units are half field lines */
        if (e->dispX < 290 || e->dispX + e->dispW > 620 || e->dispY < 38 ||
            e->dispY + e->dispH / 2 > 198) {
            printf("  stats row %d at x %d w %d, y %d h %d\n", r, e->dispX, e->dispW, e->dispY,
                   e->dispH);
            *placed = 0;
        }
    }
    return n;
}

static void testPauseStats(void)
{
    useConfig("version = 1\n");
    fakeTables();
    lt_ext_reset();
    ui_settings_reset();
    ico_photo_reset();
    memset(pad, 0, sizeof(pad));
    pad[0].ana[0] = pad[0].ana[1] = pad[0].ana[2] = pad[0].ana[3] = 128;
    gFlagGameClear = 0;
    NonLinearCameraMove = 2;
    ico_gs_reset();
    ico_gs_set_sampler(gsSampler);
    memset(&s_gs, 0, sizeof(s_gs));
    s_gs.valid = 1;
    s_gs.stage_no = 11;
    s_gs.system_status[0] = 1; /* PAL: 50 frames a second of play time */
    s_gs.system_status[1] = 2;
    s_gs.layout = 54;
    s_gs.mc_preview[2] = (12 * 60 + 34) * 50 + 49;
    ico_gs_tick();
    for (int i = 0; i < 3; i++) {
        ico_gs_signal(ICO_GS_EV_GAME_OVER, 0);
    }
    ico_gs_signal(ICO_GS_EV_YORDA_GRABBED, 0);
    for (int i = 0; i < 5; i++) {
        ico_gs_signal(ICO_GS_EV_ENEMY_KILLED, i);
    }
    ico_gs_tick();
    s_gs.layout = 41; /* "File saved." */
    ico_gs_tick();
    s_gs.layout = 57;
    ico_gs_tick();
    CHECK(ico_gs_run_game_overs() == 3 && ico_gs_run_captures() == 1 && ico_gs_run_enemies() == 5 &&
              ico_gs_run_saves() == 1,
          "the run: %u game overs, %u captures, %u enemies, %u saves", ico_gs_run_game_overs(),
          ico_gs_run_captures(), ico_gs_run_enemies(), ico_gs_run_saves());

    stage_no = 1; /* the title's stage */
    init_layout_texture(2);
    const int ph = ui_settings_photo_row();
    const LtProp *el = lt_ext_layout(ui_settings_entry_layout(57));
    CHECK(el->first == ph && el->last > ph + 2 * 13, "the lines after Photo mode (%d .. %d)",
          el->first, el->last);
    for (int r = ph + 1; r < el->last; r++) {
        CHECK(lt_ext_prop(r)->defaultMask && !lt_ext_prop(r)->selectable,
              "row %d hidden by default, not selectable", r);
    }
    settle(54, 4);
    lt_switch_layout(57);
    CHECK(settle(57, 40), "the pause menu");
    int placed;
    CHECK(statsShown(&placed) == 0, "the title's stage: no line shown");
    CHECK(statValue(UI_STR_STATS_DEATHS) == NULL, "the title's stage: Deaths hidden");

    stage_no = 11; /* st04a: a stage runs (Main Gate on the save screen) */
    frame(0);
    const int shown = statsShown(&placed);
    CHECK(shown > 0 && placed, "a stage: %d rows shown, all in the panel's place", shown);
    const char *v;
    v = statValue(UI_STR_STATS_PLAY_TIME);
    CHECK(v && strcmp(v, "00:12:34") == 0, "Play time \"%s\"", v ? v : "(hidden)");
    v = statValue(UI_STR_STATS_DEATHS);
    CHECK(v && strcmp(v, "3") == 0, "Deaths \"%s\"", v ? v : "(hidden)");
    v = statValue(UI_STR_STATS_CAPTURES);
    CHECK(v && strcmp(v, "1") == 0, "Yorda captured \"%s\"", v ? v : "(hidden)");
    v = statValue(UI_STR_STATS_SAVES);
    CHECK(v && strcmp(v, "1") == 0, "Saves \"%s\"", v ? v : "(hidden)");
    v = statValue(UI_STR_STATS_ENEMIES);
    CHECK(v && strcmp(v, "5") == 0, "Enemies \"%s\"", v ? v : "(hidden)");
    v = statValue(UI_STR_OPT_NEWGAME_PLUS);
    CHECK(v && strcmp(v, "Off") == 0, "New Game+ \"%s\"", v ? v : "(hidden)");
    v = statValue(UI_STR_OPT_MIRROR);
    CHECK(v && strcmp(v, "Off") == 0, "Mirror mode \"%s\"", v ? v : "(hidden)");
    char want[32];
    snprintf(want, sizeof(want), "0 / %d", ico_ach_count());
    v = statValue(UI_STR_SECTION_ACHIEVEMENTS);
    CHECK(v && strcmp(v, want) == 0, "Achievements \"%s\" (want \"%s\")", v ? v : "(hidden)", want);
    v = statValue(UI_STR_STATS_AREA);
    CHECK(v && strcmp(v, ui_str(UI_STR_MT_LOC_MAIN_GATE)) == 0, "Area \"%s\"", v ? v : "(hidden)");
    CHECK(statValue(UI_STR_STATS_ASSISTS) == NULL, "no assist on: no Assists line");
    /* the lines packed from the top, a pitch apart */
    const int pt = statLabel(ui_str(UI_STR_STATS_PLAY_TIME)),
              de = statLabel(ui_str(UI_STR_STATS_DEATHS)),
              ar = statLabel(ui_str(UI_STR_STATS_AREA)),
              ac = statLabel(ui_str(UI_STR_SECTION_ACHIEVEMENTS));
    CHECK(lt_ext_prop(pt)->dispX == 300 &&
              lt_ext_prop(pt + 1)->dispX + lt_ext_prop(pt + 1)->dispW <= 610,
          "from x 300 to 610");
    CHECK(lt_ext_prop(de)->dispY > lt_ext_prop(pt)->dispY &&
              lt_ext_prop(ar)->dispY ==
                  lt_ext_prop(ac)->dispY + (lt_ext_prop(de)->dispY - lt_ext_prop(pt)->dispY),
          "Area right under Achievements when no assist is on");
    /* the Photo mode row's box ends before the lines; the game's rows
       where they were */
    CHECK(lt_ext_prop(ph)->dispX + lt_ext_prop(ph)->dispW < 300, "Photo mode's box ends at x %d",
          lt_ext_prop(ph)->dispX + lt_ext_prop(ph)->dispW);
    CHECK(texProperty[294].dispX == 40 && texProperty[294].dispY == 50 &&
              texProperty[295].dispY == 90 && texProperty[296].dispY == 120,
          "the pause rows at 50, 90, 120 (%d, %d, %d)", texProperty[294].dispY,
          texProperty[295].dispY, texProperty[296].dispY);

    /* the game moves on: the values follow each frame */
    s_gs.mc_preview[2] = 100 * 3600 * 50;
    ico_gs_signal(ICO_GS_EV_GAME_OVER, 0);
    ico_gs_tick();
    gFlagGameClear = 1;
    ico_opt_set_mirror(1);
    frame(0);
    v = statValue(UI_STR_STATS_PLAY_TIME);
    CHECK(v && strcmp(v, "99:59:59") == 0, "Play time clamped as the save screen's: \"%s\"",
          v ? v : "(hidden)");
    v = statValue(UI_STR_STATS_DEATHS);
    CHECK(v && strcmp(v, "4") == 0, "Deaths \"%s\"", v ? v : "(hidden)");
    v = statValue(UI_STR_OPT_NEWGAME_PLUS);
    CHECK(v && strcmp(v, "On") == 0, "New Game+ \"%s\"", v ? v : "(hidden)");
    v = statValue(UI_STR_OPT_MIRROR);
    CHECK(v && strcmp(v, "On") == 0, "Mirror mode \"%s\"", v ? v : "(hidden)");

    /* the assists: the line and one line for each on, nothing for those off */
    ico_opt_set_yorda_safe(1);
    ico_opt_set_developer_mode(1);
    frame(0);
    const int as = statLabel(ui_str(UI_STR_STATS_ASSISTS));
    CHECK(as >= 0 && !lt_ext_prop(as)->masked, "an assist on: the Assists line");
    if (as >= 0) {
        CHECK(!lt_ext_prop(as + 3)->masked &&
                  strcmp(lt_ext_row_text(as + 3), ui_str(UI_STR_OPT_YORDA)) == 0 &&
                  !lt_ext_prop(as + 5)->masked &&
                  strcmp(lt_ext_row_text(as + 5), ui_str(UI_STR_OPT_DEVELOPER_MODE)) == 0 &&
                  lt_ext_prop(as + 7)->masked,
              "the two on, one a line (\"%s\", \"%s\")", lt_ext_row_text(as + 3),
              lt_ext_row_text(as + 5));
    }
    statsShown(&placed);
    CHECK(placed, "with the assists, every line between the bars");
    ico_opt_set_stick_fix(1);
    frame(0);
    CHECK(statsShown(&placed) == 2 * 13 + 1 && placed,
          "every line (the three assists and the area): still between the bars");
    /* each language: a label and its value never overlap once the label is
       set to fit its box (layout_ext.c: down to 60 %) */
    for (int g = 2; g <= 6; g++) {
        NonLinearCameraMove = g;
        frame(0);
        for (int r = ph + 2; r + 1 < el->last; r += 2) {
            const LtProperty *lab = lt_ext_prop(r), *val = lt_ext_prop(r + 1);
            if (lab->masked) {
                continue;
            }
            const float lw = ui_measure_text(lt_ext_row_size(r), lt_ext_row_text(r));
            const float vw = ui_measure_text(lt_ext_row_size(r + 1), lt_ext_row_text(r + 1));
            /* each as drawn: its natural width, or its box's when set to fit */
            const int blank = strcmp(lt_ext_row_text(r), " ") == 0;
            const float lDrawn = blank ? 0.0f : lw < (float)lab->dispW ? lw : (float)lab->dispW;
            const float vDrawn = vw < (float)val->dispW ? vw : (float)val->dispW;
            CHECK((blank || lw * 0.6f <= (float)lab->dispW) && vw * 0.6f <= (float)val->dispW &&
                      lDrawn + vDrawn <= (float)val->dispW,
                  "language %d: \"%s\" (%.0f in %d) and \"%s\" (%.0f in %d)", g, lt_ext_row_text(r),
                  (double)lw, lab->dispW, lt_ext_row_text(r + 1), (double)vw, val->dispW);
        }
    }
    NonLinearCameraMove = 2;
    ico_opt_set_yorda_safe(0);
    ico_opt_set_developer_mode(0);
    ico_opt_set_stick_fix(0);
    ico_opt_set_mirror(0);
    gFlagGameClear = 0;
    frame(0);
    CHECK(statValue(UI_STR_STATS_ASSISTS) == NULL && lt_ext_prop(as + 3)->masked,
          "the assists off again: hidden");

    /* a stage the save screen has no name for: no Area line */
    stage_no = 39;
    frame(0);
    CHECK(statValue(UI_STR_STATS_AREA) == NULL && statValue(UI_STR_STATS_DEATHS) != NULL,
          "no name for the beach: the Area line left out");
    stage_no = 11;
    /* a run from a save made before v0.4.0: Saves and Enemies hidden (their
       counts would start at the load), the other lines stay */
    {
        IcoGsRun r;
        ico_gs_run_get(&r);
        r.partial = 1;
        ico_gs_run_set(&r);
        frame(0);
        CHECK(statValue(UI_STR_STATS_SAVES) == NULL && statValue(UI_STR_STATS_ENEMIES) == NULL &&
                  statValue(UI_STR_STATS_DEATHS) != NULL &&
                  statValue(UI_STR_STATS_CAPTURES) != NULL,
              "a partial run: Saves and Enemies hidden, Deaths and Yorda captured shown");
        r.partial = 0;
        ico_gs_run_set(&r);
        frame(0);
        CHECK(statValue(UI_STR_STATS_SAVES) != NULL && statValue(UI_STR_STATS_ENEMIES) != NULL,
              "a whole run: both shown again");
    }
    /* photo mode: hidden */
    ico_photo_enter();
    frame(0);
    CHECK(statsShown(&placed) == 0, "photo mode: hidden");
    ico_photo_exit();
    frame(0);
    CHECK(statsShown(&placed) > 0, "back from photo mode: shown");
    /* back on the title's stage: hidden again */
    stage_no = 1;
    frame(0);
    CHECK(statsShown(&placed) == 0, "the title's stage again: hidden");
    stage_no = 0;
    ico_gs_set_sampler(NULL);
    ico_gs_reset();
}

/* the New Game screen, run by the real layout code: two rows, Mirror
 * mode and New Game+.  The cursor starts on Mirror mode's Off, Right moves
 * to On, Down to New Game+'s choice and Up back to Mirror mode's; each row
 * keeps its own choice, the other row's choice stays lit, and the note
 * follows the cursor's row.  New Game+ starts on when the game was
 * finished (gFlagGameClear set: a finished save's new game), off
 * otherwise; Cross or START writes both choices (the run's mirror mode,
 * gFlagGameClear) and starts the game once; Triangle goes back to the
 * vibration screen (layout 9). */
static void testNewGameScreen(void)
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
    int ml = ui_new_game_screen_enter();
    const int mOff = ui_new_game_screen_row(0, 0), mOn = ui_new_game_screen_row(0, 1);
    const int nOff = ui_new_game_screen_row(1, 0), nOn = ui_new_game_screen_row(1, 1);
    const int note = lt_ext_layout(ml)->last - 1;
    CHECK(ml >= 0, "the screen is there once installed");
    lt_switch_layout(ml);
    CHECK(settle(ml, 60), "the New Game screen (%d)", current_layout_id);
    LtProp *l = lt_ext_layout(ml);
    CHECK(l->curItem == mOff, "the cursor on Mirror mode's Off");
    CHECK(lt_ext_prop(nOff)->ownerItem == mOff && lt_ext_prop(nOn)->ownerItem == -1,
          "New Game+ Off lit (a plain New Game)");
    CHECK(strncmp(lt_ext_row_text(note), "Plays the game flipped", 22) == 0,
          "the note explains Mirror mode (%s)", lt_ext_row_text(note));
    press(0x2000); /* right */
    CHECK(l->curItem == mOn, "right: Mirror mode On");
    CHECK(lt_ext_prop(nOff)->ownerItem == mOn, "New Game+ Off still lit");
    press(0x4000); /* down */
    CHECK(l->curItem == nOff, "down: New Game+'s choice, Off");
    CHECK(lt_ext_prop(mOn)->ownerItem == nOff && lt_ext_prop(mOff)->ownerItem == -1,
          "Mirror mode On stays lit");
    CHECK(lt_ext_prop(nOff)->ownerItem == -1 && lt_ext_prop(nOn)->ownerItem == -1,
          "the cursor's row: the cursor alone");
    CHECK(strncmp(lt_ext_row_text(note), "New Game+ plays", 15) == 0,
          "the note explains New Game+ (%s)", lt_ext_row_text(note));
    press(0x2000);
    CHECK(l->curItem == nOn, "right: New Game+ On");
    press(0x1000); /* up */
    CHECK(l->curItem == mOn, "up: Mirror mode's choice, On, kept");
    CHECK(lt_ext_prop(nOn)->ownerItem == mOn, "New Game+ On lit");
    press(0x4000);
    CHECK(l->curItem == nOn, "down again: New Game+ On, kept");
    press(0x4000); /* the rows in a loop */
    CHECK(l->curItem == mOn, "down from the last row: Mirror mode's choice");
    int games = s_newGames;
    ico_opt_set_mirror(0);
    press(0x40); /* Cross */
    CHECK(ico_opt_mirror() == 1, "Cross: the run is mirrored");
    CHECK(gFlagGameClear == 1, "Cross: New Game+ On sets the cleared flag");
    CHECK(s_newGames == games + 1, "the game starts (gflagOn(382))");
    press(0x40);
    press(0x800);
    CHECK(s_newGames == games + 1, "once");

    /* from a finished save (gFlagGameClear set): New Game+ starts On, and
       Off chosen there clears the flag; START confirms */
    lt_switch_layout(54);
    settle(54, 60);
    gFlagGameClear = 1;
    ml = ui_new_game_screen_enter();
    lt_switch_layout(ml);
    CHECK(settle(ml, 60), "the New Game screen again");
    CHECK(l->curItem == mOff, "the cursor on Mirror mode's Off again");
    CHECK(lt_ext_prop(nOn)->ownerItem == mOff && lt_ext_prop(nOff)->ownerItem == -1,
          "New Game+ On lit (a finished save)");
    press(0x4000);
    CHECK(l->curItem == nOn, "down: New Game+ On");
    press(0x8000); /* left */
    CHECK(l->curItem == nOff, "left: New Game+ Off");
    press(0x1000);
    CHECK(l->curItem == mOff, "up: Mirror mode Off, kept");
    press(0x800); /* START */
    CHECK(ico_opt_mirror() == 0 && s_newGames == games + 2, "START: not mirrored, the game starts");
    CHECK(gFlagGameClear == 0, "New Game+ Off: the first journey even after finishing");

    /* a plain New Game with New Game+ chosen On */
    lt_switch_layout(54);
    settle(54, 60);
    gFlagGameClear = 0;
    ml = ui_new_game_screen_enter();
    lt_switch_layout(ml);
    CHECK(settle(ml, 60), "the New Game screen a third time");
    press(0x4000);
    CHECK(l->curItem == nOff, "a plain New Game: New Game+ Off");
    press(0x2000);
    press(0x40);
    CHECK(gFlagGameClear == 1 && ico_opt_mirror() == 0 && s_newGames == games + 3,
          "New Game+ On from a plain New Game");

    /* Triangle: the vibration screen, the flag as it was */
    lt_switch_layout(54);
    settle(54, 60);
    gFlagGameClear = 1;
    ml = ui_new_game_screen_enter();
    lt_switch_layout(ml);
    CHECK(settle(ml, 60), "the New Game screen a fourth time");
    press(0x4000);
    press(0x8000);
    press(0x10);
    CHECK(settle(9, 60), "Triangle: the vibration screen (%d)", current_layout_id);
    CHECK(s_newGames == games + 3, "no game started");
    CHECK(gFlagGameClear == 1, "Triangle: the cleared flag untouched");
    gFlagGameClear = 0;

    /* not built: -1 (la_vibe_select then starts the game itself) */
    ui_settings_reset();
    CHECK(ui_new_game_screen_enter() == -1 && ui_new_game_screen_layout() == -1, "not built: -1");
}

/* the title's "Quit to desktop" row under Settings (layouts 12 and 13,
 * in the entry layout), its confirmation screen run by the real layout
 * code: the cursor starts on No; Cross on No, Triangle and Circle return
 * to the title with the cursor on the row and the title's own default
 * restored after; Cross on Yes saves what is pending and calls the quit
 * handler once. */
static int s_quits;

static void countQuit(void)
{
    s_quits++;
}

static void testQuit(void)
{
    char p[1100];
    useConfig("version = 1\n");
    fakeTables();
    lt_ext_reset();
    ui_settings_reset();
    memset(pad, 0, sizeof(pad));
    pad[0].ana[0] = pad[0].ana[1] = pad[0].ana[2] = pad[0].ana[3] = 128;
    NonLinearCameraMove = 2;
    s_quits = 0;
    ui_settings_set_quit_handler(countQuit);
    init_layout_texture(2);
    settle(54, 4);
    int s12 = ui_settings_entry_row(12), s13 = ui_settings_entry_row(13);
    int q12 = ui_settings_quit_row(12), q13 = ui_settings_quit_row(13);
    int ql = ui_quit_screen_layout(), yes = ui_quit_screen_row(1), no = ui_quit_screen_row(0);
    CHECK(q12 == s12 + 1 && q13 == s13 + 1, "the quit rows follow the Settings rows (%d %d)", q12,
          q13);
    CHECK(ui_settings_quit_row(57) == -1, "no quit row in the pause menu");
    CHECK(lt_ext_prop(s13)->downItem == q13 && lt_ext_prop(q13)->upItem == s13 &&
              lt_ext_prop(q13)->downItem == -1 && lt_ext_prop(q13)->left == -1,
          "Settings <-> Quit");
    CHECK(lt_ext_prop(q13)->dispY > lt_ext_prop(s13)->dispY &&
              lt_ext_prop(s13)->dispY > texProperty[51].dispY,
          "below Settings, which is below New Game");
    CHECK(ql >= LT_GAME_LAYOUT_COUNT && lt_ext_prop(q13)->right == ql &&
              lt_ext_prop(q12)->right == ql,
          "Cross: the confirmation");
    CHECK(ui_settings_entry_item(q12) && ui_settings_entry_item(q13),
          "entry items (no game start)");
    LtProp *el = lt_ext_layout(ui_settings_entry_layout(13));
    CHECK(el->first == s13 && el->last == q13 + 1, "one entry layout, two rows");
    CHECK(strcmp(lt_ext_row_text(q13), "Quit to desktop") == 0, "the label: %s",
          lt_ext_row_text(q13));
    static const char *const kQuit[5] = {"Quit to desktop?", "Quitter vers le bureau ?",
                                         "Zum Desktop beenden?", "Uscire al desktop?",
                                         "\xC2\xBFSalir al escritorio?"};
    static const UiLang kLangs[5] = {UI_LANG_EN, UI_LANG_FR, UI_LANG_DE, UI_LANG_IT, UI_LANG_ES};
    for (int i = 0; i < 5; i++) {
        CHECK(strcmp(ui_str_in(kLangs[i], UI_STR_QUIT_CONFIRM), kQuit[i]) == 0 &&
                  ui_str_in(kLangs[i], UI_STR_QUIT_DESKTOP)[0] != '\0',
              "the question in language %d: %s", i, ui_str_in(kLangs[i], UI_STR_QUIT_CONFIRM));
    }
    ui_settings_title_mask(1);
    CHECK(lt_ext_prop(q12)->masked && lt_ext_prop(q13)->masked && lt_ext_prop(s13)->masked,
          "masked with the title's rows");
    ui_settings_title_mask(0);

    lt_switch_layout(13);
    CHECK(settle(13, 60), "the title");
    CHECK(texLayout[13].curItem == 51, "on New Game");
    press(0x4000);
    CHECK(texLayout[13].curItem == s13, "down: Settings");
    press(0x4000);
    CHECK(texLayout[13].curItem == q13, "down: Quit to desktop");
    press(0x40);
    CHECK(settle(ql, 60), "Cross: the confirmation (%d)", current_layout_id);
    CHECK(lt_ext_layout(ql)->curItem == no, "the cursor on No");
    press(0x40); /* Cross on No */
    CHECK(settle(13, 60), "No: the title");
    CHECK(texLayout[13].curItem == q13, "the cursor on the quit row");
    frame(0);
    CHECK(texLayout[13].defaultItem == 51, "the title's own default again (%d)",
          texLayout[13].defaultItem);
    static const int kBack[2] = {0x10, 0x20};
    for (int i = 0; i < 2; i++) {
        press(0x40);
        CHECK(settle(ql, 60), "the confirmation again");
        int neg = s_sounds[2];
        press(kBack[i]);
        CHECK(settle(13, 60), "%s: the title", i ? "Circle" : "Triangle");
        CHECK(texLayout[13].curItem == q13 && s_sounds[2] > neg, "on the row, the cancel sound");
    }
    CHECK(s_quits == 0, "no quit yet");
    /* Yes: a pending change is written, the handler runs once */
    press(0x40);
    CHECK(settle(ql, 60), "the confirmation a fourth time");
    CHECK(lt_ext_layout(ql)->curItem == no, "on No again");
    press(0x8000);
    CHECK(lt_ext_layout(ql)->curItem == yes, "left: Yes");
    ui_settings_step(UI_OPT_STICK_FIX, 1);
    press(0x40);
    CHECK(s_quits == 1, "Cross on Yes: the quit handler");
    press(0x40);
    press(0x20);
    CHECK(s_quits == 1 && current_layout_id == ql, "once, and the screen stays");
    path(p, sizeof(p), "settings_test.toml");
    IcoToml *t = ico_toml_load(p);
    CHECK(t != NULL && ico_toml_get_bool(t, "gameplay.stick_fix", 0) == 1,
          "the pending change written before the quit");
    if (t) {
        ico_toml_free(t);
    }
    ico_opt_set_stick_fix(0);
    ui_settings_set_quit_handler(NULL);
}

/* Circle leaves every port screen as Triangle does, even with the game
 * menus' alias off ([game] circle_back = false): the Settings pages, the
 * two lists, the menu itself (to the pause menu) and the New Game screen
 * (the quit screen: testQuit). */
static void testCirclePortScreens(void)
{
    useConfig("version = 1\n[game]\ncircle_back = false\n");
    fakeTables();
    lt_ext_reset();
    ui_settings_reset();
    memset(pad, 0, sizeof(pad));
    pad[0].ana[0] = pad[0].ana[1] = pad[0].ana[2] = pad[0].ana[3] = 128;
    NonLinearCameraMove = 2;
    gFlagGameClear = 0;
    init_layout_texture(2);
    settle(54, 4);
    CHECK(lt_ext_back_buttons() == 0x10, "the game menus' alias off");
    int mainL = ui_settings_page_layout(UI_PAGE_MAIN);
    pauseToMain();

    static const struct {
        int row; /* main page row */
        UiSettingsPage page;
    } kPages[] = {{0, UI_PAGE_DISPLAY},  {1, UI_PAGE_EFFECTS},  {3, UI_PAGE_AUDIO},
                  {4, UI_PAGE_CONTROLS}, {5, UI_PAGE_GAMEPLAY}, {7, UI_PAGE_ACHIEVEMENTS}};

    int labels[16];
    ui_settings_page_rows(UI_PAGE_MAIN, labels, NULL, NULL, 16);
    for (unsigned i = 0; i < sizeof(kPages) / sizeof(kPages[0]); i++) {
        lt_ext_layout(mainL)->curItem = labels[kPages[i].row];
        press(0x40);
        CHECK(settle(ui_settings_page_layout(kPages[i].page), 60), "page %d opens", kPages[i].page);
        int leaves = s_leaves;
        press(0x20);
        CHECK(settle(mainL, 60) && s_leaves == leaves + 1, "Circle: page %d back to the menu",
              kPages[i].page);
        CHECK(lt_ext_layout(mainL)->curItem == labels[kPages[i].row], "on its row");
    }
    /* Controls -> Remap -> Circle -> Controls */
    lt_ext_layout(mainL)->curItem = labels[4];
    press(0x40);
    int ctlL = ui_settings_page_layout(UI_PAGE_CONTROLS);
    CHECK(settle(ctlL, 60), "Controls");
    press(0x40);
    CHECK(settle(ui_settings_page_layout(UI_PAGE_REMAP), 60), "Remap");
    press(0x20);
    CHECK(settle(ctlL, 60), "Circle: Remap back to Controls");
    press(0x20);
    CHECK(settle(mainL, 60), "Circle: Controls back to the menu");
    press(0x20);
    CHECK(settle(57, 60), "Circle: the menu back to the pause menu");
    CHECK(texLayout[57].curItem == 294, "on Options");
    /* the New Game screen: Circle is Triangle there (the vibration screen) */
    lt_switch_layout(54);
    settle(54, 60);
    int ml = ui_new_game_screen_enter(), games = s_newGames;
    lt_switch_layout(ml);
    CHECK(settle(ml, 60), "the New Game screen");
    press(0x20);
    CHECK(settle(9, 60), "Circle: the vibration screen (%d)", current_layout_id);
    CHECK(s_newGames == games, "no game started");
}

/* the game's own menus through the real layout_texture.c: the Options
 * screen's rows (no longer reached, its links the PAL data's) go back
 * to the pause menu (57) through their left link, which
 * default_item_select follows on Triangle, and on Circle while [game]
 * circle_back is on (the default); off, Circle does nothing there and
 * Triangle still goes back. */
static void testCircleGameMenu(void)
{
    for (int on = 1; on >= 0; on--) {
        useConfig(on ? "version = 1\n" : "version = 1\n[game]\ncircle_back = false\n");
        fakeTables();
        lt_ext_reset();
        ui_settings_reset();
        memset(pad, 0, sizeof(pad));
        pad[0].ana[0] = pad[0].ana[1] = pad[0].ana[2] = pad[0].ana[3] = 128;
        gFlagGameClear = 0;
        init_layout_texture(2);
        settle(54, 4);
        CHECK(lt_ext_back_buttons() == (on ? 0x30 : 0x10), "circle_back %d: the bits", on);
        static const int kRows[2] = {308, 323}; /* two game rows */
        for (int r = 0; r < 2; r++) {
            int row = kRows[r];
            lt_switch_layout(58);
            CHECK(settle(58, 60), "Options");
            texLayout[58].curItem = row;
            press(0x20);
            if (on) {
                CHECK(settle(57, 60), "circle_back on: Circle on %d goes back to the pause menu",
                      row);
            } else {
                for (int i = 0; i < 30; i++) {
                    frame(0);
                }
                CHECK(current_layout_id == 58 && texLayout[58].curItem == row,
                      "circle_back off: Circle on %d does nothing (%d)", row, current_layout_id);
                press(0x10);
                CHECK(settle(57, 60), "circle_back off: Triangle on %d still goes back", row);
            }
        }
    }
}

static void testValues(void)
{
    char p[1100];
    useConfig("version = 1\n");
    fakeTables();
    lt_ext_reset();
    ui_settings_reset();
    ui_set_language(UI_LANG_EN);
    ui_settings_install();
    IcoVideoOptions o;

    ui_settings_step(UI_OPT_PRESET, 1);
    ico_video_get(&o);
    CHECK(ico_video_preset(&o) == ICO_VIDEO_ENHANCED &&
              strstr(ui_settings_value_text(UI_OPT_PRESET), "Enhanced"),
          "preset: Enhanced");
    CHECK(strstr(ui_settings_value_text(UI_OPT_RESOLUTION), "Window") != NULL,
          "resolution: Window");
    ui_settings_step(UI_OPT_RESOLUTION, 1);
    ui_settings_step(UI_OPT_RESOLUTION, 1);
    ico_video_get(&o);
    CHECK(o.resScale == 2 && strstr(ui_settings_value_text(UI_OPT_RESOLUTION), "2x"),
          "resolution: 2x (%s)", ui_settings_value_text(UI_OPT_RESOLUTION));
    ui_settings_step(UI_OPT_RESOLUTION, -1);
    ui_settings_step(UI_OPT_RESOLUTION, -1);
    ui_settings_step(UI_OPT_RESOLUTION, -1);
    ico_video_get(&o);
    /* Auto after 4x (Left from Window wraps to it) */
    CHECK(o.resScale == ICO_RES_AUTO &&
              strcmp(ui_settings_value_text(UI_OPT_RESOLUTION), "Auto") == 0,
          "resolution wraps to Auto (%s)", ui_settings_value_text(UI_OPT_RESOLUTION));
    ico_video_set_auto_scale(2);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_RESOLUTION), "Auto (2x)") == 0,
          "resolution: Auto lowered to 2x (%s)", ui_settings_value_text(UI_OPT_RESOLUTION));
    ico_video_set_auto_scale(0);
    ui_settings_step(UI_OPT_RESOLUTION, -1);
    ico_video_get(&o);
    CHECK(o.resScale == 4, "resolution: Left from Auto is 4x");
    /* Enhanced's aspect is Auto: Right wraps to 4:3, then 16:10, 16:9, 21:9, 32:9 */
    ui_settings_step(UI_OPT_ASPECT, 1);
    ui_settings_step(UI_OPT_ASPECT, 1);
    ui_settings_step(UI_OPT_ASPECT, 1);
    ico_video_get(&o);
    CHECK(o.aspect == ICO_ASPECT_16_9 && strstr(ui_settings_value_text(UI_OPT_ASPECT), "16:9"),
          "aspect 16:9");
    ui_settings_step(UI_OPT_ASPECT, 1);
    ico_video_get(&o);
    CHECK(o.aspect == ICO_ASPECT_21_9 && strstr(ui_settings_value_text(UI_OPT_ASPECT), "21:9"),
          "aspect 21:9");
    ui_settings_step(UI_OPT_ASPECT, 1);
    ico_video_get(&o);
    CHECK(o.aspect == ICO_ASPECT_32_9 && strstr(ui_settings_value_text(UI_OPT_ASPECT), "32:9"),
          "aspect 32:9");
    ui_settings_step(UI_OPT_ASPECT, 1);
    CHECK(strstr(ui_settings_value_text(UI_OPT_ASPECT), "Auto") != NULL, "aspect Auto");
    /* Enhanced's filter is anisotropic, its height full */
    ui_settings_step(UI_OPT_FILTER, -1);
    ico_video_get(&o);
    CHECK(o.filter == ICO_FILTER_TRILINEAR, "filter steps back to trilinear");
    ui_settings_step(UI_OPT_FILTER, -1);
    ui_settings_step(UI_OPT_FILTER, -1);
    ico_video_get(&o);
    CHECK(o.filter == ICO_FILTER_ANISOTROPIC, "filter wraps to anisotropic");
    ui_settings_step(UI_OPT_WINDOW_MODE, 1);
    ui_settings_step(UI_OPT_WINDOW_MODE, 1);
    ui_settings_step(UI_OPT_VSYNC, 1);
    ui_settings_step(UI_OPT_FULL_HEIGHT, 1);
    ico_video_get(&o);
    CHECK(o.windowMode == ICO_WINDOW_FULLSCREEN && o.vsync == 0 && o.fullHeight == 0,
          "the toggles");
    CHECK(strstr(ui_settings_value_text(UI_OPT_VSYNC), "Off") != NULL, "vsync Off");

    /* the CRT filter row cycles Off, Scanlines, Consumer TV,
       Trinitron, PVM, Shadow mask and around, setting [video] crt and
       crt_mode together; the strength steps in tens, clamped at 0 and 100 % */
    {
        static const char *const names[7] = {"Off", "Scanlines",   "Consumer TV", "Trinitron",
                                             "PVM", "Shadow mask", "Off"};
        static const int modes[7] = {
            -1, ICO_CRT_SCANLINES, ICO_CRT_CONSUMER, ICO_CRT_TRINITRON, ICO_CRT_PVM, ICO_CRT_SHADOW,
            -1};
        for (int i = 0; i < 7; i++) {
            ico_video_get(&o);
            CHECK(strcmp(ui_settings_value_text(UI_OPT_CRT), names[i]) == 0 &&
                      o.crt == (modes[i] >= 0) && (modes[i] < 0 || o.crtMode == modes[i]),
                  "crt row %d: \"%s\" (crt %d mode %d)", i, ui_settings_value_text(UI_OPT_CRT),
                  o.crt, o.crtMode);
            ui_settings_step(UI_OPT_CRT, 1);
        }
        ui_settings_step(UI_OPT_CRT, -1); /* back from Scanlines to Off */
        ui_settings_step(UI_OPT_CRT, -1); /* around to Shadow mask */
        ico_video_get(&o);
        CHECK(o.crt == 1 && o.crtMode == ICO_CRT_SHADOW &&
                  strcmp(ui_settings_value_text(UI_OPT_CRT), "Shadow mask") == 0,
              "crt row: Left wraps to Shadow mask");
        ui_settings_step(UI_OPT_CRT, -1); /* to PVM */
        ico_video_get(&o);
        CHECK(o.crt == 1 && o.crtMode == ICO_CRT_PVM &&
                  strcmp(ui_settings_value_text(UI_OPT_CRT), "PVM") == 0,
              "crt row: Left again to PVM");
        CHECK(strcmp(ui_settings_value_text(UI_OPT_CRT_STRENGTH), "100 %") == 0,
              "crt strength 100 %% (%s)", ui_settings_value_text(UI_OPT_CRT_STRENGTH));
        ui_settings_step(UI_OPT_CRT_STRENGTH, 1);
        ui_settings_step(UI_OPT_CRT_STRENGTH, -1);
        ui_settings_step(UI_OPT_CRT_STRENGTH, -1);
        ico_video_get(&o);
        CHECK(strcmp(ui_settings_value_text(UI_OPT_CRT_STRENGTH), "80 %") == 0 &&
                  o.crtStrength > 0.79f && o.crtStrength < 0.81f,
              "crt strength clamps at 100 %%, steps to 80 %% (%s)",
              ui_settings_value_text(UI_OPT_CRT_STRENGTH));
        for (int i = 0; i < 12; i++) {
            ui_settings_step(UI_OPT_CRT_STRENGTH, -1);
        }
        CHECK(strcmp(ui_settings_value_text(UI_OPT_CRT_STRENGTH), "0 %") == 0,
              "crt strength clamps at 0 %%");
        for (int i = 0; i < 7; i++) {
            ui_settings_step(UI_OPT_CRT_STRENGTH, 1);
        }

        /* under the filter the Resolution row reads "1x (CRT)"
           and does not step; the file's 4x is kept and back with it off */
        CHECK(strcmp(ui_settings_value_text(UI_OPT_RESOLUTION), "1x (CRT)") == 0,
              "resolution under the CRT filter: \"%s\"", ui_settings_value_text(UI_OPT_RESOLUTION));
        ui_settings_step(UI_OPT_RESOLUTION, 1);
        ui_settings_step(UI_OPT_RESOLUTION, -1);
        ui_settings_step(UI_OPT_RESOLUTION, -1);
        ico_video_get(&o);
        CHECK(o.resScale == 4 && o.resW == 0, "resolution locked under the CRT filter (%d)",
              o.resScale);
        ui_settings_step(UI_OPT_CRT, 1); /* Shadow mask */
        ui_settings_step(UI_OPT_CRT, 1); /* Off */
        ico_video_get(&o);
        CHECK(o.crt == 0 && strstr(ui_settings_value_text(UI_OPT_RESOLUTION), "4x") != NULL,
              "resolution back to 4x with the filter off (\"%s\")",
              ui_settings_value_text(UI_OPT_RESOLUTION));
        ui_settings_step(UI_OPT_RESOLUTION, 1);
        ico_video_get(&o);
        CHECK(o.resScale == ICO_RES_AUTO, "resolution steps again with the filter off (%d)",
              o.resScale);
        ui_settings_step(UI_OPT_RESOLUTION, -1);
        ui_settings_step(UI_OPT_CRT, -1); /* Shadow mask */
        ui_settings_step(UI_OPT_CRT, -1); /* PVM */
        ico_video_get(&o);
        CHECK(o.crt == 1 && o.crtMode == ICO_CRT_PVM && o.resScale == 4,
              "crt back on, PVM; resolution 4x kept");
    }

    systemStatus[0] = 1;
    int resets = s_resets;
    CHECK(strstr(ui_settings_value_text(UI_OPT_VIDEO_MODE), "PAL 50 Hz") != NULL, "PAL 50 Hz");
    ui_settings_step(UI_OPT_VIDEO_MODE, 1);
    CHECK(systemStatus[0] == 0 && s_resets == resets + 1 &&
              strstr(ui_settings_value_text(UI_OPT_VIDEO_MODE), "60 Hz") != NULL,
          "60 Hz with the boot screen's reset");
    CHECK(strcmp(ico_config_get_string("video.video_mode", ""), "60hz") == 0, "video_mode 60hz");
    ui_settings_step(UI_OPT_VIDEO_MODE, 1);
    CHECK(systemStatus[0] == 1, "back to 50 Hz");

    ui_settings_step(UI_OPT_VOLUME, -1);
    CHECK(strstr(ui_settings_value_text(UI_OPT_VOLUME), "90 %") != NULL, "volume 90 %%");
    ui_settings_step(UI_OPT_STICK_FIX, 1);
    CHECK(ico_opt_stick_fix() == 1 && strstr(ui_settings_value_text(UI_OPT_STICK_FIX), "On"),
          "stick fix on");
    ui_settings_step(UI_OPT_YORDA, 1);
    CHECK(ico_opt_yorda_safe() == 1, "yorda_safe on");
    ui_settings_step(UI_OPT_DEVELOPER, 1);
    CHECK(ico_opt_developer_mode() == 1, "developer mode on");
    ui_settings_step(UI_OPT_MOUSE_SENS, 1);
    CHECK(ico_input_live_bindings()->mouse_sens == 1.25f, "mouse sensitivity 1.25");
    /* Circle goes back, on by default; the step turns the game menus'
       alias off at once and sets the key */
    CHECK(ico_opt_circle_back() == 1 && lt_ext_circle_back() == 1 &&
              strcmp(ui_settings_value_text(UI_OPT_CIRCLE_BACK), "On") == 0 &&
              lt_ext_back_buttons() == 0x30,
          "circle_back: on by default");
    ui_settings_step(UI_OPT_CIRCLE_BACK, 1);
    CHECK(ico_opt_circle_back() == 0 && lt_ext_back_buttons() == 0x10 &&
              strcmp(ui_settings_value_text(UI_OPT_CIRCLE_BACK), "Off") == 0 &&
              ico_config_get_bool("game.circle_back", 1) == 0,
          "circle_back: off (Triangle alone)");
    ui_settings_step(UI_OPT_CIRCLE_BACK, -1);
    CHECK(ico_opt_circle_back() == 1 && lt_ext_back_buttons() == 0x30, "circle_back: on again");
    NonLinearCameraMove = 6;
    ui_settings_step(UI_OPT_LANGUAGE, 1);
    CHECK(NonLinearCameraMove == 2, "language wraps ES -> EN");

    CHECK(ui_settings_save() == 0, "save");
    path(p, sizeof(p), "settings_test.toml");
    IcoToml *t = ico_toml_load(p);
    CHECK(t != NULL, "the file");
    if (t) {
        /* the rows were stepped off Enhanced: Custom, saved as "enhanced" */
        CHECK(strcmp(ico_toml_get(t, "video.preset") ? ico_toml_get(t, "video.preset") : "",
                     "enhanced") == 0,
              "[video] preset");
        CHECK(ico_toml_get_bool(t, "video.fullscreen", 0) == 1, "[video] fullscreen");
        CHECK(ico_toml_get_bool(t, "gameplay.stick_fix", 0) == 1 &&
                  ico_toml_get_bool(t, "gameplay.yorda_safe", 0) == 1 &&
                  ico_toml_get_bool(t, "gameplay.developer_mode", 0) == 1,
              "[gameplay]");
        CHECK(ico_toml_get_float(t, "audio.volume", 0) > 0.89 &&
                  ico_toml_get_float(t, "audio.volume", 0) < 0.91,
              "[audio] volume");
        CHECK(ico_toml_get_float(t, "input.mouse_sensitivity", 0) == 1.25,
              "[input] mouse_sensitivity");
        CHECK(strcmp(ico_toml_get(t, "game.language") ? ico_toml_get(t, "game.language") : "",
                     "en") == 0,
              "[game] language");
        CHECK(strcmp(ico_toml_get(t, "video.video_mode") ? ico_toml_get(t, "video.video_mode") : "",
                     "pal50") == 0,
              "[video] video_mode");
        CHECK(ico_toml_get_bool(t, "video.crt", 0) == 1 &&
                  strcmp(ico_toml_get(t, "video.crt_mode") ? ico_toml_get(t, "video.crt_mode") : "",
                         "pvm") == 0 &&
                  ico_toml_get_float(t, "video.crt_strength", 0) > 0.69 &&
                  ico_toml_get_float(t, "video.crt_strength", 0) < 0.71,
              "[video] crt, crt_mode, crt_strength");
        ico_toml_free(t);
    }
}

/* Settings > Audio: the music and effects gains, the output mode against
   the game's own (the card's, the Options row's), the device list */
/* config.toml's [audio] output as the file holds it ("-" without one) */
static void outputOnDisk(char *out, size_t n)
{
    char p[1024];
    path(p, sizeof(p), "settings_test.toml");
    IcoToml *t = ico_toml_load(p);
    const char *v = t ? ico_toml_get(t, "audio.output") : NULL;
    snprintf(out, n, "%s", v ? v : "-");
    ico_toml_free(t);
}

static void testAudio(void)
{
    char p[1100];
    useConfig("version = 1\n");
    fakeTables();
    lt_ext_reset();
    ui_settings_reset();
    ui_set_language(UI_LANG_EN);
    ico_audio_gain_reset();
    s_outputMode = 0;
    s_outputSets = 0;
    ui_settings_install();
    CHECK(s_outputSets == 0, "auto: install leaves the game's mode alone");
    /* every audio row steps (the steppable range) */
    for (int o = UI_OPT_VOLUME; o <= UI_OPT_DEVICE; o++) {
        int row = ui_settings_row_of(UI_PAGE_AUDIO, (UiSettingsOpt)o);
        int labels[16], values[16];
        int n = ui_settings_page_rows(UI_PAGE_AUDIO, labels, NULL, values, 16);
        int has = 0;
        for (int i = 0; i < n; i++) {
            has |= labels[i] == row && values[i] >= 0;
        }
        CHECK(row >= 0 && has, "audio opt %d has a value box", o);
    }

    /* music and effects: 0 % to 100 % in tens, live */
    CHECK(strcmp(ui_settings_value_text(UI_OPT_MUSIC), "100 %") == 0, "music 100 %% (%s)",
          ui_settings_value_text(UI_OPT_MUSIC));
    CHECK(strcmp(ui_settings_value_text(UI_OPT_EFFECTS), "100 %") == 0, "effects 100 %%");
    ui_settings_step(UI_OPT_MUSIC, -1);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_MUSIC), "90 %") == 0, "music 90 %%");
    CHECK(ico_audio_gain_q12(ICO_AUDIO_CAT_MUSIC) == 3686, "music gain 0.9 live (%d)",
          ico_audio_gain_q12(ICO_AUDIO_CAT_MUSIC));
    CHECK(ico_audio_gain_q12(ICO_AUDIO_CAT_EFFECTS) == 4096, "effects untouched");
    ui_settings_step(UI_OPT_MUSIC, 1);
    ui_settings_step(UI_OPT_MUSIC, 1);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_MUSIC), "100 %") == 0 &&
              ico_audio_gain_q12(ICO_AUDIO_CAT_MUSIC) == 4096,
          "music clamps at 100 %%");
    for (int i = 0; i < 12; i++) {
        ui_settings_step(UI_OPT_EFFECTS, -1);
    }
    CHECK(strcmp(ui_settings_value_text(UI_OPT_EFFECTS), "0 %") == 0 &&
              ico_audio_gain_q12(ICO_AUDIO_CAT_EFFECTS) == 0,
          "effects clamps at 0 %%");
    ui_settings_step(UI_OPT_EFFECTS, 1);
    ui_settings_step(UI_OPT_EFFECTS, 1);
    ui_settings_step(UI_OPT_EFFECTS, 1);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_EFFECTS), "30 %") == 0, "effects 30 %% (%s)",
          ui_settings_value_text(UI_OPT_EFFECTS));

    /* output: Auto shows the game's mode; Stereo and Mono set it and (as
       the Options screen's Sound row did) make it the game's own, so
       Auto keeps the last one chosen */
    CHECK(strcmp(ui_settings_value_text(UI_OPT_OUTPUT), "Auto (Stereo)") == 0, "output Auto (%s)",
          ui_settings_value_text(UI_OPT_OUTPUT));
    s_outputMode = 1; /* the game's Options row: Mono */
    ico_opt_output_toggled(1);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_OUTPUT), "Auto (Mono)") == 0, "Auto (Mono)");
    CHECK(strcmp(ico_config_get_string("audio.output", "auto"), "auto") == 0,
          "auto: the Options row leaves the key");
    char outFile0[32], outFile[32];
    outputOnDisk(outFile0, sizeof(outFile0));
    ui_settings_step(UI_OPT_OUTPUT, 1);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_OUTPUT), "Stereo") == 0 && s_outputMode == 0 &&
              strcmp(ico_config_get_string("audio.output", ""), "stereo") == 0,
          "output Stereo, the game's mode set");
    ui_settings_step(UI_OPT_OUTPUT, 1);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_OUTPUT), "Mono") == 0 && s_outputMode == 1,
          "output Mono");
    outputOnDisk(outFile, sizeof(outFile));
    CHECK(strcmp(outFile, outFile0) == 0,
          "Stereo and Mono steps leave config.toml to the save on leaving (%s, was %s)", outFile,
          outFile0);
    ui_settings_step(UI_OPT_OUTPUT, -1);
    ui_settings_step(UI_OPT_OUTPUT, -1);
    CHECK(ico_opt_output_mode() == ICO_OUTPUT_AUTO && s_outputMode == 0 &&
              strcmp(ico_config_get_string("audio.output", ""), "auto") == 0 &&
              strcmp(ui_settings_value_text(UI_OPT_OUTPUT), "Auto (Stereo)") == 0,
          "Auto again: the game's own mode, the last chosen (Stereo; %s)",
          ui_settings_value_text(UI_OPT_OUTPUT));
    ui_settings_step(UI_OPT_OUTPUT, -1);
    CHECK(ico_opt_output_mode() == ICO_OUTPUT_MONO, "Left from Auto wraps to Mono");
    /* explicit: the card's mode does not win, the Options row's change is
       written back */
    s_outputMode = 0; /* the card's system file: Stereo */
    soundOutputModeSet(ico_opt_output_card(soundOutputModeGet()));
    CHECK(s_outputMode == 1, "mono wins over the card's stereo");
    s_outputMode = 0; /* the Options row: Stereo */
    ico_opt_output_toggled(0);
    CHECK(ico_opt_output_mode() == ICO_OUTPUT_STEREO &&
              strcmp(ico_config_get_string("audio.output", ""), "stereo") == 0,
          "the Options row's Stereo written back");
    path(p, sizeof(p), "settings_test.toml");
    {
        IcoToml *t = ico_toml_load(p);
        CHECK(t != NULL && ico_toml_get(t, "audio.output") != NULL &&
                  strcmp(ico_toml_get(t, "audio.output"), "stereo") == 0,
              "and saved at once");
        ico_toml_free(t);
    }
    /* auto: the card's value is the game's */
    ui_settings_step(UI_OPT_OUTPUT, -1);
    CHECK(ico_opt_output_mode() == ICO_OUTPUT_AUTO, "auto");
    s_outputMode = 1;
    soundOutputModeSet(ico_opt_output_card(soundOutputModeGet()));
    CHECK(s_outputMode == 1, "auto: the card's mono kept");
    /* an explicit key is the game's from install on */
    useConfig("[audio]\noutput = \"mono\"\n");
    s_outputMode = 0;
    ui_settings_install();
    CHECK(s_outputMode == 1, "output = mono at install");

    /* the device: Default, then each device; a long name cut to fit */
    useConfig("version = 1\n");
    s_devCount = 2;
    s_devNames[0] = "Speakers (Realtek High Definition Audio)";
    s_devNames[1] = "A very long name for a USB audio interface with eight outputs and more";
    s_reopens = 0;
    CHECK(strcmp(ui_settings_value_text(UI_OPT_DEVICE), "Default") == 0, "device Default (%s)",
          ui_settings_value_text(UI_OPT_DEVICE));
    ui_settings_step(UI_OPT_DEVICE, 1);
    CHECK(s_reopens == 1 && strcmp(s_reopened, s_devNames[0]) == 0 &&
              strcmp(ico_config_get_string("audio.device", ""), s_devNames[0]) == 0,
          "device: the first, reopened");
    {
        const char *v = ui_settings_value_text(UI_OPT_DEVICE);
        CHECK(ui_measure_menu_text(UI_MENU_TEXT_SIZE * 0.6f, v) <= 176.0f, "the name fits (%s)", v);
    }
    ui_settings_step(UI_OPT_DEVICE, 1);
    {
        const char *v = ui_settings_value_text(UI_OPT_DEVICE);
        size_t n = strlen(v);
        CHECK(n > 3 && strcmp(v + n - 3, "\xE2\x80\xA6") == 0 &&
                  strncmp(v, s_devNames[1], 10) == 0 &&
                  ui_measure_menu_text(UI_MENU_TEXT_SIZE * 0.6f, v) <= 176.0f,
              "the long name cut with an ellipsis (%s)", v);
    }
    ui_settings_step(UI_OPT_DEVICE, 1);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_DEVICE), "Default") == 0 && s_reopened[0] == '\0' &&
              s_reopens == 3,
          "device wraps to Default");
    ui_settings_step(UI_OPT_DEVICE, -1);
    CHECK(strcmp(s_reopened, s_devNames[1]) == 0, "Left from Default: the last device");
    /* a device no longer there counts as Default */
    useConfig("[audio]\ndevice = \"Gone\"\n");
    CHECK(strcmp(ui_settings_value_text(UI_OPT_DEVICE), "Gone") == 0, "the name as set");
    ui_settings_step(UI_OPT_DEVICE, 1);
    CHECK(strcmp(s_reopened, s_devNames[0]) == 0, "unknown steps from Default");
    /* no devices (headless): Default only, nothing reopened */
    s_devCount = 0;
    useConfig("version = 1\n");
    s_reopens = 0;
    ui_settings_step(UI_OPT_DEVICE, 1);
    CHECK(s_reopens == 0 && strcmp(ui_settings_value_text(UI_OPT_DEVICE), "Default") == 0,
          "no devices: Default");
    /* other languages */
    s_outputMode = 0;
    ui_set_language(UI_LANG_DE);
    CHECK(strcmp(ui_settings_value_text(UI_OPT_DEVICE), "Standard") == 0, "Standard");
    CHECK(strcmp(ui_settings_value_text(UI_OPT_OUTPUT), "Automatisch (Stereo)") == 0,
          "Automatisch (Stereo) (%s)", ui_settings_value_text(UI_OPT_OUTPUT));
    ui_set_language(UI_LANG_EN);

    /* the save */
    useConfig("version = 1\n");
    ui_settings_step(UI_OPT_MUSIC, -1);
    ui_settings_step(UI_OPT_OUTPUT, 1);
    CHECK(ui_settings_save() == 0, "save");
    {
        IcoToml *t = ico_toml_load(p);
        CHECK(t != NULL && ico_toml_get_float(t, "audio.music", 0) > 0.89 &&
                  ico_toml_get_float(t, "audio.music", 0) < 0.91 &&
                  strcmp(ico_toml_get(t, "audio.output") ? ico_toml_get(t, "audio.output") : "",
                         "stereo") == 0 &&
                  ico_toml_get_float(t, "audio.effects", 0) == 1.0,
              "[audio] music, output, effects in the file");
        ico_toml_free(t);
    }
    ico_audio_gain_reset();
    s_outputMode = 0;
}

static void testCapture(void)
{
    IcoBindings b;
    UiRemapCapture c;
    ico_bindings_defaults(&b);
    ui_remap_capture_start(&c, ICO_T_TRIANGLE);
    CHECK(ui_remap_capture_step(&c, &b) == UI_CAPTURE_WAITING, "waiting");
    ico_input_note_press(ICO_SRC_PAD, ICO_GP_SOUTH);
    CHECK(ui_remap_capture_step(&c, &b) == UI_CAPTURE_BOUND && !c.active, "bound");
    CHECK(b.gp[ICO_T_TRIANGLE][0] == ICO_GP_SOUTH && b.gp[ICO_T_CROSS][0] == 0,
          "south moves from Cross to Triangle");
    CHECK(b.kb[ICO_T_TRIANGLE][0] == ICO_KEY_R, "the keyboard row is kept");
    CHECK(c.cooldown == UI_REMAP_COOLDOWN_TICKS, "cooldown");
    CHECK(ui_remap_capture_step(&c, &b) == UI_CAPTURE_IDLE, "idle afterwards");
    /* a press before the capture started is not taken */
    ico_input_note_press(ICO_SRC_KEY, ICO_KEY_Q);
    ui_remap_capture_start(&c, ICO_T_L1);
    for (int i = 0; i < UI_REMAP_TIMEOUT_TICKS - 1; i++) {
        CHECK(ui_remap_capture_step(&c, &b) == UI_CAPTURE_WAITING, "still waiting");
        if (!c.active) {
            break;
        }
    }
    CHECK(ui_remap_capture_step(&c, &b) == UI_CAPTURE_TIMEOUT, "times out");
    CHECK(b.kb[ICO_T_L1][0] == ICO_KEY_TAB, "the timeout leaves the binding");
    /* a mouse button */
    ui_remap_capture_start(&c, ICO_T_R2);
    ico_input_note_press(ICO_SRC_MOUSE, 4);
    CHECK(ui_remap_capture_step(&c, &b) == UI_CAPTURE_BOUND && b.mouse[ICO_T_R2][0] == 4 &&
              b.kb[ICO_T_R2][0] == ICO_KEY_X,
          "mouse x1 onto R2, the key kept");
    /* clear and the config text */
    ico_bindings_clear(&b, ICO_T_R2);
    char buf[64];
    CHECK(strcmp(ico_bindings_row_text(&b, ICO_SRC_KEY, ICO_T_R2, buf, sizeof(buf)), "none") == 0,
          "cleared: none");
    CHECK(strcmp(ico_bindings_row_text(&b, ICO_SRC_KEY, ICO_T_L1, buf, sizeof(buf)),
                 "Tab, Backquote") == 0,
          "two keys: %s", buf);
    IcoBindings c2;
    ico_bindings_defaults(&c2);
    CHECK(ico_bindings_set(&c2, "kb.l1", "Tab, Backquote") == 0 && c2.kb[ICO_T_L1][1] != 0,
          "the written text reads back");
}

static void testBootSkip(void)
{
    static const int scf[6] = {0, 1, 2, 3, 4, 5};
    static const int game[6] = {2, 2, 3, 6, 4, 5};
    for (int i = 0; i < 6; i++) {
        CHECK(ico_scf_to_game_language(scf[i]) == game[i], "scf %d -> %d", scf[i], game[i]);
    }
    for (int g = 2; g <= 6; g++) {
        CHECK(ico_scf_to_game_language(ico_game_to_scf_language(g)) == g, "round trip %d", g);
    }
    useConfig("[game]\nlanguage = \"de\"\n[video]\nvideo_mode = \"60hz\"\n");
    CHECK(ico_boot_language() == 4, "language de -> 4");
    CHECK(ico_boot_video_mode() == 0, "video_mode 60hz -> 0");
    useConfig("[game]\nlanguage = \"it\"\n[video]\nvideo_mode = \"pal50\"\n");
    CHECK(ico_boot_language() == 5 && ico_boot_video_mode() == 1, "it, pal50");
    useConfig("[game]\nlanguage = \"auto\"\n");
    setEnv("LC_ALL", "es_ES.UTF-8");
    /* the host's locale (SDL's preferred locales in the window build, the
       environment headless) */
    CHECK(ico_boot_language() == ico_scf_to_game_language(ico_sysconf_host_language()),
          "auto: the host's locale");
    CHECK(ico_scf_to_game_language(ico_scf_language_from_locale("es_ES.UTF-8")) == 6, "es_ES -> 6");
    CHECK(ico_boot_video_mode() == 0, "no video_mode: 60 Hz");
    setEnv("LC_ALL", "ja_JP.UTF-8");
    ico_sysconf_reset();
    CHECK(ico_scf_to_game_language(ico_scf_language_from_locale("ja_JP.UTF-8")) == 2,
          "a language the game lacks: English (2)");
    setEnv("LC_ALL", NULL);
    useConfig("[video]\nvideo_mode = \"pal\"\n");
    CHECK(ico_boot_video_mode() == 0, "an invalid value: ignored (60 Hz)");
    ico_sysconf_set_language(ICO_SCF_LANGUAGE_FRENCH);
    CHECK(sceScfGetLanguage() == ICO_SCF_LANGUAGE_FRENCH &&
              strcmp(ico_config_get_string("game.language", ""), "fr") == 0,
          "the setter");
}

static int rowShown(UiSettingsPage page, UiSettingsOpt opt);

/* the Video mode row changes only when Settings was opened from the
   title; from the pause menu Left and Right leave it. Pad names and the
   frame-rate words are translated. */
static void testVideoGate(void)
{
    for (int title = 0; title < 2; title++) {
        useConfig("version = 1\n");
        fakeTables();
        lt_ext_reset();
        ui_settings_reset();
        memset(pad, 0, sizeof(pad));
        pad[0].ana[0] = pad[0].ana[1] = pad[0].ana[2] = pad[0].ana[3] = 128;
        NonLinearCameraMove = 2;
        init_layout_texture(2);
        settle(54, 4);
        if (title) {
            int s13 = ui_settings_entry_row(13);
            lt_switch_layout(13);
            CHECK(settle(13, 60), "the title");
            press(0x4000);
            CHECK(texLayout[13].curItem == s13, "on Settings");
            press(0x40);
        } else {
            pauseToMain();
        }
        int mainL = ui_settings_page_layout(UI_PAGE_MAIN);
        CHECK(settle(mainL, 60), "the menu (title %d)", title);
        press(0x40); /* Display */
        int dispL = ui_settings_page_layout(UI_PAGE_DISPLAY);
        CHECK(settle(dispL, 60), "Display");
        int labels[16], opts[16];
        int n = ui_settings_page_rows(UI_PAGE_DISPLAY, labels, opts, NULL, 16);
        int vm = 0, downs = 0;
        if (!title) {
            /* from the pause menu the row is not shown (it cannot
               step there, and the page has no room once Texture pack is
               in); its value still says why */
            CHECK(!rowShown(UI_PAGE_DISPLAY, UI_OPT_VIDEO_MODE), "pause: no Video mode row");
            systemStatus[0] = 1;
            CHECK(strstr(ui_settings_value_text(UI_OPT_VIDEO_MODE), "(title only)") != NULL,
                  "pause: the value reads \"%s\"", ui_settings_value_text(UI_OPT_VIDEO_MODE));
            continue;
        }
        while (vm < n && opts[vm] != UI_OPT_VIDEO_MODE) {
            /* Brightness, from the pause menu only, is above it */
            downs += !lt_ext_prop(labels[vm])->defaultMask;
            vm++;
        }
        CHECK(downs == (title ? vm - 1 : vm), "title %d: Brightness %s", title,
              title ? "hidden" : "shown");
        for (int i = 0; i < downs; i++) {
            press(0x4000);
        }
        CHECK(lt_ext_layout(dispL)->curItem == labels[vm], "on Video mode (%d of %d)", vm, n);
        systemStatus[0] = 1;
        CHECK((strstr(ui_settings_value_text(UI_OPT_VIDEO_MODE), "(title only)") != NULL) == !title,
              "title %d: the value reads \"%s\"", title, ui_settings_value_text(UI_OPT_VIDEO_MODE));
        press(0x2000);
        CHECK(systemStatus[0] == (title ? 0 : 1), "title %d: Right on Video mode: %d", title,
              systemStatus[0]);
        systemStatus[0] = 1;
        press(0x8000);
        CHECK(systemStatus[0] == (title ? 0 : 1), "title %d: Left on Video mode: %d", title,
              systemStatus[0]);
        systemStatus[0] = 1;
    }
    static const UiLang kLangs[5] = {UI_LANG_EN, UI_LANG_FR, UI_LANG_DE, UI_LANG_IT, UI_LANG_ES};
    static const char *const kUncapped[5] = {"Uncapped", "Illimit\xC3\xA9", "Unbegrenzt",
                                             "Illimitato", "Sin l\xC3\xADmite"};
    for (int i = 0; i < 5; i++) {
        CHECK(strcmp(ui_str_in(kLangs[i], UI_STR_VAL_UNCAPPED), kUncapped[i]) == 0,
              "Uncapped in language %d", i);
        CHECK(strcmp(ui_str_in(kLangs[i], UI_STR_FPS_UNIT), "fps") == 0, "fps in language %d", i);
        CHECK(ui_str_in(kLangs[i], UI_STR_VIDEO_MODE_TITLE_ONLY)[0] != '\0', "note %d", i);
        for (int id = UI_STR_PAD_SOUTH; id <= UI_STR_PAD_RSTICK_DOWN; id++) {
            CHECK(ui_str_in(kLangs[i], (UiStrId)id)[0] != '\0', "pad name %d in language %d", id,
                  i);
        }
    }
    CHECK(strcmp(ui_str_in(UI_LANG_FR, UI_STR_PAD_DPAD_UP), "Croix haut") == 0 &&
              strcmp(ui_str_in(UI_LANG_DE, UI_STR_PAD_DPAD_UP), "Kreuz oben") == 0 &&
              strcmp(ui_str_in(UI_LANG_EN, UI_STR_PAD_L1), "L1") == 0,
          "pad names");
}

/* --------------------------------------------- Extras and the shared lists */

/* The shared list pages (ui_list.h) on a list of its own: 20 items, three
   headings (item 0, items 5 and 6 together, item 19) the cursor skips. */
static int s_fills;

static int tlCount(void *u)
{
    return *(int *)u;
}

static int tlHeading(void *u, int k)
{
    (void)u;
    return k == 0 || k == 5 || k == 6 || k == 19;
}

static void tlFill(void *u, int k, UiListSlot *out)
{
    static char text[8][16];
    (void)u;
    s_fills++;
    char *t = text[k % 8];
    snprintf(t, 16, "item %d", k);
    out->label = t;
    out->colAStr = UI_STR_ON;
}

/* one tick: the proc, then the layout's move (default_item_select) */
static int s_hdrDecorated;

static void tlDecorate(void *u, int cur)
{
    (void)u;
    s_hdrDecorated = cur;
}

static int s_moveSounds; /* default_item_select's cursor sounds */

/* the engine's order in exec_layout_texture: the layout's proc, then
   default_item_select unless the proc set lt_item_select_disable (reset at
   the end of the frame) */
static void listStep(UiList *l, LtProp *lay, int flags)
{
    ui_list_proc(l, lay, flags);
    int before = lay->curItem;
    if (!(flags & 0x50) && lt_item_select_disable == 0) {
        const LtProperty *e = lt_ext_prop(lay->curItem);
        if ((flags & 0x1000) && e->upItem >= 0) {
            lay->curItem = e->upItem;
        } else if ((flags & 0x4000) && e->downItem >= 0) {
            lay->curItem = e->downItem;
        }
    }
    if (lay->curItem != before) {
        s_moveSounds++;
    }
    lt_item_select_disable = 0;
    ui_list_refresh(l, lay->curItem);
}

/* ----------------------------------------------------------- the gallery
 * Settings > Extras > Music (gallery.h) over a fake engine: a few streams
 * in each group, the heading skip, Cross / Square / Left / Right and leaving.
 * The list from the PAL tables is gallery_test's. */

/* the port row of a page with that text right after a glyph row */
static int glyphBefore(UiSettingsPage page, const char *word)
{
    int t = rowWithText(page, word);
    return t > 0 && lt_ext_is_glyph_row(lt_ext_prop(t - 1)) ? t - 1 : -1;
}

static int fillRow(UiSettingsPage page)
{
    LtProp *l = lt_ext_layout(ui_settings_page_layout(page));
    int last = -1;
    /* the third rect of the page: the rim, the track, the fill */
    int n = 0;
    for (int j = l->first; j < l->last; j++) {
        LtProperty *p = lt_ext_prop(j);
        if (p->texFileNo >= 0x7000 && !lt_ext_is_glyph_row(p) && ++n == 3) {
            last = j;
        }
    }
    return last;
}

/* every glyph's source row and texel rectangle, as the PAL texProperty
   table holds them (the boot ELF's rows: 182/344/343/184 the face buttons on
   buttons.tm2, 349/346/348/347 L1 R1 L2 R2 on menu_PAL_02 at v 240, 301/302
   the arrows on menu_PAL_01), and the box each draws beside a 27-unit label */
static void testGlyphSources(void)
{
    static const struct {
        int glyph, row, u, v, w, h, boxW, boxH;
    } k[] = {
        {LT_GLYPH_CROSS, 182, 32, 30, 32, 30, 32, 30},
        {LT_GLYPH_CIRCLE, 344, 0, 30, 32, 30, 32, 30},
        {LT_GLYPH_SQUARE, 343, 32, 0, 32, 30, 32, 30},
        {LT_GLYPH_TRIANGLE, 184, 0, 0, 32, 30, 32, 30},
        {LT_GLYPH_L1, 349, 420, 240, 40, 15, 40, 30},
        {LT_GLYPH_R1, 346, 340, 240, 40, 15, 40, 30},
        {LT_GLYPH_L2, 348, 460, 240, 40, 15, 40, 30},
        {LT_GLYPH_R2, 347, 380, 240, 40, 15, 40, 30},
        {LT_GLYPH_LEFT, 301, 490, 130, 20, 20, 20, 40},
        {LT_GLYPH_RIGHT, 302, 490, 150, 20, 20, 20, 40},
    };

    CHECK(sizeof(k) / sizeof(k[0]) == LT_GLYPH_COUNT, "a source listed for every glyph");
    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
        int uvwh[4], bw = 0, bh = 0;
        int row = lt_ext_glyph_source(k[i].glyph, uvwh);
        lt_ext_glyph_box(k[i].glyph, UI_MENU_TEXT_SIZE, &bw, &bh);
        CHECK(row == k[i].row && uvwh[0] == k[i].u && uvwh[1] == k[i].v && uvwh[2] == k[i].w &&
                  uvwh[3] == k[i].h,
              "glyph %d: row %d (%d,%d %dx%d)", k[i].glyph, row, uvwh[0], uvwh[1], uvwh[2],
              uvwh[3]);
        CHECK(bw == k[i].boxW && bh == k[i].boxH, "glyph %d: box %dx%d", k[i].glyph, bw, bh);
    }
}

/* The title's Options rows: a text row in the menus'
   look with the sheets' own word (UI_STR_MT_OPTIONS, each language's),
   centred in a game row's box height as Continue and New Game are; no
   glyph row, no texture */
static void testTitleOptionsWord(void)
{
    const int mainL = enterMain(1);
    (void)mainL;
    for (int g = 12; g <= 13; g++) {
        const int row = ui_settings_entry_row(g);
        const LtProperty *e = lt_ext_prop(row);
        CHECK(row >= 0 && lt_ext_is_port_prop(e) && !lt_ext_is_glyph_row(e) &&
                  lt_ext_is_text_row(e),
              "title %d: Options is a text row (%d)", g, row);
        CHECK(e->centerX && e->dispH == texProperty[50].dispH && e->selectable,
              "title %d: a game row's height, centred (%d x %d)", g, e->dispW, e->dispH);
        CHECK(strcmp(lt_ext_row_text(row), ui_str_in(UI_LANG_EN, UI_STR_MT_OPTIONS)) == 0 &&
                  strcmp(lt_ext_row_text(row), "Options") == 0,
              "title %d: its label the sheets' Options (%s)", g, lt_ext_row_text(row));
    }
    /* the label follows the language: German's word */
    const int row = ui_settings_entry_row(13);
    ui_set_language(UI_LANG_DE);
    NonLinearCameraMove = 4; /* German */
    lt_switch_layout(13);
    settle(13, 60);
    CHECK(strcmp(lt_ext_row_text(row), "Optionen") == 0, "German: the label %s (Optionen)",
          lt_ext_row_text(row));
    NonLinearCameraMove = 2;
    ui_set_language(UI_LANG_EN);
    settle(13, 60);
    frame(0);
    /* every language's word: the main page's heading and the label */
    static const char *const kWord[5] = {"Options", "Options", "Optionen", "Opzioni", "Opción"};
    static const UiLang kLangs[5] = {UI_LANG_EN, UI_LANG_FR, UI_LANG_DE, UI_LANG_IT, UI_LANG_ES};
    for (int i = 0; i < 5; i++) {
        CHECK(strcmp(ui_str_in(kLangs[i], UI_STR_SETTINGS), kWord[i]) == 0 &&
                  strcmp(ui_str_in(kLangs[i], UI_STR_MT_OPTIONS), kWord[i]) == 0,
              "language %d: the menu is the game's word %s (%s)", i, kWord[i],
              ui_str_in(kLangs[i], UI_STR_SETTINGS));
    }
}

static void testGallery(void)
{
    gallery_set_engine(&kFakeEngine);
    int mainL = enterMain(1);
    int ml[16];
    ui_settings_page_rows(UI_PAGE_MAIN, ml, NULL, NULL, 16);
    lt_ext_layout(mainL)->curItem = ml[8];
    press(0x40);
    int exL = ui_settings_page_layout(UI_PAGE_EXTRAS);
    CHECK(settle(exL, 60), "gallery: Extras");
    lt_ext_layout(exL)->curItem = ui_settings_row_of(UI_PAGE_EXTRAS, UI_OPT_EXTRAS_MUSIC);
    frame(0);
    press(0x40);
    int galL = ui_settings_page_layout(UI_PAGE_MUSIC);
    CHECK(settle(galL, 60), "gallery: the page");
    frame(0);
    int lab[16], opts[16];
    ui_settings_page_rows(UI_PAGE_MUSIC, lab, opts, NULL, 16);
    /* Soundtrack, battle, event/01, Scene sounds, event2/54, event2/55,
       Ambience, Voice: the assets' names, no column */
    CHECK(strcmp(lt_ext_row_text(lab[0]), "Soundtrack") == 0 &&
              strcmp(lt_ext_row_text(lab[1]), "battle") == 0 &&
              strcmp(lt_ext_row_text(lab[2]), "event/01") == 0 &&
              strcmp(lt_ext_row_text(lab[3]), "Scene sounds") == 0 &&
              strcmp(lt_ext_row_text(lab[4]), "event2/54") == 0,
          "gallery rows: \"%s\" \"%s\" \"%s\" \"%s\" \"%s\"", lt_ext_row_text(lab[0]),
          lt_ext_row_text(lab[1]), lt_ext_row_text(lab[2]), lt_ext_row_text(lab[3]),
          lt_ext_row_text(lab[4]));
    CHECK(strcmp(lt_ext_row_text(lab[1] + 1), "") == 0, "no column for a stream (\"%s\")",
          lt_ext_row_text(lab[1] + 1));
    CHECK(lt_ext_prop(lab[0])->dispX < lt_ext_prop(lab[1])->dispX, "headings stand out");
    /* the transport: each word after its glyph (a texture row: the fake
       tables hold the glyphs' rectangles), on one line */
    static const char *const kWords[] = {"Previous", "Play", "Stop", "Next", "Section", "Back"};
    int lastX = -1;
    for (unsigned w = 0; w < sizeof(kWords) / sizeof(kWords[0]); w++) {
        int g = glyphBefore(UI_PAGE_MUSIC, kWords[w]);
        int t = rowWithText(UI_PAGE_MUSIC, kWords[w]);
        CHECK(g >= 0, "the transport's \"%s\" after a glyph", kWords[w]);
        if (g < 0) {
            continue;
        }
        LtProperty *gp = lt_ext_prop(g), *tp = lt_ext_prop(t);
        CHECK(!lt_ext_is_text_row(gp) && lt_ext_glyph_tex_no(gp) > 0, "\"%s\": a texture glyph",
              kWords[w]);
        CHECK(gp->dispX + gp->dispW <= tp->dispX && gp->dispX > lastX,
              "\"%s\": glyph then word, left to right", kWords[w]);
        /* the glyph's middle on the word's capitals (6.5 field lines into
           its box), within a field line */
        float mid = (float)gp->dispY + (float)gp->dispH * 0.25f;
        CHECK(mid > (float)tp->dispY + 5.4f && mid < (float)tp->dispY + 7.6f,
              "\"%s\": glyph centred on the word (%.1f, box %d)", kWords[w], mid, tp->dispY);
        lastX = tp->dispX;
    }
    int fill = fillRow(UI_PAGE_MUSIC);
    CHECK(fill >= 0 && lt_ext_row_fill(fill) == 0.0f, "the bar empty while nothing plays");
    CHECK(rowWithText(UI_PAGE_MUSIC, "0:00") >= 0, "0:00 while nothing plays");
    LtProp *l = lt_ext_layout(galL);
    CHECK(l->curItem == lab[1], "the cursor skips the heading onto battle");
    press(0x40);
    CHECK(s_galPlays == 1 && s_galLastKey == 1, "Cross plays stream 1");
    frame(0);
    CHECK(rowWithText(UI_PAGE_MUSIC, "Soundtrack  \xC2\xB7  battle.int  \xC2\xB7  Playing") >= 0,
          "the status: group, file, Playing");
    CHECK(fill >= 0 && lt_ext_row_fill(fill) > 0.15f && lt_ext_row_fill(fill) < 0.17f,
          "the bar at 42.4 of 265.6 s (%.3f)", fill >= 0 ? lt_ext_row_fill(fill) : -1.0f);
    CHECK(rowWithText(UI_PAGE_MUSIC, "0:42") >= 0 && rowWithText(UI_PAGE_MUSIC, "4:25") >= 0,
          "the times 0:42 and 4:25");
    CHECK(glyphBefore(UI_PAGE_MUSIC, "Pause") >= 0, "Cross's word is Pause while it plays");
    press(0x40);
    CHECK(s_galPaused == 1 && s_galPlays == 1, "Cross again pauses it");
    frame(0);
    CHECK(rowWithText(UI_PAGE_MUSIC, "Soundtrack  \xC2\xB7  battle.int  \xC2\xB7  Paused") >= 0,
          "the status: Paused");
    CHECK(rowWithText(UI_PAGE_MUSIC, "0:42") >= 0, "the bar kept while paused");
    press(0x40);
    CHECK(s_galPaused == 0 && s_galPlays == 1, "Cross again resumes it");
    press(0x0008);
    CHECK(s_galPlays == 2 && s_galLastKey == 6 &&
              strcmp(lt_ext_row_text(l->curItem), "event/01") == 0,
          "R1: the next entry, under the cursor (%d)", s_galLastKey);
    press(0x0008);
    CHECK(s_galLastKey == 60 && strcmp(lt_ext_row_text(l->curItem), "event2/54") == 0,
          "R1 over the Scene sounds heading");
    press(0x0004);
    CHECK(s_galLastKey == 6, "L1: the previous entry");
    press(0x0004);
    press(0x80);
    CHECK(s_galStops == 1, "Square stops");
    frame(0);
    CHECK(fill >= 0 && lt_ext_row_fill(fill) == 0.0f, "the bar empty once stopped");
    press(0x4000);
    press(0x4000);
    CHECK(strcmp(lt_ext_row_text(l->curItem), "event2/54") == 0,
          "Down skips the Scene sounds heading");
    press(0x8000);
    CHECK(strcmp(lt_ext_row_text(l->curItem), "battle") == 0,
          "Left: back to the soundtrack's first entry");
    press(0x2000);
    CHECK(strcmp(lt_ext_row_text(l->curItem), "event2/54") == 0,
          "Right: the scene sounds' first entry (\"%s\")", lt_ext_row_text(l->curItem));
    press(0x2000);
    CHECK(strcmp(lt_ext_row_text(l->curItem), "event2/hint1_1") == 0,
          "Right: over the empty Ambience to the voices (\"%s\")", lt_ext_row_text(l->curItem));
    press(0x10);
    CHECK(settle(exL, 60) && s_galLeaves == 1, "Triangle leaves the gallery");
    gallery_set_engine(NULL);
}

static int tlPlain(void *u, int k)
{
    (void)u;
    (void)k;
    return 0;
}

/* a wrap through the engine's order (proc, then default_item_select) lands
   on the end row, with one cursor sound and no extra move */
static void testListWrap(int count)
{
    UiListDef def = {tlCount, tlFill, tlPlain, NULL, tlDecorate};
    UiListStyle st;
    UiList l;
    LtProp lay;
    memset(&st, 0, sizeof(st));
    st.y0 = 40;
    st.pitch = 18;
    st.label = (UiListCol){40, 400, 24.0f, UI_ALIGN_LEFT};
    st.colA = (UiListCol){440, 160, 21.0f, UI_ALIGN_RIGHT};
    st.statusY = 196;
    lt_ext_reset();
    ui_settings_reset();
    ui_list_build(&l, &def, &count, &st);
    memset(&lay, 0, sizeof(lay));
    ui_list_reset(&l);
    ui_list_refresh(&l, -1);
    int shown = ui_list_shown(&l);
    /* Up from the first item wraps to the last */
    lay.curItem = l.label[0];
    ui_list_refresh(&l, lay.curItem);
    int snd = s_sounds[0] + s_moveSounds;
    listStep(&l, &lay, 0x1000);
    CHECK(ui_list_item_of_row(&l, lay.curItem) == count - 1 && l.offset == count - shown,
          "%d items: Up from the first wraps to the last (item %d, offset %d)", count,
          ui_list_item_of_row(&l, lay.curItem), l.offset);
    CHECK(s_sounds[0] + s_moveSounds == snd + 1, "%d items: one cursor sound on the Up wrap (%d)",
          count, s_sounds[0] + s_moveSounds - snd);
    listStep(&l, &lay, 0);
    CHECK(ui_list_item_of_row(&l, lay.curItem) == count - 1, "%d items: the next frame stays",
          count);
    /* Down from the last item wraps to the first */
    snd = s_sounds[0] + s_moveSounds;
    listStep(&l, &lay, 0x4000);
    CHECK(ui_list_item_of_row(&l, lay.curItem) == 0 && l.offset == 0,
          "%d items: Down from the last wraps to the first (item %d, offset %d)", count,
          ui_list_item_of_row(&l, lay.curItem), l.offset);
    CHECK(s_sounds[0] + s_moveSounds == snd + 1, "%d items: one cursor sound on the Down wrap (%d)",
          count, s_sounds[0] + s_moveSounds - snd);
}

/* ---------------------------------------------- the game's settings */

static int rowShown(UiSettingsPage page, UiSettingsOpt opt)
{
    const int row = ui_settings_row_of(page, opt);
    return row >= 0 && !lt_ext_prop(row)->defaultMask && !lt_ext_prop(row)->masked;
}

/* the game's Options screen's settings on the pages.  Brightness
 * (Display), Button configuration, Vibration and Hold type (Controls) show
 * from the pause menu only, Film effect and Players (Gameplay) only there
 * once the game is cleared; the hidden rows are stepped over and the pages
 * still fit.  Their values are the game's variables; Film effect goes
 * through la_host_film_effect (the stage animations); Button configuration
 * opens the game's layout 59, whose OK comes back to Controls on the row. */
static void testGameOptions(void)
{
    for (int title = 1; title >= 0; title--) {
        gFlagGameClear = 0;
        stage_no = title ? 1 : 11;
        int mainL = enterMain(title);
        openPage(mainL, 0, UI_PAGE_DISPLAY);
        CHECK(rowShown(UI_PAGE_DISPLAY, UI_OPT_BRIGHTNESS) == !title, "title %d: Brightness %s",
              title, title ? "hidden" : "shown");
        checkPageFits(UI_PAGE_DISPLAY, title ? "Display (title)" : "Display (pause)");
        press(0x10);
        CHECK(settle(mainL, 60), "Display: back");
        openPage(mainL, 1, UI_PAGE_EFFECTS);
        CHECK(rowShown(UI_PAGE_EFFECTS, UI_OPT_CRT) &&
                  rowShown(UI_PAGE_EFFECTS, UI_OPT_CRT_STRENGTH) &&
                  !rowShown(UI_PAGE_DISPLAY, UI_OPT_CRT) &&
                  ui_settings_row_of(UI_PAGE_DISPLAY, UI_OPT_CRT_STRENGTH) < 0,
              "title %d: the CRT rows are on Effects, not Display", title);
        checkPageFits(UI_PAGE_EFFECTS, title ? "Effects (title)" : "Effects (pause)");
        press(0x10);
        CHECK(settle(mainL, 60), "Effects: back");
        const int ctlL = openPage(mainL, 4, UI_PAGE_CONTROLS);
        CHECK(rowShown(UI_PAGE_CONTROLS, UI_OPT_BUTTON_CONFIG) == !title &&
                  rowShown(UI_PAGE_CONTROLS, UI_OPT_VIBRATION) == !title &&
                  rowShown(UI_PAGE_CONTROLS, UI_OPT_HOLD_TYPE) == !title,
              "title %d: the game's controls %s", title, title ? "hidden" : "shown");
        checkPageFits(UI_PAGE_CONTROLS, title ? "Controls (title)" : "Controls (pause)");
        press(0x4000);
        CHECK(lt_ext_layout(ctlL)->curItem ==
                  ui_settings_row_of(UI_PAGE_CONTROLS,
                                     title ? UI_OPT_MOUSE_CAMERA : UI_OPT_BUTTON_CONFIG),
              "title %d: down from Remap", title);
        press(0x10);
        CHECK(settle(mainL, 60), "Controls: back");
        openPage(mainL, 5, UI_PAGE_GAMEPLAY);
        CHECK(!rowShown(UI_PAGE_GAMEPLAY, UI_OPT_FILM_EFFECT) &&
                  !rowShown(UI_PAGE_GAMEPLAY, UI_OPT_PLAYERS),
              "title %d, not cleared: no Film effect or Players", title);
        checkPageFits(UI_PAGE_GAMEPLAY, "Gameplay (not cleared)");
        press(0x10);
        CHECK(settle(mainL, 60), "Gameplay: back");
    }

    /* cleared, from the pause menu */
    gFlagGameClear = 1;
    stage_no = 11;
    int mainL = enterMain(0);
    const int gameL = openPage(mainL, 5, UI_PAGE_GAMEPLAY);
    const int film = ui_settings_row_of(UI_PAGE_GAMEPLAY, UI_OPT_FILM_EFFECT);
    const int players = ui_settings_row_of(UI_PAGE_GAMEPLAY, UI_OPT_PLAYERS);
    CHECK(rowShown(UI_PAGE_GAMEPLAY, UI_OPT_FILM_EFFECT) &&
              rowShown(UI_PAGE_GAMEPLAY, UI_OPT_PLAYERS),
          "cleared: Film effect and Players");
    checkPageFits(UI_PAGE_GAMEPLAY, "Gameplay (cleared)");
    press(0x4000);
    press(0x4000);
    CHECK(lt_ext_layout(gameL)->curItem == film, "down twice from Yorda: Film effect");
    optionScreenMode = 0;
    s_filmCalls = 0;
    CHECK(strcmp(ui_settings_value_text(UI_OPT_FILM_EFFECT), "Off") == 0, "film effect Off (%s)",
          ui_settings_value_text(UI_OPT_FILM_EFFECT));
    press(0x2000);
    CHECK(s_filmCalls == 1 && s_filmLast == 1 && optionScreenMode == 1 &&
              strcmp(ui_settings_value_text(UI_OPT_FILM_EFFECT), "1") == 0,
          "Right: film effect 1 through la_host_film_effect (%d calls, %d)", s_filmCalls,
          s_filmLast);
    press(0x8000);
    press(0x8000);
    CHECK(s_filmCalls == 3 && s_filmLast == 4 && optionScreenMode == 4,
          "Left twice: Off, then around to 4 (%d)", optionScreenMode);
    la_host_film_effect(0);
    press(0x4000);
    CHECK(lt_ext_layout(gameL)->curItem == players, "down: Players");
    girlControlMode = 0;
    CHECK(strcmp(ui_settings_value_text(UI_OPT_PLAYERS), "1") == 0, "Players 1");
    press(0x2000);
    CHECK(girlControlMode == 1 && strcmp(ui_settings_value_text(UI_OPT_PLAYERS), "2") == 0,
          "Right: Players 2 (girlControlMode %d)", girlControlMode);
    const int pnote = noteStarting(UI_PAGE_GAMEPLAY, "2: a second");
    CHECK(pnote >= 0 && !lt_ext_prop(pnote)->masked, "the Players note on the cursor");
    press(0x8000);
    CHECK(girlControlMode == 0, "Left: Players 1");
    press(0x10);
    CHECK(settle(mainL, 60), "Gameplay: back");

    /* Controls: Vibration and Hold type, Button configuration to 59 and back */
    const int ctlL = openPage(mainL, 4, UI_PAGE_CONTROLS);
    const int bc = ui_settings_row_of(UI_PAGE_CONTROLS, UI_OPT_BUTTON_CONFIG);
    CHECK(bc >= 0 && lt_ext_prop(bc)->right == 59, "Button configuration opens 59");
    press(0x4000);
    press(0x4000);
    CHECK(lt_ext_layout(ctlL)->curItem == ui_settings_row_of(UI_PAGE_CONTROLS, UI_OPT_VIBRATION),
          "on Vibration");
    iosPadActRequestEnable = 1;
    CHECK(strcmp(ui_settings_value_text(UI_OPT_VIBRATION), "On") == 0, "vibration On");
    press(0x2000);
    CHECK(iosPadActRequestEnable == 0 &&
              strcmp(ui_settings_value_text(UI_OPT_VIBRATION), "Off") == 0,
          "Right: vibration off (%d)", iosPadActRequestEnable);
    press(0x8000);
    CHECK(iosPadActRequestEnable == 1, "Left: on again");
    press(0x4000);
    optionControlType = 0;
    CHECK(strcmp(ui_settings_value_text(UI_OPT_HOLD_TYPE), "A (hold)") == 0, "hold type A (%s)",
          ui_settings_value_text(UI_OPT_HOLD_TYPE));
    press(0x2000);
    CHECK(optionControlType == 1 &&
              strcmp(ui_settings_value_text(UI_OPT_HOLD_TYPE), "B (toggle)") == 0,
          "Right: hold type B (%d)", optionControlType);
    const int hnote = noteStarting(UI_PAGE_CONTROLS, "A: Yorda");
    CHECK(hnote >= 0 && !lt_ext_prop(hnote)->masked, "the Hold type note on the cursor");
    press(0x2000);
    CHECK(optionControlType == 0, "Right again: A");
    press(0x1000);
    press(0x1000);
    CHECK(lt_ext_layout(ctlL)->curItem == bc, "up twice: Button configuration");
    press(0x40);
    CHECK(settle(59, 60), "Cross: the game's button configuration (%d)", current_layout_id);
    /* la_key_config's OK returns what ui_settings_key_config_back gives */
    const int to = ui_settings_key_config_back();
    CHECK(to == ctlL && lt_ext_layout(ctlL)->defaultItem == bc,
          "its OK: Controls with the cursor on the row (%d)", to);
    lt_switch_layout(to);
    CHECK(settle(ctlL, 60) && lt_ext_layout(ctlL)->curItem == bc, "back on Button configuration");
    press(0x10);
    CHECK(settle(mainL, 60), "Controls: back");
    press(0x10);
    CHECK(settle(57, 60) && texLayout[57].curItem == 294, "the menu: back to Options");

    /* End Game, then the title's Settings > Controls: the cursor on Remap,
       not on the Button configuration row the OK above left as Controls'
       default (hidden from the title; its Cross would open 59) */
    stage_no = 1;
    lt_switch_layout(13);
    CHECK(settle(13, 60), "End Game: the title");
    texLayout[13].curItem = ui_settings_entry_row(13);
    press(0x40);
    CHECK(settle(mainL, 60), "the title's Settings");
    openPage(mainL, 4, UI_PAGE_CONTROLS);
    const int remap = ui_settings_row_of(UI_PAGE_CONTROLS, UI_OPT_LINK);
    CHECK(lt_ext_layout(ctlL)->curItem == remap, "title: Controls opens on Remap (%d, not %d)",
          lt_ext_layout(ctlL)->curItem, bc);
    press(0x40);
    CHECK(settle(ui_settings_page_layout(UI_PAGE_REMAP), 60) && current_layout_id != 59,
          "title: Cross opens Remap, not 59 (%d)", current_layout_id);
    press(0x10);
    CHECK(settle(ctlL, 60), "Remap: back");
    /* a hidden row the cursor lands on anyway (a default set behind the
       entry's back) moves to the first shown row; its links skip the
       hidden rows */
    press(0x10);
    CHECK(settle(mainL, 60), "Controls: back");
    lt_ext_layout(ctlL)->defaultItem = bc;
    openPage(mainL, 4, UI_PAGE_CONTROLS);
    frame(0);
    CHECK(lt_ext_layout(ctlL)->curItem == remap && lt_ext_layout(ctlL)->defaultItem == remap,
          "title: off the hidden row (%d)", lt_ext_layout(ctlL)->curItem);
    CHECK(lt_ext_prop(bc)->upItem == remap &&
              lt_ext_prop(bc)->downItem ==
                  ui_settings_row_of(UI_PAGE_CONTROLS, UI_OPT_MOUSE_CAMERA),
          "title: the hidden row's links lead to shown rows (%d, %d)", lt_ext_prop(bc)->upItem,
          lt_ext_prop(bc)->downItem);
    for (int i = 0; i < 12; i++) {
        press(0x4000);
        CHECK(rowShown(UI_PAGE_CONTROLS, UI_OPT_LINK) &&
                  !lt_ext_prop(lt_ext_layout(ctlL)->curItem)->defaultMask,
              "title: Down %d on a shown row (%d)", i, lt_ext_layout(ctlL)->curItem);
    }
    press(0x10);
    CHECK(settle(mainL, 60), "Controls: back");
    stage_no = 11;
    lt_switch_layout(57);
    CHECK(settle(57, 40), "the pause menu again");

    /* Brightness: 0..14 a step at a time, no wrap (la_adjust_screen) */
    systemStatus[11] = 7;
    CHECK(strcmp(ui_settings_value_text(UI_OPT_BRIGHTNESS), "7 (Default)") == 0,
          "brightness 7, the default (%s)", ui_settings_value_text(UI_OPT_BRIGHTNESS));
    ui_settings_step(UI_OPT_BRIGHTNESS, 1);
    CHECK(systemStatus[11] == 8 && strcmp(ui_settings_value_text(UI_OPT_BRIGHTNESS), "8") == 0,
          "brightness 8 (%s)", ui_settings_value_text(UI_OPT_BRIGHTNESS));
    for (int i = 0; i < 10; i++) {
        ui_settings_step(UI_OPT_BRIGHTNESS, 1);
    }
    CHECK(systemStatus[11] == 14, "brightness stops at 14 (%d)", systemStatus[11]);
    for (int i = 0; i < 20; i++) {
        ui_settings_step(UI_OPT_BRIGHTNESS, -1);
    }
    CHECK(systemStatus[11] == 0 && strcmp(ui_settings_value_text(UI_OPT_BRIGHTNESS), "0") == 0,
          "brightness stops at 0 (%d)", systemStatus[11]);
    /* Square on the row: the default again (the adjust screen's Default) */
    {
        const int mainB = enterMain(0);
        const int dispL = openPage(mainB, 0, UI_PAGE_DISPLAY);
        lt_ext_layout(dispL)->curItem = ui_settings_row_of(UI_PAGE_DISPLAY, UI_OPT_BRIGHTNESS);
        frame(0);
        press(0x0080);
        CHECK(systemStatus[11] == 7 && settle(dispL, 4), "Square: brightness 7 again (%d)",
              systemStatus[11]);
        systemStatus[11] = 3;
        lt_ext_layout(dispL)->curItem = ui_settings_row_of(UI_PAGE_DISPLAY, UI_OPT_VIDEO_MODE);
        press(0x0080);
        CHECK(systemStatus[11] == 3, "Square on another row: no reset (%d)", systemStatus[11]);
        press(0x10);
        CHECK(settle(mainB, 60), "Display: back");
    }
    systemStatus[11] = 0;

    /* the game's variables, never the port config */
    CHECK(ico_config_get_string("video.brightness", NULL) == NULL &&
              ico_config_get_string("input.vibration", NULL) == NULL,
          "no port keys for the game's settings");
    /* the strings in every language */
    static const UiLang kLangs[5] = {UI_LANG_EN, UI_LANG_FR, UI_LANG_DE, UI_LANG_IT, UI_LANG_ES};
    static const int kIds[] = {
        UI_STR_OPT_BRIGHTNESS,    UI_STR_OPT_VIBRATION,     UI_STR_OPT_HOLD_TYPE,
        UI_STR_OPT_BUTTON_CONFIG, UI_STR_OPT_FILM_EFFECT,   UI_STR_OPT_PLAYERS,
        UI_STR_VAL_HOLD_A,        UI_STR_VAL_HOLD_B,        UI_STR_HOLD_TYPE_NOTE,
        UI_STR_PLAYERS_NOTE,      UI_STR_BUTTON_CONFIG_NOTE};
    for (int i = 0; i < 5; i++) {
        for (unsigned k = 0; k < sizeof(kIds) / sizeof(kIds[0]); k++) {
            CHECK(ui_str_in(kLangs[i], (UiStrId)kIds[k])[0] != '\0', "string %d in language %d",
                  kIds[k], i);
        }
    }
    gFlagGameClear = 0;
    stage_no = 0;
}

static void testList(void)
{
    testListWrap(20);
    testListWrap(5);

    int count = 20;
    UiListDef def = {tlCount, tlFill, tlHeading, NULL, tlDecorate};
    UiListStyle st;
    UiList l;
    LtProp lay;
    memset(&st, 0, sizeof(st));
    st.y0 = 40;
    st.pitch = 18;
    st.label = (UiListCol){40, 400, 24.0f, UI_ALIGN_LEFT};
    st.colA = (UiListCol){440, 160, 21.0f, UI_ALIGN_RIGHT};
    st.statusY = 196;
    lt_ext_reset();
    ui_settings_reset();
    ui_list_build(&l, &def, &count, &st);
    CHECK(lt_ext_prop_count() == UI_LIST_SLOTS * 2 + 1 && l.colB[0] == -1,
          "the list adds two rows a slot and the status line (%d)", lt_ext_prop_count());
    memset(&lay, 0, sizeof(lay));

    /* refresh counts: every slot filled from its item, empty past the end */
    s_fills = 0;
    ui_list_refresh(&l, -1);
    CHECK(s_fills == UI_LIST_SLOTS && strcmp(lt_ext_row_text(l.label[0]), "item 0") == 0 &&
              strcmp(lt_ext_row_text(l.label[7]), "item 7") == 0 &&
              strcmp(lt_ext_row_text(l.colA[2]), "On") == 0 && s_hdrDecorated == -1,
          "refresh fills %d slots (%d)", UI_LIST_SLOTS, s_fills);
    count = 5;
    s_fills = 0;
    ui_list_refresh(&l, l.label[3]);
    CHECK(s_fills == 5 && ui_list_shown(&l) == 5 &&
              strcmp(lt_ext_row_text(l.label[4]), "item 4") == 0 &&
              lt_ext_row_text(l.label[5])[0] == '\0' && lt_ext_row_text(l.colA[7])[0] == '\0' &&
              s_hdrDecorated == 3 && ui_list_item_at(&l, 6) == -1,
          "5 items: 5 fills, the rest empty (%d)", s_fills);
    count = 0;
    s_fills = 0;
    ui_list_refresh(&l, -1);
    CHECK(s_fills == 0 && ui_list_shown(&l) == 0 && lt_ext_row_text(l.label[0])[0] == '\0',
          "no items: no fills");
    count = 20;

    /* the cursor starts on item 1 (item 0 is a heading); Down to item 4 */
    ui_list_reset(&l);
    ui_list_refresh(&l, -1);
    lay.curItem = l.label[1];
    for (int i = 0; i < 3; i++) {
        listStep(&l, &lay, 0x4000);
    }
    CHECK(ui_list_item_of_row(&l, lay.curItem) == 4, "item 4 (%d)",
          ui_list_item_of_row(&l, lay.curItem));
    /* Down onto the headings 5 and 6: the next tick goes on to 7 */
    listStep(&l, &lay, 0x4000);
    listStep(&l, &lay, 0);
    CHECK(ui_list_item_of_row(&l, lay.curItem) == 7 && l.offset == 0,
          "Down skips the headings 5 and 6 (%d)", ui_list_item_of_row(&l, lay.curItem));
    /* Up from there: 6 and 5 are headings, back to 4 */
    listStep(&l, &lay, 0x1000);
    listStep(&l, &lay, 0);
    CHECK(ui_list_item_of_row(&l, lay.curItem) == 4, "Up skips them (%d)",
          ui_list_item_of_row(&l, lay.curItem));
    /* the last slot scrolls the window */
    lay.curItem = l.label[7];
    listStep(&l, &lay, 0);
    int beforeScroll = l.offset;
    listStep(&l, &lay, 0x4000);
    CHECK(l.offset == beforeScroll + 1 && ui_list_item_of_row(&l, lay.curItem) == 8,
          "Down at the last slot scrolls by one (offset %d, item %d)", l.offset,
          ui_list_item_of_row(&l, lay.curItem));
    CHECK(strcmp(lt_ext_row_text(l.label[0]), "item 1") == 0, "the window shows item 1 first");
    /* the end: 18, then the heading 19, wrapping to 1 (0 is a heading) */
    lay.curItem = l.label[6];
    l.offset = 12; /* items 12..19 */
    ui_list_refresh(&l, lay.curItem);
    CHECK(ui_list_item_of_row(&l, lay.curItem) == 18, "item 18");
    listStep(&l, &lay, 0x4000);
    listStep(&l, &lay, 0);
    CHECK(ui_list_item_of_row(&l, lay.curItem) == 1 && l.offset == 0,
          "Down past the last heading wraps to item 1 (item %d, offset %d)",
          ui_list_item_of_row(&l, lay.curItem), l.offset);
    /* Up from item 1: the heading 0, then the wrap to 18 under the heading 19 */
    listStep(&l, &lay, 0x1000);
    listStep(&l, &lay, 0);
    CHECK(ui_list_item_of_row(&l, lay.curItem) == 18 && l.offset == 12,
          "Up past the first heading wraps to item 18 (item %d, offset %d)",
          ui_list_item_of_row(&l, lay.curItem), l.offset);
    CHECK(strcmp(lt_ext_row_text(l.label[7]), "item 19") == 0,
          "the last window ends on the heading");
    /* Cross and Triangle do not move or scroll the list */
    int off = l.offset, cur = lay.curItem;
    listStep(&l, &lay, 0x40 | 0x4000);
    CHECK(l.offset == off && lay.curItem == cur, "Cross does not scroll");
}

/* Display > Texture pack ("None installed" without a pack, the
   step then doing nothing; On/Off with one), its note, Dump textures on
   the main page in developer mode only, and the [video] keys' round trip
   through the config. */
/* the texture pack note sits on the Display page, whose notes must stay one
   line to keep clear of Back: checked in all five languages */
static void testTexturePackNoteLines(void)
{
    int mainL = enterMain(1);
    int tn;
    s_packCount = 883;
    ui_settings_set_texture_pack_count(fakePackCount);
    openPage(mainL, 0, UI_PAGE_DISPLAY);
    tn = rowWithPrefix(UI_PAGE_DISPLAY, "PCSX2 packs:");
    CHECK(tn >= 0, "the texture pack note on the Display page");
    for (int lang = 0; lang < UI_LANG_COUNT && tn >= 0; lang++) {
        const char *want = ui_str_in((UiLang)lang, UI_STR_TEXTURE_PACK_NOTE);
        NonLinearCameraMove = 2 + lang; /* the game's language: 2 EN .. 6 ES */
        frame(0);
        CHECK(strncmp(lt_ext_row_text(tn), want, 8) == 0, "lang %d: the note is %s (%s)", lang,
              want, lt_ext_row_text(tn));
        CHECK(strchr(lt_ext_row_text(tn), '\n') == NULL, "lang %d: the note is one line: %s", lang,
              lt_ext_row_text(tn));
    }
    NonLinearCameraMove = 2;
    ui_set_language(UI_LANG_EN);
    ui_settings_set_texture_pack_count(NULL);
}

static void testTexturePack(void)
{
    IcoVideoOptions o;

    useConfig("version = 1\n");
    fakeTables();
    lt_ext_reset();
    ui_settings_reset();
    ui_set_language(UI_LANG_EN);
    ui_settings_install();
    CHECK(strcmp(ui_settings_value_text(UI_OPT_TEXTURE_PACK), "None installed") == 0,
          "no hook: None installed (%s)", ui_settings_value_text(UI_OPT_TEXTURE_PACK));
    ui_settings_set_texture_pack_count(fakePackCount);
    s_packCount = 0;
    CHECK(strcmp(ui_settings_value_text(UI_OPT_TEXTURE_PACK), "None installed") == 0,
          "none found: None installed");
    ui_settings_step(UI_OPT_TEXTURE_PACK, 1);
    ico_video_get(&o);
    CHECK(o.texturePack == 1 &&
              strcmp(ui_settings_value_text(UI_OPT_TEXTURE_PACK), "None installed") == 0,
          "none installed: the step does nothing");
    s_packCount = 883;
    CHECK(strcmp(ui_settings_value_text(UI_OPT_TEXTURE_PACK), "On") == 0, "a pack: On");
    ui_settings_step(UI_OPT_TEXTURE_PACK, 1);
    ico_video_get(&o);
    CHECK(o.texturePack == 0 && strcmp(ui_settings_value_text(UI_OPT_TEXTURE_PACK), "Off") == 0,
          "Right: Off");
    ui_settings_step(UI_OPT_TEXTURE_PACK, -1);
    ico_video_get(&o);
    CHECK(o.texturePack == 1 && strcmp(ui_settings_value_text(UI_OPT_TEXTURE_PACK), "On") == 0,
          "Left: On");
    {
        /* the Display page's notes are one line, clear of Back (as the CRT
           note); the main page's dump note at most two */
        int tn = rowWithPrefix(UI_PAGE_DISPLAY, "PCSX2 packs:");
        int dn = rowWithPrefix(UI_PAGE_MAIN, "For pack makers:");
        const char *nl;
        CHECK(tn >= 0, "the Display note");
        CHECK(tn >= 0 && strchr(lt_ext_row_text(tn), '\n') == NULL,
              "the Display note is one line: %s", tn >= 0 ? lt_ext_row_text(tn) : "");
        CHECK(dn >= 0, "the dump row's note");
        nl = dn >= 0 ? strchr(lt_ext_row_text(dn), '\n') : NULL;
        CHECK(nl == NULL || strchr(nl + 1, '\n') == NULL, "the dump note is at most two lines");
    }
    CHECK(strcmp(ui_settings_value_text(UI_OPT_DUMP_TEXTURES), "Off") == 0, "dump: Off");
    ui_settings_step(UI_OPT_DUMP_TEXTURES, 1);
    ico_video_get(&o);
    CHECK(o.dumpTextures == 1 && strcmp(ui_settings_value_text(UI_OPT_DUMP_TEXTURES), "On") == 0,
          "dump: On");

    /* the round trip: both keys saved, the config-only ones absent at
       their defaults */
    char p[1100];
    CHECK(ui_settings_save() == 0, "save");
    path(p, sizeof(p), "settings_test.toml");
    IcoToml *t = ico_toml_load(p);
    CHECK(t && ico_toml_get_bool(t, "video.texture_pack", 0) == 1 &&
              ico_toml_get_bool(t, "video.dump_textures", 0) == 1 &&
              !ico_toml_has(t, "video.texture_pack_budget_mb") &&
              !ico_toml_has(t, "video.texture_pack_precache"),
          "saved: texture_pack, dump_textures; no budget or precache key");
    ico_toml_free(t);
    useConfig("version = 1\n[video]\ntexture_pack = false\ndump_textures = false\n"
              "texture_pack_budget_mb = 512\ntexture_pack_precache = false\n");
    ico_video_get(&o);
    CHECK(!o.texturePack && !o.dumpTextures && o.texturePackBudgetMb == 512 &&
              !o.texturePackPrecache,
          "read back: off, off, 512 MB, no precache");
    CHECK(strcmp(ui_settings_value_text(UI_OPT_TEXTURE_PACK), "Off") == 0, "the row: Off");
    o.texturePackBudgetMb = 4096;
    ico_video_set(&o);
    CHECK(ico_video_save() == 0, "save the budget");
    t = ico_toml_load(p);
    CHECK(t && ico_toml_get_bool(t, "video.texture_pack", 1) == 0 &&
              ico_toml_get_int(t, "video.texture_pack_budget_mb", 0) == 4096 &&
              ico_toml_get_bool(t, "video.texture_pack_precache", 1) == 0,
          "saved: off, 4096 MB, no precache");
    ico_toml_free(t);
    useConfig("version = 1\n[video]\ntexture_pack_budget_mb = 5\n");
    ico_video_get(&o);
    CHECK(o.texturePack && o.texturePackPrecache && o.texturePackBudgetMb == ICO_TEXPACK_BUDGET_MIN,
          "defaults on, a budget under the minimum clamped (%d)", o.texturePackBudgetMb);
    CHECK(o.texturePackCacheMb == 0, "the RAM cache's limit automatic by default (%d)",
          o.texturePackCacheMb);
    /* the RAM cache's limit, its own key: 0 or 128..65536, saved when set */
    useConfig("version = 1\n[video]\ntexture_pack_cache_mb = 5\n");
    ico_video_get(&o);
    CHECK(o.texturePackCacheMb == ICO_TEXPACK_CACHE_MIN && o.texturePackBudgetMb == 2048,
          "a cache limit under the minimum clamped (%d), the budget its own (%d)",
          o.texturePackCacheMb, o.texturePackBudgetMb);
    useConfig("version = 1\n[video]\ntexture_pack_cache_mb = 3000\n");
    ico_video_get(&o);
    CHECK(o.texturePackCacheMb == 3000, "cache limit 3000 MB read (%d)", o.texturePackCacheMb);
    o.texturePackCacheMb = 6000;
    ico_video_set(&o);
    CHECK(ico_video_save() == 0, "save the cache limit");
    t = ico_toml_load(p);
    CHECK(t && ico_toml_get_int(t, "video.texture_pack_cache_mb", 0) == 6000 &&
              !ico_toml_has(t, "video.texture_pack_budget_mb"),
          "saved: 6000 MB of cache, no budget key");
    ico_toml_free(t);

    /* Developer mode off switches Dump textures off with it: its row hides,
       and the dumps must not go on being written with no row to stop them */
    useConfig("version = 1\n[video]\ndump_textures = true\n");
    ico_opt_set_developer_mode(1);
    ico_video_get(&o);
    CHECK(o.dumpTextures == 1, "dump on from the file");
    ui_settings_step(UI_OPT_DEVELOPER, 1);
    ico_video_get(&o);
    CHECK(!ico_opt_developer_mode() && o.dumpTextures == 0, "Developer off: the dump off too");
    ui_settings_step(UI_OPT_DEVELOPER, 1);
    ico_video_get(&o);
    CHECK(ico_opt_developer_mode() && o.dumpTextures == 0,
          "Developer on again: the dump stays off until chosen");
    ico_opt_set_developer_mode(0);

    /* Dump textures is shown in developer mode only */
    int mainL = enterMain(1);
    CHECK(!rowShown(UI_PAGE_MAIN, UI_OPT_DUMP_TEXTURES), "dump row hidden");
    ico_opt_set_developer_mode(1);
    for (int k = 0; k < 4; k++) {
        frame(0); /* the page refreshes */
    }
    CHECK(rowShown(UI_PAGE_MAIN, UI_OPT_DUMP_TEXTURES), "dump row shown in developer mode");
    {
        int labels[16];
        const int n = ui_settings_page_rows(UI_PAGE_MAIN, labels, NULL, NULL, 16);
        const LtProperty *back = lt_ext_prop(labels[n - 1]);
        CHECK(back->dispY == 40 + 13 * 11, "twelve rows 13 lines apart: Back at %d", back->dispY);
        CHECK(back->dispY + back->dispH <= 226, "Back's box ends at %d", back->dispY + back->dispH);
        checkPageFits(UI_PAGE_MAIN, "Main (title, developer mode)");
    }
    ico_opt_set_developer_mode(0);
    for (int k = 0; k < 4; k++) {
        frame(0); /* the page refreshes */
    }
    CHECK(!rowShown(UI_PAGE_MAIN, UI_OPT_DUMP_TEXTURES), "hidden again");
    /* the Display page with the row, from the title */
    openPage(mainL, 0, UI_PAGE_DISPLAY);
    CHECK(rowShown(UI_PAGE_DISPLAY, UI_OPT_TEXTURE_PACK), "the Texture pack row");
    checkPageFits(UI_PAGE_DISPLAY, "Display with Texture pack (title)");
    ui_settings_set_texture_pack_count(NULL);
    useConfig("version = 1\n");
}

/* Display > Model pack (title only; "None installed" without the
   hook or with a count of 0, the step then doing nothing; On/Off with
   one), its note, Dump models on the main page in developer mode only
   (switched off with it), and the [video] keys' round trip. */
static void testModelPack(void)
{
    IcoVideoOptions o;

    useConfig("version = 1\n");
    fakeTables();
    lt_ext_reset();
    ui_settings_reset();
    ui_set_language(UI_LANG_EN);
    ui_settings_install();
    ui_settings_set_model_pack_count(NULL);
    ico_opt_set_developer_mode(0);
    ico_video_get(&o);
    CHECK(o.modelPack == 1 && o.dumpModels == 0, "defaults: model pack on, dump off");
    CHECK(strcmp(ui_settings_value_text(UI_OPT_MODEL_PACK), "None installed") == 0,
          "no hook: None installed (%s)", ui_settings_value_text(UI_OPT_MODEL_PACK));
    ui_settings_set_model_pack_count(fakePackCount);
    s_packCount = 0;
    CHECK(strcmp(ui_settings_value_text(UI_OPT_MODEL_PACK), "None installed") == 0,
          "a count of 0: None installed");
    ui_settings_step(UI_OPT_MODEL_PACK, 1);
    ico_video_get(&o);
    CHECK(o.modelPack == 1, "none installed: the step does nothing");
    s_packCount = 12;
    CHECK(strcmp(ui_settings_value_text(UI_OPT_MODEL_PACK), "On") == 0, "a pack: On");
    ui_settings_step(UI_OPT_MODEL_PACK, 1);
    ico_video_get(&o);
    CHECK(o.modelPack == 0 && strcmp(ui_settings_value_text(UI_OPT_MODEL_PACK), "Off") == 0,
          "Right: Off");
    ui_settings_step(UI_OPT_MODEL_PACK, -1);
    ico_video_get(&o);
    CHECK(o.modelPack == 1 && strcmp(ui_settings_value_text(UI_OPT_MODEL_PACK), "On") == 0,
          "Left: On");
    CHECK(o.texturePack == 1, "the texture pack row untouched");

    /* shown from the title only; both entries fit */
    int mainL = enterMain(1);
    openPage(mainL, 0, UI_PAGE_DISPLAY);
    CHECK(rowShown(UI_PAGE_DISPLAY, UI_OPT_MODEL_PACK), "title: the Model pack row");
    checkPageFits(UI_PAGE_DISPLAY, "Display with Model pack (title)");
    {
        int tn = rowWithPrefix(UI_PAGE_DISPLAY, "Model packs:");
        CHECK(tn >= 0 && strchr(lt_ext_row_text(tn), '\n') == NULL,
              "the Model pack note: one line");
    }
    mainL = enterMain(0);
    openPage(mainL, 0, UI_PAGE_DISPLAY);
    CHECK(!rowShown(UI_PAGE_DISPLAY, UI_OPT_MODEL_PACK), "pause menu: the Model pack row hidden");
    checkPageFits(UI_PAGE_DISPLAY, "Display (pause)");

    /* Dump models: developer mode only, switched off with it */
    mainL = enterMain(1);
    CHECK(!rowShown(UI_PAGE_MAIN, UI_OPT_DUMP_MODELS), "dump models row hidden");
    ui_settings_step(UI_OPT_DEVELOPER, 1);
    CHECK(ico_opt_developer_mode(), "Developer on");
    for (int k = 0; k < 4; k++) {
        frame(0); /* the page refreshes */
    }
    CHECK(rowShown(UI_PAGE_MAIN, UI_OPT_DUMP_MODELS), "dump models row shown in developer mode");
    checkPageFits(UI_PAGE_MAIN, "Main (title, developer mode, Dump models)");
    CHECK(strcmp(ui_settings_value_text(UI_OPT_DUMP_MODELS), "Off") == 0, "dump models: Off");
    ui_settings_step(UI_OPT_DUMP_MODELS, 1);
    ico_video_get(&o);
    CHECK(o.dumpModels == 1 && strcmp(ui_settings_value_text(UI_OPT_DUMP_MODELS), "On") == 0,
          "dump models: On");
    {
        int dn = rowWithPrefix(UI_PAGE_MAIN, "For pack makers: saves each model");
        const char *nl = dn >= 0 ? strchr(lt_ext_row_text(dn), '\n') : NULL;
        CHECK(dn >= 0, "the dump models note");
        CHECK(nl == NULL || strchr(nl + 1, '\n') == NULL,
              "the dump models note: at most two lines");
    }
    char p[1100];
    CHECK(ui_settings_save() == 0, "save");
    path(p, sizeof(p), "settings_test.toml");
    IcoToml *t = ico_toml_load(p);
    CHECK(t && ico_toml_get_bool(t, "video.model_pack", 0) == 1 &&
              ico_toml_get_bool(t, "video.dump_models", 0) == 1,
          "saved: model_pack, dump_models");
    ico_toml_free(t);
    ui_settings_step(UI_OPT_DEVELOPER, 1);
    ico_video_get(&o);
    CHECK(!ico_opt_developer_mode() && o.dumpModels == 0, "Developer off: dump models off too");
    useConfig("version = 1\n[video]\nmodel_pack = false\n");
    ico_video_get(&o);
    CHECK(!o.modelPack, "model_pack = false read back");
    CHECK(strcmp(ui_settings_value_text(UI_OPT_MODEL_PACK), "Off") == 0, "the row: Off");
    ui_settings_set_model_pack_count(NULL);
    useConfig("version = 1\n");
}

/* issue 11: Options > Effects.  The link sits under Display on the main page,
   the six rows read On by default, a step flips one and ui_settings_save
   writes [video] effect_*; the Main page fits in all four entries */
static void testEffects(void)
{
    static const struct {
        UiSettingsOpt opt;
        const char *key;
    } kFx[] = {{UI_OPT_EFFECT_GLOW, "video.effect_glow"},
               {UI_OPT_EFFECT_DEPTH_OF_FIELD, "video.effect_depth_of_field"},
               {UI_OPT_EFFECT_SOFTENING, "video.effect_softening"},
               {UI_OPT_EFFECT_MOTION_BLUR, "video.effect_motion_blur"},
               {UI_OPT_EFFECT_FOG, "video.effect_fog"},
               {UI_OPT_EFFECT_CINEMATIC_BARS, "video.effect_cinematic_bars"}};

    useConfig("version = 1\n");
    int mainL = enterMain(1);
    int fxL = openPage(mainL, 1, UI_PAGE_EFFECTS);
    CHECK(fxL == ui_settings_page_layout(UI_PAGE_EFFECTS), "the Effects link opens the page");
    CHECK(rowWithPrefix(UI_PAGE_EFFECTS, "Effects") >= 0, "the page is called Effects");
    checkPageFits(UI_PAGE_EFFECTS, "Effects");
    CHECK(rowWithPrefix(UI_PAGE_EFFECTS, "The game") >= 0, "the Effects note");
    press(0x10);
    CHECK(settle(mainL, 60), "Effects: back");

    for (int i = 0; i < 6; i++) {
        CHECK(strcmp(ui_settings_value_text(kFx[i].opt), "On") == 0, "effect %d: On by default", i);
        ui_settings_step(kFx[i].opt, 1);
        CHECK(strcmp(ui_settings_value_text(kFx[i].opt), "Off") == 0, "effect %d: Off", i);
        CHECK(ui_settings_save() == 0, "save %d", i);
        char p[1100];
        path(p, sizeof(p), "settings_test.toml");
        IcoToml *t = ico_toml_load(p);
        CHECK(t != NULL, "config %d", i);
        for (int j = 0; t && j < 6; j++) {
            CHECK(ico_toml_get_bool(t, kFx[j].key, 1) == (j > i), "%s after step %d", kFx[j].key,
                  i);
        }
        ico_toml_free(t);
    }
    for (int i = 0; i < 6; i++) {
        ui_settings_step(kFx[i].opt, -1);
        CHECK(strcmp(ui_settings_value_text(kFx[i].opt), "On") == 0, "effect %d: On again", i);
    }

    for (int title = 1; title >= 0; title--) {
        for (int dev = 0; dev <= 1; dev++) {
            ico_opt_set_developer_mode(dev);
            enterMain(title);
            for (int k = 0; k < 4; k++) {
                frame(0);
            }
            checkPageFits(UI_PAGE_MAIN, title ? (dev ? "Main (title, developer)" : "Main (title)")
                                              : (dev ? "Main (pause, developer)" : "Main (pause)"));
        }
    }
    ico_opt_set_developer_mode(0);
    useConfig("version = 1\n");
}

/* Settings > Controls, the touch overlay's rows.  Hidden
   without a touch screen (no query, or one that says none), shown with
   one on both entries, the page still fitting; Auto, Medium and 75 % by
   default; a step changes the live table and ui_settings_save writes
   [input] touch_mode, touch_size and touch_opacity, which read back. */
static int s_touchAnswer;

static int fakeTouch(void)
{
    return s_touchAnswer;
}

static void testTouch(void)
{
    static const UiSettingsOpt kRows[3] = {UI_OPT_TOUCH_MODE, UI_OPT_TOUCH_SIZE,
                                           UI_OPT_TOUCH_OPACITY};
    IcoBindings *b = ico_input_live_bindings();
    char p[1100];

    for (int title = 1; title >= 0; title--) {
        ui_settings_set_touch_query(NULL);
        int mainL = enterMain(title);
        openPage(mainL, 4, UI_PAGE_CONTROLS);
        for (int i = 0; i < 3; i++) {
            CHECK(!rowShown(UI_PAGE_CONTROLS, kRows[i]), "title %d: touch row %d hidden (no query)",
                  title, i);
        }
        s_touchAnswer = 0;
        ui_settings_set_touch_query(fakeTouch);
        for (int k = 0; k < 4; k++) {
            frame(0); /* the page refreshes */
        }
        for (int i = 0; i < 3; i++) {
            CHECK(!rowShown(UI_PAGE_CONTROLS, kRows[i]),
                  "title %d: touch row %d hidden (no touch screen)", title, i);
        }
        s_touchAnswer = 1;
        for (int k = 0; k < 4; k++) {
            frame(0);
        }
        for (int i = 0; i < 3; i++) {
            CHECK(rowShown(UI_PAGE_CONTROLS, kRows[i]),
                  "title %d: touch row %d shown with a touch screen", title, i);
        }
        checkPageFits(UI_PAGE_CONTROLS,
                      title ? "Controls with touch (title)" : "Controls with touch (pause)");
        CHECK(rowWithPrefix(UI_PAGE_CONTROLS, "On-screen buttons.") >= 0, "the touch note");
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
    s_touchAnswer = 0;
    useConfig("version = 1\n");
    ico_input_reload_bindings(b);
}

int main(int argc, char **argv)
{
    snprintf(s_dir, sizeof(s_dir), "%s", argc > 1 ? argv[1] : ".");
    setEnv("LC_ALL", NULL);
    setEnv("LANG", "en_GB.UTF-8");
    testBuild();
    testBudget();
    testRepoint();
    testPlacement();
    testNavigation();
    testPhoto();
    testPauseStats();
    testNewGameScreen();
    testQuit();
    testCirclePortScreens();
    testCircleGameMenu();
    testValues();
    testAudio();
    testVideoGate();
    testFramerate();
    testPreset();
    testCapture();
    testBootSkip();
    testGlyphSources();
    testTitleOptionsWord();
    testGallery();
    testList();
    testGameOptions();
    testTexturePack();
    testTexturePackNoteLines();
    testModelPack();
    testEffects();
    testTouch();
    if (failures) {
        printf("settings_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("settings_test: ok\n");
    return 0;
}

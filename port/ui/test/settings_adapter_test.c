/* settings_adapter_test.c: Options > Graphics > Graphics card
 * (settings_graphics.c, ui_settings_set_adapter_names), over the shared
 * fixture (settings_fixture.h) with a fake UiGraphicsHost:
 * - no card names and one name: the row hidden, and with one renderer the
 *   Graphics link too; two names: the link shows (one renderer: the
 *   Graphics API row hidden), the row shows with its words and note, the
 *   page fits;
 * - Right goes round Automatic and the names, writing [video] adapter
 *   ("" for Automatic), and leaving the page writes the file;
 * - a saved name the list lacks shows as written and steps from
 *   Automatic;
 * - Apply: hidden on the card running and on Automatic when Automatic
 *   picks it, shown on another card; Cross asks the host for the renderer
 *   running on that card; Keep writes it; Revert asks for the card before
 *   and puts the value back; the countdown hides the row; Automatic on a
 *   run started on another card asks for "" (Automatic); a card that did
 *   not start is the card popup and the value the page found back;
 * - the strings, translated in every language.
 * CPU only.  Exit 0 on success, 1 on a failure. */
#include "settings_fixture.h"
#include "popup.h"

static const char *const kCards[] = {"NVIDIA GeForce RTX 3060 Laptop GPU",
                                     "Intel(R) UHD Graphics 630"};

/* the fake host: Vulkan on s_card */
static char s_card[256];
static char s_askedName[16][16], s_askedCard[16][256];
static int s_askedWhy[16];
static int s_begins, s_kepts;
static int s_answer = UI_GFX_SWITCH_DONE;

static const char *fakeRunning(void)
{
    return "vulkan";
}

static const char *fakeRunningAdapter(void)
{
    return s_card;
}

static int fakeBegin(const char *name, const char *adapter, int why)
{
    const int i = s_begins < 16 ? s_begins : 15;
    snprintf(s_askedName[i], sizeof(s_askedName[i]), "%s", name);
    snprintf(s_askedCard[i], sizeof(s_askedCard[i]), "%s", adapter != NULL ? adapter : "(null)");
    s_askedWhy[i] = why;
    s_begins++;
    if (s_answer == UI_GFX_SWITCH_DONE) {
        /* "" is Automatic: the first card here */
        snprintf(s_card, sizeof(s_card), "%s",
                 adapter != NULL && adapter[0] != '\0' ? adapter : kCards[0]);
    }
    return s_answer;
}

static int fakePoll(void)
{
    return UI_GFX_SWITCH_DONE;
}

static void fakeKept(void)
{
    s_kepts++;
}

static const UiGraphicsHost kHost = {fakeRunning, fakeBegin, fakePoll,
                                     fakeKept,    NULL,      fakeRunningAdapter};

static int rowOn(UiSettingsOpt opt)
{
    const int row = ui_settings_row_of(UI_PAGE_GRAPHICS, opt);
    return row >= 0 && !lt_ext_prop(row)->defaultMask;
}

static const char *value(void)
{
    return ui_settings_value_text(UI_OPT_GRAPHICS_ADAPTER);
}

/* the row's value is name, or name cut to fit the row with an ellipsis
   (U+2026, three bytes) at its end */
static int valueIs(const char *name)
{
    const char *v = value();
    size_t n = strlen(v);
    if (strcmp(v, name) == 0) {
        return 1;
    }
    if (n >= 3 && (unsigned char)v[n - 3] == 0xE2 && (unsigned char)v[n - 2] == 0x80 &&
        (unsigned char)v[n - 1] == 0xA6) {
        return strncmp(v, name, n - 3) == 0;
    }
    return 0;
}

/* the first 15 bytes of a translated string (whole UTF-8 sequences) for
   noteStarting */
static const char *head15(const char *s)
{
    static char buf[16];
    size_t n = strlen(s) < 15 ? strlen(s) : 15;
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) {
        n--;
    }
    memcpy(buf, s, n);
    buf[n] = '\0';
    return buf;
}

static const char *key(void)
{
    return ico_config_get_string("video.adapter", "?");
}

static void vsyncs(int n)
{
    for (int i = 0; i < n; i++) {
        ui_settings_vsync();
    }
}

static void testRow(void)
{
    static const char *const kOne[] = {"vulkan"};
    char p[1100];

    ui_settings_set_graphics_host(NULL);
    ui_settings_set_backend_names(kOne, 1);
    ui_settings_set_adapter_names(NULL, 0, NULL, NULL);
    useConfig("version = 1\n");
    int mainL = enterMain(1);
    frame(0);
    int link = mainLink(UI_PAGE_GRAPHICS);
    CHECK(link >= 0 && !ui_settings_row_shown(link), "no cards, one renderer: no Graphics link");
    CHECK(!ui_settings_opt_shown(UI_OPT_GRAPHICS_ADAPTER), "no cards: no row");
    ui_settings_set_adapter_names(kCards, 1, kCards[0], kCards[0]);
    mainL = enterMain(1);
    frame(0);
    link = mainLink(UI_PAGE_GRAPHICS);
    CHECK(link >= 0 && !ui_settings_row_shown(link), "one card, one renderer: no Graphics link");
    CHECK(!ui_settings_opt_shown(UI_OPT_GRAPHICS_ADAPTER), "one card: no row");

    /* two cards: the link, the row, no Graphics API row with one renderer */
    ui_settings_set_adapter_names(kCards, 2, kCards[0], kCards[0]);
    mainL = enterMain(1);
    frame(0);
    link = mainLink(UI_PAGE_GRAPHICS);
    CHECK(link >= 0 && ui_settings_row_shown(link), "two cards: the Graphics link shows");
    const int pageL = openPage(mainL, -1, UI_PAGE_GRAPHICS);
    const int row = ui_settings_row_of(UI_PAGE_GRAPHICS, UI_OPT_GRAPHICS_ADAPTER);
    CHECK(row >= 0 && rowOn(UI_OPT_GRAPHICS_ADAPTER) &&
              strcmp(lt_ext_row_text(row), ui_str(UI_STR_OPT_GRAPHICS_ADAPTER)) == 0,
          "the Graphics card row");
    CHECK(!ui_settings_opt_shown(UI_OPT_GRAPHICS_API) && !rowOn(UI_OPT_GRAPHICS_API),
          "one renderer: no Graphics API row");
    CHECK(noteStarting(UI_PAGE_GRAPHICS, head15(ui_str(UI_STR_GRAPHICS_ADAPTER_NOTE))) >= 0,
          "the note");
    checkPageFits(UI_PAGE_GRAPHICS, "Graphics (one renderer, two cards)");
    CHECK(strcmp(value(), ui_str(UI_STR_VAL_GRAPHICS_AUTOMATIC)) == 0, "no key: Automatic (%s)",
          value());

    /* Right: each card, then Automatic again */
    for (int i = 0; i < 2; i++) {
        ui_settings_step(UI_OPT_GRAPHICS_ADAPTER, 1);
        CHECK(valueIs(kCards[i]), "Right %d shows %s (%s)", i, kCards[i], value());
        CHECK(strcmp(key(), kCards[i]) == 0, "Right %d writes %s (%s)", i, kCards[i], key());
    }
    ui_settings_step(UI_OPT_GRAPHICS_ADAPTER, 1);
    CHECK(strcmp(value(), ui_str(UI_STR_VAL_GRAPHICS_AUTOMATIC)) == 0 && strcmp(key(), "") == 0,
          "Right wraps to Automatic, an empty name (%s)", key());
    ui_settings_step(UI_OPT_GRAPHICS_ADAPTER, -1);
    CHECK(strcmp(key(), kCards[1]) == 0, "Left wraps to the last (%s)", key());

    /* leaving the page writes the file */
    press(LT_PAD_TRIANGLE);
    CHECK(settle(mainL, 60), "Back leaves the page (%d)", pageL);
    CHECK(ui_settings_save() == 0, "save");
    path(p, sizeof(p), "settings_test.toml");
    IcoToml *t = ico_toml_load(p);
    CHECK(t != NULL, "the file");
    if (t) {
        const char *a = ico_toml_get(t, "video.adapter");
        CHECK(a != NULL && strcmp(a, kCards[1]) == 0, "[video] adapter = %s (%s)", kCards[1],
              a ? a : "-");
        ico_toml_free(t);
    }

    /* a saved name the list lacks: as written, and Right from Automatic */
    useConfig("version = 1\n[video]\nadapter = \"AMD Radeon RX 6600\"\n");
    enterMainKeep(1);
    CHECK(strcmp(value(), "AMD Radeon RX 6600") == 0, "an unlisted name shows as written (%s)",
          value());
    ui_settings_step(UI_OPT_GRAPHICS_ADAPTER, 1);
    CHECK(strcmp(key(), kCards[0]) == 0, "an unlisted name steps from Automatic (%s)", key());
    ui_settings_set_adapter_names(NULL, 0, NULL, NULL);
    ui_settings_set_backend_names(NULL, 0);
}

static void testApply(void)
{
    static const char *const kOne[] = {"vulkan"};
    char p[1100];

    snprintf(s_card, sizeof(s_card), "%s", kCards[0]);
    s_answer = UI_GFX_SWITCH_DONE;
    s_begins = s_kepts = 0;
    ui_popup_reset();
    ui_popup_set_vsync_hz(60);
    ui_settings_set_backend_names(kOne, 1);
    ui_settings_set_adapter_names(kCards, 2, kCards[0], kCards[0]);
    ui_settings_set_graphics_host(&kHost);
    useConfig("version = 1\n");
    const int mainL = enterMain(1);
    frame(0);
    int pageL = openPage(mainL, -1, UI_PAGE_GRAPHICS);
    const int apply = ui_settings_row_of(UI_PAGE_GRAPHICS, UI_OPT_GRAPHICS_APPLY);
    const int keep = ui_settings_row_of(UI_PAGE_GRAPHICS, UI_OPT_GRAPHICS_KEEP);

    /* Automatic picks the card running, and the card itself: no Apply */
    CHECK(!rowOn(UI_OPT_GRAPHICS_APPLY), "Automatic on its own card: no Apply");
    ui_settings_step(UI_OPT_GRAPHICS_ADAPTER, 1);
    frame(0);
    CHECK(!rowOn(UI_OPT_GRAPHICS_APPLY), "the card running: no Apply");

    /* another card: Apply; Cross asks for vulkan on it; the countdown */
    ui_settings_step(UI_OPT_GRAPHICS_ADAPTER, 1);
    frame(0);
    CHECK(rowOn(UI_OPT_GRAPHICS_APPLY), "another card: Apply shows");
    lt_ext_layout(pageL)->curItem = apply;
    press(0x40);
    CHECK(s_begins == 1 && strcmp(s_askedName[0], "vulkan") == 0 &&
              strcmp(s_askedCard[0], kCards[1]) == 0 && s_askedWhy[0] == UI_GFX_WHY_APPLY,
          "Apply asks for vulkan on %s (%d, %s, %s)", kCards[1], s_begins, s_askedName[0],
          s_askedCard[0]);
    CHECK(rowOn(UI_OPT_GRAPHICS_KEEP) && !rowOn(UI_OPT_GRAPHICS_ADAPTER),
          "the countdown hides the row");
    CHECK(strcmp(key(), "") == 0, "on trial: the config holds the value found (%s)", key());

    /* the countdown runs out: the card before, the value as it was */
    vsyncs(600);
    CHECK(s_begins == 2 && strcmp(s_askedCard[1], kCards[0]) == 0 &&
              s_askedWhy[1] == UI_GFX_WHY_TIMEOUT && strcmp(s_card, kCards[0]) == 0,
          "10 s: back on %s (%d, %s)", kCards[0], s_begins, s_askedCard[1]);
    CHECK(strcmp(key(), "") == 0 && s_kepts == 0, "nothing kept (%s)", key());

    /* Keep: written */
    frame(0);
    ui_settings_step(UI_OPT_GRAPHICS_ADAPTER, -1); /* Automatic -> the last card */
    frame(0);
    CHECK(strcmp(key(), kCards[1]) == 0 && rowOn(UI_OPT_GRAPHICS_APPLY), "the second card again");
    lt_ext_layout(pageL)->curItem = apply;
    press(0x40);
    lt_ext_layout(pageL)->curItem = keep;
    press(0x40);
    CHECK(s_kepts == 1 && strcmp(s_card, kCards[1]) == 0 && strcmp(key(), kCards[1]) == 0,
          "Keep: on %s, the value %s", s_card, key());
    press(LT_PAD_TRIANGLE);
    CHECK(settle(mainL, 60), "Back leaves the page");
    CHECK(ui_settings_save() == 0, "save");
    path(p, sizeof(p), "settings_test.toml");
    IcoToml *t = ico_toml_load(p);
    if (t) {
        const char *a = ico_toml_get(t, "video.adapter");
        CHECK(a != NULL && strcmp(a, kCards[1]) == 0, "Keep: [video] adapter = %s (%s)", kCards[1],
              a ? a : "-");
        ico_toml_free(t);
    } else {
        CHECK(0, "the file");
    }

    /* Revert: Automatic chosen on the second card asks for "", then the
       second card again, the value back */
    pageL = openPage(mainL, -1, UI_PAGE_GRAPHICS);
    ui_settings_step(UI_OPT_GRAPHICS_ADAPTER, 1); /* the last card -> Automatic */
    frame(0);
    CHECK(strcmp(key(), "") == 0 && rowOn(UI_OPT_GRAPHICS_APPLY),
          "Automatic (the first card) on the second: Apply");
    lt_ext_layout(pageL)->curItem = apply;
    press(0x40);
    CHECK(s_begins == 4 && strcmp(s_askedCard[3], "") == 0, "Apply asks for Automatic (%s)",
          s_askedCard[3]);
    press(0x40); /* the cursor is on Revert */
    CHECK(s_begins == 5 && strcmp(s_askedCard[4], kCards[1]) == 0 &&
              s_askedWhy[4] == UI_GFX_WHY_REVERT && strcmp(key(), kCards[1]) == 0,
          "Revert: back on %s, the value %s", s_askedCard[4], key());

    /* a card that did not start: the card popup, the value found back */
    ui_popup_reset();
    ui_settings_step(UI_OPT_GRAPHICS_ADAPTER, -1); /* the last card -> the first */
    frame(0);
    s_answer = UI_GFX_SWITCH_FAILED;
    lt_ext_layout(pageL)->curItem = apply;
    press(0x40);
    {
        char want[300];
        snprintf(want, sizeof(want), "The graphics card %s could not start; %s is kept", kCards[0],
                 kCards[1]);
        CHECK(strcmp(ui_popup_title(), want) == 0, "failed: \"%s\"", ui_popup_title());
    }
    frame(0);
    CHECK(strcmp(key(), kCards[1]) == 0 && !rowOn(UI_OPT_GRAPHICS_KEEP),
          "failed: the value back to %s (%s), no countdown", kCards[1], key());
    s_answer = UI_GFX_SWITCH_DONE;

    /* the strings, translated in every language */
    static const UiStrId kIds[] = {UI_STR_OPT_GRAPHICS_ADAPTER, UI_STR_GRAPHICS_ADAPTER_NOTE,
                                   UI_STR_GRAPHICS_ADAPTER_FAILED};
    for (int l = 1; l < UI_LANG_COUNT; l++) {
        for (unsigned i = 0; i < sizeof(kIds) / sizeof(kIds[0]); i++) {
            const char *tr = ui_str_in((UiLang)l, kIds[i]);
            CHECK(tr[0] != '\0' && strcmp(tr, ui_str_in(UI_LANG_EN, kIds[i])) != 0,
                  "language %d, string %d translated", l, kIds[i]);
        }
    }
    ui_settings_set_graphics_host(NULL);
    ui_settings_set_adapter_names(NULL, 0, NULL, NULL);
    ui_settings_set_backend_names(NULL, 0);
    ui_popup_set_vsync_hz(0);
    ui_popup_reset();
}

int main(int argc, char **argv)
{
    snprintf(s_dir, sizeof(s_dir), "%s", argc > 1 ? argv[1] : ".");
    setEnv("LC_ALL", NULL);
    setEnv("LANG", "en_GB.UTF-8");
    if (argc > 2) {
        /* the other discs: the row on their numbers */
        CHECK(ico_region_select(argv[2]) == 0, "the %s region", argv[2]);
        testRow();
        CHECK(ico_region_select("pal") == 0, "the European region again");
        printf("settings_adapter_test %s: %s\n", argv[2], failures ? "failed" : "ok");
        return failures ? 1 : 0;
    }
    testRow();
    testApply();
    if (failures) {
        printf("settings_adapter_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("settings_adapter_test: ok\n");
    return 0;
}

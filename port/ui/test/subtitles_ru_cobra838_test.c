/* subtitles_ru_cobra838_test.c: the Russian (cobra838) community
 * translation over the shared fixture (settings_fixture.h), one disc per
 * run: no argument the European disc, else the slug (ctests
 * subtitles_ru_cobra838, subtitles_ru_cobra838_<slug> for the Japanese,
 * Korean and Asian discs).  CPU only (measure-only text).  A copy of
 * subtitles_ru_test.c (bob-ttz's) for the second translation
 * (docs/TRANSLATING.md):
 *   - the subtitle table (port/game/subtitles_ru_cobra838.c): the 42 records
 *     of his PR's table in order, the eight second-play entries Yorda's
 *     (009, 029, 031, 048, 049, 086, 091, 112, as bob-ttz's table flags
 *     them), every entry one or two non-empty lines of at most
 *     ICO_SUBTITLE_LINE_MAX characters, valid UTF-8 without controls, each
 *     character a glyph of the fonts;
 *   - ui_subtitle_find over the 42 x {first play, second play}: the words
 *     on both plays, NULL on the first for Yorda's; records outside it NULL;
 *     ui_subtitle_table: each translation its own table;
 *   - ui_subtitle_words through [game] text_language = "ru-cobra838" (the
 *     fixture's config): his words (not bob-ttz's) on the European disc in
 *     English only; nothing with no key, another word, a disc language, or
 *     on any other disc;
 *   - ui_subtitle_layout of every entry: one or two lines about the band's
 *     middle, the em UI_SUBTITLE_EM or smaller so the widest line fits
 *     UI_SUBTITLE_ROOM, never under 60 % of it, every line inside the band;
 *   - the Language row (the European disc): eight entries, the five disc
 *     languages, Russian (bob-ttz), then Russian (cobra838): the English
 *     game data, [game] text_language = "ru-cobra838", the Menu language's
 *     Auto in his Russian, the game's menu words his over the English sheets
 *     and his subtitles as text; the step after it is Português (Brasil)
 *     (ichagas7), then English again, the key cleared, and eight steps come
 *     back to where they started.  On the
 *     other discs the row is not shown and the key does nothing;
 *   - the menu words' strips (menu_font.c, ui__menu_strip_raster): the five
 *     sheets' own words byte for byte with or without the translation in
 *     force but for the English sheet, which takes his Russian words. */
#include "settings_fixture.h"
#include "subtitle_text.h"
#include "ui_internal.h" /* ui__menu_strip_raster */
#include "ui_lang.h"

/* his table's records (PR 180's port/ui/subtitles_rus.c) and the second-play
   ones */
static const short kBlocks[42] = {0,  1,  2,  4,  5,  6,  7,   8,   9,   11,  12,  13,  19,  25,
                                  26, 27, 29, 31, 34, 35, 36,  37,  38,  43,  48,  49,  86,  88,
                                  91, 93, 96, 97, 98, 99, 100, 104, 105, 106, 109, 110, 111, 112};

static int secondOnly(int block)
{
    static const short k[8] = {9, 29, 31, 48, 49, 86, 91, 112};
    for (int i = 0; i < 8; i++) {
        if (k[i] == block) {
            return 1;
        }
    }
    return 0;
}

static void testTable(void)
{
    int n = 0;
    const IcoSubtitle *t = ui_subtitle_table(UI_LANG_RU_COBRA838, &n);
    CHECK(t == ico_subtitles_ru_cobra838 && n == 42 && ico_subtitles_ru_cobra838_count == 42,
          "the table: %d entries", n);
    CHECK(ui_subtitle_table(UI_LANG_RU_BOBTTZ, NULL) == ico_subtitles_ru_bobttz &&
              ico_subtitles_ru_bobttz != ico_subtitles_ru_cobra838,
          "bob-ttz's translation keeps its own table");
    /* his words, not bob-ttz's (PR 180's second table was a copy of his) */
    CHECK(strcmp(ico_subtitles_ru_cobra838[1].words,
                 "Не держи на нас зла.\nТак нужно ради блага деревни.") == 0 &&
              strcmp(ico_subtitles_ru_cobra838[1].words, ico_subtitles_ru_bobttz[1].words) != 0,
          "record 1: his words (%s)", ico_subtitles_ru_cobra838[1].words);
    for (int l = 0; l < UI_LANG_COUNT; l++) {
        int c = 7;
        CHECK(ui_subtitle_table(l, &c) == NULL && c == 0, "language %d has no table", l);
    }
    CHECK(ui_subtitle_table(UI_LANG_TABLES, NULL) == NULL, "out of range: none");
    for (int i = 0; t && i < n && i < 42; i++) {
        const IcoSubtitle *e = &t[i];
        CHECK(e->block == kBlocks[i], "entry %d: record %d, want %d", i, e->block, kBlocks[i]);
        CHECK(e->secondOnly == secondOnly(e->block), "record %d: second-play %d", e->block,
              e->secondOnly);
        CHECK(e->words && e->words[0], "record %d: words", e->block);
        if (!e->words) {
            continue;
        }
        int lines = 1, chars = 0, longest = 0;
        const char *p = e->words;
        uint32_t cp;
        while ((cp = ui_utf8_next(&p)) != 0) {
            if (cp == '\n') {
                CHECK(chars > 0, "record %d: an empty line", e->block);
                longest = chars > longest ? chars : longest;
                chars = 0;
                lines++;
                continue;
            }
            chars++;
            CHECK(cp != 0xFFFD && cp >= 0x20 && !(cp >= 0x7F && cp < 0xA0),
                  "record %d: U+%04X is not text", e->block, (unsigned)cp);
            CHECK(ui_font_has_glyph(cp), "record %d: no glyph for U+%04X", e->block, (unsigned)cp);
        }
        CHECK(chars > 0, "record %d: an empty last line", e->block);
        longest = chars > longest ? chars : longest;
        CHECK(lines <= ICO_SUBTITLE_LINES && longest <= ICO_SUBTITLE_LINE_MAX,
              "record %d: %d lines, the longest %d characters", e->block, lines, longest);
    }
    /* 42 x {first play, second play} */
    for (int i = 0; i < 42; i++) {
        const int b = kBlocks[i];
        const char *first = ui_subtitle_find(t, n, b, 0);
        const char *second = ui_subtitle_find(t, n, b, 1);
        CHECK(second != NULL && second == t[i].words, "record %d: the second play's words", b);
        CHECK(secondOnly(b) ? first == NULL : first == second, "record %d: the first play's %s", b,
              secondOnly(b) ? "picture (Yorda's script)" : "words");
    }
    CHECK(ui_subtitle_find(t, n, 3, 1) == NULL && ui_subtitle_find(t, n, 200, 0) == NULL &&
              ui_subtitle_find(t, n, -1, 0) == NULL && ui_subtitle_find(NULL, 0, 0, 0) == NULL,
          "a record outside the table keeps its picture");
}

static void testWords(int pal)
{
    /* no key */
    useConfig("version = 1\n");
    NonLinearCameraMove = pal ? 2 : 0;
    CHECK(ui_subtitle_words(0, 0) == NULL, "no key: the picture");
    useConfig("version = 1\n[game]\ntext_language = \"ru-cobra838\"\n");
    NonLinearCameraMove = pal ? 2 : 0;
    const char *w = ui_subtitle_words(1, 0);
    if (pal) {
        CHECK(w != NULL && w == ico_subtitles_ru_cobra838[1].words, "the key: record 1 as text");
        CHECK(ui_subtitle_words(9, 0) == NULL && ui_subtitle_words(9, 1) != NULL,
              "Yorda's record 9: her script first, the words on a cleared game");
        CHECK(ui_subtitle_words(3, 0) == NULL, "record 3, not translated: the picture");
        for (int g = 3; g <= 6; g++) {
            NonLinearCameraMove = g;
            CHECK(ui_subtitle_words(1, 0) == NULL, "the game in language %d: the picture", g);
        }
        NonLinearCameraMove = 2;
    } else {
        CHECK(w == NULL, "%s: the disc's own pictures", ico_region_cur()->slug);
    }
    useConfig("version = 1\n[game]\ntext_language = \"klingon\"\n");
    NonLinearCameraMove = pal ? 2 : 0;
    CHECK(ui_subtitle_words(1, 0) == NULL, "another word: the picture");
    useConfig("version = 1\n");
}

static void testLayout(void)
{
    for (int i = 0; i < ico_subtitles_ru_cobra838_count; i++) {
        const char *words = ico_subtitles_ru_cobra838[i].words;
        UiSubtitleLayout lay;
        CHECK(ui_subtitle_layout(words, &lay), "record %d: laid out",
              ico_subtitles_ru_cobra838[i].block);
        const int lines = strchr(words, '\n') ? 2 : 1;
        CHECK(lay.lines == lines && lay.x == 0.5f * (UI_SUBTITLE_X0 + UI_SUBTITLE_X1),
              "record %d: %d lines about x %.1f", ico_subtitles_ru_cobra838[i].block, lay.lines,
              (double)lay.x);
        CHECK(lay.size <= UI_SUBTITLE_EM && lay.size >= 0.6f * UI_SUBTITLE_EM,
              "record %d: the em %.2f", ico_subtitles_ru_cobra838[i].block, (double)lay.size);
        CHECK(ui_measure_menu_text(lay.size, words) <= UI_SUBTITLE_ROOM + 0.5f,
              "record %d: the widest line %.1f in %.0f", ico_subtitles_ru_cobra838[i].block,
              (double)ui_measure_menu_text(lay.size, words), (double)UI_SUBTITLE_ROOM);
        float asc = 0.0f, desc = 0.0f, cap = 0.0f;
        ui_menu_font_metrics(lay.size, &asc, &desc, &cap);
        for (int k = 0; k < lay.lines; k++) {
            CHECK(lay.y[k] - 0.5f * cap >= UI_SUBTITLE_Y0 &&
                      lay.y[k] + 0.5f * cap + desc <= UI_SUBTITLE_Y1,
                  "record %d line %d: y %.1f inside the band", ico_subtitles_ru_cobra838[i].block,
                  k, (double)lay.y[k]);
        }
        /* headless: measured, nothing drawn, nothing broken */
        const unsigned char rgba[4] = {128, 128, 128, 128};
        ui_subtitle_draw(words, ico_subtitles_ru_cobra838[i].block, rgba);
    }
    UiSubtitleLayout lay;
    CHECK(!ui_subtitle_layout("", &lay) && !ui_subtitle_layout(NULL, &lay), "no words: no layout");
}

static void testLanguageRow(int pal)
{
    static const char *const kNames[5] = {"English", "Français", "Deutsch", "Italiano", "Español"};
    useConfig("version = 1\n[ui]\nlanguage = \"auto\"\n");
    enterMainKeep(1); /* the game's word 2 on every disc (the fixture's) */
    if (!pal) {
        CHECK(!ui_settings_opt_shown(UI_OPT_LANGUAGE), "%s: no Language row",
              ico_region_cur()->slug);
        ui_settings_step(UI_OPT_LANGUAGE, 1);
        CHECK(NonLinearCameraMove == 2 && ico_opt_text_language() == -1 &&
                  ui_settings_text_language() == -1,
              "%s: the step does nothing", ico_region_cur()->slug);
        ico_opt_set_text_language(UI_LANG_RU_COBRA838);
        ui_settings_apply_language();
        CHECK(ui_settings_text_language() == -1 &&
                  ui_menu_words_language(UI_LANG_EN) == UI_LANG_EN &&
                  ui_get_language() == (UiLang)ico_region_cur()->ui_lang_default,
              "%s: the key changes nothing", ico_region_cur()->slug);
        ico_opt_set_text_language(-1);
        ui_settings_apply_language();
        return;
    }
    CHECK(ui_settings_opt_shown(UI_OPT_LANGUAGE), "the Language row");
    for (int i = 0; i < 5; i++) {
        CHECK(NonLinearCameraMove == 2 + i && ico_opt_text_language() == -1 &&
                  strcmp(ui_settings_value_text(UI_OPT_LANGUAGE), kNames[i]) == 0,
              "entry %d: %s, the game's language %d (%s)", i, kNames[i], NonLinearCameraMove,
              ui_settings_value_text(UI_OPT_LANGUAGE));
        CHECK(ui_subtitle_words(1, 0) == NULL && ui_menu_words_language(UI_LANG_EN) == UI_LANG_EN,
              "entry %d: the disc's pictures and sheets' words", i);
        ui_settings_step(UI_OPT_LANGUAGE, 1);
    }
    CHECK(NonLinearCameraMove == 2 && ico_opt_text_language() == UI_LANG_RU_BOBTTZ &&
              strcmp(ui_settings_value_text(UI_OPT_LANGUAGE), "Русский (bob-ttz)") == 0,
          "entry 5: Russian (bob-ttz) (%d, %s)", NonLinearCameraMove,
          ui_settings_value_text(UI_OPT_LANGUAGE));
    ui_settings_step(UI_OPT_LANGUAGE, 1);
    CHECK(NonLinearCameraMove == 2 && ico_opt_text_language() == UI_LANG_RU_COBRA838 &&
              strcmp(ui_settings_value_text(UI_OPT_LANGUAGE), "Русский (cobra838)") == 0,
          "entry 6: Russian (cobra838) over the English game (%d, %s)", NonLinearCameraMove,
          ui_settings_value_text(UI_OPT_LANGUAGE));
    CHECK(strcmp(ico_config_get_string("game.text_language", ""), "ru-cobra838") == 0 &&
              strcmp(ico_config_get_string("game.language", ""), "en") == 0,
          "entry 6: [game] text_language and the English [game] language");
    CHECK(ui_get_language() == UI_LANG_RU_COBRA838 && ui_sheet_language() == UI_LANG_EN &&
              ui_menu_words_language(UI_LANG_EN) == UI_LANG_RU_COBRA838,
          "entry 6: Auto in his Russian over the English sheets (%d, %d)", ui_get_language(),
          ui_sheet_language());
    CHECK(ui_subtitle_words(1, 0) == ico_subtitles_ru_cobra838[1].words, "entry 6: his subtitles");
    CHECK(strcmp(ui_str(UI_STR_SETTINGS), ui_strings_ru_cobra838[UI_STR_SETTINGS]) == 0,
          "entry 6: the Options menu in his Russian");
    /* an id his table leaves NULL is English (a backup row, newer than it) */
    CHECK(ui_strings_ru_cobra838[UI_STR_EXTRAS_BACKUP] == NULL &&
              strcmp(ui_str(UI_STR_EXTRAS_BACKUP), ui_strings_en[UI_STR_EXTRAS_BACKUP]) == 0,
          "entry 6: an untranslated row in English");
    /* a Menu language of the player's own wins over it */
    ico_opt_set_ui_language(UI_LANG_DE);
    ui_settings_apply_language();
    CHECK(ui_get_language() == UI_LANG_DE &&
              ui_menu_words_language(UI_LANG_EN) == UI_LANG_RU_COBRA838,
          "German menus, the game's words still his Russian");
    ico_opt_set_ui_language(-1);
    ui_settings_apply_language();
    /* entry 7, Brazilian Portuguese (subtitles_pt_br_test), then English */
    ui_settings_step(UI_OPT_LANGUAGE, 1);
    CHECK(NonLinearCameraMove == 2 && ico_opt_text_language() == UI_LANG_PT_BR,
          "entry 7: Brazilian Portuguese");
    ui_settings_step(UI_OPT_LANGUAGE, 1);
    CHECK(NonLinearCameraMove == 2 && ico_opt_text_language() == -1 &&
              strcmp(ico_config_get_string("game.text_language", "?"), "") == 0 &&
              ui_get_language() == UI_LANG_EN && ui_menu_words_language(UI_LANG_EN) == UI_LANG_EN &&
              ui_subtitle_words(1, 0) == NULL,
          "wraps to English, the key cleared");
    ui_settings_step(UI_OPT_LANGUAGE, -1);
    CHECK(ico_opt_text_language() == UI_LANG_PT_BR, "left: Brazilian Portuguese");
    ui_settings_step(UI_OPT_LANGUAGE, -1);
    CHECK(ico_opt_text_language() == UI_LANG_RU_COBRA838, "left: Russian (cobra838) again");
    ui_settings_step(UI_OPT_LANGUAGE, -1);
    CHECK(ico_opt_text_language() == UI_LANG_RU_BOBTTZ, "left: Russian (bob-ttz)");
    ui_settings_step(UI_OPT_LANGUAGE, 1);
    ui_settings_step(UI_OPT_LANGUAGE, 1);
    ui_settings_step(UI_OPT_LANGUAGE, 1);
    CHECK(NonLinearCameraMove == 2 && ico_opt_text_language() == -1, "English");
    /* eight entries: eight steps come back to English */
    for (int k = 0; k < 8; k++) {
        ui_settings_step(UI_OPT_LANGUAGE, 1);
        CHECK(k == 7 || NonLinearCameraMove != 2 || ico_opt_text_language() != -1,
              "step %d: not English yet", k + 1);
    }
    CHECK(NonLinearCameraMove == 2 && ico_opt_text_language() == -1 &&
              strcmp(ui_settings_value_text(UI_OPT_LANGUAGE), "English") == 0,
          "eight steps: English again (%s)", ui_settings_value_text(UI_OPT_LANGUAGE));
    /* the key from the file with the game in English: Auto resolves to it */
    useConfig("version = 1\n[ui]\nlanguage = \"auto\"\n[game]\ntext_language = \"ru-cobra838\"\n");
    NonLinearCameraMove = 2;
    ui_settings_apply_language();
    CHECK(ui_settings_text_language() == UI_LANG_RU_COBRA838 &&
              ui_settings_resolve_language() == UI_LANG_RU_COBRA838 &&
              ui_get_language() == UI_LANG_RU_COBRA838 &&
              strcmp(ui_settings_value_text(UI_OPT_LANGUAGE), "Русский (cobra838)") == 0,
          "the file's key: Auto in his Russian, the row on his entry");
    /* a key from the file with the game in French: the disc's language wins */
    useConfig("version = 1\n[game]\ntext_language = \"ru-cobra838\"\n");
    NonLinearCameraMove = 3;
    ui_settings_apply_language();
    CHECK(strcmp(ui_settings_value_text(UI_OPT_LANGUAGE), "Français") == 0 &&
              ui_settings_text_language() == -1 && ui_get_language() == UI_LANG_FR,
          "the French game: French (%s)", ui_settings_value_text(UI_OPT_LANGUAGE));
    ui_settings_step(UI_OPT_LANGUAGE, 1);
    CHECK(NonLinearCameraMove == 4 && ico_opt_text_language() == -1 &&
              strcmp(ico_config_get_string("game.text_language", "?"), "") == 0,
          "a step from French: German, the key cleared");
    NonLinearCameraMove = 2;
    useConfig("version = 1\n");
    ui_settings_apply_language();
}

static void testStrips(void)
{
    uint8_t a[512 * 64], b[512 * 64];
    int same = 0, english = 0, differs = 0, items = 0;
    for (int i = 0; i < ui_menu_text_item_count; i++) {
        const UiMenuTextItem *it = &ui_menu_text_items[i];
        if ((int)it->w * (int)it->h > (int)sizeof(a)) {
            continue;
        }
        items++;
        for (int l = 0; l < UI_LANG_PAL_COUNT; l++) {
            ui_set_menu_words_language(-1);
            const int ra = ui__menu_strip_raster(it, l, a, it->w, it->h);
            ui_set_menu_words_language(UI_LANG_RU_COBRA838);
            const int rb = ui__menu_strip_raster(it, l, b, it->w, it->h);
            const int eq = memcmp(a, b, (size_t)it->w * it->h) == 0;
            CHECK(ra == 0 && rb == 0, "item %d sheet %d: rasterised", i, l);
            if (l != UI_LANG_EN) {
                CHECK(eq, "item %d sheet %d: its own words with the translation in force", i, l);
                same += eq;
                continue;
            }
            /* the English sheet: the Russian words where the table has them */
            const char *ru = ui_strings_ru_cobra838[it->str];
            if (ru == NULL || strcmp(ru, ui_strings_en[it->str]) == 0) {
                CHECK(eq, "item %d: English kept where the translation has none", i);
                english += eq;
            } else {
                differs += !eq;
            }
        }
    }
    ui_set_menu_words_language(-1);
    printf(
        "subtitles_ru_cobra838: %d items, %d other-sheet strips the same, %d English kept, %d in "
        "Russian\n",
        items, same, english, differs);
    CHECK(items > 100 && differs > 50, "the strips compared (%d items, %d Russian)", items,
          differs);
}

int main(int argc, char **argv)
{
    snprintf(s_dir, sizeof(s_dir), "%s", argc > 1 ? argv[1] : ".");
    setEnv("LC_ALL", NULL);
    setEnv("LANG", "en_GB.UTF-8");
    const char *slug = argc > 2 ? argv[2] : "pal";
    CHECK(ico_region_select(slug) == 0, "the %s disc", slug);
    const int pal = strcmp(slug, "pal") == 0;
    CHECK(ico_region_cur()->has_game_language == pal, "%s: a game language %d", slug,
          ico_region_cur()->has_game_language);
    CHECK(ui_font_init(), "the fonts");
    testTable();
    testWords(pal);
    testLayout();
    testLanguageRow(pal);
    if (pal) {
        testStrips();
    }
    ico_opt_set_ui_language(-1);
    ico_opt_set_text_language(-1);
    NonLinearCameraMove = 2;
    CHECK(ico_region_select("pal") == 0, "the European disc again");
    printf("subtitles_ru_cobra838_test %s: %s\n", slug, failures ? "failed" : "ok");
    return failures ? 1 : 0;
}

/*
 * port/ui/test/strings_complete_test.c
 *
 * ctest strings_complete: every language's table (strings.h) defines every
 * UiStrId (ui_str_in falls back to English at run time, so a missing entry
 * would otherwise show English without a word), every entry but
 * UI_STR_NONE is non-empty, and each entry's printf conversions are the
 * English one's in the same order (the serial notes: exactly one %s); the
 * serial notes filled with each disc's serial (ui_str_serial); the sheet
 * language's default and setter; the port text's language from [ui]
 * language and the disc (ui_lang.c, the region rows' ui_lang_default and
 * menu_sheet); the Japanese and Korean line-break rules (ui_wide_char,
 * ui_break_before, ui_hangul_char).  A community translation's table
 * (UI_LANG_COMMUNITY_FIRST on) may leave an id NULL (English at run time:
 * its CSV's empty rows and the ids newer than it), but translates at least
 * half and names itself in every table; the community translation in force
 * (ui_lang_text_resolve: the European disc in English only) and the Menu
 * language's Auto following it; the game's menu words over the English
 * sheets (ui_menu_words_language). CPU only, no game data.
 */
#include <stdio.h>
#include <string.h>

#include "../core/region.h"
#include "strings.h"
#include "ui_lang.h"

static int failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "strings_complete: FAIL %s:%d: ", __FILE__, __LINE__);                 \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fprintf(stderr, "\n");                                                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* ui_lang.c's inputs, stubbed: [ui] language, [game] text_language and the
   game's language word */
static int s_choice = -1;
static int s_text = -1;
int NonLinearCameraMove = 2;

int ico_opt_ui_language(void)
{
    return s_choice;
}

int ico_opt_text_language(void)
{
    return s_text;
}

static const char *const kNames[UI_LANG_TABLES] = {"en", "fr", "de", "it",        "es",
                                                   "ja", "ko", "zh", "ru-bobttz", "ru-cobra838"};
static const char *const *const kTables[UI_LANG_TABLES] = {
    ui_strings_en, ui_strings_fr, ui_strings_de, ui_strings_it,        ui_strings_es,
    ui_strings_ja, ui_strings_ko, ui_strings_zh, ui_strings_ru_bobttz, ui_strings_ru_cobra838};

/* s's printf conversions, their letters in order ("%%" is none) */
static void conversions(const char *s, char *out, size_t n)
{
    size_t k = 0;
    for (const char *p = s; *p; p++) {
        if (*p != '%') {
            continue;
        }
        p++;
        if (*p == '%') {
            continue;
        }
        while (*p && strchr("-+ #0123456789.hlz", *p)) {
            p++;
        }
        if (!*p) {
            break;
        }
        if (k + 1 < n) {
            out[k++] = *p;
        }
    }
    out[k] = '\0';
}

static void testTables(void)
{
    for (int l = 0; l < UI_LANG_TABLES; l++) {
        const int community = l >= UI_LANG_COMMUNITY_FIRST;
        int missing = 0, english = 0;
        for (int id = 0; id < UI_STR_COUNT; id++) {
            const char *s = kTables[l][id];
            if (s == NULL && community && id != UI_STR_NONE) {
                english++; /* not translated: English at run time */
                CHECK(strcmp(ui_str_in((UiLang)l, (UiStrId)id), ui_strings_en[id]) == 0,
                      "%s: id %d falls back to English", kNames[l], id);
                continue;
            }
            if (s == NULL) {
                if (missing++ < 10) {
                    CHECK(0, "%s: id %d has no entry", kNames[l], id);
                }
                continue;
            }
            CHECK(id == UI_STR_NONE ? s[0] == '\0' : s[0] != '\0', "%s: id %d is %s", kNames[l], id,
                  s[0] ? "not empty" : "empty");
            const char *en = ui_strings_en[id] ? ui_strings_en[id] : "";
            char a[16], b[16];
            conversions(s, a, sizeof(a));
            conversions(en, b, sizeof(b));
            CHECK(strcmp(a, b) == 0, "%s: id %d's conversions \"%s\" are not English's \"%s\": %s",
                  kNames[l], id, a, b, s);
            if (ui_str_takes_serial((UiStrId)id)) {
                CHECK(strcmp(a, "s") == 0 && strstr(s, "%s") != NULL,
                      "%s: id %d takes the serial: one %%s (%s)", kNames[l], id, s);
            }
        }
        CHECK(missing == 0, "%s: %d ids without an entry", kNames[l], missing);
        if (community) {
            CHECK(english * 2 < UI_STR_COUNT, "%s: %d of %d ids in English", kNames[l], english,
                  (int)UI_STR_COUNT);
            printf("strings_complete: %s, a community translation: %d of %d ids in English\n",
                   kNames[l], english, (int)UI_STR_COUNT);
        }
    }
    /* every table names the community translations in their own words */
    for (int l = 0; l < UI_LANG_TABLES; l++) {
        CHECK(kTables[l][UI_STR_LANG_RU_BOBTTZ] != NULL &&
                  strcmp(kTables[l][UI_STR_LANG_RU_BOBTTZ], "Русский (bob-ttz)") == 0,
              "%s names Russian (bob-ttz) in Russian", kNames[l]);
        CHECK(kTables[l][UI_STR_LANG_RU_COBRA838] != NULL &&
                  strcmp(kTables[l][UI_STR_LANG_RU_COBRA838], "Русский (cobra838)") == 0,
              "%s names Russian (cobra838) in Russian", kNames[l]);
    }
    /* cobra838's table (PR 180): his rows, not bob-ttz's; the Yorda pit
       time's 1.5x step (his "x1" was the 1x one) */
    CHECK(strcmp(ui_strings_ru_cobra838[UI_STR_VAL_PIT_LONGER], "x1,5") == 0 &&
              strcmp(ui_strings_ru_cobra838[UI_STR_VAL_PIT_DOUBLE], "x2") == 0,
          "cobra838: Longer is x1,5 (%s)", ui_strings_ru_cobra838[UI_STR_VAL_PIT_LONGER]);
    /* English: the rows his table left in English, a note over its space,
       an id newer than his table, one whose English changed since */
    CHECK(ui_strings_ru_cobra838[UI_STR_OPT_GRAPHICS_ADAPTER] == NULL &&
              ui_strings_ru_cobra838[UI_STR_OPT_VAR_TOUGHNESS_NOTE_DEMO] == NULL &&
              ui_strings_ru_cobra838[UI_STR_GPU_DRIVER_NOTE_LOST] == NULL &&
              ui_strings_ru_cobra838[UI_STR_EXTRAS_BACKUP] == NULL &&
              ui_strings_ru_cobra838[UI_STR_STATES_NOTE] == NULL,
          "cobra838: the rows kept in English");
}

static void testSerial(void)
{
    static const UiStrId ids[4] = {UI_STR_TEXTURE_PACK_NOTE, UI_STR_DUMP_TEXTURES_NOTE,
                                   UI_STR_MODEL_PACK_NOTE, UI_STR_DUMP_MODELS_NOTE};
    char buf[256];
    CHECK(!ui_str_takes_serial(UI_STR_SETTINGS) && !ui_str_takes_serial(UI_STR_MV_SAVED_FMT),
          "only the four pack notes take the serial");
    for (int r = 0; r < ico_region_count(); r++) {
        const char *slug = ico_region_at(r)->slug;
        const char *serial = ico_region_at(r)->pack_serial;
        CHECK(ico_region_select(slug) == 0, "select %s", slug);
        for (int l = 0; l < UI_LANG_TABLES; l++) {
            ui_set_language((UiLang)l);
            for (int i = 0; i < 4; i++) {
                CHECK(ui_str_takes_serial(ids[i]), "id %d takes the serial", ids[i]);
                const char *s = ui_str_serial(ids[i], buf, sizeof(buf));
                CHECK(s == buf && strstr(s, serial) != NULL && strchr(s, '%') == NULL,
                      "%s %s: id %d names %s: %s", slug, kNames[l], ids[i], serial, s);
            }
        }
    }
    ico_region_select("pal");
    ui_set_language(UI_LANG_EN);
    CHECK(strcmp(ui_str_serial(UI_STR_TEXTURE_PACK_NOTE, buf, sizeof(buf)),
                 "PCSX2 packs: copy SCES-50760 into textures.") == 0,
          "the European disc's texture pack note: %s", buf);
    CHECK(strcmp(ui_str_serial(UI_STR_SETTINGS, buf, sizeof(buf)), ui_str(UI_STR_SETTINGS)) == 0,
          "another id's text unchanged: %s", buf);
    /* cut at a character's end: the Japanese note ("PCSX2の...") into 8
       bytes keeps "PCSX2", not the first two bytes of the next character */
    ui_set_language(UI_LANG_JA);
    ui_str_serial(UI_STR_TEXTURE_PACK_NOTE, buf, 8);
    CHECK(strcmp(buf, "PCSX2") == 0, "the cut note: \"%s\"", buf);
    ui_set_language(UI_LANG_EN);
}

static void testSheetLanguage(void)
{
    /* the default follows the port's language while it is a European one */
    ui_set_language(UI_LANG_DE);
    CHECK(ui_sheet_language() == UI_LANG_DE, "the sheet language follows German: %d",
          ui_sheet_language());
    ui_set_language(UI_LANG_JA);
    CHECK(ui_sheet_language() == UI_LANG_EN, "Japanese port text over the default sheets: %d",
          ui_sheet_language());
    ui_set_sheet_language(UI_SHEET_JA);
    CHECK(ui_sheet_language() == UI_SHEET_JA, "set: the Japanese disc's sheets");
    ui_set_sheet_language(UI_LANG_SHEET_COUNT);
    CHECK(ui_sheet_language() == UI_SHEET_JA, "an out of range value is ignored");
    ui_set_language(UI_LANG_FR);
    CHECK(ui_sheet_language() == UI_SHEET_JA, "once set it no longer follows");
    ui_set_sheet_language(UI_LANG_IT);
    CHECK(ui_sheet_language() == UI_LANG_IT, "set: Italian");
    ui_set_language(UI_LANG_EN);
}

static void testResolve(void)
{
    /* the rule over its inputs */
    CHECK(ui_lang_resolve(-1, UI_LANG_JA, 0, 3) == UI_LANG_JA, "auto, the Japanese disc: Japanese");
    CHECK(ui_lang_resolve(-1, UI_LANG_KO, 0, 3) == UI_LANG_KO, "auto, a Korean disc: Korean");
    CHECK(ui_lang_resolve(-1, UI_LANG_ZH, 0, 3) == UI_LANG_ZH, "auto, the Asian disc: Chinese");
    CHECK(ui_lang_resolve(-1, -1, 1, 3) == UI_LANG_FR, "auto, the European disc: the game's");
    CHECK(ui_lang_resolve(-1, -1, 1, 6) == UI_LANG_ES, "auto, the European disc: Spanish");
    CHECK(ui_lang_resolve(-1, -1, 0, 3) == UI_LANG_EN, "auto, no game language: English");
    CHECK(ui_lang_resolve(-1, UI_LANG_TABLES, 0, 3) == UI_LANG_EN,
          "auto, a disc language out of range: English");
    for (int c = 0; c < UI_LANG_TABLES; c++) {
        CHECK(ui_lang_resolve(c, UI_LANG_JA, 0, 3) == (UiLang)c &&
                  ui_lang_resolve(c, UI_LANG_KO, 0, 3) == (UiLang)c &&
                  ui_lang_resolve(c, UI_LANG_ZH, 0, 3) == (UiLang)c &&
                  ui_lang_resolve(c, -1, 1, 4) == (UiLang)c,
              "a chosen language %s wins on every disc", kNames[c]);
    }
    CHECK(ui_lang_resolve(UI_LANG_TABLES, -1, 1, 5) == UI_LANG_IT, "out of range is auto");
    /* the region rows' port-text fields (port/core/region.h: plain numbers
       there) are UiLang and sheet-language ids; a disc whose game picks
       its language names neither, one that does not names both (the
       Japanese and Korean discs their own sheets, the English-only 2001
       discs the European English sheet) */
    for (int i = 0; i < ico_region_count(); i++) {
        const IcoRegionInfo *r = ico_region_at(i);
        CHECK(r->ui_lang_default >= -1 && r->ui_lang_default < UI_LANG_COUNT &&
                  r->menu_sheet >= -1 && r->menu_sheet < UI_LANG_SHEET_COUNT &&
                  r->menu_sheet != UI_SHEET_JA_TITLE,
              "%s: ui_lang_default %d, menu_sheet %d", r->slug, r->ui_lang_default, r->menu_sheet);
        CHECK(r->has_game_language ? r->ui_lang_default < 0 && r->menu_sheet < 0
                                   : r->ui_lang_default >= 0 && r->menu_sheet >= 0,
              "%s: the game's language or the disc's own", r->slug);
    }
    CHECK(ico_region_select("jpn") == 0 && ico_region_cur()->ui_lang_default == UI_LANG_JA &&
              ico_region_cur()->menu_sheet == UI_SHEET_JA,
          "the Japanese disc: Japanese text over the Japanese sheets");
    CHECK(ico_region_select("kor") == 0 && ico_region_cur()->ui_lang_default == UI_LANG_KO &&
              ico_region_cur()->menu_sheet == UI_SHEET_KO,
          "the Korean disc: Korean text over the Korean sheets");
    CHECK(ico_region_select("cn") == 0 && ico_region_cur()->ui_lang_default == UI_LANG_ZH &&
              ico_region_cur()->menu_sheet == UI_SHEET_ZH,
          "the Asian disc: Chinese text over the Chinese sheets");
    ico_region_select("pal");
    /* through the running disc */
    ico_region_select("pal");
    s_choice = -1;
    NonLinearCameraMove = 4;
    CHECK(ui_settings_resolve_language() == UI_LANG_DE, "the European disc, German game");
    ico_region_select("jpn");
    CHECK(ui_settings_resolve_language() == UI_LANG_JA, "the Japanese disc: Japanese");
    s_choice = UI_LANG_EN;
    CHECK(ui_settings_resolve_language() == UI_LANG_EN, "the Japanese disc, English chosen");
    ico_region_select("kor");
    s_choice = -1;
    CHECK(ui_settings_resolve_language() == UI_LANG_KO, "the Korean disc: Korean");
    s_choice = UI_LANG_EN;
    CHECK(ui_settings_resolve_language() == UI_LANG_EN, "the Korean disc, English chosen");
    ico_region_select("cn");
    s_choice = -1;
    CHECK(ui_settings_resolve_language() == UI_LANG_ZH, "the Asian disc: Chinese");
    s_choice = UI_LANG_EN;
    CHECK(ui_settings_resolve_language() == UI_LANG_EN, "the Asian disc, English chosen");
    ico_region_select("pal");
    s_choice = UI_LANG_JA;
    CHECK(ui_settings_resolve_language() == UI_LANG_JA, "the European disc, Japanese chosen");
    s_choice = -1;
    NonLinearCameraMove = 2;
}

static void testCommunity(void)
{
    /* the rule over its inputs: a community translation, on a disc with a
       game language, while the game is in English */
    CHECK(ui_lang_text_resolve(UI_LANG_RU_BOBTTZ, 1, 2) == UI_LANG_RU_BOBTTZ,
          "Russian (bob-ttz) over the English European disc");
    CHECK(ui_lang_text_resolve(UI_LANG_RU_BOBTTZ, 1, 3) == -1, "not over the French game");
    CHECK(ui_lang_text_resolve(UI_LANG_RU_BOBTTZ, 0, 2) == -1, "not on a disc without one");
    CHECK(ui_lang_text_resolve(UI_LANG_RU_COBRA838, 1, 2) == UI_LANG_RU_COBRA838 &&
              ui_lang_text_resolve(UI_LANG_RU_COBRA838, 1, 4) == -1 &&
              ui_lang_text_resolve(UI_LANG_RU_COBRA838, 0, 2) == -1,
          "Russian (cobra838): over the English European disc only");
    CHECK(ui_lang_text_resolve(-1, 1, 2) == -1, "none chosen");
    for (int l = 0; l < UI_LANG_COMMUNITY_FIRST; l++) {
        CHECK(ui_lang_text_resolve(l, 1, 2) == -1, "%s is not a community translation", kNames[l]);
    }
    CHECK(ui_lang_text_resolve(UI_LANG_TABLES, 1, 2) == -1, "out of range");
    /* through the running disc: Auto follows it on the European disc only */
    ico_region_select("pal");
    s_choice = -1;
    s_text = UI_LANG_RU_BOBTTZ;
    NonLinearCameraMove = 2;
    CHECK(ui_settings_text_language() == UI_LANG_RU_BOBTTZ, "in force on the European disc");
    CHECK(ui_settings_resolve_language() == UI_LANG_RU_BOBTTZ, "Auto: the translation");
    s_choice = UI_LANG_DE;
    CHECK(ui_settings_resolve_language() == UI_LANG_DE, "a chosen Menu language wins");
    s_choice = -1;
    s_text = UI_LANG_RU_COBRA838;
    CHECK(ui_settings_text_language() == UI_LANG_RU_COBRA838 &&
              ui_settings_resolve_language() == UI_LANG_RU_COBRA838,
          "Auto: Russian (cobra838) when it is the one chosen");
    s_text = UI_LANG_RU_BOBTTZ;
    s_choice = -1;
    NonLinearCameraMove = 5;
    CHECK(ui_settings_text_language() == -1 && ui_settings_resolve_language() == UI_LANG_IT,
          "the game in Italian: the disc's language, no translation");
    NonLinearCameraMove = 2;
    static const char *const kOther[] = {"jpn", "kor", "cn", "ntsc", "aug6", "jun26"};
    for (unsigned i = 0; i < sizeof(kOther) / sizeof(kOther[0]); i++) {
        CHECK(ico_region_select(kOther[i]) == 0 && ui_settings_text_language() == -1 &&
                  ui_settings_resolve_language() != UI_LANG_RU_BOBTTZ,
              "%s: no translation in force", kOther[i]);
    }
    ico_region_select("pal");
    s_text = -1;
    CHECK(ui_settings_text_language() == -1 && ui_settings_resolve_language() == UI_LANG_EN,
          "none: the game's English");
    /* the menu words: a community table over the English sheets, the five
       sheets' own words otherwise */
    for (int l = 0; l < UI_LANG_SHEET_COUNT; l++) {
        CHECK(ui_menu_words_language(l) == (UiLang)l, "sheet %d: its own words", l);
    }
    ui_set_menu_words_language(UI_LANG_RU_BOBTTZ);
    CHECK(ui_menu_words_language(UI_LANG_EN) == UI_LANG_RU_BOBTTZ,
          "the English sheets: the translation's words");
    for (int l = 1; l < UI_LANG_SHEET_COUNT; l++) {
        CHECK(ui_menu_words_language(l) == (UiLang)l, "sheet %d keeps its words", l);
    }
    CHECK(strcmp(ui_str_in(ui_menu_words_language(UI_LANG_EN), UI_STR_MT_NEW_GAME),
                 ui_strings_ru_bobttz[UI_STR_MT_NEW_GAME]) == 0,
          "the title's New Game in Russian");
    ui_set_menu_words_language(UI_LANG_RU_COBRA838);
    CHECK(ui_menu_words_language(UI_LANG_EN) == UI_LANG_RU_COBRA838 &&
              ui_menu_words_language(UI_LANG_FR) == UI_LANG_FR &&
              strcmp(ui_str_in(ui_menu_words_language(UI_LANG_EN), UI_STR_MT_NEW_GAME),
                     ui_strings_ru_cobra838[UI_STR_MT_NEW_GAME]) == 0,
          "Russian (cobra838): his words over the English sheets only");
    ui_set_menu_words_language(UI_LANG_DE);
    CHECK(ui_menu_words_language(UI_LANG_EN) == UI_LANG_EN, "a disc language is not one");
    ui_set_menu_words_language(-1);
    CHECK(ui_menu_words_language(UI_LANG_EN) == UI_LANG_EN, "none: English");
}

static void testBreaks(void)
{
    CHECK(ui_wide_char(0x3042) && ui_wide_char(0x30A2) && ui_wide_char(0x4E2D) &&
              ui_wide_char(0xFF1A) && ui_wide_char(0x3001),
          "kana, kanji, fullwidth forms and CJK punctuation are wide");
    CHECK(!ui_wide_char('a') && !ui_wide_char(0x00E9) && !ui_wide_char(0x2026) &&
              !ui_wide_char(0x25CB),
          "Latin letters, the ellipsis and the button marks are not");
    CHECK(!ui_break_before('a', 'b') && !ui_break_before('a', ' ') && !ui_break_before(' ', 'b'),
          "Latin text breaks at its spaces only");
    CHECK(ui_break_before(0x3042, 0x3044) && ui_break_before(0x4E2D, 0x3042) &&
              ui_break_before('A', 0x3042) && ui_break_before(0x3042, 'A'),
          "between kana and kanji, and between Latin and kana");
    CHECK(!ui_break_before(0x3042, 0x3002) && !ui_break_before(0x3042, 0x3001) &&
              !ui_break_before(0x30C6, 0x30FC) && !ui_break_before(0x3057, 0x3083) &&
              !ui_break_before(0x30C9, 0x30C3) && !ui_break_before(0x3042, 0x300D) &&
              !ui_break_before(0x3042, 0xFF09) && !ui_break_before(0x3042, 0xFF1F) &&
              !ui_break_before(0x3042, 0x2026),
          "never before closing punctuation, the prolonged sound mark or small kana");
    CHECK(!ui_break_before(0x300C, 0x3042) && !ui_break_before(0xFF08, 0x3042) &&
              ui_break_before(0x300D, 0x3042),
          "never after an opening bracket; after a closing one");
    CHECK(!ui_break_before(0x3042, '.') && !ui_break_before(0x3042, ')') &&
              !ui_break_before('(', 0x3042) && ui_break_before(')', 0x3042),
          "the ASCII brackets and punctuation keep the same rules");
    CHECK(!ui_break_before(0, 0x3042), "no break at the text's start");
    /* Chinese: the ideographs and the fullwidth marks the table uses break as
       the Japanese text does (no spaces), never before 。，？！：、」） */
    CHECK(ui_wide_char(0x9078) && ui_wide_char(0x7E41) && ui_wide_char(0xFF0C) &&
              ui_wide_char(0xFF1F) && ui_break_before(0x9078, 0x9805) &&
              !ui_break_before(0x9805, 0x3002) && !ui_break_before(0x9805, 0xFF0C) &&
              !ui_break_before(0x9805, 0xFF1F) && !ui_break_before(0x9805, 0xFF01) &&
              !ui_break_before(0x9805, 0xFF1A) && !ui_break_before(0x9805, 0x3001) &&
              !ui_break_before(0x9805, 0x300D) && !ui_break_before(0xFF08, 0x9805),
          "Chinese breaks between ideographs, not before its closing marks");
    /* Korean: Hangul is not wide (it breaks at its spaces), the compatibility
       jamo inside the CJK block neither */
    CHECK(ui_hangul_char(0xD55C) && ui_hangul_char(0xAC00) && ui_hangul_char(0xD7A3) &&
              ui_hangul_char(0x3131) && ui_hangul_char(0x1100) && !ui_hangul_char(0x3042) &&
              !ui_hangul_char('a') && !ui_hangul_char(0x4E2D),
          "the syllables and jamo are Hangul, kana, kanji and Latin not");
    CHECK(!ui_wide_char(0xD55C) && !ui_wide_char(0x3131) && ui_wide_char(0x3000),
          "Hangul is not a wide character; the CJK space still is");
    CHECK(!ui_break_before(0xD55C, 0xAD6D) && !ui_break_before(0xD55C, ' ') &&
              !ui_break_before(' ', 0xD55C) && !ui_break_before(0xD55C, '.'),
          "Korean breaks at its spaces only");
}

int main(void)
{
    testTables();
    testSerial();
    testSheetLanguage();
    testResolve();
    testCommunity();
    testBreaks();
    if (failures) {
        fprintf(stderr, "strings_complete: %d failure(s)\n", failures);
        return 1;
    }
    printf("strings_complete: %d languages (%d community translations), %d ids each, every "
           "entry defined\n",
           UI_LANG_TABLES, UI_LANG_COMMUNITY_COUNT, (int)UI_STR_COUNT);
    return 0;
}

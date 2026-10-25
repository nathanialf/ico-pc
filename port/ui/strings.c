/*
 * port/ui/strings.c
 *
 * The language switch over the per-language tables, the serial notes and
 * the line-break rules for the Japanese and Korean text (strings.h).
 */
#include "strings.h"

#include <stdio.h>
#include <string.h>

#include "../core/region.h" /* the selected disc's pack serial */

static UiLang s_lang = UI_LANG_EN;
static int s_sheetLang = -1; /* -1: follows s_lang (ui_sheet_language) */

static const char *const *const s_tables[UI_LANG_COUNT] = {
    ui_strings_en, ui_strings_fr, ui_strings_de, ui_strings_it,
    ui_strings_es, ui_strings_ja, ui_strings_ko, ui_strings_zh,
};

void ui_set_language(UiLang lang)
{
    if ((int)lang >= 0 && lang < UI_LANG_COUNT) {
        s_lang = lang;
    }
}

UiLang ui_get_language(void)
{
    return s_lang;
}

void ui_set_sheet_language(int sheetLang)
{
    if (sheetLang >= 0 && sheetLang < UI_LANG_SHEET_COUNT) {
        s_sheetLang = sheetLang;
    }
}

int ui_sheet_language(void)
{
    if (s_sheetLang >= 0) {
        return s_sheetLang;
    }
    return (int)s_lang < UI_LANG_PAL_COUNT ? (int)s_lang : UI_LANG_EN;
}

UiLang ui_lang_from_game(int nonLinearCameraMove)
{
    switch (nonLinearCameraMove) {
    case 3:
        return UI_LANG_FR;
    case 4:
        return UI_LANG_DE;
    case 5:
        return UI_LANG_IT;
    case 6:
        return UI_LANG_ES;
    default:
        return UI_LANG_EN;
    }
}

const char *ui_str_in(UiLang lang, UiStrId id)
{
    if ((int)id < 0 || id >= UI_STR_COUNT) {
        return "";
    }
    if ((int)lang < 0 || lang >= UI_LANG_COUNT) {
        lang = UI_LANG_EN;
    }
    const char *s = s_tables[lang][id];
    if (s == NULL) {
        s = ui_strings_en[id];
    }
    return s != NULL ? s : "";
}

const char *ui_str(UiStrId id)
{
    return ui_str_in(s_lang, id);
}

void ui_strings_for_each(UiStringFn fn, void *user)
{
    if (!fn) {
        return;
    }
    for (int l = 0; l < UI_LANG_COUNT; l++) {
        for (int id = 0; id < UI_STR_COUNT; id++) {
            const char *s = s_tables[l][id];
            if (s && *s) {
                fn((UiLang)l, s, user);
            }
        }
    }
}

int ui_str_takes_serial(UiStrId id)
{
    return id == UI_STR_TEXTURE_PACK_NOTE || id == UI_STR_DUMP_TEXTURES_NOTE ||
           id == UI_STR_MODEL_PACK_NOTE || id == UI_STR_DUMP_MODELS_NOTE;
}

/* src into buf (n bytes), cut at a character's end */
static void copyCut(char *buf, size_t n, const char *src)
{
    size_t len = strlen(src);
    if (len >= n) {
        len = n - 1;
        while (len > 0 && ((unsigned char)src[len] & 0xC0) == 0x80) {
            len--;
        }
    }
    memcpy(buf, src, len);
    buf[len] = '\0';
}

const char *ui_str_serial(UiStrId id, char *buf, size_t n)
{
    if (!buf || n == 0) {
        return "";
    }
    if (!ui_str_takes_serial(id)) {
        copyCut(buf, n, ui_str(id));
        return buf;
    }
    /* the format's one %s (strings_complete), the serial put in through a
       buffer big enough for any note, then cut to n like the others */
    char full[512];
    const char *serial = ico_region_cur()->pack_serial;
    snprintf(full, sizeof(full), ui_str(id), serial ? serial : "");
    copyCut(buf, n, full);
    return buf;
}

int ui_hangul_char(uint32_t cp)
{
    return (cp >= 0x1100 && cp <= 0x11FF) || (cp >= 0x3130 && cp <= 0x318F) ||
           (cp >= 0xA960 && cp <= 0xA97F) || (cp >= 0xAC00 && cp <= 0xD7FF);
}

int ui_wide_char(uint32_t cp)
{
    /* the CJK blocks (U+2E80 .. U+9FFF: radicals, punctuation, kana, the
       ideographs), the compatibility ideographs and the fullwidth forms;
       not the Hangul compatibility jamo inside them (ui_hangul_char: Korean
       breaks at its spaces) */
    return ((cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
            (cp >= 0xFF01 && cp <= 0xFF60) || (cp >= 0xFFE0 && cp <= 0xFFE6)) &&
           !ui_hangul_char(cp);
}

int ui_break_before(uint32_t prev, uint32_t cp)
{
    /* the usual line-start and line-end rules: never before closing
       punctuation (CJK, fullwidth and ASCII), the prolonged sound mark, the
       middle dot, the ellipsis, the sound marks, an iteration mark or a
       small kana; never after an opening bracket */
    static const uint32_t noStart[] = {
        /* closing punctuation */
        0x3001, 0x3002, 0x3009, 0x300B, 0x300D, 0x300F, 0x3011, 0x3015, 0xFF01, 0xFF09, 0xFF0C,
        0xFF0E, 0xFF1A, 0xFF1B, 0xFF1F, 0xFF3D, 0xFF5D, ')', ']', '}', ',', '.', ':', ';', '!', '?',
        '%',
        /* the prolonged sound mark, the middle dot, the ellipsis, the sound
           marks, the iteration marks */
        0x30FC, 0x30FB, 0x2026, 0x309B, 0x309C, 0x3005, 0x309D, 0x309E, 0x30FD, 0x30FE,
        /* small kana */
        0x3041, 0x3043, 0x3045, 0x3047, 0x3049, 0x3063, 0x3083, 0x3085, 0x3087, 0x308E, 0x3095,
        0x3096, 0x30A1, 0x30A3, 0x30A5, 0x30A7, 0x30A9, 0x30C3, 0x30E3, 0x30E5, 0x30E7, 0x30EE,
        0x30F5, 0x30F6};
    static const uint32_t noEnd[] = {0x3008, 0x300A, 0x300C, 0x300E, 0x3010, 0x3014,
                                     0xFF08, 0xFF3B, 0xFF5B, '(',    '[',    '{'};
    if (!ui_wide_char(cp) && !ui_wide_char(prev)) {
        return 0; /* Latin text breaks at its spaces */
    }
    if (prev == 0 || prev == ' ' || cp == ' ') {
        return 0;
    }
    for (size_t i = 0; i < sizeof(noStart) / sizeof(noStart[0]); i++) {
        if (cp == noStart[i]) {
            return 0;
        }
    }
    for (size_t i = 0; i < sizeof(noEnd) / sizeof(noEnd[0]); i++) {
        if (prev == noEnd[i]) {
            return 0;
        }
    }
    return 1;
}

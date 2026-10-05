/*
 * port/ui/strings.c
 *
 * The language switch over the per-language tables (strings.h).
 */
#include "strings.h"

#include <stddef.h>

static UiLang s_lang = UI_LANG_EN;

static const char *const *const s_tables[UI_LANG_COUNT] = {
    ui_strings_en, ui_strings_fr, ui_strings_de, ui_strings_it, ui_strings_es,
};

void ui_SetLanguage(UiLang lang)
{
    if ((int)lang >= 0 && lang < UI_LANG_COUNT) {
        s_lang = lang;
    }
}

UiLang ui_GetLanguage(void)
{
    return s_lang;
}

UiLang ui_LangFromGame(int nonLinearCameraMove)
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

const char *ui_StrIn(UiLang lang, UiStrId id)
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

const char *ui_Str(UiStrId id)
{
    return ui_StrIn(s_lang, id);
}

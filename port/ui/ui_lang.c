/*
 * port/ui/ui_lang.c
 *
 * The port's text language from [ui] language and the running disc
 * (ui_lang.h).
 */
#include "ui_lang.h"

#include "../core/region.h" /* ICO_JPN, ico_region_cur */
#include "options.h"        /* ico_opt_ui_language */

extern int NonLinearCameraMove; /* the game's language, 2..6 (European disc) */

UiLang ui_lang_resolve(int choice, int jpn, int hasGameLanguage, int gameLanguage)
{
    if (choice >= 0 && choice < UI_LANG_COUNT) {
        return (UiLang)choice;
    }
    if (jpn) {
        return UI_LANG_JA; /* the Menu language follows the disc */
    }
    if (hasGameLanguage) {
        return ui_lang_from_game(gameLanguage);
    }
    return UI_LANG_EN;
}

UiLang ui_settings_resolve_language(void)
{
    return ui_lang_resolve(ico_opt_ui_language(), ICO_JPN, ico_region_cur()->has_game_language,
                           NonLinearCameraMove);
}

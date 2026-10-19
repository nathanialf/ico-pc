/*
 * port/ui/ui_lang.c
 *
 * The port's text language from [ui] language and the running disc
 * (ui_lang.h).
 */
#include "ui_lang.h"

#include "../core/region.h" /* ico_region_cur: ui_lang_default, has_game_language */
#include "options.h"        /* ico_opt_ui_language */

extern int NonLinearCameraMove; /* the game's language, 2..6 (European disc) */

UiLang ui_lang_resolve(int choice, int discLang, int hasGameLanguage, int gameLanguage)
{
    if (choice >= 0 && choice < UI_LANG_COUNT) {
        return (UiLang)choice;
    }
    if (discLang >= 0 && discLang < UI_LANG_COUNT) {
        return (UiLang)discLang; /* the Menu language follows the disc */
    }
    if (hasGameLanguage) {
        return ui_lang_from_game(gameLanguage);
    }
    return UI_LANG_EN;
}

UiLang ui_settings_resolve_language(void)
{
    const IcoRegionInfo *r = ico_region_cur();
    return ui_lang_resolve(ico_opt_ui_language(), r->ui_lang_default, r->has_game_language,
                           NonLinearCameraMove);
}

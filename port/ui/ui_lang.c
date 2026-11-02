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

_Static_assert(ICO_OPT_UI_LANGUAGE_COUNT == UI_LANG_TABLES &&
                   ICO_OPT_TEXT_LANGUAGE_FIRST == UI_LANG_COMMUNITY_FIRST,
               "options.c's language names are the UiLang tables'");

int ui_lang_text_resolve(int textLang, int hasGameLanguage, int gameLanguage)
{
    if (textLang < UI_LANG_COMMUNITY_FIRST || textLang >= UI_LANG_TABLES) {
        return -1;
    }
    /* over the English disc data only (NonLinearCameraMove 2) */
    return hasGameLanguage && gameLanguage == 2 ? textLang : -1;
}

int ui_settings_text_language(void)
{
    return ui_lang_text_resolve(ico_opt_text_language(), ico_region_cur()->has_game_language,
                                NonLinearCameraMove);
}

UiLang ui_lang_resolve(int choice, int discLang, int hasGameLanguage, int gameLanguage)
{
    if (choice >= 0 && choice < UI_LANG_TABLES) {
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
    const int choice = ico_opt_ui_language();
    const int text = ui_settings_text_language();
    /* Auto on the European disc: the community translation in force, as the
       game's language would be (its region row names no language) */
    if (choice < 0 && r->ui_lang_default < 0 && text >= 0) {
        return (UiLang)text;
    }
    return ui_lang_resolve(choice, r->ui_lang_default, r->has_game_language, NonLinearCameraMove);
}

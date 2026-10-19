/*
 * port/ui/ui_lang.h
 *
 * The language of the port's own text (strings.h UiLang) from the player's
 * choice and the running disc: [ui] language (port/game/options.h
 * ico_opt_ui_language), the Settings menu's Menu language row.
 */
#ifndef PORT_UI_UI_LANG_H
#define PORT_UI_UI_LANG_H

#include "strings.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The language the port's text is in now: the [ui] language the player
   chose ("en" .. "ko"), else ("auto", the default) the disc's: its region
   row's ui_lang_default (port/core/region.h: Japanese on the Japanese
   disc), else the game's language (NonLinearCameraMove) on a disc
   whose game picks one (IcoRegionInfo.has_game_language: the European
   disc), else English.  The host's system language is not consulted: the
   disc decides.  Reads the game's language word each call, so a change of
   the game's Language row is followed. */
UiLang ui_settings_resolve_language(void);

/* The same rule over its inputs (the tests' and the function's): choice
   ico_opt_ui_language's value (-1 auto, else a UiLang), discLang the
   region's ui_lang_default (a UiLang, or -1: the disc names none),
   hasGameLanguage the region's has_game_language, gameLanguage the game's
   language number (2..6). */
UiLang ui_lang_resolve(int choice, int discLang, int hasGameLanguage, int gameLanguage);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_UI_LANG_H */

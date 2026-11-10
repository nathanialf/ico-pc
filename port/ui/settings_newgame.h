/*
 * port/ui/settings_newgame.h
 *
 * The New Game screen (settings_newgame.c; its public side is settings.h's
 * ui_new_game_screen_*), built and reset by settings.c with the menus, and
 * the few calls of settings.c's it builds with.  Kept in its own file so
 * settings.c stays under the repository's 256 KiB file cap.
 */
#ifndef PORT_UI_SETTINGS_NEWGAME_H
#define PORT_UI_SETTINGS_NEWGAME_H

#ifdef __cplusplus
extern "C" {
#endif

/* the notes' text size (settings.c NOTE_SIZE) */
#define UI__NOTE_SIZE 19.0f

/* settings.c's: a port layout over rows first .. last - 1 run by proc with
   the cursor on def (its fades and shade as every Settings page's), and a
   note row's text from a string id, wrapped as the pages' notes are */
int ui__settings_add_layout(int first, int last, float colA, int (*proc)(int, int), int def);
void ui__settings_set_note(int row, int strId);
/* settings.c's: the Customize page's layout, it and its three pages on
   their first rows (the New Game screen's Customize row opens it) */
int ui__settings_open_customize(void);

/* the screen's rows and layout, built after the pages (ui_settings_install) */
void ui__newgame_build(void);
/* forgets them (ui_settings_reset) */
void ui__newgame_reset(void);
/* Customize's Back: the screen's layout, its cursor on Customize */
int ui__newgame_customize_back(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_SETTINGS_NEWGAME_H */

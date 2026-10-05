/*
 * port/game/options.h
 *
 * The port's gameplay options (docs/port/OPTIONS.md): one place that holds
 * them, so that the game hooks, the input layer, the audio layer and the
 * Settings menu (package 6B) all read and write the same values.
 *
 * Each option is read once from config.toml ([gameplay], via
 * ico_config_get_bool) the first time it is asked for, and can be set at run
 * time. Every default is the original game's behaviour, except
 * classic_menu_text and circle_back (default-on port behaviour,
 * docs/port/DIVERGENCES.md "Optional features").
 *
 *   [gameplay] stick_fix   false  the stick fix (docs/port/INPUT.md)
 *   [gameplay] yorda_safe  false  the shadows never take Yorda
 *   [gameplay] mirror      false  mirrored play (negated stick X, swapped pan)
 *   [gameplay] developer_mode
 *                          false  the development build's debug menu and
 *                                 debug options (docs/port/DEVELOPER_MODE.md)
 *   [game] classic_menu_text
 *                          false  the menus' text drawn from the PS2's
 *                                 pre-rendered sheets instead of the port
 *                                 font (docs/port/UI.md, "Menu text")
 *   [game] circle_back     true   Circle backs out of the game's menus as
 *                                 Triangle does (docs/port/SETTINGS.md);
 *                                 false is the PS2's behaviour
 *   [dev] debug_option     0      with developer mode: non-zero loads the
 *                                 debug option table the Debug Mode page saves
 *
 * Game code (ico2/) reads ico_opt_yorda_safe() and ico_opt_developer_mode()
 * under #ifdef ICO_HOST only.
 */
#ifndef ICO_PORT_GAME_OPTIONS_H
#define ICO_PORT_GAME_OPTIONS_H

int ico_opt_stick_fix(void);
void ico_opt_set_stick_fix(int on);
int ico_opt_yorda_safe(void);
void ico_opt_set_yorda_safe(int on);
/* Mirror mode: ico_input negates the stick X (ico_input_mirror), the audio
   host swaps the pan (ico_audio_mirror), the renderer flips the picture
   (rd.h rd_SetMirror, through the listener below; renderer wave 7, R7c).
   The value is the run's: [gameplay] mirror (default false) until the
   player picks at New Game (port/ui/settings.h ui_MirrorScreen*) or loads a
   save, whose slot's flag the port config keeps (below). */
int ico_opt_mirror(void);
void ico_opt_set_mirror(int on);
/* Called with the new value on every ico_opt_set_mirror, and once at
   registration with the current one (the Settings module registers the
   renderer's rd_SetMirror).  NULL unregisters.  One listener. */
void ico_opt_set_mirror_listener(void (*fn)(int on));
/* The title (layout_action.c, ICO_HOST): no run is in progress, so the
   value goes back to [gameplay] mirror (default false). */
void ico_opt_mirror_reset(void);

/* The mirror flag of each save slot (R7c, docs/port/SAVES.md "Mirror
   mode"), kept in config.toml, never in the card files:
     [mirror] slot_N = true/false     the run's flag when slot N was saved
              slot_N_sum = <uint32>   that save's game-block checksum
   N is the save file's number (game.00N, 0..9).  The checksum ties the
   entry to the save it was written for: a slot overwritten elsewhere (a
   PS2 save copied in, another folder card) has another sum and reads Off.
   ico_mirror_slot_saved writes both keys from the run's value and saves
   the file (0, or -1 when it cannot be written); ico_mirror_slot_loaded
   reads them back, sets the run's value (Off without a matching entry) and
   returns it. */
int ico_mirror_slot_saved(int slot, unsigned int sum);
int ico_mirror_slot_loaded(int slot, unsigned int sum);
/* The stored entry: 1 or 0, -1 when there is none or its sum differs. */
int ico_mirror_slot_get(int slot, unsigned int sum);
/* Developer mode (renderer wave 6, R6a): Main calls debug_Menu each tick
   (SELECT opens it), the retail build's dummy debug prints draw through the
   debug font, and debug_VariableInit may load a saved option table.  The
   achievements package suspends achievements while it is on; the trace
   records it in its header. */
int ico_opt_developer_mode(void);
void ico_opt_set_developer_mode(int on);
/* [dev] debug_option (an int, 0 when absent): read by debug_GetDebugOption
   in developer mode in place of the development build's disc-side option
   file check (common/src/debug.c).  0: the retail defaults; any other value:
   load <pref>/dev/thisIsYourDebugOption, the file the Debug Mode page's
   TRIANGLE writes. */
int ico_opt_debug_option(void);
/* [game] classic_menu_text (package P3): 1 draws the game's menu text from
   its textures, 0 (the default) with the port font.  The Settings module
   hands the value to port/ui (ui_MenuTextSetClassic) at install and on a
   change. */
int ico_opt_classic_menu_text(void);
void ico_opt_set_classic_menu_text(int on);
/* [game] circle_back (package Q2): 1 (the default) makes Circle an alias of
   the game menus' Triangle back action (common/src/layout_action.c,
   layout_texture.c default_item_select, through port/ui/layout_ext.h
   lt_ext_BackButtons); 0 is the PS2's behaviour.  The Settings module hands
   the value to port/ui (lt_ext_SetCircleBack) at install and on a change. */
int ico_opt_circle_back(void);
void ico_opt_set_circle_back(int on);
/* Forget the run-time values: each option is read from the config again on
   its next use. */
void ico_opt_reload(void);

#endif /* ICO_PORT_GAME_OPTIONS_H */

/*
 * port/game/options.h
 *
 * The port's gameplay options (docs/port/OPTIONS.md): one place that holds
 * them, so that the game hooks, the input layer, the audio layer and the
 * Settings menu (package 6B) all read and write the same values.
 *
 * Each option is read once from config.toml ([gameplay], via
 * ico_config_get_bool) the first time it is asked for, and can be set at run
 * time. Every default is the original game's behaviour.
 *
 *   [gameplay] stick_fix   false  the stick fix (docs/port/INPUT.md)
 *   [gameplay] yorda_safe  false  the shadows never take Yorda
 *   [gameplay] mirror      false  mirrored play (negated stick X, swapped pan)
 *   [gameplay] developer_mode
 *                          false  the development build's debug menu and
 *                                 debug options (docs/port/DEVELOPER_MODE.md)
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
   host swaps the pan (ico_audio_mirror); the renderer's half is package 6's. */
int ico_opt_mirror(void);
void ico_opt_set_mirror(int on);
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
/* Forget the run-time values: each option is read from the config again on
   its next use. */
void ico_opt_reload(void);

#endif /* ICO_PORT_GAME_OPTIONS_H */

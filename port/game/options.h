/*
 * port/game/options.h
 *
 * The port's gameplay options: one place that holds
 * them, so that the game hooks, the input layer, the audio layer and the
 * Settings menu all read and write the same values.
 *
 * Each option is read once from config.toml ([gameplay], via
 * ico_config_get_bool) the first time it is asked for, and can be set at run
 * time. Every default is the original game's behaviour, except
 * circle_back (default-on port behaviour).
 *
 *   [gameplay] stick_fix   false  the stick fix (off on every platform)
 *   [gameplay] yorda_safe  false  the shadows never take Yorda
 *   [gameplay] mirror      false  mirrored play (negated stick X, swapped pan)
 *   [gameplay] developer_mode
 *                          false  the development build's debug menu and
 *                                 debug options
 *   [game] circle_back     true   Circle backs out of the game's menus as
 *                                 Triangle does;
 *                                 false is the PS2's behaviour
 *   [dev] debug_option     0      with developer mode: non-zero loads the
 *                                 debug option table the Debug Mode page saves
 *
 * Game code (ico2/) reads ico_opt_yorda_safe() and ico_opt_developer_mode()
 * under #ifdef ICO_HOST only.
 */
#ifndef ICO_PORT_GAME_OPTIONS_H
#define ICO_PORT_GAME_OPTIONS_H

/* The model viewer is up (port/game/model_viewer.h), from the
   model's choice until the title is back.  While it is set, a loading
   stage other than the title starts no script (common/src/sceneManager.c)
   and the achievements are suspended (gamestate.c); nothing else reads it. */
extern int ico_mv_active;

int ico_opt_stick_fix(void);
void ico_opt_set_stick_fix(int on);
/* stick_fix's default when config.toml has none: off on every platform
   (ico_opt_stick_fix_default_for ignores its argument); a file that names
   the key keeps its value.  The one default every reader of
   gameplay.stick_fix uses. */
int ico_opt_stick_fix_default_for(int android);
int ico_opt_stick_fix_default(void);
int ico_opt_yorda_safe(void);
void ico_opt_set_yorda_safe(int on);
/* Mirror mode: ico_input negates the stick X (ico_input_mirror), the audio
   host swaps the pan (ico_audio_pan_mirror), the renderer flips the picture
   (rd.h rd_set_mirror, through the listener below).
   The value is the run's: [gameplay] mirror (default false) until the
   player picks at New Game (port/ui/settings.h ui_new_game_screen_*) or loads a
   save, whose slot's flag the port config keeps (below). */
int ico_opt_mirror(void);
void ico_opt_set_mirror(int on);
/* Called with the new value on every ico_opt_set_mirror, and once at
   registration with the current one (the Settings module registers the
   renderer's rd_set_mirror).  NULL unregisters.  One listener. */
void ico_opt_set_mirror_listener(void (*fn)(int on));
/* The title (layout_action.c, ICO_HOST): no run is in progress, so the
   value goes back to [gameplay] mirror (default false). */
void ico_opt_mirror_reset(void);

/* The mirror flag of each save slot, kept in config.toml, never in the card
   files:
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
/* Developer mode: Main calls debug_Menu each tick
   (SELECT opens it), the retail build's dummy debug prints draw through the
   debug font, and debug_VariableInit may load a saved option table.
   Achievements are suspended while it is on; the trace
   records it in its header. */
int ico_opt_developer_mode(void);
void ico_opt_set_developer_mode(int on);
/* [dev] debug_option (an int, 0 when absent): read by debug_GetDebugOption
   in developer mode in place of the development build's disc-side option
   file check (common/src/debug.c).  0: the retail defaults; any other value:
   load <pref>/dev/thisIsYourDebugOption, the file the Debug Mode page's
   TRIANGLE writes. */
int ico_opt_debug_option(void);
/* [game] circle_back: 1 (the default) makes Circle an alias of
   the game menus' Triangle back action (common/src/layout_action.c,
   layout_texture.c default_item_select, through port/ui/layout_ext.h
   lt_ext_back_buttons); 0 is the PS2's behaviour.  The Settings module hands
   the value to port/ui (lt_ext_set_circle_back) at install and on a change. */
int ico_opt_circle_back(void);
void ico_opt_set_circle_back(int on);
/* [audio] output: the
   PS2's stereo or mono choice, the game's soundOutputModeGet value (0
   stereo, 1 mono), which the memory card's system file keeps and the
   Options screen's Stereo/Mono row toggles.  "auto" (ICO_OUTPUT_AUTO, the
   default) leaves it to the game as on the PS2; "stereo" or "mono" wins
   over the card's value.  Unknown text reads as auto. */
#define ICO_OUTPUT_AUTO (-1)
#define ICO_OUTPUT_STEREO 0
#define ICO_OUTPUT_MONO 1
int ico_opt_output_mode(void);
void ico_opt_set_output_mode(int mode);
/* "auto", "stereo", "mono": the key's text */
const char *ico_opt_output_name(int mode);
/* The game's mode to set, given the game's `current` one: the explicit
   key, else the game's own value (the card's or the Options row's, or
   `current` the first time, which is then recorded as the game's).
   ui_settings_install and the Settings row apply it with soundOutputModeSet
   when it differs from current. */
int ico_opt_output_resolve(int current);
/* The memory card hook (fumi/ios/mcard.c, after the system file's mode is
   set): records the card's mode as the game's own and returns the mode in
   force (the key's when explicit). */
int ico_opt_output_card(int card_mode);
/* The Options screen hook (common/src/layout_action.c, the Stereo/Mono
   row; now Settings > Audio > Output's Stereo and Mono, the Options
   screen being no longer reached): records the new mode as the game's own; with an explicit key the
   key follows it and config.toml is saved at once, so the file and the
   card (which the game writes from soundOutputModeGet) never disagree. */
void ico_opt_output_toggled(int mode);
/* What ico_opt_output_toggled does to the game's state, without the key
   or the save: Settings > Audio > Output's Stereo and Mono, which set the
   key themselves and save it on leaving the page with the rest. */
void ico_opt_output_record(int mode);
/* Forget the run-time values: each option is read from the config again on
   its next use (and the characters' colours, appearance.h). */
void ico_opt_reload(void);

#endif /* ICO_PORT_GAME_OPTIONS_H */

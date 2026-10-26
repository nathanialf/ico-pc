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
 *   [gameplay] door_fix    false  holding hands, an arrival does not send
 *                                 Ico and Yorda straight back through the
 *                                 door they came in by (door_grace.h)
 *   [gameplay] mirror      false  mirrored play (negated stick X, swapped pan)
 *   [gameplay] natural_chains
 *                          false  a chain let down for the player keeps its
 *                                 links' size and shows more of itself as it
 *                                 comes down (port/render/rd_chain.h); draw
 *                                 only, so no pad-recording key
 *   [gameplay] skip_opening
 *                          false  the boot starts at the title screen: no
 *                                 Sony presents sign and no opening scene
 *                                 (common/src/kanbanBoot.c, script/src/op.c);
 *                                 a pad-recording key (boot timing)
 *   [gameplay] hold_type   "disc" Yorda's hand: "disc" plays the disc's own
 *                                 way (A on the European, Japanese, Korean and
 *                                 Asian discs, B on the three 2001 discs);
 *                                 "hold" keeps R1 held (the game's Hold type
 *                                 A); "toggle" takes it with one press and
 *                                 lets go with another (B)
 *   [gameplay] developer_mode
 *                          false  the development build's debug menu and
 *                                 debug options
 *   [game] circle_back     true   Circle backs out of the game's menus as
 *                                 Triangle does;
 *                                 false is the PS2's behaviour (the
 *                                 European disc's; the Japanese disc's
 *                                 menus confirm with Circle)
 *   [ui] language          auto   the language of the port's own text:
 *                                 auto (Japanese on the Japanese disc,
 *                                 the game's language on the European
 *                                 one) or en, fr, de, it, es, ja
 *   [dev] debug_option     0      with developer mode: non-zero loads the
 *                                 debug option table the Debug Mode page saves
 *
 * Game code (ico2/) reads ico_opt_yorda_safe() and ico_opt_developer_mode()
 * under #ifdef ICO_HOST only; door_fix is read by door_grace.c, which the
 * game's exit check calls; natural_chains by seki/src/BgAnimation.c under
 * #ifdef ICO_RD (the window build); skip_opening by common/src/kanbanBoot.c
 * and script/src/op.c.
 */
#ifndef ICO_PORT_GAME_OPTIONS_H
#define ICO_PORT_GAME_OPTIONS_H

/* The model viewer is up (port/game/model_viewer.h), from the
   model's choice until the title is back.  While it is set, a loading
   stage other than the title starts no script (common/src/sceneManager.c),
   a motion enters no effect (sugipon/src/frameDependSequence.c playEff)
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
/* [gameplay] door_fix (default false): the doorway fix of door_grace.h */
int ico_opt_door_fix(void);
void ico_opt_set_door_fix(int on);
/* [gameplay] natural_chains (default false): Natural chains (rd_chain.h) */
int ico_opt_natural_chains(void);
void ico_opt_set_natural_chains(int on);
/* [gameplay] skip_opening (default false): Skip the opening, read by the
   boot (kanbanBoot.c at its start, op.c at the title script's first entry),
   so a change takes effect at the next start */
int ico_opt_skip_opening(void);
void ico_opt_set_skip_opening(int on);
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
   N is the save file's number (game.00N, 0..9).  Each disc has its own
   keys, its saves being another card directory: slot_N and slot_N_sum
   for the European disc, jpn_slot_N and jpn_slot_N_sum for the Japanese
   one, kor_slot_N and kor_slot_N_sum for the Korean one (port/core/region.h
   key_prefix, by the selected region).  The checksum ties the
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
/* Players (the game's girlControlMode: 0 one player, 1 two, Yorda on the
   second controller) of each save slot on the discs whose game block has no
   word for it (the North American disc, the August 2001 preview and the
   June 2001 demo: mcard.c gameblock_write writes it after the game block on
   the others), kept in config.toml as the mirror flag is:
     [players] slot_N = 1 or 2          the count when slot N was saved
               slot_N_sum = <uint32>    that save's game-block checksum
   with the region's key_prefix (ntsc_, aug6_, jun26_).  _saved writes both
   keys and saves the file (0, or -1 when it cannot be written; logged both
   ways); _loaded returns the value to set girlControlMode to (0 without a
   matching entry: one player), logged; _get the stored value, 1 or 0, -1
   for none or another sum.  The game calls them from layout_action.c's
   load and save steps on those discs only. */
int ico_players_slot_saved(int slot, unsigned int sum, int mode);
int ico_players_slot_loaded(int slot, unsigned int sum);
int ico_players_slot_get(int slot, unsigned int sum);
/* The girl's control thread on the 2001 discs (girl_act.c
   subGirlControl_<disc>) reports girlControlMode each frame; a change is
   logged once: "options: Players <1|2> on <disc id> (Yorda on the second
   controller, port 1 | Yorda led by the game)". */
void ico_players_note(int mode);
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
/* [ui] language: the language of the port's own text (the menus it adds,
   photo mode's help, the messages): "auto" (-1, the default: the disc's,
   port/ui/ui_lang.h ui_settings_resolve_language) or "en", "fr", "de",
   "it", "es", "ja", "ko", "zh" (0..7, port/ui/strings.h UiLang order); another value
   is "auto", logged once.  The game's own text is always the disc's.
   ico_opt_ui_language_name gives the config value of one ("auto" for -1;
   the Settings menu writes it). */
int ico_opt_ui_language(void);
void ico_opt_set_ui_language(int lang);
const char *ico_opt_ui_language_name(int lang);
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
/* The Options screen hook (common/src/layout_action.c, the Sound row 308;
   the pause menu opens Settings, so the game's Options screen is not
   reached in play, and Settings > Audio > Output records through
   ico_opt_output_record): records the new mode as the game's own; with an explicit key the
   key follows it and config.toml is saved at once, so the file and the
   card (which the game writes from soundOutputModeGet) never disagree. */
void ico_opt_output_toggled(int mode);
/* What ico_opt_output_toggled does to the game's state, without the key
   or the save: Settings > Audio > Output's Stereo and Mono, which set the
   key themselves and save it on leaving the page with the rest. */
void ico_opt_output_record(int mode);
/* The mode the memory card saves: the game's own (the card's or the
   Options screen's), not an explicit key's override in force; `live` (the
   game's mode now) when the game's own is not known yet. */
int ico_opt_output_game(int live);
/* [gameplay] hold_type: the game's Hold type (optionControlType), kept in
   config.toml instead of the memory card.  The key holds a choice:
   ICO_HOLD_DISC ("disc", the default: the disc's own, port/core/region.h
   hold_type_own), ICO_HOLD_HOLD ("hold", A: keep R1 held to hold Yorda's
   hand) or ICO_HOLD_TOGGLE ("toggle", B: press R1 once to take her hand,
   again to let go).  Another word reads as "disc", logged once.  Every
   version before 0.9.1 wrote "hold" into a new config.toml, and the 2001
   discs' game never read it, so a "hold" is the player's choice only with
   [gameplay] hold_type_chosen = true beside it (written with every choice
   from Options); without it "hold" reads as "disc" (the same A on the
   European, Japanese, Korean and Asian discs; the toggle the 2001 discs
   always played).  config.toml is never rewritten for this.
   ico_opt_hold_type() is the type in force, ICO_HOLD_HOLD or
   ICO_HOLD_TOGGLE, the choice resolved for the selected disc (a log line
   once for each choice and disc: "options: hold type <disc's own|hold|
   toggle> (A|B) on <disc id>").  The memory card hook (fumi/ios/mcard.c
   gameblock_read and its 2001 arms) and Settings (ui_settings_install,
   the Hold type row) copy it into optionControlType; options.c itself
   touches no game variable. */
#define ICO_HOLD_HOLD 0
#define ICO_HOLD_TOGGLE 1
#define ICO_HOLD_DISC 2
int ico_opt_hold_type(void);
/* the choice, ICO_HOLD_DISC, ICO_HOLD_HOLD or ICO_HOLD_TOGGLE */
int ico_opt_hold_choice(void);
/* sets the choice (another value reads as ICO_HOLD_DISC) for this run */
void ico_opt_set_hold_choice(int choice);
/* the explicit type: 0 sets ICO_HOLD_HOLD, anything else ICO_HOLD_TOGGLE */
void ico_opt_set_hold_type(int type);
/* the key's word of a choice: "hold", "toggle" or "disc" */
const char *ico_opt_hold_type_name(int choice);
/* Writes the run's choice into [gameplay] hold_type and hold_type_chosen
   (true) without saving the file: 0, or -1 when it cannot be set. */
int ico_opt_hold_choice_store(void);
/* The Options screen hook (common/src/layout_action.c, the Hold type row
   318, reached only on a disc whose layout tables are not recognised): sets
   the type, writes the key and saves config.toml at once. */
void ico_opt_hold_type_toggled(int type);
/* Forget the run-time values: each option is read from the config again on
   its next use (and the characters' colours, appearance.h). */
void ico_opt_reload(void);

#endif /* ICO_PORT_GAME_OPTIONS_H */

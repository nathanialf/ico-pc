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
 *
 * Game code (ico2/) reads ico_opt_yorda_safe() under #ifdef ICO_HOST only.
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

/* Forget the run-time values: each option is read from the config again on
   its next use. */
void ico_opt_reload(void);

#endif /* ICO_PORT_GAME_OPTIONS_H */

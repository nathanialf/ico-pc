/*
 * port/config/sysconf.h
 *
 * libscf's system configuration (sceScfGetLanguage, sceScfGetTimeZone,
 * sceScfGetSummerTime; the declarations are port/compat/libscf.h) answered
 * from the port's settings and the host's locale.
 */
#ifndef ICO_CONFIG_SYSCONF_H
#define ICO_CONFIG_SYSCONF_H

/* libscf's numbering (sceScfGetLanguage). The game has English, French,
   Spanish, German and Italian screens; Japanese, Dutch and Portuguese are
   never answered. */
#define ICO_SCF_LANGUAGE_JAPANESE 0
#define ICO_SCF_LANGUAGE_ENGLISH 1
#define ICO_SCF_LANGUAGE_FRENCH 2
#define ICO_SCF_LANGUAGE_SPANISH 3
#define ICO_SCF_LANGUAGE_GERMAN 4
#define ICO_SCF_LANGUAGE_ITALIAN 5

/* "en", "fr", "de", "it" or "es" (any case, "auto" is not one) to its code,
   or -1. */
int ico_scf_language_from_name(const char *name);
/* A locale name ("fr_FR.UTF-8", "de-AT", "es") to the code of its language
   when the game has it, else English. */
int ico_scf_language_from_locale(const char *locale);
/* The code the host's locale gives: SDL_GetPreferredLocales in the window
   build, else LC_ALL, LC_MESSAGES, LANG (not "C" or "POSIX"); English when
   nothing matches. */
int ico_sysconf_host_language(void);
/* Forget the answer sceScfGetLanguage cached (tests). */
void ico_sysconf_reset(void);

/* --- Phase 6 (6C): the boot screens' choices from the config ------------- */

/* The game's language numbers, as the boot language screen stores them in
   NonLinearCameraMove (common/src/kanbanBoot.c step 102; texFile's language
   column): 2 English, 3 French, 4 German, 5 Italian, 6 Spanish. */
#define ICO_GAME_LANGUAGE_ENGLISH 2
#define ICO_GAME_LANGUAGE_FRENCH 3
#define ICO_GAME_LANGUAGE_GERMAN 4
#define ICO_GAME_LANGUAGE_ITALIAN 5
#define ICO_GAME_LANGUAGE_SPANISH 6

/* A libscf code to the game's number the way the boot screen maps it: step
   101 puts the cursor on item 26 + {EN 0, FR 1, DE 2, IT 3, ES 4} for libscf
   1, 2, 4, 5, 3 and leaves it on the default item 26 (English) otherwise,
   and step 102 stores 26..30 as 2..6. */
int ico_scf_to_game_language(int scf);
/* The inverse (2..6 to libscf); English for anything else. */
int ico_game_to_scf_language(int game);
/* [game] language when it names a language ("en" .. "es"), as a libscf code;
   -1 for "auto", absent or invalid. Does not touch sceScfGetLanguage's cache
   or the EE timers. */
int ico_sysconf_language_explicit(void);
/* The Settings menu's language: sceScfGetLanguage answers scf from now on
   and [game] language is set to its name (saved by ico_config_save). */
void ico_sysconf_set_language(int scf);

/* [video] video_mode: "pal50" (systemStatus[0] = 1, the PAL game's default)
   or "60hz" (0), the boot screen's 50/60 Hz choice (kanbanBoot.c step
   201: item 33 sets 1, item 34 sets 0). Returns 1 or 0, or -1 when the key
   is absent or not one of the two. */
int ico_sysconf_video_mode(void);
/* Sets [video] video_mode from a systemStatus[0] value. */
void ico_sysconf_set_video_mode(int pal);
/* The boot skip (kanbanBoot.c under ICO_HOST): the value the skipped screen
   would have stored. Language: sceScfGetLanguage mapped to 2..6. Video
   mode: [video] video_mode, else current (the screen's default item keeps
   it: 1, 50 Hz). */
int ico_boot_language(void);
int ico_boot_video_mode(int current);
/* After the card's system file is loaded (kanbanBoot.c step 96): an
   explicit config value wins over the card's (the Settings menu writes it;
   the card is written with the in-memory values at the next save, as
   before); "auto" or absent keeps the card's. */
int ico_boot_card_language(int card);
int ico_boot_card_video_mode(int card);

#endif /* ICO_CONFIG_SYSCONF_H */

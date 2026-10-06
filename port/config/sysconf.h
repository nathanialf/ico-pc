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
/* The Settings menu's language: sceScfGetLanguage answers scf from now on
   and [game] language is set to its name (saved by ico_config_save). */
void ico_sysconf_set_language(int scf);

/* [video] video_mode: "pal50" (systemStatus[0] = 1, the PAL game's own
   default) or "60hz" (0, the port's default: ico_boot_video_mode), the boot
   screen's 50/60 Hz choice (kanbanBoot.c step 201: item 33 sets 1, item 34
   sets 0). Returns 1 or 0, or -1 when the key is absent or not one of the
   two. */
int ico_sysconf_video_mode(void);
/* Sets [video] video_mode from a systemStatus[0] value. */
void ico_sysconf_set_video_mode(int pal);
/* The boot screens' values, which common/src/main.c sets on every boot
   before stage 1 loads and the GS starts (kanbanBoot.c skips the screens,
   and the card's saved values are not applied). Language: sceScfGetLanguage
   ([game] language, else the system's) mapped to 2..6. Video mode:
   [video] video_mode, else 0 (60 Hz). */
int ico_boot_language(void);
int ico_boot_video_mode(void);

#endif /* ICO_CONFIG_SYSCONF_H */

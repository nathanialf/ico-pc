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

#endif /* ICO_CONFIG_SYSCONF_H */

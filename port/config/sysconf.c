/*
 * port/config/sysconf.c
 *
 * sceScfGetLanguage and friends (sysconf.h). The language is [game] language
 * when it is not "auto", else the host's locale. The PS2's console language
 * only preselects the boot menu's cursor (common/src/kanbanBoot.c, step 101:
 * the language screen still shows); Phase 6 skips that screen and uses this
 * value directly.
 */
#include "sysconf.h"
#include "clock.h"
#include "config.h"
#include <ctype.h>
#include <libscf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ICO_CONFIG_SDL
#include <SDL3/SDL_locale.h>
#endif

static const struct {
    const char *name;
    int code;
} languages[] = {
    {"en", ICO_SCF_LANGUAGE_ENGLISH}, {"fr", ICO_SCF_LANGUAGE_FRENCH},
    {"de", ICO_SCF_LANGUAGE_GERMAN},  {"it", ICO_SCF_LANGUAGE_ITALIAN},
    {"es", ICO_SCF_LANGUAGE_SPANISH},
};

int ico_scf_language_from_name(const char *name)
{
    size_t i;

    if (name == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(languages) / sizeof(languages[0]); i++) {
        const char *a = name;
        const char *b = languages[i].name;

        while (*a != '\0' && tolower((unsigned char)*a) == *b) {
            a++;
            b++;
        }
        if (*a == '\0' && *b == '\0') {
            return languages[i].code;
        }
    }
    return -1;
}

int ico_scf_language_from_locale(const char *locale)
{
    char lang[4];
    size_t n = 0;
    int code;

    if (locale == NULL) {
        return ICO_SCF_LANGUAGE_ENGLISH;
    }
    while (n < 3 && isalpha((unsigned char)locale[n])) {
        lang[n] = locale[n];
        n++;
    }
    lang[n] = '\0';
    if (n != 2 || isalpha((unsigned char)locale[n])) { /* "eng" is not "en" */
        return ICO_SCF_LANGUAGE_ENGLISH;
    }
    code = ico_scf_language_from_name(lang);
    return code >= 0 ? code : ICO_SCF_LANGUAGE_ENGLISH;
}

static const char *env_locale(void)
{
    static const char *const vars[] = {"LC_ALL", "LC_MESSAGES", "LANG"};
    size_t i;

    for (i = 0; i < sizeof(vars) / sizeof(vars[0]); i++) {
        const char *v = getenv(vars[i]);

        if (v != NULL && v[0] != '\0' && strcmp(v, "C") != 0 && strcmp(v, "POSIX") != 0) {
            return v;
        }
    }
    return NULL;
}

int ico_sysconf_host_language(void)
{
#ifdef ICO_CONFIG_SDL
    int n = 0;
    SDL_Locale **loc = SDL_GetPreferredLocales(&n);

    if (loc != NULL) {
        int i;
        int code = -1;

        /* the first preferred locale the game has a language for */
        for (i = 0; i < n && code < 0; i++) {
            code = ico_scf_language_from_name(loc[i]->language);
        }
        SDL_free(loc);
        if (code >= 0) {
            return code;
        }
    }
#endif
    return ico_scf_language_from_locale(env_locale());
}

static int cached = -1;

void ico_sysconf_reset(void)
{
    cached = -1;
}

int sceScfGetLanguage(void)
{
    ico_clock_timers_attach(); /* the first libscf call of the boot */
    if (cached < 0) {
        const char *cfg = ico_config_get_string("game.language", "auto");
        int code = ico_scf_language_from_name(cfg);

        if (code >= 0) {
            fprintf(stderr, "scf: language %d from [game] language = \"%s\"\n", code, cfg);
        } else {
            code = ico_sysconf_host_language();
            if (strcmp(cfg, "auto") != 0) {
                fprintf(stderr,
                        "scf: [game] language = \"%s\" is not one of auto, en, fr, de, "
                        "it, es\n",
                        cfg);
            }
            fprintf(stderr, "scf: language %d from the system locale\n", code);
        }
        cached = code;
    }
    return cached;
}

/* minutes east of GMT; the clock sceCdReadClock reports is already local */
int sceScfGetTimeZone(void)
{
    return 0;
}

int sceScfGetSummerTime(void)
{
    return 0;
}

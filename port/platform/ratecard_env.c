/*
 * port/platform/ratecard_env.c
 *
 * The rate card's results file for the renderer (ratecard_env.h).
 */
#include "ratecard_env.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ratecard_env_begin(const char *activityPath, const char *version)
{
    char p[1100];
    size_t dir = activityPath != NULL ? strlen(activityPath) : 0;
    while (dir > 0 && activityPath[dir - 1] != '/' && activityPath[dir - 1] != '\\') {
        dir--;
    }
    if (activityPath == NULL || dir > 1024u) {
        return;
    }
    snprintf(p, sizeof p, "%.*sratecard.txt", (int)dir, activityPath);
    const char *v = version != NULL ? version : "";
#ifdef _WIN32
    _putenv_s("ICO_RATECARD_FILE", p);
    _putenv_s("ICO_RATECARD_VERSION", v);
#else
    setenv("ICO_RATECARD_FILE", p, 1);
    setenv("ICO_RATECARD_VERSION", v, 1);
#endif
}

/*
 * port/platform/ratecard_env.c
 *
 * The rate card's results file for the renderer (ratecard_env.h).
 */
#include "ratecard_env.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vu_zero_mark.h"

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
    /* no card on a start after a bad end of any kind (REV4R N3): the end as
       the cull's step classed it (vu_zero_mark_end_read, diag_host.c runs
       the marks first), bad where an attempt that was on would block on it
       (a crash, an ANR, a signal other than 9, a device loss, a GPU hang,
       the watchdog) */
    char words[160], skip[240] = "";
    const int end = vu_zero_mark_end_read(words, sizeof words);
    if (end >= 0 && vu_zero_mark_blocks(VU_ZERO_STAGE_ON, end)) {
        snprintf(skip, sizeof skip, "the run before ended badly (%.200s)", words);
    }
#ifdef _WIN32
    _putenv_s("ICO_RATECARD_FILE", p);
    _putenv_s("ICO_RATECARD_VERSION", v);
    _putenv_s("ICO_RATECARD_SKIP", skip);
#else
    setenv("ICO_RATECARD_FILE", p, 1);
    setenv("ICO_RATECARD_VERSION", v, 1);
    if (skip[0] != '\0') {
        setenv("ICO_RATECARD_SKIP", skip, 1);
    } else {
        unsetenv("ICO_RATECARD_SKIP");
    }
#endif
}

/*
 * port/ui/settings_unused.c
 *
 * Settings > Extras > Unused content (settings_unused.h): the host's
 * answers for the page's four rows and its link.  The host is the
 * program's (port/game/unused_live.c over ico_unused.h) or a test's fake;
 * without one the page's link is hidden.
 */
#include "settings.h"
#include "settings_unused.h"

#include <stdio.h>

#include "../core/region.h" /* ico_region_cur: the disc's id for the log */

#define ROWS (UI_OPT_UNUSED_ART - UI_OPT_UNUSED_ARENA + 1)

static UiUnusedHost s_host;
static int s_hostSet;
/* the disc the "no Unused content page" line was written for */
static const IcoRegionInfo *s_noneLogged;

void ui_settings_set_unused_host(const UiUnusedHost *host)
{
    s_hostSet = host != NULL && host->available != NULL && host->open != NULL;
    if (s_hostSet) {
        s_host = *host;
    }
    s_noneLogged = NULL;
}

int ui__unused_row(int opt)
{
    return opt >= UI_OPT_UNUSED_ARENA && opt <= UI_OPT_UNUSED_ART ? opt - UI_OPT_UNUSED_ARENA : -1;
}

int ui__unused_shown(int opt)
{
    const int row = ui__unused_row(opt);
    return row >= 0 && s_hostSet && s_host.available(row) == 1;
}

int ui__unused_any_shown(void)
{
    if (!s_hostSet) {
        return 0;
    }
    for (int row = 0; row < ROWS; row++) {
        if (s_host.available(row) == 1) {
            return 1;
        }
    }
    if (s_noneLogged != ico_region_cur()) {
        s_noneLogged = ico_region_cur();
        fprintf(stderr, "extras: no Unused content page on %s (its disc has none of the rows)\n",
                ico_region_cur()->disc_id);
    }
    return 0;
}

int ui__unused_open(int opt)
{
    const int row = ui__unused_row(opt);
    if (row < 0 || !s_hostSet) {
        fprintf(stderr, "extras: unused content not available (no host)\n");
        return -1;
    }
    return s_host.open(row);
}

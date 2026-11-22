/*
 * port/ui/tune_notice.c
 *
 * The "finding the best settings" pop-up (tune_notice.h).
 */
#include "tune_notice.h"

#include <stdio.h>

#include "popup.h"
#include "strings.h"

/* the judge's word taken and not queued yet (the judge gives it once a
   run: a full queue keeps it for the next vsync) */
static int s_pending;

int ui_tune_notice(int due)
{
    if (due) {
        s_pending = 1;
    }
    if (!s_pending) {
        return 0;
    }
    if (ui_popup_push(ui_str(UI_STR_TUNE_TITLE), ui_str(UI_STR_TUNE_BODY)) != 0) {
        return -1; /* a full queue: kept, tried again at the next vsync */
    }
    s_pending = 0;
    fprintf(stderr,
            "ui: pop-up \"%s\": the in-play timing of the drawing choices began (once a "
            "run; some stutter while each area is timed)\n",
            ui_str_in(UI_LANG_EN, UI_STR_TUNE_TITLE));
    return 1;
}

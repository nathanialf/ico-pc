/*
 * port/ui/tune_notice.c
 *
 * The "finding the best settings" pop-up (tune_notice.h).
 */
#include "tune_notice.h"

#include <stdio.h>

#include "popup.h"
#include "strings.h"

int ui_tune_notice(int due)
{
    if (!due) {
        return 0;
    }
    if (ui_popup_push(ui_str(UI_STR_TUNE_TITLE), ui_str(UI_STR_TUNE_BODY)) != 0) {
        return -1; /* a full queue: dropped, as any popup is */
    }
    fprintf(stderr,
            "ui: pop-up \"%s\": the in-play timing of the drawing choices began (once a "
            "run; some stutter while each area is timed)\n",
            ui_str_in(UI_LANG_EN, UI_STR_TUNE_TITLE));
    return 1;
}

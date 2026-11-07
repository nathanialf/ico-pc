/*
 * port/ui/settings_desktop.c
 *
 * Settings > Extras > Add to the applications menu (settings_desktop.h):
 * the host's handler (port/platform/window_host.c on Linux, which writes the
 * desktop entry and the icon, port/platform/desktop_entry.h) and the popup
 * for its answer.
 */
#include "settings.h"
#include "settings_desktop.h"

#include <stdio.h>

#include "popup.h"
#include "strings.h"

static int (*s_fn)(void);

void ui_settings_set_desktop_handler(int (*fn)(void))
{
    s_fn = fn;
}

int ui__desktop_hosted(void)
{
    return s_fn != NULL;
}

void ui__desktop_press(void)
{
    int r;

    if (s_fn == NULL) {
        return;
    }
    r = s_fn();
    fprintf(stderr, "extras: add to the applications menu: %d\n", r);
    if (r == UI_DESKTOP_DONE) {
        ui_popup_push(ui_str(UI_STR_DESKTOP_ADDED), ui_str(UI_STR_DESKTOP_ADDED_BODY));
    } else {
        ui_popup_push(ui_str(UI_STR_DESKTOP_FAILED), ui_str(UI_STR_DESKTOP_FAILED_BODY));
    }
}

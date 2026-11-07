/*
 * port/ui/settings_desktop.h
 *
 * Settings > Extras > Add to the applications menu (Linux): the row's
 * state and its press (settings_desktop.c), for the page settings.c builds.
 * The public side (the host's handler) is settings.h's
 * ui_settings_set_desktop_handler.
 */
#ifndef PORT_UI_SETTINGS_DESKTOP_H
#define PORT_UI_SETTINGS_DESKTOP_H

#ifdef __cplusplus
extern "C" {
#endif

/* a handler is set: the row shows (from the title) */
int ui__desktop_hosted(void);
/* Cross on the row: the handler once, its answer as the popup (done, or
   failed) */
void ui__desktop_press(void);

#ifdef __cplusplus
}
#endif

#endif

/*
 * port/ui/settings_unused.h
 *
 * Settings > Extras > Unused content: the rows' host (settings_unused.c),
 * for the page settings.c builds.  The public side is settings.h's
 * ui_settings_set_unused_host; the rows' rule (title only) and Cross are
 * settings.c's.
 */
#ifndef PORT_UI_SETTINGS_UNUSED_H
#define PORT_UI_SETTINGS_UNUSED_H

#ifdef __cplusplus
extern "C" {
#endif

/* the page's row (0 .. 3, ico_unused.h's order) of opt, or -1 when opt is
   not one of UI_OPT_UNUSED_* */
int ui__unused_row(int opt);
/* whether the host has the row now: 0 without a host */
int ui__unused_shown(int opt);
/* whether any row is there (the Extras link's rule); the first time none
   is, on a disc, one line: "extras: no Unused content page on <disc id>" */
int ui__unused_any_shown(void);
/* Cross on the row: the host's open, the layout to switch to or -1 */
int ui__unused_open(int opt);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_SETTINGS_UNUSED_H */

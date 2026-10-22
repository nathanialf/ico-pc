/*
 * port/ui/touch_edit_ui.h
 *
 * Settings > Controls > Touch screen > Edit layout: the page's side of the
 * touch layout editor (port/input/touch_edit.h). The page is one masked
 * row (settings.c UI_PAGE_TOUCH_LAYOUT); its proc hands every call here, the
 * device layer's editor (UiTouchEditHost, settings.h) takes the fingers,
 * and the overlay draws what the editor's view says (touch_ui.c).
 */
#ifndef PORT_UI_TOUCH_EDIT_UI_H
#define PORT_UI_TOUCH_EDIT_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/* One call of the page's proc. first: the page's first call (its procFirst):
   host.begin() (the posture the phone is in, the mapper's fingers
   cancelled). Every call: ico_touch_edit_tick on the host's preview layout
   with the gamepad word (ICO_PAD_* held, from held through the LT_PAD_*
   table; a button already held when the page opened counts only once
   released); flags (the press edges) and held are 0 while the page fades.
   The editor's Done, or a back button pressed (the port screens' back
   buttons but Cross, which the editor uses: Triangle, and Circle on the
   European disc; the Android Back gesture is Triangle) end the editing:
   host.end(1) (the work kept) and ui_settings_touch_dirty(). Returns 1 from
   then until the next first call (the caller leaves the page once it has
   faded in), 0 while editing. No host (the row is locked then): 1 on a
   back press only. */
int ui_touch_edit_proc(int first, unsigned int flags, unsigned int held);
/* Forgets the page's state (ui_settings_reset). */
void ui_touch_edit_page_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_TOUCH_EDIT_UI_H */

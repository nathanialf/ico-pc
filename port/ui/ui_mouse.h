/*
 * port/ui/ui_mouse.h
 *
 * The mouse pointer in the menus (package I17b, issue 17): the port's pages
 * and lists, Extras, the title's and the pause menu's rows, the memory card
 * and save screens, any layout the game's layout code runs with a cursor.
 *
 *   - pointing at a row moves the cursor onto it (as the pad's move does,
 *     with its sound);
 *   - a click on a row is Cross on it; on a value's small arrows Left or
 *     Right; on a stepped value Right (Cross does nothing there); a click
 *     off every row does nothing (the device layer keeps the left button
 *     off Cross while a menu is up: port/input/pointer.h);
 *   - the wheel is Up or Down, a notch a move (up to three notches wait);
 *   - a key or pad press hides the pointer until the mouse is used again.
 *
 * Nothing happens while the layout fades, while the layouts' procs have
 * the item select off (the title's memory card check, a list's wrap: the
 * game's own lt_item_select_disable, as the last tick left it,
 * layout_texture.c lt_host_select_disabled) or while the remap screen waits
 * for a press.  Play (layout 54) and its scenes (55) are not menus.  The
 * pointer is the system's; nothing is drawn.
 */
#ifndef PORT_UI_UI_MOUSE_H
#define PORT_UI_UI_MOUSE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Where the presenter put the picture: the output's size and the box the
   game's 4:3 picture was blitted into (output pixels; RdOverlayCtx's
   outW, outH and box), from the presentation overlay (ui_host.c).  Until
   the first call the pointer hits nothing. */
void ui_MouseSetView(int outW, int outH, int boxX, int boxY, int boxW, int boxH);
/* Once a Main tick, after the pad is read and before the layouts run
   (common/src/main.c): takes the pointer's tick (pointer.h) and acts on
   it, adding the click's and the wheel's buttons to pad[0].flags. */
void ui_MouseTick(void);
/* Whether a menu the pointer works in is up and the pointer is not hidden
   by a key or pad press: the window shows the system pointer then. */
int ui_MouseMenuActive(void);
/* Back to the start: no view, nothing hidden or queued (tests). */
void ui_MouseReset(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_UI_MOUSE_H */

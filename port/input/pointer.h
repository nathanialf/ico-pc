/*
 * port/input/pointer.h
 *
 * The mouse pointer in the menus (INPUT.md).  Plain C, no SDL
 * and no game symbols: the device layer (input_sdl.c) feeds it while the
 * pointer is free (capture mode ICO_CAPTURE_OFF, mouse_look.h): where the
 * pointer is in the window (0..1 across and down), the left button's
 * presses, the wheel and the pointer leaving the window; the menus' glue
 * (port/ui/ui_mouse.c ui_mouse_tick) takes what happened once a Main tick
 * and says whether a menu is up (ico_pointer_set_menu), which the device
 * layer reads to keep the left button off Cross there (a click off any
 * row must not confirm the row the cursor is on).
 *
 * The game runs on a fiber of the window's thread, so nothing here needs a
 * lock.
 */
#ifndef ICO_PORT_INPUT_POINTER_H
#define ICO_PORT_INPUT_POINTER_H

#ifdef __cplusplus
extern "C" {
#endif

/* What happened since the last take. */
typedef struct IcoPointerTick {
    int valid;    /* the pointer is over the window (x, y mean something) */
    float x, y;   /* where, 0..1 across and down the window */
    int moved;    /* it moved (or came back) since the last take */
    int clicks;   /* left button presses since the last take */
    int wheel;    /* whole wheel notches, up (away from the player) positive */
    int activity; /* anything from the mouse at all (a move, a button, the wheel) */
} IcoPointerTick;

/* the pointer at (nx, ny), 0..1 across and down the window (clamped) */
void ico_pointer_move(float nx, float ny);
/* the pointer left the window (or the window lost the focus, or the
   pointer was captured): no place until the next move */
void ico_pointer_leave(void);
/* the left button went down (1) or up (0); a press counts as a click */
void ico_pointer_button(int down);
/* another mouse button (right, middle): only that the mouse was used */
void ico_pointer_other_button(void);
/* wheel steps, up positive (SDL's flipped direction already undone); the
   fractions add up until they make a whole notch */
void ico_pointer_wheel(float steps);
/* what happened since the last take, then the edges cleared (the place
   and the wheel's fraction kept); returns t->activity */
int ico_pointer_take(IcoPointerTick *t);
/* whether a menu the pointer can use is up (ui_mouse.c, once a Main tick) */
void ico_pointer_set_menu(int on);
int ico_pointer_menu(void);
/* Which menu row lies under (nx, ny), 0..1 across and down the window: the
   menu layer (ui_mouse.c) registers the probe, which gives the row's layout
   and index and returns 1, or returns 0 for no row there. Used by the touch
   layer to let a tap on a row win over a Hidden control. No probe: no row. */
typedef int (*IcoPointerRowProbe)(float nx, float ny, int *layout, int *row);
void ico_pointer_set_row_probe(IcoPointerRowProbe probe);
int ico_pointer_row_at(float nx, float ny, int *layout, int *row);
/* whether the mouse's Cross and Circle buttons trade places: set (once a
   Main tick, ui_mouse.c) on the Japanese disc while a menu or photo mode
   is up, where Circle confirms and Cross goes back, so the mouse works as
   on the European disc (the right button goes back; photo mode's left
   button saves).  The device layer swaps the two rows of the mouse's
   bindings for the step. */
void ico_pointer_set_swap_face(int on);
int ico_pointer_swap_face(void);
/* everything back to the start (no place, nothing pending, no menu) */
void ico_pointer_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* ICO_PORT_INPUT_POINTER_H */

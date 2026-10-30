/*
 * port/input/touch_eaten.h
 *
 * The finger a menu row tap took from a Hidden touch control
 * (input_sdl.c touch_menu_tap): the controls never see it, down to its
 * lift.  Pure and inline so touch_test runs the device layer's steps
 * (input_sdl.c is the window build's only).
 *
 * Review finding 4: a row tap that opened the touch editor sent its lift
 * to the editor, so the mark stayed, and the next finger with the same id
 * (on Android the first finger is usually id 0) reached the controls on
 * its DOWN but lost its MOTION and UP: a button or the stick stayed held
 * until the next touch.  The mark is now dropped wherever the menu tap's
 * click is (focus lost, the editor opened or closed), and a DOWN of the
 * marked id is a new finger.
 */
#ifndef ICO_TOUCH_EATEN_H
#define ICO_TOUCH_EATEN_H

#include <stdint.h>

#include "touch.h" /* ICO_TOUCH_DOWN, _UP, _CANCEL */

typedef struct IcoTouchEaten {
    uint64_t finger;
    int on;
} IcoTouchEaten;

/* a row tap took finger id from a Hidden control */
static inline void ico_touch_eaten_take(IcoTouchEaten *e, uint64_t id)
{
    e->on = 1;
    e->finger = id;
}

/* the mark let go (its lift or cancel, focus lost, the editor opened or
   closed) */
static inline void ico_touch_eaten_drop(IcoTouchEaten *e)
{
    e->on = 0;
}

/* Before an event of finger id reaches the controls: 1 when the row took
   the finger (the event stays from the controls; its lift or a cancel lets
   the mark go). A DOWN of the marked id is a new finger: the mark goes and
   the row may take it again (ico_touch_eaten_take after this call). */
static inline int ico_touch_eaten_event(IcoTouchEaten *e, uint64_t id, int kind)
{
    if (!e->on || e->finger != id) {
        return 0;
    }
    if (kind == ICO_TOUCH_DOWN) {
        e->on = 0;
        return 0;
    }
    if (kind == ICO_TOUCH_UP || kind == ICO_TOUCH_CANCEL) {
        e->on = 0;
    }
    return 1;
}

#endif /* ICO_TOUCH_EATEN_H */

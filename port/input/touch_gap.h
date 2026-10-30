/*
 * port/input/touch_gap.h
 *
 * Where the Frame rate counter may sit while the touch controls show: read
 * from the player's actual layout (edits applied), in the layout's own
 * output pixels. Pure arithmetic on an IcoTouchLayout (no SDL, no renderer),
 * so touch_gap_test checks it on the CPU.
 */
#ifndef PORT_INPUT_TOUCH_GAP_H
#define PORT_INPUT_TOUCH_GAP_H

#include "touch.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The counter's place. cornerX, cornerY: its place with no touch controls
   (the output's corner, in from the edges by the margin); bandH its text's
   height; natW its width at full size, minW at the smallest it is drawn
   (the shrink's floor).
   A control counts when it is drawn: a button with alpha > 0, and the left
   stick's rest ring with stickAlpha > 0 (Hidden and Off controls are not
   drawn; the look pad's area is drawn only under a finger). Its box is
   padded by 2 q on each side.
   - No counted control meets the corner's band over natW: *x0 = cornerX,
     *y0 = cornerY, returns 1.
   - Else the text goes on the keys' row (region top + 3 q): *x0 is the
     first x from cornerX, going right, where no counted control meets the
     row's band over natW (past the right edge of each one in the way), and
     the function returns 2.
   - When no such x leaves room before the safe area's right edge, the first
     x that holds minW; when none does, the corner again (returns 1), drawn
     over.
   *x1 is the padded left edge of the next counted control in the band the
   text is in (the safe area's right edge for return 2 when none), 0 for no
   limit; the caller shrinks the text to fit before it. Returns 0 (outputs
   untouched) only for a NULL or empty layout. */
int ico_touch_fps_place(const IcoTouchLayout *l, float cornerX, float cornerY, float bandH,
                        float natW, float minW, float *x0, float *x1, float *y0);

#ifdef __cplusplus
}
#endif

#endif /* PORT_INPUT_TOUCH_GAP_H */

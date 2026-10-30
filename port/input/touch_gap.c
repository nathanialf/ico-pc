/*
 * port/input/touch_gap.c
 *
 * The Frame rate counter's place among the player's actual touch controls
 * (touch_gap.h).
 */
#include "touch_gap.h"

#include <stddef.h>

#define GAP_Q 2.0f /* air each side of a control, q */
#define ROW_Q 3.0f /* the keys' row below the region's top, q */
#define MAX_BOXES (ICO_TOUCH_BUTTONS + 1)

typedef struct Box {
    float x0, x1, y0, y1;
} Box;

/* every drawn control's box, padded; returns the count */
static int collect(const IcoTouchLayout *l, Box *out)
{
    const float pad = GAP_Q * l->unit;
    int n = 0;

    for (int i = 0; i < ICO_TOUCH_BUTTONS; i++) {
        const IcoTouchButton *b = &l->button[i];

        if (!(b->alpha > 0.0f) || !(b->rect.w > 0.0f)) {
            continue;
        }
        out[n].x0 = b->rect.x - pad;
        out[n].x1 = b->rect.x + b->rect.w + pad;
        out[n].y0 = b->rect.y - pad;
        out[n].y1 = b->rect.y + b->rect.h + pad;
        n++;
    }
    if (l->stickAlpha > 0.0f && l->stickR > 0.0f) {
        out[n].x0 = l->stickHomeX - l->stickR - pad;
        out[n].x1 = l->stickHomeX + l->stickR + pad;
        out[n].y0 = l->stickHomeY - l->stickR - pad;
        out[n].y1 = l->stickHomeY + l->stickR + pad;
        n++;
    }
    return n;
}

static int meets(const Box *b, float x0, float x1, float y0, float y1)
{
    return b->x0 < x1 && b->x1 > x0 && b->y0 < y1 && b->y1 > y0;
}

/* the left edge (padded) of the nearest control in the band that starts at or
   past x + minW; none: dflt */
static float limit(const Box *box, int n, float x, float minW, float y, float bandH, float dflt)
{
    float lim = dflt;

    for (int i = 0; i < n; i++) {
        if (box[i].y0 < y + bandH && box[i].y1 > y && box[i].x0 >= x + minW &&
            (lim <= 0.0f || box[i].x0 < lim)) {
            lim = box[i].x0;
        }
    }
    return lim;
}

/* the first x from x, going right, where w pixels of the band meet no box;
   1 when it ends before right */
static int slide(const Box *box, int n, float *x, float w, float y, float bandH, float right)
{
    /* each pass moves x to the right edge of one box, so n passes settle it */
    for (int pass = 0; pass <= n; pass++) {
        int moved = 0;

        for (int i = 0; i < n; i++) {
            if (meets(&box[i], *x, *x + w, y, y + bandH)) {
                *x = box[i].x1;
                moved = 1;
            }
        }
        if (!moved) {
            break;
        }
    }
    return *x + w <= right;
}

int ico_touch_fps_place(const IcoTouchLayout *l, float cornerX, float cornerY, float bandH,
                        float natW, float minW, float *x0, float *x1, float *y0)
{
    Box box[MAX_BOXES];

    if (!l || !x0 || !x1 || !y0 || !(l->unit > 0.0f) || l->outW == 0 || l->outH == 0) {
        return 0;
    }
    const int n = collect(l, box);
    const float right = l->safeRect.x + l->safeRect.w;
    const float rowY = l->region.y + ROW_Q * l->unit;
    int clear = 1;
    float x = cornerX;

    for (int i = 0; i < n; i++) {
        if (meets(&box[i], cornerX, cornerX + natW, cornerY, cornerY + bandH)) {
            clear = 0;
        }
    }
    *x0 = cornerX;
    *y0 = cornerY;
    *x1 = limit(box, n, cornerX, natW, cornerY, bandH, 0.0f);
    if (clear) {
        return 1;
    }
    /* a place that holds the text at full size, else one that holds it
       shrunk to its floor */
    if (slide(box, n, &x, natW, rowY, bandH, right)) {
        *x0 = x;
        *y0 = rowY;
        *x1 = limit(box, n, x, natW, rowY, bandH, right);
        return 2;
    }
    x = cornerX;
    if (slide(box, n, &x, minW, rowY, bandH, right)) {
        *x0 = x;
        *y0 = rowY;
        *x1 = limit(box, n, x, minW, rowY, bandH, right);
        return 2;
    }
    *x1 = 0.0f;
    return 1; /* no room anywhere on the row: the corner, drawn over */
}

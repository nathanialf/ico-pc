/*
 * port/ui/ui_mouse_geom.c
 *
 * The mouse pointer's arithmetic in the menus (ui_mouse_geom.h).
 */
#include "ui_mouse_geom.h"

int ui_mouse_to_grid(const UiMouseView *v, float px, float py, float *gx, float *gy)
{
    if (v == 0 || v->w <= 0.0f || v->h <= 0.0f) {
        return 0;
    }
    const float wide = v->h * 4.0f / 3.0f;
    const float w = v->w < wide ? v->w : wide;
    const float left = v->x + (v->w - w) * 0.5f;
    if (gx) {
        *gx = (px - left) * 640.0f / w;
    }
    if (gy) {
        *gy = 2.0f + (py - v->y) * 448.0f / v->h;
    }
    return 1;
}

int ui_mouse_row_box(const UiMouseRow *r, float box[4])
{
    const int w = r->dispW != 0 ? r->dispW : r->texW;
    const int h = r->dispH != 0 ? r->dispH : r->texH;
    if (w <= 0 || h <= 0) {
        return 0;
    }
    const float x0 = r->centerX != 0 ? 320.0f - (float)w * 0.5f : (float)r->dispX;
    const float y0 = 2.0f * (float)r->dispY;
    const float pad =
        r->role == UI_MOUSE_ROLE_LEFT || r->role == UI_MOUSE_ROLE_RIGHT ? UI_MOUSE_ARROW_PAD : 0.0f;
    box[0] = x0 - pad;
    box[1] = y0 - pad;
    box[2] = x0 + (float)w + pad;
    box[3] = y0 + (float)h + pad;
    return 1;
}

/* whether a row can be hit, and what it hits */
static int target(const UiMouseRow *r, UiMouseHit *out)
{
    if (r->masked || !r->visible) {
        return 0;
    }
    switch (r->role) {
    case UI_MOUSE_ROLE_NONE:
        return 0;
    case UI_MOUSE_ROLE_LEFT:
    case UI_MOUSE_ROLE_RIGHT:
    case UI_MOUSE_ROLE_STEP:
        if (r->ownerItem < 0) {
            return 0;
        }
        out->item = r->ownerItem;
        out->action = r->role == UI_MOUSE_ROLE_LEFT ? UI_MOUSE_ACT_LEFT : UI_MOUSE_ACT_RIGHT;
        break;
    default:
        if (!r->itemLinks && !r->reachable && r->ownerItem < 0) {
            return 0; /* a heading, a note, a picture: no item */
        }
        /* a row lit with its owner (a value, a list's column) is the owner;
           a row with item links of its own is itself even when lit with
           another (the New Game screen's chosen value in the other row) */
        out->item = r->ownerItem >= 0 && !r->itemLinks ? r->ownerItem : r->index;
        out->action = UI_MOUSE_ACT_CROSS;
        break;
    }
    out->row = r->index;
    out->layout = r->layout;
    return 1;
}

int ui_mouse_hit_test(const UiMouseRow *rows, int n, float gx, float gy, UiMouseHit *out)
{
    int found = 0;
    float bestArea = 0.0f, bestDist = 0.0f;
    UiMouseHit best = {-1, -1, -1, UI_MOUSE_ACT_NONE};

    for (int i = 0; i < n; i++) {
        float b[4];
        UiMouseHit h;
        if (!ui_mouse_row_box(&rows[i], b) || gx < b[0] || gx >= b[2] || gy < b[1] || gy >= b[3]) {
            continue;
        }
        if (!target(&rows[i], &h)) {
            continue;
        }
        const float area = (b[2] - b[0]) * (b[3] - b[1]);
        const float mid = (b[1] + b[3]) * 0.5f;
        const float dist = gy > mid ? gy - mid : mid - gy;
        if (!found || area < bestArea || (area == bestArea && dist < bestDist)) {
            found = 1;
            bestArea = area;
            bestDist = dist;
            best = h;
        }
    }
    if (found && out) {
        *out = best;
    }
    return found;
}

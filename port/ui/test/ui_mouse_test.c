/*
 * port/ui/test/ui_mouse_test.c
 *
 * The mouse pointer's arithmetic in the menus (ui_mouse_geom.h) on the
 * CPU: the pointer into the layouts' grid for a 4:3 picture
 * and a pillarboxed 1920 x 1080 one, the rows' boxes (display_texture's
 * fallbacks, the label box lt_ext_draw_row draws in), and the hit test (a
 * label is Cross, a value Right on its owner, the arrows Left and Right with
 * their padding, headings, masked and hidden rows never hit, the smallest
 * box first).
 */
#include <stdio.h>
#include <string.h>

#include "font.h" /* UI_GRID_CX, UI_GRID_CY: the overlay's grid */
#include "ui_mouse_geom.h"

static int failures;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static int near_(float a, float b)
{
    const float d = a - b;
    return d < 0.01f && d > -0.01f;
}

static void gridAt(const UiMouseView *v, float px, float py, float wantX, float wantY)
{
    float gx = -1.0f, gy = -1.0f;
    const int ok = ui_mouse_to_grid(v, px, py, &gx, &gy);
    CHECK(ok && near_(gx, wantX) && near_(gy, wantY),
          "(%g, %g) in %gx%g at %g,%g: (%g, %g), want (%g, %g)", (double)px, (double)py,
          (double)v->w, (double)v->h, (double)v->x, (double)v->y, (double)gx, (double)gy,
          (double)wantX, (double)wantY);
}

static void testGrid(void)
{
    /* a 4:3 window: the corners are the grid's, 448 lines from 2 */
    const UiMouseView w43 = {0.0f, 0.0f, 960.0f, 720.0f};
    gridAt(&w43, 0.0f, 0.0f, 0.0f, 2.0f);
    gridAt(&w43, 960.0f, 720.0f, 640.0f, 450.0f);
    gridAt(&w43, 480.0f, 360.0f, UI_GRID_CX, UI_GRID_CY);
    /* 1920 x 1080, the picture pillarboxed: the box the whole output or the
       4:3 part, the same place either way */
    const UiMouseView wide = {0.0f, 0.0f, 1920.0f, 1080.0f};
    const UiMouseView pillar = {240.0f, 0.0f, 1440.0f, 1080.0f};
    gridAt(&wide, 240.0f, 0.0f, 0.0f, 2.0f);
    gridAt(&wide, 1680.0f, 1080.0f, 640.0f, 450.0f);
    gridAt(&pillar, 240.0f, 0.0f, 0.0f, 2.0f);
    gridAt(&pillar, 1680.0f, 1080.0f, 640.0f, 450.0f);
    /* the bars are outside the grid */
    float gx = 0.0f, gy = 0.0f;
    ui_mouse_to_grid(&wide, 100.0f, 540.0f, &gx, &gy);
    CHECK(gx < 0.0f, "the left bar is left of the grid (%g)", (double)gx);
    /* the inverse of font.h's overlay map: x' = left + gx * W / 640, y' =
       box.y + (gy - 2) * box.h / 448 */
    const UiMouseView off = {100.0f, 50.0f, 1600.0f, 900.0f};
    const float W = 900.0f * 4.0f / 3.0f, left = 100.0f + (1600.0f - W) * 0.5f;
    gridAt(&off, left + 123.0f * W / 640.0f, 50.0f + (300.0f - 2.0f) * 900.0f / 448.0f, 123.0f,
           300.0f);
    const UiMouseView none = {0.0f, 0.0f, 0.0f, 0.0f};
    CHECK(!ui_mouse_to_grid(&none, 1.0f, 1.0f, &gx, &gy), "an empty view maps nothing");
}

static UiMouseRow row(int index, int x, int y, int w, int h)
{
    UiMouseRow r;
    memset(&r, 0, sizeof(r));
    r.index = index;
    r.layout = 80;
    r.dispX = x;
    r.dispY = y;
    r.dispW = w;
    r.dispH = h;
    r.visible = 1;
    r.ownerItem = -1;
    r.itemLinks = 1;
    return r;
}

static void testBoxes(void)
{
    float b[4];
    /* dispX, 2 * dispY, dispW, dispH */
    UiMouseRow r = row(500, 44, 40, 300, 36);
    CHECK(ui_mouse_row_box(&r, b) && b[0] == 44.0f && b[1] == 80.0f && b[2] == 344.0f &&
              b[3] == 116.0f,
          "a label's box (%g %g %g %g)", (double)b[0], (double)b[1], (double)b[2], (double)b[3]);
    /* centred across the screen */
    r.centerX = 1;
    r.dispW = 200;
    CHECK(ui_mouse_row_box(&r, b) && b[0] == 220.0f && b[2] == 420.0f, "centerX: 320 - w / 2 (%g)",
          (double)b[0]);
    /* no display size: the texture's */
    r = row(501, 10, 100, 0, 0);
    r.texW = 128;
    r.texH = 20;
    CHECK(ui_mouse_row_box(&r, b) && b[2] - b[0] == 128.0f && b[3] - b[1] == 20.0f &&
              b[1] == 200.0f,
          "texture size fallback (%g x %g)", (double)(b[2] - b[0]), (double)(b[3] - b[1]));
    r.texW = 0;
    CHECK(!ui_mouse_row_box(&r, b), "no size: no box");
    /* the y mapping pinned against lt_ext_draw_row: display_texture hands it
       box.y = (dispY - 113) * 16 + 4 and box.h = dispH * 8 - 16 (1/16
       field line, inset), which it lays out at by = box.y / 8 + 226 and bh
       = box.h / 8; the label's middle line must be the box's middle here,
       within the inset */
    for (int dy = 0; dy <= 220; dy += 13) {
        const int dh = 36;
        const int bx[2] = {(dy - 113) * 16 + 4, dh * 8 - 16};
        const float by = (float)bx[0] / 8.0f + UI_GRID_CY;
        const float bh = (float)bx[1] / 8.0f;
        UiMouseRow q = row(502, 44, dy, 300, dh);
        ui_mouse_row_box(&q, b);
        const float mid = (b[1] + b[3]) * 0.5f;
        const float off = by + bh * 0.5f - mid;
        /* the inset moves the drawn box's middle half a unit up */
        CHECK(off <= 0.0f && off >= -1.0f, "dispY %d: the label's middle %g, the box's %g", dy,
              (double)(by + bh * 0.5f), (double)mid);
        CHECK(by >= b[1] && by + bh <= b[3], "dispY %d: the drawn box inside the hit box", dy);
    }
}

static void testHits(void)
{
    UiMouseHit h;
    UiMouseRow rows[8];
    int n = 0;
    /* a settings row: the label (an item), its value (STEP) and arrows,
       owned by the label; a heading; a second label below */
    rows[n++] = row(600, 44, 40, 300, 36);
    UiMouseRow v = row(601, 378, 40, 176, 36);
    v.itemLinks = 0;
    v.ownerItem = 600;
    v.role = UI_MOUSE_ROLE_STEP;
    rows[n++] = v;
    UiMouseRow al = row(602, 362, 40, 14, 36);
    al.itemLinks = 0;
    al.ownerItem = 600;
    al.role = UI_MOUSE_ROLE_LEFT;
    rows[n++] = al;
    UiMouseRow ar = row(603, 556, 40, 14, 36);
    ar.itemLinks = 0;
    ar.ownerItem = 600;
    ar.role = UI_MOUSE_ROLE_RIGHT;
    rows[n++] = ar;
    UiMouseRow head = row(604, 20, 12, 600, 40);
    head.itemLinks = 0;
    rows[n++] = head;
    rows[n++] = row(605, 44, 54, 300, 36); /* 14 lines below: the boxes overlap */

    CHECK(ui_mouse_hit_test(rows, n, 200.0f, 98.0f, &h) && h.row == 600 && h.item == 600 &&
              h.action == UI_MOUSE_ACT_CROSS && h.layout == 80,
          "the label: Cross on itself (%d %d %d)", h.row, h.item, h.action);
    CHECK(ui_mouse_hit_test(rows, n, 466.0f, 98.0f, &h) && h.row == 601 && h.item == 600 &&
              h.action == UI_MOUSE_ACT_RIGHT,
          "the value: Right on its owner (%d %d %d)", h.row, h.item, h.action);
    CHECK(ui_mouse_hit_test(rows, n, 369.0f, 98.0f, &h) && h.row == 602 && h.item == 600 &&
              h.action == UI_MOUSE_ACT_LEFT,
          "the left arrow: Left (%d %d)", h.row, h.action);
    CHECK(ui_mouse_hit_test(rows, n, 563.0f, 98.0f, &h) && h.row == 603 &&
              h.action == UI_MOUSE_ACT_RIGHT,
          "the right arrow: Right (%d %d)", h.row, h.action);
    /* the padding: 8 left of the left arrow, over the value's left edge,
       and 8 right of the right arrow; 9 is outside */
    CHECK(ui_mouse_hit_test(rows, n, 355.0f, 98.0f, &h) && h.row == 602, "left arrow, padded (%d)",
          h.row);
    CHECK(ui_mouse_hit_test(rows, n, 380.0f, 98.0f, &h) && h.row == 602,
          "the arrow's padding over the value: the smaller box (%d)", h.row);
    CHECK(ui_mouse_hit_test(rows, n, 577.0f, 98.0f, &h) && h.row == 603, "right arrow, padded (%d)",
          h.row);
    CHECK(!ui_mouse_hit_test(rows, n, 579.0f, 98.0f, &h), "past the padding: nothing");
    CHECK(ui_mouse_hit_test(rows, n, 369.0f, 73.0f, &h) && h.row == 602,
          "the arrow's padding above (%d)", h.row);
    /* the heading is no item; nothing else there */
    CHECK(!ui_mouse_hit_test(rows, n, 320.0f, 30.0f, &h), "the heading is not hit");
    rows[4].itemLinks = 1;
    rows[4].role = UI_MOUSE_ROLE_NONE;
    CHECK(!ui_mouse_hit_test(rows, n, 320.0f, 30.0f, &h), "role NONE is not hit");
    rows[4].itemLinks = 0;
    rows[4].role = UI_MOUSE_ROLE_AUTO;
    rows[4].reachable = 1;
    CHECK(ui_mouse_hit_test(rows, n, 320.0f, 30.0f, &h) && h.row == 604 && h.item == 604,
          "a row the pad reaches is an item (%d)", h.row);
    rows[4].reachable = 0;
    /* masked or hidden rows are not hit */
    rows[0].masked = 1;
    CHECK(!ui_mouse_hit_test(rows, n, 200.0f, 90.0f, &h), "a masked label is not hit");
    rows[0].masked = 0;
    rows[0].visible = 0;
    CHECK(!ui_mouse_hit_test(rows, n, 200.0f, 90.0f, &h), "a hidden label is not hit");
    rows[0].visible = 1;
    /* two labels overlapping (y 80..116 and 108..144): the nearer middle */
    CHECK(ui_mouse_hit_test(rows, n, 200.0f, 110.0f, &h) && h.row == 600,
          "the overlap, nearer the first (%d)", h.row);
    CHECK(ui_mouse_hit_test(rows, n, 200.0f, 114.0f, &h) && h.row == 605,
          "the overlap, nearer the second (%d)", h.row);
    /* the smallest box wins over a bigger one around it */
    UiMouseRow big = row(606, 0, 0, 640, 226);
    UiMouseRow small = row(607, 300, 100, 40, 20);
    UiMouseRow two[2] = {big, small};
    CHECK(ui_mouse_hit_test(two, 2, 320.0f, 210.0f, &h) && h.row == 607, "smallest wins (%d)",
          h.row);
    CHECK(ui_mouse_hit_test(two, 2, 100.0f, 210.0f, &h) && h.row == 606, "outside it, the big one");
    /* a row with item links of its own is itself even when lit with an
       owner (the New Game screen's choices); without, the owner */
    UiMouseRow lit = row(608, 100, 50, 100, 30);
    lit.ownerItem = 609;
    CHECK(ui_mouse_hit_test(&lit, 1, 150.0f, 110.0f, &h) && h.item == 608, "own links: itself");
    lit.itemLinks = 0;
    CHECK(ui_mouse_hit_test(&lit, 1, 150.0f, 110.0f, &h) && h.item == 609 &&
              h.action == UI_MOUSE_ACT_CROSS,
          "a list's column: Cross on its label (%d)", h.item);
    /* an arrow without an owner is no item */
    al.ownerItem = -1;
    CHECK(!ui_mouse_hit_test(&al, 1, 369.0f, 98.0f, &h), "an ownerless arrow is not hit");
}

int main(void)
{
    testGrid();
    testBoxes();
    testHits();
    if (failures) {
        printf("ui_mouse_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("ui_mouse_test: ok\n");
    return 0;
}

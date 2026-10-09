/*
 * port/ui/ui_mouse_geom.h
 *
 * The mouse pointer's arithmetic in the menus (issue 17):
 * where a pointer on the output falls in the layouts' grid, the box a
 * layout row covers there, and which row a point hits.  Plain C with no
 * game or renderer headers, so ui_mouse_test runs it alone; the glue
 * (ui_mouse.c) fills the rows from the game's tables.
 *
 * The grid is the layouts' 640 x 448 picture (font.h): x 0..640 across the
 * 4:3 picture, y 2..450 down it (226 field lines, two y units a line).
 * ui_mouse_to_grid is the inverse of the overlay's mapping in font.h
 * (ui_begin_overlay): W = min(box.w, box.h * 4 / 3), left = box.x +
 * (box.w - W) / 2, gx = (px - left) * 640 / W, gy = 2 + (py - box.y) * 448
 * / box.h.  Nothing is mirrored (the menus are drawn unmirrored).
 *
 * A row's box is display_texture's (layout_texture.c): w = dispW, or texW
 * when 0; h = dispH, or texH when 0 (y units); x0 = 320 - w / 2 when
 * centerX, else dispX; y0 = 2 * dispY (dispY in field lines), the same box
 * lt_ext_draw_row lays a port row's label in (layout_ext.c: by = box.y / 8
 * + 226, the 1/16-field-line box display_texture hands it).  A value's
 * arrows are small, so their boxes grow by UI_MOUSE_ARROW_PAD on every
 * side.
 */
#ifndef PORT_UI_UI_MOUSE_GEOM_H
#define PORT_UI_UI_MOUSE_GEOM_H

#ifdef __cplusplus
extern "C" {
#endif

#define UI_MOUSE_ARROW_PAD 8.0f

/* the same values as layout_ext.h's LT_POINTER_* */
enum {
    UI_MOUSE_ROLE_AUTO = 0,
    UI_MOUSE_ROLE_NONE,
    UI_MOUSE_ROLE_LEFT,
    UI_MOUSE_ROLE_RIGHT,
    UI_MOUSE_ROLE_STEP
};

/* what a click does: the pad button it adds */
enum { UI_MOUSE_ACT_NONE = 0, UI_MOUSE_ACT_CROSS, UI_MOUSE_ACT_LEFT, UI_MOUSE_ACT_RIGHT };

/* where the picture is on the output (the presenter's box, output pixels) */
typedef struct UiMouseView {
    float x, y, w, h;
} UiMouseView;

/* one candidate row, as the glue reads it from the tables */
typedef struct UiMouseRow {
    int index;  /* the texProperty row */
    int layout; /* the layout whose cursor a hit moves */
    int dispX, dispY, dispW, dispH, texW, texH, centerX;
    int masked;    /* not drawn this tick */
    int visible;   /* lt_property_visible */
    int role;      /* UI_MOUSE_ROLE_* */
    int itemLinks; /* any of upItem, downItem, leftItem, rightItem >= 0 */
    int reachable; /* the pad can put the cursor on it: its layout's current
                      or default item, or another row's item link names it */
    int ownerItem; /* -1 for none */
} UiMouseRow;

typedef struct UiMouseHit {
    int row;    /* the row under the point */
    int item;   /* the item the cursor goes to (the row, or its owner) */
    int layout; /* whose cursor */
    int action; /* UI_MOUSE_ACT_* for a click */
} UiMouseHit;

/* (px, py) on the output into the grid; 0 (gx, gy untouched) when the view
   is empty */
int ui_mouse_to_grid(const UiMouseView *v, float px, float py, float *gx, float *gy);
/* the row's box in the grid, x0 y0 x1 y1, the arrows' padding included;
   0 when it has no size */
int ui_mouse_row_box(const UiMouseRow *r, float box[4]);
/* the row hit at (gx, gy): masked, hidden, role NONE and rows that are no
   item (no item links, not reachable, no owner; an arrow or a value needs
   its owner) are skipped; the smallest box holding the point wins, then the one
   whose middle line is nearest (the menus' boxes overlap a little).
   Returns 1 with *out filled, 0 when no row is hit. */
int ui_mouse_hit_test(const UiMouseRow *rows, int n, float gx, float gy, UiMouseHit *out);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_UI_MOUSE_GEOM_H */

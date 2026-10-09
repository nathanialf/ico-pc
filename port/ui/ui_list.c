/*
 * port/ui/ui_list.c
 *
 * The scrolling list pages (ui_list.h): building the slots, filling them
 * from the page's items, the heading skip and the scrolling.
 */
#include "ui_list.h"

#include <string.h>

#include "font.h"

/* layout_action.c: the menus' sound */
extern void CUR_SE(void);

/* Triangle or Circle, as settings.c's PAD_BACK */
#define PAD_BACK (LT_PAD_TRIANGLE | LT_PAD_CIRCLE)

static LtProperty *P(int index)
{
    return lt_ext_prop(index);
}

void ui_list_build(UiList *l, const UiListDef *def, void *user, const UiListStyle *st)
{
    int second = st->colB.w > 0;
    memset(l, 0, sizeof(*l));
    l->def = def;
    l->user = user;
    l->lastDir = 1;
    for (int i = 0; i < UI_LIST_SLOTS; i++) {
        int y = st->y0 + st->pitch * i;
        l->label[i] = ui_settings_add_row(st->label.x, y, st->label.w, 36, 1, -1, 0, " ",
                                          st->label.size, st->label.align);
        l->colA[i] = ui_settings_add_row(st->colA.x, y, st->colA.w, 36, 1, l->label[i], 0, " ",
                                         st->colA.size, st->colA.align);
        l->colB[i] = second ? ui_settings_add_row(st->colB.x, y, st->colB.w, 36, 1, l->label[i], 0,
                                                  " ", st->colB.size, st->colB.align)
                            : -1;
    }
    for (int i = 0; i < UI_LIST_SLOTS; i++) {
        P(l->label[i])->downItem = i + 1 < UI_LIST_SLOTS ? l->label[i + 1] : -1;
        P(l->label[i])->upItem = i > 0 ? l->label[i - 1] : -1;
    }
    l->status =
        ui_settings_add_row(20, st->statusY, 600, 30, 0, -1, 0, " ", 19.0f, UI_ALIGN_CENTER);
    P(l->status)->centerX = 1;
}

void ui_list_reset(UiList *l)
{
    l->offset = 0;
    l->lastDir = 1;
}

int ui_list_count(const UiList *l)
{
    int n = l->def->count(l->user);
    return n > 0 ? n : 0;
}

int ui_list_shown(const UiList *l)
{
    int n = ui_list_count(l);
    return n < UI_LIST_SLOTS ? n : UI_LIST_SLOTS;
}

int ui_list_slot_of(const UiList *l, int row)
{
    for (int i = 0; i < UI_LIST_SLOTS; i++) {
        if (l->label[i] == row) {
            return i;
        }
    }
    return -1;
}

int ui_list_item_at(const UiList *l, int slot)
{
    int d = l->offset + slot;
    return slot >= 0 && slot < UI_LIST_SLOTS && d < ui_list_count(l) ? d : -1;
}

int ui_list_item_of_row(const UiList *l, int row)
{
    int s = ui_list_slot_of(l, row);
    return s >= 0 ? l->offset + s : -1;
}

static int isHeading(const UiList *l, int item, int n)
{
    return l->def->heading != NULL && item >= 0 && item < n && l->def->heading(l->user, item);
}

static void setCell(int row, const char *text, int strId)
{
    if (row < 0) {
        return;
    }
    if (text != NULL) {
        lt_ext_set_text(row, text);
    } else if (strId != 0) {
        lt_ext_set_str(row, strId);
    } else {
        lt_ext_set_text(row, "");
    }
}

void ui_list_refresh(UiList *l, int cursorRow)
{
    int n = ui_list_count(l);
    for (int s = 0; s < UI_LIST_SLOTS; s++) {
        int d = l->offset + s;
        UiListSlot out;
        memset(&out, 0, sizeof(out));
        if (d < n) {
            l->def->fill(l->user, d, &out);
        }
        setCell(l->label[s], out.label, out.labelStr);
        setCell(l->colA[s], out.colA, out.colAStr);
        setCell(l->colB[s], out.colB, out.colBStr);
        /* the mouse pointer never picks a heading or an empty
           slot (the cursor would only skip on from it) */
        const int role = d >= n || isHeading(l, d, n) ? LT_POINTER_NONE : LT_POINTER_AUTO;
        lt_ext_set_pointer_role(l->label[s], role);
        lt_ext_set_pointer_role(l->colA[s], role);
        if (l->colB[s] >= 0) {
            lt_ext_set_pointer_role(l->colB[s], role);
        }
    }
    if (l->def->decorate) {
        l->def->decorate(l->user, ui_list_item_of_row(l, cursorRow));
    }
}

/* A cursor on a heading goes on in the direction it came (wrapping at the
   ends), scrolling so the item shows. */
static void skipHeading(UiList *l, LtProp *lay)
{
    int n = ui_list_count(l), shown = ui_list_shown(l);
    int s = ui_list_slot_of(l, lay->curItem);
    if (l->def->heading == NULL || s < 0 || s >= shown) {
        return;
    }
    int d = l->offset + s;
    if (!isHeading(l, d, n)) {
        return;
    }
    for (int guard = 0; guard < n && isHeading(l, d, n); guard++) {
        d = (d + l->lastDir + n) % n;
    }
    if (isHeading(l, d, n)) {
        return; /* nothing to select */
    }
    if (d < l->offset) {
        l->offset = d < shown ? 0 : d;
    } else if (d >= l->offset + shown) {
        l->offset = d >= n - shown ? n - shown : d - shown + 1;
    }
    lay->curItem = l->label[d - l->offset];
}

/* The cursor moves on the slots through their item links; at the first or
   last slot the list scrolls (wrapping at the ends, as the Options screen's
   rows do). */
static void scrollList(UiList *l, LtProp *lay, int flags)
{
    int n = ui_list_count(l);
    int s = ui_list_slot_of(l, lay->curItem);
    int shown = ui_list_shown(l);
    int wrapped = 0;
    if (s < 0 || shown == 0 || (flags & (LT_PAD_CROSS | PAD_BACK))) {
        return;
    }
    if ((flags & LT_PAD_DOWN) && s == shown - 1) {
        if (l->offset + shown < n) {
            l->offset++;
        } else {
            l->offset = 0;
            lay->curItem = l->label[0];
            wrapped = 1;
        }
        CUR_SE();
    } else if ((flags & LT_PAD_UP) && s == 0) {
        if (l->offset > 0) {
            l->offset--;
        } else {
            l->offset = n - shown;
            lay->curItem = l->label[shown - 1];
            wrapped = 1;
        }
        CUR_SE();
    }
    if (wrapped) {
        /* the layout's own move (default_item_select) runs after this proc
           with the same pad flags: it would follow the new row's link one
           row further (skipping the end row) and play the cursor sound a
           second time.  The wrap is this frame's move. */
        lt_item_select_disable = 1;
    }
}

int ui_list_proc(UiList *l, LtProp *lay, int flags)
{
    skipHeading(l, lay);
    if (flags & LT_PAD_DOWN) {
        l->lastDir = 1;
    } else if (flags & LT_PAD_UP) {
        l->lastDir = -1;
    }
    if (l->def->input != NULL) {
        int d = ui_list_item_of_row(l, lay->curItem);
        int r = l->def->input(l->user, d, flags);
        if (r != UI_LIST_PASS) {
            return r;
        }
    }
    scrollList(l, lay, flags);
    return -1;
}

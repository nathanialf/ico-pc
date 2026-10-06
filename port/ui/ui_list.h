/*
 * port/ui/ui_list.h
 *
 * The scrolling list pages of the port's menus:
 * a window of UI_LIST_SLOTS rows over a list of N items.  The Settings
 * achievements and remap pages use it, and the Extras galleries will.
 *
 * A page owns a UiList and describes its data in a UiListDef:
 *   count    how many items there are (read every tick, so it may change)
 *   fill     fills slot data (label, two columns) from item k
 *   heading  (optional) whether item k is a heading: shown, never selected
 *   input    (optional) what a press does on item k (-1 when the cursor is on
 *            no item); returns UI_LIST_PASS to let the list scroll, else the
 *            proc's result (-1 or a layout to switch to)
 *   decorate (optional) the page's own rows (a header, the status line), set
 *            after the slots are filled, with the item under the cursor
 *
 * Navigation: the cursor moves on the slots through their item links; at
 * the first or last slot the list scrolls, wrapping at the ends, as the
 * Options screen's rows do.  A cursor that lands on a heading moves on in the
 * direction it came from (one frame later, in ui_ListProc).
 *
 * The rows are port rows of the layout extension (layout_ext.h), added with
 * ui_SettingsAddRow (settings.c); the page's layout proc calls ui_ListRefresh
 * and ui_ListProc.
 */
#ifndef PORT_UI_UI_LIST_H
#define PORT_UI_UI_LIST_H

#include "layout_ext.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_LIST_SLOTS 8
/* what UiListDef.input returns for "not handled: scroll as usual" */
#define UI_LIST_PASS (-2)

/* What fill() sets for one slot.  A text of NULL with a string id of 0 is an
   empty cell; a string id (strings.h) follows the language. */
typedef struct UiListSlot {
    const char *label;
    int labelStr;
    const char *colA;
    int colAStr;
    const char *colB; /* ignored on a list built without a second column */
    int colBStr;
} UiListSlot;

typedef struct UiListDef {
    int (*count)(void *user);
    void (*fill)(void *user, int item, UiListSlot *out);
    int (*heading)(void *user, int item);          /* may be NULL */
    int (*input)(void *user, int item, int flags); /* may be NULL */
    void (*decorate)(void *user, int cursorItem);  /* may be NULL */
} UiListDef;

/* One text column or the label: a row's box, em size and alignment. */
typedef struct UiListCol {
    int x, w;
    float size;
    int align; /* UI_ALIGN_* */
} UiListCol;

typedef struct UiListStyle {
    int y0;    /* the first slot's y (field lines) */
    int pitch; /* one slot to the next */
    UiListCol label, colA;
    UiListCol colB; /* w 0: no second column */
    int statusY;    /* the status line's y */
} UiListStyle;

typedef struct UiList {
    const UiListDef *def;
    void *user;
    int label[UI_LIST_SLOTS];
    int colA[UI_LIST_SLOTS];
    int colB[UI_LIST_SLOTS]; /* -1 without a second column */
    int status;              /* the status line's row */
    int offset;              /* the item shown in slot 0 */
    int lastDir;             /* +1 / -1: the last vertical press */
} UiList;

/* Provided by settings.c: appends a port row (the Settings menu's row
   defaults: unique texel rectangle, no links); returns its property index. */
int ui_SettingsAddRow(int x, int y, int w, int h, int selectable, int owner, int strId,
                      const char *text, float size, int align);

/* Adds the slots' rows (label, column A and B per slot, the item links
   between slots) and the status line, in that order. */
void ui_ListBuild(UiList *l, const UiListDef *def, void *user, const UiListStyle *style);
/* The list as the page was entered: the first item at the top. */
void ui_ListReset(UiList *l);
/* Items now, and slots in use (the smaller of that and UI_LIST_SLOTS). */
int ui_ListCount(const UiList *l);
int ui_ListShown(const UiList *l);
/* The slot whose label row is `row`, -1 for none; the item in a slot (-1
   when empty); the item under the cursor row (-1 for none). */
int ui_ListSlotOf(const UiList *l, int row);
int ui_ListItemAt(const UiList *l, int slot);
int ui_ListItemOfRow(const UiList *l, int row);
/* Fills every slot from its item (empty past the end) and calls decorate. */
void ui_ListRefresh(UiList *l, int cursorRow);
/* One tick of input with the layout's cursor (lay->curItem): the heading
   skip, def->input, then the scrolling.  The proc's result. */
int ui_ListProc(UiList *l, LtProp *lay, int flags);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_UI_LIST_H */

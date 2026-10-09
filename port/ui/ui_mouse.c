/*
 * port/ui/ui_mouse.c
 *
 * The mouse pointer in the menus (ui_mouse.h): the game's
 * layouts read through the layout extension's lookups, the pointer's tick
 * (port/input/pointer.h), the hit test (ui_mouse_geom.h).
 */
#include "ui_mouse.h"

#include <string.h>

#include "layout_ext.h"
#include "pointer.h"
#include "settings.h"
#include "ui_mouse_geom.h"

_Static_assert((int)UI_MOUSE_ROLE_AUTO == (int)LT_POINTER_AUTO &&
                   (int)UI_MOUSE_ROLE_NONE == (int)LT_POINTER_NONE &&
                   (int)UI_MOUSE_ROLE_LEFT == (int)LT_POINTER_LEFT &&
                   (int)UI_MOUSE_ROLE_RIGHT == (int)LT_POINTER_RIGHT &&
                   (int)UI_MOUSE_ROLE_STEP == (int)LT_POINTER_STEP,
               "ui_mouse_geom.h's roles are layout_ext.h's");

/* the game's side (common/; layout_texture.h declares the lt_* calls) */
extern PadState pad[16];

/* the pad's trigger bits (keyInput.c's logical word), as settings.c has them */
#define PAD_CROSS 0x0040
#define PAD_UP 0x1000
#define PAD_RIGHT 0x2000
#define PAD_DOWN 0x4000
#define PAD_LEFT 0x8000

/* play and its scenes (layout_texture.c init_layout_texture; mouse_look.h) */
#define LAYOUT_PLAY 54
#define LAYOUT_SCENE 55

#define CHAIN_MAX 16
#define ROWS_MAX (LT_EXT_MAX_PROPERTIES + LT_GAME_PROPERTY_COUNT)
#define WHEEL_QUEUE 3

static struct {
    int haveView;
    float outW, outH;
    UiMouseView view;
    int active; /* a menu is up (ico_pointer_menu) */
    int hidden; /* a key or pad press since the mouse was last used */
    int quiet;  /* ticks without the mouse, up to 2 */
    int wheel;  /* notches waiting, up positive, at most WHEEL_QUEUE */
    int wheelWait;
} s_m;

static UiMouseRow s_rows[ROWS_MAX];
/* the rows an item link of a candidate names (gatherRows) */
static unsigned char s_named[ROWS_MAX];

void ui_MouseSetView(int outW, int outH, int boxX, int boxY, int boxW, int boxH)
{
    if (outW <= 0 || outH <= 0 || boxW <= 0 || boxH <= 0) {
        s_m.haveView = 0;
        return;
    }
    s_m.haveView = 1;
    s_m.outW = (float)outW;
    s_m.outH = (float)outH;
    s_m.view.x = (float)boxX;
    s_m.view.y = (float)boxY;
    s_m.view.w = (float)boxW;
    s_m.view.h = (float)boxH;
}

void ui_MouseReset(void)
{
    memset(&s_m, 0, sizeof(s_m));
    ico_pointer_reset();
}

int ui_MouseMenuActive(void)
{
    return s_m.active && !s_m.hidden;
}

/* the current layout and its link chain, as exec_layout_texture walks it */
static int chainOf(int *list)
{
    int n = 0;
    for (int l = current_layout_id; l >= 0 && n < CHAIN_MAX; l = lt_ext_Layout(l)->link) {
        list[n++] = l;
    }
    return n;
}

/* a menu: not play or a scene, and a cursor somewhere in the chain */
static int menuUp(void)
{
    int list[CHAIN_MAX];
    if (current_layout_id == LAYOUT_PLAY || current_layout_id == LAYOUT_SCENE) {
        return 0;
    }
    const int n = chainOf(list);
    for (int i = 0; i < n; i++) {
        if (lt_ext_Layout(list[i])->curItem >= 0) {
            return 1;
        }
    }
    return 0;
}

/* the rows of the chain, with the layout whose cursor each one moves: a
   port row in a chained layout with no cursor of its own goes with the
   current layout's (display_texture's rule, layout_texture.c), any other
   row with its own layout's; a row whose cursor layout has no cursor is
   left out */
static int gatherRows(void)
{
    int list[CHAIN_MAX];
    int n = 0;
    const int chain = chainOf(list);
    const int curHas = lt_ext_Layout(current_layout_id)->curItem >= 0;
    for (int c = 0; c < chain; c++) {
        const LtProp *lay = lt_ext_Layout(list[c]);
        for (int j = lay->first; j < lay->last && n < ROWS_MAX; j++) {
            const LtProperty *e = lt_ext_Prop(j);
            int owner = list[c];
            if (lay->curItem < 0) {
                if (!lt_ext_IsPortProp(e) || !curHas) {
                    continue;
                }
                owner = current_layout_id;
            }
            UiMouseRow *r = &s_rows[n++];
            r->index = j;
            r->layout = owner;
            r->dispX = e->dispX;
            r->dispY = e->dispY;
            r->dispW = e->dispW;
            r->dispH = e->dispH;
            r->texW = e->texW;
            r->texH = e->texH;
            r->centerX = e->centerX;
            r->masked = e->masked;
            r->visible = lt_host_property_visible(j);
            r->role = lt_ext_PointerRole(j);
            r->itemLinks =
                e->upItem >= 0 || e->downItem >= 0 || e->leftItem >= 0 || e->rightItem >= 0;
            r->ownerItem = e->ownerItem;
        }
    }
    /* reachable: the pad can put the cursor there (a row with layout links
       only, such as a panel that takes Triangle back, is no item unless
       it is) */
    memset(s_named, 0, sizeof(s_named));
    for (int i = 0; i < n; i++) {
        const LtProperty *e = lt_ext_Prop(s_rows[i].index);
        const int names[4] = {e->upItem, e->downItem, e->leftItem, e->rightItem};
        for (int k = 0; k < 4; k++) {
            if (names[k] >= 0 && names[k] < ROWS_MAX) {
                s_named[names[k]] = 1;
            }
        }
    }
    for (int i = 0; i < n; i++) {
        UiMouseRow *r = &s_rows[i];
        const LtProp *cl = lt_ext_Layout(r->layout);
        r->reachable = (r->index < ROWS_MAX && s_named[r->index]) || r->index == cl->curItem ||
                       r->index == cl->defaultItem;
    }
    return n;
}

static int hitAt(float x, float y, UiMouseHit *hit)
{
    float gx, gy;
    if (!s_m.haveView || !ui_MouseToGrid(&s_m.view, x * s_m.outW, y * s_m.outH, &gx, &gy)) {
        return 0;
    }
    return ui_MouseHitTest(s_rows, gatherRows(), gx, gy, hit);
}

void ui_MouseTick(void)
{
    IcoPointerTick t;
    const int used = ico_pointer_take(&t);

    s_m.active = menuUp();
    ico_pointer_set_menu(s_m.active);
    /* a key or a pad button (not one of the mouse's own, which arrive as
       pad buttons in the same or the next tick) hides the pointer until the
       mouse is used again */
    if (used) {
        s_m.hidden = 0;
        s_m.quiet = 0;
    } else {
        if (s_m.quiet < 2) {
            s_m.quiet++;
        }
        if (s_m.quiet >= 2 && pad[0].flags != 0) {
            s_m.hidden = 1;
        }
    }
    s_m.wheel += t.wheel;
    s_m.wheel = s_m.wheel > WHEEL_QUEUE    ? WHEEL_QUEUE
                : s_m.wheel < -WHEEL_QUEUE ? -WHEEL_QUEUE
                                           : s_m.wheel;
    if (s_m.wheelWait > 0) {
        s_m.wheelWait--;
    }
    if (!s_m.active || lt_fade_status() != 2 || lt_host_select_disabled() ||
        ui_SettingsCapturing()) {
        s_m.wheel = 0; /* nothing waits through a fade or into the next screen */
        return;
    }
    if (t.valid && (t.moved || t.clicks > 0)) {
        UiMouseHit hit;
        if (hitAt(t.x, t.y, &hit)) {
            lt_host_point_item(hit.layout, hit.item);
            if (t.clicks > 0) {
                pad[0].flags |= hit.action == UI_MOUSE_ACT_LEFT    ? PAD_LEFT
                                : hit.action == UI_MOUSE_ACT_RIGHT ? PAD_RIGHT
                                                                   : PAD_CROSS;
            }
        }
    }
    /* a notch a move; the move's next tick has its pad cleared
       (exec_layout_texture), so the next notch waits a tick */
    if (s_m.wheel != 0 && s_m.wheelWait == 0) {
        pad[0].flags |= s_m.wheel > 0 ? PAD_UP : PAD_DOWN;
        s_m.wheel += s_m.wheel > 0 ? -1 : 1;
        s_m.wheelWait = 2; /* counted down at the start of the next two ticks */
    }
}

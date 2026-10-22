/*
 * port/input/touch_edit.c
 *
 * The touch overlay's layout edits and the layout editor (touch_edit.h).
 * The effort's contract commit: the names and the defaults; the maths and
 * the state machine follow (package TL-CORE).
 */
#include "touch_edit.h"

#include <string.h>

const float ico_touch_edit_grids[3] = {2.0f, 4.0f, 8.0f};

static const char *const s_ctlNames[ICO_TOUCH_CTL_COUNT] = {
    "dpad", "cross", "circle", "square", "triangle", "r1",  "l1",
    "l2",   "r2",    "select", "start",  "stick",    "look"};
static const char *const s_postureNames[ICO_TOUCH_POSTURES] = {"full", "split", "tabletop"};

void ico_touch_edits_defaults(IcoTouchEdits *e)
{
    memset(e, 0, sizeof(*e));
    e->snap = 1;
    e->grid = ICO_TOUCH_EDIT_GRID_DEFAULT;
}

int ico_touch_ctl_hideable(int ctl)
{
    return ctl == ICO_TOUCH_CTL_SELECT || ctl == ICO_TOUCH_CTL_L1 || ctl == ICO_TOUCH_CTL_L2 ||
           ctl == ICO_TOUCH_CTL_R2 || ctl == ICO_TOUCH_CTL_LOOK;
}

const char *ico_touch_ctl_name(int ctl)
{
    return ctl >= 0 && ctl < ICO_TOUCH_CTL_COUNT ? s_ctlNames[ctl] : NULL;
}

int ico_touch_ctl_from_name(const char *name)
{
    int i;

    for (i = 0; name != NULL && i < ICO_TOUCH_CTL_COUNT; i++) {
        if (strcmp(name, s_ctlNames[i]) == 0) {
            return i;
        }
    }
    return -1;
}

const char *ico_touch_posture_name(int rule)
{
    return rule >= 0 && rule < ICO_TOUCH_POSTURES ? s_postureNames[rule] : NULL;
}

int ico_touch_posture_from_name(const char *name)
{
    int i;

    for (i = 0; name != NULL && i < ICO_TOUCH_POSTURES; i++) {
        if (strcmp(name, s_postureNames[i]) == 0) {
            return i;
        }
    }
    return -1;
}

unsigned int ico_touch_ctl_zones(int ctl)
{
    switch (ctl) {
    case ICO_TOUCH_CTL_DPAD:
        return (1u << ICO_TOUCH_B_UP) | (1u << ICO_TOUCH_B_DOWN) | (1u << ICO_TOUCH_B_LEFT) |
               (1u << ICO_TOUCH_B_RIGHT);
    case ICO_TOUCH_CTL_CROSS:
        return 1u << ICO_TOUCH_B_CROSS;
    case ICO_TOUCH_CTL_CIRCLE:
        return 1u << ICO_TOUCH_B_CIRCLE;
    case ICO_TOUCH_CTL_SQUARE:
        return 1u << ICO_TOUCH_B_SQUARE;
    case ICO_TOUCH_CTL_TRIANGLE:
        return 1u << ICO_TOUCH_B_TRIANGLE;
    case ICO_TOUCH_CTL_R1:
        return 1u << ICO_TOUCH_B_R1;
    case ICO_TOUCH_CTL_L1:
        return 1u << ICO_TOUCH_B_L1;
    case ICO_TOUCH_CTL_L2:
        return 1u << ICO_TOUCH_B_L2;
    case ICO_TOUCH_CTL_R2:
        return 1u << ICO_TOUCH_B_R2;
    case ICO_TOUCH_CTL_SELECT:
        return 1u << ICO_TOUCH_B_SELECT;
    case ICO_TOUCH_CTL_START:
        return 1u << ICO_TOUCH_B_START;
    default:
        return 0;
    }
}

/* --- the parts TL-CORE fills in (the contract commit's placeholders) ---- */

const char *ico_touch_edit_text(const IcoTouchCtlEdit *c, char *buf, unsigned size)
{
    (void)c;
    if (buf != NULL && size > 0) {
        buf[0] = '\0';
    }
    return buf;
}

int ico_touch_edit_parse(IcoTouchCtlEdit *c, int ctl, const char *text)
{
    (void)ctl;
    (void)text;
    memset(c, 0, sizeof(*c));
    return -1;
}

void ico_touch_edit_sanitize(IcoTouchCtlEdit *c, int ctl)
{
    (void)c;
    (void)ctl;
}

void ico_touch_edits_reset(IcoTouchEdits *e, int rule)
{
    if (rule < 0) {
        memset(e->ctl, 0, sizeof(e->ctl));
    } else if (rule < ICO_TOUCH_POSTURES) {
        memset(e->ctl[rule], 0, sizeof(e->ctl[rule]));
    }
    e->gen++;
}

int ico_touch_edits_any(const IcoTouchEdits *e, int rule)
{
    int r, c;

    for (r = 0; r < ICO_TOUCH_POSTURES; r++) {
        if (rule >= 0 && r != rule) {
            continue;
        }
        for (c = 0; c < ICO_TOUCH_CTL_COUNT; c++) {
            if (e->ctl[r][c].set) {
                return 1;
            }
        }
    }
    return 0;
}

void ico_touch_edit_begin(IcoTouchEditor *ed, IcoTouchEdits *live, int rule)
{
    memset(ed, 0, sizeof(*ed));
    ed->live = live;
    ed->work = *live;
    ed->rule = rule;
    ed->selected = -1;
    ed->active = 1;
}

void ico_touch_edit_event(IcoTouchEditor *ed, uint64_t finger, float nx, float ny, int kind,
                          uint64_t nowNs)
{
    IcoTouchEditEvent *ev;

    if (ed->qn >= ICO_TOUCH_ED_QUEUE) {
        return;
    }
    ev = &ed->queue[ed->qn++];
    ev->id = finger;
    ev->nx = nx;
    ev->ny = ny;
    ev->kind = kind;
    ev->ns = nowNs;
}

int ico_touch_edit_tick(IcoTouchEditor *ed, const IcoTouchLayout *l, unsigned int padHeld)
{
    (void)l;
    (void)padHeld;
    ed->qn = 0;
    return ICO_TOUCH_ED_NONE;
}

void ico_touch_edit_end(IcoTouchEditor *ed, int keep)
{
    if (keep && ed->live != NULL) {
        ed->work.gen = ed->live->gen + 1;
        *ed->live = ed->work;
    }
    ed->active = 0;
}

void ico_touch_edit_view(const IcoTouchEditor *ed, const IcoTouchLayout *l, IcoTouchEditView *out)
{
    (void)l;
    memset(out, 0, sizeof(*out));
    out->active = ed->active;
    out->rule = ed->rule;
    out->selected = ed->selected;
}

void ico_touch_edit_toolbar(const IcoTouchLayout *l, IcoTouchRect tb[ICO_TOUCH_ED_TB_COUNT],
                            IcoTouchRect confirm[ICO_TOUCH_ED_CF_COUNT])
{
    (void)l;
    memset(tb, 0, sizeof(IcoTouchRect) * ICO_TOUCH_ED_TB_COUNT);
    memset(confirm, 0, sizeof(IcoTouchRect) * ICO_TOUCH_ED_CF_COUNT);
}

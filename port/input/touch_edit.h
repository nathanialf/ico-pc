/*
 * port/input/touch_edit.h
 *
 * The touch overlay's layout edits and the layout editor (the contract every
 * package of the touch effort codes against; plain C, no SDL, no libm).
 *
 * Edits: for each posture (ICO_TOUCH_RULE_FULL / SPLIT / TABLETOP, touch.h)
 * and each editable control (ICO_TOUCH_CTL_*, touch.h: the D-pad is one
 * control, the four keys as one cross), where its centre is as an anchor
 * edge pair and an offset in q (layout millimetres: the unit the defaults
 * are stored in, touch.c), a size scale, an opacity multiplier, and for the
 * look pad its size in q. An unset control is at its default place. The
 * layout builder (ico_touch_layout_edits) applies them after place(): the
 * rects clamped inside the region and out of the hinge band, hidden
 * controls (alpha 0) skipped by the hit test.
 *
 * The rails: scale ICO_TOUCH_EDIT_SCALE_MIN..MAX; alpha at least
 * ICO_TOUCH_EDIT_ALPHA_MIN for every control that is not hideable
 * (ico_touch_ctl_hideable: Select, L1, L2, R2 and the look pad are; the
 * D-pad, the faces, R1, Start and the stick never, the menus and the game
 * need them); ico_touch_edit_sanitize enforces both on every setter and on
 * every parsed config line.
 *
 * The editor: a state machine fed the output's finger events while the
 * Edit layout page is open (the mapper steps but merges nothing then,
 * input_sdl.c) and a gamepad word once a tick; it edits a working copy of
 * the live edits for the posture the phone is in now and copies it back on
 * Done (ico_touch_edit_end). The drawing reads IcoTouchEditView (touch.h).
 */
#ifndef ICO_PORT_INPUT_TOUCH_EDIT_H
#define ICO_PORT_INPUT_TOUCH_EDIT_H

#include <stdint.h>
#include "touch.h"

enum { ICO_TOUCH_POSTURES = 3 }; /* indexed by ICO_TOUCH_RULE_* */

/* IcoTouchCtlEdit.ax / .ay: which edge of the region the offsets count from */
enum { ICO_TOUCH_ANCHOR_L = 0, ICO_TOUCH_ANCHOR_R = 1 };

enum { ICO_TOUCH_ANCHOR_T = 0, ICO_TOUCH_ANCHOR_B = 1 };

#define ICO_TOUCH_EDIT_SCALE_MIN 0.7f /* a face at 0.7 is 7.7 mm across: Android's 48 dp */
#define ICO_TOUCH_EDIT_SCALE_MAX 1.6f
#define ICO_TOUCH_EDIT_ALPHA_MIN 0.25f /* the floor of a control that cannot be hidden */
#define ICO_TOUCH_EDIT_GRID_DEFAULT 4.0f
#define ICO_TOUCH_EDIT_UNDO 8

/* the grid sizes the Grid size row offers, in q */
extern const float ico_touch_edit_grids[3];

typedef struct IcoTouchCtlEdit {
    unsigned char set;    /* 0: the default place (nothing stored or written) */
    unsigned char ax, ay; /* ICO_TOUCH_ANCHOR_* */
    float dx, dy;         /* the control's centre from those edges, in q (>= 0) */
    float scale;          /* ICO_TOUCH_EDIT_SCALE_MIN..MAX, 1 = the default size */
    float alpha;          /* 0..1 times the Touch opacity setting; 0 = hidden */
    float w, h;           /* ICO_TOUCH_CTL_LOOK only: the pad's size in q (its centre is dx, dy) */
} IcoTouchCtlEdit;

typedef struct IcoTouchEdits {
    IcoTouchCtlEdit ctl[ICO_TOUCH_POSTURES][ICO_TOUCH_CTL_COUNT];
    int snap;         /* the Snap to grid row */
    float grid;       /* one of ico_touch_edit_grids, q */
    unsigned int gen; /* bumped by every change: a layout built from an older
                         generation is rebuilt (IcoTouchLayout.editGen) */
} IcoTouchEdits;

/* Every control unset, snap on, grid ICO_TOUCH_EDIT_GRID_DEFAULT, gen 0. */
void ico_touch_edits_defaults(IcoTouchEdits *e);
/* 1 for Select, L1, L2, R2 and the look pad: the controls a player may hide. */
int ico_touch_ctl_hideable(int ctl);
/* The config names: "dpad", "cross", "circle", "square", "triangle", "r1",
   "l1", "l2", "r2", "select", "start", "stick", "look"; and back (-1 none). */
const char *ico_touch_ctl_name(int ctl);
int ico_touch_ctl_from_name(const char *name);
/* "full", "split", "tabletop" for ICO_TOUCH_RULE_*; NULL out of range. */
const char *ico_touch_posture_name(int rule);
int ico_touch_posture_from_name(const char *name);
/* The button zones (ICO_TOUCH_B_*) a control owns, as a mask of 1 << zone;
   0 for the stick and the look pad. */
unsigned int ico_touch_ctl_zones(int ctl);

/* One control's edit as config text and back. Text: whitespace-separated
   tokens "l<n>"|"r<n>" (the x anchor and offset), "t<n>"|"b<n>", "s<scale>"
   (optional, 1 when absent), "o<percent>" (optional, 100 when absent), and
   for the look pad "w<n> h<n>"; "hidden" alone (a hideable control; the
   text of alpha 0); "default" alone (unset). Examples "r20 b10.5 s1.00
   o100", "l50 t0 w60 h30 o100". ico_touch_edit_text writes the same form
   (numbers to two decimals, trailing zeros kept) into buf and returns it;
   ico_touch_edit_parse fills *c for ctl and returns 0, or -1 for a token it
   does not know, a value out of range, a missing anchor, or "hidden" on a
   control that cannot be hidden: *c is then left unset. */
const char *ico_touch_edit_text(const IcoTouchCtlEdit *c, char *buf, unsigned size);
int ico_touch_edit_parse(IcoTouchCtlEdit *c, int ctl, const char *text);
/* The rails on one edit in place: scale clamped to the range, alpha to 0..1
   and at least ICO_TOUCH_EDIT_ALPHA_MIN unless ctl is hideable, offsets at
   least 0, the look pad's size at least its minimum. */
void ico_touch_edit_sanitize(IcoTouchCtlEdit *c, int ctl);
/* Clear every control of one posture (rule), or of all (rule < 0); snap
   and grid untouched; gen bumped. */
void ico_touch_edits_reset(IcoTouchEdits *e, int rule);
/* 1 when any control of the posture (rule < 0: any posture) is set. */
int ico_touch_edits_any(const IcoTouchEdits *e, int rule);

/* --- the editor ------------------------------------------------------------ */

/* IcoTouchEditor.state */
enum { ICO_TOUCH_ED_IDLE = 0, ICO_TOUCH_ED_DRAG, ICO_TOUCH_ED_PINCH, ICO_TOUCH_ED_CONFIRM_RESET };

/* the toolbar's keys, in drawing order; IcoTouchEditView.tb[] */
enum {
    ICO_TOUCH_ED_TB_DONE = 0,
    ICO_TOUCH_ED_TB_UNDO,
    ICO_TOUCH_ED_TB_RESET,
    ICO_TOUCH_ED_TB_SNAP,
    ICO_TOUCH_ED_TB_GRID,
    ICO_TOUCH_ED_TB_SIZE_DN,
    ICO_TOUCH_ED_TB_SIZE_UP,
    ICO_TOUCH_ED_TB_OP_DN,
    ICO_TOUCH_ED_TB_OP_UP,
    ICO_TOUCH_ED_TB_ALL,
    ICO_TOUCH_ED_TB_COUNT
};

/* the confirm strip's keys (state CONFIRM_RESET); IcoTouchEditView.confirm[] */
enum {
    ICO_TOUCH_ED_CF_THIS = 0,
    ICO_TOUCH_ED_CF_ALL,
    ICO_TOUCH_ED_CF_CANCEL,
    ICO_TOUCH_ED_CF_COUNT
};

/* what ico_touch_edit_tick returns */
enum { ICO_TOUCH_ED_NONE = 0, ICO_TOUCH_ED_LEAVE = 1 };

/* one finger event, queued until the tick */
typedef struct IcoTouchEditEvent {
    uint64_t id;
    float nx, ny; /* 0..1 over the output */
    int kind;     /* ICO_TOUCH_DOWN / MOVE / UP / CANCEL */
    uint64_t ns;
} IcoTouchEditEvent;

#define ICO_TOUCH_ED_QUEUE 64

typedef struct IcoTouchEditor {
    int active;         /* fingers go here, not to the mapper */
    int rule;           /* the posture being edited (the live layout's rule) */
    IcoTouchEdits work; /* the working copy; copied to *live on Done */
    IcoTouchEdits *live;
    int state;            /* ICO_TOUCH_ED_* */
    int selected;         /* ICO_TOUCH_CTL_*, -1 none */
    int all;              /* the All key: size and opacity steps apply to every control */
    unsigned int overlap; /* 1 << ctl for each control overlapping another */
    int refused;          /* ticks left to show "cannot cover another button's middle" */
    unsigned int changed; /* the work differs from the live edits */
    /* the drag or pinch */
    uint64_t finger[2];
    float x0[2], y0[2]; /* where each went down, output pixels */
    float fx[2], fy[2]; /* where each is now */
    int fingers;        /* 0..2 down */
    float cx0, cy0;     /* the dragged control's centre at the drag's start */
    float scale0;       /* its scale at the pinch's start */
    int moved;          /* past the drag threshold */
    /* the gamepad's repeat */
    unsigned int padHeld;
    int padTicks;
    /* events since the last tick */
    IcoTouchEditEvent queue[ICO_TOUCH_ED_QUEUE];
    int qn;
    /* the undo stack (the work before each change), newest last */
    IcoTouchEdits undo[ICO_TOUCH_EDIT_UNDO];
    int undos;
    /* the last tick's hit boxes, from the layout it was given */
    IcoTouchRect tb[ICO_TOUCH_ED_TB_COUNT];
    IcoTouchRect confirm[ICO_TOUCH_ED_CF_COUNT];
    IcoTouchRect ctl[ICO_TOUCH_CTL_COUNT]; /* each control's bounding box */
} IcoTouchEditor;

/* Start editing posture rule: work = *live, active = 1, nothing selected.
   The caller cancels the mapper's fingers first (input_sdl.c). */
void ico_touch_edit_begin(IcoTouchEditor *ed, IcoTouchEdits *live, int rule);
/* One finger event (the device layer's, as ico_touch_event's arguments);
   queued for the tick. CANCEL drops a drag or pinch at once, keeps the edits. */
void ico_touch_edit_event(IcoTouchEditor *ed, uint64_t finger, float nx, float ny, int kind,
                          uint64_t nowNs);
/* Once a Main tick: drains the queue against l (the preview layout built
   from ed->work: its rects are where the controls are drawn), then the
   gamepad word (ICO_PAD_* bits held this tick; 0 without a pad). Returns
   ICO_TOUCH_ED_LEAVE when Done was taken, else NONE. When l->rule differs
   from ed->rule (the phone folded mid-edit) the drag is cancelled and the
   editor switches to l->rule. */
int ico_touch_edit_tick(IcoTouchEditor *ed, const IcoTouchLayout *l, unsigned int padHeld);
/* Stop: with keep, work -> *live (gen bumped); active = 0 either way. */
void ico_touch_edit_end(IcoTouchEditor *ed, int keep);
/* What the drawing needs, from the last tick (touch.h IcoTouchEditView). */
void ico_touch_edit_view(const IcoTouchEditor *ed, const IcoTouchLayout *l, IcoTouchEditView *out);

/* The toolbar's and confirm strip's hit boxes for a layout (the spot the
   plan proves clear of every default control: FULL and TABLETOP a 2 x 5
   block at x Rt-53..Rt-19, y T+13..T+28 in q; SPLIT 2 columns x 5 rows at
   x L+43..L+55, y T+13..T+52). Tests read it too. */
void ico_touch_edit_toolbar(const IcoTouchLayout *l, IcoTouchRect tb[ICO_TOUCH_ED_TB_COUNT],
                            IcoTouchRect confirm[ICO_TOUCH_ED_CF_COUNT]);

#endif /* ICO_PORT_INPUT_TOUCH_EDIT_H */

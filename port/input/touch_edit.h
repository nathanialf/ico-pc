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
 * are stored in, touch.c; for the look pad, a rectangle, the offsets are
 * those of its edges on the anchor sides), a size scale, an opacity
 * multiplier, and for the look pad its size in q. An unset control is at
 * its default place. The layout builder (ico_touch_layout_edits) applies
 * them after place(): the rects clamped inside the region and out of the
 * hinge band; a control at alpha 0 is not drawn, and Off (active 0) also
 * skipped by the hit test (a Hidden one, active 1, still takes touches).
 *
 * The rails: scale ICO_TOUCH_EDIT_SCALE_MIN..MAX; alpha 0..1 for every
 * control (each can be Hidden or Off: Android's Back still opens the pause
 * menu, and with the D-pad, Cross or Circle Off a tap clicks a menu row,
 * touch.h ico_touch_menu_taps); ico_touch_edit_sanitize enforces them on
 * every setter and on every parsed config line.
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

/* ax or ay of a control at its default place on that axis: the words
   "hidden" and "off" alone parse to it (a control with no position) */
enum { ICO_TOUCH_ANCHOR_DEFAULT = 2 };

#define ICO_TOUCH_EDIT_SCALE_MIN 0.7f /* a face at 0.7 is 7.7 mm across: Android's 48 dp */
#define ICO_TOUCH_EDIT_SCALE_MAX                                                                   \
    3.0f /* a tablet's (v0.8.5 and before: 1.6); a box larger
                                         than the region is shrunk to fit it (touch.c) */
#define ICO_TOUCH_EDIT_GRID_DEFAULT 4.0f
#define ICO_TOUCH_EDIT_UNDO 8
#define ICO_TOUCH_EDIT_LOOK_MIN 20.0f    /* the look pad's least width and height, q */
#define ICO_TOUCH_EDIT_OFFSET_MAX 500.0f /* the largest offset or look pad side, q */
#define ICO_TOUCH_EDIT_DRAG_MM 2.0f      /* a shorter move is a tap, q */
#define ICO_TOUCH_EDIT_REFUSED 60        /* ticks the refused note shows */
/* the gamepad's D-pad nudge repeats after this many ticks held, then every
   ICO_TOUCH_EDIT_REPEAT ticks (8 a second at the 25 Hz Main tick) */
#define ICO_TOUCH_EDIT_REPEAT_DELAY 8
#define ICO_TOUCH_EDIT_REPEAT 3

/* the grid sizes the Grid size row offers, in q */
extern const float ico_touch_edit_grids[3];

typedef struct IcoTouchCtlEdit {
    unsigned char set;    /* 0: the default place (nothing stored or written) */
    unsigned char ax, ay; /* ICO_TOUCH_ANCHOR_* (DEFAULT: the default place) */
    unsigned char active; /* 1: takes touches. Shown: alpha > 0 (sanitize makes it 1);
                             Hidden: alpha 0, active 1 (works, not drawn); Off:
                             alpha 0, active 0 (neither). Beside the anchors, so the
                             struct has no padding (the edits are compared with
                             memcmp) */
    float dx, dy;         /* the control's centre from those edges, in q (negative: past
                             the safe area, under the camera hole); the
                             look pad's edges on those sides */
    float scale;          /* ICO_TOUCH_EDIT_SCALE_MIN..MAX, 1 = the default size */
    float alpha;          /* 0..1 times the Touch opacity setting; 0 = not drawn
                             (Hidden or Off, by active) */
    float w, h;           /* ICO_TOUCH_CTL_LOOK only: the pad's size in q at scale 1;
                             0 its default size */
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
/* 1 for every control (ICO_TOUCH_CTL_*; 0 out of range): each may be
   Hidden or Off (v0.8.5 and before: Select, L1, L2, R2 and the look pad
   only, with a 25 % floor on the rest). */
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

/* The [touch] tables' format: [touch] format = 2 is written with every
   layout from v0.8.6 on; a file without it (v0.8.4, v0.8.5) is format 1. */
#define ICO_TOUCH_EDIT_FORMAT 2

/* One control's edit as config text and back. Text: whitespace-separated
   tokens "l<n>"|"r<n>" (the x anchor and offset), "t<n>"|"b<n>", "s<scale>"
   (optional, 1 when absent), "o<percent>" (optional, 100 when absent), and
   for the look pad "w<n> h<n>" (optional, its default size when absent);
   an offset may be negative ("l-3": past the safe area). At opacity 0 a
   word instead of "o": "hidden" (Hidden: not drawn, still takes touches) or
   "off" (Off: neither), alone (at the default place) or after the place
   and size ("l4 t6 s1.00 hidden", "r20 b10 s1.50 off": shown again, it is
   where the player put it); "default" alone (unset). Numbers are plain
   decimals (a sign only on an offset, no exponent, a point whatever the
   locale). Examples "r20 b10.5 s1.00 o100", "l50 t0 w60 h30 o100".
   ico_touch_edit_text writes the same form into buf and returns it:
   offsets and sizes to two decimals with trailing zeros dropped, the scale
   to two decimals always (the look pad's only when not 1 with a size), the
   opacity as a whole percent, or the word at 0 (never "o0"); "default" for
   an unset edit or one shown at its default place.
   ico_touch_edit_parse_format fills *c for ctl and returns 0, or -1 for a
   token it does not know (w/h on a control that is not the look pad), a
   value out of range (offsets -ICO_TOUCH_EDIT_OFFSET_MAX..OFFSET_MAX, s
   SCALE_MIN..MAX, o 0..100, w/h LOOK_MIN..OFFSET_MAX), a token twice, two
   words, a word with an "o", "default" with anything, or a missing anchor:
   *c is then left unset. format is the file's ([touch] format; 1 without
   the key): from ICO_TOUCH_EDIT_FORMAT on, "hidden" and "o0" are Hidden
   and "off" Off; under it "hidden" and "o0" are Off too (what v0.8.4 and
   v0.8.5 meant by them: the control neither drawn nor working). A parsed
   edit is sanitized. ico_touch_edit_parse is the current format's. */
const char *ico_touch_edit_text(const IcoTouchCtlEdit *c, char *buf, unsigned size);
int ico_touch_edit_parse_format(IcoTouchCtlEdit *c, int ctl, const char *text, int format);
int ico_touch_edit_parse(IcoTouchCtlEdit *c, int ctl, const char *text);
/* The rails on one edit in place: scale clamped to the range, alpha to 0..1
   (a NaN 1), active 1 whenever alpha > 0 (a drawn control works), offsets
   -ICO_TOUCH_EDIT_OFFSET_MAX..OFFSET_MAX (a NaN 0), an unknown anchor DEFAULT, the look pad's
   size 0 or ICO_TOUCH_EDIT_LOOK_MIN..OFFSET_MAX (w and h 0 on the others).
   An unset edit is left alone. */
void ico_touch_edit_sanitize(IcoTouchCtlEdit *c, int ctl);
/* Clear every control of one posture (rule), or of all (rule < 0); snap
   and grid untouched; gen bumped. */
void ico_touch_edits_reset(IcoTouchEdits *e, int rule);
/* 1 when any control of the posture (rule < 0: any posture) is set. */
int ico_touch_edits_any(const IcoTouchEdits *e, int rule);

/* --- the editor ------------------------------------------------------------ */

/* IcoTouchEditor.state */
enum { ICO_TOUCH_ED_IDLE = 0, ICO_TOUCH_ED_DRAG, ICO_TOUCH_ED_PINCH, ICO_TOUCH_ED_CONFIRM_RESET };

/* the toolbar strip's cells, left to right (IcoTouchEditView.tb[]): eight
   keys and two display-only readouts, SIZE_VAL (the selected control's
   size: a pinch on the control or the gamepad's L2/R2 change it) and
   OP_VAL (its opacity, between Opacity - and +). A finger on a readout is
   the toolbar's (nothing under it is picked) and does nothing. */
enum {
    ICO_TOUCH_ED_TB_DONE = 0,
    ICO_TOUCH_ED_TB_UNDO,
    ICO_TOUCH_ED_TB_RESET,
    ICO_TOUCH_ED_TB_SNAP,
    ICO_TOUCH_ED_TB_GRID,
    ICO_TOUCH_ED_TB_ALL,
    ICO_TOUCH_ED_TB_SIZE_VAL, /* display only: "Size 120 %" */
    ICO_TOUCH_ED_TB_OP_DN,
    ICO_TOUCH_ED_TB_OP_VAL, /* display only: "75 %" */
    ICO_TOUCH_ED_TB_OP_UP,
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
    /* the gesture (touch_edit.c) */
    int grab;       /* the control the first finger went down on, -1 none */
    int consumed;   /* the first finger is spent (the toolbar, the strip, a pinch's end) */
    int pushed;     /* the gesture has pushed its undo entry */
    float d0;       /* the fingers' distance at the pinch's start, pixels */
    int confirmSel; /* the confirm strip's key the gamepad is on (ICO_TOUCH_ED_CF_*) */
    int lastChange; /* the last tick changed the work: a posture flip right after
                       (a control put on a flat crease) is undone and refused */
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

/* The box (pixels, before the clamp) the edit c gives control ctl on l at
   its scale: the D-pad's cross, a round button's square, the stick's 2 R
   square, the look pad's rect (an unset c: scale 1, the look pad as l has
   it). */
void ico_touch_edit_box_size(const IcoTouchLayout *l, int ctl, const IcoTouchCtlEdit *c, float *w,
                             float *h);
/* Moves the centre (cx, cy) of a w x h box inside l's bounds (the window, or
   below a TABLETOP band; wider than the region) and out of its hinge band
   (carried along the hinge to the bounds), to the nearer side of the band that holds it (touch.c, the
   clamp the edits get). 1 when it is clear of the band and inside; 0 when
   only inside (or centred on an axis the region is too small for). */
int ico_touch_edit_clamp(const IcoTouchLayout *l, float *cx, float *cy, float w, float h);
/* 1 when the set edit c has an offset past the safe area (a negative dx on
   an L or R anchor, a negative dy on a T or B one): only such a control is
   clamped to l's bounds; every other (the v0.8.4 forms, whose offsets were
   never negative) is clamped inside the region and out of its own band as
   v0.8.4 clamped it, so an older layout file renders as it did. */
int ico_touch_edit_outside(const IcoTouchCtlEdit *c);
/* ico_touch_edit_clamp with that choice for the edit c (the clamp
   ico_touch_layout_edits gives it; the editor's own view of the control). */
int ico_touch_edit_clamp_ctl(const IcoTouchLayout *l, const IcoTouchCtlEdit *c, float *cx,
                             float *cy, float w, float h);

/* The toolbar's and confirm strip's hit boxes for a layout
   (ico_touch_edit_toolbar), and the hint line's box (ico_touch_edit_hint_
   rect); the derivation is touch_edit.c's (q the layout's unit, L, T, Rt,
   B the region's edges). The toolbar is one strip of ICO_TOUCH_ED_TB_COUNT
   cells of equal width along the region's top, y T+0.5..T+5.5, x L+3..
   Rt-3, in ICO_TOUCH_ED_TB_* order. SPLIT cuts it at the hinge band (3 q
   clear of it each side): Done .. All on the left side (as many of them as
   are at least ICO_TOUCH_ED_CELL_MIN q wide there, at most six), the rest
   on the right side in one row, or in two (y T+0.5..T+5.5 and T+6..T+11,
   the first row the larger half) when one row's cells would be narrower
   than ICO_TOUCH_ED_CELL_MIN (fit()'s smallest sides never need it). The
   confirm strip: three cells of equal width across Done .. All (SPLIT:
   across the left side's cells). The hint: y B-4..B-1, FULL and TABLETOP
   x L+35..Rt-35 (between the stick's ring and Square); SPLIT the wider
   side of the band, 3 q in from its edges. Tests read both. */
#define ICO_TOUCH_ED_CELL_MIN 8.0f /* q: the narrowest strip cell before SPLIT's second row */
void ico_touch_edit_hint_rect(const IcoTouchLayout *l, IcoTouchRect *hint);
void ico_touch_edit_toolbar(const IcoTouchLayout *l, IcoTouchRect tb[ICO_TOUCH_ED_TB_COUNT],
                            IcoTouchRect confirm[ICO_TOUCH_ED_CF_COUNT]);

#endif /* ICO_PORT_INPUT_TOUCH_EDIT_H */

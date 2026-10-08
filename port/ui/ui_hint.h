/*
 * port/ui/ui_hint.h
 *
 * A line of button prompts on a port page: each item the game's own button glyph (layout_ext.h
 * LtExtGlyph; two for a pair such as L1 / R1, none for a word alone such as
 * "Left stick: turn") followed by its word, the line centred across the
 * screen.  The words are port rows like every Settings label (the port
 * font, the light letters with the dark rim, drawn deferred at the output's
 * resolution in Enhanced), the glyphs glyph rows (the game's sprites).  The
 * music gallery's transport and the model viewer's hints use it.
 *
 * ui_HintLayout measures the words in the current language and places the
 * items; a line wider than UI_HINT_WIDTH is set smaller to fit, glyphs and
 * words alike, down to 60 % as a Settings label is.
 */
#ifndef PORT_UI_UI_HINT_H
#define PORT_UI_UI_HINT_H

#include "layout_ext.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_HINT_MAX 8
#define UI_HINT_WIDTH 600.0f

typedef struct UiHintItem {
    int glyph;  /* LtExtGlyph, or -1: the word alone */
    int glyph2; /* a second glyph after the first (L1 then R1), or -1 */
    int strId;  /* the word (strings.h) */
} UiHintItem;

typedef struct UiHint {
    int y;      /* the words' box top (dispY, field lines; the box is 15 high) */
    float size; /* the words' em */
    int n;
    UiHintItem item[UI_HINT_MAX];
    int glyphRow[UI_HINT_MAX][2]; /* -1 where there is none */
    int textRow[UI_HINT_MAX];
    unsigned char shown[UI_HINT_MAX];
    float scale; /* what the last layout set (1, or less to fit) */
} UiHint;

/* The port's prompt lines, shared by their pages and settings_render:
   the music gallery's transport (L1 previous, Cross play or pause, Square
   stop, R1 next, Left / Right section, Triangle back; settings.c), the
   model list's (Cross view, Triangle back or title screen) and the
   viewer's two lines (the sticks; Cross play, Square loop, L1 / R1
   animation, Triangle models; port/game/model_viewer.c). */
enum {
    UI_HINT_GAL_PREV,
    UI_HINT_GAL_PLAY,
    UI_HINT_GAL_STOP,
    UI_HINT_GAL_NEXT,
    UI_HINT_GAL_SECTION,
    UI_HINT_GAL_BACK,
    UI_HINT_GAL_COUNT
};

enum { UI_HINT_MV_LIST_VIEW, UI_HINT_MV_LIST_BACK, UI_HINT_MV_LIST_COUNT };

enum { UI_HINT_MV_TURN, UI_HINT_MV_MOVE, UI_HINT_MV_ZOOM, UI_HINT_MV_STICKS_COUNT };

enum {
    UI_HINT_MV_PLAY,
    UI_HINT_MV_LOOP,
    UI_HINT_MV_ANIM,
    UI_HINT_MV_BACK,
    UI_HINT_MV_SAVE,
    UI_HINT_MV_KEYS_COUNT
};

extern const UiHintItem ui_hint_gallery[UI_HINT_GAL_COUNT];
extern const UiHintItem ui_hint_mv_list[UI_HINT_MV_LIST_COUNT];
extern const UiHintItem ui_hint_mv_sticks[UI_HINT_MV_STICKS_COUNT];
extern const UiHintItem ui_hint_mv_keys[UI_HINT_MV_KEYS_COUNT];

/* Adds the rows (for each item its glyphs, then its word) and lays them
   out. */
void ui_HintBuild(UiHint *h, int y, float size, const UiHintItem *items, int n);
/* Shows or hides item i (its rows masked by default); then lay it out. */
void ui_HintShow(UiHint *h, int i, int on);
/* Changes item i's word. */
void ui_HintSetStr(UiHint *h, int i, int strId);
/* Places the shown items in the current language: once a tick from the
   page's proc is cheap (a measure per word). */
void ui_HintLayout(UiHint *h);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_UI_HINT_H */

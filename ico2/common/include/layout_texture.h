/*
 * ico2/common/include/layout_texture.h
 *
 * The declarations of what layout_texture.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef LAYOUT_TEXTURE_H
#define LAYOUT_TEXTURE_H

#include "typedef.h"

/* the item-select callback the menu keeps as a word (an int on the EE) */
typedef int (*LtSelectFn)(int);

/* layout_texture.o's .sdata globals: current_layout_id,
   lt_item_select_disable and the continue screen's decided flag */
extern int lt_continue_selected;
extern int current_layout_id;
extern int lt_item_select_disable;
/* layout_texture.c's `inline` functions, in the order of their definitions'
 * out-of-line copies at the end of the object (first-declaration order). */
void lt_switch_layout(int no);
int lt_current_property_item(void);
int lt_link_layout(int dir);
int lt_prev_layout(int stage);
int lt_next_layout(int stage);
void lt_mask_property(int idx, int flag);
void lt_default_mask_property(int idx, int flag);
int lt_fade_status(void);
void lt_set_item_select_func(ICO_WORD_PTR(LtSelectFn) val);
void lt_set_fade_mode(int val);
void lt_analog2Pad(void);
void exec_layout_texture(void);
void init_layout_texture(int stage);

/* tex-property: one layout texture property, 0x70 bytes, the rows of
 * texProperty a layout's first..last range covers. Readers:
 * ico2/common/src/layout_texture.c, ico2/fumi/src/jimaku.c (texNo). The
 * pad bits name the item links: 0x1000 up, 0x4000 down, 0x8000 left, 0x2000
 * right (exec_layout_texture). */
typedef struct LtProperty { /* field names derived */
    int word0;              /* 0x00, -1 in the shipped rows; no C reader */
    int word4;              /* 0x04, no C reader */
    int word8;              /* 0x08, no C reader */
    int wordC;              /* 0x0C, no C reader */
    int ownerItem;          /* 0x10, the item whose selection keeps this one lit, -1 for none */
    int word14;             /* 0x14, no C reader */
    void *texData;          /* 0x18, 0 in the shipped rows */
    int texNo;              /* 0x1C, the texture tex_TransTexture sends */
    int up;                 /* 0x20, lt_link_layout's layout for direction 3 */
    int down;               /* 0x24, for direction 2 */
    int left;               /* 0x28, for direction 1, and the layout a triangle switches to */
    int right;              /* 0x2C, for direction 0, and the layout a cross switches to */
    int rightItem;          /* 0x30, the item the pad's right moves to, -1 for none */
    int leftItem;           /* 0x34 */
    int downItem;           /* 0x38 */
    int upItem;             /* 0x3C */
    int word40;             /* 0x40, no C reader */
    int centerX;            /* 0x44, nonzero centres the sprite across the screen */
    int dispH;              /* 0x48, the sprite's height in field lines, 0 for the texture's */
    int dispW;              /* 0x4C, its width in pixels, 0 for the texture's */
    int dispY;              /* 0x50, from the top of the 226-line screen */
    int dispX;              /* 0x54, from the left of the 640-pixel screen */
    int texFileNo;          /* 0x58, the texFile row the texture is named from */
    int texU;               /* 0x5C, the texel rectangle the sprite draws */
    int texH;               /* 0x60 */
    int texW;               /* 0x64 */
    int texV;               /* 0x68 */
    /* 0x6C: the flag word, declared as bits */
    unsigned int selectMode : 2;  /* 1: a cross on the item takes the long fade */
    unsigned int fade_cancel : 1; /* drawn in the highlight colour */
    unsigned int selectable : 1;  /* dimmed when another item is selected, glows when it is */
    unsigned int masked : 1;      /* not drawn (lt_mask_property) */
    unsigned int defaultMask
        : 1; /* the mask restored on a layout switch (lt_default_mask_property) */
    unsigned int : 26;
} LtProperty; /* derived name */

extern LtProperty texProperty[];

/* texture-layout: one texture layout, 0x38 bytes. Readers:
 * ico2/common/src/layout_texture.c (LtProp), kanban.c (KanbanProp),
 * kanbanBoot.c, layout_action.c. Owner: ico2/common/include/layout_texture.h.
 * procFirst is set when the layout chain resets and cleared after proc
 * runs with it. */
typedef struct {                     /* field names derived */
    int first;                       /* 0x00, the first tex-property row */
    int last;                        /* 0x04 */
    float fadeInTime;                /* 0x08, seconds the layout fades in over, 0 for at once */
    float fadeOutTime;               /* 0x0C, seconds it fades out over */
    float colR;                      /* 0x10, the backdrop sprite colour, 0 to 1 */
    float colG;                      /* 0x14 */
    float colB;                      /* 0x18 */
    float colA;                      /* 0x1C */
    int (*proc)(int flag, int item); /* 0x20, the selection handler: returns the item */
    int procFirst;                   /* 0x24, proc's first-call flag */
    int defaultItem;                 /* 0x28, copied into curItem */
    int curItem;                     /* 0x2C */
    int link;                        /* 0x30, the next layout, -1 for none */
    int word34;                      /* 0x34 */
} LtProp;                            /* derived name */

extern LtProp texLayout[];

#endif /* LAYOUT_TEXTURE_H */

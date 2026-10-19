/*
 * ico2/common/include/kanban.h
 *
 * The declarations of what kanban.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef KANBAN_H
#define KANBAN_H

#include "layout_texture.h"

typedef struct { /* field names derived */
    unsigned char b[4];
} KanbanCol; /* derived name */

/* one sign on screen: a texture layout drawn over a backdrop, faded in and
   out, kept in a list ordered by priority (kanbanReqAdd) */
typedef struct Kanban {  /* field names derived */
    LtProp *layout;      /* 0x00, the texLayout row shown, 0 for a free entry */
    int pri;             /* 0x04, the list is kept in ascending order of it */
    int key;             /* 0x08, this frame's key: 1 cross, 2 triangle (JPN 1 circle, 2 cross), 0 none */
    int flags;           /* 0x0C, bit 0: fading out */
    float alpha;         /* 0x10, 0 to 127 */
    KanbanCol col;       /* 0x14 */
    struct Kanban *next; /* 0x18 */
    struct Kanban *prev; /* 0x1C */
} Kanban;                /* derived name */

void kanbanReqDel(Kanban *self);
void kanbanReqDelFade(Kanban *self);
void kanbanReqAllDel(void);
void kanbanReqAllDelFade(void);
void kanbanExec(void);

#ifdef ICO_RD

void kanbanTitleCover(void); /* PC port: kanban.c */
int kanbanTitleCoverActive(void);

#endif

void kanbanInit(int no);
Kanban *kanbanReqAdd(int no, int pri);
/* kanban.o's .sdata global */
extern int kanbanCommonRead;

#endif /* KANBAN_H */

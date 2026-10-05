/*
 * ico2/sugipon/include/multiBgaManager.h
 *
 * The declarations of what multiBgaManager.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef MULTIBGAMANAGER_H
#define MULTIBGAMANAGER_H

/* One multi-BGA slot: the frame its animation has reached (below 0 while the
   slot is idle), the position, the drift added to the position every frame
   (zero unless entered sensitive), the rotation quaternion, the animation and
   whether it stays on its last frame.  The record is quadword aligned, as its
   vectors are. */
typedef struct BgaDisp {                /* field names derived */
    float frame;                        /* 0x00 */
    float pad04[3];                     /* 0x04 */
    float pos[4];                       /* 0x10 */
    float vel[4];                       /* 0x20 */
    float rot[4];                       /* 0x30 */
    int kind;                           /* 0x40, -1 for none */
    int stay;                           /* 0x44 */
    int pad48[2];                       /* 0x48 */
} __attribute__((aligned(16))) BgaDisp; /* derived name */

/* The declarations below lead this header because their order is load-bearing:
 * gcc 2.9 emits the deferred out-of-line copy of a plain-inline function in
 * first-declaration order, so this is the order multiBgaManager.c's inline tail has. */
void EntryMultiBgaManagerNoKind(BgaDisp *bga, int no, void *pos);
void DispMultiBgaManagerWithKind(int kind, BgaDisp *base, int n);
void EntryMultiBgaManager(BgaDisp *bga, int no, int kind, void *pos, void *rot);
BgaDisp *InitMultiBgaManager(int n);
void EntryMultiBgaManagerSensitive(BgaDisp *bga, int no, int kind, void *pos, void *rot, void *vel);

/* the state every slot starts from: idle, at the origin, not drifting,
   unrotated, no animation, not staying.  It is the slot's fields without the
   slot's quadword alignment, and a slot is reset by copying it through the
   slot's type. */
typedef struct {    /* field names derived */
    float frame;    /* 0x00 */
    float pad04[3]; /* 0x04 */
    float pos[4];   /* 0x10 */
    float vel[4];   /* 0x20 */
    float rot[4];   /* 0x30 */
    int kind;       /* 0x40 */
    int stay;       /* 0x44 */
    int pad48[2];   /* 0x48 */
} BgaAnimeState;    /* derived name */

#ifdef ICO_HOST

#include "ee_view.h"

/* PC port: a slot is reset by copying this state through BgaDisp, which is
   16-byte aligned: the host compiler moves it with aligned SSE loads, so the
   state carries that alignment too (the EE's copy did not need it).  The
   host layouts must agree (tools/template_audit.py). */
extern BgaAnimeState InitialBgaMultiAnimeState __attribute__((aligned(16)));

ICO_LAYOUT_AT(BgaDisp, frame, BgaAnimeState, frame);

ICO_LAYOUT_AT(BgaDisp, pos, BgaAnimeState, pos);

ICO_LAYOUT_AT(BgaDisp, vel, BgaAnimeState, vel);

ICO_LAYOUT_AT(BgaDisp, rot, BgaAnimeState, rot);

ICO_LAYOUT_AT(BgaDisp, kind, BgaAnimeState, kind);

ICO_LAYOUT_AT(BgaDisp, stay, BgaAnimeState, stay);

ICO_LAYOUT_SIZE(BgaDisp, BgaAnimeState);

#else

extern BgaAnimeState InitialBgaMultiAnimeState;

#endif

void DispMultiBgaManager(BgaDisp *base, int n);

#endif /* MULTIBGAMANAGER_H */

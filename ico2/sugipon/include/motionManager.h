/*
 * ico2/sugipon/include/motionManager.h
 *
 * The declarations of what motionManager.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef MOTIONMANAGER_H
#define MOTIONMANAGER_H

struct GObj;

#include "typedef.h"

/* one entry of a node's interp limit table, in degrees: the heading, pitch
   and bank limits _getFinalMatrix's turns are clamped to, read at 12-byte
   steps (the table's +0x30 and +0x48 headings are entries 4 and 6) */
typedef struct { /* field names derived */
    float h;
    float p;
    float b;
} MotLimAng; /* derived name */

/* The declarations below lead this header because their order is load-bearing:
 * gcc 2.9 emits the deferred out-of-line copy of a plain-inline function in
 * first-declaration order, so this is the order motionManager.c's inline tail has. */
void SetHitCollisionDisplay(int a, int b);
int ResetMotionProgramInterpInfo(struct GObj *self, int focus);

int SetDirectMotionProgramInterpInfo(struct GObj *self, int focus, float rate);

void GetGeometryOfMotion(void *self, void *m0, void *m1, float *v, float r, float *step, int k);
void EditRotEmphasys(void);
void landingFieldAction(ClipWork *w);
void SkelTest(GObj *self);
void SkelTestGeo(GObj *self);
void GetMatrixOfMotion(GObj *self, char *tbl, void *ofs);

#endif /* MOTIONMANAGER_H */

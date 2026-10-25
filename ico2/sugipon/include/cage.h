/*
 * ico2/sugipon/include/cage.h
 *
 * The declarations of what cage.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef CAGE_H
#define CAGE_H

struct GObj;

/* The declarations below lead this header because their order is load-bearing:
 * gcc 2.9 emits the deferred out-of-line copy of a plain-inline function in
 * first-declaration order, so this is the order cage.c's inline tail has. */
int GetCageChainPoint(float *root, float *tip, struct GObj *self);
void SetCageVelocityFriction(struct GObj *self, float friction);
void StabilizeAllLayoutedCage(void);
void SetCageChainHangableFlag(struct GObj *self, int flag);

void HotInitCageGeo(struct GObj *self);
void SetCageFixGeometry(struct GObj *self, void *pos, void *dir);

#endif /* CAGE_H */

/*
 * ico2/sugipon/include/worm.h
 *
 * The declarations of what worm.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef WORM_H
#define WORM_H

struct GObj;

/* The declarations below lead this header because their order is load-bearing:
 * gcc 2.9 emits the deferred out-of-line copy of a plain-inline function in
 * first-declaration order, so this is the order worm.c's inline tail has. */
inline void SetDirectWormTargetPos(struct GObj *act, void *pos);
inline void SetWormReduceRatio(struct GObj *act, float ratio);
void TraceWormRoute(struct GObj *act, float t);

struct WormInit;

void *InitWormGeo(struct GObj *act, struct WormInit *ini);
void WormGeo(struct GObj *act);
void WormDL(struct GObj *act);

#endif /* WORM_H */

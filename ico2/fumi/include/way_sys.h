/*
 * ico2/fumi/include/way_sys.h
 *
 * The declarations of what way_sys.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef WAY_SYS_H
#define WAY_SYS_H

#include "way_util.h"

/* way_sys.c's `inline` functions, in the order of their definitions'
 * out-of-line copies at the end of the object (first-declaration order). */
WayPoint *GetWay_begin(float *from, WVTObj *w, float *goal);
void BridgeBox(void);
inline void DeleteGuideWay(WVTObj *o);
WayPoint *GetWay_next(WVTObj *w, float *pos);

WayPoint *_FUNC_GetWay_begin(float *from, WVTObj *w, float *goal, int threaded);

int GetNearNigePointN(void *out, int num, WVTObj *w, float *pos);

#endif /* WAY_SYS_H */

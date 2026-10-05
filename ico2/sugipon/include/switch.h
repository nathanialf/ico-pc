/*
 * ico2/sugipon/include/switch.h
 *
 * The declarations of what switch.c defines, for box.c, which includes
 * switch.c.  The file name is derived.
 */

#ifndef SWITCH_H
#define SWITCH_H

#include "sceneManager.h"

struct GObj;
struct Sub15C;

/* The floor and wall lever geometry block InitFloorLeverGeo returns: eight
 * words, read back as such at box.c's own call sites.  It is not called
 * FloorLeverGeo, which is the lever's per-frame function. */
typedef struct {     /* field names derived */
    short shake;     /* 0x00, the X rock the lever gives while it springs back */
    short angle;     /* 0x02, the lever's Z angle */
    int state;       /* 0x04, 0 at rest, 1 or -1 once thrown */
    int timer;       /* 0x08, frames since the lever was thrown */
    ICO_WORD_PTR(struct Sub15C *) base; /* 0x0C, the base DObj, kept as a word */
    struct Sub15C *handle; /* 0x10, the handle DObj, drawn turned by the two angles */
    int linked;      /* 0x14, nonzero once the lever is parented to the floor under it */
    int linkWait;    /* 0x18, frames counted before the parenting probe */
    int (*trigger)(struct GObj *, int); /* 0x1C, called with the parent object and the state */
} LeverGeoWork; /* derived name */

/* The declarations below lead this header because their order is load-bearing:
 * gcc 2.9 emits the deferred out-of-line copy of a plain-inline function in
 * first-declaration order, so this is the order box.c's inline tail has. */
int InitSwitchGeo(void);
void SwitchGeo(void);
void SwitchDL(void);
void SetSwitchTriggerFunc(struct GObj *lever, int (*func)(struct GObj *, int));
void SetSwitchState(struct GObj *lev, int state);
void SetFloorLeverWithNodePoint(struct GObj *lev, struct GObj *actor, int focus);
int CanFloorLeverPull(struct GObj *lev);
LeverGeoWork *InitFloorLeverGeo(struct GObj *self, SObjSimpleSetting *lay);
int GetFloorLeverAngle(struct GObj *lev);
void SetWallLeverWithNodePoint(struct GObj *lev, struct GObj *actor, int focus);
int CanWallLeverPull(struct GObj *lev);
int IsWallLeverStatus(struct GObj *lev);
LeverGeoWork *InitWallLeverGeo(struct GObj *self, SObjSimpleSetting *lay);
int GetWallLeverAngle(struct GObj *lev);

#endif /* SWITCH_H */

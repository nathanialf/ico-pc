/*
 * ico2/sugipon/include/box.h
 *
 * The declarations of what box.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef BOX_H
#define BOX_H

/* accessary: one accessory model set, 0x28 bytes, indexed by the object's
 * Sub15C+0x844. Readers: box.c and switch.c.inc (the two models, the
 * sub-box, and the pivot as the wheels' height and front and rear axle Z),
 * pool.c (the negated pivot), weapon.c, cage.c, puddle.c. */
typedef struct {     /* field names derived */
    int model;       /* 0x00, CSVSYSTEM_InitDObj's first model */
    int model2;      /* 0x04, its second model */
    int subModel;    /* 0x08, the sub-box model InitBoxGeo creates */
    float pivot[3];  /* 0x0C, pool.c places the model at its negation */
    float subPos[3]; /* 0x18, the sub-box's offset */
    float subRotY;   /* 0x24, the sub-box's facing in degrees */
} AccessaryRec;      /* derived name */

extern AccessaryRec accessary[];

struct GObj;

/* The declarations below lead this header because their order is load-bearing:
 * gcc 2.9 emits the deferred out-of-line copy of a plain-inline function in
 * first-declaration order, so this is the order box.c's inline tail has. */
int CanHoldBox(struct GObj *self);
void BoxDL(struct GObj *self);
void GetBoxGlobalHoldPoint(float *out, struct GObj *self, float *local);
int IsThisBoxTruck(struct GObj *self);
void ExecBoxMoveStartReaction(struct GObj *self, int dir);
void ExecBoxMoveEndReaction(struct GObj *self);
int BoxGeoRestore(float *dst, float *src);
int BoxExtGeoRestore(void);
int BoxMemoryFunc(void);

/* box.c includes switch.c, so switch.h's declarations follow box.c's own */
#include "switch.h"

int CheckReadyAllSwitches(void);
int GetBoxMode(struct GObj *self);
int AlignBox(struct GObj *self, float grid);
int GetBoxHoldPoint(float *out, struct GObj *self, struct GObj *chara);

int MoveBoxWithHoldPoint(struct GObj *self, float *holdPoint, struct GObj *holder, int focus,
                         float *dir);

void GetFloorLeverGlobalHoldPoint(void *dst, struct GObj *lev);
void GetWallLeverGlobalHoldPoint(void *dst, struct GObj *lev);
int MoveFloatingBox(struct GObj *self, struct GObj *other, float *dst, void *src, float lim);
void ReInitBoxGeo(struct GObj *self);

#endif /* BOX_H */

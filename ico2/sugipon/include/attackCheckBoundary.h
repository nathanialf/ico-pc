/*
 * ico2/sugipon/include/attackCheckBoundary.h
 *
 * The declarations of what attackCheckBoundary.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef ATTACKCHECKBOUNDARY_H
#define ATTACKCHECKBOUNDARY_H

#include "typedef.h"
#include "ee_view.h"

struct GObj;

/* The declarations below lead this header because their order is load-bearing:
 * gcc 2.9 emits the deferred out-of-line copy of a plain-inline function in
 * first-declaration order, so this is the order attackCheckBoundary.c's inline tail has. */
ICO_WORD InitAttackCheckBoundaryGeo(int unused, void *obj);
void AttackCheckBoundaryGeo(struct GObj *self);
void AttackCheckBoundaryDL(struct GObj *obj);
void actAttackCheckBoundaryStart(struct GObj *self);
float GetAttackCheckBoundaryRadius(struct GObj *self);
struct GObj *CreateAttackCheckBoundary(int *obj, float x, float y, float z, float r);
int GetAttackCheckBoundaryManagerStatus(struct GObj *self);
void SetAttackCheckBoundaryAttribute(struct GObj *self, int attr);

/* the attribute word of a cloth layout record; the table holds 96 and 128
   here */
typedef enum { CLOTH_ATTR_NONE = 0 } ClothAttr;

/* one record of layoutClothDef, the cloth layout table: its name, the four
   corner points, the attribute and the boundary count.  This file reads the
   first two corners, the attribute and the boundary count, and InitFlagGeo
   reads the rest. */
typedef struct {    /* field names derived */
    char name[32];  /* 0x00 */
    float pt[4][3]; /* 0x20 */
    int kind;       /* 0x50, the cloth type in the low four bits (InitFlagGeo's switch) */
    ClothAttr attr; /* 0x54 */
    int rows;       /* 0x58, the cloth's rows (ClothCfg num) */
    int count;      /* 0x5C, the columns, and the boundaries a manager lays out */
    float length;   /* 0x60, the cloth's length, shared out over the columns */
    float weight;   /* 0x64, the fall added to each point a step (ClothCfg weight) */
} LayoutClothDef; /* derived name */

extern const LayoutClothDef layoutClothDef[];

#endif /* ATTACKCHECKBOUNDARY_H */

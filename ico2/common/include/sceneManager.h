/*
 * ico2/common/include/sceneManager.h
 *
 * The declarations of what sceneManager.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef SCENEMANAGER_H
#define SCENEMANAGER_H

#include <libvu0.h>
#include "typedef.h"

struct GObj;

/* the 0x40-byte layout record CSVSYSTEM_InitDObj starts a scene object from
   and CreateLayoutedGObj hands to the kind's constructor: position, rotation
   and scale as VU0 vectors, then the object word (the generator's word at
   0x30); 0x34..0x3F is the alignment tail, copied but never written.  With
   exactly these four members initSceneGObj's constructor fills a temporary
   without clearing it first, as the ROM does. */
typedef struct SObjSimpleSetting { /* field names derived */
    sceVu0FVECTOR pos;             /* 0x00 */
    sceVu0FVECTOR rot;             /* 0x10 */
    sceVu0FVECTOR scale;           /* 0x20 */
    ICO_WORD obj;                  /* 0x30, an index, or an object address (attackCheckBoundary.c) */
} SObjSimpleSetting;               /* derived name */

/* sceneManager.c's .data global */
extern SObjSimpleSetting InitialSObjSimpleSetting;
/* sceneManager.c's `inline` functions, in the order of their definitions'
 * out-of-line copies at the end of the object (first-declaration order). */
void ChangeStageStartInfo(int a0, int a1, int wait1, int wait2, int wait3);
struct GObj *CreateLayoutedGObj(int id, int model, int accessary, int light, void *lay, int label, int key, int useStart);
void MoveNextStage_Set(float *pos, float *rot, int wait1, int wait2, int wait3, int stage);
void test_nextstage_firstwalk_set(int unused, int wait1, int wait2, int wait3);
int GetStageStartInfo(struct GObj *self, int a1, int a2, int *wait1, int *wait2, int *wait3);
void MoveNextStage_Clear(void);
void InitStageLight(int stage);
void InitSceneObjects(int stage);

/* enemy-model-grp: one enemy model group, 0x28 bytes. Reader:
 * ico2/common/src/sceneManager.c (EnemyMdlRec). Owner:
 * ico2/common/include/sceneManager.h. */
typedef struct {   /* field names derived */
    char name[32]; /* 0x00 */
    int first;     /* 0x20, the enemymodelTable range */
    int last;      /* 0x24 */
} EnemyMdlRec;     /* derived name */

extern const int
    enemymodelTable[]; /* enemy-model-tbl: the model ids enemymodelGroup ranges cover */
extern const EnemyMdlRec enemymodelGroup[];

#endif /* SCENEMANAGER_H */

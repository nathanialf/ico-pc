/*
 * ico2/sugipon/include/enemy.h
 *
 * The declarations of what enemy.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef ENEMY_H
#define ENEMY_H

#include "region.h"

struct GObj;

/* enemy-random-def: one random enemy kind range, 8 bytes, for the kinds from
   0x10000 on: the first and the last row of randomEnemyVariationKind the
   kind draws from. */
typedef struct {  /* field names derived */
    int first;    /* 0x00 */
    int last;     /* 0x04 */
} EnemyKindRange; /* derived name */

/* enemy-def: one enemy kind, 0x1C bytes, by kind: the model and the particle
   object it is drawn with, its life, the scale its model is drawn at, the
   dodge range, then the attribute word: the parameter row in bits 0-7, the
   fly type in bits 8-9 and the battle type in bits 10-11. */
typedef struct {    /* field names derived */
    int model;      /* 0x00, the model id, 0x610 for none */
    int particle;   /* 0x04, the particle object, -1 for none */
    float life;     /* 0x08 */
    float float0C;  /* 0x0C */
    float scale;    /* 0x10 */
    float dodge;    /* 0x14 */
    int paraIndex : 8;
    unsigned int flyType : 2;
    unsigned int battleType : 2;
} EnemyDef; /* derived name */

/* one table per disc (enemyKind.c), in the European record */
extern const EnemyDef enemyKind_pal[];
extern const EnemyDef enemyKind_jpn[];
extern const EnemyDef enemyKind_ntsc[];
extern const EnemyDef enemyKind_aug6[];
#define enemyKind PAL_JPNKORCN_NTSC_AUG6(enemyKind_pal, enemyKind_jpn, enemyKind_ntsc, enemyKind_aug6)
extern const EnemyKindRange randomEnemyKind[];
extern const int randomEnemyVariationKind[];
int CanThisEnemyFly(struct GObj *self);
int CheckEnemyHit(struct GObj *self, float *pos, float *a, float *b);
void EnemyDeleteParticle(struct GObj *self, float *dir, short *list);
void EnemySetfAppearAll(struct GObj *self);
void EnemySetfDisappearAll(struct GObj *self);
int EnemyGetNSafeParts(struct GObj *self);
void enemySetParticleDie(void *root, float *dir);
int GetEnemyBattleType(struct GObj *self);
float GetEnemyDefDodgeRange(struct GObj *self);
float GetEnemyDefLife(struct GObj *self);
float GetEnemyFlyXZAccel(struct GObj *self);
int *GetEnemyHitNodeFlag(struct GObj *self);
int RandomizeEnemy(struct GObj *self);
void ResetEnemyPositionInfo(struct GObj *self);
void ReviveEnemyParticle(struct GObj *self, int node);
void SetEnemyDissolve(struct GObj *self, float ratio);
void SetEnemyFootPrintSwitch(struct GObj *self, int on);
int isExistEnemyParticle(struct GObj *self, int node);

#endif /* ENEMY_H */

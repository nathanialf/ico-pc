/*
 * ico2/omori/include/chain.h
 *
 * The declarations of what chain.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef CHAIN_H
#define CHAIN_H

struct GObj;

/* the wall a chain hangs against: the object and node ClipWall hit and the
   wall record it returned */
typedef struct ClimbCol {   /* field names derived */
    struct ObjNode wallSrc; /* typedef.h's record */
    struct FcWallEnt *wall;
} ClimbCol; /* derived name */

void ChainGeo(struct GObj *gobj);

void ChainPositionReset(struct GObj *chain);

int CheckChainClimbablePos(struct GObj *chain);
void EnableChainHang(struct GObj *chain);

struct ChainNode;

struct ChainPendulum;

void GetChainClimbCollision(struct ClimbCol *dst, struct GObj *chain);
void GetChainClimbOrient(float *dst, struct GObj *chain);
int GetChainDirCorrectVal(struct GObj *chain, int *deg);
float GetChainHangRange(struct GObj *chain);
float GetChainLength(struct GObj *chain);
void GetChainPendulum(struct GObj *chain, float *angle, float *amp, float *cycle);
void GetRootPositionHandExtra(void *gobj, float *out);
void DecreasePdlChain(struct GObj *chain);
void IncreasePdlChain(struct GObj *chain);

int IsAbleChainHang(struct GObj *chain);

void HoldChain(struct GObj *chain, struct GObj *owner, float *pos);
void LockChainGeo(struct GObj *chain);
void PlumbOrientUpdateChain(struct GObj *chain, float *src);
void ReleaseChain(struct GObj *chain, struct GObj *owner);
void SetChainParentGObj(struct GObj *chain, void *parent);

void SetChainRootUpdateMode(struct GObj *gobj, int mode, float *pos);

void UnLockChainGeo(struct GObj *chain);
void UnableChainHang(struct GObj *chain);

void _GetCorrectOrientOfChain(float *out, struct GObj *gobj, float *dir);
void correct_vector(float *out, float *v);
void ChainDL(struct GObj *gobj);
int isStopChain(struct GObj *chain);
int isBottomOfChain(struct GObj *chain);

int GetChainNearestNodePosition(float *out, struct GObj *gobj, float *p);

void InitPendulum(struct GObj *chain);

#endif /* CHAIN_H */

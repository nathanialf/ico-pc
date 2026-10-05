/*
 * ico2/sugipon/include/geometryManager.h
 *
 * The declarations of what geometryManager.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef GEOMETRYMANAGER_H
#define GEOMETRYMANAGER_H

struct GObj;
struct ObjNode;
struct Sub15C;

int CylinderCollision(struct GObj *self, int group, float r, float h, float s);

int CylinderCollisionWithControlDynamics(struct GObj *self, int group, int ctrl, float r, float h,
                                         float s);

struct GObj **GetCharGObjList(void);
void GetGlobalDirectionOrient(float *dir, struct GObj *obj, void *src);
void GetInitialSkeltonMatrixByDObj(struct Sub15C *mdl);
float GetProjectionOfPlane(void *out, void *plane, void *pos);
float GetProjectionOfPlaneWithKeepAway(void *out, void *plane, void *pos, float keepAway);
void GetProjectionPosOfPlane(void *out, void *plane, void *pos);
void GetRootMatrix(void *mtx, struct GObj *obj);
void GetRootMatrixRotOffset(void *q, struct GObj *obj);
void GetRootMatrixTransOffset(float *dst, struct GObj *src);
void GetRootMotionMatrix(float (*mtx)[4], struct GObj *obj);
void GetRootMotionOrient(float *dir, struct GObj *obj);
void GetRootOrient(float *dir, struct GObj *obj);
void GetRootPosition(void *pos, struct GObj *obj);
void GetRootPositionByDObj(void *pos, struct Sub15C *src);
void GetRootQuaternion(void *q, struct GObj *obj);
void GetRootQuaternionByDObj(void *q, struct Sub15C *dobj);
void GlobalizeGeometry(struct GObj *gobj);
int LimitExistGeometry(float *pos, float *move);
void LocalizeDirectionOrient(struct GObj *self, struct ObjNode *link);
void LocalizeGeometry(struct GObj *gobj, struct ObjNode *link);
void SetDirectRootPosition(struct GObj *self, void *v);
void SetDirectRootPositionNoFitting(struct GObj *self, void *v);
void SetDirectRootPositionNoFittingWithNodePoint(struct GObj *gobj, int node, float *pos, float t);

void SetDirectRootPositionNoFittingWithNodePointXZ(struct GObj *gobj, int node, float *pos,
                                                   float t);

void SetDirectRootPositionWithNodePoint(struct GObj *gobj, int node, float *pos, float t);
void SetRootMatrixRotOffset(struct GObj *obj, void *q);
void SetRootMatrixWithTransOffset(struct GObj *obj, float x, float y, float z);
void SetRootPosition(struct GObj *obj, void *pos);
void SetRootQuaternion(struct GObj *obj, void *quat);
void SetRootBaseQuaternion(struct GObj *obj, void *q);
void UpdateRootMatrix(struct GObj *obj);
void UpdateRootMatrixByDObj(struct Sub15C *dobj);

int cylinderCollisionCheck(struct GObj *self, float *ppos, struct GObj *target, float r, float rr,
                           float h, float s, float t, int ctrl, int exceptOwn);

/* the GObj's sub-object slot, read as an int, a byte pointer or a Sub15C */
typedef union SubHandle { /* field names derived */
    int i;
    char *p;
    struct Sub15C *sub;
} SubHandle; /* derived name */

/* the GObj's sub-object slot (+0x15C) as a SubHandle: the EE's byte offset, the
   host's field */
#include "ee_view.h"
#define SUBHANDLE_OF(g) ICO_RAWP(SubHandle *, g, 0x15C, (SubHandle *)&((struct GObj *)(g))->dobj)

int GetCylinderCollisionWithExceptOwnCollision(struct GObj *self, struct GObj *target, float r,
                                               float h, float s, float t, int ctrl);

void SetRootMatrixWithTransOffsetByDObj(struct Sub15C *dobj, float x, float y, float z);
void GetRootMatrixRotOffsetByDObj(void *q, struct Sub15C *dobj);
void SetRootMatrixRotOffsetByDObj(struct Sub15C *dobj, void *q);
void GetRootVelocity(float *vel, struct GObj *obj);
void GetInitialInverseMatrixByDObj(char *mat, struct Sub15C *mdl);
void GetInitialInverseMatrix(char *mat, struct GObj *gobj);
void MakeCharGObjList(void);

int GetCylinderCollision(struct GObj *self, struct GObj *target, float r, float h, float s,
                         int ctrl);

void GetRootMatrixByDObj(float *m, struct Sub15C *src);
void GetRootMatrixTransOffsetByDObj(float *dst, struct Sub15C *src);

#endif /* GEOMETRYMANAGER_H */

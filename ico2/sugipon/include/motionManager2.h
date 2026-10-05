/*
 * ico2/sugipon/include/motionManager2.h
 *
 * The declarations of what motionManager2.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef MOTIONMANAGER2_H
#define MOTIONMANAGER2_H

/* One skeleton node's 32-byte motion element, the record CopyMotion,
   CopyMotionWithNodeHrc and the blends copy whole: the node's word at 0
   (250 for a node CopyMotionWithNodeHrc leaves unflagged, blended as a
   number by GetBlendedMotion) and the rotation quaternion at 0x10.  The
   copies move it as four doublewords. */
typedef struct StreamElem { /* field names derived */
    long long d[2];
    float q[4];
} StreamElem; /* derived name */

/* motionManager2.c's other records: the head of a stream motion frame and
   the motion state block every actor starts from */
struct StreamShapeHdr;

#include "typedef.h"

int AdjustMotionHeightToNearestField(GObj *self);
void AdjustRootPositionToVerticalSidePlaneOfWall(void *self, void *wall, float dist);
void AdjustVerticalSidePlaneOfWall(float *out, WallCfg *cfg, float *pos, float t);
int CheckFieldContact(ClipWork *info, GObj *self, float *pos, float lim);
/* the GObj and the attribute mask (act_bird.c passes 0x40 and 0x50;
   boyact, script, a_p_1 and frameDependSequence do the same) */
int CheckFloorAttribute(GObj *self, int attr);
int CheckPureWallAttribute(GObj *self, int attr);
int CheckWallAttribute(GObj *self, int attr);
int CheckPureCliffAttribute(GObj *self, int attr);
void ClearMotionBlendlessNode(GObj *self);
void ClearMotionGeometryInfo(GObj *self);
void CopyMotion(struct StreamElem *dst, struct StreamElem *src, int n);
void DebugDisp1Collision(WallCfg *cfg);
void DebugDisp1CollisionWithColor(WallCfg *cfg, void *color);
void DisableChangeRootUpdateMode(GObj *self);
void DisableMotionOrientUpdate(GObj *self);
void DispSkelton(GObj *self, ICO_WORD_PTR(void *) motion);
void EnableChangeRootUpdateMode(GObj *self);
void EnableMotionOrientUpdate(GObj *self);
void FeedbackWallWorkInfoToBrainSystem(GObj *self);
float ForMotionViewer_GetCurrentAnimationFrame(GObj *self);
int ForMotionViewer_GetCurrentMotion(GObj *self);

void GetBlendedMotion(struct StreamElem *dst, float *root, struct StreamElem *a, float *rootA,
                      struct StreamElem *b, float *rootB, float t, unsigned char *mask, int count);

float GetDifferenceFromLastField(GObj *self, int node);
float GetDifferenceFromLowerField(GObj *self, int node);
float GetDifferenceFromWallLowerPlane(GObj *self, int node);
float GetDifferenceFromWallUpperField(GObj *self, int node);
float GetDifferenceFromWallUpperPlane(GObj *self, int node);
float GetHeightOfFieldPlaneDifference(GObj *a, GObj *b);
/* the floor a 10000-long ray from pos along +y meets (ClipFloor) */
void GetLowerPlaneCollision(ClipWork *w, float *pos);
int GetMotionFrameFlag1(GObj *self);
int GetMotionFrameFlag2(GObj *self);
void GetOrientOfCliffOfGObj(float *dir, GObj *obj);
int GetPureVerticalPlane(void *plane0, void *plane1, float *ptsIn, WallCfg *cfg, int flip);
int GetPureVerticalPlaneOfCurrentPosition(void *plane0, void *plane1, float *ptsIn, WallCfg *cfg,
                                          int flip, float *pos);
void GetRootProjectionPosOfGObj(float *pos, GObj *obj);
int GetSkeltonFocusNode(GObj *self, int focus);
int GetStreamMotion(StreamElem *dst, float *out, char *node, SkelNode *skel);
int GetStreamShapeMotion(float *dst, struct StreamShapeHdr *hdr);
void InitMotionGeoInfo(struct MotRoot *self, float x, float y, float z, float rx, float ry,
                       float rz);
void InitMotionRotElem(int *elem, int count);
void InitMotionStateInfo(struct MotCtrl *self);
void LockForceGroundParent(GObj *gobj);
void SetMotionBlendlessNode(GObj *self, int *node);
void SetMotionDirection(GObj *self, float *dir);
void SetMotionDirectionWithLimit(GObj *self, float *dir, float lim0, float lim1);

void SetMotionNodeFixModeParameter(GObj *self, GObj *obj, int mode, int node, void *quat, float x,
                                   float y, float z, float w);

void SetMotionPlaySpeedRatio(GObj *self, float val);
void SetRootUpdateMode(GObj *self, int val);
void SetSkeltonDispSwitch(int val);
void UnlockForceGroundParent(GObj *gobj);
void _GetMotionDirection(float *dir, GObj *obj);
void _getMotion(void *dst, void *m, int node, int frame);
void _getS16MotRotElem(void *dst, void *src);
void SlopeIKControl(GObj *self, char *mot, float *v, Vec4 *vel, int n);
void CopyMotionWithNodeHrc(struct StreamElem *dst, struct StreamElem *src, SkelNode *hrc, int node,
                           int flag);
void GetFloatingShapeMotion(float *dst, char *m, float t, int count);
void GetFloatingMotionRootPos(float *dst, void *m, float t);
void GetOutOutsideOfWall(GObj *obj, float threshold);
int GetWaterReaction(float *outH, int *outFlag, ClipWork *info, float *pos, float *vel,
                     float h0, float h1, float h2, float scaleIn, float amp);
void dispPlane(Vec4 *plane, float *pos);
void GetOrientOfWallOfGObj(float *dir, GObj *obj);
void GetRootPosOfNextFrame(float *pos, GObj *obj);
void AdjustMotionHeightToField(GObj *obj);
void GetFloatingMotion(struct StreamElem *dst, float t, float *root, void *motion, int count,
                       unsigned char *mask, SkelNode *hrc);
void MakeMirrorMotion(struct StreamElem *a, SkelNode *b);
void *GetMotionPointer(GObj *self);
int GetCollisionOfLastActiveField(GObj *self);
float GetRopeHangablePos(GObj *self);
float GetHeightOfWallFromGObj(GObj *self);
float GetHeightOfCliffFromGObj(GObj *self);
void GetMotionRootPos(float *dst, void *motion, int idx);
void GetMotion(char *dst, float *root, void *motion, int idx, unsigned char *mask, int count,
               SkelNode *hrc);
void GetShapeMotion(float *dst, char *motion, int idx, int count);
void fitYToPlane(long long *src, float *dest);
void GetBlendedMotionRootPos(float *dst, float *a, float *b, float t);
void _getMotRotElem(char *dst, char *src);

#endif /* MOTIONMANAGER2_H */

/*
 * ico2/sugipon/include/quaternion.h
 *
 * The declarations of what quaternion.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef QUATERNION_H
#define QUATERNION_H

/* The declarations below lead this header because their order is load-bearing:
 * gcc 2.9 emits the deferred out-of-line copy of a plain-inline function in
 * first-declaration order, so this is the order quaternion.c's inline tail has. */
float *GetCurrentQuaternion(void);
float *GetLastQuaternion(void);
void PushQuaternionWithNoCopy(void);
void PopQuaternion(void);
void SetQuaternionByAxisRotate(float *self, short ang, float x, float y, float z);
void SetQuaternionByAxisRotateWithNoRegularize(float *self, short ang, float x, float y, float z);
void SetQuaternionByAxisRotateEAngle(float *out, float *in, float x, float y, float z);
void SetQuaternionByAxisRotateV(float *self, short ang, float *src);
void SetQuaternionByAxisRotateVWithNoRegularize(float *self, short ang, float *src);
void MultiQuaternion(void *out, void *qa, void *qb);
void DivQuaternion(void *self, void *qa, void *qb);
void GetMatrixFromQuaternionRotElem(void *mtx, void *q);
void GetMatrixFromQuaternionPos(void *mtx, void *q, void *pos);
void MultiMatrixByQuaternion(void *src);
void GetMirrorQuaternion(float *dst, float *src, int mode);
void RotQuaternionX(void *self, short ang);
void RotQuaternionY(void *self, short ang);
void RotQuaternionZ(void *self, short ang);
void RotQuaternionEAX(void *self, float *in);
void RotQuaternionEAZ(void *self, float *in);
void GetXUnitVectorOfQuaternion(float *out, float *q);
void GetYUnitVectorOfQuaternion(float *out, float *q);
void GetZUnitVectorOfQuaternion(float *out, float *q);
void GetDifferencialQuaternionWithNoRegularize(void *out, void *a, void *b);
float GetQuaternionMagnitude(void *q);
void SetQuaternionByCosineAxisRotateVWithNoRegularize(void *out, void *axis, float angle);
void SetQuaternionByCosineAxisRotateV(void *out, void *axis, float angle);
void SetQuaternionByAxisRotateVEAngle(void *out, float *cosAngle, void *axis);
float GetQuaternionCosRadian(void *qa, void *qb);
void CopyQuaternion(void *dst, void *src);
void GetInverseQuaternion(void *dst, void *src);
void GetMatrixFromQuaternion(void *mtx, void *q);
/* GetSlerpQuaternion passes its arguments on to
 * GetSlerpQuaternionNoRegularize and regularizes the result; its callers
 * pass four. */
void GetSlerpQuaternion(void *out, void *qa, void *qb, float t);

void GetSlerpQuaternionNoRegularize(void *out, void *qa, void *qb, float t);

extern float IdentityQuaternion[4];
void InitQuaternionDrive(void);
void PushQuaternion(void);
void RegularizeQuaternion(void *q);
void SetCurrentQuaternion(float *q);
void SetIdentityQuaternion(void *q);
void MultiCurrentQuaternion(void *src);
void RotCurrentQuaternionX(short ang);
void RotCurrentQuaternionY(short ang);
void RotCurrentQuaternionZ(short ang);
void InvertCurrentQuaternion(void);
void GetQuaternionFromMatrix(void *q, void *mtx);

#endif /* QUATERNION_H */

/*
 * ico2/omori/include/camera-ico2.h
 *
 * The declarations of what camera-ico2.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef CAMERA_ICO2_H
#define CAMERA_ICO2_H

extern int current_group;
/* the camera data file of each camera set (the camera-set data member) */
extern char cameraSetList[][32];

struct CamGroup;

struct CamSetFile;

void GetHandCameraStickInfo(float *outX, float *outZ, float *outMag);

void SetCameraZoomOffsetRatio(float val);
int GetCameraGroupCurrent(void);
int GetCameraGroupFromGObj(void *obj);
int GetCameraGroupFromPosition(float *pos);
void AddPluralCameraSet(int id, char *name);
void InitPluralCameraSet(void);
inline void *GetPluralCameraSet(int id);
void MakeCameraSetBinary(struct CamGroup *src, int count, struct CamGroup *dst);
int GetSizeOfCameraSetBinary(struct CamGroup *p, int n);
void SetCameraTargetPosition(void *target, void *eye, float fov);
void CameraSetCameraSet(int id);
void CameraSetCameraSet_Default(void);
void InitIco2Camera(void);
void ReflectCameraSetBinary(struct CamGroup *src, int count);
void SetCameraMatrix_Ico2(int flag);

#endif /* CAMERA_ICO2_H */

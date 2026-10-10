/*
 * ico2/seki/include/GsBase.h
 *
 * The declarations of what GsBase.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef GSBASE_H
#define GSBASE_H

/* GsBase.c's globals */
extern int currentFocusDistance;
extern int fbKeep;
extern int fbClear;
extern float center_X;
extern float center_Y;
extern int ScreenWidth;
extern int ScreenHeight;
extern int currentScreenWidth;
extern int currentScreenHeight;
extern int screenOffsetX;
extern int screenOffsetY;
extern int vsWidth;
extern int vsHeight;
/* GsBase.c's `inline` functions, in the order of their definitions'
 * out-of-line copies at the end of the object (first-declaration order). */
void gsb_SetBGColor(void *db, int r, int g, int b);
void gsb_GetBGColor(unsigned char *col);
void gsb_ResetFilmNoise(void);
void gsb_SetZoom(float target, float speed);
int gsb_SyncGSSystem(void);
int gsb_LoadStageSettings(void);
int gsb_SaveStageSettings(void);
void gsb_ClearFrameBuffer(void);
int gsb_ResetSnap(void);
int gsb_TakeSnap(void);
int lockOtherEditing(void);
int unlockOtherEditing(void);
int gsb_ClipBox(float *p);
void gsb_Init(void *db);
void gsb_InitGSSystem(void);
void gsb_MakeCommonMatrix(void);
void gsb_Reduction(void);
void gsb_ResetGSSystem(void);
void gsb_SetMotionBlur(void);
void gsb_SetVSMatrix(int w, int h, float d);
void gsb_UpdateGSSystem(int keep);
int gsb_StageSetting(void);
/* PC port (photo mode, issue 14): the camera saved and put back, and the
   distance of the last full-screen gsb_SetVSMatrix (GsBase.c lists what the
   save holds) */
void gsb_PushView(void);
void gsb_PopView(void);
float gsb_ViewFocus(void);
/* PC port (issue 29): the cull against the cameras of the blended
   pictures.  RegistPacket.c says whether the part it culls next is locked
   to the camera (node flag 2: culled against this tick's camera only);
   gsb_HostCullViewsUsed is how many cameras the last gsb_ClipBox tested
   (1, 2 or 3; the tests read it).  The windows that scale with the
   picture's width: the water dots' (waterDot.c; ip the dot's GS position
   in 1/16 pixels, pos its world position), the lines' x clip
   (lineManager.c) and the shadow volumes' edge clip (Shadow.c). */
void gsb_HostCullCameraLocked(int on);
int gsb_HostCullViewsUsed(void);
int gsb_HostDotVisible(const int *ip, const float *pos);
float gsb_HostLineHalfWidth(void);
float gsb_HostShadowClipX(void);

#endif /* GSBASE_H */

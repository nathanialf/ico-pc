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

#endif /* GSBASE_H */

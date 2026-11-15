/*
 * ico2/seki/include/GsBase.h
 *
 * The declarations of what GsBase.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef GSBASE_H
#define GSBASE_H

/* PC port: the native frame's type (IcoFrame) */
#include "../../../port/include/ico_frame.h"
/* PC port: the shadow volumes' clip box by disc (ico_shadow_clip_x/_y) */
#include "../../../port/include/ico_shadow_clip.h"

/* GsBase.c's globals */
extern int currentFocusDistance;
extern int fbKeep;
extern int fbClear;
extern float center_X;
extern float center_Y;
extern int ScreenWidth; /* SCALE / UI / ADDRESS only: GsBase.c's rule */
extern int ScreenHeight;
extern int currentScreenWidth;
extern int currentScreenHeight;
extern int screenOffsetX;
extern int screenOffsetY;
extern int vsWidth;
extern int vsHeight;
/* PC port: the native frame this tick's matrices and frame extents are
   built for (port/include/ico_frame.h), latched at each flip from the
   display options (GsBase.c gsbHostFrameLatch); 512 x ScreenHeight at 4:3 */
extern IcoFrame gsFrame;
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
/* PC port (photo mode): the camera saved and put back, and the
   distance of the last full-screen gsb_SetVSMatrix (GsBase.c lists what the
   save holds) */
void gsb_PushView(void);
void gsb_PopView(void);
float gsb_ViewFocus(void);
/* PC port: the cull against the cameras of the blended
   pictures.  RegistPacket.c says whether the part it culls next is locked
   to the camera (node flag 2: culled against this tick's camera only);
   gsb_HostCullViewsUsed is how many cameras the last gsb_ClipBox tested
   (1, 2 or 3; 0 when a step past half the presenter's limits kept every
   box; the tests and F12's cull line read it).  The windows sized by the native
   frame's width (gsFrame.w): the water dots' (waterDot.c; ip the dot's GS position
   in 1/16 pixels, pos its world position), the lines' x clip
   (lineManager.c) and the shadow volumes' edge clip (Shadow.c). */
void gsb_HostCullCameraLocked(int on);
int gsb_HostCullViewsUsed(void);

/* why the last gsb_ClipBox tested this tick's camera alone
   (gsb_HostCullViewsUsed 1), for F12's cull line: GSB_CULL_ONE_NOT when it
   tested more (or kept every box) */
enum {
    GSB_CULL_ONE_NOT = 0,
    GSB_CULL_ONE_ORIGINAL, /* the Original framerate: nothing blended */
    GSB_CULL_ONE_FIRST,    /* a stage's first tick: no earlier camera */
    GSB_CULL_ONE_CUT,      /* a cut the game signalled since the flip */
    GSB_CULL_ONE_STILL,    /* the camera the tick before's, bitwise */
    GSB_CULL_ONE_JUMP,     /* a step past the presenter's limits: the pictures snap */
    GSB_CULL_ONE_LOCKED,   /* a part locked to the camera (node flag 2) */
    GSB_CULL_ONE_SINGULAR  /* this tick's matrix has no inverse */
};

int gsb_HostCullOneWhy(void);
int gsb_HostDotVisible(const int *ip, const float *pos);
float gsb_HostLineHalfWidth(void);
float gsb_HostShadowClipX(void);
float gsb_HostShadowClipY(void);

/* the largest edge clip gsb_HostShadowClipX gives (GsBase.c: a clipped end
   at GS x 2 or 4094 passes Shadow.c's range test) */
#define GSB_SHADOW_CLIP_MAX ICO_SHADOW_CLIP_X_MAX
/* gsb_ClipBox drops a box only when every corner is past a plane by
   |w| GSB_DROP_REL + GSB_DROP_ABS (GsBase.c gsbHostDropFlags) */
#define GSB_DROP_REL (1.0f / 65536.0f)
#define GSB_DROP_ABS 1.0f
#endif /* GSBASE_H */

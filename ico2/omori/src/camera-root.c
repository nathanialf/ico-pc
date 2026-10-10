#include "camera-root.h"
#include "debug.h"
#include "gobj.h"
#include "boyact.h"
#include "camera-editor.h"
#include "GsBase.h"
#include "geometryManager.h"
#include "matrixDrive.h"
#include "tableSin.h"
#include <math.h>
#include <string.h>
#include <libvu0.h>
#include "hand-camera.h"
#include "BgAnimation.h"
#include "act-game.h"
#include "main.h"
#include "gv.h"
#include "commonact.h"
#include "pad.h"
#include "camera-ico2.h"
#include "poly-flat.h"

/* PC port (renderer R7b): the hard
   camera cuts, for the presenter's interpolation (port/game/video_options.c;
   a counter no game state reads) */
extern void ico_video_camera_cut(void);

#define CAM_HOST_CUT_IF(c) ((c) ? ico_video_camera_cut() : (void)0)

static int InsertCamera_isEnable(void);

union PendCopy { /* field names derived */
    float f[8];
    long long q[4];
};

/* the camera position and the point it looks at, the pair InitCamera seeds
   both camera work areas with; the halves are named after the same pair in
   InsertCameraWork */
typedef struct { /* field names derived */
    float pos[4];
    float tgt[4];
} CamTgt __attribute__((aligned(16)));

/* The camera set the whole TU works on: position (0x00) plus the three
   fixed-point angles MatrixDrive rotates by (0x10/0x12/0x1C).  The word at
   0x18 is the semi-auto move flag DebugCameraSemiAuto runs the camera to its
   target with, cleared on every mode change. */
typedef struct CameraSet2 { /* field names derived */
    float pos[3];           /* 0x00 */
    char pad0c[4];
    short rotX; /* 0x10 */
    short rotY; /* 0x12 */
    float fov;  /* 0x14 */
    int moving; /* 0x18 */
    short rotZ; /* 0x1C */
} CameraSet2;   /* derived name */

/* What SetWSMatrix / DebugCameraSemiAuto hand in: eye (0x00) and look-at
   (0x10) points plus the field of view at 0x20, copied as doublewords. */
union CameraSetIn { /* field names derived */
    float f[12];
    long long q[6];
};

/* The camera target request Camctrl_SetTarget files and Camctrl_Exec hands to
   CameraSetTargetGObj once a frame: the object the camera follows, its sub
   object, the priority the request was filed at and the priority that ran the
   frame before. */
typedef struct CamCtrl { /* field names derived */
    GObj *gobj;          /* 0x00 */
    GObj *subGObj;       /* 0x04 */
    int pri;             /* 0x08 */
    int lastPri;         /* 0x0C */
} CamCtrl;               /* derived name */

typedef struct InsertCameraWork { /* field names derived */
    int frames;                   /* 0x00, how long the insert camera runs */
    int count;                    /* 0x04, the frames it has run */
    char pad08[8];
    float pos[3]; /* 0x10 */
    char pad1c[4];
    float tgt[3]; /* 0x20 */
    char pad2c[4];
    float blend;           /* 0x30 */
    unsigned char enable;  /* 0x34 */
    unsigned char cut;     /* 0x35 */
    unsigned char cutType; /* 0x36 */
    unsigned char zoom;    /* 0x37, the zoom request while it runs */
    unsigned char cutBack; /* 0x38, cut back to the game camera at the end */
    char pad39[7];
    /* a VU0 quadword record: pos and tgt are quadword vectors */
} InsertCameraWork __attribute__((aligned(16)));

/* The camera set the default mode interpolates away from, the live camera,
   the set the semi-auto camera is running to, the target request and the
   insert-camera request. */
static union CameraSetIn prevCameraSet; /* derived name */

#include "ee_view.h"

/* PC port: InitCamera and CameraSetTargetGObj copy a CamTgt (pos over pos and
   the pad, tgt over rotX..rotZ) whole over these two, and CamTgt is 16-byte
   aligned: the host compiler moves it with aligned SSE loads and stores, so
   the two sets carry that alignment (the EE's quadword copy did not fault on
   a 4-byte aligned set; a host one would).  The layouts must agree
   (tools/template_audit.py). */
static CameraSet2 cameraSet __attribute__((aligned(16)));       /* derived name */
static CameraSet2 targetCameraSet __attribute__((aligned(16))); /* derived name */

ICO_LAYOUT_AT(CamTgt, pos, CameraSet2, pos);

ICO_LAYOUT_AT(CamTgt, tgt, CameraSet2, rotX);

ICO_LAYOUT_SIZE(CamTgt, CameraSet2);

_Static_assert(__alignof__(cameraSet) >= __alignof__(CamTgt) &&
                   __alignof__(targetCameraSet) >= __alignof__(CamTgt),
               "the camera sets are not aligned for CamTgt's copy");

static CamCtrl camctrl; /* derived name */

static InsertCameraWork insertCamera; /* derived name */

/* The two ends of the zoom range, the target object and its sub object, the
   two cut-back requests, the zoom blend ratio, the camera mode, the zoom
   distance and field of view, the lws cut-back request and the four demo
   limits; handCameraLimitP and handCameraLimitV take the P and V of their
   own exported setters. */
static int zoomRangeMin; /* derived name */

static int zoomRangeMax; /* derived name */

static GObj *cameraTargetGObj; /* derived name */

static GObj *cameraTargetSubGObj; /* derived name */

static unsigned char gamecamCutBack; /* derived name */

static unsigned char zoomRequest; /* derived name */

static float zoomBlend; /* derived name */

static int cameraMode; /* derived name */

static float cameraZoom; /* derived name */

static float cameraFov; /* derived name */

static unsigned char lwsCutBack; /* derived name */

static int handCameraLimitP; /* derived name */

static int handCameraLimitV; /* derived name */

static int zoomMaxInDemo; /* derived name */

static int zoomBase; /* derived name */

void ConvertCameraSet(CameraSet2 *dst, union CameraSetIn *src);

void SetWSMatrix(void *src)
{
    ConvertCameraSet(&cameraSet, src);
    MakeCameraMatrix(&cameraSet);
}

void ConvertCameraSet(CameraSet2 *dst, union CameraSetIn *src)
{
    union CameraSetIn in;
    float dir[4];
    in = *src;
    dst->pos[0] = in.f[0];
    dst->pos[1] = in.f[1];
    dst->pos[2] = in.f[2];
    memset(dir, 0, 16);
    dir[3] = 1.0f;
    sceVu0SubVector(dir, &in.f[4], &in.f[0]);
    FSqrt(dir[0] * dir[0] + dir[2] * dir[2]);
    dst->rotY = atan2f(dir[0], dir[2]) * 32768.0f / 3.14159265f;
    dst->rotX = atan2f(dir[1], FSqrt(dir[0] * dir[0] + dir[2] * dir[2])) * -32768.0f / 3.14159265f;
    dst->fov = in.f[8];
    dst->rotZ = 0;
}

static void MakeMatrixFromCameraSet2(void *dst, CameraSet2 *cs)
{
    MatrixDrive_PushMatrix();
    sceVu0UnitMatrix(MatrixDrive_GetMatrix());
    MatrixDrive_RotMatrixY(cs->rotY);
    MatrixDrive_RotMatrixX(cs->rotX);
    MatrixDrive_RotMatrixZ(cs->rotZ);
    sceVu0TransposeMatrix(MatrixDrive_GetMatrix(), MatrixDrive_GetMatrix());
    MatrixDrive_TransMatrix(-cs->pos[0], -cs->pos[1], -cs->pos[2]);
    CopyMatrix(dst, MatrixDrive_GetMatrix());
    MatrixDrive_PopMatrix();
}

void MakeCameraMatrix(CameraSet2 *cs)
{
    float mat[16];
    MakeMatrixFromCameraSet2(mat, cs);
    MatrixDrive_PushMatrix();
    CopyMatrix(MatrixDrive_GetMatrix(), mat);
    gsb_SetVSMatrix(ScreenWidth, ScreenHeight,
                    GetTableCos((short)(int)(cs->fov * 32768.0f / 180.0f)) * 1024.0f /
                        GetTableSin((short)(int)(cs->fov * 32768.0f / 180.0f)));
    sceVu0CopyMatrix(matrixptr + 0x80, MatrixDrive_GetMatrix());
    MatrixDrive_PopMatrix();
    gsb_MakeCommonMatrix();
}

/* the manual camera's speed, 1 to 4 on the pad's buttons */
static int manualCameraSpeed = 2; /* derived name */

static void CameraEditManual(CameraSet2 *set, int noLock)
{
    float mz = 0.0f, mx = 0.0f;
    int d;
    int t;
    float v[4];
    float out[4];

    if (pad[1].flags & 0x1000) {
        manualCameraSpeed = 1;
    }
    if (pad[1].flags & 0x2000) {
        manualCameraSpeed = 2;
    }
    if (pad[1].flags & 0x4000) {
        manualCameraSpeed = 3;
    }
    if (pad[1].flags & 0x8000) {
        manualCameraSpeed = 4;
    }

    d = 128 - pad[1].ana[1];
    if ((d < 0 ? -d : d) < 50) {
        d = 0;
    }
    if (pad[1].now & 2) {
        if ((d < 0 ? -d : d) >= 51) {
            if (d < 50) {
                t = (d + 50) * 10;
                set->pos[1] -= (float)(manualCameraSpeed * t) / 78.0f;
            }
            if (d >= 51) {
                t = (d - 50) * 10;
                set->pos[1] -= (float)(manualCameraSpeed * t) / 78.0f;
            }
        }
    } else {
        if ((d < 0 ? -d : d) >= 51) {
            if (d < 50) {
                set->rotX -= (d + 50) * (d + 50) * 5 / 78;
            }
            if (d >= 51) {
                set->rotX += (d - 50) * (d - 50) * 5 / 78;
            }
        }
    }

    d = 128 - pad[1].ana[0];
    if ((d < 0 ? -d : d) < 50) {
        d = 0;
    }
    if ((d < 0 ? -d : d) >= 51) {
        if (d < 50) {
            set->rotY += (d + 50) * (d + 50) * 5 / 78;
        }
        if (d >= 51) {
            set->rotY -= (d - 50) * (d - 50) * 5 / 78;
        }
    }

    d = 128 - pad[1].ana[3];
    if ((d < 0 ? -d : d) < 50) {
        d = 0;
    }
    if ((d < 0 ? -d : d) >= 51) {
        if (noLock || (pad[0].now & 1) == 0) {
            if (d < 50) {
                t = (d + 50) * 10;
                mz = (float)(manualCameraSpeed * t) / 78.0f;
            }
            if (d >= 51) {
                t = (d - 50) * 10;
                mz = (float)(manualCameraSpeed * t) / 78.0f;
            }
        }
    }

    d = 128 - pad[1].ana[2];
    if ((d < 0 ? -d : d) < 50) {
        d = 0;
    }
    if ((d < 0 ? -d : d) >= 51) {
        if ((pad[0].now & 0x200) == 0) {
            if (d < 50) {
                t = (d + 50) * 10;
                mx = (float)(manualCameraSpeed * t) / 78.0f;
            }
            if (d >= 51) {
                t = (d - 50) * 10;
                mx = (float)(manualCameraSpeed * t) / 78.0f;
            }
        }
    }

    MatrixDrive_PushMatrix();
    v[0] = mx;
    v[1] = 0.0f;
    v[2] = mz;
    v[3] = 0.0f;
    sceVu0UnitMatrix(MatrixDrive_GetMatrix());
    MatrixDrive_RotMatrixY(-set->rotY);
    sceVu0ApplyMatrix(out, MatrixDrive_GetMatrix(), v);
    set->pos[0] -= out[0];
    set->pos[2] += out[2];
    MatrixDrive_PopMatrix();
}

static void DebugCameraManual(void)
{
    CameraEditManual(&cameraSet, 0);
    MakeCameraMatrix(&cameraSet);
}

static void DebugCameraSemiAuto(void)
{
    if (targetCameraSet.moving != 0) {
        if (_MoveGV(cameraSet.pos, cameraSet.pos, targetCameraSet.pos, 50.0f) < 1.0f) {
            targetCameraSet.moving = 0;
        }
    } else {
        union CameraSetIn buf;
        CameraEditManual(&cameraSet, 1);
        buf.f[0] = cameraSet.pos[0];
        buf.f[1] = cameraSet.pos[1];
        buf.f[2] = cameraSet.pos[2];
        buf.f[8] = cameraSet.fov;
        GetRootPosition(&buf.f[4], cameraTargetGObj);
        ConvertCameraSet(&cameraSet, &buf);
    }
    MakeCameraMatrix(&cameraSet);
}

static void BackToGameCamera(void)
{
    struct {             /* field names derived */
        float target[4]; /* the point the camera looks at */
        float eye[4];    /* the camera position */
        float m[16];     /* the view rotation, transposed */
        float root[4];   /* the target object's root */
        float trans[4];  /* the view translation */
    } buf;

    float f20v;
    memset(buf.target, 0, 16);
    buf.target[2] = 1.0f;
    sceVu0TransposeMatrix(buf.m, matrixptr + 0x80);
    CopyVector(buf.trans, matrixptr + 0xB0);
    buf.trans[3] = 0.0f;
    sceVu0ApplyMatrix(buf.eye, buf.m, buf.trans);
    sceVu0ScaleVector(buf.eye, buf.eye, -1.0f);
    GetRootPosition(buf.root, default_cameratarget_gobj);
    f20v = _DistGV(buf.eye, buf.root);
    buf.target[3] = 0.0f;
    sceVu0ApplyMatrix(buf.target, buf.m, buf.target);
    sceVu0ScaleVector(buf.target, buf.target, f20v);
    sceVu0AddVector(buf.target, buf.target, buf.eye);
    SetCameraTargetPosition(buf.target, buf.eye, cameraFov);
}

void GetCameraInfomationFromGlobalPosition(float *pos, float *outDist, int *outAngle, float *fov,
                                           float *zoom)
{
    *fov = cameraSet.fov;
    *zoom = (float)debug_zoom_per / 100.0f;
    CameraGetOtherObjOffset(pos, outDist, outAngle);
}

/* GetCameraDefaultTargetGObj's body, for InitCamera and the camera step */
static inline GObj *getCameraDefaultTargetGObj(void) /* derived name */
{
    int id = GetEfStageCameraTargetID();
    if (id != 0) {
        GObj *gobj = isysGObjSearchFromObjLayoutID(id);
        if (gobj != 0) {
            return gobj;
        }
    }
    return boyGObj;
}

/* hand the camera to gobj at no priority, for Camctrl_ExitEveRock and
   InitCamera */
static inline void Camctrl_ForceTarget(GObj *gobj) /* derived name */
{
    camctrl.pri = 0;
    camctrl.gobj = gobj;
    camctrl.subGObj = 0;
}

static inline void Camctrl_Init(GObj *gobj) /* derived name */
{
    camctrl.lastPri = 0;
    Camctrl_ForceTarget(gobj);
}

/* the pair InitCamera starts every stage from: the camera one metre up,
   looking at a point just over half a metre up */
static CamTgt cameraTargetDefault =
    /* derived name */ {{0.0f, 100.0f, 0.0f, 0.0f}, {0.0f, 52.0f, 0.0f, 0.0f}};

/* the cleared insert-camera request: no target, no blend, one cut pending */
static InsertCameraWork insertCameraClear = {
    /* derived name */
    0, 0, {0}, {0.0f, 0.0f, 0.0f}, {0}, {0.0f, 0.0f, 0.0f}, {0}, -1.0f, 0, 1, 0, 0, 1, {0}};

/* clear the insert-camera request */
static inline void InsertCamera_Clear(void) /* derived name */
{
    insertCamera = insertCameraClear;
}

int CameraCalclated_f;

GObj *default_cameratarget_gobj;

int InsertCameraWorkingFlag;

int FixViewInGameCameraFlag;

int monitorCameraHold; /* derived name */

int insertCameraBlendTimer; /* derived name */

void InitCamera(void)
{
    GObj *gobj = getCameraDefaultTargetGObj();
    InsertCamera_Clear();
    CAM_HOST_CUT_IF(1); /* port (R7b): a stage's first camera */
    default_cameratarget_gobj = gobj;
    cameraMode = 3;
    *(CamTgt *)&targetCameraSet = *(CamTgt *)&cameraSet = cameraTargetDefault;
    Camctrl_Init(gobj);
    InitIco2Camera();
    InitCameraEditor();
    targetCameraSet.moving = 0;
    gamecamCutBack = 0;
    zoomRequest = 0;
    lwsCutBack = 0;
    CameraCalclated_f = 0;
    FixViewInGameCameraFlag = 0;
    InsertCameraWorkingFlag = 0;
    monitorCameraHold = 0;
    debug_zoom_per = 100;
    handCameraLimitP = GlobalStageSetting.handCameraLimitP;
    handCameraLimitV = GlobalStageSetting.handCameraLimitV;
    zoomMaxInDemo = GlobalStageSetting.zoomMaxInDemo;
}

typedef struct { /* field names derived */
    int step;    /* 0x00 */
    int max;     /* 0x04 */
} CamZoomStep;   /* derived name */

/* one quadword copied whole out of the const table */
union CamQuad { /* field names derived */
    float f[4];
    long long q[2];
};

/* one step of the camera target queue */
static inline void Camctrl_Exec(void) /* derived name */
{
    float pos[4];
    int last;

    if (camctrl.pri != 0) {
        insertCameraBlendTimer = 0;
    }
    if (insertCameraBlendTimer != 0) {
        insertCameraBlendTimer = insertCameraBlendTimer - 1;
    }
    if ((last = camctrl.lastPri) == 1 && camctrl.pri == 0 && default_cameratarget_gobj != 0 &&
        IsPointIsInScreen(pos, test_CURRENTROOT(default_cameratarget_gobj)) < 0.0f) {
        insertCameraBlendTimer = (60 - systemStatus[0] * 10) / systemStatus[1];
        InsertCameraWorkingFlag = last;
    }
    CameraSetTargetGObj(camctrl.gobj, camctrl.subGObj);
    camctrl.lastPri = camctrl.pri;
}

/* release the target once its priority drops below the demo level;
   Camctrl_ExitEveRock is the same shape with 4 */
static inline void Camctrl_ExitNormal(void) /* derived name */
{
    if (camctrl.pri < 3) {
        Camctrl_ForceTarget(default_cameratarget_gobj);
    }
}

/* InsertCamera_isEnable's body, for the camera step below */
static inline unsigned char insertCamera_isEnable(void) /* derived name */
{
    if (camctrl.pri < 2) {
        return 1;
    }
    return 0;
}

/* one step of the insert camera */
static inline void InsertCamera_Step(void) /* derived name */
{
    if (insertCamera_isEnable() == 0) {
        insertCamera.enable = 0;
    }
    if (insertCamera.enable != 0) {
        if (insertCamera.count < insertCamera.frames) {
            insertCamera.count = insertCamera.count + 1;
            zoomRequest = insertCamera.zoom;
            zoomBlend = insertCamera.blend;
        } else {
            insertCamera.enable = 0;
            gamecamCutBack = insertCamera.cutBack;
        }
    }
}

/* CameraSetMode's body: set the mode and stop the target set's move */
static inline void cameraSetMode(int mode) /* derived name */
{
    cameraMode = mode;
    targetCameraSet.moving = 0;
}

#define CAM_ABS(x) ((x) < 0 ? -(x) : (x))

/* set when the monitor camera must start over */
static int monitorCameraInit = 0; /* derived name */

void SetCameraMatrix(GObj *self)
{
    float m[16];
    GObj *gobj;
    GObj *root;
    int useDemo;
    int zoomMax;
    int target;
    int step;
    IosPadCtx *p;
    float zoom;

    useDemo = 0;
    gobj = getCameraDefaultTargetGObj();
    default_cameratarget_gobj = gobj;
    CameraCalclated_f = 1;
    InsertCameraWorkingFlag = 0;
    FixViewInGameCameraFlag = 0;
    zoomBlend = -1.0f;
    Camctrl_Exec();
    cameraZoom = 0.0f;
    if (bga_GetCameraMatrix(m) != 0) {
        cameraMode = 4;
        cameraZoom = bga_GetZoom();
    } else if (cameraMode == 4) {
        cameraSetMode(3);
        BackToGameCamera();
        bga_ResetCamera();
        gamecamCutBack = lwsCutBack;
        lwsCutBack = 0;
    }
    InsertCamera_Step();
    switch (cameraMode) {
    case 0:
        break;
    case 2:
        DebugCameraSemiAuto();
        if (debug_font_flag3 != 0 || (debug_font_flag & 1) != 0) {
            debug_Printf(220, 30, 0xFFFFFF00, "FREECAM");
        }
        if ((pad[0].now & 2) != 0 && (pad[0].flags & 0x100) != 0) {
            cameraSetMode(3);
        }
        break;
    case 3:
        if (debug_ignore_demo_camera != 0) {
            goto handCamera;
        }
        CAM_HOST_CUT_IF(gamecamCutBack != 0); /* port (R7b): the cut back to the game camera */
        SetCameraMatrix_Ico2(gamecamCutBack);
        gamecamCutBack = 0;
        if (debug_font_flag3 != 0 || (debug_font_flag & 1) != 0) {
            debug_Printf(220, 30, 0xFFFFFF00, "GAMECAM");
        }
        break;
    case 1:
    handCamera:
        DebugCameraManual();
        if (debug_font_flag3 != 0 || (debug_font_flag & 1) != 0) {
            debug_Printf(220, 30, 0xFFFFFF00, "HANDCAM");
        }
        if ((pad[0].flags & 0x100) != 0) {
            cameraSetMode(3);
        }
        break;
    case 4: {
        float ofs[4];
        float mt[16];
        float pos[4];
        float eye[4];

        if (debug_ignore_demo_camera != 0) {
            goto handCamera;
        }
        SetLimitHandCameraCorrect((float)handCameraLimitP, (float)handCameraLimitV);
        if (debug_font_flag3 != 0 || (debug_font_flag & 1) != 0) {
            debug_Printf(220, 30, 0xFFFFFF00, "PATHCAM");
        }
        if (debug_font_flag3 != 0 || (debug_font_flag & 1) != 0) {
            debug_Printf(310, 30, 0xFFFFFF00, "%d,%d,%d %d", (int)m[12], (int)m[13], (int)m[14],
                         (int)cameraZoom);
        }
        sceVu0TransposeMatrix(mt, m);
        CopyVector(ofs, &m[12]);
        ofs[3] = 0.0f;
        sceVu0ApplyMatrix(cameraSet.pos, mt, ofs);
        sceVu0ScaleVector(cameraSet.pos, cameraSet.pos, -1.0f);
        useDemo = debug_hand_camera != 0;
        if (debug_hand_camera != 0) {
            if (handCameraLimitP != 0 || handCameraLimitV != 0) {
                union CameraSetIn in;
                CameraSet2 set;
                float stickX;
                float stickZ;
                float stickMag;

                MatrixDrive_PushMatrix();
                *(union CamQuad *)eye = (union CamQuad){{0.0f, 0.0f, 1000.0f, 1.0f}};
                CopyMatrix(MatrixDrive_GetMatrix(), m);
                MatrixDrive_SetTransposeMatrix(MatrixDrive_GetMatrix(), MatrixDrive_GetMatrix());
                eye[3] = 0.0f;
                sceVu0ApplyMatrix(eye, MatrixDrive_GetMatrix(), eye);
                sceVu0AddVector(pos, cameraSet.pos, eye);
                MatrixDrive_PopMatrix();
                GetHandCameraStickInfo(&stickX, &stickZ, &stickMag);
                if (GlobalTimer != 0) {
                    ClearHandCameraCorrect();
                    debug_zoom_per = zoomBase;
                }
                HandCameraCorrect(cameraSet.pos, pos, 1, stickX, stickZ,
                                  60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]));
                in.f[0] = cameraSet.pos[0];
                in.f[1] = cameraSet.pos[1];
                in.f[2] = cameraSet.pos[2];
                in.f[4] = pos[0];
                in.f[5] = pos[1];
                in.f[6] = pos[2];
                ConvertCameraSet(&set, &in);
                MakeMatrixFromCameraSet2(m, &set);
            }
        }
        zoom = cameraZoom;
        if (zoom == 0.0f) {
            if (zoom == 0.0f) {
                zoom = GetTableCos((short)(int)(cameraSet.fov * 32768.0f / 180.0f)) * 1024.0f /
                       GetTableSin((short)(int)(cameraSet.fov * 32768.0f / 180.0f));
            }
            cameraZoom = zoom;
            cameraFov = cameraSet.fov;
        } else {
            cameraFov = atan2f(1024.0f / zoom, 1.0f) * 180.0f / 3.14159265f;
        }
        gsb_SetVSMatrix(ScreenWidth, ScreenHeight, cameraZoom);
        sceVu0CopyMatrix(matrixptr + 0x80, m);
        gsb_MakeCommonMatrix();
        break;
    }
    default: {
        union CameraSetIn in;
        float eye[4];
        float rootPos[4];
        union CamQuad ofs;
        float ry;

        root = default_cameratarget_gobj;
        GetRootPosition(rootPos, root);
        ofs = (union CamQuad){{0.0f, -200.0f, -500.0f, 0.0f}};
        ry = (float)(int)(_GetDirection(test_CURRENTORIENT(root)) / 3.14159265f * 180.0f) *
             3.14159265f / 180.0f;
        _ApplyRyGV(ofs.f, ry);
        sceVu0AddVector(eye, rootPos, ofs.f);
        _InterGV(&in.f[4], rootPos, &prevCameraSet.f[4], 10.0f, 1.0f);
        _InterGV(in.f, eye, prevCameraSet.f, 48.0f, 1.0f);
        in.f[8] = 50.0f;
        SetWSMatrix(&in);
        prevCameraSet = in;
        break;
    }
    }
    Camctrl_ExitNormal();
    {
        static unsigned char zoomBaseInit = 1; /* derived name */
        CamZoomStep zp[3] = {
            {10, 200}, {10, 200}, {(int)_ACTGame_GetParamF(12), (int)_ACTGame_GetParamF(11)}};
        IosPadCtx padCtx;
        GObj *ply;

        if (useDemo != 0) {
            zoomMax = zoomMaxInDemo;
        } else {
            zoomMax = zp[0].max;
        }
        iosPadConnect(&padCtx, 0, 0, &iosPadConfCustom);
        if (zoomBaseInit != 0) {
            zoomBaseInit = 0;
            zoomBase = debug_zoom_per;
        }
        iosPadRead(&padCtx);
        ply = boyGObj;
        if (ply != 0 && useDemo == 0) {
            p = &GOBJ_ACT(ply)->pad;
        } else {
            p = &padCtx;
        }
        if (zoomRequest != 0) {
            step = 2;
        } else if (p->now & 2) {
            step = 1;
        } else {
            step = 0;
        }
        zoomRangeMin = zoomBase;
        zoomRangeMax = zoomBase + zoomMax;
        if (zoomRequest == 2) {
            zoomRangeMax = ico_d2i(ico_dmul(ico_i2d(zoomBase), ICO_D(0.75)));
        }
        target = step != 0 ? zoomRangeMax : zoomBase;
        zoomRequest = 0;
        if (zoomBlend < 0.0f) {
            if (CAM_ABS(debug_zoom_per - target) < zp[step].step * 5) {
                debug_zoom_per = ((float)debug_zoom_per * 4.0f + (float)target) / 5.0f;
            } else if (target < debug_zoom_per) {
                debug_zoom_per = debug_zoom_per - zp[step].step;
            } else if (debug_zoom_per < target) {
                debug_zoom_per = debug_zoom_per + zp[step].step;
            }
        } else {
            debug_zoom_per = (float)debug_zoom_per * (1.0f - zoomBlend) + (float)target * zoomBlend;
        }
        /* PC port: the range is empty (max == min) on some stages; the EE's
           div.s gives +-Fmax there, IEEE gives Inf or NaN */
        SetCameraZoomOffsetRatio(1.0f - ps2_div((float)(debug_zoom_per - zoomRangeMin),
                                                (float)(zoomRangeMax - zoomRangeMin)));
    }
    /* the mode the last frame ran in */
    {
        static int lastCameraMode = 3; /* derived name */

        GlobalTimer = 0;
        if (lastCameraMode != cameraMode || monitorCameraInit != 0) {
            GlobalTimer = 1;
            CAM_HOST_CUT_IF(1); /* port (R7b): a camera mode change (path camera in or out) */
        }
        lastCameraMode = cameraMode;
        monitorCameraInit = 0;
    }
}

void Camctrl_ExitEveRock(void)
{
    if (camctrl.pri < 4) {
        Camctrl_ForceTarget(default_cameratarget_gobj);
    }
}

void Camctrl_SetTarget(GObj *gobj, GObj *subGObj, int pri)
{
    if (pri < camctrl.pri) {
        return;
    }
    camctrl.gobj = gobj;
    camctrl.subGObj = subGObj;
    camctrl.pri = pri;
}

/* the object the camera follows by default: the stage's camera target, or
   the boy */
GObj *GetCameraDefaultTargetGObj(void)
{
    int id = GetEfStageCameraTargetID();
    if (id != 0) {
        GObj *gobj = isysGObjSearchFromObjLayoutID(id);
        if (gobj != 0) {
            return gobj;
        }
    }
    return boyGObj;
}

void CameraSetTargetGObj(GObj *gobj, GObj *subGObj)
{
    cameraTargetGObj = gobj;
    cameraTargetSubGObj = subGObj;
}

void CameraChangeTargetParallel(GObj *oldTarget, GObj *newTarget)
{
    struct {           /* field names derived */
        float move[4]; /* the step from the old target to the new one */
        float from[4]; /* the old target's root */
        float to[4];   /* the new target's root */
    } buf;

    if (oldTarget == 0) {
        buf.move[0] = 0.0f;
        buf.move[1] = 0.0f;
        buf.move[2] = 0.0f;
    } else {
        GetRootPosition(buf.from, oldTarget);
        GetRootPosition(buf.to, newTarget);
        sceVu0SubVector(buf.move, buf.to, buf.from);
    }
    *(CamTgt *)&targetCameraSet = *(CamTgt *)&cameraSet;
    sceVu0AddVector(targetCameraSet.pos, targetCameraSet.pos, buf.move);

    targetCameraSet.moving = 1;
}

GObj *CameraGetTarget(void)
{
    return cameraTargetGObj;
}

void CameraGetTargets(GObj **gobj, GObj **subGObj)
{
    *gobj = cameraTargetGObj;
    *subGObj = cameraTargetSubGObj;
}

void CameraSetMode(int mode)
{
    cameraMode = mode;
    targetCameraSet.moving = 0;
}

int CameraGetMode(void)
{
    return cameraMode;
}

void CameraGetOtherObjOffset(float *pos, float *outDist, int *outAngle)
{
    float v[4];
    int ang;
    *outDist = _DistGV(cameraSet.pos, pos);
    sceVu0SubVector(v, pos, cameraSet.pos);
    sceVu0Normalize(v, v);
    ang = (int)(_GetDirection(v) / 3.14159265f * 180.0f) - cameraSet.rotY * 180 / 32768;
    if (ang > 180) {
        ang -= 360;
    }
    if (ang <= -180) {
        ang += 360;
    }
    *outAngle = ang;
}

void InsertCamera_Set(float *pos, float *tgt, int frames)
{
    if (InsertCamera_isEnable()) {
        insertCamera.frames = frames;
        insertCamera.count = 0;
        insertCamera.pos[0] = pos[0];
        insertCamera.pos[1] = pos[1];
        insertCamera.pos[2] = pos[2];
        insertCamera.tgt[0] = tgt[0];
        insertCamera.tgt[1] = tgt[1];
        insertCamera.tgt[2] = tgt[2];
        insertCamera.enable = 1;
        insertCamera.cut = 1;
        insertCamera.cutType = 0;
        insertCamera.zoom = 1;
        insertCamera.cutBack = 1;
        insertCamera.blend = -1.0f;
    }
}

void InsertCamera_SetNoraml(float *pos, float *tgt, int frames, int cutType)
{
    if (InsertCamera_isEnable()) {
        insertCamera.frames = frames;
        insertCamera.count = 0;
        sceVu0ScaleVector(insertCamera.pos, pos, -1.0f);
        sceVu0ScaleVector(insertCamera.tgt, tgt, -1.0f);
        insertCamera.enable = 1;
        insertCamera.cut = 1;
        insertCamera.cutType = cutType;
        insertCamera.zoom = 1;
        insertCamera.cutBack = 1;
        insertCamera.blend = -1.0f;
    }
}

void InsertCamera_SetDetail(float *pos, float *tgt, int frames, int cutType, int zoom, int cutBack,
                            float blend)
{
    if (InsertCamera_isEnable()) {
        insertCamera.frames = frames;
        insertCamera.count = 0;
        sceVu0ScaleVector(insertCamera.pos, pos, -1.0f);
        sceVu0ScaleVector(insertCamera.tgt, tgt, -1.0f);
        insertCamera.enable = 1;
        insertCamera.cut = 1;
        insertCamera.cutType = cutType;
        insertCamera.zoom = zoom;
        insertCamera.cutBack = cutBack;
        insertCamera.blend = blend;
    }
}

void InsertCamera_Exec(float *cam, int *cut, int *cutType, int *enable)
{
    *cut = 0;
    *cutType = 0;
    *enable = 0;
    if (insertCamera.enable) {
        if (insertCamera.cut) {
            CAM_HOST_CUT_IF(insertCamera.cutType == 0); /* port (R7b): initMonitorCamera(1) */
            *cut = 1;
            *cutType = insertCamera.cutType;
            insertCamera.cut = 0;
            insertCamera.cutType = 0;
        }
        cam[0] = insertCamera.pos[0];
        cam[1] = insertCamera.pos[1];
        cam[2] = insertCamera.pos[2];
        cam[4] = insertCamera.tgt[0];
        cam[5] = insertCamera.tgt[1];
        cam[6] = insertCamera.tgt[2];
        *enable = 1;
    }
}

int *GetCurrentCameraSet2(void)
{
    return (int *)&cameraSet;
}

void SetCameraFlag_LwsCutBack(void)
{
    lwsCutBack = 1;
}

void SetCameraFlag_GamecamCutBack(void)
{
    gamecamCutBack = 1;
}

void SetHandCameraLimitInDemo(int limitP, int limitV)
{
    handCameraLimitP = limitP;
    handCameraLimitV = limitV;
}

void ResetHandCameraLimitInDemo(void)
{
    handCameraLimitP = GlobalStageSetting.handCameraLimitP;
    handCameraLimitV = GlobalStageSetting.handCameraLimitV;
}

void SetZoomMaxValInDemo(int zoom)
{
    zoomMaxInDemo = zoom;
}

void ResetZoomMaxValInDemo(void)
{
    zoomMaxInDemo = GlobalStageSetting.zoomMaxInDemo;
}

int UpdateHandCameraLimitP(void)
{
    handCameraLimitP = GlobalStageSetting.handCameraLimitP;
    return 0;
}

int UpdateHandCameraLimitV(void)
{
    handCameraLimitV = GlobalStageSetting.handCameraLimitV;
    return 0;
}

int UpdateZoomMaxVallInDemo(void)
{
    zoomMaxInDemo = GlobalStageSetting.zoomMaxInDemo;
    return 0;
}

static int InsertCamera_isEnable(void)
{
    return camctrl.pri < 2;
}

void CameraSetCameraPosition(float *src)
{
    if (cameraMode != 3) {
        cameraSet.pos[0] = src[0];
        cameraSet.pos[1] = src[1];
        cameraSet.pos[2] = src[2];
        targetCameraSet.pos[0] = src[0];
        targetCameraSet.pos[1] = src[1];
        targetCameraSet.pos[2] = src[2];
    }
}

void CameraSetTargetPos(void) {}

void *GetCameraPos(void)
{
    if (CameraCalclated_f == 0) {
        return 0;
    }
    return &cameraSet;
}

void GetCameraInfo_tmp(void *dst, float *out)
{
    union PendCopy *s = (union PendCopy *)&cameraSet;
    union PendCopy *d = (union PendCopy *)dst;
    d->q[0] = s->q[0];
    d->q[1] = s->q[1];
    d->q[2] = s->q[2];
    d->q[3] = s->q[3];
    *out = debug_zoom_per / 100.0f;
}

void testcamerazoom(void)
{
    zoomRequest = 1;
}

void SetMonitorCameraInitializeFlag(void)
{
    monitorCameraInit = 1;
}

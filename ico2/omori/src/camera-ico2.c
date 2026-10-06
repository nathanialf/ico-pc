#include "typedef.h"
#include "debug.h"
#include "memory.h"
#include "pad.h"
#include "act-game.h"
#include "commonact.h"
#include "boyact.h"
#include "camera-editor.h"
#include "camera-ico2.h"
#include "camera-set-manager.h"
#include "hand-camera.h"
#include "geometryManager.h"
#include "matrixDrive.h"
#include <libvu0.h>
#include <string.h>
#include "ios.h"
#include "camera-root.h"
#include "debug_exception.h"
#include "main.h"
#include "gv.h"
#include <assert.h>

/* PC port (renderer R7d, docs/port/RENDER_API.md "Frame rate and interpolation"): a camera
   group whose kind differs from the current one re-initialises the monitor
   camera, a hard cut for the presenter's interpolation
   (port/game/video_options.c; a counter no game state reads) */
extern void ico_video_camera_cut(void);
static void *ReadCameraSet(struct CamSetFile *f, int stage);

/* a loaded camera set: the file header, then the group records */
typedef struct CamSetHdr { /* field names derived */
    int magic;
    int ver;
    int count;          /* 0x08, the number of groups */
    int total;          /* 0x0C, the number of items */
    CamGroup groups[1]; /* 0x10 */
} CamSetHdr;            /* derived name */

typedef struct PluralCameraSet { /* field names derived */
    int id;                      /* 0x00 */
    void *set;                   /* 0x04 */
} PluralCameraSet;               /* derived name */

typedef struct CamWork { /* field names derived */
    Mat4 eye;            /* 0x00 */
    Mat4 at;             /* 0x10 */
    Mat4 ext;            /* 0x20 */
} CamWork;               /* derived name */

typedef struct CameraState { /* field names derived */
    char pad0[68];
    unsigned char active; /* 0x44 */
    char pad45[0x50 - 0x45];
    CamWork work;   /* 0x50 */
    float dbgA[4];  /* 0x80 */
    float dbgB[4];  /* 0x90 */
    float moveDist; /* 0xA0 */
    float atRate;   /* 0xA4 */
    char padA8[8];  /* 0xA8, to the record's 0xB0 bytes */
} CameraState;      /* derived name */

/* The per-group distance and weight arrays the group chooser scores, the
   monitor camera's whole state record, the two smoothed camera targets with
   the raw and previous copies the filter runs on, the position the group
   search is done at, and the plural camera sets.  Only the first three words
   of groupProbePos are used. */
static float cameraDist[100]; /* derived name */

static float cameraWeight[100]; /* derived name */

static CameraState monitorCamera; /* derived name */

static float targetAStart[3]; /* derived name */

static float targetASmooth[3]; /* derived name */

static float targetBSmooth[3]; /* derived name */

static float groupProbePos[36]; /* derived name */

/* SetCameraTargetPosition stores both with a 16-byte vector store; on the
   EE each lies in its own 16-byte slot of .bss and the fourth word lands in
   the slot's padding.  The host gives them that fourth word (package 2I,
   ASan global-buffer-overflow). */
#define CAM_PREV_WORDS 4

static float targetAPrev[CAM_PREV_WORDS]; /* derived name */

static float targetBPrev[CAM_PREV_WORDS]; /* derived name */

/* ten sets, the AddPluralCameraSet limit */
static PluralCameraSet pluralCameraSet[10]; /* derived name */

/* The camera-set binary this file reads in and the group window into it, the frame counter the warp guard tests, the current group,
   the two hand-camera correction rates setHandCameraRates scales by the frame
   budget, the "group changed this frame" flag, and the number of plural camera
   sets held in pluralCameraSet. */
static char *cameraSetBuf; /* derived name */

static char *cameraSetGroups; /* derived name */

static char *cameraSetGroupsEnd; /* derived name */

static int cameraSetGroupNum; /* derived name */

static int cameraFrames; /* derived name */

static int cameraGroupCurrent; /* derived name */

static float handCameraEyeRate; /* derived name */

static float handCameraAtRate; /* derived name */

static unsigned char cameraGroupChanged; /* derived name */

static int pluralCameraSetNum; /* derived name */

static float zoomOffsetRatio = 1.0f; /* derived name */

/* the camera-set binary (camera-editor.h's CamSetFile): a sixteen byte header,
   `count` group records of 0x4C and `total` item records whose stride is the
   file version's */

typedef struct CamItemV0 { /* 0x38 */ /* field names derived */
    unsigned char pin[56];            /* a version-0 pin record */
} CamItemV0;                          /* derived name */

typedef struct CamItemV1 { /* 0x40 */ /* field names derived */
    unsigned char pin[64];            /* a version-1 pin record */
} CamItemV1;                          /* derived name */

typedef struct CamItemV2 { /* 0x50 */ /* field names derived */
    unsigned char pin[80];            /* a version-2 pin record */
} CamItemV2;                          /* derived name */

#include "ee_view.h"

/* PC port: an old camera set's pin records are copied whole over the head of
   PinRec, so each version's record must end at the PinRec member the next
   version adds (tools/template_audit.py) */
_Static_assert(sizeof(CamItemV0) == __builtin_offsetof(PinRec, eyeRate), "CamItemV0 over PinRec");

_Static_assert(sizeof(CamItemV1) == __builtin_offsetof(PinRec, limitP), "CamItemV1 over PinRec");

_Static_assert(sizeof(CamItemV2) == __builtin_offsetof(PinRec, ofsB), "CamItemV2 over PinRec");

inline void SetCameraZoomOffsetRatio(float val)
{
    zoomOffsetRatio = val;
}

void CameraSetCameraSet(int id)
{
    CamGroup *p;
    CamGroup *end;
    int n;
    int i;

    TopCameraSetDataOfCurrentStage = GetPluralCameraSet(id);
    NumOfGroup = n = ((CamSetHdr *)TopCameraSetDataOfCurrentStage)->count;
    p = ((CamSetHdr *)TopCameraSetDataOfCurrentStage)->groups;
    end = &p[n];
    for (i = 0; i < n; i++) {
        CAMGROUP_SET_ITEMS(&p[i], (PinRec *)end);
    }
    ReflectCameraSetBinary(p, n);
}

void CameraSetCameraSet_Default(void)
{
    CameraSetCameraSet(stageData[stage_no].camSetId);
}

static void GetRootPositionForCamera(float *out, GObj *gobj)

{
    if (gobj == boyGObj) {
        GetBoyRootPositionForCamera(out, gobj);
    } else {
        GetRootPosition(out, gobj);
    }
}

inline void SetCameraTargetPosition(void *target, void *eye, float fov)
{
    sceVu0ScaleVector((&monitorCamera.work), eye, -1.0f);
    sceVu0ScaleVector(&monitorCamera.work.at, target, -1.0f);
    sceVu0ScaleVector(targetAPrev, target, -1.0f);
    sceVu0ScaleVector(targetBPrev, target, -1.0f);
    monitorCamera.work.ext.f[0] = fov;
}

static void ico2camera_GetTargetPos(int reset)
{
    unsigned char flag = reset;
    GObj *p1;
    GObj *p2;
    float v0[4];
    float v1[4];
    float v2[4];
    float A[4];
    float B[4];
    float C[4];
    int i;

    CameraGetTargets(&p1, &p2);
    if (p1 == 0) {
        return;
    }
    if (p2 != 0) {
        GetRootPositionForCamera(A, p1);
        GetRootPositionForCamera(B, p2);
        sceVu0ScaleVector(A, A, -1.0f);
        sceVu0ScaleVector(B, B, -1.0f);
        v1[0] = A[0];
        v1[1] = A[1];
        v1[2] = A[2];
        v0[0] = B[0];
        v0[1] = B[1];
        v0[2] = B[2];
        v2[0] = A[0];
        v2[1] = A[1];
        v2[2] = A[2];
    } else {
        GetRootPositionForCamera(C, p1);
        sceVu0ScaleVector(C, C, -1.0f);
        v0[0] = C[0];
        v0[1] = C[1];
        v0[2] = C[2];
        v1[0] = C[0];
        v1[1] = C[1];
        v1[2] = C[2];
        v2[0] = C[0];
        v2[1] = C[1];
        v2[2] = C[2];
    }
    {
        groupProbePos[0] = v2[0];
        groupProbePos[1] = v2[1];
        groupProbePos[2] = v2[2];
    }
    if (flag != 0) {
        float x = v0[0];
        float y = v0[1];
        float z = v0[2];
        targetAStart[0] = x;
        targetAStart[1] = y;
        targetAStart[2] = z;
        targetAPrev[0] = x;
        targetAPrev[1] = y;
        targetAPrev[2] = z;
        targetBPrev[0] = v1[0];
        targetBPrev[1] = v1[1];
        targetBPrev[2] = v1[2];
    }
    for (i = 0; i < 3; i++) {
        targetASmooth[i] = (v0[i] + targetAPrev[i] * 3.0f) * 0.25f;
        targetBSmooth[i] = (v1[i] + targetBPrev[i] * 3.0f) * 0.25f;
    }
    targetAPrev[0] = targetASmooth[0];
    targetAPrev[1] = targetASmooth[1];
    targetAPrev[2] = targetASmooth[2];
    targetBPrev[0] = targetBSmooth[0];
    targetBPrev[1] = targetBSmooth[1];
    targetBPrev[2] = targetBSmooth[2];
}

static int ico2camera_GetGroupNearest(float *query)
{
    int result = -1;
    float min = 3.40282347e+38f; /* FLT_MAX */
    int i;
    for (i = 0; i < cameraSetGroupNum; i++) {
        float buf[4];
        CamGroup *entry = (CamGroup *)(cameraSetGroups + i * 76);
        float *center = entry->center;
        float *range = entry->range;
        int k;
        memset(buf, 0, 16);
        for (k = 0; k < 3; k++) {
            float d = query[k] - center[k];
            float r;
            float t;
            if (d < 0.0f)
                d = -d;
            r = range[k];
            if (r < 0.0f)
                r = -r;
            if (r < 0.0f)
                t = 0.0f;
            else if (d < r)
                t = d;
            else
                t = r;
            buf[k] = d - t;
        }
        {
            float sum = buf[0] * buf[0] + buf[1] * buf[1] + buf[2] * buf[2];
            if (sum < min) {
                result = i;
                min = sum;
            }
        }
    }
    return result;
}

static void initMonitorCamera(unsigned char init)
{
    monitorCamera.active = 1;
    if (init)
        SetMonitorCameraInitializeFlag();
}

/* The retail build compiles out this function's debug arms, which is why
 * `vDbg` is read at the writeback with nothing having written it, `vDiff` is
 * written and never read, `vSpare` is read only by the DEBUG build's report in
 * the second arm, and one loop keeps its counter with an empty body. */
static void monitorMonitorCamera(CamWork *cam, CamWork *out)
{
    float vDiff[4];
    float vDbg[4];
    float vEye[4];
    float vOut[4];
    float vAt[4];
    float vAt2[4];
    float vSpare[4];
    GObj *p1;
    GObj *p2;
    int flag;
    int i;
    int k;
    int held;
    int mode;
    float len;
    float t;
    float d;
    float d1;
    float d2;
    float r1;
    float r2;

    flag = 0;
    for (i = 0; i < 3; i++) {
        if (1000000.0f < cam->at.f[i] || cam->at.f[i] < -1000000.0f) {
            flag = 1;
            break;
        }
    }
    if (monitorCamera.active != 0) {
        monitorCamera.work = *cam;
        monitorCamera.moveDist = 0.0f;
        monitorCamera.atRate = 0.0f;
        monitorCamera.active = 0;
        if (flag != 0 && cameraFrames < 10) {
            monitorCamera.active = 1;
        }
        monitorCamera.dbgA[0] = 0.0f;
        monitorCamera.dbgA[1] = 0.0f;
        monitorCamera.dbgA[2] = 0.0f;
        monitorCamera.dbgB[0] = 0.0f;
        monitorCamera.dbgB[1] = 0.0f;
        monitorCamera.dbgB[2] = 0.0f;
        *out = *cam;
        return;
    }
    if (monitorCameraHold != 0) {
        *cam = monitorCamera.work;
    }
    *out = *cam;
    vDiff[0] = cam->eye.f[0] - monitorCamera.work.eye.f[0];
    vDiff[1] = cam->eye.f[1] - monitorCamera.work.eye.f[1];
    vDiff[2] = cam->eye.f[2] - monitorCamera.work.eye.f[2];
    sceVu0SubVector(vEye, cam, &monitorCamera.work);
    len = FSqrt(vEye[0] * vEye[0] + vEye[1] * vEye[1] + vEye[2] * vEye[2]);
    if (handCameraEyeRate * 30.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]) * 11.0f <
        len) {
        if (handCameraEyeRate * 30.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]) <
            monitorCamera.moveDist) {
            monitorCamera.moveDist =
                handCameraEyeRate * 30.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]);
        }
        if (monitorCamera.moveDist < len) {
            len = monitorCamera.moveDist +
                  handCameraEyeRate * 30.0f /
                      (float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 30.0f;
            sceVu0Normalize(vEye, vEye);
            sceVu0ScaleVector(vEye, vEye, len);
            sceVu0AddVector(out, &monitorCamera.work, vEye);
        } else {
            len =
                handCameraEyeRate * 30.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]);
            sceVu0Normalize(vEye, vEye);
            sceVu0ScaleVector(vEye, vEye, len);
            sceVu0AddVector(out, &monitorCamera.work, vEye);
        }
    } else {
        _InterGV(out->eye.f, cam->eye.f, monitorCamera.work.eye.f, 10.0f, 1.0f);
        for (k = 0; k < 3; k++) {}
    }
    sceVu0SubVector(vOut, out, &monitorCamera.work);
    t = FSqrt(vOut[0] * vOut[0] + vOut[1] * vOut[1] + vOut[2] * vOut[2]);
    monitorCamera.moveDist = t;
#ifdef DEBUG
    sceVu0SubVector(vSpare, &cam->at, &monitorCamera.work.at);
    scePrintf("monitor camera move %f at %f %f %f\n", t, vSpare[0], vSpare[1], vSpare[2]);
#endif
    held = 0;
    d = handCameraAtRate * 30.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]);
    if (d < 0.0f) {
        d = -d;
    }
    sceVu0SubVector(vAt, &cam->at, &monitorCamera.work.at);
    len = FSqrt(vAt[0] * vAt[0] + vAt[1] * vAt[1] + vAt[2] * vAt[2]);
    sceVu0Normalize(vAt, vAt);
    CameraGetTargets(&p1, &p2);
    if (p1 == boyGObj) {
        held = (p2 == 0);
    }
    if (held == 0 && 95.0f <= d) {
        d = (float)(600 / ((60 - systemStatus[0] * 10) / systemStatus[1]));
    }
    if (d * 9.0f < len) {
        monitorCamera.atRate = monitorCamera.atRate + 0.5f;
        if (d < monitorCamera.atRate) {
            monitorCamera.atRate = d;
        }
        sceVu0ScaleVector(vAt, vAt, monitorCamera.atRate);
        sceVu0AddVector(&out->at, &monitorCamera.work.at, vAt);
    } else {
        mode = 8;
        if (held != 0) {
            if (115.0f <= d) {
                mode = 1;
            } else if (105.0f <= d) {
                mode = 2;
            } else if (95.0f <= d) {
                mode = 4;
            }
        }
        _InterGV(out->at.f, cam->at.f, monitorCamera.work.at.f,
                 mode * (float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 30.0f,
                 30.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]));
        sceVu0SubVector(vAt, &out->at, &monitorCamera.work.at);
        len = FSqrt(vAt[0] * vAt[0] + vAt[1] * vAt[1] + vAt[2] * vAt[2]);
        if (d < len) {
            len = d;
        }
        sceVu0Normalize(vAt, vAt);
        sceVu0ScaleVector(vAt, vAt, len);
        sceVu0AddVector(&out->at, &monitorCamera.work.at, vAt);
        sceVu0SubVector(vAt2, &out->at, &monitorCamera.work.at);
        len = FSqrt(vAt2[0] * vAt2[0] + vAt2[1] * vAt2[1] + vAt2[2] * vAt2[2]);
        monitorCamera.atRate = len;
        if (d < len) {
            monitorCamera.atRate = d;
        }
    }
    if (out->ext.f[0] - monitorCamera.work.ext.f[0] != 0.0f) {
        d1 = _DistGV(out, cam);
        d2 = _DistGV(out, &monitorCamera.work);
        if (d1 + d2 != 0.0f) {
            out->ext.f[0] = (out->ext.f[0] * d2 + monitorCamera.work.ext.f[0] * d1) / (d1 + d2);
        }
    }
    if (insertCameraBlendTimer != 0) {
        r1 = ico_d2f(
            ico_ddiv(ico_f2d((float)((60 - systemStatus[0] * 10) / systemStatus[1])), ICO_D(30.0)));
        r2 = ico_d2f(
            ico_ddiv(ICO_D(3.0), ico_f2d((float)((60 - systemStatus[0] * 10) / systemStatus[1]))));
        _InterGV(out->eye.f, cam->eye.f, monitorCamera.work.eye.f, r1, r2);
        _InterGV(out->at.f, cam->at.f, monitorCamera.work.at.f, r1, r2);
        out->ext.f[0] = (cam->ext.f[0] * r2 + monitorCamera.work.ext.f[0] * r1) / (r1 + r2);
    }
    monitorCamera.work = *out;
    monitorCamera.dbgA[0] = vDbg[0];
    monitorCamera.dbgA[1] = vDbg[1];
    monitorCamera.dbgA[2] = vDbg[2];
}

static void ChaseCamera(float *pos, float *cam)
{
    Mat4 v0;
    Mat4 v1;
    /* the chase offset from the target, 200 up and 500 behind, rotated onto the
       target orient below; the same two distances are spelled out again for the
       fallback eye this function blends with */
    Mat4 mat = {{0.0f, 200.0f, 500.0f, 0.0f}};
    Mat4 v3;
    float t;
    t = _GetDirection(test_CURRENTORIENT(default_cameratarget_gobj));
    _ApplyRyGV(mat.f, (float)(int)(t / 3.1415927f * 180.0f) * 3.1415927f / 180.0f);
    sceVu0AddVector(&v0, pos, &mat);
    sceVu0SubVector(&v3, cam, pos);
    v3.f[1] = 0.0f;
    FSqrt(v3.f[0] * v3.f[0] + v3.f[1] + v3.f[2] * v3.f[2]);
    sceVu0Normalize(&v3, &v3);
    sceVu0ScaleVector(&v3, &v3, -500.0f);
    sceVu0AddVector(&v1, &v3, pos);
    v1.f[1] = pos[1] + 200.0f;
    _InterGV(cam, v0.f, v1.f, 4.0f, 5.0f);
    cam[0] = v0.f[0];
    cam[1] = v0.f[1];
    cam[2] = v0.f[2];
    cam[4] = pos[0];
    cam[5] = pos[1];
    cam[6] = pos[2];
    cam[8] = 50.0f;
}

/* Scale the two hand-camera correction rates by the frame budget and report
 * the frame step, for InitIco2Camera and CameraMove. */
static inline int setHandCameraRates(float a, float b) /* derived name */
{
    handCameraEyeRate = a * 60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]);
    handCameraAtRate = b * 60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]);
    return (60 - systemStatus[0] * 10) / systemStatus[1];
}

#define CAMSET_GROUP(n) ((CamGroup *)(cameraSetGroups + (n) * 76))

/* the debug markers' pulse, two steps a frame */
static int markerPulse = 0; /* derived name */

/* the mean and the standard deviation of the first `n` weights, written back
 * through two pointers */
static inline void cameraWeightStat(float *arr, int n, float *outSd,
                                    float *outMean) /* derived name */
{
    float var;
    int j;

    *outMean = 0.0f;
    for (j = 0; j < n; j++) {
        *outMean = *outMean + arr[j];
    }
    *outMean = *outMean / (float)n;
    var = 0.0f;
    for (j = 0; j < n; j++) {
        float t = arr[j] - *outMean;
        var = var + t * t;
    }
    *outSd = FSqrt(var / (float)n);
}

static void CameraMove(int group, float *pos, float *out, float *ofsA, float *ofsB)
{
    float acc[4];
    Mat4 q;
    float sd;
    float mean;
    PinRec *p;
    PinRec *r;
    int i;
    int j;
    float sum;
    float total;
    float w;
    float u;
    float d;
    float rate;
    float e0;
    float e1;
    float e2;
    float e3;
    float e4;

    if (CAMSET_GROUP(group)->mode == 2) {
        ChaseCamera(pos, out);
        return;
    }
    ofsA[0] = 300.0f;
    ofsA[1] = 100.0f;
    ofsA[2] = 0.0f;
    sum = ofsA[2];
    i = 0;
    for (p = &CAMGROUP_ITEMS(CAMSET_GROUP(group))[CAMSET_GROUP(group)->first];
         p != &CAMGROUP_ITEMS(CAMSET_GROUP(group))[CAMSET_GROUP(group)->end]; p++) {
        if (p->on != 0) {
            {
                Mat4 tv;
                memset(tv.f, 0, 16);
                tv.f[0] = p->look[0];
                tv.f[1] = p->look[1];
                tv.f[2] = p->look[2];
                q = tv;
            }
            d = _DistGV(pos, &q);
            cameraDist[i] = cameraWeight[i] = d;
            sum = sum + d;
            i++;
        }
    }
    if (i == 1) {
        cameraWeight[0] = 1.0f;
    } else if (i < 5) {
        i = 0;
        for (p = &CAMGROUP_ITEMS(CAMSET_GROUP(group))[CAMSET_GROUP(group)->first];
             p != &CAMGROUP_ITEMS(CAMSET_GROUP(group))[CAMSET_GROUP(group)->end]; p++) {
            if (p->on != 0) {
                cameraWeight[i] = (sum - cameraWeight[i]) * (sum - cameraWeight[i]);
                i++;
            }
        }
    } else {
        cameraWeightStat(cameraWeight, i, &sd, &mean);
        i = 0;
        for (p = &CAMGROUP_ITEMS(CAMSET_GROUP(group))[CAMSET_GROUP(group)->first];
             p != &CAMGROUP_ITEMS(CAMSET_GROUP(group))[CAMSET_GROUP(group)->end]; p++) {
            if (p->on != 0) {
                cameraWeight[i] = (cameraWeight[i] - mean) * 10.0f / sd + 50.0f;
                if (cameraWeight[i] < 0.0f || 100.0f < cameraWeight[i]) {
                    cameraWeight[i] = 0.0f;
                }
                cameraWeight[i] = 100.0f - cameraWeight[i];
                if (cameraWeight[i] < 40.0f) {
                    cameraWeight[i] = 0.0f;
                } else {
                    cameraWeight[i] = cameraWeight[i] - 40.0f;
                }
                cameraWeight[i] =
                    (cameraWeight[i] * cameraWeight[i]) * (cameraWeight[i] * cameraWeight[i]);
                i++;
            }
        }
    }
    i = 0;
    for (p = &CAMGROUP_ITEMS(CAMSET_GROUP(group))[CAMSET_GROUP(group)->first];
         p != &CAMGROUP_ITEMS(CAMSET_GROUP(group))[CAMSET_GROUP(group)->end]; p++) {
        if (p->on != 0) {
            if (p->range != 0.0f && cameraDist[i] < p->range) {
                u = (cameraDist[i] - 100.0f) / p->range;
                rate = u < 0.0001f ? 0.0001f : (1.0f < u ? 1.0f : u);
                j = 0;
                for (r = &CAMGROUP_ITEMS(CAMSET_GROUP(group))[CAMSET_GROUP(group)->first];
                     r != &CAMGROUP_ITEMS(CAMSET_GROUP(group))[CAMSET_GROUP(group)->end]; r++) {
                    if (r->on != 0) {
                        if (j != i) {
                            cameraWeight[j] = cameraWeight[j] * rate;
                        }
                        j++;
                    }
                }
            }
            i++;
        }
    }
    total = 0.0f;
    i = 0;
    for (p = &CAMGROUP_ITEMS(CAMSET_GROUP(group))[CAMSET_GROUP(group)->first];
         p != &CAMGROUP_ITEMS(CAMSET_GROUP(group))[CAMSET_GROUP(group)->end]; p++) {
        if (p->on != 0) {
            total = total + cameraWeight[i];
            i++;
        }
    }
    acc[0] = acc[1] = acc[2] = 0.0f;
    ofsA[0] = 0.0f;
    ofsA[1] = 0.0f;
    ofsA[2] = 0.0f;
    ofsB[0] = 0.0f;
    ofsB[1] = 0.0f;
    ofsB[2] = 0.0f;
    e0 = 0.0f;
    e1 = 0.0f;
    e2 = 0.0f;
    e3 = 0.0f;
    e4 = 0.0f;
    i = 0;
    for (p = &CAMGROUP_ITEMS(CAMSET_GROUP(group))[CAMSET_GROUP(group)->first];
         p != &CAMGROUP_ITEMS(CAMSET_GROUP(group))[CAMSET_GROUP(group)->end]; p++) {
        if (p->on != 0) {
            w = cameraWeight[i] / total;
            acc[0] = acc[0] + p->pos[0] * w;
            acc[1] = acc[1] + p->pos[1] * w;
            acc[2] = acc[2] + p->pos[2] * w;
            e0 = e0 + p->fov * w;
            e1 = e1 + p->eyeRate * w;
            e2 = e2 + p->atRate * w;
            e3 = e3 + p->limitP * w;
            e4 = e4 + p->limitV * w;
            ofsA[0] = ofsA[0] + p->ofs[0] * w;
            ofsA[1] = ofsA[1] + p->ofs[1] * w;
            ofsA[2] = ofsA[2] + p->ofs[2] * w;
            ofsB[0] = ofsB[0] + p->ofsB[0] * w;
            ofsB[1] = ofsB[1] + p->ofsB[1] * w;
            ofsB[2] = ofsB[2] + p->ofsB[2] * w;
            if (debug_camera_flag != 0) {
                if (0.0f < p->range) {
                    debug_Marker(p->look, (int)(w * 255.0f), 0, 0, p->range, (float)markerPulse);
                } else {
                    debug_Marker(p->look, 0, (int)(w * 255.0f), 0, 100.0f, (float)markerPulse);
                }
            }
            i++;
        }
    }
    markerPulse = markerPulse + 2;
    out[0] = acc[0];
    out[1] = acc[1];
    out[2] = acc[2];
    out[4] = pos[0];
    out[5] = pos[1];
    out[6] = pos[2];
    setHandCameraRates(e1, e2);
    SetLimitHandCameraCorrect(e3, e4);
    out[8] = e0;
}

inline int GetSizeOfCameraSetBinary(CamGroup *p, int n)
{
    int size = n * 76;
    int i;
    for (i = 0; i < n; i++) {
        size += (p->end - p->first) * 92;
        p++;
    }
    return size;
}

inline void MakeCameraSetBinary(CamGroup *src, int count, CamGroup *dst)
{
    int total = 0;
    PinRec *out = (PinRec *)(dst + count);
    PinRec *outBase = out;
    CamGroup *s;
    for (s = src; s != src + count; dst++, s++) {
        PinRec *is;
        *dst = *s;
        dst->first = total;
        CAMGROUP_SET_ITEMS(dst, outBase);
        is = CAMGROUP_ITEMS(s) + s->first;
        while (is != CAMGROUP_ITEMS(s) + s->end) {
            *out = *is;
            out++;
            total++;
            is++;
        }

        dst->end = total;
    }
}

void ReflectCameraSetBinary(CamGroup *src, int count)
{
    if (cameraSetBuf != 0) {
        iosFree(cameraSetBuf);
    }

    cameraSetBuf =
        iosMallocDebug(ios_partition_oomori, GetSizeOfCameraSetBinary(src, count), __FILE__, 1577);
    cameraSetGroups = cameraSetBuf;
    cameraSetGroupsEnd = cameraSetBuf + count * 76;
    cameraSetGroupNum = count;
    MakeCameraSetBinary(src, count, (CamGroup *)cameraSetBuf);
}

void InitIco2Camera(void)
{
    cameraFrames = 0;
    cameraSetBuf = 0;
    CameraSetCameraSet_Default();
    initMonitorCamera(1);
    cameraGroupCurrent = -1;
    cameraGroupChanged = 1;
    setHandCameraRates(stageData[stage_no].handCameraRate, 10.0f);
    InitHandCameraCorrect();
}

/* the target offset the smoothing test measures the new one against, reset
   whenever the actor asks for no offset */

/* sceVu0AddVector below stores all four lanes (the EE's sqc2 writes the 4
   bytes after the array, whatever the linker put there); the host gives the
   fourth lane its own room */
static float lastTargetOffset[4] = {0.0f, 0.0f, 0.0f}; /* derived name */

static void GetTargetOffset(GObj *gobj, float *v, unsigned char flag)
{
    float ofs[4];
    float w[4];
    Sub15C *p;
    int n;
    int need;

    if (gobj == default_cameratarget_gobj && gobj != 0) {
        n = (int)(_GetDirection(test_CURRENTORIENT(gobj)) / 3.1415927f * 180.0f);
        ofs[0] = v[0];
        ofs[1] = v[1];
        ofs[2] = -v[2];
        need = ACTNotNeedCameraOffset(gobj) ? 1 : flag;
        if (need) {
            lastTargetOffset[0] = 0.0f;
            lastTargetOffset[1] = 0.0f;
            lastTargetOffset[2] = 0.0f;
        }
        _ApplyRyGV(ofs, (float)n * 3.1415927f / 180.0f);
        p = GOBJ_SUB(gobj);
        if (3.0f < FSqrt(p->root.move[0] * p->root.move[0] + p->root.move[2] * p->root.move[2])) {
            sceVu0SubVector(w, ofs, lastTargetOffset);
            if (FSqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]) < 1.5f) {
                lastTargetOffset[0] = ofs[0];
                lastTargetOffset[1] = ofs[1];
                lastTargetOffset[2] = ofs[2];
            } else {
                sceVu0Normalize(w, w);
                sceVu0ScaleVector(w, w, 1.5f);
                sceVu0AddVector(lastTargetOffset, lastTargetOffset, w);
            }
        }
        v[0] = lastTargetOffset[0];
        v[1] = lastTargetOffset[1];
        v[2] = lastTargetOffset[2];
    }
}

inline void GetHandCameraStickInfo(float *outX, float *outZ, float *outMag)
{
    IosPadCtx padCtx;
    IosPadStick st;

    iosPadConnect(&padCtx, 0, 0, &iosPadConfDefault);
    iosPadRead(&padCtx);
    iosPadGetStick(&padCtx, &st, 1, 2, 2, 0);
    {
        Mat4 dir = {{(float)st.x - 127.5f, 0.0f, (float)st.y - 127.5f, 0.0f}};

        sceVu0Normalize(&dir, &dir);
        *outMag = st.mag;
        *outX = dir.f[0] * *outMag;
        *outZ = dir.f[2] * *outMag;
    }
}

/* the camera-group search GetCameraGroupFromGObj, GetCameraGroupFromPosition
 * and SetCameraMatrix_Ico2 share */
static inline int findCameraGroupContaining(float *pos) /* derived name */
{
    int result = -1;
    int i;
    for (i = 0; i < cameraSetGroupNum; i++) {
        int k = 0;
        CamGroup *entry = (CamGroup *)(cameraSetGroups + i * 76);
        float *range = entry->range;
        float *center = entry->center;
        float *p = pos;
        do {
            if (*p < *center - *range) {
                break;
            }
            if (*center + *range < *p) {
                break;
            }
            p++;
            range++;
            center++;
        } while (++k < 3);
        if (k == 3) {
            result = i;
            break;
        }
    }
    return result;
}

void SetCameraMatrix_Ico2(int flag)
{
    CamWork cw = monitorCamera.work;
    float vA[4];
    float vB[4];
    CamWork cw2;
    int cut;
    int cutType;
    int enable;
    int mode = 1;
    int changed = 0;
    unsigned char f8;
    int group;

    if (cameraGroupChanged != 0) {
        flag = 1;
        cameraGroupChanged = 0;
        changed = 1;
    }
    if (flag) {
        changed = mode;
    }
    f8 = flag;
    ico2camera_GetTargetPos(f8);
    group = findCameraGroupContaining(groupProbePos);
    if (changed && group == -1) {
        group = ico2camera_GetGroupNearest(groupProbePos);
    }
    if (group == -1) {
        cw = monitorCamera.work;
        cw.at.f[0] = targetASmooth[0];
        cw.at.f[1] = targetASmooth[1];
        cw.at.f[2] = targetASmooth[2];
        memset(vA, 0, 16);
        GetTargetOffset(default_cameratarget_gobj, vA, 0);
        sceVu0ScaleVector(vA, vA, zoomOffsetRatio);
        sceVu0AddVector(cw.at.f, cw.at.f, vA);
    } else {
        if (flag != 0 || (cameraGroupCurrent != -1 &&
                          ((CamGroup *)cameraSetGroups)[group].kind !=
                              ((CamGroup *)cameraSetGroups)[cameraGroupCurrent].kind)) {
            initMonitorCamera(1);
            ico_video_camera_cut();
            f8 = 1;
        }
        memset(vA, 0, 16);
        memset(vB, 0, 16);
        CameraMove(group, targetBSmooth, (float *)&cw, vA, vB);
        cw.at.f[0] = targetASmooth[0];
        cw.at.f[1] = targetASmooth[1];
        cw.at.f[2] = targetASmooth[2];
        GetTargetOffset(default_cameratarget_gobj, vA, f8);
        sceVu0ScaleVector(vA, vA, zoomOffsetRatio);
        sceVu0ScaleVector(vB, vB, zoomOffsetRatio);
        sceVu0AddVector(cw.at.f, cw.at.f, vA);
        sceVu0AddVector(cw.at.f, cw.at.f, vB);
        cameraGroupCurrent = group;
    }
    InsertCamera_Exec((float *)&cw, &cut, &cutType, &enable);
    if (cut != 0) {
        initMonitorCamera(cutType == 0);
    }
    if (enable != 0) {
        mode = 0;
        FixViewInGameCameraFlag = 1;
    }
    monitorMonitorCamera(&cw, &cw2);
    cw = cw2;
    if (debug_camera_flag != 0) {
        debug_Marker(cw.at.f, 0, 0, 255, 100.0f, 0.0f);
    }
    sceVu0ScaleVector(&cw, &cw, -1.0f);
    sceVu0ScaleVector(cw.at.f, cw.at.f, -1.0f);
    {
        float sx;
        float sz;
        float mag;

        GetHandCameraStickInfo(&sx, &sz, &mag);
        if (systemStatus[5] != 0 || IsAbleBoyControl() == 0 || mode == 0) {
            ClearHandCameraCorrect();
        } else {
            HandCameraCorrect(&cw, cw.at.f, 0, sx, sz,
                              60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]));
        }
    }
    SetWSMatrix(&cw);
    cameraFrames = cameraFrames + 1;
}

inline void *GetPluralCameraSet(int id)
{
    int i;

    for (i = 0; i < pluralCameraSetNum; i++) {
        if (pluralCameraSet[i].id == id) {
            return pluralCameraSet[i].set;
        }
    }
    /* EUC-JP: "[%s] was not found\n" */
    debug_StdPrintfDummy("[%s]が見つかりません\n", cameraSetList[id]);
    debug_assert(__FILE__, 2036);
    __assert(__FILE__, 2036, "0");
    return 0;
}

inline void AddPluralCameraSet(int id, char *name)
{
    PluralCameraSet *p;

    if (pluralCameraSetNum >= 10) {
        /* EUC-JP: "at most [%d] camera sets can be registered in one stage." */
        debug_StdPrintfDummy("１ステージに登録できるカメラセットは、最大[%d]個です。", 10);
        debug_assert(__FILE__, 2045);
        __assert(__FILE__, 2045, "0");
    }
    p = &pluralCameraSet[pluralCameraSetNum];
    p->id = id;
    p->set = ReadCameraSet((CamSetFile *)name, stage_no);
    pluralCameraSetNum++;
}

inline void InitPluralCameraSet(void)
{
    pluralCameraSetNum = 0;
}

/* the sizes allocCameraSet, GetSizeOfCameraSetBinary and the group walks
   spell as 16, 76 and 92: the frozen disc records (eeword.h keeps
   CamGroup.items a 32-bit word) */
_Static_assert(sizeof(CamSetFile) == 16, "CamSetFile is the 16-byte .gcm head");

_Static_assert(sizeof(CamGroup) == 76, "CamGroup is the 76-byte .gcm group");

_Static_assert(sizeof(PinRec) == 92, "PinRec is the 92-byte .gcm pin");

/* the camera-set reallocator: a block sized for the set's groups and items,
   for ReadCameraSet's four call sites */
static inline CamSetFile *allocCameraSet(CamSetFile *f) /* derived name */
{
    CamSetFile *p;

    p = (CamSetFile *)iosMallocDebug(ios_partition_seki, 16 + f->count * 76 + f->total * 92,
                                     __FILE__, 2166);
    *p = *f;
    p->magic = 0x1234;
    p->ver = 3;
    return p;
}

static void *ReadCameraSet(CamSetFile *f, int stage)
{
    CamSetFile *p = 0;
    int n = f->count;
    int i;
    int n_pin;

    debug_StdPrintfDummy("camera data version = [%d]\n", f->ver);
    switch (f->ver) {
    case 0: {
        CamGroup *og = (CamGroup *)((char *)f + 16);
        char *oi = (char *)og + n * 76;
        CamGroup *ng;
        PinRec *ni;
        int total;

        total = 0;
        for (i = 0; i < n; i++) {
            total += og[i].end - og[i].first;
        }
        f->total = n_pin = total;
        debug_StdPrintfDummy("n_group[%d], n_pin[%d]\n", n, n_pin);
        p = allocCameraSet(f);
        ng = (CamGroup *)((char *)p + 16);
        ni = (PinRec *)((char *)ng + n * 76);
        for (i = 0; i < n; i++) {
            ng[i] = og[i];
        }
        for (i = 0; i < total; i++) {
            *(CamItemV0 *)&ni[i] = ((CamItemV0 *)oi)[i];
        }
        for (i = 0; i < total; i++) {
            ni[i].eyeRate = stageData[stage].handCameraRate;
            ni[i].atRate = 10.0f;
            ni[i].limitP = 120.0f;
            ni[i].limitV = 80.0f;
            ni[i].ofsB[0] = ni[i].ofsB[1] = ni[i].ofsB[2] = 0.0f;
        }
        break;
    }
    case 1: {
        CamGroup *og = (CamGroup *)((char *)f + 16);
        char *oi = (char *)og + n * 76;
        CamGroup *ng;
        PinRec *ni;
        int total;

        total = 0;
        for (i = 0; i < n; i++) {
            total += og[i].end - og[i].first;
        }
        f->total = n_pin = total;
        debug_StdPrintfDummy("n_group[%d], n_pin[%d]\n", n, n_pin);
        p = allocCameraSet(f);
        ng = (CamGroup *)((char *)p + 16);
        ni = (PinRec *)((char *)ng + n * 76);
        for (i = 0; i < n; i++) {
            ng[i] = og[i];
        }
        for (i = 0; i < total; i++) {
            *(CamItemV1 *)&ni[i] = ((CamItemV1 *)oi)[i];
        }
        for (i = 0; i < total; i++) {
            ni[i].limitP = 120.0f;
            ni[i].limitV = 80.0f;
            ni[i].ofsB[0] = ni[i].ofsB[1] = ni[i].ofsB[2] = 0.0f;
        }
        break;
    }
    case 2: {
        CamGroup *og = (CamGroup *)((char *)f + 16);
        char *oi = (char *)og + n * 76;
        CamGroup *ng;
        PinRec *ni;
        int total;

        total = 0;
        for (i = 0; i < n; i++) {
            total += og[i].end - og[i].first;
        }
        f->total = n_pin = total;
        debug_StdPrintfDummy("n_group[%d], n_pin[%d]\n", n, n_pin);
        p = allocCameraSet(f);
        ng = (CamGroup *)((char *)p + 16);
        ni = (PinRec *)((char *)ng + n * 76);
        for (i = 0; i < n; i++) {
            ng[i] = og[i];
        }
        for (i = 0; i < total; i++) {
            *(CamItemV2 *)&ni[i] = ((CamItemV2 *)oi)[i];
        }
        for (i = 0; i < total; i++) {
            ni[i].ofsB[0] = ni[i].ofsB[1] = ni[i].ofsB[2] = 0.0f;
        }
        break;
    }
    case 3: {
        CamGroup *og = (CamGroup *)((char *)f + 16);
        char *oi = (char *)og + n * 76;
        CamGroup *ng;
        PinRec *ni;
        int total;

        total = 0;
        for (i = 0; i < n; i++) {
            total += og[i].end - og[i].first;
        }
        f->total = n_pin = total;
        debug_StdPrintfDummy("n_group[%d], n_pin[%d]\n", n, n_pin);
        p = allocCameraSet(f);
        ng = (CamGroup *)((char *)p + 16);
        ni = (PinRec *)((char *)ng + n * 76);
        for (i = 0; i < n; i++) {
            ng[i] = og[i];
        }
        for (i = 0; i < total; i++) {
            ni[i] = ((PinRec *)oi)[i];
        }
        for (i = 0; i < total; i++) {}
        break;
    }
    default:
        /* EUC-JP: "the camera data version is wrong. please tell Omori.\n" */
        debug_StdPrintfDummy(
            "カメラデータのバージョンに異常があります。大森まで知らせてください\n");
        debug_StdPrintfDummy("illegal camera data version [%d]\n", f->ver);
        debug_assert(__FILE__, 2355);
        __assert(__FILE__, 2355, "0");
        break;
    }
    return p;
}

inline int GetCameraGroupCurrent(void)
{
    return cameraGroupCurrent;
}

inline int GetCameraGroupFromGObj(void *obj)
{
    float buf[4];
    float *bp;
    int result;
    int i;
    GetRootPosition(buf, obj);
    sceVu0ScaleVector(buf, buf, -1.0f);
    bp = buf;
    result = -1;
    for (i = 0; i < cameraSetGroupNum; i++) {
        int k = 0;
        CamGroup *entry = (CamGroup *)(cameraSetGroups + i * 76);
        float *range = entry->range;
        float *center = entry->center;
        float *p = bp;
        do {
            if (*p < *center - *range) {
                break;
            }
            if (*center + *range < *p) {
                break;
            }
            p++;
            range++;
            center++;
        } while (++k < 3);
        if (k == 3) {
            result = i;
            break;
        }
    }
    return result;
}

inline int GetCameraGroupFromPosition(float *pos)
{
    float buf[4];
    float *bp;
    int result;
    int i;
    sceVu0ScaleVector(buf, pos, -1.0f);
    bp = buf;
    result = -1;
    for (i = 0; i < cameraSetGroupNum; i++) {
        int k = 0;
        CamGroup *entry = (CamGroup *)(cameraSetGroups + i * 76);
        float *range = entry->range;
        float *center = entry->center;
        float *p = bp;
        do {
            if (*p < *center - *range) {
                break;
            }
            if (*center + *range < *p) {
                break;
            }
            p++;
            range++;
            center++;
        } while (++k < 3);
        if (k == 3) {
            result = i;
            break;
        }
    }
    return result;
}

/* three zero words; nothing reads them */
static int cameraIco2Word0 = 0; /* derived name */

int current_group = 0;

static int cameraIco2Word1 = 0; /* derived name */

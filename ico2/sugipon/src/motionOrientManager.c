#include "debug.h"
#include "debug_exception.h"
#include "gobj.h"
#include "s_init.h"
#include "frameDependSequence.h"
#include "matrixDrive.h"
#include "motionManager.h"
#include "motionManager2.h"
#include "geometryManager.h"
#include "wireLetter.h"
#include "obj_manager.h"
#include "quaternion.h"
#include "motionOrientManager.h"
#include <stdio.h>
#include <assert.h>
#include "streamMotionManager.h"
#include "tableSin.h"
#include "typedef.h"
#include "GifPacket.h"
#include <libvu0.h>
#include "GsBase.h"
#include "motionFileManager.h"
#include "fieldCollision.h"
#include <math.h>
#include "lineManager.h"
#include "main.h"
#include "Matrix.h"
#include "ee_view.h"
#include <string.h> /* memset (shiftMotionOrientBeginFunc) */

static void getMotionGeometry(void *self);
static void getStreamBlendShapeGeometry(void *self, void *m0, void *m1, float t);
static void getStreamShapeGeometry(void *self, void *sm);
static void shiftMotionData(ICO_WORD_PTR(void *) obj, int motion, int request, int shiftMode);

/* The rope's interpolation rate: the chain's geometry sets it from the hang
   height and rootUpdateY_Rope moves the root by it. */
float ropeInterRate = 0.0f; /* derived name */

extern MotionOrientEntry motionOrient[];

struct MotOriHead8 { /* field names derived */
    long long v;
} __attribute__((packed));

struct MotOriFloat { /* field names derived */
    float frame;
};

/* the object's display object, read through geometryManager.h's SubHandle
   union */
#define MOWORK(self) (((SubHandle *)&((GObj *)(self))->dobj)->sub) /* derived name */

/* The 0x470 motion work area is motionManager.h's MotCtrl. */

/* the trigger definition table, read-only */
extern const MotionDef motionKind[];

/* the seventeen fixed captions the orientation debug window prints, one per
   trigger kind, plus the window's own format */

/* declared here: ico2/fumi/src/commonact.c declares the table without
   const, so motionOrientManager.h cannot carry it */
extern const MotOriName motionOriKind[];

static void orientDebug(void *self, int idx, int y)
{
    char buf[256];
    MotOriName name;

    switch (motionKind[MOWORK(self)->ctrl.motion].rootUpdateMode) {
    default:
    case 12:
    case 13:
    case 14:
    case 18:
    case 19:
        sprintf(buf, "ASSERT");
        break;
    case 1:
        sprintf(buf, "XZ");
        break;
    case 20:
        sprintf(buf, "XZ_FIT");
        break;
    case 2:
        sprintf(buf, "XZ_MOTPOS");
        break;
    case 17:
        sprintf(buf, "XZ_MOTPOS_FIT");
        break;
    case 7:
        sprintf(buf, "UPPERWALLSOLUTION");
        break;
    case 16:
        sprintf(buf, "UPPERWALLSOL_FIT");
        break;
    case 8:
        sprintf(buf, "LOWERWALLSOLUTION");
        break;
    case 9:
        sprintf(buf, "CLIFFSOLUTION");
        break;
    case 10:
        sprintf(buf, "HANG");
        break;
    case 15:
        sprintf(buf, "HANG FIT");
        break;
    case 11:
        sprintf(buf, "WATER");
        break;
    case 5:
        sprintf(buf, "NODEFIX");
        break;
    case 4:
        sprintf(buf, "Y_ROPE");
        break;
    case 3:
        sprintf(buf, "Y");
        break;
    case 0:
        sprintf(buf, "TRUEMOTION");
        break;
    case 6:
        sprintf(buf, "DIRECTPLAY");
        break;
    }
    if (debug_window_flag != 0) {
        name = motionOriKind[idx];
        debug_PrintFontWindow(y, "%s \207 %s (%s)\n", &name,
                              motionKind[MOWORK(self)->ctrl.motion].name, buf);
    }
}

static inline void checkMotionKind(int i, int j) /* derived name */
{
    if (motionKind[i].blendKind != 320) {
        char buf[256];

        /* EUC-JP: "the node-blending motion (%s) uses a node-blending motion again" */
        debug_StdPrintfDummy(
            "ノードを混ぜるモーション(%s)が、\n再度ノードを混ぜるモーションを利用しています。\n",
            motionKind[j].name);
        sprintf(buf, "NODE BLEND MOTION \"%s\" REFERS\nNODE BLEND MOTION RECURSIVELY.\n",
                motionKind[j].name);
        debug_assertMessage(__FILE__, 152, buf);
        __assert(__FILE__, 152, "e");
    }
}

int GetNbMotionFrames(int id)
{
    int m;
    int n;

    if (motionKind[id].blendKind == 320) {
        return *motionTable[id];
    }
    m = blendMotionKind[motionKind[id].blendKind].motion;
    n = blendMotionKind[motionKind[id].blendKind].frames;
    checkMotionKind(m, id);
    if (n != -1) {
        return n;
    }
    return *motionTable[m];
}

float GetMotionPlaySpeedRatio(int id)
{
    int m;

    if (motionKind[id].blendKind == 320) {
        return motionKind[id].playSpeedRatio;
    }
    m = blendMotionKind[motionKind[id].blendKind].motion;
    checkMotionKind(m, id);
    return motionKind[m].playSpeedRatio;
}

static void execFrameTrigger(void *self)
{
    struct MotCtrl *w = &MOWORK(self)->ctrl;
    float t;

    t = (float)motionKind[w->motion].triggerStart;
    if (0.0f <= t) {
        if (w->trigger1Done == 0) {
            if (t < w->animFrame) {
                w->trigger1 = 1;
                w->trigger1Done = 1;
            } else {
                w->trigger1 = 0;
            }
        } else {
            w->trigger1 = 0;
        }
    }
    t = (float)motionKind[w->motion].trigger2Start;
    if (0.0f <= t) {
        if (w->trigger2Done != 0) {
            w->trigger2 = 0;
        } else if (t < w->animFrame) {
            w->trigger2 = 1;
            w->trigger2Done = 1;
        } else {
            w->trigger2 = 0;
        }
    }
}

/* inlined here and in UpdateFrameCounter; the store order is the
 * source's own */
static __inline__ void clearFrameTriggerState(void *self) /* derived name */
{
    GOBJ_SUB(self)->ctrl.trigger1 = 0;
    GOBJ_SUB(self)->ctrl.trigger2 = 0;
    GOBJ_SUB(self)->ctrl.trigger1Done = 0;
    GOBJ_SUB(self)->ctrl.trigger2Done = 0;
}

/* three range tests; shiftMotionData and UpdateFrameCounter both inline
 * them */
static __inline__ int checkFrameInRange(int mot, float t) /* derived name */
{
    if ((float)motionKind[mot].triggerStart <= t && t <= (float)motionKind[mot].triggerEnd) {
        return 1;
    }
    return 0;
}

static __inline__ int checkFrameInRange2(int mot, float t, float t2) /* derived name */
{
    if ((float)motionKind[mot].trigger2Start <= t2 && t <= (float)motionKind[mot].trigger2End) {
        return 1;
    }
    return 0;
}

static __inline__ int checkMotionShiftRange(int mot, float t, float t2) /* derived name */
{
    const MotionDef *e = &motionKind[mot];
    float a = (float)e->shiftStart;
    float b = (float)e->shiftLength;
    float ab = a + b;

    if (a < 0.0f || b < 0.0f) {
        return 0;
    }
    if (e->flags.bits.shiftInside) {
        if (a < t2 && t < ab) {
            return 1;
        }
        return 0;
    }
    if (a < t2 && t < ab) {
        return 0;
    }
    return 1;
}

int UpdateFrameCounter(void *self)
{
    char *m = (char *)MOWORK(self);
    struct MotCtrl *w = ICO_RAWP(struct MotCtrl *, m, 0x470, &MOWORK(self)->ctrl);
    int nf = GetNbMotionFrames(w->motion);
    float t;
    float r;
    float frame;

    if (w->justShifted != 0) {
        w->justShifted = 0;
    }
    w->frameEnd = 0;
    w->loopFlag = 0;
    if (motionFrameUpdate == 1) {
        t = w->speedRatio * motionKind[w->motion].playSpeedRatio * w->playRate *
            (60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]) * 0.5f);
        if (systemStatus[0] != 0) {
            t = t * motionKind[w->motion].palSpeedRatio;
        }
        if (w->waterDrag != 0) {
            switch (w->rootUpdateMode) {
            case 1:
            case 2:
            case 17:
            case 20:
                if (MOWORK(self)->skel != 0) {
                    float a = 1.0f - MOWORK(self)->ctrl.waterDepth * MOWORK(self)->scaleRatio;

                    if (a < 0.1f) {
                        a = 0.1f;
                    }
                    if (1.0f < a) {
                        a = 1.0f;
                    }
                    t = t * a;
                }
                break;
            }
        }
        w->lastFrame = w->animFrame;
        w->animFrame = w->animFrame + t;
        switch (motionKind[w->motion].playMode) {
        case 1:
            if ((float)(nf - 1) <= w->animFrame) {
                w->animFrame = w->animFrame - (float)(nf - 1);
                w->frameEnd = motionKind[w->motion].playMode;
                InitFrameDependSequence(ICO_RAWP(char *, m, 0x740, MOWORK(self)->fdsFlags));
                clearFrameTriggerState(self);
                if (motionKind[w->motion].flags.bits.loop != 0) {
                    w->loopFlag = 1;
                }
            }
            break;
        case 4:
            r = w->frameRatio;
            r = r < 0.0f ? 0.0f : (1.0f < r ? 1.0f : r);
            w->animFrame = (float)(nf - 1) * r;
            break;
        default:
            if ((float)(nf - 1) <= w->animFrame) {
                w->animFrame = w->lastFrame;
                w->frameEnd = 1;
            }
            break;
        }
        w->playTime =
            w->playTime +
            w->speedRatio * (60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]) * 0.5f);
        w->step = w->step + 1;
        if (!(w->blendFrames < w->blendCount)) {
            w->blendCount = w->blendCount + 1;
        }
        /* the two-frame range tests take a second frame value; here it is
           the same frame, held in a local */
        frame = w->animFrame;
        w->frameFlag1 = checkFrameInRange(w->motion, w->animFrame);
        w->frameFlag2 = checkFrameInRange2(w->motion, w->animFrame, frame);
        w->shiftReady = checkMotionShiftRange(w->motion, w->animFrame, frame);
    }
    return w->frameEnd;
}

inline MotionOrientEntry *GetMotionOrient(int i, int n, int id, int kind)
{
    int found = -1;

    for (; i < n; i++) {
        if (motionOrient[i].kind == kind) {
            if (motionOrient[i].id == id) {
                return &motionOrient[i];
            }
            if (motionOrient[i].id == 1146) {
                found = i;
            }
        }
    }
    if (found != -1) {
        return &motionOrient[found];
    }
    return 0;
}

inline MotionOrientEntry *getMotionOrient(int i, int n, int id, int kind)
{
    MotionOrientEntry *p = GetMotionOrient(i, n, id, kind);

    if (p != 0) {
        return p;
    }
    return &motionOrient[2467];
}

static void sendStateMail(void *self)
{
    float m[4][4];
    float pos[4];
    struct MotCtrl *w = &MOWORK(self)->ctrl;

    if (debug_wire_string != 0) {
        MatrixDrive_PushMatrix();
        sceVu0TransposeMatrix(m, matrixptr + 0x80);
        m[0][3] = m[1][3] = m[2][3] = 0.0f;
        sceVu0UnitMatrix(MatrixDrive_GetMatrix());
        GetRootPosition(pos, self);
        MatrixDrive_TransMatrixV((char *)pos);
        sceVu0MulMatrix(MatrixDrive_GetMatrix(), MatrixDrive_GetMatrix(), m);
    }
    if (w->flags & 0x2) {
        if (debug_wire_string != 0) {
            MatrixDrive_PushMatrix();
            MatrixDrive_TransMatrix(0.0f, 80.0f, 0.0f);
            DispWireString("HIT");
            MatrixDrive_PopMatrix();
        }
    }
    if (w->flags & 0x4) {
        iosOmSendMail(self, 7, self);
        if (debug_wire_string != 0) {
            MatrixDrive_PushMatrix();
            MatrixDrive_TransMatrix(-50.0f, 0.0f, 0.0f);
            DispWireString("FALL");
            MatrixDrive_PopMatrix();
        }
    }
    if (w->flags & 0x10) {
        iosOmSendMail(self, 8, self);
        if (debug_wire_string != 0) {
            MatrixDrive_PushMatrix();
            MatrixDrive_TransMatrix(50.0f, 40.0f, 0.0f);
            DispWireString("CLIFF");
            MatrixDrive_PopMatrix();
        }
    }
    if (w->flags & 0x20) {
        iosOmSendMail(self, 9, self);
        if (debug_wire_string != 0) {
            MatrixDrive_PushMatrix();
            MatrixDrive_TransMatrix(50.0f, 0.0f, 0.0f);
            DispWireString("WALL");
            MatrixDrive_PopMatrix();
        }
    }
    if (w->flags & 0x1000) {
        iosOmSendMail(self, 33, self);
        if (debug_wire_string != 0) {
            MatrixDrive_PushMatrix();
            MatrixDrive_TransMatrix(50.0f, 0.0f, 0.0f);
            DispWireString("UPPER WALL");
            MatrixDrive_PopMatrix();
        }
    }
    if (w->flags & 0x8) {
        iosOmSendMail(self, 10, self);
        if (debug_wire_string != 0) {
            MatrixDrive_PushMatrix();
            MatrixDrive_TransMatrix(0.0f, -80.0f, 0.0f);
            DispWireString("LANDING");
            MatrixDrive_PopMatrix();
        }
    }
    if (w->flags & 0x400) {
        iosOmSendMail(self, 26, self);
        if (debug_wire_string != 0) {
            MatrixDrive_PushMatrix();
            MatrixDrive_TransMatrix(0.0f, -80.0f, 0.0f);
            DispWireString("WATER");
            MatrixDrive_PopMatrix();
        }
    }
    if (w->flags & 0x800) {
        iosOmSendMail(self, 27, self);
    }
    if (w->flags & 0x100) {
        iosOmSendMail(self, 15, self);
    }
    if (w->flags & 0x200) {
        iosOmSendMail(self, 16, self);
    }
    if (debug_wire_string != 0) {
        MatrixDrive_PopMatrix();
    }
}

/* Both shift functions inline it: it walks the request table at the work
 * area's 0x1C and returns the paired entry from the table at 0x20. */
static inline int searchMotionShift(void *self, int id, int cur) /* derived name */
{
    struct MotCtrl *m = &MOWORK(self)->ctrl;
    int i;

    if (m->shiftReq != 0 && m->shiftNext != 0) {
        for (i = 0; (m->shiftReq)[i] != -1; i++) {
            if ((m->shiftReq)[i] == id) {
                if ((m->shiftNext)[i] == cur) {
                    return 0x479;
                }
                return (m->shiftNext)[i];
            }
        }
    }
    return -1;
}

/* mirrorMotionTable is six {request, substitute} pairs */
static __inline__ int searchAltMotion(int req) /* derived name */
{
    int i;

    for (i = 0; i < 6; i++) {
        if (mirrorMotionTable[i].req == req) {
            return mirrorMotionTable[i].alt;
        }
    }
    return req;
}

static void shiftMotionData(ICO_WORD_PTR(void *) obj, int motion, int request, int shiftMode)
{
    char *m = (char *)GOBJ_SUB(obj);
    struct MotCtrl *w = ICO_RAWP(struct MotCtrl *, m, 0x470, &GOBJ_SUB(obj)->ctrl);
    struct MotRoot *mw = ICO_RAWP(struct MotRoot *, m, 0xA0, &GOBJ_SUB(obj)->root);
    int mot;
    float frame;

    if (w->shiftReady != 0) {
        mot = searchAltMotion(motion);
    } else {
        mot = motion;
    }
    w->lastNoAlt = w->noAlt;
    w->noAlt = 0;
    if (mot == -1) {
        w->noAlt = 1;
        mot = motion;
    }
    w->lastMotion = w->motion;
    w->shiftFrame = (int)w->animFrame;
    w->blendFrames =
        (int)((float)shiftMode * ((float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f));
    mw->standNode = -1;
    mw->slopeIK = motionKind[mot].slopeIK;
    mw->noStepSearch = motionKind[mot].noStepSearch;
    mw->gravity = motionKind[mot].gravity;
    mw->handIK = motionKind[mot].modeBits.bits.handIK;
    mw->stairStep = motionKind[mot].flags.bits.stairStep;
    mw->radiusFrom = mw->radius;
    mw->radiusTo = motionKind[mot].clipRadius < 5.0f ? 5.0f : motionKind[mot].clipRadius;
    mw->lookIK = motionKind[mot].lookIK;
    mw->handTurnIK = motionKind[mot].handTurnIK;
    mw->fuchiMode = motionKind[mot].modeBits.bits.fuchiMode;
    mw->fieldWall = motionKind[mot].fieldWall;
    mw->cylinder = motionKind[mot].cylinder;
    mw->avgWallPlane = motionKind[mot].flags.bits.avgWallPlane;
    mw->flag330 = motionKind[mot].flags2.bits.dropNode4Turn;
    w->updateModeChanged = 0;
    if (w->keepUpdateMode == 0) {
        if (w->rootUpdateMode != motionKind[mot].rootUpdateMode) {
            w->updateModeChanged = 1;
            w->rootUpdateMode = motionKind[mot].rootUpdateMode;
        }
    }
    w->parallelEnded = 0;
    if (w->parallel != 0) {
        if (motionKind[mot].flags.bits.parallel == 0) {
            w->parallelEnded = 1;
        }
    }
    w->parallel = motionKind[mot].flags.bits.parallel;
    w->motion = mot;
    w->request = request;
    w->blendCount = 1;
    w->step = 0;
    w->animFrame = 0.0f;
    w->lastFrame = w->animFrame;
    w->justShifted = 1;
    frame = w->animFrame;
    w->frameFlag1 = checkFrameInRange(mot, w->animFrame);
    w->frameFlag2 = checkFrameInRange2(w->motion, w->animFrame, frame);
    w->shiftReady = checkMotionShiftRange(w->motion, w->animFrame, frame);
    w->shifted = 1;
    w->frameEnd = 0;
    w->contactFlags = 0;
    if (motionKind[mot].flags.bits.loop != 0) {
        w->loopFlag = 1;
        if (motionKind[w->lastMotion].flags.bits.loop == 0) {
            w->posReserve = 0;
        }
    } else {
        w->loopFlag = 0;
        w->posReserve = 0;
    }
}

static void shiftMotionOrientEndFunc(void *self)
{
    struct MotCtrl *w = &MOWORK(self)->ctrl;
    int x;

    if (w->seGroup[0] == -1) {
        /* EUC-JP: "the SE internal processing seems wrong for some reason; report it to Sugiyama" */
        debug_StdPrintfDummy(
            "何らかの理由でSEの内部処理がおかしいようです。杉山に報告してください。\n");
        debug_assert(__FILE__, 745);
        __assert(__FILE__, 745, "0");
    }
    StopSEPackageWithGroupVariation(self, 0);
    StopSEPackageWithGroupVariation(self, 1);
    w->lastSlipFlags = 0;
    x = w->rootUpdateMode;
    if (x < 9) {
        if (x >= 7) {
            goto ok;
        }
        return;
    }
    if (x != 16) {
        return;
    }
ok:
    if (motionKind[w->motion].wallFeedback == 0) {
        return;
    }
    FeedbackWallWorkInfoToBrainSystem(self);
}

inline void CopyBlendMotionDataSource(void *self, short ang)
{
    float quat[4];
    StreamElem *mot = MOWORK(self)->blendBuf;
    int i = 0;

    CopyMotion(mot, MOWORK(self)->motionBuf, MOWORK(self)->skelNodeNum);
    CopyVector(MOWORK(self)->localPos, MOWORK(self)->motionPos);
    CopyVector(MOWORK(self)->localMove, MOWORK(self)->root.move);
    MOWORK(self)->localHeight = MOWORK(self)->root.height;
    MOWORK(self)->local = MOWORK(self)->parent;
    while (MOWORK(self)->skel[i].parent == -1) {
        SetQuaternionByAxisRotate(quat, ang, 0.0f, 1.0f, 0.0f);
        MultiQuaternion(mot[i].q, quat, mot[i].q);
        i = MOWORK(self)->skel[i].sibling;
    }
}

static void shiftMotionOrientBeginFunc(void *self, int motion, int request, int shiftMode)
{
    Vec16 v;
    char *m = (char *)MOWORK(self);
    struct MotCtrl *w = ICO_RAWP(struct MotCtrl *, m, 0x470, &MOWORK(self)->ctrl);
    struct MotRoot *p = ICO_RAWP(struct MotRoot *, m, 0xA0, &MOWORK(self)->root);
    short ang;

    debug_StdPrintfDummy(
        "Change \"\033[33m%s(%d)\033[m\"in \"\033[33m%s(%d)\033[m\" at control \"\033[33m%s\033[m\".\n",
        motionOriKind[w->orientKind].s, w->orientKind, motionKind[w->motion].name, w->motion, "");
    shiftMotionData((ICO_WORD_PTR(void *))self, motion, request, shiftMode);
    if (w->parallelEnded != 0) {
        GetOutOutsideOfWall(self, p->radius);
    }
    memset(&v, 0, 16);
    ang = 0;
    v.f[2] = 1.0f;
    if (w->rootUpdateMode != 0 && w->rootUpdateMode != 6) {
        int kind = motionKind[w->lastMotion].adjustAngle;

        if (motionKind[w->motion].flags.bits.adjustRoot || kind != 0) {
            sceVu0UnitMatrix(MatrixDrive_GetMatrix());
            if (kind != 0 && kind != 1) {
                ang = (short)(kind * 32768 / 180);
                debug_StdPrintfDummy("ADJUST FROM TABLE \033[36m%08x(%f)\033[m\n", ang,
                                     (float)ang * 180.0f / 32768.0f);
            } else {
                float mtx[4][4];

                GetMatrixFromQuaternion((char *)mtx, p->motionQuat);
                sceVu0ApplyMatrix(&v, mtx, &v);
                v.f[1] = 0.0f;
                _NormalizeVector(&v, &v);
                ang = GetTableArcTan2(v.f[0], v.f[2]);
                debug_StdPrintfDummy("ADJUST \033[36m%08x(%f)\033[m\n", ang,
                                     (float)ang * 180.0f / 32768.0f);
            }
            MatrixDrive_RotMatrixY(ang);
            sceVu0ApplyMatrix(w->dir, MatrixDrive_GetMatrix(), w->dir);
        } else {
            debug_StdPrintfDummy("ROT DIFF %08x(%f)\n", 0, (float)ang);
        }
    }
    CopyBlendMotionDataSource(self, ang);
    StopFDSVibration(MOWORK(self)->fdsFlags);
    InitFrameDependSequence(MOWORK(self)->fdsFlags);
    clearFrameTriggerState(self);
}

void ForTest_ForceShiftMotion(ICO_WORD_PTR(void *) obj, int motion)
{
    shiftMotionData(obj, motion, motion, 0);
}

/* Clear the pending-shift words and report whether the entry may start.
 * The 0x479 arm's refusal goes to the guard's `return 0`. */
static inline int checkMotionShiftReady(struct MotCtrl *m, MotionOrientEntry *p) /* derived name */
{
    int kind;

    m->ctrlFlags = 0;
    m->shifted = 0;
    if (m->orientUpdateOff != 0 || p->kind == -1) {
    fail:
        return 0;
    }
    kind = p->nextId;
    m->orientReq = kind;
    if (kind == 0x479) {
        if (m->frameEnd == 0) {
            goto fail;
        }
        m->ctrlFlags = 4;
        return 1;
    }
    if (m->frameEnd != 0) {
        m->animFrame = (float)(GetNbMotionFrames(m->motion) - 1) - 1.0e-6f;
        m->ctrlFlags |= 0x10;
        return 1;
    }
    return p->shiftFrom != -1 && (float)p->shiftFrom < m->animFrame;
}

static int normalMotionShift(void *self, int force)
{
    struct MotCtrl *w = &MOWORK(self)->ctrl;
    MotionOrientEntry *p = getMotionOrient(w->oriFrom, w->oriTo, w->request, w->orientKind);

    if (force == 0) {
        if (p->id == 1146) {
            return 0;
        }
    }
    if (checkMotionShiftReady(&MOWORK(self)->ctrl, p) != 0) {
        int kind = p->nextId;
        int mode = p->shiftMode;
        int next;
        int r = -1; /* no pairing found; the else arm searches with it as cur */

        if (kind == 0x479) {
            kind = p->id;
            mode = 5;
            r = searchMotionShift(self, kind, w->motion);
            if (r == -1 || r == 0x479) {
                return 0;
            }
            next = r;
        } else {
            r = searchMotionShift(self, kind, r);
            next = (r == -1) ? kind : r;
        }
        shiftMotionOrientEndFunc(self);
        shiftMotionOrientBeginFunc(self, next, kind, mode);
        return 1;
    }
    return 0;
}

/* parallelMotionOrient is 54 rows of five words, keyed on the current and
 * the requested motion. */
static inline MotionOrientEntry *findParallelMotion(int cur, int next) /* derived name */
{
    int i;

    if (cur == next) {
        return &motionOrient[2467];
    }
    for (i = 0; i < 54; i++) {
        if (parallelMotionOrient[i].id == cur) {
            if (parallelMotionOrient[i].kind == next) {
                return (MotionOrientEntry *)&parallelMotionOrient[i];
            }
        }
    }
    return 0;
}

/* the -1 test encloses the body and every refusal falls to the one
 * `return 0` at the end */
static int parallelMotionShift(void *self)
{
    struct MotCtrl *m = &MOWORK(self)->ctrl;
    int next = searchMotionShift(self, m->request, m->motion);
    MotionOrientEntry *p;

    if (next != -1) {
        p = findParallelMotion(m->motion, next);

        if (p != 0) {
            if (checkMotionShiftReady(&MOWORK(self)->ctrl, p) != 0) {
                shiftMotionOrientEndFunc(self);
                shiftMotionOrientBeginFunc(self, p->nextId, m->request, p->shiftMode);
                return 1;
            }
        } else if (next != 0x479) {
            MotionOrientEntry e = {m->motion, m->orientKind, next, m->shiftFrom, m->shiftMode};

            if (checkMotionShiftReady(&MOWORK(self)->ctrl, &e) != 0) {
                shiftMotionOrientEndFunc(self);
                shiftMotionOrientBeginFunc(self, next, m->request, m->shiftMode);
                return 1;
            }
        }
    }
    return 0;
}

/* The ignored-request report: how many times in a row, and for which
   motion. */
static int ignoreCount = 0; /* derived name */

static int ignoreMotion = 0; /* derived name */

/* The four one-character spinners the debug line cycles with the frame count. */
typedef struct MotOriSpin { /* field names derived */
    char *s[4];
} MotOriSpin; /* derived name */

char *SetMotionRequest(void *self, int mot, MotOriReq req)
{
    char *w = ICO_RAWP(char *, GOBJ_SUB(self), 0x470, (char *)&GOBJ_SUB(self)->ctrl);
    struct MotCtrl *c = &GOBJ_SUB(self)->ctrl;
    int old = ICO_RAW(int, w, 0xD0, c->orientKind);

    ICO_RAW(int, w, 0xD0, c->orientKind) = mot;
    if (normalMotionShift(self, 1) != 0) {
        ICO_RAW(int, w, 0x44, *(int *)&c->playTime) = 0;
        switch (ICO_RAW(int, w, 0x68, c->rootUpdateMode)) {
        case 3:
        case 7:
        case 8:
        case 9:
        case 10:
        case 13:
        case 15:
        case 16:
            *(MotOriReq *)&GOBJ_SUB(self)->root.wall = req;
            debug_StdPrintfDummy("\033[36mUpdate with collision info that act memorized.\033[m\n");
            break;
        }
        if ((debug_mot_debug_target == 0 && self == boyGObj) ||
            (debug_mot_debug_target == 1 && self == girlGObj) ||
            (debug_mot_debug_target == 2 && self == isysGObjSearchFromObjKindID_begin(32)) ||
            (debug_mot_debug_target == 3 && self == isysGObjSearchFromObjKindID_begin(4)) ||
            (debug_mot_debug_target == 4 && self == isysGObjSearchFromObjKindID_begin(47))) {
            if (ignoreCount != 0 && debug_window_flag != 0) {
                debug_PrintFontWindow(0xC0FF20, "\n ");
            }
            orientDebug(self, mot, 0xFFFFFF80);
            ignoreCount = 0;
        }
    } else {
        ICO_RAW(int, w, 0xD0, c->orientKind) = old;
        if (pad[0].now & 0x2) {
            if ((debug_mot_debug_target == 0 && self == boyGObj) ||
                (debug_mot_debug_target == 1 && self == girlGObj) ||
                (debug_mot_debug_target == 2 && self == isysGObjSearchFromObjKindID_begin(32)) ||
                (debug_mot_debug_target == 3 && self == isysGObjSearchFromObjKindID_begin(4)) ||
                (debug_mot_debug_target == 4 && self == isysGObjSearchFromObjKindID_begin(47))) {
                MotOriSpin spin = {{"-", "\\", "|", "/"}};

                if (ignoreMotion != mot && ignoreCount != 0) {
                    ignoreCount = 0;
                    if (debug_window_flag != 0) {
                        debug_PrintFontWindow(0xC0FF20, "\n ");
                    }
                }
                if (debug_window_flag != 0) {
                    MotOriName name = motionOriKind[mot];

                    debug_PrintFontWindow(
                        0x3080FF20, "%s %s at %s ignore %d times", spin.s[frame_count & 3], name.s,
                        motionKind[ICO_RAW(int, w, 0x30, c->motion)].name, ++ignoreCount);
                }
                ignoreMotion = mot;
            }
        }
    }
    /* PC port: the callers read the result as the motion control block
       (struct MotCtrl); GOBJ_SUB + 0x470 is that block on the EE only */
    return (char *)c;
}

inline void SetParallelMotionTableWithNoRequest(void *self, int *next, int *req)
{
    struct MotCtrl *m = &GOBJ_SUB(self)->ctrl;

    if (m->shiftStop == 0) {
        m->shiftReq = req;
        m->shiftNext = next;
    }
}

inline void SetParallelMotionTable(void *self, int *next, int *req, int from, int mode)
{
    struct MotCtrl *m = &GOBJ_SUB(self)->ctrl;
    int old = m->orientKind;

    m->orientKind = 269;
    SetParallelMotionTableWithNoRequest(self, next, req);
    m->shiftFrom = from;
    m->shiftMode = mode;
    if (parallelMotionShift(self) != 0) {
        m->playTime = 0;
    } else {
        m->orientKind = old;
    }
}

static void getNodeBlendedFloatingMotion(StreamElem *dst, float *root, int id, int n,
                                         unsigned char *mask, void *self, float t)
{
    float v[4];
    StreamElem mot[n];
    int i;
    int j;
    int prev = -1;
    SkelNode *skel = MOWORK(self)->skel;

    for (i = 0, j = motionKind[id].blendKind; blendMotionKind[j].motion != 0x47B; i++, j++) {
        int node = blendMotionKind[j].motion;

        checkMotionKind(node, id);
        if (i == 0) {
            GetFloatingMotion(dst, t, root, motionTable[node], n, mask, skel);
            CopyMotion(mot, dst, n);
            prev = node;
        } else {
            int fn;

            if (prev != node) {
                GetFloatingMotion(mot, t * blendMotionKind[j].rate, v, motionTable[node], n, mask,
                                  skel);
                prev = node;
            }
            fn = GetSkeltonFocusNode(self, blendMotionKind[j].node);
            if (fn != -1) {
                CopyMotionWithNodeHrc(dst, mot, skel, fn, 0);
            }
        }
    }
    if (i == 0) {
        /* EUC-JP: "(%s) the motion to blend is not defined" */
        debug_StdPrintfDummy("(%s)混ぜるモーションが定義されていません。\n", motionKind[id].name);
        debug_assert(__FILE__, 1228);
        __assert(__FILE__, 1228, "0");
    }
}

/* the slope vector getMotionGeometry normalises for its pitch angle */
static sceVu0FVECTOR slopeVector = {0.0f, 0.0f, 0.0f, 0.0f}; /* derived name */

/* getMotionGeometry is its only caller */
static inline void getMotionRootPos(struct MotCtrl *w, float *v) /* derived name */
{
    int m = blendMotionKind[motionKind[w->motion].blendKind].motion;
    float t = w->lastFrame;

    checkMotionKind(m, w->motion);
    GetFloatingMotionRootPos(v, motionTable[m], t);
}

/* The motion-loaded assert and the node-count assert, each an inlined body
 * of its own with its own 1024-byte message buffer. */
static inline void assertMotionLoaded(struct MotCtrl *w, int *md) /* derived name */
{
    if (md == 0) {
        char buf[1024];

        sprintf(buf, "THE MOTION \"%s\"(%d)\nDID NOT LOAD IN THIS STAGE\nOR IS INVALID ID.\n",
                motionKind[w->motion].name, w->motion);
        debug_assertMessage(__FILE__, 1275, buf);
        __assert(__FILE__, 1275, "e");
    }
}

static inline void assertMotionNodeCount(struct MotCtrl *w, int *md, int n) /* derived name */
{
    int *e = ICO_EEPTR(int *, md[3]);
    int i = 0;

    do {
        i++;
    } while (*e++ != 0);
    if (i - 1 != n) {
        char buf[1024];

        sprintf(
            buf,
            "THE NUMBER OF NODE DATAS OF MOTION\n\"%s\"(%d SKELTONS) DOES NOT MATCH\nTHE NUMBER OF SKELTON NODES(%d SKELTONS)\n",
            motionKind[w->motion].name, i - 1, n);
        debug_assertMessage(__FILE__, 1287, buf);
        __assert(__FILE__, 1287, "e");
    }
}

/* a nested function of getMotionGeometry, inlined into the arm that uses it */
static inline void rotateNodes(char *m, SkelNode *s, void *q) /* derived name */
{
    int i = 0;

    do {
        MultiQuaternion(m + i * 32 + 16, q, m + i * 32 + 16);
        i = s[i].sibling;
    } while (i != -1);
}

static void getMotionGeometry(void *self)
{
    SkelNode *skel = MOWORK(self)->skel;
    struct MotRoot *mo = &MOWORK(self)->root;
    struct MotCtrl *w = &MOWORK(self)->ctrl;
    int n = MOWORK(self)->skelNodeNum;
    char *blendless = MOWORK(self)->blendless;
    char mot[n * 32];
    float scale = MOWORK(self)->nodes->scale[0];
    int *md = motionTable[w->motion];

    if (motionKind[w->motion].blendKind == 320) {
        assertMotionLoaded(w, md);
        assertMotionNodeCount(w, md, n);
    }
    RegularizeQuaternion(MOWORK(self)->root.quat);
    MatrixDrive_PushMatrix();
    {
        Vec16 v;
        Vec16 rv;

        /* PC port: neither root-position call writes w (see below); 0
           rather than whatever the host's frame left there (issue 19) */
        v.f[3] = 0.0f;
        rv.f[3] = 0.0f;
        if (motionKind[w->motion].blendKind == 320) {
            GetFloatingMotion((StreamElem *)mot, w->animFrame, v.f, md, n, blendless, skel);
            GetFloatingMotionRootPos(rv.f, md, w->lastFrame);
        } else {
            getNodeBlendedFloatingMotion((StreamElem *)mot, v.f, w->motion, n, blendless, self,
                                         w->animFrame);
            getMotionRootPos(w, rv.f);
        }
        if (w->motion == 102) {
            v.f[0] = v.f[0] + 1.0f;
        }
        sceVu0ScaleVector(&v, &v, scale);
        /* PC port: a root position blended between two frames
           (GetBlendedMotionRootPos) gets x, y and z only, so w is the stack
           word an earlier call left in v (and in rv). VU0 scales an
           exponent-255 word as a number, saturating at +-Fmax, and the root
           update's sceVu0ApplyMatrix multiplies it by the zero translation
           row, so rootMove's x, y and z are the rotation alone; a host NaN
           there gave NaN in all three (a shadow carrying the girl, stage 4).
           Read it as the PS2 does. */
        v.f[3] = ps2_operand(v.f[3]);
        if (w->animFrame < w->lastFrame) {
            CopyVector(mo->step, ZeroVector);
        } else {
            sceVu0ScaleVector(&rv, &rv, scale);
            rv.f[3] = ps2_operand(rv.f[3]);
            sceVu0SubVector(mo->step, &v, &rv);
        }
        if (w->noAlt != 0) {
            MakeMirrorMotion((StreamElem *)mot, skel);
        }
        if (w->catchBoy != 0) {
            SlopeIKControl(self, mot, v.f, (Vec4 *)mo->step, n);
        }
        {
            Vec16 fv;
            Vec16 tv;
            Vec16 tmp;
            float len;
            float c;
            float ang;
            float d;
            float r;
            int flag;
            int k;

            len = VectorLength(mo->move) *
                  ((float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f);
            CopyVector(&fv, w->dir);
            CopyVector(&tv, w->lastDir);
            if (MOWORK(self)->parent.obj != 0) {
                sceVu0ApplyMatrix(&fv,
                                  (char *)MOWORK(MOWORK(self)->parent.obj)->nodeMtx +
                                      MOWORK(self)->parent.node * 0x40,
                                  &fv);
                sceVu0ApplyMatrix(&tv,
                                  (char *)MOWORK(MOWORK(self)->parent.obj)->nodeMtx +
                                      MOWORK(self)->parent.node * 0x40,
                                  &tv);
            }
            d = tv.f[0] * fv.f[0] + tv.f[2] * fv.f[2];
            if (1.0f < d) {
                d = 1.0f;
            }
            c = d < -1.0f ? -1.0f : d;
            ang = acosf(c) *
                  ((float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f * 10430.378f);
            if (0.0f < tv.f[0] * fv.f[2] - tv.f[2] * fv.f[0]) {
                ang = -ang;
            }
            r = ang * 0.1f * len * len;
            if (6144.0f < r) {
                r = 6144.0f;
            }
            if (r < -6144.0f) {
                r = -6144.0f;
            }
            if (debug_now_motion_viewer == 0) {
                float t = 60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]) * 0.2f;

                mo->twist = (short)((float)mo->twist * (1.0f - t)) + r * t;
            }
            if (w->rootUpdateMode == 20) {
                Vec16 up;
                float mtx[4][4];
                Vec16 rot;
                short a;

                tmp = mo->plane;
                GetMatrixFromQuaternion((char *)mtx, mo->quat);
                _ApplyMatrix(&up, mtx, ZUnitVector);
                tmp.f[3] = 0.0f;
                slopeVector[1] = GetYDistanceFromPlane(tmp.f, up.f);
                slopeVector[2] = 1.0f;
                _NormalizeVector(slopeVector, slopeVector);
                a = GetTableArcTan2(slopeVector[1], slopeVector[2]);
                SetIdentityQuaternion(&rot);
                RotQuaternionX(&rot, a);
                rotateNodes(mot, skel, &rot);
            }
            flag = 0;
            if (debug_motion_interporate != 0) {
                flag = w->blendFrames >= w->blendCount;
            }
            k = motionKind[w->motion].stepNode;
            if (flag != 0) {
                /* PC port: shiftMotionData sets blendFrames to 0 for a
                   shift shorter than one frame at this rate; the guard
                   then passes only with blendCount 0 (a record not yet
                   shifted), where 0 / 0 is +Fmax on the EE and NaN under
                   IEEE */
                float s = ps2_div((float)w->blendCount, (float)w->blendFrames);

                GetBlendedMotion(MOWORK(self)->motionBuf, tmp.f, (StreamElem *)mot, v.f,
                                 MOWORK(self)->blendBuf, MOWORK(self)->localPos, s, blendless, n);
                mo->radius = mo->radiusFrom * (1.0f - s) + mo->radiusTo * s;
                GetGeometryOfMotion(self, mot, MOWORK(self)->motionBuf, v.f, s, mo->step, k);
            } else {
                CopyMotion(MOWORK(self)->motionBuf, (StreamElem *)mot, n);
                mo->radius = mo->radiusTo;
                GetGeometryOfMotion(self, mot, MOWORK(self)->motionBuf, v.f, 1.0f, mo->step,
                                    w->justShifted ? k : -1);
            }
            SetIdentityQuaternion(&tmp);
            RotQuaternionX(&tmp, -32768);
            RotQuaternionY(&tmp, -32768);
            MultiQuaternion(&tmp, &tmp, skel->quat);
            GetInverseQuaternion(&tmp, &tmp);
            MultiQuaternion(mo->motionQuat, mot + 16, &tmp);
            CopyVector(w->lastDir, w->dir);
        }
    }
    MatrixDrive_PopMatrix();
    if (w->mailDelay <= 0) {
        w->mailDelay = w->mailDelay + 1;
    } else {
        sendStateMail(self);
    }
    DispSkelton(self, mot);
    ExecFrameDependSequence(self);
    execFrameTrigger(self);
    UpdateFrameCounter(self);
    if (stage_no == 16 && MOWORK(self)->ctrl.wallHit != 0) {
        void *g = isysGObjSearchFromObjLayoutID(865);

        if (g != 0) {
            void *o;
            void *t;

            if ((void *)MOWORK(self)->root.wall.o.obj == g) {
                g = isysGObjSearchFromObjLayoutID(866);
            }
            o = MOWORK(g)->parent.obj;
            t = (void *)MOWORK(self)->root.wall.o.obj;
            if (((GObj *)o)->kind == 17) {
                if (o == t) {
                    MOWORK(self)->ctrl.wallFloorHeight = 3.40282347e+38f;
                    MOWORK(self)->ctrl.wallTopHeight = 3.40282347e+38f;
                }
            }
        }
    }
}

/* The two line colours the debug bar graph draws with: 16-byte records,
 * 8-byte aligned; ico2/omori/src/camera-editor.c spells the union the same
 * way (BoxCol4). */
typedef union { /* field names derived */
    unsigned int c[4];
    unsigned long long w[2];
} MotOriCol4; /* derived name */

/* debug_bar_flag is debug.o's debug bar mode, which ico2/common/src/debug.c
 * clears and debug_DrawBar dispatches on 1 and 2; this file reads it as an
 * enumerated mode. */
enum DebugDisplayMode {
    DEBUG_DISPLAY_OFF,
    DEBUG_DISPLAY_ON,
    DEBUG_DISPLAY_FULL
}; /* derived name */

extern enum DebugDisplayMode debug_bar_flag;

static void getShapeGeometry(void *self)
{
    struct MotCtrl *m = &GOBJ_SUB(self)->ctrl;

    if (motionKind[m->motion].blendKind == 320) {
        void *mot = motionTable[m->motion];

        if (CheckMotionIncludeFacialData(mot) == 0) {
            int n = *ICO_EEPTR(int *, *(int *)((char *)mot + 0x10));
            float buf[n];
            int i;

            GetFloatingShapeMotion(buf, mot, m->animFrame, n);
            if (n != 0) {
                int cnt = GOBJ_SUB(self)->morphNum;

                if (cnt != 0) {
                    if (cnt != n) {
                        /* EUC-JP: "the shape motion data and the number of targets differ; motion: %d target: %d" */
                        debug_StdPrintfDummy(
                            "シェイプモーションデータとターゲットの数が違います。\nモーション:%d ターゲット:%d\n",
                            n, cnt);
                    }
                    for (i = 0; i < n; i++) {
                        float x = buf[i] * 0.01f;
                        int *p = (int *)(i * 4 + ICO_RAW(int, GOBJ_SUB(self), 0x838,
                                                         (ICO_WORD)GOBJ_SUB(self)->morphWeight));

                        if (x < 0.0f) {
                            if (0.0001f < -x) {
                                *(float *)p = x;
                            } else {
                                *p = 0;
                            }
                        } else {
                            if (0.0001f < x) {
                                *(float *)p = x;
                            } else {
                                *p = 0;
                            }
                        }
                    }
                } else {
                    /* EUC-JP: "no place is reserved to store the handover data" */
                    debug_StdPrintfDummy("受渡しデータを格納する場所が確保されていません。\n");
                }
                if (debug_bar_flag != 0) {
                    gif_StartPacketPri(11);
                    {
                        int p1[4];
                        int p2[4];
                        MotOriCol4 c0 = {{64, 32, 0, 128}};
                        MotOriCol4 c1 = {{255, 128, 0, 128}};

                        gif_SetAlpha(1, 5, 128);
                        for (i = 0; i < n; i++) {
                            p1[0] = (ScreenWidth / 2 + 1824) << 4;
                            p2[0] = (ScreenWidth / 2 + 2024) << 4;
                            p1[1] = p2[1] = (ScreenHeight / 2 - n * 6 + 2024 + i * 6) << 4;
                            Draw2DLine(p1, p2, (int *)c0.c, -1);
                            p1[0] = p2[0] = (ScreenWidth / 2 + (int)buf[i] + 1924) << 4;
                            p1[1] = (ScreenHeight / 2 - n * 6 + 2025 + i * 6) << 4;
                            p2[1] = (ScreenHeight / 2 - n * 6 + 2028 + i * 6) << 4;
                            Draw2DLine(p1, p2, (int *)c1.c, -1);
                        }
                        gif_EndPacket();
                    }
                }
            }
        }
    }
}

/* both stream-geometry functions inline it (once and twice) */
static inline int getStreamVec(void *self, void *sm, float *v, void *mot) /* derived name */
{
    float s = MOWORK(self)->nodes->scale[0];

    if (GetStreamMotion(mot, v, sm, MOWORK(self)->skel) != 0) {
        if (MOWORK(self)->streamScale != 0) {
            _ScaleVectorXYZ(v, v, s);
        }
        v[0] -= MOWORK(self)->streamOfs[0];
        v[1] -= MOWORK(self)->streamOfs[1];
        v[2] += MOWORK(self)->streamOfs[2];
        return 1;
    }
    return 0;
}

static void getStreamMotionGeometry(void *self, void *sm)
{
    float v[4];
    StreamElem mot[MOWORK(self)->skelNodeNum];

    if (getStreamVec(self, sm, v, mot)) {
        GetGeometryOfMotion(self, mot, mot, v, 1.0f, ZeroVector, -1);
        CopyMotion(MOWORK(self)->motionBuf, mot, MOWORK(self)->skelNodeNum);
        CopyVector(MOWORK(self)->motionPos, v);
        DispSkelton(self, mot);
    }
}

static void getStreamBlendMotionGeometry(void *self, void *sm0, void *sm1, float t)
{
    float v0[4];
    float v1[4];
    float v2[4];
    int n = MOWORK(self)->skelNodeNum;
    StreamElem mot0[n];

    if (getStreamVec(self, sm0, v0, mot0)) {
        StreamElem mot1[n], mot2[n];

        getStreamVec(self, sm1, v1, mot1);
        GetBlendedMotion(mot2, v2, mot1, v1, mot0, v0, t, MOWORK(self)->blendless, n);
        GetGeometryOfMotion(self, mot2, mot2, v2, 1.0f, ZeroVector, -1);
        CopyMotion(MOWORK(self)->motionBuf, mot2, MOWORK(self)->skelNodeNum);
        CopyVector(MOWORK(self)->motionPos, v2);
        DispSkelton(self, mot2);
    }
}

/* The clamp and its own `int *p` declaration, a macro with a block body
 * shared by getStreamBlendShapeGeometry and getStreamShapeGeometry.  The test
 * is this programmer's absolute-value idiom (box.c, geometryManager.c,
 * motionManager.c). */
#define SET_SHAPE_VALUE(self, i, x) /* derived name */                                             \
    {                                                                                              \
        int *p = (int *)((i) * 4 + ICO_RAW(int, GOBJ_SUB(self), 0x838,                             \
                                           (ICO_WORD)GOBJ_SUB(self)->morphWeight));                \
                                                                                                   \
        if (0.0001f < ((x) < 0.0f ? -(x) : (x))) {                                                 \
            *(float *)p = (x);                                                                     \
        } else {                                                                                   \
            *p = 0;                                                                                \
        }                                                                                          \
    }

static void getStreamBlendShapeGeometry(void *self, void *sm0, void *sm1, float t)
{
    int i;
    int n = GOBJ_SUB(self)->morphNum;
    if (n != 0) {
        float a[n];
        if (GetStreamShapeMotion(a, sm0) != 0) {
            float b[n];

            GetStreamShapeMotion(b, sm1);
            for (i = 0; i < n; i++) {
                float x;
                x = (a[i] * (1.0f - t) + b[i] * t) * 0.01f;
                SET_SHAPE_VALUE(self, i, x);
            }

        } else
            for (i = 0; i < n; i++)
                *(int *)(ICO_RAW(int, GOBJ_SUB(self), 0x838,
                                 (ICO_WORD)GOBJ_SUB(self)->morphWeight) +
                         i * 4) = 0;
    }
}

static void getStreamShapeGeometry(void *self, void *sm)
{
    int n = GOBJ_SUB(self)->morphNum;

    if (n != 0) {
        float buf[n];
        int i;

        if (GetStreamShapeMotion(buf, sm) != 0) {
            for (i = 0; i < n; i++) {
                float x = buf[i] * 0.01f;
                SET_SHAPE_VALUE(self, i, x);
            }
        } else {
            for (i = 0; i < n; i++) {
                *(int *)(ICO_RAW(int, GOBJ_SUB(self), 0x838,
                                 (ICO_WORD)GOBJ_SUB(self)->morphWeight) +
                         i * 4) = 0;
            }
        }
    }
}

static void getStreamMotion(void *self)
{
    int s = MOWORK(self)->ctrl.stream;
    char a[GetDataSizeOfStreamMotion(s)];
    float t = GetStreamMotionData(a, s);

    if (t <= 0.0f) {
        getStreamMotionGeometry(self, a);
        getStreamShapeGeometry(self, a);
    } else {
        char b[GetDataSizeOfStreamMotion(s)];

        GetStreamMotionDataNext(b, s);
        getStreamBlendMotionGeometry(self, a, b, t);
        getStreamBlendShapeGeometry(self, a, b, t);
    }
}

void ExecMotionOrient(void *self)
{
    struct MotCtrl *w = &GOBJ_SUB(self)->ctrl;

    if (w->shiftStop != 0) {
        /* EUC-JP: "the motion replacement function is stopped" */
        debug_StdPrintfDummy("\033[36mモーション置き換え機能が停止しています。\033[m: %p\n", self);
    }
    if (w->stream == -1) {
        if (w->orientKind != 269) {
            normalMotionShift(self, 0);
        } else {
            parallelMotionShift(self);
        }
        if ((debug_mot_debug_target == 0 && self == boyGObj) ||
            (debug_mot_debug_target == 1 && self == girlGObj) ||
            (debug_mot_debug_target == 2 && self == isysGObjSearchFromObjKindID_begin(32)) ||
            (debug_mot_debug_target == 3 && self == isysGObjSearchFromObjKindID_begin(4)) ||
            (debug_mot_debug_target == 4 && self == isysGObjSearchFromObjKindID_begin(47))) {
            if (w->shifted != 0) {
                if (ignoreCount != 0 && debug_window_flag != 0) {
                    debug_PrintFontWindow(0xC0FF20, "\n ");
                }
                orientDebug(self, w->orientKind, 0xE0FF20);
                ignoreCount = 0;
            }
        }
        if (w->justShifted != 0) {
            int n = GOBJ_SUB(self)->morphNum;
            int i;

            for (i = 0; i < n; i++) {
                *(int *)(i * 4 + ICO_RAW(int, GOBJ_SUB(self), 0x838,
                                         (ICO_WORD)GOBJ_SUB(self)->morphWeight)) = 0;
            }
        }
        getMotionGeometry(self);
        getShapeGeometry(self);
    } else {
        getStreamMotion(self);
    }
}

void SetNodeRotationLimitDataTable(void *self, int from, int to)
{
    int i;

    for (i = from; i < to;) {
        MotOriLimit tmp;
        int node = GetSkeltonFocusNode(self, motionLimitDef[i].node);

        if (node < 0) {
            /* EUC-JP: "the node the node rotation limit data names is not in the skeleton" */
            debug_StdPrintfDummy(
                "ノード回転リミットデータの示すノードがスケルトン中にありません。\n");
            debug_assert(__FILE__, 1838);
            __assert(__FILE__, 1838, "0");
        }
        /* a 4-byte word cannot hold the row's address: the row, one-based
           (motMan_getFinalMatrix.c.inc reads it back) */
        MOWORK(self)->nodeLimit[node] = i + 1;
        if (motionLimitDef[i].mid.y < motionLimitDef[i + 2].mid.y) {
            tmp = motionLimitDef[i];
            *(MotOriLimit *)&motionLimitDef[i] = motionLimitDef[i + 2];
            *(MotOriLimit *)&motionLimitDef[i + 2] = tmp;
        }
        if (motionLimitDef[i + 1].hi.x < motionLimitDef[i + 1].lo.x) {
            int j;

            for (j = 0; j < 3; j++) {
                tmp.lo = motionLimitDef[i + j].hi;
                tmp.mid = motionLimitDef[i + j].mid;
                tmp.hi = motionLimitDef[i + j].lo;
                *(MotOriLimit *)&motionLimitDef[i + j] = tmp;
            }
        }
        i += 3;
    }
}

inline void InitMotionOrient(void *self, int oriFrom, int oriTo, int limitFrom, int limitTo,
                             int motion)
{
    struct MotCtrl *m = &GOBJ_SUB(self)->ctrl;

    if (limitFrom >= 0 && limitTo >= 0) {
        SetNodeRotationLimitDataTable(self, limitFrom, limitTo);
    }
    m->oriFrom = oriFrom;
    m->oriTo = oriTo;
    shiftMotionData((ICO_WORD_PTR(void *))self, motion, motion, 0);
    m->seGroup[0] = soundSeGroupGet();
    m->seGroup[1] = soundSeGroupGet();
}

inline unsigned int GetCurrentMotionDirectionAdjustFlag(GObj *self)
{
    return motionKind[GOBJ_SUB(self)->ctrl.motion].modeBits.bits.dirAdjust;
}

inline int ExecuteSlipProc(GObj *self)
{
    Sub15C *e = self->dobj;
    if (e->ctrl.lastSlipFlags != e->ctrl.slipFlags) {
        StopSEPackageWithGroupVariation(self, 1);
        if (GOBJ_SUB(self)->ctrl.slipFlags & 0x100000) {
            ExecuteSEPackageWithGroupVariation(self, 114, 1);
        }
        if (GOBJ_SUB(self)->ctrl.slipFlags & 0x200000) {
            ExecuteSEPackageWithGroupVariation(self, 116, 1);
        }
        if (GOBJ_SUB(self)->ctrl.slipFlags & 0x400000) {
            ExecuteSEPackageWithGroupVariation(self, 118, 1);
        }
        if (GOBJ_SUB(self)->ctrl.slipFlags & 0x800000) {
            ExecuteSEPackageWithGroupVariation(self, 120, 1);
        }
    }
    return 1;
}

inline int ExecutePauseSlipProc(GObj *self)
{
    if (systemStatus[5] != 0) {
        GOBJ_SUB(self)->ctrl.lastSlipFlags = 0;
        StopSEPackageWithGroupVariation(self, 1);
    }
    return 1;
}

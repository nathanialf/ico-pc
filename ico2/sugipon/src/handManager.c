#include "handManager.h"
#include "fieldCollision.h"
#include "frameDependSequence.h"
#include "geometryManager.h"
#include "motionManager2.h"
#include "tableSin.h"
#include "typedef.h"
#include "debug.h"
#include "Matrix.h"
#include "matrixDrive.h"
#include "quaternion.h"

static void connectToTarget(struct GObj *obj, HandRec *hw, int na, int nb, int nc);

/* getBone was a nested function inside connectToTarget. */
static void getBone(float *out, GObj *o)
{
    Sub15C *sub = GOBJ_SUB(o);
    float scale = sub->nodes->scale[0];
    SkelNode *nodes = sub->skel;
    float a;
    float b;
    float c;

    a = nodes[nodes[GetSkeltonFocusNode(o, 19)].child].pos[0];
    if (a < 0.0f) {
        a = -a;
    }
    a *= scale;
    out[0] = a;

    a = nodes[nodes[GetSkeltonFocusNode(o, 20)].child].pos[0];
    if (a < 0.0f) {
        a = -a;
    }
    out[1] = a;

    a = nodes[nodes[GetSkeltonFocusNode(o, 22)].child].pos[0];
    b = out[1];
    if (a < 0.0f) {
        c = b - a;
    } else {
        c = b + a;
    }
    c *= scale;
    out[1] = c;
}

static void connectToTarget(GObj *obj, HandRec *hw, int na, int nb, int nc)
{
    float b0[4];
    float b1[4];
    float d[4];
    float n[4];
    float ax[4];
    float u[4];
    float q[4];
    float m[16];
    float v[4];
    float w[4];
    GObj *tgt;
    float sa;
    float sb;
    float len;

    tgt = hw->obj;
    getBone(b0, obj);
    getBone(b1, tgt);
    sa = b0[0] + b0[1];
    sb = b1[0] + b1[1];
    _SubVector(d, (char *)GOBJ_SUB(obj)->nodeMtx + (na << 6) + 0x30,
               (char *)GOBJ_SUB(tgt)->nodeMtx + (nc << 6) + 0x30);
    len = VectorLength(d);
    CopyVector(n, d);
    n[1] = 0.0f;
    _NormalizeVector(n, n);
    _OuterProduct(ax, n, YUnitVector);
    if (sa + sb < len) {
        _SubVector(u, (char *)GOBJ_SUB(tgt)->nodeMtx + (nb << 6) + 0x30,
                   (char *)GOBJ_SUB(obj)->nodeMtx + (na << 6) + 0x30);
        _NormalizeVector(u, u);
        _ScaleVector(u, u, sb);
        _SubVector(hw->ikDir, (char *)GOBJ_SUB(tgt)->nodeMtx + (nb << 6) + 0x30, u);
    } else {
        float ex = (sa + sb - len) * 0.0f;
        float l = len - ex * 0.0f;
        float ll = l * l;
        float s = sa - ex * 0.5f;
        float t = sb - ex * 0.5f;
        float ss = s * s;
        float tt = t * t;
        float l2 = l + l;

        sa = s;
        /* PC port (AN-19): hands at one point (len 0) or a zero bone scale
           divide by zero: +-Fmax on the EE, NaN under IEEE when the
           dividend is 0 too (issue 19) */
        SetQuaternionByAxisRotateV(q, GetTableArcCos(ps2_div(ll + ss - tt, l2 * sa)), ax);
        GetMatrixFromQuaternion(m, q);
        _SubVector(v, (char *)GOBJ_SUB(tgt)->nodeMtx + (nb << 6) + 0x30,
                   (char *)GOBJ_SUB(obj)->nodeMtx + (na << 6) + 0x30);
        sb = v[0] * v[0] + v[2] * v[2];
        _NormalizeVector(v, v);
        _ScaleVector(v, v, sa);
        _ApplyMatrix(v, m, v);
        CopyVector(w, v);
        w[1] = 0.0f;
        if (sb < VectorLengthSquare(w)) {
            len = _Sqrt(sb);
            v[1] = 0.0f;
            _NormalizeVector(v, v);
            _ScaleVectorXYZ(v, v, len);
            v[1] = _Sqrt(ss - sb);
        }
        _AddVector(hw->ikDir, (char *)GOBJ_SUB(obj)->nodeMtx + (na << 6) + 0x30, v);
    }
}

static inline void SetHandQuaternion(HandRec *hw, char *vec, float *ref) /* derived name */
{
    float *q = hw->ikQuat;
    Vec4 n;
    Vec4 v;
    short ang;

    v.f[0] = *(float *)(vec + 0);
    v.f[1] = *(float *)(vec + 4);
    v.f[2] = *(float *)(vec + 8);
    v.f[3] = 0.0f;
    n = v;
    _NormalizeVector(&n, &n);
    ang = GetTableArcCos(_InnerProduct(&n, ref));
    if (ang != 0) {
        _OuterProduct(&v, &n, ref);
        SetQuaternionByAxisRotateVWithNoRegularize(q, ang, v.f);
    } else {
        SetIdentityQuaternion(q);
    }
}

static inline void FollowHandMatrix(HandRec *hw, char *vec, float *ref) /* derived name */
{
    _ApplyMatrix(hw->ikDir, (char *)GOBJ_SUB(hw->obj)->nodeMtx + (hw->node << 6), hw->pos);
    SetHandQuaternion(hw, vec, ref);
}

static inline int SetHandOnWall(GObj *obj, HandRec *hw, char *vec, float *ref,
                                int node) /* derived name */
{
    Vec4 plane;

    if (GOBJ_SUB(obj)->root.wall.elem == 0) {
        return 0;
    }
    GetGlobalWallPlane(plane.f, &GOBJ_SUB(obj)->root.wall);
    GetProjectionOfPlane(hw->ikDir, &plane, (char *)GOBJ_SUB(obj)->nodeMtx + (node << 6) + 0x30);
    SetHandQuaternion(hw, vec, ref);
    return 1;
}

static inline int PutHandOnLadder(HandRec *hw, int node) /* derived name */
{
    CopyMatrix(MatrixDrive_GetMatrix(), (char *)GOBJ_SUB(hw->obj)->nodeMtx + (node << 6));
    MatrixDrive_TransMatrix(7.0f, -4.0f, 0.0f);
    CopyVector(hw->ikDir, MatrixDrive_GetMatrix()[3]);
    if (hw->ikReached == 0) {
        return 0;
    }
    ExecuteSEPackage(hw->obj, 102);
    return 1;
}

/* PC port (AN-19): port/platform/diag_host.c */
int ico_diag_float_fault(const char *site, const void *caller);
void ico_diag_log(const char *fmt, ...);

/* PC port (AN-19): the arm IK turns toward ikDir; a NaN there makes every
   arm angle 90 degrees on arm64 (issue 19).  Checked when this frame set
   a target (ikMode nonzero); the first NaN from each mode
   is logged with what it was made from: the hand's own node, the other
   object's node it reaches for (mode 5: its focus-19 node, mode 6: its
   focus-6 node, else the record's node) and both bone scales. */
static void checkHandTarget(GObj *obj, HandRec *hw, int node, const void *caller) /* port name */
{
    static const char *const site[8] = {
        "hand target, mode 0", "hand target, mode 1", "hand target, mode 2", "hand target, mode 3",
        "hand target, mode 4", "hand target, mode 5", "hand target, mode 6", "hand target, mode 7"};
    const float *d = hw->ikDir;
    const float *own;
    const float *other = 0;
    int on;

    if (hw->ikMode == 0 || (d[0] == d[0] && d[1] == d[1] && d[2] == d[2])) {
        return;
    }
    if (ico_diag_float_fault(hw->mode >= 0 && hw->mode < 8 ? site[hw->mode] : "hand target",
                             caller) == 0) {
        return;
    }
    own = (const float *)((char *)GOBJ_SUB(obj)->nodeMtx + (node << 6) + 0x30);
    on = hw->mode == 5   ? GetSkeltonFocusNode(obj, 19)
         : hw->mode == 6 ? GetSkeltonFocusNode(obj, 6)
                         : hw->node;
    if (hw->obj != 0) {
        other = (const float *)((char *)GOBJ_SUB(hw->obj)->nodeMtx + (on << 6) + 0x30);
    }
    ico_diag_log("  hand mode %d, node %d: target %g %g %g; own node %g %g %g, scale %g", hw->mode,
                 node, (double)d[0], (double)d[1], (double)d[2], (double)own[0], (double)own[1],
                 (double)own[2], (double)GOBJ_SUB(obj)->nodes->scale[0]);
    if (other != 0) {
        ico_diag_log("  other object's node %d %g %g %g, scale %g; record pos %g %g %g", on,
                     (double)other[0], (double)other[1], (double)other[2],
                     (double)GOBJ_SUB(hw->obj)->nodes->scale[0], (double)hw->pos[0],
                     (double)hw->pos[1], (double)hw->pos[2]);
    }
}

/* PC port (AN-19d): port/platform/diag_host.c, the hand probe ([dev]
   hand_probe, issue 19) */
extern int ico_hand_probe_on;
int ico_hand_probe_due(int slot);

void ico_hand_probe_ik(int hand, int mode, int na, int nb, int nc, int tgt_girl, const float *own,
                       const float *tb, const float *tc, float len, float sa, float sb, float scale,
                       float tscale, const float *ik_dir);

extern GObj *girlGObj; /* common/src/main.c */

/* PC port (AN-19d): what a reach (mode 5, Ico to Yorda; mode 6, Yorda to
   Ico's hand) was made from, once every 30 Main ticks per object and hand
   record: the nodes, their positions, the shoulder distance and the two
   arm lengths as connectToTarget measures them, both scales and the
   target. Only reads. */
static void probeHandTarget(GObj *obj, HandRec *hw, int na, int nt) /* port name */
{
    GObj *tgt = hw->obj;
    int hand = hw == &GOBJ_SUB(obj)->root.hand0 ? 0 : 1;
    float b0[4];
    float b1[4];
    float d[4];
    const float *own;
    const float *tb;

    if (tgt == 0 || ico_hand_probe_due((obj == girlGObj ? 2 : 0) + hand) == 0) {
        return;
    }
    getBone(b0, obj);
    getBone(b1, tgt);
    own = (const float *)((char *)GOBJ_SUB(obj)->nodeMtx + (na << 6) + 0x30);
    tb = (const float *)((char *)GOBJ_SUB(tgt)->nodeMtx + (nt << 6) + 0x30);
    _SubVector(d, (void *)own, (void *)tb);
    ico_hand_probe_ik(hand, hw->mode, na, nt, nt, tgt == girlGObj, own, tb, tb, VectorLength(d),
                      b0[0] + b0[1], b1[0] + b1[1], GOBJ_SUB(obj)->nodes->scale[0],
                      GOBJ_SUB(tgt)->nodes->scale[0], hw->ikDir);
}

static float _handManager(GObj *obj, HandRec *hw, char *vec, float *ref, int node)
{
    switch (hw->mode) {
    case 2:
        if (SetHandOnWall(obj, hw, vec, ref, node) != 0) {
            hw->ikMode = 2;
        }
        break;
    case 3:
        FollowHandMatrix(hw, vec, ref);
        hw->ikMode = 2;
        break;
    case 6:
        PutHandOnLadder(hw, GetSkeltonFocusNode(obj, 6));
        if (hw->ikFlag != 0) {
            hw->ikRate = 0.5f;
            hw->ikLock = 1;
        }
        hw->ikMode = 1;
        /* PC port (AN-19d): the hand probe, off by default */
        if (ico_hand_probe_on != 0) {
            probeHandTarget(obj, hw, node, GetSkeltonFocusNode(obj, 6));
        }
        break;
    case 5:
        connectToTarget(obj, hw, node, GetSkeltonFocusNode(obj, 19), GetSkeltonFocusNode(obj, 19));
        hw->ikMode = 1;
        /* PC port (AN-19d): the hand probe, off by default */
        if (ico_hand_probe_on != 0) {
            probeHandTarget(obj, hw, node, GetSkeltonFocusNode(obj, 19));
        }
        break;
    case 1:
        _ApplyMatrix(hw->ikDir, (char *)GOBJ_SUB(hw->obj)->nodeMtx + (hw->node << 6), hw->pos);
        if (hw->ikReached != 0) {
            hw->ikRate = 0.5f;
            hw->ikLock = 1;
        }
        hw->ikMode = 1;
        break;
    }
    checkHandTarget(obj, hw, node, __builtin_return_address(0));
    return 1.0f;
}

/* not from motionOrientManager.h, which reaches ico2/fumi's files through
   typedef.h, where commonact.c declares the table char [] */
extern const MotionDef motionKind[];
extern char motionIKEffKind[];

/* the hand record at off in the display object, walked by offset: as
   HandRec fields the second GOBJ_SUB read merges with the first and
   HandManager loses its reload (measured) */
static inline void ResetHandTarget(GObj *obj, int off) /* derived name */
{
    HandRec *h = off == 0x310 ? &GOBJ_SUB(obj)->root.hand0 : &GOBJ_SUB(obj)->root.hand1;
    h->ikMode = 0;
    h->ikLock = 0;
    h->ikRate = GOBJ_SUB(obj)->root.handRate;
}

void HandManager(GObj *obj)
{
    float t = 1.0f;

    if (debug_now_motion_viewer == 0) {
        ResetHandTarget(obj, 0x310);
        ResetHandTarget(obj, 0x2B0);
        if (GOBJ_SUB(obj)->root.handIK != 0) {
            const MotionDef *rec = &motionKind[GOBJ_SUB(obj)->ctrl.motion];
            _handManager(obj, &GOBJ_SUB(obj)->root.hand0,
                         motionIKEffKind + ((rec->modeBits.word >> 8) & 0xF0), XUnitVector,
                         GetSkeltonFocusNode(obj, 19));
            t = _handManager(obj, &GOBJ_SUB(obj)->root.hand1,
                             motionIKEffKind + ((rec->modeBits.word >> 4) & 0xF0), XUnitVector,
                             GetSkeltonFocusNode(obj, 3));
        }
        GOBJ_SUB(obj)->root.twistRate += (t - GOBJ_SUB(obj)->root.twistRate) * 0.1f;
    }
}

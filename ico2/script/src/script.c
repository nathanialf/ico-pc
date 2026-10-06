#include "sugiCommon.h"
#include "backStage.h"
#include "debug.h"
#include "gamesys.h"
#include "layout_texture.h"
#include "obj_manager.h"
#include "act-way.h"
#include "enemy_act.h"
#include "way_llf.h"
#include "camera-root.h"
#include "fightSound.h"
#include "generator.h"
#include "gv.h"
#include "gflag.h"
#include "st25a.h"
#include "Primitive.h"
#include "StageAnimation.h"
#include "Texture.h"
#include "a_p_1.h"
#include "cage.h"
#include "particleEffect.h"
#include "spider.h"
#include <string.h>
#include <stdlib.h>
#include "StageManager.h"
#include "act.h"
#include "motionOrientManager.h"
#include "typedef.h"
#include "torch.h"
#include "matrixDrive.h"
#include "script.h"
#include "layout_action.h"
#include "Basic.h"
#include "GifPacket.h"
#include <libvu0.h>
#include "lodManager.h"
#include "geometryManager.h"
#include "adpcm_init.h"
#include "s_init.h"
#include "Matrix.h"
#include "item.h"
#include "pad.h"
#include "rotObject.h"
#include "quaternion.h"
#include "act_a_p_1.h"
#include "gobj.h"
#include "boyact.h"
#include "act-game.h"
#include "commonact.h"
#include "fieldCollision.h"
#include "main.h"
#include "motionManager2.h"
#include "weapon.h"

/* .sdata.  The three words after scpSeEnvMasterVolRate are the girl's hint
   voice: its ADPCM handle and the distance range its volume follows. */
int scpBoyControlReadDisable = 0;

float scpSeEnvMasterVolRate = 1.0f;

static SqEntry *girlHintVoice = 0; /* derived name */

static float girlHintRangeMin = 500.0f; /* derived name */

static float girlHintRangeMax = 4000.0f; /* derived name */

SqEntry *sekizo_common = 0;

char *scpDummyGObj = 0;

char *scpDummyGObj2 = 0;

int sekizo_yure = 0;

unsigned char sekizo_yure_vol = 0;

/* the parent link stage_SetParentOfGObj copies into a stage animation: the
   object and its skeleton node, -1 when the node was not found */
struct ParentLink { /* derived name */ /* field names derived */
    GObj *gobj;
    int node;
};

/* .bss: the wall-collision result, the two-slot ADPCM play-request table
   (2 x 0x18 bytes), and the camera target the script last asked for. */
static WallCfg wallColResult; /* derived name */

/* the two-slot ADPCM play-request table */
typedef struct AdpcmReq { /* field names derived */
    int kind;             /* 0x00, the sound id, 0 == slot free */
    SqEntry **id;         /* 0x04: the caller's handle variable */
    int loopNum;          /* 0x08, AdpcmOpen's loop count */
    int ch;               /* 0x0C, AdpcmOpen's channel */
    int play;             /* 0x10, start playing once open */
    int cancel;           /* 0x14, close as soon as it opens */
} AdpcmReq;               /* derived name */

static AdpcmReq adpcmReq[2]; /* derived name */

static float scriptCameraTarget[4]; /* derived name */

/* sugipon's motionKind (MotionDef, motionOrientManager.h), read here through
   this file's own view of the record */
extern struct MotTblRec { /* derived name */ /* field names derived */
    char pad0[390];
    short smzAngle;
    char pad188[12];
} motionKind[];

/* .data: the colour packet prim_DispWireBox draws the debug trigger box
   with, declared as the whole 4-word record. */
static int wireBoxColor[4] = {0, 16, 32, 128}; /* derived name */

/* .data: the two exported door tables, then five read only here.  Each is
   the usual actor mail pair: the mail the door thread answers, then the 429
   end marker act.c walks to. */
ActMail scpInterDoorUpLever1[2] = {{406, scpDoorTypeUpSwitch}, {429}};

ActMail scpInterDoorUpLever2[2] = {{407, scpDoorTypeUpSwitch}, {429}};

static ActMail doorTypeUp_mes[2] = {{430}, {429}}; /* derived name */

static ActMail doorTypeUpSwitchDown_mes[2] = {{430}, {429}}; /* derived name */

static ActMail doorTypeUpSwitchUp_mes[2] = {{430}, {429}}; /* derived name */

static ActMail doorTypeUpDown_mes[2] = {{430}, {429}}; /* derived name */

static ActMail doorTypeUpUp_mes[2] = {{430}, {429}}; /* derived name */

/* the 0x30-byte wood-bridge table entry at woodBoxTbl: an object id, the
   trigger `kind` that selects which axis test runs, the bridge end offset the
   way group is built from, and the four axis bounds the tests read. */
struct WoodBoxEnt { /* derived name */ /* field names derived */
    short id;                          /* 0x00 */
    char kind;                         /* 0x02 */
    char pad3[13];                     /* 0x03 */
    float ofs[4];                      /* 0x10 */
    float b0;                          /* 0x20 */
    float b1;                          /* 0x24 */
    float b2;                          /* 0x28 */
    float b3;                          /* 0x2C */
};

/* .data: the wood-bridge trigger table, one row per bridge object, walked by
   object id. */
static struct WoodBoxEnt woodBoxTbl[11] = {
    {276, 6, {0}, {0.0f, -100.0f, 100.0f, 0.0f}, 1068.0f, -135.0f, 200.0f, 580.0f},
    {1710, 0, {0}, {-100.0f, -200.0f, 0.0f, 0.0f}, -1410.0f, 1000.0f, 0.0f, 0.0f},
    {1705, 6, {0}, {0.0f, -200.0f, 100.0f, 0.0f}, 807.0f, 2000.0f, 570.0f, 1027.0f},
    {1707, 6, {0}, {0.0f, -200.0f, 100.0f, 0.0f}, 807.0f, 2000.0f, 570.0f, 1027.0f},
    {363, 1, {0}, {0.0f, -200.0f, -100.0f, 0.0f}, 740.0f, -3600.0f, 0.0f, 0.0f},
    {988, 6, {0}, {0.0f, -200.0f, 100.0f, 0.0f}, -1431.0f, 105.0f, -470.0f, -259.0f},
    {865, 8, {0}, {-100.0f, -200.0f, 0.0f, 0.0f}, -350.0f, -285.0f, -450.0f, -280.0f},
    {866, 8, {0}, {-100.0f, -200.0f, 0.0f, 0.0f}, -350.0f, -285.0f, -450.0f, -280.0f},
    {1773, 7, {0}, {-100.0f, -200.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.0f, 0.0f},
    {1626, 1, {0}, {100.0f, -200.0f, 0.0f, 0.0f}, 680.0f, 230.0f, 0.0f, 0.0f},
    {3294, 9, {0}, {-100.0f, -200.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0.0f, 0.0f},
}; /* derived name */

/* .sbss: a stage change has been requested and no further one is
   accepted. */
static int stageChangeReq; /* derived name */

/* declared (void) here: the call passes no object (the ROM leaves a0 as
   scpIsWallLever2On received it), while switch.h takes the lever's GObj *
   (so neither switch.h nor box.h, which includes it, is included) */
extern int IsWallLeverStatus(void);

/* .data: the layout record scpBornSpider fills and hands MakeAP1GObj for
   each spider */
static SObjSimpleSetting spiderLayout = {{0.0f}, {0.0f}, {1.0f, 1.0f, 1.0f}}; /* derived name */

/* box.c's, defined in switch.c.inc */
extern int CheckReadyAllSwitches(void);

/* Before flag 332 this hands the actor queen_appear_mes and posts it; after
 * it, it shows object 2149, plays its motion and starts the face shadow
 * scrolls and the stage animations. */
inline void actSubSekizoSe(GObj *volatile self)
{
    GObj *x = self;
    Act *act = (Act *)actInitialize(self);

    _ACTWait(1);
    if (gflagChk(332) == 0) {
        ScpCallCameraSetTarget(3834.0f, -888.0f, 0.0f);
        scpSearchGobj(2149)->active = 0;
        stage_SetLoopFlag(555, 0);
        queen_appear_mes[0].func = actSt25aQueenAppearChk;
        act->mail = queen_appear_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
        return;
    }
    scpLinkBGAtoKindTargetSkeltonWithLocalRotationFlag(47, 0, 555, 0);
    scpSearchGobj(2149)->active = 1;
    scpPlayMot(scpSearchGobj(2149), 1104);
    tex_SetUVScroll(faceShadowTex, 0.0f, 0.0f, 0.25f, 0.0625f, 0.8f, 0.8f, 1);
    tex_SetUVScroll(faceShadowTex00, 0.0f, 0.0f, 0.25f, 0.0625f, 0.45f, 0.45f, 1);
    ScpCallCameraSetTarget(3834.0f, -888.0f, 0.0f);
    stage_SetAnimation(156, 0, -1);
    stage_SetAnimation(159, 1, 0);
}

inline void scpDispOffAllWithKind(int kind)
{
    void *v0 = isysGObjSearchFromObjKindID_begin(kind);
    while (v0 != 0) {
        ((GObj *)v0)->drawMask = 0;
        v0 = isysGObjSearchFromObjKindID_next(v0);
    }
}

inline void scpDispOnAllWithKind(int x)
{
    GObj *p = isysGObjSearchFromObjKindID_begin(x);
    while (p != 0) {
        p->drawMask = 0xFFFFFFFF;
        p = isysGObjSearchFromObjKindID_next(p);
    }
}

inline void scpActivateAllWithKind(int kind)
{
    GObj *p = isysGObjSearchFromObjKindID_begin(kind);
    while (p != 0) {
        p->active = 1;
        p = isysGObjSearchFromObjKindID_next(p);
    }
}

inline void scpDisActivateAllWithKind(int kind)
{
    void *v0 = isysGObjSearchFromObjKindID_begin(kind);
    while (v0 != 0) {
        ((GObj *)v0)->active = 0;
        v0 = isysGObjSearchFromObjKindID_next(v0);
    }
}

inline int scpIsTorchLightOn(int id)
{
    GObj *ret1 = scpSearchGobj(id);
    GObj *ret2 = scpSearchGobj(0);
    ret2->active = 1;
    return IsTorchLightOn(ret1);
}

void scpTorchLightOn(int id)
{
    void *r = scpSearchGobj(id);
    if (r) {
        LightTorchOn(r);
    }
}

void scpTorchLightOff(int id)
{
    GObj *v = scpSearchGobj(id);
    if (v) {
        LightTorchOff(v);
    }
}

inline GObj *scpIsBombExplode(int x)
{
    GObj *p = isysGObjSearchFromObjKindID_begin(x);
    if (p != 0) {
        do {
            if (IsBombExplode(p) != 0) {
                return p;
            }
            p = isysGObjSearchFromObjKindID_next(p);
        } while (p != 0);
    }
    return 0;
}

void scpSetCageVelocityFriction(int id, float friction)
{
    GObj *v = scpSearchGobj(id);
    if (v) {
        SetCageVelocityFriction(v, friction);
    }
    /* no hanging object found */
    debug_StdPrintfDummy("ぶら下がりオブジェクトが見つかりません。(scpSetCageVelocityFriction)\n");
}

inline float scpGetRotObjectRotCount(int id)
{
    GObj *v = scpSearchGobj(id);
    if (v != 0) {
        return GetRotObjectRotCount(v);
    }
    /* no push-turn object found */
    debug_StdPrintfDummy("押し回しオブジェクトが見つかりません。(scpGetRotObjectRotCount)\n");
    return 0.0f;
}

inline short scpGetRotObjectCurrentRot(int no)
{
    short rot;
    int ext;
    GamesysObjInfo *info = gamesysObjInfoGet(18, no);
    if (info != 0) {
        GetRotObjectGameSysObjInfoExtData(&rot, &ext, info);
        return rot;
    }
    return 0;
}

inline int scpIsRotObjectZPlusDirInclude(int id, int from, int to)
{
    GObj *q = scpSearchGobj(id);
    if (q != 0) {
        int e = GetRotObjectZPlusDirection(q);
        short A1 = (from << 15) / 180;
        short A2 = (to << 15) / 180;
        if (A2 < A1) {
            if (A1 < e || e < A2)
                return 1;
            return 0;
        }
        if (A1 < e && e < A2)
            return 1;
        return 0;
    }
    /* no push-turn object found */
    debug_StdPrintfDummy("押し回しオブジェクトが見つかりません。(scpGetRotObjectZDirInclude)\n");
    return 0;
}

inline void scpLinkBGAtoLayoutedTarget(int id, int key)
{
    GObj *ret = scpSearchGobj(id);
    if (ret != 0) {
        struct ParentLink link = {ret, 0};
        stage_SetParentOfGObj(key, &link);
    }
}

inline void scpLinkBGAtoLayoutedTargetSkeltonWithLocalRotationFlag(int id, int focus, int key,
                                                                   int localRotation)
{
    GObj *ret = scpSearchGobj(id);
    if (ret != 0) {
        struct ParentLink copy;
        struct ParentLink pair;
        pair.gobj = ret;
        pair.node = GetSkeltonFocusNode(ret, focus);
        copy = pair;
        if (copy.node == -1)
            /* LWS skeleton parenting: the node was not found */
            debug_StdPrintfDummy(
                "LWSのスケルトンペアレント処理において,ノードが見つかりませんでした\n");
        else
            stage_SetParentOfGObjWithLocalRotationFlag(key, &copy, localRotation);
    }
}

inline void scpLinkBGAtoKindTargetSkeltonWithLocalRotationFlag(int kind, int focus, int key,
                                                               int localRotation)
{
    GObj *ret = isysGObjSearchFromObjKindID_begin(kind);
    if (ret != 0) {
        struct ParentLink copy;
        struct ParentLink pair;
        pair.gobj = ret;
        pair.node = GetSkeltonFocusNode(ret, focus);
        copy = pair;
        if (copy.node == -1)
            /* LWS skeleton parenting: the node was not found */
            debug_StdPrintfDummy(
                "LWSのスケルトンペアレント処理において,ノードが見つかりませんでした\n");
        else
            stage_SetParentOfGObjWithLocalRotationFlag(key, &copy, localRotation);
    }
}

inline void scpLinkBGAtoLayoutedTargetSkelton(int id, int focus, int key)
{
    GObj *ret = scpSearchGobj(id);
    if (ret != 0) {
        struct ParentLink copy;
        struct ParentLink pair;
        pair.gobj = ret;
        pair.node = GetSkeltonFocusNode(ret, focus);
        copy = pair;
        if (copy.node == -1)
            /* LWS skeleton parenting: the node was not found */
            debug_StdPrintfDummy(
                "LWSのスケルトンペアレント処理において,ノードが見つかりませんでした\n");
        else
            stage_SetParentOfGObjWithLocalRotationFlag(key, &copy, 1);
    }
}

inline WallCfg *scpGetWallCollision(float x0, float y0, float z0, float x1, float y1, float z1)
{
    ClipWork work;

    work.pt[0][0] = x0;
    work.pt[0][1] = y0;
    work.pt[0][2] = z0;
    work.pt[0][3] = 1.0f;
    work.pt[1][0] = x1;
    work.pt[1][1] = y1;
    work.pt[1][2] = z1;
    work.pt[1][3] = 1.0f;
    work.radius = 0.0f;
    ClipWall(&work);
    wallColResult.elem = work.wall.elem;
    wallColResult.o = work.wall.o;
    if (work.wall.elem == 0) {
        /* no wall collision found */
        debug_StdPrintfDummy(
            "scpGetWallCollision: (%4.3f, %4.3f, %4.3f) => (%4.3f, %4.3f, %4.3f)\n\t壁コリジョンが見つかりません。\n",
            x0, y0, z0, x1, y1, z1);
    }
    return &wallColResult;
}

inline GObj *scpSearchGobj(int id)
{
    return isysGObjSearchFromObjLayoutID(id);
}

void scpPlayMotDir(GObj *self, float *dir)
{
    sceVu0Normalize(dir, dir);
    SetMotionDirection(self, dir);
}

void scpPlayMotDirSmz(GObj *self, float *dir)
{
    sceVu0Normalize(dir, dir);
    SetMotionDirectionSmooze(
        self, dir,
        (float)((struct MotTblRec *)(GOBJ_SUB(self)->ctrl.motion * 404 + (char *)motionKind))
            ->smzAngle);
}

inline void scpPlayMotNode(void *self, int mot, void *obj, int node)
{
    float buf[4];
    memset(buf, 0, 16);
    buf[3] = 1.0f;
    SetMotionNodeFixModeParameter(self, obj, 0, node, buf, 0.0f, 0.0f, 0.0f, 1.0f);
    scpPlayMot(self, mot);
}

void scpPlayMot(GObj *self, int mot)
{
    Act *act = GOBJ_ACT(self);
    int id = -1;

    if (self == boyGObj) {
        id = 1281;
    } else if (self == girlGObj) {
        id = 2118;
    } else if (self == isysGObjSearchFromObjLayoutID(2149)) {
        id = 2413;
    } else if (((GObj *)self)->kind == 4) {
        id = 2405;
    }

    if (id < 0) {
        InitMotionOrient(self, 2122, 2407, -1, -1, mot);
        return;
    }
    ControlMotionOrient(id, mot);
    act->motReq = SetMotionRequest(self, 268, act->env.motOriReq);
}

inline void scpPlayMotReq(GObj *self, int mot)
{
    Act *p = GOBJ_ACT(self);
    p->motReq = SetMotionRequest(self, mot, p->env.motOriReq);
}

inline void scpPlayPosSet(void *self, float x, float y, float z)
{
    float buf[4];
    memset(buf, 0, 16);
    buf[0] = x;
    buf[1] = y;
    buf[2] = z;
    SetDirectRootPosition(self, buf);
    ClearMotionGeometryInfo(self);
}

void scpPlayJump(GObj *self, int orient)
{
    ACTItemForceDrop(self);
    GOBJ_ACT(self)->enemy->jumpOrient = orient;
    iosOmSendMail(self, 45, self);
}

void scpPlayStart(GObj *self)
{
    ACTItemForceDrop(self);
    iosOmSendMail(self, 46, self);
    SetLodLevel(self, 0);
}

void scpPlayEnd(GObj *self)
{
    iosOmSendMail(self, 47, self);
    SetLodLevel(self, 2);
}

inline void scpPlayWaitMotEnd(GObj *self)
{
    Act *p = GOBJ_ACT(self);
    /* PC port: motReq is the struct MotCtrl SetMotionRequest returns; 0x5C
       (frameEnd) is the EE offset */
    while ((((struct MotCtrl *)p->motReq)->frameEnd & 1) == 0) {
        _ACTWait(1);
    }
}

void scpTrans(void *self, float *rot)
{
    SetRootMatrixWithTransOffset(self, rot[0], rot[1], rot[2]);
}

/* one linear step of *p toward TARGET; returns non-zero once it arrives */
static inline int scpTransStep(float *p, float target, float step) /* derived name */
{
    int done = 0;

    if (0.0f < target) {
        *p += step;
        if (target < *p) {
            *p = target;
            done = 1;
        }
    } else {
        *p -= step;
        if (*p < target) {
            *p = target;
            done = 1;
        }
    }
    return done;
}

inline void scpTransLinear(GObj *obj, int axis, float target, float step)
{
    int done = 0;
    float pos[4];

    GetRootMatrixTransOffset(pos, obj);
    while (done == 0) {
        done = scpTransStep(&pos[axis], target, step);
        scpTrans(obj, pos);
        _ACTWait(1);
    }
}

inline void scpRotateLinear(void *obj, int deg, short step, int axis)
{
    float q[4];
    int t;
    int n;
    short rem;

    t = (deg << 15) / 180;
    rem = (short)(t % step);
    n = t / step < 0 ? -(t / step) : t / step;
    if (deg < 0) {
        step = -step;
    }
    while (n-- > 0 || rem != 0) {
        if (n < 0) {
            step = rem;
            rem = 0;
        }
        GetRootMatrixRotOffset(q, obj);
        step = -step;
        switch (axis) {
        case 0:
            RotQuaternionX(q, step);
            break;
        case 1:
            RotQuaternionY(q, step);
            break;
        case 2:
            RotQuaternionZ(q, step);
            break;
        }
        SetRootMatrixRotOffset(obj, q);
        _ACTWait(1);
    }
}

/* the colour a trigger ball's wire sphere is drawn in; red while it hits */
static const Col4 triggerBallColor = {{0, 16, 32, 128}}; /* derived name */

inline int scpTriggerPosBall(float *pos, float *target, float r)
{
    float d[4];
    Col4 col;
    float rr;
    int hit;

    sceVu0SubVector(d, target, pos);
    rr = r * r;
    if (sceVu0InnerProduct(d, d) < rr) {
        hit = 1;
    } else {
        hit = 0;
    }
    if (debug_wallcheck_flag != 0) {
        MatrixDrive_PushMatrix();
        col = triggerBallColor;
        if (hit != 0) {
            col.c[0] = 255;
        }
        gif_StartPacketPri(11);
        sceVu0UnitMatrix(MatrixDrive_GetMatrix());
        MatrixDrive_TransMatrixV(pos);
        prim_DispWireSphere(r, &col, 16, 8);
        gif_EndPacket();
        MatrixDrive_PopMatrix();
    }
    return hit;
}

inline int scpTriggerBall(GObj *obj, GObj *target, float r)
{
    float tpos[4];
    float pos[4];

    GetRootPosition(tpos, target);
    GetRootPosition(pos, obj);
    return scpTriggerPosBall(pos, tpos, r);
}

/* the object kinds a trigger ball tests */
static const int targetManKind[] = {1, 2, 4}; /* derived name */

int scpTriggerBallTargetMan(GObj *obj, float r)
{
    GObj *g;
    unsigned int i;
    int hit = 0;

    for (i = 0; i < 3 && hit == 0; i++) {
        for (g = isysGObjSearchFromObjKindID_begin(targetManKind[i]); g != 0;
             g = isysGObjSearchFromObjKindID_next(g)) {
            if (scpTriggerBall(obj, g, r) != 0) {
                hit = 1;
                break;
            }
        }
    }
    return hit;
}

/* the object kinds a trigger may ignore, up to the -1 end marker */
static const int ignoreKind[] = {1, 2, 4, -1}; /* derived name */

inline int scpTriggerIgnore(GObj *self)
{
    int i = 0;

    while (ignoreKind[i] != -1) {
        if (((GObj *)self)->kind == ignoreKind[i]) {
            Sub15C *sub = GOBJ_SUB(self);
            if (_ACTGame_GetParamF(2) < sub->ctrl.groundHeight || GOBJ_ACT(self)->actMode == 22) {
                return 1;
            }
        }
        i++;
    }
    return 0;
}

inline int scpTriggerFloorAttr(GObj *self, int attr)
{
    if (scpTriggerIgnore(self) != 0) {
        return 0;
    }
    return CheckFloorAttribute(self, attr);
}

inline int scpTriggerWallAttr(GObj *self, int attr)
{
    if (scpTriggerIgnore(self) != 0) {
        return 0;
    }
    return CheckWallAttribute(self, attr);
}

/* the object kinds a floor-attribute trigger tests */
static const int floorTargetManKind[] = {1, 2, 4, 17}; /* derived name */

inline int scpTriggerFloorAttrTargetMan(GObj *self, int attr)
{
    GObj *g;
    unsigned int i;
    int hit = 0;

    for (i = 0; i < 4 && hit == 0; i++) {
        for (g = isysGObjSearchFromObjKindID_begin(floorTargetManKind[i]); g != 0;
             g = isysGObjSearchFromObjKindID_next(g)) {
            if (scpTriggerFloorAttr(g, attr) != 0) {
                hit = 1;
                break;
            }
        }
    }
    return hit;
}

inline int scpTriggerPosBox(float *p, float *pos, float *size)
{
    int hit = 0;

    if (pos[0] - size[0] < p[0] && p[0] < pos[0] + size[0] && pos[1] - size[1] < p[1] &&
        p[1] < pos[1] + size[1] && pos[2] - size[2] < p[2] && p[2] < pos[2] + size[2]) {
        hit = 1;
    }
    if (debug_wallcheck_flag != 0) {
        MatrixDrive_PushMatrix();
        if (hit != 0) {
            wireBoxColor[0] = 255;
        } else {
            wireBoxColor[0] = 0;
        }
        gif_StartPacketPri(11);
        sceVu0UnitMatrix(MatrixDrive_GetMatrix());
        MatrixDrive_TransMatrixV(pos);
        prim_DispWireBox(size, wireBoxColor);
        gif_EndPacket();
        MatrixDrive_PopMatrix();
    }
    return hit;
}

inline int scpEffectStart(void *pos, int kind)
{
    int buf[4];
    SetIdentityQuaternion(buf);
    return SetParticleEffect(kind, pos, buf);
}

inline void scpDoorTypeUp(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);

    if (gflagChk(act->doorFlag) != 0) {
        GObj *obj = self;
        scpTransLinear(obj, 1, -act->doorDist, act->doorDist);
    }
    doorTypeUp_mes[0].func = scpDoorTypeUpMain;
    act->mail = doorTypeUp_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

inline void scpDoorTypeUpMain(GObj *volatile self)
{
    Act *p = GOBJ_ACT(self);
    p->mainMail = p->doorMail;
    for (;;) {
        _ACTWait(1);
    }
}

inline void scpDoorTypeUpSwitch(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);

    act->mainMail = 0;
    if (gflagChk(act->doorFlag) != 0) {
        doorTypeUpSwitchDown_mes[0].func = scpDoorTypeUpDown;
        act->mail = doorTypeUpSwitchDown_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
    doorTypeUpSwitchUp_mes[0].func = scpDoorTypeUpUp;
    act->mail = doorTypeUpSwitchUp_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void scpDoorTypeUpDown(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);

    if (act->doorCamera != 0) {
        Camctrl_SetTarget(act->doorCamera, 0, 3);
        if (act->doorCamWait != 0) {
            _ACTWait(act->doorCamWait);
        }
    }
    debug_StdPrintfDummy("start animation down\n");
    gflagOff(act->doorFlag);
    scpTransLinear(self, 1, act->doorDist, act->doorStep);
    if (act->doorEndWait != 0) {
        _ACTWait(act->doorEndWait);
    }
    Camctrl_ExitEveRock();
    doorTypeUpDown_mes[0].func = scpDoorTypeUpMain;
    act->mail = doorTypeUpDown_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void scpDoorTypeUpUp(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);

    if (act->doorCamera != 0) {
        Camctrl_SetTarget(act->doorCamera, 0, 3);
        if (act->doorCamWait != 0) {
            _ACTWait(act->doorCamWait);
        }
    }
    debug_StdPrintfDummy("start animation up\n");
    gflagOn(act->doorFlag);
    scpTransLinear(self, 1, -act->doorDist, act->doorStep);
    if (act->doorEndWait != 0) {
        _ACTWait(act->doorEndWait);
    }
    Camctrl_ExitEveRock();
    doorTypeUpUp_mes[0].func = scpDoorTypeUpMain;
    act->mail = doorTypeUpUp_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

inline void scpAdpcmPlayRequestFunc(int kind, SqEntry **id, int ch, int loopNum, int play)
{
    int i;

    if (id != 0) {
        *id = 0;
    }
    for (i = 0; i < 2; i++) {
        if (adpcmReq[i].kind == 0) {
            goto found;
        }
    }
    return;

found:
    adpcmReq[i].kind = kind;
    adpcmReq[i].id = id;
    adpcmReq[i].loopNum = loopNum;
    adpcmReq[i].ch = ch;
    adpcmReq[i].play = play;
    adpcmReq[i].cancel = 0;
}

inline int scpAdpcmPlayRequestNum(void)
{
    int i;
    int n = 0;
    for (i = 0; i < 2; i++) {
        if (adpcmReq[i].kind != 0) {
            n++;
        }
    }
    return n;
}

/* the slot of the pending play request whose id is ID, or -1 (inlined into
   scpAdpcmCloseChkFunc, scpGirlHintVoiceCancel and others) */
static inline int scpAdpcmRequestSlot(SqEntry **id) /* derived name */
{
    int i;
    for (i = 0; i < 2; i++) {
        if (adpcmReq[i].kind != 0 && adpcmReq[i].id == id)
            goto found;
    }
    i = -1;
found:
    return i;
}

/* flag the pending play request whose id is ID so the ADPCM daemon closes
   it (inlined into scpAdpcmCloseFunc and scpAdpcmFadeCloseFunc) */
static inline void scpAdpcmRequestClose(SqEntry **id) /* derived name */
{
    int i;
    for (i = 0; i < 2; i++)
        if (adpcmReq[i].kind != 0 && adpcmReq[i].id == id) {
            adpcmReq[i].cancel = 1;
            break;
        }
}

void scpSubAdpcmPlay(GObj *volatile self)
{
    AdpcmOpenReq work;
    int i;
    SqEntry *h;

    memset(adpcmReq, 0, sizeof(adpcmReq));
    for (;;) {
        AdpcmReq *tbl = adpcmReq;
        for (i = 0; i < 2; i++) {
            if (tbl[i].kind != 0) {
                AdpcmReq *p = &tbl[i];

                while (AdpcmFreeAreaGet() == 0) {
                    /* could not open: ADPCM is full */
                    debug_StdPrintfDummy("ADPCM一杯で開けませんでした。\n");
                    if (AdpcmNotUseIopAreaFree() != 0) {
                        /* an IOP area in use although nothing is open: found and forced free */
                        debug_StdPrintfDummy(
                            "オープンされていないのにも関わらず使われていないIOP領域発見&強制解放\n");
                    } else if (fightSoundPlayChk() != 0) {
                        /* stop the battle music and request instead; no battle music until the stage changes */
                        debug_StdPrintfDummy(
                            "戦闘曲を止めて,変わりにリクエストします。以降ステージ切り換えまで戦闘曲なりません。\n");
                        fightSoundProcessRequestPause();
                        while (fightSoundPlayChk() != 0) {
                            _ACTWait(1);
                        }
                    } else {
                        /* the battle music is not playing, so to keep the program running stop every tune and request instead */
                        debug_StdPrintfDummy(
                            "戦闘曲なっていないので,プログラムを止めないために\n全部曲を止めて,変わりにリクエストします。\n");
                        AdpcmFadeCloseAll(0x3FFF);
                        _ACTWait(1);
                    }
                }
                soundDataOpen(&work, 2, p->kind, p->ch, p->loopNum);
                while ((h = soundDataOpenSync(&work)) == (SqEntry *)-1) {
                    _ACTWait(1);
                }
                if (h != 0) {
                    if (p->play != 0) {
                        AdpcmPlay(h->stream);
                    }
                    if (p->cancel == 0) {
                        if (p->id != 0) {
                            *p->id = h;
                        }
                    } else {
                        soundDataClose(h);
                    }
                }
                p->kind = 0;
            }
        }
        _ACTWait(1);
    }
}

void scpAdpcmCloseFunc(SqEntry **h)
{
    SqEntry *handle = *h;
    if (handle != 0) {
        soundDataClose(handle);
        return;
    }
    scpAdpcmRequestClose(h);
}

inline int scpAdpcmFadeCloseFunc(SqEntry **h, short fade)
{
    SqEntry *p = *h;

    if (p != 0) {
        AdpcmStream *s = p->stream;
        if (s == 0) {
            return 0;
        }
        s->fadeStep = fade;
        return 1;
    }
    scpAdpcmRequestClose(h);
    return 0;
}

inline int scpAdpcmCloseChkFunc(SqEntry **h)
{
    int no;
    SqEntry *p = *h;
    if (p != 0) {
        if (p->stream == 0 || p->stream->bg == 0) {
            return 0;
        }
        return 1;
    }
    no = -1;
    return no < scpAdpcmRequestSlot(h);
}

/* scpAdpcmCloseChkFunc's check for the hint voice's slot, used only by
   scpGirlHintVoiceTickProc */
static inline int scpGirlHintVoiceChk(void) /* derived name */
{
    return scpAdpcmCloseChkFunc(&girlHintVoice);
}

inline void scpDeamon(GObj *volatile self)
{
    debug_StdPrintfDummy("deamon start");
    girlHintVoice = 0;
    startStagePauseDisableTimer = 0;
    if (stage_no == 11 && gflagChk(137) == 0) {
        scpSeEnvMasterVolRate = 0.0f;
    } else {
        scpSeEnvMasterVolRate = 1.0f;
    }
    gflagOff(389);
    _ACTWait(1);
    actCreateSubThread(scpSubAdpcmPlay, 21);
    _ACTWait(3);
    StabilizeAllLayoutedCage();
    _ACTWait(1);
    backStageProcessInStage(0.0f);
    gflagOff(394);
}

void scpGirlHintVoiceReady(int kind)
{
    float p0[4];
    float p1[4];
    float d[4];
    float dist;

    if (girlHintVoice != 0) {
        /* a hint voice is playing, so a new one cannot be READY */
        debug_StdPrintfDummy("ヒントポイス再生中なので新にREADYできません\n");
    }
    if (AdpcmFreeAreaGet() == 0) {
        /* hint voice: could not open, ADPCM is full */
        debug_StdPrintfDummy("ヒントポイスADPCM一杯で開けませんでした。\n");
        return;
    }
    if (boyGObj == 0) {
        return;
    }
    GetRootPosition(p0, boyGObj);
    GetRootPosition(p1, girlGObj);
    _SubVector(d, p0, p1);
    dist = _InnerProduct(d, d);
    girlHintRangeMin = 500.0f;
    girlHintRangeMax = 4000.0f;
    switch (kind) {
    case 101:
        if (500000.0f < dist) {
            kind = 102;
        }
        break;
    case 103:
        if (500000.0f < dist) {
            kind = 104;
        }
        break;
    }
    scpAdpcmPlayRequestFunc(kind, &girlHintVoice, 1, 1, 0);
}

void scpGirlHintVoicePlay(void)
{
    SqEntry *p = girlHintVoice;
    if (p != 0) {
        AdpcmPlay(p->stream);
    } else {
        /* the hint voice is not prepared yet, so it could not play */
        debug_StdPrintfDummy("ヒントポイスの準備未終了の状態なのでならせませんでした。\n");
    }
}

inline void scpGirlHintVoiceCancel(void)
{
    if (scpAdpcmCloseChkFunc(&girlHintVoice) != 0) {
        scpAdpcmCloseFunc(&girlHintVoice);
        girlHintVoice = 0;
    }
}

void scpGirlHintVoiceTickProc(void *cam)
{
    float rmin = girlHintRangeMin; /* both range globals are read into locals
                                      before the early returns */
    float rmax = girlHintRangeMax;
    float pos[4];
    float dist;
    int deg;
    AdpcmStream *snd;
    float vol;
    float lr;
    float l;
    float r;
    int adeg;

    if (girlHintVoice == 0 || girlGObj == 0)
        return;
    if (scpGirlHintVoiceChk() == 0) {
        girlHintVoice = 0;
        return;
    }
    snd = girlHintVoice->stream;
    GetRootPosition(pos, girlGObj);
    CameraGetOtherObjOffset(pos, &dist, &deg);
    if (rmax <= dist) {
        vol = 0.0f;
    } else if (dist < rmin) {
        vol = 1.0f / (dist / rmin + 1.0f);
    } else {
        dist = dist - rmin;
        vol = 1.0f / (dist / (rmax - rmin) + 1.0f) - 0.5f;
    }
    /* adeg is the angle's magnitude: the angle is copied first and the
       negation reads it again */
    adeg = deg;
    if (adeg <= -1)
        adeg = -deg;
    lr = (float)adeg * -0.0027777778f + 1.0f;
    /* each arm sets its fixed side first */
    if (deg >= 0) {
        r = 1.0f;
        adeg = deg;
        if (adeg >= 91)
            adeg = 180 - adeg;
        l = (float)adeg * -0.01f + 1.0f;
    } else {
        l = 1.0f;
        adeg = -deg;
        if (adeg >= 91)
            adeg = 180 - adeg;
        r = (float)adeg * -0.01f + 1.0f;
    }
    snd->volL[0] = vol * 16383.0f * lr * l;
    snd->volR[1] = vol * 16383.0f * lr * r;
    AdpcmInterStereoVolumeSet(snd, 0);
}

static void scpWoodSrh(GObj *self, struct WoodBoxEnt *w)
{
    float pos[4];
    float dst[4];
    float gpos[4];
    GObj *g;
    int st;
    int way = 0;

    for (;;) {
        st = 0;
        GetRootPosition(pos, self);
        pos[1] -= 50.0f;
        switch (w->kind) {
        case 0:
            if (pos[0] < w->b0 && pos[1] > w->b1) {
                st = 1;
            } else {
                st = 2;
            }
            break;
        case 1:
            if (pos[0] > w->b0 && pos[1] > w->b1) {
                st = 1;
            } else {
                st = 2;
            }
            break;
        case 2:
            if (pos[2] > w->b0 && pos[1] > w->b1) {
                st = 1;
            } else {
                st = 2;
            }
            break;
        case 4:
        case 8:
            if (pos[0] < w->b0 && pos[1] > w->b1 && pos[2] > w->b2 && pos[2] < w->b3) {
                st = 1;
            } else {
                st = 2;
            }
            if (st == 1 && w->kind == 8) {
                for (g = isysGObjSearchFromObjKindID_begin(17); g != 0;
                     g = isysGObjSearchFromObjKindID_next(g)) {
                    if (g != self) {
                        if (g->labelId == 865 || g->labelId == 866) {
                            GetRootPosition(gpos, g);
                            gpos[1] -= 50.0f;
                            if (gpos[1] < -182.0f && gpos[1] > -280.0f) {
                                st = 2;
                            }
                        }
                    }
                }
            }
            break;
        case 5:
            if (pos[2] < w->b0 && pos[1] > w->b1 && pos[0] > w->b2 && pos[0] < w->b3) {
                st = 1;
            } else {
                st = 2;
            }
            break;
        case 6:
            if (pos[2] > w->b0 && pos[1] > w->b1 && pos[0] > w->b2 && pos[0] < w->b3) {
                st = 1;
            } else {
                st = 2;
            }
            break;
        case 7:
            if (pos[1] > -190.0f && (pos[0] < 660.0f || pos[0] > 916.0f) && pos[2] > -1600.0f) {
                st = 1;
                if (pos[0] > 916.0) {
                    w->ofs[0] = 100.0f;
                } else {
                    w->ofs[0] = -100.0f;
                }
            } else {
                st = 2;
            }
            break;
        case 9:
            if (pos[1] > 750.0f && (pos[0] < -515.0f || pos[0] > 325.0f)) {
                st = 1;
                if (pos[0] > 325.0) {
                    w->ofs[0] = 100.0f;
                } else {
                    w->ofs[0] = -100.0f;
                }
            } else {
                st = 2;
            }
            break;
        }
        switch (st) {
        case 1:
            if (way != 0) {
                DeleteWayGroup(way);
            } else {
                debug_StdPrintfDummy("bridge create");
            }
            sceVu0AddVector(dst, pos, w->ofs);
            way = CreateBridge(pos, dst);
            break;
        case 2:
            if (way != 0) {
                debug_StdPrintfDummy("bridge delete");
                DeleteWayGroup(way);
                way = 0;
            }
            break;
        }
        _ACTWait(1);
    }
}

inline void scpWoodBox(GObj *volatile self)
{
    struct WoodBoxEnt *p;
    unsigned int i;

    _ACTWait(10);

    for (i = 0, p = woodBoxTbl; i < 11; i++, p++) {
        if (p->id == self->labelId) {
            goto found;
        }
    }
    return;

found:
    scpWoodSrh(self, p);
}

/* where the stone statue's sound effects play */
static const Vec16 sekizouSePos = {{6646.0f, -2157.0f, 1102.0f, 0.0f}}; /* derived name */

void scpSekizou(GObj *self, int flag, int anim, int anim2, int kind, float bx, float by, float bz,
                float gx, float gy, float gz)
{
    int fade = 0;

    if (gflagChk(flag) != 0) {
        stage_SetAnimation(anim, 0, -1);
        return;
    }
    stage_SetAnimation(anim, 0, 0);
    if (girlGObj == 0) {
        return;
    }
    for (;;) {
        if (scpTriggerBall(self, girlGObj, 200.0f) == 0 ||
            scpTriggerBall(self, boyGObj, 200.0f) == 0 || scpActStatusDeathFall(boyGObj) != 0) {
            _ACTWait(1);
        } else {
            break;
        }
    }
    if (gflagChk(394) != 0) {
        fade = 1;
        scpFadeOut(255.0f, 0, 0, 0);
    }
    stgmgrNextStagePreLoadForceStageSet(0);
    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;
    scpAdpcmPlayRequestFunc(kind, &sekizo_common, 1, 1, 0);
    while (sekizo_common == 0) {
        _ACTWait(1);
    }
    AdpcmPlay(sekizo_common->stream);
    if (fade != 0) {
        scpFadeIn(8.0f);
    }
    stgmgrNextStagePreLoadDistBoyMode();
    scpKillEnemyAll();
    scpMaskGeneratorAll();
    stage_SetAnimation(anim, 1, 0);
    ReviveAllCarryableItemsWithNonSleepFrame(250);
    sekizo_yure = iosPadActRequest(boyPad, 9);
    sekizo_yure_vol = 128;
    iosPadActVolumeSet(sekizo_yure, 128);
    scpPlayStart(girlGObj);
    scpPlayMot(girlGObj, 532);
    scpPlayPosSet(girlGObj, gx, gy, gz);
    scpPlayPosSet(boyGObj, bx, by, bz);
    _ACTWait(1);
    {
        Vec16 v;

        sceVu0SubVector(v.f, test_CURRENTROOT(self), test_CURRENTROOT(girlGObj));
        scpPlayMotDir(girlGObj, v.f);
        scpBoyControlReadDisable = 1;
        sceVu0SubVector(v.f, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
        scpPlayMotDir(boyGObj, v.f);
        scpSekizouCheckPoint();
        scpPlayMot(girlGObj, 645);
        scpPlayWaitMotEnd(girlGObj);
        gflagOn(flag);
        if (anim2 != 0) {
            int h;

            stage_SetAnimation(anim2, 1, 0);
            v = sekizouSePos;
            soundSeDefPlay(1220, 0, v.f, 1);
            _ACTWait(30);
            h = soundSeDefPlay(1221, 0, v.f, 1);
            _ACTWait(38);
            soundSeDefStop(h);
            soundSeDefPlay(1222, 0, v.f, 1);
        }
    }
    while (stage_CheckAnimationFrame(anim, 180, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    iosPadActStop(sekizo_yure);
    while (stage_CheckAnimationFinish(anim) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    scpPlayMot(girlGObj, 532);
    scpPlayEnd(girlGObj);
    lt_switch_layout(54);
    scpBoyControlReadDisable = 0;
}

inline void InitStageChange(void)
{
    stageChangeReq = 0;
}

inline int RequestStageChange(int no, GObj *g, GObj *girl, float speed, float wait)
{
    return RequestStageChangeWithColor(no, g, girl, speed, wait, 0, 0, 0);
}

inline int RequestStageChangeWithColor(int no, GObj *g, GObj *girl, float speed, float wait,
                                       unsigned char r, unsigned char gr, unsigned char b)
{
    int ret;
    short next;
    Act *act;

    next = stageData[stage_no].ent[no - 1];
    ret = 0;
    if (gameover_flag == 0 && stageChangeReq == 0 && next != 0) {
        if (g != 0) {
            act = GOBJ_ACT(g);
            ACTGame_StageChangeGObj(g, next);
            if (act->weapon != 0) {
                ACTGame_StageChangeGObj(act->weapon, next);
            }
            if (act->curItem != 0) {
                ACTGame_StageChangeGObj(act->curItem, next);
            }
            BoyInfoUpdate_StageChange();
        }
        if (girl != 0) {
            ACTGame_StageChangeGObj(girlGObj, next);
        }
        stgmgrForceSwitchWithFadeColor(exitData[next].nextStage, speed, wait, r, gr, b);
        ret = 1;
        stageChangeReq = 1;
    }
    return ret;
}

inline int RequestStageChangeSimple(int no, float speed, float wait, unsigned char r,
                                    unsigned char gr, unsigned char b)
{
    int ret = 0;

    if (gameover_flag == 0) {
        if (stageChangeReq == 0) {
            stgmgrForceSwitchWithFadeColor(no, speed, wait, r, gr, b);
            stageChangeReq = 1;
            ret = 1;
        }
    }
    return ret;
}

/* the root position a direct stage change parks the actor at, far out of the map */
static const Vec16 farRootPos = {{-1000000.0f, 0.0f, 0.0f, 0.0f}}; /* derived name */

inline void RequestStageChangeDirect(GObj *self, int stage, void *dir, int deg)

{
    Vec16 pos;
    ACTGame_StageChangeGObjDirect(self, stage, dir, deg);
    ACTCharctrl_Lock(self);
    pos = farRootPos;
    SetDirectRootPosition(self, &pos);
    iosOmSendMail(self, 39, self);
}

inline void scpFadeOut(float speed, int r, int g, int b)
{
    fadeStatus = 1;
    fadeSpeed = speed;
    fadeContinue = 1;
    fadeColor[0] = r;
    fadeColor[1] = g;
    fadeColor[2] = b;
}

inline void scpFadeIn(float f)
{
    fadeStatus = 1;
    fadeContinue = 0;
    fadeSpeed = -f;
}

inline int scpFadeChk(void)
{
    int v = fadeStatus;
    if (v == 0) {
        return 0;
    }
    if (v == 3) {
        v = 0;
        return v;
    }
    return 1;
}

void _SCPBoySupportGirl(float x0, float y0, float z0, float x1, float y1, float z1)
{
    float v0[4] = {x0, y0, z0};
    float v1[4] = {x1, y1, z1};
    WallCfg *wc;

    if (boyGObj == 0 || girlGObj == 0) {
        return;
    }
    sceVu0ScaleVector(v0, v0, -1.0f);
    sceVu0ScaleVector(v1, v1, -1.0f);
    wc = scpGetWallCollision(v0[0], v0[1], v0[2], v1[0], v1[1], v1[2]);
    if (wc != 0) {
        GOBJ_ACT(boyGObj)->env.supportReq.b.wall = *wc;
        GOBJ_ACT(girlGObj)->env.supportReq.a.wall = *wc;
        iosOmSendMail(boyGObj, 385, boyGObj);
        iosOmSendMail(girlGObj, 386, boyGObj);
    }
}

inline int _SCPMoveCharactorByWay(GObj *self, ICO_WORD tgt, float *dir, float speed, int flags)
{
    Act *act = GOBJ_ACT(self);

    act->flags18.ll |= 1ULL << 47;
    ACTCharctrl_Lock(self);
    ACTSendMailCorrect(self, 262);
    ACTWayExec_Position(self, tgt, dir, speed, flags);
    act->flags18.ll &= ~(1ULL << 47);
    ACTCharctrl_Unlock(self);
    return 0;
}

void _SCPMoveCharactorByWay_Cancel(GObj *self)
{
    GOBJ_ACT(self)->flags18.ll &= ~(1ULL << 47);
    ACTCharctrl_Unlock(self);
}

inline void _SCPCharacterStop(GObj *self)
{
    Act *p = GOBJ_ACT(self);
    p->dir[0] = 0;
    p->dir[1] = 0;
    p->dir[2] = 0;
    p->stick.x = p->stick.y = 127;
    p->stick.mag = 0;
}

inline int _SCPMoveByWay_ToChar(GObj *self, GObj *target, int deg, int flags, float scale,
                                float speed)
{
    float v[4];
    float w[4];

    v[0] = test_CURRENTORIENT(target)[0];
    v[1] = test_CURRENTORIENT(target)[1];
    v[2] = test_CURRENTORIENT(target)[2];
    _ApplyRyGV(v, (float)deg * 3.1415927f / 180.0f);
    sceVu0ScaleVector(v, v, scale);
    sceVu0AddVector(w, test_CURRENTROOT(target), v);
    return _SCPMoveCharactorByWay(self, (ICO_WORD)target, w, speed, flags);
}

inline int scpGameStat_BoyWeaponkind(void)
{
    GObj *w = GOBJ_ACT(boyGObj)->weapon;
    if (w == 0)
        return 0;
    return CheckWeaponKind(w);
}

void scpSekizouCheckPoint(void)
{
    int was;

    if (girlGObj != 0) {
        gamesysObjInfoPosSetStage(girlGObj, GOBJ_ACT(girlGObj)->infoPos, 0, stage_no);
    }
    gamesysObjInfoPosSetStage(boyGObj, GOBJ_ACT(boyGObj)->infoPos, 0, stage_no);
    was = gflagChk(381);
    gflagOn(381);
    CheckPoint();
    if (was == 0) {
        gflagOff(381);
    }
}

inline int scpIsWallLever2On(void)
{
    return IsWallLeverStatus();
}

inline int scpIsHangChain(GObj *self)
{
    return ACTGame_isHangChain(self) != 0;
}

inline int scpIsHangChainOptional(GObj *self, int b)
{
    register GObj *p;        /* v1 */
    register int b_save;     /* s0 */
    register unsigned int v; /* v0 */
    b_save = b;
    p = ACTGame_isHangChain(self);
    v = 0;
    if (p == 0)
        goto out;
    v = (unsigned int)(p->labelId ^ b_save) < 1;
out:
    return (int)v;
}

void scpWakeupEnemyOne(int id)
{
    void *rc = isysGObjSearchFromObjLayoutID(id);
    if (rc) {
        iosOmSendMail(rc, 31, rc);
    }
}

inline void scpWakeupEnemyAll(void)
{
    GObj *g;
    for (g = isysGObjSearchFromObjKindID_begin(4); g != 0;
         g = isysGObjSearchFromObjKindID_next(g)) {
        iosOmSendMail(g, 31, g);
    }
    for (g = isysGObjSearchFromObjKindID_begin(62); g != 0;
         g = isysGObjSearchFromObjKindID_next(g)) {
        iosOmSendMail(g, 31, g);
    }
}

void scpSleepEnemyOne(int id)
{
    void *rc = isysGObjSearchFromObjLayoutID(id);
    if (rc) {
        iosOmSendMail(rc, 32, rc);
    }
}

void scpSleepSpiderGroupOne(int id)
{
    void *v = isysGObjSearchFromObjLayoutID(id);
    if (v) {
        SleepSpiderGroup(v);
    }
}

void scpWakeupSpiderGroupOne(int id)
{
    void *v = isysGObjSearchFromObjLayoutID(id);
    if (v) {
        WakeupSpiderGroup(v);
    }
}

inline void scpSleepEnemyAll(void)
{
    GObj *g;
    for (g = isysGObjSearchFromObjKindID_begin(4); g != 0;
         g = isysGObjSearchFromObjKindID_next(g)) {
        iosOmSendMail(g, 32, g);
    }
    for (g = isysGObjSearchFromObjKindID_begin(62); g != 0;
         g = isysGObjSearchFromObjKindID_next(g)) {
        iosOmSendMail(g, 32, g);
    }
}

inline void scpKillEnemyOne(int id)
{
    GObj *p = isysGObjSearchFromObjLayoutID(id);
    if (p != 0) {
        iosOmSendMail(p, 38, p);
        objLayout[p->labelId].reviveCount = 0;
    }
}

inline void scpKillEnemyAll(void)
{
    GObj *g;

    for (g = isysGObjSearchFromObjKindID_begin(4); g != 0;
         g = isysGObjSearchFromObjKindID_next(g)) {
        iosOmSendMail(g, 38, g);
        objLayout[g->labelId].reviveCount = 0;
    }
    for (g = isysGObjSearchFromObjKindID_begin(62); g != 0;
         g = isysGObjSearchFromObjKindID_next(g)) {
        iosOmSendMail(g, 38, g);
    }
}

inline void scpMaskGeneratorAll(void)
{
    GObj *p = isysGObjSearchFromObjKindID_begin(33);
    while (p != 0) {
        Generator_Mask(p);
        p = isysGObjSearchFromObjKindID_next(p);
    }
}

void scpKillSpiderGroup(int id)
{
    DeleteAllSpidersOfLayoutGroup(isysGObjSearchFromObjLayoutID(id));
}

inline void scpBornSpider(int n, float a, float b, float c, float d)
{
    int i;
    float t1, t2;
    int r;
    GObj *dead;
    for (i = 0; i < n; i++) {
        t1 = random_unit();
        spiderLayout.pos[1] = b;
        spiderLayout.pos[0] = a + d * (t1 + t1 - 1.0f);
        t2 = random_unit();
        spiderLayout.pos[2] = c + d * (t2 + t2 - 1.0f);
        r = rand();
        spiderLayout.obj = 1;
        spiderLayout.rot[1] = (float)((r >> 4) & 0xFFFF) * 3.1415927f * 3.0517578125e-05f;
        dead = MakeAP1GObj(&spiderLayout);
        _ACTWait(1);
        WakeUpAP1(dead);
    }
}

inline void scpSetStreamMotionRootOffset(GObj *self, float x, float y, float z)
{
    Vec4u v;
    v.f[0] = x;
    v.f[1] = y;
    v.f[2] = z;
    v.i[3] = 0;
    CopyVector(GOBJ_SUB(self)->streamOfs, &v);
}

/* the colour the item-revival boundary's wire sphere is drawn in */
static const Col4 itemBoundaryColor = {{0, 32, 16, 128}}; /* derived name */

inline void scpWakeupItemWithBoundary(float x, float y, float z, float r)
{
    float pos[4];
    Col4 col;

    pos[0] = x;
    pos[1] = y;
    pos[2] = z;
    ((int *)pos)[3] = 0;
    ReviveCarryableItemsWithBoundary(pos, r);
    if (debug_wallcheck_flag != 0) {
        MatrixDrive_PushMatrix();
        col = itemBoundaryColor;
        gif_StartPacketPri(11);
        sceVu0UnitMatrix(MatrixDrive_GetMatrix());
        MatrixDrive_TransMatrixV(pos);
        prim_DispWireSphere(r, &col, 16, 8);
        gif_EndPacket();
        MatrixDrive_PopMatrix();
    }
}

inline int scpCheckReadyAllObjects(void)
{
    return CheckReadyAllSwitches() != 0;
}

inline void ScpCallCameraOff(void)
{
    GObj *g = boyGObj;
    if (g != 0) {
        GOBJ_ACT(g)->flags20.ll &= ~(1ULL << 23);
    }
}

inline void ScpCallCameraOn(void)
{
    GObj *g = boyGObj;
    if (g != 0) {
        GOBJ_ACT(g)->flags20.ll |= 0x800000;
    }
}

inline void ScpCallCameraSetTarget(float x, float y, float z)
{
    /* the camera target is the NEGATED point, as a homogeneous vector */
    float pos[4] = {-x, -y, -z, 1.0f};
    GObj *g = boyGObj;

    if (g != 0) {
        ActStatus *st = &GOBJ_ACT(g)->flags20;
        st->ll = (st->ll & ~(3ULL << 24)) | (1ULL << 24);
        scriptCameraTarget[0] = pos[0];
        scriptCameraTarget[1] = pos[1];
        scriptCameraTarget[2] = pos[2];
    }
}

inline void ScpCallCameraTargetOff(void)
{
    GObj *g = boyGObj;
    if (g != 0) {
        GOBJ_ACT(g)->flags20.ll &= ~(3ULL << 24);
    }
}

inline void ScpCallCameraGetTarget(float *dst)
{
    dst[0] = scriptCameraTarget[0];
    dst[1] = scriptCameraTarget[1];
    dst[2] = scriptCameraTarget[2];
}

inline void scpTransGObj(void *self, float dx, float dy, float dz)
{
    float buf[4];
    GetRootPosition(buf, self);
    buf[0] = buf[0] + dx;
    buf[1] = buf[1] + dy;
    buf[2] = buf[2] + dz;
    SetDirectRootPosition(self, buf);
}

inline void scpExplodeSecretItem(void)
{
    void *o = (void *)isysGObjSearchFromObjKindID_begin(19);
    while (o) {
        if (GetItemKind(o) == 6 && CheckItemDead(o) == 0) {
            BreakItemFromOutside(o);
            return;
        }
        o = (void *)isysGObjSearchFromObjKindID_next(o);
    }
}

void preload(int idx)
{
    short s;

    s = stageData[stage_no].ent[idx - 1];
    stgmgrNextStagePreLoadForceStageSet(exitData[s].nextStage);
    stgmgrNextStagePreLoadForceNoCancel(1);
}

inline int scpActStatusDeathFall(GObj *self)
{
    Sub15C *sub;

    switch ((unsigned int)GOBJ_ACT(self)->actMode) {
    case 4:
    case 5:
    case 62:
        break;
    case 22:
    case 24:
        return 1;
    default:
        return 0;
    }

    sub = GOBJ_SUB(self);
    if (stage_no == 34) {
        if (_ACTGame_GetParamF(2) - 200.0f < sub->ctrl.groundHeight) {
            return 1;
        }
        sub = GOBJ_SUB(self);
    }
    if (_ACTGame_GetParamF(2) < sub->ctrl.groundHeight) {
        return 1;
    }
    sub = GOBJ_SUB(self);
    if (!(_ACTGame_GetParamF(2) < sub->ctrl.fallHeight)) {
        return 0;
    }
    return 1;
}

void scpSetBoyWeaponGObj(void *w)
{
    SetBoyWeaponGObj(w);
}

inline int scpCheckExistAliveEnemy(void)
{
    GObj *g;
    for (g = isysGObjSearchFromObjKindID_begin(4); g != 0;
         g = isysGObjSearchFromObjKindID_next(g)) {
        if (actEnemyFlagCheckDead(g) == 0) {
            /* found a living enemy */
            debug_StdPrintfDummy("scpCheckExistAliveEnemy: 生きている敵を発見\n");
            return 1;
        }
    }
    /* no living enemy */
    debug_StdPrintfDummy("scpCheckExistAliveEnemy: 生きている敵はいません\n");
    return 0;
}

inline int scpCheckExistAliveSpider(void)
{
    GObj *g;
    for (g = isysGObjSearchFromObjKindID_begin(62); g != 0;
         g = isysGObjSearchFromObjKindID_next(g)) {
        if (IsActCharDead(g) == 0) {
            /* found a living spider */
            debug_StdPrintfDummy("scpCheckExistAliveSpider: 生きている蜘蛛を発見\n");
            return 1;
        }
    }
    /* no living spider */
    debug_StdPrintfDummy("scpCheckExistAliveSpider: 生きている蜘蛛はいません\n");
    return 0;
}

inline void scpLockMaxRotate(GObj *self, float rot)
{
    GOBJ_ACT(self)->flags20.ll |= (1ULL << 33);
    GOBJ_WORK(self)->lockedMaxRotate = rot;
}

inline void scpUnLockMaxRotate(GObj *self)
{
    GOBJ_ACT(self)->flags20.ll &= ~(1ULL << 33);
}

inline void scpCheckDisconnectWallStart(GObj *self)
{
    GOBJ_ACT(self)->flags18.ll |= (1ULL << 58);
}

inline void scpCheckDisconnectWallEnd(GObj *self)
{
    GOBJ_ACT(self)->flags18.ll &= ~(1ULL << 58);
}

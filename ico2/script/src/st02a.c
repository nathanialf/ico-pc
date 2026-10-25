#include "st02a.h"
#include "layout_texture.h"
#include "pad.h"
#include "thread.h"
#include "s_init.h"
#include "act.h"
#include "gobj_process.h"
#include "commonact.h"
#include "way_llf.h"
#include "camera-root.h"
#include "generator.h"
#include "gflag.h"
#include "StageAnimation.h"
#include "box.h"
#include "matrixDrive.h"
#include "motionManager2.h"
#include "particleLayout.h"
#include "pool.h"
#include "typedef.h"
#include "main.h"
#include "script.h"

static void actSt02aFenceOpenSub(GObj *volatile self);
/* .data: one mail record per posting site. Word 0 of each entry is the mail id
 * the entry answers (430 the actor post, 429 the trailing entry); .func is
 * filled in at run time before the post, except in the two main-mail records,
 * which answer 406 and 407 with their switch threads. Each record is named for
 * the thread that owns and posts it; where one thread owns two, for the
 * watcher it installs. */
void actSt02aFenceSwitch(GObj *volatile self);
void actSt02aGondolaSwitch(GObj *volatile self);

static ActMail door_down_start_mail_pal[2] = {{430}, {429}}; /* derived name */

static ActMail door_down_start_mail_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail door_down_start_mail_aug6[2] = {{393}, {392}}; /* derived name */

#define door_down_start_mail                                                                       \
    PAL_NTSC_AUG6(door_down_start_mail_pal, door_down_start_mail_ntsc, door_down_start_mail_aug6)

static ActMail door_up_start_mail_pal[2] = {{430}, {429}}; /* derived name */

static ActMail door_up_start_mail_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail door_up_start_mail_aug6[2] = {{393}, {392}}; /* derived name */

#define door_up_start_mail                                                                         \
    PAL_NTSC_AUG6(door_up_start_mail_pal, door_up_start_mail_ntsc, door_up_start_mail_aug6)

static ActMail door_up_chk_mail_pal[2] = {{430}, {429}}; /* derived name */

static ActMail door_up_chk_mail_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail door_up_chk_mail_aug6[2] = {{393}, {392}}; /* derived name */

#define door_up_chk_mail                                                                           \
    PAL_NTSC_AUG6(door_up_chk_mail_pal, door_up_chk_mail_ntsc, door_up_chk_mail_aug6)

static ActMail door_down_chk_mail_pal[2] = {{430}, {429}}; /* derived name */

static ActMail door_down_chk_mail_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail door_down_chk_mail_aug6[2] = {{393}, {392}}; /* derived name */

#define door_down_chk_mail                                                                         \
    PAL_NTSC_AUG6(door_down_chk_mail_pal, door_down_chk_mail_ntsc, door_down_chk_mail_aug6)

static ActMail fence_main_mail_pal[2] = {{406, actSt02aFenceSwitch}, {429}}; /* derived name */

static ActMail fence_main_mail_ntsc[2] = {{376, actSt02aFenceSwitch}, {396}}; /* derived name */

static ActMail fence_main_mail_aug6[2] = {{372, actSt02aFenceSwitch}, {392}}; /* derived name */

#define fence_main_mail                                                                            \
    PAL_NTSC_AUG6(fence_main_mail_pal, fence_main_mail_ntsc, fence_main_mail_aug6)

static ActMail fence_mail_pal[2] = {{430}, {429}}; /* derived name */

static ActMail fence_mail_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail fence_mail_aug6[2] = {{393}, {392}}; /* derived name */

#define fence_mail PAL_NTSC_AUG6(fence_mail_pal, fence_mail_ntsc, fence_mail_aug6)

static ActMail fence_switch_mail_pal[2] = {{430}, {429}}; /* derived name */

static ActMail fence_switch_mail_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail fence_switch_mail_aug6[2] = {{393}, {392}}; /* derived name */

#define fence_switch_mail                                                                          \
    PAL_NTSC_AUG6(fence_switch_mail_pal, fence_switch_mail_ntsc, fence_switch_mail_aug6)

static ActMail waterfall_mail_pal[2] = {{430}, {429}}; /* derived name */

static ActMail waterfall_mail_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail waterfall_mail_aug6[2] = {{393}, {392}}; /* derived name */

#define waterfall_mail PAL_NTSC_AUG6(waterfall_mail_pal, waterfall_mail_ntsc, waterfall_mail_aug6)

static ActMail gondola_main_mail_pal[2] = {{407, actSt02aGondolaSwitch}, {429}}; /* derived name */

static ActMail gondola_main_mail_ntsc[2] = {{377, actSt02aGondolaSwitch}, {396}}; /* derived name */

static ActMail gondola_main_mail_aug6[2] = {{373, actSt02aGondolaSwitch}, {392}}; /* derived name */

#define gondola_main_mail                                                                          \
    PAL_NTSC_AUG6(gondola_main_mail_pal, gondola_main_mail_ntsc, gondola_main_mail_aug6)

static ActMail gondola_mail_pal[2] = {{430}, {429}}; /* derived name */

static ActMail gondola_mail_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail gondola_mail_aug6[2] = {{393}, {392}}; /* derived name */

#define gondola_mail PAL_NTSC_AUG6(gondola_mail_pal, gondola_mail_ntsc, gondola_mail_aug6)

static ActMail gondola_switch_down_mail_pal[2] = {{430}, {429}}; /* derived name */

static ActMail gondola_switch_down_mail_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail gondola_switch_down_mail_aug6[2] = {{393}, {392}}; /* derived name */

#define gondola_switch_down_mail                                                                   \
    PAL_NTSC_AUG6(gondola_switch_down_mail_pal, gondola_switch_down_mail_ntsc,                     \
                  gondola_switch_down_mail_aug6)

static ActMail gondola_switch_up_mail_pal[2] = {{430}, {429}}; /* derived name */

static ActMail gondola_switch_up_mail_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail gondola_switch_up_mail_aug6[2] = {{393}, {392}}; /* derived name */

#define gondola_switch_up_mail                                                                     \
    PAL_NTSC_AUG6(gondola_switch_up_mail_pal, gondola_switch_up_mail_ntsc,                         \
                  gondola_switch_up_mail_aug6)

static ActMail gondola_up_mail_pal[2] = {{430}, {429}}; /* derived name */

static ActMail gondola_up_mail_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail gondola_up_mail_aug6[2] = {{393}, {392}}; /* derived name */

#define gondola_up_mail                                                                            \
    PAL_NTSC_AUG6(gondola_up_mail_pal, gondola_up_mail_ntsc, gondola_up_mail_aug6)

static ActMail gondola_down_mail_pal[2] = {{430}, {429}}; /* derived name */

static ActMail gondola_down_mail_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail gondola_down_mail_aug6[2] = {{393}, {392}}; /* derived name */

#define gondola_down_mail                                                                          \
    PAL_NTSC_AUG6(gondola_down_mail_pal, gondola_down_mail_ntsc, gondola_down_mail_aug6)

static ActMail ene_mail_pal[2] = {{430}, {429}}; /* derived name */

static ActMail ene_mail_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail ene_mail_aug6[2] = {{393}, {392}}; /* derived name */

#define ene_mail PAL_NTSC_AUG6(ene_mail_pal, ene_mail_ntsc, ene_mail_aug6)

static ActMail way_off_start_mail_pal[2] = {{430}, {429}}; /* derived name */

static ActMail way_off_start_mail_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail way_off_start_mail_aug6[2] = {{393}, {392}}; /* derived name */

#define way_off_start_mail                                                                         \
    PAL_NTSC_AUG6(way_off_start_mail_pal, way_off_start_mail_ntsc, way_off_start_mail_aug6)

static ActMail way_on_start_mail_pal[2] = {{430}, {429}}; /* derived name */

static ActMail way_on_start_mail_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail way_on_start_mail_aug6[2] = {{393}, {392}}; /* derived name */

#define way_on_start_mail                                                                          \
    PAL_NTSC_AUG6(way_on_start_mail_pal, way_on_start_mail_ntsc, way_on_start_mail_aug6)

static ActMail way_on_mail_pal[2] = {{430}, {429}}; /* derived name */

static ActMail way_on_mail_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail way_on_mail_aug6[2] = {{393}, {392}}; /* derived name */

#define way_on_mail PAL_NTSC_AUG6(way_on_mail_pal, way_on_mail_ntsc, way_on_mail_aug6)

static ActMail way_off_mail_pal[2] = {{430}, {429}}; /* derived name */

static ActMail way_off_mail_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail way_off_mail_aug6[2] = {{393}, {392}}; /* derived name */

#define way_off_mail PAL_NTSC_AUG6(way_off_mail_pal, way_off_mail_ntsc, way_off_mail_aug6)

static ActMail taki_way_mail_pal[2] = {{430}, {429}}; /* derived name */

static ActMail taki_way_mail_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail taki_way_mail_aug6[2] = {{393}, {392}}; /* derived name */

#define taki_way_mail PAL_NTSC_AUG6(taki_way_mail_pal, taki_way_mail_ntsc, taki_way_mail_aug6)

static ActMail taki_on_mail_pal[2] = {{430}, {429}}; /* derived name */

static ActMail taki_on_mail_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail taki_on_mail_aug6[2] = {{393}, {392}}; /* derived name */

#define taki_on_mail PAL_NTSC_AUG6(taki_on_mail_pal, taki_on_mail_ntsc, taki_on_mail_aug6)

static ActMail taki_off_mail_pal[2] = {{430}, {429}}; /* derived name */

static ActMail taki_off_mail_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail taki_off_mail_aug6[2] = {{393}, {392}}; /* derived name */

#define taki_off_mail PAL_NTSC_AUG6(taki_off_mail_pal, taki_off_mail_ntsc, taki_off_mail_aug6)

static ActMail secret_item_mail_pal[2] = {{430}, {429}}; /* derived name */

static ActMail secret_item_mail_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail secret_item_mail_aug6[2] = {{393}, {392}}; /* derived name */

#define secret_item_mail                                                                           \
    PAL_NTSC_AUG6(secret_item_mail_pal, secret_item_mail_ntsc, secret_item_mail_aug6)

void actSt02aInit(void)
{
    if (gflagChk(PAL_NTSCAUG6(118, 105)) == 0) {
        SetWayGroupActive(26, 0);
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(99, 95, 85, 84), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(99, 95, 85, 84), 0, -1);
    }
}

void actSt02aDoor(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);
    if (scpTriggerBall(self, boyGObj, 200.0f) != 0 ||
        (girlGObj != 0 && scpTriggerBall(self, girlGObj, 400.0f) != 0)) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(98, 94, 84, 83), 0, 0);
        _ACTWait(60);
        door_down_start_mail[0].func = actSt02aDoorDownChk;
        act->mail = door_down_start_mail;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(97, 93, 83, 82), 0, 0);
        door_up_start_mail[0].func = actSt02aDoorUpChk;
        act->mail = door_up_start_mail;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
        _ACTWait(0);
    }
}

/* The two-part door's SE position, copied into the local the sound call reads. */
static const ConstVec doorSePos = {{-1823.0f, -1174.0f, 2429.0f, 0.0f}}; /* derived name */

/* The door's two effect positions; the up sequence plays them in this order and
 * the down sequence in the other.  ico2/script/src/st08b.c carries the same two
 * points for its own door. */
static const ConstVec doorUpEffectPos = {{-505.0f, -1200.0f, -5671.0f, 1.0f}}; /* derived name */

static const ConstVec doorUpEffect2Pos = {{-505.0f, -1447.0f, -5671.0f, 1.0f}}; /* derived name */

/* The two boundary points the boy's splash check tests against. */
static const ConstVec boySplashPos[2] = {{{840.0f, 235.0f, 560.0f, 1.0f}},
                                         {{740.0f, 235.0f, 560.0f, 1.0f}}};

/* The waterfall's two reflection meshes and the two layout quads they are
 * stretched over: the first quad drops from y 280 to y 0, which is the falling
 * water, and the second is flat at y 0, which is the pool below it.  The value
 * at offset 0x1C is a hardware field. */
static const PoolMesh fallReflactionMesh_pal = {30, 20, 0, 0,
                                                0,  0,  0, 0x60687080}; /* derived name */

static const PoolMesh fallReflactionMesh_aug6 = {40, 20, 0, 0,
                                                 0,  0,  0, 0x60687080}; /* derived name */

#define fallReflactionMesh (*PAL_AUG6(&fallReflactionMesh_pal, &fallReflactionMesh_aug6))

static const PoolMesh poolReflactionMesh = {10, 10, 0, 0, 0, 0, 0, 0x60687080}; /* derived name */

static const PoolMeshQuad fallReflactionQuad_pal = {{{650.0f, 280.0f, 550.0f, 1.0f},
                                                     {650.0f, 0.0f, 700.0f, 1.0f},
                                                     {920.0f, 280.0f, 550.0f, 1.0f},
                                                     {920.0f, 0.0f, 700.0f, 1.0f}}};

static const PoolMeshQuad fallReflactionQuad_aug6 = {
    {{650.0f, 280.0f, 550.0f, 1.0f},
     {650.0f, 0.0f, 700.0f, 1.0f},
     {900.0f, 280.0f, 550.0f, 1.0f},
     {900.0f, 0.0f, 700.0f, 1.0f}}}; /* derived name */

#define fallReflactionQuad (*PAL_AUG6(&fallReflactionQuad_pal, &fallReflactionQuad_aug6))

static const PoolMeshQuad poolReflactionQuad = {{{650.0f, 0.0f, 700.0f, 1.0f},
                                                 {650.0f, 0.0f, 1200.0f, 1.0f},
                                                 {920.0f, 0.0f, 700.0f, 1.0f},
                                                 {920.0f, 0.0f, 1200.0f, 1.0f}}};

void actSt02aDoorUpChk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    ConstVec pos;
    int h;

    while (scpTriggerFloorAttrTargetMan(self, 0x1000000) == 0) {
        _ACTWait(1);
    }
    _ACTWait(15);
    actCreateSubThread(actSt02aDoorUpEffect, 21);
    scpWakeupItemWithBoundary(-1827.0f, -1072.0f, 2285.0f, 100.0f);
    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(97, 93, 83, 82), 1, 0);
    pos = doorSePos;
    soundSeDefPlay(PAL_NTSCAUG6(1220, 1196), 0, pos.f, 1);
    _ACTWait(30);
    h = soundSeDefPlay(PAL_NTSCAUG6(1221, 1197), 0, pos.f, 1);
    _ACTWait(30);
    soundSeDefStop(h);
    soundSeDefPlay(PAL_NTSCAUG6(1222, 1198), 0, pos.f, 1);
    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6(97, 93, 83, 82)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    door_up_chk_mail[0].func = actSt02aDoorDownChk;
    sub->mail = door_up_chk_mail;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
    _ACTWait(0);
}

void actSt02aDoorDownChk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    ConstVec pos;
    int h;

    while (scpTriggerFloorAttrTargetMan(self, 0x1000000) != 0) {
        _ACTWait(1);
    }
    _ACTWait(15);
    actCreateSubThread(actSt02aDoorDownEffect, 21);
    scpWakeupItemWithBoundary(-1827.0f, -1072.0f, 2285.0f, 100.0f);
    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(98, 94, 84, 83), 1, 0);
    pos = doorSePos;
    soundSeDefPlay(PAL_NTSCAUG6(1220, 1196), 0, pos.f, 1);
    _ACTWait(30);
    h = soundSeDefPlay(PAL_NTSCAUG6(1221, 1197), 0, pos.f, 1);
    _ACTWait(30);
    soundSeDefStop(h);
    soundSeDefPlay(PAL_NTSCAUG6(1222, 1198), 0, pos.f, 1);
    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6(98, 94, 84, 83)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    door_down_chk_mail[0].func = actSt02aDoorUpChk;
    sub->mail = door_down_chk_mail;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
    _ACTWait(0);
}

/* .sdata */
SqEntry *st02a_fence = 0;

/* .sbss: the demo's own end flag, raised by the subthread the wait loop below
   spins for. */
static int demoEnd;

static void actSt02aFenceOpen_ntsc(GObj *volatile self)
{
    scpSleepEnemyAll();
    gflagOn(105);
    scpAdpcmPlayRequestFunc(94, &st02a_fence, 1, 1, 1);
    while (st02a_fence == 0) {
        _ACTWait(1);
    }
    stage_SetAnimation(85, 1, 0);
    stage_SetAnimation(86, 1, 0);
    while (stage_CheckAnimationFinish(86) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    SetWayGroupActive(26, 1);
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
    scpWakeupEnemyAll();
}

static void actSt02aFenceOpen_aug6(GObj *volatile self)
{
    scpSleepEnemyAll();
    gflagOn(105);
    scpAdpcmPlayRequestFunc(94, &st02a_fence, 1, 1, 1);
    while (st02a_fence == 0) {
        _ACTWait(1);
    }
    stage_SetAnimation(84, 1, 0);
    stage_SetAnimation(85, 1, 0);
    while (stage_CheckAnimationFinish(85) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    SetWayGroupActive(26, 1);
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
    scpWakeupEnemyAll();
}

void actSt02aFenceOpen(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt02aFenceOpen_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt02aFenceOpen_aug6(self);
        return;
    }
    GProc *th;

    scpSleepEnemyAll();
    gflagOn(118);
    scpAdpcmPlayRequestFunc(96, &st02a_fence, 1, 1, 1);
    th = actCreateSubThread(actSt02aFenceOpenSub, 21);
    demoEnd = 0;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(&th->thread, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);
        while (st02a_fence == 0) {
            _ACTWait(1);
        }
        scpAdpcmFadeCloseFunc(&st02a_fence, 512);
        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }
        stage_SetAnimation(PAL_JPNKORCN(99, 95), 0, -1);
        stage_SetAnimation(PAL_JPNKORCN(100, 96), 0, -1);
        SetCameraFlag_GamecamCutBack();
        scpFadeIn(8.0f);
        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
    }
    SetWayGroupActive(26, 1);
    scpBoyControlReadDisable = 0;
    lt_switch_layout(PAL_JPNKORCN(54, 51));
    scpWakeupEnemyAll();
}

void actSt02WaterFallBoySplashCheck(GObj *volatile self)
{
    ConstVec buf[2];
    ConstVec buf2;
    int idx;
    if (boyGObj == 0)
        return;
    buf[0].d[0] = boySplashPos[0].d[0];
    buf[0].d[1] = boySplashPos[0].d[1];
    buf[1].d[0] = boySplashPos[1].d[0];
    buf[1].d[1] = boySplashPos[1].d[1];
    for (;;) {
        idx = GetSkeltonFocusNode(boyGObj, 35);
        CopyVector(buf2.f, (float *)(GOBJ_SUB(boyGObj)->nodeMtx + (idx << 6) + 48));
        if (scpTriggerPosBall(buf[0].f, buf2.f, 100.0f))
            scpEffectStart(buf2.f, 47);
        _ACTWait(10);
        idx = GetSkeltonFocusNode(boyGObj, 35);
        CopyVector(buf2.f, (float *)(GOBJ_SUB(boyGObj)->nodeMtx + (idx << 6) + 48));
        if (scpTriggerPosBall(buf[1].f, buf2.f, 100.0f))
            scpEffectStart(buf2.f, 47);
        _ACTWait(10);
    }
}

static void actSt02aWaterFallReflactionEffect_aug6(GObj *volatile self)
{
    PoolMesh m0 = fallReflactionMesh;
    PoolMeshQuad q0 = fallReflactionQuad;

    InitLayoutedPoolReflactionMesh(&m0, &q0);
    for (;;) {
        SetLayoutedPoolReflactionMesh(&m0);
        DispLimitedPoolReflactionMesh(&m0);
        _ACTWait(1);
    }
}

void actSt02aWaterFallReflactionEffect(GObj *volatile self)
{
    if (ICO_AUG6) {
        actSt02aWaterFallReflactionEffect_aug6(self);
        return;
    }
    PoolMesh m0 = fallReflactionMesh;
    PoolMesh m1 = poolReflactionMesh;
    PoolMeshQuad q0 = fallReflactionQuad;
    PoolMeshQuad q1 = poolReflactionQuad;

    InitLayoutedPoolReflactionMesh(&m0, &q0);
    InitLayoutedPoolReflactionMesh(&m1, &q1);
    for (;;) {
        SetLayoutedPoolReflactionMesh(&m0);
        DispLimitedPoolReflactionMesh(&m0);
        SetLayoutedPoolReflactionMesh(&m1);
        DispLimitedPoolReflactionMesh(&m1);
        _ACTWait(1);
    }
}

void actSt02aWaterFallChk(GObj *volatile self)
{
    Act *act = GOBJ_ACT(boyGObj);

    act->flags20.ll &= ~PAL_NTSC_AUG6(0x80000000000LL, 0x4000000LL, 0x80000LL);
    scpSearchGobj(PAL_NTSC_AUG6(1713, 1562, 1548))->active = 1;
    scpSearchGobj(PAL_NTSC_AUG6(1670, 1558, 1544))->active = 0;
    scpSearchGobj(PAL_NTSC_AUG6(1687, 1567, 1553))->active = 0;
    scpSearchGobj(PAL_NTSC_AUG6(1688, 1568, 1554))->active = 0;
    scpSearchGobj(PAL_NTSC_AUG6(1691, 1571, 1557))->active = 0;
    DeleteParticleLayout(scpSearchGobj(PAL_NTSC_AUG6(1722, 1600, 1590)));
    DeleteParticleLayout(scpSearchGobj(PAL_NTSC_AUG6(1723, 1601, 1591)));
    DeleteParticleLayout(scpSearchGobj(PAL_NTSC_AUG6(1724, 1602, 1592)));
    DeleteParticleLayout(scpSearchGobj(PAL_NTSC_AUG6(1725, 1603, 1593)));
    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(391, 387, 355, 354), -1, -2);
    scpTransGObj(scpSearchGobj(PAL_NTSC_AUG6(1705, 1584, 1574)), 0.0f, -200.0f, 0.0f);
    scpTransGObj(scpSearchGobj(PAL_NTSC_AUG6(1707, 1586, 1576)), 0.0f, -200.0f, 0.0f);
    _ACTWait(5);
    ReInitBoxGeo(scpSearchGobj(PAL_NTSC_AUG6(1705, 1584, 1574)));
    ReInitBoxGeo(scpSearchGobj(PAL_NTSC_AUG6(1707, 1586, 1576)));
}

SqEntry *gondola = 0;

void actSt02aGondolaUp(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpAdpcmPlayRequestFunc(PAL_NTSCAUG6(87, 85), &gondola, 1, 1, 1);

    while (gondola == 0) {
        _ACTWait(1);
    }

    _ACTWait(16);
    if (ICO_NTSC) {
        stage_SetAnimation(87, 1, 0);
        gflagOn(106);
    } else if (ICO_AUG6) {
        stage_SetAnimation(86, 1, 0);
        gflagOn(106);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(101, 97), 1, 0);
        gflagOn(119);
    }

    while (PAL_JPNKORCN_NTSC_AUG6(
               stage_CheckAnimationFrame(101, 140, 0), stage_CheckAnimationFrame(97, 140, 0),
               stage_CheckAnimationFrame(87, 149, 1), stage_CheckAnimationFrame(86, 149, 1)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 16);

    if (!ICO_NTSCAUG6) {
        while (stage_CheckAnimationFrame(PAL_JPNKORCN(101, 97), 149, 1) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);
    }
    _ACTWait(120);

    if (gondola != 0) {
        if (ICO_NTSCAUG6) {
            soundDataClose(gondola);
        } else {
            scpAdpcmCloseFunc(&gondola);
        }
    }

    if (ICO_NTSC) {
        lt_switch_layout(50);
        scpBoyControlReadDisable = 0;
        scpWakeupEnemyAll();
    } else if (ICO_AUG6) {
    } else {
        lt_switch_layout(PAL_JPNKORCN(54, 51));
        scpBoyControlReadDisable = 0;
        scpWakeupEnemyAll();
    }

    gondola_up_mail[0].func = actSt02aGondolaMain;
    sub->mail = gondola_up_mail;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
    _ACTWait(0);
}

SqEntry *gondola_test = 0;

void actSt02aGondolaDown(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpAdpcmPlayRequestFunc(PAL_NTSCAUG6(87, 85), &gondola_test, 1, 1, 1);

    while (gondola_test == 0) {
        _ACTWait(1);
    }

    if (ICO_NTSC) {
        stage_SetAnimation(87, 1, 150);
        gflagOff(106);
    } else if (ICO_AUG6) {
        stage_SetAnimation(86, 1, 150);
        gflagOff(106);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(101, 97), 1, 150);
        gflagOff(119);
    }

    while (PAL_JPNKORCN_NTSC_AUG6(
               stage_CheckAnimationFrame(101, 295, 0), stage_CheckAnimationFrame(97, 295, 0),
               stage_CheckAnimationFrame(87, 300, 1), stage_CheckAnimationFrame(86, 300, 1)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 16);

    if (!ICO_NTSCAUG6) {
        while (stage_CheckAnimationFrame(PAL_JPNKORCN(101, 97), 300, 1) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);
    }
    _ACTWait(120);

    if (gondola_test != 0) {
        if (ICO_NTSCAUG6) {
            soundDataClose(gondola_test);
        } else {
            scpAdpcmCloseFunc(&gondola_test);
        }
    }

    if (ICO_NTSC) {
        lt_switch_layout(50);
        scpBoyControlReadDisable = 0;
        scpWakeupEnemyAll();
    } else if (ICO_AUG6) {
    } else {
        lt_switch_layout(PAL_JPNKORCN(54, 51));
        scpBoyControlReadDisable = 0;
        scpWakeupEnemyAll();
    }

    gondola_down_mail[0].func = actSt02aGondolaMain;
    sub->mail = gondola_down_mail;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
    _ACTWait(0);
}

void actSt02aBox(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);
    if (gflagChk(PAL_NTSCAUG6(109, 98)) != 0) {
        scpSearchGobj(PAL_NTSC_AUG6(1705, 1584, 1574))->active = 0;
        if (gflagChk(PAL_NTSCAUG6(106, 95)) != 0) {
            scpSearchGobj(PAL_NTSC_AUG6(1705, 1584, 1574))->active = 0;
            scpSearchGobj(PAL_NTSC_AUG6(1706, 1585, 1575))->active = 0;
        } else {
            scpSearchGobj(PAL_NTSC_AUG6(1707, 1586, 1576))->active = 0;
        }
    }
    if (gflagChk(PAL_NTSCAUG6(108, 97)) != 0) {
        scpSearchGobj(PAL_NTSC_AUG6(1706, 1585, 1575))->active = 0;
        scpSearchGobj(PAL_NTSC_AUG6(1707, 1586, 1576))->active = 0;
    }
    if (gflagChk(PAL_NTSCAUG6(108, 97)) == 0 && gflagChk(PAL_NTSCAUG6(109, 98)) == 0) {
        scpSearchGobj(PAL_NTSC_AUG6(1705, 1584, 1574))->active = 0;
        scpSearchGobj(PAL_NTSC_AUG6(1706, 1585, 1575))->active = 0;
        scpSearchGobj(PAL_NTSC_AUG6(1707, 1586, 1576))->active = 0;
    }
}

void actSt02aGondola(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6(119, 106)) != 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(101, 97, 87, 86), 0, 0);
        _ACTWait(10);
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(101, 97, 87, 86), 0, 149);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(101, 97, 87, 86), 0, 300);
    }

    gondola_mail[0].func = actSt02aGondolaMain;
    act->mail = gondola_mail;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
    _ACTWait(0);
}

void actSt02aFence(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6(118, 105)) == 0) {
        fence_mail[0].func = actSt02aFenceMain;
        act->mail = fence_mail;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
        _ACTWait(0);
    }
}

void actSt02aWaterFall(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6(106, 95)) != 0) {
        waterfall_mail[0].func = actSt02aWaterFallChk;
        act->mail = waterfall_mail;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
        _ACTWait(0);
    }

    scpSearchGobj(PAL_NTSC_AUG6(1713, 1562, 1548))->active = 0;

    actCreateSubThread(actSt02WaterFallBoySplashCheck, 21);
    actCreateSubThread(actSt02aWaterFallReflactionEffect, 21);
}

void actSt02aBoxEvent2(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6(110, 99)) == 0) {
        scpSearchGobj(PAL_NTSC_AUG6(1709, 1588, 1578))->active = 0;
    }
}

void actSt02aEne(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6(121, 108)) == 0) {
        ene_mail[0].func = actSt02aEneChk;
        act->mail = ene_mail;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
        _ACTWait(0);
    }
}

void actSt02aEnemy1(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);

    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(PAL_NTSCAUG6(122, 109)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    Generator_MaskOff(self);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
}

void actSt02aEnemy2(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);

    _ACTWait(1);

    Generator_Mask(self);
    if (!ICO_NTSCAUG6) {
        Generator_Mask(scpSearchGobj(1703));
    }

    while (gflagChk(PAL_NTSCAUG6(122, 109)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    Generator_MaskOff(self);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    if (!ICO_NTSCAUG6) {
        Generator_Call(scpSearchGobj(1703));
    }
}

void actSt02aSekizo(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    scpSekizou(self, PAL_NTSCAUG6(123, 110), PAL_JPNKORCN_NTSC_AUG6(102, 98, 88, 87), 0, 18, 900.0f,
               1828.0f, 1150.0f, 800.0f, 1828.0f, 1150.0f);
}

void actSt02aWay(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6(124, 111)) == 0) {
        way_off_start_mail[0].func = actSt02aWayOffChk;
        act->mail = way_off_start_mail;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
        _ACTWait(0);
    } else {
        way_on_start_mail[0].func = actSt02aWayOnChk;
        act->mail = way_on_start_mail;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
        _ACTWait(0);
    }
}

void actSt02aTakiWay(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6(106, 95)) != 0) {
        taki_way_mail[0].func = actSt02aTakiWayOnChk;
        act->mail = taki_way_mail;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
        _ACTWait(0);
    }
}

void actSt02aSecretItem(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6(114, 103)) == 0) {
        secret_item_mail[0].func = actSt02aSecretItemChk;
        act->mail = secret_item_mail;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
        _ACTWait(0);
    }
}

void actSt02aDoorEvent(int x)
{
    volatile int local = x;
}

void actSt02aDoorUpEffect(GObj *volatile self)
{
    long long b1[2];
    long long b2[2];
    long long v0a = doorUpEffectPos.d[0];
    long long v0b = doorUpEffect2Pos.d[0];
    int i;
    for (i = 0; i < 50; i++) {
        switch (i) {
        case 0:
            b1[0] = v0a;
            b1[1] = doorUpEffectPos.d[1];
            scpEffectStart(b1, 0);
            break;
        case 30:
            b2[0] = v0b;
            b2[1] = doorUpEffect2Pos.d[1];
            scpEffectStart(b2, 0);
            break;
        }
        _ACTWait(1);
    }
}

void actSt02aDoorDownEffect(GObj *volatile self)
{
    long long b1[2];
    long long b2[2];
    long long v0a = doorUpEffect2Pos.d[0];
    long long v0b = doorUpEffectPos.d[0];
    int i;
    for (i = 0; i < 50; i++) {
        switch (i) {
        case 0:
            b1[0] = v0a;
            b1[1] = doorUpEffect2Pos.d[1];
            scpEffectStart(b1, 0);
            break;
        case 30:
            b2[0] = v0b;
            b2[1] = doorUpEffectPos.d[1];
            scpEffectStart(b2, 0);
            break;
        }
        _ACTWait(1);
    }
}

void actSt02aFenceMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    if (ICO_AUG6) {
        scpBoyControlReadDisable = 0;
    }
    sub->mainMail = fence_main_mail;
    while (1) {
        _ACTWait(1);
    }
}

void actSt02aFenceSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = 0;
    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6(55, 52, 51));
    scpBoyControlReadDisable = 1;

    fence_switch_mail[0].func = actSt02aFenceOpen;
    sub->mail = fence_switch_mail;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
    _ACTWait(0);
}

static void actSt02aFenceOpenSub(GObj *volatile self)
{
    while (st02a_fence == 0) {
        _ACTWait(1);
    }
    stage_SetAnimation(PAL_JPNKORCN(99, 95), 1, 0);
    stage_SetAnimation(PAL_JPNKORCN(100, 96), 1, 0);
    while (stage_CheckAnimationFinish(PAL_JPNKORCN(100, 96)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    demoEnd = 1;
    _ACTWait(0);
}

void actSt02aGondolaMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    if (ICO_AUG6) {
        lt_switch_layout(50);
        scpBoyControlReadDisable = 0;
        scpWakeupEnemyAll();
    }
    sub->mainMail = gondola_main_mail;
    while (1) {
        _ACTWait(1);
    }
}

void actSt02aGondolaSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = 0;
    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6(55, 52, 51));
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    if (gflagChk(PAL_NTSCAUG6(119, 106)) != 0) {
        gondola_switch_down_mail[0].func = actSt02aGondolaDown;
        sub->mail = gondola_switch_down_mail;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
        _ACTWait(0);
    }

    gondola_switch_up_mail[0].func = actSt02aGondolaUp;
    sub->mail = gondola_switch_up_mail;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
    _ACTWait(0);
}

void actSt02aEneChk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpTriggerFloorAttr(girlGObj, PAL_AUG6(0x5000000, 0x3000000)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    gflagOn(PAL_NTSCAUG6(121, 108));
    gflagOn(PAL_NTSCAUG6(122, 109));
}

void actSt02aSekizoEvent(int x)
{
    volatile int local = x;
}

static void actSt02aWayOnChk_ntsc(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpCheckExistAliveEnemy() != 0 || scpTriggerFloorAttr(girlGObj, 0x4000000) == 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(53, 1);
    SetWayGroupActive(55, 1);
    SetWayGroupActive(56, 1);
    SetWayGroupActive(57, 1);
    gflagOff(111);

    way_on_mail[0].func = actSt02aWayOffChk;
    sub->mail = way_on_mail;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static void actSt02aWayOnChk_aug6(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpTriggerFloorAttr(girlGObj, 0x4000000) == 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(53, 1);
    SetWayGroupActive(55, 1);
    SetWayGroupActive(56, 1);
    SetWayGroupActive(57, 1);
    gflagOff(111);

    way_on_mail[0].func = actSt02aWayOffChk;
    sub->mail = way_on_mail;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);
}

void actSt02aWayOnChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt02aWayOnChk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt02aWayOnChk_aug6(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);

    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpCheckExistAliveEnemy() != 0 || scpTriggerFloorAttr(girlGObj, 0x4000000) == 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(53, 1);
    SetWayGroupActive(55, 1);
    SetWayGroupActive(56, 1);
    SetWayGroupActive(57, 1);
    gflagOff(124);

    way_on_mail[0].func = actSt02aWayOffChk;
    sub->mail = way_on_mail;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

static void actSt02aWayOffChk_ntsc(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpCheckExistAliveEnemy() == 0 && scpTriggerFloorAttr(girlGObj, 0x3000000) == 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(53, 0);
    SetWayGroupActive(55, 0);
    SetWayGroupActive(56, 0);
    SetWayGroupActive(57, 0);
    gflagOn(111);

    way_off_mail[0].func = actSt02aWayOnChk;
    sub->mail = way_off_mail;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static void actSt02aWayOffChk_aug6(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpTriggerFloorAttr(girlGObj, 0x3000000) == 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(53, 0);
    SetWayGroupActive(55, 0);
    SetWayGroupActive(56, 0);
    SetWayGroupActive(57, 0);
    gflagOn(111);

    way_off_mail[0].func = actSt02aWayOnChk;
    sub->mail = way_off_mail;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);
}

void actSt02aWayOffChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt02aWayOffChk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt02aWayOffChk_aug6(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);

    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpCheckExistAliveEnemy() == 0 && scpTriggerFloorAttr(girlGObj, 0x3000000) == 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(53, 0);
    SetWayGroupActive(55, 0);
    SetWayGroupActive(56, 0);
    SetWayGroupActive(57, 0);
    gflagOn(124);

    way_off_mail[0].func = actSt02aWayOnChk;
    sub->mail = way_off_mail;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

static void actSt02aTakiWayOnChk_ntsc(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpCheckExistAliveEnemy() != 0 || scpTriggerFloorAttr(girlGObj, 0x6000000) == 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(35, 1);
    SetWayGroupActive(59, 1);

    taki_on_mail[0].func = actSt02aTakiWayOffChk;
    sub->mail = taki_on_mail;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static void actSt02aTakiWayOnChk_aug6(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpTriggerFloorAttr(girlGObj, 0x6000000) == 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(35, 1);
    SetWayGroupActive(59, 1);

    taki_off_mail[0].func = actSt02aTakiWayOffChk;
    sub->mail = taki_off_mail;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);
}

void actSt02aTakiWayOnChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt02aTakiWayOnChk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt02aTakiWayOnChk_aug6(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);

    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpCheckExistAliveEnemy() != 0 || scpTriggerFloorAttr(girlGObj, 0x6000000) == 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(35, 1);
    SetWayGroupActive(59, 1);

    taki_on_mail[0].func = actSt02aTakiWayOffChk;
    sub->mail = taki_on_mail;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

static void actSt02aTakiWayOffChk_ntsc(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpCheckExistAliveEnemy() == 0 && scpTriggerFloorAttr(girlGObj, 0x5000000) == 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(35, 0);
    SetWayGroupActive(59, 0);

    taki_off_mail[0].func = actSt02aTakiWayOnChk;
    sub->mail = taki_off_mail;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static void actSt02aTakiWayOffChk_aug6(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpTriggerFloorAttr(girlGObj, 0x5000000) == 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(35, 0);
    SetWayGroupActive(59, 0);

    taki_on_mail[0].func = actSt02aTakiWayOnChk;
    sub->mail = taki_on_mail;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);
}

void actSt02aTakiWayOffChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt02aTakiWayOffChk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt02aTakiWayOffChk_aug6(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);

    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpCheckExistAliveEnemy() == 0 && scpTriggerFloorAttr(girlGObj, 0x5000000) == 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(35, 0);
    SetWayGroupActive(59, 0);

    taki_off_mail[0].func = actSt02aTakiWayOnChk;
    sub->mail = taki_off_mail;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt02aSecretItemChk(GObj *volatile self)
{
    while (scpSearchGobj(PAL_NTSC_AUG6(1770, 1639, 1625)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(60);

    scpExplodeSecretItem();
}

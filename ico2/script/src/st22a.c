#include "st22a.h"
#include "layout_texture.h"
#include "thread.h"
#include "adpcm_init.h"
#include "act.h"
#include "gobj_process.h"
#include "commonact.h"
#include "camera-root.h"
#include "gflag.h"
#include "script.h"
#include "StageAnimation.h"
#include "cage.h"
#include <libvu0.h>
#include "st04c.h"
#include "typedef.h"
#include "gamesys.h"
#include "main.h"
#include "StageManager.h"

/* .sdata: the lightning stream handle and the two animation numbers the
   lightning plays. */
struct SqEntry *lightning = 0;

/* the European disc's numbers, then the Japanese disc's (4 lower) */
static int lightningAnims_pal[2] = {758, 759}; /* derived name */

static int lightningAnims_jpn[2] = {754, 755}; /* derived name */

#define lightningAnims PAL_JPNKORCN(lightningAnims_pal, lightningAnims_jpn)

/* the lightning's volume for height y */
static inline float getVolume(float y) /* derived name */
{
    float lo = -8000.0f, hi = -1355.0f;

    if (y < lo) {
        return 0.0f;
    } else if (hi < y) {
        return 1.0f;
    }
    return (y - lo) / (hi - lo);
}

void actSt22aLightningVolime(GObj *volatile self)
{
    float *pos;
    float v;

    while (lightning == 0) {
        _ACTWait(1);
    }
    for (;;) {
        pos = GetCameraPos();
        v = getVolume(pos[2]);
        if (v < 0.1f) {
            v = 0.1f;
        }
        AdpcmVolumeSet(lightning, (int)(v * 16383.0f));
        _ACTWait(1);
    }
}

static ActMail intro_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail intro_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail intro_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail intro_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define intro_mes                                                                                  \
    PAL_NTSC_AUG6_JUN26(intro_mes_pal, intro_mes_ntsc, intro_mes_aug6, intro_mes_jun26)

/* the point the boy turns to face when the intro is skipped */
static const StVec introFacePos = {{-2000.0f, 0.0f, -1129.0f, 1.0f}}; /* derived name */

/* no header declares it; gamesys.c defines it */
void actSt22aIntroChk(GObj *volatile self);

static void actSt22aIntro_jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);
    scpAdpcmPlayRequestFunc(87, &lightning, 1, 0, 1);
    if (gflagChk(281) == 0) {
        scpFadeOut(255.0f, 0, 0, 0);
        intro_mes[0].func = actSt22aIntroChk;
        act->mail = intro_mes;
        ACTSendMailCorrect(self, 377);
        _ACTWait(0);
    }
}

void actSt22aIntro(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt22aIntro_jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    StVec pos;
    float dir[4];

    _ACTWait(1);
    lightning = 0;
    scpAdpcmPlayRequestFunc(PAL_NTSCAUG6JUN26(94, 92), &lightning, 1, 0, 1);
    actCreateSubThread(actSt22aLightningVolime, 21);
    ScpCallCameraSetTarget(7000.0f, 280.0f, 4938.0f);
    SetCageChainHangableFlag(scpSearchGobj(PAL_NTSC_AUG6JUN26(1847, 1704, 1688)), 0);
    SetCageChainHangableFlag(scpSearchGobj(PAL_NTSC_AUG6JUN26(1848, 1705, 1689)), 0);
    SetCageChainHangableFlag(scpSearchGobj(PAL_NTSC_AUG6JUN26(1849, 1706, 1690)), 0);
    SetCageChainHangableFlag(scpSearchGobj(PAL_NTSC_AUG6JUN26(1850, 1707, 1691)), 0);
    SetCageChainHangableFlag(scpSearchGobj(PAL_NTSC_AUG6JUN26(1851, 1708, 1692)), 0);
    SetCageChainHangableFlag(scpSearchGobj(PAL_NTSC_AUG6JUN26(1852, 1709, 1693)), 0);
    SetCageChainHangableFlag(scpSearchGobj(PAL_NTSC_AUG6JUN26(1853, 1710, 1694)), 0);
    SetCageChainHangableFlag(scpSearchGobj(PAL_NTSC_AUG6JUN26(1854, 1711, 1695)), 0);
    SetCageChainHangableFlag(scpSearchGobj(PAL_NTSC_AUG6JUN26(1855, 1712, 1696)), 0);
    if (gflagChk(PAL_NTSC_AUG6JUN26(324, 295, 294)) != 0) {
        return;
    }
    scpPlayPosSet(boyGObj, -808.0f, 148.0f, -1053.0f);
    if (gflagChk(PAL_NTSC_AUG6JUN26(323, 294, 293)) == 0) {
        scpFadeOut(255.0f, 0, 0, 0);
        gamesysNObjInfoInit();
        intro_mes[0].func = actSt22aIntroChk;
        act->mail = intro_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        gflagOn(PAL_NTSC_AUG6JUN26(324, 295, 294));
        pos = introFacePos;
        sceVu0SubVector(dir, &pos, test_CURRENTROOT(boyGObj));
        scpPlayMotDir(boyGObj, dir);
        _ACTWait(1);
        SetCameraFlag_GamecamCutBack();
    }
}

/* .sbss: the demo's own end flag, raised by the subthread the wait loop below
   spins for. */
static int demoEnd;

typedef struct St22Anims { /* field names derived */
    int id[2];
} St22Anims; /* derived name */

static void actSt22aIntroChk_jun26(GObj *volatile self)
{
    StVec pos;
    float dir[4];

    while (scpTriggerBall(self, boyGObj, 1000.0f) == 0) {
        _ACTWait(1);
    }
    CheckPoint();
    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpPlayStart(boyGObj);
    gflagOn(281);
    stage_SetAnimation(634, 1, 0);
    _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 10);
    scpFadeIn(6.0f);
    stage_SetAnimation(634, 1, 0);
    scpPlayMot(boyGObj, 400);
    while (stage_ContinueAnimation(634, 635) == 0) {
        _ACTWait(1);
    }
    scpPlayPosSet(boyGObj, -707.0f, 148.0f, -1112.0f);
    pos = introFacePos;
    sceVu0SubVector(dir, &pos, test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);
    scpPlayMot(boyGObj, 401);
    while (stage_CheckAnimationFinish(635) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);
    lt_switch_layout(50);
    scpBoyControlReadDisable = 0;
}

void actSt22aIntroChk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt22aIntroChk_jun26(self);
        return;
    }
    St22Anims anims;
    StVec pos;
    float dir[4];
    GProc *th;
    int fin;
    unsigned int i;

    gflagOn(PAL_NTSC_AUG6JUN26(323, 294, 293));
    scpSekizouCheckPoint();
    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(55, 52, 51));
    scpBoyControlReadDisable = 1;
    scpPlayStart(boyGObj);
    gflagOn(PAL_NTSC_AUG6JUN26(324, 295, 294));
    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(758, 754, 684, 683), 1, 0);
    _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 10);
    scpFadeIn(6.0f);
    th = actCreateSubThread(actSt22aIntroSub, 21);
    demoEnd = 0;
    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }
    fin = demoEnd ^ 1;
    if (fin != 0) {
        scpFadeOut(16.0f, 0, 0, 0);
        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
    }
    iosThreadSetPri(&th->thread, 34);
    if (fin != 0) {
        anims = *(St22Anims *)lightningAnims;
        for (i = 0; i < 2; i++) {
            stage_SetAnimation(anims.id[i], 1, -1);
            _ACTWait(1);
        }
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(759, 755, 685, 684), 1, -1);
        StabilizeAllLayoutedCage();
        scpPlayPosSet(boyGObj, -808.0f, 148.0f, -1053.0f);
        pos = introFacePos;
        sceVu0SubVector(dir, &pos, test_CURRENTROOT(boyGObj));
        scpPlayMotDir(boyGObj, dir);
        scpPlayMot(boyGObj, 0);
        _ACTWait(1);
        SetCameraFlag_GamecamCutBack();
        if (ICO_NTSCAUG6JUN26) {
            scpFadeIn(3.0f);
        } else {
            scpFadeIn(6.0f);
            while (scpFadeChk() != 0) {
                _ACTWait(1);
            }
        }
    } else {
        scpPlayMot(boyGObj, 0);
    }
    scpPlayEnd(boyGObj);
    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(54, 51, 50));
    scpBoyControlReadDisable = 0;
}

static void actSt22aIntroSub_ntsc(GObj *volatile self)
{
    StVec pos;
    float dir[4];

    stage_SetAnimation(684, 1, 0);
    scpPlayMot(boyGObj, 391);
    while (stage_ContinueAnimation(684, 685) == 0) {
        _ACTWait(1);
    }
    scpPlayPosSet(boyGObj, -707.0f, 148.0f, -1112.0f);
    pos = introFacePos;
    sceVu0SubVector(dir, &pos, test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);
    scpPlayMot(boyGObj, 392);
    while (stage_CheckAnimationFinish(685) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    demoEnd = 1;
    _ACTWait(0);
}

static void actSt22aIntroSub_aug6jun26(GObj *volatile self)
{
    StVec pos;
    float dir[4];

    stage_SetAnimation(683, 1, 0);
    scpPlayMot(boyGObj, 391);
    while (stage_ContinueAnimation(683, 684) == 0) {
        _ACTWait(1);
    }
    scpPlayPosSet(boyGObj, -707.0f, 148.0f, -1112.0f);
    pos = introFacePos;
    sceVu0SubVector(dir, &pos, test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);
    scpPlayMot(boyGObj, 392);
    while (stage_CheckAnimationFinish(684) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    demoEnd = 1;
    _ACTWait(0);
}

void actSt22aIntroSub(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt22aIntroSub_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt22aIntroSub_aug6jun26(self);
        return;
    }
    StVec pos;
    float dir[4];

    stage_SetAnimation(PAL_JPNKORCN(758, 754), 1, 0);
    scpPlayMot(boyGObj, 397);
    while (stage_ContinueAnimation(PAL_JPNKORCN(758, 754), PAL_JPNKORCN(759, 755)) == 0) {
        _ACTWait(1);
    }
    scpPlayPosSet(boyGObj, -707.0f, 148.0f, -1112.0f);
    pos = introFacePos;
    sceVu0SubVector(dir, &pos, test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);
    scpPlayMot(boyGObj, 398);
    while (stage_CheckAnimationFinish(PAL_JPNKORCN(759, 755)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    demoEnd = 1;
    _ACTWait(0);
}

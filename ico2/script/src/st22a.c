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

/* declared ahead with no size for actSt22aIntroChk; the sized definition
   follows the stream handle in .sdata */
static int lightningAnims[];

/* .sdata: the lightning stream handle and the two animation numbers the
   lightning plays. */
struct SqEntry *lightning = 0;

static int lightningAnims[2] = {758, 759}; /* derived name */

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

static ActMail intro_mes[2] = {{430}, {429}}; /* derived name */

/* the point the boy turns to face when the intro is skipped */
static const StVec introFacePos = {{-2000.0f, 0.0f, -1129.0f, 1.0f}}; /* derived name */

/* no header declares it; gamesys.c defines it */
void actSt22aIntroChk(GObj *volatile self);

void actSt22aIntro(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    StVec pos;
    float dir[4];

    _ACTWait(1);
    lightning = 0;
    scpAdpcmPlayRequestFunc(94, &lightning, 1, 0, 1);
    actCreateSubThread(actSt22aLightningVolime, 21);
    ScpCallCameraSetTarget(7000.0f, 280.0f, 4938.0f);
    SetCageChainHangableFlag(scpSearchGobj(1847), 0);
    SetCageChainHangableFlag(scpSearchGobj(1848), 0);
    SetCageChainHangableFlag(scpSearchGobj(1849), 0);
    SetCageChainHangableFlag(scpSearchGobj(1850), 0);
    SetCageChainHangableFlag(scpSearchGobj(1851), 0);
    SetCageChainHangableFlag(scpSearchGobj(1852), 0);
    SetCageChainHangableFlag(scpSearchGobj(1853), 0);
    SetCageChainHangableFlag(scpSearchGobj(1854), 0);
    SetCageChainHangableFlag(scpSearchGobj(1855), 0);
    if (gflagChk(324) != 0) {
        return;
    }
    scpPlayPosSet(boyGObj, -808.0f, 148.0f, -1053.0f);
    if (gflagChk(323) == 0) {
        scpFadeOut(255.0f, 0, 0, 0);
        gamesysNObjInfoInit();
        intro_mes[0].func = actSt22aIntroChk;
        act->mail = intro_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        gflagOn(324);
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

void actSt22aIntroChk(GObj *volatile self)
{
    St22Anims anims;
    StVec pos;
    float dir[4];
    GProc *th;
    int fin;
    unsigned int i;

    gflagOn(323);
    scpSekizouCheckPoint();
    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;
    scpPlayStart(boyGObj);
    gflagOn(324);
    stage_SetAnimation(758, 1, 0);
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
        stage_SetAnimation(759, 1, -1);
        StabilizeAllLayoutedCage();
        scpPlayPosSet(boyGObj, -808.0f, 148.0f, -1053.0f);
        pos = introFacePos;
        sceVu0SubVector(dir, &pos, test_CURRENTROOT(boyGObj));
        scpPlayMotDir(boyGObj, dir);
        scpPlayMot(boyGObj, 0);
        _ACTWait(1);
        SetCameraFlag_GamecamCutBack();
        scpFadeIn(6.0f);
        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
    } else {
        scpPlayMot(boyGObj, 0);
    }
    scpPlayEnd(boyGObj);
    lt_switch_layout(54);
    scpBoyControlReadDisable = 0;
}

void actSt22aIntroSub(GObj *volatile self)
{
    StVec pos;
    float dir[4];

    stage_SetAnimation(758, 1, 0);
    scpPlayMot(boyGObj, 397);
    while (stage_ContinueAnimation(758, 759) == 0) {
        _ACTWait(1);
    }
    scpPlayPosSet(boyGObj, -707.0f, 148.0f, -1112.0f);
    pos = introFacePos;
    sceVu0SubVector(dir, &pos, test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);
    scpPlayMot(boyGObj, 398);
    while (stage_CheckAnimationFinish(759) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    demoEnd = 1;
    _ACTWait(0);
}

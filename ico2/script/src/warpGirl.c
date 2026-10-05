#include "debug.h"
#include "gamesys.h"
#include "boyact.h"
#include "gflag.h"
#include "script.h"
#include "geometryManager.h"
/* header prototypes (order fixes the inline tail) */
#include "warpGirl.h"
#include <string.h>
#include <libvu0.h>
#include "main.h"

int warpGirlInStageSet = 0;

int warpGirlId;

inline void warpGirlInit(void)
{
    warpGirlId = 0;
}

/* the 16-byte vector this file copies whole */
typedef union WarpVec { /* derived name */ /* field names derived */
    float f[4];
    long long q[2];
} WarpVec; /* derived name */

/* .sbss: set when a warp destination has been found. */
static int warpFound; /* derived name */

/* the "this record wins" setter */
static inline void warpGirlOutSet(int id, int noSet) /* derived name */
{
    if (noSet != 0) {
        return;
    }
    warpGirlId = id;
    warpFound = 1;
}

void warpGirlOutStage(int stage, int noSet)
{
    float b0[4]; /* the trigger box, corner 0 */
    float b1[4]; /* the trigger box, corner 1 */
    float rpos[4];
    int i;
    int j;
    int hit;

    hit = 0;
    if (gameSysObjInfo[1].stage != stage) {
        return;
    }
    if (girlGObj == 0) {
        int n = 10;

        debug_StdPrintfDummy("\x1b[36m");
        while (n-- != 0) {
            /* warpGirl.c: odd to come through here outside DEBUG STAGE SELECT */
            debug_StdPrintfDummy(
                "warpGirl.c:もしDEBUG STAGE SELECTでなくてここを通ったら おかしい！");
        }
        debug_StdPrintfDummy("\x1b[0m");
        return;
    }
    warpGirlId = 0;
    for (j = 2; j < 22; j++) {
        if (gameSysObjInfo[j].no != 0 && gameSysObjInfo[j].stage == stage_no &&
            gameSysObjInfo[j].work[0] == 4) {
            hit = 1;
            break;
        }
    }
    if (hit != 0) {
        return;
    }
    warpFound = 0;
    for (i = 1; i < 25 && warpFound == 0; i++) {
        const WarpRec *w = &girlWarpList[i];

        if (w->from != stage) {
            continue;
        }
        switch (w->kind) {
        case 0:
            warpGirlOutSet(i, noSet);
            break;
        case 2:
            if (gflagChk(w->gflag) == 0) {
                continue;
            }
            /* fall through */
        case 1:
            memset(b1, 0, 16);
            b1[0] = w->box1[2];
            b1[1] = w->box1[1];
            b1[2] = w->box1[0];
            *(WarpVec *)b0 = *(WarpVec *)b1;
            memset(rpos, 0, 16);
            rpos[0] = w->box0[2];
            rpos[1] = w->box0[1];
            rpos[2] = w->box0[0];
            *(WarpVec *)b1 = *(WarpVec *)rpos;
            GetRootPosition(rpos, girlGObj);
            if (scpTriggerPosBox(rpos, b0, b1) == 0) {
                continue;
            }
            warpGirlOutSet(i, noSet);
            break;
        default:
            ICO_BREAK();
            break;
        }
    }
}

void warpGirlInStage(int stageNo)
{
    float pos[4];
    float rot[4];
    float ry;
    const WarpRec *w = &girlWarpList[warpGirlId];

    warpGirlInStageSet = 0;
    if (IsGirlEscortedInNextStage() != 0) {
        return;
    }
    warpGirlInStageSet = 1;
    if (w->to0 == stageNo) {
        pos[0] = -w->arrive0[3];
        pos[1] = -w->arrive0[2];
        pos[2] = -w->arrive0[1];
        ry = w->arrive0[0];
    } else if (w->to1 == stageNo) {
        pos[0] = -w->arrive1[3];
        pos[1] = -w->arrive1[2];
        pos[2] = -w->arrive1[1];
        ry = w->arrive1[0];
    } else if (w->to2 == stageNo) {
        pos[0] = -w->arrive2[3];
        pos[1] = -w->arrive2[2];
        pos[2] = -w->arrive2[1];
        ry = w->arrive2[0];
    } else {
        warpGirlInStageSet = 0;
        return;
    }
    rot[0] = 0.0f;
    rot[1] = ry;
    rot[2] = 0.0f;
    sceVu0ScaleVector(rot, rot, 0.017453292f);
    debug_StdPrintfDummy("girl %p\n", girlGObj);
    gamesysObjInfoPosNewStageSet(148, 2, stageNo, pos, rot);
}

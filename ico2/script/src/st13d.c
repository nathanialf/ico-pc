#include "act.h"
#include "gflag.h"
#include "script.h"
#include "geometryManager.h"
#include "st13d.h"
#include "main.h"

inline void actSt13dInit(void) {}

/* A 16-byte constant vector: the float view carries the values, the long
   long view is the one the whole-object copy reads. */
typedef union PosBox { /* field names derived */
    float f[4];
    long long lo[2];
} PosBox; /* derived name */

static const PosBox exitPos = {{854.0f, -156.0f, 0.0f, 0.0f}}; /* derived name */

static const PosBox exit2Pos = {{330.0f, 200.0f, 200.0f, 0.0f}}; /* derived name */

static const PosBox exitRPos = {{854.0f, -156.0f, -400.0f, 0.0f}}; /* derived name */

static const PosBox exitLPos = {{854.0f, -156.0f, 400.0f, 0.0f}}; /* derived name */

static void actSt13dExit_jun26(GObj *volatile self)
{
    GObj *x = self;
    PosBox pos;
    PosBox size;
    PosBox p;

    actInitialize(self);
    _ACTWait(1);

    pos = exitPos;
    size = exit2Pos;

    GetRootPosition(&p, boyGObj);

    while (scpTriggerPosBox((float *)&p, (float *)&pos, (float *)&size) == 0) {
        _ACTWait(1);
    }

    RequestStageChange(2, boyGObj, 0, 16.0f, 16.0f);
}

void actSt13dExit(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt13dExit_jun26(self);
        return;
    }
    GObj *x = self;
    PosBox pos;
    PosBox size;
    PosBox p;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(18, 17)) == 0) {
        ScpCallCameraOff();
    }

    pos = exitPos;
    size = exit2Pos;

    GetRootPosition(&p, boyGObj);

    while (scpTriggerPosBox((float *)&p, (float *)&pos, (float *)&size) == 0) {
        _ACTWait(1);
    }

    RequestStageChange(2, boyGObj, 0, 16.0f, 16.0f);
}

void actSt13dExitR(GObj *volatile self)
{
    GObj *x = self;
    PosBox pos;
    PosBox size;
    PosBox p;

    actInitialize(self);
    _ACTWait(1);

    pos = exitRPos;
    size = exit2Pos;

    GetRootPosition(&p, boyGObj);

    while (scpTriggerPosBox((float *)&p, (float *)&pos, (float *)&size) == 0) {
        _ACTWait(1);
    }

    RequestStageChange(7, boyGObj, 0, 16.0f, 16.0f);
}

void actSt13dExitL(GObj *volatile self)
{
    GObj *x = self;
    PosBox pos;
    PosBox size;
    PosBox p;

    actInitialize(self);
    _ACTWait(1);

    pos = exitLPos;
    size = exit2Pos;

    GetRootPosition(&p, boyGObj);

    while (scpTriggerPosBox((float *)&p, (float *)&pos, (float *)&size) == 0) {
        _ACTWait(1);
    }

    RequestStageChange(8, boyGObj, 0, 16.0f, 16.0f);
}

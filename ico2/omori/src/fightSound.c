#include "fightSound.h"
#include "adpcm_init.h"
#include "s_init.h"
#include "act-game.h"
#include "debug_exception.h"
#include "main.h"
#include "gamesys.h"
#include <assert.h>

/* set while the fight music is paused */
static int fightSoundPause = 0; /* derived name */

/* the fight loop's ADPCM handle, its volume and the open request */
static struct { /* field names derived */
    SqEntry *handle;
    int volume;
    AdpcmOpenReq req;
} fightSnd; /* derived name */

/* the fight music's step: 1 once the open is requested, 2 after it */
static int fightSoundState = 0; /* derived name */

/* set while the girl is held (status 9) or taken off the stage */
static int fightSoundGirlTaken = 0; /* derived name */

static void fightSoundProcessMain(void)
{
    int req;
    int cond = 0;
    int step;

    req = 0x110001;
    fightSnd.handle = soundDataAreaSearch(&req);
    if (fightSnd.handle == 0) {
        fightSnd.volume = 0;
        if (fightSoundPause == 1) {
            return;
        }
    }
    if (systemStatus[6] != 0) {
        return;
    }
    if (systemStatus[5] == 0 && fightSoundPause != 1) {
        if (boyGObj != 0) {
            cond = 0;
            if (_ACTCharStatus_Check(boyGObj, 17) != 0) {
                cond = 1;
            }
        }
        if (systemStatus[6] == 0) {
            fightSoundGirlTaken = 0;
            if ((girlGObj != 0 && _ACTCharStatus_Check(girlGObj, 9) != 0) ||
                gamesysAnotherStageTsuresari != 0) {
                fightSoundGirlTaken = 1;
            }
        }
    } else {
        fightSoundGirlTaken = 0;
        cond = 0;
    }
    if (fightSnd.handle == 0) {
        if (cond != 0 || fightSoundGirlTaken != 0) {
            if (systemStatus[6] == 0) {
                soundDataOpen(&fightSnd.req, 2, 1, 2, 0);
                if (fightSnd.req.iopBuf != 0) {
                    fightSoundState = 1;
                }
            }
        }
    }
    if (fightSnd.handle == 0) {
        return;
    }
    step = 96;
    if (fightSoundPause == 1) {
        step = 1024;
    }
    if (cond == 0 && fightSoundGirlTaken == 0) {
        fightSnd.volume -= step;
        if (fightSnd.volume < 0) {
            fightSnd.volume = 0;
        }
    } else {
        fightSnd.volume += step;
        if (fightSnd.volume > 6144) {
            fightSnd.volume = 6144;
        }
    }
    AdpcmVolumeSet(fightSnd.handle, fightSnd.volume);
    if (fightSnd.volume == 0) {
        fightSoundState = 2;
    }
}

/* PC port (package MUS3; docs/port/MUSIC.md, "Playback"): while set, the
   fight music's step does nothing.  The step finds stream 1 (battle.int) by
   its number whoever opened it, and with no fight on fades it out and
   closes it at once; the music gallery sets this while it is open so that
   its battle.int plays. */
int fightSoundHostHold = 0;

void fightSoundProcess(void)
{
    SqEntry *h;

    if (fightSoundHostHold != 0) {
        return;
    }
    switch (fightSoundState) {
    case 0:
        fightSoundProcessMain();
        break;
    case 1:
        h = soundDataOpenSync(&fightSnd.req);
        fightSnd.handle = h;
        if (h != (SqEntry *)-1) {
            if (h != 0) {
                AdpcmPlay(h->stream);
            }
            fightSoundState = 0;
        }
        break;
    case 2:
        if (fightSnd.handle != 0) {
            soundDataClose(fightSnd.handle);
        }
        fightSnd.handle = 0;
        fightSoundState = 0;
        break;
    default:
        debug_assert("src/fightSound.c", 255);
        __assert("src/fightSound.c", 255, "0");
    }
}

void fightSoundProcessRequestPause(void)
{
    fightSoundPause = 1;
}

void fightSoundClose(void)
{
    if (fightSnd.handle != 0) {
        soundDataClose(fightSnd.handle);
        fightSnd.handle = 0;
    }
}

void fightSoundProcessRequestStart(void)
{
    fightSoundPause = 0;
}

int fightSoundProcessRequestStatus(void)
{
    return fightSoundPause;
}

SqEntry *fightSoundPlayChk(void)
{
    return fightSnd.handle;
}

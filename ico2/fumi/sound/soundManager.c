#include "typedef.h"
#include "debug.h"
#include "StageManager.h"
#include "adpcm_init.h"
#include "Matrix.h"
#include "main.h"
#include "thread.h"
#include "s_init.h"
#include <sound.h>
#include "soundManager.h"

inline void sndManager(void)
{
    int mode;

    mode = -1;
    debug_StdPrintfDummy("sound manager in\n");
    debug_StdPrintfDummy("IosSndLock %d\n", IosSndLock);
    debug_StdPrintfDummy("SOUND MANAGER START\n");
    while (1) {
        iosThreadCancelWakeup(0);
        iosThreadSleep();
        while (SgSndn2RemoteSync() != 0)
            ;
        _PushVu0Registers();
        SgCalledTickProc();
        if (mpegPlay == 0) {
            soundVBlank();
        }
        if (mode != soundOutputModeGet()) {
            mode = soundOutputModeGet();
            AdpcmInterStereoVolumeSetAll();
        }
        _PopVu0Registers();
    }
}

void sndBgmReadyNextStage(int a, int b)
{
    soundDataSegNextStageNotUseClose(1, a);
    soundDataSegNextStageNotUseClose(2, a);
    soundSePlayModeStop(1);
    soundDataSegAllClose(1, 0);
    soundSeEnvNotUseClose(a, b);
}

int sndInitBgmCancelFlag;

static void sndInit_jun26(int idx)
{
    soundSeKindBuild();
    adpcmPauseRequest(0);
    soundReverbDepthSet(stageData[idx].reverbDepth);
}

void sndInit(int idx)
{
    if (ICO_JUN26) {
        sndInit_jun26(idx);
        return;
    }
    short attrOff;
    soundSeKindBuild();
    adpcmPauseRequest(0);
    attrOff = PAL_AUG6JUN26(0x18C, 0x188);
    /* the EE's record offset of the reverb depth; StgPre is larger on the
       host, so the field by name */
    (void)attrOff;
    soundReverbDepthSet(stageData[idx].reverbDepth);
}

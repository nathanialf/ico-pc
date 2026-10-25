#include "gflag.h"
#include "gamesys.h"
#include "mcard.h"
#include "boyact.h"
#include "stage_orient.h"
#include "itou_gflag.h"
#include "generator.h"
#include "lws_kyomi.h"
#include "warpGirl.h"
#include <string.h>
#include "main.h"

/* the story flag bitmap, one bit per event flag; saved and restored whole */
unsigned char gFlag[46] = {0};

/* the story flag bitmap, one bit per event flag; saved and restored whole */
static unsigned char gflags[50] = {0}; /* derived name */

/* .sdata: the game-clear state, saved and loaded with the flags, then the
   stage the save was made on. */
int gFlagGameClear = 0; /* derived name */

int gFlagSaveStage = 0;

static void gflagInit_ntsc(void)
{
    int keep = gflagChk(346);

    memset(gFlag, 0, sizeof(gFlag));
    before_stage_no = 0;
    if (keep != 0) {
        gflagOn(346);
    }
    Boy_Init();
    StageOrientInit();
    gamesysObjInfoInit();
    Generator_Init();
    warpGirlInit();
    systemStatus[3] = 0;
    memset(IosMcPreviewInfo, 0, 16);
    gamesysVersionDiff = 0;
    itouGFlagInit();
}

static void gflagInit_aug6jun26(void)
{
    int keep = gflagChk(345);

    memset(gFlag, 0, sizeof(gFlag));
    before_stage_no = 0;
    if (keep != 0) {
        gflagOn(345);
    }
    Boy_Init();
    StageOrientInit();
    gamesysObjInfoInit();
    Generator_Init();
    warpGirlInit();
    systemStatus[3] = 0;
    memset(IosMcPreviewInfo, 0, 16);
    gamesysVersionDiff = 0;
    itouGFlagInit();
}

void gflagInit(void)
{
    if (ICO_NTSC) {
        gflagInit_ntsc();
        return;
    }
    if (ICO_AUG6JUN26) {
        gflagInit_aug6jun26();
        return;
    }
    int keep = gflagChk(379);

    memset(gflags, 0, sizeof(gflags));
    before_stage_no = 0;
    if (keep != 0) {
        gflagOn(379);
    }
    Boy_Init();
    Hint_Init();
    StageOrientInit();
    gamesysObjInfoInit();
    Generator_Init();
    warpGirlInit();
    systemStatus[3] = 0;
    memset(IosMcPreviewInfo, 0, 20);
    gamesysVersionDiff = 0;
    itouGFlagInit();
}

void gflagSave(GamesysMemCursor *fp)
{
    gFlagSaveStage = stage_no;
    gamesysMemoryHandlerWrite(fp, &gFlagSaveStage, 4);
    if (ICO_NTSCAUG6JUN26) {
        gamesysMemoryHandlerWrite(fp, gFlag, sizeof(gFlag));
    } else {
        gamesysMemoryHandlerWrite(fp, &gFlagGameClear, 4);
        gamesysMemoryHandlerWrite(fp, gflags, sizeof(gflags));
    }
}

void gflagLoad(GamesysMemCursor *fp)
{
    gamesysMemoryHandlerRead(fp, &gFlagSaveStage, 4);
    if (ICO_NTSCAUG6JUN26) {
        gamesysMemoryHandlerRead(fp, gFlag, sizeof(gFlag));
    } else {
        gamesysMemoryHandlerRead(fp, &gFlagGameClear, 4);
        gamesysMemoryHandlerRead(fp, gflags, sizeof(gflags));
    }
}

int gflagChk(int bit_idx)
{
    return (PAL_NTSCAUG6JUN26(gflags[bit_idx >> 3], gFlag[bit_idx >> 3]) >> (bit_idx & 7)) & 1;
}

void gflagOn(int bit_idx)
{
    if (ICO_NTSCAUG6JUN26) {
        gFlag[bit_idx >> 3] |= 1 << (bit_idx & 7);
    } else {
        gflags[bit_idx >> 3] |= 1 << (bit_idx & 7);
    }
}

void gflagOff(int bit_idx)
{
    if (ICO_NTSCAUG6JUN26) {
        gFlag[bit_idx >> 3] &= ~(1 << (bit_idx & 7));
    } else {
        gflags[bit_idx >> 3] &= ~(1 << (bit_idx & 7));
    }
}

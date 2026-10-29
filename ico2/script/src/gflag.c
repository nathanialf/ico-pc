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

static void gflagInit_jun26(void)
{
    int keep = gflagChk(330);

    memset(gFlag, 0, sizeof(gFlag));
    before_stage_no = 0;
    if (keep != 0) {
        gflagOn(330);
    }
    Boy_Init();
    StageOrientInit();
    gamesysObjInfoInit();
    Generator_Init();
    warpGirlInit();
    systemStatus[3] = 0;
    memset(IosMcPreviewInfo, 0, 16);
    gamesysVersionDiff = 0;
}

static void gflagInit_aug6(void)
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
    if (ICO_JUN26) {
        gflagInit_jun26();
        return;
    }
    if (ICO_NTSC) {
        gflagInit_ntsc();
        return;
    }
    if (ICO_AUG6) {
        gflagInit_aug6();
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

/* PC port: port/core/region.c (the disc's flag count, the region table's
   gflag_count) and port/platform/diag_host.h (a rate-limited log line) */
int ico_region_gflag_count(void);
void ico_host_gflag_range(int id, int write, int count, void *caller);

/* PC port: a diagnostic only, the game's access goes ahead unchanged: an id
   at or past the disc's flag count (a table of another disc's numbers, as
   issue 160's keep list was) is logged with its caller */
#define GFLAG_RANGE_NOTE(id, write)                                                                \
    do {                                                                                           \
        const int count_ = ico_region_gflag_count();                                               \
        if ((unsigned int)(id) >= (unsigned int)count_) {                                          \
            ico_host_gflag_range((id), (write), count_, __builtin_return_address(0));              \
        }                                                                                          \
    } while (0)

int gflagChk(int bit_idx)
{
    GFLAG_RANGE_NOTE(bit_idx, 0); /* PC port */
    return (PAL_NTSCAUG6JUN26(gflags[bit_idx >> 3], gFlag[bit_idx >> 3]) >> (bit_idx & 7)) & 1;
}

void gflagOn(int bit_idx)
{
    GFLAG_RANGE_NOTE(bit_idx, 1); /* PC port */
    if (ICO_NTSCAUG6JUN26) {
        gFlag[bit_idx >> 3] |= 1 << (bit_idx & 7);
    } else {
        gflags[bit_idx >> 3] |= 1 << (bit_idx & 7);
    }
}

void gflagOff(int bit_idx)
{
    GFLAG_RANGE_NOTE(bit_idx, 2); /* PC port */
    if (ICO_NTSCAUG6JUN26) {
        gFlag[bit_idx >> 3] &= ~(1 << (bit_idx & 7));
    } else {
        gflags[bit_idx >> 3] &= ~(1 << (bit_idx & 7));
    }
}

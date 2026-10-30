/*
 * port/platform/game_data_verify.c: ico_game_data_ranges_verify
 * (game_data_range.h), in the program only: it names the game's globals,
 * so it links where the game library does.
 */
#include "game_data_range.h"

#include <stdio.h>
#include "../data/iop_ram.h"
#include "../game/options.h"
#include "sched.h"

/* The game's globals, as their definitions declare them (the headers'
   arrays of unknown size are checked by their first element). */
extern int systemStatus[12];               /* ico2/common/src/main.c, .data */
extern int stage_no;                       /* ico2/common/src/main.c */
extern int frame_count;                    /* ico2/common/src/main.c */
extern char stagePreLoadBuff[1024 * 2048]; /* ico2/common/src/StageManager.c, .bss */
extern void *gameSysMemoryFuncList[];      /* ico2/common/src/gamesys.c, pointers */
extern char gameSysMainSaveBuff[25596];    /* ico2/common/src/gamesys.c, .bss */
extern int gFlagGameClear;                 /* ico2/script/src/gflag.c */
extern float gameParam[];                  /* port/data/gen/table_defs.c */
extern unsigned short seKind[];            /* port/data/gen/table_defs.c */
extern char ico_scratchpad[16 * 1024];     /* ico2/seki/src/Basic.c */

/* ico_pc's own, which must stay outside: one initialised, one not. */
static volatile int verify_port_data = 1;
static volatile int verify_port_bss;

int ico_game_data_ranges_verify(char *why, size_t n)
{
    IcoMemRange r[2];
    const void *sched_tab = NULL;
    size_t sched_n = 0;
    if (ico_game_data_ranges(r) != 0) {
        if (why != NULL && n > 0)
            snprintf(why, n, "no game data sections in this program");
        return -1;
    }
    ico_sched_state_block(&sched_tab, &sched_n);
    const IcoGameDataProbe in[] = {
        {"systemStatus", systemStatus, sizeof(systemStatus)},
        {"stage_no", &stage_no, sizeof(stage_no)},
        {"frame_count", &frame_count, sizeof(frame_count)},
        {"stagePreLoadBuff", stagePreLoadBuff, sizeof(stagePreLoadBuff)},
        {"gameSysMemoryFuncList", gameSysMemoryFuncList, sizeof(gameSysMemoryFuncList[0])},
        {"gameSysMainSaveBuff", gameSysMainSaveBuff, sizeof(gameSysMainSaveBuff)},
        {"gFlagGameClear", &gFlagGameClear, sizeof(gFlagGameClear)},
        {"gameParam", gameParam, sizeof(gameParam[0])},
        {"seKind", seKind, sizeof(seKind[0])},
        {"ico_scratchpad", ico_scratchpad, sizeof(ico_scratchpad)},
    };
    const IcoGameDataProbe out[] = {
        {"the scheduler's tables", sched_tab, sched_n},
        {"ico_iop_ram", ico_iop_ram, sizeof(ico_iop_ram)},
        {"ico_mv_active", &ico_mv_active, sizeof(ico_mv_active)},
        {"the program's data", (const void *)&verify_port_data, sizeof(verify_port_data)},
        {"the program's bss", (const void *)&verify_port_bss, sizeof(verify_port_bss)},
    };
    return ico_game_data_ranges_check(r, in, (int)(sizeof(in) / sizeof(in[0])), out,
                                      (int)(sizeof(out) / sizeof(out[0])), why, n);
}

#include "itou_gflag.h"
#include "itou_boss.h"

void itouGFlagInit(void)
{
    itou_boss_gflag_init();
}

void itouGflagLoad(void)
{
    /* PC port: the June 2001 demo's save table has no itou pair (gamesys.c keeps one table
       for every disc) */
    if (ICO_JUN26) {
        return;
    }
    itou_boss_gflag_init();
}

void itouGflagSave(void) {}

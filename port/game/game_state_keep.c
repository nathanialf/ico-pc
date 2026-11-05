/*
 * port/game/game_state_keep.c
 *
 * The game's state kept across a port excursion from the title and put
 * back at the title (game_state_keep.h): Extras > Credits (credits_live.c)
 * and Extras > Unused content's test rooms (unused_live.c).  The excursion
 * moves the boy's record to its stages (an entrance, then each stage
 * change), so without it the title would come back with no boy, and its
 * camera, falling back to him when the title's animation ends, would read
 * through a null object.
 */
#include "game_state_keep.h"

#include <string.h>

#include "../core/region.h" /* ico_region_cur */

#include "gamesys.h"
#include "gflag.h"

extern int systemStatus[];

_Static_assert(sizeof(gameSysMainSaveBuff) == ICO_KEEP_SAVE_IMAGE,
               "the kept checkpoint image is gamesys.c's size");

#define GFLAG_COUNT                                                                                \
    (ico_region_cur()->gflag_count < ICO_KEEP_GFLAG_MAX ? ico_region_cur()->gflag_count            \
                                                        : ICO_KEEP_GFLAG_MAX)

void ico_game_keep_take(IcoGameKeep *k)
{
    int i;

    for (i = 0; i < GFLAG_COUNT; i++) {
        k->flags[i] = (unsigned char)gflagChk(i);
    }
    k->gameClear = gFlagGameClear;
    memset(k->world, 0, sizeof(k->world));
    gamesysMemorySave(gameSysMemoryFuncList, k->world, 0);
    memcpy(k->checkpoint, gameSysMainSaveBuff, ICO_KEEP_SAVE_IMAGE);
    for (i = 0; i < 3; i++) {
        k->status[i] = systemStatus[2 + i];
    }
}

int ico_game_keep_put_back(IcoGameKeep *k)
{
    int i;
    int n = 0;

    gamesysMemoryLoad(gameSysMemoryFuncList, k->world, 0);
    memcpy(gameSysMainSaveBuff, k->checkpoint, ICO_KEEP_SAVE_IMAGE);
    for (i = 0; i < 3; i++) {
        systemStatus[2 + i] = k->status[i];
    }

    for (i = 0; i < GFLAG_COUNT; i++) {
        if (gflagChk(i) != k->flags[i]) {
            n++;
            if (k->flags[i]) {
                gflagOn(i);
            } else {
                gflagOff(i);
            }
        }
    }
    gFlagGameClear = k->gameClear;
    return n;
}

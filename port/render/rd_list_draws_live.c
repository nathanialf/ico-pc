/*
 * port/render/rd_list_draws_live.c
 *
 * A list's draws by fragment entry and DATE mode, the renderer's side
 * (rd_list_draws.h): the replay's look-ups (the render thread), merged into
 * the block's at the replay's end, the block's taken by the window.
 */
#include "rd_list_draws.h"

#include <string.h>

#include "rd_internal.h" /* RD_FS_SLOTS, rd__replay_list */

_Static_assert(RD_FS_SLOTS <= RD_LIST_DRAWS_FS,
               "rd_list_draws: every fragment entry counted apart");

#define CELLS (RD_LIST_DRAWS_LISTS * RD_LIST_DRAWS_FS * RD_LIST_DRAWS_DATES)
#define TOUCHED_MAX 256u

/* the replay's look-ups (the render thread's alone): a count a cell and the
   cells touched, so the merge and the reset walk those alone */
static uint32_t s_cell[CELLS];
static uint16_t s_touched[TOUCHED_MAX];
static unsigned s_touchedN;
static uint32_t s_other;

/* the block's (the render thread merges, the window takes: under s_lock) */
static RdListDraws s_block;
static int s_lock;

static void lock(void)
{
    while (__atomic_test_and_set(&s_lock, __ATOMIC_ACQUIRE)) {}
}

static void unlock(void)
{
    __atomic_clear(&s_lock, __ATOMIC_RELEASE);
}

void rd__list_draws_note(uint8_t fs, uint8_t stencil)
{
    const int list = rd__replay_list();

    if (list < 0) {
        return; /* outside the game's lists (the present, a start-up set) */
    }
    if ((unsigned)list >= RD_LIST_DRAWS_LISTS || fs >= RD_LIST_DRAWS_FS) {
        s_other++;
        return;
    }
    const unsigned c = ((unsigned)list * RD_LIST_DRAWS_FS + fs) * RD_LIST_DRAWS_DATES +
                       rd_list_draws_date_of(stencil);
    if (s_cell[c] == 0u) {
        if (s_touchedN >= TOUCHED_MAX) {
            s_other++;
            return;
        }
        s_touched[s_touchedN++] = (uint16_t)c;
    }
    s_cell[c]++;
}

void rd__list_draws_replay_end(void)
{
    lock();
    s_block.replays++;
    s_block.other += s_other;
    for (unsigned i = 0; i < s_touchedN; i++) {
        const unsigned c = s_touched[i];
        const unsigned date = c % RD_LIST_DRAWS_DATES,
                       fs = (c / RD_LIST_DRAWS_DATES) % RD_LIST_DRAWS_FS;
        const unsigned list = c / (RD_LIST_DRAWS_DATES * RD_LIST_DRAWS_FS);
        s_block.n[list][fs][date] += s_cell[c];
        s_cell[c] = 0;
    }
    unlock();
    s_touchedN = 0;
    s_other = 0;
}

void rd_list_draws_take(RdListDraws *out)
{
    lock();
    memcpy(out, &s_block, sizeof(*out));
    memset(&s_block, 0, sizeof(s_block));
    unlock();
}

const char *rd_list_draws_fs_name(uint8_t fs)
{
    return rd__fs_name(fs);
}

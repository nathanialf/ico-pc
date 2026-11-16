/*
 * port/render/rd_list_draws.h
 *
 * A list's draws by fragment entry and DATE mode (issue 179).
 *
 * The Mali-G52 reporter's v0.11.7 log has list 3 as the block's most
 * expensive list in the opening's "deja" scene (23-32 ms a picture over
 * 106-158 draws, the perf classes line), and nothing says what those draws
 * are.  The renderer looks a pipeline up for each draw or merged run of
 * draws (rd_pipeline.c rd__get_pipeline); each look-up in a game list is
 * counted here by its list, its fragment entry (RdPipeKeyInt.fs, the RD_FS_*
 * names) and its DATE mode as the pipeline key carries it: the stencil
 * bit's test of DATE 0 or DATE 1 (rd.h RD_STENCIL_DATE_TEST0 / TEST1 and
 * their pairs), a DATE set-up (the bit written), or none in the key (no
 * DATE, or DATE through the snapshot the fragment entry reads: the key does
 * not tell those apart; the replay's DATE snapshots are on the performance
 * lines).  At the 10 s block's end the window names the most expensive
 * list's kinds when that list is not list 0 (window_perf_rank.c, one line,
 * RD_LIST_DRAWS_SHOW kinds and the rest summed).
 *
 * CPU-side only: a look-up adds one to a replay's table (an index and an
 * add), the replay's end merges its touched entries into the block's under a
 * spin lock (once a replay), and the window takes the block's (once a block).
 * The table and the text are pure (port/render/test/rd_list_draws_test.c);
 * the live part is rd_list_draws_live.c.
 */
#ifndef RD_LIST_DRAWS_H
#define RD_LIST_DRAWS_H

#include <stddef.h>
#include <stdint.h>

#define RD_LIST_DRAWS_LISTS 13u /* rd.h RD_LIST_COUNT */
#define RD_LIST_DRAWS_FS 96u    /* the fragment entries counted apart (RD_FS_SLOTS at most) */
#define RD_LIST_DRAWS_DATES 4u  /* RD_LIST_DRAWS_DATE_* */
#define RD_LIST_DRAWS_SHOW 8u   /* the kinds a line names (then the rest summed) */

enum {
    RD_LIST_DRAWS_DATE_NONE = 0, /* no DATE in the key (none, or the snapshot's) */
    RD_LIST_DRAWS_DATE_0,        /* the stencil bit's test, DATM 0 */
    RD_LIST_DRAWS_DATE_1,        /* the stencil bit's test, DATM 1 */
    RD_LIST_DRAWS_DATE_SETUP     /* the bit written (a DATE set-up or clear) */
};

typedef struct RdListDraws {
    uint32_t replays; /* replays merged in */
    uint32_t other;   /* look-ups with a list or entry out of the table */
    uint32_t n[RD_LIST_DRAWS_LISTS][RD_LIST_DRAWS_FS][RD_LIST_DRAWS_DATES];
} RdListDraws;

/* The DATE mode of a pipeline key's stencil mode (rd.h RdStencilMode) */
unsigned rd_list_draws_date_of(uint8_t stencil);

void rd_list_draws_reset(RdListDraws *d);
/* one look-up (list -1 or past the table, or fs past it: counted in other) */
void rd_list_draws_add(RdListDraws *d, int list, unsigned fs, unsigned date);

/* The list whose GPU time is the largest (listMs[0..count), each the
   block's sum or mean), -1 when it is list 0 or none is above 0 */
int rd_list_draws_pick(const double *listMs, unsigned count);

/* a fragment entry's name (NULL: "entry N") */
typedef const char *(*RdListDrawsName)(uint8_t fs);

/* "list 3's draws by fragment entry and DATE mode (the block's most
   expensive list, 23.1 ms a picture; pipeline look-ups a picture over 161
   replays, a look-up a draw or a merged run of draws): vu_ps no DATE in the
   key 62.3, sprite_ps DATE 0 by the stencil 21.0, ...; 3 more kinds 4.0"
   (after "window: "); the most first, ties by entry then mode; the length
   snprintf gives */
int rd_list_draws_text(const RdListDraws *d, int list, double listMs, RdListDrawsName name,
                       char *buf, size_t n);

/* --- the live part (rd_list_draws_live.c) --------------------------------- */
/* rd_pipeline.c rd__get_pipeline: a look-up of a key with fragment entry fs
   and stencil mode stencil (the render thread, in a replay's lists) */
void rd__list_draws_note(uint8_t fs, uint8_t stencil);
/* rd_perf.c at a replay's end: its look-ups into the block's (the render
   thread) */
void rd__list_draws_replay_end(void);
/* the window at the block's end: the block's table copied out and emptied
   (any thread) */
void rd_list_draws_take(RdListDraws *out);
/* a fragment entry's embedded name (rd_replay.c's table; "?" past it) */
const char *rd_list_draws_fs_name(uint8_t fs);

#endif /* RD_LIST_DRAWS_H */

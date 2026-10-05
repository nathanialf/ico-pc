/*
 * port/fmv/movie_pace.c
 *
 * movie_pace.h.  The comments name the ito/mpeg code each step follows.
 */
#include "movie_pace.h"
#include <string.h>

void ico_movie_pace_init(IcoMoviePace *p)
{
    memset(p, 0, sizeof(*p));
}

void ico_movie_pace_start(IcoMoviePace *p)
{
    p->running = 1; /* startDisplay: dispRunning = 1 */
}

int ico_movie_pace_vblank(IcoMoviePace *p, int field)
{
    /* vblankHandler: the oldest tag (voBufGetTag), status 2 -> 1 on an even
       field, 1 -> 0 on an odd one; handler_endimage then voBufDecCount */
    if (!p->running || p->count == 0) {
        return ICO_PACE_NONE;
    }
    if (field == 0 && !p->head_shown) {
        p->head_shown = 1;
        p->shown++;
        return ICO_PACE_SHOW;
    }
    if (field != 0 && p->head_shown) {
        p->head_shown = 0;
        p->count--;
        p->freed++;
        return ICO_PACE_FREE;
    }
    return ICO_PACE_NONE;
}

int ico_movie_pace_room(const IcoMoviePace *p)
{
    return p->count < ICO_MOVIE_SLOTS; /* voBufGetData: !isFull */
}

void ico_movie_pace_put(IcoMoviePace *p)
{
    p->count++; /* voBufIncCount */
    p->decoded++;
}

int ico_movie_pace_poll_due(const IcoMoviePace *p)
{
    return p->decoded >= ICO_MOVIE_POLL_AFTER;
}

int ico_movie_pace_check_end(IcoMoviePace *p, int64_t total, int freed_now)
{
    if (p->ended) {
        return p->ended;
    }
    if (p->abort_pending && (freed_now || ico_movie_pace_room(p))) {
        /* decBitStrm0 leaves its loop at the abort once a slot is free;
           readMpeg sees state 3 and ends */
        p->ended = 2;
    } else if (total >= 0 && p->decoded >= (uint64_t)total) {
        /* the last picture is in the ring: videoDecIsFlushed */
        p->ended = 1;
    }
    if (p->ended) {
        p->running = 0; /* endDisplay */
    }
    return p->ended;
}

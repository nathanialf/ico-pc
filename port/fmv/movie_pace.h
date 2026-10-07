/*
 * port/fmv/movie_pace.h
 *
 * The movie's display timing, as ito/mpeg/mv_disp.c's vblankHandler and
 * mv_vobuf.c's ring gave it on the PS2, without the hardware:
 *
 *   - the decoder fills a ring of ICO_MOVIE_SLOTS decoded pictures
 *     (voBufCreate: max = 5) as fast as it can, one picture per free slot;
 *   - once the display runs, each vblank looks at the oldest picture: on a
 *     field-0 vblank a fresh one (status 2) is shown (its even field, status
 *     1); on the next field-1 vblank its odd field is shown and its slot is
 *     freed (handler_endimage's voBufDecCount).  So every picture is on
 *     screen for exactly two vsyncs: 25 pictures a second at 50 Hz;
 *   - the player stops when the decoder has put the last picture into the
 *     ring (readMpeg's flush wait ends there), so the last pictures still in
 *     the ring are never shown, as on the PS2; an abort (the pad's START
 *     through movie_abort_check, polled once 11 pictures are decoded) stops
 *     it at the next freed slot (decBitStrm0 notices the abort only after
 *     its wait for a free slot).
 *
 * Nothing here depends on whether the pictures are actually decoded or
 * drawn, only on how many the stream holds: the movie lasts the same number
 * of vsyncs in the window and headless builds.  movie.c drives it; fmv_test
 * checks it.
 */
#ifndef ICO_PORT_FMV_MOVIE_PACE_H
#define ICO_PORT_FMV_MOVIE_PACE_H

#include <stdint.h>

#define ICO_MOVIE_SLOTS 5
#define ICO_MOVIE_POLL_AFTER 11 /* readMpeg: dec->mpeg.frameCount >= 11 */

typedef struct IcoMoviePace {
    int running;       /* startDisplay .. endDisplay */
    int count;         /* pictures in the ring (voBuf.count) */
    int head_shown;    /* the oldest picture's even field is on screen (status 1) */
    uint32_t decoded;  /* pictures put into the ring */
    uint32_t shown;    /* pictures whose first field reached the screen */
    uint32_t freed;    /* pictures shown whole and released */
    int abort_pending; /* poll() returned 1 */
    int ended;         /* 1 played out, 2 aborted */
    /* called once per picture shown (ICO_PACE_SHOW), or NULL: movie.c
       gives the watchdog its sign of life here (diag_host.h
       ico_diag_note_progress), since the whole movie runs inside one Main
       tick */
    void (*on_show)(void);
} IcoMoviePace;

enum {
    ICO_PACE_NONE = 0,
    ICO_PACE_SHOW = 1, /* show picture number p->shown - 1 (its even field) */
    ICO_PACE_FREE = 2  /* the shown picture's odd field went out; its slot is free */
};

/* Clears the state, on_show included. */
void ico_movie_pace_init(IcoMoviePace *p);
/* startDisplay: the display runs from the next vblank. */
void ico_movie_pace_start(IcoMoviePace *p);
/* One vblank with the GS's field (GS_CSR.FIELD) of that vsync: returns
   ICO_PACE_SHOW, ICO_PACE_FREE or ICO_PACE_NONE. */
int ico_movie_pace_vblank(IcoMoviePace *p, int field);
/* Whether a slot is free for the next decoded picture. */
int ico_movie_pace_room(const IcoMoviePace *p);
/* A decoded picture went into the ring (decoded or not: the timing is the
   same). */
void ico_movie_pace_put(IcoMoviePace *p);
/* Whether the player polls the pad now (11 pictures decoded). */
int ico_movie_pace_poll_due(const IcoMoviePace *p);
/* After the foreground's work for this vsync: total is the stream's picture
   count once known (-1 before the end of the stream is reached), freed_now
   whether this vsync's vblank freed a slot.  Sets and returns p->ended. */
int ico_movie_pace_check_end(IcoMoviePace *p, int64_t total, int freed_now);

#endif /* ICO_PORT_FMV_MOVIE_PACE_H */

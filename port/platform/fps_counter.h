/*
 * port/platform/fps_counter.h
 *
 * The Frame rate counter's number (Options > Display > Frame rate counter,
 * [video] show_fps), free of SDL and rd so a unit test checks it: the
 * presents per second over the last whole second or more, the figure the
 * window's 10 s line gives as "presents/s".
 *
 * Presents are counted two ways, as that line counts them: with the
 * renderer's interpolation on, each present the window makes (fps_present);
 * with it off, each frame replay the renderer made (the count rd_replay_time_max
 * reports, which the 10 s line zeroes every 10 s: a smaller count than the
 * last one means a zeroing, and the replays since then are the count).  The
 * mode in force is passed in at each tick; a change of mode starts the second
 * again.
 *
 * Zero-initialise before the first call.  fps_present and fps_tick run on
 * the main thread; fps_value may be read from any thread.
 */
#ifndef ICO_PLATFORM_FPS_COUNTER_H
#define ICO_PLATFORM_FPS_COUNTER_H

#include <stdint.h>

typedef struct FpsCounter {
    uint64_t markAt;    /* when the second began, ns (0: not begun) */
    unsigned presents;  /* presents so far (interpolation on) */
    unsigned replays;   /* replays so far (interpolation off) */
    unsigned mark;      /* the total of the mode in force at markAt */
    uint32_t lastCount; /* the replay count at the last tick */
    int interp;         /* the mode the second began in */
    _Atomic int value;  /* presents per second, 0 before the first second */
} FpsCounter;

/* one present made (interpolation on) */
void fps_present(FpsCounter *c);
/* every pace of the window: now_ns on a monotonic clock, the mode, and
   rd_replay_time_max's count since its last zeroing; once a second or more
   has passed since the second began, the value is the presents over it,
   rounded to a whole number */
void fps_tick(FpsCounter *c, uint64_t now_ns, int interp, uint32_t replayCount);
/* the last whole second's presents per second (0: none yet) */
int fps_value(const FpsCounter *c);

#endif /* ICO_PLATFORM_FPS_COUNTER_H */

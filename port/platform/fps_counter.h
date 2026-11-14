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
 * Time the game was not presenting is not a frame rate (issue 123: the
 * reporter's counter read about 5 after the app came back from 13 s in the
 * background, and after the 1.1 s a bug-report capture held the window):
 * the window notes it (fps_gap: the app back in the foreground, a pause of
 * the host such as a swapchain made again for a rotation or a mode, a
 * capture written), and the next tick starts the second again from there,
 * the value kept, no value made from a span that held such a gap.  A gap
 * between two ticks of gapNs or more counts the same, a guard for a pause
 * no note names: gapNs is the GPU hang limit in force (gpu_hang.h; 0 here:
 * its shortest, ICO_GPU_HANG_MIN_S), since a single wait for the GPU that
 * long ends the run, so no stall the run lives through makes it.  A stall
 * shorter than that (the reporter's were 365-486 ms, in clusters) is what
 * the counter is there to show: it stays inside the second and lowers the
 * value truthfully.
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
    int gap;            /* fps_gap noted since the last tick (__atomic) */
    uint64_t tickAt;    /* the last tick's now_ns (0: none yet) */
    uint64_t gapNs;     /* a gap between ticks this long restarts (0: the default) */
    unsigned restarts;  /* the seconds started again by a gap, this run */
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
/* the game was not presenting for a while (the app away, a pause of the
   host, a capture): the next tick starts the second again, the value kept.
   Any thread. */
void fps_gap(FpsCounter *c);
/* the seconds started again by a gap (a note or the guard), this run */
unsigned fps_restarts(const FpsCounter *c);

#endif /* ICO_PLATFORM_FPS_COUNTER_H */

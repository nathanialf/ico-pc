/*
 * port/platform/slow_arm.h
 *
 * Which replay an armed slow-scene dump takes (window_host.c slow_scene_poll
 * and slow_scene_check; docs/port/ANDROID_DIAGNOSTICS.md "The slow-scene
 * dump"; issue 179).
 *
 * A due block arms the dump with its mean vertex work a replay, and the
 * first replay after it at or above that mean whose frame the renderer
 * still holds is the frame (window_scene.h window_slow_scene_pick).  Until
 * v0.11.14 an arm that found none by the next block's end was dropped there
 * without a line, so a first slow block whose next block was lighter (issue
 * 179's v0.11.14 log: the first boat block, 103.9 ms a picture, lines
 * 1250-1255) left no capture and nothing said why.  Now, while armed, the
 * heaviest replay seen whose frame was still held is kept as the stand-in
 * (a heavier one replaces it, and so does any one once its frame has left
 * the ring), and at the block's end an arm still on takes that stand-in
 * when the ring still holds it, with a line; with none it is dropped with a
 * line naming why (no replay finished while armed; or none reached the
 * mean and none seen is still held), the first 8 then every 100th.  The
 * captures a run takes stay bounded as before (window_slow_scene_due's
 * count: a take at the block's end is one of them).
 *
 * The lines are written to stderr (the log) and kept for the tests
 * (slow_arm_last_line).  Pure otherwise: dump_auto_test runs it.
 */
#ifndef ICO_PLATFORM_SLOW_ARM_H
#define ICO_PLATFORM_SLOW_ARM_H

#include <stddef.h>
#include <stdint.h>

typedef struct SlowArmPick {
    unsigned looked;     /* new replays looked at while armed */
    unsigned gone;       /* of them at or above the mean, with their frame gone */
    double heaviestWork; /* the heaviest seen (held or not) */
    uint32_t bestFrame;  /* the stand-in: the heaviest seen while its frame was held */
    double bestWork;
} SlowArmPick;

typedef enum SlowArmSee {
    SLOW_ARM_WAIT = 0, /* not the frame: the arm waits for a later replay */
    SLOW_ARM_TAKE,     /* at or above the mean and held: write it now */
    SLOW_ARM_GONE      /* at or above the mean but no longer held (a line) */
} SlowArmSee;

/* a new arm: nothing seen */
void slow_arm_begin(SlowArmPick *a);

/* A new replay while armed: frame, its vertex work, picked
   (window_slow_scene_pick against the arm's mean), held (rd_frame_retained
   of frame) and bestHeld (rd_frame_retained of a->bestFrame, 0 when there
   is none).  A held replay that is not taken becomes the stand-in when
   heavier than it, or when the stand-in's frame is gone */
SlowArmSee slow_arm_see(SlowArmPick *a, uint32_t frame, double work, int picked, int held,
                        int bestHeld);

/* The block's end with the arm still on: the stand-in's frame when bestHeld
   (rd_frame_retained of a->bestFrame), its work into *work; 0 when the arm
   is dropped.  Either way a line (a drop's the first 8, then every 100th) */
uint32_t slow_arm_end(const SlowArmPick *a, int bestHeld, double meanWork, double *work);

/* the last line written ("" before any), and the arms dropped so far */
const char *slow_arm_last_line(void);
unsigned slow_arm_dropped(void);

#endif /* ICO_PLATFORM_SLOW_ARM_H */

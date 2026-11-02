/*
 * port/render/rd_pipe_finish.h
 *
 * The background finisher's choices (rd.h rd_pipelines_background_start):
 * when the start-up set stopped at the free-memory floor on a driver whose
 * floor is the set's own need (port/platform/memory_floor.h), the keys it
 * had not made yet are compiled in the background, one batch at a time
 * between pictures, on low-priority pipeline threads
 * (rhi.h rhi_create_pipelines_begin), instead of each being made when a
 * draw first needs it (a VU pipeline took 0.63-0.81 s on the PowerVR
 * DXT-48's render thread, three in one walk froze the picture for 2.2 s,
 * one start on that phone).
 *
 *   the batch   rd__finish_pick: the next keys of the set, in the set's
 *               own order from where it stopped, the ones already made (on
 *               first use meanwhile), failed or never made skipped, up to
 *               the batch's size, all with a destination read or all
 *               without (the start-up set's batch rule, rd_pipeline.c
 *               createKeys);
 *   the step    rd__finish_step: after each picture, what the finisher
 *               does: a batch in flight is taken once its compiles have
 *               ended; else the system's low-memory signal ends the
 *               finisher (the signal is real), a late game step or a slow
 *               picture holds the next batch back for RD_FINISH_HOLD_MS, a
 *               reading under the floor holds it for RD_FINISH_FLOOR_MS,
 *               and otherwise the next batch begins; no key left ends it.
 *
 * Pure C (no renderer state): the unit test is
 * port/render/test/rd_pipe_finish_test.c; rd_pipeline.c holds the keys,
 * the pipelines and the log lines.
 */
#ifndef RD_PIPE_FINISH_H
#define RD_PIPE_FINISH_H

#include <stdbool.h>
#include <stdint.h>

/* a batch of the finisher (the start-up set's RD_PIPE_BATCH) */
#define RD_FINISH_BATCH 32u
/* its compiling threads: the start-up set's take every core but one (7 on
   an 8-core phone) and on the DXT-48 its VU batches took the process from
   about 200 MB to 694 MB of compilers' memory (about 70 MB a thread); in
   play the game and render threads keep their cores and 4 threads hold
   about 280 MB at most, inside the 512 MB floor's margin */
#define RD_FINISH_THREADS 4u
/* a late step or a slow picture (RD_FINISH_SLOW_MS) holds the next batch
   back this long */
#define RD_FINISH_HOLD_MS 1000.0
/* a picture this long (less the pipelines made in it) holds it */
#define RD_FINISH_SLOW_MS 100.0
/* a reading under the floor: asked again this much later */
#define RD_FINISH_FLOOR_MS 5000.0
/* the pipeline cache written this often while the finisher runs, and at
   its end (a 39 MB file took 131 ms to write on the DXT-48) */
#define RD_FINISH_SAVE_MS 30000.0

/* The next batch: keys from *cursor to m in order; key(user, i) says what
   key i is: -1 skip it (made, failed, never to be made), 0 a key without a
   destination read, 1 one with; at most max keys, all of the first
   taken's kind (a key of the other kind ends the batch and stays for the
   next).  out[] gets the indices taken; returns how many; *cursor moves
   past every key taken or skipped. */
typedef int (*RdFinishKeyFn)(void *user, uint32_t i);
uint32_t rd__finish_pick(uint32_t m, uint32_t *cursor, uint32_t max, RdFinishKeyFn key, void *user,
                         uint32_t *out);

/* What the finisher does after a picture */
typedef enum RdFinishStep {
    RD_FINISH_IDLE = 0,    /* not on */
    RD_FINISH_WAIT_BATCH,  /* a batch's compiles go on */
    RD_FINISH_END_BATCH,   /* a batch's compiles ended: its pipelines kept */
    RD_FINISH_STOP_SIGNAL, /* the system's low-memory signal: the finisher ends */
    RD_FINISH_WAIT_HOLD,   /* a late step or slow picture: later */
    RD_FINISH_WAIT_FLOOR,  /* under the floor at the last reading: later */
    RD_FINISH_CHECK,       /* the memory to be read now (then RD_FINISH_BEGIN or a wait) */
    RD_FINISH_BEGIN,       /* the next batch */
    RD_FINISH_DONE         /* no key left: the finisher ends */
} RdFinishStep;

typedef struct RdFinishIn {
    bool on;        /* started and not ended */
    bool inflight;  /* a batch begun and not taken */
    bool finished;  /* its compiles have all ended */
    uint32_t left;  /* keys from the cursor on (0: none) */
    double nowMs;   /* the clock */
    double holdMs;  /* no batch begins before this (late step, slow picture) */
    double floorMs; /* nor before this (the last reading under the floor) */
    int memory;     /* -1 not read yet this step; 0 go; 1 under the floor; 2 the signal */
} RdFinishIn;

RdFinishStep rd__finish_step(const RdFinishIn *in);
/* the step's words for the log ("waits for a late step or a slow picture",
   ...) */
const char *rd__finish_step_words(RdFinishStep s);

#endif /* RD_PIPE_FINISH_H */

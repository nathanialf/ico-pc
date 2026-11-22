/* rd_loss_keep_block.h: the block a loss-keep copy is made into (issue 179).
 *
 * A loss-keep copy (rd_dump.c "the lost frame") serialises a frame on the
 * game thread while a slot wait has stalled.  Until v0.11.14 it grew its
 * buffer by doubling from 1 MiB there (1, 2, 4, 8 then 16 MiB for a 10 MB
 * frame, a realloc that cannot grow in place moving the bytes so far, and
 * every new page touched for the first time on the game thread): issue
 * 179's v0.11.14 log timed 27-49 ms a copy ("copy 45.2 ms, reads wait 0.0
 * ms"), where the slow-scene capture copies 10-12 MB into a block the dump
 * worker made in 4.7-5.4 ms (rd_dump_mem.h, port/platform/dump_auto.h).
 *
 * So the copy goes into a block the dump worker made and touched, as the
 * slow-scene capture's does.  The block is asked for only after the run's
 * first stalled wait has made a copy (steady play, where no wait stalls,
 * holds no block), sized from the last copy (an eighth more and 1 MiB, at
 * most RD_KEEP_BLOCK_MAX: a frame over it is not kept at all); once a copy
 * has taken it, the next is asked for.  While none is ready the copy
 * measures the frame first and makes ONE allocation of exactly its size
 * (rd_dump.c dumpFrameMemMax).  The memory is read (the host's memory
 * function, the background finisher's free-memory floor and the system's
 * low-memory signal) at most once every RD_KEEP_BLOCK_MEM_MS while a block
 * is held, asked for or wanted: under the floor (or after the signal) the
 * block is given back and none is asked for until a reading above it.
 *
 * The host (the window: port/platform/window_keep_block.c over the dump
 * worker, dump_auto.h ico_dump_auto_keep_*) is set once; without one (the
 * tests, the tools) every copy takes the measured allocation.
 *
 * Pure: no renderer state, no allocation of its own (rd_loss_keep_block_test
 * runs it over a fake host). */
#ifndef PORT_RENDER_RD_LOSS_KEEP_BLOCK_H
#define PORT_RENDER_RD_LOSS_KEEP_BLOCK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* the largest block asked for: a kept copy's own bound (rd_dump.c
   RD_LOSS_KEEP_BYTES) */
#define RD_KEEP_BLOCK_MAX ((size_t)16u << 20)
/* the memory read at most this often while a block is held, asked or wanted */
#define RD_KEEP_BLOCK_MEM_MS 1000.0

typedef struct RdLossKeepHost {
    /* the worker to make a block of `bytes` with its pages touched: 0
       asked, anything else not (busy, no thread) */
    int (*ask)(size_t bytes);
    /* the asked block: 1 made (*block, *size now the caller's, to free()),
       0 still being made, -1 none (it could not be made, or none was asked) */
    int (*take)(void **block, size_t *size);
    /* the ask withdrawn (a block being made is freed by the worker) and
       block (may be NULL), a block taken and not used, freed */
    void (*giveBack)(void *block);
    /* the memory: 0 above the floor, 1 under it, 2 the system's low-memory
       signal; why the reading's words (may be NULL) */
    int (*memory)(char *why, size_t size);
} RdLossKeepHost;

typedef struct RdKeepBlock {
    bool stalled;   /* a copy has been made this run (rd_keep_block_copied) */
    bool asked;     /* an ask is out */
    void *block;    /* a block taken from the host, not used yet */
    size_t size;    /* its bytes */
    size_t lastLen; /* the last copy's bytes */
    bool memRead;   /* the memory read once */
    int low;        /* the last reading: 0, 1, 2 */
    double memAtMs; /* its time */
    uint64_t asks, made, used, small, released, failed;
} RdKeepBlock;

/* what a tick did, the last of: */
typedef enum RdKeepBlockEvent {
    RD_KEEP_BLOCK_NONE = 0,
    RD_KEEP_BLOCK_TAKEN,    /* the asked block was made and taken */
    RD_KEEP_BLOCK_FAILED,   /* the worker could not make it */
    RD_KEEP_BLOCK_RELEASED, /* under the floor (or the signal): the block given back */
    RD_KEEP_BLOCK_ASKED,    /* a block asked for */
} RdKeepBlockEvent;

/* the block asked for after a copy of lastLen bytes: an eighth more and 1
   MiB, at most RD_KEEP_BLOCK_MAX; 0 for 0 */
size_t rd_keep_block_want(size_t lastLen);

/* The game thread, at each recycle before the copy is decided: an asked
   block taken when made; the memory read (at most once every
   RD_KEEP_BLOCK_MEM_MS) while a block is held, asked or wanted, the block
   given back (and the ask withdrawn) under the floor; a block asked for
   when one is wanted (a copy made, none held or asked) above it.  why
   (whySize bytes, may be NULL) the memory's words of a RELEASED tick.
   h NULL: nothing */
RdKeepBlockEvent rd_keep_block_tick(RdKeepBlock *k, const RdLossKeepHost *h, double nowMs,
                                    char *why, size_t whySize);

/* the block held, now the caller's (*size its bytes); NULL when none is */
void *rd_keep_block_get(RdKeepBlock *k, size_t *size);

/* a copy made, of len bytes: into the block (intoBlock) or by the measured
   allocation; the next tick asks for a block of its size */
void rd_keep_block_copied(RdKeepBlock *k, size_t len, bool intoBlock);

/* a block from rd_keep_block_get that the frame did not fit: given back
   to the host (the copy then takes the measured allocation) */
void rd_keep_block_small(RdKeepBlock *k, const RdLossKeepHost *h, void *block);

/* the renderer's end: the block given back, the ask withdrawn, the state
   cleared (the run's counts too) */
void rd_keep_block_end(RdKeepBlock *k, const RdLossKeepHost *h);

#endif /* PORT_RENDER_RD_LOSS_KEEP_BLOCK_H */

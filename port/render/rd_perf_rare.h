/*
 * port/render/rd_perf_rare.h
 *
 * A stalled replay's list 0 named by its pipelines (issue 123).
 *
 * Issue 123's Adreno 740 hangs inside list 0's pass about every 16 s of
 * stage 12 (the kernel driver resets the GPU in about 350 ms; the clock
 * line's "the first: list 0's end", rd_perf_clock.h), and the Adreno 710 of
 * issue 211 hung three times at one place in list 0.  Which draw is running
 * at the hang is not in the log: the GPU's stamps cannot be read while it
 * runs (the single submission wait reads nothing in flight, rhi_tile_rule.h).
 * What the CPU knows is every pipeline list 0 asked for (rd_pipeline.c
 * rd__get_pipeline, through the pass ring's note, rd_passring.h
 * rd__passring_pipeline): so each replay keeps list 0's distinct pipelines
 * with their lookups (a draw or a merged run of draws each), the block (the
 * window's 10 s) counts in how many replays each pipeline was seen, and a
 * stalled replay's line lists its list 0 pipelines seen in under
 * RD_PERF_RARE_PCT % of the block's replays, then its most used ones: a
 * draw that is drawn rarely and hangs the GPU when it is shows as the rare
 * pipeline common to every stall line.  CPU-side only: nothing is asked of
 * the GPU, a few stores a lookup.  The words: "vs" the vertex entry (for a
 * VU draw the VU program's entry, its family), "fs" the fragment entry,
 * "dst" the destination read.  Vertex counts are not kept (they are the
 * draw code's, rd_replay_vu.c, not the lookup's).
 *
 * Pure C, no globals: the unit test is port/render/test/rd_perf_rare_test.c;
 * the renderer's part (the replay's table, the slots, the block) is in
 * rd_perf.c.
 */
#ifndef RD_PERF_RARE_H
#define RD_PERF_RARE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RD_PERF_RARE_KEYS 64u   /* list 0's distinct pipelines a replay keeps */
#define RD_PERF_RARE_SLOTS 128u /* its index (a power of two above KEYS) */
#define RD_PERF_RARE_BLOCK 512u /* the block's distinct pipelines counted */
#define RD_PERF_RARE_BSLOTS 1024u
#define RD_PERF_RARE_PCT 5.0   /* rare: seen in under this share of the block's replays */
#define RD_PERF_RARE_SHOW 8u   /* the rare pipelines a line lists */
#define RD_PERF_RARE_COMMON 3u /* then the most used */

typedef struct RdPerfRareKey {
    uint32_t id; /* the RHI pipeline (never 0) */
    uint16_t lookups;
    uint8_t vs, fs, dst;
} RdPerfRareKey;

typedef struct RdPerfRareReplay {
    uint32_t replay;
    uint32_t n;       /* keys held */
    uint32_t lookups; /* every lookup, the dropped included */
    uint32_t dropped; /* lookups of pipelines past RD_PERF_RARE_KEYS */
    RdPerfRareKey k[RD_PERF_RARE_KEYS];
    uint8_t at[RD_PERF_RARE_SLOTS]; /* index + 1, 0 empty */
} RdPerfRareReplay;

typedef struct RdPerfRareBlock {
    uint32_t replays; /* replays added */
    uint32_t n, dropped;
    uint32_t id[RD_PERF_RARE_BLOCK];
    uint32_t seen[RD_PERF_RARE_BLOCK]; /* the replays it was seen in */
    uint16_t at[RD_PERF_RARE_BSLOTS];  /* index + 1, 0 empty */
} RdPerfRareBlock;

/* a replay's table emptied for replay */
void rd_perf_rare_replay_reset(RdPerfRareReplay *r, uint32_t replay);
/* one lookup in list 0 */
void rd_perf_rare_replay_add(RdPerfRareReplay *r, uint32_t id, uint8_t vs, uint8_t fs, uint8_t dst);

void rd_perf_rare_block_reset(RdPerfRareBlock *b);
/* a finished replay's pipelines into the block's counts */
void rd_perf_rare_block_add(RdPerfRareBlock *b, const RdPerfRareReplay *r);
/* the replays of the block that saw pipeline id (0: none, or not counted) */
uint32_t rd_perf_rare_seen(const RdPerfRareBlock *b, uint32_t id);

/* a shader's name (NULL: numbers) */
typedef const char *(*RdPerfRareName)(uint8_t code);

/* "replay R's list 0 (stalled): ..." into buf (without "rd: "); returns
   snprintf's count */
int rd_perf_rare_text(const RdPerfRareBlock *b, const RdPerfRareReplay *r, double pct,
                      RdPerfRareName vsName, RdPerfRareName fsName, char *buf, size_t n);

/* The renderer's part (rd_perf.c): a pipeline looked up (rd_passring.c
   rd__passring_pipeline, the render thread), kept while the replay is in
   list 0; the stalled replay's line from its slot, written when the clock
   line's limiter let it (k its line number, 0 none: rd_perf_clock_due) */
void rd__perf_rare_pipeline(uint32_t id, uint8_t vs, uint8_t fs, uint8_t dst);
void rd__perf_rare_stall(uint32_t slot, uint32_t replay, uint32_t k);
/* the window's 10 s block ended (any thread): the next replay starts a new
   block's counts */
void rd_perf_rare_block_end(void);

#endif /* RD_PERF_RARE_H */

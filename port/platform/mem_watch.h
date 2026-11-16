/*
 * port/platform/mem_watch.h
 *
 * The system's memory around the start-up set (issue 215): where the
 * device's free memory goes while the set makes its pipelines
 * (mem_breakdown.h reads it).  Nothing here changes behaviour; the lines:
 *
 *   once, at the first full read: "ico_pc: memory: the system's memory split
 *       read from: /proc/meminfo, ...; not readable: ... (errno 13, EACCES)"
 *   the set's start: "ico_pc: memory before the start-up set (684
 *       pipelines): free 7249 MB of 11265 MB (MemAvailable; ...); ..." and
 *       the batch lines' rule ("every 43 pipelines and whenever free memory
 *       falls more than 334 MB below the lowest shown")
 *   a set's batch line: "; system: free 5059 MB (-2190 since the set
 *       began), file cache 2104, ..." (rd.h rd_set_memory_clause_fn) on the
 *       batches the rule picks
 *   the set's end: "ico_pc: memory at the start-up set's end (290 of 684
 *       pipelines made): ..." and the summary "ico_pc: memory over the
 *       start-up set: 290 pipelines made in 4.3 s; free memory 7249 -> 1911
 *       MB: 18.4 MB lost a pipeline; ...; the most: GPU"
 *   each onTrimMemory: "ico_pc: memory at the system's signal (level 15):
 *       ..."
 *   the 10 s block line's clause (window_lines.c): "; the system now: ..."
 *
 * The batch lines' rule (mem_watch_rule): a clause every everyPipes
 * pipelines, everyPipes = ceil(total / MEM_WATCH_EVERY_LINES), and on a
 * batch whose free memory is more than stepMb below the lowest a clause or
 * the set's start line showed, stepMb = ceil((free at the start - the
 * floor) / MEM_WATCH_STEP_LINES), at least 1 MB.  Any clause starts both
 * counts again.  So a set adds at most MEM_WATCH_EVERY_LINES clauses by
 * count and MEM_WATCH_STEP_LINES + 1 by a fall (each such clause lowers the
 * lowest shown by more than stepMb, and the set stops at the floor), plus
 * its start, rule, end and summary lines: 37 for any set, the 684 of issue
 * 215 included (and the sources' line, once a run).
 */
#ifndef ICO_PLATFORM_MEM_WATCH_H
#define ICO_PLATFORM_MEM_WATCH_H

#include <stddef.h>
#include <stdint.h>

#include "mem_breakdown.h"

/* the batch lines' clauses by count, and by a fall (plus the floor's one) */
#define MEM_WATCH_EVERY_LINES 16u
#define MEM_WATCH_STEP_LINES 16

typedef struct MemWatchRule {
    unsigned everyPipes; /* a clause every this many pipelines */
    long stepMb;         /* and on a fall of more than this below the lowest shown */
} MemWatchRule;

typedef struct MemWatchDue {
    unsigned sincePipes; /* pipelines since the last clause */
    long lowShownMb;     /* the lowest free memory shown, -1 none */
    unsigned byCount, byFall;
} MemWatchDue;

/* --- The pure part ------------------------------------------------------------ */

/* The rule for a set of total pipelines, free memory availMb at its start
   and its floor (floorMb <= 0: none) */
void mem_watch_rule(unsigned total, long availMb, long floorMb, MemWatchRule *r);
/* the rule's line, without "ico_pc: memory: " */
void mem_watch_rule_text(const MemWatchRule *r, unsigned total, long availMb, long floorMb,
                         char *out, size_t size);
/* The count starts at the set's start, its line showing startMb */
void mem_watch_due_start(MemWatchDue *d, long startMb);
/* A batch of pipes pipelines with availMb free now (-1 not read): 1 when
   its line carries the clause (the counts then start again) */
int mem_watch_due(const MemWatchRule *r, MemWatchDue *d, unsigned pipes, long availMb);

/* --- The run's watch ----------------------------------------------------------- */

/* The start-up set's count (diag_host.c ico_diag_set_pipelines): a total
   after none starts the set (its lines), none after a total ends it (its
   lines and the summary) */
void mem_watch_set_pipelines(unsigned done, unsigned total);
/* rd.h RdMemoryClauseFn: a start-up batch's clause when the rule picks it */
int mem_watch_batch_clause(uint32_t first, uint32_t last, uint32_t total, char *out, size_t size);
/* the system's onTrimMemory (diag_host.c ico_diag_note_low_memory) */
void mem_watch_trim(int level);
/* the 10 s block line's clause (a full read now) */
void mem_watch_block_clause(char *out, size_t size);
/* Tests: the lines to fn in place of the log (NULL: the log), the paths
   under root (NULL or "": the system's), the floor in MB (-2: the
   platform's own rule; tests pass one) */
void mem_watch_test_set(void (*fn)(const char *line), const char *root, long floorMb);

#endif

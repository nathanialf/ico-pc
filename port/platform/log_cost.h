/*
 * port/platform/log_cost.h
 *
 * The logging's own cost, a number in the log (the owner's question on
 * issues 123 and 179: "is it possible intense logging is the stutter?").
 *
 * How the log is written (host_config.c redirect_output):
 *   - Android: stdout and stderr are a pipe (host_android.c, the log
 *     mirror); each fprintf on an unbuffered stream is one write(2) into
 *     it, a copy into the kernel's pipe buffer (64 KiB by default); the
 *     "ico-log" thread reads the pipe and writes the file (the app's
 *     external files folder, FUSE-backed on recent Android) and logcat.  A
 *     writer (the game, render or submit thread) waits only while the pipe
 *     is full, that is while the log thread is a whole pipe behind.
 *   - Linux and macOS: unbuffered; each fprintf one write(2) to the file on
 *     the calling thread (into the page cache).
 *   - Windows: fully buffered (64 KiB), flushed once a vsync by the main
 *     loop and by the fatal paths.
 * Nothing calls fsync on the log.  On a hard kill what is lost is what the
 * pipe still holds (Android: at most its size, normally nothing, the log
 * thread drains it within a write) or the stream's buffer (Windows: up to
 * a vsync's lines).
 *
 * The 10 s block's line (window_lines.c window_log_cost_end) says what this
 * cost in the block: the window thread's time printing the block's lines
 * (wall ms, around window_host.c window_pace_log's prints), the log's bytes
 * in the block (every thread's), the log thread's file writes (Android:
 * count, ms, the longest and the line family it began with, those over
 * LOG_COST_SLOW_MS and whether they overlapped a present that blocked or a
 * late game step, the most bytes waiting in the pipe against its size),
 * and the automatic captures' (the slow-scene dump, the skin check's
 * capture: their game-thread and worker time, overlaps).  Counters only in
 * the hot path; the line once a block.
 *
 * Pure C, no globals: the unit test is port/platform/test/log_cost_test.c.
 */
#ifndef ICO_PLATFORM_LOG_COST_H
#define ICO_PLATFORM_LOG_COST_H

#include <stddef.h>

#define LOG_COST_SLOW_MS 8.0  /* a log-thread file write this long is kept for the overlaps */
#define LOG_COST_LATE_MS 10.0 /* a game step this much over its period counts as late */
#define LOG_COST_SPANS 32u    /* spans of each kind kept a block (the rest counted) */
#define LOG_COST_JOBS 8u

/* the window thread's block print in parts (window_log_cost_part): its
   records' last drain (the dev CSVs flushed in it), the kernel GPU and
   thermal reads (the JNI headroom ask), the memory reads (/proc), the
   performance lines (their fits and texts), the other lines' formatting
   and writes */
enum {
    LOG_COST_DRAIN = 0,
    LOG_COST_KERNEL,
    LOG_COST_MEMORY,
    LOG_COST_RANK,
    LOG_COST_OTHER,
    LOG_COST_PARTS
};

#define LOG_COST_OTHER_NAMES 16u

typedef struct LogCostSpan {
    double t0, t1; /* ms on the monotonic clock (ico_thread_now_ms) */
} LogCostSpan;

typedef struct LogCost {
    /* the window thread's prints of the block */
    int printValid;
    double printMs;
    double partMs[LOG_COST_PARTS]; /* of printMs (partValid: measured) */
    int partValid;

    /* the other lines' own parts, named by the window's marks
       (window_log_cost_mark), in their order; the names are static strings */
    struct {
        const char *name;
        double ms;
    } other[LOG_COST_OTHER_NAMES];

    unsigned otherN;
    /* the log thread's asks (window_stall.c): their runs in the block and
       what the last thermal ask and memory read took there */
    unsigned asks;
    double askThermalMs, askMemoryMs;
    LogCostSpan print;
    long long bytes; /* the log's bytes in the block, every thread's; -1 not known */
    /* the log thread (Android) */
    int thread;
    unsigned writes;
    double writeMs, longestMs, logcatMs;
    char longestFamily[48];
    unsigned slow; /* writes of LOG_COST_SLOW_MS or more */
    LogCostSpan slowW[LOG_COST_SPANS];
    unsigned slowN;
    unsigned pipeMaxKB, pipeCapKB; /* the most waiting in the pipe, its size (0: not known) */
    /* the automatic captures finished in the block */
    unsigned jobs;
    double jobGameMs, jobWorkerMs, jobLongestGameMs;
    LogCostSpan jobW[LOG_COST_JOBS]; /* the worker's spans */
    unsigned jobN;
    /* the block's events */
    LogCostSpan present[LOG_COST_SPANS];
    unsigned presentN, presentAll;
    LogCostSpan late[LOG_COST_SPANS];
    unsigned lateN, lateAll;
} LogCost;

void log_cost_reset(LogCost *c);
/* ms of the other lines' part named name (a static string): added to the
   same name's entry, or a new one in order (past the table: to the last) */
void log_cost_other(LogCost *c, const char *name, double ms);
/* a present that blocked over 50 ms (its call's span) */
void log_cost_present(LogCost *c, double t0, double t1);
/* a game step late by LOG_COST_LATE_MS or more (its late stretch) */
void log_cost_late(LogCost *c, double t0, double t1);
/* the log thread's file writes of the block, their slow ones one by one */
void log_cost_writes(LogCost *c, unsigned writes, double writeMs, double longestMs,
                     const char *longestFamily, double logcatMs);
void log_cost_slow_write(LogCost *c, double t0, double t1);
void log_cost_pipe(LogCost *c, unsigned maxKB, unsigned capKB);
/* a capture whose result came in the block: the game thread's ms, the
   worker's span */
void log_cost_job(LogCost *c, double gameMs, double t0, double t1);
/* how many of a's spans overlap at least one of b's */
unsigned log_cost_overlaps(const LogCostSpan *a, unsigned na, const LogCostSpan *b, unsigned nb);
/* "logging: ..." (without "window: "); returns snprintf's count */
int log_cost_text(const LogCost *c, char *buf, size_t n);

#endif /* ICO_PLATFORM_LOG_COST_H */

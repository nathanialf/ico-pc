/*
 * port/platform/mem_watch.c
 *
 * The system's memory around the start-up set (mem_watch.h, issue 215).
 */
#include "mem_watch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diag_host.h"
#include "last_run.h"
#include "memory_floor.h"

/* --- The pure part ------------------------------------------------------------ */

void mem_watch_rule(unsigned total, long availMb, long floorMb, MemWatchRule *r)
{
    const long range = availMb - (floorMb > 0 ? floorMb : 0);
    r->everyPipes = (total + MEM_WATCH_EVERY_LINES - 1u) / MEM_WATCH_EVERY_LINES;
    if (r->everyPipes == 0u) {
        r->everyPipes = 1u;
    }
    r->stepMb = range > 0 ? (range + MEM_WATCH_STEP_LINES - 1) / MEM_WATCH_STEP_LINES : 1;
    if (r->stepMb < 1) {
        r->stepMb = 1;
    }
}

void mem_watch_rule_text(const MemWatchRule *r, unsigned total, long availMb, long floorMb,
                         char *out, size_t size)
{
    char fl[48];
    if (floorMb > 0) {
        snprintf(fl, sizeof fl, " less the floor %ld MB", floorMb);
    } else {
        fl[0] = '\0';
    }
    snprintf(out, size,
             "the start-up set's batch lines carry the system's memory every %u pipelines (%u "
             "pipelines in %u lines) and whenever free memory falls more than %ld MB below the "
             "lowest shown (free %ld MB%s in %d steps): at most %u + %d lines",
             r->everyPipes, total, MEM_WATCH_EVERY_LINES, r->stepMb, availMb, fl,
             MEM_WATCH_STEP_LINES, MEM_WATCH_EVERY_LINES, MEM_WATCH_STEP_LINES + 1);
}

void mem_watch_due_start(MemWatchDue *d, long startMb)
{
    d->sincePipes = 0u;
    d->lowShownMb = startMb;
    d->byCount = d->byFall = 0u;
}

int mem_watch_due(const MemWatchRule *r, MemWatchDue *d, unsigned pipes, long availMb)
{
    const int fell = availMb >= 0 && d->lowShownMb >= 0 && d->lowShownMb - availMb > r->stepMb;
    int count;
    d->sincePipes += pipes;
    count = d->sincePipes >= r->everyPipes;
    if (!fell && !count) {
        return 0;
    }
    if (fell) {
        d->byFall++;
    } else {
        d->byCount++;
    }
    d->sincePipes = 0u;
    if (availMb >= 0 && (d->lowShownMb < 0 || availMb < d->lowShownMb)) {
        d->lowShownMb = availMb;
    }
    return 1;
}

/* --- The run's watch ----------------------------------------------------------- */

static void (*s_logFn)(const char *line);
static const char *s_root = "";
static long s_testFloor = -2;

/* the set's state: the progress thread's and the set thread's (one thread
   in the product: rd_pipeline.c calls both from its set loop) */
static int s_open;
static unsigned s_total, s_made;
static double s_t0;
static MemWatchRule s_rule;
static MemWatchDue s_due;
/* the set's start, for the 10 s line too (s_haveBase set after it) */
static MemBreakdown s_base;
static int s_haveBase;
static int s_saidSources;

static void say(const char *line)
{
    if (s_logFn != NULL) {
        s_logFn(line);
    } else {
        ico_diag_log("%s", line);
    }
}

void mem_watch_test_set(void (*fn)(const char *line), const char *root, long floorMb)
{
    s_logFn = fn;
    s_root = root != NULL ? root : "";
    s_testFloor = floorMb;
    s_open = 0;
    s_haveBase = 0;
    s_saidSources = 0;
}

/* the start-up set's floor as the set reads it (diag_host.c
   memory_floor_stop), MB; -1 none on this system */
static long floor_mb(long totalMb)
{
    if (s_testFloor != -2) {
        return s_testFloor;
    }
#ifdef __ANDROID__
    {
        long setting = ico_memory_floor_setting(getenv("ICO_PIPELINE_MEMORY_FLOOR_MB"));
        if (setting == -2) {
            setting = -1;
        }
        return totalMb > 0 ? ico_memory_floor_mb(totalMb, setting, ico_diag_memory_floor_driver())
                           : -1;
    }
#else
    (void)totalMb;
    return -1;
#endif
}

/* a full read; the sources' line once a run */
static int read_all(MemBreakdown *m)
{
    MemSources src;
    const int r = mem_breakdown_read(s_root, MEM_BD_ALL, m, &src);
    if (!__atomic_exchange_n(&s_saidSources, 1, __ATOMIC_ACQ_REL)) {
        char line[1024];
        const size_t n = (size_t)snprintf(line, sizeof line, "ico_pc: memory: ");
        mem_breakdown_sources_text(&src, line + n, sizeof line - n);
        say(line);
    }
    return r;
}

/* "ico_pc: <what>: <the whole picture>" */
static void say_full(const char *what, const MemBreakdown *m)
{
    char line[1536];
    const char *proc = "";
    size_t n;
#ifdef __ANDROID__
    proc = last_run_gpu_memory_text();
#endif
    n = (size_t)snprintf(line, sizeof line, "ico_pc: %s: ", what);
    if (n < sizeof line) {
        mem_breakdown_full_text(m, proc, line + n, sizeof line - n);
    }
    say(line);
}

void mem_watch_set_pipelines(unsigned done, unsigned total)
{
    if (!s_open && total > 0u) {
        char what[96], line[512];
        long floorMb;
        s_open = 1;
        s_total = total;
        s_made = done;
        s_t0 = ico_diag_uptime();
        (void)read_all(&s_base);
        __atomic_store_n(&s_haveBase, 1, __ATOMIC_RELEASE);
        snprintf(what, sizeof what, "memory before the start-up set (%u pipelines)", total);
        say_full(what, &s_base);
        floorMb = floor_mb(s_base.totalKb >= 0 ? s_base.totalKb / 1024 : -1);
        mem_watch_rule(total, s_base.availKb >= 0 ? s_base.availKb / 1024 : 0, floorMb, &s_rule);
        mem_watch_due_start(&s_due, s_base.availKb >= 0 ? s_base.availKb / 1024 : -1);
        {
            const size_t n = (size_t)snprintf(line, sizeof line, "ico_pc: memory: ");
            mem_watch_rule_text(&s_rule, total, s_base.availKb >= 0 ? s_base.availKb / 1024 : -1,
                                floorMb, line + n, sizeof line - n);
        }
        say(line);
        return;
    }
    if (s_open && total > 0u) {
        s_made = done > s_made ? done : s_made;
        return;
    }
    if (s_open && total == 0u) {
        MemBreakdown end;
        char what[128], line[1536];
        size_t n;
        s_open = 0;
        s_made = done > s_made ? done : s_made;
        (void)read_all(&end);
        snprintf(what, sizeof what,
                 "memory at the start-up set's end (%u of %u pipelines made; %u clauses on its "
                 "batch lines, %u by count and %u by a fall)",
                 s_made, s_total, s_due.byCount + s_due.byFall, s_due.byCount, s_due.byFall);
        say_full(what, &end);
        n = (size_t)snprintf(line, sizeof line, "ico_pc: memory over the start-up set: ");
        if (n < sizeof line) {
            mem_breakdown_summary(&s_base, &end, s_made, ico_diag_uptime() - s_t0, line + n,
                                  sizeof line - n);
        }
        say(line);
    }
}

int mem_watch_batch_clause(uint32_t first, uint32_t last, uint32_t total, char *out, size_t size)
{
    MemBreakdown m;
    const unsigned pipes = last >= first ? last - first + 1u : 1u;
    (void)total;
    if (!s_open || size == 0) {
        return 0;
    }
    /* the read decides: a fall is seen only by reading */
    (void)read_all(&m);
    if (!mem_watch_due(&s_rule, &s_due, pipes, m.availKb >= 0 ? m.availKb / 1024 : -1)) {
        return 0;
    }
    mem_breakdown_clause(&m, &s_base, out, size); /* "not read" without /proc/meminfo */
    return 1;
}

void mem_watch_trim(int level)
{
    MemBreakdown m;
    char what[64];
    (void)read_all(&m);
    snprintf(what, sizeof what, "memory at the system's signal (onTrimMemory level %d)", level);
    say_full(what, &m);
}

void mem_watch_block_clause(char *out, size_t size)
{
    MemBreakdown m;
    if (size == 0) {
        return;
    }
    out[0] = '\0';
    if (read_all(&m) != 0) {
        return;
    }
    mem_breakdown_clause(&m, __atomic_load_n(&s_haveBase, __ATOMIC_ACQUIRE) ? &s_base : NULL, out,
                         size);
}

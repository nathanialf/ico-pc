/*
 * port/platform/vmstat_read.h
 *
 * The kernel's memory events a 10 s block (issue 179: whether the stalls
 * meet memory or swap pressure; /proc/pressure/memory is not readable on
 * the reporter's phone, errno 13).  /proc/vmstat is read once a second on
 * Android's log thread (window_stall.c asks_tick), read only; whether it
 * could be read is said once:
 *   memory events: /proc/vmstat read once a second on the log thread (a
 *     block's line from the second block on)
 *   memory events: /proc/vmstat not readable (errno 13): no memory events
 *     lines
 * and each block after the first:
 *   memory events in the block (/proc/vmstat): direct reclaim stalls 0,
 *     compaction stalls 0, major page faults 123, swap-ins 0, swap-outs
 *     0, refaults 45
 * the counts since the block before's last reading: allocstall* (the
 * kernel's direct reclaim, summed over its zones), compact_stall,
 * pgmajfault, pswpin, pswpout and workingset_refault* (summed: anon and
 * file on newer kernels, one count on older).  A count the file does not
 * have is "not in the file".
 *
 * Pure: the parser and the text (port/platform/test/vmstat_read_test.c,
 * from strings).
 */
#ifndef ICO_PLATFORM_VMSTAT_READ_H
#define ICO_PLATFORM_VMSTAT_READ_H

#include <stdbool.h>
#include <stddef.h>

enum {
    VMSTAT_ALLOCSTALL = 0,
    VMSTAT_COMPACT_STALL,
    VMSTAT_PGMAJFAULT,
    VMSTAT_PSWPIN,
    VMSTAT_PSWPOUT,
    VMSTAT_REFAULT,
    VMSTAT_FIELDS
};

typedef struct VmStat {
    bool have[VMSTAT_FIELDS];
    unsigned long long v[VMSTAT_FIELDS];
} VmStat;

/* /proc/vmstat's text (len bytes, no NUL needed): true when any of the
   counts is there */
bool vmstat_read_parse(const char *buf, size_t len, VmStat *out);
/* "memory events in the block (/proc/vmstat): ..." (after "window: "),
   now's counts less before's (a count that went back: "reset") */
size_t vmstat_delta_text(const VmStat *before, const VmStat *now, char *out, size_t cap);

#endif

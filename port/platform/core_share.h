/*
 * port/platform/core_share.h
 *
 * Issue 179: the share of a thread's run time on the faster cores against
 * the slower ones, for the threads that do not sample their own core (the
 * submit thread, SDL's and the system's audio threads, the threads named
 * like the graphics driver's, the pipeline workers).  Once a second the log
 * thread reads each one's schedstat run time and the core it last ran on
 * (/proc/self/task/<tid>/stat, field 39 "processor") and adds the second's
 * run time to that core's class (render_thread.c ico_render_thread_tick);
 * the 10 s game thread line says it beside the core waits:
 *   ; run time on the faster cores 6-7: submit 4 % of 0.3 s, SDL audio 0 %
 *   of 0.4 s, system audio 0 % of 0.1 s, mali-cmar-backe 1 % of 1.2 s,
 *   pipeline workers 100 % of 2.0 s (each thread's last core once a second,
 *   weighted by its run time in that second)
 * The method's error: a second's run time all goes to the core the thread
 * last ran on, so a thread that moved between classes inside a second is
 * counted whole on one side (the error is at most the run time of the
 * seconds it moved in; a thread kept to one class by a pin has none).
 * Pure (port/platform/test/core_share_test.c).
 */
#ifndef ICO_PLATFORM_CORE_SHARE_H
#define ICO_PLATFORM_CORE_SHARE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CORE_SHARE_MAX 12 /* names (a group of threads under one name: the pipeline workers) */

typedef struct CoreShare {
    int n;

    struct {
        char name[24];
        double fastNs, slowNs;
    } t[CORE_SHARE_MAX];
} CoreShare;

void core_share_reset(CoreShare *s);
/* runNs of a thread named name (its group) on core (its last), fastMask the
   cores faster than the slowest; a core not known (< 0, past 63) or no
   fast mask: nothing added; a full table: nothing added */
void core_share_add(CoreShare *s, const char *name, int core, uint64_t runNs, uint64_t fastMask);
/* "run time on the faster cores 6-7: submit 4 % of 0.3 s, ..." (names in the
   order first added; those with no run time left out), the length written;
   "" with nothing added or no fast mask */
size_t core_share_text(const CoreShare *s, uint64_t fastMask, char *out, size_t cap);
/* /proc/<pid>/task/<tid>/stat's field 39, the core the thread last ran on
   (counted after the command's closing bracket: a name may hold spaces and
   brackets); false when not there */
bool core_share_parse_stat(const char *buf, size_t len, int *core);

#endif

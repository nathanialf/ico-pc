/*
 * port/platform/thread_stat.c
 *
 * A thread's time on a core and waiting for one (thread_stat.h).
 */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE /* syscall */
#endif
#include "thread_stat.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#if defined(__linux__)
/* the system's calls by number, as cpu_place.c (port/platform's sched.h
   hides the C library's) */
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

/* the next unsigned number at *i (blanks before it skipped); false for none */
static bool number(const char *buf, size_t len, size_t *i, uint64_t *out)
{
    uint64_t v = 0;

    while (*i < len && (buf[*i] == ' ' || buf[*i] == '\t')) {
        (*i)++;
    }
    if (*i >= len || buf[*i] < '0' || buf[*i] > '9') {
        return false;
    }
    for (; *i < len && buf[*i] >= '0' && buf[*i] <= '9'; (*i)++) {
        if (v < 1000000000000000000ull) { /* past 1e18 the digits are dropped, never overflow */
            v = v * 10u + (uint64_t)(buf[*i] - '0');
        }
    }
    *out = v;
    return true;
}

bool thread_stat_parse(const char *buf, size_t len, ThreadStat *out)
{
    size_t i = 0;
    ThreadStat s;

    if (!number(buf, len, &i, &s.runNs) || !number(buf, len, &i, &s.waitNs) ||
        !number(buf, len, &i, &s.slices)) {
        return false;
    }
    *out = s;
    return true;
}

long thread_stat_tid(void)
{
#if defined(__linux__)
    return (long)syscall(SYS_gettid);
#else
    return 0;
#endif
}

int thread_stat_read(const char *root, long tid, ThreadStat *out)
{
#if defined(__linux__)
    char path[512], buf[128];
    ssize_t got;
    int fd, e;

    if (tid <= 0) {
        return ENOSYS;
    }
    snprintf(path, sizeof(path), "%s/%ld/schedstat", root != NULL ? root : "/proc/self/task", tid);
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return errno != 0 ? errno : EIO;
    }
    got = read(fd, buf, sizeof(buf));
    e = got < 0 ? (errno != 0 ? errno : EIO) : 0;
    close(fd);
    if (e != 0) {
        return e;
    }
    return thread_stat_parse(buf, (size_t)got, out) ? 0 : EINVAL;
#else
    (void)root;
    (void)tid;
    (void)out;
    return ENOSYS;
#endif
}

static uint64_t since(uint64_t now, uint64_t last)
{
    return now >= last ? now - last : 0u;
}

bool thread_stat_track(ThreadStatTrack *t, long tid, const ThreadStat *now, ThreadStat *delta)
{
    const bool same = t->tid != 0 && t->tid == tid;

    memset(delta, 0, sizeof(*delta));
    if (same) {
        delta->runNs = since(now->runNs, t->last.runNs);
        delta->waitNs = since(now->waitNs, t->last.waitNs);
        delta->slices = since(now->slices, t->last.slices);
    }
    t->tid = tid;
    t->last = *now;
    return same;
}

size_t thread_stat_text(char *out, size_t cap, const char *const *who, const ThreadStat *delta,
                        const bool *have, int n)
{
    size_t at = 0;
    int any = 0;

    if (cap == 0) {
        return 0;
    }
    out[0] = '\0';
    for (int i = 0; i < n && at < cap; i++) {
        int w;

        if (!have[i]) {
            continue;
        }
        w = snprintf(out + at, cap - at, "%s%s %.1f s / %.1f s",
                     any ? ", " : "ran / waited for a core: ", who[i], (double)delta[i].runNs / 1e9,
                     (double)delta[i].waitNs / 1e9);
        if (w < 0) {
            break;
        }
        at += (size_t)w;
        any = 1;
    }
    return at < cap ? at : cap - 1;
}

size_t thread_stat_fail_text(char *out, size_t cap, int err)
{
    char e[24];
    int w;

    if (cap == 0) {
        return 0;
    }
    switch (err) {
    case EACCES:
        snprintf(e, sizeof(e), "EACCES");
        break;
    case EPERM:
        snprintf(e, sizeof(e), "EPERM");
        break;
    case ENOENT:
        snprintf(e, sizeof(e), "ENOENT");
        break;
    case EINVAL:
        snprintf(e, sizeof(e), "not three numbers");
        break;
    default:
        snprintf(e, sizeof(e), "errno %d", err);
        break;
    }
    w = snprintf(out, cap, "ran / waited for a core: schedstat not readable (%s)", e);
    if (w < 0) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)w < cap ? (size_t)w : cap - 1;
}

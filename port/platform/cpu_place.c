/* cpu_place.c: which core a thread runs on (cpu_place.h). */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE /* syscall */
#endif
#include "cpu_place.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__linux__)
/* the system's calls by number: port/platform's own sched.h (the game's
   scheduler) hides the C library's <sched.h> on this folder's include
   path, so sched_getcpu, sched_setaffinity and cpu_set_t are not
   reachable by name; the kernel's interface is the same on glibc and
   Android's bionic */
#include <dirent.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

int cpu_place_now(void)
{
#if defined(__linux__)
    unsigned cpu = 0;

    return syscall(SYS_getcpu, &cpu, NULL, NULL) == 0 ? (int)cpu : -1;
#elif defined(_WIN32)
    return (int)GetCurrentProcessorNumber();
#else
    return -1;
#endif
}

void cpu_place_note(CpuPlaceHist *h, int cpu)
{
    if (cpu >= 0 && cpu < CPU_PLACE_MAX) {
        __atomic_fetch_add(&h->n[cpu], 1u, __ATOMIC_RELAXED);
    } else {
        __atomic_fetch_add(&h->unknown, 1u, __ATOMIC_RELAXED);
    }
}

void cpu_place_take(CpuPlaceHist *h, CpuPlaceHist *out)
{
    for (int i = 0; i < CPU_PLACE_MAX; i++) {
        out->n[i] = __atomic_exchange_n(&h->n[i], 0u, __ATOMIC_RELAXED);
    }
    out->unknown = __atomic_exchange_n(&h->unknown, 0u, __ATOMIC_RELAXED);
}

int cpu_place_most(const CpuPlaceHist *h, uint32_t *count)
{
    int best = -1;
    uint32_t n = 0;

    for (int i = 0; i < CPU_PLACE_MAX; i++) {
        if (h->n[i] > n) {
            n = h->n[i];
            best = i;
        }
    }
    if (count != NULL) {
        *count = n;
    }
    return best;
}

uint32_t cpu_place_total(const CpuPlaceHist *h)
{
    uint32_t t = h->unknown;

    for (int i = 0; i < CPU_PLACE_MAX; i++) {
        t += h->n[i];
    }
    return t;
}

uint32_t cpu_place_in(const CpuPlaceHist *h, uint64_t mask)
{
    uint32_t t = 0;

    for (int i = 0; i < CPU_PLACE_MAX; i++) {
        if (mask & ((uint64_t)1 << i)) {
            t += h->n[i];
        }
    }
    return t;
}

int cpu_place_classes_read(const char *root, CpuClasses *c)
{
    memset(c, 0, sizeof(*c));
#if defined(__linux__)
    if (root == NULL) {
        root = "/sys/devices/system/cpu";
    }
    for (int i = 0; i < CPU_PLACE_MAX; i++) {
        char path[512];
        FILE *f;
        unsigned long khz = 0;

        snprintf(path, sizeof(path), "%s/cpu%d/cpufreq/cpuinfo_max_freq", root, i);
        f = fopen(path, "r");
        if (f == NULL) {
            continue; /* an offline core, or past the last */
        }
        if (fscanf(f, "%lu", &khz) == 1 && khz > 0 && khz <= 0xffffffffu) {
            c->khz[i] = (uint32_t)khz;
            c->count = i + 1;
        }
        fclose(f);
    }
#else
    (void)root;
#endif
    return c->count;
}

/* append to out at *len (the text cut at cap, *len the length it would
   have) */
#if defined(__GNUC__)
static void put(char *out, size_t cap, size_t *len, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));
#endif
static void put(char *out, size_t cap, size_t *len, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(*len < cap ? out + *len : NULL, *len < cap ? cap - *len : 0, fmt, ap);
    va_end(ap);
    if (n > 0) {
        *len += (size_t)n;
    }
}

static size_t done(char *out, size_t cap, size_t len)
{
    if (cap > 0 && len >= cap) {
        out[cap - 1] = '\0';
        return cap - 1;
    }
    return len;
}

size_t cpu_place_classes_text(const CpuClasses *c, char *out, size_t cap)
{
    size_t len = 0;
    bool any = false;

    if (cap > 0) {
        out[0] = '\0';
    }
    for (int i = 0; i < c->count;) {
        int j = i;

        while (j + 1 < c->count && c->khz[j + 1] == c->khz[i]) {
            j++;
        }
        put(out, cap, &len, "%s", any ? ", " : "");
        if (j > i) {
            put(out, cap, &len, "%d-%d", i, j);
        } else {
            put(out, cap, &len, "%d", i);
        }
        if (c->khz[i] != 0) {
            put(out, cap, &len, " %.1f GHz", (double)c->khz[i] / 1e6);
        } else {
            put(out, cap, &len, " speed not known");
        }
        any = true;
        i = j + 1;
    }
    if (!any) {
        put(out, cap, &len, "not known");
    }
    return done(out, cap, len);
}

/* the slowest known speed; 0 when no core's is known or all are one */
static uint32_t slowest(const CpuClasses *c)
{
    uint32_t lo = 0, hi = 0;

    for (int i = 0; i < c->count; i++) {
        const uint32_t k = c->khz[i];

        if (k != 0) {
            lo = lo == 0 || k < lo ? k : lo;
            hi = k > hi ? k : hi;
        }
    }
    return lo != hi ? lo : 0;
}

uint64_t cpu_place_slow_mask(const CpuClasses *c)
{
    const uint32_t lo = slowest(c);
    uint64_t m = 0;

    for (int i = 0; lo != 0 && i < c->count; i++) {
        if (c->khz[i] == lo) {
            m |= (uint64_t)1 << i;
        }
    }
    return m;
}

uint64_t cpu_place_fast_mask(const CpuClasses *c)
{
    const uint32_t lo = slowest(c);
    uint64_t m = 0;

    for (int i = 0; lo != 0 && i < c->count; i++) {
        if (c->khz[i] > lo) {
            m |= (uint64_t)1 << i;
        }
    }
    return m;
}

bool cpu_place_split(const CpuClasses *c, uint64_t *render, uint64_t *submit)
{
    const uint64_t fast = cpu_place_fast_mask(c);
    uint32_t hi = 0;
    int top = -1, tops = 0;

    *render = *submit = fast;
    for (int i = 0; i < c->count && i < CPU_PLACE_MAX; i++) {
        hi = c->khz[i] > hi ? c->khz[i] : hi;
    }
    for (int i = 0; hi != 0 && i < c->count && i < CPU_PLACE_MAX; i++) {
        if (c->khz[i] == hi) {
            top = i;
            tops++;
        }
    }
    if (fast == 0 || tops != 1 || (fast & ~((uint64_t)1 << top)) == 0) {
        return false;
    }
    *render = (uint64_t)1 << top;
    *submit = fast & ~*render;
    return true;
}

void cpu_place_plan(const CpuClasses *c, CpuPlan *out)
{
    const uint64_t fast = cpu_place_fast_mask(c);
    int n = 0, known = 0;

    memset(out, 0, sizeof(*out));
    for (int i = 0; i < c->count && i < CPU_PLACE_MAX; i++) {
        n += (int)((fast >> i) & 1u);
        known += c->khz[i] != 0;
    }
    out->fastCount = n;
    out->coreCount = known;
    if (n == 0) {
        out->kind = CPU_PLAN_NONE;
    } else if (n == 1) {
        out->kind = CPU_PLAN_ONE_FAST;
        out->render = fast;
    } else if (n == 2) {
        out->kind = CPU_PLAN_TWO_FAST;
        out->game = out->render = fast;
    } else {
        out->kind = CPU_PLAN_MANY_FAST;
        out->split = cpu_place_split(c, &out->render, &out->submit);
    }
}

size_t cpu_place_mask_text(uint64_t mask, char *out, size_t cap)
{
    size_t len = 0;
    bool any = false;

    if (cap > 0) {
        out[0] = '\0';
    }
    for (int i = 0; i < CPU_PLACE_MAX;) {
        if (!(mask & ((uint64_t)1 << i))) {
            i++;
            continue;
        }
        int j = i;

        while (j + 1 < CPU_PLACE_MAX && (mask & ((uint64_t)1 << (j + 1)))) {
            j++;
        }
        put(out, cap, &len, "%s", any ? ", " : "");
        if (j > i) {
            put(out, cap, &len, "%d-%d", i, j);
        } else {
            put(out, cap, &len, "%d", i);
        }
        any = true;
        i = j + 1;
    }
    if (!any) {
        put(out, cap, &len, "none");
    }
    return done(out, cap, len);
}

static void one(char *out, size_t cap, size_t *len, const char *who, const CpuPlaceHist *h)
{
    const uint32_t total = cpu_place_total(h);
    uint32_t n = 0;
    const int core = cpu_place_most(h, &n);

    if (total == 0) {
        put(out, cap, len, "%s:-", who);
    } else if (core < 0) {
        put(out, cap, len, "%s:not known", who);
    } else {
        put(out, cap, len, "%s:%d (%u %%)", who, core,
            (unsigned)((n * 100ull + total / 2) / total));
    }
}

static void share(char *out, size_t cap, size_t *len, const char *who, const CpuPlaceHist *h,
                  uint64_t slow)
{
    const uint32_t total = cpu_place_total(h);

    if (total == 0) {
        put(out, cap, len, " %s -", who);
    } else {
        const uint32_t in = cpu_place_in(h, slow);

        put(out, cap, len, " %s %u %%", who, (unsigned)((in * 100ull + total / 2) / total));
    }
}

size_t cpu_place_part(char *out, size_t cap, const CpuPlaceHist *g, const CpuPlaceHist *r,
                      const CpuPlaceHist *s, uint64_t slow)
{
    size_t len = 0;

    if (cap > 0) {
        out[0] = '\0';
    }
    put(out, cap, &len, "on cores ");
    one(out, cap, &len, "G", g);
    put(out, cap, &len, " ");
    one(out, cap, &len, "R", r);
    if (s != NULL) {
        put(out, cap, &len, " ");
        one(out, cap, &len, "S", s);
    }
    if (slow != 0) {
        char m[96];

        (void)cpu_place_mask_text(slow, m, sizeof(m));
        put(out, cap, &len, ", on the slowest cores (%s)", m);
        share(out, cap, &len, "G", g, slow);
        share(out, cap, &len, "R", r, slow);
        if (s != NULL) {
            share(out, cap, &len, "S", s, slow);
        }
    }
    return done(out, cap, len);
}

bool cpu_place_pin(uint64_t mask, char *why, size_t cap)
{
    return cpu_place_pin_tid(0, mask, why, cap);
}

bool cpu_place_pin_tid(long tid, uint64_t mask, char *why, size_t cap)
{
    if (cap > 0) {
        why[0] = '\0';
    }
    if (mask == 0) {
        snprintf(why, cap, "no cores to keep it to");
        return false;
    }
#if defined(__linux__)
    {
        /* the kernel's mask: CPU_PLACE_MAX bits, little end first */
        unsigned long set[CPU_PLACE_MAX / (8 * sizeof(unsigned long))];

        memset(set, 0, sizeof(set));
        for (int i = 0; i < CPU_PLACE_MAX; i++) {
            if (mask & ((uint64_t)1 << i)) {
                set[i / (8 * sizeof(unsigned long))] |= 1ul << (i % (8 * sizeof(unsigned long)));
            }
        }
        if (syscall(SYS_sched_setaffinity, (pid_t)tid, sizeof(set), set) != 0) {
            const int e = errno;

            snprintf(why, cap, "errno %d: %s", e, strerror(e));
            return false;
        }
        return true;
    }
#else
    (void)tid;
    snprintf(why, cap, "not on this system");
    return false;
#endif
}

uint64_t cpu_place_allowed(void)
{
    return cpu_place_allowed_tid(0);
}

uint64_t cpu_place_allowed_tid(long tid)
{
#if defined(__linux__)
    /* the kernel writes as many bytes as its own mask has (refusing a
       buffer smaller than that): room for 1024 cores, the first
       CPU_PLACE_MAX read */
    unsigned long set[1024 / (8 * sizeof(unsigned long))];
    uint64_t m = 0;

    memset(set, 0, sizeof(set));
    if (syscall(SYS_sched_getaffinity, (pid_t)tid, sizeof(set), set) <= 0) {
        return 0;
    }
    for (int i = 0; i < CPU_PLACE_MAX; i++) {
        if (set[i / (8 * sizeof(unsigned long))] & (1ul << (i % (8 * sizeof(unsigned long))))) {
            m |= (uint64_t)1 << i;
        }
    }
    return m;
#else
    (void)tid;
    return 0;
#endif
}

static void shares_one(char *out, size_t cap, size_t *len, const char *who, const CpuPlaceHist *h)
{
    const uint32_t total = cpu_place_total(h);

    if (total == 0) {
        put(out, cap, len, "%s -", who);
        return;
    }
    put(out, cap, len, "%s", who);
    for (int i = 0; i < CPU_PLACE_MAX; i++) {
        if (h->n[i] != 0) {
            put(out, cap, len, " %d:%u", i, (unsigned)((h->n[i] * 100ull + total / 2) / total));
        }
    }
    if (h->unknown != 0) {
        put(out, cap, len, " ?:%u", (unsigned)((h->unknown * 100ull + total / 2) / total));
    }
    put(out, cap, len, " %%");
}

size_t cpu_place_shares_text(char *out, size_t cap, const CpuPlaceHist *g, const CpuPlaceHist *r,
                             const CpuPlaceHist *s)
{
    size_t len = 0;

    if (cap > 0) {
        out[0] = '\0';
    }
    put(out, cap, &len, "the samples by core: ");
    shares_one(out, cap, &len, "G", g);
    put(out, cap, &len, ", ");
    shares_one(out, cap, &len, "R", r);
    if (s != NULL) {
        put(out, cap, &len, ", ");
        shares_one(out, cap, &len, "S", s);
    }
    return done(out, cap, len);
}

CpuPinResult cpu_place_pin_result(bool called, uint64_t asked, uint64_t readBack)
{
    if (!called) {
        return CPU_PIN_REFUSED;
    }
    if (readBack == 0) {
        return CPU_PIN_UNKNOWN;
    }
    return readBack == asked ? CPU_PIN_KEPT : CPU_PIN_OTHER;
}

CpuPinResult cpu_place_pin_checked(uint64_t mask, uint64_t *got, char *why, size_t cap)
{
    const bool called = cpu_place_pin(mask, why, cap);

    *got = called ? cpu_place_allowed() : 0;
    return cpu_place_pin_result(called, mask, *got);
}

size_t cpu_place_priority_text(char *out, size_t cap)
{
    size_t len = 0;

    if (cap > 0) {
        out[0] = '\0';
    }
#if defined(__linux__)
    {
        const long tid = syscall(SYS_gettid);
        int nice;
        int policy;

        errno = 0;
        nice = getpriority(PRIO_PROCESS, (id_t)tid);
        if (errno == 0) {
            put(out, cap, &len, "nice %d", nice);
        }
        policy = (int)syscall(SYS_sched_getscheduler, 0);
        if (policy == 1 || policy == 2) { /* SCHED_FIFO, SCHED_RR */
            put(out, cap, &len, "%sreal-time", len ? ", " : "");
        }
    }
#endif
    return done(out, cap, len);
}

/* the process's cores kept before the first pin (cpu_place_keep_free);
   the resets and the failures since the start */
static uint64_t s_freeMask;
static unsigned s_freeResets, s_freeFailures;

/* the threads that put themselves on the kept cores (cpu_place_thread_free
   with a mask kept: Android's rule on), by id, for cpu_place_refree: a slot
   is claimed by a compare-and-swap on 0, cleared by the refree when its
   thread has ended */
static long s_freeTids[CPU_FREE_TIDS];

static void free_register(void)
{
#if defined(__linux__)
    const long tid = syscall(SYS_gettid);

    for (int i = 0; i < CPU_FREE_TIDS; i++) {
        if (__atomic_load_n(&s_freeTids[i], __ATOMIC_ACQUIRE) == tid) {
            return; /* already known (a second call at the thread's start) */
        }
    }
    for (int i = 0; i < CPU_FREE_TIDS; i++) {
        long zero = 0;

        if (__atomic_compare_exchange_n(&s_freeTids[i], &zero, tid, false, __ATOMIC_ACQ_REL,
                                        __ATOMIC_ACQUIRE)) {
            return;
        }
    }
#endif
}

unsigned cpu_place_refree(uint64_t mask, const long *skip, int nSkip, unsigned *ended)
{
    unsigned moved = 0, gone = 0;

    for (int i = 0; mask != 0 && i < CPU_FREE_TIDS; i++) {
        const long tid = __atomic_load_n(&s_freeTids[i], __ATOMIC_ACQUIRE);
        bool skipIt = tid <= 0;
        char why[96];

        for (int k = 0; !skipIt && k < nSkip; k++) {
            skipIt = skip[k] == tid;
        }
        if (skipIt) {
            continue;
        }
        if (cpu_place_allowed_tid(tid) == mask) {
            continue;
        }
        if (cpu_place_pin_tid(tid, mask, why, sizeof(why))) {
            moved++;
        } else if (cpu_place_allowed_tid(tid) == 0) {
            /* the thread has ended (ESRCH): its slot free again */
            long was = tid;

            (void)__atomic_compare_exchange_n(&s_freeTids[i], &was, 0L, false, __ATOMIC_ACQ_REL,
                                              __ATOMIC_ACQUIRE);
            gone++;
        }
    }
    if (ended != NULL) {
        *ended = gone;
    }
    return moved;
}

int cpu_place_sweep_pick(const long *ids, const uint64_t *masks, int n, uint64_t held,
                         const long *skip, int nSkip, long *out, int cap)
{
    int m = 0;

    for (int i = 0; i < n && m < cap; i++) {
        bool skipIt = ids[i] <= 0 || masks[i] == 0 || (masks[i] & held) == 0;

        for (int k = 0; !skipIt && k < nSkip; k++) {
            skipIt = skip[k] == ids[i];
        }
        if (!skipIt) {
            out[m++] = ids[i];
        }
    }
    return m;
}

static int by_long(const void *a, const void *b)
{
    const long x = *(const long *)a, y = *(const long *)b;

    return x < y ? -1 : x > y;
}

int cpu_place_task_ids(const char *root, long *ids, int cap)
{
    int n = 0;
#if defined(__linux__)
    DIR *d = opendir(root != NULL ? root : "/proc/self/task");
    struct dirent *e;

    if (d == NULL) {
        return 0;
    }
    while (n < cap && (e = readdir(d)) != NULL) {
        char *end;
        const long tid = strtol(e->d_name, &end, 10);

        if (e->d_name[0] >= '0' && e->d_name[0] <= '9' && *end == '\0' && tid > 0) {
            ids[n++] = tid;
        }
    }
    closedir(d);
    if (n > 1) {
        qsort(ids, (size_t)n, sizeof(ids[0]), by_long);
    }
#else
    (void)root;
    (void)ids;
    (void)cap;
#endif
    return n;
}

unsigned cpu_place_sweep(uint64_t held, uint64_t to, const long *skip, int nSkip, unsigned *seen,
                         unsigned *failed)
{
    /* the log thread's alone: off its stack */
    static long s_ids[CPU_SWEEP_MAX], s_pick[CPU_SWEEP_MAX];
    static uint64_t s_masks[CPU_SWEEP_MAX];
    unsigned moved = 0, bad = 0;
    int n = 0, m;

    if (held != 0 && to != 0 && (held & ~to) != 0) {
        n = cpu_place_task_ids(NULL, s_ids, CPU_SWEEP_MAX);
    }
    for (int i = 0; i < n; i++) {
        s_masks[i] = cpu_place_allowed_tid(s_ids[i]);
    }
    m = cpu_place_sweep_pick(s_ids, s_masks, n, held, skip, nSkip, s_pick, CPU_SWEEP_MAX);
    for (int i = 0; i < m; i++) {
        char why[96];

        if (cpu_place_pin_tid(s_pick[i], to, why, sizeof(why))) {
            moved++;
        } else if (cpu_place_allowed_tid(s_pick[i]) != 0) {
            bad++; /* still there: refused (an ended thread reads 0) */
        }
    }
    if (seen != NULL) {
        *seen = (unsigned)n;
    }
    if (failed != NULL) {
        *failed = bad;
    }
    return moved;
}

void cpu_place_keep_free(uint64_t mask)
{
    __atomic_store_n(&s_freeMask, mask, __ATOMIC_RELEASE);
}

uint64_t cpu_place_free_mask(void)
{
    return __atomic_load_n(&s_freeMask, __ATOMIC_ACQUIRE);
}

CpuFreeResult cpu_place_thread_free(void)
{
    const uint64_t keep = cpu_place_free_mask();
    uint64_t now;
    uint64_t got = 0;
    char why[128];
    CpuPinResult res;

    if (keep == 0) {
        return CPU_FREE_NONE;
    }
    free_register();
    now = cpu_place_allowed();
    if (now == 0) {
        return CPU_FREE_NONE; /* not Linux, or nothing read */
    }
    if (now == keep) {
        return CPU_FREE_ALREADY;
    }
    res = cpu_place_pin_checked(keep, &got, why, sizeof(why));
    if (res == CPU_PIN_KEPT || res == CPU_PIN_UNKNOWN) {
        (void)__atomic_fetch_add(&s_freeResets, 1u, __ATOMIC_RELAXED);
        return CPU_FREE_RESET;
    }
    {
        const unsigned n = __atomic_add_fetch(&s_freeFailures, 1u, __ATOMIC_RELAXED);

        if (n <= 8 || n % 100 == 0) {
            char want[96], had[96], gotText[96];

            (void)cpu_place_mask_text(keep, want, sizeof(want));
            (void)cpu_place_mask_text(now, had, sizeof(had));
            (void)cpu_place_mask_text(got, gotText, sizeof(gotText));
            fprintf(stderr,
                    "cpu: a worker thread could not be put back on cores %s (it had %s from the "
                    "thread that made it; %s%s) [%u so far]\n",
                    want, had, res == CPU_PIN_OTHER ? "the system keeps it to cores " : "",
                    res == CPU_PIN_OTHER ? gotText : why, n);
        }
    }
    return CPU_FREE_FAILED;
}

void cpu_place_free_counts(unsigned *resets, unsigned *failures)
{
    *resets = __atomic_load_n(&s_freeResets, __ATOMIC_RELAXED);
    *failures = __atomic_load_n(&s_freeFailures, __ATOMIC_RELAXED);
}

uint64_t cpu_place_thread_cpu_ns(void)
{
#if defined(_WIN32)
    FILETIME created, exited, kernel, user;

    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) {
        return 0;
    }
    /* 100 ns units */
    return ((((uint64_t)kernel.dwHighDateTime << 32) | kernel.dwLowDateTime) +
            (((uint64_t)user.dwHighDateTime << 32) | user.dwLowDateTime)) *
           100u;
#elif defined(CLOCK_THREAD_CPUTIME_ID)
    struct timespec ts;

    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
#else
    return 0;
#endif
}

size_t cpu_place_cpu_text(double cpuMs, char *out, size_t cap)
{
    size_t len = 0;

    if (cap > 0) {
        out[0] = '\0';
    }
    if (cpuMs > 0.0) {
        put(out, cap, &len, "CPU %.1f ms of it", cpuMs);
    }
    return done(out, cap, len);
}

uint64_t cpu_place_list_parse(const char *text)
{
    uint64_t m = 0;
    const char *p = text;
    bool any = false;

    if (p == NULL) {
        return 0;
    }
    for (;;) {
        char *end;
        long a, b;

        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p < '0' || *p > '9') {
            break;
        }
        a = strtol(p, &end, 10);
        p = end;
        b = a;
        if (*p == '-') {
            p++;
            if (*p < '0' || *p > '9') {
                return 0;
            }
            b = strtol(p, &end, 10);
            p = end;
        }
        if (b < a) {
            return 0;
        }
        for (long i = a; i <= b && i < CPU_PLACE_MAX; i++) {
            m |= (uint64_t)1 << i;
        }
        any = true;
        if (*p != ',') {
            break;
        }
        p++;
    }
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        p++;
    }
    return any && *p == '\0' ? m : 0;
}

#if defined(__linux__)
/* a small file's text (open, read, close: procfs makes it at the read),
   NUL-ended, its trailing newline kept; false when not read */
static bool read_small(const char *path, char *buf, size_t cap)
{
    FILE *f = fopen(path, "r");
    size_t got;

    if (f == NULL) {
        return false;
    }
    got = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[got] = '\0';
    return got > 0;
}

int cpu_place_cpuset_read(const char *taskRoot, const char *cpusetRoot, long tid, char *path,
                          size_t cap, uint64_t *cpus)
{
    char file[512], buf[256];
    FILE *f;
    size_t got;

    *cpus = 0;
    if (cap > 0) {
        path[0] = '\0';
    }
    snprintf(file, sizeof(file), "%s/%ld/cpuset", taskRoot != NULL ? taskRoot : "/proc/self/task",
             tid);
    f = fopen(file, "r");
    if (f == NULL) {
        return errno != 0 ? errno : ENOENT;
    }
    got = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[got] = '\0';
    buf[strcspn(buf, "\r\n")] = '\0';
    if (buf[0] != '/') {
        return EINVAL;
    }
    snprintf(path, cap, "%.*s", (int)(cap > 0 ? cap - 1 : 0), buf);
    for (int k = 0; k < 2 && *cpus == 0; k++) {
        /* "/" is the root set: <root>/cpus */
        snprintf(file, sizeof(file), "%s%s/%s", cpusetRoot != NULL ? cpusetRoot : "/dev/cpuset",
                 strcmp(buf, "/") == 0 ? "" : buf, k == 0 ? "cpus" : "effective_cpus");
        if (read_small(file, buf + 128, 128)) {
            *cpus = cpu_place_list_parse(buf + 128);
        }
    }
    return 0;
}

bool cpu_place_uclamp_read(long tid, unsigned *utilMin, unsigned *utilMax)
{
#if defined(SYS_sched_getattr)
    /* the kernel's struct sched_attr (include/uapi/linux/sched/types.h):
       size, policy, flags, nice, priority, runtime, deadline, period, then
       util_min and util_max (the 56-byte version 1) */
    struct {
        uint32_t size, policy;
        uint64_t flags;
        int32_t nice;
        uint32_t priority;
        uint64_t runtime, deadline, period;
        uint32_t utilMin, utilMax;
    } a;

    memset(&a, 0, sizeof(a));
    if (syscall(SYS_sched_getattr, (pid_t)tid, &a, (unsigned)sizeof(a), 0u) != 0 ||
        a.size < sizeof(a)) {
        return false;
    }
    *utilMin = a.utilMin;
    *utilMax = a.utilMax;
    return true;
#else
    (void)tid;
    *utilMin = *utilMax = 0;
    return false;
#endif
}

static int by_tid(const void *a, const void *b)
{
    const long x = ((const CpuThread *)a)->tid, y = ((const CpuThread *)b)->tid;

    return x < y ? -1 : x > y;
}
#else
int cpu_place_cpuset_read(const char *taskRoot, const char *cpusetRoot, long tid, char *path,
                          size_t cap, uint64_t *cpus)
{
    (void)taskRoot;
    (void)cpusetRoot;
    (void)tid;
    *cpus = 0;
    if (cap > 0) {
        path[0] = '\0';
    }
    return ENOSYS;
}

bool cpu_place_uclamp_read(long tid, unsigned *utilMin, unsigned *utilMax)
{
    (void)tid;
    *utilMin = *utilMax = 0;
    return false;
}
#endif

int cpu_place_threads_read(const char *root, CpuThreads *out)
{
    out->n = out->total = 0;
#if defined(__linux__)
    DIR *d;
    struct dirent *e;

    if (root == NULL) {
        root = "/proc/self/task";
    }
    d = opendir(root);
    if (d == NULL) {
        return 0;
    }
    while ((e = readdir(d)) != NULL) {
        char path[512], buf[4096];
        char *end;
        const long tid = strtol(e->d_name, &end, 10);
        CpuThread *t;

        if (e->d_name[0] < '0' || e->d_name[0] > '9' || *end != '\0') {
            continue; /* "." and ".." */
        }
        out->total++;
        if (out->n >= CPU_THREADS_MAX) {
            continue;
        }
        t = &out->t[out->n++];
        memset(t, 0, sizeof(*t));
        t->tid = tid;
        snprintf(path, sizeof(path), "%s/%s/comm", root, e->d_name);
        if (read_small(path, buf, sizeof(t->name))) {
            buf[strcspn(buf, "\n")] = '\0';
            /* the precision bounds the copy where gcc's -O2 truncation check sees it */
            snprintf(t->name, sizeof(t->name), "%.*s", (int)(sizeof(t->name) - 1), buf);
        }
        snprintf(path, sizeof(path), "%s/%s/status", root, e->d_name);
        if (read_small(path, buf, sizeof(buf))) {
            const char *at = strstr(buf, "Cpus_allowed_list:");

            if (at != NULL) {
                char list[256];
                size_t k = 0;

                at += strlen("Cpus_allowed_list:");
                while (*at == ' ' || *at == '\t') {
                    at++;
                }
                while (at[k] != '\0' && at[k] != '\n' && k + 1 < sizeof(list)) {
                    list[k] = at[k];
                    k++;
                }
                list[k] = '\0';
                t->mask = cpu_place_list_parse(list);
            }
        }
    }
    closedir(d);
    if (out->n > 1) {
        qsort(out->t, (size_t)out->n, sizeof(out->t[0]), by_tid);
    }
#else
    (void)root;
#endif
    return out->n;
}

static int popcount64(uint64_t m)
{
    int n = 0;

    for (; m != 0; m &= m - 1) {
        n++;
    }
    return n;
}

/* one group's names: the game thread first, then by thread id (the
   table's order), each name once with its count; at most
   CPU_THREADS_NAMES, then "and N more" */
static void group_names(const CpuThreads *t, uint64_t mask, long gameTid, char *out, size_t cap,
                        size_t *len)
{
    int order[CPU_THREADS_MAX], n = 0, said = 0, shown = 0;
    bool used[CPU_THREADS_MAX];

    for (int i = 0; i < t->n; i++) {
        if (t->t[i].mask == mask && t->t[i].tid == gameTid && gameTid != 0) {
            order[n++] = i;
        }
    }
    for (int i = 0; i < t->n; i++) {
        if (t->t[i].mask == mask && !(t->t[i].tid == gameTid && gameTid != 0)) {
            order[n++] = i;
        }
    }
    memset(used, 0, sizeof(used));
    for (int k = 0; k < n && said < CPU_THREADS_NAMES; k++) {
        const CpuThread *a = &t->t[order[k]];
        const char *name = a->name[0] != '\0' ? a->name : "?";
        const bool game = gameTid != 0 && a->tid == gameTid;
        int same = 1;

        if (used[k]) {
            continue;
        }
        used[k] = true;
        for (int j = k + 1; j < n && !game; j++) {
            const CpuThread *b = &t->t[order[j]];

            if (!used[j] && strcmp(b->name[0] != '\0' ? b->name : "?", name) == 0) {
                used[j] = true;
                same++;
            }
        }
        put(out, cap, len, "%s%s", said ? ", " : "", name);
        if (game) {
            put(out, cap, len, " (the game thread)");
        } else if (same > 1) {
            put(out, cap, len, " (%d)", same);
        }
        said++;
        shown += same;
    }
    if (shown < n) {
        put(out, cap, len, " and %d more", n - shown);
    }
}

size_t cpu_place_threads_format(const CpuThreads *t, long gameTid, char *out, size_t cap)
{
    uint64_t masks[CPU_THREADS_MAX], full = 0;
    int nm = 0;
    bool unknown = false;
    size_t len = 0;

    if (cap > 0) {
        out[0] = '\0';
    }
    if (t->n == 0) {
        put(out, cap, &len, "threads: none read");
        return done(out, cap, len);
    }
    for (int i = 0; i < t->n; i++) {
        const uint64_t m = t->t[i].mask;
        bool seen = false;

        full |= m;
        if (m == 0) {
            unknown = true;
            continue;
        }
        for (int k = 0; k < nm; k++) {
            seen = seen || masks[k] == m;
        }
        if (!seen) {
            masks[nm++] = m;
        }
    }
    /* the narrowest first (fewest cores, then the lowest), the widest last */
    for (int i = 1; i < nm; i++) {
        for (int k = i; k > 0; k--) {
            const int pa = popcount64(masks[k - 1]), pb = popcount64(masks[k]);

            if (pa > pb || (pa == pb && masks[k - 1] > masks[k])) {
                const uint64_t x = masks[k - 1];

                masks[k - 1] = masks[k];
                masks[k] = x;
            }
        }
    }
    put(out, cap, &len, "threads: %d", t->total);
    for (int k = 0; k < nm; k++) {
        char m[96];

        (void)cpu_place_mask_text(masks[k], m, sizeof(m));
        if (masks[k] == full) {
            put(out, cap, &len, "; on %s: ", m);
        } else {
            put(out, cap, &len, "; kept to core%s %s: ", popcount64(masks[k]) == 1 ? "" : "s", m);
        }
        group_names(t, masks[k], gameTid, out, cap, &len);
    }
    if (unknown) {
        put(out, cap, &len, "; cores not known: ");
        group_names(t, 0, gameTid, out, cap, &len);
    }
    if (t->total > t->n) {
        put(out, cap, &len, "; %d more not looked at", t->total - t->n);
    }
    return done(out, cap, len);
}

size_t cpu_place_threads_text(const char *root, long gameTid, char *out, size_t cap)
{
    static CpuThreads s_t; /* 5 KB: off the caller's stack (the window's thread) */

    (void)cpu_place_threads_read(root, &s_t);
    return cpu_place_threads_format(&s_t, gameTid, out, cap);
}

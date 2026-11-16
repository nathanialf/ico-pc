/*
 * port/platform/mem_breakdown.c
 *
 * Where the device's memory is (mem_breakdown.h, issue 215).
 */
#include "mem_breakdown.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "diag_host.h" /* ico_diag_uptime */

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#define MB_PRINTF(a, b) __attribute__((format(printf, a, b)))
#else
#define MB_PRINTF(a, b)
#endif

/* text appended at *at, cut at the buffer's end */
MB_PRINTF(4, 5) static void put(char *out, size_t size, size_t *at, const char *fmt, ...)
{
    va_list ap;
    int k;
    if (size == 0 || *at >= size - 1) {
        return;
    }
    va_start(ap, fmt);
    k = vsnprintf(out + *at, size - *at, fmt, ap);
    va_end(ap);
    if (k > 0) {
        *at = *at + (size_t)k < size ? *at + (size_t)k : size - 1;
    }
}

static long mb(long kb)
{
    return kb >= 0 ? kb / 1024 : -1;
}

void mem_breakdown_clear(MemBreakdown *m)
{
    m->totalKb = m->freeKb = m->availKb = m->buffersKb = m->cachedKb = m->swapCachedKb = m->anonKb =
        m->shmemKb = m->slabKb = m->kstackKb = m->ptablesKb = m->swapTotalKb = m->swapFreeKb = -1;
    m->rssKb = m->hwmKb = m->rssAnonKb = m->rssFileKb = m->rssShmemKb = m->vmSwapKb = -1;
    m->gpuKb = m->dmaKb = m->zramKb = -1;
    m->sysAt = -1.0;
}

/* --- Parsing ------------------------------------------------------------------ */

/* the value of the line "<key> <digits> ..." among the lines held whole
   (ended by a newline), -1 none */
static long field_kb(const char *buf, size_t n, const char *key)
{
    const size_t k = strlen(key);
    size_t i = 0;
    while (i < n) {
        size_t e = i;
        while (e < n && buf[e] != '\n') {
            e++;
        }
        if (e == n) {
            return -1; /* a line the read cut: not read */
        }
        if (e - i > k && memcmp(buf + i, key, k) == 0) {
            size_t j = i + k;
            long v = 0;
            while (j < e && (buf[j] == ' ' || buf[j] == '\t')) {
                j++;
            }
            if (j == e || buf[j] < '0' || buf[j] > '9') {
                return -1;
            }
            while (j < e && buf[j] >= '0' && buf[j] <= '9') {
                if (v > (0x7fffffffL - 9) / 10) {
                    return -1; /* past 2 TB: not a figure of this */
                }
                v = v * 10 + (buf[j] - '0');
                j++;
            }
            return v;
        }
        i = e + 1;
    }
    return -1;
}

int mem_breakdown_parse_meminfo(const char *buf, size_t n, MemBreakdown *m)
{
    static const char *const keys[13] = {
        "MemTotal:",   "MemFree:",   "MemAvailable:", "Buffers:", "Cached:",
        "SwapCached:", "AnonPages:", "Shmem:",        "Slab:",    "KernelStack:",
        "PageTables:", "SwapTotal:", "SwapFree:",
    };
    long *const at[13] = {
        &m->totalKb,      &m->freeKb,      &m->availKb,    &m->buffersKb, &m->cachedKb,
        &m->swapCachedKb, &m->anonKb,      &m->shmemKb,    &m->slabKb,    &m->kstackKb,
        &m->ptablesKb,    &m->swapTotalKb, &m->swapFreeKb,
    };
    int got = 0;
    for (int i = 0; i < 13; i++) {
        *at[i] = field_kb(buf, n, keys[i]);
        got += *at[i] >= 0;
    }
    return got;
}

int mem_breakdown_parse_status(const char *buf, size_t n, MemBreakdown *m)
{
    static const char *const keys[6] = {
        "VmRSS:", "VmHWM:", "RssAnon:", "RssFile:", "RssShmem:", "VmSwap:"};
    long *const at[6] = {&m->rssKb,     &m->hwmKb,      &m->rssAnonKb,
                         &m->rssFileKb, &m->rssShmemKb, &m->vmSwapKb};
    int got = 0;
    for (int i = 0; i < 6; i++) {
        *at[i] = field_kb(buf, n, keys[i]);
        got += *at[i] >= 0;
    }
    return got;
}

int mem_breakdown_parse_node(const char *buf, size_t n, int field, long long *out)
{
    size_t i = 0;
    for (int f = 0;; f++) {
        long long v = 0;
        size_t s;
        while (i < n && (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\n')) {
            i++;
        }
        s = i;
        while (i < n && buf[i] >= '0' && buf[i] <= '9') {
            if (v > (0x7fffffffffffffffLL - 9) / 10) {
                return EINVAL;
            }
            v = v * 10 + (buf[i] - '0');
            i++;
        }
        if (i == s || (i < n && buf[i] != ' ' && buf[i] != '\t' && buf[i] != '\n')) {
            return EINVAL; /* no digits, or a word */
        }
        if (f == field) {
            *out = v;
            return 0;
        }
        if (i >= n) {
            return EINVAL;
        }
    }
}

/* --- The parts ------------------------------------------------------------------ */

static long kb0(long v)
{
    return v > 0 ? v : 0;
}

int mem_breakdown_cats(const MemBreakdown *m, long cat[MEM_CAT_N])
{
    long used, known = 0;
    for (int i = 0; i < MEM_CAT_N; i++) {
        cat[i] = 0;
    }
    if (m->totalKb < 0 || m->freeKb < 0) {
        return 0;
    }
    used = m->totalKb - m->freeKb;
    /* Cached holds Shmem: the file cache without it */
    cat[MEM_CAT_FILE] = kb0(m->buffersKb) + kb0(m->cachedKb) - kb0(m->shmemKb);
    cat[MEM_CAT_SHARED] = kb0(m->shmemKb);
    cat[MEM_CAT_ANON] = kb0(m->anonKb);
    cat[MEM_CAT_SWAPCACHE] = kb0(m->swapCachedKb);
    cat[MEM_CAT_KERNEL] = kb0(m->slabKb) + kb0(m->kstackKb) + kb0(m->ptablesKb);
    cat[MEM_CAT_GPU] = kb0(m->gpuKb);
    cat[MEM_CAT_DMA] = kb0(m->dmaKb);
    cat[MEM_CAT_ZRAM] = kb0(m->zramKb);
    for (int i = 0; i < MEM_CAT_REST; i++) {
        known += cat[i];
    }
    cat[MEM_CAT_REST] = used - known;
    return 1;
}

const char *mem_breakdown_cat_name(int cat)
{
    static const char *const names[MEM_CAT_N] = {
        "file cache", "shared",  "anonymous", "swap cache", "kernel",
        "GPU",        "dma-buf", "zram",      "the rest",
    };
    return cat >= 0 && cat < MEM_CAT_N ? names[cat] : "";
}

/* whether a part is read (the GPU, dma-buf and zram parts may not be) */
static int cat_read(const MemBreakdown *m, int cat)
{
    switch (cat) {
    case MEM_CAT_GPU:
        return m->gpuKb >= 0;
    case MEM_CAT_DMA:
        return m->dmaKb >= 0;
    case MEM_CAT_ZRAM:
        return m->zramKb >= 0;
    default:
        return 1;
    }
}

/* --- The texts ------------------------------------------------------------------ */

#if !defined(_WIN32) && !defined(__APPLE__)
/* the sources' paths (under the read's root) */
static const char *const s_srcName[MEM_SRC_N] = {
    "/proc/meminfo",
    "/proc/self/status",
    "/sys/class/kgsl/kgsl/page_alloc",
    "/sys/class/kgsl/kgsl/vmalloc",
    "/sys/class/kgsl/kgsl/coherent",
    "/sys/class/kgsl/kgsl/secure",
    "/sys/kernel/dma_heap/total_pools_kb",
    "/sys/kernel/ion/total_heaps_kb",
    "/sys/block/zram0/mm_stat",
};

static const char *err_word(int e)
{
    return e == ENOENT   ? ", not present"
           : e == EACCES ? ", EACCES"
           : e == EINVAL ? ", no figure in it"
                         : "";
}
#endif

void mem_breakdown_sources_text(const MemSources *s, char *out, size_t size)
{
    size_t at = 0;
    int nr = 0, nn = 0;
    if (size == 0) {
        return;
    }
    out[0] = '\0';
#if defined(_WIN32) || defined(__APPLE__)
    (void)s;
    (void)nr;
    (void)nn;
    put(out, size, &at, "%s",
        "the system's memory split not available on this system (Linux and Android only)");
#else
    put(out, size, &at, "the system's memory split read from:");
    for (int i = 0; i < MEM_SRC_N; i++) {
        if (s->err[i] == 0) {
            put(out, size, &at, "%s %s", nr++ ? "," : "", s_srcName[i]);
        }
    }
    if (nr == 0) {
        put(out, size, &at, " none");
    }
    put(out, size, &at, "; not readable:");
    for (int i = 0; i < MEM_SRC_N; i++) {
        if (s->err[i] > 0) {
            put(out, size, &at, "%s %s (errno %d%s)", nn++ ? "," : "", s_srcName[i], s->err[i],
                err_word(s->err[i]));
        }
    }
    if (nn == 0) {
        put(out, size, &at, " none");
    }
    put(out, size, &at,
        "; a part not readable is in the rest (the memory in use in none of the parts read)");
#endif
}

void mem_breakdown_full_text(const MemBreakdown *m, const char *gpuProc, char *out, size_t size)
{
    long cat[MEM_CAT_N];
    size_t at = 0;
    if (size == 0) {
        return;
    }
    out[0] = '\0';
    if (!mem_breakdown_cats(m, cat)) {
        put(out, size, &at, "the system's memory not read (no /proc/meminfo)");
        return;
    }
    put(out, size, &at,
        "free %ld MB of %ld MB (MemAvailable; MemFree %ld MB); in use %ld MB:", mb(m->availKb),
        mb(m->totalKb), mb(m->freeKb), mb(m->totalKb - m->freeKb));
    for (int i = 0; i < MEM_CAT_N; i++) {
        const char *how = i == MEM_CAT_ANON     ? " (every process's)"
                          : i == MEM_CAT_KERNEL ? " (slab, stacks, page tables)"
                          : i == MEM_CAT_GPU    ? " (the kernel GPU driver in all)"
                          : i == MEM_CAT_DMA    ? " (the heaps' pools)"
                          : i == MEM_CAT_REST   ? " (in none of these)"
                                                : "";
        if (cat_read(m, i)) {
            put(out, size, &at, "%s %s %ld MB%s", i ? "," : "", mem_breakdown_cat_name(i),
                cat[i] / 1024, how);
        } else {
            put(out, size, &at, "%s %s not readable (in the rest)", i ? "," : "",
                mem_breakdown_cat_name(i));
        }
    }
    if (m->swapTotalKb > 0 && m->swapFreeKb >= 0) {
        put(out, size, &at, "; swap used %ld of %ld MB", mb(m->swapTotalKb - m->swapFreeKb),
            mb(m->swapTotalKb));
    } else if (m->swapTotalKb == 0) {
        put(out, size, &at, "; no swap");
    }
    if (m->rssKb >= 0) {
        put(out, size, &at,
            "; this process %ld MB resident (anonymous %ld, file-backed %ld, "
            "shared %ld MB), %ld MB swapped out",
            mb(m->rssKb), mb(m->rssAnonKb), mb(m->rssFileKb), mb(m->rssShmemKb), mb(m->vmSwapKb));
    }
    if (gpuProc != NULL && gpuProc[0] != '\0') {
        put(out, size, &at, "; this process's GPU memory: %s", gpuProc);
    }
}

void mem_breakdown_clause(const MemBreakdown *m, const MemBreakdown *base, char *out, size_t size)
{
    long cat[MEM_CAT_N];
    size_t at = 0;
    if (size == 0) {
        return;
    }
    out[0] = '\0';
    if (!mem_breakdown_cats(m, cat)) {
        put(out, size, &at, "not read");
        return;
    }
    put(out, size, &at, "free %ld MB", mb(m->availKb));
    if (base != NULL && base->availKb >= 0 && m->availKb >= 0) {
        put(out, size, &at, " (%+ld since the set began)", mb(m->availKb) - mb(base->availKb));
    }
    for (int i = 0; i < MEM_CAT_N; i++) {
        if (i == MEM_CAT_SWAPCACHE ||
            ((i == MEM_CAT_DMA || i == MEM_CAT_ZRAM) && !cat_read(m, i))) {
            continue; /* small, or not on this system */
        }
        if (cat_read(m, i)) {
            put(out, size, &at, ", %s %ld", mem_breakdown_cat_name(i), cat[i] / 1024);
        } else {
            put(out, size, &at, ", %s ?", mem_breakdown_cat_name(i));
        }
    }
    put(out, size, &at, " MB");
    if (m->swapTotalKb > 0 && m->swapFreeKb >= 0) {
        put(out, size, &at, ", swap used %ld MB", mb(m->swapTotalKb - m->swapFreeKb));
    }
}

void mem_breakdown_last_run_text(const MemBreakdown *m, char *out, size_t size)
{
    long cat[MEM_CAT_N];
    size_t at = 0;
    if (size == 0) {
        return;
    }
    out[0] = '\0';
    if (!mem_breakdown_cats(m, cat)) {
        return;
    }
    put(out, size, &at, "file cache %ld, anon %ld, kernel %ld, GPU ", cat[MEM_CAT_FILE] / 1024,
        cat[MEM_CAT_ANON] / 1024, cat[MEM_CAT_KERNEL] / 1024);
    if (m->gpuKb >= 0) {
        put(out, size, &at, "%ld", cat[MEM_CAT_GPU] / 1024);
    } else {
        put(out, size, &at, "?");
    }
    put(out, size, &at, ", rest %ld", cat[MEM_CAT_REST] / 1024);
    if (m->swapTotalKb > 0 && m->swapFreeKb >= 0) {
        put(out, size, &at, ", swap used %ld", mb(m->swapTotalKb - m->swapFreeKb));
    }
    put(out, size, &at, " MB");
}

void mem_breakdown_summary(const MemBreakdown *a, const MemBreakdown *b, unsigned made,
                           double seconds, char *out, size_t size)
{
    long ca[MEM_CAT_N], cb[MEM_CAT_N];
    size_t at = 0;
    int most = -1;
    double per = made > 0 ? 1.0 / (double)made : 1.0;
    const char *unit = made > 0 ? " a pipeline" : " in all";
    if (size == 0) {
        return;
    }
    out[0] = '\0';
    put(out, size, &at, "%u pipelines made in %.1f s", made, seconds);
    if (!mem_breakdown_cats(a, ca) || !mem_breakdown_cats(b, cb) || a->availKb < 0 ||
        b->availKb < 0) {
        put(out, size, &at, "; the system's memory not read at both ends");
        return;
    }
    put(out, size, &at, "; free memory %ld -> %ld MB: %.1f MB lost%s", mb(a->availKb),
        mb(b->availKb), (double)(a->availKb - b->availKb) / 1024.0 * per, unit);
    put(out, size, &at, "; the memory in use %+ld MB, what grew%s (they sum to it):",
        mb(b->totalKb - b->freeKb) - mb(a->totalKb - a->freeKb), unit);
    for (int i = 0; i < MEM_CAT_N; i++) {
        const long d = cb[i] - ca[i];
        if (!cat_read(a, i) || !cat_read(b, i)) {
            put(out, size, &at, "%s %s not read at both ends (in the rest)", i ? "," : "",
                mem_breakdown_cat_name(i));
            continue;
        }
        put(out, size, &at, "%s %s %+.1f", i ? "," : "", mem_breakdown_cat_name(i),
            (double)d / 1024.0 * per);
        if (d > 0 && (most < 0 || d > cb[most] - ca[most])) {
            most = i;
        }
    }
    put(out, size, &at, " MB");
    if (a->swapTotalKb > 0 && a->swapFreeKb >= 0 && b->swapTotalKb > 0 && b->swapFreeKb >= 0) {
        put(out, size, &at, "; swapped out %+.1f MB%s",
            (double)((b->swapTotalKb - b->swapFreeKb) - (a->swapTotalKb - a->swapFreeKb)) / 1024.0 *
                per,
            unit);
    }
    if (a->rssKb >= 0 && b->rssKb >= 0) {
        put(out, size, &at, "; this process %+.1f MB%s (resident)",
            (double)(b->rssKb - a->rssKb) / 1024.0 * per, unit);
    }
    if (most >= 0) {
        put(out, size, &at, "; the most: %s", mem_breakdown_cat_name(most));
    } else {
        put(out, size, &at, "; nothing grew");
    }
}

/* --- The reader -------------------------------------------------------------- */

#if !defined(_WIN32) && !defined(__APPLE__)
/* the last /sys figures any read made (a /proc-only read takes them) */
static long s_lastGpu = -1, s_lastDma = -1, s_lastZram = -1;
static long long s_lastSysMs = -1;

/* a file's text into buf (to its end or cap - 1): bytes read, or -errno */
static long read_file(const char *root, const char *path, char *buf, size_t cap)
{
    char full[256];
    long got = 0;
    int fd;
    if ((size_t)snprintf(full, sizeof full, "%s%s", root, path) >= sizeof full) {
        return -ENAMETOOLONG;
    }
    fd = open(full, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return errno != 0 ? -errno : -EIO;
    }
    while ((size_t)got < cap - 1) {
        const ssize_t r = read(fd, buf + got, cap - 1 - (size_t)got);
        if (r < 0) {
            const int e = errno != 0 ? errno : EIO;
            if (got == 0) {
                close(fd);
                return -e;
            }
            break; /* the lines read so far are kept */
        }
        if (r == 0) {
            break;
        }
        got += (long)r;
    }
    close(fd);
    buf[got] = '\0';
    return got;
}

/* a node's figure (field), *err its state */
static long long read_node(const char *root, int src, int field, int *err)
{
    char buf[128];
    long long v = -1;
    const long n = read_file(root, s_srcName[src], buf, sizeof buf);
    if (n < 0) {
        *err = (int)-n;
        return -1;
    }
    *err = mem_breakdown_parse_node(buf, (size_t)n, field, &v);
    return *err == 0 ? v : -1;
}
#endif

int mem_breakdown_read(const char *root, unsigned what, MemBreakdown *out, MemSources *src)
{
    MemSources local;
    MemSources *s = src != NULL ? src : &local;
    mem_breakdown_clear(out);
    for (int i = 0; i < MEM_SRC_N; i++) {
        s->err[i] = -1;
    }
#if defined(_WIN32) || defined(__APPLE__)
    (void)root;
    (void)what;
    return -1;
#else
    {
        /* /proc/self/status reaches VmSwap past 1 KB on Android (its
           Groups line alone can be long; window_stall.c) */
        char buf[4096];
        long n;
        if (root == NULL) {
            root = "";
        }
        if (what & MEM_BD_PROC) {
            n = read_file(root, s_srcName[MEM_SRC_MEMINFO], buf, sizeof buf);
            if (n >= 0) {
                s->err[MEM_SRC_MEMINFO] =
                    mem_breakdown_parse_meminfo(buf, (size_t)n, out) > 0 ? 0 : EINVAL;
            } else {
                s->err[MEM_SRC_MEMINFO] = (int)-n;
            }
            n = read_file(root, s_srcName[MEM_SRC_STATUS], buf, sizeof buf);
            if (n >= 0) {
                s->err[MEM_SRC_STATUS] =
                    mem_breakdown_parse_status(buf, (size_t)n, out) > 0 ? 0 : EINVAL;
            } else {
                s->err[MEM_SRC_STATUS] = (int)-n;
            }
        }
        if (what & MEM_BD_SYS) {
            long long gpu = -1, v;
            for (int i = MEM_SRC_KGSL_PAGE; i <= MEM_SRC_KGSL_SECURE; i++) {
                v = read_node(root, i, 0, &s->err[i]);
                if (v >= 0) {
                    gpu = (gpu < 0 ? 0 : gpu) + v / 1024; /* bytes */
                }
            }
            out->gpuKb = (long)gpu;
            out->dmaKb = -1;
            v = read_node(root, MEM_SRC_DMA_HEAP, 0, &s->err[MEM_SRC_DMA_HEAP]);
            if (v >= 0) {
                out->dmaKb = (long)v; /* KB */
            }
            v = read_node(root, MEM_SRC_ION, 0, &s->err[MEM_SRC_ION]);
            if (v >= 0) {
                out->dmaKb = (out->dmaKb < 0 ? 0 : out->dmaKb) + (long)v; /* KB */
            }
            v = read_node(root, MEM_SRC_ZRAM, 2, &s->err[MEM_SRC_ZRAM]);
            out->zramKb = v >= 0 ? (long)(v / 1024) : -1; /* bytes */
            out->sysAt = ico_diag_uptime();
            __atomic_store_n(&s_lastGpu, out->gpuKb, __ATOMIC_RELAXED);
            __atomic_store_n(&s_lastDma, out->dmaKb, __ATOMIC_RELAXED);
            __atomic_store_n(&s_lastZram, out->zramKb, __ATOMIC_RELAXED);
            __atomic_store_n(&s_lastSysMs, (long long)(out->sysAt * 1000.0), __ATOMIC_RELEASE);
        } else {
            const long long ms = __atomic_load_n(&s_lastSysMs, __ATOMIC_ACQUIRE);
            if (ms >= 0) {
                out->gpuKb = __atomic_load_n(&s_lastGpu, __ATOMIC_RELAXED);
                out->dmaKb = __atomic_load_n(&s_lastDma, __ATOMIC_RELAXED);
                out->zramKb = __atomic_load_n(&s_lastZram, __ATOMIC_RELAXED);
                out->sysAt = (double)ms / 1000.0;
            }
        }
        return s->err[MEM_SRC_MEMINFO] == 0 ? 0 : -1;
    }
#endif
}

/*
 * port/platform/mem_breakdown.h
 *
 * Where the device's memory is (issue 215: a Retroid Pocket 6 whose free
 * memory fell from 7249 to 1911 MB while the start-up set made 290
 * pipelines and the process grew 168 -> 260 MB; the same fall, only in the
 * VU batches, on the issue 123 tablet).  MemAvailable alone cannot say
 * what holds the memory; this reads the system's own split of it.
 *
 * One read of each file a call (open, read to its end or the buffer's,
 * close; no allocation), on Linux and Android ("not available" elsewhere):
 *
 *   /proc/meminfo        MemTotal, MemFree, MemAvailable, Buffers, Cached,
 *                        SwapCached, AnonPages, Shmem, Slab, KernelStack,
 *                        PageTables, SwapTotal, SwapFree (KB)
 *   /proc/self/status    VmRSS, VmHWM, RssAnon, RssFile, RssShmem, VmSwap
 *   the kernel GPU       /sys/class/kgsl/kgsl/page_alloc, vmalloc, coherent
 *   driver (KGSL)        and secure, summed (taken for bytes: not verified
 *                        on a device), Qualcomm's kernels only
 *   dma-buf              /sys/kernel/dma_heap/total_pools_kb (the heaps'
 *                        pools, GKI kernels) and /sys/kernel/ion/
 *                        total_heaps_kb (ION, older kernels)
 *   zram                 /sys/block/zram0/mm_stat's third field (the
 *                        memory the compressed swap holds, bytes)
 *
 * A short read keeps only the lines it holds whole (a line cut before its
 * newline is not read); a file that cannot be opened or read keeps its
 * errno for the sources' line.  The /sys nodes are read only when asked
 * (MEM_BD_SYS): the heartbeat (the watchdog's thread) reads /proc alone and
 * takes the last /sys figures another read made.
 *
 * What holds the memory (mem_breakdown_cats), from one read: the memory in
 * use (MemTotal - MemFree) split into the file cache (Buffers + Cached -
 * Shmem), shared (Shmem), anonymous (AnonPages: every process's), the swap
 * cache (SwapCached), the kernel (Slab + KernelStack + PageTables), the GPU
 * (KGSL's totals), the dma-buf heaps, zram, and the rest: what is in use
 * but in none of these (on a phone with KGSL not readable, the GPU driver's
 * memory is in it).  The rest is the remainder, so the parts always sum to
 * the memory in use; an overlap of two sources shows as a rest below zero.
 */
#ifndef ICO_PLATFORM_MEM_BREAKDOWN_H
#define ICO_PLATFORM_MEM_BREAKDOWN_H

#include <stddef.h>

/* what a read takes */
#define MEM_BD_PROC 1u /* /proc/meminfo and /proc/self/status */
#define MEM_BD_SYS 2u  /* the /sys nodes (KGSL, dma-buf, zram) */
#define MEM_BD_ALL (MEM_BD_PROC | MEM_BD_SYS)

/* the sources, in the sources' line's order */
enum {
    MEM_SRC_MEMINFO,
    MEM_SRC_STATUS,
    MEM_SRC_KGSL_PAGE,
    MEM_SRC_KGSL_VMALLOC,
    MEM_SRC_KGSL_COHERENT,
    MEM_SRC_KGSL_SECURE,
    MEM_SRC_DMA_HEAP,
    MEM_SRC_ION,
    MEM_SRC_ZRAM,
    MEM_SRC_N
};

/* the parts of the memory in use (mem_breakdown_cats), in the lines' order */
enum {
    MEM_CAT_FILE,
    MEM_CAT_SHARED,
    MEM_CAT_ANON,
    MEM_CAT_SWAPCACHE,
    MEM_CAT_KERNEL,
    MEM_CAT_GPU,
    MEM_CAT_DMA,
    MEM_CAT_ZRAM,
    MEM_CAT_REST,
    MEM_CAT_N
};

/* One read.  KB; -1 for a figure not read. */
typedef struct MemBreakdown {
    long totalKb, freeKb, availKb, buffersKb, cachedKb, swapCachedKb, anonKb, shmemKb, slabKb,
        kstackKb, ptablesKb, swapTotalKb, swapFreeKb;
    long rssKb, hwmKb, rssAnonKb, rssFileKb, rssShmemKb, vmSwapKb;
    long gpuKb;   /* KGSL's readable totals summed, -1 none readable */
    long dmaKb;   /* the dma-buf heaps' pools plus ION's heaps, -1 neither */
    long zramKb;  /* zram's memory, -1 not readable */
    double sysAt; /* seconds since the start of the /sys figures' read, -1 none */
} MemBreakdown;

/* Each source's state at a read: 0 read, else the errno of its open or
   read (EINVAL: read but no figure in it), -1 not asked */
typedef struct MemSources {
    int err[MEM_SRC_N];
} MemSources;

/* --- The pure part (tested on every platform) ------------------------------ */

/* every figure -1 */
void mem_breakdown_clear(MemBreakdown *m);
/* /proc/meminfo's and /proc/self/status's text (n bytes, not NUL-ended):
   the fields of lines held whole; returns the fields read */
int mem_breakdown_parse_meminfo(const char *buf, size_t n, MemBreakdown *m);
int mem_breakdown_parse_status(const char *buf, size_t n, MemBreakdown *m);
/* a node's first unsigned integer (field 0) or its field-th (blanks
   between): 0 and *out, or EINVAL */
int mem_breakdown_parse_node(const char *buf, size_t n, int field, long long *out);
/* The parts of the memory in use (KB, MEM_CAT_*); the GPU, dma-buf and
   zram parts 0 where not read (in the rest then).  Returns 0 when MemTotal
   or MemFree was not read (nothing split). */
int mem_breakdown_cats(const MemBreakdown *m, long cat[MEM_CAT_N]);
/* the parts' names in the lines */
const char *mem_breakdown_cat_name(int cat);
/* The sources' line's text, without "ico_pc: memory: " */
void mem_breakdown_sources_text(const MemSources *s, char *out, size_t size);
/* The whole picture, one line: free memory, every part, swap, this
   process; gpuProc: this process's GPU memory as the sampler words it
   (NULL or "": left out) */
void mem_breakdown_full_text(const MemBreakdown *m, const char *gpuProc, char *out, size_t size);
/* The compact clause for the batch lines and the 10 s line: "free 5059 MB
   (-2190 since the set began), file cache 2104, ..., the rest 980 MB, swap
   used 1200 MB"; base: the set's start (NULL: no change said) */
void mem_breakdown_clause(const MemBreakdown *m, const MemBreakdown *base, char *out, size_t size);
/* last-run.txt's part of its free memory field: "file cache 2104, anon
   3012, kernel 640, GPU 1204, rest 980, swap used 1200 MB" ("" when
   MemTotal or MemFree was not read) */
void mem_breakdown_last_run_text(const MemBreakdown *m, char *out, size_t size);
/* The summary at the set's end: free memory lost a pipeline made and what
   grew, a pipeline, part by part, from a (the set's start) to b (its
   end); made pipelines over seconds */
void mem_breakdown_summary(const MemBreakdown *a, const MemBreakdown *b, unsigned made,
                           double seconds, char *out, size_t size);

/* --- The reader -------------------------------------------------------------- */

/* One read of what `what` asks (MEM_BD_*), each path under root ("" the
   system's own; the tests' fake tree otherwise).  src (NULL: not kept)
   each source's state.  A /proc-only read takes the last /sys figures any
   read made (their time in sysAt).  Returns 0 when /proc/meminfo was read,
   else -1 (Windows, macOS and iOS: always -1, nothing read). */
int mem_breakdown_read(const char *root, unsigned what, MemBreakdown *out, MemSources *src);

#endif

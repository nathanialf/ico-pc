/*
 * port/platform/test/mem_breakdown_test.c
 *
 * Where the device's memory is (mem_breakdown.h, mem_watch.h, issue 215):
 *   parse    /proc/meminfo's and /proc/self/status's fields ("Cached:" not
 *            "SwapCached:"), a field missing, a read cut inside a line (that
 *            line not read), a figure past its room, the /sys nodes' figures
 *            (a field of zram's mm_stat, a word, nothing);
 *   parts    the memory in use split into its parts, the rest the remainder
 *            (below zero where two sources overlap), the texts word for
 *            word (the whole picture, the batch lines' clause, last-run.txt's
 *            part, a part not read);
 *   summary  the set's summary: free memory lost a pipeline and what grew,
 *            part by part, the most; no pipeline made; one end not read;
 *   rule     the batch lines' every-Nth and fell-by-step rules for the
 *            684-pipeline set of issue 215: 43 pipelines and 334 MB, 15
 *            clauses in batches of 4 and 14 in batches of 8 with no fall, a
 *            fall of 335 MB taken and one of 334 not, and the bound (16 by
 *            count, 17 by a fall) under a steady and an uneven fall;
 *   reader   (Linux and Android) a fake /proc and /sys tree under the build
 *            folder: every source read, a node that is a folder (EISDIR) and
 *            nodes not present named in the sources' line, the KGSL totals
 *            summed, a /proc-only read taking the last /sys figures, a tree
 *            without /proc/meminfo;
 *   watch    (Linux and Android) the set's lines through the tree: the
 *            sources once, the start's picture and rule, a clause by count
 *            and one by a fall, the end's picture and the summary, the
 *            system's signal, the 10 s clause;
 *   lastrun  last-run.txt's free memory field with the split, through its
 *            text and parse (the free figure, the lowest and the floor still
 *            read).
 * argv[1]: a folder the test may write in (the build folder).
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_fs.h"
#include "last_run.h"
#include "mem_breakdown.h"
#include "mem_watch.h"

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static int text_is(const char *got, const char *want)
{
    if (strcmp(got, want) != 0) {
        printf("got:  %s\nwant: %s\n", got, want);
        return 0;
    }
    return 1;
}

/* the set's start and end as the tree's files hold them (the issue 215
   console's 11265 MB, its 7249 and 1911 MB free) */
static const char kMeminfoA[] = "MemTotal:       11535360 kB\n"
                                "MemFree:          831488 kB\n"
                                "MemAvailable:    7422976 kB\n"
                                "Buffers:           10240 kB\n"
                                "Cached:          6553600 kB\n"
                                "SwapCached:         3072 kB\n"
                                "Active:          4000000 kB\n"
                                "AnonPages:       2097152 kB\n"
                                "Shmem:             46080 kB\n"
                                "Slab:             409600 kB\n"
                                "KernelStack:       61440 kB\n"
                                "PageTables:        81920 kB\n"
                                "SwapTotal:       6291456 kB\n"
                                "SwapFree:        5062656 kB\n";
static const char kMeminfoB[] = "MemTotal:       11535360 kB\n"
                                "MemFree:          307200 kB\n"
                                "MemAvailable:    1956864 kB\n"
                                "Buffers:           10240 kB\n"
                                "Cached:          1638400 kB\n"
                                "SwapCached:         3072 kB\n"
                                "AnonPages:       2150400 kB\n"
                                "Shmem:             46080 kB\n"
                                "Slab:             419840 kB\n"
                                "KernelStack:       62464 kB\n"
                                "PageTables:        83968 kB\n"
                                "SwapTotal:       6291456 kB\n"
                                "SwapFree:        4956160 kB\n";
static const char kStatusA[] = "Name:\tico-pc\nVmHWM:\t  172032 kB\nVmRSS:\t  172032 kB\n"
                               "RssAnon:\t   81920 kB\nRssFile:\t   88064 kB\n"
                               "RssShmem:\t    2048 kB\nVmSwap:\t       0 kB\n";
static const char kStatusB[] = "Name:\tico-pc\nVmHWM:\t  266240 kB\nVmRSS:\t  266240 kB\n"
                               "RssAnon:\t  172032 kB\nRssFile:\t   90112 kB\n"
                               "RssShmem:\t    2048 kB\nVmSwap:\t       0 kB\n";

/* the start's and the end's reads as the reader makes them (the KGSL
   totals page_alloc + vmalloc: 1024 + 10 and 5120 + 10 MB; the dma-buf
   pools 50 MB; zram 520 and 620 MB) */
static void fill(MemBreakdown *m, int end)
{
    mem_breakdown_clear(m);
    (void)mem_breakdown_parse_meminfo(end ? kMeminfoB : kMeminfoA,
                                      end ? sizeof kMeminfoB - 1 : sizeof kMeminfoA - 1, m);
    (void)mem_breakdown_parse_status(end ? kStatusB : kStatusA,
                                     end ? sizeof kStatusB - 1 : sizeof kStatusA - 1, m);
    m->gpuKb = end ? 5242880 + 10240 : 1048576 + 10240;
    m->dmaKb = 51200;
    m->zramKb = end ? 634880 : 532480;
}

static const char kFullA[] =
    "free 7249 MB of 11265 MB (MemAvailable; MemFree 812 MB); in use 10453 MB: file cache 6365 "
    "MB, shared 45 MB, anonymous 2048 MB (every process's), swap cache 3 MB, kernel 540 MB (slab, "
    "stacks, page tables), GPU 1034 MB (the kernel GPU driver in all), dma-buf 50 MB (the heaps' "
    "pools), zram 520 MB, the rest -152 MB (in none of these); swap used 1200 of 6144 MB; this "
    "process 168 MB resident (anonymous 80, file-backed 86, shared 2 MB), 0 MB swapped out";
static const char kFullB[] =
    "free 1911 MB of 11265 MB (MemAvailable; MemFree 300 MB); in use 10965 MB: file cache 1565 "
    "MB, shared 45 MB, anonymous 2100 MB (every process's), swap cache 3 MB, kernel 553 MB (slab, "
    "stacks, page tables), GPU 5130 MB (the kernel GPU driver in all), dma-buf 50 MB (the heaps' "
    "pools), zram 620 MB, the rest 899 MB (in none of these); swap used 1304 of 6144 MB; this "
    "process 260 MB resident (anonymous 168, file-backed 88, shared 2 MB), 0 MB swapped out";
static const char kClauseA[] =
    "free 7249 MB (+0 since the set began), file cache 6365, shared 45, anonymous 2048, kernel "
    "540, GPU 1034, dma-buf 50, zram 520, the rest -152 MB, swap used 1200 MB";
static const char kClauseB[] =
    "free 1911 MB (-5338 since the set began), file cache 1565, shared 45, anonymous 2100, kernel "
    "553, GPU 5130, dma-buf 50, zram 620, the rest 899 MB, swap used 1304 MB";
static const char kSummaryTail[] =
    " s; free memory 7249 -> 1911 MB: 18.4 MB lost a pipeline; the memory in use +512 MB, what "
    "grew a pipeline (they sum to it): file cache -16.6, shared +0.0, anonymous +0.2, swap cache "
    "+0.0, kernel +0.0, GPU +14.1, dma-buf +0.0, zram +0.3, the rest +3.6 MB; swapped out +0.4 MB "
    "a pipeline; this process +0.3 MB a pipeline (resident); the most: GPU";

static void parse(void)
{
    MemBreakdown m;
    long long v = -1;
    static const char cut[] = "MemTotal:       11535360 kB\nMemFree:          831488 kB\n"
                              "MemAvailable:    74";
    static const char odd[] = "SwapCached:         3072 kB\nCached:           100 kB\n"
                              "AnonPages:  99999999999999 kB\nShmem: none\n";

    mem_breakdown_clear(&m);
    CHECK(mem_breakdown_parse_meminfo(kMeminfoA, sizeof kMeminfoA - 1, &m) == 13);
    CHECK(m.totalKb == 11535360 && m.freeKb == 831488 && m.availKb == 7422976 &&
          m.buffersKb == 10240 && m.cachedKb == 6553600 && m.swapCachedKb == 3072 &&
          m.anonKb == 2097152 && m.shmemKb == 46080 && m.slabKb == 409600 && m.kstackKb == 61440 &&
          m.ptablesKb == 81920 && m.swapTotalKb == 6291456 && m.swapFreeKb == 5062656);
    /* a read cut inside MemAvailable's line: the lines before it read */
    mem_breakdown_clear(&m);
    CHECK(mem_breakdown_parse_meminfo(cut, sizeof cut - 1, &m) == 2);
    CHECK(m.totalKb == 11535360 && m.freeKb == 831488 && m.availKb == -1 && m.cachedKb == -1);
    /* Cached is not SwapCached; past 2 TB and a word are not figures */
    mem_breakdown_clear(&m);
    CHECK(mem_breakdown_parse_meminfo(odd, sizeof odd - 1, &m) == 2);
    CHECK(m.cachedKb == 100 && m.swapCachedKb == 3072 && m.anonKb == -1 && m.shmemKb == -1);
    /* the status: a field missing */
    mem_breakdown_clear(&m);
    CHECK(mem_breakdown_parse_status(kStatusA, sizeof kStatusA - 1, &m) == 6);
    CHECK(m.rssKb == 172032 && m.hwmKb == 172032 && m.rssAnonKb == 81920 && m.rssFileKb == 88064 &&
          m.rssShmemKb == 2048 && m.vmSwapKb == 0);
    mem_breakdown_clear(&m);
    CHECK(mem_breakdown_parse_status("VmRSS:\t 100 kB\n", 15, &m) == 1);
    CHECK(m.rssKb == 100 && m.rssShmemKb == -1 && m.vmSwapKb == -1);
    /* the nodes */
    CHECK(mem_breakdown_parse_node("1073741824\n", 11, 0, &v) == 0 && v == 1073741824LL);
    CHECK(mem_breakdown_parse_node(" 42", 3, 0, &v) == 0 && v == 42);
    CHECK(mem_breakdown_parse_node("2147483648 536870912 545259520 0 545259520 0 0 0\n", 49, 2,
                                   &v) == 0 &&
          v == 545259520LL);
    CHECK(mem_breakdown_parse_node("12 34\n", 6, 2, &v) == EINVAL);
    CHECK(mem_breakdown_parse_node("12abc\n", 6, 0, &v) == EINVAL);
    CHECK(mem_breakdown_parse_node("", 0, 0, &v) == EINVAL);
    CHECK(mem_breakdown_parse_node("none\n", 5, 0, &v) == EINVAL);
}

static void parts(void)
{
    MemBreakdown a, b, none;
    long cat[MEM_CAT_N];
    char out[1024];
    long sum = 0;

    fill(&a, 0);
    fill(&b, 1);
    CHECK(mem_breakdown_cats(&a, cat) == 1);
    for (int i = 0; i < MEM_CAT_N; i++) {
        sum += cat[i];
    }
    CHECK(sum == a.totalKb - a.freeKb); /* the parts sum to the memory in use */
    CHECK(cat[MEM_CAT_FILE] == 10240 + 6553600 - 46080 && cat[MEM_CAT_KERNEL] == 552960 &&
          cat[MEM_CAT_GPU] == 1058816 && cat[MEM_CAT_REST] == -155648);
    mem_breakdown_full_text(&a, NULL, out, sizeof out);
    CHECK(text_is(out, kFullA));
    mem_breakdown_full_text(&b, "", out, sizeof out);
    CHECK(text_is(out, kFullB));
    mem_breakdown_clause(&a, &a, out, sizeof out);
    CHECK(text_is(out, kClauseA));
    mem_breakdown_clause(&b, &a, out, sizeof out);
    CHECK(text_is(out, kClauseB));
    mem_breakdown_last_run_text(&b, out, sizeof out);
    CHECK(text_is(out, "file cache 1565, anon 2100, kernel 553, GPU 5130, rest 899, swap used "
                       "1304 MB"));
    /* the GPU, dma-buf and zram not read: in the rest */
    b.gpuKb = b.dmaKb = b.zramKb = -1;
    mem_breakdown_clause(&b, NULL, out, sizeof out);
    CHECK(text_is(out, "free 1911 MB, file cache 1565, shared 45, anonymous 2100, kernel 553, GPU "
                       "?, the rest 6699 MB, swap used 1304 MB"));
    mem_breakdown_last_run_text(&b, out, sizeof out);
    CHECK(text_is(out, "file cache 1565, anon 2100, kernel 553, GPU ?, rest 6699, swap used "
                       "1304 MB"));
    mem_breakdown_full_text(&b, "kernel 2 MB, gpumem_mapped 180 MB", out, sizeof out);
    CHECK(strstr(out,
                 ", GPU not readable (in the rest), dma-buf not readable (in the rest), zram "
                 "not readable (in the rest), the rest 6699 MB (in none of these); ") != NULL &&
          strstr(out, "; this process's GPU memory: kernel 2 MB, gpumem_mapped 180 MB") != NULL);
    /* nothing read */
    mem_breakdown_clear(&none);
    CHECK(mem_breakdown_cats(&none, cat) == 0);
    mem_breakdown_last_run_text(&none, out, sizeof out);
    CHECK(out[0] == '\0');
    mem_breakdown_clause(&none, NULL, out, sizeof out);
    CHECK(text_is(out, "not read"));
    /* a short buffer: cut, ended */
    mem_breakdown_full_text(&a, NULL, out, 20);
    CHECK(strlen(out) == 19);
}

static void summary(void)
{
    MemBreakdown a, b;
    char out[1024], want[1024];

    fill(&a, 0);
    fill(&b, 1);
    mem_breakdown_summary(&a, &b, 290, 4.3, out, sizeof out);
    snprintf(want, sizeof want, "290 pipelines made in 4.3%s", kSummaryTail);
    CHECK(text_is(out, want));
    /* no pipeline: the changes in all */
    mem_breakdown_summary(&a, &b, 0, 0.5, out, sizeof out);
    {
        static const char head[] = "0 pipelines made in 0.5 s; free memory 7249 -> 1911 MB: "
                                   "5338.0 MB lost in all; the memory in use +512 MB, what grew "
                                   "in all (they sum to it): file cache -4800.0, ";
        CHECK(strncmp(out, head, sizeof head - 1) == 0);
    }
    CHECK(strstr(out, ", GPU +4096.0, ") != NULL && strstr(out, "; the most: GPU") != NULL);
    /* a part read at one end only */
    b.gpuKb = -1;
    mem_breakdown_summary(&a, &b, 290, 4.3, out, sizeof out);
    CHECK(strstr(out, ", GPU not read at both ends (in the rest), ") != NULL &&
          strstr(out, "; the most: the rest") != NULL);
    /* the end not read */
    mem_breakdown_clear(&b);
    mem_breakdown_summary(&a, &b, 290, 4.3, out, sizeof out);
    CHECK(text_is(out, "290 pipelines made in 4.3 s; the system's memory not read at both ends"));
}

static void rule(void)
{
    MemWatchRule r;
    MemWatchDue d;
    unsigned n;
    char out[512];

    mem_watch_rule(684, 7249, 1915, &r);
    CHECK(r.everyPipes == 43u && r.stepMb == 334); /* ceil(684 / 16), ceil(5334 / 16) */
    mem_watch_rule_text(&r, 684, 7249, 1915, out, sizeof out);
    CHECK(text_is(out, "the start-up set's batch lines carry the system's memory every 43 "
                       "pipelines (684 pipelines in 16 lines) and whenever free memory falls more "
                       "than 334 MB below the lowest shown (free 7249 MB less the floor 1915 MB "
                       "in 16 steps): at most 16 + 17 lines"));
    /* no floor, a set of 1, free under its floor */
    mem_watch_rule(684, 7249, -1, &r);
    CHECK(r.stepMb == 454); /* ceil(7249 / 16) */
    mem_watch_rule(1, 100, 200, &r);
    CHECK(r.everyPipes == 1u && r.stepMb == 1);
    mem_watch_rule(0, 15, 0, &r);
    CHECK(r.everyPipes == 1u && r.stepMb == 1);

    /* no fall: by count alone, every 11th batch of 4 (44 >= 43), every 6th
       of 8 (48) */
    mem_watch_rule(684, 7249, 1915, &r);
    mem_watch_due_start(&d, 7249);
    n = 0;
    for (unsigned done = 0; done < 684; done += 4) {
        n += (unsigned)mem_watch_due(&r, &d, 4, 7249);
    }
    CHECK(n == 15u && d.byCount == 15u && d.byFall == 0u);
    mem_watch_due_start(&d, 7249);
    n = 0;
    for (unsigned done = 0; done < 684; done += 8) {
        n += (unsigned)mem_watch_due(&r, &d, 684 - done < 8 ? 684 - done : 8, 7249);
    }
    CHECK(n == 14u && d.byCount == 14u);
    /* a fall of 334 MB is not more than the step; 335 is */
    mem_watch_due_start(&d, 7249);
    CHECK(mem_watch_due(&r, &d, 4, 6915) == 0);
    CHECK(mem_watch_due(&r, &d, 4, 6914) == 1 && d.byFall == 1u && d.lowShownMb == 6914 &&
          d.sincePipes == 0u);
    /* a rise and the same fall again: the lowest shown is the measure */
    CHECK(mem_watch_due(&r, &d, 4, 7000) == 0 && mem_watch_due(&r, &d, 4, 6600) == 0);
    CHECK(mem_watch_due(&r, &d, 4, 6579) == 1 && d.byFall == 2u);
    /* not read: by count only */
    mem_watch_due_start(&d, -1);
    CHECK(mem_watch_due(&r, &d, 42, -1) == 0 && mem_watch_due(&r, &d, 1, -1) == 1 &&
          d.byCount == 1u);

    /* the bound: the set of issue 215 falling from 7249 to 1911 MB, steady
       (15 by a fall) and every other batch back at the start (13 by count
       and 14 by a fall); at most 16 and 17 */
    mem_watch_due_start(&d, 7249);
    for (unsigned i = 1; i <= 171; i++) {
        (void)mem_watch_due(&r, &d, 4, 7249 - (long)(5338u * i / 171u));
    }
    CHECK(d.byCount == 0u && d.byFall == 15u);
    mem_watch_due_start(&d, 7249);
    for (unsigned i = 1; i <= 171; i++) {
        (void)mem_watch_due(&r, &d, 4, (i & 1u) ? 7249 - (long)(5338u * i / 171u) : 7249);
    }
    CHECK(d.byCount == 13u && d.byFall == 14u);
    CHECK(d.byCount <= MEM_WATCH_EVERY_LINES && d.byFall <= (unsigned)MEM_WATCH_STEP_LINES + 1u);
}

static void lastrun(void)
{
    static char text[LAST_RUN_BYTES];
    static LastRunFacts f;
    MemBreakdown b;
    char split[112], value[LAST_RUN_VALUE_MAX];
    const char *vals[LAST_RUN_SLOTS];

    fill(&b, 1);
    mem_breakdown_last_run_text(&b, split, sizeof split);
    snprintf(value, sizeof value,
             "1911 MB now of 11265 MB, lowest 1897 MB at 4.1 s, floor 1915 "
             "MB; %s",
             split);
    for (int s = 0; s < LAST_RUN_SLOTS; s++) {
        vals[s] = s == LAST_RUN_HEADER ? LAST_RUN_FORMAT : s == LAST_RUN_FREE ? value : "x";
    }
    /* the field has room for it whole */
    CHECK(strlen(value) <= last_run_slot_width(LAST_RUN_FREE) - LAST_RUN_TAIL -
                               strlen(last_run_slot_key(LAST_RUN_FREE)) - 2);
    last_run_text(vals, text);
    CHECK(last_run_parse(text, sizeof text, &f) == LAST_RUN_SLOTS);
    CHECK(f.state[LAST_RUN_FREE] == LAST_RUN_SLOT_READ && text_is(f.value[LAST_RUN_FREE], value));
    CHECK(f.freeMb == 1911 && f.freeLowMb == 1897 && f.floorMb == 1915);
}

#if defined(__linux__)
/* a file of the fake tree (its folders made) */
static void put_file(const char *root, const char *rel, const char *text)
{
    char path[700];
    FILE *fp;
    for (const char *p = strchr(rel + 1, '/'); p != NULL; p = strchr(p + 1, '/')) {
        snprintf(path, sizeof path, "%s%.*s", root, (int)(p - rel), rel);
        (void)ico_mkdir(path);
    }
    snprintf(path, sizeof path, "%s%s", root, rel);
    fp = ico_fopen(path, "wb");
    CHECK(fp != NULL);
    if (fp != NULL) {
        fputs(text, fp);
        fclose(fp);
    }
}

static void tree(const char *root, int end)
{
    put_file(root, "/proc/meminfo", end ? kMeminfoB : kMeminfoA);
    put_file(root, "/proc/self/status", end ? kStatusB : kStatusA);
    put_file(root, "/sys/class/kgsl/kgsl/page_alloc", end ? "5368709120\n" : "1073741824\n");
    put_file(root, "/sys/class/kgsl/kgsl/vmalloc", "10485760\n");
    put_file(root, "/sys/kernel/dma_heap/total_pools_kb", "51200\n");
    put_file(root, "/sys/block/zram0/mm_stat",
             end ? "2147483648 536870912 650117120 0 650117120 0 0 0\n"
                 : "2147483648 536870912 545259520 0 545259520 0 0 0\n");
}

static char s_log[16 * 1024];
static unsigned s_lines;

static void log_line(const char *line)
{
    const size_t n = strlen(s_log);
    snprintf(s_log + n, sizeof s_log - n, "%s\n", line);
    s_lines++;
}

static const char kSources[] =
    "ico_pc: memory: the system's memory split read from: /proc/meminfo, /proc/self/status, "
    "/sys/class/kgsl/kgsl/page_alloc, /sys/class/kgsl/kgsl/vmalloc, "
    "/sys/kernel/dma_heap/total_pools_kb, /sys/block/zram0/mm_stat; not readable: "
    "/sys/class/kgsl/kgsl/coherent (errno 21), /sys/class/kgsl/kgsl/secure (errno 2, not "
    "present), /sys/kernel/ion/total_heaps_kb (errno 2, not present); a part not readable is in "
    "the rest (the memory in use in none of the parts read)\n";

static void reader(const char *base)
{
    char root[600], path[700], out[600];
    MemBreakdown m;
    MemSources src;

    snprintf(root, sizeof root, "%s/mem_breakdown_test_root", base);
    (void)ico_remove_tree(root);
    CHECK(ico_mkdir(root) == 0);
    tree(root, 0);
    /* a node that is a folder: opened, its read fails (EISDIR) */
    snprintf(path, sizeof path, "%s/sys/class/kgsl/kgsl/coherent", root);
    CHECK(ico_mkdir(path) == 0);

    CHECK(mem_breakdown_read(root, MEM_BD_ALL, &m, &src) == 0);
    CHECK(src.err[MEM_SRC_MEMINFO] == 0 && src.err[MEM_SRC_STATUS] == 0 &&
          src.err[MEM_SRC_KGSL_PAGE] == 0 && src.err[MEM_SRC_KGSL_VMALLOC] == 0 &&
          src.err[MEM_SRC_KGSL_COHERENT] == EISDIR && src.err[MEM_SRC_KGSL_SECURE] == ENOENT &&
          src.err[MEM_SRC_DMA_HEAP] == 0 && src.err[MEM_SRC_ION] == ENOENT &&
          src.err[MEM_SRC_ZRAM] == 0);
    CHECK(m.availKb == 7422976 && m.rssKb == 172032 && m.gpuKb == 1048576 + 10240 &&
          m.dmaKb == 51200 && m.zramKb == 532480 && m.sysAt >= 0.0);
    mem_breakdown_full_text(&m, NULL, out, sizeof out);
    CHECK(text_is(out, kFullA));
    /* /proc alone: the last /sys figures */
    CHECK(mem_breakdown_read(root, MEM_BD_PROC, &m, &src) == 0);
    CHECK(src.err[MEM_SRC_KGSL_PAGE] == -1 && m.gpuKb == 1048576 + 10240 && m.zramKb == 532480);
    /* /sys alone: no /proc figure, -1 */
    CHECK(mem_breakdown_read(root, MEM_BD_SYS, &m, &src) == -1);
    CHECK(m.availKb == -1 && m.gpuKb == 1048576 + 10240 && src.err[MEM_SRC_MEMINFO] == -1);
    /* no /proc/meminfo */
    snprintf(path, sizeof path, "%s/proc/meminfo", root);
    CHECK(ico_remove(path) == 0);
    CHECK(mem_breakdown_read(root, MEM_BD_ALL, &m, &src) == -1);
    CHECK(src.err[MEM_SRC_MEMINFO] == ENOENT && m.availKb == -1 && m.rssKb == 172032);
    (void)ico_remove_tree(root);
}

static void watch(const char *base)
{
    char root[600], path[700], out[600], want[1600];
    const char *p;

    snprintf(root, sizeof root, "%s/mem_watch_test_root", base);
    (void)ico_remove_tree(root);
    CHECK(ico_mkdir(root) == 0);
    tree(root, 0);
    snprintf(path, sizeof path, "%s/sys/class/kgsl/kgsl/coherent", root);
    CHECK(ico_mkdir(path) == 0);
    mem_watch_test_set(log_line, root, 1915);

    /* the set's start: the sources, the picture, the rule */
    s_log[0] = '\0';
    s_lines = 0;
    mem_watch_set_pipelines(0, 684);
    snprintf(want, sizeof want,
             "%sico_pc: memory before the start-up set (684 pipelines): %s\nico_pc: memory: the "
             "start-up set's batch lines carry the system's memory every 43 pipelines (684 "
             "pipelines in 16 lines) and whenever free memory falls more than 334 MB below the "
             "lowest shown (free 7249 MB less the floor 1915 MB in 16 steps): at most 16 + 17 "
             "lines\n",
             kSources, kFullA);
    CHECK(text_is(s_log, want));
    /* ten batches of 4: no clause; the eleventh: by count */
    for (int i = 0; i < 10; i++) {
        CHECK(mem_watch_batch_clause(1, 4, 684, out, sizeof out) == 0);
    }
    CHECK(mem_watch_batch_clause(41, 44, 684, out, sizeof out) == 1 && text_is(out, kClauseA));
    /* the memory falls: the next batch by a fall */
    tree(root, 1);
    CHECK(mem_watch_batch_clause(45, 48, 684, out, sizeof out) == 1 && text_is(out, kClauseB));
    CHECK(mem_watch_batch_clause(49, 52, 684, out, sizeof out) == 0);
    mem_watch_set_pipelines(290, 684);
    /* the end: the picture and the summary */
    s_log[0] = '\0';
    s_lines = 0;
    mem_watch_set_pipelines(0, 0);
    snprintf(want, sizeof want,
             "ico_pc: memory at the start-up set's end (290 of 684 pipelines made; 2 clauses on "
             "its batch lines, 1 by count and 1 by a fall): %s\nico_pc: memory over the start-up "
             "set: 290 pipelines made in ",
             kFullB);
    CHECK(s_lines == 2u && strncmp(s_log, want, strlen(want)) == 0);
    p = strstr(s_log, " s; free memory ");
    snprintf(want, sizeof want, "%s\n", kSummaryTail);
    CHECK(p != NULL && text_is(p, want));
    /* no second end; a batch after it: no clause */
    s_log[0] = '\0';
    mem_watch_set_pipelines(0, 0);
    CHECK(s_log[0] == '\0' && mem_watch_batch_clause(1, 4, 684, out, sizeof out) == 0);
    /* the system's signal; the 10 s clause against the set's start */
    mem_watch_trim(15);
    snprintf(want, sizeof want,
             "ico_pc: memory at the system's signal (onTrimMemory level 15): %s\n", kFullB);
    CHECK(text_is(s_log, want));
    mem_watch_block_clause(out, sizeof out);
    CHECK(text_is(out, kClauseB));
    mem_watch_test_set(NULL, NULL, -2);
    (void)ico_remove_tree(root);
}
#endif

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: mem_breakdown_test <folder>\n");
        return 2;
    }
    parse();
    parts();
    summary();
    rule();
    lastrun();
#if defined(__linux__)
    reader(argv[1]);
    watch(argv[1]);
#else
    {
        MemBreakdown m;
        MemSources src;
        char out[256];
        CHECK(mem_breakdown_read("", MEM_BD_ALL, &m, &src) == -1 && m.availKb == -1);
        mem_breakdown_sources_text(&src, out, sizeof out);
#if defined(_WIN32) || defined(__APPLE__)
        CHECK(text_is(out, "the system's memory split not available on this system (Linux and "
                           "Android only)"));
#endif
    }
#endif
    if (fails != 0) {
        printf("mem_breakdown_test: %d failure(s)\n", fails);
        return 1;
    }
    printf("mem_breakdown_test: ok\n");
    return 0;
}

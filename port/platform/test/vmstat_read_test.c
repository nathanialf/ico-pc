/*
 * port/platform/test/vmstat_read_test.c
 *
 * The kernel's memory events a block (vmstat_read.h, issue 179), from
 * strings in the test: a newer kernel's /proc/vmstat (allocstall by zone
 * and the anon and file refaults summed, a count of another name not
 * taken), an older kernel's (one allocstall, one refault count, no
 * compaction or swap counts), the last line with no newline, a text with
 * none of the counts, the block's line word for word (the counts' growth,
 * a count not in the file, a count gone back), a short buffer cut.
 */
#include <stdio.h>
#include <string.h>

#include "vmstat_read.h"

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

static const char kNew[] = "nr_free_pages 12345\n"
                           "allocstall_dma 0\n"
                           "allocstall_dma32 0\n"
                           "allocstall_normal 7\n"
                           "allocstall_movable 3\n"
                           "compact_stall 2\n"
                           "compact_fail 1\n"
                           "pgmajfault 1000\n"
                           "pswpin 10\n"
                           "pswpout 20\n"
                           "workingset_refault_anon 5\n"
                           "workingset_refault_file 40\n"
                           "workingset_restore_anon 9\n";

static const char kNewLater[] = "allocstall_normal 9\n"
                                "allocstall_movable 4\n"
                                "compact_stall 2\n"
                                "pgmajfault 1123\n"
                                "pswpin 10\n"
                                "pswpout 25\n"
                                "workingset_refault_anon 6\n"
                                "workingset_refault_file 44\n";

static const char kOld[] = "allocstall 4\nworkingset_refault 6\npgmajfault 50";

int main(void)
{
    VmStat a, b, o;
    char t[512];

    CHECK(vmstat_read_parse(kNew, sizeof kNew - 1u, &a));
    CHECK(a.have[VMSTAT_ALLOCSTALL] && a.v[VMSTAT_ALLOCSTALL] == 10u);
    CHECK(a.have[VMSTAT_COMPACT_STALL] && a.v[VMSTAT_COMPACT_STALL] == 2u);
    CHECK(a.have[VMSTAT_PGMAJFAULT] && a.v[VMSTAT_PGMAJFAULT] == 1000u);
    CHECK(a.have[VMSTAT_PSWPIN] && a.v[VMSTAT_PSWPIN] == 10u);
    CHECK(a.have[VMSTAT_PSWPOUT] && a.v[VMSTAT_PSWPOUT] == 20u);
    CHECK(a.have[VMSTAT_REFAULT] && a.v[VMSTAT_REFAULT] == 45u);

    CHECK(vmstat_read_parse(kNewLater, sizeof kNewLater - 1u, &b));
    CHECK(vmstat_delta_text(&a, &b, t, sizeof t) == strlen(t));
    CHECK(text_is(t, "memory events in the block (/proc/vmstat): direct reclaim stalls 3, "
                     "compaction stalls 0, major page faults 123, swap-ins 0, swap-outs 5, "
                     "refaults 5"));

    /* an older kernel: one allocstall, one refault count, the last line
       without a newline */
    CHECK(vmstat_read_parse(kOld, sizeof kOld - 1u, &o));
    CHECK(o.have[VMSTAT_ALLOCSTALL] && o.v[VMSTAT_ALLOCSTALL] == 4u);
    CHECK(o.have[VMSTAT_REFAULT] && o.v[VMSTAT_REFAULT] == 6u);
    CHECK(o.have[VMSTAT_PGMAJFAULT] && o.v[VMSTAT_PGMAJFAULT] == 50u);
    CHECK(!o.have[VMSTAT_COMPACT_STALL] && !o.have[VMSTAT_PSWPIN] && !o.have[VMSTAT_PSWPOUT]);
    (void)vmstat_delta_text(&o, &o, t, sizeof t);
    CHECK(text_is(t, "memory events in the block (/proc/vmstat): direct reclaim stalls 0, "
                     "compaction stalls not in the file, major page faults 0, swap-ins not in "
                     "the file, swap-outs not in the file, refaults 0"));

    /* a count gone back */
    (void)vmstat_delta_text(&b, &a, t, sizeof t);
    CHECK(strstr(t, "major page faults reset") != NULL);

    /* none of the counts */
    {
        static const char g[] = "hello\nnr_free_pages 5\n";
        CHECK(!vmstat_read_parse(g, sizeof g - 1u, &o));
    }
    CHECK(!vmstat_read_parse("", 0, &o));

    /* a short buffer: cut and ended */
    {
        char s[16];
        const size_t n = vmstat_delta_text(&a, &b, s, sizeof s);
        CHECK(n == sizeof s - 1u && strlen(s) == n);
    }

    if (fails) {
        printf("vmstat_read_test: %d failures\n", fails);
        return 1;
    }
    printf("vmstat_read_test: ok\n");
    return 0;
}

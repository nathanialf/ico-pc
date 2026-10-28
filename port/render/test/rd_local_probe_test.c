/* rd_local_probe_test.c: the desktop's local-read probe's verdict
 * (rd_local_probe.c rd__local_probe_keep) on synthetic times, no device:
 * the local reads stay when their least GPU time is at most
 * RD_LOCAL_PROBE_TOLERANCE (5 %) above the snapshots' least, go off above
 * it, and stay when either time is missing (0, negative or NaN: nothing
 * measured).  Without rd_init the probe itself does not run (-1).  The
 * probe on a device runs in ctests rd_local_probe_kept and
 * rd_local_probe_off (lavapipe, both verdicts). */
#include <math.h>
#include <stdio.h>

#include "rd_internal.h"

static int failures;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static const struct {
    double local, snap;
    bool keep;
    const char *what;
} kCases[] = {
    {0.400, 0.530, true, "the local reads faster"},
    {0.530, 0.530, true, "equal"},
    {0.550, 0.530, true, "3.8 % slower: within the tolerance"},
    {1.050, 1.000, true, "5 % slower: the tolerance's edge"},
    {1.051, 1.000, false, "5.1 % slower"},
    {0.611, 0.530, false, "15 % slower"},
    {4.000, 1.000, false, "four times"},
    {0.0, 0.530, true, "no local time"},
    {0.530, 0.0, true, "no snapshot time"},
    {-1.0, 0.530, true, "a negative time"},
    {NAN, 0.530, true, "NaN"},
    {0.530, NAN, true, "NaN for the snapshots"},
};

int main(void)
{
    printf("rd_local_probe_test\n");
    CHECK(RD_LOCAL_PROBE_TOLERANCE == 0.05, "the tolerance is 5 %% (%g)", RD_LOCAL_PROBE_TOLERANCE);
    for (size_t i = 0; i < sizeof kCases / sizeof kCases[0]; i++) {
        const bool keep = rd__local_probe_keep(kCases[i].local, kCases[i].snap);
        printf("  %-40s %.3f vs %.3f ms: %s\n", kCases[i].what, kCases[i].local, kCases[i].snap,
               keep ? "kept" : "off");
        CHECK(keep == kCases[i].keep, "%s: %s, %s expected", kCases[i].what, keep ? "kept" : "off",
              kCases[i].keep ? "kept" : "off");
    }
    CHECK(rd__local_probe(RD_FOG_INPLACE) == -1, "no renderer: the probe does not run");
    if (failures) {
        printf("rd_local_probe_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_local_probe_test: ok\n");
    return 0;
}

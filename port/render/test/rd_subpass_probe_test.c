/* rd_subpass_probe_test.c: the subpass read's probe rule (rd_subpass_probe.c
 * rd__subpass_probe_keep) on synthetic least times, no device: the read
 * stays when its frame is at most RD_SUBPASS_PROBE_TOLERANCE (5 %) above the
 * snapshot's and the shaped plain pass at most the larger of that and the
 * plain twin's own spread above the twin's; the read's frame is judged
 * first; any time missing (0, negative, NaN, infinite) is untimed.  Without
 * a renderer the after-self-test hook does nothing.  The probe on a device
 * runs in ctests rd_subpass_probe_kept, _refused, _plain, _selftest and
 * _end (lavapipe). */
#include <math.h>
#include <stdio.h>

#include "rd_internal.h"
#include "rd_subpass_probe.h"

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
    double read, snap, shaped, plain, spread;
    RdSubpassProbeRule want;
    const char *what;
} kCases[] = {
    {0.400, 0.530, 0.200, 0.200, 0.0, RD_SUBPASS_PROBE_KEEP, "the read faster, the shape free"},
    {1.050, 1.000, 1.050, 1.000, 0.0, RD_SUBPASS_PROBE_KEEP, "both at the tolerance's edge"},
    {1.051, 1.000, 1.000, 1.000, 0.0, RD_SUBPASS_PROBE_READ_SLOWER, "the read 5.1 % slower"},
    {1.000, 1.000, 1.051, 1.000, 0.0, RD_SUBPASS_PROBE_PLAIN_SLOWER, "a plain pass 5.1 % slower"},
    {1.000, 1.000, 1.080, 1.000, 0.10, RD_SUBPASS_PROBE_KEEP,
     "8 % slower inside the twin's own 10 % spread"},
    {1.000, 1.000, 1.101, 1.000, 0.10, RD_SUBPASS_PROBE_PLAIN_SLOWER, "past the 10 % spread"},
    {1.000, 1.000, 1.051, 1.000, 0.01, RD_SUBPASS_PROBE_PLAIN_SLOWER,
     "a spread under the tolerance: the tolerance"},
    {1.000, 1.000, 1.051, 1.000, NAN, RD_SUBPASS_PROBE_PLAIN_SLOWER, "a NaN spread: the tolerance"},
    {2.000, 1.000, 2.000, 1.000, 0.0, RD_SUBPASS_PROBE_READ_SLOWER, "both slower: the read first"},
    {0.0, 0.530, 0.2, 0.2, 0.0, RD_SUBPASS_PROBE_UNTIMED, "no read time"},
    {0.4, 0.0, 0.2, 0.2, 0.0, RD_SUBPASS_PROBE_UNTIMED, "no snapshot time"},
    {0.4, 0.5, -1.0, 0.2, 0.0, RD_SUBPASS_PROBE_UNTIMED, "a negative plain time"},
    {0.4, 0.5, 0.2, NAN, 0.0, RD_SUBPASS_PROBE_UNTIMED, "NaN for the twin"},
    {INFINITY, 0.5, 0.2, 0.2, 0.0, RD_SUBPASS_PROBE_UNTIMED, "an infinite read time"},
};

int main(void)
{
    printf("rd_subpass_probe_test\n");
    CHECK(RD_SUBPASS_PROBE_TOLERANCE == RD_LOCAL_PROBE_TOLERANCE,
          "the tolerance is the local-read probe's (%g)", RD_SUBPASS_PROBE_TOLERANCE);
    for (size_t i = 0; i < sizeof kCases / sizeof kCases[0]; i++) {
        const RdSubpassProbeRule r = rd__subpass_probe_keep(
            kCases[i].read, kCases[i].snap, kCases[i].shaped, kCases[i].plain, kCases[i].spread);
        printf("  %-46s %d\n", kCases[i].what, (int)r);
        CHECK(r == kCases[i].want, "%s: %d, %d expected", kCases[i].what, (int)r,
              (int)kCases[i].want);
    }
    rd__subpass_probe_after_selftest(1, true, "stencil ok 5, snapshot ok 5");
    CHECK(true, "no renderer: nothing done");
    if (failures) {
        printf("rd_subpass_probe_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_subpass_probe_test: ok\n");
    return 0;
}

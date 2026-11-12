/* rd_cpu_phase_test.c: the render thread's CPU phases (rd_cpu_phase.c), CPU
 * only.
 *
 *   1. two pictures of one frame: the first with two marks and the hash's,
 *      the look-ups counted; the second a start and its end: two pictures,
 *      one tick, one hashed, 4 + 2 clock reads, the look-ups folded once and
 *      g_rdCpu emptied, every phase's time non-negative; taken again: empty;
 *   2. the line's words from a made-up window (phase k at k + 1 ms a
 *      picture over 2 pictures of one tick), character by character;
 *   3. no picture: no line. */
#include <stdio.h>
#include <string.h>

#include "rd_cpu_phase.h"

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

int main(void)
{
    printf("rd_cpu_phase_test\n");
    /* 1. the folds */
    RdCpuWindow w;
    rd_cpu_phase_take(&w); /* empty to start with */
    rd_cpu_start();
    rd_cpu_mark(RD_CPU_I_COPY);
    rd_cpu_mark_hash(true);
    g_rdCpu.pipeLookups = 5;
    g_rdCpu.pipeHits = 3;
    g_rdCpu.uniformLookups = 7;
    g_rdCpu.uniformHits = 2;
    rd_cpu_picture(10);
    CHECK(g_rdCpu.pipeLookups == 0 && g_rdCpu.uniformLookups == 0, "the counts not emptied");
    rd_cpu_start();
    rd_cpu_picture(10);
    rd_cpu_phase_take(&w);
    CHECK(w.pictures == 2 && w.ticks == 1 && w.hashed == 1,
          "%llu pictures, %llu ticks, %llu hashed", (unsigned long long)w.pictures,
          (unsigned long long)w.ticks, (unsigned long long)w.hashed);
    CHECK(w.reads == 6, "%llu clock reads, 6 expected", (unsigned long long)w.reads);
    CHECK(w.pipeLookups == 5 && w.pipeHits == 3 && w.uniformLookups == 7 && w.uniformHits == 2,
          "the look-ups %llu/%llu %llu/%llu", (unsigned long long)w.pipeLookups,
          (unsigned long long)w.pipeHits, (unsigned long long)w.uniformLookups,
          (unsigned long long)w.uniformHits);
    rd_cpu_phase_take(&w);
    CHECK(w.pictures == 0 && w.reads == 0, "taken twice: %llu pictures",
          (unsigned long long)w.pictures);
    char out[1024];
    CHECK(!rd_cpu_phase_format(&w, out, sizeof out) && out[0] == '\0', "a line with no picture");

    /* 2. the words */
    memset(&w, 0, sizeof(w));
    for (int k = 0; k < RD_CPU_PHASES; k++) {
        w.ns[k] = (uint64_t)(k + 1) * 2000000u; /* k + 1 ms a picture over 2 */
    }
    w.pictures = 2;
    w.ticks = 1;
    w.hashed = 1;
    w.reads = 44;
    w.pipeLookups = 1000;
    w.pipeHits = 900;
    w.uniformLookups = 1200;
    w.uniformHits = 300;
    const bool ok = rd_cpu_phase_format(&w, out, sizeof out);
    const char *want =
        "CPU phases ms a picture over 2 pictures (2.0 a tick): interp copy 1.00, index 2.00, "
        "blend 3.00, morph 4.00, unmatched 5.00, hash 6.00 (1 of the pictures hashed), the rest "
        "7.00; upload cull 8.00, ring 9.00, acquire and begin 10.00, join 11.00, meshes 12.00, "
        "scans: layer 13.00, fades 14.00, the store plan's walk 15.00, present plan 16.00, clears "
        "17.00; walk pre-pass 18.00, late uploads 19.00, lists 20.00; per picture 500 pipeline "
        "look-ups (450 hits), 600 uniform blocks looked up (150 hits); 22.0 clock reads a "
        "picture";
    CHECK(ok && strcmp(out, want) == 0, "the line:\n  %s\nexpected\n  %s", out, want);
    if (failures) {
        printf("rd_cpu_phase_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_cpu_phase_test: ok\n");
    return 0;
}

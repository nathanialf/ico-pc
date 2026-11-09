/* rd_set_resume_test: the start-up set resumed after a low-memory stop
 * (issue 123; rd.h rd_set_pipeline_resume, rd_pipeline_set_stop;
 * port/platform/pipe_resume.h).  On the Adreno 740 under Qualcomm's driver
 * a stop of the set for memory turned the set off for every later start of
 * the version, and the first scene then compiled 29 pipelines inside one
 * picture's walk (1.39 s).  The window now resumes the set where it
 * stopped; this test runs the renderer's half on the device the tests run
 * on (lavapipe in CI), each start a fresh rd_init with the low-memory
 * function saying yes after the first batch:
 *
 *   stop     the whole set from its first key in the default batches (32):
 *            stopped, the place past the first batch (a batch ends early
 *            where the destination-read kind changes, so only past the
 *            first key), the batch 32, the set's size, 1 to 32 pipelines
 *            made, the rest on first use (rd_pipelines_lazy_on)
 *   rotate   resumed at that place, batch 0 (half the set's: 16), the
 *            pipelines before it made after the rest (no cache file): the
 *            progress counts the whole set, the batch 16, 1 to 16 made, the
 *            stop's place moved on from the resume's in the set's own order
 *            (the rotation undone, modulo the size)
 *   skip     resumed at the second stop's place in batches of 8 with the
 *            keys before it left to the pipeline cache: the progress counts
 *            the rest only (size - place), the batch 8, 1 to 8 made, the
 *            stop past the place and inside the set
 *   past     a place at or past the set's size starts from its first key
 *            (the set shrank): the progress counts the whole set
 *   floor    a resume batch under 4 is made 4 (pipe_resume.h's floor)
 *   full     no resume and no stop: the whole set, rd_pipeline_set_stop
 *            false
 *
 * Exit 77 without a device. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"

static int s_fail;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            s_fail++;                                                                              \
        }                                                                                          \
    } while (0)

#define W 512
#define H 448

/* the low-memory function (rd_set_low_memory_fn): yes from its first ask
   (after the set's first batch) while s_low is set */
static int s_low;

static int lowMemory(char *why, size_t size)
{
    if (!s_low) {
        return 0;
    }
    snprintf(why, size, "test signal");
    return 1;
}

/* the progress function (rd_set_pipeline_progress): its first call's total */
static struct {
    int calls;
    uint32_t total;
} s_prog;

static int progress(void *ctx, uint32_t done, uint32_t total)
{
    (void)ctx;
    (void)done;
    if (s_prog.calls++ == 0) {
        s_prog.total = total;
    }
    return 0;
}

static void settings(RdSettings *st)
{
    memset(st, 0, sizeof(*st));
    st->outputWidth = 640;
    st->outputHeight = 480;
    st->aspect = 4.0f / 3.0f;
    st->preset = RD_PRESET_ORIGINAL;
    st->sceneScale = 1.0f;
}

/* one start: rd_init, the resume (at below 0: none), the set with the
   low-memory function saying yes after its first batch (low) or never;
   the pipelines it made and where it stopped */
static bool start(int64_t at, uint32_t batch, bool skip, bool low, RdPipelineSetStop *stop,
                  uint32_t *made)
{
    RdSettings st;
    settings(&st);
    if (!rd_init(W, H, &st, NULL)) {
        return false;
    }
    memset(&s_prog, 0, sizeof(s_prog));
    s_low = low ? 1 : 0;
    rd_set_low_memory_fn(lowMemory);
    rd_set_pipeline_progress(progress, NULL);
    if (at >= 0) {
        rd_set_pipeline_resume((uint32_t)at, batch, skip);
    }
    const uint32_t before = rd__pipeline_count();
    (void)rd_precreate_pipelines();
    *made = rd__pipeline_count() - before;
    rd_set_pipeline_progress(NULL, NULL);
    rd_set_low_memory_fn(NULL);
    const bool stopped = rd_pipeline_set_stop(stop);
    CHECK(stopped == low, "the set %s, the low-memory function %s",
          stopped ? "stopped" : "did not stop", low ? "said yes" : "never said yes");
    return true;
}

int main(void)
{
    RdPipelineSetStop a, b, c, d;
    uint32_t made = 0;

    rd__set_not_implemented_fatal(true);

    /* stop: the whole set, stopped after its first batch */
    if (!start(-1, 0, false, true, &a, &made)) {
        printf("no device: skipped\n");
        return 77;
    }
    const uint32_t m = a.total;
    printf("stop: %u of %u, batch %u, %u made\n", a.at, m, a.batch, made);
    CHECK(m > 64, "the set has %u keys (too few to resume in)", m);
    CHECK(s_prog.total == m, "stop: the progress counted %u of %u", s_prog.total, m);
    CHECK(a.batch == 32, "stop: batch %u, not the default 32", a.batch);
    CHECK(a.at > 0 && a.at < m, "stop: at %u of %u", a.at, m);
    CHECK(made >= 1 && made <= 32, "stop: %u made in one batch of 32", made);
    CHECK(rd_pipelines_lazy_on(), "stop: the rest not left to first use");
    CHECK(strstr(a.why, "test signal") != NULL, "stop: why \"%s\"", a.why);
    rd_shutdown();

    /* rotate: resumed there, half the batch, the keys before it after the
       rest */
    if (!start(a.at, 0, false, true, &b, &made)) {
        printf("FAIL: rd_init (rotate)\n");
        return 1;
    }
    printf("rotate: from %u: %u of %u, batch %u, %u made\n", a.at, b.at, b.total, b.batch, made);
    CHECK(s_prog.total == m, "rotate: the progress counted %u, not the set's %u", s_prog.total, m);
    CHECK(b.total == m, "rotate: total %u of %u", b.total, m);
    CHECK(b.batch == 16, "rotate: batch %u, not 16", b.batch);
    CHECK(made >= 1 && made <= 16, "rotate: %u made in one batch of 16", made);
    CHECK(b.at != a.at, "rotate: stopped where it resumed (%u)", b.at);
    rd_shutdown();

    /* skip: the keys before the place left to the cache */
    if (!start(b.at, 8, true, true, &c, &made)) {
        printf("FAIL: rd_init (skip)\n");
        return 1;
    }
    printf("skip: from %u: %u of %u, batch %u, %u made\n", b.at, c.at, c.total, c.batch, made);
    CHECK(s_prog.total == m - b.at, "skip: the progress counted %u, not the rest's %u",
          s_prog.total, m - b.at);
    CHECK(c.batch == 8, "skip: batch %u, not 8", c.batch);
    CHECK(made >= 1 && made <= 8, "skip: %u made in one batch of 8", made);
    CHECK(c.at > b.at && c.at <= m, "skip: stopped at %u from %u of %u", c.at, b.at, m);
    rd_shutdown();

    /* past: a place past the set starts from its first key; floor: a
       batch under 4 is 4 */
    if (!start((int64_t)m + 5, 2, true, true, &d, &made)) {
        printf("FAIL: rd_init (past)\n");
        return 1;
    }
    printf("past: %u of %u, batch %u, %u made\n", d.at, d.total, d.batch, made);
    CHECK(s_prog.total == m, "past: the progress counted %u of %u", s_prog.total, m);
    CHECK(d.batch == 4, "past: batch %u, not the floor's 4", d.batch);
    CHECK(made >= 1 && made <= 4, "past: %u made in one batch of 4", made);
    CHECK(d.at > 0 && d.at < m, "past: stopped at %u", d.at);
    rd_shutdown();

    /* full: no resume, no stop */
    if (!start(-1, 0, false, false, &d, &made)) {
        printf("FAIL: rd_init (full)\n");
        return 1;
    }
    CHECK(s_prog.total == m, "full: the progress counted %u of %u", s_prog.total, m);
    CHECK(d.total == 0 && d.at == 0, "full: a stop reported (%u of %u)", d.at, d.total);
    rd_shutdown();

    printf("rd_set_resume_test: %s\n", s_fail ? "FAILED" : "ok");
    return s_fail ? 1 : 0;
}

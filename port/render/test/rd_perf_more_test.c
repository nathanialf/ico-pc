/* rd_perf_more_test.c: the cost counters' block sums and lines
 * (rd_perf_more.h): the breaks' reasons by piece, the screen box's area
 * (inside, clipped, behind the eye, the scissor mode's w range), and the
 * costs and compare lines word for word from two made-up records of
 * issue 179's stage 42 shape (CPU only). */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "rd_perf_more.h"

static int fails;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                    \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static void text_is(const char *got, const char *want, int line)
{
    if (strcmp(got, want) != 0) {
        printf("FAIL line %d:\n got  %s\n want %s\n", line, got, want);
        fails++;
    }
}

static void test_breaks(void)
{
    CHECK(rd_perf_break_of(RD_PERF_PASS_DATE, RD_TARGET_DATE_SNAPSHOT, -1) == RD_BREAK_DATE);
    CHECK(rd_perf_break_of(RD_PERF_PASS_RENDER, RD_PERF_TARGET_SHADOW_COUNT, -1) ==
          RD_BREAK_SHADOW);
    CHECK(rd_perf_break_of(RD_PERF_PASS_RENDER, RD_TARGET_SHADOW1, -1) == RD_BREAK_SHADOW);
    CHECK(rd_perf_break_of(RD_PERF_PASS_STENCIL, RD_TARGET_SCENE, -1) == RD_BREAK_SHADOW);
    CHECK(rd_perf_break_of(RD_PERF_PASS_RENDER, RD_TARGET_FEED128, RD_PERF_POST_AURA) ==
          RD_BREAK_AURA);
    CHECK(rd_perf_break_of(RD_PERF_PASS_RENDER, RD_TARGET_DISPLAY, RD_PERF_POST_REDUCTION) ==
          RD_BREAK_REDUCTION);
    CHECK(rd_perf_break_of(RD_PERF_PASS_COPY, RD_TARGET_SCENE, RD_PERF_POST_CRACK) ==
          RD_BREAK_CRACK);
    CHECK(rd_perf_break_of(RD_PERF_PASS_RENDER, RD_TARGET_WORK0, RD_PERF_POST_GLOW) ==
          RD_BREAK_GLOW);
    CHECK(rd_perf_break_of(RD_PERF_PASS_RENDER, RD_TARGET_WORK2, -1) == RD_BREAK_LAMPS);
    CHECK(rd_perf_break_of(RD_PERF_PASS_SNAP, RD_TARGET_SCENE, -1) == RD_BREAK_COPY);
    CHECK(rd_perf_break_of(RD_PERF_PASS_PRESENT, RD_PERF_TARGET_WINDOW, -1) == RD_BREAK_PRESENT);
    CHECK(rd_perf_break_of(RD_PERF_PASS_RENDER, RD_TARGET_AA0, -1) == RD_BREAK_OTHER);
    CHECK(strcmp(rd_perf_break_word(RD_BREAK_AGAIN), "again") == 0);
    CHECK(strcmp(rd_perf_break_word(-1), "other") == 0);
    CHECK(strcmp(rd_perf_break_word(RD_BREAK_COUNT), "other") == 0);
}

static void test_box(void)
{
    /* h = (x, y, z, w): the box itself on screen */
    const float id[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    const float lo[4] = {10, 20, 0, 1}, hi[4] = {30, 60, 5, 2};
    CHECK(fabs(rd_perf_box_area(id, lo, hi, false, 0, 0, 100, 100) - 800.0) < 1e-9);
    /* clipped to the rectangle */
    CHECK(fabs(rd_perf_box_area(id, lo, hi, false, 0, 0, 25, 50) - 450.0) < 1e-9);
    /* nothing inside */
    CHECK(rd_perf_box_area(id, lo, hi, false, 40, 0, 100, 100) == 0.0);
    /* the scissor mode: w over 1..2, (5..30) x (10..60) */
    CHECK(fabs(rd_perf_box_area(id, lo, hi, true, 0, 0, 100, 100) - 1250.0) < 1e-9);
    /* behind the eye: the whole rectangle */
    const float back[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, -1}};
    CHECK(fabs(rd_perf_box_area(back, lo, hi, false, 0, 0, 50, 40) - 2000.0) < 1e-9);
}

/* stage 42's shape: list 0 heavy, three DATE snapshots, two breaks for
   them and one for the aura, the two-pass blend on the composites */
static void record(RdPerfRecord *r, double list0)
{
    memset(r, 0, sizeof(*r));
    r->vuTris = 52000;
    r->vuPreVertices = 85000;
    r->draws = 351;
    r->renderPasses = 21;
    r->noDualPairs = 3;
    r->noDualColour = 1;
    r->noDualTris = 0;
    r->breakN[RD_BREAK_DATE] = 2;
    r->breakKB[RD_BREAK_DATE] = 9420;
    r->breakN[RD_BREAK_AURA] = 1;
    r->breakKB[RD_BREAK_AURA] = 4608;
    r->dateSnapshots = 3;
    r->dateRetakeWhy[RD_DATE_RETAKE_FIRST] = 1;
    r->dateRetakeWhy[RD_DATE_RETAKE_WRITTEN] = 2;
    r->listBoxPx[0] = 1850000;
    r->listBoxDraws[0] = 112;
    r->listBoxNone[0] = 6;
    r->listBoxTargetPx[0] = 466944; /* 912 x 512 */
    r->vuListTris[0] = 50000;
    r->vuListTris[3] = 2000;
    r->glowLight = RD_GLOW_LIGHT_NONE;
    r->gpuValid = 1;
    r->gpuMs = 110.0;
    r->gpuUploadMs = 4.3;
    r->gpuPrepassMs = 6.2;
    r->gpuPresentMs = 2.9;
    r->gpuListMs[0] = list0;
    r->gpuListMs[3] = 10.1;
    r->gpuPostMs[RD_PERF_POST_AURA] = 5.5;
    r->gpuPostMs[RD_PERF_POST_REDUCTION] = 4.3;
}

static void test_lines(void)
{
    RdPerfMore b;
    RdPerfRecord r;
    char t[2048];

    rd_perf_more_reset(&b);
    CHECK(rd_perf_more_cost_text(&b, t, sizeof t) == 0 && t[0] == '\0');
    record(&r, 76.0);
    rd_perf_more_add(&b, &r, true);
    record(&r, 77.0);
    r.keep = 1;
    rd_perf_more_add(&b, &r, true);
    CHECK(b.n == 2 && b.keep == 1 && b.gpuN == 2 && b.prepassN == 2);
    CHECK(rd_perf_more_draw_invocations(52000, true) == 156000u);
    CHECK(rd_perf_more_draw_invocations(52000, false) == 468000u);
    /* after the index compaction: a triangle it left out one invocation
       (156000 - 2 x 20000), at most every triangle, none without the
       pre-pass */
    CHECK(rd_perf_more_draw_invocations_left(52000, 20000, true) == 116000u);
    CHECK(rd_perf_more_draw_invocations_left(52000, 60000, true) == 52000u);
    CHECK(rd_perf_more_draw_invocations_left(52000, 20000, false) == 468000u);
    CHECK(rd_perf_more_draw_invocations_left(52000, 0, true) == 156000u);
    (void)rd_perf_more_cost_text(&b, t, sizeof t);
    text_is(t,
            "costs a picture: two-pass blend 3.0 draws drawn twice (0 triangles' vertex work "
            "twice), 1.0 drawn once by the colour pass; SCENE's pass ended mid-picture 3.0 times "
            "(DATE 2.0, 9.2 MB; aura 1.0, 4.5 MB); DATE snapshots 3.0 (the first 1.0, an alpha "
            "write since 2.0), 0.0 reused, 0.0 composites with an empty box; boxes against "
            "triangles: list 0 50k triangles in 1.85 Mpx of boxes (4.0 times its target's 0.47 "
            "Mpx GS; 112 draws, 6 without a box), list 3 2.0k triangles, no box (0 draws without "
            "one, or the culler off)",
            __LINE__);
    (void)rd_perf_more_compare_text(&b, "stage 42 scene (24a4demo)", true,
                                    "on, buffers (gpu_robust)", t, sizeof t);
    text_is(t,
            "compare: stage 42 scene (24a4demo): 2 pictures (1 keep frames); a picture 52k "
            "triangles, 351 draws, 21.0 passes, 3.0 breaks; GPU ms a picture: span 110.0, before "
            "list 0 4.3, list 0 76.5, list 3 10.1, aura 5.5, reduction 4.3, glow 0.0, crack fill "
            "0.0, present 2.9, pre-pass 6.2; vertex invocations a picture: the pre-pass's 85k, "
            "the draws' 156k (3 a triangle; never summed); glow plans: none 2; robust access on, "
            "buffers (gpu_robust)",
            __LINE__);
    /* a dual-source device: no two-pass clause; no pre-pass: 9 a triangle */
    rd_perf_more_reset(&b);
    record(&r, 20.0);
    r.noDualPairs = r.noDualColour = 0;
    r.gpuValid = 0;
    r.glowLight = RD_GLOW_LIGHT_LAMPS;
    rd_perf_more_add(&b, &r, false);
    (void)rd_perf_more_cost_text(&b, t, sizeof t);
    CHECK(strncmp(t, "costs a picture: SCENE's pass ended mid-picture 3.0 times", 57) == 0);
    (void)rd_perf_more_compare_text(&b, "", false, NULL, t, sizeof t);
    text_is(t,
            "compare: no scene: 1 pictures (0 keep frames); a picture 52k triangles, 351 draws, "
            "21.0 passes, 3.0 breaks; no GPU times; vertex invocations a picture: the pre-pass's "
            "0, the draws' 468k (9 a triangle without the pre-pass; never summed); glow plans: "
            "lamps 1",
            __LINE__);
    /* a short buffer: cut and terminated */
    {
        char s[32];
        const size_t n = rd_perf_more_cost_text(&b, s, sizeof s);
        CHECK(n == strlen(s) && n == sizeof s - 1u);
        const size_t m = rd_perf_more_compare_text(&b, "x", false, "off", s, sizeof s);
        CHECK(m == strlen(s) && m == sizeof s - 1u);
    }
}

int main(void)
{
    test_breaks();
    test_box();
    test_lines();
    if (fails) {
        printf("rd_perf_more_test: %d failed\n", fails);
        return 1;
    }
    printf("rd_perf_more_test: ok\n");
    return 0;
}

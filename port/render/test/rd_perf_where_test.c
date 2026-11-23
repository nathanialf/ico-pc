/*
 * port/render/test/rd_perf_where_test.c
 *
 * Where the time went (rd_perf_where.h) from a made-up block of issue
 * 179's boat scene shape (ten records, two timed pass by pass): the six
 * lines word for word, a piece of 0 ms left out, a list not timed pass by
 * pass "[...]", an effect without a name, the window's texts absent, no
 * timed picture, the rate card's hook (not yet answering: no clause), a
 * short buffer cut and the count past it (CPU only).
 */
#include <stdio.h>
#include <string.h>

#include "rd_perf_where.h"

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

static const char *post_name(int p)
{
    return p == RD_PERF_POST_REDUCTION ? "reduction" : p == RD_PERF_POST_AURA ? "aura" : NULL;
}

static RdPerfRankBlock s_b;

int main(void)
{
    RdPerfRankBlock *b = &s_b;
    memset(b, 0, sizeof(*b));
    b->n = 10;
    b->gpuN = 10;
    b->gpuMs = 1053.0;
    b->workMs = 1039.0;
    b->listMs[0] = 490.0;
    b->listMs[3] = 150.0;
    b->listMs[12] = 24.0;
    b->prepassMs = 71.0;
    b->uploadGpuMs = 5.0;
    b->presentGpuMs = 27.0;
    b->postMs[RD_PERF_POST_REDUCTION] = 24.0;
    b->postMs[RD_PERF_POST_AURA] = 42.0;
    b->postMs[RD_PERF_POST_GLOW] = 3.0; /* no name: "effect 4" */
    b->timed = 2;
    b->cls[RD_PERF_RANK_LIST0].seen = true;
    b->cls[RD_PERF_RANK_LIST0].draws = 566.0;
    b->cls[RD_PERF_RANK_LIST0].colorLoadKB = 2048.0;
    b->cls[RD_PERF_RANK_LIST0].depthLoadKB = 409.6;
    b->cls[RD_PERF_RANK_LIST0].colorStoreKB = 6963.2;
    b->cls[RD_PERF_RANK_LIST0].kTexels = 700.0;
    b->ownMs = 201.0;
    b->walkMs = 80.0;
    b->uploadCpuMs = 30.0;
    b->bindMs = 20.0;
    b->interpMs = 10.0;
    b->submitMs = 61.0;
    b->presentMs = 12.0;
    b->fenceMs = 400.0;
    b->draws = 9000.0;
    b->vuTris = 701000.0;
    b->vuInv = 810000.0;
    b->colorLoad = 2.0 * 1048576.0 * 10.0;
    b->depthLoad = 1.1 * 1048576.0 * 10.0;
    b->colorStore = 6.2 * 1048576.0 * 10.0;
    b->texels = 12.0e6 * 10.0;

    RdPerfRankInfo in;
    memset(&in, 0, sizeof in);
    in.scene = "stage 42 play";
    in.presentedFps = 9.5;
    in.targetFps = 25.0;
    in.gameFps = 25.0;
    in.gameRateFps = 25.0;
    RdPerfRankVerdict v;
    memset(&v, 0, sizeof v);
    v.bound = RD_PERF_BOUND_GPU;
    v.workMs = 103.9;
    v.ownMs = 20.1;
    v.stepCpuMs = 8.0;
    v.periodMs = 105.3;
    v.tickMs = 40.0;

    const RdPerfWhereExtra x = {
        post_name, "ahead on; zero-area cull on (kept 1, without 0, trials 1)",
        "thermal 1/0.67/46.5/1200:1800,2000:1500; battery -812 mA", "12, most in list 0 (8)", NULL};
    static const char *const want[RD_PERF_WHERE_LINES] = {
        "where: stage 42 play: limit GPU-bound; 9.5 of 25.0 fps (the game 25.0 of 25.0); GPU work "
        "103.9 ms (99 % of the 105.3 ms between presents); render thread 20.1 ms (19 %); game "
        "step 8.0 ms (20 % of its 40.0 ms tick)",
        "where: GPU a picture 105.3 ms (work 103.9 ms): list 0 49.0 ms 47 % [283 draws, 1.2 MB "
        "loaded, 3.4 MB stored, 0.35 Mpx]; list 3 15.0 ms 14 % [...]; list 12 2.4 ms 2 % [...]; "
        "pre-pass 7.1 ms 7 %; before list 0 0.5 ms 0 %; present 2.7 ms 3 %; effects (inside "
        "their lists): reduction 2.4 ms 2 %, aura 4.2 ms 4 %, effect 4 0.3 ms 0 %; all: 70.1k "
        "triangles, 81.0k vertex invocations, 900 draws, 3.1 MB loaded, 6.2 MB stored, 12.0 Mpx",
        "where: CPU a picture: render thread 20.1 ms (walk 8.0, uploads 3.0, binds 2.0, "
        "interpolation 1.0, submit 6.1); submit thread 0.0 ms in 0 pictures; present calls 1.2 "
        "ms; GPU waits 40.0 ms; game step 8.0 ms",
        "where: attempts: ahead on; zero-area cull on (kept 1, without 0, trials 1)",
        "where: phone: thermal 1/0.67/46.5/1200:1800,2000:1500; battery -812 mA; stalls: 12, most "
        "in list 0 (8)",
        "where: largest: GPU list 0 49.0 ms (47 % of the GPU span); CPU walk 8.0 ms (8 % of the "
        "time between presents)"};
    char t[2048];
    for (unsigned i = 0; i < RD_PERF_WHERE_LINES; i++) {
        const int w = rd_perf_where_text(b, &in, &v, &x, i, t, sizeof t);
        CHECK(w == (int)strlen(t));
        CHECK(text_is(t, want[i]));
    }
    CHECK(rd_perf_where_text(b, &in, &v, &x, RD_PERF_WHERE_LINES, t, sizeof t) == 0);

    /* the window's texts absent; the game step the largest CPU piece */
    v.stepCpuMs = 30.0;
    (void)rd_perf_where_text(b, &in, &v, NULL, 3, t, sizeof t);
    CHECK(text_is(t, "where: attempts: not known"));
    (void)rd_perf_where_text(b, &in, &v, NULL, 4, t, sizeof t);
    CHECK(text_is(t, "where: phone: not read; stalls: none"));
    (void)rd_perf_where_text(b, &in, &v, NULL, 5, t, sizeof t);
    CHECK(strstr(t, "; CPU game step 30.0 ms (28 % of the time between presents)") != NULL);

    /* the rate card's hook: not answering yet, no clause */
    {
        const RdPerfWhereRates r = {0.5, 2.0, 4.0};
        const RdPerfWhereExtra xr = {post_name, NULL, NULL, NULL, &r};
        CHECK(rd_perf_where_explained_ms(b, &r) < 0.0);
        (void)rd_perf_where_text(b, &in, &v, &xr, 5, t, sizeof t);
        CHECK(strstr(t, "rate card") == NULL);
    }

    /* no picture timed */
    b->gpuN = 0;
    (void)rd_perf_where_text(b, &in, &v, &x, 1, t, sizeof t);
    CHECK(text_is(t, "where: GPU a picture 0.0 ms (work 0.0 ms): no picture timed"));
    (void)rd_perf_where_text(b, &in, &v, &x, 5, t, sizeof t);
    CHECK(strstr(t, "where: largest: GPU none timed; CPU game step") == t);

    /* a short buffer: cut, the count the whole line's */
    {
        char sh[16];
        const int w = rd_perf_where_text(b, &in, &v, &x, 0, sh, sizeof sh);
        CHECK(strlen(sh) == sizeof sh - 1u && w > (int)sizeof sh);
    }

    if (fails) {
        printf("rd_perf_where_test: %d failures\n", fails);
        return 1;
    }
    printf("rd_perf_where_test: ok\n");
    return 0;
}

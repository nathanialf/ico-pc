/*
 * port/render/test/rd_perf_rank_test.c
 *
 * The block's performance lines and the run's profile (rd_perf_rank.h)
 * from made-up records and pieces shaped like issue 179's stage 3 play on
 * the Mali-G52 (v0.11.2's log: 16.8 of 25 fps, about 59 ms of GPU work a
 * picture, list 3 22 ms, list 0 20 ms, the aura, the reduction, three DATE
 * snapshots, the present):
 *   gpu        the bound (GPU-bound: the work over 80 % of the time between
 *              presents), the scene, classes, rates, slowest and ranked
 *              lines (the ranked line word for word), the small passes' fit
 *              recovering its made-up constants, the DATE breaks' estimate;
 *   cpu        a render-thread-bound block ranks the CPU's phases; a game-
 *              thread-bound one its step; one at the game's rate has no
 *              bound;
 *   classes    a piece's class (a list, an effect, the shadow count, a kind);
 *   run        the profile by scene: blocks, the median and worst picture,
 *              the median and lowest rate, the bounds, the worst block's
 *              ranking kept, a full table counted;
 *   short      a small buffer is cut and terminated.
 */
#include <stdio.h>
#include <string.h>

#include "rd_perf_rank.h"

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)
#define CHECK_STR(got, want)                                                                       \
    do {                                                                                           \
        if (strcmp((got), (want)) != 0) {                                                          \
            printf("FAIL %s:%d:\n got  \"%s\"\n want \"%s\"\n", __FILE__, __LINE__, (got),         \
                   (want));                                                                        \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)
#define CHECK_IN(text, part)                                                                       \
    do {                                                                                           \
        if (strstr((text), (part)) == NULL) {                                                      \
            printf("FAIL %s:%d: \"%s\" not in\n \"%s\"\n", __FILE__, __LINE__, (part), (text));    \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static int near(double a, double b, double eps)
{
    return a - b < eps && b - a < eps;
}

static int label(const RdPerfPass *p, char *buf, size_t n)
{
    if (p->kind == RD_PERF_PASS_RENDER && p->post == RD_PERF_POST_AURA) {
        return snprintf(buf, n, "aura");
    }
    if (p->kind == RD_PERF_PASS_RENDER && p->post == RD_PERF_POST_REDUCTION) {
        return snprintf(buf, n, "reduction");
    }
    if (p->kind == RD_PERF_PASS_RENDER) {
        return snprintf(buf, n, "list %d", (int)p->list);
    }
    return snprintf(buf, n, "kind %d", (int)p->kind);
}

/* a record of the stage 3 shape: GPU span 60 ms (display wait 1), its
   CPU's phases, 20 passes, the bytes the passes asked */
static RdPerfRecord record(uint32_t replay, double gpuMs)
{
    RdPerfRecord r;
    memset(&r, 0, sizeof(r));
    r.replay = replay;
    r.frame = replay + 100u;
    r.presented = 1;
    r.gpuValid = 1;
    r.gpuMs = gpuMs;
    r.gpuDisplayWaitMs = 1.0;
    r.gpuUploadMs = 0.3;
    r.gpuPrepassMs = 4.2;
    r.gpuPresentMs = 3.0;
    r.gpuListMs[0] = 14.7;
    r.gpuListMs[3] = 27.7;
    r.gpuPostMs[RD_PERF_POST_AURA] = 5.5;
    r.totalMs = 39.6;
    r.walkMs = 9.2;
    r.submitMs = 1.7;
    r.uploadMs = 2.0;
    r.bindMs = 0.4;
    r.presentMs = 25.8;
    r.fenceWaitMs = 27.5;
    r.draws = 360;
    r.pipelineBinds = 329;
    r.bindGroupBinds = 458;
    r.renderPasses = 20;
    r.dateSnapshots = 3;
    r.uploadBytes = 1826u * 1024u;
    r.textureUploads = 1;
    r.colorLoadBytes = 8u << 20;
    r.colorStoreBytes = 9u << 20;
    r.depthLoadBytes = 4u << 20;
    r.depthStoreBytes = 2u << 20;
    r.clearBytes = 3u << 20;
    r.passTexels = 6000000u;
    r.copyBytes = 1u << 20;
    r.passUntracked = 1; /* the present's window image */
    r.gpuSlotWaitMs = 27.0;
    r.gpuDoneMs = 61.0;
    r.gpuDoneValid = 1;
    return r;
}

static RdPerfPass piece(int kind, int list, int post, uint32_t draws, double ms, uint32_t loadKB,
                        uint32_t storeKB)
{
    RdPerfPass p;
    memset(&p, 0, sizeof(p));
    p.kind = (uint8_t)kind;
    p.list = (int8_t)list;
    p.post = (int8_t)post;
    p.target = 0;
    p.w = 912;
    p.h = 512;
    p.draws = draws;
    p.pipelineBinds = draws;
    p.gpuUs = (float)(ms * 1000.0);
    p.colorLoadKB = loadKB / 2u;
    p.depthLoadKB = loadKB - loadKB / 2u;
    p.colorStoreKB = storeKB;
    p.kTexels = 467;
    return p;
}

/* the pieces of one timed replay: k varies the small passes' bytes so the
   fit has a spread (0.10 ms a pass + 0.40 ms a MB moved, exactly) */
static uint32_t pieces(RdPerfPass *p, uint32_t k)
{
    uint32_t n = 0;
    p[n++] = piece(RD_PERF_PASS_UPLOAD, -1, -1, 0, 0.3, 0, 0);
    p[n++] = piece(RD_PERF_PASS_PREPASS, -1, -1, 11, 4.2, 0, 0);
    p[n++] = piece(RD_PERF_PASS_RENDER, 0, -1, 283, 20.1, 3686, 3686);
    p[n++] = piece(RD_PERF_PASS_RENDER, 3, -1, 61, 22.4, 3686, 3686);
    for (uint32_t d = 0; d < 3; d++) {
        /* a DATE snapshot: 1 draw, (k + d) MB moved */
        const uint32_t kb = 1024u * (k % 5u + d);
        p[n++] = piece(RD_PERF_PASS_DATE, 3, -1, 1, 0.10 + 0.40 * (double)kb / 1024.0, kb, 0);
    }
    for (uint32_t a = 0; a < 4; a++) {
        const uint32_t kb = 512u * (a + k % 3u);
        p[n++] = piece(RD_PERF_PASS_RENDER, 8, RD_PERF_POST_AURA, 1,
                       0.10 + 0.40 * (double)kb / 1024.0, kb / 2u, kb - kb / 2u);
    }
    p[n++] = piece(RD_PERF_PASS_RENDER, 12, RD_PERF_POST_REDUCTION, 1, 3.4, 0, 912);
    RdPerfPass c = piece(RD_PERF_PASS_COPY, 3, -1, 0, 0.5, 0, 0);
    c.copyKB = 2048;
    p[n++] = c;
    p[n++] = piece(RD_PERF_PASS_PRESENT, -1, -1, 5, 3.0, 0, 4534);
    return n;
}

static RdPerfRankInfo info(const char *scene, double presented)
{
    const RdPerfRankInfo in = {scene, presented, 25.0, 25.0, 25.0, 2.9, true};
    return in;
}

static void fillGpu(RdPerfRankBlock *b)
{
    RdPerfPass p[32];
    rd_perf_rank_block_reset(b);
    for (uint32_t i = 0; i < 168; i++) {
        /* GPU span 57..63 ms; vertex invocations rise with it */
        const RdPerfRecord r = record(1000u + i, 57.0 + (double)(i % 7u));
        rd_perf_rank_add_record(b, &r, 40000.0 + 2000.0 * (double)(i % 7u));
        if (i % 14u == 0u) {
            const uint32_t n = pieces(p, i / 14u);
            rd_perf_rank_add_pieces(b, &r, p, n);
        }
    }
}

static void testGpu(void)
{
    static RdPerfRankBlock b;
    fillGpu(&b);
    CHECK(b.n == 168 && b.gpuN == 168 && b.timed == 12);

    RdPerfRankRates rt;
    rd_perf_rank_rates(&b, &rt);
    CHECK(rt.smallFit && near(rt.passMs, 0.10, 1e-3) && near(rt.mbMs, 0.40, 1e-3));
    CHECK(rt.smallR2 > 0.999 && rt.smallN == 12u * 3u);
    CHECK(rt.copyRate && near(rt.copyMbMs, 0.25, 1e-6));
    CHECK(rt.vuFit && near(rt.vuPerK, 0.5, 1e-6) && near(rt.vuBaseMs, 36.0, 1e-6));
    CHECK(near(rt.mbRate, 0.40, 1e-3) && strcmp(rt.mbFrom, "the byte-bound passes' fit") == 0);

    const RdPerfRankInfo in = info("stage 3 play", 16.8);
    RdPerfRankVerdict v;
    rd_perf_rank_verdict(&b, &in, &v);
    CHECK(v.below && v.bound == RD_PERF_BOUND_GPU);
    CHECK(near(v.workMs, 59.0, 1e-9) && near(v.periodMs, 1000.0 / 16.8, 1e-9));
    CHECK(v.count >= 3 && v.c[0].ms >= v.c[1].ms && v.c[1].ms >= v.c[2].ms);
    char line[4096];

    rd_perf_rank_ranked_text(&in, &v, line, sizeof line);
    printf("%s\n", line);
    CHECK_STR(line,
              "perf ranked: stage 3 play: 16.8 of 25.0 fps (the game 25.0 of 25.0), GPU-bound "
              "(the GPU's work 59.0 ms a picture of the 59.5 ms between presents): 1. vertex work "
              "est. 23.0 ms (46k vertex invocations a picture at 0.500 ms a thousand, R^2 1.00); "
              "2. list 3 22.4 ms measured (1.0 passes, 61 draws, 61 binds; loads 3.6 MB, stores "
              "3.6 MB, 0.47 Mpx); 3. list 0 20.1 ms measured (1.0 passes, 283 draws, 283 binds; "
              "loads 3.6 MB, stores 3.6 MB, 0.47 Mpx) (each saving alone, they overlap; est. = "
              "this block's counters times its own rates)");

    /* the other candidates are there too, with their figures */
    int date = -1, loads = -1, stores = -1, passes = -1;
    for (uint32_t i = 0; i < v.count; i++) {
        if (strncmp(v.c[i].text, "DATE breaks", 11) == 0) {
            date = (int)i;
        } else if (strncmp(v.c[i].text, "tile loads", 10) == 0) {
            loads = (int)i;
        } else if (strncmp(v.c[i].text, "depth stores", 12) == 0) {
            stores = (int)i;
        } else if (strncmp(v.c[i].text, "pass count", 10) == 0) {
            passes = (int)i;
        }
    }
    CHECK(date >= 0 && loads >= 0 && stores >= 0 && passes >= 0);
    if (loads >= 0) {
        /* 12 MB loaded a picture at 0.40 ms a MB */
        CHECK(near(v.c[loads].ms, 4.8, 1e-2) && v.c[loads].estimate);
        CHECK_IN(v.c[loads].text, "(12.0 MB loaded a picture by 20.0 passes, at 0.40 ms a MB, the "
                                  "byte-bound passes' fit)");
    }
    if (stores >= 0) {
        CHECK(near(v.c[stores].ms, 0.8, 1e-2));
    }
    if (passes >= 0) {
        CHECK(near(v.c[passes].ms, 2.0, 1e-2));
    }
    if (date >= 0) {
        /* the snapshots' measured time, and 3 reloads of a list piece's 3.6 MB */
        CHECK_IN(v.c[date].text, "(3.0 a picture:");
        CHECK_IN(v.c[date].text, "MB reloaded after them)");
    }

    rd_perf_rank_scene_text(&b, &in, &v, line, sizeof line);
    printf("%s\n", line);
    CHECK_IN(line, "perf scene: stage 3 play, 168 pictures (0 keep frames): 16.8 of 25.0 fps (the "
                   "game 25.0 of 25.0), GPU-bound; the GPU's work 59.0 ms a picture of the 59.5 ms "
                   "between presents (99 %)");
    CHECK_IN(line, "the render thread's own CPU 13.3 ms (walk 9.2, submit 1.7, uploads 2.0, binds "
                   "0.4, interpolation 0.0), its waits for the GPU 27.5 ms");
    CHECK_IN(line, "a picture's work done 61.0 ms after its last submit (longest 61.0), the slot "
                   "wait blocked 168 times (27.0 ms on average, longest 27.0)");
    CHECK_IN(
        line,
        "colour 8.0 MB loaded 9.0 MB stored, depth and stencil 4.0 MB loaded 2.0 MB "
        "stored, 3.0 MB cleared, 6.00 Mpx of area, copies 1.0 MB (uncompressed; 1.0 passes with an "
        "attachment of no known format, at 4 bytes a texel)");
    CHECK_IN(line, "the pictures' GPU work median 59.5 ms, worst 63 ms");

    rd_perf_rank_classes_text(&b, label, 4, line, sizeof line);
    printf("%s\n", line);
    CHECK_IN(line, "perf classes a picture (12 timed replays): list 3 22.40 ms: 1.0 passes, 61 "
                   "draws, 61 binds, colour 1.8/3.6 MB, depth 1.8/0.0 MB loaded/stored");
    CHECK_IN(line, "; list 0 20.10 ms");
    CHECK_IN(line, " (4 of 9 classes; all ");

    rd_perf_rank_rates_text(&b, &rt, line, sizeof line);
    printf("%s\n", line);
    CHECK_IN(
        line,
        "the DATE snapshots and passes with no draw 0.10 ms a pass + 0.40 ms a MB loaded "
        "or stored (R^2 1.00, 36 pieces); copies 0.25 ms a MB (24.0 MB in 6.0 ms); the GPU's work 36.0 "
        "ms + 0.500 ms a thousand vertex invocations (R^2 1.00, 168 pictures); the "
        "estimates use the byte-bound passes' fit (0.40 ms a MB)");

    rd_perf_rank_slowest_text(&b, label, 3, line, sizeof line);
    printf("%s\n", line);
    /* the timed replays are every 14th (57 ms each): the first is kept */
    CHECK_IN(line, "perf slowest picture: replay 1000 (frame 1100), GPU span 57.0 ms over 14 "
                   "pieces: list 3 912x512 22.40 ms (61 draws, 61 binds; loads 3.6 MB, stores 3.6 "
                   "MB), list 0 912x512 20.10 ms");
    CHECK_IN(line, "(the top 3 by GPU time; GPU time by pipeline is not measured)");
}

static void testCpu(void)
{
    static RdPerfRankBlock b;
    rd_perf_rank_block_reset(&b);
    for (uint32_t i = 0; i < 100; i++) {
        RdPerfRecord r = record(i + 1u, 20.0);
        r.walkMs = 40.0;
        rd_perf_rank_add_record(&b, &r, 1000.0);
    }
    RdPerfRankInfo in = info("stage 5 play", 20.0);
    RdPerfRankVerdict v;
    rd_perf_rank_verdict(&b, &in, &v);
    CHECK(v.bound == RD_PERF_BOUND_RENDER && v.below);
    CHECK(v.count > 0 && strncmp(v.c[0].text, "the walk 40.0 ms measured (360 draws", 36) == 0);
    char line[2048];
    rd_perf_rank_ranked_text(&in, &v, line, sizeof line);
    CHECK_IN(line, "render-thread-bound (its own CPU 44.1 ms a picture of the 50.0 ms between "
                   "presents): 1. the walk 40.0 ms measured");

    /* the game's step over 80 % of its tick, the rest light */
    rd_perf_rank_block_reset(&b);
    for (uint32_t i = 0; i < 100; i++) {
        RdPerfRecord r = record(i + 1u, 10.0);
        r.walkMs = 1.0;
        rd_perf_rank_add_record(&b, &r, 1000.0);
    }
    in = info("stage 5 scene", 20.0);
    in.gameFps = 20.0;
    in.stepCpuMs = 38.0;
    rd_perf_rank_verdict(&b, &in, &v);
    CHECK(v.bound == RD_PERF_BOUND_GAME);
    CHECK(v.count == 1 && strcmp(v.c[0].text, "the game step's CPU 38.0 ms measured of its 40.0 ms "
                                              "tick") == 0);

    /* at the game's rate: no bound */
    in = info("title", 25.0);
    rd_perf_rank_verdict(&b, &in, &v);
    CHECK(!v.below && v.bound == RD_PERF_BOUND_NONE);
    /* no presents: not measured */
    in = info("title", 0.0);
    rd_perf_rank_verdict(&b, &in, &v);
    CHECK(v.bound == RD_PERF_BOUND_UNKNOWN);
}

static void testClasses(void)
{
    RdPerfPass p;
    memset(&p, 0, sizeof(p));
    p.kind = RD_PERF_PASS_RENDER;
    p.list = 4;
    p.post = -1;
    CHECK(rd_perf_rank_class_of(&p) == RD_PERF_RANK_LIST0 + 4);
    p.post = RD_PERF_POST_GLOW;
    CHECK(rd_perf_rank_class_of(&p) == RD_PERF_RANK_POST0 + RD_PERF_POST_GLOW);
    p.post = -1;
    p.target = RD_PERF_TARGET_SHADOW_COUNT;
    CHECK(rd_perf_rank_class_of(&p) == RD_PERF_RANK_SHADOW);
    p.kind = RD_PERF_PASS_DATE;
    CHECK(rd_perf_rank_class_of(&p) == RD_PERF_RANK_KIND0 + RD_PERF_PASS_DATE);
}

static void testRun(void)
{
    static RdPerfRankRun run;
    static RdPerfRankBlock b;
    memset(&run, 0, sizeof(run));
    fillGpu(&b);
    char ranked[2048], line[4096];
    const double fps[3] = {16.8, 16.5, 16.9};
    for (int k = 0; k < 3; k++) {
        const RdPerfRankInfo in = info("stage 3 play", fps[k]);
        RdPerfRankVerdict v;
        rd_perf_rank_verdict(&b, &in, &v);
        rd_perf_rank_ranked_text(&in, &v, ranked, sizeof ranked);
        rd_perf_rank_run_add(&run, &b, &in, &v, 10.0, ranked);
    }
    {
        const RdPerfRankInfo in = info("title", 25.0);
        RdPerfRankVerdict v;
        rd_perf_rank_verdict(&b, &in, &v);
        rd_perf_rank_run_add(&run, &b, &in, &v, 10.0, NULL);
    }
    CHECK(run.count == 2 && run.s[0].blocks == 3 && run.s[0].pictures == 504);
    rd_perf_rank_profile_text(&run, 0, line, sizeof line);
    printf("%s\n", line);
    CHECK_IN(line, "performance profile: stage 3 play: 3 blocks (30 s, 504 pictures); a picture's "
                   "GPU work median 59.5 ms, worst 63 ms; presented fps median 16.8, lowest block "
                   "16.5, of 25.0; GPU-bound in 3 blocks; the worst block: 16.5 of 25.0 fps");
    rd_perf_rank_profile_text(&run, 1, line, sizeof line);
    CHECK_IN(line, "performance profile: title: 1 blocks");
    CHECK_IN(line, "at the game's rate in 1 blocks");
    CHECK(strstr(line, "the worst block") == NULL);
    CHECK(rd_perf_rank_profile_text(&run, 2, line, sizeof line) == 0 && line[0] == '\0');

    /* a full table: later scenes counted, named on the last line */
    for (int k = 0; k < RD_PERF_RANK_SCENES + 3; k++) {
        char name[32];
        snprintf(name, sizeof name, "stage %d play", 10 + k);
        const RdPerfRankInfo in = info(name, 25.0);
        RdPerfRankVerdict v;
        rd_perf_rank_verdict(&b, &in, &v);
        rd_perf_rank_run_add(&run, &b, &in, &v, 10.0, NULL);
    }
    CHECK(run.count == RD_PERF_RANK_SCENES && run.overflow == 5);
    rd_perf_rank_profile_text(&run, RD_PERF_RANK_SCENES - 1, line, sizeof line);
    CHECK_IN(line, "; 5 blocks of further scenes not kept");

    /* the histogram's median and worst */
    uint32_t h[RD_PERF_RANK_BINS];
    memset(h, 0, sizeof h);
    CHECK(rd_perf_rank_hist_median(h) == 0.0 && rd_perf_rank_hist_worst(h) == 0.0);
    h[10] = 3;
    h[40] = 1;
    h[RD_PERF_RANK_BINS - 1] = 1;
    CHECK(near(rd_perf_rank_hist_median(h), 10.5, 1e-9));
    CHECK(near(rd_perf_rank_hist_worst(h), (double)RD_PERF_RANK_BINS, 1e-9));
}

static void testShort(void)
{
    static RdPerfRankBlock b;
    fillGpu(&b);
    const RdPerfRankInfo in = info("stage 3 play", 16.8);
    RdPerfRankVerdict v;
    rd_perf_rank_verdict(&b, &in, &v);
    char small[24];
    memset(small, 'x', sizeof small);
    const int w = rd_perf_rank_scene_text(&b, &in, &v, small, sizeof small);
    CHECK(w > (int)sizeof small && strlen(small) == sizeof small - 1);
    CHECK(strncmp(small, "perf scene: stage 3 pla", sizeof small - 1) == 0);
    memset(small, 'x', sizeof small);
    (void)rd_perf_rank_ranked_text(&in, &v, small, sizeof small);
    CHECK(strlen(small) == sizeof small - 1);
}

int main(void)
{
    testGpu();
    testCpu();
    testClasses();
    testRun();
    testShort();
    printf("rd_perf_rank_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}

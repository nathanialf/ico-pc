/*
 * port/render/test/rd_perf_where_test.c
 *
 * Where the time went (rd_perf_where.h) from a made-up block of issue
 * 179's boat scene shape (ten records, two timed pass by pass): the seven
 * lines word for word, a piece of 0 ms left out, a list not timed pass by
 * pass "[...]", an effect without a name, the window's texts absent, no
 * timed picture, a short buffer cut and the count past it; the rate card's
 * bounds (never summed; "at least" only under a bound below the measured
 * time) with the hand-computed numbers of the reporter's v0.11.15 card
 * (Mali-G52, log :339-342), and "no rate card figure" for each way the card
 * has none: no card, another graphics API, an item not measured, at or
 * below 0, in another unit, timed with the other pre-pass state, an empty
 * pass of another size, a list not timed pass by pass (CPU only).
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "rd_perf_where.h"
#include "rd_ratecard.h"

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
static RdPerfRankBlock s_b2;
static RdRateCard s_card;

static void cardItem(int i, const char *label, const char *unit, double med)
{
    snprintf(s_card.item[i].label, sizeof s_card.item[i].label, "%s", label);
    snprintf(s_card.item[i].unit, sizeof s_card.item[i].unit, "%s", unit);
    s_card.item[i].u.ok = true;
    s_card.item[i].u.med = s_card.item[i].u.lo = s_card.item[i].u.hi = med;
}

/* the reporter's v0.11.15 card's three items the clause reads */
static void cardMali(void)
{
    memset(&s_card, 0, sizeof s_card);
    s_card.have = true;
    snprintf(s_card.device, sizeof s_card.device, "Mali-G52 MC2 (Mali-G52 MC2 32.1.0, Vulkan)");
    s_card.w = s_card.h = s_card.tw = s_card.th = 512;
    cardItem(RD_RC_VERTEX, "a vertex (a corner drawn off the target, the VU pre-pass on)", "ns",
             118.29);
    cardItem(RD_RC_TRIANGLE,
             "a triangle binned (2048 in 2x2 GS pixel cells against 2, the same 64x64 square)",
             "ns", 541.76);
    cardItem(RD_RC_PASS, "a pass with nothing drawn (SCENE cleared, a replay's span)", "ms", 0.003);
}

static int near(double a, double b)
{
    return fabs(a - b) < 1e-9;
}

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
    b->cls[RD_PERF_RANK_LIST0].pieces = 2.0; /* one pass a timed picture */
    b->cls[RD_PERF_RANK_LIST0].first.target = RD_TARGET_SCENE;
    b->cls[RD_PERF_RANK_LIST0].first.w = 512;
    b->cls[RD_PERF_RANK_LIST0].first.h = 512;
    /* a picture: list 0 60.1k VU triangles, 31.0k drawn again; list 3
       10.0k and 2.1k of the shadow volumes'; list 12 none */
    b->listTris[0] = 601000.0;
    b->listAgainTris[0] = 310000.0;
    b->listTris[3] = 100000.0;
    b->listShadowTris[3] = 21000.0;
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
    in.prepass = true;
    RdPerfRankVerdict v;
    memset(&v, 0, sizeof v);
    v.bound = RD_PERF_BOUND_GPU;
    v.workMs = 103.9;
    v.ownMs = 20.1;
    v.stepCpuMs = 8.0;
    v.periodMs = 105.3;
    v.tickMs = 40.0;

    cardMali();
    RdPerfWhereRates rates;
    rd_perf_where_rates_of(&s_card, "Vulkan", &rates);
    CHECK(rates.have && rates.vertex.ok && rates.triangle.ok && rates.pass.ok);
    CHECK(rates.prepass == 1 && rates.tw == 512u && rates.th == 512u);
    const RdPerfWhereExtra x = {post_name,
                                "ahead on; zero-area cull on (kept 1, without 0, trials 1)",
                                "thermal 1/0.67/46.5/1200:1800,2000:1500; battery -812 mA",
                                "12, most in list 0 (8)", &rates};
    static const char *const want[RD_PERF_WHERE_LINES] = {
        "where: stage 42 play: limit GPU-bound; 9.5 of 25.0 fps (the game 25.0 of 25.0); GPU work "
        "103.9 ms (99 % of the 105.3 ms between presents); render thread 20.1 ms (19 %); game "
        "step 8.0 ms (20 % of its 40.0 ms tick)",
        "where: GPU a picture 105.3 ms (work 103.9 ms): list 0 49.0 ms 47 % [283 draws, 1.2 MB "
        "loaded, 3.4 MB stored, 0.35 Mpx]; list 3 15.0 ms 14 % [...]; list 12 2.4 ms 2 % [...]; "
        "pre-pass 7.1 ms 7 %; before list 0 0.5 ms 0 %; present 2.7 ms 3 %; effects (inside "
        "their lists): reduction 2.4 ms 2 %, aura 4.2 ms 4 %, effect 4 0.3 ms 0 %; all: 70.1k "
        "triangles, 81.0k vertex invocations, 900 draws, 3.1 MB loaded, 6.2 MB stored, 12.0 Mpx, "
        "31.0k VU triangles drawn again by a later pass, 2.1k shadow volume triangles",
        "where: CPU a picture: render thread 20.1 ms (walk 8.0, uploads 3.0, binds 2.0, "
        "interpolation 1.0, submit 6.1); submit thread 0.0 ms in 0 pictures; present calls 1.2 "
        "ms; GPU waits 40.0 ms; game step 8.0 ms",
        "where: attempts: ahead on; zero-area cull on (kept 1, without 0, trials 1)",
        "where: phone: thermal 1/0.67/46.5/1200:1800,2000:1500; battery -812 mA; stalls: 12, most "
        "in list 0 (8)",
        "where: largest: GPU list 0 49.0 ms (47 % of the GPU span); CPU walk 8.0 ms (8 % of the "
        "time between presents)",
        /* list 0: 91.1k triangles x 541.76 ns = 49.35 ms (above its 49.0 ms
           measured: nothing said of the rest); 3 x (60.1k + 31.0k) = 273.3k
           corners x 118.29 ns = 32.33 ms; 2 passes / 2 timed x 0.003 ms.
           list 3: 12.1k triangles = 6.56 ms, under its 15.0 ms: at least
           8.44 ms is something else; 30.0k corners = 3.55 ms; its passes
           not timed.  Never summed */
        "where: explained by the rate card, upper bounds, not a split (fragments, loads and stores "
        "not counted): list 0 49.0 ms measured: its triangles at most 49.4 ms (91.1k x 541.76 ns, "
        "each with its three corners and one 2x2 quad of fragments; 31.0k of them drawn again by a "
        "later pass, 0.0k the shadow volumes'), its corners alone at most 32.3 ms (273.3k x 118.29 "
        "ns: the card's corner also holds one pre-pass vertex), passes 0.0 ms (1.0 x 0.003 ms, an "
        "empty SCENE pass each); list 3 15.0 ms measured: its triangles at most 6.6 ms (12.1k x "
        "541.76 ns, each with its three corners and one 2x2 quad of fragments; 0.0k of them drawn "
        "again by a later pass, 2.1k the shadow volumes'), its corners alone at most 3.5 ms (30.0k "
        "x 118.29 ns: the card's corner also holds one pre-pass vertex), passes: no rate card "
        "figure (the list not timed pass by pass), so at least 8.4 ms of it is not vertices or "
        "triangles by the card: fragments, loads and stores, or a stall"};
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

    /* the clause's terms, unrounded */
    {
        RdPerfWhereExplained e;
        CHECK(rd_perf_where_explained(b, &in, &rates, 0, &e));
        CHECK(near(e.corners, 273300.0) && near(e.tris, 60100.0) && near(e.again, 31000.0));
        CHECK(near(e.vertexMs, 273300.0 * 118.29e-6) && e.bound);
        CHECK(near(e.triMs, 91100.0 * 541.76e-6));
        CHECK(near(e.passes, 1.0) && near(e.passMs, 0.003));
        CHECK(e.gapMs < 0.0); /* the bound above the measured time */
        CHECK(near(e.ms, 49.0));
        CHECK(rd_perf_where_explained(b, &in, &rates, 3, &e));
        CHECK(near(e.shadow, 2100.0) && e.passMs < 0.0);
        CHECK(near(e.gapMs, 15.0 - 12100.0 * 541.76e-6));
        CHECK(!rd_perf_where_explained(b, &in, &rates, 12, &e)); /* no triangles */
        CHECK(!rd_perf_where_explained(b, &in, NULL, 0, &e));
    }

    /* no rate card figure, each way */
    {
        static const char head[] =
            "where: explained by the rate card, upper bounds, not a split (fragments, loads and "
            "stores not counted): ";
        char want[512];
        RdPerfWhereRates r;
        RdPerfWhereExtra xr = x;
        xr.rates = &r;
        /* no card this run */
        rd_perf_where_rates_of(NULL, "Vulkan", &r);
        (void)rd_perf_where_text(b, &in, &v, &xr, 6, t, sizeof t);
        snprintf(want, sizeof want,
                 "%sno rate card figure (no rate card this run: its start "
                 "lines say why)",
                 head);
        CHECK(text_is(t, want));
        /* none given (the hook's caller passed none) */
        xr.rates = NULL;
        (void)rd_perf_where_text(b, &in, &v, &xr, 6, t, sizeof t);
        snprintf(want, sizeof want, "%sno rate card figure (no rate card given)", head);
        CHECK(text_is(t, want));
        xr.rates = &r;
        /* a card of another graphics API */
        rd_perf_where_rates_of(&s_card, "OpenGL", &r);
        CHECK(!r.have);
        (void)rd_perf_where_text(b, &in, &v, &xr, 6, t, sizeof t);
        snprintf(want, sizeof want,
                 "%sno rate card figure (the card is not of this block's "
                 "graphics API, OpenGL)",
                 head);
        CHECK(text_is(t, want));
        /* the vertex not measured, the triangle at or below 0 (v0.11.15's
           hidden-through-twin figure), the pass in another unit */
        s_card.item[RD_RC_VERTEX].u.ok = false;
        snprintf(s_card.item[RD_RC_VERTEX].u.why, sizeof s_card.item[RD_RC_VERTEX].u.why,
                 "5 and 4 timed replays, 5 wanted");
        s_card.item[RD_RC_TRIANGLE].u.med = -0.94;
        snprintf(s_card.item[RD_RC_PASS].unit, sizeof s_card.item[RD_RC_PASS].unit, "x");
        rd_perf_where_rates_of(&s_card, "Vulkan", &r);
        CHECK(r.have && !r.vertex.ok && !r.triangle.ok && !r.pass.ok);
        (void)rd_perf_where_text(b, &in, &v, &xr, 6, t, sizeof t);
        CHECK(strstr(t, "list 0 49.0 ms measured: its triangles: no rate card figure (the card's "
                        "figure -0.94 ns is not above 0) (31.0k drawn again by a later pass, 0.0k "
                        "the shadow volumes'), its corners alone: no rate card figure (not "
                        "measured: 5 and 4 timed replays, 5 wanted), passes: no rate card figure "
                        "(the card's unit is \"x\", ms wanted); list 3") != NULL);
        CHECK(strstr(t, "at most") == NULL && strstr(t, "at least") == NULL);
        CHECK(strstr(t, " ms:") == NULL); /* never a number */
        /* the card's pre-pass not the block's; the empty pass of another size */
        cardMali();
        rd_perf_where_rates_of(&s_card, "Vulkan", &r);
        in.prepass = false;
        b->cls[RD_PERF_RANK_LIST0].first.w = 1024;
        (void)rd_perf_where_text(b, &in, &v, &xr, 6, t, sizeof t);
        CHECK(strstr(t, "list 0 49.0 ms measured: its triangles: no rate card figure (the card "
                        "timed it with the VU pre-pass on, the block drew with it off) (31.0k "
                        "drawn again by a later pass, 0.0k the shadow volumes'), its corners "
                        "alone: no rate card figure (the card timed it with the VU pre-pass on, "
                        "the block drew with it off), passes: no rate card figure (the card's "
                        "empty pass is SCENE at 512x512 texels, the list's first pass SCENE at "
                        "1024x512); list 3") != NULL);
        CHECK(strstr(t, " ms (") == NULL); /* never a number */
        /* both without the pre-pass: "about", no bound and so no "at least" */
        snprintf(s_card.item[RD_RC_VERTEX].label, sizeof s_card.item[RD_RC_VERTEX].label,
                 "a vertex (a corner drawn off the target, the VU pre-pass off)");
        rd_perf_where_rates_of(&s_card, "Vulkan", &r);
        CHECK(r.prepass == 0);
        (void)rd_perf_where_text(b, &in, &v, &xr, 6, t, sizeof t);
        CHECK(strstr(t, "list 0 49.0 ms measured: its triangles about 49.4 ms (91.1k x 541.76 "
                        "ns, each with its three corners and one 2x2 quad of fragments; 31.0k of "
                        "them drawn again by a later pass, 0.0k the shadow volumes'), its corners "
                        "alone about 32.3 ms (273.3k x 118.29 ns), passes: ") != NULL);
        CHECK(strstr(t, "at most") == NULL && strstr(t, "at least") == NULL);
        in.prepass = true;
        b->cls[RD_PERF_RANK_LIST0].first.w = 512;
        /* no triangles drawn */
        RdPerfRankBlock *nb = &s_b2;
        *nb = *b;
        memset(nb->listTris, 0, sizeof nb->listTris);
        memset(nb->listAgainTris, 0, sizeof nb->listAgainTris);
        memset(nb->listShadowTris, 0, sizeof nb->listShadowTris);
        cardMali();
        rd_perf_where_rates_of(&s_card, "Vulkan", &r);
        (void)rd_perf_where_text(nb, &in, &v, &xr, 6, t, sizeof t);
        snprintf(want, sizeof want, "%sno VU or shadow volume triangles drawn", head);
        CHECK(text_is(t, want));
    }

    /* no picture timed */
    b->gpuN = 0;
    (void)rd_perf_where_text(b, &in, &v, &x, 1, t, sizeof t);
    CHECK(text_is(t, "where: GPU a picture 0.0 ms (work 0.0 ms): no picture timed"));
    (void)rd_perf_where_text(b, &in, &v, &x, 5, t, sizeof t);
    CHECK(strstr(t, "where: largest: GPU none timed; CPU game step") == t);
    (void)rd_perf_where_text(b, &in, &v, &x, 6, t, sizeof t);
    CHECK(text_is(t, "where: explained by the rate card, upper bounds, not a split (fragments, "
                     "loads and stores not counted): no picture timed"));

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

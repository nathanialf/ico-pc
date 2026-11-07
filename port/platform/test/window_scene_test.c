/*
 * port/platform/test/window_scene_test.c
 *
 * The window's scene lines (window_scene.h, issue 179): the layout share
 * over a sequence of layout ids and step times (kinds, the movie counted as
 * a scene, the order and the empty block, a reset, a cut buffer), the slow
 * step's game threads part (the longest switch and the core, each part
 * left out when not known) and the VU work of a replay (pre-pass off and
 * on, the count words, no replays); the list-0 line over the block's timed
 * replays (an exact line, a noisy one with its R^2, two replays, the
 * triangles not varying, list 0 not varying, none, a cut buffer) and the
 * swapchain's images in use; the slow-scene dump's trigger over a block's
 * figures (each condition alone, the bound exactly, the display wait less
 * from the work) and its line.
 */
#include <stdio.h>
#include <string.h>
#include "window_scene.h"

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
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

int main(void)
{
    /* the European disc's ids (region_pal.c) */
    const SceneIds ids = {54, 55, 12, 13, 57, 58, 59};
    SceneShare s;
    char b[256];

    CHECK(window_scene_kind(&ids, 54, 0) == SCENE_PLAY);
    CHECK(window_scene_kind(&ids, 55, 0) == SCENE_SCENE);
    CHECK(window_scene_kind(&ids, 54, 1) == SCENE_SCENE); /* a movie over any layout */
    CHECK(window_scene_kind(&ids, 12, 0) == SCENE_TITLE);
    CHECK(window_scene_kind(&ids, 13, 0) == SCENE_TITLE);
    CHECK(window_scene_kind(&ids, 57, 0) == SCENE_PAUSE);
    CHECK(window_scene_kind(&ids, 58, 0) == SCENE_SETTINGS);
    CHECK(window_scene_kind(&ids, 59, 0) == SCENE_SETTINGS);
    CHECK(window_scene_kind(&ids, 14, 0) == SCENE_OTHER);
    CHECK(window_scene_kind(&ids, -1, 0) == SCENE_OTHER);

    /* a block: play, a scene, play again, a movie, the pause menu */
    memset(&s, 0, sizeof(s));
    window_scene_text(&s, b, sizeof(b));
    CHECK(text_is(b, "layout: no steps"));
    {
        static const struct {
            int layout, movie;
            uint64_t ms;
        } steps[] = {{54, 0, 4000}, {55, 0, 3000}, {54, 0, 2200},
                     {54, 1, 100},  {57, 0, 700},  {99, 0, 40}};

        for (unsigned i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
            window_scene_note(&s, window_scene_kind(&ids, steps[i].layout, steps[i].movie),
                              steps[i].ms * 1000000ull);
        }
    }
    window_scene_text(&s, b, sizeof(b));
    CHECK(text_is(b, "layout: play 6.2 s, scene 3.1 s, pause 0.7 s, other 0.0 s"));
    /* the kinds out of range are ignored */
    window_scene_note(&s, (SceneKind)99, 5000000000ull);
    window_scene_text(&s, b, sizeof(b));
    CHECK(strstr(b, "5.0") == NULL);
    /* a cut buffer ends terminated, inside its size */
    {
        char t[12];
        const size_t n = window_scene_text(&s, t, sizeof(t));

        CHECK(n < sizeof(t) && strlen(t) == n);
    }
    window_scene_reset(&s);
    window_scene_text(&s, b, sizeof(b));
    CHECK(text_is(b, "layout: no steps"));

    /* the slow step's game threads part */
    window_scene_threads_text(b, sizeof(b), 199.3, 39, 171.2, "Main", 2);
    CHECK(text_is(b, "game threads 199.3 in 39 switches (longest Main 171.2 ms; on core 2)"));
    window_scene_threads_text(b, sizeof(b), 199.3, 39, 171.2, "sound", -1);
    CHECK(text_is(b, "game threads 199.3 in 39 switches (longest sound 171.2 ms)"));
    window_scene_threads_text(b, sizeof(b), 3.0, 0, 0.0, "", 5);
    CHECK(text_is(b, "game threads 3.0 in 0 switches (on core 5)"));
    window_scene_threads_text(b, sizeof(b), 3.0, 0, 0.0, NULL, -1);
    CHECK(text_is(b, "game threads 3.0 in 0 switches"));
    window_scene_threads_text(b, sizeof(b), 9.0, 12, 4.5, NULL, 0);
    CHECK(text_is(b, "game threads 9.0 in 12 switches (longest ? 4.5 ms; on core 0)"));
    {
        char t[20];
        const size_t n = window_scene_threads_text(t, sizeof(t), 199.3, 39, 171.2, "Main", 2);

        CHECK(n < sizeof(t) && strlen(t) == n);
    }

    /* the VU work: totals over 10 replays, a replay's average */
    window_scene_vu_text(b, sizeof(b), 11820, 610000, 0, 10, 0);
    CHECK(text_is(
        b, "VU: 1182 draws, 61k triangles, 549k vertex invocations (pre-pass off: 9 a triangle)"));
    window_scene_vu_text(b, sizeof(b), 2500, 92000, 41000, 10, 1);
    CHECK(text_is(
        b, "VU: 250 draws, 9.2k triangles, 4.1k vertex invocations (pre-pass: each vertex once)"));
    window_scene_vu_text(b, sizeof(b), 100, 2000000, 0, 1, 0);
    CHECK(strstr(b, "2.0M triangles, 18.0M vertex") != NULL);
    window_scene_vu_text(b, sizeof(b), 0, 0, 0, 0, 0);
    CHECK(
        text_is(b, "VU: 0 draws, 0 triangles, 0 vertex invocations (pre-pass off: 9 a triangle)"));
    {
        char t[16];
        const size_t n = window_scene_vu_text(t, sizeof(t), 100, 200, 0, 1, 0);

        CHECK(n < sizeof(t) && strlen(t) == n);
    }

    /* list 0 against the VU triangles (issue 179): list 0 = 12.3 ms + 0.41
       ms a thousand triangles exactly */
    {
        SceneVuFit f;
        double a = 0.0, k = 0.0, r2 = 0.0;
        static const uint64_t tris[4] = {10000, 20000, 40000, 60000};
        static const double noisy[4][2] = {{10, 20}, {20, 22}, {30, 30}, {40, 28}};

        memset(&f, 0, sizeof(f));
        CHECK(window_scene_vu_fit_text(b, sizeof(b), &f) == 0 && b[0] == '\0');
        for (int i = 0; i < 4; i++) {
            window_scene_vu_fit_add(&f, tris[i], 12.3 + 0.41 * (double)tris[i] / 1000.0);
        }
        CHECK(window_scene_vu_fit(&f, &a, &k, &r2));
        CHECK(a > 12.299 && a < 12.301 && k > 0.4099 && k < 0.4101 && r2 > 0.9999);
        window_scene_vu_fit_text(b, sizeof(b), &f);
        CHECK(text_is(b, "; list 0 GPU 25.6 ms a replay, 12.3 ms + 0.41 ms a thousand triangles "
                         "(R^2 1.00, 4 replays)"));
        /* noisy: b 0.32, a 17, R^2 25600 / 34000 */
        memset(&f, 0, sizeof(f));
        for (int i = 0; i < 4; i++) {
            window_scene_vu_fit_add(&f, (uint64_t)(noisy[i][0] * 1000.0), noisy[i][1]);
        }
        window_scene_vu_fit_text(b, sizeof(b), &f);
        CHECK(text_is(b, "; list 0 GPU 25.0 ms a replay, 17.0 ms + 0.32 ms a thousand triangles "
                         "(R^2 0.75, 4 replays)"));
        /* two replays: the mean alone */
        memset(&f, 0, sizeof(f));
        window_scene_vu_fit_add(&f, 10000, 20.0);
        window_scene_vu_fit_add(&f, 20000, 22.0);
        CHECK(!window_scene_vu_fit(&f, &a, &k, &r2));
        window_scene_vu_fit_text(b, sizeof(b), &f);
        CHECK(text_is(b, "; list 0 GPU 21.0 ms a replay (2 replays)"));
        /* the triangles not varying: no slope */
        memset(&f, 0, sizeof(f));
        for (int i = 0; i < 3; i++) {
            window_scene_vu_fit_add(&f, 10000, 5.0 + i);
        }
        CHECK(!window_scene_vu_fit(&f, &a, &k, &r2));
        window_scene_vu_fit_text(b, sizeof(b), &f);
        CHECK(text_is(b, "; list 0 GPU 6.0 ms a replay (3 replays)"));
        /* list 0 not varying: the line, no R^2 */
        memset(&f, 0, sizeof(f));
        for (int i = 1; i <= 3; i++) {
            window_scene_vu_fit_add(&f, (uint64_t)i * 10000u, 5.0);
        }
        CHECK(window_scene_vu_fit(&f, &a, &k, &r2) && r2 < 0.0);
        window_scene_vu_fit_text(b, sizeof(b), &f);
        CHECK(text_is(b, "; list 0 GPU 5.0 ms a replay, 5.0 ms + 0.00 ms a thousand triangles (3 "
                         "replays)"));
        {
            char t[16];
            const size_t n = window_scene_vu_fit_text(t, sizeof(t), &f);

            CHECK(n < sizeof(t) && strlen(t) == n);
        }
    }
    /* the slow-scene dump's trigger (issue 179): a scene-dominant block on a
       window, no movie, 30 s up, 8 timed replays, the work (span less the
       display wait, a replay's mean) over the bound, not yet written */
    {
        SlowSceneBlock t;

        memset(&s, 0, sizeof(s));
        CHECK(window_scene_dominant(&s) == SCENE_OTHER); /* no steps */
        window_scene_note(&s, SCENE_PLAY, 3000000000ull);
        window_scene_note(&s, SCENE_SCENE, 5000000000ull);
        window_scene_note(&s, SCENE_PAUSE, 1000000000ull);
        CHECK(window_scene_dominant(&s) == SCENE_SCENE);
        window_scene_note(&s, SCENE_PLAY, 3000000000ull);
        CHECK(window_scene_dominant(&s) == SCENE_PLAY); /* 6 s of play against 5 s */

        memset(&t, 0, sizeof(t));
        t.dominant = SCENE_SCENE;
        t.window = 1;
        t.uptimeS = 45.0;
        t.gpuN = 10;
        t.spanSumMs = 600.0;   /* 60 ms a replay */
        t.waitSumMs = 100.0;   /* 10 ms of it the display's image */
        t.presentSumMs = 20.0; /* 2 ms the present pass: work 48 */
        t.boundMs = 40.0;
        CHECK(window_slow_scene_work_ms(&t) > 47.999 && window_slow_scene_work_ms(&t) < 48.001);
        CHECK(window_slow_scene_due(&t));
        /* a driver whose stamps run ahead of the wait (a Mali): the wait is in
           the present figure, and a display-bound block is not the dump's */
        t.waitSumMs = 2.0;
        t.presentSumMs = 250.0; /* 25 ms a replay of it the image: work 34.8 */
        CHECK(!window_slow_scene_due(&t));
        t.waitSumMs = 100.0;
        t.presentSumMs = 20.0;
        /* each condition alone defeats it */
        t.dominant = SCENE_PLAY;
        CHECK(!window_slow_scene_due(&t));
        t.dominant = SCENE_SCENE;
        t.window = 0;
        CHECK(!window_slow_scene_due(&t));
        t.window = 1;
        t.movie = 1;
        CHECK(!window_slow_scene_due(&t));
        t.movie = 0;
        t.written = 1; /* once a run */
        CHECK(!window_slow_scene_due(&t));
        t.written = 0;
        t.uptimeS = 29.9;
        CHECK(!window_slow_scene_due(&t));
        t.uptimeS = 30.0;
        CHECK(window_slow_scene_due(&t)); /* 30 s is enough */
        t.gpuN = 7;
        CHECK(!window_slow_scene_due(&t));
        t.gpuN = 8;
        CHECK(window_slow_scene_due(&t));
        t.boundMs = 0.0; /* [dev] slow_scene_dump_ms = 0: off */
        CHECK(!window_slow_scene_due(&t));
        t.boundMs = -5.0;
        CHECK(!window_slow_scene_due(&t));
        /* the display wait is not GPU work: a span of 60 with a wait of 30
           is under 40; the bound itself is not over */
        t.boundMs = 40.0;
        t.gpuN = 10;
        t.waitSumMs = 300.0;
        CHECK(!window_slow_scene_due(&t));
        t.waitSumMs = 200.0; /* work 40.0 exactly */
        CHECK(!window_slow_scene_due(&t));
        t.waitSumMs = 199.0;
        CHECK(window_slow_scene_due(&t));
        /* a wait above the span, or no replay: no work, never negative */
        t.waitSumMs = 700.0;
        CHECK(window_slow_scene_work_ms(&t) == 0.0);
        t.gpuN = 0;
        CHECK(window_slow_scene_work_ms(&t) == 0.0 && !window_slow_scene_due(&t));
        /* the line */
        t.gpuN = 10;
        t.waitSumMs = 100.0;
        window_slow_scene_text(b, sizeof(b), &t, 4812, "slow-20261106-120000.zip");
        CHECK(text_is(b, "slow scene: the block's GPU work 50.0 ms a replay (bound 40; display "
                         "wait 10.0, span 60.0) in a scene layout: frame 4812 written to "
                         "logs/slow-20261106-120000.zip; attach it with the log"));
        {
            char c[20];
            const size_t n = window_slow_scene_text(c, sizeof(c), &t, 1, "slow-x.zip");

            CHECK(n < sizeof(c) && strlen(c) == n);
        }
    }
    /* the swapchain's images */
    window_scene_images_text(b, sizeof(b), 10, 1);
    CHECK(text_is(b, " (images: 10, in use 1)"));
    CHECK(window_scene_images_text(b, sizeof(b), 0, 0) == 0 && b[0] == '\0');

    if (failures != 0) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("all passed\n");
    return 0;
}

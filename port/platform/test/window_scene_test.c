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
 * from the work) and its line; the GPU-bound hint (issue 123: the
 * reporter's 3x figures with invocations spread a factor of 2, 2x fits; each
 * condition that leaves it out; the reporter's own narrow invocations and no
 * fit at all: GPU-bound, no step named; the vertex work alone past the
 * budget; a flat line at 1x, at a fractional scale (not a Resolution
 * row's: Window, Auto or an upscaler) a lower Resolution at a render scale
 * rounded down, the largest offered step that fits; a cut buffer; replays
 * with no VU work in the block (keep frames) do not pass the spread guard).
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
    char b[640]; /* the slow-scene line passes 256 bytes (window_host.c holds 640) */

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

    /* the skin check's twins (issue 123): a replay's average, "" without */
    window_scene_twins_text(b, sizeof(b), 420, 760000, 10);
    CHECK(text_is(b, "; twins 42 draws, 76k vertex invocations a picture"));
    window_scene_twins_text(b, sizeof(b), 3, 2700, 0);
    CHECK(text_is(b, "; twins 3 draws, 2.7k vertex invocations a picture"));
    window_scene_twins_text(b, sizeof(b), 0, 0, 10);
    CHECK(text_is(b, ""));
    {
        char t[16];
        const size_t n = window_scene_twins_text(t, sizeof(t), 420, 760000, 10);

        CHECK(n < sizeof(t) && strlen(t) == n);
    }

    /* list 0 against the VU triangles (issue 179): list 0 = 12.3 ms + 0.41
       ms a thousand vertex invocations exactly */
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
        CHECK(text_is(
            b, "; GPU work 25.6 ms a replay, 12.3 ms + 0.41 ms a thousand vertex invocations "
               "(R^2 1.00, 4 replays)"));
        /* noisy: b 0.32, a 17, R^2 25600 / 34000 */
        memset(&f, 0, sizeof(f));
        for (int i = 0; i < 4; i++) {
            window_scene_vu_fit_add(&f, (uint64_t)(noisy[i][0] * 1000.0), noisy[i][1]);
        }
        window_scene_vu_fit_text(b, sizeof(b), &f);
        CHECK(text_is(
            b, "; GPU work 25.0 ms a replay, 17.0 ms + 0.32 ms a thousand vertex invocations "
               "(R^2 0.75, 4 replays)"));
        /* two replays: the mean alone */
        memset(&f, 0, sizeof(f));
        window_scene_vu_fit_add(&f, 10000, 20.0);
        window_scene_vu_fit_add(&f, 20000, 22.0);
        CHECK(!window_scene_vu_fit(&f, &a, &k, &r2));
        window_scene_vu_fit_text(b, sizeof(b), &f);
        CHECK(text_is(b, "; GPU work 21.0 ms a replay (2 replays)"));
        /* the triangles not varying: no slope */
        memset(&f, 0, sizeof(f));
        for (int i = 0; i < 3; i++) {
            window_scene_vu_fit_add(&f, 10000, 5.0 + i);
        }
        CHECK(!window_scene_vu_fit(&f, &a, &k, &r2));
        window_scene_vu_fit_text(b, sizeof(b), &f);
        CHECK(text_is(b, "; GPU work 6.0 ms a replay (3 replays); fit: not enough spread (x "
                         "10.0..10.0 thousand vertex invocations)"));
        /* the spread under a factor of 2 (a cutscene: the log's -64 ms + 2.42): no line;
           exactly 2 fits */
        memset(&f, 0, sizeof(f));
        window_scene_vu_fit_add(&f, 40000, 60.0);
        window_scene_vu_fit_add(&f, 50000, 64.0);
        window_scene_vu_fit_add(&f, 79000, 70.0);
        CHECK(!window_scene_vu_fit(&f, &a, &k, &r2));
        window_scene_vu_fit_text(b, sizeof(b), &f);
        CHECK(text_is(b, "; GPU work 64.7 ms a replay (3 replays); fit: not enough spread (x "
                         "40.0..79.0 thousand vertex invocations)"));
        memset(&f, 0, sizeof(f));
        window_scene_vu_fit_add(&f, 40000, 60.0);
        window_scene_vu_fit_add(&f, 60000, 64.0);
        window_scene_vu_fit_add(&f, 80000, 70.0);
        CHECK(window_scene_vu_fit(&f, &a, &k, &r2));
        /* a cutscene block with a fade's keep frames (no VU work, a few ms):
           the zeros do not make the narrow scene replays a spread */
        memset(&f, 0, sizeof(f));
        window_scene_vu_fit_add(&f, 0, 3.0);
        window_scene_vu_fit_add(&f, 150000, 40.0);
        window_scene_vu_fit_add(&f, 0, 2.5);
        window_scene_vu_fit_add(&f, 152000, 41.0);
        window_scene_vu_fit_add(&f, 155000, 40.5);
        CHECK(!window_scene_vu_fit(&f, &a, &k, &r2));
        window_scene_vu_fit_text(b, sizeof(b), &f);
        CHECK(strstr(b, "fit: not enough spread (x 150.0..155.0 thousand") != NULL);
        /* none with VU work: no line */
        memset(&f, 0, sizeof(f));
        for (int i = 0; i < 3; i++) {
            window_scene_vu_fit_add(&f, 0, 3.0 + i);
        }
        CHECK(!window_scene_vu_fit(&f, &a, &k, &r2));
        /* list 0 not varying: the line, no R^2 */
        memset(&f, 0, sizeof(f));
        for (int i = 1; i <= 3; i++) {
            window_scene_vu_fit_add(&f, (uint64_t)i * 10000u, 5.0);
        }
        CHECK(window_scene_vu_fit(&f, &a, &k, &r2) && r2 < 0.0);
        window_scene_vu_fit_text(b, sizeof(b), &f);
        CHECK(text_is(
            b, "; GPU work 5.0 ms a replay, 5.0 ms + 0.00 ms a thousand vertex invocations (3 "
               "replays)"));
        {
            char t[16];
            const size_t n = window_scene_vu_fit_text(t, sizeof(t), &f);

            CHECK(n < sizeof(t) && strlen(t) == n);
        }
    }
    /* the GPU-bound hint (issue 123): the Adreno 740's blocks at Custom 3x,
       25.7 presented fps against 30 game fps, 217 late skips, GPU 37.7 ms a
       picture; a fit of 34.0 ms + 0.01 ms a thousand invocations over 376k
       on average (spread 200k..552k, the fit's factor of 2): 3.76 ms of
       vertex work, 33.94 ms scaling with the pixels; 2x = 3.76 + 33.94 x
       4/9 = 18.8 ms under 0.9 of the 33.3 ms step */
    {
        SceneVuFit f, flat;
        static const uint64_t inv[3] = {200000, 376000, 552000};
        static const uint64_t narrow[3] = {370000, 376000, 382000};

        memset(&f, 0, sizeof(f));
        for (int i = 0; i < 3; i++) {
            window_scene_vu_fit_add(&f, inv[i], 34.0 + 0.01 * (double)inv[i] / 1000.0);
        }
        window_scene_gpu_hint_text(b, sizeof(b), 25.7, 30.0, 217, 37.7, 3.0, &f);
        CHECK(text_is(b, "; the GPU takes 37.7 ms a picture at this resolution (3x): 2x would fit "
                         "the game's rate (37.7 ms = 3.8 ms of vertex work by the fit + 33.9 ms "
                         "that scales with the pixels: 3.8 + 33.9 x (2/3)^2 = 18.8 ms, under 30.0 "
                         "ms, 0.9 of the 33.3 ms step at 30.0 game fps; a hint: nothing changes "
                         "by itself)"));
        /* not GPU-bound: presented at the game's rate, no late skips, the GPU
           inside the step, no game rate: nothing */
        CHECK(window_scene_gpu_hint_text(b, sizeof(b), 30.0, 30.0, 217, 37.7, 3.0, &f) == 0 &&
              b[0] == '\0');
        CHECK(window_scene_gpu_hint_text(b, sizeof(b), 25.7, 30.0, 0, 37.7, 3.0, &f) == 0);
        CHECK(window_scene_gpu_hint_text(b, sizeof(b), 25.7, 30.0, 217, 33.3, 3.0, &f) == 0);
        CHECK(window_scene_gpu_hint_text(b, sizeof(b), 0.0, 0.0, 217, 37.7, 3.0, &f) == 0);
        /* the reporter's own blocks: the invocations 370k..382k, not spread
           enough for a line (a vertex-bound block would read as pixels):
           GPU-bound, no step named */
        memset(&f, 0, sizeof(f));
        for (int i = 0; i < 3; i++) {
            window_scene_vu_fit_add(&f, narrow[i], 0.1 * (double)narrow[i] / 1000.0);
        }
        window_scene_gpu_hint_text(b, sizeof(b), 25.7, 30.0, 217, 37.7, 3.0, &f);
        CHECK(text_is(b, "; GPU-bound at 3x (the GPU takes 37.7 ms a picture, the step 33.3 ms "
                         "at 30.0 game fps): no resolution hint (fit: not enough spread)"));
        window_scene_gpu_hint_text(b, sizeof(b), 25.7, 30.0, 217, 37.7, 3.0, NULL);
        CHECK(text_is(b, "; GPU-bound at 3x (the GPU takes 37.7 ms a picture, the step 33.3 ms "
                         "at 30.0 game fps): no resolution hint (fit: under 3 timed replays)"));
        /* the vertex work alone over the budget (0.1 ms a thousand over
           376k: 37.6 of 40 ms): no step fits */
        memset(&f, 0, sizeof(f));
        for (int i = 0; i < 3; i++) {
            window_scene_vu_fit_add(&f, inv[i], 0.1 * (double)inv[i] / 1000.0);
        }
        window_scene_gpu_hint_text(b, sizeof(b), 25.7, 30.0, 217, 40.0, 3.0, &f);
        CHECK(text_is(b, "; the GPU takes 40.0 ms a picture at this resolution (3x): no lower "
                         "resolution step would fit the game's rate (37.6 ms of vertex work by "
                         "the fit + 2.4 ms that scales with the pixels: 37.6 + 2.4 x (1/3)^2 = "
                         "37.9 ms at 1x against 30.0 ms, 0.9 of the 33.3 ms step at 30.0 game "
                         "fps)"));
        /* a line with no vertex cost (the work flat over spread invocations):
           all of it the pixels' */
        memset(&flat, 0, sizeof(flat));
        for (int i = 0; i < 3; i++) {
            window_scene_vu_fit_add(&flat, inv[i], 40.0);
        }
        /* at 1x */
        window_scene_gpu_hint_text(b, sizeof(b), 25.7, 30.0, 217, 40.0, 1.0, &flat);
        CHECK(text_is(b, "; the GPU takes 40.0 ms a picture at this resolution (1x): at 1x "
                         "already, no lower resolution step is offered (0.0 ms of vertex work by "
                         "the fit + 40.0 ms that scales with the pixels, against 30.0 ms, 0.9 of "
                         "the 33.3 ms step at 30.0 game fps)"));
        /* a fractional scale (Window, Auto, an upscaler's render scale) is
           no Resolution row: a lower Resolution, the render scale rounded
           down to an offered step */
        window_scene_gpu_hint_text(b, sizeof(b), 25.7, 30.0, 217, 40.0, 2.5, &flat);
        CHECK(text_is(b, "; the GPU takes 40.0 ms a picture at this render scale (2.5x, not a "
                         "Resolution row's: Window, Auto or an upscaler): a lower Resolution (or "
                         "Upscaling quality) would fit the game's rate at a render scale of 2x or "
                         "under (40.0 ms = 0.0 ms of vertex work by the fit + 40.0 ms that scales "
                         "with the pixels: 0.0 + 40.0 x (2/2.5)^2 = 25.6 ms, under 30.0 ms, 0.9 of "
                         "the 33.3 ms step at 30.0 game fps; a hint: nothing changes by itself)"));
        /* the largest step that fits (4x of 6x at 25 game fps), 5x not offered */
        window_scene_gpu_hint_text(b, sizeof(b), 20.0, 25.0, 9, 50.0, 6.0, &flat);
        CHECK(text_is(b, "; the GPU takes 50.0 ms a picture at this resolution (6x): 4x would fit "
                         "the game's rate (50.0 ms = 0.0 ms of vertex work by the fit + 50.0 ms "
                         "that scales with the pixels: 0.0 + 50.0 x (4/6)^2 = 22.2 ms, under 36.0 "
                         "ms, 0.9 of the 40.0 ms step at 25.0 game fps; a hint: nothing changes by "
                         "itself)"));
        /* issue 179 (Mali-G52): no late skip, 157 frames left out because
           the render thread was busy: the call site adds them, the hint
           prints (the spread guard still names no step) */
        CHECK(window_scene_hint_skips(0, 157) == 157 && window_scene_hint_skips(3, 4) == 7 &&
              window_scene_hint_skips(0xFFFFFFFFu, 5) == 0xFFFFFFFFu);
        window_scene_gpu_hint_text(b, sizeof(b), 15.0, 25.0, window_scene_hint_skips(0, 157), 53.0,
                                   1.0, &flat);
        CHECK(b[0] != '\0' && strstr(b, "at 1x already") != NULL);
        memset(&f, 0, sizeof(f));
        for (int i = 0; i < 3; i++) {
            window_scene_vu_fit_add(&f, narrow[i], 0.1 * (double)narrow[i] / 1000.0);
        }
        window_scene_gpu_hint_text(b, sizeof(b), 15.0, 25.0, window_scene_hint_skips(0, 157), 53.0,
                                   1.0, &f);
        CHECK(strstr(b, "no resolution hint (fit: not enough spread)") != NULL &&
              strstr(b, "2x would") == NULL);
        CHECK(window_scene_gpu_hint_text(b, sizeof(b), 15.0, 25.0, window_scene_hint_skips(0, 0),
                                         53.0, 1.0, &f) == 0);
        {
            char t[16];
            const size_t n =
                window_scene_gpu_hint_text(t, sizeof(t), 20.0, 25.0, 9, 50.0, 6.0, &flat);

            CHECK(n < sizeof(t) && strlen(t) == n);
        }
    }
    /* the block's acquire line: the pictures whose image was free at the
       present's submit; nothing when none asked (not Vulkan) */
    CHECK(window_scene_acquire_text(b, sizeof(b), 41, 19) == strlen(b) &&
          text_is(b, "acquire: the display's image was free at the present's submit in 41 of 60 "
                     "pictures"));
    CHECK(window_scene_acquire_text(b, sizeof(b), 0, 7) > 0 &&
          strstr(b, "in 0 of 7 pictures") != NULL);
    CHECK(window_scene_acquire_text(b, sizeof(b), 0, 0) == 0 && b[0] == '\0');
    {
        char t[16];
        const size_t n = window_scene_acquire_text(t, sizeof(t), 3, 4);

        CHECK(n < sizeof(t) && strlen(t) == n);
    }
    /* the end of the 10 s GPU lines on a tile-based GPU (window_tiler_words):
       the stretch before list 0 named; nothing elsewhere */
    CHECK(window_scene_tiler_words(0)[0] == '\0');
    CHECK(text_is(window_scene_tiler_words(1),
                  " (tile-based GPU: the last scene pass carries the earlier passes' tile work; "
                  "the present's own list alone waits for the display's image: the display "
                  "wait, or the present where the GPU holds only its colour writes): the "
                  "stretch before list 0 is the uploads and the GPU's work still in flight"));
    /* the GPU ms line's pre-pass part: its cost on, the word off */
    CHECK(window_scene_prepass_gpu_text(b, sizeof(b), 1, 3.456) == 4 && text_is(b, "3.46"));
    CHECK(window_scene_prepass_gpu_text(b, sizeof(b), 0, 0.0) == 3 && text_is(b, "off"));
    CHECK(window_scene_prepass_gpu_text(b, sizeof(b), 0, 2.5) == 3 && text_is(b, "off"));
    {
        char t[3];

        CHECK(window_scene_prepass_gpu_text(t, sizeof(t), 0, 0.0) == 2 && strlen(t) == 2);
    }
    /* the block's share of a counter: the difference, or the value itself
       after a restart from 0 (a renderer rebuild or a backend switch: no
       wrap to 18446744073709551xxx) */
    CHECK(window_scene_counter_delta(60, 41) == 19);
    CHECK(window_scene_counter_delta(41, 41) == 0);
    CHECK(window_scene_counter_delta(7, 41) == 7);
    CHECK(window_scene_counter_delta(0, 41) == 0);
    CHECK(window_scene_counter_delta(UINT64_MAX, 0) == UINT64_MAX);
    /* the slow-scene dump's trigger (issue 179): a scene-dominant block on a
       window, no movie, 30 s up, 8 timed replays, the work (span less the
       display wait, a replay's mean) over the bound, and up to three dumps a run,
       each from a block 1.5 times as heavy as the last */
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
        t.count = SLOW_SCENE_DUMPS; /* three a run */
        t.lastWorkMs = 1.0;
        CHECK(!window_slow_scene_due(&t));
        t.count = 0;
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
        t.waitSumMs = 180.0; /* with the present's 20: work 40.0 exactly */
        CHECK(!window_slow_scene_due(&t));
        t.waitSumMs = 179.0;
        CHECK(window_slow_scene_due(&t));
        /* a wait above the span, or no replay: no work, never negative */
        t.waitSumMs = 700.0;
        CHECK(window_slow_scene_work_ms(&t) == 0.0);
        t.gpuN = 0;
        CHECK(window_slow_scene_work_ms(&t) == 0.0 && !window_slow_scene_due(&t));
        /* the later dumps: the work a block at least 1.5 times the last's */
        t.gpuN = 10;
        t.waitSumMs = 100.0;
        t.presentSumMs = 20.0; /* work 48.0 again */
        t.count = 0;
        t.lastWorkMs = 1000.0; /* the first dump ignores the last */
        CHECK(window_slow_scene_due(&t));
        t.count = 1;
        t.lastWorkMs = 32.0; /* 48 is exactly 1.5 times 32 */
        CHECK(window_slow_scene_due(&t));
        t.lastWorkMs = 50.0;         /* work 74.9 is not 1.5 times 50 */
        t.spanSumMs = 600.0 + 269.0; /* work (869 - 100 - 20) / 10 = 74.9 */
        CHECK(window_slow_scene_work_ms(&t) > 74.89 && window_slow_scene_work_ms(&t) < 74.91);
        CHECK(!window_slow_scene_due(&t));
        t.spanSumMs = 600.0 + 270.0; /* 75.0 */
        CHECK(window_slow_scene_due(&t));
        t.count = 2;
        CHECK(window_slow_scene_due(&t));
        t.count = 3; /* three dumps a run, however heavy */
        CHECK(!window_slow_scene_due(&t));
        t.count = 1;
        t.lastWorkMs = 0.0; /* a last block of no work: anything over the bound */
        CHECK(window_slow_scene_due(&t));
        t.spanSumMs = 600.0;
        t.count = 1;
        t.lastWorkMs = 40.0; /* 48 is under 60: a second dump needs a heavier block */
        CHECK(!window_slow_scene_due(&t));
        /* the bound still applies to the first dump at any last work */
        t.count = 0;
        t.waitSumMs = 180.0; /* work 40.0 exactly */
        CHECK(!window_slow_scene_due(&t));
        t.waitSumMs = 100.0;
        /* issue 179: the boat's 99.7 ms block (stage 42) dumped, then the cage's
           90.9 ms in a new stage is the first slow block of that stage: due though
           not 1.5 times the boat's; the same stage is not */
        t.count = 1;
        t.lastStage = 42;
        t.stage = 42;
        t.lastWorkMs = 99.7;
        t.spanSumMs = 1029.0; /* work (1029 - 100 - 20) / 10 = 90.9 */
        CHECK(window_slow_scene_work_ms(&t) > 90.8 && window_slow_scene_work_ms(&t) < 91.0);
        CHECK(!window_slow_scene_new_stage(&t));
        CHECK(!window_slow_scene_due(&t));
        t.stage = 40;
        CHECK(window_slow_scene_new_stage(&t));
        CHECK(window_slow_scene_due(&t));
        window_slow_scene_text(b, sizeof(b), &t, 7, 2000.0, 1000.0, "slow-x.zip");
        CHECK(strstr(b, "in a scene layout (the first slow block of stage 40): frame 7 (") != NULL);
        t.count = 0; /* the first dump of a run is no new stage's */
        CHECK(!window_slow_scene_new_stage(&t));
        t.count = SLOW_SCENE_DUMPS; /* and three a run still holds */
        CHECK(!window_slow_scene_due(&t));
        t.count = 1;
        t.stage = 40;
        t.waitSumMs = 180.0; /* a new stage under the bound is not due either */
        t.spanSumMs = 600.0;
        CHECK(!window_slow_scene_due(&t));
        t.stage = t.lastStage = 0;
        t.count = 0;
        t.waitSumMs = 100.0;
        /* the replay the dump is to hold: its vertex work at or above the
           block's mean a replay */
        CHECK(window_slow_scene_pick(598000.0, 598000.0));
        CHECK(window_slow_scene_pick(682100.0, 598000.0));
        CHECK(!window_slow_scene_pick(597999.0, 598000.0));
        CHECK(!window_slow_scene_pick(0.0, 0.0)); /* no figures: no pick */
        CHECK(!window_slow_scene_pick(5000.0, 0.0));
        CHECK(!window_slow_scene_pick(-1.0, 598000.0));
        /* the line */
        t.count = 1;
        window_slow_scene_text(b, sizeof(b), &t, 4812, 682100.0, 598000.0,
                               "slow-20261106-120000.zip");
        CHECK(text_is(b, "slow scene: the block's GPU work 48.0 ms a replay (bound 40; display "
                         "wait 10.0, present 2.0, span 60.0) in a scene layout: frame 4812 "
                         "(its vertex work 682.1 thousand against the block's mean 598.0 "
                         "thousand) written to logs/slow-20261106-120000.zip (dump 2 of 3 this "
                         "run); attach it with the log"));
        t.count = 0;
        window_slow_scene_text(b, sizeof(b), &t, 4813, 9.0, 4.5, "slow-x.zip");
        CHECK(text_is(b, "slow scene: the block's GPU work 48.0 ms a replay (bound 40; display "
                         "wait 10.0, present 2.0, span 60.0) in a scene layout: frame 4813 "
                         "(its vertex work 0.0 thousand against the block's mean 0.0 thousand) "
                         "written to logs/slow-x.zip (dump 1 of 3 this run); attach it with "
                         "the log"));
        {
            char c[20];
            const size_t n = window_slow_scene_text(c, sizeof(c), &t, 1, 5.0, 4.0, "slow-x.zip");

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

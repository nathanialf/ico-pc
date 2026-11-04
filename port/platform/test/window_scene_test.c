/*
 * port/platform/test/window_scene_test.c
 *
 * The window's scene lines (window_scene.h, issue 179): the layout share
 * over a sequence of layout ids and step times (kinds, the movie counted as
 * a scene, the order and the empty block, a reset, a cut buffer), the slow
 * step's game threads part (the longest switch and the core, each part
 * left out when not known) and the VU work of a replay (pre-pass off and
 * on, the count words, no replays).
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

    if (failures != 0) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("all passed\n");
    return 0;
}

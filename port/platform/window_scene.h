/*
 * port/platform/window_scene.h
 *
 * Text writers of the window's 10 s block that say which part of the game
 * the time was in (issue 179: a cutscene read from one phone log), kept out
 * of window_host.c (at its file size gate).  Pure: no window, no renderer;
 * port/platform/test/window_scene_test.c runs them.
 *
 *   - the layout share: the block's steps' time by the game's layout (play,
 *     a scene or a movie, the title, the pause menu, the options pages,
 *     the rest), "layout: play 6.2 s, scene 3.1 s, pause 0.7 s";
 *   - the slow step's game threads part, with the longest single fiber
 *     switch and the core the game thread was on: "game threads 199.3 in 39
 *     switches (longest Main 171.2 ms; on core 2)";
 *   - the VU work of a replay: "VU: 1182 draws, 61k triangles, 550k vertex
 *     invocations (pre-pass off: 9 a triangle)".
 */
#ifndef ICO_PLATFORM_WINDOW_SCENE_H
#define ICO_PLATFORM_WINDOW_SCENE_H

#include <stddef.h>
#include <stdint.h>

typedef enum SceneKind {
    SCENE_PLAY = 0,
    SCENE_SCENE, /* a scene's empty layout, or a movie playing */
    SCENE_TITLE,
    SCENE_PAUSE,
    SCENE_SETTINGS, /* the game's options and button configuration */
    SCENE_OTHER,    /* every other layout (the port's pages, the memory card's) */
    SCENE_KIND_COUNT
} SceneKind;

/* The running disc's layout ids (region.h layout_*) */
typedef struct SceneIds {
    int play, scene, titleContinue, titleNew, pause, options, keyConfig;
} SceneIds;

/* The kind of the game's state: a movie counts as a scene, whatever the
   layout */
SceneKind window_scene_kind(const SceneIds *ids, int layout, int movie);

/* The block's time by kind, in ns */
typedef struct SceneShare {
    uint64_t ns[SCENE_KIND_COUNT];
} SceneShare;

/* ns of a step added to the kind of the state it ran in */
void window_scene_note(SceneShare *s, SceneKind kind, uint64_t ns);
/* "layout: play 6.2 s, scene 3.1 s" in the kinds' order, those with any
   time, to a tenth of a second; "layout: no steps" with none; the length
   written */
size_t window_scene_text(const SceneShare *s, char *out, size_t cap);
/* the block over: the sums empty */
void window_scene_reset(SceneShare *s);

/* The slow step's game threads part: "game threads 199.3 in 39 switches
   (longest Main 171.2 ms; on core 2)".  longestMs 0 (no switch timed) leaves
   the bracket out; core below 0 (not known) leaves "; on core N" out; name
   NULL or empty is "?" */
size_t window_scene_threads_text(char *out, size_t cap, double threadsMs, unsigned long switches,
                                 double longestMs, const char *name, int core);

/* A replay's VU work, averaged over replays (n): draws, triangles drawn
   (index lists in threes), the vertices the pre-passes recorded (a
   pre-pass's: each vertex once), and prepass whether the pre-pass is on.
   Off, every triangle shades 3 corners three times (a tiler's vertex shader
   runs for position then for varyings per index: 9 invocations a triangle).
     "VU: 1182 draws, 61k triangles, 550k vertex invocations (pre-pass off: 9 a triangle)"
     "VU: 250 draws, 9.2k triangles, 4.1k vertex invocations (pre-pass: each vertex once)" */
size_t window_scene_vu_text(char *out, size_t cap, uint64_t draws, uint64_t tris,
                            uint64_t preVertices, unsigned n, int prepass);

#endif /* ICO_PLATFORM_WINDOW_SCENE_H */

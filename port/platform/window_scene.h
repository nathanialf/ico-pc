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
 *     invocations (pre-pass off: 9 a triangle)", and (issue 179) what list
 *     0, the scene, cost the GPU in the block's timed replays against
 *     their VU triangles: "; list 0 GPU 41.2 ms a replay, 12.3 ms + 0.41 ms
 *     a thousand triangles (R^2 0.80, 38 replays)";
 *   - the swapchain's images and those the game holds: " (images: 10, in
 *     use 1)" after the present mode.
 */
#ifndef ICO_PLATFORM_WINDOW_SCENE_H
#define ICO_PLATFORM_WINDOW_SCENE_H

#include <stdbool.h>
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

/* The kind that held most of the block's step time (the first of equals);
   SCENE_OTHER for a block with no steps */
SceneKind window_scene_dominant(const SceneShare *s);

/* The slow-scene dump's trigger (issue 179, window_host.c slow_scene_check):
   the figures of a 10 s block, and whether this block is the one to write
   the run's single cutscene frame.  boundMs is [dev] slow_scene_dump_ms (0
   or less: off); workMs and waitMs are the block's GPU sums, from which the
   mean work less the display wait is compared with the bound. */
#define SLOW_SCENE_UPTIME_MIN_S 30.0
#define SLOW_SCENE_GPU_MIN 8

typedef struct SlowSceneBlock {
    SceneKind dominant; /* window_scene_dominant of the block */
    int window;         /* a window is up */
    int movie;          /* a movie is playing */
    int written;        /* this run already wrote its dump */
    double uptimeS;
    unsigned gpuN;       /* the block's timed replays */
    double spanSumMs;    /* their GPU spans summed */
    double waitSumMs;    /* and their display waits */
    double presentSumMs; /* and their present passes: on a driver whose stamps do
                            not wait (a Mali) the display wait lands here */
    double boundMs;
} SlowSceneBlock;

/* The mean GPU work a replay: (span less the display wait less the present
   pass) over gpuN, 0 with no timed replay and never below 0.  The present
   pass is counted out because on a driver whose timestamps run ahead of the
   acquire wait (the Mali of issue 179) the wait lands in the present figure,
   not in the display wait: a display-bound block must not write the file */
double window_slow_scene_work_ms(const SlowSceneBlock *b);
/* true when the block is the dump's: a window up, no movie, the dominant
   kind a scene, the bound on, not yet written, 30 s of uptime, at least 8
   timed replays and the work over the bound (not at it) */
bool window_slow_scene_due(const SlowSceneBlock *b);
/* "slow scene: the block's GPU work 52.3 ms a replay (bound 40; display
   wait 3.1, span 55.4) in a scene layout: frame 4812 written to
   logs/slow-20261106-120000.zip; attach it with the log" (the name is the
   file name alone); the length written */
size_t window_slow_scene_text(char *out, size_t cap, const SlowSceneBlock *b, unsigned frame,
                              const char *zipName);

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

/* The block's timed replays as (VU triangles, list 0's GPU ms) pairs, for
   a least-squares line: list 0 = a + b x (x in thousands of triangles).
   Sums, no ring: a block's few hundred replays in doubles */
typedef struct SceneVuFit {
    unsigned n;
    double sx, sy, sxx, sxy, syy;
} SceneVuFit;

void window_scene_vu_fit_add(SceneVuFit *f, uint64_t tris, double list0Ms);
/* The line's a (ms), b (ms a thousand triangles) and r2 (the share of
   list 0's variance the triangles explain; -1 when list 0 did not vary):
   false with fewer than 3 replays or the triangles' count not varying
   (no slope to be had) */
bool window_scene_vu_fit(const SceneVuFit *f, double *a, double *b, double *r2);
/* "; list 0 GPU 41.2 ms a replay, 12.3 ms + 0.41 ms a thousand triangles
   (R^2 0.80, 38 replays)": the mean, then the line where it is had; "" with
   no replay timed.  b near 0 with a high R^2 is not possible; a high b with
   a high R^2 says the vertex work (the VU draws' triangles) sets list 0's
   time, a low R^2 that something else does (the pixels, the passes).  The
   length written */
size_t window_scene_vu_fit_text(char *out, size_t cap, const SceneVuFit *f);

/* " (images: 10, in use 1)": the swapchain's images and those acquired and
   not yet handed back to the presentation engine; "" with no image count
   (a backend without one); the length written */
size_t window_scene_images_text(char *out, size_t cap, uint64_t images, uint64_t inUse);

#endif /* ICO_PLATFORM_WINDOW_SCENE_H */

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
 *     invocations (pre-pass off: 9 a triangle)", the skin check's twins'
     share ("; twins 42 draws, 76k vertex invocations a picture", issue
     123), and (issue 179) what the
 *     GPU's work (the span less the display wait and the present) cost in
 *     the block's timed replays against their vertex invocations: "; GPU
 *     work 51.8 ms a replay, 16.0 ms + 0.17 ms a thousand vertex
 *     invocations (R^2 0.81, 70 replays)";
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
   the figures of a 10 s block, and whether this block is one of the run's
   cutscene dumps (up to SLOW_SCENE_DUMPS, each from a block at least
   SLOW_SCENE_STEP times as heavy as the one before).  boundMs is [dev]
   slow_scene_dump_ms (0 or less: off); the sums are the block's GPU
   figures, from which the mean work less the display wait is compared with
   the bound. */
#define SLOW_SCENE_UPTIME_MIN_S 30.0
#define SLOW_SCENE_GPU_MIN 8
#define SLOW_SCENE_DUMPS 3
#define SLOW_SCENE_STEP 1.5

typedef struct SlowSceneBlock {
    SceneKind dominant; /* window_scene_dominant of the block */
    int window;         /* a window is up */
    int movie;          /* a movie is playing */
    int count;          /* the dumps this run has written so far */
    double lastWorkMs;  /* the work (window_slow_scene_work_ms) of the block of the last one */
    double uptimeS;
    unsigned gpuN;       /* the block's timed replays */
    double spanSumMs;    /* their GPU spans summed */
    double waitSumMs;    /* and their display waits */
    double presentSumMs; /* and their present passes: on a driver whose stamps do
                            not wait (a Mali) the display wait lands here */
    double boundMs;
    int stage;     /* the game's stage_no at the block's end */
    int lastStage; /* the stage of the last dump's block (unused with count 0) */
} SlowSceneBlock;

/* The mean GPU work a replay: (span less the display wait less the present
   pass) over gpuN, 0 with no timed replay and never below 0.  The present
   pass is counted out because on a driver whose timestamps run ahead of the
   acquire wait (the Mali of issue 179) the wait lands in the present figure,
   not in the display wait: a display-bound block must not write the file */
double window_slow_scene_work_ms(const SlowSceneBlock *b);
/* true when the block is a dump's: a window up, no movie, the dominant
   kind a scene, the bound on, 30 s of uptime, at least 8 timed replays, the
   work over the bound (not at it) and either no dump yet this run or fewer
   than SLOW_SCENE_DUMPS with the work at least SLOW_SCENE_STEP times the
   last dump's block (so a run of climbing blocks ends with the worst one's
   file, the zips being pruned to the newest three); or a later dump (fewer
   than SLOW_SCENE_DUMPS) whose block is in another stage than the last
   dump's (window_slow_scene_new_stage), at any work over the bound: the
   first slow block of each stage gets a file (issue 179: the cage, 90.9 ms,
   after the boat's 99.7 ms, which the 1.5 times rule alone refused) */
bool window_slow_scene_due(const SlowSceneBlock *b);
/* true when a dump has been made this run and the block's stage differs from
   the last dump's block's: the block is the first slow one of its stage */
bool window_slow_scene_new_stage(const SlowSceneBlock *b);
/* Whether a replay is the block's heavy part: its CPU-side vertex work
   (window_scene_vu_invocations of its own counts) at or above the block's
   mean of that a replay (the frame the dump is to hold is the first such
   replay after the block's end, window_host.c slow_scene_poll).  False for
   a work or a mean that is not above 0 */
bool window_slow_scene_pick(double work, double meanWork);
/* "slow scene: the block's GPU work 52.3 ms a replay (bound 40; display
   wait 3.1, present 2.0, span 55.4) in a scene layout: frame 4812 (its
   vertex work 682.1 thousand against the block's mean 598.0 thousand)
   written to logs/slow-20261106-120000.zip (dump 2 of 3 this run); attach
   it with the log" (a new-stage block reads "in a scene layout (the first
   slow block of stage 40): frame ..."; the works in vertex invocations, printed in thousands;
   the name is the file name alone; dump is count + 1).  The length
   written */
size_t window_slow_scene_text(char *out, size_t cap, const SlowSceneBlock *b, unsigned frame,
                              double frameWork, double meanWork, const char *zipName);

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

/* The skin check's twins in a replay, averaged over replays (n): the draws
   they added and their vertex invocations (issue 123: what the check costs
   a picture), appended to the VU line; "" with no twin drawn:
     "; twins 42 draws, 76k vertex invocations a picture" */
size_t window_scene_twins_text(char *out, size_t cap, uint64_t draws, uint64_t invocations,
                               unsigned n);

/* The vertex invocations of a replay's VU draws: the pre-pass's vertices
   with it on, 9 a triangle with it off (the VU line's own quantity) */
uint64_t window_scene_vu_invocations(uint64_t tris, uint64_t preVertices, int prepass);

/* A replay's GPU work: its span less the display wait and the present (the
   slow-scene rule's quantity), never negative.  The stamps inside the span
   are not used: on a tile-based GPU the "before list 0" and list 0 figures
   trade at a constant sum (issue 179) */
double window_scene_gpu_work_ms(double spanMs, double waitMs, double presentMs);

/* The block's timed replays as (vertex invocations, GPU work ms) pairs, for
   a least-squares line: work = a + b x (x in thousands of invocations).
   Sums, no ring: a block's few hundred replays in doubles */
typedef struct SceneVuFit {
    unsigned n;
    double sx, sy, sxx, sxy, syy;
    double xMin, xMax; /* the smallest and largest x (thousands of invocations) */
} SceneVuFit;

void window_scene_vu_fit_add(SceneVuFit *f, uint64_t invocations, double workMs);
/* The line's a (ms), b (ms a thousand invocations) and r2 (the share of the
   work's variance the invocations explain; -1 when the work did not vary):
   false with fewer than 3 replays or the invocations' count not varying
   (no slope to be had), or the invocations' largest not at least twice the
   smallest (a cutscene's replays: the line would be noise read as a cost) */
bool window_scene_vu_fit(const SceneVuFit *f, double *a, double *b, double *r2);
/* "; GPU work 51.8 ms a replay, 16.0 ms + 0.17 ms a thousand vertex
   invocations (R^2 0.81, 70 replays)": the mean, then the line where it is
   had; "" with no replay timed.  A high b with a high R^2 says the vertex
   work sets the GPU's time, a low R^2 that something else does (the
   pixels, the passes).  The length written */
size_t window_scene_vu_fit_text(char *out, size_t cap, const SceneVuFit *f);

/* "acquire: the display's image was free at the present's submit in 41 of 60
   pictures": the pictures whose present list's submit found the acquire's
   fence signalled (RhiStats acquireReadyAtSubmit) of those it asked
   (that and acquireNotReady); "" with none asked (every backend but Vulkan).
   The length written */
size_t window_scene_acquire_text(char *out, size_t cap, uint64_t ready, uint64_t notReady);
/* window_tiler_words' text (window_lines.c, the end of the 10 s GPU lines):
   on a tile-based GPU (tiler nonzero) where a pass's time and the display
   wait land, ending ": the stretch before list 0 is the uploads and the
   GPU's work still in flight"; "" elsewhere */
const char *window_scene_tiler_words(int tiler);
/* The VU pre-pass's part of the "GPU ms per replay" line: its GPU ms a
   replay ("%.2f") with it on, the word "off" with it off (a 0.00 read as a
   cost).  The length written */
size_t window_scene_prepass_gpu_text(char *out, size_t cap, int on, double msPerReplay);
/* A block's share of a counter that only grows, from its value now and its
   value at the block before: now less that, or now itself when now is below
   it (the backend was made again and its counters started from 0) */
uint64_t window_scene_counter_delta(uint64_t now, uint64_t snapshot);

/* " (images: 10, in use 1)": the swapchain's images and those acquired and
   not yet handed back to the presentation engine; "" with no image count
   (a backend without one); the length written */
size_t window_scene_images_text(char *out, size_t cap, uint64_t images, uint64_t inUse);

#endif /* ICO_PLATFORM_WINDOW_SCENE_H */

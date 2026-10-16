/*
 * port/platform/test/diag_test.c
 *
 * The diagnostics (diag_host.h), each case in a child process whose log is
 * then read back:
 *   - segv:     a game thread (fiber) dereferences NULL: exit 3, the log
 *               names the signal, the faulting address, the thread and its
 *               last kernel call, and lists stack candidates;
 *   - segvnote: the same with the activity note set (a pipeline build):
 *               "while: <note>" right after the CRASH line, and the
 *               graphics driver crash named, with no relaunch (the child
 *               never prepared one); segvcleared: a cleared note is not
 *               printed; segvworker: a batch of the start-up set's note
 *               and the fault on another OS thread (a worker compiling a
 *               pipeline, rhi_create_pipelines): the same report;
 *   - abort:    an assertion through ico_assert: exit 3 and the failure
 *               message in the report;
 *   - watchdog: a game thread spins without a kernel call, so the host loop
 *               never runs again: the watchdog (1 s) stops the run with
 *               exit 4, samples where the main thread is and dumps the
 *               threads, and the heartbeat reported "no progress" before;
 *   - progress: the Main tick stands still (a movie plays inside one) but
 *               the progress count moves for longer than the later limit:
 *               no fire; then both stop and the watchdog fires naming the
 *               movie, and the heartbeat printed the count and the
 *               start-up set's pipelines;
 *   - stall:    the tick stands still and nothing else moves: the later
 *               limit fires;
 *   - present:  with an effects program set, a fake present of 4 s inside a
 *               2 s later limit (the tick stands still) does not fire (only
 *               the part above 1 s is excused); the stall after it does,
 *               2 s after the present ended; a long present before the first
 *               tick does not trip a 2 s first limit either; without the
 *               effects program a 2.5 s present fires inside it;
 *   - pump:     the main thread inside a wait the system holds (the
 *               window's event pump, ico_diag_os_wait_enter; issue 105) for
 *               3.5 s against a 1 s later limit (the player's 70 s against
 *               60 s, scaled): no fire; "has not returned for 1 s (the
 *               system is holding it: the game is not hung); waiting" once,
 *               then "returned after 3.5 s"; 0.4 s after it with the tick
 *               still standing (the next step has not run yet) no fire
 *               either; then the tick moves and the run ends with exit 0;
 *               pumphang: the same, then the game's own code stands still:
 *               the later limit fires 1 s after it, so the excuse does not
 *               leak into a game-side stall; pumpbound: a wait that never
 *               returns ends the run at the bound for one wait (3 s here,
 *               ico_diag_set_os_wait_limit; 600 s by default, which the
 *               watchdog's start line names) with the crash-style report;
 *               pumpearly, pumpoff: that bound (1 s) with no limit in
 *               force (the reporter before the limits, Android's start-up
 *               set; the watchdog off) does not end a 2.5 s wait;
 *   - lines:    milestones and logs reach the file at once;
 *   - activity: the start-up activity file (issue 93): written with each
 *               note while the start-up set is created (a batch's note,
 *               put back after the progress screen's own pipeline in the
 *               middle of it) and kept when the run just ends; the next start names it ("the previous run
 *               ended while"), deletes it and is lazy; a lazy run keeps its
 *               notes and a line between them, and keeps the file at a
 *               clean exit (the next start is lazy); a normal end of the
 *               set deletes it; a restart after a driver crash logs it but
 *               is not lazy; a file another version wrote is not lazy; a
 *               lazy run's clean exit is logged as such at the next start,
 *               not as an ended run; a file cut short after a write (the
 *               NUL padding) reads as the new note;
 *   - loss:     the restart after a device loss (ico_diag_device_lost_restart)
 *               with the relaunch prepared and stubbed (no new process):
 *               lossstep, the first loss: the blend step, the restarted
 *               run's ICO_STARTUP_RETRY=blend:<backend> (which draws with
 *               the fixed-function blend, the value host_config.c exports
 *               ICO_RD_BLEND=fixed for), handed over at exit after the
 *               normal quit path's steps; lossbackend, the next loss on a
 *               build with D3D12: backend:d3d12; lossnone, no step left:
 *               0 (the window's box), nothing at exit; lossunprepared, no
 *               relaunch prepared: 0; lossfail, the relaunch fails: the box
 *               saying the game could not start itself again;
 *   - lossdump: the loss bisect's file steps (ico_diag_note_device_loss,
 *               ico_diag_loss_bisect) with the bisect stubbed: a loss
 *               noted on a run at the restart after a device loss (the
 *               relaunch stubbed) leaves "loss dump: <zip>" in the activity
 *               file through the run's end; the next start names it, runs
 *               the bisect once before the game and logs its line (a lost
 *               pass, a clean frame, a bisect that could not run) and
 *               deletes the file; a start ended inside the bisect leaves the
 *               bisect's note, and the start after names it, is not lazy and
 *               does not bisect again; a lazy run's loss keeps the lazy note
 *               beside the loss (the next start is lazy and bisects); a
 *               run killed after the loss still leaves it; a loss with no
 *               dump is named and not bisected; a loss from another version
 *               is named and not bisected;
 *   - bisect2:  the loss bisect's second level (ico_diag_loss_bisect2) with
 *               its step stubbed (actbisect2, ICO_TEST_BISECT2: a verdict a
 *               step, lost, clean, die (the run ends inside the step),
 *               noinit (its renderer did not start), notrun; past the list
 *               an oracle, draw ICO_TEST_BISECT2_D of 1,312 faults with
 *               every draw before it): the draw found and named with its
 *               verdict in 4 starts (at most 3 lost steps a start, then
 *               "step 5 waits for the next start"), the state file
 *               deleted; a start killed at step 3 continues at step 4 at the next (the step
 *               counted as killed); noinit stops the start and the step
 *               runs again at the next; the 16-step and 4-start bounds; the
 *               time budget; a state file that does not parse (garbage, a
 *               line too long, numbers out of range, a step out of order)
 *               or from another version is deleted;
 *   - early:    the reporter started before the limits (Android's start):
 *               the first limit counts from ico_diag_start, not from the
 *               thread's start;
 *   - symbols:  the symbolizer (symmap.h) on a small GNU ld map of an ELF
 *               program (a wrapped function section, a static function in
 *               an object, an anchor that is not at the module's start), a
 *               Windows map excerpt (image base 0x140000000, a
 *               libX.a(obj.c.obj) object), an nm table, the same table
 *               gzip-compressed (larger than inflate's 32 KB window), and a
 *               map without the anchor; then the segv and watchdog cases'
 *               symbol lines from diag_test's own link map (diag_test.map,
 *               linked beside it): sched.c's functions named, the test's
 *               static functions as its object's;
 *   - report:   the segv and watchdog cases write logs/report-<stamp>.zip
 *               (report.h), read back with miniz: report.txt names the end,
 *               the summary line, the report block with its symbol lines,
 *               and ico-pc.log is the log; the crash's report.txt is
 *               written to diag_test_report.txt (an example); the prune
 *               keeps the newest 5;
 *   - limited:  ico_log_limited: 250 calls write the first 8 and the 100th
 *               and 200th, the 8th saying so, and the end summary counts
 *               250 (10 written).
 * POSIX only (fork); elsewhere it reports skipped (77).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <eekernel.h>
#include "diag_host.h"
#include "miniz.h"
#include "rd_bisect_plan.h"
#include "report.h"
#include "sched.h"
#include "startup_retry.h"
#include "symmap.h"

#if !defined(_WIN32)

#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#endif

void ico_assert(const char *file, int line, const char *failedexpr);

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static unsigned int fake_ticks;

static unsigned int fake_vsyncs;

static volatile int spin_forever = 1;

static unsigned int get_ticks(void)
{
    return fake_ticks;
}

static unsigned int get_vsyncs(void)
{
    return fake_vsyncs;
}

static void status(char *out, size_t size)
{
    snprintf(out, size, "vsync %u, tick %u", fake_vsyncs, fake_ticks);
}

static void exit_hook(const char *reason)
{
    ico_diag_log("test: exit hook (%s)", reason);
}

static int sema;

/* the store to NULL is the point: UBSan (the asan preset) must let it
   fault so the crash report is what is tested */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((no_sanitize("undefined")))
#endif
static void segv_thread(void *arg)
{
    volatile int *p = (volatile int *)arg;
    ico_diag_milestone("segv thread runs");
    PollSema(sema);
    *p = 1; /* NULL */
}

/* the start-up set's batch note (port/render/rd_pipeline.c
   rd_precreate_pipelines) */
#define BATCH_NOTE                                                                                 \
    "creating pipelines 1-4 of 9: program 5 blend 1 vs 4 fs 5 fmt 1/8 msaa 0 dst 0 to program 6 "  \
    "blend 1 vs 7 fs 19 fmt 1/8 msaa 0 dst 0"

/* a batch of the VU vertex shaders' extended variants (issue 107;
   rd_pipeline.c keyWords " ext 1", batchModules) and one of the plain ones */
#define VU_EXT_NOTE                                                                                \
    "creating pipelines 329-360 of 702: program 0 blend 12 vs 4 fs 5 fmt 1/8 msaa 0 dst 0 ext 1 "  \
    "to program 0 blend 4 vs 4 fs 19 fmt 1/8 msaa 0 dst 0 ext 1; vertex vu_prelit_vs 50325 "       \
    "words; largest fragment vu_texa_ps 21000 words"
/* a VU batch's compile-time lines (rd.h rd_set_batch_fn) */
#define VU_TIMES_1                                                                                 \
    "VU batch 300-331 of 702 (plain, compiler E031.41.03.47): 32 pipelines 0.20-0.90 s, mean "     \
    "0.41 s, 3.2 s on 4 threads; vu_prelit_plain_vs 22392 words 4100 instructions: 32 x "          \
    "0.20-0.90 s mean 0.41 s"
#define VU_TIMES_2                                                                                 \
    "VU batch 332-340 of 702 (plain, compiler E031.41.03.47): 9 pipelines 0.30-1.10 s, mean "      \
    "0.52 s, 1.4 s on 4 threads; vu_lit_plain_vs 23305 words 4300 instructions: 9 x 0.30-1.10 "    \
    "s mean 0.52 s"
#define VU_PLAIN_NOTE                                                                              \
    "creating pipelines 297-328 of 702: program 0 blend 12 vs 4 fs 5 fmt 1/8 msaa 0 dst 0 ext 0 "  \
    "to program 0 blend 4 vs 4 fs 19 fmt 1/8 msaa 0 dst 0 ext 0; vertex vu_prelit_plain_vs "       \
    "22392 words; largest fragment vu_texa_ps 21000 words"

/* issues 93 and 107: a batch of the set's 2D half (v0.7.7 ended inside one
   on both Adreno 740 devices) and a finished one's line with the memory and
   the pipeline cache's size (rd_pipeline.c batchTimes), and a lazy batch's
   line (rd_pipelines_lazy) */
#define NOTE_2D                                                                                    \
    "creating pipelines 193-224 of 527: program 3 blend 0 vs 1 fs 7 fmt 1/8 msaa 0 dst 0 to "      \
    "program 9 blend 2 vs 1 fs 7 fmt 1/0 msaa 0 dst 0; vertex sprite_vs 900 words (210 "           \
    "instructions); largest fragment font_sheet_ps 70444 words"
#define TIMES_2D                                                                                   \
    "2D batch 161-192 of 527 (compiler E031.41.03.47): 32 pipelines 0.02-0.21 s, mean 0.08 s, "    \
    "0.9 s on 8 threads; memory 185 MB (most 194 MB), pipeline cache 3104 KB; slowest 0.21 s "     \
    "program 3 blend 0 vs 1 fs 7 fmt 1/8 msaa 0 dst 0 (sprite_vs 900 words + font_sheet_ps "       \
    "70444 words)"
#define TIMES_LAZY                                                                                 \
    "lazy batch 3, pipelines 41-57 of this run (compiler E031.41.03.47): 17 pipelines "            \
    "0.02-0.61 s, mean 0.14 s, over 1.8 s; memory 210 MB (most 214 MB), pipeline cache 3104 "      \
    "KB; slowest 0.61 s program 3 blend 0 vs 1 fs 7 fmt 1/8 msaa 0 dst 0 (sprite_vs 900 words + "  \
    "font_sheet_ps 70444 words)"

#if !defined(_WIN32)
/* an OS thread that is not a game thread faults, as a worker inside the
   graphics driver's pipeline compile would */
static void *segv_worker(void *arg)
{
    volatile int *p = (volatile int *)arg;
    ico_diag_milestone("segv worker runs");
    *p = 1; /* an unmapped address */
    return NULL;
}
#endif

static void abort_thread(void *arg)
{
    (void)arg;
    ico_assert("diag_test.c", 42, "the test fails here");
}

static void spin_thread(void *arg)
{
    (void)arg;
    SleepThread(); /* a note, then woken */
    while (spin_forever) {}
}

/* the child's own pace, on its main thread (POSIX only runs the children).
   The watchdog's sample signals this thread and cuts a sleep short, so it
   sleeps again until the time has passed. */
static void pause_ms(unsigned int ms)
{
#if defined(_WIN32)
    (void)ms;
#else
    const double end = ico_diag_uptime() + ms / 1000.0;
    double now;
    while ((now = ico_diag_uptime()) < end) {
        usleep((useconds_t)((end - now) * 1e6) + 1u);
    }
#endif
}

/* A movie's shape (progress != 0): ticks stopped at 1, progress noted
   every 50 ms for 2.5 s against a 1 s later limit, then nothing until the
   watchdog ends the run (exit 4) or 5 s pass (exit 0: it never fired).
   Without progress, only the stopped tick. */
static void progress_child(int progress)
{
    int i;
    fake_ticks = 1;
    ico_diag_start(0, 1);
    if (progress) {
        ico_diag_set_movie(1);
        /* the start-up set's count in the heartbeat */
        ico_diag_set_pipelines(12, 527, 1310);
        /* the batch in flight, repeated by the heartbeat (issue 107) */
        ico_diag_set_activity(VU_EXT_NOTE);
    }
    for (i = 0; progress && i < 50; i++) {
        pause_ms(50);
        ico_diag_note_progress();
    }
    if (progress) {
        ico_diag_log("test: progress kept it alive (%u)", ico_diag_progress());
    }
    pause_ms(5000);
    exit(0);
}

/* A present (ico_diag_present_enter/leave) against a limit.
   effects != 0: an effects program is set; a 4 s present against a 2 s limit
   (3 s of it excused). first != 0: the first limit, before any tick; the run
   must end with exit 0. Otherwise the later limit: it must not fire inside
   the present and must after it.
   effects == 0: a 2.5 s present against a 1 s later limit; nothing is
   excused, so the watchdog fires inside it. */
static void present_child(int first, int effects)
{
    const unsigned int lim = effects ? 2 : 1;
    fake_ticks = first ? 0 : 1;
    ico_diag_set_effects_program(effects);
    ico_diag_start(first ? lim : 0, first ? 0 : lim);
    ico_diag_present_enter();
    pause_ms(effects ? 4000 : 2500);
    ico_diag_present_leave();
    ico_diag_log("test: present done");
    if (first) {
        pause_ms(300);
        exit(0);
    }
    pause_ms(5000);
    exit(0);
}

/* The window's event pump held by the system (issue 105), scaled: 3.5 s
   inside ico_diag_os_wait_enter against a 1 s later limit, then 0.4 s
   with the tick still standing (the step after the pump has not run yet),
   then a tick. mode "pump": exit 0 after it; "pumphang": the game's code
   then stands still until the watchdog ends the run; "pumpbound": the
   bound for one wait is 3 s and the wait never returns (6 s). */
/* the window's description of the wait (pump_watch_describe there) */
static void pump_describe(char *out, size_t size)
{
    snprintf(out, size, "in the test's pump, handling WM_TEST(0x0400)");
}

static void pump_child(const char *mode)
{
    const int bound = strcmp(mode, "pumpbound") == 0;
    if (strcmp(mode, "pumpearly") == 0 || strcmp(mode, "pumpoff") == 0) {
        /* the bound (1 s here) with no limit in force: Android's reporter
           before the game (the start-up set's progress pump), or the
           watchdog off; a 2.5 s wait does not end the run */
        ico_diag_set_os_wait_limit(1);
        if (strcmp(mode, "pumpearly") == 0) {
            ico_diag_start_reporter();
        } else {
            ico_diag_start(0, 0);
        }
        ico_diag_os_wait_enter("the window's event pump");
        pause_ms(2500);
        ico_diag_os_wait_leave();
        ico_diag_log("test: pump returned");
        exit(0);
    }
    fake_ticks = 1;
    if (bound) {
        ico_diag_set_os_wait_limit(3);
    }
    ico_diag_set_os_wait_describe(pump_describe);
    ico_diag_start(0, 1);
    ico_diag_os_wait_enter("the window's event pump");
    pause_ms(bound ? 6000 : 3500);
    ico_diag_os_wait_leave();
    ico_diag_log("test: pump returned");
    pause_ms(400);
    fake_ticks++;
    pause_ms(500);
    fake_ticks++;
    ico_diag_log("test: game resumed");
    if (strcmp(mode, "pumphang") == 0) {
        pause_ms(5000);
    }
    exit(0);
}

/* a child's end without the clean-up, as a killed process ends */
#if defined(_WIN32)
#define child_quit(c) exit(c)
#else
#define child_quit(c) _exit(c)
#endif

/* the activity file beside the log */
static void act_path(char *out, size_t size, const char *log)
{
    snprintf(out, size, "%s.act", log);
}

/* the loss bisect, stubbed (ICO_TEST_BISECT: lost, clean, die, or a
   bisect that could not run) */
static int bisect_stub(const char *zip, char *line, size_t size, void *user)
{
    const char *how = getenv("ICO_TEST_BISECT");
    (void)user;
    ico_diag_log("test: bisect stub given %s", zip);
    if (how != NULL && strcmp(how, "die") == 0) {
        _exit(0); /* the device or the system ends the run inside it */
    }
    if (how != NULL && strcmp(how, "lost") == 0) {
        snprintf(line, size,
                 "pass 12 of 61, list 3, pipeline 409 (vs 7 fs 5 dst 0), pass colour "
                 "4 (640x448), 14 draws");
        return ICO_DIAG_BISECT_LOST;
    }
    if (how != NULL && strcmp(how, "clean") == 0) {
        snprintf(line, size, "the whole frame (61 passes) ran without losing the device");
        return ICO_DIAG_BISECT_CLEAN;
    }
    snprintf(line, size, "the renderer did not start");
    return ICO_DIAG_BISECT_NOT_RUN;
}

/* the second level's step, stubbed (diag_host.h IcoBisect2StepFn) */
static int bisect2_stub(const char *zip, uint32_t pass, uint32_t number, const RdBisectStep *st,
                        RdBisectKey *key, char *note, size_t size, double *ms, void *user)
{
    const char *list = getenv("ICO_TEST_BISECT2");
    const char *d = getenv("ICO_TEST_BISECT2_D");
    const char *msv = getenv("ICO_TEST_BISECT2_MS");
    const uint32_t D = d != NULL ? (uint32_t)atoi(d) : 731u;
    char how[16] = "";
    uint32_t k = 1;
    (void)user;
    (void)pass;
    /* the number-th word of the list */
    for (const char *p = list != NULL ? list : ""; *p != '\0'; k++) {
        size_t n = strcspn(p, ",");
        if (k == number && n < sizeof how) {
            memcpy(how, p, n);
            how[n] = '\0';
        }
        p += n;
        p += *p == ',';
    }
    ico_diag_log("test: bisect2 step %u given %s", (unsigned)number, zip);
    *ms = msv != NULL ? atof(msv) : 600.0;
    key->valid = 1;
    key->vu = 1;
    key->vs = 5;
    key->clip = 0;
    key->skinned = 0;
    key->tex = 65537;
    snprintf(note, size, "list 0 command %u, mesh st26a_p1, vs 5 fs 5", (unsigned)st->drawLast);
    if (strcmp(how, "die") == 0) {
        _exit(0); /* the device or the system ends the run inside the step */
    }
    if (strcmp(how, "noinit") == 0) {
        return ICO_DIAG_BISECT2_NOINIT;
    }
    if (strcmp(how, "notrun") == 0) {
        return ICO_DIAG_BISECT2_NOT_RUN;
    }
    if (strcmp(how, "lost") == 0) {
        return ICO_DIAG_BISECT2_LOST;
    }
    if (strcmp(how, "clean") == 0) {
        return ICO_DIAG_BISECT2_CLEAN;
    }
    /* the oracle: masked, alone and the classes lose too */
    if (st->kind == RD_BISECT_STEP_PREFIX || st->kind == RD_BISECT_STEP_AGAIN) {
        return st->drawLast >= D ? ICO_DIAG_BISECT2_LOST : ICO_DIAG_BISECT2_CLEAN;
    }
    return ICO_DIAG_BISECT2_LOST;
}

/* window_host.c pipeline_set_persist_lazy, stubbed: the config write
   logged; ICO_TEST_PERSIST_FAIL makes it fail */
static int persist_lazy_stub(void)
{
    ico_diag_log("test: config write dev.pipeline_set = lazy");
    return getenv("ICO_TEST_PERSIST_FAIL") != NULL ? -1 : 0;
}

/* window_host.c vu_plain_persist, stubbed the same way */
static int persist_vu_stub(void)
{
    ico_diag_log("test: config write dev.vu_plain_only = true");
    return getenv("ICO_TEST_PERSIST_FAIL") != NULL ? -1 : 0;
}

/* The run after the policy (ICO_TEST_THEN): "die" ends it without a word
   inside a pipeline ("dievu": an extended VU batch), "between" between two
   creations of a lazy run ("betweenbg": there in the background), "clean" ends it cleanly (a lazy run's exit, or
   the set finished), "cleanvu" the set finished after an extended VU
   batch; unset: no run (the file stays deleted) */
static void pipeset_then(int set)
{
    const char *then = getenv("ICO_TEST_THEN");
    const int lazy = set != ICO_DIAG_PIPESET_FULL;
    if (then == NULL) {
        return;
    }
    ico_diag_startup_activity_record(lazy ? ICO_DIAG_ACTIVITY_LAZY : ICO_DIAG_ACTIVITY_STARTUP);
    if (strcmp(then, "die") == 0) {
        ico_diag_set_activity(lazy ? "creating pipeline fog test" : NOTE_2D);
        _exit(0);
    } else if (strcmp(then, "dievu") == 0) {
        ico_diag_set_activity(VU_EXT_NOTE);
        _exit(0);
    } else if (strcmp(then, "between") == 0 || strcmp(then, "betweenbg") == 0) {
        ico_diag_set_activity("creating pipeline fog test");
        ico_diag_clear_activity();
        if (strcmp(then, "betweenbg") == 0) {
            ico_diag_startup_activity_background(1);
        }
        _exit(0);
    } else if (strcmp(then, "cleanvu") == 0) {
        ico_diag_set_activity(VU_EXT_NOTE);
    } else if (lazy) {
        ico_diag_set_activity("creating pipeline fog test");
        ico_diag_clear_activity();
    }
    if (lazy) {
        ico_diag_startup_activity_exit();
    } else {
        ico_diag_startup_activity_record(0);
    }
}

/* The start-up activity file's steps, each ending as a killed process
   would (_exit: no clean-up) unless the step ends cleanly */
static void activity_child(const char *mode, const char *log)
{
    char path[300];
    int lazy, r2;
    act_path(path, sizeof path, log);
    ico_diag_set_version(getenv("ICO_TEST_ACT_VERSION") != NULL ? getenv("ICO_TEST_ACT_VERSION")
                                                                : "test 1");
    lazy = ico_diag_startup_activity_begin(path, strcmp(mode, "actrelaunch") == 0 ||
                                                     strcmp(mode, "actpipesetrelaunch") == 0);
    ico_diag_log("test: lazy %d", lazy);
    /* what window_host.c asks of the previous note (issue 107) */
    ico_diag_log("test: previous vu ext %d",
                 startup_retry_note_has_vu_ext(ico_diag_startup_activity_prev()) ? 1 : 0);
    if (strcmp(mode, "actrecord") == 0) {
        ico_diag_startup_activity_record(ICO_DIAG_ACTIVITY_STARTUP);
        /* the set's own note before a batch is not written */
        ico_diag_set_activity("creating the start-up pipelines (1 of 9)");
        ico_diag_set_activity(BATCH_NOTE);
        /* the progress screen's own pipeline while the batch compiles,
           then the batch's note back */
        ico_diag_set_activity("creating pipeline program 7 blend 0 vs 1 fs 2 fmt 1/0 msaa 0 dst 0");
        ico_diag_set_activity(BATCH_NOTE);
        /* a frame between two batches clears the note: the file keeps the
           last one */
        ico_diag_clear_activity();
        child_quit(0);
    } else if (strcmp(mode, "actlazy") == 0) {
        ico_diag_startup_activity_record(lazy ? ICO_DIAG_ACTIVITY_LAZY : 0);
        ico_diag_set_activity("creating pipeline fog test");
        if (getenv("ICO_TEST_ACT_BETWEEN") != NULL) {
            ico_diag_clear_activity();
        }
        child_quit(0);
    } else if (strcmp(mode, "actlazyexit") == 0) {
        /* a lazy run's clean exit keeps the file */
        ico_diag_startup_activity_record(lazy ? ICO_DIAG_ACTIVITY_LAZY : 0);
        ico_diag_set_activity("creating pipeline fog test");
        ico_diag_clear_activity();
        ico_diag_startup_activity_exit();
        child_quit(0);
    } else if (strcmp(mode, "actloss") == 0) {
        /* a device loss on a run (lazy when the start is), its dump named
           by ICO_TEST_LOSS_ZIP (unset: none written); a clean exit, or a
           kill with ICO_TEST_LOSS_KILL */
        ico_diag_startup_activity_record(lazy ? ICO_DIAG_ACTIVITY_LAZY : 0);
        if (lazy) {
            ico_diag_set_activity("creating pipeline fog test");
            ico_diag_clear_activity();
        }
        ico_diag_note_device_loss(getenv("ICO_TEST_LOSS_ZIP"));
        if (getenv("ICO_TEST_LOSS_KILL") == NULL) {
            ico_diag_startup_activity_exit();
        }
        child_quit(0);
    } else if (strcmp(mode, "actbisect") == 0) {
        /* the next start: the bisect before the game, then the game's own
           record (the start-up set ended normally) */
        ico_diag_log("test: loss dump %s",
                     ico_diag_loss_dump() != NULL ? ico_diag_loss_dump() : "(none)");
        ico_diag_log("test: bisect %d", ico_diag_loss_bisect(bisect_stub, NULL));
        if (getenv("ICO_TEST_NO_RECORD") == NULL) {
            ico_diag_startup_activity_record(lazy ? ICO_DIAG_ACTIVITY_LAZY
                                                  : ICO_DIAG_ACTIVITY_STARTUP);
            if (!lazy) {
                ico_diag_startup_activity_record(0);
            }
        }
        child_quit(0);
    } else if (strcmp(mode, "actbisect2") == 0) {
        /* the second level: begun (ICO_TEST_BISECT2_BEGIN names the zip)
           after the first ended on pass 1 of 60 (1,312 draws), or the
           pending one continued */
        char st[300];
        const char *begin = getenv("ICO_TEST_BISECT2_BEGIN");
        const char *budget = getenv("ICO_TEST_BISECT2_BUDGET");
        const double b = budget != NULL ? atof(budget) : 90000.0;
        snprintf(st, sizeof st, "%s.bisect", log);
        ico_diag_loss_bisect2_path(st);
        if (begin != NULL) {
            r2 = ico_diag_loss_bisect2(begin, 1, 1312, 60, bisect2_stub, NULL, b);
        } else if (ico_diag_loss_bisect2_pending()) {
            r2 = ico_diag_loss_bisect2(NULL, 0, 0, 0, bisect2_stub, NULL, b);
        } else {
            r2 = -1;
        }
        ico_diag_log("test: bisect2 %d", r2);
        ico_diag_startup_activity_record(ICO_DIAG_ACTIVITY_STARTUP);
        ico_diag_startup_activity_record(0);
        child_quit(0);
    } else if (strcmp(mode, "actvuext") == 0 || strcmp(mode, "actvuplain") == 0) {
        /* the start-up set ended without a word inside a VU batch (the
           device froze): the batch's note is in the file */
        ico_diag_startup_activity_record(ICO_DIAG_ACTIVITY_STARTUP);
        ico_diag_set_activity(BATCH_NOTE);
        ico_diag_set_activity(strcmp(mode, "actvuext") == 0 ? VU_EXT_NOTE : VU_PLAIN_NOTE);
        _exit(0);
    } else if (strcmp(mode, "actvutimes") == 0) {
        /* the plain batches compiled (their times kept as the file's
           "compiled: " lines), then the device froze in an extended one */
        ico_diag_startup_activity_record(ICO_DIAG_ACTIVITY_STARTUP);
        ico_diag_set_activity(VU_PLAIN_NOTE);
        ico_diag_startup_activity_batch(VU_TIMES_1);
        ico_diag_startup_activity_batch(VU_TIMES_2);
        ico_diag_set_activity(VU_EXT_NOTE);
        _exit(0);
    } else if (strcmp(mode, "actvutimesdone") == 0) {
        /* the same set completed: no times are left for the next start */
        ico_diag_startup_activity_record(ICO_DIAG_ACTIVITY_STARTUP);
        ico_diag_set_activity(VU_PLAIN_NOTE);
        ico_diag_startup_activity_batch(VU_TIMES_1);
        ico_diag_startup_activity_record(0);
        child_quit(0);
    } else if (strcmp(mode, "act2dbatch") == 0) {
        /* issues 93 and 107: a 2D batch finished (its line with the
           memory), then the system ended the start in the next one; with
           ICO_TEST_LOW_MEMORY a low-memory signal came first (Android's
           onTrimMemory, level 15) */
        /* "early": the app went to the background before the recording
           began (Home during the renderer's start) */
        if (getenv("ICO_TEST_BACKGROUND") != NULL &&
            strcmp(getenv("ICO_TEST_BACKGROUND"), "early") == 0) {
            ico_diag_startup_activity_background(1);
        }
        ico_diag_startup_activity_record(ICO_DIAG_ACTIVITY_STARTUP);
        ico_diag_set_pipelines(192, 527, 210);
        ico_diag_startup_activity_batch(TIMES_2D);
        ico_diag_set_activity(NOTE_2D);
        ico_diag_set_pipelines(201, 527, 210);
        if (getenv("ICO_TEST_LOW_MEMORY") != NULL) {
            ico_diag_note_low_memory(15);
            ico_diag_startup_activity_poll();
        }
        /* the app went to the background there (Home, a swipe from the
           recent apps): "bg" the lifecycle's event, "trim20" Android's
           onTrimMemory(20) first (the line only in the mark: the held
           main thread did not poll), "back" to the background and back */
        if (getenv("ICO_TEST_BACKGROUND") != NULL &&
            strcmp(getenv("ICO_TEST_BACKGROUND"), "early") != 0) {
            const char *bg = getenv("ICO_TEST_BACKGROUND");
            if (strcmp(bg, "trim20") == 0) {
                ico_diag_note_low_memory(20);
            }
            ico_diag_startup_activity_background(1);
            if (strcmp(bg, "back") == 0) {
                ico_diag_startup_activity_background(0);
            }
        }
        _exit(0);
    } else if (strcmp(mode, "actlowlazy") == 0) {
        /* a run without the set: a lazy batch closed, then the system's
           signal between two creations, then the end */
        ico_diag_startup_activity_record(ICO_DIAG_ACTIVITY_LAZY);
        ico_diag_set_pipelines_made(57);
        ico_diag_startup_activity_batch(TIMES_LAZY);
        ico_diag_note_low_memory(80);
        ico_diag_startup_activity_poll();
        _exit(0);
    } else if (strcmp(mode, "actpipeset") == 0 || strcmp(mode, "actpipesetrelaunch") == 0) {
        /* the next start's policy (window_host.c ico_window_open): the
           plain VU variants, the set, then after the device is made the
           lines and settings of an end in a row (ICO_TEST_TABLE_LAZY: the
           device table's lazy rule, RhiLimits.lazyPipelines;
           ICO_TEST_TABLE_VU: its plain VU rule, RhiLimits.vuPlainOnly) */
        const int relaunch = strcmp(mode, "actpipesetrelaunch") == 0;
        const int vu = relaunch ? 0 : ico_diag_startup_vu_plain("config.toml");
        const int set = ico_diag_startup_pipeline_set(relaunch, vu > 0 ? 0 : lazy,
                                                      getenv("ICO_TEST_PIPESET"), "config.toml");
        if (set == ICO_DIAG_PIPESET_LAZY_ONCE || set == ICO_DIAG_PIPESET_LAZY_NOW) {
            (void)ico_diag_startup_end_persist(ICO_DIAG_END_PIPELINES,
                                               getenv("ICO_TEST_TABLE_LAZY"), persist_lazy_stub,
                                               "config.toml");
        }
        if (vu >= 1) {
            (void)ico_diag_startup_end_persist(ICO_DIAG_END_VU_EXT, getenv("ICO_TEST_TABLE_VU"),
                                               persist_vu_stub, "config.toml");
        }
        ico_diag_log("test: vu plain %d", vu);
        ico_diag_log("test: pipeline set %d", set);
        pipeset_then(set);
        child_quit(0);
    } else if (strcmp(mode, "actclean") == 0) {
        ico_diag_startup_activity_record(ICO_DIAG_ACTIVITY_STARTUP);
        ico_diag_set_activity("creating the start-up pipelines (1 of 9)");
        ico_diag_startup_activity_record(0);
        child_quit(0);
    }
    child_quit(0); /* actrelaunch, actnext */
}

/* main's program path, for the loss children's relaunch */
static const char *self_path = "diag_test";

/* the normal quit path's steps (main_host.c and the window register them
   with atexit after ico_diag_prepare_relaunch) */
static void loss_quit_steps(void)
{
    ico_diag_log("test: the end-of-run steps");
}

/* the relaunch, stubbed: the value, its step and backend as the restarted
   run reads them, and whether that run draws with the fixed-function
   blend (host_config.c ico_ini_export: ICO_RD_BLEND=fixed) */
static int loss_relaunch_stub(const char *value)
{
    StartupRetry step = STARTUP_RETRY_NONE;
    char backend[16] = "";
    const int ok = startup_retry_parse(value, &step, backend, sizeof backend);
    ico_diag_log("test: relaunch %s=%s (parse %d, step %d, backend %s, blend %s)",
                 STARTUP_RETRY_ENV, value, ok, (int)step, backend,
                 startup_retry_value_draws_fixed(value) ? "fixed" : "not fixed");
    return getenv("ICO_TEST_LOSS_FAIL") != NULL ? -1 : 0;
}

static void loss_box(const char *text)
{
    ico_diag_log("test: box: %s", text);
}

/* A device loss on a run at step `step`: the decision, then a normal exit
   (atexit: the quit steps, then the relaunch) */
static void loss_child(const char *mode, const char *log)
{
    static char *argv[4];
    int r;
    argv[0] = (char *)self_path;
    argv[1] = (char *)mode;
    argv[2] = (char *)log;
    argv[3] = NULL;
    if (strcmp(mode, "lossunprepared") != 0) {
        ico_diag_prepare_relaunch(3, argv);
    }
    ico_diag_set_relaunch_stub(loss_relaunch_stub);
    ico_diag_set_fatal_ui(loss_box, NULL);
    atexit(loss_quit_steps);
    if (strcmp(mode, "lossbackend") == 0) {
        ico_diag_set_startup_retry(STARTUP_RETRY_LOSS_BLEND, 1, 0, 0, "vulkan");
    } else if (strcmp(mode, "lossnone") == 0) {
        ico_diag_set_startup_retry(STARTUP_RETRY_LOSS_BLEND, 0, 0, 0, "vulkan");
    } else {
        ico_diag_set_startup_retry(STARTUP_RETRY_NONE, 0, 0, 0, "vulkan");
    }
    if (getenv("ICO_TEST_LOSS_ACT") != NULL) {
        /* the window's order: the activity file at the start, the loss dump
           noted before the restart decision */
        char path[300];
        act_path(path, sizeof path, log);
        ico_diag_set_version("test 1");
        ico_diag_startup_activity_begin(path, 0);
        ico_diag_note_device_loss(getenv("ICO_TEST_LOSS_ACT"));
    }
    /* a run at a loss step drew with the fixed-function blend */
    r = ico_diag_device_lost_restart(strcmp(mode, "lossbackend") == 0 ||
                                     strcmp(mode, "lossnone") == 0);
    ico_diag_log("test: restart %d", r);
    exit(0);
}

/* ico_diag_start_reporter first, the limits 1.5 s later: the first limit
   (1 s) has not fired 0.6 s after them, and fires after that */
static void early_child(void)
{
    ico_diag_start_reporter();
    pause_ms(1500);
    ico_diag_start(1, 0);
    pause_ms(600);
    ico_diag_log("test: not fired yet");
    pause_ms(5000);
    exit(0);
}

static void run_child(const char *mode, const char *log)
{
    int id;
    void (*fn)(void *) = NULL;

    ico_diag_init(log);
    ico_diag_set_sources(status, get_ticks, get_vsyncs);
    ico_diag_set_exit_hook(exit_hook);
    ico_diag_name_func((void *)segv_thread, "segv_thread");
    ico_diag_name_func((void *)spin_thread, "spin_thread");
    ico_diag_log("test: child %s", mode);
    if (getenv("ICO_TEST_REPORT_DIR") != NULL) {
        /* the report file (report.h) into the test's own folder */
        const char *dir = getenv("ICO_TEST_REPORT_DIR");

        ico_report_set_paths(dir, NULL, dir, NULL);
        ico_diag_set_version("test (commit 0)");
        ico_diag_set_summary("version test; platform the test; GPU none");
    }
    if (strcmp(mode, "limited") == 0) {
        for (unsigned int i = 1; i <= 250; i++) {
            ico_log_limited("test: limited", "test: limited line %u", i);
        }
        ico_log_limit_log_summary();
        exit(0);
    }
    ico_sched_reset();
    sema = ico_sched_create_sema(1, 1, 0, 0);
    if (strncmp(mode, "act", 3) == 0) {
        activity_child(mode, log);
    } else if (strncmp(mode, "loss", 4) == 0) {
        loss_child(mode, log);
    } else if (strcmp(mode, "early") == 0) {
        early_child();
    } else if (strncmp(mode, "pump", 4) == 0) {
        pump_child(mode);
    } else if (strcmp(mode, "segv") == 0) {
        fn = segv_thread;
    } else if (strcmp(mode, "segvnote") == 0) {
        /* a crash inside the graphics driver's pipeline build; this child
           never called ico_diag_prepare_relaunch, so it cannot start again */
        ico_diag_set_activity("creating pipeline program %d blend 0", 3);
        fn = segv_thread;
    } else if (strcmp(mode, "segvworker") == 0) {
#if !defined(_WIN32)
        pthread_t t;
        ico_diag_set_activity(BATCH_NOTE);
        /* a low unmapped address, not NULL: the undefined-behaviour
           sanitizer would report a store through NULL before the fault */
        if (pthread_create(&t, NULL, segv_worker, (void *)16) == 0) {
            pthread_join(t, NULL);
        }
        ico_diag_log("test: the worker did not fault");
        exit(0);
#endif
    } else if (strcmp(mode, "segvcleared") == 0) {
        ico_diag_set_activity("creating pipeline program 3");
        ico_diag_clear_activity();
        fn = segv_thread;
    } else if (strcmp(mode, "abort") == 0) {
        fn = abort_thread;
    } else if (strcmp(mode, "watchdog") == 0) {
        fn = spin_thread;
        ico_diag_start(1, 2);
    } else if (strcmp(mode, "progress") == 0 || strcmp(mode, "stall") == 0) {
        progress_child(strcmp(mode, "progress") == 0 ? 1 : 0);
    } else if (strcmp(mode, "present") == 0 || strcmp(mode, "presentfirst") == 0 ||
               strcmp(mode, "presentplain") == 0) {
        present_child(strcmp(mode, "presentfirst") == 0, strcmp(mode, "presentplain") != 0);
    } else {
        ico_diag_milestone("a milestone");
        exit(0);
    }
    id = ico_sched_boot(fn, NULL, 10);
    ico_host_thread_func(id, (void *)fn, 10);
    ico_sched_run();
    /* the spin thread slept: wake it as an interrupt would, then it spins
       inside ico_sched_run forever */
    ico_sched_interrupt_begin();
    ico_sched_wakeup(id, 1);
    ico_sched_interrupt_end();
    fake_vsyncs++;
    ico_sched_run();
    exit(0);
}

#if !defined(_WIN32)

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    static char buf[1 << 16];
    size_t n;
    if (f == NULL) {
        buf[0] = '\0';
        return buf;
    }
    n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static int run(const char *self, const char *mode, const char *log)
{
    pid_t pid;
    int st;
    remove(log);
    pid = fork();
    if (pid == 0) {
        execl(self, self, mode, log, (char *)NULL);
        _exit(99);
    }
    if (pid < 0 || waitpid(pid, &st, 0) != pid) {
        return -1;
    }
    return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
}

#endif

#if !defined(_WIN32)

/* --- The symbolizer on synthetic files --------------------------------------------- */

/* a GNU ld map of an ELF program: the anchor (ico_diag_init) at 0x2000 */
static const char elf_map[] =
    "Archive member included to satisfy reference by file (symbol)\n"
    "\n"
    "libico_game.a(main.c.o)       port/game/gamestate.c.o (pad)\n"
    "\n"
    "Discarded input sections\n"
    "\n"
    " .text          0x0000000000000000       0x10 dropped.c.o\n"
    "\n"
    "Linker script and memory map\n"
    "\n"
    "                0x0000000000000000                PROVIDE (__executable_start = "
    "SEGMENT_START (\"text-segment\", 0x0))\n"
    ".init           0x0000000000001000       0x17\n"
    " .init          0x0000000000001000        0x4 /usr/lib/crti.o\n"
    "                0x0000000000001000                _init\n"
    ".text           0x0000000000002000     0x1000\n"
    " *(.text .stub .text.* .gnu.linkonce.t.*)\n"
    " .text          0x0000000000002000      0x200 port/platform/CMakeFiles/p.dir/diag_host.c.o\n"
    "                0x0000000000002000                ico_diag_init\n"
    "                0x0000000000002100                ico_diag_log\n"
    " .text.a_static_function_whose_section_name_is_long_enough_to_wrap\n"
    "                0x0000000000002200       0x80 port/render/CMakeFiles/r.dir/rd_blur.c.o\n"
    " *fill*         0x0000000000002280        0x0 \n"
    " .text          0x0000000000002280      0x300 libico_game.a(vu0.c.o)\n"
    "                0x0000000000002300                _CopyVector\n"
    " .text          0x0000000000002580      0x100 statics.c.o\n"
    ".data           0x0000000000004000      0x100\n"
    " .data          0x0000000000004000      0x100 data.c.o\n"
    "                0x0000000000004010                some_data\n";

/* tools/package_win.sh's ico_pc_x64.map, cut down (the real lines' form) */
static const char win_map[] =
    "                0x0000000140000000                __image_base__ = 0x140000000\n"
    ".text           0x0000000140001000   0x2aa400\n"
    " *(SORT_NONE(.init))\n"
    " *(.text)\n"
    " .text          0x0000000140001000      0x430 /mingw/lib/crt2.o\n"
    "                0x00000001400013d0                WinMainCRTStartup\n"
    " .text          0x0000000140196000     0x21e0 libico_platform.a(diag_host.c.obj)\n"
    "                0x0000000140197930                ico_diag_set_movie\n"
    "                0x0000000140197940                ico_diag_set_exit_hook\n"
    "                0x0000000140197fc0                ico_diag_init\n"
    "                0x00000001401981b0                ico_diag_arm_vectored\n"
    " .text          0x00000001401981e0      0x190 libico_platform.a(hotkeys.c.obj)\n"
    "                0x0000000140198200                ico_hotkey_for\n"
    "                0x0000000140198220                ico_stats_period_ns\r\n";

static const char nm_table[] = "0000000000001000 T _init\n"
                               "0000000000002000 T ico_diag_init\n"
                               "0000000000002100 t static_fn\n"
                               "0000000000002180 D some_data\n"
                               "0000000000002200 W weak_fn\n";

static int resolve_mem(const char *text, size_t n, const char *anchor, uint64_t anchor_off,
                       IcoSymHit *h, int count)
{
    IcoSymSource src;
    IcoSymMem m;

    ico_sym_mem_source(&src, &m, text, n);
    return ico_sym_resolve(&src, anchor, anchor_off, h, count);
}

static const char *hit_text(const IcoSymHit *h)
{
    static char d[256];

    ico_sym_describe(h, d, sizeof d);
    return d;
}

/* text as a gzip member (RFC 1952), in a malloc block */
static unsigned char *gzip_of(const char *text, size_t n, size_t *out_n)
{
    size_t dn = 0;
    void *d = tdefl_compress_mem_to_heap(text, n, &dn, 128);
    unsigned char *g;
    uint32_t crc = (uint32_t)mz_crc32(0, (const unsigned char *)text, n);
    static const unsigned char head[10] = {0x1f, 0x8b, 8, 8, 0, 0, 0, 0, 0, 3};

    if (d == NULL) {
        return NULL;
    }
    /* with an FNAME, as gzip without -n writes */
    g = malloc(10 + 12 + dn + 8);
    memcpy(g, head, 10);
    memcpy(g + 10, "libmain.syms", 13);
    memcpy(g + 23, d, dn);
    for (int i = 0; i < 4; i++) {
        g[23 + dn + i] = (unsigned char)(crc >> (8 * i));
        g[23 + dn + 4 + i] = (unsigned char)((uint32_t)n >> (8 * i));
    }
    free(d);
    *out_n = 23 + dn + 8;
    return g;
}

static void symbol_cases(void)
{
    IcoSymHit h[8];
    int r;

    memset(h, 0, sizeof h);
    h[0].off = 0x2104;
    h[1].off = 0x2210;
    h[2].off = 0x2304;
    h[3].off = 0x2290;
    h[4].off = 0x2590;
    h[5].off = 0x1002;
    r = resolve_mem(elf_map, sizeof elf_map - 1, "ico_diag_init", 0x2000, h, 6);
    CHECK(r == 6);
    CHECK(strcmp(hit_text(&h[0]), "ico_diag_log+0x4 (diag_host.c.o)") == 0);
    CHECK(strcmp(hit_text(&h[1]),
                 "a_static_function_whose_section_name_is_long_enough_to_wrap+0x10 "
                 "(rd_blur.c.o)") == 0);
    CHECK(h[1].named_by_section == 1);
    CHECK(strcmp(hit_text(&h[2]), "_CopyVector+0x4 (vu0.c.o)") == 0);
    CHECK(strcmp(hit_text(&h[3]), "a static function in vu0.c.o, +0x10 into its code") == 0);
    CHECK(strcmp(hit_text(&h[4]), "a static function in statics.c.o, +0x10 into its code") == 0);
    CHECK(strcmp(hit_text(&h[5]), "_init+0x2 (crti.o)") == 0);
    for (int i = 0; i < 6; i++) {
        printf("symbols: elf +0x%llx %s\n", (unsigned long long)h[i].off, hit_text(&h[i]));
    }
    /* the anchor 0x1000 into the module: every offset 0x1000 less */
    memset(h, 0, sizeof h);
    h[0].off = 0x1304;
    r = resolve_mem(elf_map, sizeof elf_map - 1, "_ico_diag_init", 0x1000, h, 1);
    CHECK(r == 1 && strcmp(hit_text(&h[0]), "_CopyVector+0x4 (vu0.c.o)") == 0);
    /* no anchor: nothing */
    r = resolve_mem(elf_map, sizeof elf_map - 1, "not_there", 0, h, 1);
    CHECK(r == -1);

    /* Windows: offsets from the image base */
    memset(h, 0, sizeof h);
    h[0].off = 0x198210;
    h[1].off = 0x197945;
    h[2].off = 0x1003d8;
    r = resolve_mem(win_map, sizeof win_map - 1, "ico_diag_init", 0x197fc0, h, 3);
    CHECK(r == 2);
    CHECK(strcmp(hit_text(&h[0]), "ico_hotkey_for+0x10 (hotkeys.c.obj)") == 0);
    CHECK(strcmp(hit_text(&h[1]), "ico_diag_set_exit_hook+0x5 (diag_host.c.obj)") == 0);
    CHECK(hit_text(&h[2])[0] == '\0'); /* past the excerpt's sections: crt2's, unnamed */
    for (int i = 0; i < 2; i++) {
        printf("symbols: windows +0x%llx %s\n", (unsigned long long)h[i].off, hit_text(&h[i]));
    }

    /* nm: static functions named, data left out */
    memset(h, 0, sizeof h);
    h[0].off = 0x2150;
    h[1].off = 0x21a0;
    h[2].off = 0x2204;
    r = resolve_mem(nm_table, sizeof nm_table - 1, "ico_diag_init", 0x2000, h, 3);
    CHECK(r == 3);
    CHECK(strcmp(hit_text(&h[0]), "static_fn+0x50") == 0);
    CHECK(strcmp(hit_text(&h[1]), "static_fn+0xa0") == 0);
    CHECK(strcmp(hit_text(&h[2]), "weak_fn+0x4") == 0);

    /* the same kind of table gzip-compressed, past inflate's window */
    {
        static char big[200000];
        size_t n = 0, gn = 0;
        unsigned char *gz;
        IcoSymSource raw, src;
        IcoSymMem m;

        n += (size_t)snprintf(big + n, sizeof big - n, "0000000000001000 T ico_diag_init\n");
        for (unsigned int i = 0; i < 4000; i++) {
            n += (size_t)snprintf(big + n, sizeof big - n, "%016x t function_number_%u\n",
                                  0x10000u + i * 0x40u, i);
        }
        gz = gzip_of(big, n, &gn);
        CHECK(gz != NULL && n > 100000);
        if (gz != NULL) {
            memset(h, 0, sizeof h);
            h[0].off = 0x10000u + 3999u * 0x40u + 0x8u;
            h[1].off = 0x10000u + 17u * 0x40u;
            ico_sym_mem_source(&raw, &m, gz, gn);
            ico_sym_gz_source(&src, &raw);
            r = ico_sym_resolve(&src, "ico_diag_init", 0x1000, h, 2);
            CHECK(r == 2);
            CHECK(strcmp(hit_text(&h[0]), "function_number_3999+0x8") == 0);
            CHECK(strcmp(hit_text(&h[1]), "function_number_17+0x0") == 0);
            /* a member cut short: the stream ends in an error, the lookup
               names nothing (and returns) */
            ico_sym_mem_source(&raw, &m, gz, gn / 2);
            ico_sym_gz_source(&src, &raw);
            r = ico_sym_resolve(&src, "ico_diag_init", 0x1000, h, 2);
            CHECK(r == -1);
            free(gz);
        }
    }
}

/* --- The report file ------------------------------------------------------------------ */

/* the newest report-*.zip in dir into out (1), or 0 */
static int newest_report(const char *dir, char *out, size_t size)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    char best[256] = "";

    if (d == NULL) {
        return 0;
    }
    while ((e = readdir(d)) != NULL) {
        if (strncmp(e->d_name, "report-", 7) == 0 && strcmp(best, e->d_name) < 0) {
            snprintf(best, sizeof best, "%s", e->d_name);
        }
    }
    closedir(d);
    if (best[0] == '\0') {
        return 0;
    }
    snprintf(out, size, "%s/%s", dir, best);
    return 1;
}

/* one entry of zip as a NUL-terminated malloc block (NULL: none) */
static char *zip_entry(const char *zip, const char *name)
{
    mz_zip_archive z;
    size_t n = 0;
    char *data, *out;

    memset(&z, 0, sizeof z);
    if (!mz_zip_reader_init_file(&z, zip, 0)) {
        return NULL;
    }
    data = mz_zip_reader_extract_file_to_heap(&z, name, &n, 0);
    mz_zip_reader_end(&z);
    if (data == NULL) {
        return NULL;
    }
    out = malloc(n + 1);
    memcpy(out, data, n);
    out[n] = '\0';
    mz_free(data);
    return out;
}

static void clear_dir(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    char p[512];

    if (d == NULL) {
        return;
    }
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] != '.') {
            snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
            remove(p);
        }
    }
    closedir(d);
}

#endif

int main(int argc, char **argv)
{
#if defined(_WIN32)
    (void)argc;
    (void)argv;
    /* the child modes compile here too; only the POSIX driver runs them */
    (void)fails;
    (void)run_child;
    printf("diag_test: skipped (POSIX only)\n");
    return 77;
#else
    char log[256];
    char *s;
    int r;

    self_path = argv[0];
    if (argc == 3) {
        run_child(argv[1], argv[2]);
        return 0;
    }
    snprintf(log, sizeof log, "diag_test_%d.log", (int)getpid());

    r = run(argv[0], "lines", log);
    s = read_file(log);
    CHECK(r == 0);
    CHECK(strstr(s, "test: child lines") != NULL);
    CHECK(strstr(s, "] a milestone") != NULL);

    symbol_cases();

    /* the crash's report file, in a folder of its own */
    char rdir[64];
    snprintf(rdir, sizeof rdir, "diag_test_reports_%d", (int)getpid());
    mkdir(rdir, 0755);
    clear_dir(rdir);
    setenv("ICO_TEST_REPORT_DIR", rdir, 1);
    r = run(argv[0], "segv", log);
    unsetenv("ICO_TEST_REPORT_DIR");
    s = read_file(log);
    CHECK(r == 3);
    /* the symbol lines from diag_test.map, after the raw ones */
    CHECK(strstr(s, "ico_pc: symbols from diag_test.map (each offset above in diag_test, in "
                    "order):\n") != NULL);
    CHECK(strstr(s, " ico_sched_run+0x") != NULL || strstr(s, " ico_sched_") != NULL);
    CHECK(strstr(s, "a static function in diag_test.c.o") != NULL);
    CHECK(strstr(s, "ico_pc: symbols from") > strstr(s, "ico_pc: threads"));
    CHECK(strstr(s, "ico_pc: report: report-") != NULL);
    {
        char zip[512];
        char *txt = NULL, *zlog = NULL;

        CHECK(newest_report(rdir, zip, sizeof zip));
        if (newest_report(rdir, zip, sizeof zip)) {
            txt = zip_entry(zip, "report.txt");
            zlog = zip_entry(zip, "ico-pc.log");
        }
        CHECK(txt != NULL && zlog != NULL);
        if (txt != NULL) {
            FILE *ex = fopen("diag_test_report.txt", "wb");

            CHECK(strstr(txt, "what ended the run: SIGSEGV") != NULL);
            CHECK(strstr(txt, "summary: version test; platform the test; GPU none\n") != NULL);
            CHECK(strstr(txt, "the last ico_pc:/window:/rd: lines before the report:\n") != NULL);
            CHECK(strstr(txt, "ico_pc: summary: version test") != NULL);
            CHECK(strstr(txt, "ico_pc: CRASH: SIGSEGV") != NULL);
            CHECK(strstr(txt, "ico_pc: symbols from diag_test.map") != NULL);
            CHECK(strstr(txt, "achievements.toml: (no stages line)") != NULL);
            if (ex != NULL) {
                fputs(txt, ex);
                fclose(ex);
            }
        }
        if (zlog != NULL) {
            CHECK(strstr(zlog, "test: child segv") != NULL);
            CHECK(strstr(zlog, "ico_pc: CRASH: SIGSEGV") != NULL);
        }
        free(txt);
        free(zlog);
    }
    clear_dir(rdir);
    setenv("ICO_TEST_REPORT_DIR", rdir, 1);
    r = run(argv[0], "watchdog", log);
    unsetenv("ICO_TEST_REPORT_DIR");
    CHECK(r == 4);
    s = read_file(log);
    CHECK(strstr(s, "ico_pc: symbols from diag_test.map") != NULL);
    {
        char zip[512];
        char *txt = NULL;

        if (newest_report(rdir, zip, sizeof zip)) {
            txt = zip_entry(zip, "report.txt");
        }
        CHECK(txt != NULL && strstr(txt, "what ended the run: the watchdog: no Main tick 1 s "
                                         "after boot started") != NULL);
        free(txt);
    }
    /* the prune keeps the newest ICO_REPORT_KEEP */
    clear_dir(rdir);
    {
        static const char *names[] = {"report-20261001-000000.zip", "report-20261002-000000.zip",
                                      "report-20261003-000000.zip", "report-20261004-000000.zip",
                                      "report-20261005-000000.zip", "report-20261006-000000.zip",
                                      "report-20261007-000000.zip", "loss-20261001-000000.zip"};
        char p[256];

        for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
            FILE *f;

            snprintf(p, sizeof p, "%s/%s", rdir, names[i]);
            f = fopen(p, "wb");
            if (f != NULL) {
                fclose(f);
            }
        }
        ico_report_set_paths(rdir, NULL, NULL, NULL);
        CHECK(ico_report_prune(ICO_REPORT_KEEP) == 2);
        snprintf(p, sizeof p, "%s/report-20261002-000000.zip", rdir);
        CHECK(access(p, F_OK) != 0);
        snprintf(p, sizeof p, "%s/report-20261003-000000.zip", rdir);
        CHECK(access(p, F_OK) == 0);
        snprintf(p, sizeof p, "%s/loss-20261001-000000.zip", rdir);
        CHECK(access(p, F_OK) == 0);
    }
    /* the prune sorts by stamp then k, whatever order the folder lists */
    clear_dir(rdir);
    {
        static const char *names[] = {
            "report-20261002-000000-1.zip", "report-20261003-000000.zip",
            "report-20261002-000000.zip",   "report-20261003-000000-2.zip",
            "report-20261001-235959.zip",   "report-20261003-000000-10.zip",
            "report-20261003-000000-1.zip", "report-20261002-000000-2.zip"};
        static const int gone[] = {1, 0, 1, 0, 1, 0, 0, 0}; /* keep 5: the oldest 3 go */
        char p[256];

        for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
            FILE *f;

            snprintf(p, sizeof p, "%s/%s", rdir, names[i]);
            f = fopen(p, "wb");
            if (f != NULL) {
                fclose(f);
            }
        }
        ico_report_set_paths(rdir, NULL, NULL, NULL);
        CHECK(ico_report_prune(5) == 3);
        for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
            snprintf(p, sizeof p, "%s/%s", rdir, names[i]);
            CHECK((access(p, F_OK) != 0) == (gone[i] != 0));
        }
    }
    clear_dir(rdir);
    rmdir(rdir);

    /* one helper for the lines that repeat */
    r = run(argv[0], "limited", log);
    s = read_file(log);
    CHECK(r == 0);
    CHECK(strstr(s, "test: limited line 1\n") != NULL);
    CHECK(strstr(s, "test: limited line 8 (8 of these so far; from here every 100th is "
                    "written)\n") != NULL);
    CHECK(strstr(s, "test: limited line 9") == NULL);
    CHECK(strstr(s, "test: limited line 99") == NULL);
    CHECK(strstr(s, "test: limited line 100 (number 100 of these)\n") != NULL);
    CHECK(strstr(s, "test: limited line 200 (number 200 of these)\n") != NULL);
    CHECK(strstr(s, "ico_pc: limited lines (key, count, written): test: limited 250 (10 "
                    "written)\n") != NULL);
    if (fails) {
        printf("--- limited log ---\n%s\n", s);
    }

    r = run(argv[0], "segv", log);
    s = read_file(log);
    CHECK(r == 3);
    CHECK(strstr(s, "CRASH: SIGSEGV") != NULL);
    CHECK(strstr(s, "reading address 0x0") != NULL);
    CHECK(strstr(s, "game thread then: #1 segv_thread") != NULL);
    CHECK(strstr(s, "last call PollSema(") != NULL);
    CHECK(strstr(s, "return-address candidates") != NULL);
    CHECK(strstr(s, "test: exit hook") != NULL);
    CHECK(strstr(s, "ico_pc: while:") == NULL);
    CHECK(strstr(s, "graphics driver") == NULL);
    if (fails) {
        printf("--- segv log ---\n%s\n", s);
    }

    /* the activity note: named right after the CRASH line, and a pipeline
       build makes it a graphics driver crash (no relaunch: not prepared) */
    r = run(argv[0], "segvnote", log);
    s = read_file(log);
    CHECK(r == 3);
    CHECK(strstr(s, "CRASH: SIGSEGV") != NULL);
    CHECK(strstr(s, "ico_pc: while: creating pipeline program 3 blend 0\n") != NULL);
    CHECK(strstr(s, "CRASH: SIGSEGV") != NULL && strstr(s, "ico_pc: while:") != NULL &&
          strstr(s, "CRASH: SIGSEGV") < strstr(s, "ico_pc: while:") &&
          strstr(s, "faulting access") > strstr(s, "ico_pc: while:"));
    CHECK(strstr(s, "the graphics driver crashed while preparing the shaders: the game cannot "
                    "start itself again on this system") != NULL);
    CHECK(strstr(s, "starts again with") == NULL);
    if (fails) {
        printf("--- segvnote log ---\n%s\n", s);
    }
    /* the same from a worker thread under a batch's note */
    r = run(argv[0], "segvworker", log);
    s = read_file(log);
    CHECK(r == 3);
    CHECK(strstr(s, "CRASH: SIGSEGV") != NULL);
    CHECK(strstr(s, "ico_pc: while: " BATCH_NOTE "\n") != NULL);
    CHECK(strstr(s, "the graphics driver crashed while preparing the shaders: the game cannot "
                    "start itself again on this system") != NULL);
    CHECK(strstr(s, "test: the worker did not fault") == NULL);
    if (fails) {
        printf("--- segvworker log ---\n%s\n", s);
    }
    r = run(argv[0], "segvcleared", log);
    s = read_file(log);
    CHECK(r == 3);
    CHECK(strstr(s, "ico_pc: while:") == NULL);
    CHECK(strstr(s, "graphics driver") == NULL);

    r = run(argv[0], "abort", log);
    s = read_file(log);
    CHECK(r == 3);
    CHECK(strstr(s, "CRASH: abort()") != NULL);
    CHECK(strstr(s, "the test fails here") != NULL);
    CHECK(strstr(s, "line 42") != NULL);

    r = run(argv[0], "watchdog", log);
    s = read_file(log);
    CHECK(r == 4);
    CHECK(strstr(s, "WATCHDOG: no Main tick 1 s after boot started") != NULL);
    CHECK(strstr(s, "the main thread was at") != NULL);
    CHECK(strstr(s, "#1 spin_thread") != NULL);
    CHECK(strstr(s, "test: exit hook (watchdog)") != NULL);
    /* the bound for one wait in the system, by default 10 minutes */
    CHECK(strstr(s, "time the system holds the window's event pump is not counted, up to 600 s "
                    "for one wait") != NULL);
    if (fails) {
        printf("--- watchdog log ---\n%s\n", s);
    }

    /* issue 105: the system holds the window's event pump for 3.5 s against
       a 1 s limit (70 s against 60 s, scaled): not a hang */
    r = run(argv[0], "pump", log);
    s = read_file(log);
    CHECK(r == 0);
    CHECK(strstr(s, "WATCHDOG:") == NULL);
    CHECK(strstr(s, "ico_pc: watchdog: the window's event pump has not returned for 1 s (the "
                    "system is holding it: the game is not hung); waiting\n") != NULL);
    CHECK(strstr(s, "ico_pc: watchdog: in the test's pump, handling WM_TEST(0x0400)\n") != NULL);
    {
        /* named once per stall */
        const char *w = strstr(s, "has not returned for");
        CHECK(w != NULL && strstr(w + 1, "has not returned for") == NULL);
    }
    CHECK(strstr(s, "ico_pc: watchdog: the window's event pump returned after 3.") != NULL);
    CHECK(strstr(s, "| in the window's event pump ") != NULL);
    CHECK(strstr(s, "test: game resumed") != NULL);
    if (fails) {
        printf("--- pump log ---\n%s\n", s);
    }
    /* ... and a stall in the game's own code after it still ends the run */
    r = run(argv[0], "pumphang", log);
    s = read_file(log);
    CHECK(r == 4);
    CHECK(strstr(s, "test: game resumed") != NULL);
    CHECK(strstr(s, "WATCHDOG: no new Main tick for 1 s (the last was tick 3, progress 0)") !=
          NULL);
    CHECK(strstr(s, "test: game resumed") != NULL && strstr(s, "WATCHDOG:") != NULL &&
          strstr(s, "test: game resumed") < strstr(s, "WATCHDOG:"));
    CHECK(strstr(s, "the main thread has been inside") == NULL);
    if (fails) {
        printf("--- pumphang log ---\n%s\n", s);
    }
    /* ... and a wait that never returns ends it at the bound for one wait */
    r = run(argv[0], "pumpbound", log);
    s = read_file(log);
    CHECK(r == 4);
    CHECK(strstr(s, "up to 3 s for one wait") != NULL);
    CHECK(strstr(s, "the window's event pump has not returned for 1 s (the system is holding "
                    "it: the game is not hung); waiting") != NULL);
    CHECK(strstr(s, "WATCHDOG: the window's event pump has not returned for 3 s (the system "
                    "holds it; the limit is [dev] watchdog_pump)") != NULL);
    CHECK(strstr(s, "the main thread has been inside the window's event pump for 3 s") != NULL);
    CHECK(strstr(s, "ico_pc: in the test's pump, handling WM_TEST(0x0400)\n") != NULL);
    CHECK(strstr(s, "the main thread was at") != NULL);
    CHECK(strstr(s, "test: pump returned") == NULL);
    if (fails) {
        printf("--- pumpbound log ---\n%s\n", s);
    }
    /* ... but only while a limit is in force: not before the limits
       (Android's start-up set), not with the watchdog off */
    r = run(argv[0], "pumpearly", log);
    s = read_file(log);
    CHECK(r == 0);
    CHECK(strstr(s, "WATCHDOG:") == NULL);
    CHECK(strstr(s, "test: pump returned") != NULL);
    if (fails) {
        printf("--- pumpearly log ---\n%s\n", s);
    }
    r = run(argv[0], "pumpoff", log);
    s = read_file(log);
    CHECK(r == 0);
    CHECK(strstr(s, "WATCHDOG:") == NULL);
    CHECK(strstr(s, "test: pump returned") != NULL);
    if (fails) {
        printf("--- pumpoff log ---\n%s\n", s);
    }

    /* the second sign of life: 2.5 s of progress without a tick against a
       1 s limit does not fire; the stop after it does, and says a movie was
       playing */
    r = run(argv[0], "progress", log);
    s = read_file(log);
    CHECK(r == 4);
    CHECK(strstr(s, "test: progress kept it alive (50)") != NULL);
    CHECK(strstr(s, "WATCHDOG: a movie was playing and showed no new picture for 1 s (the last "
                    "was tick 1, progress 50)") != NULL);
    CHECK(strstr(s, "WATCHDOG:") != NULL &&
          strstr(s, "test: progress kept it alive") < strstr(s, "WATCHDOG:"));
    CHECK(strstr(s, "| progress ") != NULL);
    CHECK(strstr(s, "(a movie is playing)") != NULL);
    CHECK(strstr(s, " | pipelines 12 of 527, longest 1.3 s | creating pipelines 329-360 of 702") !=
          NULL);
    CHECK(strstr(s, "vertex vu_prelit_vs 50325 words") != NULL);
    if (fails) {
        printf("--- progress log ---\n%s\n", s);
    }

    /* without progress the tick alone counts: it fires during
       the 2.5 s the progress case survived */
    r = run(argv[0], "stall", log);
    s = read_file(log);
    CHECK(r == 4);
    CHECK(strstr(s, "WATCHDOG: no new Main tick for 1 s (the last was tick 1, progress 0)") !=
          NULL);
    CHECK(strstr(s, "a movie was playing") == NULL);
    if (fails) {
        printf("--- stall log ---\n%s\n", s);
    }

    /* with an effects program a long present is not a stall: the later limit
       fires only after it */
    r = run(argv[0], "present", log);
    s = read_file(log);
    CHECK(r == 4);
    CHECK(strstr(s, "test: present done") != NULL);
    CHECK(strstr(s, "WATCHDOG: no new Main tick for 2 s") != NULL);
    CHECK(strstr(s, "test: present done") < strstr(s, "WATCHDOG:"));
    CHECK(strstr(s, "for a picture to be shown") == NULL);
    if (fails) {
        printf("--- present log ---\n%s\n", s);
    }

    /* ... and the first limit does not count it either (exit 0: no fire) */
    r = run(argv[0], "presentfirst", log);
    s = read_file(log);
    CHECK(r == 0);
    CHECK(strstr(s, "test: present done") != NULL);
    CHECK(strstr(s, "WATCHDOG:") == NULL);
    if (fails) {
        printf("--- presentfirst log ---\n%s\n", s);
    }
    /* ... but without one nothing is excused: it fires inside the present */
    r = run(argv[0], "presentplain", log);
    s = read_file(log);
    CHECK(r == 4);
    CHECK(strstr(s, "test: present done") == NULL);
    CHECK(strstr(s, "WATCHDOG: no new Main tick for 1 s") != NULL);
    if (fails) {
        printf("--- presentplain log ---\n%s\n", s);
    }
    /* the start-up activity file */
    {
        char act[300], ends[320], bgmark[320];
        act_path(act, sizeof act, log);
        /* the counts of ends in a row and the background mark, beside it */
        snprintf(ends, sizeof ends, "%s-ends.txt", act);
        snprintf(bgmark, sizeof bgmark, "%s-background.txt", act);
        remove(act);
        remove(ends);
        remove(bgmark);
        /* no file: not lazy; the start-up set's note stays after a
           cleared note and when the run just ends */
        r = run(argv[0], "actrecord", log);
        s = read_file(log);
        CHECK(r == 0);
        CHECK(strstr(s, "test: lazy 0") != NULL);
        CHECK(strstr(s, "the previous run ended while") == NULL);
        s = read_file(act);
        CHECK(strcmp(s, "test 1\n" BATCH_NOTE) == 0);
        /* the next start names it, deletes it and is lazy; its own note
           replaces the file's */
        r = run(argv[0], "actlazy", log);
        s = read_file(log);
        CHECK(r == 0);
        CHECK(strstr(s, "ico_pc: the previous run ended while: " BATCH_NOTE "\n") != NULL);
        CHECK(strstr(s, "test: lazy 1") != NULL);
        s = read_file(act);
        CHECK(strcmp(s, "test 1\ncreating pipeline fog test") == 0);
        /* a lazy run ended between two creations: the line between them,
           and the next start is lazy again (no further step) */
        setenv("ICO_TEST_ACT_BETWEEN", "1", 1);
        r = run(argv[0], "actlazy", log);
        unsetenv("ICO_TEST_ACT_BETWEEN");
        s = read_file(log);
        CHECK(r == 0);
        CHECK(strstr(s, "ico_pc: the previous run ended while: creating pipeline fog test\n") !=
              NULL);
        CHECK(strstr(s, "test: lazy 1") != NULL);
        s = read_file(act);
        CHECK(strstr(s, "none was being created") != NULL);
        r = run(argv[0], "actnext", log);
        s = read_file(log);
        CHECK(strstr(s, "ended while: running with the pipelines created as the frames need "
                        "them") != NULL);
        CHECK(strstr(s, "test: lazy 1") != NULL);
        /* ... which deleted it: the start after is not lazy */
        r = run(argv[0], "actnext", log);
        s = read_file(log);
        CHECK(strstr(s, "test: lazy 0") != NULL);
        CHECK(strstr(s, "the previous run ended while") == NULL);
        /* a normal end of the set deletes the file */
        r = run(argv[0], "actclean", log);
        CHECK(r == 0);
        {
            FILE *f = fopen(act, "rb");
            CHECK(f == NULL);
            if (f != NULL) {
                fclose(f);
            }
        }
        /* a lazy run's clean exit keeps the file: the next start of the
           same version is lazy too */
        run(argv[0], "actrecord", log);
        r = run(argv[0], "actlazyexit", log);
        s = read_file(log);
        CHECK(r == 0);
        CHECK(strstr(s, "test: lazy 1") != NULL);
        s = read_file(act);
        CHECK(strcmp(s, "test 1\nexited with the pipelines created as the frames needed them") ==
              0);
        r = run(argv[0], "actnext", log);
        s = read_file(log);
        CHECK(strstr(s, "ico_pc: the previous run created the pipelines as the frames needed "
                        "them; this one does too\n") != NULL);
        CHECK(strstr(s, "the previous run ended while") == NULL);
        CHECK(strstr(s, "test: lazy 1") != NULL);
        /* another version tries the start-up set again (and deletes the
           file); an end while making the pipelines counts whatever the
           version, so its line leaves the policy to the count's line */
        run(argv[0], "actrecord", log);
        setenv("ICO_TEST_ACT_VERSION", "test 2", 1);
        r = run(argv[0], "actnext", log);
        unsetenv("ICO_TEST_ACT_VERSION");
        s = read_file(log);
        CHECK(r == 0);
        CHECK(strstr(s, "ico_pc: the previous run ended while: " BATCH_NOTE
                        " (version test 1)\n") != NULL);
        CHECK(strstr(s, "trying the start-up set again") == NULL);
        CHECK(strstr(s, "test: lazy 0") != NULL);
        run(argv[0], "actrecord", log);
        run(argv[0], "actlazyexit", log);
        setenv("ICO_TEST_ACT_VERSION", "test 2", 1);
        r = run(argv[0], "actnext", log);
        unsetenv("ICO_TEST_ACT_VERSION");
        s = read_file(log);
        CHECK(strstr(s, "ico_pc: the previous run (version test 1) created the pipelines as the "
                        "frames needed them; trying the start-up set again on test 2\n") != NULL);
        CHECK(strstr(s, "test: lazy 0") != NULL);
        {
            FILE *f = fopen(act, "rb");
            CHECK(f == NULL);
            if (f != NULL) {
                fclose(f);
            }
        }
        /* a restart after a driver crash: logged and deleted, its own step
           decides (not lazy) */
        run(argv[0], "actrecord", log);
        r = run(argv[0], "actrelaunch", log);
        s = read_file(log);
        CHECK(r == 0);
        CHECK(strstr(s, "ico_pc: the previous run ended while: creating pipelines 1-4 of 9") !=
              NULL);
        CHECK(strstr(s, "test: lazy 0") != NULL);
        {
            FILE *f = fopen(act, "rb");
            CHECK(f == NULL);
            if (f != NULL) {
                fclose(f);
            }
        }
        /* issue 107: a start that ended inside a batch of the extended VU
           variants: the next start reads the note (any version, relaunch
           or not) and names it; window_host.c then writes [dev]
           vu_plain_only. A plain batch's note does not count */
        r = run(argv[0], "actvuext", log);
        CHECK(r == 0);
        s = read_file(act);
        CHECK(strcmp(s, "test 1\n" VU_EXT_NOTE) == 0);
        r = run(argv[0], "actnext", log);
        s = read_file(log);
        CHECK(r == 0);
        CHECK(strstr(s, "ico_pc: the previous run ended while: " VU_EXT_NOTE "\n") != NULL);
        CHECK(strstr(s, "test: previous vu ext 1") != NULL);
        run(argv[0], "actvuext", log);
        setenv("ICO_TEST_ACT_VERSION", "test 2", 1);
        r = run(argv[0], "actnext", log);
        unsetenv("ICO_TEST_ACT_VERSION");
        s = read_file(log);
        CHECK(strstr(s, "test: previous vu ext 1") != NULL);
        CHECK(strstr(s, "test: lazy 0") != NULL);
        run(argv[0], "actvuplain", log);
        r = run(argv[0], "actnext", log);
        s = read_file(log);
        CHECK(strstr(s, "ico_pc: the previous run ended while: " VU_PLAIN_NOTE "\n") != NULL);
        CHECK(strstr(s, "test: previous vu ext 0") != NULL);
        CHECK(strstr(s, "test: lazy 1") != NULL);
        /* the next start found no file */
        r = run(argv[0], "actnext", log);
        s = read_file(log);
        CHECK(strstr(s, "test: previous vu ext 0") != NULL);
        /* the plain batches' compile times survive the freeze in an
           extended one: the file holds them after the note, and the next
           start logs each with the version, then the note */
        r = run(argv[0], "actvutimes", log);
        CHECK(r == 0);
        s = read_file(act);
        CHECK(strcmp(s, "test 1\n" VU_EXT_NOTE "\ncompiled: " VU_TIMES_1
                        "\ncompiled: " VU_TIMES_2) == 0);
        r = run(argv[0], "actnext", log);
        s = read_file(log);
        CHECK(r == 0);
        CHECK(strstr(s, "ico_pc: the previous run (version test 1) compiled: " VU_TIMES_1 "\n") !=
              NULL);
        CHECK(strstr(s, "ico_pc: the previous run (version test 1) compiled: " VU_TIMES_2 "\n") !=
              NULL);
        CHECK(strstr(s, "ico_pc: the previous run ended while: " VU_EXT_NOTE "\n") != NULL);
        CHECK(strstr(s, "test: previous vu ext 1") != NULL);
        /* a set that completed leaves no file and no times */
        run(argv[0], "actvutimesdone", log);
        r = run(argv[0], "actnext", log);
        s = read_file(log);
        CHECK(strstr(s, "compiled: ") == NULL);
        CHECK(strstr(s, "test: lazy 0") != NULL);
        /* issues 93 and 107: a start that ended inside a 2D batch of the
           set: the file keeps the note and the finished batch's line (its
           memory and pipeline cache); the next start logs both, the last
           batch again, and counts the end: the first in a row makes that
           start lazy for that start only, nothing written */
        remove(ends);
        r = run(argv[0], "act2dbatch", log);
        CHECK(r == 0);
        s = read_file(act);
        CHECK(strcmp(s, "test 1\n" NOTE_2D "\ncompiled: " TIMES_2D) == 0);
        setenv("ICO_TEST_THEN", "die", 1);
        r = run(argv[0], "actpipeset", log);
        unsetenv("ICO_TEST_THEN");
        s = read_file(log);
        CHECK(r == 0);
        CHECK(strstr(s, "ico_pc: the previous run (version test 1) compiled: " TIMES_2D "\n") !=
              NULL);
        CHECK(strstr(s, "ico_pc: the previous run's last finished batch: " TIMES_2D "\n") != NULL);
        CHECK(strstr(s, "ico_pc: the previous run ended while: " NOTE_2D "\n") != NULL);
        CHECK(strstr(s, "ico_pc: starts in a row that ended while preparing the graphics (the "
                        "pipelines): 1\n") != NULL);
        CHECK(strstr(s, "test: config write") == NULL);
        CHECK(strstr(s, "ico_pc: the previous start ended while preparing the graphics (" NOTE_2D
                        "); this start only makes the pipelines as the game needs them, with no "
                        "start-up set (nothing is written to config.toml); if this start also "
                        "ends while preparing them, [dev] pipeline_set = \"lazy\" is written for "
                        "good\n") != NULL);
        CHECK(strstr(s, "test: pipeline set 4\n") != NULL);
        /* ... and that lazy run also ended inside a pipeline: the second in
           a row writes [dev] pipeline_set = "lazy" for good and says so */
        s = read_file(act);
        CHECK(strcmp(s, "test 1\ncreating pipeline fog test") == 0);
        r = run(argv[0], "actpipeset", log);
        s = read_file(log);
        CHECK(r == 0);
        CHECK(strstr(s, "ico_pc: starts in a row that ended while preparing the graphics (the "
                        "pipelines): 2\n") != NULL);
        CHECK(strstr(s, "test: config write dev.pipeline_set = lazy\n") != NULL);
        CHECK(strstr(s, "ico_pc: the previous start ended while preparing the graphics (creating "
                        "pipeline fog test); this start and every start after it make the "
                        "pipelines as the game needs them, with no start-up set ([dev] "
                        "pipeline_set = \"lazy\" in config.toml); delete that line to try the "
                        "start-up set again\n") != NULL);
        CHECK(strstr(s, "test: pipeline set 3\n") != NULL);
        /* any version's end counts (the setting outlives the version) */
        run(argv[0], "act2dbatch", log);
        setenv("ICO_TEST_ACT_VERSION", "test 2", 1);
        r = run(argv[0], "actpipeset", log);
        s = read_file(log);
        CHECK(strstr(s, "test: lazy 0\n") != NULL);
        CHECK(strstr(s, "ico_pc: the previous run ended while: " NOTE_2D " (version test 1)\n") !=
              NULL);
        CHECK(strstr(s, "trying the start-up set again") == NULL);
        CHECK(strstr(s, "this start only makes the pipelines as the game needs them") != NULL);
        CHECK(strstr(s, "test: config write") == NULL);
        CHECK(strstr(s, "test: pipeline set 4\n") != NULL);
        run(argv[0], "act2dbatch", log);
        r = run(argv[0], "actpipeset", log);
        unsetenv("ICO_TEST_ACT_VERSION");
        s = read_file(log);
        CHECK(strstr(s, "test: config write dev.pipeline_set = lazy\n") != NULL);
        CHECK(strstr(s, "test: pipeline set 3\n") != NULL);
        /* the setting could not be written: this start only, and the
           count stays, so the next end in a row is due again */
        remove(ends);
        run(argv[0], "act2dbatch", log);
        run(argv[0], "actpipeset", log);
        run(argv[0], "act2dbatch", log);
        setenv("ICO_TEST_PERSIST_FAIL", "1", 1);
        setenv("ICO_TEST_THEN", "die", 1);
        r = run(argv[0], "actpipeset", log);
        unsetenv("ICO_TEST_THEN");
        unsetenv("ICO_TEST_PERSIST_FAIL");
        s = read_file(log);
        CHECK(strstr(s, "("
                        "[dev] pipeline_set = \"lazy\" in config.toml, which could not be "
                        "written: this start only)") != NULL);
        CHECK(strstr(s, "test: pipeline set 3\n") != NULL);
        r = run(argv[0], "actpipeset", log);
        s = read_file(log);
        CHECK(strstr(s, "ico_pc: starts in a row that ended while preparing the graphics (the "
                        "pipelines): 3\n") != NULL);
        CHECK(strstr(s, "test: config write dev.pipeline_set = lazy\n") != NULL);
        CHECK(strstr(s, "test: pipeline set 3\n") != NULL);
        CHECK(read_file(ends)[0] == '\0');
        /* ... and that start's lazy run says "for this start only", so the
           start after a clean exit tries the set again */
        run(argv[0], "act2dbatch", log);
        run(argv[0], "actpipeset", log);
        run(argv[0], "act2dbatch", log);
        setenv("ICO_TEST_PERSIST_FAIL", "1", 1);
        setenv("ICO_TEST_THEN", "clean", 1);
        r = run(argv[0], "actpipeset", log);
        unsetenv("ICO_TEST_THEN");
        unsetenv("ICO_TEST_PERSIST_FAIL");
        s = read_file(act);
        CHECK(strcmp(s, "test 1\nexited with the pipelines created as the frames needed them for "
                        "that start only") == 0);
        CHECK(read_file(ends)[0] == '\0');
        /* a count that cannot be written is said once */
        mkdir(ends, 0755);
        run(argv[0], "act2dbatch", log);
        r = run(argv[0], "actpipeset", log);
        rmdir(ends);
        s = read_file(log);
        {
            char want[512];
            snprintf(want, sizeof want,
                     "ico_pc: the count of starts that ended while preparing the graphics could "
                     "not be written to %s (pipelines 1, extended VU shaders 0)\n",
                     ends);
            CHECK(strstr(s, want) != NULL);
        }
        CHECK(strstr(s, "test: pipeline set 4\n") != NULL);
        remove(ends);
        /* a lazy run for that start only that exited cleanly: its file says
           so, the next start tries the set again, and the row is cleared
           (the next end is the first again) */
        run(argv[0], "act2dbatch", log);
        setenv("ICO_TEST_THEN", "clean", 1);
        r = run(argv[0], "actpipeset", log);
        unsetenv("ICO_TEST_THEN");
        s = read_file(log);
        CHECK(strstr(s, "test: pipeline set 4\n") != NULL);
        s = read_file(act);
        CHECK(strcmp(s, "test 1\nexited with the pipelines created as the frames needed them for "
                        "that start only") == 0);
        CHECK(read_file(ends)[0] == '\0');
        r = run(argv[0], "actpipeset", log);
        s = read_file(log);
        CHECK(strstr(s, "ico_pc: the previous run made the pipelines as the frames needed them "
                        "for that start only; this one tries the start-up set again\n") != NULL);
        CHECK(strstr(s, "test: lazy 0\n") != NULL);
        CHECK(strstr(s, "test: pipeline set 0\n") != NULL);
        run(argv[0], "act2dbatch", log);
        r = run(argv[0], "actpipeset", log);
        s = read_file(log);
        CHECK(strstr(s, "test: config write") == NULL);
        CHECK(strstr(s, "test: pipeline set 4\n") != NULL);
        /* ... one that ended between two creations: the same, and the row
           is broken (not an end while preparing the graphics) */
        remove(ends);
        run(argv[0], "act2dbatch", log);
        setenv("ICO_TEST_THEN", "between", 1);
        r = run(argv[0], "actpipeset", log);
        unsetenv("ICO_TEST_THEN");
        s = read_file(act);
        CHECK(strcmp(s, "test 1\nrunning with the pipelines created as the frames need them for "
                        "this start only (none was being created)") == 0);
        r = run(argv[0], "actpipeset", log);
        s = read_file(log);
        CHECK(strstr(s, "ico_pc: the previous run ended while: running with the pipelines created "
                        "as the frames need them for this start only (none was being created); "
                        "that lazy run was for that start only, so this one tries the start-up "
                        "set again\n") != NULL);
        CHECK(strstr(s, "ico_pc: the previous run made the pipelines") == NULL);
        CHECK(strstr(s, "test: pipeline set 0\n") != NULL);
        run(argv[0], "act2dbatch", log);
        r = run(argv[0], "actpipeset", log);
        s = read_file(log);
        CHECK(strstr(s, "test: config write") == NULL);
        CHECK(strstr(s, "test: pipeline set 4\n") != NULL);
        /* a restart after a crash report takes its own step: nothing
           counted or written */
        remove(ends);
        run(argv[0], "act2dbatch", log);
        r = run(argv[0], "actpipesetrelaunch", log);
        s = read_file(log);
        CHECK(strstr(s, "test: config write") == NULL);
        CHECK(strstr(s, "starts in a row") == NULL);
        CHECK(strstr(s, "test: pipeline set 0\n") != NULL);
        /* an end while the app was in the background (the lifecycle's
           event): the mark names where, the next start says it is not
           counted and builds the set; the mark is deleted, and it breaks
           the row (the next end is the first again) */
        run(argv[0], "act2dbatch", log);
        run(argv[0], "actpipeset", log);
        setenv("ICO_TEST_BACKGROUND", "bg", 1);
        r = run(argv[0], "act2dbatch", log);
        unsetenv("ICO_TEST_BACKGROUND");
        s = read_file(log);
        CHECK(strstr(s, "ico_pc: the app went to the background at pipelines 201 of 527 of the "
                        "start-up set; an end there is not counted as one while preparing the "
                        "graphics\n") != NULL);
        s = read_file(bgmark);
        CHECK(strcmp(s, "test 1\nbackground: the app went to the background at pipelines 201 "
                        "of 527 of the start-up set\n") == 0);
        r = run(argv[0], "actpipeset", log);
        s = read_file(log);
        CHECK(strstr(s, "ico_pc: the previous run ended while: " NOTE_2D ", in the background "
                        "(the app went to the background at pipelines 201 of 527 of the "
                        "start-up set); the system ends apps there, so it is not counted as an "
                        "end while preparing the graphics: the start-up set as usual\n") != NULL);
        CHECK(strstr(s, "starts in a row") == NULL);
        CHECK(strstr(s, "test: lazy 0\n") != NULL);
        CHECK(strstr(s, "test: config write") == NULL);
        CHECK(strstr(s, "test: pipeline set 0\n") != NULL);
        CHECK(read_file(bgmark)[0] == '\0');
        run(argv[0], "act2dbatch", log);
        r = run(argv[0], "actpipeset", log);
        s = read_file(log);
        CHECK(strstr(s, "test: pipeline set 4\n") != NULL);
        /* ... Android's onTrimMemory(20) marks it too, from its own thread,
           and the line the activity file never got is logged from the
           mark */
        remove(ends);
        setenv("ICO_TEST_BACKGROUND", "trim20", 1);
        run(argv[0], "act2dbatch", log);
        unsetenv("ICO_TEST_BACKGROUND");
        /* the mark is set once: the lifecycle's event after it logs
           nothing, and the system's line stays in the mark */
        s = read_file(log);
        {
            const char *first = strstr(s, "ico_pc: the app went to the background ");
            CHECK(first != NULL);
            CHECK(first == NULL ||
                  strstr(first + 1, "ico_pc: the app went to the background ") == NULL);
        }
        CHECK(strstr(read_file(bgmark), "\nsystem: the app's screen was hidden (onTrimMemory "
                                        "level 20, ") != NULL);
        r = run(argv[0], "actpipeset", log);
        s = read_file(log);
        CHECK(strstr(s, "ico_pc: the previous run was told by the system in the background: the "
                        "app's screen was hidden (onTrimMemory level 20, ") != NULL);
        CHECK(strstr(s, ", in the background (the app went to the background at pipelines 201 "
                        "of 527 of the start-up set); the system ends apps there") != NULL);
        CHECK(strstr(s, "test: pipeline set 0\n") != NULL);
        /* ... back in the foreground first: the mark is deleted and the
           end counts */
        setenv("ICO_TEST_BACKGROUND", "back", 1);
        run(argv[0], "act2dbatch", log);
        unsetenv("ICO_TEST_BACKGROUND");
        s = read_file(log);
        CHECK(strstr(s, "ico_pc: back in the foreground; the background mark is deleted\n") !=
              NULL);
        r = run(argv[0], "actpipeset", log);
        s = read_file(log);
        CHECK(strstr(s, "in the background (") == NULL);
        CHECK(strstr(s, "test: pipeline set 4\n") != NULL);
        /* a device the table already makes lazy (RhiLimits.lazyPipelines):
           the second end in a row writes nothing and says why, and the
           row is cleared */
        run(argv[0], "act2dbatch", log);
        setenv("ICO_TEST_TABLE_LAZY", "Adreno 740, issues 93 and 107", 1);
        r = run(argv[0], "actpipeset", log);
        unsetenv("ICO_TEST_TABLE_LAZY");
        s = read_file(log);
        CHECK(strstr(s, "test: config write") == NULL);
        CHECK(strstr(s, "ico_pc: the second start in a row ended while preparing the graphics "
                        "(" NOTE_2D "); [dev] pipeline_set = \"lazy\" is not written to "
                        "config.toml: the device table already makes the pipelines as the game "
                        "needs them on this device (Adreno 740, issues 93 and 107)\n") != NULL);
        CHECK(strstr(s, "test: pipeline set 3\n") != NULL);
        CHECK(read_file(ends)[0] == '\0');
        /* ... and the first: no line promises a setting, and its lazy run
           follows the table, not "this start only" */
        run(argv[0], "act2dbatch", log);
        setenv("ICO_TEST_TABLE_LAZY", "Adreno 740, issues 93 and 107", 1);
        setenv("ICO_TEST_THEN", "between", 1);
        r = run(argv[0], "actpipeset", log);
        unsetenv("ICO_TEST_THEN");
        unsetenv("ICO_TEST_TABLE_LAZY");
        s = read_file(log);
        CHECK(strstr(s, "ico_pc: the previous start ended while preparing the graphics (" NOTE_2D
                        "); nothing is counted or written to config.toml: the device table "
                        "already makes the pipelines as the game needs them on this device "
                        "(Adreno 740, issues 93 and 107)\n") != NULL);
        CHECK(strstr(s, "written for good") == NULL);
        CHECK(strstr(s, "makes the setting permanent") == NULL);
        CHECK(read_file(ends)[0] == '\0');
        s = read_file(act);
        CHECK(strcmp(s, "test 1\nrunning with the pipelines created as the frames need them "
                        "(none was being created)") == 0);
        /* a lazy run ended in the background between two creations: the
           next start says so */
        remove(ends);
        run(argv[0], "act2dbatch", log);
        setenv("ICO_TEST_THEN", "betweenbg", 1);
        r = run(argv[0], "actpipeset", log);
        unsetenv("ICO_TEST_THEN");
        r = run(argv[0], "actpipeset", log);
        s = read_file(log);
        CHECK(strstr(s, "ico_pc: the previous run was in the background when it ended (the app "
                        "went to the background with 0 pipelines made)\n") != NULL);
        CHECK(strstr(s, "that lazy run was for that start only") != NULL);
        /* the app in the background before the recording began (Home while
           the renderer starts): marked when it begins */
        remove(ends);
        setenv("ICO_TEST_BACKGROUND", "early", 1);
        r = run(argv[0], "act2dbatch", log);
        unsetenv("ICO_TEST_BACKGROUND");
        s = read_file(log);
        CHECK(strstr(s, "ico_pc: the app was in the background before preparing the graphics "
                        "began (with 0 pipelines made); an end there is not counted as one while "
                        "preparing the graphics\n") != NULL);
        r = run(argv[0], "actpipeset", log);
        s = read_file(log);
        CHECK(strstr(s, ", in the background (the app was in the background before preparing the "
                        "graphics began (with 0 pipelines made)); the system ends apps there") !=
              NULL);
        CHECK(strstr(s, "starts in a row") == NULL);
        CHECK(strstr(s, "test: pipeline set 0\n") != NULL);
        /* an extended VU batch's end makes the plain variants this start's
           instead (nothing written, the set as usual); a plain run in
           between leaves the row, and the next end inside an extended
           batch writes [dev] vu_plain_only for good */
        remove(ends);
        run(argv[0], "actvuext", log);
        setenv("ICO_TEST_THEN", "clean", 1);
        r = run(argv[0], "actpipeset", log);
        unsetenv("ICO_TEST_THEN");
        s = read_file(log);
        CHECK(strstr(s, "ico_pc: starts in a row that ended while preparing the graphics (the "
                        "extended VU shaders): 1\n") != NULL);
        CHECK(strstr(s, "ico_pc: the previous start ended while the graphics driver compiled the "
                        "largest 3D shaders (the VU extended variants); this start only uses the "
                        "smaller ones (nothing is written to config.toml): the wide-screen rule "
                        "is off for this start; if a start that compiles them ends there again, "
                        "[dev] vu_plain_only = true is written for good\n") != NULL);
        CHECK(strstr(s, "test: config write") == NULL);
        CHECK(strstr(s, "test: vu plain 1\n") != NULL);
        CHECK(strstr(s, "test: pipeline set 0\n") != NULL);
        run(argv[0], "actvuext", log);
        r = run(argv[0], "actpipeset", log);
        s = read_file(log);
        CHECK(strstr(s, "test: config write dev.vu_plain_only = true\n") != NULL);
        CHECK(strstr(s, "ico_pc: the previous start ended while the graphics driver compiled the "
                        "largest 3D shaders (the VU extended variants); this start and every "
                        "start after it use the smaller ones only ([dev] vu_plain_only = true in "
                        "config.toml): the wide-screen rule is off; delete that line to try the "
                        "larger ones again\n") != NULL);
        CHECK(strstr(s, "test: vu plain 2\n") != NULL);
        CHECK(strstr(s, "test: pipeline set 0\n") != NULL);
        /* ... a run that compiled the extended ones and finished clears
           the row */
        run(argv[0], "actvuext", log);
        setenv("ICO_TEST_THEN", "cleanvu", 1);
        run(argv[0], "actpipeset", log);
        unsetenv("ICO_TEST_THEN");
        run(argv[0], "actvuext", log);
        r = run(argv[0], "actpipeset", log);
        s = read_file(log);
        CHECK(strstr(s, "test: config write") == NULL);
        CHECK(strstr(s, "test: vu plain 1\n") != NULL);
        /* ... and on a device the table draws with the plain ones only
           (RhiLimits.vuPlainOnly) nothing is written; its lazy rule alone
           does not stop the write */
        run(argv[0], "actvuext", log);
        setenv("ICO_TEST_TABLE_VU", "Adreno 740, issue 107", 1);
        r = run(argv[0], "actpipeset", log);
        unsetenv("ICO_TEST_TABLE_VU");
        s = read_file(log);
        CHECK(strstr(s, "test: config write") == NULL);
        CHECK(strstr(s, "[dev] vu_plain_only = true is not written to config.toml: the device "
                        "table already draws with the smaller 3D shaders (the VU plain variants) "
                        "only on this device (Adreno 740, issue 107)\n") != NULL);
        CHECK(strstr(s, "test: vu plain 2\n") != NULL);
        run(argv[0], "actvuext", log);
        run(argv[0], "actpipeset", log);
        run(argv[0], "actvuext", log);
        setenv("ICO_TEST_TABLE_LAZY", "Adreno 740, issues 93 and 107", 1);
        r = run(argv[0], "actpipeset", log);
        unsetenv("ICO_TEST_TABLE_LAZY");
        s = read_file(log);
        CHECK(strstr(s, "test: config write dev.vu_plain_only = true\n") != NULL);
        CHECK(strstr(s, "test: vu plain 2\n") != NULL);
        /* no file: the set; the setting "lazy": none, nothing written */
        r = run(argv[0], "actpipeset", log);
        s = read_file(log);
        CHECK(strstr(s, "test: pipeline set 0\n") != NULL);
        setenv("ICO_TEST_PIPESET", "lazy", 1);
        r = run(argv[0], "actpipeset", log);
        unsetenv("ICO_TEST_PIPESET");
        s = read_file(log);
        CHECK(strstr(s, "test: config write") == NULL);
        CHECK(strstr(s, "test: pipeline set 2\n") != NULL);
        /* issue 93: a low-memory signal before the end: logged at once with
           the set's count and the memory, kept in the file after the
           batch's line, logged by the next start */
        remove(ends);
        setenv("ICO_TEST_LOW_MEMORY", "1", 1);
        r = run(argv[0], "act2dbatch", log);
        unsetenv("ICO_TEST_LOW_MEMORY");
        s = read_file(log);
        CHECK(r == 0);
        CHECK(strstr(s, "ico_pc: the system asked the app to free memory: low memory "
                        "(onTrimMemory level 15, running critical; signal 1 of this run) at "
                        "pipelines 201 of 527 of the start-up set; memory ") != NULL);
        s = read_file(act);
        CHECK(strncmp(s,
                      "test 1\n" NOTE_2D "\ncompiled: " TIMES_2D
                      "\nsystem: low memory (onTrimMemory level 15, running critical; signal 1 "
                      "of this run) at pipelines 201 of 527 of the start-up set; memory ",
                      strlen("test 1\n" NOTE_2D "\ncompiled: " TIMES_2D
                             "\nsystem: low memory (onTrimMemory level 15, running critical; "
                             "signal 1 of this run) at pipelines 201 of 527 of the start-up set; "
                             "memory ")) == 0);
        r = run(argv[0], "actpipeset", log);
        s = read_file(log);
        CHECK(strstr(s, "ico_pc: the previous run (version test 1) was told by the system: low "
                        "memory (onTrimMemory level 15, running critical; signal 1 of this run) "
                        "at pipelines 201 of 527 of the start-up set; memory ") != NULL);
        CHECK(strstr(s, "ico_pc: the previous run's last finished batch: " TIMES_2D "\n") != NULL);
        CHECK(strstr(s, "test: pipeline set 4\n") != NULL);
        /* a run without the set ended between two creations, after its
           lazy batch and a signal: the batch and the signal are logged,
           the next start of the version is lazy from the file, and nothing
           is written (no pipeline was being made) */
        r = run(argv[0], "actlowlazy", log);
        s = read_file(log);
        CHECK(strstr(s, "ico_pc: the system asked the app to free memory: the app is in the "
                        "background (onTrimMemory level 80, complete, among the next to be "
                        "ended; signal 1 of this run) with 57 pipelines made; memory ") != NULL);
        CHECK(strstr(s, "ico_pc: the app went to the background with 57 pipelines made; an end "
                        "there is not counted as one while preparing the graphics\n") != NULL);
        r = run(argv[0], "actpipeset", log);
        s = read_file(log);
        CHECK(strstr(s, "ico_pc: the previous run (version test 1) compiled: " TIMES_LAZY "\n") !=
              NULL);
        CHECK(strstr(s, "ico_pc: the previous run (version test 1) was told by the system: the "
                        "app is in the background (onTrimMemory level 80, complete, among the "
                        "next to be ended; signal 1 of this run) with 57 pipelines made") != NULL);
        CHECK(strstr(s, "test: lazy 1\n") != NULL);
        CHECK(strstr(s, "test: config write") == NULL);
        CHECK(strstr(s, "test: pipeline set 1\n") != NULL);
        remove(act);
        /* a run ended between a shorter note's write and the cut: the
           padding's NUL ends the note (diag_host.c act_write) */
        {
            static const char cut[] = "test 1\ncreating pipeline a\0\0\0\0 (start-up set)";
            FILE *f = fopen(act, "wb");
            CHECK(f != NULL);
            if (f != NULL) {
                fwrite(cut, 1, sizeof cut - 1, f);
                fclose(f);
            }
        }
        r = run(argv[0], "actnext", log);
        s = read_file(log);
        CHECK(r == 0);
        CHECK(strstr(s, "ico_pc: the previous run ended while: creating pipeline a\n") != NULL);
        CHECK(strstr(s, "test: lazy 1") != NULL);
        remove(act);
        remove(ends);
        remove(bgmark);
        if (fails) {
            printf("--- activity log ---\n%s\n", read_file(log));
        }
    }

    /* the restart after a device loss, the relaunch stubbed */
    r = run(argv[0], "lossstep", log);
    s = read_file(log);
    CHECK(r == 0);
    CHECK(strstr(s, "the game starts again at the title when this run has closed, with "
                    "ICO_STARTUP_RETRY=blend:vulkan: ") != NULL);
    CHECK(strstr(s, "test: restart 1\n") != NULL);
    CHECK(strstr(s, "test: relaunch ICO_STARTUP_RETRY=blend:vulkan (parse 0, step 5, backend "
                    "vulkan, blend fixed)\n") != NULL);
    /* the normal quit path first, then the relaunch */
    CHECK(strstr(s, "test: restart 1") != NULL && strstr(s, "test: the end-of-run steps") != NULL &&
          strstr(s, "ico_pc: starting the game again") != NULL &&
          strstr(s, "test: relaunch ") != NULL &&
          strstr(s, "ico_pc: started the game again") != NULL &&
          strstr(s, "test: restart 1") < strstr(s, "test: the end-of-run steps") &&
          strstr(s, "test: the end-of-run steps") < strstr(s, "ico_pc: starting the game again") &&
          strstr(s, "ico_pc: starting the game again") < strstr(s, "test: relaunch ") &&
          strstr(s, "test: relaunch ") < strstr(s, "ico_pc: started the game again"));
    CHECK(strstr(s, "test: box:") == NULL);
    if (fails) {
        printf("--- lossstep log ---\n%s\n", s);
    }
    /* the next loss, on a build with D3D12: the other backend */
    r = run(argv[0], "lossbackend", log);
    s = read_file(log);
    CHECK(r == 0);
    CHECK(strstr(s, "test: restart 1\n") != NULL);
    CHECK(strstr(s, "test: relaunch ICO_STARTUP_RETRY=backend:d3d12 (parse 0, step 6, backend "
                    "d3d12, blend fixed)\n") != NULL);
    CHECK(strstr(s, "ico_pc: started the game again") != NULL);
    if (fails) {
        printf("--- lossbackend log ---\n%s\n", s);
    }
    /* no step left: 0 (the window shows its box), nothing at exit */
    r = run(argv[0], "lossnone", log);
    s = read_file(log);
    CHECK(r == 0);
    CHECK(strstr(s, "ico_pc: the graphics card stopped responding; the game does not start "
                    "again: ") != NULL);
    CHECK(strstr(s, "test: restart 0\n") != NULL);
    CHECK(strstr(s, "test: the end-of-run steps") != NULL);
    CHECK(strstr(s, "starting the game again") == NULL);
    CHECK(strstr(s, "test: relaunch ") == NULL);
    CHECK(strstr(s, "test: box:") == NULL);
    if (fails) {
        printf("--- lossnone log ---\n%s\n", s);
    }
    /* no relaunch prepared: 0 as well */
    r = run(argv[0], "lossunprepared", log);
    s = read_file(log);
    CHECK(r == 0);
    CHECK(strstr(s, "the game does not start again: the game cannot start itself again on this "
                    "system") != NULL);
    CHECK(strstr(s, "test: restart 0\n") != NULL);
    CHECK(strstr(s, "test: relaunch ") == NULL);
    if (fails) {
        printf("--- lossunprepared log ---\n%s\n", s);
    }
    /* the relaunch fails: the box, after the attempt */
    setenv("ICO_TEST_LOSS_FAIL", "1", 1);
    r = run(argv[0], "lossstep", log);
    unsetenv("ICO_TEST_LOSS_FAIL");
    s = read_file(log);
    CHECK(r == 0);
    CHECK(strstr(s, "test: relaunch ICO_STARTUP_RETRY=blend:vulkan") != NULL);
    CHECK(strstr(s, "ico_pc: could not start the game again") != NULL);
    CHECK(strstr(s, "ico_pc: started the game again") == NULL);
    CHECK(strstr(s, "test: box: The graphics card stopped responding, and the game could not "
                    "start itself again.") != NULL);
    CHECK(strstr(s, "test: relaunch ") != NULL && strstr(s, "test: box:") != NULL &&
          strstr(s, "test: relaunch ") < strstr(s, "test: box:"));
    if (fails) {
        printf("--- lossfail log ---\n%s\n", s);
    }

    /* the loss dump and the loss bisect's file steps, the bisect stubbed */
    {
        char act[300];
        act_path(act, sizeof act, log);
        remove(act);
        /* a loss on a run at the restart after a loss (the relaunch
           stubbed): the file keeps it through the run's end */
        setenv("ICO_TEST_LOSS_ACT", "/logs/loss-20261015-101500.zip", 1);
        r = run(argv[0], "lossstep", log);
        unsetenv("ICO_TEST_LOSS_ACT");
        CHECK(r == 0);
        CHECK(strstr(read_file(log), "test: relaunch ICO_STARTUP_RETRY=blend:vulkan") != NULL);
        s = read_file(act);
        CHECK(strcmp(s, "test 1\n\nloss dump: /logs/loss-20261015-101500.zip") == 0);
        /* the next start names it, bisects once before the game, logs the
           pass and deletes the file; the start-up set then ends normally */
        setenv("ICO_TEST_BISECT", "lost", 1);
        r = run(argv[0], "actbisect", log);
        s = read_file(log);
        CHECK(r == 0);
        CHECK(strstr(s, "ico_pc: the previous run lost the graphics device; its loss dump is "
                        "/logs/loss-20261015-101500.zip\n") != NULL);
        CHECK(strstr(s, "test: lazy 0") != NULL);
        CHECK(strstr(s, "test: loss dump /logs/loss-20261015-101500.zip\n") != NULL);
        CHECK(strstr(s, "test: bisect stub given /logs/loss-20261015-101500.zip\n") != NULL);
        CHECK(strstr(s, "ico_pc: loss bisect: pass 12 of 61, list 3, pipeline 409 (vs 7 fs 5 dst "
                        "0), pass colour 4 (640x448), 14 draws\n") != NULL);
        CHECK(strstr(s, "ico_pc: loss bisect: done; the game starts\n") != NULL);
        CHECK(strstr(s, "test: bisect 1\n") != NULL);
        CHECK(read_file(act)[0] == '\0');
        /* ... once: the start after has nothing to bisect */
        r = run(argv[0], "actbisect", log);
        s = read_file(log);
        CHECK(strstr(s, "test: loss dump (none)") != NULL);
        CHECK(strstr(s, "test: bisect -1\n") != NULL);
        CHECK(strstr(s, "loss bisect:") == NULL);
        CHECK(strstr(s, "test: bisect stub") == NULL);
        /* a clean frame, and a bisect that could not run, are logged too;
           the run killed after its loss still leaves the loss */
        setenv("ICO_TEST_LOSS_ZIP", "/logs/loss-a.zip", 1);
        setenv("ICO_TEST_LOSS_KILL", "1", 1);
        run(argv[0], "actloss", log);
        unsetenv("ICO_TEST_LOSS_KILL");
        CHECK(strcmp(read_file(act), "test 1\n\nloss dump: /logs/loss-a.zip") == 0);
        setenv("ICO_TEST_BISECT", "clean", 1);
        r = run(argv[0], "actbisect", log);
        s = read_file(log);
        CHECK(strstr(s, "ico_pc: loss bisect: the whole frame (61 passes) ran without losing the "
                        "device\n") != NULL);
        CHECK(strstr(s, "test: bisect 2\n") != NULL);
        run(argv[0], "actloss", log);
        setenv("ICO_TEST_BISECT", "none", 1);
        r = run(argv[0], "actbisect", log);
        s = read_file(log);
        CHECK(strstr(s, "ico_pc: loss bisect: not run: the renderer did not start\n") != NULL);
        CHECK(strstr(s, "ico_pc: loss bisect: done; the game starts\n") != NULL);
        CHECK(strstr(s, "test: bisect 0\n") != NULL);
        /* a start ended inside the bisect: the bisect's note; the start
           after names it, is not lazy and does not bisect again */
        run(argv[0], "actloss", log);
        setenv("ICO_TEST_BISECT", "die", 1);
        r = run(argv[0], "actbisect", log);
        CHECK(r == 0);
        CHECK(strstr(read_file(log), "test: bisect stub given /logs/loss-a.zip") != NULL);
        CHECK(strstr(read_file(log), "test: bisect ") != NULL &&
              strstr(read_file(log), "loss bisect: done") == NULL);
        CHECK(strcmp(read_file(act), "test 1\nreplaying the lost frame pass by pass (the loss "
                                     "bisect)") == 0);
        setenv("ICO_TEST_NO_RECORD", "1", 1);
        r = run(argv[0], "actbisect", log);
        unsetenv("ICO_TEST_NO_RECORD");
        s = read_file(log);
        CHECK(strstr(s, "ico_pc: the previous run ended while: replaying the lost frame pass by "
                        "pass (the loss bisect); it is not run again\n") != NULL);
        CHECK(strstr(s, "test: lazy 0") != NULL);
        CHECK(strstr(s, "test: bisect -1\n") != NULL);
        CHECK(read_file(act)[0] == '\0');
        /* a lazy run's loss: the lazy note beside the loss; the next start
           is lazy and bisects */
        run(argv[0], "actrecord", log);
        setenv("ICO_TEST_BISECT", "lost", 1);
        r = run(argv[0], "actloss", log);
        CHECK(strstr(read_file(log), "test: lazy 1") != NULL);
        CHECK(strcmp(read_file(act), "test 1\nexited with the pipelines created as the frames "
                                     "needed them\nloss dump: /logs/loss-a.zip") == 0);
        setenv("ICO_TEST_NO_RECORD", "1", 1);
        r = run(argv[0], "actbisect", log);
        unsetenv("ICO_TEST_NO_RECORD");
        s = read_file(log);
        CHECK(strstr(s, "this one does too") != NULL);
        CHECK(strstr(s, "test: lazy 1") != NULL);
        CHECK(strstr(s, "test: bisect 1\n") != NULL);
        unsetenv("ICO_TEST_LOSS_ZIP");
        /* a loss with no dump: named, not bisected */
        run(argv[0], "actloss", log);
        CHECK(strcmp(read_file(act), "test 1\n\ngraphics device lost, no loss dump") == 0);
        r = run(argv[0], "actbisect", log);
        s = read_file(log);
        CHECK(strstr(s, "ico_pc: the previous run lost the graphics device (no loss dump was "
                        "written)\n") != NULL);
        CHECK(strstr(s, "test: bisect -1\n") != NULL);
        /* a loss from another version: named, not bisected */
        setenv("ICO_TEST_LOSS_ZIP", "/logs/loss-b.zip", 1);
        run(argv[0], "actloss", log);
        unsetenv("ICO_TEST_LOSS_ZIP");
        setenv("ICO_TEST_ACT_VERSION", "test 2", 1);
        r = run(argv[0], "actbisect", log);
        unsetenv("ICO_TEST_ACT_VERSION");
        s = read_file(log);
        CHECK(strstr(s, "ico_pc: the previous run (version test 1) lost the graphics device; its "
                        "loss dump /logs/loss-b.zip is not replayed on test 2\n") != NULL);
        CHECK(strstr(s, "test: bisect -1\n") != NULL);
        unsetenv("ICO_TEST_BISECT");
        remove(act);
        if (fails) {
            printf("--- loss bisect log ---\n%s\n", read_file(log));
        }
    }

    /* the loss bisect's second level, its step stubbed */
    {
        char act[300], st[300];
        FILE *f;
        act_path(act, sizeof act, log);
        snprintf(st, sizeof st, "%s.bisect", log);
        remove(act);
        remove(st);
        /* draw 731 of 1312 found, alone, its classes, in 4 starts: at
           most 3 lost steps a start (Windows' 5 resets in 60 s) */
        setenv("ICO_TEST_BISECT2_BEGIN", "/logs/loss-c.zip", 1);
        r = run(argv[0], "actbisect2", log);
        unsetenv("ICO_TEST_BISECT2_BEGIN");
        s = read_file(log);
        CHECK(r == 0);
        CHECK(strstr(s, "ico_pc: loss bisect: narrowing pass 1 (1312 draws) draw by draw") != NULL);
        CHECK(strstr(s, "ico_pc: loss bisect: step 1 of 16: pass 1, draws 1..1312 of 1312, marks "
                        "masked: lost in 0.6 s\n") != NULL);
        CHECK(strstr(s, "ico_pc: loss bisect: step 2 of 16: pass 1, draws 1..656 of 1312: clean "
                        "in 0.6 s\n") != NULL);
        CHECK(strstr(s, "ico_pc: loss bisect: 3 lost steps in this start; step 5 waits for the "
                        "next start\n") != NULL);
        CHECK(strstr(s, "ico_pc: loss bisect: step 5 of 16") == NULL);
        CHECK(strstr(s, "test: bisect2 1\n") != NULL);
        r = run(argv[0], "actbisect2", log);
        s = read_file(log);
        CHECK(strstr(s, "continuing at step 5 (start 2 of 4)\n") != NULL);
        CHECK(strstr(s, "ico_pc: loss bisect: 3 lost steps in this start; step 13 waits for the "
                        "next start\n") != NULL);
        r = run(argv[0], "actbisect2", log);
        CHECK(strstr(read_file(log), "; step 16 waits for the next start\n") != NULL);
        r = run(argv[0], "actbisect2", log);
        s = read_file(log);
        CHECK(strstr(s, "continuing at step 16 (start 4 of 4)\n") != NULL);
        CHECK(strstr(s,
                     "ico_pc: loss bisect: pass 1, draw 731 of 1312 (list 0 command 731, mesh "
                     "st26a_p1, vs 5 fs 5): faults alone; lost with the marks masked\n") != NULL);
        CHECK(read_file(st)[0] == '\0');
        CHECK(read_file(act)[0] == '\0');
        if (fails) {
            printf("--- bisect2 log ---\n%s\n", s);
        }
        /* killed at step 3: the next start continues at step 4 */
        setenv("ICO_TEST_BISECT2", "lost,clean,die", 1);
        setenv("ICO_TEST_BISECT2_BEGIN", "/logs/loss-c.zip", 1);
        r = run(argv[0], "actbisect2", log);
        unsetenv("ICO_TEST_BISECT2_BEGIN");
        s = read_file(log);
        CHECK(strstr(s, "test: bisect2 step 3 given /logs/loss-c.zip") != NULL);
        CHECK(strstr(s, "test: bisect2 ") != NULL &&
              strstr(s, "loss bisect: step 3 of 16") == NULL);
        CHECK(strstr(read_file(st), "\nstep 3 prefix 1 984 all 0 - 0 key 0 0 0 0 0 0 note \n") !=
              NULL);
        CHECK(strcmp(read_file(act), "test 1\nreplaying the lost frame draw by draw (the loss "
                                     "bisect)") == 0);
        unsetenv("ICO_TEST_BISECT2");
        r = run(argv[0], "actbisect2", log);
        s = read_file(log);
        CHECK(strstr(s, "ico_pc: the previous run ended while: replaying the lost frame draw by "
                        "draw (the loss bisect); it continues\n") != NULL);
        CHECK(strstr(s, "test: lazy 0") != NULL);
        CHECK(strstr(s, "ico_pc: loss bisect: continuing at step 4 (start 2 of 4; step 3 ended "
                        "the run)\n") != NULL);
        CHECK(strstr(s, "test: bisect2 step 3 given") == NULL);
        CHECK(strstr(s, "test: bisect2 step 4 given") != NULL);
        /* step 3 (1..984) killed counts as lost: step 4 asks 1..820 */
        CHECK(strstr(s, "ico_pc: loss bisect: step 4 of 16: pass 1, draws 1..820 of 1312:") !=
              NULL);
        CHECK(strstr(s, "; step 10 waits for the next start\n") != NULL);
        run(argv[0], "actbisect2", log);
        r = run(argv[0], "actbisect2", log);
        CHECK(strstr(read_file(log), "ico_pc: loss bisect: pass 1, draw 731 of 1312") != NULL);
        CHECK(read_file(st)[0] == '\0');
        if (fails) {
            printf("--- bisect2 killed log ---\n%s\n", s);
        }
        /* noinit: the start stops, the step runs again at the next */
        setenv("ICO_TEST_BISECT2", "lost,noinit", 1);
        setenv("ICO_TEST_BISECT2_BEGIN", "/logs/loss-c.zip", 1);
        r = run(argv[0], "actbisect2", log);
        unsetenv("ICO_TEST_BISECT2_BEGIN");
        s = read_file(log);
        CHECK(strstr(s, "ico_pc: loss bisect: step 2 of 16: pass 1, draws 1..656 of 1312: the "
                        "renderer did not start; the step runs again at the next start\n") != NULL);
        CHECK(strstr(s, "test: bisect2 3\n") != NULL);
        CHECK(strstr(read_file(st), "\nstep 1 mask 1 1312 mask_marks 0 lost 600 ") != NULL &&
              strstr(read_file(st), "\nstep 2 ") == NULL);
        /* the 4-start bound: noinit at the first step of starts 2, 3 and 4 */
        setenv("ICO_TEST_BISECT2", "lost,noinit,noinit,noinit", 1);
        r = run(argv[0], "actbisect2", log);
        CHECK(strstr(read_file(log), "continuing at step 2 (start 2 of 4)\n") != NULL);
        r = run(argv[0], "actbisect2", log);
        CHECK(strstr(read_file(log), "continuing at step 2 (start 3 of 4)\n") != NULL);
        r = run(argv[0], "actbisect2", log);
        CHECK(strstr(read_file(log), "continuing at step 2 (start 4 of 4)\n") != NULL);
        r = run(argv[0], "actbisect2", log);
        s = read_file(log);
        CHECK(strstr(s, "ico_pc: loss bisect: pass 1 of 1312 draws: not finished; lost with the "
                        "marks masked (4 starts)\n") != NULL);
        CHECK(strstr(s, "test: bisect2 0\n") != NULL);
        CHECK(read_file(st)[0] == '\0');
        unsetenv("ICO_TEST_BISECT2");
        /* the 16-step bound: draw 1312 (the classes fill the steps before
           the whole pass is asked again) */
        setenv("ICO_TEST_BISECT2_D", "1312", 1);
        setenv("ICO_TEST_BISECT2_BEGIN", "/logs/loss-c.zip", 1);
        r = run(argv[0], "actbisect2", log);
        unsetenv("ICO_TEST_BISECT2_BEGIN");
        CHECK(strstr(read_file(log), "; step 15 waits for the next start\n") != NULL);
        r = run(argv[0], "actbisect2", log);
        unsetenv("ICO_TEST_BISECT2_D");
        s = read_file(log);
        CHECK(strstr(s, "ico_pc: loss bisect: step 16 of 16:") != NULL);
        CHECK(strstr(s, "ico_pc: loss bisect: pass 1, draw 1312 of 1312 (list 0 command 1312, "
                        "mesh st26a_p1, vs 5 fs 5): faults alone; lost with the marks masked (16 "
                        "steps)\n") != NULL);
        CHECK(read_file(st)[0] == '\0');
        /* the time budget: two steps of 0.6 s in 1 s, the rest next start */
        setenv("ICO_TEST_BISECT2_BUDGET", "1000", 1);
        setenv("ICO_TEST_BISECT2_BEGIN", "/logs/loss-c.zip", 1);
        r = run(argv[0], "actbisect2", log);
        unsetenv("ICO_TEST_BISECT2_BEGIN");
        unsetenv("ICO_TEST_BISECT2_BUDGET");
        s = read_file(log);
        CHECK(strstr(s, "ico_pc: loss bisect: 1 s spent; step 3 waits for the next start\n") !=
              NULL);
        r = run(argv[0], "actbisect2", log);
        CHECK(strstr(read_file(log), "continuing at step 3 (start 2 of 4)\n") != NULL);
        run(argv[0], "actbisect2", log);
        r = run(argv[0], "actbisect2", log);
        CHECK(strstr(read_file(log), "ico_pc: loss bisect: pass 1, draw 731 of 1312") != NULL);
        CHECK(read_file(st)[0] == '\0');
        /* state files that do not parse, or another version's: deleted */
        {
            static const char *const bad[] = {
                "garbage\n",
                "ico-pc loss bisect 1\nversion test 1\nzip /z.zip\npass 99999 draws 4 passes "
                "60\nstarts 1\n",
                "ico-pc loss bisect 1\nversion test 1\nzip /z.zip\npass 1 draws 4 passes "
                "60\nstarts 1\nstep 2 prefix 1 2 all 0 lost 1 key 0 0 0 0 0 0 note \n",
                "ico-pc loss bisect 1\nversion test 1\nzip /z.zip\npass 1 draws 4 passes "
                "60\nstarts 1\nstep 1 sideways 1 2 all 0 lost 1 key 0 0 0 0 0 0 note \n",
                "ico-pc loss bisect 1\nversion test 1\nzip /z.zip\npass 1 draws 4 passes "
                "60\nstarts 9\n",
                "ico-pc loss bisect 2\nversion test 1\nzip /z.zip\npass 1 draws 4 passes "
                "60\nstarts 1\n",
            };
            for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
                f = fopen(st, "wb");
                if (f != NULL) {
                    fputs(bad[i], f);
                    fclose(f);
                }
                r = run(argv[0], "actbisect2", log);
                s = read_file(log);
                CHECK(strstr(s, "does not read as one; deleted") != NULL);
                CHECK(strstr(s, "test: bisect2 -1\n") != NULL);
                CHECK(read_file(st)[0] == '\0');
            }
            /* a line past the bound */
            f = fopen(st, "wb");
            if (f != NULL) {
                fputs("ico-pc loss bisect 1\nversion test 1\nzip /", f);
                for (int i = 0; i < 2000; i++) {
                    fputc('z', f);
                }
                fputs("\npass 1 draws 4 passes 60\nstarts 1\n", f);
                fclose(f);
            }
            r = run(argv[0], "actbisect2", log);
            CHECK(strstr(read_file(log), "does not read as one; deleted") != NULL);
            CHECK(read_file(st)[0] == '\0');
            /* another version's: deleted, named */
            f = fopen(st, "wb");
            if (f != NULL) {
                fputs("ico-pc loss bisect 1\nversion test 0\nzip /z.zip\npass 1 draws 4 passes "
                      "60\nstarts 1\nan unknown line\n",
                      f);
                fclose(f);
            }
            r = run(argv[0], "actbisect2", log);
            CHECK(strstr(read_file(log), "ico_pc: loss bisect: the state file is from version "
                                         "test 0; deleted (this is test 1)\n") != NULL);
            CHECK(read_file(st)[0] == '\0');
            /* a good one with an unknown line: continued */
            f = fopen(st, "wb");
            if (f != NULL) {
                fputs("ico-pc loss bisect 1\nversion test 1\nzip /z.zip\npass 1 draws 4 passes "
                      "60\nstarts 1\nan unknown line\n",
                      f);
                fclose(f);
            }
            setenv("ICO_TEST_BISECT2_D", "2", 1);
            r = run(argv[0], "actbisect2", log);
            CHECK(strstr(read_file(log), "continuing at step 1 (start 2 of 4)\n") != NULL);
            r = run(argv[0], "actbisect2", log);
            unsetenv("ICO_TEST_BISECT2_D");
            CHECK(strstr(read_file(log), "ico_pc: loss bisect: pass 1, draw 2 of 4") != NULL);
        }
        remove(act);
        remove(st);
        if (fails) {
            printf("--- bisect2 log ---\n%s\n", read_file(log));
        }
    }

    /* the reporter before the limits: they count from ico_diag_start */
    r = run(argv[0], "early", log);
    s = read_file(log);
    CHECK(r == 4);
    CHECK(strstr(s, "the watchdog starts with the game") != NULL);
    CHECK(strstr(s, "test: not fired yet") != NULL);
    CHECK(strstr(s, "WATCHDOG: no Main tick 1 s after boot started") != NULL);
    CHECK(strstr(s, "test: not fired yet") != NULL && strstr(s, "WATCHDOG:") != NULL &&
          strstr(s, "test: not fired yet") < strstr(s, "WATCHDOG:"));
    if (fails) {
        printf("--- early log ---\n%s\n", s);
    }

    remove(log);
    if (fails) {
        printf("diag_test: %d failures\n", fails);
        return 1;
    }
    printf("diag_test: ok\n");
    return 0;
#endif
}

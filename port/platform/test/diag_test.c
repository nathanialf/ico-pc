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
 *   - early:    the reporter started before the limits (Android's start):
 *               the first limit counts from ico_diag_start, not from the
 *               thread's start.
 * POSIX only (fork); elsewhere it reports skipped (77).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <eekernel.h>
#include "diag_host.h"
#include "sched.h"
#include "startup_retry.h"

#if !defined(_WIN32)

#include <pthread.h>
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

#if !defined(_WIN32)
/* an OS thread that is not a game thread faults, as a worker inside the
   graphics driver's pipeline compile would */
static void *segv_worker(void *arg)
{
    volatile int *p = (volatile int *)arg;
    ico_diag_milestone("segv worker runs");
    *p = 1; /* NULL */
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

/* The start-up activity file's steps, each ending as a killed process
   would (_exit: no clean-up) unless the step ends cleanly */
static void activity_child(const char *mode, const char *log)
{
    char path[300];
    int lazy;
    act_path(path, sizeof path, log);
    ico_diag_set_version(getenv("ICO_TEST_ACT_VERSION") != NULL ? getenv("ICO_TEST_ACT_VERSION")
                                                                : "test 1");
    lazy = ico_diag_startup_activity_begin(path, strcmp(mode, "actrelaunch") == 0);
    ico_diag_log("test: lazy %d", lazy);
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
    ico_sched_reset();
    sema = ico_sched_create_sema(1, 1, 0, 0);
    if (strncmp(mode, "act", 3) == 0) {
        activity_child(mode, log);
    } else if (strncmp(mode, "loss", 4) == 0) {
        loss_child(mode, log);
    } else if (strcmp(mode, "early") == 0) {
        early_child();
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
        if (pthread_create(&t, NULL, segv_worker, NULL) == 0) {
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
    if (fails) {
        printf("--- watchdog log ---\n%s\n", s);
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
    CHECK(strstr(s, " | pipelines 12 of 527, longest 1.3 s") != NULL);
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
        char act[300];
        act_path(act, sizeof act, log);
        remove(act);
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
           file) */
        run(argv[0], "actrecord", log);
        setenv("ICO_TEST_ACT_VERSION", "test 2", 1);
        r = run(argv[0], "actnext", log);
        unsetenv("ICO_TEST_ACT_VERSION");
        s = read_file(log);
        CHECK(r == 0);
        CHECK(strstr(s, "ico_pc: the previous run ended while: " BATCH_NOTE
                        " (version test 1); trying the start-up set again on test 2\n") != NULL);
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

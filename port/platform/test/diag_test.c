/*
 * port/platform/test/diag_test.c
 *
 * The diagnostics (diag_host.h), each case in a child process whose log is
 * then read back:
 *   - segv:     a game thread (fiber) dereferences NULL: exit 3, the log
 *               names the signal, the faulting address, the thread and its
 *               last kernel call, and lists stack candidates;
 *   - abort:    an assertion through ico_assert: exit 3 and the failure
 *               message in the report;
 *   - watchdog: a game thread spins without a kernel call, so the host loop
 *               never runs again: the watchdog (1 s) stops the run with
 *               exit 4, samples where the main thread is and dumps the
 *               threads, and the heartbeat reported "no progress" before;
 *   - progress: the Main tick stands still (a movie plays inside one) but
 *               the progress count moves for longer than the later limit:
 *               no fire; then both stop and the watchdog fires naming the
 *               movie, and the heartbeat printed the count;
 *   - stall:    the tick stands still and nothing else moves: the later
 *               limit fires as before;
 *   - present:  a fake present of 2.5 s inside a 1 s later limit (the tick
 *               stands still) does not fire; the stall after it does, 1 s
 *               after the present ended; a long present before the first
 *               tick does not trip a 1 s first limit either;
 *   - lines:    milestones and logs reach the file at once.
 * POSIX only (fork); elsewhere it reports skipped (77).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <eekernel.h>
#include "diag_host.h"
#include "sched.h"

#if !defined(_WIN32)

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

/* A present of 2.5 s (ico_diag_present_enter/leave) against a 1 s limit.
   first != 0: the first limit, before any tick; the run must end with exit 0.
   Otherwise the later limit: it must not fire inside the present and must
   after it. */
static void present_child(int first)
{
    fake_ticks = first ? 0 : 1;
    ico_diag_start(first ? 1 : 0, first ? 0 : 1);
    ico_diag_present_enter();
    pause_ms(2500);
    ico_diag_present_leave();
    ico_diag_log("test: present done");
    if (first) {
        pause_ms(300);
        exit(0);
    }
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
    if (strcmp(mode, "segv") == 0) {
        fn = segv_thread;
    } else if (strcmp(mode, "abort") == 0) {
        fn = abort_thread;
    } else if (strcmp(mode, "watchdog") == 0) {
        fn = spin_thread;
        ico_diag_start(1, 2);
    } else if (strcmp(mode, "progress") == 0 || strcmp(mode, "stall") == 0) {
        progress_child(strcmp(mode, "progress") == 0 ? 1 : 0);
    } else if (strcmp(mode, "present") == 0 || strcmp(mode, "presentfirst") == 0) {
        present_child(strcmp(mode, "presentfirst") == 0);
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
    if (fails) {
        printf("--- segv log ---\n%s\n", s);
    }

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
    if (fails) {
        printf("--- progress log ---\n%s\n", s);
    }

    /* without progress the tick alone counts, as before: it fires during
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

    /* a long present is not a stall: the later limit fires only after it */
    r = run(argv[0], "present", log);
    s = read_file(log);
    CHECK(r == 4);
    CHECK(strstr(s, "test: present done") != NULL);
    CHECK(strstr(s, "WATCHDOG: no new Main tick for 1 s") != NULL);
    CHECK(strstr(s, "test: present done") < strstr(s, "WATCHDOG:"));
    CHECK(strstr(s, "inside a present") == NULL);
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
    remove(log);
    if (fails) {
        printf("diag_test: %d failures\n", fails);
        return 1;
    }
    printf("diag_test: ok\n");
    return 0;
#endif
}

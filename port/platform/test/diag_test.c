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
    remove(log);
    if (fails) {
        printf("diag_test: %d failures\n", fails);
        return 1;
    }
    printf("diag_test: ok\n");
    return 0;
#endif
}

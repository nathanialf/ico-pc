/*
 * port/platform/test/fiber_test.c
 *
 * The context-switch layer (fiber.h): many fibers switched round-robin with
 * their stack contents checked across switches, deep stack use, destroying
 * suspended fibers, and the FP control state seen inside a fiber. Run under
 * the asan preset it also checks minicoro's sanitizer fiber annotations.
 *
 * `fiber_test guard` overflows a fiber's stack in a child process and
 * expects the guard page to stop it (exit status 77 where there is no guard
 * page of ours, which ctest reports as skipped).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fiber.h"
#include "fpenv.h"

#if !defined(_WIN32)
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

#define N_FIBERS 64
#define ROUNDS 200

static int counters[N_FIBERS];

/* Each fiber fills a local array with its own pattern, yields, and checks
   the pattern survived the other fibers' runs. */
static void worker(void *arg)
{
    int idx = (int)(size_t)arg;
    volatile unsigned char local[1024];
    int r;
    size_t i;
    for (r = 0; r < ROUNDS; r++) {
        for (i = 0; i < sizeof local; i++) {
            local[i] = (unsigned char)(idx * 31 + r + i);
        }
        ico_fiber_yield();
        for (i = 0; i < sizeof local; i++) {
            if (local[i] != (unsigned char)(idx * 31 + r + i)) {
                fails++;
                return;
            }
        }
        counters[idx]++;
    }
}

static void test_round_robin(void)
{
    IcoFiber *f[N_FIBERS];
    int i;
    int r;
    for (i = 0; i < N_FIBERS; i++) {
        f[i] = ico_fiber_create(worker, (void *)(size_t)i, ICO_FIBER_STACK_SIZE);
        CHECK(f[i] != NULL);
    }
    for (r = 0; r <= ROUNDS; r++) {
        for (i = 0; i < N_FIBERS; i++) {
            if (!ico_fiber_finished(f[i])) {
                CHECK(ico_fiber_resume(f[i]) == 0);
            }
        }
    }
    for (i = 0; i < N_FIBERS; i++) {
        CHECK(ico_fiber_finished(f[i]));
        CHECK(counters[i] == ROUNDS);
        ico_fiber_destroy(f[i]);
    }
}

/* About 200 KB of a 256 KB stack, in frames of 4 KB. */
static int recurse(int depth)
{
    volatile char frame[4096];
    frame[0] = (char)depth;
    frame[sizeof frame - 1] = (char)depth;
    if (depth == 0) {
        ico_fiber_yield();
        return frame[0] + frame[sizeof frame - 1];
    }
    return recurse(depth - 1) + (frame[0] == (char)depth) +
           (frame[sizeof frame - 1] == (char)depth);
}

static int deep_result;

static void deep(void *arg)
{
    (void)arg;
    deep_result = recurse(50);
}

static void test_deep_stack(void)
{
    IcoFiber *f = ico_fiber_create(deep, NULL, ICO_FIBER_STACK_SIZE);
    CHECK(f != NULL);
    CHECK(ico_fiber_resume(f) == 0); /* yields at the bottom */
    CHECK(!ico_fiber_finished(f));
    CHECK(ico_fiber_resume(f) == 0);
    CHECK(ico_fiber_finished(f));
    CHECK(deep_result == 100);
    ico_fiber_destroy(f);
}

#define HOST_NESTED_SLOT 11

static IcoFiber *host_test_fiber;
static IcoFiber *host_nested_fiber;
static int host_stack_used;
static int host_nested_runs;

static int host_recurse(int depth)
{
    volatile unsigned char frame[4096];
    int result = 0;
    frame[0] = (unsigned char)depth;
    frame[sizeof frame - 1] = (unsigned char)depth;
    if (depth == 0) {
        void *lo;
        void *hi;
        CHECK(ico_fiber_current_stack(&lo, &hi) == 0);
        host_stack_used = (uintptr_t)&frame[0] >= (uintptr_t)lo &&
                          (uintptr_t)&frame[0] < (uintptr_t)hi &&
                          (uintptr_t)hi - (uintptr_t)&frame[0] > ICO_FIBER_STACK_SIZE;
        ico_fiber_yield();
    } else {
        result = host_recurse(depth - 1) + 1;
    }
    CHECK(frame[0] == (unsigned char)depth && frame[sizeof frame - 1] == (unsigned char)depth);
    return result;
}

static void host_nested(void *arg)
{
    (void)arg;
    host_nested_runs++;
    ico_fiber_yield();
    host_nested_runs++;
}

static void host_large_stack(void *arg)
{
    void *lo;
    void *hi;
    (void)arg;
    CHECK(ico_fiber_current() == host_test_fiber);
    CHECK(ico_fiber_slot(host_test_fiber) == -1);
    CHECK(ico_fiber_current_stack(&lo, &hi) == 0);
    CHECK((uintptr_t)hi - (uintptr_t)lo == ICO_FIBER_HOST_STACK_SIZE);
    CHECK(host_recurse(80) == 80);
    CHECK(ico_fiber_resume(host_nested_fiber) == 0);
    CHECK(ico_fiber_current() == host_test_fiber);
    CHECK(!ico_fiber_finished(host_nested_fiber));
    ico_fiber_yield();
    CHECK(ico_fiber_in_slot(HOST_NESTED_SLOT) == NULL);
}

static void test_host_stack(void)
{
    IcoFiberImage img;
    IcoFiber *host;
    IcoFiber *nested;

    host_nested_runs = 0;
    host_stack_used = 0;
    nested = ico_fiber_create_in_slot(HOST_NESTED_SLOT, host_nested, NULL);
    host = ico_fiber_create_host(host_large_stack, NULL);
    CHECK(nested != NULL && host != NULL);
    if (nested == NULL || host == NULL) {
        if (nested != NULL)
            ico_fiber_destroy(nested);
        if (host != NULL)
            ico_fiber_destroy(host);
        return;
    }
    host_test_fiber = host;
    host_nested_fiber = nested;
    CHECK(ico_fiber_resume(host) == 0);
    CHECK(host_stack_used);
    CHECK(!ico_fiber_finished(host));
    CHECK(ico_fiber_resume(host) == 0);
    CHECK(host_nested_runs == 1);
    CHECK(!ico_fiber_finished(host));
    CHECK(ico_fiber_slot_save(HOST_NESTED_SLOT, &img) == 0);
    CHECK(img.stack_bytes > 0);
    CHECK(ico_fiber_resume(nested) == 0);
    CHECK(host_nested_runs == 2 && ico_fiber_finished(nested));
    ico_fiber_slot_discard(HOST_NESTED_SLOT);
    CHECK(ico_fiber_in_slot(HOST_NESTED_SLOT) == NULL);
    CHECK(ico_fiber_resume(host) == 0);
    CHECK(ico_fiber_finished(host));
    ico_fiber_destroy(host);
}

static void forever(void *arg)
{
    (void)arg;
    for (;;) {
        char buf[256];
        memset(buf, 0x5A, sizeof buf);
        ico_fiber_yield();
        CHECK(buf[0] == 0x5A && buf[255] == 0x5A);
    }
}

/* The scheduler destroys fibers that never finish (TerminateThread). */
static void test_destroy_suspended(void)
{
    int i;
    for (i = 0; i < 200; i++) {
        IcoFiber *f = ico_fiber_create(forever, NULL, ICO_FIBER_STACK_SIZE);
        CHECK(f != NULL);
        CHECK(ico_fiber_resume(f) == 0);
        CHECK(ico_fiber_resume(f) == 0);
        CHECK(ico_fiber_current() == NULL);
        ico_fiber_destroy(f);
    }
}

static unsigned long long fp_inside;
static IcoFiber *self_seen;

static void fp_probe(void *arg)
{
    self_seen = ico_fiber_current();
    (void)arg;
    fp_inside = ico_fpenv_raw();
}

/* The simulation's FP mode set on the host thread reaches a fiber when the
   start hook re-applies it (sched.c does; fiber.h says why). */
static void fp_hooked(void *arg)
{
    ico_fpenv_sim_enter();
    fp_probe(arg);
}

static void test_fp_state(void)
{
    IcoFiber *f;
    unsigned long long host;
    ico_fpenv_sim_enter();
    host = ico_fpenv_raw();
    f = ico_fiber_create(fp_hooked, NULL, ICO_FIBER_STACK_SIZE);
    CHECK(ico_fiber_resume(f) == 0);
    CHECK(self_seen == f);
    CHECK(fp_inside == host);
    CHECK(ico_fpenv_raw() == host);
    ico_fiber_destroy(f);
    ico_fpenv_host_enter();
}

#if !defined(_WIN32)
static int overflow(int n)
{
    volatile char frame[1024];
    frame[0] = (char)n;
    if (n == -1) { /* never: keeps the compiler from calling this endless */
        return 0;
    }
    return overflow(n + 1) + frame[0];
}

static void runaway(void *arg)
{
    (void)arg;
    printf("unreachable %d\n", overflow(0));
}

static int guard_case(int host)
{
    pid_t pid;
    int st;
    fflush(stdout);
    pid = fork();
    if (pid == 0) {
        IcoFiber *f = host ? ico_fiber_create_host(runaway, NULL)
                           : ico_fiber_create(runaway, NULL, ICO_FIBER_STACK_SIZE);
        if (f == NULL) {
            _exit(2); /* a failed allocation is not evidence for a working guard */
        }
        ico_fiber_resume(f);
        _exit(0); /* the overflow went unnoticed */
    }
    if (pid < 0 || waitpid(pid, &st, 0) != pid) {
        printf("FAIL: fork\n");
        return 1;
    }
    /* SIGSEGV, or the sanitizer's report and its exit status */
    if (WIFEXITED(st) && WEXITSTATUS(st) == 0) {
        printf("FAIL: a stack overflow in a %s fiber was not caught\n", host ? "host" : "pool");
        return 1;
    }
    if (WIFEXITED(st) && WEXITSTATUS(st) == 2) {
        printf("FAIL: could not allocate the %s fiber for the guard test\n",
               host ? "host" : "pool");
        return 1;
    }
    printf("%s overflow stopped (%s %d)\n", host ? "host" : "pool",
           WIFSIGNALED(st) ? "signal" : "exit status",
           WIFSIGNALED(st) ? WTERMSIG(st) : WEXITSTATUS(st));
    return 0;
}

static int guard_test(void)
{
    if (!ico_fiber_has_guard_page()) {
        printf("no guard page on this backend (%s)\n", ico_fiber_backend());
        return 77;
    }
    return guard_case(0) != 0 || guard_case(1) != 0;
}
#endif

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "guard") == 0) {
#if !defined(_WIN32)
        return guard_test();
#else
        return 77;
#endif
    }
    printf("fiber backend: %s, guard page: %s\n", ico_fiber_backend(),
           ico_fiber_has_guard_page() ? "yes" : "no");
    test_round_robin();
    test_deep_stack();
    test_host_stack();
    test_destroy_suspended();
    test_fp_state();
    if (fails != 0) {
        printf("%d failure(s)\n", fails);
        return 1;
    }
    printf("all passed\n");
    return 0;
}

/*
 * port/platform/test/ico_thread_test.c
 *
 * ico_thread.h: a thread runs and joins, a mutex and condition hand a
 * value from one thread to another, the clock does not run backwards, and
 * the hardware thread count is at least 1.
 */
#include <stdio.h>

#include "ico_thread.h"

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

typedef struct Box {
    IcoMutex *m;
    IcoCond *c;
    int go;
    int result;
} Box;

static void worker(void *arg)
{
    Box *b = (Box *)arg;

    ico_mutex_lock(b->m);
    while (!b->go) {
        ico_cond_wait(b->c, b->m);
    }
    b->result = 42;
    ico_mutex_unlock(b->m);
}

int main(void)
{
    Box b = {0};
    IcoThread *t;
    double t0, t1;

    b.m = ico_mutex_create();
    b.c = ico_cond_create();
    CHECK(b.m != NULL && b.c != NULL);
    t = ico_thread_create(worker, &b);
    CHECK(t != NULL);
    ico_mutex_lock(b.m);
    CHECK(b.result == 0); /* the worker waits for the signal */
    b.go = 1;
    ico_cond_broadcast(b.c);
    ico_mutex_unlock(b.m);
    ico_thread_join(t);
    CHECK(b.result == 42);
    ico_cond_destroy(b.c);
    ico_mutex_destroy(b.m);

    t0 = ico_thread_now_ms();
    t1 = ico_thread_now_ms();
    CHECK(t1 >= t0);
    CHECK(ico_thread_cpu_count() >= 1); /* the texture decode pool's size */

    if (fails == 0) {
        printf("ico_thread_test: ok\n");
    }
    return fails != 0;
}

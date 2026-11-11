/*
 * port/platform/test/sys_cell_test.c
 *
 * The last-value cell (sys_cell.h): empty, written and read back with its
 * time and cost, more values asked than written, a write in progress (an
 * odd sequence) read as nothing, and a writer thread against a reader
 * thread: every copy the reader gets is whole (its values agree).
 */
#include <stdio.h>
#include <string.h>

#include "ico_thread.h"
#include "sys_cell.h"

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static SysCell s_cell;
static int s_stop;

static void writer(void *u)
{
    (void)u;
    for (int i = 1; i <= 200000; i++) {
        const double v[3] = {(double)i, (double)i * 2.0, (double)i * 3.0};
        sys_cell_put(&s_cell, (double)i, 0.5, v, 3);
    }
    __atomic_store_n(&s_stop, 1, __ATOMIC_RELEASE);
}

int main(void)
{
    SysCell c;
    double v[SYS_CELL_VALUES], at = -1.0, took = -1.0;
    memset(&c, 0, sizeof c);
    CHECK(sys_cell_get(&c, v, 3, &at, &took) == 0 && at == -1.0);
    const double w[2] = {0.42, 3.0};
    sys_cell_put(&c, 1000.0, 1.5, w, 2);
    CHECK(sys_cell_get(&c, v, 6, &at, &took) == 2 && v[0] == 0.42 && v[1] == 3.0 && at == 1000.0 &&
          took == 1.5);
    CHECK(sys_cell_get(&c, v, 1, NULL, NULL) == 1 && v[0] == 0.42);
    c.seq |= 1u; /* a write in progress */
    CHECK(sys_cell_get(&c, v, 2, &at, &took) == 0);

    memset(&s_cell, 0, sizeof s_cell);
    IcoThread *t = ico_thread_create(writer, NULL);
    CHECK(t != NULL);
    unsigned got = 0, torn = 0;
    while (!__atomic_load_n(&s_stop, __ATOMIC_ACQUIRE)) {
        if (sys_cell_get(&s_cell, v, 3, &at, NULL) == 3) {
            got++;
            torn += !(v[1] == v[0] * 2.0 && v[2] == v[0] * 3.0 && at == v[0]);
        }
    }
    if (t != NULL) {
        ico_thread_join(t);
    }
    CHECK(torn == 0);
    CHECK(sys_cell_get(&s_cell, v, 3, &at, NULL) == 3 && v[0] == 200000.0);
    printf("sys_cell_test: %s (%u copies read during the writes)\n", failures ? "FAILED" : "ok",
           got);
    return failures ? 1 : 0;
}

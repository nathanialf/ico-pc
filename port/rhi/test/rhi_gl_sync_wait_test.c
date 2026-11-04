/* rhi_gl_sync_wait_test.c: the OpenGL backend's fence wait with a limit
 * (gl/gl_sync_wait.c, gl_device.c gl_wait_sync) on a stub fence and a fake
 * clock, CPU only: a fence that never signals ends at the limit (the GPU
 * hang limit, 15 s) in 50 ms slices with the pump between them and the 2 s
 * line once, instead of the loop without an end the rc2 code reading
 * found; one that signals after three slices returns at once with neither
 * line; a failed wait returns failed; no limit (the tests and tools) waits
 * out a 100 s fence in 1 s slices. */
#include <stdio.h>

#include "gl/gl_sync_wait.h"

static int fails;

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                    \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

typedef struct Stub {
    uint64_t clock;    /* the fake clock, ns */
    uint32_t signalAt; /* the slice that signals (0: never) */
    int fail;          /* every wait fails */
    uint32_t waits, pumps, slows;
    uint64_t slowAt;
} Stub;

static GlswSlice stubWait(void *u, uint64_t timeoutNs)
{
    Stub *s = u;
    s->waits++;
    if (s->fail) {
        return GLSW_SLICE_FAILED;
    }
    if (s->signalAt != 0 && s->waits >= s->signalAt) {
        s->clock += timeoutNs / 2;
        return GLSW_SLICE_SIGNALED;
    }
    s->clock += timeoutNs; /* a wait that times out takes its whole timeout */
    return GLSW_SLICE_TIMEOUT;
}

static uint64_t stubNow(void *u)
{
    return ((Stub *)u)->clock;
}

static void stubPump(void *u, uint64_t heldNs)
{
    (void)heldNs;
    ((Stub *)u)->pumps++;
}

static void stubSlow(void *u, uint64_t heldNs)
{
    Stub *s = u;
    s->slows++;
    s->slowAt = heldNs;
}

#define MS 1000000ull
#define S 1000000000ull

int main(void)
{
    {
        /* never signals: the GPU hang limit, 50 ms slices */
        Stub st = {1000 * S, 0, 0, 0, 0, 0, 0};
        const GlswOps ops = {&st, stubWait, stubNow, stubPump, stubSlow};
        uint64_t held = 0;
        uint32_t n = 0;
        const GlswResult r = gl_sync_wait_limited(&ops, 15 * S, 50 * MS, &held, &n);
        CHECK(r == GLSW_LIMIT);
        CHECK(held >= 15 * S && held < 15 * S + 50 * MS);
        CHECK(n == 300);
        CHECK(st.slows == 1 && st.slowAt >= 2 * S && st.slowAt < 2 * S + 50 * MS);
        CHECK(st.pumps == n - 1); /* between slices, not after the last */
    }
    {
        /* signals in the third slice */
        Stub st = {0, 3, 0, 0, 0, 0, 0};
        const GlswOps ops = {&st, stubWait, stubNow, stubPump, stubSlow};
        uint64_t held = 0;
        uint32_t n = 0;
        CHECK(gl_sync_wait_limited(&ops, 15 * S, 50 * MS, &held, &n) == GLSW_SIGNALED);
        CHECK(n == 3 && held == 125 * MS && st.slows == 0 && st.pumps == 2);
    }
    {
        /* a failed wait */
        Stub st = {0, 0, 1, 0, 0, 0, 0};
        const GlswOps ops = {&st, stubWait, stubNow, NULL, NULL};
        uint32_t n = 0;
        CHECK(gl_sync_wait_limited(&ops, 15 * S, 50 * MS, NULL, &n) == GLSW_FAILED);
        CHECK(n == 1);
    }
    {
        /* no limit: a 100 s fence waited out in 1 s slices, the line once */
        Stub st = {0, 101, 0, 0, 0, 0, 0};
        const GlswOps ops = {&st, stubWait, stubNow, NULL, stubSlow};
        uint64_t held = 0;
        CHECK(gl_sync_wait_limited(&ops, 0, S, &held, NULL) == GLSW_SIGNALED);
        CHECK(held == 100 * S + S / 2 && st.slows == 1 && st.slowAt == 2 * S);
    }
    if (fails) {
        printf("rhi_gl_sync_wait_test: %d check(s) failed\n", fails);
        return 1;
    }
    printf("rhi_gl_sync_wait_test: the limit, the slices, the pump and the 2 s line passed\n");
    return 0;
}

/*
 * port/render/test/rd_pipe_finish_test.c
 *
 * The background finisher's choices (rd_pipe_finish.h):
 *   pick     a set of 1196 keys stopped after 33 (a DXT-48 start):
 *            the batches go on from key 33 in the set's order, 32 a batch,
 *            the keys made meanwhile (on first use) and the failed ones
 *            skipped, a batch never mixing keys with and without a
 *            destination read (the key of the other kind starts the next
 *            batch), every key not made taken exactly once, 37 batches to
 *            the end, then none; a cursor at the end takes nothing;
 *   step     a batch in flight waits until its compiles end, then is
 *            taken, whatever the memory or a hold; the system's signal ends
 *            the finisher; no key left ends it; a late step or slow
 *            picture holds the next batch until its time; a reading under
 *            the floor holds it until the next reading's time; the memory
 *            is read only when nothing else holds the batch; then the next
 *            batch begins; off is idle; every step has its words.
 */
#include <stdio.h>
#include <string.h>
#include "rd_pipe_finish.h"

static int failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);               \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

#define SET 1196u
#define STOPPED 33u

typedef struct Keys {
    int state[SET]; /* -1 made or failed, 0/1 its destination-read kind */
    unsigned taken[SET];
} Keys;

static int keyOf(void *user, uint32_t i)
{
    const Keys *k = user;
    return k->state[i];
}

static void testPick(void)
{
    static Keys k;
    uint32_t out[RD_FINISH_BATCH], cursor = STOPPED, batches = 0, prev = 0;
    memset(&k, 0, sizeof(k));
    for (uint32_t i = 0; i < SET; i++) {
        /* the set's 2D keys first, some with a destination read in runs */
        k.state[i] = (i / 50u) % 3u == 2u ? 1 : 0;
        if (i < STOPPED) {
            k.state[i] = -1; /* the set's first batch, made */
        }
    }
    /* made on first use while the finisher ran, and a failed key */
    k.state[40] = k.state[41] = k.state[700] = k.state[1195] = -1;
    for (;;) {
        const uint32_t n = rd__finish_pick(SET, &cursor, RD_FINISH_BATCH, keyOf, &k, out);
        if (n == 0) {
            break;
        }
        batches++;
        CHECK(n <= RD_FINISH_BATCH);
        for (uint32_t j = 0; j < n; j++) {
            CHECK(out[j] >= STOPPED);
            CHECK(k.state[out[j]] == k.state[out[0]]); /* one kind a batch */
            CHECK(out[j] >= prev);                     /* the set's order */
            prev = out[j];
            k.taken[out[j]]++;
        }
        /* a short batch ends at a key of the other kind (or the set's end) */
        if (n < RD_FINISH_BATCH && cursor < SET) {
            CHECK(k.state[cursor] >= 0 && k.state[cursor] != k.state[out[0]]);
        }
    }
    for (uint32_t i = 0; i < SET; i++) {
        CHECK(k.taken[i] == (k.state[i] >= 0 ? 1u : 0u));
    }
    CHECK(cursor == SET);
    CHECK(batches >= (SET - STOPPED - 4u + RD_FINISH_BATCH - 1u) / RD_FINISH_BATCH);
    /* the runs of 50 split batches at each change of kind: 37 full batches
       would be the floor, the 23 kind changes past key 33 add the rest */
    printf("rd_pipe_finish_test: %u batches for %u keys left\n", batches, SET - STOPPED - 4u);
    CHECK(rd__finish_pick(SET, &cursor, RD_FINISH_BATCH, keyOf, &k, out) == 0);
    CHECK(rd__finish_pick(SET, &cursor, RD_FINISH_BATCH, NULL, &k, out) == 0);
    {
        /* every key one kind: 1163 keys in 37 batches, the last of 11 */
        uint32_t c = STOPPED, b = 0, last = 0;
        for (uint32_t i = 0; i < SET; i++) {
            k.state[i] = i < STOPPED ? -1 : 0;
        }
        for (uint32_t n; (n = rd__finish_pick(SET, &c, RD_FINISH_BATCH, keyOf, &k, out)) > 0;) {
            b++;
            last = n;
        }
        CHECK(b == 37u);
        CHECK(last == 11u);
    }
}

static RdFinishIn base(void)
{
    RdFinishIn in;
    memset(&in, 0, sizeof(in));
    in.on = true;
    in.left = 900;
    in.nowMs = 10000.0;
    in.memory = -1;
    return in;
}

static void testStep(void)
{
    RdFinishIn in = base();
    CHECK(rd__finish_step(NULL) == RD_FINISH_IDLE);
    in.on = false;
    CHECK(rd__finish_step(&in) == RD_FINISH_IDLE);
    in = base();
    CHECK(rd__finish_step(&in) == RD_FINISH_CHECK);
    in.memory = 0;
    CHECK(rd__finish_step(&in) == RD_FINISH_BEGIN);
    in.memory = 1;
    CHECK(rd__finish_step(&in) == RD_FINISH_WAIT_FLOOR);
    in.memory = 2;
    CHECK(rd__finish_step(&in) == RD_FINISH_STOP_SIGNAL);
    /* a batch in flight: taken once done, whatever else holds */
    in = base();
    in.inflight = true;
    in.memory = 2;
    in.holdMs = in.nowMs + 500.0;
    CHECK(rd__finish_step(&in) == RD_FINISH_WAIT_BATCH);
    in.finished = true;
    CHECK(rd__finish_step(&in) == RD_FINISH_END_BATCH);
    /* the holds, in time */
    in = base();
    in.holdMs = in.nowMs + RD_FINISH_HOLD_MS;
    CHECK(rd__finish_step(&in) == RD_FINISH_WAIT_HOLD);
    in.nowMs = in.holdMs;
    CHECK(rd__finish_step(&in) == RD_FINISH_CHECK);
    in.floorMs = in.nowMs + RD_FINISH_FLOOR_MS;
    CHECK(rd__finish_step(&in) == RD_FINISH_WAIT_FLOOR);
    in.nowMs = in.floorMs + 1.0;
    CHECK(rd__finish_step(&in) == RD_FINISH_CHECK);
    /* the end */
    in = base();
    in.left = 0;
    CHECK(rd__finish_step(&in) == RD_FINISH_DONE);
    in.memory = 2;
    CHECK(rd__finish_step(&in) == RD_FINISH_STOP_SIGNAL);
    for (int s = RD_FINISH_IDLE; s <= RD_FINISH_DONE; s++) {
        const char *w = rd__finish_step_words((RdFinishStep)s);
        CHECK(w != NULL && w[0] != '\0' && strcmp(w, "unknown") != 0);
    }
}

int main(void)
{
    testPick();
    testStep();
    printf("rd_pipe_finish_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}

/*
 * port/save/test/state_verdicts_test.c
 *
 * The Save states slots' verdicts on a worker and their hand-over to a load
 * (port/save/state_verdicts.h), with a fake piece of work whose start and
 * end the test controls.  CPU only.
 *
 *   arrives    a slot listed before its verdict: not known, queued; the
 *              worker started; the verdict arrives after the listing, the
 *              next listing has it; one piece of work
 *   waits      a load asked while the worker is on that slot waits for it
 *              (a second thread asks, the work is held open until the load
 *              is seen waiting, ico_vd_waiting) and takes the worker's
 *              result: one piece of work, no second pass
 *   first      a load waiting while the worker is on another slot goes
 *              before the worker's next queued slot (the work's order)
 *   cancel     the queue dropped while the worker is on a slot: that slot
 *              ends, no other is worked out, a later kick starts nothing
 *   changed    the file changed after its verdict (its time, then its
 *              header's build with the same size and time, then the
 *              settings): the listing queues it again, a load works it out
 *              itself
 *   nofile     a slot without a file: nothing to hand over
 *   nothread   no worker: the queue worked out inside the kick
 *   release    a replaced verdict's owned memory released, the rest at the end
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ico_check.h"
#include "ico_thread.h"
#include "state_verdicts.h"

typedef struct Res {
    int value;
    int computed_by_worker;
    int *owned; /* released by the release callback */
} Res;

static IcoVdKey g_key[ICO_VD_SLOTS + 1];
static int g_have[ICO_VD_SLOTS + 1];
static int g_value[ICO_VD_SLOTS + 1];
static int g_computes[ICO_VD_SLOTS + 1];
static int g_released;
static IcoMutex *g_mx;
static IcoCond *g_cv;
static int g_hold;                 /* the work waits while set */
static int g_in_work;              /* the work has started */
static int g_order[16], g_n_order; /* the slots in the order worked out */

static int key_now(int slot, void *user, IcoVdKey *key)
{
    (void)user;
    if (!g_have[slot]) {
        return -1;
    }
    *key = g_key[slot];
    return 0;
}

static int compute(int slot, void *user, void *result, IcoVdKey *key)
{
    Res *r = (Res *)result;
    static int one = 1;

    (void)user;
    ico_mutex_lock(g_mx);
    g_computes[slot]++;
    if (g_n_order < 16) {
        g_order[g_n_order++] = slot;
    }
    g_in_work = 1;
    ico_cond_broadcast(g_cv);
    while (g_hold) {
        ico_cond_wait(g_cv, g_mx);
    }
    ico_mutex_unlock(g_mx);
    if (!g_have[slot]) {
        return -1;
    }
    *key = g_key[slot];
    r->value = g_value[slot];
    r->owned = &one;
    return 0;
}

static void release(void *result, void *user)
{
    Res *r = (Res *)result;

    (void)user;
    if (r->owned != NULL) {
        g_released++;
        r->owned = NULL;
    }
}

static IcoVerdicts *make(int no_thread)
{
    IcoVdOps ops;

    memset(&ops, 0, sizeof ops);
    ops.compute = compute;
    ops.key_now = key_now;
    ops.release = release;
    ops.result_bytes = sizeof(Res);
    ops.slots = 4;
    ops.no_thread = no_thread;
    return ico_vd_create(&ops);
}

static void reset(void)
{
    int i;

    for (i = 0; i <= ICO_VD_SLOTS; i++) {
        g_key[i].size = 1000u + (unsigned)i;
        g_key[i].mtime = 1793000000 + i;
        g_key[i].keys = 0xABCDu;
        g_have[i] = 1;
        g_value[i] = 10 * i;
        g_computes[i] = 0;
    }
    g_released = 0;
    g_hold = 0;
    g_in_work = 0;
    g_n_order = 0;
}

/* until n callers wait for the work lock (bounded: 10 s) */
static int wait_waiting(IcoVerdicts *v, int n)
{
    const double t0 = ico_thread_now_ms();

    while (ico_vd_waiting(v) < n) {
        if (ico_thread_now_ms() - t0 > 10000.0) {
            return -1;
        }
    }
    return 0;
}

static void hold_until_in_work(void)
{
    ico_mutex_lock(g_mx);
    while (!g_in_work) {
        ico_cond_wait(g_cv, g_mx);
    }
    ico_mutex_unlock(g_mx);
}

static void let_go(void)
{
    ico_mutex_lock(g_mx);
    g_hold = 0;
    ico_cond_broadcast(g_cv);
    ico_mutex_unlock(g_mx);
}

/* until the worker is done (bounded: 10 s) */
static int wait_idle(IcoVerdicts *v)
{
    const double t0 = ico_thread_now_ms();

    while (ico_vd_running(v)) {
        if (ico_thread_now_ms() - t0 > 10000.0) {
            return -1;
        }
    }
    return 0;
}

static void take_fn(void *result, void *ctx)
{
    *(Res *)ctx = *(const Res *)result;
}

static void test_arrives(void)
{
    IcoVerdicts *v;
    Res r;
    unsigned g0;

    reset();
    v = make(0);
    CHECK(v != NULL);
    if (v == NULL) {
        return;
    }
    g0 = ico_vd_gen(v);
    CHECK(ico_vd_list(v, 1, &r) == 0); /* listed from its header: to come */
    CHECK(ico_vd_kick(v) == 1);
    CHECK(wait_idle(v) == 0);
    CHECK(ico_vd_gen(v) != g0); /* the listing's rebuild trigger moved */
    memset(&r, 0, sizeof r);
    CHECK(ico_vd_list(v, 1, &r) == 1 && r.value == 10);
    CHECK(g_computes[1] == 1 && g_computes[2] == 0);
    CHECK(ico_vd_kick(v) == 0); /* nothing queued */
    ico_vd_destroy(v);
    CHECK(g_released == 1);
}

typedef struct Asker {
    IcoVerdicts *v;
    int slot;
    int rc;
    Res r;
} Asker;

static void asker(void *arg)
{
    Asker *a = (Asker *)arg;

    a->rc = ico_vd_with(a->v, a->slot, take_fn, &a->r);
}

static void test_waits(void)
{
    IcoVerdicts *v;
    IcoThread *t;
    Asker a;
    Res r;

    reset();
    v = make(0);
    if (v == NULL) {
        CHECK(v != NULL);
        return;
    }
    g_hold = 1;
    CHECK(ico_vd_list(v, 2, &r) == 0);
    CHECK(ico_vd_kick(v) == 1);
    /* the worker inside the work, held open */
    ico_mutex_lock(g_mx);
    while (!g_in_work) {
        ico_cond_wait(g_cv, g_mx);
    }
    ico_mutex_unlock(g_mx);
    memset(&a, 0, sizeof a);
    a.v = v;
    a.slot = 2;
    a.rc = -9;
    t = ico_thread_create(asker, &a);
    CHECK(t != NULL);
    /* the load is seen waiting, then the work ends */
    CHECK(t == NULL || wait_waiting(v, 1) == 0, "the load never waited");
    let_go();
    if (t != NULL) {
        ico_thread_join(t);
    }
    CHECK(a.rc == 1, "the load took %d (1: the worker's result)", a.rc);
    CHECK(a.r.value == 20);
    CHECK(g_computes[2] == 1, "%d pieces of work (no second pass)", g_computes[2]);
    CHECK(wait_idle(v) == 0);
    ico_vd_destroy(v);
}

static void test_first(void)
{
    IcoVerdicts *v;
    IcoThread *t;
    Asker a;
    Res r;

    reset();
    v = make(0);
    if (v == NULL) {
        CHECK(v != NULL);
        return;
    }
    g_hold = 1;
    CHECK(ico_vd_list(v, 1, &r) == 0 && ico_vd_list(v, 2, &r) == 0 && ico_vd_list(v, 3, &r) == 0);
    CHECK(ico_vd_kick(v) == 1);
    hold_until_in_work(); /* the worker on slot 1 */
    memset(&a, 0, sizeof a);
    a.v = v;
    a.slot = 3;
    a.rc = -9;
    t = ico_thread_create(asker, &a);
    CHECK(t != NULL);
    CHECK(t == NULL || wait_waiting(v, 1) == 0, "the load never waited");
    let_go();
    if (t != NULL) {
        ico_thread_join(t);
    }
    CHECK(wait_idle(v) == 0);
    CHECK(a.rc == 0 && a.r.value == 30, "the load worked slot 3 out itself (%d, %d)", a.rc,
          a.r.value);
    CHECK(g_n_order == 3 && g_order[0] == 1 && g_order[1] == 3 && g_order[2] == 2,
          "the load's slot before the worker's next: %d %d %d (%d)", g_order[0], g_order[1],
          g_order[2], g_n_order);
    ico_vd_destroy(v);
}

static void test_cancel(void)
{
    IcoVerdicts *v;
    Res r;

    reset();
    v = make(0);
    if (v == NULL) {
        CHECK(v != NULL);
        return;
    }
    g_hold = 1;
    CHECK(ico_vd_list(v, 1, &r) == 0 && ico_vd_list(v, 2, &r) == 0 && ico_vd_list(v, 3, &r) == 0);
    CHECK(ico_vd_kick(v) == 1);
    hold_until_in_work();
    ico_vd_cancel(v);
    let_go();
    CHECK(wait_idle(v) == 0);
    CHECK(g_n_order == 1 && g_order[0] == 1, "after the cancel only the slot in work (%d)",
          g_n_order);
    CHECK(ico_vd_list(v, 2, &r) == 0 && ico_vd_kick(v) == 0, "a later kick starts nothing");
    ico_vd_destroy(v);
}

static void test_changed(void)
{
    IcoVerdicts *v;
    Res r;

    reset();
    v = make(0);
    if (v == NULL) {
        CHECK(v != NULL);
        return;
    }
    (void)ico_vd_list(v, 3, &r);
    (void)ico_vd_kick(v);
    CHECK(wait_idle(v) == 0);
    CHECK(ico_vd_list(v, 3, &r) == 1 && g_computes[3] == 1);
    /* saved again: another time */
    g_key[3].mtime++;
    g_value[3] = 33;
    CHECK(ico_vd_list(v, 3, &r) == 0); /* queued again */
    memset(&r, 0, sizeof r);
    CHECK(ico_vd_with(v, 3, take_fn, &r) == 0); /* worked out by the load */
    CHECK(r.value == 33 && g_computes[3] == 2);
    CHECK(g_released == 1, "the replaced verdict released (%d)", g_released);
    /* another file of the same size and second (its header's build): the same */
    g_key[3].file++;
    CHECK(ico_vd_list(v, 3, &r) == 0);
    CHECK(ico_vd_with(v, 3, take_fn, &r) == 0 && g_computes[3] == 3);
    /* the settings changed: the same */
    g_key[3].keys++;
    CHECK(ico_vd_list(v, 3, &r) == 0);
    /* the worker may take the queue later; the destroy joins it */
    (void)ico_vd_kick(v);
    CHECK(wait_idle(v) == 0);
    ico_vd_destroy(v);
}

static void test_nofile(void)
{
    IcoVerdicts *v;
    Res r;

    reset();
    v = make(0);
    if (v == NULL) {
        CHECK(v != NULL);
        return;
    }
    g_have[4] = 0;
    CHECK(ico_vd_list(v, 4, &r) == 0);
    CHECK(ico_vd_with(v, 4, take_fn, &r) == -1);
    CHECK(ico_vd_list(v, 0, &r) == 0 && ico_vd_list(v, 5, &r) == 0); /* out of range */
    ico_vd_destroy(v);
}

static void test_nothread(void)
{
    IcoVerdicts *v;
    Res r;

    reset();
    v = make(1);
    if (v == NULL) {
        CHECK(v != NULL);
        return;
    }
    CHECK(ico_vd_list(v, 1, &r) == 0 && ico_vd_list(v, 2, &r) == 0);
    CHECK(ico_vd_kick(v) == -1);
    CHECK(!ico_vd_running(v));
    CHECK(ico_vd_list(v, 1, &r) == 1 && r.value == 10);
    CHECK(ico_vd_list(v, 2, &r) == 1 && r.value == 20);
    CHECK(g_computes[1] == 1 && g_computes[2] == 1);
    ico_vd_destroy(v);
    CHECK(g_released == 2);
}

int main(void)
{
    IcoVdOps bad;

    g_mx = ico_mutex_create();
    g_cv = ico_cond_create();
    memset(&bad, 0, sizeof bad);
    CHECK(ico_vd_create(&bad) == NULL);
    test_arrives();
    test_waits();
    test_first();
    test_cancel();
    test_changed();
    test_nofile();
    test_nothread();
    ico_cond_destroy(g_cv);
    ico_mutex_destroy(g_mx);
    return ico_check_summary("state_verdicts");
}

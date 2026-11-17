/*
 * port/save/state_verdicts.c: the slots' verdicts on a worker, handed to a
 * load (state_verdicts.h).
 */
#include "state_verdicts.h"

#include <stdlib.h>
#include <string.h>

#include "ico_thread.h"

typedef struct VdSlot {
    int have;
    int queued;
    IcoVdKey key;
    unsigned char *res;
} VdSlot;

struct IcoVerdicts {
    IcoVdOps ops;
    IcoMutex *work, *tab;
    IcoCond *turn; /* with tab: the worker waits while a load wants the work lock */
    IcoThread *th;
    int running;
    int want; /* loads (and lock_work callers) waiting for the work lock */
    int stop; /* cancelled: the worker takes no further slot */
    unsigned gen;
    VdSlot slot[ICO_VD_SLOTS + 1];
};

static int same_key(const IcoVdKey *a, const IcoVdKey *b)
{
    return a->size == b->size && a->mtime == b->mtime && a->file == b->file && a->keys == b->keys;
}

IcoVerdicts *ico_vd_create(const IcoVdOps *ops)
{
    IcoVerdicts *v;
    int i;

    if (ops == NULL || ops->compute == NULL || ops->key_now == NULL || ops->slots < 1 ||
        ops->slots > ICO_VD_SLOTS || ops->result_bytes == 0) {
        return NULL;
    }
    v = (IcoVerdicts *)calloc(1, sizeof *v);
    if (v == NULL) {
        return NULL;
    }
    v->ops = *ops;
    v->work = ico_mutex_create();
    v->tab = ico_mutex_create();
    v->turn = ico_cond_create();
    for (i = 1; i <= ops->slots; i++) {
        v->slot[i].res = (unsigned char *)calloc(1, ops->result_bytes);
    }
    for (i = 1; i <= ops->slots; i++) {
        if (v->slot[i].res == NULL) {
            ico_vd_destroy(v);
            return NULL;
        }
    }
    if (v->work == NULL || v->tab == NULL || v->turn == NULL) {
        ico_vd_destroy(v);
        return NULL;
    }
    return v;
}

static void release(IcoVerdicts *v, VdSlot *s)
{
    if (s->have && v->ops.release != NULL) {
        v->ops.release(s->res, v->ops.user);
    }
    s->have = 0;
}

void ico_vd_cancel(IcoVerdicts *v)
{
    int i;

    if (v == NULL || v->tab == NULL) {
        return;
    }
    ico_mutex_lock(v->tab);
    v->stop = 1;
    for (i = 1; i <= v->ops.slots; i++) {
        v->slot[i].queued = 0;
    }
    if (v->turn != NULL) {
        ico_cond_broadcast(v->turn);
    }
    ico_mutex_unlock(v->tab);
}

void ico_vd_destroy(IcoVerdicts *v)
{
    int i;

    if (v == NULL) {
        return;
    }
    /* the queue dropped first: the worker ends after the slot it is on */
    ico_vd_cancel(v);
    if (v->th != NULL) {
        ico_thread_join(v->th);
    }
    for (i = 1; i <= v->ops.slots; i++) {
        if (v->slot[i].res != NULL) {
            release(v, &v->slot[i]);
            free(v->slot[i].res);
        }
    }
    if (v->work != NULL) {
        ico_mutex_destroy(v->work);
    }
    if (v->tab != NULL) {
        ico_mutex_destroy(v->tab);
    }
    if (v->turn != NULL) {
        ico_cond_destroy(v->turn);
    }
    free(v);
}

/* slot worked out into a fresh buffer and stored (the work lock held) */
static void work_out(IcoVerdicts *v, int slot)
{
    unsigned char *res = (unsigned char *)calloc(1, v->ops.result_bytes);
    IcoVdKey key;
    int rc;

    memset(&key, 0, sizeof key);
    rc = res != NULL ? v->ops.compute(slot, v->ops.user, res, &key) : -1;
    ico_mutex_lock(v->tab);
    release(v, &v->slot[slot]);
    v->slot[slot].queued = 0;
    if (rc == 0) {
        memcpy(v->slot[slot].res, res, v->ops.result_bytes);
        v->slot[slot].key = key;
        v->slot[slot].have = 1;
    }
    v->gen++;
    ico_mutex_unlock(v->tab);
    free(res);
}

static void worker(void *arg)
{
    IcoVerdicts *v = (IcoVerdicts *)arg;

    for (;;) {
        int slot = 0, i, still;

        /* a load waiting for the work lock goes first (a mutex hands over
           in no order: the worker re-taking it at once could keep a load
           waiting through every queued slot) */
        ico_mutex_lock(v->tab);
        while (v->want > 0 && !v->stop) {
            ico_cond_wait(v->turn, v->tab);
        }
        for (i = 1; i <= v->ops.slots && slot == 0 && !v->stop; i++) {
            if (v->slot[i].queued) {
                slot = i;
            }
        }
        if (slot == 0) {
            v->running = 0;
            v->gen++;
            ico_mutex_unlock(v->tab);
            return;
        }
        ico_mutex_unlock(v->tab);
        ico_mutex_lock(v->work);
        ico_mutex_lock(v->tab);
        still = v->slot[slot].queued && !v->stop; /* a load may have worked it out */
        ico_mutex_unlock(v->tab);
        if (still) {
            work_out(v, slot);
        }
        ico_mutex_unlock(v->work);
    }
}

int ico_vd_list(IcoVerdicts *v, int slot, void *out)
{
    IcoVdKey now;
    int fresh = 0;

    if (v == NULL || slot < 1 || slot > v->ops.slots) {
        return 0;
    }
    if (v->ops.key_now(slot, v->ops.user, &now) != 0) {
        memset(&now, 0xff, sizeof now); /* no file: nothing is fresh */
    }
    ico_mutex_lock(v->tab);
    if (v->slot[slot].have && same_key(&v->slot[slot].key, &now)) {
        memcpy(out, v->slot[slot].res, v->ops.result_bytes);
        fresh = 1;
    } else {
        v->slot[slot].queued = 1;
    }
    ico_mutex_unlock(v->tab);
    return fresh;
}

int ico_vd_kick(IcoVerdicts *v)
{
    int any = 0, i;

    if (v == NULL) {
        return 0;
    }
    ico_mutex_lock(v->tab);
    if (v->stop) {
        ico_mutex_unlock(v->tab);
        return 0;
    }
    if (v->running) {
        ico_mutex_unlock(v->tab);
        return 1;
    }
    for (i = 1; i <= v->ops.slots; i++) {
        any |= v->slot[i].queued;
    }
    if (any) {
        v->running = 1;
    }
    ico_mutex_unlock(v->tab);
    if (!any) {
        return 0;
    }
    if (v->th != NULL) {
        ico_thread_join(v->th); /* the last run, finished (running was 0) */
        v->th = NULL;
    }
    if (!v->ops.no_thread) {
        v->th = ico_thread_create(worker, v);
    }
    if (v->th == NULL) {
        worker(v); /* here: the listing as it was */
        return -1;
    }
    return 1;
}

unsigned ico_vd_gen(IcoVerdicts *v)
{
    unsigned g;

    if (v == NULL) {
        return 0;
    }
    ico_mutex_lock(v->tab);
    g = v->gen;
    ico_mutex_unlock(v->tab);
    return g;
}

int ico_vd_running(IcoVerdicts *v)
{
    int r;

    if (v == NULL) {
        return 0;
    }
    ico_mutex_lock(v->tab);
    r = v->running;
    ico_mutex_unlock(v->tab);
    return r;
}

/* the work lock for a load: the worker lets it in before its next slot */
static void lock_work_first(IcoVerdicts *v)
{
    ico_mutex_lock(v->tab);
    v->want++;
    ico_mutex_unlock(v->tab);
    ico_mutex_lock(v->work);
    ico_mutex_lock(v->tab);
    v->want--;
    ico_cond_broadcast(v->turn);
    ico_mutex_unlock(v->tab);
}

int ico_vd_waiting(IcoVerdicts *v)
{
    int n;

    if (v == NULL) {
        return 0;
    }
    ico_mutex_lock(v->tab);
    n = v->want;
    ico_mutex_unlock(v->tab);
    return n;
}

int ico_vd_with(IcoVerdicts *v, int slot, void (*fn)(void *result, void *ctx), void *ctx)
{
    IcoVdKey now;
    int rc = -1;

    if (v == NULL || slot < 1 || slot > v->ops.slots ||
        v->ops.key_now(slot, v->ops.user, &now) != 0) {
        return -1;
    }
    /* the work lock first: a worker on this slot finishes and stores it */
    lock_work_first(v);
    ico_mutex_lock(v->tab);
    if (v->slot[slot].have && same_key(&v->slot[slot].key, &now)) {
        fn(v->slot[slot].res, ctx);
        rc = 1;
    }
    ico_mutex_unlock(v->tab);
    if (rc < 0) {
        work_out(v, slot);
        ico_mutex_lock(v->tab);
        if (v->slot[slot].have) {
            fn(v->slot[slot].res, ctx);
            rc = 0;
        }
        ico_mutex_unlock(v->tab);
    }
    ico_mutex_unlock(v->work);
    return rc;
}

void ico_vd_lock_work(IcoVerdicts *v)
{
    if (v != NULL) {
        lock_work_first(v);
    }
}

void ico_vd_unlock_work(IcoVerdicts *v)
{
    if (v != NULL) {
        ico_mutex_unlock(v->work);
    }
}

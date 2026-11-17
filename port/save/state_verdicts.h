/*
 * port/save/state_verdicts.h
 *
 * The Save states slots' verdicts worked out on a worker thread and handed
 * to a load (port/game/state_host.c: what a slot of another build or
 * platform does at Load, the exact path's verdict and the checkpoint's).
 * Without the game: the work is a callback, the file's identity another.
 *
 * Each slot has at most one result, kept with the key it was worked out
 * for (the file's size and time, the settings' hash).  ico_vd_list (the
 * listing, the game thread) never waits: a fresh result is copied out, a
 * missing or stale one is queued and ico_vd_kick starts the worker on the
 * queue.  ico_vd_with (a load) waits while the worker is on that slot,
 * works the slot out itself when there is no fresh result, then calls the
 * caller's function on the result under the table's lock (it may move
 * things out of it).  Every piece of work runs under the work lock, the
 * worker's and the caller's alike (ico_vd_lock_work for other work that
 * shares the callback's statics); the table has its own lock, held only
 * for copies.  Without a thread (none could be made) the queue is worked
 * out inside ico_vd_kick, as the listing did before.
 */
#ifndef ICO_SAVE_STATE_VERDICTS_H
#define ICO_SAVE_STATE_VERDICTS_H

#include <stddef.h>
#include <stdint.h>

#define ICO_VD_SLOTS 8

typedef struct IcoVdKey {
    unsigned long long size;
    long long mtime;
    uint64_t file; /* the file's own identity (state_host.c: its header's build id and commit) */
    uint64_t keys; /* the settings it is judged under */
} IcoVdKey;

typedef struct IcoVdOps {
    /* works slot out into result (result_bytes, zeroed first); *key the key
       it was worked out for; 0, or -1 when there is nothing (no file) */
    int (*compute)(int slot, void *user, void *result, IcoVdKey *key);
    /* the slot's key now (a stat and the settings): 0, or -1 (no file) */
    int (*key_now)(int slot, void *user, IcoVdKey *key);
    /* frees what result owns (may be NULL) */
    void (*release)(void *result, void *user);
    void *user;
    size_t result_bytes;
    int slots;     /* 1..ICO_VD_SLOTS */
    int no_thread; /* tests: the fallback (worked out in ico_vd_kick) */
} IcoVdOps;

typedef struct IcoVerdicts IcoVerdicts;

IcoVerdicts *ico_vd_create(const IcoVdOps *ops);
/* Drops the queue and stops the worker from taking another slot (the one
   it is on finishes); a later ico_vd_kick starts nothing.  Never waits. */
void ico_vd_cancel(IcoVerdicts *v);
/* cancels (above), joins the worker (at most the slot it was on), frees
   every result */
void ico_vd_destroy(IcoVerdicts *v);

/* 1 with a copy of slot's fresh result in out (a shallow copy: what it
   points to stays the table's), 0 when it is to come (queued) */
int ico_vd_list(IcoVerdicts *v, int slot, void *out);
/* starts the worker on the queue (1 started or running, 0 nothing to do,
   -1 no thread: the queue worked out here) */
int ico_vd_kick(IcoVerdicts *v);
/* bumped by every result stored (the listing's rebuild trigger) */
unsigned ico_vd_gen(IcoVerdicts *v);
/* 1 while the worker runs */
int ico_vd_running(IcoVerdicts *v);

/* A load's turn: slot's fresh result (waiting for the worker, else worked
   out here), then fn(result, ctx) under the table's lock.  Returns 1 when
   it was the worker's (or an earlier) result, 0 when worked out here, -1
   when there is nothing (no file). */
int ico_vd_with(IcoVerdicts *v, int slot, void (*fn)(void *result, void *ctx), void *ctx);

/* how many callers wait for the work lock now (ico_vd_with,
   ico_vd_lock_work): the worker lets each in before its next slot (tests) */
int ico_vd_waiting(IcoVerdicts *v);
/* the work lock, for other work sharing the callback's statics */
void ico_vd_lock_work(IcoVerdicts *v);
void ico_vd_unlock_work(IcoVerdicts *v);

#endif /* ICO_SAVE_STATE_VERDICTS_H */

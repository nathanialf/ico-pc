/*
 * port/platform/frame_mailbox.c
 *
 * The game thread's and the render thread's hand-over (frame_mailbox.h).
 */
#include "frame_mailbox.h"

#include <string.h>

void fm_init(FrameMailbox *m, uint64_t nowNs)
{
    memset(m, 0, sizeof(*m));
    m->blockAt = nowNs;
    m->idleSince = nowNs;
}

bool fm_publish(FrameMailbox *m, const FmFrame *f)
{
    const bool replaced = m->published > m->taken;

    if (replaced) {
        m->replaced++;
    }
    m->latest = *f;
    m->published++;
    return replaced;
}

bool fm_take(FrameMailbox *m, FmFrame *out)
{
    if (m->taken == m->published) {
        return false;
    }
    *out = m->latest;
    m->taken = m->published;
    return true;
}

void fm_lock_set(FrameMailbox *m, const FmLock *l)
{
    m->lock = *l;
    m->lockSerial++;
}

bool fm_lock_get(const FrameMailbox *m, FmLock *out)
{
    if (m->lockSerial == 0) {
        return false;
    }
    *out = m->lock;
    return true;
}

void fm_pin(FrameMailbox *m, uint32_t slot)
{
    if (slot < FM_SLOTS) {
        m->pins[slot]++;
    }
}

void fm_unpin(FrameMailbox *m, uint32_t slot)
{
    if (slot < FM_SLOTS && m->pins[slot] > 0) {
        m->pins[slot]--;
    }
}

bool fm_pinned(const FrameMailbox *m, uint32_t slot)
{
    return slot < FM_SLOTS && m->pins[slot] > 0;
}

uint64_t fm_post(FrameMailbox *m, void (*fn)(void *arg), void *arg, FmJobKind kind)
{
    if (fn == NULL || m->quit || m->parkWanted || m->parked || m->count == FM_JOBS) {
        return 0;
    }
    FmJob *j = &m->jobs[(m->head + m->count) % FM_JOBS];

    j->fn = fn;
    j->arg = arg;
    j->kind = kind;
    j->seq = ++m->posted;
    m->count++;
    return j->seq;
}

FmNext fm_next(FrameMailbox *m, FmJob *out, uint64_t nowNs)
{
    if (m->count > 0) {
        /* queued jobs run first, park or quit asked or not (none is taken
           after either is asked: fm_post) */
        *out = m->jobs[m->head];
        m->head = (m->head + 1) % FM_JOBS;
        m->count--;
        m->started = out->seq;
        if (m->idleSince != 0 && nowNs > m->idleSince) {
            const uint64_t wait = nowNs - m->idleSince;
            const uint64_t from = m->idleSince > m->blockAt ? m->idleSince : m->blockAt;

            m->waitMaxNs = wait > m->waitMaxNs ? wait : m->waitMaxNs;
            m->idleNs += nowNs > from ? nowNs - from : 0;
        }
        m->idleSince = 0;
        m->busySince = nowNs != 0 ? nowNs : 1;
        return FM_NEXT_JOB;
    }
    if (m->quit) {
        return FM_NEXT_QUIT;
    }
    if (m->parkWanted) {
        m->parked = true;
        return FM_NEXT_PARKED;
    }
    return FM_NEXT_WAIT;
}

void fm_finish(FrameMailbox *m, const FmJob *job, uint64_t nowNs)
{
    if (m->busySince != 0 && nowNs > m->busySince) {
        const uint64_t busy = nowNs - m->busySince;

        m->busyMaxNs = busy > m->busyMaxNs ? busy : m->busyMaxNs;
    }
    m->done = job->seq;
    m->busySince = 0;
    m->lastDoneNs = nowNs;
    m->idleSince = nowNs != 0 ? nowNs : 1;
    m->blockJobs++;
    if (job->kind == FM_JOB_PICTURE) {
        m->blockPictures++;
    }
}

bool fm_idle(const FrameMailbox *m)
{
    return m->count == 0 && m->done == m->posted;
}

bool fm_done_up_to(const FrameMailbox *m, uint64_t seq)
{
    return m->done >= seq;
}

void fm_park(FrameMailbox *m)
{
    m->parkWanted = true;
}

bool fm_is_parked(const FrameMailbox *m)
{
    return m->parked;
}

void fm_unpark(FrameMailbox *m)
{
    m->parkWanted = false;
    m->parked = false;
}

void fm_quit(FrameMailbox *m)
{
    m->quit = true;
}

void fm_present_note(FrameMailbox *m, const FmPresent *p)
{
    m->lastPresent = *p;
    m->presents++;
}

bool fm_last_present(const FrameMailbox *m, FmPresent *out)
{
    if (m->presents == 0) {
        return false;
    }
    *out = m->lastPresent;
    return true;
}

uint64_t fm_heartbeat(const FrameMailbox *m, uint64_t nowNs, uint64_t *seq, uint64_t *done)
{
    *seq = m->busySince != 0 ? m->started : 0;
    *done = m->done;
    return m->busySince != 0 && nowNs > m->busySince ? nowNs - m->busySince : 0;
}

void fm_block_take(FrameMailbox *m, uint64_t nowNs, FmBlock *out)
{
    const uint64_t span = nowNs > m->blockAt ? nowNs - m->blockAt : 0;
    uint64_t idle = m->idleNs, busyMax = m->busyMaxNs;

    if (m->idleSince != 0 && nowNs > m->idleSince) {
        /* waiting now: its part in this block */
        const uint64_t from = m->idleSince > m->blockAt ? m->idleSince : m->blockAt;

        idle += nowNs > from ? nowNs - from : 0;
    }
    if (m->busySince != 0 && nowNs > m->busySince && nowNs - m->busySince > busyMax) {
        busyMax = nowNs - m->busySince; /* the job in progress, so far */
    }
    memset(out, 0, sizeof(*out));
    out->idlePercent = span > 0 ? 100.0 * (double)(idle < span ? idle : span) / (double)span : 0.0;
    out->waitMaxMs = (double)m->waitMaxNs / 1e6;
    out->busyMaxMs = (double)busyMax / 1e6;
    out->jobs = m->blockJobs;
    out->pictures = m->blockPictures;
    out->replaced = (unsigned)(m->replaced - m->replacedAt);
    m->blockAt = nowNs;
    m->idleNs = 0;
    m->waitMaxNs = 0;
    m->busyMaxNs = 0;
    m->blockJobs = m->blockPictures = 0;
    m->replacedAt = m->replaced;
}

static bool word_is(const char *v, const char *const *words)
{
    for (; *words != NULL; words++) {
        if (strcmp(v, *words) == 0) {
            return true;
        }
    }
    return false;
}

FmMode fm_mode_choose(const char *value, bool singleThread, bool window, const char **why)
{
    static const char *const on[] = {"on", "true", "1", "yes", NULL};
    static const char *const off[] = {"off", "false", "0", "no", NULL};
    if (!window) {
        *why = "no window";
        return FM_MODE_OFF;
    }
    if (value != NULL && word_is(value, off)) {
        *why = "[dev] render_thread = off";
        return FM_MODE_OFF;
    }
    /* OpenGL and Direct3D 11 take every call on one thread: from the
       render thread's start that thread is the render thread, the game
       thread's own calls taking the context over (render_thread.c) */
    if (value != NULL && strcmp(value, "sync") == 0) {
        *why = singleThread ? "[dev] render_thread = sync: the game thread waits for each picture; "
                              "every graphics call on the render thread"
                            : "[dev] render_thread = sync: the game thread waits for each picture";
        return FM_MODE_SYNC;
    }
    if (value != NULL && word_is(value, on)) {
        *why = singleThread ? "[dev] render_thread = on; every graphics call on the render thread"
                            : "[dev] render_thread = on";
        return FM_MODE_ON;
    }
    if (value == NULL || value[0] == '\0' || strcmp(value, "auto") == 0) {
        *why = singleThread ? "[dev] render_thread = auto; every graphics call on the render thread"
                            : "[dev] render_thread = auto";
        return FM_MODE_ON;
    }
    *why = "[dev] render_thread is not auto, on, off or sync: taken as auto";
    return FM_MODE_ON;
}

const char *fm_mode_name(FmMode mode)
{
    switch (mode) {
    case FM_MODE_ON:
        return "on";
    case FM_MODE_SYNC:
        return "sync";
    default:
        return "off";
    }
}

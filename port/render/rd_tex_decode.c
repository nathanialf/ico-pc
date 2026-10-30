/* rd_tex_decode.c: the texture decode pool (rd_tex_decode.h). */
#include "rd_tex_decode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ico_thread.h"
#include "rd_internal.h"

/* a settle that waited longer than this for the decodes is logged */
#define RDTEX_WAIT_LOG_MS 20.0

static struct {
    int threads;    /* the workers asked for (0: off) */
    int override;   /* rdtex_set_decode_threads: -2 none, -1 auto, 0.. */
    IcoMutex *lock; /* the list's states, the cursor, stop, undone */
    IcoCond *work;  /* a job was queued, or stop */
    IcoCond *done;  /* a worker finished a job */
    IcoThread *worker[RDTEX_DECODE_THREADS_MAX];
    int running; /* workers started */
    int stop;
    RdTexJob *head, *tail; /* the settle order (the game thread's) */
    RdTexJob *cursor;      /* the first job that may still be queued */
    uint32_t pending;      /* jobs in the list */
    uint32_t undone;       /* queued or running */
    uint32_t sinceSettle;  /* queued since the last settle */
    uint32_t waitsOver;    /* settles that waited over RDTEX_WAIT_LOG_MS */
    char logged[96];       /* the last start line (written again only when it changes) */
} s_dp = {.override = -2};

static int autoThreads(int *cpus)
{
    *cpus = ico_thread_cpu_count();
    return *cpus <= 2 ? 1 : 2;
}

static void startLine(const char *line)
{
    if (strcmp(line, s_dp.logged) != 0) {
        snprintf(s_dp.logged, sizeof(s_dp.logged), "%s", line);
        rd__log("%s", line);
    }
}

static void stopWorkers(void)
{
    if (s_dp.running == 0) {
        return;
    }
    ico_mutex_lock(s_dp.lock);
    s_dp.stop = 1;
    ico_cond_broadcast(s_dp.work);
    ico_mutex_unlock(s_dp.lock);
    for (int i = 0; i < s_dp.running; i++) {
        ico_thread_join(s_dp.worker[i]);
        s_dp.worker[i] = NULL;
    }
    s_dp.running = 0;
    s_dp.stop = 0;
}

/* the size for this context, written once a change */
static void choose(int windowed)
{
    char line[96];
    int cpus = 0;
    int n;
    const char *env = getenv("ICO_RD_TEXTURE_DECODE_THREADS");

    if (s_dp.override >= -1) {
        n = s_dp.override >= 0 ? s_dp.override : autoThreads(&cpus);
        snprintf(line, sizeof(line), "texture decodes: %d worker threads (set by the program)", n);
    } else if (env && env[0] && strcmp(env, "auto") != 0) {
        const long v = strtol(env, NULL, 10);
        n = v < 0 ? 0 : v > RDTEX_DECODE_THREADS_MAX ? RDTEX_DECODE_THREADS_MAX : (int)v;
        if (n == 0) {
            snprintf(line, sizeof(line),
                     "texture decodes: on the game thread ([dev] texture_decode_threads = 0)");
        } else {
            snprintf(line, sizeof(line),
                     "texture decodes: %d worker threads ([dev] texture_decode_threads)", n);
        }
    } else if (windowed || (env && strcmp(env, "auto") == 0)) {
        n = autoThreads(&cpus);
        snprintf(line, sizeof(line), "texture decodes: %d worker threads (%d hardware threads)", n,
                 cpus);
    } else {
        n = 0;
        snprintf(line, sizeof(line), "texture decodes: on the game thread (no window)");
    }
    if (n != s_dp.threads) {
        rdtex_settle();
        stopWorkers();
        s_dp.threads = n;
    }
    startLine(line);
}

void rdtex__decode_init(int windowed)
{
    choose(windowed);
}

void rdtex_set_decode_threads(int n)
{
    s_dp.override = n < -1 ? -1 : n > RDTEX_DECODE_THREADS_MAX ? RDTEX_DECODE_THREADS_MAX : n;
    choose(0);
}

int rdtex_decode_threads(void)
{
    return s_dp.threads;
}

int rdtex__decode_threads(void)
{
    return s_dp.threads;
}

uint32_t rdtex__decode_pending(void)
{
    return s_dp.pending;
}

/* the first queued job from the cursor on, taken (lock held) */
static RdTexJob *take(void)
{
    while (s_dp.cursor && s_dp.cursor->state != RDTEX_JOB_QUEUED) {
        s_dp.cursor = s_dp.cursor->next;
    }
    RdTexJob *j = s_dp.cursor;
    if (j) {
        j->state = RDTEX_JOB_RUNNING;
        s_dp.cursor = j->next;
    }
    return j;
}

static void workerMain(void *arg)
{
    (void)arg;
    ico_mutex_lock(s_dp.lock);
    for (;;) {
        RdTexJob *j = NULL;
        while (!s_dp.stop && (j = take()) == NULL) {
            ico_cond_wait(s_dp.work, s_dp.lock);
        }
        if (j == NULL) {
            break; /* stop */
        }
        ico_mutex_unlock(s_dp.lock);
        rdtex__job_run(j);
        ico_mutex_lock(s_dp.lock);
        j->state = RDTEX_JOB_DONE;
        s_dp.undone--;
        ico_cond_broadcast(s_dp.done);
    }
    ico_mutex_unlock(s_dp.lock);
}

/* the lock, the conditions and the workers, the first time a job is
   queued; 0 when not even the lock could be made (the job then runs at
   once).  A worker the system would not start leaves the jobs to the
   others and to rdtex_settle, which decodes what nobody took. */
static int ensurePool(void)
{
    if (s_dp.lock == NULL) {
        s_dp.lock = ico_mutex_create();
        s_dp.work = ico_cond_create();
        s_dp.done = ico_cond_create();
        if (!s_dp.lock || !s_dp.work || !s_dp.done) {
            ico_cond_destroy(s_dp.work);
            ico_cond_destroy(s_dp.done);
            ico_mutex_destroy(s_dp.lock);
            s_dp.lock = NULL;
            s_dp.work = s_dp.done = NULL;
            return 0;
        }
    }
    while (s_dp.running < s_dp.threads) {
        IcoThread *t = ico_thread_create(workerMain, NULL);
        if (t == NULL) {
            static int s_failLogged;
            if (!s_failLogged) {
                s_failLogged = 1;
                rd__log("texture decodes: worker thread %d of %d could not start; the game "
                        "thread decodes what the others do not",
                        s_dp.running + 1, s_dp.threads);
            }
            break;
        }
        s_dp.worker[s_dp.running++] = t;
    }
    return 1;
}

void rdtex__decode_add(RdTexJob *j, int queued)
{
    j->next = NULL;
    j->state = queued ? RDTEX_JOB_QUEUED : RDTEX_JOB_DONE;
    if (queued && !ensurePool()) {
        rdtex__job_run(j);
        j->state = RDTEX_JOB_DONE;
        queued = 0;
    }
    if (s_dp.lock) {
        ico_mutex_lock(s_dp.lock);
    }
    if (s_dp.tail) {
        s_dp.tail->next = j;
    } else {
        s_dp.head = j;
    }
    s_dp.tail = j;
    s_dp.pending++;
    if (queued) {
        if (s_dp.cursor == NULL) {
            s_dp.cursor = j;
        }
        s_dp.undone++;
        s_dp.sinceSettle++;
        ico_cond_broadcast(s_dp.work);
    }
    if (s_dp.lock) {
        ico_mutex_unlock(s_dp.lock);
    }
}

void rdtex_settle(void)
{
    if (s_dp.head == NULL) {
        return;
    }
    const char *last = NULL;
    double waited = 0.0;
    if (s_dp.lock) {
        ico_mutex_lock(s_dp.lock);
        if (s_dp.undone) {
            const double t0 = ico_thread_now_ms();
            /* the jobs no worker took yet: decoded here rather than waited for */
            RdTexJob *j;
            while ((j = take()) != NULL) {
                ico_mutex_unlock(s_dp.lock);
                rdtex__job_run(j);
                ico_mutex_lock(s_dp.lock);
                j->state = RDTEX_JOB_DONE;
                s_dp.undone--;
                last = j->name;
            }
            /* and the ones a worker is running */
            for (RdTexJob *k = s_dp.head; k; k = k->next) {
                while (k->state != RDTEX_JOB_DONE) {
                    last = k->name;
                    ico_cond_wait(s_dp.done, s_dp.lock);
                }
            }
            waited = ico_thread_now_ms() - t0;
        }
        s_dp.cursor = NULL;
        ico_mutex_unlock(s_dp.lock);
    }
    /* every job done: the pixels into their textures in the stores' order */
    char name[32] = "";
    if (last) {
        snprintf(name, sizeof(name), "%s", last);
    }
    const uint32_t decoded = s_dp.sinceSettle;
    RdTexJob *j = s_dp.head;
    s_dp.head = s_dp.tail = NULL;
    s_dp.pending = 0;
    s_dp.sinceSettle = 0;
    while (j) {
        RdTexJob *next = j->next;
        rdtex__job_finish(j);
        j = next;
    }
    if (waited > RDTEX_WAIT_LOG_MS) {
        s_dp.waitsOver++;
        if (s_dp.waitsOver <= 8 || s_dp.waitsOver % 100 == 0) {
            rd__log("waited %.1f ms for texture decodes (%u on %d worker threads since the last "
                    "replay, the last \"%s\"; %u waits over %.0f ms so far)",
                    waited, decoded, s_dp.threads, name, s_dp.waitsOver, RDTEX_WAIT_LOG_MS);
        }
    }
}

void rdtex__decode_shutdown(void)
{
    rdtex_settle();
    stopWorkers();
}

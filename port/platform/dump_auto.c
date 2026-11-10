/*
 * port/platform/dump_auto.c
 *
 * The automatic dumps' worker (dump_auto.h).
 */
#include "dump_auto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dump_zip.h"
#include "host_fs.h"
#include "ico_thread.h"

#define RECORD_MAX 16

typedef struct Auto {
    IcoMutex *mu;
    IcoCond *cv;
    IcoThread *thread;
    int started, quit;
    int hasJob;  /* a job waits or is being written */
    int hasDone; /* a result waits for the game thread */
    int gate;
    IcoDumpJob job;
    IcoDumpDone done;
    unsigned dropped;
    void (*flush)(void);
    char recorded[RECORD_MAX][ICO_PATH_MAX];
    int nrecorded;
} Auto;

static Auto g;

static void record_locked(const char *zip)
{
    if (g.nrecorded == RECORD_MAX) {
        memmove(g.recorded[0], g.recorded[1], sizeof(g.recorded[0]) * (RECORD_MAX - 1));
        g.nrecorded--;
    }
    snprintf(g.recorded[g.nrecorded++], sizeof(g.recorded[0]), "%s", zip);
}

void ico_dump_auto_record(const char *zip)
{
    if (g.mu == NULL) {
        g.mu = ico_mutex_create();
        g.cv = ico_cond_create();
    }
    ico_mutex_lock(g.mu);
    record_locked(zip);
    ico_mutex_unlock(g.mu);
}

void ico_dump_auto_forget(void)
{
    if (g.mu == NULL) {
        return;
    }
    ico_mutex_lock(g.mu);
    g.nrecorded = 0;
    ico_mutex_unlock(g.mu);
}

/* the job, on the worker thread, no lock held */
static void write_job(IcoDumpJob *j, IcoDumpDone *d)
{
    const double t0 = ico_thread_now_ms();
    IcoZipMem e[2];
    int n = 0;

    memset(d, 0, sizeof(*d));
    snprintf(d->tag, sizeof(d->tag), "%s", j->tag);
    snprintf(d->zip, sizeof(d->zip), "%s", j->zip);
    snprintf(d->text, sizeof(d->text), "%s", j->text);
    d->gameMs = j->gameMs;
    d->bytes = j->dumpLen;
    e[n].name = j->dumpName;
    e[n].data = j->dump;
    e[n++].size = j->dumpLen;
    if (j->prev != NULL && j->prevLen > 0) {
        e[n].name = j->prevName;
        e[n].data = j->prev;
        e[n++].size = j->prevLen;
    }
    /* the entries carry their file names only, as the loose files' did */
    for (int i = 0; i < n; i++) {
        const char *b = e[i].name + strlen(e[i].name);

        while (b > e[i].name && b[-1] != '/' && b[-1] != '\\') {
            b--;
        }
        e[i].name = b;
    }
    if (g.flush != NULL) {
        g.flush();
    }
    d->result = ico_zip_mem_files(j->zip, e, n, j->logPath, j->stored, d->why, sizeof(d->why));
    if (d->result < 0) {
        /* the frame is kept loose, as F12's is: the capture is not lost */
        FILE *f = ico_fopen(j->dumpName, "wb");
        const int kept = f != NULL && fwrite(j->dump, 1, j->dumpLen, f) == j->dumpLen;

        if (f != NULL) {
            fclose(f);
        }
        if (!kept) {
            ico_remove(j->dumpName);
        }
    } else {
        if (j->sidecar[0] != '\0') {
            char swhy[160] = "";
            const char *b = j->sidecar + strlen(j->sidecar);

            while (b > j->sidecar && b[-1] != '/' && b[-1] != '\\') {
                b--;
            }
            if (ico_zip_add_file(j->zip, j->sidecar, b, swhy, sizeof(swhy), NULL) != 0) {
                size_t u = strlen(d->why);
                snprintf(d->why + u, sizeof(d->why) - u, "the sidecar is not in the zip: %s", swhy);
            }
        }
        if (j->pruneSlow) {
            char dir[ICO_PATH_MAX];
            const char *z = j->zip + strlen(j->zip);

            while (z > j->zip && z[-1] != '/' && z[-1] != '\\') {
                z--;
            }
            snprintf(dir, sizeof(dir), "%.*s", (int)(z - j->zip), j->zip);
            (void)ico_slow_dump_prune(dir, ICO_SLOW_DUMP_KEEP);
        }
    }
    d->workerMs = ico_thread_now_ms() - t0;
}

static void worker_main(void *arg)
{
    (void)arg;
    ico_mutex_lock(g.mu);
    for (;;) {
        while (!g.quit && !(g.hasJob && !g.hasDone)) {
            ico_cond_wait(g.cv, g.mu);
        }
        if (!g.hasJob) {
            break; /* quit with nothing to write */
        }
        while (g.gate) {
            ico_cond_wait(g.cv, g.mu);
        }
        ico_mutex_unlock(g.mu);
        IcoDumpDone d;
        write_job(&g.job, &d);
        free(g.job.dump);
        free(g.job.prev);
        g.job.dump = g.job.prev = NULL;
        ico_mutex_lock(g.mu);
        g.done = d;
        g.hasDone = 1;
        if (d.result >= 0) {
            record_locked(d.zip);
        }
        g.hasJob = 0;
        if (g.quit) {
            break;
        }
    }
    ico_mutex_unlock(g.mu);
}

int ico_dump_auto_busy(void)
{
    int busy;

    if (g.mu == NULL) {
        return 0;
    }
    ico_mutex_lock(g.mu);
    busy = g.hasJob || g.hasDone;
    ico_mutex_unlock(g.mu);
    return busy;
}

static void drop_line(const char *tag, const char *why)
{
    const unsigned n = ++g.dropped;

    if (n <= 8u || n % 100u == 0u) {
        fprintf(stderr,
                "window: %s: dropped, %s (%u dropped so far; one dump is written at a time)\n", tag,
                why, n);
    }
}

int ico_dump_auto_submit(const IcoDumpJob *job)
{
    if (g.mu == NULL) {
        g.mu = ico_mutex_create();
        g.cv = ico_cond_create();
    }
    ico_mutex_lock(g.mu);
    if (g.hasJob || g.hasDone || g.quit) {
        drop_line(job->tag, "the worker is still writing the one before");
        ico_mutex_unlock(g.mu);
        return -1;
    }
    if (!g.started) {
        g.thread = ico_thread_create(worker_main, NULL);
        if (g.thread == NULL) {
            drop_line(job->tag, "no thread could be made");
            ico_mutex_unlock(g.mu);
            return -2;
        }
        g.started = 1;
    }
    g.job = *job;
    g.hasJob = 1;
    ico_cond_broadcast(g.cv);
    ico_mutex_unlock(g.mu);
    return 0;
}

int ico_dump_auto_poll(IcoDumpDone *out)
{
    int r = 0;

    if (g.mu == NULL) {
        return 0;
    }
    ico_mutex_lock(g.mu);
    if (g.hasDone) {
        *out = g.done;
        g.hasDone = 0;
        r = 1;
        ico_cond_broadcast(g.cv);
    }
    ico_mutex_unlock(g.mu);
    return r;
}

void ico_dump_auto_shutdown(void)
{
    IcoThread *t;

    if (g.mu == NULL) {
        return;
    }
    ico_mutex_lock(g.mu);
    g.gate = 0;
    g.quit = 1;
    t = g.started ? g.thread : NULL;
    g.thread = NULL;
    g.started = 0;
    ico_cond_broadcast(g.cv);
    ico_mutex_unlock(g.mu);
    if (t != NULL) {
        ico_thread_join(t);
    }
    ico_mutex_lock(g.mu);
    g.quit = 0; /* a later start (the tests) may begin again */
    ico_mutex_unlock(g.mu);
}

void ico_dump_auto_set_flush(void (*flush)(void))
{
    g.flush = flush;
}

unsigned ico_dump_auto_dropped(void)
{
    return g.dropped;
}

void ico_dump_auto_test_gate(int closed)
{
    if (g.mu == NULL) {
        g.mu = ico_mutex_create();
        g.cv = ico_cond_create();
    }
    ico_mutex_lock(g.mu);
    g.gate = closed;
    ico_cond_broadcast(g.cv);
    ico_mutex_unlock(g.mu);
}

/* ------------------------------------------------------------ the bundle */

typedef struct Cand {
    char path[ICO_PATH_MAX];
    unsigned long long size;
    long long mtime;
} Cand;

static const char *base_of(const char *p)
{
    const char *b = p + strlen(p);

    while (b > p && b[-1] != '/' && b[-1] != '\\') {
        b--;
    }
    return b;
}

int ico_dump_auto_bundle(const char *reportZip, char *line, size_t lineSize)
{
    Cand c[RECORD_MAX];
    int nc = 0;
    char note[2048] = "";
    size_t nu = 0;
    const char *add[ICO_BUNDLE_MAX_FILES];
    int na = 0;
    unsigned long long total = 0;
    char why[160] = "";

    if (g.mu == NULL) {
        snprintf(line, lineSize, "no automatic capture was made this run");
        return 0;
    }
    ico_mutex_lock(g.mu);
    for (int i = 0; i < g.nrecorded; i++) {
        unsigned long long sz = 0;
        long long mt = 0;

        if (ico_path_kind(g.recorded[i], &sz, &mt) == 0) {
            snprintf(c[nc].path, sizeof(c[nc].path), "%s", g.recorded[i]);
            c[nc].size = sz;
            c[nc].mtime = mt;
            nc++;
        }
    }
    const int inFlight = g.hasJob;
    ico_mutex_unlock(g.mu);
    if (nc == 0) {
        snprintf(line, lineSize, "no automatic capture of this run is still on disk%s",
                 inFlight ? " (one is still being written)" : "");
        return 0;
    }
    /* newest first: the recorded order is the order written */
    nu +=
        (size_t)snprintf(note + nu, sizeof(note) - nu,
                         "The automatic captures of this run still on disk, newest first (at most "
                         "%d, %u bytes together):\n",
                         ICO_BUNDLE_MAX_FILES, (unsigned)ICO_BUNDLE_MAX_BYTES);
    for (int i = nc - 1; i >= 0; i--) {
        const char *why1 = NULL;

        if (na >= ICO_BUNDLE_MAX_FILES) {
            why1 = "left out: only the newest three go in";
        } else if (c[i].size == 0 || total + c[i].size > ICO_BUNDLE_MAX_BYTES) {
            why1 = c[i].size == 0 ? "left out: empty" : "left out: over the size limit";
        }
        if (nu < sizeof(note)) {
            nu += (size_t)snprintf(note + nu, sizeof(note) - nu, "%s: %llu bytes, %s\n",
                                   base_of(c[i].path), c[i].size, why1 != NULL ? why1 : "included");
        }
        if (why1 == NULL) {
            add[na++] = c[i].path;
            total += c[i].size;
        }
    }
    if (inFlight && nu < sizeof(note)) {
        snprintf(note + nu, sizeof(note) - nu, "one more capture was still being written\n");
    }
    if (na == 0 || ico_zip_add_bundle(reportZip, add, na, "automatic-captures.txt", note, why,
                                      sizeof(why)) != 0) {
        snprintf(line, lineSize, "%d on disk, none added to the report: %s", nc,
                 na == 0 ? "all over the limits" : why);
        return 0;
    }
    snprintf(line, lineSize,
             "%d of %d automatic captures added (%llu bytes; the rest named in "
             "automatic-captures.txt)",
             na, nc, total);
    return na;
}

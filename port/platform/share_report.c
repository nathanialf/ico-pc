/*
 * port/platform/share_report.c
 *
 * The rc builds' share menu's files (share_report.h).
 */
#include "share_report.h"

#include <stdio.h>
#include <string.h>

#include "host_fs.h"

/* the newest file of a folder whose name has the prefix and the suffix */
typedef struct Newest {
    const char *prefix, *suffix;
    char path[ICO_SHARE_REPORT_PATH];
    long long mtime;
    int found;
} Newest;

static int ends_with(const char *s, const char *suffix)
{
    const size_t n = strlen(s), k = strlen(suffix);
    return n >= k && strcmp(s + n - k, suffix) == 0;
}

static const char *base_name(const char *path)
{
    const char *b = path + strlen(path);
    while (b > path && b[-1] != '/' && b[-1] != '\\') {
        b--;
    }
    return b;
}

static int newest_visit(const char *path, const char *name, void *user)
{
    Newest *n = (Newest *)user;
    long long mtime = 0;

    if (strncmp(name, n->prefix, strlen(n->prefix)) != 0 || !ends_with(name, n->suffix) ||
        strlen(path) >= sizeof(n->path) || ico_path_kind(path, NULL, &mtime) != 0) {
        return 0;
    }
    if (!n->found || mtime > n->mtime ||
        (mtime == n->mtime && strcmp(name, base_name(n->path)) > 0)) {
        snprintf(n->path, sizeof(n->path), "%s", path);
        n->mtime = mtime;
        n->found = 1;
    }
    return 0;
}

static void add_path(IcoShareReport *out, const char *path)
{
    if (out->count < ICO_SHARE_REPORT_MAX && strlen(path) < ICO_SHARE_REPORT_PATH &&
        ico_path_kind(path, NULL, NULL) == 0) {
        snprintf(out->path[out->count++], ICO_SHARE_REPORT_PATH, "%s", path);
    }
}

static void add_named(IcoShareReport *out, const char *dir, const char *name)
{
    char path[ICO_SHARE_REPORT_PATH];
    const int n = snprintf(path, sizeof(path), "%s/%s", dir, name);

    if (n > 0 && (size_t)n < sizeof(path)) {
        add_path(out, path);
    }
}

static void add_newest(IcoShareReport *out, const char *dir, const char *prefix, const char *suffix)
{
    Newest n;

    memset(&n, 0, sizeof(n));
    n.prefix = prefix;
    n.suffix = suffix;
    if (ico_dir_walk(dir, 0, newest_visit, &n) >= 0 && n.found) {
        add_path(out, n.path);
    }
}

/* this run's zips: the newest ICO_SHARE_REPORT_RUN_ZIPS modified at the
   run's start or later, kept newest first */
typedef struct RunZips {
    long long since;
    int n;
    char path[ICO_SHARE_REPORT_RUN_ZIPS][ICO_SHARE_REPORT_PATH];
    long long mtime[ICO_SHARE_REPORT_RUN_ZIPS];
} RunZips;

/* 1 when (ta, a) is newer than (tb, b): the later time, then the greater name */
static int newer(long long ta, const char *a, long long tb, const char *b)
{
    return ta > tb || (ta == tb && strcmp(base_name(a), base_name(b)) > 0);
}

static int run_visit(const char *path, const char *name, void *user)
{
    RunZips *z = (RunZips *)user;
    long long mtime = 0;

    if (!ends_with(name, ".zip") || strlen(path) >= ICO_SHARE_REPORT_PATH ||
        ico_path_kind(path, NULL, &mtime) != 0 || mtime < z->since) {
        return 0;
    }
    int at = z->n;
    while (at > 0 && newer(mtime, path, z->mtime[at - 1], z->path[at - 1])) {
        at--;
    }
    if (at >= ICO_SHARE_REPORT_RUN_ZIPS) {
        return 0; /* older than the ones kept */
    }
    const int last = z->n < ICO_SHARE_REPORT_RUN_ZIPS ? z->n : ICO_SHARE_REPORT_RUN_ZIPS - 1;
    for (int i = last; i > at; i--) {
        memcpy(z->path[i], z->path[i - 1], sizeof(z->path[i]));
        z->mtime[i] = z->mtime[i - 1];
    }
    snprintf(z->path[at], sizeof(z->path[at]), "%s", path);
    z->mtime[at] = mtime;
    if (z->n < ICO_SHARE_REPORT_RUN_ZIPS) {
        z->n++;
    }
    return 0;
}

/* this run's zips, oldest first; 0 when the run has none */
static int add_run_zips(IcoShareReport *out, const char *dir, long long since)
{
    RunZips z;

    memset(&z, 0, sizeof(z));
    z.since = since;
    if (ico_dir_walk(dir, 0, run_visit, &z) < 0) {
        return 0;
    }
    for (int i = z.n - 1; i >= 0; i--) {
        add_path(out, z.path[i]);
    }
    return z.n;
}

int ico_share_report_collect(const char *logs_dir, const char *dumps_dir, IcoShareReport *out)
{
    return ico_share_report_collect_run(logs_dir, dumps_dir, -1, out);
}

int ico_share_report_collect_run(const char *logs_dir, const char *dumps_dir, long long run_start,
                                 IcoShareReport *out)
{
    memset(out, 0, sizeof(*out));
    const int logs = logs_dir != NULL && logs_dir[0] != '\0';
    const int dumps = dumps_dir != NULL && dumps_dir[0] != '\0';

    if (logs) {
        add_named(out, logs_dir, "ico-pc.log");
        add_named(out, logs_dir, "ico-pc-previous.log");
    }
    if (dumps) {
        if (run_start < 0 || add_run_zips(out, dumps_dir, run_start) == 0) {
            add_newest(out, dumps_dir, "", ".zip");
        }
        add_newest(out, dumps_dir, "", "-present.png");
    }
    if (logs) {
        add_newest(out, logs_dir, "report-", ".zip");
        /* the skin check's capture on failure (rd.h rd_vu_skin_capture_due) */
        add_newest(out, logs_dir, "vu-skin-fail-", ".bin");
        /* the slow-scene dump (window_host.c slow_scene_dump, issue 179) */
        add_newest(out, logs_dir, "slow-", ".zip");
        /* the rc bench's tables (window_bench.h) */
        add_newest(out, logs_dir, "bench-", ".csv");
    }
    return out->count;
}

void ico_share_report_names(const IcoShareReport *r, char *out, size_t size)
{
    size_t n = 0;

    if (size == 0) {
        return;
    }
    out[0] = '\0';
    if (r->count == 0) {
        snprintf(out, size, "none");
        return;
    }
    for (int i = 0; i < r->count && n < size; i++) {
        const int k = snprintf(out + n, size - n, "%s%s", i ? ", " : "", base_name(r->path[i]));
        if (k < 0) {
            break;
        }
        n += (size_t)k;
    }
}

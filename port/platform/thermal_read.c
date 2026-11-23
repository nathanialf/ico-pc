/*
 * port/platform/thermal_read.c
 *
 * The phone's heat and clock state (thermal_read.h).  POSIX file reads
 * only (no allocation); the folders under a root so the test reads a
 * fixture tree.
 */
#include "thermal_read.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifndef O_CLOEXEC
#define O_CLOEXEC 0 /* the Windows builds' C runtime (nothing to inherit there) */
#endif

/* a path into buf: false when it does not fit (the read then fails as a
   missing file would) */
static bool pathf(char *buf, size_t cap, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int w = vsnprintf(buf, cap, fmt, ap);
    va_end(ap);
    if (w < 0 || (size_t)w >= cap) {
        buf[0] = '\0';
        return false;
    }
    return true;
}

/* src's text into dst, cut to fit */
static void copy_cut(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    n = n < cap - 1u ? n : cap - 1u;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void thermal_sample_none(ThermalSample *s)
{
    memset(s, 0, sizeof(*s));
    s->status = THERMAL_STATUS_NONE_READ;
    s->headroom = -1.0;
    s->hottest = -1;
    for (int i = 0; i < THERMAL_ZONES_MAX; i++) {
        s->zoneC[i] = -1000.0;
    }
}

int thermal_type_rank(const char *type)
{
    static const char *const k[] = {"gpu", "cpu", "soc", "battery", "skin"};
    char low[64];
    size_t n = 0;
    while (type[n] != '\0' && n + 1 < sizeof low) {
        low[n] = (char)tolower((unsigned char)type[n]);
        n++;
    }
    low[n] = '\0';
    for (int i = 0; i < (int)(sizeof k / sizeof k[0]); i++) {
        if (strstr(low, k[i]) != NULL) {
            return i;
        }
    }
    return -1;
}

static bool number(const char *text, long long *v)
{
    const char *p = text;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    char *end = NULL;
    errno = 0;
    const long long x = strtoll(p, &end, 10);
    if (end == p || errno != 0) {
        return false;
    }
    while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') {
        end++;
    }
    if (*end != '\0') {
        return false; /* garbage after the number */
    }
    *v = x;
    return true;
}

bool thermal_temp_c(const char *text, double *c)
{
    long long v;
    if (!number(text, &v)) {
        return false;
    }
    const long long a = v < 0 ? -v : v;
    *c = a >= 1000 ? (double)v / 1000.0 : (double)v;
    return true;
}

bool thermal_khz_mhz(const char *text, int *mhz)
{
    long long v;
    if (!number(text, &v) || v < 0 || v > 100000000LL) {
        return false;
    }
    *mhz = (int)((v + 500) / 1000);
    return true;
}

bool thermal_cpu_list(const char *text, int *first, int *last)
{
    bool any = false;
    const char *p = text;
    while (*p != '\0') {
        if (!isdigit((unsigned char)*p)) {
            p++;
            continue;
        }
        char *end;
        const long v = strtol(p, &end, 10);
        p = end;
        if (v < 0 || v > 4096) {
            return false;
        }
        if (!any || v < *first) {
            *first = (int)v;
        }
        if (!any || v > *last) {
            *last = (int)v;
        }
        any = true;
    }
    return any;
}

/* a small file into buf (NUL-ended): its length, or -errno */
static int read_small(const char *path, char *buf, size_t cap)
{
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return -errno;
    }
    const ssize_t n = read(fd, buf, cap - 1u);
    const int e = errno;
    close(fd);
    if (n < 0) {
        return -e;
    }
    buf[n] = '\0';
    return (int)n;
}

/* the number after prefix in name ("thermal_zone12" -> 12), -1 none */
static int index_of(const char *name, const char *prefix)
{
    const size_t p = strlen(prefix);
    if (strncmp(name, prefix, p) != 0 || !isdigit((unsigned char)name[p])) {
        return -1;
    }
    char *end;
    const long v = strtol(name + p, &end, 10);
    return *end == '\0' && v >= 0 && v < 100000 ? (int)v : -1;
}

void thermal_zones_scan(const char *root, ThermalZones *z)
{
    memset(z, 0, sizeof(*z));
    char dir[THERMAL_PATH];
    (void)pathf(dir, sizeof dir, "%s/sys/class/thermal", root);
    DIR *d = opendir(dir);
    if (d == NULL) {
        z->err = errno != 0 ? errno : ENOENT;
        return;
    }
    /* the candidates: rank, then the zone's number */
    int rank[THERMAL_ZONES_SCAN], num[THERMAL_ZONES_SCAN];
    char type[THERMAL_ZONES_SCAN][24];
    unsigned k = 0;
    int err = 0;
    const struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        const int i = index_of(e->d_name, "thermal_zone");
        if (i < 0) {
            continue;
        }
        z->seen++;
        if (k == THERMAL_ZONES_SCAN) {
            continue;
        }
        char path[THERMAL_PATH + 32], buf[64];
        (void)pathf(path, sizeof path, "%s/thermal_zone%d/type", dir, i);
        int n = read_small(path, buf, sizeof buf);
        if (n < 0) {
            err = -n;
            continue;
        }
        while (n > 0 && isspace((unsigned char)buf[n - 1])) {
            buf[--n] = '\0';
        }
        (void)pathf(path, sizeof path, "%s/thermal_zone%d/temp", dir, i);
        char t[32];
        const int m = read_small(path, t, sizeof t);
        double c;
        if (m < 0) {
            err = -m;
            continue;
        }
        if (!thermal_temp_c(t, &c)) {
            continue; /* garbage */
        }
        z->readable++;
        const int r = thermal_type_rank(buf);
        if (r < 0) {
            continue;
        }
        rank[k] = r;
        num[k] = i;
        copy_cut(type[k], sizeof type[k], buf);
        k++;
    }
    closedir(d);
    /* the best THERMAL_ZONES_MAX: by rank, then number */
    bool used[THERMAL_ZONES_SCAN] = {false};
    while (z->n < THERMAL_ZONES_MAX) {
        int best = -1;
        for (unsigned i = 0; i < k; i++) {
            if (!used[i] && (best < 0 || rank[i] < rank[best] ||
                             (rank[i] == rank[best] && num[i] < num[best]))) {
                best = (int)i;
            }
        }
        if (best < 0) {
            break;
        }
        used[best] = true;
        copy_cut(z->type[z->n], sizeof z->type[z->n], type[best]);
        (void)pathf(z->path[z->n], sizeof z->path[z->n], "%s/thermal_zone%d/temp", dir, num[best]);
        z->n++;
    }
    if (z->n == 0) {
        z->err = err;
    }
}

/* a cpufreq folder's file as MHz, -1 not read */
static int freq_of(const char *dir, const char *file)
{
    char path[THERMAL_PATH + 32], buf[32];
    int mhz;
    (void)pathf(path, sizeof path, "%s/%s", dir, file);
    return read_small(path, buf, sizeof buf) > 0 && thermal_khz_mhz(buf, &mhz) ? mhz : -1;
}

void thermal_cpus_scan(const char *root, ThermalCpus *c)
{
    memset(c, 0, sizeof(*c));
    char base[THERMAL_PATH];
    (void)pathf(base, sizeof base, "%s/sys/devices/system/cpu", root);
    /* cpufreq's policy folders first: one a cluster */
    char pol[THERMAL_PATH];
    (void)pathf(pol, sizeof pol, "%s/cpufreq", base);
    DIR *d = opendir(pol);
    int err = 0;
    if (d != NULL) {
        int idx[THERMAL_CPUS];
        unsigned k = 0;
        const struct dirent *e;
        while ((e = readdir(d)) != NULL && k < THERMAL_CPUS) {
            const int i = index_of(e->d_name, "policy");
            if (i >= 0) {
                idx[k++] = i;
            }
        }
        closedir(d);
        /* in order of their number */
        for (unsigned a = 1; a < k; a++) {
            const int v = idx[a];
            unsigned b = a;
            while (b > 0 && idx[b - 1] > v) {
                idx[b] = idx[b - 1];
                b--;
            }
            idx[b] = v;
        }
        for (unsigned a = 0; a < k && c->n < THERMAL_CLUSTERS; a++) {
            char dir[THERMAL_PATH + 16], path[THERMAL_PATH + 48], buf[64];
            (void)pathf(dir, sizeof dir, "%s/policy%d", pol, idx[a]);
            const int cur = freq_of(dir, "scaling_cur_freq");
            const int hw = freq_of(dir, "cpuinfo_max_freq");
            (void)pathf(path, sizeof path, "%s/related_cpus", dir);
            int first = idx[a], last = idx[a];
            if (read_small(path, buf, sizeof buf) > 0) {
                (void)thermal_cpu_list(buf, &first, &last);
            }
            if (cur <= 0) {
                err = EACCES;
                continue;
            }
            const unsigned j = c->n++;
            c->first[j] = first;
            c->last[j] = last;
            c->hwMaxMhz[j] = hw;
            copy_cut(c->dir[j], sizeof c->dir[j], dir);
        }
        if (c->n > 0) {
            c->policy = true;
            return;
        }
    } else {
        err = errno != 0 ? errno : ENOENT;
    }
    /* each core's folder: cores with the same hardware most in a row one
       cluster */
    for (int i = 0; i < THERMAL_CPUS; i++) {
        char dir[THERMAL_PATH + 24];
        (void)pathf(dir, sizeof dir, "%s/cpu%d/cpufreq", base, i);
        const int cur = freq_of(dir, "scaling_cur_freq");
        if (cur <= 0) {
            char probe[THERMAL_PATH + 24];
            (void)pathf(probe, sizeof probe, "%s/cpu%d", base, i);
            DIR *pd = opendir(probe);
            if (pd == NULL) {
                break; /* no such core: the end */
            }
            closedir(pd);
            err = err != 0 ? err : EACCES;
            continue;
        }
        const int hw = freq_of(dir, "cpuinfo_max_freq");
        if (c->n > 0 && c->hwMaxMhz[c->n - 1] == hw && c->last[c->n - 1] == i - 1) {
            c->last[c->n - 1] = i;
            continue;
        }
        if (c->n == THERMAL_CLUSTERS) {
            break;
        }
        const unsigned j = c->n++;
        c->first[j] = c->last[j] = i;
        c->hwMaxMhz[j] = hw;
        copy_cut(c->dir[j], sizeof c->dir[j], dir);
    }
    if (c->n == 0) {
        c->err = err != 0 ? err : ENOENT;
    }
}

bool thermal_sample_read(const ThermalZones *z, const ThermalCpus *c, ThermalSample *s)
{
    bool any = false;
    s->zones = z->n;
    s->hottest = -1;
    for (unsigned i = 0; i < z->n; i++) {
        char buf[32];
        double t;
        s->zoneC[i] = -1000.0;
        if (read_small(z->path[i], buf, sizeof buf) > 0 && thermal_temp_c(buf, &t)) {
            s->zoneC[i] = t;
            any = true;
            if (s->hottest < 0 || t > s->hottestC) {
                s->hottest = (int)i;
                s->hottestC = t;
            }
        }
    }
    s->clusters = c->n;
    for (unsigned i = 0; i < c->n; i++) {
        s->curMhz[i] = freq_of(c->dir[i], "scaling_cur_freq");
        s->maxMhz[i] = freq_of(c->dir[i], "scaling_max_freq");
        any = any || s->curMhz[i] > 0;
    }
    return any || s->status != THERMAL_STATUS_NONE_READ || s->headroom >= 0.0;
}

typedef struct Out {
    char *out;
    size_t cap, at;
} Out;

static void put(Out *o, const char *fmt, ...)
{
    if (o->cap == 0 || o->at + 1u >= o->cap) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    const int w = vsnprintf(o->out + o->at, o->cap - o->at, fmt, ap);
    va_end(ap);
    if (w > 0) {
        o->at = o->at + (size_t)w < o->cap ? o->at + (size_t)w : o->cap - 1u;
    }
}

static Out out_of(char *out, size_t cap)
{
    Out o = {out, cap, 0};
    if (cap > 0) {
        out[0] = '\0';
    }
    return o;
}

static void cores(Out *o, const ThermalCpus *c, unsigned i)
{
    if (c->first[i] == c->last[i]) {
        put(o, "core %d", c->first[i]);
    } else {
        put(o, "cores %d-%d", c->first[i], c->last[i]);
    }
}

size_t thermal_read_open_text(const ThermalZones *z, const ThermalCpus *c,
                              const ThermalSample *first, int api, const char *statusWhy,
                              const char *headroomWhy, char *out, size_t cap)
{
    Out o = out_of(out, cap);
    put(&o, "thermal: status %s, headroom %s", statusWhy[0] ? statusWhy : "from the system",
        headroomWhy[0] ? headroomWhy : "from the system");
    if (api > 0) {
        put(&o, " (API %d)", api);
    }
    if (z->n > 0) {
        put(&o, "; zones read: %u of %u (", z->n, z->seen);
        for (unsigned i = 0; i < z->n; i++) {
            put(&o, "%s%s", i ? ", " : "", z->type[i]);
            if (first != NULL && first->zoneC[i] > -100.0) {
                put(&o, " %.1f C", first->zoneC[i]);
            }
        }
        put(&o, "; %u readable, the others' types not GPU, CPU, SoC, battery or skin, or past %d)",
            z->readable, THERMAL_ZONES_MAX);
    } else if (z->err != 0) {
        put(&o, "; zones not readable (errno %d, %u found)", z->err, z->seen);
    } else {
        put(&o, "; no zone of the GPU, CPU, SoC, battery or skin (%u found, %u readable)", z->seen,
            z->readable);
    }
    if (c->n > 0) {
        put(&o,
            "; CPU clusters read (%s): ", c->policy ? "cpufreq policies" : "each core's cpufreq");
        for (unsigned i = 0; i < c->n; i++) {
            put(&o, "%s", i ? ", " : "");
            cores(&o, c, i);
            if (c->hwMaxMhz[i] > 0) {
                put(&o, " (most %d MHz)", c->hwMaxMhz[i]);
            }
        }
    } else {
        put(&o, "; CPU clocks not readable (errno %d)", c->err);
    }
    return o.at;
}

size_t thermal_token(const ThermalSample *s, char *out, size_t cap)
{
    Out o = out_of(out, cap);
    if (s->status != THERMAL_STATUS_NONE_READ) {
        put(&o, "%d/", s->status);
    } else {
        put(&o, "-/");
    }
    if (s->headroom >= 0.0) {
        put(&o, "%.2f/", s->headroom);
    } else {
        put(&o, "-/");
    }
    if (s->hottest >= 0) {
        put(&o, "%.1f/", s->hottestC);
    } else {
        put(&o, "-/");
    }
    if (s->clusters == 0) {
        put(&o, "-");
    }
    for (unsigned i = 0; i < s->clusters; i++) {
        put(&o, "%s", i ? "," : "");
        if (s->curMhz[i] > 0) {
            put(&o, "%d", s->curMhz[i]);
        } else {
            put(&o, "-");
        }
        if (s->maxMhz[i] > 0) {
            put(&o, ":%d", s->maxMhz[i]);
        } else {
            put(&o, ":-");
        }
    }
    return o.at;
}

size_t thermal_read_block_text(const ThermalSample *s, const bool *known, unsigned n,
                               const ThermalCpus *c, char *out, size_t cap)
{
    Out o = out_of(out, cap);
    int stLo = 99, stHi = -99, stFirst = THERMAL_STATUS_NONE_READ;
    bool stChanged = false, any = false;
    double hLo = 1e9, hHi = -1.0, tLo = 1e9, tHi = -1e9;
    int curLo[THERMAL_CLUSTERS], curHi[THERMAL_CLUSTERS], maxLo[THERMAL_CLUSTERS],
        maxHi[THERMAL_CLUSTERS];
    for (int i = 0; i < THERMAL_CLUSTERS; i++) {
        curLo[i] = maxLo[i] = 1 << 30;
        curHi[i] = maxHi[i] = -1;
    }
    unsigned clusters = 0;
    for (unsigned k = 0; k < n; k++) {
        if (!known[k]) {
            continue;
        }
        any = true;
        const ThermalSample *x = &s[k];
        if (x->status != THERMAL_STATUS_NONE_READ) {
            if (stFirst == THERMAL_STATUS_NONE_READ) {
                stFirst = x->status;
            } else if (x->status != stFirst) {
                stChanged = true;
            }
            stLo = x->status < stLo ? x->status : stLo;
            stHi = x->status > stHi ? x->status : stHi;
        }
        if (x->headroom >= 0.0) {
            hLo = x->headroom < hLo ? x->headroom : hLo;
            hHi = x->headroom > hHi ? x->headroom : hHi;
        }
        if (x->hottest >= 0) {
            tLo = x->hottestC < tLo ? x->hottestC : tLo;
            tHi = x->hottestC > tHi ? x->hottestC : tHi;
        }
        clusters = x->clusters > clusters ? x->clusters : clusters;
        for (unsigned i = 0; i < x->clusters && i < THERMAL_CLUSTERS; i++) {
            if (x->curMhz[i] > 0) {
                curLo[i] = x->curMhz[i] < curLo[i] ? x->curMhz[i] : curLo[i];
                curHi[i] = x->curMhz[i] > curHi[i] ? x->curMhz[i] : curHi[i];
            }
            if (x->maxMhz[i] > 0) {
                maxLo[i] = x->maxMhz[i] < maxLo[i] ? x->maxMhz[i] : maxLo[i];
                maxHi[i] = x->maxMhz[i] > maxHi[i] ? x->maxMhz[i] : maxHi[i];
            }
        }
    }
    if (!any) {
        return 0;
    }
    put(&o, "thermal in the block: status ");
    if (stHi >= stLo) {
        put(&o, "%d-%d (%s)", stLo, stHi, stChanged ? "changed" : "steady");
    } else {
        put(&o, "not read");
    }
    if (hHi >= 0.0) {
        put(&o, ", headroom %.2f-%.2f", hLo, hHi);
    } else {
        put(&o, ", headroom not read");
    }
    if (tHi > -1e8) {
        put(&o, ", hottest zone %.1f-%.1f C", tLo, tHi);
    } else {
        put(&o, ", no zone read");
    }
    bool maxChanged = false;
    for (unsigned i = 0; i < clusters && i < THERMAL_CLUSTERS; i++) {
        put(&o, "; ");
        if (c != NULL && i < c->n) {
            cores(&o, c, i);
        } else {
            put(&o, "cluster %u", i);
        }
        if (curHi[i] > 0) {
            put(&o, " %d-%d MHz", curLo[i], curHi[i]);
        } else {
            put(&o, " clock not read");
        }
        if (maxHi[i] > 0) {
            put(&o, ", allowed %d-%d MHz", maxLo[i], maxHi[i]);
            maxChanged = maxChanged || maxLo[i] != maxHi[i];
        }
        if (c != NULL && i < c->n && c->hwMaxMhz[i] > 0) {
            put(&o, " of %d", c->hwMaxMhz[i]);
        }
    }
    if (clusters > 0) {
        put(&o, "; the allowed most changed: %s", maxChanged ? "yes" : "no");
    }
    return o.at;
}

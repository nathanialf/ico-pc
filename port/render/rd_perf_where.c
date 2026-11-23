/*
 * port/render/rd_perf_where.c
 *
 * Where the time went, one table a block (rd_perf_where.h): pure.
 */
#include "rd_perf_where.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct Out {
    char *buf;
    size_t n, at;
    int total;
} Out;

static void put(Out *o, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *dst = o->at < o->n ? o->buf + o->at : NULL;
    const size_t room = o->at < o->n ? o->n - o->at : 0;
    const int w = vsnprintf(dst, room, fmt, ap);
    va_end(ap);
    if (w > 0) {
        o->total += w;
        o->at = o->at + (size_t)w < o->n ? o->at + (size_t)w : (o->n > 0 ? o->n - 1 : 0);
    }
}

static Out out_of(char *buf, size_t n)
{
    Out o = {buf, n, 0, 0};
    if (n > 0) {
        buf[0] = '\0';
    }
    return o;
}

static double per(double sum, double count)
{
    return count > 0.0 ? sum / count : 0.0;
}

static double pct(double a, double b)
{
    return b > 0.0 ? 100.0 * a / b : 0.0;
}

double rd_perf_where_explained_ms(const RdPerfRankBlock *b, const RdPerfWhereRates *r)
{
    (void)b;
    (void)r;
    return -1.0; /* the rate card's package fills this in */
}

/* the GPU pieces a picture: the lists, the pre-pass, the stretch before
   list 0, the present (the effects apart: inside their lists) */
#define PIECES (RD_LIST_COUNT + 3)

static double piece_ms(const RdPerfRankBlock *b, int i)
{
    const double g = (double)b->gpuN;
    if (i < RD_LIST_COUNT) {
        return per(b->listMs[i], g);
    }
    if (i == RD_LIST_COUNT) {
        return per(b->prepassMs, g);
    }
    if (i == RD_LIST_COUNT + 1) {
        return per(b->uploadGpuMs, g);
    }
    return per(b->presentGpuMs, g);
}

static void piece_name(int i, char *out, size_t cap)
{
    static const char *const k_rest[3] = {"pre-pass", "before list 0", "present"};
    if (i < RD_LIST_COUNT) {
        snprintf(out, cap, "list %d", i);
    } else {
        snprintf(out, cap, "%s", k_rest[i - RD_LIST_COUNT]);
    }
}

static void limit_line(Out *o, const RdPerfRankInfo *in, const RdPerfRankVerdict *v)
{
    put(o, "where: %s: limit %s; %.1f of %.1f fps (the game %.1f of %.1f)",
        in->scene != NULL ? in->scene : "?", rd_perf_bound_name(v->bound), in->presentedFps,
        in->targetFps, in->gameFps, in->gameRateFps);
    put(o, "; GPU work %.1f ms (%.0f %% of the %.1f ms between presents)", v->workMs,
        pct(v->workMs, v->periodMs), v->periodMs);
    put(o, "; render thread %.1f ms (%.0f %%)", v->ownMs, pct(v->ownMs, v->periodMs));
    put(o, "; game step %.1f ms (%.0f %% of its %.1f ms tick)", v->stepCpuMs,
        pct(v->stepCpuMs, v->tickMs), v->tickMs);
}

static void gpu_line(Out *o, const RdPerfRankBlock *b, const RdPerfWhereExtra *x)
{
    const double g = (double)b->gpuN, n = b->n ? (double)b->n : 1.0;
    const double span = per(b->gpuMs, g);
    put(o, "where: GPU a picture %.1f ms (work %.1f ms):", span, per(b->workMs, g));
    if (b->gpuN == 0) {
        put(o, " no picture timed");
        return;
    }
    const double t = (double)b->timed;
    const char *sep = " ";
    for (int i = 0; i < PIECES; i++) {
        const double ms = piece_ms(b, i);
        if (!(ms > 0.0)) {
            continue;
        }
        char name[32];
        piece_name(i, name, sizeof name);
        put(o, "%s%s %.1f ms %.0f %%", sep, name, ms, pct(ms, span));
        sep = "; ";
        if (i < RD_LIST_COUNT) {
            const RdPerfRankClass *c = &b->cls[RD_PERF_RANK_LIST0 + i];
            if (t > 0.0 && c->seen) {
                put(o, " [%.0f draws, %.1f MB loaded, %.1f MB stored, %.2f Mpx]", c->draws / t,
                    (c->colorLoadKB + c->depthLoadKB) / 1024.0 / t,
                    (c->colorStoreKB + c->depthStoreKB) / 1024.0 / t, c->kTexels / 1000.0 / t);
            } else {
                put(o, " [...]");
            }
        }
    }
    bool any = false;
    for (int p = 0; p < RD_PERF_POST_COUNT; p++) {
        const double ms = per(b->postMs[p], g);
        if (!(ms > 0.0)) {
            continue;
        }
        const char *nm = x != NULL && x->postName != NULL ? x->postName(p) : NULL;
        char tmp[24];
        if (nm == NULL) {
            snprintf(tmp, sizeof tmp, "effect %d", p);
            nm = tmp;
        }
        put(o, "%s%s %.1f ms %.0f %%", any ? ", " : "; effects (inside their lists): ", nm, ms,
            pct(ms, span));
        any = true;
    }
    put(o,
        "; all: %.1fk triangles, %.1fk vertex invocations, %.0f draws, %.1f MB loaded, %.1f MB "
        "stored, %.1f Mpx",
        b->vuTris / n / 1000.0, b->vuInv / n / 1000.0, b->draws / n,
        (b->colorLoad + b->depthLoad) / n / 1048576.0,
        (b->colorStore + b->depthStore) / n / 1048576.0, b->texels / n / 1e6);
}

/* the render thread's parts a picture (submit only where it submitted) */
#define CPU_PARTS 5

static void cpu_parts(const RdPerfRankBlock *b, double *ms)
{
    const double n = b->n ? (double)b->n : 1.0;
    ms[0] = b->walkMs / n;
    ms[1] = b->uploadCpuMs / n;
    ms[2] = b->bindMs / n;
    ms[3] = b->interpMs / n;
    ms[4] = b->submitMs / n;
}

static const char *const k_cpu[CPU_PARTS] = {"walk", "uploads", "binds", "interpolation", "submit"};

static void cpu_line(Out *o, const RdPerfRankBlock *b, const RdPerfRankVerdict *v)
{
    const double n = b->n ? (double)b->n : 1.0;
    double ms[CPU_PARTS];
    cpu_parts(b, ms);
    put(o, "where: CPU a picture: render thread %.1f ms (", b->ownMs / n);
    for (int i = 0; i < CPU_PARTS; i++) {
        put(o, "%s%s %.1f", i ? ", " : "", k_cpu[i], ms[i]);
    }
    put(o, "); submit thread %.1f ms in %u pictures; present calls %.1f ms; GPU waits %.1f ms",
        per(b->subSubmitMs + b->subPresentMs, (double)b->subN), b->subN, b->presentMs / n,
        b->fenceMs / n);
    put(o, "; game step %.1f ms", v->stepCpuMs);
}

static void largest_line(Out *o, const RdPerfRankBlock *b, const RdPerfRankVerdict *v,
                         const RdPerfWhereExtra *x)
{
    put(o, "where: largest:");
    const double span = per(b->gpuMs, (double)b->gpuN);
    int best = -1;
    double bestMs = 0.0;
    for (int i = 0; i < PIECES && b->gpuN > 0; i++) {
        const double ms = piece_ms(b, i);
        if (ms > bestMs) {
            best = i;
            bestMs = ms;
        }
    }
    if (best >= 0) {
        char name[32];
        piece_name(best, name, sizeof name);
        put(o, " GPU %s %.1f ms (%.0f %% of the GPU span)", name, bestMs, pct(bestMs, span));
    } else {
        put(o, " GPU none timed");
    }
    double ms[CPU_PARTS];
    cpu_parts(b, ms);
    int c = -1;
    double cMs = 0.0;
    for (int i = 0; i < CPU_PARTS; i++) {
        if (ms[i] > cMs) {
            c = i;
            cMs = ms[i];
        }
    }
    const char *cName = c >= 0 ? k_cpu[c] : NULL;
    if (v->stepCpuMs > cMs) {
        cName = "game step";
        cMs = v->stepCpuMs;
    }
    if (cName != NULL) {
        put(o, "; CPU %s %.1f ms (%.0f %% of the time between presents)", cName, cMs,
            pct(cMs, v->periodMs));
    } else {
        put(o, "; CPU none measured");
    }
    if (x != NULL && x->rates != NULL) {
        const double e = rd_perf_where_explained_ms(b, x->rates);
        if (e >= 0.0) {
            put(o, "; explained by the rate card: %.1f of %.1f ms", e,
                per(b->workMs, (double)b->gpuN));
        }
    }
}

int rd_perf_where_text(const RdPerfRankBlock *b, const RdPerfRankInfo *in,
                       const RdPerfRankVerdict *v, const RdPerfWhereExtra *x, unsigned line,
                       char *buf, size_t n)
{
    Out o = out_of(buf, n);
    switch (line) {
    case 0:
        limit_line(&o, in, v);
        break;
    case 1:
        gpu_line(&o, b, x);
        break;
    case 2:
        cpu_line(&o, b, v);
        break;
    case 3:
        put(&o, "where: attempts: %s",
            x != NULL && x->attempts != NULL && x->attempts[0] ? x->attempts : "not known");
        break;
    case 4:
        put(&o, "where: phone: %s; stalls: %s",
            x != NULL && x->phone != NULL && x->phone[0] ? x->phone : "not read",
            x != NULL && x->stalls != NULL && x->stalls[0] ? x->stalls : "none");
        break;
    case 5:
        largest_line(&o, b, v, x);
        break;
    default:
        return 0;
    }
    return o.total;
}

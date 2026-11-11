/*
 * port/render/rd_perf_rank.c
 *
 * The 10 s block's performance lines by scene and the run's profile
 * (rd_perf_rank.h).  Pure: no device, no renderer state.
 */
#include "rd_perf_rank.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
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
        o->at += (size_t)w;
    }
}

static Out outOf(char *buf, size_t n)
{
    Out o = {buf, n, 0, 0};
    if (n > 0) {
        buf[0] = '\0';
    }
    return o;
}

static double mb(double bytes)
{
    return bytes / (1024.0 * 1024.0);
}

static double per(double sum, double count)
{
    return count > 0.0 ? sum / count : 0.0;
}

int rd_perf_rank_class_of(const RdPerfPass *p)
{
    if (p->kind == RD_PERF_PASS_RENDER) {
        if (p->post >= 0 && p->post < RD_PERF_POST_COUNT) {
            return RD_PERF_RANK_POST0 + p->post;
        }
        if (p->target == RD_PERF_TARGET_SHADOW_COUNT) {
            return RD_PERF_RANK_SHADOW;
        }
        if (p->list >= 0 && p->list < RD_LIST_COUNT) {
            return RD_PERF_RANK_LIST0 + p->list;
        }
    }
    const int k = p->kind < RD_PERF_PASS_KIND_COUNT ? p->kind : RD_PERF_PASS_RENDER;
    return RD_PERF_RANK_KIND0 + k;
}

void rd_perf_rank_block_reset(RdPerfRankBlock *b)
{
    memset(b, 0, sizeof(*b));
}

void rd_perf_rank_add_record(RdPerfRankBlock *b, const RdPerfRecord *r, double vuInvocations)
{
    b->n++;
    b->keep += r->keep ? 1u : 0u;
    /* the submit thread's submit is its own, not the render thread's
       (rd_perf.c rd__perf_submit_times hands its times to the record) */
    const bool onSubmit = r->presentThread == RD_PRESENT_ON_SUBMIT;
    const double own =
        r->interpMs + r->uploadMs + r->walkMs + r->bindMs + (onSubmit ? 0.0 : r->submitMs);
    b->cpuMs += r->totalMs;
    b->ownMs += own;
    b->walkMs += r->walkMs;
    if (onSubmit) {
        b->subN++;
        b->subSubmitMs += r->submitMs;
        b->subPresentMs += r->presentMs;
    } else {
        b->submitMs += r->submitMs;
    }
    if (r->presented) {
        b->presents++;
    }
    if (b->n == 1u) {
        b->firstStartMs = r->startMs;
    }
    b->lastStartMs = r->startMs;
    b->uploadCpuMs += r->uploadMs;
    b->bindMs += r->bindMs;
    b->interpMs += r->interpMs;
    b->presentMs += r->presentMs;
    b->fenceMs += r->fenceWaitMs;
    b->draws += r->draws;
    b->pipeBinds += r->pipelineBinds;
    b->groupBinds += r->bindGroupBinds;
    b->passes += r->renderPasses;
    b->dateSnaps += r->dateSnapshots;
    b->uploadBytes += (double)r->uploadBytes;
    b->texUploads += r->textureUploads;
    b->vuInv += vuInvocations;
    b->colorLoad += (double)r->colorLoadBytes;
    b->colorStore += (double)r->colorStoreBytes;
    b->depthLoad += (double)r->depthLoadBytes;
    b->depthStore += (double)r->depthStoreBytes;
    b->clear += (double)r->clearBytes;
    b->texels += (double)r->passTexels;
    b->copy += (double)r->copyBytes;
    b->untracked += r->passUntracked;
    if (r->gpuValid) {
        b->gpuN++;
        b->gpuMs += r->gpuMs;
        double work = r->gpuMs - r->gpuDisplayWaitMs;
        work = work > 0.0 ? work : 0.0;
        b->workMs += work;
        b->prepassMs += r->gpuPrepassMs;
        b->uploadGpuMs += r->gpuUploadMs;
        b->presentGpuMs += r->gpuPresentMs;
        b->displayWaitMs += r->gpuDisplayWaitMs;
        for (int l = 0; l < RD_LIST_COUNT; l++) {
            b->listMs[l] += r->gpuListMs[l];
        }
        for (int p = 0; p < RD_PERF_POST_COUNT; p++) {
            b->postMs[p] += r->gpuPostMs[p];
        }
        const int bin = work >= (double)(RD_PERF_RANK_BINS - 1) ? RD_PERF_RANK_BINS - 1 : (int)work;
        b->hist[bin]++;
        if (!r->keep) {
            /* a keep frame's work is a held picture's: no vertex work */
            const double x = vuInvocations / 1000.0;
            if (x > 0.0 && (b->fxMin == 0.0 || x < b->fxMin)) {
                b->fxMin = x;
            }
            b->fxMax = x > b->fxMax ? x : b->fxMax;
            b->fitN++;
            b->fx += x;
            b->fy += work;
            b->fxx += x * x;
            b->fxy += x * work;
            b->fyy += work * work;
        }
    }
    if (r->gpuSlotWaitMs >= 0.05) {
        b->waitN++;
        b->waitMs += r->gpuSlotWaitMs;
        b->waitMaxMs = r->gpuSlotWaitMs > b->waitMaxMs ? r->gpuSlotWaitMs : b->waitMaxMs;
    }
    if (r->gpuDoneValid) {
        b->doneN++;
        b->doneMs += r->gpuDoneMs;
        b->doneMaxMs = r->gpuDoneMs > b->doneMaxMs ? r->gpuDoneMs : b->doneMaxMs;
    }
}

void rd_perf_rank_add_pieces(RdPerfRankBlock *b, const RdPerfRecord *r, const RdPerfPass *p,
                             uint32_t n)
{
    if (p == NULL || n == 0) {
        return;
    }
    b->timed++;
    for (uint32_t i = 0; i < n; i++) {
        const RdPerfPass *q = &p[i];
        if (q->gpuUs < 0.0f) {
            continue; /* a stamp missing */
        }
        const double ms = (double)q->gpuUs / 1000.0;
        RdPerfRankClass *c = &b->cls[rd_perf_rank_class_of(q)];
        if (!c->seen) {
            c->first = *q;
            c->seen = true;
        }
        c->ms += ms;
        c->pieces += 1.0;
        c->draws += q->draws;
        c->binds += q->pipelineBinds;
        c->colorLoadKB += q->colorLoadKB;
        c->colorStoreKB += q->colorStoreKB;
        c->depthLoadKB += q->depthLoadKB;
        c->depthStoreKB += q->depthStoreKB;
        c->clearKB += q->clearKB;
        c->copyKB += q->copyKB;
        c->kTexels += q->kTexels;
        /* the passes whose time is their bytes: a DATE snapshot (one flat
           draw reading SCENE's alpha) and a render pass with no draw (its
           load, clear and store alone); the present waits for the display */
        if (q->kind == RD_PERF_PASS_DATE || (q->kind == RD_PERF_PASS_RENDER && q->draws == 0u)) {
            const double x =
                (double)(q->colorLoadKB + q->colorStoreKB + q->depthLoadKB + q->depthStoreKB) /
                1024.0;
            b->smN++;
            b->sx += x;
            b->sy += ms;
            b->sxx += x * x;
            b->sxy += x * ms;
            b->syy += ms * ms;
        }
        if (q->kind == RD_PERF_PASS_COPY || q->kind == RD_PERF_PASS_SNAP) {
            b->copyPieceMs += ms;
            b->copyPieceKB += q->copyKB;
        }
        if (q->kind == RD_PERF_PASS_RENDER && q->post < 0 && q->list >= 0 &&
            q->target != RD_PERF_TARGET_SHADOW_COUNT) {
            b->listLoadKB += (double)(q->colorLoadKB + q->depthLoadKB);
            b->listPieces += 1.0;
        }
    }
    if (r != NULL && r->gpuValid && r->gpuMs > b->slowMs) {
        b->slowMs = r->gpuMs;
        b->slowReplay = r->replay;
        b->slowFrame = r->frame;
        b->slowN = n < RD_PERF_RANK_SLOW_MAX ? n : RD_PERF_RANK_SLOW_MAX;
        memcpy(b->slow, p, (size_t)b->slowN * sizeof(*p));
    }
}

/* least squares y = a + s x over n points: false without a spread */
static bool fit(uint32_t n, double sx, double sy, double sxx, double sxy, double syy, double *a,
                double *s, double *r2)
{
    const double N = (double)n;
    const double vx = N * sxx - sx * sx, vy = N * syy - sy * sy, cxy = N * sxy - sx * sy;
    if (n < 8u || vx <= 1e-12) {
        return false;
    }
    *s = cxy / vx;
    *a = (sy - *s * sx) / N;
    *r2 = vy > 1e-12 ? (cxy * cxy) / (vx * vy) : 0.0;
    return true;
}

void rd_perf_rank_rates(const RdPerfRankBlock *b, double earlierMbMs, RdPerfRankRates *out)
{
    memset(out, 0, sizeof(*out));
    out->mbFrom = "no rate";
    double a = 0.0, s = 0.0, r2 = 0.0;
    if (fit(b->smN, b->sx, b->sy, b->sxx, b->sxy, b->syy, &a, &s, &r2)) {
        out->smallFit = true;
        out->passMs = a > 0.0 ? a : 0.0;
        out->mbMs = s > 0.0 ? s : 0.0;
        out->smallR2 = r2;
        out->smallGood = out->mbMs > 0.0 && r2 >= 0.3;
    }
    out->smallN = b->smN;
    if (b->copyPieceKB / 1024.0 >= 0.5) {
        out->copyRate = true;
        out->copyMbMs = b->copyPieceMs / (b->copyPieceKB / 1024.0);
    }
    out->vuXMin = b->fxMin;
    out->vuXMax = b->fxMax;
    if (fit(b->fitN, b->fx, b->fy, b->fxx, b->fxy, b->fyy, &a, &s, &r2)) {
        out->vuFit = true;
        out->vuBaseMs = a;
        out->vuPerK = s;
        out->vuR2 = r2;
        out->vuUsable =
            s > 0.0 && r2 >= RD_PERF_RANK_VU_R2 && b->fxMin > 0.0 && b->fxMax >= 2.0 * b->fxMin;
    }
    /* the attachment traffic's rate: this block's byte-bound fit, else the
       last that held earlier in the run; never the copies' (on a tile-based
       GPU a copy's piece carries the pass before it's flush: the PowerVR's
       0.01 ms a MB against its fits' 0.05 to 0.16) */
    if (out->smallGood) {
        out->mbRate = out->mbMs;
        out->mbFrom = "the byte-bound passes' fit";
    } else if (earlierMbMs > 0.0) {
        out->mbRate = earlierMbMs;
        out->mbFrom = "the rate of an earlier block of this run";
    }
}

const char *rd_perf_bound_name(int bound)
{
    switch (bound) {
    case RD_PERF_BOUND_NONE:
        return "at the game's rate";
    case RD_PERF_BOUND_GPU:
        return "GPU-bound";
    case RD_PERF_BOUND_RENDER:
        return "render-thread-bound";
    case RD_PERF_BOUND_GAME:
        return "game-thread-bound";
    case RD_PERF_BOUND_ELSE:
        return "bound by waits elsewhere";
    default:
        return "not measured";
    }
}

static void cand(RdPerfRankVerdict *v, double ms, bool estimate, const char *fmt, ...)
{
    if (!(ms > 0.05)) {
        return;
    }
    RdPerfRankCand *c;
    if (v->count < RD_PERF_RANK_CANDIDATES) {
        c = &v->c[v->count++];
    } else {
        /* full: the smallest goes, never a larger one (the table holds
           every class and estimate today; this keeps it so) */
        uint32_t low = 0;
        for (uint32_t i = 1; i < v->count; i++) {
            low = v->c[i].ms < v->c[low].ms ? i : low;
        }
        if (!(ms > v->c[low].ms)) {
            return;
        }
        c = &v->c[low];
    }
    c->ms = ms;
    c->estimate = estimate;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(c->text, sizeof c->text, fmt, ap);
    va_end(ap);
}

static void sortCands(RdPerfRankVerdict *v)
{
    for (uint32_t i = 1; i < v->count; i++) {
        const RdPerfRankCand x = v->c[i];
        uint32_t j = i;
        while (j > 0 && v->c[j - 1].ms < x.ms) {
            v->c[j] = v->c[j - 1];
            j--;
        }
        v->c[j] = x;
    }
}

/* a class's name for the ranked line (no label callback in the verdict) */
static void className(int k, char *buf, size_t n)
{
    static const char *const kPost[RD_PERF_POST_COUNT] = {
        "reduction",      "fog",       "motion blur", "aura",         "glow",
        "depth of field", "softening", "CRT",         "MSAA resolve", "crack fill",
    };
    static const char *const kKind[RD_PERF_PASS_KIND_COUNT] = {
        "uploads",      "VU pre-pass",    "render passes",  "copies",
        "snapshots",    "DATE snapshots", "fog depth read", "exact blend",
        "MSAA resolve", "stencil clears", "present",        "display wait",
    };
    if (k < RD_PERF_RANK_POST0) {
        snprintf(buf, n, "list %d", k - RD_PERF_RANK_LIST0);
    } else if (k < RD_PERF_RANK_SHADOW) {
        snprintf(buf, n, "%s", kPost[k - RD_PERF_RANK_POST0]);
    } else if (k == RD_PERF_RANK_SHADOW) {
        snprintf(buf, n, "shadow count");
    } else {
        snprintf(buf, n, "%s", kKind[k - RD_PERF_RANK_KIND0]);
    }
}

/* the vertex estimate, or why there is none (v->vertexWhy) */
static void vertexCand(const RdPerfRankBlock *b, const RdPerfRankInfo *in,
                       const RdPerfRankRates *rt, RdPerfRankVerdict *v)
{
    const double n = b->n ? (double)b->n : 1.0;
    if (!rt->vuUsable) {
        Out o = outOf(v->vertexWhy, sizeof v->vertexWhy);
        if (!rt->vuFit) {
            put(&o, "no fit over %u pictures without keep frames", b->fitN);
        } else {
            const char *sep = "";
            if (!(rt->vuPerK > 0.0)) {
                put(&o, "the fit's slope %.3f ms a thousand is not above 0", rt->vuPerK);
                sep = ", ";
            }
            if (rt->vuR2 < RD_PERF_RANK_VU_R2) {
                put(&o, "%sthe fit's R^2 %.2f is under %.2f", sep, rt->vuR2, RD_PERF_RANK_VU_R2);
                sep = ", ";
            }
            if (!(rt->vuXMin > 0.0) || rt->vuXMax < 2.0 * rt->vuXMin) {
                put(&o,
                    "%sthe invocations (%.1fk to %.1fk a picture) are under the 2x spread "
                    "the fit needs",
                    sep, rt->vuXMin, rt->vuXMax);
            }
        }
        return;
    }
    const double k = b->vuInv / n / 1000.0;
    double ms = k * rt->vuPerK;
    bool capped = false;
    double cap = 0.0;
    if (in->prepass) {
        /* with the pre-pass the vertex work is its pass: no more than it
           measured (the fit's slope also scales with the pixels the
           geometry brings) */
        const int kp = RD_PERF_RANK_KIND0 + RD_PERF_PASS_PREPASS;
        cap = b->timed && b->cls[kp].seen ? b->cls[kp].ms / (double)b->timed
                                          : per(b->prepassMs, (double)b->gpuN);
        if (ms > cap) {
            ms = cap;
            capped = true;
        }
    }
    cand(v, ms, true,
         "vertex work est. %.1f ms (%.0fk vertex invocations a picture at %.3f ms a thousand, R^2 "
         "%.2f%s)",
         ms, k, rt->vuPerK, rt->vuR2, capped ? "; capped at the measured VU pre-pass" : "");
}

static void gpuCands(const RdPerfRankBlock *b, const RdPerfRankInfo *in, const RdPerfRankRates *rt,
                     RdPerfRankVerdict *v)
{
    const double n = b->n ? (double)b->n : 1.0;
    const double g = b->gpuN ? (double)b->gpuN : 1.0;
    char name[48];
    if (b->timed) {
        const double t = (double)b->timed;
        for (int k = 0; k < RD_PERF_RANK_CLASSES; k++) {
            const RdPerfRankClass *c = &b->cls[k];
            if (!c->seen || k == RD_PERF_RANK_KIND0 + RD_PERF_PASS_WAIT ||
                k == RD_PERF_RANK_KIND0 + RD_PERF_PASS_UPLOAD) {
                continue; /* the display wait is no work; the uploads below */
            }
            className(k, name, sizeof name);
            cand(v, c->ms / t, false,
                 "%s %.1f ms measured (%.1f passes, %.0f draws, %.0f binds; loads %.1f MB, stores "
                 "%.1f MB, %.2f Mpx)",
                 name, c->ms / t, c->pieces / t, c->draws / t, c->binds / t,
                 (c->colorLoadKB + c->depthLoadKB) / t / 1024.0,
                 (c->colorStoreKB + c->depthStoreKB) / t / 1024.0, c->kTexels / t / 1000.0);
        }
    } else if (b->gpuN) {
        /* no pass timing: the records' lists and effects */
        for (int l = 0; l < RD_LIST_COUNT; l++) {
            cand(v, b->listMs[l] / g, false, "list %d %.1f ms measured", l, b->listMs[l] / g);
        }
        for (int p = 0; p < RD_PERF_POST_COUNT; p++) {
            className(RD_PERF_RANK_POST0 + p, name, sizeof name);
            cand(v, b->postMs[p] / g, false, "%s %.1f ms measured (inside its list)", name,
                 b->postMs[p] / g);
        }
    }
    if (b->gpuN) {
        cand(v, b->uploadGpuMs / g, false, "uploads %.1f ms measured (%.0f KB, %.1f textures)",
             b->uploadGpuMs / g, b->uploadBytes / n / 1024.0, b->texUploads / n);
    }
    const double loads = mb(b->colorLoad + b->depthLoad) / n;
    const double dstore = mb(b->depthStore) / n;
    if (rt->mbRate > 0.0) {
        cand(v, loads * rt->mbRate, true,
             "tile loads est. %.1f ms (%.1f MB loaded a picture by %.1f passes, at %.2f ms a MB, "
             "%s)",
             loads * rt->mbRate, loads, b->passes / n, rt->mbRate, rt->mbFrom);
        cand(v, dstore * rt->mbRate, true,
             "depth stores est. %.1f ms (%.1f MB of depth and stencil stored a picture, at %.2f "
             "ms a MB)",
             dstore * rt->mbRate, dstore, rt->mbRate);
    }
    if (rt->smallGood && rt->passMs > 0.0) {
        cand(v, b->passes / n * rt->passMs, true,
             "pass count est. %.1f ms (%.1f passes a picture at %.2f ms each, the byte-bound "
             "passes' fit)",
             b->passes / n * rt->passMs, b->passes / n, rt->passMs);
    }
    if (b->dateSnaps > 0.0) {
        const double t = b->timed ? (double)b->timed : 0.0;
        const double dateMs = t > 0.0 ? b->cls[RD_PERF_RANK_KIND0 + RD_PERF_PASS_DATE].ms / t : 0.0;
        const double reload = b->listPieces > 0.0 ? b->listLoadKB / b->listPieces / 1024.0 : 0.0;
        const double per = b->dateSnaps / n;
        cand(v, dateMs + per * reload * rt->mbRate, true,
             "DATE breaks est. %.1f ms (%.1f a picture: %.1f ms measured for the snapshots + "
             "%.1f MB reloaded after them%s)",
             dateMs + per * reload * rt->mbRate, per, dateMs, per * reload,
             rt->mbRate > 0.0 ? "" : ", no rate for the reload");
    }
    vertexCand(b, in, rt, v);
}

static void cpuCands(const RdPerfRankBlock *b, RdPerfRankVerdict *v)
{
    const double n = b->n ? (double)b->n : 1.0;
    cand(v, b->walkMs / n, false,
         "the walk %.1f ms measured (%.0f draws, %.0f pipeline and %.0f "
         "bind group binds a picture)",
         b->walkMs / n, b->draws / n, b->pipeBinds / n, b->groupBinds / n);
    /* the render thread's own submits only (the submit thread's are not
       this thread's time) */
    cand(v, b->submitMs / n, false, "the submit %.1f ms measured", b->submitMs / n);
    cand(v, b->uploadCpuMs / n, false, "the uploads' CPU %.1f ms measured (%.0f KB a picture)",
         b->uploadCpuMs / n, b->uploadBytes / n / 1024.0);
    cand(v, b->bindMs / n, false, "the binds %.1f ms measured", b->bindMs / n);
    cand(v, b->interpMs / n, false, "the interpolation %.1f ms measured", b->interpMs / n);
}

void rd_perf_rank_verdict(const RdPerfRankBlock *b, const RdPerfRankInfo *in,
                          RdPerfRankVerdict *out)
{
    memset(out, 0, sizeof(*out));
    const double n = b->n ? (double)b->n : 1.0;
    out->workMs = per(b->workMs, (double)b->gpuN);
    out->ownMs = b->ownMs / n;
    out->stepCpuMs = in->stepCpuMs;
    out->periodMs = in->presentedFps > 0.0 ? 1000.0 / in->presentedFps : 0.0;
    out->tickMs = in->gameRateFps > 0.0 ? 1000.0 / in->gameRateFps : 0.0;
    out->below = (in->targetFps > 0.0 && in->presentedFps < 0.95 * in->targetFps) ||
                 (in->gameRateFps > 0.0 && in->gameFps < 0.95 * in->gameRateFps);
    if (b->n == 0 || out->periodMs <= 0.0) {
        out->bound = RD_PERF_BOUND_UNKNOWN;
    } else if (!out->below) {
        out->bound = RD_PERF_BOUND_NONE;
    } else if (b->gpuN && out->workMs >= 0.8 * out->periodMs) {
        out->bound = RD_PERF_BOUND_GPU;
    } else if (out->ownMs >= 0.8 * out->periodMs) {
        out->bound = RD_PERF_BOUND_RENDER;
    } else if (out->tickMs > 0.0 && in->stepCpuMs >= 0.8 * out->tickMs) {
        out->bound = RD_PERF_BOUND_GAME;
    } else {
        out->bound = RD_PERF_BOUND_ELSE;
    }
    RdPerfRankRates rt;
    rd_perf_rank_rates(b, in->earlierMbMs, &rt);
    if (out->bound == RD_PERF_BOUND_RENDER) {
        cpuCands(b, out);
    } else if (out->bound == RD_PERF_BOUND_GAME) {
        cand(out, in->stepCpuMs, false, "the game step's CPU %.1f ms measured of its %.1f ms tick",
             in->stepCpuMs, out->tickMs);
    } else if (out->bound == RD_PERF_BOUND_ELSE) {
        cand(out, b->presentMs / n, false, "the present calls %.1f ms measured a picture",
             b->presentMs / n);
        cand(out, b->fenceMs / n, false,
             "the waits for the GPU %.1f ms measured a picture (the slot waits)", b->fenceMs / n);
        cand(out, b->subSubmitMs / n, false,
             "the submit thread's submit %.1f ms measured a picture", b->subSubmitMs / n);
        gpuCands(b, in, &rt, out);
    } else {
        gpuCands(b, in, &rt, out);
    }
    sortCands(out);
}

int rd_perf_rank_ranked_text(const RdPerfRankInfo *in, const RdPerfRankVerdict *v, char *buf,
                             size_t n)
{
    Out o = outOf(buf, n);
    put(&o, "perf ranked: %s: %.1f of %.1f fps (the game %.1f of %.1f), %s", in->scene,
        in->presentedFps, in->targetFps, in->gameFps, in->gameRateFps,
        rd_perf_bound_name(v->bound));
    if (v->bound == RD_PERF_BOUND_GPU) {
        put(&o, " (the GPU's work %.1f ms a picture of the %.1f ms between presents)", v->workMs,
            v->periodMs);
    } else if (v->bound == RD_PERF_BOUND_RENDER) {
        put(&o, " (its own CPU %.1f ms a picture of the %.1f ms between presents)", v->ownMs,
            v->periodMs);
    } else if (v->bound == RD_PERF_BOUND_GAME) {
        put(&o, " (its step's CPU %.1f ms of the %.1f ms tick)", v->stepCpuMs, v->tickMs);
    } else if (v->bound == RD_PERF_BOUND_ELSE) {
        put(&o,
            " (the GPU's work %.1f ms, the render thread's CPU %.1f ms, the game step's %.1f "
            "ms, of the %.1f ms between presents)",
            v->workMs, v->ownMs, v->stepCpuMs, v->periodMs);
    }
    const uint32_t top = v->count < RD_PERF_RANK_TOP ? v->count : RD_PERF_RANK_TOP;
    if (top == 0) {
        put(&o, ": nothing measured to rank");
    } else {
        put(&o, ":");
        for (uint32_t i = 0; i < top; i++) {
            put(&o, "%s %u. %s", i ? ";" : "", i + 1u, v->c[i].text);
        }
    }
    if (v->vertexWhy[0] != '\0') {
        put(&o, "; no vertex work estimate (%s)", v->vertexWhy);
    }
    if (top != 0) {
        put(&o, " (each saving alone, they overlap; est. = this block's counters times its own "
                "rates)");
    }
    return o.total;
}

int rd_perf_rank_scene_text(const RdPerfRankBlock *b, const RdPerfRankInfo *in,
                            const RdPerfRankVerdict *v, char *buf, size_t n)
{
    Out o = outOf(buf, n);
    const double N = b->n ? (double)b->n : 1.0;
    put(&o,
        "perf scene: %s, %u pictures (%u keep frames): %.1f of %.1f fps (the game %.1f of "
        "%.1f), %s",
        in->scene, b->n, b->keep, in->presentedFps, in->targetFps, in->gameFps, in->gameRateFps,
        rd_perf_bound_name(v->bound));
    if (b->gpuN) {
        put(&o, "; the GPU's work %.1f ms a picture", v->workMs);
    } else {
        put(&o, "; no GPU times");
    }
    if (v->periodMs > 0.0) {
        put(&o, " of the %.1f ms between presents (%.0f %%)", v->periodMs,
            100.0 * v->workMs / v->periodMs);
    }
    if (b->subN == 0) {
        put(&o,
            "; the render thread's own CPU %.1f ms (walk %.1f, submit %.1f, uploads %.1f, binds "
            "%.1f, interpolation %.1f), its waits for the GPU %.1f ms, its present calls %.1f ms",
            v->ownMs, b->walkMs / N, b->submitMs / N, b->uploadCpuMs / N, b->bindMs / N,
            b->interpMs / N, b->fenceMs / N, b->presentMs / N);
    } else {
        /* the submit thread on: its submit and present apart (the render
           thread's own CPU is not that thread's time) */
        put(&o,
            "; the render thread's own CPU %.1f ms (walk %.1f, uploads %.1f, binds %.1f, "
            "interpolation %.1f",
            v->ownMs, b->walkMs / N, b->uploadCpuMs / N, b->bindMs / N, b->interpMs / N);
        if (b->subN < b->n) {
            put(&o, ", submit %.1f", b->submitMs / N);
        }
        put(&o, "), its waits for the GPU %.1f ms", b->fenceMs / N);
        if (b->subN < b->n) {
            put(&o, ", its present calls %.1f ms", (b->presentMs - b->subPresentMs) / N);
        }
        put(&o,
            "; the submit thread's submit %.1f ms and present calls %.1f ms (%u of the %u "
            "pictures)",
            b->subSubmitMs / N, b->subPresentMs / N, b->subN, b->n);
    }
    put(&o, "; the game step's CPU %.1f ms", in->stepCpuMs);
    if (v->tickMs > 0.0) {
        put(&o, " of its %.1f ms tick", v->tickMs);
    }
    if (b->doneN) {
        put(&o,
            "; a picture's work done %.1f ms after its last submit (longest %.1f), the slot "
            "wait blocked %u times (%.1f ms on average, longest %.1f)",
            b->doneMs / b->doneN, b->doneMaxMs, b->waitN, per(b->waitMs, (double)b->waitN),
            b->waitMaxMs);
    }
    put(&o,
        "; passes asked a picture: %.1f passes, colour %.1f MB loaded %.1f MB stored, depth and "
        "stencil %.1f MB loaded %.1f MB stored, %.1f MB cleared, %.2f Mpx of area, copies %.1f "
        "MB (each copy once: as many read as written; uncompressed; %.1f passes with an attachment of no known format, at 4 bytes a "
        "texel); %.0f draws, %.0f pipeline and %.0f bind group binds, %.0fk vertex "
        "invocations, %.0f KB uploaded (%.1f textures), %.1f DATE snapshots",
        b->passes / N, mb(b->colorLoad) / N, mb(b->colorStore) / N, mb(b->depthLoad) / N,
        mb(b->depthStore) / N, mb(b->clear) / N, b->texels / N / 1e6, mb(b->copy) / N,
        b->untracked / N, b->draws / N, b->pipeBinds / N, b->groupBinds / N, b->vuInv / N / 1000.0,
        b->uploadBytes / N / 1024.0, b->texUploads / N, b->dateSnaps / N);
    if (b->gpuN) {
        const double g = (double)b->gpuN;
        put(&o,
            "; GPU a picture: span %.1f ms, display wait %.1f, before list 0 %.1f, VU pre-pass "
            "%.1f, present %.1f; the pictures' GPU work median %.1f ms, worst %.0f ms",
            b->gpuMs / g, b->displayWaitMs / g, b->uploadGpuMs / g, b->prepassMs / g,
            b->presentGpuMs / g, rd_perf_rank_hist_median(b->hist),
            rd_perf_rank_hist_worst(b->hist));
    }
    return o.total;
}

int rd_perf_rank_classes_text(const RdPerfRankBlock *b, RdPerfRankLabel label, unsigned top,
                              char *buf, size_t n)
{
    Out o = outOf(buf, n);
    if (b->timed == 0) {
        put(&o, "perf classes: no replay timed pass by pass in the block");
        return o.total;
    }
    const double t = (double)b->timed;
    int order[RD_PERF_RANK_CLASSES];
    int count = 0;
    double all = 0.0;
    for (int k = 0; k < RD_PERF_RANK_CLASSES; k++) {
        if (b->cls[k].seen) {
            order[count++] = k;
            all += b->cls[k].ms;
        }
    }
    for (int i = 1; i < count; i++) {
        const int x = order[i];
        int j = i;
        while (j > 0 && b->cls[order[j - 1]].ms < b->cls[x].ms) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = x;
    }
    put(&o, "perf classes a picture (%u timed replays): ", b->timed);
    const int shown = (unsigned)count < top ? count : (int)top;
    for (int i = 0; i < shown; i++) {
        const RdPerfRankClass *c = &b->cls[order[i]];
        char name[64];
        if (order[i] >= RD_PERF_RANK_KIND0 || label == NULL) {
            className(order[i], name, sizeof name);
        } else {
            label(&c->first, name, sizeof name);
        }
        put(&o,
            "%s%s %.2f ms: %.1f passes, %.0f draws, %.0f binds, colour %.1f/%.1f MB, depth "
            "%.1f/%.1f MB loaded/stored, %.1f MB cleared, %.2f Mpx",
            i ? "; " : "", name, c->ms / t, c->pieces / t, c->draws / t, c->binds / t,
            c->colorLoadKB / t / 1024.0, c->colorStoreKB / t / 1024.0, c->depthLoadKB / t / 1024.0,
            c->depthStoreKB / t / 1024.0, c->clearKB / t / 1024.0, c->kTexels / t / 1000.0);
        if (c->copyKB > 0.0) {
            put(&o, ", copies %.1f MB", c->copyKB / t / 1024.0);
        }
    }
    put(&o, " (%d of %d classes; all %.2f ms)", shown, count, all / t);
    return o.total;
}

int rd_perf_rank_rates_text(const RdPerfRankBlock *b, const RdPerfRankRates *r, char *buf, size_t n)
{
    Out o = outOf(buf, n);
    put(&o, "perf rates (this device, from the block's own pictures): ");
    if (r->smallFit) {
        put(&o,
            "the DATE snapshots and passes with no draw %.2f ms a pass + %.2f ms a MB loaded or stored (R^2 "
            "%.2f, %u pieces)",
            r->passMs, r->mbMs, r->smallR2, r->smallN);
    } else {
        put(&o,
            "the DATE snapshots and passes with no draw: no fit (%u, or no spread in their bytes)",
            r->smallN);
    }
    if (r->copyRate) {
        put(&o, "; copies %.2f ms a MB (%.1f MB in %.1f ms)", r->copyMbMs, b->copyPieceKB / 1024.0,
            b->copyPieceMs);
    } else {
        put(&o, "; copies under 0.5 MB");
    }
    if (r->vuFit) {
        put(&o,
            "; the GPU's work %.1f ms + %.3f ms a thousand vertex invocations (R^2 %.2f, %u "
            "pictures without keep frames, %.1fk to %.1fk invocations%s)",
            r->vuBaseMs, r->vuPerK, r->vuR2, b->fitN, r->vuXMin, r->vuXMax,
            r->vuUsable ? "" : "; not used for an estimate");
    } else {
        put(&o, "; no vertex fit (%u pictures without keep frames)", b->fitN);
    }
    if (r->mbRate > 0.0) {
        put(&o, "; the estimates use %s (%.2f ms a MB)", r->mbFrom, r->mbRate);
    } else {
        put(&o, "; the estimates use no rate (no fit in this block or an earlier one of the run: "
                "the traffic estimates left out)");
    }
    if (r->copyRate) {
        put(&o, "; the copies' rate is never used for attachment traffic");
    }
    return o.total;
}

int rd_perf_rank_slowest_text(const RdPerfRankBlock *b, RdPerfRankLabel label, unsigned top,
                              char *buf, size_t n)
{
    Out o = outOf(buf, n);
    if (b->slowN == 0) {
        put(&o, "perf slowest picture: none timed pass by pass");
        return o.total;
    }
    uint16_t order[RD_PERF_RANK_SLOW_MAX];
    for (uint32_t i = 0; i < b->slowN; i++) {
        order[i] = (uint16_t)i;
    }
    for (uint32_t i = 1; i < b->slowN; i++) {
        const uint16_t x = order[i];
        uint32_t j = i;
        while (j > 0 && b->slow[order[j - 1]].gpuUs < b->slow[x].gpuUs) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = x;
    }
    put(&o, "perf slowest picture: replay %u (frame %u), GPU span %.1f ms over %u pieces:",
        b->slowReplay, b->slowFrame, b->slowMs, b->slowN);
    const uint32_t shown = b->slowN < top ? b->slowN : top;
    for (uint32_t i = 0; i < shown; i++) {
        const RdPerfPass *p = &b->slow[order[i]];
        char name[64];
        if (label != NULL) {
            label(p, name, sizeof name);
        } else {
            className(rd_perf_rank_class_of(p), name, sizeof name);
        }
        put(&o, "%s %s %ux%u %.2f ms (%u draws, %u binds; loads %.1f MB, stores %.1f MB)",
            i ? "," : "", name, (unsigned)p->w, (unsigned)p->h, (double)p->gpuUs / 1000.0, p->draws,
            p->pipelineBinds, (double)(p->colorLoadKB + p->depthLoadKB) / 1024.0,
            (double)(p->colorStoreKB + p->depthStoreKB) / 1024.0);
    }
    put(&o, " (the top %u by GPU time; GPU time by pipeline is not measured)", shown);
    return o.total;
}

double rd_perf_rank_hist_median(const uint32_t *hist)
{
    uint64_t all = 0;
    for (int i = 0; i < RD_PERF_RANK_BINS; i++) {
        all += hist[i];
    }
    if (all == 0) {
        return 0.0;
    }
    uint64_t at = 0;
    for (int i = 0; i < RD_PERF_RANK_BINS; i++) {
        at += hist[i];
        if (2u * at >= all) {
            return (double)i + 0.5;
        }
    }
    return (double)RD_PERF_RANK_BINS;
}

double rd_perf_rank_hist_worst(const uint32_t *hist)
{
    for (int i = RD_PERF_RANK_BINS - 1; i >= 0; i--) {
        if (hist[i]) {
            return (double)(i + 1);
        }
    }
    return 0.0;
}

void rd_perf_rank_run_add(RdPerfRankRun *run, const RdPerfRankBlock *b, const RdPerfRankInfo *in,
                          const RdPerfRankVerdict *v, double seconds, const char *ranked)
{
    if (b->n == 0) {
        return;
    }
    RdPerfRankScene *s = NULL;
    for (uint32_t i = 0; i < run->count; i++) {
        if (strcmp(run->s[i].name, in->scene) == 0) {
            s = &run->s[i];
            break;
        }
    }
    if (s == NULL) {
        if (run->count >= RD_PERF_RANK_SCENES) {
            run->overflow++;
            return;
        }
        s = &run->s[run->count++];
        memset(s, 0, sizeof(*s));
        snprintf(s->name, sizeof s->name, "%s", in->scene);
        s->lowFps = -1.0;
        s->worstRatio = 2.0;
    }
    s->blocks++;
    s->pictures += b->n;
    s->seconds += seconds;
    s->targetFps = in->targetFps;
    if (v->bound >= 0 && v->bound < RD_PERF_BOUND_COUNT) {
        s->bound[v->bound]++;
    }
    for (int i = 0; i < RD_PERF_RANK_BINS; i++) {
        s->hist[i] += b->hist[i];
    }
    if (s->fpsN < RD_PERF_RANK_FPS_MAX) {
        s->fps[s->fpsN++] = (float)in->presentedFps;
    }
    if (s->lowFps < 0.0 || in->presentedFps < s->lowFps) {
        s->lowFps = in->presentedFps;
    }
    const double ratio = in->targetFps > 0.0 ? in->presentedFps / in->targetFps : 1.0;
    if (v->below && ranked != NULL && ratio < s->worstRatio) {
        s->worstRatio = ratio;
        /* the ranked line past its scene ("perf ranked: <scene>: ") */
        const char *tail = ranked;
        const char *at = strstr(ranked, in->scene);
        if (at != NULL) {
            tail = at + strlen(in->scene);
            tail += strncmp(tail, ": ", 2) == 0 ? 2 : 0;
        }
        snprintf(s->worst, sizeof s->worst, "%s", tail);
    }
}

static int cmpFloat(const void *a, const void *b)
{
    const float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

int rd_perf_rank_profile_text(const RdPerfRankRun *run, uint32_t i, char *buf, size_t n)
{
    if (i >= run->count) {
        if (n > 0) {
            buf[0] = '\0';
        }
        return 0;
    }
    const RdPerfRankScene *s = &run->s[i];
    Out o = outOf(buf, n);
    float fps[RD_PERF_RANK_FPS_MAX];
    memcpy(fps, s->fps, (size_t)s->fpsN * sizeof(fps[0]));
    if (s->fpsN > 1) {
        qsort(fps, s->fpsN, sizeof(fps[0]), cmpFloat);
    }
    const double med = s->fpsN ? (double)fps[s->fpsN / 2] : 0.0;
    put(&o,
        "performance profile: %s: %u blocks (%.0f s, %u pictures); a picture's GPU work median "
        "%.1f ms, worst %.0f ms; presented fps median %.1f, lowest block %.1f, of %.1f; ",
        s->name, s->blocks, s->seconds, s->pictures, rd_perf_rank_hist_median(s->hist),
        rd_perf_rank_hist_worst(s->hist), med, s->lowFps > 0.0 ? s->lowFps : 0.0, s->targetFps);
    bool any = false;
    for (int k = 0; k < RD_PERF_BOUND_COUNT; k++) {
        if (s->bound[k]) {
            put(&o, "%s%s in %u", any ? ", " : "", rd_perf_bound_name(k), s->bound[k]);
            any = true;
        }
    }
    put(&o, " blocks");
    if (s->worst[0] != '\0') {
        put(&o, "; the worst block: %s", s->worst);
    }
    if (i + 1u == run->count && run->overflow) {
        put(&o, "; %u blocks of further scenes not kept", run->overflow);
    }
    return o.total;
}

double rd_perf_rank_partial_fps(const RdPerfRankBlock *b)
{
    const double span = b->lastStartMs - b->firstStartMs;
    if (b->n < 2u || !(span > 0.0) || b->presents == 0u) {
        return 0.0;
    }
    /* n records span n - 1 replay periods */
    const double perRecord = span / (double)(b->n - 1u);
    return 1000.0 * (double)b->presents / ((double)b->n * perRecord);
}

int rd_perf_rank_partial_text(const RdPerfRankBlock *b, const RdPerfRankInfo *in,
                              const RdPerfRankVerdict *v, char *buf, size_t n)
{
    Out o = outOf(buf, n);
    if (b->n == 0) {
        return 0;
    }
    const double span = b->lastStartMs > b->firstStartMs ? b->lastStartMs - b->firstStartMs : 0.0;
    put(&o,
        "performance profile: %s (the block in progress): %.1f s, %u pictures (%u keep frames) "
        "not yet in a 10 s block; a picture's GPU work median %.1f ms, worst %.0f ms; presented "
        "fps about %.1f",
        in->scene, span / 1000.0, b->n, b->keep, rd_perf_rank_hist_median(b->hist),
        rd_perf_rank_hist_worst(b->hist), rd_perf_rank_partial_fps(b));
    const bool known = in->targetFps > 0.0;
    if (known) {
        put(&o, " of %.1f, %s", in->targetFps, rd_perf_bound_name(v->bound));
    } else {
        put(&o, " (the target rate not known before the first 10 s block)");
    }
    const uint32_t top = v->count < RD_PERF_RANK_TOP ? v->count : RD_PERF_RANK_TOP;
    if (v->below || !known) {
        for (uint32_t i = 0; i < top; i++) {
            put(&o, "%s %u. %s", i ? ";" : ":", i + 1u, v->c[i].text);
        }
        if (v->vertexWhy[0] != '\0') {
            put(&o, "; no vertex work estimate (%s)", v->vertexWhy);
        }
    }
    return o.total;
}

bool rd_perf_rank_profile_due(uint64_t *lastNs, uint64_t nowNs)
{
    if (*lastNs != 0 && nowNs >= *lastNs && nowNs - *lastNs < RD_PERF_RANK_PROFILE_GAP_NS) {
        return false;
    }
    *lastNs = nowNs != 0 ? nowNs : 1u;
    return true;
}

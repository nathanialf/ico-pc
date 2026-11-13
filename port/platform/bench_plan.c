/*
 * port/platform/bench_plan.c
 *
 * The rc-only bench's pure parts (bench_plan.h).
 */
#include "bench_plan.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ the picks */

void ico_bench_pick_init(IcoBenchPick *p)
{
    memset(p, 0, sizeof(*p));
    p->runPlan = ICO_BENCH_PLAN_NONE;
    p->armed = -1;
}

static void endRun(IcoBenchPick *p)
{
    p->runPlan = ICO_BENCH_PLAN_NONE;
    p->run = 0;
}

/* the kind a picture of this plan and run meets, -1 none */
static int kindOf(const IcoBenchPick *p, int plan, int withVolume)
{
    if (plan == ICO_BENCH_PLAN_LAMPS && p->run > ICO_BENCH_LAMPS_RUN) {
        return withVolume ? ICO_BENCH_LAMPS_VOLUME : ICO_BENCH_LAMPS_PLAIN;
    }
    if (plan == ICO_BENCH_PLAN_DARK && p->run > ICO_BENCH_DARK_RUN) {
        return ICO_BENCH_DARK;
    }
    if (plan == ICO_BENCH_PLAN_WHOLE && p->run > ICO_BENCH_SUN_RUN) {
        return ICO_BENCH_SUN;
    }
    return -1;
}

int ico_bench_pick_picture(IcoBenchPick *p, int inOpening, int plan, int withVolume, uint32_t frame)
{
    if (!inOpening) {
        endRun(p);
        p->haveFrame = 0;
        p->armed = -1;
        return -1;
    }
    if (plan != ICO_BENCH_PLAN_DARK && plan != ICO_BENCH_PLAN_LAMPS &&
        plan != ICO_BENCH_PLAN_WHOLE) {
        if (!(p->haveFrame && frame == p->lastFrame)) {
            endRun(p); /* a new frame without a plan */
        }
        return -1;
    }
    if (plan == p->runPlan) {
        p->run++;
    } else {
        p->runPlan = plan;
        p->run = 1;
    }
    p->haveFrame = 1;
    p->lastFrame = frame;
    const int kind = kindOf(p, plan, withVolume);
    if (kind < 0 || (p->used & (1u << kind)) != 0) {
        return -1;
    }
    if (p->armed >= 0 && p->armed != kind) {
        return -1; /* one at a time: the pump keeps the armed one first */
    }
    p->armed = kind;
    p->armedFrame = frame;
    return kind;
}

void ico_bench_pick_done(IcoBenchPick *p, int ok)
{
    (void)ok;
    if (p->armed >= 0) {
        p->used |= 1u << p->armed;
    }
    p->armed = -1;
}

const char *ico_bench_kind_name(int kind)
{
    switch (kind) {
    case ICO_BENCH_LAMPS_VOLUME:
        return "a lamps picture with a shadow volume";
    case ICO_BENCH_LAMPS_PLAIN:
        return "a lamps picture without a shadow volume";
    case ICO_BENCH_DARK:
        return "a dark picture";
    case ICO_BENCH_SUN:
        return "a sun picture (the glow's whole chain)";
    case ICO_BENCH_PAUSE:
        return "the paused picture";
    default:
        return "a picture";
    }
}

const char *ico_bench_plan_name(uint32_t glowLight)
{
    static const char *const kNames[] = {"none", "dark", "lamps", "whole", "off"};
    return glowLight < sizeof(kNames) / sizeof(kNames[0]) ? kNames[glowLight] : "?";
}

/* ---------------------------------------------------------- the schedule */

int ico_bench_schedule(int variants, int rounds, int every, IcoBenchBlock *out, int max)
{
    if (variants < 1 || variants > ICO_BENCH_VARIANTS || rounds < 1 || rounds > ICO_BENCH_ROUNDS ||
        every < 1 || !out) {
        return 0;
    }
    const int m = variants - 1;
    int n = 0;
#define PUT(v_, r_)                                                                                \
    do {                                                                                           \
        if (n >= max) {                                                                            \
            return 0;                                                                              \
        }                                                                                          \
        out[n].variant = (uint8_t)(v_);                                                            \
        out[n].round = (uint8_t)(r_);                                                              \
        n++;                                                                                       \
    } while (0)
    for (int r = 0; r < rounds; r++) {
        const int o = m ? (r * m) / rounds : 0;
        PUT(0, r);
        for (int i = 0; i < m; i++) {
            PUT(1 + (o + i) % m, r);
            if ((i + 1) % every == 0 && i + 1 < m) {
                PUT(0, r);
            }
        }
        if (m) {
            PUT(0, r);
        }
    }
#undef PUT
    return n;
}

int ico_bench_k(double pictureMs, int blocks, int w, double budgetMs, int least, int most)
{
    if (!(pictureMs > 0.0) || blocks < 1) {
        return most;
    }
    const double k = floor(budgetMs / (pictureMs * (double)blocks)) - (double)w;
    if (k < (double)least) {
        return least;
    }
    return k > (double)most ? most : (int)k;
}

/* ----------------------------------------------------------- statistics */

static int cmpFloat(const void *a, const void *b)
{
    const float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

double ico_bench_quantile(const float *v, int n, double p)
{
    if (n <= 0 || !v) {
        return 0.0;
    }
    float tmp[ICO_BENCH_ROUNDS * ICO_BENCH_BLOCKS];
    float *s = n <= (int)(sizeof(tmp) / sizeof(tmp[0])) ? tmp : malloc((size_t)n * sizeof(float));
    if (!s) {
        return 0.0;
    }
    memcpy(s, v, (size_t)n * sizeof(float));
    qsort(s, (size_t)n, sizeof(float), cmpFloat);
    const double h = (double)(n - 1) * p;
    const int lo = (int)floor(h);
    double q = s[lo];
    if (lo + 1 < n) {
        q += (h - (double)lo) * ((double)s[lo + 1] - (double)s[lo]);
    }
    if (s != tmp) {
        free(s);
    }
    return q;
}

IcoBenchStat ico_bench_stat(const float *v, int n)
{
    IcoBenchStat st;
    st.n = n;
    st.median = ico_bench_quantile(v, n, 0.5);
    st.q1 = ico_bench_quantile(v, n, 0.25);
    st.q3 = ico_bench_quantile(v, n, 0.75);
    return st;
}

/* a block's kept picture times; their count */
static int blockPics(const IcoBenchRun *run, int b, float *out)
{
    int n = 0;
    for (int j = 0; j < run->ns[b]; j++) {
        if (!run->s[b][j].excluded) {
            out[n++] = run->s[b][j].pictureMs;
        }
    }
    return n;
}

static double blockMedian(const IcoBenchRun *run, int b, int *n)
{
    float v[ICO_BENCH_K_MAX];
    *n = blockPics(run, b, v);
    return ico_bench_quantile(v, *n, 0.5);
}

/* the nearest baseline block of the round from b in direction dir with a
   kept sample, -1 none */
static int bracket(const IcoBenchRun *run, int b, int dir)
{
    for (int i = b + dir; i >= 0 && i < run->nBlocks; i += dir) {
        if (run->block[i].round != run->block[b].round) {
            break;
        }
        if (run->block[i].variant == 0) {
            int n = 0;
            (void)blockMedian(run, i, &n);
            if (n > 0) {
                return i;
            }
        }
    }
    return -1;
}

void ico_bench_results(const IcoBenchRun *run, IcoBenchResult *out, IcoBenchDrift *drift)
{
    static float pic[ICO_BENCH_BLOCKS * ICO_BENCH_K_MAX], gpu[ICO_BENCH_BLOCKS * ICO_BENCH_K_MAX],
        wait[ICO_BENCH_BLOCKS * ICO_BENCH_K_MAX], passes[ICO_BENCH_BLOCKS * ICO_BENCH_K_MAX],
        draws[ICO_BENCH_BLOCKS * ICO_BENCH_K_MAX], tris[ICO_BENCH_BLOCKS * ICO_BENCH_K_MAX];
    for (int v = 0; v < run->nVariants; v++) {
        IcoBenchResult *r = &out[v];
        memset(r, 0, sizeof(*r));
        int np = 0, ng = 0;
        double dsum = 0.0;
        for (int b = 0; b < run->nBlocks; b++) {
            if (run->block[b].variant != v) {
                continue;
            }
            for (int j = 0; j < run->ns[b]; j++) {
                const IcoBenchSample *s = &run->s[b][j];
                if (s->excluded) {
                    continue;
                }
                pic[np] = s->pictureMs;
                wait[np] = s->waitMs;
                passes[np] = (float)s->passes;
                draws[np] = (float)s->draws;
                tris[np] = (float)s->tris;
                np++;
                if (s->gpuValid) {
                    gpu[ng++] = s->gpuMs;
                }
                r->last = *s;
            }
            if (v == 0) {
                continue;
            }
            int n = 0;
            const double med = blockMedian(run, b, &n);
            if (n == 0) {
                continue;
            }
            const int before = bracket(run, b, -1), after = bracket(run, b, 1);
            if (before < 0 && after < 0) {
                continue;
            }
            int nb = 0, na = 0;
            const double mb = before >= 0 ? blockMedian(run, before, &nb) : 0.0;
            const double ma = after >= 0 ? blockMedian(run, after, &na) : 0.0;
            const double base = before >= 0 && after >= 0 ? (mb + ma) / 2.0 : before >= 0 ? mb : ma;
            const double d = med - base;
            if (r->deltaRounds == 0 || d < r->deltaLo) {
                r->deltaLo = d;
            }
            if (r->deltaRounds == 0 || d > r->deltaHi) {
                r->deltaHi = d;
            }
            dsum += d;
            r->deltaRounds++;
        }
        r->ran = np;
        r->pic = ico_bench_stat(pic, np);
        r->gpu = ico_bench_stat(gpu, ng);
        r->wait = ico_bench_stat(wait, np);
        r->passes = ico_bench_quantile(passes, np, 0.5);
        r->draws = ico_bench_quantile(draws, np, 0.5);
        r->tris = ico_bench_quantile(tris, np, 0.5);
        r->delta = r->deltaRounds ? dsum / (double)r->deltaRounds : 0.0;
    }
    if (drift) {
        memset(drift, 0, sizeof(*drift));
        for (int b = 0; b < run->nBlocks; b++) {
            if (run->block[b].variant != 0) {
                continue;
            }
            int n = 0;
            const double m = blockMedian(run, b, &n);
            if (n == 0) {
                continue;
            }
            if (drift->blocks == 0) {
                drift->first = drift->lo = drift->hi = m;
            }
            drift->lo = m < drift->lo ? m : drift->lo;
            drift->hi = m > drift->hi ? m : drift->hi;
            drift->last = m;
            drift->blocks++;
        }
    }
}

int ico_bench_variants_ran(const IcoBenchResult *res, int n)
{
    int k = 0;
    for (int v = 0; v < n; v++) {
        k += res[v].ran > 0;
    }
    return k;
}

/* ---------------------------------------------------------------- lines */

#define PUTS(...)                                                                                  \
    do {                                                                                           \
        const int k_ = at < size ? snprintf(out + at, size - at, __VA_ARGS__) : 0;                 \
        if (k_ > 0) {                                                                              \
            at = at + (size_t)k_ < size ? at + (size_t)k_ : (size > 0 ? size - 1 : 0);             \
        }                                                                                          \
    } while (0)

int ico_bench_head_line(char *out, size_t size, const IcoBenchRun *run, uint32_t commands,
                        uint32_t kb, const char *presentMode, int gpuStamps)
{
    size_t at = 0;
    if (size) {
        out[0] = '\0';
    }
    PUTS("bench: frame %d of %d: %s (frame %u, %u commands, %u KB): %d variants, %d round%s of "
         "%d warm-up and %d timed replays each, a baseline after every %d; present mode %s; GPU "
         "stamps %s",
         run->frame, run->frames, ico_bench_kind_name(run->kind), run->number, commands, kb,
         run->nVariants, run->rounds, run->rounds == 1 ? "" : "s", run->w, run->k, run->every,
         presentMode ? presentMode : "unknown", gpuStamps ? "yes" : "no");
    return (int)at;
}

static void cutsText(char *out, size_t size, const IcoBenchSample *s)
{
    size_t at = 0;
    if (size) {
        out[0] = '\0';
    }
    PUTS("plan %s, no volume %u, composites skipped %u, volumes merged %u, crack fills %u, SCENE "
         "presented %u",
         ico_bench_plan_name(s->plan), s->noVolume, s->emptyComposites, s->merged, s->crackFills,
         s->presentScene);
}

int ico_bench_variant_line(char *out, size_t size, const IcoBenchRun *run, int v,
                           const IcoBenchResult *r)
{
    size_t at = 0;
    char cuts[192];
    if (size) {
        out[0] = '\0';
    }
    cutsText(cuts, sizeof(cuts), &r->last);
    PUTS("bench: frame %d | %s | ", run->frame, run->info[v].name);
    if (r->ran == 0) {
        PUTS("no timed replay kept");
        return (int)at;
    }
    PUTS("%.0f passes, %.0f draws, %.0f triangles | picture %.2f ms (%.2f-%.2f, n %d) | ",
         r->passes, r->draws, r->tris, r->pic.median, r->pic.q1, r->pic.q3, r->pic.n);
    if (r->gpu.n > 0) {
        PUTS("GPU %.2f ms (%.2f-%.2f) | ", r->gpu.median, r->gpu.q1, r->gpu.q3);
    } else {
        PUTS("GPU no stamps | ");
    }
    PUTS("wait %.2f ms | ", r->wait.median);
    if (v == 0) {
        PUTS("the baseline");
    } else if (r->deltaRounds > 0) {
        PUTS("%+.2f ms against the baselines (rounds %+.2f to %+.2f)", r->delta, r->deltaLo,
             r->deltaHi);
    } else {
        PUTS("no baseline beside it");
    }
    PUTS(" | removed %u | cuts: %s", run->info[v].removed, cuts);
    return (int)at;
}

int ico_bench_drift_line(char *out, size_t size, const IcoBenchRun *run, const IcoBenchDrift *d)
{
    size_t at = 0;
    if (size) {
        out[0] = '\0';
    }
    if (d->blocks == 0) {
        PUTS("bench: frame %d: the baseline kept no timed replay", run->frame);
        return (int)at;
    }
    PUTS("bench: frame %d: the baseline over %d blocks: first %.2f ms, last %.2f ms, from %.2f to "
         "%.2f ms (drift %.2f ms)",
         run->frame, d->blocks, d->first, d->last, d->lo, d->hi, d->hi - d->lo);
    return (int)at;
}

int ico_bench_rank(const IcoBenchRun *run, const IcoBenchResult *res, int *order, int max)
{
    int n = 0;
    for (int v = 1; v < run->nVariants && n < max; v++) {
        if (!run->info[v].knock || res[v].deltaRounds == 0) {
            continue;
        }
        /* insertion by saving (-delta), most first; ties by the number */
        int at = n;
        while (at > 0 && -res[order[at - 1]].delta < -res[v].delta) {
            order[at] = order[at - 1];
            at--;
        }
        order[at] = v;
        n++;
    }
    return n;
}

int ico_bench_rank_line(char *out, size_t size, const IcoBenchRun *run, const IcoBenchResult *res,
                        const int *order, int n, int from, int count)
{
    size_t at = 0;
    if (size) {
        out[0] = '\0';
    }
    PUTS("bench: frame %d: what removing each part saves (the picture %.2f ms)%s:", run->frame,
         res[0].pic.median, from > 0 ? ", on" : "");
    for (int i = from; i < n && i < from + count; i++) {
        PUTS("%s %s %.2f ms", i > from ? "," : "", run->info[order[i]].name, -res[order[i]].delta);
    }
    return (int)at;
}

int ico_bench_sum_line(char *out, size_t size, const IcoBenchRun *run, const IcoBenchResult *res)
{
    size_t at = 0;
    double lists = 0.0, parts = 0.0;
    for (int v = 1; v < run->nVariants; v++) {
        if (res[v].deltaRounds == 0) {
            continue;
        }
        if (run->info[v].sum == 1) {
            lists -= res[v].delta;
        } else if (run->info[v].sum == 2) {
            parts -= res[v].delta;
        }
    }
    const double pic = res[0].pic.median;
    const double pl = pic > 0.0 ? 100.0 * lists / pic : 0.0,
                 pp = pic > 0.0 ? 100.0 * parts / pic : 0.0;
    if (size) {
        out[0] = '\0';
    }
    PUTS(
        "bench: frame %d: the lists' savings add up to %.2f ms of the picture's %.2f ms (%.0f %%), "
        "the parts' (the shadow window, the composites, the fog with the crack fill it arms, the "
        "glow's parts, the aura, the other effects, the reduction) to %.2f ms (%.0f %%); what "
        "neither holds is the present, the uploads, the passes' own loads and stores and what a "
        "part enables in a later pass",
        run->frame, lists, pic, pl, parts, pp);
    return (int)at;
}

int ico_bench_csv_header(char *out, size_t size)
{
    return snprintf(out, size,
                    "frame,kind,number,variant,knock,removed,passes,draws,triangles,picture_ms,"
                    "picture_q1,picture_q3,picture_n,gpu_ms,gpu_q1,gpu_q3,gpu_n,wait_ms,delta_ms,"
                    "delta_lo_ms,delta_hi_ms,delta_rounds,drift_ms,plan,no_volume,"
                    "empty_composites,merged,crack_fills,present_scene,excluded");
}

int ico_bench_csv_row(char *out, size_t size, const IcoBenchRun *run, int v,
                      const IcoBenchResult *r, const IcoBenchDrift *d)
{
    size_t at = 0;
    if (size) {
        out[0] = '\0';
    }
    PUTS("%d,%d,%u,\"%s\",%u,%u,%.0f,%.0f,%.0f,%.3f,%.3f,%.3f,%d,%.3f,%.3f,%.3f,%d,%.3f,%.3f,%.3f,"
         "%.3f,%d,%.3f,%s,%u,%u,%u,%u,%u,%u",
         run->frame, run->kind, run->number, run->info[v].name, run->info[v].knock,
         run->info[v].removed, r->passes, r->draws, r->tris, r->pic.median, r->pic.q1, r->pic.q3,
         r->pic.n, r->gpu.median, r->gpu.q1, r->gpu.q3, r->gpu.n, r->wait.median, r->delta,
         r->deltaLo, r->deltaHi, r->deltaRounds, d->blocks ? d->hi - d->lo : 0.0,
         ico_bench_plan_name(r->last.plan), r->last.noVolume, r->last.emptyComposites,
         r->last.merged, r->last.crackFills, r->last.presentScene, run->excluded);
    return (int)at;
}

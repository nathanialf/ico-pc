/*
 * port/render/rd_perf_where.c
 *
 * Where the time went, one table a block (rd_perf_where.h): pure.
 */
#include "rd_perf_where.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "rd_ratecard.h" /* RdRateCard: the types alone, nothing of it is called */

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

/* ------------------------------------------- the rate card's clause */

static void rateOf(const RdRcItem *it, const char *unit, RdPerfWhereRate *o)
{
    memset(o, 0, sizeof(*o));
    if (!it->u.ok) {
        snprintf(o->why, sizeof o->why, "not measured: %.120s",
                 it->u.why[0] ? it->u.why : "no reason given");
    } else if (strcmp(it->unit, unit) != 0) {
        snprintf(o->why, sizeof o->why, "the card's unit is \"%.3s\", %s wanted", it->unit, unit);
    } else if (!(it->u.med > 0.0) || !isfinite(it->u.med)) {
        snprintf(o->why, sizeof o->why, "the card's figure %.3g %s is not above 0", it->u.med,
                 unit);
    } else {
        o->ok = true;
        o->v = it->u.med;
    }
}

void rd_perf_where_rates_of(const struct RdRateCard *c, const char *backend, RdPerfWhereRates *out)
{
    memset(out, 0, sizeof(*out));
    out->prepass = -1;
    if (c == NULL || !c->have) {
        snprintf(out->none, sizeof out->none, "no rate card this run: its start lines say why");
        return;
    }
    if (backend != NULL && backend[0] != '\0') {
        char want[48];
        snprintf(want, sizeof want, ", %s)", backend);
        if (strstr(c->device, want) == NULL) {
            snprintf(out->none, sizeof out->none,
                     "the card is not of this block's graphics API, %.40s", backend);
            return;
        }
    }
    out->have = true;
    out->tw = c->tw;
    out->th = c->th;
    rateOf(&c->item[RD_RC_VERTEX], "ns", &out->vertex);
    rateOf(&c->item[RD_RC_TRIANGLE], "ns", &out->triangle);
    rateOf(&c->item[RD_RC_PASS], "ms", &out->pass);
    const char *lb = c->item[RD_RC_VERTEX].label;
    out->prepass = strstr(lb, "pre-pass on") != NULL    ? 1
                   : strstr(lb, "pre-pass off") != NULL ? 0
                                                        : -1;
}

bool rd_perf_where_explained(const RdPerfRankBlock *b, const RdPerfRankInfo *in,
                             const RdPerfWhereRates *r, int list, RdPerfWhereExplained *out)
{
    memset(out, 0, sizeof(*out));
    if (b == NULL || r == NULL || !r->have || list < 0 || list >= RD_LIST_COUNT) {
        return false;
    }
    const double n = b->n ? (double)b->n : 1.0;
    out->ms = per(b->listMs[list], (double)b->gpuN);
    out->tris = b->listTris[list] / n;
    out->again = b->listAgainTris[list] / n;
    out->shadow = b->listShadowTris[list] / n;
    if (!(out->tris + out->again + out->shadow > 0.0)) {
        return false;
    }
    out->corners = 3.0 * (out->tris + out->again);
    out->vertexMs = out->triMs = out->passMs = -1.0;
    const bool prepass = in != NULL && in->prepass;
    if (!r->vertex.ok) {
        snprintf(out->vertexWhy, sizeof out->vertexWhy, "%s", r->vertex.why);
    } else if (r->prepass >= 0 && (r->prepass == 1) != prepass) {
        snprintf(out->vertexWhy, sizeof out->vertexWhy,
                 "the card timed a vertex with the VU pre-pass %s, the block drew with it %s",
                 r->prepass ? "on" : "off", prepass ? "on" : "off");
    } else {
        out->vertexMs = out->corners * r->vertex.v / 1.0e6;
        out->vertexAtMost = prepass;
    }
    if (r->triangle.ok) {
        out->triMs = (out->tris + out->again + out->shadow) * r->triangle.v / 1.0e6;
    } else {
        snprintf(out->triWhy, sizeof out->triWhy, "%s", r->triangle.why);
    }
    const RdPerfRankClass *c = &b->cls[RD_PERF_RANK_LIST0 + list];
    if (!r->pass.ok) {
        snprintf(out->passWhy, sizeof out->passWhy, "%s", r->pass.why);
    } else if (b->timed == 0 || !c->seen) {
        snprintf(out->passWhy, sizeof out->passWhy, "the list not timed pass by pass");
    } else if (c->first.target != RD_TARGET_SCENE || c->first.w != r->tw || c->first.h != r->th) {
        snprintf(out->passWhy, sizeof out->passWhy,
                 "the card's empty pass is SCENE at %ux%u texels, the list's first pass %s at "
                 "%ux%u",
                 (unsigned)r->tw, (unsigned)r->th,
                 c->first.target == RD_TARGET_SCENE ? "SCENE" : "another target",
                 (unsigned)c->first.w, (unsigned)c->first.h);
    } else {
        out->passes = c->pieces / (double)b->timed;
        out->passMs = out->passes * r->pass.v;
    }
    out->sum = -1.0;
    const double t[3] = {out->vertexMs, out->triMs, out->passMs};
    for (int i = 0; i < 3; i++) {
        if (t[i] >= 0.0) {
            out->sum = (out->sum < 0.0 ? 0.0 : out->sum) + t[i];
        }
    }
    return true;
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
    double again = 0.0, shadow = 0.0;
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        again += b->listAgainTris[l];
        shadow += b->listShadowTris[l];
    }
    put(o, ", %.1fk VU triangles drawn again by a later pass, %.1fk shadow volume triangles",
        again / n / 1000.0, shadow / n / 1000.0);
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

static void largest_line(Out *o, const RdPerfRankBlock *b, const RdPerfRankVerdict *v)
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
}

/* "vertices at most 30.4 ms: 257.1k corners x 118.29 ns" or "vertices: no
   rate card figure (why)" */
static void term(Out *o, const char *name, bool atMost, double ms, const char *why)
{
    if (ms >= 0.0) {
        put(o, "%s %s%.1f ms: ", name, atMost ? "at most " : "", ms);
    } else {
        put(o, "%s: no rate card figure (%s)", name, why);
    }
}

static void explained_line(Out *o, const RdPerfRankBlock *b, const RdPerfRankInfo *in,
                           const RdPerfWhereExtra *x)
{
    const RdPerfWhereRates *r = x != NULL ? x->rates : NULL;
    put(o, "where: explained by the rate card (vertices and triangles; fragments not counted):");
    if (r == NULL || !r->have) {
        put(o, " no rate card figure (%s)", r != NULL ? r->none : "no rate card given");
        return;
    }
    if (b->gpuN == 0) {
        put(o, " no picture timed");
        return;
    }
    const char *sep = " ";
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        RdPerfWhereExplained e;
        if (!rd_perf_where_explained(b, in, r, l, &e)) {
            continue;
        }
        if (e.sum >= 0.0) {
            put(o, "%slist %d %.1f of %.1f ms (", sep, l, e.sum, e.ms);
        } else {
            put(o, "%slist %d no rate card figure of its %.1f ms (", sep, l, e.ms);
        }
        sep = "; ";
        term(o, "vertices", e.vertexAtMost, e.vertexMs, e.vertexWhy);
        if (e.vertexMs >= 0.0) {
            put(o, "%.1fk corners x %.2f ns", e.corners / 1000.0, r->vertex.v);
        }
        put(o, "; ");
        term(o, "triangles", true, e.triMs, e.triWhy);
        if (e.triMs >= 0.0) {
            put(o, "%.1fk x %.2f ns", (e.tris + e.again + e.shadow) / 1000.0, r->triangle.v);
        }
        put(o, ", of them %.1fk drawn again by a later pass, %.1fk the shadow volumes'; ",
            e.again / 1000.0, e.shadow / 1000.0);
        term(o, "passes", false, e.passMs, e.passWhy);
        if (e.passMs >= 0.0) {
            put(o, "%.1f x %.3f ms", e.passes, r->pass.v);
        }
        put(o, ")");
    }
    if (sep[0] == ' ') {
        put(o, " no VU or shadow volume triangles drawn");
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
        largest_line(&o, b, v);
        break;
    case 6:
        explained_line(&o, b, in, x);
        break;
    default:
        return 0;
    }
    return o.total;
}

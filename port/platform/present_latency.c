/*
 * port/platform/present_latency.c
 *
 * The present's latency in a 10 s block (present_latency.h): pure.
 */
#include "present_latency.h"

#include <stdarg.h>
#include <stdio.h>

void present_latency_add(PresentLatency *p, const PresentLatencyRec *r)
{
    if (!(r->endMs > 0.0) || !(r->fromMs > 0.0) || r->endMs < r->fromMs) {
        return;
    }
    const double ms = r->endMs - r->fromMs;
    p->n++;
    p->handed += r->handed ? 1u : 0u;
    p->sum += ms;
    if (ms > p->maxMs) {
        p->maxMs = ms;
        p->maxReplay = r->replay;
    }
    const double before = ms > r->presentMs ? ms - r->presentMs : 0.0;
    p->before += before;
    p->fence += r->fenceMs;
    p->acquire += r->acquireMs;
    p->present += ms - before;
    p->call += r->callMs;
    if (r->gpuEndMs > 0.0 && r->callMs > 0.0) {
        /* the call taken as the present's last part (its end the present's,
           the surface poll after it under a millisecond): the part of it
           before the picture's GPU end, and the rest */
        const double callStart = r->endMs - r->callMs;
        double gpu = r->gpuEndMs - callStart;
        gpu = gpu < 0.0 ? 0.0 : gpu > r->callMs ? r->callMs : gpu;
        p->split++;
        p->callGpu += gpu;
        p->callAfter += r->callMs - gpu;
    }
}

typedef struct Out {
    char *out;
    size_t cap, at;
} Out;

static void put(Out *o, const char *fmt, ...)
{
    if (o->cap == 0) {
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

size_t present_latency_text(const PresentLatency *p, char *out, size_t cap)
{
    Out o = {out, cap, 0};
    if (cap > 0) {
        out[0] = '\0';
    }
    if (p->n == 0) {
        return 0;
    }
    const double n = (double)p->n;
    put(&o, "present latency in the block: %u presents (%u from the hand-over", p->n, p->handed);
    if (p->handed < p->n) {
        put(&o, ", %u from the replay's start", p->n - p->handed);
    }
    put(&o,
        "), mean %.1f ms, longest %.1f ms (replay %u); of the mean: before the present %.1f ms "
        "(the slot's wait for the GPU %.1f ms, the acquire %.1f ms), the present %.1f ms, the "
        "driver's present call %.1f ms of it",
        p->sum / n, p->maxMs, p->maxReplay, p->before / n, p->fence / n, p->acquire / n,
        p->present / n, p->call / n);
    if (p->split > 0) {
        const double s = (double)p->split;
        put(&o,
            "; over %u presents with the picture's GPU end seen, the call %.1f ms while the "
            "picture's GPU work still ran and %.1f ms after its end (the display's)",
            p->split, p->callGpu / s, p->callAfter / s);
    } else {
        put(&o, "; the picture's GPU end not seen exactly (its slot's wait did not block): the "
                "call not split");
    }
    return o.at;
}

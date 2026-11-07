/*
 * port/platform/window_scene.c
 *
 * The window's scene lines (window_scene.h).
 */
#include "window_scene.h"
#include <stdio.h>

static const char *const kKindWords[SCENE_KIND_COUNT] = {"play",  "scene",    "title",
                                                         "pause", "settings", "other"};

SceneKind window_scene_kind(const SceneIds *ids, int layout, int movie)
{
    if (movie || layout == ids->scene) {
        return SCENE_SCENE;
    }
    if (layout == ids->play) {
        return SCENE_PLAY;
    }
    if (layout == ids->titleContinue || layout == ids->titleNew) {
        return SCENE_TITLE;
    }
    if (layout == ids->pause) {
        return SCENE_PAUSE;
    }
    if (layout == ids->options || layout == ids->keyConfig) {
        return SCENE_SETTINGS;
    }
    return SCENE_OTHER;
}

void window_scene_note(SceneShare *s, SceneKind kind, uint64_t ns)
{
    if ((unsigned)kind < SCENE_KIND_COUNT) {
        s->ns[kind] += ns;
    }
}

size_t window_scene_text(const SceneShare *s, char *out, size_t cap)
{
    size_t at = 0;
    int any = 0;

    if (cap == 0) {
        return 0;
    }
    at += (size_t)snprintf(out, cap, "layout:");
    for (int k = 0; k < SCENE_KIND_COUNT && at < cap; k++) {
        if (s->ns[k] == 0) {
            continue;
        }
        at += (size_t)snprintf(out + at, cap - at, "%s %s %.1f s", any ? "," : "", kKindWords[k],
                               (double)s->ns[k] / 1e9);
        any = 1;
    }
    if (!any && at < cap) {
        at += (size_t)snprintf(out + at, cap - at, " no steps");
    }
    return at < cap ? at : cap - 1;
}

void window_scene_reset(SceneShare *s)
{
    for (int k = 0; k < SCENE_KIND_COUNT; k++) {
        s->ns[k] = 0;
    }
}

size_t window_scene_threads_text(char *out, size_t cap, double threadsMs, unsigned long switches,
                                 double longestMs, const char *name, int core)
{
    size_t at;

    if (cap == 0) {
        return 0;
    }
    at = (size_t)snprintf(out, cap, "game threads %.1f in %lu switches", threadsMs, switches);
    if (longestMs > 0.0 && at < cap) {
        at += (size_t)snprintf(out + at, cap - at, " (longest %s %.1f ms",
                               name != NULL && name[0] != '\0' ? name : "?", longestMs);
        if (core >= 0 && at < cap) {
            at += (size_t)snprintf(out + at, cap - at, "; on core %d", core);
        }
        if (at < cap) {
            at += (size_t)snprintf(out + at, cap - at, ")");
        }
    } else if (core >= 0 && at < cap) {
        at += (size_t)snprintf(out + at, cap - at, " (on core %d)", core);
    }
    return at < cap ? at : cap - 1;
}

/* 1182, 61k, 4.1k, 550k, 1.2M: three significant digits */
static void count_words(double v, char *out, size_t cap)
{
    if (v >= 1e6) {
        snprintf(out, cap, "%.1fM", v / 1e6);
    } else if (v >= 1e4) {
        snprintf(out, cap, "%.0fk", v / 1e3);
    } else if (v >= 1e3) {
        snprintf(out, cap, "%.1fk", v / 1e3);
    } else {
        snprintf(out, cap, "%.0f", v);
    }
}

uint64_t window_scene_vu_invocations(uint64_t tris, uint64_t preVertices, int prepass)
{
    return prepass ? preVertices : tris * 9u;
}

double window_scene_gpu_work_ms(double spanMs, double waitMs, double presentMs)
{
    const double w = spanMs - waitMs - presentMs;

    return w > 0.0 ? w : 0.0;
}

size_t window_scene_vu_text(char *out, size_t cap, uint64_t draws, uint64_t tris,
                            uint64_t preVertices, unsigned n, int prepass)
{
    const double d = n ? (double)n : 1.0;
    const double inv = (double)window_scene_vu_invocations(tris, preVertices, prepass) / d;
    char t[24], v[24];
    int w;

    if (cap == 0) {
        return 0;
    }
    count_words((double)tris / d, t, sizeof(t));
    count_words(inv, v, sizeof(v));
    w = snprintf(out, cap, "VU: %.0f draws, %s triangles, %s vertex invocations (%s)",
                 (double)draws / d, t, v,
                 prepass ? "pre-pass: each vertex once" : "pre-pass off: 9 a triangle");
    if (w < 0) {
        return 0;
    }
    return (size_t)w < cap ? (size_t)w : cap - 1;
}

size_t window_scene_twins_text(char *out, size_t cap, uint64_t draws, uint64_t invocations,
                               unsigned n)
{
    const double d = n ? (double)n : 1.0;
    char v[24];
    int w;

    if (cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (draws == 0) {
        return 0;
    }
    count_words((double)invocations / d, v, sizeof(v));
    w = snprintf(out, cap, "; twins %.0f draws, %s vertex invocations a picture", (double)draws / d,
                 v);
    if (w < 0) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)w < cap ? (size_t)w : cap - 1;
}

void window_scene_vu_fit_add(SceneVuFit *f, uint64_t invocations, double workMs)
{
    const double x = (double)invocations / 1000.0;
    const double list0Ms = workMs;

    f->n++;
    f->sx += x;
    f->sy += list0Ms;
    f->sxx += x * x;
    f->sxy += x * list0Ms;
    f->syy += list0Ms * list0Ms;
}

bool window_scene_vu_fit(const SceneVuFit *f, double *a, double *b, double *r2)
{
    const double n = (double)f->n;
    double vx, vy, cxy;

    if (f->n < 3) {
        return false;
    }
    vx = f->sxx - f->sx * f->sx / n;
    vy = f->syy - f->sy * f->sy / n;
    cxy = f->sxy - f->sx * f->sy / n;
    /* the invocations' spread against their size: a count that did not vary
       (or by rounding only) has no slope */
    if (!(vx > 1e-9 * (f->sxx > 1.0 ? f->sxx : 1.0))) {
        return false;
    }
    *b = cxy / vx;
    *a = (f->sy - *b * f->sx) / n;
    *r2 = vy > 1e-12 ? cxy * cxy / (vx * vy) : -1.0;
    if (*r2 > 1.0) {
        *r2 = 1.0;
    }
    return true;
}

size_t window_scene_vu_fit_text(char *out, size_t cap, const SceneVuFit *f)
{
    double a, b, r2;
    int w;

    if (cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (f->n == 0) {
        return 0;
    }
    if (!window_scene_vu_fit(f, &a, &b, &r2)) {
        w = snprintf(out, cap, "; GPU work %.1f ms a replay (%u replays)", f->sy / f->n, f->n);
    } else if (r2 < 0.0) {
        w = snprintf(
            out, cap,
            "; GPU work %.1f ms a replay, %.1f ms %s %.2f ms a thousand vertex invocations "
            "(%u replays)",
            f->sy / f->n, a, b < 0.0 ? "-" : "+", b < 0.0 ? -b : b, f->n);
    } else {
        w = snprintf(
            out, cap,
            "; GPU work %.1f ms a replay, %.1f ms %s %.2f ms a thousand vertex invocations "
            "(R^2 %.2f, %u replays)",
            f->sy / f->n, a, b < 0.0 ? "-" : "+", b < 0.0 ? -b : b, r2, f->n);
    }
    if (w < 0) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)w < cap ? (size_t)w : cap - 1;
}

size_t window_scene_images_text(char *out, size_t cap, uint64_t images, uint64_t inUse)
{
    int w;

    if (cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (images == 0) {
        return 0;
    }
    w = snprintf(out, cap, " (images: %llu, in use %llu)", (unsigned long long)images,
                 (unsigned long long)inUse);
    if (w < 0) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)w < cap ? (size_t)w : cap - 1;
}

size_t window_scene_acquire_text(char *out, size_t cap, uint64_t ready, uint64_t notReady)
{
    int w;

    if (cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (ready + notReady == 0) {
        return 0;
    }
    w = snprintf(out, cap,
                 "acquire: the display's image was free at the present's submit in %llu of %llu "
                 "pictures",
                 (unsigned long long)ready, (unsigned long long)(ready + notReady));
    if (w < 0) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)w < cap ? (size_t)w : cap - 1;
}

SceneKind window_scene_dominant(const SceneShare *s)
{
    SceneKind best = SCENE_OTHER;
    uint64_t most = 0;

    for (int k = 0; k < SCENE_KIND_COUNT; k++) {
        if (s->ns[k] > most) {
            most = s->ns[k];
            best = (SceneKind)k;
        }
    }
    return best;
}

double window_slow_scene_work_ms(const SlowSceneBlock *b)
{
    if (b->gpuN == 0) {
        return 0.0;
    }
    const double w = (b->spanSumMs - b->waitSumMs - b->presentSumMs) / (double)b->gpuN;

    return w > 0.0 ? w : 0.0;
}

bool window_slow_scene_due(const SlowSceneBlock *b)
{
    return b->boundMs > 0.0 && b->window && !b->movie && !b->written &&
           b->dominant == SCENE_SCENE && b->uptimeS >= SLOW_SCENE_UPTIME_MIN_S &&
           b->gpuN >= SLOW_SCENE_GPU_MIN && window_slow_scene_work_ms(b) > b->boundMs;
}

size_t window_slow_scene_text(char *out, size_t cap, const SlowSceneBlock *b, unsigned frame,
                              const char *zipName)
{
    if (cap == 0) {
        return 0;
    }
    const double n = b->gpuN ? (double)b->gpuN : 1.0;
    const int c = snprintf(out, cap,
                           "slow scene: the block's GPU work %.1f ms a replay (bound %.0f; display "
                           "wait %.1f, present %.1f, span %.1f) in a scene layout: frame %u "
                           "written to logs/%s; attach it with the log",
                           window_slow_scene_work_ms(b), b->boundMs, b->waitSumMs / n,
                           b->presentSumMs / n, b->spanSumMs / n, frame, zipName);

    return c < 0 ? 0 : ((size_t)c < cap ? (size_t)c : cap - 1);
}

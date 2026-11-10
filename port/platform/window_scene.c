/*
 * port/platform/window_scene.c
 *
 * The window's scene lines (window_scene.h).
 */
#include "window_scene.h"
#include <limits.h>
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

    /* the smallest x above 0: a replay with no VU work (a keep frame, a
       menu) would make the spread guard below pass for any block */
    if (x > 0.0 && (f->xMin == 0.0 || x < f->xMin)) {
        f->xMin = x;
    }
    if (f->n == 0 || x > f->xMax) {
        f->xMax = x;
    }
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
    if (!(f->xMin > 0.0) || !(f->xMax >= 2.0 * f->xMin)) {
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
        w = f->n < 3
                ? snprintf(out, cap, "; GPU work %.1f ms a replay (%u replays)", f->sy / f->n, f->n)
                : snprintf(out, cap,
                           "; GPU work %.1f ms a replay (%u replays); fit: not enough spread "
                           "(x %.1f..%.1f thousand vertex invocations)",
                           f->sy / f->n, f->n, f->xMin, f->xMax);
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

/* the Resolution row's whole scales (port/ui/settings.c kResScales) */
static const int kHintScales[] = {1, 2, 3, 4, 6, 8, 12, 16};

unsigned window_scene_hint_skips(unsigned late, unsigned busy)
{
    return busy > UINT_MAX - late ? UINT_MAX : late + busy;
}

size_t window_scene_gpu_hint_text(char *out, size_t cap, double presentedFps, double gameFps,
                                  unsigned skippedLate, double gpuMs, double scale,
                                  const SceneVuFit *fit)
{
    double a, b, r2, v = 0.0, p, period, budget;
    int k = 0, w, row = 0;

    if (cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (!(gameFps > 0.0) || !(presentedFps < gameFps) || skippedLate == 0 || !(scale > 0.0)) {
        return 0;
    }
    period = 1000.0 / gameFps;
    if (!(gpuMs > period)) {
        return 0;
    }
    budget = 0.9 * period;
    if (fit == NULL || fit->n < 3 || !window_scene_vu_fit(fit, &a, &b, &r2)) {
        /* no line to split the time by (fewer than 3 timed replays, or
           the invocations not spread enough: window_scene_vu_fit): a
           vertex-bound block would be read as pixels and a lower step
           named that does not help, so no step is named */
        w = snprintf(out, cap,
                     "; GPU-bound at %.4gx (the GPU takes %.1f ms a picture, the step %.1f ms at "
                     "%.1f game fps): no resolution hint (fit: %s)",
                     scale, gpuMs, period, gameFps,
                     fit == NULL || fit->n < 3 ? "under 3 timed replays" : "not enough spread");
        if (w < 0) {
            out[0] = '\0';
            return 0;
        }
        return (size_t)w < cap ? (size_t)w : cap - 1;
    }
    if (b > 0.0) {
        v = b * (fit->sx / fit->n);
        v = v < gpuMs ? v : gpuMs;
    }
    p = gpuMs - v;
    for (size_t i = 0; i < sizeof kHintScales / sizeof kHintScales[0]; i++) {
        const double kk = (double)kHintScales[i];
        /* the scale is a Resolution row's (else Window, Auto or an
           upscaler's render scale: no row to name) */
        row |= scale > kk - 1e-3 && scale < kk + 1e-3;
        if (kk < scale - 1e-6 && v + p * (kk / scale) * (kk / scale) <= budget) {
            k = kHintScales[i];
        }
    }
    if (scale <= 1.0 + 1e-6) {
        w = snprintf(out, cap,
                     "; the GPU takes %.1f ms a picture at this resolution (%.4gx): at 1x "
                     "already, no lower resolution step is offered (%.1f ms of vertex work by the "
                     "fit + %.1f ms that scales with the pixels, against %.1f ms, 0.9 of the %.1f "
                     "ms step at %.1f game fps)",
                     gpuMs, scale, v, p, budget, period, gameFps);
    } else if (k == 0) {
        w = snprintf(out, cap,
                     "; the GPU takes %.1f ms a picture at this resolution (%.4gx): no lower "
                     "resolution step would fit the game's rate (%.1f ms of vertex work by the fit "
                     "+ %.1f ms that scales with the pixels: %.1f + %.1f x (1/%.4g)^2 = %.1f ms at "
                     "1x against %.1f ms, 0.9 of the %.1f ms step at %.1f game fps)",
                     gpuMs, scale, v, p, v, p, scale, v + p / (scale * scale), budget, period,
                     gameFps);
    } else if (!row) {
        w = snprintf(
            out, cap,
            "; the GPU takes %.1f ms a picture at this render scale (%.4gx, not a "
            "Resolution row's: Window, Auto or an upscaler): a lower Resolution (or "
            "Upscaling quality) would fit the game's rate at a render scale of %dx or "
            "under (%.1f ms = %.1f ms of vertex work by the fit + %.1f ms that scales with "
            "the pixels: %.1f + %.1f x (%d/%.4g)^2 = %.1f ms, under %.1f ms, 0.9 of the "
            "%.1f ms step at %.1f game fps; a hint: nothing changes by itself)",
            gpuMs, scale, k, gpuMs, v, p, v, p, k, scale,
            v + p * ((double)k / scale) * ((double)k / scale), budget, period, gameFps);
    } else {
        w = snprintf(out, cap,
                     "; the GPU takes %.1f ms a picture at this resolution (%.4gx): %dx would fit "
                     "the game's rate (%.1f ms = %.1f ms of vertex work by the fit + %.1f ms that "
                     "scales with the pixels: %.1f + %.1f x (%d/%.4g)^2 = %.1f ms, under %.1f ms, "
                     "0.9 of the %.1f ms step at %.1f game fps; a hint: nothing changes by itself)",
                     gpuMs, scale, k, gpuMs, v, p, v, p, k, scale,
                     v + p * ((double)k / scale) * ((double)k / scale), budget, period, gameFps);
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

const char *window_scene_tiler_words(int tiler)
{
    return tiler ? " (tile-based GPU: the last scene pass carries the earlier passes' tile work; "
                   "the present's own list alone waits for the display's image: the display "
                   "wait, or the present where the GPU holds only its colour writes): the "
                   "stretch before list 0 is the uploads and the GPU's work still in flight"
                 : "";
}

size_t window_scene_prepass_gpu_text(char *out, size_t cap, int on, double msPerReplay)
{
    int w;

    if (cap == 0) {
        return 0;
    }
    w = on ? snprintf(out, cap, "%.2f", msPerReplay) : snprintf(out, cap, "off");
    if (w < 0) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)w < cap ? (size_t)w : cap - 1;
}

uint64_t window_scene_counter_delta(uint64_t now, uint64_t snapshot)
{
    return now >= snapshot ? now - snapshot : now;
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
    if (!(b->boundMs > 0.0 && b->window && !b->movie && b->dominant == SCENE_SCENE &&
          b->uptimeS >= SLOW_SCENE_UPTIME_MIN_S && b->gpuN >= SLOW_SCENE_GPU_MIN)) {
        return false;
    }
    const double work = window_slow_scene_work_ms(b);

    if (!(work > b->boundMs)) {
        return false;
    }
    return b->count == 0 ||
           (b->count < SLOW_SCENE_DUMPS &&
            (window_slow_scene_new_stage(b) || work >= SLOW_SCENE_STEP * b->lastWorkMs));
}

bool window_slow_scene_new_stage(const SlowSceneBlock *b)
{
    return b->count > 0 && b->stage != b->lastStage;
}

bool window_slow_scene_pick(double work, double meanWork)
{
    return work > 0.0 && meanWork > 0.0 && work >= meanWork;
}

size_t window_slow_scene_text(char *out, size_t cap, const SlowSceneBlock *b, unsigned frame,
                              double frameWork, double meanWork, const char *zipName)
{
    if (cap == 0) {
        return 0;
    }
    const double n = b->gpuN ? (double)b->gpuN : 1.0;
    const double mean = b->spanSumMs / n;
    char which[160], layout[96] = "";

    if (window_slow_scene_new_stage(b)) {
        snprintf(layout, sizeof(layout), " (the first slow block of stage %d)", b->stage);
    }
    snprintf(which, sizeof(which),
             "its vertex work %.1f thousand against the block's mean %.1f thousand",
             frameWork / 1000.0, meanWork / 1000.0);
    const int c =
        snprintf(out, cap,
                 "slow scene: the block's GPU work %.1f ms a replay (bound %.0f; display "
                 "wait %.1f, present %.1f, span %.1f) in a scene layout%s: frame %u (%s) "
                 "written to logs/%s (dump %d of %d this run); attach it with the log",
                 window_slow_scene_work_ms(b), b->boundMs, b->waitSumMs / n, b->presentSumMs / n,
                 mean, layout, frame, which, zipName, b->count + 1, SLOW_SCENE_DUMPS);

    return c < 0 ? 0 : ((size_t)c < cap ? (size_t)c : cap - 1);
}

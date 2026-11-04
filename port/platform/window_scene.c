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

size_t window_scene_vu_text(char *out, size_t cap, uint64_t draws, uint64_t tris,
                            uint64_t preVertices, unsigned n, int prepass)
{
    const double d = n ? (double)n : 1.0;
    const double inv = prepass ? (double)preVertices / d : (double)tris * 9.0 / d;
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

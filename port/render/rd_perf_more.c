/* rd_perf_more.c: the cost counters' block sums and lines (rd_perf_more.h).
 * Pure: no renderer state. */
#include "rd_perf_more.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

int rd_perf_break_of(int kind, int target, int post)
{
    if (kind == RD_PERF_PASS_DATE) {
        return RD_BREAK_DATE;
    }
    if (kind == RD_PERF_PASS_STENCIL || target == RD_TARGET_SHADOW0 ||
        target == RD_TARGET_SHADOW1 || target == RD_TARGET_SHADOW2 ||
        target == RD_PERF_TARGET_SHADOW_COUNT) {
        return RD_BREAK_SHADOW;
    }
    if (kind == RD_PERF_PASS_PRESENT) {
        return RD_BREAK_PRESENT;
    }
    switch (post) {
    case RD_PERF_POST_AURA:
        return RD_BREAK_AURA;
    case RD_PERF_POST_REDUCTION:
        return RD_BREAK_REDUCTION;
    case RD_PERF_POST_CRACK:
        return RD_BREAK_CRACK;
    case RD_PERF_POST_GLOW:
        return RD_BREAK_GLOW;
    default:
        break;
    }
    if (kind == RD_PERF_PASS_COPY || kind == RD_PERF_PASS_SNAP || kind == RD_PERF_PASS_FOG_COPY ||
        kind == RD_PERF_PASS_EXACT || kind == RD_PERF_PASS_MSAA) {
        return RD_BREAK_COPY;
    }
    if (target == RD_TARGET_WORK0 || target == RD_TARGET_WORK1 || target == RD_TARGET_WORK2 ||
        target == RD_TARGET_WORK3 || target == RD_TARGET_WORK2_PAD) {
        return RD_BREAK_LAMPS;
    }
    return RD_BREAK_OTHER;
}

const char *rd_perf_break_word(int why)
{
    static const char *const k[RD_BREAK_COUNT] = {"DATE",       "shadow", "aura",  "reduction",
                                                  "crack fill", "glow",   "lamps", "copies",
                                                  "present",    "again",  "other"};
    return why >= 0 && why < RD_BREAK_COUNT ? k[why] : "other";
}

double rd_perf_box_area(const float (*m)[4], const float *lo, const float *hi, bool scissor,
                        double x0, double y0, double x1, double y1)
{
    const double whole = (x1 - x0) * (y1 - y0);
    double minX = 0.0, maxX = 0.0, minY = 0.0, maxY = 0.0;
    const int corners = scissor ? 16 : 8;
    for (int c = 0; c < corners; c++) {
        const double p[4] = {(c & 1) ? hi[0] : lo[0], (c & 2) ? hi[1] : lo[1],
                             (c & 4) ? hi[2] : lo[2], scissor ? ((c & 8) ? hi[3] : lo[3]) : 1.0};
        double h[4];
        for (int k = 0; k < 4; k++) {
            h[k] = m[0][k] * p[0] + m[1][k] * p[1] + m[2][k] * p[2] + m[3][k] * p[3];
        }
        if (!isfinite(h[0]) || !isfinite(h[1]) || !isfinite(h[3]) || !(h[3] > 0.0)) {
            return whole > 0.0 ? whole : 0.0; /* at or behind the eye: unbounded on screen */
        }
        const double sx = h[0] / h[3], sy = h[1] / h[3];
        if (c == 0) {
            minX = maxX = sx;
            minY = maxY = sy;
        } else {
            minX = sx < minX ? sx : minX;
            maxX = sx > maxX ? sx : maxX;
            minY = sy < minY ? sy : minY;
            maxY = sy > maxY ? sy : maxY;
        }
    }
    minX = minX < x0 ? x0 : minX;
    minY = minY < y0 ? y0 : minY;
    maxX = maxX > x1 ? x1 : maxX;
    maxY = maxY > y1 ? y1 : maxY;
    return maxX > minX && maxY > minY ? (maxX - minX) * (maxY - minY) : 0.0;
}

void rd_perf_more_reset(RdPerfMore *b)
{
    memset(b, 0, sizeof(*b));
}

uint64_t rd_perf_more_draw_invocations(uint64_t tris, bool prepass)
{
    return tris * (prepass ? 3u : 9u);
}

uint64_t rd_perf_more_draw_invocations_left(uint64_t tris, uint64_t leftOut, bool prepass)
{
    if (!prepass) {
        return tris * 9u;
    }
    const uint64_t left = leftOut < tris ? leftOut : tris;
    return tris * 3u - left * 2u;
}

void rd_perf_more_add(RdPerfMore *b, const RdPerfRecord *r, bool prepass)
{
    b->n++;
    b->keep += r->keep ? 1u : 0u;
    b->prepassN += prepass ? 1u : 0u;
    b->tris += (double)r->vuTris;
    b->draws += (double)r->draws;
    b->passes += (double)r->renderPasses;
    b->noDualPairs += (double)r->noDualPairs;
    b->noDualColour += (double)r->noDualColour;
    b->noDualTris += (double)r->noDualTris;
    for (int k = 0; k < RD_BREAK_COUNT; k++) {
        b->breakN[k] += (double)r->breakN[k];
        b->breakKB[k] += (double)r->breakKB[k];
    }
    b->dateSnaps += (double)r->dateSnapshots;
    for (int k = 0; k < RD_DATE_RETAKE_COUNT; k++) {
        b->dateWhy[k] += (double)r->dateRetakeWhy[k];
    }
    b->dateReused += (double)r->dateSnapReused;
    b->boxEmpty += (double)r->shadowBoxEmpty;
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        b->listBoxPx[l] += (double)r->listBoxPx[l];
        b->listBoxDraws[l] += (double)r->listBoxDraws[l];
        b->listBoxNone[l] += (double)r->listBoxNone[l];
        b->listTris[l] += (double)r->vuListTris[l];
        if (r->listBoxTargetPx[l] != 0) {
            b->listTargetPx[l] = r->listBoxTargetPx[l];
        }
    }
    b->preVerts += prepass ? (double)r->vuPreVertices : 0.0;
    b->drawInv += (double)rd_perf_more_draw_invocations_left(r->vuTris, r->vuTrisLeftOut, prepass);
    b->leftOut += prepass ? (double)r->vuTrisLeftOut : 0.0;
    if (r->glowLight <= RD_GLOW_LIGHT_OFF) {
        b->glowPlan[r->glowLight]++;
    }
    if (r->gpuValid) {
        b->gpuN++;
        b->gpuMs += r->gpuMs;
        b->uploadMs += r->gpuUploadMs;
        b->prepassMs += r->gpuPrepassMs;
        b->presentMs += r->gpuPresentMs;
        for (int l = 0; l < RD_LIST_COUNT; l++) {
            b->listMs[l] += r->gpuListMs[l];
        }
        for (int p = 0; p < RD_PERF_POST_COUNT; p++) {
            b->postMs[p] += r->gpuPostMs[p];
        }
    }
}

/* text into out at at (kept at most cap - 1) */
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

/* 1182, 61k, 4.1k, 1.2M: as window_scene.c's counts */
static const char *count(double v, char *buf, size_t n)
{
    if (v >= 1e6) {
        snprintf(buf, n, "%.1fM", v / 1e6);
    } else if (v >= 1e4) {
        snprintf(buf, n, "%.0fk", v / 1e3);
    } else if (v >= 1e3) {
        snprintf(buf, n, "%.1fk", v / 1e3);
    } else {
        snprintf(buf, n, "%.0f", v);
    }
    return buf;
}

static const char *const k_dateWhy[RD_DATE_RETAKE_COUNT] = {
    "the first", "another target before", "an alpha write since", "its area not held",
    "the snapshot used elsewhere"};

size_t rd_perf_more_cost_text(const RdPerfMore *b, char *out, size_t cap)
{
    Out o = {out, cap, 0};
    if (cap > 0) {
        out[0] = '\0';
    }
    if (b->n == 0) {
        return 0;
    }
    const double n = (double)b->n;
    char c1[24], c2[24];
    put(&o, "costs a picture:");
    const char *sep = " ";
    if (b->noDualPairs > 0.0 || b->noDualColour > 0.0) {
        /* only where the device takes the fallback (no dual-source blending) */
        put(&o,
            "%stwo-pass blend %.1f draws drawn twice (%s triangles' vertex work twice), %.1f "
            "drawn once by the colour pass",
            sep, b->noDualPairs / n, count(b->noDualTris / n, c1, sizeof c1), b->noDualColour / n);
        sep = "; ";
    }
    double breaks = 0.0;
    for (int k = 0; k < RD_BREAK_COUNT; k++) {
        breaks += b->breakN[k];
    }
    put(&o, "%sSCENE's pass ended mid-picture %.1f times", sep, breaks / n);
    sep = "; ";
    if (breaks > 0.0) {
        const char *in = " (";
        for (int k = 0; k < RD_BREAK_COUNT; k++) {
            if (b->breakN[k] > 0.0) {
                put(&o, "%s%s %.1f, %.1f MB", in, rd_perf_break_word(k), b->breakN[k] / n,
                    b->breakKB[k] / n / 1024.0);
                in = "; ";
            }
        }
        put(&o, ")");
    }
    put(&o, "%sDATE snapshots %.1f", sep, b->dateSnaps / n);
    if (b->dateSnaps > 0.0) {
        const char *in = " (";
        for (int k = 0; k < RD_DATE_RETAKE_COUNT; k++) {
            if (b->dateWhy[k] > 0.0) {
                put(&o, "%s%s %.1f", in, k_dateWhy[k], b->dateWhy[k] / n);
                in = ", ";
            }
        }
        if (in[0] == ',') {
            put(&o, ")");
        }
    }
    put(&o, ", %.1f reused, %.1f composites with an empty box", b->dateReused / n, b->boxEmpty / n);
    const char *lists = "; boxes against triangles:";
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        if (b->listTris[l] <= 0.0 && b->listBoxDraws[l] <= 0.0 && b->listBoxNone[l] <= 0.0) {
            continue;
        }
        put(&o, "%s%s list %d %s triangles", lists, lists[0] == ';' ? "" : ",", l,
            count(b->listTris[l] / n, c1, sizeof c1));
        lists = "";
        if (b->listBoxDraws[l] > 0.0) {
            put(&o, " in %.2f Mpx of boxes", b->listBoxPx[l] / n / 1e6);
            if (b->listTargetPx[l] != 0) {
                put(&o, " (%.1f times its target's %.2f Mpx GS",
                    b->listBoxPx[l] / n / (double)b->listTargetPx[l],
                    (double)b->listTargetPx[l] / 1e6);
            } else {
                put(&o, " (GS");
            }
            put(&o, "; %.0f draws, %.0f without a box)", b->listBoxDraws[l] / n,
                b->listBoxNone[l] / n);
        } else {
            put(&o, ", no box (%s draws without one, or the culler off)",
                count(b->listBoxNone[l] / n, c2, sizeof c2));
        }
    }
    return o.at;
}

size_t rd_perf_more_compare_text(const RdPerfMore *b, const char *scene, bool tiler,
                                 const char *robust, char *out, size_t cap)
{
    Out o = {out, cap, 0};
    if (cap > 0) {
        out[0] = '\0';
    }
    const double n = b->n ? (double)b->n : 1.0;
    char c1[24], c2[24], c3[24];
    double breaks = 0.0;
    for (int k = 0; k < RD_BREAK_COUNT; k++) {
        breaks += b->breakN[k];
    }
    put(&o,
        "compare: %s: %u pictures (%u keep frames); a picture %s triangles, %.0f draws, %.1f "
        "passes, %.1f breaks",
        scene != NULL && scene[0] != '\0' ? scene : "no scene", b->n, b->keep,
        count(b->tris / n, c1, sizeof c1), b->draws / n, b->passes / n, breaks / n);
    if (b->gpuN) {
        const double g = (double)b->gpuN;
        put(&o,
            "; GPU ms a picture: span %.1f, %s %.1f, list 0 %.1f, list 3 %.1f, aura %.1f, "
            "reduction %.1f, glow %.1f, crack fill %.1f, present %.1f, pre-pass %.1f",
            b->gpuMs / g, tiler ? "before list 0" : "uploads", b->uploadMs / g, b->listMs[0] / g,
            b->listMs[3] / g, b->postMs[RD_PERF_POST_AURA] / g,
            b->postMs[RD_PERF_POST_REDUCTION] / g, b->postMs[RD_PERF_POST_GLOW] / g,
            b->postMs[RD_PERF_POST_CRACK] / g, b->presentMs / g, b->prepassMs / g);
    } else {
        put(&o, "; no GPU times");
    }
    put(&o, "; vertex invocations a picture: the pre-pass's %s, the draws' %s (%s; never summed)",
        count(b->preVerts / n, c2, sizeof c2), count(b->drawInv / n, c3, sizeof c3),
        b->leftOut > 0.0      ? "3 a triangle, after the index compaction 1 a triangle it left out"
        : b->prepassN == b->n ? "3 a triangle"
        : b->prepassN == 0u   ? "9 a triangle without the pre-pass"
                              : "3 a triangle with the pre-pass, 9 without");
    static const char *const k_plan[RD_GLOW_LIGHT_OFF + 1] = {"none", "dark", "lamps", "whole",
                                                              "off"};
    put(&o, "; glow plans:");
    for (int k = 0; k <= RD_GLOW_LIGHT_OFF; k++) {
        if (b->glowPlan[k] != 0) {
            put(&o, " %s %u", k_plan[k], b->glowPlan[k]);
        }
    }
    if (b->n == 0) {
        put(&o, " none");
    }
    if (robust != NULL && robust[0] != '\0') {
        put(&o, "; robust access %s", robust);
    }
    return o.at;
}

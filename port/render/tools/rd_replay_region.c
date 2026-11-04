/* rd_replay_region.c: rd_replay_tool --region-stats (docs/port/RENDER.md's
 * tool table, docs/port/VU1_PROGRAMS.md "The region rule").
 *
 * For every VU draw of lists 0..6 under the region rule (prelit, lit,
 * skinned and grid draws whose clip mode is REGION) the rule's verdict on
 * each triangle the draw kicks, through the CPU oracle (vu1_ref.h): the
 * shaders' region test on each corner (vu1ref_region_position,
 * vu1ref_region_inside_at), the cut (what the GPU draws of the triangle,
 * cut to 0 <= z <= w, then to the camera's picture's rows, its x extent
 * met with the picture's columns: vu_common.hlsli vu_cut_z, vu_cut_rows),
 * the card test (vu1ref_strip_passes, ICO_VU_MARK_JOINED), the verdict
 * (vu1ref_region_draw's order) and, on a frame blended between two ticks
 * (ICO_VU_TICKS: the draw's u[3] RD_VU_TICK_CAMERAS), the ticks' cameras
 * (vu_tick_shown, vu1ref_region_between).  A triangle is
 *   exact   all three corners inside the region: drawn at its GS positions
 *   clip    drawn clipped: under photo mode's free camera, or wholly beside
 *           the camera's picture
 *   strip   drawn clipped by the card test: it reaches the picture in a
 *           state other than out of the box, every corner in front of the
 *           eye, and its strip is joined or a triangle of it passes
 *   drop    not drawn, for the first of: a corner at w <= 0 (behind-eye,
 *           the rule's last step), out of the box (out-of-box: the Original
 *           preset at 4:3 and 1x, where the PS2 drew nothing), else its
 *           strip a card (card)
 *   rescued a drop drawn clipped because a tick's camera showed it
 *   beside  a drop (after the ticks) drawn clipped beside the camera's
 *           picture, its pixels in the picture's columns discarded (the
 *           rule's last step, VU_RULE_CLIP_BESIDE): it faces the eye
 *           (vu1ref_region_facing: the corners' homogeneous determinant,
 *           the draw's matrix's sign "det" below, the kick's
 *           ICO_VU_MARK_FLIP) and the draw has side bands: the frame is
 *           wider than 4:3 and the bound target's frame wider than the
 *           picture, the mesh's winding marks came from normals
 *           (RdMeshRec.facingFrom; never a grid), ICO_RD_VU_BESIDE not 0
 *           (rd_replay_vu.c's ICO_VU_NO_BESIDE; the exact blend's storage
 *           read, which also keeps the drop, is not modelled); never a
 *           card (the card test's drop at the draw's own camera keeps
 *           it: issue 189's mist quads), so beside's "card k" is 0
 * and a HIDE triangle (the content table) is counted apart.  The cut is
 * made in double precision in GS pixels (a corner with a GS position at
 * it, w = 1, as vu_cut_position takes it; one without in the homogeneous
 * form, its z the GS Z times 2^-32, the D24S8 scale): a triangle on the
 * picture's edge to a rounding can get the other verdict than the
 * shader's.  The frame a tick's verdict is met with is the bound target's
 * GS extent, 2048 -+ half the state's GS size.
 *
 * Lines (each "<dump>: region: ..."): one a draw, "L:I mesh|skinned|grid
 * NAME tris T: exact E clip C strip S drop D (behind-eye B, out-of-box X,
 * card K; rescued at a tick R), beside N (behind-eye b, out-of-box x, card
 * k) det -|+[; hidden H]; beside N, dropped back D[ (no side bands)]" (the
 * drop's counts are the triangles that stay dropped, back-facing or
 * edge-on or without side bands, by reason; beside's the ones drawn
 * beside, by the reason they were dropped; the closing field repeats the
 * two sums so `--mesh NAME` shows at a glance whether every drop of a
 * mesh is back-facing: the opening's cliff st26a_p1 must read "beside 0";
 * det: the sign of the
 * determinant of the x, y and w rows of the draw's model-to-GS matrix,
 * VU memory 16..18 for the normal and grid programs, 4..6 for the cluster,
 * the columns as vu_mat reads them: negative where the projection mirrors
 * the model, ICO_VU_MIRROR; "; hidden H" after it when the draw has HIDE
 * triangles); one
 * a list with its totals; the frame's with the state, the totals and the
 * draws whose every triangle is dropped or hidden (a beside triangle
 * draws), as a --nop spec
 * (--region-out FILE writes it: replayed with --nop @FILE the pass dumps
 * are the same, the rd_region_stats test).  --region-png FILE paints each
 * dropped triangle of a draw on the frame's own target (its GS size the
 * frame's, the frame camera's picture) as the GPU would have drawn it,
 * cut to 0 <= z <= w and to the frame, on black at the native frame's
 * size: red behind-eye, yellow out-of-box, cyan card (the drops that
 * stay), magenta a drop drawn beside (the whole polygon: the GPU keeps
 * only its part outside the band); the 4:3 band
 * (GS 2048 -+ 256) outlined in green. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "rd_mesh.h"
#include "rd_replay_region.h"
#include "../vu1_ref/vu1_ref.h"
#include "../../shaders/shader_consts.h"

/* R_EYE .. R_CARD: the drops that stay (back-facing or edge-on, or the draw
 * has no facing); R_BESIDE_EYE .. R_BESIDE_CARD: the drops drawn beside the
 * picture instead (facing the eye on a wide frame), by the same reasons;
 * under the camera rule R_CAM_EYE .. R_CAM_CARD the drops it draws (facing
 * the eye, or a grid's), by the reason the PS2's rule dropped them, and
 * R_STRIP_BACK a triangle the PS2's card test draws (a joined or passing
 * strip outside out of the box) that faces away: the camera rule drops it */
enum {
    R_EXACT,
    R_CLIP,
    R_STRIP,
    R_EYE,
    R_BOX,
    R_CARD,
    R_RESCUED,
    R_BESIDE_EYE,
    R_BESIDE_BOX,
    R_BESIDE_CARD,
    R_STRIP_BACK,
    R_CAM_EYE,
    R_CAM_BOX,
    R_CAM_CARD,
    R_HIDDEN,
    R_COUNT
};

typedef struct RegionCount {
    uint64_t n[R_COUNT];
    uint32_t draws, mirrored;
} RegionCount;

/* a corner of the draw: the clip position and what the shaders make of it */
typedef struct RegionVtx {
    float h[4];
    int inside;
    int gsOk; /* vu_gs_ok: drawn at its GS position (gx, gy, gz) */
    int32_t gx, gy, gz;
} RegionVtx;

#define POLY_MAX 16

typedef struct RegionWalk {
    const char *dump;
    const RdFrame *f;
    const RdRegionOpts *o;
    int extended;
    RegionCount list[7];
    RegionCount total;
    uint32_t tickDraws, otherDraws;
    char *spec;
    size_t specLen, specCap;
    uint32_t specItems;
    uint8_t *canvas; /* --region-png: f->gsW x f->gsH RGBA */
    uint32_t painted;
    /* the rule the counts and --region-png follow: the camera's
     * (rd__region_camera) or the PS2's ([dev] region_rule = ps2) */
    int camera;
    /* --region-mask: f->gsW x f->gsH RGBA, where the two rules' pictures
     * may differ: R a triangle the PS2's rule drops (whole, or beside the
     * picture) that the camera rule draws, G one the PS2's rule draws that
     * the camera rule drops (a back face the card test drew), B a sprite
     * the PS2's rule drops out of the box that the camera rule draws whole */
    uint8_t *mask;
    uint64_t maskTris, maskBack, maskSprites;
    /* the sprites outside the VU's window reaching the picture out of the
     * box (the PS2's drop, drawn whole by the camera rule) */
    uint64_t spritesDropped;
} RegionWalk;

/* vu_common.hlsli vu_rcp: a zero (or denormal) divisor gives +-Fmax */
static float regionRcp(float x)
{
    uint32_t u;
    memcpy(&u, &x, 4);
    if ((u & 0x7F800000u) == 0u) {
        const uint32_t m = (u & 0x80000000u) | 0x7F7FFFFFu;
        float r;
        memcpy(&r, &m, 4);
        return r;
    }
    return 1.0f / x;
}

/* vu_ftoi4: x * 16 truncated, saturated (NaN 0) */
static int32_t regionFtoi4(float x)
{
    const float v = x * 16.0f;
    if (v >= 2147483648.0f) {
        return INT32_MAX;
    }
    if (v <= -2147483648.0f) {
        return INT32_MIN;
    }
    return v == v ? (int32_t)v : 0;
}

/* the divide and the test on h (vu_divide, vu_gs_ok, vu_inside) */
static void regionFinish(RegionVtx *v, const float lo[4], const float hi[4])
{
    const float q = regionRcp(v->h[3]);
    const float px = v->h[0] * q, py = v->h[1] * q, pz = v->h[2] * q;
    v->gx = regionFtoi4(px);
    v->gy = regionFtoi4(py);
    v->gz = regionFtoi4(pz);
    v->gsOk = v->h[3] > 0.0f && v->gz >= 0 && v->gz != INT32_MAX && v->gx != INT32_MAX &&
              v->gx != INT32_MIN && v->gy != INT32_MAX && v->gy != INT32_MIN;
    v->inside = vu1ref_region_inside_at(v->h, lo, hi);
}

/* vu_cut_position in GS pixels: the GS position (w 1) or the homogeneous
 * form */
static void regionCutPos(const RegionVtx *v, double p[4])
{
    if (v->gsOk) {
        p[0] = (double)v->gx / 16.0;
        p[1] = (double)v->gy / 16.0;
        p[2] = (double)v->gz * 0x1p-32;
        p[3] = 1.0;
        return;
    }
    p[0] = v->h[0];
    p[1] = v->h[1];
    p[2] = 16.0 * (double)v->h[2] * 0x1p-32;
    p[3] = v->h[3];
}

/* Sutherland-Hodgman: poly[0..n) cut to pl . p >= 0 */
static int regionClip(double (*poly)[4], int n, const double pl[4])
{
    double q[POLY_MAX][4];
    int m = 0;
    for (int i = 0; i < n; i++) {
        const double *a = poly[i], *b = poly[i + 1 < n ? i + 1 : 0];
        const double da = pl[0] * a[0] + pl[1] * a[1] + pl[2] * a[2] + pl[3] * a[3];
        const double db = pl[0] * b[0] + pl[1] * b[1] + pl[2] * b[2] + pl[3] * b[3];
        if (da >= 0.0 && m < POLY_MAX) {
            memcpy(q[m++], a, sizeof(q[0]));
        }
        if ((da >= 0.0) != (db >= 0.0) && m < POLY_MAX) {
            const double t = da / (da - db);
            for (int c = 0; c < 4; c++) {
                q[m][c] = a[c] + (b[c] - a[c]) * t;
            }
            m++;
        }
    }
    memcpy(poly, q, (size_t)m * sizeof(q[0]));
    return m;
}

/* the triangle cut to 0 <= z <= w (vu_cut_z) */
static int regionCutZ(const RegionVtx *a, const RegionVtx *b, const RegionVtx *c, double (*poly)[4])
{
    static const double kZ0[4] = {0, 0, 1, 0}, kZ1[4] = {0, 0, -1, 1};
    regionCutPos(a, poly[0]);
    regionCutPos(b, poly[1]);
    regionCutPos(c, poly[2]);
    int n = regionClip(poly, 3, kZ0);
    return regionClip(poly, n, kZ1);
}

/* whether the z-cut polygon reaches the rectangle e (left, right, top,
 * bottom in GS pixels): cut to its rows, the x extent met with its columns
 * (vu_cut_rows) */
static int regionReaches(const double (*zp)[4], int n, const double e[4])
{
    double p[POLY_MAX][4];
    memcpy(p, zp, (size_t)n * sizeof(p[0]));
    const double top[4] = {0, 1, 0, -e[2]}, bottom[4] = {0, -1, 0, e[3]};
    n = regionClip(p, n, top);
    n = regionClip(p, n, bottom);
    if (n < 3) {
        return 0;
    }
    double lo = INFINITY, hi = -INFINITY;
    for (int i = 0; i < n; i++) {
        const double x = p[i][3] > 0.0 ? p[i][0] / p[i][3] : p[i][0] >= 0.0 ? INFINITY : -INFINITY;
        lo = x < lo ? x : lo;
        hi = x > hi ? x : hi;
    }
    return lo < e[1] && hi > e[0];
}

/* the corner through tick k's camera (vu_tick_vertex: R_k h, the columns
 * of RdFrame.tickCams[k] in the VU's order) */
static void regionTick(const RdFrame *f, int k, const RegionVtx *v, const float lo[4],
                       const float hi[4], RegionVtx *o)
{
    const float *r = f->tickCams[k];
    for (int i = 0; i < 4; i++) {
        float s = r[i] * v->h[0];
        s = s + r[4 + i] * v->h[1];
        s = s + r[8 + i] * v->h[2];
        o->h[i] = s + r[12 + i] * v->h[3];
    }
    regionFinish(o, lo, hi);
}

/* vu_tick_shown: 0 not shown at tick k, 1 through the card test, 2 drawn */
static int regionTickShown(const RegionWalk *w, int camera, int k, const RegionVtx *a,
                           const RegionVtx *b, const RegionVtx *c, const float lo[4],
                           const float hi[4], const double pic[4], const double frame[4])
{
    RegionVtx t[3];
    regionTick(w->f, k, a, lo, hi, &t[0]);
    regionTick(w->f, k, b, lo, hi, &t[1]);
    regionTick(w->f, k, c, lo, hi, &t[2]);
    double zp[POLY_MAX][4];
    const int n = regionCutZ(&t[0], &t[1], &t[2], zp);
    int rule = 0; /* 0 exact, 1 clip, 2 strip, 3 drop */
    if (!(t[0].inside && t[1].inside && t[2].inside)) {
        if (!regionReaches((const double (*)[4])zp, n, pic)) {
            rule = 1;
        } else if (!camera && w->extended && t[0].h[3] > 0.0f && t[1].h[3] > 0.0f &&
                   t[2].h[3] > 0.0f) {
            /* the card test (none under the camera rule) */
            rule = 2;
        } else {
            rule = 3;
        }
    }
    if (rule == 3 || !regionReaches((const double (*)[4])zp, n, frame)) {
        return 0;
    }
    return rule == 2 ? 1 : 2;
}

static void specAdd(RegionWalk *w, int l, uint32_t i)
{
    char item[32];
    const int len = snprintf(item, sizeof(item), "%s%d:%u", w->specItems ? "," : "", l, i);
    if (w->specLen + (size_t)len + 1 > w->specCap) {
        const size_t cap = w->specCap ? w->specCap * 2 : 256;
        char *s = realloc(w->spec, cap);
        if (!s) {
            return;
        }
        w->spec = s;
        w->specCap = cap;
    }
    memcpy(w->spec + w->specLen, item, (size_t)len + 1);
    w->specLen += (size_t)len;
    w->specItems++;
}

/* The polygon zp (GS pixels, homogeneous) cut to the frame and filled at
 * pixel centres on canvas (f->gsW x f->gsH RGBA): each channel set to rgb's
 * (orMask 0, --region-png) or or-ed with it (orMask 1, --region-mask);
 * true when a pixel was filled */
static bool regionFill(RegionWalk *w, uint8_t *canvas, const double (*zp)[4], int n,
                       const uint8_t rgb[3], int orMask)
{
    const RdFrame *f = w->f;
    const double ox = 2048.0 - (double)(f->gsW >> 1), oy = 2048.0 - (double)(f->gsH >> 1);
    double p[POLY_MAX][4];
    memcpy(p, zp, (size_t)n * sizeof(p[0]));
    const double planes[5][4] = {{1, 0, 0, -ox},
                                 {-1, 0, 0, ox + f->gsW},
                                 {0, 1, 0, -oy},
                                 {0, -1, 0, oy + f->gsH},
                                 {0, 0, 0, 1}};
    for (int k = 0; k < 5 && n >= 3; k++) {
        n = regionClip(p, n, planes[k]);
    }
    double xy[POLY_MAX][2];
    double y0 = INFINITY, y1 = -INFINITY;
    int m = 0;
    for (int i = 0; i < n; i++) {
        if (!(p[i][3] > 0.0)) {
            continue;
        }
        xy[m][0] = p[i][0] / p[i][3] - ox;
        xy[m][1] = p[i][1] / p[i][3] - oy;
        y0 = xy[m][1] < y0 ? xy[m][1] : y0;
        y1 = xy[m][1] > y1 ? xy[m][1] : y1;
        m++;
    }
    if (m < 3) {
        return false;
    }
    int r0 = (int)floor(y0 - 0.5), r1 = (int)ceil(y1 - 0.5);
    r0 = r0 < 0 ? 0 : r0;
    r1 = r1 >= (int)f->gsH ? (int)f->gsH - 1 : r1;
    bool any = false;
    for (int r = r0; r <= r1; r++) {
        const double yc = r + 0.5;
        double xl = INFINITY, xr = -INFINITY;
        for (int i = 0; i < m; i++) {
            const double *a = xy[i], *b = xy[i + 1 < m ? i + 1 : 0];
            if ((a[1] <= yc) == (b[1] <= yc)) {
                continue;
            }
            const double x = a[0] + (b[0] - a[0]) * (yc - a[1]) / (b[1] - a[1]);
            xl = x < xl ? x : xl;
            xr = x > xr ? x : xr;
        }
        if (!(xl <= xr)) {
            continue;
        }
        int c0 = (int)ceil(xl - 0.5), c1 = (int)ceil(xr - 0.5) - 1;
        c0 = c0 < 0 ? 0 : c0;
        c1 = c1 >= (int)f->gsW ? (int)f->gsW - 1 : c1;
        for (int c = c0; c <= c1; c++) {
            uint8_t *px = canvas + ((size_t)r * f->gsW + (size_t)c) * 4;
            for (int k = 0; k < 3; k++) {
                px[k] = orMask ? (uint8_t)(px[k] | rgb[k]) : rgb[k];
            }
            any = true;
        }
    }
    return any;
}

/* --region-png: the polygon painted in rgb */
static void regionPaint(RegionWalk *w, const double (*zp)[4], int n, const uint8_t rgb[3])
{
    w->painted += regionFill(w, w->canvas, zp, n, rgb, 0);
}

/* --region-mask: the polygon's pixels marked in the channels of rgb */
static void regionMark(RegionWalk *w, const double (*zp)[4], int n, const uint8_t rgb[3])
{
    if (w->mask) {
        regionFill(w, w->mask, zp, n, rgb, 1);
    }
}

/* the sign of the determinant of the x, y and w rows of the draw's
 * model-to-GS matrix: columns c[0..2] */
static int regionDet(const float (*c)[4])
{
    const double a = c[0][0], b = c[1][0], d = c[2][0];
    const double e = c[0][1], g = c[1][1], h = c[2][1];
    const double i = c[0][3], j = c[1][3], k = c[2][3];
    const double det = a * (g * k - h * j) - b * (e * k - h * i) + d * (e * j - g * i);
    return det > 0.0 ? 1 : det < 0.0 ? -1 : 0;
}

static void countAdd(RegionCount *t, const RegionCount *d)
{
    for (int k = 0; k < R_COUNT; k++) {
        t->n[k] += d->n[k];
    }
    t->draws += d->draws;
    t->mirrored += d->mirrored;
}

static uint64_t countTris(const RegionCount *c)
{
    uint64_t t = 0;
    for (int k = 0; k < R_HIDDEN; k++) {
        t += c->n[k];
    }
    return t;
}

static uint64_t countDropped(const RegionCount *c)
{
    return c->n[R_EYE] + c->n[R_BOX] + c->n[R_CARD] + c->n[R_STRIP_BACK];
}

static uint64_t countCamera(const RegionCount *c)
{
    return c->n[R_CAM_EYE] + c->n[R_CAM_BOX] + c->n[R_CAM_CARD];
}

static uint64_t countBeside(const RegionCount *c)
{
    return c->n[R_BESIDE_EYE] + c->n[R_BESIDE_BOX] + c->n[R_BESIDE_CARD];
}

static void countText(const RegionCount *c, char *buf, size_t cap)
{
    snprintf(buf, cap,
             "tris %llu: exact %llu clip %llu strip %llu drop %llu (behind-eye %llu, out-of-box "
             "%llu, card %llu; rescued at a tick %llu; back faces the card test drew %llu), "
             "beside %llu (behind-eye %llu, out-of-box %llu, card %llu), drawn by the camera "
             "rule %llu (behind-eye %llu, out-of-box %llu, card %llu)",
             (unsigned long long)countTris(c), (unsigned long long)c->n[R_EXACT],
             (unsigned long long)c->n[R_CLIP], (unsigned long long)c->n[R_STRIP],
             (unsigned long long)countDropped(c), (unsigned long long)c->n[R_EYE],
             (unsigned long long)c->n[R_BOX], (unsigned long long)c->n[R_CARD],
             (unsigned long long)c->n[R_RESCUED], (unsigned long long)c->n[R_STRIP_BACK],
             (unsigned long long)countBeside(c), (unsigned long long)c->n[R_BESIDE_EYE],
             (unsigned long long)c->n[R_BESIDE_BOX], (unsigned long long)c->n[R_BESIDE_CARD],
             (unsigned long long)countCamera(c), (unsigned long long)c->n[R_CAM_EYE],
             (unsigned long long)c->n[R_CAM_BOX], (unsigned long long)c->n[R_CAM_CARD]);
}

/* the program's kind for the oracle (rd_mesh.c cardKind): -1 not a draw
 * under the region rule */
static int regionKind(const RdCmd *c, const RdVuPayload *p)
{
    if (p->clip != RD_VU_CLIP_REGION) {
        return -1;
    }
    switch (p->prog) {
    case RD_PROG_PRELIT:
    case RD_PROG_LIT:
    case RD_PROG_LIT_SPEC:
    case RD_PROG_REFLECT:
        return c->type == RDC_MESH ? VU1REF_CARD_NORMAL : -1;
    case RD_PROG_SKIN:
    case RD_PROG_SKIN_SPEC:
        return c->type == RDC_SKINNED ? VU1REF_CARD_CLUSTER : -1;
    case RD_PROG_GRID:
    case RD_PROG_GRID_LIT:
        return c->type == RDC_GRID ? VU1REF_CARD_MESH : -1;
    default:
        return -1;
    }
}

/* A particle command (vu_particle.hlsl vu_particle, vu_particle_vs; the
 * stream after the bones: qwords 3, 4 the clip window, 5 the size factor
 * and the UV step, then two a sprite): the sprites the PS2's rule drops out
 * of the box (shown, outside the VU's x/y window, the quad reaching the
 * camera's picture; a tick's rescue not modelled, so the count and the
 * mask are the most that can change) and the camera rule draws whole,
 * counted and marked in --region-mask's blue */
static void regionSprites(RegionWalk *w, const RdCmd *c, const RdStateBlock *st,
                          const RdVuPayload *p)
{
    const RdFrame *f = w->f;
    if (w->extended || f->camera.freeCamera) {
        return; /* drawn whole under either rule */
    }
    const uint64_t at = (uint64_t)c->u[1] + sizeof(*p) + sizeof(RdVuBlock);
    const uint32_t count = p->vertsPerBatch;
    if (p->streamQw < 6u + 2u * (uint64_t)count ||
        at + ((uint64_t)p->boneQw + p->streamQw) * 16 > f->payloadSize) {
        return;
    }
    const float (*mem)[4] = (const float (*)[4])(const void *)(f->payload + c->u[1] + sizeof(*p));
    const float (*q)[4] = (const float (*)[4])(const void *)(f->payload + at) + p->boneQw;
    const uint32_t fh = st->gsH > g_rd.gsH ? st->gsH : g_rd.gsH;
    const double halfW = c->b[3] ? (double)c->b[3] : 256.0;
    const double halfH = c->b[6] ? (double)c->b[6] : (double)(fh >> 1);
    const int mark = w->mask && st->gsW == f->gsW && st->gsH == f->gsH;
    for (uint32_t i = 0; i < count; i++) {
        const float *a = q[6 + 2 * i], *b = q[7 + 2 * i];
        /* vu_matm: c0 * v.x + c1 * v.y + c2 * v.z + c3 * w, each step rounded */
        float h[4], e[4];
        const float sz = (0.0f + a[3]) * q[5][0];
        for (int k = 0; k < 4; k++) {
            float t = mem[16][k] * a[0];
            t = t + mem[17][k] * a[1];
            t = t + mem[18][k] * a[2];
            h[k] = t + mem[19][k] * 1.0f;
            float u = mem[20][k] * sz;
            u = u + mem[21][k] * sz;
            u = u + mem[22][k] * 0.0f;
            e[k] = u + mem[23][k] * 1.0f;
        }
        uint32_t ab;
        memcpy(&ab, &b[3], 4);
        const int alphaZero = (ab & 0x7F800000u) == 0u;
        const float rq = regionRcp(h[3]);
        const float p0[2] = {(h[0] - e[0]) * rq, (h[1] - e[1]) * rq};
        const float p1[2] = {(h[0] + e[0]) * rq, (h[1] + e[1]) * rq};
        const float *lo = q[3], *hi = q[4];
        const int in0 = lo[0] < p0[0] && lo[1] < p0[1] && lo[3] < h[3] && p0[0] < hi[0] &&
                        p0[1] < hi[1] && h[3] < hi[3];
        const int in1 = lo[0] < p1[0] && lo[1] < p1[1] && lo[3] < h[3] && p1[0] < hi[0] &&
                        p1[1] < hi[1] && h[3] < hi[3];
        const int shown = !alphaZero && lo[3] < h[3] && h[3] < hi[3];
        if (!shown || (in0 && in1)) {
            continue;
        }
        /* the corners on the GS's 12.4 grid (vu_ftoi4), the quad against the picture
           (vu_rect_reaches_picture: edges count) */
        const double x0 = regionFtoi4(p0[0]) / 16.0, y0 = regionFtoi4(p0[1]) / 16.0;
        const double x1 = regionFtoi4(p1[0]) / 16.0, y1 = regionFtoi4(p1[1]) / 16.0;
        const double lx = x0 < x1 ? x0 : x1, hx = x0 < x1 ? x1 : x0;
        const double ly = y0 < y1 ? y0 : y1, hy = y0 < y1 ? y1 : y0;
        if (!(lx <= 2048.0 + halfW && hx >= 2048.0 - halfW && ly <= 2048.0 + halfH &&
              hy >= 2048.0 - halfH)) {
            continue;
        }
        w->spritesDropped++;
        if (mark) {
            double quad[POLY_MAX][4] = {
                {lx, ly, 0, 1}, {hx, ly, 0, 1}, {hx, hy, 0, 1}, {lx, hy, 0, 1}};
            static const uint8_t kB[3] = {0, 0, 255};
            regionMark(w, (const double (*)[4])quad, 4, kB);
            w->maskSprites++;
        }
    }
}

static void regionCmd(void *user, int l, uint32_t idx, const RdCmd *c, const RdStateBlock *st)
{
    RegionWalk *w = user;
    const RdFrame *f = w->f;
    if (l <= 6 && c->type == RDC_PARTICLES) {
        RdVuPayload pp;
        if ((uint64_t)c->u[1] + sizeof(pp) + sizeof(RdVuBlock) <= f->payloadSize) {
            memcpy(&pp, f->payload + c->u[1], sizeof(pp));
            regionSprites(w, c, st, &pp);
        }
        return;
    }
    if (l > 6 || (c->type != RDC_MESH && c->type != RDC_SKINNED && c->type != RDC_GRID)) {
        return;
    }
    RdVuPayload p;
    if ((uint64_t)c->u[1] + sizeof(p) + sizeof(RdVuBlock) > f->payloadSize) {
        return;
    }
    memcpy(&p, f->payload + c->u[1], sizeof(p));
    const int kind = regionKind(c, &p);
    if (kind < 0) {
        w->otherDraws++;
        return;
    }
    const float (*mem)[4] = (const float (*)[4])(const void *)(f->payload + c->u[1] + sizeof(p));
    static float bones[240][4];
    const float (*after)[4] = mem + 36;
    const uint64_t afterAt = (uint64_t)c->u[1] + sizeof(p) + sizeof(RdVuBlock);
    if (kind == VU1REF_CARD_CLUSTER) {
        const uint32_t nb = p.boneQw < 240u ? p.boneQw : 240u;
        if (afterAt + (uint64_t)nb * 16 > f->payloadSize) {
            return;
        }
        memset(bones, 0, sizeof(bones));
        memcpy(bones, after, (size_t)nb * 16);
    }
    /* the vertices (a grid's numbered b * vpb + v) and the kicks */
    const RdMeshRec *m = NULL;
    uint32_t nv = 0, qpv = 0, kickCount = 0;
    uint32_t *kicks = NULL;
    const float (*gridQw)[4] = NULL;
    uint32_t vpb = 0, stride = 0;
    const char *name = "grid";
    if (c->type == RDC_GRID) {
        vpb = p.vertsPerBatch;
        qpv = p.qwPerVertex;
        stride = 3 + vpb * qpv + 1;
        if (vpb < 3 || qpv == 0 || p.batchCount == 0 ||
            (uint64_t)p.batchCount * stride > p.streamQw ||
            afterAt + ((uint64_t)p.boneQw + p.streamQw) * 16 > f->payloadSize) {
            return;
        }
        gridQw = after + p.boneQw;
        nv = p.batchCount * vpb;
        kicks = malloc((size_t)nv * sizeof(uint32_t));
        if (!kicks) {
            return;
        }
        for (uint32_t b = 0; b < p.batchCount; b++) {
            for (uint32_t v = 2; v < vpb; v++) {
                kicks[kickCount++] = b * vpb + v;
            }
        }
    } else {
        m = rd__mesh_rec(c->u[0]);
        if (!m || !m->stream || !m->vu || m->batchCount == 0) {
            return;
        }
        name = m->name;
        nv = m->vertexCount;
        qpv = m->qwPerVertex;
        uint32_t last = p.firstBatch + p.batchCount;
        if (last > m->batchCount || p.batchCount == 0) {
            last = m->batchCount;
        }
        if (p.firstBatch >= last) {
            return;
        }
        const RdVuBatchRec *b0 = &m->batches[p.firstBatch], *b1 = &m->batches[last - 1];
        const uint32_t end = rd__vu_sheet_end(m, b1->firstIndex + b1->indexCount, w->extended != 0);
        const uint32_t n = end > b0->firstIndex ? (end - b0->firstIndex) / 3 : 0;
        kicks = malloc((size_t)(n ? n : 1) * sizeof(uint32_t));
        if (!kicks) {
            return;
        }
        for (uint32_t i = 0; i < n && b0->firstIndex + 3 * i < m->indexCount; i++) {
            const uint32_t k = (m->index[b0->firstIndex + 3 * i] & ICO_VU_INDEX_MASK) >> 2;
            if (k >= 2 && k < nv) {
                kicks[kickCount++] = k;
            }
        }
    }
    RegionVtx *vx = calloc(nv ? nv : 1, sizeof(RegionVtx));
    VuGsVertex *gv = calloc(nv ? nv : 1, sizeof(VuGsVertex));
    float *stw = m ? calloc(nv ? nv : 1, sizeof(float)) : NULL;
    int *first = calloc(nv ? nv : 1, sizeof(int));
    if (!vx || !gv || (m && !stw) || !first) {
        free(vx);
        free(gv);
        free(stw);
        free(first);
        free(kicks);
        return;
    }
    float lo[4] = {0}, hi[4] = {0};
    for (uint32_t v = 0; v < nv; v++) {
        const float (*q)[4] = m ? (const float (*)[4])(m->stream + (size_t)v * qpv)
                                : gridQw + (size_t)(v / vpb) * stride + 3 + (size_t)(v % vpb) * qpv;
        vu1ref_region_position(kind, mem, (const float (*)[4])bones, q, vx[v].h, lo, hi);
        regionFinish(&vx[v], lo, hi);
        gv[v].inside = vx[v].inside;
        gv[v].clipW = vx[v].h[3];
        if (m) {
            stw[v] = m->stream[(size_t)v * qpv + qpv - 2][3];
        }
        first[v] = m ? 0 : (int)(v / vpb * vpb);
    }
    if (m) {
        for (uint32_t b = 0; b < m->batchCount; b++) {
            const uint32_t fv = m->batches[b].firstVertex;
            for (uint32_t v = fv; v < nv && v < fv + m->batches[b].vertexCount; v++) {
                first[v] = (int)fv;
            }
        }
    }
    /* the camera's picture (rd_replay.c frameGroupOf's frame[1], frame[2])
     * and the bound target's frame, GS pixels: left, right, top, bottom */
    const uint32_t fh = st->gsH > g_rd.gsH ? st->gsH : g_rd.gsH;
    const double halfW = c->b[3] ? (double)c->b[3] : 256.0;
    const double halfH = c->b[6] ? (double)c->b[6] : (double)(fh >> 1);
    const double pic[4] = {2048.0 - halfW, 2048.0 + halfW, 2048.0 - halfH, 2048.0 + halfH};
    const double frame[4] = {2048.0 - (double)(st->gsW >> 1), 2048.0 + (double)(st->gsW >> 1),
                             2048.0 - (double)(st->gsH >> 1), 2048.0 + (double)(st->gsH >> 1)};
    const int freeCam = f->camera.freeCamera != 0;
    const int ticks = f->hasTickCams && c->u[3] == RD_VU_TICK_CAMERAS;
    const int paint = w->canvas && !c->b[3] && !c->b[6] && st->gsW == f->gsW && st->gsH == f->gsH;
    /* --region-mask: every draw on a target of the frame's size (a
       reflection camera's picture included: its triangles land on the same
       GS pixels when it draws into the frame) */
    const int mark = w->mask && st->gsW == f->gsW && st->gsH == f->gsH;
    /* the side bands (rd_replay_vu.c vuDropMode's RD_DROPS_BESIDE, vu_common.hlsli
       vu_frame_wider), the PS2's rule's: a frame wider than 4:3 whose bound target's frame is
       wider than the camera's picture, a mesh whose winding marks came from normals (not a
       grid, not a mesh without them), the switch ICO_RD_VU_BESIDE not 0, not the device's
       plain set */
    const char *besideEnv = getenv("ICO_RD_VU_BESIDE");
    const int wide = g_rd.gsW > ICO_SH_FRAME_BASE_W && (frame[0] < pic[0] || frame[1] > pic[1]) &&
                     m && m->facingFrom != RD_MESH_FACING_NONE &&
                     !(besideEnv && strcmp(besideEnv, "0") == 0) && !g_rd.vuNoBeside;
    /* the camera rule's (RD_DROPS_CAMERA): a mesh with facing data, or a grid (two-sided:
       ICO_VU_TWO_SIDED), not the device's plain set */
    const int twoSided = c->type == RDC_GRID;
    const int camOk = !g_rd.vuNoBeside && (twoSided || (m && m->facingFrom != RD_MESH_FACING_NONE));
    const int det = regionDet(kind == VU1REF_CARD_CLUSTER ? mem + 4 : mem + 16);
    RegionCount d;
    memset(&d, 0, sizeof(d));
    for (uint32_t i = 0; i < kickCount; i++) {
        const uint32_t k = kicks[i];
        const uint32_t marks =
            m ? rd__mesh_kick_mark(m, k) : (p.batchCount >= 2 ? (uint32_t)ICO_VU_MARK_JOINED : 0u);
        if (marks & ICO_VU_MARK_HIDE) {
            d.n[R_HIDDEN]++;
            continue;
        }
        const RegionVtx *a = &vx[k - 2], *b = &vx[k - 1], *cc = &vx[k];
        if (a->inside && b->inside && cc->inside) {
            d.n[R_EXACT]++;
            continue;
        }
        if (freeCam) {
            d.n[R_CLIP]++;
            continue;
        }
        double zp[POLY_MAX][4];
        const int n = regionCutZ(a, b, cc, zp);
        if (!regionReaches((const double (*)[4])zp, n, pic)) {
            d.n[R_CLIP]++;
            continue;
        }
        const int front = a->h[3] > 0.0f && b->h[3] > 0.0f && cc->h[3] > 0.0f;
        const int joined = (marks & ICO_VU_MARK_JOINED) != 0u;
        /* the PS2's rule ([dev] region_rule = ps2): drawn whole (the card test, a tick),
           beside the picture, or dropped */
        const int strip = w->extended && front;
        const int passes =
            strip && (joined || vu1ref_strip_passes(gv, stw, first, (int)nv, (int)k));
        int ps2Rescued = 0;
        if (!strip && ticks) {
            /* VU_RULE_DROP at the blended camera: a tick that showed it */
            const int s0 = regionTickShown(w, 0, 0, a, b, cc, lo, hi, pic, frame);
            const int s1 = s0 == 2 ? 2 : regionTickShown(w, 0, 1, a, b, cc, lo, hi, pic, frame);
            ps2Rescued = s0 == 2 || s1 == 2;
            if (!ps2Rescued && (s0 | s1)) {
                /* VU_RULE_STRIP from a tick: the card test (vu_rule_card) */
                ps2Rescued = joined || vu1ref_strip_passes(gv, stw, first, (int)nv, (int)k);
            }
        }
        const int why = !front ? R_EYE : !w->extended ? R_BOX : R_CARD;
        /* facing the eye (vu1ref_region_facing: the corners' determinant, the matrix's
           sign, the kick's ICO_VU_MARK_FLIP) */
        const int facing =
            vu1ref_region_facing(a->h, b->h, cc->h, det < 0, (marks & ICO_VU_MARK_FLIP) != 0u);
        const int ps2Drawn = passes || ps2Rescued;
        /* VU_RULE_CLIP_BESIDE, the side bands on a final drop: facing the eye on a wide
           frame; a card keeps the drop (vu_rule_beside: the camera's own verdict the card
           test's) */
        const int ps2Beside = !ps2Drawn && wide && why != R_CARD && facing;
        /* the camera rule: no card test, the ticks on every drop, then what faces the eye
           (vu_drop_fate); a draw it cannot take (no facing data, the device's plain set)
           keeps the PS2's rule under it too (rd_replay_vu.c RD_DROPS_KEPT) */
        int camRescued = ps2Rescued, camFront = 0, camDrawn = ps2Drawn;
        if (camOk) {
            camRescued = 0;
            if (ticks) {
                const int s0 = regionTickShown(w, 1, 0, a, b, cc, lo, hi, pic, frame);
                camRescued = s0 == 2 || regionTickShown(w, 1, 1, a, b, cc, lo, hi, pic, frame) == 2;
            }
            camFront = twoSided || facing;
            camDrawn = camRescued || camFront;
        }
        static const uint8_t kRgb[3][3] = {{255, 64, 64}, {255, 220, 0}, {0, 200, 255}};
        static const uint8_t kBeside[3] = {255, 0, 255};
        static const uint8_t kBack[3] = {0, 90, 255};
        if (!w->camera || !camOk) {
            if (passes) {
                d.n[R_STRIP]++;
            } else if (ps2Rescued) {
                d.n[R_RESCUED]++;
            } else {
                d.n[ps2Beside ? why - R_EYE + R_BESIDE_EYE : why]++;
                if (paint) {
                    regionPaint(w, (const double (*)[4])zp, n,
                                ps2Beside ? kBeside : kRgb[why - R_EYE]);
                }
            }
        } else if (camRescued) {
            d.n[R_RESCUED]++;
        } else if (camFront) {
            /* a passing or joined strip the card test draws too: the same picture */
            d.n[passes ? R_STRIP : why - R_EYE + R_CAM_EYE]++;
            if (paint && !passes) {
                regionPaint(w, (const double (*)[4])zp, n, kBeside);
            }
        } else {
            d.n[passes ? R_STRIP_BACK : why]++;
            if (paint) {
                regionPaint(w, (const double (*)[4])zp, n, passes ? kBack : kRgb[why - R_EYE]);
            }
        }
        if (mark && camDrawn && !ps2Drawn) {
            /* dropped (or drawn beside only) by the PS2's rule, drawn whole by the camera's */
            static const uint8_t kR[3] = {255, 0, 0};
            regionMark(w, (const double (*)[4])zp, n, kR);
            w->maskTris++;
        } else if (mark && !camDrawn && (ps2Drawn || ps2Beside)) {
            /* drawn by the PS2's rule, dropped by the camera's (a back face) */
            static const uint8_t kG[3] = {0, 255, 0};
            regionMark(w, (const double (*)[4])zp, n, kG);
            w->maskBack++;
        }
    }
    d.draws = 1;
    d.mirrored = det < 0;
    w->tickDraws += ticks != 0;
    if (!w->o->mesh || (m && strcmp(m->name, w->o->mesh) == 0)) {
        char text[320];
        countText(&d, text, sizeof(text));
        char hidden[40] = "";
        if (d.n[R_HIDDEN]) {
            snprintf(hidden, sizeof(hidden), "; hidden %llu", (unsigned long long)d.n[R_HIDDEN]);
        }
        printf("%s: region: %d:%u %s %.24s %s det %s%s; beside %llu, dropped back %llu%s; "
               "drawn by the camera rule %llu\n",
               w->dump, l, idx,
               c->type == RDC_MESH      ? "mesh"
               : c->type == RDC_SKINNED ? "skinned"
                                        : "grid",
               name, text, det < 0 ? "-" : "+", hidden, (unsigned long long)countBeside(&d),
               (unsigned long long)countDropped(&d), wide ? "" : " (no side bands)",
               (unsigned long long)countCamera(&d));
    }
    const uint64_t drawn = d.n[R_EXACT] + d.n[R_CLIP] + d.n[R_STRIP] + d.n[R_RESCUED] +
                           countBeside(&d) + countCamera(&d);
    if (drawn == 0 && countTris(&d) + d.n[R_HIDDEN] > 0) {
        specAdd(w, l, idx);
    }
    countAdd(&w->list[l], &d);
    countAdd(&w->total, &d);
    free(vx);
    free(gv);
    free(stw);
    free(first);
    free(kicks);
}

bool rd_replay_region(const char *dump, const RdFrame *f, const RdRegionOpts *o)
{
    RegionWalk w;
    memset(&w, 0, sizeof(w));
    w.dump = dump;
    w.f = f;
    w.o = o;
    w.extended = rd__vu_extended() ? 1 : 0;
    w.camera = rd__region_camera() ? 1 : 0;
    if (o->mask && f->gsW && f->gsH) {
        w.mask = calloc((size_t)f->gsW * f->gsH, 4);
        for (size_t i = 0; w.mask && i < (size_t)f->gsW * f->gsH; i++) {
            w.mask[i * 4 + 3] = 255;
        }
    }
    if (o->png && f->gsW && f->gsH) {
        w.canvas = malloc((size_t)f->gsW * f->gsH * 4);
        if (w.canvas) {
            for (size_t i = 0; i < (size_t)f->gsW * f->gsH; i++) {
                w.canvas[i * 4 + 0] = w.canvas[i * 4 + 1] = w.canvas[i * 4 + 2] = 0;
                w.canvas[i * 4 + 3] = 255;
            }
        }
    }
    /* the card test's joins over a part's meshes, as the replay makes
       them before its lists (rd_replay.c; done once a set of meshes) */
    rd__join_parts(f, (int)f->keep);
    RdStateBlock st = f->startState;
    rd__walk(f, (int)f->keep, &st, regionCmd, &w);
    char text[320];
    for (int l = 0; l < 7; l++) {
        if (!w.list[l].draws) {
            continue;
        }
        countText(&w.list[l], text, sizeof(text));
        printf("%s: region: list %d: %u draws, %s; hidden %llu; det - in %u draws\n", dump, l,
               w.list[l].draws, text, (unsigned long long)w.list[l].n[R_HIDDEN],
               w.list[l].mirrored);
    }
    countText(&w.total, text, sizeof(text));
    printf("%s: region: frame %u, %ux%u, %s, free camera %s, %u draws with the ticks' cameras; "
           "lists 0-6: %u draws under the region rule (%u VU draws not), %s; hidden %llu; det - "
           "in %u draws; drawing nothing: %u draws: %s\n",
           dump, f->number, f->gsW, f->gsH, w.extended ? "extended" : "out of the box",
           f->camera.freeCamera ? "on" : "off", w.tickDraws, w.total.draws, w.otherDraws, text,
           (unsigned long long)w.total.n[R_HIDDEN], w.total.mirrored, w.specItems,
           w.specItems ? w.spec : "none");
    /* the rule the counts follow ([dev] region_rule), the sprites
       (regionSprites) and the two rules' difference (--region-mask),
       whatever the rule in force */
    printf("%s: region: rule: %s\n", dump,
           w.camera ? "the camera's (a triangle the PS2 drops is drawn clipped when it faces the "
                      "eye)"
                    : "the PS2's ([dev] region_rule = ps2)");
    printf("%s: region: sprites: %llu outside the VU's window reaching the picture out of the "
           "box (the PS2's drop; drawn whole by the camera rule)\n",
           dump, (unsigned long long)w.spritesDropped);
    bool ok = true;
    if (o->mask) {
        if (!w.mask || !rd_write_png(o->mask, w.mask, f->gsW, f->gsH, f->gsW * 4, 0)) {
            fprintf(stderr, "%s: the --region-mask picture could not be written\n", o->mask);
            ok = false;
        } else {
            printf("%s: region: mask: %llu triangles the PS2's rule drops and the camera's draws "
                   "(red), %llu the PS2's draws and the camera's drops (green), %llu sprites "
                   "(blue), %ux%u GS pixels -> %s\n",
                   dump, (unsigned long long)w.maskTris, (unsigned long long)w.maskBack,
                   (unsigned long long)w.maskSprites, f->gsW, f->gsH, o->mask);
        }
    }
    if (o->out) {
        FILE *fp = fopen(o->out, "wb");
        if (!fp || fprintf(fp, "%s\n", w.specItems ? w.spec : "none") < 0) {
            fprintf(stderr, "%s: the --region-out spec could not be written\n", o->out);
            ok = false;
        }
        if (fp && fclose(fp) != 0) {
            ok = false;
        }
    }
    if (o->png) {
        if (w.canvas) {
            /* the 4:3 band, GS 2048 -+ 256, outlined */
            const int x0 = (int)(f->gsW >> 1) - 256, x1 = (int)(f->gsW >> 1) + 255;
            for (uint32_t y = 0; y < f->gsH; y++) {
                for (int e = 0; e < 2; e++) {
                    const int x = e ? x1 : x0;
                    if (x >= 0 && x < (int)f->gsW) {
                        uint8_t *px = w.canvas + ((size_t)y * f->gsW + (size_t)x) * 4;
                        px[0] = 0;
                        px[1] = 255;
                        px[2] = 0;
                    }
                }
            }
        }
        if (!w.canvas || !rd_write_png(o->png, w.canvas, f->gsW, f->gsH, f->gsW * 4, 0)) {
            fprintf(stderr, "%s: the --region-png picture could not be written\n", o->png);
            ok = false;
        } else {
            printf("%s: region: %u draws' dropped triangles painted -> %s\n", dump, w.painted,
                   o->png);
        }
    }
    free(w.canvas);
    free(w.mask);
    free(w.spec);
    return ok;
}

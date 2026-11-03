/* rd_vis_hiz.c: the occlusion test of the scene draws' visibility test
 * ([dev] cull = full; rd_vis.h, rd_vis.c; docs/port/RENDER.md, "Culling").
 *
 * In list 0, after the frustum test, a draw whose every pixel lies behind
 * what the opaque scene draws before it have already written to its depth
 * buffer fails its GEQUAL depth test at every sample and writes nothing
 * (colour, depth, alpha, stencil); rd_vis.c turns it into an RDC_NOP like
 * a draw off the target.  The picture is the same, byte for byte
 * (rd_cull_ab).
 *
 * The record.  Cells of 2 x 2 GS pixels over the bound target (and 4 GS
 * pixels round it) hold a lower bound of the depth buffer's GS Z at every
 * sample whose GS position lies in the cell (0: nothing known; larger Z is
 * nearer); tiles of 8 x 8 cells keep their least cell, worked out again
 * when a cell of theirs rose.  The record is reset to 0 at the head of list
 * 0, whenever the colour target, the depth target or the frame size
 * changes, at every clear, copy and exact blend, and at every draw that may
 * lower the depth (Z write on under a test other than GEQUAL or GREATER):
 * 0 is a lower bound of any depth, so a reset is always sound.  A GEQUAL
 * draw only ever raises the depth, so between two resets every bound holds.
 *
 * The occluders.  The triangles of an opaque prelit or lit mesh draw (code
 * 32 or 36: rd_vis.c says which states, among them an alpha test the
 * alpha plan proves passes at every pixel) that the program draws exactly in
 * every state: all three vertices pass its region test (code 32: 0 < x, y <
 * 4094 and 0 < w < 16777214 after the divide; code 36: no clip flag in
 * mem[20..23]'s space) with a margin the float rounding cannot cross, in
 * front of the eye, GS Z inside ftoi4's range, neither hidden by the
 * content table nor raised by the later mark (vu_later_out) nor a sheet's
 * tile.  Such a triangle is VU_RULE_EXACT (vu_common.hlsli vu_region_rule,
 * vu_triangle_out_pos) at its GS positions: each corner's ftoi4 X / 16 lies
 * in (p - 1/16, p] of the exact x / w = p computed here in double (the
 * float product's error, RD_VIS_TOL of the sum of the absolute products,
 * added), and the GPU snaps it to its own sub-pixel grid (a 16th of a
 * texel at the coarsest a device may have, 8 bits on the GPUs seen; a
 * texel is a GS pixel or less but under Full pixel in a window narrower
 * than the frame: 1/8 GS pixel covers texels up to two GS pixels).  So the
 * corner lies within r of the centre p - 1/32 on each axis
 * (rd__vis_hiz_vertex).  A cell
 * counts as covered by the triangle when the cell's square, grown by a GS
 * pixel each way (a sample's depth may come from a sample up to a GS pixel
 * away: a texel's samples about its centre, which the MSAA epoch loads from
 * the scene and resolves into it) and by half a GS pixel more up and down
 * (a draw positioned with the frame's half-line offset and one without it
 * put the same sample half a pixel apart in GS terms: rd_replay.c
 * sceneKeepsHalf), lies inside the triangle by a margin
 * that holds for every placement of the corners within r and leaves 0.25 GS
 * pixel to the GPU's rasterisation rules (rd__vis_hiz_raster): then the
 * GPU rasterises every pixel or MSAA sample centre in it, at every scale,
 * and writes its depth or finds it nearer already.  No face culling exists
 * anywhere in the port.  The cell's bound becomes max(bound, floor(the
 * least corner Z) - 1): the triangle's depth over the cell is the
 * interpolation of its corners' depths, never below the least of them but
 * for the interpolation's float error (a few hundred GS Z units, under the
 * margin below).  Only triangles whose screen box covers a 64th of the frame
 * count, the largest first within the budget ICO_RD_CULL_HIZ_TRIS (default
 * 12000 a picture).  Inside the target's scissor only.
 *
 * The occludee.  A mesh or grid draw of list 0 under GEQUAL whose bound
 * (rd_vis.c's box of its stream, the corners through its matrix) has every
 * corner in front of the eye (w > 0 by the tolerance): every vertex is then
 * a convex combination of the corners with w > 0, so its x / w and its Z =
 * 16 z / w lie between the corners' (the mediant of the corners'
 * fractions), and the GPU's points of its triangles too (exact corners
 * within 1/16 + the snap, homogeneous ones exactly, the clipped points
 * convex combinations).  The box of the corners' x / w, y / w, grown by
 * those amounts, holds every sample the draw can touch; its Z bound Zmax
 * every depth it can write (below 0xFFFF0000: gs_math.hlsli gs_z_to_depth's
 * top band never reached).  The draw is hidden when every cell the box
 * touches holds a bound above Zmax + margin, margin = 4096 + Zmax / 2^17
 * (the depth buffer's steps: 256 GS Z units on D24, 2^-24 of the value on
 * D32F; the interpolation's float error on both sides), and for a mesh with
 * the later mark max(Zmax / 256, 1024) more (vu_later_bias's cap).  Then at
 * every sample p of the draw depth(p) >= cell > Zmax + margin >= Z(p) +
 * margin: GEQUAL fails at any rounding inside the margin.  A coplanar decal
 * or second pass (its Z equal to the occluder's) is kept by the strict
 * compare and the margin; a draw touching a cell no occluder covered whole
 * is kept. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "rd_mesh.h"
#include "rd_vis.h"
#include "shader_consts.h"

/* the region test's bounds (vu_inside: VU_LO0, VU_HI4094) with a GS pixel
 * of room, and its w bound */
#define HIZ_LO 1.0
#define HIZ_HI 4093.0
#define HIZ_WHI 16777214.0
/* an occluder's corner Z below ftoi4's saturation (2^31 - 1) with room */
#define HIZ_ZTOP 2147418112.0 /* 2^31 - 2^16 */
/* an occludee's Z bound below gs_z_to_depth's top band */
#define HIZ_DTOP 4294901760.0 /* 0xFFFF0000 */
/* the divide's and the product's float rounding (q = 1 / w, p = h q), with
 * room */
#define HIZ_ULP (1.0 / 1048576.0) /* 2^-20 */
/* the GPU's snap of a position to its sub-pixel grid (4 bits at least, of
 * a texel up to two GS pixels), and the GS-to-clip mapping's float
 * rounding */
#define HIZ_SNAP (1.0 / 8.0)
#define HIZ_NDC (1.0 / 1024.0)
/* the rasterisation's own room inside every edge (GS pixels) */
#define HIZ_SHRINK 0.25
/* the frame head's half line (RD_TARGET_HALF_Y) one draw may have and
 * another not */
#define HIZ_HALF_Y 0.5
/* how far a sample's depth may come from: a texel's samples lie within
 * half a texel (a GS pixel at most, two texels a GS pixel at least but
 * under Full pixel in a narrow window) of its centre, and the MSAA epoch
 * loads a pixel's depth into its samples and resolves them into it
 * (rd_replay.c msaaAttach, rd_msaa.c) */
#define HIZ_RESAMPLE 1.0
/* the cells beyond the target's rectangle on each side (GS pixels) */
#define HIZ_PAD 4.0
/* the largest record (cells): 8192 x 4096 GS pixels */
#define HIZ_MAX_CELLS (4096u * 2048u)

#define HIZ_T_WRITTEN 1u /* a cell of the tile is above 0 */
#define HIZ_T_DIRTY 2u   /* the tile's least cell is stale */

static bool finite4(const float *p)
{
    return isfinite(p[0]) && isfinite(p[1]) && isfinite(p[2]) && isfinite(p[3]);
}

/* h = m[0] v.x + m[1] v.y + m[2] v.z + m[3] v.w in the VU's order, in
 * double, and each lane's sum of the absolute products */
static void hOf(const float (*m)[4], const double v[4], double h[4], double mag[4])
{
    for (int c = 0; c < 4; c++) {
        double s = (double)m[0][c] * v[0];
        s = s + (double)m[1][c] * v[1];
        s = s + (double)m[2][c] * v[2];
        s = s + (double)m[3][c] * v[3];
        h[c] = s;
        if (mag) {
            mag[c] = fabs((double)m[0][c] * v[0]) + fabs((double)m[1][c] * v[1]) +
                     fabs((double)m[2][c] * v[2]) + fabs((double)m[3][c] * v[3]);
        }
    }
}

static double dmax(double a, double b)
{
    return a > b ? a : b;
}

static double dmin(double a, double b)
{
    return a < b ? a : b;
}

bool rd__vis_hiz_vertex(const float (*m)[4], const float (*clip)[4], const float *pos,
                        RdVisHizVtx *out)
{
    if (!m || !pos || !finite4(pos)) {
        return false;
    }
    /* code 36 reads pos.w (vu_prelit.hlsl, vu_lit.hlsl), the others 1 */
    const double v[4] = {pos[0], pos[1], pos[2], clip ? (double)pos[3] : 1.0};
    double h[4], mag[4];
    hOf(m, v, h, mag);
    if (!isfinite(h[0]) || !isfinite(h[1]) || !isfinite(h[2]) || !isfinite(h[3])) {
        return false;
    }
    const double tw = RD_VIS_TOL * mag[3], tz = RD_VIS_TOL * mag[2];
    const double wlo = h[3] - tw, whi = h[3] + tw;
    if (!(wlo > 0.0) || !(whi < HIZ_WHI)) {
        return false;
    }
    if (clip) {
        /* code 36: no clip flag (vu_clipw: |x|, |y|, |z| <= |w|) by the
         * tolerance */
        double c[4], cm[4];
        hOf(clip, v, c, cm);
        const double aw = fabs(c[3]) - RD_VIS_TOL * cm[3];
        for (int k = 0; k < 3; k++) {
            if (!(fabs(c[k]) + RD_VIS_TOL * cm[k] < aw)) {
                return false;
            }
        }
    }
    double p[2], e[2];
    for (int k = 0; k < 2; k++) {
        p[k] = h[k] / h[3];
        e[k] = (RD_VIS_TOL * mag[k] + fabs(p[k]) * tw) / wlo + fabs(p[k]) * HIZ_ULP;
        if (!(p[k] - e[k] > HIZ_LO) || !(p[k] + e[k] < HIZ_HI)) {
            return false;
        }
    }
    /* GS Z = ftoi4(z q): at least 16 (z - tz) / whi less the roundings and
     * the truncation */
    if (!(h[2] - tz > 0.0)) {
        return false;
    }
    const double zlo = 16.0 * (h[2] - tz) / whi * (1.0 - HIZ_ULP) - 1.0;
    const double zhi = 16.0 * (h[2] + tz) / wlo * (1.0 + HIZ_ULP) + 1.0;
    if (!(zlo > 0.0) || !(zhi < HIZ_ZTOP)) {
        return false;
    }
    /* the drawn X lies in (p - 1/16, p] (ftoi4 truncates; ICO_RD_VU_OFFGRID
     * draws p itself) of the float p, within e of this one: about
     * p - 1/32, and the GPU's snap and mapping on top */
    out->x = p[0] - 1.0 / 32.0;
    out->y = p[1] - 1.0 / 32.0;
    out->r = 1.0 / 32.0 + dmax(e[0], e[1]) + HIZ_SNAP + HIZ_NDC;
    out->zlo = zlo;
    return true;
}

bool rd__vis_hiz_tri(const RdVisHizVtx *a, const RdVisHizVtx *b, const RdVisHizVtx *c,
                     RdVisHizTri *t)
{
    const RdVisHizVtx *v[3] = {a, b, c};
    double zmin = a->zlo, r = 0.0;
    double x0 = a->x, x1 = a->x, y0 = a->y, y1 = a->y;
    for (int k = 0; k < 3; k++) {
        t->x[k] = v[k]->x;
        t->y[k] = v[k]->y;
        r = dmax(r, v[k]->r);
        zmin = dmin(zmin, v[k]->zlo);
        x0 = dmin(x0, v[k]->x);
        x1 = dmax(x1, v[k]->x);
        y0 = dmin(y0, v[k]->y);
        y1 = dmax(y1, v[k]->y);
    }
    t->r = r;
    t->area = (x1 - x0) * (y1 - y0);
    t->cmd = 0;
    t->sel = 1;
    /* floor of the least corner's bound, less one */
    const double lb = floor(zmin) - 1.0;
    if (!(lb >= 1.0) || !(lb < HIZ_ZTOP)) {
        t->lb = 0;
        return false;
    }
    t->lb = (uint32_t)lb;
    return true;
}

bool rd__vis_hiz_occludee(const float (*m)[4], const RdVisBox *b, bool scissor, RdVisHizBox *out)
{
    if (!m || !b || !out || b->n == 0) {
        return false;
    }
    for (int i = 0; i < 4; i++) {
        if (!finite4(m[i])) {
            return false;
        }
    }
    /* w: 1, or under code 36 the stream's pos.w range with 1 (rd_vis.c
     * rd__vis_test) */
    double ws[2] = {1.0, 1.0};
    if (scissor) {
        ws[0] = b->lo[3] < 1.0f ? (double)b->lo[3] : 1.0;
        ws[1] = b->hi[3] > 1.0f ? (double)b->hi[3] : 1.0;
    }
    /* each lane's bound of the absolute products over the box: every
     * vertex's float h lies within RD_VIS_TOL of it of the exact one */
    const double ax = dmax(fabs((double)b->lo[0]), fabs((double)b->hi[0]));
    const double ay = dmax(fabs((double)b->lo[1]), fabs((double)b->hi[1]));
    const double az = dmax(fabs((double)b->lo[2]), fabs((double)b->hi[2]));
    const double aw = dmax(fabs(ws[0]), fabs(ws[1]));
    double tol[4];
    for (int c = 0; c < 4; c++) {
        tol[c] = RD_VIS_TOL * (fabs((double)m[0][c]) * ax + fabs((double)m[1][c]) * ay +
                               fabs((double)m[2][c]) * az + fabs((double)m[3][c]) * aw);
    }
    double x0 = 0.0, x1 = 0.0, y0 = 0.0, y1 = 0.0, zu = 0.0;
    bool any = false;
    const int nw = scissor ? 2 : 1;
    for (int k = 0; k < 8; k++) {
        for (int j = 0; j < nw; j++) {
            const double v[4] = {(double)(k & 1 ? b->hi[0] : b->lo[0]),
                                 (double)(k & 2 ? b->hi[1] : b->lo[1]),
                                 (double)(k & 4 ? b->hi[2] : b->lo[2]), ws[j]};
            double h[4];
            hOf(m, v, h, NULL);
            const double wlo = h[3] - tol[3], whi = h[3] + tol[3];
            /* every corner in front of the eye, and x and y positive (the
             * box inside (0, 4094) below): the mediants then bound every
             * vertex's fractions */
            if (!(wlo > 0.0) || !(h[0] - tol[0] > 0.0) || !(h[1] - tol[1] > 0.0) ||
                !isfinite(h[2])) {
                return false;
            }
            const double cx0 = (h[0] - tol[0]) / whi, cx1 = (h[0] + tol[0]) / wlo;
            const double cy0 = (h[1] - tol[1]) / whi, cy1 = (h[1] + tol[1]) / wlo;
            const double cz = 16.0 * (h[2] + tol[2]) / wlo;
            x0 = any ? dmin(x0, cx0) : cx0;
            x1 = any ? dmax(x1, cx1) : cx1;
            y0 = any ? dmin(y0, cy0) : cy0;
            y1 = any ? dmax(y1, cy1) : cy1;
            zu = any ? dmax(zu, cz) : cz;
            any = true;
        }
    }
    /* the divide's rounding; a GS position up to 1/16 left of (above) its
     * point and the GPU's snap either way */
    out->x0 = x0 * (1.0 - HIZ_ULP) - 1.0 / 16.0 - HIZ_SNAP - HIZ_NDC;
    out->y0 = y0 * (1.0 - HIZ_ULP) - 1.0 / 16.0 - HIZ_SNAP - HIZ_NDC;
    out->x1 = x1 * (1.0 + HIZ_ULP) + HIZ_SNAP + HIZ_NDC;
    out->y1 = y1 * (1.0 + HIZ_ULP) + HIZ_SNAP + HIZ_NDC;
    out->zmax = zu > 0.0 ? zu * (1.0 + HIZ_ULP) + 16.0 : 16.0;
    return out->x0 > 0.0 && out->y0 > 0.0 && out->x1 < 4094.0 && out->y1 < 4094.0 &&
           out->zmax < HIZ_DTOP;
}

double rd__vis_hiz_margin(double zmax, bool later)
{
    double m = 4096.0 + zmax / 131072.0; /* 2^-17 */
    if (later) {
        m += dmax(zmax / 256.0, 1024.0); /* vu_later_bias's cap */
    }
    return m;
}

/* ------------------------------------------------------------ the record */

void rd__vis_hiz_reset(RdVisHiz *z)
{
    for (uint32_t k = 0; k < z->nWritten; k++) {
        const uint32_t t = z->written[k];
        memset(&z->cell[(size_t)t << 6], 0, 64u * sizeof(uint32_t));
        z->tmin[t] = 0;
        z->tflag[t] = 0;
    }
    z->nWritten = 0;
}

bool rd__vis_hiz_setup(RdVisHiz *z, const RdVisRect *r)
{
    rd__vis_hiz_reset(z);
    z->ncx = z->ncy = z->ntx = z->nty = 0;
    if (!r || !(r->x1 > r->x0) || !(r->y1 > r->y0) || !(r->x0 > -65536.0) || !(r->x1 < 65536.0) ||
        !(r->y0 > -65536.0) || !(r->y1 < 65536.0)) {
        return false;
    }
    z->gx0 = floor(r->x0) - HIZ_PAD;
    z->gy0 = floor(r->y0) - HIZ_PAD;
    /* the samples a draw can have: the rectangle's, half a texel round it
     * at worst (rd_vis.c) */
    z->x0 = r->x0 - 1.0;
    z->y0 = r->y0 - 1.0;
    z->x1 = r->x1 + 1.0;
    z->y1 = r->y1 + 1.0;
    const uint32_t cx = (uint32_t)ceil((r->x1 + HIZ_PAD - z->gx0) / RD_VIS_HIZ_CELL);
    const uint32_t cy = (uint32_t)ceil((r->y1 + HIZ_PAD - z->gy0) / RD_VIS_HIZ_CELL);
    const uint32_t ntx = (cx + 7u) / 8u, nty = (cy + 7u) / 8u;
    const uint64_t cells = (uint64_t)ntx * nty * 64u;
    if (cells > HIZ_MAX_CELLS) {
        return false;
    }
    if (cells > z->cap) {
        uint32_t *cell = calloc((size_t)cells, sizeof(uint32_t));
        uint32_t *tmin = calloc((size_t)cells / 64u, sizeof(uint32_t));
        uint8_t *tflag = calloc((size_t)cells / 64u, 1);
        uint32_t *written = malloc((size_t)cells / 64u * sizeof(uint32_t));
        if (!cell || !tmin || !tflag || !written) {
            free(cell);
            free(tmin);
            free(tflag);
            free(written);
            return false;
        }
        free(z->cell);
        free(z->tmin);
        free(z->tflag);
        free(z->written);
        z->cell = cell;
        z->tmin = tmin;
        z->tflag = tflag;
        z->written = written;
        z->cap = (uint32_t)cells;
    }
    z->ncx = ntx * 8u;
    z->ncy = nty * 8u;
    z->ntx = ntx;
    z->nty = nty;
    return true;
}

void rd__vis_hiz_free(RdVisHiz *z)
{
    free(z->cell);
    free(z->tmin);
    free(z->tflag);
    free(z->written);
    memset(z, 0, sizeof(*z));
}

static void setCell(RdVisHiz *z, uint32_t i, uint32_t j, uint32_t lb)
{
    const uint32_t t = (j >> 3) * z->ntx + (i >> 3);
    uint32_t *c = &z->cell[((size_t)t << 6) | ((j & 7u) << 3) | (i & 7u)];
    if (*c >= lb) {
        return;
    }
    *c = lb;
    if (!(z->tflag[t] & HIZ_T_WRITTEN)) {
        z->tflag[t] |= HIZ_T_WRITTEN;
        z->written[z->nWritten++] = t;
    }
    z->tflag[t] |= HIZ_T_DIRTY;
}

uint32_t rd__vis_hiz_raster(RdVisHiz *z, const RdVisHizTri *t, const double clip[4])
{
    if (!z->ncx || !t->lb) {
        return 0;
    }
    const double ax = t->x[0], ay = t->y[0];
    const double area2 = (t->x[1] - ax) * (t->y[2] - ay) - (t->y[1] - ay) * (t->x[2] - ax);
    if (!(fabs(area2) > 0.0)) {
        return 0;
    }
    const double s = area2 > 0.0 ? 1.0 : -1.0;
    /* each corner anywhere within r on each axis: within er of its centre */
    const double er = t->r * 1.4142135623730951;
    double len[3], lmax = 0.0;
    for (int e = 0; e < 3; e++) {
        const int n = (e + 1) % 3;
        len[e] = hypot(t->x[n] - t->x[e], t->y[n] - t->y[e]);
        lmax = dmax(lmax, len[e]);
    }
    /* Edge e from corner e (A) to the next (B), u = B - A: f(Q) = s u x
     * (Q - A) is positive inside.  With the corners moved by a and b (|a|,
     * |b| <= er), the moved edge's f'(Q) >= f(Q) - er |u| - 2 er |Q - A| -
     * 2 er^2, and |Q - A| <= lmax for Q inside; a distance of HIZ_SHRINK
     * from the moved edge needs f' >= HIZ_SHRINK |u + b - a|.  So a point
     * with f(Q) >= k[e] on every edge is inside the triangle (k[e] > 0)
     * and that far inside the moved one. */
    double fa[3], fb[3], fc[3], k[3];
    for (int e = 0; e < 3; e++) {
        const int n = (e + 1) % 3;
        const double ux = t->x[n] - t->x[e], uy = t->y[n] - t->y[e];
        fa[e] = -s * uy;
        fb[e] = s * ux;
        fc[e] = s * (uy * t->x[e] - ux * t->y[e]);
        k[e] = er * len[e] + 2.0 * er * lmax + 2.0 * er * er + HIZ_SHRINK * (len[e] + 2.0 * er);
        /* the solve's own rounding */
        k[e] += 1e-9 * (fabs(fa[e]) + fabs(fb[e])) * 4096.0;
    }
    /* the cells' square grown by HIZ_RESAMPLE each way and the half line
     * up and down must lie inside every edge and the clip rectangle (x0,
     * y0, x1, y1) */
    const double gx = HIZ_RESAMPLE, gy = HIZ_RESAMPLE + HIZ_HALF_Y;
    double ylo = dmin(t->y[0], dmin(t->y[1], t->y[2]));
    double yhi = dmax(t->y[0], dmax(t->y[1], t->y[2]));
    double xlo = dmin(t->x[0], dmin(t->x[1], t->x[2]));
    double xhi = dmax(t->x[0], dmax(t->x[1], t->x[2]));
    ylo = dmax(ylo, clip[1]) + gy;
    yhi = dmin(yhi, clip[3]) - gy;
    xlo = dmax(xlo, clip[0]) + gx;
    xhi = dmin(xhi, clip[2]) - gx;
    if (!(yhi - ylo >= RD_VIS_HIZ_CELL) || !(xhi - xlo >= RD_VIS_HIZ_CELL)) {
        return 0;
    }
    const double cs = RD_VIS_HIZ_CELL;
    int64_t j0 = (int64_t)ceil((ylo - z->gy0) / cs), j1 = (int64_t)floor((yhi - z->gy0) / cs) - 1;
    int64_t i0 = (int64_t)ceil((xlo - z->gx0) / cs), i1 = (int64_t)floor((xhi - z->gx0) / cs) - 1;
    j0 = j0 < 0 ? 0 : j0;
    i0 = i0 < 0 ? 0 : i0;
    j1 = j1 >= (int64_t)z->ncy ? (int64_t)z->ncy - 1 : j1;
    i1 = i1 >= (int64_t)z->ncx ? (int64_t)z->ncx - 1 : i1;
    uint32_t n = 0;
    for (int64_t j = j0; j <= j1; j++) {
        const double Y0 = z->gy0 + cs * (double)j - gy;
        const double Y1 = Y0 + cs + 2.0 * gy;
        if (Y0 < clip[1] || Y1 > clip[3]) {
            continue;
        }
        /* the cells' left edges X = gx0 + cs i, the grown square's X - gx
         * .. X + cs + gx: per edge f's least over it is at its left (fa >=
         * 0) or right, and at Y0 (fb >= 0) or Y1 */
        double lo = (double)i0, hi = (double)i1;
        for (int e = 0; e < 3 && lo <= hi; e++) {
            const double yy = fb[e] >= 0.0 ? Y0 : Y1;
            const double rest = k[e] - fc[e] - fb[e] * yy; /* need fa X* >= rest */
            if (fa[e] > 0.0) {
                lo = dmax(lo, ceil((rest / fa[e] + gx - z->gx0) / cs));
            } else if (fa[e] < 0.0) {
                hi = dmin(hi, floor((rest / fa[e] - cs - gx - z->gx0) / cs));
            } else if (rest > 0.0) {
                hi = lo - 1.0;
            }
        }
        for (double i = lo; i <= hi; i += 1.0) {
            setCell(z, (uint32_t)i, (uint32_t)j, t->lb);
            n++;
        }
    }
    return n;
}

static uint32_t tileMin(RdVisHiz *z, uint32_t t)
{
    if (z->tflag[t] & HIZ_T_DIRTY) {
        const uint32_t *c = &z->cell[(size_t)t << 6];
        uint32_t m = c[0];
        for (int k = 1; k < 64; k++) {
            m = c[k] < m ? c[k] : m;
        }
        z->tmin[t] = m;
        z->tflag[t] &= (uint8_t)~HIZ_T_DIRTY;
    }
    return z->tmin[t];
}

bool rd__vis_hiz_hidden_why(RdVisHiz *z, const RdVisHizBox *b, double thr, int *why)
{
    int w0;
    if (!why) {
        why = &w0;
    }
    *why = RD_VIS_HIZ_OUTSIDE;
    if (!z->ncx || !b || !(thr >= 0.0)) {
        return false;
    }
    /* the box's samples: inside the target's (no sample beyond) */
    const double x0 = dmax(b->x0, z->x0), x1 = dmin(b->x1, z->x1);
    const double y0 = dmax(b->y0, z->y0), y1 = dmin(b->y1, z->y1);
    if (!(x1 >= x0) || !(y1 >= y0)) {
        return false; /* the frustum test's to say */
    }
    const double cs = RD_VIS_HIZ_CELL;
    const int64_t i0 = (int64_t)floor((x0 - z->gx0) / cs), i1 = (int64_t)floor((x1 - z->gx0) / cs);
    const int64_t j0 = (int64_t)floor((y0 - z->gy0) / cs), j1 = (int64_t)floor((y1 - z->gy0) / cs);
    if (i0 < 0 || j0 < 0 || i1 >= (int64_t)z->ncx || j1 >= (int64_t)z->ncy) {
        return false;
    }
    for (int64_t ty = j0 >> 3; ty <= j1 >> 3; ty++) {
        for (int64_t tx = i0 >> 3; tx <= i1 >> 3; tx++) {
            const uint32_t t = (uint32_t)(ty * z->ntx + tx);
            if (!(z->tflag[t] & HIZ_T_WRITTEN)) {
                *why = RD_VIS_HIZ_UNCOVERED;
                return false;
            }
            const int64_t ci0 = tx * 8 > i0 ? tx * 8 : i0, ci1 = tx * 8 + 7 < i1 ? tx * 8 + 7 : i1;
            const int64_t cj0 = ty * 8 > j0 ? ty * 8 : j0, cj1 = ty * 8 + 7 < j1 ? ty * 8 + 7 : j1;
            if (ci0 == tx * 8 && ci1 == tx * 8 + 7 && cj0 == ty * 8 && cj1 == ty * 8 + 7) {
                /* the whole tile: its least cell */
                if (!((double)tileMin(z, t) > thr)) {
                    *why = tileMin(z, t) == 0u ? RD_VIS_HIZ_UNCOVERED : RD_VIS_HIZ_MARGIN;
                    return false;
                }
                continue;
            }
            /* a part of it: the cells the box touches */
            const uint32_t *c = &z->cell[(size_t)t << 6];
            for (int64_t j = cj0; j <= cj1; j++) {
                for (int64_t i = ci0; i <= ci1; i++) {
                    if (!((double)c[((j & 7) << 3) | (i & 7)] > thr)) {
                        *why = c[((j & 7) << 3) | (i & 7)] == 0u ? RD_VIS_HIZ_UNCOVERED
                                                                 : RD_VIS_HIZ_MARGIN;
                        return false;
                    }
                }
            }
        }
    }
    *why = RD_VIS_HIZ_HIDDEN;
    return true;
}

bool rd__vis_hiz_hidden(RdVisHiz *z, const RdVisHizBox *b, double thr)
{
    return rd__vis_hiz_hidden_why(z, b, thr, NULL);
}

/* ------------------------------------------- a texture not uploaded yet */

bool rd__vis_hiz_pending_scan(uint32_t id, RdTexAlphaScan *out)
{
    const RdTexRec *t = rd__tex_rec(id);
    if (!t || !t->live || t->kind != RD_TEXKIND_IMAGE || !t->dirty || t->replacement ||
        t->refused || t->format != RD_TEXEL_RGBA8 || !t->pixels || !t->w || !t->h ||
        rd__sheet_baked(t)) {
        return false;
    }
    const bool gs = t->gsLevels > 1 && t->gsChain;
    uint8_t ref;
    if (!gs && rd__mip_boost(t, &ref)) {
        return false; /* rdtex_keep_alpha_coverage may move the levels' alpha */
    }
    rdtex_alpha_scan_begin(out);
    rdtex_alpha_scan(out, t->pixels, (size_t)t->w * t->h);
    if (gs) {
        const uint8_t *src = t->gsChain;
        uint32_t w = t->w, h = t->h;
        for (uint32_t l = 1; l < t->gsLevels; l++) {
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
            rdtex_alpha_scan(out, src, (size_t)w * h);
            src += (size_t)w * h * 4;
        }
    }
    return out->any != 0;
}

/* ------------------------------------------------- an occluder's triangles */

static struct {
    RdVisHizVtx *v;
    uint32_t *stamp;
    uint8_t *ok;
    uint32_t cap, now;
} s_memo;

bool rd__vis_hiz_list_push(RdVisHizList *l, const RdVisHizTri *t)
{
    if (l->n == l->cap) {
        const uint32_t cap = l->cap ? l->cap * 2u : 256u;
        RdVisHizTri *g = realloc(l->t, (size_t)cap * sizeof(RdVisHizTri));
        if (!g) {
            return false;
        }
        l->t = g;
        l->cap = cap;
    }
    l->t[l->n++] = *t;
    return true;
}

/* vertex v of m through the draw (memoised for the draw: s_memo.now) */
static bool vertexOf(const RdMeshRec *m, uint32_t v, const float (*mm)[4], const float (*clip)[4],
                     const RdVisHizVtx **out)
{
    if (s_memo.stamp[v] != s_memo.now) {
        s_memo.stamp[v] = s_memo.now;
        s_memo.ok[v] =
            rd__vis_hiz_vertex(mm, clip, m->stream[(size_t)v * m->qwPerVertex], &s_memo.v[v]) ? 1u
                                                                                              : 0u;
    }
    *out = &s_memo.v[v];
    return s_memo.ok[v] != 0;
}

/* the triangles of the index range [i0, i1) of m */
static int rangeTris(const RdMeshRec *m, uint32_t i0, uint32_t i1, const float (*mm)[4],
                     const float (*clip)[4], uint32_t cmd, double minArea, RdVisHizList *out,
                     uint32_t *lbAll)
{
    int all = 1;
    i1 = i1 > m->indexCount ? m->indexCount : i1;
    for (uint32_t i = i0; i < i1; i += 3u) {
        if (i1 - i < 3u) {
            all = 0; /* a part of a triangle: not read */
            break;
        }
        const uint32_t kick = m->index[i] >> 2;
        if ((m->index[i] & 3u) != 0u || m->index[i + 1u] != m->index[i] + 1u ||
            m->index[i + 2u] != m->index[i] + 2u || kick < 2u || kick >= m->vertexCount) {
            all = 0;
            continue;
        }
        const uint8_t mark = m->kickMark && kick < m->markCount ? m->kickMark[kick] : 0u;
        if (mark & ICO_VU_MARK_HIDE) {
            continue; /* drawn nowhere (vu_content_hidden) */
        }
        const RdVisHizVtx *a, *b, *c;
        const bool okA = vertexOf(m, kick - 2u, mm, clip, &a);
        const bool okB = vertexOf(m, kick - 1u, mm, clip, &b);
        const bool okC = vertexOf(m, kick, mm, clip, &c);
        RdVisHizTri t;
        if (!okA || !okB || !okC || !rd__vis_hiz_tri(a, b, c, &t)) {
            all = 0;
            continue;
        }
        *lbAll = t.lb < *lbAll ? t.lb : *lbAll;
        if ((mark & ICO_VU_MARK_LATER) || (m->tiledFrom && kick >= m->tiledFrom) ||
            t.area < minArea) {
            continue; /* drawn, raised by the later mark or only in some states: not counted */
        }
        t.cmd = cmd;
        if (!rd__vis_hiz_list_push(out, &t)) {
            return -1;
        }
    }
    return all;
}

int rd__vis_hiz_mesh(const RdMeshRec *m, const RdVuPayload *p, const float (*mem)[4], uint32_t cmd,
                     double minArea, RdVisHizList *out, uint32_t *lbAll)
{
    *lbAll = UINT32_MAX;
    if (!m || !m->stream || !m->index || !m->batches || p->firstBatch >= m->batchCount ||
        (p->clip != RD_VU_CLIP_REGION && p->clip != RD_VU_CLIP_SCISSOR &&
         p->clip != RD_VU_CLIP_NONE)) { /* NONE (code 34): drawn at the GS position unless X or Y
                                         * wraps to 16 bits, which no vertex accepted below does */
        return 0;
    }
    if (m->vertexCount > s_memo.cap) {
        RdVisHizVtx *v = realloc(s_memo.v, (size_t)m->vertexCount * sizeof(RdVisHizVtx));
        if (v) {
            s_memo.v = v;
        }
        uint32_t *st = v ? realloc(s_memo.stamp, (size_t)m->vertexCount * sizeof(uint32_t)) : NULL;
        if (st) {
            s_memo.stamp = st;
        }
        uint8_t *ok = st ? realloc(s_memo.ok, m->vertexCount) : NULL;
        if (!ok) {
            return -1;
        }
        s_memo.ok = ok;
        /* the new entries' stamps: none matches a draw */
        memset(s_memo.stamp + s_memo.cap, 0, (size_t)(m->vertexCount - s_memo.cap) * 4u);
        s_memo.cap = m->vertexCount;
    }
    if (++s_memo.now == 0u) {
        if (s_memo.cap) {
            memset(s_memo.stamp, 0, (size_t)s_memo.cap * 4u);
        }
        s_memo.now = 1u;
    }
    const float (*mm)[4] = &mem[16];
    const float (*clip)[4] = p->clip == RD_VU_CLIP_SCISSOR ? &mem[20] : NULL;
    /* the index ranges doVuDraw (rd_replay_vu.c) draws */
    uint32_t last = p->firstBatch + p->batchCount;
    if (last > m->batchCount || p->batchCount == 0) {
        last = m->batchCount;
    }
    const bool extended = rd__vu_extended();
    int all = 1;
    if (p->clip != RD_VU_CLIP_SCISSOR) {
        const RdVuBatchRec *b0 = &m->batches[p->firstBatch], *b1 = &m->batches[last - 1u];
        const uint32_t end = rd__vu_sheet_end(m, b1->firstIndex + b1->indexCount, extended);
        if (end > b0->firstIndex) {
            all = rangeTris(m, b0->firstIndex, end, mm, clip, cmd, minArea, out, lbAll);
        }
        return all;
    }
    for (uint32_t b = p->firstBatch; b < last && all >= 0; b++) {
        const RdVuBatchRec *br = &m->batches[b];
        const uint32_t end = rd__vu_sheet_end(m, br->firstIndex + br->indexCount, extended);
        if (end > br->firstIndex) {
            const int a = rangeTris(m, br->firstIndex, end, mm, clip, cmd, minArea, out, lbAll);
            all = a < 0 ? -1 : (a && all);
        }
    }
    return all;
}
